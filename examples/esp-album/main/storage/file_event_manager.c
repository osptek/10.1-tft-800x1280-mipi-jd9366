/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "file_event_manager.h"
#include "photo_album.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "file_event_mgr";
static QueueHandle_t file_event_queue = NULL;

/**
 * @brief File worker task - serializes all file system operations
 * This prevents concurrent access to file_manager/Data_Cache which could cause crashes
 */
static void file_worker_task(void *arg)
{
    file_event_msg_t msg;
    
    ESP_LOGI(TAG, "File worker task started");
    
    while (true) {
        if (xQueueReceive(file_event_queue, &msg, portMAX_DELAY) == pdTRUE) {
            switch (msg.type) {
                case FILE_EVENT_ADD:
                    ESP_LOGI(TAG, "Processing file add: %s", msg.path);
                    photo_album_refresh();
                    ESP_LOGI(TAG, "Photo album refreshed after file add");
                    break;
                    
                case FILE_EVENT_DEL:
                    ESP_LOGI(TAG, "Processing file delete: %s", msg.path);
                    photo_album_refresh();
                    ESP_LOGI(TAG, "Photo album refreshed after file delete");
                    break;
                    
                case FILE_EVENT_REFRESH:
                    ESP_LOGI(TAG, "Processing album refresh");
                    photo_album_refresh();
                    ESP_LOGI(TAG, "Photo album refreshed");
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown file event type: %d", msg.type);
                    break;
            }
        }
    }
}

esp_err_t file_event_manager_init(void)
{
    // Create file event message queue
    file_event_queue = xQueueCreate(8, sizeof(file_event_msg_t));
    if (file_event_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create file event queue");
        return ESP_ERR_NO_MEM;
    }

    // Create file worker task (pinned to Core 1 to avoid UI conflicts)
    BaseType_t task_ret = xTaskCreatePinnedToCore(
        file_worker_task,
        "file_worker",
        8192,  // 8KB stack
        NULL,
        5,     // Medium priority
        NULL,
        1      // Core 1
    );
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create file worker task");
        vQueueDelete(file_event_queue);
        file_event_queue = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "File event manager initialized");
    return ESP_OK;
}

void on_file_uploaded(const char *filepath)
{
    if (file_event_queue == NULL) {
        ESP_LOGW(TAG, "File event queue not initialized");
        return;
    }
    
    file_event_msg_t msg;
    
    if (filepath) {
        ESP_LOGI(TAG, "New file uploaded: %s", filepath);
        msg.type = FILE_EVENT_ADD;
        strlcpy(msg.path, filepath, sizeof(msg.path));
    } else {
        ESP_LOGI(TAG, "File deleted, refreshing album");
        msg.type = FILE_EVENT_DEL;
        msg.path[0] = '\0'; // Empty path indicates delete operation
    }
    
    // Send to file worker queue without blocking HTTP thread
    if (xQueueSend(file_event_queue, &msg, 0) != pdTRUE) {
        ESP_LOGW(TAG, "File event queue full, skipping refresh");
    }
} 