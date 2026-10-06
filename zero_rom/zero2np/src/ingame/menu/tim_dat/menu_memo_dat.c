// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/menu_memo_dat.c
//
// The notes page's sprite table (menu_memo_dat.o, data 0x3285f8).
//
// One flat SPRT_DAT array covering the whole page.  The groups menu_memo.c
// walks are:
//
//     0..3     the paper, four tiles laid out by MenuMemoBgDisp()
//     4..5     the two edge rails, drawn with rot 270 -- with the rotation
//              about (x, y + w) the sprite's own width is what steps y
//     6..7     the page title plate, drawn from the MENU_BG pak; [7] is [6]
//              mirrored (flip 2) and butted against it
//     8        the word itself, out of the memo pak
//     9..42    the memo titles at revision 0.  Each is a vertical column of
//              brush text split into one, two or three tiles stacked down
//              the page at fixed scattered positions -- which is why
//              memo_first_tbl[] is [20][3] with -1 padding.
//     43..61   the titles that CHANGE at revision 1.  memo_second_tbl[] is
//              the same shape and repeats a first-revision index wherever
//              the fuller memo reuses the same art (rows 3, 4, 6, 7, 8, 10,
//              12, 13, 15, 17 and 19 are identical in both tables).
//     62..63   the unread bracket and its mirror, drawn either side of a
//              list row by MenuMemoNonReadFrameDisp()
//
// Entries 0..7, 62 and 63 carry bln 1; everything from 8 on is bln 0, which
// is what lets MenuMemoPlyrMemoDisp() tint a title with r/g/b.
//
// Read straight out of the ROM's .data and diffed against it byte-for-byte.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "menu_memo_dat.h"

SPRT_DAT menu_memo_tex[64] =                                /* data 3285f8 */
{
    /* ---- the paper itself -- four tiles of the memo page background ---- */
    /*   0 */ { 0x20066a6665322d76ULL,    1,    1,  510,  448,     0,     0,    0, 0x80, 0, 1 },
    /*   1 */ { 0x20066ae621313176ULL,    1,    1,  116,  254,   510,   194,    0, 0x80, 0, 1 },
    /*   2 */ { 0x20066ae621313176ULL,  119,    1,   14,  254,   626,   194,    0, 0x80, 0, 1 },
    /*   3 */ { 0x20066ae621313176ULL,  135,    1,  116,  194,   510,     0,    0, 0x80, 0, 1 },

    /* ---- the two edge rails, drawn with rot 270 so their own w steps y ---- */
    /*   4 */ { 0x20066ae621313176ULL,  135,  197,  120,   14,   626,    74,    0, 0x80, 0, 1 },
    /*   5 */ { 0x20066ae621313176ULL,  135,  213,   74,   14,   626,     0,    0, 0x80, 0, 1 },

    /* ---- the page title plate, one half mirrored (out of the MENU_BG pak) ---- */
    /*   6 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,     2,    19,  160, 0x80, 0, 1 },
    /*   7 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,    92,    19,  160, 0x80, 2, 1 },

    /* ---- the word "MEMO" (out of the memo pak) ---- */
    /*   8 */ { 0x20066a6665322d76ULL,    1,  451,   71,   25,    54,    23,    0, 0x80, 0, 0 },

    /* ---- memo title art, first revision -- memo_first_tbl[] indexes here ---- */
    /*   9 */ { 0x200666c5d940abc0ULL,    1,    1,   62,  126,   403,   100,    0, 0x80, 0, 0 },
    /*  10 */ { 0x200667059940abf0ULL,    1,    1,   30,   62,   266,   103,    0, 0x80, 0, 0 },
    /*  11 */ { 0x200667059940abf0ULL,   33,    1,   30,   62,   266,   165,    0, 0x80, 0, 0 },
    /*  12 */ { 0x200667459d40ac00ULL,    1,    1,   46,   62,   190,   202,    0, 0x80, 0, 0 },
    /*  13 */ { 0x200667459d40ac00ULL,   49,    1,   45,   62,   202,   264,    0, 0x80, 0, 0 },
    /*  14 */ { 0x200667459d40ac00ULL,   96,    1,   31,   30,   223,   326,    0, 0x80, 0, 0 },
    /*  15 */ { 0x200667859940ac20ULL,    1,    1,   56,   62,   538,   213,    0, 0x80, 0, 0 },
    /*  16 */ { 0x200667a59940ac28ULL,    1,    1,   30,   62,   509,   160,    0, 0x80, 0, 0 },
    /*  17 */ { 0x200667a59940ac28ULL,   33,    1,   30,   62,   509,   222,    0, 0x80, 0, 0 },
    /*  18 */ { 0x200667c5d940ac30ULL,    1,    1,   62,  126,   360,    88,    0, 0x80, 0, 0 },
    /*  19 */ { 0x200668059940ac60ULL,    1,    1,   30,   49,   465,    76,    0, 0x80, 0, 0 },
    /*  20 */ { 0x200668059940ac60ULL,   33,    1,   30,   54,   476,   125,    0, 0x80, 0, 0 },
    /*  21 */ { 0x20066825d940ac68ULL,    1,   30,   48,   97,   350,   226,    0, 0x80, 0, 0 },
    /*  22 */ { 0x200668459540ac80ULL,    1,    1,   30,   62,   252,   277,    0, 0x80, 0, 0 },
    /*  23 */ { 0x200668659d40ac86ULL,    1,    1,   49,   62,   488,    75,    0, 0x80, 0, 0 },
    /*  24 */ { 0x200668659d40ac86ULL,   52,    1,   51,   62,   517,   137,    0, 0x80, 0, 0 },
    /*  25 */ { 0x200668659d40ac86ULL,  105,    1,   22,   18,   549,   199,    0, 0x80, 0, 0 },
    /*  26 */ { 0x200668a59940aca6ULL,    1,    1,   34,   57,   435,    84,    0, 0x80, 0, 0 },
    /*  27 */ { 0x200668c59d40acaeULL,    1,    1,   44,   62,   299,   178,    0, 0x80, 0, 0 },
    /*  28 */ { 0x200668c59d40acaeULL,   47,    1,   44,   62,   303,   240,    0, 0x80, 0, 0 },
    /*  29 */ { 0x200668c59d40acaeULL,   93,    1,   34,   22,   317,   302,    0, 0x80, 0, 0 },
    /*  30 */ { 0x200669059940acceULL,    1,    1,   62,   62,   408,   225,    0, 0x80, 0, 0 },
    /*  31 */ { 0x20066925d940acd6ULL,    1,    1,   62,  126,   249,   197,    0, 0x80, 0, 0 },
    /*  32 */ { 0x20066945d940aceeULL,    1,    1,   62,  126,   567,   153,    0, 0x80, 0, 0 },
    /*  33 */ { 0x20066985d940ad1eULL,    1,    1,   62,  126,   303,   103,    0, 0x80, 0, 0 },
    /*  34 */ { 0x200669a59d40ad36ULL,    1,    1,   44,   62,   457,   159,    0, 0x80, 0, 0 },
    /*  35 */ { 0x200669a59d40ad36ULL,   47,    1,   44,   62,   468,   221,    0, 0x80, 0, 0 },
    /*  36 */ { 0x200669a59d40ad36ULL,   93,    1,   34,   18,   481,   283,    0, 0x80, 0, 0 },
    /*  37 */ { 0x200669e59940ad56ULL,    1,    1,   30,   62,   210,   108,    0, 0x80, 0, 0 },
    /*  38 */ { 0x200669e59940ad56ULL,   33,    1,   30,   62,   217,   170,    0, 0x80, 0, 0 },
    /*  39 */ { 0x20066a059940ad5eULL,    1,    1,   30,   62,   159,   240,    0, 0x80, 0, 0 },
    /*  40 */ { 0x20066a059940ad5eULL,   33,    1,   30,   62,   169,   302,    0, 0x80, 0, 0 },
    /*  41 */ { 0x20066a459940ad6eULL,    1,    1,   36,   62,   138,   133,    0, 0x80, 0, 0 },
    /*  42 */ { 0x20066a459940ad6eULL,   38,    1,   17,   23,   157,   195,    0, 0x80, 0, 0 },

    /* ---- memo title art, second revision -- the entries memo_second_tbl[] ---- */
    /*  43 */ { 0x200666e5d940abd8ULL,    1,    1,   62,  126,   403,   100,    0, 0x80, 0, 0 },
    /*  44 */ { 0x200667259940abf8ULL,    1,    1,   30,   62,   266,   103,    0, 0x80, 0, 0 },
    /*  45 */ { 0x200667259940abf8ULL,   33,    1,   30,   62,   266,   165,    0, 0x80, 0, 0 },
    /*  46 */ { 0x200667659d40ac10ULL,    1,    1,   46,   62,   190,   202,    0, 0x80, 0, 0 },
    /*  47 */ { 0x200667659d40ac10ULL,   49,    1,   45,   62,   202,   264,    0, 0x80, 0, 0 },
    /*  48 */ { 0x200667659d40ac10ULL,   96,    1,   31,   30,   223,   326,    0, 0x80, 0, 0 },
    /*  49 */ { 0x200667e5d940ac48ULL,    1,    1,   62,  126,   360,    88,    0, 0x80, 0, 0 },
    /*  50 */ { 0x200668859d40ac96ULL,    1,    1,   49,   62,   488,    75,    0, 0x80, 0, 0 },
    /*  51 */ { 0x200668859d40ac96ULL,   52,    1,   51,   62,   517,   137,    0, 0x80, 0, 0 },
    /*  52 */ { 0x200668859d40ac96ULL,  105,    1,   22,   18,   549,   199,    0, 0x80, 0, 0 },
    /*  53 */ { 0x200668e59d40acbeULL,    1,    1,   44,   62,   299,   178,    0, 0x80, 0, 0 },
    /*  54 */ { 0x200668e59d40acbeULL,   47,    1,   44,   62,   303,   240,    0, 0x80, 0, 0 },
    /*  55 */ { 0x200668e59d40acbeULL,   93,    1,   34,   22,   317,   302,    0, 0x80, 0, 0 },
    /*  56 */ { 0x20066965d940ad06ULL,    1,    1,   62,  126,   567,   153,    0, 0x80, 0, 0 },
    /*  57 */ { 0x200669c59d40ad46ULL,    1,    1,   44,   62,   457,   159,    0, 0x80, 0, 0 },
    /*  58 */ { 0x200669c59d40ad46ULL,   47,    1,   44,   62,   468,   221,    0, 0x80, 0, 0 },
    /*  59 */ { 0x200669c59d40ad46ULL,   93,    1,   34,   18,   481,   283,    0, 0x80, 0, 0 },
    /*  60 */ { 0x20066a259940ad66ULL,    1,    1,   30,   62,   159,   240,    0, 0x80, 0, 0 },
    /*  61 */ { 0x20066a259940ad66ULL,   33,    1,   30,   62,   169,   302,    0, 0x80, 0, 0 },

    /* ---- the unread marker: one 11x28 bracket and its mirror ---- */
    /*  62 */ { 0x20066a6665322d76ULL,    1,  481,   11,   28,    30,     0,    0, 0x80, 0, 1 },
    /*  63 */ { 0x20066a6665322d76ULL,    1,  481,   11,   28,   257,     0,    0, 0x80, 2, 1 },
};
