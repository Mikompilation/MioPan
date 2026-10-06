/* ==========================================================================
 *  ingame/menu/ghost_seal_door.h
 *
 *  Ghost-sealed doors -- the nine doors the story locks behind a spirit seal.
 *
 *  A seal is one state byte per door, held in ghost_seal_door.c and handed to
 *  the save system whole.  It only ever moves forward:
 *
 *      NONE --Appear--> SET --Release--> RELEASE --Main--> END
 *
 *  The event script drives the first two edges (SET_GHOST_SEAL_DOOR and
 *  RELEASE_GHOST_SEAL_DOOR in ingame/event/prg/ev_macro.c).  The last is
 *  driven by the player: GhostSealDoorMain() runs every frame with the room
 *  they are standing in, and retires a released seal once they reach that
 *  seal's end_room_label.
 *
 *  menu_map.c is the only reader.  MenuMapGhostSealNormalDoorDisp() and its
 *  double-door twin draw an ordinary door for NONE and END, the sealed door
 *  art for SET and RELEASE, and add the seal marker for SET alone -- so the
 *  marker tracks "sealed right now", while the special art lingers until the
 *  player has actually been through to the far side.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), ghost_seal_door.o.
 * ======================================================================== */

#ifndef _INGAME_MENU_GHOST_SEAL_DOOR_H
#define _INGAME_MENU_GHOST_SEAL_DOOR_H

#include "eetypes.h"

#include "../../common/save_data.h"             /* MC_SAVE_DATA */
#include "tim_dat/ghost_seal_door_dat.h"        /* GHOST_SEAL_DOOR_MAX_NUM */

/* Seal states.  The ROM writes the four values as bare literals, so these
 * names are the port's; the transitions they are used in are the ROM's. */
#define GHOST_SEAL_DOOR_NONE    0       /* never sealed, or already retired  */
#define GHOST_SEAL_DOOR_SET     1       /* sealed; marker shown on the map   */
#define GHOST_SEAL_DOOR_RELEASE 2       /* seal broken, far side not reached */
#define GHOST_SEAL_DOOR_END     3       /* player reached end_room_label     */

/* Clears every seal.  Called once from IngameInit() (ingame.c). */
void GhostSealDoorInit(void);

/* Driven every frame from RoomInCheckMain() (plyr_room_info.c) with the
 * player's current room label, -1 included -- no seal uses -1 as its end
 * room, so the miss is harmless.  Advances RELEASE -> END. */
void GhostSealDoorMain(int room_label);

/* The two event-script edges: NONE -> SET and SET -> RELEASE.  Both ignore
 * the call if the seal is not in the expected state. */
void GhostSealDoorAppear(int seal_label);
void GhostSealDoorRelease(int seal_label);

int  GetGhostSealDoorState(int seal_label);

void SetSave_GhostSealDoor(MC_SAVE_DATA *data);

#endif /* _INGAME_MENU_GHOST_SEAL_DOOR_H */
