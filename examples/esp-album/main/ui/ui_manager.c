/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "ui_manager.h"
#include "photo_album_constants.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "bsp/esp-bsp.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include "esp_timer.h"

#include "ksdiy_lvgl_port.h"
// Forward declaration for volume auto-hide callback
static void volume_hide_timer_cb(void *arg);
// Forward declaration for time update callback
static void time_update_timer_cb(void *arg);

static const char *TAG = "ui_mgr";

// Helper macros for LVGL locking
#define UI_LOCK() do { \
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) { \
        ESP_LOGE(TAG, "Failed to acquire display lock"); \
        return ESP_ERR_TIMEOUT; \
    } \
} while(0)

#define UI_UNLOCK() ksdiy_lvgl_unlock()

#define UI_LOCK_VOID() do { \
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) { \
        ESP_LOGE(TAG, "Failed to acquire display lock"); \
        return; \
    } \
} while(0)

// Extended UI state with video support and time display
static struct {
    lv_obj_t *main_screen;
    lv_obj_t *img_obj;
    lv_obj_t *loading_spinner;
    lv_obj_t *settings_panel;
    lv_obj_t *time_roller;
    lv_obj_t *progress_label;
    lv_obj_t *video_canvas;
    lv_obj_t *time_label;              // Time display label
    lv_indev_t *touch_indev;
    ui_event_cb_t event_cb;
    void *user_data;
    lv_img_dsc_t current_img_dsc;
    bool settings_visible;
    bool owns_current_data;
    ui_mode_t current_mode;
    // Touch tracking
    bool touch_started;
    lv_point_t touch_start_pos;
    lv_point_t touch_last_pos;
    bool swipe_detected;
    // Volume UI
    lv_obj_t *volume_container;
    lv_obj_t *volume_bar;
    lv_obj_t *volume_label;
    bool volume_visible;
    esp_timer_handle_t volume_timer;
    // Time display
    esp_timer_handle_t time_timer;     // Timer for updating time
    bool time_visible;                 // Time display state
} s_ui;

// Time intervals for settings (in milliseconds)
static const uint32_t time_intervals[] = {
    SLIDESHOW_INTERVAL_2S, SLIDESHOW_INTERVAL_3S, SLIDESHOW_INTERVAL_5S, 
    SLIDESHOW_INTERVAL_10S, SLIDESHOW_INTERVAL_15S, SLIDESHOW_INTERVAL_30S, SLIDESHOW_INTERVAL_60S
};
static const char *time_labels[] = {"2s", "3s", "5s", "10s", "15s", "30s", "60s"};
static const size_t time_count = SLIDESHOW_INTERVALS_COUNT;

static void main_screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_get_act();
    
    if (code == LV_EVENT_PRESSED) {
        if (indev) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            s_ui.touch_start_pos = point;
            s_ui.touch_last_pos = point;
            s_ui.touch_started = true;
            s_ui.swipe_detected = false;
        }
    }
    
    if (code == LV_EVENT_RELEASED && s_ui.touch_started) {
        if (indev) {
            lv_point_t point;
            lv_indev_get_point(indev, &point);
            
            int32_t total_dx = point.x - s_ui.touch_start_pos.x;
            int32_t total_dy = point.y - s_ui.touch_start_pos.y;
            uint32_t distance = (uint32_t)sqrt(total_dx * total_dx + total_dy * total_dy);
            
            if (distance > 30) {
                if (abs(total_dx) > abs(total_dy)) {
                    // Horizontal swipe (for all modes)
                    if (total_dx > 30) {
                        s_ui.swipe_detected = true;
                        if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_SWIPE_RIGHT, s_ui.user_data);
                    } else if (total_dx < -30) {
                        s_ui.swipe_detected = true;
                        if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_SWIPE_LEFT, s_ui.user_data);
                    }
                } else if (s_ui.current_mode == UI_MODE_VIDEO) {
                    // Vertical swipe (only for video mode - volume control)
                    if (total_dy < -30) {
                        s_ui.swipe_detected = true;
                        if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_SWIPE_UP, s_ui.user_data);
                    } else if (total_dy > 30) {
                        s_ui.swipe_detected = true;
                        if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_SWIPE_DOWN, s_ui.user_data);
                    }
                }
            }
        }
        s_ui.touch_started = false;
    }
    
    if (code == LV_EVENT_LONG_PRESSED) {
        if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_LONG_PRESS, s_ui.user_data);
    }
    
    if (code == LV_EVENT_CLICKED) {
        if (s_ui.swipe_detected) {
            s_ui.swipe_detected = false;
            return;
        }
        
        if (s_ui.current_mode == UI_MODE_VIDEO) {
            if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_TAP, s_ui.user_data);
        }
    }
    
    if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(indev);
        if (dir == LV_DIR_LEFT) {
            if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_SWIPE_LEFT, s_ui.user_data);
        } else if (dir == LV_DIR_RIGHT) {
            if (s_ui.event_cb) s_ui.event_cb(UI_EVENT_SWIPE_RIGHT, s_ui.user_data);
        }
    }
}

static void settings_confirm_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        if (s_ui.event_cb) {
            s_ui.event_cb(UI_EVENT_SETTINGS_CLOSE, s_ui.user_data);
        }
    }
}

static void settings_cancel_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        if (s_ui.event_cb) {
            s_ui.event_cb(UI_EVENT_SETTINGS_CANCEL, s_ui.user_data);
        }
    }
}

static void create_main_screen(void)
{
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) {
        ESP_LOGE(TAG, "Failed to acquire display lock");
        return;
    }
    
    s_ui.main_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_ui.main_screen, lv_color_black(), 0);
    lv_obj_clear_flag(s_ui.main_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // Enable gestures on main screen
    lv_obj_add_flag(s_ui.main_screen, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_ui.main_screen, main_screen_event_cb, LV_EVENT_ALL, NULL);
    
    // Create image object with gesture support
    s_ui.img_obj = lv_img_create(s_ui.main_screen);
    lv_obj_center(s_ui.img_obj);
    lv_obj_add_flag(s_ui.img_obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_ui.img_obj, main_screen_event_cb, LV_EVENT_ALL, NULL);
    
    // Create loading spinner
    s_ui.loading_spinner = lv_spinner_create(s_ui.main_screen);
    lv_obj_set_size(s_ui.loading_spinner, LOADING_SPINNER_SIZE, LOADING_SPINNER_SIZE);
    lv_obj_center(s_ui.loading_spinner);
    lv_obj_add_flag(s_ui.loading_spinner, LV_OBJ_FLAG_HIDDEN);
    
    // Create photo counter label
    s_ui.progress_label = lv_label_create(s_ui.main_screen);
    lv_label_set_text(s_ui.progress_label, PROGRESS_FORMAT);
    lv_obj_set_style_text_color(s_ui.progress_label, lv_color_white(), 0);
    lv_obj_align(s_ui.progress_label, LV_ALIGN_BOTTOM_MID, 0, -PROGRESS_LABEL_BOTTOM_OFFSET);
    lv_obj_add_flag(s_ui.progress_label, LV_OBJ_FLAG_HIDDEN);
    
    /* -------- Time label (watermark style) -------- */
    s_ui.time_label = lv_label_create(s_ui.main_screen);
    lv_label_set_text(s_ui.time_label, "00:00:00");

    /* Text: pure white, extra-large font for better readability */
    lv_obj_set_style_text_color(s_ui.time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_ui.time_label, &lv_font_montserrat_48, 0);

    /* Transparent background → true watermark */
    lv_obj_set_style_bg_opa(s_ui.time_label, LV_OPA_TRANSP, 0);

    /* Optional light shadow, improve visibility on bright images */
    lv_obj_set_style_shadow_color(s_ui.time_label, lv_color_black(), 0);
    lv_obj_set_style_shadow_opa(s_ui.time_label, LV_OPA_40, 0);
    lv_obj_set_style_shadow_width(s_ui.time_label, 6, 0);

    /* Move slightly away from the edge to avoid clipping the large font */
    lv_obj_align(s_ui.time_label, LV_ALIGN_TOP_RIGHT, -24, 24);
    lv_obj_add_flag(s_ui.time_label, LV_OBJ_FLAG_HIDDEN);  // Hidden by default
    s_ui.time_visible = false;

    /* ---------------- Volume UI ---------------- */
    s_ui.volume_container = lv_obj_create(s_ui.main_screen);
    lv_obj_set_size(s_ui.volume_container, 80, 200);
    lv_obj_center(s_ui.volume_container);
    lv_obj_set_style_bg_color(s_ui.volume_container, lv_color_hex(0x333333), 0);
    lv_obj_set_style_opa(s_ui.volume_container, LV_OPA_80, 0);
    lv_obj_set_style_radius(s_ui.volume_container, 8, 0);
    lv_obj_set_style_border_opa(s_ui.volume_container, LV_OPA_TRANSP, 0);

    lv_obj_set_flex_flow(s_ui.volume_container, LV_FLEX_FLOW_COLUMN_REVERSE);
    lv_obj_set_flex_align(s_ui.volume_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    s_ui.volume_bar = lv_bar_create(s_ui.volume_container);
    lv_bar_set_range(s_ui.volume_bar, MIN_AUDIO_VOLUME, MAX_AUDIO_VOLUME);
    lv_obj_set_size(s_ui.volume_bar, 20, 120);
    lv_bar_set_value(s_ui.volume_bar, DEFAULT_AUDIO_VOLUME, LV_ANIM_OFF);

    s_ui.volume_label = lv_label_create(s_ui.volume_container);
    lv_label_set_text_fmt(s_ui.volume_label, "%d%%", DEFAULT_AUDIO_VOLUME);
    lv_obj_set_style_text_color(s_ui.volume_label, lv_color_white(), 0);

    lv_obj_add_flag(s_ui.volume_container, LV_OBJ_FLAG_HIDDEN);
    s_ui.volume_visible = false;

    // Create volume hide timer
    esp_timer_create_args_t vol_timer_args = {
        .callback = volume_hide_timer_cb,
        .arg = NULL,
        .name = "vol_hide"
    };
    esp_timer_create(&vol_timer_args, &s_ui.volume_timer);
    
    lv_scr_load(s_ui.main_screen);
    
    // Get touch input device for debugging
    s_ui.touch_indev = bsp_display_get_input_dev();
    if (s_ui.touch_indev) {
        ESP_LOGD(TAG, "Touch device found: %p", s_ui.touch_indev);
    } else {
        ESP_LOGW(TAG, "Touch device not found!");
    }
    
    ksdiy_lvgl_unlock();
}

static void create_settings_panel(void)
{
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) {
        ESP_LOGE(TAG, "Failed to acquire display lock for settings panel creation");
        return;
    }
    
    // Create modal background
    s_ui.settings_panel = lv_obj_create(s_ui.main_screen);
    lv_obj_set_size(s_ui.settings_panel, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ui.settings_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_ui.settings_panel, SETTINGS_BG_OPACITY, 0);
    lv_obj_add_flag(s_ui.settings_panel, LV_OBJ_FLAG_HIDDEN);
    
    // Create settings container with increased size
    lv_obj_t *container = lv_obj_create(s_ui.settings_panel);
    lv_obj_set_size(container, SETTINGS_PANEL_WIDTH, SETTINGS_PANEL_HEIGHT);
    lv_obj_center(container);
    lv_obj_set_style_bg_color(container, lv_color_hex(SETTINGS_PANEL_BG_COLOR), 0);
    lv_obj_set_style_border_color(container, lv_color_white(), 0);
    lv_obj_set_style_border_width(container, SETTINGS_PANEL_BORDER_WIDTH, 0);
    lv_obj_set_style_radius(container, SETTINGS_PANEL_RADIUS, 0);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Title - positioned at top
    lv_obj_t *title = lv_label_create(container);
    lv_label_set_text(title, "Slideshow Settings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_32, 0);  // Larger font
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, SETTINGS_TITLE_TOP_OFFSET);
    
    // Time interval label - positioned below title
    lv_obj_t *time_label = lv_label_create(container);
    lv_label_set_text(time_label, "Time Interval:");
    lv_obj_set_style_text_color(time_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(time_label, &lv_font_montserrat_24, 0);  // Larger font
    lv_obj_align(time_label, LV_ALIGN_TOP_LEFT, SETTINGS_TIME_LABEL_LEFT_OFFSET, SETTINGS_TIME_LABEL_TOP_OFFSET);
    
    // Time roller - positioned with absolute top offset to avoid overlap
    s_ui.time_roller = lv_roller_create(container);
    
    // Build options string from time_labels array
    char options[TIME_OPTIONS_BUFFER_SIZE] = "";
    for (size_t i = 0; i < time_count; i++) {
        if (i > 0) strcat(options, "\n");
        strcat(options, time_labels[i]);
    }
    lv_roller_set_options(s_ui.time_roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(s_ui.time_roller, SETTINGS_ROLLER_VISIBLE_ROWS);
    lv_obj_set_width(s_ui.time_roller, SETTINGS_ROLLER_WIDTH);
    
    // Use absolute positioning to avoid overlap
    lv_obj_align(s_ui.time_roller, LV_ALIGN_TOP_MID, 0, SETTINGS_ROLLER_TOP_OFFSET);
    
    // Enhanced roller styling
    lv_obj_set_style_bg_color(s_ui.time_roller, lv_color_hex(0x2C2C2C), 0);  // Dark gray background
    lv_obj_set_style_border_color(s_ui.time_roller, lv_color_hex(0x007ACC), 0);  // Blue border
    lv_obj_set_style_border_width(s_ui.time_roller, 3, 0);  // Thicker border
    lv_obj_set_style_radius(s_ui.time_roller, 10, 0);  // More rounded corners
    lv_obj_set_style_pad_ver(s_ui.time_roller, 8, 0);  // Vertical padding
    
    // Remove scrollbar completely
    lv_obj_set_style_bg_opa(s_ui.time_roller, LV_OPA_TRANSP, LV_PART_SCROLLBAR);
    lv_obj_set_style_width(s_ui.time_roller, 0, LV_PART_SCROLLBAR);
    
    // Style the selected item (highlighted row)
    lv_obj_set_style_bg_color(s_ui.time_roller, lv_color_hex(0x007ACC), LV_PART_SELECTED);  // Blue highlight
    lv_obj_set_style_bg_opa(s_ui.time_roller, LV_OPA_100, LV_PART_SELECTED);
    lv_obj_set_style_text_color(s_ui.time_roller, lv_color_white(), LV_PART_SELECTED);
    lv_obj_set_style_text_font(s_ui.time_roller, &lv_font_montserrat_24, LV_PART_SELECTED);  // Larger font
    lv_obj_set_style_radius(s_ui.time_roller, 6, LV_PART_SELECTED);
    
    // Style the normal items
    lv_obj_set_style_text_color(s_ui.time_roller, lv_color_hex(0xDDDDDD), LV_PART_MAIN);  // Light text
    lv_obj_set_style_text_font(s_ui.time_roller, &lv_font_montserrat_20, LV_PART_MAIN);  // Larger font
    lv_obj_set_style_text_align(s_ui.time_roller, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    
    // Create button container - positioned at bottom with more space
    lv_obj_t *btn_container = lv_obj_create(container);
    lv_obj_set_size(btn_container, LV_PCT(85), 70);  // Increased height
    lv_obj_align(btn_container, LV_ALIGN_BOTTOM_MID, 0, -SETTINGS_CLOSE_LABEL_BOTTOM_OFFSET);
    lv_obj_set_style_bg_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(btn_container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(btn_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(btn_container, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(btn_container, LV_OBJ_FLAG_SCROLLABLE);
    
    // Confirm button - enhanced styling
    lv_obj_t *confirm_btn = lv_btn_create(btn_container);
    lv_obj_set_size(confirm_btn, 140, 55);  // Larger button
    lv_obj_set_style_bg_color(confirm_btn, lv_color_hex(0x28A745), 0);  // Better green
    lv_obj_set_style_radius(confirm_btn, 8, 0);
    lv_obj_set_style_shadow_width(confirm_btn, 4, 0);
    lv_obj_set_style_shadow_opa(confirm_btn, LV_OPA_30, 0);
    lv_obj_add_event_cb(confirm_btn, settings_confirm_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "OK");
    lv_obj_set_style_text_color(confirm_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(confirm_label, &lv_font_montserrat_20, 0);  // Larger font
    lv_obj_center(confirm_label);
    
    // Cancel button - enhanced styling
    lv_obj_t *cancel_btn = lv_btn_create(btn_container);
    lv_obj_set_size(cancel_btn, 140, 55);  // Larger button
    lv_obj_set_style_bg_color(cancel_btn, lv_color_hex(0xDC3545), 0);  // Better red
    lv_obj_set_style_radius(cancel_btn, 8, 0);
    lv_obj_set_style_shadow_width(cancel_btn, 4, 0);
    lv_obj_set_style_shadow_opa(cancel_btn, LV_OPA_30, 0);
    lv_obj_add_event_cb(cancel_btn, settings_cancel_event_cb, LV_EVENT_CLICKED, NULL);
    
    lv_obj_t *cancel_label = lv_label_create(cancel_btn);
    lv_label_set_text(cancel_label, "Cancel");
    lv_obj_set_style_text_color(cancel_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(cancel_label, &lv_font_montserrat_20, 0);  // Larger font
    lv_obj_center(cancel_label);
    
    ksdiy_lvgl_unlock();
}

static void volume_hide_timer_cb(void *arg)
{
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) return;
    lv_obj_add_flag(s_ui.volume_container, LV_OBJ_FLAG_HIDDEN);
    s_ui.volume_visible = false;
    ksdiy_lvgl_unlock();
}

// Time update timer callback
static void time_update_timer_cb(void *arg)
{
    if (!s_ui.time_visible || !s_ui.time_label) {
        return;
    }
    
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) {
        return;
    }
    
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    // Format time as HH:MM:SS
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    
    lv_label_set_text(s_ui.time_label, time_str);
    
    ksdiy_lvgl_unlock();
}

esp_err_t ui_manager_init(ui_event_cb_t event_cb, void *user_data)
{
    if (!event_cb) {
        return ESP_ERR_INVALID_ARG;
    }
    
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.event_cb = event_cb;
    s_ui.user_data = user_data;
    
    create_main_screen();
    create_settings_panel();
    
    ESP_LOGI(TAG, "UI manager initialized");
    return ESP_OK;
}

esp_err_t ui_manager_deinit(void)
{
    // Note: s_ui.current_img_dsc.data now points to external data,
    // so we don't free it here - it's managed by the photo album
    s_ui.current_img_dsc.data = NULL;
    
    if (s_ui.volume_timer) {
        esp_timer_stop(s_ui.volume_timer);
        esp_timer_delete(s_ui.volume_timer);
        s_ui.volume_timer = NULL;
    }
    
    ESP_LOGI(TAG, "UI manager deinitialized");
    return ESP_OK;
}

esp_err_t ui_manager_display_image(const decoded_image_t *image)
{
    if (!image || !image->rgb_data || !image->is_valid) {
        ESP_LOGE(TAG, "Invalid image parameter");
        return ESP_ERR_INVALID_ARG;
    }
    
    ui_manager_switch_mode(UI_MODE_IMAGE);
    
    if (s_ui.current_img_dsc.data && s_ui.owns_current_data) {
        // Try to reuse existing buffer to avoid extra malloc
        void *new_ptr = heap_caps_realloc((void*)s_ui.current_img_dsc.data, image->data_size,
                                          MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!new_ptr) {
            free((void*)s_ui.current_img_dsc.data);
            new_ptr = heap_caps_malloc(image->data_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        }
        s_ui.current_img_dsc.data = new_ptr;
        s_ui.owns_current_data = true;
    }

    s_ui.current_img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;
    s_ui.current_img_dsc.header.w = image->width;
    s_ui.current_img_dsc.header.h = image->height;
    s_ui.current_img_dsc.data_size = image->data_size;

    if (!s_ui.current_img_dsc.data) {
        s_ui.current_img_dsc.data = heap_caps_malloc(image->data_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (!s_ui.current_img_dsc.data) {
            ESP_LOGE(TAG, "Failed to allocate UI image buffer");
            return ESP_ERR_NO_MEM;
        }
        s_ui.owns_current_data = true;
    }

    memcpy((void*)s_ui.current_img_dsc.data, image->rgb_data, image->data_size);
    
    UI_LOCK();
    
    lv_img_set_src(s_ui.img_obj, &s_ui.current_img_dsc);
    lv_obj_center(s_ui.img_obj);
    lv_obj_add_flag(s_ui.loading_spinner, LV_OBJ_FLAG_HIDDEN);
    
    UI_UNLOCK();
    
    return ESP_OK;
}

esp_err_t ui_manager_show_loading(void)
{
    UI_LOCK();
    
    if (s_ui.current_mode == UI_MODE_VIDEO && s_ui.video_canvas) {
        lv_obj_add_flag(s_ui.video_canvas, LV_OBJ_FLAG_HIDDEN);
    }
    
    lv_obj_clear_flag(s_ui.loading_spinner, LV_OBJ_FLAG_HIDDEN);
    
    UI_UNLOCK();
    return ESP_OK;
}

esp_err_t ui_manager_hide_loading(void)
{
    UI_LOCK();
    lv_obj_add_flag(s_ui.loading_spinner, LV_OBJ_FLAG_HIDDEN);
    UI_UNLOCK();
    return ESP_OK;
}

esp_err_t ui_manager_show_settings(uint32_t current_interval)
{
    if (s_ui.settings_visible) {
        return ESP_OK;
    }
    
    UI_LOCK();
    
    for (size_t i = 0; i < time_count; i++) {
        if (time_intervals[i] == current_interval) {
            lv_roller_set_selected(s_ui.time_roller, i, LV_ANIM_OFF);
            break;
        }
    }
    
    lv_obj_move_foreground(s_ui.settings_panel);
    lv_obj_clear_flag(s_ui.settings_panel, LV_OBJ_FLAG_HIDDEN);
    
    UI_UNLOCK();
    
    s_ui.settings_visible = true;
    
    return ESP_OK;
}

esp_err_t ui_manager_hide_settings(void)
{
    if (!s_ui.settings_visible) {
        return ESP_OK;
    }
    
    UI_LOCK();
    lv_obj_add_flag(s_ui.settings_panel, LV_OBJ_FLAG_HIDDEN);
    UI_UNLOCK();
    
    s_ui.settings_visible = false;
    
    return ESP_OK;
}

esp_err_t ui_manager_update_progress(int current, int total)
{
    UI_LOCK();
    
    if (total <= 0) {
        lv_obj_add_flag(s_ui.progress_label, LV_OBJ_FLAG_HIDDEN);
        UI_UNLOCK();
        return ESP_OK;
    }
    
    // Update photo counter label
    char text[32];
    snprintf(text, sizeof(text), PROGRESS_FORMAT, current + PROGRESS_INDEX_OFFSET, total);
    lv_label_set_text(s_ui.progress_label, text);
    
    // Show counter
    lv_obj_clear_flag(s_ui.progress_label, LV_OBJ_FLAG_HIDDEN);
    
    UI_UNLOCK();
    
    return ESP_OK;
}

uint32_t ui_manager_get_selected_interval(void)
{
    if (!s_ui.time_roller) {
        return DEFAULT_SLIDESHOW_MS;
    }
    
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) {
        ESP_LOGE(TAG, "Failed to acquire display lock");
        return DEFAULT_SLIDESHOW_MS;
    }
    
    uint16_t selected = lv_roller_get_selected(s_ui.time_roller);
    
    ksdiy_lvgl_unlock();
    
    if (selected < time_count) {
        return time_intervals[selected]; // Already in milliseconds
    }
    
    return DEFAULT_SLIDESHOW_MS;
}

esp_err_t ui_manager_switch_mode(ui_mode_t mode)
{
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    
    s_ui.current_mode = mode;
    
    switch (mode) {
        case UI_MODE_IMAGE:
            lv_obj_clear_flag(s_ui.img_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_ui.progress_label, LV_OBJ_FLAG_HIDDEN);
            if (s_ui.video_canvas) {
                lv_obj_add_flag(s_ui.video_canvas, LV_OBJ_FLAG_HIDDEN);
            }
            break;
            
        case UI_MODE_VIDEO:
            lv_obj_add_flag(s_ui.img_obj, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_ui.progress_label, LV_OBJ_FLAG_HIDDEN);
            break;
    }
    
    ksdiy_lvgl_unlock();
    return ESP_OK;
}

esp_err_t ui_manager_display_video_frame(const uint8_t *frame_buffer, uint32_t width, uint32_t height)
{
    if (!frame_buffer) return ESP_ERR_INVALID_ARG;
    
    if (!ksdiy_lvgl_lock(UI_DISPLAY_LOCK_TIMEOUT)) {
        return ESP_ERR_TIMEOUT;
    }
    
    if (!s_ui.video_canvas) {
        s_ui.video_canvas = lv_canvas_create(s_ui.main_screen);
        lv_obj_center(s_ui.video_canvas);
        
        // Ensure video_canvas does not block touch event propagation
        lv_obj_add_flag(s_ui.video_canvas, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(s_ui.video_canvas, LV_OBJ_FLAG_CLICKABLE);
    }
    
    // Zero-copy: directly set buffer pointer
    lv_canvas_set_buffer(s_ui.video_canvas, (void*)frame_buffer,
                        width, height, LV_COLOR_FORMAT_RGB565);
    
    lv_obj_set_size(s_ui.video_canvas, width, height);
    lv_obj_center(s_ui.video_canvas);
    lv_obj_clear_flag(s_ui.video_canvas, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_ui.loading_spinner, LV_OBJ_FLAG_HIDDEN);
    
    ksdiy_lvgl_unlock();
    return ESP_OK;
}

// -------------------- Volume Display --------------------

esp_err_t ui_manager_show_volume(int volume_percent)
{
    if (volume_percent < MIN_AUDIO_VOLUME) volume_percent = MIN_AUDIO_VOLUME;
    if (volume_percent > MAX_AUDIO_VOLUME) volume_percent = MAX_AUDIO_VOLUME;

    UI_LOCK();

    lv_bar_set_value(s_ui.volume_bar, volume_percent, LV_ANIM_OFF);
    lv_label_set_text_fmt(s_ui.volume_label, "%d%%", volume_percent);

    if (!s_ui.volume_visible) {
        lv_obj_clear_flag(s_ui.volume_container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_ui.volume_container);
        s_ui.volume_visible = true;
    }

    // Restart auto-hide timer (hide after 2 seconds)
    if (s_ui.volume_timer) {
        esp_timer_stop(s_ui.volume_timer);
        esp_timer_start_once(s_ui.volume_timer, 2000 * 1000ULL);
    }

    UI_UNLOCK();
    return ESP_OK;
}

// -------------------- Time Display --------------------

esp_err_t ui_manager_show_time(void)
{
    if (s_ui.time_visible) {
        return ESP_OK;  // Already visible
    }
    
    UI_LOCK();
    
    // Show time label
    lv_obj_clear_flag(s_ui.time_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_ui.time_label);
    s_ui.time_visible = true;
    
    // Update time immediately
    time_t now;
    struct tm timeinfo;
    time(&now);
    localtime_r(&now, &timeinfo);
    
    char time_str[16];
    strftime(time_str, sizeof(time_str), "%H:%M:%S", &timeinfo);
    lv_label_set_text(s_ui.time_label, time_str);
    
    UI_UNLOCK();
    
    // Create and start update timer if not exists
    if (!s_ui.time_timer) {
        esp_timer_create_args_t timer_args = {
            .callback = time_update_timer_cb,
            .arg = NULL,
            .name = "time_update"
        };
        esp_err_t ret = esp_timer_create(&timer_args, &s_ui.time_timer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to create time timer: %s", esp_err_to_name(ret));
            return ret;
        }
    }
    
    // Start periodic timer (update every second)
    esp_err_t ret = esp_timer_start_periodic(s_ui.time_timer, 1000 * 1000ULL);  // 1 second in microseconds
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start time timer: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ESP_LOGI(TAG, "Time display enabled");
    return ESP_OK;
}

esp_err_t ui_manager_hide_time(void)
{
    if (!s_ui.time_visible) {
        return ESP_OK;  // Already hidden
    }
    
    UI_LOCK();
    
    // Hide time label
    lv_obj_add_flag(s_ui.time_label, LV_OBJ_FLAG_HIDDEN);
    s_ui.time_visible = false;
    
    UI_UNLOCK();
    
    // Stop timer
    if (s_ui.time_timer) {
        esp_timer_stop(s_ui.time_timer);
    }
    
    ESP_LOGI(TAG, "Time display disabled");
    return ESP_OK;
} 