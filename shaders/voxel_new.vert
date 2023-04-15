#version 450
#extension GL_GOOGLE_include_directive : enable

#include "global.h"

#define ENABLE_DEBUG 1

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
#if ENABLE_DEBUG
// debug data
layout (location = 10) in uint debug_data1;
layout (location = 11) in uint debug_data2; // depth pyramid mip
layout (location = 12) in uint debug_data3;
#endif

// out
layout (location = 0) out vec3 outPosition;
// NOTE: these are same for every 3 vertices
layout (location = 1) flat out vec3 outNormal;
layout (location = 2) flat out vec4 outColor;

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

vec3 colors[16] = {
  vec3(1.0),           // mip 0
  vec3(0.2, 0.2, 1.0), // mip 1
  vec3(0.2, 1.0, 0.2), // mip 2
  vec3(0.2, 1.0, 1.0), // mip 3
  vec3(1.0, 1.0, 0.2), // mip 4
  vec3(1.0, 0.2, 1.0), // mip 5
  vec3(1.0, 0.2, 0.0), // mip 6
  vec3(0.2, 1.0, 0.2), // mip 7
  vec3(0.5, 0.5, 0.5), // mip 8
  vec3(0.2, 0.2, 0.2), // mip 9
  vec3(1.0, 0.5, 0.2), // mip 10
  vec3(0.2, 1.0, 0.7), // mip 11
  vec3(0.2, 0.7, 1.0), // mip 12
  vec3(1.0, 0.9, 0.9), // mip 13
  vec3(0.0, 0.0, 0.0), // mip 14
  vec3(0.9, 0.9, 1.0), // mip 15
};

void main() {
  vec3 pos = doTransform(inPosition, inRotation, inTranslation, inScale);
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
  } else {
    normal = normals[5];
  }
  outNormal = normalize(rotate(normal, inRotation));
#if ENABLE_DEBUG
  // outColor = vec4(vec3(0.1) * debug_data2, 1.0);
  outColor = vec4(colors[debug_data2], 1.0);
#else
  outColor = decompress_color(inColor);
#endif
  outPosition = pos;
  gl_Position = g.camera_projview * vec4(pos, 1.0);
}
