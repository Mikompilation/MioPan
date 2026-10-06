/* ==========================================================================
 *  ingame/gameover/prg/gameover_menu_load.h
 *
 *  The game-over menu's load screen (gameover_menu_load.o, .text 0x1acfd0) --
 *  the folder's largest translation unit.
 *
 *  It is outgame/loadgame.c with a wrapper around it: the same fifteen-step
 *  memory-card machine, the same five slots, the same delegation of all
 *  drawing to save_load_disp.c.  What differs is the frame around it (a
 *  three-step screen state of its own, so the load screen can animate in and
 *  out of the game-over menu rather than being a phase in its own right), the
 *  fact that it loads its own copy of OUTGAME_PK2 instead of borrowing the
 *  title screen's, and where a successful load goes: back into the game via
 *  GID_STORY_LOAD_MISSION_SAVE, or to the setup menu on a cleared file.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_LOAD_H
#define _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_LOAD_H

#include "eetypes.h"

/* The screen's own state, wrapping the card machine below. */
#define GAMEOVER_LOAD_STEP_DATA_LOAD_WAIT   0
#define GAMEOVER_LOAD_STEP_MC               1
#define GAMEOVER_LOAD_STEP_RETURN_MENU      2

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char      step;
    /* 0x1 */ char      mc_step;
    /* 0x2 */ short int wait_timer;     /* zeroed once and never read again */
    /* 0x4 */ char      csr;
    /* 0x5 */ char      conf_csr;
    /* 0x6 */ char      csr_timer;      /* the disp block owns the live one  */
} GAMEOVER_LOAD_CTRL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char csr_timer;
    /* 0x4 */ int  msg_id;
} GAMEOVER_LOAD_DISP;

/* Claimed and requested by init_GameOver_Menu(), well before the phase is
 * ever entered -- both paks have to be resident before the screen opens. */
void GetGameOverLoadTexMem(void);
void GameOverLoadDataLoadReq(void);
int  GameOverLoadDataLoadWait(void);
void ReleaseGameOverLoadTexMem(void);

void GameOverLoadInit(void);
void GameOverLoadMain(void);
void GameOverLoadDispMain(void);
void GameOverLoadEnd(void);

#endif /* _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_LOAD_H */
