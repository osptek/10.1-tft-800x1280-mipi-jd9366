/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include <time.h>
#include "esp_err.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "bsp/esp-bsp.h"

#ifdef __cplusplus
extern "C" {
#endif

// Configuration constants
#define PHOTO_BASE_PATH         "/sdcard/photos"
#define MAX_FILENAME_LEN        256
#define MAX_FILES_COUNT         1000
#define SCREEN_WIDTH            BSP_LCD_H_RES
#define SCREEN_HEIGHT           BSP_LCD_V_RES
#define DEFAULT_SLIDESHOW_MS    5000
#define IDLE_TIMEOUT_MS         10000

// Memory pool configuration  
#define MEMORY_POOL_SIZE        (6 * 1024 * 1024)     // 6MB fixed memory pool
#define MAX_DECODE_WIDTH        1920                   // Max supported width
#define MAX_DECODE_HEIGHT       1080                   // Max supported height
#define JPEG_ALIGNMENT          8                      // JPEG decoder alignment requirement

// Image format types
typedef enum {
    IMAGE_FORMAT_JPEG,
    IMAGE_FORMAT_PNG,
    IMAGE_FORMAT_UNKNOWN
} image_format_t;

// Sort modes for image files
typedef enum {
    SORT_BY_NAME,
    SORT_BY_DATE,
    SORT_BY_SIZE
} sort_mode_t;

// Scale modes for image display
typedef enum {
    SCALE_MODE_FIT,        // Scale to fit screen
    SCALE_MODE_FILL,       // Crop to fill screen
    SCALE_MODE_CENTER,     // Center on screen
    SCALE_MODE_CROP_ONLY   // Direct crop without scaling (for landscape images larger than screen)
} scale_mode_t;

// Image file information
typedef struct {
    char filename[MAX_FILENAME_LEN];
    char full_path[MAX_FILENAME_LEN];
    image_format_t format;
    size_t file_size;
    time_t modify_time;
} image_file_info_t;

// Photo collection structure
typedef struct {
    image_file_info_t *files;
    int total_count;
    int current_index;
    char base_directory[128];
    bool scan_subdirs;
} photo_collection_t;

// Decoded image data
typedef struct {
    uint8_t *rgb_data;
    uint32_t width;
    uint32_t height;
    size_t data_size;
    bool is_valid;
    bool owns_data;      // True if this struct owns the rgb_data memory
} decoded_image_t;

// Unified memory pool for image operations
typedef struct {
    uint8_t *pool_buffer;           // Fixed memory pool
    size_t pool_size;               // Pool size
    size_t used_size;               // Currently used size
    bool is_allocated;              // Pool allocation status
    SemaphoreHandle_t mutex;        // Pool access mutex
} memory_pool_t;

// Slideshow control
typedef struct {
    esp_timer_handle_t timer;
    uint32_t interval_ms;
    bool is_running;
    bool manual_control;
    esp_timer_handle_t idle_timer;
} slideshow_ctrl_t;

// Main photo album context (cache removed, memory pool added)
typedef struct {
    photo_collection_t *collection;
    memory_pool_t memory_pool;      // Unified memory pool
    slideshow_ctrl_t slideshow;
    SemaphoreHandle_t mutex;
    bool initialized;
} photo_album_t;

// Public API functions
esp_err_t photo_album_init(void);
esp_err_t photo_album_start(void);
esp_err_t photo_album_deinit(void);
esp_err_t photo_album_refresh(void);
esp_err_t photo_album_next(void);
esp_err_t photo_album_prev(void);
esp_err_t photo_album_goto(int index);
esp_err_t photo_album_set_interval(uint32_t interval_ms);
esp_err_t photo_album_pause(void);
esp_err_t photo_album_pause_for_usb(void);  // USB-specific pause (no auto-resume)
esp_err_t photo_album_resume(void);
bool photo_album_is_paused(void);
int photo_album_get_total_count(void);
int photo_album_get_current_index(void);
const image_file_info_t* photo_album_get_current_info(void);

#ifdef __cplusplus
}
#endif 