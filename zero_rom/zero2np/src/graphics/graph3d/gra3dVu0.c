/* ==========================================================================
 *  gra3dVu0.c
 *
 *  VU0 (macro-mode) math helpers for the gra3d renderer: matrix/vector applies,
 *  weighted-vertex blends, bounding-box clip and centre calculations, etc.
 *
 *  Every routine in this translation unit is declared `inline` in gra3dVu0.h
 *  and is expanded directly into its call sites (gra3dSGD.c / gra3dShadow.c /
 *  gra3d.c), so the only machine code GCC 2.96-ee emitted for this object is
 *  the compiler-generated ctl/fixed_array<> bounds-check template, instantiated
 *  here for the void char unsigned int* element types this TU touches:
 *
 *      _fixed_array_assert(char *strType, size_t, size_t)
 *      _fixed_array_verifyrange<void *>(size_t, size_t)
 *      _fixed_array_verifyrange<char *>(size_t, size_t)
 *      _fixed_array_verifyrange<unsigned int *>(size_t, size_t)
 *
 *  Those are not part of the hand-written source -- they are the inlined bodies
 *  from ctl/fixed_array.h -- so they are not reproduced here (see _CONVENTIONS).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dVu0.h"           /* the inline gra3dVu0* VU0 macro-mode helpers */
#include "ctl/fixed_array.h"    /* fixed_array<> (template bodies emitted here) */
