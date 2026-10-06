// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/tim_dat/btn_sprt_dat.c
//
// Button-icon sprite table (btn_tex[]) consumed by DrawCmnButton().  Each entry
// is a SPRT_DAT (compact caller sprite record): the source (u,v,w,h) rectangle
// inside the button texture, an optional screen (x,y) home, priority, alpha,
// flip and blend flags, plus the packed GS TEX0 register that selects the
// texture page / CLUT for the sprite.
//
// Entries 0..6 share one texture page (TEX0 0x2007e00622410000: the common UI
// glyph atlas) and are placed by the caller; entries 7..14 carry their own
// screen homes for the fixed decorative frame/arrow art.
//
// The tex0 values are packed GS register payloads baked by the asset pipeline;
// they are preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "btn_sprt_dat.h"          // btn_tex[] declaration (checked vs definition)
#include "../g2d_draw.h"            // SPRT_DAT

// ──────────────────────────────────────────────────────────────────────
// btn_tex[]  (data 2d7e68) — button-icon sprites.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT btn_tex[15] =
{
    { 0x2007e00622410000,   1,  1, 26, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007e00622410000,  84,  1, 26, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007e00622410000,  29,  1, 26, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007e00622410000,  57,  1, 25, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 112,  1, 26, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 140,  1, 56, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 198,  1, 56, 28,   0,   0, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,   1, 59, 126,  4, 153, 225, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,   1, 59, 126,  4, 153, 248, 0, 128, 1, 1 },
    { 0x2007ed859db09ad8,   1, 27,  61, 31, 151, 224, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,   1, 27,  61, 31, 212, 224, 0, 128, 2, 1 },
    { 0x2007ed859db09ad8,  99, 41,  11, 12, 209, 172, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,  80, 41,  17, 16, 206, 170, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,  99, 41,  11, 12, 422, 172, 0, 128, 2, 1 },
    { 0x2007ed859db09ad8,  80, 41,  17, 16, 419, 170, 0, 128, 2, 1 },
};
