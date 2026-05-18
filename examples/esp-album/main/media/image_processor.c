/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "image_processor.h"
#include "photo_album_constants.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_cache.h"
#include "esp_private/esp_cache_private.h"
#include "driver/ppa.h"
#include <string.h>
#include <math.h>
#include "esp_err.h"

static const char *TAG = "img_proc";

#define ALIGN_UP(num, align)    (((num) + ((align) - 1)) & ~((align) - 1))
#define PPA_SCALE_STEP          0.125f
#define PPA_MIN_SCALE           0.125f  
#define PPA_MAX_SCALE           16.0f
#define BYTES_PER_PIXEL_RGB565  2
#define PPA_MAX_PENDING_TRANSACTIONS 1

static ppa_client_handle_t s_ppa_client = NULL;
static size_t data_cache_line_size = 0;

// Adjust target scale to PPA supported values (0.125 multiples)
static float calculate_valid_ppa_scale(float target_scale)
{
    float valid_scale = roundf(target_scale / PPA_SCALE_STEP) * PPA_SCALE_STEP;
    
    if (valid_scale < PPA_MIN_SCALE) valid_scale = PPA_MIN_SCALE;
    if (valid_scale > PPA_MAX_SCALE) valid_scale = PPA_MAX_SCALE;
    
    return valid_scale;
}

// Calculate fit parameters preserving image orientation
static void calculate_fit_params_preserve_orientation(uint32_t src_width, uint32_t src_height,
                                                     uint32_t dst_width, uint32_t dst_height,
                                                     process_params_t *params)
{
    // Calculate ideal scale to fit screen
    float scale_x = (float)dst_width / src_width;
    float scale_y = (float)dst_height / src_height;
    float ideal_scale = (scale_x < scale_y) ? scale_x : scale_y;
    
    // Adjust to PPA supported scale
    float valid_scale = calculate_valid_ppa_scale(ideal_scale);
    
    // Calculate scaled dimensions
    uint32_t scaled_width = (uint32_t)(src_width * valid_scale);
    uint32_t scaled_height = (uint32_t)(src_height * valid_scale);
    
    // Initialize crop parameters
    uint32_t crop_width = src_width;
    uint32_t crop_height = src_height;
    uint32_t crop_offset_x = 0;
    uint32_t crop_offset_y = 0;
    uint32_t output_width = scaled_width;
    uint32_t output_height = scaled_height;
    
    // Adjust crop if scaled size exceeds screen
    if (scaled_width > dst_width) {
        crop_width = (uint32_t)(dst_width / valid_scale);
        crop_offset_x = (src_width - crop_width) / 2;
        output_width = dst_width;
    }
    
    if (scaled_height > dst_height) {
        crop_height = (uint32_t)(dst_height / valid_scale);
        crop_offset_y = (src_height - crop_height) / 2;
        output_height = dst_height;
    }
    
    // Set parameters
    params->target_width = output_width;
    params->target_height = output_height;
    params->operation = PROCESS_OP_SCALE_CROP;
    params->crop_width = crop_width;
    params->crop_height = crop_height;
    params->crop_offset_x = crop_offset_x;
    params->crop_offset_y = crop_offset_y;
    params->scale_x = valid_scale;
    params->scale_y = valid_scale;
}

esp_err_t image_processor_init(void)
{
    ESP_ERROR_CHECK(esp_cache_get_alignment(MALLOC_CAP_SPIRAM, &data_cache_line_size));

    ppa_client_config_t ppa_config = {
        .oper_type = PPA_OPERATION_SRM,
        .max_pending_trans_num = PPA_MAX_PENDING_TRANSACTIONS,
        .data_burst_length = PPA_DATA_BURST_LENGTH_128,
    };
    
    esp_err_t ret = ppa_register_client(&ppa_config, &s_ppa_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PPA client");
        return ret;
    }
    
    ESP_LOGI(TAG, "Image processor initialized");
    return ESP_OK;
}

esp_err_t image_processor_deinit(void)
{
    if (s_ppa_client) {
        esp_err_t ret = ppa_unregister_client(s_ppa_client);
        s_ppa_client = NULL;
        return ret;
    }
    return ESP_OK;
}

esp_err_t image_processor_calculate_params(uint32_t src_width, uint32_t src_height,
                                          uint32_t dst_width, uint32_t dst_height,
                                          scale_mode_t mode, process_params_t *params)
{
    if (!params) return ESP_ERR_INVALID_ARG;
    
    params->target_width = dst_width;
    params->target_height = dst_height;
    params->scale_mode = mode;
    
    // Initialize default values
    params->crop_width = src_width;
    params->crop_height = src_height;
    params->crop_offset_x = 0;
    params->crop_offset_y = 0;
    params->scale_x = 1.0f;
    params->scale_y = 1.0f;
    
    float scale_x = (float)dst_width / src_width;
    float scale_y = (float)dst_height / src_height;
    
    switch (mode) {
        case SCALE_MODE_FIT:
            calculate_fit_params_preserve_orientation(src_width, src_height, dst_width, dst_height, params);
            break;
            
        case SCALE_MODE_FILL:
            {
                float fill_scale = (scale_x > scale_y) ? scale_x : scale_y;
                fill_scale = calculate_valid_ppa_scale(fill_scale);
                
                uint32_t scaled_width = (uint32_t)(src_width * fill_scale);
                uint32_t scaled_height = (uint32_t)(src_height * fill_scale);
                
                if (scaled_width > dst_width) {
                    params->crop_width = (uint32_t)(dst_width / fill_scale);
                    params->crop_offset_x = (src_width - params->crop_width) / 2;
                } else {
                    params->crop_width = src_width;
                    params->crop_offset_x = 0;
                }
                
                if (scaled_height > dst_height) {
                    params->crop_height = (uint32_t)(dst_height / fill_scale);
                    params->crop_offset_y = (src_height - params->crop_height) / 2;
                } else {
                    params->crop_height = src_height;
                    params->crop_offset_y = 0;
                }
                
                params->scale_x = fill_scale;
                params->scale_y = fill_scale;
                params->operation = PROCESS_OP_SCALE_CROP;
            }
            break;
            
        case SCALE_MODE_CENTER:
            if (src_width <= dst_width && src_height <= dst_height) {
                params->target_width = src_width;
                params->target_height = src_height;
                params->operation = PROCESS_OP_SCALE;
                params->scale_x = 1.0f;
                params->scale_y = 1.0f;
            } else {
                float center_scale = (scale_x < scale_y) ? scale_x : scale_y;
                center_scale = calculate_valid_ppa_scale(center_scale);
                
                params->target_width = (uint32_t)(src_width * center_scale);
                params->target_height = (uint32_t)(src_height * center_scale);
                params->operation = PROCESS_OP_SCALE;
                params->scale_x = center_scale;
                params->scale_y = center_scale;
            }
            break;
            
        case SCALE_MODE_CROP_ONLY:
            params->target_width = dst_width;
            params->target_height = dst_height;
            params->crop_width = dst_width;
            params->crop_height = dst_height;
            params->crop_offset_x = (src_width > dst_width) ? (src_width - dst_width) / 2 : 0;
            params->crop_offset_y = (src_height > dst_height) ? (src_height - dst_height) / 2 : 0;
            params->operation = PROCESS_OP_CROP;
            break;
    }
    
    return ESP_OK;
}

esp_err_t image_processor_process(const decoded_image_t *input, 
                                 decoded_image_t *output,
                                 const process_params_t *params)
{
    if (!input || !output || !params || !input->rgb_data || !input->is_valid) {
        return ESP_ERR_INVALID_ARG;
    }
    
    ESP_LOGI(TAG, "Processing: %dx%d -> %dx%d", 
             input->width, input->height, params->target_width, params->target_height);
    
    // If no processing needed, directly reference input data
    if (params->operation == PROCESS_OP_SCALE && 
        params->target_width == input->width && 
        params->target_height == input->height) {
        
        output->width = input->width;
        output->height = input->height;
        output->data_size = input->data_size;
        output->rgb_data = input->rgb_data;  // Direct reference, no copy
        output->is_valid = true;
        output->owns_data = false;  // We don't own this data
        
        return ESP_OK;
    }
    
    // Process through PPA
    uint16_t *input_rgb565 = (uint16_t*)input->rgb_data;
    size_t input_data_size = input->data_size;
    
    // Calculate correct JPEG row stride using 16-byte alignment
    uint32_t expected_size = input->width * input->height * 2;
    uint32_t jpeg_aligned_width = ((input->width + 15) & ~15);
    
    // Check input buffer alignment for PPA
    uint16_t *aligned_input_rgb565 = input_rgb565;
    bool need_free_input = false;
    
    if ((uintptr_t)input_rgb565 % data_cache_line_size != 0) {
        size_t input_buffer_size = ALIGN_UP(input_data_size, data_cache_line_size);
        aligned_input_rgb565 = heap_caps_aligned_alloc(data_cache_line_size, input_buffer_size, 
                                                      MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!aligned_input_rgb565) {
            aligned_input_rgb565 = heap_caps_aligned_alloc(data_cache_line_size, input_buffer_size, 
                                                          MALLOC_CAP_SPIRAM);
        }
        
        if (!aligned_input_rgb565) {
            ESP_LOGE(TAG, "Failed to allocate aligned input buffer: %zu bytes", input_buffer_size);
            return ESP_ERR_NO_MEM;
        }
        
        memcpy(aligned_input_rgb565, input_rgb565, input_data_size);
        need_free_input = true;
    }
    
    // Allocate output buffer
    uint32_t output_pixels = params->target_width * params->target_height;
    size_t buffer_size = ALIGN_UP(output_pixels * sizeof(uint16_t), data_cache_line_size);
    
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    
    if (buffer_size > free_internal && buffer_size > free_spiram) {
        ESP_LOGE(TAG, "Insufficient memory: need %d bytes", buffer_size);
        if (need_free_input) {
            free(aligned_input_rgb565);
        }
        return ESP_ERR_NO_MEM;
    }
    
    uint16_t *output_rgb565 = heap_caps_aligned_alloc(data_cache_line_size, buffer_size, 
                                                     MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!output_rgb565) {
        output_rgb565 = heap_caps_aligned_alloc(data_cache_line_size, buffer_size, MALLOC_CAP_SPIRAM);
    }
    
    if (!output_rgb565) {
        ESP_LOGE(TAG, "Failed to allocate output buffer");
        if (need_free_input) {
            free(aligned_input_rgb565);
        }
        return ESP_ERR_NO_MEM;
    }
    
    // Configure PPA operation parameters
    float scale_x = (float)params->target_width / input->width;
    float scale_y = (float)params->target_height / input->height;
    
    uint32_t block_w = input->width;
    uint32_t block_h = input->height;
    uint32_t block_offset_x = 0;
    uint32_t block_offset_y = 0;
    
    // Handle different processing operations
    if (params->operation == PROCESS_OP_CROP) {
        scale_x = 1.0f;
        scale_y = 1.0f;
        
        if (input->width > params->target_width) {
            block_w = params->target_width;
            block_offset_x = (input->width - params->target_width) / 2;
        }
        
        if (input->height > params->target_height) {
            block_h = params->target_height;
            block_offset_y = (input->height - params->target_height) / 2;
        }
    } else if (params->operation == PROCESS_OP_SCALE_CROP) {
        if (params->scale_mode == SCALE_MODE_FIT) {
            scale_x = params->scale_x;
            scale_y = params->scale_y;
            block_w = params->crop_width;
            block_h = params->crop_height;
            block_offset_x = params->crop_offset_x;
            block_offset_y = params->crop_offset_y;
        } else {
            // SCALE_FILL logic
            if (scale_x > scale_y) {
                uint32_t calculated_block_w = (uint32_t)(params->target_width / scale_y);
                if (calculated_block_w <= input->width) {
                    block_w = calculated_block_w;
                    block_offset_x = (input->width - block_w) / 2;
                }
                scale_x = scale_y;
            } else {
                uint32_t calculated_block_h = (uint32_t)(params->target_height / scale_x);
                if (calculated_block_h <= input->height) {
                    block_h = calculated_block_h;
                    block_offset_y = (input->height - block_h) / 2;
                }
                scale_y = scale_x;
            }
        }
    }
    
    // Ensure valid PPA scale values
    scale_x = calculate_valid_ppa_scale(scale_x);
    scale_y = calculate_valid_ppa_scale(scale_y);
    
    // Validate block bounds
    if (block_offset_x + block_w > input->width || block_offset_y + block_h > input->height) {
        ESP_LOGE(TAG, "Block bounds exceed input dimensions");
        free(output_rgb565);
        if (need_free_input) {
            free(aligned_input_rgb565);
        }
        return ESP_ERR_INVALID_ARG;
    }
    
    // Calculate PPA input configuration - handle JPEG padding correctly
    uint32_t ppa_pic_w = input->width;
    if (input_data_size > expected_size) {
        ppa_pic_w = jpeg_aligned_width;  // Use correct 16-byte aligned stride
    }
    
    ppa_srm_oper_config_t srm_config = {
        .in = {
            .buffer = aligned_input_rgb565,
            .pic_w = ppa_pic_w,
            .pic_h = input->height,
            .block_w = block_w,
            .block_h = block_h,
            .block_offset_x = block_offset_x,
            .block_offset_y = block_offset_y,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .out = {
            .buffer = output_rgb565,
            .buffer_size = buffer_size,
            .pic_w = params->target_width,
            .pic_h = params->target_height,
            .block_offset_x = 0,
            .block_offset_y = 0,
            .srm_cm = PPA_SRM_COLOR_MODE_RGB565,
        },
        .rotation_angle = PPA_SRM_ROTATION_ANGLE_0,
        .scale_x = scale_x,
        .scale_y = scale_y,
        .mirror_x = false,
        .mirror_y = false,
        .rgb_swap = false,
        .byte_swap = false,
        .alpha_update_mode = PPA_ALPHA_NO_CHANGE,
        .mode = PPA_TRANS_MODE_BLOCKING,
    };
    
    // Ensure cache sync for input data
    esp_cache_msync(aligned_input_rgb565, input_data_size, 
                   ESP_CACHE_MSYNC_FLAG_DIR_C2M | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    
    // Execute PPA operation
    esp_err_t ret = ppa_do_scale_rotate_mirror(s_ppa_client, &srm_config);
    
    if (need_free_input) {
        free(aligned_input_rgb565);
    }
    
    if (ret != ESP_OK) {
        free(output_rgb565);
        ESP_LOGE(TAG, "PPA operation failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Set output data
    output->width = params->target_width;
    output->height = params->target_height;
    output->data_size = output_pixels * BYTES_PER_PIXEL_RGB565;
    output->rgb_data = (uint8_t*)output_rgb565;
    output->is_valid = true;
    output->owns_data = true;
    
    ESP_LOGI(TAG, "Processed successfully: %dx%d -> %dx%d", 
             input->width, input->height, output->width, output->height);
    
    return ESP_OK;
} 