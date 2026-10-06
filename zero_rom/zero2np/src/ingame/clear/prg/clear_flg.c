/* ==========================================================================
 *  ingame/clear/prg/clear_flg.c
 *
 *  Clear-flag control (clear_flg.o).  One record of everything the player has
 *  ever finished, kept across playthroughs because it lives in the system file
 *  rather than a save slot: the per-difficulty clear counts, the unlocked
 *  costumes and accessories, which ending movies have been watched, which
 *  difficulties may be selected, and mission mode's two all-clear awards.
 *
 *  Nothing here draws or decides anything.  It is written by
 *  game_result_top.o (one *GameClearExe per difficulty, chosen by
 *  ingame_wrk.mDifficulty), by mission_ctl.o (the two mission Exe), by
 *  ending.o (the two ending Exe) and by soul_list.o (comp_soul_list_flg); it
 *  is read by the setup menu, the gallery, the album and the costume select.
 *
 *  Three of the writers reach *outside* the record and raise camera upgrade
 *  flags in m_plyr_camera.camera_power_up as well -- clearing the game is how
 *  the tempered lenses and the extra camera parts are handed out, and
 *  game_result_top.o's GameResultTopClearFlgSet() tests precisely the bits
 *  raised below to decide whether to announce a camera upgrade.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

// Trailing /* NNN */ comments are the original source line numbers, measured
// from symbols.txt's $LM/SOL records rather than guessed.
//
// A statement whose whole body is one BIT_FLAGS<N> call carries **no
// clear_flg.c $LM at all**: AllDown()/FlgUp()/IsUp() are inline, so the note
// that reaches the object is variable.h's (801 AllDown's store, 825/829/830/
// 833 FlgUp, 852..858 IsUp) and the caller's own line never gets one.  The
// same is true of a statement that only subscripts the fixed_array<char,4>
// (fixed_array.h 124/125).  Those statements are left unannotated except
// where a measured gap forces the number -- a wrong annotation is worse than
// none.  The unpinned gaps, for whoever matches this next:
//
//     ClearFlgCtrlInit                 61 .. ~78   8 flag statements
//     ClearFlg_EasyGameClearExe       162 .. 164   1
//     ClearFlg_NormalGameClearExe     177 .. 186   5  /  188 .. 189   1
//     ClearFlg_HardGameClearExe       202 .. 209   4  /  211 forced   1
//     ClearFlg_NightMareGameClearExe  224 .. ~226  1
//     ClearFlg_MissionAllClearExe     235 .. ~240  2
//     ClearFlg_AllRankS_...ClearExe   267 .. ~270  2
//     ClearFlg_AddClearCnt            319 .. ~325  3
//
// Every function in the file opens its first statement exactly three lines
// below its PROC line, or five where it declares an `int i`, which is what
// places the signature / brace / blank run the annotations assume.

#include "clear_flg.h"

#include "../../item/prg/file.h"                /* FileGet / FILE_TYPE_SCRAP */
#include "../../photo/m_plyr_camera.h"          /* m_plyr_camera             */

#include <string.h>                                                 /* memset */

CLEAR_FLG_CTRL clear_flg_ctrl;                                 /* data 2d8d20 */

/* Boot-time reset, from init_super().  Two things start unlocked: costume 0
 * (the uniform both sisters begin in) and difficulties 0 and 1 (easy and
 * normal), which is why difficulty_flg's word comes out 3 rather than 1.
 *
 * The four AllDown() calls are inferred rather than read off: the FlgUp()s
 * that follow emit no load from clear_flg_ctrl, so GCC must have known each
 * word's value, which it can only do because a store it then deleted as dead
 * had just put a 0 there.  Only ending_movie_flg's survives -- nothing
 * overwrites it -- and it is the one AllDown the disassembly shows outright,
 * at variable.h 801. */
void ClearFlgCtrlInit(void)                                             /* 51 */
{
    int i;

    for (i = 0; i < 4; i++) {                                           /* 56 */
        clear_flg_ctrl.clear_cnt[i] = 0;                                /* 57 */
    }                                                                   /* 58 */
    clear_flg_ctrl.clear_flg = 0;                                       /* 59 */
    clear_flg_ctrl.comp_soul_list_flg = 0;                              /* 60 */

    clear_flg_ctrl.accessory_flg.AllDown();
    clear_flg_ctrl.ending_movie_flg.AllDown();
    clear_flg_ctrl.costume_flg.AllDown();
    clear_flg_ctrl.difficulty_flg.AllDown();

    clear_flg_ctrl.accessory_flg.FlgUp(0);
    clear_flg_ctrl.costume_flg.FlgUp(0);
    clear_flg_ctrl.difficulty_flg.FlgUp(0);
    clear_flg_ctrl.difficulty_flg.FlgUp(1);
}

/* Merge two clear records into a third.  Both arguments come in by value and
 * the result goes out by value; game_data_save.c and system_data_save.c each
 * feed it the card's copy and the running one, so a save never loses a costume
 * or an ending the other playthrough unlocked.
 *
 * Per-difficulty clear counts take the larger of the two; everything else is a
 * straight OR.  Note the two `== 1` tests: a clear_flg holding any other
 * non-zero value does not survive the merge.
 *
 * `result_flg`'s declaration runs BIT_FLAGS's default constructor over all
 * four flag words -- the four variable.h 801 stores at the head of the object
 * -- and line 91 then memsets the whole thing anyway. */
CLEAR_FLG_CTRL ClearFlgMerging(CLEAR_FLG_CTRL buff1, CLEAR_FLG_CTRL buff2)  /* 86 */
{
    CLEAR_FLG_CTRL result_flg;
    int            i;

    memset(&result_flg, 0, sizeof(CLEAR_FLG_CTRL));                     /* 91 */

    for (i = 0; i < 4; i++) {                                           /* 94 */
        if (buff1.clear_cnt[i] <= buff2.clear_cnt[i]) {                 /* 95 */
            result_flg.clear_cnt[i] = buff2.clear_cnt[i];               /* 96 */
        }                                                               /* 97 */
        else {                                                          /* 98 */
            result_flg.clear_cnt[i] = buff1.clear_cnt[i];               /* 99 */
        }                                                              /* 100 */
    }                                                                  /* 101 */

    if ((buff1.clear_flg == 1) || (buff2.clear_flg == 1)) {            /* 103 */
        result_flg.clear_flg = 1;                                      /* 104 */
    }

    if ((buff1.comp_soul_list_flg == 1) || (buff2.comp_soul_list_flg == 1)) { /* 107 */
        result_flg.comp_soul_list_flg = 1;                             /* 108 */
    }

    for (i = 0; i < 3; i++) {                                          /* 111 */
        if (buff1.accessory_flg.IsUp(i) || buff2.accessory_flg.IsUp(i)) { /* 112 */
            result_flg.accessory_flg.FlgUp(i);                         /* 113 */
        }
    }                                                                  /* 115 */

    for (i = 0; i < 2; i++) {                                          /* 117 */
        if (buff1.ending_movie_flg.IsUp(i) || buff2.ending_movie_flg.IsUp(i)) { /* 118 */
            result_flg.ending_movie_flg.FlgUp(i);                      /* 119 */
        }
    }                                                                  /* 121 */

    for (i = 0; i < 9; i++) {                                          /* 123 */
        if (buff1.costume_flg.IsUp(i) || buff2.costume_flg.IsUp(i)) {  /* 124 */
            result_flg.costume_flg.FlgUp(i);                           /* 125 */
        }
    }                                                                  /* 127 */

    for (i = 0; i < 4; i++) {                                          /* 129 */
        if (buff1.difficulty_flg.IsUp(i) || buff2.difficulty_flg.IsUp(i)) { /* 130 */
            result_flg.difficulty_flg.FlgUp(i);                        /* 131 */
        }
    }                                                                  /* 133 */

    return result_flg;                                                 /* 136 */
}                                                                      /* 137 */

/* Wholesale replacement of the running record.  A plain struct assignment in
 * the ROM -- GCC unrolled it into three unaligned 8-byte pairs, by way of a
 * stack copy of the by-value parameter. */
void SetClearFlgCtrl(CLEAR_FLG_CTRL new_flg_ctrl)                      /* 147 */
{
    clear_flg_ctrl = new_flg_ctrl;                                     /* 150 */
}

/* --------------------------------------------------------------------------
 *  Per-difficulty clear.
 *
 *  Each one chains to the difficulty below it, so a nightmare clear posts
 *  everything hard, normal and easy would have and only its own increment is
 *  written here.  game_result_top.o calls exactly one of the four.
 *
 *  The FileGet()s are scrap files -- the newspaper clippings that turn up in
 *  the notes menu once the game has been finished at that difficulty.
 * ------------------------------------------------------------------------ */

/* Easy: the record's own "cleared at least once" flag, which is what opens
 * mission mode and the gallery, plus the first camera function. */
void ClearFlg_EasyGameClearExe(void)                                   /* 158 */
{
    clear_flg_ctrl.clear_flg = 1;                                      /* 161 */

    m_plyr_camera.camera_power_up.mAdditionFlg.FlgUp(3);

    FileGet(FILE_TYPE_SCRAP, 35);                                      /* 165 */
}

/* Normal: two costumes, an accessory, hard mode, and two camera upgrades --
 * the second of which is raised between the two scrap files rather than beside
 * its siblings.  That is the ROM's own ordering, not a scheduling artefact:
 * the pointer it needs is derived in the first FileGet's delay slot. */
void ClearFlg_NormalGameClearExe(void)                                 /* 173 */
{
    ClearFlg_EasyGameClearExe();                                       /* 176 */

    clear_flg_ctrl.costume_flg.FlgUp(1);
    clear_flg_ctrl.costume_flg.FlgUp(2);

    clear_flg_ctrl.accessory_flg.FlgUp(1);

    clear_flg_ctrl.difficulty_flg.FlgUp(2);

    m_plyr_camera.camera_power_up.mTemperedRenzFlg.FlgUp(4);

    FileGet(FILE_TYPE_SCRAP, 25);                                      /* 187 */

    m_plyr_camera.camera_power_up.mCamPartsFlg.FlgUp(3);

    FileGet(FILE_TYPE_SCRAP, 39);                                      /* 190 */
}

/* Hard: two more costumes, nightmare mode, and two more tempered lenses. */
void ClearFlg_HardGameClearExe(void)                                   /* 198 */
{
    ClearFlg_NormalGameClearExe();                                     /* 201 */

    clear_flg_ctrl.costume_flg.FlgUp(3);
    clear_flg_ctrl.costume_flg.FlgUp(4);

    clear_flg_ctrl.difficulty_flg.FlgUp(3);

    m_plyr_camera.camera_power_up.mTemperedRenzFlg.FlgUp(8);

    FileGet(FILE_TYPE_SCRAP, 29);                                      /* 210 */
    m_plyr_camera.camera_power_up.mTemperedRenzFlg.FlgUp(5);           /* 211 */
    FileGet(FILE_TYPE_SCRAP, 26);                                      /* 212 */
}

/* Nightmare: one last costume, and no scrap file of its own. */
void ClearFlg_NightMareGameClearExe(void)                              /* 220 */
{
    ClearFlg_HardGameClearExe();                                       /* 223 */

    clear_flg_ctrl.costume_flg.FlgUp(8);
}

/* --------------------------------------------------------------------------
 *  Mission mode's two awards.
 *
 *  mission_ctl.o polls the Check pair whenever a mission is scored and runs
 *  the matching Exe once, so each Check answers "has this already been
 *  awarded?".  MisCheckClearAll() is the caller that turns that round into
 *  "there is still an award to give".
 * ------------------------------------------------------------------------ */

/* All 25 missions cleared: one costume and one accessory. */
void ClearFlg_MissionAllClearExe(void)                                 /* 234 */
{
    clear_flg_ctrl.costume_flg.FlgUp(7);

    clear_flg_ctrl.accessory_flg.FlgUp(2);
}

int ClearFlg_CheckMissionAllClear(void)                                /* 249 */
{
    return (clear_flg_ctrl.costume_flg.IsUp(7)
            && clear_flg_ctrl.accessory_flg.IsUp(2));                  /* 257 */
}                                                                      /* 259 */

/* Every mission at rank S.  Chains the plain all-clear award, so reaching all
 * S without ever having tripped the first one still hands both over. */
void ClearFlg_AllRankS_MissionClearExe(void)                           /* 263 */
{
    ClearFlg_MissionAllClearExe();                                     /* 266 */

    clear_flg_ctrl.costume_flg.FlgUp(5);
    clear_flg_ctrl.costume_flg.FlgUp(6);
}

int ClearFlg_CheckAllRankS_MissionClear(void)                          /* 279 */
{
    return (clear_flg_ctrl.costume_flg.IsUp(5)
            && clear_flg_ctrl.costume_flg.IsUp(6));                    /* 288 */
}                                                                      /* 289 */

/* --------------------------------------------------------------------------
 *  Ending movies watched.
 *
 *  ending.o raises these as the movie starts -- init_Ending_Normal1() and
 *  init_Ending_Hard() are the only callers in the ROM -- and the gallery reads
 *  them back to decide which endings may be replayed.
 * ------------------------------------------------------------------------ */

void ClearFlgEndingNormalExe(void)                                     /* 294 */
{
    clear_flg_ctrl.ending_movie_flg.FlgUp(0);
}

void ClearFlgEndingHardExe(void)                                       /* 306 */
{
    clear_flg_ctrl.ending_movie_flg.FlgUp(1);
}

/* Count a finished playthrough at one difficulty, saturating at 99.  This is
 * what makes GameResultTopClearFlgSet()'s `clear_cnt[n] == 0` tests read as
 * "first clear at this difficulty": the count is bumped only after every one
 * of those tests has been taken.
 *
 * The increment loads with `lbu` and the clamp compares with `lb`, so the
 * clamp is the signed test it looks like.  Three separate operator[] calls
 * survive, one per subscript -- GCC CSEd the address but not the bounds
 * check. */
void ClearFlg_AddClearCnt(int difficulty_label)                        /* 318 */
{
    clear_flg_ctrl.clear_cnt[difficulty_label]++;

    if (99 < clear_flg_ctrl.clear_cnt[difficulty_label]) {
        clear_flg_ctrl.clear_cnt[difficulty_label] = 99;
    }
}

/* The clear flags are one of the two blocks in the system file (the other is
 * the option work area), so they survive a new game -- which is what unlocks
 * costumes and the gallery across playthroughs.  save_data.c's
 * save_system_data[] points here; there is no jal to it anywhere in the ROM,
 * only that table's pointer. */
void SetSave_ClearFlg(MC_SAVE_DATA *data)                              /* 338 */
{
    data->addr = (u_char *)&clear_flg_ctrl;                            /* 341 */
    data->size = sizeof(clear_flg_ctrl);                               /* 342 */
}

/* Debug: everything at once, from the ingame menu's debug page.  One clear at
 * each difficulty, the ghost list complete, then the nightmare and all-rank-S
 * chains plus both endings -- which between them raise every flag in the
 * record. */
void DebugAllClearFlgUp(void)                                          /* 354 */
{
    int i;

    for (i = 0; i < 4; i++) {                                          /* 359 */
        clear_flg_ctrl.clear_cnt[i] = 1;                               /* 360 */
    }                                                                  /* 361 */

    clear_flg_ctrl.comp_soul_list_flg = 1;                             /* 363 */
    ClearFlg_NightMareGameClearExe();                                  /* 364 */
    ClearFlg_AllRankS_MissionClearExe();                               /* 365 */
    ClearFlgEndingNormalExe();                                         /* 366 */
    ClearFlgEndingHardExe();                                           /* 367 */
}
