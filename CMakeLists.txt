cmake_minimum_required(VERSION 3.10)

# Project version
project(plaatcraft VERSION 0.1)

# Use the C99 standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)

# Config file generator
configure_file(include/config.h.in ../include/config.h)

# Include directory
include_directories(include/)

# Glad Library
add_library(glad src/glad/glad.c)

# STB Image Library
add_library(stb_image src/stb_image/stb_image.c)

# Tiny C Thread Library
add_library(tinycthread src/tinycthread/tinycthread.c)

# Plaatcraft executable
add_executable(
    plaatcraft src/main.c src/log.c src/utils.c
    src/geometry/block.c src/geometry/plane.c
    src/math/vector4.c src/math/matrix4.c
    src/shaders/shader.c src/shaders/block_shader.c src/shaders/flat_shader.c
    src/textures/texture.c src/textures/texture_atlas.c
    src/game.c src/camera.c src/chunk.c src/world.c
)
target_compile_options(plaatcraft PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Final linking with external libs
target_link_libraries(plaatcraft glad stb_image tinycthread glfw m dl pthread)

# Assets folder
file(COPY assets DESTINATION ${CMAKE_BINARY_DIR})