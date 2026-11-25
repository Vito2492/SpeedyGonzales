cmake_minimum_required(VERSION 3.22)
project(speedyarriba C)

if(NOT SDL2_DIR)
    message(FATAL_ERROR "Pass -DSDL2_DIR=/path/to/SDL2-2.x.x")
endif()

add_subdirectory("${SDL2_DIR}" "${CMAKE_BINARY_DIR}/SDL2" EXCLUDE_FROM_ALL)

add_library(main SHARED
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src/speedy.c"
)

target_include_directories(main PRIVATE
    "${SDL2_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../../../../src"
)

target_link_libraries(main
    SDL2
    SDL2main
    log
    android
    dl
    m
)
