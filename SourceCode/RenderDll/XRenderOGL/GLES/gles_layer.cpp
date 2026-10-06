/*=============================================================================
  gles_layer.cpp : OpenGL ES 3.0 translation layer entry table.

  The renderer's crygl* table is filled from here: implemented entry points
  from the layer's modules, state-free forwards to the ES driver, and a
  logging stub for everything else. See GLES-PORT-PLAN.md.
=============================================================================*/
#include "gles_internal.h"
#include "gles_layer.h"
#include "gles_arb.h"
#include <signal.h>
#include <unistd.h>
#ifndef __ANDROID__
#include <execinfo.h>
#include <dlfcn.h>
#include <sys/ucontext.h>

// Development aid: print a native backtrace on a crash, since the engine has no handler on POSIX.
static void CrashHandler(int sig, siginfo_t* info, void* ctx)
{
  void* frames[64];
  int n = backtrace(frames, 64);
#if defined(__APPLE__) && defined(__aarch64__)
  // The faulting function is missing from the frame chain when it is a leaf; print its pc.
  void* pc = (void*)((ucontext_t*)ctx)->uc_mcontext->__ss.__pc;
  Dl_info di;
  if (dladdr(pc, &di))
    fprintf(stderr, "GLES: fault at %p (%s %s+%ld, image base %p), address %p\n", pc, di.dli_fname,
            di.dli_sname ? di.dli_sname : "?", (long)((char*)pc - (char*)di.dli_saddr), di.dli_fbase, info->si_addr);
#endif
  fprintf(stderr, "GLES: signal %d, backtrace:\n", sig);
  backtrace_symbols_fd(frames, n, STDERR_FILENO);
  if (iLog) iLog->Log("GLES: crashed with signal %d (backtrace on stderr)\n", sig);
  signal(sig, SIG_DFL);
  raise(sig);
}
#endif

//////////////////////////////////////////////////////////////////////////
// Stubs: one per entry in GLFuncs.h, generated with the X-macro.
//////////////////////////////////////////////////////////////////////////

static std::map<std::string, unsigned>& StubHits()
{
  static std::map<std::string, unsigned> hits;
  return hits;
}

static void GLES_StubHit(const char* name)
{
  unsigned& n = StubHits()[name];
  if (n++ == 0)
    GLES_Log("GLES: unimplemented %s called", name);
}

#define GL_EXT(name)
#define GL_PROC(ext,ret,func,parms) static ret __stdcall stub_##func parms { GLES_StubHit(#func); return (ret)0; }
#include "../GLFuncs.h"
#undef GL_EXT
#undef GL_PROC

// Built with statements: GLFuncs.h has typedefs between its GL_PROC lines.
static std::map<std::string, void*>& StubTable()
{
  static std::map<std::string, void*> table;
  if (table.empty())
  {
#define GL_EXT(name)
#define GL_PROC(ext,ret,func,parms) table[#func] = (void*)stub_##func;
#include "../GLFuncs.h"
#undef GL_EXT
#undef GL_PROC
  }
  return table;
}

//////////////////////////////////////////////////////////////////////////
// Driver report
//////////////////////////////////////////////////////////////////////////

static std::string sExtensions, sVendor, sRenderer, sVersion;
static bool sQueried;

// Lazy because the function table is filled before the context exists.
static void QueryNative()
{
  if (sQueried || !es_glGetString) return;
  sQueried = true;
  sVendor   = std::string("GLES layer / ") + (const char*)es_glGetString(GL_VENDOR);
  sRenderer = (const char*)es_glGetString(GL_RENDERER);
  sVersion  = std::string("2.1 (GLES layer on ") + (const char*)es_glGetString(GL_VERSION) + ")";

  // What the layer emulates, plus what the ES driver really has. The renderer
  // picks its ARB-program (PS 2.0 class) path from this list.
  sExtensions =
    "GL_ARB_multitexture GL_ARB_texture_env_combine GL_EXT_texture_env_combine GL_EXT_texture_env_add "
    "GL_ARB_texture_cube_map GL_EXT_texture_cube_map GL_ARB_vertex_program GL_ARB_fragment_program "
    "GL_ARB_vertex_buffer_object GL_EXT_stencil_two_side GL_EXT_stencil_wrap GL_NV_occlusion_query "
    "GL_ARB_texture_compression GL_SGIS_generate_mipmap GL_EXT_texture3D GL_EXT_bgra "
    "GL_EXT_secondary_color GL_EXT_draw_range_elements GL_EXT_texture_lod_bias GL_EXT_texture_rectangle ";
  // Depth shadow maps work through the scene framebuffer, but the reference GL renderer on macOS
  // has no SGIX depth textures either; opt in to compare.
  if (getenv("FARCRY_GLES_DEPTHMAPS"))
    sExtensions += "GL_SGIX_depth_texture GL_SGIX_shadow ";
  if (GLES_DriverS3TC())
    sExtensions += "GL_EXT_texture_compression_s3tc ";
  if (GLES_HasNativeExt("GL_EXT_texture_filter_anisotropic"))
    sExtensions += "GL_EXT_texture_filter_anisotropic ";
}

static const GLubyte* __stdcall gles_glGetString(GLenum name)
{
  QueryNative();
  switch (name)
  {
    case GL_VENDOR:     return (const GLubyte*)sVendor.c_str();
    case GL_RENDERER:   return (const GLubyte*)sRenderer.c_str();
    case GL_VERSION:    return (const GLubyte*)sVersion.c_str();
    case GL_EXTENSIONS: return (const GLubyte*)sExtensions.c_str();
    case 0x8874 /* GL_PROGRAM_ERROR_STRING_ARB */: return (const GLubyte*)"";
  }
  return es_glGetString ? es_glGetString(name) : (const GLubyte*)"";
}

//////////////////////////////////////////////////////////////////////////
// Straight forwards: identical semantics in ES 3.0. Renderer name -> ES name.
//////////////////////////////////////////////////////////////////////////

struct SForward { const char* name; const char* esName; };

static const SForward sForwardTable[] = {
  { "glClear", "glClear" }, { "glClearColor", "glClearColor" }, { "glClearStencil", "glClearStencil" },
  { "glViewport", "glViewport" }, { "glScissor", "glScissor" },
  { "glBlendFunc", "glBlendFunc" }, { "glDepthFunc", "glDepthFunc" }, { "glDepthMask", "glDepthMask" },
  { "glColorMask", "glColorMask" }, { "glCullFace", "glCullFace" }, { "glFrontFace", "glFrontFace" },

  { "glFinish", "glFinish" }, { "glFlush", "glFlush" }, { "glPolygonOffset", "glPolygonOffset" },
  { "glLineWidth", "glLineWidth" },
  { NULL, NULL }
};

static std::map<std::string, void*>& ImplTable()
{
  static std::map<std::string, void*> table;
  if (table.empty())
  {
    table["glGetString"] = (void*)gles_glGetString;
    GLES_RegisterState(table);
    GLES_RegisterTexture(table);
    GLES_RegisterVertex(table);
    GLES_RegisterFFP(table);
    GLES_RegisterARB(table);
    GLES_RegisterQuery(table);
  }
  return table;
}

//////////////////////////////////////////////////////////////////////////

bool GLES_Init()
{
#ifndef __ANDROID__
  // bionic has no execinfo; Android's runtime writes tombstones instead.
  struct sigaction sa = {};
  sa.sa_sigaction = CrashHandler;
  sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, NULL);
  sigaction(SIGBUS, &sa, NULL);
  sigaction(SIGABRT, &sa, NULL);
#endif
  if (!GLES_LoadNative())
    return false;
  GLES_InitState();
  sQueried = false;
  SDL_Window* win = SDL_GL_GetCurrentWindow();
  if (win)
  {
    int w = 0, h = 0, pw = 0, ph = 0;
    SDL_GetWindowSize(win, &w, &h);
    SDL_GetWindowSizeInPixels(win, &pw, &ph);
    GLES_Log("GLES: window %dx%d, drawable %dx%d", w, h, pw, ph);
    void GLES_RenderSize(int pw, int ph, int* w, int* h);
    int rw, rh;
    GLES_RenderSize(pw, ph, &rw, &rh);
    GLES_SceneFBOEnsure(rw, rh);
  }
  return true;
}

void* GLES_GetProcAddress(const char* name)
{
  static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
  std::map<std::string, void*>::iterator it = ImplTable().find(name);
  if (it != ImplTable().end())
  {
    if (debug) GLES_Log("GLES: %s -> layer", name);
    return it->second;
  }
  for (const SForward* f = sForwardTable; f->name; f++)
    if (!strcmp(f->name, name))
    {
      void* proc = (void*)SDL_GL_GetProcAddress(f->esName);
      if (proc)
        return proc;
      GLES_Log("GLES: driver lacks %s (for %s), using stub", f->esName, name);
      break;
    }
  it = StubTable().find(name);
  if (it != StubTable().end())
  {
    if (debug) GLES_Log("GLES: %s -> stub", name);
    return it->second;
  }
  GLES_Log("GLES: %s is not in GLFuncs.h", name);
  return NULL;
}

void GLES_ReportUnimplemented()
{
  std::map<std::string, unsigned>& hits = StubHits();
  GLES_Log("GLES: %d unimplemented entry points were called:", (int)hits.size());
  for (std::map<std::string, unsigned>::iterator it = hits.begin(); it != hits.end(); ++it)
    GLES_Log("GLES:   %-40s %u", it->first.c_str(), it->second);
}

void GLES_SetContextAttributes()
{
#ifdef __APPLE__
  // macOS has no ES; SDL loads ANGLE's EGL/GLESv2 from FARCRY_ANGLE_DIR or next to the executable.
  const char* dir = getenv("FARCRY_ANGLE_DIR");
  std::string base = dir ? std::string(dir) + "/" : std::string(SDL_GetBasePath());
  static std::string egl, gles;
  egl = base + "libEGL.dylib";
  gles = base + "libGLESv2.dylib";
  SDL_SetHint(SDL_HINT_EGL_LIBRARY, egl.c_str());
  SDL_SetHint(SDL_HINT_OPENGL_LIBRARY, gles.c_str());
  GLES_Log("GLES: ANGLE from %s", base.c_str());
#endif
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
  SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
  SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
  // The window only receives the scene framebuffer blit; an alpha channel on it is composited
  // by the OS on ES (ANGLE/Metal, Android), turning any pixel with alpha < 1 black.
  SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 0);
  SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}
