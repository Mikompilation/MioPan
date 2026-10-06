/* ==========================================================================
 *  libvu0.h  (SCE EE VU0 library — PC-port shim)
 *
 *  The fundamental quadword vector/matrix types and the VU0 macro-mode
 *  instruction set live in vu0_intrin.h (force-included everywhere).  This
 *  header declares the higher-level sceVu0* vector-math library the engine
 *  links against; the bodies are implemented in vu0_lib.cpp.
 *
 *  Signatures follow vu0_lib.cpp, which is derived from a matching decomp and
 *  is the authority here -- destination first, scalars last, as in the real
 *  SCE prototypes.  (On the EE these were indistinguishable: a float argument
 *  goes in f12 wherever it sits in the signature, so the order the decompiler
 *  reports for the pointer arguments is the only observable part.)
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _LIBVU0_H
#define _LIBVU0_H

#include "vu0_intrin.h"      /* sceVu0FVECTOR / sceVu0IVECTOR / sceVu0FMATRIX + intrinsics */

#ifdef __cplusplus
extern "C" {
#endif

/* ---- vectors ------------------------------------------------------------ */
void  sceVu0CopyVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1);
void  sceVu0AddVector(sceVu0FVECTOR v0, const sceVu0FVECTOR v1,
                      const sceVu0FVECTOR v2);
void  sceVu0SubVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2);
void  sceVu0MulVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2);
void  sceVu0ScaleVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float s);
/* As sceVu0ScaleVector, but leaves w untouched. */
void  sceVu0ScaleVectorXYZ(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float s);
void  sceVu0DivVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float q);
void  sceVu0DivVectorXYZ(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float q);
void  sceVu0ClampVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, float min, float max);
void  sceVu0Normalize(sceVu0FVECTOR v0, sceVu0FVECTOR v1);
float sceVu0InnerProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1);
void  sceVu0OuterProduct(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2);

/* PS2 weighting: v0 = r*v1 + (1 - r)*v2.  Note r selects v1, not v2. */
void  sceVu0InterVector(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2, float r);
/* Same weighting on xyz; w is copied straight from v1 rather than blended. */
void  sceVu0InterVectorXYZ(sceVu0FVECTOR v0, sceVu0FVECTOR v1, sceVu0FVECTOR v2, float r);

/* ---- matrices ----------------------------------------------------------- */
void  sceVu0UnitMatrix(sceVu0FMATRIX m);
void  sceVu0CopyMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1);
void  sceVu0TransposeMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1);
void  sceVu0InversMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1);
void  sceVu0MulMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1, sceVu0FMATRIX m2);
void  sceVu0ApplyMatrix(sceVu0FVECTOR v0, const sceVu0FMATRIX m,
                        const sceVu0FVECTOR v1);
void  sceVu0CameraMatrix(sceVu0FMATRIX m, sceVu0FVECTOR p, sceVu0FVECTOR zd,
                         sceVu0FVECTOR yd);
/* translate: m0[3] = m1[3] + tv, other rows copied */
void  sceVu0TransMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1, sceVu0FVECTOR tv);
void  sceVu0RotMatrixX(sceVu0FMATRIX m0, sceVu0FMATRIX m1, float rx);
void  sceVu0RotMatrixY(sceVu0FMATRIX m0, sceVu0FMATRIX m1, float ry);
void  sceVu0RotMatrixZ(sceVu0FMATRIX m0, sceVu0FMATRIX m1, float rz);
/* Z, then Y, then X. */
void  sceVu0RotMatrix(sceVu0FMATRIX m0, sceVu0FMATRIX m1, sceVu0FVECTOR rot);

/* ---- transform + perspective ------------------------------------------- */
void  sceVu0RotTransPers(sceVu0IVECTOR v0, sceVu0FMATRIX m0, sceVu0FVECTOR v1, int mode);
void  sceVu0RotTransPersF(sceVu0FVECTOR v0, sceVu0FMATRIX m0, sceVu0FVECTOR v1, int mode);
void  sceVu0RotTransPersN(sceVu0IVECTOR *v0, sceVu0FMATRIX m0, sceVu0FVECTOR *v1,
                          int n, int mode);
void  sceVu0RotTransPersNF(sceVu0FVECTOR *v0, sceVu0FMATRIX m0, sceVu0FVECTOR *v1,
                           int n, int mode);

/* --------------------------------------------------------------------------
 *  Declared but NOT implemented in vu0_lib.cpp.
 *
 *  None of these exist as symbols in the prototype either -- they were inline
 *  COP2 in the original and only became calls in this port, so they are shims
 *  rather than reconstructions.  Everything below currently link-errors.
 *
 *    sceVu0Sqrt          9 call sites; g3dxVu0Sqrt() is the inline equivalent
 *    sceVu0NormalizeVector  gra3dShadow.c; g3dxVu0NormalizeVector() matches it
 *    sceVu0Magnitude     _CalcWeightedLocalWorldMatrix
 *    sceVu0LoadMatrix0/1 skinning: load the two bone matrices for the blend
 *    sceVu0BlendVertex   skinning: dp = blend of the two loaded matrices
 *    sceVu0BlendNormal   as above, normalised and without translation
 *
 *  The skinning four are load-bearing for character rendering.
 * ------------------------------------------------------------------------ */
void  sceVu0LoadMatrix0(sceVu0FMATRIX m0);
void  sceVu0LoadMatrix1(sceVu0FMATRIX m0);
void  sceVu0BlendVertex(sceVu0FVECTOR v0, sceVu0FVECTOR v1);
void  sceVu0BlendNormal(sceVu0FVECTOR v0, sceVu0FVECTOR v1);

#ifdef __cplusplus
}
#endif

#endif /* _LIBVU0_H */
