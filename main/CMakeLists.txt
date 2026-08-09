idf_component_register(SRCS "main.cpp" INCLUDE_DIRS "."
                       REQUIRES assets audio bbc_core board driver esp_timer fatfs input
                                wear_levelling)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Werror)
