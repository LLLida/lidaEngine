cmake_minimum_required(VERSION 3.1)
project(lidaEngine)

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
