/*
  Vector and linear algebra.
 */

typedef struct {

  float x;
  float y;

} Vec2;

typedef struct {

  float x;
  float y;
  float z;

} Vec3;

typedef struct {

  float x;
  float y;
  float z;
  float w;

} Vec4;

// Column-major 4x4 matrix
typedef struct {

  float m00, m10, m20, m30;
  float m01, m11, m21, m31;
  float m02, m12, m22, m32;
  float m03, m13, m23, m33;

} Mat4;

typedef struct {
  float x, y, z, w;
} Quat;

// 32 bytes
typedef struct {
  Quat rotation;
  Vec3 position;
  float scale;
} Transform;

enum {
  CAMERA_PRESSED_FORWARD = (1<<0),
  CAMERA_PRESSED_LEFT = (1<<1),
  CAMERA_PRESSED_RIGHT = (1<<2),
  CAMERA_PRESSED_BACK = (1<<3),
  CAMERA_PRESSED_UP = (1<<4),
  CAMERA_PRESSED_DOWN = (1<<5),
};

typedef struct {

  // note: need to update before access
  Mat4 projection_matrix;
  // note: need to update before access
  Mat4 view_matrix;
  // note: need to update before access
  Vec3 front;

  Vec3 position;
  Vec3 up;
  Vec3 rotation;

  float rotation_speed;
  float movement_speed;

  float fovy;
  float aspect_ratio;
  float z_near;

  uint32_t pressed;

} Camera;

// same as Vec2 but fields are ints
typedef struct {
  int x, y;
} iVec2;

// same as Vec3 but fields are ints
typedef struct {
  int x, y, z;
} iVec3;

// same as Vec4 but fields are ints
typedef struct {
  int x, y, z, w;
} iVec4;

// same as Vec2 but fields are ints
typedef struct {
  unsigned int x, y;
} uVec2;

// same as Vec3 but fields are ints
typedef struct {
  unsigned int x, y, z;
} uVec3;

// same as Vec4 but fields are ints
typedef struct {
  unsigned int x, y, z, w;
} uVec4;

#define RADIANS(degrees) (degrees / 180.0f * 3.141592653589793238f)

#define VEC2_CREATE(x_, y_) (Vec2) { .x = x_, .y = y_ }
#define VEC2_IDENTITY() VEC2_CREATE(0.0f, 0.0f)
#define VEC2_ADD(l, r) (Vec2) { (l).x + (r).x, (l).y + (r).y }
#define VEC2_SUB(l, r) (Vec2) { (l).x - (r).x, (l).y - (r).y }
#define VEC2_DOT(l, r) ((l).x * (r).x + (l).y * (r).y)
#define VEC2_MUL(l, s) (Vec2) { (l).x * (s), (l).y * (s) }
#define VEC2_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y)

#define VEC3_CREATE(x_, y_, z_) (Vec3) { .x = x_, .y = y_, .z = z_ }
#define VEC3_IDENTITY() VEC3_CREATE(0.0f, 0.0f, 0.0f)
#define VEC3_ADD(l, r) (Vec3) { (l).x + (r).x, (l).y + (r).y, (l).z + (r).z }
#define VEC3_SUB(l, r) (Vec3) { (l).x - (r).x, (l).y - (r).y, (l).z - (r).z }
#define VEC3_DOT(l, r) ((l).x * (r).x + (l).y * (r).y + (l).z * (r).z)
#define VEC3_MUL(l, s) (Vec3) { (l).x * (s), (l).y * (s), (l).z * (s) }
#define VEC3_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y + (v).z*(v).z)
#define VEC_CROSS(l, r) (Vec3) { (l).y*(r).z - (l).z*(r).y, (l).z*(r).x - (l).x*(r).z, (l).x*(r).y - (l).y*(r).x }

#define VEC4_CREATE(x_, y_, z_, w_) (Vec4) { .x = x_, .y = y_, .z = z_, .w = w_ }
#define VEC4_IDENTITY() VEC4_CREATE(0.0f, 0.0f, 0.0f, 0.0f)
#define VEC4_ADD(l, r) (Vec4) { (l).x + (r).x, (l).y + (r).y, (l).z + (r).z, (l).w + (r).w }
#define VEC4_SUB(l, r) (Vec4) { (l).x - (r).x, (l).y - (r).y, (l).z - (r).z, (l).w - (r).w }
#define VEC4_DOT(l, r) (l.x * r.x + l.y * r.y + l.z * r.z + l.w * r.w)
#define VEC4_MUL(l, s) (Vec4) { (l).x * (s), (l).y * (s), (l).z * (s), (l).w * (s) }
#define VEC4_LENGTH(v) sqrt((v).x*(v).x + (v).y*(v).y + (v).z*(v).z + (v).w*(v).w)

#define MAT4_IDENTITY() (Mat4) {      \
    1.0f, 0.0f, 0.0f, 0.0f,                     \
      0.0f, 1.0f, 0.0f, 0.0f,                   \
      0.0f, 0.0f, 1.0f, 0.0f,                   \
      0.0f, 0.0f, 0.0f, 1.0f                    \
      }
#define MAT4_ROW(mat, i) (Vec4*)(&mat.m00 + (i) * 4)

#define QUAT_IDENTITY() (Quat) { .x = 0.0f, .y = 0.0f, .z = 0.0f, .w = 1.0f }


/// Functions

// NOTE: 'in' must not be zeros
INTERNAL void
Vec2_Normalize(const Vec2* in, Vec2* out)
{
  float inv_len = 1.0f / sqrt(VEC2_LENGTH(*in));
  out->x = in->x * inv_len;
  out->y = in->y * inv_len;
}

// NOTE: 'in' must not be zeros
INTERNAL void
Vec3_Normalize(const Vec3* in, Vec3* out)
{
  float inv_len = 1.0f / VEC3_LENGTH(*in);
  out->x = in->x * inv_len;
  out->y = in->y * inv_len;
  out->z = in->z * inv_len;
}

// NOTE: 'in' must not be zeros
INTERNAL void
Vec4_Normalize(const Vec4* in, Vec4* out)
{
  float inv_len = 1.0f / sqrt(VEC3_LENGTH(*in));
  out->x = in->x * inv_len;
  out->y = in->y * inv_len;
  out->z = in->z * inv_len;
  out->w = in->w * inv_len;
}

INTERNAL void
Mat4_Add(const Mat4* lhs, const Mat4* rhs, Mat4* out)
{
#define A(i, j) out->m##i##j = lhs->m##i##j + rhs->m##i##j
  A(0, 0); A(0, 1); A(0, 2); A(0, 3);
  A(1, 0); A(1, 1); A(1, 2); A(1, 3);
  A(2, 0); A(2, 1); A(2, 2); A(2, 3);
  A(3, 0); A(3, 1); A(3, 2); A(3, 3);
#undef A
}

INTERNAL void
Mat4_Sub(const Mat4* lhs, const Mat4* rhs, Mat4* out)
{
#define A(i, j) out->m##i##j = lhs->m##i##j - rhs->m##i##j
  A(0, 0); A(0, 1); A(0, 2); A(0, 3);
  A(1, 0); A(1, 1); A(1, 2); A(1, 3);
  A(2, 0); A(2, 1); A(2, 2); A(2, 3);
  A(3, 0); A(3, 1); A(3, 2); A(3, 3);
#undef A
}

INTERNAL void
Mat4_Mul(const Mat4* lhs, const Mat4* rhs, Mat4* out)
{
  Mat4 temp;
  temp.m00 = lhs->m00*rhs->m00 + lhs->m01*rhs->m10 + lhs->m02*rhs->m20 + lhs->m03*rhs->m30;
  temp.m01 = lhs->m00*rhs->m01 + lhs->m01*rhs->m11 + lhs->m02*rhs->m21 + lhs->m03*rhs->m31;
  temp.m02 = lhs->m00*rhs->m02 + lhs->m01*rhs->m12 + lhs->m02*rhs->m22 + lhs->m03*rhs->m32;
  temp.m03 = lhs->m00*rhs->m03 + lhs->m01*rhs->m13 + lhs->m02*rhs->m23 + lhs->m03*rhs->m33;
  temp.m10 = lhs->m10*rhs->m00 + lhs->m11*rhs->m10 + lhs->m12*rhs->m20 + lhs->m13*rhs->m30;
  temp.m11 = lhs->m10*rhs->m01 + lhs->m11*rhs->m11 + lhs->m12*rhs->m21 + lhs->m13*rhs->m31;
  temp.m12 = lhs->m10*rhs->m02 + lhs->m11*rhs->m12 + lhs->m12*rhs->m22 + lhs->m13*rhs->m32;
  temp.m13 = lhs->m10*rhs->m03 + lhs->m11*rhs->m13 + lhs->m12*rhs->m23 + lhs->m13*rhs->m33;
  temp.m20 = lhs->m20*rhs->m00 + lhs->m21*rhs->m10 + lhs->m22*rhs->m20 + lhs->m23*rhs->m30;
  temp.m21 = lhs->m20*rhs->m01 + lhs->m21*rhs->m11 + lhs->m22*rhs->m21 + lhs->m23*rhs->m31;
  temp.m22 = lhs->m20*rhs->m02 + lhs->m21*rhs->m12 + lhs->m22*rhs->m22 + lhs->m23*rhs->m32;
  temp.m23 = lhs->m20*rhs->m03 + lhs->m21*rhs->m13 + lhs->m22*rhs->m23 + lhs->m23*rhs->m33;
  temp.m30 = lhs->m30*rhs->m00 + lhs->m31*rhs->m10 + lhs->m32*rhs->m20 + lhs->m33*rhs->m30;
  temp.m31 = lhs->m30*rhs->m01 + lhs->m31*rhs->m11 + lhs->m32*rhs->m21 + lhs->m33*rhs->m31;
  temp.m32 = lhs->m30*rhs->m02 + lhs->m31*rhs->m12 + lhs->m32*rhs->m22 + lhs->m33*rhs->m32;
  temp.m33 = lhs->m30*rhs->m03 + lhs->m31*rhs->m13 + lhs->m32*rhs->m23 + lhs->m33*rhs->m33;

  memcpy(out, &temp, sizeof(Mat4));
}

INTERNAL void
Mat4Transpose(const Mat4* in, Mat4* out)
{
#define A(i, j) out->m##i##j = in->m##j##i
  A(0, 0); A(0, 1); A(0, 2); A(0, 3);
  A(1, 0); A(1, 1); A(1, 2); A(1, 3);
  A(2, 0); A(2, 1); A(2, 2); A(2, 3);
  A(3, 0); A(3, 1); A(3, 2); A(3, 3);
#undef A
}

// TODO: add matrix inverse function

INTERNAL void
TranslationMatrix(Mat4* out, const Vec3* pos)
{
  *out = MAT4_IDENTITY();
  out->m30 = pos->x;
  out->m31 = pos->y;
  out->m32 = pos->z;
}

INTERNAL void
RotationMatrixAxisAngle(const Mat4* in, Mat4* out, float radians, const Vec3* v)
{
  // took from glm
  float c = cos(radians);
  float s = sin(radians);
  Vec3 axis;
  Vec3_Normalize(v, &axis);
  Vec3 temp = VEC3_MUL(axis, 1-c);
  Mat4 r;
  r.m00 = c + temp.x * axis.x;
  r.m01 = temp.x * axis.y + s * axis.z;
  r.m02 = temp.x * axis.z - s * axis.y;
  r.m10 = temp.y * axis.x - s * axis.z;
  r.m11 = c + temp.y * axis.y;
  r.m12 = temp.y * axis.z + s * axis.x;
  r.m20 = temp.z * axis.x + s * axis.y;
  r.m21 = temp.z * axis.y - s * axis.x;
  r.m22 = c + temp.z * axis.z;

  // 'in' and 'out' may overlap, need to write result to a temporary place
  Mat4 tmp;

  tmp.m00 = in->m00 * r.m00 + in->m01 * r.m10 + in->m02 * r.m20;
  tmp.m10 = in->m10 * r.m00 + in->m11 * r.m10 + in->m12 * r.m20;
  tmp.m20 = in->m20 * r.m00 + in->m21 * r.m10 + in->m22 * r.m20;
  tmp.m30 = in->m30 * r.m00 + in->m31 * r.m10 + in->m32 * r.m20;

  tmp.m01 = in->m00 * r.m01 + in->m01 * r.m11 + in->m02 * r.m21;
  tmp.m11 = in->m10 * r.m01 + in->m11 * r.m11 + in->m12 * r.m21;
  tmp.m21 = in->m20 * r.m01 + in->m21 * r.m11 + in->m22 * r.m21;
  tmp.m31 = in->m30 * r.m01 + in->m31 * r.m11 + in->m32 * r.m21;

  tmp.m02 = in->m00 * r.m02 + in->m01 * r.m12 + in->m02 * r.m22;
  tmp.m12 = in->m10 * r.m02 + in->m11 * r.m12 + in->m12 * r.m22;
  tmp.m22 = in->m20 * r.m02 + in->m21 * r.m12 + in->m22 * r.m22;
  tmp.m32 = in->m30 * r.m02 + in->m31 * r.m12 + in->m32 * r.m22;

  tmp.m03 = in->m03;
  tmp.m13 = in->m13;
  tmp.m23 = in->m23;
  tmp.m33 = in->m33;

  memcpy(out, &tmp, sizeof(Mat4));
}

INTERNAL void
RotationMatrixEulerAngles(Mat4* out, const Vec3* euler_angles)
{
  const Vec3 x_axis = {1.0f, 0.0f, 0.0f},
    y_axis = {0.0f, 1.0f, 0.0f},
    z_axis = {0.0f, 0.0f, 1.0f};
  *out = MAT4_IDENTITY();
  RotationMatrixAxisAngle(out, out, -euler_angles->x, &x_axis);
  RotationMatrixAxisAngle(out, out,  euler_angles->y, &y_axis);
  RotationMatrixAxisAngle(out, out,  euler_angles->z, &z_axis);
}

INTERNAL void
OrthographicMatrix(float left, float right, float bottom, float top,
                        float z_near, float z_far, Mat4* out)
{
  for (size_t i = 0; i < 16; i++)
    ((float*)out)[i] = 0.0f;
  // TODO: current implementation is not correct, write a proper one
  // *out = (Mat4) {
  //   2.0f / (right - left), 0.0f, 0.0f, 0.0f,
  //   0.0f, 2.0f / (top - bottom), 0.0f, 0.0f,
  //   0.0f, 0.0f, -1.0f / (z_far - z_near), 0.0f,
  // };
#if 1
  out->m00 = 2.0f / (right - left);
  out->m11 = -2.0f / (top - bottom);
  out->m22 = -1.0f / (z_far - z_near);
  out->m30 = -(right + left) / (right - left);
  out->m31 = -(top + bottom) / (top - bottom);
  out->m32 = -z_near / (z_far - z_near);
#else
  out->m00 = 2.0f / (right - left);
  out->m11 = 2.0f / (top - bottom);
  out->m22 = -1.0f / (z_far - z_near);
  out->m00 = -(right + left) / (right - left);
  out->m13 = -(top + bottom) / (top - bottom);
  out->m23 = -z_near / (z_far - z_near);
#endif
}

INTERNAL void
PerspectiveMatrix(float fov_y, float aspect_ratio, float z_near, Mat4* out)
{
  // z far is inifinity, depth is one to zero
  // this gives us better precision when working with depth buffer
  float f = 1.0f / tan(fov_y * 0.5f);
  memset(out, 0, sizeof(float) * 16);
  out->m00 = f / aspect_ratio;
  out->m11 = -f;
  out->m32 = -1.0f;
  out->m23 = z_near;
}

INTERNAL void
LookAtMatrix(const Vec3* eye, const Vec3* target, const Vec3* up, Mat4* out)
{
  // calculate look-at matrix
  // https://medium.com/@carmencincotti/lets-look-at-magic-lookat-matrices-c77e53ebdf78
  Vec3 dir = VEC3_SUB(*target, *eye);
  Vec3_Normalize(&dir, &dir);
  Vec3 s = VEC_CROSS(dir, *up);
  Vec3 u = VEC_CROSS(s, dir);
  Vec3 t = {
    VEC3_DOT(*eye, s),
    VEC3_DOT(*eye, u),
    VEC3_DOT(*eye, dir)
  };
  *out = (Mat4) {
    s.x,  u.x,  dir.x, 0.0f,
    s.y,  u.y,  dir.y, 0.0f,
    s.z,  u.z,  dir.z, 0.0f,
    -t.x, -t.y, -t.z,  1.0f
  };
}

INTERNAL void
CameraUpdateProjection(Camera* camera)
{
  PerspectiveMatrix(camera->fovy, camera->aspect_ratio, camera->z_near, &camera->projection_matrix);
}

INTERNAL void
CameraUpdateView(Camera* camera)
{
  // calculate look-at matrix
  // https://medium.com/@carmencincotti/lets-look-at-magic-lookat-matrices-c77e53ebdf78
  Vec3 s = VEC_CROSS(camera->front, camera->up);
  Vec3 u = VEC_CROSS(s, camera->front);
  Vec3 t = {
    VEC3_DOT(camera->position, s),
    VEC3_DOT(camera->position, u),
    VEC3_DOT(camera->position, camera->front)
  };
  camera->view_matrix = (Mat4) {
    s.x,  u.x,  camera->front.x, 0.0f,
    s.y,  u.y,  camera->front.y, 0.0f,
    s.z,  u.z,  camera->front.z, 0.0f,
    -t.x, -t.y, -t.z,            1.0f
  };
}

INTERNAL void
CameraRotate(Camera* camera, float dx, float dy, float dz)
{
  camera->rotation.x += dx * camera->rotation_speed;
  camera->rotation.y += dy * camera->rotation_speed;
  camera->rotation.z += dz * camera->rotation_speed;
}

INTERNAL void
CameraMove(Camera* camera, float dx, float dy, float dz)
{
  camera->position.x += dx * camera->movement_speed;
  camera->position.y += dy * camera->movement_speed;
  camera->position.z += dz * camera->movement_speed;
}

INTERNAL void
CameraPressed(Camera* camera, uint32_t flags)
{
  camera->pressed |= flags;
}

INTERNAL void
CameraUnpressed(Camera* camera, uint32_t flags)
{
  camera->pressed ^= flags;
}

INTERNAL void
CameraUpdate(Camera* camera, float dt, uint32_t window_width, uint32_t window_height)
{
  // euler angles are just spheral coordinates
  camera->front = VEC3_CREATE(cos(camera->rotation.x) * sin(camera->rotation.y),
                              sin(camera->rotation.x),
                              cos(camera->rotation.x) * cos(camera->rotation.y));
  if (camera->pressed & (CAMERA_PRESSED_FORWARD|CAMERA_PRESSED_BACK)) {
    Vec3 plane = VEC3_SUB(VEC3_CREATE(1.0f, 1.0f, 1.0f), camera->up);
    Vec3 vec;
    vec.x = -camera->front.x * plane.x;
    vec.y = -camera->front.y * plane.y;
    vec.z = -camera->front.z * plane.z;
    if (camera->pressed & CAMERA_PRESSED_FORWARD) {
      CameraMove(camera, dt * vec.x, dt * vec.y, dt * vec.z);
    }
    if (camera->pressed & CAMERA_PRESSED_BACK) {
      CameraMove(camera, -dt * vec.x, -dt * vec.y, -dt * vec.z);
    }
  }
  if (camera->pressed & (CAMERA_PRESSED_RIGHT|CAMERA_PRESSED_LEFT)) {
    Vec3 right = VEC_CROSS(camera->front, camera->up);
    if (camera->pressed & CAMERA_PRESSED_RIGHT) {
      CameraMove(camera, dt * right.x, dt * right.y, dt * right.z);
    }
    if (camera->pressed & CAMERA_PRESSED_LEFT) {
      CameraMove(camera, -dt * right.x, -dt * right.y, -dt * right.z);
    }
  }
  if (camera->pressed & CAMERA_PRESSED_UP) {
    CameraMove(camera, dt * camera->up.x, dt * camera->up.y, dt * camera->up.z);
  }
  if (camera->pressed & CAMERA_PRESSED_DOWN) {
    CameraMove(camera, -dt * camera->up.x, -dt * camera->up.y, -dt * camera->up.z);
  }
  camera->aspect_ratio = (float)window_width / (float)window_height;
}
