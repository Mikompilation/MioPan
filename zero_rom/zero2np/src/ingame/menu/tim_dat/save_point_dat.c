// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/save_point_dat.c
//
// The save points, as the map page draws them (save_point_dat.o, data
// 0x33ec18).
//
// Twelve entries plus the all -1 terminator MenuMapSavePointDisp() stops on.
// A mark is drawn only once GetRoomInfo(room_label) says the room has been
// visited, so the lanterns appear on the map as the player finds them.
//
// This is the map page's own table and is unrelated to ingame/savepoint/ --
// that folder owns the save screen, and it keys off the event script rather
// than off these coordinates.
//
// Read straight out of the ROM's .data and diffed against it byte-for-byte.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "save_point_dat.h"

MAP_SAVE_POINT map_save_point[13] =                         /* data 33ec18 */
{
    /*  0 */ {     311.0f,     470.0f,   0,   11 },
    /*  1 */ {     556.0f,     481.0f,   0,    8 },
    /*  2 */ {     267.0f,     626.0f,   0,    0 },
    /*  3 */ {     201.0f,      42.0f,   1,   47 },
    /*  4 */ {     147.0f,     576.0f,   4,   66 },
    /*  5 */ {      94.0f,     331.0f,   5,  100 },
    /*  6 */ {     330.0f,     139.0f,   4,   92 },
    /*  7 */ {     291.0f,     460.0f,   6,  117 },
    /*  8 */ {      32.0f,      95.0f,   7,  147 },
    /*  9 */ {      29.0f,      62.0f,   8,  150 },
    /* 10 */ {     294.0f,      44.0f,  10,  186 },
    /* 11 */ {     201.0f,     186.0f,  10,  192 },
    /* 12 */ {      -1.0f,      -1.0f,  -1,   -1 },
};
