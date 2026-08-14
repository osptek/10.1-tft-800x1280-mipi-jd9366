#include "esp_sdmmc_card.h"
#include "esp_log.h"

static const char *TAG = "SDMMC";
static sdmmc_card_t *card = NULL;
static sd_pwr_ctrl_handle_t pwr_ctrl_handle = NULL;

esp_err_t esp_sdmmc_card_init(const esp_sdmmc_pin_config_t *pin_config) {
    if (!pin_config) {
        ESP_LOGE(TAG, "引脚配置为空");
        return ESP_ERR_INVALID_ARG;
    }

    if (pin_config->slot != SDMMC_HOST_SLOT_0 && pin_config->slot != SDMMC_HOST_SLOT_1) {
        ESP_LOGE(TAG, "无效的卡槽编号: %d", pin_config->slot);
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret;
    // SDMMC 配置
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.slot = pin_config->slot; // 使用外部提供的卡槽编号
    host.max_freq_khz = SDMMC_FREQ_HIGHSPEED;

    sd_pwr_ctrl_ldo_config_t ldo_config = {
        .ldo_chan_id = 4, // 使用 `LDO_VO4` 作为 SDMMC IO 电源
    };

    ret = sd_pwr_ctrl_new_on_chip_ldo(&ldo_config, &pwr_ctrl_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法创建片上 LDO 电源控制驱动");
        return ret;
    }
    host.pwr_ctrl_handle = pwr_ctrl_handle;

    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = pin_config->width; // 使用提供的总线宽度
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    slot_config.clk = pin_config->clk; // 分配 CLK 引脚
    slot_config.cmd = pin_config->cmd; // 分配 CMD 引脚
    slot_config.d0 = pin_config->d0;  // 分配 D0 引脚
    if (pin_config->width == 4) {
        slot_config.d1 = pin_config->d1; // 为 4 位模式分配 D1 引脚
        slot_config.d2 = pin_config->d2; // 为 4 位模式分配 D2 引脚
        slot_config.d3 = pin_config->d3; // 为 4 位模式分配 D3 引脚
    }

    // FatFs 挂载配置
    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // 不自动格式化
        .max_files = 5,
        .allocation_unit_size = 16 * 1024
    };

    ret = esp_vfs_fat_sdmmc_mount("/sdcard", &host, &slot_config, &mount_config, &card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法挂载 SD 卡: %s", esp_err_to_name(ret));
        sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
        pwr_ctrl_handle = NULL;
        return ret;
    }
    ESP_LOGI(TAG, "SD 卡已挂载");
    return ESP_OK;
}

esp_err_t esp_sdmmc_card_deinit(void) {
    if (!card) {
        ESP_LOGW(TAG, "SD 卡未挂载");
        return ESP_OK; // 无需释放
    }

    esp_err_t ret = esp_vfs_fat_sdcard_unmount("/sdcard", card);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "无法卸载 SD 卡: %s", esp_err_to_name(ret));
        return ret;
    }

    // 清理电源控制句柄
    if (pwr_ctrl_handle) {
        ret = sd_pwr_ctrl_del_on_chip_ldo(pwr_ctrl_handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "无法删除片上 LDO 电源控制驱动: %s", esp_err_to_name(ret));
            return ret;
        }
        pwr_ctrl_handle = NULL;
    }

    card = NULL;
    ESP_LOGI(TAG, "SD 卡已卸载并释放资源");
    return ESP_OK;
}
