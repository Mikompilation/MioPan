/* ==========================================================================
 *  ingame/gameover/prg/gameover_menu_top.h
 *
 *  The game-over menu's three-row top screen (gameover_menu_top.o,
 *  .text 0x1aeb18).
 *
 *  Continue / Album / Give up, each behind a yes-no confirm window.  The
 *  screen is savepoint_top.o's twin and shares its whole drawing layer --
 *  SavePoint_MenuWinDisp(), SavePointTopCaptionDisp() and
 *  SavePoint_MenuConfWinDisp() are savepoint_disp.o's and savepoint_top.o's
 *  own -- so the only art this file owns is the "GAME OVER" plate in
 *  gameover_dat.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_TOP_H
#define _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_TOP_H

#include "eetypes.h"

/* gameover_menu_top_ctrl.step */
#define GAMEOVER_MENU_TOP_STEP_INIT         0
#define GAMEOVER_MENU_TOP_STEP_LOAD_WAIT    1
#define GAMEOVER_MENU_TOP_STEP_OPEN         2
#define GAMEOVER_MENU_TOP_STEP_DECIDED      3

/* gameover_menu_top_ctrl.mode -- which of the two pad handlers is live */
#define GAMEOVER_MENU_TOP_MODE_MENU         0
#define GAMEOVER_MENU_TOP_MODE_CONF         1

/* gameover_menu_top_ctrl.csr -- the three rows, in draw order */
#define GAMEOVER_MENU_TOP_CSR_LOAD          0
#define GAMEOVER_MENU_TOP_CSR_ALBUM         1
#define GAMEOVER_MENU_TOP_CSR_EXIT          2
#define GAMEOVER_MENU_TOP_CSR_NUM           3

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mode;
    /* 0x2 */ char csr;
    /* 0x3 */ char conf_csr;        /* 0 = yes, 1 = no */
} GAMEOVER_MENU_TOP_CTRL;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ char anim_step;       /* the menu window   */
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char conf_anim_step;  /* the confirm window */
    /* 0x3 */ char conf_anim_timer;
} GAMEOVER_MENU_TOP_DISP;

/* Called once by init_GameOver_Menu(), before the phase is ever entered. */
void GameOverMenuTopFirstInit(void);

/* Called on every entry to GID_GAMEOVER_MENU_TOP. */
void GameOverMenuTopInit(void);

void GameOverMenuTopBackGroundLoadReq(void);
void GameOverMenuTopMemFree(void);

void GameOverMenuTopMain(void);
void GameOverMenuTopDisp(void);

#endif /* _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_TOP_H */
