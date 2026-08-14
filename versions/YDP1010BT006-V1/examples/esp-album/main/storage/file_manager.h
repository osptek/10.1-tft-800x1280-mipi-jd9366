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

// Media type definition
typedef enum {
    MEDIA_TYPE_UNKNOWN,
    MEDIA_TYPE_IMAGE,
    MEDIA_TYPE_VIDEO
} media_type_t;

// SD card status
typedef enum {
    SD_STATUS_MOUNTED,
    SD_STATUS_UNMOUNTED,
    SD_STATUS_ERROR
} sd_status_t;

// SD card read optimization
#define SDCARD_READ_BUFFER_SIZE    (32 * 1024)  // 32KB buffer for optimal SD read performance

// File manager functions
esp_err_t file_manager_init(void);
esp_err_t file_manager_deinit(void);
esp_err_t file_manager_scan_images(const char *dir_path, photo_collection_t *collection);
esp_err_t file_manager_load_image(const char *file_path, uint8_t **data, size_t *size);
void file_manager_sort_collection(photo_collection_t *collection, sort_mode_t mode);
sd_status_t file_manager_get_sd_status(void);
bool file_manager_is_supported_image(const char *filename);

// Media file support
bool file_manager_is_supported_media(const char *filename);
media_type_t file_manager_get_media_type(const char *filename);

#ifdef __cplusplus
}
#endif 