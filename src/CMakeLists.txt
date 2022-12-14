cmake_minimum_required(VERSION 3.1)
project(lidaEngine)

add_library(lidaEngine STATIC
  device.c memory.c window.c base.c linalg.c ecs.c render.c voxel.cc volk.c)
target_link_libraries(lidaEngine PUBLIC SDL2::SDL2)
target_include_directories(lidaEngine PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(lidaEngine PRIVATE
  LIDA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")

set_target_properties(lidaEngine PROPERTIES
  C_STANDARD 11
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
  CXX_STANDARD 11
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF)

set(ENABLE_ASAN 0)

if (ENABLE_ASAN)
  # enable all warnings from compiler
  target_compile_options(lidaEngine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>                         # MSVC
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -fsanitize=address -fno-omit-frame-pointer> # g++ or clang
  )
  target_link_options(lidaEngine PUBLIC
    -fsanitize=address -fno-omit-frame-pointer -lrt)
else ()
  # enable all warnings from compiler
  target_compile_options(lidaEngine PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>                         # MSVC
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic> # gcc or clang
  )
endif()
