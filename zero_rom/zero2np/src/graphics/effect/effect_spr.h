/* ==========================================================================
 *  graphics/effect/effect_spr.h
 *
 *  The effect layer's two screen-space textured quads.  Both draw one
 *  effdat[] texture as a GS triangle strip in UV (FST) mode, with a half-texel
 *  inset on every edge, and both go out as a DIRECT DMA packet.
 *
 *  SetEffSQITex() takes an integer GS position (1/16 pixel, 2048-centred) and
 *  a caller-chosen blend mode; SetEffSQTex() takes a float pixel position,
 *  guard-band clips it, and uses a fixed blend.
 *
 *  COMPLETE -- both ZERO2.MAP .text symbols.
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x001652b0.
 * ======================================================================== */

#ifndef _GRAPHICS_EFFECT_EFFECT_SPR_H
#define _GRAPHICS_EFFECT_EFFECT_SPR_H

#include "eetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

/* --------------------------------------------------------------------------
 *  Screen-space effect quad, integer position.
 *
 *  `n`  indexes effdat[] directly -- unlike Set3DPosTexure(), this one does
 *       *not* add EffWrkMonochroModeGet(), so it always draws the colour twin
 *       and desaturates in the vertex colour instead.
 *  `v`  is { x, y, z } in raw GS units: x/y are 1/16 pixel biased by 2048,
 *       z is the 24-bit depth value written straight into XYZ2.
 *  `w`  is the half-width and `h` the half-height, both in pixels.  `h` is
 *       halved again when the display is interlaced.
 *  `tp` is a blend-mode bitfield: bit 0 -> ZBUF ZMSK, bit 1 -> ALPHA B,
 *       bit 2 -> ALPHA D.  Clear means Cd, set means 0, so tp == 0 is the
 *       ordinary (Cs-Cd)*As+Cd lerp and tp == 2 is additive.
 * ------------------------------------------------------------------------ */
void SetEffSQITex(int n, int *v, int tp, float w, float h,
                  u_char r, u_char g, u_char b, u_char a);

/* --------------------------------------------------------------------------
 *  Screen-space effect quad, float position.
 *
 *  `v` is { x, y, z } in GS *pixels* (2048-centred); z is scaled by 16 on the
 *  way into the packet, x/y by 16 after the half-extents are applied.  Unlike
 *  SetEffSQITex() this one guard-band clips -- any corner outside
 *  0x4000..0xc000, or a depth outside 0xff..0xfffffff, drops the whole quad.
 *  The blend mode is fixed at ALPHA 0x44; see the note on the static draw env
 *  in the .c, which captures `tp` only on the very first call.
 * ------------------------------------------------------------------------ */
void SetEffSQTex(int n, float *v, int tp, float w, float h,
                 u_char r, u_char g, u_char b, u_char a);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_EFFECT_EFFECT_SPR_H */
