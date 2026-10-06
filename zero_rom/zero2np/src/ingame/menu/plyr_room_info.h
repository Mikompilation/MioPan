/* ==========================================================================
 *  ingame/menu/plyr_room_info.h
 *
 *  Room-in tracker: the "have I been here?" bit behind the in-game map, one
 *  byte per room label.  RoomInCheckMain() sets the bit for whichever room
 *  the player currently stands in, every frame; the map screen reads it back
 *  through GetRoomInfo() to decide which rooms to draw.
 *
 *  The projection helpers live here too, because the room test is done on the
 *  *map sheet*, not in the world: the player's world position is folded down
 *  to map units by ChangeWorldPosToWinPos() and then tested against the room
 *  quads in map_area_dat[].
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), plyr_room_info.o
 *  0x002400e8..0x00240ab7.
 * ======================================================================== */

#ifndef _INGAME_ROOM_IN_H
#define _INGAME_ROOM_IN_H

#include "../../common/save_data.h"     /* MC_SAVE_DATA */

/* Clears every room-in flag.  Called from the new-game path in ingame.c. */
void RoomInInfoInit(void);

/* Per-frame: flags the player's current room (and the house it belongs to)
 * as visited, then advances the ghost-sealed doors. */
void RoomInCheckMain(void);

/* Room label the player currently occupies, or -1 when the position is not
 * inside any registered room quad. */
int GetPlyrRoomLabel(void);

/* Room label containing `pos` on the given floor of the given area, or -1
 * when it is outside every registered room.  player.c's PlyrHeightCtrl()
 * drives the room bookkeeping from it. */
int GetRoomLabel(int area_label, int floor_label, float *pos);

/* World position -> map-sheet position for `map_label`.  `scall` picks the
 * zoom: 0 uses map_scall_dat[].normal, 1 uses .big; any other value leaves
 * both outputs untouched. */
void ChangeWorldPosToWinPos(float *pos_x, float *pos_y, int map_label,
                            float *pos, int scall);

/* Map sheet the given room sits on.  Returns the terminator's map_label
 * (-1) when the room is not registered. */
int GetMapLabelToRoomLabel(int room_label);

/* The room's visited flag: non-zero once the player has been inside. */
int GetRoomInfo(int room_label);

/* Publishes the whole flag array to the save system. */
void SetSave_RoomInInfo(MC_SAVE_DATA *data);

/* Debug: mark every room visited, revealing all maps at once. */
void DebugAllMapDisp(void);

#endif /* _INGAME_ROOM_IN_H */
