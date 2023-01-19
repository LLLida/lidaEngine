#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec4 outColor;

void main() {
  // validation layers complain if we don't use these variables in fragment shader
  outColor = vec4(inPosition + inNormal, 1.0);
  outColor = vec4(inColor.xyz, 1.0);
}
