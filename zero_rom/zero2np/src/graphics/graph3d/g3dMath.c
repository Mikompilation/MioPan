/* ==========================================================================
 *  g3dMath.c
 *
 *  Scalar transcendental wrappers for the 3D engine.  Each trig/log routine
 *  validates its input and result with the debug-assert macros (every value
 *  must be finite) and then defers to the EE libm implementation.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dMath.h"
#include "g3ddbg.h"
#include <mathf.h>              /* sinf/cosf/atanf/atan2f/acosf/logf + isnanf… */

/* --------------------------------------------------------------------------
 *  G3DASSERT_FINITE
 *
 *  The three-way "is this float usable" check the engine stamps around every
 *  math call: reject NaN, reject Inf, require finite.  Emitted inline at each
 *  call site (one G3DASSERT per condition); `nm` selects the "f:%f" vs
 *  "fRet:%f" message used to report the offending value.
 * ------------------------------------------------------------------------ */
#define G3DASSERT_FINITE(v, nm)                                          \
    do {                                                                 \
        G3DASSERT(!isnanf(v), nm ":%f", (double)(v));                    \
        G3DASSERT(!isinff(v), nm ":%f", (double)(v));                    \
        G3DASSERT(finitef(v), nm ":%f", (double)(v));                    \
    } while (0)

float g3dSinf(float f)
{
    float fRet;

    G3DASSERT_FINITE(f, "f");

    fRet = sinf(f);

    G3DASSERT_FINITE(fRet, "fRet");

    return fRet;
}

float g3dCosf(float f)
{
    float fRet;

    G3DASSERT_FINITE(f, "f");

    fRet = cosf(f);

    G3DASSERT_FINITE(fRet, "fRet");

    return fRet;
}

float g3dAtanf(float f)
{
    float fRet;

    G3DASSERT_FINITE(f, "f");

    fRet = atanf(f);

    G3DASSERT_FINITE(fRet, "fRet");

    return fRet;
}

float g3dAtan2f(float fX, float fY)
{
    float fRet;

    fRet = atan2f(fX, fY);

    G3DASSERT_FINITE(fRet, "fRet");

    return fRet;
}

float g3dAcosf(float f)
{
    float fRet;

    /* acosf's domain is [-1,1]; clamp first so rounding noise can't trip it. */
    f = g3dClampf(f, -1.0f, 1.0f);

    G3DASSERT_FINITE(f, "f");

    fRet = acosf(f);

    G3DASSERT_FINITE(fRet, "fRet");

    return fRet;
}

float g3dLogf2(float fBase, float f)
{
    G3DASSERT(fBase > 0.0f, "");
    G3DASSERT(f > 0.0f, "");

    return logf(f) / logf(fBase);
}

int g3dLogi2(int iBase, int i)
{
    int iWork;
    int iRet;

    G3DASSERT(iBase > 1, "");
    G3DASSERT(i >= 0, "");

    iWork = 1;
    iRet  = 0;
    while (true)
    {
        G3DASSERT(iWork <= i, "iBase : %d, i : %d", iBase, i);

        if (iWork == i)
        {
            break;
        }

        iWork = iWork * iBase;
        iRet  = iRet + 1;
    }

    return iRet;
}
