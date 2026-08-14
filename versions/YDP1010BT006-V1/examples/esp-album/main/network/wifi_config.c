/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "wifi_config.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>

static const char *TAG = "wifi_config";

// Default template content for wifi_config.txt
static const char *DEFAULT_CONFIG = 
    "WiFi Configuration\n"
    "Please edit this file with notepad and restart device\n"
    "\n"
    "WiFi Name (SSID): \n"
    "\n"
    "WiFi Password: \n"
    "\n"
    "Example:\n"
    "WiFi Name (SSID): MyHomeWiFi\n"
    "WiFi Password: MyPassword123\n";

/**
 * @brief Create default config file if not exists
 */
static esp_err_t create_default_config(void)
{
    struct stat st = {0};

    /* Ensure directory exists */
    if (stat(WIFI_CONFIG_DIR_PATH, &st) == -1) {
        if (mkdir(WIFI_CONFIG_DIR_PATH, 0755) != 0) {
            ESP_LOGE(TAG, "Failed to create dir %s", WIFI_CONFIG_DIR_PATH);
            return ESP_FAIL;
        }
    }

    // Create default config file if not exists
    if (stat(WIFI_CONFIG_FILE_PATH, &st) == -1) {
        FILE *f = fopen(WIFI_CONFIG_FILE_PATH, "w");
        if (!f) {
            ESP_LOGE(TAG, "Failed to create default config file");
            return ESP_FAIL;
        }
        
        fwrite(DEFAULT_CONFIG, 1, strlen(DEFAULT_CONFIG), f);
        fclose(f);
        ESP_LOGI(TAG, "Created default config file: %s", WIFI_CONFIG_FILE_PATH);
        ESP_LOGI(TAG, "Edit with notepad: add WiFi name after 'WiFi Name (SSID):', password after 'WiFi Password:'");
        ESP_LOGI(TAG, "Example: WiFi Name (SSID): MyWiFi");
    }
    
    return ESP_OK;
}

/**
 * @brief Trim whitespace and newlines from string
 */
static void trim_string(char *str)
{
    if (!str) return;
    
    // Remove leading whitespace
    char *start = str;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') {
        start++;
    }
    
    // Move string to beginning
    if (start != str) {
        memmove(str, start, strlen(start) + 1);
    }
    
    // Remove trailing whitespace
    int len = strlen(str);
    while (len > 0 && (str[len-1] == ' ' || str[len-1] == '\t' || 
                       str[len-1] == '\r' || str[len-1] == '\n')) {
        str[--len] = '\0';
    }
}

/**
 * @brief Extract value from a line that may contain "label:value" format
 * If line contains ':', take everything after ':' and trim it
 * Otherwise, take the whole line and trim it
 */
static void extract_value_from_line(char *line, char *output, size_t output_len)
{
    if (!line || !output) return;
    
    output[0] = '\0';
    
    // Look for ':' separator
    char *colon_pos = strchr(line, ':');
    if (colon_pos) {
        // Extract everything after ':'
        strncpy(output, colon_pos + 1, output_len - 1);
    } else {
        // No ':', take the whole line
        strncpy(output, line, output_len - 1);
    }
    
    output[output_len - 1] = '\0';
    trim_string(output);
}

esp_err_t wifi_config_load(char *ssid, size_t ssid_len, char *password, size_t password_len)
{
    if (!ssid || !password) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ssid[0] = '\0';
    password[0] = '\0';
    
    ESP_LOGI(TAG, "Checking WiFi config file: %s", WIFI_CONFIG_FILE_PATH);
    
    // Create default config if not exists
    esp_err_t ret = create_default_config();
    if (ret != ESP_OK) {
        return ret;
    }
    
    // Read config file
    FILE *f = fopen(WIFI_CONFIG_FILE_PATH, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open config file: %s", WIFI_CONFIG_FILE_PATH);
        return ESP_FAIL;
    }
    
    ESP_LOGI(TAG, "Reading WiFi configuration...");
    
    char line[128];
    int line_num = 0;
    char temp_value[128];
    
    while (fgets(line, sizeof(line), f)) {
        line_num++;
        
        // Line 4: WiFi SSID (may contain "WiFi Name (SSID): actual_ssid")
        if (line_num == 4) {
            extract_value_from_line(line, temp_value, sizeof(temp_value));
            if (strlen(temp_value) > 0) {
                strncpy(ssid, temp_value, ssid_len - 1);
                ssid[ssid_len - 1] = '\0';
            }
            ESP_LOGI(TAG, "Found SSID on line 4: %s", ssid[0] ? ssid : "(empty)");
        }
        // Line 6: WiFi Password (may contain "WiFi Password: actual_password")
        else if (line_num == 6) {
            extract_value_from_line(line, temp_value, sizeof(temp_value));
            if (strlen(temp_value) > 0) {
                strncpy(password, temp_value, password_len - 1);
                password[password_len - 1] = '\0';
            }
            ESP_LOGI(TAG, "Found password on line 6: %s", 
                     password[0] ? "***configured***" : "(empty)");
        }
    }
    
    fclose(f);
    
    if (ssid[0] == '\0') {
        ESP_LOGW(TAG, "No WiFi name found on line 4 of config file");
        ESP_LOGW(TAG, "Please edit %s with notepad:", WIFI_CONFIG_FILE_PATH);
        ESP_LOGW(TAG, "  Line 4: Enter your WiFi name after 'WiFi Name (SSID):'");
        ESP_LOGW(TAG, "  Line 6: Enter your WiFi password after 'WiFi Password:'");
        ESP_LOGW(TAG, "  Example:");
        ESP_LOGW(TAG, "    WiFi Name (SSID): MyWiFiNetwork");
        ESP_LOGW(TAG, "    WiFi Password: MyPassword123");
        return ESP_ERR_INVALID_STATE;
    }
    
    ESP_LOGI(TAG, "WiFi configuration loaded successfully");
    return ESP_OK;
} 