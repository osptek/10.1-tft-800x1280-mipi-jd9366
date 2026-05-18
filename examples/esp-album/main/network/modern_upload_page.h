/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Embedded modern upload HTML page
 */
extern const uint8_t modern_upload_html_start[] asm("_binary_modern_upload_html_start");
extern const uint8_t modern_upload_html_end[] asm("_binary_modern_upload_html_end");

/**
 * @brief Embedded modern upload CSS file
 */
extern const uint8_t modern_upload_css_start[] asm("_binary_modern_upload_css_start");
extern const uint8_t modern_upload_css_end[] asm("_binary_modern_upload_css_end");

/**
 * @brief Embedded modern upload JavaScript file
 */
extern const uint8_t modern_upload_js_start[] asm("_binary_modern_upload_js_start");
extern const uint8_t modern_upload_js_end[] asm("_binary_modern_upload_js_end");

/**
 * @brief Get the size of the embedded modern upload HTML page
 * @return Size in bytes
 */
size_t get_modern_upload_html_size(void);

/**
 * @brief Get the size of the embedded modern upload CSS file
 * @return Size in bytes
 */
size_t get_modern_upload_css_size(void);

/**
 * @brief Get the size of the embedded modern upload JavaScript file
 * @return Size in bytes
 */
size_t get_modern_upload_js_size(void);

#ifdef __cplusplus
}
#endif
