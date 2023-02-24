#version 450
#extension GL_GOOGLE_include_directive : enable

#include "global.h"

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

const vec3 normals[] = {
  vec3(-1.0, 0.0, 0.0),
  vec3(1.0, 0.0, 0.0),
  vec3(0.0, -1.0, 0.0),
  vec3(0.0, 1.0, 0.0),
  vec3(0.0, 0.0, -1.0),
  vec3(0.0, 0.0, 1.0),
};

// TODO: find a better way to specify normal index
PUSH_CONSTANT Normal_ID {
  uint normal_ID;
};

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  vec3 pos = doTransform(inPosition, inRotation, inTranslation, inScale);
  outNormal = normalize(rotate(normals[normal_ID], inRotation));
  outColor = decompress_color(inColor);
  // vec4 viewSpacePos = g.camera_view * vec4(pos, 1.0);
  // outPosition = viewSpacePos.xyz;
  outPosition = pos;
  gl_Position = g.camera_projview * vec4(pos, 1.0);
}
