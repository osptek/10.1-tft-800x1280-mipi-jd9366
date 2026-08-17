/*
 * SPDX-FileCopyrightText: Copyright 2026 OSPTEK
 * SPDX-License-Identifier: CC-BY-4.0
 *
 * https://github.com/osptek
 */

#include "lvgl_init.h"
#include "esp_sdmmc_card.h"
#include "esp_mjpeg_decode.h"

#define ROOT "/sdcard"
#define MJPEG_FILENAME ROOT "/mjpeg_800_1280_30fps.mjpeg"
#define TARGET_FPS 60
#define FRAME_TIME_MS (1000 / TARGET_FPS)

// 变量
static int total_frames = 0;              // 总帧数
static uint64_t total_read_video = 0;     // 读取视频总时间
static uint64_t total_decode_video = 0;   // 解码视频总时间
static uint64_t total_show_video = 0;     // 显示视频总时间
static uint64_t start_ms, curr_ms;        // 时间戳
static esp_mjpeg_decode_t mjpeg = {0};    // MJPEG 解码器实例
static size_t mjpeg_buffer_size = 800 * 1280 * 2 / 5; // 输入缓冲区大小
static size_t output_buffer_size = 800 * 1280 * 2;     // 输出缓冲区大小
static lv_obj_t *video_img = NULL;        // LVGL 图像对象
static lv_image_dsc_t img_dsc;            // 图像描述符（用于 raw 缓冲区）

void app_main(void) {
    start_ms = esp_timer_get_time() / 1000; // 记录开始时间（毫秒）

    // 初始化 LCD
    ESP_ERROR_CHECK(app_lcd_init());

    // 初始化 LVGL
    ESP_ERROR_CHECK(app_lvgl_init());

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
        ESP_LOGE(TAG, "esp_mjpeg_decode_setup 失败");
        esp_sdmmc_card_deinit();
        return;
    }

    // 创建 LVGL 屏幕和图像对象（只创建一次）
    lv_obj_t *screen = lv_screen_active();
    video_img = lv_image_create(screen);
    lv_obj_center(video_img);  // 居中显示
    lv_obj_set_size(video_img, EXAMPLE_LCD_H_RES, EXAMPLE_LCD_V_RES);  // 设置为全屏

    // 初始化图像描述符（header 固定，data 在循环中更新）
    memset(&img_dsc, 0, sizeof(img_dsc));
    img_dsc.header.cf = LV_COLOR_FORMAT_RGB565;  // RGB565 格式

    while (esp_mjpeg_decode_read_mjpeg_buf(&mjpeg)) {
        uint64_t frame_start_ms = esp_timer_get_time() / 1000;  // 帧开始时间

        // 读取时间
        curr_ms = esp_timer_get_time() / 1000;
        total_read_video += curr_ms - start_ms;
        uint64_t prev = curr_ms;

        // 解码到输出缓冲区
        if (esp_mjpeg_decode_jpg(&mjpeg) != ESP_OK) continue;  // 解码失败则跳过
        curr_ms = esp_timer_get_time() / 1000;
        total_decode_video += curr_ms - prev;
        prev = curr_ms;

        // LVGL 线程安全锁
        lvgl_port_lock(0);

        // 更新 LVGL 图像源（使用解码缓冲区）
        img_dsc.header.w = esp_mjpeg_decode_get_width(&mjpeg);
        img_dsc.header.h = esp_mjpeg_decode_get_height(&mjpeg);
        img_dsc.data_size = img_dsc.header.w * img_dsc.header.h * (LV_COLOR_DEPTH / 8);  // RGB565: 2 字节/像素
        img_dsc.data = (uint8_t *)mjpeg.output_buf;  // 指向解码器输出缓冲区
        lv_image_set_src(video_img, &img_dsc);

        // 刷新 LVGL 显示（触发 flush）
        lv_refr_now(lvgl_disp);  // 立即刷新（适合视频）

        // 释放 LVGL 锁
        lvgl_port_unlock();

        curr_ms = esp_timer_get_time() / 1000;
        total_show_video += curr_ms - prev;

        total_frames++;

        // 控制帧率
        uint64_t frame_end_ms = esp_timer_get_time() / 1000;
        int64_t sleep_ms = FRAME_TIME_MS - (frame_end_ms - frame_start_ms);
        if (sleep_ms > 0) {
            vTaskDelay(pdMS_TO_TICKS(sleep_ms));  // 延迟以维持目标帧率
        }
    }

    // 性能报告
    uint64_t time_used = esp_timer_get_time() / 1000 - start_ms;
    float fps = 1000.0f * total_frames / time_used;
    ESP_LOGI(TAG, "ESP32-P4 MJPEG 解码器与 LVGL");
    ESP_LOGI(TAG, "帧尺寸：%d x %d", esp_mjpeg_decode_get_width(&mjpeg), esp_mjpeg_decode_get_height(&mjpeg));
    ESP_LOGI(TAG, "总帧数：%d", total_frames);
    ESP_LOGI(TAG, "使用时间：%llu 毫秒", time_used);
    ESP_LOGI(TAG, "平均 FPS：%.1f", fps);
    ESP_LOGI(TAG, "读取 MJPEG：%llu 毫秒 (%.1f %%)", total_read_video, 100.0f * total_read_video / time_used);
    ESP_LOGI(TAG, "解码视频：%llu 毫秒 (%.1f %%)", total_decode_video, 100.0f * total_decode_video / time_used);
    ESP_LOGI(TAG, "显示视频：%llu 毫秒 (%.1f %%)", total_show_video, 100.0f * total_show_video / time_used);

    // 清理
    esp_mjpeg_decode_close(&mjpeg);

    // 删除 LVGL 图像对象
    if (video_img) {
        lv_obj_delete(video_img);
        video_img = NULL;
    }

    esp_sdmmc_card_deinit();
    ESP_LOGI(TAG, "MJPEG 结束");
}
