// FILE: /home/zero_rom/zero2np/src/ingame/savepoint/tim_dat/savepoint_dat.c
//
// Save-point sprite table (savepoint_tex[]).  Sixteen records in five groups,
// and the grouping is what the draw code in savepoint_disp.c walks:
//
//   [0]      moyou (pattern) layer 1        SavePoint_BgPattern1Disp
//   [1]      moyou layer 2                  SavePoint_BgPattern2Disp
//   [2..5]   the four corner motes          SavePoint_BgFleaDisp
//   [6..9]   the four drifting motes        SavePoint_FleaDisp
//   [10..13] the four corner vignettes      SavePoint_ShadowDisp
//   [14,15]  the two-piece screen title     SavePointTopTitleDisp
//
// Each group of four is one source image drawn four times with flip 0/1/2/3,
// which is why the four entries share a tex0 and differ only in x/y/flip: the
// artwork is one quadrant and the other three are mirrored copies.  The
// [6..9] and [10..13] positions look like they sit off-screen because both
// groups are drawn at 3x / 320x224-normalised scale respectively.
//
// The two title pieces come from one 126x39 strip: [14] is the left 126
// columns and [15] the next 114 starting at v=42.
//
// The tex0 values are packed GS register payloads baked by the asset
// pipeline; they are preserved verbatim from the build.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), data
// 33ece8 (savepoint_dat.o, the folder's only data-only translation unit).

#include "savepoint_dat.h"          // savepoint_tex[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// savepoint_tex[]  (data 33ece8) — save-point screen sprites.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT savepoint_tex[16] =
{
    /* [ 0] moyou 1     */ { 0x2005a72621412c30, 0,  0, 256, 256,    0,    0, 160, 128, 0, 1 },
    /* [ 1] moyou 2     */ { 0x2005a74621412cb0, 0,  0, 256, 256,    0,    0, 160, 128, 0, 1 },

    /* [ 2] bg mote     */ { 0x2005a6a59930ac20, 1,  1,  62,  62,  172,   60, 160, 128, 0, 1 },
    /* [ 3]             */ { 0x2005a6a59930ac20, 1,  1,  62,  62,  172,  209, 160, 128, 1, 1 },
    /* [ 4]             */ { 0x2005a6a59930ac20, 1,  1,  62,  62,  321,   60, 160, 128, 2, 1 },
    /* [ 5]             */ { 0x2005a6a59930ac20, 1,  1,  62,  62,  321,  209, 160, 128, 3, 1 },

    /* [ 6] mote        */ { 0x2005a605dd40abc0, 1,  1, 126, 126,  -58, -170, 160, 128, 0, 1 },
    /* [ 7]             */ { 0x2005a605dd40abc0, 1,  1, 126, 126,  -58,  208, 160, 128, 1, 1 },
    /* [ 8]             */ { 0x2005a605dd40abc0, 1,  1, 126, 126,  320, -170, 160, 128, 2, 1 },
    /* [ 9]             */ { 0x2005a605dd40abc0, 1,  1, 126, 126,  320,  208, 160, 128, 3, 1 },

    /* [10] vignette    */ { 0x2005a625dd30abe0, 1,  1, 126, 126,    0,    0, 160, 128, 0, 1 },
    /* [11]             */ { 0x2005a625dd30abe0, 1,  1, 126, 126,    0,  224, 160, 128, 1, 1 },
    /* [12]             */ { 0x2005a625dd30abe0, 1,  1, 126, 126,  320,    0, 160, 128, 2, 1 },
    /* [13]             */ { 0x2005a625dd30abe0, 1,  1, 126, 126,  320,  224, 160, 128, 3, 1 },

    /* [14] title left  */ { 0x20057c05dd40abc0, 1,  1, 126,  39,  199,  103, 160, 128, 0, 0 },
    /* [15] title right */ { 0x20057c05dd40abc0, 1, 42, 114,  39,  325,  103, 160, 128, 0, 0 },
};
