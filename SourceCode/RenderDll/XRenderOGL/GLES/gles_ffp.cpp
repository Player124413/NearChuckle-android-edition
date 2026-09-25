/*=============================================================================
  gles_ffp.cpp : per-draw program selection. Pairs the vertex and fragment
  halves (ARB program translated by gles_arb_translate.cpp, or a generated
  fixed-function half) into a GLSL ES 3.00 program and uploads its uniforms.

  Interface shared by all halves:
    attributes a_pos, a_normal, a_color, a_color2, a_texN (vec4)
    varyings   v_color, v_color2, v_texN (vec4), v_fogc (float)
=============================================================================*/
#include "gles_internal.h"
#include "gles_arb.h"
#include <stdio.h>

enum { UNIT_OFF = 0, UNIT_2D, UNIT_RECT, UNIT_CUBE, UNIT_3D };

// Fixed-function state a generated half depends on.
struct SFFPKey
{
  unsigned char unitType[GLES_MAX_UNITS];
  unsigned char texMatrix[GLES_MAX_UNITS];
  STexEnv env[GLES_MAX_UNITS];
  unsigned char alphaTest, alphaFunc;
  unsigned char fogMode;   // 0 off, 1 linear, 2 exp, 3 exp2
  unsigned char texGenMode[GLES_MAX_UNITS];  // 0 off, else GL_OBJECT_LINEAR.. as TG_*
  unsigned char texGenMask[GLES_MAX_UNITS];  // bit per coordinate S,T,R,Q
  unsigned char lightMask, colorMaterial, clipMask;
};

enum { TG_OFF = 0, TG_OBJECT, TG_EYE, TG_SPHERE, TG_REFLECTION, TG_NORMAL };

struct SPipeKey
{
  GLuint vp, fp;           // ARB program ids, 0 = fixed function half
  SFFPKey ffp;
  bool operator<(const SPipeKey& o) const { return memcmp(this, &o, sizeof(*this)) < 0; }
};

struct SPipeProgram
{
  GLuint prog;
  GLint uMVP, uMV, uTexMat[GLES_MAX_UNITS], uEnvColor[GLES_MAX_UNITS], uRectScale[GLES_MAX_UNITS];
  GLint uAlphaRef, uFogColor, uFogParams, uEnvV, uEnvF;
  GLint uObjPlane, uEyePlane, uClipPlane;
  GLint uLightPos, uLightAmb, uLightDiff, uLightSpec, uLightAtt;
  GLint uMatAmb, uMatDiff, uMatSpec, uMatEmis, uShininess, uGlobalAmb;
  int envVCount, envFCount;
  unsigned envVersionV, envVersionF;   // env state version last uploaded
};

static std::map<SPipeKey, SPipeProgram> sPrograms;
// Last program found: consecutive draws mostly repeat the key, which saves the map's memcmp walk.
static std::map<SPipeKey, SPipeProgram>::iterator sLastProg;
static bool sLastProgValid;

static std::map<std::string, GLuint> sShaders;   // compiled shader per source text
static GLuint sCurrentProg;
GLuint GLES_CurrentProgram() { return sCurrentProg; }
static const SPipeKey* sCurrentKey;

// An ARB program id that is re-specified or deleted invalidates every linked pipeline built with it;
// the renderer reuses ids (shader variants, reloads), so a stale link draws with the wrong shader.
void GLES_ForgetARBProgram(GLuint id)
{
  for (std::map<SPipeKey, SPipeProgram>::iterator it = sPrograms.begin(); it != sPrograms.end();)
  {
    if (it->first.vp == id || it->first.fp == id)
    {
      if (it->second.prog) es_glDeleteProgram(it->second.prog);
      if (sCurrentProg == it->second.prog) { sCurrentProg = 0; sCurrentKey = NULL; }
      sPrograms.erase(it++);
      sLastProgValid = false;
    }
    else ++it;
  }
}

// For the draw dump: the key of the program in use.
const char* GLES_CurrentProgramDesc()
{
  static char buf[64];
  if (!sCurrentKey) return "none";
  sprintf(buf, "vp %u fp %u alpha %d", sCurrentKey->vp, sCurrentKey->fp, sCurrentKey->ffp.alphaTest);
  return buf;
}

static const char* UnitSampler(int type)
{
  switch (type) { case UNIT_CUBE: return "samplerCube"; case UNIT_3D: return "sampler3D"; default: return "sampler2D"; }
}

//////////////////////////////////////////////////////////////////////////
// Fixed-function halves
//////////////////////////////////////////////////////////////////////////

static void AppendArg(std::string& s, int unit, GLenum src, GLenum op, bool alpha)
{
  char buf[64];
  std::string v;
  if (src == GL_TEXTURE) { sprintf(buf, "t%d", unit); v = buf; }
  else if (src >= ES_TEXTURE0 && src < ES_TEXTURE0 + GLES_MAX_UNITS) { sprintf(buf, "t%d", (int)(src - ES_TEXTURE0)); v = buf; }
  else if (src == ES_CONSTANT) { sprintf(buf, "u_envColor[%d]", unit); v = buf; }
  else if (src == ES_PRIMARY_COLOR) v = "v_color";
  else v = "prev"; // GL_PREVIOUS
  switch (op)
  {
    case GL_SRC_COLOR:           s += alpha ? v + ".a" : v + ".rgb"; break;
    case GL_ONE_MINUS_SRC_COLOR: s += alpha ? "(1.0-" + v + ".a)" : "(vec3(1.0)-" + v + ".rgb)"; break;
    case GL_SRC_ALPHA:           s += alpha ? v + ".a" : "vec3(" + v + ".a)"; break;
    case GL_ONE_MINUS_SRC_ALPHA: s += alpha ? "(1.0-" + v + ".a)" : "vec3(1.0-" + v + ".a)"; break;
    default:                     s += alpha ? v + ".a" : v + ".rgb"; break;
  }
}

static void AppendCombine(std::string& s, int unit, GLenum func, const GLenum* src, const GLenum* op, float scale, bool alpha)
{
  std::string a[3];
  for (int i = 0; i < 3; i++) AppendArg(a[i], unit, src[i], op[i], alpha);
  std::string r;
  switch (func)
  {
    case GL_REPLACE:      r = a[0]; break;
    case GL_MODULATE:     r = a[0] + "*" + a[1]; break;
    case GL_ADD:          r = a[0] + "+" + a[1]; break;
    case ES_ADD_SIGNED:   r = a[0] + "+" + a[1] + (alpha ? "-0.5" : "-vec3(0.5)"); break;
    case ES_INTERPOLATE:  r = a[0] + "*" + a[2] + "+" + a[1] + "*" + (alpha ? "(1.0-" + a[2] + ")" : "(vec3(1.0)-" + a[2] + ")"); break;
    case ES_SUBTRACT:     r = a[0] + "-" + a[1]; break;
    case ES_DOT3_RGB: case ES_DOT3_RGBA:
      r = alpha ? a[0] : "vec3(4.0*dot(" + a[0] + "-vec3(0.5)," + a[1] + "-vec3(0.5)))"; break;
    default:              r = a[0] + "*" + a[1]; break;
  }
  char buf[32];
  if (scale != 1.0f) { sprintf(buf, "*%.1f", scale); r = "(" + r + ")" + buf; }
  s += r;
}

static std::string CommonVertexHeader()
{
  std::string s = "#version 300 es\n";
  s += "in vec4 a_pos; in vec4 a_color; in vec4 a_normal; in vec4 a_color2;\n";
  s += "out vec4 v_color; out vec4 v_color2; out float v_fogc;\n";
  char buf[96];
  for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "in vec4 a_tex%d; out vec4 v_tex%d;\n", i, i); s += buf; }
  return s;
}

// Transforms with the matrix stacks; writes every varying so any fragment half can link against it.
static std::string BuildFFPVertex(const SFFPKey& k)
{
  std::string s = CommonVertexHeader();
  s += "uniform mat4 u_mvp; uniform mat4 u_mv;\n";
  s += "uniform vec4 u_objPlane[32]; uniform vec4 u_eyePlane[32]; uniform vec4 u_clipPlane[6];\n";
  s += "uniform vec4 u_lightPos[4]; uniform vec4 u_lightAmb[4]; uniform vec4 u_lightDiff[4]; uniform vec4 u_lightSpec[4]; uniform vec4 u_lightAtt[4];\n";
  s += "uniform vec4 u_matAmb; uniform vec4 u_matDiff; uniform vec4 u_matSpec; uniform vec4 u_matEmis; uniform float u_shininess; uniform vec4 u_globalAmb;\n";
  char buf[1024]; // the lighting block below is long
  for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "uniform mat4 u_texMat%d;\n", i); s += buf; }
  for (int i = 0; i < 6; i++) if (k.clipMask & (1 << i)) { sprintf(buf, "out float v_clip%d;\n", i); s += buf; }
  s += "void main(){\n gl_Position = u_mvp * a_pos;\n";
  s += " vec4 eyePos = u_mv * a_pos;\n vec3 eyeN = normalize(mat3(u_mv) * a_normal.xyz);\n";
  s += " vec3 eyeU = normalize(eyePos.xyz);\n vec3 refl = eyeU - 2.0 * eyeN * dot(eyeN, eyeU);\n";
  s += " v_fogc = -eyePos.z;\n v_color2 = a_color2;\n";
  if (k.lightMask)
  {
    s += k.colorMaterial ? " vec4 matAmb = a_color; vec4 matDiff = a_color;\n" : " vec4 matAmb = u_matAmb; vec4 matDiff = u_matDiff;\n";
    s += " vec3 lit = u_matEmis.rgb + u_globalAmb.rgb * matAmb.rgb;\n";
    for (int i = 0; i < GLES_MAX_LIGHTS; i++)
    {
      if (!(k.lightMask & (1 << i))) continue;
      sprintf(buf, " {\n  vec3 L; float att = 1.0;\n  if (u_lightPos[%d].w == 0.0) L = normalize(u_lightPos[%d].xyz);\n"
                   "  else { vec3 d = u_lightPos[%d].xyz - eyePos.xyz; float dist = length(d); L = d / dist; att = 1.0 / (u_lightAtt[%d].x + u_lightAtt[%d].y * dist + u_lightAtt[%d].z * dist * dist); }\n"
                   "  float ndl = max(dot(eyeN, L), 0.0);\n  vec3 h = normalize(L + vec3(0.0, 0.0, 1.0));\n"
                   "  float sp = (ndl > 0.0) ? pow(max(dot(eyeN, h), 0.0), u_shininess) : 0.0;\n"
                   "  lit += att * (u_lightAmb[%d].rgb * matAmb.rgb + ndl * u_lightDiff[%d].rgb * matDiff.rgb + sp * u_lightSpec[%d].rgb * u_matSpec.rgb);\n }\n", i, i, i, i, i, i, i, i, i);
      s += buf;
    }
    s += " v_color = vec4(clamp(lit, 0.0, 1.0), matDiff.a);\n";
  }
  else
    s += " v_color = a_color;\n";
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    sprintf(buf, " vec4 tc%d = a_tex%d;\n", i, i); s += buf;
    if (k.texGenMode[i])
    {
      s += " {\n  float m = 2.0 * sqrt(refl.x * refl.x + refl.y * refl.y + (refl.z + 1.0) * (refl.z + 1.0));\n";
      static const char* comp = "xyzw";
      for (int c = 0; c < 4; c++)
      {
        if (!(k.texGenMask[i] & (1 << c))) continue;
        switch (k.texGenMode[i])
        {
          case TG_OBJECT: sprintf(buf, "  tc%d.%c = dot(u_objPlane[%d], a_pos);\n", i, comp[c], i * 4 + c); break;
          case TG_EYE:    sprintf(buf, "  tc%d.%c = dot(u_eyePlane[%d], eyePos);\n", i, comp[c], i * 4 + c); break;
          case TG_SPHERE: sprintf(buf, "  tc%d.%c = refl.%c / m + 0.5;\n", i, comp[c], comp[c < 2 ? c : 0]); break;
          case TG_REFLECTION: sprintf(buf, "  tc%d.%c = refl.%c;\n", i, comp[c], comp[c < 3 ? c : 2]); break;
          case TG_NORMAL: sprintf(buf, "  tc%d.%c = eyeN.%c;\n", i, comp[c], comp[c < 3 ? c : 2]); break;
          default: buf[0] = 0; break;
        }
        s += buf;
      }
      s += " }\n";
    }
    if (k.texMatrix[i]) sprintf(buf, " v_tex%d = u_texMat%d * tc%d;\n", i, i, i);
    else sprintf(buf, " v_tex%d = tc%d;\n", i, i);
    s += buf;
  }
  for (int i = 0; i < 6; i++) if (k.clipMask & (1 << i)) { sprintf(buf, " v_clip%d = dot(u_clipPlane[%d], eyePos);\n", i, i); s += buf; }
  s += "}\n";
  return s;
}

static std::string AlphaTestCode(const SFFPKey& k, const char* var)
{
  if (!k.alphaTest) return "";
  const char* cmp = "true";
  char buf[64];
  switch (k.alphaFunc + GL_NEVER)
  {
    case GL_NEVER: cmp = "false"; break;
    case GL_LESS: sprintf(buf, "%s.a < u_alphaRef", var); cmp = buf; break;
    case GL_EQUAL: sprintf(buf, "%s.a == u_alphaRef", var); cmp = buf; break;
    case GL_LEQUAL: sprintf(buf, "%s.a <= u_alphaRef", var); cmp = buf; break;
    case GL_GREATER: sprintf(buf, "%s.a > u_alphaRef", var); cmp = buf; break;
    case GL_NOTEQUAL: sprintf(buf, "%s.a != u_alphaRef", var); cmp = buf; break;
    case GL_GEQUAL: sprintf(buf, "%s.a >= u_alphaRef", var); cmp = buf; break;
  }
  return std::string(" if (!(") + cmp + ")) discard;\n";
}

static std::string BuildFFPFragment(const SFFPKey& k)
{
  std::string s = "#version 300 es\nprecision highp float;\nprecision highp sampler3D;\n";
  s += "in vec4 v_color; in vec4 v_color2; in float v_fogc;\n";
  s += "uniform vec4 u_envColor[8]; uniform vec2 u_rectScale[8]; uniform float u_alphaRef;\n";
  s += "uniform vec4 u_fogColor; uniform vec4 u_fogParams;\nout vec4 fragColor;\n";
  char buf[256];
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    if (k.unitType[i] == UNIT_OFF) continue;
    sprintf(buf, "in vec4 v_tex%d; uniform %s u_tex%d;\n", i, UnitSampler(k.unitType[i]), i);
    s += buf;
  }
  for (int i = 0; i < 6; i++) if (k.clipMask & (1 << i)) { sprintf(buf, "in float v_clip%d;\n", i); s += buf; }
  s += "void main(){\n";
  for (int i = 0; i < 6; i++) if (k.clipMask & (1 << i)) { sprintf(buf, " if (v_clip%d < 0.0) discard;\n", i); s += buf; }
  s += " vec4 prev = v_color;\n";
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    switch (k.unitType[i])
    {
      case UNIT_2D:   sprintf(buf, " vec4 t%d = textureProj(u_tex%d, vec3(v_tex%d.xy, v_tex%d.w));\n", i, i, i, i); break;
      case UNIT_RECT: sprintf(buf, " vec4 t%d = texture(u_tex%d, v_tex%d.xy / v_tex%d.w * u_rectScale[%d]);\n", i, i, i, i, i); break;
      case UNIT_CUBE: case UNIT_3D: sprintf(buf, " vec4 t%d = texture(u_tex%d, v_tex%d.xyz);\n", i, i, i); break;
      default: continue;
    }
    s += buf;
  }
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    if (k.unitType[i] == UNIT_OFF) continue;
    const STexEnv& e = k.env[i];
    sprintf(buf, "t%d", i);
    std::string t = buf, rgb, a;
    if (e.mode == ES_COMBINE)
    {
      AppendCombine(rgb, i, e.combineRGB, e.srcRGB, e.opRGB, e.scaleRGB, false);
      if (e.combineRGB == ES_DOT3_RGBA) a = "(" + rgb + ").r";
      else AppendCombine(a, i, e.combineAlpha, e.srcAlpha, e.opAlpha, e.scaleAlpha, true);
    }
    else switch (e.mode)
    {
      case GL_REPLACE: rgb = t + ".rgb"; a = t + ".a"; break;
      case GL_DECAL:   rgb = "mix(prev.rgb, " + t + ".rgb, " + t + ".a)"; a = "prev.a"; break;
      case GL_BLEND:   sprintf(buf, "mix(prev.rgb, u_envColor[%d].rgb, %s.rgb)", i, t.c_str()); rgb = buf; a = "prev.a*" + t + ".a"; break;
      case GL_ADD:     rgb = "prev.rgb+" + t + ".rgb"; a = "prev.a*" + t + ".a"; break;
      default:         rgb = "prev.rgb*" + t + ".rgb"; a = "prev.a*" + t + ".a"; break; // MODULATE
    }
    s += " prev = clamp(vec4(" + rgb + ", " + a + "), 0.0, 1.0);\n";
  }
  s += " vec4 c = prev;\n";
  if (k.fogMode == 1) s += " float f = clamp((u_fogParams.y - v_fogc) * u_fogParams.z, 0.0, 1.0);\n c.rgb = mix(u_fogColor.rgb, c.rgb, f);\n";
  else if (k.fogMode == 2) s += " float f = clamp(exp(-u_fogParams.w * v_fogc), 0.0, 1.0);\n c.rgb = mix(u_fogColor.rgb, c.rgb, f);\n";
  else if (k.fogMode == 3) s += " float f = clamp(exp(-u_fogParams.w * u_fogParams.w * v_fogc * v_fogc), 0.0, 1.0);\n c.rgb = mix(u_fogColor.rgb, c.rgb, f);\n";
  s += AlphaTestCode(k, "c");
  s += " fragColor = c;\n}\n";
  return s;
}

// The GL alpha test still applies after an ARB fragment program.
static std::string ARBFragmentWithAlphaTest(const SARBProgram& fp, const SFFPKey& k)
{
  std::string s = fp.glsl;
  if (!k.alphaTest) return s;
  size_t pos = s.find("  fragColor = oColor;");
  if (pos == std::string::npos) return s;
  std::string inject = "uniform float u_alphaRef;\n";
  size_t hdr = s.find("out vec4 fragColor;");
  s.insert(hdr, inject);
  pos = s.find("  fragColor = oColor;");
  s.insert(pos, AlphaTestCode(k, "oColor"));
  return s;
}

//////////////////////////////////////////////////////////////////////////
// Compile and link
//////////////////////////////////////////////////////////////////////////

static GLuint Compile(GLenum type, const std::string& src)
{
  std::map<std::string, GLuint>::iterator it = sShaders.find(src);
  if (it != sShaders.end()) return it->second;
  GLuint sh = es_glCreateShader(type);
  const char* p = src.c_str();
  es_glShaderSource(sh, 1, &p, NULL);
  es_glCompileShader(sh);
  GLint ok = 0;
  es_glGetShaderiv(sh, ES_COMPILE_STATUS, &ok);
  if (!ok)
  {
    char log[4096];
    es_glGetShaderInfoLog(sh, sizeof(log), NULL, log);
    GLES_Log("GLES: %s shader failed:\n%s\n--- source ---\n%s", type == ES_VERTEX_SHADER ? "vertex" : "fragment", log, src.c_str());
  }
  sShaders[src] = sh;
  return sh;
}

static SPipeProgram Build(const SPipeKey& k, const SARBProgram* vp, const SARBProgram* fp)
{
  SPipeProgram p;
  memset(&p, 0, sizeof(p));
  std::string vsrc = vp ? vp->glsl : BuildFFPVertex(k.ffp);
  std::string fsrc = fp ? ARBFragmentWithAlphaTest(*fp, k.ffp) : BuildFFPFragment(k.ffp);
  static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
  if (debug) GLES_Log("GLES: program #%d (vp %u, fp %u) sources:\n%s\n%s", (int)sPrograms.size() + 1, k.vp, k.fp, vsrc.c_str(), fsrc.c_str());
  GLuint vs = Compile(ES_VERTEX_SHADER, vsrc);
  GLuint fs = Compile(ES_FRAGMENT_SHADER, fsrc);
  p.prog = es_glCreateProgram();
  es_glAttachShader(p.prog, vs);
  es_glAttachShader(p.prog, fs);
  es_glBindAttribLocation(p.prog, GLES_ATTR_POS, "a_pos");
  es_glBindAttribLocation(p.prog, GLES_ATTR_NORMAL, "a_normal");
  es_glBindAttribLocation(p.prog, GLES_ATTR_COLOR, "a_color");
  es_glBindAttribLocation(p.prog, GLES_ATTR_COLOR2, "a_color2");
  char buf[32];
  for (int i = 0; i < GLES_MAX_UNITS; i++) { sprintf(buf, "a_tex%d", i); es_glBindAttribLocation(p.prog, GLES_ATTR_TEX0 + i, buf); }
  es_glLinkProgram(p.prog);
  GLint ok = 0;
  es_glGetProgramiv(p.prog, ES_LINK_STATUS, &ok);
  if (!ok)
  {
    char log[4096];
    es_glGetProgramInfoLog(p.prog, sizeof(log), NULL, log);
    GLES_Log("GLES: program link failed (vp %u, fp %u): %s", k.vp, k.fp, log);
  }
  es_glUseProgram(p.prog);
  sCurrentProg = p.prog;
  p.uMVP = es_glGetUniformLocation(p.prog, "u_mvp");
  p.uMV = es_glGetUniformLocation(p.prog, "u_mv");
  p.uAlphaRef = es_glGetUniformLocation(p.prog, "u_alphaRef");
  p.uFogColor = es_glGetUniformLocation(p.prog, "u_fogColor");
  p.uFogParams = es_glGetUniformLocation(p.prog, "u_fogParams");
  p.uEnvV = es_glGetUniformLocation(p.prog, "envv");
  p.uEnvF = es_glGetUniformLocation(p.prog, "envf");
  p.uObjPlane = es_glGetUniformLocation(p.prog, "u_objPlane");
  p.uEyePlane = es_glGetUniformLocation(p.prog, "u_eyePlane");
  p.uClipPlane = es_glGetUniformLocation(p.prog, "u_clipPlane");
  p.uLightPos = es_glGetUniformLocation(p.prog, "u_lightPos");
  p.uLightAmb = es_glGetUniformLocation(p.prog, "u_lightAmb");
  p.uLightDiff = es_glGetUniformLocation(p.prog, "u_lightDiff");
  p.uLightSpec = es_glGetUniformLocation(p.prog, "u_lightSpec");
  p.uLightAtt = es_glGetUniformLocation(p.prog, "u_lightAtt");
  p.uMatAmb = es_glGetUniformLocation(p.prog, "u_matAmb");
  p.uMatDiff = es_glGetUniformLocation(p.prog, "u_matDiff");
  p.uMatSpec = es_glGetUniformLocation(p.prog, "u_matSpec");
  p.uMatEmis = es_glGetUniformLocation(p.prog, "u_matEmis");
  p.uShininess = es_glGetUniformLocation(p.prog, "u_shininess");
  p.uGlobalAmb = es_glGetUniformLocation(p.prog, "u_globalAmb");
  p.envVCount = vp ? vp->maxEnv : 0;
  p.envFCount = fp ? fp->maxEnv : 0;
  p.envVersionV = p.envVersionF = 0;
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    sprintf(buf, "u_texMat%d", i); p.uTexMat[i] = es_glGetUniformLocation(p.prog, buf);
    sprintf(buf, "u_envColor[%d]", i); p.uEnvColor[i] = es_glGetUniformLocation(p.prog, buf);
    sprintf(buf, "u_rectScale[%d]", i); p.uRectScale[i] = es_glGetUniformLocation(p.prog, buf);
    sprintf(buf, "u_tex%d", i);
    GLint loc = es_glGetUniformLocation(p.prog, buf);
    if (loc >= 0) es_glUniform1i(loc, i);
  }
  return p;
}

//////////////////////////////////////////////////////////////////////////
// Per-draw state
//////////////////////////////////////////////////////////////////////////

static STextureObj* TextureForARBUnit(int unit, int target)
{
  STexUnitState& u = g_es.unit[unit];
  GLuint id = 0;
  switch (target)
  {
    case ARB_TEX_2D: case ARB_TEX_SHADOW2D: id = u.bound2D; break;
    case ARB_TEX_RECT: case ARB_TEX_SHADOWRECT: id = u.boundRect; break;
    case ARB_TEX_CUBE: id = u.boundCube; break;
    case ARB_TEX_3D: id = u.bound3D; break;
  }
  return GLES_FindTexture(id);
}

static void BuildKey(SPipeKey& k, const SARBProgram* vp, const SARBProgram* fp)
{
  memset(&k, 0, sizeof(k));
  k.vp = vp ? vp->id : 0;
  k.fp = fp ? fp->id : 0;
  SFFPKey& f = k.ffp;
  if (!vp)
  {
    for (int i = 0; i < GLES_MAX_UNITS; i++)
    {
      const STexUnitState& u = g_es.unit[i];
      f.texMatrix[i] = u.texMatrixIdentity ? 0 : 1;
      for (int c = 0; c < 4; c++) if (u.texGen[c]) f.texGenMask[i] |= (1 << c);
      if (f.texGenMask[i])
      {
        int c = (f.texGenMask[i] & 1) ? 0 : (f.texGenMask[i] & 2) ? 1 : (f.texGenMask[i] & 4) ? 2 : 3;
        switch (u.texGenMode[c])
        {
          case GL_OBJECT_LINEAR: f.texGenMode[i] = TG_OBJECT; break;
          case GL_EYE_LINEAR:    f.texGenMode[i] = TG_EYE; break;
          case GL_SPHERE_MAP:    f.texGenMode[i] = TG_SPHERE; break;
          case 0x8512: case 0x8510: f.texGenMode[i] = TG_REFLECTION; break;  // GL_REFLECTION_MAP_{NV,ARB}
          case 0x8511: case 0x850F: f.texGenMode[i] = TG_NORMAL; break;      // GL_NORMAL_MAP_{NV,ARB}
          default: f.texGenMode[i] = TG_OFF; f.texGenMask[i] = 0; break;
        }
      }
    }
    if (g_es.lighting)
      for (int i = 0; i < GLES_MAX_LIGHTS; i++) if (g_es.light[i].enabled) f.lightMask |= (1 << i);
    f.colorMaterial = (g_es.lighting && g_es.colorMaterial) ? 1 : 0;
    for (int i = 0; i < 6; i++) if (g_es.clipPlane[i]) f.clipMask |= (1 << i);
  }
  if (!fp)
  {
    for (int i = 0; i < GLES_MAX_UNITS; i++)
    {
      GLenum target = 0;
      STextureObj* o = GLES_BoundTexture(i, target);
      if (!o) continue;
      switch (target)
      {
        case ES_TEXTURE_CUBE_MAP: f.unitType[i] = UNIT_CUBE; break;
        case ES_TEXTURE_3D: f.unitType[i] = UNIT_3D; break;
        case ES_TEXTURE_RECTANGLE_NV: f.unitType[i] = UNIT_RECT; break;
        default: f.unitType[i] = UNIT_2D; break;
      }
      f.env[i] = g_es.unit[i].env;
      memset(f.env[i].color, 0, sizeof(f.env[i].color)); // uniform, not part of the key
    }
    if (g_es.fog) f.fogMode = (g_es.fogMode == GL_LINEAR) ? 1 : (g_es.fogMode == GL_EXP) ? 2 : 3;
  }
  f.alphaTest = g_es.alphaTest && g_es.alphaFunc != GL_ALWAYS;
  f.alphaFunc = (unsigned char)(g_es.alphaFunc - GL_NEVER);
}

static int sProgramSwitches;

void GLES_ReissueProgram() { es_glUseProgram(sCurrentProg); }
void GLES_ReportTiming()
{
  if (!g_esDebug) return;
  static const char* names[GLES_T_COUNT] = { "prepare", "map", "query", "teximage", "texsub", "copytex", "readpix", "immediate", "drawelem", "imupload" };
  std::string s;
  char buf[96];
  for (int i = 0; i < GLES_T_COUNT; i++) { sprintf(buf, " %s %d/%.1fms", names[i], g_esCounts[i], g_esTimers[i] * 1000.0); s += buf; g_esCounts[i] = 0; g_esTimers[i] = 0; }
  GLES_Log("GLES: per 300 frames:%s, %d program switches", s.c_str(), sProgramSwitches);
  sProgramSwitches = 0;
}

bool GLES_PrepareDraw()
{
  SESTimer timer(GLES_T_PREPARE);
  const SARBProgram* vp = GLES_ARB_Bound(true);
  const SARBProgram* fp = GLES_ARB_Bound(false);
  SPipeKey k;
  BuildKey(k, vp, fp);
  std::map<SPipeKey, SPipeProgram>::iterator it;
  if (sLastProgValid && memcmp(&k, &sLastProg->first, sizeof(k)) == 0)
    it = sLastProg;
  else if ((it = sPrograms.find(k)) == sPrograms.end())
  {
    it = sPrograms.insert(std::make_pair(k, Build(k, vp, fp))).first;
    // The fixed-function features in play, to see which paths a scene exercises.
    unsigned texGen = 0;
    for (int i = 0; i < GLES_MAX_UNITS; i++) if (k.ffp.texGenMode[i]) texGen |= 1u << i;
    GLES_Log("GLES: program #%d built (vp %u, fp %u) texgen 0x%x lights 0x%x clip 0x%x fog %d alpha %d", (int)sPrograms.size(), k.vp, k.fp, texGen, k.ffp.lightMask, k.ffp.clipMask, k.ffp.fogMode, k.ffp.alphaTest);
  }
  sLastProg = it; sLastProgValid = true;
  SPipeProgram& p = it->second;
  if (sCurrentProg != p.prog) { es_glUseProgram(p.prog); sCurrentProg = p.prog; sProgramSwitches++; }
  sCurrentKey = &it->first;

  if (!vp)
  {
    es_glUniformMatrix4fv(p.uMVP, 1, GL_FALSE, GLES_CurrentMVP());
    if (p.uMV >= 0) es_glUniformMatrix4fv(p.uMV, 1, GL_FALSE, g_es.modelview.m[g_es.modelview.depth]);
    for (int i = 0; i < GLES_MAX_UNITS; i++)
      if (k.ffp.texMatrix[i] && p.uTexMat[i] >= 0) es_glUniformMatrix4fv(p.uTexMat[i], 1, GL_FALSE, g_es.unit[i].texMatrix);
    for (int i = 0; i < GLES_MAX_UNITS; i++)
    {
      if (k.ffp.texGenMode[i] == TG_OBJECT && p.uObjPlane >= 0) es_glUniform4fv(p.uObjPlane + i * 4, 4, &g_es.unit[i].texGenPlaneObj[0][0]);
      if (k.ffp.texGenMode[i] == TG_EYE && p.uEyePlane >= 0) es_glUniform4fv(p.uEyePlane + i * 4, 4, &g_es.unit[i].texGenPlaneEye[0][0]);
    }
    if (k.ffp.clipMask && p.uClipPlane >= 0) es_glUniform4fv(p.uClipPlane, 6, &g_es.clipPlaneEq[0][0]);
    if (k.ffp.lightMask)
    {
      float pos[GLES_MAX_LIGHTS][4], amb[GLES_MAX_LIGHTS][4], dif[GLES_MAX_LIGHTS][4], spec[GLES_MAX_LIGHTS][4], att[GLES_MAX_LIGHTS][4];
      for (int i = 0; i < GLES_MAX_LIGHTS; i++)
      {
        const SLightState& l = g_es.light[i];
        memcpy(pos[i], l.position, 16); memcpy(amb[i], l.ambient, 16); memcpy(dif[i], l.diffuse, 16); memcpy(spec[i], l.specular, 16);
        att[i][0] = l.attenuation[0]; att[i][1] = l.attenuation[1]; att[i][2] = l.attenuation[2]; att[i][3] = 0;
      }
      if (p.uLightPos >= 0) es_glUniform4fv(p.uLightPos, GLES_MAX_LIGHTS, &pos[0][0]);
      if (p.uLightAmb >= 0) es_glUniform4fv(p.uLightAmb, GLES_MAX_LIGHTS, &amb[0][0]);
      if (p.uLightDiff >= 0) es_glUniform4fv(p.uLightDiff, GLES_MAX_LIGHTS, &dif[0][0]);
      if (p.uLightSpec >= 0) es_glUniform4fv(p.uLightSpec, GLES_MAX_LIGHTS, &spec[0][0]);
      if (p.uLightAtt >= 0) es_glUniform4fv(p.uLightAtt, GLES_MAX_LIGHTS, &att[0][0]);
      const SMaterialState& m = g_es.material;
      if (p.uMatAmb >= 0) es_glUniform4fv(p.uMatAmb, 1, m.ambient);
      if (p.uMatDiff >= 0) es_glUniform4fv(p.uMatDiff, 1, m.diffuse);
      if (p.uMatSpec >= 0) es_glUniform4fv(p.uMatSpec, 1, m.specular);
      if (p.uMatEmis >= 0) es_glUniform4fv(p.uMatEmis, 1, m.emission);
      if (p.uShininess >= 0) es_glUniform1f(p.uShininess, m.shininess);
      if (p.uGlobalAmb >= 0) es_glUniform4fv(p.uGlobalAmb, 1, g_es.globalAmbient);
    }
  }
  else if (p.uEnvV >= 0 && p.envVCount > 0 && p.envVersionV != GLES_ARB_EnvVersion(true))
  {
    es_glUniform4fv(p.uEnvV, p.envVCount, GLES_ARB_Env(true));
    p.envVersionV = GLES_ARB_EnvVersion(true);
  }
  if (fp && p.uEnvF >= 0 && p.envFCount > 0 && p.envVersionF != GLES_ARB_EnvVersion(false))
  {
    es_glUniform4fv(p.uEnvF, p.envFCount, GLES_ARB_Env(false));
    p.envVersionF = GLES_ARB_EnvVersion(false);
  }
  if (k.ffp.alphaTest && p.uAlphaRef >= 0) es_glUniform1f(p.uAlphaRef, g_es.alphaRef);
  if ((fp && fp->fogMode) || (!fp && k.ffp.fogMode))
  {
    float range = g_es.fogEnd - g_es.fogStart;
    float params[4] = { g_es.fogStart, g_es.fogEnd, range != 0.0f ? 1.0f / range : 1.0f, g_es.fogDensity };
    if (p.uFogColor >= 0) es_glUniform4fv(p.uFogColor, 1, g_es.fogColor);
    if (p.uFogParams >= 0) es_glUniform4fv(p.uFogParams, 1, params);
  }

  // Textures: the ARB program names its targets; fixed function follows the enables.
  for (int i = 0; i < GLES_MAX_UNITS; i++)
  {
    STextureObj* o = NULL;
    bool rect = false;
    if (fp)
    {
      if (!fp->texTarget[i]) continue;
      o = TextureForARBUnit(i, fp->texTarget[i]);
      rect = (fp->texTarget[i] == ARB_TEX_RECT || fp->texTarget[i] == ARB_TEX_SHADOWRECT);
    }
    else
    {
      if (k.ffp.unitType[i] == UNIT_OFF) continue;
      GLenum target = 0;
      o = GLES_BoundTexture(i, target);
      rect = (k.ffp.unitType[i] == UNIT_RECT);
      if (p.uEnvColor[i] >= 0) es_glUniform4fv(p.uEnvColor[i], 1, g_es.unit[i].env.color);
    }
    if (!o) continue;
    GLES_NativeBindTexture(i, o->target, o->id);
    if (rect && p.uRectScale[i] >= 0)
      es_glUniform2f(p.uRectScale[i], o->width ? 1.0f / o->width : 1.0f, o->height ? 1.0f / o->height : 1.0f);
  }

  static bool debug = getenv("FARCRY_GLES_DEBUG") != NULL;
  if (debug)
  {
    static int errors;
    GLenum err = es_glGetError();
    if (err && errors++ < 20) GLES_Log("GLES: GL error 0x%x before draw (vp %u, fp %u)", err, k.vp, k.fp);
  }
  return true;
}

void GLES_RegisterFFP(std::map<std::string, void*>&) {}
