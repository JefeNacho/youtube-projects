idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES driver esp_timer esp_partition nvs_flash esp-sr esp-dsp
)
