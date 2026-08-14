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

// Processing operation type
typedef enum {
    PROCESS_OP_SCALE,
    PROCESS_OP_CROP,
    PROCESS_OP_SCALE_CROP
} process_op_t;

// Processing parameters
typedef struct {
    uint32_t target_width;
    uint32_t target_height;
    scale_mode_t scale_mode;
    process_op_t operation;
    
    // PPA crop and scale parameters
    uint32_t crop_width;
    uint32_t crop_height;
    uint32_t crop_offset_x;
    uint32_t crop_offset_y;
    float scale_x;
    float scale_y;
} process_params_t;

// Image processor functions
esp_err_t image_processor_init(void);
esp_err_t image_processor_deinit(void);
esp_err_t image_processor_process(const decoded_image_t *input, 
                                 decoded_image_t *output,
                                 const process_params_t *params);
esp_err_t image_processor_calculate_params(uint32_t src_width, uint32_t src_height,
                                          uint32_t dst_width, uint32_t dst_height,
                                          scale_mode_t mode, process_params_t *params);

#ifdef __cplusplus
}
#endif 