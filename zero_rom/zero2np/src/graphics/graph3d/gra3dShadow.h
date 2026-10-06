/* ==========================================================================
 *  gra3dShadow.h
 *
 *  Public interface for the projected ("map") shadow renderer: register a
 *  caster/receiver and a light, then render the projected shadow onto the
 *  receiver geometry.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRA3DSHADOW_H
#define _GRA3DSHADOW_H

#include "sgd_types.h"
#include "g3dLight.h"           /* G3DLIGHT */
#include "gra3dTypes.h"         /* GRA3DSHADOWDEBUG */

struct GRA3DSHADOWCREATIONDATA;

/* Shadow-subsystem debug switches (defined in gra3dShadow.c).  Declared with
 * C++ linkage to match the definition (which is not extern "C"). */
extern GRA3DSHADOWDEBUG g_gra3dShadowDebug;

/* ---- lifecycle --------------------------------------------------------- */
void gra3dshadowInit(GRA3DSHADOWCREATIONDATA *pCD);

/* ---- registration ------------------------------------------------------ */
void gra3dshadowAddProjectModel(SGDFILEHEADER *pSGDTop);
void gra3dshadowClearProjectModel(void);
void gra3dshadowSetSourceModel(SGDFILEHEADER *pSM);
void gra3dshadowSetAssignGroup(int gnum);
int  gra3dshadowGetAssignGroup(void);

/* ---- shadow parameters ------------------------------------------------- */
void gra3dshadowSetBoundingBox(float avBB[8][4], float mat[4][4]);
void gra3dshadowSetLight(G3DLIGHT *pLight);
float (*gra3dshadowGetTarget(void))[4];

/* ---- draw -------------------------------------------------------------- */
void gra3dshadowDrawSGD(SGDFILEHEADER *pSGDTop, SGDCOORDINATE *pCoord, int iObjectIndex);
void AssignShadowPrim(SGDPROCUNITHEADER *pPUHead);

#endif /* _GRA3DSHADOW_H */
