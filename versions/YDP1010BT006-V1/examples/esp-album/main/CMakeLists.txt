# Define module directories
set(CORE_DIR core)
set(STORAGE_DIR storage)
set(MEDIA_DIR media)
set(UI_DIR ui)
set(CONTROL_DIR control)
set(USB_DIR usb)
set(NETWORK_DIR network)

# Collect source files from each module
file(GLOB_RECURSE CORE_SOURCES ${CORE_DIR}/*.c)
file(GLOB_RECURSE STORAGE_SOURCES ${STORAGE_DIR}/*.c)
file(GLOB_RECURSE MEDIA_SOURCES ${MEDIA_DIR}/*.c)
file(GLOB_RECURSE UI_SOURCES ${UI_DIR}/*.c)
file(GLOB_RECURSE CONTROL_SOURCES ${CONTROL_DIR}/*.c)
file(GLOB_RECURSE USB_SOURCES ${USB_DIR}/*.c)
file(GLOB_RECURSE NETWORK_SOURCES ${NETWORK_DIR}/*.c)


# Combine all sources
set(COMPONENT_SRCS
    "main.c" "ksdiy_lvgl_port.c"
    ${CORE_SOURCES}
    ${STORAGE_SOURCES}
    ${MEDIA_SOURCES}
    ${UI_SOURCES}
    ${CONTROL_SOURCES}
    ${USB_SOURCES}
    ${NETWORK_SOURCES}
)

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS
        "."
        ${CORE_DIR}
        ${STORAGE_DIR}
        ${MEDIA_DIR}
        ${UI_DIR}
        ${CONTROL_DIR}
        ${USB_DIR}
        ${NETWORK_DIR}
    REQUIRES
        esp_driver_jpeg
        esp_driver_ppa
        esp_mm
        bsp_extra
        esp_extractor
        espressif__esp32_p4_function_ev_board
        esp_tinyusb
        esp_http_server
        esp_wifi
        nvs_flash
        json
        esp_lcd_jd9366
    EMBED_FILES
        "network/assets/modern_upload.html"
        "network/assets/modern_upload.css"
        "network/assets/modern_upload.js"
)
