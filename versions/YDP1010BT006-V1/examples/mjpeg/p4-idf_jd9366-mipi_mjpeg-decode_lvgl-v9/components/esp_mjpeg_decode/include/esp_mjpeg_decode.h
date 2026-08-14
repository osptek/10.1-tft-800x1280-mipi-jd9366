#pragma once

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/jpeg_decode.h"

#define READ_BATCH_SIZE 1024  // 每次读取的字节数

typedef struct {
    FILE *input;                // 输入文件句柄
    uint8_t *mjpeg_buf;         // JPEG 输入缓冲区
    uint16_t *output_buf;       // RGB565 输出缓冲区
    jpeg_decoder_handle_t decoder_engine;  // JPEG 解码器句柄
    int16_t w, h;               // 帧宽高
    uint8_t *p;                 // 当前读取指针
    int32_t read;               // 已读取字节数
    size_t mjpeg_buffer_size;   // JPEG 输入缓冲区大小
    size_t output_buffer_size;  // RGB565 输出缓冲区大小
} esp_mjpeg_decode_t;

// 初始化 MJPEG 解码器，传入缓冲区大小
esp_err_t esp_mjpeg_decode_setup(esp_mjpeg_decode_t *mj, const char *path, size_t mjpeg_buffer_size, size_t output_buffer_size);

// 读取 MJPEG 帧
bool esp_mjpeg_decode_read_mjpeg_buf(esp_mjpeg_decode_t *mj);

// 解码 JPEG 帧
esp_err_t esp_mjpeg_decode_jpg(esp_mjpeg_decode_t *mj);

// 获取帧宽度
int16_t esp_mjpeg_decode_get_width(esp_mjpeg_decode_t *mj);

// 获取帧高度
int16_t esp_mjpeg_decode_get_height(esp_mjpeg_decode_t *mj);

// 获取输出缓冲区
uint16_t *esp_mjpeg_decode_get_out_buf(esp_mjpeg_decode_t *mj);

// 关闭 MJPEG 文件
void esp_mjpeg_decode_close(esp_mjpeg_decode_t *mj);