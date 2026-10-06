// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/tim_dat/caption_dat.c
//
// Common-caption sprite table (caption_tex[]) consumed by DrawCmnCaption().
// Each entry is a SPRT_DAT selecting a 126x28 caption strip inside the shared
// caption texture page (TEX0 0x2007e00622410000); the caller supplies the
// screen (x,y), so the home coordinates here stay zero.  Indices 1 and 7 are
// empty slots (zero rectangle).
//
// The tex0 value is a packed GS register payload baked by the asset pipeline;
// it is preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "caption_dat.h"           // caption_tex[] declaration (checked vs definition)
#include "../g2d_draw.h"            // SPRT_DAT

// ──────────────────────────────────────────────────────────────────────
// caption_tex[]  (data 2d8af0) — common caption strips.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT caption_tex[17] =
{
    { 0x2007e00622410000, 129,  31, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   0,   0,   0,  0, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1,  61, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129,  31, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1, 181, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129,  61, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129, 151, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   0,   0,   0,  0, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129, 181, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1,  31, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1, 121, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129, 211, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1,  91, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129, 121, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1, 211, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000, 129,  91, 126, 28, 0, 0, 0, 128, 0, 1 },
    { 0x2007e00622410000,   1, 151, 126, 28, 0, 0, 0, 128, 0, 1 },
};
