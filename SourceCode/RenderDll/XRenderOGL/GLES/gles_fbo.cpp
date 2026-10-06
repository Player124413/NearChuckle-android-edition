/*=============================================================================
  gles_fbo.cpp : the scene framebuffer.

  ES cannot read depth back from the window, so the renderer draws into a
  layer-owned framebuffer with a depth-stencil texture. The colour is blitted
  to the window at swap; the depth texture answers glReadPixels(GL_DEPTH_COMPONENT)
  (flare and sun probes) and glCopyTexImage2D of a depth format (shadow maps).
=============================================================================*/
#include "gles_internal.h"

static GLuint sSceneFBO, sSceneColor, sSceneDepth;   // colour renderbuffer, depth-stencil texture
static int sSceneW, sSceneH;

// The scene renders at FARCRY_RENDER_SIZE=WxH, or at FARCRY_RENDER_SCALE (0.25-1) of the window, never
// above the window, and is scaled up at the swap (letterboxed with FARCRY_RENDER_ASPECT=1). The Android
// engine sizes r_Width/r_Height the same way (SystemInit.cpp).
void GLES_RenderSize(int pw, int ph, int* w, int* h)
{
  *w = pw; *h = ph;
  const char* size = getenv("FARCRY_RENDER_SIZE");
  int rw = 0, rh = 0;
  if (size && sscanf(size, "%dx%d", &rw, &rh) == 2 && rw >= 64 && rh >= 64)
  {
    float fit = (float)pw / rw < (float)ph / rh ? (float)pw / rw : (float)ph / rh;
    if (fit < 1.0f) { rw = (int)(rw * fit); rh = (int)(rh * fit); }
    *w = rw; *h = rh;
    return;
  }
  float s = getenv("FARCRY_RENDER_SCALE") ? (float)atof(getenv("FARCRY_RENDER_SCALE")) : 1.0f;
  if (s >= 0.25f && s < 1.0f) { *w = (int)(pw * s); *h = (int)(ph * s); }
}
static void EnsureScene(int pw, int ph) { int w, h; GLES_RenderSize(pw, ph, &w, &h); GLES_SceneFBOEnsure(w, h); }
static GLuint sScratchFBO;

static void DestroyScene()
{
  if (sSceneFBO) es_glDeleteFramebuffers(1, &sSceneFBO);
  if (sSceneColor) es_glDeleteRenderbuffers(1, &sSceneColor);
  if (sSceneDepth) es_glDeleteTextures(1, &sSceneDepth);
  sSceneFBO = sSceneColor = sSceneDepth = 0;
}

bool GLES_SceneFBOEnsure(int w, int h)
{
  if (sSceneFBO && w == sSceneW && h == sSceneH) return true;
  DestroyScene();
  if (w < 1 || h < 1) return false;
  es_glGenRenderbuffers(1, &sSceneColor);
  es_glBindRenderbuffer(ES_RENDERBUFFER, sSceneColor);
  es_glRenderbufferStorage(ES_RENDERBUFFER, ES_RGBA8, w, h);
  es_glGenTextures(1, &sSceneDepth);
  es_glActiveTexture(ES_TEXTURE0 + GLES_SCRATCH_UNIT);
  es_glBindTexture(GL_TEXTURE_2D, sSceneDepth);
  es_glTexImage2D(GL_TEXTURE_2D, 0, ES_DEPTH24_STENCIL8, w, h, 0, ES_DEPTH_STENCIL, ES_UNSIGNED_INT_24_8, NULL);
  es_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  es_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  es_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  es_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  es_glBindTexture(GL_TEXTURE_2D, 0);
  es_glActiveTexture(ES_TEXTURE0 + GLES_NativeActiveUnit());
  es_glGenFramebuffers(1, &sSceneFBO);
  es_glBindFramebuffer(ES_FRAMEBUFFER, sSceneFBO);
  es_glFramebufferRenderbuffer(ES_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, ES_RENDERBUFFER, sSceneColor);
  es_glFramebufferTexture2D(ES_FRAMEBUFFER, ES_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, sSceneDepth, 0);
  GLenum status = es_glCheckFramebufferStatus(ES_FRAMEBUFFER);
  if (status != ES_FRAMEBUFFER_COMPLETE)
  {
    GLES_Log("GLES: scene framebuffer %dx%d incomplete (0x%x); drawing to the window, depth readback unavailable", w, h, status);
    DestroyScene();
    es_glBindFramebuffer(ES_FRAMEBUFFER, 0);
    return false;
  }
  sSceneW = w; sSceneH = h;
  // The renderer sets no viewport for the menus and videos and relies on the window's default one.
  es_glViewport(0, 0, w, h);
  es_glScissor(0, 0, w, h);
  GLES_Log("GLES: scene framebuffer %dx%d", w, h);
  return true;
}

#ifdef __ANDROID__
// The touch overlay draws from inside SDL_GL_SwapWindow (a swap callback in the OpenTouch SDL fork)
// with its own program, attributes, buffers, texture unit 0 and fixed state, and none of the
// layer's shadows see it. Snapshot the native state it touches and put everything back afterwards.
struct SOverlaySaved
{
  GLboolean blend, cull, depth, scissor, depthMask;
  GLint blendSrc, blendDst, elementBuffer, viewport[4], scissorBox[4];
};

static void SaveForOverlay(SOverlaySaved& s)
{
  s.blend = es_glIsEnabled(GL_BLEND);
  s.cull = es_glIsEnabled(GL_CULL_FACE);
  s.depth = es_glIsEnabled(GL_DEPTH_TEST);
  s.scissor = es_glIsEnabled(GL_SCISSOR_TEST);
  es_glGetBooleanv(GL_DEPTH_WRITEMASK, &s.depthMask);
  es_glGetIntegerv(0x80C9 /*GL_BLEND_SRC_RGB*/, &s.blendSrc);
  es_glGetIntegerv(0x80C8 /*GL_BLEND_DST_RGB*/, &s.blendDst);
  es_glGetIntegerv(0x8895 /*GL_ELEMENT_ARRAY_BUFFER_BINDING*/, &s.elementBuffer);
  es_glGetIntegerv(GL_VIEWPORT, s.viewport);
  es_glGetIntegerv(GL_SCISSOR_BOX, s.scissorBox);
}

static void RestoreAfterOverlay(const SOverlaySaved& s)
{
  if (s.blend) es_glEnable(GL_BLEND); else es_glDisable(GL_BLEND);
  if (s.cull) es_glEnable(GL_CULL_FACE); else es_glDisable(GL_CULL_FACE);
  if (s.depth) es_glEnable(GL_DEPTH_TEST); else es_glDisable(GL_DEPTH_TEST);
  if (s.scissor) es_glEnable(GL_SCISSOR_TEST); else es_glDisable(GL_SCISSOR_TEST);
  es_glDepthMask(s.depthMask);
  es_glBlendFunc(s.blendSrc, s.blendDst);
  es_glBindBuffer(ES_ELEMENT_ARRAY_BUFFER, s.elementBuffer);
  es_glViewport(s.viewport[0], s.viewport[1], s.viewport[2], s.viewport[3]);
  es_glScissor(s.scissorBox[0], s.scissorBox[1], s.scissorBox[2], s.scissorBox[3]);
  GLES_ReissueProgram();
  GLES_ReissueAttribArrays();
  GLES_ReissueTextureUnit0();
}
#endif

void GLES_SwapWindow(SDL_Window* win)
{
  extern bool g_esDumpDraw;
  g_esDumpDraw = getenv("FARCRY_GLES_DUMPDRAW") && atoi(getenv("FARCRY_GLES_DUMPDRAW"));
  void GLES_WatchTexture(const char* what, GLuint id);
  GLES_WatchTexture("frame", 0);
  void GLES_RingEndFrame();
  GLES_RingEndFrame();
  int pw = 0, ph = 0;
  SDL_GetWindowSizeInPixels(win, &pw, &ph);
  if (sSceneFBO)
  {
    // The blit honours the scissor; the renderer leaves one set.
    bool scissor = es_glIsEnabled(GL_SCISSOR_TEST) != 0;
    if (scissor) es_glDisable(GL_SCISSOR_TEST);
    es_glBindFramebuffer(ES_READ_FRAMEBUFFER, sSceneFBO);
    // The frame's depth-stencil is finished with: a tiler then skips writing it back to memory.
    static const GLenum depthStencil[] = { 0x821A /* DEPTH_STENCIL_ATTACHMENT */ };
    es_glInvalidateFramebuffer(ES_READ_FRAMEBUFFER, 1, depthStencil);
    es_glBindFramebuffer(ES_DRAW_FRAMEBUFFER, 0);
    int x0 = 0, y0 = 0, x1 = pw, y1 = ph;
    static bool aspect = getenv("FARCRY_RENDER_ASPECT") && atoi(getenv("FARCRY_RENDER_ASPECT"));
    if (aspect && (long long)sSceneW * ph != (long long)sSceneH * pw)
    {
      // Letterbox: the largest rect of the scene's aspect, centred, with black bars.
      if ((long long)sSceneW * ph > (long long)sSceneH * pw) { int h = (int)((long long)pw * sSceneH / sSceneW); y0 = (ph - h) / 2; y1 = y0 + h; }
      else { int w = (int)((long long)ph * sSceneW / sSceneH); x0 = (pw - w) / 2; x1 = x0 + w; }
      GLboolean mask[4]; GLfloat clear[4];
      es_glGetBooleanv(GL_COLOR_WRITEMASK, mask);
      es_glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
      es_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
      es_glClearColor(0, 0, 0, 1);
      es_glClear(GL_COLOR_BUFFER_BIT);
      es_glColorMask(mask[0], mask[1], mask[2], mask[3]);
      es_glClearColor(clear[0], clear[1], clear[2], clear[3]);
    }
    bool same = sSceneW == x1 - x0 && sSceneH == y1 - y0;
    es_glBlitFramebuffer(0, 0, sSceneW, sSceneH, x0, y0, x1, y1, GL_COLOR_BUFFER_BIT, same ? GL_NEAREST : GL_LINEAR);
    // If the surface still has alpha, the compositor honours it (macOS/ANGLE, Android): the sky
    // pass leaves alpha 0 and shows black on screen while screenshots look fine. Write alpha 1.
    static int alphaBits = -1;
    if (alphaBits < 0) { alphaBits = 0; SDL_GL_GetAttribute(SDL_GL_ALPHA_SIZE, &alphaBits); GLES_Log("GLES: window surface alpha bits %d", alphaBits); }
    if (alphaBits > 0)
    {
      GLboolean mask[4]; GLfloat clear[4];
      es_glGetBooleanv(GL_COLOR_WRITEMASK, mask);
      es_glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
      es_glBindFramebuffer(ES_FRAMEBUFFER, 0);
      es_glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
      es_glClearColor(0, 0, 0, 1);
      es_glClear(GL_COLOR_BUFFER_BIT);
      es_glColorMask(mask[0], mask[1], mask[2], mask[3]);
      es_glClearColor(clear[0], clear[1], clear[2], clear[3]);
    }
    if (scissor) es_glEnable(GL_SCISSOR_TEST);
  }
#ifdef __ANDROID__
  SOverlaySaved saved;
  SaveForOverlay(saved);
  SDL_GL_SwapWindow(win);
  RestoreAfterOverlay(saved);
#else
  SDL_GL_SwapWindow(win);
#endif
  EnsureScene(pw, ph);
  es_glBindFramebuffer(ES_FRAMEBUFFER, sSceneFBO);
}

//////////////////////////////////////////////////////////////////////////
// Depth readback: a full-screen triangle packs the depth texture into RGBA8 (24 bits) in a small
// colour target, which ES can read.
//////////////////////////////////////////////////////////////////////////

static GLuint sDepthProg, sDepthFBO, sDepthTex;
static int sDepthTexW, sDepthTexH;
static GLint sUOrigin;
static bool sDepthProgFailed;

static const char* sDepthVS =
  "#version 300 es\n"
  "void main() { gl_Position = vec4(float((gl_VertexID & 1) * 4 - 1), float((gl_VertexID & 2) * 2 - 1), 0.0, 1.0); }\n";
static const char* sDepthFS =
  "#version 300 es\n"
  "precision highp float; precision highp sampler2D;\n"
  "uniform sampler2D u_depth; uniform vec2 u_origin;\n"
  "out vec4 o;\n"
  "void main() {\n"
  "  float d = texelFetch(u_depth, ivec2(u_origin) + ivec2(gl_FragCoord.xy), 0).r;\n"
  "  uint v = min(uint(clamp(d, 0.0, 1.0) * 16777216.0), 16777215u);\n"   // 24-bit fixed point; avoids float overflow at 1.0
  "  o = vec4(float(v & 255u), float((v >> 8) & 255u), float((v >> 16) & 255u), 255.0) / 255.0;\n"
  "}\n";

static bool BuildDepthProgram()
{
  if (sDepthProg) return true;
  if (sDepthProgFailed) return false;
  GLuint vs = es_glCreateShader(ES_VERTEX_SHADER), fs = es_glCreateShader(ES_FRAGMENT_SHADER);
  es_glShaderSource(vs, 1, &sDepthVS, NULL); es_glCompileShader(vs);
  es_glShaderSource(fs, 1, &sDepthFS, NULL); es_glCompileShader(fs);
  GLuint prog = es_glCreateProgram();
  es_glAttachShader(prog, vs); es_glAttachShader(prog, fs);
  es_glLinkProgram(prog);
  es_glDeleteShader(vs); es_glDeleteShader(fs);
  GLint ok = 0;
  es_glGetProgramiv(prog, ES_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[1024];
    es_glGetProgramInfoLog(prog, sizeof(log), NULL, log);
    GLES_Log("GLES: depth readback program failed to link: %s", log);
    es_glDeleteProgram(prog);
    sDepthProgFailed = true;
    return false;
  }
  sUOrigin = es_glGetUniformLocation(prog, "u_origin");
  es_glUseProgram(prog);
  es_glUniform1i(es_glGetUniformLocation(prog, "u_depth"), GLES_SCRATCH_UNIT);
  es_glUseProgram(GLES_CurrentProgram());
  es_glGenFramebuffers(1, &sDepthFBO);
  sDepthProg = prog;
  return true;
}

bool GLES_ReadDepth(int x, int y, int w, int h, float* out)
{
  if (!sSceneFBO || w < 1 || h < 1 || !BuildDepthProgram()) return false;
  GLint prevFBO = 0, vp[4], pack = 4;
  GLboolean mask[4];
  es_glGetIntegerv(ES_FRAMEBUFFER_BINDING, &prevFBO);
  es_glGetIntegerv(GL_VIEWPORT, vp);
  es_glGetIntegerv(ES_PACK_ALIGNMENT, &pack);
  es_glGetBooleanv(GL_COLOR_WRITEMASK, mask);
  static const GLenum caps[] = { GL_SCISSOR_TEST, GL_DEPTH_TEST, GL_STENCIL_TEST, GL_BLEND, GL_CULL_FACE };
  bool was[5];
  for (int i = 0; i < 5; i++) { was[i] = es_glIsEnabled(caps[i]) != 0; if (was[i]) es_glDisable(caps[i]); }

  es_glActiveTexture(ES_TEXTURE0 + GLES_SCRATCH_UNIT);
  if (w > sDepthTexW || h > sDepthTexH)
  {
    if (!sDepthTex) es_glGenTextures(1, &sDepthTex);
    sDepthTexW = w > sDepthTexW ? w : sDepthTexW;
    sDepthTexH = h > sDepthTexH ? h : sDepthTexH;
    es_glBindTexture(GL_TEXTURE_2D, sDepthTex);
    es_glTexImage2D(GL_TEXTURE_2D, 0, ES_RGBA8, sDepthTexW, sDepthTexH, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    es_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    es_glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    es_glBindFramebuffer(ES_FRAMEBUFFER, sDepthFBO);
    es_glFramebufferTexture2D(ES_FRAMEBUFFER, ES_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sDepthTex, 0);
  }
  es_glBindTexture(GL_TEXTURE_2D, sSceneDepth);
  es_glBindFramebuffer(ES_FRAMEBUFFER, sDepthFBO);
  es_glViewport(0, 0, w, h);
  es_glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  es_glUseProgram(sDepthProg);
  es_glUniform2f(sUOrigin, (float)x, (float)y);
  GLES_SuspendAttribArrays(true);
  es_glDrawArrays(GL_TRIANGLES, 0, 3);
  GLES_SuspendAttribArrays(false);
  std::vector<GLubyte> px(w * h * 4);
  es_glPixelStorei(ES_PACK_ALIGNMENT, 1);
  es_glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, &px[0]);
  es_glPixelStorei(ES_PACK_ALIGNMENT, pack);

  es_glBindTexture(GL_TEXTURE_2D, 0);
  es_glActiveTexture(ES_TEXTURE0 + GLES_NativeActiveUnit());
  es_glUseProgram(GLES_CurrentProgram());
  es_glBindFramebuffer(ES_FRAMEBUFFER, prevFBO);
  es_glViewport(vp[0], vp[1], vp[2], vp[3]);
  es_glColorMask(mask[0], mask[1], mask[2], mask[3]);
  for (int i = 0; i < 5; i++) if (was[i]) es_glEnable(caps[i]);

  for (int i = 0; i < w * h; i++)
  {
    const GLubyte* p = &px[i * 4];
    out[i] = (float)(p[0] | (p[1] << 8) | (p[2] << 16)) / 16777215.0f;
  }
  static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
  if (debug) { static int n; if (n++ < 8) GLES_Log("GLES: depth read (%d,%d) %dx%d -> %f", x, y, w, h, out[0]); }
  return true;
}

// Blit the scene depth into a depth-stencil texture level (same format, so the blit is legal).
bool GLES_CopySceneDepth(GLuint tex, GLenum target, int level, int x, int y, int w, int h)
{
  if (!sSceneFBO) return false;
  GLint prevFBO = 0;
  es_glGetIntegerv(ES_FRAMEBUFFER_BINDING, &prevFBO);
  bool scissor = es_glIsEnabled(GL_SCISSOR_TEST) != 0;
  if (scissor) es_glDisable(GL_SCISSOR_TEST);
  if (!sScratchFBO) es_glGenFramebuffers(1, &sScratchFBO);
  es_glBindFramebuffer(ES_DRAW_FRAMEBUFFER, sScratchFBO);
  es_glFramebufferTexture2D(ES_DRAW_FRAMEBUFFER, ES_DEPTH_STENCIL_ATTACHMENT, target, tex, level);
  es_glBindFramebuffer(ES_READ_FRAMEBUFFER, sSceneFBO);
  GLenum status = es_glCheckFramebufferStatus(ES_DRAW_FRAMEBUFFER);
  if (status == ES_FRAMEBUFFER_COMPLETE)
    es_glBlitFramebuffer(x, y, x + w, y + h, 0, 0, w, h, GL_DEPTH_BUFFER_BIT, GL_NEAREST);
  else
    GLES_Log("GLES: depth copy target texture %u incomplete (0x%x)", tex, status);
  es_glFramebufferTexture2D(ES_DRAW_FRAMEBUFFER, ES_DEPTH_STENCIL_ATTACHMENT, target, 0, 0);
  es_glBindFramebuffer(ES_FRAMEBUFFER, prevFBO);
  if (scissor) es_glEnable(GL_SCISSOR_TEST);
  return status == ES_FRAMEBUFFER_COMPLETE;
}
