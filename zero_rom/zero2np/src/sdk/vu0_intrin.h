/* ==========================================================================
 *  vu0_intrin.h  (EE VU0 fixed<->float helpers — PC-port shim)
 *
 *  On the EE the VU0 "macro-mode" names (`_lqc2`, `_vmulabc`, `_vmaddbc`, ...)
 *  are inline COP2 instructions emitted by the GCC EE toolchain, not a library.
 *  There is no PS2 toolchain here, so those inline-asm call sites are rewritten
 *  to equivalent scalar C directly in the reconstructed sources (matching the
 *  Fatal Frame 1 PC port's approach).
 *
 *  The only VU0 names kept here are the ones the reconstruction uses in plain
 *  FUNCTION form — the fixed-point conversions `_ftoiN(int *out, float *in)` —
 *  which have a well-defined scalar meaning: out[k] = (int)(in[k] * 2^N), the
 *  4.N fixed formats the GS/VIF expect.
 *
 *  The fundamental quadword vector/matrix types also live here so they are
 *  ambient wherever the sceVu0* library types are referenced.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _VU0_INTRIN_H
#define _VU0_INTRIN_H

/* ---- fundamental vector/matrix types (verbatim from types.txt) --------- */
typedef float sceVu0FVECTOR[4];    /* xyzw */
typedef int   sceVu0IVECTOR[4];
typedef float sceVu0FMATRIX[4][4];

/* ---- VU0 pipeline/status ops (no math; no host equivalent) -------------- */
/* The host has no VU0 coprocessor pipeline, so a NOP is literally nothing and
   the clip/status registers read back "nothing pending".  These are control
   ops, not the arithmetic kernels (those are rewritten to scalar C in place). */
#ifndef _vnop
#define _vnop()      ((void)0)
#endif
#ifndef _vclip
#define _vclip(a, b) ((void)0)          /* clip-flag pipeline: nothing to do  */
#endif
#ifndef _cfc2
#define _cfc2(...)   (0u)               /* clip flags / VPU-STAT: idle/clear   */
#endif

#ifdef __cplusplus

inline int getCopCondition(int, int) { return 0; }

/* ---- fixed<->float conversions (function form: out, in as pointers) ----- */
/* out[k] = (int)(in[k] * 2^N) — the 4.N fixed formats the GS/VIF expect.     */
inline void _ftoi0 (int *o, const float *i){ for (int k=0;k<4;k++) o[k]=(int)(i[k]);          }
inline void _ftoi4 (int *o, const float *i){ for (int k=0;k<4;k++) o[k]=(int)(i[k]*16.0f);    }
inline void _ftoi12(int *o, const float *i){ for (int k=0;k<4;k++) o[k]=(int)(i[k]*4096.0f);  }
inline void _ftoi15(int *o, const float *i){ for (int k=0;k<4;k++) o[k]=(int)(i[k]*32768.0f); }

#endif /* __cplusplus */

#endif /* _VU0_INTRIN_H */
