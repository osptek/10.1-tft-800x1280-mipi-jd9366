/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ========================================
// MEMORY POOL AND ALIGNMENT CONSTANTS
// ========================================

// Memory pool configuration  
#define MEMORY_POOL_SIZE        (6 * 1024 * 1024)     // 6MB fixed memory pool
#define MAX_DECODE_WIDTH        1920                   // Max supported width (1080P FHD)
#define MAX_DECODE_HEIGHT       1080                   // Max supported height (1080P FHD)
#define JPEG_ALIGNMENT          8                      // JPEG decoder alignment requirement

// JPEG decode buffer limits - actual constraint is memory availability
// Note: RGB565 format requires width × height × 2 bytes for decode buffer
// Current shared JPEG decoder needs sufficient buffer for target resolution
// For larger images, the decode buffer size needs to be increased accordingly:
// - 1920×1080 (2.07MP) requires ~4.1MB decode buffer
// - 1280×720 (0.92MP) requires ~1.8MB decode buffer
// - For 1024×600 display, we set practical limit to support 1080P content
#define PRACTICAL_DECODE_BUFFER_LIMIT       (4 * 1024 * 1024)  // 4MB practical decode buffer limit

// ========================================
// IMAGE DECODER CONSTANTS
// ========================================

// Maximum image dimensions
#define IMAGE_DECODER_MAX_WIDTH     MAX_DECODE_WIDTH    // Maximum decode width
#define IMAGE_DECODER_MAX_HEIGHT    MAX_DECODE_HEIGHT   // Maximum decode height

// ========================================
// COLOR FORMAT CONSTANTS
// ========================================

// Bytes per pixel for different formats
#define BYTES_PER_PIXEL_RGB565              2       // RGB565 format: 2 bytes per pixel
#define BYTES_PER_PIXEL_RGB888              3       // RGB888 format: 3 bytes per pixel
#define BYTES_PER_PIXEL_RGBA8888            4       // RGBA8888 format: 4 bytes per pixel

// RGB888 to RGB565 conversion constants
#define RGB888_TO_RGB565_R_SHIFT            3       // RGB888->RGB565 R channel right shift
#define RGB888_TO_RGB565_G_SHIFT            2       // RGB888->RGB565 G channel right shift
#define RGB888_TO_RGB565_B_SHIFT            3       // RGB888->RGB565 B channel right shift

// RGB565 format bit masks and shifts
#define RGB565_R_MASK                       0xF800  // RGB565 R channel mask (bits 15-11)
#define RGB565_G_MASK                       0x07E0  // RGB565 G channel mask (bits 10-5)
#define RGB565_B_MASK                       0x001F  // RGB565 B channel mask (bits 4-0)
#define RGB565_R_SHIFT                      11      // RGB565 R channel bit shift
#define RGB565_G_SHIFT                      5       // RGB565 G channel bit shift

// RGB565 to RGB888 conversion constants
#define RGB565_TO_RGB888_R_SHIFT            3       // RGB565->RGB888 R channel left shift
#define RGB565_TO_RGB888_G_SHIFT            2       // RGB565->RGB888 G channel left shift
#define RGB565_TO_RGB888_B_SHIFT            3       // RGB565->RGB888 B channel left shift

// RGB565 color constants
#define RGB565_WHITE                        0xFFFF  // RGB565 white color
#define RGB565_BLACK                        0x0000  // RGB565 black color

// ========================================
// TASK AND SYSTEM CONSTANTS  
// ========================================

// Task configuration
#define PRELOAD_TASK_STACK_SIZE             8192    // Preload task stack size (increased for large image decoding)
#define PRELOAD_TASK_PRIORITY               5       // Preload task priority
#define PRELOAD_TASK_DELAY_MS               10      // Preload task delay

// Collection management  
#define INVALID_INDEX                       -1      // Invalid index identifier
#define PROGRESS_INDEX_OFFSET               1       // Progress display index offset (1-based)
#define MIN_COLLECTION_SIZE_FOR_SORT        1       // Minimum collection size for sorting

// ========================================
// FILE SYSTEM CONSTANTS
// ========================================

// Directory and file permissions
#define PHOTO_DIR_PERMISSIONS               0755    // Photo directory permissions

// SD card read optimization
#define SDCARD_READ_BUFFER_SIZE             (32 * 1024)     // 32KB SD card read buffer

// ========================================
// VIDEO PROCESSING CONSTANTS
// ========================================

// Video player buffer settings
#define MAX_VIDEO_WIDTH                     MAX_DECODE_WIDTH    // Maximum video width  
#define MAX_VIDEO_HEIGHT                    MAX_DECODE_HEIGHT   // Maximum video height
#define VIDEO_BUFFER_SIZE                   (MAX_VIDEO_WIDTH * MAX_VIDEO_HEIGHT * BYTES_PER_PIXEL_RGB565)
#define DEFAULT_AUDIO_VOLUME                50      // Default audio volume (0-100)

// ========================================
// UI AND DISPLAY CONSTANTS
// ========================================

// Loading screen constants
#define LOADING_SPINNER_SIZE                50      // Loading spinner size in pixels
#define LOADING_TEXT_SIZE                   24      // Loading text font size

// Progress display constants
#define PROGRESS_BAR_HEIGHT                 6       // Progress bar height in pixels
#define PROGRESS_TEXT_MARGIN                10      // Progress text margin in pixels

// ========================================
// IMAGE PROCESSING CONSTANTS
// ========================================

// Image decoder limits
#define IMAGE_DECODER_JPEG_TIMEOUT_MS       5000    // JPEG decode timeout

// PNG format constants
#define PNG_HEADER_MIN_SIZE                 24      // PNG file header minimum bytes
#define PNG_SIGNATURE_SIZE                  8       // PNG signature size
#define PNG_WIDTH_OFFSET_START              16      // PNG width info start offset
#define PNG_WIDTH_OFFSET_END                19      // PNG width info end offset
#define PNG_HEIGHT_OFFSET_START             20      // PNG height info start offset
#define PNG_HEIGHT_OFFSET_END               23      // PNG height info end offset

// Image processor constants
#define PPA_MAX_PENDING_TRANSACTIONS        1       // PPA max pending transactions
#define MEMORY_ALIGNMENT_BYTES              64      // Memory alignment bytes

// ========================================
// TASK AND SYSTEM CONSTANTS  
// ========================================

// Cache and collection limits
#define CACHE_REPLACEMENT_SLOT              0       // Cache replacement slot
#define PRELOAD_TARGET_COUNT                2       // Preload target count (front/back)

// ========================================
// FILE SYSTEM CONSTANTS
// ========================================

// SD card read optimization
#define LARGE_FILE_THRESHOLD                (100 * 1024)    // Large file threshold (100KB)
#define PROGRESS_REPORT_INTERVAL            (512 * 1024)    // Progress report interval (512KB)

// File system constants
#define HIDDEN_FILE_PREFIX                  '.'     // Hidden file prefix
#define STAT_SUCCESS                        0       // stat() success return value

// Performance calculation constants
#define MICROSECONDS_PER_MILLISECOND        1000.0f // Microseconds to milliseconds conversion
#define BITS_PER_BYTE                       8.0f    // Bytes to bits conversion
#define PERCENTAGE_MULTIPLIER               100.0f  // Percentage calculation multiplier

// ========================================
// UI CONSTANTS
// ========================================

// Loading spinner
#define PROGRESS_BAR_WIDTH                  300     // Progress bar width
#define PROGRESS_BAR_BOTTOM_OFFSET          60      // Progress bar bottom offset
#define PROGRESS_LABEL_BOTTOM_OFFSET        30      // Progress label bottom offset

// Settings panel
#define SETTINGS_PANEL_WIDTH                500     // Settings panel width (increased)
#define SETTINGS_PANEL_HEIGHT               400     // Settings panel height (increased from 300)
#define SETTINGS_PANEL_BG_COLOR             0x333333 // Settings panel background color
#define SETTINGS_PANEL_BORDER_WIDTH         2       // Settings panel border width
#define SETTINGS_PANEL_RADIUS               12      // Settings panel border radius
#define SETTINGS_BG_OPACITY                 200     // Settings background opacity

// Settings panel layout
#define SETTINGS_TITLE_TOP_OFFSET           25      // Title top offset
#define SETTINGS_TIME_LABEL_TOP_OFFSET      85      // Time label top offset
#define SETTINGS_TIME_LABEL_LEFT_OFFSET     40      // Time label left offset
#define SETTINGS_ROLLER_TOP_OFFSET          130     // Roller top offset (new, absolute positioning)
#define SETTINGS_ROLLER_WIDTH               120     // Roller width (increased)
#define SETTINGS_ROLLER_VISIBLE_ROWS        3       // Roller visible rows (reduced from 5)
#define SETTINGS_CLOSE_LABEL_BOTTOM_OFFSET  25      // Close label bottom offset

// Color constants
#define CLOSE_LABEL_COLOR                   0x888888    // Close label color

// UI timeout and delay
#define UI_DISPLAY_LOCK_TIMEOUT             0       // UI display lock timeout (0=infinite wait)

// Options string buffer
#define TIME_OPTIONS_BUFFER_SIZE            128     // Time options string buffer

// ========================================
// SLIDESHOW INTERVALS (in milliseconds)
// ========================================

#define SLIDESHOW_INTERVAL_2S               2000    // 2 second interval
#define SLIDESHOW_INTERVAL_3S               3000    // 3 second interval  
#define SLIDESHOW_INTERVAL_5S               5000    // 5 second interval
#define SLIDESHOW_INTERVAL_10S              10000   // 10 second interval
#define SLIDESHOW_INTERVAL_15S              15000   // 15 second interval
#define SLIDESHOW_INTERVAL_30S              30000   // 30 second interval
#define SLIDESHOW_INTERVAL_60S              60000   // 60 second interval

// Slideshow interval array indices
#define SLIDESHOW_INTERVALS_COUNT           7       // Slideshow interval options count

// ========================================
// STRING CONSTANTS
// ========================================

// File extension strings
#define FILE_EXT_JPG                        ".jpg"
#define FILE_EXT_JPEG                       ".jpeg"
#define FILE_EXT_PNG                        ".png"

// PNG signature string
#define PNG_SIGNATURE                       "\x89PNG\r\n\x1a\n"

// Progress display format
#define PROGRESS_FORMAT                     "%d / %d"   // Progress display format

// Directory separator
#define DIR_SEPARATOR                       "/"         // Directory separator

// ========================================
// AUDIO CONSTANTS
// ========================================

// Audio volume settings
#define VOLUME_ADJUSTMENT_STEP              10          // Volume adjustment step
#define MIN_AUDIO_VOLUME                    0           // Minimum volume
#define MAX_AUDIO_VOLUME                    100         // Maximum volume

// ========================================
// VIDEO CONSTANTS
// ========================================

// Video buffer allocation - support up to 1080P with 16-byte alignment margin
#define VIDEO_BUFFER_ALIGNMENT_MARGIN       64          // 16-byte alignment margin

#ifdef __cplusplus
}
#endif 