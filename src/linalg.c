#include "linalg.h"

#include <string.h>

float
lida_rqsqrt(float number)
{
  // https://en.wikipedia.org/wiki/Fast_inverse_square_root
  long i;
  float x2;
  float y;
  x2 = number * 0.5F;
  y = number;
  i = *(long*)&y;
  i = 0x5f3759df - (i >> 1);
  y = *(float*)&i;
  y = y * ( 1.5f - (x2*y*y));
  y = y * ( 1.5f - (x2*y*y));
  return y;
}

void
lida_Vec2Normalize(const lida_Vec2* in, lida_Vec2* out)
{
  float inv_len = lida_rqsqrt(LIDA_VEC2_LENGTH(*in));
  out->x = in->x * inv_len;
  out->y = in->y * inv_len;
}

void
lida_Vec3Normalize(const lida_Vec3* in, lida_Vec3* out)
{
  float inv_len = lida_rqsqrt(LIDA_VEC3_LENGTH(*in));
  out->x = in->x * inv_len;
  out->y = in->y * inv_len;
  out->z = in->z * inv_len;
}

void
lida_Vec4Normalize(const lida_Vec4* in, lida_Vec4* out)
{
  float inv_len = lida_rqsqrt(LIDA_VEC4_LENGTH(*in));
  out->x = in->x * inv_len;
  out->y = in->y * inv_len;
  out->z = in->z * inv_len;
  out->w = in->w * inv_len;
}

void
lida_Mat4Add(const lida_Mat4* lhs, const lida_Mat4* rhs, lida_Mat4* out)
{
#define A(i, j) out->m##i##j = lhs->m##i##j + rhs->m##i##j
  A(0, 0); A(0, 1); A(0, 2); A(0, 3);
  A(1, 0); A(1, 1); A(1, 2); A(1, 3);
  A(2, 0); A(2, 1); A(2, 2); A(2, 3);
  A(3, 0); A(3, 1); A(3, 2); A(3, 3);
#undef A
}

void
lida_Mat4Sub(const lida_Mat4* lhs, const lida_Mat4* rhs, lida_Mat4* out)
{
#define A(i, j) out->m##i##j = lhs->m##i##j - rhs->m##i##j
  A(0, 0); A(0, 1); A(0, 2); A(0, 3);
  A(1, 0); A(1, 1); A(1, 2); A(1, 3);
  A(2, 0); A(2, 1); A(2, 2); A(2, 3);
  A(3, 0); A(3, 1); A(3, 2); A(3, 3);
#undef A
}

void
lida_Mat4Mul(const lida_Mat4* lhs, const lida_Mat4* rhs, lida_Mat4* out)
{
  lida_Mat4 temp;
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
  memcpy(out, &temp, sizeof(lida_Mat4));
}

void
lida_Mat4Transpose(const lida_Mat4* in, lida_Mat4* out)
{
#define A(i, j) out->m##i##j = in->m##j##i
  A(0, 0); A(0, 1); A(0, 2); A(0, 3);
  A(1, 0); A(1, 1); A(1, 2); A(1, 3);
  A(2, 0); A(2, 1); A(2, 2); A(2, 3);
  A(3, 0); A(3, 1); A(3, 2); A(3, 3);
#undef A
}

void
lida_Mat4Inverse(const lida_Mat4* in, lida_Mat4* out)
{
  /* float Coef00 = m[2][2] * m[3][3] - m[3][2] * m[2][3]; */
  /* float Coef02 = m[1][2] * m[3][3] - m[3][2] * m[1][3]; */
  /* float Coef03 = m[1][2] * m[2][3] - m[2][2] * m[1][3]; */

  /* float Coef04 = m[2][1] * m[3][3] - m[3][1] * m[2][3]; */
  /* float Coef06 = m[1][1] * m[3][3] - m[3][1] * m[1][3]; */
  /* float Coef07 = m[1][1] * m[2][3] - m[2][1] * m[1][3]; */

  /* float Coef08 = m[2][1] * m[3][2] - m[3][1] * m[2][2]; */
  /* float Coef10 = m[1][1] * m[3][2] - m[3][1] * m[1][2]; */
  /* float Coef11 = m[1][1] * m[2][2] - m[2][1] * m[1][2]; */

  /* float Coef12 = m[2][0] * m[3][3] - m[3][0] * m[2][3]; */
  /* float Coef14 = m[1][0] * m[3][3] - m[3][0] * m[1][3]; */
  /* float Coef15 = m[1][0] * m[2][3] - m[2][0] * m[1][3]; */

  /* float Coef16 = m[2][0] * m[3][2] - m[3][0] * m[2][2]; */
  /* float Coef18 = m[1][0] * m[3][2] - m[3][0] * m[1][2]; */
  /* float Coef19 = m[1][0] * m[2][2] - m[2][0] * m[1][2]; */

  /* float Coef20 = m[2][0] * m[3][1] - m[3][0] * m[2][1]; */
  /* float Coef22 = m[1][0] * m[3][1] - m[3][0] * m[1][1]; */
  /* float Coef23 = m[1][0] * m[2][1] - m[2][0] * m[1][1]; */

  /* vec4 Fac0 = vec4(Coef00, Coef00, Coef02, Coef03); */
  /* vec4 Fac1 = vec4(Coef04, Coef04, Coef06, Coef07); */
  /* vec4 Fac2 = vec4(Coef08, Coef08, Coef10, Coef11); */
  /* vec4 Fac3 = vec4(Coef12, Coef12, Coef14, Coef15); */
  /* vec4 Fac4 = vec4(Coef16, Coef16, Coef18, Coef19); */
  /* vec4 Fac5 = vec4(Coef20, Coef20, Coef22, Coef23); */

  /* vec4 Vec0 = vec4(m[1][0], m[0][0], m[0][0], m[0][0]); */
  /* vec4 Vec1 = vec4(m[1][1], m[0][1], m[0][1], m[0][1]); */
  /* vec4 Vec2 = vec4(m[1][2], m[0][2], m[0][2], m[0][2]); */
  /* vec4 Vec3 = vec4(m[1][3], m[0][3], m[0][3], m[0][3]); */

  /* vec4 Inv0 = vec4(Vec1 * Fac0 - Vec2 * Fac1 + Vec3 * Fac2); */
  /* vec4 Inv1 = vec4(Vec0 * Fac0 - Vec2 * Fac3 + Vec3 * Fac4); */
  /* vec4 Inv2 = vec4(Vec0 * Fac1 - Vec1 * Fac3 + Vec3 * Fac5); */
  /* vec4 Inv3 = vec4(Vec0 * Fac2 - Vec1 * Fac4 + Vec2 * Fac5); */

  /* static immutable SignA = vec4(+1, -1, +1, -1); */
  /* static immutable SignB = vec4(-1, +1, -1, +1); */
  /* mat4 Inverse; */
  /* Inverse[0] = Inv0 * SignA; */
  /* Inverse[1] = Inv1 * SignB; */
  /* Inverse[2] = Inv2 * SignA; */
  /* Inverse[3] = Inv3 * SignB; */

  /* vec4 Row0 = vec4(Inverse[0][0], Inverse[1][0], Inverse[2][0], Inverse[3][0]); */

  /* vec4 Dot0 = vec4(m[0] * Row0); */
  /* float Dot1 = (Dot0.x + Dot0.y) + (Dot0.z + Dot0.w); */

  /* return Inverse * (1.0f / Dot1); */
}

void
lida_TranslationMatrix(lida_Mat4* out, const lida_Vec3* pos)
{
  /* out-> */
}

void
lida_RotationMatrix(const lida_Mat4* in, lida_Mat4* out, float radians, const lida_Vec3* v)
{
  // took from glm
  float c = cos(radians);
  float s = sin(radians);
  lida_Vec3 axis;
  lida_Vec3Normalize(v, &axis);
  lida_Vec3 temp = LIDA_VEC3_MUL(axis, 1-c);
  lida_Mat4 r;
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
  lida_Mat4 tmp;

  tmp.m00 = in->m00 * r.m00 + in->m10 * r.m01 + in->m20 * r.m02;
  tmp.m01 = in->m01 * r.m00 + in->m11 * r.m01 + in->m21 * r.m02;
  tmp.m02 = in->m02 * r.m00 + in->m12 * r.m01 + in->m22 * r.m02;
  tmp.m03 = in->m03 * r.m00 + in->m13 * r.m01 + in->m23 * r.m02;

  tmp.m10 = in->m00 * r.m10 + in->m10 * r.m11 + in->m20 * r.m12;
  tmp.m11 = in->m01 * r.m10 + in->m11 * r.m11 + in->m21 * r.m12;
  tmp.m12 = in->m02 * r.m10 + in->m12 * r.m11 + in->m22 * r.m12;
  tmp.m13 = in->m03 * r.m10 + in->m13 * r.m11 + in->m23 * r.m12;

  tmp.m20 = in->m00 * r.m20 + in->m10 * r.m21 + in->m20 * r.m22;
  tmp.m21 = in->m01 * r.m20 + in->m11 * r.m21 + in->m21 * r.m22;
  tmp.m22 = in->m02 * r.m20 + in->m12 * r.m21 + in->m22 * r.m22;
  tmp.m23 = in->m03 * r.m20 + in->m13 * r.m21 + in->m23 * r.m22;

  tmp.m30 = in->m30;
  tmp.m31 = in->m31;
  tmp.m32 = in->m32;
  tmp.m33 = in->m33;

  memcpy(out, &tmp, sizeof(lida_Mat4));
}

void
lida_OrthographicMatrix(float left, float right, float bottom, float top,
                             float z_near, float z_far, lida_Mat4* out)
{
  memset(out, 0, sizeof(float) * 16);
  out->m00 = 2.0f / (right - left);
  out->m11 = 2.0f / (top - bottom);
  out->m22 = -1.0f / (z_far - z_near);
  out->m30 = -(right + left) / (right - left);
  out->m31 = -(top + bottom) / (top - bottom);
  out->m32 = -z_near / (z_far - z_near);
}

void
lida_ProjectionMatrix(float zoom, float aspect_ratio, float z_near,
                      lida_Mat4* out)
{
  float f = 1.0f / tan(zoom * 0.5f);
  memset(out, 0, sizeof(float) * 16);
  out->m00 = f / aspect_ratio;
  out->m11 = f;
  out->m23 = -1.0f;
  out->m32 = z_near;
  /* return mat4(f / aspect, 0.0f,  0.0f,  0.0f, */
  /*             0.0f,    f,  0.0f,  0.0f, */
  /*             0.0f, 0.0f,  0.0f, -1.0f, */
  /*             0.0f, 0.0f, near,  0.0f); */
}
