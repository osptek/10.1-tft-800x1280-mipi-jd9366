/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "video_player.h"
#include "app_stream_adapter.h"
#include "ui_manager.h"
#include "photo_album_constants.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "driver/jpeg_decode.h"
#include <string.h>
#include "photo_album.h"
#include "esp_timer.h"
#include "slideshow_ctrl.h"

static const char *TAG = "video";

static struct {
    app_stream_adapter_handle_t adapter;
    void *buffer_a;
    void *buffer_b;
    video_state_t state;
    uint32_t width, height;
    bool playback_finished;
    esp_codec_dev_handle_t audio_dev;
    int current_volume;
    char current_file[256];  // Store current playing file
    bool has_error;          // Track error state
    esp_timer_handle_t finish_timer;
} s_video = {0};

static void video_finish_timer_cb(void *arg)
{
    // Restart slideshow timer and move to next media
    slideshow_ctrl_start();
    photo_album_next();
}

static esp_err_t video_frame_callback(uint8_t *buffer, uint32_t buffer_size,
                                     uint32_t width, uint32_t height,
                                     uint32_t frame_index, void *user_data)
{
    // Store frame dimensions for reference
    s_video.width = width;
    s_video.height = height;
    
    // Clear error state on successful frame callback
    s_video.has_error = false;
    
    // Directly pass JPEG decoded buffer to Canvas
    // Canvas will automatically handle cropping to display area
    return ui_manager_display_video_frame(buffer, width, height);
}

esp_err_t video_player_init(esp_codec_dev_handle_t audio_dev)
{
    if (s_video.adapter) return ESP_OK;
    
    s_video.audio_dev = audio_dev;
    s_video.current_volume = DEFAULT_AUDIO_VOLUME;
    s_video.has_error = false;
    memset(s_video.current_file, 0, sizeof(s_video.current_file));
    
    // Allocate larger buffers to support high-resolution videos (up to 1080P)
    // with margin for JPEG 16-byte alignment requirements
    size_t buffer_size = VIDEO_BUFFER_SIZE;
    size_t allocated_size_a, allocated_size_b;
    
    ESP_LOGI(TAG, "Allocating video buffers: %zu bytes each (supports up to %dx%d)", 
             buffer_size, MAX_VIDEO_WIDTH, MAX_VIDEO_HEIGHT);
    
    s_video.buffer_a = shared_jpeg_alloc_output_buffer(buffer_size, &allocated_size_a);
    s_video.buffer_b = shared_jpeg_alloc_output_buffer(buffer_size, &allocated_size_b);
    
    if (!s_video.buffer_a || !s_video.buffer_b) {
        ESP_LOGE(TAG, "Buffer allocation failed");
        if (s_video.buffer_a) shared_jpeg_free_buffer(s_video.buffer_a);
        if (s_video.buffer_b) shared_jpeg_free_buffer(s_video.buffer_b);
        return ESP_ERR_NO_MEM;
    }
    
    static void *buffers[2];
    buffers[0] = s_video.buffer_a;
    buffers[1] = s_video.buffer_b;
    
    app_stream_adapter_config_t config = {
        .frame_cb = video_frame_callback,
        .user_data = NULL,
        .decode_buffers = buffers,
        .buffer_count = 2,
        .buffer_size = buffer_size,
        .audio_dev = s_video.audio_dev,
        .jpeg_config = APP_STREAM_JPEG_CONFIG_DEFAULT_RGB565()
    };
    
    esp_err_t ret = app_stream_adapter_init(&config, &s_video.adapter);
    if (ret == ESP_OK) {
        s_video.state = VIDEO_STATE_STOPPED;
        ESP_LOGI(TAG, "Video player initialized %s audio support", 
                 s_video.audio_dev ? "with" : "without");
    }
    
    return ret;
}

esp_err_t video_player_play(const char *mp4_file)
{
    if (!s_video.adapter) return ESP_ERR_INVALID_STATE;
    
    // Store current file path
    strncpy(s_video.current_file, mp4_file, sizeof(s_video.current_file) - 1);
    s_video.current_file[sizeof(s_video.current_file) - 1] = '\0';
    
    // Clear error state
    s_video.has_error = false;
    
    // Extract audio only if audio device is available
    bool extract_audio = (s_video.audio_dev != NULL);
    esp_err_t ret = app_stream_adapter_set_file(s_video.adapter, mp4_file, extract_audio);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set MP4 file: %s", esp_err_to_name(ret));
        s_video.has_error = true;
        s_video.state = VIDEO_STATE_ERROR;
        return ret;
    }
    
    ESP_LOGI(TAG, "Playing MP4: %s %s audio", mp4_file, extract_audio ? "with" : "without");
    
    ret = app_stream_adapter_start(s_video.adapter);
    if (ret == ESP_OK) {
        s_video.state = VIDEO_STATE_PLAYING;
        s_video.playback_finished = false;
        ui_manager_switch_mode(UI_MODE_VIDEO);

        // Schedule automatic switch to next media after video duration
        uint32_t duration_ms = 0;
        if (app_stream_adapter_get_info(s_video.adapter, NULL, NULL, NULL, &duration_ms) == ESP_OK && duration_ms > 0) {
            // Add a small margin (500ms)
            uint64_t timeout_us = ((uint64_t)duration_ms + 500) * 1000ULL;

            // Create timer if not created
            if (s_video.finish_timer == NULL) {
                esp_timer_create_args_t t_args = {
                    .callback = video_finish_timer_cb,
                    .arg = NULL,
                    .name = "video_finish"
                };
                if (esp_timer_create(&t_args, &s_video.finish_timer) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to create finish timer");
                }
            }
            if (s_video.finish_timer) {
                esp_timer_stop(s_video.finish_timer);
                esp_timer_start_once(s_video.finish_timer, timeout_us);
                ESP_LOGI(TAG, "Finish timer started: %u ms", duration_ms + 500);
            }
        }
    } else {
        ESP_LOGE(TAG, "Failed to start MP4 playback: %s", esp_err_to_name(ret));
        s_video.has_error = true;
        s_video.state = VIDEO_STATE_ERROR;
    }
    
    return ret;
}

esp_err_t video_player_pause(void)
{
    if (s_video.state == VIDEO_STATE_PLAYING) {
        esp_err_t ret = app_stream_adapter_pause(s_video.adapter);
        if (ret == ESP_OK) {
            s_video.state = VIDEO_STATE_PAUSED;

            // Cancel finish timer if running，避免暂停后触发自动跳转
            if (s_video.finish_timer) {
                esp_timer_stop(s_video.finish_timer);
            }
        } else {
            ESP_LOGW(TAG, "Failed to pause video: %s", esp_err_to_name(ret));
            s_video.has_error = true;
            s_video.state = VIDEO_STATE_ERROR;
        }
        return ret;
    }
    return ESP_OK;
}

esp_err_t video_player_resume(void)
{
    if (s_video.state == VIDEO_STATE_PAUSED) {
        esp_err_t ret = app_stream_adapter_resume(s_video.adapter);
        if (ret == ESP_OK) {
            s_video.state = VIDEO_STATE_PLAYING;

            /* 重新启动 finish_timer，估算剩余时间(粗略) */
            uint32_t duration_ms = 0, fps = 0;
            app_stream_stats_t stats;
            if (app_stream_adapter_get_info(s_video.adapter, NULL, NULL, &fps, &duration_ms) == ESP_OK &&
                app_stream_adapter_get_stats(s_video.adapter, &stats) == ESP_OK &&
                fps > 0 && duration_ms > 0) {
                /* 已播放时长(ms) = 已处理帧数 * (1000/fps) */
                uint32_t elapsed_ms = (uint32_t)((float)stats.frames_processed * (1000.0f / fps));
                uint32_t remaining_ms = (duration_ms > elapsed_ms) ? (duration_ms - elapsed_ms) : 1000; // 至少 1s

                uint64_t timeout_us = ((uint64_t)remaining_ms + 500) * 1000ULL; // 再加 500ms 保险

                if (s_video.finish_timer == NULL) {
                    esp_timer_create_args_t t_args = {
                        .callback = video_finish_timer_cb,
                        .arg = NULL,
                        .name = "video_finish"
                    };
                    if (esp_timer_create(&t_args, &s_video.finish_timer) != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to create finish timer on resume");
                    }
                }

                if (s_video.finish_timer) {
                    esp_timer_stop(s_video.finish_timer);
                    esp_timer_start_once(s_video.finish_timer, timeout_us);
                    ESP_LOGI(TAG, "Finish timer restarted on resume: %u ms remaining", remaining_ms + 500);
                }
            }
        } else {
            ESP_LOGW(TAG, "Failed to resume video: %s", esp_err_to_name(ret));
            s_video.has_error = true;
            s_video.state = VIDEO_STATE_ERROR;
        }
        return ret;
    }
    return ESP_OK;
}

esp_err_t video_player_stop(void)
{
    if (s_video.adapter && s_video.state != VIDEO_STATE_STOPPED) {
        s_video.state = VIDEO_STATE_STOPPED;
        s_video.playback_finished = true;
        s_video.has_error = false;
        
        esp_err_t ret = app_stream_adapter_stop(s_video.adapter);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop adapter: %s", esp_err_to_name(ret));
        }

        // Cancel finish timer if running
        if (s_video.finish_timer) {
            esp_timer_stop(s_video.finish_timer);
        }
    }
    return ESP_OK;
}

video_state_t video_player_get_state(void)
{
    return s_video.state;
}

bool video_player_has_error(void)
{
    return s_video.has_error || (s_video.state == VIDEO_STATE_ERROR);
}

esp_err_t video_player_restart_current(void)
{
    if (strlen(s_video.current_file) == 0) {
        ESP_LOGE(TAG, "No current file to restart");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "Restarting current video: %s", s_video.current_file);
    
    // Stop current playback
    video_player_stop();
    
    // Small delay to ensure cleanup
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Restart playback
    return video_player_play(s_video.current_file);
}

bool video_player_is_finished(void)
{
    // Simple check: rely on finish_timer for accurate end detection
    return s_video.playback_finished;
}

esp_err_t video_player_deinit(void)
{
    video_player_stop();
    
    if (s_video.adapter) {
        app_stream_adapter_deinit(s_video.adapter);
        s_video.adapter = NULL;
    }
    
    if (s_video.buffer_a) {
        shared_jpeg_free_buffer(s_video.buffer_a);
        s_video.buffer_a = NULL;
    }
    
    if (s_video.buffer_b) {
        shared_jpeg_free_buffer(s_video.buffer_b);
        s_video.buffer_b = NULL;
    }
    
    return ESP_OK;
}

esp_err_t video_player_set_volume(int volume)
{
    if (volume < MIN_AUDIO_VOLUME) volume = MIN_AUDIO_VOLUME;
    if (volume > MAX_AUDIO_VOLUME) volume = MAX_AUDIO_VOLUME;
    
    s_video.current_volume = volume;
    
    if (s_video.audio_dev) {
        esp_err_t ret = esp_codec_dev_set_out_vol(s_video.audio_dev, volume);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Volume set to %d", volume);
        } else {
            ESP_LOGW(TAG, "Failed to set volume: %s", esp_err_to_name(ret));
        }
        return ret;
    }
    
    return ESP_OK;
}

int video_player_get_volume(void)
{
    return s_video.current_volume;
}

esp_err_t video_player_switch_file(const char *mp4_file)
{
    if (!s_video.adapter) {
        ESP_LOGE(TAG, "Video player not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!mp4_file) {
        ESP_LOGE(TAG, "Invalid file path");
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Switching to video: %s", mp4_file);
    
    // Store new file path
    strncpy(s_video.current_file, mp4_file, sizeof(s_video.current_file) - 1);
    s_video.current_file[sizeof(s_video.current_file) - 1] = '\0';
    
    // Cancel any existing finish timer
    if (s_video.finish_timer) {
        esp_timer_stop(s_video.finish_timer);
    }
    
    // Soft stop: stop playback but keep adapter/buffers alive
    // This will properly close audio device and reset state flags
    if (s_video.state != VIDEO_STATE_STOPPED) {
        esp_err_t ret = app_stream_adapter_stop(s_video.adapter);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop adapter for switch: %s", esp_err_to_name(ret));
        }
    }
    
    // Clear error state
    s_video.has_error = false;
    s_video.playback_finished = false;
    
    // Set new file (this will reset audio state flags in app_extractor_start)
    bool extract_audio = (s_video.audio_dev != NULL);
    esp_err_t ret = app_stream_adapter_set_file(s_video.adapter, mp4_file, extract_audio);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to switch to file %s: %s", mp4_file, esp_err_to_name(ret));
        s_video.has_error = true;
        s_video.state = VIDEO_STATE_ERROR;
        return ret;
    }
    
    // Restart playback immediately
    ret = app_stream_adapter_start(s_video.adapter);
    if (ret == ESP_OK) {
        s_video.state = VIDEO_STATE_PLAYING;
        
        // Setup finish timer for new video
        uint32_t duration_ms = 0;
        if (app_stream_adapter_get_info(s_video.adapter, NULL, NULL, NULL, &duration_ms) == ESP_OK && duration_ms > 0) {
            uint64_t timeout_us = ((uint64_t)duration_ms + 500) * 1000ULL;
            
            if (s_video.finish_timer == NULL) {
                esp_timer_create_args_t t_args = {
                    .callback = video_finish_timer_cb,
                    .arg = NULL,
                    .name = "video_finish"
                };
                if (esp_timer_create(&t_args, &s_video.finish_timer) != ESP_OK) {
                    ESP_LOGW(TAG, "Failed to create finish timer");
                }
            }
            
            if (s_video.finish_timer) {
                esp_timer_start_once(s_video.finish_timer, timeout_us);
                ESP_LOGI(TAG, "Finish timer restarted: %u ms", duration_ms + 500);
            }
        }
        
        ESP_LOGI(TAG, "Video switched successfully: %s", mp4_file);
    } else {
        ESP_LOGE(TAG, "Failed to start new video: %s", esp_err_to_name(ret));
        s_video.has_error = true;
        s_video.state = VIDEO_STATE_ERROR;
    }
    
    return ret;
} 