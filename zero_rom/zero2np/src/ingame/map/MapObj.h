/* ==========================================================================
 *  ingame/map/MapObj.h
 *
 *  Map object controller.  Every MapObj.o symbol in ZERO2.MAP is implemented
 *  in MapObj.c; the four statics (MapObjSetEffect, MapObjDeleteEffect,
 *  MapObjUpdateEffectDraw, MapObjUpdateAnim) and the record-field accessors
 *  are not exported.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPOBJ_H
#define _INGAME_MAP_MAPOBJ_H

#include "../../graphics/graph3d/gra3dLightData.h"
#include "MapObjReg.h"                  /* MAPOBJ_DAT */

/* The effect an "eff_*" placeholder record stands for.  MapObjCheckEffect()
 * maps the model name onto one of these; MapObjSetEffect() dispatches on it. */
typedef enum
{
    MAP_OBJ_EFFECT_ITEM       = 0,
    MAP_OBJ_EFFECT_TORCH0     = 1,
    MAP_OBJ_EFFECT_TORCH1     = 2,
    MAP_OBJ_EFFECT_TORCH2     = 3,
    MAP_OBJ_EFFECT_TORCH3     = 4,
    MAP_OBJ_EFFECT_TORCH4     = 5,
    MAP_OBJ_EFFECT_TORCH5     = 6,
    MAP_OBJ_EFFECT_TORCH6     = 7,
    MAP_OBJ_EFFECT_BUTTERFLY0 = 8,
    MAP_OBJ_EFFECT_CANDLE     = 9,
    MAP_OBJ_EFFECT_RAIN       = 10,
    MAP_OBJ_EFFECT_HIKARI     = 11,
    MAP_OBJ_EFFECT_MOYA       = 12,
    MAP_OBJ_EFFECT_OCHIBA     = 13,
    MAP_OBJ_EFFECT_MIZU       = 14,
    MAP_OBJ_EFFECT_KAWA       = 15,
    MAP_OBJ_EFFECT_TOUROU     = 16,
    MAP_OBJ_EFFECT_CANDLE2    = 17,
    MAP_OBJ_EFFECT_SIMI       = 18,
    MAP_OBJ_EFFECT_TORCH7     = 19,
    MAP_OBJ_EFFECT_MAX        = 20
} MAP_OBJ_EFFECT_ENUM;

/* One row of the name -> effect-id table.  The offsets are the ROM's; on this
 * host the pointer is 8 bytes and the struct 0x10, which is harmless because
 * the table is built by the compiler rather than read off the disc. */
typedef struct                          /* 0x8 */
{
    /* 0x0 */ char *name;
    /* 0x4 */ int   id;
} MAPOBJ_EFF;

/* One cloth wind preset: swing strength and period in frames. */
typedef struct                          /* 0x8 */
{
    /* 0x0 */ float pow;
    /* 0x4 */ int   cycle;
} MAPOBJ_WIND;

/* Semi-transparency ramp used when an "eff_simi" object fades: SimiEnd is the
 * target alpha, SimiTime the number of frames.  Both are on the ROOM debug
 * menu (MhCtl.c). */
extern int MapObjSimiEnd;           /* sdata 3eef20 */
extern int MapObjSimiTime;          /* sdata 3eef24 */

void MapObjInit(void);
void MapObjProc(void);

/* Suppresses / restores the item-glint effect (MapObjFlg bit 0). */
void MapObjItemOff(void);
void MapObjItemOn(void);

/* Clears the per-frame "model requested" marks on both buffers' draw lists. */
void MapObjDrawON(void);

/* The draw entry of type `type` whose model-name digits are `id`, or NULL. */
MAPOBJ_DAT *MapObjGetDat(int type, int id);

GRA3DLIGHTDATA *MapObjGetLight(int type, int id);

/* The object's model, marking it as drawn by the caller rather than by the
 * ordinary object pass. */
int *MapObjGetModelAddr(int type, int id);

/* The model name of a draw entry, read back off its registration record. */
char *MapObjGetModelName(MAPOBJ_DAT *dp);

/* Effect id for a model name, or -1 when the name is an ordinary model and
 * -2 when it is an unknown "eff_" name. */
int  MapObjCheckEffect(char *name);

/* Starts (sw != 0) or stops the record's effect. */
void MapObjSetDrawEffect(MAPOBJ_DAT *dp, int sw);

/* Applies the record's Visible value to the put-object's draw mode. */
void MapObjUpdateFlg(void *hdl, int v_flg);

/* Per-attribute animation control for furniture: cloth (attr 1) and rope
 * (attr 5). */
void MapObjNunoCtl(void *pHdl, int iAction, int iActionType);
int  MapObjBoneCtl(int iFurnID, int action, int a_type);

/* MapPut draw callback for item-effect objects. */
void MapObjEffCallback(void);

#endif /* _INGAME_MAP_MAPOBJ_H */
