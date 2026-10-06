/* ==========================================================================
 *  ingame/gameover/prg/gameover_menu.h
 *
 *  The game-over menu's spine (gameover_menu.o, .text 0x1ac968).
 *
 *  GID_GAMEOVER_MENU is a *parent* phase, exactly as GID_SAVEPOINT_MAIN is:
 *  its pre/after callbacks run every frame while one of three children --
 *  GID_GAMEOVER_MENU_TOP, _LOAD or _ALBUM -- is the active phase.  So the
 *  background, both black fades and the BGM live here, and the children only
 *  draw on top.
 *
 *  This file and savepoint_main.c are the same source with the names changed:
 *  the two control blocks have identical layouts, the step machines are the
 *  same five states, and every helper sits within two lines of its twin.  The
 *  background is literally the same asset -- SAVEPOINT_BG_PK2 -- drawn by
 *  savepoint_disp.o's own SavePoint_BgDisp().
 *
 *  gameover_menu_ctrl.step:
 *      0  entered; nothing done yet
 *      1  waiting on the background pak
 *      2  fading up from black (30 frames)
 *      3  open -- the children have the screen
 *      4  fading back down to black, then GID_TITLE_TOP
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_H
#define _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_H

#include "eetypes.h"
#include "../../../main/phasefunc.h"                /* GPHASE_ENUM */

/* gameover_menu_ctrl.step */
#define GAMEOVER_MENU_STEP_ENTRY        0
#define GAMEOVER_MENU_STEP_LOAD_WAIT    1
#define GAMEOVER_MENU_STEP_FADE_IN      2
#define GAMEOVER_MENU_STEP_OPEN         3
#define GAMEOVER_MENU_STEP_FADE_OUT     4

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  stream_id;       /* the menu BGM, held for the whole visit */
    /* 0x4 */ char step;
} GAMEOVER_MENU_CTRL;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ int fade_timer;       /* shared by both black fades             */
    /* 0x4 */ int bg_anim_timer;    /* drives all three background alphas     */
    /* 0x8 */ int moyou1_anim_timer;
    /* 0xc */ int moyou2_anim_timer;
} GAMEOVER_MENU_DISP;

/* Background pak: claim, load, free.  gameover_menu_top.c reuses all three
 * for its own (language-dependent) text pak, which is why they take the
 * address of the pointer rather than touching gameover_bg_tex_addr. */
void GetGameOverMenuTexMem(void **tex_addr, int data_label);
void GameOverMenuTexLoadReq(void *tex_addr, int data_label);
void LiberateGameOverMenuTexMem(void **tex_addr);

/* Start the closing fade.  Called by gameover_menu_top.c when the player
 * picks an exit; it also fades the BGM out over the same 30 frames. */
void GameOverMenuFadeOutReq(void);

#endif /* _INGAME_GAMEOVER_PRG_GAMEOVER_MENU_H */
