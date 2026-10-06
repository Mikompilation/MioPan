// FILE: /home/zero_rom/zero2np/src/save_load/dat/save_load_dat.c
//
// Save / load screen sprite table (save_load_tex[]).  Sixty records, and the
// grouping is exactly what save_load_disp.c walks:
//
//   [ 0..12]  the outer frame                     SaveLoadFrameDisp
//   [13..15]  the slot strip: left cap, tile x8, right cap        "
//   [16,17]   the two-piece slot cursor           SaveLoadCursorDisp
//   [18,19]   the two-piece selection flare       SaveLoadSelFlareDisp
//   [20]      the snapshot drop shadow            SaveLoadSnapShadowDisp
//   [21]      "DATA" plate, dim                   SaveLoadNonSelNoDisp
//   [22..26]  slot numbers 1..5, dim              SaveLoadNonSelDataNumDisp
//   [27]      underline, dim                      SaveLoadNonSelLineDisp
//   [28]      "DATA" plate, lit                   SaveLoadSelNoDisp
//   [29..33]  slot numbers 1..5, lit              SaveLoadSelDataNumDisp
//   [34]      underline, lit                      SaveLoadSelLineDisp
//   [35..38]  the four-quadrant clear glow        SaveLoadClearFlareDisp
//   [39]      the clear-count frame               SaveLoadClearFrameDisp
//   [40]      the no-clear mask over it           SaveLoadNonClearMaskDisp
//   [41..50]  the ten 12x14 clear-count digits    SaveLoadClearNumberDisp_One
//   [51,52]   "MEMORY CARD" / "(8MB)"             SaveLoadMemoryCardSlotDisp
//   [53,54]   slot glyph, port 1 / port 2                 "
//   [55,56]   the two-piece "SAVE" title          SaveLoadTitleSaveDisp
//   [57,58]   the two-piece "LOAD" title          SaveLoadTitleLoadDisp
//   [59]      the slot snapshot itself            SaveLoadSnapShotDisp
//
// The five slots are one set of records drawn five times, stepped 118 pixels
// apart by the caller's data_num -- which is why every per-slot entry carries
// the *first* slot's x and nothing else distinguishes them.
//
// Groups of four sharing a tex0 and differing only in flip are one quadrant
// image mirrored: [35..38] is the clear glow with flips 0/2/1/3.  The dim and
// lit label sets ([21..27] vs [28..34]) are two separate 128x64 textures with
// identical geometry, which is why their u/v/w/h agree exactly.
//
// The tex0 values are packed GS register payloads baked by the asset
// pipeline; they are preserved verbatim from the build.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), data
// 33e498 (save_load_dat.o, whose only content this is).

#include "save_load_dat.h"          // save_load_tex[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// save_load_tex[]  (data 33e498) — save / load screen sprites.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT save_load_tex[60] =
{
    /* --- outer frame ------------------------------------------------ */
    /* [ 0] */ { 0x2005c04625422bd0,   1,   1,  73, 170,   12,  55,   0, 128, 0, 1 },
    /* [ 1] */ { 0x2005c04625422bd0,  76,   1,  68, 170,  556,  55,   0, 128, 0, 1 },
    /* [ 2] */ { 0x2005c04625422bd0, 146,   1, 365, 120,   85, 105,   0, 128, 0, 1 },
    /* [ 3] */ { 0x2005c04625422bd0, 146, 123, 106, 120,  450, 105,   0, 128, 0, 1 },
    /* [ 4] */ { 0x2005c04625422bd0, 254, 123, 257, 125,   12, 225,   0, 128, 0, 1 },
    /* [ 5] */ { 0x2005c04625422bd0,   1, 173,  49,  11,  269, 332,   0, 128, 0, 1 },
    /* [ 6] */ { 0x2005c04625422bd0,   1, 173,  49,  11,  318, 332,   0, 128, 0, 1 },
    /* [ 7] */ { 0x2005c04625422bd0, 254, 123, 257, 125,  367, 225,   0, 128, 2, 1 },
    /* [ 8] */ { 0x2005c06621312cd0,   1, 171,  98,  41,   75,  63,   0, 128, 0, 1 },
    /* [ 9] */ { 0x2005c06621312cd0,   1, 214,  98,  41,  173,  63,   0, 128, 0, 1 },
    /* [10] */ { 0x2005c06621312cd0,   1, 214,  98,  41,  271,  63,   0, 128, 0, 1 },
    /* [11] */ { 0x2005c06621312cd0,   1, 214,  98,  41,  369,  63,   0, 128, 0, 1 },
    /* [12] */ { 0x2005c06621312cd0,   1, 171,  98,  41,  467,  63,   0, 128, 2, 1 },

    /* --- slot strip: left cap, the tile drawn eight times, right cap - */
    /* [13] */ { 0x2005c06621312cd0, 101, 143,  50, 112,   68, 222,   0, 128, 0, 1 },
    /* [14] */ { 0x2005c06621312cd0, 153, 143,  50, 112,  118, 222,   0, 128, 0, 1 },
    /* [15] */ { 0x2005c06621312cd0, 101, 143,  50, 112,  518, 222,   0, 128, 2, 1 },

    /* --- slot cursor ------------------------------------------------- */
    /* [16] */ { 0x2005c06621312cd0,   1,   1,  77, 117,    6, 106,   0, 128, 0, 1 },
    /* [17] */ { 0x2005c06621312cd0,   1,   1,  77, 117,   83, 106,   0, 128, 2, 1 },

    /* --- selection flare (additive, alphar 0x48) --------------------- */
    /* [18] */ { 0x2005c06621312cd0,  80,  37,  54,  90,   32, 121,   0,  64, 0, 1 },
    /* [19] */ { 0x2005c06621312cd0, 136,  68,  47,  59,   86, 152,   0,  64, 0, 1 },

    /* --- snapshot drop shadow ---------------------------------------- */
    /* [20] */ { 0x2005c06621312cd0, 138,   1, 112,  62,   27, 151,   0, 128, 0, 1 },

    /* --- "DATA n" row, dim ------------------------------------------- */
    /* [21] */ { 0x2005c0e59d40add0,   1,   1,  41,  28,   31, 120,   0, 128, 0, 0 },
    /* [22] */ { 0x2005c0e59d40add0,  44,   1,  18,  28,   72, 120,   0, 128, 0, 0 },
    /* [23] */ { 0x2005c0e59d40add0,  64,   1,  18,  28,  188, 120,   0, 128, 0, 0 },
    /* [24] */ { 0x2005c0e59d40add0,  84,   1,  18,  28,  308, 120,   0, 128, 0, 0 },
    /* [25] */ { 0x2005c0e59d40add0, 104,   1,  18,  28,  425, 120,   0, 128, 0, 0 },
    /* [26] */ { 0x2005c0e59d40add0,   1,  31,  20,  28,  543, 120,   0, 128, 0, 0 },
    /* [27] */ { 0x2005c0e59d40add0,  23,  31,  51,  10,   35, 129,   0, 128, 0, 1 },

    /* --- "DATA n" row, lit ------------------------------------------- */
    /* [28] */ { 0x2005c1059d40ade0,   1,   1,  41,  28,   31, 120,   0, 128, 0, 0 },
    /* [29] */ { 0x2005c1059d40ade0,  44,   1,  18,  28,   72, 120,   0, 128, 0, 0 },
    /* [30] */ { 0x2005c1059d40ade0,  64,   1,  18,  28,  188, 120,   0, 128, 0, 0 },
    /* [31] */ { 0x2005c1059d40ade0,  84,   1,  18,  28,  308, 120,   0, 128, 0, 0 },
    /* [32] */ { 0x2005c1059d40ade0, 104,   1,  18,  28,  425, 120,   0, 128, 0, 0 },
    /* [33] */ { 0x2005c1059d40ade0,   1,  31,  20,  28,  543, 120,   0, 128, 0, 0 },
    /* [34] */ { 0x2005c1059d40ade0,  23,  31,  51,  10,   35, 129,   0, 128, 0, 1 },

    /* --- clear glow: one quadrant, four flips ------------------------ */
    /* [35] */ { 0x2005c06621312cd0,  80,   1,  56,  34,   27, 150,   0, 128, 0, 1 },
    /* [36] */ { 0x2005c06621312cd0,  80,   1,  56,  34,   83, 150,   0, 128, 2, 1 },
    /* [37] */ { 0x2005c06621312cd0,  80,   1,  56,  34,   27, 184,   0, 128, 1, 1 },
    /* [38] */ { 0x2005c06621312cd0,  80,   1,  56,  34,   83, 184,   0, 128, 3, 1 },

    /* --- clear-count frame, and the mask that hides it --------------- */
    /* [39] */ { 0x2005c06621312cd0,   1, 120,  44,  37,   97, 112,   0, 128, 0, 1 },
    /* [40] */ { 0x2005c06621312cd0,  47, 120,  31,  22,  102, 119,   0, 128, 0, 1 },

    /* --- clear-count digits 0..9 (positioned by the caller) ---------- */
    /* [41] */ { 0x2005c06621312cd0, 185,  65,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [42] */ { 0x2005c06621312cd0, 199,  65,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [43] */ { 0x2005c06621312cd0, 213,  65,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [44] */ { 0x2005c06621312cd0, 227,  65,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [45] */ { 0x2005c06621312cd0, 241,  65,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [46] */ { 0x2005c06621312cd0, 185,  81,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [47] */ { 0x2005c06621312cd0, 199,  81,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [48] */ { 0x2005c06621312cd0, 213,  81,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [49] */ { 0x2005c06621312cd0, 227,  81,  12,  14,    0,   0,   0, 128, 0, 0 },
    /* [50] */ { 0x2005c06621312cd0, 241,  81,  12,  14,    0,   0,   0, 128, 0, 0 },

    /* --- card caption, and the port glyph beside it ------------------ */
    /* [51] */ { 0x2005c1259d40adf0,   1,   1, 126,  30,  185,  67,   0, 128, 0, 0 },
    /* [52] */ { 0x2005c1259d40adf0,   1,  33, 126,  30,  311,  66,   0, 128, 0, 0 },
    /* [53] */ { 0x2005c06621312cd0, 185,  97,  23,  27,  430,  66,   0, 128, 0, 0 },
    /* [54] */ { 0x2005c06621312cd0, 210,  97,  23,  27,  430,  66,   0, 128, 0, 0 },

    /* --- screen titles ----------------------------------------------- */
    /* [55] */ { 0x2005c0259940abc8,   1,   1,  62,  28,   28,  21, 160, 128, 0, 0 },
    /* [56] */ { 0x2005c0259940abc8,   1,  31,  62,  28,   90,  21, 160, 128, 0, 0 },
    /* [57] */ { 0x2005c0059940abc0,   1,   1,  62,  28,   28,  21, 160, 128, 0, 0 },
    /* [58] */ { 0x2005c0059940abc0,   1,  31,  62,  28,   90,  21, 160, 128, 0, 0 },

    /* --- the slot snapshot; its pak is passed in, not this one ------- */
    /* [59] */ { 0x20057c059d30abc0,   1,   1, 105,  62,   29, 152,   0, 128, 0, 1 },
};
