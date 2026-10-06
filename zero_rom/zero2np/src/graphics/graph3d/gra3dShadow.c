/* ===================================================================== *
 *  gra3dShadow.c
 *
 *  Projected ("map") shadow renderer.  A source model's bounding box and a
 *  light are registered; the renderer derives a projection direction, builds
 *  a culling matrix, renders the caster(s) into an off-screen render target,
 *  and projects that texture back onto the receiver geometry on VU1.
 *
 *  Most of the per-vertex / per-matrix math below is inlined PS2 VU0 macro
 *  code (g3dxVu0.h / gra3dVu0.h).  Where an inlined _lqc2/_vmaddabc/_sqc2
 *  block is plainly a standard matrix/vector primitive it is written here as
 *  the corresponding sceVu0 / g3dxVu0 call (matching how gra3dSGDData.c's
 *  sgdCalcCoordinate documented its inlined block); the raw intrinsic form is
 *  kept only where the operation is not a clean library primitive.
 *
 *  Shift-JIS asserts are documented with the decoded Japanese in a comment
 *  above the assert, the same way gra3dSGDData.c does.
 * ===================================================================== */

#include "g3ddbg.h"
#include "g3dDma.h"
#include "g3dGeom.h"
#include "g3dUtil.h"
#include "g3dxVu0.h"
#include "gra3d.h"
#include "gra3dBoundingBox.h"
#include "gra3dConst.h"
#include "gra3dDebug.h"
#include "gra3dDma.h"
#include "g3dRenderTarget.h"
#include "gra3dShadow.h"
#include "gra3dSGD.h"
#include "gra3dSGDData.h"
#include "../dmaVif1.h"
#include "../../common/variable.h"
#include "miopan/rendering/miopan_graph3d.h"   /* receiver mesh submission */
#include "miopan/rendering/miopan_renderer.h"  /* MioPan_RendererShadow*    */
#include <libgraph.h>
#include "ctl/fixed_array.h"    /* fixed_array */
#include "ctl/fixed_stack.h"    /* fixed_stack<SGDFILEHEADER *, 40> */
#include "ctl/originholder.h"   /* originholder<G3DLIGHT> (RAII save/restore) */
#include <algorithm>
#include <cmath>
#include <cstring>

#ifndef ABS
#define ABS(x) (((x) < 0) ? -(x) : (x))
#endif

extern unsigned int dma_1;

/* ----- module statics (see globals.txt) -------------------------------- */
/* ROM .data 0x318210, read straight out of the ELF -- NOT zero.  Three of the
 * seven ship enabled, and two of those gate the whole subsystem:
 * gra3dDrawSGDShadowCharacter() tests bDrawCharShadow before casting a
 * character's shadow and MhCtlDrawShadow() tests bDrawObjectShadow before
 * casting the room's, so a zero-initialised struct silently disables every
 * shadow in the game -- the shadow code is simply never entered.
 * See [[zeroed-statics-lose-rom-initialisers]]. */
GRA3DSHADOWDEBUG               g_gra3dShadowDebug = {
    0,  /* bDrawShadowModelBB  -- debug: draw the caster's bounding box     */
    0,  /* bDrawCastShadowOnBB -- debug: draw each receiver's bounding box  */
    0,  /* bDrawLightDir       -- debug: draw the projection direction      */
    1,  /* bTextureMapEnable   -- texture-map bit in the projection GIFtags */
    0,  /* bFogEnable          -- fog bit in the projection GIFtags         */
    1,  /* bDrawCharShadow     -- player / sister shadows                   */
    1,  /* bDrawObjectShadow   -- room object shadows under the flashlight  */
};
/* ROM .sdata 0x3f13c0 = -1, not 0.  _gra3dDrawSGD()'s SRT_MAPSHADOW arm asserts
 * gra3dshadowGetAssignGroup() < 0, and nothing in this build ever calls
 * gra3dshadowSetAssignGroup(), so -1 is its permanent value -- a zero here
 * fires that assert on the first shadow.
 * See [[zeroed-statics-lose-rom-initialisers]]. */
static int                     shadow_apgnum = -1;
/* ROM .data 0x318230.  gra3dshadowInit() copies this into the live scratchpad
 * layout, so it is the projection pass's entire VU1 image: the VIF1 code, the
 * two constant vf registers, the double-buffer addresses, the screen offset,
 * and the two GIFtags SetUpShadow() patches the texture-map and fog bits into.
 * All zeros here meant the shadow packet was built from an empty template. */
static GRA3DSCRATCHPADLAYOUT_MAPSHADOW s_gra3dScratchpadLayoutDefault = {
    .qwVif1Code = { 0, 0, 0x01000404, 0x6c190000 },
    .Vu1Mem = { .Direct = {
        ._vf01        = { 1.0f, 1.0f, 1.0f, -1.0f },
        ._vf02        = { 1.0f, 0.0f, 0.0f, 0.0f },
        .DataAddress  = { 0x60, 0x230, { 0, 0 } },
        .vOffsetData  = { 0.0f, 0.0f, 625.0f, 0.0f },
        .gtTRISTRIP   = { 0x00008000, 0x30224000, 0x00000412, 0 },
        .gtTRIFAN     = { 0x00008000, 0x3022c000, 0x00000412, 0 },
        /* Alpha 64 is what makes _IsDraw() true before _CalcColor() has run. */
        .ivColor      = { 128, 128, 128, 64 },
    } },
};
static GRA3DSCRATCHPADLAYOUT_MAPSHADOW *s_pScratchpadLayout;
/* ROM .data 0x3183d0.  SetShadowCamera() only writes the position, target and
 * aspect -- the field of view, depth range, screen centre and projection type
 * come from here, so a zeroed struct gave _ApplyCamera() fFov 0, near == far 0
 * and PT_PERSPECTIVE, i.e. a degenerate projection that renders a blank
 * shadow map.  fFov is one ulp under PI/3 and fNearZ one ulp under 0.1: EE GCC
 * literal truncation, see [[ee-gcc-truncates-float-literals]]. */
static GRA3DCAMERA             s_Camera = {
    .fFov     = 1.0471975f,        /* 60 degrees, truncated  */
    .fNearZ   = 0.099999994f,      /* 0.1f, truncated        */
    .fFarZ    = 32768.0f,
    .fAspectX = 1.0f,
    .fAspectY = 1.0f,
    .fCenterX = 2048.0f,
    .fCenterY = 2048.0f,
    .fZmin    = 0.0f,
    .fZmax    = 16777000.0f,
    /* Orthographic -- which is what a projected shadow wants, and the only
     * camera in the build that selects it.  MioPan_Graph3dApplyCamera()'s
     * comment claiming nothing uses PT_ORTHO is now out of date; its generic
     * z' = 0.5w - 0.5z column is the path this camera takes, and it still
     * yields the renderer's reversed-Z (near 1, far 0). */
    .type     = PT_ORTHO,
    .vTarget  = { 0.0f, 120.0f, 0.0f, 0.0f },
};
static CRenderTarget           s_RenderTarget;
static fixed_stack<SGDFILEHEADER *, 40> s_stackpProjectModel;
static float                   s_vDirection[4];
static float                   s_matIP[4][4];
static float                   s_matCull[4][4];
static float                   s_avBB[9][4];          /* [0..7] corners, [8] center/target */
static SGDFILEHEADER          *s_pSourceModel;
static G3DLIGHT                s_Light;
static float                   s_fFundamentScale;
static float                   s_fTextureScale;
static int                     s_iMaxTextureWidth;
static int                     s_iMaxTextureHeight;
static const float             s_matTransTexture[4][4] = { /* rdata: texture remap matrix */
    { 0.5f, 0.0f, 0.0f, 0.0f },
    { 0.0f, 0.5f, 0.0f, 0.0f },
    { 0.0f, 0.0f, 1.0f, 0.0f },
    { 0.5f, 0.5f, 0.0f, 1.0f },
};

/* PSMCT32, not PSMT8H as an earlier pass guessed.  _RenderShadow()'s assert is
 * `lui s1,0x3f0` (bits 20-25, the TEX0 PSM field) followed by `beq v0,zero` --
 * it skips when PSM is zero, so the format it demands IS zero.  gra3dshadowInit
 * agrees: it writes TBP0, TW, TH, TBW, TCC, TFX, CBP, CPSM, CSM, CSA and CLD
 * one field at a time (ROM lines 1346-1357) and never touches PSM at all, so
 * the target is left at PSMCT32.  With PSMT8H here the assert fired on every
 * shadow. */
#define GRA3D_SHADOWTEXTURE_FORMAT  SCE_GS_PSMCT32

/* The fixed_array / fixed_stack bounds-check helpers (_fixed_array_assert /
 * _fixed_array_verifyrange<T>) are inline templates from ctl/fixed_array.h;
 * the compiler emits a static copy of each into this object file (here for
 * void*, char* and unsigned int*, plus SGDFILEHEADER* via the stack). */


/* ===================================================================== *
 *  small helpers
 * ===================================================================== */

static int _GetCurrentFBP()
{
    /* alternate frame buffer between fields */
    if ((sys_wrk.count & 1) == 0)
    {
        return 0;
    }
    return 0x8c;
}

static int _IsDraw()
{
    /* color alpha non-zero means there is something to draw */
    return (s_pScratchpadLayout->Vu1Mem.Direct.ivColor[3] != 0);
}

static void SetVU1HeaderShadow()
{
    g3dDmaCopyPacket(s_pScratchpadLayout, 0x1a);
}

/* ===================================================================== *
 *  mesh assignment on VU1
 * ===================================================================== */

/* PORT: the receiver block's local->world, stashed by AssignShadowPrim()'s
 * coordinate case so ShadowMeshDataVU() can hand it to the host renderer.  On
 * hardware the matrix only ever reaches VU1 (as matLIP = s_matIP * this), and
 * the mesh case has no other way to reach it. */
static float (*s_pHostReceiverLocalWorld)[4];

static void ShadowMeshDataVU(SGDPROCUNITHEADER *pPUHead)
{
    int             mtype       = pPUHead->VUVNDesc.ucVectorType & 0x53;
    SGDVUMESHDESC  &rVUMeshDesc  = pPUHead->VUMeshDesc;

    /* The ROM's own second comparison is unreachable: 0x32 & 0x53 == 0x12, so
     * `mtype` can never equal 0x32.  It is a GCC switch decision tree over the
     * masked value (equal 0x12 -> body, less than 0x13 -> exit, equal 0x32 ->
     * body) and is reproduced as found. */
    if (mtype == 0x12 || mtype == 0x32)
    {
        dmaVif1AddRefTag((uintptr_t)(_GetVUVNPRIM() + 1),
                         _GetVUVNPRIM()->VUVNDesc.ucSize);
        g3dDmaAddPacket(pPUHead + 2, rVUMeshDesc.iTagSize);

        /* PORT: submit the same block to the host as a shadow receiver.  The
         * VU1 program this packet targets (SgSuShadow_dma_main) is what
         * projects the shadow texture over the block; with no VU here the
         * renderer replays the geometry through the projective receiver
         * pipeline instead.  The DMA above is left intact so the emulated ring
         * stays consistent with the rest of the engine. */
        if (s_pHostReceiverLocalWorld != NULL)
        {
            MioPan_RendererShadowBeginReceiver();
            /* The PRESET bridge, not the runtime one.  The gate above admits
             * exactly iMT_2 (0x12) and iMT_2F (0x32), which are preset mesh
             * types -- the same pair SetVUMeshDataP() handles -- because the
             * receivers are room geometry and rooms are preset SGDs.
             * DrawRuntimeMesh() filters on (ucMeshType & 0xd3) against the
             * non-preset families {0x00,0x02,0x42,0x80,0x82} and silently
             * dropped every one of these.
             *
             * gra3dsgdGetData() is gra3dSGD.c's own _GetSGDTop(), which is
             * file-static there; both return s_pSGDTop, the model
             * _gra3dDrawSGD() is currently walking. */
            MioPan_Graph3dDrawPresetMesh(gra3dsgdGetData(), _GetVUVNPRIM(),
                                         pPUHead,
                                         (const float *)s_pHostReceiverLocalWorld);
            MioPan_RendererShadowEndReceiver();
        }

        if (_GetEdgeCheck() != 0)
        {
            gra3dCallMicroSubroutine3((u_int *)0xb20);
        }
        else
        {
            gra3dCallMicroSubroutine4((u_int *)0x1a68);
        }
    }
}

/* ===================================================================== *
 *  bounding-box vs shadow-region clipping
 * ===================================================================== */

/* Project one homogeneous corner through the culling matrix (vf4..vf7) and
 * return the VU0 clip flags for it. */
static int ShadowBoundClip(float *v0, const float *v1)
{
    /* The cull matrix lived in the VU0 registers vf4..vf7 loaded by the caller;
       with no host register file the projection + hardware VU clip can't be
       reproduced.  Pass the corner through and report no clip (visible). */
    int c;
    for (c = 0; c < 4; c++) v0[c] = v1[c];
    return 0;
}

/* shadowtex[4][4]: the 4 corners of the shadow texture quad, ROM .data
 * 0x3185b0 -- a unit square centred on the origin, w = 1.  Zeroed here, every
 * corner was the same point and the overlap test degenerated. */
static float shadowtex[4][4] = {
    { -0.5f, -0.5f, 0.0f, 1.0f },
    {  0.5f, -0.5f, 0.0f, 1.0f },
    { -0.5f,  0.5f, 0.0f, 1.0f },
    {  0.5f,  0.5f, 0.0f, 1.0f },
};

/* Test whether the screen-projected edge described by 'bl' overlaps the
 * shadow texture rectangle (separating-axis style min/max test). */
static int AppendShadowClipCheck(float (*sts)[4], const BoundLine *bl)
{
    float bmin;
    float bmax;
    float smin;
    float smax;
    int   i;
    int   s;
    int   e;
    float kei[4];

    /* slope of the bounding edge */
    if (sts[bl->e][0] == sts[bl->s][0])
    {
        kei[0] = 0.0f;
    }
    else
    {
        kei[0] = (sts[bl->e][1] - sts[bl->s][1]) / (sts[bl->e][0] - sts[bl->s][0]);
    }

    if (kei[0] == 0.0f)
    {
        return 1;
    }

    /* project the first bounding-edge point and the first shadow corner onto
     * the edge normal ( dot with (kei, -1) ) */
    {
        sceVu0FVECTOR n;

        n[0] = kei[0];
        n[1] = -1.0f;
        n[2] = 0.0f;
        n[3] = 0.0f;

        bmax = g3dxVu0InnerProduct(sts[bl->s], n);
        smax = g3dxVu0InnerProduct(shadowtex[0], n);
    }

    bmin = bmax;
    smin = smax;
    e    = bl[1].s;

    for (i = 2; i >= 0; i--)
    {
        sceVu0FVECTOR n;
        float         bproj;
        float         sproj;

        n[0] = kei[0];
        n[1] = -1.0f;
        n[2] = 0.0f;
        n[3] = 0.0f;

        bproj = g3dxVu0InnerProduct(sts[e], n);
        sproj = g3dxVu0InnerProduct(shadowtex[3 - i], n);

        if (bproj > bmax)
        {
            bmax = bproj;
        }
        if (bproj < bmin)
        {
            bmin = bproj;
        }
        if (sproj > smax)
        {
            smax = sproj;
        }
        if (sproj < smin)
        {
            smin = sproj;
        }

        e = bl[2 - i].s;
    }

    s = 0;
    if (bmin <= smax)
    {
        s = 1;
        if (bmax < smin)
        {
            s = 0;
        }
    }
    return s;
}

/* The 12 edges of the bounding box used by the trace test. */
/* ROM .data 0x318630: the 12 edges of a bounding box as (start, end) corner
 * index pairs -- four along each axis.  Zeroed here, all twelve edges were the
 * degenerate 0->0. */
static BoundLine boundline[12] = {
    { 0, 1 }, { 4, 5 }, { 2, 3 }, { 6, 7 },
    { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
    { 1, 3 }, { 5, 7 }, { 4, 6 }, { 0, 2 },
};

/* clip matrix used to project box corners into shadow-texture space.
 * ROM .data 0x3185f0: identity with a -0.5 translation and w 0.5. */
static float clipmtx[4][4] = {
    {  1.0f,  0.0f,  0.0f, 0.0f },
    {  0.0f,  1.0f,  0.0f, 0.0f },
    {  0.0f,  0.0f,  1.0f, 0.0f },
    { -0.5f, -0.5f, -0.5f, 0.5f },
};

static int CheckBoundingBoxShadowTrace(float (*lwmtx)[4], float (*tmpv)[4], const float *dir)
{
    int   i;
    int   clip;
    int   clip0;
    int   clip1;
    float tmpmat[4][4];
    float setmat[4][4];

    /* setmat = s_matCull * lwmtx  (cull-space transform of this object) */
    sceVu0MulMatrix(setmat, s_matCull, lwmtx);

    /* quick reject: clip all 8 corners against the cull frustum. The 0x2a
     * mask keeps only the side planes we care about. */
    {
        u_int f0 = ShadowBoundClip((float *)&DAT_70003900, tmpv[0]);
        u_int f1 = ShadowBoundClip((float *)&DAT_70003910, tmpv[1]);
        u_int f2 = ShadowBoundClip((float *)&DAT_70003920, tmpv[2]);
        u_int f3 = ShadowBoundClip((float *)&DAT_70003930, tmpv[3]);
        u_int f4 = ShadowBoundClip((float *)&DAT_70003940, tmpv[4]);
        u_int f5 = ShadowBoundClip((float *)&DAT_70003950, tmpv[5]);
        u_int f6 = ShadowBoundClip((float *)&DAT_70003960, tmpv[6]);
        u_int f7 = ShadowBoundClip((float *)&DAT_70003970, tmpv[7]);

        if ((f0 & 0x2a & f1 & f2 & f3 & f4 & f5 & f6 & f7) != 0)
        {
            return 0;
        }
    }

    /* full test: project corners through s_matIP then clipmtx into the
     * shadow-texture rectangle */
    sceVu0MulMatrix(tmpmat, s_matIP, lwmtx);
    sceVu0MulMatrix(setmat, clipmtx, tmpmat);
    g3dxVu0LoadMatrix(setmat);                 /* into vf4..vf7 for ShadowBoundClip */

    clip0 = 0x2f;
    clip1 = 0;
    for (i = 0; i < 8; i++)
    {
        u_int f = ShadowBoundClip((float *)(&DAT_70003900 + i * 4), tmpv[i]) & 0xf;

        if (f == 0)
        {
            return 1;
        }
        clip1 |= f;
        clip0 &= f;
    }

    clip = 0;
    if (clip0 == 0 && clip1 != 0xf)
    {
        clip = 1;
        if (AppendShadowClipCheck((float (*)[4])&DAT_70003900, &boundline[0]) == 0 ||
            AppendShadowClipCheck((float (*)[4])&DAT_70003900, &boundline[4]) == 0)
        {
            clip = 0;
        }
        else
        {
            clip = 1;
        }
    }
    return clip;
}

static int CheckBoundingBoxShadow(SGDPROCUNITHEADER *pPUHead)
{
    SGDCOORDINATE *pCoord = gra3dsgdGetCoordinate(pPUHead->CoordDesc.iCoordId0);
    int            clip;

    clip = CheckBoundingBoxShadowTrace(pCoord->matLocalWorld,
                                       (float (*)[4])(pPUHead + 1), s_vDirection);
    if (clip != 0)
    {
        _SetEdgeCheck(pCoord->edge_check);
    }
    return (clip != 0);
}

/* ===================================================================== *
 *  target & helpers
 * ===================================================================== */

static void _CalcTarget(float *vTarget, float (*avBB)[4])
{
    gra3dbbCalcCenterBase(vTarget, avBB);
}

static int _IsSamePositionAsTarget(const float *vPosition)
{
    /* within 0.5 units of the shadow target ( s_avBB[8] ) */
    float dx = vPosition[0] - s_avBB[8][0];
    float dy = vPosition[1] - s_avBB[8][1];
    float dz = vPosition[2] - s_avBB[8][2];

    return g3dxVu0Sqrt(dx*dx + dy*dy + dz*dz) < 0.5f;
}

/* ===================================================================== *
 *  per-procunit shadow assignment
 * ===================================================================== */

void AssignShadowPrim(SGDPROCUNITHEADER *pPUHead)
{
    int                              i;
    int                              cn;
    float                            tmpvec[4];
    CoordCache                       CC;
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pSL;

    /* PORT: no receiver matrix until this walk's coordinate case supplies one. */
    s_pHostReceiverLocalWorld = NULL;

    while (pPUHead)
    {
        switch (pPUHead->iCategory)
        {
        case 0:
        {
            _SetVUVNPRIM(pPUHead);
            break;
        }
        case 1:
        {
            ShadowMeshDataVU(pPUHead);
            break;
        }
        case 4:
        {
            SGDCOORDINATE *pCoord;
            CoordCache    *pCC;

            if (CheckBoundingBoxShadow(pPUHead) == 0)
            {
                return;
            }

            cn     = pPUHead->VUMeshDesc.iTagSize;
            pCoord = gra3dsgdGetCoordinate(cn);

            gra3dbbApplyMatrix((float (*)[4])&DAT_70003900,
                               (float (*)[4])(pPUHead + 1), pCoord->matLocalWorld);
            gra3dbbCalcCenterBase((float *)&DAT_70003980, (float (*)[4])&DAT_70003900);

            if (_IsSamePositionAsTarget((float *)&DAT_70003980) != 0)
            {
                return;
            }

            /* PORT: remember this block's local->world for the host receiver
             * submission in ShadowMeshDataVU().  Taken here rather than in the
             * setup block below, because that block is skipped whenever the
             * coordinate cache hits while the mesh still needs the matrix --
             * but after both early-outs, so a rejected block leaves nothing
             * behind for the next one to pick up. */
            s_pHostReceiverLocalWorld = pCoord->matLocalWorld;

            /* 0x4ae */
            if (g_gra3dShadowDebug.bDrawCastShadowOnBB != 0)
            {
                gra3ddbgDrawBB((float (*)[4])&DAT_70003900, 0xff00ffff);
            }

            /* if the cached coordinate's matrix is unchanged we can reuse it */
            pCC = _GetCoordCache();
            if (pCC->cache_on == 1 && pCC->edge_check == _GetEdgeCheck())
            {
                int byteoff = 0;

                for (i = 0; i < 4; i++)
                {
                    SGDCOORDINATE *pNow = gra3dsgdGetCoordinate(cn);
                    SGDCOORDINATE *pOld = gra3dsgdGetCoordinate(_GetCoordCache()->cn0);
                    int            j;

                    for (j = 0; j < 4; j++)
                    {
                        if (*(float *)((uintptr_t)pNow->matLocalWorld[0] + byteoff + j * 4) !=
                            *(float *)((uintptr_t)pOld->matLocalWorld[0] + byteoff + j * 4))
                        {
                            CC = *_GetCoordCache();
                            CC.cache_on = -1;          /* force re-setup */
                            _SetCoordCache(&CC);
                            goto setup;
                        }
                    }
                    byteoff = (i + 1) * 0x10;
                }
            }

setup:
            pSL = s_pScratchpadLayout;
            if (_GetCoordCache()->cache_on != 1)
            {
                SGDCOORDINATE *pCoordSetup = gra3dsgdGetCoordinate(cn);
                float        (*matSrc)[4] = pCoordSetup->matLocalWorld;
                GRA3DCAMERA   *pCamera;

                CC           = *_GetCoordCache();
                CC.cache_on  = 1;
                CC.edge_check = _GetEdgeCheck();
                CC.cn0       = cn;
                _SetCoordCache(&CC);

                /* inverse-transform the shadow box into this object's space */
                g3dMatrixInverseTransform((float (*)[4])&DAT_70003900, matSrc);

                /* light direction in this object's space: rotate s_vDirection by
                   the inverse matrix just written above (rows 0..2). */
                {
                    float (*mInv)[4] = (float (*)[4])&DAT_70003900;
                    int    c;

                    for (c = 0; c < 3; c++)
                        tmpvec[c] = s_vDirection[0]*mInv[0][c]
                                  + s_vDirection[1]*mInv[1][c]
                                  + s_vDirection[2]*mInv[2][c];
                }

                /* matLIP = s_matIP * matLocalWorld */
                sceVu0MulMatrix(pSL->Vu1Mem.Direct.matLIP, s_matIP, matSrc);

                /* matLocalScreen = camera.matWorldScreen * matLocalWorld */
                pCamera = gra3dGetCamera();
                sceVu0MulMatrix(pSL->Vu1Mem.Direct.matLocalScreen,
                                pCamera->matWorldScreen, matSrc);

                /* matLocalClip = camera.matWorldClipPolygon * matLocalWorld */
                pCamera = gra3dGetCamera();
                sceVu0MulMatrix(pSL->Vu1Mem.Direct.matLocalClip,
                                pCamera->matWorldClipPolygon, matSrc);

                /* fold the projection offset into matLIP column-w */
                pSL->Vu1Mem.Direct.matLIP[0][3] = -tmpvec[0];
                pSL->Vu1Mem.Direct.matLIP[1][3] = -tmpvec[1];
                pSL->Vu1Mem.Direct.matLIP[2][3] = -tmpvec[2];
                pSL->Vu1Mem.Direct.matLIP[3][3] = 0.0f;

                /* push the depth a hair to avoid z-fighting (10000.0) */
                pSL->Vu1Mem.Direct.matLocalScreen[3][2] += 10000.0f;

                SetVU1HeaderShadow();
            }
            break;
        }
        case 3:
        default:
        {
            break;
        }
        }

        pPUHead = pPUHead->pNext;
    }
}

/* ===================================================================== *
 *  GS / VU1 setup for the projection pass
 * ===================================================================== */

static void SetUpShadow()
{
    GRA3DVU1MEMLAYOUT_MAPSHADOW_DIRECT &rVu1Mem = s_pScratchpadLayout->Vu1Mem.Direct;
    sceGifPackAd                        aGPA[3];
    G3DFOG                             *pFog;

    pFog = &gra3dGetFogRef();
    g3dCalcVu1Fog(&rVu1Mem.Fog, pFog);

    /* texture-map enable bit in the two strip/fan GIFtags */
    if (g_gra3dShadowDebug.bTextureMapEnable == 0)
    {
        *(u_long *)((uintptr_t)&rVu1Mem + 0x40) &= 0xfff7ffffffffffff;
        *(u_long *)((uintptr_t)&rVu1Mem + 0x50) &= 0xfff7ffffffffffff;
    }
    else
    {
        u_long t0 = *(u_long *)((uintptr_t)&rVu1Mem + 0x40);
        u_long t1 = *(u_long *)((uintptr_t)&rVu1Mem + 0x50);

        *(u_long *)((uintptr_t)&rVu1Mem + 0x40) =
            t0 & 0xfc007fffffffffff | (t0 >> 0x2f & 0x7ff | 0x10) << 0x2f;
        *(u_long *)((uintptr_t)&rVu1Mem + 0x50) =
            t1 & 0xfc007fffffffffff | (t1 >> 0x2f & 0x7ff | 0x10) << 0x2f;
    }

    /* fog enable bit */
    if (g_gra3dShadowDebug.bFogEnable == 0)
    {
        *(u_long *)((uintptr_t)&rVu1Mem + 0x40) &= 0xffefffffffffffff;
        *(u_long *)((uintptr_t)&rVu1Mem + 0x50) &= 0xffefffffffffffff;
    }
    else
    {
        u_long t0 = *(u_long *)((uintptr_t)&rVu1Mem + 0x40);
        u_long t1 = *(u_long *)((uintptr_t)&rVu1Mem + 0x50);

        *(u_long *)((uintptr_t)&rVu1Mem + 0x40) =
            t0 & 0xfc007fffffffffff | (t0 >> 0x2f & 0x7ff | 0x20) << 0x2f;
        *(u_long *)((uintptr_t)&rVu1Mem + 0x50) =
            t1 & 0xfc007fffffffffff | (t1 >> 0x2f & 0x7ff | 0x20) << 0x2f;
    }

    gra3dDmaLoadVu1MicroProgram((u_int *)&dma_1);
    gra3dSetGsRegisterDefault();

    /* bind the shadow render target texture as the projector source */
    aGPA[0].DATA = *(u_long *)&s_RenderTarget.GetGsTex0Ref();
    aGPA[0].ADDR = SCE_GS_TEX0_1;
    aGPA[1].DATA = 5;
    aGPA[1].ADDR = SCE_GS_TEX1_1;
    aGPA[2].DATA = 0x5801b;
    aGPA[2].ADDR = SCE_GS_TEST_1;
    gra3dSetGsRegisters(aGPA, 3);
}

/* ===================================================================== *
 *  projection-matrix construction
 * ===================================================================== */

/* Build a rotation matrix that aligns +Z with vSrc (light direction). */
static void GetRotMatrixZAxis(float (*matDest)[4], const float *vSrc)
{
    float  vWork[4];
    float  ry;
    float  rx;

    sceVu0UnitMatrix(matDest);

    vWork[0] = vSrc[0];
    vWork[1] = vSrc[1];
    vWork[2] = vSrc[2];

    /* rotate about Y to bring the vector into the YZ plane */
    if (vWork[0] != 0.0f || vWork[2] != 0.0f)
    {
        float (*pm1)[4] = matDest;

        ry = -g3dAtan2f(vSrc[0], vSrc[2]);

        /* wrap to (-pi, pi] */
        if (3.1415925f < ry)
        {
            float f  = fmodf(ry, 6.283185f);
            float f1 = 3.1415925f;
            float f2 = 6.283185f;

            if (f1 < f)
            {
                f -= f2;
            }
            else if (f < -f1)
            {
                f += f2;
            }
            ry = f;
        }

        sceVu0RotMatrixY(matDest, matDest, ry);
        sceVu0ApplyMatrix(vWork, matDest, (float*)vSrc);
    }

    /* rotate about X to finish aligning with +Z */
    if (vWork[1] != 0.0f || vWork[2] != 0.0f)
    {
        rx = g3dAtan2f(vWork[1], vWork[2]);

        if (3.1415925f < rx)
        {
            float f  = fmodf(rx, 6.283185f);
            float f1 = 3.1415925f;
            float f2 = 6.283185f;

            if (f1 < f)
            {
                f -= f2;
            }
            else if (f < -f1)
            {
                f += f2;
            }
            rx = f;
        }

        sceVu0RotMatrixX(matDest, matDest, rx);
    }
}

static void _CalcProjectionShadowMatrix(float (*matProjection)[4], float ax, float ay,
                                        float fTextureScale, int iTexWidth, int iTexHeight,
                                        const float *vLightDir, float (*avBBWorld)[4])
{
    float vScale[4];
    float matRot[4][4];
    float vTransInverse[4];
    float planeCull[4];
    int   aiBBIndex[8];
    float culval[8];
    int   i;

    /* scale the texture-remap matrix by the per-axis aspect */
    vScale[0] = (fTextureScale / (float)iTexWidth) * ax;
    vScale[1] = (-fTextureScale / (float)iTexHeight) * ay;
    vScale[2] = -1.0f;
    vScale[3] = 0.0f;

    matProjection[0][0] = s_matTransTexture[0][0] * vScale[2];
    matProjection[1][0] = s_matTransTexture[1][0] * vScale[2];
    matProjection[2][0] = s_matTransTexture[2][0] * vScale[2];
    g3dxVu0ScaleMatrixColumns(matProjection, s_matTransTexture, vScale);

    /* orient the projector toward the light */
    GetRotMatrixZAxis(matRot, vLightDir);
    sceVu0MulMatrix(matProjection, matProjection, matRot);

    /* translate so the shadow target sits at the projector origin */
    vTransInverse[0] = g_v0001[0] - s_avBB[8][0];
    vTransInverse[1] = g_v0001[1] - s_avBB[8][1];
    vTransInverse[2] = g_v0001[2] - s_avBB[8][2];
    vTransInverse[3] = g_v0001[3] - s_avBB[8][3];
    g3dxVu0TranslateMatrix(matProjection, matProjection, vTransInverse);

    /* find the near plane that bounds all box corners along Z */
    g3dPlaneFromMatrixZ(planeCull, matProjection);

    aiBBIndex[0] = 0;
    aiBBIndex[1] = 1;
    aiBBIndex[2] = 2;
    aiBBIndex[3] = 3;
    aiBBIndex[4] = 4;
    aiBBIndex[5] = 5;
    aiBBIndex[6] = 6;
    aiBBIndex[7] = 7;

    for (i = 0; i < 8; i++)
    {
        culval[i] = g3dxVu0InnerProduct(planeCull, avBBWorld[aiBBIndex[i]]);
    }

    matProjection[3][2] = -*std::min_element(culval, &culval[8]);
}

/* ===================================================================== *
 *  render-target sizing
 * ===================================================================== */

static void CalcShadowHeight(float (*bbox)[4])
{
    static const char s_function[17] = "CalcShadowHeight";
    u_int            tmp;
    float            tmpvec[4];
    sceVu0IVECTOR    itmp;

    /* size from the diagonal length of the box */
    sceVu0SubVector(tmpvec, bbox[0], bbox[7]);
    sceVu0MulVector(tmpvec, tmpvec, tmpvec);

    tmpvec[0] = g3dxVu0Sqrt2(tmpvec[0] + tmpvec[1] + tmpvec[2]) / s_fFundamentScale + 0.5f;

    itmp[0] = (int)tmpvec[0];                          /* _vftoi0 */
    tmp     = itmp[0];

    /* snap to power-of-two-ish tile counts {1,2,4,8} */
    if (tmp == 0)
    {
        tmp = 1;
    }
    else if (tmp == 3)
    {
        tmp = 4;
    }
    else if (tmp - 5 < 3)
    {
        tmp = 8;
    }

    if (tmp - 1 > 1 && tmp != 4 && tmp != 8)
    {
        /* 0x3ea */
        _SetLineInfo("gra3dShadow.c", 0x3ea, __FUNCTION__,
                     "tmp == 1 || tmp == 2 || tmp == 4 || tmp == 8");
        g3ddbgWarning(tmp == 4 || tmp == 8, "tmp : %d", tmp);
    }

    s_RenderTarget.SetWidth(tmp << 6);
    s_RenderTarget.SetHeight(tmp << 6);
    s_RenderTarget.SetWidth(0x100);
    s_RenderTarget.SetHeight(0x100);
}

/* ===================================================================== *
 *  culling matrix
 * ===================================================================== */

static void _CalcCullingMatrix()
{
    float tmpmat[4][4];
    float tmpvec[4];
    int   i;

    /* flip the box along whichever axes the light comes from */
    sceVu0UnitMatrix(tmpmat);
    if (0.0f < s_vDirection[0])
    {
        tmpmat[0][0] = -1.0f;
    }
    if (0.0f < s_vDirection[1])
    {
        tmpmat[1][1] = -1.0f;
    }
    if (0.0f < s_vDirection[2])
    {
        tmpmat[2][2] = -1.0f;
    }

    gra3dbbApplyMatrix((float (*)[4])&DAT_70003900, s_avBB, tmpmat);

    /* accumulate the per-component minimum corner */
    tmpvec[0] = ((float *)&DAT_70003900)[0];
    tmpvec[1] = ((float *)&DAT_70003900)[1];
    tmpvec[2] = ((float *)&DAT_70003900)[2];

    {
        float *p = (float *)&DAT_70003910;

        for (i = 6; i >= 0; i--)
        {
            if (p[0] < tmpvec[0])
            {
                tmpvec[0] = p[0];
            }
            if (p[1] < tmpvec[1])
            {
                tmpvec[1] = p[1];
            }
            if (p[2] < tmpvec[2])
            {
                tmpvec[2] = p[2];
            }
            p += 4;
        }
    }

    /* build s_matCull from the flip matrix with the min corner as translation */
    s_matCull[0][0] = tmpmat[0][0];
    s_matCull[0][1] = tmpmat[0][1];
    s_matCull[0][2] = tmpmat[0][2];
    s_matCull[0][3] = tmpmat[0][3];
    s_matCull[1][0] = tmpmat[1][0];
    s_matCull[1][1] = tmpmat[1][1];
    s_matCull[1][2] = tmpmat[1][2];
    s_matCull[1][3] = tmpmat[1][3];
    s_matCull[2][0] = tmpmat[2][0];
    s_matCull[2][1] = tmpmat[2][1];
    s_matCull[2][2] = tmpmat[2][2];
    s_matCull[2][3] = tmpmat[2][3];
    s_matCull[3][0] = -tmpvec[0];
    s_matCull[3][1] = -tmpvec[1];
    s_matCull[3][2] = -tmpvec[2];
    s_matCull[3][3] = 0.0f;
}

/* ===================================================================== *
 *  shadow camera
 * ===================================================================== */

static void _SetCameraCoord()
{
    float tmpvec[4];

    /* look at the shadow target from a point pushed back along -dir */
    s_Camera.vTarget[0] = s_avBB[8][0];
    s_Camera.vTarget[1] = s_avBB[8][1];
    s_Camera.vTarget[2] = s_avBB[8][2];
    s_Camera.vTarget[3] = s_avBB[8][3];

    sceVu0ScaleVector(tmpvec, s_vDirection, 1000.0f);
    sceVu0AddVector(s_Camera.matCoord[3], s_Camera.vTarget, tmpvec);
}

static void _CalcAspectRatioForRenderShadow(float *pAX, float *pAY)
{
    float matWorldScreen[4][4];
    float xmax;
    float ymax;
    float tmpvec[4];
    int   i;

    if (_IsSamePositionAsTarget(s_Camera.matCoord[3]) != 0)
    {
        *pAX = 1.0f;
        *pAY = 1.0f;
        return;
    }

    s_Camera.fAspectX = 1.0f;
    s_Camera.fAspectY = 1.0f;
    gra3dCalcWorldScreenMatrix(matWorldScreen, &s_Camera, 1);
    gra3dbbApplyMatrix((float (*)[4])&DAT_70003900, s_avBB, matWorldScreen);

    xmax = 0.0f;
    ymax = 0.0f;
    {
        float *p = (float *)&DAT_70003900;

        for (i = 7; i >= 0; i--)
        {
            if (xmax < ABS(p[0] - s_Camera.fCenterX))
            {
                xmax = ABS(p[0] - s_Camera.fCenterX);
            }
            if (ymax < ABS(p[1] - s_Camera.fCenterY))
            {
                ymax = ABS(p[1] - s_Camera.fCenterY);
            }
            p += 4;
        }
    }

    CalcShadowHeight(s_avBB);
    *pAX = ((float)s_RenderTarget.GetWidth() * 0.5f) / xmax;
    *pAY = ((float)s_RenderTarget.GetHeight() * 0.5f) / ymax;
}

static void SetShadowCamera()
{
    _SetCameraCoord();
    _CalcAspectRatioForRenderShadow(&s_Camera.fAspectX, &s_Camera.fAspectY);
    _CalcCullingMatrix();
}

static void _ApplyCamera(const GRA3DCAMERA *pCamera, int bFixUp)
{
    gra3dcamSetPosition((float*)pCamera->matCoord[3]);
    gra3dcamSetTarget((float*)pCamera->vTarget, bFixUp);
    gra3dcamSetAspect(pCamera->fAspectX, pCamera->fAspectY);
    gra3dcamSetFov(pCamera->fFov);
    gra3dcamSetDepth(pCamera->fZmin, pCamera->fZmax);
    gra3dcamSetType(pCamera->type);
    gra3dApplyCamera(NULL, bFixUp);
}

/* ===================================================================== *
 *  rendering
 * ===================================================================== */

static void _RenderShadow(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord, int iObjectIndex)
{
    sceGifPackAd      aGPA0[5];
    int               iWidth;
    int               iHeight;
    sceGifPackAd      aGPA1[2];
    u_long           *pZbuf;

    /* 0x4cf */
    G3DASSERT(GRA3D_SHADOWTEXTURE_FORMAT == s_RenderTarget.GetGsTex0Ref().PSM, "");

    gra3dSetGsRegister((long)_GetCurrentFBP() | 0x10a0000, SCE_GS_FRAME_1);

    SetShadowCamera();
    _ApplyCamera(&s_Camera, 1);

    /* The shadow pass's draw environment.
     *
     * The last entry rewrites ZBUF_1 keeping the live Z buffer's PSM (bits
     * 24-27) and forcing ZMSK (bit 32) -- depth testing stays on, depth writes
     * are off, so rendering the caster cannot disturb the scene's Z buffer.
     *
     * PORT: both the read and the write were SCE_GS_FRAME_2 (0x4d) here.  The
     * ROM uses SCE_GS_ZBUF_1 (0x4e) for both -- `li a0,0x4e` in the
     * gra3dGetGsRegisterRef() delay slot at 0x1bef24, and 0x4e stored as the
     * fifth ADDR at 0x1bef88.  That is not a cosmetic difference: ZBUF_1 is one
     * of the seven registers _GetRegisterSpecified() answers from its own
     * getter, so the ROM's read never touches the core object, while FRAME_2
     * falls through to g3dGetGsRegisterRef() and dereferences s_pObject. */
    pZbuf = (u_long *)&gra3dGetGsRegisterRef(SCE_GS_ZBUF_1);

    aGPA0[0].DATA = 0x44;                 aGPA0[0].ADDR = SCE_GS_ALPHA_1;
    aGPA0[1].DATA = 0x60;                 aGPA0[1].ADDR = SCE_GS_TEX1_1;
    aGPA0[2].DATA = 0;                    aGPA0[2].ADDR = SCE_GS_CLAMP_1;
    aGPA0[3].DATA = 0x30803;              aGPA0[3].ADDR = SCE_GS_TEST_1;
    aGPA0[4].DATA = (*pZbuf & 0xf000000) | 0x100000000;
    aGPA0[4].ADDR = SCE_GS_ZBUF_1;

    gra3dSetGsRegisters(aGPA0, 5);

    iWidth  = s_RenderTarget.GetWidth();
    iHeight = s_RenderTarget.GetHeight();

    s_RenderTarget.Begin();
    s_RenderTarget.Clear();

    aGPA1[0].DATA = 0x3001b;
    aGPA1[0].ADDR = SCE_GS_TEST_1;
    aGPA1[1].DATA = (long)(iWidth - 2) << 0x10 | (long)(iHeight - 2) << 0x30 | 0x100000001;
    aGPA1[1].ADDR = SCE_GS_SCISSOR_1;
    gra3dSetGsRegisters(aGPA1, 2);

    /* PORT: the caster draw.  s_RenderTarget is GS memory the host does not
     * emulate, so instead of writing into it these draws are tagged and
     * replayed into a host shadow-map texture before the frame is composited.
     * EndCaster() also snapshots the light camera _ApplyCamera() installed
     * above -- it has to run before _DrawShadow() puts the game camera back. */
    MioPan_RendererShadowBeginCaster();
    _gra3dDrawSGD(pSGDTop, SRT_REALTIME, pCoord, iObjectIndex);
    MioPan_RendererShadowEndCaster();
    s_RenderTarget.End();
}

static void _DrawShadow(GRA3DCAMERA *pCamera, float ax, float ay)
{
    int           i;
    size_t        i2;
    SGDFILEHEADER *pSGDTop;
    int           iTexWidth;
    int           iTexHeight;

    _gra3dSetCameraForce(pCamera);
    /* PORT: _gra3dSetCameraForce() only assigns the camera struct -- it never
     * reaches gra3dApplyCamera(), so the renderer would still be projecting
     * from the light.  Restore what gra3dshadowDrawSGD() snapshotted. */
    MioPan_RendererShadowRestoreCamera();

    iTexWidth  = s_RenderTarget.GetWidth();
    iTexHeight = s_RenderTarget.GetHeight();
    _CalcProjectionShadowMatrix(s_matIP, ax, ay, s_fTextureScale,
                                iTexWidth, iTexHeight, s_vDirection, s_avBB);
    SetUpShadow();

    /* PORT: hand the projector's darkness to the host.  This is _CalcColor()'s
     * alpha -- the light's diffuse luminance at the shadow target over sqrt(3),
     * halved -- which VU1 reads out of the scratchpad as a 0..255 byte and
     * modulates the projected texture with. */
    MioPan_RendererShadowSetStrength(
        (float)s_pScratchpadLayout->Vu1Mem.Direct.ivColor[3] / 255.0f);

    for (i = 0; i < (int)s_stackpProjectModel.size(); i++)
    {
        pSGDTop = s_stackpProjectModel[i];
        if (pSGDTop != s_pSourceModel)
        {
            _gra3dDrawSGD(pSGDTop, SRT_MAPSHADOW, NULL, -1);
        }
    }

}

/* ===================================================================== *
 *  public API
 * ===================================================================== */

void gra3dshadowInit(GRA3DSHADOWCREATIONDATA *pCD)
{
    sceGsTex0                gsTex0;
    RENDERTARGETCREATIONDATA rtCD;
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pSrc;
    GRA3DSCRATCHPADLAYOUT_MAPSHADOW *pDst;
    u_int                    uLogW;
    u_int                    uLogH;
    int                      iBlockW;

    /* copy the default scratchpad layout into the caller's SP region */
    s_pScratchpadLayout = pCD->pSL;
    pSrc = &s_gra3dScratchpadLayoutDefault;
    pDst = pCD->pSL;
    *pDst = *pSrc;

    uLogW   = g3dLogi2(2, 0x100);
    uLogH   = g3dLogi2(2, 0x100);
    iBlockW = 1 << (uLogW & 0xf);
    if (iBlockW < 0)
    {
        iBlockW += 0x3f;
    }

    s_iMaxTextureWidth  = g3dLogi2(2, 0x100);
    s_iMaxTextureHeight = g3dLogi2(2, 0x100);

    s_fTextureScale   = 1.0176f;
    s_fFundamentScale = 1000.0f;

    s_stackpProjectModel.clear();

    *(u_long *)&rtCD.gsTex0 = (long)(int)(uLogW & 0xf) << 0x1a | 0x3000U |
                              (long)(int)(uLogH & 0xf) << 0x1e |
                              ((long)(iBlockW >> 6) & 0x3fU) << 0xe |
                              0x2006000400000000;
    rtCD.ClearColor = 0;
    rtCD.fZMax      = 0.77111465f;
    s_RenderTarget.Create(&rtCD);
}

void gra3dshadowAddProjectModel(SGDFILEHEADER *pSGDTop)
{
    size_t            i;

    if (pSGDTop == NULL)
    {
        return;
    }

    /* 0x56b */
    G3DRETURN(s_stackpProjectModel.size() != s_stackpProjectModel.max_size(), "Number of ProjectModel is over");

    /* skip if already registered */
    for (i = 0; i < s_stackpProjectModel.size(); i++)
    {
        if (s_stackpProjectModel[i] == pSGDTop)
        {
            return;
        }
    }

    s_stackpProjectModel.push_exclusive(pSGDTop);
}

void gra3dshadowSetSourceModel(SGDFILEHEADER *pSM)
{
    s_pSourceModel = pSM;
}

void gra3dshadowSetAssignGroup(int gnum)
{
    shadow_apgnum = gnum;
}

int gra3dshadowGetAssignGroup()
{
    return shadow_apgnum;
}

void gra3dshadowClearProjectModel()
{
    s_stackpProjectModel.clear();

    /* PORT: MhCtlDraw() calls this once at the top of the frame, before the
     * room registers its receivers, which is exactly where the host's shadow
     * capture state wants clearing too. */
    MioPan_RendererShadowReset();
}

/* ===================================================================== *
 *  light / direction / color
 * ===================================================================== */

static void _CalcColor()
{
    float                   vWork[4];
    originholder<G3DLIGHT>  oh(&s_Light);   /* save & restore s_Light */
    G3DLIGHT               *pv0;
    G3DLIGHT               *pv1;
    float                   fAtten;
    sceVu0IVECTOR           ivColor;

    /* spotlight: widen the cone for a softer projected edge */
    if (s_Light.Type == G3DLIGHT_SPOT)
    {
        s_Light.fAngleOutside += s_Light.fAngleOutside;
        s_Light.fAngleInside  *= 0.5f;
    }

    /* diffuse * attenuation, with alpha from luminance/sqrt(3) */
    fAtten      = g3dCalcLightAttenuation(&s_Light, s_avBB[8]);
    vWork[0]    = s_Light.vDiffuse[0] * fAtten;
    vWork[1]    = s_Light.vDiffuse[1] * fAtten;
    vWork[2]    = s_Light.vDiffuse[2] * fAtten;
    vWork[3]    = (g3dxVu0CalcLength(vWork) / g3dxVu0Sqrt2(3.0f)) * 0.5f;

    /* pack to 0..255 and store as the projector color */
    ivColor[0] = (int)(vWork[0] * 255.0f);
    ivColor[1] = (int)(vWork[1] * 255.0f);
    ivColor[2] = (int)(vWork[2] * 255.0f);
    ivColor[3] = (int)(vWork[3] * 255.0f);
    memcpy((char *)&s_pScratchpadLayout->Vu1Mem + 0x170, ivColor, sizeof(ivColor));
}

static void _CalcDirection()
{
    switch (s_Light.Type)
    {
    case G3DLIGHT_POINT:
    {
        /* 0x5c6  ( #exp == "0" ) */
        G3DASSERT(0, "");
        return;
    }
    case G3DLIGHT_DIRECTIONAL:
    {
        s_vDirection[0] = s_Light.vDirection[0];
        s_vDirection[1] = s_Light.vDirection[1];
        s_vDirection[2] = s_Light.vDirection[2];
        s_vDirection[3] = s_Light.vDirection[3];
        return;
    }
    case G3DLIGHT_SPOT:
    {
        /* direction = normalize( target - position ) */
        g3dxVu0NormalizeVector(s_vDirection, s_Light.vPosition, s_avBB[8]);
        return;
    }
    case G3DLIGHTTYPE_FORCE_DWORD:
    {
        s_vDirection[0] = g_v0000[0];
        s_vDirection[1] = g_v0000[1];
        s_vDirection[2] = g_v0000[2];
        s_vDirection[3] = g_v0000[3];
        return;
    }
    default:
    {
        /* 0x5cf  ( #exp == "0" ) */
        G3DASSERT(0, "");
        return;
    }
    }
}

void gra3dshadowSetBoundingBox(float (*avBB)[4], float (*mat)[4])
{
    gra3dbbApplyMatrix(s_avBB, avBB, mat);
    _CalcTarget(s_avBB[8], s_avBB);
    _CalcDirection();
    _CalcColor();
}

void gra3dshadowSetLight(G3DLIGHT *pLight)
{
    static const char s_function[20] = "gra3dshadowSetLight";
    float            *pv;
    float             fAllowableError = 0.001f;

    s_Light = *pLight;

    /* 0x5e8 */
    G3DWARNING(g3dxVu0VectorIsNormalized(s_Light.vDirection),
               "g3dxVu0VectorIsNormalized( s_Light.vDirection )");

    _CalcDirection();
    _CalcColor();
}

float (*gra3dshadowGetTarget())[4]
{
    return &s_avBB[8];
}

/* ===================================================================== *
 *  top-level draw entry
 * ===================================================================== */

void gra3dshadowDrawSGD(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord, int iObjectIndex)
{
    static const char s_function[19] = "gra3dshadowDrawSGD";
    GRA3DCAMERA       camOrigin;
    float             vvv[4];

    /* save the live camera so we can restore it after the shadow pass */
    camOrigin = *gra3dGetCamera();

    /* PORT: the host keeps its view and projection in the renderer rather than
     * in GRA3DCAMERA, so the struct copy above does not save them.  Take the
     * matching snapshot here; _DrawShadow() puts it back. */
    MioPan_RendererShadowSaveCamera();

    if (g_gra3dShadowDebug.bDrawLightDir != 0)
    {
        gra3dSetGsRegisterDefault();
        /* draw a line from the target along the projection direction */
        sceVu0ScaleVector(vvv, s_vDirection, 1000.0f);
        prefetch(0x4b3eb0, 0);
        sceVu0AddVector(vvv, s_avBB[8], vvv);
        gra3ddbgDrawLine(s_avBB[8], vvv, 0xffffffff);
    }

    if (g_gra3dShadowDebug.bDrawShadowModelBB != 0)
    {
        gra3ddbgDrawBB(s_avBB, 0xffff00ff);
    }

    /* 0x623 */
    G3DASSERT(pSGDTop, "pSGDTop");
    /* 0x624  「影モデルにボーンデータが含まれてない風です」
     *        = "the shadow model doesn't seem to contain bone data" */
    G3DASSERT(pSGDTop->pCoord, "影モデルにボーンデータが含まれてない風です");

    if (_IsDraw() != 0)
    {
        _RenderShadow(pSGDTop, pCoord, iObjectIndex);
        _DrawShadow(&camOrigin, -s_Camera.fAspectX, -s_Camera.fAspectY);
        gra3dSetGsRegister(0, SCE_GS_TEX1_1);
    }
}

/* ===================================================================== *
 *  shadow sprite (debug / overlay blit of the shadow texture)
 * ===================================================================== */

void DispShadowSprite()
{
    qword         *base;
    sceVu0IVECTOR  urp;
    sceVu0IVECTOR  dlp;

    base = dmaVif1GetPacketFLUSH_DIRECT();

    /* GIFtag: untextured sprite (clear / fill quad) */
    ((int *)base)[0] = 0x8001;
    ((int *)base)[1] = 0x30034000;
    ((int *)base)[2] = 0x441;
    ((int *)base)[3] = 0;
    base[1][0] = 0;
    base[1][1] = 0x40;
    base[1][2] = 0;
    base[1][3] = 0x80;
    base[2][0] = 0x7ff0;
    base[2][1] = 0x7970;
    base[2][2] = 240000;
    base[2][3] = 0;
    base[3][0] = 0x8810;
    base[3][1] = 0x7d90;
    base[3][2] = 240000;
    base[3][3] = 0;

    /* GIFtag: textured sprite blitting the shadow render target */
    ((int *)(base + 4))[0] = 0x8001;
    ((int *)(base + 4))[1] = 0x702b4000;
    ((int *)(base + 4))[2] = 0x4124126;
    ((int *)(base + 4))[3] = 0;
    *(sceGsTex0 *)(base + 5) = s_RenderTarget.GetGsTex0Ref();
    base[6][0] = 0;
    base[6][1] = 0;
    base[6][2] = 0x3f800000;
    base[6][3] = 0;
    base[7][0] = 0x80;
    base[7][1] = 0x80;
    base[7][2] = 0x80;
    base[7][3] = 0x80;
    base[8][0] = 0x8000;
    base[8][1] = 0x7980;
    base[8][2] = 240000;
    base[8][3] = 0;
    base[9][0] = 0x3f800000;
    base[9][1] = 0x3f800000;
    base[9][2] = 0x3f800000;
    base[9][3] = 0;
    base[10][0] = 0x80;
    base[10][1] = 0x80;
    base[10][2] = 0x80;
    base[10][3] = 0x80;
    base[11][0] = 0x8800;
    base[11][1] = 0x7d80;
    base[11][2] = 240000;
    base[11][3] = 0;

    dmaVif1SetPacketFLUSH_DIRECT(base + 0xc);
}

/* ===================================================================== *
 *  STL-ish template instantiations emitted into this TU
 * ===================================================================== */
#if 0

template <> float *min_element<float *>(float *__first, float *__last)
{
    float *__result;

    if (__first == __last)
    {
        return __first;
    }
    for (__result = __first; ++__first != __last; )
    {
        if (*__first < *__result)
        {
            __result = __first;
        }
    }
    return __result;
}

/* _fixed_array_verifyrange<SGDFILEHEADER *> is the ctl/fixed_array.h template
 * instantiated for the project-model stack's element type. */

template <>
void fill<SGDFILEHEADER **, SGDFILEHEADER *>(SGDFILEHEADER **__first, SGDFILEHEADER **__last,
                                             SGDFILEHEADER *&__value)
{
    for ( ; __first != __last; ++__first)
    {
        *__first = __value;
    }
}

/* ===================================================================== *
 *  static init / dtor (compiler-generated, keyed to g_gra3dShadowDebug)
 * ===================================================================== */

static void __static_initialization_and_destruction_0(int __initialize_p, int __priority)
{
    if (__priority == 0xffff)
    {
        if (__initialize_p == 1)
        {
            s_RenderTarget.CRenderTarget();
            s_stackpProjectModel.null = NULL;
            fill<SGDFILEHEADER **, SGDFILEHEADER *>(
                (SGDFILEHEADER **)&s_stackpProjectModel,
                &s_stackpProjectModel.null, &s_stackpProjectModel.null);
        }
        if (__initialize_p == 0)
        {
            s_RenderTarget.~CRenderTarget();
        }
    }
}

static void global_constructors_keyed_to_g_gra3dShadowDebug()
{
    __static_initialization_and_destruction_0(1, 0xffff);
}

static void global_destructors_keyed_to_g_gra3dShadowDebug()
{
    __static_initialization_and_destruction_0(0, 0xffff);
}
#endif
