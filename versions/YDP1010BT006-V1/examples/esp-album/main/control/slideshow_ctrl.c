/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "slideshow_ctrl.h"
#include "esp_log.h"

static const char *TAG = "slideshow";

// Idle timeout before resuming slideshow (in ms)
#define IDLE_TIMEOUT_MS 3000

static struct {
    esp_timer_handle_t timer;
    esp_timer_handle_t idle_timer;
    slideshow_next_cb_t next_cb;
    uint32_t interval_ms;
    bool is_running;
    bool manual_control;
} s_slideshow;

static void slideshow_timer_callback(void *arg)
{
    if (s_slideshow.next_cb && s_slideshow.is_running && !s_slideshow.manual_control) {
        ESP_LOGD(TAG, "Auto next image");
        s_slideshow.next_cb();
    }
}

static void idle_timer_callback(void *arg)
{
    if (s_slideshow.manual_control) {
        s_slideshow.manual_control = false;
        ESP_LOGD(TAG, "Resume auto slideshow");
        
        // Restart slideshow timer
        if (s_slideshow.is_running) {
            esp_timer_start_periodic(s_slideshow.timer, s_slideshow.interval_ms * 1000);
        }
    }
}

esp_err_t slideshow_ctrl_init(slideshow_next_cb_t next_cb, uint32_t interval_ms)
{
    if (!next_cb) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_slideshow.next_cb = next_cb;
    s_slideshow.interval_ms = interval_ms;
    s_slideshow.is_running = false;
    s_slideshow.manual_control = false;
    
    // Create slideshow timer
    esp_timer_create_args_t timer_args = {
        .callback = slideshow_timer_callback,
        .name = "slideshow"
    };
    
    esp_err_t ret = esp_timer_create(&timer_args, &s_slideshow.timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create slideshow timer");
        return ret;
    }
    
    // Create idle timer
    timer_args.callback = idle_timer_callback;
    timer_args.name = "idle";
    
    ret = esp_timer_create(&timer_args, &s_slideshow.idle_timer);
    if (ret != ESP_OK) {
        esp_timer_delete(s_slideshow.timer);
        ESP_LOGE(TAG, "Failed to create idle timer");
        return ret;
    }
    
    ESP_LOGD(TAG, "Slideshow controller initialized (interval: %"PRIu32"ms)", interval_ms);
    return ESP_OK;
}

esp_err_t slideshow_ctrl_deinit(void)
{
    slideshow_ctrl_stop();
    
    if (s_slideshow.timer) {
        esp_timer_delete(s_slideshow.timer);
        s_slideshow.timer = NULL;
    }
    
    if (s_slideshow.idle_timer) {
        esp_timer_delete(s_slideshow.idle_timer);
        s_slideshow.idle_timer = NULL;
    }
    
    ESP_LOGD(TAG, "Slideshow controller deinitialized");
    return ESP_OK;
}

esp_err_t slideshow_ctrl_start(void)
{
    if (s_slideshow.is_running) {
        return ESP_OK; // Already running
    }
    
    s_slideshow.is_running = true;
    s_slideshow.manual_control = false;
    
    esp_err_t ret = esp_timer_start_periodic(s_slideshow.timer, s_slideshow.interval_ms * 1000);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Slideshow started");
    }
    
    return ret;
}

esp_err_t slideshow_ctrl_stop(void)
{
    if (!s_slideshow.is_running) {
        return ESP_OK; // Already stopped
    }
    
    s_slideshow.is_running = false;
    s_slideshow.manual_control = false;
    
    esp_timer_stop(s_slideshow.timer);
    esp_timer_stop(s_slideshow.idle_timer);
    
    ESP_LOGD(TAG, "Slideshow stopped");
    return ESP_OK;
}

esp_err_t slideshow_ctrl_pause(void)
{
    if (!s_slideshow.is_running || s_slideshow.manual_control) {
        return ESP_OK; // Already paused or in manual mode
    }
    
    s_slideshow.manual_control = true;
    esp_timer_stop(s_slideshow.timer);
    
    // Start idle timer to resume after timeout
    esp_timer_start_once(s_slideshow.idle_timer, IDLE_TIMEOUT_MS * 1000);
    
    ESP_LOGD(TAG, "Slideshow paused");
    return ESP_OK;
}

esp_err_t slideshow_ctrl_resume(void)
{
    if (!s_slideshow.is_running || !s_slideshow.manual_control) {
        return ESP_OK; // Not paused
    }
    
    s_slideshow.manual_control = false;
    esp_timer_stop(s_slideshow.idle_timer);
    
    esp_err_t ret = esp_timer_start_periodic(s_slideshow.timer, s_slideshow.interval_ms * 1000);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Slideshow resumed");
    }
    
    return ret;
}

esp_err_t slideshow_ctrl_set_interval(uint32_t interval_ms)
{
    s_slideshow.interval_ms = interval_ms;
    
    // If running, restart with new interval
    if (s_slideshow.is_running && !s_slideshow.manual_control) {
        esp_timer_stop(s_slideshow.timer);
        esp_timer_start_periodic(s_slideshow.timer, interval_ms * 1000);
    }
    
    ESP_LOGD(TAG, "Slideshow interval set to %"PRIu32"ms", interval_ms);
    return ESP_OK;
}

esp_err_t slideshow_ctrl_manual_trigger(void)
{
    if (!s_slideshow.is_running) {
        return ESP_ERR_INVALID_STATE;
    }
    
    // Switch to manual control mode
    slideshow_ctrl_pause();
    
    // Restart idle timer
    esp_timer_stop(s_slideshow.idle_timer);
    esp_timer_start_once(s_slideshow.idle_timer, IDLE_TIMEOUT_MS * 1000);
    
    ESP_LOGD(TAG, "Manual trigger");
    return ESP_OK;
}

bool slideshow_ctrl_is_running(void)
{
    return s_slideshow.is_running;
} 