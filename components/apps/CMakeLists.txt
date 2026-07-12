idf_component_register(
    SRCS
        "apps.c"
        "app_life.c"
        "app_tama.c"
        "app_companion.c"
    INCLUDE_DIRS "include"
    REQUIRES chaos_os lvgl
    PRIV_REQUIRES nvs_flash esp_hw_support esp_timer
)
