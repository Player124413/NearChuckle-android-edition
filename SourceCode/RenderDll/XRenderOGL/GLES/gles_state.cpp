/*=============================================================================
  gles_state.cpp : matrix stacks, enables, texenv, fog, alpha test, legacy gets.
=============================================================================*/
#include "gles_internal.h"
#include <math.h>

SGLESState g_es;
std::map<GLuint, STextureObj> g_esTextures;
std::map<GLuint, SESBuffer> g_esBuffers;

//////////////////////////////////////////////////////////////////////////
// Matrices (column-major, GL layout)
//////////////////////////////////////////////////////////////////////////

static void MatIdentity(float* m)
{
  for (int i = 0; i < 16; i++) m[i] = (i % 5 == 0) ? 1.0f : 0.0f;
}

void GLES_MatrixMultiply(const float* a, const float* b, float* out)
{
  float r[16];
  for (int c = 0; c < 4; c++)
    for (int rr = 0; rr < 4; rr++)
      r[c*4+rr] = a[0*4+rr]*b[c*4+0] + a[1*4+rr]*b[c*4+1] + a[2*4+rr]*b[c*4+2] + a[3*4+rr]*b[c*4+3];
  memcpy(out, r, sizeof(r));
}

bool GLES_MatrixInvert(const float* m, float* out)
{
  float inv[16];
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
  float det = m[0]*inv[0] + m[1]*inv[4] + m[2]*inv[8] + m[3]*inv[12];
  if (det == 0.0f) return false;
  det = 1.0f / det;
  for (int i = 0; i < 16; i++) out[i] = inv[i] * det;
  return true;
}

// GL stores eye planes and clip planes multiplied by the inverse modelview at call time: p' = p * MV^-1.
static void PlaneToEye(const float* plane, float* out)
{
  float inv[16];
  const float* mv = g_es.modelview.m[g_es.modelview.depth];
  if (!GLES_MatrixInvert(mv, inv)) { memcpy(out, plane, 16); return; }
  for (int j = 0; j < 4; j++)
    out[j] = plane[0]*inv[j*4+0] + plane[1]*inv[j*4+1] + plane[2]*inv[j*4+2] + plane[3]*inv[j*4+3];
}

static SMatrixStack& CurStack()
{
  switch (g_es.matrixMode)
  {
    case GL_PROJECTION: return g_es.projection;
    case GL_TEXTURE:    return g_es.texture[g_es.activeUnit];
    default:            return g_es.modelview;
  }
}

static float* CurMat() { SMatrixStack& s = CurStack(); return s.m[s.depth]; }

static void MatrixChanged()
{
  if (g_es.matrixMode == GL_TEXTURE)
  {
    STexUnitState& u = g_es.unit[g_es.activeUnit];
    const float* m = CurMat();
    memcpy(u.texMatrix, m, sizeof(u.texMatrix));
    u.texMatrixIdentity = true;
    for (int i = 0; i < 16 && u.texMatrixIdentity; i++)
      if (m[i] != ((i % 5 == 0) ? 1.0f : 0.0f)) u.texMatrixIdentity = false;
  }
  else
    g_es.mvpDirty = true;
}

static void MultCur(const float* m)
{
  GLES_MatrixMultiply(CurMat(), m, CurMat());
  MatrixChanged();
}

static void __stdcall gles_glMatrixMode(GLenum mode) { g_es.matrixMode = mode; }

static void __stdcall gles_glLoadIdentity() { MatIdentity(CurMat()); MatrixChanged(); }

static void __stdcall gles_glLoadMatrixf(const GLfloat* m) { memcpy(CurMat(), m, 64); MatrixChanged(); }

static void __stdcall gles_glLoadMatrixd(const GLdouble* m)
{
  float* d = CurMat();
  for (int i = 0; i < 16; i++) d[i] = (float)m[i];
  MatrixChanged();
}

static void __stdcall gles_glMultMatrixf(const GLfloat* m) { MultCur(m); }

static void __stdcall gles_glMultMatrixd(const GLdouble* m)
{
  float f[16];
  for (int i = 0; i < 16; i++) f[i] = (float)m[i];
  MultCur(f);
}

static void __stdcall gles_glPushMatrix()
{
  SMatrixStack& s = CurStack();
  if (s.depth >= 31) { GLES_Log("GLES: matrix stack overflow"); return; }
  memcpy(s.m[s.depth+1], s.m[s.depth], 64);
  s.depth++;
}

static void __stdcall gles_glPopMatrix()
{
  SMatrixStack& s = CurStack();
  if (s.depth <= 0) { GLES_Log("GLES: matrix stack underflow"); return; }
  s.depth--;
  MatrixChanged();
}

static void __stdcall gles_glTranslatef(GLfloat x, GLfloat y, GLfloat z)
{
  float m[16]; MatIdentity(m);
  m[12] = x; m[13] = y; m[14] = z;
  MultCur(m);
}

static void __stdcall gles_glTranslated(GLdouble x, GLdouble y, GLdouble z) { gles_glTranslatef((float)x, (float)y, (float)z); }

static void __stdcall gles_glScalef(GLfloat x, GLfloat y, GLfloat z)
{
  float m[16]; MatIdentity(m);
  m[0] = x; m[5] = y; m[10] = z;
  MultCur(m);
}

static void __stdcall gles_glScaled(GLdouble x, GLdouble y, GLdouble z) { gles_glScalef((float)x, (float)y, (float)z); }

static void __stdcall gles_glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
  float len = sqrtf(x*x + y*y + z*z);
  if (len == 0.0f) return;
  x /= len; y /= len; z /= len;
  float a = angle * 3.14159265f / 180.0f, c = cosf(a), s = sinf(a), t = 1.0f - c;
  float m[16] = {
    t*x*x + c,   t*x*y + s*z, t*x*z - s*y, 0,
    t*x*y - s*z, t*y*y + c,   t*y*z + s*x, 0,
    t*x*z + s*y, t*y*z - s*x, t*z*z + c,   0,
    0, 0, 0, 1 };
  MultCur(m);
}

static void __stdcall gles_glRotated(GLdouble a, GLdouble x, GLdouble y, GLdouble z) { gles_glRotatef((float)a, (float)x, (float)y, (float)z); }

static void __stdcall gles_glOrtho(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
  float m[16]; MatIdentity(m);
  m[0] = (float)(2 / (r - l)); m[5] = (float)(2 / (t - b)); m[10] = (float)(-2 / (f - n));
  m[12] = (float)(-(r + l) / (r - l)); m[13] = (float)(-(t + b) / (t - b)); m[14] = (float)(-(f + n) / (f - n));
  MultCur(m);
}

static void __stdcall gles_glFrustum(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f)
{
  float m[16] = {0};
  m[0] = (float)(2*n / (r - l)); m[5] = (float)(2*n / (t - b));
  m[8] = (float)((r + l) / (r - l)); m[9] = (float)((t + b) / (t - b));
  m[10] = (float)(-(f + n) / (f - n)); m[11] = -1.0f;
  m[14] = (float)(-2*f*n / (f - n));
  MultCur(m);
}

const float* GLES_CurrentMVP()
{
  static float mvp[16];
  static bool valid;
  if (g_es.mvpDirty || !valid)
  {
    GLES_MatrixMultiply(g_es.projection.m[g_es.projection.depth], g_es.modelview.m[g_es.modelview.depth], mvp);
    g_es.mvpDirty = false;
    valid = true;
  }
  return mvp;
}

//////////////////////////////////////////////////////////////////////////
// Gets
//////////////////////////////////////////////////////////////////////////

static bool LegacyGetFloat(GLenum pname, GLfloat* params)
{
  switch (pname)
  {
    case GL_MODELVIEW_MATRIX:  memcpy(params, g_es.modelview.m[g_es.modelview.depth], 64); return true;
    case GL_PROJECTION_MATRIX: memcpy(params, g_es.projection.m[g_es.projection.depth], 64); return true;
    case GL_TEXTURE_MATRIX:    memcpy(params, g_es.texture[g_es.activeUnit].m[g_es.texture[g_es.activeUnit].depth], 64); return true;
    case GL_CURRENT_COLOR:     memcpy(params, g_es.color, 16); return true;
    case GL_FOG_COLOR:         memcpy(params, g_es.fogColor, 16); return true;
    case GL_FOG_START:         params[0] = g_es.fogStart; return true;
    case GL_FOG_END:           params[0] = g_es.fogEnd; return true;
    case GL_FOG_DENSITY:       params[0] = g_es.fogDensity; return true;
    case GL_MAX_LIGHTS:                  params[0] = 8; return true;
    case GL_MAX_CLIP_PLANES:             params[0] = 6; return true;
    case GL_MAX_MODELVIEW_STACK_DEPTH:   params[0] = 32; return true;
    case GL_MAX_PROJECTION_STACK_DEPTH:  params[0] = 32; return true;
    case GL_MAX_TEXTURE_STACK_DEPTH:     params[0] = 32; return true;
    case 0x84E2 /* GL_MAX_TEXTURE_UNITS_ARB */:
    case 0x8871 /* GL_MAX_TEXTURE_COORDS_ARB */: params[0] = GLES_MAX_UNITS; return true;
    case GL_RED_BITS: case GL_GREEN_BITS: case GL_BLUE_BITS: case GL_ALPHA_BITS: params[0] = 8; return true;
    case GL_DEPTH_BITS:  params[0] = 24; return true;
    case GL_STENCIL_BITS: params[0] = 8; return true;
    case GL_FOG:         params[0] = g_es.fog ? 1.0f : 0.0f; return true;
    case GL_FOG_MODE:    params[0] = (float)g_es.fogMode; return true;
    case GL_MATRIX_MODE: params[0] = (float)g_es.matrixMode; return true;
    case GL_ACTIVE_TEXTURE_ARB: params[0] = (float)(ES_TEXTURE0 + g_es.activeUnit); return true;
    case 0x864B /* GL_PROGRAM_ERROR_POSITION_ARB */: params[0] = -1; return true;
  }
  return false;
}

static void __stdcall gles_glGetFloatv(GLenum pname, GLfloat* params)
{
  if (LegacyGetFloat(pname, params)) return;
  if (es_glGetFloatv) es_glGetFloatv(pname, params);
}

static void __stdcall gles_glGetDoublev(GLenum pname, GLdouble* params)
{
  float f[16] = {0};
  int n = (pname == GL_MODELVIEW_MATRIX || pname == GL_PROJECTION_MATRIX || pname == GL_TEXTURE_MATRIX) ? 16 : 4;
  gles_glGetFloatv(pname, f);
  for (int i = 0; i < n; i++) params[i] = f[i];
}

static void __stdcall gles_glGetIntegerv(GLenum pname, GLint* params)
{
  float f[16];
  switch (pname)
  {
    case GL_MODELVIEW_MATRIX: case GL_PROJECTION_MATRIX: case GL_TEXTURE_MATRIX:
      gles_glGetFloatv(pname, f);
      for (int i = 0; i < 16; i++) params[i] = (GLint)f[i];
      return;
    case GL_CURRENT_COLOR: case GL_FOG_COLOR:
      gles_glGetFloatv(pname, f);
      for (int i = 0; i < 4; i++) params[i] = (GLint)(f[i] * 2147483647.0f);
      return;
  }
  if (LegacyGetFloat(pname, f)) { params[0] = (GLint)f[0]; return; }
  if (es_glGetIntegerv) es_glGetIntegerv(pname, params);
}

static void __stdcall gles_glGetBooleanv(GLenum pname, GLboolean* params)
{
  GLint i = 0;
  gles_glGetIntegerv(pname, &i);
  params[0] = i ? GL_TRUE : GL_FALSE;
}

static GLenum __stdcall gles_glGetError() { return es_glGetError ? es_glGetError() : GL_NO_ERROR; }

//////////////////////////////////////////////////////////////////////////
// Enables
//////////////////////////////////////////////////////////////////////////

static bool IsNativeCap(GLenum cap)
{
  switch (cap)
  {
    case GL_BLEND: case GL_CULL_FACE: case GL_DEPTH_TEST: case GL_STENCIL_TEST:
    case GL_SCISSOR_TEST: case GL_DITHER: case GL_POLYGON_OFFSET_FILL:
    case 0x809E /* GL_SAMPLE_ALPHA_TO_COVERAGE */: case 0x80A0 /* GL_SAMPLE_COVERAGE */:
      return true;
  }
  return false;
}

static void SetCap(GLenum cap, bool on)
{
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  switch (cap)
  {
    case GL_TEXTURE_1D:            u.enable1D = on; return;
    case GL_TEXTURE_2D:            u.enable2D = on; return;
    case ES_TEXTURE_3D:            u.enable3D = on; return;
    case ES_TEXTURE_CUBE_MAP:      u.enableCube = on; return;
    case ES_TEXTURE_RECTANGLE_NV:  u.enableRect = on; return;
    case GL_TEXTURE_GEN_S: case GL_TEXTURE_GEN_T: case GL_TEXTURE_GEN_R: case GL_TEXTURE_GEN_Q:
      u.texGen[cap - GL_TEXTURE_GEN_S] = on; return;
    case GL_ALPHA_TEST:            g_es.alphaTest = on; return;
    case GL_FOG:                   g_es.fog = on; return;
    case GL_LIGHTING:              g_es.lighting = on; return;
    case GL_LIGHT0: case GL_LIGHT0 + 1: case GL_LIGHT0 + 2: case GL_LIGHT0 + 3: g_es.light[cap - GL_LIGHT0].enabled = on; return;
    case GL_COLOR_MATERIAL:        g_es.colorMaterial = on; return;
    case GL_NORMALIZE:             g_es.normalize = on; return;
    case ES_VERTEX_PROGRAM_ARB:    g_es.vertexProgram = on; return;
    case ES_FRAGMENT_PROGRAM_ARB:  g_es.fragmentProgram = on; return;
    case 0x8910 /* GL_STENCIL_TEST_TWO_SIDE_EXT */: g_es.stencilTwoSide = on; return;
  }
  if (cap >= GL_CLIP_PLANE0 && cap < GL_CLIP_PLANE0 + 6) { g_es.clipPlane[cap - GL_CLIP_PLANE0] = on; return; }
  if (IsNativeCap(cap)) { if (on) es_glEnable(cap); else es_glDisable(cap); return; }
  // Everything else (NV combiners, texture shaders, smoothing, multisample, two-sided stencil for now) has no effect.
}

static void __stdcall gles_glEnable(GLenum cap) { SetCap(cap, true); }
static void __stdcall gles_glDisable(GLenum cap) { SetCap(cap, false); }

// Two-sided stencil (shadow volumes): EXT face selection and the ATI separate calls map onto
// the ES *Separate entry points.
static GLenum StencilFace() { return g_es.stencilTwoSide ? g_es.stencilFace : GL_FRONT_AND_BACK; }
static void __stdcall gles_glActiveStencilFaceEXT(GLenum face) { g_es.stencilFace = face; }
static void __stdcall gles_glStencilFunc(GLenum func, GLint ref, GLuint mask) { es_glStencilFuncSeparate(StencilFace(), func, ref, mask); }
static void __stdcall gles_glStencilOp(GLenum fail, GLenum zfail, GLenum zpass) { es_glStencilOpSeparate(StencilFace(), fail, zfail, zpass); }
static void __stdcall gles_glStencilMask(GLuint mask) { es_glStencilMaskSeparate(StencilFace(), mask); }
static void __stdcall gles_glStencilOpSeparateATI(GLenum face, GLenum fail, GLenum zfail, GLenum zpass) { es_glStencilOpSeparate(face, fail, zfail, zpass); }
static void __stdcall gles_glStencilFuncSeparateATI(GLenum frontFunc, GLenum backFunc, GLint ref, GLuint mask)
{
  es_glStencilFuncSeparate(GL_FRONT, frontFunc, ref, mask);
  es_glStencilFuncSeparate(GL_BACK, backFunc, ref, mask);
}

static GLboolean __stdcall gles_glIsEnabled(GLenum cap)
{
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  switch (cap)
  {
    case GL_TEXTURE_1D: return u.enable1D;
    case GL_TEXTURE_2D: return u.enable2D;
    case ES_TEXTURE_3D: return u.enable3D;
    case ES_TEXTURE_CUBE_MAP: return u.enableCube;
    case ES_TEXTURE_RECTANGLE_NV: return u.enableRect;
    case GL_ALPHA_TEST: return g_es.alphaTest;
    case GL_FOG: return g_es.fog;
    case GL_LIGHTING: return g_es.lighting;
    case ES_VERTEX_PROGRAM_ARB: return g_es.vertexProgram;
    case ES_FRAGMENT_PROGRAM_ARB: return g_es.fragmentProgram;
  }
  if (cap >= GL_TEXTURE_GEN_S && cap <= GL_TEXTURE_GEN_Q) return u.texGen[cap - GL_TEXTURE_GEN_S];
  if (cap >= GL_CLIP_PLANE0 && cap < GL_CLIP_PLANE0 + 6) return g_es.clipPlane[cap - GL_CLIP_PLANE0];
  if (IsNativeCap(cap)) return es_glIsEnabled(cap);
  return GL_FALSE;
}

//////////////////////////////////////////////////////////////////////////
// Texture units, texenv, texgen
//////////////////////////////////////////////////////////////////////////

static void __stdcall gles_glActiveTextureARB(GLenum tex)
{
  int unit = tex - ES_TEXTURE0;
  if (unit < 0 || unit >= GLES_MAX_UNITS) return;
  g_es.activeUnit = unit;
  extern void GLES_NoteActiveUnit(int);
  es_glActiveTexture(tex);
  GLES_NoteActiveUnit(unit);
}

static void __stdcall gles_glClientActiveTextureARB(GLenum tex)
{
  int unit = tex - ES_TEXTURE0;
  if (unit >= 0 && unit < GLES_MAX_UNITS) g_es.clientActiveUnit = unit;
}

static void TexEnvSet(GLenum target, GLenum pname, const float* fparams, GLint iparam)
{
  if (target == GL_TEXTURE_FILTER_CONTROL_EXT) return; // LOD bias: no ES equivalent
  if (target != GL_TEXTURE_ENV) return;
  STexEnv& e = g_es.unit[g_es.activeUnit].env;
  switch (pname)
  {
    case GL_TEXTURE_ENV_MODE:  e.mode = iparam; break;
    case GL_TEXTURE_ENV_COLOR: memcpy(e.color, fparams, 16); break;
    case ES_COMBINE_RGB:       e.combineRGB = iparam; break;
    case ES_COMBINE_ALPHA:     e.combineAlpha = iparam; break;
    case ES_RGB_SCALE:         e.scaleRGB = fparams[0]; break;
    case GL_ALPHA_SCALE:       e.scaleAlpha = fparams[0]; break;
    default:
      if (pname >= ES_SOURCE0_RGB && pname < ES_SOURCE0_RGB + 3)        e.srcRGB[pname - ES_SOURCE0_RGB] = iparam;
      else if (pname >= ES_SOURCE0_ALPHA && pname < ES_SOURCE0_ALPHA + 3) e.srcAlpha[pname - ES_SOURCE0_ALPHA] = iparam;
      else if (pname >= ES_OPERAND0_RGB && pname < ES_OPERAND0_RGB + 3)  e.opRGB[pname - ES_OPERAND0_RGB] = iparam;
      else if (pname >= ES_OPERAND0_ALPHA && pname < ES_OPERAND0_ALPHA + 3) e.opAlpha[pname - ES_OPERAND0_ALPHA] = iparam;
      break;
  }
}

static void __stdcall gles_glTexEnvi(GLenum target, GLenum pname, GLint param)
{
  float f[4] = { (float)param, 0, 0, 0 };
  TexEnvSet(target, pname, f, param);
}
static void __stdcall gles_glTexEnvf(GLenum target, GLenum pname, GLfloat param)
{
  float f[4] = { param, 0, 0, 0 };
  TexEnvSet(target, pname, f, (GLint)param);
}
static void __stdcall gles_glTexEnvfv(GLenum target, GLenum pname, const GLfloat* params) { TexEnvSet(target, pname, params, (GLint)params[0]); }
static void __stdcall gles_glTexEnviv(GLenum target, GLenum pname, const GLint* params)
{
  float f[4] = { (float)params[0], (float)params[1], (float)params[2], (float)params[3] };
  TexEnvSet(target, pname, f, params[0]);
}

static void TexGenSet(GLenum coord, GLenum pname, const float* params)
{
  int c = coord - GL_S;
  if (c < 0 || c > 3) return;
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  switch (pname)
  {
    case GL_TEXTURE_GEN_MODE: u.texGenMode[c] = (GLenum)params[0]; break;
    case GL_OBJECT_PLANE:     memcpy(u.texGenPlaneObj[c], params, 16); break;
    case GL_EYE_PLANE:       PlaneToEye(params, u.texGenPlaneEye[c]); break;
  }
}
static void __stdcall gles_glTexGeni(GLenum coord, GLenum pname, GLint param) { float f[4] = {(float)param,0,0,0}; TexGenSet(coord, pname, f); }
static void __stdcall gles_glTexGenf(GLenum coord, GLenum pname, GLfloat param) { float f[4] = {param,0,0,0}; TexGenSet(coord, pname, f); }
static void __stdcall gles_glTexGenfv(GLenum coord, GLenum pname, const GLfloat* params) { TexGenSet(coord, pname, params); }
static void __stdcall gles_glTexGendv(GLenum coord, GLenum pname, const GLdouble* params)
{
  float f[4] = { (float)params[0], (float)params[1], (float)params[2], (float)params[3] };
  TexGenSet(coord, pname, f);
}

//////////////////////////////////////////////////////////////////////////
// Alpha test, fog, clip planes, and calls without an ES meaning
//////////////////////////////////////////////////////////////////////////

static void __stdcall gles_glAlphaFunc(GLenum func, GLclampf ref) { g_es.alphaFunc = func; g_es.alphaRef = ref; }

static void FogSet(GLenum pname, const float* p)
{
  switch (pname)
  {
    case GL_FOG_MODE:    g_es.fogMode = (GLenum)p[0]; break;
    case GL_FOG_START:   g_es.fogStart = p[0]; break;
    case GL_FOG_END:     g_es.fogEnd = p[0]; break;
    case GL_FOG_DENSITY: g_es.fogDensity = p[0]; break;
    case GL_FOG_COLOR:   memcpy(g_es.fogColor, p, 16); break;
  }
}
static void __stdcall gles_glFogf(GLenum pname, GLfloat param) { float f[4] = {param,0,0,0}; FogSet(pname, f); }
static void __stdcall gles_glFogi(GLenum pname, GLint param) { float f[4] = {(float)param,0,0,0}; FogSet(pname, f); }
static void __stdcall gles_glFogfv(GLenum pname, const GLfloat* params) { FogSet(pname, params); }
static void __stdcall gles_glFogiv(GLenum pname, const GLint* params) { float f[4] = {(float)params[0],0,0,0}; FogSet(pname, f); }

static void __stdcall gles_glClipPlane(GLenum plane, const GLdouble* eq)
{
  int i = plane - GL_CLIP_PLANE0;
  if (i < 0 || i >= 6) return;
  float p[4] = { (float)eq[0], (float)eq[1], (float)eq[2], (float)eq[3] };
  PlaneToEye(p, g_es.clipPlaneEq[i]);
}

static void __stdcall gles_glClearDepth(GLclampd d) { es_glClearDepthf((GLfloat)d); }
static void __stdcall gles_glDepthRange(GLclampd n, GLclampd f) { es_glDepthRangef((GLfloat)n, (GLfloat)f); }

static void __stdcall gles_glPixelStorei(GLenum pname, GLint param)
{
  if (pname == ES_UNPACK_ALIGNMENT) g_es.unpackAlignment = param;
  if (pname == ES_UNPACK_ALIGNMENT || pname == ES_PACK_ALIGNMENT) es_glPixelStorei(pname, param);
}

//////////////////////////////////////////////////////////////////////////
// Lighting state
//////////////////////////////////////////////////////////////////////////

static void LightSet(GLenum light, GLenum pname, const float* p)
{
  int i = light - GL_LIGHT0;
  if (i < 0 || i >= GLES_MAX_LIGHTS) return;
  SLightState& l = g_es.light[i];
  switch (pname)
  {
    case GL_POSITION:
    {
      // Stored in eye space: MV * position
      const float* mv = g_es.modelview.m[g_es.modelview.depth];
      for (int r = 0; r < 4; r++)
        l.position[r] = mv[0*4+r]*p[0] + mv[1*4+r]*p[1] + mv[2*4+r]*p[2] + mv[3*4+r]*p[3];
      break;
    }
    case GL_AMBIENT:  memcpy(l.ambient, p, 16); break;
    case GL_DIFFUSE:  memcpy(l.diffuse, p, 16); break;
    case GL_SPECULAR: memcpy(l.specular, p, 16); break;
    case GL_CONSTANT_ATTENUATION:  l.attenuation[0] = p[0]; break;
    case GL_LINEAR_ATTENUATION:    l.attenuation[1] = p[0]; break;
    case GL_QUADRATIC_ATTENUATION: l.attenuation[2] = p[0]; break;
  }
}
static void __stdcall gles_glLightfv(GLenum light, GLenum pname, const GLfloat* params) { LightSet(light, pname, params); }
static void __stdcall gles_glLightf(GLenum light, GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; LightSet(light, pname, p); }

static void MaterialSet(GLenum pname, const float* p)
{
  SMaterialState& m = g_es.material;
  switch (pname)
  {
    case GL_AMBIENT:  memcpy(m.ambient, p, 16); break;
    case GL_DIFFUSE:  memcpy(m.diffuse, p, 16); break;
    case GL_SPECULAR: memcpy(m.specular, p, 16); break;
    case GL_EMISSION: memcpy(m.emission, p, 16); break;
    case GL_SHININESS: m.shininess = p[0]; break;
    case GL_AMBIENT_AND_DIFFUSE: memcpy(m.ambient, p, 16); memcpy(m.diffuse, p, 16); break;
  }
}
static void __stdcall gles_glMaterialfv(GLenum, GLenum pname, const GLfloat* params) { MaterialSet(pname, params); }
static void __stdcall gles_glMaterialf(GLenum, GLenum pname, GLfloat param) { float p[4] = { param, 0, 0, 0 }; MaterialSet(pname, p); }

static void __stdcall gles_glLightModelfv(GLenum pname, const GLfloat* params) { if (pname == GL_LIGHT_MODEL_AMBIENT) memcpy(g_es.globalAmbient, params, 16); }
static void __stdcall gles_glLightModeli(GLenum, GLint) {}
static void __stdcall gles_glLightModelf(GLenum, GLfloat) {}
static void __stdcall gles_glColorMaterial(GLenum, GLenum mode) { g_es.colorMaterialMode = mode; }

void GLES_ReportTiming();
// glDrawBuffer is called once per frame by the renderer: a convenient frame tick for the timing report.
static void __stdcall gles_glDrawBuffer(GLenum)
{
  static int frames;
  if (++frames % 300 == 0) GLES_ReportTiming();
}

// No effect on ES
static void __stdcall gles_glShadeModel(GLenum) {}
static void __stdcall gles_glHint(GLenum, GLenum) {}
static void __stdcall gles_glReadBuffer(GLenum) {}
static void __stdcall gles_glPolygonMode(GLenum, GLenum) {}
static void __stdcall gles_glPointSize(GLfloat) {}

void GLES_InitState()
{
  memset(&g_es, 0, sizeof(g_es));
  g_es.stencilFace = GL_FRONT;
  g_esTextures.clear();
  GLES_ForgetTextureCache();
  g_esBuffers.clear();
  GLES_ForgetBufferCache();
  g_es.matrixMode = GL_MODELVIEW;
  MatIdentity(g_es.modelview.m[0]);
  MatIdentity(g_es.projection.m[0]);
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    MatIdentity(g_es.texture[i].m[0]);
    STexUnitState& u = g_es.unit[i];
    MatIdentity(u.texMatrix);
    u.texMatrixIdentity = true;
    u.env.mode = GL_MODULATE;
    u.env.combineRGB = GL_MODULATE; u.env.combineAlpha = GL_MODULATE;
    u.env.srcRGB[0] = GL_TEXTURE; u.env.srcRGB[1] = ES_PREVIOUS; u.env.srcRGB[2] = ES_CONSTANT;
    u.env.srcAlpha[0] = GL_TEXTURE; u.env.srcAlpha[1] = ES_PREVIOUS; u.env.srcAlpha[2] = ES_CONSTANT;
    u.env.opRGB[0] = GL_SRC_COLOR; u.env.opRGB[1] = GL_SRC_COLOR; u.env.opRGB[2] = GL_SRC_ALPHA;
    u.env.opAlpha[0] = GL_SRC_ALPHA; u.env.opAlpha[1] = GL_SRC_ALPHA; u.env.opAlpha[2] = GL_SRC_ALPHA;
    u.env.scaleRGB = u.env.scaleAlpha = 1.0f;
    for (int c = 0; c < 4; c++) u.texGenMode[c] = GL_EYE_LINEAR;
    g_es.texcoord[i][3] = 1.0f;
  }
  g_es.color[0] = g_es.color[1] = g_es.color[2] = g_es.color[3] = 1.0f;
  g_es.normal[2] = 1.0f;
  g_es.alphaFunc = GL_ALWAYS;
  g_es.fogMode = GL_EXP; g_es.fogEnd = 1.0f; g_es.fogDensity = 1.0f;
  g_es.colorMaterialMode = GL_AMBIENT_AND_DIFFUSE;
  for (int i = 0; i < GLES_MAX_LIGHTS; i++)
  {
    SLightState& l = g_es.light[i];
    l.position[2] = 1.0f;
    l.attenuation[0] = 1.0f;
    if (i == 0) { l.diffuse[0] = l.diffuse[1] = l.diffuse[2] = l.diffuse[3] = 1.0f; memcpy(l.specular, l.diffuse, 16); }
    l.ambient[3] = 1.0f;
  }
  SMaterialState& m = g_es.material;
  m.ambient[0] = m.ambient[1] = m.ambient[2] = 0.2f; m.ambient[3] = 1.0f;
  m.diffuse[0] = m.diffuse[1] = m.diffuse[2] = 0.8f; m.diffuse[3] = 1.0f;
  m.specular[3] = 1.0f; m.emission[3] = 1.0f;
  g_es.globalAmbient[0] = g_es.globalAmbient[1] = g_es.globalAmbient[2] = 0.2f; g_es.globalAmbient[3] = 1.0f;
  g_es.mvpDirty = true;
  g_es.unpackAlignment = 4;
}

void GLES_RegisterState(std::map<std::string, void*>& t)
{
#define REG(name) t[#name] = (void*)gles_##name;
  REG(glMatrixMode) REG(glLoadIdentity) REG(glLoadMatrixf) REG(glLoadMatrixd) REG(glMultMatrixf) REG(glMultMatrixd)
  REG(glPushMatrix) REG(glPopMatrix) REG(glTranslatef) REG(glTranslated) REG(glScalef) REG(glScaled)
  REG(glRotatef) REG(glRotated) REG(glOrtho) REG(glFrustum)
  REG(glGetFloatv) REG(glGetDoublev) REG(glGetIntegerv) REG(glGetBooleanv) REG(glGetError)
  REG(glEnable) REG(glDisable) REG(glIsEnabled)
  REG(glActiveTextureARB) REG(glClientActiveTextureARB)
  REG(glTexEnvi) REG(glTexEnvf) REG(glTexEnvfv) REG(glTexEnviv)
  REG(glTexGeni) REG(glTexGenf) REG(glTexGenfv) REG(glTexGendv)
  REG(glAlphaFunc) REG(glFogf) REG(glFogi) REG(glFogfv) REG(glFogiv) REG(glClipPlane)
  REG(glClearDepth) REG(glDepthRange) REG(glPixelStorei)
  REG(glShadeModel) REG(glHint) REG(glDrawBuffer) REG(glReadBuffer) REG(glPolygonMode)
  REG(glActiveStencilFaceEXT) REG(glStencilFunc) REG(glStencilOp) REG(glStencilMask) REG(glStencilOpSeparateATI) REG(glStencilFuncSeparateATI)
  REG(glLightModelfv) REG(glLightModeli) REG(glLightModelf) REG(glMaterialfv) REG(glMaterialf)
  REG(glLightfv) REG(glLightf) REG(glColorMaterial) REG(glPointSize)
#undef REG
}
