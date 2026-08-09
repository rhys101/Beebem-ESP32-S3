cmake_minimum_required(VERSION 3.20)
project(bc32_host LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CORE_DIR ${CMAKE_CURRENT_LIST_DIR}/../../components/bbc_core)

add_executable(bc32_host
    host_main.cpp
    ${CORE_DIR}/src/6502core.cpp
    ${CORE_DIR}/src/atodconv.cpp
    ${CORE_DIR}/src/bbc_core.cpp
    ${CORE_DIR}/src/beebmem.cpp
    ${CORE_DIR}/src/beebwin.cpp
    ${CORE_DIR}/src/disc8271.cpp
    ${CORE_DIR}/src/mode7font.c
    ${CORE_DIR}/src/platform_shim.cpp
    ${CORE_DIR}/src/sysvia.cpp
    ${CORE_DIR}/src/uservia.cpp
    ${CORE_DIR}/src/via.cpp
    ${CORE_DIR}/src/video.cpp)

target_include_directories(bc32_host PRIVATE ${CORE_DIR}/include ${CORE_DIR}/src)
target_compile_definitions(bc32_host PRIVATE HAVE_CONFIG_H=0 GP2X=1)
target_compile_options(bc32_host PRIVATE -Wall -Wextra -Wno-unused-parameter
    -Wno-unused-variable -Wno-unused-function -Wno-parentheses -Wno-sign-compare)
