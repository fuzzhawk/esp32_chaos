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
    # Public deps: bsp.h exposes lvgl + esp_lcd types to callers.
    REQUIRES driver esp_lcd lvgl
    # Private deps: the panel/touch/port drivers are pulled in via
    # idf_component.yml and only referenced inside the .c files.
    PRIV_REQUIRES esp_timer esp_lvgl_port esp_lcd_sh8601
)
