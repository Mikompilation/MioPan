/* ==========================================================================
 *  ingame/map/MapGeom.h
 *
 *  The geometry helpers the map code carries as header inlines.  Ghidra tags
 *  the expansions "inlined from MapGeom.h", which is where the file name comes
 *  from; the function names are this port's, since an inlined symbol leaves
 *  none behind.
 *
 *  MapObjSetPutMatrix() keeps the name it had while it was a private static in
 *  MapObjReg.c, so that module's six call sites are unchanged.
 *
 *  The header's line map, tallied from every SOL/$LM pair in symbols.txt that
 *  names MapGeom.h -- it is what places each helper and says who uses it:
 *
 *      23, 26-28   MapGeomLoopValue        MapSp.o only
 *      34, 37, 39  MapGeomDegToRad         MapObj.o, MapSp.o
 *      49-52       MapGeomSetScaleMatrix   MapAnim/MapDoor/MapObj/
 *                                          MapObjReg/MapPut/MapSp
 *      58-69       MapObjSetPutMatrix      MapObj/MapObjReg/MapPut/MapSp
 *      76-89       MapDoorSetMatrix        MapDoor.o only
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPGEOM_H
#define _INGAME_MAP_MAPGEOM_H

#include <libvu0.h>

#include "../../common/utility.h"                /* _SetVector */

/* lit4 3ed850..3ed860 in MapObjReg.o -- five copies of the same constant, one
 * per expansion of the matrix builder below. */
#define MAPOBJ_DEG2RAD  0.017453290f

/* Placed models are authored at 1/25 scale. */
#define MAPOBJ_SCALE    25.0f

/* The ROM spells pi as the literal 3.141592 here, in double precision, and
 * emits a separate .rodata copy per branch (0x39ee28 / 0x39ee38).  Not M_PI --
 * keep the shortened value or the wrapped angle drifts. */
#define MAPGEOM_PI      3.141592

/* --------------------------------------------------------------------------
 *  MapGeomLoopValue
 *
 *  Wraps `in` into [min, max) by repeated addition, not by fmod -- so a value
 *  far outside the range costs one iteration per width.  Every caller in the
 *  build passes (0.0f, 360.0f), which folds `w` to 360 and makes both loops
 *  compare against the same literal.
 *
 *  Only MapSp.o expands it.  The parameter names are the ROM's: they survive
 *  in MapSpKazSetMatrix()'s and MapSpMoviProc()'s local lists, which carry
 *  `in` / `min` / `max` / `w` from this expansion.
 * ------------------------------------------------------------------------ */
static inline float MapGeomLoopValue(float in, float min, float max)
{                                                                       /* 23 */
    float w = max - min;                                                /* 24 */

    while (in >= max) { in -= w; }                                      /* 27 */
    while (in <  min) { in += w; }                                      /* 28 */

    return in;                                                          /* 30 */
}

/* --------------------------------------------------------------------------
 *  MapGeomDegToRad
 *
 *  Degrees to radians, wrapping [180, 360) onto [-pi, 0) rather than clamping.
 *  The arithmetic is done in double and narrowed at the end, which is what the
 *  ROM does (the expansion calls libgcc's dpmul / dpdiv / dpsub).
 * ------------------------------------------------------------------------ */
static inline float MapGeomDegToRad(float deg)
{
    if (deg >= 180.0f)                                                  /* 34 */
    {
        return (float)(((double)(deg - 180.0f) * MAPGEOM_PI) / 180.0    /* 37 */
                       - MAPGEOM_PI);
    }

    return (float)(((double)deg * MAPGEOM_PI) / 180.0);                 /* 39 */
}

/* --------------------------------------------------------------------------
 *  MapGeomSetScaleMatrix
 *
 *  Writes the three scale terms onto an already-unit matrix.  It is its own
 *  inline rather than three lines inside MapObjSetPutMatrix(): MapSp.o expands
 *  49-52 on their own, with no 58/59/64-66 around them, which is only possible
 *  if MapSpKazSetMatrix() calls this directly.
 *
 *  The negation and the *25 belong to the callers, not here -- MapSp passes
 *  the three literals ready-made (25.0f, -25.0f, -25.0f).
 * ------------------------------------------------------------------------ */
static inline void MapGeomSetScaleMatrix(float mat[4][4],
                                         float x, float y, float z)
{                                                                       /* 49 */
    mat[0][0] = x;                                                      /* 50 */
    mat[1][1] = y;                                                      /* 51 */
    mat[2][2] = z;                                                      /* 52 */
}

/* --------------------------------------------------------------------------
 *  MapObjSetPutMatrix
 *
 *  The placement matrix: scale on the diagonal with Y and Z negated (the map's
 *  handedness), then Z-Y-X rotation in degrees, then the translation.  X and Y
 *  rotations are negated for the same handedness reason.
 * ------------------------------------------------------------------------ */
static inline void MapObjSetPutMatrix(float mat[4][4], const float *pos,
                                      const float *rot,
                                      float sx, float sy, float sz)
{                                                                       /* 58 */
    sceVu0UnitMatrix(mat);                                              /* 59 */

    MapGeomSetScaleMatrix(mat,  sx * MAPOBJ_SCALE,
                               -sy * MAPOBJ_SCALE,
                               -sz * MAPOBJ_SCALE);

    sceVu0RotMatrixX(mat, mat, -(rot[0] * MAPOBJ_DEG2RAD));             /* 64 */
    sceVu0RotMatrixY(mat, mat, -(rot[1] * MAPOBJ_DEG2RAD));             /* 65 */
    sceVu0RotMatrixZ(mat, mat,  (rot[2] * MAPOBJ_DEG2RAD));             /* 66 */

    /* The ROM copies a whole quadword here; written out component-wise because
     * several call sites pass a bare float[3] (MDAT_OBJ::Pos and friends). */
    mat[3][0] = pos[0];
    mat[3][1] = pos[1];
    mat[3][2] = pos[2];
    mat[3][3] = 1.0f;
}

/* --------------------------------------------------------------------------
 *  MapDoorSetMatrix
 *
 *  The door variant, and genuinely a second inline rather than a wrapper: the
 *  scale is fixed at 1 and the rotations are applied Z-X-Y, not X-Y-Z.  Both
 *  facts are read off the emission order at MapDoorAnimSetPos() and
 *  MapDoorAnimClose(); do not fold the two builders together.
 * ------------------------------------------------------------------------ */
static inline void MapDoorSetMatrix(float mat[4][4], const float *pos,
                                    const float *rot)
{                                                                       /* 75 */
    float vPos[4];

    _SetVector(vPos, pos[0], pos[1], pos[2], 1.0f);                     /* 79 */

    sceVu0UnitMatrix(mat);                                              /* 81 */
    MapGeomSetScaleMatrix(mat, MAPOBJ_SCALE, -MAPOBJ_SCALE,
                               -MAPOBJ_SCALE);

    sceVu0RotMatrixZ(mat, mat,  (rot[2] * MAPOBJ_DEG2RAD));             /* 84 */
    sceVu0RotMatrixX(mat, mat, -(rot[0] * MAPOBJ_DEG2RAD));             /* 85 */
    sceVu0RotMatrixY(mat, mat, -(rot[1] * MAPOBJ_DEG2RAD));             /* 86 */

    sceVu0CopyVector(mat[3], vPos);
    mat[3][3] = 1.0f;
}                                                                       /* 89 */

#endif /* _INGAME_MAP_MAPGEOM_H */
