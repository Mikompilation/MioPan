/* ==========================================================================
 *  ingame/savepoint/savepoint_top.h
 *
 *  The save-point menu itself (savepoint_top.o, .text 0x248138).
 *
 *  Three rows -- save, album, leave -- with a yes/no confirm window over the
 *  first two.  Runs as GID_SAVEPOINT_TOP under GID_SAVEPOINT_MAIN, which owns
 *  the background and the fades; this module owns only the window, the text
 *  pak (which is language-dependent, so it is loaded separately from the
 *  background) and the cursor.
 *
 *  savepoint_top_ctrl.step:
 *      0  entered; reset the display state
 *      1  waiting on the text pak
 *      2  open, taking input
 *      3  chosen -- waiting for the window to close, then act
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_SAVEPOINT_SAVEPOINT_TOP_H
#define _INGAME_SAVEPOINT_SAVEPOINT_TOP_H

#include "eetypes.h"

/* savepoint_top_ctrl.step */
#define SAVEPOINT_TOP_STEP_ENTRY        0
#define SAVEPOINT_TOP_STEP_LOAD_WAIT    1
#define SAVEPOINT_TOP_STEP_MENU         2
#define SAVEPOINT_TOP_STEP_DECIDED      3

/* savepoint_top_ctrl.mode -- which of the two windows has the pad */
#define SAVEPOINT_TOP_MODE_MENU         0
#define SAVEPOINT_TOP_MODE_CONF         1

/* savepoint_top_ctrl.csr -- the three rows, in screen order */
#define SAVEPOINT_TOP_CSR_SAVE          0
#define SAVEPOINT_TOP_CSR_ALBUM         1
#define SAVEPOINT_TOP_CSR_EXIT          2
#define SAVEPOINT_TOP_CSR_NUM           3

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mode;
    /* 0x2 */ char csr;
    /* 0x3 */ char conf_csr;        /* 0 = yes, 1 = no                        */
} SAVEPOINT_TOP_CTRL;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char anim_step;       /* Zero2Anim2D in/out step for the menu   */
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char conf_anim_step;  /* ...and for the confirm window          */
    /* 0x3 */ char conf_anim_timer;
} SAVEPOINT_TOP_DISP;

/* Once per visit, from init_SavePoint_Main(). */
void SavePointTopFirstInit(void);

/* Once per *entry* to GID_SAVEPOINT_TOP -- which includes coming back from
 * the save screen or the album, hence the guard flag inside. */
void SavePointTopInit(void);

/* The language-dependent text pak. */
void SavePointTopBackGroundLoadReq(void);
void SavePointTopMemFree(void);

void SavePointTopMain(void);
void SavePointTopDisp(void);

#endif /* _INGAME_SAVEPOINT_SAVEPOINT_TOP_H */
