#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec2 pos;
layout (location = 1) in uint color;

layout (location = 0) out vec4 out_color;

struct gl_PerVertex {
  vec4 gl_Position;
};

#include "common.h"

void main() {
  gl_Position = vec4(2.0 * pos - 1.0, 0, 1);
  out_color = decompress_color(color);
}
