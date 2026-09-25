/*=============================================================================
  gles_glu.cpp : the GLU calls the renderer makes, for builds without libGLU.
  Matrix functions go through the crygl* table so the layer's matrix stack sees them.
=============================================================================*/
#include "RenderPCH.h"
#include "../GL_Renderer.h"
#include <math.h>

extern "C" {

void APIENTRY gluPerspective(GLdouble fovy, GLdouble aspect, GLdouble zNear, GLdouble zFar)
{
  GLdouble ymax = zNear * tan(fovy * 3.14159265358979323846 / 360.0);
  GLdouble xmax = ymax * aspect;
  cryglFrustum(-xmax, xmax, -ymax, ymax, zNear, zFar);
}

void APIENTRY gluOrtho2D(GLdouble left, GLdouble right, GLdouble bottom, GLdouble top)
{
  cryglOrtho(left, right, bottom, top, -1.0, 1.0);
}

static void Normalize(float v[3])
{
  float r = sqrtf(v[0]*v[0] + v[1]*v[1] + v[2]*v[2]);
  if (r == 0.0f) return;
  v[0] /= r; v[1] /= r; v[2] /= r;
}

static void Cross(const float a[3], const float b[3], float out[3])
{
  out[0] = a[1]*b[2] - a[2]*b[1];
  out[1] = a[2]*b[0] - a[0]*b[2];
  out[2] = a[0]*b[1] - a[1]*b[0];
}

void APIENTRY gluLookAt(GLdouble eyex, GLdouble eyey, GLdouble eyez,
                        GLdouble centerx, GLdouble centery, GLdouble centerz,
                        GLdouble upx, GLdouble upy, GLdouble upz)
{
  float f[3] = { (float)(centerx - eyex), (float)(centery - eyey), (float)(centerz - eyez) };
  float up[3] = { (float)upx, (float)upy, (float)upz };
  float s[3], u[3];
  Normalize(f);
  Cross(f, up, s);
  Normalize(s);
  Cross(s, f, u);
  float m[16] = {
    s[0], u[0], -f[0], 0,
    s[1], u[1], -f[1], 0,
    s[2], u[2], -f[2], 0,
    0, 0, 0, 1 };
  cryglMultMatrixf(m);
  cryglTranslatef((float)-eyex, (float)-eyey, (float)-eyez);
}

static void MulMatVec(const GLdouble m[16], const GLdouble in[4], GLdouble out[4])
{
  for (int i = 0; i < 4; i++)
    out[i] = in[0]*m[0*4+i] + in[1]*m[1*4+i] + in[2]*m[2*4+i] + in[3]*m[3*4+i];
}

static void MulMat(const GLdouble a[16], const GLdouble b[16], GLdouble r[16])
{
  for (int i = 0; i < 4; i++)
    for (int j = 0; j < 4; j++)
      r[i*4+j] = a[i*4+0]*b[0*4+j] + a[i*4+1]*b[1*4+j] + a[i*4+2]*b[2*4+j] + a[i*4+3]*b[3*4+j];
}

// Gauss-Jordan; the same routine Mesa's GLU uses.
static int InvertMat(const GLdouble m[16], GLdouble out[16])
{
  GLdouble inv[16], det;
  inv[0]  =  m[5]*m[10]*m[15] - m[5]*m[11]*m[14] - m[9]*m[6]*m[15] + m[9]*m[7]*m[14] + m[13]*m[6]*m[11] - m[13]*m[7]*m[10];
  inv[4]  = -m[4]*m[10]*m[15] + m[4]*m[11]*m[14] + m[8]*m[6]*m[15] - m[8]*m[7]*m[14] - m[12]*m[6]*m[11] + m[12]*m[7]*m[10];
  inv[8]  =  m[4]*m[9]*m[15]  - m[4]*m[11]*m[13] - m[8]*m[5]*m[15] + m[8]*m[7]*m[13] + m[12]*m[5]*m[11] - m[12]*m[7]*m[9];
  inv[12] = -m[4]*m[9]*m[14]  + m[4]*m[10]*m[13] + m[8]*m[5]*m[14] - m[8]*m[6]*m[13] - m[12]*m[5]*m[10] + m[12]*m[6]*m[9];
  inv[1]  = -m[1]*m[10]*m[15] + m[1]*m[11]*m[14] + m[9]*m[2]*m[15] - m[9]*m[3]*m[14] - m[13]*m[2]*m[11] + m[13]*m[3]*m[10];
  inv[5]  =  m[0]*m[10]*m[15] - m[0]*m[11]*m[14] - m[8]*m[2]*m[15] + m[8]*m[3]*m[14] + m[12]*m[2]*m[11] - m[12]*m[3]*m[10];
  inv[9]  = -m[0]*m[9]*m[15]  + m[0]*m[11]*m[13] + m[8]*m[1]*m[15] - m[8]*m[3]*m[13] - m[12]*m[1]*m[11] + m[12]*m[3]*m[9];
  inv[13] =  m[0]*m[9]*m[14]  - m[0]*m[10]*m[13] - m[8]*m[1]*m[14] + m[8]*m[2]*m[13] + m[12]*m[1]*m[10] - m[12]*m[2]*m[9];
  inv[2]  =  m[1]*m[6]*m[15]  - m[1]*m[7]*m[14]  - m[5]*m[2]*m[15] + m[5]*m[3]*m[14] + m[13]*m[2]*m[7]  - m[13]*m[3]*m[6];
  inv[6]  = -m[0]*m[6]*m[15]  + m[0]*m[7]*m[14]  + m[4]*m[2]*m[15] - m[4]*m[3]*m[14] - m[12]*m[2]*m[7]  + m[12]*m[3]*m[6];
  inv[10] =  m[0]*m[5]*m[15]  - m[0]*m[7]*m[13]  - m[4]*m[1]*m[15] + m[4]*m[3]*m[13] + m[12]*m[1]*m[7]  - m[12]*m[3]*m[5];
  inv[14] = -m[0]*m[5]*m[14]  + m[0]*m[6]*m[13]  + m[4]*m[1]*m[14] - m[4]*m[2]*m[13] - m[12]*m[1]*m[6]  + m[12]*m[2]*m[5];
  inv[3]  = -m[1]*m[6]*m[11]  + m[1]*m[7]*m[10]  + m[5]*m[2]*m[11] - m[5]*m[3]*m[10] - m[9]*m[2]*m[7]   + m[9]*m[3]*m[6];
  inv[7]  =  m[0]*m[6]*m[11]  - m[0]*m[7]*m[10]  - m[4]*m[2]*m[11] + m[4]*m[3]*m[10] + m[8]*m[2]*m[7]   - m[8]*m[3]*m[6];
  inv[11] = -m[0]*m[5]*m[11]  + m[0]*m[7]*m[9]   + m[4]*m[1]*m[11] - m[4]*m[3]*m[9]  - m[8]*m[1]*m[7]   + m[8]*m[3]*m[5];
  inv[15] =  m[0]*m[5]*m[10]  - m[0]*m[6]*m[9]   - m[4]*m[1]*m[10] + m[4]*m[2]*m[9]  + m[8]*m[1]*m[6]   - m[8]*m[2]*m[5];
  det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  if (det == 0) return 0;
  det = 1.0 / det;
  for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
  return 1;
}

int APIENTRY gluProject(GLdouble objx, GLdouble objy, GLdouble objz,
                        const GLdouble modelMatrix[16], const GLdouble projMatrix[16], const GLint viewport[4],
                        GLdouble* winx, GLdouble* winy, GLdouble* winz)
{
  GLdouble in[4] = { objx, objy, objz, 1.0 }, tmp[4], out[4];
  MulMatVec(modelMatrix, in, tmp);
  MulMatVec(projMatrix, tmp, out);
  if (out[3] == 0.0) return GL_FALSE;
  out[0] /= out[3]; out[1] /= out[3]; out[2] /= out[3];
  *winx = viewport[0] + (1 + out[0]) * viewport[2] / 2;
  *winy = viewport[1] + (1 + out[1]) * viewport[3] / 2;
  *winz = (1 + out[2]) / 2;
  return GL_TRUE;
}

int APIENTRY gluUnProject(GLdouble winx, GLdouble winy, GLdouble winz,
                          const GLdouble modelMatrix[16], const GLdouble projMatrix[16], const GLint viewport[4],
                          GLdouble* objx, GLdouble* objy, GLdouble* objz)
{
  GLdouble final[16], inv[16];
  MulMat(modelMatrix, projMatrix, final);
  if (!InvertMat(final, inv)) return GL_FALSE;
  GLdouble in[4] = {
    (winx - viewport[0]) / viewport[2] * 2 - 1,
    (winy - viewport[1]) / viewport[3] * 2 - 1,
    2 * winz - 1, 1.0 };
  GLdouble out[4];
  MulMatVec(inv, in, out);
  if (out[3] == 0.0) return GL_FALSE;
  *objx = out[0] / out[3]; *objy = out[1] / out[3]; *objz = out[2] / out[3];
  return GL_TRUE;
}

const GLubyte* APIENTRY gluErrorString(GLenum errCode)
{
  static char buf[32];
  sprintf(buf, "GL error 0x%x", errCode);
  return (const GLubyte*)buf;
}

}

// Quadrics are only used for debug spheres; drawn later through the layer's immediate mode.
extern "C" {
struct GLUquadric { int unused; };
GLUquadric* APIENTRY gluNewQuadric(void) { return new GLUquadric; }
void APIENTRY gluDeleteQuadric(GLUquadric* q) { delete q; }
void APIENTRY gluSphere(GLUquadric*, GLdouble, GLint, GLint) {}
}
