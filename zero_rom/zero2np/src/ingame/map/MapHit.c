// FILE: /home/zero_rom/zero2np/src/ingame/map/MapHit.c
//
// Dynamic hit registration: the small table of rectangles that are not part of
// the map's static wall set -- shut doors and pushed furniture -- which
// map_hit_check.c consults on top of the registration rectangles.
//
// A registration is (id, key, flg, rec).  `id` names the owner and doubles as
// the free marker (< 0 means the slot is empty); `id / 1000` is its group, the
// same area encoding RegDat uses for labels.  `key` lets a caller ask only for
// its own registrations, and bit 0 of `flg` disables one without removing it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapHit.o
// 0x00108b88..0x00109507.

#include "MapHit.h"
#include "../../common/utility2.h"            /* PRINT_ERROR */

#include "hit_check_base.h"     /* HcBasePointRectangle */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "map_hit_check.h"      /* MapHitCollisionPoint / MapHitCollisionLine */

#include <libvu0.h>
#include <stdio.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

/* 0x3ecccccc -- one ULP below 0.4f, because the shipped value came back out
 * of the ROOM debug menu's DOOR_HIT_Z row via DbmSave(). */
float MapHitDoorZ = 0.39999998f;                                        /* sdata 3eedf8 */

/* Written by HcBasePointRectangle() through MapHitCheckCol(); the callbacks
 * read them back rather than being handed the values, which is why they are
 * file statics with accessors instead of locals. */
static float MapHitColLen;                                              /* sdata 3eedfc */
static float MapHitColPoint[2][4];                                      /* bss 400e10 */

/* Door hit template, in door-local space: an 18-wide strip 2*MapHitDoorZ deep.
 * MapHitSetDoorHit() overwrites the Z column from MapHitDoorZ on every call,
 * so only the X/Y/W columns of this initialiser are ever read. */
static float MapHitPoint[4][4] =                                        /* data 2c8ce0 */
{
    {  0.0f, 0.0f,  0.4f, 1.0f },
    {  0.0f, 0.0f, -0.4f, 1.0f },
    { 18.0f, 0.0f,  0.4f, 1.0f },
    { 18.0f, 0.0f, -0.4f, 1.0f }
};

static fixed_array<MAPHIT_HEAD, MAPHIT_REC_NUM> MapHitRecList;          /* bss 400b00 */
static MAPHIT_CALLBACK MapHitCallBack[MAPHIT_CALLBACK_NUM];             /* bss 400d80 */

/* Two door slots, so a double door can have both leaves registered at once.
 * The rectangles live here rather than in the registration, which only stores
 * a pointer to them -- moving a door rewrites these in place and every
 * registration pointing at them follows automatically. */
static float MapHitDoorVec[2][4][4];                                    /* bss 400d90 */
static int   MapHitDoorID;                                              /* sbss 3f4a90 */

float MapHitGetColLen(void)
{
    return MapHitColLen;
}

float (*MapHitGetColPoint(void))[4]
{
    return MapHitColPoint;
}

void MapHitSetFlg(int id, int flg)
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)
    {
        if (MapHitRecList[i].id == id)
        {
            MapHitRecList[i].flg |= flg;                                /* 62 */
        }
    }
}

void MapHitDeleteFlg(int id, int flg)
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)
    {
        if (MapHitRecList[i].id == id)
        {
            MapHitRecList[i].flg &= ~flg;                               /* 65 */
        }
    }
}

/* Claims the first empty slot and stamps `id` into it.  The table is fixed at
 * 32 and there is no eviction -- running out is a content error, so the ROM
 * prints and hands back NULL rather than recycling anything. */
static MAPHIT_HEAD *MapHitGetFreeArea(int id)                           /* 72 */
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)                                /* 82 */
    {
        if (MapHitRecList[i].id < 0)                                    /* 83 */
        {
            MapHitRecList[i].id = id;                                   /* 84 */
            return &MapHitRecList[i];                                   /* 86 */
        }
    }

    PRINT_ERROR("ERR! NO_FREE_SPACE[%d]\n", id);                /* 88 */
    return (MAPHIT_HEAD *)0;                                            /* 89 */
}

/* NOTE: `flg` is deliberately not cleared here -- MapHitGetFreeArea() only
 * rewrites `id`.  A slot recycled after MapHitDeleteOne() therefore inherits
 * whatever flags its previous owner left behind, and callers that care set
 * them explicitly afterwards.  This is the ROM's behaviour, not an omission. */
int MapHitRegistRec(int id, int stat, int key, float *rec)              /* 94 */
{
    MAPHIT_HEAD *rp = MapHitGetFreeArea(id);                            /* 97 */

    if (rp == (MAPHIT_HEAD *)0)                                         /* 98 */
    {
        return -1;
    }

    rp->stat = stat;                                                    /* 99 */
    rp->key  = key;
    rp->rec  = rec;                                                     /* 100 */

    return 0;                                                           /* 101 */
}

/* Every slot carrying `id` is released, not just the first: one owner may have
 * registered several rectangles. */
void MapHitDeleteOne(int id)                                            /* 105 */
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)                                /* 108 */
    {
        if (MapHitRecList[i].id == id)                                  /* 110 */
        {
            MapHitRecList[i].id = -1;                                   /* 111 */
        }
    }
}

void MapHitDeleteAll(void)                                              /* 117 */
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)                                /* 120 */
    {
        MapHitRecList[i].id = -1;                                       /* 122 */
    }
}

/* Releases a whole area's worth of registrations at once -- id / 1000 is the
 * area, matching the label encoding RegDatGetStID4Label() decodes. */
void MapHitDeleteGroup(int a_id)                                        /* 128 */
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)                                /* 131 */
    {
        if ((MapHitRecList[i].id / 1000) == a_id)                       /* 133 */
        {
            MapHitRecList[i].id = -1;                                   /* 135 */
        }
    }
}

/* type 0 = pre-pass (before the walk), 1 = per-hit, 2 = post-pass. */
void MapHitRegistCallback(int type, MAPHIT_CALLBACK func)               /* 142 */
{
    if ((u_int)type < MAPHIT_CALLBACK_NUM)                              /* 143 */
    {
        MapHitCallBack[type] = func;                                    /* 144 */
    }
}

/* Walks the live registrations and reports whether `pos` (radius r) collides
 * with any.  `key` < 0 means "any owner"; otherwise only matching keys are
 * considered.
 *
 * The per-hit callback decides whether to keep going: returning 0 stops the
 * walk at the first hit, non-zero carries on so a position wedged between two
 * registrations gets pushed out of both.  With no callback installed the walk
 * stops at the first hit.  The return value starts at -1 and only becomes 1
 * once something is hit, so the post-pass callback can tell "no registrations
 * matched" from "matched and resolved". */
int MapHitCheckCol(float *pos, float r, int call_work, int key)         /* 153 */
{
    MAPHIT_DAT c_dat;
    MAPHIT_HEAD *hp;
    int ret = -1;
    int type;
    int i;

    c_dat.pos       = (float (*)[4])pos;                                /* 157 */
    c_dat.r         = r;                                                /* 159 */
    c_dat.call_work = call_work;

    if (MapHitCallBack[0] != (MAPHIT_CALLBACK)0)                        /* 162 */
    {
        MapHitCallBack[0]((MAPHIT_HEAD *)0, &c_dat, 0);                 /* 163 */
    }

    for (i = 0; i < MAPHIT_REC_NUM; i++)                                /* 166 */
    {
        hp = &MapHitRecList[i];

        if (hp->id < 0)                                                 /* 168 */
        {
            continue;
        }
        if ((key >= 0) && (key != hp->key))                             /* 169 */
        {
            continue;
        }
        if ((hp->flg & 1) != 0)                                         /* 171 */
        {
            continue;
        }

        type = HcBasePointRectangle(&MapHitColLen, pos,                 /* 178 */
                                    MapHitColPoint[0], MapHitColPoint[1],
                                    (float (*)[4])hp->rec, r);
        if (type == 0)
        {
            continue;
        }

        ret = 1;                                                        /* 186 */
        if (MapHitCallBack[1] == (MAPHIT_CALLBACK)0)
        {
            break;
        }
        if (MapHitCallBack[1](hp, &c_dat, type) == 0)                   /* 188 */
        {
            break;
        }
    }

    if (MapHitCallBack[2] != (MAPHIT_CALLBACK)0)                        /* 195 */
    {
        ret = MapHitCallBack[2]((MAPHIT_HEAD *)0, &c_dat, ret);         /* 196 */
    }

    return ret;                                                         /* 199 */
}

void MapHitInit(void)                                                   /* 207 */
{
    int i;

    for (i = 0; i < MAPHIT_REC_NUM; i++)                                /* 210 */
    {
        MapHitRecList[i].id = -1;                                       /* 212 */
    }

    for (i = 0; i < MAPHIT_CALLBACK_NUM; i++)                           /* 216 */
    {
        MapHitCallBack[i] = (MAPHIT_CALLBACK)0;                         /* 217 */
    }

    MapHitDoorID = -1;                                                  /* 221 */

    /* The default per-hit handler pushes the position back out; a caller that
     * wants to observe hits without being moved replaces it. */
    MapHitRegistCallback(1, MapTesCallbackHit);                         /* 224 */
}

void MapHitTerm(void)                                                   /* 235 */
{
}

/* ---- doors --------------------------------------------------------------
 * A door is registered as one rectangle whose vertices live in MapHitDoorVec,
 * so the door animation can rewrite the geometry each frame without touching
 * the registration.  Only one door is tracked at a time (MapHitDoorID); the
 * two MapHitDoorVec slots are the two leaves of a double door. */

void MapHitSetDoorRec(int id, int door_id, int key)                     /* 251 */
{
    MapHitRegistRec(id, 0, key, (float *)MapHitDoorVec[door_id & 1]);   /* 255 */
    MapHitDoorID = id;                                                  /* 256 */
}

void MapHitDeleteDoorFlg(int flg)                                       /* 261 */
{
    if (MapHitDoorID >= 0)                                              /* 262 */
    {
        MapHitDeleteFlg(MapHitDoorID, flg);                             /* 263 */
    }
}

void MapHitSetDoorFlg(int flg)                                          /* 268 */
{
    if (MapHitDoorID >= 0)                                              /* 269 */
    {
        MapHitSetFlg(MapHitDoorID, flg);                                /* 270 */
    }
}

void MapHitRegistDoorVec(int door_id, int vec_id, float *vec)
{
    g3dxVu0CopyVector(MapHitDoorVec[door_id & 1][vec_id & 3], vec);
}

void MapHitDeleteDoorRec(void)                                          /* 281 */
{
    if (MapHitDoorID >= 0)                                              /* 282 */
    {
        MapHitDeleteOne(MapHitDoorID);                                  /* 285 */
        MapHitDoorID = -1;                                              /* 286 */
    }
}

/* Rebuilds the door's blocking rectangle from the template and the door's
 * current matrix.  `type` picks which side of the hinge the strip sits on:
 * 1 shifts it back by the door's own width, 2 shifts it forward, anything
 * else leaves it centred on the hinge.  That is how a swinging door blocks
 * the doorway it has swung into rather than the one it came from. */
void MapHitSetDoorHit(int id, int type, float (*mat)[4])                /* 291 */
{
    float vec[4][4];
    float dx;
    int i;

    if (type == 1)                                                      /* 297 */
    {
        dx = 18.0f;
    }
    else if (type == 2)
    {
        dx = -18.0f;
    }
    else
    {
        dx = 0.0f;
    }

    for (i = 0; i < 4; i++)                                             /* 299 */
    {
        /* Corners alternate across the door's thickness. */
        MapHitPoint[i][2] = ((i & 1) == 0) ? -MapHitDoorZ : MapHitDoorZ; /* 300 */

        g3dxVu0CopyVector(vec[i], MapHitPoint[i]);                      /* 305 */
        vec[i][0] -= dx;                                                /* 306 */
        sceVu0ApplyMatrix(vec[i], mat, vec[i]);                         /* 308 */

        MapHitRegistDoorVec(id, i, vec[i]);                             /* 310 */
    }
}

void MapHitSetDoorZ(float num)
{
    MapHitDoorZ = num;
}

/* The default per-hit callback: push the position out of whatever
 * HcBasePointRectangle() said it was inside.  Type 1 is a corner (one point),
 * type 2 an edge (two points).  Returning 0 stops the walk, so the first
 * registration hit wins; an unknown type returns 1 to keep looking. */
int MapTesCallbackHit(MAPHIT_HEAD *hp, MAPHIT_DAT *dp, int type)        /* 328 */
{
    float len;
    float (*vec)[4];

    (void)hp;

    len = MapHitGetColLen();                                            /* 332 */
    vec = MapHitGetColPoint();                                          /* 335 */

    if (type == 1)                                                      /* 336 */
    {
        MapHitCollisionPoint(*dp->pos, *dp->pos, vec[0], len, dp->r);   /* 339 */
    }
    else if (type == 2)                                                 /* 342 */
    {
        MapHitCollisionLine(*dp->pos, *dp->pos, vec[0], vec[1], len, dp->r);
    }
    else
    {
        return 1;
    }

    return 0;                                                           /* 348 */
}
