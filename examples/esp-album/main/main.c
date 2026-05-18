/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "photo_album.h"
#include "usb_manager.h"
#include "video_player.h"
#include "file_manager.h"
#include "ui_manager.h"
#include "network_manager.h"
#include "ksdiy_lvgl_port.h"
#include "lvgl.h"
#include "lv_demos.h"
static const char *TAG = "main";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting digital photo album with HTTP upload");
    // ksdiy_lvgl_port_init();
    // Initialize display
    bsp_display_cfg_t cfg = {
        .lvgl_port_cfg = ESP_LVGL_PORT_INIT_CONFIG(),
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        }
    };
    bsp_display_start_with_config(&cfg); 
    bsp_display_backlight_on();
    ESP_LOGI(TAG, "Display initialized");
    // ksdiy_lvgl_lock(1000);
    // lv_demo_music();
    // ksdiy_lvgl_unlock();
    // Initialize photo album (mounts SD card)
    esp_err_t ret = photo_album_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize photo album: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize network manager for HTTP uploads
    ret = network_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Network manager initialization failed, continuing without HTTP access");
    }

    // Initialize USB manager
    ret = usb_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "USB manager init failed, continuing without USB: %s", esp_err_to_name(ret));
    }

    // Start photo album
    ret = photo_album_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start photo album: %s", esp_err_to_name(ret));
        if (ret == ESP_ERR_NOT_FOUND) {
            ESP_LOGW(TAG, "No images found. Please put images in SD card /photos directory or upload via HTTP");
        }
        return;
    }

    ESP_LOGI(TAG, "Digital photo album started with %d images",
             photo_album_get_total_count());

    ESP_LOGI(TAG, "System ready!");
    ESP_LOGI(TAG, "- Upload files at: http://192.168.4.1");
}
