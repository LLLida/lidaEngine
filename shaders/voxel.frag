#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 inPosition;
layout (location = 1) in vec3 inNormal;
layout (location = 2) in vec4 inColor;

layout (location = 0) out vec4 outColor;

#include "common.h"

void main() {
  // validation layers complain if we don't use inPosition in fragment shader
  outColor = vec4(inPosition, 1.0);
  
  float diffuse = max(dot(inNormal, g.sun_dir), 0.0);

  vec3 light = (g.sun_ambient + diffuse) * inColor.xyz;
  
  outColor = vec4(light, 1.0);
}
