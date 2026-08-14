/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "network_manager.h"
#include "app_wifi.h"
#include "app_http_server.h"
#include "photo_album.h"
#include "wifi_config.h"
#include "app_sntp.h"
#include "ui_manager.h"
#include "esp_log.h"
#include "esp_event.h"
#include "esp_wifi.h"
#include <string.h>

static const char *TAG = "network_mgr";

// HTTP file uploaded/delete callback
static void http_file_uploaded_cb(const char *filepath)
{
    if (filepath) {
        ESP_LOGI(TAG, "File uploaded via HTTP: %s", filepath);
        
        // Check if wifi config file was uploaded/modified
        if (strstr(filepath, "wifi_config.txt")) {
            ESP_LOGI(TAG, "WiFi config updated - restart device to apply changes");
        }
    } else {
        ESP_LOGI(TAG, "File deleted via HTTP");
    }
    // Refresh photo album asynchronously by posting event to UI task
    photo_album_refresh();
}

// WiFi STA event handler for SNTP initialization
static void wifi_sta_event_handler(void *arg, esp_event_base_t event_base,
                                   int32_t event_id, void *event_data)
{
    if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "Internet connection established!");
        ESP_LOGI(TAG, "Device IP: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Starting time synchronization...");
        ESP_LOGI(TAG, "========================================");
        
        app_sntp_init();
        
        // Show time display on UI after successful connection and SNTP init
        esp_err_t ret = ui_manager_show_time();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to show time display: %s", esp_err_to_name(ret));
        } else {
            ESP_LOGI(TAG, "Time display enabled on screen");
        }
    }
}

// Static flag to prevent multiple initializations
static bool s_network_manager_initialized = false;

esp_err_t network_manager_init(void)
{
    if (s_network_manager_initialized) {
        ESP_LOGW(TAG, "Network manager already initialized");
        return ESP_OK;
    }

    esp_err_t ret;

    // Initialize WiFi (AP + STA support)
    ESP_LOGI(TAG, "Initializing WiFi...");
    ret = app_wifi_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "WiFi init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "WiFi initialized successfully");

    // Load WiFi configuration and try STA connection
    char ssid[33] = {0};
    char password[65] = {0};
    
    ESP_LOGI(TAG, "Loading WiFi configuration from SD card...");
    ret = wifi_config_load(ssid, sizeof(ssid), password, sizeof(password));
    
    if (ret == ESP_OK && ssid[0] != '\0') {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "WiFi configuration found!");
        ESP_LOGI(TAG, "SSID: %s", ssid);
        ESP_LOGI(TAG, "Password: %s", password[0] ? "***configured***" : "***empty***");
        ESP_LOGI(TAG, "Attempting to connect...");
        ESP_LOGI(TAG, "========================================");
        
        // Register STA event handler for SNTP
        ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, 
                                                   &wifi_sta_event_handler, NULL));
        
        ret = app_wifi_connect_sta(ssid, password);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "STA connection failed: %s", esp_err_to_name(ret));
            ESP_LOGW(TAG, "Check WiFi credentials in /sdcard/wifi_config/wifi_config.txt");
        }
    } else {
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "No WiFi configuration found");
        ESP_LOGI(TAG, "Device running in AP-only mode");
        ESP_LOGI(TAG, "Edit /sdcard/wifi_config/wifi_config.txt to connect to WiFi");
        ESP_LOGI(TAG, "========================================");
    }

    // Start HTTP file server
    ESP_LOGI(TAG, "Starting HTTP file server...");
    ret = start_file_server("/sdcard/photos", http_file_uploaded_cb);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "HTTP server start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "HTTP server started successfully");

    // Display connection information
    esp_netif_ip_info_t ip_info;
    if (app_wifi_get_ip_info(&ip_info) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi AP IP: " IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Upload URL: http://" IPSTR, IP2STR(&ip_info.ip));
        ESP_LOGI(TAG, "Modern UI: http://" IPSTR "/modern_upload.html", IP2STR(&ip_info.ip));
    }

    s_network_manager_initialized = true;
    return ESP_OK;
}
