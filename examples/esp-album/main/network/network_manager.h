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
 * @brief Initialize and start HTTP file server for photo album uploads
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t network_manager_init(void);

#ifdef __cplusplus
}
#endif 