/* ==========================================================================
 *  gra3dConst.h
 *
 *  Public declarations for the graph3d engine's file-scope constant data
 *  (defined in gra3dConst.c): the named debug colours, the canonical basis /
 *  permutation vectors (g_v1000 .. g_v111_1), the identity / VU / scaled
 *  identity matrices, the screen-image <-> playstation-image conversion
 *  vectors and matrices, the null material / null light, and the default
 *  camera.  Consumers include this header instead of re-declaring the objects
 *  with inline externs.
 *
 *  g_uiMustBeSetValue is the link-time "must be initialised" sentinel the
 *  build's global-constructor list is keyed to.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DCONST_H
#define _GRA3DCONST_H

#include "eetypes.h"
#include "g3dMath.h"            /* XVECTOR / XMATRIX */
#include "gra3dTypes.h"         /* G3DCOLOR / G3DMATERIAL / GRA3DCAMERA */
#include "g3dLight.h"           /* G3DLIGHT */

#ifdef __cplusplus
extern "C" {
#endif

/* Link-time "must be initialised" sentinel (0xdeadbeef). */
extern unsigned int g_uiMustBeSetValue;

/* Named debug colours (G3DCOLOR is packed 0xAABBGGRR). */
extern G3DCOLOR g_colWhite;
extern G3DCOLOR g_colBlack;
extern G3DCOLOR g_colGray;
extern G3DCOLOR g_colRed;
extern G3DCOLOR g_colGreen;
extern G3DCOLOR g_colBlue;
extern G3DCOLOR g_colSkyblue;
extern G3DCOLOR g_colPurple;
extern G3DCOLOR g_colYellow;

/* Canonical xyzw vectors; the four-digit suffix gives the component values
 * (g_v1001 = {1,0,0,1}), g_v111_1 = {1,1,1,-1}. */
extern float g_v1000[4];
extern float g_v0100[4];
extern float g_v0010[4];
extern float g_v1001[4];
extern float g_v0101[4];
extern float g_v0011[4];
extern float g_v0111[4];
extern float g_v1011[4];
extern float g_v1101[4];
extern float g_v0110[4];
extern float g_v1010[4];
extern float g_v1100[4];
extern float g_v0001[4];
extern float g_v1110[4];
extern float g_v1111[4];
extern float g_v0000[4];
extern float g_v111_1[4];

/* Writable zero vector (initialised by the file-scope constructor). */
extern XVECTOR g_xv0000;

/* Identity matrices: g_matUnit is row-major identity, g_VUmatUnit is the
 * VU-form identity, g_matUnitScaled is identity * 25. */
extern float g_matUnit[4][4];
extern float g_VUmatUnit[4][4];
extern float g_matUnitScaled[4][4];

/* Null material / null light. */
extern G3DMATERIAL g_NullMaterial;
extern G3DLIGHT    g_NullLight;

/* Screen-image <-> playstation-image conversion (SI2PS scale 25, PS2SI 0.04;
 * y and z mirrored). */
extern float   g_vConvertSI2PS[4];
extern float   g_vConvertPS2SI[4];
extern float   g_matConvertSI2PS[4][4];
extern float   g_matConvertPS2SI[4][4];
extern XMATRIX g_xmatConvertSI2PS;
extern XMATRIX g_xmatConvertPS2SI;

/* Default camera (~44 degree fov, near 0.1, far 65535, 4:3.5 aspect). */
extern GRA3DCAMERA g_CameraDefault;

#ifdef __cplusplus
}
#endif

#endif /* _GRA3DCONST_H */
