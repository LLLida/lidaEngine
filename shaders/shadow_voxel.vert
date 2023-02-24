#version 450
#extension GL_GOOGLE_include_directive : enable

#include "global.h"

// vertex
layout (location = 0) in vec3 inPosition;

// transform
layout (location = 1) in quat inRotation;
layout (location = 2) in vec3 inTranslation;
layout (location = 3) in float inScale;

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  vec3 pos = doTransform(inPosition, inRotation, inTranslation, inScale);
  gl_Position = g.light_space_matrix * vec4(pos, 1.0);
}
