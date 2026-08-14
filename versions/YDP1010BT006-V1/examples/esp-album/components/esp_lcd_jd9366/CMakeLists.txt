idf_component_register(SRCS "esp_lcd_jd9366.c" "esp_lcd_touch_jd9366.c"
                       INCLUDE_DIRS "include"
                       REQUIRES esp_lcd esp_lcd_touch)

include(package_manager)
cu_pkg_define_version(${CMAKE_CURRENT_LIST_DIR})
