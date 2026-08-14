/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "file_manager.h"
#include "photo_album_constants.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <dirent.h>
#include <sys/stat.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "esp_timer.h"
#include "driver/jpeg_decode.h"

static const char *TAG = "file_mgr";
static sd_status_t s_sd_status = SD_STATUS_UNMOUNTED;

bool file_manager_is_supported_image(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    
    return (strcasecmp(ext, FILE_EXT_JPG) == 0 || 
            strcasecmp(ext, FILE_EXT_JPEG) == 0 || 
            strcasecmp(ext, FILE_EXT_PNG) == 0);
}

bool file_manager_is_supported_media(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return false;
    
    // Image formats
    if (strcasecmp(ext, FILE_EXT_JPG) == 0 || 
        strcasecmp(ext, FILE_EXT_JPEG) == 0 || 
        strcasecmp(ext, FILE_EXT_PNG) == 0) {
        return true;
    }
    
    // Video formats
    if (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".avi") == 0) {
        return true;
    }
    
    return false;
}

media_type_t file_manager_get_media_type(const char *filename)
{
    const char *ext = strrchr(filename, '.');
    if (!ext) return MEDIA_TYPE_UNKNOWN;
    
    if (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".avi") == 0) {
        return MEDIA_TYPE_VIDEO;
    }
    
    if (strcasecmp(ext, FILE_EXT_JPG) == 0 || 
        strcasecmp(ext, FILE_EXT_JPEG) == 0 || 
        strcasecmp(ext, FILE_EXT_PNG) == 0) {
        return MEDIA_TYPE_IMAGE;
    }
    
    return MEDIA_TYPE_UNKNOWN;
}

static int compare_by_name(const void *a, const void *b)
{
    const image_file_info_t *fa = (const image_file_info_t *)a;
    const image_file_info_t *fb = (const image_file_info_t *)b;
    return strcasecmp(fa->filename, fb->filename);
}

static int compare_by_time(const void *a, const void *b)
{
    const image_file_info_t *fa = (const image_file_info_t *)a;
    const image_file_info_t *fb = (const image_file_info_t *)b;
    return (fa->modify_time > fb->modify_time) ? 1 : -1;
}

static bool validate_image_file(const char *file_path, image_format_t format, size_t file_size)
{
    // Basic size limits (consistent with runtime validate_file_for_decoding)
    if (file_size < 100) {
        ESP_LOGW(TAG, "Skip %s: file too small (%zu bytes)", file_path, file_size);
        return false;
    }
    if (file_size > (10 * 1024 * 1024)) { // >10 MB
        ESP_LOGW(TAG, "Skip %s: file too large (%zu bytes)", file_path, file_size);
        return false;
    }

    // Magic number verification for all image formats
    // Use static buffer to avoid large stack usage (4KB)
    static uint8_t header_buf[4096];
    
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGW(TAG, "Skip %s: cannot open file", file_path);
        return false;
    }
    
    ssize_t bytes = read(fd, header_buf, sizeof(header_buf));
    close(fd);
    if (bytes < 16) { // Need at least 16 bytes for magic number checks
        ESP_LOGW(TAG, "Skip %s: insufficient header data (%zd bytes)", file_path, bytes);
        return false;
    }

    // Verify magic numbers to detect file format spoofing
    bool magic_valid = false;
    if (format == IMAGE_FORMAT_JPEG) {
        // JPEG: FF D8 FF (SOI marker)
        if (bytes >= 3 && header_buf[0] == 0xFF && header_buf[1] == 0xD8 && header_buf[2] == 0xFF) {
            magic_valid = true;
        }
    } else if (format == IMAGE_FORMAT_PNG) {
        // PNG: 89 50 4E 47 0D 0A 1A 0A (8-byte signature)
        if (bytes >= 8 && 
            header_buf[0] == 0x89 && header_buf[1] == 0x50 && header_buf[2] == 0x4E && header_buf[3] == 0x47 &&
            header_buf[4] == 0x0D && header_buf[5] == 0x0A && header_buf[6] == 0x1A && header_buf[7] == 0x0A) {
            magic_valid = true;
        }
    } else {
        // Check common formats by magic numbers regardless of claimed format
        if (bytes >= 8 && 
            header_buf[0] == 0x89 && header_buf[1] == 0x50 && header_buf[2] == 0x4E && header_buf[3] == 0x47) {
            // Actually a PNG file
            format = IMAGE_FORMAT_PNG;
            magic_valid = true;
        } else if (bytes >= 3 && header_buf[0] == 0xFF && header_buf[1] == 0xD8 && header_buf[2] == 0xFF) {
            // Actually a JPEG file
            format = IMAGE_FORMAT_JPEG;
            magic_valid = true;
        } else if (bytes >= 6 && 
                   ((header_buf[0] == 'G' && header_buf[1] == 'I' && header_buf[2] == 'F' && 
                     header_buf[3] == '8' && (header_buf[4] == '7' || header_buf[4] == '9') && header_buf[5] == 'a') ||
                    (header_buf[0] == 0x42 && header_buf[1] == 0x4D))) { // BMP: "BM"
            // GIF87a/GIF89a or BMP format - we don't support these currently
            ESP_LOGW(TAG, "Skip %s: unsupported format (GIF/BMP detected)", file_path);
            return false;
        }
    }
    
    if (!magic_valid) {
        ESP_LOGW(TAG, "Skip %s: invalid magic number or corrupted header", file_path);
        return false;
    }

    if (format == IMAGE_FORMAT_JPEG) {
        jpeg_decode_picture_info_t pic_info;
        esp_err_t ret = jpeg_decoder_get_info(header_buf, (uint32_t)bytes, &pic_info);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Skip %s: JPEG header parse err (%s)", file_path, esp_err_to_name(ret));
            return false;
        }

        // Check for reasonable dimensions to avoid memory exhaustion
        if (pic_info.width == 0 || pic_info.height == 0 || 
            pic_info.width > 8192 || pic_info.height > 8192) {
            ESP_LOGW(TAG, "Skip %s: invalid dimensions %ux%u", file_path, pic_info.width, pic_info.height);
            return false;
        }

        if ((pic_info.width % JPEG_ALIGNMENT) != 0 || (pic_info.height % JPEG_ALIGNMENT) != 0) {
            ESP_LOGW(TAG, "Skip %s: dimensions %ux%u not %d-byte aligned", file_path, pic_info.width, pic_info.height, JPEG_ALIGNMENT);
            return false;
        }
        
        // Enhanced resolution check based on available decode buffer
        // Calculate required output buffer size for RGB565 format
        uint64_t required_bytes = (uint64_t)pic_info.width * pic_info.height * 2; // 2 bytes per pixel for RGB565
        const uint64_t MAX_DECODE_BUFFER = PRACTICAL_DECODE_BUFFER_LIMIT; // Use practical decode buffer limit
        
        if (required_bytes > MAX_DECODE_BUFFER) {
            ESP_LOGW(TAG, "Skip %s: %ux%u requires %llu bytes, exceeds decode buffer %llu bytes", 
                     file_path, pic_info.width, pic_info.height, required_bytes, MAX_DECODE_BUFFER);
            return false;
        }
        
        if (pic_info.width > MAX_DECODE_WIDTH || pic_info.height > MAX_DECODE_HEIGHT) {
            // Allow portrait images that exceed height limit as long as total pixels fit within budget
            uint64_t total_pixels = (uint64_t)pic_info.width * pic_info.height;
            uint64_t max_pixels   = (uint64_t)MAX_DECODE_WIDTH * MAX_DECODE_HEIGHT;
            if (total_pixels > max_pixels) {
                ESP_LOGW(TAG, "Skip %s: %ux%u (%llu px) exceeds pixel budget %llu px", 
                         file_path, pic_info.width, pic_info.height, total_pixels, max_pixels);
                return false;
            }
        }
    } else if (format == IMAGE_FORMAT_PNG) {
        // Basic PNG header validation
        if (bytes < 33) { // PNG needs at least IHDR chunk (33 bytes minimum)
            ESP_LOGW(TAG, "Skip %s: PNG header too short", file_path);
            return false;
        }
        
        // Extract width and height from IHDR chunk (bytes 16-23)
        uint32_t width = (header_buf[16] << 24) | (header_buf[17] << 16) | (header_buf[18] << 8) | header_buf[19];
        uint32_t height = (header_buf[20] << 24) | (header_buf[21] << 16) | (header_buf[22] << 8) | header_buf[23];
        
        if (width == 0 || height == 0 || width > 8192 || height > 8192) {
            ESP_LOGW(TAG, "Skip %s: invalid PNG dimensions %ux%u", file_path, width, height);
            return false;
        }
        
        // Check decode buffer requirements for PNG
        uint64_t required_bytes = (uint64_t)width * height * 2; // RGB565
        const uint64_t MAX_DECODE_BUFFER = PRACTICAL_DECODE_BUFFER_LIMIT;
        
        if (required_bytes > MAX_DECODE_BUFFER) {
            ESP_LOGW(TAG, "Skip %s: PNG %ux%u requires %llu bytes, exceeds decode buffer", 
                     file_path, width, height, required_bytes);
            return false;
        }
        
        // Check against maximum processing dimensions
        if (width > MAX_DECODE_WIDTH || height > MAX_DECODE_HEIGHT) {
            uint64_t total_pixels = (uint64_t)width * height;
            uint64_t max_pixels = (uint64_t)MAX_DECODE_WIDTH * MAX_DECODE_HEIGHT;
            if (total_pixels > max_pixels) {
                ESP_LOGW(TAG, "Skip %s: PNG %ux%u (%llu px) exceeds pixel budget %llu px", 
                         file_path, width, height, total_pixels, max_pixels);
                return false;
            }
        }
    }
    
    return true;
}

static esp_err_t scan_directory_recursive(const char *dir_path, photo_collection_t *collection)
{
    DIR *dir = opendir(dir_path);
    if (!dir) {
        ESP_LOGE(TAG, "Failed to open dir: %s", dir_path);
        return ESP_FAIL;
    }
    
    struct dirent *entry;
    struct stat file_stat;
    char full_path[MAX_FILENAME_LEN];
    
    while ((entry = readdir(dir)) != NULL && collection->total_count < MAX_FILES_COUNT) {
        if (entry->d_name[0] == HIDDEN_FILE_PREFIX) continue; // Skip hidden files
        
        snprintf(full_path, sizeof(full_path), "%s" DIR_SEPARATOR "%s", dir_path, entry->d_name);
        
        if (stat(full_path, &file_stat) == STAT_SUCCESS) {
            if (S_ISDIR(file_stat.st_mode) && collection->scan_subdirs) {
                scan_directory_recursive(full_path, collection);
            } else if (S_ISREG(file_stat.st_mode) && file_manager_is_supported_media(entry->d_name)) {
                // Validate image files before adding to collection
                image_format_t fmt = IMAGE_FORMAT_UNKNOWN;
                const char *ext = strrchr(entry->d_name, '.');
                if (ext) {
                    if (strcasecmp(ext, FILE_EXT_JPG) == 0 || strcasecmp(ext, FILE_EXT_JPEG) == 0) {
                        fmt = IMAGE_FORMAT_JPEG;
                    } else if (strcasecmp(ext, FILE_EXT_PNG) == 0) {
                        fmt = IMAGE_FORMAT_PNG;
                    }
                }
                // For video or validated image pass-through
                bool add_file = true;
                if (fmt != IMAGE_FORMAT_UNKNOWN) {
                    add_file = validate_image_file(full_path, fmt, file_stat.st_size);
                }
                if (!add_file) {
                    continue; // Skip invalid file
                }

                image_file_info_t *file_info = &collection->files[collection->total_count];
                snprintf(file_info->filename, MAX_FILENAME_LEN, "%s", entry->d_name);
                snprintf(file_info->full_path, MAX_FILENAME_LEN, "%s", full_path);
                file_info->file_size = file_stat.st_size;
                file_info->modify_time = file_stat.st_mtime;
                file_info->format = fmt != IMAGE_FORMAT_UNKNOWN ? fmt : IMAGE_FORMAT_JPEG; // default for video/JPEG

                collection->total_count++;
            }
        }
    }
    
    closedir(dir);
    return ESP_OK;
}

esp_err_t file_manager_init(void)
{
    esp_err_t ret = bsp_sdcard_mount();
    if (ret == ESP_OK) {
        s_sd_status = SD_STATUS_MOUNTED;
        ESP_LOGD(TAG, "SD card mounted successfully");
        
        // Create photo directory if not exists
        struct stat st;
        if (stat(PHOTO_BASE_PATH, &st) != STAT_SUCCESS) {
            if (mkdir(PHOTO_BASE_PATH, PHOTO_DIR_PERMISSIONS) == STAT_SUCCESS) {
                ESP_LOGD(TAG, "Created photo directory: %s", PHOTO_BASE_PATH);
            }
        }
    } else {
        s_sd_status = SD_STATUS_ERROR;
        ESP_LOGE(TAG, "Failed to mount SD card");
    }
    
    return ret;
}

esp_err_t file_manager_deinit(void)
{
    esp_err_t ret = bsp_sdcard_unmount();
    s_sd_status = SD_STATUS_UNMOUNTED;
    return ret;
}

esp_err_t file_manager_scan_images(const char *dir_path, photo_collection_t *collection)
{
    if (s_sd_status != SD_STATUS_MOUNTED) {
        ESP_LOGE(TAG, "SD card not mounted");
        return ESP_ERR_INVALID_STATE;
    }
    
    collection->total_count = 0;
    ESP_LOGD(TAG, "Scanning images in: %s", dir_path);
    
    esp_err_t ret = scan_directory_recursive(dir_path, collection);
    if (ret == ESP_OK) {
        ESP_LOGD(TAG, "Found %d images", collection->total_count);
    }
    
    return ret;
}

esp_err_t file_manager_load_image(const char *file_path, uint8_t **data, size_t *size)
{
    if (s_sd_status != SD_STATUS_MOUNTED) {
        return ESP_ERR_INVALID_STATE;
    }
    
    int64_t start_time = esp_timer_get_time();
    
    // Use stat to get file size efficiently
    struct stat file_stat;
    if (stat(file_path, &file_stat) != 0) {
        ESP_LOGE(TAG, "Failed to stat file: %s", file_path);
        return ESP_FAIL;
    }
    
    *size = file_stat.st_size;
    
    // Add sanity check for file size - typical image files should not exceed 50MB
    const size_t MAX_REASONABLE_IMAGE_SIZE = 50 * 1024 * 1024;  // 50MB
    if (*size > MAX_REASONABLE_IMAGE_SIZE) {
        ESP_LOGE(TAG, "File size too large: %zu bytes (max: %zu bytes) for file: %s", 
                 *size, MAX_REASONABLE_IMAGE_SIZE, file_path);
        return ESP_ERR_INVALID_SIZE;
    }
    
    // Also check for zero or very small files
    if (*size == 0) {
        ESP_LOGE(TAG, "File is empty: %s", file_path);
        return ESP_ERR_INVALID_SIZE;
    }
    
    if (*size < 100) {  // Most image files should be at least 100 bytes
        ESP_LOGW(TAG, "File suspiciously small: %zu bytes for %s", *size, file_path);
    }
    
    ESP_LOGI(TAG, "Loading file: %s (size: %zu bytes)", file_path, *size);
    
    // Open file using POSIX interface for better performance
    int fd = open(file_path, O_RDONLY);
    if (fd < 0) {
        ESP_LOGE(TAG, "Failed to open: %s", file_path);
        return ESP_FAIL;
    }
    
    // Allocate memory (prefer PSRAM for large images)
    *data = heap_caps_malloc(*size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!*data) {
        *data = malloc(*size);
        if (!*data) {
            ESP_LOGE(TAG, "Failed to allocate memory for %zu bytes", *size);
            close(fd);
            return ESP_ERR_NO_MEM;
        }
        ESP_LOGD(TAG, "Using internal RAM for %zu bytes (PSRAM unavailable)", *size);
    } else {
        ESP_LOGD(TAG, "Using PSRAM for %zu bytes", *size);
    }
    
    // Read file using POSIX read() with optimized buffer size for better SD card performance
    size_t bytes_read = 0;
    uint8_t *buffer_ptr = *data;
    
    // Use larger buffer for big files to optimize SD card throughput
    const size_t read_chunk_size = (*size > SDCARD_READ_BUFFER_SIZE) ? 
                                  SDCARD_READ_BUFFER_SIZE : *size;
    
    ESP_LOGD(TAG, "Reading %zu bytes in chunks of %zu bytes", *size, read_chunk_size);
    
    while (bytes_read < *size) {
        size_t remaining = *size - bytes_read;
        size_t to_read = (remaining > read_chunk_size) ? read_chunk_size : remaining;
        
        ssize_t result = read(fd, buffer_ptr + bytes_read, to_read);
        if (result < 0) {
            ESP_LOGE(TAG, "Failed to read from file: %s (errno: %d)", file_path, errno);
            free(*data);
            *data = NULL;
            close(fd);
            return ESP_FAIL;
        } else if (result == 0) {
            // End of file reached unexpectedly
            ESP_LOGE(TAG, "Unexpected EOF in file: %s (read %zu/%zu bytes)", 
                     file_path, bytes_read, *size);
            free(*data);
            *data = NULL;
            close(fd);
            return ESP_FAIL;
        }
        bytes_read += result;
        
        // Log progress for large files
        if (*size > LARGE_FILE_THRESHOLD && bytes_read % PROGRESS_REPORT_INTERVAL == 0) {
            ESP_LOGD(TAG, "Read progress: %zu/%zu bytes (%.1f%%)", 
                     bytes_read, *size, (float)bytes_read * PERCENTAGE_MULTIPLIER / *size);
        }
    }
    
    close(fd);
    
    // Calculate and log performance metrics
    int64_t end_time = esp_timer_get_time();
    int64_t duration_us = end_time - start_time;
    float duration_ms = duration_us / MICROSECONDS_PER_MILLISECOND;
    float throughput_mbps = (*size * BITS_PER_BYTE) / duration_us;  // Megabits per second
    
    ESP_LOGI(TAG, "Loaded %zu bytes in %.1fms (%.2f MB/s) from %s", 
             bytes_read, duration_ms, throughput_mbps, 
             strrchr(file_path, '/') ? strrchr(file_path, '/') + 1 : file_path);
    
    return ESP_OK;
}

void file_manager_sort_collection(photo_collection_t *collection, sort_mode_t mode)
{
    if (collection->total_count <= MIN_COLLECTION_SIZE_FOR_SORT) return;
    
    switch (mode) {
        case SORT_BY_NAME:
            qsort(collection->files, collection->total_count, 
                  sizeof(image_file_info_t), compare_by_name);
            break;
        case SORT_BY_DATE:
            qsort(collection->files, collection->total_count, 
                  sizeof(image_file_info_t), compare_by_time);
            break;
        default:
            break;
    }
    
    ESP_LOGI(TAG, "Sorted %d files by %s", collection->total_count,
             mode == SORT_BY_NAME ? "name" : "date");
}

sd_status_t file_manager_get_sd_status(void)
{
    return s_sd_status;
} 