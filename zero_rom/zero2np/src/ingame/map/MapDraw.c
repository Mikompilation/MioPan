// FILE: /home/zero_rom/zero2np/src/ingame/map/MapDraw.c
//
// Room drawing: the per-room-buffer half of the map pipeline.
//
// Three jobs live here.  The first is resource setup -- MapDrawRegistModel()
// pulls the room model and its two shadow variants out of the model pak,
// MapDrawInitRegDat() registers the room's registration files, and
// MapDrawInitRoom() ties them together with the light file and the height map.
// The second is teardown, MapDrawDeleteRoom() and the pieces under
// MapDrawDeleteNoDraw(), which is where every other map module gets told the
// buffer is going away.  The third is the actual draw, MapDrawRoom() ->
// MapDrawRoomOne(), plus the two MapDrawObj* helpers that MapPut and the
// effect code use to draw a single placed model through a caller-supplied
// matrix.
//
// Two room buffers exist (a door transition keeps both rooms resident), so
// everything here is indexed by buff_id and every static is a pair.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapDraw.o
// 0x00107278..0x001081af.

#include "MapDraw.h"

#include "FurnCtl.h"                            /* FurnCtlDelete*/FurnWorkFree */
#include "MapAnim.h"                            /* MapAnimDeleteBuffID / MapManimDelete */
#include "MapDoor.h"                            /* MapDoorDeleteBuff    */
#include "MapHit.h"                             /* MapHitDeleteGroup    */
#include "MapLight.h"                           /* MapMei* / MapLight*  */
#include "MapObjReg.h"                          /* MapObjRegist* / MapObjSetHitArea */
#include "MapSave.h"                            /* MapSaveSetMstDat     */
#include "RegDat.h"                             /* RegDat*              */
#include "map_height.h"                         /* MhInitMapHeight      */

#include "../../common/packfile.h"              /* GetNumInPak / GetFileInPak */
#include "../../common/utility.h"               /* _SetVector           */
#include "../../common/utility2.h"              /* PRINT_ERROR / PRINT_ASSERT */
#include "../../debug/DbFurnPre.h"              /* DbFurnPreSetBuffID   */
#include "../../graphics/effect/effect_oth.h"   /* EffectThunderLightSetRoomLight */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dConst.h"  /* g_v0000 / g_matConvertSI2PS */
#include "../../graphics/graph3d/gra3dLightData.h"
#include "../../graphics/graph3d/gra3dMisc.h"   /* gra3dGetProjectorSpot */
#include "../../graphics/graph3d/gra3dSGD.h"    /* _gra3dDrawSGD        */
#include "../../graphics/graph3d/gra3dSGDData.h"
#include "../../miopan/miopan_profiler.h"
#include "../../graphics/graph3d/gra3dShadow.h"
#include "../../graphics/obj_draw_ctrl.h"       /* GetRoomDrawFLG       */

#include <stddef.h>
#include <stdio.h>
#include "../../graphics/graph3d/g3dxVu0.h" /* g3dxVu0CopyVector */

enum
{
    MAP_DRAW_BUFF_NUM    = 2,       /* one per resident room                */
    MAP_DRAW_REG_DAT_NUM = 4,       /* MLOAD_HEAD::reg_id capacity          */

    /* Room 37 is the one room whose light file leaves the red channel too
     * dark; MapDrawInitRoom() lifts it by hand.  Distinct from the "Mei"
     * room, which MapMeiCheck() picks out by label instead. */
    MAP_DRAW_SP_AMBIENT_ROOM_NO = 0x25
};

/* .lit4 3ed82c / 3ed830.  Both read straight out of the ELF -- the values are
 * far lower than they look like they should be, but the room light data is
 * scaled up again downstream. */
#define MAP_DRAW_AMBIENT_MIN    0.002f          /* lit4 3ed82c */
#define MAP_DRAW_AMBIENT_SP_R   0.024f          /* lit4 3ed830 */

/* The room light MapDrawInitRoom() builds: the room's own light data with the
 * room origin folded into every light position. */
static GRA3DLIGHTDATA MapDrawPS2CoordLight[MAP_DRAW_BUFF_NUM];          /* bss 3fe3b0 */

/* Scene-supplied override.  When set (scene.c hands one over for the cutscene
 * path) it wins over the per-room light in MapDrawGetLightPtr4BuffID().  The
 * misspelling is the original's. */
static GRA3DLIGHTDATA *MapDrawRoomLihgtSp[MAP_DRAW_BUFF_NUM];           /* sdata 3eed50 */

/* Lives in .sdata, not .sbss -- it boots at 1, so the room draw does the full
 * light setup unless scene.c explicitly turns it off. */
static int s_bEnableLightFlashlightOnly = 1;                            /* sdata 3eed58 */

/* --------------------------------------------------------------------------
 *  Room coordinate setup
 * ------------------------------------------------------------------------ */

/* Parks the room model at `pos` in PS2 space: the SI->PS2 basis change with
 * the position dropped into the translation row, then a full bone pass so the
 * static room geometry has world matrices. */
void MapDrawCalcRoomCoord(void *sgd_top, float *pos)
{                                                                       /* 72 */
    SGDFILEHEADER *pFH = (SGDFILEHEADER *)sgd_top;

    if (sgd_top == nullptr)                                           /* 72 */
    {
        PRINT_ERROR("ADDR_NULL\n");                                     /* 73 */
        return;
    }

    SGDCOORDINATE* cp = pFH->pCoord;                                                   /* 77 */

    sceVu0CopyMatrix(cp->matCoord, g_matConvertSI2PS);
    g3dxVu0CopyVector(cp->matCoord[3], pos);

    sgdCalcBoneCoordinate(cp, (int)pFH->uiNumBlock - 1);                /* 82 */
}

/* Stamps `mat` into both the coordinate and the local-world matrix of block 0.
 * No validation at all in the original -- callers hand it a model they have
 * already resolved. */
void MapDrawSetMatrixSGD(void *sgd_top, float mat[4][4])
{                                                                       /* 88 */
    SGDCOORDINATE *cp = ((SGDFILEHEADER *)sgd_top)->pCoord;

    sceVu0CopyMatrix(cp->matCoord, mat);
    sceVu0CopyMatrix(cp->matLocalWorld, mat);
}

void MapDrawSGD(void *sgd_top)
{                                                                       /* 95 */
    _gra3dDrawSGD((SGDFILEHEADER *)sgd_top, SRT_REALTIME, nullptr, -1); /* 97 */
}

/* Centre of the model's bounding box, in the world units the map records use
 * (the +-25 scale and the Y flip are the SI->PS2 convention).  MapPut caches
 * the result per placed object. */
int MapDrawGetCenPos(void *sgd_top, float (*rvec)[4])
{                                                                       /* 104 */
    float h_pos[8][4];

    sgdGetBoundingBox((SGDFILEHEADER *)sgd_top, h_pos);                 /* 108 */

    sceVu0AddVector(*rvec, h_pos[0], h_pos[7]);                         /* 129 */
    sceVu0ScaleVector(*rvec, *rvec, 0.5f);                              /* 130 */

    (*rvec)[0] *= 25.0f;                                                /* 132 */
    (*rvec)[1] *= -25.0f;                                               /* 133 */
    (*rvec)[2] *= 25.0f;                                                /* 134 */
    (*rvec)[3]  = 1.0f;                                                 /* 135 */

    return 0;                                                           /* 139 */
}

/* --------------------------------------------------------------------------
 *  Light lookup
 * ------------------------------------------------------------------------ */

/* Installs a scene-wide light override.  A buffer with no room loaded gets a
 * null rather than `lp`, so the override dies with the room. */
void MapDrawSetSpRoomLight(GRA3DLIGHTDATA *lp)
{                                                                       /* 146 */
    for (int i = 0; i < MAP_DRAW_BUFF_NUM; i++)                         /* 149 */
    {
        if (MapLoadGetHeadPtr(i) == nullptr)                            /* 150 */
        {                                                               /* 151 */
            MapDrawRoomLihgtSp[i] = nullptr;                            /* 152 */
        }
        else
        {
            MapDrawRoomLihgtSp[i] = lp;                                 /* 154 */
        }
    }
}                                                                       /* 156 */

/* The raw per-buffer room light, ignoring both the scene override and the Mei
 * special case.  Unlike its siblings this one range-checks, because the
 * prelight path calls it with a buff_id that may be -1. */
GRA3DLIGHTDATA *MapDrawGetLightPtr4BuffID2(int buff_id)
{                                                                       /* 162 */
    if ((u_int)buff_id < MAP_DRAW_BUFF_NUM)                             /* 163 */
    {
        return &MapDrawPS2CoordLight[buff_id];                          /* 164 */
    }

    PRINT_ERROR("NO BUFF_ID[%d]*************\n", buff_id);              /* 166 */

    return nullptr;                                         /* 167 */
}

/* The light the renderer should actually use for `buff_id`: the scene override
 * if one is installed, otherwise the Mei light or the room light.  No range
 * check -- every caller comes through MapLoadGetBuffID(). */
GRA3DLIGHTDATA *MapDrawGetLightPtr4BuffID(int buff_id)
{                                                                       /* 171 */
    MLOAD_HEAD *hp;

    if (MapDrawRoomLihgtSp[buff_id] != nullptr)             /* 174 */
    {
        return MapDrawRoomLihgtSp[buff_id];
    }

    hp = MapLoadGetHeadPtr(buff_id);                                    /* 177 */
    if (hp == nullptr || hp->labelID < 0)                       /* 178 */
    {
        return nullptr;
    }

    if (MapMeiCheck(hp) != 0)                                           /* 181 */
    {
        return MapMeiGetLight();
    }

    return &MapDrawPS2CoordLight[buff_id];                              /* 183 */
}                                                                       /* 184 */

GRA3DLIGHTDATA *MapDrawGetLightPtr(int room_no)
{                                                                       /* 192 */
    int buff_id = MapLoadGetBuffID(room_no);

    if (buff_id >= 0)
    {
        return MapDrawGetLightPtr4BuffID(buff_id);                      /* 193 */
    }

    return nullptr;
}

/* --------------------------------------------------------------------------
 *  Room resources
 * ------------------------------------------------------------------------ */

/* Registers every registration file in the room's reg-dat pak with RegDat and
 * records the ids in the header.  RegDatRegist()'s return is stored raw: a
 * failure comes back negative and lands in the slot as such, which is the
 * same -1 the clear loop wrote. */
int MapDrawInitRegDat(MLOAD_HEAD *hp)
{                                                                       /* 200 */
    for (int i = 0; i < MAP_DRAW_REG_DAT_NUM; i++)                      /* 203 */
    {
        hp->reg_id[i] = -1;                                             /* 205 */
    }

    int num = GetNumInPak((void *)hp->reg_dat_top);                         /* 208 */

    if (num > MAP_DRAW_REG_DAT_NUM)                                     /* 210 */
    {
        PRINT_ERROR("REG_DAT_NUM_MAX_OVER\n");                          /* 211 */
        num = MAP_DRAW_REG_DAT_NUM;
    }

    for (int i = 0; i < num; i++)                                       /* 215 */
    {
        char *reg_dat = (char *)GetFileInPak((void *)hp->reg_dat_top, i); /* 217 */

        hp->reg_id[i] = (char)RegDatRegist(reg_dat);                    /* 219, 220 */
    }

    return 0;                                                           /* 221 */
}

/* File 0 of the model pak is the room, 1 the shadow source and 2 the shadow
 * receiver.  sgdRemap() is called unconditionally on the room -- it guards its
 * own null and version checks -- and only when present on the other two. */
int MapDrawRegistModel(MLOAD_HEAD *hp)
{                                                                       /* 226 */
    hp->model_addr    = (uintptr_t)GetFileInPak((void *)hp->model_pak_addr, 0); /* 228 */
    hp->shadow_s_addr = (uintptr_t)GetFileInPak((void *)hp->model_pak_addr, 1); /* 229 */
    hp->shadow_addr   = (uintptr_t)GetFileInPak((void *)hp->model_pak_addr, 2); /* 230 */

    sgdRemap((SGDFILEHEADER *)hp->model_addr);                          /* 233 */

    if (hp->shadow_s_addr != 0)                                         /* 236 */
    {
        sgdRemap((SGDFILEHEADER *)hp->shadow_s_addr);                   /* 237 */
    }

    if (hp->shadow_addr != 0)                                           /* 240 */
    {
        sgdRemap((SGDFILEHEADER *)hp->shadow_addr);                     /* 241 */
    }

    return 0;                                                           /* 251 */
}

/* Everything a freshly streamed room needs before it can be drawn: models,
 * origin, light data and height map.
 *
 * The assert is not a guard -- `rp` is dereferenced straight after it either
 * way, which is the original's behaviour.  A room whose first registration
 * file has no header is unrecoverable. */
int MapDrawInitRoom(MLOAD_HEAD *hp)
{                                                                       /* 256 */
    MioPanProfileScope profile_scope(MIOPAN_PROFILE_ROOM_INIT);

    MapDrawRegistModel(hp);                                             /* 259 */

    MB_OUT_HEAD* rp = RegDatGetHead((int)hp->reg_id[0]);
    if (rp == nullptr)                                                  /* 265 */
    {
        PRINT_ASSERT("CAN_NOT_GET_REG_DAT_HEAD\n");                     /* 266 */
    }

    g3dxVu0CopyVector(hp->pos, rp->Pos);

    sgdVerifyLightData(&hp->lit_dat, (ZERO2LIGHTDATAFILE *)hp->lit_addr); /* 274 */

    /* A light file with no ambient at all would render the room pitch black,
     * so a floor is applied. */
    if (hp->lit_dat.vAmbient[0] == 0.0f &&                              /* 277 */
        hp->lit_dat.vAmbient[1] == 0.0f &&
        hp->lit_dat.vAmbient[2] == 0.0f)
    {
        hp->lit_dat.vAmbient[0] = MAP_DRAW_AMBIENT_MIN;                 /* 281 */
        hp->lit_dat.vAmbient[1] = MAP_DRAW_AMBIENT_MIN;                 /* 282 */
        hp->lit_dat.vAmbient[2] = MAP_DRAW_AMBIENT_MIN;                 /* 283 */
    }

    if (MapLoadGetRoomNo4BuffID(hp->buff_id) == MAP_DRAW_SP_AMBIENT_ROOM_NO) /* 287 */
    {
        hp->lit_dat.vAmbient[0] = MAP_DRAW_AMBIENT_SP_R;                /* 288 */
    }

    gra3dLightDataAddOffsetPosition(&MapDrawPS2CoordLight[hp->buff_id],
                                    &hp->lit_dat, hp->pos);             /* 295 */

    if (MapMeiCheck(hp) != 0)                                           /* 298 */
    {
        MapMeiInit(&MapDrawPS2CoordLight[hp->buff_id]);                 /* 300 */
    }
    else
    {
        gra3dSetLightData(&MapDrawPS2CoordLight[hp->buff_id], nullptr); /* 302 */
    }

    /* rp->Pos, not hp->pos -- the same four floats, but this is the ROM's
     * argument. */
    if (hp->high_addr != 0)                                             /* 306 */
    {
        MhInitMapHeight(hp->high_addr, rp->Pos, hp->buff_id);           /* 307 */
    }

    return 0;                                                           /* 310 */
}

/* Second half of the room load: hand the registration data to MapObjReg so the
 * furniture, doors and put-items become draw entries, then mark the buffer
 * populated (stat bit 2). */
void MapDrawInitFurn(MLOAD_HEAD *hp)
{                                                                       /* 315 */
    MioPanProfileScope profile_scope(MIOPAN_PROFILE_ROOM_FURN);
    const int room_already_populated = (hp->stat & 4) != 0;

    gra3dSetLightData(&MapDrawPS2CoordLight[hp->buff_id], nullptr);  /* 321 */

    DbFurnPreSetBuffID(hp->buff_id);                                    /* 324 */
    MapObjRegistPhf(hp->buff_id, (char *)hp->lit_addr);                 /* 327 */
    MapLoadInitFreeMem(hp->buff_id);                                    /* 330 */
    
    /* Draw-disable tears down the temporary MapPut/object handles, but the
     * room's shared furniture SGDs and their baked vertex colours remain
     * resident.  Clearing this flag on every re-enable made registration run
     * gra3dExecPrelight over all of those models again.  A real room overwrite
     * resets stat to 3 in MapLoadRegistReq(), so only a new generation needs
     * its baked-light flags cleared.  Private/live-lit and animation-specific
     * registrations still rebuild their own state below. */
    if (!room_already_populated)
    {
        FurnCtlDeleteDrawFlgAll(hp->buff_id);                            /* 332 */
    }

    {
        MioPanProfileScope registration_scope(MIOPAN_PROFILE_ROOM_REGISTRATION);

        for (int i = 0; i < MAP_DRAW_REG_DAT_NUM; i++)                  /* 335 */
        {
            int reg_id = (int)hp->reg_id[i];

            if (reg_id != -1)                                           /* 338 */
            {
                MapSaveSetMstDat(reg_id);                               /* 341 */
                MapObjRegistRegDatOne(hp->buff_id, reg_id);             /* 343 */
                MapObjSetHitArea(reg_id);                               /* 345 */
            }
        }
    }                                                                   /* 346 */

    hp->stat |= 4;                                                      /* 349 */
}

void MapDrawPreLight(MLOAD_HEAD *hp)
{                                                                       /* 354 */
    for (int i = 0; i < MAP_DRAW_REG_DAT_NUM; i++)                      /* 358 */
    {
        if (hp->reg_id[i] != -1)
        {
            MapLightRePreRender(hp->buff_id, (int)hp->reg_id[i]);       /* 360 */
        }
    }                                                                   /* 362 */
}                                                                       /* 363 */

/* --------------------------------------------------------------------------
 *  Teardown
 * ------------------------------------------------------------------------ */

void MapDrawDelRegDatAll(MLOAD_HEAD *hp)
{                                                                       /* 370 */
    for (int i = 0; i < MAP_DRAW_REG_DAT_NUM; i++)                      /* 373 */
    {
        if (hp->reg_id[i] != -1)
        {
            RegDatDeleteBuffList((int)hp->reg_id[i]);
            hp->reg_id[i] = -1;
        }
    }
}                                                                       /* 377 */

/* Drops the hit groups the room's registration files created.  Each reg-dat
 * header carries the area id its rectangles were filed under. */
static void MapDrawDeleteHit(int buff_id)
{                                                                       /* 382 */
    MLOAD_HEAD *hp = MapLoadGetHeadPtr(buff_id);                        /* 384 */

    for (int i = 0; i < MAP_DRAW_REG_DAT_NUM; i++)                      /* 386 */
    {
        if (hp->reg_id[i] != -1)
        {
            MapHitDeleteGroup(RegDatGetHead((int)hp->reg_id[i])->area_id); /* 391 */
        }
    }                                                                   /* 392 */
}

/* Everything that has to be told a room buffer is going away, in the order the
 * original ran it -- doors first (MapObjDeletDraw hands a door being walked
 * through to the other buffer, so the door list has to be settled by then),
 * the furniture work area last. */
void MapDrawDeleteNoDraw(int buff_id)
{
    MapDoorDeleteBuff(buff_id);                                         /* 398 */
    MapDrawDeleteHit(buff_id);                                          /* 399 */
    MapManimDelete(buff_id);                                            /* 400 */
    MapAnimDeleteBuffID(buff_id);                                       /* 401 */
    FurnCtlDeleteManimFlgAll(buff_id);                                  /* 402 */
    MapObjDeletDraw(buff_id);                                           /* 403 */
    FurnWorkFree(buff_id);                                              /* 404 */
}

void MapDrawDeleteRoom(MLOAD_HEAD *hp)
{
    MapDrawDelRegDatAll(hp);                                            /* 410 */
    MapDrawDeleteNoDraw(hp->buff_id);                                   /* 411 */
    FurnCtlClearBuff(hp->buff_id);                                      /* 412 */
}

void MapDrawDeleteRoomAll(void)
{                                                                       /* 417 */
    MapDrawDeleteRoom(MapLoadGetHeadPtr(0));                            /* 421, 422 */
    MapDrawDeleteRoom(MapLoadGetHeadPtr(1));                            /* 423, 424 */
}

/* --------------------------------------------------------------------------
 *  Single-object draw
 *
 *  The coordinate array of a placed model sits immediately behind its file
 *  header, so a sane SGD has pCoord quadword-aligned and within 0x200 bytes of
 *  the top.  Anything else is a mis-resolved address and is reported rather
 *  than drawn.
 * ------------------------------------------------------------------------ */

void MapDrawObj(void *top, float mat[4][4])
{
    SGDFILEHEADER *pFH = (SGDFILEHEADER *)top;
    SGDCOORDINATE *cp  = pFH->pCoord;                                   /* 433 */
    ptrdiff_t      distance = (char *)cp - (char *)top;

    if (distance < 0)
    {
        distance = -distance;
    }

    if (((uintptr_t)cp & 0xf) != 0 || distance >= 0x201)                /* 436 */
    {
        /* ROM prints %x -- pointers were 4 bytes there. */
        PRINT_ERROR("NO_SGD_FILE_FORMAT addr[%p]\n", top);              /* 437 */
        return;
    }

    sceVu0CopyMatrix(cp->matCoord, mat);
    sgdCalcBoneCoordinate(cp, (int)pFH->uiNumBlock - 1);                /* 443 */
    _gra3dDrawSGD(pFH, SRT_REALTIME, nullptr, -1);           /* 444 */
    gra3dshadowAddProjectModel(pFH);                                    /* 448 */
}

/* Same, minus the shadow projection.  Used by the light-come-in effect, whose
 * models must not cast.
 *
 * The ROM's line table maps this body onto the same 433..448 span as
 * MapDrawObj -- the two were emitted from one shared source region -- so the
 * annotations below are literally what the binary reports, not a guess at
 * where the second copy sat. */
void MapDrawObjNoShadow(void *top, float mat[4][4])
{
    SGDFILEHEADER *pFH = (SGDFILEHEADER *)top;
    SGDCOORDINATE *cp  = pFH->pCoord;                                   /* 433 */
    ptrdiff_t      distance = (char *)cp - (char *)top;

    if (distance < 0)
    {
        distance = -distance;
    }

    if (((uintptr_t)cp & 0xf) != 0 || distance >= 0x201)                /* 436 */
    {
        PRINT_ERROR("NO_SGD_FILE_FORMAT addr[%p]\n", top);              /* 437 */
        return;
    }

    sceVu0CopyMatrix(cp->matCoord, mat);
    sgdCalcBoneCoordinate(cp, (int)pFH->uiNumBlock - 1);                /* 443 */
    _gra3dDrawSGD(pFH, SRT_REALTIME, nullptr, -1);                      /* 444 */
}                                                                       /* 448 */

/* --------------------------------------------------------------------------
 *  Room draw
 * ------------------------------------------------------------------------ */

void MapDrawSetUpRoomCoordinate(MLOAD_HEAD *hp)
{                                                                       /* 466 */
    float m_pos[4];

    _SetVector(m_pos, hp->pos[0], hp->pos[1], hp->pos[2], 1.0f);        /* 470 */

    MapDrawCalcRoomCoord((void *)hp->model_addr, m_pos);                /* 472 */

    if (hp->shadow_addr != 0)                                           /* 475 */
    {
        MapDrawCalcRoomCoord((void *)hp->shadow_addr, m_pos);           /* 476 */
    }

    if (hp->shadow_s_addr != 0)                                         /* 479 */
    {
        MapDrawCalcRoomCoord((void *)hp->shadow_s_addr, m_pos);         /* 480 */
    }
}

/* `lp` is accepted and ignored, as in the original -- the shadow source is
 * drawn with whatever light state MapDrawRoomOne() has already applied. */
void MapDrawShadowOne(MLOAD_HEAD *hp, GRA3DLIGHTDATA *lp)
{                                                                       /* 488 */
    (void)lp;

    if (hp->shadow_s_addr != 0)
    {
        _gra3dDrawSGD((SGDFILEHEADER *)hp->shadow_s_addr, SRT_REALTIME, nullptr, -1); /* 491 */
    }
}

/* Draws one resident room.
 *
 * The light block only runs while flashlight-only mode is on, which is the
 * boot state.  Outside the Mei room it rebuilds the lighting from scratch each
 * frame: everything off, the player's flashlight back on, then the projector
 * spot if the scene has one -- with its direction negated, because gra3d wants
 * the direction light travels and the projector stores where it points. */
void MapDrawRoomOne(MLOAD_HEAD *hp, GRA3DLIGHTDATA *lp)
{                                                                       /* 502 */
    if (hp->labelID < 0)                                                /* 502 */
    {
        return;
    }

    SGDFILEHEADER* pFH = (SGDFILEHEADER *)hp->model_addr;               /* 504 */

    if (pFH == nullptr)                                                 /* 506 */
    {
        return;
    }

    if (pFH->uiVersionId != SGD_VALID_VERSIONID)                        /* 508 */
    {
        return;
    }

    if (pFH->pCoord->bCalc == 0)                                        /* 510, 511 */
    {
        MapDrawSetUpRoomCoordinate(hp);                                 /* 512 */
    }

    if (s_bEnableLightFlashlightOnly != 0)                              /* 516 */
    {
        if (MapMeiCheck(hp) != 0)                                       /* 518 */
        {
            gra3dLightEnableAll(1);                                     /* 519 */

            if (MapMeiProc() < 0)                                       /* 521 */
            {
                PRINT_ERROR("NO_MEIMETU_LIGHT\n");                      /* 522 */
            }
        }
        else
        {
            gra3dLightEnableAll(0);                                     /* 527 */
            MapLightSetPlayerReal();                                    /* 529 */

            if (gra3dIsSpecialLightActive() != 0)                       /* 531 */
            {
                G3DLIGHT     L = gra3dGetProjectorSpot();               /* 533 */

                /* PORT DEVIATION -- the ROM negates L.vDirection here and the
                 * port does not, for the same reason MapLightSetPlayerReal()
                 * no longer does: vDirection is the BEAM engine-wide and the
                 * VU1 kernel transcriptions negate it themselves.
                 * s_ProjectorSpot is copied verbatim out of the room light
                 * block (gra3dMisc.c line 515), so it is already in that
                 * convention.  See vu1/LIGHTING.md section 3.3. */

                GRA3DLIGHTID lightId = gra3dGetProjectorSpotId();       /* 536 */
                gra3dSetLight(lightId, &L);                             /* 537 */
                gra3dLightEnable(lightId, 1);                           /* 538 */
            }

            EffectThunderLightSetRoomLight();                           /* 541 */
            gra3dSetAmbient(MapDrawPS2CoordLight[hp->buff_id].vAmbient); /* 543 */
            gra3dApplyLight();                                          /* 544 */
        }
    }

    if (GetRoomDrawFLG() != 0)                                          /* 551 */
    {
        _gra3dDrawSGD((SGDFILEHEADER *)hp->model_addr, SRT_REALTIME, nullptr, -1);
    }

    if (GetSdwSrcDrawFLG() != 0)                                        /* 554 */
    {
        MapDrawShadowOne(hp, lp);
    }

    gra3dshadowAddProjectModel((SGDFILEHEADER *)hp->model_addr);        /* 567 */
}                                                                       /* 573 */

void MapDrawRoom(void)
{                                                                       /* 578 */
    for (int i = 0; i < MAP_DRAW_BUFF_NUM; i++)                         /* 583 */
    {
        if (MapLoadCheckDrawFlg(i) != 0)                                /* 585 */
        {
            MLOAD_HEAD *hp = MapLoadGetHeadPtr(i);                      /* 588 */

            MapDrawRoomOne(hp, MapDrawRoomLihgtSp[i]);                  /* 590 */
        }
    }                                                                   /* 591 */
}

void MapDrawEnableFlashlightOnly(int b)
{
    s_bEnableLightFlashlightOnly = b;                                   /* 601 */
}

int MapDrawIsEnableFlashlightOnly(void)
{
    return s_bEnableLightFlashlightOnly;                                /* 610 */
}
