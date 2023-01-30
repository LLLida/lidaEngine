#ifndef GLSL_COMMON_H
#define GLSL_COMMON_H

#define quat vec4

struct Transform {
  quat rotation;
  vec3 position;
  float scale;
};

layout (set = 0, binding = 0) uniform SceneInfo {
  mat4 camera_projview;
  mat4 camera_projection;
  mat4 camera_view;
  mat4 camera_invproj;
} g;

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

vec3 doTransform(vec3 pos, quat rotation, vec3 translation, float scale) {
  return rotate(pos * scale, rotation) + translation;
}

// do transform to pos, picking transform from storage buffer
// note: index is gl_InstanceIndex >> 3
vec3 doTransform(vec3 pos) {
  Transform transform = transforms[gl_InstanceIndex >> 3];
  return doTransform(pos, transform.rotation, transform.position, transform.scale);
}


vec3 rotateNormal(vec3 normal) {
  Transform transform = transforms[gl_InstanceIndex >> 3];
  return normalize(rotate(normal, transform.rotation));
}

#endif
