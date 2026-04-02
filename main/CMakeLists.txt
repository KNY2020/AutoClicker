idf_component_register(
    SRCS
        "main.c"
        "ble_hid.c"
        "click_engine.c"
        "config_manager.c"
        "status_led.c"
    INCLUDE_DIRS "."
    REQUIRES
        nvs_flash
        bt
        esp_timer
        esp_driver_gpio
        hal
)
