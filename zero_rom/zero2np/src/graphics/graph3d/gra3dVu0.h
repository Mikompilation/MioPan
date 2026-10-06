/* ==========================================================================
 *  gra3dVu0.h
 *
 *  VU0 (macro-mode) math helpers for the gra3d renderer.  Every routine here
 *  is `inline` and is expanded directly into its call sites (gra3dSGD.c,
 *  gra3d.c, gra3dShadow.c), which is why gra3dVu0.c emits no machine code of
 *  its own beyond the ctl/fixed_array<> template bodies.  The bodies were
 *  recovered from the "inlined from gra3dVu0.h" sections the decompiler
 *  attributed to this header in those callers.
 *
 *  The vector/matrix loads/stores and per-component multiply-accumulates use
 *  the EE VU0 macro-mode intrinsics (_lqc2/_sqc2/_vmulabc/_vmaddabc/_vmaddbc/
 *  _vclip/_cfc2/...), matching the idiom used throughout the rest of graph3d.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DVU0_H
#define _GRA3DVU0_H

#include "eetypes.h"            /* u_long128 */
#include <libvu0.h>             /* sceVu0FVECTOR, sceVu0FMATRIX */
#include "g3dMath.h"            /* LMATRIX */
#include "g3dxVu0.h"            /* VU0 macro-mode intrinsics (_lqc2 / _sqc2 / ...) */

/* --------------------------------------------------------------------------
 *  gra3dVu0ClearMatrix
 *
 *  Reset a 3-row light matrix to the identity-ish "zero" rows: each row is
 *  vf0 == (0,0,0,1) stored straight from the register file (the decompiler
 *  shows three back-to-back _sqc2(in_vf0) into lm[0..2]).
 * ------------------------------------------------------------------------ */
static inline void gra3dVu0ClearMatrix(LMATRIX lm)
{
    /* each row <- vf0 == (0,0,0,1) */
    int r;
    for (r = 0; r < 3; r++)
    {
        lm[r][0] = 0.0f; lm[r][1] = 0.0f; lm[r][2] = 0.0f; lm[r][3] = 1.0f;
    }
}

/* --------------------------------------------------------------------------
 *  gra3dVu0CopyLMatrix
 *
 *  Copy a 3-row light matrix (12 floats) component-by-component.  Emitted as
 *  twelve scalar float moves -- this is the `mat == NULL` branch of the
 *  directional-light setup.
 * ------------------------------------------------------------------------ */
static inline void gra3dVu0CopyLMatrix(LMATRIX lmDest, LMATRIX lmSrc)
{
    lmDest[0][0] = lmSrc[0][0];
    lmDest[0][1] = lmSrc[0][1];
    lmDest[0][2] = lmSrc[0][2];
    lmDest[0][3] = lmSrc[0][3];
    lmDest[1][0] = lmSrc[1][0];
    lmDest[1][1] = lmSrc[1][1];
    lmDest[1][2] = lmSrc[1][2];
    lmDest[1][3] = lmSrc[1][3];
    lmDest[2][0] = lmSrc[2][0];
    lmDest[2][1] = lmSrc[2][1];
    lmDest[2][2] = lmSrc[2][2];
    lmDest[2][3] = lmSrc[2][3];
}

/* --------------------------------------------------------------------------
 *  gra3dVu0ApplyMatrixToLMatrix
 *
 *  Rotate the three rows of a light matrix by the upper-left 3x3 of mat:
 *  lmDest[r] = lmSrc.col0 * mat[r].x + lmSrc.col1 * mat[r].y
 *            + lmSrc.col2 * mat[r].z   (no translation column).
 *  The three lmSrc rows are pre-loaded into vf and broadcast-multiplied by
 *  each mat row (vmulabc/vmaddabc/vmaddbc on .x/.y/.z).
 * ------------------------------------------------------------------------ */
static inline void gra3dVu0ApplyMatrixToLMatrix(LMATRIX lmDest, LMATRIX lmSrc, float (*mat)[4])
{
    float s0[4], s1[4], s2[4];       /* snapshot lmSrc (lmDest may alias lmSrc) */
    int   i, c;

    for (c = 0; c < 4; c++) { s0[c] = lmSrc[0][c]; s1[c] = lmSrc[1][c]; s2[c] = lmSrc[2][c]; }

    /* lmDest[i] = lmSrc[0]*mat[i].x + lmSrc[1]*mat[i].y + lmSrc[2]*mat[i].z */
    for (i = 0; i < 3; i++)
        for (c = 0; c < 4; c++)
            lmDest[i][c] = s0[c]*mat[i][0] + s1[c]*mat[i][1] + s2[c]*mat[i][2];
}

/* --------------------------------------------------------------------------
 *  gra3dVu0BlendVectorWeighted
 *
 *  Skin one vertex+normal pair across two bone matrices.  pVertex is two
 *  quadwords -- the same point expressed in bone 0's and bone 1's space, with
 *  bone 0's blend weight in its .w -- and pNormal likewise.  Each is
 *  transformed by its own bone matrix (the vertex picks up the translation
 *  row at full weight via vmaddw vf0.w, the normal does not) and the pair is
 *  then blended by w / (1 - w).
 *
 *  Recovered instruction-for-instruction from the inline in SetVUVNDataPost
 *  (0x001b7d60..0x001b7de4, gra3dVu0.h lines 535-563).  Two hand-scheduled
 *  details are load-bearing and deliberately reproduced:
 *
 *    - the normal's first weight is the *vertex's* w.  vmove.w put it in
 *      vf13.w during the vertex stage and the normal stage's vmaddz.xyz only
 *      writes xyz, so the register still carries it at vmulaw.  Its second
 *      weight really does come from the normal's own w (vsubw.w vf14).
 *    - the blended normal is scaled by mat0[3][3] (vmulw.xyz ... vf7), which
 *      is 1.0 for an ordinary local->world matrix.  Kept so a caller that
 *      parks a scale there behaves as the ROM does.
 *
 *  pDest : DVECTOR (two quadwords: blended vertex, blended normal)
 *  pVertex: two weighted vertex qwords (one per bone)
 *  pNormal: two weighted normal qwords (one per bone)
 * ------------------------------------------------------------------------ */
static inline void gra3dVu0BlendVectorWeighted(void *pDest,
                                               const void *pVertex,
                                               const void *pNormal,
                                               float (*mat0)[4], float (*mat1)[4])
{
    const float *pVtx   = (const float *)pVertex;
    const float *pNrm   = (const float *)pNormal;
    const float *vtx0   = pVtx + 0;        /* 0x00: vertex, bone 0 */
    const float *vtx1   = pVtx + 4;        /* 0x10: vertex, bone 1 */
    const float *nrm0   = pNrm + 0;        /* 0x00: normal, bone 0 */
    const float *nrm1   = pNrm + 4;        /* 0x10: normal, bone 1 */
    float       *dVtx   = (float *)pDest + 0;   /* 0x00: blended vertex */
    float       *dNrm   = (float *)pDest + 4;   /* 0x10: blended normal */
    const float  wVtx   = vtx0[3];              /* vmove.w  vf13, vf12 */
    const float  wVtxI  = 1.0f - vtx0[3];       /* vsubw.w  vf14, vf0, vf12 */
    const float  wNrmI  = 1.0f - nrm0[3];       /* vsubw.w  vf14, vf0, vf12 */
    int          c;

    /* vertex: (mat0 * vtx0 + mat0[3]) * w + (mat1 * vtx1 + mat1[3]) * (1 - w) */
    for (c = 0; c < 3; c++)
        dVtx[c] = (vtx0[0]*mat0[0][c] + vtx0[1]*mat0[1][c] + vtx0[2]*mat0[2][c] + mat0[3][c]) * wVtx
                + (vtx1[0]*mat1[0][c] + vtx1[1]*mat1[1][c] + vtx1[2]*mat1[2][c] + mat1[3][c]) * wVtxI;
    dVtx[3] = 1.0f;                                              /* vmove.w vf15, vf0 */

    /* normal: rotation only, then the mat0[3][3] rescale */
    for (c = 0; c < 3; c++)
        dNrm[c] = ((nrm0[0]*mat0[0][c] + nrm0[1]*mat0[1][c] + nrm0[2]*mat0[2][c]) * wVtx
                +  (nrm1[0]*mat1[0][c] + nrm1[1]*mat1[1][c] + nrm1[2]*mat1[2][c]) * wNrmI) * mat0[3][3];
    dNrm[3] = 1.0f;                                              /* vmove.w vf12, vf0 */
}

/* --------------------------------------------------------------------------
 *  gra3dVu0ClipFlags
 *
 *  One VU0 CLIP instruction (`vclipw.xyz vf, vf`): test a clip-space quadword's
 *  x/y/z against +-w and return the six-bit "outside" mask
 *
 *      0x01 x > +w     0x04 y > +w     0x10 z > +w
 *      0x02 x < -w     0x08 y < -w     0x20 z < -w
 *
 *  A point behind the eye has w < 0, which sets *both* bits of an axis.  That
 *  is the hardware's own behaviour and it is what lets the AND-across-corners
 *  reduction in gra3dbbIsInViewvolume reject a box entirely behind the camera.
 *
 *  PORT NOTE.  g3dCalcViewClipMatrixPerspective negates mat[1][1] against the
 *  ROM, so clip Y comes out sign-flipped here: bits 0x04 and 0x08 swap.  Both
 *  reductions built on this (the AND cull below, the OR edge test in
 *  gra3dVu0ClipBB) are invariant under a bit permutation applied uniformly to
 *  every corner, so neither test changes.
 * ------------------------------------------------------------------------ */
static inline u_int gra3dVu0ClipFlags(const float *vClip)
{
    float w = vClip[3];
    u_int uFlag = 0;

    if (vClip[0] >  w) { uFlag |= 0x01; }
    if (vClip[0] < -w) { uFlag |= 0x02; }
    if (vClip[1] >  w) { uFlag |= 0x04; }
    if (vClip[1] < -w) { uFlag |= 0x08; }
    if (vClip[2] >  w) { uFlag |= 0x10; }
    if (vClip[2] < -w) { uFlag |= 0x20; }

    return uFlag;
}

/* --------------------------------------------------------------------------
 *  gra3dVu0ApplyMatrix2
 *
 *  Concatenate two world->clip matrices onto a local->world matrix, producing
 *  the two local->clip matrices the bounding-box test that follows needs:
 *  mat0 = matWorldClipObject (the tight, screen-sized frustum -- drives the
 *  visibility cull), mat1 = matWorldClipPolygon (the guard-banded frustum --
 *  drives the edge check).  Each product is lm * mat in the engine's row-vector
 *  convention, i.e. exactly what sceVu0MulMatrix(dest, mat, lm) computes.
 *
 *  PORT DEVIATION.  The ROM left both products in the VU0 register file
 *  (vf4..vf7 and vf8..vf11) and stored nothing, so gra3dbbIsInViewvolume and
 *  gra3dVu0ClipBB picked them up implicitly.  There is no host register file,
 *  so the two destinations are now explicit parameters and the callee takes the
 *  matrices as arguments.
 * ------------------------------------------------------------------------ */
static inline void gra3dVu0ApplyMatrix2(float (*matDest0)[4], float (*mat0)[4], float (*lm0)[4],
                                        float (*matDest1)[4], float (*mat1)[4], float (*lm1)[4])
{
    sceVu0MulMatrix(matDest0, mat0, lm0);
    sceVu0MulMatrix(matDest1, mat1, lm1);
}

/* --------------------------------------------------------------------------
 *  gra3dVu0ClipBB
 *
 *  Run the four clip-space quadwords at avClip0 and the four at avClip1 through
 *  the VU0 CLIP instruction and return the ORed clip-flag words read back via
 *  CFC2.  A zero (masked 0xffffff) result means no corner falls outside any
 *  plane, i.e. the box is wholly inside the volume.  The two four-row groups
 *  are avWork[8..11] / avWork[12..15] in CheckBoundingBox -- the box's eight
 *  corners in *polygon* clip space, so what this really answers is "does the
 *  model cross the guard band", which is the edge check.
 * ------------------------------------------------------------------------ */
static inline int gra3dVu0ClipBB(float (*avClip0)[4], float (*avClip1)[4])
{
    u_int uFlag0;
    u_int uFlag1;
    int   i;

    /* The VU0 clipping-flag register shifts left six bits per CLIP and retains
       the last four results, so a single CFC2 after four CLIPs yields four
       corners packed into 24 bits.  The caller only tests the word against
       zero, but packing it the way the hardware does keeps the value faithful. */
    uFlag0 = 0;
    uFlag1 = 0;
    for (i = 0; i < 4; i++)
    {
        uFlag0 = (uFlag0 << 6) | gra3dVu0ClipFlags(avClip0[i]);
        uFlag1 = (uFlag1 << 6) | gra3dVu0ClipFlags(avClip1[i]);
    }

    return (int)((uFlag0 | uFlag1) & 0xffffff);
}

/* --------------------------------------------------------------------------
 *  gra3dVu0CalcBBCenter
 *
 *  Compute the bounding-box centre into the VU1 scratch quadword at
 *  0x70003980 from the min corner (0x70003900) and max corner (0x70003970)
 *  already deposited there by gra3dbbApplyMatrix:  c = min*0.5 + max*0.5,
 *  i.e. midpoint, done with a 0.5 broadcast (vmulabc/vmaddbc).  Operates
 *  purely on the fixed VU1-scratch addresses, so it takes no arguments.
 * ------------------------------------------------------------------------ */
static inline void gra3dVu0CalcBBCenter(void)
{
    /* Operated on fixed PS2 VU1-scratchpad addresses (min at 0x70003900, max at
       0x70003970, centre out to 0x70003980).  Those addresses don't exist on the
       host and the centre is only ever consumed by VU1 microcode that never
       runs, so this stays a no-op — unlike the CLIP path above, nothing on the
       host reads the result. */
}

#endif /* _GRA3DVU0_H */
