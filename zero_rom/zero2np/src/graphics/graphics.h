/* ===========================================================================
 *  graphics/graphics.h
 *
 *  Immediate-mode 2D/3D primitive drawing, screen capture, and billboard
 *  packet helpers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPHICS_H
#define _GRAPHICS_GRAPHICS_H

#include "eetypes.h"
#include "sce_gs.h"

void DrawHitCircle(float *mpos, float rot_y, int adj_y,
                   u_char col, u_char alp, float rad);
void SetLine2DPacket(float x1, float y1, float x2, float y2,
                     u_char r, u_char g, u_char b, u_char a);
void SetLine2D(float x1, float y1, float x2, float y2,
               u_char r, u_char g, u_char b, u_char a);
void DrawHitCircle2D(float *mpos, float rot_y, int adj_y,
                     u_char col, u_char alp, float rad);

void Draw3DSquare(float *mpos1, float *mpos2, float *mpos3, float *mpos4,
                  u_char r, u_char g, u_char b, u_char a);
void DrawCrossLineA(float *bpos, u_char alp);
void DrawCrossLine(float *bpos);
void DrawLine(float *mpos1, u_char r1, u_char g1, u_char b1, u_char a1,
              float *mpos2, u_char r2, u_char g2, int b2, int a2);
void DrawLineTestOff(float *mpos1, u_char r1, u_char g1, u_char b1, u_char a1,
                     float *mpos2, u_char r2, u_char g2, int b2, int a2);
void DrawCrossLineLast(float *bpos);
void DrawCrossLineLastReal(void);

void SetSquareS(int pri, float x1, float y1, float x4, float y4,
                u_char r, u_char g, u_char b, u_char a);
void SetSquare(int pri, float x1, float y1, float x2, float y2,
               float x3, float y3, float x4, float y4,
               u_char r, u_char g, u_char b, u_char a);
void SetSquare2s(int pri, float x1, float y1, float x4, float y4,
                 u_char r1, u_char g1, u_char b1,
                 u_char r2, u_char g2, u_char b2, u_char a);
void SetSquareZ(int pri, float x1, float y1, float x4, float y4, int z);
void SetSquareSN(int pri, float x1, float y1, float x4, float y4,
                 u_char r, u_char g, u_char b, u_char a);
void SetSquareN(int pri, float x1, float y1, float x2, float y2,
                float x3, float y3, float x4, float y4,
                u_char r, u_char g, u_char b, u_char a);
void SetTriangle(int pri, float x1, float y1, float x2, float y2,
                 float x3, float y3, u_char r, u_char g, u_char b, u_char a);
void SetTriangleZ(int pri, float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float x3, float y3, float z3,
                  u_char r, u_char g, u_char b, u_char a);
void SetLine(int pri, float x1, float y1, float x2, float y2,
             u_char r, u_char g, u_char b, u_char a);
void SetLine2(int pri, float x1, float y1, float x2, float y2,
              u_char r, u_char g, u_char b, u_char a);
void SetLine2PC(int pri, float x1, float y1, u_char r1, u_char g1, u_char b1,
                float x2, float y2, u_char r2, u_char g2, u_char b2, u_char a);

void DrawPoint(float *mpos, int no);
void DrawPoint2(float *mpos, u_char r, u_char g, u_char b, u_char a);
void DrawSphere(float fRange, float x, float y, float z,
                u_char r, u_char g, u_char b, u_char a, float *vScale);
void DrawTube(float *p1, float *p2, float rad,
              u_char r, u_char g, u_char b, u_char a);

void CaptureScreen(u_int addr);
void DrawScreen(u_int pri, u_int addr, u_char r, u_char g, u_char b, u_char a);
void MakePacket3D(float (*pa3DPos)[4], int iNum,
                  int iR, int iG, int iB, int iA,
                  float fUX, float fUY, float fUW, float fUH,
                  sceGsTex0 Tex0, int iZOffset);
void MakeBillBoardPacket(float *Pos, float fWidth, float fHeight,
                         int iR, int iG, int iB, int iA,
                         float fUX, float fUY, float fUW, float fUH,
                         sceGsTex0 Tex0, int iZOffset);

/* PORT-ONLY, no ROM counterpart.  Draws `count` (3 or 4) world-space corners
 * as a host textured triangle list, wound 0-1-2 / 1-3-2 to match the PS2
 * triangle strip.  UVs are texels within a `tw` x `th` texture.  Every ROM
 * function that builds a 3D textured GIF packet queues this alongside, since
 * the DIRECT packet itself is inert in this port. */
/* `z_offset` is MakePacket3D()'s iZOffset: the GS-Z bias the ROM's DIRECT
 * packet adds so a coplanar quad wins its depth test.  Defaulted to 0 because
 * only the movie-room screen uses it. */
void RendererPacket3D(float (*positions)[4], int count,
                      int r, int g, int b, int a,
                      float ux, float uy, float uw, float uh,
                      float tw, float th, const sceGsTex0 *tex0,
                      int z_offset = 0);

/* PORT-ONLY, no ROM counterpart.  RendererPacket3D with the texture
 * coordinates given per source vertex rather than derived from a
 * sub-rectangle.  `uv` is two normalised floats per corner, in the caller's
 * own corner order.
 *
 * The rectangle form above assumes the strip runs TL, TR, BL, BR -- u along
 * v0->v1 and v along v0->v2.  A quad whose corners are not laid out that way
 * needs its own table, or the rectangle form transposes its texture:
 * effect_oth.c's candle flame runs one column and then the other, so u there
 * has to follow v0->v2 instead. */
void RendererPacket3DUV(float (*positions)[4], int count, const float *uv,
                        int r, int g, int b, int a, const sceGsTex0 *tex0,
                        int z_offset = 0);

/* PORT-ONLY, no ROM counterpart.  An untextured gouraud triangle list in world
 * space: `positions` is `vertex_count` vec4 world points and `rgba` four bytes
 * per vertex in the GS's own scale (colour 0..255, alpha 0..128).  Neither
 * RendererPacket3D (one flat colour, textured) nor the flat-fill path can
 * carry a per-vertex colour, which is what effect_ene.c's damage trail needs:
 * its bright centre and transparent edges are one gouraud ribbon. */
void RendererGouraudTriangles3D(float (*positions)[4], const u_char *rgba,
                                int vertex_count);

/* PORT-ONLY, no ROM counterpart.  One world-space line, gouraud from p1's
 * colour to p2's; components are raw PS2 0..128.  Same reason as above: the
 * GS line packet the ROM emits alongside is never executed here. */
void RendererWorldLine(const float *p1, const float *p2,
                       u_char r1, u_char g1, u_char b1, u_char a1,
                       u_char r2, u_char g2, u_char b2, u_char a2,
                       int depth_test);

#endif /* _GRAPHICS_GRAPHICS_H */
