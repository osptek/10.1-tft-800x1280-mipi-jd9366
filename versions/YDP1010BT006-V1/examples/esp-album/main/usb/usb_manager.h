/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "usb_msc.h"
#include "file_manager.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief USB manager state structure
 */
typedef struct {
    bool usb_active;                    // USB connection status
    bool photo_album_paused;            // Photo album pause status
    bool video_was_playing;             // Video playback status before USB
    media_type_t current_media_type;    // Current media type
} usb_manager_state_t;

/**
 * @brief Storage information structure
 */
typedef struct storage_info_s {
    uint32_t used_mb;                   // Used space in MB
    uint32_t total_mb;                  // Total space in MB
    uint32_t total_files;               // Total file count
} storage_info_t;

/**
 * @brief USB state callback function type
 */
typedef void (*usb_state_callback_t)(usb_msc_status_t status);

/**
 * @brief Button callback function type
 */
typedef void (*usb_button_callback_t)(void);

/**
 * @brief Initialize USB manager
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t usb_manager_init(void);

/**
 * @brief Deinitialize USB manager
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t usb_manager_deinit(void);

/**
 * @brief Get current USB manager state
 * 
 * @return Pointer to current state structure
 */
const usb_manager_state_t* usb_manager_get_state(void);

/**
 * @brief Get real-time storage information
 * 
 * @param storage_info Pointer to storage info structure to fill
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t usb_manager_get_storage_info(storage_info_t *storage_info);

/**
 * @brief Update storage information display
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t usb_manager_update_storage_display(void);

/**
 * @brief Check if USB is active
 * 
 * @return true if USB is active, false otherwise
 */
bool usb_manager_is_active(void);

#ifdef __cplusplus
}
#endif 