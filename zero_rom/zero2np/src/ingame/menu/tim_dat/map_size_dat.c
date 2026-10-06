// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/map_size_dat.c
//
// Per-map (17 maps) geometry for the in-game map screen.
//
//   map_world_point[] - world origin of the map sheet, in map units.  It is
//                       the offset added after the world position has been
//                       divided down by the map's world-to-map divisor
//                       (100 for maps 0 and 13, 50 for the rest).
//   map_size_dat[]    - the sheet's width/height in map units.
//   map_scall_dat[]   - the two zoom factors the map is drawn at; `normal`
//                       is also what RoomInfoSetPosition() scales the room
//                       quads by, so hit testing happens in drawn units.
//
// Extracted verbatim from the Feb 6 2004 prototype (SLES_523.84), .data 31e210.

#include "map_size_dat.h"

// Fields: { x, y }
MAP_WORLD_POINT map_world_point[17] =
{
    /* [ 0] */ {   241.0f,   696.0f },
    /* [ 1] */ {     0.0f,   142.0f },
    /* [ 2] */ {     0.0f,   142.0f },
    /* [ 3] */ {   108.0f,  -130.0f },
    /* [ 4] */ {     2.0f,   524.0f },
    /* [ 5] */ {     2.0f,   524.0f },
    /* [ 6] */ {     2.0f,   524.0f },
    /* [ 7] */ {     9.0f,   310.0f },
    /* [ 8] */ {     9.0f,   308.0f },
    /* [ 9] */ {   -96.0f,    78.0f },
    /* [10] */ {  -136.0f,   261.0f },
    /* [11] */ {  -136.0f,   263.0f },
    /* [12] */ {     0.0f,     0.0f },
    /* [13] */ {   137.0f,   589.0f },
    /* [14] */ {   -20.0f,    81.0f },
    /* [15] */ {     1.0f,   150.0f },
    /* [16] */ {    -2.0f,    91.0f },
};

// Fields: { w, h }
MAP_SIZE_DAT map_size_dat[17] =
{
    /* [ 0] */ {  568,  698 },
    /* [ 1] */ {  216,  142 },
    /* [ 2] */ {  216,  142 },
    /* [ 3] */ {  154,   81 },
    /* [ 4] */ {  544,  622 },
    /* [ 5] */ {  544,  622 },
    /* [ 6] */ {  544,  622 },
    /* [ 7] */ {  236,  314 },
    /* [ 8] */ {  188,  190 },
    /* [ 9] */ {  262,   76 },
    /* [10] */ {  314,  236 },
    /* [11] */ {  273,  188 },
    /* [12] */ {    0,    0 },
    /* [13] */ {  302, 1033 },
    /* [14] */ {   35,   61 },
    /* [15] */ {   72,  152 },
    /* [16] */ {   84,   86 },
};

// Fields: { normal, big }
MAP_SCALL_DAT map_scall_dat[17] =
{
    /* [ 0] */ {  1.25f,   2.5f },
    /* [ 1] */ {   1.5f,   3.0f },
    /* [ 2] */ {   1.5f,   3.0f },
    /* [ 3] */ {   1.5f,   3.0f },
    /* [ 4] */ {   1.5f,   3.0f },
    /* [ 5] */ {   1.5f,   3.0f },
    /* [ 6] */ {   1.5f,   3.0f },
    /* [ 7] */ {   1.5f,   3.0f },
    /* [ 8] */ {   1.5f,   3.0f },
    /* [ 9] */ {   1.5f,   3.0f },
    /* [10] */ {   1.5f,   3.0f },
    /* [11] */ {   1.5f,   3.0f },
    /* [12] */ {   1.5f,   3.0f },
    /* [13] */ {  1.25f,   2.5f },
    /* [14] */ {   1.5f,   3.0f },
    /* [15] */ {   1.5f,   3.0f },
    /* [16] */ {   1.5f,   3.0f },
};
