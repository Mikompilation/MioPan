// FILE: /home/zero_rom/zero2np/src/ingame/clear/dat/rank_time_dat.c
//
// The clear-time rank thresholds -- the whole of rank_time_dat.o, which has
// no code at all and this one 0x54-byte table.
//
// Seven ranks, 0 (best) to 6.  GameResultTopRankCheck() in game_result_top.c
// scans from 0 and stops at the first entry the play time does not exceed, so
// each row is the slowest clear that still earns that rank; anything past the
// last row is rank 6.  The seven glyphs the ranks draw with are
// gameclear_tex[24..30], picked through rank_tex_tbl[].
//
// PORT DEVIATION: the ROM's copy is in .rodata, so the source declared it
// const.  It is left non-const here because a namespace-scope `const` in C++
// has internal linkage, and game_result_top.c is a different translation unit.
// Nothing writes it.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), rdata
// 3c46c0.

#include "rank_time_dat.h"          // rank_time_tbl[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// rank_time_tbl[]  (rdata 3c46c0) — clear-time rank thresholds.
//   fields: { hour, min, sec }

int rank_time_tbl[7][3] =
{
    /* [0] */ {  2, 15, 0 },
    /* [1] */ {  2, 30, 0 },
    /* [2] */ {  3, 30, 0 },
    /* [3] */ {  5,  0, 0 },
    /* [4] */ {  7,  0, 0 },
    /* [5] */ {  8,  0, 0 },
    /* [6] */ { 10,  0, 0 },
};
