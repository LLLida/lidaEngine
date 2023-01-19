#version 450
#extension GL_GOOGLE_include_directive : enable

layout (location = 0) in vec3 inPosition;
layout (location = 1) in uint inColor;

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

#define quat vec4

struct Transform {
  quat rotation;
  vec3 position;
  float padding;
};

layout (set = 0, binding = 0) uniform SceneInfo {
  mat4 projview;
  mat4 projection;
  mat4 view;
  mat4 invproj;
} camera;

layout (std140, set = 1, binding = 0) readonly buffer TransformBuffer {
  Transform transforms[];
};

// multiply two quaternions
// note: quatMul(q1, q2) != quatMul(q2, q1) - quaternion multiplication is not commutative
quat quatMul(quat q1, quat q2) {
  quat q;
  q.x = (q1.w * q2.x) + (q1.x * q2.w) + (q1.y * q2.z) - (q1.z * q2.y);
  q.y = (q1.w * q2.y) - (q1.x * q2.z) + (q1.y * q2.w) + (q1.z * q2.x);
  q.z = (q1.w * q2.z) + (q1.x * q2.y) - (q1.y * q2.x) + (q1.z * q2.w);
  q.w = (q1.w * q2.w) - (q1.x * q2.x) - (q1.y * q2.y) - (q1.z * q2.z);
  return q;
}

vec3 rotate(vec3 v, quat q) {
  return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
}

vec3 doTransform(vec3 pos, quat rotation, vec3 translation) {
  return rotate(pos, rotation) + translation;
}

// do transform to pos, picking transform from storage buffer
// note: index is gl_InstanceIndex >> 3
vec3 doTransform(vec3 pos) {
  Transform transform = transforms[gl_InstanceIndex >> 3];
  return doTransform(pos, transform.rotation, transform.position);
}


vec3 rotateNormal(vec3 normal) {
  Transform transform = transforms[gl_InstanceIndex >> 3];
  return normalize(rotate(normal, transform.rotation));
}

out gl_PerVertex {
  vec4 gl_Position;
};

void main() {
  vec3 pos = doTransform(inPosition);
  outNormal = rotateNormal(normals[gl_InstanceIndex & 7]);
  outColor = decompress(inColor);
  vec4 viewSpacePos = camera.view * vec4(pos, 1.0);
  outPosition = viewSpacePos.xyz;
  gl_Position = camera.projview * vec4(pos, 1.0);
}
