/*=============================================================================
  gles_texture.cpp : texture objects, format conversion, parameters.
=============================================================================*/
#include "gles_internal.h"
#include <set>
#if defined(__APPLE__)
#include <execinfo.h>
#endif

static GLenum NativeTarget(GLenum target)
{
  switch (target)
  {
    case GL_TEXTURE_1D: case GL_TEXTURE_2D: case ES_TEXTURE_RECTANGLE_NV: return GL_TEXTURE_2D;
    case ES_TEXTURE_3D: return ES_TEXTURE_3D;
    case ES_TEXTURE_CUBE_MAP: return ES_TEXTURE_CUBE_MAP;
  }
  if (target >= 0x8515 && target <= 0x851A) return ES_TEXTURE_CUBE_MAP; // cube faces
  return GL_TEXTURE_2D;
}

static GLuint& BoundSlot(STexUnitState& u, GLenum target)
{
  switch (target)
  {
    case GL_TEXTURE_1D: return u.bound1D;
    case ES_TEXTURE_RECTANGLE_NV: return u.boundRect;
    case ES_TEXTURE_3D: return u.bound3D;
    case ES_TEXTURE_CUBE_MAP: return u.boundCube;
  }
  if (target >= 0x8515 && target <= 0x851A) return u.boundCube;
  return u.bound2D;
}

static GLuint sNativeBound[GLES_MAX_UNITS][4];  // per unit: 2D, cube, 3D, other
static int sNativeActiveUnit;

static int TargetSlot(GLenum t) { return t == GL_TEXTURE_2D ? 0 : t == ES_TEXTURE_CUBE_MAP ? 1 : t == ES_TEXTURE_3D ? 2 : 3; }

void GLES_NativeBindTexture(int unit, GLenum target, GLuint id)
{
  int slot = TargetSlot(target);
  if (sNativeBound[unit][slot] == id) return;
  if (sNativeActiveUnit != unit) { es_glActiveTexture(ES_TEXTURE0 + unit); sNativeActiveUnit = unit; }
  es_glBindTexture(target, GLES_NativeTexName(id, target));
  sNativeBound[unit][slot] = id;
}

// The renderer's own glActiveTextureARB is forwarded natively; keep the shadow in step.
void GLES_NoteActiveUnit(int unit) { sNativeActiveUnit = unit; }

// The overlay binds its own 2D texture on unit 0 and leaves unit 0 active.
void GLES_ReissueTextureUnit0()
{
  es_glActiveTexture(ES_TEXTURE0);
  es_glBindTexture(GL_TEXTURE_2D, GLES_NativeTexName(sNativeBound[0][0]));
  es_glActiveTexture(ES_TEXTURE0 + sNativeActiveUnit);
}
int GLES_NativeActiveUnit() { return sNativeActiveUnit; }

// Direct-mapped cache in front of g_esTextures: every draw looks up each enabled unit's texture.
static struct { GLuint id; STextureObj* obj; } sTexCache[4096];
void GLES_ForgetTextureCache() { memset(sTexCache, 0, sizeof(sTexCache)); }

STextureObj* GLES_FindTexture(GLuint id)
{
  if (!id) return NULL;
  unsigned slot = id & 4095;
  if (sTexCache[slot].id == id) return sTexCache[slot].obj;
  std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(id);
  if (it == g_esTextures.end()) return NULL;
  sTexCache[slot].id = id; sTexCache[slot].obj = &it->second;
  return &it->second;
}

static STextureObj& Obj(GLuint id, GLenum target)
{
  STextureObj& o = g_esTextures[id];
  if (o.id == 0)
  {
    o.id = id;
    o.target = NativeTarget(target);
    o.isRect = (target == ES_TEXTURE_RECTANGLE_NV);
    es_glGenTextures(1, &o.native);
  }
  return o;
}

// The texture manager picks its own names (0x1000 + id) and binds them without glGenTextures. Mali's
// allocator does not skip those, so its generated names (the overlay's, the renderer's fonts and shadow
// maps) ran into them. The driver only ever sees names it generated; the renderer keeps its own.
GLuint GLES_NativeTexName(GLuint id, GLenum target)
{
  if (!id) return 0;
  STextureObj* o = GLES_FindTexture(id);
  return o ? o->native : Obj(id, target).native;
}

// Renderer-side names for glGenTextures: small and never one the texture manager or a live texture uses,
// as the renderer tells its raw GL textures from the texture manager's by value (below 0x1000).
static std::set<GLuint> sGenerated;
static GLuint sNextName = 1;

static void __stdcall gles_glGenTextures(GLsizei n, GLuint* ids)
{
  for (GLsizei i = 0; i < n; i++)
  {
    while (sGenerated.count(sNextName) || g_esTextures.count(sNextName))
      sNextName++;
    if (sNextName >= 0x1000) GLES_Log("GLES: glGenTextures reached the texture manager's names (%u)", sNextName);
    ids[i] = sNextName++;
    sGenerated.insert(ids[i]);
  }
}

static GLboolean __stdcall gles_glIsTexture(GLuint id)
{
  return id && (sGenerated.count(id) || g_esTextures.count(id)) ? GL_TRUE : GL_FALSE;
}

static void CheckNativeTarget(const char* what, const STextureObj* o);

// FARCRY_GLES_WATCHTEX=<id>: log whenever the middle texel of that texture's level 0 changes, naming the
// call that changed it. A debugging aid for "this texture holds the wrong image".
void GLES_WatchTexture(const char* what, GLuint id)
{
  static GLuint watched = getenv("FARCRY_GLES_WATCHTEX") ? (GLuint)atoi(getenv("FARCRY_GLES_WATCHTEX")) : 0;
  static int last = -1;
  if (!watched) return;
  std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(watched);
  if (it == g_esTextures.end() || it->second.target != GL_TEXTURE_2D || !it->second.width) return;
  GLint drawFbo = 0, readFbo = 0;
  es_glGetIntegerv(0x8CA6, &drawFbo); es_glGetIntegerv(0x8CAA, &readFbo);
  GLuint fbo = 0; es_glGenFramebuffers(1, &fbo); es_glBindFramebuffer(ES_READ_FRAMEBUFFER, fbo);
  es_glFramebufferTexture2D(ES_READ_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, GL_TEXTURE_2D, it->second.native, 0);
  GLubyte t[4] = { 0, 0, 0, 0 };
  int lum = -2;
  if (es_glCheckFramebufferStatus(ES_READ_FRAMEBUFFER) == 0x8CD5) { es_glReadPixels(it->second.width / 2, it->second.height / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, t); lum = (t[0] + t[1] + t[2]) / 3; }
  es_glBindFramebuffer(ES_READ_FRAMEBUFFER, (GLuint)readFbo); es_glBindFramebuffer(0x8CA9, (GLuint)drawFbo); es_glDeleteFramebuffers(1, &fbo);
  if (lum != last) { GLES_Log("GLES: WATCH tex %u mid texel %d -> %d after %s (tex %u, unit %d)", watched, last, lum, what, id, g_es.activeUnit); last = lum; }
}

// The texture the renderer means by (active unit, target), bound natively so the following call hits it.
static STextureObj* BindForEdit(GLenum target)
{
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  GLuint id = BoundSlot(u, target);
  if (!id) return NULL;
  STextureObj& o = Obj(id, target);
  GLES_NativeBindTexture(g_es.activeUnit, o.target, id);
  // The draw-time sampler binds leave the native active unit wherever they finished; an upload or
  // parameter call must go to the renderer's unit even when the binding itself is already in place.
  if (sNativeActiveUnit != g_es.activeUnit) { es_glActiveTexture(ES_TEXTURE0 + g_es.activeUnit); sNativeActiveUnit = g_es.activeUnit; }
  CheckNativeTarget("edit", &o);
  GLES_WatchTexture("previous edit", id);
  return &o;
}

// Debug: the texture the native GL will actually write, compared with the one the layer means.
static void CheckNativeTarget(const char* what, const STextureObj* o)
{
  if (!g_esDebug || !o || o->target != GL_TEXTURE_2D) return;
  GLint au = 0, b = 0;
  es_glGetIntegerv(0x84E0 /* GL_ACTIVE_TEXTURE */, &au);
  es_glGetIntegerv(0x8069 /* GL_TEXTURE_BINDING_2D */, &b);
  if ((GLuint)b != o->native) GLES_Log("GLES: MISMATCH %s meant tex %u on unit %d but native unit %d has %d bound (shadow native unit %d)", what, o->id, g_es.activeUnit, au - 0x84C0, b, sNativeActiveUnit);
}

STextureObj* GLES_BoundTexture(int unit, GLenum& samplerTarget)
{
  STexUnitState& u = g_es.unit[unit];
  GLuint id = 0;
  if (u.enableCube && u.boundCube) { id = u.boundCube; samplerTarget = ES_TEXTURE_CUBE_MAP; }
  else if (u.enable3D && u.bound3D) { id = u.bound3D; samplerTarget = ES_TEXTURE_3D; }
  else if (u.enableRect && u.boundRect) { id = u.boundRect; samplerTarget = ES_TEXTURE_RECTANGLE_NV; }
  else if (u.enable2D && u.bound2D) { id = u.bound2D; samplerTarget = GL_TEXTURE_2D; }
  else if (u.enable1D && u.bound1D) { id = u.bound1D; samplerTarget = GL_TEXTURE_1D; }
  return GLES_FindTexture(id);
}

static void __stdcall gles_glBindTexture(GLenum target, GLuint id)
{
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  BoundSlot(u, target) = id;
  if (id && !GLES_FindTexture(id)) Obj(id, target);
  GLES_NativeBindTexture(g_es.activeUnit, NativeTarget(target), id);
}

static void __stdcall gles_glDeleteTextures(GLsizei n, const GLuint* ids)
{
  if (g_esDebug) for (GLsizei i = 0; i < n; i++) GLES_Log("GLES: glDeleteTextures %u", ids[i]);
  std::vector<GLuint> natives;
  for (GLsizei i = 0; i < n; i++)
  {
    std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(ids[i]);
    if (it != g_esTextures.end() && it->second.native) natives.push_back(it->second.native);
    if (sGenerated.erase(ids[i]) && ids[i] < sNextName) sNextName = ids[i];
    g_esTextures.erase(ids[i]);
    if (sTexCache[ids[i] & 4095].id == ids[i]) sTexCache[ids[i] & 4095].id = 0;
    for (int un = 0; un < GLES_MAX_UNITS; un++)
    {
      STexUnitState& u = g_es.unit[un];
      if (u.bound2D == ids[i]) u.bound2D = 0;
      if (u.boundRect == ids[i]) u.boundRect = 0;
      if (u.boundCube == ids[i]) u.boundCube = 0;
      if (u.bound3D == ids[i]) u.bound3D = 0;
      if (u.bound1D == ids[i]) u.bound1D = 0;
    }
  }
  if (!natives.empty()) es_glDeleteTextures((GLsizei)natives.size(), &natives[0]);
  for (GLsizei i = 0; i < n; i++)
    for (int un = 0; un < GLES_MAX_UNITS; un++)
      for (int s = 0; s < 4; s++)
        if (sNativeBound[un][s] == ids[i]) sNativeBound[un][s] = 0;
}

//////////////////////////////////////////////////////////////////////////
// Formats
//////////////////////////////////////////////////////////////////////////

struct SFormat
{
  GLenum internal;   // sized ES internal format
  GLenum format;     // ES upload format
  int channels;      // channels of the ES upload format
  const char* swizzle; // NULL or 4 chars of r,g,b,a,0,1
};

static bool MapInternalFormat(GLenum internalFormat, SFormat& f)
{
  f.swizzle = NULL;
  switch (internalFormat)
  {
    case 3: case GL_RGB: case ES_RGB8: case GL_RGB5: case GL_RGB4: case GL_R3_G3_B2: case GL_RGB10: case GL_RGB12: case GL_RGB16:
    case 0x84ED /* GL_COMPRESSED_RGB_ARB */:
      f.internal = ES_RGB8; f.format = GL_RGB; f.channels = 3; return true;
    case 4: case GL_RGBA: case ES_RGBA8: case GL_RGBA4: case GL_RGB5_A1: case GL_RGBA2: case GL_RGB10_A2: case GL_RGBA12: case GL_RGBA16:
    case 0x84EE /* GL_COMPRESSED_RGBA_ARB */: case ES_BGRA_EXT:
      f.internal = ES_RGBA8; f.format = GL_RGBA; f.channels = 4; return true;
    case 1: case GL_LUMINANCE: case GL_LUMINANCE8: case GL_LUMINANCE4: case GL_LUMINANCE12: case GL_LUMINANCE16:
    case 0x84EA /* GL_COMPRESSED_LUMINANCE_ARB */:
      f.internal = ES_R8; f.format = ES_RED; f.channels = 1; f.swizzle = "rrr1"; return true;
    case GL_ALPHA: case GL_ALPHA8: case GL_ALPHA4: case GL_ALPHA12: case GL_ALPHA16:
    case 0x84E9 /* GL_COMPRESSED_ALPHA_ARB */:
      f.internal = ES_R8; f.format = ES_RED; f.channels = 1; f.swizzle = "000r"; return true;
    case GL_INTENSITY: case GL_INTENSITY8: case GL_INTENSITY4: case GL_INTENSITY12: case GL_INTENSITY16:
      f.internal = ES_R8; f.format = ES_RED; f.channels = 1; f.swizzle = "rrrr"; return true;
    case ES_COMPRESSED_RGB_S3TC_DXT1: // uncompressed data with a compressed internal format: store uncompressed
      f.internal = ES_RGB8; f.format = GL_RGB; f.channels = 3; return true;
    case ES_COMPRESSED_RGBA_S3TC_DXT1: case ES_COMPRESSED_RGBA_S3TC_DXT3: case ES_COMPRESSED_RGBA_S3TC_DXT5:
      f.internal = ES_RGBA8; f.format = GL_RGBA; f.channels = 4; return true;
    case 2: case GL_LUMINANCE_ALPHA: case GL_LUMINANCE8_ALPHA8: case GL_LUMINANCE4_ALPHA4: case GL_LUMINANCE6_ALPHA2: case GL_LUMINANCE12_ALPHA4: case GL_LUMINANCE12_ALPHA12: case GL_LUMINANCE16_ALPHA16:
    case 0x84EB /* GL_COMPRESSED_LUMINANCE_ALPHA_ARB */:
      f.internal = ES_RG8; f.format = ES_RG; f.channels = 2; f.swizzle = "rrrg"; return true;
  }
  return false;
}

static int SrcChannels(GLenum format)
{
  switch (format)
  {
    case GL_RGB: case GL_BGR_EXT: return 3;
    case GL_RGBA: case ES_BGRA_EXT: return 4;
    case GL_LUMINANCE: case GL_ALPHA: case ES_RED: case GL_INTENSITY: case GL_COLOR_INDEX: return 1;
    case GL_LUMINANCE_ALPHA: case ES_RG: return 2;
  }
  return 0;
}

// Convert an unsigned-byte image to the ES upload format. Returns NULL if no conversion is needed.
static std::vector<GLubyte> sConvBuf;
static const void* ConvertPixels(int w, int h, GLenum srcFormat, GLenum type, const void* pixels, const SFormat& dst, bool& converted)
{
  converted = false;
  if (!pixels || type != GL_UNSIGNED_BYTE) return pixels;
  int sc = SrcChannels(srcFormat);
  if (sc == 0) return pixels;
  bool bgr = (srcFormat == ES_BGRA_EXT || srcFormat == GL_BGR_EXT);
  if (!bgr && sc == dst.channels) return pixels;

  int align = g_es.unpackAlignment;
  int srcRow = w * sc;
  srcRow = (srcRow + align - 1) / align * align;
  sConvBuf.resize(w * h * dst.channels);
  const GLubyte* s = (const GLubyte*)pixels;
  GLubyte* d = &sConvBuf[0];
  for (int y = 0; y < h; y++)
  {
    const GLubyte* sr = s + y * srcRow;
    for (int x = 0; x < w; x++, sr += sc, d += dst.channels)
    {
      GLubyte r = 0, g = 0, b = 0, a = 255;
      switch (sc)
      {
        case 1: r = g = b = sr[0]; if (srcFormat == GL_ALPHA) { a = sr[0]; r = g = b = 0; } break;
        case 2: r = g = b = sr[0]; a = sr[1]; break;
        case 3: if (bgr) { r = sr[2]; g = sr[1]; b = sr[0]; } else { r = sr[0]; g = sr[1]; b = sr[2]; } break;
        case 4: if (bgr) { r = sr[2]; g = sr[1]; b = sr[0]; } else { r = sr[0]; g = sr[1]; b = sr[2]; } a = sr[3]; break;
      }
      switch (dst.channels)
      {
        case 1: d[0] = (dst.format == ES_RED && srcFormat == GL_ALPHA) ? a : r; break;
        case 2: d[0] = r; d[1] = a; break;
        case 3: d[0] = r; d[1] = g; d[2] = b; break;
        case 4: d[0] = r; d[1] = g; d[2] = b; d[3] = a; break;
      }
    }
  }
  converted = true;
  return &sConvBuf[0];
}

static void ApplySwizzle(STextureObj& o, const char* sw)
{
  if (!sw || o.hasSwizzle) return;
  static const GLenum pn[4] = { ES_TEXTURE_SWIZZLE_R, ES_TEXTURE_SWIZZLE_G, ES_TEXTURE_SWIZZLE_B, ES_TEXTURE_SWIZZLE_A };
  for (int i = 0; i < 4; i++)
  {
    GLint v = GL_RED;
    switch (sw[i]) { case 'r': v = ES_RED; break; case 'g': v = GL_GREEN; break; case 'b': v = GL_BLUE; break; case 'a': v = GL_ALPHA; break; case '0': v = GL_ZERO; break; case '1': v = GL_ONE; break; }
    es_glTexParameteri(o.target, pn[i], v);
  }
  o.hasSwizzle = true;
}

static bool IsS3TC(GLenum f) { return f >= ES_COMPRESSED_RGB_S3TC_DXT1 && f <= ES_COMPRESSED_RGBA_S3TC_DXT5; }
static int FaceIndex(GLenum target) { return (target >= 0x8515 && target <= 0x851A) ? (int)(target - 0x8515) : -1; }
static int CompKey(GLenum target, int level) { int f = FaceIndex(target); return level + (f > 0 ? f * 16 : 0); }

// The renderer uploads single cube faces into texture objects of their own and reads them back
// (CopyTexture assembles cube maps that way). ES will not attach or sample a face of an incomplete
// cube map, so the faces not yet uploaded at this level are allocated empty.
static void CompleteCubeLevel(STextureObj& o, GLenum target, int level, GLsizei w, GLsizei h, GLenum internal, GLenum format, int compSize)
{
  int f = FaceIndex(target);
  if (f < 0 || level >= 16) return;
  o.faceMask[level] |= 1 << f;
  if (o.faceMask[level] == 0x3F) return;
  std::vector<GLubyte> zero(compSize > 0 ? compSize : 0);
  for (int i = 0; i < 6; i++)
  {
    if (o.faceMask[level] & (1 << i)) continue;
    if (compSize > 0) es_glCompressedTexImage2D(0x8515 + i, level, internal, w, h, 0, compSize, &zero[0]);
    else es_glTexImage2D(0x8515 + i, level, internal, w, h, 0, format, GL_UNSIGNED_BYTE, NULL);
  }
}
static bool IsDepthFormat(GLenum f) { return f == GL_DEPTH_COMPONENT || (f >= 0x81A5 && f <= 0x81A7) /* DEPTH_COMPONENT16/24/32 */; }

// Depth textures are allocated in the scene framebuffer's format so its depth can be blitted into them.
static void AllocDepthTexture(STextureObj& o, GLenum nativeTarget, GLint level, GLsizei w, GLsizei h)
{
  es_glTexImage2D(nativeTarget, level, ES_DEPTH24_STENCIL8, w, h, 0, ES_DEPTH_STENCIL, ES_UNSIGNED_INT_24_8, NULL);
  ApplySwizzle(o, "rrr1");
}

static bool NativeS3TC()
{
  static int has = -1;
  // FARCRY_GLES_DECODEDXT: decode on the CPU even where the driver has S3TC (the engine still sees the extension unless FARCRY_GLES_NOS3TC).
  if (has < 0) has = (!getenv("FARCRY_GLES_DECODEDXT") && !getenv("FARCRY_GLES_NOS3TC") && (GLES_HasNativeExt("GL_EXT_texture_compression_s3tc") || GLES_HasNativeExt("GL_ANGLE_texture_compression_dxt"))) ? 1 : 0;
  return has == 1;
}

// Expand any unsigned-byte source image to tightly packed RGBA.
static void ToRGBA(int w, int h, GLenum srcFormat, const GLubyte* s, std::vector<GLubyte>& rgba)
{
  int sc = SrcChannels(srcFormat);
  bool bgr = (srcFormat == ES_BGRA_EXT || srcFormat == GL_BGR_EXT);
  int align = g_es.unpackAlignment;
  int srcRow = (w * sc + align - 1) / align * align;
  rgba.resize(w * h * 4);
  GLubyte* d = &rgba[0];
  for (int y = 0; y < h; y++)
  {
    const GLubyte* sr = s + y * srcRow;
    for (int x = 0; x < w; x++, sr += sc, d += 4)
    {
      switch (sc)
      {
        case 1: d[0] = d[1] = d[2] = sr[0]; d[3] = 255; break;
        case 2: d[0] = d[1] = d[2] = sr[0]; d[3] = sr[1]; break;
        case 3: d[0] = bgr ? sr[2] : sr[0]; d[1] = sr[1]; d[2] = bgr ? sr[0] : sr[2]; d[3] = 255; break;
        default: d[0] = bgr ? sr[2] : sr[0]; d[1] = sr[1]; d[2] = bgr ? sr[0] : sr[2]; d[3] = sr[3]; break;
      }
    }
  }
}

static void __stdcall gles_glTexImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const GLvoid* pixels)
{
  SESTimer timer(GLES_T_TEXIMAGE);
  STextureObj* o = BindForEdit(target);
  if (!o) return;
  GLenum nativeTargetC = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  // The renderer asks the driver to compress: do it here, so the streamer can read the blocks back.
  static bool noCompress = getenv("FARCRY_GLES_NOCOMPRESS") != NULL;
  static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
  if (debug && !pixels) GLES_Log("GLES: glTexImage2D tex %u level %d %dx%d internal 0x%x format 0x%x (no data)", o->id, level, w, h, internalFormat, format);
#if defined(__APPLE__)
  {
    // Mac only: who uploads the watched texture (FARCRY_GLES_WATCHTEX).
    static GLuint watched = getenv("FARCRY_GLES_WATCHTEX") ? (GLuint)atoi(getenv("FARCRY_GLES_WATCHTEX")) : 0;
    if (watched && o->id == watched && level == 0)
    {
      void* frames[24]; int n = backtrace(frames, 24); char** syms = backtrace_symbols(frames, n);
      for (int i = 0; i < n; i++) GLES_Log("GLES:   upload from %s", syms[i]);
      free(syms);
    }
  }
#endif
  if (debug && pixels && !IsS3TC(internalFormat))
  {
    // Content check for uncompressed uploads: mean of the first row.
    int sc = SrcChannels(format), sum[4] = { 0, 0, 0, 0 };
    if (sc && type == GL_UNSIGNED_BYTE) for (int x = 0; x < w; x++) for (int c = 0; c < sc; c++) sum[c] += ((const GLubyte*)pixels)[x * sc + c];
    GLES_Log("GLES: glTexImage2D tex %u level %d %dx%d internal 0x%x format 0x%x type 0x%x row0 mean %d %d %d %d", o->id, level, w, h, internalFormat, format, type,
      w && sc ? sum[0] / w : -1, w && sc > 1 ? sum[1] / w : -1, w && sc > 2 ? sum[2] / w : -1, w && sc > 3 ? sum[3] / w : -1);
  }
  if (IsS3TC(internalFormat) && pixels && type == GL_UNSIGNED_BYTE && SrcChannels(format) > 0 && !noCompress)
  {
    if (debug) GLES_Log("GLES: glTexImage2D tex %u level %d %dx%d compress 0x%x from format 0x%x", o->id, level, w, h, internalFormat, format);
    std::vector<GLubyte> rgba;
    ToRGBA(w, h, format, (const GLubyte*)pixels, rgba);
    // ES cannot generate mipmaps of compressed textures: build the chain here when asked.
    int lw = w, lh = h, lvl = level;
    for (;;)
    {
      std::vector<GLubyte>& blocks = o->compressed[CompKey(target, lvl)];
      int size = GLES_CompressDXT(internalFormat, lw, lh, &rgba[0], blocks);
      if (NativeS3TC())
      {
        es_glCompressedTexImage2D(nativeTargetC, lvl, internalFormat, lw, lh, 0, size, &blocks[0]);
        CompleteCubeLevel(*o, target, lvl, lw, lh, internalFormat, 0, size);
      }
      else
      {
        es_glPixelStorei(ES_UNPACK_ALIGNMENT, 1);
        es_glTexImage2D(nativeTargetC, lvl, ES_RGBA8, lw, lh, 0, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
        es_glPixelStorei(ES_UNPACK_ALIGNMENT, g_es.unpackAlignment);
        CompleteCubeLevel(*o, target, lvl, lw, lh, ES_RGBA8, GL_RGBA, 0);
      }
      if (lvl >= o->levels) o->levels = lvl + 1;
      if (!(level == 0 && o->generateMipmap) || (lw == 1 && lh == 1)) break;
      int nw = lw > 1 ? lw / 2 : 1, nh = lh > 1 ? lh / 2 : 1;
      std::vector<GLubyte> next(nw * nh * 4);
      for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++)
          for (int c = 0; c < 4; c++)
          {
            int x0 = x * 2, y0 = y * 2, x1 = lw > 1 ? x0 + 1 : x0, y1 = lh > 1 ? y0 + 1 : y0;
            next[(y * nw + x) * 4 + c] = (GLubyte)((rgba[(y0 * lw + x0) * 4 + c] + rgba[(y0 * lw + x1) * 4 + c] + rgba[(y1 * lw + x0) * 4 + c] + rgba[(y1 * lw + x1) * 4 + c] + 2) / 4);
          }
      rgba.swap(next);
      lw = nw; lh = nh; lvl++;
    }
    if (level == 0) { o->width = w; o->height = h; o->internalFormat = internalFormat; }
    return;
  }
  if (IsDepthFormat(internalFormat))
  {
    if (pixels) GLES_Log("GLES: glTexImage2D with depth data is not supported (tex %u)", o->id);
    AllocDepthTexture(*o, nativeTargetC, level, w, h);
    if (level == 0) { o->width = w; o->height = h; o->internalFormat = internalFormat; }
    if (level >= o->levels) o->levels = level + 1;
    return;
  }
  SFormat f;
  if (!MapInternalFormat(internalFormat, f))
  {
    static std::map<GLenum, bool> logged;
    if (!logged[internalFormat]) { logged[internalFormat] = true; GLES_Log("GLES: glTexImage2D internal format 0x%x unsupported", internalFormat); }
    return;
  }
  // Signed bytes (DSDT bump maps) are biased to unsigned; the NV texture shaders that read them signed are not offered.
  std::vector<GLubyte> biased;
  if (type == GL_BYTE && pixels && SrcChannels(format) > 0)
  {
    biased.resize(w * h * SrcChannels(format));
    const GLubyte* s = (const GLubyte*)pixels;
    for (size_t i = 0; i < biased.size(); i++) biased[i] = s[i] ^ 0x80;
    pixels = &biased[0];
    type = GL_UNSIGNED_BYTE;
  }
  if (type != GL_UNSIGNED_BYTE)
  {
    static std::map<GLenum, bool> logged;
    if (!logged[type]) { logged[type] = true; GLES_Log("GLES: glTexImage2D type 0x%x unsupported", type); }
    return;
  }
  bool converted;
  const void* data = ConvertPixels(w, h, format, type, pixels, f, converted);
  if (converted) es_glPixelStorei(ES_UNPACK_ALIGNMENT, 1);
  GLenum nativeTarget = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  if (debug) GLES_Log("GLES: glTexImage2D tex %u target 0x%x level %d %dx%d internal 0x%x -> 0x%x format 0x%x -> 0x%x%s", o->id, target, level, w, h, internalFormat, f.internal, format, f.format, converted ? " (converted)" : "");
  es_glTexImage2D(nativeTarget, level, f.internal, w, h, 0, f.format, GL_UNSIGNED_BYTE, data);
  if (converted) es_glPixelStorei(ES_UNPACK_ALIGNMENT, g_es.unpackAlignment);
  CompleteCubeLevel(*o, target, level, w, h, f.internal, f.format, 0);
  ApplySwizzle(*o, f.swizzle);
  if (level == 0) { o->width = w; o->height = h; o->internalFormat = internalFormat; }
  if (level >= o->levels) o->levels = level + 1;
  if (level == 0 && o->generateMipmap) es_glGenerateMipmap(o->target);
}

static void __stdcall gles_glTexSubImage2D(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const GLvoid* pixels)
{
  if (g_esDebug) { GLenum t = 0; STextureObj* o = GLES_BoundTexture(g_es.activeUnit, t); GLES_Log("GLES: glTexSubImage2D tex %u level %d %d,%d %dx%d format 0x%x type 0x%x", o ? o->id : 0, level, x, y, w, h, format, type); }
  SESTimer timer(GLES_T_TEXSUB);
  STextureObj* o = BindForEdit(target);
  if (!o || type != GL_UNSIGNED_BYTE) return;
  SFormat f;
  if (!MapInternalFormat(o->internalFormat, f)) return;
  bool converted;
  const void* data = ConvertPixels(w, h, format, type, pixels, f, converted);
  if (converted) es_glPixelStorei(ES_UNPACK_ALIGNMENT, 1);
  GLenum nativeTarget = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  es_glTexSubImage2D(nativeTarget, level, x, y, w, h, f.format, GL_UNSIGNED_BYTE, data);
  if (converted) es_glPixelStorei(ES_UNPACK_ALIGNMENT, g_es.unpackAlignment);
  if (level == 0 && o->generateMipmap) es_glGenerateMipmap(o->target);
}

static void __stdcall gles_glCompressedTexImage2DARB(GLenum target, GLint level, GLenum internalFormat, GLsizei w, GLsizei h, GLint border, GLsizei size, const GLvoid* data)
{
  if (g_esDebug) { GLenum t = 0; STextureObj* o = GLES_BoundTexture(g_es.activeUnit, t); GLES_Log("GLES: glCompressedTexImage2D tex %u level %d %dx%d format 0x%x size %d", o ? o->id : 0, level, w, h, internalFormat, size); }
  STextureObj* o = BindForEdit(target);
  if (!o) return;
  GLenum nativeTarget = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  if (data) o->compressed[CompKey(target, level)].assign((const GLubyte*)data, (const GLubyte*)data + size);
  if (IsS3TC(internalFormat) && !NativeS3TC())
  {
    // No S3TC on this driver (Adreno 6xx): decode the streamed-in DXT to RGBA8. The blocks stay kept above for readback.
    std::vector<GLubyte> rgba;
    if (data) GLES_DecompressDXT(internalFormat, w, h, (const GLubyte*)data, rgba);
    es_glPixelStorei(ES_UNPACK_ALIGNMENT, 1);
    es_glTexImage2D(nativeTarget, level, ES_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, data ? &rgba[0] : NULL);
    es_glPixelStorei(ES_UNPACK_ALIGNMENT, g_es.unpackAlignment);
    CompleteCubeLevel(*o, target, level, w, h, ES_RGBA8, GL_RGBA, 0);
  }
  else
  {
    es_glCompressedTexImage2D(nativeTarget, level, internalFormat, w, h, 0, size, data);
    CompleteCubeLevel(*o, target, level, w, h, internalFormat, 0, size);
  }
  if (level == 0 && o->generateMipmap) GLES_Log("GLES: mipmap generation requested for compressed texture %u; not possible on ES", o->id);
  if (level == 0) { o->width = w; o->height = h; o->internalFormat = internalFormat; }
  if (level >= o->levels) o->levels = level + 1;
}

static void __stdcall gles_glGetCompressedTexImageARB(GLenum target, GLint level, GLvoid* out)
{
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  GLuint id = BoundSlot(u, target);
  std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(id);
  if (it == g_esTextures.end()) return;
  std::map<int, std::vector<GLubyte> >::iterator lv = it->second.compressed.find(CompKey(target, level));
  if (lv == it->second.compressed.end()) { GLES_Log("GLES: glGetCompressedTexImage: no blocks kept for texture %u level %d", id, level); return; }
  memcpy(out, &lv->second[0], lv->second.size());
}

static void __stdcall gles_glCompressedTexSubImage2DARB(GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLsizei size, const GLvoid* data)
{
  STextureObj* o = BindForEdit(target);
  if (!o) return;
  GLenum nativeTarget = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  if (IsS3TC(format) && !NativeS3TC())
  {
    std::vector<GLubyte> rgba;
    GLES_DecompressDXT(format, w, h, (const GLubyte*)data, rgba);
    es_glPixelStorei(ES_UNPACK_ALIGNMENT, 1);
    es_glTexSubImage2D(nativeTarget, level, x, y, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &rgba[0]);
    es_glPixelStorei(ES_UNPACK_ALIGNMENT, g_es.unpackAlignment);
    return;
  }
  es_glCompressedTexSubImage2D(nativeTarget, level, x, y, w, h, format, size, data);
}

static void __stdcall gles_glCopyTexSubImage2D(GLenum target, GLint level, GLint xo, GLint yo, GLint x, GLint y, GLsizei w, GLsizei h)
{
  if (g_esDebug) { GLenum t = 0; STextureObj* o = GLES_BoundTexture(g_es.activeUnit, t); GLES_Log("GLES: glCopyTexSubImage2D tex %u level %d %dx%d", o ? o->id : 0, level, w, h); }
  SESTimer timer(GLES_T_COPYTEX);
  STextureObj* o = BindForEdit(target);
  if (!o) return;
  GLenum nativeTarget = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  if (IsDepthFormat(o->internalFormat))
  {
    if (xo || yo) GLES_Log("GLES: glCopyTexSubImage2D of depth with an offset is not supported");
    else GLES_CopySceneDepth(o->native, nativeTarget, level, x, y, w, h);
    return;
  }
  es_glCopyTexSubImage2D(nativeTarget, level, xo, yo, x, y, w, h);
}

static void __stdcall gles_glCopyTexImage2D(GLenum target, GLint level, GLenum internalFormat, GLint x, GLint y, GLsizei w, GLsizei h, GLint)
{
  if (g_esDebug) { GLenum t = 0; STextureObj* o = GLES_BoundTexture(g_es.activeUnit, t); GLES_Log("GLES: glCopyTexImage2D tex %u level %d %dx%d format 0x%x", o ? o->id : 0, level, w, h, internalFormat); }
  SESTimer timer(GLES_T_COPYTEX);
  STextureObj* o = BindForEdit(target);
  if (!o) return;
  GLenum nativeTarget = (target >= 0x8515 && target <= 0x851A) ? target : o->target;
  if (IsDepthFormat(internalFormat))
  {
    AllocDepthTexture(*o, nativeTarget, level, w, h);
    GLES_CopySceneDepth(o->native, nativeTarget, level, x, y, w, h);
  }
  else
  {
    SFormat f;
    if (!MapInternalFormat(internalFormat, f)) { GLES_Log("GLES: glCopyTexImage2D internal format 0x%x unsupported", internalFormat); return; }
    es_glCopyTexImage2D(nativeTarget, level, f.internal, x, y, w, h, 0);
    CompleteCubeLevel(*o, target, level, w, h, f.internal, f.format, 0);
    ApplySwizzle(*o, f.swizzle);
  }
  if (level == 0) { o->width = w; o->height = h; o->internalFormat = internalFormat; }
  if (level >= o->levels) o->levels = level + 1;
  if (level == 0 && o->generateMipmap && !IsDepthFormat(internalFormat)) es_glGenerateMipmap(o->target);
}

static void TexParam(GLenum target, GLenum pname, GLint iparam, GLfloat fparam)
{
  STextureObj* o = BindForEdit(target);
  if (!o) return;
  switch (pname)
  {
    case GL_TEXTURE_WRAP_S: case GL_TEXTURE_WRAP_T: case ES_TEXTURE_WRAP_R:
      if (iparam == GL_CLAMP || iparam == 0x812D /* CLAMP_TO_BORDER */) iparam = GL_CLAMP_TO_EDGE;
      es_glTexParameteri(o->target, pname, iparam);
      return;
    case GL_TEXTURE_MIN_FILTER: case GL_TEXTURE_MAG_FILTER:
    case GL_TEXTURE_MIN_LOD: case GL_TEXTURE_MAX_LOD: case GL_TEXTURE_BASE_LEVEL: case ES_TEXTURE_MAX_LEVEL:
      es_glTexParameteri(o->target, pname, iparam);
      return;
    case ES_GENERATE_MIPMAP_SGIS:
      o->generateMipmap = iparam != 0;
      return;
    case ES_TEXTURE_MAX_ANISOTROPY:
      if (GLES_HasNativeExt("GL_EXT_texture_filter_anisotropic")) es_glTexParameterf(o->target, pname, fparam);
      return;
    // Shadow compare: SGIX names mapped, ARB names are the ES values.
    case 0x819A /* GL_TEXTURE_COMPARE_SGIX */:
      es_glTexParameteri(o->target, ES_TEXTURE_COMPARE_MODE, iparam ? ES_COMPARE_REF_TO_TEXTURE : GL_NONE);
      return;
    case 0x819B /* GL_TEXTURE_COMPARE_OPERATOR_SGIX */:
      es_glTexParameteri(o->target, ES_TEXTURE_COMPARE_FUNC, iparam == 0x819D /* GEQUAL_R */ ? GL_GEQUAL : GL_LEQUAL);
      return;
    case ES_TEXTURE_COMPARE_MODE: case ES_TEXTURE_COMPARE_FUNC:
      es_glTexParameteri(o->target, pname, iparam);
      return;
    // border colour, priority, LOD bias: nothing to do on ES
  }
}

static void __stdcall gles_glTexParameteri(GLenum target, GLenum pname, GLint param) { TexParam(target, pname, param, (GLfloat)param); }
static void __stdcall gles_glTexParameterf(GLenum target, GLenum pname, GLfloat param) { TexParam(target, pname, (GLint)param, param); }
static void __stdcall gles_glTexParameteriv(GLenum target, GLenum pname, const GLint* params) { TexParam(target, pname, params[0], (GLfloat)params[0]); }
static void __stdcall gles_glTexParameterfv(GLenum target, GLenum pname, const GLfloat* params) { TexParam(target, pname, (GLint)params[0], params[0]); }

static void __stdcall gles_glGetTexLevelParameteriv(GLenum target, GLint level, GLenum pname, GLint* params)
{
  STexUnitState& u = g_es.unit[g_es.activeUnit];
  GLuint id = BoundSlot(u, target);
  std::map<GLuint, STextureObj>::iterator it = g_esTextures.find(id);
  if (it == g_esTextures.end()) { params[0] = 0; return; }
  STextureObj& o = it->second;
  int w = o.width >> level, h = o.height >> level;
  switch (pname)
  {
    case GL_TEXTURE_WIDTH:  params[0] = w > 0 ? w : 1; break;
    case GL_TEXTURE_HEIGHT: params[0] = h > 0 ? h : 1; break;
    case GL_TEXTURE_INTERNAL_FORMAT: params[0] = o.internalFormat; break;
    case 0x86A1 /* GL_TEXTURE_COMPRESSED_ARB */: params[0] = (o.internalFormat >= 0x83F0 && o.internalFormat <= 0x83F3); break;
    case 0x86A0 /* GL_TEXTURE_COMPRESSED_IMAGE_SIZE_ARB */:
    {
      std::map<int, std::vector<GLubyte> >::iterator lv = o.compressed.find(level);
      if (lv != o.compressed.end()) { params[0] = (GLint)lv->second.size(); break; }
      int bs = (o.internalFormat == ES_COMPRESSED_RGB_S3TC_DXT1 || o.internalFormat == ES_COMPRESSED_RGBA_S3TC_DXT1) ? 8 : 16;
      if (w < 1) w = 1;
      if (h < 1) h = 1;
      params[0] = ((w + 3) / 4) * ((h + 3) / 4) * bs;
      break;
    }
    default: params[0] = 0; break;
  }
}

static void __stdcall gles_glPrioritizeTextures(GLsizei, const GLuint*, const GLclampf*) {}
static GLboolean __stdcall gles_glAreTexturesResident(GLsizei n, const GLuint*, GLboolean* res) { for (GLsizei i = 0; i < n; i++) res[i] = GL_TRUE; return GL_TRUE; }

void GLES_RegisterTexture(std::map<std::string, void*>& t)
{
#define REG(name) t[#name] = (void*)gles_##name;
  REG(glGenTextures) REG(glIsTexture) REG(glBindTexture) REG(glDeleteTextures) REG(glTexImage2D) REG(glTexSubImage2D)
  REG(glCompressedTexImage2DARB) REG(glCompressedTexSubImage2DARB) REG(glCopyTexSubImage2D) REG(glCopyTexImage2D)
  REG(glTexParameteri) REG(glTexParameterf) REG(glTexParameteriv) REG(glTexParameterfv)
  REG(glGetTexLevelParameteriv) REG(glPrioritizeTextures) REG(glAreTexturesResident) REG(glGetCompressedTexImageARB)
#undef REG
}
