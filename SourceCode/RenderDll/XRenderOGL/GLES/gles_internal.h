/*=============================================================================
  gles_internal.h : shared state of the GLES translation layer.
=============================================================================*/
#ifndef GLES_INTERNAL_H
#define GLES_INTERNAL_H

#include "RenderPCH.h"
#include "../GL_Renderer.h"
#include <map>
#include <string>
#include <vector>

// ES 3.0 enums the layer needs that Mygl.h (GL 1.2 era) does not define.
#define ES_ARRAY_BUFFER                 0x8892
#define ES_ELEMENT_ARRAY_BUFFER         0x8893
#define ES_STREAM_DRAW                  0x88E0
#define ES_STATIC_DRAW                  0x88E4
#define ES_DYNAMIC_DRAW                 0x88E8
#define ES_MAP_READ_BIT                 0x0001
#define ES_MAP_WRITE_BIT                0x0002
#define ES_MAP_INVALIDATE_BUFFER_BIT    0x0008
#define ES_FRAGMENT_SHADER              0x8B30
#define ES_VERTEX_SHADER                0x8B31
#define ES_COMPILE_STATUS               0x8B81
#define ES_LINK_STATUS                  0x8B82
#define ES_TEXTURE0                     0x84C0
#define ES_TEXTURE_CUBE_MAP             0x8513
#define ES_TEXTURE_3D                   0x806F
#define ES_TEXTURE_WRAP_R               0x8072
#define ES_TEXTURE_MAX_LEVEL            0x813D
#define ES_TEXTURE_SWIZZLE_R            0x8E42
#define ES_TEXTURE_SWIZZLE_G            0x8E43
#define ES_TEXTURE_SWIZZLE_B            0x8E44
#define ES_TEXTURE_SWIZZLE_A            0x8E45
#define ES_RGBA8                        0x8058
#define ES_RGB8                         0x8051
#define ES_R8                           0x8229
#define ES_RG8                          0x822B
#define ES_RED                          0x1903
#define ES_RG                           0x8227
#define ES_COMPRESSED_RGB_S3TC_DXT1     0x83F0
#define ES_COMPRESSED_RGBA_S3TC_DXT1    0x83F1
#define ES_COMPRESSED_RGBA_S3TC_DXT3    0x83F2
#define ES_COMPRESSED_RGBA_S3TC_DXT5    0x83F3
#define ES_TEXTURE_MAX_ANISOTROPY       0x84FE
#define ES_TEXTURE_RECTANGLE_NV         0x84F5
#define ES_GENERATE_MIPMAP_SGIS         0x8191
#define ES_UNPACK_ALIGNMENT             0x0CF5
#define ES_PACK_ALIGNMENT               0x0D05
#define ES_BGRA_EXT                     0x80E1
#define ES_COMBINE                      0x8570
#define ES_COMBINE_RGB                  0x8571
#define ES_COMBINE_ALPHA                0x8572
#define ES_RGB_SCALE                    0x8573
#define ES_ADD_SIGNED                   0x8574
#define ES_INTERPOLATE                  0x8575
#define ES_CONSTANT                     0x8576
#define ES_PRIMARY_COLOR                0x8577
#define ES_PREVIOUS                     0x8578
#define ES_SUBTRACT                     0x84E7
#define ES_DOT3_RGB                     0x86AE
#define ES_DOT3_RGBA                    0x86AF
#define ES_SOURCE0_RGB                  0x8580
#define ES_SOURCE0_ALPHA                0x8588
#define ES_OPERAND0_RGB                 0x8590
#define ES_OPERAND0_ALPHA               0x8598
#define ES_SECONDARY_COLOR_ARRAY        0x845E
#define ES_FOG_COORDINATE_ARRAY         0x8457
#define ES_VERTEX_PROGRAM_ARB           0x8620
#define ES_FRAGMENT_PROGRAM_ARB         0x8804
#define ES_MAX_VERTEX_ATTRIBS           0x8869
#define ES_ANY_SAMPLES_PASSED           0x8C2F
#define ES_QUERY_RESULT                 0x8866
#define ES_QUERY_RESULT_AVAILABLE       0x8867
#define ES_FRAMEBUFFER                  0x8D40
#define ES_READ_FRAMEBUFFER             0x8CA8
#define ES_COLOR_ATTACHMENT0            0x8CE0
#define ES_FRAMEBUFFER_COMPLETE         0x8CD5
#define ES_DRAW_FRAMEBUFFER             0x8CA9
#define ES_FRAMEBUFFER_BINDING          0x8CA6
#define ES_RENDERBUFFER                 0x8D41
#define ES_DEPTH_STENCIL_ATTACHMENT     0x821A
#define ES_DEPTH24_STENCIL8             0x88F0
#define ES_DEPTH_STENCIL                0x84F9
#define ES_UNSIGNED_INT_24_8            0x84FA
#define ES_TEXTURE_COMPARE_MODE         0x884C
#define ES_TEXTURE_COMPARE_FUNC         0x884D
#define ES_COMPARE_REF_TO_TEXTURE       0x884E

// Texture unit the layer uses for its own work, above the renderer's GLES_MAX_UNITS.
#define GLES_SCRATCH_UNIT 15

// Native ES entry points used by the layer. Loaded through SDL in GLES_LoadNative().
#define ES_PROCS \
  ES_PROC(const GLubyte*, glGetString, (GLenum)) \
  ES_PROC(void, glGetIntegerv, (GLenum, GLint*)) \
  ES_PROC(void, glGetBufferParameteriv, (GLenum, GLenum, GLint*)) \
  ES_PROC(void, glGetVertexAttribiv, (GLuint, GLenum, GLint*)) \
  ES_PROC(void, glGetFloatv, (GLenum, GLfloat*)) \
  ES_PROC(GLenum, glGetError, ()) \
  ES_PROC(void, glEnable, (GLenum)) \
  ES_PROC(void, glDisable, (GLenum)) \
  ES_PROC(GLboolean, glIsEnabled, (GLenum)) \
  ES_PROC(void, glClearDepthf, (GLfloat)) \
  ES_PROC(void, glDepthRangef, (GLfloat, GLfloat)) \
  ES_PROC(void, glGenBuffers, (GLsizei, GLuint*)) \
  ES_PROC(void, glBindBuffer, (GLenum, GLuint)) \
  ES_PROC(void, glBufferData, (GLenum, GLsizeiptrARB, const void*, GLenum)) \
  ES_PROC(void, glBufferSubData, (GLenum, GLintptrARB, GLsizeiptrARB, const void*)) \
  ES_PROC(void, glDeleteBuffers, (GLsizei, const GLuint*)) \
  ES_PROC(void*, glMapBufferRange, (GLenum, GLintptrARB, GLsizeiptrARB, GLbitfield)) \
  ES_PROC(GLboolean, glUnmapBuffer, (GLenum)) \
  ES_PROC(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
  ES_PROC(void, glEnableVertexAttribArray, (GLuint)) \
  ES_PROC(void, glDisableVertexAttribArray, (GLuint)) \
  ES_PROC(void, glVertexAttrib4f, (GLuint, GLfloat, GLfloat, GLfloat, GLfloat)) \
  ES_PROC(void, glDrawArrays, (GLenum, GLint, GLsizei)) \
  ES_PROC(void, glDrawElements, (GLenum, GLsizei, GLenum, const void*)) \
  ES_PROC(GLuint, glCreateShader, (GLenum)) \
  ES_PROC(void, glShaderSource, (GLuint, GLsizei, const char* const*, const GLint*)) \
  ES_PROC(void, glCompileShader, (GLuint)) \
  ES_PROC(void, glGetShaderiv, (GLuint, GLenum, GLint*)) \
  ES_PROC(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
  ES_PROC(void, glDeleteShader, (GLuint)) \
  ES_PROC(GLuint, glCreateProgram, ()) \
  ES_PROC(void, glAttachShader, (GLuint, GLuint)) \
  ES_PROC(void, glBindAttribLocation, (GLuint, GLuint, const char*)) \
  ES_PROC(void, glLinkProgram, (GLuint)) \
  ES_PROC(void, glGetProgramiv, (GLuint, GLenum, GLint*)) \
  ES_PROC(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, char*)) \
  ES_PROC(void, glUseProgram, (GLuint)) \
  ES_PROC(void, glDeleteProgram, (GLuint)) \
  ES_PROC(GLint, glGetUniformLocation, (GLuint, const char*)) \
  ES_PROC(void, glUniform1i, (GLint, GLint)) \
  ES_PROC(void, glUniform1f, (GLint, GLfloat)) \
  ES_PROC(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
  ES_PROC(void, glUniform4fv, (GLint, GLsizei, const GLfloat*)) \
  ES_PROC(void, glUniformMatrix4fv, (GLint, GLsizei, GLboolean, const GLfloat*)) \
  ES_PROC(void, glActiveTexture, (GLenum)) \
  ES_PROC(void, glBindTexture, (GLenum, GLuint)) \
  ES_PROC(void, glGenTextures, (GLsizei, GLuint*)) \
  ES_PROC(void, glDeleteTextures, (GLsizei, const GLuint*)) \
  ES_PROC(void, glTexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
  ES_PROC(void, glTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void*)) \
  ES_PROC(void, glTexImage3D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLsizei, GLint, GLenum, GLenum, const void*)) \
  ES_PROC(void, glCompressedTexImage2D, (GLenum, GLint, GLenum, GLsizei, GLsizei, GLint, GLsizei, const void*)) \
  ES_PROC(void, glCompressedTexSubImage2D, (GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLsizei, const void*)) \
  ES_PROC(void, glTexParameteri, (GLenum, GLenum, GLint)) \
  ES_PROC(void, glTexParameterf, (GLenum, GLenum, GLfloat)) \
  ES_PROC(void, glGenerateMipmap, (GLenum)) \
  ES_PROC(void, glPixelStorei, (GLenum, GLint)) \
  ES_PROC(void, glReadPixels, (GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void*)) \
  ES_PROC(void, glCopyTexSubImage2D, (GLenum, GLint, GLint, GLint, GLint, GLint, GLsizei, GLsizei)) \
  ES_PROC(void, glBlendFunc, (GLenum, GLenum)) \
  ES_PROC(void, glDepthMask, (GLboolean)) \
  ES_PROC(void, glFinish, ()) \
  ES_PROC(void, glGenQueries, (GLsizei, GLuint*)) \
  ES_PROC(void, glDeleteQueries, (GLsizei, const GLuint*)) \
  ES_PROC(void, glBeginQuery, (GLenum, GLuint)) \
  ES_PROC(void, glEndQuery, (GLenum)) \
  ES_PROC(void, glGetQueryObjectuiv, (GLuint, GLenum, GLuint*)) \
  ES_PROC(void, glGenFramebuffers, (GLsizei, GLuint*)) \
  ES_PROC(void, glDeleteFramebuffers, (GLsizei, const GLuint*)) \
  ES_PROC(void, glBindFramebuffer, (GLenum, GLuint)) \
  ES_PROC(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
  ES_PROC(GLenum, glCheckFramebufferStatus, (GLenum)) \
  ES_PROC(void, glCopyTexImage2D, (GLenum, GLint, GLenum, GLint, GLint, GLsizei, GLsizei, GLint)) \
  ES_PROC(void, glBlitFramebuffer, (GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum)) \
  ES_PROC(void, glInvalidateFramebuffer, (GLenum, GLsizei, const GLenum*)) \
  ES_PROC(void, glGenRenderbuffers, (GLsizei, GLuint*)) \
  ES_PROC(void, glDeleteRenderbuffers, (GLsizei, const GLuint*)) \
  ES_PROC(void, glBindRenderbuffer, (GLenum, GLuint)) \
  ES_PROC(void, glRenderbufferStorage, (GLenum, GLenum, GLsizei, GLsizei)) \
  ES_PROC(void, glFramebufferRenderbuffer, (GLenum, GLenum, GLenum, GLuint)) \
  ES_PROC(void, glGetBooleanv, (GLenum, GLboolean*)) \
  ES_PROC(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
  ES_PROC(void, glScissor, (GLint, GLint, GLsizei, GLsizei)) \
  ES_PROC(void, glColorMask, (GLboolean, GLboolean, GLboolean, GLboolean)) \
  ES_PROC(void, glStencilFunc, (GLenum, GLint, GLuint)) \
  ES_PROC(void, glStencilOp, (GLenum, GLenum, GLenum)) \
  ES_PROC(void, glStencilMask, (GLuint)) \
  ES_PROC(void, glStencilFuncSeparate, (GLenum, GLenum, GLint, GLuint)) \
  ES_PROC(void, glStencilOpSeparate, (GLenum, GLenum, GLenum, GLenum)) \
  ES_PROC(void, glStencilMaskSeparate, (GLenum, GLuint)) \
  ES_PROC(void, glClear, (GLbitfield)) \
  ES_PROC(void, glClearColor, (GLfloat, GLfloat, GLfloat, GLfloat))

#define ES_PROC(ret, name, params) typedef ret (*PFN_##name) params; extern PFN_##name es_##name;
ES_PROCS
#undef ES_PROC

bool GLES_LoadNative();
bool GLES_HasNativeExt(const char* name);

//////////////////////////////////////////////////////////////////////////
// State
//////////////////////////////////////////////////////////////////////////

#define GLES_MAX_UNITS 8
#define GLES_ATTR_POS 0
#define GLES_ATTR_NORMAL 2
#define GLES_ATTR_COLOR 3
#define GLES_ATTR_COLOR2 4
#define GLES_ATTR_TEX0 8

struct SMatrixStack
{
  float m[32][16];
  int depth;
};

struct STexEnv
{
  GLenum mode;            // GL_MODULATE, GL_REPLACE, GL_DECAL, GL_BLEND, GL_ADD, ES_COMBINE
  GLenum combineRGB, combineAlpha;
  GLenum srcRGB[3], srcAlpha[3];
  GLenum opRGB[3], opAlpha[3];
  float scaleRGB, scaleAlpha;
  float color[4];
};

struct STexUnitState
{
  STexEnv env;
  bool enable2D, enableRect, enableCube, enable3D, enable1D;
  GLuint bound2D, boundRect, boundCube, bound3D, bound1D;
  bool texGen[4];
  GLenum texGenMode[4];
  float texGenPlaneObj[4][4], texGenPlaneEye[4][4];
  float texMatrix[16];
  bool texMatrixIdentity;
};

struct SESArray
{
  bool enabled;
  GLint size;
  GLenum type;
  GLsizei stride;
  const void* pointer;
  GLuint buffer;          // VBO bound when the pointer was set
};

struct STextureObj
{
  GLuint id;
  GLuint native;          // driver-generated name; the renderer's own names never reach the driver
  GLenum target;          // native target
  bool isRect;
  int width, height, depth;
  GLenum internalFormat;
  int levels;
  bool generateMipmap;
  bool hasSwizzle;
  unsigned short faceMask[16];                      // cube maps: faces uploaded per level
  std::map<int, std::vector<GLubyte> > compressed;  // per level (+16 per cube face), for glGetCompressedTexImage
};

int GLES_CompressDXT(GLenum format, int w, int h, const GLubyte* rgba, std::vector<GLubyte>& out);
void GLES_DecompressDXT(GLenum format, int w, int h, const GLubyte* blocks, std::vector<GLubyte>& rgba);

#define GLES_MAX_LIGHTS 4

struct SLightState
{
  bool enabled;
  float position[4];      // eye space (transformed at glLight time)
  float ambient[4], diffuse[4], specular[4];
  float attenuation[3];   // constant, linear, quadratic
};

struct SMaterialState
{
  float ambient[4], diffuse[4], specular[4], emission[4];
  float shininess;
};

struct SGLESState
{
  // matrices
  GLenum matrixMode;
  SMatrixStack modelview, projection, texture[GLES_MAX_UNITS];
  bool mvpDirty;

  // current vertex attributes
  float color[4], normal[3], color2[3], texcoord[GLES_MAX_UNITS][4];

  // texture units
  int activeUnit, clientActiveUnit;
  STexUnitState unit[GLES_MAX_UNITS];

  // fixed-function state
  bool alphaTest; GLenum alphaFunc; float alphaRef;
  bool fog; GLenum fogMode; float fogStart, fogEnd, fogDensity, fogColor[4];
  bool lighting, colorMaterial, normalize;
  GLenum colorMaterialMode;
  SLightState light[GLES_MAX_LIGHTS];
  SMaterialState material;
  float globalAmbient[4];
  bool clipPlane[6]; float clipPlaneEq[6][4];   // eye space (transformed at glClipPlane time)
  bool vertexProgram, fragmentProgram;
  GLuint boundVP, boundFP;

  // EXT_stencil_two_side: while enabled, stencil calls apply to the active face only
  bool stencilTwoSide;
  GLenum stencilFace;

  // client arrays
  SESArray vertexArray, normalArray, colorArray, color2Array, texcoordArray[GLES_MAX_UNITS];
  GLuint arrayBuffer, elementBuffer;

  // immediate mode
  bool inBegin;
  GLenum beginMode;

  int unpackAlignment;
};

// Plain data only in SGLESState (it is memset on init); objects live beside it.
extern SGLESState g_es;
extern std::map<GLuint, STextureObj> g_esTextures;
struct SESBuffer
{
  GLuint size;
  GLenum usage;
  GLenum mapped;                     // target while a CPU-copy map is outstanding
  std::vector<unsigned char> shadow; // CPU copy, only for buffers the engine maps
  bool ringValid;                    // mode 4: the current contents live in the persistent ring
  bool direct;                       // mode 4: the engine writes straight into the ring (dynamic buffers)
  bool rangeMapped;                  // mode 4: the outstanding map is a GLES_MapBufferRange slice
  int ringIndex;                     // which ring (0 array, 1 element)
  size_t ringOffset;
  unsigned ringLap;         // ring lap of ringOffset, to tell a current backing from a stale queue entry
  // Mode 4 slices: [start, end) of this buffer written since its last wrap, and where each lives in the ring.
  struct SRange { size_t start, end, ringOff; };
  std::vector<SRange> ranges;
  size_t lastEnd;
  SESBuffer() : size(0), usage(0), mapped(0), ringValid(false), direct(false), rangeMapped(false), ringIndex(0), ringOffset(0), ringLap(0), lastEnd(0) {}
};
extern std::map<GLuint, SESBuffer> g_esBuffers;   // VBO id -> size, usage, CPU copy

void GLES_InitState();
void GLES_ForgetBufferCache();
void GLES_ForgetTextureCache();
struct STextureObj* GLES_FindTexture(GLuint id);
GLuint GLES_NativeTexName(GLuint id, GLenum target = GL_TEXTURE_2D);
void GLES_RegisterState(std::map<std::string, void*>& t);
void GLES_RegisterTexture(std::map<std::string, void*>& t);
void GLES_RegisterVertex(std::map<std::string, void*>& t);
void GLES_RegisterFFP(std::map<std::string, void*>& t);
void GLES_RegisterQuery(std::map<std::string, void*>& t);

// Called before every draw: binds the program matching the current state and uploads uniforms.
// Returns false if nothing can be drawn (no vertex array).
bool GLES_PrepareDraw();
void GLES_NoteArrayBufferBound(GLuint id);
// Native texture binding with a per-unit shadow; the shadow is authoritative for what ES has bound.
void GLES_NativeBindTexture(int unit, GLenum target, GLuint id);
void GLES_FlushImmediate();
const float* GLES_CurrentMVP();
void GLES_MatrixMultiply(const float* a, const float* b, float* out);
bool GLES_MatrixInvert(const float* m, float* out);
STextureObj* GLES_BoundTexture(int unit, GLenum& samplerTarget);
int GLES_NativeActiveUnit();
GLuint GLES_CurrentProgram();
// Disable/re-enable the enabled attribute arrays around a draw of the layer's own (shadow untouched).
void GLES_SuspendAttribArrays(bool suspend);
// Re-issue shadowed state after something outside the layer drew (the Android touch overlay).
void GLES_ReissueAttribArrays();
void GLES_ReissueTextureUnit0();
void GLES_ReissueProgram();

// Scene framebuffer (gles_fbo.cpp)
bool GLES_SceneFBOEnsure(int w, int h);
bool GLES_ReadDepth(int x, int y, int w, int h, float* out);
bool GLES_CopySceneDepth(GLuint tex, GLenum target, int level, int x, int y, int w, int h);
void GLES_Log(const char* fmt, ...);

// Development timing counters (FARCRY_GLES_DEBUG), reported every 300 frames.
enum { GLES_T_PREPARE, GLES_T_MAP, GLES_T_QUERY, GLES_T_TEXIMAGE, GLES_T_TEXSUB, GLES_T_COPYTEX, GLES_T_READPIX, GLES_T_IMMEDIATE, GLES_T_DRAWELEM, GLES_T_IMUPLOAD, GLES_T_COUNT };
#define ES_MAP_UNSYNCHRONIZED_BIT 0x0020
#define ES_MAP_INVALIDATE_RANGE_BIT 0x0004
#define ES_MAP_FLUSH_EXPLICIT_BIT 0x0010
extern double g_esTimers[GLES_T_COUNT];
extern int g_esCounts[GLES_T_COUNT];
extern bool g_esDebug;
struct SESTimer
{
  int which; Uint64 t0;
  SESTimer(int w) : which(w), t0(g_esDebug ? SDL_GetPerformanceCounter() : 0) { g_esCounts[w]++; }
  ~SESTimer() { if (g_esDebug) g_esTimers[which] += (double)(SDL_GetPerformanceCounter() - t0) / (double)SDL_GetPerformanceFrequency(); }
};

#endif
