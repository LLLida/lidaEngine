#pragma once

#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {

  float x;
  float y;

} lida_Vec2;

typedef struct {

  float x;
  float y;
  float z;

} lida_Vec3;

typedef struct {

  float x;
  float y;
  float z;
  float w;

} lida_Vec4;

typedef struct {

  float m00, m01, m02, m03;
  float m10, m11, m12, m13;
  float m20, m21, m22, m23;
  float m30, m31, m32, m33;

} lida_Mat4;

typedef struct {

  lida_Vec3 position;
  lida_Vec3 up;
  lida_Vec3 rotation;

  float rotation_speed;
  float movement_speed;

  float fovy;
  float aspect_ratio;
  float z_near;
  float z_far;

} lida_Camera;

#define LIDA_VEC2_IDENTITY() (lida_Vec2) { .x = 0.0f, .y = 0.0f }
#define LIDA_VEC2_ADD(l, r) (lida_Vec2) { (l).x + (r).x, (l).y + (r).y }
#define LIDA_VEC2_SUB(l, r) (lida_Vec2) { (l).x - (r).x, (l).y - (r).y }
#define LIDA_VEC2_DOT(l, r) ((l).x * (r).x + (l).y * (r).y)
#define LIDA_VEC2_MUL(l, s) (lida_Vec2) { (l).x * (s), (l).y * (s) }
#define LIDA_VEC2_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y)

#define LIDA_VEC3_IDENTITY() (lida_Vec3) { .x = 0.0f, .y = 0.0f, .z = 0.0f }
#define LIDA_VEC3_ADD(l, r) (lida_Vec3) { (l).x + (r).x, (l).y + (r).y, (l).z + (r).z }
#define LIDA_VEC3_SUB(l, r) (lida_Vec3) { (l).x - (r).x, (l).y - (r).y, (l).z - (r).z }
#define LIDA_VEC3_DOT(l, r) ((l).x * (r).x + (l).y * (r).y + (l).z * (r).z)
#define LIDA_VEC3_MUL(l, s) (lida_Vec3) { (l).x * (s), (l).y * (s), (l).z * (s) }
#define LIDA_VEC3_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y + (v).z*(v).z)
#define LIDA_VEC_CROSS(l, r) (lida_Vec3) { (l).y*(r).z - (l).z*(r).y, (l).z*(r).x - (l).x*(r).z, (l).x*(r).y - (l).y*(r).x }

#define LIDA_VEC4_IDENTITY() (lida_Vec4) { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 0.0f }
#define LIDA_VEC4_ADD(l, r) (lida_Vec4) { (l).x + (r).x, (l).y + (r).y, (l).z + (r).z, (l).w + (r).w }
#define LIDA_VEC4_SUB(l, r) (lida_Vec4) { (l).x - (r).x, (l).y - (r).y, (l).z - (r).z, (l).w - (r).w }
#define LIDA_VEC4_DOT(l, r) (l.x * r.x + l.y * r.y + l.z * r.z + l.w * r.w)
#define LIDA_VEC4_MUL(l, s) (lida_Vec4) { (l).x * (s), (l).y * (s), (l).z * (s), (l).w * (s) }
#define LIDA_VEC4_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y + (v).z*(v).z + (v).w*(v).w)

float lida_rqsqrt(float number);

void lida_Vec2Normalize(const lida_Vec2* in, lida_Vec2* out);
void lida_Vec3Normalize(const lida_Vec3* in, lida_Vec3* out);
void lida_Vec4Normalize(const lida_Vec4* in, lida_Vec4* out);

void lida_Mat4Add(const lida_Mat4* lhs, const lida_Mat4* rhs, lida_Mat4* out);
void lida_Mat4Sub(const lida_Mat4* lhs, const lida_Mat4* rhs, lida_Mat4* out);
void lida_Mat4Mul(const lida_Mat4* lhs, const lida_Mat4* rhs, lida_Mat4* out);
void lida_Mat4Transpose(const lida_Mat4* in, lida_Mat4* out);
void lida_Mat4Inverse(const lida_Mat4* in, lida_Mat4* out);

void lida_TranslationMatrix(lida_Mat4* out, const lida_Vec3* pos);
void lida_RotationMatrix(const lida_Mat4* in, lida_Mat4* out, float radians, const lida_Vec3* axis);

void lida_OrthographicMatrix(float left, float right,
                             float bottom, float top,
                             float z_near, float z_far,
                             lida_Mat4* out);
void lida_ProjectionMatrix(float zoom, float aspect_ratio, float z_near,
                           lida_Mat4* out);

#ifdef __cplusplus
}
#endif
