/* ==========================================================================
 *  gra3dSGD.c
 *
 *  SGD (Scene Graph Data) model renderer for the zero2np 3D engine.
 *
 *  Handles remapping/parsing of SGD process-unit chains, building VIF1/VU1
 *  packets for vertex/normal (VUVN) and mesh data, material/light/coordinate
 *  setup, bounding-box clipping, weighted-skin vertex blending, the preset
 *  (pre-lit) render path, TRI2 texture file loading, ST-coordinate editing
 *  and per-vertex colour editing.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 *
 *  NOTE on Shift-JIS: a handful of assert/warning messages were authored in
 *  Japanese (Shift-JIS).  The decoded text is given in a comment above each
 *  use, matching the convention in gra3dSGDData.c.
 * ======================================================================== */

#include "gra3dSGD.h"
#include "gra3dSGDData.h"
#include "gra3d.h"
#include "gra3dBoundingBox.h"
#include "g3ddbg.h"
#include "g3dCore.h"
#include "g3dDma.h"
#include "g3dLightEx.h"
#include "g3dUtil.h"
#include "g3dxVu0.h"
#include "gra3dDma.h"
#include "gra3dShadow.h"
#include "gra3dTRI2.h"
#include "gra3dVu0.h"           /* gra3dVu0BlendVectorWeighted / ApplyMatrix2 / ClipBB / CalcBBCenter */
#include "miopan/rendering/miopan_graph3d.h"
#include "miopan/miopan_profiler.h"
#include "ctl/fixed_array.h"    /* fixed_array / _fixed_array_verifyrange */
#include <algorithm>
#include <cstring>

/* -------------------------------------------------------------------------
 * file-local state
 * ---------------------------------------------------------------------- */
static float          (*s_pGlobalVertexBuffer)[4]; /* sdata 3f12a8 */
static float          (*s_pGlobalNormalBuffer)[4]; /* sdata 3f12ac */
static int             s_iGlobalBufferSize;        /* sdata 3f12b0 */

static SGDPROCUNITHEADER *save_tri2_pointer;       /* sbss  3f4cd8 */
static SGDPROCUNITHEADER *save_bw_pointer;         /* sbss  3f4cdc */
static SGDPROCUNITHEADER *s_ppuhVUVN;              /* sbss  3f4ce0 */
static SGDCOORDINATE     *s_pCoordBase;            /* sbss  3f4ce4 */
static SGDFILEHEADER     *s_pSGDTop;               /* sbss  3f4ce8 */
static CoordCache         ccahe;                   /* bss   4b3c10 */
static int                edge_check;              /* sbss  3f4cec */
/* sdata 3f1308.  The ROM initialises this to { 0.0f, -1 } -- the bytes at
 * 0x3f1308 are 00000000 ffffffff -- and the -1 is load-bearing: it is what
 * _SelectLightByType tests with `aiIndex[i] >= 0` to tell a ranked light from
 * an empty slot.  Left as a zero-initialised static, every slot the ranking
 * did not fill binds light index 0 of its type instead of staying dark, so a
 * room with one spot in range comes out with all three g3d slots pointing at
 * the same light. */
static _LIGHTCOMPAREDATA  s_NullLightCompareData = { 0.0f, -1 };
static SGDPROCUNITHEADER *previous_tri2_prim;      /* sbss  3f4cf0 */
unsigned int              dma_1;                   /* VU1 microprogram placeholder */

template <int NUM>
static void _SelectLightByType(G3DLIGHTTYPE type, float avBB[][4]);
static void _CalcWeightedVertexBuffer(float *dp, float *v);
static void _CalcWeightedNormalBuffer(float *dp, float *v);

/* --------------------------------------------------------------------------
 *  _HostPublishLight  (port addition -- no ROM counterpart)
 *
 *  Publish the gra3d light bank into the g3d slots that g3dCalcVertexColor()
 *  reads, which is how the host stands in for the VU1's per-vertex lighting.
 *
 *  Two things the ROM never had to do:
 *
 *  - The directional slots are bound here.  Realtime directional lighting never
 *    went through g3d at all on the EE: it came from gra3d's own
 *    s_lmDiffuseLight / s_lmSpecularLight columns, which
 *    _SetVu1LightData_Directional() hands to the VU1 alongside the colours
 *    gra3dCalcVu1MaterialDataDirectional() folds.  They carry most of a
 *    character's light, because playerSetLight() collapses the whole room --
 *    including the flashlight's bounce -- into exactly these three slots via
 *    gra3dEmulateLightData().  SelectLight() covers point and spot, so only the
 *    directional half is missing on the preset path.
 *
 *  - g3dApplyLight() republishes all nine into the VU1 image the lighting
 *    kernels read.  The ROM only ever called it from the prelighting pass, so
 *    without it a model would light from whichever object was pre-lit last.
 * ------------------------------------------------------------------------ */
static void _HostPublishLight(void)
{
    for (int i = 0; i < GRA3D_NUM_LIGHT_DIRECTIONAL; i++)
    {
        int bEnable = gra3dIsLightEnable(i + GRA3D_START_LIGHT_DIRECTIONAL);

        g3dLightEnable(i + G3D_START_LIGHT_DIRECTIONAL, bEnable);

        if (bEnable != 0)
        {
            g3dSetLight(i + G3D_START_LIGHT_DIRECTIONAL,
                        &gra3dGetLightRef(i + GRA3D_START_LIGHT_DIRECTIONAL));
        }
    }

    g3dApplyLight();
}

static const float s_matIdentity[4][4] =
{
    {1.0f, 0.0f, 0.0f, 0.0f},
    {0.0f, 1.0f, 0.0f, 0.0f},
    {0.0f, 0.0f, 1.0f, 0.0f},
    {0.0f, 0.0f, 0.0f, 1.0f},
};


/* The fixed_array<> bounds-check helpers (_fixed_array_assert /
 * _fixed_array_verifyrange<T>) are inline templates from ctl/fixed_array.h;
 * the compiler emits a static copy of each into this object file. */


/* =========================================================================
 *  top-of-file accessors
 * ====================================================================== */

static SGDFILEHEADER *_GetSGDTop()
{
    return s_pSGDTop;
}

static PHEAD *_GetLPHEAD()
{
    G3DASSERT(_GetSGDTop(), "sgd_top_addr is null");
    return (PHEAD *)(SGDVECTORINFO *)s_pSGDTop->pVectorInfo;
}

static const float *GetHostRuntimeMeshTransform(int bPostSkinBuffer)
{
    if (bPostSkinBuffer)
    {
        return (const float *)s_matIdentity;
    }

    /*
     * The realtime VU path carries the current local->world matrix through its
     * coordinate/light packet; it does not update GRA3DTS_WORLD.  The host
     * renderer therefore must use the coordinate selected by the most recent
     * bounding-box unit instead of inheriting that unrelated global transform.
     */

    if (s_pCoordBase != NULL && ccahe.cache_on != -1 &&
        ccahe.cn0 >= 0 && ccahe.cn0 < gra3dsgdGetNumBlock() - 1)
    {
        return (const float *)s_pCoordBase[ccahe.cn0].matLocalWorld;
    }

    if (s_pCoordBase != NULL)
    {
        return (const float *)s_pCoordBase[0].matLocalWorld;
    }

    return (const float *)s_matIdentity;
}

static int IsPostSkinVertexBufferActive()
{
    if (s_ppuhVUVN == NULL || s_ppuhVUVN->VUVNDesc.ucVectorType < SVA_WEIGHTED)
    {
        return 0;
    }

    PHEAD* pVI = _GetLPHEAD();
    return pVI != NULL && pVI->pWeightedList != NULL;
}

/* skip forward over a VIF1 packet until the next UNPACK (0x60000000) code */
static u_int *GetNextUnpackAddr(u_int *prim)
{
    while ((*prim & 0x60000000) != 0x60000000)
    {
        prim++;
    }
    return prim;
}


/* =========================================================================
 *  VUVN (vertex / normal) packet generation
 * ====================================================================== */

static void SetVUVNData(SGDPROCUNITHEADER *pPUHead)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_SKINNING);
    SGDVUVNDESC          &rVUVNDesc = pPUHead->VUVNDesc;
    SGDVUVNDATA          *pVUVNData = (SGDVUVNDATA *)&pPUHead[1];
    _VECTORADDRESS       *raVA       = (_VECTORADDRESS *)&pPUHead[3];
    DVECTOR              *ravDest    = (DVECTOR *)g3dDmaOpenPacket();
    unsigned char        *pPacketBytes;
    int                   iExpectedNum;
    int                   iUnpackNum;

    /*
     * The original EE code copies two qwords (the complete 0x20-byte
     * SGDVUVNDATA header).  Its UNPACK code is the fourth word in that
     * header, so NUM lives at destination byte 0x0e.
     */
    std::memcpy(&ravDest[0], pVUVNData, sizeof(*pVUVNData));
    pPacketBytes = (unsigned char *)&ravDest[0];
    iExpectedNum = rVUVNDesc.sNumVertex + rVUVNDesc.sNumNormal + 1;

    /*
     * Ghidra shows an lbu from packet byte 0x0e.  Reading that byte directly
     * also avoids relying on host/compiler bitfield layout for the on-disc VIF
     * word.
     */
    iUnpackNum = pPacketBytes[0x0e];

    G3DASSERT(rVUVNDesc.sNumVertex == rVUVNDesc.sNumNormal, "");
    G3DASSERT(iUnpackNum == iExpectedNum,
              "VUVN UNPACK count mismatch (packet:%d, expected:%d, "
              "vertex:%d, normal:%d, type:0x%02x)",
              iUnpackNum, iExpectedNum, rVUVNDesc.sNumVertex,
              rVUVNDesc.sNumNormal, rVUVNDesc.ucVectorType);

    for (int i = 0; i < rVUVNDesc.sNumVertex; i++)
    {
        /*
         * raVA is an on-disc array of two 32-bit self-relative references.
         * Resolve both references explicitly on the 64-bit host, then copy
         * the vertex and normal qwords as one contiguous DVECTOR.
         */
        *(u_long128 *)&ravDest[1 + i][0] =
            *(u_long128 *)raVA[i].pVertex.get();
        *(u_long128 *)&ravDest[1 + i][1] =
            *(u_long128 *)raVA[i].pNormal.get();
    }

    g3dDmaClosePacket(&ravDest[1 + rVUVNDesc.sNumVertex]);
}

/* The vertex/normal pairs SetVUVNDataPost() last wrote, so SetVUMeshDataPost()
 * can hand them to the host renderer.  On the EE the VU reads this straight out
 * of the DMA packet; the host renderer has no VU, so it needs the pointer.
 * Only valid immediately after a SetVUVNDataPost() call. */
static DVECTOR *s_pPostVUVNVerts;

static void SetVUVNDataPost(SGDPROCUNITHEADER *pPUHead)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_SKINNING);
    SGDVUVNDESC      &rVUVNDesc = pPUHead->VUVNDesc;
    SGDVUVNDATA      *pVUVNData = (SGDVUVNDATA *)&pPUHead[1];
    _VECTORADDRESS   *raVA       = (_VECTORADDRESS *)&pPUHead[3];
    DVECTOR          *ravDest    = (DVECTOR *)g3dDmaOpenPacket();

    /* header qword pair first, then one vertex/normal pair per vertex */
    s_pPostVUVNVerts = &ravDest[1];
    PHEAD            *pVI         = _GetLPHEAD();
    SGDCOORDINATE    *pCoord0;
    SGDCOORDINATE    *pCoord1;
    int               i;

    /* The original copies the complete two-qword VUVN header. */
    std::memcpy(&ravDest[0], pVUVNData, sizeof(*pVUVNData));

    switch (rVUVNDesc.ucVectorType)
    {
    case 2:                                                  /* weighted, single bone-pair */
    {
        if (pVI->pWeightedList == NULL)
        {
            SGDVUVNDATA_WEIGHTED *pW = (SGDVUVNDATA_WEIGHTED *)pVUVNData;

            pCoord0 = gra3dsgdGetCoordinate(pW->ucBoneId0);
            pCoord1 = gra3dsgdGetCoordinate(pW->ucBoneId1);

            for (i = 0; i < rVUVNDesc.sNumVertex; i++)
            {
                /* blend vertex/normal by the two bone matrices in VU0 */
                gra3dVu0BlendVectorWeighted(&ravDest[1 + i],
                                            raVA[i].pVertex.get(),
                                            raVA[i].pNormal.get(),
                                            pCoord0->matLocalWorld,
                                            pCoord1->matLocalWorld);
            }
        }
        else
        {
            for (i = 0; i < rVUVNDesc.sNumVertex; i++)
            {
                *(u_long128 *)&ravDest[1 + i][0] =
                    *(u_long128 *)raVA[i].pVertex.get();
                *(u_long128 *)&ravDest[1 + i][1] =
                    *(u_long128 *)raVA[i].pNormal.get();
            }
        }
        break;
    }

    case 3:                                                  /* weighted, per-vertex bone-pair */
    {
        if (pVI->pWeightedList == NULL)
        {
            for (i = 0; i < rVUVNDesc.sNumVertex; i++)
            {
                SGDVUVNDATA_WEIGHTED *pW =
                    (SGDVUVNDATA_WEIGHTED *)raVA[i].pVertex.get();

                pCoord0 = gra3dsgdGetCoordinate(pW->ucBoneId0);
                pCoord1 = gra3dsgdGetCoordinate(pW->ucBoneId1);

                gra3dVu0BlendVectorWeighted(&ravDest[1 + i],
                                            raVA[i].pVertex.get(),
                                            raVA[i].pNormal.get(),
                                            pCoord0->matLocalWorld,
                                            pCoord1->matLocalWorld);
            }
        }
        else
        {
            for (i = 0; i < rVUVNDesc.sNumVertex; i++)
            {
                *(u_long128 *)&ravDest[1 + i][0] =
                    *(u_long128 *)raVA[i].pVertex.get();
                *(u_long128 *)&ravDest[1 + i][1] =
                    *(u_long128 *)raVA[i].pNormal.get();
            }
        }
        break;
    }

    default:                                                 /* plain copy */
    {
        for (i = 0; i < rVUVNDesc.sNumVertex; i++)
        {
            *(u_long128 *)&ravDest[1 + i][0] =
                *(u_long128 *)raVA[i].pVertex.get();
            *(u_long128 *)&ravDest[1 + i][1] =
                *(u_long128 *)raVA[i].pNormal.get();
        }
        break;
    }
    }

    g3dDmaClosePacket(&ravDest[1 + rVUVNDesc.sNumVertex]);
}


/* =========================================================================
 *  VUMesh packet dispatch (real-time path)
 * ====================================================================== */

static void SetVUMeshData(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDESC &rVUMeshDesc = pPUHead->VUMeshDesc;
    SGDVUMESHDATA *pVUMeshData = (SGDVUMESHDATA *)&pPUHead[1];
    SGDVUVNDESC   *pVUVNDesc   = &s_ppuhVUVN->VUVNDesc;
    SGDVUVNDATA   *pVUVNData   = (SGDVUVNDATA *)&s_ppuhVUVN[1];
    unsigned char  mtype       = pPUHead->VUVNDesc.ucVectorType;

    switch (mtype & 0xd3)
    {
    case 0x00:                                               /* unique */
    {
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        SetVUVNData(s_ppuhVUVN);
        MioPan_Graph3dDrawRuntimeMesh(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()));
        gra3dCallMicroSubroutine2((u_int *)0x12f8);
        return;
    }
    case 0x02:                                               /* weighted */
    {
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        SetVUVNData(s_ppuhVUVN);
        MioPan_Graph3dDrawRuntimeMesh(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()));
        gra3dCallMicroSubroutine2((u_int *)0x598);
        return;
    }
    case 0x42:                                               /* common (no VUVN data) */
    {
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        MioPan_Graph3dDrawRuntimeMesh(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()));
        gra3dCallMicroSubroutine2((u_int *)0x1968);
        return;
    }
    case 0x80:                                               /* preloaded unique */
    {
        MioPan_Graph3dDrawRuntimeMesh(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()));
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        g3dDmaAddPacket(pVUVNData, pVUVNDesc->ucSize);
        gra3dCallMicroSubroutine2((u_int *)0x12f8);
        return;
    }
    case 0x82:                                               /* preloaded weighted */
    {
        MioPan_Graph3dDrawRuntimeMesh(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()));
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        g3dDmaAddPacket(pVUVNData, pVUVNDesc->ucSize);
        gra3dCallMicroSubroutine2((u_int *)0x598);
        return;
    }
    default:
    {
        /* 0x185  「来てはいけないところに来ています。」=
         *        "You have come to a place you should not be." */
        G3DASSERT(0, "来てはいけないところに来ています。(mtype:%d)", pPUHead->VUMeshDesc.ucMeshType);
        return;
    }
    }
}

static void SetVUMeshDataPost(SGDPROCUNITHEADER *pPUHead)
{
    SGDVUMESHDESC &rVUMeshDesc = pPUHead->VUMeshDesc;
    SGDVUMESHDATA *pVUMeshData = (SGDVUMESHDATA *)&pPUHead[1];
    unsigned char  mtype       = pPUHead->VUVNDesc.ucVectorType;

    switch (mtype & 0x53)
    {
    case 0x00:
    {
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        SetVUVNDataPost(s_ppuhVUVN);
        MioPan_Graph3dDrawRuntimeMeshPost(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()),
            IsPostSkinVertexBufferActive() ? s_pPostVUVNVerts : NULL);
        gra3dCallMicroSubroutine2((u_int *)0x12f8);
        return;
    }
    case 0x02:
    {
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        SetVUVNDataPost(s_ppuhVUVN);
        MioPan_Graph3dDrawRuntimeMeshPost(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()),
            IsPostSkinVertexBufferActive() ? s_pPostVUVNVerts : NULL);
        gra3dCallMicroSubroutine2((u_int *)0xc88);
        return;
    }
    case 0x42:
    {
        g3dDmaAddPacket(pVUMeshData, rVUMeshDesc.iTagSize);
        MioPan_Graph3dDrawRuntimeMesh(
            _GetSGDTop(), s_ppuhVUVN, pPUHead,
            GetHostRuntimeMeshTransform(IsPostSkinVertexBufferActive()));
        gra3dCallMicroSubroutine2((u_int *)0x1968);
        return;
    }
    default:
    {
        /* 0x1b8  「来てはいけないところに来ています。」=
         *        "You have come to a place you should not be." */
        G3DASSERT(0, "来てはいけないところに来ています。(mtype:%d)",
                  pPUHead->VUVNDesc.ucVectorType);
        return;
    }
    }
}


/* =========================================================================
 *  coordinate-cache validation
 * ====================================================================== */

static int CheckCoordCache(int cn)
{
    GRA3DMATERIALINDEXCACHE *pMatIndexCachePoint;
    GRA3DMATERIALINDEXCACHE *pMatIndexCacheSpot;
    int                      bEnablePoint;
    int                      bEnableSpot;
    int                      i;

    if ((ccahe.cache_on != -1) && (ccahe.edge_check == _GetEdgeCheck()))
    {
        bEnablePoint = gra3dIsLightTypeEnable(G3DLIGHT_POINT);
        bEnableSpot  = gra3dIsLightTypeEnable(G3DLIGHT_SPOT);

        if ((ccahe.Point.bEnable == bEnablePoint) && (ccahe.Spot.bEnable == bEnableSpot))
        {
            int bSame = 1;

            if (bEnablePoint != 0)
            {
                GRA3DVU1MATERIALCACHE_POINT *pMatCache =
                    (GRA3DVU1MATERIALCACHE_POINT *)g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);

                for (i = 0; i < 3; i++)
                {
                    if (ccahe.Point.aiIndex[i] != pMatCache->Index.aiIndex[i])
                    {
                        bSame = 0;
                        break;
                    }
                }
            }

            if (bSame && (bEnableSpot != 0))
            {
                GRA3DVU1MATERIALCACHE_SPOT *pMatCache =
                    (GRA3DVU1MATERIALCACHE_SPOT *)g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);

                for (i = 0; i < 3; i++)
                {
                    if (ccahe.Spot.aiIndex[i] != pMatCache->Index.aiIndex[i])
                    {
                        bSame = 0;
                        break;
                    }
                }
            }

            if (bSame &&
                (memcmp(s_pCoordBase[cn].matLocalWorld,
                        s_pCoordBase[ccahe.cn0].matLocalWorld, 0x40) == 0))
            {
                return 1;
            }
        }
    }

    ccahe.cache_on   = 1;
    ccahe.edge_check = _GetEdgeCheck();
    ccahe.cn0        = cn;

    if (gra3dIsLightTypeEnable(G3DLIGHT_POINT) != 0)
    {
        GRA3DVU1MATERIALCACHE_POINT *pMatCache =
            (GRA3DVU1MATERIALCACHE_POINT *)g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);

        ccahe.Point.aiIndex[0] = pMatCache->Index.aiIndex[0];
        ccahe.Point.aiIndex[1] = pMatCache->Index.aiIndex[1];
        ccahe.Point.aiIndex[2] = pMatCache->Index.aiIndex[2];
    }
    if (gra3dIsLightTypeEnable(G3DLIGHT_SPOT) != 0)
    {
        GRA3DVU1MATERIALCACHE_SPOT *pMatCache =
            (GRA3DVU1MATERIALCACHE_SPOT *)g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);

        ccahe.Spot.aiIndex[0] = pMatCache->Index.aiIndex[0];
        ccahe.Spot.aiIndex[1] = pMatCache->Index.aiIndex[1];
        ccahe.Spot.aiIndex[2] = pMatCache->Index.aiIndex[2];
    }

    return 0;
}


/* =========================================================================
 *  material packet generation
 * ====================================================================== */

static int old_tag_buf;            /* sdata 3f12c4 */
static SGDMATERIAL *s_pMatOld;     /* sdata 3f12c8 */

static void SetMaterialDataVU(SGDPROCUNITHEADER *pPUHead)
{
    SGDMATERIAL                  *pMat = pPUHead->VUMaterialDesc.pMat;
    GRA3DVU1MATERIALPACKET_POINT *pPacketPoint;
    GRA3DVU1MATERIALPACKET_SPOT  *pPacketSpot;
    int                          *base;
    int                          *top;
    int                           qwc;
    int                           bEnablePoint;
    int                           bEnableSpot;
    int                           i;

    /* a toggle change means the VRAM material cache must be reset */
    if (old_tag_buf != (int)dmaVif1GetToggle())
    {
        sgdResetMaterialCache(_GetSGDTop());
        old_tag_buf = (int)dmaVif1GetToggle();
        pMat        = pPUHead->VUMaterialDesc.pMat;
    }

    if (pMat->iCacheStatus >= 0)
    {
        int bSame = 1;

        bEnablePoint = gra3dIsLightTypeEnable(G3DLIGHT_POINT);
        bEnableSpot  = gra3dIsLightTypeEnable(G3DLIGHT_SPOT);

        if ((pMat->aCache[G3DLIGHT_POINT].bEnable == bEnablePoint) &&
            (pMat->aCache[G3DLIGHT_SPOT].bEnable == bEnableSpot))
        {
            if (bEnablePoint != 0)
            {
                GRA3DVU1MATERIALCACHE_POINT *pMatCache =
                    (GRA3DVU1MATERIALCACHE_POINT *)g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);

                for (i = 0; i < 3; i++)
                {
                    if (pMat->aCache[G3DLIGHT_POINT].aiIndex[i] != pMatCache->Index.aiIndex[i])
                    {
                        bSame = 0;
                        break;
                    }
                }
            }

            if (bSame && (bEnableSpot != 0))
            {
                GRA3DVU1MATERIALCACHE_SPOT *pMatCache =
                    (GRA3DVU1MATERIALCACHE_SPOT *)g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);

                for (i = 0; i < 3; i++)
                {
                    if (pMat->aCache[G3DLIGHT_SPOT].aiIndex[i] != pMatCache->Index.aiIndex[i])
                    {
                        bSame = 0;
                        break;
                    }
                }
            }
        }
        else
        {
            bSame = 0;
        }

        if (bSame && pMat == s_pMatOld)
        {
            return;
        }
    }

    s_pMatOld = pMat;

    base = (int *)g3dDmaOpenPacket();
    /*
     * The original PS2 path cached this packet address in iTagAddressOld and
     * later emitted it through a REF tag. The SGD material layout is still
     * 32-bit file/runtime data here, so storing a host pointer would truncate
     * it on x64. Rebuild the small material packet instead.
     */
    pMat->iTagAddressOld = 0;

    qwc         = 0;
    pPacketSpot = NULL;
    top         = base;
    if (gra3dIsLightTypeEnable(G3DLIGHT_SPOT) != 0)
    {
        pPacketSpot = (GRA3DVU1MATERIALPACKET_SPOT *)base;
        top         = base + 0x20;
        qwc         = 8;
    }

    pPacketPoint = NULL;
    if (gra3dIsLightTypeEnable(G3DLIGHT_POINT) != 0)
    {
        pPacketPoint = (GRA3DVU1MATERIALPACKET_POINT *)top;
        top          = top + 0x20;
        qwc         += 8;
    }
    qwc += 8;

    pMat->iSizeOld = qwc;
    gra3dSetMaterial(pPUHead->VUMaterialDesc.pMat);

    if (pPacketSpot != NULL)
    {
        GRA3DVU1MATERIALCACHE_SPOT *pMatCache;

        gra3dSetVif1Code_Unpack((int *)pPacketSpot, 0x35, 7, 0x6c);
        gra3dCalcVu1MaterialDataSpot(&pPacketSpot->Data);
        pMatCache = (GRA3DVU1MATERIALCACHE_SPOT *)g3dGetVu1MaterialCache(G3DLIGHT_SPOT, 0);
        pMat->aCache[G3DLIGHT_SPOT].aiIndex[0] = pMatCache->Index.aiIndex[0];
        pMat->aCache[G3DLIGHT_SPOT].aiIndex[1] = pMatCache->Index.aiIndex[1];
        pMat->aCache[G3DLIGHT_SPOT].aiIndex[2] = pMatCache->Index.aiIndex[2];
    }
    pMat->aCache[G3DLIGHT_SPOT].bEnable = gra3dIsLightTypeEnable(G3DLIGHT_SPOT);

    if (pPacketPoint != NULL)
    {
        GRA3DVU1MATERIALCACHE_POINT *pMatCache;

        gra3dSetVif1Code_Unpack((int *)pPacketPoint, 0x3c, 7, 0x6c);
        gra3dCalcVu1MaterialDataPoint(&pPacketPoint->Data);
        pMatCache = (GRA3DVU1MATERIALCACHE_POINT *)g3dGetVu1MaterialCache(G3DLIGHT_POINT, 0);
        pMat->aCache[G3DLIGHT_POINT].aiIndex[0] = pMatCache->Index.aiIndex[0];
        pMat->aCache[G3DLIGHT_POINT].aiIndex[1] = pMatCache->Index.aiIndex[1];
        pMat->aCache[G3DLIGHT_POINT].aiIndex[2] = pMatCache->Index.aiIndex[2];
    }
    pMat->aCache[G3DLIGHT_POINT].bEnable = gra3dIsLightTypeEnable(G3DLIGHT_POINT);

    gra3dSetVif1Code_Unpack(top, 0x2e, 7, 0x6c);
    gra3dCalcVu1MaterialDataDirectional((GRA3DVU1MATERIALDATA_DIRECTIONAL *)(top + 4));

    g3dDmaClosePacket(base + qwc * 4);
}


/* =========================================================================
 *  GS image upload / coordinate (light) data packet
 * ====================================================================== */

static void GsImageProcess(SGDPROCUNITHEADER *pPUHead)
{
    MioPan_Graph3dUploadGsImage(pPUHead);
    g3dDmaAddPacket(&pPUHead[1], pPUHead->GSImageDesc.iQWordSize);
}

static void _SetCoordData(SGDPROCUNITHEADER *pPUHead)
{
    GRA3DVU1LIGHTPACKET *pVu1LightPacket;
    SGDCOORDINATE       *cp0;

    if (CheckCoordCache(pPUHead->CoordDesc.iCoordId0) == 0)
    {
        pVu1LightPacket = (GRA3DVU1LIGHTPACKET *)g3dDmaOpenPacket();
        gra3dSetVif1Code_Unpack((int *)pVu1LightPacket, 0x19, 0x15, 0x6c);

        G3DASSERT(pVu1LightPacket, "");                      /* 0x2a0 */
        G3DASSERT(s_pCoordBase, "");                         /* 0x2a1 */

        cp0 = gra3dsgdGetCoordinate(pPUHead->CoordDesc.iCoordId0);
        g3dSetVu1LightData(&pVu1LightPacket->Data, cp0, NULL);

        g3dDmaClosePacket((int *)pVu1LightPacket + 0x58);
        gra3dVu1TransGTEOP();
        SetVU1Header();
    }
}


/* =========================================================================
 *  VUVN-prim tracking accessors
 * ====================================================================== */

void _SetVUVNPRIM(SGDPROCUNITHEADER *ppuhVUVN)
{
    s_ppuhVUVN = ppuhVUVN;
}

SGDPROCUNITHEADER *_GetVUVNPRIM()
{
    return s_ppuhVUVN;
}


/* =========================================================================
 *  bounding-box visibility test
 * ====================================================================== */

static int CheckBoundingBox(SGDPROCUNITHEADER *pPUHead)
{
    SGDBOUNDINGBOXDESC &rBBDesc = pPUHead->BoundingBoxDesc;
    GRA3DCAMERA        *pCam0;
    GRA3DCAMERA        *pCam1;
    SGDCOORDINATE      *pCoord;
    int                 clip1;
    float               matLocalClipObject[4][4];
    float               matLocalClipPolygon[4][4];
    float               avWork[16][4];
    SGDCOORDINATE       Coord;
    SGDCOORDINATE       CoordIn;

    /* mark this coordinate not-in-view-volume by default */
    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);
    Coord  = *pCoord;
    Coord.bInViewvolume = 0;
    gra3dsgdSetCoordinate(&Coord, rBBDesc.iCoordId);

    pCam0  = gra3dGetCamera();
    pCam1  = gra3dGetCamera();
    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);

    /* Concatenate both world->clip matrices onto local->world.  The ROM left the
       two products in the VU0 registers gra3dbbIsInViewvolume read; on the host
       they are locals passed down explicitly (see gra3dVu0ApplyMatrix2). */
    gra3dVu0ApplyMatrix2(matLocalClipObject,  pCam0->matWorldClipObject,  pCoord->matLocalWorld,
                         matLocalClipPolygon, pCam1->matWorldClipPolygon, pCoord->matLocalWorld);

    /* Cull against the object (screen-sized) frustum; the corners written back
       to avWork[8..15] are in polygon (guard-banded) clip space, ready for the
       edge check below. */
    if (gra3dIsBBInViewvolume(avWork + 8, avWork, (float (*)[4])&pPUHead[1],
                              matLocalClipObject, matLocalClipPolygon) == 0)
    {
        return 0;
    }

    /* visible: mark in-view-volume and compute edge clip flags */
    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);
    CoordIn = *pCoord;
    CoordIn.bInViewvolume = 1;
    gra3dsgdSetCoordinate(&CoordIn, rBBDesc.iCoordId);

    clip1 = gra3dVu0ClipBB(avWork + 8, avWork + 12);
    if ((clip1 & 0xffffff) == 0)
    {
        _SetEdgeCheck(0);
    }
    else
    {
        _SetEdgeCheck(1);
    }

    return 1;
}


/* =========================================================================
 *  real-time draw walker
 * ====================================================================== */
static void SgSortUnitPrim(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != NULL)
    {
        G3DASSERT(!((uintptr_t)pPUHead & 0xf), "memory illegal access occured");  /* 0x30e */

        switch (pPUHead->iCategory)
        {
        case 0:
        {
            _SetVUVNPRIM(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 1:
        {
            SetVUMeshData(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 2:
        {
            SetMaterialDataVU(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 4:
        {
            if (CheckBoundingBox(pPUHead) == 0)
            {
                return;
            }
            _SetCoordData(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 5:
        {
            GsImageProcess(pPUHead);
            /* fall through */
        }
        case 3:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        default:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        }
    }
}


/* =========================================================================
 *  weighted-skin vertex blending
 * ====================================================================== */

static float (*_CalcWeightedLocalWorldMatrix(float matRet[4][4], float matLocalWorld[4][4]))[4]
{
    float matTemp[4][4];
    float fScaleSum;

    /* copy the rotation/translation rows verbatim */
    sceVu0CopyMatrix(matRet, matLocalWorld);

    /* matRet[3][3] holds 3 / (|col0| + |col1| + |col2|) - the inverse mean scale.
     *
     * The ROM transposes matLocalWorld into stack scratch (pextlw/pextuw/
     * pcpyld/pcpyud at 0x001b8c7c) and measures the first three rows of *that*,
     * so the three lengths are the matrix's columns, not its rows.  They only
     * agree when the bone scale is uniform; motion.c drives per-axis scale, so
     * spell the gather out. */
    for (int c = 0; c < 3; c++)
    {
        float col[4];

        col[0] = matLocalWorld[0][c];
        col[1] = matLocalWorld[1][c];
        col[2] = matLocalWorld[2][c];
        col[3] = matLocalWorld[3][c];

        fScaleSum = (c == 0) ? g3dxVu0CalcLength(col)
                             : fScaleSum + g3dxVu0CalcLength(col);
    }
    matRet[3][3] = 3.0f / fScaleSum;

    return matTemp;
}

static void SetCoordData(GRA3DVU1LIGHTDATA *pVu1LightData, SGDPROCUNITHEADER *pPUHead)
{
    SGDCOORDINATEDESC &rCoordDesc = pPUHead->CoordDesc;
    SGDCOORDINATE     *pCoord0;
    SGDCOORDINATE     *pCoord1;

    if (rCoordDesc.iCoordId1 != 0)
    {
        if (rCoordDesc.iCoordId0 == 0)
        {
            pCoord0 = nullptr;
        }
        else
        {
            pCoord0 = gra3dsgdGetCoordinate(rCoordDesc.iCoordId0);
        }

        if (rCoordDesc.iCoordId1 == 0)
        {
            pCoord1 = nullptr;
        }
        else
        {
            pCoord1 = gra3dsgdGetCoordinate(rCoordDesc.iCoordId1);
        }

        g3dSetVu1LightData(pVu1LightData, pCoord0, pCoord1);
    }
}

static int s_iWriteSize;   /* sdata 3f12d8 */

static void _CalcWeightedVectorBuffer(_ONELIST *aList, int iNumList, float pvDest[][4],
                                      float pvSrc[][4], LPFUNC_CALCWEIGHTEDVECTORBUFFER pFunc)
{
    float          matWork[4][4];

    G3DASSERT(pFunc, "");                                    /* 0x3d6 */

    for (int i = 0; i < iNumList; i++)
    {
        _ONELIST &rList = aList[i];

        if (i == 0)
        {
            s_iWriteSize = 0;
        }
        s_iWriteSize += rList.usNumVector;

        G3DASSERT(s_iWriteSize < s_iGlobalBufferSize, "");   /* 0x3ed */

        /* load both bone matrices (scale-normalised) into the VU0 register file */
        _CalcWeightedLocalWorldMatrix(matWork, gra3dsgdGetCoordinate(rList.sCoordId0)->matLocalWorld);
        sceVu0LoadMatrix0(matWork);

        _CalcWeightedLocalWorldMatrix(matWork, gra3dsgdGetCoordinate(rList.sCoordId1)->matLocalWorld);
        sceVu0LoadMatrix1(matWork);

        for (int j = 0; j < rList.usNumVector; j++)
        {
            (*pFunc)((float *)pvDest, (float *)pvSrc);
            pvDest++;
            pvSrc += 2;
        }
    }
}

static void CalcVertexBuffer(SGDPROCUNITHEADER *pPUHead)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_SKINNING);
    PHEAD       *pVI = _GetLPHEAD();
    _VERTEXLIST *pVL;

    /* This runs on an SPC_COORDINATE unit, so the 0x8 union is a
     * SGDCOORDINATEDESC -- a second bone id means the geometry that follows is
     * skinned across a bone pair and needs the weighted buffer built. */
    if (pPUHead->CoordDesc.iCoordId1 != 0)
    {
        pVL = (_VERTEXLIST *)(u_int *)pVI->pWeightedList;
        if (pVL != NULL)
        {
            /* Two lists back to back: vertices, then normals.  The normal list
             * begins where the vertex list's entries end -- the same place
             * sgdRemap hands to its second MappingVertexList. */
            _VERTEXLIST *pNL = (_VERTEXLIST *)&pVL->aList[pVL->iNumList];

            _CalcWeightedVectorBuffer(pVL->aList, pVL->iNumList, s_pGlobalVertexBuffer, (float (*)[4])(float *)pVI->pWeightedVertex, _CalcWeightedVertexBuffer);
            _CalcWeightedVectorBuffer(pNL->aList, pNL->iNumList,
                                      s_pGlobalNormalBuffer, (float (*)[4])(float *)pVI->pWeightedNormal,
                                      _CalcWeightedNormalBuffer);
        }
    }
}


/* =========================================================================
 *  post-sort (second pass) draw walker
 * ====================================================================== */

static void SgSortUnitPrimPost(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != NULL)
    {
        switch (pPUHead->iCategory)
        {
        case 0:
        {
            _SetVUVNPRIM(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 1:
        {
            SetVUMeshDataPost(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 2:
        {
            SetMaterialDataVU(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 3:
        {
            int* dest = (int *)g3dDmaOpenPacket();
            gra3dSetVif1Code_Unpack(dest, 0x19, 0x15, 0x6c);
            SetCoordData((GRA3DVU1LIGHTDATA *)(dest + 4), pPUHead);
            g3dDmaClosePacket(dest + 0x58);
            gra3dVu1TransGTEOP();
            ccahe.cache_on = -1;
            SetVU1Header();
            CalcVertexBuffer(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 5:
        {
            GsImageProcess(pPUHead);
            /* fall through */
        }
        case 4:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        default:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        }
    }
}


/* =========================================================================
 *  preset (pre-lit vertex colour) render path
 * ====================================================================== */

/* `pPUHead` is the *mesh* block (SgPreRenderPrim case 1).  The vertex/normal
 * pool it draws from belongs to the VUVN block that preceded it, which case 0
 * latched into s_ppuhVUVN -- so the geometry counts and the data base both
 * come from there, and only the mesh type and strip count come from pPUHead.
 * Reading the descriptor off pPUHead instead lands on VUMeshDesc::iTagSize
 * and makes the iMT_2F assert below fire on every mesh in every room. */
static void SetPreRenderMeshData(SGDPROCUNITHEADER *pPUHead, int bAddColor)
{                                                                       /* 1130 */
    SGDVUVNDESC          &rVUVNDesc = s_ppuhVUVN->VUVNDesc;             /* 1132 */
    SGDVUVNDATA_PRESET   &rVUVNData = *(SGDVUVNDATA_PRESET *)&s_ppuhVUVN[1];    /* 1133 */
    /* The ROM reads both of these through the VUVNDesc arm of the union;
     * ucMeshType/ucNumMesh are the same two bytes with the useful names. */
    int                  mtype      = pPUHead->VUMeshDesc.ucMeshType;   /* 1139 */
    int                  gloops     = pPUHead->VUMeshDesc.ucNumMesh;    /* 1140 */
    float                vV[4];
    float                vN[4];
    float                pcol[4];
    float                first[4];

    SGDVUMESHDATA_PRESET& rVUMeshData = ((SGDPROCUNITDATA*)&pPUHead[1])->VUMeshData_Preset;

    /* iMT_0 / iMT_2 / iMT_2F only */
    G3DRETURN((mtype == iMT_0 || mtype == iMT_2 || mtype == iMT_2F), "");   /* 1157 */

    short sOffsetToPrim = rVUMeshData.sOffsetToPrim;   /* 1159 */
    if (sOffsetToPrim == 0)
    {
        return;
    }

    _SGDVUMESHCOLORDATA *pVMCD = (_SGDVUMESHCOLORDATA *) (&pPUHead->pNext + sOffsetToPrim);  /* 1162 */

    /* Prelighting runs on the EE, not the VU1, so it is the one consumer of
     * gra3dCalcVertexColor() that really does use the ROM's
     * g3dCalcLightDistanceAttenuation() ramp.  Every actual draw -- realtime
     * or preset -- goes through the shared VU1 kernels instead and wants the
     * inverse-distance law.  See g3dCore.c.
     *
     * This was briefly switched to the realtime law on the theory that a dark
     * bake meant the ramp was culling everything.  It is not: the ramp is
     * bounded and generous against the authored data.  p-msn-kagaribi1, the
     * brazier in room 0, carries fMinRange 1500 / fMaxRange 1610, so the ramp
     * is a flat 1.0 out to 1500 units and falls to 0 by 1610 -- a large, hard
     * pool of light.  fMaxRange/d over the same light is 16x at 100 units and
     * still 0.32 at 5000, i.e. it saturates every surface near a lamp and
     * leaks across the whole room; that is what made baked objects render far
     * too bright, indoors and in cutscenes alike.  The dark bake had a
     * different cause -- the spot cone sense, fixed in g3dCore.c. */
    g3dSetRealtimeLighting(0);

    G3DASSERT(!(gloops != rVUVNDesc.sNumNormal && mtype == iMT_2F), "gloop(%d) != rVUVNDesc.sNumNormal(%d), mtype:2F", gloops, rVUVNDesc.sNumNormal); /* 1166 */
    G3DASSERT(!(rVUVNDesc.sNumVertex != rVUVNDesc.sNumNormal && (mtype == iMT_0 || mtype == iMT_2)), "rVUVNDesc.sNumVertex(%d) != rVUVNDesc.sNumNormal(%d), mtype:0,2", rVUVNDesc.sNumVertex, rVUVNDesc.sNumNormal);

    int iVertexCount = 0;
    for (int j = 0; j < gloops; j++)
    {
        pVMCD = (_SGDVUMESHCOLORDATA*) GetNextUnpackAddr((u_int *)pVMCD);

        int loops = pVMCD->VifUnpack.NUM;
        for (int i = 0; i < loops; i++)
        {
            VECTOR3 *pvVSrc;
            VECTOR3 *pvNSrc;
            VECTOR3 *pcolDest = (VECTOR3 *)(&pVMCD->avColor[i]);
            int      iAbs      = iVertexCount + i;

            memset(vV, 0, sizeof(vV));
            vV[3] = 1.0f;
            memset(vN, 0, sizeof(vN));

            if (mtype == iMT_0 || mtype == iMT_2)                       /* 1202 */
            {
                /* Interleaved: one 24-byte position+normal pair per vertex. */
                SGDMESHVERTEXDATA_TYPE2 *p = &rVUVNData.avt2[iAbs];
                pvVSrc = &p->vVertex;                                   /* 1206 */
                pvNSrc = &p->vNormal;                                   /* 1208 */
            }
            else /* iMT_2F */
            {
                /* Flat-shaded: the block is VECTOR3 avNormal[sNumNormal] --
                 * one normal per strip -- followed by the positions, both
                 * packed at 12 bytes.  Not the interleaved avt2[] form. */
                pvNSrc = &rVUVNData.vt2f.avNormal[j];                   /* 1215 */
                pvVSrc = &rVUVNData.vt2f.avNormal[iAbs + rVUVNDesc.sNumNormal]; /* 1216 */
            }

            vV[0] = (*pvVSrc)[0];
            vV[1] = (*pvVSrc)[1];
            vV[2] = (*pvVSrc)[2];
            vN[0] = (*pvNSrc)[0];
            vN[1] = (*pvNSrc)[1];
            vN[2] = (*pvNSrc)[2];

            memset(first, 0, sizeof(first));
            if (bAddColor != 0)
            {
                first[0] = (*pcolDest)[0];
                first[1] = (*pcolDest)[1];
                first[2] = (*pcolDest)[2];
            }

            gra3dCalcVertexColor(pcol, vV, vN, first);

            (*pcolDest)[0] = pcol[0];
            (*pcolDest)[1] = pcol[1];
            (*pcolDest)[2] = pcol[2];
        }

        iVertexCount += loops;
        pVMCD = (_SGDVUMESHCOLORDATA *)(&pVMCD->avColor[loops]);
    }
}

static void SelectLight(SGDPROCUNITHEADER *pPUHead)
{
    SGDBOUNDINGBOXDESC &rBBDesc = pPUHead->BoundingBoxDesc;
    float             (*mat)[4] = s_pCoordBase[rBBDesc.iCoordId].matLocalWorld;
    float               avBBWork[9][4];

    gra3dbbApplyMatrix(avBBWork, (float (*)[4])&pPUHead[1], mat);
    gra3dbbCalcCenter(avBBWork[8], avBBWork);

    _SelectLightByType<4>(G3DLIGHT_POINT, avBBWork);
    _SelectLightByType<4>(G3DLIGHT_SPOT,  avBBWork);
}

static int iCount_GRA3DDL;   /* sdata 3f12dc */
static int iCount_GRA3DPL;   /* sdata 3f12e0 */
static int iCount_GRA3DSL;   /* sdata 3f12e4 */

static int _SetG3DLightForPrelighting(int bFirst)
{
    int iCount_G3DDL;
    int iCount_G3DPL;
    int iCount_G3DSL;
    int i;

    if (bFirst != 0)
    {
        iCount_GRA3DDL = 0;
        iCount_GRA3DPL = 0;
        iCount_GRA3DSL = 0;
    }

    if (!((iCount_GRA3DDL >= GRA3D_NUM_LIGHT_DIRECTIONAL) && (iCount_GRA3DPL >= GRA3D_NUM_LIGHT_POINT) && (iCount_GRA3DSL >= 0x11)))
    {
        iCount_G3DDL = 0;
        iCount_G3DPL = 0;
        iCount_G3DSL = 0;

        /* directional lights: ids [0, 3) */
        for (; iCount_GRA3DDL < GRA3D_NUM_LIGHT_DIRECTIONAL; iCount_GRA3DDL++)
        {
            if (gra3dIsLightEnable(iCount_GRA3DDL) != 0)
            {
                g3dSetLight(iCount_G3DDL, &gra3dGetLightRef(iCount_GRA3DDL));
                iCount_G3DDL++;
            }
        }
        for (i = 0; i < 3; i++)
        {
            g3dLightEnable(i, (i < iCount_G3DDL));
        }

        /* point lights: ids [3, 0x16) */
        for (; (iCount_GRA3DPL < GRA3D_NUM_LIGHT_POINT) && (iCount_G3DPL < GRA3D_START_LIGHT_POINT); iCount_GRA3DPL++)
        {
            if (gra3dIsLightEnable(iCount_GRA3DPL + GRA3D_START_LIGHT_POINT) != 0)
            {
                g3dSetLight(iCount_G3DPL + GRA3D_START_LIGHT_POINT, &gra3dGetLightRef(iCount_GRA3DPL + GRA3D_START_LIGHT_POINT));
                iCount_G3DPL++;
            }
        }
        for (i = 0; i < 3; i++)
        {
            g3dLightEnable(i + 3, (i < iCount_G3DPL));
        }

        /* spot lights: ids [0x16, 0x27) */
        for (; (iCount_GRA3DSL < GRA3D_NUM_LIGHT_SPOT) && (iCount_G3DSL < 3); iCount_GRA3DSL++)
        {
            if (gra3dIsLightEnable(iCount_GRA3DSL + GRA3D_START_LIGHT_SPOT) != 0)
            {
                g3dSetLight(iCount_G3DSL + 6, &gra3dGetLightRef(iCount_GRA3DSL + GRA3D_START_LIGHT_SPOT));
                iCount_G3DSL++;
            }
        }
        for (i = 0; i < 3; i++)
        {
            g3dLightEnable(i + 6, (i < iCount_G3DSL));
        }

        g3dApplyLight();
        return 1;
    }

    return 0;
}

static void SgPreRenderPrim(SGDPROCUNITHEADER *pPUHead)
{
    bool bFirst = true;
    while (pPUHead)
    {
        switch(pPUHead->iCategory)
        {
            case VUVN:           _SetVUVNPRIM(pPUHead); break;
            case MESH:
                bFirst = true;
                while (_SetG3DLightForPrelighting(bFirst))
                {
                    SetPreRenderMeshData(pPUHead, bFirst ^ 1);
                    bFirst = false;
                }
                break;
            case MATERIAL:      gra3dSetMaterial(pPUHead->VUMaterialDesc.pMat); break;
            case COORDINATE:    gra3dSetTransform(GRA3DTS_WORLD, s_pCoordBase[pPUHead->CoordDesc.iCoordId0].matLocalWorld); break;
            case BOUNDING_BOX:
            default:            break;
        }
        pPUHead = pPUHead->pNext;
    }
}


/* =========================================================================
 *  coordinate table accessors
 * ====================================================================== */

SGDCOORDINATE *gra3dsgdGetCoordinate(int iIndex)
{
    SGDCOORDINATE *pCoord = s_pCoordBase + iIndex;

    G3DASSERT(iIndex < gra3dsgdGetNumBlock(), "");           /* 0x61e */

    return pCoord;
}

void gra3dsgdSetCoordinate(SGDCOORDINATE *pCU, int iIndex)
{
    G3DASSERT(pCU, "");

    if (iIndex == -1)
    {
        s_pCoordBase = (SGDCOORDINATE *)pCU;
        return;
    }

    s_pCoordBase[iIndex] = *pCU;
}


/* =========================================================================
 *  preset vertex colour clear
 * ====================================================================== */

static void ClearPreRenderMeshData(SGDPROCUNITHEADER *pPUHead)
{
    int    mtype  = pPUHead->VUVNDesc.ucVectorType;
    int    gloops = pPUHead->VUVNDesc.aucPad[0];
    short  sOffsetToPrim = *(short *)((uintptr_t)&pPUHead[1].iCategory + 2);
    u_int *prim;
    int    j;

    if (sOffsetToPrim == 0)
    {
        return;
    }
    prim = (u_int *)(&pPUHead->pNext + sOffsetToPrim);

    switch (mtype)
    {
    case iMT_0:                                              /* 0x10 */
    case iMT_2:                                              /* 0x12 */
    case iMT_2F:                                             /* 0x32 */
    {
        for (j = 0; j < gloops; j++)
        {
            u_int *pUnpack = GetNextUnpackAddr(prim);
            int    loops   = *(byte *)((uintptr_t)pUnpack + 2);
            u_int *pCol    = pUnpack + 1;
            int    k;

            for (k = loops; k != 0; k--)
            {
                if (*pCol != 1)
                {
                    pCol[0] = 0;
                    pCol[1] = 0;
                    pCol[2] = 0;
                }
                pCol += 3;
            }
            prim = pUnpack + loops * 3 + 1;
        }
        break;
    }
    case 2:                                                  /* nothing to clear */
    {
        return;
    }
    default:
    {
        G3DWARNING(0, "illegal type(mtype:%d)", mtype);             /* 0x667 */
        break;
    }
    }
}

static void SgClearPreRenderPrim(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != NULL)
    {
        if (pPUHead->iCategory == 1)
        {
            ClearPreRenderMeshData(pPUHead);
        }
        pPUHead = pPUHead->pNext;
    }
}


/* =========================================================================
 *  VU1 microprogram setup
 * ====================================================================== */

void gra3dsgdSetupVu1()
{
    ccahe.cache_on = -1;

    if (gra3dGetNumEnableLight(G3DLIGHT_POINT) == 0)
    {
        gra3dEnableLightType(G3DLIGHT_POINT, 0);
    }
    else
    {
        gra3dEnableLightType(G3DLIGHT_POINT, 1);
    }

    if (gra3dGetNumEnableLight(G3DLIGHT_SPOT) == 0)
    {
        gra3dEnableLightType(G3DLIGHT_SPOT, 0);
    }
    else
    {
        gra3dEnableLightType(G3DLIGHT_SPOT, 1);
    }

    gra3dSetTransform(GRA3DTS_WORLDSCREEN, gra3dGetCamera()->matWorldScreen);
    gra3dSetTransform(GRA3DTS_WORLDCLIP, gra3dGetCamera()->matWorldClipPolygon);
}


/* =========================================================================
 *  preset bounding box / transform setup
 * ====================================================================== */

int BoundingBoxCalcP(SGDPROCUNITHEADER *_prim)
{
    SGDBOUNDINGBOXDESC &rBBDesc = _prim->BoundingBoxDesc;
    GRA3DCAMERA        *pCam;
    SGDCOORDINATE      *pCoord;
    float             (*rMat)[4];
    float               matWorldScreen[4][4];
    float               matWorldClip[4][4];
    SGDCOORDINATE       CU;

    pCam   = gra3dGetCamera();
    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);
    rMat   = pCoord->matLocalWorld;

    gra3dSetTransform(GRA3DTS_WORLD, rMat);

    sceVu0MulMatrix(matWorldScreen, pCam->matWorldScreen, pCoord->matLocalWorld);
    gra3dSetTransform(GRA3DTS_WORLDSCREEN, matWorldScreen);

    if (_GetEdgeCheck() == 0)
    {
        sceVu0MulMatrix(matWorldClip, pCam->matWorldClipPolygon, pCoord->matLocalWorld);
        gra3dSetTransform(GRA3DTS_WORLDCLIP, matWorldClip);
    }
    else
    {
        sceVu0MulMatrix(matWorldClip, pCam->matWorldClipObject, pCoord->matLocalWorld);
        gra3dSetTransform(GRA3DTS_WORLDCLIP, matWorldClip);
    }

    pCoord = gra3dsgdGetCoordinate(rBBDesc.iCoordId);
    CU     = *pCoord;
    CU.edge_check = _GetEdgeCheck();
    gra3dsgdSetCoordinate(&CU, rBBDesc.iCoordId);

    SelectLight(_prim);

    /* SelectLight() has just rebound this coordinate's point and spot slots
     * from the whole 39-light bank -- in a room that is where the player's
     * flashlight arrives, since MapDrawRoomOne() enables nothing else.  Publish
     * the result (plus the directional half SelectLight does not cover) so the
     * host lights this object's vertices with it. */
    _HostPublishLight();

    return 1;
}

static void SetVUMeshDataP(SGDPROCUNITHEADER *pPUHead)
{
    u_int          dsize;
    SGDVUMESHDESC &rVUMeshDesc = pPUHead->VUMeshDesc;
    SGDVUVNDESC   &rVUVNDesc   = _GetVUVNPRIM()->VUVNDesc;
    SGDPROCUNITHEADER *pVUVN   = _GetVUVNPRIM();

    switch (rVUMeshDesc.ucMeshType)
    {
    case iMT_0:
    {
        MioPan_Graph3dDrawPresetMesh(_GetSGDTop(), pVUVN, pPUHead, (const float *)gra3dGetTransformRef(GRA3DTS_WORLD));
        gra3dDmaLoadVu1MicroProgram((u_int *)&dma_1);
        g3dDmaAddPacket(&pPUHead[2], rVUMeshDesc.iTagSize);
        dsize = _GetVUVNPRIM()->VUVNDesc.ucSize;
        g3dDmaAddPacket(&_GetVUVNPRIM()[4], dsize);

        if (_GetEdgeCheck() == 0)
        {
            gra3dCallMicroSubroutine2((u_int *)0x1ba8);
        }
        else
        {
            gra3dCallMicroSubroutine1((u_int *)0xf58);
        }
        return;
    }
    case iMT_2:
    case iMT_2F:
    {
        MioPan_Graph3dDrawPresetMesh(_GetSGDTop(), pVUVN, pPUHead, (const float *)gra3dGetTransformRef(GRA3DTS_WORLD));
        gra3dDmaLoadVu1MicroProgram((u_int *)&dma_1);
        g3dDmaAddPacket(&pVUVN[1], rVUVNDesc.ucSize);
        g3dDmaAddPacket(&pPUHead[2], rVUMeshDesc.iTagSize);

        if (_GetEdgeCheck() == 0)
        {
            gra3dCallMicroSubroutine2((u_int *)0x2108);
        }
        else
        {
            gra3dCallMicroSubroutine1((u_int *)0xf58);
        }
        return;
    }
    case 0x52:
    case 0x72:
    {
        if (_GetEdgeCheck() == 0)
        {
            gra3dCallMicroSubroutine2((u_int *)0x3180);
        }
        return;
    }
    default:
    {
        return;
    }
    }
}


/* =========================================================================
 *  VRAM page-size helper + TRI2 file management
 * ====================================================================== */

unsigned int gra3dCalcVRAMPageSize(unsigned int uiBlockSize)
{
    if ((uiBlockSize & 0x1f) != 0)
    {
        return (uiBlockSize >> 5) + 1;
    }
    return uiBlockSize >> 5;
}

SGDPROCUNITHEADER *_GetPREVIOUSTRI2PRIM()
{
    return previous_tri2_prim;
}

void _SetPREVIOUSTRI2PRIM(SGDPROCUNITHEADER *p)
{
    previous_tri2_prim = p;
}

static void LoadTRI2Files(SGDPROCUNITHEADER *pPUHead)
{
    SGDTEXTUREIMAGEDESC *pTexDesc;
    SGDTRI2FILEHEADER   *pTRI2Head;

    if ((pPUHead != NULL) && (pPUHead != _GetPREVIOUSTRI2PRIM()))
    {
        _SetPREVIOUSTRI2PRIM(pPUHead);

        pTexDesc  = &pPUHead->TexDesc;
        pTRI2Head = (SGDTRI2FILEHEADER *)((uintptr_t)&pPUHead[1].pNext + pTexDesc->iPaddingSize);
        pTRI2Head->uiVif1Code_FLUSH = 0x11000000;

        gra3dLoadTRI2FileToVRAM(pTexDesc->iNumTexture, pTRI2Head, 1);
        gra3dSetGsRegister(0, 0x3f);
    }
}


/* =========================================================================
 *  preset draw walker
 * ====================================================================== */

void gra3dsgdDrawPresetDataObject(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead)
    {
        switch (pPUHead->iCategory)
        {
            case VUVN:         _SetVUVNPRIM(pPUHead);       break;
            case MESH:         SetVUMeshDataP(pPUHead);     break;
            case MATERIAL:     SetMaterialDataVU(pPUHead);  break;
            case BOUNDING_BOX:
                if (!CheckBoundingBox(pPUHead))
                {
                    return;
                }
                if (!BoundingBoxCalcP(pPUHead))
                {
                    return;
                }

                _SetCoordData(pPUHead);

                if (save_tri2_pointer)
                {
                    LoadTRI2Files(save_tri2_pointer);
                    save_tri2_pointer = nullptr;
                }

                if (save_bw_pointer)
                {
                    LoadTRI2Files(save_bw_pointer);
                    save_bw_pointer = nullptr;
                }

                break;
            case GS_IMAGE:     GsImageProcess(pPUHead);     break;
            case 0x37: break;
            case 0x38: break;
            case 0x39: break;
            case 0x40: break;
            case 0x41: break;
                // default:                                        break;
        }

        pPUHead = pPUHead->pNext;
    }
}


/* =========================================================================
 *  preset pre-process (TRI2 / black-white image hoisting)
 * ====================================================================== */

static void SgSortPreProcessP(SGDPROCUNITHEADER *pPUHead)
{
    while (pPUHead != NULL)
    {
        switch (pPUHead->iCategory)
        {
        case 5:
        {
            GsImageProcess(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 6:
        case 7:
        case 8:
        case 9:
        case 0xb:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        case 10:
        {
            if (save_tri2_pointer == (SGDPROCUNITHEADER *)-1)
            {
                LoadTRI2Files(pPUHead);
                save_tri2_pointer = NULL;
            }
            else
            {
                save_tri2_pointer = pPUHead;
            }
            pPUHead = pPUHead->pNext;
            break;
        }
        case 0xc:
        {
            G3DASSERT(0, "");                                /* 0x829 */
            pPUHead = pPUHead->pNext;
            break;
        }
        case 0xd:
        {
            if (gra3dIsMonotoneDrawEnable() != 0)
            {
                if (save_bw_pointer == (SGDPROCUNITHEADER *)-1)
                {
                    LoadTRI2Files(pPUHead);
                    save_bw_pointer = NULL;
                }
                else
                {
                    save_bw_pointer = pPUHead;
                }
            }
            pPUHead = pPUHead->pNext;
            break;
        }
        case 0xe:
        {
            G3DASSERT(0, "");                                /* 0x837 */
            pPUHead = pPUHead->pNext;
            break;
        }
        default:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        }

        G3DWARNING((uintptr_t)pPUHead != (int)0xffffffff, "sgd has been broken...");   /* 0x83c */
    }
}


/* =========================================================================
 *  block count accessor
 * ====================================================================== */

int gra3dsgdGetNumBlock()
{
    G3DASSERT(_GetSGDTop(), "sgd_top_addr is null");         /* 0x846 */
    return s_pSGDTop->uiNumBlock;
}


/* =========================================================================
 *  coordinate-cache + global VN buffer accessors
 * ====================================================================== */

CoordCache *_GetCoordCache()
{
    return &ccahe;
}

void _SetCoordCache(CoordCache *pCC)
{
    ccahe = *pCC;
}

float (*_GetGlobalVertexBuffer())[4]
{
    return (float (*)[4])s_pGlobalVertexBuffer;
}

float (*_GetGlobalNormalBuffer())[4]
{
    return (float (*)[4])s_pGlobalNormalBuffer;
}

int _GetGlobalBufferSize()
{
    return s_iGlobalBufferSize;
}

static void SgSetVNBuffer(float (*varraysizeof)[4], int size)
{
    s_pGlobalVertexBuffer = varraysizeof;
    s_iGlobalBufferSize   = size / 2;
    s_pGlobalNormalBuffer = varraysizeof + size / 2;
}


/* =========================================================================
 *  public API
 * ====================================================================== */

void gra3dsgdInit(GRA3DSGDCREATIONDATA *pCD)
{
    SgSetVNBuffer((float (*)[4])pCD->vnarray, pCD->size);
    _SetPREVIOUSTRI2PRIM(NULL);
}

void gra3dsgdSetData(SGDFILEHEADER *pSGDTop)
{
    G3DRETURN(pSGDTop, "pSGDTop is NULL");                                   /* 0x88b */
    G3DRETURN(pSGDTop->uiVersionId == SGD_VALID_VERSIONID, "Invalid SGD File"); /* 0x88f */

    s_pSGDTop = pSGDTop;
}

SGDFILEHEADER *gra3dsgdGetData()
{
    return s_pSGDTop;
}

int _GetEdgeCheck()
{
    return edge_check;
}

void _SetEdgeCheck(int ec)
{
    edge_check = ec;
}


/* =========================================================================
 *  pre-process (real-time path)
 * ====================================================================== */

void SgSortPreProcess(u_int *_prim)
{
    SGDPROCUNITHEADER *pPUHead = (SGDPROCUNITHEADER *)_prim;

    while (pPUHead != NULL)
    {
        switch (pPUHead->iCategory)
        {
        case 5:
        {
            GsImageProcess(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 10:
        {
            LoadTRI2Files(pPUHead);
            pPUHead = pPUHead->pNext;
            break;
        }
        case 0xd:
        {
            if (gra3dIsMonotoneDrawEnable() != 0)
            {
                LoadTRI2Files(pPUHead);
            }
            pPUHead = pPUHead->pNext;
            break;
        }
        default:
        {
            pPUHead = pPUHead->pNext;
            break;
        }
        }
    }
}


/* =========================================================================
 *  master draw entry
 * ====================================================================== */

void _gra3dDrawSGD(SGDFILEHEADER *pSGDTop, SGDRENDERTYPE type, SGDCOORDINATE *pCoord, int pnum)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_SGD_CPU,
                               type == SRT_REALTIME);
    int                  bPreset;
    SGDSELF32<SGDPROCUNITHEADER> *pk;
    CoordCache           CC;
    int                  iNumBlock;
    int                  i;
    int                  b;

    G3DRETURN(pSGDTop, "pSGDTop is NULL");                                   /* 0x8db */

    bPreset = pSGDTop->ucModelType & 1;


    G3DRETURN(pSGDTop->uiVersionId == SGD_VALID_VERSIONID, "Invalid SGD File"); /* 0x8e2 */
    G3DRETURN(!((uintptr_t)pCoord & 0x0f), "SGDFILEHEADER::pCoord is illegal"); /* 0x8e6 */

    gra3dsgdSetData(pSGDTop);
    if (pCoord == NULL)
    {
        gra3dsgdSetCoordinate(pSGDTop->pCoord, -1);
    }
    else
    {
        gra3dsgdSetCoordinate(pCoord, -1);
    }

    if (type == SRT_REALTIME)
    {
        sgdResetMaterialCache(pSGDTop);
        gra3dsgdSetupVu1();
    }
    else if (type == SRT_MAPSHADOW)
    {
        CC = *_GetCoordCache();
        CC.cache_on = -1;
        _SetCoordCache(&CC);
    }

    G3DASSERT(!(pnum != INVALID_SGD_OBJECTID && pnum < 0), "");                                /* 0x919 */
    G3DASSERT(pnum <= gra3dsgdGetNumBlock() - 1, "pnum:%d, gra3dsgdGetNumBlock()-1:%d", pnum, gra3dsgdGetNumBlock() - 1);

    pk = pSGDTop->apProcUnitHead;

    G3DASSERT(pk, "");                                                       /* 0x91e */

    if (bPreset == 0)
    {
        if (type == SRT_REALTIME)
        {
            /* enable the per-object point & spot lights up front */
            for (i = 0; i < 3; i++)
            {
                int bEnable = gra3dIsLightEnable(i * 1 + 3);
                g3dLightEnable(i + 3, bEnable);

                if (bEnable != 0)
                {
                    g3dSetLight(i + 3, &gra3dGetLightRef(i + 3));
                }
            }

            for (i = 0; i < 3; i++)
            {
                int bEnable = gra3dIsLightEnable(i + 0x16);
                g3dLightEnable(i + 6, bEnable);

                if (bEnable != 0)
                {
                    g3dSetLight(i + 6, &gra3dGetLightRef(i + 0x16));
                }
            }

            _HostPublishLight();

            gra3dDmaLoadVu1MicroProgram((u_int *)&dma_1);

            if (pnum < 0)
            {
                SgSortPreProcess((u_int *)(SGDPROCUNITHEADER *)pk[0]);
                iNumBlock = gra3dsgdGetNumBlock();

                for (i = 1; i < iNumBlock - 1; i++)
                {
                    SgSortUnitPrim(pk[i]);
                }

                if (pk[iNumBlock - 1] != NULL)
                {
                    SgSortUnitPrimPost(pk[iNumBlock - 1]);
                }
                return;
            }

            if (pnum == 0)
            {
                SgSortPreProcess((u_int *)(SGDPROCUNITHEADER *)pk[0]);
                return;
            }

            if (pnum == gra3dsgdGetNumBlock() - 1)
            {
                SgSortUnitPrimPost(pk[pnum]);
                return;
            }

            SgSortUnitPrim(pk[pnum]);
            return;
        }
    }
    else if (type == SRT_REALTIME)
    {
        if (pnum < 0)
        {
            save_bw_pointer   = NULL;
            save_tri2_pointer = NULL;
            SgSortPreProcessP(pk[0]);
            iNumBlock = gra3dsgdGetNumBlock();
            for (i = 1; i < iNumBlock; i++)
            {
                gra3dsgdDrawPresetDataObject(pk[i]);
            }
            return;
        }
        if (pnum != 0)
        {
            save_bw_pointer   = NULL;
            save_tri2_pointer = NULL;
            gra3dsgdDrawPresetDataObject(pk[pnum]);
            return;
        }
        save_bw_pointer   = (SGDPROCUNITHEADER *)&_heap_size;
        save_tri2_pointer = (SGDPROCUNITHEADER *)&_heap_size;
        SgSortPreProcessP(pk[0]);
        return;
    }

    if (type == SRT_PRELIGHTING)
    {
        G3DASSERT(bPreset, "");                                              /* 0x99d */
        if (pnum < 0)
        {
            iNumBlock = gra3dsgdGetNumBlock();
            for (i = 1; i < iNumBlock; i++)
            {
                SgPreRenderPrim(pk[i]);
            }
        }
        else if (pnum != 0)
        {
            SgPreRenderPrim(pk[pnum]);
        }
    }
    else if (type == SRT_CLEARPRELIGHTING)
    {
        if (pnum < 0)
        {
            iNumBlock = gra3dsgdGetNumBlock();
            for (i = 1; i < iNumBlock; i++)
            {
                SgClearPreRenderPrim(pk[i]);
            }
        }
        else if (pnum != 0)
        {
            SgClearPreRenderPrim(pk[pnum]);
        }
    }
    else if (type == SRT_MAPSHADOW)
    {
        G3DASSERT(gra3dshadowGetAssignGroup() < 0, "グループパケットが必要！？意味不明"); /*  "A group packet is required!?  Doesn't make sense" */         
        G3DASSERT(pnum == -1, "see old source");                             /* 0x9c1 */

        iNumBlock = gra3dsgdGetNumBlock();
        for (i = 1; i < iNumBlock - 1; i++)
        {
            if (gra3dsgdGetCoordinate(i)->bInViewvolume != 0)
            {
                AssignShadowPrim(pk[i]);
            }
        }
    }
    else
    {
        G3DASSERT(0, "");                                                    /* 0x9d3 */
    }
}


/* =========================================================================
 *  texture-coordinate (ST) editing
 * ====================================================================== */

static void ChangeST(SGDVUMESHTEXGIFTAG *pMGTHead, int iNumMesh, float fAddS, float fAddT)
{
    SGDVUMESHPOINTNUM *pMPointNum;
    SGDVUMESHSTREGSET *pMSTReg;
    SGDVUMESHSTDATA   *pMSTData;

    pMPointNum = (SGDVUMESHPOINTNUM *)pMGTHead[1].auiGifTag;
    pMSTReg = (SGDVUMESHSTREGSET*)&pMPointNum[iNumMesh];
    pMSTData   = (SGDVUMESHSTDATA *)&pMSTReg->auiVifCode[3];

    for (int i = 0; i < iNumMesh; i++)
    {
        unsigned int uiPointNum = pMPointNum->uiPointNum;

        for (int j = 0; j < (int)uiPointNum; j++)
        {
            pMSTData->astData[j].fS += fAddS;
            pMSTData->astData[j].fT += fAddT;
        }

        pMPointNum++;
        pMSTData   = (SGDVUMESHSTDATA *)&pMSTData->astData[uiPointNum];
    }
}

static void ChangeSTP(SGDPROCUNITHEADER *pPUHead, float fAddS, float fAddT)
{
    SGDVUMESHSTREGSET *pMSTReg;
    SGDVUMESHSTDATA   *pMSTData;
    int                gloops = pPUHead->VUMeshDesc.ucNumMesh;
    int                i;
    int                j;

    pMSTData = (SGDVUMESHSTDATA *)((uintptr_t)&pPUHead->VUVNDesc +
                                   (short)pPUHead[1].iCategory * 4 + 4);

    for (i = gloops; i != 0; i--)
    {
        int loops = pMSTData->VifUnpack.NUM;

        for (j = 0; j < loops; j++)
        {
            float *pS = (float *)((uintptr_t)pMSTData + 4 + j * 8);
            float *pT = (float *)((uintptr_t)pMSTData + 8 + j * 8);
            *pS += fAddS;
            *pT += fAddT;
        }

        pMSTData = (SGDVUMESHSTDATA *)((uintptr_t)pMSTData + j * 8 + 4);
    }
}

static void ChangeSTVUMeshData(SGDPROCUNITHEADER *pPUHead, float fAddS, float fAddT)
{
    SGDVUMESHDESC &rVUMeshDesc = pPUHead->VUMeshDesc;
    SGDVUMESHDATA *pVUMeshData = (SGDVUMESHDATA *)&pPUHead[1];
    int            gloops      = rVUMeshDesc.ucNumMesh;
    unsigned char  mtype       = rVUMeshDesc.ucMeshType;

    switch (mtype & 0xd3)
    {
    case 0x00:                                               /* unique: no ST GIF tag */
    {
        return;
    }
    case 0x02:                                               /* weighted */
    {
        ChangeST((SGDVUMESHTEXGIFTAG *)&pPUHead[1], gloops, fAddS, fAddT);
        return;
    }
    case 0x42:                                               /* common */
    {
        return;
    }
    case 0x80:                                               /* preloaded unique */
    {
        return;
    }
    case 0x82:                                               /* preloaded weighted */
    {
        ChangeST((SGDVUMESHTEXGIFTAG *)&pPUHead[1], gloops, fAddS, fAddT);
        return;
    }
    default:
    {
        /* 0xa53  「来てはいけないところに来ています。」=
         *        "You have come to a place you should not be." */
        G3DASSERT(0, "来てはいけないところに来ています。(mtype:%d)", mtype);
        return;
    }
    }
}

static void ChangeSTVUMeshDataP(SGDPROCUNITHEADER *pPUHead, float fAddS, float fAddT)
{
    unsigned char mtype = pPUHead->VUVNDesc.ucVectorType;

    switch (mtype)
    {
    case iMT_2:                                              /* 0x12 */
    case iMT_2F:                                             /* 0x32 */
    {
        ChangeSTP(pPUHead, fAddS, fAddT);
        break;
    }
    default:
    {
        break;
    }
    }
}

static void SgChangeSTUnitPrim(SGDPROCUNITHEADER *pPUHead, int bPreset, float fAddS, float fAddT)
{
    while (pPUHead != NULL)
    {
        if (pPUHead->iCategory == 1)
        {
            if (bPreset == 0)
            {
                ChangeSTVUMeshData(pPUHead, fAddS, fAddT);
            }
            else
            {
                ChangeSTVUMeshDataP(pPUHead, fAddS, fAddT);
            }
        }
        pPUHead = pPUHead->pNext;
    }
}

void gra3dChangeST(SGDFILEHEADER *pSGDTop, float fAddS, float fAddT)
{
    int   bPreset;
    SGDSELF32<SGDPROCUNITHEADER>* pk;
    int   i;

    bPreset = pSGDTop->ucModelType & 1;

    G3DRETURN(pSGDTop, "pSGDTop is NULL");                                   /* 0xa8c */
    G3DRETURN(pSGDTop->uiVersionId == SGD_VALID_VERSIONID, "Invalid SGD File"); /* 0xa90 */

    /* ST data is one of the immutable cached streams.  Drop only this SGD's
     * entries before editing it; queued draws retain their old version.  Its
     * resolved textures survive: they are named by TEX0, not by ST. */
    MioPan_Graph3dNotifyUVChange(pSGDTop);

    pk = pSGDTop->apProcUnitHead;
    gra3dsgdSetData(pSGDTop);

    for (i = 1; i < gra3dsgdGetNumBlock() - 1; i++)
    {
        SgChangeSTUnitPrim((SGDPROCUNITHEADER *)pk[i], bPreset, fAddS, fAddT);
    }
}


/* =========================================================================
 *  per-vertex colour editing (preset models)
 * ====================================================================== */

static void SetVertexColorPresetMeshData(SGDPROCUNITHEADER *pPUHead, int iVertexNo,
                                         float *vSetColor)
{
    int    mtype  = pPUHead->VUMeshDesc.ucMeshType;
    int    gloops = pPUHead->VUMeshDesc.ucNumMesh;
    short  sOffsetToPrim = *(short *)((uintptr_t)&pPUHead[1].iCategory + 2);
    u_int *prim;
    int    iVertexCount;

    if (sOffsetToPrim == 0)
    {
        return;
    }
    prim = (u_int *)(&pPUHead->pNext + sOffsetToPrim);

    switch (mtype)
    {
    case iMT_0:                                              /* 0x10 */
    case iMT_2:                                              /* 0x12 */
    case iMT_2F:                                             /* 0x32 */
    {
        if (iVertexNo < 0)
        {
            /* set every vertex */
            for (int j = 0; j < gloops; j++)
            {
                u_int *pUnpack = GetNextUnpackAddr(prim);
                int    loops   = *(byte *)((uintptr_t)pUnpack + 2);
                float *pCol    = (float *)(pUnpack + 1);
                int    k;

                for (k = loops; k != 0; k--)
                {
                    pCol[0] = vSetColor[0];
                    pCol[1] = vSetColor[1];
                    pCol[2] = vSetColor[2];
                    pCol += 3;
                }
                prim = pUnpack + loops * 3 + 1;
            }
        }
        else
        {
            /* set a single vertex by index */
            iVertexCount = 0;
            for (int j = 0; j < gloops; j++)
            {
                u_int *pUnpack = GetNextUnpackAddr(prim);
                int    loops   = *(byte *)((uintptr_t)pUnpack + 2);

                if (iVertexNo <= iVertexCount + loops)
                {
                    int k;
                    for (k = 0; k < loops; k++)
                    {
                        if (iVertexCount == iVertexNo)
                        {
                            float *pCol = (float *)(pUnpack + k * 3 + 1);
                            pCol[0] = vSetColor[0];
                            pCol[1] = vSetColor[1];
                            pCol[2] = vSetColor[2];
                            return;
                        }
                        iVertexCount++;
                    }
                }
                else
                {
                    iVertexCount += loops;
                }
                prim = pUnpack + loops * 3 + 1;
            }
        }
        break;
    }
    default:
    {
        G3DWARNING(0, "illegal type(mtype:%d)", mtype);             /* 0xae6 */
        break;
    }
    }
}

static void SgSetVertexColorUnitPrimP(SGDPROCUNITHEADER *pPUHead, int iVertexNo, float *vSetColor)
{
    while (pPUHead != NULL)
    {
        if (pPUHead->iCategory == 1)
        {
            SetVertexColorPresetMeshData(pPUHead, iVertexNo, vSetColor);
        }
        pPUHead = pPUHead->pNext;
    }
}

void gra3dSetVertexColorPreset(SGDFILEHEADER *pSGDTop, int iVertexNo, float *vSetColor)
{
    int   i;

    G3DRETURN(pSGDTop, "pSGDTop is NULL");                                   /* 0xb02 */
    G3DRETURN(pSGDTop->uiVersionId == SGD_VALID_VERSIONID, "Invalid SGD File"); /* 0xb06 */

    /* Explicit type: `auto` would deduce SGDPROCUNITHEADTABLE and copy the
     * 4-byte member, so the laundered base would come from the copy. */
    SGDSELF32<SGDPROCUNITHEADER> *pk = pSGDTop->apProcUnitHead;
    gra3dsgdSetData(pSGDTop);

    for (i = 1; i < gra3dsgdGetNumBlock() - 1; i++)
    {
        SgSetVertexColorUnitPrimP(pk[i].get(), iVertexNo, vSetColor);
    }
}


/* =========================================================================
 *  light selection helpers (template instantiations on NUM=4)
 * ====================================================================== */

template <int NUM>
static void _CalcValidLightIndexByType(int *aiRet, G3DLIGHTTYPE type, float avBB[][4]);

template <int NUM>
static void _SelectLightByType(G3DLIGHTTYPE type, float avBB[][4])
{
    int aiIndex[NUM];
    int iIndex;

    _CalcValidLightIndexByType<NUM>(aiIndex, type, avBB);

    gra3dEnableLightType(type, 0);
    gra3dSetValidLightId(type, 0, 0x7fffffff);
    gra3dSetValidLightId(type, 1, 0x7fffffff);
    gra3dSetValidLightId(type, 2, 0x7fffffff);

    for (iIndex = 0; iIndex < G3D_MAX_LIGHT_PER_TYPE; iIndex++)
    {
        if (aiIndex[iIndex] >= 0)
        {
            gra3dSetValidLightId(type, iIndex, aiIndex[iIndex]);
            gra3dEnableLightType(type, 1);
        }
    }
}

template <int NUM>
static void _SortLightCompareData(fixed_array<_LIGHTCOMPAREDATA, NUM> &raLCD,
                                  float fMaxPower, int iIndex)
{
    int j;
    int k;

    for (j = 0; j < NUM - 1; j++)
    {
        if (raLCD[j].fMaxPower < fMaxPower)
        {
            /* shift the weaker entries down to make room */
            for (k = NUM - 1; j < k; k--)
            {
                _LIGHTCOMPAREDATA tmp = raLCD[k];
                raLCD[k]     = raLCD[k - 1];
                raLCD[k - 1] = tmp;
            }
            raLCD[j].iIndex   = iIndex;
            raLCD[j].fMaxPower = fMaxPower;
            return;
        }
    }
}

template <int NUM>
static void _CalcValidLightIndexByType(int *aiRet, G3DLIGHTTYPE type, float avBB[][4])
{
    fixed_array<_LIGHTCOMPAREDATA, NUM> aLCD;
    int   i;
    int   iNum;
    int   iBase;

    std::fill(aLCD.begin(), aLCD.end(), s_NullLightCompareData);

    /* light-id range for this type */
    switch (type)
    {
    case G3DLIGHT_DIRECTIONAL:
    {
        iNum  = GRA3D_NUM_LIGHT_DIRECTIONAL;
        iBase = GRA3D_START_LIGHT_DIRECTIONAL;
        break;
    }
    case G3DLIGHT_POINT:
    {
        iNum  = GRA3D_NUM_LIGHT_POINT;
        iBase = GRA3D_START_LIGHT_POINT;
        break;
    }
    case G3DLIGHT_SPOT:
    {
        iNum  = GRA3D_NUM_LIGHT_SPOT;
        iBase = GRA3D_START_LIGHT_SPOT;
        break;
    }
    default:
    {
        iNum  = 0;
        iBase = INVALID_G3DLIGHTINDEX;
        break;
    }
    }

    for (i = 0; i < iNum; i++)
    {
        int iId = iBase + i;
        if (gra3dIsLightEnable(iId) != 0)
        {
            G3DLIGHT *pLight = &gra3dGetLightRef(iId);

            if (g3dIsBBLightingup(pLight, avBB) != 0)
            {
                /* power = (2*diffuse) * maxrange / distance-to-BB-center */
                float colscale = sceVu0DiffusePower(pLight->vDiffuse);
                _SortLightCompareData<4>(aLCD,
                                         (colscale * pLight->fMaxRange) /
                                             sceVu0DistanceToBB(pLight->vPosition, avBB[8]),
                                         i);
            }
        }
    }

    for (i = 0; i < 3; i++)
    {
        aiRet[i] = aLCD[i].iIndex;
    }
}


/* =========================================================================
 *  weighted vertex / normal blend kernels (VU0 macro mode)
 * ====================================================================== */

static void _CalcWeightedVertexBuffer(float *dp, float *v)
{
    /* dp = (mat0 * v) * w + (mat1 * v) * (1-w), where w = v[7] (the bone weight) */
    sceVu0BlendVertex(dp, v);
}

static void _CalcWeightedNormalBuffer(float *dp, float *v)
{
    /* same blend but normalised and without the translation row */
    sceVu0BlendNormal(dp, v);
}
