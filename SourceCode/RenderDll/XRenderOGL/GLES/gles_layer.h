/*=============================================================================
  gles_layer.h : OpenGL ES 3.0 translation layer for XRenderOGL.

  The renderer calls GL only through its crygl* function table (GLFuncs.h).
  With GLES_RENDERER that table is filled from this layer instead of the
  driver: implemented entry points, straight forwards to the ES driver where
  semantics match, and a logging stub for everything else.
=============================================================================*/
#ifndef GLES_LAYER_H
#define GLES_LAYER_H

// Resolve the ES entry points the layer itself uses. Needs the GL library loaded (window created).
bool GLES_Init();

// Never returns NULL: implementation, native forward, or a logging stub.
void* GLES_GetProcAddress(const char* name);

// Log every entry point that hit a stub, with call counts.
void GLES_ReportUnimplemented();

// Request an ES 3.0 context (and point SDL at ANGLE on macOS). Call before SDL_CreateWindow.
void GLES_SetContextAttributes();

// Present: the frame is drawn into the layer's scene framebuffer; this blits it to the window and swaps.
struct SDL_Window;
void GLES_SwapWindow(struct SDL_Window* win);

#endif
