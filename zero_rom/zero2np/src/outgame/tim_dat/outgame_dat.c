// FILE: /home/zero_rom/zero2np/src/outgame/tim_dat/outgame_dat.c
//
// Shared outgame sprite table (out_game_tex[]).  Eight records out of one
// 128x128 sheet in OUTGAME_PK2, the pak every outgame screen loads alongside
// its own -- so these are the pieces that are the same on all of them.
//
//   [0,1]     the two-piece title frame           SaveLoadTitleFrameDisp
//   [2..4]    a three-piece bar: left, middle, right (mirrored left)
//   [5..7]    a narrower three-piece bar, same construction
//
// Entries [2..7] carry no position of their own -- every caller supplies one,
// which is why their x/y are zero here and only their u/v/w/h matter.
//
// The tex0 values are packed GS register payloads baked by the asset
// pipeline; they are preserved verbatim from the build.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), data
// 33bfd8 (outgame_dat.o, whose only content this is).

#include "outgame_dat.h"            // out_game_tex[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// out_game_tex[]  (data 33bfd8) — shared outgame sprites.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT out_game_tex[8] =
{
    /* [0] title frame L */ { 0x20058005dd30abc0,   7,  1, 90, 32,  2, 19, 160, 128, 0, 1 },
    /* [1] title frame R */ { 0x20058005dd30abc0,   7,  1, 90, 32, 92, 19, 160, 128, 2, 1 },

    /* [2] wide bar L    */ { 0x20058005dd30abc0,   1, 76, 64, 30,  0,  0, 160, 128, 0, 1 },
    /* [3] wide bar M    */ { 0x20058005dd30abc0,  67, 76, 10, 30,  0,  0, 160, 128, 0, 1 },
    /* [4] wide bar R    */ { 0x20058005dd30abc0,   1, 76, 64, 30,  0,  0, 160, 128, 2, 1 },

    /* [5] narrow bar L  */ { 0x20058005dd30abc0,  79, 76, 48, 28,  0,  0, 160, 128, 0, 1 },
    /* [6] narrow bar M  */ { 0x20058005dd30abc0, 107,  1, 10, 28,  0,  0, 160, 128, 0, 1 },
    /* [7] narrow bar R  */ { 0x20058005dd30abc0,  79, 76, 48, 28,  0,  0, 160, 128, 2, 1 },
};
