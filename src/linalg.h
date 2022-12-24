#pragma once

#include <stdint.h>
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

#if 0
  float m00, m01, m02, m03;
  float m10, m11, m12, m13;
  float m20, m21, m22, m23;
  float m30, m31, m32, m33;
#else
  float m00, m10, m20, m30;
  float m01, m11, m21, m31;
  float m02, m12, m22, m32;
  float m03, m13, m23, m33;
#endif

} lida_Mat4;

enum {
  LIDA_CAMERA_PRESSED_FORWARD = (1<<0),
  LIDA_CAMERA_PRESSED_LEFT = (1<<1),
  LIDA_CAMERA_PRESSED_RIGHT = (1<<2),
  LIDA_CAMERA_PRESSED_BACK = (1<<3),
  LIDA_CAMERA_PRESSED_UP = (1<<4),
  LIDA_CAMERA_PRESSED_DOWN = (1<<5),
};

typedef struct {

  // note: need to update before access
  lida_Mat4 projection_matrix;
  // note: need to update before access
  lida_Mat4 view_matrix;
  // note: need to update before access
  lida_Vec3 front;

  lida_Vec3 position;
  lida_Vec3 up;
  lida_Vec3 rotation;

  float rotation_speed;
  float movement_speed;

  float fovy;
  float aspect_ratio;
  float z_near;

  uint32_t pressed;

} lida_Camera;

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
void lida_RotationMatrixAxisAngle(const lida_Mat4* in, lida_Mat4* out,
                                  float radians, const lida_Vec3* axis);
void lida_RotationMatrixEulerAngles(lida_Mat4* out, const lida_Vec3* euler_angles);

void lida_OrthographicMatrix(float left, float right,
                             float bottom, float top,
                             float z_near, float z_far,
                             lida_Mat4* out);
void lida_PerspectiveMatrix(float fov_y, float aspect_ratio, float z_near,
                           lida_Mat4* out);

void lida_CameraUpdateProjection(lida_Camera* camera);
void lida_CameraUpdateView(lida_Camera* camera);
void lida_CameraRotate(lida_Camera* camera, float dx, float dy, float dz);
void lida_CameraMove(lida_Camera* camera, float dx, float dy, float dz);
void lida_CameraPressed(lida_Camera* camera, uint32_t flags);
void lida_CameraUnpressed(lida_Camera* camera, uint32_t flags);
void lida_CameraUpdate(lida_Camera* camera, float dt, uint32_t window_width, uint32_t window_height);

#define LIDA_RADIANS(degrees) (degrees / 180.0f * 3.141592653589793238f)

#define LIDA_VEC2_CREATE(x_, y_) (lida_Vec2) { .x = x_, .y = y_ }
#define LIDA_VEC2_IDENTITY() LIDA_VEC2_CREATE(0.0f, 0.0f)
#define LIDA_VEC2_ADD(l, r) (lida_Vec2) { (l).x + (r).x, (l).y + (r).y }
#define LIDA_VEC2_SUB(l, r) (lida_Vec2) { (l).x - (r).x, (l).y - (r).y }
#define LIDA_VEC2_DOT(l, r) ((l).x * (r).x + (l).y * (r).y)
#define LIDA_VEC2_MUL(l, s) (lida_Vec2) { (l).x * (s), (l).y * (s) }
#define LIDA_VEC2_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y)

#define LIDA_VEC3_CREATE(x_, y_, z_) (lida_Vec3) { .x = x_, .y = y_, .z = z_ }
#define LIDA_VEC3_IDENTITY() LIDA_VEC3_CREATE(0.0f, 0.0f, 0.0f)
#define LIDA_VEC3_ADD(l, r) (lida_Vec3) { (l).x + (r).x, (l).y + (r).y, (l).z + (r).z }
#define LIDA_VEC3_SUB(l, r) (lida_Vec3) { (l).x - (r).x, (l).y - (r).y, (l).z - (r).z }
#define LIDA_VEC3_DOT(l, r) ((l).x * (r).x + (l).y * (r).y + (l).z * (r).z)
#define LIDA_VEC3_MUL(l, s) (lida_Vec3) { (l).x * (s), (l).y * (s), (l).z * (s) }
#define LIDA_VEC3_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y + (v).z*(v).z)
#define LIDA_VEC_CROSS(l, r) (lida_Vec3) { (l).y*(r).z - (l).z*(r).y, (l).z*(r).x - (l).x*(r).z, (l).x*(r).y - (l).y*(r).x }

#define LIDA_VEC4_CREATE(x_, y_, z_, w_) (lida_Vec4) { .x = x_, .y = y_, .z = z_, .w = w_ }
#define LIDA_VEC4_IDENTITY() LIDA_VEC4_CREATE(0.0f, 0.0f, 0.0f)
#define LIDA_VEC4_ADD(l, r) (lida_Vec4) { (l).x + (r).x, (l).y + (r).y, (l).z + (r).z, (l).w + (r).w }
#define LIDA_VEC4_SUB(l, r) (lida_Vec4) { (l).x - (r).x, (l).y - (r).y, (l).z - (r).z, (l).w - (r).w }
#define LIDA_VEC4_DOT(l, r) (l.x * r.x + l.y * r.y + l.z * r.z + l.w * r.w)
#define LIDA_VEC4_MUL(l, s) (lida_Vec4) { (l).x * (s), (l).y * (s), (l).z * (s), (l).w * (s) }
#define LIDA_VEC4_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y + (v).z*(v).z + (v).w*(v).w)

#define LIDA_MAT4_IDENTITY() (lida_Mat4) {      \
    1.0f, 0.0f, 0.0f, 0.0f,                     \
      0.0f, 1.0f, 0.0f, 0.0f,                   \
      0.0f, 0.0f, 1.0f, 0.0f,                   \
      0.0f, 0.0f, 0.0f, 1.0f                    \
      }
#define LIDA_MAT4_ROW(mat, i) (lida_Vec4*)(&mat.m00 + (i) * 4)

#ifdef __cplusplus
}
#endif
