cmake_minimum_required(VERSION 3.6)

project(GAME)

# armeabi-v7a requires cpufeatures library
# include(AndroidNdkModules)
# android_ndk_import_module_cpufeatures()

message(STATUS "Android CPU: ${ANDROID_ARCH}")

add_subdirectory(SDL2)

# Compilation of companion libraries
#add_subdirectory(SDL_image)
#add_subdirectory(SDL_mixer)
#add_subdirectory(SDL_ttf)

add_subdirectory(freetype)

find_library(SDL2 SDL2)
find_library(log log)

add_library(main SHARED)

set(LIDA_ENGINE_SOURCE_DIR ../../../src)

target_sources(main PRIVATE
  ${LIDA_ENGINE_SOURCE_DIR}/lida_engine.c
  ${LIDA_ENGINE_SOURCE_DIR}/lida_platform_android.cc
  # Vulkan loader
  ${LIDA_ENGINE_SOURCE_DIR}/lib/volk.c)

target_link_libraries(main SDL2 log freetype)
