/* ==========================================================================
 *  ingame/savepoint/savepoint_main.h
 *
 *  The save-point screen's spine (savepoint_main.o, .text 0x247ae8).
 *
 *  GID_SAVEPOINT_MAIN is a *parent* phase: its pre/after callbacks run every
 *  frame while one of three children -- GID_SAVEPOINT_TOP, _SAVE or _ALBUM --
 *  is the active phase.  So this module owns the background, the two black
 *  fades that bracket the whole visit, and the BGM stream, and the children
 *  only draw on top of it.
 *
 *  savepoint_main_ctrl.step:
 *      0  entered; nothing done yet
 *      1  waiting on the background pak
 *      2  fading up from black (30 frames)
 *      3  open -- the children have the screen
 *      4  fading back down to black, then GID_SAVEPOINT_FADEOUT
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_SAVEPOINT_SAVEPOINT_MAIN_H
#define _INGAME_SAVEPOINT_SAVEPOINT_MAIN_H

#include "eetypes.h"
#include "../../main/phasefunc.h"                   /* GPHASE_ENUM */

/* savepoint_main_ctrl.step */
#define SAVEPOINT_MAIN_STEP_ENTRY       0
#define SAVEPOINT_MAIN_STEP_LOAD_WAIT   1
#define SAVEPOINT_MAIN_STEP_FADE_IN     2
#define SAVEPOINT_MAIN_STEP_OPEN        3
#define SAVEPOINT_MAIN_STEP_FADE_OUT    4

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  stream_id;       /* the menu BGM, held for the whole visit */
    /* 0x4 */ char step;
} SAVEPOINT_MAIN_CTRL;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ int fade_timer;       /* shared by both black fades             */
    /* 0x4 */ int bg_anim_timer;    /* drives all three background alphas     */
    /* 0x8 */ int moyou1_anim_timer;
    /* 0xc */ int moyou2_anim_timer;
} SAVEPOINT_MAIN_DISP;

/* Background pak: claim, load, free.  savepoint_top.c reuses all three for
 * its own (language-dependent) text pak, which is why they take the address
 * of the pointer rather than touching savepoint_bg_tex_addr directly. */
void SavePointMainBackGroundLoadReq(void);
void GetSavePointMainTexMem(void **tex_addr, int data_label);
void SavePointMainTexLoadReq(void *tex_addr, int data_label);
void LiberateSavePointMainTexMem(void **tex_addr);
void SavePointMainMemFree(void);

/* Start the closing fade.  Called by savepoint_top.c when the player leaves;
 * it also fades the BGM out over the same 30 frames. */
void SavePointMainFadeOutReq(void);

#endif /* _INGAME_SAVEPOINT_SAVEPOINT_MAIN_H */
