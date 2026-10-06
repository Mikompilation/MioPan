/* ==========================================================================
 *  ingame/clear/prg/game_result_top.h
 *
 *  The game-clear result page (game_result_top.o) -- the screen that follows
 *  the ending: difficulty, clear time, score, rank, and then a list of what
 *  the playthrough just unlocked.
 *
 *  Only the three entry points below are exported; everything else in the
 *  object is a file static.  game_result.o drives all three:
 *  init_GameResult_Top() calls Init(), and one_GameResult_Top() calls Main()
 *  and Disp() every frame while the parent phase is past its fade-in.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_CLEAR_PRG_GAME_RESULT_TOP_H
#define _INGAME_CLEAR_PRG_GAME_RESULT_TOP_H

#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* fixed_array<> */

/* The page's own state.  `flg_msg_id` is the list of "you unlocked this"
 * message ids GameResultTopClearFlgSet() builds at Init() time, `flg_msg_num`
 * how many of the seven slots it filled, and `rank` the clear-time grade
 * GameResultTopRankCheck() worked out from the play timer.
 *
 * Seven slots is exactly enough: the most any single difficulty can add is
 * seven (normal, on a first clear with the lens still missing). */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ fixed_array<int, 7> flg_msg_id;
    /* 0x1c */ char                flg_msg_num;
    /* 0x1d */ char                step;
    /* 0x1e */ char                rank;
} GAME_RESULT_TOP_CTRL;

/* The page's animation state.  rank_anim_step / rank_anim_timer are a
 * Zero2Anim2D_InOutAnimCtrl() pair over the whole result block; each unlock
 * message has its own pair; and line_anim_* belong to
 * GameResultTopLineAnimCtrl(), which is this file's own one-way slide rather
 * than a Zero2Anim2D fade. */
typedef struct                      /* 0x12 */
{
    /* 0x00 */ char                 rank_anim_step;
    /* 0x01 */ char                 rank_anim_timer;
    /* 0x02 */ fixed_array<char, 7> flg_anim_step;
    /* 0x09 */ fixed_array<char, 7> flg_anim_timer;
    /* 0x10 */ char                 line_anim_step;
    /* 0x11 */ char                 line_anim_timer;
} GAME_RESULT_TOP_DISP;

void GameResultTopInit(void);       /* 0x1ab1d0 */
int  GameResultTopMain(void);       /* 0x1ab610 */
void GameResultTopDisp(void);       /* 0x1ab980 */

#endif /* _INGAME_CLEAR_PRG_GAME_RESULT_TOP_H */
