/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_manager.h"
#include "usb_status_ui.h"
#include "photo_album.h"
#include "video_player.h"
#include "ui_manager.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include <string.h>

// External SD card handle from BSP
extern sdmmc_card_t *bsp_sdcard;

// Cache SD card total capacity to avoid repeated calculation
static uint32_t s_cached_total_mb = 0;

static const char *TAG = "usb_mgr";

// Storage update task handle
static TaskHandle_t s_storage_update_task = NULL;

// Global USB manager state
static usb_manager_state_t s_usb_state = {
    .usb_active = false,
    .photo_album_paused = false,
    .video_was_playing = false,
    .current_media_type = MEDIA_TYPE_UNKNOWN
};

// Storage information update task
static void storage_update_task(void *pvParameters)
{
    const TickType_t update_interval = pdMS_TO_TICKS(2000); // Update every 2 seconds
    
    while (1) {
        // Only update when USB is active and UI is visible
        if (s_usb_state.usb_active && usb_status_ui_is_visible()) {
            esp_err_t ret = usb_manager_update_storage_display();
            if (ret != ESP_OK) {
                ESP_LOGD(TAG, "Storage display update failed: %s", esp_err_to_name(ret));
            }
        }
        
        vTaskDelay(update_interval);
    }
}

// USB status callback - handles USB connection state changes
static void usb_status_changed_cb(usb_msc_status_t status)
{
    switch (status) {
        case USB_MSC_CONNECTED:
        case USB_MSC_MOUNTED:
            ESP_LOGD(TAG, "USB connected - stopping all media playback");
            s_usb_state.usb_active = true;
            
            // Check current media type
            const image_file_info_t* current_info = photo_album_get_current_info();
            if (current_info) {
                s_usb_state.current_media_type = file_manager_get_media_type(current_info->full_path);
            } else {
                s_usb_state.current_media_type = MEDIA_TYPE_UNKNOWN;
            }
            
            // Handle video playback - pause to preserve position
            if (s_usb_state.current_media_type == MEDIA_TYPE_VIDEO) {
                video_state_t video_state = video_player_get_state();
                if (video_state == VIDEO_STATE_PLAYING) {
                    ESP_LOGI(TAG, "Pausing video playback for USB access");
                    s_usb_state.video_was_playing = true;
                    video_player_pause();  // Pause to preserve playback position
                } else if (video_state == VIDEO_STATE_PAUSED) {
                    ESP_LOGI(TAG, "Video was already paused");
                    s_usb_state.video_was_playing = false;
                } else {
                    s_usb_state.video_was_playing = false;
                }
                
                // Switch to image mode to avoid UI conflicts
                ui_manager_switch_mode(UI_MODE_IMAGE);
            } else {
                s_usb_state.video_was_playing = false;
                ESP_LOGI(TAG, "Current media is image, stopping slideshow");
            }
            
            // Always pause photo album (this stops slideshow timer and any video)
            s_usb_state.photo_album_paused = true;
            photo_album_pause_for_usb();  // Use USB-specific pause (no auto-resume)
            
            // Show USB status UI（如果还没显示）
            usb_status_ui_show(USB_UI_STATE_CONNECTED);
            break;

        case USB_MSC_DISCONNECTED:
        case USB_MSC_EJECTED:
            ESP_LOGD(TAG, "USB disconnected - resuming media playback");
            // Refresh photo album collection to reflect any file changes made via USB
            ESP_LOGI(TAG, "Refreshing photo album after USB disconnect");
            photo_album_refresh();

            s_usb_state.usb_active = false;
            
            if (s_usb_state.photo_album_paused) {
                usb_status_ui_hide();
                
                // Resume based on what was playing before USB connection
                if (s_usb_state.video_was_playing && s_usb_state.current_media_type == MEDIA_TYPE_VIDEO) {
                    ESP_LOGI(TAG, "Resuming video playback from paused position");
                    ui_manager_switch_mode(UI_MODE_VIDEO);
                    
                    // Resume the paused video from where it left off
                    esp_err_t ret = video_player_resume();
                    if (ret != ESP_OK) {
                        ESP_LOGW(TAG, "Failed to resume video: %s", esp_err_to_name(ret));
                        // If resume fails, try to restart as fallback
                        ret = video_player_restart_current();
                        if (ret != ESP_OK) {
                            ESP_LOGE(TAG, "Failed to restart video, moving to next media");
                            photo_album_next();
                        }
                    }
                } else if (s_usb_state.current_media_type == MEDIA_TYPE_VIDEO && !s_usb_state.video_was_playing) {
                    ESP_LOGI(TAG, "Video was paused before USB, keeping it paused");
                    ui_manager_switch_mode(UI_MODE_VIDEO);
                    // Video should remain paused - no action needed
                } else {
                    ESP_LOGI(TAG, "Resuming slideshow for images");
                    photo_album_resume();  // This will resume slideshow for images
                }
                
                s_usb_state.photo_album_paused = false;
                s_usb_state.video_was_playing = false;
                s_usb_state.current_media_type = MEDIA_TYPE_UNKNOWN;
            }
            break;

        case USB_MSC_ERROR:
            ESP_LOGW(TAG, "USB error occurred");
            usb_status_ui_show(USB_UI_STATE_ERROR);
            break;

        default:
            break;
    }
}

esp_err_t usb_manager_init(void)
{
    // Configure USB MSC
    usb_msc_config_t usb_config = {
        .enable_usb_msc = true,
        .auto_mount_on_connect = true,
        .mount_point = "/sdcard"
    };

    esp_err_t ret = usb_msc_init(&usb_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init USB MSC: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register status callback
    ret = usb_msc_register_status_callback(usb_status_changed_cb);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register USB callback: %s", esp_err_to_name(ret));
        usb_msc_deinit();
        return ret;
    }

    // Initialize USB status UI
    ret = usb_status_ui_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init USB UI: %s", esp_err_to_name(ret));
        usb_msc_deinit();
        return ret;
    }

    // Start USB MSC
    ret = usb_msc_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start USB MSC: %s", esp_err_to_name(ret));
        usb_status_ui_deinit();
        usb_msc_deinit();
        return ret;
    }

    // Create storage update task
    BaseType_t task_ret = xTaskCreate(
        storage_update_task,
        "usb_storage_update",
        2048,  // Stack size
        NULL,  // Parameters
        2,     // Priority (lower than main tasks)
        &s_storage_update_task
    );
    
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create storage update task");
        usb_msc_stop();
        usb_status_ui_deinit();
        usb_msc_deinit();
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGD(TAG, "USB manager initialized successfully");
    return ESP_OK;
}

esp_err_t usb_manager_deinit(void)
{
    // Delete storage update task first
    if (s_storage_update_task != NULL) {
        vTaskDelete(s_storage_update_task);
        s_storage_update_task = NULL;
        ESP_LOGD(TAG, "Storage update task deleted");
    }
    
    usb_status_ui_deinit();
    usb_msc_deinit();
    
    // Reset state
    memset(&s_usb_state, 0, sizeof(s_usb_state));
    s_usb_state.current_media_type = MEDIA_TYPE_UNKNOWN;
    
    ESP_LOGD(TAG, "USB manager deinitialized");
    return ESP_OK;
}

const usb_manager_state_t* usb_manager_get_state(void)
{
    return &s_usb_state;
}

esp_err_t usb_manager_get_storage_info(storage_info_t *storage_info)
{
    if (!storage_info) {
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get accurate file count from photo album
    storage_info->total_files = photo_album_get_total_count();
    
    // Use cached total capacity or calculate once
    if (s_cached_total_mb == 0) {
        // Get real SD card capacity via CSD (if available)
        if (bsp_sdcard && bsp_sdcard->csd.capacity > 0) {
            uint64_t total_bytes = (uint64_t)bsp_sdcard->csd.capacity * bsp_sdcard->csd.sector_size;
            s_cached_total_mb = (uint32_t)(total_bytes / (1024 * 1024));
        } else {
            // Fallback to 16GB estimate if SD card not available
            s_cached_total_mb = 16384;
        }
    }
    storage_info->total_mb = s_cached_total_mb;
    
    // Get real filesystem usage via esp_vfs_fat_info (works without <sys/statvfs.h>)
    uint64_t total_bytes = 0, free_bytes = 0;
    if (esp_vfs_fat_info("/sdcard", &total_bytes, &free_bytes) == ESP_OK) {
        uint64_t used_bytes = total_bytes - free_bytes;
        
        storage_info->used_mb = (uint32_t)(used_bytes / (1024 * 1024));
        
        // Use filesystem total if it's more reasonable than CSD (accounts for filesystem overhead)
        uint32_t fs_total_mb = (uint32_t)(total_bytes / (1024 * 1024));
        if (fs_total_mb > 0 && fs_total_mb < storage_info->total_mb) {
            storage_info->total_mb = fs_total_mb;
        }
    } else {
        // Fallback: estimate 2MB per file if filesystem query fails
        storage_info->used_mb = storage_info->total_files * 2;
    }
    
    // Ensure used doesn't exceed total
    if (storage_info->used_mb > storage_info->total_mb) {
        storage_info->used_mb = storage_info->total_mb;
    }
    
    return ESP_OK;
}

esp_err_t usb_manager_update_storage_display(void)
{
    if (!usb_status_ui_is_visible()) {
        return ESP_OK;  // No need to update if UI is not visible
    }
    
    storage_info_t storage_info;
    esp_err_t ret = usb_manager_get_storage_info(&storage_info);
    if (ret == ESP_OK) {
        usb_status_ui_update_storage_info(&storage_info);
    }
    
    return ret;
}

bool usb_manager_is_active(void)
{
    return s_usb_state.usb_active;
} 