/* ==========================================================================
 *  graphics/graph2d/graph2d.h
 *
 *  Public interface for the 2D graphics subsystem: the per-frame driver and
 *  boot/effect-frame hooks (g2d_main.c) plus the handful of cross-subsystem
 *  entry points the rest of the engine reaches the 2D layer through.
 *
 *  This header is referenced by name from system.c (InitGraph2dEFrame /
 *  DrawPerformanceCounter2 are called out of the per-frame DMA back end).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_GRAPH2D_H
#define _GRAPHICS_GRAPH2D_GRAPH2D_H

#include "g2d_draw.h"               /* SPRT_DAT (fntdat) */

/* --------------------------------------------------------------------------
 *  Subsystem work block (g2d_main.c).
 * ------------------------------------------------------------------------ */
typedef struct G2D_WRK              /* 0x8 */
{
    /* 0x0 */ int init;
    /* 0x4 */ int flow;
} G2D_WRK;

extern G2D_WRK g2d_wrk;

/* Font texture-descriptor table (g2d_main.c): the per-bank glyph SPRT_DAT
 * source records the font renderer indexes.  (The initialised table itself is
 * a data-reconstruction gap; only the declaration lives here.) */
extern SPRT_DAT fntdat[6];          /* data 315570 */

/* Effect texture-descriptor table (g2d_main.c): one SPRT_DAT per effect
 * texture, in colour/monochrome pairs.  Set3DPosTexure() indexes it as
 * effdat[texno + EffWrkMonochroModeGet()], so the odd entry of each pair is
 * the desaturated twin of the even one. */
extern SPRT_DAT effdat[98];         /* data 314930 */

/* --------------------------------------------------------------------------
 *  Subsystem entry points (g2d_main.c).
 * ------------------------------------------------------------------------ */
void InitGraph2dON(void);
void InitGraph2dBoot(void);
void InitGraph2dEFrame(void);
void Graph2dMain(void);

/* --------------------------------------------------------------------------
 *  2D debug overlays (graphics/effect/g2d_debug.c).
 *
 *  DrawPerformanceCounter2() is the frame performance meter, drawn from the
 *  per-frame DMA back end in system.c.  The rest of the object is what is left
 *  of a much larger debug file: three functions whose bodies were removed at
 *  the source level and four globals with no reader anywhere in the ROM.  They
 *  are declared here because g2d_debug.c has no header of its own that the
 *  debug info can place.
 * ------------------------------------------------------------------------ */
void DrawPerformanceCounter2(int draw_counter);

void InitShibataSet(void);
void SetShibataSet(void);
void CheckHintTex(void);

extern int dither_alp;              /* sdata 3f09e0 */
extern int dither_col;              /* sdata 3f09e4 */
extern int hint_test_sw;            /* sdata 3f09f8 */
extern int hint_test_posx;          /* sdata 3f09fc */

#endif /* _GRAPHICS_GRAPH2D_GRAPH2D_H */
