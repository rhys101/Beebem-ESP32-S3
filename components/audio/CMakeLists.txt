idf_component_register(
    SRCS "bc32_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_codec_dev esp_driver_gpio esp_driver_i2c esp_driver_i2s)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror -O2)
