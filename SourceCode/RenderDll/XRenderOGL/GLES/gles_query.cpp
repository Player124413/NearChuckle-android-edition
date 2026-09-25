/*=============================================================================
  gles_query.cpp : NV occlusion queries on ES 3.0 queries, and texture readback
  through a scratch framebuffer for glGetTexImage.
=============================================================================*/
#include "gles_internal.h"
#include <map>
#if defined(__APPLE__)
#include <execinfo.h>
#endif

// ES only answers "any samples passed"; the renderer expects a pixel count. It uses the
// count as a visibility test, so report a nominal count when anything passed.
#define GLES_VISIBLE_SAMPLES 64

static void __stdcall gles_glGenOcclusionQueriesNV(GLsizei n, GLuint* ids) { es_glGenQueries(n, ids); }
static void __stdcall gles_glDeleteOcclusionQueriesNV(GLsizei n, const GLuint* ids);
static void __stdcall gles_glBeginOcclusionQueryNV(GLuint id) { es_glBeginQuery(ES_ANY_SAMPLES_PASSED, id); }
static void __stdcall gles_glEndOcclusionQueryNV() { es_glEndQuery(ES_ANY_SAMPLES_PASSED); }

// The renderer reads last frame's corona query with a blocking GL_PIXEL_COUNT_NV ("Stupidly block until
// we have a query result"), which drains the GPU pipeline whenever the GPU is the bottleneck. A result
// that is not ready yet answers with the query's previous result instead (visible until known):
// a corona's visibility then lags a frame, which its fade hides. FARCRY_GLES_QUERYWAIT=1 restores blocking.
static std::map<GLuint, GLuint> sQueryLast;
static void __stdcall gles_glGetOcclusionQueryuivNV(GLuint id, GLenum pname, GLuint* params)
{
  SESTimer timer(GLES_T_QUERY);
  GLuint v = 0;
  if (pname == 0x8867 /* GL_PIXEL_COUNT_AVAILABLE_NV */) { es_glGetQueryObjectuiv(id, ES_QUERY_RESULT_AVAILABLE, &v); params[0] = v; return; }
  static bool wait = getenv("FARCRY_GLES_QUERYWAIT") != NULL;
  GLuint avail = 1;
  if (!wait) es_glGetQueryObjectuiv(id, ES_QUERY_RESULT_AVAILABLE, &avail);
  if (!avail)
  {
    std::map<GLuint, GLuint>::iterator it = sQueryLast.find(id);
    params[0] = it != sQueryLast.end() ? it->second : GLES_VISIBLE_SAMPLES;
    return;
  }
  es_glGetQueryObjectuiv(id, ES_QUERY_RESULT, &v);
  params[0] = sQueryLast[id] = v ? GLES_VISIBLE_SAMPLES : 0;
}
static void __stdcall gles_glGetOcclusionQueryivNV(GLuint id, GLenum pname, GLint* params) { gles_glGetOcclusionQueryuivNV(id, pname, (GLuint*)params); }

static void __stdcall gles_glDeleteOcclusionQueriesNV(GLsizei n, const GLuint* ids)
{
  for (GLsizei i = 0; i < n; i++) sQueryLast.erase(ids[i]);
  es_glDeleteQueries(n, ids);
}

// glGetTexImage: read the level back through a framebuffer. RGBA/UNSIGNED_BYTE only; other
// formats are converted from that.
static GLuint sReadFBO;

static void __stdcall gles_glGetTexImage(GLenum target, GLint level, GLenum format, GLenum type, GLvoid* pixels)
{
  if (type != GL_UNSIGNED_BYTE) { GLES_Log("GLES: glGetTexImage type 0x%x unsupported", type); return; }
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  GLuint id = 0;
  GLenum attachTarget = GL_TEXTURE_2D;
  if (target == GL_TEXTURE_2D || target == GL_TEXTURE_1D) id = u.bound2D;
  else if (target == ES_TEXTURE_RECTANGLE_NV) id = u.boundRect;
  else if (target >= 0x8515 && target <= 0x851A) { id = u.boundCube; attachTarget = target; }
  else { GLES_Log("GLES: glGetTexImage target 0x%x unsupported", target); return; }
  std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(id);
  if (it == g_esTextures.end()) return;
  STextureObj& o = it->second;
  int w = o.width >> level, h = o.height >> level;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (!sReadFBO) es_glGenFramebuffers(1, &sReadFBO);
  GLint prevFBO = 0;
  es_glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &prevFBO);
  es_glBindFramebuffer(ES_FRAMEBUFFER, sReadFBO);
  es_glFramebufferTexture2D(ES_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, attachTarget, id, level);
  GLenum status = es_glCheckFramebufferStatus(ES_FRAMEBUFFER);
  if (status != ES_FRAMEBUFFER_COMPLETE)
  {
    GLES_Log("GLES: glGetTexImage: texture %u level %d not readable (status 0x%x, error 0x%x, target 0x%x, internal 0x%x, %dx%d, %d levels%s)",
      id, level, status, es_glGetError(), attachTarget, o.internalFormat, o.width, o.height, o.levels, o.generateMipmap ? ", auto mips" : "");
    es_glBindFramebuffer(ES_FRAMEBUFFER, prevFBO);
    return;
  }
  std::vector<GLubyte> rgba(w * h * 4);
  es_glPixelStorei(ES_PACK_ALIGNMENT, 1);
  es_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
  es_glPixelStorei(ES_PACK_ALIGNMENT, 4);
  es_glFramebufferTexture2D(ES_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, attachTarget, 0, 0);
  es_glBindFramebuffer(ES_FRAMEBUFFER, prevFBO);
  // The framebuffer read ignores the swizzle that makes R8/RG8 stand in for the GL luminance,
  // alpha and intensity formats, so first rebuild the RGBA that GL would report for the texture.
  int n = w * h;
  GLenum base = o.internalFormat;
  switch (base)
  {
    case 1: case GL_LUMINANCE4: case GL_LUMINANCE8: case GL_LUMINANCE12: case GL_LUMINANCE16: case 0x84EA: base = GL_LUMINANCE; break;
    case GL_ALPHA4: case GL_ALPHA8: case GL_ALPHA12: case GL_ALPHA16: case 0x84E9: base = GL_ALPHA; break;
    case GL_INTENSITY4: case GL_INTENSITY8: case GL_INTENSITY12: case GL_INTENSITY16: base = GL_INTENSITY; break;
    case 2: case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12: case GL_LUMINANCE16_ALPHA16: case 0x84EB: base = GL_LUMINANCE_ALPHA; break;
  }
  for (int i = 0; i < n; i++)
  {
    GLubyte* p = &rgba[i * 4];
    switch (base)
    {
      case GL_LUMINANCE:       p[1] = p[2] = p[0]; p[3] = 255; break;
      case GL_ALPHA:           p[3] = p[0]; p[0] = p[1] = p[2] = 0; break;
      case GL_INTENSITY:       p[1] = p[2] = p[3] = p[0]; break;
      case GL_LUMINANCE_ALPHA: p[3] = p[1]; p[1] = p[2] = p[0]; break;
    }
  }
  GLubyte* d = (GLubyte*)pixels;
  const GLubyte* s = &rgba[0];
  switch (format)
  {
    case GL_RGBA: memcpy(d, s, n * 4); break;
    case ES_BGRA_EXT: for (int i = 0; i < n; i++, s += 4, d += 4) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; } break;
    case GL_RGB: for (int i = 0; i < n; i++, s += 4, d += 3) { d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; } break;
    case GL_BGR_EXT: for (int i = 0; i < n; i++, s += 4, d += 3) { d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; } break;
    case GL_ALPHA: for (int i = 0; i < n; i++, s += 4) d[i] = s[3]; break;
    case GL_LUMINANCE: case ES_RED: case GL_INTENSITY: for (int i = 0; i < n; i++, s += 4) d[i] = s[0]; break;
    case GL_LUMINANCE_ALPHA: for (int i = 0; i < n; i++, s += 4, d += 2) { d[0] = s[0]; d[1] = s[3]; } break;
    default: GLES_Log("GLES: glGetTexImage format 0x%x unsupported", format); break;
  }
}

// ES 3.0 reads RGBA/UNSIGNED_BYTE from the colour buffer; convert the other formats from that.
static void __stdcall gles_glReadPixels(GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, GLvoid* pixels)
{
  if (g_esDebug)
  {
    static int n;
    if (n++ < 60)
    {
#if defined(__APPLE__)
      void* fr[8]; int k = backtrace(fr, 8); char** sy = backtrace_symbols(fr, k);
      GLES_Log("GLES: glReadPixels #%d %d,%d %dx%d format 0x%x type 0x%x from %s", n, x, y, w, h, format, type, k > 1 ? sy[1] : "?");
      free(sy);
#else
      GLES_Log("GLES: glReadPixels #%d %d,%d %dx%d format 0x%x type 0x%x from %p", n, x, y, w, h, format, type, __builtin_return_address(0));
#endif
    }
  }
  SESTimer timer(GLES_T_READPIX);
  if (format == GL_RGBA && type == GL_UNSIGNED_BYTE) { es_glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels); return; }
  if (format == GL_DEPTH_COMPONENT || format == GL_STENCIL_INDEX)
  {
    int n = w * h;
    std::vector<float> d(n, 1.0f);
    if (format == GL_STENCIL_INDEX || !GLES_ReadDepth(x, y, w, h, &d[0]))
    {
      static bool logged;
      if (!logged) { logged = true; GLES_Log("GLES: glReadPixels format 0x%x unavailable; returning far depth", format); }
    }
    if (type == GL_FLOAT) memcpy(pixels, &d[0], n * sizeof(float));
    else if (type == GL_UNSIGNED_BYTE) for (int i = 0; i < n; i++) ((GLubyte*)pixels)[i] = (GLubyte)(d[i] * 255.0f + 0.5f);
    else if (type == GL_UNSIGNED_SHORT) for (int i = 0; i < n; i++) ((GLushort*)pixels)[i] = (GLushort)(d[i] * 65535.0f + 0.5f);
    else GLES_Log("GLES: glReadPixels depth type 0x%x unsupported", type);
    return;
  }
  if (type != GL_UNSIGNED_BYTE) { GLES_Log("GLES: glReadPixels type 0x%x unsupported", type); return; }
  std::vector<GLubyte> rgba(w * h * 4);
  GLint pack = 4;
  es_glGetIntegerv(ES_PACK_ALIGNMENT, &pack);
  es_glPixelStorei(ES_PACK_ALIGNMENT, 1);
  es_glReadPixels(x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
  es_glPixelStorei(ES_PACK_ALIGNMENT, pack);
  int channels = 0;
  switch (format) { case ES_BGRA_EXT: channels = 4; break; case GL_RGB: case GL_BGR_EXT: channels = 3; break; case GL_ALPHA: case GL_LUMINANCE: case ES_RED: case GL_GREEN: case GL_BLUE: channels = 1; break; }
  if (!channels) { GLES_Log("GLES: glReadPixels format 0x%x unsupported", format); return; }
  int rowBytes = (w * channels + pack - 1) / pack * pack;
  const GLubyte* s = &rgba[0];
  for (int row = 0; row < h; row++)
  {
    GLubyte* d = (GLubyte*)pixels + row * rowBytes;
    for (int i = 0; i < w; i++, s += 4)
    {
      switch (format)
      {
        case ES_BGRA_EXT: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = s[3]; d += 4; break;
        case GL_RGB: d[0] = s[0]; d[1] = s[1]; d[2] = s[2]; d += 3; break;
        case GL_BGR_EXT: d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d += 3; break;
        case GL_ALPHA: *d++ = s[3]; break;
        case GL_GREEN: *d++ = s[1]; break;
        case GL_BLUE: *d++ = s[2]; break;
        default: *d++ = s[0]; break;
      }
    }
  }
}

void GLES_RegisterQuery(std::map<std::string, void*>& t)
{
#define REG(name) t[#name] = (void*)gles_##name;
  REG(glGenOcclusionQueriesNV) REG(glDeleteOcclusionQueriesNV) REG(glBeginOcclusionQueryNV) REG(glEndOcclusionQueryNV)
  REG(glGetOcclusionQueryuivNV) REG(glGetOcclusionQueryivNV) REG(glGetTexImage) REG(glReadPixels)
#undef REG
}
