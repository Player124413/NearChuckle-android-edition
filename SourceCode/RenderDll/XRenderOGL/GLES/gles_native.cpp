/*=============================================================================
  gles_native.cpp : native ES 3.0 entry points used inside the layer.
=============================================================================*/
#include "gles_internal.h"

#define ES_PROC(ret, name, params) PFN_##name es_##name;
ES_PROCS
#undef ES_PROC

static std::string sNativeExt;
double g_esTimers[GLES_T_COUNT];
int g_esCounts[GLES_T_COUNT];
bool g_esDebug = getenv("FARCRY_GLES_DEBUG") != NULL;

bool GLES_LoadNative()
{
  bool ok = true;
#define ES_PROC(ret, name, params) \
  es_##name = (PFN_##name)SDL_GL_GetProcAddress(#name); \
  if (!es_##name) { GLES_Log("GLES: native %s missing", #name); ok = false; }
  ES_PROCS
#undef ES_PROC
  return ok;
}

bool GLES_HasNativeExt(const char* name)
{
  if (sNativeExt.empty() && es_glGetString)
  {
    const char* e = (const char*)es_glGetString(GL_EXTENSIONS);
    sNativeExt = e ? e : " ";
  }
  return sNativeExt.find(name) != std::string::npos;
}

// The driver decodes DXT itself. Not on Android by default: Adreno 7xx drivers decode the game's
// gloss and normal maps wrongly (characters turn white); FARCRY_GLES_NATIVES3TC opts back in.
bool GLES_DriverS3TC()
{
  if (getenv("FARCRY_GLES_NOS3TC"))
    return false;
#ifdef __ANDROID__
  if (!getenv("FARCRY_GLES_NATIVES3TC"))
    return false;
#endif
  return GLES_HasNativeExt("GL_EXT_texture_compression_s3tc") || GLES_HasNativeExt("GL_ANGLE_texture_compression_dxt");
}

void GLES_Log(const char* fmt, ...)
{
  static char buf[16384]; // shader sources are logged whole under FARCRY_GLES_DEBUG
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  // stderr as well: the engine log is buffered and lost if the process is killed. On Android the
  // engine log already reaches logcat, so stderr only covers the time before it exists.
#ifdef __ANDROID__
  if (!iLog)
#endif
  fprintf(stderr, "%s\n", buf);
  if (iLog) iLog->Log("%s\n", buf);
}
