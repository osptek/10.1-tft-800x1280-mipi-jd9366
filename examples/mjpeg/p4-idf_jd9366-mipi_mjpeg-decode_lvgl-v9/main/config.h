#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "sdkconfig.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"

#include "esp_lcd_jd9366.h"

#include "lvgl.h"
#include "esp_lvgl_port.h"

static const char *TAG = "example";

/* LCD size */
#define EXAMPLE_LCD_H_RES   (800)
#define EXAMPLE_LCD_V_RES   (1280)

#if LV_COLOR_DEPTH == 16
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB565)
#define BSP_LCD_COLOR_DEPTH (16)
#define LV_COLOR_FORMAT LV_COLOR_FORMAT_RGB565
#elif LV_COLOR_DEPTH == 24
#define MIPI_DPI_PX_FORMAT (LCD_COLOR_PIXEL_FORMAT_RGB888)
#define BSP_LCD_COLOR_DEPTH (24)
#define LV_COLOR_FORMAT LV_COLOR_FORMAT_RGB888
#endif

// “VDD_MIPI_DPHY”应供电 2.5V，可从内部 LDO 稳压器或外部 LDO 芯片获取电源
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN 3 // LDO_VO3 连接至 VDD_MIPI_DPHY
#define EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV 2500
#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL 1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_BK_LIGHT -1
#define EXAMPLE_PIN_NUM_LCD_RST  -1

/* LCD IO and panel */
static esp_lcd_panel_handle_t panel_handle = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;

// 定义 SDMMC 引脚
#define SDMMC_D0             (GPIO_NUM_39)  // 数据0引脚
#define SDMMC_D1             (GPIO_NUM_40)  // 数据1引脚
#define SDMMC_D2             (GPIO_NUM_41)  // 数据2引脚
#define SDMMC_D3             (GPIO_NUM_42)  // 数据3引脚
#define SDMMC_CMD            (GPIO_NUM_44)  // 命令引脚
#define SDMMC_CLK            (GPIO_NUM_43)  // 时钟引脚
#define SDMMC_WIDTH          4              // 总线宽度（1 或 4）
#define SDMMC_SLOT           (SDMMC_HOST_SLOT_1) // 卡槽编号（SDMMC_HOST_SLOT_0 或 SDMMC_HOST_SLOT_1）