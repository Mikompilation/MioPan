/* ==========================================================================
 *  g3dLight.c
 *
 *  Per-light evaluation helpers for the zero2np 3D engine: defaulting a light
 *  record, the spotlight cone falloff, and the distance / total attenuation a
 *  light applies at a world-space vertex.  These are the CPU-side reference
 *  versions of the lighting the VU1 kernels (g3dCore.c) compute on the GPU.
 *
 *  Shift-JIS messages are documented with the decoded Japanese in a comment
 *  above the assert/warning.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dLight.h"
#include "g3dCore.h"            /* g3dGetGlobalStateRef / G3DGS_LIGHTATTENUATIONTYPE */
#include "g3dGeom.h"           /* g3dCalcAngle */
#include "g3dMath.h"           /* g3dClampf */
#include "g3dxVu0.h"           /* g3dxVu0Length3 */
#include "g3ddbg.h"
#include "gra3dConst.h"        /* g_NullLight */
#include <mathf.h>            /* cosf / powf */

/* --------------------------------------------------------------------------
 *  g3dSetLightStatus
 *
 *  Stub in the prototype: the VU1 light-status word is applied elsewhere.
 * ------------------------------------------------------------------------ */
void g3dSetLightStatus(G3DVU1LIGHTSTATUS *pLS)
{
}

/* --------------------------------------------------------------------------
 *  g3dutilSetLightDefault
 *
 *  Reset pLight to the null-light template, then stamp the requested type.
 * ------------------------------------------------------------------------ */
void g3dutilSetLightDefault(G3DLIGHT *pLight, G3DLIGHTTYPE iLightType)
{
    *pLight = g_NullLight;
    pLight->Type = iLightType;
}

/* --------------------------------------------------------------------------
 *  g3dCalcSpotlightFalloff
 *
 *  Cone falloff a spot light applies at vVertexPosition: 1 inside the inner
 *  cone, 0 outside the outer cone, and a smooth (optionally fFalloff-powered)
 *  ramp between.  Non-spot lights always return 1.
 * ------------------------------------------------------------------------ */
float g3dCalcSpotlightFalloff(G3DLIGHT *pLight, float *vVertexPosition)
{
    float fCosAlpha;
    float fCosPhi;
    float fCosTheta;
    float fRet;
    float min;
    static int bOnce = 1;

    fRet = 1.0f;

    if (pLight->Type == G3DLIGHT_SPOT)
    {
        fRet = 0.0f;

        if (pLight->fAngleInside <= pLight->fAngleOutside)
        {
            /* clamp the outer half-angle into [0, PI/2] (g3dMath.h) */
            min = g3dClampf(pLight->fAngleOutside, 0.0f, 1.5707963);
            pLight->fAngleOutside = min;

            fCosPhi   = cosf(min);
            fCosTheta = cosf(pLight->fAngleInside);

            fCosAlpha = cosf(g3dCalcAngle(pLight->vDirection, pLight->vPosition, vVertexPosition));
            acosf(fCosAlpha);

            fRet = 0.0f;
            if ((fCosPhi <= fCosAlpha) && (fRet = 1.0f, fCosAlpha < fCosTheta))
            {
                if (pLight->fFalloff == 1.0f)
                {
                    fRet = (fCosAlpha - fCosPhi) / (fCosTheta - fCosPhi);
                }
                else
                {
                    /* G3DLIGHT::fFalloff が 1.0f じゃないけど、おｋ？
                     * ("fFalloff isn't 1.0f, but OK?") -- warn once */
                    if (bOnce != 0)
                    {
                        G3DWARNING(0, "GG3DLIGHT::fFalloff が 1.0f じゃないけど、おｋ？");
                        bOnce = 0;
                    }
                    fRet = powf((fCosAlpha - fCosPhi) / (fCosTheta - fCosPhi), pLight->fFalloff);
                }
            }
        }
    }

    return fRet;
}

/* --------------------------------------------------------------------------
 *  g3dCalcLightDistanceAttenuation
 *
 *  Distance attenuation of pLight at vVertexPosition: 1 within fMinRange,
 *  0 beyond fMaxRange, and a linear or quadratic ramp between (selected by the
 *  G3DGS_LIGHTATTENUATIONTYPE global state).  Directional lights never
 *  attenuate.
 * ------------------------------------------------------------------------ */
float g3dCalcLightDistanceAttenuation(G3DLIGHT *pLight, float *vVertexPosition)
{
    float        fScope;
    float        fRelativeDistance1;
    unsigned int uiLightAttenuationType;
    float        f;

    f = 1.0f;

    if (pLight->Type != G3DLIGHT_DIRECTIONAL)
    {
        /* min must not exceed max */
        G3DWARNING(pLight->fMinRange <= pLight->fMaxRange, "");

        fRelativeDistance1 = g3dxVu0Length3(pLight->vPosition, vVertexPosition);

        f = 0.0f;
        if (fRelativeDistance1 <= pLight->fMaxRange)
        {
            f = 1.0f;
            if (pLight->fMinRange < fRelativeDistance1)
            {
                fRelativeDistance1 = fRelativeDistance1 - pLight->fMinRange;
                fScope             = pLight->fMaxRange - pLight->fMinRange;

                uiLightAttenuationType = g3dGetGlobalStateRef(G3DGS_LIGHTATTENUATIONTYPE);
                if (uiLightAttenuationType == 0)
                {
                    f = (fScope - fRelativeDistance1) / fScope;
                }
                else if (uiLightAttenuationType == 1)
                {
                    f = (fScope - fRelativeDistance1) / fScope;
                    f = f * f;
                }
                else
                {
                    G3DASSERT(0, "");
                    f = 0.0f;
                }
            }
        }
    }

    return f;
}

/* --------------------------------------------------------------------------
 *  g3dCalcLightAttenuation
 *
 *  Total attenuation of pLight at vVertex: directional = 1, point = distance
 *  attenuation, spot = distance attenuation * cone falloff.
 * ------------------------------------------------------------------------ */
float g3dCalcLightAttenuation(G3DLIGHT *pLight, float *vVertex)
{
    float fRet;

    if (pLight->Type == G3DLIGHT_POINT)
    {
        return g3dCalcLightDistanceAttenuation(pLight, vVertex);
    }

    if ((int)pLight->Type < G3DLIGHT_SPOT)
    {
        if (pLight->Type == G3DLIGHT_DIRECTIONAL)
        {
            return 1.0f;
        }
    }
    else
    {
        if (pLight->Type == G3DLIGHT_SPOT)
        {
            fRet = g3dCalcLightDistanceAttenuation(pLight, vVertex);
            return fRet * g3dCalcSpotlightFalloff(pLight, vVertex);
        }
        if (pLight->Type == G3DLIGHTTYPE_FORCE_DWORD)
        {
            return 0.0f;
        }
    }

    G3DASSERT(0, "");

    return 1.0f;
}
