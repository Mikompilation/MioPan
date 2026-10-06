/* ==========================================================================
 *  outgame/setup.h
 *
 *  The setup mode (setup.c): the background and texture loads shared by the
 *  setup menu and every mission screen, and the phase callbacks for all six.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_SETUP_H
#define _OUTGAME_SETUP_H

#include <sys/types.h>              /* u_char */

/* setup.c's two work blocks (sbss 3f4f80 / 3f4f88). */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char step;
    /* 0x4 */ int  stream_id;       /* declared by the ROM, never written    */
} SETUP_CTRL;

typedef struct                      /* 0x2 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
} SETUP_DISP_CTRL;

void SetupInit(void);                                       /* 0x255c20 */
void SetupBackGroundLoadReq(void);                          /* 0x255c28 */
void SetupMain(void);                                       /* 0x255e10 */
void SetupDispMain(void);                                   /* 0x256000 */
void SetupMemFree(void);                                    /* 0x255ee0 */

/* Ask to fade out and go back to the title menu. */
void SetupReturnTitleReq(void);                             /* 0x255eb0 */

/* The three paks this mode owns; the setup menu and the mission screens draw
 * out of them rather than loading their own. */
void *GetSetupBgPk2Addr(void);                              /* 0x255ec8 */
void *GetSetupFontPk2Addr(void);                            /* 0x255ed0 */
void *GetSetupMsnslPk2Addr(void);                           /* 0x255ed8 */

void SetupBlackBgDisp(int off_x, int off_y, u_char alpha);  /* 0x256200 */

#endif /* _OUTGAME_SETUP_H */
