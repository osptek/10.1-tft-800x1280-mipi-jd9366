/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include "esp_timer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Slideshow callback for next image
typedef void (*slideshow_next_cb_t)(void);

// Slideshow controller functions
esp_err_t slideshow_ctrl_init(slideshow_next_cb_t next_cb, uint32_t interval_ms);
esp_err_t slideshow_ctrl_deinit(void);
esp_err_t slideshow_ctrl_start(void);
esp_err_t slideshow_ctrl_stop(void);
esp_err_t slideshow_ctrl_pause(void);
esp_err_t slideshow_ctrl_resume(void);
esp_err_t slideshow_ctrl_set_interval(uint32_t interval_ms);
esp_err_t slideshow_ctrl_manual_trigger(void);
bool slideshow_ctrl_is_running(void);

#ifdef __cplusplus
}
#endif 