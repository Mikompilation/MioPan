/* ==========================================================================
 *  ingame/clear/prg/game_result.h
 *
 *  The game-clear phase (game_result.o, .text 0x1aa6f0) -- the parent of the
 *  result page in game_result_top.o.
 *
 *  GID_GAMERESULT is a *parent* phase: its pre/after callbacks run every frame
 *  while GID_GAMERESULT_TOP is the active phase, so this module owns the
 *  background, the two paks and the black fade that brackets the visit, and
 *  the result page only draws on top of it.
 *
 *  Structurally it is clearmenu.o's twin (and savepoint_main.o's), with two
 *  differences that matter: the background is this file's own six-sprite draw
 *  out of gameclear_tex[] rather than a borrowed one, and the whole screen --
 *  background pak included -- is chosen by ingame_wrk.mDifficulty.
 *
 *  game_result_ctrl.step:
 *      0  entered; nothing done yet
 *      1  waiting on the two paks
 *      2  fading up from black (20 frames)
 *      3  open -- the result page has the screen and takes input
 *      4  fading back down to black, then GID_CLEARMENU_TOP
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_CLEAR_PRG_GAME_RESULT_H
#define _INGAME_CLEAR_PRG_GAME_RESULT_H

/* game_result_ctrl.step */
#define GAME_RESULT_STEP_ENTRY      0
#define GAME_RESULT_STEP_LOAD_WAIT  1
#define GAME_RESULT_STEP_FADE_IN    2
#define GAME_RESULT_STEP_OPEN       3
#define GAME_RESULT_STEP_FADE_OUT   4

/* Note the member order against clearmenu.o's CLEAR_MENU_CTRL, which has
 * step first: here the timer is at 0x0.  There is no stream id -- the clear
 * BGM this phase starts belongs to the clear menu, which is handed the id
 * through SetClearMenuStreamID(). */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  anim_timer;      /* shared by both black fades */
    /* 0x4 */ char step;
} GAME_RESULT_CTRL;

/* Hand the phase over to its closing fade.  game_result_top.c calls this from
 * both of its "player pressed a button" arms.  Unlike clearmenu.o's twin it
 * does not fade the BGM -- the same stream plays on under the clear menu. */
void  GameResultFadeOutReq(void);                   /* 0x1aab40 */

/* The result screen's character/text pak, once the loader has it resident.
 * It is GAMECLEAR_CHARA_PK2 + GetLanguage(); game_result_top.c draws every
 * one of its plates out of it. */
void *GetGameResultCharPk2Addr(void);               /* 0x1aab58 */

#endif /* _INGAME_CLEAR_PRG_GAME_RESULT_H */
