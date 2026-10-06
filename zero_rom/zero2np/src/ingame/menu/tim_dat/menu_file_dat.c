// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/menu_file_dat.c
//
// The collected-documents page's sprite table (menu_file_dat.o, data 0x324268).
//
// One flat SPRT_DAT array covering all four sub-pages, drawn from five
// different paks -- which is why menu_file.c calls PK2SendVram() again in
// front of nearly every group.  The groups menu_file.c walks are:
//
//      0        the word "FILE", out of the file-common pak
//      1..2     the title plate, out of the MENU_BG pak; [2] is [1] mirrored
//      3..10    the top page's window frame, eight tiles
//     11..15    the five tab heads, indexed directly by tag_csr
//     16        the lens flare over the picture, drawn 2x and additive
//     17..18    the tab arrows (MenuFileSmallArrowDisp's local table)
//     19..22    the unselected list row: left cap, 20 middles, right cap, end
//     23..25    the selected list row: left cap, 18 middles, right cap
//     26..27    the unread bracket pair, drawn 280 px apart
//     28..29    the scrollbar thumb caps -- the only two the cursor pulse
//              tints, which is why they are drawn after the bar
//     30..32    the scrollbar itself: top cap, middle tile, bottom cap
//     33..36    the scrollbar's frame: top, bottom, 12 middles, tail
//     37        the top page's picture.  tex0 is zero: MenuTim2SendVram()
//              points the VRAM page at the cross-fade slot and the caller
//              writes the real TEX0 in.
//     38..44    the document reader's window frame, seven tiles
//     45..46    the page arrows, and 47..48 the glow behind them
//     49..51    the photograph page's window frame, three tiles
//     52        the photograph page's centre picture (tex0 patched)
//     53..54    the previous/next photograph thumbnails
//     55..67    the map page's window frame, thirteen tiles
//     68..71    the map page's arrows: [68],[69] the crisp pair and
//              [70],[71] the glow, which is the pair that pulses
//     72        the map page's centre picture (tex0 patched)
//     73..74    the previous/next map thumbnails
//
// Entries 37, 52 and 72 are the three whose tex0 is 0 -- the cross-faded
// pictures, whose TEX0 is written at draw time.  Everything else carries the
// authored value.
//
// Read straight out of the ROM's .data and diffed against it byte-for-byte.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "menu_file_dat.h"

SPRT_DAT menu_file_tex[75] =                                /* data 324268 */
{
    /* ---- the word "FILE" -- file-common pak ---- */
    /*   0 */ { 0x20058005dd30abc0ULL,    1,    1,   98,   30,    44,    21,     0, 0x80, 0, 0 },

    /* ---- the title plate -- MENU_BG pak, [2] mirrors [1] ---- */
    /*   1 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,     2,    19,   160, 0x80, 0, 1 },
    /*   2 */ { 0x2005ac4621312c20ULL,  157,    1,   90,   32,    92,    19,   160, 0x80, 2, 1 },

    /* ---- the top page window, eight tiles ---- */
    /*   3 */ { 0x2005c80625322bc0ULL,    1,    1,  254,   68,    23,    59,     0, 0x80, 0, 1 },
    /*   4 */ { 0x2005c80625322bc0ULL,    1,  136,   88,   44,   277,    59,     0, 0x80, 0, 1 },
    /*   5 */ { 0x2005c80625322bc0ULL,  257,    1,  254,  254,   365,    59,     0, 0x80, 0, 1 },
    /*   6 */ { 0x2005c80625322bc0ULL,   91,  136,   11,  119,    34,   127,     0, 0x80, 0, 1 },
    /*   7 */ { 0x2005c80625322bc0ULL,   91,  136,   11,   40,    34,   246,     0, 0x80, 0, 1 },
    /*   8 */ { 0x2005c80625322bc0ULL,    1,  182,   88,   73,    23,   286,     0, 0x80, 0, 1 },
    /*   9 */ { 0x2005c80625322bc0ULL,    1,  119,  254,   15,   111,   333,     0, 0x80, 0, 1 },
    /*  10 */ { 0x2005c80625322bc0ULL,    1,   71,  254,   46,   365,   313,     0, 0x80, 0, 1 },

    /* ---- the five tab heads, indexed by tag_csr ---- */
    /*  11 */ { 0x2005c80625322bc0ULL,  105,  136,  124,   35,    57,    59,     0, 0x80, 0, 1 },
    /*  12 */ { 0x2005c80625322bc0ULL,  105,  173,  124,   35,   157,    59,     0, 0x80, 0, 1 },
    /*  13 */ { 0x2005c80625322bc0ULL,  105,  210,  124,   35,   259,    59,     0, 0x80, 0, 1 },
    /*  14 */ { 0x2005c885dd30adc0ULL,    2,    1,  124,   35,   360,    59,     0, 0x80, 0, 1 },
    /*  15 */ { 0x2005c885dd30adc0ULL,    2,   38,  124,   35,   461,    59,     0, 0x80, 0, 1 },

    /* ---- the lens flare over the picture (2x, additive) ---- */
    /*  16 */ { 0x2005c905dd30ae00ULL,    1,    1,  105,  104,   393,   116,   160, 0x19, 0, 1 },

    /* ---- the tab arrows ---- */
    /*  17 */ { 0x20058005dd30abc0ULL,  101,    1,   21,   22,    41,    66,     0, 0x80, 0, 1 },
    /*  18 */ { 0x20058005dd30abc0ULL,  101,    1,   21,   22,   579,    66,     0, 0x80, 2, 1 },

    /* ---- an unselected list row: cap, middle x20, cap, tail ---- */
    /*  19 */ { 0x20058005dd30abc0ULL,   79,   33,   48,   28,    76,   117,     0, 0x26, 0, 1 },
    /*  20 */ { 0x20058005dd30abc0ULL,  118,   63,   10,   28,   124,   117,     0, 0x26, 0, 1 },
    /*  21 */ { 0x20058005dd30abc0ULL,  118,   63,    6,   28,   324,   117,     0, 0x26, 0, 1 },
    /*  22 */ { 0x20058005dd30abc0ULL,   79,   33,   48,   28,   330,   117,     0, 0x26, 2, 1 },

    /* ---- a selected list row: cap, middle x18, cap ---- */
    /*  23 */ { 0x20058005dd30abc0ULL,    1,   33,   64,   30,    73,   116,     0, 0x66, 0, 1 },
    /*  24 */ { 0x20058005dd30abc0ULL,   67,   33,   10,   30,   137,   116,     0, 0x66, 0, 1 },
    /*  25 */ { 0x20058005dd30abc0ULL,    1,   33,   64,   30,   317,   116,     0, 0x66, 2, 1 },

    /* ---- the unread bracket pair, 280 px apart ---- */
    /*  26 */ { 0x20058005dd30abc0ULL,   48,   65,   11,   28,    81,   117,     0, 0x80, 0, 1 },
    /*  27 */ { 0x20058005dd30abc0ULL,   48,   65,   11,   28,   361,   117,     0, 0x80, 2, 1 },

    /* ---- the scrollbar thumb caps -- these two take the pulse ---- */
    /*  28 */ { 0x20058005dd30abc0ULL,   82,   63,   18,   19,    52,   107,     0, 0x80, 0, 1 },
    /*  29 */ { 0x20058005dd30abc0ULL,   82,   63,   18,   19,    52,   312,     0, 0x80, 1, 1 },

    /* ---- the scrollbar: top cap, middle tile, bottom cap ---- */
    /*  30 */ { 0x20058005dd30abc0ULL,   32,   81,   14,   14,    55,   129,     0, 0x80, 1, 1 },
    /*  31 */ { 0x20058005dd30abc0ULL,   32,   65,   14,   14,    55,   143,     0, 0x80, 0, 1 },
    /*  32 */ { 0x20058005dd30abc0ULL,   32,   81,   14,   14,    55,   295,     0, 0x80, 0, 1 },

    /* ---- the scrollbar frame: top, bottom, middle x12, tail ---- */
    /*  33 */ { 0x2005c905dd30ae00ULL,  105,   79,   22,   48,    51,   108,   160, 0x80, 0, 1 },
    /*  34 */ { 0x2005c905dd30ae00ULL,  105,   79,   22,   48,    51,   282,   160, 0x80, 1, 1 },
    /*  35 */ { 0x2005c905dd30ae00ULL,    1,  107,   22,   10,    51,   156,   160, 0x80, 0, 1 },
    /*  36 */ { 0x2005c905dd30ae00ULL,    1,  107,   22,    6,    51,   276,   160, 0x80, 0, 1 },

    /* ---- the top page picture -- TEX0 patched at draw time ---- */
    /*  37 */ { 0x0000000000000000ULL,    0,    0,  256,  256,   370,    91,   160, 0x80, 0, 1 },

    /* ---- the document reader window, seven tiles ---- */
    /*  38 */ { 0x2005b80625322bc0ULL,    1,    1,  510,   76,    30,    50,     0, 0x80, 0, 1 },
    /*  39 */ { 0x2005b80625322bc0ULL,   75,  122,   70,  133,   540,    50,     0, 0x80, 0, 1 },
    /*  40 */ { 0x2005b80625322bc0ULL,    1,  122,   35,  133,    39,   126,     0, 0x80, 0, 1 },
    /*  41 */ { 0x2005b80625322bc0ULL,   38,  122,   35,  126,    39,   259,     0, 0x80, 0, 1 },
    /*  42 */ { 0x2005b80625322bc0ULL,  148,  122,   35,  133,   566,   183,     0, 0x80, 0, 1 },
    /*  43 */ { 0x2005b80625322bc0ULL,    1,   79,  510,   41,    30,   385,     0, 0x80, 0, 1 },
    /*  44 */ { 0x2005b80625322bc0ULL,  185,  122,   70,  110,   540,   316,     0, 0x80, 0, 1 },

    /* ---- the page arrows, and the glow drawn behind them ---- */
    /*  45 */ { 0x20058005dd30abc0ULL,   61,   65,   19,   22,    20,   248,     0, 0x80, 0, 1 },
    /*  46 */ { 0x20058005dd30abc0ULL,   61,   65,   19,   22,   601,   248,     0, 0x80, 2, 1 },
    /*  47 */ { 0x20058005dd30abc0ULL,    1,   65,   29,   30,    16,   243,     0, 0x80, 0, 1 },
    /*  48 */ { 0x20058005dd30abc0ULL,    1,   65,   29,   30,   595,   243,     0, 0x80, 2, 1 },

    /* ---- the photograph page window, three tiles ---- */
    /*  49 */ { 0x2005c80625322bc0ULL,    1,    1,  510,  254,    65,    58,     0, 0x80, 0, 1 },
    /*  50 */ { 0x2005c885e1312dc0ULL,    1,    1,  254,   45,    65,   312,     0, 0x80, 0, 1 },
    /*  51 */ { 0x2005c885e1312dc0ULL,    1,   48,  254,   45,   319,   312,     0, 0x80, 0, 1 },

    /* ---- the photograph -- TEX0 patched at draw time ---- */
    /*  52 */ { 0x0000000000000000ULL,    1,    1,  275,  238,   181,   106,     0, 0x80, 0, 1 },

    /* ---- the previous / next photograph thumbnails ---- */
    /*  53 */ { 0x20057c059d30abc0ULL,    1,    1,   69,   60,    91,   195,     0, 0x80, 0, 1 },
    /*  54 */ { 0x20057c059d30abc0ULL,    1,    1,   69,   60,   477,   195,     0, 0x80, 0, 1 },

    /* ---- the map page window, thirteen tiles ---- */
    /*  55 */ { 0x2005bc0625322bc0ULL,    1,    1,  261,   51,    26,    54,     0, 0x80, 0, 1 },
    /*  56 */ { 0x2005bc0625322bc0ULL,    1,   54,  261,   51,   287,    54,     0, 0x80, 0, 1 },
    /*  57 */ { 0x2005bc859d30adc0ULL,    8,    1,   63,   51,   548,    54,     0, 0x80, 0, 1 },
    /*  58 */ { 0x2005bc0625322bc0ULL,   86,  107,   90,  105,    29,   253,     0, 0x80, 0, 1 },
    /*  59 */ { 0x2005bc0625322bc0ULL,    1,  107,   83,  148,    36,   105,     0, 0x80, 0, 1 },
    /*  60 */ { 0x2005bc0625322bc0ULL,  486,  133,   19,  116,    17,   166,     0, 0x80, 0, 1 },
    /*  61 */ { 0x2005bc0625322bc0ULL,  178,  107,   82,  148,   519,   105,     0, 0x80, 0, 1 },
    /*  62 */ { 0x2005bc0625322bc0ULL,  466,  133,   18,  116,   601,   166,     0, 0x80, 0, 1 },
    /*  63 */ { 0x2005bc0625322bc0ULL,   86,  214,   82,   41,   519,   253,     0, 0x80, 0, 1 },
    /*  64 */ { 0x2005bc0625322bc0ULL,  466,    1,   45,   64,   519,   294,     0, 0x80, 0, 1 },
    /*  65 */ { 0x2005bc0625322bc0ULL,  466,   67,   45,   64,   564,   294,     0, 0x80, 0, 1 },
    /*  66 */ { 0x2005bc0625322bc0ULL,  264,    1,  200,  253,   119,   105,     0, 0x80, 0, 1 },
    /*  67 */ { 0x2005bc0625322bc0ULL,  264,    1,  200,  253,   319,   105,     0, 0x80, 2, 1 },

    /* ---- the map arrows: crisp pair, then the glow that pulses ---- */
    /*  68 */ { 0x2005bc859d30adc0ULL,   73,   25,   14,   16,    64,   180,     0, 0x80, 2, 1 },
    /*  69 */ { 0x2005bc859d30adc0ULL,   73,   25,   14,   16,   559,   180,     0, 0x80, 0, 1 },
    /*  70 */ { 0x2005bc859d30adc0ULL,   73,    1,   21,   22,    60,   177,     0, 0x80, 2, 1 },
    /*  71 */ { 0x2005bc859d30adc0ULL,   73,    1,   21,   22,   556,   177,     0, 0x80, 0, 1 },

    /* ---- the map picture -- TEX0 patched at draw time ---- */
    /*  72 */ { 0x0000000000000000ULL,    1,    1,  400,  238,   119,   105,     0, 0x80, 0, 1 },

    /* ---- the previous / next map thumbnails ---- */
    /*  73 */ { 0x20057c059d30abc0ULL,    1,    1,   69,   44,    39,   204,     0, 0x80, 0, 1 },
    /*  74 */ { 0x20057c059d30abc0ULL,    1,    1,   69,   44,   529,   205,     0, 0x80, 0, 1 },
};
