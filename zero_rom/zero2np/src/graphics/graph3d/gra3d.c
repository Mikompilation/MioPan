/* ==========================================================================
 *  gra3d.c
 *
 *  The gra3d facade layer over the g3d core: camera setup and stabilization,
 *  the light bank (push/pop/emulate/blend, per-type VU1 light packets), fog,
 *  material -> VU1 material packets, transform-state forwarding, GS-register
 *  set/get dispatch, scratch-pad micro-memory management, and the init/draw
 *  entry points.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3d.h"
#include "g3dLightEx.h"
#include "g3dCore.h"
#include "g3dCamera.h"
#include "g3dGeom.h"
#include "g3dLight.h"
#include "g3dMath.h"
#include "g3dDma.h"
#include "g3dRenderTarget.h"
#include "g3dVif1.h"
#include "g3dUtil.h"            /* indexof<> */
#include "gra3dBoundingBox.h"
#include "gra3dConst.h"
#include "gra3dDebug.h"
#include "gra3dDma.h"
#include "gra3dTypes.h"
#include "sgd_types.h"
#include "gra3dSGD.h"           /* gra3dsgdInit, _gra3dDrawSGD */
#include "gra3dSGDData.h"
#include "gra3dShadow.h"        /* gra3dshadowInit */
#include "g3ddbg.h"
#include "g3dxVu0.h"            /* VU0 macro-mode intrinsics */
#include "gra3dVu0.h"           /* gra3dVu0ClearMatrix / ApplyMatrixToLMatrix / CopyLMatrix */
#include "ctl/fixed_array.h"   /* fixed_array<G3DLIGHT,39> etc. (per-TU helpers) */
#include "miopan/rendering/miopan_graph3d.h"
#include "miopan/rendering/miopan_renderer.h"   /* MioPan_RendererGetViewExtend */
#include "../draw_env.h"
#include "../../system/os/system.h"
#include "../../ingame/map/MhCtl.h"
#include <pcport_stubs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mathf.h>
#include <algorithm>           /* std::sort / std::max_element / std::min */

#define SPR_PTR(byteoff, type) ((type *)&g_sceScratchpad[(byteoff) / 4])
#define SPR_FMATRIX(byteoff)  ((float (*)[4])&g_sceScratchpad[(byteoff) / 4])
#define SPR_FLOAT(byteoff)    (g_sceScratchpad[(byteoff) / 4])

#define SPR_GRA3DSCRATCHPADLAYOUT            SPR_PTR(0x0000, GRA3DSCRATCHPADLAYOUT)
#define SPR_GRA3DSCRATCHPADLAYOUT_MAPSHADOW  SPR_PTR(0x0440, GRA3DSCRATCHPADLAYOUT_MAPSHADOW)
#define SPR_G3DCOREOBJECT                    SPR_PTR(0x05f0, G3DCOREOBJECT)

/* u_long, not the ROM's `long int`, to match draw_env.h -- see the note there
 * about the EE's 64-bit `long`.  gra3dSetGsRegister() itself still takes
 * `long int`, so a register written through _SetRegisterSpecified() has
 * already lost anything above bit 31 by the time it arrives; only the callers
 * that reach draw_env directly carry the full 64 bits today. */
typedef void (*LPFUNC_SETREGISTER_WITHCONTEXT_GRA3D)(int context_no, u_long data);
typedef void (*LPFUNC_SETREGISTER_GRA3D)(u_long data);
typedef u_long (*LPFUNC_GETREGISTER_WITHCONTEXT_GRA3D)(int context_no);
typedef u_long (*LPFUNC_GETREGISTER_GRA3D)(void);

struct SETREGISTERPAIR
{
    long int lAddress;
    LPFUNC_SETREGISTER_WITHCONTEXT_GRA3D pFuncWithContext;
    LPFUNC_SETREGISTER_GRA3D pFunc;
};

struct GETREGISTERPAIR
{
    long int lAddress;
    LPFUNC_GETREGISTER_WITHCONTEXT_GRA3D pFuncWithContext;
    LPFUNC_GETREGISTER_GRA3D pFunc;
};

CVu0Matrix g_Vu0Matrix;
LPFUNC_VU0LOADMATRIX CVu0Matrix::s_pFuncLoadMatrix;
LPFUNC_VU0APPLYMATRIXWITHOUTTRANS CVu0Matrix::s_pFuncApplyMatrixWithoutTrans;

static int s_bEnableMonotoneDraw;
static int s_bUseScratchpad = 1;
static GRA3DSCRATCHPADLAYOUT s_gra3dScratchpadLayoutDefault;
static GRA3DSCRATCHPADLAYOUT *s_pScratchpadLayout;
static G3DLIGHTMANAGE s_LightManage;
static G3DLIGHTMANAGE s_WorkLightManage;
static GRA3DCAMERA s_Camera;
static G3DFOG s_Fog;
static sceVu0IVECTOR s_ivFogColor;
static float clip_value[4];
static G3DLIGHT s_aLight[NUM_GRA3DLIGHTID];
static G3DLIGHT s_WorkaLight[NUM_GRA3DLIGHTID];
static float s_lmDiffuseLight[3][4];
static float s_lmSpecularLight[3][4];
static float s_lmDiffuseColor[3][4];
static float s_lmSpecularColor[3][4];
static unsigned int s_uiMaterialPrimType;
static SGDMATERIALCACHE s_aMaterialCache[3];
/* PORT -- these five are one object, laid out in the ROM's .bss order.
 *
 * _ClearMaterialData() memsets 0x150 bytes into the 0x80-byte cache it is
 * handed (see the note on s_Vu1MaterialDirImage below), so every point or spot
 * rebuild also zeroes whatever follows that cache.  On the EE that was these
 * five, back to back from 0x4b3880: the point clear runs through the spot
 * cache and 0x50 bytes of the directional packet, the spot clear through the
 * directional packet and 0x50 bytes of the point one.
 *
 * As five separate statics the overrun hit whatever the host compiler placed
 * after the cache.  -O0 keeps declaration order, which is why the Debug build
 * behaved like the ROM.  -O2/-O3 emit this file's .bss in reverse, so in
 * RelWithDebInfo and Release the spot clear wiped the point cache it had just
 * been handed, and the point clear zeroed s_uiMaterialPrimType,
 * s_lmSpecularColor, s_lmDiffuseColor and all of s_lmSpecularLight.  And
 * Ubuntu's GCC fortifies memset by default (_FORTIFY_SOURCE=3), so the Linux
 * release build aborted on the first room drawn with
 * "*** buffer overflow detected ***".
 *
 * As one object the overrun reaches the ROM's own neighbours on every host,
 * and a fortified memset of 0x150 from either cache stays inside its 0x280
 * bytes.  The old names are references, so every use reads as it did. */
static struct
{
    GRA3DVU1MATERIALCACHE_POINT        aVu1MaterialCache_Point[1];  /* bss 4b3880 */
    GRA3DVU1MATERIALCACHE_SPOT         aVu1MaterialCache_Spot[1];   /* bss 4b3900 */
    GRA3DVU1MATERIALPACKET_DIRECTIONAL MaterialPacketDirectional;   /* bss 4b3980 */
    GRA3DVU1MATERIALPACKET_POINT       MaterialPacketPoint;         /* bss 4b3a00 */
    GRA3DVU1MATERIALPACKET_SPOT        MaterialPacketSpot;          /* bss 4b3a80 */
} s_Vu1MaterialBss;
static GRA3DVU1MATERIALCACHE_POINT (&s_aVu1MaterialCache_Point)[1] = s_Vu1MaterialBss.aVu1MaterialCache_Point;
static GRA3DVU1MATERIALCACHE_SPOT (&s_aVu1MaterialCache_Spot)[1]   = s_Vu1MaterialBss.aVu1MaterialCache_Spot;
static GRA3DVU1MATERIALPACKET_DIRECTIONAL &s_MaterialPacketDirectional = s_Vu1MaterialBss.MaterialPacketDirectional;
static GRA3DVU1MATERIALPACKET_POINT       &s_MaterialPacketPoint       = s_Vu1MaterialBss.MaterialPacketPoint;
static GRA3DVU1MATERIALPACKET_SPOT        &s_MaterialPacketSpot        = s_Vu1MaterialBss.MaterialPacketSpot;
/* PORT ADDITION -- host mirror of the light half of VU1 memory (VU 25..45).
 * g3dSetVu1LightData() builds that block straight into a DMA packet the host
 * never executes, so the last one built is kept here for the GPU vertex path.
 * Holding it across draws is faithful rather than convenient: the ROM only
 * re-unpacks it when CheckCoordCache() misses, so VU1 memory really does keep
 * the previous coordinate's lights until then. */
static GRA3DVU1LIGHTDATA s_Vu1LightImage;

/* PORT ADDITION -- host mirror of the material half of VU1 memory (VU 46..66).
 *
 * It cannot read s_MaterialPacket* directly.  Those are SCRATCH on hardware:
 * gra3dCalcVu1MaterialData*() copies each one into the emitted DMA packet and
 * the VU1 gets its copy from there, so the static is free to be recycled --
 * and it is.  _ClearMaterialData() memsets 0x150 bytes (ROM 0x001b20b8,
 * `li a2,0x150`) into the 0x80-byte cache it is handed, and the ROM's own .bss
 * puts s_aVu1MaterialCache_Spot at 0x4b3900 with s_MaterialPacketDirectional
 * 0x80 later at 0x4b3980 -- so every _SetVu1LightData_Spot() wipes the
 * directional packet and half the point one.  Harmless on the EE, fatal to a
 * host path that reads the static at draw time instead of the packet.
 *
 * Measured: the packet held a correct 6.40 immediately after the build and 0.00
 * when the mesh was drawn, which blacked out whole runs of character meshes.
 * Mirroring at the copy-out point is the same thing s_Vu1LightImage does for
 * the light half, and it keeps the ROM's overrun reproduced rather than
 * papered over. */
static GRA3DVU1MATERIALDATA_DIRECTIONAL s_Vu1MaterialDirImage;
static GRA3DVU1MATERIALDATA_POINT       s_Vu1MaterialPointImage;
static GRA3DVU1MATERIALDATA_SPOT        s_Vu1MaterialSpotImage;

static int s_bFogEnable;
static float clip_volume[4] = { 1920.0f, 1792.0f, 256.0f, 16777000.0f };
static float clip_volumev[4] = { 320.0f, 224.0f, 0.099999994f, 16777000.0f };
static G3DVIF1CMDDATA s_aVif1CmdData[6] =
{
    { 0x03000060, { 0, 0, 0, 0 } },
    { 0x020001d0, { 0, 0, 0, 0 } },
    { 0x01000404, { 0, 0, 0, 0 } },
    { 0x20000000, { 0, 0, 0, 0 } },
    { 0x05000000, { 0, 0, 0, 0 } },
    { 0x30000000, { 0, 0, 0x3f800000, 0x3f800000 } },
};
static int s_bLightEnableList[NUM_GRA3DLIGHTID];
static LPFUNC_VIEWSCREENMATRIX s_apViewScreenMatrixFunc[2] =
{
    g3dCalcViewScreenMatrixPerspective,
    g3dCalcViewScreenMatrixOrtho,
};
static LPFUNC_VIEWCLIPMATRIX s_apViewClipMatrixFunc[2] =
{
    g3dCalcViewClipMatrixPerspective,
    g3dCalcViewClipMatrixOrtho,
};
static G3DINTFLOAT s_if_1_255 = { 0x3b808080 };
static sceGifPackAd s_aGsRegisterDefault[5] =
{
    { 0x0000000000000044ULL, SCE_GS_ALPHA_1 },
    { 0x0000000000000060ULL, SCE_GS_TEX1_1 },
    { 0x0000000000000000ULL, SCE_GS_CLAMP_1 },
    { 0x000000000005001bULL, SCE_GS_TEST_1 },
    { 0x000000000a000118ULL, SCE_GS_ZBUF_1 },
};
static const float g_fPALAspectScale = 1.142857193f;

template <class T>
static T *GetStaticInstance(void)
{
    static T obj;
    return &obj;
}

static void _Vu0LoadMatrix_4_5_6_7(float (*mat)[4]);
static void _Vu0ApplyMatrixWithoutTrans_4_5_6_7(float *vDest, float *vSrc);
void gra3dExecPrelight(SGDFILEHEADER *pSGDHead, float (*mat)[4]);

static int _Gra3dSetGsRegisterForAutoState(long int lData, long int lAddress, int iDmaChan)
{
    (void)iDmaChan;
    return gra3dSetGsRegister(lData, lAddress);
}

static int _Gra3dSetGsRegistersForAutoState(const sceGifPackAd *aGPA, int iNum, int iDmaChan)
{
    (void)iDmaChan;
    return gra3dSetGsRegisters((sceGifPackAd *)aGPA, iNum);
}

static int _Gra3dSetTransformForAutoState(G3DTRANSFORMSTATETYPE State, const float (*mat)[4])
{
    return gra3dSetTransform((GRA3DTRANSFORMSTATETYPE)State, (float (*)[4])mat);
}

static float (&_Gra3dGetTransformRefForAutoState(G3DTRANSFORMSTATETYPE State))[4][4]
{
    return gra3dGetTransformRef((GRA3DTRANSFORMSTATETYPE)State);
}

static int g3dIsValidLightType(G3DLIGHTTYPE type)
{
    return 0 <= type && type < NUM_G3DLIGHTTYPE;
}

static G3DLIGHTTYPE gra3dGetLightType(int iLightId)
{
    if (iLightId <= GRA3D_END_LIGHT_DIRECTIONAL)
    {
        return G3DLIGHT_DIRECTIONAL;
    }
    if (iLightId <= GRA3D_END_LIGHT_POINT)
    {
        return G3DLIGHT_POINT;
    }
    if (iLightId <= GRA3D_END_LIGHT_SPOT)
    {
        return G3DLIGHT_SPOT;
    }

    return G3DLIGHTTYPE_FORCE_DWORD;
}

static void gra3dAddPositionOffset(float *vDest, const float *vSrc, const float *vOffset)
{
    vDest[0] = vSrc[0] + vOffset[0];
    vDest[1] = vSrc[1] + vOffset[1];
    vDest[2] = vSrc[2] + vOffset[2];
    vDest[3] = vSrc[3];
}

/* ==========================================================================
 *  gra3d.c -- PART A   (0x001afe10 .. 0x001b1ce7 inclusive)
 *
 *  Reconstructed function bodies only.  No banner / no #include -- these are
 *  merged into the assembled gra3d.c by the integrator.  Functions are in
 *  address order.  Module statics referenced here (s_bEnableMonotoneDraw,
 *  s_LightManage, s_WorkLightManage, s_Camera, g_CameraDefault, s_Fog,
 *  clip_value, s_aLight, s_WorkaLight, s_apViewScreenMatrixFunc,
 *  s_apViewClipMatrixFunc, g_Vu0Matrix) live elsewhere in the file.
 * ======================================================================== */

/* Compiler-generated copies of _fixed_array_assert / _fixed_array_verifyrange<T>
 * (001afd38..001afe0f) are the inlined ctl/fixed_array.h template; not emitted. */

/* --------------------------------------------------------------------------
 *  _MakeColorToMonotone (float)
 *
 *  When monotone draw is on, collapse an RGB float colour to its grey
 *  average (r+g+b)/3 in all three channels.  The dot with (1,1,1) is the
 *  inlined VU0 add-across from g3dxVu0.h.
 * ------------------------------------------------------------------------ */
static void _MakeColorToMonotone(float *v)
{
    float fAverage;

    if (s_bEnableMonotoneDraw != 0)
    {
        /* fAverage = (v[0] + v[1] + v[2]) / 3 */
        fAverage = (v[0] + v[1] + v[2]) / 3.0f;

        std::fill(v, v + 3, fAverage);
    }
}

/* --------------------------------------------------------------------------
 *  _MakeColorToMonotone (int)
 *
 *  Integer overload: same grey-average, written back to all three channels.
 * ------------------------------------------------------------------------ */
static void _MakeColorToMonotone(int *v)
{
    if (s_bEnableMonotoneDraw != 0)
    {
        int iAverage = (v[0] + v[1] + v[2]) / 3;
        v[0] = iAverage;
        v[1] = iAverage;
        v[2] = iAverage;
    }
}

/* --------------------------------------------------------------------------
 *  _InitLight
 *
 *  Reset the light subsystem: disable all three light types, clear every
 *  GRA3DLIGHTSTATUS slot, and prime the 39 light slots with the type-correct
 *  default light.
 * ------------------------------------------------------------------------ */
static void _InitLight(void)
{
    int          i;
    G3DLIGHTTYPE iLightType;

    for (i = G3DLIGHT_DIRECTIONAL; i < NUM_G3DLIGHTTYPE; i++)
    {
        gra3dEnableLightType((G3DLIGHTTYPE)i, 0);
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i = i + 1)
    {
        s_LightManage.aStatus[i].bEnable                   = 0;
        s_LightManage.aStatus[i].bEnableToChar             = 0;
        s_LightManage.aStatus[i].bEnableToShadow           = 0;
        s_LightManage.aStatus[i].bEmulateToDirectionalLight = 0;
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i = i + 1)
    {
        if (i <= GRA3D_END_LIGHT_DIRECTIONAL)
        {
            iLightType = G3DLIGHT_DIRECTIONAL;
        }
        else if (i <= GRA3D_END_LIGHT_POINT)
        {
            iLightType = G3DLIGHT_POINT;
        }
        else if (i <= GRA3D_END_LIGHT_SPOT)
        {
            iLightType = G3DLIGHT_SPOT;
        }
        else
        {
            iLightType = G3DLIGHTTYPE_FORCE_DWORD;
        }

        g3dutilSetLightDefault(&s_aLight[i], iLightType);
    }
}

/* --------------------------------------------------------------------------
 *  _InitFog
 *
 *  Default fog: min/max/near at 0, far at the engine far constant (1e7),
 *  fog enabled.
 * ------------------------------------------------------------------------ */
static void _InitFog(void)
{
    s_Fog.fMin  = 0.0f;
    s_Fog.fMax  = 0.0f;
    s_Fog.fNear = 0.0f;
    s_Fog.fFar  = 10000000.0f;

    gra3dEnableFog(1);
}

/* --------------------------------------------------------------------------
 *  _InitCamera
 *
 *  Copy the default camera into the module camera and apply it.
 * ------------------------------------------------------------------------ */
static void _InitCamera(void)
{
    s_Camera = g_CameraDefault;

    printf("gra3d camera clip init: near=%.9g far=%.9g ratio=%.9g\n",
           s_Camera.fNearZ, s_Camera.fFarZ,
           s_Camera.fFarZ / s_Camera.fNearZ);

    gra3dApplyCamera(NULL, 1);
}

/* --------------------------------------------------------------------------
 *  gra3dLightDataAddOffsetPosition
 *
 *  Copy a light-data record and shift every non-directional light's position
 *  by vPosition.  The offset positions must stay homogeneous (w == 1).
 * ------------------------------------------------------------------------ */
void gra3dLightDataAddOffsetPosition(GRA3DLIGHTDATA *pDest, const GRA3DLIGHTDATA *pSrc, const float *vPosition)
{
    *pDest = *pSrc;

    for (int i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        G3DLIGHT &rL = pDest->aLight[i];

        if (rL.Type != G3DLIGHT_DIRECTIONAL)
        {
            gra3dAddPositionOffset(rL.vPosition, rL.vPosition, vPosition);

            /* "maybe lightdata is illegal" */
            G3DASSERT(rL.vPosition[3] == 1.0f, "maybe lightdata is illegal");
        }
    }
}

/* --------------------------------------------------------------------------
 *  gra3dLightEnableAll
 * ------------------------------------------------------------------------ */
void gra3dLightEnableAll(int bEnable)
{
    for (int i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        gra3dLightEnable(i, bEnable);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dLightEnablePush / gra3dLightEnablePop  (stubs in the prototype)
 * ------------------------------------------------------------------------ */
void gra3dLightEnablePush(void)
{
}

void gra3dLightEnablePop(void)
{
}

/* --------------------------------------------------------------------------
 *  gra3dLightPushData
 *
 *  Snapshot the live light bank: copy the GRA3DLIGHTSTATUS array into the
 *  work bank, then copy each enabled light's G3DLIGHT into the work lights.
 * ------------------------------------------------------------------------ */
void gra3dLightPushData(void)
{
    s_WorkLightManage = s_LightManage;

    for (int i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        if (gra3dIsLightEnable(i) != 0)
        {
            s_WorkaLight[i] = gra3dGetLightRef(i);
        }
    }
}

/* --------------------------------------------------------------------------
 *  gra3dLightPopData
 *
 *  Restore the light bank from the work snapshot taken by gra3dLightPushData.
 * ------------------------------------------------------------------------ */
void gra3dLightPopData(void)
{
    int i;

    s_LightManage = s_WorkLightManage;

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        if (gra3dIsLightEnable(i) != 0)
        {
            gra3dSetLight(i, &s_WorkaLight[i]);
        }
    }
}

/* --------------------------------------------------------------------------
 *  gra3dSetLightData
 *
 *  Push a full light-data record into the engine: set each slot's status,
 *  and for every enabled slot copy out the G3DLIGHT, offset its position by
 *  vTrans (non-directional only), then install it.  Finally set the ambient
 *  and re-apply all lights.
 * ------------------------------------------------------------------------ */
void gra3dSetLightData(GRA3DLIGHTDATA *pLightData, float *vTrans)
{
    G3DLIGHT L;

    for (int i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        GRA3DLIGHTSTATUS *pv0 = &pLightData->aStatus[i];

        gra3dSetLightStatus(i, pv0);

        if (pv0->bEnable != 0)
        {
            L = pLightData->aLight[i];

            if (vTrans != NULL)
            {
                if (L.Type != G3DLIGHT_DIRECTIONAL)
                {
                    gra3dAddPositionOffset(L.vPosition, L.vPosition, vTrans);
                }
            }

            /* "L.vPosition[3] : %f" */
            G3DASSERT(L.vPosition[3] == 1.0f, "L.vPosition[3] : %f", (double)L.vPosition[3]);

            gra3dSetLight(i, &L);
        }
    }

    g3dSetAmbient(pLightData->vAmbient);
    gra3dApplyLight();
}

/* --------------------------------------------------------------------------
 *  gra3dEmulateLightData
 *
 *  Build a destination light-data record in which the lights flagged
 *  "emulate to directional" (and enabled-to-char) are replaced by up to three
 *  synthesised directional lights.  Each candidate is converted to a
 *  G3D_EMULATE_DIRECTIONALLIGHT_DATA, the set is partial-sorted by length
 *  (greater-first), the strongest three are regenerated as directional lights
 *  and scaled by fMagnification.  Slot enables are rebuilt accordingly.
 * ------------------------------------------------------------------------ */
void gra3dEmulateLightData(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc, float *vPosition, float fMagnification)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA  raEmuDirLight[NUM_GRA3DLIGHTID];
    int                                iNumLightEnable;
    int                                i;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA *__first;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA *__last;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA::greater __comp;

    /* "fMagnification >= 0.0f" (caller frame: gra3dEmulateLightDataSub) */
    G3DASSERT(fMagnification >= 0.0f, "");

    memset(raEmuDirLight, 0, sizeof(raEmuDirLight));
    iNumLightEnable = 0;

    g3dxVu0CopyVector(pLDDest->vAmbient, pLDSrc->vAmbient);

    pLDDest->aStatus = pLDSrc->aStatus;

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        if (pLDSrc->aStatus[i].bEnable != 0
            && pLDSrc->aStatus[i].bEmulateToDirectionalLight != 0
            && pLDSrc->aStatus[i].bEnableToChar != 0)
        {
            g3dEmulateDirectionalLight(&raEmuDirLight[iNumLightEnable], &pLDSrc->aLight[i], vPosition);

            if (raEmuDirLight[iNumLightEnable].fLength > 0.0f)
            {
                iNumLightEnable++;
            }
        }
    }

    __first = raEmuDirLight;
    __last  = raEmuDirLight + iNumLightEnable;
    __comp  = G3D_EMULATE_DIRECTIONALLIGHT_DATA::greater();
    std::sort(__first, __last, __comp);

    g3dGenerateDirectionalLightByEmulatedData(pLDDest->aLight.data(), raEmuDirLight, iNumLightEnable);

    iNumLightEnable = std::min(iNumLightEnable, 3);

    for (i = 0; i < iNumLightEnable; i++)
    {
        sceVu0ScaleVector(pLDDest->aLight[i].vDiffuse, pLDDest->aLight[i].vDiffuse, fMagnification);
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        int bEnable;

        if (pLDSrc->aStatus[i].bEmulateToDirectionalLight == 0 && pLDSrc->aStatus[i].bEnable != 0)
        {
            bEnable = 1;
        }
        else if (i < 0)
        {
            bEnable = 0;
        }
        else if (i < iNumLightEnable)
        {
            bEnable = 1;
        }
        else
        {
            bEnable = 0;
        }

        pLDDest->aStatus[i].bEnable = bEnable;
    }
}

/* --------------------------------------------------------------------------
 *  gra3dEmulateLightDataObj
 *
 *  As gra3dEmulateLightData, but the candidate test ignores bEnableToChar
 *  (object lights, not character lights).
 * ------------------------------------------------------------------------ */
void gra3dEmulateLightDataObj(GRA3DLIGHTDATA *pLDDest, GRA3DLIGHTDATA *pLDSrc, float *vPosition, float fMagnification)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA  raEmuDirLight[NUM_GRA3DLIGHTID];
    int                                iNumLightEnable;
    int                                i;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA *__first;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA *__last;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA::greater __comp;

    /* "fMagnification >= 0.0f" (caller frame: gra3dEmulateLightDataSub) */
    G3DASSERT(fMagnification >= 0.0f, "");

    memset(raEmuDirLight, 0, sizeof(raEmuDirLight));
    iNumLightEnable = 0;

    g3dxVu0CopyVector(pLDDest->vAmbient, pLDSrc->vAmbient);

    pLDDest->aStatus = pLDSrc->aStatus;

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        if (pLDSrc->aStatus[i].bEnable != 0
            && pLDSrc->aStatus[i].bEmulateToDirectionalLight != 0)
        {
            g3dEmulateDirectionalLight(&raEmuDirLight[iNumLightEnable], &pLDSrc->aLight[i], vPosition);

            if (raEmuDirLight[iNumLightEnable].fLength > 0.0f)
            {
                iNumLightEnable = iNumLightEnable + 1;
            }
        }
    }

    __first = raEmuDirLight;
    __last  = raEmuDirLight + iNumLightEnable;
    __comp  = G3D_EMULATE_DIRECTIONALLIGHT_DATA::greater();
    std::sort(__first, __last, __comp);

    g3dGenerateDirectionalLightByEmulatedData(pLDDest->aLight.data(), raEmuDirLight, iNumLightEnable);

    iNumLightEnable = std::min(iNumLightEnable, 3);

    for (i = 0; i < iNumLightEnable; i++)
    {
        sceVu0ScaleVector(pLDDest->aLight[i].vDiffuse, pLDDest->aLight[i].vDiffuse, fMagnification);
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        int bEnable;

        if (pLDSrc->aStatus[i].bEmulateToDirectionalLight == 0 && pLDSrc->aStatus[i].bEnable != 0)
        {
            bEnable = 1;
        }
        else if (i < 0)
        {
            bEnable = 0;
        }
        else if (i < iNumLightEnable)
        {
            bEnable = 1;
        }
        else
        {
            bEnable = 0;
        }

        pLDDest->aStatus[i].bEnable = bEnable;
    }
}

/* --------------------------------------------------------------------------
 *  _SetClipValue
 *
 *  Store the current clip rectangle (minx, maxx, miny, maxy) into the
 *  module clip_value cache.
 * ------------------------------------------------------------------------ */
static void _SetClipValue(float minx, float maxx, float miny, float maxy)
{
    clip_value[0] = minx;
    clip_value[1] = maxx;
    clip_value[2] = miny;
    clip_value[3] = maxy;
}

/* --------------------------------------------------------------------------
 *  _GetClipValueCheck
 *
 *  Return non-zero if the clip rectangle differs from the canonical "no clip"
 *  value (all -1.0f); zero when it matches.
 * ------------------------------------------------------------------------ */
static int _GetClipValueCheck(void)
{
    float vClip[4];

    vClip[0] = -1.0f;
    vClip[1] =  1.0f;
    vClip[2] = -1.0f;
    vClip[3] =  1.0f;

    return memcmp(vClip, clip_value, sizeof(vClip)) != 0;
}

/* --------------------------------------------------------------------------
 *  gra3dIsBBInViewvolume
 *
 *  Test a bounding box against the view volume.  Asserts the clip cache is at
 *  its default; delegates to gra3dbbIsInViewvolume.
 *
 *  avBBClipped is dead in the original too -- it is never written and never
 *  passed on -- but it is part of the ROM's signature, so it stays.
 *
 *  PORT DEVIATION.  matClip / matTransform were the VU0 register groups the
 *  caller had loaded (vf4..vf7 and vf8..vf11); with no host register file they
 *  are explicit parameters.  matTransform may be NULL, see gra3dbbIsInViewvolume.
 * ------------------------------------------------------------------------ */
int gra3dIsBBInViewvolume(float (*avBBTransformed)[4], float (*avBBClipped)[4],
                          float (*avBBWorld)[4],
                          float (*matClip)[4], float (*matTransform)[4])
{
    G3DASSERT(!_GetClipValueCheck(), "");

    return gra3dbbIsInViewvolume(avBBTransformed, avBBWorld, matClip, matTransform) != 0;
}

/* --------------------------------------------------------------------------
 *  CheckModelBoundingBox
 *
 *  Test a model's *world-space* bounding box against the view volume, using the
 *  camera's world->clip(object) matrix -- the tight, screen-sized frustum.  No
 *  local->world concatenation here: the box is already in world space, which is
 *  why the ROM _lqc2'd matWorldClipObject straight into vf4..vf7.
 *
 *  It loaded nothing into vf8..vf11, so the corners written back to avWork came
 *  out of whatever matrix the previous caller had left in those registers.
 *  avWork is a dead local -- nothing here reads it -- so the port passes NULL
 *  and the store is skipped rather than reproducing garbage.
 * ------------------------------------------------------------------------ */
int CheckModelBoundingBox(float (*avBBWorld)[4])
{
    GRA3DCAMERA *pCam;
    float        avWork[16][4];

    pCam = gra3dGetCamera();

    return gra3dIsBBInViewvolume(avWork + 8, avWork, avBBWorld,
                                 pCam->matWorldClipObject, NULL) != 0;
}

/* --------------------------------------------------------------------------
 *  gra3dExecPrelight (vTrans, vRot)
 *
 *  Convenience overload: wrap each Euler rotation component into (-PI,PI],
 *  build a rotation matrix and translation in the VU0 scratch matrix at
 *  0x70003900, then forward to the matrix overload.
 * ------------------------------------------------------------------------ */
void gra3dExecPrelight(SGDFILEHEADER *pSGDHead, float *vTrans, float *vRot)
{
    float vRotWrapped[4];
    float f;

    G3DRETURN((pSGDHead), "");

    /* wrap each axis into (-PI, PI] */
    f = vRot[0];
    if (f > 3.141592502593994f)
    {
        f = fmodf(f, 6.283185005187988f);
        if (f > 3.141592502593994f)
        {
            f = f - 6.283185005187988f;
        }
        else if (f < -3.141592502593994f)
        {
            f = f + 6.283185005187988f;
        }
    }
    vRotWrapped[0] = f;

    f = vRot[1];
    if (f > 3.141592502593994f)
    {
        f = fmodf(f, 6.283185005187988f);
        if (f > 3.141592502593994f)
        {
            f = f - 6.283185005187988f;
        }
        else if (f < -3.141592502593994f)
        {
            f = f + 6.283185005187988f;
        }
    }
    vRotWrapped[1] = f;

    f = vRot[2];
    if (f > 3.141592502593994f)
    {
        f = fmodf(f, 6.283185005187988f);
        if (f > 3.141592502593994f)
        {
            f = f - 6.283185005187988f;
        }
        else if (f < -3.141592502593994f)
        {
            f = f + 6.283185005187988f;
        }
    }
    vRotWrapped[2] = f;

    /* Build rotation into the VU0 scratch matrix, then drop in the translation.
     *
     * The seed matrix is g_matConvertSI2PS -- diag(25, -25, -25, 1) -- not the
     * identity.  Measured in the ROM: gra3dExecPrelight(SGDFILEHEADER *, float
     * const *, float const *) at 0x001b0f30 materialises 0x3b50d0 into s3
     * (lui v0,0x3b / addiu s3,v0,0x50d0) and passes it as a1 to sceVu0RotMatrix
     * (jal 0x0028a3e0).  ZERO2.MAP puts g_matConvertSI2PS at 0x3b50d0;
     * g_matUnit is a different symbol at 0x3b4f30.
     *
     * It has to be this one: MapDrawCalcRoomCoord() stamps the same
     * g_matConvertSI2PS into the very pCoord->matCoord that sgdCalcCoordinate()
     * overwrites here, so the bake and the realtime draw of the room mesh share
     * a basis.  Seeding the identity instead baked the room at 1/25 scale with Y
     * and Z un-mirrored, which left every vertex near the room origin while the
     * room's lights carry the full-scale offset -- the distance ramp then
     * clamped every point and spot light away and only the ambient floor
     * survived (0.002 * 255 = 0.51, i.e. code value 0).  The room went perfectly
     * black on the first monotone toggle, because gra3dPrelightScene() is the
     * only thing in the tree that ever rebakes hp->model_addr. */
    sceVu0RotMatrix(SPR_FMATRIX(0x3900), g_matConvertSI2PS, vRotWrapped);
    SPR_FLOAT(0x3930) = vTrans[0];
    SPR_FLOAT(0x3934) = vTrans[1];
    SPR_FLOAT(0x3938) = vTrans[2];
    SPR_FLOAT(0x393c) = 1.0f;

    gra3dExecPrelight(pSGDHead, SPR_FMATRIX(0x3900));
}

/* --------------------------------------------------------------------------
 *  gra3dExecPrelight (mat)
 *
 *  Run the SGD prelighting pass: validate the SGD header/version, transform
 *  the coordinate tree by mat, draw the prelighting pass, then clear the
 *  coordinate's calc flag.
 * ------------------------------------------------------------------------ */
void gra3dExecPrelight(SGDFILEHEADER *pSGDHead, float (*mat)[4])
{
    G3DRETURN(pSGDHead, "");
    G3DRETURN(pSGDHead->uiVersionId == SGD_VALID_VERSIONID, "");

    /* "mat[3][3] : %f" */
    G3DASSERT(mat[3][3] == 1.0f, "mat[3][3] : %f", mat[3][3]);
    /* "no coord data" */
    G3DASSERT(pSGDHead->pCoord, "no coord data");

    sgdCalcCoordinate(pSGDHead, mat);
    _gra3dDrawSGD(pSGDHead, SRT_PRELIGHTING, NULL, -1);
    pSGDHead->pCoord->bCalc = 0;
}

/* --------------------------------------------------------------------------
 *  gra3dGetCamera
 * ------------------------------------------------------------------------ */
GRA3DCAMERA *gra3dGetCamera(void)
{
    return &s_Camera;
}

/* --------------------------------------------------------------------------
 *  gra3dcamGetPosition / Direction / Target
 * ------------------------------------------------------------------------ */
float (&gra3dcamGetPosition(void))[4]
{
    return gra3dGetCamera()->matCoord[3];
}

float (&gra3dcamGetDirection(void))[4]
{
    return gra3dGetCamera()->matCoord[2];
}

float (&gra3dcamGetTarget(void))[4]
{
    return gra3dGetCamera()->vTarget;
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetPosition (vector)
 *
 *  Save the old position then move the camera, keeping its orientation.
 * ------------------------------------------------------------------------ */
void gra3dcamSetPosition(float *vPos)
{
    sceVu0CopyVector(s_Camera.vPositionOld, s_Camera.matCoord[3]);

    g3dMatrixSetPosition(s_Camera.matCoord, vPos);
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetPosition (x, y, z)
 * ------------------------------------------------------------------------ */
void gra3dcamSetPosition(float x, float y, float z)
{
    float v[4];

    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 1.0f;

    gra3dcamSetPosition(v);
}

/* --------------------------------------------------------------------------
 *  gra3dcamGetPositionOld
 * ------------------------------------------------------------------------ */
float (&gra3dcamGetPositionOld(void))[4]
{
    return s_Camera.vPositionOld;
}

/* --------------------------------------------------------------------------
 *  _ResetCameraTarget
 *
 *  Recompute the camera target from the camera position + the normalised
 *  forward axis scaled by the current position->target distance.
 * ------------------------------------------------------------------------ */
static void _ResetCameraTarget(void)
{
    sceVu0FVECTOR vDir;

    /* Two inlined COP2 blocks and the store, nothing else: the first is the
     * position->target distance, the second normalises the forward axis,
     * scales it by that distance and adds it back onto the position.  Both
     * work on all four lanes -- the original masks neither the vmulq nor the
     * vmulbc -- so vTarget[3] tracks matCoord[3][3] rather than being reset. */
    float dist = g3dxVu0Length3(s_Camera.matCoord[3], s_Camera.vTarget);
    g3dxVu0Normalize(vDir, s_Camera.matCoord[2]);

    s_Camera.vTarget[0] = s_Camera.matCoord[3][0] + vDir[0] * dist;
    s_Camera.vTarget[1] = s_Camera.matCoord[3][1] + vDir[1] * dist;
    s_Camera.vTarget[2] = s_Camera.matCoord[3][2] + vDir[2] * dist;
    s_Camera.vTarget[3] = s_Camera.matCoord[3][3] + vDir[3] * dist;
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetCoord
 *
 *  Set the camera's orientation rows (the 3x3 of matCoord) from mat, then
 *  recompute the target so the camera keeps looking the same way.
 * ------------------------------------------------------------------------ */
void gra3dcamSetCoord(float (*mat)[4])
{
    g3dxVu0CopyVector(s_Camera.matCoord[0], mat[0]);
    g3dxVu0CopyVector(s_Camera.matCoord[1], mat[1]);
    g3dxVu0CopyVector(s_Camera.matCoord[2], mat[2]);

    _ResetCameraTarget();
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetTarget (vector)
 *
 *  Aim the camera at vTarget (optionally fixing up the up vector) and cache
 *  the target.
 * ------------------------------------------------------------------------ */
void gra3dcamSetTarget(float *vTarget, int bFixUp)
{
    g3dMatrixSetTarget(s_Camera.matCoord, vTarget, bFixUp);

    sceVu0CopyVector(s_Camera.vTarget, vTarget);
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetTarget (x, y, z)
 * ------------------------------------------------------------------------ */
void gra3dcamSetTarget(float x, float y, float z, int bFixUp)
{
    float v[4];

    v[0] = x;
    v[1] = y;
    v[2] = z;
    v[3] = 1.0f;

    gra3dcamSetTarget(v, bFixUp);
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetRoll / gra3dcamGetRoll / gra3dcamRotationByAxis
 * ------------------------------------------------------------------------ */
void gra3dcamSetRoll(float fRad)
{
    g3dMatrixSetRoll(s_Camera.matCoord, fRad);
    _ResetCameraTarget();
}

float gra3dcamGetRoll(void)
{
    return g3dMatrixGetRoll(s_Camera.matCoord);
}

void gra3dcamRotationByAxis(float *vAxis, float fAngle)
{
    g3dMatrixRotationByAxis(s_Camera.matCoord, vAxis, fAngle);
    _ResetCameraTarget();
}

/* --------------------------------------------------------------------------
 *  gra3dGetNumLightEnable
 *
 *  Count the enabled status slots within the iLightType light range of a
 *  light-data record.  Ambient (type 3) always reports 1.
 * ------------------------------------------------------------------------ */
int gra3dGetNumLightEnable(GRA3DLIGHTDATA *pLD, int iLightType)
{
    int iRet;
    int iStart;
    int iNum;
    int i;

    if (iLightType == G3DLIGHT_AMBIENT)
    {
        return 1;
    }

    switch (iLightType)
    {
        case G3DLIGHT_DIRECTIONAL:
        {
            iStart = GRA3D_START_LIGHT_DIRECTIONAL;
            iNum   = GRA3D_NUM_LIGHT_DIRECTIONAL;
            break;
        }
        case G3DLIGHT_POINT:
        {
            iStart = GRA3D_START_LIGHT_POINT;
            iNum   = GRA3D_NUM_LIGHT_POINT;
            break;
        }
        case G3DLIGHT_SPOT:
        {
            iStart = GRA3D_START_LIGHT_SPOT;
            iNum   = GRA3D_NUM_LIGHT_SPOT;
            break;
        }
        default:
        {
            iStart = INVALID_GRA3DLIGHTID;
            iNum   = 0;
            break;
        }
    }

    iRet = 0;
    for (i = iStart; i < iStart + iNum; i++)
    {
        if (pLD->aStatus[i].bEnable != 0)
        {
            iRet++;
        }
    }

    return iRet;
}

/* --------------------------------------------------------------------------
 *  gra3dGetNumLightInitial
 *
 *  Return the recorded initial light count for a type (ambient -> 1, null
 *  record -> 0).
 * ------------------------------------------------------------------------ */
int gra3dGetNumLightInitial(GRA3DLIGHTDATA *pLD, int iLightType)
{
    if (iLightType == G3DLIGHT_AMBIENT)
    {
        return 1;
    }

    if (pLD == NULL)
    {
        return 0;
    }

    return pLD->aiNumInitial[iLightType];
}

/* --------------------------------------------------------------------------
 *  utilSetGRA3DLIGHTDATADefault
 *
 *  Zero a light-data record and prime its 39 lights with the type-correct
 *  default light.
 * ------------------------------------------------------------------------ */
void utilSetGRA3DLIGHTDATADefault(GRA3DLIGHTDATA *pLD)
{
    G3DLIGHTTYPE iLightType;

    memset(pLD, 0, sizeof(GRA3DLIGHTDATA));

    for (int i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        if (i <= GRA3D_END_LIGHT_DIRECTIONAL)
        {
            iLightType = G3DLIGHT_DIRECTIONAL;
        }
        else if (i <= GRA3D_END_LIGHT_POINT)
        {
            iLightType = G3DLIGHT_POINT;
        }
        else if (i < NUM_GRA3DLIGHTID)
        {
            iLightType = G3DLIGHT_SPOT;
        }
        else
        {
            iLightType = G3DLIGHTTYPE_FORCE_DWORD;
        }

        g3dutilSetLightDefault(&pLD->aLight[i], iLightType);
    }
}

/* --------------------------------------------------------------------------
 *  gra3dSetGsRegisterDefault
 *
 *  Push the five default GS register values (from the rdata table) to the GS.
 * ------------------------------------------------------------------------ */
void gra3dSetGsRegisterDefault(void)
{
    sceGifPackAd aGPA[5];

    aGPA[0] = s_aGsRegisterDefault[0];
    aGPA[1] = s_aGsRegisterDefault[1];
    aGPA[2] = s_aGsRegisterDefault[2];
    aGPA[3] = s_aGsRegisterDefault[3];
    aGPA[4] = s_aGsRegisterDefault[4];

    gra3dSetGsRegisters(aGPA, 5);
}

/* --------------------------------------------------------------------------
 *  _GetCameraDirectionInverse
 *
 *  v = -normalize(camera forward axis).
 * ------------------------------------------------------------------------ */
static void _GetCameraDirectionInverse(float *v)
{
    sceVu0FVECTOR vDir;
    int           c;

    /* v = -normalize(camera forward axis) */
    sceVu0Normalize(vDir, s_Camera.matCoord[2]);
    for (c = 0; c < 4; c++) v[c] = -vDir[c];
}

/* --------------------------------------------------------------------------
 *  _StabilizeCamera
 *
 *  Re-normalise the camera's three orientation rows, force the position row's
 *  w to 1, then re-aim at the cached target (optionally fixing up).
 * ------------------------------------------------------------------------ */
static void _StabilizeCamera(GRA3DCAMERA *pCam, int bFixup)
{
    float (*mat)[4];

    mat = pCam->matCoord;

    sceVu0Normalize(pCam->matCoord[0], pCam->matCoord[0]);
    sceVu0Normalize(pCam->matCoord[1], pCam->matCoord[1]);
    sceVu0Normalize(pCam->matCoord[2], pCam->matCoord[2]);

    pCam->matCoord[3][3] = 1.0f;

    g3dMatrixSetTarget(mat, pCam->vTarget, bFixup);
}

/* --------------------------------------------------------------------------
 *  gra3dCalcWorldScreenMatrix
 *
 *  Build the world->screen matrix for pCam into mat: stabilise the camera,
 *  derive the view matrix in the VU0 scratch matrix, multiply by the
 *  projection chosen by the camera's projection type.
 * ------------------------------------------------------------------------ */
void gra3dCalcWorldScreenMatrix(float (*mat)[4], GRA3DCAMERA *pCam, int bFixup)
{
    float fScrZ;

    _StabilizeCamera(pCam, bFixup);

    fScrZ = tanf(pCam->fFov * 0.5f);

    sceVu0CameraMatrix(SPR_FMATRIX(0x3900), pCam->matCoord[3], pCam->matCoord[2], pCam->matCoord[1]);

    G3DASSERT(pCam->type < NUM_G3DCAMPROJECTIONTYPE, "");

    s_apViewScreenMatrixFunc[pCam->type](SPR_FMATRIX(0x3940), 224.0f / fScrZ,
                                         pCam->fAspectX, pCam->fAspectY,
                                         pCam->fCenterX, pCam->fCenterY,
                                         pCam->fZmin, pCam->fZmax,
                                         pCam->fNearZ, pCam->fFarZ);

    sceVu0MulMatrix(mat, SPR_FMATRIX(0x3940), SPR_FMATRIX(0x3900));
}

/* --------------------------------------------------------------------------
 *  gra3dApplyCamera
 *
 *  Apply pCam (or the module camera when NULL): stabilise it, build the view
 *  and the screen / polygon-clip / object-clip projection matrices, compose
 *  the world->screen and the two world->clip matrices, and push the VIEW and
 *  PROJECTION transforms into the engine.
 * ------------------------------------------------------------------------ */
void gra3dApplyCamera(GRA3DCAMERA *pCam, int bFixup)
{
    float fScrZ;
    float fExtendX;
    float fExtendY;

    if (pCam == NULL)
    {
        pCam = &s_Camera;
    }

    _StabilizeCamera(pCam, bFixup);

    fScrZ = 224.0f / tanf(pCam->fFov * 0.5f);

    sceVu0CameraMatrix(SPR_FMATRIX(0x3900), pCam->matCoord[3], pCam->matCoord[2], pCam->matCoord[1]);

    G3DASSERT(pCam->type < NUM_G3DCAMPROJECTIONTYPE, "");

    /* Widescreen (port).  The clip volume is the frustum's half-extent in
     * screen units, and g3dCalcViewClipMatrix*() divides by it -- so growing it
     * by however much wider the window is than the ROM's 640x448 frame widens
     * the view instead of stretching or pillarboxing it.  fScrZ is deliberately
     * left on the original 224 half-height: that is what fixes the vertical
     * field of view, and scaling it would zoom rather than reveal.  Both
     * factors are 1 on a 4:3 output, where this reduces to the ROM's call. */
    MioPan_RendererGetViewExtend(&fExtendX, &fExtendY);

    s_apViewScreenMatrixFunc[pCam->type](SPR_FMATRIX(0x3940), fScrZ,
                                         pCam->fAspectX, pCam->fAspectY,
                                         pCam->fCenterX, pCam->fCenterY,
                                         pCam->fZmin, pCam->fZmax,
                                         pCam->fNearZ, pCam->fFarZ);

    s_apViewClipMatrixFunc[pCam->type](pCam->matViewClipPolygon,
                                       fScrZ, pCam->fAspectX, pCam->fAspectY,
                                       pCam->fNearZ, pCam->fFarZ,
                                       1920.0f * fExtendX, 1792.0f * fExtendY);

    s_apViewClipMatrixFunc[pCam->type](pCam->matViewClipObject,
                                       fScrZ, pCam->fAspectX, pCam->fAspectY,
                                       pCam->fNearZ, pCam->fFarZ,
                                       320.0f * fExtendX, 224.0f * fExtendY);

    sceVu0MulMatrix(pCam->matWorldScreen,       SPR_FMATRIX(0x3940), SPR_FMATRIX(0x3900));
    sceVu0MulMatrix(pCam->matWorldClipPolygon,  pCam->matViewClipPolygon, SPR_FMATRIX(0x3900));
    sceVu0MulMatrix(pCam->matWorldClipObject,   pCam->matViewClipObject,  SPR_FMATRIX(0x3900));

    gra3dSetTransform(GRA3DTS_VIEW,       SPR_FMATRIX(0x3900));
    gra3dSetTransform(GRA3DTS_PROJECTION, SPR_FMATRIX(0x3940));

    /* PC bridge.  The host renderer takes the engine's own matrices: the view
     * built above and matViewClipObject, which is already a real projection in
     * the same row-vector convention the renderer transforms with.  It used to
     * rebuild both from pCam and had to be kept in step by hand. */
    MioPan_Graph3dApplyCamera(pCam, SPR_FMATRIX(0x3900));
}

/* ==========================================================================
 *  gra3d.c -- PART B   (0x001b1ce8 .. 0x001b3427 inclusive)
 *
 *  Reconstructed function bodies only.  No banner / no #include -- these are
 *  merged into the assembled gra3d.c by the integrator.  Functions are in
 *  address order.  Module statics referenced here (s_Camera, s_Fog,
 *  s_ivFogColor, s_bFogEnable, s_bEnableMonotoneDraw, s_LightManage, s_aLight,
 *  s_pScratchpadLayout, clip_volume, clip_volumev, s_lmDiffuseLight,
 *  s_lmSpecularLight, s_lmDiffuseColor, s_lmSpecularColor,
 *  s_aVu1MaterialCache_Point, s_aVu1MaterialCache_Spot) and the externals
 *  g_matUnit / CVu0Matrix live elsewhere in the file / engine.
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  _gra3dSetCameraForce
 *
 *  Copy the whole GRA3DCAMERA into the module camera.  The ROM (0x1b1ce8) is a
 *  quadword-pair loop over exactly sizeof(GRA3DCAMERA):
 *
 *      addiu v1,a0,0x1e0            ; end = pCamera + 0x1e0
 *      loop: ld/sd 0x20 bytes, a0 += 0x20, bne a0,v1,loop
 *
 *  so it is a straight struct assignment, not a field list.
 *
 *  This used to copy eight fields by hand and stop at fZmin (0x01c), which
 *  silently dropped fZmax (0x020).  g3dCalcViewScreenMatrixPerspective builds
 *  the depth mapping from both -- fAspectZ and fCenterZ are functions of
 *  (fZmax - fZmin) -- so a stale fZmax skews screen Z.  The full copy is
 *  correct and is what the ROM does; note though that it was NOT the cause of
 *  the close-range effect cut-out.  That was g_CameraDefault.fNearZ, which had
 *  been transcribed as 10.0f against the ROM's 0.1f (0x3dcccccc at 0x3b5154).
 *  fAspectZ is linear in fNearZ, so the 100x error pulled the screen-Z overflow
 *  point from 25.6 units out to 2464 -- ordinary room distance -- and
 *  PartsDeformClipCheck(), whose window is 1..0xfffff, clipped almost every
 *  effect as the camera closed in.  s_Camera is seeded from g_CameraDefault
 *  (gra3d.c:327) and no gameplay camera calls gra3dcamSetClip(), so that one
 *  literal set the live near plane for all of ingame.
 * ------------------------------------------------------------------------ */
void _gra3dSetCameraForce(GRA3DCAMERA *pCamera)
{
    s_Camera = *pCamera;
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetFov / gra3dcamGetFov
 * ------------------------------------------------------------------------ */
void gra3dcamSetFov(float fFov)
{
    s_Camera.fFov = fFov;
}

float gra3dcamGetFov(void)
{
    return s_Camera.fFov;
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetAspect
 *
 *  Store the camera aspect; in PAL mode the Y aspect is scaled by the
 *  PAL field-height correction factor.
 * ------------------------------------------------------------------------ */
void gra3dcamSetAspect(float fX, float fY)
{
    s_Camera.fAspectX = fX;
    s_Camera.fAspectY = fY;

    if (GetPALMode() != 0)
    {
        s_Camera.fAspectY = s_Camera.fAspectY * g_fPALAspectScale;
    }
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetDepth / gra3dcamSetClip
 * ------------------------------------------------------------------------ */
void gra3dcamSetDepth(float fMinZ, float fMaxZ)
{
    s_Camera.fZmax = fMaxZ;
    s_Camera.fZmin = fMinZ;
}

void gra3dcamSetClip(float fNearZ, float fFarZ)
{
    s_Camera.fFarZ  = fFarZ;
    s_Camera.fNearZ = fNearZ;
}

/* --------------------------------------------------------------------------
 *  gra3dcamSetType
 * ------------------------------------------------------------------------ */
void gra3dcamSetType(G3DCAMPROJECTIONTYPE type)
{
    /* "type:%d" */
    G3DASSERT(type == PT_PERSPECTIVE || type == PT_ORTHO, "type:%d", type);

    s_Camera.type = type;
}

/* --------------------------------------------------------------------------
 *  _GetClipVolume / _GetClipVolumeV
 *
 *  Both are external in the ROM (gra3d.o 0x001b1e30 / 0x001b1e40); MapSky.c
 *  calls _GetClipVolumeV() to size its own perspective matrix.
 * ------------------------------------------------------------------------ */
float (*_GetClipVolume(void))[4]
{
    return &clip_volume;
}

float (*_GetClipVolumeV(void))[4]
{
    return &clip_volumev;
}

/* --------------------------------------------------------------------------
 *  gra3dSetFog (min, max, near, far)
 * ------------------------------------------------------------------------ */
void gra3dSetFog(float fMin, float fMax, float fNear, float fFar)
{
    s_Fog.fFar  = fFar;
    s_Fog.fMin  = fMin;
    s_Fog.fMax  = fMax;
    s_Fog.fNear = fNear;
}

/* --------------------------------------------------------------------------
 *  gra3dSetFog (G3DFOG)
 * ------------------------------------------------------------------------ */
void gra3dSetFog(G3DFOG *pFog)
{
    s_Fog.fMin  = pFog->fMin;
    s_Fog.fMax  = pFog->fMax;
    s_Fog.fNear = pFog->fNear;
    s_Fog.fFar  = pFog->fFar;
}

/* --------------------------------------------------------------------------
 *  gra3dSetFogColor
 *
 *  Store the fog colour (with monotone collapse if enabled) and push it to the
 *  GS FOGCOL register (BGR packed).
 * ------------------------------------------------------------------------ */
void gra3dSetFogColor(int r, int g, int b)
{
    s_ivFogColor[0] = r;
    s_ivFogColor[1] = g;
    s_ivFogColor[2] = b;
    s_ivFogColor[3] = 0;

    _MakeColorToMonotone(s_ivFogColor);

    gra3dSetGsRegister((long)s_ivFogColor[0]
                       | (long)s_ivFogColor[1] << 8
                       | (long)s_ivFogColor[2] << 0x10,
                       SCE_GS_FOGCOL);
}

/* --------------------------------------------------------------------------
 *  _PushFogToHost
 *
 *  PC bridge.  The VU1 fog block below is the ROM's whole output: VU1 read it
 *  to produce one 8-bit F per vertex and the GS blended with it.  There is no
 *  VU here, so the same four numbers go to the renderer instead and the
 *  fragment shader evaluates the ramp per pixel.
 *
 *  gra3dApplyFog() is the only caller, and that is on purpose: it is the one
 *  point where the ROM commits a fog setting, and both of its callers
 *  (MapFog.c and scene.c) push the ramp and the colour immediately before it.
 *  See gra3dEnableFog() for why the FOGE flag rides along here rather than
 *  pushing from its own setter.
 * ------------------------------------------------------------------------ */
static void _PushFogToHost(void)
{
    MioPan_RendererSetFog(s_bFogEnable,
                          s_Fog.fMin, s_Fog.fMax,
                          g3dCalcFA(&s_Fog), g3dCalcFB(&s_Fog),
                          s_ivFogColor[0], s_ivFogColor[1], s_ivFogColor[2]);
}

/* --------------------------------------------------------------------------
 *  gra3dApplyFog
 *
 *  Recompute the VU1 fog block in the scratchpad image from the module fog.
 * ------------------------------------------------------------------------ */
void gra3dApplyFog(void)
{
    g3dCalcVu1Fog(&s_pScratchpadLayout->Vu1Mem.Direct.Fog, &s_Fog);

    _PushFogToHost();
}

/* --------------------------------------------------------------------------
 *  gra3dGetFogRef / gra3dIsFogEnable
 * ------------------------------------------------------------------------ */
G3DFOG &gra3dGetFogRef(void)
{
    return s_Fog;
}

int gra3dIsFogEnable(void)
{
    return s_bFogEnable;
}

/* --------------------------------------------------------------------------
 *  _ModifyFogParam
 *
 *  Fold the current fog-enable flag into the FOGE bit (bit 0x34) of each of
 *  the four primitive GIFTAGs in the VU1 scratchpad image (the tri-strip and
 *  tri-fan, textured and untextured, PRIM templates).
 * ------------------------------------------------------------------------ */
static void _ModifyFogParam(void)
{
    GRA3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pScratchpadLayout->Vu1Mem.Direct;
    u_long                    lFogBit;

    lFogBit = ((u_long)s_bFogEnable & 1) << 0x34;

    *(u_long *)&rVu1Mem.gtTRISTRIP_NOTEXTURE =
        (*(u_long *)&rVu1Mem.gtTRISTRIP_NOTEXTURE & ~(1ULL << 0x34)) | lFogBit;
    *(u_long *)&rVu1Mem.gtTRISTRIP_TEXTURE =
        (*(u_long *)&rVu1Mem.gtTRISTRIP_TEXTURE & ~(1ULL << 0x34)) | lFogBit;
    *(u_long *)&rVu1Mem.gtTRIFAN_NOTEXTURE =
        (*(u_long *)&rVu1Mem.gtTRIFAN_NOTEXTURE & ~(1ULL << 0x34)) | lFogBit;
    *(u_long *)&rVu1Mem.gtTRIFAN_TEXTURE =
        (*(u_long *)&rVu1Mem.gtTRIFAN_TEXTURE & ~(1ULL << 0x34)) | lFogBit;
}

/* --------------------------------------------------------------------------
 *  gra3dEnableFog
 * ------------------------------------------------------------------------ */
void gra3dEnableFog(int b)
{
    /* Deliberately does NOT push to the host renderer, though it is half of
     * what the host fog state is made of.  _InitFog() enables fog while s_Fog
     * is still fMin == fMax == 0 -- a ramp that says "pure fog colour at every
     * depth" -- and on hardware that is harmless, because the VU1 fog block
     * has not been written yet and nothing 3D has been drawn.  Pushing here
     * would hand the shader that ramp for real and black out anything drawn
     * before the first scene loads.  gra3dApplyFog() carries s_bFogEnable
     * along with the ramp instead, which is the moment the ROM commits both. */
    if (s_bFogEnable != b)
    {
        s_bFogEnable = b;
        _ModifyFogParam();
    }
}

/* --------------------------------------------------------------------------
 *  g3dCalcVu1Fog
 *
 *  Fill the VU1 fog block: min/max copied straight through, FA/FB computed
 *  from the fog ramp.
 * ------------------------------------------------------------------------ */
void g3dCalcVu1Fog(G3DVU1FOG *pVu1Fog, G3DFOG *pFog)
{
    pVu1Fog->fMin = pFog->fMin;
    pVu1Fog->fMax = pFog->fMax;
    pVu1Fog->FA   = g3dCalcFA(pFog);
    pVu1Fog->FB   = g3dCalcFB(pFog);
}

/* --------------------------------------------------------------------------
 *  g3dCalcFA
 *
 *  Linear fog "A" coefficient.
 * ------------------------------------------------------------------------ */
float g3dCalcFA(G3DFOG *pFog)
{
    return (((pFog->fMin - pFog->fMax) * (pFog->fFar + pFog->fNear)) / (pFog->fFar - pFog->fNear)
            + pFog->fMin + pFog->fMax) * 0.5f;
}

/* --------------------------------------------------------------------------
 *  g3dCalcFB
 *
 *  Linear fog "B" coefficient.
 * ------------------------------------------------------------------------ */
float g3dCalcFB(G3DFOG *pFog)
{
    return (pFog->fFar * pFog->fNear * (pFog->fMax - pFog->fMin)) / (pFog->fFar - pFog->fNear);
}

/* --------------------------------------------------------------------------
 *  gra3dIsMonotoneDrawEnable / gra3dMonotoneDrawEnable
 * ------------------------------------------------------------------------ */
int gra3dIsMonotoneDrawEnable(void)
{
    return s_bEnableMonotoneDraw;
}

void gra3dMonotoneDrawEnable(int bEnable)
{
    s_bEnableMonotoneDraw = bEnable;
}

/* --------------------------------------------------------------------------
 *  _ClearMaterialData
 *
 *  Zero a 0x150-byte VU1 material-data / material-cache block.
 * ------------------------------------------------------------------------ */
void _ClearMaterialData(void *pMatData)
{
    memset(pMatData, 0, sizeof(GRA3DVU1MATERIALDATA));
}

/* --------------------------------------------------------------------------
 *  gra3dSetValidLightId
 *
 *  Bind one of the dynamic light slots (point or spot) to a source light from
 *  the static light bank, or disable it when iLightTypeIndex is INVALID.  The
 *  iIndex is the slot within the type's dynamic range; the source for a point
 *  light is taken from the point sub-bank (offset +3) and for a spot light
 *  from the spot sub-bank (offset +0x16).
 * ------------------------------------------------------------------------ */
void gra3dSetValidLightId(int iLightType, unsigned int iIndex, int iLightTypeIndex)
{
    if (iLightType == G3DLIGHT_POINT)
    {
        if (iLightTypeIndex == INVALID_GRA3DLIGHTID)
        {
            g3dLightEnable(iIndex + GRA3D_START_LIGHT_POINT, 0);
            return;
        }

        g3dSetLight(iIndex + GRA3D_START_LIGHT_POINT, &s_aLight[iLightTypeIndex + GRA3D_START_LIGHT_POINT]);
        g3dLightEnable(iIndex + GRA3D_START_LIGHT_POINT, 1);
    }
    else if (iLightType == G3DLIGHT_SPOT)
    {
        if (iLightTypeIndex == INVALID_GRA3DLIGHTID)
        {
            g3dLightEnable(iIndex + G3D_START_LIGHT_SPOT, 0);
            return;
        }

        g3dSetLight(iIndex + G3D_START_LIGHT_SPOT, &s_aLight[iLightTypeIndex + GRA3D_START_LIGHT_SPOT]);
        g3dLightEnable(iIndex + G3D_START_LIGHT_SPOT, 1);
    }
}

/* --------------------------------------------------------------------------
 *  _UpdateLight_Directional
 *
 *  Rebuild the three-column directional light matrices in module memory: the
 *  inverse camera direction (for the half-vector base), then per enabled
 *  directional slot copy the diffuse / specular colours and set the
 *  normalised light direction column (diffuse) and the normalised
 *  (direction + cameraInverse) half-vector column (specular).
 * ------------------------------------------------------------------------ */
static void _UpdateLight_Directional(void)
{
    GRA3DCAMERA  *pCam;
    int           i;
    float         vCamDirInverse[4];
    float         vNormalizedDir[4];
    float         vTemp[4];
    int           c;

    pCam = gra3dGetCamera();

    /* vCamDirInverse = -(camera forward axis) */
    for (c = 0; c < 4; c++) vCamDirInverse[c] = -pCam->matCoord[2][c];

    /* clear all four light matrices */
    gra3dVu0ClearMatrix(s_lmDiffuseLight);
    gra3dVu0ClearMatrix(s_lmSpecularLight);
    gra3dVu0ClearMatrix(s_lmDiffuseColor);
    gra3dVu0ClearMatrix(s_lmSpecularColor);

    for (i = 0; i < GRA3D_NUM_LIGHT_DIRECTIONAL; i++)
    {
        if (s_LightManage.aStatus[i].bEnable != 0)
        {
            G3DLIGHT &rLight = s_aLight[i + GRA3D_START_LIGHT_DIRECTIONAL];
            int       k;

            /* The specular colour is overwritten with the diffuse one first --
             * the same thing _UpdateLight_Point / _UpdateLight_Spot do, except
             * that here the w is left alone.  It is a word-at-a-time copy in
             * the ROM (0x1b22f0, four iterations, ROM line 1599), not the
             * lq/sq an sceVu0CopyVector would give, so it is written out as a
             * loop.  A directional light therefore always specularises in its
             * own diffuse colour, whatever vSpecular arrived holding. */
            for (k = 0; k < 4; k++)
            {
                rLight.vSpecular[k] = rLight.vDiffuse[k];
            }

            g3dxVu0CopyVector(s_lmDiffuseColor[i],  rLight.vDiffuse);
            g3dxVu0CopyVector(s_lmSpecularColor[i], rLight.vSpecular);

            /* diffuse column = normalise(light direction) */
            sceVu0Normalize(vNormalizedDir, rLight.vDirection);
            g3dMatrixSetColumnXYZ(s_lmDiffuseLight, vNormalizedDir, i);

            /* specular column = normalise(light direction + cameraDirInverse) */
            {
                int k;

                /* half-vector = light dir + inverse camera dir, normalised */
                for (k = 0; k < 4; k++)
                    vTemp[k] = vCamDirInverse[k] + vNormalizedDir[k];
                sceVu0Normalize(vTemp, vTemp);
            }
            g3dMatrixSetColumnXYZ(s_lmSpecularLight, vTemp, i);
        }
    }

    s_lmDiffuseColor[0][3] = 1.0f;
}

/* --------------------------------------------------------------------------
 *  _UpdateLight_Point
 *
 *  For each enabled point slot, copy its diffuse colour into the specular
 *  colour slot and clear the w of both (the point kernel reuses the diffuse
 *  colour for specular and carries no per-channel alpha here).
 * ------------------------------------------------------------------------ */
static void _UpdateLight_Point(void)
{
    for (int i = GRA3D_START_LIGHT_POINT; i <= GRA3D_END_LIGHT_POINT; i++)
    {
        if (s_LightManage.aStatus[i].bEnable != 0)
        {
            G3DLIGHT &rLight = s_aLight[i];

            sceVu0CopyVector(rLight.vSpecular, rLight.vDiffuse);
            rLight.vSpecular[3] = 0.0f;
            rLight.vDiffuse[3]  = 0.0f;
        }
    }
}

/* --------------------------------------------------------------------------
 *  _UpdateLight_Spot
 *
 *  For each enabled spot slot, copy its diffuse colour into the specular
 *  colour slot (clearing both w), then re-normalise the spot direction in
 *  place.
 * ------------------------------------------------------------------------ */
static void _UpdateLight_Spot(void)
{
    for (int i = GRA3D_START_LIGHT_SPOT; i <= GRA3D_END_LIGHT_SPOT; i++)
    {
        if (s_LightManage.aStatus[i].bEnable != 0)
        {
            G3DLIGHT &rL = s_aLight[i];

            sceVu0CopyVector(rL.vSpecular, rL.vDiffuse);
            rL.vDiffuse[3]  = 0.0f;
            rL.vSpecular[3] = 0.0f;

            sceVu0Normalize(rL.vDirection, rL.vDirection);
        }
    }
}

/* --------------------------------------------------------------------------
 *  gra3dGetNumEnableLight
 *
 *  Count the enabled status slots within a light type's range (ambient -> 3).
 * ------------------------------------------------------------------------ */
int gra3dGetNumEnableLight(int iLightType)
{
    int iRet;
    int iStart;
    int iNum;
    int i;

    switch (iLightType)
    {
        case G3DLIGHT_DIRECTIONAL:
        {
            iStart = GRA3D_START_LIGHT_DIRECTIONAL;
            iNum   = GRA3D_NUM_LIGHT_DIRECTIONAL;
            break;
        }
        case G3DLIGHT_POINT:
        {
            iStart = GRA3D_START_LIGHT_POINT;
            iNum   = GRA3D_NUM_LIGHT_POINT;
            break;
        }
        case G3DLIGHT_SPOT:
        {
            iStart = GRA3D_START_LIGHT_SPOT;
            iNum   = GRA3D_NUM_LIGHT_SPOT;
            break;
        }
        case G3DLIGHT_AMBIENT:
        {
            iStart = 3;
            iNum   = 0x13;
            break;
        }
        default:
        {
            iStart = INVALID_GRA3DLIGHTID;
            iNum   = 0;
            break;
        }
    }

    iRet = 0;
    for (i = iStart; i < iStart + iNum; i++)
    {
        if (s_LightManage.aStatus[i].bEnable != 0)
        {
            iRet++;
        }
    }

    return iRet;
}

/* --------------------------------------------------------------------------
 *  _SetVu1LightData_Point
 *
 *  Pack the enabled point lights into the VU1 point light-data block (three
 *  per group) and the corresponding material cache.  For each light: store its
 *  position (homogeneous), the diffuse attenuation factor in the spare w of
 *  the position matrix (1 / (maxRange * falloff * sum(diffuse))), and stash the
 *  max range and diffuse / specular colour matrices in the material cache.
 *  The cache's per-group index records the (gcount-based) light id.
 * ------------------------------------------------------------------------ */
static void _SetVu1LightData_Point(GRA3DVU1LIGHTDATA_POINT *pVu1LightDataPoint)
{
    int gnum;
    int gcount;
    int i;
    int iLightId;
    int iColInData;

    _ClearMaterialData(s_aVu1MaterialCache_Point);

    gra3dVu0ClearMatrix(pVu1LightDataPoint->lmPosition);

    gnum   = 0;
    gcount = 0;
    iColInData = 0;

    for (i = 0; i < 3; i++)
    {
        iLightId = gcount + G3D_START_LIGHT_POINT;

        if (g3dIsLightEnable(iLightId) != 0)
        {
            G3DLIGHT &rL = g3dGetLightRef(iLightId);

            g3dxVu0CopyVector(pVu1LightDataPoint->lmPosition[iColInData], rL.vPosition);

            /* The scalar here is afPad0[0] -- the light INTENSITY that
             * gra3dSetLightIntens() stores -- not fFalloff.  The ROM loads
             * 0x68(a3) at 0x1b27f8 for the zero test and reuses it at
             * 0x1b280c; fFalloff is 0x64.  Reading fFalloff instead made an
             * intensity-0 light upload a finite reciprocal and scaled every
             * other one by the wrong factor.  Note fMinRange (0x60) is never
             * loaded anywhere in this function -- see the note in
             * _Vu0SetupPositionalLights about what that means for the
             * realtime falloff. */
            if (rL.afPad0[0] == 0.0f)
            {
                pVu1LightDataPoint->lmPosition[iColInData][3] = 0.0f;
            }
            else
            {
                float fSum;

                /* fSum = vDiffuse.x + vDiffuse.y + vDiffuse.z (VU0 add-across) */
                fSum = rL.vDiffuse[0] + rL.vDiffuse[1] + rL.vDiffuse[2];

                pVu1LightDataPoint->lmPosition[iColInData][3] =
                    1.0f / (rL.fMaxRange * rL.afPad0[0] * fSum);
            }

            s_aVu1MaterialCache_Point[gnum].Data.vPower[iColInData] = rL.fMaxRange;
            g3dxVu0CopyVector(s_aVu1MaterialCache_Point[gnum].Data.lmDiffuse[iColInData],  rL.vDiffuse);
            g3dxVu0CopyVector(s_aVu1MaterialCache_Point[gnum].Data.lmSpecular[iColInData], rL.vSpecular);

            s_aVu1MaterialCache_Point[gnum].Index.aiIndex[iColInData] = gcount;

            iColInData++;
            if (iColInData > 2)
            {
                iColInData = 0;
                gnum++;

                /* The earlier pass had a
                 * gra3dVu0ClearMatrix(pVu1LightDataPoint->lmPosition) here.
                 * It is not in the ROM and it was catastrophic: with exactly
                 * three enabled point lights the third fill pushes
                 * iColInData to 3, so the clear ran and wiped the positions it
                 * had just written -- while the material cache above kept its
                 * vPower.  Every character lit by three point lights then had
                 * lights at the origin, which in a cutscene (ambient 0) is a
                 * black character.
                 *
                 * The ROM's line 1741 is only the index/pointer resets for the
                 * next group: 0x001b28b8-0x001b28d8 is `li s8,0x70`,
                 * `move s7,zero`, `addiu s3,s2,0xc`, `move s1,s2`,
                 * `move s5,zero`, `move s6,zero` -- no sqc2 anywhere in it.
                 * The only clear is the pre-loop one at 0x001b2790. */
                if (gnum > 0)
                {
                    gcount = gra3dGetNumEnableLight(G3DLIGHT_POINT) + 100;
                }
            }
        }

        gcount++;
    }
}

/* --------------------------------------------------------------------------
 *  _SetVu1LightData_Spot
 *
 *  Pack the enabled spot lights into the VU1 spot light-data block (three per
 *  group) and the spot material cache.  Per light: store position, the cone
 *  falloff (vIntens = cos^2(cone half-angle), vIntensB = 1/sin^2), the spot
 *  direction column, the camera-relative half-vector column (transformed by
 *  wlmtx when supplied), and the max-range / diffuse / specular colours in the
 *  material cache.
 * ------------------------------------------------------------------------ */
static void _SetVu1LightData_Spot(GRA3DVU1LIGHTDATA_SPOT *pVu1LightDataSpot, float (*wlmtx)[4])
{
    int   gnum;
    int   gcount;
    float dtmp[4];
    float stmp[4];
    float vCamDirInv[4];
    int   i;
    int   iColInData;
    float vNorm[4];

    _GetCameraDirectionInverse(vCamDirInv);

    _ClearMaterialData(s_aVu1MaterialCache_Spot);

    gra3dVu0ClearMatrix(pVu1LightDataSpot->lmPosition);
    g3dxVu0CopyVector(pVu1LightDataSpot->vIntens,  g_v0000);
    g3dxVu0CopyVector(pVu1LightDataSpot->vIntensB, g_v0000);
    gra3dVu0ClearMatrix(pVu1LightDataSpot->lmDirection);
    gra3dVu0ClearMatrix(pVu1LightDataSpot->lmSpecular);

    gnum       = 0;
    gcount     = 0;
    iColInData = 0;

    for (i = 0; i < 3; i = i + 1)
    {
        int iLightId = gcount + G3D_START_LIGHT_SPOT;

        if (g3dIsLightEnable(iLightId) != 0)
        {
            G3DLIGHT &rL = g3dGetLightRef(iLightId);

            g3dxVu0CopyVector(pVu1LightDataSpot->lmPosition[iColInData], rL.vPosition);

            /* afPad0[0], not fFalloff -- the same mistake _SetVu1LightData_Point
             * carries a note about.  The ROM loads 0x68(s1) at 0x1b2a28 and
             * again at 0x1b2a3c (lines 1786/1787); fFalloff is 0x64.  It
             * matters: every writer in the game sets fFalloff to exactly 1.0f,
             * so 1/(1-f) divides by zero and CalcIntens' cone factor
             * max(cos^2 a - vIntens, 0) * vIntensB collapses to 0 for every
             * spot light.
             *
             * afPad0[0] is cos^2(cone half-angle) -- gra3dSetLightIntens() is
             * always called with fCos*fCos (scene.c, fod.c) and scn_test.c
             * reads the angle back with acos(sqrt(afPad0[0])).  So the VU1's
             * ramp is (cos^2 alpha - cos^2 theta) / sin^2 theta: 1 on the beam
             * axis, 0 at the cone edge.  See vu1/LIGHTING.md. */
            pVu1LightDataSpot->vIntens[iColInData]  = rL.afPad0[0];
            pVu1LightDataSpot->vIntensB[iColInData] = 1.0f / (1.0f - rL.afPad0[0]);

            g3dxVu0CopyVector(pVu1LightDataSpot->lmDirection[iColInData], rL.vDirection);

            if (wlmtx == NULL)
            {
                g3dxVu0CopyVector(dtmp, rL.vDirection);
                g3dxVu0CopyVector(stmp, vCamDirInv);
            }
            else
            {
                CVu0Matrix::LoadMatrix(wlmtx);
                CVu0Matrix::ApplyWithoutTrans(dtmp, rL.vDirection);
                CVu0Matrix::ApplyWithoutTrans(stmp, vCamDirInv);
            }

            /* Half-vector column = normalise(dtmp - stmp) -- except the ROM
             * normalises dtmp, so the subtraction's result is discarded.
             * Dead either way: SPOTLIGHTSPC (VU 39..41) is never loaded by any
             * of the four microprograms, which take their spot specular from
             * the clamped diffuse coefficient raised to the eighth instead.
             * Kept as found. */
            sceVu0SubVector(stmp, dtmp, stmp);
            sceVu0Normalize(vNorm, dtmp);
            g3dMatrixSetColumnXYZ(pVu1LightDataSpot->lmSpecular, vNorm, iColInData);

            s_aVu1MaterialCache_Spot[gnum].Data.vPower[iColInData] = rL.fMaxRange;
            g3dxVu0CopyVector(s_aVu1MaterialCache_Spot[gnum].Data.lmDiffuse[iColInData],  rL.vDiffuse);
            g3dxVu0CopyVector(s_aVu1MaterialCache_Spot[gnum].Data.lmSpecular[iColInData], rL.vSpecular);

            s_aVu1MaterialCache_Spot[gnum].Index.aiIndex[iColInData] = gcount;

            iColInData = iColInData + 1;
            if (iColInData > 2)
            {
                iColInData = 0;
                gnum       = gnum + 1;

                /* Same invented clears as _SetVu1LightData_Point had, and
                 * removed for the same reason: every sqc2 vf0 in the ROM's
                 * body is at 0x001b297c-0x001b29d0, before the loop (the
                 * g3dxVu0.h 733 inline).  After `slti v0,s7,0x3` at line 1825
                 * the only call is gra3dGetNumEnableLight at 1831. */
                if (gnum > 0)
                {
                    gcount = gra3dGetNumEnableLight(G3DLIGHT_SPOT) + 100;
                }
            }
        }

        gcount = gcount + 1;
    }
}

/* --------------------------------------------------------------------------
 *  _SetVu1LightData_Directional
 *
 *  Copy the module directional diffuse / specular light matrices into the VU1
 *  directional light-data block.  When mat is supplied the matrices are first
 *  transformed (rotation-only) by it; otherwise they are copied verbatim.
 * ------------------------------------------------------------------------ */
static void _SetVu1LightData_Directional(GRA3DVU1LIGHTDATA_DIRECTIONAL *pVu1LightDataDirectional, float (*mat)[4])
{
    if (mat != NULL)
    {
        gra3dVu0ApplyMatrixToLMatrix(pVu1LightDataDirectional->lmDiffuse,  s_lmDiffuseLight,  mat);
        gra3dVu0ApplyMatrixToLMatrix(pVu1LightDataDirectional->lmSpecular, s_lmSpecularLight, mat);
    }
    else
    {
        gra3dVu0CopyLMatrix(pVu1LightDataDirectional->lmDiffuse,  s_lmDiffuseLight);
        gra3dVu0CopyLMatrix(pVu1LightDataDirectional->lmSpecular, s_lmSpecularLight);
    }
}

/* --------------------------------------------------------------------------
 *  g3dSetVu1LightData
 *
 *  Build the per-coordinate VU1 light-data block.  When cp1 is NULL the local
 *  light is built relative to cp0's local->world matrix: the transposed matrix
 *  is computed (matWork), its rotation columns are normalised and re-transposed
 *  to give the lighting rotation matrices (matDirectional / matSpot), and the
 *  scratchpad's matLocalWorld (normalised) and matLocalWorldNoNormalized
 *  (cp0's raw) images are refreshed.  When cp1 is non-NULL (shadow/global
 *  pass) the identity matrix is used.  Then each light-type builder is invoked.
 * ------------------------------------------------------------------------ */
void g3dSetVu1LightData(GRA3DVU1LIGHTDATA *pVu1LightData, SGDCOORDINATE *cp0, SGDCOORDINATE *cp1)
{
    GRA3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pScratchpadLayout->Vu1Mem.Direct;
    float                     matWork[4][4];
    float                     matSpot[4][4];
    float                     matDirectional[4][4];

    G3DASSERT(!((uintptr_t)cp0 & 0xf), "");
    G3DASSERT(!((uintptr_t)cp1 & 0xf), "");

    if (cp1 == NULL)
    {
        G3DASSERT(cp0, "cp0");
        G3DASSERT(s_pScratchpadLayout, "s_pScratchpadLayout");

        /* matWork = transpose(cp0->matLocalWorld) */
        sceVu0TransposeMatrix(matWork, cp0->matLocalWorld);

        /* matDirectional = transpose of the per-row-normalised rotation */
        {
            int i;

            for (i = 0; i < 3; i = i + 1)
            {
                sceVu0Normalize(matSpot[i], matWork[i]);
            }
            sceVu0CopyVector(matSpot[3], g_v0000);

            sceVu0TransposeMatrix(matDirectional, matSpot);
        }

        /* refresh the scratchpad world matrices (raw + normalised) */
        sceVu0CopyMatrix(rVu1Mem.matLocalWorldNoNormalized, cp0->matLocalWorld);
        sceVu0CopyMatrix(rVu1Mem.matLocalWorld,             matDirectional);

        _SetVu1LightData_Directional(&pVu1LightData->dir, matDirectional);
        _SetVu1LightData_Point(&pVu1LightData->point);
        _SetVu1LightData_Spot(&pVu1LightData->spot, matSpot);
    }
    else
    {
        G3DASSERT(s_pScratchpadLayout, "s_pScratchpadLayout");

        sceVu0CopyMatrix(rVu1Mem.matLocalWorldNoNormalized, g_matUnit);
        sceVu0CopyMatrix(rVu1Mem.matLocalWorld,             g_matUnit);

        _SetVu1LightData_Directional(&pVu1LightData->dir, NULL);
        _SetVu1LightData_Point(&pVu1LightData->point);
        _SetVu1LightData_Spot(&pVu1LightData->spot, NULL);
    }

    /* PORT ADDITION -- see s_Vu1LightImage.  The packet is about to be handed
     * to a VIF1 unpack the host does not run; keep the image so
     * gra3dSnapshotVu1Lighting() can hand it to the vertex shader. */
    s_Vu1LightImage = *pVu1LightData;
}

/* --------------------------------------------------------------------------
 *  gra3dSnapshotVu1Lighting                                  [PORT ADDITION]
 *
 *  De-interleave the current VU1 light and material images into one float4 per
 *  light for the GPU vertex path.  Everything here is state the ROM has
 *  already uploaded to VU1 memory: the light half from g3dSetVu1LightData()
 *  (mirrored into s_Vu1LightImage) and the material half from the three
 *  gra3dCalcVu1MaterialData*() packets, which the SGD walker refreshes at
 *  every material block.
 *
 *  The one substitution is the directional direction pair: the packet's copy
 *  has been rotated into the drawn object's local frame, so the world-space
 *  originals are taken instead and the shader dots them against the world
 *  normal.  Same product, one less basis to carry.
 *
 *  Spot and point need no per-light enable mask.  A disabled slot arrives with
 *  a cleared position and, through _ClearMaterialData(), zero colours and a
 *  zero vPower -- so its coefficient and its colour are both zero and the lane
 *  contributes nothing, exactly as it does on the VU.  Only the two light-TYPE
 *  enables matter, because the microcode skips the whole kernel for a disabled
 *  type and VU1 memory then keeps whatever was there before.
 * ------------------------------------------------------------------------ */
void gra3dSnapshotVu1Lighting(GRA3DVU1LIGHTSNAPSHOT *out)
{
    int i;

    if (out == NULL)
    {
        return;
    }

    memset(out, 0, sizeof(*out));

    out->aiConfig[0] = ((gra3dIsLightTypeEnable(G3DLIGHT_SPOT)  != 0) ? 1 : 0)
                     | ((gra3dIsLightTypeEnable(G3DLIGHT_POINT) != 0) ? 2 : 0);
    out->aiConfig[1] = 1;

    /* GLOBALAMBIENT.  .w is the 255.0 gra3dCalcVu1MaterialDataDirectional()
     * stores and the microcode's closing MINIw clamps against. */
    g3dxVu0CopyVector(out->vAmbient, s_Vu1MaterialDirImage.vAmbient);

    for (i = 0; i < 3; i++)
    {
        out->vSpotBTimes[i]  = s_Vu1MaterialSpotImage.vPower[i];
        out->vPointBTimes[i] = s_Vu1MaterialPointImage.vPower[i];
        out->vSpotIntens[i]  = s_Vu1LightImage.spot.vIntens[i];
        out->vSpotIntensB[i] = s_Vu1LightImage.spot.vIntensB[i];

        /* s_lmDiffuseLight / s_lmSpecularLight are COLUMN per light (they are
         * filled with g3dMatrixSetColumnXYZ), which is what lets the VU
         * broadcast the normal's components across the three lanes.  The
         * shader wants one vector per light, so transpose here. */
        out->avDirLightDif[i][0] = s_lmDiffuseLight[0][i];
        out->avDirLightDif[i][1] = s_lmDiffuseLight[1][i];
        out->avDirLightDif[i][2] = s_lmDiffuseLight[2][i];
        out->avDirLightSpc[i][0] = s_lmSpecularLight[0][i];
        out->avDirLightSpc[i][1] = s_lmSpecularLight[1][i];
        out->avDirLightSpc[i][2] = s_lmSpecularLight[2][i];

        /* Everything else is already row per light. */
        g3dxVu0CopyVector(out->avDirColDif[i],
                          s_Vu1MaterialDirImage.lmDiffuse[i]);
        g3dxVu0CopyVector(out->avDirColSpc[i],
                          s_Vu1MaterialDirImage.lmSpecular[i]);

        g3dxVu0CopyVector(out->avSpotPos[i],    s_Vu1LightImage.spot.lmPosition[i]);
        g3dxVu0CopyVector(out->avSpotDir[i],    s_Vu1LightImage.spot.lmDirection[i]);
        g3dxVu0CopyVector(out->avSpotColDif[i], s_Vu1MaterialSpotImage.lmDiffuse[i]);
        g3dxVu0CopyVector(out->avSpotColSpc[i], s_Vu1MaterialSpotImage.lmSpecular[i]);

        g3dxVu0CopyVector(out->avPointPos[i],    s_Vu1LightImage.point.lmPosition[i]);
        g3dxVu0CopyVector(out->avPointColDif[i], s_Vu1MaterialPointImage.lmDiffuse[i]);
        g3dxVu0CopyVector(out->avPointColSpc[i], s_Vu1MaterialPointImage.lmSpecular[i]);
    }

    /* PORT DEVIATION.  DIRCOLDIF[0].w is the vertex alpha, and on hardware it
     * comes from whatever material packet was last uploaded -- so a cached
     * material block legitimately reuses the previous upload.  The port does
     * not replay uploads: it draws each mesh as the walker reaches it, and
     * SetMaterialDataVU()'s cache early-out returns before
     * gra3dCalcVu1MaterialDataDirectional(), while SgPreRenderPrim() (the
     * prelight walker) calls gra3dSetMaterial() without rebuilding the packet
     * at all.  Either one leaves s_MaterialPacketDirectional describing a
     * different material than the one this mesh is about to be drawn with.
     *
     * Measured: the packet read 0 on whole runs of character meshes whose live
     * material was a correct 128, which erased those body parts once the alpha
     * actually reached the renderer.  The live material is the one the ROM
     * intends for this mesh, so take the alpha from there.
     *
     * NOTE: the same staleness applies to the rgb above, which would show as
     * mis-lit rather than missing geometry.  Fixing the packet's own tracking
     * is the real repair; this covers the term that deletes geometry. */
    out->avDirColDif[0][3] = g3dGetMaterialRef().vDiffuse[3];
}

/* --------------------------------------------------------------------------
 *  gra3dVu1TransGTEOP
 *
 *  Stamp the spot / point light-type-enable flags into the EOP GIFTAG block of
 *  the VU1 image and unpack it to VU1 memory address GRA3DVU1MEM_GTEOP.
 * ------------------------------------------------------------------------ */
void gra3dVu1TransGTEOP(void)
{
    GRA3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pScratchpadLayout->Vu1Mem.Direct;

    /* The EOP GIFTAG block carries the spot / point light-type-enable flags in
     * its two spare words (byte offsets 4 and 8 of gtEOP); the micro-program
     * reads them to skip the spot/point lighting stages. */
    ((unsigned int *)&rVu1Mem.gtEOP)[1] = (gra3dIsLightTypeEnable(G3DLIGHT_SPOT)  != 0);
    ((unsigned int *)&rVu1Mem.gtEOP)[2] = (gra3dIsLightTypeEnable(G3DLIGHT_POINT) != 0);

    g3dVif1Unpack(GRA3DVU1MEM_GTEOP, &rVu1Mem.gtEOP, 1);
}

/* --------------------------------------------------------------------------
 *  SetVU1Header
 *
 *  Build the VIF1 UNPACK code for the scratchpad VU1 image header and copy the
 *  header packet down the DMA chain.
 * ------------------------------------------------------------------------ */
void SetVU1Header(void)
{
    gra3dSetVif1Code_Unpack(s_pScratchpadLayout->qwVif1Code0, 0, 0x19, 0x6c);

    g3dDmaCopyPacket(s_pScratchpadLayout, 0x1a);
}

/* --------------------------------------------------------------------------
 *  gra3dIsLightTypeEnable
 *
 *  Return non-zero when any of a light type's three slots is enabled.
 * ------------------------------------------------------------------------ */
int gra3dIsLightTypeEnable(G3DLIGHTTYPE type)
{
    int iStart;

    /* "type is invalid" */
    G3DRETURNVAL(NUM_G3DLIGHTTYPE > type, 0, "type is invalid");

    /* "type:%d" */
    G3DASSERT(g3dIsValidLightType(type), "type:%d", type);

    switch (type)
    {
        case G3DLIGHT_DIRECTIONAL:
        {
            iStart = 0;
            break;
        }
        case G3DLIGHT_POINT:
        {
            iStart = 3;
            break;
        }
        case G3DLIGHT_SPOT:
        {
            iStart = 6;
            break;
        }
        default:
        {
            iStart = INVALID_GRA3DLIGHTID;
            break;
        }
    }

    if (g3dIsLightEnable(iStart) != 0)
    {
        return 1;
    }
    if (g3dIsLightEnable(iStart + 1) != 0)
    {
        return 1;
    }
    if (g3dIsLightEnable(iStart + 2) != 0)
    {
        return 1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  gra3dEnableLightType  (stub in the prototype)
 * ------------------------------------------------------------------------ */
void gra3dEnableLightType(G3DLIGHTTYPE type, int bEnable)
{
}

/* --------------------------------------------------------------------------
 *  gra3dSetAmbient
 * ------------------------------------------------------------------------ */
void gra3dSetAmbient(float *vAmbient)
{
    g3dSetAmbient(vAmbient);
}

/* ==========================================================================
 *  gra3d.c -- PART C   (0x001b3410 .. end of file)
 *
 *  Reconstructed function bodies only.  No banner / no #include -- these are
 *  merged into the assembled gra3d.c by the integrator.  Functions are in
 *  address order.  Module statics referenced here (s_aLight, s_LightManage,
 *  s_lmDiffuseColor, s_lmSpecularColor, s_uiMaterialPrimType, s_aMaterialCache,
 *  s_aVu1MaterialCache_Point, s_aVu1MaterialCache_Spot, s_MaterialPacketDirectional,
 *  s_MaterialPacketPoint, s_MaterialPacketSpot, s_pScratchpadLayout,
 *  s_gra3dScratchpadLayoutDefault, s_bUseScratchpad, s_aSRPair, s_aGRPair,
 *  lRet, s_aVif1CmdData) and the file-scope constants g_f1_255 / s_if_1_255 /
 *  g_xv0000 / g_v0000 / g_Vu0Matrix live elsewhere in the file / engine.
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  gra3dGetAmbientRef
 * ------------------------------------------------------------------------ */
float (&gra3dGetAmbientRef(void))[4]
{
    return g3dGetAmbientRef();
}

/* --------------------------------------------------------------------------
 *  gra3dSetLight
 *
 *  Validate a light against its slot id (type must match, ranges/angles sane,
 *  point lights carry a zero direction, falloff == 1) and copy it into the
 *  module light bank.
 * ------------------------------------------------------------------------ */
void gra3dSetLight(int iLightId, G3DLIGHT *pLight)
{
    /* "pLight" */
    G3DASSERT(pLight, "");
    G3DASSERT(NUM_GRA3DLIGHTID > iLightId, "iLightId(%d) is invalid\n", iLightId);

    G3DASSERT(gra3dGetLightType(iLightId) == pLight->Type, "%d != %d LightId[%d]", gra3dGetLightType(iLightId), pLight->Type, iLightId);
    G3DWARNING(pLight->Type == G3DLIGHT_DIRECTIONAL || pLight->fMaxRange >= pLight->fMinRange, "");
    G3DWARNING(pLight->Type != G3DLIGHT_SPOT || pLight->fAngleOutside >= pLight->fAngleInside, "");
    G3DWARNING(pLight->Type != G3DLIGHT_POINT || EqualMemory128(pLight->vDirection, g_v0000, 1), "");
    G3DWARNING(pLight->fFalloff == 1.0f, "");

    g3dutilCopyLight(&s_aLight[iLightId], pLight);
}

/* --------------------------------------------------------------------------
 *  gra3dGetLightRef
 * ------------------------------------------------------------------------ */
G3DLIGHT &gra3dGetLightRef(int iLightId)
{
    G3DASSERT(NUM_GRA3DLIGHTID > iLightId, "iLightId(%d) is invalid\n", iLightId);
    G3DASSERT(gra3dGetLightType( iLightId ) == s_aLight[iLightId].Type, "gra3dGetLightType( iLightId ) : %d, pLight->Type", gra3dGetLightType( iLightId ), s_aLight[iLightId].Type);

    return s_aLight[iLightId];
}

/* --------------------------------------------------------------------------
 *  gra3dLightEnable
 * ------------------------------------------------------------------------ */
void gra3dLightEnable(int iLightId, int bEnable)
{
    G3DWARNING(NUM_GRA3DLIGHTID > iLightId, "iLightId(%d) is invalid");

    s_LightManage.aStatus[iLightId].bEnable = bEnable;
}

/* --------------------------------------------------------------------------
 *  gra3dIsLightEnable
 * ------------------------------------------------------------------------ */
int gra3dIsLightEnable(int iLightId)
{
    G3DWARNING(NUM_GRA3DLIGHTID > iLightId, "iLightId(%d) is invalid");

    return s_LightManage.aStatus[iLightId].bEnable;
}

/* --------------------------------------------------------------------------
 *  gra3dSetLightStatus
 * ------------------------------------------------------------------------ */
void gra3dSetLightStatus(int iLightId, GRA3DLIGHTSTATUS *pS)
{
    G3DWARNING(NUM_GRA3DLIGHTID > iLightId, "iLightId(%d) is invalid");

    s_LightManage.aStatus[iLightId] = *pS;
}

/* --------------------------------------------------------------------------
 *  gra3dGetLightStatusRef
 * ------------------------------------------------------------------------ */
GRA3DLIGHTSTATUS &gra3dGetLightStatusRef(int iLightId)
{
    G3DWARNING(NUM_GRA3DLIGHTID > iLightId, "iLightId(%d) is invalid");

    return s_LightManage.aStatus[iLightId];
}

/* --------------------------------------------------------------------------
 *  gra3dApplyLight
 *
 *  Rebuild the module directional / point / spot light matrices from the
 *  current light bank.
 * ------------------------------------------------------------------------ */
void gra3dApplyLight(void)
{
    _UpdateLight_Directional();
    _UpdateLight_Point();
    _UpdateLight_Spot();
}

/* --------------------------------------------------------------------------
 *  gra3dDrawPrimitive  (stub in the prototype)
 * ------------------------------------------------------------------------ */
void gra3dDrawPrimitive(void *pData, int iSize)
{
}

/* --------------------------------------------------------------------------
 *  g3dGetVu1MaterialCache
 *
 *  Return the point / spot VU1 material cache entry for a light index.
 * ------------------------------------------------------------------------ */
void *g3dGetVu1MaterialCache(G3DLIGHTTYPE type, int iIndex)
{
    if (type == G3DLIGHT_POINT)
    {
        return &s_aVu1MaterialCache_Point[iIndex];
    }
    if (type == G3DLIGHT_SPOT)
    {
        return &s_aVu1MaterialCache_Spot[iIndex];
    }

    /* "0" */
    G3DASSERT(0, "");
    return nullptr;
}

/* --------------------------------------------------------------------------
 *  SetMaxColor255
 *
 *  Normalise an RGB colour so its largest channel maps to 255: divide each
 *  channel by max(col) / 255 (clamped away from 0), store into dcol and return
 *  the divisor.
 * ------------------------------------------------------------------------ */
static float SetMaxColor255(float *dcol, float *col)
{
    float fDiv = *std::max_element(col, col + 3) * s_if_1_255.f;

    if (fDiv == 0.0f)
    {
        fDiv = 1.0f;
    }

    /* dcol = col * (1 / fDiv) */
    sceVu0ScaleVector(dcol, col, 1.0f / fDiv);

    return fDiv;
}

/* --------------------------------------------------------------------------
 *  gra3dCalcVu1MaterialDataDirectional
 *
 *  Build the directional VU1 material data packet from the current material,
 *  the global ambient and the module directional light colour matrices.  The
 *  ambient term is (global ambient * material ambient + emissive) scaled to the
 *  GS colour range (192 for an SGD primitive, 255 otherwise); per enabled
 *  directional slot the diffuse / specular colours are folded with the
 *  material and stored, the rest are zeroed.  The finished packet data is then
 *  copied out to the caller's block.
 * ------------------------------------------------------------------------ */
void gra3dCalcVu1MaterialDataDirectional(GRA3DVU1MATERIALDATA_DIRECTIONAL *_pDirectionalData)
{
    G3DMATERIAL  *rMat;
    float       (*rvAmbient)[4];
    float         vWork[4];
    float         fAmbientScale;
    int           iLightId;

    fAmbientScale = 128.0f;
    if (s_uiMaterialPrimType != 1)
    {
        fAmbientScale = 255.0f;
    }

    rMat      = &g3dGetMaterialRef();
    rvAmbient = &gra3dGetAmbientRef();

    /* vWork = ambient * material.vAmbient + material.vEmissive */
    sceVu0MulVector(vWork, *rvAmbient, rMat->vAmbient);
    sceVu0AddVector(vWork, vWork, rMat->vEmissive);
    _MakeColorToMonotone(vWork);

    /* scale into GS colour range and store the ambient term */
    sceVu0ScaleVector(s_MaterialPacketDirectional.Data.vAmbient, vWork, fAmbientScale);
    s_MaterialPacketDirectional.Data.vAmbient[3] = 255.0f;

    for (iLightId = 0; iLightId < 3; iLightId++)
    {
        if (gra3dIsLightEnable(iLightId) == 0)
        {
            sceVu0CopyVector(s_MaterialPacketDirectional.Data.lmDiffuse[iLightId],  g_v0000);
            sceVu0CopyVector(s_MaterialPacketDirectional.Data.lmSpecular[iLightId], g_v0000);
        }
        else
        {
            /* diffuse colour = light diffuse colour * material diffuse */
            sceVu0MulVector(vWork, s_lmDiffuseColor[iLightId], rMat->vDiffuse);
            _MakeColorToMonotone(vWork);
            sceVu0ScaleVector(s_MaterialPacketDirectional.Data.lmDiffuse[iLightId], vWork, s_lmDiffuseColor[0][3]);

            /* specular colour = light specular colour * material specular */
            sceVu0MulVector(vWork, s_lmSpecularColor[iLightId], rMat->vSpecular);
            _MakeColorToMonotone(vWork);
            sceVu0ScaleVector(s_MaterialPacketDirectional.Data.lmSpecular[iLightId], vWork, s_lmSpecularColor[1][3]);
        }
    }

    s_MaterialPacketDirectional.Data.lmDiffuse[0][3]  = rMat->vDiffuse[3];
    s_MaterialPacketDirectional.Data.lmSpecular[0][3] = rMat->vDiffuse[3];

    /* PORT ADDITION -- mirror what the packet receives; see s_Vu1MaterialDirImage. */
    s_Vu1MaterialDirImage = s_MaterialPacketDirectional.Data;

    *_pDirectionalData = s_MaterialPacketDirectional.Data;
}

/* --------------------------------------------------------------------------
 *  gra3dCalcVu1MaterialDataPoint
 *
 *  Build the point VU1 material data packet.  For each enabled point light
 *  group the per-light diffuse / specular colours from the material cache are
 *  folded with the material colours: diffuse is range-normalised through
 *  SetMaxColor255 (the divisor feeds the power column), specular is scaled by
 *  (sum(material.specular) * powerScale / divisor).  The light index map is
 *  cached into s_aMaterialCache[1].  The finished packet is copied out.
 * ------------------------------------------------------------------------ */
void gra3dCalcVu1MaterialDataPoint(GRA3DVU1MATERIALDATA_POINT *_pPointData)
{
    G3DMATERIAL                 *rMat;
    GRA3DVU1MATERIALCACHE_POINT *pMatCache;
    float                        fSpecularScale;
    float                        fDiffuseScale;
    float                        fSpecularSum;
    float                        vWork[4];
    int                          i;
    int                          j;

    rMat = &g3dGetMaterialRef();

    /* fSpecularSum = material.specular.x + .y + .z (VU0 add-across) */
    fSpecularSum = rMat->vSpecular[0] + rMat->vSpecular[1] + rMat->vSpecular[2];

    if (s_uiMaterialPrimType == 1)
    {
        fSpecularScale = 43.0f;
        fDiffuseScale  = 192.0f;
    }
    else
    {
        fSpecularScale = 86.0f;
        fDiffuseScale  = 255.0f;
    }

    s_aMaterialCache[1].bEnable = gra3dIsLightTypeEnable(G3DLIGHT_POINT);

    for (i = 0; i < gra3dIsLightTypeEnable(G3DLIGHT_POINT); i = i + 1)
    {
        pMatCache = (GRA3DVU1MATERIALCACHE_POINT *)g3dGetVu1MaterialCache(G3DLIGHT_POINT, i);

        for (j = 0; j < 3; j = j + 1)
        {
            float fDiv;

            /* diffuse = cache.diffuse * material.diffuse, range-normalised */
            sceVu0MulVector(vWork, pMatCache->Data.lmDiffuse[j], rMat->vDiffuse);
            _MakeColorToMonotone(vWork);
            sceVu0ScaleVector(vWork, vWork, fDiffuseScale);
            fDiv = SetMaxColor255(s_MaterialPacketPoint.Data.lmDiffuse[j], vWork);

            /* specular = cache.specular * material.specular, power-scaled */
            sceVu0MulVector(vWork, pMatCache->Data.lmSpecular[j], rMat->vSpecular);
            _MakeColorToMonotone(vWork);
            sceVu0ScaleVector(s_MaterialPacketPoint.Data.lmSpecular[j], vWork,
                              (fSpecularSum * fSpecularScale) / fDiv);

            s_aMaterialCache[1].aiIndex[j]        = pMatCache->Index.aiIndex[j];
            s_MaterialPacketPoint.Data.vPower[j]  = pMatCache->Data.vPower[j] * fDiv;
        }
    }

    /* PORT ADDITION -- mirror what the packet receives; see s_Vu1MaterialPointImage. */
    s_Vu1MaterialPointImage = s_MaterialPacketPoint.Data;

    *_pPointData = s_MaterialPacketPoint.Data;
}

/* --------------------------------------------------------------------------
 *  gra3dCalcVu1MaterialDataSpot
 *
 *  As gra3dCalcVu1MaterialDataPoint, for the spot light groups; the index map
 *  is cached into s_aMaterialCache[2].
 * ------------------------------------------------------------------------ */
void gra3dCalcVu1MaterialDataSpot(GRA3DVU1MATERIALDATA_SPOT *_pSpotData)
{
    G3DMATERIAL                *rMat;
    GRA3DVU1MATERIALCACHE_SPOT *pMatCache;
    float                       fSpecularScale;
    float                       fDiffuseScale;
    float                       fSpecularSum;
    float                       vWork[4];
    int                         i;
    int                         j;

    rMat = &g3dGetMaterialRef();

    /* fSpecularSum = material.specular.x + .y + .z (VU0 add-across) */
    fSpecularSum = rMat->vSpecular[0] + rMat->vSpecular[1] + rMat->vSpecular[2];

    if (s_uiMaterialPrimType == 1)
    {
        fSpecularScale = 43.0f;
        fDiffuseScale  = 192.0f;
    }
    else
    {
        fSpecularScale = 86.0f;
        fDiffuseScale  = 255.0f;
    }

    s_aMaterialCache[2].bEnable = gra3dIsLightTypeEnable(G3DLIGHT_SPOT);

    for (i = 0; i < gra3dIsLightTypeEnable(G3DLIGHT_SPOT); i = i + 1)
    {
        pMatCache = (GRA3DVU1MATERIALCACHE_SPOT *)g3dGetVu1MaterialCache(G3DLIGHT_SPOT, i);

        for (j = 0; j < 3; j = j + 1)
        {
            float fDiv;

            /* diffuse = cache.diffuse * material.diffuse, range-normalised */
            sceVu0MulVector(vWork, pMatCache->Data.lmDiffuse[j], rMat->vDiffuse);
            _MakeColorToMonotone(vWork);
            sceVu0ScaleVector(vWork, vWork, fDiffuseScale);
            fDiv = SetMaxColor255(s_MaterialPacketSpot.Data.lmDiffuse[j], vWork);

            /* specular = cache.specular * material.specular, power-scaled */
            sceVu0MulVector(vWork, pMatCache->Data.lmSpecular[j], rMat->vSpecular);
            _MakeColorToMonotone(vWork);
            sceVu0ScaleVector(s_MaterialPacketSpot.Data.lmSpecular[j], vWork,
                              (fSpecularSum * fSpecularScale) / fDiv);

            s_aMaterialCache[2].aiIndex[j]      = pMatCache->Index.aiIndex[j];
            s_MaterialPacketSpot.Data.vPower[j] = pMatCache->Data.vPower[j] * fDiv;
        }
    }

    /* PORT ADDITION -- mirror what the packet receives; see s_Vu1MaterialSpotImage. */
    s_Vu1MaterialSpotImage = s_MaterialPacketSpot.Data;

    *_pSpotData = s_MaterialPacketSpot.Data;
}

/* --------------------------------------------------------------------------
 *  gra3dCalcVu1WideLanes                                     [PORT ADDITION]
 *
 *  The same per-light derivation the three-lane builders above perform, run
 *  over gra3d's WHOLE light bank instead of the three the VU1 had room for.
 *
 *  Why this exists.  _SelectLightByType() (gra3dSGD.c) ranks the bank by power
 *  at the bounding box and copies the best three into g3d's slots, because the
 *  VU1's light image has exactly three lanes per positional type.  That ranking
 *  runs per bounding box, so which three survive changes as objects move, and
 *  lights visibly pop in and out.  The host has no register file to respect, so
 *  the fragment path takes every enabled light and the popping goes away.
 *
 *  It is deliberately written HERE, next to _SetVu1LightData_Point/_Spot and
 *  gra3dCalcVu1MaterialData*(), rather than in the renderer bridge: every line
 *  below is one of theirs, and keeping them adjacent is what stops the two
 *  copies drifting.  Read those three functions before changing anything here.
 *
 *    position / direction  _SetVu1LightData_Spot 1888-1908 (raw, world space)
 *    cone pair             ditto 1905-1906; afPad0[0] is cos^2(half-angle),
 *                          NOT fFalloff -- see the ROM-bug note there
 *    the "material cache"  ditto 1932-1934: it is just the light's own
 *                          vDiffuse / vSpecular / fMaxRange, copied
 *    colours and bTimes    gra3dCalcVu1MaterialDataSpot 2570-2601
 *
 *  Two things this does NOT reproduce, both on purpose:
 *
 *    - g3dIsBBLightingup()'s bounding-box reach test.  That test is itself a
 *      source of popping, and the attenuation law (bTimes/|L|, no cutoff)
 *      already takes a distant light to nothing.  MapLightSelect() has also
 *      already thinned the room to MAP_LIGHT_SELECT_MAX by power at the
 *      listener, so what arrives here is a room-sized set, not the whole bank.
 *    - the exact ranking metric.  If more lights are enabled than there are
 *      lanes the strongest survive, by the same power/distance shape
 *      _CalcValidLightIndexByType() uses but measured from `vRefPos` (the
 *      caller's mesh origin) rather than a bounding-box centre.  With
 *      MAP_LIGHT_SELECT_MAX at 14 against iMaxLanes of 16 this is a safety net
 *      that should never fire.
 *
 *  `type` must be G3DLIGHT_POINT or G3DLIGHT_SPOT.  Returns the lane count.
 * ------------------------------------------------------------------------ */
int gra3dCalcVu1WideLanes(GRA3DVU1LANE *aLanes, int iMaxLanes,
                          G3DLIGHTTYPE type, const float *vRefPos)
{
    G3DMATERIAL *rMat;
    float        fSpecularScale;
    float        fDiffuseScale;
    float        fSpecularSum;
    int          iBase;
    int          iNum;
    int          iCount;
    int          i;

    if (aLanes == NULL || iMaxLanes <= 0)
    {
        return 0;
    }

    if (type == G3DLIGHT_POINT)
    {
        iBase = GRA3D_START_LIGHT_POINT;
        iNum  = GRA3D_NUM_LIGHT_POINT;
    }
    else if (type == G3DLIGHT_SPOT)
    {
        iBase = GRA3D_START_LIGHT_SPOT;
        iNum  = GRA3D_NUM_LIGHT_SPOT;
    }
    else
    {
        G3DASSERT(0, "gra3dCalcVu1WideLanes: positional types only");
        return 0;
    }

    rMat = &g3dGetMaterialRef();

    /* fSpecularSum = material.specular.x + .y + .z (VU0 add-across), and the
     * two scales, exactly as gra3dCalcVu1MaterialDataSpot() picks them. */
    fSpecularSum = rMat->vSpecular[0] + rMat->vSpecular[1] + rMat->vSpecular[2];
    if (s_uiMaterialPrimType == 1)
    {
        fSpecularScale = 43.0f;
        fDiffuseScale  = 192.0f;
    }
    else
    {
        fSpecularScale = 86.0f;
        fDiffuseScale  = 255.0f;
    }

    iCount = 0;
    for (i = 0; i < iNum; i = i + 1)
    {
        int          iId = iBase + i;
        G3DLIGHT    *pL;
        GRA3DVU1LANE *pLane;
        float        vWork[4];
        float        fDiv;
        int          c;

        if (gra3dIsLightEnable(iId) == 0)
        {
            continue;
        }

        pL = &gra3dGetLightRef(iId);

        if (iCount >= iMaxLanes)
        {
            /* Over budget: displace the weakest lane if this light beats it.
             * See the note above -- a safety net, not the normal path. */
            float fThisPower = (sceVu0DiffusePower(pL->vDiffuse) * pL->fMaxRange)
                             / sceVu0DistanceToBB(pL->vPosition, vRefPos);
            int   iWeakest   = 0;
            int   j;

            for (j = 1; j < iMaxLanes; j = j + 1)
            {
                if (aLanes[j].fRank < aLanes[iWeakest].fRank)
                {
                    iWeakest = j;
                }
            }
            if (fThisPower <= aLanes[iWeakest].fRank)
            {
                continue;
            }
            pLane = &aLanes[iWeakest];
            pLane->fRank = fThisPower;
        }
        else
        {
            pLane = &aLanes[iCount];
            pLane->fRank = (sceVu0DiffusePower(pL->vDiffuse) * pL->fMaxRange)
                         / sceVu0DistanceToBB(pL->vPosition, vRefPos);
            iCount = iCount + 1;
        }

        /* -- geometry -------------------------------------------------- */
        g3dxVu0CopyVector(pLane->vPosition, pL->vPosition);
        if (type == G3DLIGHT_SPOT)
        {
            g3dxVu0CopyVector(pLane->vDirection, pL->vDirection);
            pLane->vParams[1] = pL->afPad0[0];
            pLane->vParams[2] = 1.0f / (1.0f - pL->afPad0[0]);
        }
        else
        {
            g3dxVu0CopyVector(pLane->vDirection, g_v0000);
            pLane->vParams[1] = 0.0f;
            pLane->vParams[2] = 0.0f;
        }

        /* -- colours --------------------------------------------------- */
        sceVu0MulVector(vWork, pL->vDiffuse, rMat->vDiffuse);
        _MakeColorToMonotone(vWork);
        sceVu0ScaleVector(vWork, vWork, fDiffuseScale);
        fDiv = SetMaxColor255(pLane->vColDif, vWork);

        sceVu0MulVector(vWork, pL->vSpecular, rMat->vSpecular);
        _MakeColorToMonotone(vWork);
        sceVu0ScaleVector(pLane->vColSpc, vWork,
                          (fSpecularSum * fSpecularScale) / fDiv);

        /* bTimes carries fDiv back, which is what cancels the colour's
         * normalisation inside the kernel -- see the note on Vu1Coefficient. */
        pLane->vParams[0] = pL->fMaxRange * fDiv;
        pLane->vParams[3] = 0.0f;

        /* DIRCOLDIF[0].w is the vertex alpha and comes from the directional
         * block, not from here; leave the w lanes of the colours alone. */
        pLane->vColDif[3] = 0.0f;
        pLane->vColSpc[3] = 0.0f;
    }

    for (i = iCount; i < iMaxLanes; i = i + 1)
    {
        memset(&aLanes[i], 0, sizeof(aLanes[i]));
    }

    return iCount;
}

/* --------------------------------------------------------------------------
 *  gra3dGetMaterialPrimType                                  [PORT ADDITION]
 *
 *  s_uiMaterialPrimType decides the 192/43 against 255/86 colour scales.  The
 *  ROM never needed to read it back; the host's wide-lane builder above is in
 *  this file precisely so it does not have to.  Exposed for the equivalence
 *  harness, which drives the derivation against the three-lane path.
 * ------------------------------------------------------------------------ */
unsigned int gra3dGetMaterialPrimType(void)
{
    return s_uiMaterialPrimType;
}

/* --------------------------------------------------------------------------
 *  gra3dSetMaterial
 *
 *  Translate an SGD material into a G3DMATERIAL and hand it to the core, also
 *  caching the SGD primitive type / per-type material index caches and the
 *  diffuse / specular alpha scales used by the VU1 material builders.
 * ------------------------------------------------------------------------ */
void gra3dSetMaterial(SGDMATERIAL *pMat)
{
    G3DMATERIAL mat;
    float       fDiffuseScale;
    float       fSpecularScale;
    float       fSpecularSum;

    pMat->iCacheStatus    = 1;
    s_uiMaterialPrimType  = pMat->uiPrimType;

    s_aMaterialCache[0] = pMat->aCache[0];
    s_aMaterialCache[1] = pMat->aCache[1];
    s_aMaterialCache[2] = pMat->aCache[2];

    /* fSpecularSum = specular.x + .y + .z (VU0 add-across) */
    fSpecularSum = pMat->vSpecular[0] + pMat->vSpecular[1] + pMat->vSpecular[2];

    if (pMat->uiPrimType == 1)
    {
        fSpecularScale = 43.0f;
        fDiffuseScale  = 192.0f;
    }
    else
    {
        fSpecularScale = 86.0f;
        fDiffuseScale  = 255.0f;
    }
    s_lmDiffuseColor[0][3]  = fDiffuseScale;
    s_lmSpecularColor[1][3] = fSpecularSum * fSpecularScale;
    s_lmSpecularColor[0][3] = 255.0f;

    /* Hand the same three scales to g3d so its host mirror of the VU1 can
     * apply them per term instead of one flat 255.  The ambient one is
     * gra3dCalcVu1MaterialData*'s fAmbientScale, which is 128 -- not 192 --
     * for an SGD primitive. */
    g3dSetVu1ColourScales(pMat->uiPrimType == 1 ? 128.0f : 255.0f,
                          fDiffuseScale,
                          fSpecularSum * fSpecularScale);

    g3dxVu0CopyVector(mat.vDiffuse,  pMat->vDiffuse);
    g3dxVu0CopyVector(mat.vAmbient,  pMat->vAmbient);
    g3dxVu0CopyVector(mat.vSpecular, pMat->vSpecular);
    g3dxVu0CopyVector(mat.vEmissive, pMat->vEmission);
    mat.fPower = 1.0f;

    g3dSetMaterial(&mat);
}

/* --------------------------------------------------------------------------
 *  gra3dSetTransform
 *
 *  The WORLDCLIP / WORLDSCREEN transforms are written straight into the
 *  scratchpad VU1 image; every other transform is forwarded to the core.
 * ------------------------------------------------------------------------ */
int gra3dSetTransform(GRA3DTRANSFORMSTATETYPE state, float (*mat)[4])
{
    if (state == GRA3DTS_WORLDCLIP)
    {
        sceVu0CopyMatrix(s_pScratchpadLayout->Vu1Mem.Direct.matWorldClip, mat);
    }
    else if (state == GRA3DTS_WORLDSCREEN)
    {
        sceVu0CopyMatrix(s_pScratchpadLayout->Vu1Mem.Direct.matWorldScreen, mat);
        return 1;
    }

    return g3dSetTransform((G3DTRANSFORMSTATETYPE)state, mat);
}

/* --------------------------------------------------------------------------
 *  gra3dGetTransformRef
 * ------------------------------------------------------------------------ */
float (&gra3dGetTransformRef(GRA3DTRANSFORMSTATETYPE state))[4][4]
{
    if (state == GRA3DTS_WORLDSCREEN)
    {
        return s_pScratchpadLayout->Vu1Mem.Direct.matWorldScreen;
    }

    return g3dGetTransformRef((G3DTRANSFORMSTATETYPE)state);
}

/* --------------------------------------------------------------------------
 *  _gra3dEnableTextureForce
 *
 *  Toggle the TME (texture map enable, bit 0x34) of the two textured-strip /
 *  textured-fan PRIM GIFTAGs in the scratchpad VU1 image.
 * ------------------------------------------------------------------------ */
void _gra3dEnableTextureForce(int bEnable)
{
    GRA3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pScratchpadLayout->Vu1Mem.Direct;
    long int                  lTRIFANbits;
    long int                  lTRISTRIPbits;

    lTRIFANbits = (long)(*(u_long *)&rVu1Mem.gtTRIFAN_TEXTURE >> 0x2f) & 0x7ef;
    if (bEnable != 0)
    {
        lTRIFANbits = ((long)(*(u_long *)&rVu1Mem.gtTRIFAN_TEXTURE >> 0x2f) & 0x7ff) | 0x10;
    }

    lTRISTRIPbits = (long)(*(u_long *)&rVu1Mem.gtTRISTRIP_TEXTURE >> 0x2f) & 0x7ff;
    if (bEnable == 0)
    {
        lTRISTRIPbits = (long)(*(u_long *)&rVu1Mem.gtTRISTRIP_TEXTURE >> 0x2f) & 0x7ef;
    }

    *(u_long *)&rVu1Mem.gtTRIFAN_TEXTURE =
        (*(u_long *)&rVu1Mem.gtTRIFAN_TEXTURE & 0xfc007fffffffffffULL) | ((u_long)lTRIFANbits << 0x2f);

    if (bEnable != 0)
    {
        lTRISTRIPbits = lTRISTRIPbits | 0x10;
    }

    *(u_long *)&rVu1Mem.gtTRISTRIP_TEXTURE =
        (*(u_long *)&rVu1Mem.gtTRISTRIP_TEXTURE & 0xfc007fffffffffffULL) | ((u_long)lTRISTRIPbits << 0x2f);
}

/* --------------------------------------------------------------------------
 *  gra3dCalcVu1VertexColor                                   [PORT ADDITION]
 *
 *  The VU1's three lighting kernels, on the CPU, for the host paths that light
 *  vertices before submitting them.  Transcribed from vu1/ff2_00.vsm:
 *  directional from PLOOP_TYPE2 0x6c8-0x7d8 (and RotTransPersInner, which is
 *  the same body for the preset draw types), spot from CalcIntens
 *  0x018-0x208, point from CalcPoint 0x220-0x3e0.  vu1/LIGHTING.md has the
 *  derivation.
 *
 *  This replaces gra3dCalcVertexColor() on every realtime path.  That function
 *  is the PRELIGHT model -- a genuinely different calculation, with a
 *  range-band attenuation and an angle-based cone -- and it stays in use for
 *  the bake, which is the one consumer the ROM really does evaluate on the EE.
 *
 *  vVertex / vNormal are local, transformed through GRA3DTS_WORLD exactly as
 *  _Vu0CalcVertexPositionNormal() does (upper 3x3 applied directly, then
 *  normalised -- the ROM's own approximation, not an inverse transpose).
 *  vSrcColor and the result are GS 0..255 units; NULL seeds from black, which
 *  is what a runtime VUVN packet carries.
 * ------------------------------------------------------------------------ */
void gra3dCalcVu1VertexColor(float *vDest, const GRA3DVU1LIGHTSNAPSHOT *snap,
                             const float *vVertex, const float *vNormal,
                             const float *vSrcColor)
{
    const float (*matWorld)[4] = gra3dGetTransformRef(GRA3DTS_WORLD);
    float        vPos[3];
    float        vNrm[3];
    float        acc[3];
    float        fLen2;
    float        q;
    int          c;
    int          i;

    for (c = 0; c < 3; c++)
    {
        vPos[c] = vVertex[0] * matWorld[0][c] + vVertex[1] * matWorld[1][c]
                + vVertex[2] * matWorld[2][c] + matWorld[3][c];
        vNrm[c] = vNormal[0] * matWorld[0][c] + vNormal[1] * matWorld[1][c]
                + vNormal[2] * matWorld[2][c];
    }

    /* Same guard as _Vu0CalcVertexPositionNormal: VU0's vrsqrt on a zero
     * normal yields a huge finite value, the host's 1/sqrtf(0) an infinity. */
    fLen2 = vNrm[0] * vNrm[0] + vNrm[1] * vNrm[1] + vNrm[2] * vNrm[2];
    q     = (fLen2 > 0.0f) ? (1.0f / g3dxVu0Sqrt(fLen2)) : 0.0f;
    for (c = 0; c < 3; c++)
    {
        vNrm[c] = vNrm[c] * q;
    }

    /* The seed.  A preset mesh folds its baked colour in here, which is what
     * CalcParallel does on the P programs (it adds to the vf18 the packet
     * carried); DRAWTYPE2 starts from black because a runtime VUVN packet has
     * no colour of its own.
     *
     * PORT DEVIATION: the monotone collapse is applied to the SOURCE, not to
     * the result.  The VU1 does neither -- on hardware the light colours are
     * already grey (gra3dCalcVu1MaterialData*() runs _MakeColorToMonotone) and
     * the baked colours are grey because entering monotone mode re-bakes the
     * room through gra3dPrelightScene().  Greying the source here reproduces
     * that without depending on the re-bake having run, and it is a no-op
     * whenever monotone draw is off. */
    if (vSrcColor != NULL)
    {
        float vSeed[4];

        g3dxVu0CopyVector(vSeed, vSrcColor);
        _MakeColorToMonotone(vSeed);
        for (c = 0; c < 3; c++)
        {
            acc[c] = vSeed[c];
        }
    }
    else
    {
        acc[0] = acc[1] = acc[2] = 0.0f;
    }

    for (c = 0; c < 3; c++)
    {
        acc[c] += snap->vAmbient[c];
    }

    /* Directional.  Fixed eighth power, and the "half-vector" is the one
     * _UpdateLight_Directional() built once this frame from the camera's
     * forward axis -- not a per-vertex eye direction. */
    for (i = 0; i < 3; i++)
    {
        float nd = snap->avDirLightDif[i][0] * vNrm[0]
                 + snap->avDirLightDif[i][1] * vNrm[1]
                 + snap->avDirLightDif[i][2] * vNrm[2];
        float ns = snap->avDirLightSpc[i][0] * vNrm[0]
                 + snap->avDirLightSpc[i][1] * vNrm[1]
                 + snap->avDirLightSpc[i][2] * vNrm[2];

        if (nd < 0.0f) { nd = 0.0f; }
        if (ns < 0.0f) { ns = 0.0f; }
        ns = ns * ns;
        ns = ns * ns;
        ns = ns * ns;

        for (c = 0; c < 3; c++)
        {
            acc[c] += snap->avDirColDif[i][c] * nd
                    + snap->avDirColSpc[i][c] * ns;
        }
    }

    /* A disabled slot arrives with zero colours and a zero bTimes, so its lane
     * contributes nothing on its own.  Only the light-TYPE enable matters: the
     * microcode skips the whole kernel for a disabled type, and VU1 memory
     * then keeps whatever the previous draw left in it. */
    if (snap->aiConfig[0] & 1)                                  /* CalcIntens */
    {
        for (i = 0; i < 3; i++)
        {
            float L[3];
            float len2;
            float invLen2;
            float coneDot;
            float cone;
            float coef;
            float e;

            for (c = 0; c < 3; c++) { L[c] = snap->avSpotPos[i][c] - vPos[c]; }
            len2 = L[0] * L[0] + L[1] * L[1] + L[2] * L[2];
            if (len2 <= 0.0f) { continue; }
            invLen2 = 1.0f / len2;

            /* PORT DEVIATION -- negated against the microcode, which dots
             * vDirection straight against `light - vertex` and so opens its
             * cone along -vDirection.  The port keeps vDirection as the BEAM
             * engine-wide (authored room spots, g3dCalcSpotlightFalloff(),
             * _IsBBLightingupSpot() and every install site agree on that), so
             * the sense is reconciled here rather than at the install sites.
             * Converting only some of those sites made meshes flip between lit
             * and black as the camera moved.  vu1/LIGHTING.md section 3.3. */
            coneDot = -(snap->avSpotDir[i][0] * L[0]
                      + snap->avSpotDir[i][1] * L[1]
                      + snap->avSpotDir[i][2] * L[2]);
            if (coneDot < 0.0f) { coneDot = 0.0f; }
            cone = coneDot * coneDot * invLen2 - snap->vSpotIntens[i];
            if (cone < 0.0f) { cone = 0.0f; }
            cone = cone * snap->vSpotIntensB[i];

            /* Capped before the cone multiplies it; the cone is not capped. */
            coef = vNrm[0] * L[0] + vNrm[1] * L[1] + vNrm[2] * L[2];
            if (coef < 0.0f) { coef = 0.0f; }
            coef = coef * snap->vSpotBTimes[i] * invLen2;
            if (coef > 1.0f) { coef = 1.0f; }

            e = coef * coef;
            e = e * e;
            e = e * e;                                          /* ^8 */

            for (c = 0; c < 3; c++)
            {
                acc[c] += snap->avSpotColDif[i][c] * (coef * cone)
                        + snap->avSpotColSpc[i][c] * cone * e;
            }
        }
    }

    if (snap->aiConfig[0] & 2)                                   /* CalcPoint */
    {
        for (i = 0; i < 3; i++)
        {
            float L[3];
            float Ldot[3];
            float len2;
            float invLen2;
            float coef;
            float e;

            for (c = 0; c < 3; c++) { L[c] = snap->avPointPos[i][c] - vPos[c]; }
            len2 = L[0] * L[0] + L[1] * L[1] + L[2] * L[2];
            if (len2 <= 0.0f) { continue; }
            invLen2 = 1.0f / len2;

            /* ROM BUG, reproduced.  CalcPoint omits the MR32.z vf15, vf14 that
             * CalcIntens has at 0x0e0, so lane 2's transpose keeps light 1's
             * Lz where light 2's Ly belongs.  vu1/LIGHTING.md section 3.2. */
            Ldot[0] = L[0];
            Ldot[1] = (i == 2) ? (snap->avPointPos[1][1] - vPos[1]) : L[1];
            Ldot[2] = L[2];

            coef = vNrm[0] * Ldot[0] + vNrm[1] * Ldot[1] + vNrm[2] * Ldot[2];
            if (coef < 0.0f) { coef = 0.0f; }
            coef = coef * snap->vPointBTimes[i] * invLen2;
            if (coef > 1.0f) { coef = 1.0f; }

            e = coef * coef;
            e = e * e;                                           /* ^4 */

            for (c = 0; c < 3; c++)
            {
                acc[c] += snap->avPointColDif[i][c] * coef
                        + snap->avPointColSpc[i][c] * e;
            }
        }
    }

    /* MINIw against GLOBALAMBIENT.w (255) -- the microcode's only clamp. */
    for (c = 0; c < 3; c++)
    {
        float v = acc[c];

        if (v < 0.0f)                 { v = 0.0f; }
        if (v > snap->vAmbient[3])    { v = snap->vAmbient[3]; }
        vDest[c] = v;
    }

    /* Alpha is not lit: every kernel writes .xyz only, and the vertex alpha
     * the VU1 emits is DIRCOLDIF[0].w -- which
     * gra3dCalcVu1MaterialDataDirectional() stores as the material's own
     * vDiffuse[3], unconditionally and after the per-light loop, so it
     * survives even when directional light 0 is disabled.  See
     * vu1/LIGHTING.md section 2 ("alpha = DIRCOLDIF0.w").
     *
     * This is the term ManmdlSetAlpha() drives: it forces vDiffuse[3] across
     * every material in an SGD, which is how a ghost fades in and out
     * (enemy.c's tr_rate), how a model cross-fades during a swap, and how the
     * torch effects dim.  Returning 0 here dropped all of it and every
     * character drew fully opaque.  Units are the GS 0..128 the material
     * carries, matching the xyz above. */
    vDest[3] = snap->avDirColDif[0][3];
}

/* --------------------------------------------------------------------------
 *  gra3dCalcVertexColor
 *
 *  CPU-side mirror of the PRELIGHT lighting: convert the 0..255 source colour
 *  to 0..1, light it through the core, scale the result back to 0..255 and
 *  apply the monotone collapse.  The realtime paths use
 *  gra3dCalcVu1VertexColor() instead; see the note there.
 * ------------------------------------------------------------------------ */
void gra3dCalcVertexColor(float *vDest, float *vVertex, float *vNormal, float *vSrcColor)
{
    float vFirst[4];

    /* vFirst = vSrcColor * (1/255) */
    sceVu0ScaleVector(vFirst, vSrcColor, s_if_1_255.f);

    g3dCalcVertexColor(vDest, vVertex, vNormal, vFirst);

    /* vDest = vDest * 255 */
    sceVu0ScaleVector(vDest, vDest, 255.0f);

    _MakeColorToMonotone(vDest);
}

/* --------------------------------------------------------------------------
 *  _SetRegisterSpecified
 *
 *  Look up lAddress in the "special" GS register table; if matched, route the
 *  write through the register-specific setter (the alpha-register setter takes
 *  no value) and report that it was handled.
 * ------------------------------------------------------------------------ */
static int _SetRegisterSpecified(long int lData, long int lAddress)
{
    static SETREGISTERPAIR s_aSRPair[7] =
    {
        { SCE_GS_ALPHA_1,   SetAlphaRegister,   NULL },
        { SCE_GS_TEST_1,    SetTestRegister,    NULL },
        { SCE_GS_ZBUF_1,    SetZbufRegister,    NULL },
        { SCE_GS_TEX1_1,    SetTex1Register,    NULL },
        { 0x01,             SetClampRegister,   NULL },
        { SCE_GS_SCISSOR_1, SetScissorRegister, NULL },
        { SCE_GS_TEXA,      NULL,               SetTexaRegister },
    };

    for (int i = 0; i < 7; i = i + 1)
    {
        SETREGISTERPAIR &rSRPair = s_aSRPair[i];

        if (rSRPair.lAddress == lAddress)
        {
            if (rSRPair.pFuncWithContext != NULL)
            {
                rSRPair.pFuncWithContext(0, lData);
                return 1;
            }

            rSRPair.pFunc(lData);
            return 1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  _GetRegisterSpecified
 *
 *  Inverse of _SetRegisterSpecified: read a "special" GS register's current
 *  value through its getter into rlData; report whether it was handled.
 * ------------------------------------------------------------------------ */
static int _GetRegisterSpecified(long int &rlData, long int lAddress)
{
    static GETREGISTERPAIR s_aGRPair[7] =
    {
        { SCE_GS_ALPHA_1,   GET_ALPHA_REGISTER,   NULL },
        { SCE_GS_TEST_1,    GET_TEST_REGISTER,    NULL },
        { SCE_GS_ZBUF_1,    GET_ZBUF_REGISTER,    NULL },
        { SCE_GS_TEX1_1,    GET_TEX1_REGISTER,    NULL },
        { 0x01,             GET_CLAMP_REGISTER,   NULL },
        { SCE_GS_SCISSOR_1, GET_SCISSOR_REGISTER, NULL },
        { SCE_GS_TEXA,      NULL,                 GET_TEXA_REGISTER },
    };
    int i;

    for (i = 0; i < 7; i = i + 1)
    {
        GETREGISTERPAIR &rGRPair = s_aGRPair[i];

        if (rGRPair.lAddress == lAddress)
        {
            if (rGRPair.pFuncWithContext == NULL)
            {
                rlData = rGRPair.pFunc();
            }
            else
            {
                rlData = rGRPair.pFuncWithContext(0);
            }
            return 1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  gra3dSetGsRegister
 *
 *  Write a GS register: register-specific setters handle their own GS push
 *  (so the core is told not to, iDmaChan == -1); everything else is pushed by
 *  the core (iDmaChan == 1).
 * ------------------------------------------------------------------------ */
int gra3dSetGsRegister(long int lData, long int lAddress)
{
    int iDmaChan = 1;

    if (_SetRegisterSpecified(lData, lAddress) != 0)
    {
        iDmaChan = -1;
    }

    return g3dSetGsRegister(lData, lAddress, iDmaChan);
}

/* --------------------------------------------------------------------------
 *  gra3dSetGsRegisters
 * ------------------------------------------------------------------------ */
int gra3dSetGsRegisters(sceGifPackAd *aGPA, int iNum)
{
    for (int i = 0; i < iNum; i = i + 1)
    {
        gra3dSetGsRegister(aGPA[i].DATA, aGPA[i].ADDR);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  gra3dGetGsRegisterRef
 *
 *  Return a reference to a GS register value: special registers are read into
 *  the module scratch lRet, the rest come straight from the core shadow.
 * ------------------------------------------------------------------------ */
long int &gra3dGetGsRegisterRef(long int lAddress)
{
    static long int lRet;

    if (_GetRegisterSpecified(lRet, lAddress) != 0)
    {
        return lRet;
    }

    return g3dGetGsRegisterRef(lAddress);
}

/* --------------------------------------------------------------------------
 *  _ModifyGra3dScratchpadDefault
 *
 *  Copy the default scratchpad VU1 image into the live scratchpad layout, then
 *  refresh the fog parameter bits.
 * ------------------------------------------------------------------------ */
static void _ModifyGra3dScratchpadDefault(void)
{
    *s_pScratchpadLayout = s_gra3dScratchpadLayoutDefault;

    _ModifyFogParam();
}

/* --------------------------------------------------------------------------
 *  _ModifyScratchpad
 *
 *  Move the three engine scratchpad objects (the core G3DCOREOBJECT, the gra3d
 *  scratchpad VU1 layout and the shadow scratchpad layout) between their static
 *  BSS homes and the EE scratchpad (0x70000000) according to s_bUseScratchpad,
 *  re-initialising each subsystem against the now-current copy.
 * ------------------------------------------------------------------------ */
static void _ModifyScratchpad(void)
{
    G3DCREATIONDATA           g3dCD;
    GRA3DSHADOWCREATIONDATA   gra3dshadowCD;

    /* ----- core object ----- */
    G3DCOREOBJECT *pStaticObj = GetStaticInstance<G3DCOREOBJECT>();
    if (s_bUseScratchpad == 0)
    {
        *pStaticObj = *(G3DCOREOBJECT *)SPR_G3DCOREOBJECT;
        g3dCD.pObj  = pStaticObj;
    }
    else
    {
        *(G3DCOREOBJECT *)SPR_G3DCOREOBJECT = *pStaticObj;
        g3dCD.pObj = (G3DCOREOBJECT *)SPR_G3DCOREOBJECT;
    }
    g3dInitialize(&g3dCD);

    /* ----- gra3d scratchpad VU1 layout ----- */
    GRA3DSCRATCHPADLAYOUT *pStaticSL = GetStaticInstance<GRA3DSCRATCHPADLAYOUT>();
    if (s_bUseScratchpad == 0)
    {
        *pStaticSL          = *(GRA3DSCRATCHPADLAYOUT *)SPR_GRA3DSCRATCHPADLAYOUT;
        s_pScratchpadLayout = pStaticSL;
    }
    else
    {
        *(GRA3DSCRATCHPADLAYOUT *)SPR_GRA3DSCRATCHPADLAYOUT = *pStaticSL;
        s_pScratchpadLayout = (GRA3DSCRATCHPADLAYOUT *)SPR_GRA3DSCRATCHPADLAYOUT;
        _ModifyGra3dScratchpadDefault();
    }

    /* ----- shadow scratchpad VU1 layout ----- */
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pStaticMapShadow = GetStaticInstance<GRA3DSCRATCHPADLAYOUT_MAPSHADOW>();
    if (s_bUseScratchpad == 0)
    {
        *pStaticMapShadow = *(GRA3DSCRATCHPADLAYOUT_MAPSHADOW *)SPR_GRA3DSCRATCHPADLAYOUT_MAPSHADOW;
        gra3dshadowCD.pSL = pStaticMapShadow;
    }
    else
    {
        *(GRA3DSCRATCHPADLAYOUT_MAPSHADOW *)SPR_GRA3DSCRATCHPADLAYOUT_MAPSHADOW = *pStaticMapShadow;
        gra3dshadowCD.pSL = (GRA3DSCRATCHPADLAYOUT_MAPSHADOW *)SPR_GRA3DSCRATCHPADLAYOUT_MAPSHADOW;
    }
    gra3dshadowInit(&gra3dshadowCD);
}

/* --------------------------------------------------------------------------
 *  gra3dUseScratchpad
 * ------------------------------------------------------------------------ */
void gra3dUseScratchpad(int b)
{
    if (b != s_bUseScratchpad)
    {
        s_bUseScratchpad = b;
        _ModifyScratchpad();
    }
}

/* --------------------------------------------------------------------------
 *  gra3dIsUsingScratchpad
 * ------------------------------------------------------------------------ */
int gra3dIsUsingScratchpad(void)
{
    return s_bUseScratchpad;
}

/* --------------------------------------------------------------------------
 *  _OnExitApp
 * ------------------------------------------------------------------------ */
static void _OnExitApp(void)
{
    printf("-- Application Exit --\n");
}

/* --------------------------------------------------------------------------
 *  gra3dInit
 *
 *  Bring the gra3d layer up: register the exit hook, lay out the scratchpad,
 *  init VIF1 / camera / lights / fog / clip, hand the SGD vertex/normal packet
 *  to the SGD subsystem, and bind the IG3DCompatible / CVu0Matrix function
 *  pointers to the gra3d implementations.
 * ------------------------------------------------------------------------ */
void gra3dInit(void *pPacket, int iSize)
{
    GRA3DSGDCREATIONDATA gra3dsgdCD;

    atexit(_OnExitApp);

    _ModifyScratchpad();

    g3dVif1Init();
    g3dVif1SetRegister(s_aVif1CmdData, 6);

    gra3ddbgVerifyVu1MemAddress();

    _InitCamera();
    _InitLight();
    _InitFog();
    _SetClipValue(-1.0f, 1.0f, -1.0f, 1.0f);

    gra3dsgdCD.vnarray = (sceVu0FVECTOR *)pPacket;
    gra3dsgdCD.size    = (unsigned int)iSize >> 4;
    gra3dsgdInit(&gra3dsgdCD);

    IG3DCompatible::s_pFuncSetGsRegister      = _Gra3dSetGsRegisterForAutoState;
    IG3DCompatible::s_pFuncSetGsRegisters     = _Gra3dSetGsRegistersForAutoState;
    IG3DCompatible::s_pFuncGetGsRegisterRef   = gra3dGetGsRegisterRef;
    IG3DCompatible::s_pFuncSetTransform       = _Gra3dSetTransformForAutoState;
    IG3DCompatible::s_pFuncGetTransformRef    = _Gra3dGetTransformRefForAutoState;

    CVu0Matrix::s_pFuncLoadMatrix             = _Vu0LoadMatrix_4_5_6_7;
    CVu0Matrix::s_pFuncApplyMatrixWithoutTrans = _Vu0ApplyMatrixWithoutTrans_4_5_6_7;
}

/* --------------------------------------------------------------------------
 *  gra3dDraw
 *
 *  Per-frame entry: re-assert the aspect ratio and run the model-hierarchy
 *  draw.
 * ------------------------------------------------------------------------ */
void gra3dDraw(void)
{
    /* PORT DEVIATION -- display pixel aspect.
     *
     * fAspectY is the CRT's pixel aspect, not a camera parameter: it scales
     * view->screen Y so the 640x448 GS frame comes out square on the TV.  The
     * ROM's 0.875 is the NTSC value, and gra3dcamSetAspect() multiplies it by
     * g_fPALAspectScale (8/7) in PAL mode, landing on exactly 1.0 -- i.e. the
     * PAL frame has square pixels.
     *
     * The host has no CRT.  It presents the 640x448 frame at 640:448 and the
     * whole renderer is built on that: UpdateViewExtend() measures the window
     * against kLogicalWidth/kLogicalHeight, and gra3dApplyCamera() grows the
     * clip volume by the result.  That arithmetic only closes when the
     * projection's own aspect is 640:448, which requires fAspectY == 1.0.
     *
     * This disc is PAL (SLES_523.84) and boots PAL in the ROM, so it got 1.0
     * for free.  InitSysWrk() here boots NTSC instead to run at 60Hz, which
     * leaves fAspectY at 0.875 and renders every frame 8/7 (14.3%) too wide --
     * unmistakable on a character, easy to miss on a room.  Ask for the value
     * that ends at 1.0 in whichever mode is running. */
    gra3dcamSetAspect(1.0f, GetPALMode() != 0 ? 0.875f : 1.0f);
    MhCtlDraw();
}

/* --------------------------------------------------------------------------
 *  global constructors keyed to g_Vu0Matrix
 *
 *  The compiler-generated file-scope ctor for g_Vu0Matrix is omitted; the
 *  object itself is the natural file-scope CVu0Matrix instance defined earlier
 *  in gra3d.c.
 * ------------------------------------------------------------------------ */

/* --------------------------------------------------------------------------
 *  _Vu0LoadMatrix_4_5_6_7
 *
 *  Load a 4x4 matrix into the VU0 register file rows vf4..vf7 (the bound
 *  CVu0Matrix matrix-load implementation).
 * ------------------------------------------------------------------------ */
/* Emulated VU0 matrix register file (vf4..vf7): on the EE a matrix was _lqc2'd
   into these registers and a later _Vu0ApplyMatrixWithoutTrans_4_5_6_7 read them
   back.  With no host register file we mirror that hand-off through this
   file-scope matrix. */
static sceVu0FMATRIX s_matVu0_4567;

static void _Vu0LoadMatrix_4_5_6_7(float (*mat)[4])
{
    sceVu0CopyMatrix(s_matVu0_4567, mat);
}

/* --------------------------------------------------------------------------
 *  _Vu0ApplyMatrixWithoutTrans_4_5_6_7
 *
 *  Apply the vf4..vf7 matrix to vSrc and store the result in vDest.
 *
 *  "WithoutTrans" names what is dropped on the *source* side, not the matrix:
 *  the ROM's last accumulate is
 *
 *      vmaddw.xyzw vf12, vf7, vf0        vf12 = ACC + row3 * vf0.w
 *
 *  and vf0.w is the constant 1.0, so the translation row IS applied -- what is
 *  discarded is vSrc's own w, which never reaches the arithmetic.  (That is why
 *  the character bounding-box table in charBB.c can store w == 0 in every
 *  entry.)  A caller that wants a rotation-only apply loads a matrix whose
 *  row 3 is zero; the ROM has no separate rotation-only entry point, this
 *  function pointer is bound once in gra3dInit and every CVu0Matrix::
 *  ApplyWithoutTrans call in the engine lands here.
 * ------------------------------------------------------------------------ */
static void _Vu0ApplyMatrixWithoutTrans_4_5_6_7(float *vDest, float *vSrc)
{
    /* vDest = vSrc.x*row0 + vSrc.y*row1 + vSrc.z*row2 + row3 */
    for (int c = 0; c < 4; c++)
    {
        vDest[c] = vSrc[0]*s_matVu0_4567[0][c]
                 + vSrc[1]*s_matVu0_4567[1][c]
                 + vSrc[2]*s_matVu0_4567[2][c]
                 + s_matVu0_4567[3][c];
    }
}

/* ==========================================================================
 *  MIOPAN_PROBE -- TEMPORARY.  Delete this block, its prototype in gra3d.h,
 *  and the two call sites (scene.c / plyr_mdl.c) when the cutscene hand
 *  blowout is resolved.  grep MIOPAN_PROBE.
 *
 *  Why this exists: the hand carrying the torch saturates in CUTSCENES only.
 *  Static analysis ruled out the scale/clamp chain (exact against ff2_00.vsm),
 *  the HDR transform, bEnableToChar, and "scene.c is missing the character
 *  light builder" (a jal scan proved the ROM has no scene-side call either).
 *  What is left is a difference in WHICH lights reach the character, which
 *  only a measurement can settle.
 *
 *  Read it as: any slot at a distance of ~0 from the torch is lighting its own
 *  carrier, and a slot present under "scene" but absent under "gameplay" is
 *  the asymmetry.  Compare fMaxRange and vSpecular between the two dumps --
 *  bTimes is fMaxRange * fDiv, so a large range pins coef to its cap of 1 far
 *  out, and at coef == 1 the exponent e == 1 too, i.e. FULL diffuse AND FULL
 *  specular.
 * ======================================================================== */
void MioPan_ProbeDumpCharLights(const char *where, const float *vRef)
{
    static int s_iLines;

    if (s_iLines >= 8)
    {
        return;
    }
    s_iLines++;

    printf("[MIOPAN_PROBE %s] ref=(%.1f %.1f %.1f)\n",
           where, (double)vRef[0], (double)vRef[1], (double)vRef[2]);

    for (int i = 0; i < NUM_GRA3DLIGHTID; i = i + 1)
    {
        if (gra3dIsLightEnable(i) == 0)
        {
            continue;
        }

        G3DLIGHT &rL = gra3dGetLightRef(i);
        float     dx = rL.vPosition[0] - vRef[0];
        float     dy = rL.vPosition[1] - vRef[1];
        float     dz = rL.vPosition[2] - vRef[2];
        float     d  = sqrtf(dx * dx + dy * dy + dz * dz);

        printf("    slot %2d type %d dist %8.1f range %8.1f cone %.4f "
               "dif(%.2f %.2f %.2f) spc(%.2f %.2f %.2f)\n",
               i, (int)rL.Type, (double)d, (double)rL.fMaxRange,
               (double)rL.afPad0[0],
               (double)rL.vDiffuse[0], (double)rL.vDiffuse[1], (double)rL.vDiffuse[2],
               (double)rL.vSpecular[0], (double)rL.vSpecular[1], (double)rL.vSpecular[2]);
    }
}
