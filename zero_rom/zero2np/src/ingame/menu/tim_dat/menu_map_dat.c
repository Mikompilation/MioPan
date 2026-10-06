// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/menu_map_dat.c
//
// The map page's sprite table (menu_map_dat.o, data 0x325d98).
//
// One flat SPRT_DAT array covering the whole page.  The groups menu_map.c
// walks are:
//
//     0..1     the page title, out of the map page's own language pak
//     2..3     the same title out of the shared menu background pak; the two
//              pairs are drawn one on top of the other by MenuMapTitleDisp()
//     4..23    the window frame -- MenuMapWindowDisp() draws all twenty
//     24       the room snapshot (105x62 at 499,66).  MenuMapSnapShotDisp()
//              lays its own black SQAR_DAT under it at exactly this rect.
//     25       the player marker, drawn rotated to the player's facing
//     26..27   two DEGENERATE entries -- w and h are both zero, so they cover
//              no pixels.  Nothing in menu_map.o reads them.
//     28       the save-point mark (alpha 0x66)
//     29..35   the translucent base panels behind the map sheet (alpha 0x6c)
//     36..45   the door marks.  A door type picks one of 39/38/41/43/45; the
//              ghost-sealed variant of the first four is 37/36/40/42, and 44
//              is the seal mark itself.
//     46..49   the four scroll arrows around the centre, drawn up / rot 270 /
//              rot 90 / flipped by MenuMapCenterDisp()
//     50..53   the same four arrows one size up (21x18 against 17x14).
//              Unused by menu_map.o.
//     54       a full 512x512 plate.  Unused by menu_map.o.
//     55..56   the two additive glows MenuMapBaseDisp() finishes with
//
// Read straight out of the ROM's .data and diffed against it byte-for-byte.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "menu_map_dat.h"

SPRT_DAT menu_map_tex[57] =                                 /* data 325d98 */
{
    /*   0 */ { 0x2005ad459940ac40ULL,    1,    1,   62,   30,    44,    20,    0, 0x80, 0, 0 },
    /*   1 */ { 0x2005ad459940ac40ULL,    1,   33,   20,   30,   106,    20,    0, 0x80, 0, 0 },
    /*   2 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,     2,    19,  160, 0x80, 0, 1 },
    /*   3 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,    92,    19,  160, 0x80, 2, 1 },
    /*   4 */ { 0x2005ad6621312c48ULL,    1,    1,  254,   61,    21,    53,    0, 0x80, 0, 1 },
    /*   5 */ { 0x2005ad6621312c48ULL,    1,   64,  241,   56,   275,    58,    0, 0x80, 0, 1 },
    /*   6 */ { 0x2005ad6621312c48ULL,  242,   64,   13,   20,   516,    58,    0, 0x80, 0, 1 },
    /*   7 */ { 0x2005ad6621312c48ULL,    1,  122,   90,   89,   529,    53,    0, 0x80, 0, 1 },
    /*   8 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   87,    20,   114,    0, 0x80, 0, 1 },
    /*   9 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   28,    20,   201,    0, 0x80, 0, 1 },
    /*  10 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   87,    20,   287,    0, 0x80, 0, 1 },
    /*  11 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   32,    20,   374,    0, 0x80, 0, 1 },
    /*  12 */ { 0x2005ad6621312c48ULL,   93,  122,   40,   58,    27,   229,    0, 0x80, 0, 1 },
    /*  13 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   87,   595,   142,    0, 0x80, 0, 1 },
    /*  14 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   87,   595,   287,    0, 0x80, 0, 1 },
    /*  15 */ { 0x2005ad6621312c48ULL,  237,  121,   18,   32,   595,   374,    0, 0x80, 0, 1 },
    /*  16 */ { 0x2005ad6621312c48ULL,  135,  122,   40,   58,   573,   229,    0, 0x80, 0, 1 },
    /*  17 */ { 0x2005ad6621312c48ULL,    1,  213,   66,   20,   287,   114,    0, 0x80, 0, 1 },
    /*  18 */ { 0x2005ad6621312c48ULL,  177,  148,   47,   28,   482,   114,    0, 0x80, 0, 1 },
    /*  19 */ { 0x2005ad6621312c48ULL,  177,  122,   24,   24,    21,   406,    0, 0x80, 0, 1 },
    /*  20 */ { 0x2005ad6621312c48ULL,  203,  122,   24,   24,   595,   406,    0, 0x80, 0, 1 },
    /*  21 */ { 0x2005ad6621312c48ULL,    1,  235,  246,   19,    45,   406,    0, 0x80, 0, 1 },
    /*  22 */ { 0x2005ad6621312c48ULL,    1,  235,  246,   19,   349,   406,    0, 0x80, 0, 1 },
    /*  23 */ { 0x2005ad6621312c48ULL,   93,  182,   58,   42,   291,   383,    0, 0x80, 0, 1 },
    /*  24 */ { 0x20057c059d30abc0ULL,    1,    1,  105,   62,   499,    66,    0, 0x80, 0, 1 },
    /*  25 */ { 0x2005ad6621312c48ULL,  186,  179,   50,   48,     0,     0,    0, 0x80, 0, 1 },
    /*  26 */ { 0x2005ad6621312c48ULL,   93,  229,    0,    0,   240,   255,    0, 0x80, 0, 1 },
    /*  27 */ { 0x2005ad6621312c48ULL,   93,  229,    0,    0,   319,   176,    0, 0x80, 0, 1 },
    /*  28 */ { 0x2005ad6621312c48ULL,   69,  213,   22,   19,     0,     0,    0, 0x66, 0, 1 },
    /*  29 */ { 0x2005ade5dd40ad48ULL,    1,    1,  124,  124,    33,    99,    0, 0x6c, 0, 1 },
    /*  30 */ { 0x2005ade5dd40ad48ULL,    1,    1,   92,  124,   405,    99,    0, 0x6c, 0, 1 },
    /*  31 */ { 0x2005ade5dd40ad48ULL,    1,    1,  111,   94,   497,   129,    0, 0x6c, 0, 1 },
    /*  32 */ { 0x2005ade5dd40ad48ULL,    1,    1,  124,  124,    33,   223,    0, 0x6c, 0, 1 },
    /*  33 */ { 0x2005ade5dd40ad48ULL,    1,    1,   79,  124,   529,   223,    0, 0x6c, 0, 1 },
    /*  34 */ { 0x2005ade5dd40ad48ULL,    1,    1,  124,   71,    33,   347,    0, 0x6c, 0, 1 },
    /*  35 */ { 0x2005ade5dd40ad48ULL,    1,    1,   79,   71,   529,   347,    0, 0x6c, 0, 1 },
    /*  36 */ { 0x2005ad6621312c48ULL,  226,  163,    5,   13,     0,     0,    0, 0x80, 0, 1 },
    /*  37 */ { 0x2005ad6621312c48ULL,  171,  217,   13,    5,     0,     0,    0, 0x80, 0, 1 },
    /*  38 */ { 0x2005ad6621312c48ULL,  226,  148,    5,   13,     0,     0,    0, 0x80, 0, 1 },
    /*  39 */ { 0x2005ad6621312c48ULL,  237,  222,   13,    5,     0,     0,    0, 0x80, 0, 1 },
    /*  40 */ { 0x2005ad6621312c48ULL,  154,  215,   11,   11,     0,     0,    0, 0x80, 2, 1 },
    /*  41 */ { 0x2005ad6621312c48ULL,  154,  202,   11,   11,     0,     0,    0, 0x80, 2, 1 },
    /*  42 */ { 0x2005ad6621312c48ULL,  154,  215,   11,   11,     0,     0,    0, 0x80, 0, 1 },
    /*  43 */ { 0x2005ad6621312c48ULL,  154,  202,   11,   11,     0,     0,    0, 0x80, 0, 1 },
    /*  44 */ { 0x2005ad6621312c48ULL,  237,  210,   11,   10,     0,     0,    0, 0x80, 0, 1 },
    /*  45 */ { 0x2005ad6621312c48ULL,  153,  182,    8,   13,    68,   281,    0, 0x80, 0, 1 },
    /*  46 */ { 0x2005ad6621312c48ULL,  167,  202,   17,   14,   312,   224,    0, 0x80, 0, 1 },
    /*  47 */ { 0x2005ad6621312c48ULL,  167,  202,   17,   14,   286,   249,    0, 0x80, 0, 1 },
    /*  48 */ { 0x2005ad6621312c48ULL,  167,  202,   17,   14,   341,   249,    0, 0x80, 0, 1 },
    /*  49 */ { 0x2005ad6621312c48ULL,  167,  202,   17,   14,   312,   277,    0, 0x80, 1, 1 },
    /*  50 */ { 0x2005ad6621312c48ULL,  163,  182,   21,   18,   310,   222,    0, 0x80, 0, 1 },
    /*  51 */ { 0x2005ad6621312c48ULL,  163,  182,   21,   18,   284,   247,    0, 0x80, 0, 1 },
    /*  52 */ { 0x2005ad6621312c48ULL,  163,  182,   21,   18,   339,   247,    0, 0x80, 0, 1 },
    /*  53 */ { 0x2005ad6621312c48ULL,  163,  182,   21,   18,   310,   275,    0, 0x80, 1, 1 },
    /*  54 */ { 0x2005b80665422bc0ULL,    0,    0,  512,  512,     0,     0,    0, 0x80, 0, 0 },
    /*  55 */ { 0x2005ad05e1412bc0ULL,    1,    1,  242,  126,    34,   102,    0, 0x0c, 0, 1 },
    /*  56 */ { 0x2005ad25e1412c00ULL,    1,    1,  254,  126,   349,   289,    0, 0x0c, 0, 1 },
};
