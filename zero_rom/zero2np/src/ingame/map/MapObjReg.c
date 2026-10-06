// FILE: /home/zero_rom/zero2np/src/ingame/map/MapObjReg.c
//
// Map-object registration: turns a room's registration records into the
// per-buffer draw list.
//
// One pass per record type -- doors (7), furniture (3), put-items (11) -- each
// walking RegDat and handing every record to a Regist* entry point.  Those
// share a spine: normalise the model name, bias the position by the area
// origin, claim a draw entry, resolve the model, put it in the draw list, then
// attach lighting, animation and hit rectangles.
//
// Two buffers exist so a door transition can have both rooms resident.  That is
// also why MapObjDeletDraw() hands a door being walked through to the *other*
// buffer instead of dropping it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapObjReg.o
// 0x0010e3d0..0x001107bf.

#include "MapObjReg.h"
#include "../../common/utility2.h"            /* PRINT_ERROR */

#include "FurnCtl.h"
#include "FurnLoad.h"
#include "MapAnim.h"
#include "MapDoor.h"
#include "MapDraw.h"
#include "MapGeom.h"                            /* MapObjSetPutMatrix / MAPOBJ_* */
#include "MapHit.h"
#include "MapLight.h"
#include "MapLoad.h"
#include "MapObj.h"
#include "MapPut.h"
#include "MapSave.h"
#include "MapSp.h"
#include "RegDat.h"
#include "../../graphics/graph3d/ctl/fixed_array.h"

#include "../ingame_effect.h"                       /* IgEffect*ModelDraw   */
#include "../../common/utility.h"                   /* _SetVector / GetRandValI */
#include "../../graphics/effect/effect_obj.h"
#include "../../graphics/graph3d/g3dCore.h"         /* g3dSetGsRegisters    */
#include "../../graphics/graph3d/sgd_types.h"       /* SGDFILEHEADER        */
#include "../../graphics/motion/accessory.h"        /* acsRope* / acsChodo* */
#include "../../graphics/obj_draw_ctrl.h"           /* GetObjDrawFLG        */
#include "../photo/photo_dat.h"                     /* photo_datObj*        */

#include <libvu0.h>
#include <stdio.h>
#include <string.h>

/* Model-id ranges MapObjRegistEffect() dispatches on. */
enum
{
    MAPOBJ_ALPHA_LO    = 100,    /* alpha-ramp models              */
    MAPOBJ_ALPHA_LEN   = 100,
    MAPOBJ_WATER_LO    = 0x25b,  /* water flow / light shaft block */
    MAPOBJ_TOUROU_LO   = 0x32a,  /* lantern block                  */
    MAPOBJ_TOUROU_LEN  = 0x5a,
    MAPOBJ_TOUROU2_LO  = 0x96,   /* second lantern block           */
    MAPOBJ_TOUROU2_LEN = 0x2b,
    MAPOBJ_TOUROU_FREA = 0x332   /* the one lantern with a flame   */
};

static int MapObjSceneLoadFlg;                                          /* sdata 3eef78 */
static fixed_array<MAPOBJ_HEAD, MAPOBJ_LIST_NUM> MapObjList;                         /* bss 40b6a8 */

/* The repeated inline: put an already-loaded model into the draw list at
 * pos/rot with unit scale. */
static void *MapObjPutObj(int buff_id, int *mdl_addr,
                          const float *mpos, const float *mrot)
{                                                                       /* 293 */
    float scale[4];
    float pos[4];
    float rot[4];
    void *hdl;

    scale[0] = 1.0f;                                                    /* 294 */
    scale[1] = 1.0f;
    scale[2] = 1.0f;
    scale[3] = 1.0f;

    _SetVector(pos, mpos[0], mpos[1], mpos[2], 1.0f);                   /* 300 */
    _SetVector(rot, mrot[0], mrot[1], mrot[2], 0.0f);                   /* 301 */

    hdl = MapPutSetObj(buff_id, (u_int *)mdl_addr, pos, rot, scale,
                       (GRA3DLIGHTDATA *)0, 2);
    if (hdl == (void *)0)
    {
        PRINT_ERROR("PUT_OBJ_MAX_OVER.\n");
    }

    return hdl;
}

void MapObjRegSetSceneLoad(int flg)
{
    MapObjSceneLoadFlg = flg;
}

/* First entry whose obj_ptr is NULL.  The list is fixed at 300 per buffer and
 * there is no eviction -- running out is a content error. */
static MAPOBJ_DAT *MapObjGetFreeListSpacePtr(MAPOBJ_HEAD *hp)           /* 76 */
{
    int i;

    for (i = 0; i < MAPOBJ_DAT_NUM; i++)                                /* 79 */
    {
        if (hp->dat[i].obj_ptr == (void *)0)                            /* 80 */
        {
            return &hp->dat[i];                                         /* 82 */
        }
    }

    PRINT_ERROR("NO_FREE_SPACE_OBJ\n");                         /* 84 */

    return (MAPOBJ_DAT *)0;                                             /* 85 */
}

MAPOBJ_HEAD *MapObjGetListPtr(int id)                                   /* 89 */
{
    if ((u_int)id >= MAPOBJ_LIST_NUM)                                   /* 90 */
    {
        return (MAPOBJ_HEAD *)0;
    }

    return &MapObjList[id];                                             /* 92 */
}

/* Raises or drops an object's wall collision.  Raising registers every
 * rectangle carrying the object's label -- one object can own several -- and
 * dropping releases them all by id, which is why there is no matching walk on
 * the delete path. */
int MapObjSetHit2(MDAT_OBJ *op, int hit_sw)                             /* 98 */
{
    MB_OUT_HEAD *hp;
    MB_OUT_RECT *rp;
    int buff_id;

    if (op == (MDAT_OBJ *)0)                                            /* 102 */
    {
        return -1;
    }

    op->HitCheck = hit_sw;                                              /* 106 */
    buff_id = RegDatBuffID4Label((int)op->head.labelID);                /* 107 */
    hp = RegDatGetHead(buff_id);                                        /* 110 */

    if (hit_sw == 0)                                                    /* 113 */
    {
        MapHitDeleteOne((int)op->head.labelID);                         /* 115 */
        return 0;
    }

    RegDatVecFind4Label(buff_id, (int)op->head.labelID);                /* 117 */
    while ((rp = RegDatVecNextFind(buff_id)) != (MB_OUT_RECT *)0)       /* 120 */
    {
        MapHitRegistRec((int)op->head.labelID, 0, hp->kai, rp->vec[0]); /* 122 */
    }

    return 0;                                                           /* 123 */
}

int MapObjSetHit(int labelID, int hit_sw)                               /* 128 */
{
    MDAT_OBJ *op = (MDAT_OBJ *)RegDatGetStPtr4Label2(labelID);

    return MapObjSetHit2(op, hit_sw);                                   /* 130 */
}

/* Raises collision for every type-3 record in the buffer that is marked solid. */
int MapObjSetHitArea(int reg_id)                                        /* 135 */
{
    MB_OUT_SECTION *mp;

    RegDatGetStPtrStart(reg_id, 3);                                     /* 139 */

    while ((mp = RegDatGetNextStPtr(reg_id)) != (MB_OUT_SECTION *)0)    /* 140 */
    {
        if (((MDAT_OBJ *)mp)->HitCheck != 0)                            /* 143 */
        {
            MapObjSetHit((int)mp->labelID, 1);                          /* 145 */
        }
    }

    return 0;                                                           /* 147 */
}

/* Attributes 1 (cloth) and 5 (bone) light off the baked block rather than the
 * live room light -- both deform their mesh, so a live light would have to be
 * re-evaluated every frame. */
int MapObjGetLightFlg(char *name)                                       /* 155 */
{
    if (FurnLoadGetAttr(name) == 1)                                     /* 156 */
    {
        return 1;
    }

    return (FurnLoadGetAttr(name) == 5);                                /* 158 */
}

/* Effect models are ordinary SGDs that additionally drive an effect.  The
 * model id picks which: an alpha ramp, a water flow, a light shaft, or a
 * lantern.  Returns the put handle, or NULL when the name is not an effect
 * model at all -- which is the caller's signal to place it normally. */
static void *MapObjRegistEffect(int buff_id, char *name, int *mdl_addr,
                                int label, float *mpos, float *mrot)
{                                                                       /* 163 */
    float mat[4][4];
    u_short *sp;
    u_int *flg;
    void *hdl;
    int id;
    int type;

    id = FurnCtlGetID(name);                                            /* 165 */
    if (*name != 'f')                                                   /* 168 */
    {
        return (void *)0;
    }

    if (((u_int)(id - MAPOBJ_TOUROU_LO) < MAPOBJ_TOUROU_LEN) ||         /* 171 */
        ((u_int)(id - MAPOBJ_TOUROU2_LO) < MAPOBJ_TOUROU2_LEN))
    {
        /* Lanterns: the base is always drawn, the flame only on the one id
         * that has one. */
        if (id == MAPOBJ_TOUROU_FREA)                                   /* 181 */
        {
            hdl = MapPutSetFunc(buff_id, (u_int *)IgEffectTourouFreaModelDraw, 0);
            EffectTourouFreaRegist(hdl, buff_id);                       /* 184 */
        }
        else
        {
            hdl = MapPutSetFunc(buff_id, (u_int *)IgEffectTourouBaseModelDraw, 0);
            EffectTourouBaseRegist(mdl_addr, buff_id);                  /* 187 */
        }

        /* Bit 4 off: the effect draws the model itself. */
        flg = (u_int *)MapPutGetFlgPtr(hdl);                            /* 190 */
        *flg &= ~0x10u;
    }
    else if ((u_int)(id - MAPOBJ_ALPHA_LO) < MAPOBJ_ALPHA_LEN)          /* 196 */
    {
        /* Alpha-ramp models are placed normally; the ramp is keyed on the
         * label so the event macros can drive it.  The four half-words written
         * back into the record are its runtime state -- 0x12 is the initial
         * mode, the -1 pair the "no request pending" marker. */
        EffectModelAlphaChangeRegist(mdl_addr, label);                  /* 197 */
        hdl = MapObjPutObj(buff_id, mdl_addr, mpos, mrot);              /* 198 */

        sp = RegDatGetStPtr4Label2(label);                              /* 201 */
        sp[0x2c] = 0x12;
        sp[0x2d] = 0;
        sp[0x2a] = 0xffff;
        sp[0x2b] = 0xffff;                                              /* 202 */

        if (*(int *)(sp + 0x28) != 0)                                   /* 205 */
        {
            EffectModelAlphaChangeReq(label, 0x80, 0x80, 0);
        }
        else
        {
            EffectModelAlphaChangeReq(label, 0, 0, 0);
        }

        return hdl;
    }
    else if (FurnLoadGetAttr(name) != 4)                                /* 210 */
    {
        return (void *)0;
    }
    else
    {
        type = id - MAPOBJ_WATER_LO;                                    /* 213 */

        if (((type >= 0) && (type <= 3)) || ((type >= 7) && (type <= 10)))
        {
            /* Water flow.  The second block continues the first, so its ids
             * are folded back onto the same 0..3 variant range. */
            if (type >= 7)                                              /* 250 */
            {
                type = id - (MAPOBJ_WATER_LO + 3);
            }

            EffectWaterFlowRegist(mdl_addr, buff_id, type);             /* 252 */
            hdl = MapObjPutObj(buff_id, mdl_addr, mpos, mrot);          /* 254 */
            /* Drawn before everything else in the buffer. */
            MapPutSetFirst(hdl, (short)id);                             /* 260 */

            return hdl;                                                 /* 261 */
        }

        /* Light shafts.  Two ids get a narrower cone. */
        hdl = MapPutSetFunc(buff_id, (u_int *)IgEffectLightComeInModelDraw, 0);
        if (id == 0x25f)                                                /* 216 */
        {
            type = 1;
        }
        else if (id == 0x261)
        {
            type = 2;
        }
        else
        {
            type = 0;
        }
        EffectLightComeInRegist(mdl_addr, buff_id, type);               /* 220 */

        flg = (u_int *)MapPutGetFlgPtr(hdl);                            /* 221 */
        *flg &= ~0x10u;
    }

    /* Shared tail for the callback-drawn effects: they are not put-objects, so
     * their matrix has to be built and handed over explicitly. */
    MapObjSetPutMatrix(mat, mpos, mrot, 1.0f, 1.0f, 1.0f);              /* 265 */
    MapPutSetWork(hdl, (intptr_t)mdl_addr);                        /* 277 */
    MapPutSetMatrix(hdl, mat);                                          /* 278 */

    return hdl;                                                         /* 282 */
}

/* Places one model.  Attribute 5 (bone) ignores the authored rotation -- the
 * bone animation supplies it. */
static void *MapObjRegistModel(int buff_id, char *name, int *mdl_addr,
                               int label, float *mpos, float *mrot)
{                                                                       /* 293 */
    float rrot[4];
    void *hdl;

    if (mdl_addr == (int *)0)
    {
        PRINT_ERROR("MODEL_NOT_PACK_FILE: buff_id[%d] addr[%x]\n", buff_id, 0);
        return (void *)0;
    }

    if (FurnLoadGetAttr(name) == 5)
    {
        rrot[0] = 0.0f;
        rrot[1] = 0.0f;
        rrot[2] = 0.0f;
    }
    else
    {
        rrot[0] = mrot[0];
        rrot[1] = mrot[1];
        rrot[2] = mrot[2];
    }

    hdl = MapObjRegistEffect(buff_id, name, mdl_addr, label, mpos, rrot);
    if (hdl == (void *)0)
    {
        hdl = MapObjPutObj(buff_id, mdl_addr, mpos, rrot);              /* 307 */
    }

    return hdl;                                                         /* 311 */
}

static MAPOBJ_DAT *MapObjGetFreeDatPtr(int buff_id, void *op, int stat) /* 319 */
{
    MAPOBJ_HEAD *hp = MapObjGetListPtr(buff_id);                        /* 321 */
    MAPOBJ_DAT  *dp;

    if (hp == (MAPOBJ_HEAD *)0)
    {
        return (MAPOBJ_DAT *)0;
    }

    dp = MapObjGetFreeListSpacePtr(hp);                                 /* 323 */
    if (dp == (MAPOBJ_DAT *)0)
    {
        return (MAPOBJ_DAT *)0;
    }

    dp->flg     = 0;                                                    /* 324 */
    dp->anim_id = -1;                                                   /* 325 */
    dp->stat    = stat;
    dp->obj_ptr = op;                                                   /* 326 */
    dp->obj_hdl = (void *)0;

    /* Only furniture persists state across a room reload. */
    dp->obj_save = (stat == 3)                                          /* 330 */
                 ? MapSaveGetTblPtr((int)((MB_OUT_SECTION *)op)->labelID)
                 : (void *)0;                                           /* 335 */

    return dp;                                                          /* 336 */
}

/* Claims a draw entry, resolves and places the model, then attaches lighting.
 * A NULL `name` is legal: the caller wants a bookkeeping entry with no model,
 * which is how effect-only records get tracked. */
static MAPOBJ_DAT *MapObjAddDrawList(int buff_id, void *op, char *name,
                                     float *mpos, float *mrot, int stat)
{                                                                       /* 341 */
    MAPOBJ_DAT *dp;
    MLOAD_HEAD *hp;
    short *flg;
    u_int *put_flg;
    int pre_flg;

    dp = MapObjGetFreeDatPtr(buff_id, op, stat);                        /* 343 */
    if (dp == (MAPOBJ_DAT *)0)
    {
        PRINT_ERROR("NO_OBJ_CTL_SPASE[%s]\n", name);                /* 345 */
        return (MAPOBJ_DAT *)0;
    }

    if (name == (char *)0)                                              /* 349 */
    {
        dp->mdl_addr = (int *)0;
        return dp;
    }

    dp->mdl_addr = (int *)FurnCtlGetModelAddr(buff_id, name);           /* 355 */
    if (dp->mdl_addr == (int *)0)
    {
        PRINT_ERROR("NO_MODEL_ADDR[%s]\n", name);                   /* 357 */
        return dp;
    }

    dp->obj_hdl = MapObjRegistModel(buff_id, name, dp->mdl_addr,        /* 365 */
                                    (int)((MB_OUT_SECTION *)op)->labelID,
                                    mpos, mrot);
    if (dp->obj_hdl == (void *)0)
    {
        PRINT_ERROR("NO_OBJ_HDL[%s]\n", name);                      /* 390 */
        return dp;
    }

    flg = FurnCtlGetFlgPtr(buff_id, name);                              /* 367 */
    hp  = MapLoadGetHeadPtr(buff_id);
    pre_flg = 1;

    /* The Mei room lights everything off its own fixed light, so the
     * per-object decision is skipped there. */
    if (MapMeiCheck(hp) == 0)                                           /* 369 */
    {
        pre_flg = MapObjGetLightFlg(name);                              /* 372 */

        /* Put-flag bit 5 marks "uses the live room light". */
        put_flg = (u_int *)MapPutGetFlgPtr(dp->obj_hdl);                /* 375 */
        if (pre_flg == 0)
        {
            *put_flg |= 0x20u;
        }
        else
        {
            *put_flg &= ~0x20u;
        }
    }

    /* FurnCtl flag bit 1 means this model's light was already built once for
     * the room.  A baked-light object still needs its own copy; a live-light
     * one can share, so it is skipped. */
    if (((*flg & 2) != 0) && (pre_flg == 0))                            /* 378 */
    {
        return dp;
    }

    MapLightSetLight(buff_id, dp->mdl_addr, dp->obj_hdl, pre_flg);      /* 383 */
    *flg |= 2;                                                          /* 387 */

    return dp;                                                          /* 395 */
}

/* Draw callback for rope-animated models (attribute 5).  The SGD's coordinate
 * array is reset to identity, the put matrix copied into the root, and the
 * rope solver run over it before the model is drawn.
 *
 * The two negations rebuild the map's handedness in the coordinate frame --
 * the same Y/Z flip MapObjSetPutMatrix() applies to a placement matrix. */
void MapObjCallbackBornAnim(void)                                       /* 403 */
{
    SGDCOORDINATE *furn_cp;
    float (*mat)[4][4];
    SGDFILEHEADER *sgd_top;
    u_int furn_id;
    void *obj;
    u_int i;
    float z;

    if (GetObjDrawFLG() == 0)                                           /* 411 */
    {
        return;
    }

    obj     = MapPutGetNowHdl();                                        /* 412 */
    furn_id = (u_int)MapPutGetWork(obj);                                /* 413 */
    sgd_top = (SGDFILEHEADER *)MapPutGetModelPtr(obj);                                   /* 416 */
    furn_cp = (SGDCOORDINATE *)sgd_top->pCoord;

    for (i = 0; i < sgd_top->uiNumBlock - 1; i++)                                /* 417 */
    {
        sceVu0UnitMatrix(furn_cp[i].matCoord);                          /* 418 */
    }

    mat = MapPutGetMatrixPtr(obj);                                      /* 421 */
    sceVu0CopyMatrix(furn_cp->matCoord, *mat);

    z = furn_cp->matCoord[2][2];                                        /* 422 */
    furn_cp->matCoord[1][1] = -furn_cp->matCoord[1][1];
    furn_cp->matCoord[2][2] = -z;

    acsMoveRope(furn_id, furn_cp);                                      /* 425 */
    MapDrawSGD(sgd_top);                                                /* 427 */
}

static int MapObjRegistBornAnim(MB_OUT_SECTION *sp, char *name,
                                void *obj_hdl, int *mdl_addr)
{                                                                       /* 433 */
    int anim_id;

    (void)mdl_addr;

    anim_id = FurnCtlGetAnimID(name, 1);                                /* 439 */
    if (anim_id < 0)
    {
        PRINT_ERROR("NO_REGIST_BORN_ANIM[%s]\n", name);             /* 441 */
        return -1;
    }

    acsRopeSetWork((int)sp->labelID, (u_char)anim_id);                  /* 446 */

    /* Put-flag bit 0 routes the draw through the callback above. */
    *(u_int *)MapPutGetFlgPtr(obj_hdl) |= 1u;                           /* 448 */
    MapPutSetFuncAddr(obj_hdl, MapObjCallbackBornAnim);                 /* 451 */
    MapPutSetWork(obj_hdl, (int)sp->labelID);                           /* 452 */

    return 0;                                                           /* 453 */
}

/* Releases every rope work whose furniture id belongs to one of the buffer's
 * areas.  The rope list is global, so it has to be filtered by area rather
 * than walked per buffer. */
void MapObjBornDelete(int buff_id)                                      /* 458 */
{
    MLOAD_HEAD  *mp = MapLoadGetHeadPtr(buff_id);                       /* 460 */
    MB_OUT_HEAD *rp;
    u_int furn_id;
    u_int i;
    u_int j;

    for (i = 0; i < 4; i++)                                             /* 462 */
    {
        if (mp->reg_id[i] == -1)
        {
            continue;
        }

        rp = RegDatGetHead((int)mp->reg_id[i]);                         /* 468 */

        for (j = 0; j < 20; j++)                                        /* 469 */
        {
            furn_id = acsRopeGetFurnID(j);                              /* 471 */
            if (((int)furn_id / 1000) == rp->area_id)
            {
                acsRopeReleaseWork(furn_id);                            /* 472 */
            }
        }
    }
}

/* Draw callback for the multi-instance (tree) animation.  The two GS register
 * writes bracket the draw with the alpha-test state the foliage needs. */
static void MapObjCallbackTreeAnim(void)                                /* 483 */
{
    SGDFILEHEADER *pSGDHead;
    sceGifPackAd aGPA[1];
    sceGifPackAd aGPAEnd[1];
    float (*mat)[4][4];
    void *obj;
    int id;

    obj      = MapPutGetNowHdl();                                       /* 484 */
    pSGDHead = (SGDFILEHEADER *)MapPutGetModelPtr(obj);                 /* 485 */
    id       = (int)MapPutGetWork(obj);                                      /* 488 */

    if (GetObjDrawFLG() == 0)
    {
        return;
    }

    aGPA[0].ADDR = 0x47;                                                /* 498 */
    aGPA[0].DATA = 0x5360b;
    g3dSetGsRegisters(aGPA, 1, 1);

    MapManimSetMatrixSgdOne2(pSGDHead, id);                             /* 501 */
    mat = MapPutGetMatrixPtr(obj);                                      /* 504 */
    MapDrawObj(pSGDHead, *mat);                                         /* 506 */

    aGPAEnd[0].ADDR = 0x47;                                             /* 507 */
    aGPAEnd[0].DATA = 0x5001b;
    g3dSetGsRegisters(aGPAEnd, 1, 1);                                   /* 510 */
}

/* Cloth (attribute 1).  The solver needs somewhere to keep its per-instance
 * state, so it is handed the top of the room's free memory and the watermark
 * is advanced by however much it took. */
static int MapObjCheckNuno(int buff_id, char *name, void *hdl, int *mdl_addr)
{                                                                       /* 527 */
    float (*mat)[4][4];
    u_int *mem;
    int anim_id;

    anim_id = FurnCtlGetAnimID(name, 0);                                /* 535 */
    if (anim_id < 0)
    {
        PRINT_ERROR("NO_NUNO_ID[%s]\n", name);                      /* 537 */
        return -1;
    }

    mat = MapPutGetMatrixPtr(hdl);                                      /* 541 */
    MapDrawSetMatrixSGD((u_int *)mdl_addr, *mat);                       /* 543 */

    mem = (u_int *)MapLoadGetFreeMemAddr(buff_id);                      /* 545 */
    mem = acsChodoSetCloth((u_int *)mdl_addr, anim_id, buff_id, mem,
                           (int)(intptr_t)hdl);
    MapLoadSetFreeMemAddr(buff_id, (char *)mem);                        /* 548 */

    /* Bit 11 marks the put-object as cloth-driven. */
    *(u_int *)MapPutGetFlgPtr(hdl) |= 0x800u;                           /* 550 */

    return 1;                                                           /* 560 */
}

/* Foliage.  Three instances are registered per tree type the first time that
 * type is seen (FurnCtl flag bit 0 is the "already done" marker), and each
 * placed tree then picks one of the three at random so a stand does not move
 * in lockstep.  MapSpAraCheck() selects the windier motion in some areas. */
static int MapObjRegistTreeAnim(int buff_id, char *m_name, char *model_addr,
                                char *mot_addr, void *obj_hdl,
                                float *offset, float *rot)
{                                                                       /* 569 */
    /* Per-type MapManim sequence list, read out of MapObjReg.o's .data
     * (0x2c8fd0, 0xa8 bytes -- the object's only .data).  All six rows are
     * identical: two holes, then sub-sequences 2, 1 and 0, then the -99
     * terminator.  So one MapManimSetAnim() costs three MapAnim slots and a
     * tree type costs nine.
     *
     * This has to stay initialised.  Zeroed, there is no terminator and every
     * entry reads as a valid sequence id, so MapManimSetAnimSub() runs all the
     * way to the model's block count and registers a slot per block -- which
     * overflows the 32-slot table on the first stand of trees in the first
     * room (MANIM_MAX_OVER). */
    static int anim_list[6][7] =                                        /* data 2c8fd0 */
    {
        { -1, -1, 2, 1, 0, MAPMANIM_ID_END, MAPMANIM_ID_END },
        { -1, -1, 2, 1, 0, MAPMANIM_ID_END, MAPMANIM_ID_END },
        { -1, -1, 2, 1, 0, MAPMANIM_ID_END, MAPMANIM_ID_END },
        { -1, -1, 2, 1, 0, MAPMANIM_ID_END, MAPMANIM_ID_END },
        { -1, -1, 2, 1, 0, MAPMANIM_ID_END, MAPMANIM_ID_END },
        { -1, -1, 2, 1, 0, MAPMANIM_ID_END, MAPMANIM_ID_END },
    };
    static int id_tes[7][3];                                            /* bss 40b650 */

    int work_list[7];
    short *flp;
    int tree_id;
    int cnt;

    tree_id = m_name[3] - '0';                                          /* 580 */
    if (tree_id >= 7)
    {
        PRINT_ERROR("TREE_TYPE_MAX_OVER[%d]\n", tree_id);           /* 616 */
        return -1;                                                      /* 617 */
    }

    /* PORT: the ROM's guard admits seven tree types (id_tes is [7][3]) but
     * anim_list only has six rows, so type 6 memcpy's 28 bytes past the table.
     * Harmless-ish on the EE, an out-of-bounds read here -- and the garbage it
     * copied could never have produced a sane sequence list anyway. */
    if (tree_id >= 6)
    {
        PRINT_ERROR("TREE_TYPE_MAX_OVER[%d]\n", tree_id);
        return -1;
    }

    if (MapSpAraCheck() != 0)                                           /* 583 */
    {
        mot_addr = FurnCtlGetMotAddrEx(buff_id, m_name, 2);             /* 584 */
    }

    flp = FurnCtlGetFlgPtr(buff_id, m_name);                            /* 585 */
    if ((*flp & 1) == 0)                                                /* 592 */
    {
        for (cnt = 0; cnt < 3; cnt++)                                   /* 594 */
        {
            memcpy(work_list, anim_list[tree_id], sizeof(work_list));   /* 597 */
            id_tes[tree_id][cnt] =
                MapManimSetAnim(buff_id, model_addr, mot_addr, work_list,
                                offset, rot, GetRandValI(100));         /* 599 */
        }
        *flp |= 1;                                                      /* 605 */
    }

    /* Bit 0 routes the draw through the tree callback. */
    *(u_int *)MapPutGetFlgPtr(obj_hdl) |= 1u;                           /* 608 */
    MapPutSetFuncAddr(obj_hdl, MapObjCallbackTreeAnim);                 /* 609 */
    MapPutSetWork(obj_hdl, id_tes[tree_id][GetRandValI(3)]);            /* 610 */

    return 0;                                                           /* 614 */
}

/* Registers whatever animation the model's pak carries.  A model with no
 * motion at index 1 simply has none.  Attribute 2 is foliage, which goes
 * through MapManim and reports -1 because its instances are not tracked here. */
int MapObjCheckAnim(int buff_id, char *m_name, float *offset, float *rot,
                    void *obj_hdl, int ani_type)
{                                                                       /* 624 */
    char *model_addr = (char *)0;
    char *mot_addr   = (char *)0;
    float (*mat)[4][4];
    u_int flg;
    int id;

    FurnCtlGetAddr(buff_id, m_name, &model_addr, &mot_addr);            /* 629 */

    if (mot_addr == (char *)0)                                          /* 632 */
    {
        return -1;
    }

    if (model_addr == (char *)0)
    {
        PRINT_ERROR("NO_ANIM_MODEL [%s]\n", m_name);                /* 635 */
        return -2;                                                      /* 636 */
    }

    if (FurnCtlGetAttr(buff_id, m_name) == 2)                           /* 640 */
    {
        if (FurnCtlGetID(m_name) < MAPOBJ_TOUROU_LO)                    /* 642 */
        {
            MapObjRegistTreeAnim(buff_id, m_name, model_addr, mot_addr,
                                 obj_hdl, offset, rot);                 /* 645 */
        }
        return -1;
    }

    mat = MapPutGetMatrixPtr(obj_hdl);                                  /* 651 */
    id  = MapAnimRegistEx(ani_type, (u_int *)model_addr, (u_int *)mot_addr,
                          mat, offset, rot, 0x20, 2);                   /* 653 */

    /* Buffer 0 and 1 get different draw-group bits. */
    flg = (buff_id == 0) ? 4u : 8u;                                     /* 655 */
    MapAnimSetFlg(id, flg | 0xa2u);                                     /* 658 */

    if (MapObjSceneLoadFlg == 0)                                        /* 661 */
    {
        MapLightSetLight(buff_id, (int *)model_addr, obj_hdl, 0);       /* 664 */
    }

    return id;                                                          /* 665 */
}

/* Dispatches a placed object to its animation kind.  Note the +PI on Y: the
 * authored rotation is measured from the opposite facing. */
void MapObjRegistMot(int buff_id, MAPOBJ_DAT *dp, char *name, int action,
                     int a_type, float *irot, float *ipos)
{                                                                       /* 671 */
    void *hdl      = dp->obj_hdl;                                       /* 675 */
    int  *mdl_addr = dp->mdl_addr;                                      /* 676 */
    float rot[4];
    float offset[4];
    int attr;

    (void)action;

    if (hdl == (void *)0)                                               /* 677 */
    {
        PRINT_ERROR("NO_MODEL_HDL [%s]\n", name);
        return;
    }
    if (mdl_addr == (int *)0)                                           /* 679 */
    {
        PRINT_ERROR("NO_ANIM_MODEL [%s]\n", name);
        return;
    }

    attr = FurnLoadGetAttr(name);                                       /* 680 */
    if (attr == 1)                                                      /* 681 */
    {
        MapObjCheckNuno(buff_id, name, hdl, mdl_addr);                  /* 685 */
    }
    else if (attr == 5)                                                 /* 687 */
    {
        MapObjRegistBornAnim((MB_OUT_SECTION *)dp->obj_ptr, name, hdl, mdl_addr);
    }                                                                   /* 688 */
    else
    {
        _SetVector(rot,
                   irot[0] * MAPOBJ_DEG2RAD,
                   irot[1] * MAPOBJ_DEG2RAD + 3.1415927f,
                   irot[2] * MAPOBJ_DEG2RAD, 1.0f);                     /* 691 */
        _SetVector(offset, ipos[0], ipos[1], ipos[2], 1.0f);            /* 697 */

        dp->anim_id = MapObjCheckAnim(buff_id, name, offset, rot, hdl, a_type);
    }                                                                   /* 698 */

    MapSpObjReg(buff_id, name, dp, irot);                               /* 701 */
}

/* Records carry area-relative positions; `stat` bit 2 marks a buffer whose
 * records are already in world space, so the origin bias is applied only when
 * that bit is clear. */
static void MapObjBiasPos(int reg_id, int stat, float *pos)
{
    MB_OUT_HEAD *hp;

    if ((stat & 4) != 0)                                                /* 518 */
    {
        return;
    }

    hp = RegDatGetHead(reg_id);                                         /* 519 */
    pos[0] += hp->Pos[0];                                               /* 520 */
    pos[1] += hp->Pos[1];                                               /* 521 */
    pos[2] += hp->Pos[2];                                               /* 522 */
}

int MapObjRegistDoor(int buff_id, int reg_id, int stat, MB_OUT_SECTION *reg_p)
{                                                                       /* 714 */
    MDAT_DOOR  *op = (MDAT_DOOR *)reg_p;
    MAPOBJ_DAT *dp;
    char name[36];
    /* u_int in the ROM -- a 4-byte MAPDOOR_HEAD * handed out as an integer. */
    uintptr_t door_hdl;

    FurnCtlGetMdoelName(name, op->ModelName);                           /* 722 */

    door_hdl = MapDoorAdd(buff_id, op);                                 /* 728 */
    if (door_hdl == 0)
    {
        return 0;
    }

    MapObjBiasPos(reg_id, stat, op->Pos);                               /* 734 */

    dp = MapObjAddDrawList(buff_id, reg_p, name, op->Pos, op->Rot, 7);  /* 737 */
    if (dp == (MAPOBJ_DAT *)0)
    {
        PRINT_ERROR("MDOEL OBJ NULL\n");                            /* 739 */
    }
    else
    {
        MapDoorSetStat(door_hdl, dp->obj_hdl);                          /* 744 */
    }

    return 0;                                                           /* 746 */
}

int MapObjRegistFurn(int buff_id, int reg_id, int stat, MB_OUT_SECTION *reg_p)
{                                                                       /* 758 */
    MDAT_OBJ   *op = (MDAT_OBJ *)reg_p;
    MAPOBJ_DAT *dp;
    char name[36];
    float mat[4][4];
    void *hdl;
    int eff;

    if (MapObjGetListPtr(buff_id) == (MAPOBJ_HEAD *)0)                  /* 759 */
    {
        return 0;
    }

    FurnCtlGetMdoelName(name, op->ModelName);                           /* 762 */

    if (op->PhotoAble != 0)                                             /* 763 */
    {
        photo_datObjStart(op);
    }

    MapObjBiasPos(reg_id, stat, op->Pos);                               /* 772 */

    eff = MapObjCheckEffect(op->ModelName);                             /* 774 */

    if (eff == -1)
    {
        /* An ordinary model. */
        dp = MapObjAddDrawList(buff_id, reg_p, name, op->Pos, op->Rot, 3);
        MapObjUpdateFlg(dp->obj_hdl, op->Visible);                      /* 777 */

        if (dp != (MAPOBJ_DAT *)0)                                      /* 778 */
        {
            if (op->HitCheck != 0)                                      /* 780 */
            {
                MapObjSetHit2(op, 1);                                   /* 782 */
            }
            MapObjRegistMot(buff_id, dp, name, op->Action, op->ActionType,
                            op->Rot, op->Pos);                          /* 784 */
        }
    }
    else if (eff != -2)
    {
        /* An effect with no model of its own: the record's Action/ActionType
         * are repurposed to carry the effect id. */
        op->Action     = -1;                                            /* 790 */
        op->ActionType = eff;                                           /* 792 */

        MapObjAddDrawList(buff_id, reg_p, (char *)0, (float *)0, (float *)0, 3);

        if (eff == 0)                                                   /* 796 */
        {
            hdl = MapPutSetFunc(buff_id, (u_int *)MapObjEffCallback, 0); /* 801 */

            /* Bit 4 off -- the callback draws it. */
            *(u_int *)MapPutGetFlgPtr(hdl) &= ~0x10u;                   /* 806 */

            MapObjSetPutMatrix(mat, op->Pos, op->Rot, 1.0f, 1.0f, 1.0f); /* 809 */
            MapPutSetWork(hdl, (int)reg_p->labelID);                    /* 817 */
            MapPutSetMatrix(hdl, mat);                                  /* 819 */
        }
    }

    return 0;                                                           /* 820 */
}

/* Put-items differ from furniture in three ways: they carry a scale, they are
 * skipped entirely when not Visible, and their matrix is written straight into
 * the put-object rather than built separately. */
static int MapObjRegistPutFurn(int buff_id, int reg_id, int stat,
                               MB_OUT_SECTION *reg_p)
{                                                                       /* 833 */
    MDAT_PUT   *op = (MDAT_PUT *)reg_p;
    MAPOBJ_DAT *dp;
    float (*mat)[4][4];
    char name[36];
    void *hdl;
    int eff;
    int attr;

    if (MapObjGetListPtr(buff_id) == (MAPOBJ_HEAD *)0)                  /* 834 */
    {
        return 0;
    }
    if (op->Visible == 0)                                               /* 836 */
    {
        return 0;
    }

    FurnCtlGetMdoelName(name, op->ModelName);                           /* 853 */
    MapObjBiasPos(reg_id, stat, op->Pos);                               /* 855 */

    eff = MapObjCheckEffect(op->ModelName);                             /* 857 */

    if (eff != -1)
    {
        if (eff != -2)                                                  /* 897 */
        {
            op->ActionType = eff;
            op->Action     = -1;
            dp = MapObjAddDrawList(buff_id, reg_p, (char *)0,
                                   (float *)0, (float *)0, 11);         /* 900 */
            MapObjSetDrawEffect(dp, 1);                                 /* 901 */
        }
        return 0;
    }

    dp = MapObjAddDrawList(buff_id, reg_p, name, op->Pos, op->Rot, 11); /* 858 */
    if (dp == (MAPOBJ_DAT *)0)
    {
        return 0;
    }

    hdl = (void *)0;
    if (dp->obj_hdl != (void *)0)                                       /* 861 */
    {
        mat = MapPutGetMatrixPtr(dp->obj_hdl);                          /* 873 */
        MapObjSetPutMatrix(*mat, op->Pos, op->Rot,
                           op->Scale[0], op->Scale[1], op->Scale[2]);   /* 874 */
        hdl = dp->obj_hdl;                                              /* 881 */
    }

    MapObjUpdateFlg(hdl, op->Visible);                                  /* 885 */
    MapObjRegistMot(buff_id, dp, name, op->Action, op->ActionType,
                    op->Rot, op->Pos);                                  /* 888 */

    attr = FurnLoadGetAttr(name);                                       /* 891 */
    if (attr == 1)                                                      /* 893 */
    {
        MapObjNunoCtl(dp->obj_hdl, op->Action, op->ActionType);
    }
    else if (attr == 5)
    {
        MapObjBoneCtl((int)reg_p->labelID, op->Action, op->ActionType);
    }

    return 0;
}

static void MapObjCallFuncRecDat(int buff_id, int reg_id, int stat, int type,
                                 int (*func)(int, int, int, MB_OUT_SECTION *))
{                                                                       /* 906 */
    MB_OUT_SECTION *mp;

    RegDatGetStPtrStart(reg_id, type);                                  /* 910 */

    while ((mp = RegDatGetNextStPtr(reg_id)) != (MB_OUT_SECTION *)0)    /* 911 */
    {
        func(buff_id, reg_id, stat, mp);                                /* 913 */
    }
}

void MapObjRegistRegDatOne(int buff_id, int reg_id)                     /* 920 */
{
    MLOAD_HEAD *mp = MapLoadGetHeadPtr(buff_id);

    MapObjCallFuncRecDat(buff_id, reg_id, mp->stat, 7,  MapObjRegistDoor);
    MapObjCallFuncRecDat(buff_id, reg_id, mp->stat, 3,  MapObjRegistFurn);
    MapObjCallFuncRecDat(buff_id, reg_id, mp->stat, 11, MapObjRegistPutFurn);
}                                                                       /* 927 */

/* The alpha ramp is keyed on area rather than buffer, so teardown has to map
 * the buffer back to its areas first. */
static void MapObjDeleteAlphaChangeEffect(int buff_id)                  /* 935 */
{
    MLOAD_HEAD  *mp = MapLoadGetHeadPtr(buff_id);                       /* 937 */
    MB_OUT_HEAD *rp;
    int i;

    for (i = 0; i < 4; i++)                                             /* 939 */
    {
        if (mp->reg_id[i] == -1)
        {
            continue;
        }
        rp = RegDatGetHead((int)mp->reg_id[i]);
        EffectModelAlphaChangeDeleteGroup(rp->area_id);                 /* 946 */
    }
}

MDAT_DOOR *MapObjSetDoorDat(int b_id, int door_id)                      /* 958 */
{
    MLOAD_HEAD *mp = MapLoadGetHeadPtr(b_id);                           /* 959 */
    MDAT_DOOR  *dp;
    int i;

    if (mp == nullptr)
    {
        PRINT_ERROR("DEL_DOOR_ERR! CANNOTGET_HEADER PTR = NULL\n"); /* 960 */
        return nullptr;
    }

    if (mp->labelID < 0)                                                /* 965 */
    {
        return nullptr;
    }

    for (i = 0; i < 4; i++)                                             /* 971 */
    {
        if (mp->reg_id[i] == -1)
        {
            continue;
        }

        RegDatGetStPtrStart((int)mp->reg_id[i], 7);                     /* 972 */

        while ((dp = (MDAT_DOOR *)RegDatGetNextStPtr((int)mp->reg_id[i])) != nullptr)                                       /* 973 */
        {
            if (dp->DatID == door_id)                                   /* 974 */
            {
                PRINT_ERROR("DEL_DOOR_KEEP_DOOR id[%d]\n", door_id);         /* 977 */
                return dp;
            }
        }
    }

    return nullptr;                                              /* 981 */
}

/* Tears the buffer down.  The door case is the interesting one: a door the
 * player is walking through must survive into the room being entered, so its
 * record is copied into the *other* buffer's draw list -- model address, put
 * handle, position, rotation and hit state -- and only then dropped here. */
void MapObjDeletDraw(int buff_id)                                       /* 989 */
{
    MAPOBJ_HEAD *hp = MapObjGetListPtr(buff_id);
    MAPOBJ_DAT  *dp;
    MAPOBJ_DAT  *mop;
    MDAT_OBJ    *op;
    MDAT_DOOR   *work;
    char model_name[48];
    int now_id;
    int j;

    if (hp == nullptr)
    {
        return;
    }

    acsChodoDel(buff_id);                                               /* 991 */
    EffectLightComeInDeleteMapBuffId(buff_id);                          /* 992 */
    EffectWaterFlowDelete(buff_id);                                     /* 993 */
    MapObjDeleteAlphaChangeEffect(buff_id);                             /* 994 */
    EffectTourouFreaDelete(buff_id);                                    /* 995 */
    EffectTourouBaseDelete(buff_id);                                    /* 996 */
    MapObjBornDelete(buff_id);                                          /* 997 */
    MapSpObjRelease(buff_id);                                           /* 998 */

    for (j = 0; j < MAPOBJ_DAT_NUM; j++)                                /* 1002 */
    {
        dp = &hp->dat[j];
        op = (MDAT_OBJ *)dp->obj_ptr;

        if (op == nullptr)
        {
            continue;
        }

        if (dp->stat == 7)                                              /* 1012 */
        {
            now_id = buff_id ^ 1;                                       /* 1015 */

            if ((MapLoadGetHeadPtr(now_id) == nullptr) ||       /* 1018 */
                ((mop = MapObjGetFreeListSpacePtr(MapObjGetListPtr(now_id))) == nullptr))
            {
                PRINT_ERROR("DEL_DOOR ERR! NO_DRAW_LIST_PTR \n");            /* 1096 */
                dp->obj_ptr = nullptr;
                goto cleared;
            }

            work = MapObjSetDoorDat(now_id, ((MDAT_DOOR *)op)->DatID);  /* 1024 */
            if (work == nullptr)
            {
                dp->obj_ptr = nullptr;
                goto cleared;
            }

            FurnCtlGetMdoelName(model_name, ((MDAT_DOOR *)op)->ModelName);
            mop->mdl_addr = (int *)FurnCtlGetModelAddr(now_id, model_name);
            if (mop->mdl_addr == nullptr)                              /* 1035 */
            {
                PRINT_ERROR("DELL_DOOR ERR! NO_DOOR_MODEL_IN_PAK [%s]\n", model_name);/* 1036 */
            }

            mop->obj_ptr = work;                                        /* 1043 */
            mop->stat    = 7;
            mop->obj_hdl = dp->obj_hdl;

            work->Pos[0]   = ((MDAT_DOOR *)op)->Pos[0];                 /* 1048 */
            work->Pos[1]   = ((MDAT_DOOR *)op)->Pos[1];
            work->Pos[2]   = ((MDAT_DOOR *)op)->Pos[2];
            work->Rot[0]   = ((MDAT_DOOR *)op)->Rot[0];                 /* 1055 */
            work->Rot[1]   = ((MDAT_DOOR *)op)->Rot[1];
            work->Rot[2]   = ((MDAT_DOOR *)op)->Rot[2];
            work->HitCheck = ((MDAT_DOOR *)op)->HitCheck;               /* 1050 */

            if (mop->obj_hdl == nullptr)                              /* 1060 */
            {
                PRINT_ERROR("DELL_DOOR ERR! NO_REGIST_PUTOBJ [%s]\n", model_name);/* 1092 */
                dp->obj_ptr = nullptr;
                goto cleared;
            }

            MapPutSetModelPtr(mop->obj_hdl, (u_int *)mop->mdl_addr);    /* 1066 */

            if (MapMeiCheck(MapLoadGetHeadPtr(now_id)) == 0)            /* 1074 */
            {
                MapLightPreRenderObj(mop->obj_hdl, now_id);             /* 1075 */
            }
            else
            {
                MapPutSetLitPtr(mop->obj_hdl, MapMeiGetLight());        /* 1077 */
            }

            dp->obj_ptr = nullptr;                                    /* 1083 */
        }
        else
        {
            if (dp->stat == 3)                                          /* 1084 */
            {
                if (op->PhotoAble != 0)
                {
                    photo_datObjEnd(op);                                /* 1087 */
                }
            }
            else if (dp->stat != 11)                                    /* 1089 */
            {
                dp->obj_ptr = nullptr;
                goto cleared;
            }

            /* sdata 3eef80 -- the "eff_" prefix again; effect-backed entries
             * need their effect stopped before the entry is released. */
            if (strncmp(MapObjGetModelName(dp), "eff_", 4) == 0)        /* 1100 */
            {
                MapObjSetDrawEffect(dp, 0);                             /* 1101 */
            }
            dp->obj_ptr = nullptr;                                    /* 1102 */
        }

cleared:
        dp->mdl_addr = nullptr;                                        /* 1106 */
        dp->flg      = 0;
    }

    MapPutDelete(buff_id);                                              /* 1107 */
}

int MapObjRegistPhf(int buff_id, char *lit_addr)                        /* 1115 */
{
    MAPOBJ_HEAD *hp = MapObjGetListPtr(buff_id);                        /* 1116 */

    if (hp == (MAPOBJ_HEAD *)0)
    {
        PRINT_ERROR("NO_OBJLIST_WORK\n");                           /* 1117 */
        return -1;
    }

    hp->lit_addr = (int *)lit_addr;                                     /* 1122 */
    FurnCtlModelInit(buff_id);                                          /* 1123 */

    return 0;                                                           /* 1124 */
}

void MapObjRegInit(void)                                                /* 1130 */
{
    MapObjSceneLoadFlg = 0;
    MapObjDeletDraw(0);                                                 /* 1137 */
    MapObjDeletDraw(1);                                                 /* 1138 */
}
