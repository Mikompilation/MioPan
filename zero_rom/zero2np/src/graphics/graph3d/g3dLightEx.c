/* ==========================================================================
 *  g3dLightEx.c
 *
 *  Extended light services for the zero2np 3D engine.  These operate on the
 *  G3DLIGHT records the game places in the world: emulating any light type as
 *  an equivalent directional light at a given target point, blending several
 *  such emulated lights into one, measuring a light's "power" reaching a
 *  bounding box, testing whether a light reaches a box at all, and sorting the
 *  lights affecting a box by power.
 *
 *  G3D_EMULATE_DIRECTIONALLIGHT_DATA is the intermediate "this light, seen from
 *  the target, looks like a directional light of this colour/direction with
 *  this scalar strength (fLength)" representation everything funnels through.
 *
 *  The inlined PS2 VU0 (macro-mode) blocks are written as the equivalent
 *  SCE EE VU0 / g3dxVu0 calls, matching the other reconstructed graph3d
 *  sources.  Shift-JIS messages are documented with the decoded Japanese in a
 *  comment above the assert/warning.  The per-TU static copies of the
 *  ctl/fixed_array.h helpers (_fixed_array_assert / _fixed_array_verifyrange<>)
 *  are the inlined template -- they are not re-emitted here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dLight.h"
#include "g3dCore.h"           /* g3dGetGlobalStateRef */
#include "g3dGeom.h"          /* g3dCalcPlaneFromPointNormal */
#include "g3dMath.h"
#include "g3dxVu0.h"          /* VU0 macro-mode helpers */
#include "g3dUtil.h"          /* indexof<> */
#include "ctl/fixed_array.h"  /* fixed_array<> (inlined helpers live here) */
#include "g3ddbg.h"
#include <libvu0.h>
#include "gra3dConst.h"       /* g_xv0000 / g_v0000 / g_NullLight */
#include <string.h>           /* memset */
#include <algorithm>          /* std::max_element / std::sort */

/* The per-light-type "is this box lit" dispatch (directional => always lit,
 * so its slot is NULL). */
static int _IsBBLightingupSpot(G3DLIGHT *pLight, float (*avBB)[4]);
static int _IsBBLightingupPoint(G3DLIGHT *pLight, float (*avBB)[4]);

/* --------------------------------------------------------------------------
 *  _IsBBLightingupSpot
 *
 *  True if any of the box's 8 corners lies in front of the spot light's plane
 *  (position + direction normal): if every corner is behind it, the cone can
 *  never reach the box.
 *
 *  PORT DEVIATION -- the ROM tests `< 0.0f` here (0x001a2aa0, the `< 0.0` at
 *  the bottom of the corner loop) and the port tests `> 0.0f`.
 *
 *  `g3dCalcPlaneFromPointNormal()` builds (n, -dot(n, p)), so the value tested
 *  is dot(vDirection, corner - lightPos): positive means the corner is on the
 *  side vDirection points at.  gra3d's spot cone opens ALONG vDirection -- see
 *  the cone-ramp note in g3dCalcVertexColor() -- so a box the beam can reach
 *  is one with a corner at positive distance, and the ROM's test rejects
 *  exactly the boxes it should accept.
 *
 *  This is not cosmetic: _CalcValidLightIndexByType() gates every point and
 *  spot slot binding on this, so with the ROM's sense the player's flashlight
 *  is never bound for any room mesh in front of her and the room draws with no
 *  live light at all.  It is the third site in the same family as
 *  MapLightSetPlayerReal() and MapDrawRoomOne()'s projector: all three were
 *  written against the opposite reading of vDirection, and all three are
 *  deviated from so one convention holds engine-wide.
 * ------------------------------------------------------------------------ */
static int _IsBBLightingupSpot(G3DLIGHT *pLight, float (*avBB)[4])
{
    float  plane[4];
    float *pv;
    float  fAllowableError;
    int    i;

    pv = pLight->vDirection;

    /* SPOTLIGHT::Dir is not normalized, maybe not to call g3dApplyLight(Length:%f) */
    G3DWARNING(g3dxVu0VectorIsNormalized( pLight->vDirection ), "SPOTLIGHT::Dir is not normalized, maybe not to call g3dApplyLight(Length:%f)", (double)g3dxVu0CalcLength(pLight->vDirection));

    g3dCalcPlaneFromPointNormal(plane, pLight->vPosition, pv);

    for (i = 0; i < 8; i++)
    {
        fAllowableError = sceVu0InnerProduct(plane, avBB[0]) + plane[3];
        avBB++;

        if (fAllowableError > 0.0f)
        {
            return 1;
        }
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  _IsBBLightingupPoint
 *
 *  A point light is omnidirectional, so it always potentially lights the box.
 * ------------------------------------------------------------------------ */
static int _IsBBLightingupPoint(G3DLIGHT *pLight, float (*avBB)[4])
{
    return 1;
}

/* --------------------------------------------------------------------------
 *  _ConvertEmulateDirectionallightDataToG3DLIGHT
 *
 *  Materialise an emulated-directional record back into a real (directional)
 *  G3DLIGHT: default the light, copy the diffuse colour, store the normalised
 *  direction.
 * ------------------------------------------------------------------------ */
static void _ConvertEmulateDirectionallightDataToG3DLIGHT(G3DLIGHT *pDest,
                                                          G3D_EMULATE_DIRECTIONALLIGHT_DATA *pEDD)
{
    g3dutilSetLightDefault(pDest, G3DLIGHT_DIRECTIONAL);

    pDest->vDiffuse[0] = pEDD->vDiffuse.x;
    pDest->vDiffuse[1] = pEDD->vDiffuse.y;
    pDest->vDiffuse[2] = pEDD->vDiffuse.z;
    pDest->vDiffuse[3] = pEDD->vDiffuse.w;

    sceVu0Normalize(pDest->vDirection, pEDD->vDirection);

    pDest->Type = G3DLIGHT_DIRECTIONAL;
}

/* --------------------------------------------------------------------------
 *  _BlendEmulatedDirectionalLight (two sources)   [ROM g3dLightEx.c 135-180]
 *
 *  Reads like "fold pSrc1 into pSrc0 by their relative strength".  It is not
 *  what the ROM does.
 *
 *  ROM BUG, REPRODUCED -- and load-bearing, so read this before changing it.
 *  Both interpolations are the same g3dxVu0.h inline (lines 941 / 943), and
 *  that inline takes its two operands from ONE pointer.  The colour pair at
 *  0x1a2d48 / 0x1a2d4c is `d8a40000` / `d8a50000` -- LQC2 with base a1
 *  (pSrc0) in both, differing only in the vf register -- and the direction
 *  pair at 0x1a2d84 / 0x1a2d88 is `d8640000` / `d8650000`, base v1
 *  (= pSrc0 + 0x10).  Those words are read out of SLES_523.84 itself rather
 *  than off the decompiler, because a single register field decides the whole
 *  function.  So what actually executes is
 *
 *      dst.xyz = src.xyz * t + src.xyz * (1 - t)    ==  src.xyz
 *      dst.w   = src.w                                  (vmove.w)
 *
 *  for the colour and for the direction; pSrc1 never reaches either one.  Its
 *  fLength does reach fTotal, but only to be divided straight back out:
 *  fTotal * (pSrc0->fLength / fTotal) == pSrc0->fLength.
 *
 *  The one call site passes pDest == pSrc0, so the whole call is a no-op.  The
 *  fourth and later emulated lights are *discarded*, not folded, and the top
 *  three keep their own direction and colour.  Implementing the blend the
 *  comment describes drags the dominant light's direction toward every weak
 *  light folded into it, which on a character can swing N.L negative over most
 *  of the model -- that is what made characters read as black on the host
 *  while the emulator lit them.
 *
 *  Written as the multiply-add the VU0 issues rather than a plain copy, so the
 *  code says what the hardware does.  (The g3dxVu0.h helper's real signature is
 *  not recoverable: this is its only expansion in the build, and in it both
 *  operands are the same address.)
 * ------------------------------------------------------------------------ */
static void _BlendEmulatedDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pDest,
                                           G3D_EMULATE_DIRECTIONALLIGHT_DATA *pSrc0,
                                           G3D_EMULATE_DIRECTIONALLIGHT_DATA *pSrc1)
{
    float fTotal;
    float fRatioSrc0;
    float t;

    fTotal = pSrc0->fLength + pSrc1->fLength;                          /* 141 */

    if (fTotal <= 0.0f)                                                /* 143 */
    {
        pDest->vDiffuse   = g_xv0000;                                  /* 134-135 */
        pDest->vDirection = g_xv0000;
        return;                                                        /* 180 */
    }

    fRatioSrc0 = pSrc0->fLength / fTotal;                              /* 150 */

    /* g3dxVu0.h 943 -- vmulax.xyz / vmaddx.xyz, vmove.w for the fourth lane */
    t = fRatioSrc0;
    pDest->vDiffuse.x = pSrc0->vDiffuse.x * t + pSrc0->vDiffuse.x * (1.0f - t);
    pDest->vDiffuse.y = pSrc0->vDiffuse.y * t + pSrc0->vDiffuse.y * (1.0f - t);
    pDest->vDiffuse.z = pSrc0->vDiffuse.z * t + pSrc0->vDiffuse.z * (1.0f - t);
    pDest->vDiffuse.w = pSrc0->vDiffuse.w;

    t = 1.0f - fRatioSrc0;                                             /* 941 */
    pDest->vDirection.x = pSrc0->vDirection.x * t + pSrc0->vDirection.x * (1.0f - t);
    pDest->vDirection.y = pSrc0->vDirection.y * t + pSrc0->vDirection.y * (1.0f - t);
    pDest->vDirection.z = pSrc0->vDirection.z * t + pSrc0->vDirection.z * (1.0f - t);
    pDest->vDirection.w = pSrc0->vDirection.w;
    sceVu0Normalize((float *)&pDest->vDirection, (float *)&pDest->vDirection); /* 481 */

    pDest->fLength = fTotal * fRatioSrc0;                              /* 160 */
}

/* --------------------------------------------------------------------------
 *  _BlendEmulatedDirectionalLight (N sources)
 *
 *  Strength-weighted blend of iNumSrc emulated lights: the diffuse is the
 *  weighted sum (clamped to [0,1]), the direction the weighted-sum direction
 *  renormalised, and the strength fTotal * iNumSrc.
 * ------------------------------------------------------------------------ */
static void _BlendEmulatedDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pDest,
                                           G3D_EMULATE_DIRECTIONALLIGHT_DATA *aSrc,
                                           int iNumSrc)
{
    float fTotal;
    float vDestWork[4];
    int   i;

    /* G3D_MAX_BLENDLIGHT >= iNumSrc */
    G3DASSERT(G3D_MAX_BLENDLIGHT >= iNumSrc, "");

    fTotal = 0.0f;
    for (i = 0; i < iNumSrc; i++)
    {
        fTotal = fTotal + aSrc[i].fLength;
    }

    if (0.0f < fTotal)
    {
        int k;

        /* diffuse = clamp( sum( src.diffuse * src.fLength ), 0, 1 ).  The
         * original accumulated this with VU0 macro-mode COP2 asm (the
         * _qmtc2(0x3f800000) constants are the float bits of 1.0f); rewritten
         * here as the equivalent scalar C. */
        memset(vDestWork, 0, sizeof(vDestWork));
        for (i = 0; i < iNumSrc; i++)
        {
            vDestWork[0] = vDestWork[0] + aSrc[i].vDiffuse.x * aSrc[i].fLength;
            vDestWork[1] = vDestWork[1] + aSrc[i].vDiffuse.y * aSrc[i].fLength;
            vDestWork[2] = vDestWork[2] + aSrc[i].vDiffuse.z * aSrc[i].fLength;
            vDestWork[3] = vDestWork[3] + aSrc[i].vDiffuse.w * aSrc[i].fLength;
        }
        {
            /* g3dxVu0.h 820/821 -- vmaxx.xyz then vminibcx.xyz, so only the
             * three colour lanes are clamped; the sq stores the accumulator's
             * own w through untouched. */
            float *pd = (float *)&pDest->vDiffuse;
            for (k = 0; k < 3; k++)
            {
                float f = vDestWork[k];

                if (f < 0.0f)
                {
                    f = 0.0f;
                }

                if (1.0f < f)
                {
                    f = 1.0f;
                }

                pd[k] = f;
            }
            pd[3] = vDestWork[3];
        }

        /* direction = normalize( sum( src.dir * src.fLength ) ) */
        sceVu0CopyVector(vDestWork, g_v0000);

        for (i = 0; i < iNumSrc; i++)
        {
            vDestWork[0] = vDestWork[0] + aSrc[i].vDirection.x * aSrc[i].fLength;
            vDestWork[1] = vDestWork[1] + aSrc[i].vDirection.y * aSrc[i].fLength;
            vDestWork[2] = vDestWork[2] + aSrc[i].vDirection.z * aSrc[i].fLength;
            vDestWork[3] = vDestWork[3] + aSrc[i].vDirection.w * aSrc[i].fLength;
        }

        sceVu0Normalize((float *)&pDest->vDirection, vDestWork);

        pDest->fLength = fTotal * (float)iNumSrc;
    }
    else
    {
        pDest->vDiffuse   = g_xv0000;
        pDest->vDirection = g_xv0000;
        pDest->fLength    = 0.0f;
    }
}

/* --------------------------------------------------------------------------
 *  gra3dSetLightIntens
 *
 *  Store fIntens in the light's spare slot and, for spot lights, map it to a
 *  cone half-angle: 0 -> 90deg, 1 -> 0deg (outer), inner = outer/2.
 * ------------------------------------------------------------------------ */
void gra3dSetLightIntens(G3DLIGHT *pLight, float fIntens)
{
    float fAngle;

    pLight->afPad0[0] = fIntens;

    if (pLight->Type == G3DLIGHT_SPOT)
    {
        if ((fIntens < 0.0f) || (1.0f < fIntens))
        {
            /* インテンスがいけてません(%f)  ("intensity is no good (%f)") */
            G3DWARNING(0.0f <= fIntens && fIntens <= 1.0f, "インテンスがいけてません(%f)", fIntens);
        }

        /* (90 - 90*intens) degrees, in radians */
        fAngle = ((fIntens - 0.0f) * -90.0f + 90.0f) * 0.01745329052209854f;
        pLight->fAngleOutside = fAngle;
        pLight->fAngleInside  = fAngle * 0.5f;
    }
}

/* --------------------------------------------------------------------------
 *  g3dEmulateDirectionalLight
 *
 *  Emulate pLight, as seen from vPos, as a directional light:
 *    directional -> copy direction/colour, strength 1.
 *    point       -> direction = normalize(pos - vPos), strength = distance
 *                   attenuation, diffuse *= strength.
 *    spot        -> as point, additionally * cone falloff.
 * ------------------------------------------------------------------------ */
void g3dEmulateDirectionalLight(G3D_EMULATE_DIRECTIONALLIGHT_DATA *pEDD, G3DLIGHT *pLight,
                                float *vPos)
{
    /* pLight != NULL */
    G3DASSERT(pLight, "");

    switch (pLight->Type)
    {
        case G3DLIGHT_DIRECTIONAL:
        {
            g3dxVu0CopyVector(pEDD->vDirection, pLight->vDirection);
            g3dxVu0CopyVector(pEDD->vDiffuse, pLight->vDiffuse);

            pEDD->fLength = 1.0f;
            return;
        }
        case G3DLIGHT_POINT:
        {
            g3dxVu0NormalizeVector(pEDD->vDirection, pLight->vPosition, vPos);

            /* ::g3dxVu0CalcLength( &pEDD->vDirection ) > 0.0f
             * ("light position and object coords may be the same?") */
            //G3DWARNING(g3dxVu0CalcLength( (const float *)&pEDD->vDirection ) > 0.0f, "光源の位置と、対象物の座標が同じかも？ - light position and object coords may be the same?");

            pEDD->fLength = g3dCalcLightDistanceAttenuation(pLight, vPos);
            sceVu0ScaleVector(pEDD->vDiffuse, pLight->vDiffuse, pEDD->fLength);
            return;
        }
        case G3DLIGHT_SPOT:
        {
            g3dxVu0NormalizeVector(pEDD->vDirection, pLight->vPosition, vPos);

            pEDD->fLength = g3dCalcLightDistanceAttenuation(pLight, vPos);

            /* pLight->fFalloff == 1.0f */
            G3DASSERT(pLight->fFalloff == 1.0f, "");

            pEDD->fLength  = pEDD->fLength * g3dCalcSpotlightFalloff(pLight, vPos);
            sceVu0ScaleVector(pEDD->vDiffuse, pLight->vDiffuse, pEDD->fLength);
            return;
        }
        default:
        {
            break;
        }
    }

    G3DASSERT(0, "");
}

/* --------------------------------------------------------------------------
 *  g3dCalcLightPower
 *
 *  Scalar measure of how strongly pLight illuminates vPos: emulate it as
 *  directional and return strength * |diffuse|.
 * ------------------------------------------------------------------------ */
float g3dCalcLightPower(G3DLIGHT *pLight, float *vPos)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA edd{};

    g3dEmulateDirectionalLight(&edd, pLight, vPos);

    return edd.fLength * g3dxVu0CalcLength(edd.vDiffuse);
}

/* --------------------------------------------------------------------------
 *  gra3dEmulateDirectionalLight
 *
 *  Emulate pLight as a directional light at vPos and write the result into a
 *  real G3DLIGHT (pDest).
 * ------------------------------------------------------------------------ */
void gra3dEmulateDirectionalLight(G3DLIGHT *pDest, G3DLIGHT *pLight, G3DLIGHTTYPE iLightType,
                                  float *vPos)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA edd = {0};

    g3dEmulateDirectionalLight(&edd, pLight, vPos);
    _ConvertEmulateDirectionallightDataToG3DLIGHT(pDest, &edd);
}

/* --------------------------------------------------------------------------
 *  g3dGenerateDirectionalLightByEmulatedData
 *
 *  Reduce iNumEmulated emulated lights down to three directional lights:
 *  keep the first three as the "top" set, then fold each remaining light into
 *  whichever top light its direction best matches (largest cos), and finally
 *  materialise the three into aDest.
 * ------------------------------------------------------------------------ */
void g3dGenerateDirectionalLightByEmulatedData(G3DLIGHT *aDest,
                                               G3D_EMULATE_DIRECTIONALLIGHT_DATA *aSrc,
                                               int iNumEmulated)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA aTop[3];
    float                             afCos[3];
    int                               i;
    int                               j;
    int                               iIndex;
    int                               iIndex2;
    float                            *pfMax;

    /* seed the top set with the first three emulated lights (zero the rest) */
    for (i = G3D_START_LIGHT_DIRECTIONAL; i < G3D_NUM_LIGHT_DIRECTIONAL; i++)
    {
        if (i < iNumEmulated)
        {
            aTop[i] = aSrc[i];
        }
        else
        {
            memset(&aTop[i], 0, sizeof(G3D_EMULATE_DIRECTIONALLIGHT_DATA));
        }
    }

    /* fold the 4th..Nth lights into the best-matching top light.
     *
     * The match is taken against aSrc[j], not aTop[j] -- the ROM walks the
     * source array here (pXVar5 = &aSrc->vDirection, stepped by 0x30) even
     * though aTop is what the blend writes.  They hold the same three lights
     * because _BlendEmulatedDirectionalLight never modifies its destination
     * (see the note on it), so the two spellings agree; aSrc is what the ROM
     * actually reads. */
    for (i = G3D_START_LIGHT_POINT; i < iNumEmulated; i++)
    {
        for (j = 0; j < G3D_NUM_LIGHT_SPOT; j++)
        {
            afCos[j] = sceVu0InnerProduct(aSrc[j].vDirection, aSrc[i].vDirection);
        }

        pfMax = std::max_element(afCos, &afCos[3]);
        if (0.0f < *pfMax)
        {
            iIndex  = (int)(pfMax - afCos);
            iIndex2 = indexof<float>(afCos, pfMax);

            G3DASSERT(iIndex2 == iIndex, "");
            G3DASSERT(iIndex < 3, "");

            _BlendEmulatedDirectionalLight(&aTop[iIndex], &aTop[iIndex], &aSrc[i]);
        }
    }

    /* materialise the three top lights into aDest */
    for (i = G3D_START_LIGHT_DIRECTIONAL; i < NUM_G3DLIGHTTYPE; i++)
    {
        if (i < iNumEmulated)
        {
            _ConvertEmulateDirectionallightDataToG3DLIGHT(&aDest[i], &aTop[i]);
        }
        else
        {
            g3dutilSetLightDefault(&aDest[i], G3DLIGHT_DIRECTIONAL);
            aDest[i].Type = G3DLIGHT_DIRECTIONAL;
        }
    }
}

/* --------------------------------------------------------------------------
 *  g3dIsBBLightingup
 *
 *  Does pLight potentially light the bounding box avBB?  Dispatched by light
 *  type (directional -> always; point -> always; spot -> plane test).
 * ------------------------------------------------------------------------ */
int g3dIsBBLightingup(G3DLIGHT *pLight, float (*avBB)[4])
{
    LPFUNC_ISBBLIGHTINGUP aFunc[3];
    LPFUNC_ISBBLIGHTINGUP pFunc;

    aFunc[0] = (LPFUNC_ISBBLIGHTINGUP)0;     /* directional => always lit */
    aFunc[1] = _IsBBLightingupPoint;
    aFunc[2] = _IsBBLightingupSpot;

    G3DASSERT(pLight->Type != INVALID_G3DLIGHTTYPE, "");
    G3DASSERT((int)pLight->Type < NUM_G3DLIGHTTYPE, "pLight->Type : %d", pLight->Type);

    pFunc = aFunc[pLight->Type];
    if (pFunc != (LPFUNC_ISBBLIGHTINGUP)0)
    {
        return pFunc(pLight, avBB);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dSortLightForBoundingBoxByPowerOrder
 *
 *  Build a LIGHTCOMPAREDATA record per source light (power reaching the box,
 *  0 if it cannot reach it) and sort them by descending power.  All source
 *  lights must be the same type.
 * ------------------------------------------------------------------------ */
void g3dSortLightForBoundingBoxByPowerOrder(int iNum, LIGHTCOMPAREDATA *aCD,
                                            G3DLIGHT **apLightSrc, float (*avBB)[4])
{
    G3DLIGHTTYPE       lt;
    int                i;
    G3DLIGHT          *pL;
    float              colscale;
    float              spower;

    lt = (*apLightSrc)->Type;

    for (i = 0; i < iNum; i++)
    {
        pL = apLightSrc[i];

        /* 別タイプのライトがまじってるじゃん！ ("a different-type light is mixed in!") */
        G3DASSERT(pL->Type == lt, "別タイプのライトがまじってるじゃん！");

        aCD[i].iIndex = i;
        aCD[i].fPower = 0.0f;
        aCD[i].pLight = nullptr;

        if ((pL != nullptr) && (g3dIsBBLightingup(pL, avBB) != 0))
        {
            /* power = sum(diffuse.xyz) * fMaxRange / distance(light, box centre) */
            colscale = pL->vDiffuse[0] + pL->vDiffuse[1] + pL->vDiffuse[2];
            spower   = g3dxVu0Length3(pL->vPosition, avBB[8]);

            aCD[i].pLight = pL;
            aCD[i].fPower = (colscale * pL->fMaxRange) / spower;
        }
    }

    /* std::sort by descending power (LIGHTCOMPAREDATA::greater) */
    std::sort(aCD, aCD + iNum, LIGHTCOMPAREDATA::greater());
}

/* --------------------------------------------------------------------------
 *  g3dBlendLight
 *
 *  Emulate iNumSrc lights at vTarget, blend them into one, and write the
 *  result into pLight as a directional light (defaulted from g_NullLight).
 * ------------------------------------------------------------------------ */
void g3dBlendLight(G3DLIGHT *pLight, G3DLIGHT **apSrc, int iNumSrc, float *vTarget)
{
    G3D_EMULATE_DIRECTIONALLIGHT_DATA EDDDest;
    G3D_EMULATE_DIRECTIONALLIGHT_DATA aEDDSrc[6];
    int                               i;

    /* G3D_MAX_BLENDLIGHT >= iNumSrc */
    G3DASSERT(G3D_MAX_BLENDLIGHT >= iNumSrc, "");

    for (i = G3D_START_LIGHT_DIRECTIONAL; i < iNumSrc; i++)
    {
        if (apSrc[i] == nullptr)
        {
            aEDDSrc[i].vDirection = g_xv0000;
            aEDDSrc[i].vDiffuse   = g_xv0000;
            aEDDSrc[i].fLength    = 0.0f;
        }
        else
        {
            g3dEmulateDirectionalLight(&aEDDSrc[i], apSrc[i], vTarget);
        }
    }

    _BlendEmulatedDirectionalLight(&EDDDest, aEDDSrc, iNumSrc);

    *pLight = g_NullLight;

    pLight->vDiffuse[0] = EDDDest.vDiffuse.x;
    pLight->vDiffuse[1] = EDDDest.vDiffuse.y;
    pLight->vDiffuse[2] = EDDDest.vDiffuse.z;
    pLight->vDiffuse[3] = EDDDest.vDiffuse.w;

    pLight->vDirection[0] = EDDDest.vDirection.x;
    pLight->vDirection[1] = EDDDest.vDirection.y;
    pLight->vDirection[2] = EDDDest.vDirection.z;
    pLight->vDirection[3] = EDDDest.vDirection.w;

    pLight->Type = G3DLIGHT_DIRECTIONAL;
}
