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
 * @brief Callback function for when file upload is completed
 *
 * @param filepath Full path of the uploaded file
 */
typedef void (*upload_complete_callback_t)(const char *filepath);

/**
 * @brief Start the HTTP file server
 *
 * @param base_path Base path for file storage
 * @param callback Callback function to call when upload completes (can be NULL)
 * @return esp_err_t ESP_OK on success
 */
esp_err_t start_file_server(const char *base_path, upload_complete_callback_t callback);

/**
 * @brief Stop the file server and free allocated resources
 *
 * @return
 *     - ESP_OK: Server stopped successfully.
 *     - ESP_ERR_INVALID_STATE: Server is not running.
 */
esp_err_t stop_file_server(void);

#ifdef __cplusplus
}
#endif 