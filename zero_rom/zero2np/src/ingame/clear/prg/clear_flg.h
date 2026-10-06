/* ==========================================================================
 *  ingame/clear/prg/clear_flg.h
 *
 *  Clear-flag control (clear_flg.o).  One global record of everything the
 *  player has finished across playthroughs -- clear count, unlocked costumes
 *  and accessories, which endings have been seen, and which difficulties have
 *  been beaten.
 *
 *  Complete: all 17 ZERO2.MAP .text exports are reconstructed.  The object has
 *  no static data at all -- its .rodata and .sdata hold nothing but the
 *  fixed_array assert literal, the "void*"/"char*"/"unsigned int*" type names
 *  and the nine BIT_FLAGS __FUNCTION__ strings, none of which is referenced
 *  because every FlgUp()/IsUp() in the file is called with a constant.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_CLEAR_PRG_CLEAR_FLG_H
#define _INGAME_CLEAR_PRG_CLEAR_FLG_H

#include "../../../common/save_data.h"                      /* MC_SAVE_DATA */
#include "../../../common/variable.h"                       /* BIT_FLAGS */
#include "../../../graphics/graph3d/ctl/fixed_array.h"

typedef struct                      /* 0x18 */
{
    /* 0x00 */ fixed_array<char, 4> clear_cnt;
    /* 0x04 */ char                 clear_flg;
    /* 0x05 */ char                 comp_soul_list_flg;
    /* 0x08 */ BIT_FLAGS<3>         accessory_flg;
    /* 0x0c */ BIT_FLAGS<2>         ending_movie_flg;
    /* 0x10 */ BIT_FLAGS<9>         costume_flg;
    /* 0x14 */ BIT_FLAGS<4>         difficulty_flg;
} CLEAR_FLG_CTRL;

extern CLEAR_FLG_CTRL clear_flg_ctrl;                       /* data 2d8d20 */

/* Boot-time reset, from init_super().  Leaves costume 0 and difficulties 0/1
 * (easy, normal) raised; everything else down. */
void ClearFlgCtrlInit(void);                                /* 0x12ee80 */

/* Save-block descriptor for the whole CLEAR_FLG_CTRL, referenced from
 * system/mc/dat/save_data.c's save_system_data[]. */
void SetSave_ClearFlg(MC_SAVE_DATA *data);                  /* 0x12f898 */

/* Merge two clear records, taking the larger clear count per difficulty and
 * the OR of every flag.  Both arguments and the result travel by value, as in
 * the ROM.  game_data_save.c and system_data_save.c fold the card's copy into
 * the running one just before they overwrite the card. */
CLEAR_FLG_CTRL ClearFlgMerging(CLEAR_FLG_CTRL buff1, CLEAR_FLG_CTRL buff2); /* 0x12ef00 */

/* Replace the running record wholesale. */
void SetClearFlgCtrl(CLEAR_FLG_CTRL new_flg_ctrl);          /* 0x12f548 */

/* ---- mission mode's two all-clear awards (mission_ctl.o) ---- */

/* Non-zero once the award has been handed out -- mission_ctl.o tests
 * `Check...() == 0` to mean "there is still an award to give". */
int  ClearFlg_CheckMissionAllClear(void);       /* 0x12f768 */
int  ClearFlg_CheckAllRankS_MissionClear(void); /* 0x12f7c8 */

/* All 25 missions cleared: costume 7 and accessory 2.  The all-rank-S award
 * chains it, then adds costumes 5 and 6. */
void ClearFlg_MissionAllClearExe(void);         /* 0x12f740 */
void ClearFlg_AllRankS_MissionClearExe(void);   /* 0x12f798 */

/* ---- per-difficulty clear (game_result_top.o) ---- */

/* Record a finished playthrough at each difficulty: the costumes, endings and
 * accessories that clear unlocks, the difficulty_flg bit itself, the scrap
 * files it hands over, and the camera upgrade flags in
 * m_plyr_camera.camera_power_up.  game_result_top.c calls exactly one of the
 * four, chosen by ingame_wrk.mDifficulty; each chains the one below it. */
void ClearFlg_EasyGameClearExe(void);           /* 0x12f5c0 */
void ClearFlg_NormalGameClearExe(void);         /* 0x12f600 */
void ClearFlg_HardGameClearExe(void);           /* 0x12f690 */
void ClearFlg_NightMareGameClearExe(void);      /* 0x12f710 */

/* Bump clear_cnt[difficulty_label], saturating at 99.  That count is what
 * GameResultTopClearFlgSet() tests to decide whether a clear is the first at
 * its difficulty, so it is bumped only after all of those tests. */
void ClearFlg_AddClearCnt(int difficulty_label); /* 0x12f820 */

/* ---- ending movies watched (ending.o) ---- */

/* Raised as the ending movie starts, by init_Ending_Normal1() and
 * init_Ending_Hard(); the gallery reads them back. */
void ClearFlgEndingNormalExe(void);             /* 0x12f7f0 */
void ClearFlgEndingHardExe(void);               /* 0x12f808 */

/* ---- debug (menu_top.o) ---- */

/* Debug switch: raise every clear flag at once. */
void DebugAllClearFlgUp(void);                  /* 0x12f8b0 */

#endif /* _INGAME_CLEAR_PRG_CLEAR_FLG_H */
