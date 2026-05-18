/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include "esp_codec_dev.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    VIDEO_STATE_STOPPED,
    VIDEO_STATE_PLAYING,
    VIDEO_STATE_PAUSED,
    VIDEO_STATE_ERROR
} video_state_t;

esp_err_t video_player_init(esp_codec_dev_handle_t audio_dev);
esp_err_t video_player_play(const char *mp4_file);
esp_err_t video_player_pause(void);
esp_err_t video_player_resume(void);
esp_err_t video_player_stop(void);
esp_err_t video_player_deinit(void);

// Volume control functions
esp_err_t video_player_set_volume(int volume);
int video_player_get_volume(void);

video_state_t video_player_get_state(void);
bool video_player_is_finished(void);
bool video_player_has_error(void);
esp_err_t video_player_restart_current(void);
esp_err_t video_player_switch_file(const char *mp4_file);

#ifdef __cplusplus
}
#endif 