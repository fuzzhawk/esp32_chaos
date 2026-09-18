idf_component_register(
    SRCS
        "bsp.c"
        "bsp_i2c.c"
        "bsp_display.c"
        "bsp_touch.c"
        "bsp_imu.c"
        "bsp_power.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES driver esp_lcd
    PRIV_REQUIRES esp_timer esp_lcd_sh8601
)
