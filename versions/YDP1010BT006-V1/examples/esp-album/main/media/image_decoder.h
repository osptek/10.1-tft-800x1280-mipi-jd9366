/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "photo_album.h"
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Decoder configuration
typedef struct {
    uint32_t max_width;
    uint32_t max_height;
    bool use_psram;
} decoder_config_t;

// Image decoder functions
esp_err_t image_decoder_init(const decoder_config_t *config);
esp_err_t image_decoder_deinit(void);
esp_err_t image_decoder_decode(const uint8_t *data, size_t data_size, 
                              image_format_t format, decoded_image_t *output);
esp_err_t image_decoder_get_info(const uint8_t *data, size_t data_size,
                                image_format_t format, uint32_t *width, uint32_t *height);
void image_decoder_free_image(decoded_image_t *image);

#ifdef __cplusplus
}
#endif 