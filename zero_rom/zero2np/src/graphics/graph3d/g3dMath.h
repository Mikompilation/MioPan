/* ==========================================================================
 *  g3dMath.h
 *
 *  Scalar/vector/matrix math for the zero2np 3D engine: the XVECTOR/XMATRIX
 *  C++ value types, the plain float[] aliases used throughout the SGD code
 *  (VECTOR3, LMATRIX), and the g3d* transcendental wrappers implemented in
 *  g3dMath.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DMATH_H
#define _G3DMATH_H

#include "eetypes.h"
#include <libvu0.h>             /* sceVu0FVECTOR / sceVu0FMATRIX */

/* ---- plain-array aliases (verbatim from types.txt) --------------------- */
typedef float VECTOR3[3];       /* packed 3-float vertex/normal */
typedef float LMATRIX[3][4];    /* "light matrix": 3 rows of xyzw */

/* ---- XVECTOR: 16-byte xyzw value type --------------------------------- */
struct XVECTOR                                  /* 0x10 */
{
    float x;                                    /* 0x0 */
    float y;                                    /* 0x4 */
    float z;                                    /* 0x8 */
    float w;                                    /* 0xc */

    operator float *()             { return &x; }
    operator const float *() const { return &x; }
    float &operator[](int i)       { return (&x)[i]; }
};

/* ---- XMATRIX: 4x4 float matrix (row-major) ---------------------------- */
struct XMATRIX                                  /* 0x40 */
{
    union
    {
        struct
        {
            float __11, __12, __13, __14;
            float __21, __22, __23, __24;
            float __31, __32, __33, __34;
            float __41, __42, __43, __44;
        };
        float m[4][4];
    };
};

/* ---- inline clamp (g3dMath.h) ----------------------------------------- *
 * Inlined into g3dAcosf to fold f into [fMin,fMax] before the libm call.   */
inline float g3dClampf(float f, float fMin, float fMax)
{
    if (f < fMin)
    {
        return fMin;
    }
    if (f > fMax)
    {
        return fMax;
    }
    return f;
}

/* ---- transcendental wrappers (g3dMath.c) ------------------------------ */
float g3dSinf(float f);
float g3dCosf(float f);
float g3dAtanf(float f);
float g3dAtan2f(float fX, float fY);
float g3dAcosf(float f);
float g3dLogf2(float fBase, float f);   /* log base fBase of f (float)   */
int   g3dLogi2(int iBase, int i);        /* integer log base iBase of i   */

#endif /* _G3DMATH_H */
