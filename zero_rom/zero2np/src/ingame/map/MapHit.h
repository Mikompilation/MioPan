/* ==========================================================================
 *  ingame/map/MapHit.h
 *
 *  Dynamic hit registration -- the rectangles that are not part of the map's
 *  static wall set (shut doors, pushed furniture), consulted by
 *  map_hit_check.c on top of the registration rectangles.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPHIT_H
#define _INGAME_MAP_MAPHIT_H

#include "eetypes.h"

enum
{
    MAPHIT_REC_NUM      = 32,
    MAPHIT_CALLBACK_NUM = 3
};

/* One registration.  `id` names the owner and doubles as the free marker
 * (negative = empty slot); `id / 1000` is its group.  Bit 0 of `flg` disables
 * the registration without releasing the slot.  `rec` points at the four
 * vertices -- it is not a copy, so an owner that moves its geometry updates
 * the rectangle in place. */
typedef struct MAPHIT_HEAD              /* 0x14 */
{
    /* 0x00 */ int    id;
    /* 0x04 */ int    flg;
    /* 0x08 */ int    stat;
    /* 0x0c */ int    key;
    /* 0x10 */ float *rec;
} MAPHIT_HEAD;

/* The query being resolved, handed to every callback. */
typedef struct MAPHIT_DAT               /* 0x0c */
{
    /* 0x00 */ float (*pos)[4];
    /* 0x04 */ float r;
    /* 0x08 */ int   call_work;
} MAPHIT_DAT;

/* Slot 0 runs once before the walk, 1 once per hit, 2 once after.  The per-hit
 * return value controls the walk: 0 stops at this hit, non-zero carries on. */
typedef int (*MAPHIT_CALLBACK)(MAPHIT_HEAD *hp, MAPHIT_DAT *dp, int type);

/* Z-depth a door's hit registration extends to.  Tunable from the ROOM debug
 * menu (MhCtl.c) as DOOR_HIT_Z. */
extern float MapHitDoorZ;           /* sdata 3eedf8 */

void MapHitInit(void);
void MapHitTerm(void);

/* Registration.  MapHitRegistRec() returns 0, or -1 when the 32-slot table is
 * full.  Note it does not clear `flg` -- a recycled slot inherits the previous
 * owner's flags. */
int  MapHitRegistRec(int id, int stat, int key, float *rec);
void MapHitDeleteOne(int id);
void MapHitDeleteAll(void);
void MapHitDeleteGroup(int a_id);

void MapHitSetFlg(int id, int flg);
void MapHitDeleteFlg(int id, int flg);

void MapHitRegistCallback(int type, MAPHIT_CALLBACK func);

/* Returns 1 when `pos` (radius r) collides with a live registration, or -1
 * when none matched -- unless a post-pass callback replaces the result.
 * `key` < 0 accepts any owner; otherwise only matching keys are tested. */
int MapHitCheckCol(float *pos, float r, int call_work, int key);

/* Where the last HcBasePointRectangle() hit landed.  The callbacks read these
 * back rather than being passed them. */
float MapHitGetColLen(void);
float (*MapHitGetColPoint(void))[4];

/* The default per-hit callback installed by MapHitInit(): pushes the position
 * out of the rectangle it is inside. */
int MapTesCallbackHit(MAPHIT_HEAD *hp, MAPHIT_DAT *dp, int type);

/* ---- doors -------------------------------------------------------------
 * One door is tracked at a time.  Its four vertices live in MapHit.c and the
 * registration only points at them, so MapHitSetDoorHit() can rebuild the
 * geometry each frame of the door animation without re-registering. */

void MapHitSetDoorRec(int id, int door_id, int key);
void MapHitDeleteDoorRec(void);
void MapHitSetDoorFlg(int flg);
/* Drops the "a door is blocking" flag once the player has walked through. */
void MapHitDeleteDoorFlg(int flg);
void MapHitRegistDoorVec(int door_id, int vec_id, float *vec);
/* Rebuilds the door's blocking rectangle from `mat`.  `type` 1 shifts it back
 * by the door's width, 2 forward, anything else leaves it on the hinge. */
void MapHitSetDoorHit(int id, int type, float (*mat)[4]);
void MapHitSetDoorZ(float num);

#endif /* _INGAME_MAP_MAPHIT_H */
