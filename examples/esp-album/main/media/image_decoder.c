/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "image_decoder.h"
#include "app_stream_adapter.h"
#include "photo_album_constants.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_memory_utils.h"
#include "driver/jpeg_decode.h"
#include "png.h"
#include <string.h>
#include <stdlib.h>
#include "esp_cache.h"

static const char *TAG = "img_dec";
static jpeg_decoder_handle_t s_jpeg_decoder = NULL;
static decoder_config_t s_config;

// Cache sync helper function
static esp_err_t safe_cache_sync(void *addr, size_t size, int flags)
{
    if (!addr || size == 0) {
        return ESP_OK;
    }
    
    // Check if address is in cacheable memory region
    if (!esp_ptr_in_dram(addr) && !esp_ptr_external_ram(addr)) {
        return ESP_OK; // Skip cache sync for non-cacheable memory
    }
    
    // Handle alignment requirements based on direction
    if (flags & ESP_CACHE_MSYNC_FLAG_DIR_C2M) {
        // C2M direction supports UNALIGNED flag
        return esp_cache_msync(addr, size, flags | ESP_CACHE_MSYNC_FLAG_UNALIGNED);
    } else {
        // M2C direction doesn't support UNALIGNED flag - check alignment
        size_t cache_line_size = 128; // Typical cache line size for ESP32-P4
        bool aligned_addr = ((uintptr_t)addr % cache_line_size) == 0;
        bool aligned_size = (size % cache_line_size) == 0;
        
        if (aligned_addr && aligned_size) {
            // Properly aligned, can do sync
            return esp_cache_msync(addr, size, flags);
        } else {
            // Not aligned, skip M2C sync (hardware will handle coherency)
            return ESP_OK;
        }
    }
}

// PNG read callback
static void png_read_callback(png_structp png_ptr, png_bytep data, png_size_t length)
{
    uint8_t **png_data_ptr = (uint8_t **)png_get_io_ptr(png_ptr);
    memcpy(data, *png_data_ptr, length);
    *png_data_ptr += length;
}

// Convert RGB888 to RGB565
static void convert_rgb888_to_rgb565(const uint8_t *rgb888, uint16_t *rgb565, uint32_t pixel_count)
{
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint8_t r = rgb888[i * BYTES_PER_PIXEL_RGB888 + 0] >> RGB888_TO_RGB565_R_SHIFT;
        uint8_t g = rgb888[i * BYTES_PER_PIXEL_RGB888 + 1] >> RGB888_TO_RGB565_G_SHIFT;
        uint8_t b = rgb888[i * BYTES_PER_PIXEL_RGB888 + 2] >> RGB888_TO_RGB565_B_SHIFT;
        rgb565[i] = (r << RGB565_R_SHIFT) | (g << RGB565_G_SHIFT) | b;
    }
}

static esp_err_t decode_jpeg_image(const uint8_t *data, size_t data_size, decoded_image_t *output)
{
    if (!s_jpeg_decoder) {
        ESP_LOGE(TAG, "JPEG decoder not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    // Early validation: check data size reasonableness
    if (data_size > 10 * 1024 * 1024) {  // >10MB is suspicious
        ESP_LOGE(TAG, "JPEG data too large: %zu bytes, likely corrupted", data_size);
        return ESP_ERR_INVALID_ARG;
    }
    
    if (data_size < 100) {  // Too small to be valid JPEG
        ESP_LOGE(TAG, "JPEG data too small: %zu bytes", data_size);
        return ESP_ERR_INVALID_ARG;
    }
    
    // Get JPEG header info first
    jpeg_decode_picture_info_t header_info;
    esp_err_t ret = jpeg_decoder_get_info(data, data_size, &header_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get JPEG info: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // CRITICAL: ESP32-P4 JPEG decoder requires dimensions divisible by 8
    if ((header_info.width % JPEG_ALIGNMENT) != 0 || (header_info.height % JPEG_ALIGNMENT) != 0) {
        ESP_LOGE(TAG, "JPEG dimensions not supported: %dx%d (must be divisible by %d)", 
                 header_info.width, header_info.height, JPEG_ALIGNMENT);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Check total pixel count and memory requirements instead of individual width/height
    uint64_t total_pixels = (uint64_t)header_info.width * header_info.height;
    uint64_t max_pixels = (uint64_t)MAX_DECODE_WIDTH * MAX_DECODE_HEIGHT;  // 1920x1080 = ~2M pixels
    
    if (total_pixels > max_pixels) {
        ESP_LOGE(TAG, "JPEG resolution too large: %dx%d (%llu pixels, max: %llu pixels)", 
                 header_info.width, header_info.height, total_pixels, max_pixels);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Additional check: ensure no single dimension is extremely large
    if (header_info.width > 2560 || header_info.height > 2560) {
        ESP_LOGE(TAG, "JPEG dimension too large: %dx%d (single dimension max: 2560)", 
                 header_info.width, header_info.height);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    // Check minimum supported resolution
    if (header_info.width < JPEG_ALIGNMENT || header_info.height < JPEG_ALIGNMENT) {
        ESP_LOGE(TAG, "JPEG resolution too small: %dx%d (min: %dx%d)", 
                 header_info.width, header_info.height, JPEG_ALIGNMENT, JPEG_ALIGNMENT);
        return ESP_ERR_NOT_SUPPORTED;
    }
    
    output->width = header_info.width;
    output->height = header_info.height;
    
    // Calculate buffer size with JPEG decoder alignment
    // ESP32-P4 JPEG decoder automatically aligns both width and height to 16 bytes
    uint32_t aligned_width = ((header_info.width + 15) & ~15);   // 16-byte width alignment
    uint32_t aligned_height = ((header_info.height + 15) & ~15); // 16-byte height alignment
    
    size_t calculated_size = output->width * output->height * BYTES_PER_PIXEL_RGB565;
    size_t aligned_buffer_size = aligned_width * aligned_height * BYTES_PER_PIXEL_RGB565;
    
    output->data_size = calculated_size;
    
    // Allocate output buffer using JPEG decoder alignment
    jpeg_decode_memory_alloc_cfg_t mem_cfg = {
        .buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER,
    };
    
    size_t allocated_size;
    output->rgb_data = (uint8_t*)jpeg_alloc_decoder_mem(aligned_buffer_size, &mem_cfg, &allocated_size);
    if (!output->rgb_data) {
        ESP_LOGE(TAG, "Failed to allocate JPEG output buffer: need %zu bytes", aligned_buffer_size);
        
        // Log memory status for debugging
        size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        size_t free_spiram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        ESP_LOGE(TAG, "Available memory: internal=%zu, SPIRAM=%zu", free_internal, free_spiram);
        
        return ESP_ERR_NO_MEM;
    }
    
    // Cache sync for input data - ensure hardware sees latest data
    ret = safe_cache_sync((void*)data, data_size, ESP_CACHE_MSYNC_FLAG_DIR_M2C);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Input cache sync failed: %s", esp_err_to_name(ret));
    }
    
    // Cache sync for output buffer - clear old cache data
    ret = safe_cache_sync((void*)output->rgb_data, allocated_size, 
                         ESP_CACHE_MSYNC_FLAG_DIR_M2C | ESP_CACHE_MSYNC_FLAG_INVALIDATE);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Output buffer cache sync failed: %s", esp_err_to_name(ret));
    }
    
    jpeg_decode_cfg_t decode_cfg = {
        .output_format = JPEG_DECODE_OUT_FORMAT_RGB565,
        .rgb_order = JPEG_DEC_RGB_ELEMENT_ORDER_BGR,
        .conv_std = JPEG_YUV_RGB_CONV_STD_BT601,
    };
    
    uint32_t out_size;
    ret = jpeg_decoder_process(s_jpeg_decoder, &decode_cfg, data, data_size, 
                              output->rgb_data, allocated_size, &out_size);
    if (ret != ESP_OK) {
        free(output->rgb_data);
        output->rgb_data = NULL;
        ESP_LOGE(TAG, "JPEG decode failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Cache sync for result - ensure CPU sees decode results
    ret = safe_cache_sync((void*)output->rgb_data, out_size, ESP_CACHE_MSYNC_FLAG_DIR_C2M);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Result cache sync failed: %s", esp_err_to_name(ret));
    }
    
    // Update data size if different
    if (out_size != calculated_size) {
        output->data_size = out_size;
    }
    
    output->is_valid = true;
    output->owns_data = true;
    
    ESP_LOGD(TAG, "JPEG decoded: %"PRIu32"x%"PRIu32" (aligned: %"PRIu32"x%"PRIu32"), out=%"PRIu32"B", 
             output->width, output->height, aligned_width, aligned_height, out_size);
    return ESP_OK;
}

static esp_err_t decode_png_image(const uint8_t *data, size_t data_size, decoded_image_t *output)
{
    png_structp png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) {
        return ESP_FAIL;
    }
    
    png_infop info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return ESP_FAIL;
    }
    
    if (setjmp(png_jmpbuf(png_ptr))) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return ESP_FAIL;
    }
    
    uint8_t *png_data_ptr = (uint8_t *)data;
    png_set_read_fn(png_ptr, &png_data_ptr, png_read_callback);
    png_read_info(png_ptr, info_ptr);
    
    output->width = png_get_image_width(png_ptr, info_ptr);
    output->height = png_get_image_height(png_ptr, info_ptr);
    png_byte color_type = png_get_color_type(png_ptr, info_ptr);
    png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
    
    // Normalize PNG format
    if (bit_depth == 16) png_set_strip_16(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png_ptr);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA) 
        png_set_gray_to_rgb(png_ptr);
    
    bool has_transparency = (color_type & PNG_COLOR_MASK_ALPHA) || 
                           png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS);
    
    output->data_size = output->width * output->height * BYTES_PER_PIXEL_RGB565;
    
    // Allocate output buffer
    if (s_config.use_psram) {
        output->rgb_data = heap_caps_malloc(output->data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!output->rgb_data) {
        output->rgb_data = malloc(output->data_size);
    }
    
    if (!output->rgb_data) {
        png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
        return ESP_ERR_NO_MEM;
    }
    
    if (has_transparency) {
        // Handle transparent PNG
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS)) {
            png_set_tRNS_to_alpha(png_ptr);
        }
        png_read_update_info(png_ptr, info_ptr);
        
        uint8_t *row_buffer = malloc(output->width * BYTES_PER_PIXEL_RGBA8888);
        if (!row_buffer) {
            free(output->rgb_data);
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            return ESP_ERR_NO_MEM;
        }
        
        uint16_t *rgb565_data = (uint16_t*)output->rgb_data;
        
        for (uint32_t y = 0; y < output->height; y++) {
            png_read_row(png_ptr, row_buffer, NULL);
            uint16_t *output_row = rgb565_data + y * output->width;

            for (uint32_t x = 0; x < output->width; x++) {
                uint8_t r = row_buffer[x * BYTES_PER_PIXEL_RGBA8888 + 0];
                uint8_t g = row_buffer[x * BYTES_PER_PIXEL_RGBA8888 + 1];
                uint8_t b = row_buffer[x * BYTES_PER_PIXEL_RGBA8888 + 2];
                uint8_t a = row_buffer[x * BYTES_PER_PIXEL_RGBA8888 + 3];

                // Alpha blending with white background
                if (a != 255) {
                    if (a == 0) {
                        output_row[x] = RGB565_WHITE;
                        continue;
                    }
                    r = (r * a + 255 * (255 - a)) / 255;
                    g = (g * a + 255 * (255 - a)) / 255;
                    b = (b * a + 255 * (255 - a)) / 255;
                }

                output_row[x] = ((r >> RGB888_TO_RGB565_R_SHIFT) << RGB565_R_SHIFT) |
                               ((g >> RGB888_TO_RGB565_G_SHIFT) << RGB565_G_SHIFT) |
                               (b >> RGB888_TO_RGB565_B_SHIFT);
            }
        }
        free(row_buffer);
        
    } else {
        // Handle opaque PNG
        if (color_type & PNG_COLOR_MASK_ALPHA) {
            png_set_strip_alpha(png_ptr);
        }
        png_read_update_info(png_ptr, info_ptr);
        
        uint8_t *row_buffer = malloc(output->width * BYTES_PER_PIXEL_RGB888);
        if (!row_buffer) {
            free(output->rgb_data);
            png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
            return ESP_ERR_NO_MEM;
        }
        
        uint16_t *rgb565_data = (uint16_t*)output->rgb_data;
        
        for (uint32_t y = 0; y < output->height; y++) {
            png_read_row(png_ptr, row_buffer, NULL);
            uint16_t *output_row = rgb565_data + y * output->width;
            convert_rgb888_to_rgb565(row_buffer, output_row, output->width);
        }
        free(row_buffer);
    }
    
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
    
    output->is_valid = true;
    output->owns_data = true;
    
    ESP_LOGI(TAG, "PNG decoded (%s): %"PRIu32"x%"PRIu32, 
             has_transparency ? "alpha" : "opaque", output->width, output->height);
    return ESP_OK;
}

esp_err_t image_decoder_init(const decoder_config_t *config)
{
    if (!config) {
        return ESP_ERR_INVALID_ARG;
    }
    
    s_config = *config;
    
    // Initialize shared JPEG decoder manager
    esp_err_t ret = shared_jpeg_decoder_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize shared JPEG decoder: %d", ret);
        return ret;
    }
    
    // Get shared JPEG decoder handle
    ret = shared_jpeg_decoder_acquire(&s_jpeg_decoder);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to acquire shared JPEG decoder: %d", ret);
        return ret;
    }
    
    ESP_LOGI(TAG, "Image decoder initialized with shared JPEG: max %dx%d, PSRAM=%s", 
             s_config.max_width, s_config.max_height, 
             s_config.use_psram ? "yes" : "no");
    return ESP_OK;
}

esp_err_t image_decoder_deinit(void)
{
    if (s_jpeg_decoder) {
        // Release shared JPEG decoder
        shared_jpeg_decoder_release();
        s_jpeg_decoder = NULL;
        ESP_LOGI(TAG, "Image decoder deinitialized, shared JPEG decoder released");
    }
    return ESP_OK;
}

esp_err_t image_decoder_decode(const uint8_t *data, size_t data_size, 
                              image_format_t format, decoded_image_t *output)
{
    if (!data || !output || data_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(output, 0, sizeof(decoded_image_t));
    
    switch (format) {
        case IMAGE_FORMAT_JPEG:
            return decode_jpeg_image(data, data_size, output);
        case IMAGE_FORMAT_PNG:
            return decode_png_image(data, data_size, output);
        default:
            ESP_LOGE(TAG, "Unsupported format: %d", format);
            return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t image_decoder_get_info(const uint8_t *data, size_t data_size,
                                image_format_t format, uint32_t *width, uint32_t *height)
{
    if (!data || !width || !height || data_size == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    
    switch (format) {
        case IMAGE_FORMAT_JPEG: {
            jpeg_decode_picture_info_t info;
            esp_err_t ret = jpeg_decoder_get_info(data, data_size, &info);
        if (ret == ESP_OK) {
                *width = info.width;
                *height = info.height;
        }
        return ret;
        }
        case IMAGE_FORMAT_PNG:
            // PNG info parsing would require libpng, skip for now
            return ESP_ERR_NOT_SUPPORTED;
        default:
    return ESP_ERR_NOT_SUPPORTED;
    }
}

void image_decoder_free_image(decoded_image_t *image)
{
    if (image && image->rgb_data && image->owns_data) {
        free(image->rgb_data);
        memset(image, 0, sizeof(decoded_image_t));
    }
} 