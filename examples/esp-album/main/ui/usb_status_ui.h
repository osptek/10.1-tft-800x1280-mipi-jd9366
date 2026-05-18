/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "lvgl.h"
#include "usb_msc.h"

// Forward declaration to avoid circular dependency
typedef struct storage_info_s storage_info_t;

#ifdef __cplusplus
extern "C" {
#endif

// UI display states
typedef enum {
    USB_UI_STATE_HIDDEN,          // UI hidden, show photo album
    USB_UI_STATE_CONNECTED,       // USB connected screen
    USB_UI_STATE_TRANSFERRING,    // File transfer progress
    USB_UI_STATE_WIFI_HOTSPOT,    // WiFi hotspot information
    USB_UI_STATE_DUAL_MODE,       // USB + WiFi dual mode
    USB_UI_STATE_ERROR            // Error display
} usb_ui_state_t;

// Transfer progress info
typedef struct {
    int progress_percent;         // 0-100
    uint32_t files_found;         // Number of files found
    uint32_t transfer_speed_kbps; // Transfer speed in KB/s
    uint32_t time_remaining_sec;  // Estimated time remaining
    char status_text[64];         // Current operation text
} transfer_progress_t;

// WiFi info
typedef struct {
    char ssid[32];               // WiFi SSID
    char password[32];           // WiFi password
    char ip_address[16];         // IP address
    uint8_t connected_clients;   // Number of connected clients
} wifi_info_t;

// USB status UI functions
esp_err_t usb_status_ui_init(void);
esp_err_t usb_status_ui_deinit(void);

// Display control
esp_err_t usb_status_ui_show(usb_ui_state_t state);
esp_err_t usb_status_ui_hide(void);
bool usb_status_ui_is_visible(void);

// Status updates
esp_err_t usb_status_ui_update_transfer_progress(const transfer_progress_t *progress);
esp_err_t usb_status_ui_update_storage_info(const storage_info_t *storage);
esp_err_t usb_status_ui_update_wifi_info(const wifi_info_t *wifi);
esp_err_t usb_status_ui_show_error(const char *error_msg);

#ifdef __cplusplus
}
#endif 