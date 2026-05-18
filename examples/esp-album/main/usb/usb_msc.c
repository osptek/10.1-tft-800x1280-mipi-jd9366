/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_msc.h"
#include "file_manager.h"
#include "tinyusb.h"
#include "tusb_msc_storage.h"
#include "esp_vfs_fat.h"
#include "bsp/esp-bsp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"

static const char *TAG = "usb_msc";

// External SD card handle from BSP
extern sdmmc_card_t *bsp_sdcard;

// Global state
static struct {
    usb_msc_status_t status;
    usb_status_callback_t status_callback;
    usb_msc_config_t config;
    SemaphoreHandle_t storage_mutex;
    bool storage_busy;
    bool initialized;
    bool usb_connected;  // Track physical USB connection
    esp_timer_handle_t status_check_timer;  // Timer for periodic status check
} s_usb_msc = {
    .status = USB_MSC_DISCONNECTED,
    .status_callback = NULL,
    .storage_mutex = NULL,
    .storage_busy = false,
    .initialized = false,
    .usb_connected = false,
    .status_check_timer = NULL
};

// Forward declarations
static void usb_mount_status_changed_cb(tinyusb_msc_event_t *event);
static void update_status(usb_msc_status_t new_status);
static void usb_status_check_timer_cb(void *arg);

// Timer callback to periodically check USB connection status
static void usb_status_check_timer_cb(void *arg)
{
    (void)arg;
    
    // Check if TinyUSB is connected by checking if device is configured
    bool currently_connected = tud_connected() && tud_ready();
    
    if (currently_connected != s_usb_msc.usb_connected) {
        s_usb_msc.usb_connected = currently_connected;
        
        if (currently_connected) {
            ESP_LOGI(TAG, "USB connected detected");
            update_status(USB_MSC_CONNECTED);
        } else {
            ESP_LOGI(TAG, "USB disconnected detected");
            update_status(USB_MSC_DISCONNECTED);
        }
    }
}

static void update_status(usb_msc_status_t new_status)
{
    if (s_usb_msc.status != new_status) {
        usb_msc_status_t old_status = s_usb_msc.status;
        s_usb_msc.status = new_status;
        
        ESP_LOGI(TAG, "USB status changed: %d -> %d", old_status, new_status);
        
        if (s_usb_msc.status_callback) {
            s_usb_msc.status_callback(new_status);
        }
    }
}

static void usb_mount_status_changed_cb(tinyusb_msc_event_t *event)
{
    if (event->type == TINYUSB_MSC_EVENT_PREMOUNT_CHANGED) {
        ESP_LOGI(TAG, "Storage pre-mount for USB access");
        s_usb_msc.storage_busy = true;
        // Don't change status here - we already set CONNECTED when USB plugged in
    } else if (event->type == TINYUSB_MSC_EVENT_MOUNT_CHANGED) {
        if (event->mount_changed_data.is_mounted) {
            ESP_LOGI(TAG, "Storage mounted by application");
            s_usb_msc.storage_busy = false;
            // If USB is still connected, keep status as connected
            if (s_usb_msc.usb_connected) {
                update_status(USB_MSC_CONNECTED);
            } else {
                update_status(USB_MSC_DISCONNECTED);
            }
        } else {
            ESP_LOGI(TAG, "Storage unmounted for USB host access");
            if (s_usb_msc.usb_connected) {
                update_status(USB_MSC_MOUNTED);  // Storage available to host
            }
        }
    }
}

esp_err_t usb_msc_init(const usb_msc_config_t *config)
{
    if (s_usb_msc.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    if (!config) {
        ESP_LOGE(TAG, "Invalid config");
        return ESP_ERR_INVALID_ARG;
    }

    // Copy configuration
    s_usb_msc.config = *config;
    
    // Create mutex for storage access coordination
    s_usb_msc.storage_mutex = xSemaphoreCreateMutex();
    if (!s_usb_msc.storage_mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    if (config->enable_usb_msc) {
        // Check if SD card is already mounted by photo album
        if (!bsp_sdcard) {
            ESP_LOGW(TAG, "SD card not mounted yet, cannot initialize USB MSC");
            vSemaphoreDelete(s_usb_msc.storage_mutex);
            return ESP_ERR_NOT_FOUND;
        }

        ESP_LOGI(TAG, "Using existing SD card: %s, %d GB", 
                 bsp_sdcard->cid.name,
                 (int)(bsp_sdcard->csd.capacity / (1024 * 1024 * 1024 / bsp_sdcard->csd.sector_size)));

        // Initialize TinyUSB (check if already installed)
        const tinyusb_config_t tusb_cfg = {0};
        esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
        if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) {
            ESP_LOGE(TAG, "TinyUSB init failed: %s", esp_err_to_name(ret));
            vSemaphoreDelete(s_usb_msc.storage_mutex);
            return ret;
        } else if (ret == ESP_ERR_INVALID_STATE) {
            ESP_LOGI(TAG, "TinyUSB already installed");
        } else {
            ESP_LOGI(TAG, "TinyUSB installed successfully");
        }

        // Initialize MSC storage with existing SD card
        tinyusb_msc_sdmmc_config_t msc_config = {
            .card = bsp_sdcard,
            .callback_mount_changed = usb_mount_status_changed_cb,
            .callback_premount_changed = usb_mount_status_changed_cb,
            .mount_config = {
                .format_if_mount_failed = false,  // Don't format SD card automatically
                .max_files = 20,
                .allocation_unit_size = 16 * 1024
            }
        };

        ret = tinyusb_msc_storage_init_sdmmc(&msc_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "MSC storage init failed: %s", esp_err_to_name(ret));
            vSemaphoreDelete(s_usb_msc.storage_mutex);
            return ret;
        }

        // Create timer for periodic USB status checking
        const esp_timer_create_args_t timer_args = {
            .callback = &usb_status_check_timer_cb,
            .arg = NULL,
            .name = "usb_status_check"
        };
        ret = esp_timer_create(&timer_args, &s_usb_msc.status_check_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create status check timer: %s", esp_err_to_name(ret));
            vSemaphoreDelete(s_usb_msc.storage_mutex);
            tinyusb_msc_storage_deinit();
            return ret;
        }

        ESP_LOGI(TAG, "USB MSC initialized with existing SD card");
    }

    s_usb_msc.initialized = true;
    return ESP_OK;
}

esp_err_t usb_msc_start(void)
{
    if (!s_usb_msc.initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_usb_msc.config.enable_usb_msc) {
        ESP_LOGW(TAG, "USB MSC disabled");
        return ESP_OK;
    }

    // Unmount from application so it can be accessed via USB
    esp_err_t ret = tinyusb_msc_storage_unmount();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to unmount for USB access: %s", esp_err_to_name(ret));
        return ret;
    }

    // Start the USB status check timer (check every 500ms)
    if (s_usb_msc.status_check_timer) {
        ret = esp_timer_start_periodic(s_usb_msc.status_check_timer, 500000);  // 500ms
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start status check timer: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "USB status check timer started");
        }
    }

    ESP_LOGI(TAG, "USB MSC started - SD card available to host");
    return ESP_OK;
}

esp_err_t usb_msc_stop(void)
{
    if (!s_usb_msc.initialized) {
        return ESP_OK;
    }

    // Stop the status check timer
    if (s_usb_msc.status_check_timer) {
        esp_timer_stop(s_usb_msc.status_check_timer);
    }

    if (s_usb_msc.config.enable_usb_msc) {
        // Mount back to application
        esp_err_t ret = tinyusb_msc_storage_mount(s_usb_msc.config.mount_point);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to mount for application: %s", esp_err_to_name(ret));
        }
    }

    s_usb_msc.usb_connected = false;
    update_status(USB_MSC_DISCONNECTED);
    ESP_LOGI(TAG, "USB MSC stopped");
    return ESP_OK;
}

esp_err_t usb_msc_deinit(void)
{
    if (!s_usb_msc.initialized) {
        return ESP_OK;
    }

    usb_msc_stop();

    // Delete the timer
    if (s_usb_msc.status_check_timer) {
        esp_timer_delete(s_usb_msc.status_check_timer);
        s_usb_msc.status_check_timer = NULL;
    }

    if (s_usb_msc.config.enable_usb_msc) {
        tinyusb_msc_storage_deinit();
    }

    if (s_usb_msc.storage_mutex) {
        vSemaphoreDelete(s_usb_msc.storage_mutex);
        s_usb_msc.storage_mutex = NULL;
    }

    s_usb_msc.initialized = false;
    s_usb_msc.status_callback = NULL;
    
    ESP_LOGI(TAG, "USB MSC deinitialized");
    return ESP_OK;
}

usb_msc_status_t usb_msc_get_status(void)
{
    return s_usb_msc.status;
}

bool usb_msc_is_connected(void)
{
    return (s_usb_msc.status == USB_MSC_CONNECTED || 
            s_usb_msc.status == USB_MSC_MOUNTED);
}

bool usb_msc_is_mounted(void)
{
    return (s_usb_msc.status == USB_MSC_MOUNTED);
}

bool usb_msc_is_ejected(void)
{
    return (s_usb_msc.status == USB_MSC_EJECTED);
}

esp_err_t usb_msc_register_status_callback(usb_status_callback_t callback)
{
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_usb_msc.status_callback = callback;
    return ESP_OK;
}

esp_err_t usb_msc_unregister_status_callback(void)
{
    s_usb_msc.status_callback = NULL;
    return ESP_OK;
}

esp_err_t usb_msc_request_storage_access(void)
{
    if (!s_usb_msc.storage_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_usb_msc.storage_mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGW(TAG, "Storage access timeout");
        return ESP_ERR_TIMEOUT;
    }

    return ESP_OK;
}

void usb_msc_release_storage_access(void)
{
    if (s_usb_msc.storage_mutex) {
        xSemaphoreGive(s_usb_msc.storage_mutex);
    }
}

bool usb_msc_is_storage_busy(void)
{
    return s_usb_msc.storage_busy || tinyusb_msc_storage_in_use_by_usb_host();
} 