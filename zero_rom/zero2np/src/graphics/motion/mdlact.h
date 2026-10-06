/* ==========================================================================
 *  graphics/motion/mdlact.h
 *
 *  Motion "action" helpers: the look-at / neck-aim solvers that bend a
 *  character's chest, head and eyes toward a world position on top of whatever
 *  the motion data already produced, plus the interpolation utilities the
 *  motion player leans on.
 *
 *  These were previously declared in motion.h even though the link map puts
 *  every one of them in mdlact.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_MDLACT_H
#define _GRAPHICS_MOTION_MDLACT_H

#include "mdlwork.h"

float motGetRandom(float upper, float lower);
float motLinearSupValue(float moto, float saki, u_char mode, u_int cnt, u_int all_cnt);
int motGetEneNeckRot(float (*trot_m)[4], ANI_CTRL *ani_ctrl, SGDCOORDINATE *cp2, float *e_rot);
int motSetNeckWork(ANI_CTRL *ani_ctrl);
void GetNeckPos(float *pos, void *ani_hndl);
int IsTargetInSight(ANI_CTRL *ani_ctrl, float *pos);
int motLookAtCtrl(ANI_CTRL *ani_ctrl, LOOK_AT_PARAM *param);
void movGetMoveval(float *spd, float *old_spd, ANI_CTRL *ani_ctrl, u_int frame_num, float frame_f);

#endif /* _GRAPHICS_MOTION_MDLACT_H */
