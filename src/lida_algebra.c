/*
  lida_algebra.c
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
DECLARE_COMPONENT(Transform);

// Oriented Bounding Box
typedef struct {

  Vec3 corners[8];

} OBB;
DECLARE_COMPONENT(OBB);

enum {
  CAMERA_PRESSED_FORWARD = (1<<0),
  CAMERA_PRESSED_LEFT = (1<<1),
  CAMERA_PRESSED_RIGHT = (1<<2),
  CAMERA_PRESSED_BACK = (1<<3),
  CAMERA_PRESSED_UP = (1<<4),
  CAMERA_PRESSED_DOWN = (1<<5),
};

enum {
  CAMERA_TYPE_ORTHO,
  CAMERA_TYPE_PERSP,
};

// TODO: get rid of some fields that some cameras won't use
typedef struct {

  // NOTE: need to update before access
  Mat4 projection_matrix;
  Mat4 view_matrix;
  // projection_matrix * view_matrix
  Mat4 projview_matrix;
  Vec3 front;

  Vec3 position;
  Vec3 up;
  Vec3 rotation;

  float rotation_speed;
  float movement_speed;

  float fovy;
  float aspect_ratio;
  float z_near;

  uint32_t cull_mask;
  uint32_t pressed;
  int type;

} Camera;
DECLARE_COMPONENT(Camera);

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
Mat4_Mul_Vec4(const Mat4* lhs, const Vec4* rhs, Vec4* out)
{
  Vec4 temp;
  temp.x = rhs->x * lhs->m00 + rhs->y * lhs->m01 + rhs->z * lhs->m02 + rhs->w * lhs->m03;
  temp.y = rhs->x * lhs->m10 + rhs->y * lhs->m11 + rhs->z * lhs->m12 + rhs->w * lhs->m13;
  temp.z = rhs->x * lhs->m20 + rhs->y * lhs->m21 + rhs->z * lhs->m22 + rhs->w * lhs->m23;
  temp.w = rhs->x * lhs->m30 + rhs->y * lhs->m31 + rhs->z * lhs->m32 + rhs->w * lhs->m33;
  memcpy(out, &temp, sizeof(Vec4));
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
  memset(out, 0, sizeof(float) * 16);
  // check https://www.youtube.com/watch?v=JiudfB4z1DM&t=411s to see
  // how I got this matrix.  In video they use OpenGL(depth [-1..1])
  // but our depth is [1..0]. Our equation for Z becomes
  // z_{ndc} = \frac{z}{f-n} + \frac{f}{f-n}
  out->m00 = 2.0f / (right - left);
  out->m11 = -2.0f / (top - bottom);
  out->m22 = 1.0f / (z_far - z_near);
  out->m03 = -(right+left) / (right-left);
  out->m13 = -(top+bottom) / (top-bottom);
  out->m23 = z_far / (z_far - z_near);
  out->m33 = 1.0f;
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
RotateByQuat(const Vec3* v, const Quat* q, Vec3* out)
{
  // glsl:
  //  return v + 2.0 * cross(q.xyz, cross(q.xyz, v) + q.w * v);
  // This is the only place where I missed operator overloading.
  Vec3 xyz = { q->x, q->y, q->z };
  Vec3 xyzv = VEC_CROSS(xyz, *v);
  Vec3 vqw = VEC3_MUL(*v, q->w);
  Vec3 xyzvvqw = VEC3_ADD(xyzv, vqw);
  Vec3 xyzxyz = VEC_CROSS(xyz, xyzvvqw);
  Vec3 xyz2 = VEC3_MUL(xyzxyz, 2.0f);
  *out = VEC3_ADD(*v, xyz2);
}

// NOTE: quaternion multiplication is not commutative
INTERNAL void
MultiplyQuats(const Quat* lhs, const Quat* rhs, Quat* out)
{
  Quat temp;
  temp.x = lhs->w * rhs->x + lhs->x * rhs->w + lhs->y * rhs->z - lhs->z * rhs->y;
  temp.y = lhs->w * rhs->y - lhs->x * rhs->z + lhs->y * rhs->w + lhs->z * rhs->x;
  temp.z = lhs->w * rhs->z + lhs->x * rhs->y - lhs->y * rhs->x + lhs->z * rhs->w;
  temp.w = lhs->w * rhs->w - lhs->x * rhs->x - lhs->y * rhs->y - lhs->z * rhs->z;
  memcpy(out, &temp, sizeof(Quat));
}

INTERNAL void
QuatFromEulerAngles(float yaw, float pitch, float roll, Quat* q)
{
  float cy = cosf(yaw * 0.5f);
  float sy = sinf(yaw * 0.5f);
  float cp = cosf(pitch * 0.5f);
  float sp = sinf(pitch * 0.5f);
  float cr = cosf(roll * 0.5f);
  float sr = sinf(roll * 0.5f);
  q->w = cr * cp * cy + sr * sp * sy;
  q->x = sr * cp * cy - cr * sp * sy;
  q->y = cr * sp * cy + sr * cp * sy;
  q->z = cr * cp * sy - sr * sp * cy;
}

INTERNAL void
QuatFromAxisAngle(const Vec3* axis, float angle, Quat* q)
{
  float a = cosf(angle * 0.5f);
  float b = sinf(angle * 0.5f);
  q->w = a;
  q->x = axis->x * b;
  q->y = axis->y * b;
  q->z = axis->z * b;
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

INTERNAL void
GetCenterOBB(const OBB* obb, Vec3* center)
{
  // compute average value between corners
  center->x = (obb->corners[0].x + obb->corners[7].x) / 2.0f;
  center->y = (obb->corners[0].y + obb->corners[7].y) / 2.0f;
  center->z = (obb->corners[0].z + obb->corners[7].z) / 2.0f;
}

INTERNAL void
CalculateObjectOBB(const Vec3* half_size, const Transform* transform, OBB* obb)
{
  Vec3 box[3];
  box[0] = VEC3_CREATE(half_size->x, 0.0f, 0.0f);
  box[1] = VEC3_CREATE(0.0f, half_size->y, 0.0f);
  box[2] = VEC3_CREATE(0.0f, 0.0f, half_size->z);
  RotateByQuat(&box[0], &transform->rotation, &box[0]);
  RotateByQuat(&box[1], &transform->rotation, &box[1]);
  RotateByQuat(&box[2], &transform->rotation, &box[2]);
  const Vec3 muls[8] = {
    { -1.0f, -1.0f, -1.0f },
    { -1.0f, -1.0f, 1.0f },
    { -1.0f, 1.0f, -1.0f },
    { -1.0f, 1.0f, 1.0f },
    { 1.0f, -1.0f, -1.0f },
    { 1.0f, -1.0f, 1.0f },
    { 1.0f, 1.0f, -1.0f },
    { 1.0f, 1.0f, 1.0f },
  };
  for (size_t i = 0; i < 8; i++) {
    Vec3 basis[3];
    basis[0] = VEC3_MUL(box[0], muls[i].x * (transform->scale + 0.01f));
    basis[1] = VEC3_MUL(box[1], muls[i].y * (transform->scale + 0.01f));
    basis[2] = VEC3_MUL(box[2], muls[i].z * (transform->scale + 0.01f));
    obb->corners[i].x = basis[0].x + basis[1].x + basis[2].x + transform->position.x;
    obb->corners[i].y = basis[0].y + basis[1].y + basis[2].y + transform->position.y;
    obb->corners[i].z = basis[0].z + basis[1].z + basis[2].z + transform->position.z;
  }
}

INTERNAL int
TestFrustumOBB(const Mat4* projview, const OBB* obb)
{
  // modified version of
  // https://gamedev.ru/code/articles/FrustumCulling
  Vec4 points[8];
  for (size_t i = 0; i < 8; i++) {
    // transform to clip space
    Vec4 pos = { obb->corners[i].x, obb->corners[i].y, obb->corners[i].z, 1.0f };
    Mat4_Mul_Vec4(projview, &pos, &points[i]);
    // try to early test visible objects. This check is not necessary,
    // in fact if there're many objects that are not visible you may
    // consider to remove this check.
    // NOTE: no upper check for z because we have inifinite z
    if (-points[i].w <= points[i].x && points[i].x <= points[i].w &&
        -points[i].w <= points[i].y && points[i].y <= points[i].w &&
          0.0f <= points[i].z) {
      return 1;
    }
  }
  // TODO: SIMD version of this
  // clip against right plane
  if (points[0].x > points[0].w &&
      points[1].x > points[1].w &&
      points[2].x > points[2].w &&
      points[3].x > points[3].w &&
      points[4].x > points[4].w &&
      points[5].x > points[5].w &&
      points[6].x > points[6].w &&
      points[7].x > points[7].w) {
    return 0;
  }
  // clip against left plane
  if (points[0].x < -points[0].w &&
      points[1].x < -points[1].w &&
      points[2].x < -points[2].w &&
      points[3].x < -points[3].w &&
      points[4].x < -points[4].w &&
      points[5].x < -points[5].w &&
      points[6].x < -points[6].w &&
      points[7].x < -points[7].w) {
    return 0;
  }
  // clip against bottom plane
  if (points[0].y > points[0].w &&
      points[1].y > points[1].w &&
      points[2].y > points[2].w &&
      points[3].y > points[3].w &&
      points[4].y > points[4].w &&
      points[5].y > points[5].w &&
      points[6].y > points[6].w &&
      points[7].y > points[7].w) {
    return 0;
  }
  // clip against top plane
  if (points[0].y < -points[0].w &&
      points[1].y < -points[1].w &&
      points[2].y < -points[2].w &&
      points[3].y < -points[3].w &&
      points[4].y < -points[4].w &&
      points[5].y < -points[5].w &&
      points[6].y < -points[6].w &&
      points[7].y < -points[7].w) {
    return 0;
  }
  // clip against near plane
  if (points[0].z < 0.0f &&
      points[1].z < 0.0f &&
      points[2].z < 0.0f &&
      points[3].z < 0.0f &&
      points[4].z < 0.0f &&
      points[5].z < 0.0f &&
      points[6].z < 0.0f &&
      points[7].z < 0.0f) {
    return 0;
  }
  return 1;
}

INTERNAL int
CheckRayHitOBB(const Vec3* origin, const Vec3* dir, const OBB* obb, float* dist)
{
  Vec3 center;
  GetCenterOBB(obb, &center);

  // FIXME: this isn't correct at all. I just made a sample
  // implementation to get things working. Obviously, when we would
  // need proper mouse picking stuff we would want to implement a
  // proper algorithm using SAT or transforming ray to unitcube space.

  Vec3 q = VEC3_SUB(center, *origin);
  Vec3_Normalize(&q, &q);
  float angle = VEC3_DOT(*dir, q);
  for (int i = 0; i < 8; i++) {
    Vec3 t = VEC3_SUB(obb->corners[i], *origin);
    Vec3_Normalize(&t, &t);
    float p = VEC3_DOT(*dir, t);
    if (angle < p)
      return 0;
  }
  *dist = sqrtf(VEC3_DOT(q, q));
  return 1;
}
