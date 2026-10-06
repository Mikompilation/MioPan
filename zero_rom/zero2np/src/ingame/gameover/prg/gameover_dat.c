// FILE: /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover_dat.c
//
// Game-over screen sprite table (gameover_tex[]).  The folder's only
// data-only translation unit, and the smallest in the tree: two records,
// 0x40 bytes, and nothing else.
//
// Both entries share one tex0 and differ only in v and x, so this is one
// 124x62 source image cut into two 124x30 strips 32 texels apart:
//
//   [0]  left  half of the "GAME OVER" plate   v=1,  drawn at x=196
//   [1]  right half                            v=33, drawn at x=320
//
// 196 + 124 == 320, so the two pieces butt up exactly and read as a single
// 248x30 plate at (196, 107).  GameOverMenuTopTitleDisp() draws them in a
// two-iteration loop, which is why the table is a table at all.
//
// The tex0 values are packed GS register payloads baked by the asset
// pipeline; they are preserved verbatim from the build.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), data
// 316c20 (gameover_dat.o).

#include "gameover_dat.h"           // gameover_tex[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// gameover_tex[]  (data 316c20) — the "GAME OVER" title plate.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT gameover_tex[2] =
{
    /* [0] title left  */ { 0x20057a059d40abc0, 1,  1, 124, 30, 196, 107, 160, 128, 0, 0 },
    /* [1] title right */ { 0x20057a059d40abc0, 1, 33, 124, 30, 320, 107, 160, 128, 0, 0 },
};
