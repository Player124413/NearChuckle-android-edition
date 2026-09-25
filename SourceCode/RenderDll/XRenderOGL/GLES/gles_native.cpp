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

void GLES_Log(const char* fmt, ...)
{
  static char buf[16384]; // shader sources are logged whole under FARCRY_GLES_DEBUG
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);
  // stderr as well: the engine log is buffered and lost if the process is killed.
  fprintf(stderr, "%s\n", buf);
  if (iLog) iLog->Log("%s\n", buf);
}
