/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "app_sntp.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <time.h>
#include <sys/time.h>

static const char *TAG = "sntp";

/**
 * @brief SNTP time sync notification callback
 */
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "Time synchronized from SNTP server");
    settimeofday(tv, NULL);
}

/**
 * @brief Initialize SNTP service
 */
static void initialize_sntp(void)
{
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "ntp.aliyun.com");
    esp_sntp_setservername(1, "time.asia.apple.com");
    esp_sntp_setservername(2, "pool.ntp.org");
    esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
    esp_sntp_init();
}

void app_sntp_init(void)
{
    time_t now;
    struct tm timeinfo;
    
    ESP_LOGI(TAG, "Initializing time synchronization...");
    
    // Set timezone to China Standard Time
    setenv("TZ", "CST-8", 1);
    tzset();
    ESP_LOGI(TAG, "Timezone set to China Standard Time (UTC+8)");
    
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Check if time is already set (year > 2016)
    if (timeinfo.tm_year < (2016 - 1900)) {
        ESP_LOGI(TAG, "System time not set, starting SNTP synchronization...");
        ESP_LOGI(TAG, "SNTP servers: ntp.aliyun.com, time.asia.apple.com, pool.ntp.org");
        initialize_sntp();
        
        // Wait for time to be set
        int retry = 0;
        const int retry_count = 10;
        while (sntp_get_sync_status() == SNTP_SYNC_STATUS_RESET && ++retry < retry_count) {
            ESP_LOGI(TAG, "Waiting for time sync... (%d/%d)", retry, retry_count);
            vTaskDelay(2000 / portTICK_PERIOD_MS);
        }
        time(&now);
        
        if (sntp_get_sync_status() != SNTP_SYNC_STATUS_RESET) {
            ESP_LOGI(TAG, "Time synchronization successful!");
        } else {
            ESP_LOGW(TAG, "Time synchronization failed after %d attempts", retry_count);
        }
    } else {
        ESP_LOGI(TAG, "System time already set, updating via SNTP...");
        initialize_sntp();
    }
    
    // Log current time
    localtime_r(&now, &timeinfo);
    char strftime_buf[64];
    strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "Current system time: %s", strftime_buf);
    ESP_LOGI(TAG, "Time display will appear on screen");
    ESP_LOGI(TAG, "========================================");
} 