#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2c_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_mipi_dsi.h"
#include "driver/gpio.h"
#include "esp_ldo_regulator.h"
#include "esp_lvgl_port.h"
#include "lv_demos.h"

#include "esp_lcd_jd9366.h"
#include "esp_lcd_touch_jd9366.h"

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

/* Touch settings */
#define EXAMPLE_TOUCH_I2C_NUM       (0)
#define EXAMPLE_TOUCH_I2C_CLK_HZ    (400000)

/* LCD touch pins */
#define EXAMPLE_TOUCH_I2C_SCL       (GPIO_NUM_8)
#define EXAMPLE_TOUCH_I2C_SDA       (GPIO_NUM_7)
#define EXAMPLE_TOUCH_RST           (GPIO_NUM_NC)
#define EXAMPLE_TOUCH_INT           (GPIO_NUM_NC)

static const char *TAG = "EXAMPLE";

/* LCD IO and panel */
static esp_lcd_panel_handle_t lcd_panel = NULL;
static esp_lcd_panel_io_handle_t io_handle = NULL;
static esp_lcd_touch_handle_t touch_handle = NULL;

/* LVGL display and touch */
static lv_display_t *lvgl_disp = NULL;
static lv_indev_t *lvgl_touch_indev = NULL;

static void example_bsp_enable_dsi_phy_power(void)
{
    // 打开 MIPI DSI PHY 的电源，使其从“无电”状态进入“关机”状态
    esp_ldo_channel_handle_t ldo_mipi_phy = NULL;
#ifdef EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN
    esp_ldo_channel_config_t ldo_mipi_phy_config = {
        .chan_id = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_CHAN,
        .voltage_mv = EXAMPLE_MIPI_DSI_PHY_PWR_LDO_VOLTAGE_MV,
    };
    ESP_ERROR_CHECK(esp_ldo_acquire_channel(&ldo_mipi_phy_config, &ldo_mipi_phy));
    ESP_LOGI(TAG, "MIPI DSI PHY Powered on");
#endif
}

static void example_bsp_init_lcd_backlight(void)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT};
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif
}

static void example_bsp_set_lcd_backlight(uint32_t level)
{
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, level);
#endif
}

static const jd9366_lcd_init_cmd_t lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
        // Page 1 commands
        {0x30, (uint8_t []){0x01}, 1, 0},
        {0x78, (uint8_t []){0x49, 0x61, 0x02, 0x00}, 4, 0},
        
        // Page 2 commands  
        {0x30, (uint8_t []){0x02}, 1, 0},
        {0x31, (uint8_t []){0x12}, 1, 0},
        {0x32, (uint8_t []){0x08}, 1, 0},
        {0x33, (uint8_t []){0x3f}, 1, 0},
        {0x3c, (uint8_t []){0x04}, 1, 0},
        {0x3d, (uint8_t []){0x78}, 1, 0},
        {0x3e, (uint8_t []){0x43}, 1, 0},
        {0x3f, (uint8_t []){0x30}, 1, 0},
        {0x42, (uint8_t []){0xa2}, 1, 0},
        {0x43, (uint8_t []){0xf0}, 1, 0},
        {0x44, (uint8_t []){0x01}, 1, 0},
        {0x46, (uint8_t []){0x17}, 1, 0},
        {0x49, (uint8_t []){0xc0}, 1, 0},
        {0x6d, (uint8_t []){0x30}, 1, 0},
        {0x6e, (uint8_t []){0x21}, 1, 0},
        {0x41, (uint8_t []){0x5b, 0x5b, 0x03, 0x03, 0x5b, 0x5b, 0x02, 0x02, 0x03, 0x03, 0x03, 0x03}, 12, 0},
        {0x5a, (uint8_t []){0x00, 0x00, 0x34, 0x34, 0x31, 0x31, 0x23, 0x23, 0x24, 0x24, 0x23}, 11, 0},
        {0x5b, (uint8_t []){0x23, 0x0b, 0x0b, 0x09, 0x09, 0x0f, 0x0f, 0x0d, 0x0d, 0x06, 0x06}, 11, 0},
        {0x5c, (uint8_t []){0x00, 0x00, 0x34, 0x34, 0x31, 0x31, 0x23, 0x23, 0x24, 0x24, 0x23}, 11, 0},
        {0x5d, (uint8_t []){0x23, 0x0a, 0x0a, 0x08, 0x08, 0x0e, 0x0e, 0x0c, 0x0c, 0x05, 0x05}, 11, 0},
        {0x5e, (uint8_t []){0x00, 0x00, 0x31, 0x31, 0x34, 0x34, 0x23, 0x23, 0x24, 0x24, 0x23}, 11, 0},
        {0x5f, (uint8_t []){0x23, 0x0c, 0x0c, 0x0e, 0x0e, 0x08, 0x08, 0x0a, 0x0a, 0x05, 0x05}, 11, 0},
        {0x60, (uint8_t []){0x00, 0x00, 0x31, 0x31, 0x34, 0x34, 0x23, 0x23, 0x24, 0x24, 0x23}, 11, 0},
        {0x61, (uint8_t []){0x23, 0x0d, 0x0d, 0x0f, 0x0f, 0x09, 0x09, 0x0b, 0x0b, 0x06, 0x06}, 11, 0},
        {0x64, (uint8_t []){0xff, 0xff, 0x3f}, 3, 0},
        {0x65, (uint8_t []){0xff, 0xff, 0x3f}, 3, 0},
        {0x6f, (uint8_t []){0x03, 0x00, 0x00}, 3, 0},
        {0x70, (uint8_t []){0x03, 0x00, 0x00}, 3, 0},
        {0x71, (uint8_t []){0x00, 0x00, 0x80}, 3, 0},
        {0x72, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
        {0x4c, (uint8_t []){0x22, 0x22}, 2, 0},
        {0x73, (uint8_t []){0x2a}, 1, 0},
        {0x4e, (uint8_t []){0x00, 0x00}, 2, 0},
        {0x50, (uint8_t []){0x00, 0x00}, 2, 0},
        {0x55, (uint8_t []){0xff, 0xff}, 2, 0},
        {0x56, (uint8_t []){0xff, 0xff}, 2, 0},
        {0x57, (uint8_t []){0x00, 0x00}, 2, 0},
        {0x58, (uint8_t []){0xff, 0xff}, 2, 0},
        {0x66, (uint8_t []){0xff, 0xff}, 2, 0},
        {0x67, (uint8_t []){0xff, 0xff}, 2, 0},
        {0x4a, (uint8_t []){0x3f}, 1, 0},
        
        // Page 8 commands
        {0x30, (uint8_t []){0x08}, 1, 0},
        {0x31, (uint8_t []){0x65}, 1, 0},
        {0x33, (uint8_t []){0x05}, 1, 0},
        {0x40, (uint8_t []){0x50}, 1, 0},
        {0x41, (uint8_t []){0x80}, 1, 0},
        {0x42, (uint8_t []){0x1a}, 1, 0},
        {0x47, (uint8_t []){0x0a}, 1, 0},
        {0x48, (uint8_t []){0x0d}, 1, 0},
        {0x50, (uint8_t []){0x17}, 1, 0},
        {0x5a, (uint8_t []){0x20}, 1, 0},
        {0x5b, (uint8_t []){0x00}, 1, 0},
        {0x5c, (uint8_t []){0x53}, 1, 0},
        // MIPI 2 lane configuration
        {0x5D, (uint8_t []){0x0D}, 1, 0},
        {0x5F, (uint8_t []){0x01}, 1, 0},
        {0x62, (uint8_t []){0x04}, 1, 0},
        {0x65, (uint8_t []){0x5f}, 1, 0},
        {0x73, (uint8_t []){0x01}, 1, 0},
        
        // Page A commands
        {0x30, (uint8_t []){0x0a}, 1, 0},
        {0x32, (uint8_t []){0xff}, 1, 0},
        {0x33, (uint8_t []){0x28}, 1, 0},
        {0x3f, (uint8_t []){0x53}, 1, 0},
        {0x40, (uint8_t []){0x15}, 1, 0},
        {0x47, (uint8_t []){0x20}, 1, 0},
        {0x48, (uint8_t []){0x80}, 1, 0},
        {0x49, (uint8_t []){0x03}, 1, 0},
        
        // Page B gamma commands
        {0x30, (uint8_t []){0x0b}, 1, 0},
        {0x33, (uint8_t []){0x00, 0x3d}, 2, 0},
        {0x3c, (uint8_t []){0x00, 0xbf}, 2, 0},
        {0x43, (uint8_t []){0xb1}, 1, 0},
        {0x44, (uint8_t []){0x31}, 1, 0},
        {0x3e, (uint8_t []){0x00, 0x10, 0x1e, 0x27, 0x2f}, 5, 0},
        {0x3f, (uint8_t []){0x54, 0x6f, 0x73, 0x7c, 0x77, 0x91, 0x90, 0x99, 0xa8, 0xa5, 0xae, 0xb5, 0xc5, 0xb8}, 14, 0},
        {0x40, (uint8_t []){0x52, 0x52, 0x53, 0x7f, 0x00, 0x10, 0x1e, 0x27, 0x2f}, 9, 0},
        {0x41, (uint8_t []){0x54, 0x6f, 0x73, 0x7c, 0x77, 0x91, 0x98, 0x99, 0xa8, 0xa5, 0xae, 0xb5, 0xc5, 0xb8}, 14, 0},
        {0x42, (uint8_t []){0x52, 0x52, 0x53, 0x7f}, 4, 0},
        {0x45, (uint8_t []){0x70}, 1, 0},
        {0x46, (uint8_t []){0x3b}, 1, 0},
        {0x48, (uint8_t []){0x7c}, 1, 0},
        {0x49, (uint8_t []){0x1e}, 1, 0},
        {0x4a, (uint8_t []){0x3a}, 1, 0},
        
        // Page C commands
        {0x30, (uint8_t []){0x0c}, 1, 0},
        {0x32, (uint8_t []){0x62}, 1, 0},
        {0x71, (uint8_t []){0x77}, 1, 0},
        
        // Page D commands
        {0x30, (uint8_t []){0x0d}, 1, 0},
        {0x4c, (uint8_t []){0x74}, 1, 0},
        
        // Back to page 0
        {0x30, (uint8_t []){0x00}, 1, 0},
        
        // Tearing effect line on
        {0x35, (uint8_t []){0x00}, 1, 0},
        
        // Sleep out and display on
        {0x11, (uint8_t []){0x00}, 1, 120},  // Sleep out - wait 120ms
        {0x29, (uint8_t []){0x00}, 1, 20},   // Display on - wait 20ms
};

static esp_err_t app_lcd_init(void)
{
    esp_err_t ret = ESP_OK;

    example_bsp_enable_dsi_phy_power();
    example_bsp_init_lcd_backlight();
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL);

    // 首先创建 MIPI DSI 总线，它还将初始化 DSI PHY
    esp_lcd_dsi_bus_handle_t mipi_dsi_bus;
    esp_lcd_dsi_bus_config_t bus_config = {                    \
        .bus_id = 0,                                           \
        .num_data_lanes = 2,                                   \
        .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,           \
        .lane_bit_rate_mbps = 1500,                            \
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_dsi_bus(&bus_config, &mipi_dsi_bus), err, TAG, "LCD init failed");

    ESP_LOGI(TAG, "Install MIPI DSI LCD control panel");
    // 我们使用DBI接口发送LCD命令和参数
    esp_lcd_dbi_io_config_t dbi_config = JD9366_MIPI_PANEL_IO_DBI_CONFIG();

    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_io_dbi(mipi_dsi_bus, &dbi_config, &io_handle), err, TAG, "LCD init failed");

    // 创建JD9366控制面板
    esp_lcd_dpi_panel_config_t dpi_config = {                 \
        .dpi_clk_src = MIPI_DSI_DPI_CLK_SRC_DEFAULT,          \
        .dpi_clock_freq_mhz = 76,                             \
        .virtual_channel = 0,                                 \
        .pixel_format = MIPI_DPI_PX_FORMAT,                   \
        .num_fbs = 1,                                         \
        .video_timing = {                                     \
            .h_size = EXAMPLE_LCD_H_RES,                      \
            .v_size = EXAMPLE_LCD_V_RES,                      \
            .hsync_back_porch = 28,                           \
            .hsync_pulse_width = 8,                           \
            .hsync_front_porch = 40,                          \
            .vsync_back_porch = 20,                           \
            .vsync_pulse_width = 8,                           \
            .vsync_front_porch = 140,                         \
        },                                                    \
        .flags.use_dma2d = true,                              \
    };

    jd9366_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,      // Uncomment these line if use custom initialization commands
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(jd9366_lcd_init_cmd_t),
        //.flags.use_mipi_interface = 1,
        .mipi_config = {
            .dsi_bus = mipi_dsi_bus,
            .dpi_config = &dpi_config,
        },
    };
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_COLOR_DEPTH,
        .vendor_config = &vendor_config,
    };
    ESP_GOTO_ON_ERROR(esp_lcd_new_panel_jd9366(io_handle, &panel_config, &lcd_panel), err, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_reset(lcd_panel), err, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_init(lcd_panel), err, TAG, "LCD init failed");
    ESP_GOTO_ON_ERROR(esp_lcd_panel_disp_on_off(lcd_panel, true), err, TAG, "LCD init failed");

    // 打开背光
    example_bsp_set_lcd_backlight(EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);

    return ret;

err:
    if (lcd_panel) {
        esp_lcd_panel_del(lcd_panel);
    }
    return ret;
}

static esp_err_t app_touch_init(void)
{
    /* Initilize I2C */
    const i2c_master_bus_config_t i2c_bus_config = {
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .i2c_port = EXAMPLE_TOUCH_I2C_NUM,
        .scl_io_num = EXAMPLE_TOUCH_I2C_SCL,
        .sda_io_num = EXAMPLE_TOUCH_I2C_SDA,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t i2c_bus;
    ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_bus_config, &i2c_bus));

    esp_lcd_panel_io_handle_t tp_io_handle = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_config = ESP_LCD_TOUCH_IO_I2C_JD9366_CONFIG();
    tp_io_config.scl_speed_hz = EXAMPLE_TOUCH_I2C_CLK_HZ;

    ESP_ERROR_CHECK(esp_lcd_new_panel_io_i2c(i2c_bus, &tp_io_config, &tp_io_handle));

    /* Initialize touch HW */
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = EXAMPLE_LCD_H_RES,
        .y_max = EXAMPLE_LCD_V_RES,
        .rst_gpio_num = EXAMPLE_TOUCH_RST,
        .int_gpio_num = EXAMPLE_TOUCH_INT,
        .levels = {
            .reset = 0,
            .interrupt = 0,
        },
        .flags = {
            .swap_xy = 0,
            .mirror_x = 0,
            .mirror_y = 0,
        },
    };
   
    return esp_lcd_touch_new_i2c_jd9366(tp_io_handle, &tp_cfg, &touch_handle);
}

static esp_err_t app_lvgl_init(void)
{
    /* Initialize LVGL */
    const lvgl_port_cfg_t lvgl_cfg = {
        .task_priority = 4,         /* LVGL task priority */
        .task_stack = 4096*2,         /* LVGL 任务堆栈大小*/
        .task_affinity = -1,        /* LVGL task pinned to core (-1 is no affinity) */
        .task_max_sleep_ms = 500,   /* Maximum sleep in LVGL task */
        .timer_period_ms = 5        /* LVGL timer tick period in ms */
    };
    ESP_RETURN_ON_ERROR(lvgl_port_init(&lvgl_cfg), TAG, "LVGL port initialization failed");

    /* Add LCD screen */
    ESP_LOGD(TAG, "Add LCD screen");
    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = io_handle,
        .panel_handle = lcd_panel,
        .buffer_size = EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES,
        .double_buffer = true,
        .hres = EXAMPLE_LCD_H_RES,
        .vres = EXAMPLE_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = true,// true：软件；false：硬件
            .swap_bytes = false,
            .full_refresh = false,
            .direct_mode = false,
        }
    };

    const lvgl_port_display_dsi_cfg_t dpi_cfg = {
        .flags = {
            .avoid_tearing = false,
        }
    };

    lvgl_disp = lvgl_port_add_disp_dsi(&disp_cfg, &dpi_cfg);

    /* Add touch input (for selected screen) */
    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = lvgl_disp,
        .handle = touch_handle,
    };
    lvgl_touch_indev = lvgl_port_add_touch(&touch_cfg);

    return ESP_OK;
}

void app_main(void)
{
    /* LCD HW initialization */
    ESP_ERROR_CHECK(app_lcd_init());

    /* Touch initialization */
    ESP_ERROR_CHECK(app_touch_init());

    /* LVGL initialization */
    ESP_ERROR_CHECK(app_lvgl_init());

    /* Show LVGL objects */
    lvgl_port_lock(0);

    lv_disp_set_rotation(lvgl_disp, LV_DISP_ROTATION_90);

    // lv_demo_music();
    lv_demo_widgets();

    lvgl_port_unlock();
}
