/*
 * SPDX-FileCopyrightText: Copyright 2026 OSPTEK
 * SPDX-License-Identifier: CC-BY-4.0
 *
 * https://github.com/osptek
 */

#include "lcd_init.h"
#include "esp_sdmmc_card.h"
#include "esp_mjpeg_decode.h"

#define ROOT "/sdcard"
#define MJPEG_FILENAME ROOT "/mjpeg_800_1280_30fps.mjpeg"

// 变量
static int total_frames = 0;
static uint64_t total_read_video = 0;
static uint64_t total_decode_video = 0;
static uint64_t total_show_video = 0;
static uint64_t start_ms, curr_ms;
static int16_t x = -1, y = -1, w = -1, h = -1;
static esp_mjpeg_decode_t mjpeg = {0};
static size_t mjpeg_buffer_size = 800 * 1280 * 2 / 5; // 输入缓冲区大小
static size_t output_buffer_size = 800 * 1280 * 2;     // 输出缓冲区大小
static int lcd_width = 800, lcd_height = 1280;  // 假设 LCD 尺寸

void app_main(void) {
    start_ms = esp_timer_get_time() / 1000;
    
    // 初始化 LCD
    ESP_ERROR_CHECK(app_lcd_init());

    // 初始化 SDMMC
    esp_sdmmc_pin_config_t pin_config = {
        .clk = SDMMC_CLK,    // 时钟引脚
        .cmd = SDMMC_CMD,    // 命令引脚
        .d0 = SDMMC_D0,      // 数据0引脚
        .d1 = SDMMC_D1,      // 数据1引脚
        .d2 = SDMMC_D2,      // 数据2引脚
        .d3 = SDMMC_D3,      // 数据3引脚
        .width = SDMMC_WIDTH, // 使用宏定义的总线宽度
        .slot = SDMMC_SLOT   // 使用宏定义的卡槽编号
    };
    esp_sdmmc_card_init(&pin_config);

    // 初始化 MJPEG 解码器
    if (esp_mjpeg_decode_setup(&mjpeg, MJPEG_FILENAME, mjpeg_buffer_size, output_buffer_size) != ESP_OK) {
        ESP_LOGE(TAG, "esp_mjpeg_decode_setup failed");
        esp_sdmmc_card_deinit();
        return;
    }

    while (esp_mjpeg_decode_read_mjpeg_buf(&mjpeg)) {
        // 读取时间
        curr_ms = esp_timer_get_time() / 1000;
        total_read_video += curr_ms - start_ms;
        uint64_t prev = curr_ms;

        // 解码
        if (esp_mjpeg_decode_jpg(&mjpeg) != ESP_OK) continue;
        curr_ms = esp_timer_get_time() / 1000;
        total_decode_video += curr_ms - prev;
        prev = curr_ms;

        // 设置显示坐标
        if (x == -1) {
            w = esp_mjpeg_decode_get_width(&mjpeg);
            h = esp_mjpeg_decode_get_height(&mjpeg);
            x = (w > lcd_width) ? 0 : ((lcd_width - w) / 2);
            y = (h > lcd_height) ? 0 : ((lcd_height - h) / 2);
        }

        // 显示
        draw16bitbergbbitmap(x, y, w, h, esp_mjpeg_decode_get_out_buf(&mjpeg));
        curr_ms = esp_timer_get_time() / 1000;
        total_show_video += curr_ms - prev;

        total_frames++;
    }

    // 性能报告
    uint64_t time_used = esp_timer_get_time() / 1000 - start_ms;
    float fps = 1000.0f * total_frames / time_used;
    ESP_LOGI(TAG, "ESP32-P4 MJPEG decoder");
    ESP_LOGI(TAG, "Frame size: %d x %d", esp_mjpeg_decode_get_width(&mjpeg), esp_mjpeg_decode_get_height(&mjpeg));
    ESP_LOGI(TAG, "Total frames: %d", total_frames);
    ESP_LOGI(TAG, "Time used: %llu ms", time_used);
    ESP_LOGI(TAG, "Average FPS: %.1f", fps);
    ESP_LOGI(TAG, "Read MJPEG: %llu ms (%.1f %%)", total_read_video, 100.0f * total_read_video / time_used);
    ESP_LOGI(TAG, "Decode video: %llu ms (%.1f %%)", total_decode_video, 100.0f * total_decode_video / time_used);
    ESP_LOGI(TAG, "Show video: %llu ms (%.1f %%)", total_show_video, 100.0f * total_show_video / time_used);

    // 清理
    esp_mjpeg_decode_close(&mjpeg);
    esp_lcd_panel_disp_on_off(panel_handle, false);
    esp_sdmmc_card_deinit();
    ESP_LOGI(TAG, "MJPEG end");
}