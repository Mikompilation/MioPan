// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/ghost_seal_door_dat.c
//
// The nine ghost-sealed doors.
//
//   end_room_label - room whose entry advances the seal from RELEASE to END.
//                    GhostSealDoorMain() polls this every frame against the
//                    player's current room; see ingame/menu/ghost_seal_door.c.
//   pos_x / pos_y  - map-space position of the seal marker.  menu_map.c's
//                    MenuMapGhostSealMarkDisp() scales these by the map's zoom
//                    factor and adds the sheet origin, so the marker sits at a
//                    fixed spot on the sheet rather than on the door sprite --
//                    which is why it carries its own position here instead of
//                    reusing the MAP_DOOR_POINT the door is drawn from.
//
// Entries 3/4 and 5/6 repeat their end room: those are pairs of doors sealed
// together, and both clear on the one room entry.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), .rodata
// 3b4288.  The ROM's copy is const (hence .rodata); it is declared plain here
// to match every other exported table in the port.

#include "ghost_seal_door_dat.h"

// Fields: { end_room_label, pos_x, pos_y }
GHOST_SEAL_DOOR_DATA ghost_seal_door_data[GHOST_SEAL_DOOR_MAX_NUM] =
{
    /* [0] */ {  54,  111.0f,   74.0f },
    /* [1] */ {  53,   58.0f,   59.0f },
    /* [2] */ { 119,  294.0f,  458.0f },
    /* [3] */ { 178,  284.0f,  149.0f },
    /* [4] */ { 178,  225.0f,   92.0f },
    /* [5] */ { 191,  250.0f,   77.0f },
    /* [6] */ { 191,  218.0f,   40.0f },
    /* [7] */ { 187,  219.0f,  148.0f },
    /* [8] */ { 238,  131.0f,  460.0f },
};
