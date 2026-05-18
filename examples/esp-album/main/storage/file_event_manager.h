/* SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief File event types for photo album management
 */
typedef enum {
    FILE_EVENT_ADD,     // New file added
    FILE_EVENT_DEL,     // File deleted
    FILE_EVENT_REFRESH  // Full album refresh
} file_event_type_t;

/**
 * @brief File event message structure
 */
typedef struct {
    file_event_type_t type;
    char path[256];
} file_event_msg_t;

/**
 * @brief Initialize file event manager
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t file_event_manager_init(void);

/**
 * @brief HTTP upload/delete completion callback
 * Sends message to file worker queue to avoid concurrent file system access
 * 
 * @param filepath Path of uploaded file, or NULL for delete operations
 */
void on_file_uploaded(const char *filepath);

#ifdef __cplusplus
}
#endif 