/* ==========================================================================
 *  graphics/scene/scene_dat.c
 *
 *  Static scene tables.  scene_dat.o carries no code at all -- its .text holds
 *  nothing but the fixed_array template instantiations -- so the whole object
 *  is these tables.
 *
 *  Both of scene_dat.o's globals are here: scene_cut_timing, which fod.c's
 *  FodNextFrame() reads, and scene_data_cmn[72] (data 0x343210, 0x48 bytes),
 *  which InitMovieWithTitle() reads.
 *
 *  scene_cut_timing[SceneNo] is a -1-terminated list of the frames a scene's
 *  camera cuts on.  In PAL the frame counter advances 1.2 frames a tick, so it
 *  drifts off the authored cut points; FodNextFrame() snaps it back to the next
 *  entry in this list so a cut lands on the same frame it does in NTSC.  It is
 *  never read in NTSC.
 *
 *  Verified against the ROM: all 71 slots resolve to one of the 38 arrays
 *  below, and every array's length matches globals.txt.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "scene_dat.h"

/* Every scene with no authored cuts shares this one empty list. */
static int sceneDummy_cut_timing[1] =
{
    -1
};

static int scene0110_cut_timing[7] =
{
    50, 140, 230, 480, 600, 720, -1
};

static int scene0120_cut_timing[16] =
{
    40, 170, 435, 505, 555, 735, 785, 900, 945, 1145, 1190, 1205, 1225, 1240, 1265, -1
};

static int scene0122_cut_timing[9] =
{
    10, 35, 65, 195, 250, 415, 450, 483, -1
};

static int scene0130_cut_timing[8] =
{
    105, 300, 495, 565, 720, 840, 885, -1
};

static int scene0132_cut_timing[8] =
{
    30, 160, 500, 560, 620, 670, 750, -1
};

static int scene0133_cut_timing[5] =
{
    70, 115, 135, 154, -1
};

static int scene0140_cut_timing[19] =
{
    60, 210, 300, 458, 670, 680, 740, 860, 890, 1090, 1105, 1125, 1134, 1164, 1204, 1224, 1264,
    1294, -1
};

static int scene0150_cut_timing[10] =
{
    60, 80, 105, 210, 235, 265, 305, 390, 410, -1
};

static int scene0160_cut_timing[5] =
{
    60, 130, 180, 260, -1
};

static int scene0170_cut_timing[2] =
{
    215, -1
};

static int scene0190_cut_timing[4] =
{
    185, 300, 420, -1
};

static int scene0210_cut_timing[5] =
{
    210, 240, 300, 360, -1
};

static int scene0231_cut_timing[10] =
{
    100, 140, 200, 230, 250, 270, 285, 330, 420, -1
};

static int scene0240_cut_timing[7] =
{
    120, 170, 230, 320, 400, 460, -1
};

static int scene0340_cut_timing[23] =
{
    60, 110, 150, 200, 245, 350, 405, 490, 555, 703, 706, 760, 761, 765, 766, 767, 769, 860, 942,
    1102, 1152, 1242, -1
};

static int scene0350_cut_timing[24] =
{
    130, 160, 200, 245, 255, 270, 310, 370, 400, 440, 520, 590, 650, 750, 770, 830, 900, 970,
    1030, 1060, 1150, 1190, 1230, -1
};

static int scene0352_cut_timing[12] =
{
    100, 150, 210, 260, 320, 400, 460, 530, 670, 710, 800, -1
};

static int scene0410_cut_timing[5] =
{
    125, 280, 310, 360, -1
};

static int scene0510_cut_timing[2] =
{
    180, -1
};

static int scene0520_cut_timing[18] =
{
    170, 300, 370, 400, 460, 620, 635, 670, 810, 930, 970, 1010, 1060, 1120, 1190, 1230, 1280,
    -1
};

static int scene0610_cut_timing[11] =
{
    70, 110, 180, 225, 325, 355, 510, 555, 620, 660, -1
};

static int scene0620_cut_timing[7] =
{
    40, 70, 90, 170, 280, 460, -1
};

static int scene0720_cut_timing[14] =
{
    320, 480, 570, 620, 710, 840, 870, 1020, 1050, 1191, 1270, 1380, 1470, -1
};

static int scene0721_cut_timing[5] =
{
    310, 400, 570, 929, -1
};

static int scene0730_cut_timing[14] =
{
    25, 45, 125, 195, 275, 410, 470, 530, 605, 640, 690, 780, 860, -1
};

static int scene0820_cut_timing[7] =
{
    60, 90, 240, 330, 420, 560, -1
};

static int scene1010_cut_timing[11] =
{
    90, 150, 310, 460, 510, 590, 750, 800, 850, 900, -1
};

static int scene9001_cut_timing[4] =
{
    100, 135, 170, -1
};

static int scene9002_cut_timing[10] =
{
    130, 170, 185, 225, 240, 310, 360, 410, 470, -1
};

static int scene9101_cut_timing[8] =
{
    190, 320, 460, 524, 599, 778, 950, -1
};

static int scene9203_cut_timing[4] =
{
    180, 340, 380, -1
};

static int scene9204_cut_timing[4] =
{
    62, 99, 129, -1
};

static int scene9205_cut_timing[6] =
{
    150, 255, 295, 390, 470, -1
};

static int scene9206_cut_timing[16] =
{
    20, 106, 136, 180, 240, 330, 390, 490, 535, 710, 850, 940, 970, 1240, 1260, -1
};

static int scene9302_cut_timing[9] =
{
    120, 150, 210, 320, 380, 430, 450, 500, -1
};

static int scene9303_cut_timing[10] =
{
    60, 95, 180, 200, 230, 360, 400, 480, 530, -1
};

static int scene9501_cut_timing[20] =
{
    90, 130, 260, 360, 420, 584, 630, 740, 820, 990, 1100, 1140, 1210, 1290, 1440, 1600, 1852,
    1880, 1960, -1
};

int *scene_cut_timing[71] =
{
    /*  0 */ sceneDummy_cut_timing,
    /*  1 */ sceneDummy_cut_timing,
    /*  2 */ scene0110_cut_timing,
    /*  3 */ scene0120_cut_timing,
    /*  4 */ sceneDummy_cut_timing,
    /*  5 */ scene0122_cut_timing,
    /*  6 */ scene0130_cut_timing,
    /*  7 */ scene0132_cut_timing,
    /*  8 */ scene0133_cut_timing,
    /*  9 */ scene0140_cut_timing,
    /* 10 */ sceneDummy_cut_timing,
    /* 11 */ scene0150_cut_timing,
    /* 12 */ scene0160_cut_timing,
    /* 13 */ scene0170_cut_timing,
    /* 14 */ sceneDummy_cut_timing,
    /* 15 */ scene0190_cut_timing,
    /* 16 */ sceneDummy_cut_timing,
    /* 17 */ scene0210_cut_timing,
    /* 18 */ sceneDummy_cut_timing,
    /* 19 */ sceneDummy_cut_timing,
    /* 20 */ scene0231_cut_timing,
    /* 21 */ scene0240_cut_timing,
    /* 22 */ sceneDummy_cut_timing,
    /* 23 */ sceneDummy_cut_timing,
    /* 24 */ scene0340_cut_timing,
    /* 25 */ scene0350_cut_timing,
    /* 26 */ sceneDummy_cut_timing,
    /* 27 */ scene0352_cut_timing,
    /* 28 */ scene0410_cut_timing,
    /* 29 */ scene0510_cut_timing,
    /* 30 */ scene0520_cut_timing,
    /* 31 */ scene0610_cut_timing,
    /* 32 */ sceneDummy_cut_timing,
    /* 33 */ scene0620_cut_timing,
    /* 34 */ sceneDummy_cut_timing,
    /* 35 */ sceneDummy_cut_timing,
    /* 36 */ sceneDummy_cut_timing,
    /* 37 */ sceneDummy_cut_timing,
    /* 38 */ scene0720_cut_timing,
    /* 39 */ scene0721_cut_timing,
    /* 40 */ scene0730_cut_timing,
    /* 41 */ sceneDummy_cut_timing,
    /* 42 */ sceneDummy_cut_timing,
    /* 43 */ sceneDummy_cut_timing,
    /* 44 */ scene0820_cut_timing,
    /* 45 */ sceneDummy_cut_timing,
    /* 46 */ sceneDummy_cut_timing,
    /* 47 */ sceneDummy_cut_timing,
    /* 48 */ sceneDummy_cut_timing,
    /* 49 */ sceneDummy_cut_timing,
    /* 50 */ scene1010_cut_timing,
    /* 51 */ sceneDummy_cut_timing,
    /* 52 */ sceneDummy_cut_timing,
    /* 53 */ sceneDummy_cut_timing,
    /* 54 */ scene9001_cut_timing,
    /* 55 */ scene9002_cut_timing,
    /* 56 */ sceneDummy_cut_timing,
    /* 57 */ scene9101_cut_timing,
    /* 58 */ scene9203_cut_timing,
    /* 59 */ scene9204_cut_timing,
    /* 60 */ scene9205_cut_timing,
    /* 61 */ scene9206_cut_timing,
    /* 62 */ scene9302_cut_timing,
    /* 63 */ scene9303_cut_timing,
    /* 64 */ scene9501_cut_timing,
    /* 65 */ sceneDummy_cut_timing,
    /* 66 */ sceneDummy_cut_timing,
    /* 67 */ sceneDummy_cut_timing,
    /* 68 */ sceneDummy_cut_timing,
    /* 69 */ sceneDummy_cut_timing,
    /* 70 */ sceneDummy_cut_timing,
};

/* ──────────────────────────────────────────────────────────────────────
 * scene_data_cmn[]  (data 343210) -- per-scene movie volume, as a percentage.
 *
 * InitMovieWithTitle() passes it to InitMovie(), which scales the BGM group
 * volume by it -- so this trims one cutscene against the others without
 * touching what the player set on the option screen.  The struct really is
 * one byte wide: the whole table is 72 bytes.
 *
 * Extracted verbatim from the ROM. */
SCENE_DATA_CMN scene_data_cmn[SCENE_DATA_CMN_MAX] =
{
    /*  0 */ {  83 }, {  80 }, { 100 }, { 100 }, {  67 }, { 100 }, { 100 }, { 100 },
    /*  8 */ { 100 }, { 100 }, {  52 }, { 100 }, { 100 }, { 100 }, {  71 }, { 100 },
    /* 16 */ {  60 }, { 100 }, {  75 }, {  72 }, { 100 }, { 100 }, {  90 }, {  63 },
    /* 24 */ { 100 }, { 100 }, {  63 }, { 100 }, { 100 }, { 100 }, { 100 }, { 100 },
    /* 32 */ {  85 }, { 100 }, {  80 }, {  80 }, {  80 }, {  80 }, { 100 }, { 100 },
    /* 40 */ { 100 }, {  80 }, {  85 }, {  90 }, { 100 }, { 100 }, { 100 }, {  95 },
    /* 48 */ {  90 }, { 100 }, { 100 }, {  95 }, {  95 }, { 100 }, { 100 }, { 100 },
    /* 56 */ {  70 }, { 100 }, { 100 }, { 100 }, { 100 }, { 100 }, { 100 }, { 100 },
    /* 64 */ { 100 }, {  70 }, {  60 }, { 100 }, {  85 }, {  65 }, {  65 }, { 100 },
};
