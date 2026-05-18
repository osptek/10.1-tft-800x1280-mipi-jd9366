/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_netif.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize WiFi in AP mode
 */
esp_err_t app_wifi_init(void);

/**
 * @brief Connect to WiFi network as STA
 * 
 * @param ssid WiFi SSID
 * @param password WiFi password
 * @return esp_err_t ESP_OK on success
 */
esp_err_t app_wifi_connect_sta(const char *ssid, const char *password);

/**
 * @brief Get AP IP information
 */
esp_err_t app_wifi_get_ip_info(esp_netif_ip_info_t *ip_info);

#ifdef __cplusplus
}
#endif 