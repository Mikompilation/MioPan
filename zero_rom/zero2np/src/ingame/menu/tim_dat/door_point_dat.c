// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/door_point_dat.c
//
// The map page's door positions (door_point_dat.o, data 0x2db398 + rodata
// 0x3a4518).
//
// One array per map sheet, each walked by MenuMapDoorDisp() until it finds a
// record whose room_label1 AND room_label2 are both -1 -- so the last entry
// of every array is that terminator rather than a door.  The declared bounds
// below therefore count one more than the sheet has doors.
//
// Two facts worth knowing:
//
//  * A door is drawn if EITHER of its two rooms has been seen.  That is what
//    the pair is for: a door on the boundary between two rooms appears as
//    soon as the player has been in one of them, so the map does not have
//    holes along the edge of explored ground.  Several records name the same
//    room twice, which is the "this door only belongs to one room" case.
//
//  * ghost_seal_door_label is an index into ghost_seal_door_data[], not a
//    flag.  -1 means an ordinary door; anything else routes the draw through
//    MenuMapGhostSeal*DoorDisp(), which asks GetGhostSealDoorState() whether
//    to use the sealed art and whether to add the seal mark.
//
// The names are the ROM's own and follow the pak naming: soto is the grounds,
// os/ks/ry/tb the four houses (0/1/2 being the floor within each), chika the
// basement and ku* the Kurosawa house.
//
// Read straight out of the ROM and diffed against it byte-for-byte.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "door_point_dat.h"

#include <stddef.h>                    /* NULL */

static const MAP_DOOR_POINT map_soto_door_point[13] =                       /* rdata 3a4518 */
{
    /*  0 */ {      469.0f,      473.0f,  -1,    5,    5,  1 },
    /*  1 */ {      415.0f,      302.0f,  -1,   14,   15,  5 },
    /*  2 */ {      415.0f,      111.0f,  -1,   38,   38,  5 },
    /*  3 */ {      286.0f,      461.0f,   2,   11,   11,  5 },
    /*  4 */ {      375.0f,      354.0f,  -1,   13,   13,  0 },
    /*  5 */ {      351.0f,      416.0f,  -1,   12,   12,  6 },
    /*  6 */ {      458.0f,      342.0f,  -1,   14,   14,  1 },
    /*  7 */ {      547.0f,      500.0f,  -1,    8,   19,  3 },
    /*  8 */ {      543.0f,      486.0f,  -1,    8,    8,  3 },
    /*  9 */ {      130.0f,      463.0f,   8,   26,   26,  0 },
    /* 10 */ {      125.0f,      480.0f,  -1,   26,   26,  0 },
    /* 11 */ {       68.0f,      280.0f,  -1,   34,   34,  4 },
    /* 12 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_os1_door_point[7] =                         /* rdata 3a4650 */
{
    /*  0 */ {      214.0f,       28.0f,  -1,   46,   46,  1 },
    /*  1 */ {      177.0f,       22.0f,  -1,   46,   49,  1 },
    /*  2 */ {      177.0f,       72.0f,  -1,   47,   49,  1 },
    /*  3 */ {      114.0f,       73.0f,   0,   49,   54,  1 },
    /*  4 */ {       61.0f,       57.0f,   1,   53,   55,  1 },
    /*  5 */ {        3.0f,      104.0f,  -1,   52,   58,  0 },
    /*  6 */ {       -1.0f,       -1.0f,  -1,   -1,   -1,  0 },
};

static const MAP_DOOR_POINT map_os2_door_point[2] =                         /* rdata 3a46f8 */
{
    /*  0 */ {       96.0f,       50.0f,  -1,   60,   62,  1 },
    /*  1 */ {       -1.0f,       -1.0f,  -1,   -1,   -1,  0 },
};

static const MAP_DOOR_POINT map_os0_door_point[1] =                         /* rdata 3a4728 */
{
    /*  0 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_ks1_door_point[20] =                        /* rdata 3a4740 */
{
    /*  0 */ {      165.0f,      583.0f,  -1,   66,   66,  5 },
    /*  1 */ {      167.0f,      471.0f,  -1,   66,   67,  5 },
    /*  2 */ {      206.0f,      471.0f,  -1,   66,   97,  0 },
    /*  3 */ {      167.0f,      346.0f,  -1,   68,   69,  1 },
    /*  4 */ {      139.0f,      424.0f,  -1,   69,   70,  0 },
    /*  5 */ {      109.0f,      501.0f,  -1,   72,   74,  0 },
    /*  6 */ {       16.0f,      377.0f,  -1,   73,   75,  1 },
    /*  7 */ {       19.0f,      342.0f,  -1,   75,   76,  0 },
    /*  8 */ {      109.0f,      342.0f,  -1,   75,   77,  0 },
    /*  9 */ {      405.0f,      495.0f,  -1,   82,   83,  5 },
    /* 10 */ {      416.0f,      510.0f,  -1,   83,   83,  0 },
    /* 11 */ {      334.0f,      167.0f,  -1,   91,   92,  3 },
    /* 12 */ {      309.0f,      142.0f,  -1,   90,   92,  3 },
    /* 13 */ {      358.0f,      106.0f,  -1,   92,   93,  8 },
    /* 14 */ {      187.0f,      346.0f,  -1,   68,   78,  1 },
    /* 15 */ {      206.0f,      415.0f,  -1,   78,   79,  0 },
    /* 16 */ {      273.0f,      430.0f,  -1,   80,   86,  5 },
    /* 17 */ {      418.0f,       42.0f,  -1,   93,   94,  8 },
    /* 18 */ {      394.0f,      475.0f,  -1,   81,   82,  1 },
    /* 19 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_ks2_door_point[8] =                         /* rdata 3a4920 */
{
    /*  0 */ {      141.0f,      342.0f,  -1,   99,  104,  0 },
    /*  1 */ {      153.0f,      387.0f,  -1,  104,  108,  1 },
    /*  2 */ {      107.0f,      428.0f,  -1,  105,  106,  1 },
    /*  3 */ {       64.0f,      425.0f,  -1,  106,  103,  0 },
    /*  4 */ {      126.0f,      394.0f,  -1,  103,  107,  0 },
    /*  5 */ {      234.0f,      431.0f,  -1,  102,  109,  1 },
    /*  6 */ {      184.0f,      384.0f,  -1,  101,  108,  0 },
    /*  7 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_ks0_door_point[5] =                         /* rdata 3a49e0 */
{
    /*  0 */ {      334.0f,      325.0f,  -1,  110,  116,  1 },
    /*  1 */ {      353.0f,      397.0f,  -1,  112,  114,  1 },
    /*  2 */ {      214.0f,      397.0f,  -1,  111,  113,  1 },
    /*  3 */ {      329.0f,       77.0f,  -1,  115,  115,  1 },
    /*  4 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_ry1_door_point[16] =                        /* rdata 3a4a58 */
{
    /*  0 */ {       17.0f,      309.0f,  -1,  119,  119,  5 },
    /*  1 */ {       97.0f,      285.0f,  -1,  120,  122,  0 },
    /*  2 */ {       66.0f,      235.0f,  -1,  122,  125,  0 },
    /*  3 */ {       61.0f,      203.0f,  -1,  125,  123,  1 },
    /*  4 */ {       61.0f,      157.0f,  -1,  125,  124,  1 },
    /*  5 */ {       46.0f,      142.0f,  -1,  126,  147,  1 },
    /*  6 */ {       78.0f,       81.0f,  -1,  128,  142,  1 },
    /*  7 */ {      130.0f,       79.0f,  -1,  129,  132,  0 },
    /*  8 */ {       78.0f,       16.0f,  -1,  132,  138,  1 },
    /*  9 */ {       12.0f,       58.0f,  -1,  134,  139,  1 },
    /* 10 */ {        1.0f,       92.0f,  -1,  133,  147,  0 },
    /* 11 */ {      157.0f,       29.0f,  -1,  137,  140,  1 },
    /* 12 */ {      145.0f,       54.0f,  -1,  137,  146,  0 },
    /* 13 */ {      186.0f,       82.0f,  -1,  146,  144,  1 },
    /* 14 */ {      188.0f,      103.0f,  -1,  144,  144,  0 },
    /* 15 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_ry2_door_point[6] =                         /* rdata 3a4bd8 */
{
    /*  0 */ {      173.0f,       83.0f,  -1,  148,  157,  0 },
    /*  1 */ {      173.0f,      159.0f,  -1,  148,  148,  0 },
    /*  2 */ {      116.0f,       36.0f,  -1,  161,  161,  1 },
    /*  3 */ {      116.0f,       22.0f,  -1,  158,  152,  1 },
    /*  4 */ {      105.0f,      106.0f,  -1,  154,  165,  0 },
    /*  5 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_ry0_door_point[1] =                         /* rdata 3a4c68 */
{
    /*  0 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_tb1_door_point[16] =                        /* rdata 3a4c80 */
{
    /*  0 */ {       -2.0f,      190.0f,  -1,  167,  167,  6 },
    /*  1 */ {       22.0f,      125.0f,  -1,  168,  170,  1 },
    /*  2 */ {       72.0f,      156.0f,  -1,  170,  171,  1 },
    /*  3 */ {       88.0f,      151.0f,  -1,  171,  176,  5 },
    /*  4 */ {      124.0f,      151.0f,  -1,  171,  176,  5 },
    /*  5 */ {       80.0f,      122.0f,  -1,  176,  176,  6 },
    /*  6 */ {      157.0f,      183.0f,  -1,  172,  192,  0 },
    /*  7 */ {      218.0f,      151.0f,   7,  174,  187,  0 },
    /*  8 */ {      228.0f,       91.0f,   4,  175,  178,  1 },
    /*  9 */ {      283.0f,      151.0f,   3,  178,  184,  0 },
    /* 10 */ {      215.0f,      220.0f,  -1,  179,  192,  1 },
    /* 11 */ {      270.0f,       72.0f,  -1,  183,  186,  0 },
    /* 12 */ {      253.0f,       76.0f,   5,  183,  191,  1 },
    /* 13 */ {      217.0f,       43.0f,   6,  191,  189,  0 },
    /* 14 */ {      204.0f,       33.0f,  -1,  189,  189,  1 },
    /* 15 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_tb2_door_point[14] =                        /* rdata 3a4e00 */
{
    /*  0 */ {      265.0f,      113.0f,  -1,  204,  205,  0 },
    /*  1 */ {      277.0f,      113.0f,  -1,  202,  196,  0 },
    /*  2 */ {      234.0f,      116.0f,  -1,  194,  200,  1 },
    /*  3 */ {      223.0f,      164.0f,  -1,  197,  199,  0 },
    /*  4 */ {      201.0f,      116.0f,  -1,  198,  209,  1 },
    /*  5 */ {      153.0f,      159.0f,  -1,  207,  211,  1 },
    /*  6 */ {      112.0f,      167.0f,  -1,  212,  213,  0 },
    /*  7 */ {      109.0f,      179.0f,  -1,  212,  214,  1 },
    /*  8 */ {       88.0f,      211.0f,  -1,  215,  217,  1 },
    /*  9 */ {       62.0f,      167.0f,  -1,  214,  216,  0 },
    /* 10 */ {       86.0f,      167.0f,  -1,  213,  214,  0 },
    /* 11 */ {       54.0f,      135.0f,  -1,  216,  216,  0 },
    /* 12 */ {       31.0f,      113.0f,  -1,  216,  216,  0 },
    /* 13 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_tb0_door_point[1] =                         /* rdata 3a4f50 */
{
    /*  0 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_chika_door_point[2] =                       /* rdata 3a4f68 */
{
    /*  0 */ {      299.0f,       78.0f,  -1,  225,  225,  1 },
    /*  1 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_kur_door_point[3] =                         /* rdata 3a4f98 */
{
    /*  0 */ {        5.0f,       58.0f,  -1,  239,  239,  5 },
    /*  1 */ {       13.0f,       34.0f,  -1,  239,  239,  0 },
    /*  2 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_kuh_door_point[3] =                         /* rdata 3a4fe0 */
{
    /*  0 */ {       23.0f,      147.0f,  -1,  237,  237,  5 },
    /*  1 */ {       29.0f,       14.0f,  -1,  237,  237,  0 },
    /*  2 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};

static const MAP_DOOR_POINT map_kuc_door_point[3] =                         /* rdata 3a5028 */
{
    /*  0 */ {       21.0f,       61.0f,  -1,  238,  238,  3 },
    /*  1 */ {       55.0f,       64.0f,  -1,  238,  238,  2 },
    /*  2 */ {       -1.0f,       -1.0f,  -1,   -1,   -1, -1 },
};
/* map_label 0..16, then the NULL that ends the table.  MenuMapDoorDisp()
 * indexes this directly with the sheet it was handed and returns at once on
 * a NULL, so a sheet with no door array of its own costs nothing. */
const MAP_DOOR_POINT *menu_map_door_data[18] =              /* data 2db398 */
{
    /*  0 */ map_soto_door_point,
    /*  1 */ map_os1_door_point,
    /*  2 */ map_os2_door_point,
    /*  3 */ map_os0_door_point,
    /*  4 */ map_ks1_door_point,
    /*  5 */ map_ks2_door_point,
    /*  6 */ map_ks0_door_point,
    /*  7 */ map_ry1_door_point,
    /*  8 */ map_ry2_door_point,
    /*  9 */ map_ry0_door_point,
    /* 10 */ map_tb1_door_point,
    /* 11 */ map_tb2_door_point,
    /* 12 */ map_tb0_door_point,
    /* 13 */ map_chika_door_point,
    /* 14 */ map_kur_door_point,
    /* 15 */ map_kuh_door_point,
    /* 16 */ map_kuc_door_point,
    /* 17 */ NULL,
};
