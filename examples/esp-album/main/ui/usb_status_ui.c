/*
 * SPDX-FileCopyrightText: 2025 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 */

#include "usb_status_ui.h"
#include "usb_manager.h"
#include "photo_album_constants.h"
#include "bsp/esp-bsp.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

#include "ksdiy_lvgl_port.h"
static const char *TAG = "usb_status_ui";

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

// UI objects
static struct {
    lv_obj_t *main_screen;
    lv_obj_t *content_container;
    lv_obj_t *title_label;
    lv_obj_t *status_label;
    lv_obj_t *info_label;
    lv_obj_t *progress_bar;
    lv_obj_t *progress_label;
    lv_obj_t *previous_screen;  // Save previous screen to restore later
    usb_ui_state_t current_state;
    bool visible;
} s_ui = {
    .main_screen = NULL,
    .previous_screen = NULL,
    .current_state = USB_UI_STATE_HIDDEN,
    .visible = false
};

// Create main screen layout
static void create_status_screen(void)
{
    if (s_ui.main_screen) {
        return;
    }

    UI_LOCK_VOID();

    // Create independent screen instead of child object
    s_ui.main_screen = lv_obj_create(NULL);
    lv_obj_set_size(s_ui.main_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ui.main_screen, lv_color_black(), 0);

    // Create content container (100% width) to keep labels centered
    s_ui.content_container = lv_obj_create(s_ui.main_screen);
    lv_obj_set_size(s_ui.content_container, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(s_ui.content_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_opa(s_ui.content_container, LV_OPA_TRANSP, 0);
    lv_obj_set_flex_flow(s_ui.content_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ui.content_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_ui.content_container, 20, 0); // Vertical spacing

    // Title label - remove emoji and increase font size
    s_ui.title_label = lv_label_create(s_ui.content_container);
    lv_label_set_text(s_ui.title_label, "USB Connected");
    lv_obj_set_style_text_font(s_ui.title_label, &lv_font_montserrat_48, 0);  // Increased from 32 to 48
    lv_obj_set_style_text_color(s_ui.title_label, lv_color_white(), 0);
    // Flex container handles alignment

    // Status label - increase font size
    s_ui.status_label = lv_label_create(s_ui.content_container);
    lv_label_set_text(s_ui.status_label, "Device connected as USB storage");
    lv_obj_set_style_text_font(s_ui.status_label, &lv_font_montserrat_28, 0);  // Increased from 20 to 28
    lv_obj_set_style_text_color(s_ui.status_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_align(s_ui.status_label, LV_TEXT_ALIGN_CENTER, 0);
    // Flex handles align

    // Info label for detailed information - remove emoji and increase font size
    s_ui.info_label = lv_label_create(s_ui.content_container);
    lv_label_set_text(s_ui.info_label, "Safe to transfer files\n\nEject safely when done");
    lv_obj_set_style_text_font(s_ui.info_label, &lv_font_montserrat_24, 0);  // Increased from 18 to 24
    lv_obj_set_style_text_color(s_ui.info_label, lv_color_white(), 0);
    lv_obj_set_style_text_align(s_ui.info_label, LV_TEXT_ALIGN_CENTER, 0);
    // Flex handles

    // Progress bar (hidden by default)
    s_ui.progress_bar = lv_bar_create(s_ui.content_container);
    lv_obj_set_size(s_ui.progress_bar, 400, 24);  // Increased size
    lv_obj_add_flag(s_ui.progress_bar, LV_OBJ_FLAG_HIDDEN);

    // Progress label - increase font size
    s_ui.progress_label = lv_label_create(s_ui.content_container);
    lv_label_set_text(s_ui.progress_label, "");
    lv_obj_set_style_text_font(s_ui.progress_label, &lv_font_montserrat_20, 0);  // Increased from 16 to 20
    lv_obj_set_style_text_color(s_ui.progress_label, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_align(s_ui.progress_label, LV_TEXT_ALIGN_CENTER, 0);
    // Flex handles

    // Initially hidden - don't show until explicitly called
    // No need to hide here since we're not loading it as active screen

    UI_UNLOCK();
}

esp_err_t usb_status_ui_init(void)
{
    ESP_LOGI(TAG, "Initializing USB status UI");
    create_status_screen();
    return ESP_OK;
}

esp_err_t usb_status_ui_deinit(void)
{
    UI_LOCK();
    
    if (s_ui.main_screen) {
        lv_obj_del(s_ui.main_screen);
        s_ui.main_screen = NULL;
    }
    s_ui.visible = false;
    s_ui.current_state = USB_UI_STATE_HIDDEN;
    
    UI_UNLOCK();
    
    ESP_LOGI(TAG, "USB status UI deinitialized");
    return ESP_OK;
}

esp_err_t usb_status_ui_show(usb_ui_state_t state)
{
    if (!s_ui.main_screen) {
        create_status_screen();
    }

    UI_LOCK();

    // Save the current active screen before switching
    s_ui.previous_screen = lv_scr_act();
    
    s_ui.current_state = state;
    s_ui.visible = true;

    // Update content based on state
    switch (state) {
        case USB_UI_STATE_CONNECTED:
            lv_label_set_text(s_ui.title_label, "USB Connected");
            lv_label_set_text(s_ui.status_label, "Device connected as USB storage");
            lv_label_set_text(s_ui.info_label, "Safe to transfer files\n\nEject safely when done");
            lv_obj_add_flag(s_ui.progress_bar, LV_OBJ_FLAG_HIDDEN);
            break;

        case USB_UI_STATE_TRANSFERRING:
            lv_label_set_text(s_ui.title_label, "File Transfer");
            lv_label_set_text(s_ui.status_label, "Scanning new files...");
            lv_label_set_text(s_ui.info_label, "Please wait while files are being processed");
            lv_obj_clear_flag(s_ui.progress_bar, LV_OBJ_FLAG_HIDDEN);
            break;

        case USB_UI_STATE_WIFI_HOTSPOT:
            lv_label_set_text(s_ui.title_label, "WiFi Hotspot");
            lv_label_set_text(s_ui.status_label, "WiFi hotspot active");
            lv_label_set_text(s_ui.info_label, "Connect your device to access files wirelessly");
            lv_obj_add_flag(s_ui.progress_bar, LV_OBJ_FLAG_HIDDEN);
            break;

        case USB_UI_STATE_DUAL_MODE:
            lv_label_set_text(s_ui.title_label, "Dual Access Mode");
            lv_label_set_text(s_ui.status_label, "USB + WiFi active");
            lv_label_set_text(s_ui.info_label, "Access via USB cable or WiFi connection");
            lv_obj_add_flag(s_ui.progress_bar, LV_OBJ_FLAG_HIDDEN);
            break;

        case USB_UI_STATE_ERROR:
            lv_label_set_text(s_ui.title_label, "Connection Error");
            lv_label_set_text(s_ui.status_label, "USB connection error");
            lv_label_set_text(s_ui.info_label, "Please check cable connection and try again");
            lv_obj_add_flag(s_ui.progress_bar, LV_OBJ_FLAG_HIDDEN);
            break;

        default:
            break;
    }

    // Ensure USB status UI is on top of everything
    lv_obj_move_foreground(s_ui.main_screen);
    lv_obj_clear_flag(s_ui.main_screen, LV_OBJ_FLAG_HIDDEN);
    
    // Force the USB UI to be the active layer
    lv_scr_load_anim(s_ui.main_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);

    UI_UNLOCK();

    ESP_LOGI(TAG, "USB status UI shown (state: %d)", state);
    return ESP_OK;
}

esp_err_t usb_status_ui_hide(void)
{
    UI_LOCK();
    
    if (s_ui.main_screen) {
        lv_obj_add_flag(s_ui.main_screen, LV_OBJ_FLAG_HIDDEN);
    }
    
    // Restore the previous screen if it exists
    if (s_ui.previous_screen) {
        lv_scr_load_anim(s_ui.previous_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
        s_ui.previous_screen = NULL;
    }
    
    s_ui.visible = false;
    s_ui.current_state = USB_UI_STATE_HIDDEN;
    
    UI_UNLOCK();
    
    ESP_LOGI(TAG, "USB status UI hidden");
    return ESP_OK;
}

bool usb_status_ui_is_visible(void)
{
    return s_ui.visible;
}

esp_err_t usb_status_ui_update_transfer_progress(const transfer_progress_t *progress)
{
    if (!progress || !s_ui.main_screen) {
        return ESP_ERR_INVALID_ARG;
    }

    UI_LOCK();

    // Update progress bar
    lv_bar_set_value(s_ui.progress_bar, progress->progress_percent, LV_ANIM_ON);

    // Update progress text
    char progress_text[64];
    snprintf(progress_text, sizeof(progress_text), "%d%% - %s", 
             progress->progress_percent, progress->status_text);
    lv_label_set_text(s_ui.progress_label, progress_text);

    UI_UNLOCK();

    return ESP_OK;
}

esp_err_t usb_status_ui_update_storage_info(const storage_info_t *storage)
{
    if (!storage || !s_ui.main_screen) {
        return ESP_ERR_INVALID_ARG;
    }

    UI_LOCK();

    char info_text[128];
    snprintf(info_text, sizeof(info_text), 
             "Storage: %d MB used / %d MB total\nFiles: %d\n\nSafe to eject when done",
             storage->used_mb, storage->total_mb, storage->total_files);
    lv_label_set_text(s_ui.info_label, info_text);

    UI_UNLOCK();

    return ESP_OK;
}

esp_err_t usb_status_ui_update_wifi_info(const wifi_info_t *wifi)
{
    if (!wifi || !s_ui.main_screen) {
        return ESP_ERR_INVALID_ARG;
    }

    UI_LOCK();

    char info_text[128];
    snprintf(info_text, sizeof(info_text), 
             "SSID: %s\nPassword: %s\nIP: %s",
             wifi->ssid, wifi->password, wifi->ip_address);
    lv_label_set_text(s_ui.info_label, info_text);

    UI_UNLOCK();

    return ESP_OK;
}

esp_err_t usb_status_ui_show_error(const char *error_msg)
{
    if (!error_msg || !s_ui.main_screen) {
        return ESP_ERR_INVALID_ARG;
    }

    UI_LOCK();

    lv_label_set_text(s_ui.info_label, error_msg);

    UI_UNLOCK();

    return ESP_OK;
} 