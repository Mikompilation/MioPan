// FILE: /home/zero_rom/zero2np/src/ingame/pause/prg/pause_dat.c
//
// Pause-screen sprite table.  Its own translation unit in the ROM
// (pause_dat.o holds nothing but this .data array).
//
// Only entry 0 is live: PauseTitleDisp() / MisPauseTitleDisp() copy it into a
// DISP_SPRT and blit it as the "PAUSE" caption.  Those two PARAM references to
// the table base are its only cross-references in the build, so entries 1..8
// -- the divider bars and corner pieces of a wider window layout, all homed at
// y 381..405 -- are dead data in this prototype.  They are kept because the
// array is one linked object and its size (0x120) is fixed by the map.
//
// The tex0 values are packed GS register payloads baked by the asset pipeline;
// they are preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "pause_dat.h"

// ──────────────────────────────────────────────────────────────────────
// pause_tex[]  (data 33c0f8)
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT pause_tex[9] =
{
    { 0x2007ed859db09ad8,  1,   1,  90, 25, 271, 145, 0, 128, 0, 0 },
    { 0x2007ed859db09ad8,  1, 123, 126,  4, 153, 382, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,  1, 123, 126,  4, 153, 405, 0, 128, 1, 1 },
    { 0x2007ed859db09ad8,  1, 123, 126,  4, 360, 382, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8,  1, 123, 126,  4, 360, 405, 0, 128, 1, 1 },
    { 0x2007ed859db09ad8, 66,  89,  61, 31, 151, 381, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8, 66,  89,  61, 31, 212, 381, 0, 128, 2, 1 },
    { 0x2007ed859db09ad8, 66,  89,  61, 31, 358, 381, 0, 128, 0, 1 },
    { 0x2007ed859db09ad8, 66,  89,  61, 31, 419, 381, 0, 128, 2, 1 },
};
