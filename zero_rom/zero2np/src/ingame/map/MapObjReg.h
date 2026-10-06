/* ==========================================================================
 *  ingame/map/MapObjReg.h
 *
 *  Map-object registration: turns a room's registration records into the
 *  per-buffer draw list, wiring each placed object to its model, its lighting,
 *  its animation and its hit rectangles.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPOBJREG_H
#define _INGAME_MAP_MAPOBJREG_H

#include "eetypes.h"
#include "RegDat.h"                     /* MDAT_OBJ / MDAT_DOOR / MB_OUT_SECTION */

enum
{
    MAPOBJ_DAT_NUM  = 300,      /* draw entries per buffer */
    MAPOBJ_LIST_NUM = 2         /* room buffers */
};

/* One entry in a buffer's draw list.  `obj_ptr` is the registration record it
 * came from and doubles as the free marker (NULL = empty).  `stat` is the
 * record type: 3 furniture, 7 door, 11 put-item. */
typedef struct MAPOBJ_DAT           /* 0x1c */
{
    /* 0x00 */ int   stat;
    /* 0x04 */ int   flg;
    /* 0x08 */ int  *mdl_addr;
    /* 0x0c */ int   anim_id;
    /* 0x10 */ void *obj_ptr;
    /* 0x14 */ void *obj_hdl;       /* MapPut handle */
    /* 0x18 */ void *obj_save;      /* MapSave block, stat 3 only */
} MAPOBJ_DAT;

typedef struct MAPOBJ_HEAD          /* 0x20d4 */
{
    /* 0x0000 */ int       *lit_addr;
    /* 0x0004 */ MAPOBJ_DAT dat[MAPOBJ_DAT_NUM];
} MAPOBJ_HEAD;

void MapObjRegInit(void);

/* Suppresses the per-object light setup while a scene is loading -- the scene
 * lights the room itself. */
void MapObjRegSetSceneLoad(int flg);

MAPOBJ_HEAD *MapObjGetListPtr(int id);

/* Walks one registration buffer and registers its doors (type 7), furniture
 * (3) and put-items (11) into the draw list. */
void MapObjRegistRegDatOne(int buff_id, int reg_id);

/* Latches the room's baked light block and resolves every registered model's
 * address.  Must run after the files are loaded and before anything draws. */
int MapObjRegistPhf(int buff_id, char *lit_addr);

/* Tears the whole buffer down: effects, rope work, special objects, then every
 * draw entry.  A door being walked through is handed to the other buffer
 * rather than dropped, which is what keeps it drawn across the transition. */
void MapObjDeletDraw(int buff_id);

/* Raises or drops an object's wall-collision registration.  MapObjSetHit()
 * resolves the record from a label; SetHit2() takes it directly.  SetHitArea()
 * raises it for every solid object in the buffer. */
int MapObjSetHit(int labelID, int hit_sw);
int MapObjSetHit2(MDAT_OBJ *op, int hit_sw);
int MapObjSetHitArea(int reg_id);

/* Non-zero when the model wants the baked light rather than the live one. */
int MapObjGetLightFlg(char *name);

/* The door record for `door_id` in buffer `b_id`, or NULL. */
MDAT_DOOR *MapObjSetDoorDat(int b_id, int door_id);

/* Registers an animation for a placed model.  Returns the animation id, or a
 * negative code -- note the tree path deliberately returns -1 on success,
 * because its instances are tracked by MapManim rather than here. */
int MapObjCheckAnim(int buff_id, char *m_name, float *offset, float *rot,
                    void *obj_hdl, int ani_type);
void MapObjRegistMot(int buff_id, MAPOBJ_DAT *dp, char *name, int action,
                     int a_type, float *irot, float *ipos);

/* Registration entry points, called through the record walk. */
int MapObjRegistDoor(int buff_id, int reg_id, int stat, MB_OUT_SECTION *reg_p);
int MapObjRegistFurn(int buff_id, int reg_id, int stat, MB_OUT_SECTION *reg_p);

/* Draw callback installed on the put-object for rope-animated models. */
void MapObjCallbackBornAnim(void);

/* Releases the rope work every registration in the buffer owns. */
void MapObjBornDelete(int buff_id);

#endif /* _INGAME_MAP_MAPOBJREG_H */
