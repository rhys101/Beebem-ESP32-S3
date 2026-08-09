idf_component_register(
    SRCS
        "src/6502core.cpp"
        "src/atodconv.cpp"
        "src/bbc_core.cpp"
        "src/beebmem.cpp"
        "src/beebwin.cpp"
        "src/disc8271.cpp"
        "src/mode7font.c"
        "src/platform_shim.cpp"
        "src/sysvia.cpp"
        "src/uservia.cpp"
        "src/via.cpp"
        "src/video.cpp"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_timer esp_psram)

target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2
    -Wno-deprecated-enum-enum-conversion
    -Wno-format
    -Wno-unused-but-set-variable
    -Wno-unused-variable)
