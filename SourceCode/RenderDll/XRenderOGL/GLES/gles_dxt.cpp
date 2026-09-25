/*=============================================================================
  gles_dxt.cpp : CPU DXT1 / DXT3 / DXT5 encoder for uploads the renderer
  expects the driver to compress (GL_COMPRESSED_*_S3TC internal formats).
  Range-fit quality: endpoints from the block's colour bounding box.
=============================================================================*/
#include <vector>
#include <string.h>
#include <stdlib.h>

typedef unsigned char GLubyte;
typedef unsigned int GLenum;
#define ES_COMPRESSED_RGB_S3TC_DXT1     0x83F0
#define ES_COMPRESSED_RGBA_S3TC_DXT1    0x83F1
#define ES_COMPRESSED_RGBA_S3TC_DXT3    0x83F2
#define ES_COMPRESSED_RGBA_S3TC_DXT5    0x83F3

static inline unsigned short Pack565(int r, int g, int b)
{
  return (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

static inline void Unpack565(unsigned short c, int* rgb)
{
  rgb[0] = ((c >> 11) & 31) * 255 / 31;
  rgb[1] = ((c >> 5) & 63) * 255 / 63;
  rgb[2] = (c & 31) * 255 / 31;
}

// 16 RGBA pixels -> 8 bytes of DXT1 colour block. With alphaCutout, pixels with alpha < 128 use the transparent code.
static void EncodeColorBlock(const GLubyte px[16][4], bool alphaCutout, GLubyte* out)
{
  int mn[3] = { 255, 255, 255 }, mx[3] = { 0, 0, 0 };
  bool anyOpaque = false;
  for (int i = 0; i < 16; i++)
  {
    if (alphaCutout && px[i][3] < 128) continue;
    anyOpaque = true;
    for (int c = 0; c < 3; c++) { if (px[i][c] < mn[c]) mn[c] = px[i][c]; if (px[i][c] > mx[c]) mx[c] = px[i][c]; }
  }
  if (!anyOpaque) { mn[0] = mn[1] = mn[2] = mx[0] = mx[1] = mx[2] = 0; }
  // shrink the box slightly towards the centre: better fit for the interpolated colours
  for (int c = 0; c < 3; c++)
  {
    int inset = (mx[c] - mn[c]) >> 4;
    mn[c] += inset; mx[c] -= inset;
    if (mn[c] > mx[c]) mn[c] = mx[c];
  }
  unsigned short c0 = Pack565(mx[0], mx[1], mx[2]), c1 = Pack565(mn[0], mn[1], mn[2]);
  // DXT1: c0 > c1 selects the 4-colour mode; c0 <= c1 the 3-colour + transparent mode
  if (alphaCutout) { if (c0 > c1) { unsigned short t = c0; c0 = c1; c1 = t; } }
  else if (c0 < c1) { unsigned short t = c0; c0 = c1; c1 = t; }
  else if (c0 == c1 && c1 > 0) c1--;
  int pal[4][3];
  Unpack565(c0, pal[0]);
  Unpack565(c1, pal[1]);
  if (c0 > c1)
  {
    for (int c = 0; c < 3; c++) { pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3; pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3; }
  }
  else
  {
    for (int c = 0; c < 3; c++) { pal[2][c] = (pal[0][c] + pal[1][c]) / 2; pal[3][c] = 0; }
  }
  unsigned int idx = 0;
  for (int i = 0; i < 16; i++)
  {
    int best = 0;
    if (alphaCutout && px[i][3] < 128) best = 3;
    else
    {
      int bestD = 1 << 30;
      int n = (c0 > c1) ? 4 : 3;
      for (int k = 0; k < n; k++)
      {
        int dr = px[i][0] - pal[k][0], dg = px[i][1] - pal[k][1], db = px[i][2] - pal[k][2];
        int d = dr * dr + dg * dg + db * db;
        if (d < bestD) { bestD = d; best = k; }
      }
    }
    idx |= (unsigned int)best << (i * 2);
  }
  out[0] = (GLubyte)c0; out[1] = (GLubyte)(c0 >> 8); out[2] = (GLubyte)c1; out[3] = (GLubyte)(c1 >> 8);
  out[4] = (GLubyte)idx; out[5] = (GLubyte)(idx >> 8); out[6] = (GLubyte)(idx >> 16); out[7] = (GLubyte)(idx >> 24);
}

static void EncodeAlphaBlockDXT3(const GLubyte px[16][4], GLubyte* out)
{
  for (int i = 0; i < 8; i++)
    out[i] = (GLubyte)((px[i*2][3] >> 4) | (px[i*2+1][3] & 0xF0));
}

static void EncodeAlphaBlockDXT5(const GLubyte px[16][4], GLubyte* out)
{
  int mn = 255, mx = 0;
  for (int i = 0; i < 16; i++) { if (px[i][3] < mn) mn = px[i][3]; if (px[i][3] > mx) mx = px[i][3]; }
  if (mx == mn) { if (mx < 255) mx++; else mn--; }
  int a0 = mx, a1 = mn; // 8-value mode
  int pal[8];
  pal[0] = a0; pal[1] = a1;
  for (int k = 1; k < 7; k++) pal[k + 1] = ((7 - k) * a0 + k * a1) / 7;
  unsigned long long bits = 0;
  for (int i = 0; i < 16; i++)
  {
    int best = 0, bestD = 1 << 30;
    for (int k = 0; k < 8; k++) { int d = abs(px[i][3] - pal[k]); if (d < bestD) { bestD = d; best = k; } }
    bits |= (unsigned long long)best << (i * 3);
  }
  out[0] = (GLubyte)a0; out[1] = (GLubyte)a1;
  for (int i = 0; i < 6; i++) out[2 + i] = (GLubyte)(bits >> (i * 8));
}

// rgba: tightly packed w*h*4. Returns the compressed size.
int GLES_CompressDXT(GLenum format, int w, int h, const GLubyte* rgba, std::vector<GLubyte>& out)
{
  bool dxt1 = (format == ES_COMPRESSED_RGB_S3TC_DXT1 || format == ES_COMPRESSED_RGBA_S3TC_DXT1);
  bool cutout = (format == ES_COMPRESSED_RGBA_S3TC_DXT1);
  int bs = dxt1 ? 8 : 16;
  int bw = (w + 3) / 4, bh = (h + 3) / 4;
  out.resize(bw * bh * bs);
  GLubyte* o = &out[0];
  GLubyte px[16][4];
  for (int by = 0; by < bh; by++)
    for (int bx = 0; bx < bw; bx++)
    {
      for (int y = 0; y < 4; y++)
        for (int x = 0; x < 4; x++)
        {
          int sx = bx * 4 + x, sy = by * 4 + y;
          if (sx >= w) sx = w - 1;
          if (sy >= h) sy = h - 1;
          memcpy(px[y * 4 + x], rgba + (sy * w + sx) * 4, 4);
        }
      if (format == ES_COMPRESSED_RGBA_S3TC_DXT3) { EncodeAlphaBlockDXT3(px, o); o += 8; }
      else if (format == ES_COMPRESSED_RGBA_S3TC_DXT5) { EncodeAlphaBlockDXT5(px, o); o += 8; }
      EncodeColorBlock(px, cutout, o);
      o += 8;
    }
  return (int)out.size();
}

//////////////////////////////////////////////////////////////////////////
// Decoder: DXT1 / DXT3 / DXT5 blocks -> RGBA8, for drivers without S3TC (Adreno 6xx and older).
//////////////////////////////////////////////////////////////////////////

static void DecodeColorBlock(const GLubyte* in, bool dxt1, GLubyte px[16][4])
{
  unsigned short c0 = in[0] | (in[1] << 8), c1 = in[2] | (in[3] << 8);
  int pal[4][4];
  Unpack565(c0, pal[0]); Unpack565(c1, pal[1]);
  pal[0][3] = pal[1][3] = 255;
  if (!dxt1 || c0 > c1)
  {
    for (int c = 0; c < 3; c++) { pal[2][c] = (2 * pal[0][c] + pal[1][c]) / 3; pal[3][c] = (pal[0][c] + 2 * pal[1][c]) / 3; }
    pal[2][3] = pal[3][3] = 255;
  }
  else
  {
    for (int c = 0; c < 3; c++) { pal[2][c] = (pal[0][c] + pal[1][c]) / 2; pal[3][c] = 0; }
    pal[2][3] = 255; pal[3][3] = 0;
  }
  unsigned idx = in[4] | (in[5] << 8) | (in[6] << 16) | ((unsigned)in[7] << 24);
  for (int i = 0; i < 16; i++, idx >>= 2)
    for (int c = 0; c < 4; c++) px[i][c] = (GLubyte)pal[idx & 3][c];
}

static void DecodeAlphaBlockDXT3(const GLubyte* in, GLubyte px[16][4])
{
  for (int i = 0; i < 16; i++)
  {
    int a = (in[i / 2] >> ((i & 1) * 4)) & 15;
    px[i][3] = (GLubyte)(a * 17);
  }
}

static void DecodeAlphaBlockDXT5(const GLubyte* in, GLubyte px[16][4])
{
  int a[8];
  a[0] = in[0]; a[1] = in[1];
  if (a[0] > a[1])
    for (int i = 1; i < 7; i++) a[i + 1] = ((7 - i) * a[0] + i * a[1]) / 7;
  else
  {
    for (int i = 1; i < 5; i++) a[i + 1] = ((5 - i) * a[0] + i * a[1]) / 5;
    a[6] = 0; a[7] = 255;
  }
  unsigned long long bits = 0;
  for (int i = 0; i < 6; i++) bits |= (unsigned long long)in[2 + i] << (8 * i);
  for (int i = 0; i < 16; i++, bits >>= 3) px[i][3] = (GLubyte)a[bits & 7];
}

void GLES_DecompressDXT(GLenum format, int w, int h, const GLubyte* blocks, std::vector<GLubyte>& rgba)
{
  bool dxt1 = (format == ES_COMPRESSED_RGB_S3TC_DXT1 || format == ES_COMPRESSED_RGBA_S3TC_DXT1);
  int bs = dxt1 ? 8 : 16;
  rgba.assign((size_t)w * h * 4, 0);
  const GLubyte* in = blocks;
  for (int by = 0; by < h; by += 4)
    for (int bx = 0; bx < w; bx += 4, in += bs)
    {
      GLubyte px[16][4];
      DecodeColorBlock(in + (dxt1 ? 0 : 8), dxt1, px);
      if (format == ES_COMPRESSED_RGBA_S3TC_DXT3) DecodeAlphaBlockDXT3(in, px);
      else if (format == ES_COMPRESSED_RGBA_S3TC_DXT5) DecodeAlphaBlockDXT5(in, px);
      for (int y = 0; y < 4 && by + y < h; y++)
        for (int x = 0; x < 4 && bx + x < w; x++)
          memcpy(&rgba[((by + y) * w + bx + x) * 4], px[y * 4 + x], 4);
    }
}
