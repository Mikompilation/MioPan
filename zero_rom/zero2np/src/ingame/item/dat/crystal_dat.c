/* ==========================================================================
 *  ingame/item/dat/crystal_dat.c
 *
 *  Subtitle timing for the spirit-stone (crystal) audio recordings.  Pure
 *  data: crystal_dat.o contributes no code at all -- it has no .text section
 *  in ZERO2.MAP, not even fixed_array boilerplate.
 *
 *  One table per crystal, each row a message id and the frame range it is
 *  shown over.  crystal_title_dat[] gathers them so crystal.c can index by
 *  label; entry 40 is the out-of-range fallback and points at the one-row
 *  dummy, which pairs with crystal_stream[40] in crystal.c.
 *
 *  Read straight out of SLES_523.84 at data 2d8e60, 279 rows of 0xc bytes
 *  plus the 41-pointer index.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "crystal_dat.h"

/* msg_id, start_frame, end_frame */

MOVIE_TITLE_DAT crystal_title_dummy[1] =                    /* data 2d8e60 */
{
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_000[5] =                      /* data 2d8e70 */
{
    {      0,   120,   241 },
    {      2,   263,   346 },
    {      3,   356,   510 },
    {      6,   529,   650 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_001[9] =                      /* data 2d8eb0 */
{
    {     10,    85,   175 },
    {     11,   175,   225 },
    {     12,   229,   335 },
    {     13,   338,   410 },
    {     14,   410,   478 },
    {     15,   507,   569 },
    {     16,   586,   657 },
    {     17,   667,   743 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_002[7] =                      /* data 2d8f20 */
{
    {     20,    93,   231 },
    {     21,   255,   307 },
    {     22,   320,   390 },
    {     23,   405,   446 },
    {     24,   471,   628 },
    {     26,   653,   710 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_003[7] =                      /* data 2d8f78 */
{
    {     29,    70,   230 },
    {     30,   258,   427 },
    {     31,   447,   523 },
    {     32,   540,   618 },
    {     33,   629,   717 },
    {     34,   731,   843 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_004[7] =                      /* data 2d8fd0 */
{
    {     38,   115,   175 },
    {     39,   178,   234 },
    {     40,   264,   323 },
    {     41,   327,   470 },
    {     42,   507,   610 },
    {     43,   613,   703 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_005[5] =                      /* data 2d9028 */
{
    {     45,    75,   126 },
    {     47,   128,   219 },
    {     48,   223,   287 },
    {     49,   299,   394 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_006[8] =                      /* data 2d9068 */
{
    {     53,    92,   144 },
    {     54,   146,   212 },
    {     55,   215,   318 },
    {     56,   321,   398 },
    {     58,   401,   467 },
    {     59,   470,   565 },
    {     60,   568,   680 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_007[9] =                      /* data 2d90c8 */
{
    {     63,   115,   235 },
    {     64,   245,   298 },
    {     65,   307,   389 },
    {     66,   402,   503 },
    {     67,   527,   578 },
    {     68,   591,   663 },
    {     69,   676,   748 },
    {     71,   754,   823 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_008[4] =                      /* data 2d9138 */
{
    {     74,    85,   170 },
    {     76,   173,   272 },
    {     77,   282,   402 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_009[10] =                     /* data 2d9168 */
{
    {     80,    75,   119 },
    {     81,   122,   162 },
    {     82,   162,   227 },
    {     83,   256,   298 },
    {     84,   301,   393 },
    {     85,   397,   472 },
    {     86,   475,   541 },
    {     87,   543,   600 },
    {     88,   603,   680 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_010[5] =                      /* data 2d91e0 */
{
    {     91,    69,   104 },
    {     92,   119,   215 },
    {     94,   328,   396 },
    {     95,   402,   456 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_011[6] =                      /* data 2d9220 */
{
    {    106,    55,   176 },
    {    107,   195,   250 },
    {    108,   270,   419 },
    {    109,   432,   481 },
    {    110,   550,   650 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_012[8] =                      /* data 2d9268 */
{
    {     98,    74,   135 },
    {     99,   150,   224 },
    {    100,   251,   334 },
    {    101,   379,   483 },
    {    102,   500,   591 },
    {    103,   612,   682 },
    {    104,   705,   775 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_013[7] =                      /* data 2d92c8 */
{
    {    111,   115,   200 },
    {    112,   231,   269 },
    {    113,   297,   388 },
    {    115,   402,   446 },
    {    116,   475,   542 },
    {    117,   620,   689 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_014[7] =                      /* data 2d9320 */
{
    {    119,   109,   199 },
    {    120,   221,   267 },
    {    121,   277,   335 },
    {    122,   345,   454 },
    {    123,   480,   727 },
    {    124,   742,  1014 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_015[5] =                      /* data 2d9378 */
{
    {    128,    82,   126 },
    {    129,   148,   240 },
    {    130,   278,   415 },
    {    131,   444,   570 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_016[6] =                      /* data 2d93b8 */
{
    {    134,    68,   245 },
    {    135,   247,   399 },
    {    136,   420,   495 },
    {    137,   498,   600 },
    {    138,   603,   755 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_017[8] =                      /* data 2d9400 */
{
    {    140,    68,   136 },
    {    141,   142,   248 },
    {    142,   251,   369 },
    {    143,   395,   519 },
    {    144,   540,   655 },
    {    145,   681,   811 },
    {    146,   838,   978 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_018[7] =                      /* data 2d9460 */
{
    {    150,    91,   190 },
    {    152,   192,   265 },
    {    153,   268,   348 },
    {    154,   351,   425 },
    {    155,   489,   573 },
    {    156,   588,   704 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_019[6] =                      /* data 2d94b8 */
{
    {    160,    79,   192 },
    {    161,   196,   295 },
    {    162,   297,   369 },
    {    163,   374,   439 },
    {    164,   446,   525 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_020[10] =                     /* data 2d9500 */
{
    {    167,   142,   201 },
    {    168,   206,   321 },
    {    169,   330,   389 },
    {    170,   419,   491 },
    {    171,   495,   565 },
    {    172,   569,   666 },
    {    173,   669,   724 },
    {    174,   780,   887 },
    {    175,   900,  1013 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_021[10] =                     /* data 2d9578 */
{
    {    178,    89,   248 },
    {    179,   267,   419 },
    {    180,   446,   540 },
    {    181,   556,   692 },
    {    182,   729,   889 },
    {    183,   921,  1034 },
    {    184,  1051,  1152 },
    {    185,  1157,  1218 },
    {    186,  1232,  1337 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_022[7] =                      /* data 2d95f0 */
{
    {    189,    98,   173 },
    {    190,   188,   313 },
    {    191,   323,   381 },
    {    192,   385,   485 },
    {    193,   512,   595 },
    {    194,   601,   721 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_023[5] =                      /* data 2d9648 */
{
    {    197,    90,   162 },
    {    198,   173,   255 },
    {    199,   259,   332 },
    {    200,   343,   404 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_024[5] =                      /* data 2d9688 */
{
    {    203,   114,   173 },
    {    204,   215,   314 },
    {    205,   365,   451 },
    {    206,   476,   536 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_025[4] =                      /* data 2d96c8 */
{
    {    210,    90,   154 },
    {    211,   190,   316 },
    {    212,   349,   496 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_026[5] =                      /* data 2d96f8 */
{
    {    217,    88,   198 },
    {    219,   214,   314 },
    {    220,   322,   394 },
    {    221,   405,   459 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_027[6] =                      /* data 2d9738 */
{
    {    223,    86,   283 },
    {    225,   284,   433 },
    {    226,   450,   597 },
    {    228,   612,   670 },
    {    229,   675,   765 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_028[8] =                      /* data 2d9780 */
{
    {    232,   117,   204 },
    {    233,   210,   272 },
    {    234,   278,   338 },
    {    235,   361,   451 },
    {    236,   463,   542 },
    {    237,   567,   632 },
    {    238,   640,   716 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_029[7] =                      /* data 2d97e0 */
{
    {    240,   134,   182 },
    {    241,   218,   339 },
    {    242,   391,   503 },
    {    243,   534,   598 },
    {    244,   648,   783 },
    {    245,   805,   864 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_030[7] =                      /* data 2d9838 */
{
    {    247,   103,   219 },
    {    248,   284,   383 },
    {    249,   400,   472 },
    {    250,   554,   834 },
    {    251,   890,  1056 },
    {    252,  1066,  1300 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_031[8] =                      /* data 2d9890 */
{
    {    255,    84,   155 },
    {    256,   170,   237 },
    {    257,   280,   373 },
    {    258,   400,   465 },
    {    259,   494,   560 },
    {    260,   574,   638 },
    {    261,   665,   763 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_032[9] =                      /* data 2d98f0 */
{
    {    265,    94,   159 },
    {    266,   192,   289 },
    {    267,   328,   406 },
    {    268,   414,   465 },
    {    269,   482,   539 },
    {    270,   570,   626 },
    {    271,   631,   679 },
    {    272,   692,   764 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_033[6] =                      /* data 2d9960 */
{
    {    279,    91,   192 },
    {    280,   224,   333 },
    {    281,   362,   427 },
    {    282,   453,   500 },
    {    283,   540,   607 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_034[6] =                      /* data 2d99a8 */
{
    {    287,    99,   158 },
    {    289,   161,   282 },
    {    290,   329,   453 },
    {    292,   487,   597 },
    {    294,   610,   698 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_035[8] =                      /* data 2d99f0 */
{
    {    297,    70,   165 },
    {    298,   202,   272 },
    {    299,   301,   393 },
    {    301,   416,   469 },
    {    303,   499,   622 },
    {    304,   644,   740 },
    {    305,   770,   846 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_036[9] =                      /* data 2d9a50 */
{
    {    308,    80,   152 },
    {    309,   148,   235 },
    {    310,   239,   289 },
    {    311,   284,   338 },
    {    312,   341,   458 },
    {    313,   480,   536 },
    {    314,   543,   608 },
    {    315,   614,   678 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_037[8] =                      /* data 2d9ac0 */
{
    {    318,    85,   148 },
    {    319,   173,   341 },
    {    320,   378,   432 },
    {    321,   456,   581 },
    {    322,   618,   697 },
    {    323,   722,   859 },
    {    324,   885,  1009 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_038[6] =                      /* data 2d9b20 */
{
    {    327,    86,   188 },
    {    329,   273,   340 },
    {    330,   358,   421 },
    {    331,   441,   516 },
    {    332,   522,   700 },
    {     -1,    -1,    -1 },
};

MOVIE_TITLE_DAT crystal_title_039[8] =                      /* data 2d9b68 */
{
    {    336,    84,   196 },
    {    338,   212,   323 },
    {    339,   334,   488 },
    {    341,   515,   668 },
    {    342,   677,   726 },
    {    343,   743,   833 },
    {    344,   855,   952 },
    {     -1,    -1,    -1 },
};

/* Index by crystal label.  [40] is the fallback the range assert does not
 * actually prevent you from reaching -- PrintAssertReal returns. */
MOVIE_TITLE_DAT *crystal_title_dat[CRYSTAL_TITLE_DAT_MAX] = /* data 2d9bc8 */
{
    crystal_title_000, crystal_title_001, crystal_title_002, crystal_title_003,
    crystal_title_004, crystal_title_005, crystal_title_006, crystal_title_007,
    crystal_title_008, crystal_title_009, crystal_title_010, crystal_title_011,
    crystal_title_012, crystal_title_013, crystal_title_014, crystal_title_015,
    crystal_title_016, crystal_title_017, crystal_title_018, crystal_title_019,
    crystal_title_020, crystal_title_021, crystal_title_022, crystal_title_023,
    crystal_title_024, crystal_title_025, crystal_title_026, crystal_title_027,
    crystal_title_028, crystal_title_029, crystal_title_030, crystal_title_031,
    crystal_title_032, crystal_title_033, crystal_title_034, crystal_title_035,
    crystal_title_036, crystal_title_037, crystal_title_038, crystal_title_039,
    crystal_title_dummy,
};
