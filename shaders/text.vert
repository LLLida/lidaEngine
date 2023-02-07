#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec2 pos;
layout (location = 1) in vec2 uv;
layout (location = 2) in vec4 color;

layout (location = 0) out vec2 out_uv;
layout (location = 1) out vec4 out_color;

struct gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  gl_Position = vec4(pos, 0, 1);
  out_uv = uv;
  out_color = color;
}
