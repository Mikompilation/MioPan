// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/menu_glist_dat.c
//
// The ghost-list page's sprite table (menu_glist_dat.o, data 0x324bc8).
//
// One flat SPRT_DAT array covering the whole page.  The groups menu_soul.c
// walks are:
//
//     0..3     the page background, four tiles laid out by MenuSoulBgDisp()
//     4        the fifth tile, drawn with rot 270 about (x, y + w) -- so the
//              sprite's own width is what places it down the screen
//     5..6     the page title plate, out of the MENU_BG pak; [6] is [5]
//              mirrored (flip 2) and butted against it.  Byte-identical to
//              menu_memo_tex[6]/[7] -- every page shares the same plate
//     7        the words themselves, out of the ghost-list pak
//     8..17    the digits 0..9 of the completion-rate readout.  Only [8] is
//              ever named: DrawCmnNumberTex() takes the zero and steps the
//              u by hand, which is why the ten entries share x/y == 0
//     18..20   the selected row's frame -- left cap, body and the left cap
//              again mirrored.  MenuSoulCursorDisp() scales the body to 253
//              pixels with scw, so its authored w of 32 is only a unit
//     21..22   the up / down triangles either side of the selected row, both
//              tinted with menu_soul_disp.rgb
//     23       the ghost photo.  Its TEX0 is overwritten at draw time (see
//              MENU_SOUL_PHOTO_TEX0) because the picture is streamed into a
//              VRAM scratch page rather than living in the pak
//     24..25   the scroll arrows, the second one flipped
//     26..28   the scrollbar thumb: top cap, middle strip, bottom cap.  All
//              three are authored at alpha 0x66, unlike everything else here
//     29       the unread bracket, drawn on rows the player has not read
//     30       the "COMPLETE" plate
//     31..33   the 1st / 2nd / 3rd best-score badges
//     34..35   the two congratulation plates
//
// Entries out of the two picture paks (7, 8..17, 23, 30..35) carry bln 0;
// the rest carry bln 1, which is what lets the cursor and the arrows be
// tinted through r/g/b.
//
// Read straight out of the ROM's .data and diffed against it byte-for-byte.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "menu_glist_dat.h"

SPRT_DAT menu_glist_tex[36] =                               /* data 324bc8 */
{
    /* ---- the page background -- four tiles, then the fifth laid on its side ---- */
    /*  0 */ { 0x2006050625322bc0ULL,    1,    1,  510,  254,     9,    51,     0, 0x80, 0, 1 },
    /*  1 */ { 0x2006058621312dc0ULL,    1,    1,  254,  125,     9,   305,     0, 0x80, 0, 1 },
    /*  2 */ { 0x2006058621312dc0ULL,    1,  128,  254,  125,   263,   305,     0, 0x80, 0, 1 },
    /*  3 */ { 0x20060605dd30aec0ULL,    1,    1,  110,  125,   517,   305,     0, 0x80, 0, 1 },
    /*  4 */ { 0x20060685e1312f00ULL,    1,    1,  254,  108,   519,    51,     0, 0x80, 0, 1 },

    /* ---- the page title plate, one half mirrored (out of the MENU_BG pak) ---- */
    /*  5 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,     2,    19,   160, 0x80, 0, 1 },
    /*  6 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,    92,    19,   160, 0x80, 2, 1 },

    /* ---- the words "GHOST LIST" (out of the ghost-list pak) ---- */
    /*  7 */ { 0x20060705dd30af80ULL,    1,   98,  126,   29,    27,    22,     0, 0x80, 0, 0 },

    /* ---- the completion-rate digits 0..9, DrawCmnNumberTex()'s base ---- */
    /*  8 */ { 0x200607c59d40b008ULL,    1,    1,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /*  9 */ { 0x200607c59d40b008ULL,   26,    1,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 10 */ { 0x200607c59d40b008ULL,   51,    1,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 11 */ { 0x200607c59d40b008ULL,   76,    1,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 12 */ { 0x200607c59d40b008ULL,  101,    1,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 13 */ { 0x200607c59d40b008ULL,    1,   31,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 14 */ { 0x200607c59d40b008ULL,   26,   31,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 15 */ { 0x200607c59d40b008ULL,   51,   31,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 16 */ { 0x200607c59d40b008ULL,   76,   31,   23,   28,     0,     0,     0, 0x80, 0, 0 },
    /* 17 */ { 0x200607c59d40b008ULL,  101,   31,   23,   28,     0,     0,     0, 0x80, 0, 0 },

    /* ---- the selected row's frame: left cap, body (stretched to 253) and
     *      the left cap again mirrored ---- */
    /* 18 */ { 0x200607a59940b000ULL,    1,    1,   14,   37,    36,     0,     0, 0x80, 0, 1 },
    /* 19 */ { 0x200607a59940b000ULL,   16,    1,   32,   37,    50,     0,     0, 0x80, 0, 1 },
    /* 20 */ { 0x200607a59940b000ULL,    1,    1,   14,   37,   303,     0,     0, 0x80, 2, 1 },

    /* ---- the up / down triangles either side of the selected row ---- */
    /* 21 */ { 0x200607a59940b000ULL,   50,    1,   13,   10,   171,     0,     0, 0x80, 0, 1 },
    /* 22 */ { 0x200607a59940b000ULL,   50,   13,   13,   10,   171,     0,     0, 0x80, 2, 1 },

    /* ---- the ghost photo.  TEX0 is patched at draw time; this entry's own
     *      value is never used ---- */
    /* 23 */ { 0x20058805e1312bc0ULL,    0,    0,  192,  128,   374,   138,     0, 0x80, 0, 0 },

    /* ---- the scroll arrows, top and bottom (the second one flipped) ---- */
    /* 24 */ { 0x20060605dd30aec0ULL,  113,   30,   12,   15,    24,   117,     0, 0x80, 0, 1 },
    /* 25 */ { 0x20060605dd30aec0ULL,  113,   30,   12,   15,    24,   403,     0, 0x80, 1, 1 },

    /* ---- the scrollbar thumb: top cap, middle strip, bottom cap ---- */
    /* 26 */ { 0x20060605dd30aec0ULL,  113,   47,   12,    8,    24,   132,     0, 0x66, 0, 1 },
    /* 27 */ { 0x20060605dd30aec0ULL,  113,   57,   12,   60,    24,   140,     0, 0x66, 0, 1 },
    /* 28 */ { 0x20060605dd30aec0ULL,  113,  119,   12,    8,    24,   200,     0, 0x66, 0, 1 },

    /* ---- the unread bracket ---- */
    /* 29 */ { 0x20060605dd30aec0ULL,  113,    1,    8,   27,    43,     0,     0, 0x80, 0, 1 },

    /* ---- the "COMPLETE" plate ---- */
    /* 30 */ { 0x20060705dd30af80ULL,    1,   43,  117,   53,    89,    57,     0, 0x80, 0, 0 },

    /* ---- the 1st / 2nd / 3rd best-score badges ---- */
    /* 31 */ { 0x20060705dd30af80ULL,    1,    1,   40,   40,    32,   108,     0, 0x80, 0, 0 },
    /* 32 */ { 0x20060705dd30af80ULL,   43,    1,   40,   40,    32,   142,     0, 0x80, 0, 0 },
    /* 33 */ { 0x20060705dd30af80ULL,   85,    1,   40,   40,    32,   176,     0, 0x80, 0, 0 },

    /* ---- the two congratulation plates ---- */
    /* 34 */ { 0x20060785e1412fc0ULL,    1,    1,  254,   45,   103,   120,     0, 0x80, 0, 0 },
    /* 35 */ { 0x20060785e1412fc0ULL,    1,   48,  180,   45,   357,   120,     0, 0x80, 0, 0 },
};
