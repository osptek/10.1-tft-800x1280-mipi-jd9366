/*******************************************************************
 *
 * main.c - LVGL runner for Raspberry Pi 5 (Linux DRM + EVDEV)
 *
 * Modified for RP1 DSI (/dev/dri/card2) and JD9366 Touch (/dev/input/event5)
 *
 ******************************************************************/
#include <unistd.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl/lvgl.h"
#include "lvgl/demos/lv_demos.h"

#include "src/lib/driver_backends.h"
#include "src/lib/simulator_util.h"
#include "src/lib/simulator_settings.h"

/* Internal functions */
static void configure_simulator(int argc, char ** argv);
static void print_lvgl_version(void);
static void print_usage(void);

/* contains the name of the selected backend if user
 * has specified one on the command line */
static char * selected_backend;

/* Global simulator settings, defined in lv_linux_backend.c */
extern simulator_settings_t settings;


/**
 * @brief Print LVGL version
 */
static void print_lvgl_version(void)
{
    fprintf(stdout, "%d.%d.%d-%s\n",
            LVGL_VERSION_MAJOR,
            LVGL_VERSION_MINOR,
            LVGL_VERSION_PATCH,
            LVGL_VERSION_INFO);
}

/**
 * @brief Print usage information
 */
static void print_usage(void)
{
    fprintf(stdout,
            "\nlvglsim [-V] [-B] [-f] [-m] [-b backend_name] [-W window_width] [-H window_height] [-R rotation]\n\n");
    fprintf(stdout, "-V print LVGL version\n");
    fprintf(stdout, "-B list supported backends\n");
    fprintf(stdout, "-f fullscreen\n");
    fprintf(stdout, "-m maximize\n");
}

/**
 * @brief Configure simulator
 * @description process arguments received by the program to select
 * appropriate options
 * @param argc the count of arguments in argv
 * @param argv The arguments
 */
static void configure_simulator(int argc, char ** argv)
{
    int opt = 0;

    selected_backend = NULL;
    driver_backends_register();

    const char * env_w = getenv("LV_SIM_WINDOW_WIDTH");
    const char * env_h = getenv("LV_SIM_WINDOW_HEIGHT");
    
    /* 针对 800x1280 JD9366 面板的默认分辨率 */
    settings.window_width = atoi(env_w ? env_w : "800");
    settings.window_height = atoi(env_h ? env_h : "1280");

    /* Parse the command-line options. */
    while((opt = getopt(argc, argv, "b:fmW:H:R:BVh")) != -1) {
        switch(opt) {
            case 'h':
                print_usage();
                exit(EXIT_SUCCESS);
                break;
            case 'V':
                print_lvgl_version();
                exit(EXIT_SUCCESS);
                break;
            case 'B':
                driver_backends_print_supported();
                exit(EXIT_SUCCESS);
                break;
            case 'b':
                if(driver_backends_is_supported(optarg) == 0) {
                    die("error no such backend: %s\n", optarg);
                }
                selected_backend = strdup(optarg);
                break;
            case 'f':
                settings.fullscreen = true;
                break;
            case 'm':
                settings.maximize = true;
                break;
            case 'W':
                settings.window_width = atoi(optarg);
                break;
            case 'H':
                settings.window_height = atoi(optarg);
                break;
            case 'R':
                switch(atoi(optarg)) {
                    case 0:
                        settings.rotation = LV_DISPLAY_ROTATION_0;
                        break;
                    case 90:
                        settings.rotation = LV_DISPLAY_ROTATION_90;
                        break;
                    case 180:
                        settings.rotation = LV_DISPLAY_ROTATION_180;
                        break;
                    case 270:
                        settings.rotation = LV_DISPLAY_ROTATION_270;
                        break;
                    default:
                        LV_LOG_WARN("Invalid rotation angle. Valid angles are {0, 90, 180, 270}");
                        break;
                }
                break;
            case ':':
                print_usage();
                die("Option -%c requires an argument.\n", optopt);
                break;
            case '?':
                print_usage();
                die("Unknown option -%c.\n", optopt);
        }
    }
}

/**
 * @brief entry point
 * @description start a demo
 * @param argc the count of arguments in argv
 * @param argv The arguments
 */
int main(int argc, char ** argv)
{
    configure_simulator(argc, argv);

    /* Initialize LVGL. */
    lv_init();

    /* 1. 显式指定树莓派 5 默认后端的环境变量（如果没有通过 -b 指定） */
    if(selected_backend == NULL) {
        selected_backend = "DRM";
    }

    /* 2. 覆盖设置 DRM 显卡设备节点为树莓派 5 的 MIPI-DSI 卡节点 (/dev/dri/card2) */
    setenv("LV_LINUX_DRM_CARD", "/dev/dri/card2", 1);

    /* 初始化 DRM 显示后端 */
    if(driver_backends_init_backend(selected_backend) == -1) {
        die("Failed to initialize display backend");
    }

    if(settings.rotation) {
#if LV_USE_DRAW_NANOVG && LV_DRAW_TRANSFORM_USE_MATRIX
        lv_display_set_matrix_rotation(NULL, true);
#endif
        lv_display_set_rotation(NULL, settings.rotation);
    }

    /* 3. 配置 EVDEV 触摸设备节点为 jd9366_touch 的 /dev/input/event5 */
#if LV_USE_EVDEV
    setenv("LV_LINUX_EVDEV_POINTER_DEVICE", "/dev/input/event5", 1);
    if(driver_backends_init_backend("EVDEV") == -1) {
        die("Failed to initialize evdev");
    }
#endif

    /* 4. 创建及启动 Demo */
    lv_demo_widgets();

    /* 隐藏所有指针/触摸设备绑定的光标 */
    lv_indev_t * indev = lv_indev_get_next(NULL);
    while(indev) {
        if(lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER) {
            lv_obj_t * cursor = lv_indev_get_cursor(indev);
            if(cursor) {
                lv_obj_add_flag(cursor, LV_OBJ_FLAG_HIDDEN);
            }
        }
        indev = lv_indev_get_next(indev);
    }

    /* 5. 进入主运行循环 */
    driver_backends_run_loop();

    return 0;
}
