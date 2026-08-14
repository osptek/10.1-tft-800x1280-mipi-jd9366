/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_log.h"
#include "stdbool.h"
#ifdef __cplusplus
extern "C" {
#endif

// USB MSC status
typedef enum {
    USB_MSC_DISCONNECTED,     // USB not connected
    USB_MSC_CONNECTED,        // USB connected but not mounted
    USB_MSC_MOUNTED,          // USB mounted and accessible
    USB_MSC_EJECTED,          // USB ejected by host
    USB_MSC_ERROR             // USB error state
} usb_msc_status_t;

// USB MSC configuration
typedef struct {
    bool enable_usb_msc;      // Enable USB mass storage
    bool auto_mount_on_connect; // Auto mount when USB connected
    const char *mount_point;  // Mount point path
} usb_msc_config_t;

// USB MSC callbacks
typedef void (*usb_status_callback_t)(usb_msc_status_t status);

// USB MSC management functions
esp_err_t usb_msc_init(const usb_msc_config_t *config);
esp_err_t usb_msc_start(void);
esp_err_t usb_msc_stop(void);
esp_err_t usb_msc_deinit(void);

// USB status functions
usb_msc_status_t usb_msc_get_status(void);
bool usb_msc_is_connected(void);
bool usb_msc_is_mounted(void);
bool usb_msc_is_ejected(void);

// Callback management
esp_err_t usb_msc_register_status_callback(usb_status_callback_t callback);
esp_err_t usb_msc_unregister_status_callback(void);

// Storage access coordination
esp_err_t usb_msc_request_storage_access(void);
void usb_msc_release_storage_access(void);
bool usb_msc_is_storage_busy(void);

#ifdef __cplusplus
}
#endif 