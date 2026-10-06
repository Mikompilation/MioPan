/* ==========================================================================
 *  graphics/motion/anicode.h
 *
 *  ANI_CODE command reader.  Commands are 16-bit words: the high nibble is
 *  the command class and bits 10-11 select one of three argument layouts.
 *
 *  These were previously declared in motion.h even though the link map puts
 *  every one of them in anicode.o.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MOTION_ANICODE_H
#define _GRAPHICS_MOTION_ANICODE_H

#include "mdlwork.h"

void motAniCodeClearBuf(ANI_CTRL *ani_ctrl);
void motAniCodeSetBuf(ANI_CTRL *ani_ctrl, ANI_CODE code);
u_char motAniCodeRead(ANI_CTRL *ani_ctrl);
void motAniCodeExec(ANI_CTRL *ani_ctrl, ANI_CODE code, int *args);
void motAniTimerCodeExec(ANI_CTRL *ani_ctrl);
int motAniCodeIsEnd(ANI_CODE code);
void GetAniCodeArgs(ANI_CODE code, int *args);
int motAniCodeReadCTRL(ANI_CTRL *ani_ctrl, int *args);
void motAniCodeReadTIMER(ANI_CTRL *ani_ctrl, int *args);
void motAniCodeReadMOT(ANI_CTRL *ani_ctrl, int *args);
void motAniCodeReadMIM(ANI_CTRL *ani_ctrl, int *args);
void motAniCodeReadSE(ANI_CTRL *ani_ctrl, int *args);
void motAniCodeReadEFCT(ANI_CTRL *ani_ctrl, int *args);
u_char motGetNextMotion(ANI_CTRL *ani_ctrl);

/* MrecSetSEInfo / MrecGetSeNo and IgEffectPlayerDustReq used to be stubbed
 * here; they belong to map_rectangle.o and ingame_effect.o and now come from
 * map_rectangle.h and ingame_effect.h. */

#endif /* _GRAPHICS_MOTION_ANICODE_H */
