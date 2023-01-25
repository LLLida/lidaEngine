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

// Column-major 4x4 matrix
typedef struct {

  float m00, m10, m20, m30;
  float m01, m11, m21, m31;
  float m02, m12, m22, m32;
  float m03, m13, m23, m33;

} lida_Mat4;

typedef struct {
  float x, y, z, w;
} lida_Quat;

// 32 bytes
typedef struct {
  lida_Quat rotation;
  lida_Vec3 position;
  float padding;
} lida_Transform;

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

// same as lida_Vec2 but fields are ints
typedef struct {
  int x, y;
} lida_iVec2;

// same as lida_Vec3 but fields are ints
typedef struct {
  int x, y, z;
} lida_iVec3;

// same as lida_Vec4 but fields are ints
typedef struct {
  int x, y, z, w;
} lida_iVec4;

// same as lida_Vec2 but fields are ints
typedef struct {
  unsigned int x, y;
} lida_uVec2;

// same as lida_Vec3 but fields are ints
typedef struct {
  unsigned int x, y, z;
} lida_uVec3;

// same as lida_Vec4 but fields are ints
typedef struct {
  unsigned int x, y, z, w;
} lida_uVec4;

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

#define LIDA_QUAT_IDENTITY() (lida_Quat) { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f }

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus

inline lida_Vec2 operator+(const lida_Vec2& lhs, const lida_Vec2& rhs) {
  return { lhs.x + rhs.x, lhs.y + rhs.y };
}

inline lida_Vec2 operator-(const lida_Vec2& lhs, const lida_Vec2& rhs) {
  return { lhs.x - rhs.x, lhs.y - rhs.y };
}

inline lida_Vec2 operator*(const lida_Vec2& lhs, float num) {
  return { lhs.x * num, lhs.y * num };
}

inline lida_Vec2 operator*(float num, const lida_Vec2& rhs) {
  return { rhs.x * num, rhs.y * num };
}

inline lida_Vec3 operator+(const lida_Vec3& lhs, const lida_Vec3& rhs) {
  return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z };
}

inline lida_Vec3 operator-(const lida_Vec3& lhs, const lida_Vec3& rhs) {
  return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z };
}

inline lida_Vec3 operator*(const lida_Vec3& lhs, float num) {
  return { lhs.x * num, lhs.y * num, lhs.z * num };
}

inline lida_Vec3 operator*(float num, const lida_Vec3& rhs) {
  return { rhs.x * num, rhs.y * num, rhs.z * num };
}

inline lida_Vec4 operator+(const lida_Vec4& lhs, const lida_Vec4& rhs) {
  return { lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w };
}

inline lida_Vec4 operator-(const lida_Vec4& lhs, const lida_Vec4& rhs) {
  return { lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w };
}

inline lida_Vec4 operator*(const lida_Vec4& lhs, float num) {
  return { lhs.x * num, lhs.y * num, lhs.z * num, lhs.w * num };
}

inline lida_Vec4 operator*(float num, const lida_Vec4& rhs) {
  return { rhs.x * num, rhs.y * num, num * rhs.z, num * rhs.w };
}

#endif
