file(GLOB_RECURSE UI_SRCS "ui/*.c")

idf_component_register(
    SRCS
        "main.cpp"
        "ble_adapter.cpp"
        "storage_adapter.cpp"
        "display.cpp"
        "fonts.cpp"
        "poll_scheduler.cpp"
        "diagnostics.cpp"
        "config_manager.cpp"
        "ota.cpp"
        ${UI_SRCS}
    INCLUDE_DIRS
        "."
        "ui"
    REQUIRES
        bt
        app_update
        esp_app_format
        nvs_flash
        driver
        esp_timer
        mbedtls
        tesla-ble
        esp_lcd
        lvgl
)

set_source_files_properties(fonts.cpp PROPERTIES COMPILE_FLAGS "-O0")

