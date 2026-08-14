
idf_component_register(
    SRCS "esp32_p4_function_ev_board.c" 
    INCLUDE_DIRS "include" "include/bsp"
    PRIV_INCLUDE_DIRS "priv_include"
    REQUIRES driver esp_lcd_jd9366
    PRIV_REQUIRES esp_lcd usb spiffs fatfs  
)

