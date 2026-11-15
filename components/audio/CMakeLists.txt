idf_component_register(
    SRCS "audio.c"
    INCLUDE_DIRS "include"
    REQUIRES bsp
    PRIV_REQUIRES driver esp_codec_dev
)
