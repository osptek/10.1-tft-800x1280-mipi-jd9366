#pragma once

#include "esp_vfs_fat.h"
#include "driver/sdmmc_host.h"
#include "sd_pwr_ctrl_by_on_chip_ldo.h"

// 用于存储 SDMMC 引脚和卡槽配置的结构体
typedef struct {
    int clk;    // CLK 引脚
    int cmd;    // CMD 引脚
    int d0;     // D0 引脚
    int d1;     // D1 引脚（1位模式下可选）
    int d2;     // D2 引脚（1位模式下可选）
    int d3;     // D3 引脚（1位模式下可选）
    int width;  // 总线宽度（1 或 4）
    int slot;   // 卡槽编号（SDMMC_HOST_SLOT_0 或 SDMMC_HOST_SLOT_1）
} esp_sdmmc_pin_config_t;

// 使用外部引脚和卡槽配置初始化 SDMMC 卡
esp_err_t esp_sdmmc_card_init(const esp_sdmmc_pin_config_t *pin_config);

// 释放 SDMMC 卡资源
esp_err_t esp_sdmmc_card_deinit(void);
