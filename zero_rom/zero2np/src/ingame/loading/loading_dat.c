// FILE: /home/zero_rom/zero2np/src/ingame/loading/loading_dat.c
//
// Loading-screen sprite table (loading_tex[]).  Entries 0/1 are the scrolling
// background pattern (drawn twice, offset by width, for a seamless wrap);
// entry 2 is the scrolling cloud layer (drawn via the csx/csy scroll-region
// fields); entries 3/4 are the "NOW LOADING" caption and its glyph.
//
// The tex0 values are packed GS register payloads baked by the asset
// pipeline; they are preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "loading_dat.h"           // loading_tex[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// loading_tex[]  (data 3199d8) — loading-screen sprites.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT loading_tex[5] =
{
    { 0x2005d8c625422dc0,   0,   0, 512, 256, 130, 194, 0, 128, 0, 1 },
    { 0x2005d82625422c40,   0,   0, 512, 256, 130, 194, 0, 128, 0, 1 },
    { 0x2005d80621412bc0,   0,   0, 256, 256, 130, 194, 0, 128, 0, 1 },
    { 0x2005d845e1312d40,   1,   1, 254,  46, 295, 346, 0, 128, 0, 1 },
    { 0x2005d845e1312d40,   1,  49,  84,  51, 549, 349, 0, 128, 0, 1 },
};
