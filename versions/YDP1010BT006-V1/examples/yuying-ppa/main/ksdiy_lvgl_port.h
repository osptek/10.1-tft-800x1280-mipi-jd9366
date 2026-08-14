#ifndef _KSDIY_LVGL_PORT_
#define _KSDIY_LVGL_PORT_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"

#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_log.h"
#include "driver/gpio.h"


#ifdef __cplusplus
extern "C" {
#endif
void ksdiy_lvgl_port_init(void);
bool ksdiy_lvgl_lock(int timeout_ms);

void ksdiy_lvgl_unlock(void);

#ifdef __cplusplus
} /*extern "C"*/
#endif

#endif