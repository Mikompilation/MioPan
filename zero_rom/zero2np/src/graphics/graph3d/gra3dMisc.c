/* ==========================================================================
 *  gra3dMisc.c
 *
 *  Miscellaneous high-level gra3d services that bridge the core renderer / the
 *  shadow subsystem and the game's map / player modules:
 *
 *    - prelighting of the current scene (gra3dPrelight / gra3dPrelightScene),
 *    - casting shadows for SGD models, per-object and per-character
 *      (gra3dDrawSGDShadow / ...EveryObject / ...Character),
 *    - blending the strongest shadow-casting lights at a target point into a
 *      single directional shadow light (gra3dCalcShadowLight),
 *    - building a character's per-frame light-data record, folding in the
 *      player's flashlights and self-reflection light
 *      (gra3dGenerateLightDataToChar / _SetLightToShadow), and
 *    - the projector spot light used in two specific rooms
 *      (gra3dStart/End/Update/IsSpecialLightActive / gra3dGetProjectorSpot*).
 *
 *  The std::sort over the LIGHTCOMPAREDATA array (and the per-TU fixed_array
 *  template helpers) are inlined by the compiler and are not re-emitted here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dMisc.h"
#include "gra3d.h"
#include "gra3dShadow.h"        /* projected-shadow API + g_gra3dShadowDebug */
#include "gra3dBoundingBox.h"
#include "gra3dSGD.h"
#include "gra3dSGDData.h"       /* sgdGetProcUnit / sgdCalcBoneCoordinate */
#include "gra3dConst.h"         /* g_v0000 / g_matUnit / g_NullLight */
#include "g3dCore.h"
#include "g3dLight.h"
#include "g3dLightEx.h"
#include "g3dRenderTarget.h"
#include "g3dxVu0.h"
#include "g3ddbg.h"
#include "ctl/fixed_array.h"
#include "main/glob.h"              /* plyr_wrk / debug_var */
#include "ingame/plyr/player.h"
#include "ingame/map/MapLoad.h"
#include "ingame/map/MapDraw.h"
#include "graphics/motion/mdlwork.h"
#include "graphics/obj_draw_ctrl.h"
#include "graphics/effect/effect_oth.h"
#include <algorithm>            /* std::sort / std::fill */

#include "ingame/map/MapLight.h"

/* Compiler-generated copies of _fixed_array_assert / _fixed_array_verifyrange<T>
 * (001b69b8..001b6a8f) are the inlined ctl/fixed_array.h template; not emitted.
 * The std::sort instantiation (__introsort_loop / __final_insertion_sort /
 * heap helpers / fill / ZeroArray) for the LIGHTCOMPAREDATA sort in
 * gra3dCalcShadowLight is likewise compiler-emitted and not re-emitted. */

/* The shadow-light compare array is built in the EE scratchpad.
 *
 * PORT: this must go through the emulated scratchpad, not the EE's physical
 * 0x70003900 -- which is an unmapped address on the host, so ZeroArray()'s
 * first store faulted and took every shadow in the game down with it.  Every
 * other scratchpad user in the tree already goes through _SPR / DAT_7000xxxx
 * (pcport_stubs.h); this define was the one left as a raw address.
 *
 * The array is also twice the ROM's size here: LIGHTCOMPAREDATA is 0xc bytes
 * on the EE and 0x18 on the host, because it holds a G3DLIGHT *.  39 entries
 * therefore span 0x3900..0x3ca8 rather than 0x3900..0x3a24.  That is still
 * inside the 16 KiB buffer, and nothing in the tree uses the scratchpad above
 * 0x3980, so the extra reach is harmless -- but it is why this array cannot be
 * treated as byte-exact with the ROM's the way the overlaid VU1 layouts are. */
#define SPR_LIGHTCOMPAREDATA    ((LIGHTCOMPAREDATA *)&_SPR(0x3900))

/* ----- small helpers provided by sgd_types.h / ctl (template instances) - */

/* True when an SGD model is a preset (build-baked) model. */
static inline int sgdIsPresetData(SGDFILEHEADER *pSGD)
{
    return pSGD->ucModelType & 1;
}

/* Zero an array of n T (the per-TU std-style fill-with-zero instantiation). */
template <class T>
void ZeroArray(T *a, int n)
{
    std::fill(a, a + n, T());
}

/* Character shadow VRAM destination block. */
#define ENE_VADR                0x2bc0

/* ----- module statics (globals.txt) ------------------------------------- */
static int      s_iObjdctIdDrawNoShadow;
static int      s_bSpecialLightActive;
static G3DLIGHT s_ProjectorSpot;

/* Canonical vectors / null light come from gra3dConst.h; the shadow-subsystem
 * debug switches (g_gra3dShadowDebug) from gra3dShadow.h; the SGD walk helpers
 * (sgdGetProcUnit / sgdCalcBoneCoordinate) from gra3dSGDData.h -- all included
 * above. */

/* Game-module externals live in their owner headers/stub modules. */

/* --------------------------------------------------------------------------
 *  _DrawShadowTexture
 *
 *  Stub in the prototype: it reserves a six-register GS-register packet and a
 *  CAutoGsRegisters guard but performs no work (the shadow-texture composite
 *  is disabled in this build).
 * ------------------------------------------------------------------------ */
void _DrawShadowTexture(void)
{
    sceGifPackAd        aGPA[6];
    CAutoGsRegisters<6> ARs;

    (void)aGPA;
    (void)ARs;
}

/* --------------------------------------------------------------------------
 *  gra3dCalcShadowLight
 *
 *  Pick the shadow light for a caster at vTarget: score every enabled
 *  shadow-casting light by its power reaching vTarget, sort the 39 candidates
 *  by descending power, then blend the top five with a fixed downward "adjust"
 *  directional light into pLight.  The result's diffuse is halved.
 * ------------------------------------------------------------------------ */
void gra3dCalcShadowLight(G3DLIGHT *pLight, float *vTarget)
{
    LIGHTCOMPAREDATA          (&aLCD)[NUM_GRA3DLIGHTID] = *(LIGHTCOMPAREDATA (*)[NUM_GRA3DLIGHTID])SPR_LIGHTCOMPAREDATA;
    G3DLIGHT                    lAdjust;
    int                         i;
    LIGHTCOMPAREDATA           *__first;
    LIGHTCOMPAREDATA::greater   __comp;
    G3DLIGHT                   *apSrc[5];

    ZeroArray<LIGHTCOMPAREDATA>(aLCD, NUM_GRA3DLIGHTID);

    for (i = 0; i < NUM_GRA3DLIGHTID; i = i + 1)
    {
        LIGHTCOMPAREDATA &rLCD = aLCD[i];
        GRA3DLIGHTSTATUS &rS   = gra3dGetLightStatusRef(i);

        if (rS.bEnable == 0 || rS.bEnableToShadow == 0)
        {
            rLCD.iIndex = INVALID_GRA3DLIGHTID;
        }
        else
        {
            G3DLIGHT &rL = gra3dGetLightRef(i);
            float     sc;

            rLCD.pLight = &rL;
            sc          = g3dCalcLightPower(&rL, vTarget);
            rLCD.iIndex = i;
            rLCD.fPower = sc;
        }
    }

    __first = aLCD;
    __comp  = LIGHTCOMPAREDATA::greater();
    std::sort(__first, aLCD + NUM_GRA3DLIGHTID, __comp);

    /* a constant downward directional light folded into the blend */
    lAdjust = g_NullLight;
    lAdjust.vDiffuse[0]   = 1.0f;
    lAdjust.vDiffuse[1]   = 1.0f;
    lAdjust.vDiffuse[2]   = 1.0f;
    lAdjust.vDiffuse[3]   = 1.0f;
    lAdjust.vDirection[0] = 0.0f;
    lAdjust.vDirection[1] = -1.0f;
    lAdjust.vDirection[2] = 0.0001f;
    lAdjust.vDirection[3] = 0.0f;
    lAdjust.Type          = G3DLIGHT_DIRECTIONAL;

    /* blend the four strongest candidates and the adjust light */
    apSrc[0] = aLCD[0].pLight;
    apSrc[1] = aLCD[1].pLight;
    apSrc[2] = aLCD[2].pLight;
    apSrc[3] = aLCD[3].pLight;
    apSrc[4] = &lAdjust;
    g3dBlendLight(pLight, apSrc, 5, vTarget);

    /* halve the resulting diffuse */
    {
        float *pv = *(sceVu0FVECTOR *)pLight->vDiffuse;   /* -> float[4] */
        pv[0] *= 0.5f; pv[1] *= 0.5f; pv[2] *= 0.5f; pv[3] *= 0.5f;
    }
}

/* --------------------------------------------------------------------------
 *  gra3dPrelightScene
 *
 *  Prelight the room RoomNo: fetch its loaded map header, and (when present
 *  and not a "mei" placeholder) install the room's light data, run the SGD
 *  prelighting pass over the model and shadow models at the room's world
 *  offset, then mark the map's prelight done.
 * ------------------------------------------------------------------------ */
void gra3dPrelightScene(int RoomNo)
{
    int            iBuffId;
    MLOAD_HEAD    *hp;
    GRA3DLIGHTDATA *pLightData;
    float          vOffset[4];

    iBuffId = MapLoadGetBuffID(RoomNo);
    hp      = MapLoadGetHeadPtr(iBuffId);

    if (hp != nullptr && MapMeiCheck(hp) == 0)
    {
        iBuffId    = MapLoadGetBuffID(RoomNo);
        pLightData = MapDrawGetLightPtr4BuffID2(iBuffId);

        gra3dSetLightData(pLightData, nullptr);

        g3dxVu0CopyVector(vOffset, MapLoadGetOffset(RoomNo));

        gra3dExecPrelight((SGDFILEHEADER *)hp->model_addr,    vOffset, g_v0000);
        gra3dExecPrelight((SGDFILEHEADER *)hp->shadow_s_addr, vOffset, g_v0000);
        MapDrawPreLight(hp);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dPrelight
 *
 *  Prelight the player's current room.
 * ------------------------------------------------------------------------ */
void gra3dPrelight(void)
{
    int RoomNo;

    RoomNo = GetPlyrAreaNo();
    gra3dPrelightScene(RoomNo);
}

/* --------------------------------------------------------------------------
 *  gra3dDrawSGDShadow
 *
 *  Cast pSGDTop's shadow with pLight: set the shadow subsystem's bounding box
 *  (world space) and light, then run the shadow draw.  The model must be a
 *  non-preset (runtime) SGD.
 * ------------------------------------------------------------------------ */
void gra3dDrawSGDShadow(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord, G3DLIGHT *pLight, float (*avBBWorld)[4])
{
    if (pSGDTop != NULL)
    {
        /* "shadow model is expected No-Preset model" */
        G3DRETURN(!sgdIsPresetData(pSGDTop), "shadow model is expected No-Preset model");

        gra3dshadowSetBoundingBox(avBBWorld, g_matUnit);
        gra3dshadowSetLight(pLight);
        gra3dshadowDrawSGD(pSGDTop, pCoord, -1);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dSetObjectIdDrawNoShadow
 *
 *  Mark one per-block object id to skip when casting object shadows.
 * ------------------------------------------------------------------------ */
void gra3dSetObjectIdDrawNoShadow(int iId)
{
    s_iObjdctIdDrawNoShadow = iId;
}

/* --------------------------------------------------------------------------
 *  gra3dDrawSGDShadowEveryObject
 *
 *  Cast a shadow for every object (BOUNDINGBOX-tagged block) in a shadow
 *  model: rebuild its bone coordinates, draw the realtime pass, set the
 *  light, then for each object block (except the no-shadow id) set the
 *  per-object bounding box and run its shadow draw.
 * ------------------------------------------------------------------------ */
void gra3dDrawSGDShadowEveryObject(SGDFILEHEADER *pShadowModel, G3DLIGHT *pLight)
{
    unsigned int        i;
    SGDPROCUNITHEADER  *pPUHead;

    if (pShadowModel != NULL && pShadowModel->pCoord != NULL)
    {
        /* "shadow model is expected No-Preset model" */
        G3DRETURN(!sgdIsPresetData(pShadowModel), "shadow model is expected No-Preset model");

        sgdCalcBoneCoordinate(pShadowModel->pCoord, pShadowModel->uiNumBlock - 1);
        gra3dSetGsRegister(0, SCE_GS_FOGCOL);
        _gra3dDrawSGD(pShadowModel, SRT_REALTIME, NULL, 0);
        gra3dshadowSetLight(pLight);

        i = 1;
        while (i < pShadowModel->uiNumBlock - 1)
        {
            pPUHead = pShadowModel->apProcUnitHead[i];

            pPUHead = sgdGetProcUnit(pPUHead, SPC_BOUNDINGBOX, 0);
            if (pPUHead != NULL && s_iObjdctIdDrawNoShadow != (int)i)
            {
                gra3dshadowSetBoundingBox((float (*)[4])(pPUHead + 1),
                                          (float (*)[4])(SGDCOORDINATE *)pShadowModel->pCoord);
                gra3dshadowDrawSGD(pShadowModel, NULL, i);
            }

            i = i + 1;
        }
    }
}

/* --------------------------------------------------------------------------
 *  gra3dGenerateLightDataToChar
 *
 *  Build a character's light-data record from a room source record, then fold
 *  in the player's two flashlights and self-reflection light: each is copied
 *  into its dedicated light slot (flashlight -> point slot 0x26 or 0x13 by
 *  type, second flashlight -> 0x14, self-reflection -> 0x15), scaled / range-
 *  set per pData, and its status flags (enable / to-char / to-shadow /
 *  emulate-to-directional) are set.  Slots whose player light is absent are
 *  disabled.
 * ------------------------------------------------------------------------ */
void gra3dGenerateLightDataToChar(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc,
                                  GRA3DEMULATIONLIGHTDATACREATIONDATA *pData)
{
    GRA3DLIGHTID flashlightId;
    int          bEnableSelfreflection;
    int          bEmulateSelfreflection;
    int          bEnableFlashlight;
    int          bEmulateFlashlight;
    int          bEnableFlashlight2;
    int          bEmulateFlashlight2;
    G3DLIGHT    *pDest;

    /* "pLDDest" / "pLDSrc" */
    G3DASSERT(pLDDest, "");
    G3DASSERT(pLDSrc, "");

    *pLDDest = *pLDSrc;

    /* flashlight 0 lands in the self-reflection point slot (0x13) when it is a
     * point light, otherwise the flashlight spot slot (0x26). */
    flashlightId = LID_SPOT_FLASHLIGHT;             /* 0x26 */
    if (plyr_wrk.fl.Type == G3DLIGHT_POINT)
    {
        flashlightId = LID_POINT_FLASHLIGHT_0;       /* 0x13 */
    }

    bEnableSelfreflection  = pData->bEnableSelfreflection;
    bEmulateSelfreflection = pData->bEmulateSelfreflection;
    bEnableFlashlight      = pData->bEnableFlashlight;
    bEmulateFlashlight     = pData->bEmulateFlashlight;
    bEnableFlashlight2     = pData->bEnableFlashlight2;
    bEmulateFlashlight2    = pData->bEmulateFlashlight2;

    if (plyr_wrk.fl.Type == G3DLIGHTTYPE_FORCE_DWORD)
    {
        bEmulateFlashlight     = 0;
        bEnableSelfreflection  = 0;
        bEmulateSelfreflection = 0;
        bEnableFlashlight      = 0;
    }
    if (plyr_wrk.fl2.Type == G3DLIGHTTYPE_FORCE_DWORD)
    {
        bEmulateFlashlight2 = 0;
        bEnableFlashlight2  = 0;
    }

    /* flashlight 0 */
    if (bEnableFlashlight != 0)
    {
        _fixed_array_verifyrange<G3DLIGHT>(flashlightId, NUM_GRA3DLIGHTID);

        pDest  = &pLDDest->aLight[flashlightId];
        *pDest = plyr_wrk.fl;

        gra3dSetLightIntens(pDest, pDest->afPad0[0] * pData->fAngleScale);

        {
            float  s  = pData->fDiffuseScale;
            float *pv = *(sceVu0FVECTOR *)pDest->vDiffuse;   /* -> float[4] */
            pv[0] *= s; pv[1] *= s; pv[2] *= s; pv[3] *= s;
        }
    }

    /* self-reflection light -> slot LID_POINT_SELFREFLECTION (0x15) */
    if (bEnableSelfreflection != 0)
    {
        _fixed_array_verifyrange<G3DLIGHT>(LID_POINT_SELFREFLECTION, NUM_GRA3DLIGHTID);
        pLDDest->aLight[LID_POINT_SELFREFLECTION] = plyr_wrk.reflectionlight;
    }

    /* flashlight 1 -> slot LID_POINT_FLASHLIGHT_1 (0x14), point only */
    if (plyr_wrk.fl2.Type == G3DLIGHT_POINT && bEnableFlashlight2 != 0)
    {
        _fixed_array_verifyrange<G3DLIGHT>(LID_POINT_FLASHLIGHT_1, NUM_GRA3DLIGHTID);
        pLDDest->aLight[LID_POINT_FLASHLIGHT_1] = plyr_wrk.fl2;
    }

    /* self-reflection slot status */
    pLDDest->aStatus[LID_POINT_SELFREFLECTION].bEnable                    = bEnableSelfreflection;
    pLDDest->aStatus[LID_POINT_SELFREFLECTION].bEnableToChar              = 1;
    pLDDest->aStatus[LID_POINT_SELFREFLECTION].bEnableToShadow            = 1;
    pLDDest->aStatus[LID_POINT_SELFREFLECTION].bEmulateToDirectionalLight = bEmulateSelfreflection;

    /* flashlight 0 slot status */
    pLDDest->aStatus[flashlightId].bEnable                    = bEnableFlashlight;
    pLDDest->aStatus[flashlightId].bEnableToChar              = 1;
    pLDDest->aStatus[flashlightId].bEnableToShadow            = 1;
    pLDDest->aStatus[flashlightId].bEmulateToDirectionalLight = bEmulateFlashlight;

    /* flashlight 1 slot status */
    pLDDest->aStatus[LID_POINT_FLASHLIGHT_1].bEnable                    = bEnableFlashlight2;
    pLDDest->aStatus[LID_POINT_FLASHLIGHT_1].bEnableToChar              = 1;
    pLDDest->aStatus[LID_POINT_FLASHLIGHT_1].bEnableToShadow            = 1;
    pLDDest->aStatus[LID_POINT_FLASHLIGHT_1].bEmulateToDirectionalLight = bEmulateFlashlight2;
}

/* --------------------------------------------------------------------------
 *  _SetLightToShadow
 *
 *  Install the player's room light data (with the character emulation applied)
 *  as the live light bank; when the projector spot is active it is copied into
 *  its slot and enabled.
 * ------------------------------------------------------------------------ */
static void _SetLightToShadow(GRA3DEMULATIONLIGHTDATACREATIONDATA *pData)
{
    GRA3DLIGHTDATA LD;
    int            iAreaId;
    GRA3DLIGHTDATA *pLDSrc;

    iAreaId = GetPlyrAreaNo();
    pLDSrc  = MapDrawGetLightPtr(iAreaId);
    gra3dGenerateLightDataToChar(&LD, pLDSrc, pData);

    if (gra3dIsSpecialLightActive() != 0)
    {
        GRA3DLIGHTID lightId = gra3dGetProjectorSpotId();
        G3DLIGHT    &rSpot   = gra3dGetProjectorSpot();

        _fixed_array_verifyrange<G3DLIGHT>(lightId, NUM_GRA3DLIGHTID);
        LD.aLight[lightId] = rSpot;
        LD.aStatus[lightId].bEnable = 1;
    }

    gra3dSetLightData(&LD, NULL);
}

/* --------------------------------------------------------------------------
 *  gra3dDrawSGDShadowCharacter
 *
 *  Cast a character's shadow: stream its model to VRAM, optionally draw the
 *  realtime shadow model, then (when character shadows are on and the shadow
 *  draw flag is set) compute the shadow light at the box base centre -- from
 *  the thunder-light effect when active, otherwise blended from the scene --
 *  and run the shadow pass.
 * ------------------------------------------------------------------------ */
void gra3dDrawSGDShadowCharacter(ANI_CTRL *pAC, SGDCOORDINATE *pCoord, float (*avBBWorld)[4],
                                 GRA3DEMULATIONLIGHTDATACREATIONDATA *pELDCD)
{
    float    vBBCenter[4];
    G3DLIGHT shadowlight;

    if (pAC != NULL)
    {
        SendEneVram(pAC->mdl_p, ENE_VADR);

        if (debug_var.shadow_model_disp != 0)
        {
            _gra3dDrawSGD((SGDFILEHEADER *)pAC->base_p, SRT_REALTIME, pCoord, -1);
        }

        if (g_gra3dShadowDebug.bDrawCharShadow != 0 && GetSdwDrawFLG() != 0)
        {
            gra3dbbCalcCenterBase(vBBCenter, avBBWorld);
            _SetLightToShadow(pELDCD);

            if (EffectThunderLightGetLightningFlg() == 0)
            {
                gra3dCalcShadowLight(&shadowlight, vBBCenter);
            }
            else
            {
                EffectThunderLightGetG3dLight(&shadowlight);
            }

            gra3dLightEnableAll(0);
            gra3dDrawSGDShadow((SGDFILEHEADER *)pAC->base_p, pCoord, &shadowlight, avBBWorld);
        }
    }
}

/* --------------------------------------------------------------------------
 *  gra3dStartSpecialLight / gra3dEndSpecialLight
 *
 *  Arm / disarm the projector spot light.
 * ------------------------------------------------------------------------ */
void gra3dStartSpecialLight(void)
{
    if (s_bSpecialLightActive == 0)
    {
        s_bSpecialLightActive = 1;
    }
}

void gra3dEndSpecialLight(void)
{
    s_bSpecialLightActive = 0;
}

/* --------------------------------------------------------------------------
 *  gra3dUpdateSpecialLight
 *
 *  Refresh the projector spot from the current room's projector-spot light:
 *  copy it, force its range (min 2600, max 2800), and flicker its colour to a
 *  random brightness in [0.4, 0.45].
 * ------------------------------------------------------------------------ */
void gra3dUpdateSpecialLight(void)
{
    int           iAreaId;
    int           iBuffId;
    GRA3DLIGHTDATA *pLD;
    GRA3DLIGHTID  lightId;
    float         f;

    if (s_bSpecialLightActive != 0)
    {
        iAreaId = GetPlyrAreaNo();
        iBuffId = MapLoadGetBuffID(iAreaId);
        pLD     = MapDrawGetLightPtr4BuffID2(iBuffId);

        lightId = gra3dGetProjectorSpotId();

        s_ProjectorSpot          = pLD->aLight[lightId];
        s_ProjectorSpot.fMinRange = 2600.0f;
        s_ProjectorSpot.fMaxRange = 2800.0f;

        /* random brightness in [0.4, 0.45] written to all three colour
         * channels (diffuse rgb). */
        f = 0.4f + 0.05f * ((float)((double)MioPan_Rand() / (double)MIOPAN_RAND_MAXF));
        std::fill(s_ProjectorSpot.vDiffuse, s_ProjectorSpot.vSpecular, f);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dIsSpecialLightActive
 * ------------------------------------------------------------------------ */
int gra3dIsSpecialLightActive(void)
{
    return s_bSpecialLightActive;
}

/* --------------------------------------------------------------------------
 *  gra3dGetProjectorSpot
 * ------------------------------------------------------------------------ */
G3DLIGHT &gra3dGetProjectorSpot(void)
{
    return s_ProjectorSpot;
}

/* --------------------------------------------------------------------------
 *  gra3dGetProjectorSpotId
 *
 *  The projector spot's slot id depends on the room: LID_SPOT_10 in rry05
 *  (area 0x23), LID_SPOT_3 in rtb05 (area 0x18); invalid elsewhere.
 * ------------------------------------------------------------------------ */
GRA3DLIGHTID gra3dGetProjectorSpotId(void)
{
    int          iAreaId;
    GRA3DLIGHTID GVar2;

    iAreaId = GetPlyrAreaNo();

    GVar2 = LID_SPOT_10;
    if (iAreaId != 0x23)
    {
        GVar2 = LID_SPOT_3;
        if (iAreaId != 0x18)
        {
            /* "iAreaId == rry05 || iAreaId == rtb05" */
            G3DRETURNVAL(iAreaId == 0x23 || iAreaId == 0x18, GRA3DLIGHTID_FORCE_DWORD, "");
            GVar2 = GRA3DLIGHTID_FORCE_DWORD;
        }
    }

    return GVar2;
}
