/* ==========================================================================
 *  ingame/map/MapDoor.h
 *
 *  Map door controller.  Every MapDoor.o symbol in ZERO2.MAP is implemented in
 *  MapDoor.c; the five statics (MapDoorCheckID, MapDoorGetFreeSpace,
 *  MapDoorSetHitFlg, MapDoorAnimGetID4TypeLabel, MapDoorAnimSetPos,
 *  MapDoorAnim, MapDoorCalcRotY) are not exported.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPDOOR_H
#define _INGAME_MAP_MAPDOOR_H

#include <stdint.h>

#include "MapLoad.h"
#include "RegDat.h"                      /* MDAT_DOOR */
#include "../../graphics/graph3d/gra3dTypes.h"
#include "../../graphics/graph3d/sgd_types.h"    /* SGDFILEHEADER */
#include "../../graphics/graph3d/ctl/fixed_array.h"

/* One resident door.
 *
 * A doorway belongs to two rooms, so both the buffer id and the registration
 * record come in pairs: slot 0 is the buffer that registered it first, slot 1
 * the neighbour that later found the same DatID already present.  There is a
 * single MapPut handle -- the door is drawn once, whichever room owns it.
 *
 * The offsets are the ROM's; the host's pointers are 8 bytes, so the real
 * layout is wider. */
typedef struct                          /* 0x14 */
{
    /* 0x00 */ fixed_array<int, 2>        buff_id;
    /* 0x08 */ fixed_array<MDAT_DOOR *, 2> dat;
    /* 0x10 */ void                      *hdl;
} MAPDOOR_HEAD;

/* ---- registry ---------------------------------------------------------- */

void MapDoorInit(void);
void MapDoorRelease(void);

MLOAD_DOOR_DAT *MapDoorGetDatListPtr(int id);

/* Adds `op` to the registry and returns its entry, 0 when the door was already
 * registered by the other room buffer or the registry is full.
 *
 * The ROM's return type is u_int -- a 4-byte MAPDOOR_HEAD * handed out as an
 * integer.  Widened here so it survives on a 64-bit host; it is still opaque
 * and still only ever goes straight back into MapDoorSetStat(). */
uintptr_t MapDoorAdd(int buff_id, MDAT_DOOR *op);

/* Binds the door's draw handle once it is in the object draw list. */
void  MapDoorSetStat(uintptr_t ptr, void *hdl);

/* Releases the room buffer's doors.  Runs first in MapDrawDeleteNoDraw()
 * because MapObjDeletDraw() afterwards hands a door being walked through to
 * the other buffer, and needs the door list settled by then. */
void  MapDoorDeleteBuff(int buff_id);

int   MapDoorGetHitFlg(int door_id);
void *MapDoorGetHdl(int door_id);
int   MapDoorGetModelSize(char *pModelName);

/* ---- lighting ---------------------------------------------------------- */

/* Builds a door's light into `out` by blending the two rooms it joins.  Not
 * cached anywhere, so MapObjGetLight() rebuilds it on every call. */
void  MapDoorMakeDualRoomLight(GRA3DLIGHTDATA *out, float *vPos, void *mdl_p);
GRA3DLIGHTDATA *MapDoorGetLight(void);
void  MapDoorAllPreRender(int room_no);

/* ---- transition -------------------------------------------------------- */

uintptr_t MapDoorAnimInit(uintptr_t *anim_addr);
void  MapDoorLoadReq(char *name);
int   MapDoorCheckLoad(void);

/* Latches the door the player triggered; MapDoorAnimOpen() then starts the
 * motion and MapDoorAnimClose() puts everything back. */
void  MapDoorSetAnimID(int door_type);
int   MapDoorAnimOpen(void);
int   MapDoorAnimClose(int door_id);
int   MapDoorAnimCheckPlay(void);
int   DoorSEIsReady(void);

/* MapAnim / MapPut callbacks installed for the duration of the transition. */
int   MapDoorAnimCallback(int id, float (*mat)[4], void *dat, void *dat2);
void  MapDoorPutCallback(void);
void  MapDoorCopyMatrxSgd(SGDFILEHEADER *pOut, SGDFILEHEADER *pMst);

#endif /* _INGAME_MAP_MAPDOOR_H */
