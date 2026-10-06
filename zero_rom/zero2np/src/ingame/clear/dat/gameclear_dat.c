// FILE: /home/zero_rom/zero2np/src/ingame/clear/dat/gameclear_dat.c
//
// The game-clear / result screen's sprite table (gameclear_tex[]).  Fifty
// records, and the grouping is what the draw code walks:
//
//   [ 0.. 5]  background, six pieces          game_result.c   GameResultBgDisp
//   [ 6..11]  the same six again              (the second background set)
//   [12,13]   screen title                    GameResultTopTitleDisp
//   [14]      "difficulty" label              GameResultTopDifficultyDisp
//   [15..18]  the four difficulty names       ... through difficulty_tex_tbl[]
//   [19,20]   "clear time" label              GameResultTopClearTime
//   [21,22]   "score" label                   GameResultTopScoreTime
//   [23]      "rank" label                    GameResultTopRankDisp
//   [24..30]  the seven rank glyphs           ... through rank_tex_tbl[]
//   [31..40]  the digits 0..9                 DrawCmnNumberTex zero_dat
//   [41,42]   the two h:mm:ss separators      GameResultTopClearTime
//   [43]      the score's unit plate          GameResultTopScoreTime
//   [44..47]  the horizontal rule, 4 pieces   GameResultTopLineDisp
//   [48,49]   the clear menu's title          clearmenu_top.c ClearMenuTopTitleDisp
//
// [13] and [22] are degenerate: w = h = 0, so they cover no pixels.  The ROM
// draws them anyway -- GameResultTopTitleDisp loops over [12],[13] and
// GameResultTopScoreTime over [21],[22] -- and so does the port.  Same
// flavour as n_finder_dat[12] in spirit_gage.o.
//
// The rule at [44..47] is a left cap, a 82-wide middle drawn five times
// stepping by its own w (79, 161, 243, 325, 407), a 72-wide piece and a right
// cap: 20 + 5*82 + 72 + 20 spans x = 59..581.
//
// The tex0 values are packed GS register payloads baked by the asset
// pipeline; they are preserved verbatim from the build.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), data
// 3165e0 (gameclear_dat.o, a data-only translation unit).

#include "gameclear_dat.h"          // gameclear_tex[] declaration (checked vs definition)

// ──────────────────────────────────────────────────────────────────────
// gameclear_tex[]  (data 3165e0) — result-screen sprites.
//   fields: { tex0, u, v, w, h, x, y, pri, alpha, flip, bln }

SPRT_DAT gameclear_tex[50] =
{
    /* [ 0] bg body      */ { 0x2006180665322bc0,   1,   1, 510, 448,   0,   0, 0, 128, 0, 1 },
    /* [ 1] bg right     */ { 0x2006188621312fc0,   1,   1, 116, 254, 510, 194, 0, 128, 0, 1 },
    /* [ 2]              */ { 0x2006188621312fc0, 135,   1, 116, 194, 510,   0, 0, 128, 0, 1 },
    /* [ 3]              */ { 0x2006188621312fc0, 119,   1,  14, 254, 626, 194, 0, 128, 0, 1 },
    /* [ 4]              */ { 0x2006188621312fc0, 135, 197, 120,  14, 626,  74, 0, 128, 0, 1 },
    /* [ 5]              */ { 0x2006188621312fc0, 135, 213,  74,  14, 626,   0, 0, 128, 0, 1 },

    /* [ 6] bg body 2    */ { 0x2006180665322bc0,   1,   1, 510, 448,   0,   0, 0, 128, 0, 1 },
    /* [ 7] bg right 2   */ { 0x2006188621312fc0,   1,   1, 116, 254, 510, 194, 0, 128, 0, 1 },
    /* [ 8]              */ { 0x2006188621312fc0, 135,   1, 116, 194, 510,   0, 0, 128, 0, 1 },
    /* [ 9]              */ { 0x2006188621312fc0, 119,   1,  14, 254, 626, 194, 0, 128, 0, 1 },
    /* [10]              */ { 0x2006188621312fc0, 135, 197, 120,  14, 626,  74, 0, 128, 0, 1 },
    /* [11]              */ { 0x2006188621312fc0, 135, 213,  74,  14, 626,   0, 0, 128, 0, 1 },

    /* [12] title        */ { 0x20059a4621412c50,   1,   1, 251,  67, 193,  86, 0, 128, 0, 1 },
    /* [13] title (none) */ { 0x20059a4621412c50,   0,   0,   0,   0,   0,   0, 0, 128, 0, 1 },

    /* [14] "difficulty" */ { 0x20059a4621412c50,   1,  70, 187,  34, 142, 173, 0, 128, 0, 1 },

    /* [15] hard         */ { 0x20059a2621412bd0,   1,  37, 143,  34, 359, 173, 0, 128, 0, 1 },
    /* [16] easy         */ { 0x20059a2621412bd0,   1,  73, 101,  37, 359, 173, 0, 128, 0, 1 },
    /* [17] normal       */ { 0x20059a2621412bd0,   1,   1, 129,  34, 359, 173, 0, 128, 0, 1 },
    /* [18] nightmare    */ { 0x20059a2621412bd0, 104,  73, 150,  37, 359, 173, 0, 128, 0, 1 },

    /* [19] "clear time" */ { 0x20059a4621412c50,   1, 106, 254,  36,  67, 210, 0, 128, 0, 1 },
    /* [20]              */ { 0x20059a4621412c50,   1, 144,   8,  36, 321, 210, 0, 128, 0, 1 },

    /* [21] "score"      */ { 0x20059a4621412c50,  11, 144, 226,  36, 103, 246, 0, 128, 0, 1 },
    /* [22] "score"(none)*/ { 0x20059a4621412c50,   0,   0,   0,   0,   0,   0, 0, 128, 0, 1 },

    /* [23] "rank"       */ { 0x20059a4621412c50,   1, 182, 246,  56,  91, 297, 0, 128, 0, 1 },

    /* [24] rank 0       */ { 0x20059a2621412bd0,  53, 112,  74,  66, 363, 289, 0, 128, 0, 1 },
    /* [25] rank 1       */ { 0x20059a2621412bd0,   1, 189,  46,  66, 376, 289, 0, 128, 0, 1 },
    /* [26] rank 2       */ { 0x20059a2621412bd0,  49, 189,  48,  66, 375, 289, 0, 128, 0, 1 },
    /* [27] rank 3       */ { 0x20059a2621412bd0,  99, 189,  50,  66, 374, 289, 0, 128, 0, 1 },
    /* [28] rank 4       */ { 0x20059a2621412bd0, 151, 189,  51,  66, 374, 289, 0, 128, 0, 1 },
    /* [29] rank 5       */ { 0x20059a2621412bd0, 204, 189,  51,  66, 374, 289, 0, 128, 0, 1 },
    /* [30] rank 6       */ { 0x20059a2621412bd0,   1, 112,  50,  66, 374, 289, 0, 128, 0, 1 },

    /* [31] digit 0      */ { 0x20059a059d40abc0,   1,   1,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [32] digit 1      */ { 0x20059a059d40abc0,  21,   1,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [33] digit 2      */ { 0x20059a059d40abc0,  41,   1,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [34] digit 3      */ { 0x20059a059d40abc0,  61,   1,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [35] digit 4      */ { 0x20059a059d40abc0,  81,   1,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [36] digit 5      */ { 0x20059a059d40abc0, 101,   1,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [37] digit 6      */ { 0x20059a059d40abc0,   1,  27,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [38] digit 7      */ { 0x20059a059d40abc0,  21,  27,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [39] digit 8      */ { 0x20059a059d40abc0,  41,  27,  18,  24,   0,   0, 0, 128, 0, 1 },
    /* [40] digit 9      */ { 0x20059a059d40abc0,  61,  27,  18,  24,   0,   0, 0, 128, 0, 1 },

    /* [41] h:mm sep     */ { 0x20059a059d40abc0,  81,  27,   9,  19, 406, 218, 0, 128, 0, 1 },
    /* [42] mm:ss sep    */ { 0x20059a059d40abc0,  81,  27,   9,  19, 459, 218, 0, 128, 0, 1 },

    /* [43] score unit   */ { 0x20059a059d40abc0,  92,  27,  35,  16, 478, 258, 0, 128, 0, 1 },

    /* [44] rule cap L   */ { 0x20059a2621412bd0,   1, 180,  20,   7,  59, 153, 0, 128, 0, 1 },
    /* [45] rule middle  */ { 0x20059a2621412bd0,  23, 180,  82,   7,  79, 153, 0, 128, 0, 1 },
    /* [46] rule tail    */ { 0x20059a2621412bd0,  23, 180,  72,   7, 489, 153, 0, 128, 0, 1 },
    /* [47] rule cap R   */ { 0x20059a2621412bd0, 107, 180,  20,   7, 561, 153, 0, 128, 0, 1 },

    /* [48] menu title   */ { 0x20057a059d40abc0,   1,   1, 126,  29, 231, 107, 0, 128, 0, 1 },
    /* [49]              */ { 0x20057a059d40abc0,   1,  32,  43,  29, 357, 107, 0, 128, 0, 1 },
};
