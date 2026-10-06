/* ==========================================================================
 *  graphics/motion/morph.h
 *
 *  Model morph interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_MORPH_H
#define _GRAPHICS_MOTION_MORPH_H

#include "mdlwork.h"

#ifdef __cplusplus
extern "C" {
#endif

int MorphInit(void);
int MorphSetRate(ANI_CTRL *ani_ctrl, float rate);
float MorphGetRate(ANI_CTRL *ani_ctrl);
int IsMorphEnable(ANI_CTRL *ani_ctrl);
float MorphGetAlpha2(float alpha);
float MorphGetAlpha1(float alpha);
int MorphCheckId1(int id);
int MorphCheckId2(int id);
int MorphSetCtrl(void *ani_hndl, int mdl_no);
int MorphDell(void *ani_hndl);
int MorphRun(ANI_CTRL *ani_ctrl, u_int *mpk_p);
int MorphReset(ANI_CTRL *ani_ctrl, u_int *mpk_p);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_MOTION_MORPH_H */
