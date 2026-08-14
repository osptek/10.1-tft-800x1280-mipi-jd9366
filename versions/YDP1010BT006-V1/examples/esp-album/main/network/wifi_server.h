/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// WiFi server configuration
typedef struct {
    bool enable_wifi;           // Enable WiFi functionality
    char ap_ssid[32];          // WiFi AP SSID
    char ap_password[64];      // WiFi AP password
    char server_ip[16];        // Server IP address
    uint16_t server_port;      // Server port
    uint8_t max_clients;       // Maximum connected clients
} wifi_server_config_t;

// WiFi server status
typedef enum {
    WIFI_SERVER_STOPPED,
    WIFI_SERVER_STARTING,
    WIFI_SERVER_RUNNING,
    WIFI_SERVER_ERROR
} wifi_server_status_t;

// WiFi server functions
esp_err_t wifi_server_init(const wifi_server_config_t *config);
esp_err_t wifi_server_start(const char *base_path);
esp_err_t wifi_server_stop(void);
esp_err_t wifi_server_deinit(void);

// Status functions
wifi_server_status_t wifi_server_get_status(void);
bool wifi_server_is_running(void);
uint8_t wifi_server_get_client_count(void);
const char* wifi_server_get_ip_address(void);

#ifdef __cplusplus
}
#endif 