/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define WIFI_CONFIG_DIR_PATH  "/sdcard/wifi_config"
#define WIFI_CONFIG_FILE_PATH WIFI_CONFIG_DIR_PATH "/wifi_config.txt"

/**
 * @brief Load WiFi configuration from SD card
 * 
 * @param ssid Buffer to store SSID
 * @param ssid_len Size of SSID buffer
 * @param password Buffer to store password  
 * @param password_len Size of password buffer
 * @return esp_err_t ESP_OK if valid config loaded, ESP_ERR_INVALID_STATE if no valid SSID
 */
esp_err_t wifi_config_load(char *ssid, size_t ssid_len, char *password, size_t password_len);

#ifdef __cplusplus
}
#endif 