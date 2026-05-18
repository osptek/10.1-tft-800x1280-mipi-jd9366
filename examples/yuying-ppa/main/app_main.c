#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "lv_demos.h"
#include "ksdiy_lvgl_port.h"

static const char *TAG = "MIPI_TEST";
void app_main(void)
{
    ksdiy_lvgl_port_init();
    ksdiy_lvgl_lock(-1);
    lv_demo_widgets();
    //lv_demo_music();
    ksdiy_lvgl_unlock();
}