cmake_minimum_required(VERSION 3.1)
project(lidaEngine)

find_package(SDL2 REQUIRED)
find_package(Freetype REQUIRED)

add_subdirectory(src)

add_executable(tst src/main.cpp)
target_link_libraries(tst lidaEngine)

set(ENABLE_ASAN 0)

add_subdirectory(shaders)
add_shader(tst "triangle.vert")
add_shader(tst "triangle.frag")
add_shader(tst "rect.vert")
add_shader(tst "rect.frag")
add_shader(tst "voxel.vert")
add_shader(tst "voxel.frag")
add_shader(tst "shadow_voxel.vert")
add_shader(tst "text.vert")
add_shader(tst "text.frag")

include(CTest)
add_subdirectory(test)
enable_testing()
