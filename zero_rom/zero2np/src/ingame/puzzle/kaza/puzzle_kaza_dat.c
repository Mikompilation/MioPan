// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/kaza/puzzle_kaza_dat.c
//
// The two kazaguruma (pinwheel) boards' answer tables and sprite table.
//
// A data-only translation unit shared by kaza_pzl.o and kaza2_pzl.o.  Each
// panel gets a four-entry colour ring -- the colours of its four wings, read
// clockwise -- and turning the panel rotates which entry a given wing shows.
// The puzzle is solved when four pairs of touching wings match, which
// kaza_pzl.c checks by indexing these rings.
//
// The rings are reference_fixed_array<int,4>, i.e. bounds-checked views onto
// the plain int[4] tables below, so they are built by a static constructor
// rather than laid out in .data -- which is why puzzle_kaza_dat.o has both a
// .ctors entry and 0x1d8 of .text despite exporting no function.
//
// Extracted from .data 0x33d7f0 / .rodata 0x3c45b0 and verified byte-for-byte
// against the ROM.  The tex0 values are GS TEX0 register words naming the
// pak's texture pages and are reproduced as stored rather than decoded.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), puzzle_kaza_dat.o.

#include "puzzle_kaza_dat.h"

/* Board 1.  The two boards differ in one ring: right_down. */
static int kaza_panel_center_dat[4]     = { 1, 3, 0, 2 };   /* rdata 3c45b0 */
static int kaza_panel_left_up_dat[4]    = { 0, 1, 0, 1 };   /* rdata 3c45c0 */
static int kaza_panel_right_up_dat[4]   = { 0, 3, 3, 3 };   /* rdata 3c45d0 */
static int kaza_panel_right_down_dat[4] = { 0, 1, 0, 1 };   /* rdata 3c45e0 */
static int kaza_panel_left_down_dat[4]  = { 2, 0, 2, 2 };   /* rdata 3c45f0 */

/* Board 2. */
static int kaza2_panel_center_dat[4]     = { 1, 3, 0, 2 };  /* rdata 3c4600 */
static int kaza2_panel_left_up_dat[4]    = { 0, 1, 0, 1 };  /* rdata 3c4610 */
static int kaza2_panel_right_up_dat[4]   = { 0, 3, 3, 3 };  /* rdata 3c4620 */
static int kaza2_panel_right_down_dat[4] = { 2, 0, 2, 2 };  /* rdata 3c4630 */
static int kaza2_panel_left_down_dat[4]  = { 2, 0, 2, 2 };  /* rdata 3c4640 */

reference_fixed_array<int, 4> kaza_panel_center(kaza_panel_center_dat);          /* sdata 3f3b20 */
reference_fixed_array<int, 4> kaza_panel_left_up(kaza_panel_left_up_dat);        /* sdata 3f3b28 */
reference_fixed_array<int, 4> kaza_panel_right_up(kaza_panel_right_up_dat);      /* sdata 3f3b30 */
reference_fixed_array<int, 4> kaza_panel_right_down(kaza_panel_right_down_dat);  /* sdata 3f3b38 */
reference_fixed_array<int, 4> kaza_panel_left_down(kaza_panel_left_down_dat);    /* sdata 3f3b40 */

reference_fixed_array<int, 4> kaza2_panel_center(kaza2_panel_center_dat);         /* sdata 3f3b48 */
reference_fixed_array<int, 4> kaza2_panel_left_up(kaza2_panel_left_up_dat);       /* sdata 3f3b50 */
reference_fixed_array<int, 4> kaza2_panel_right_up(kaza2_panel_right_up_dat);     /* sdata 3f3b58 */
reference_fixed_array<int, 4> kaza2_panel_right_down(kaza2_panel_right_down_dat); /* sdata 3f3b60 */
reference_fixed_array<int, 4> kaza2_panel_left_down(kaza2_panel_left_down_dat);   /* sdata 3f3b68 */

SPRT_DAT puzzle_kaza_tex[PUZZLE_KAZA_TEX_NUM] =             /* data 33d7f0 */
{
    /*         tex0                    u    v    w    h     x     y  pri  alp fl bl */
    /*  0 */ { 0x2007780665322bc0ULL,   1,   1, 510, 448,    0,    0,   0, 128, 0, 1 },
    /*  1 */ { 0x2007788665322fc0ULL,   1,   1, 130, 448,  510,    0,   0, 128, 0, 1 },
    /*  2 */ { 0x2007788665322fc0ULL, 133,   1, 131, 131,  166,   45,   0, 128, 0, 1 },
    /*  3 */ { 0x2007788665322fc0ULL, 133,   1, 131, 131,  165,  222,   0, 128, 0, 1 },
    /*  4 */ { 0x2007788665322fc0ULL, 133,   1, 131, 131,  341,   45,   0, 128, 0, 1 },
    /*  5 */ { 0x2007788665322fc0ULL, 133,   1, 131, 131,  341,  222,   0, 128, 0, 1 },
    /*  6 */ { 0x2007788665322fc0ULL, 133,   1, 131, 131,  253,  134,   0, 128, 0, 1 },
    /*  7 */ { 0x2007788665322fc0ULL, 133, 134, 131, 131,  166,   45,   0, 128, 0, 1 },
    /*  8 */ { 0x2007788665322fc0ULL, 133, 134, 131, 131,  165,  222,   0, 128, 0, 1 },
    /*  9 */ { 0x2007788665322fc0ULL, 133, 134, 131, 131,  341,   45,   0, 128, 0, 1 },
    /* 10 */ { 0x2007788665322fc0ULL, 133, 134, 131, 131,  341,  222,   0, 128, 0, 1 },
    /* 11 */ { 0x2007788665322fc0ULL, 133, 134, 131, 131,  253,  134,   0, 128, 0, 1 },
    /* 12 */ { 0x2007788665322fc0ULL, 266, 153, 131, 131,  166,   45,   0, 128, 0, 1 },
    /* 13 */ { 0x2007788665322fc0ULL, 266, 153, 131, 131,  165,  222,   0, 128, 0, 1 },
    /* 14 */ { 0x2007788665322fc0ULL, 266, 153, 131, 131,  341,   45,   0, 128, 0, 1 },
    /* 15 */ { 0x2007788665322fc0ULL, 266, 153, 131, 131,  341,  222,   0, 128, 0, 1 },
    /* 16 */ { 0x2007788665322fc0ULL, 266, 153, 131, 131,  253,  134,   0, 128, 0, 1 },
    /* 17 */ { 0x2007788665322fc0ULL, 266,   1, 149, 150,  155,   31,   0, 128, 0, 1 },
    /* 18 */ { 0x2007788665322fc0ULL, 266,   1, 149, 150,  154,  208,   0, 128, 0, 1 },
    /* 19 */ { 0x2007788665322fc0ULL, 266,   1, 149, 150,  331,   31,   0, 128, 0, 1 },
    /* 20 */ { 0x2007788665322fc0ULL, 266,   1, 149, 150,  331,  208,   0, 128, 0, 1 },
    /* 21 */ { 0x2007788665322fc0ULL, 266,   1, 149, 150,  242,  120,   0, 128, 0, 1 },
    /* 22 */ { 0x20077a86213135c0ULL,   7,   2, 118, 118,  259,  135,   0, 128, 0, 1 },
    /* 23 */ { 0x20077b06213136c0ULL,   7,   2, 118, 118,  347,   46,   0, 128, 0, 1 },
    /* 24 */ { 0x20077b86213137c0ULL,   7,   2, 118, 118,  347,  223,   0, 128, 0, 1 },
    /* 25 */ { 0x20077c06213138c0ULL,   7,   2, 118, 118,  172,   46,   0, 128, 0, 1 },
    /* 26 */ { 0x20077c86213139c0ULL,   7,   2, 118, 118,  171,  223,   0, 128, 0, 1 },
    /* 27 */ { 0x20077906213133c0ULL,   1,   1, 148,  81,   38,  329,   0, 128, 0, 1 },
    /* 28 */ { 0x20077906213133c0ULL,   1,  83, 186, 122,   18,  309,   0,  55, 0, 1 },
    /* 29 */ { 0x20077985e13134c0ULL,   1,   1, 112,  36,  416,  377,   0, 128, 0, 1 },
    /* 30 */ { 0x20077985e13134c0ULL, 137,   1,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 31 */ { 0x20077985e13134c0ULL, 161,   1,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 32 */ { 0x20077985e13134c0ULL, 185,   1,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 33 */ { 0x20077985e13134c0ULL, 209,   1,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 34 */ { 0x20077985e13134c0ULL, 233,   1,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 35 */ { 0x20077985e13134c0ULL, 209,  35,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 36 */ { 0x20077985e13134c0ULL, 233,  35,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 37 */ { 0x20077985e13134c0ULL, 209,  69,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 38 */ { 0x20077985e13134c0ULL, 233,  69,  22,  32,  543,  377,   0, 128, 0, 1 },
    /* 39 */ { 0x20077985e13134c0ULL,   1,  39, 187,  77,  397,  357,   0,  76, 0, 1 },
    /* 40 */ { 0x20077d0621313ac0ULL,   0,   0, 256, 256,    0,    0,   0, 128, 0, 1 },
    /* 41 */ { 0x20077985e13134c0ULL,   1,   1, 112,  36,  491,  377,   0, 128, 0, 1 },
    /* 42 */ { 0x20077985e13134c0ULL, 137,   1,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 43 */ { 0x20077985e13134c0ULL, 161,   1,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 44 */ { 0x20077985e13134c0ULL, 185,   1,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 45 */ { 0x20077985e13134c0ULL, 209,   1,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 46 */ { 0x20077985e13134c0ULL, 233,   1,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 47 */ { 0x20077985e13134c0ULL, 209,  35,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 48 */ { 0x20077985e13134c0ULL, 233,  35,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 49 */ { 0x20077985e13134c0ULL, 209,  69,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 50 */ { 0x20077985e13134c0ULL, 233,  69,  22,  32,  454,  377,   0, 128, 0, 1 },
    /* 51 */ { 0x2007780665322bc0ULL,   1,   1, 510, 448,    0,    0,   0, 128, 0, 1 },
    /* 52 */ { 0x2007788665322fc0ULL,   1,   1, 130, 448,  510,    0,   0, 128, 0, 1 },
    /* 53 */ { 0x20077a86213135c0ULL,   7,   2, 118, 118,  259,  135,   0, 128, 0, 1 },
    /* 54 */ { 0x20077b06213136c0ULL,   7,   2, 118, 118,  347,   46,   0, 128, 0, 1 },
    /* 55 */ { 0x20077c86213139c0ULL,   7,   2, 118, 118,  347,  223,   0, 128, 0, 1 },
    /* 56 */ { 0x20077c06213138c0ULL,   7,   2, 118, 118,  172,   46,   0, 128, 0, 1 },
    /* 57 */ { 0x20077c86213139c0ULL,   7,   2, 118, 118,  171,  223,   0, 128, 0, 1 },
};
