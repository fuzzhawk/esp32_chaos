idf_component_register(
    SRCS
        "modes.c"
        "crystal.c"
        "mode_sand.c"
        "mode_garden.c"
        "mode_glitch.c"
        "mode_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp fx audio
    PRIV_REQUIRES esp_timer nvs_flash
)
