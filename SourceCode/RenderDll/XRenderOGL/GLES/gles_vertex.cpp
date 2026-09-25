/*=============================================================================
  gles_vertex.cpp : current attributes, immediate mode, client arrays, VBOs, draws.
=============================================================================*/
#include "gles_internal.h"
#include "gles_arb.h"
#include <deque>
#include <set>
#include <SDL3/SDL.h>

#define IM_UNITS 4

struct SImVertex
{
  float pos[4];
  float normal[3];
  float color[4];
  float color2[3];
  float tex[IM_UNITS][4];
};

static std::vector<SImVertex> sIm;
static std::vector<GLushort> sIndices;

//////////////////////////////////////////////////////////////////////////
// Current attributes
//////////////////////////////////////////////////////////////////////////

static void SetColor(float r, float g, float b, float a) { g_es.color[0] = r; g_es.color[1] = g; g_es.color[2] = b; g_es.color[3] = a; }

static void __stdcall gles_glColor3f(GLfloat r, GLfloat g, GLfloat b) { SetColor(r, g, b, 1); }
static void __stdcall gles_glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { SetColor(r, g, b, a); }
static void __stdcall gles_glColor3fv(const GLfloat* v) { SetColor(v[0], v[1], v[2], 1); }
static void __stdcall gles_glColor4fv(const GLfloat* v) { SetColor(v[0], v[1], v[2], v[3]); }
static void __stdcall gles_glColor3d(GLdouble r, GLdouble g, GLdouble b) { SetColor((float)r, (float)g, (float)b, 1); }
static void __stdcall gles_glColor4d(GLdouble r, GLdouble g, GLdouble b, GLdouble a) { SetColor((float)r, (float)g, (float)b, (float)a); }
static void __stdcall gles_glColor3ub(GLubyte r, GLubyte g, GLubyte b) { SetColor(r/255.f, g/255.f, b/255.f, 1); }
static void __stdcall gles_glColor4ub(GLubyte r, GLubyte g, GLubyte b, GLubyte a) { SetColor(r/255.f, g/255.f, b/255.f, a/255.f); }
static void __stdcall gles_glColor3ubv(const GLubyte* v) { SetColor(v[0]/255.f, v[1]/255.f, v[2]/255.f, 1); }
static void __stdcall gles_glColor4ubv(const GLubyte* v) { SetColor(v[0]/255.f, v[1]/255.f, v[2]/255.f, v[3]/255.f); }

static void __stdcall gles_glNormal3f(GLfloat x, GLfloat y, GLfloat z) { g_es.normal[0] = x; g_es.normal[1] = y; g_es.normal[2] = z; }
static void __stdcall gles_glNormal3fv(const GLfloat* v) { gles_glNormal3f(v[0], v[1], v[2]); }

static void __stdcall gles_glSecondaryColor3fEXT(GLfloat r, GLfloat g, GLfloat b) { g_es.color2[0] = r; g_es.color2[1] = g; g_es.color2[2] = b; }
static void __stdcall gles_glSecondaryColor3fvEXT(const GLfloat* v) { gles_glSecondaryColor3fEXT(v[0], v[1], v[2]); }
static void __stdcall gles_glSecondaryColor3ubvEXT(const GLubyte* v) { gles_glSecondaryColor3fEXT(v[0]/255.f, v[1]/255.f, v[2]/255.f); }

static void SetTex(int unit, float s, float t, float r, float q)
{
  if (unit < 0 || unit >= GLES_MAX_UNITS) return;
  g_es.texcoord[unit][0] = s; g_es.texcoord[unit][1] = t; g_es.texcoord[unit][2] = r; g_es.texcoord[unit][3] = q;
}

static void __stdcall gles_glTexCoord1f(GLfloat s) { SetTex(0, s, 0, 0, 1); }
static void __stdcall gles_glTexCoord2f(GLfloat s, GLfloat t) { SetTex(0, s, t, 0, 1); }
static void __stdcall gles_glTexCoord3f(GLfloat s, GLfloat t, GLfloat r) { SetTex(0, s, t, r, 1); }
static void __stdcall gles_glTexCoord4f(GLfloat s, GLfloat t, GLfloat r, GLfloat q) { SetTex(0, s, t, r, q); }
static void __stdcall gles_glTexCoord2fv(const GLfloat* v) { SetTex(0, v[0], v[1], 0, 1); }
static void __stdcall gles_glTexCoord3fv(const GLfloat* v) { SetTex(0, v[0], v[1], v[2], 1); }
static void __stdcall gles_glTexCoord4fv(const GLfloat* v) { SetTex(0, v[0], v[1], v[2], v[3]); }
static void __stdcall gles_glTexCoord2d(GLdouble s, GLdouble t) { SetTex(0, (float)s, (float)t, 0, 1); }

static void __stdcall gles_glMultiTexCoord1fARB(GLenum u, GLfloat s) { SetTex(u - ES_TEXTURE0, s, 0, 0, 1); }
static void __stdcall gles_glMultiTexCoord2fARB(GLenum u, GLfloat s, GLfloat t) { SetTex(u - ES_TEXTURE0, s, t, 0, 1); }
static void __stdcall gles_glMultiTexCoord3fARB(GLenum u, GLfloat s, GLfloat t, GLfloat r) { SetTex(u - ES_TEXTURE0, s, t, r, 1); }
static void __stdcall gles_glMultiTexCoord4fARB(GLenum u, GLfloat s, GLfloat t, GLfloat r, GLfloat q) { SetTex(u - ES_TEXTURE0, s, t, r, q); }
static void __stdcall gles_glMultiTexCoord2fvARB(GLenum u, const GLfloat* v) { SetTex(u - ES_TEXTURE0, v[0], v[1], 0, 1); }
static void __stdcall gles_glMultiTexCoord3fvARB(GLenum u, const GLfloat* v) { SetTex(u - ES_TEXTURE0, v[0], v[1], v[2], 1); }
static void __stdcall gles_glMultiTexCoord4fvARB(GLenum u, const GLfloat* v) { SetTex(u - ES_TEXTURE0, v[0], v[1], v[2], v[3]); }

//////////////////////////////////////////////////////////////////////////
// Immediate mode
//////////////////////////////////////////////////////////////////////////

static void __stdcall gles_glBegin(GLenum mode)
{
  g_es.inBegin = true;
  g_es.beginMode = mode;
  sIm.clear();
}

static void Emit(float x, float y, float z, float w)
{
  if (!g_es.inBegin) return;
  SImVertex v;
  v.pos[0] = x; v.pos[1] = y; v.pos[2] = z; v.pos[3] = w;
  memcpy(v.normal, g_es.normal, sizeof(v.normal));
  memcpy(v.color, g_es.color, sizeof(v.color));
  memcpy(v.color2, g_es.color2, sizeof(v.color2));
  for (int i = 0; i < IM_UNITS; i++) memcpy(v.tex[i], g_es.texcoord[i], sizeof(v.tex[i]));
  sIm.push_back(v);
}

static void __stdcall gles_glVertex2f(GLfloat x, GLfloat y) { Emit(x, y, 0, 1); }
static void __stdcall gles_glVertex3f(GLfloat x, GLfloat y, GLfloat z) { Emit(x, y, z, 1); }
static void __stdcall gles_glVertex4f(GLfloat x, GLfloat y, GLfloat z, GLfloat w) { Emit(x, y, z, w); }
static void __stdcall gles_glVertex2fv(const GLfloat* v) { Emit(v[0], v[1], 0, 1); }
static void __stdcall gles_glVertex3fv(const GLfloat* v) { Emit(v[0], v[1], v[2], 1); }
static void __stdcall gles_glVertex4fv(const GLfloat* v) { Emit(v[0], v[1], v[2], v[3]); }
static void __stdcall gles_glVertex2i(GLint x, GLint y) { Emit((float)x, (float)y, 0, 1); }
static void __stdcall gles_glVertex3i(GLint x, GLint y, GLint z) { Emit((float)x, (float)y, (float)z, 1); }
static void __stdcall gles_glVertex2d(GLdouble x, GLdouble y) { Emit((float)x, (float)y, 0, 1); }
static void __stdcall gles_glVertex3d(GLdouble x, GLdouble y, GLdouble z) { Emit((float)x, (float)y, (float)z, 1); }
static void __stdcall gles_glVertex3dv(const GLdouble* v) { Emit((float)v[0], (float)v[1], (float)v[2], 1); }

//////////////////////////////////////////////////////////////////////////
// Attribute state shadow: ANGLE's per-call cost makes redundant pointer/enable/constant calls the
// dominant expense, so only changes are issued.
//////////////////////////////////////////////////////////////////////////

struct SAttribShadow
{
  bool enabled;
  GLuint buffer; GLint size; GLenum type; GLboolean norm; GLsizei stride; const void* ptr;
  bool constValid; float constant[4];
};
static SAttribShadow sAttr[16];
static GLuint sBoundArrayBuffer;

static void NativeBindArrayBuffer(GLuint id)
{
  if (sBoundArrayBuffer == id) return;
  sBoundArrayBuffer = id;
  es_glBindBuffer(ES_ARRAY_BUFFER, id);
}

static void SetAttribArray(int slot, GLuint buffer, GLint size, GLenum type, GLboolean norm, GLsizei stride, const void* ptr)
{
  SAttribShadow& a = sAttr[slot];
  // Client memory (buffer 0) is read at draw time, so the same pointer with new contents is fine.
  if (a.enabled && a.buffer == buffer && a.size == size && a.type == type && a.norm == norm && a.stride == stride && a.ptr == ptr)
    return;
  NativeBindArrayBuffer(buffer);
  if (!a.enabled) { es_glEnableVertexAttribArray(slot); a.enabled = true; }
  es_glVertexAttribPointer(slot, size, type, norm, stride, ptr);
  a.buffer = buffer; a.size = size; a.type = type; a.norm = norm; a.stride = stride; a.ptr = ptr;
}

static void SetAttribConstant(int slot, float x, float y, float z, float w)
{
  SAttribShadow& a = sAttr[slot];
  if (a.enabled) { es_glDisableVertexAttribArray(slot); a.enabled = false; }
  if (a.constValid && a.constant[0] == x && a.constant[1] == y && a.constant[2] == z && a.constant[3] == w) return;
  es_glVertexAttrib4f(slot, x, y, z, w);
  a.constValid = true;
  a.constant[0] = x; a.constant[1] = y; a.constant[2] = z; a.constant[3] = w;
}

// The renderer's own glBindBufferARB goes through the layer too; keep the shadow in step.
void GLES_NoteArrayBufferBound(GLuint id) { sBoundArrayBuffer = id; }

void GLES_SuspendAttribArrays(bool suspend)
{
  for (int i = 0; i < 16; i++)
    if (sAttr[i].enabled) { if (suspend) es_glDisableVertexAttribArray(i); else es_glEnableVertexAttribArray(i); }
}

void GLES_ReissueAttribArrays()
{
  for (int i = 0; i < 16; i++)
  {
    const SAttribShadow& a = sAttr[i];
    if (!a.enabled) { es_glDisableVertexAttribArray(i); continue; }
    es_glBindBuffer(ES_ARRAY_BUFFER, a.buffer);
    es_glVertexAttribPointer(i, a.size, a.type, a.norm, a.stride, a.ptr);
    es_glEnableVertexAttribArray(i);
  }
  es_glBindBuffer(ES_ARRAY_BUFFER, sBoundArrayBuffer);
}

// Immediate-mode vertices stream through an orphaned VBO: client arrays cost a buffer allocation
// per draw on ANGLE. Quads use a static index pattern.
#define IM_VBO_BYTES (2 * 1024 * 1024)
#define IM_MAX_QUADS 16000
static GLuint sImVBO, sImOffset = IM_VBO_BYTES, sQuadIBO;

// Unsynchronized writes into a ring that is orphaned when it wraps: measured on ANGLE at 163 fps against
// 35 fps for glBufferSubData into the same ring and 124 fps for glBufferData per draw. Adreno is the
// other way round: every unsynchronized map of a buffer the GPU still references allocates new GPU
// memory in the kernel, and the menu's UI windows (hundreds of glEnd per frame) fell to 9 fps against
// 40 with glBufferData per draw and 52 with client arrays (Note 20 Ultra). FARCRY_GLES_IMMODE:
// 0 subdata ring, 1 bufferdata per draw, 2 unsynchronized map ring, 3 client arrays, 4 the persistent
// ring shared with map mode 4 (GL_EXT_buffer_storage; falls back to 3 without it).
#define IM_MODE_DEFAULT 2
static int ImMode()
{
  static int mode = getenv("FARCRY_GLES_IMMODE") ? atoi(getenv("FARCRY_GLES_IMMODE")) : IM_MODE_DEFAULT;
  return mode;
}

static GLuint UploadImmediate(const SImVertex* v, int n)
{
  SESTimer timer(GLES_T_IMUPLOAD);
  int mode = ImMode();
  GLuint bytes = n * sizeof(SImVertex);
  if (!sImVBO) es_glGenBuffers(1, &sImVBO);
  NativeBindArrayBuffer(sImVBO);
  if (mode == 1)
  {
    es_glBufferData(ES_ARRAY_BUFFER, bytes, v, ES_STREAM_DRAW);
    return 0;
  }
  if (sImOffset + bytes > IM_VBO_BYTES)
  {
    es_glBufferData(ES_ARRAY_BUFFER, IM_VBO_BYTES, NULL, ES_STREAM_DRAW);  // orphan
    sImOffset = 0;
  }
  GLuint off = sImOffset;
  if (mode == 2)
  {
    void* p = es_glMapBufferRange(ES_ARRAY_BUFFER, off, bytes, ES_MAP_WRITE_BIT | ES_MAP_UNSYNCHRONIZED_BIT | ES_MAP_INVALIDATE_RANGE_BIT);
    if (p) { memcpy(p, v, bytes); es_glUnmapBuffer(ES_ARRAY_BUFFER); }
    else es_glBufferSubData(ES_ARRAY_BUFFER, off, bytes, v);
  }
  else
    es_glBufferSubData(ES_ARRAY_BUFFER, off, bytes, v);
  sImOffset += (bytes + 15) & ~15u;
  return off;
}

static void BindQuadIndices()
{
  if (!sQuadIBO)
  {
    std::vector<GLushort> idx(IM_MAX_QUADS * 6);
    for (int q = 0; q < IM_MAX_QUADS; q++)
    {
      GLushort b = (GLushort)(q * 4);
      idx[q*6+0] = b; idx[q*6+1] = b + 1; idx[q*6+2] = b + 2; idx[q*6+3] = b; idx[q*6+4] = b + 2; idx[q*6+5] = b + 3;
    }
    es_glGenBuffers(1, &sQuadIBO);
    es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, sQuadIBO);
    es_glBufferData(ES_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(GLushort), &idx[0], ES_STATIC_DRAW);
  }
  else
    es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, sQuadIBO);
}

unsigned char* GLES_RingImmediate(size_t bytes, size_t* off, GLuint* vbo);

static void SetAttribArrays(const SImVertex* v, int n)
{
  const GLsizei stride = sizeof(SImVertex);
  uintptr_t base;
  GLuint buffer;
  size_t ringOff = 0;
  unsigned char* ring = ImMode() == 4 ? GLES_RingImmediate(n * sizeof(SImVertex), &ringOff, &buffer) : NULL;
  if (ring)
  {
    memcpy(ring, v, n * sizeof(SImVertex));
    base = ringOff;
  }
  else
  {
    // Mode 3 (and 4 without the ring) reads client memory at draw time; the others stream through the VBO.
    bool client = ImMode() >= 3;
    base = client ? (uintptr_t)v : UploadImmediate(v, n);  // creates sImVBO on first use
    buffer = client ? 0 : sImVBO;
  }
  #define IMOFF(field) ((const void*)(base + offsetof(SImVertex, field)))
  SetAttribArray(GLES_ATTR_POS, buffer, 4, GL_FLOAT, GL_FALSE, stride, IMOFF(pos));
  SetAttribArray(GLES_ATTR_NORMAL, buffer, 3, GL_FLOAT, GL_FALSE, stride, IMOFF(normal));
  SetAttribArray(GLES_ATTR_COLOR, buffer, 4, GL_FLOAT, GL_FALSE, stride, IMOFF(color));
  SetAttribArray(GLES_ATTR_COLOR2, buffer, 3, GL_FLOAT, GL_FALSE, stride, IMOFF(color2));
  SetAttribArray(GLES_ATTR_TEX0 + 0, buffer, 4, GL_FLOAT, GL_FALSE, stride, IMOFF(tex[0]));
  SetAttribArray(GLES_ATTR_TEX0 + 1, buffer, 4, GL_FLOAT, GL_FALSE, stride, IMOFF(tex[1]));
  SetAttribArray(GLES_ATTR_TEX0 + 2, buffer, 4, GL_FLOAT, GL_FALSE, stride, IMOFF(tex[2]));
  SetAttribArray(GLES_ATTR_TEX0 + 3, buffer, 4, GL_FLOAT, GL_FALSE, stride, IMOFF(tex[3]));
  #undef IMOFF
  for (int i = IM_UNITS; i < GLES_MAX_UNITS; i++)
    SetAttribConstant(GLES_ATTR_TEX0 + i, 0, 0, 0, 1);
}

// Quads become an indexed triangle list; the rest map straight onto ES primitives.
static void DrawImmediate()
{
  SESTimer timer(GLES_T_IMMEDIATE);
  int n = (int)sIm.size();
  if (n == 0) return;
  GLenum mode = g_es.beginMode;
  if (!GLES_PrepareDraw()) return;
  SetAttribArrays(&sIm[0], n);
  switch (mode)
  {
    case GL_QUADS:
    {
      int quads = n / 4;
      if (quads > IM_MAX_QUADS) quads = IM_MAX_QUADS;
      BindQuadIndices();
      es_glDrawElements(GL_TRIANGLES, quads * 6, GL_UNSIGNED_SHORT, 0);
      break;
    }
    case GL_QUAD_STRIP: es_glDrawArrays(GL_TRIANGLE_STRIP, 0, n); break;
    case GL_POLYGON:    es_glDrawArrays(GL_TRIANGLE_FAN, 0, n); break;
    case GL_LINE_LOOP:  es_glDrawArrays(GL_LINE_LOOP, 0, n); break;
    default:            es_glDrawArrays(mode, 0, n); break;
  }
}

static void __stdcall gles_glEnd()
{
  if (!g_es.inBegin) return;
  g_es.inBegin = false;
  DrawImmediate();
  sIm.clear();
  static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
  if (debug)
  {
    static int errors;
    GLenum err = es_glGetError();
    if (err && errors++ < 20) GLES_Log("GLES: GL error 0x%x after immediate draw", err);
  }
}

void GLES_FlushImmediate() {}

//////////////////////////////////////////////////////////////////////////
// Client arrays and buffers
//////////////////////////////////////////////////////////////////////////

static SESArray* ArrayFor(GLenum array)
{
  switch (array)
  {
    case GL_VERTEX_ARRAY: return &g_es.vertexArray;
    case GL_NORMAL_ARRAY: return &g_es.normalArray;
    case GL_COLOR_ARRAY:  return &g_es.colorArray;
    case GL_TEXTURE_COORD_ARRAY: return &g_es.texcoordArray[g_es.clientActiveUnit];
    case ES_SECONDARY_COLOR_ARRAY: return &g_es.color2Array;
  }
  return NULL;
}

static void __stdcall gles_glEnableClientState(GLenum array) { SESArray* a = ArrayFor(array); if (a) a->enabled = true; }
static void __stdcall gles_glDisableClientState(GLenum array) { SESArray* a = ArrayFor(array); if (a) a->enabled = false; }

static void SetPointer(SESArray& a, GLint size, GLenum type, GLsizei stride, const void* p)
{
  a.size = size; a.type = type; a.stride = stride; a.pointer = p; a.buffer = g_es.arrayBuffer;
}

static void __stdcall gles_glVertexPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { SetPointer(g_es.vertexArray, size, type, stride, p); }
static void __stdcall gles_glNormalPointer(GLenum type, GLsizei stride, const GLvoid* p) { SetPointer(g_es.normalArray, 3, type, stride, p); }
static void __stdcall gles_glColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { SetPointer(g_es.colorArray, size, type, stride, p); }
static void __stdcall gles_glTexCoordPointer(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { SetPointer(g_es.texcoordArray[g_es.clientActiveUnit], size, type, stride, p); }
static void __stdcall gles_glSecondaryColorPointerEXT(GLint size, GLenum type, GLsizei stride, const GLvoid* p) { SetPointer(g_es.color2Array, size, type, stride, p); }

static void __stdcall gles_glGenBuffersARB(GLsizei n, GLuint* ids) { es_glGenBuffers(n, ids); }
// Direct-mapped cache in front of g_esBuffers: each draw resolves its vertex and index buffers.
static struct { GLuint id; SESBuffer* buf; } sResolveCache[4096];
void GLES_ForgetBufferCache() { memset(sResolveCache, 0, sizeof(sResolveCache)); }

static void __stdcall gles_glDeleteBuffersARB(GLsizei n, const GLuint* ids)
{
  for (GLsizei i = 0; i < n; i++)
  {
    g_esBuffers.erase(ids[i]);
    if (sResolveCache[ids[i] & 4095].id == ids[i]) sResolveCache[ids[i] & 4095].id = 0;
  }
  es_glDeleteBuffers(n, ids);
}
static void __stdcall gles_glBindBufferARB(GLenum target, GLuint id)
{
  if (target == ES_ARRAY_BUFFER) { g_es.arrayBuffer = id; GLES_NoteArrayBufferBound(id); }
  else if (target == ES_ELEMENT_ARRAY_BUFFER) g_es.elementBuffer = id;
  es_glBindBuffer(target, id);
}
// glMapBufferARB(GL_WRITE_ONLY) is the engine's per-frame update path for dynamic meshes (particles,
// sprites, skinned characters, muzzle flashes). A synchronized map of a buffer the GPU may still be
// reading makes Adreno allocate a fresh backing store in the kernel (the same kgsl alloc the
// immediate-mode ring hit): a fifth of a frame while firing a machine gun. FARCRY_GLES_MAPMODE:
// 0 real map, 1 CPU copy + glBufferData at unmap, 2 CPU copy + glBufferSubData, 3 CPU copy drawn as
// client arrays (no GPU copy; the driver then copies whole streams per draw, worse), 4 CPU copy
// pushed into a persistently mapped ring (GL_EXT_buffer_storage) that draws bind instead of the
// engine's buffer: no driver allocation at all. The light passes re-fill their streams per lit
// object per light, which is what made firing at the ground drop to 18 fps in mode 1; mode 4 is
// the Android default where the extension exists, mode 1 the fallback.
static int MapMode()
{
  static int mode = getenv("FARCRY_GLES_MAPMODE") ? atoi(getenv("FARCRY_GLES_MAPMODE")) : 0;
  return mode;
}

//////////////////////////////////////////////////////////////////////////
// Mode 4: persistent rings. Frames are fenced at the swap so a wrap never overwrites data the GPU
// may still read; a frame that needs more than the ring falls back to mode 1 for the remainder.
//////////////////////////////////////////////////////////////////////////
typedef void (*PFN_esBufferStorage)(GLenum, GLsizeiptrARB, const void*, GLbitfield);
typedef void* (*PFN_esFenceSync)(GLenum, GLbitfield);
typedef GLenum (*PFN_esClientWaitSync)(void*, GLbitfield, unsigned long long);
typedef void (*PFN_esDeleteSync)(void*);
static PFN_esBufferStorage esBufferStorage;
static PFN_esFenceSync esFenceSync;
static PFN_esClientWaitSync esClientWaitSync;
static PFN_esDeleteSync esDeleteSync;

struct SRingFrame { void* fence; size_t start, end; };
struct SRingBack { GLuint id; size_t off; unsigned lap; }; // a whole buffer living in the ring
struct SRing
{
  GLenum target;
  GLuint vbo;
  unsigned char* base;
  size_t size, pos, frameStart;
  std::deque<SRingFrame> frames;
  int state; // 0 untried, 1 ready, -1 unavailable
  unsigned lap;
  std::deque<SRingBack> backs; // in ring order, so the front is the next to be overwritten
};
static SRing sRings[2] = { { ES_ARRAY_BUFFER, 0, NULL, 32u << 20, 0, 0, std::deque<SRingFrame>(), 0 }, { ES_ELEMENT_ARRAY_BUFFER, 0, NULL, 8u << 20, 0, 0, std::deque<SRingFrame>(), 0 } };

static unsigned sRingPushes, sRingDirect, sRingSettles; static size_t sRingBytes;

static bool RingReady(SRing& r)
{
  if (r.state) return r.state > 0;
  r.state = -1;
  if (!GLES_HasNativeExt("GL_EXT_buffer_storage")) { GLES_Log("GLES: no GL_EXT_buffer_storage, map mode 4 falls back to 1"); return false; }
  esBufferStorage = (PFN_esBufferStorage)SDL_GL_GetProcAddress("glBufferStorageEXT");
  esFenceSync = (PFN_esFenceSync)SDL_GL_GetProcAddress("glFenceSync");
  esClientWaitSync = (PFN_esClientWaitSync)SDL_GL_GetProcAddress("glClientWaitSync");
  esDeleteSync = (PFN_esDeleteSync)SDL_GL_GetProcAddress("glDeleteSync");
  if (!esBufferStorage || !esFenceSync || !esClientWaitSync || !esDeleteSync) { GLES_Log("GLES: buffer storage entry points missing, map mode 4 falls back to 1"); return false; }
  GLint prev = 0;
  es_glGetIntegerv(r.target == ES_ARRAY_BUFFER ? 0x8894 : 0x8895, &prev);
  es_glGenBuffers(1, &r.vbo);
  es_glBindBuffer(r.target, r.vbo);
  const GLbitfield flags = ES_MAP_WRITE_BIT | 0x0040 /* MAP_PERSISTENT */ | 0x0080 /* MAP_COHERENT */;
  esBufferStorage(r.target, (GLsizeiptrARB)r.size, NULL, flags);
  r.base = (unsigned char*)es_glMapBufferRange(r.target, 0, (GLsizeiptrARB)r.size, flags);
  es_glBindBuffer(r.target, (GLuint)prev);
  if (!r.base) { GLES_Log("GLES: persistent map failed (error 0x%x), map mode 4 falls back to 1", es_glGetError()); es_glDeleteBuffers(1, &r.vbo); r.vbo = 0; return false; }
  r.state = 1;
  GLES_Log("GLES: map mode 4: %u MB persistent %s ring", (unsigned)(r.size >> 20), r.target == ES_ARRAY_BUFFER ? "array" : "element");
  return true;
}

// The frame's writes so far, fenced; called on a wrap and at the swap.
static void RingCloseFrame(SRing& r)
{
  if (r.pos == r.frameStart || !r.state || r.state < 0) { r.frameStart = r.pos; return; }
  SRingFrame f = { esFenceSync(0x9117 /* SYNC_GPU_COMMANDS_COMPLETE */, 0), r.frameStart, r.pos };
  r.frames.push_back(f);
  r.frameStart = r.pos;
}

void GLES_RingEndFrame()
{
  for (int i = 0; i < 2; i++) if (sRings[i].state > 0) RingCloseFrame(sRings[i]);
  static int frames; static unsigned pushes, direct; static size_t bytes;
  static unsigned settles;
  pushes += sRingPushes; direct += sRingDirect; settles += sRingSettles; bytes += sRingBytes; sRingPushes = sRingDirect = sRingSettles = 0; sRingBytes = 0;
  if (g_esDebug && ++frames % 300 == 0)
  {
    GLES_Log("GLES: ring: %.1f pushes/frame (%.1f direct), %.0f KB/frame, %u settled", pushes / 300.0, direct / 300.0, bytes / 300.0 / 1024.0, settles);
    pushes = direct = settles = 0; bytes = 0;
  }
}

// A buffer still backed by a ring region about to be reused (a mesh written once, drawn for many
// frames) gets those bytes as its own storage; glBufferData copies them before the ring is written.
static void RingSettle(SRing& r, const SRingBack& k)
{
  std::map<GLuint, SESBuffer>::iterator it = g_esBuffers.find(k.id);
  if (it == g_esBuffers.end()) return;
  SESBuffer& b = it->second;
  if (!b.ringValid || &sRings[b.ringIndex] != &r || b.ringOffset != k.off || b.ringLap != k.lap) return;
  es_glBindBuffer(0x8F37 /* COPY_WRITE_BUFFER */, k.id);
  es_glBufferData(0x8F37, b.size, r.base + k.off, b.usage ? b.usage : ES_DYNAMIC_DRAW);
  es_glBindBuffer(0x8F37, 0);
  b.ringValid = false;
  sRingSettles++;
}

// Returns the ring offset for `bytes`, or (size_t)-1 when the ring cannot take it.
static size_t RingAlloc(SRing& r, size_t bytes)
{
  if (!RingReady(r)) return (size_t)-1;
  bytes = (bytes + 15) & ~(size_t)15;
  if (bytes > r.size / 4) return (size_t)-1;
  if (r.pos + bytes > r.size) { RingCloseFrame(r); r.pos = 0; r.frameStart = 0; r.lap++; }
  while (!r.backs.empty())
  {
    const SRingBack& k = r.backs.front();
    if (k.lap == r.lap || (k.lap + 1 == r.lap && k.off >= r.pos + bytes)) break;
    RingSettle(r, k);
    r.backs.pop_front();
  }
  // This frame's own earlier writes are never re-used before a wrap, so only older frames can overlap.
  while (!r.frames.empty())
  {
    SRingFrame& f = r.frames.front();
    bool overlap = f.start < r.pos + bytes && r.pos < f.end;
    // Frames are chronological; everything the write may run into is at or beyond the front.
    if (!overlap && !(f.end <= r.pos && r.frames.size() > 1)) break;
    if (overlap && f.fence) esClientWaitSync(f.fence, 1 /* SYNC_FLUSH_COMMANDS_BIT */, 1000000000ull);
    if (f.fence) esDeleteSync(f.fence);
    r.frames.pop_front();
  }
  size_t off = r.pos;
  r.pos += bytes;
  return off;
}

// Mode 4 draw-time redirection: an array or index pointer into a ring-backed buffer binds the ring.
static void ResolveRing(GLuint& buffer, const void*& ptr)
{
  if (!buffer || MapMode() != 4) return;
  unsigned slot = buffer & 4095;
  if (sResolveCache[slot].id != buffer)
  {
    std::map<GLuint, SESBuffer>::iterator it = g_esBuffers.find(buffer);
    if (it == g_esBuffers.end()) return;
    sResolveCache[slot].id = buffer; sResolveCache[slot].buf = &it->second;
  }
  SESBuffer& b = *sResolveCache[slot].buf;
  if (!b.ranges.empty())
  {
    size_t off = (uintptr_t)ptr;
    for (size_t i = b.ranges.size(); i-- > 0;)
      if (off >= b.ranges[i].start && off < b.ranges[i].end)
      {
        ptr = (const void*)(uintptr_t)(b.ranges[i].ringOff + (off - b.ranges[i].start));
        buffer = sRings[b.ringIndex].vbo;
        return;
      }
    return;
  }
  if (!b.ringValid) return;
  ptr = (const unsigned char*)ptr + b.ringOffset;
  buffer = sRings[b.ringIndex].vbo;
}

static SESBuffer& BoundBuffer(GLenum target);

// Immediate mode 4: a slice of the array ring for one glBegin/glEnd batch.
unsigned char* GLES_RingImmediate(size_t bytes, size_t* off, GLuint* vbo)
{
  size_t r = RingAlloc(sRings[0], bytes);
  if (r == (size_t)-1) return NULL;
  *off = r; *vbo = sRings[0].vbo;
  return sRings[0].base + r;
}

// Slices (ranges) are not settled when the ring wraps, so only STREAM buffers, re-specified per use
// (the engine's per-frame vertex and index streams), are sliced; a DYNAMIC buffer lives there whole.
static bool RingUsage(const SESBuffer& b) { return b.usage == ES_STREAM_DRAW; }
static bool RingWholeUsage(const SESBuffer& b) { return b.usage == ES_STREAM_DRAW || b.usage == ES_DYNAMIC_DRAW; }
static GLuint BoundBufferId(GLenum target) { return target == ES_ELEMENT_ARRAY_BUFFER ? g_es.elementBuffer : g_es.arrayBuffer; }

// A slice of a dynamic buffer that the engine appends into: a fresh ring region, recorded for ResolveRing.
// An offset below the last slice's end means the engine wrapped, so the older slices are finished with.
static void* RingSlice(SESBuffer& b, GLenum target, size_t offset, size_t size)
{
  int ri = (target == ES_ELEMENT_ARRAY_BUFFER) ? 1 : 0;
  size_t r = RingAlloc(sRings[ri], size);
  if (r == (size_t)-1) return NULL;
  if (offset < b.lastEnd || b.ringIndex != ri) b.ranges.clear();
  b.ringValid = false; b.ringIndex = ri; b.lastEnd = offset + size;
  SESBuffer::SRange rg = { offset, offset + size, r };
  b.ranges.push_back(rg);
  sRingPushes++; sRingBytes += size;
  return sRings[ri].base + r;
}

// Engine entry (GL_Renderer.h VB_Lock): map only [offset, offset+size) of the bound buffer for writing.
// NULL means "use glMapBufferARB instead" (mode 4 off or the ring is unavailable).
void* GLES_MapBufferRange(GLenum target, size_t offset, size_t size)
{
  if (MapMode() != 4 || !size) return NULL;
  SESBuffer& b = BoundBuffer(target);
  if (!b.size || offset + size > b.size || !RingUsage(b)) return NULL;
  void* p = RingSlice(b, target, offset, size);
  if (p) b.rangeMapped = true;
  return p;
}

static bool RingPush(SESBuffer& b, GLenum target)
{
  if (!RingWholeUsage(b)) return false;
  int ri = (target == ES_ELEMENT_ARRAY_BUFFER) ? 1 : 0;
  // Taken first: the allocation may settle this very buffer, and may land on its old region (hence memmove).
  const unsigned char* old = b.ringValid ? sRings[b.ringIndex].base + b.ringOffset : NULL;
  size_t off = RingAlloc(sRings[ri], b.size);
  if (off == (size_t)-1) { b.ringValid = false; return false; }
  b.ranges.clear(); b.lastEnd = 0;
  if (b.shadow.size() == b.size) memcpy(sRings[ri].base + off, &b.shadow[0], b.size);
  else if (old) memmove(sRings[ri].base + off, old, b.size); // direct buffer: carry the old contents
  b.ringValid = true; b.ringIndex = ri; b.ringOffset = off; b.ringLap = sRings[ri].lap;
  SRingBack k = { BoundBufferId(target), off, sRings[ri].lap };
  sRings[ri].backs.push_back(k);
  sRingPushes++; sRingBytes += b.size;
  return true;
}

// Stream buffers are written straight into a fresh ring region: no CPU copy at all. The engine
// re-fills such buffers whole, so nothing is lost by not seeding them.
static void* RingMapDirect(SESBuffer& b, GLenum target)
{
  if (!RingWholeUsage(b)) return NULL;
  int ri = (target == ES_ELEMENT_ARRAY_BUFFER) ? 1 : 0;
  size_t off = RingAlloc(sRings[ri], b.size);
  if (off == (size_t)-1) return NULL;
  b.ringValid = true; b.ringIndex = ri; b.ringOffset = off; b.ringLap = sRings[ri].lap; b.direct = true;
  b.ranges.clear(); b.lastEnd = 0;
  SRingBack k = { BoundBufferId(target), off, sRings[ri].lap };
  sRings[ri].backs.push_back(k);
  sRingPushes++; sRingDirect++; sRingBytes += b.size;
  return sRings[ri].base + off;
}
static SESBuffer& BoundBuffer(GLenum target) { return g_esBuffers[(target == ES_ARRAY_BUFFER) ? g_es.arrayBuffer : g_es.elementBuffer]; }

static void __stdcall gles_glBufferDataARB(GLenum target, GLsizeiptrARB size, const GLvoid* data, GLenum usage)
{
  SESBuffer& b = BoundBuffer(target);
  b.size = (GLuint)size;
  b.usage = usage;
  b.ringValid = false; b.direct = false; b.rangeMapped = false;
  b.ranges.clear(); b.lastEnd = 0;
  if (!b.shadow.empty())
  {
    b.shadow.assign((size_t)size, 0);
    if (data) memcpy(&b.shadow[0], data, (size_t)size);
  }
  es_glBufferData(target, size, data, usage);
}
static void __stdcall gles_glBufferSubDataARB(GLenum target, GLintptrARB offset, GLsizeiptrARB size, const GLvoid* data)
{
  SESBuffer& b = BoundBuffer(target);
  // Mode 4: sub-updates of stream buffers (the engine's appended index stream) become ring slices.
  if (MapMode() == 4 && data && size > 0 && b.shadow.empty() && !b.ringValid && RingUsage(b) && offset >= 0 && (size_t)(offset + size) <= b.size)
  {
    void* p = RingSlice(b, target, (size_t)offset, (size_t)size);
    if (p) { memcpy(p, data, (size_t)size); return; }
  }
  if (!b.shadow.empty() && offset >= 0 && (size_t)(offset + size) <= b.shadow.size()) memcpy(&b.shadow[offset], data, (size_t)size);
  if (b.ringValid)
  {
    // A sub-update of a ring-backed buffer moves it to a fresh region (the old one may be in flight).
    if (RingPush(b, target))
    {
      if (b.shadow.empty() && offset >= 0 && (size_t)(offset + size) <= b.size) memcpy(sRings[b.ringIndex].base + b.ringOffset + offset, data, (size_t)size);
      return;
    }
  }
  es_glBufferSubData(target, offset, size, data);
}
static void* __stdcall gles_glMapBufferARB(GLenum target, GLenum access)
{
  SESTimer timer(GLES_T_MAP);
  SESBuffer& b = BoundBuffer(target);
  if (!b.size) return NULL;
  if (MapMode() == 0 || access != GL_WRITE_ONLY_ARB)
    return es_glMapBufferRange(target, 0, b.size, ES_MAP_WRITE_BIT | (access == GL_WRITE_ONLY_ARB ? 0 : ES_MAP_READ_BIT));
  if (MapMode() == 4)
  {
    void* p = RingMapDirect(b, target);
    if (p) { b.mapped = target; return p; }
  }
  if (b.shadow.size() != b.size)
  {
    // First map: seed the copy from the GPU so a partial write keeps the rest of the buffer.
    b.shadow.assign(b.size, 0);
    if (void* p = es_glMapBufferRange(target, 0, b.size, ES_MAP_READ_BIT))
    {
      memcpy(&b.shadow[0], p, b.size);
      es_glUnmapBuffer(target);
    }
  }
  b.mapped = target;
  return &b.shadow[0];
}
static GLboolean __stdcall gles_glUnmapBufferARB(GLenum target)
{
  SESBuffer& b = BoundBuffer(target);
  if (b.rangeMapped) { b.rangeMapped = false; return GL_TRUE; } // coherent ring: nothing to flush
  if (!b.mapped) return es_glUnmapBuffer(target);
  b.mapped = 0;
  if (b.direct) { b.direct = false; return GL_TRUE; }
  if (MapMode() == 3) return GL_TRUE;
  if (MapMode() == 4 && RingPush(b, target)) return GL_TRUE;
  if (MapMode() == 2) es_glBufferSubData(target, 0, b.size, &b.shadow[0]);
  else es_glBufferData(target, b.size, &b.shadow[0], b.usage ? b.usage : ES_DYNAMIC_DRAW);
  return GL_TRUE;
}

// Mode 3: an array or index pointer into a buffer with a CPU copy becomes a client pointer into that copy.
static void ResolveClient(GLuint& buffer, const void*& ptr)
{
  if (MapMode() == 4) { ResolveRing(buffer, ptr); return; }
  if (!buffer || MapMode() != 3) return;
  std::map<GLuint, SESBuffer>::iterator it = g_esBuffers.find(buffer);
  if (it == g_esBuffers.end() || it->second.shadow.empty()) return;
  ptr = &it->second.shadow[0] + (uintptr_t)ptr;
  buffer = 0;
}
static void SetClientArray(int slot, const SESArray& a, GLint size, GLboolean norm)
{
  GLuint buffer = a.buffer;
  const void* ptr = a.pointer;
  ResolveClient(buffer, ptr);
  SetAttribArray(slot, buffer, size, a.type, norm, a.stride, ptr);
}

static void __stdcall gles_glLockArraysEXT(GLint, GLsizei) {}
static void __stdcall gles_glUnlockArraysEXT() {}

static bool ArrayActive(const SESArray& a) { return a.enabled && (a.pointer != NULL || a.buffer != 0); }

static bool SetupClientArrays()
{
  if (!ArrayActive(g_es.vertexArray)) return false;
  SetClientArray(GLES_ATTR_POS, g_es.vertexArray, g_es.vertexArray.size, GL_FALSE);
  if (ArrayActive(g_es.normalArray)) SetClientArray(GLES_ATTR_NORMAL, g_es.normalArray, 3, GL_TRUE);
  else SetAttribConstant(GLES_ATTR_NORMAL, g_es.normal[0], g_es.normal[1], g_es.normal[2], 0);
  if (ArrayActive(g_es.colorArray)) SetClientArray(GLES_ATTR_COLOR, g_es.colorArray, g_es.colorArray.size, GL_TRUE);
  else SetAttribConstant(GLES_ATTR_COLOR, g_es.color[0], g_es.color[1], g_es.color[2], g_es.color[3]);
  if (ArrayActive(g_es.color2Array)) SetClientArray(GLES_ATTR_COLOR2, g_es.color2Array, g_es.color2Array.size, GL_TRUE);
  else SetAttribConstant(GLES_ATTR_COLOR2, g_es.color2[0], g_es.color2[1], g_es.color2[2], 0);
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    const SESArray& t = g_es.texcoordArray[i];
    if (ArrayActive(t)) SetClientArray(GLES_ATTR_TEX0 + i, t, t.size, GL_FALSE);
    else SetAttribConstant(GLES_ATTR_TEX0 + i, g_es.texcoord[i][0], g_es.texcoord[i][1], g_es.texcoord[i][2], g_es.texcoord[i][3]);
  }
  NativeBindArrayBuffer(g_es.arrayBuffer);
  return true;
}

static std::vector<GLuint> sQuadIdx;

// Expand quad indices to triangles. Index data may live in the element buffer (then it is an offset) or in memory.
static const void* ExpandQuads(GLsizei count, GLenum type, const void* indices, GLsizei& outCount, GLenum& outType)
{
  sQuadIdx.clear();
  const GLubyte* p8 = (const GLubyte*)indices;
  if (g_es.elementBuffer)
  {
    GLES_Log("GLES: GL_QUADS from an element buffer is not supported yet");
    outCount = 0;
    return NULL;
  }
  for (GLsizei q = 0; q + 3 < count; q += 4)
  {
    GLuint v[4];
    for (int k = 0; k < 4; k++)
      v[k] = (type == GL_UNSIGNED_SHORT) ? ((const GLushort*)p8)[q+k] : (type == GL_UNSIGNED_INT) ? ((const GLuint*)p8)[q+k] : p8[q+k];
    sQuadIdx.push_back(v[0]); sQuadIdx.push_back(v[1]); sQuadIdx.push_back(v[2]);
    sQuadIdx.push_back(v[0]); sQuadIdx.push_back(v[2]); sQuadIdx.push_back(v[3]);
  }
  outCount = (GLsizei)sQuadIdx.size();
  outType = GL_UNSIGNED_INT;
  return sQuadIdx.empty() ? NULL : &sQuadIdx[0];
}

// FARCRY_GLES_DUMPDRAW (re-read each swap, so the dev hook can switch it on for a frame or two): one line
// per draw with the state that decides what the fixed-function path samples.
bool g_esDumpDraw;
static void DumpDrawState(const char* what, GLsizei count, GLenum indexType = 0, const void* indices = NULL)
{
  GLint blend = 0, src = 0, dst = 0, prog = 0;
  es_glGetIntegerv(0x0BE2 /* GL_BLEND */, &blend);
  es_glGetIntegerv(0x80C9 /* GL_BLEND_SRC_RGB */, &src);
  es_glGetIntegerv(0x80C8 /* GL_BLEND_DST_RGB */, &dst);
  es_glGetIntegerv(0x8B8D /* GL_CURRENT_PROGRAM */, &prog);
  std::string units;
  char buf[256];
  for (int i = 0; i < 4; i++)
  {
    const STexUnitState& u = g_es.unit[i];
    const SESArray& t = g_es.texcoordArray[i];
    if (!u.enable2D && !t.enabled) continue;
    std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(u.bound2D);
    sprintf(buf, " u%d[2d %d tex %u %dx%d fmt 0x%x lv %d env 0x%x/%x src %x,%x mat %s tc %d:%dx0x%x st%d buf%u p%p]", i, u.enable2D, u.bound2D,
      it != g_esTextures.end() ? it->second.width : -1, it != g_esTextures.end() ? it->second.height : -1,
      it != g_esTextures.end() ? it->second.internalFormat : 0, it != g_esTextures.end() ? it->second.levels : -1,
      u.env.mode, u.env.combineRGB, u.env.srcRGB[0], u.env.srcRGB[1], u.texMatrixIdentity ? "I" : "M",
      t.enabled, t.size, t.type, t.stride, t.buffer, t.pointer);
    units += buf;
  }
  const char* GLES_CurrentProgramDesc();
  const char* GLES_ARB_EnvDesc();
  GLES_Log("GLES: draw %s n=%d prog %d (%s) blend %d %x/%x color %.2f,%.2f,%.2f,%.2f vtx %d:%dx0x%x buf%u col %d%s",
    what, count, prog, GLES_CurrentProgramDesc(), blend, src, dst, g_es.color[0], g_es.color[1], g_es.color[2], g_es.color[3],
    g_es.vertexArray.enabled, g_es.vertexArray.size, g_es.vertexArray.type, g_es.vertexArray.buffer, g_es.colorArray.enabled, units.c_str());
  if (prog) GLES_Log("GLES:   fog %d mode 0x%x %.1f..%.1f col %.2f,%.2f,%.2f env%s", g_es.fog, g_es.fogMode, g_es.fogStart, g_es.fogEnd,
    g_es.fogColor[0], g_es.fogColor[1], g_es.fogColor[2], GLES_ARB_EnvDesc());
  {
    // Each ARB program's source, the first time a dumped draw uses it.
    static std::set<const void*> shown;
    for (int v = 0; v < 2; v++)
    {
      SARBProgram* p = GLES_ARB_Bound(v == 0);
      if (p && shown.insert(p).second) GLES_Log("GLES:   %s program %u source:\n%s", v == 0 ? "vertex" : "fragment", p->id, p->source.c_str());
    }
  }
  if (prog)
  {
    // What unit 0 really samples: the native binding and two texels read back through a scratch framebuffer.
    GLint nat = 0, drawFbo = 0, readFbo = 0;
    int GLES_NativeActiveUnit();
    es_glActiveTexture(ES_TEXTURE0); es_glGetIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &nat); es_glActiveTexture(ES_TEXTURE0 + GLES_NativeActiveUnit());
    es_glGetIntegerv(0x8CA6 /* DRAW_FRAMEBUFFER_BINDING */, &drawFbo); es_glGetIntegerv(0x8CAA /* READ_FRAMEBUFFER_BINDING */, &readFbo);
    std::map<GLuint, STextureObj>::iterator it = g_esTextures.find((GLuint)nat);
    int tw = it != g_esTextures.end() ? it->second.width : 1, th = it != g_esTextures.end() ? it->second.height : 1;
    GLuint fbo = 0; es_glGenFramebuffers(1, &fbo); es_glBindFramebuffer(ES_READ_FRAMEBUFFER, fbo);
    es_glFramebufferTexture2D(ES_READ_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, GL_TEXTURE_2D, (GLuint)nat, 0);
    GLenum st = es_glCheckFramebufferStatus(ES_READ_FRAMEBUFFER);
    GLubyte px[2][4] = { { 0 }, { 0 } };
    std::string row;
    if (st == 0x8CD5)
    {
      es_glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[0]); es_glReadPixels(tw / 2, th / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px[1]);
      // The middle row and column, 8 samples each, as luminance.
      for (int k = 0; k < 16; k++)
      {
        GLubyte t[4] = { 0, 0, 0, 0 };
        if (k < 8) es_glReadPixels(k * tw / 8, th / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, t);
        else es_glReadPixels(tw / 2, (k - 8) * th / 8, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, t);
        sprintf(buf, "%s%d", k == 8 ? " | " : " ", (t[0] + t[1] + t[2]) / 3); row += buf;
      }
    }
    es_glBindFramebuffer(ES_READ_FRAMEBUFFER, (GLuint)readFbo); es_glBindFramebuffer(0x8CA9 /* DRAW_FRAMEBUFFER */, (GLuint)drawFbo); es_glDeleteFramebuffers(1, &fbo);
    GLES_Log("GLES:   native u0 tex %d (%dx%d) fbo 0x%x texel(0,0)=%d,%d,%d,%d texel(mid)=%d,%d,%d,%d row/col%s", nat, tw, th, st,
      px[0][0], px[0][1], px[0][2], px[0][3], px[1][0], px[1][1], px[1][2], px[1][3], row.c_str());
  }
  // Native attribute state of the position, colour and texcoord 0 slots.
  {
    std::string a;
    const int slots[3] = { GLES_ATTR_POS, GLES_ATTR_COLOR, GLES_ATTR_TEX0 };
    for (int i = 0; i < 3; i++)
    {
      GLint en = 0, sz = 0, ty = 0, no = 0, st = 0, bb = 0;
      es_glGetVertexAttribiv(slots[i], 0x8622, &en); es_glGetVertexAttribiv(slots[i], 0x8623, &sz); es_glGetVertexAttribiv(slots[i], 0x8625, &ty);
      es_glGetVertexAttribiv(slots[i], 0x886A, &no); es_glGetVertexAttribiv(slots[i], 0x8624, &st); es_glGetVertexAttribiv(slots[i], 0x889F, &bb);
      sprintf(buf, " slot%d[en %d sz %d ty 0x%x norm %d st %d buf %d]", slots[i], en, sz, ty, no, st, bb); a += buf;
    }
    GLES_Log("GLES:   attribs%s", a.c_str());
  }
  // Small indexed draws: the index values, from the element buffer or client memory.
  if (indexType && count <= 48)
  {
    std::vector<GLubyte> ib; const GLubyte* ip = (const GLubyte*)indices;
    int isz = indexType == GL_UNSIGNED_INT ? 4 : indexType == GL_UNSIGNED_SHORT ? 2 : 1;
    if (g_es.elementBuffer)
    {
      GLint prev = 0; es_glGetIntegerv(0x8895 /* ELEMENT_ARRAY_BUFFER_BINDING */, &prev);
      es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, g_es.elementBuffer);
      size_t need = (uintptr_t)indices + count * isz;
      void* m = es_glMapBufferRange(ES_ELEMENT_ARRAY_BUFFER, 0, (GLsizeiptrARB)need, ES_MAP_READ_BIT);
      if (m) { ib.assign((GLubyte*)m + (uintptr_t)indices, (GLubyte*)m + need); es_glUnmapBuffer(ES_ELEMENT_ARRAY_BUFFER); ip = &ib[0]; }
      else ip = NULL;
      es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, (GLuint)prev);
    }
    std::string v;
    for (int i = 0; ip && i < count; i++)
    {
      unsigned idx = isz == 4 ? ((const GLuint*)ip)[i] : isz == 2 ? ((const GLushort*)ip)[i] : ip[i];
      sprintf(buf, " %u", idx); v += buf;
    }
    GLES_Log("GLES:   indices (type 0x%x, ebo %u)%s", indexType, g_es.elementBuffer, ip ? v.c_str() : " unreadable");
  }
  // Small draws: the first four vertices (position, colour bytes, texcoord) as the arrays describe them.
  if (count <= 2000 && g_es.vertexArray.enabled && g_es.vertexArray.stride >= 12)
  {
    const SESArray& va = g_es.vertexArray; const SESArray& ta = g_es.texcoordArray[0]; const SESArray& ca = g_es.colorArray;
    const GLubyte* base = NULL; std::vector<GLubyte> copy;
    if (va.buffer)
    {
      std::map<GLuint, SESBuffer>::iterator it = g_esBuffers.find(va.buffer);
      if (it != g_esBuffers.end() && !it->second.shadow.empty()) base = &it->second.shadow[0];
      else
      {
        GLint prev = 0; es_glGetIntegerv(0x8894 /* ARRAY_BUFFER_BINDING */, &prev);
        es_glBindBuffer(ES_ARRAY_BUFFER, va.buffer);
        size_t need = (uintptr_t)va.pointer + 8 * va.stride;
        void* m = es_glMapBufferRange(ES_ARRAY_BUFFER, 0, (GLsizeiptrARB)need, ES_MAP_READ_BIT);
        if (m) { copy.assign((GLubyte*)m, (GLubyte*)m + need); es_glUnmapBuffer(ES_ARRAY_BUFFER); base = &copy[0]; }
        else
        {
          GLint mapped = 0, size = 0; es_glGetBufferParameteriv(ES_ARRAY_BUFFER, 0x88BC /* BUFFER_MAPPED */, &mapped); es_glGetBufferParameteriv(ES_ARRAY_BUFFER, 0x8764 /* BUFFER_SIZE */, &size);
          GLES_Log("GLES:   verts: cannot read buffer %u (need %u bytes, size %d, mapped %d, layer size %u mapped %u, error 0x%x)", va.buffer, (unsigned)need, size, mapped,
            it != g_esBuffers.end() ? it->second.size : 0, it != g_esBuffers.end() ? it->second.mapped : 0, es_glGetError());
        }
        es_glBindBuffer(ES_ARRAY_BUFFER, (GLuint)prev);
      }
    }
    else base = (const GLubyte*)0;
    if (base || !va.buffer)
    {
      std::string v;
      for (int i = 0; i < 8; i++)
      {
        const GLubyte* p = (base ? base : (const GLubyte*)0) + (uintptr_t)va.pointer + i * va.stride;
        const float* pos = (const float*)p;
        const GLubyte* col = ca.enabled ? (base ? base : (const GLubyte*)0) + (uintptr_t)ca.pointer + i * ca.stride : NULL;
        const float* uv = ta.enabled ? (const float*)((base ? base : (const GLubyte*)0) + (uintptr_t)ta.pointer + i * ta.stride) : NULL;
        sprintf(buf, " [%d] p %g,%g,%g", i, pos[0], pos[1], pos[2]); v += buf;
        if (col) { sprintf(buf, " c %d,%d,%d,%d", col[0], col[1], col[2], col[3]); v += buf; }
        if (uv) { sprintf(buf, " uv %g,%g", uv[0], uv[1]); v += buf; }
      }
      // Screen extent: clip-space transform with the ARB vertex program's row constants (env 0..3) or the FFP MVP.
      {
        const float* GLES_ARB_Env(bool vertex);
        const float* GLES_CurrentMVP();
        float lo[2] = { 1e9f, 1e9f }, hi[2] = { -1e9f, -1e9f }; bool bad = false;
        for (int i = 0; i < 8; i++)
        {
          const float* pos = (const float*)((base ? base : (const GLubyte*)0) + (uintptr_t)va.pointer + i * va.stride);
          float clip[4];
          if (prog && g_es.boundVP) { const float* e = GLES_ARB_Env(true); for (int r = 0; r < 4; r++) clip[r] = e[r*4]*pos[0] + e[r*4+1]*pos[1] + e[r*4+2]*pos[2] + e[r*4+3]; }
          else { const float* m = GLES_CurrentMVP(); for (int r = 0; r < 4; r++) clip[r] = m[r]*pos[0] + m[4+r]*pos[1] + m[8+r]*pos[2] + m[12+r]; }
          if (clip[3] == 0.0f || clip[3] != clip[3]) { bad = true; continue; }
          float nx = clip[0] / clip[3], ny = clip[1] / clip[3];
          if (nx != nx || ny != ny) { bad = true; continue; }
          if (nx < lo[0]) lo[0] = nx; if (nx > hi[0]) hi[0] = nx; if (ny < lo[1]) lo[1] = ny; if (ny > hi[1]) hi[1] = ny;
        }
        sprintf(buf, " ndc x %.2f..%.2f y %.2f..%.2f%s", lo[0], hi[0], lo[1], hi[1], bad ? " (non-finite w)" : ""); v += buf;
      }
      GLES_Log("GLES:   verts%s", v.c_str());
    }
  }
}

static void __stdcall gles_glDrawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid* indices)
{
  SESTimer timer(GLES_T_DRAWELEM);
  if (!SetupClientArrays()) return;
  if (!GLES_PrepareDraw()) return;
  // FARCRY_GLES_SKIPFP=<id>: drop draws that use that ARB fragment program (bisecting a wrong draw).
  { static GLuint skip = getenv("FARCRY_GLES_SKIPFP") ? (GLuint)atoi(getenv("FARCRY_GLES_SKIPFP")) : 0; if (skip && g_es.boundFP == skip) return; }
  if (g_esDumpDraw) DumpDrawState("elements", count, type, indices);
  GLuint elementBuffer = g_es.elementBuffer;
  ResolveClient(elementBuffer, indices);
  es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, elementBuffer);
  if (mode == GL_QUADS)
  {
    GLsizei n; GLenum t;
    const void* idx = ExpandQuads(count, type, indices, n, t);
    if (idx) { es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, 0); es_glDrawElements(GL_TRIANGLES, n, t, idx); }
    return;
  }
  if (mode == GL_QUAD_STRIP) mode = GL_TRIANGLE_STRIP;
  else if (mode == GL_POLYGON) mode = GL_TRIANGLE_FAN;
  es_glDrawElements(mode, count, type, indices);
}

static void __stdcall gles_glDrawRangeElementsEXT(GLenum mode, GLuint, GLuint, GLsizei count, GLenum type, const GLvoid* indices) { gles_glDrawElements(mode, count, type, indices); }

static void __stdcall gles_glDrawArrays(GLenum mode, GLint first, GLsizei count)
{
  if (!SetupClientArrays()) return;
  if (!GLES_PrepareDraw()) return;
  if (g_esDumpDraw) DumpDrawState("arrays", count);
  if (mode == GL_QUADS)
  {
    sQuadIdx.clear();
    for (GLsizei q = 0; q + 3 < count; q += 4)
    {
      GLuint b = first + q;
      sQuadIdx.push_back(b); sQuadIdx.push_back(b+1); sQuadIdx.push_back(b+2);
      sQuadIdx.push_back(b); sQuadIdx.push_back(b+2); sQuadIdx.push_back(b+3);
    }
    if (sQuadIdx.empty()) return;
    es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, 0);
    es_glDrawElements(GL_TRIANGLES, (GLsizei)sQuadIdx.size(), GL_UNSIGNED_INT, &sQuadIdx[0]);
    return;
  }
  if (mode == GL_QUAD_STRIP) mode = GL_TRIANGLE_STRIP;
  else if (mode == GL_POLYGON) mode = GL_TRIANGLE_FAN;
  es_glDrawArrays(mode, first, count);
}

void GLES_RegisterVertex(std::map<std::string, void*>& t)
{
#define REG(name) t[#name] = (void*)gles_##name;
  REG(glColor3f) REG(glColor4f) REG(glColor3fv) REG(glColor4fv) REG(glColor3d) REG(glColor4d)
  REG(glColor3ub) REG(glColor4ub) REG(glColor3ubv) REG(glColor4ubv)
  REG(glNormal3f) REG(glNormal3fv)
  REG(glSecondaryColor3fEXT) REG(glSecondaryColor3fvEXT) REG(glSecondaryColor3ubvEXT)
  REG(glTexCoord1f) REG(glTexCoord2f) REG(glTexCoord3f) REG(glTexCoord4f) REG(glTexCoord2fv) REG(glTexCoord3fv) REG(glTexCoord4fv) REG(glTexCoord2d)
  REG(glMultiTexCoord1fARB) REG(glMultiTexCoord2fARB) REG(glMultiTexCoord3fARB) REG(glMultiTexCoord4fARB)
  REG(glMultiTexCoord2fvARB) REG(glMultiTexCoord3fvARB) REG(glMultiTexCoord4fvARB)
  REG(glBegin) REG(glEnd)
  REG(glVertex2f) REG(glVertex3f) REG(glVertex4f) REG(glVertex2fv) REG(glVertex3fv) REG(glVertex4fv)
  REG(glVertex2i) REG(glVertex3i) REG(glVertex2d) REG(glVertex3d) REG(glVertex3dv)
  REG(glEnableClientState) REG(glDisableClientState)
  REG(glVertexPointer) REG(glNormalPointer) REG(glColorPointer) REG(glTexCoordPointer) REG(glSecondaryColorPointerEXT)
  REG(glGenBuffersARB) REG(glDeleteBuffersARB) REG(glBindBufferARB) REG(glBufferDataARB) REG(glBufferSubDataARB)
  REG(glMapBufferARB) REG(glUnmapBufferARB) REG(glLockArraysEXT) REG(glUnlockArraysEXT)
  REG(glDrawElements) REG(glDrawRangeElementsEXT) REG(glDrawArrays)
#undef REG
}
