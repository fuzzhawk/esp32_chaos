idf_component_register(
    SRCS
        "os_core.c"
        "launcher.c"
        "theme.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES lvgl bsp
    PRIV_REQUIRES esp_timer
)
