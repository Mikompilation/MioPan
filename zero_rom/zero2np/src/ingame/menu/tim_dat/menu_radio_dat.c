// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/menu_radio_dat.c
//
// The crystal-radio page's sprite table (menu_radio_dat.o, data 0x32a998) and
// the three per-language caption x tables (rodata 0x3beb40).
//
// One flat SPRT_DAT array covering the whole page.  The groups menu_radio.c
// walks are:
//
//     0        the full-screen backdrop, out of the MENU_RADIO pak's first
//              page.  Alone in its group -- MenuRadioBgDisp() draws it and
//              then loops over the next three
//     1..3     the right-hand column, three tiles out of the pak's second
//              page
//     4..5     two more strips off that same page, drawn with rot 270 about
//              (x, y + w), so the sprite's own width places them down the
//              screen.  Same idiom as menu_glist_tex[4]
//     6..7     the page's own title words, out of the MENU_RADIO pak
//     8..9     the shared title plate, out of the MENU_BG pak; [9] is [8]
//              mirrored (flip 2) and butted against it.  Byte-identical to
//              menu_glist_tex[5]/[6] -- every page shares this plate
//     10..11   the scroll arrows above and below the rail, [11] flipped.
//              Both tinted with menu_radio_disp.rgb
//     12..14   the scrollbar rail: top cap, bottom cap (flip 1) and the
//              middle strip [14], which MenuRadioScrollFrameDisp() stretches
//              to 126 px with sch.  All three drawn additively (alphar 0x48)
//     15..17   the rail's thumb: top cap, middle strip, bottom cap (flip 1).
//              Authored at alpha 0x66
//     18..20   the UNselected row's frame -- left cap, the body [19] (which
//              MenuRadioNonSelFrameDisp() rotates 90 and stretches to 164 px)
//              and the left cap again mirrored.  Authored at alpha 0x26
//     21..23   the SELECTED row's frame, same three-piece shape but stretched
//              to 138 px through scw rather than rotated, and authored at
//              alpha 0x66 -- which is what makes the selected row the bright
//              one
//     24..25   the "not listened to yet" bracket, a pair placed 255 px apart
//     26       the crystal picture.  Out of the cross-fade pak, not this one
//     27..30   the crystal's flare: the halo [27] plus three word plates
//              underneath it.  Also out of the cross-fade pak
//     31       the PLAY button caption plate; x comes from play_cap_tbl[]
//     32..33   the two STOP button caption plates; x from stop_cap_tbl_1[]
//              and stop_cap_tbl_2[]
//     34..36   UNREFERENCED.  A three-piece frame in the same shape as
//              18..20 / 21..23 (left cap, middle, mirrored left cap) at
//              alpha 0x4c, positioned at 0,0 so it was meant to be placed at
//              draw time.  A jal-and-offset scan over the whole object finds
//              no reference above index 33, so nothing in this build draws
//              them
//
// Entries 26..30 carry a TEX0 from the RADIO_CRYSTAL_nn paks and are drawn
// with whatever MenuCrossFade's slot uploaded, so their TEX0 here is only
// the authoring-time one; the record's u/v/w/h are what matter.
//
// Read straight out of the ROM's .data and .rodata and diffed against it
// byte-for-byte.  The 4 bytes of padding after each caption table are the
// linker's 8-byte alignment, not a sixth entry: 20 + 4 + 20 + 4 + 20 is the
// section's 0x44 exactly.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "menu_radio_dat.h"

SPRT_DAT menu_radio_tex[37] =                               /* data 32a998 */
{
    /* ---- the backdrop: one full-screen plate ... ---- */
    /*  0 */ { 0x20061c8665322be0ULL,    1,    1,  510,  448,     0,     0,     0, 0x80, 0, 1 },

    /* ---- ... then the right-hand column, three tiles ---- */
    /*  1 */ { 0x20061d0621312fe0ULL,    1,    1,  116,  254,   510,   194,     0, 0x80, 0, 1 },
    /*  2 */ { 0x20061d0621312fe0ULL,  119,    1,   14,  254,   626,   194,     0, 0x80, 0, 1 },
    /*  3 */ { 0x20061d0621312fe0ULL,  135,    1,  116,  194,   510,     0,     0, 0x80, 0, 1 },

    /* ---- two strips laid on their side (rot 270 about x, y + w) ---- */
    /*  4 */ { 0x20061d0621312fe0ULL,  135,  197,  120,   14,   626,    74,     0, 0x80, 0, 1 },
    /*  5 */ { 0x20061d0621312fe0ULL,  135,  213,   74,   14,   626,     0,     0, 0x80, 0, 1 },

    /* ---- the page's own title words (out of the MENU_RADIO pak) ---- */
    /*  6 */ { 0x20061c8665322be0ULL,    1,  451,   62,   29,    44,    21,     0, 0x80, 0, 0 },
    /*  7 */ { 0x20061c8665322be0ULL,   63,  451,   22,   29,   106,    20,     0, 0x80, 0, 0 },

    /* ---- the shared title plate, one half mirrored (out of the MENU_BG pak) ---- */
    /*  8 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,     2,    19,   160, 0x80, 0, 1 },
    /*  9 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,    92,    19,   160, 0x80, 2, 1 },

    /* ---- the scroll arrows, tinted with the shared cursor pulse ---- */
    /* 10 */ { 0x20061c059d30abc0ULL,  103,   13,   18,   19,    20,    75,     0, 0x80, 0, 1 },
    /* 11 */ { 0x20061c059d30abc0ULL,  103,   13,   18,   19,    20,   280,     0, 0x80, 1, 1 },

    /* ---- the scrollbar rail: top cap, bottom cap, stretched middle ---- */
    /* 12 */ { 0x20061c059d30abc0ULL,    1,    1,   22,   58,    19,    66,     0, 0x80, 0, 1 },
    /* 13 */ { 0x20061c059d30abc0ULL,    1,    1,   22,   58,    19,   250,     0, 0x80, 1, 1 },
    /* 14 */ { 0x20061c059d30abc0ULL,  103,    1,   22,   10,    19,   124,     0, 0x80, 0, 1 },

    /* ---- the rail's thumb ---- */
    /* 15 */ { 0x20061c059d30abc0ULL,   75,   46,   14,   14,    22,    93,     0, 0x66, 0, 1 },
    /* 16 */ { 0x20061c059d30abc0ULL,   91,   46,   14,   14,    22,   107,     0, 0x66, 0, 1 },
    /* 17 */ { 0x20061c059d30abc0ULL,   75,   46,   14,   14,    22,   121,     0, 0x66, 1, 1 },

    /* ---- an UNselected row's frame (dim) ---- */
    /* 18 */ { 0x20061c059d30abc0ULL,   25,   34,   48,   28,    47,    83,     0, 0x26, 0, 1 },
    /* 19 */ { 0x20061c059d30abc0ULL,   75,   34,   28,   10,    94,    83,     0, 0x26, 0, 1 },
    /* 20 */ { 0x20061c059d30abc0ULL,   25,   34,   48,   28,   259,    83,     0, 0x26, 2, 1 },

    /* ---- the SELECTED row's frame (bright) ---- */
    /* 21 */ { 0x20061c059d30abc0ULL,   25,    1,   64,   30,    44,    82,     0, 0x66, 0, 1 },
    /* 22 */ { 0x20061c059d30abc0ULL,   91,    1,   10,   30,   108,    82,     0, 0x66, 0, 1 },
    /* 23 */ { 0x20061c059d30abc0ULL,   25,    1,   64,   30,   246,    82,     0, 0x66, 2, 1 },

    /* ---- the "not listened to yet" bracket, placed at draw time ---- */
    /* 24 */ { 0x20061c059d30abc0ULL,  107,   34,   11,   28,     0,     0,     0, 0x80, 0, 1 },
    /* 25 */ { 0x20061c059d30abc0ULL,  107,   34,   11,   28,     0,     0,     0, 0x80, 2, 1 },

    /* ---- the crystal picture and its flare (out of the cross-fade pak) ---- */
    /* 26 */ { 0x20059005dd30abc0ULL,    1,    1,  103,  100,   401,   105,     0, 0x80, 0, 1 },
    /* 27 */ { 0x20059085e1312c00ULL,    1,    1,  191,  126,   356,    69,     0, 0x80, 0, 1 },
    /* 28 */ { 0x20059085e1312c00ULL,  194,    1,   61,   38,   362,   195,     0, 0x80, 0, 1 },
    /* 29 */ { 0x20059085e1312c00ULL,  194,   41,   61,   38,   423,   195,     0, 0x80, 0, 1 },
    /* 30 */ { 0x20059085e1312c00ULL,  194,   81,   57,   38,   484,   195,     0, 0x80, 0, 1 },

    /* ---- the button captions; x comes from the three tables below ---- */
    /* 31 */ { 0x20061c8665322be0ULL,   59,  482,   93,   27,   352,    24,     0, 0x80, 0, 1 },
    /* 32 */ { 0x20061c8665322be0ULL,  154,  482,   58,   27,   325,    24,     0, 0x80, 0, 1 },
    /* 33 */ { 0x20061c8665322be0ULL,  154,  482,   58,   27,   413,    24,     0, 0x80, 0, 1 },

    /* ---- unreferenced: a fourth three-piece frame, never drawn ---- */
    /* 34 */ { 0x20061c8665322be0ULL,    1,  482,   30,   29,     0,     0,     0, 0x4c, 0, 1 },
    /* 35 */ { 0x20061c8665322be0ULL,   33,  482,   24,   29,     0,     0,     0, 0x4c, 0, 1 },
    /* 36 */ { 0x20061c8665322be0ULL,    1,  482,   30,   29,     0,     0,     0, 0x4c, 2, 1 },
};

/* Where each caption plate's left edge sits, per language.  The words are
 * different lengths, so the plate has to move rather than the text inside it
 * being re-laid. */
const int play_cap_tbl[5] =                                 /* rdata 3beb40 */
{
    328, 285, 297, 315, 239
};

const int stop_cap_tbl_1[5] =                               /* rdata 3beb58 */
{
    498, 494, 480, 481, 494
};

const int stop_cap_tbl_2[5] =                               /* rdata 3beb70 */
{
    575, 572, 566, 566, 572
};
