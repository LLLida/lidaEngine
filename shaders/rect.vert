#version 450
#extension GL_GOOGLE_include_directive : enable

const vec2 positions[4] = {
  vec2(-1.0, -1.0),
  vec2(-1.0, 1.0),
  vec2(1.0, -1.0),
  vec2(1.0, 1.0),
};

layout (location = 0) out vec2 uv;

void main() {
  vec2 pos = positions[gl_VertexIndex];
  uv = (pos + 1.0) * 0.5;
  gl_Position = vec4(pos, 0.0, 1.0);
}
