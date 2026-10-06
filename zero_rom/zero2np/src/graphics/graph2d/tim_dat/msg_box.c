// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/tim_dat/msg_box.c
//
// Message-box frame sprite table (msg_box_tex[]) used by the common-window
// widgets (DrawCmnWindow / DrawCmnTwoLineWindow, draw_cmn_win.c).  The twelve
// entries are the corner and edge pieces of the rounded message-box frame:
// vertical edges (0..3), horizontal edges (4..7), the joint pieces (8,9) and
// the rounded corner caps (10,11).  All share one texture page
// (TEX0 0x2007f58599b06138) and carry their default screen homes.
//
// The tex0 value is a packed GS register payload baked by the asset pipeline;
// it is preserved verbatim from the build.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "msg_box.h"               // msg_box_tex[] declaration (checked vs definition)
#include "../g2d_draw.h"            // SPRT_DAT

// ──────────────────────────────────────────────────────────────────────
// msg_box_tex[]  (data 338400) — message-box frame pieces.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT msg_box_tex[12] =
{
    { 0x2007f58599b06138,  1,  7,  4, 45,  22, 324, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  1,  7,  4, 45, 612, 324, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  1,  7,  4, 45,  22, 393, 0, 128, 1, 1 },
    { 0x2007f58599b06138,  1,  7,  4, 45, 612, 393, 0, 128, 1, 1 },
    { 0x2007f58599b06138,  1,  1, 46,  4,   8, 335, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  1,  1, 46,  4, 586, 335, 0, 128, 2, 1 },
    { 0x2007f58599b06138,  1,  1, 46,  4,   8, 425, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  1,  1, 46,  4, 586, 425, 0, 128, 2, 1 },
    { 0x2007f58599b06138, 49,  1, 10,  4,  54, 335, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  1, 53,  4, 10,  22, 369, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  7,  7, 30, 16,  24, 339, 0, 128, 0, 1 },
    { 0x2007f58599b06138,  7,  7, 30, 16, 586, 339, 0, 128, 2, 1 },
};
