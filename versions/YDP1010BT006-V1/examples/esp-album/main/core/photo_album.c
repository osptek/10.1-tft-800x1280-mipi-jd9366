/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "photo_album.h"
#include "photo_album_constants.h"
#include "file_manager.h"
#include "image_decoder.h"
#include "image_processor.h"
#include "ui_manager.h"
#include "slideshow_ctrl.h"
#include "video_player.h"
#include "app_stream_adapter.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"

static const char *TAG = "album";
static photo_album_t s_album;
static decoded_image_t s_current_image = {0};
static decoded_image_t s_processed_image = {0};

// Pause reason tracking
static enum {
    PAUSE_REASON_NONE,
    PAUSE_REASON_USER,      // User interaction pause (auto-resume after timeout)
    PAUSE_REASON_USB        // USB connection pause (manual resume required)
} s_pause_reason = PAUSE_REASON_NONE;

// Forward declarations
static void ui_event_handler(ui_event_t event, void *user_data);
static void slideshow_next_callback(void);
static esp_err_t load_and_display_media(int index);
static esp_err_t load_and_display_image(int index);

// MEMORY POOL IMPLEMENTATION

static esp_err_t memory_pool_init(memory_pool_t *pool)
{
    if (!pool) {
        return ESP_ERR_INVALID_ARG;
    }

    pool->mutex = xSemaphoreCreateMutex();
    if (!pool->mutex) {
        ESP_LOGE(TAG, "Failed to create memory pool mutex");
        return ESP_ERR_NO_MEM;
    }

    pool->pool_size = MEMORY_POOL_SIZE;
    pool->pool_buffer = heap_caps_malloc(pool->pool_size, MALLOC_CAP_SPIRAM);
    if (!pool->pool_buffer) {
        ESP_LOGE(TAG, "Failed to allocate memory pool: %zu bytes", pool->pool_size);
        vSemaphoreDelete(pool->mutex);
        return ESP_ERR_NO_MEM;
    }

    pool->used_size = 0;
    pool->is_allocated = true;

    ESP_LOGD(TAG, "Memory pool initialized: %zu bytes", pool->pool_size);
    return ESP_OK;
}

static void memory_pool_deinit(memory_pool_t *pool)
{
    if (!pool) return;

    if (pool->mutex) {
        xSemaphoreTake(pool->mutex, portMAX_DELAY);
    }

    if (pool->pool_buffer) {
        free(pool->pool_buffer);
        pool->pool_buffer = NULL;
    }

    pool->used_size = 0;
    pool->is_allocated = false;

    if (pool->mutex) {
        xSemaphoreGive(pool->mutex);
        vSemaphoreDelete(pool->mutex);
        pool->mutex = NULL;
    }

    ESP_LOGD(TAG, "Memory pool deinitialized");
}

static void* memory_pool_alloc(memory_pool_t *pool, size_t size)
{
    if (!pool || !pool->is_allocated || size == 0) {
        return NULL;
    }

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    // Reset pool for each new allocation (single operation at a time)
    pool->used_size = 0;

    if (size > pool->pool_size) {
        ESP_LOGE(TAG, "Requested size %zu exceeds pool size %zu", size, pool->pool_size);
        xSemaphoreGive(pool->mutex);
        return NULL;
    }

    void *ptr = pool->pool_buffer;
    pool->used_size = size;

    ESP_LOGD(TAG, "Memory pool allocated: %zu bytes", size);
    xSemaphoreGive(pool->mutex);
    return ptr;
}

static void memory_pool_reset(memory_pool_t *pool)
{
    if (!pool || !pool->is_allocated) return;

    xSemaphoreTake(pool->mutex, portMAX_DELAY);
    pool->used_size = 0;
    ESP_LOGD(TAG, "Memory pool reset");
    xSemaphoreGive(pool->mutex);
}

// FILE VALIDATION FUNCTIONS

static bool validate_file_for_decoding(const image_file_info_t *file_info)
{
    // Check file size limits
    if (file_info->file_size > 10 * 1024 * 1024) {  // >10MB suspicious
        ESP_LOGW(TAG, "File too large: %s (%zu bytes)", file_info->filename, file_info->file_size);
        return false;
    }

    if (file_info->file_size < 100) {  // <100 bytes too small
        ESP_LOGW(TAG, "File too small: %s (%zu bytes)", file_info->filename, file_info->file_size);
        return false;
    }

    return true;
}

// IMAGE PROCESSING FUNCTIONS

static bool image_needs_processing(uint32_t width, uint32_t height)
{
    return (width > SCREEN_WIDTH || height > SCREEN_HEIGHT);
}

static esp_err_t process_image_for_display(const decoded_image_t *input, decoded_image_t *output)
{
    process_params_t params;
    scale_mode_t mode;
    
    if (input->width >= input->height) {
        mode = (input->width <= SCREEN_WIDTH && input->height <= SCREEN_HEIGHT) ? 
               SCALE_MODE_CENTER : SCALE_MODE_CROP_ONLY;
    } else {
        mode = (input->width <= SCREEN_WIDTH && input->height <= SCREEN_HEIGHT) ? 
               SCALE_MODE_CENTER : SCALE_MODE_FIT;
    }
    
    esp_err_t ret = image_processor_calculate_params(input->width, input->height,
                                                    SCREEN_WIDTH, SCREEN_HEIGHT,
                                                    mode, &params);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to calculate processing parameters");
        return ret;
    }
    
    return image_processor_process(input, output, &params);
}

// MAIN IMAGE LOADING FUNCTION

static esp_err_t load_and_display_image(int index)
{
    if (!s_album.collection || index < 0 || index >= s_album.collection->total_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    const image_file_info_t *file_info = &s_album.collection->files[index];
    
    // Early file validation
    if (!validate_file_for_decoding(file_info)) {
        ESP_LOGW(TAG, "Skipping invalid file: %s", file_info->filename);
        return ESP_ERR_INVALID_ARG;
    }

    ui_manager_show_loading();
    
    xSemaphoreTake(s_album.mutex, portMAX_DELAY);

    // Clear previous images
    if (s_current_image.rgb_data) {
        image_decoder_free_image(&s_current_image);
    }
    if (s_processed_image.rgb_data) {
        image_decoder_free_image(&s_processed_image);
    }

    esp_err_t ret = ESP_OK;
    uint8_t *file_data = NULL;
    size_t file_size = 0;

    // Load file data
    ret = file_manager_load_image(file_info->full_path, &file_data, &file_size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to load file: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Decode image directly (no caching)
    ret = image_decoder_decode(file_data, file_size, file_info->format, &s_current_image);
    free(file_data);
    file_data = NULL;
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to decode image %s: %s", file_info->filename, esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGD(TAG, "Image decoded: %dx%d, size: %zu B", s_current_image.width, s_current_image.height, s_current_image.data_size);

    // Process image if scaling/cropping is required
    decoded_image_t *display_image = &s_current_image;
    if (image_needs_processing(s_current_image.width, s_current_image.height)) {
        ret = process_image_for_display(&s_current_image, &s_processed_image);
        if (ret == ESP_OK) {
            display_image = &s_processed_image;
            ESP_LOGD(TAG, "Image processed: %dx%d -> %dx%d", 
                     s_current_image.width, s_current_image.height,
                     s_processed_image.width, s_processed_image.height);
        } else {
            ESP_LOGE(TAG, "Failed to process image: %s", esp_err_to_name(ret));
            goto cleanup;
        }
    }

    // Display image
    ret = ui_manager_display_image(display_image);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to display image: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    // Ensure slideshow timer is running (may have been stopped for video)
    if (!slideshow_ctrl_is_running() && s_pause_reason == PAUSE_REASON_NONE) {
        slideshow_ctrl_start();
    }

    // Update collection index and progress display
    s_album.collection->current_index = index;
    ui_manager_update_progress(index, s_album.collection->total_count);
    
    ESP_LOGD(TAG, "Image displayed successfully: %s (%d/%d)", 
             file_info->filename, index + 1, s_album.collection->total_count);

cleanup:
    if (file_data) {
        free(file_data);
    }
    
    ui_manager_hide_loading();
    xSemaphoreGive(s_album.mutex);
    
    return ret;
}

// UI EVENT HANDLERS

static void ui_event_handler(ui_event_t event, void *user_data)
{
    // Cache current media type to avoid repeated lookups
    media_type_t current_media_type = MEDIA_TYPE_UNKNOWN;
    if (s_album.collection && s_album.collection->current_index >= 0 && 
        s_album.collection->current_index < s_album.collection->total_count) {
        current_media_type = file_manager_get_media_type(
            s_album.collection->files[s_album.collection->current_index].full_path);
    }
    
    switch (event) {
        case UI_EVENT_SWIPE_LEFT:
            slideshow_ctrl_manual_trigger();
            photo_album_next();
            break;
            
        case UI_EVENT_SWIPE_RIGHT:
            slideshow_ctrl_manual_trigger();
            photo_album_prev();
            break;
            
        case UI_EVENT_SWIPE_UP:
            // Volume up (MP4 mode only)
            if (current_media_type == MEDIA_TYPE_VIDEO) {
                int current_volume = video_player_get_volume();
                int new_volume = current_volume + VOLUME_ADJUSTMENT_STEP;
                if (new_volume > MAX_AUDIO_VOLUME) new_volume = MAX_AUDIO_VOLUME;
                video_player_set_volume(new_volume);
                ui_manager_show_volume(new_volume);
                ESP_LOGD(TAG, "Volume up: %d -> %d", current_volume, new_volume);
            }
            break;
            
        case UI_EVENT_SWIPE_DOWN:
            // Volume down (MP4 mode only)
            if (current_media_type == MEDIA_TYPE_VIDEO) {
                int current_volume = video_player_get_volume();
                int new_volume = current_volume - VOLUME_ADJUSTMENT_STEP;
                if (new_volume < MIN_AUDIO_VOLUME) new_volume = MIN_AUDIO_VOLUME;
                video_player_set_volume(new_volume);
                ui_manager_show_volume(new_volume);
                ESP_LOGD(TAG, "Volume down: %d -> %d", current_volume, new_volume);
            }
            break;
            
        case UI_EVENT_LONG_PRESS:
            /* Stop slideshow timer when settings panel opens to avoid auto switching */
            ui_manager_show_settings(s_album.slideshow.interval_ms);
            slideshow_ctrl_stop();
            break;
            
        case UI_EVENT_TAP:
            if (current_media_type == MEDIA_TYPE_VIDEO) {
                video_state_t state = video_player_get_state();
                if (state == VIDEO_STATE_PLAYING) {
                    video_player_pause();
                    slideshow_ctrl_pause();
                } else if (state == VIDEO_STATE_PAUSED) {
                    video_player_resume();
                }
            }
            break;
            
        case UI_EVENT_SETTINGS_CLOSE:
            if (ui_manager_get_selected_interval() != s_album.slideshow.interval_ms) {
                s_album.slideshow.interval_ms = ui_manager_get_selected_interval();
                slideshow_ctrl_set_interval(s_album.slideshow.interval_ms);
                ESP_LOGD(TAG, "Slideshow interval updated: %"PRIu32"ms", s_album.slideshow.interval_ms);
            }
            ui_manager_hide_settings();
            /* Restart slideshow timer after settings panel is closed */
            slideshow_ctrl_start();
            break;
            
        case UI_EVENT_SETTINGS_CANCEL:
            // Hide settings panel without saving changes
            ui_manager_hide_settings();
            /* If user cancels, resume slideshow timer as well */
            slideshow_ctrl_start();
            break;
            
        default:
            break;
    }
}

static void slideshow_next_callback(void)
{
    photo_album_next();
}

static esp_err_t load_and_display_media(int index)
{
    if (!s_album.collection || index < 0 || index >= s_album.collection->total_count) {
        return ESP_ERR_INVALID_ARG;
    }

    // Add retry protection to avoid infinite loops
    int max_retries = s_album.collection->total_count;
    int retry_count = 0;
    int current_index = index;
    
    // Check current state to optimize transitions
    video_state_t video_state = video_player_get_state();
    bool is_currently_playing_video = (video_state == VIDEO_STATE_PLAYING || video_state == VIDEO_STATE_PAUSED);
    
    while (retry_count < max_retries) {
        media_type_t media_type = file_manager_get_media_type(s_album.collection->files[current_index].full_path);
        
        if (media_type == MEDIA_TYPE_VIDEO) {
            esp_err_t ret;
            
            if (is_currently_playing_video) {
                // Video → Video: Use soft switch (no UI mode change, no loading screen)
                ret = video_player_switch_file(s_album.collection->files[current_index].full_path);
                ESP_LOGI(TAG, "Soft video switch to: %s", s_album.collection->files[current_index].filename);
            } else {
                // Image → Video: Full initialization
                ui_manager_show_loading();
                ui_manager_switch_mode(UI_MODE_VIDEO);
                
                // Stop slideshow timer while video is playing
                slideshow_ctrl_stop();
                
                ret = video_player_play(s_album.collection->files[current_index].full_path);
                ui_manager_hide_loading();
            }
            
            if (ret == ESP_OK) {
                s_album.collection->current_index = current_index;
                ui_manager_update_progress(current_index, s_album.collection->total_count);
                ESP_LOGD(TAG, "Video started: %s (%d/%d)", 
                         s_album.collection->files[current_index].filename, 
                         current_index + 1, s_album.collection->total_count);
            } else {
                ESP_LOGE(TAG, "Failed to start video: %s", esp_err_to_name(ret));
                if (!is_currently_playing_video) {
                    ui_manager_switch_mode(UI_MODE_IMAGE);
                }
                // Try next file
                current_index = (current_index + 1) % s_album.collection->total_count;
                retry_count++;
                continue;
            }
            
            return ret;
        } else if (media_type == MEDIA_TYPE_IMAGE) {
            // Video → Image: Stop video player completely (already done by caller for next/prev navigation)
            
            ui_manager_switch_mode(UI_MODE_IMAGE);
            esp_err_t ret = load_and_display_image(current_index);
            
            if (ret == ESP_OK) {
                return ESP_OK;
            } else if (ret == ESP_ERR_NOT_SUPPORTED || ret == ESP_ERR_INVALID_ARG) {
                // Skip unsupported images and try next one
                ESP_LOGW(TAG, "Skipping unsupported image: %s, trying next...", 
                         s_album.collection->files[current_index].filename);
                current_index = (current_index + 1) % s_album.collection->total_count;
                retry_count++;
                continue;
            } else {
                // Other errors, also try next image
                ESP_LOGW(TAG, "Error loading image: %s, trying next...", 
                         s_album.collection->files[current_index].filename);
                current_index = (current_index + 1) % s_album.collection->total_count;
                retry_count++;
                continue;
            }
        } else {
            // Unknown media type, skip
            ESP_LOGW(TAG, "Unknown media type for: %s, skipping...", 
                     s_album.collection->files[current_index].filename);
            current_index = (current_index + 1) % s_album.collection->total_count;
            retry_count++;
            continue;
        }
    }
    
    ESP_LOGE(TAG, "Failed to load any media after trying all %d files", max_retries);
    return ESP_ERR_NOT_FOUND;
}

// PUBLIC API IMPLEMENTATION

esp_err_t photo_album_init(void)
{
    if (s_album.initialized) {
        return ESP_OK;
    }
    
    memset(&s_album, 0, sizeof(photo_album_t));
    
    s_album.mutex = xSemaphoreCreateMutex();
    if (!s_album.mutex) {
        return ESP_ERR_NO_MEM;
    }

    // Initialize memory pool
    esp_err_t ret = memory_pool_init(&s_album.memory_pool);
    if (ret != ESP_OK) {
        goto cleanup;
    }

    ret = shared_jpeg_decoder_init();
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
    ret = file_manager_init();
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
    // Initialize image decoder with limited resolution
    decoder_config_t decoder_config = {
        .max_width = MAX_DECODE_WIDTH,
        .max_height = MAX_DECODE_HEIGHT,
        .use_psram = true
    };
    ret = image_decoder_init(&decoder_config);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
    ret = image_processor_init();
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
    ret = ui_manager_init(ui_event_handler, NULL);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
    ret = slideshow_ctrl_init(slideshow_next_callback, DEFAULT_SLIDESHOW_MS);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
    // Initialize audio codec for MP4 playback
    ESP_LOGI(TAG, "Initializing audio codec...");
    esp_codec_dev_handle_t audio_dev = bsp_audio_codec_speaker_init();
    if (audio_dev == NULL) {
        ESP_LOGW(TAG, "Failed to initialize audio codec, MP4 will play without audio");
    } else {
        ESP_LOGI(TAG, "Audio codec initialized successfully");
        esp_codec_dev_set_out_vol(audio_dev, DEFAULT_AUDIO_VOLUME);
    }
    
    ret = video_player_init(audio_dev);
    if (ret != ESP_OK) {
        goto cleanup;
    }
    
        s_album.collection = malloc(sizeof(photo_collection_t));
    if (!s_album.collection) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    
        s_album.collection->files = malloc(sizeof(image_file_info_t) * MAX_FILES_COUNT);
    if (!s_album.collection->files) {
        ret = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    
    s_album.collection->total_count = 0;
    s_album.collection->current_index = 0;
    s_album.slideshow.interval_ms = DEFAULT_SLIDESHOW_MS;
    s_album.initialized = true;
    
    ESP_LOGI(TAG, "Photo album initialized with unified memory pool (%zu bytes)", MEMORY_POOL_SIZE);
    return ESP_OK;

cleanup:
    if (s_album.collection) {
        if (s_album.collection->files) {
        free(s_album.collection->files);
        }
        free(s_album.collection);
    }
    memory_pool_deinit(&s_album.memory_pool);
    if (s_album.mutex) {
        vSemaphoreDelete(s_album.mutex);
    }
    return ret;
}

esp_err_t photo_album_start(void)
{
    if (!s_album.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t ret = file_manager_scan_images(PHOTO_BASE_PATH, s_album.collection);
    if (ret != ESP_OK || s_album.collection->total_count == 0) {
        ESP_LOGE(TAG, "No images found in %s", PHOTO_BASE_PATH);
        return ESP_ERR_NOT_FOUND;
    }
    
    ESP_LOGI(TAG, "Found %d media files", s_album.collection->total_count);
    esp_err_t ret2 = load_and_display_media(0);
    if (ret2 == ESP_OK) {
        slideshow_ctrl_start();
    }
    return ret2;
}

esp_err_t photo_album_deinit(void)
{
    if (!s_album.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    slideshow_ctrl_stop();
    
    xSemaphoreTake(s_album.mutex, portMAX_DELAY);
    
    // Clean up images  
    if (s_current_image.rgb_data) {
        image_decoder_free_image(&s_current_image);
    }
    if (s_processed_image.rgb_data) {
        image_decoder_free_image(&s_processed_image);
    }
    
    // Clean up collection memory
    if (s_album.collection) {
        if (s_album.collection->files) {
            free(s_album.collection->files);
        }
        free(s_album.collection);
        s_album.collection = NULL;
    }
    
    // Clean up memory pool
    memory_pool_deinit(&s_album.memory_pool);
    
    s_album.initialized = false;
    
    xSemaphoreGive(s_album.mutex);
    vSemaphoreDelete(s_album.mutex);
    s_album.mutex = NULL;
    
    ESP_LOGI(TAG, "Photo album deinitialized");
    return ESP_OK;
}

esp_err_t photo_album_refresh(void)
{
    if (!s_album.initialized) {
        ESP_LOGW(TAG, "Photo album not initialized, cannot refresh");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Refreshing photo album...");
    
    xSemaphoreTake(s_album.mutex, portMAX_DELAY);
    
    // Remember current file info
    char current_filename[MAX_FILENAME_LEN] = {0};
    int old_index = s_album.collection->current_index;
    
    if (old_index >= 0 && old_index < s_album.collection->total_count) {
        snprintf(current_filename, sizeof(current_filename), "%s", 
                 s_album.collection->files[old_index].filename);
    }
    
    // Rescan directory into existing collection structure
    esp_err_t ret = file_manager_scan_images(PHOTO_BASE_PATH, s_album.collection);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to rescan directory after refresh: %s", esp_err_to_name(ret));
        xSemaphoreGive(s_album.mutex);
        return ret;
    }
    
    // Try to find the same file again
    int new_index = 0;
    if (strlen(current_filename) > 0) {
        for (int i = 0; i < s_album.collection->total_count; i++) {
            if (strcmp(s_album.collection->files[i].filename, current_filename) == 0) {
                new_index = i;
                break;
            }
        }
    }
    
    // Update current index (ensure within bounds)
    if (s_album.collection->total_count > 0) {
        s_album.collection->current_index = new_index % s_album.collection->total_count;
    } else {
        s_album.collection->current_index = 0;
    }
    
    ESP_LOGI(TAG, "Photo album refreshed: %d files found", s_album.collection->total_count);
    ESP_LOGI(TAG, "Current index updated from %d to %d", old_index, s_album.collection->current_index);
    
    xSemaphoreGive(s_album.mutex);
    
    // If slideshow is running, load the current image to update display
    if (!photo_album_is_paused()) {
        load_and_display_media(s_album.collection->current_index);
    }
    
    return ESP_OK;
}

esp_err_t photo_album_next(void)
{
    if (!s_album.initialized || !s_album.collection || s_album.collection->total_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Check if we need to stop video before switching
    video_state_t video_state = video_player_get_state();
    if (video_state == VIDEO_STATE_PLAYING || video_state == VIDEO_STATE_PAUSED) {
        ui_manager_show_loading();
        ui_manager_switch_mode(UI_MODE_IMAGE);
        video_player_stop();
    }
    
    int next_index = (s_album.collection->current_index + 1) % s_album.collection->total_count;
    return load_and_display_media(next_index);
}

esp_err_t photo_album_prev(void)
{
    if (!s_album.initialized || !s_album.collection || s_album.collection->total_count == 0) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check if we need to stop video before switching
    video_state_t video_state = video_player_get_state();
    if (video_state == VIDEO_STATE_PLAYING || video_state == VIDEO_STATE_PAUSED) {
        ui_manager_show_loading();
        ui_manager_switch_mode(UI_MODE_IMAGE);
        video_player_stop();
    }

    // Add retry protection for previous navigation as well
    int max_retries = s_album.collection->total_count;
    int retry_count = 0;
    int prev_index = (s_album.collection->current_index - 1 + s_album.collection->total_count) % s_album.collection->total_count;
    
    while (retry_count < max_retries) {
        esp_err_t ret = load_and_display_media(prev_index);
        
        if (ret == ESP_OK) {
            return ESP_OK;
        } else if (ret == ESP_ERR_NOT_SUPPORTED || ret == ESP_ERR_INVALID_ARG) {
            // Skip unsupported images and try previous one
            ESP_LOGW(TAG, "Skipping unsupported image in prev navigation, trying previous...");
            prev_index = (prev_index - 1 + s_album.collection->total_count) % s_album.collection->total_count;
            retry_count++;
            continue;
        } else {
            // Other errors, also try previous image
            ESP_LOGW(TAG, "Error loading image in prev navigation, trying previous...");
            prev_index = (prev_index - 1 + s_album.collection->total_count) % s_album.collection->total_count;
            retry_count++;
            continue;
        }
    }
    
    ESP_LOGE(TAG, "Failed to load any media in prev navigation after trying all %d files", max_retries);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t photo_album_goto(int index)
{
    if (!s_album.initialized || !s_album.collection || index < 0 || index >= s_album.collection->total_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    return load_and_display_media(index);
}

esp_err_t photo_album_set_interval(uint32_t interval_ms)
{
    if (!s_album.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_album.slideshow.interval_ms = interval_ms;
    return slideshow_ctrl_set_interval(interval_ms);
}

int photo_album_get_total_count(void)
{
    if (!s_album.initialized || !s_album.collection) {
        return 0;
    }
    return s_album.collection->total_count;
}

int photo_album_get_current_index(void)
{
    if (!s_album.initialized || !s_album.collection) {
        return -1;
    }
    return s_album.collection->current_index;
}

const image_file_info_t* photo_album_get_current_info(void)
{
    if (!s_album.initialized || !s_album.collection || 
        s_album.collection->current_index < 0 || 
        s_album.collection->current_index >= s_album.collection->total_count) {
        return NULL;
    }
    return &s_album.collection->files[s_album.collection->current_index];
}

esp_err_t photo_album_pause(void)
{
    if (!s_album.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
        s_pause_reason = PAUSE_REASON_USER;
    return slideshow_ctrl_pause();
}

esp_err_t photo_album_pause_for_usb(void)
{
    if (!s_album.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    s_pause_reason = PAUSE_REASON_USB;
    esp_err_t ret = slideshow_ctrl_stop();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Photo album paused for USB connection (timer stopped)");
    }
    return ret;
}

esp_err_t photo_album_resume(void)
{
    if (!s_album.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // If paused for USB, restart slideshow timer instead of resume
    if (s_pause_reason == PAUSE_REASON_USB) {
        s_pause_reason = PAUSE_REASON_NONE;
        esp_err_t ret = slideshow_ctrl_start();
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Photo album resumed after USB disconnect");
        }
        return ret;
    }
    
    s_pause_reason = PAUSE_REASON_NONE;
    esp_err_t ret = slideshow_ctrl_resume();
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Photo album resumed");
    }
    return ret;
}

bool photo_album_is_paused(void)
{
    if (!s_album.initialized) {
        return true;
    }
    return !slideshow_ctrl_is_running();
} 