// FILE: /home/zero_rom/zero2np/src/ingame/map/MapObj.c
//
// Map object controller: the per-frame side of the draw list MapObjReg builds.
//
// MapObjProc() walks both room buffers' 300-entry lists once a frame and, for
// each furniture record, decides three things from its Visible field: whether
// the put-object draws, whether its effect is running, and whether the sister
// should look at it.  Records whose model name starts with "eff_" have no
// geometry at all -- they are placeholders whose whole purpose is to start and
// stop a particle effect, which is what MapObjSetEffect/MapObjDeleteEffect do.
//
// The rest is lookup (MapObjGetDat and the accessors that resolve a field out
// of a record whose type is only known at runtime) and per-attribute animation
// control (MapObjNunoCtl for cloth, MapObjBoneCtl for rope).
//
// A record's Action field doubles as the effect's live state: MapObjSetEffect()
// returns the id it started and the caller stores it back, so `action !=
// eff_id` is the "needs starting" test.  MapObjUpdateAnim() uses the sibling
// ActionType the same way, marking a request consumed by flipping it to its
// complement -- negative for any non-negative value, which is exactly the
// "nothing pending" test at the top of that function.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapObj.o
// 0x0010d190..0x0010e3cf.

#include "MapObj.h"

#include "MapAnim.h"                            /* MapAnim{Get,Set,Delete}Flg */
#include "MapDoor.h"                            /* MapDoorMakeDualRoomLight */
#include "MapGeom.h"                            /* MapGeomDegToRad / MapObjSetPutMatrix */
#include "MapLoad.h"                            /* MapLoadCheckDrawFlg  */
#include "MapObjReg.h"                          /* MapObjGetListPtr     */
#include "MapPut.h"
#include "MapSave.h"                            /* MapSaveCopyDat       */
#include "MapSp.h"                              /* MapSpProc            */
#include "RegDat.h"                             /* MDAT_OBJ / DOOR / PUT */

#include "../ingame.h"                          /* CheckIngameMission   */
#include "../ingame_effect.h"                   /* IgEffectButterfly*   */
#include "../photo/photo.h"                     /* PhotoFlgIsUp         */
#include "../plyr/player.h"                     /* GetPlayerPos         */
#include "../plyr/sis_mdl.h"                    /* SisNeckRegisterTarget */
#include "../plyr/unit_ctl.h"                   /* GetTrgtRotY          */
#include "../../common/utility.h"               /* _SetVector           */
#include "../../common/utility2.h"              /* PRINT_ERROR          */
#include "../../common/variable.h"              /* plyr_wrk             */
#include "../../graphics/effect/effect_obj.h"
#include "../../graphics/effect/effect_oth.h"
#include "../../graphics/effect/effect_rain.h"
#include "../../graphics/effect/effect_rdr.h"
#include "../../graphics/effect/effect_sub.h"
#include "../../graphics/effect/effect_torch.h"
#include "../../graphics/motion/accessory.h"    /* acsChodo* / acsRope* */
#include "../../graphics/obj_draw_ctrl.h"       /* GetObjDrawFLG        */

#include <libvu0.h>
#include <stdio.h>
#include <stdlib.h>                             /* atoi                 */
#include <string.h>

/* Record types that reach the draw list, as MAPOBJ_DAT::stat. */
enum
{
    MAPOBJ_STAT_FURN = 3,
    MAPOBJ_STAT_DOOR = 7,
    MAPOBJ_STAT_PUT  = 11
};

/* MAPOBJ_DAT::flg bit 0 -- "something asked for this model this frame".
 * MapObjDrawON() clears it, MapObjGetModelAddr() sets it. */
#define MAPOBJ_FLG_REQUESTED    1

/* The matching MapPut draw flag.  Its four siblings are the Visible-driven
 * draw modes MapObjUpdateFlg() selects between. */
enum
{
    MAPPUT_FLG_NODRAW    = 0x008,
    MAPPUT_FLG_MODE_3    = 0x040,
    MAPPUT_FLG_MODE_2    = 0x080,
    MAPPUT_FLG_MODE_5    = 0x100,
    MAPPUT_FLG_MODE_4    = 0x200,
    MAPPUT_FLG_DRAW_MASK = MAPPUT_FLG_NODRAW | MAPPUT_FLG_MODE_3 |
                           MAPPUT_FLG_MODE_2 | MAPPUT_FLG_MODE_5 |
                           MAPPUT_FLG_MODE_4
};

/* MapObjFlg bit 0 -- item effects suppressed (MapObjItemOff/On). */
#define MAPOBJ_FLG_ITEM_OFF     1

/* Finder mode; the two Visible modes that key off it invert each other. */
#define MAPOBJ_PLYR_MODE_FINDER 6

/* Model-name prefix that marks an effect placeholder.  sdata 3eef30. */
#define MAPOBJ_EFF_PREFIX       "eff_"

/* Sister look-at tuning, one .lit4 each.  lit4 3ed83c / 3ed840 / 3ed844. */
#define MAPOBJ_LOOK_EYE_SPD     0.12f
#define MAPOBJ_LOOK_HEAD_SPD    0.03f
#define MAPOBJ_LOOK_CHEST_SPD   0.04f

/* Look-at ranges.  lit4 3ed848 / 3ed84c. */
#define MAPOBJ_LOOK_RANGE_OBJ   6250000.0f
#define MAPOBJ_LOOK_RANGE_ITEM  4000000.0f

int MapObjSimiEnd  = 128;                                               /* sdata 3eef20 */
int MapObjSimiTime = 100;                                               /* sdata 3eef24 */

/* Name -> effect id.  Terminated by a null name, which is how
 * MapObjCheckEffect() knows where to stop.  The ids happen to be dense and in
 * order, but the lookup is by string, so it is the table order that matters. */
static const MAPOBJ_EFF MapObjEffLabel[MAP_OBJ_EFFECT_MAX + 1] =        /* data 2c8f18 */
{
    { (char *)"eff_item",        MAP_OBJ_EFFECT_ITEM       },
    { (char *)"eff_torch_0",     MAP_OBJ_EFFECT_TORCH0     },
    { (char *)"eff_torch_1",     MAP_OBJ_EFFECT_TORCH1     },
    { (char *)"eff_torch_2",     MAP_OBJ_EFFECT_TORCH2     },
    { (char *)"eff_torch_3",     MAP_OBJ_EFFECT_TORCH3     },
    { (char *)"eff_torch_4",     MAP_OBJ_EFFECT_TORCH4     },
    { (char *)"eff_torch_5",     MAP_OBJ_EFFECT_TORCH5     },
    { (char *)"eff_torch_6",     MAP_OBJ_EFFECT_TORCH6     },
    { (char *)"eff_butterfly_0", MAP_OBJ_EFFECT_BUTTERFLY0 },
    { (char *)"eff_cdl_fire",    MAP_OBJ_EFFECT_CANDLE     },
    { (char *)"eff_rain",        MAP_OBJ_EFFECT_RAIN       },
    { (char *)"eff_op_hikari",   MAP_OBJ_EFFECT_HIKARI     },
    { (char *)"eff_moya",        MAP_OBJ_EFFECT_MOYA       },
    { (char *)"eff_ha_0",        MAP_OBJ_EFFECT_OCHIBA     },
    { (char *)"eff_mizu",        MAP_OBJ_EFFECT_MIZU       },
    { (char *)"eff_kawa",        MAP_OBJ_EFFECT_KAWA       },
    { (char *)"eff_tourou",      MAP_OBJ_EFFECT_TOUROU     },
    { (char *)"eff_cdl_gather",  MAP_OBJ_EFFECT_CANDLE2    },
    { (char *)"eff_simi",        MAP_OBJ_EFFECT_SIMI       },
    { (char *)"eff_torch_7",     MAP_OBJ_EFFECT_TORCH7     },
    { (char *)0,                 -1                        }
};

static int MapObjFlg;                                                   /* sdata 3eef28 */

/* --------------------------------------------------------------------------
 *  Draw-list lookup
 * ------------------------------------------------------------------------ */

/* Clears the per-frame "requested" mark on every entry in both buffers, along
 * with the matching MapPut no-draw bit.  Anything that still wants to draw
 * itself re-asks through MapObjGetModelAddr() before the frame ends. */
void MapObjDrawON(void)
{                                                                       /* 115 */
    for (int i = 0; i < MAPOBJ_LIST_NUM; i++)                           /* 118 */
    {
        MAPOBJ_HEAD *hp = MapObjGetListPtr(i);                          /* 119 */
        MAPOBJ_DAT  *dp = hp->dat;

        for (int j = 0; j < MAPOBJ_DAT_NUM; j++, dp++)
        {
            if (dp->obj_ptr != (void *)0)                               /* 122 */
            {
                dp->flg &= ~MAPOBJ_FLG_REQUESTED;                       /* 123 */

                if (dp->obj_hdl != (void *)0)                           /* 124 */
                {
                    *(u_int *)MapPutGetFlgPtr(dp->obj_hdl) &= ~MAPPUT_FLG_NODRAW; /* 125 */
                }
            }
        }                                                               /* 126 */
    }
}                                                                       /* 127 */

/* Finds the draw entry of type `type` whose model-name digits are `id`.
 *
 * The digits start one character in -- a model name is a key letter followed
 * by the number -- and atoi() stops at the first non-digit, so the trailing
 * "_p" / "_ev" suffixes do not matter.  Both buffers are searched. */
MAPOBJ_DAT *MapObjGetDat(int type, int id)
{                                                                       /* 133 */
    for (int i = 0; i < MAPOBJ_LIST_NUM; i++)                           /* 136 */
    {
        MAPOBJ_HEAD *hp = MapObjGetListPtr(i);                          /* 137 */
        MAPOBJ_DAT  *dp = hp->dat;

        for (int j = 0; j < MAPOBJ_DAT_NUM; j++, dp++)                  /* 139 */
        {
            const char *name;

            if (dp->obj_ptr == (void *)0 || dp->stat != type)           /* 140, 141 */
            {
                continue;
            }

            switch (dp->stat)                                           /* 143 */
            {
            case MAPOBJ_STAT_DOOR:
                name = ((MDAT_DOOR *)dp->obj_ptr)->ModelName;
                break;
            case MAPOBJ_STAT_FURN:
                name = ((MDAT_OBJ *)dp->obj_ptr)->ModelName;
                break;
            case MAPOBJ_STAT_PUT:
                name = ((MDAT_PUT *)dp->obj_ptr)->ModelName;
                break;
            default:
                continue;
            }

            if (atoi(name + 1) == id)                                   /* 156 */
            {
                return dp;
            }
        }
    }                                                                   /* 160 */

    return (MAPOBJ_DAT *)0;                                             /* 161 */
}                                                                       /* 162 */

/* The light a placed object should be drawn with.  Doors are the exception:
 * one straddles two rooms, so its light is rebuilt on every call from both
 * sides into a single shared scratch block rather than cached per object. */
GRA3DLIGHTDATA *MapObjGetLight(int type, int id)
{                                                                       /* 166 */
    static GRA3DLIGHTDATA lwork;                                        /* bss 40a2b0 */
    MAPOBJ_DAT *dp = MapObjGetDat(type, id);                            /* 168 */
    MDAT_DOOR  *op;
    float       vPos[4];

    if (dp == (MAPOBJ_DAT *)0)                                          /* 171 */
    {
        return (GRA3DLIGHTDATA *)0;
    }

    if (type == MAPOBJ_STAT_DOOR)                                       /* 173 */
    {
        op = (MDAT_DOOR *)dp->obj_ptr;
        _SetVector(vPos, op->Pos[0], op->Pos[1], op->Pos[2], 1.0f);     /* 174, 175 */

        MapDoorMakeDualRoomLight(&lwork, vPos,
                                 MapPutGetModelPtr(dp->obj_hdl));       /* 179, 180 */
        return &lwork;
    }

    return MapPutGetLitPtr(dp->obj_hdl);                                /* 183 */
}                                                                       /* 184 */

/* Hands out the object's model and marks it wanted this frame: bit 0 on the
 * draw entry, and the MapPut no-draw bit so the ordinary object pass leaves it
 * alone -- whoever asked for the address is drawing it themselves. */
int *MapObjGetModelAddr(int type, int id)
{                                                                       /* 192 */
    MAPOBJ_DAT *dp = MapObjGetDat(type, id);

    if (dp == (MAPOBJ_DAT *)0)                                          /* 194 */
    {
        return (int *)0;
    }

    if (dp->obj_hdl != (void *)0)                                       /* 195 */
    {
        *(u_int *)MapPutGetFlgPtr(dp->obj_hdl) |= MAPPUT_FLG_NODRAW;    /* 197 */
    }

    dp->flg |= MAPOBJ_FLG_REQUESTED;                                    /* 198 */

    return dp->mdl_addr;
}                                                                       /* 199 */

/* --------------------------------------------------------------------------
 *  Record field accessors
 *
 *  A draw entry only learns its record's type at runtime, and the three record
 *  layouts do not agree, so every field read goes through one of these.  A
 *  type that has no such field yields NULL, and the callers lean on that.
 * ------------------------------------------------------------------------ */

char *MapObjGetModelName(MAPOBJ_DAT *dp)
{
    if (dp->obj_ptr == (void *)0)
    {
        return (char *)0;
    }

    switch (dp->stat)
    {
    case MAPOBJ_STAT_DOOR: return ((MDAT_DOOR *)dp->obj_ptr)->ModelName;
    case MAPOBJ_STAT_FURN: return ((MDAT_OBJ  *)dp->obj_ptr)->ModelName;
    case MAPOBJ_STAT_PUT:  return ((MDAT_PUT  *)dp->obj_ptr)->ModelName;
    }

    return (char *)0;
}

/* Doors have no Visible / Action / ActionType -- they are opened and closed by
 * MapDoor, not driven from the draw list. */
static int *MapObjGetVisiblePtr(MAPOBJ_DAT *dp)
{
    if (dp->obj_ptr == (void *)0)
    {
        return (int *)0;
    }

    switch (dp->stat)                                                   /* 224 */
    {
    case MAPOBJ_STAT_FURN: return &((MDAT_OBJ *)dp->obj_ptr)->Visible;
    case MAPOBJ_STAT_PUT:  return &((MDAT_PUT *)dp->obj_ptr)->Visible;
    }

    return (int *)0;
}

static int *MapObjGetActionPtr(MAPOBJ_DAT *dp)
{
    if (dp->obj_ptr == (void *)0)
    {
        return (int *)0;
    }

    switch (dp->stat)                                                   /* 228 */
    {
    case MAPOBJ_STAT_FURN: return &((MDAT_OBJ *)dp->obj_ptr)->Action;
    case MAPOBJ_STAT_PUT:  return &((MDAT_PUT *)dp->obj_ptr)->Action;
    }

    return (int *)0;
}

static int *MapObjGetActionTypePtr(MAPOBJ_DAT *dp)
{
    if (dp->obj_ptr == (void *)0)
    {
        return (int *)0;
    }

    switch (dp->stat)                                                   /* 232 */
    {
    case MAPOBJ_STAT_FURN: return &((MDAT_OBJ *)dp->obj_ptr)->ActionType;
    case MAPOBJ_STAT_PUT:  return &((MDAT_PUT *)dp->obj_ptr)->ActionType;
    }

    return (int *)0;
}

static float *MapObjGetPosPtr(MAPOBJ_DAT *dp)
{
    if (dp->obj_ptr == (void *)0)
    {
        return (float *)0;
    }

    switch (dp->stat)                                                   /* 241 */
    {
    case MAPOBJ_STAT_DOOR: return ((MDAT_DOOR *)dp->obj_ptr)->Pos;
    case MAPOBJ_STAT_FURN: return ((MDAT_OBJ  *)dp->obj_ptr)->Pos;
    case MAPOBJ_STAT_PUT:  return ((MDAT_PUT  *)dp->obj_ptr)->Pos;
    }

    return (float *)0;
}

static float *MapObjGetRotPtr(MAPOBJ_DAT *dp)
{
    if (dp->obj_ptr == (void *)0)
    {
        return (float *)0;
    }

    switch (dp->stat)                                                   /* 245 */
    {
    case MAPOBJ_STAT_DOOR: return ((MDAT_DOOR *)dp->obj_ptr)->Rot;
    case MAPOBJ_STAT_FURN: return ((MDAT_OBJ  *)dp->obj_ptr)->Rot;
    case MAPOBJ_STAT_PUT:  return ((MDAT_PUT  *)dp->obj_ptr)->Rot;
    }

    return (float *)0;
}

/* --------------------------------------------------------------------------
 *  Effect placeholders
 * ------------------------------------------------------------------------ */

/* -1 for an ordinary model, the effect id for a known "eff_" name, and -2 for
 * an "eff_" name that is not in the table -- a content error, hence the
 * report.  Note the inverted sense of the -1: it is the value that means "this
 * is a normal model", which is the path that actually draws geometry. */
int MapObjCheckEffect(char *name)
{                                                                       /* 251 */
    if (strncmp(name, MAPOBJ_EFF_PREFIX, 4) != 0)                       /* 255 */
    {
        return -1;
    }

    for (int i = 0; MapObjEffLabel[i].name != (char *)0; i++)           /* 258 */
    {
        if (strcmp(MapObjEffLabel[i].name, name) == 0)
        {
            return MapObjEffLabel[i].id;                                /* 261 */
        }
    }                                                                   /* 265 */

    PRINT_ERROR("NO_EFF_ID[%s]\n", name);                               /* 267 */

    return -2;                                                          /* 268 */
}                                                                       /* 269 */

/* Starts effect `eff_id` for the record labelled `label_id`.
 *
 * `action` is the effect already running on this record, so the
 * `action != eff_id` guard is what stops a still-burning torch being
 * re-requested every frame.  The return value is the new state the caller
 * writes back into the record's Action field.
 *
 * Three ids (MIZU / KAWA / TOUROU) fall through to the default and report -1:
 * those are model-backed effects, registered once by MapObjRegistEffect()
 * rather than driven from here. */
static int MapObjSetEffect(int label_id, int eff_id, int action,
                           float *rpos, float *rrot)
{                                                                       /* 274 */
    switch (eff_id)                                                     /* 275 */
    {
    case MAP_OBJ_EFFECT_ITEM:
        if ((MapObjFlg & MAPOBJ_FLG_ITEM_OFF) != 0)                     /* 279 */
        {
            /* Suppressed: report the state unchanged, so the next frame tries
             * again instead of believing the effect is already up. */
            return action;
        }
        if (action != eff_id)                                           /* 280 */
        {
            ItemEffectReq(rpos, label_id, 1);                           /* 281 */
        }
        break;                                                          /* 282 */

    case MAP_OBJ_EFFECT_CANDLE:
        if (action != eff_id)                                           /* 286 */
        {
            SetRDLongFire2(rpos, 0, 1.0f, 1.0f, 0.0f, 0.0f,
                           1.0f, 1.0f, 1.0f, -1.0f, label_id);          /* 290 */
        }
        break;                                                          /* 291 */

    case MAP_OBJ_EFFECT_RAIN:
        if (action != eff_id)                                           /* 295 */
        {
            EffectRainReq(rpos);                                        /* 296 */
            EffectSprayReq(rpos);                                       /* 297 */
            EffectDropOfWaterReq(rpos);                                 /* 298 */
        }
        break;                                                          /* 299 */

    case MAP_OBJ_EFFECT_TORCH0:
    case MAP_OBJ_EFFECT_TORCH1:
    case MAP_OBJ_EFFECT_TORCH2:
    case MAP_OBJ_EFFECT_TORCH3:
    case MAP_OBJ_EFFECT_TORCH4:
    case MAP_OBJ_EFFECT_TORCH5:
        if (action != eff_id)                                           /* 308 */
        {
            SetRDPFire(rpos, label_id, eff_id - 1);                     /* 310 */
        }
        break;                                                          /* 311 */

    case MAP_OBJ_EFFECT_TORCH7:
        if (action != eff_id)                                           /* 314 */
        {
            SetRDPFire(rpos, label_id, 8);                              /* 315 */
        }
        break;                                                          /* 316 */

    case MAP_OBJ_EFFECT_TORCH6:
        if (action != eff_id)                                           /* 320 */
        {
            SetRDPFireMove(rpos, rrot, label_id);                       /* 321 */
        }
        break;                                                          /* 322 */

    case MAP_OBJ_EFFECT_BUTTERFLY0:
        /* No `action` guard here, unlike every other case. */
        IgEffectButterflyReq(rpos);                                     /* 326 */
        break;                                                          /* 327 */

    case MAP_OBJ_EFFECT_HIKARI:
        if (action != eff_id)                                           /* 331 */
        {
            EffectTorch2BigFreaReq(rpos);                               /* 332 */
        }
        break;                                                          /* 333 */

    case MAP_OBJ_EFFECT_MOYA:
        if (action != eff_id)                                           /* 337 */
        {
            EffectHazeReqId(rpos, label_id);                            /* 338 */
        }
        break;                                                          /* 339 */

    case MAP_OBJ_EFFECT_OCHIBA:
        if (action != eff_id)                                           /* 343 */
        {
            EffectLeavesFallReq(rpos, label_id);                        /* 344 */
        }
        break;                                                          /* 345 */

    case MAP_OBJ_EFFECT_CANDLE2:
        if (action != eff_id)                                           /* 349 */
        {
            EffectManyCandleLoadReq(rpos);                              /* 350 */
        }
        break;                                                          /* 351 */

    case MAP_OBJ_EFFECT_SIMI:
        if (action != eff_id)                                           /* 355 */
        {
            EffectModelAlphaChangeReq(label_id, 0,
                                      MapObjSimiEnd, MapObjSimiTime);   /* 357 */
        }
        break;                                                          /* 358 */

    default:
        return -1;                                                      /* 361 */
    }

    return eff_id;
}                                                                       /* 365 */

/* Stops whatever `a_type` started.  `action` is the live state; -1 means
 * nothing is running, so there is nothing to cut. */
static int MapObjDeleteEffect(int label_id, int a_type, int action)
{                                                                       /* 369 */
    if (action == -1)                                                   /* 371 */
    {
        return -1;
    }

    switch ((u_int)a_type)                                              /* 373 */
    {
    case MAP_OBJ_EFFECT_ITEM:
        ItemEffectCut(label_id);                                        /* 377 */
        break;                                                          /* 378 */

    case MAP_OBJ_EFFECT_CANDLE:
        ResetRDLongFire(label_id);                                      /* 382 */
        break;                                                          /* 383 */

    case MAP_OBJ_EFFECT_RAIN:
        EffectRainCut();                                                /* 387 */
        EffectSprayAllCut();                                            /* 388 */
        EffectDropOfWaterCut();                                         /* 389 */
        break;                                                          /* 390 */

    case MAP_OBJ_EFFECT_TORCH0:
    case MAP_OBJ_EFFECT_TORCH1:
    case MAP_OBJ_EFFECT_TORCH2:
    case MAP_OBJ_EFFECT_TORCH3:
    case MAP_OBJ_EFFECT_TORCH4:
    case MAP_OBJ_EFFECT_TORCH5:
    case MAP_OBJ_EFFECT_TORCH6:
    case MAP_OBJ_EFFECT_TORCH7:
        ResetRDPFire(label_id);                                         /* 401 */
        break;                                                          /* 402 */

    case MAP_OBJ_EFFECT_BUTTERFLY0:
        IgEffectButterflyAllCut();                                      /* 406 */
        break;                                                          /* 407 */

    case MAP_OBJ_EFFECT_HIKARI:
        EffectTorch2BigFreaCut();                                       /* 411 */
        break;                                                          /* 412 */

    case MAP_OBJ_EFFECT_MOYA:
        EffectHazeCutId(label_id);                                      /* 416 */
        break;                                                          /* 417 */

    case MAP_OBJ_EFFECT_OCHIBA:
        EffectLeavesFallCut(label_id);                                  /* 421 */
        break;                                                          /* 422 */

    case MAP_OBJ_EFFECT_CANDLE2:
        EffectManyCandleLoadCut();                                      /* 426 */
        break;                                                          /* 427 */

    case MAP_OBJ_EFFECT_SIMI:
        /* Ramp the other way -- the request form fades 0 -> SimiEnd. */
        EffectModelAlphaChangeReq(label_id, MapObjSimiEnd,
                                  0, MapObjSimiTime);                   /* 432 */
        break;
    }

    return -1;                                                          /* 440 */
}                                                                       /* 441 */

/* Starts (`sw` non-zero) or stops the record's effect, writing the new state
 * back into its Action field. */
void MapObjSetDrawEffect(MAPOBJ_DAT *dp, int sw)
{                                                                       /* 445 */
    MDAT_OBJ *op       = (MDAT_OBJ *)dp->obj_ptr;
    int      *action_p = MapObjGetActionPtr(dp);                        /* 228 */
    int      *atype_p  = MapObjGetActionTypePtr(dp);                    /* 232 */

    if (sw == 0)                                                        /* 450 */
    {
        *action_p = MapObjDeleteEffect(op->head.labelID,
                                       *atype_p, *action_p);            /* 458 */
        return;
    }

    {
        const float *pos_p = MapObjGetPosPtr(dp);                       /* 241 */
        const float *rot_p = MapObjGetRotPtr(dp);                       /* 245 */
        float        rpos[4];
        float        rrot[4];

        rpos[0] = pos_p[0];                                             /* 453 */
        rpos[1] = pos_p[1];
        rpos[2] = pos_p[2];
        rpos[3] = 1.0f;

        rrot[0] = rot_p[0];                                             /* 454 */
        rrot[1] = rot_p[1];
        rrot[2] = rot_p[2];
        rrot[3] = 0.0f;

        *action_p = MapObjSetEffect(op->head.labelID, *atype_p,
                                    *action_p, rpos, rrot);             /* 456 */
    }
}

/* Turns a record's Visible field into "should the effect be running", then
 * applies it.  Modes 2 and 3 are finder-only and finder-off; 4 and 5 are the
 * same pair for the camera being raised. */
static int MapObjUpdateEffectDraw(int v_flg, MAPOBJ_DAT *dp)
{                                                                       /* 464 */
    int sw = 0;                                                         /* 465 */

    switch (v_flg)                                                      /* 467 */
    {
    case 1:
        sw = 1;
        break;

    case 2:
        if (plyr_wrk.cmn_wrk.mode == MAPOBJ_PLYR_MODE_FINDER)           /* 477 */
        {
            sw = 1;
        }
        break;                                                          /* 478 */

    case 3:
        if (plyr_wrk.cmn_wrk.mode != MAPOBJ_PLYR_MODE_FINDER)           /* 481 */
        {
            sw = 1;                                                     /* 482 */
        }
        break;

    case 4:
        if (PhotoFlgIsUp() != 0)                                        /* 485 */
        {
            sw = 1;                                                     /* 486 */
        }
        break;

    case 5:
        sw = (PhotoFlgIsUp() == 0);                                     /* 489 */
        break;
    }

    MapObjSetDrawEffect(dp, sw);                                        /* 494 */

    return sw;                                                          /* 499 */
}

/* MapPut draw callback for item-effect objects.  Suppressed while the object
 * pass is off, while items are off, and during a mission. */
void MapObjEffCallback(void)
{                                                                       /* 507 */
    if (GetObjDrawFLG() != 0 &&                                         /* 509 */
        (MapObjFlg & MAPOBJ_FLG_ITEM_OFF) == 0 &&                       /* 511 */
        CheckIngameMission() == 0)
    {
        void *obj = MapPutGetNowHdl();                                  /* 514 */

        ItemEffectDrawOne((int)MapPutGetWork(obj));                          /* 515 */
    }
}                                                                       /* 516 */

void MapObjItemOff(void)
{
    MapObjFlg |= MAPOBJ_FLG_ITEM_OFF;                                   /* 523 */
}

void MapObjItemOn(void)
{
    MapObjFlg &= ~MAPOBJ_FLG_ITEM_OFF;                                  /* 530 */
}

/* --------------------------------------------------------------------------
 *  Per-attribute animation control
 * ------------------------------------------------------------------------ */

/* Cloth (furniture attribute 1).  ActionType's low three bits are a compass
 * point -- eight 45-degree steps -- and iAction picks one of two wind presets.
 *
 * The ROM passes the MapPut handle straight in as the accessory key, where a
 * pointer was 4 bytes.  The truncating cast is kept because the cloth was
 * registered under the same truncated value -- MapObjRegistNunoAnim() calls
 * acsChodoSetCloth(..., (int)(intptr_t)hdl) -- and the two keys have to match.
 * Widening one side alone would stop the lookup finding anything. */
void MapObjNunoCtl(void *pHdl, int iAction, int iActionType)
{                                                                       /* 548 */
    static const MAPOBJ_WIND aWindPowerList[2] =                        /* data 2c8fc0 */
    {
        { 0.4f, 120 },
        { 0.8f,  60 }
    };

    if (iAction == 0)                                                   /* 550 */
    {
        acsChodoResetWind((int)(intptr_t)pHdl);
        return;
    }

    if (iAction < 0 || iAction >= 3)
    {
        return;
    }

    acsChodoSetWind((int)(intptr_t)pHdl,
                    MapGeomDegToRad((float)((iActionType & 7) * 45)),
                    aWindPowerList[iAction - 1].pow,
                    aWindPowerList[iAction - 1].cycle);                 /* 558 */
}                                                                       /* 559 */

/* Rope / bone (furniture attribute 5).  a_type is the swing power in
 * hundredths. */
int MapObjBoneCtl(int iFurnID, int action, int a_type)
{                                                                       /* 565 */
    if (action == 0)                                                    /* 566 */
    {
        acsRopeMoveStop((u_int)iFurnID);                                /* 569 */
    }
    else if (action == 1)                                               /* 571 */
    {
        acsRopeMoveRequest((u_int)iFurnID, 1, (float)a_type / 100.0f);  /* 572 */
    }

    return 0;                                                           /* 574 */
}                                                                       /* 577 */

/* Services one furniture record's animation request.
 *
 * The two model-name special cases dispatch by attribute -- name digit '5' is
 * rope, '9' is cloth -- and the numbered actions drive MapAnim.  Only the
 * look-at-player case returns without consuming the request, because it has to
 * re-run every frame. */
static void MapObjUpdateAnim(int anim_id, void *hdl, MDAT_OBJ *op)
{                                                                       /* 585 */
    if (op->ActionType < 0)                                             /* 585 */
    {
        return;
    }

    if (op->ModelName[1] == '5')                                        /* 588 */
    {
        MapObjBoneCtl(op->head.labelID, op->Action, op->ActionType);    /* 591 */
    }
    else if (op->ModelName[1] == '9')                                   /* 594 */
    {
        MapObjNunoCtl(hdl, op->Action, op->ActionType);                 /* 597 */
    }
    else
    {
        /* Actions below 6 wait for the animation to go idle; flag 0x40 is
         * "still playing". */
        if (op->Action < 6)                                             /* 602 */
        {
            if (anim_id < 0)                                            /* 603 */
            {
                return;
            }
            if ((MapAnimGetFlg(anim_id) & 0x40) != 0)                   /* 605 */
            {
                return;
            }
        }

        switch ((u_int)op->Action)                                      /* 609 */
        {
        case 0:
            MapAnimSetFlg(anim_id, 0x22);                               /* 611 */
            return;                                                     /* 612 */

        case 1:
        case 2:
        case 3:
            MapAnimCall(anim_id, op->ActionType);                       /* 616 */
            MapAnimDeleteFlg(anim_id, 0x12);                            /* 617 */
            MapAnimSetFlg(anim_id, 0x20);                               /* 618 */
            return;                                                     /* 619 */

        case 4:
        case 5:
            MapAnimCall(anim_id, op->ActionType);                       /* 622 */
            MapAnimSetFlg(anim_id, 0x30);                               /* 623 */
            MapAnimDeleteFlg(anim_id, 2);                               /* 624 */
            return;                                                     /* 625 */

        case 6:
        {
            /* Face the player.  Everything but the Y rotation stays the
             * record's own, and the new Y is written back so it persists. */
            float pos[4];
            float scale[4];
            float ppos[4];
            float rot[4];
            float mat[4][4];

            _SetVector(pos, op->Pos[0], op->Pos[1], op->Pos[2], 1.0f);  /* 627 */
            _SetVector(scale, 1.0f, 1.0f, 1.0f, 1.0f);                  /* 630 */

            GetPlayerPos(ppos);                                         /* 635 */
            op->Rot[1] = -(GetTrgtRotY(ppos, pos) * 180.0f
                           / (float)MAPGEOM_PI);                        /* 636 */

            _SetVector(rot, op->Rot[0], op->Rot[1], op->Rot[2], 0.0f);  /* 637 */
            MapObjSetPutMatrix(mat, pos, rot,
                               scale[0], scale[1], scale[2]);

            MapPutSetMatrix(hdl, mat);                                  /* 639 */
            return;                                                     /* 641 */
        }

        default:
            break;
        }
    }

    op->ActionType = ~op->ActionType;                                   /* 647 */
}                                                                       /* 648 */

/* --------------------------------------------------------------------------
 *  Per-frame update
 * ------------------------------------------------------------------------ */

/* Maps a record's Visible field onto the put-object's draw mode.  Mode 1 is
 * "leave it alone" -- the mask is cleared but nothing is set. */
void MapObjUpdateFlg(void *hdl, int v_flg)
{
    u_int *flg_p;

    if (hdl == (void *)0)                                               /* 657 */
    {
        return;
    }

    flg_p  = (u_int *)MapPutGetFlgPtr(hdl);                             /* 659 */
    *flg_p &= ~MAPPUT_FLG_DRAW_MASK;                                    /* 661 */

    switch ((u_int)v_flg)                                               /* 663 */
    {
    case 0: *flg_p |= MAPPUT_FLG_NODRAW; break;                         /* 666, 667 */
    case 2: *flg_p |= MAPPUT_FLG_MODE_2; break;                         /* 673, 674 */
    case 3: *flg_p |= MAPPUT_FLG_MODE_3; break;                         /* 677, 678 */
    case 4: *flg_p |= MAPPUT_FLG_MODE_4; break;                         /* 682, 683 */
    case 5: *flg_p |= MAPPUT_FLG_MODE_5; break;                         /* 686 */
    }
}                                                                       /* 689 */

/* One pass over both buffers' draw lists.
 *
 * Doors and put-items are skipped outright -- this loop only services
 * furniture.  Each record then splits three ways on its model name: an "f1"
 * model gets its draw flag updated and falls into the effect path, an "eff_"
 * model is a pure effect placeholder, and anything else is an ordinary model
 * that also gets animation and a sister look-at target.
 *
 * `param` is filled in the same partial way the ROM fills it -- pos[3] and
 * `enable` are left alone at both call sites, so SisNeckRegisterTarget() must
 * not read them.  That is the original's behaviour, not an omission here. */
void MapObjProc(void)
{
    for (int i = 0; i < MAPOBJ_LIST_NUM; i++)                           /* 699 */
    {
        if (MapLoadCheckDrawFlg(i) == 0)                                /* 702 */
        {
            continue;
        }

        MAPOBJ_HEAD *hp = MapObjGetListPtr(i);                          /* 704 */
        MAPOBJ_DAT  *dp = hp->dat;

        for (int j = 0; j < MAPOBJ_DAT_NUM; j++, dp++)                  /* 706 */
        {
            LOOK_AT_PARAM param;
            int          *v_flg_p;
            char         *m_name;
            int           flg;
            int           ex_flg;
            int           d_flg;
            MDAT_OBJ     *mp;

            if (dp->obj_ptr == (void *)0)                               /* 711 */
            {
                continue;
            }
            if (dp->stat == MAPOBJ_STAT_PUT)                            /* 713 */
            {
                continue;
            }
            if (dp->stat == MAPOBJ_STAT_DOOR)                           /* 714 */
            {
                continue;
            }

            flg = (dp->flg & MAPOBJ_FLG_REQUESTED) != 0
                      ? MAPPUT_FLG_NODRAW : 0;                          /* 718 */

            v_flg_p = MapObjGetVisiblePtr(dp);                          /* 224 */
            if (v_flg_p != (int *)0)
            {
                flg = (*v_flg_p != 0) ? 0 : MAPPUT_FLG_NODRAW;          /* 720 */
            }

            if (flg == 0)                                               /* 725 */
            {
                dp->flg &= ~MAPOBJ_FLG_REQUESTED;
            }

            ex_flg = 0;
            m_name = MapObjGetModelName(dp);                            /* 727 */

            if (m_name[0] == 'f' && m_name[1] == '1')                   /* 734 */
            {
                if (*v_flg_p != 0)                                      /* 736 */
                {
                    MapObjUpdateFlg(dp->obj_hdl, *v_flg_p);             /* 738 */
                }
                ex_flg = 1;                                             /* 740 */
            }

            if (strncmp(m_name, MAPOBJ_EFF_PREFIX, 4) != 0 && ex_flg == 0) /* 744 */
            {
                MapObjUpdateFlg(dp->obj_hdl, *v_flg_p);                 /* 747 */

                if (dp->stat == MAPOBJ_STAT_FURN)                       /* 750 */
                {
                    mp = (MDAT_OBJ *)dp->obj_ptr;                       /* 751 */

                    MapObjUpdateAnim(dp->anim_id, dp->obj_hdl, mp);     /* 753 */

                    param.pos[0]    = mp->Pos[0];                       /* 757 */
                    param.pos[1]    = mp->Pos[1];                       /* 758 */
                    param.pos[2]    = mp->Pos[2];                       /* 759 */
                    param.eye_spd   = MAPOBJ_LOOK_EYE_SPD;              /* 760 */
                    param.head_spd  = MAPOBJ_LOOK_HEAD_SPD;             /* 761 */
                    param.chest_spd = MAPOBJ_LOOK_CHEST_SPD;

                    SisNeckRegisterTarget(&param, LTP_MAYU_OBJ,
                                          MAPOBJ_LOOK_RANGE_OBJ);       /* 765 */
                }
            }
            else
            {
                d_flg = MapObjUpdateEffectDraw(*v_flg_p, dp);           /* 772 */
                mp    = (MDAT_OBJ *)dp->obj_ptr;                        /* 773 */

                /* The sister only looks at an idle placeholder that is not
                 * currently showing its effect. */
                if (mp->ActionType == 0 && d_flg == 0)                  /* 776 */
                {
                    param.pos[0]    = mp->Pos[0];                       /* 779 */
                    param.pos[1]    = mp->Pos[1];                       /* 780 */
                    param.pos[2]    = mp->Pos[2];                       /* 781 */
                    param.eye_spd   = MAPOBJ_LOOK_EYE_SPD;              /* 782 */
                    param.head_spd  = MAPOBJ_LOOK_HEAD_SPD;             /* 783 */
                    param.chest_spd = MAPOBJ_LOOK_CHEST_SPD;

                    SisNeckRegisterTarget(&param, LTP_MAYU_ITEM,
                                          MAPOBJ_LOOK_RANGE_ITEM);      /* 787 */
                }
            }

            if (dp->obj_save != (void *)0)                              /* 791 */
            {
                MapSaveCopyDat(dp->obj_save, dp->obj_ptr);              /* 793 */
            }
        }                                                               /* 795 */
    }                                                                   /* 796 */

    MapSpProc();                                                        /* 798 */
}

void MapObjInit(void)
{
    MapObjRegInit();                                                    /* 806 */
    MapObjFlg = 0;                                                      /* 807 */
}
