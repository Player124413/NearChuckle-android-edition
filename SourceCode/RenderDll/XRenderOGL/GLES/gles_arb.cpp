/*=============================================================================
  gles_arb.cpp : ARB program objects, binding and environment parameters.
=============================================================================*/
#include "gles_arb.h"

static std::map<GLuint, SARBProgram> sPrograms;
static GLuint sNextId = 1;
static float sEnv[2][96][4];   // [vertex ? 0 : 1]
static unsigned sEnvVersion[2] = { 1, 1 };

// Two recent lookups: every draw asks for the bound vertex and fragment program.
static GLuint sFindId[2];
static SARBProgram* sFindPtr[2];
static int sFindNext;

static SARBProgram* Find(GLuint id)
{
  if (id && id == sFindId[0]) return sFindPtr[0];
  if (id && id == sFindId[1]) return sFindPtr[1];
  std::map<GLuint, SARBProgram>::iterator it = sPrograms.find(id);
  if (it == sPrograms.end()) return NULL;
  sFindId[sFindNext] = id; sFindPtr[sFindNext] = &it->second; sFindNext ^= 1;
  return &it->second;
}

static void __stdcall gles_glGenProgramsARB(GLsizei n, GLuint* ids)
{
  for (GLsizei i = 0; i < n; i++)
  {
    ids[i] = sNextId++;
    SARBProgram& p = sPrograms[ids[i]];
    memset(&p, 0, sizeof(GLuint) * 0); // members below
    p.id = ids[i]; p.vertex = false; p.valid = false; p.shader = 0; p.maxEnv = 0; p.fogMode = 0; p.writesFog = false;
    memset(p.texTarget, 0, sizeof(p.texTarget));
    memset(p.usesTexcoord, 0, sizeof(p.usesTexcoord));
  }
}

static void __stdcall gles_glDeleteProgramsARB(GLsizei n, const GLuint* ids)
{
  for (GLsizei i = 0; i < n; i++)
  {
    SARBProgram* p = Find(ids[i]);
    if (!p) continue;
    if (p->shader) es_glDeleteShader(p->shader);
    void GLES_ForgetARBProgram(GLuint);
    GLES_ForgetARBProgram(ids[i]);
    sPrograms.erase(ids[i]);
    sFindId[0] = sFindId[1] = 0;
    if (g_es.boundVP == ids[i]) g_es.boundVP = 0;
    if (g_es.boundFP == ids[i]) g_es.boundFP = 0;
  }
}

static void __stdcall gles_glBindProgramARB(GLenum target, GLuint id)
{
  if (target == ES_VERTEX_PROGRAM_ARB) g_es.boundVP = id;
  else if (target == ES_FRAGMENT_PROGRAM_ARB) g_es.boundFP = id;
  if (id && !Find(id))
  {
    SARBProgram& p = sPrograms[id];
    p.id = id; p.vertex = (target == ES_VERTEX_PROGRAM_ARB); p.valid = false; p.shader = 0; p.maxEnv = 0; p.fogMode = 0; p.writesFog = false;
    memset(p.texTarget, 0, sizeof(p.texTarget));
    memset(p.usesTexcoord, 0, sizeof(p.usesTexcoord));
  }
}

static void __stdcall gles_glProgramStringARB(GLenum target, GLenum format, GLsizei len, const GLvoid* string)
{
  GLuint id = (target == ES_VERTEX_PROGRAM_ARB) ? g_es.boundVP : g_es.boundFP;
  SARBProgram* p = Find(id);
  if (!p || format != 0x8875 /* GL_PROGRAM_FORMAT_ASCII_ARB */) return;
  p->vertex = (target == ES_VERTEX_PROGRAM_ARB);
  p->source.assign((const char*)string, len);
  if (p->shader) { es_glDeleteShader(p->shader); p->shader = 0; }
  { void GLES_ForgetARBProgram(GLuint); GLES_ForgetARBProgram(id); }
  static int specs = 0;
  if (g_esDebug) GLES_Log("GLES: ARB %s program %u specified (spec #%d)", target == ES_VERTEX_PROGRAM_ARB ? "vertex" : "fragment", id, ++specs);
  std::string err;
  p->valid = GLES_ARB_Translate(*p, err);
  if (!p->valid)
    GLES_Log("GLES: ARB %s program %u: %s\n%s", p->vertex ? "vertex" : "fragment", id, err.c_str(), p->source.c_str());
  else
  {
    static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
    if (debug) GLES_Log("GLES: ARB %s program %u source:\n%s\ntranslated:\n%s", p->vertex ? "vertex" : "fragment", id, p->source.c_str(), p->glsl.c_str());
  }
}

// For the draw dump: the first few environment constants of each program target.
const char* GLES_ARB_EnvDesc()
{
  static char buf[1024];
  int n = 0;
  for (int t = 0; t < 2; t++)
    for (int i = 0; i < (t ? 3 : 10) && n < (int)sizeof(buf) - 80; i++)
      n += sprintf(buf + n, " %s%d=%.3g,%.3g,%.3g,%.3g", t ? "f" : "v", i, sEnv[t][i][0], sEnv[t][i][1], sEnv[t][i][2], sEnv[t][i][3]);
  return buf;
}

static void SetEnv(GLenum target, GLuint index, const float* v)
{
  if (index >= 96) return;
  int t = (target == ES_VERTEX_PROGRAM_ARB) ? 0 : 1;
  if (memcmp(sEnv[t][index], v, 16) == 0) return;
  memcpy(sEnv[t][index], v, 16);
  sEnvVersion[t]++;
}

static void __stdcall gles_glProgramEnvParameter4fvARB(GLenum target, GLuint index, const GLfloat* v) { SetEnv(target, index, v); }
static void __stdcall gles_glProgramEnvParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { float v[4] = { x, y, z, w }; SetEnv(target, index, v); }
static void __stdcall gles_glProgramEnvParameter4dvARB(GLenum target, GLuint index, const GLdouble* d) { float v[4] = { (float)d[0], (float)d[1], (float)d[2], (float)d[3] }; SetEnv(target, index, v); }
// The renderer rewrites program.local to program.env; treat them as the same registers.
static void __stdcall gles_glProgramLocalParameter4fvARB(GLenum target, GLuint index, const GLfloat* v) { SetEnv(target, index, v); }
static void __stdcall gles_glProgramLocalParameter4fARB(GLenum target, GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { float v[4] = { x, y, z, w }; SetEnv(target, index, v); }

static void __stdcall gles_glGetProgramivARB(GLenum, GLenum pname, GLint* params)
{
  switch (pname)
  {
    case 0x88B1 /* GL_PROGRAM_INSTRUCTIONS_ARB */: params[0] = 0; break;
    case 0x8874 /* GL_PROGRAM_ERROR_STRING_ARB */: params[0] = 0; break;
    case 0x8677 /* GL_PROGRAM_BINDING_ARB */: params[0] = 0; break;
    default: params[0] = 1024; break; // limits
  }
}

SARBProgram* GLES_ARB_Bound(bool vertex)
{
  if (vertex ? !g_es.vertexProgram : !g_es.fragmentProgram) return NULL;
  SARBProgram* p = Find(vertex ? g_es.boundVP : g_es.boundFP);
  return (p && p->valid) ? p : NULL;
}

const float* GLES_ARB_Env(bool vertex) { return &sEnv[vertex ? 0 : 1][0][0]; }
unsigned GLES_ARB_EnvVersion(bool vertex) { return sEnvVersion[vertex ? 0 : 1]; }
int GLES_ARB_EnvCount(bool) { return 96; }

GLuint GLES_ARB_Shader(SARBProgram& p) { return p.shader; }

void GLES_RegisterARB(std::map<std::string, void*>& t)
{
#define REG(name) t[#name] = (void*)gles_##name;
  REG(glGenProgramsARB) REG(glDeleteProgramsARB) REG(glBindProgramARB) REG(glProgramStringARB)
  REG(glProgramEnvParameter4fvARB) REG(glProgramEnvParameter4fARB) REG(glProgramEnvParameter4dvARB)
  REG(glProgramLocalParameter4fvARB) REG(glProgramLocalParameter4fARB) REG(glGetProgramivARB)
#undef REG
}
