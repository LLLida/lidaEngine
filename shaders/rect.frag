#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec2 uv;
layout (location = 0) out vec4 outColor;

layout (set = 0, binding = 0) uniform sampler2D in_attachment;

void main() {
  outColor = texture(in_attachment, uv);
}
