idf_component_register(
    SRCS "motion_input.c" "power_button.c" "boot_button.c" "touch_input.c" "ble_keyboard.c" "esp_hid_gap.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES bt esp_hid driver nvs_flash qmi8658)

target_compile_options(${COMPONENT_LIB} PRIVATE -O2)
