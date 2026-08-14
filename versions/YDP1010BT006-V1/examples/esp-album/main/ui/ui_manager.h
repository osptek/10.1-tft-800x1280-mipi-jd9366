/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "image_decoder.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    UI_EVENT_SWIPE_LEFT,
    UI_EVENT_SWIPE_RIGHT, 
    UI_EVENT_SWIPE_UP,
    UI_EVENT_SWIPE_DOWN,
    UI_EVENT_LONG_PRESS,
    UI_EVENT_TAP,
    UI_EVENT_SETTINGS_CLOSE,
    UI_EVENT_SETTINGS_CANCEL
} ui_event_t;

typedef enum {
    UI_MODE_IMAGE,
    UI_MODE_VIDEO
} ui_mode_t;

typedef void (*ui_event_cb_t)(ui_event_t event, void *user_data);

/**
 * @brief Initialize UI manager
 */
esp_err_t ui_manager_init(ui_event_cb_t event_cb, void *user_data);

/**
 * @brief Deinitialize UI manager
 */
esp_err_t ui_manager_deinit(void);

/**
 * @brief Display image on screen
 */
esp_err_t ui_manager_display_image(const decoded_image_t *image);

/**
 * @brief Show loading spinner
 */
esp_err_t ui_manager_show_loading(void);

/**
 * @brief Hide loading spinner
 */
esp_err_t ui_manager_hide_loading(void);

/**
 * @brief Show settings panel
 */
esp_err_t ui_manager_show_settings(uint32_t current_interval);

/**
 * @brief Hide settings panel
 */
esp_err_t ui_manager_hide_settings(void);

/**
 * @brief Update progress display
 */
esp_err_t ui_manager_update_progress(int current, int total);

/**
 * @brief Get selected interval from settings
 */
uint32_t ui_manager_get_selected_interval(void);

/**
 * @brief Switch UI mode between image and video
 */
esp_err_t ui_manager_switch_mode(ui_mode_t mode);

/**
 * @brief Display video frame
 */
esp_err_t ui_manager_display_video_frame(const uint8_t *frame_buffer, uint32_t width, uint32_t height);

/**
 * @brief Show volume control
 */
esp_err_t ui_manager_show_volume(int volume_percent);

/**
 * @brief Show floating time display
 */
esp_err_t ui_manager_show_time(void);

/**
 * @brief Hide floating time display
 */
esp_err_t ui_manager_hide_time(void);

#ifdef __cplusplus
}
#endif 