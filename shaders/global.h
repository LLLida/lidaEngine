#ifndef GLSL_GLOBAL_H
#define GLSL_GLOBAL_H

#include "common.h"

layout (set = 0, binding = 0) uniform SceneInfo {
  mat4 camera_projview;
  mat4 camera_projection;
  mat4 camera_view;
  mat4 camera_invproj;
  mat4 light_space_matrix;
  vec3 sun_dir;
  float sun_ambient;
  vec3 camera_pos;
} g;

#endif
