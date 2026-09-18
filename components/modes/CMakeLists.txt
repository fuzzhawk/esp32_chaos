idf_component_register(
    SRCS
        "modes.c"
        "mode_sand.c"
        "mode_rpg.c"
        "mode_glitch.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp fx
    PRIV_REQUIRES esp_timer
)
