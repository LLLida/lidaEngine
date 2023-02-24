#ifndef GLSL_COMMON_H
#define GLSL_COMMON_H

#define quat vec4

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

#define PUSH_CONSTANT layout(push_constant) uniform

vec4 decompress_color(uint color) {
  // note: this doesn't consider endianness
  uint a = (color >> 24) & 255;
  uint b = (color >> 16) & 255;
  uint g = (color >> 8) & 255;
  uint r = color & 255;
  return vec4(r/255.0, g/255.0, b/255.0, a/255.0);
}

#endif
