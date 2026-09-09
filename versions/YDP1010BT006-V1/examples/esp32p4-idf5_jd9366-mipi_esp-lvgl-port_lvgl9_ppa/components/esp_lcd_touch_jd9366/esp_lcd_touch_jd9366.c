#include <string.h>
#include <sys/cdefs.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_touch.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_lcd_touch_jd9366.h"

static const char *TAG = "JD9366";

// 地址与芯片常量
#define JD9366T_ID                   0x9032      // 期望的芯片 ID
#define JD9366T_TOUCH_DATA_LEN       80          // 完整触摸数据长度 (10点 x 5字节 + 偏移)
#define JD9366T_MAX_TOUCH_POINTS     10          // 芯片支持的最大触摸点数

// 硬件结构体定义：单个触摸点原始数据格式 (5 字节)
#pragma pack(push, 1)
typedef struct {
    uint8_t x_area_id;
    uint8_t x;
    uint8_t y_area_id;
    uint8_t y;
    uint8_t fix_unknown;
} jd9366_touch_point_t;
#pragma pack(pop)

// 函数声明
static esp_err_t jd9366_enter_backdoor(esp_lcd_touch_handle_t tp);
static esp_err_t jd9366_read_chip_id(esp_lcd_touch_handle_t tp, uint16_t *chip_id);
static esp_err_t touch_jd9366_read_data(esp_lcd_touch_handle_t tp);
static bool touch_jd9366_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num);
static esp_err_t touch_jd9366_del(esp_lcd_touch_handle_t tp);
static esp_err_t touch_jd9366_reset(esp_lcd_touch_handle_t tp);

esp_err_t esp_lcd_touch_new_i2c_jd9366(const esp_lcd_panel_io_handle_t io, const esp_lcd_touch_config_t *config, esp_lcd_touch_handle_t *out_touch)
{
    esp_err_t ret = ESP_OK;

    ESP_GOTO_ON_FALSE(io, ESP_ERR_INVALID_ARG, err, TAG, "Invalid io handle");
    ESP_GOTO_ON_FALSE(config, ESP_ERR_INVALID_ARG, err, TAG, "Invalid config");
    ESP_GOTO_ON_FALSE(out_touch, ESP_ERR_INVALID_ARG, err, TAG, "Invalid output handle");

    // 分配内存
    esp_lcd_touch_handle_t esp_lcd_touch_jd9366 = heap_caps_calloc(1, sizeof(esp_lcd_touch_t), MALLOC_CAP_DEFAULT);
    ESP_GOTO_ON_FALSE(esp_lcd_touch_jd9366, ESP_ERR_NO_MEM, err, TAG, "no mem for JD9366 controller");

    // 初始化基本配置
    esp_lcd_touch_jd9366->io = io;

    // 设置基础接口函数
    esp_lcd_touch_jd9366->read_data = touch_jd9366_read_data;
    esp_lcd_touch_jd9366->get_xy = touch_jd9366_get_xy;
    esp_lcd_touch_jd9366->del = touch_jd9366_del;

    /* Mutex 初始化 */
    esp_lcd_touch_jd9366->data.lock.owner = portMUX_FREE_VAL;

    /* 保存配置 */
    memcpy(&esp_lcd_touch_jd9366->config, config, sizeof(esp_lcd_touch_config_t));

    // 复位控制
    if (esp_lcd_touch_jd9366->config.rst_gpio_num != GPIO_NUM_NC)
    {
        ESP_RETURN_ON_ERROR(gpio_set_level(esp_lcd_touch_jd9366->config.rst_gpio_num, 0), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_RETURN_ON_ERROR(gpio_set_level(esp_lcd_touch_jd9366->config.rst_gpio_num, 1), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    else
    {
        ret = touch_jd9366_reset(esp_lcd_touch_jd9366);
        ESP_GOTO_ON_ERROR(ret, err_free, TAG, "JD9366 reset failed");
    }

    ESP_LOGI(TAG, "开始初始化 JD9366 触摸芯片");

    // 读取并验证芯片 ID
    uint16_t chip_id = 0;
    ret = jd9366_read_chip_id(esp_lcd_touch_jd9366, &chip_id);
    if (ret == ESP_OK)
    {
        ESP_LOGI(TAG, "读取到芯片 ID: 0x%04X (期望: 0x%04X)", chip_id, JD9366T_ID);
        if (chip_id != JD9366T_ID)
        {
            ESP_LOGW(TAG, "芯片 ID 不匹配，但继续初始化");
        }
    }
    else
    {
        ESP_LOGE(TAG, "读取芯片 ID 失败");
        goto err_free;
    }

    // 进入 BackDoor 模式
    ret = jd9366_enter_backdoor(esp_lcd_touch_jd9366);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "进入 BackDoor 模式失败");
        goto err_free;
    }

    ESP_LOGI(TAG, "JD9366 驱动初始化完成");

    *out_touch = esp_lcd_touch_jd9366;
    return ESP_OK;

err_free:
    free(esp_lcd_touch_jd9366);
err:
    return ret;
}

// 进入 BackDoor 模式
static esp_err_t jd9366_enter_backdoor(esp_lcd_touch_handle_t tp)
{
    esp_err_t ret;
    ESP_LOGI(TAG, "进入 BackDoor 模式");

    ret = esp_lcd_panel_io_tx_param(tp->io, 0x40008004, (uint8_t[]){0xa5}, 1);
    if (ret != ESP_OK) return ret;

    ret = esp_lcd_panel_io_tx_param(tp->io, 0x40008081, (uint8_t[]){0x00}, 1);
    if (ret != ESP_OK) return ret;

    // 等待 BackDoor 建立
    vTaskDelay(pdMS_TO_TICKS(10));

    ESP_LOGI(TAG, "进入 BackDoor 模式成功");
    return ESP_OK;
}

// 读取芯片 ID
static esp_err_t jd9366_read_chip_id(esp_lcd_touch_handle_t tp, uint16_t *chip_id)
{
    esp_err_t ret;
    uint8_t data[2] = {0};

    // 读取 ID 寄存器 0x40008076
    ret = esp_lcd_panel_io_rx_param(tp->io, 0x40008076, data, 2);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "读取芯片 ID 数据失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 组合字节: (data[1] << 8) | data[0]
    *chip_id = (uint16_t)((data[1] << 8) | data[0]);

    ESP_LOGI(TAG, "原始 ID 数据: [0]=0x%02X, [1]=0x%02X", data[0], data[1]);

    return ESP_OK;
}

// 读取触摸坐标数据
static esp_err_t touch_jd9366_read_data(esp_lcd_touch_handle_t tp)
{
    esp_err_t ret;
    uint8_t cmd[4] = {0x20, 0x02, 0x11, 0x20}; // 读取坐标命令包
    uint8_t read_buf[JD9366T_TOUCH_DATA_LEN] = {0};

    // 1. 发送读取坐标命令
    ret = esp_lcd_panel_io_tx_param(tp->io, -1, cmd, sizeof(cmd));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "发送坐标读取命令失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 读取 80 字节数据
    ret = esp_lcd_panel_io_rx_param(tp->io, -1, read_buf, sizeof(read_buf));
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "读取触摸数据失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 临界区加锁保护
    portENTER_CRITICAL(&tp->data.lock);

    // 3. 检查是否有触摸点
    if (read_buf[0] == 0x00) {
        tp->data.points = 0;
        portEXIT_CRITICAL(&tp->data.lock);
        return ESP_OK;
    }

    uint8_t point_cnt = 0;
    uint16_t max_x = tp->config.x_max;
    uint16_t max_y = tp->config.y_max;

    // 4. 解析多点触摸数据 (数据从 index 3 开始，每个点 5 字节)
    for (int i = 0; i < JD9366T_MAX_TOUCH_POINTS && point_cnt < CONFIG_ESP_LCD_TOUCH_MAX_POINTS; i++) {
        jd9366_touch_point_t *raw_pt = (jd9366_touch_point_t *)&read_buf[3 + i * 5];

        // 过滤非按压/脏数据逻辑 (大于 100 判定为无效点)
        if (raw_pt->x_area_id > 100 || raw_pt->y_area_id > 100) {
            continue;
        }

        // 使用原厂标准计算公式: actual = area_id * 255 + offset
        uint16_t actual_x = (uint16_t)raw_pt->x_area_id * 255 + raw_pt->x;
        uint16_t actual_y = (uint16_t)raw_pt->y_area_id * 255 + raw_pt->y;

        // 边界保护
        if (actual_x > max_x) actual_x = max_x;
        if (actual_y > max_y) actual_y = max_y;

        // 写入全局坐标缓存
        tp->data.coords[point_cnt].x = actual_x;
        tp->data.coords[point_cnt].y = actual_y;
        tp->data.coords[point_cnt].strength = 50; // 默认压力值
        point_cnt++;
    }

    tp->data.points = point_cnt;

    // 退出临界区
    portEXIT_CRITICAL(&tp->data.lock);

    return ESP_OK;
}

// 软件复位控制
static esp_err_t touch_jd9366_reset(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);

    if (tp->config.rst_gpio_num != GPIO_NUM_NC)
    {
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(50));
        ESP_RETURN_ON_ERROR(gpio_set_level(tp->config.rst_gpio_num, !tp->config.levels.reset), TAG, "GPIO set level error!");
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    return ESP_OK;
}

// 提取坐标（提供给 LVGL 或上层 UI 使用）
static bool touch_jd9366_get_xy(esp_lcd_touch_handle_t tp, uint16_t *x, uint16_t *y, uint16_t *strength, uint8_t *point_num, uint8_t max_point_num)
{
    assert(tp != NULL);
    assert(x != NULL);
    assert(y != NULL);
    assert(point_num != NULL);
    assert(max_point_num > 0);

    portENTER_CRITICAL(&tp->data.lock);

    *point_num = (tp->data.points > max_point_num ? max_point_num : tp->data.points);

    for (size_t i = 0; i < *point_num; i++)
    {
        x[i] = tp->data.coords[i].x;
        y[i] = tp->data.coords[i].y;

        if (strength)
        {
            strength[i] = tp->data.coords[i].strength;
        }
    }

    /* 提取完成后清理当前点数标识，等待下次 read_data 刷新 */
    tp->data.points = 0;

    portEXIT_CRITICAL(&tp->data.lock);

    return (*point_num > 0);
}

// 删除并释放驱动资源
static esp_err_t touch_jd9366_del(esp_lcd_touch_handle_t tp)
{
    assert(tp != NULL);
    free(tp);
    return ESP_OK;
}
