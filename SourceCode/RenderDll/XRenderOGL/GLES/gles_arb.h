/*=============================================================================
  gles_arb.h : ARB vertex/fragment program objects translated to GLSL ES.
=============================================================================*/
#ifndef GLES_ARB_H
#define GLES_ARB_H

#include "gles_internal.h"

enum { ARB_TEX_NONE = 0, ARB_TEX_2D, ARB_TEX_RECT, ARB_TEX_CUBE, ARB_TEX_3D, ARB_TEX_SHADOW2D, ARB_TEX_SHADOWRECT };

struct SARBProgram
{
  GLuint id;
  bool vertex;
  bool valid;
  std::string source;      // ARB text
  std::string glsl;        // translated shader
  GLuint shader;           // compiled ES shader, 0 until first use
  int maxEnv;              // highest program.env index used + 1
  unsigned char texTarget[GLES_MAX_UNITS]; // fragment: ARB_TEX_* per unit
  int fogMode;             // fragment: 0 none, 1 linear, 2 exp, 3 exp2
  bool usesTexcoord[GLES_MAX_UNITS];        // fragment inputs read / vertex outputs written
  bool writesFog;
};

SARBProgram* GLES_ARB_Bound(bool vertex);      // bound and enabled program, NULL otherwise
GLuint GLES_ARB_Shader(SARBProgram& p);         // compiled shader object (compiles on first use)
const float* GLES_ARB_Env(bool vertex);          // env parameter array
unsigned GLES_ARB_EnvVersion(bool vertex);       // incremented on every env change (never 0)
int GLES_ARB_EnvCount(bool vertex);
void GLES_RegisterARB(std::map<std::string, void*>& t);

// Translate ARB program text to GLSL ES 3.00. Fills the metadata in p. Returns false on a parse error.
bool GLES_ARB_Translate(SARBProgram& p, std::string& error);

#endif
