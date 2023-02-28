#version 450
#extension GL_GOOGLE_include_directive : enable

#include "global.h"

// vertex
layout (location = 0) in vec3 inPosition;
layout (location = 1) in uint inColor;

// out
layout (location = 0) out vec4 outColor;

void main() {
  gl_Position = g.camera_projview * vec4(inPosition, 1.0);
  outColor = decompress_color(inColor);
}
