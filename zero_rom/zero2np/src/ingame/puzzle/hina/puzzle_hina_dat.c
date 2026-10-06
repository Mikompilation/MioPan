// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/hina/puzzle_hina_dat.c
//
// The hina-dan puzzle's board layout and sprite table.
//
// A data-only translation unit.  The board is a 4x4 grid dealt from
// hina_first_pos[][]: nine numbered dolls, one empty slot (-2) that they slide
// into, and four corner holes (-1) where the tiered shelf has no cell.  The
// four float tables give each cell's screen position, so the grid is only a
// logical index -- the shelf itself is drawn wider towards the bottom, which
// is also why hina_pzl.c scales each row by 0.93 / 0.96 / 1.0 / 1.03.
//
// A doll number is its own puzzle_hina_tex[] index, and its selected-glow
// sprite is that plus nine.  Dolls 9 and 10 -- the pair listed in
// no_move_hina[] -- are decoration: the cursor will not stop on them.
//
// Extracted from .data 0x33d110 / .sdata 0x3f3b00 and verified byte-for-byte
// against the ROM.  The tex0 values are GS TEX0 register words naming the
// pak's texture pages and are reproduced as stored rather than decoded.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), puzzle_hina_dat.o.

#include "puzzle_hina_dat.h"

/* Row 0 is the top shelf and has only two cells; the puzzle is solved when
 * both of them hold doll 8.  Doll numbers repeat -- 2, 3, 5 and 8 each appear
 * twice -- so the pair that matters has to be told apart by position. */
int hina_first_pos[4][4] =                                  /* data 33d110 */
{
    { -1,  5,  7, -1 },
    {  9,  2,  3,  6 },
    {  4,  5, -2, 10 },
    {  8,  3,  8,  2 },
};

int no_move_hina[2] = { 9, 10 };                            /* sdata 3f3b00 */

float hina_pos_x[4][4] =                                    /* data 33d150 */
{
    {  -1.0f, 188.0f, 340.0f,  -1.0f },
    {  52.0f, 189.0f, 333.0f, 464.0f },
    {  46.0f, 192.0f, 341.0f, 487.0f },
    {  44.0f, 197.0f, 348.0f, 492.0f },
};

float hina_pos_y[4][4] =                                    /* data 33d190 */
{
    {  -1.0f,  48.0f,  48.0f,  -1.0f },
    { 138.0f, 140.0f, 139.0f, 140.0f },
    { 219.0f, 221.0f, 221.0f, 221.0f },
    { 303.0f, 305.0f, 305.0f, 304.0f },
};

/* The glow sits up and left of the doll it marks.  Four cells carry -1 here
 * that hold a real doll in hina_pos_*: [1][0] and [2][3] are the two fixed
 * dolls, which are never selected, so they need no glow position. */
float hina_flea_pos_x[4][4] =                               /* data 33d1d0 */
{
    {  -1.0f, 170.0f, 322.0f,  -1.0f },
    {  -1.0f, 171.0f, 316.0f, 446.0f },
    {  28.0f, 174.0f, 323.0f,  -1.0f },
    {  26.0f, 179.0f, 330.0f, 474.0f },
};

float hina_flea_pos_y[4][4] =                               /* data 33d210 */
{
    {  -1.0f,  30.0f,  30.0f,  -1.0f },
    {  -1.0f, 122.0f, 121.0f, 122.0f },
    { 201.0f, 203.0f, 203.0f,  -1.0f },
    { 285.0f, 287.0f, 287.0f, 286.0f },
};

SPRT_DAT puzzle_hina_tex[PUZZLE_HINA_TEX_NUM] =             /* data 33d250 */
{
    /*         tex0                    u    v    w    h     x     y  pri  alp fl bl */
    /*  0 */ { 0x2007794665322d40ULL,   1,   1, 510, 448,    0,    0,   0, 128, 0, 1 },
    /*  1 */ { 0x200779c665323140ULL,   1,   1, 130, 448,  510,    0,   0, 128, 0, 1 },
    /*  2 */ { 0x20077805dd30abc0ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  3 */ { 0x200778a5dd30ac80ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  4 */ { 0x20077a45dd30b540ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  5 */ { 0x20077ce5dd30b740ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  6 */ { 0x20077d85dd30b800ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  7 */ { 0x20077e25dd30b8c0ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  8 */ { 0x20077ec5dd30b980ULL,   1,   1, 113, 107,    0,    0,   0, 128, 0, 0 },
    /*  9 */ { 0x20077f65dd30ba40ULL,   1,   1, 113, 107,   52,  138,   0, 128, 0, 0 },
    /* 10 */ { 0x20077fe5dd30ba80ULL,   1,   1, 113, 107,  487,  221,   0, 128, 0, 0 },
    /* 11 */ { 0x2007788621412c00ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 12 */ { 0x2007792621412cc0ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 13 */ { 0x20077ac621413580ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 14 */ { 0x20077d6621413780ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 15 */ { 0x20077e0621413840ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 16 */ { 0x20077ea621413900ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 17 */ { 0x20077f46214139c0ULL,   1,   1, 148, 144,    0,    0,   0, 128, 0, 0 },
    /* 18 */ { 0x20078185dd30bb80ULL,   1,   1, 126,  36,  445,   29,   0, 128, 0, 1 },
    /* 19 */ { 0x20078185dd30bb80ULL,   1,  39,  69,  36,  571,   29,   0, 128, 0, 1 },
    /* 20 */ { 0x200780659d30bac0ULL,   1,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 21 */ { 0x200780659d30bac0ULL,  19,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 22 */ { 0x200780659d30bac0ULL,  38,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 23 */ { 0x200780659d30bac0ULL,  57,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 24 */ { 0x200780659d30bac0ULL,  77,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 25 */ { 0x200780659d30bac0ULL,   1,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 26 */ { 0x200780659d30bac0ULL,  20,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 27 */ { 0x200780659d30bac0ULL,  39,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 28 */ { 0x200780659d30bac0ULL,  57,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 29 */ { 0x200780659d30bac0ULL,  77,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 30 */ { 0x200780e59d30bae0ULL,   1,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 31 */ { 0x200780e59d30bae0ULL,  19,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 32 */ { 0x200780e59d30bae0ULL,  38,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 33 */ { 0x200780e59d30bae0ULL,  57,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 34 */ { 0x200780e59d30bae0ULL,  77,   1,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 35 */ { 0x200780e59d30bae0ULL,   1,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 36 */ { 0x200780e59d30bae0ULL,  20,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 37 */ { 0x200780e59d30bae0ULL,  39,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 38 */ { 0x200780e59d30bae0ULL,  57,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 39 */ { 0x200780e59d30bae0ULL,  77,  30,  17,  27,    0,    0,   0, 128, 0, 0 },
    /* 40 */ { 0x20077ae5e1313600ULL,   1,   1, 226, 126,    1,   12,   0, 128, 0, 1 },
    /* 41 */ { 0x20077b65e1313680ULL,   1,   1, 158,  79,   48,   38,   0, 128, 0, 1 },
    /* 42 */ { 0x2007816621413b00ULL,   1,   1, 254, 254,    0,    0,   0, 128, 0, 1 },
    /* 43 */ { 0x20077be59d30b700ULL,   1,   1, 122,  57,  183,  115,   0, 128, 0, 1 },
    /* 44 */ { 0x20077c659d30b720ULL,   1,   1, 122,  57,  336,  115,   0, 128, 0, 1 },
};
