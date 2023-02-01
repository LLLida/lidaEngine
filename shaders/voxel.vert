#version 450
#extension GL_GOOGLE_include_directive : enable

#include "common.h"

// vertex
layout (location = 0) in vec3 inPosition;
layout (location = 1) in uint inColor;

// transform
layout (location = 2) in quat inRotation;
layout (location = 3) in vec3 inTranslation;
layout (location = 4) in float inScale;

// out
layout (location = 0) out vec3 outPosition;
layout (location = 1) out vec3 outNormal;
layout (location = 2) out vec4 outColor;

vec4 decompress(uint color) {
  // note: this doesn't consider endianness
  uint a = (color >> 24) & 255;
  uint b = (color >> 16) & 255;
  uint g = (color >> 8) & 255;
  uint r = color & 255;
  return vec4(r/255.0, g/255.0, b/255.0, a/255.0);
}

const vec3 normals[] = {
  vec3(-1.0, 0.0, 0.0),
  vec3(1.0, 0.0, 0.0),
  vec3(0.0, -1.0, 0.0),
  vec3(0.0, 1.0, 0.0),
  vec3(0.0, 0.0, -1.0),
  vec3(0.0, 0.0, 1.0),
};

PUSH_CONSTANT Normal_ID {
  uint normal_ID;
};

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  vec3 pos = doTransform(inPosition, inRotation, inTranslation, inScale);
  // TODO: find a way to specify normal index
  outNormal = normalize(rotate(normals[normal_ID], inRotation));
  outColor = decompress(inColor);
  vec4 viewSpacePos = g.camera_view * vec4(pos, 1.0);
  outPosition = viewSpacePos.xyz;
  gl_Position = g.camera_projview * vec4(pos, 1.0);
}
