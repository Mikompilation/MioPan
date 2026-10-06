/* ==========================================================================
 *  ingame/map/MapLight.c
 *
 *  Room lighting.  Four jobs live here:
 *
 *    - the player's flashlight, folded into whichever block is about to be
 *      handed to gra3d (MapLightSetPlayer* / MapLightMakeRoomReal);
 *    - the pre-render pass, which bakes the room light into a placed object's
 *      vertices once instead of lighting it every frame (MapLightRePreRender
 *      and friends);
 *    - light selection.  The GS has 3 directional, 16 point and 16 spot slots,
 *      so when two rooms are resident at once their lights compete for the
 *      same ids.  MapLightSelect() thins one block down; MapLightMakeDual()
 *      merges two of them by ranking every candidate with MapLightGetPower();
 *    - the "Mei" room's flicker, an animation table that scales the whole
 *      room light up and down each frame.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), MapLight.o.
 * ======================================================================== */

#include "MapLight.h"

#include "FurnCtl.h"                                /* FurnCtlGetFlgPtr etc */
#include "MapDraw.h"                                /* MapDrawGetLightPtr4BuffID */
#include "MapObj.h"                                 /* MapObjCheckEffect    */
#include "MapObjReg.h"                              /* MapObjGetLightFlg    */
#include "MapPut.h"                                 /* MapPutGet*Ptr        */
#include "RegDat.h"                                 /* RegDatGetNextStPtr   */

#include "../../common/utility.h"                   /* GetDistV2            */
#include "../../common/utility2.h"                  /* PRINT_ERROR / PRINT_ASSERT */
#include "../../common/variable.h"                  /* plyr_wrk             */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../graphics/graph3d/g3ddbg.h"          /* G3DASSERT            */
#include "../../graphics/graph3d/g3dxVu0.h"         /* g3dxVu0CalcLength    */
#include "../../graphics/graph3d/gra3d.h"
#include "../../graphics/graph3d/gra3dConst.h"      /* g_v0000              */
#include "../../graphics/graph3d/gra3dSGD.h"        /* _gra3dDrawSGD        */
#include "../../graphics/graph3d/sgd_types.h"       /* SGDFILEHEADER        */
#include "../../sdk/libvu0.h"
#include "../../system/eeiop/cddat.h"               /* RKS10_PK2            */

#include <string.h>                                 /* strcmp               */

/* sdata 3eee40 / 3eee48 / 3eee50 -- read out of the ELF, not guessed. */
float MapLightPower     = 0.999999f;
float MapLightIntens[2] = { 0.8f, 0.8f };
float MapLightDiff[2]   = { 0.5f, 0.5f };

static void  MapLigtPreRenderOne(int buff_id, char *name);
static void  MapLightPreRenderType(int buff_id, int reg_id, int type);
static float MapLightGetPower(G3DLIGHT *pLightDat, float *vChrPos);
static int   MapLightSetSortDat(int l_num, int st, int reg_cnt, float *pos,
                                MAP_LIGHT_SORT *llist, GRA3DLIGHTDATA *lp);
static int   MapLightMakeDualSub(MAP_LIGHT_DAT *w_light, GRA3DLIGHTDATA *in1,
                                 GRA3DLIGHTDATA *in2, int st, int en,
                                 float *pos);
static void  MapLightClearPointDirection(GRA3DLIGHTDATA *pLight);
static int   MapLightCheckDirectLight(GRA3DLIGHTDATA *pLight);
static void  MapMeiAnimLight(GRA3DLIGHTDATA *out, GRA3DLIGHTDATA *mst, int max);
static int   MapMeiAnimFrame(MAPMEI_HEAD *hp);

/* --------------------------------------------------------------------------
 *  MapLightLed
 *
 *  Debug aid: paint every point and spot slot's diffuse red channel full.  The
 *  range is the ROM's own -- it starts at LID_POINT_0 and stops one short of
 *  LID_SPOT_15, so the last spot slot keeps its colour.
 * ------------------------------------------------------------------------ */
void MapLightLed(GRA3DLIGHTDATA *light)
{                                                                       /* 53 */
    for (int i = LID_POINT_0; i < LID_SPOT_15; i++)                     /* 57 */
    {
        light->aLight[i].vDiffuse[0] = 255.0f;                          /* 59 */
    }                                                                   /* 60 */
}

/* --------------------------------------------------------------------------
 *  MapLightPreRenderObj / MapLightPreRenderObj2
 *
 *  Bake the room's light into one already-placed object.
 * ------------------------------------------------------------------------ */
void MapLightPreRenderObj(void *hdl, int buff_id)
{                                                                       /* 67 */
    gra3dSetLightData(MapDrawGetLightPtr4BuffID(buff_id), nullptr);     /* 68 */

    MapLightSetLight(buff_id, (int *)MapPutGetModelPtr(hdl), hdl, 0);   /* 70 */
}

void MapLightPreRenderObj2(void *hdl, int room_no)
{                                                                       /* 75 */
    MapLightPreRenderObj(hdl, MapLoadGetBuffID(room_no));               /* 76 */
}

/* --------------------------------------------------------------------------
 *  MapLightSetPlayerOnly
 *
 *  Kill every light except the player's own.  fl is the flashlight -- point or
 *  spot depending on the mode -- and fl2 the secondary point light; a Type of
 *  G3DLIGHTTYPE_FORCE_DWORD means "no light".
 * ------------------------------------------------------------------------ */
void MapLightSetPlayerOnly(void)
{                                                                       /* 105 */
    gra3dLightEnableAll(0);                                             /* 106 */

    if (plyr_wrk.fl.Type != G3DLIGHTTYPE_FORCE_DWORD)
    {
        int iLightId = plyr_wrk.fl.Type == G3DLIGHT_POINT
                       ? LID_POINT_FLASHLIGHT_0
                       : LID_SPOT_FLASHLIGHT;
        gra3dLightEnable(iLightId, 1);                                  /* 111 */
    }

    if (plyr_wrk.fl2.Type == G3DLIGHT_POINT)
    {
        gra3dLightEnable(LID_POINT_FLASHLIGHT_1, 1);                    /* 116 */
    }
}

/* --------------------------------------------------------------------------
 *  MapLightSetPlayerReal
 *
 *  Push the player's live flashlight values into the gra3d slots.  A spot
 *  light's direction is negated on the way in: plyr_wrk stores it pointing
 *  the way the player faces, gra3d wants it pointing back at the surface.
 * ------------------------------------------------------------------------ */
void MapLightSetPlayerReal(void)
{                                                                       /* 122 */
    G3DLIGHT L;
    int      iLightId;

    if (plyr_wrk.fl.Type == G3DLIGHTTYPE_FORCE_DWORD)
    {
        gra3dLightEnable(LID_SPOT_FLASHLIGHT, 0);                       /* 127 */
        gra3dLightEnable(LID_POINT_FLASHLIGHT_0, 0);
    }
    else
    {
        iLightId = plyr_wrk.fl.Type == G3DLIGHT_POINT
                       ? LID_POINT_FLASHLIGHT_0
                       : LID_SPOT_FLASHLIGHT;

        L = plyr_wrk.fl;                                                /* 130 */

        /* PORT DEVIATION -- the ROM negates the spot direction here (line 131,
         * `vmulx.xyz vf12,vf12,vf13` against -1.0 at 0x00109b30) and the port
         * does not.
         *
         * The ROM's realtime cone really does open along -vDirection:
         * CalcIntens (vu1/ff2_00.vsm 0x0b0-0x0d8) dots vDirection against
         * `spotPos - vertex`.  So this negation is correct *for the VU1*, and
         * it was briefly restored on that basis.  It is reverted, because the
         * ROM does not apply the same sense everywhere and the port cannot
         * afford to be half-converted: the authored room spots, the prelight's
         * g3dCalcSpotlightFalloff(), _IsBBLightingupSpot()'s bounding-box gate
         * and SceneSetHandSpotLightToPlyrWrk()'s install of this very light
         * (swapped there for the same reason) all read
         * vDirection as the BEAM.
         *
         * Restoring only this site and MapDrawRoomOne()'s projector left
         * _IsBBLightingupSpot() accepting boxes behind the light and rejecting
         * the ones the beam reaches, so meshes flipped between lit and black
         * as the camera moved.  The port therefore keeps one convention
         * engine-wide -- vDirection is the beam -- and the four VU1 kernel
         * transcriptions negate it themselves.  vu1/LIGHTING.md section 3.3. */

        gra3dSetLight(iLightId, &L);
        gra3dLightEnable(iLightId, 1);
    }

    if (plyr_wrk.fl2.Type == G3DLIGHT_POINT)                            /* 141 */
    {
        L = plyr_wrk.fl2;                                               /* 146 */
        gra3dSetLight(LID_POINT_FLASHLIGHT_1, &L);
        gra3dLightEnable(LID_POINT_FLASHLIGHT_1, 1);                    /* 147 */
    }
    else
    {
        gra3dLightEnable(LID_POINT_FLASHLIGHT_1, 0);
    }
}                                                                       /* 151 */

/* --------------------------------------------------------------------------
 *  MapLightMakeRoomReal
 *
 *  Copy the room block and overwrite the flashlight slot with the player's
 *  live light, scaled down by the mode-dependent tuning.  Index 1 of
 *  MapLightIntens/Diff is the camera-finder mode (plyr_wrk.cmn_wrk.mode == 6),
 *  where the flashlight is dimmed so the viewfinder stays readable.
 * ------------------------------------------------------------------------ */
void MapLightMakeRoomReal(GRA3DLIGHTDATA *LD, GRA3DLIGHTDATA *mst)
{                                                                       /* 157 */
    GRA3DLIGHTID flashlightId;
    G3DLIGHT    *light;
    int          finder_mode;

    flashlightId = plyr_wrk.fl.Type == G3DLIGHT_POINT
                       ? LID_POINT_FLASHLIGHT_0
                       : LID_SPOT_FLASHLIGHT;
    finder_mode  = plyr_wrk.cmn_wrk.mode == 6;

    *LD = *mst;                                                         /* 163 */

    if (plyr_wrk.fl.Type == G3DLIGHTTYPE_FORCE_DWORD)                   /* 165 */
    {
        LD->aStatus[flashlightId].bEnable = 0;
        LD->aStatus[flashlightId].bEmulateToDirectionalLight = 0;
    }
    else
    {
        light  = &LD->aLight[flashlightId];
        *light = plyr_wrk.fl;

        gra3dSetLightIntens(light, light->afPad0[0] * MapLightIntens[finder_mode]);
        sceVu0ScaleVector(light->vDiffuse, light->vDiffuse, MapLightDiff[finder_mode]);

        LD->aStatus[flashlightId].bEnable = 1;
        LD->aStatus[flashlightId].bEmulateToDirectionalLight = 1;
    }
}                                                                       /* 173 */

/* --------------------------------------------------------------------------
 *  MapLightSetLight
 *
 *  pre_flg 0 -- mark the object pre-lit (flag 0x20) and bake the current gra3d
 *  light into its model vertices right now.
 *  pre_flg 1 -- clear that flag and point the object at the room's live light
 *  block instead, so it is lit per frame.
 * ------------------------------------------------------------------------ */
void MapLightSetLight(int buff_id, int *mdl_addr, void *obj_hdl, int pre_flg)
{
    if (pre_flg == 0)                                                   /* 185 */
    {
        *(u_int *)MapPutGetFlgPtr(obj_hdl) |= 0x20;                     /* 188 */
        gra3dExecPrelight((SGDFILEHEADER *)mdl_addr, *MapPutGetMatrixPtr(obj_hdl)); /* 190 */
    }
    else if (pre_flg == 1)
    {
        *(u_int *)MapPutGetFlgPtr(obj_hdl) &= ~0x20u;                   /* 195 */
        MapPutSetLitPtr(obj_hdl, MapDrawGetLightPtr4BuffID(buff_id));   /* 196 */
    }
}                                                                       /* 200 */

/* --------------------------------------------------------------------------
 *  MapLigtPreRenderOne
 *
 *  Pre-light one model by name.  The ROM's spelling of the name is kept: the
 *  __FUNCTION__ string baked into its assert reads "MapLigtPreRenderOne".
 *
 *  Skipped when the record is an effect placeholder, when the name is the
 *  literal "0" (the empty-slot marker), when the model has already been
 *  pre-lit (flag bit 1), when MapObjGetLightFlg() says it wants live lighting,
 *  and in the Mei room -- where the light changes every frame, so baking it
 *  once would freeze the flicker.
 * ------------------------------------------------------------------------ */
static void MapLigtPreRenderOne(int buff_id, char *name)
{                                                                       /* 208 */
    short         *flg;
    SGDFILEHEADER *pSGDHead;

    if (MapObjCheckEffect(name) != -1)                                  /* 213 */
    {
        return;
    }

    if (strcmp(name, "0") == 0)                                         /* 215 */
    {
        return;
    }

    flg = FurnCtlGetFlgPtr(buff_id, name);                              /* 218 */
    if (flg == nullptr)
    {
        return;
    }

    if ((*flg & 2) != 0)                                                /* 223 */
    {
        return;
    }

    if (MapObjGetLightFlg(name) != 0)                                   /* 225 */
    {
        return;
    }

    if (MapMeiCheck(MapLoadGetHeadPtr(buff_id)) != 0)                   /* 227 */
    {
        return;
    }

    pSGDHead = (SGDFILEHEADER *)FurnCtlGetModelAddr(buff_id, name);     /* 229 */
    if (pSGDHead == nullptr)                                 /* 230 */
    {
        PRINT_ERROR("NO_SGD_MODEL[%s]\n", name);                        /* 231 */
        PRINT_ASSERT("ERR NO MODEL ADDR");                              /* 232 */
        return;
    }

    _gra3dDrawSGD(pSGDHead, SRT_PRELIGHTING, nullptr, -1);   /* 236 */

    /* The baked result lives in the model, so the coordinate block must not
     * be recalculated -- doing so would relight it from the live block. */
    pSGDHead->pCoord->bCalc = 0;                                        /* 239 */

    *flg |= 2;                                                          /* 242 */
}                                                                       /* 243 */

/* --------------------------------------------------------------------------
 *  MapLightPreRenderType
 *
 *  Walk one registration-record type and pre-light each model it names.  The
 *  three types that carry a model name put it at different offsets, which is
 *  the only reason the switch exists.
 *
 *  The fetch below carries ROM line 262 rather than the loop's own line: the
 *  original tests at the bottom, and GCC kept that line on the test.
 * ------------------------------------------------------------------------ */
static void MapLightPreRenderType(int buff_id, int reg_id, int type)
{                                                                       /* 247 */
    MB_OUT_SECTION *mp;

    RegDatGetStPtrStart(reg_id, type);                                  /* 251 */

    while ((mp = RegDatGetNextStPtr(reg_id)) != nullptr)    /* 262 */
    {
        switch (mp->SecStID)                                            /* 253 */
        {
        case RECORD_TYPE_DOOR:                                                         /* door */
            MapLigtPreRenderOne(buff_id, ((MDAT_DOOR *)mp)->ModelName); /* 255 */
            break;

        case RECORD_TYPE_OBJECT:                                                         /* object */
            MapLigtPreRenderOne(buff_id, ((MDAT_OBJ *)mp)->ModelName);  /* 258 */
            break;                                                      /* 259 */

        case RECORD_TYPE_PUT_ITEM:                                                        /* put item */
            MapLigtPreRenderOne(buff_id, ((MDAT_PUT *)mp)->ModelName);  /* 261 */
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 *  MapLightRePreRender
 *
 *  Re-bake the whole room.  Clearing the draw flags first is what makes
 *  MapLigtPreRenderOne()'s "already pre-lit" test pass again.
 * ------------------------------------------------------------------------ */
void MapLightRePreRender(int buff_id, int reg_id)
{
    FurnCtlDeleteDrawFlgAll(buff_id);                                   /* 271 */
    MapLightPreRenderType(buff_id, reg_id, 7);                          /* 273 */
    MapLightPreRenderType(buff_id, reg_id, 3);                          /* 274 */
    MapLightPreRenderType(buff_id, reg_id, 11);                         /* 275 */
}

/* --------------------------------------------------------------------------
 *  MapLightGetPower
 *
 *  How much a light is worth at `vChrPos`: its raw brightness (the length of
 *  the diffuse colour) attenuated by distance.  Inside fMinRange it counts
 *  full; past twice fMaxRange it counts for nothing.
 * ------------------------------------------------------------------------ */
static float MapLightGetPower(G3DLIGHT *pLightDat, float *vChrPos)
{                                                                       /* 302 */
    float fPower;
    float fLen;

    fPower = g3dxVu0CalcLength(pLightDat->vDiffuse);                    /* 304 */
    fLen   = GetDistV2(pLightDat->vPosition, vChrPos);                  /* 307 */

    if (pLightDat->fMinRange < fLen)                                    /* 309 */
    {
        if (fLen < pLightDat->fMaxRange + pLightDat->fMaxRange)          /* 311 */
        {
            fPower = fPower * ((pLightDat->fMaxRange - pLightDat->fMinRange) /
                               (fLen - pLightDat->fMinRange));
        }
        else
        {
            fPower = 0.0f;
        }
    }

    return fPower;                                                      /* 312 */
}

/* --------------------------------------------------------------------------
 *  MapLightSetSortDat
 *
 *  Append every enabled light of [st, st + reg_cnt) to the candidate list and
 *  return the new list length.
 *
 *  MAP_LIGHT_SORT::flg is deliberately not written -- the ROM leaves it as
 *  whatever was on the caller's stack, and MapLightMakeDualSub() copies it
 *  through to MAP_LIGHT_DAT::flg.  Nothing reads that field afterwards.
 * ------------------------------------------------------------------------ */
static int MapLightSetSortDat(int l_num, int st, int reg_cnt, float *pos,
                              MAP_LIGHT_SORT *llist, GRA3DLIGHTDATA *lp)
{                                                                       /* 317 */
    for (int j = st; j < st + reg_cnt; j++)                             /* 321 */
    {
        if (lp->aStatus[j].bEnable != 0)
        {
            llist[l_num].addr    = &lp->aLight[j];
            llist[l_num].st_addr = &lp->aStatus[j];
            llist[l_num].power   = MapLightGetPower(&lp->aLight[j], pos);
            l_num++;                                                    /* 335 */
        }
    }                                                                   /* 336 */

    return l_num;                                                       /* 337 */
}

/* --------------------------------------------------------------------------
 *  MapLightSelect
 *
 *  Thin [iStart, iEnd] down to the MAP_LIGHT_SELECT_MAX brightest lights as
 *  seen from `vPos`.  Everything in the range is disabled first; a light that
 *  was already disabled is marked in aFlgList so it can never be picked back
 *  up, then the strongest survivor is re-enabled repeatedly.
 * ------------------------------------------------------------------------ */
void MapLightSelect(GRA3DLIGHTDATA *pLightMst, float *vPos, int iStart, int iEnd)
{                                                                       /* 347 */
    MAP_LIGHT_HEAD aFlgList[NUM_GRA3DLIGHTID];
    int            i;
    int            j;
    int            iLightID;
    float          fPow;

    for (i = iStart; i <= iEnd; i++)                                    /* 352 */
    {
        aFlgList[i].iFlg = (pLightMst->aStatus[i].bEnable == 0);
        pLightMst->aStatus[i].bEnable = 0;
        aFlgList[i].fPow = MapLightGetPower(&pLightMst->aLight[i], vPos);
    }                                                                   /* 362 */

    for (j = 0; j < MAP_LIGHT_SELECT_MAX; j++)                          /* 365 */
    {
        fPow     = -1.0f;                                               /* 367 */
        iLightID = -1;

        for (i = iStart; i <= iEnd; i++)                                /* 369 */
        {
            if (aFlgList[i].iFlg != 1)                                  /* 370 */
            {
                if (fPow < aFlgList[i].fPow)                            /* 371 */
                {
                    fPow     = aFlgList[i].fPow;                        /* 372 */
                    iLightID = i;                                       /* 373 */
                }
            }
        }                                                               /* 375 */

        if (iLightID < 0)                                               /* 376 */
        {
            break;
        }

        aFlgList[iLightID].iFlg = 1;                                    /* 377 */
        pLightMst->aStatus[iLightID].bEnable = 1;
    }                                                                   /* 379 */
}

/* Thin both the point and the spot range. */
void MapLightSelectEnable(GRA3DLIGHTDATA *pLightMst, float *vPos)
{                                                                       /* 384 */
    MapLightSelect(pLightMst, vPos, GRA3D_START_LIGHT_POINT, LID_POINT_15);     /* 386 */
    MapLightSelect(pLightMst, vPos, GRA3D_START_LIGHT_SPOT, LID_SPOT_15);      /* 387 */
}

/* --------------------------------------------------------------------------
 *  MapLightMakeDualSub
 *
 *  Merge one light range of two rooms into `w_light`.  Both rooms' enabled
 *  lights go into a single candidate list, then the strongest is repeatedly
 *  moved into the next output slot; the vacated list entry is filled from the
 *  tail so the search shrinks by one each round.
 *
 *  Returns the candidate count, not the number written -- the two differ when
 *  the two rooms together have more lights than the range has slots.
 * ------------------------------------------------------------------------ */
static int MapLightMakeDualSub(MAP_LIGHT_DAT *w_light, GRA3DLIGHTDATA *in1,
                               GRA3DLIGHTDATA *in2, int st, int en, float *pos)
{                                                                       /* 395 */
    MAP_LIGHT_SORT  llist[NUM_GRA3DLIGHTID * 2];
    MAP_LIGHT_SORT *lsp;
    int             i;
    int             j;
    int             l_num;
    int             in_cnt;
    int             ret;
    int             reg_cnt;

    reg_cnt = (en - st) + 1;                                            /* 399 */

    for (i = st; i <= en; i++)                                          /* 402 */
    {
        w_light[i].lstat.bEnable = 0;                                   /* 403 */
    }                                                                   /* 404 */

    l_num = MapLightSetSortDat(0, st, reg_cnt, pos, llist, in1);        /* 407 */
    l_num = MapLightSetSortDat(l_num, st, reg_cnt, pos, llist, in2);    /* 408 */
    ret   = l_num;

    if (l_num == 0)                                                     /* 411 */
    {
        return 0;
    }

    in_cnt = st;
    while (0 < l_num)                                                   /* 415 */
    {
        lsp = llist;
        for (j = 1; j < l_num; j++)                                     /* 418 */
        {
            if (lsp->power < llist[j].power)                            /* 419 */
            {
                lsp = &llist[j];
            }
        }                                                               /* 420 */

        w_light[in_cnt].ldat  = *lsp->addr;                             /* 423 */
        w_light[in_cnt].lstat = *lsp->st_addr;                          /* 424 */
        w_light[in_cnt].flg   = lsp->flg;                               /* 425 */
        w_light[in_cnt].lstat.bEnable = 1;                              /* 426 */

        /* The slot bound is tested after the copy, not before it: the ROM
         * fills w_light[en] and only then stops. */
        in_cnt++;                                                       /* 450 */
        if (en < in_cnt)
        {
            break;
        }

        *lsp = llist[l_num - 1];                                        /* 453 */
        l_num--;                                                        /* 454 */
    }

    return ret;                                                         /* 457 */
}                                                                       /* 458 */

/* --------------------------------------------------------------------------
 *  MapLightClearPointDirection
 *
 *  A point light has no direction, but the blend above copies whole G3DLIGHT
 *  records around, so a stale direction can arrive in a point slot.  Zero it.
 * ------------------------------------------------------------------------ */
static void MapLightClearPointDirection(GRA3DLIGHTDATA *pLight)
{                                                                       /* 462 */
    int i;

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)                              /* 463 */
    {
        if (pLight->aLight[i].Type == G3DLIGHT_POINT)
        {
            g3dxVu0CopyVector(pLight->aLight[i].vDirection, g_v0000);
        }
    }                                                                   /* 466 */
}

/* Non-zero when any of the three directional slots is on. */
static int MapLightCheckDirectLight(GRA3DLIGHTDATA *pLight)
{                                                                       /* 471 */
    int i;

    for (i = 0; i < GRA3D_NUM_LIGHT_DIRECTIONAL; i++)                   /* 472 */
    {
        if (pLight->aStatus[i].bEnable == 1)                            /* 474 */
        {
            return 1;                                                   /* 475 */
        }
    }

    return 0;                                                           /* 476 */
}

/* --------------------------------------------------------------------------
 *  MapLightMakeDualDirect
 *
 *  Merge the three directional slots of two rooms.  Room 1's directionals are
 *  installed first, then each of room 2's is offered against the weakest entry
 *  of fPowList and takes its place if it is brighter.
 *
 *  Two things here look like slips but are the ROM's:
 *
 *    - the brightness recorded in fPowList is measured from *pLightOut*, which
 *      MapLightMakeDual() has just filled with a copy of pLight2, and not from
 *      the light being installed;
 *    - the winning entry is copied out of pLight2 at index iSarID, the slot
 *      being replaced, rather than at the loop's own index j.
 *
 *  Both are reproduced as found.
 * ------------------------------------------------------------------------ */
void MapLightMakeDualDirect(GRA3DLIGHTDATA *pLightOut, GRA3DLIGHTDATA *pLight1,
                            GRA3DLIGHTDATA *pLight2)
{                                                                       /* 481 */
    float fPowList[GRA3D_NUM_LIGHT_DIRECTIONAL];
    float fSarPow;
    float fPow;
    int   j;
    int   l;
    int   iSarID;

    for (l = 0; l < GRA3D_NUM_LIGHT_DIRECTIONAL; l++)                   /* 488 */
    {
        if (pLight1->aStatus[l].bEnable == 0)
        {
            fPowList[l] = 0.0f;                                         /* 490 */
            pLightOut->aStatus[l].bEnable = 0;                          /* 492 */
        }
        else
        {
            fPowList[l] = g3dxVu0CalcLength(pLightOut->aLight[l].vDiffuse);
            pLightOut->aLight[l]  = pLight1->aLight[l];
            pLightOut->aStatus[l] = pLight1->aStatus[l];
        }
    }                                                                   /* 500 */

    for (j = 0; j < GRA3D_NUM_LIGHT_DIRECTIONAL; j++)                   /* 503 */
    {
        if (pLight2->aStatus[j].bEnable != 0)
        {
            /* The ROM keeps this in an unnamed register temp; it is evaluated
             * once and used by both the test and the store below. */
            fPow = g3dxVu0CalcLength(pLightOut->aLight[j].vDiffuse);

            fSarPow = fPowList[0];                                      /* 514 */
            iSarID  = 0;                                                /* 515 */

            for (l = 1; l < GRA3D_NUM_LIGHT_DIRECTIONAL; l++)           /* 516 */
            {
                if (fPowList[l] < fSarPow)                              /* 517 */
                {
                    fSarPow = fPowList[l];                              /* 518 */
                    iSarID  = l;                                        /* 519 */
                }
            }                                                           /* 521 */

            if (fSarPow < fPow)                                         /* 524 */
            {
                fPowList[iSarID]        = fPow;                         /* 525 */
                pLightOut->aLight[iSarID]  = pLight2->aLight[iSarID];
                pLightOut->aStatus[iSarID] = pLight2->aStatus[iSarID];
                pLightOut->aStatus[iSarID].bEnable = 1;
            }
        }
    }                                                                   /* 532 */
}

/* --------------------------------------------------------------------------
 *  MapLightUpdate
 *
 *  Fold the merged run back into `pOutLight` and return how many slots came
 *  out enabled.
 *
 *  The trailing clear of aStatus[iEnd] is the ROM's: the last slot of each
 *  range is written by the loop and then forced off again, which reserves it.
 * ------------------------------------------------------------------------ */
int MapLightUpdate(GRA3DLIGHTDATA *pOutLight, MAP_LIGHT_DAT *pUpLight,
                   int iStart, int iEnd)
{                                                                       /* 539 */
    int cnt;
    int i;

    cnt = 0;                                                            /* 540 */

    for (i = iStart; i <= iEnd; i++)                                    /* 542 */
    {
        if (pUpLight[i].lstat.bEnable == 0)                             /* 543 */
        {
            pOutLight->aStatus[i].bEnable = 0;                          /* 545 */
        }
        else
        {
            pOutLight->aLight[i]  = pUpLight[i].ldat;
            pOutLight->aStatus[i] = pUpLight[i].lstat;
            cnt++;                                                      /* 551 */
        }
    }                                                                   /* 552 */

    pOutLight->aStatus[iEnd].bEnable = 0;

    return cnt;                                                         /* 557 */
}

/* --------------------------------------------------------------------------
 *  MapLightMakeDual
 *
 *  Blend two rooms' light blocks at `vCenPos`.  With only one room supplied
 *  there is nothing to blend, so its block is copied straight through and the
 *  caller told about it; with neither, nothing happens at all.
 *
 *  aiNumInitial[1] and [2] carry the resulting point and spot counts, which
 *  the renderer uses to size its light upload.
 * ------------------------------------------------------------------------ */
void MapLightMakeDual(GRA3DLIGHTDATA *out, GRA3DLIGHTDATA *light1,
                      GRA3DLIGHTDATA *light2, float *vCenPos)
{                                                                       /* 571 */
    MAP_LIGHT_DAT   w_light[NUM_GRA3DLIGHTID];
    GRA3DLIGHTDATA *wp;
    int             i;

    if (light1 == nullptr && light2 == nullptr)
    {
        PRINT_ERROR("NO_LIGHT_DATA\n");                                 /* 572 */
        return;                                                         /* 573 */
    }

    wp = nullptr;                                           /* 577 */
    if (light1 == nullptr)
    {
        wp = light2;                                                    /* 578 */
    }
    if (light2 == nullptr)
    {
        wp = light1;                                                    /* 580 */
    }

    if (wp != nullptr)
    {
        *out = *wp;                                                     /* 581 */
        /* The ROM prints light1 twice -- its own format string says
         * "light1[%x] light1[%x]".  Kept as found. */
        PRINT_ERROR("LIGHT_DATA_NO_DUAL light1[%x] light1[%x]\n",       /* 582 */
                    light1, light1);                                    /* 583 */
        return;                                                         /* 584 */
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)                              /* 588 */
    {
        w_light[i].lstat.bEnable = 0;                                   /* 589 */
    }                                                                   /* 590 */

    MapLightMakeDualSub(w_light, light1, light2,
                        GRA3D_START_LIGHT_POINT, LID_POINT_15, vCenPos);        /* 594 */
    MapLightMakeDualSub(w_light, light1, light2,
                        GRA3D_START_LIGHT_SPOT, LID_SPOT_15, vCenPos);         /* 597 */

    /* Room 2's block supplies the ambient and everything outside the two
     * ranges; the merged point/spot slots are written over it below. */
    *out = *light2;                                                     /* 600 */

    out->aiNumInitial[1] =
        MapLightUpdate(out, w_light, GRA3D_START_LIGHT_POINT, LID_POINT_15);    /* 604 */
    out->aiNumInitial[2] =
        MapLightUpdate(out, w_light, GRA3D_START_LIGHT_SPOT, LID_SPOT_15);     /* 606 */

    if (MapLightCheckDirectLight(light1) != 0 &&                        /* 609 */
        MapLightCheckDirectLight(light2) != 0)
    {
        MapLightMakeDualDirect(out, light1, light2);                    /* 612 */
    }

    MapLightClearPointDirection(out);                                   /* 615 */
}                                                                       /* 616 */

/* --------------------------------------------------------------------------
 *  MapLightSetScale
 *
 *  Copy `mst` into `out`.  The trailing loop is the ROM's: by this build its
 *  body had been reduced to nothing but fixed_array's range check -- one
 *  _fixed_array_verifyrange<GRA3DLIGHTSTATUS> per slot and no load or store --
 *  so whatever it scaled was already commented out.  The loop is kept because
 *  the bounds check is the only thing it still does.
 * ------------------------------------------------------------------------ */
void MapLightSetScale(GRA3DLIGHTDATA *out, GRA3DLIGHTDATA *mst)
{                                                                       /* 620 */

    *out = *mst;                                                        /* 622 */

    for (int i = 0; i < NUM_GRA3DLIGHTID; i++)                          /* 624 */
    {
        (void)out->aStatus[i];
    }                                                                   /* 627 */
}

/* --------------------------------------------------------------------------
 *  Mei (flicker) light
 *
 *  Room RKS10 drives its lighting off an animation table instead of the static
 *  room light: MapMeiList is a run of key frames split into four sequences by
 *  max < 0 separators and closed by max == MAPMEI_LIST_END, MapMeiHead tracks
 *  the cursor into each, and MapMeiWork holds the pair of light blocks the
 *  animation interpolates between -- [0] is the live output, [1] the master
 *  copy it is scaled from.
 *
 *  MapMeiWork is the block every caller of MapMeiGetLight() receives, so it
 *  must exist even before MapMeiInit() has run -- returning null here is what
 *  made playerSetLight() trip gra3dGenerateLightDataToChar's
 *  G3DASSERT(pLDSrc) in that room.
 * ------------------------------------------------------------------------ */

/* Key frames, read out of MapLight.o's .data (0x2c8d20, 0x1f4 bytes -- the
 * object's only .data).  max < 0 is a separator; -1000 terminates. */
static MAPMEI_FRAME MapMeiList[MAPMEI_LIST_NUM] =                       /* data 2c8d20 */
{
    {    -1, {  -1,  -1,  -1,  -1 } },
    {   100, {   2,   1,  10,   1 } },
    {    70, {   7,   1,  15,  30 } },
    {   100, {   2,   1,  10,   1 } },
    {    50, {   2,   1,   7,  15 } },
    {   100, {   2,   1,  10,   5 } },
    {    70, {   2,   1,   5,   1 } },
    {   100, {   2,   1,  10,   5 } },
    {    80, {   2,   1,   5,  30 } },
    {    -1, {  -1,  -1,  -1,  -1 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {    -1, {  -1,  -1,  -1,  -1 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {    -1, {  -1,  -1,  -1,  -1 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    {   100, {  10,  20,  10,  20 } },
    { -1000, {  -1,  -1,  -1,  -1 } },
};

static MAPMEI_HEAD     *MapMeiNowHeadPtr;                               /* sdata 3eee5c */
static GRA3DLIGHTDATA   MapMeiWork[2];                                  /* bss 403fb0 */
static MAPMEI_HEAD      MapMeiHead[MAPMEI_HEAD_NUM];                    /* bss 4066f0 */
static MAPMEI_LIGHTONE  MapMeiLightOneWork[MAPMEI_LIGHTONE_NUM];        /* bss 406730 */

GRA3DLIGHTDATA *MapMeiGetLight(void)
{
    return MapMeiWork;
}

int MapMeiCheck(MLOAD_HEAD *hp)
{                                                                       /* 671 */
    G3DASSERT(hp, "");

    return (hp->labelID == RKS10_PK2);                                  /* 674 */
}

/* --------------------------------------------------------------------------
 *  MapMeiRegistLightOne
 *
 *  Bind a light that lives outside the room block -- an effect's or a placed
 *  lamp's -- into the flicker.  The current values are snapshotted into
 *  m_light and `lip` remembers where to write the scaled result back.
 * ------------------------------------------------------------------------ */
void MapMeiRegistLightOne(G3DLIGHT *lp)
{                                                                       /* 685 */
    for (int i = 0; i < MAPMEI_LIGHTONE_NUM; i++)                       /* 686 */
    {
        if (MapMeiLightOneWork[i].lip == nullptr)                       /* 687 */
        {
            MapMeiLightOneWork[i].lip     = lp;                         /* 688 */
            MapMeiLightOneWork[i].m_light = *lp;                        /* 689 */
            return;
        }
    }                                                                   /* 690 */
}

/* Seeds the animation from the room's own light block.  Both work slots start
 * as a copy of `mst`, so the room is lit correctly from frame one whether or
 * not the flicker animation has stepped yet. */
void MapMeiInit(GRA3DLIGHTDATA *mst)
{                                                                       /* 697 */
    MAPMEI_FRAME *lp;
    int           i;

    for (i = 0; i < MAPMEI_HEAD_NUM; i++)                               /* 700 */
    {
        MapMeiHead[i].top_dat_p = nullptr;                              /* 701 */
    }                                                                   /* 702 */

    /* One head per sequence: run to the next separator, step over it, and
     * take what follows as that sequence's first key frame. */
    lp = MapMeiList;                                                    /* 704 */
    for (i = 0; i < MAPMEI_HEAD_NUM; i++)                               /* 705 */
    {
        while (lp->max >= 0)                                            /* 707 */
        {
            lp++;                                                       /* 708 */
        }

        if (lp->max == MAPMEI_LIST_END)                                 /* 709 */
        {
            break;
        }
        lp++;                                                           /* 710 */

        MapMeiHead[i].top_dat_p = lp;                                   /* 711 */
        MapMeiHead[i].now_dat_p = lp;                                   /* 712 */
        MapMeiHead[i].stat      = 0;                                    /* 713 */
        MapMeiHead[i].frame     = 0;
    }

    MapMeiNowHeadPtr = MapMeiHead;                                      /* 718 */

    MapMeiWork[0] = *mst;                                               /* 721 */
    MapMeiWork[1] = *mst;                                               /* 722 */

    for (i = 0; i < MAPMEI_LIGHTONE_NUM; i++)                           /* 725 */
    {
        MapMeiLightOneWork[i].lip = nullptr;                            /* 726 */
    }                                                                   /* 727 */
}

void MapMeiTerm(void)
{
    /* Empty in the original too. */                                    /* 736 */
}

/* --------------------------------------------------------------------------
 *  MapMeiAnimLightOne
 *
 *  Scale one light by `num`.  Inlined at both of MapMeiAnimLight()'s call
 *  sites in the ROM, so it has no symbol of its own -- the name is ours; the
 *  body (ROM lines 745-750) is not.
 *
 *  Only xyz of the two colours is touched: w carries no colour, and the
 *  ambient term is deliberately left alone so the room never goes fully dark.
 * ------------------------------------------------------------------------ */
static inline void MapMeiAnimLightOne(G3DLIGHT *out, G3DLIGHT *mst, float num)
{
    out->fMaxRange = mst->fMaxRange * num;                              /* 745 */
    out->fMinRange = mst->fMinRange * num;                              /* 746 */

    for (int j = 0; j < 3; j++)                                         /* 747 */
    {
        out->vDiffuse[j]  = mst->vDiffuse[j] * num;                     /* 748 */
        out->vSpecular[j] = mst->vSpecular[j] * num;                    /* 749 */
    }                                                                   /* 750 */
}

/* Apply one frame's brightness (percent) to the whole room block and to every
 * externally registered light. */
static void MapMeiAnimLight(GRA3DLIGHTDATA *out, GRA3DLIGHTDATA *mst, int max)
{                                                                       /* 755 */
    float num;
    int   i;

    num = (float)max / 100.0f;                                          /* 757 */

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)                              /* 760 */
    {
        if (mst->aStatus[i].bEnable != 0)
        {
            MapMeiAnimLightOne(&out->aLight[i], &mst->aLight[i], num);
        }
    }                                                                   /* 764 */

    for (i = 0; i < MAPMEI_LIGHTONE_NUM; i++)                           /* 767 */
    {
        if (MapMeiLightOneWork[i].lip != nullptr)                       /* 769 */
        {
            MapMeiAnimLightOne(MapMeiLightOneWork[i].lip,
                               &MapMeiLightOneWork[i].m_light, num);
        }
    }                                                                   /* 771 */
}

/* --------------------------------------------------------------------------
 *  MapMeiAnimFrame
 *
 *  Step one sequence and return this frame's brightness in percent.
 *
 *  Each key frame is a four-phase envelope, with frame[stat] holding the
 *  length of phase `stat`:
 *      0  ramp MAPMEI_BASE_POWER -> max
 *      1  hold at max
 *      2  ramp max -> MAPMEI_BASE_POWER
 *      3  hold at MAPMEI_BASE_POWER
 *  On the phase boundary the value snaps to that phase's end (`lnum`), and
 *  after phase 3 the cursor advances -- wrapping to the sequence top when it
 *  walks into the next separator.
 * ------------------------------------------------------------------------ */
static int MapMeiAnimFrame(MAPMEI_HEAD *hp)
{                                                                       /* 776 */
    MAPMEI_FRAME *np;
    int           num;
    int           lnum;
    int           stat;
    int           frame;

    np    = hp->now_dat_p;                                              /* 777 */
    stat  = hp->stat;
    frame = hp->frame;
    num   = 0;                                                          /* 783 */
    lnum  = 0;                                                          /* 784 */

    switch (stat)                                                       /* 791 */
    {
    case 0:                                                             /* 792 */
        lnum = np->max;                                                 /* 795 */
        num  = ((np->max - MAPMEI_BASE_POWER) * frame) / np->frame[0] +
               MAPMEI_BASE_POWER;                                       /* 798 */
        break;                                                          /* 799 */

    case 1:                                                             /* 801 */
        lnum = np->max;                                                 /* 803 */
        num  = np->max;
        break;

    case 2:                                                             /* 807 */
        lnum = MAPMEI_BASE_POWER;                                       /* 808 */
        num  = (np->max - MAPMEI_BASE_POWER) -
               ((np->max - MAPMEI_BASE_POWER) * frame) / np->frame[2] +
               MAPMEI_BASE_POWER;                                       /* 810 */
        break;                                                          /* 811 */

    case 3:                                                             /* 816 */
        lnum = MAPMEI_BASE_POWER;                                       /* 817 */
        num  = MAPMEI_BASE_POWER;                                       /* 818 */
        break;
    }

    hp->frame = frame + 1;                                              /* 819 */
    if (np->frame[stat] <= hp->frame)                                   /* 820 */
    {
        hp->frame = 0;                                                  /* 821 */
        hp->stat  = stat + 1;                                           /* 822 */
        num       = lnum;                                               /* 823 */

        if (3 < hp->stat)
        {
            hp->stat      = 0;
            hp->now_dat_p = np + 1;

            /* Walking into the next separator means the sequence is over. */
            if (np[1].max < 0)
            {
                hp->now_dat_p = hp->top_dat_p;
            }
        }
    }

    return num;                                                         /* 828 */
}

/* Advance the flicker one frame and push the result at gra3d. */
int MapMeiProc(void)
{                                                                       /* 833 */
    if (MapMeiNowHeadPtr == nullptr)                                    /* 836 */
    {
        return -1;                                                      /* 837 */
    }

    if (MapMeiNowHeadPtr->top_dat_p == nullptr)                         /* 840 */
    {
        return -2;
    }

    MapMeiAnimLight(MapMeiWork, &MapMeiWork[1],
                    MapMeiAnimFrame(MapMeiNowHeadPtr));                 /* 842 */

    gra3dSetLightData(MapMeiWork, nullptr);                             /* 844 */
    MapLightSetPlayerReal();                                            /* 846 */
    gra3dApplyLight();                                                  /* 847 */

    return 0;                                                           /* 849 */
}                                                                       /* 850 */
