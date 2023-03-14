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

// normal information
layout (location = 5) in uint inCount0;
layout (location = 6) in uint inCount1;
layout (location = 7) in uint inCount2;
layout (location = 8) in uint inCount3;
layout (location = 9) in uint inCount4;

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

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  vec3 pos = doTransform(inPosition, inRotation, inTranslation, inScale);
#if 1
  vec3 normal;
  if (gl_VertexIndex < inCount0) {
    normal = normals[0];
  } else if (gl_VertexIndex < inCount1) {
    normal = normals[1];
  } else if (gl_VertexIndex < inCount2) {
    normal = normals[2];
  } else if (gl_VertexIndex < inCount3) {
    normal = normals[3];
  } else if (gl_VertexIndex < inCount4) {
    normal = normals[4];
  }
  outNormal = normalize(rotate(normal, inRotation));
#else
  if (gl_VertexIndex < inCount0) {
    outNormal = vec3(0.8, 0.0, 0.0);
  } else if (gl_VertexIndex < inCount1) {
    outNormal = vec3(0.0, 0.8, 0.0);
  } else if (gl_VertexIndex < inCount2) {
    outNormal = vec3(0.0, 0.0, 0.8);
  } else if (gl_VertexIndex < inCount3) {
    outNormal = vec3(0.8, 0.8, 0.0);
  } else if (gl_VertexIndex < inCount4) {
    outNormal = vec3(0.0, 0.8, 0.8);
  }
#endif
  outColor = decompress_color(inColor);
  outPosition = pos;
  gl_Position = g.camera_projview * vec4(pos, 1.0);
}
