#version 450
#extension GL_GOOGLE_include_directive : enable

const vec2 positions[3] = vec2[](vec2(0.0, -0.5),
                                 vec2(0.5, 0.5),
                                 vec2(-0.5, 0.5)
                                 );
// const vec3 colors[3] = vec3[](vec3(1.0, 0.1, 0.1),
//                               vec3(0.1, 1.0, 0.1),
//                               vec3(0.1, 0.1, 1.0));

layout (location = 0) out vec3 outColor;

#include "global.h"

layout (push_constant) uniform Color {
  vec3 red;
  vec3 green;
  vec3 blue;
  vec3 pos;
} colors;

void main() {
  // gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
  gl_Position =  g.camera_projview * vec4(colors.pos + vec3(positions[gl_VertexIndex], 0.0), 1.0);
  // outColor = colors[gl_VertexIndex];
  if (gl_VertexIndex == 0) {
    outColor = colors.red;
  } else if (gl_VertexIndex == 1) {
    outColor = colors.green;
  } else {
    outColor = colors.blue;
  }
}
