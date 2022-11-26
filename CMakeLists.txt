cmake_minimum_required(VERSION 3.0)
project(lidaEngine2)

find_package(SDL2 REQUIRED)

add_executable(tst src/main.c src/device.c src/memory.c src/window.c src/base.c src/volk.c)
target_link_libraries(tst SDL2::SDL2)

set_target_properties(tst PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

set(ENABLE_ASAN 0)

if (ENABLE_ASAN)
  # enable all warnings from compiler
  target_compile_options(tst PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>                         # MSVC
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic -fsanitize=address -fno-omit-frame-pointer> # g++ or clang
  )

  target_link_options(tst PUBLIC
    -fsanitize=address -fno-omit-frame-pointer -lrt)
else ()
  # enable all warnings from compiler
  target_compile_options(tst PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /WX>                         # MSVC
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic> # gcc or clang
  )
endif()

# https://stackoverflow.com/questions/57478571/why-wont-cmake-build-my-vulkan-spirv-shaders
set(SHADER_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)
set(SHADER_BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/shaders)
set(GLSLC_EXECUTABLE glslc)

function(add_shader TARGET SHADER)
  find_program(GLSLC glslc)

  set(current-shader-path ${CMAKE_CURRENT_SOURCE_DIR}/shaders/${SHADER})
  set(current-output-path ${CMAKE_BINARY_DIR}/shaders/${SHADER}.spv)

  get_filename_component(current-output-dir ${current-output-path} DIRECTORY)
  file(MAKE_DIRECTORY ${current-output-dir})

  add_custom_command(
	OUTPUT ${current-output-path}
	COMMAND ${GLSLC} -O -MD -MF ${current-output-path}.d -o ${current-output-path} ${current-shader-path}
        DEPENDS ${current-shader-path}
        IMPLICIT_DEPENDS CXX ${current-shader-path}
        VERBATIM)

  set_source_files_properties(${current-output-path} PROPERTIES GENERATED TRUE)
  target_sources(${TARGET} PRIVATE ${current-output-path})
endfunction(add_shader)

add_shader(tst "triangle.vert")
add_shader(tst "triangle.frag")
