idf_component_register(
    SRCS
        "modes.c"
        "crystal.c"
        "mode_sand.c"
        "mode_garden.c"
        "mode_glitch.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp fx
    PRIV_REQUIRES esp_timer nvs_flash
)
