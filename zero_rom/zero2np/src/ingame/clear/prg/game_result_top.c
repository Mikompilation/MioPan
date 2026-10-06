// FILE: /home/zero_rom/zero2np/src/ingame/clear/prg/game_result_top.c
//
// The game-clear result page: difficulty, clear time, total score and a
// clear-time rank, then a list of what this playthrough unlocked.
//
// The page runs in two halves and game_result_top_ctrl.step is the join.
// Steps 0/1 hold the result block open until the player presses something.
// If nothing was unlocked that goes straight to the fade-out (step 5);
// otherwise the result block is faded away (step 2), the horizontal rule
// slides down the screen to make room (step 3), and each unlock message is
// armed as the rule passes it -- ten frames apart, keyed off the rule's own
// animation timer rather than a counter of its own.  Step 4 waits for a
// second button press and step 5 is the hand-off to GameResultFadeOutReq().
//
// GameResultTopClearFlgSet() is where the unlock list comes from, and it is
// the only thing in the file with side effects outside the screen: it calls
// the per-difficulty ClearFlg_*GameClearExe() and bumps both the per-
// difficulty clear count and ingame_wrk.mClearCnt.  It runs once, from
// GameResultTopInit().
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
// game_result_top.o.  All three ZERO2.MAP exports plus the thirteen statics.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs with tools/reverse/annot.py.  Three things about them:
//
//   * every statement whose only memory access goes through a fixed_array
//     subscript is attributed to fixed_array.h 124/125 and so leaves no $LM
//     of its own -- those are interpolated into the measured gap;
//   * an instruction the scheduler moved into a branch delay slot loses its
//     line note, which is why GameResultTopDispInit()'s rank_anim_timer store
//     and GameResultTopPad()'s `res = 0` carry none;
//   * GCC 2.96 tags a for-loop's increment and condition with the line of the
//     loop body's *closing brace*, not the `for` -- GameResultTopInit()'s
//     146/148 pair is the clean example, and it is what places every `}` here.
//
// One spot the disassembly cannot settle: the ClearFlg_AddClearCnt() call at
// 292 is the last measured line in its function, so the mClearCnt.Increment()
// after it has no number of its own.
//
// A fourth thing about the markers, settled later against clearmenu_top.o:
// GCC emits a line note whenever the source line changes, *including*
// part-way through one statement's expression.  So two adjacent markers do
// not imply two statements -- GameResultTopLineAnimCtrl()'s 546/547 pair is
// one assignment written over two lines, not a seed and an add.

#include "game_result_top.h"

#include "clear_flg.h"                              /* clear_flg_ctrl         */
#include "game_result.h"                            /* GameResultFadeOutReq   */
#include "../dat/gameclear_dat.h"                   /* gameclear_tex          */
#include "../dat/rank_time_dat.h"                   /* rank_time_tbl          */

#include "../../menu/anim_2d.h"                     /* Anim2D_CalcNowPos      */
#include "../../menu/play_data.h"                   /* GetPlayTime            */
#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_InOut...   */
#include "../../photo/m_plyr_camera.h"              /* m_plyr_camera          */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* ingame_wrk             */
#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnNumberTex       */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT              */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg_Arrange       */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../system/eeiop/snd3d.h"            /* SND_3D_SET             */
#include "../../../system/os/system.h"              /* SystemBankPlay         */
#include "../../../system/pad/pad.h"                /* paddat                 */

/* --------------------------------------------------------------------------
 *  Constants.  The ROM writes all of these as literals; the names are the
 *  port's, chosen the same way savepoint_top.o's were.
 * ------------------------------------------------------------------------ */

/* game_result_top_ctrl.step. */
#define GAME_RESULT_TOP_STEP_INIT       0   /* seed and fall into RESULT      */
#define GAME_RESULT_TOP_STEP_RESULT     1   /* result block up, waiting       */
#define GAME_RESULT_TOP_STEP_RESULT_OUT 2   /* result block fading away       */
#define GAME_RESULT_TOP_STEP_FLG_MSG    3   /* rule sliding, messages arming  */
#define GAME_RESULT_TOP_STEP_FLG_MSG_END 4  /* all messages up, waiting       */
#define GAME_RESULT_TOP_STEP_END        5   /* fade-out requested             */

/* game_result_top_disp.line_anim_step.  0 and 1 are written by
 * GameResultTopLineAnimCtrl() itself and 2 by its own end test; 3 and 4 are
 * handled but nothing in the object ever stores them, so those two arms are
 * dead.  They exist because the field shares its shape with the
 * Zero2Anim2D_InOutAnimCtrl() pairs beside it. */
#define GAME_RESULT_TOP_LINE_STEP_START 0
#define GAME_RESULT_TOP_LINE_STEP_MOVE  1
#define GAME_RESULT_TOP_LINE_STEP_END   2

/* How many unlock messages fit.  Exactly enough for the worst case: a first
 * clear, on normal, with a camera function still to come -- costume,
 * accessory, HARD mode, camera function, Mission Mode, Spirit List and the
 * Gallery line make seven. */
#define GAME_RESULT_TOP_FLG_MSG_MAX     7

/* The result block's fade, in frames -- one Zero2Anim2D pair for all of it. */
#define GAME_RESULT_TOP_RANK_IN_TIME    20
#define GAME_RESULT_TOP_RANK_OUT_TIME   20

/* Each unlock message's own fade. */
#define GAME_RESULT_TOP_FLG_IN_TIME     10
#define GAME_RESULT_TOP_FLG_OUT_TIME    20

/* The rule's resting y, and the message column under it. */
#define GAME_RESULT_TOP_LINE_Y          153.0f
#define GAME_RESULT_TOP_MSG_X           320
#define GAME_RESULT_TOP_MSG_Y           183
#define GAME_RESULT_TOP_MSG_DY          24

/* Message bank the unlock lines and the caption group come out of. */
#define GAME_RESULT_TOP_MSG_TYPE        0x24
#define GAME_RESULT_TOP_CAP_GROUP       7

/* gameclear_tex[] groups this file draws. */
#define GAME_RESULT_TEX_TITLE           12
#define GAME_RESULT_TEX_TITLE_NUM       2
#define GAME_RESULT_TEX_DIFFICULTY      14
#define GAME_RESULT_TEX_TIME            19
#define GAME_RESULT_TEX_TIME_NUM        2
#define GAME_RESULT_TEX_SCORE           21
#define GAME_RESULT_TEX_SCORE_NUM       2
#define GAME_RESULT_TEX_RANK            23
#define GAME_RESULT_TEX_ZERO            31
#define GAME_RESULT_TEX_TIME_SEP        41
#define GAME_RESULT_TEX_TIME_SEP_NUM    2
#define GAME_RESULT_TEX_SCORE_UNIT      43
#define GAME_RESULT_TEX_LINE_L          44
#define GAME_RESULT_TEX_LINE_M          45
#define GAME_RESULT_TEX_LINE_R          46
#define GAME_RESULT_TEX_LINE_R_NUM      2
#define GAME_RESULT_TEX_LINE_M_NUM      5

/* The unlock message ids GameResultTopClearFlgSet() can post.  The ROM writes
 * them as bare numbers; the names below are the port's but the meanings are
 * not guesses -- they are the disc's own English strings, read out of
 * IMG_BD.BIN's language file (CD file 0xd38, bank 0x24) and quoted here.
 * Bank 0x24 also holds the save-point menu's lines at 0..5 and 11..13, and a
 * tenth unlock line at 10 ("Gallery Mode has been added.") that nothing in
 * this file posts. */
#define GAME_RESULT_MSG_COSTUME          6  /* A costume has been added.      */
#define GAME_RESULT_MSG_ACCESSORY        7  /* An accessory has been added.   */
#define GAME_RESULT_MSG_MISSION_MODE     8  /* Mission Mode has been added.   */
#define GAME_RESULT_MSG_SPIRIT_LIST      9  /* Spirit List has been added.    */
#define GAME_RESULT_MSG_HARD_MODE       14  /* HARD Mode has been added.      */
#define GAME_RESULT_MSG_NIGHTMARE_MODE  15  /* NIGHTMARE Mode has been added. */
#define GAME_RESULT_MSG_CAMERA_FUNC     16  /* A new camera function ...      */
#define GAME_RESULT_MSG_GALLERY         17  /* A new feature ... the Gallery.  */

/* --------------------------------------------------------------------------
 *  File state.  Both blocks are .bss and zero at boot; GameResultTopInit()
 *  seeds them.
 * ------------------------------------------------------------------------ */
static GAME_RESULT_TOP_CTRL game_result_top_ctrl;   /* bss 4af688 */
static GAME_RESULT_TOP_DISP game_result_top_disp;   /* bss 4af6a8 */

/* Which gameclear_tex[] record names each difficulty.  Note the order: the
 * table is indexed by ingame_wrk.mDifficulty, and the artwork is not laid out
 * in difficulty order. */
static int difficulty_tex_tbl[4] = { 16, 17, 15, 18 };          /* rdata 3b3be8 */

/* Which gameclear_tex[] record draws each clear-time rank, 0 (best) to 6. */
static int rank_tex_tbl[7] = { 24, 25, 26, 27, 28, 29, 30 };    /* rdata 3b3bf8 */

static char  GameResultTopRankCheck(TIME_INFO play_time);
static void  GameResultTopClearFlgSet(void);
static void  GameResultTopFlgMsgSet(int msg_id);
static int   GameResultTopPad(void);
static void  GameResultTopDispInit(void);
static float GameResultTopLineAnimCtrl(char *anim_step, char *anim_timer);
static void  GameResultTopTitleDisp(int off_x, int off_y, u_char alpha);
static void  GameResultTopDifficultyDisp(int dif_label, int off_x, int off_y,
                                         u_char alpha);
static void  GameResultTopClearTime(TIME_INFO clear_time, int off_x, int off_y,
                                    u_char alpha);
static void  GameResultTopScoreTime(int score, int off_x, int off_y,
                                    u_char alpha);
static void  GameResultTopRankDisp(int rank, int off_x, int off_y,
                                   u_char alpha);
static void  GameResultTopLineDisp(float y, u_char alpha);
static void  GameResultTopCaptionDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Set-up
 * ------------------------------------------------------------------------ */

/* Grade the play time, build the unlock list, and arm the animations.  Called
 * once, from init_GameResult_Top().
 *
 * GetPlayTime() returns a TIME_INFO by value and GameResultTopRankCheck()
 * takes one by value, so the ROM's single stack temporary serves both -- one
 * statement, not two.  See [[struct-return-by-value-looks-like-an-out-param]]. */
void GameResultTopInit(void)                                            /* 138 */
{
    int i;

    game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_INIT;              /* 143 */
    game_result_top_ctrl.rank = GameResultTopRankCheck(GetPlayTime());  /* 144 */

    game_result_top_ctrl.flg_msg_num = 0;                               /* 145 */

    for (i = 0; i < GAME_RESULT_TOP_FLG_MSG_MAX; i++) {                 /* 146 */
        game_result_top_ctrl.flg_msg_id[i] = -1;                        /* 147 */
    }                                                                   /* 148 */

    GameResultTopClearFlgSet();                                         /* 151 */

    GameResultTopDispInit();                                            /* 154 */
}

/* The clear-time grade: the first rank_time_tbl[] row the play time does not
 * exceed, or 6 if it exceeds them all.
 *
 * The three comparisons are spelled out rather than folded: h < row.h wins
 * outright, h == row.h falls through to the minutes, and the seconds test is
 * the only one that is <= rather than <.  rank_time_tbl is an int[7][3] in the
 * ROM, not a TIME_INFO[7], so the subscripts are 0/1/2. */
static char GameResultTopRankCheck(TIME_INFO play_time)                 /* 164 */
{
    int  i;
    char rank;

    rank = 6;                                                           /* 169 */

    for (i = 0; i < 7; i++) {                                           /* 172 */

        if (play_time.hour < rank_time_tbl[i][0]) {                     /* 174 */
            rank = (char)i;                                             /* 175 */
            break;                                                      /* 176 */
        }
        if (play_time.hour == rank_time_tbl[i][0]) {                    /* 178 */

            if (play_time.min < rank_time_tbl[i][1]) {                  /* 180 */
                rank = (char)i;                                         /* 181 */
                break;                                                  /* 182 */
            }
            if (play_time.min == rank_time_tbl[i][1]) {                 /* 184 */

                if (play_time.sec <= rank_time_tbl[i][2]) {             /* 186 */
                    rank = (char)i;                                     /* 187 */
                    break;                                              /* 188 */
                }
            }
        }
    }                                                                   /* 192 */

    return rank;                                                        /* 195 */
}

/* Work out what this playthrough unlocked, tell clear_flg.o about it, and
 * count the clear.
 *
 * The four difficulty arms all have the same shape -- "if this difficulty had
 * never been cleared, post its costume/unlock lines", then "if a camera
 * upgrade is still to come, post the camera-function line", then the
 * per-difficulty ClearFlg_*GameClearExe().  Which flag the camera line is
 * gated on differs per difficulty, and normal is the only one that falls back
 * on a *second* flag (mCamPartsFlg bit 3) when the first is already up.
 *
 * The clear_cnt[] tests are what make this a first-clear check: the count is
 * bumped by ClearFlg_AddClearCnt() at the bottom, after every one of them has
 * been taken. */
static void GameResultTopClearFlgSet(void)                              /* 201 */
{
    if (clear_flg_ctrl.clear_flg == 0) {                                /* 205 */

        GameResultTopFlgMsgSet(GAME_RESULT_MSG_MISSION_MODE);           /* 207 */

        GameResultTopFlgMsgSet(GAME_RESULT_MSG_GALLERY);                /* 211 */
    }

    if (ingame_wrk.mClearCnt.Get() == 0) {                              /* 214 */

        GameResultTopFlgMsgSet(GAME_RESULT_MSG_SPIRIT_LIST);            /* 216 */
    }

    switch (ingame_wrk.mDifficulty.Get()) {                             /* 219 */
    case 0:     /* easy */

        if (m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(3) == 0) {  /* 223 */
            GameResultTopFlgMsgSet(GAME_RESULT_MSG_CAMERA_FUNC);        /* 224 */
        }

        ClearFlg_EasyGameClearExe();                                    /* 227 */
        break;                                                          /* 228 */

    case 1:     /* normal */
        if (clear_flg_ctrl.clear_cnt[1] == 0) {                         /* 231 */
            GameResultTopFlgMsgSet(GAME_RESULT_MSG_COSTUME);            /* 232 */

            GameResultTopFlgMsgSet(GAME_RESULT_MSG_ACCESSORY);          /* 234 */

            GameResultTopFlgMsgSet(GAME_RESULT_MSG_HARD_MODE);          /* 236 */
        }

        if (m_plyr_camera.camera_power_up.mTemperedRenzFlg.IsUp(4) == 0) { /* 239 */

            GameResultTopFlgMsgSet(GAME_RESULT_MSG_CAMERA_FUNC);        /* 242 */
        }
        else {

            if (m_plyr_camera.camera_power_up.mCamPartsFlg.IsUp(3) == 0) { /* 246 */
                GameResultTopFlgMsgSet(GAME_RESULT_MSG_CAMERA_FUNC);    /* 247 */
            }
        }

        ClearFlg_NormalGameClearExe();                                  /* 251 */
        break;                                                          /* 252 */

    case 2:     /* hard */

        if (clear_flg_ctrl.clear_cnt[2] == 0) {                         /* 256 */
            GameResultTopFlgMsgSet(GAME_RESULT_MSG_COSTUME);            /* 257 */

            GameResultTopFlgMsgSet(GAME_RESULT_MSG_NIGHTMARE_MODE);     /* 259 */

            GameResultTopFlgMsgSet(GAME_RESULT_MSG_GALLERY);            /* 261 */
        }

        if (m_plyr_camera.camera_power_up.mTemperedRenzFlg.IsUp(8) == 0) { /* 264 */

            GameResultTopFlgMsgSet(GAME_RESULT_MSG_CAMERA_FUNC);        /* 267 */
        }
        else {

            if (m_plyr_camera.camera_power_up.mTemperedRenzFlg.IsUp(5) == 0) { /* 271 */
                GameResultTopFlgMsgSet(GAME_RESULT_MSG_CAMERA_FUNC);    /* 272 */
            }
        }

        ClearFlg_HardGameClearExe();                                    /* 276 */
        break;                                                          /* 277 */

    case 3:     /* nightmare */
        if (clear_flg_ctrl.clear_cnt[3] == 0) {                         /* 280 */
            GameResultTopFlgMsgSet(GAME_RESULT_MSG_COSTUME);            /* 281 */
        }

        ClearFlg_NightMareGameClearExe();                               /* 285 */
        break;                                                          /* 286 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 288 */
        break;
    }

    ClearFlg_AddClearCnt(ingame_wrk.mDifficulty.Get());                 /* 292 */
    ingame_wrk.mClearCnt.Increment();
}

/* Append `msg_id` to the unlock list, unless it is already there.  Both hard
 * difficulties can post GAME_RESULT_MSG_GALLERY twice, which is what the scan
 * exists for.
 *
 * There is no room check before the append: flg_msg_num is only safe because
 * the callers cannot exceed the seven slots.  An eighth would run into
 * fixed_array's own bounds assert. */
static void GameResultTopFlgMsgSet(int msg_id)                          /* 304 */
{
    int i;

    for (i = 0; i < GAME_RESULT_TOP_FLG_MSG_MAX; i++) {                 /* 309 */

        if (game_result_top_ctrl.flg_msg_id[i] == msg_id) {             /* 311 */
            return;                                                     /* 312 */
        }
    }                                                                   /* 314 */

    game_result_top_ctrl.flg_msg_id[game_result_top_ctrl.flg_msg_num] = msg_id; /* 316 */

    game_result_top_ctrl.flg_msg_num++;                                 /* 318 */
}

/* --------------------------------------------------------------------------
 *  Per-frame
 * ------------------------------------------------------------------------ */

/* The page's state machine.  Always returns 0; one_GameResult_Top() ignores
 * it and returns GPHASE_CONTINUE itself.
 *
 * The interesting arm is step 3.  Nothing counts messages -- the rule's own
 * animation timer does it: every tenth frame from frame 20 on, message
 * (timer / 10 - 2) is armed, so the list unrolls in step with the rule
 * sliding past.  The `flg_msg_start < 0` assert can only fire if that
 * arithmetic is ever reached below frame 20, which the second half of the
 * line-372 test rules out. */
int GameResultTopMain(void)                                             /* 333 */
{
    int flg_msg_start;

    switch (game_result_top_ctrl.step) {                                /* 342 */
    case GAME_RESULT_TOP_STEP_INIT:
        game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_RESULT;        /* 344 */

        /* falls through */
    case GAME_RESULT_TOP_STEP_RESULT:
        if (GameResultTopPad() != 0) {                                  /* 347 */

            if (game_result_top_ctrl.flg_msg_num == 0) {                /* 349 */
                GameResultFadeOutReq();                                 /* 350 */

                game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_END;   /* 352 */
            }
            else {
                game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_RESULT_OUT; /* 355 */

                game_result_top_disp.rank_anim_step  = ZERO2_ANIM2D_STEP_OUT; /* 357 */
                game_result_top_disp.rank_anim_timer = 0;               /* 358 */
            }
        }
        break;                                                          /* 361 */

    case GAME_RESULT_TOP_STEP_RESULT_OUT:
        if (game_result_top_disp.rank_anim_step == ZERO2_ANIM2D_STEP_END) { /* 363 */
            game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_FLG_MSG;   /* 364 */
        }
        break;                                                          /* 366 */

    case GAME_RESULT_TOP_STEP_FLG_MSG:
        if (game_result_top_disp.line_anim_step == GAME_RESULT_TOP_LINE_STEP_END) { /* 368 */
            game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_FLG_MSG_END; /* 369 */
        }
        else {
            if (((game_result_top_disp.line_anim_timer % 10) == 0)
                && (game_result_top_disp.line_anim_timer >= 20)) {      /* 372 */
                flg_msg_start = game_result_top_disp.line_anim_timer / 10 - 2; /* 373 */

                if (flg_msg_start < 0) {                                /* 375 */
                    PRINT_ASSERT("Error! %s", __FUNCTION__);            /* 376 */
                }

                if (flg_msg_start < game_result_top_ctrl.flg_msg_num) { /* 379 */
                    if (game_result_top_ctrl.flg_msg_id[flg_msg_start] != -1) { /* 380 */
                        game_result_top_disp.flg_anim_step[flg_msg_start]  = ZERO2_ANIM2D_STEP_START; /* 381 */
                        game_result_top_disp.flg_anim_timer[flg_msg_start] = 0; /* 382 */
                    }
                }
            }
        }
        break;                                                          /* 387 */

    case GAME_RESULT_TOP_STEP_FLG_MSG_END:
        if (GameResultTopPad() != 0) {                                  /* 389 */
            GameResultFadeOutReq();                                     /* 390 */

            game_result_top_ctrl.step = GAME_RESULT_TOP_STEP_END;       /* 392 */
        }
        break;                                                          /* 394 */

    case GAME_RESULT_TOP_STEP_END:
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 398 */
        break;
    }

    return 0;                                                           /* 402 */
}

/* CROSS or TRIANGLE, each with its own cue.  Returns 1 if either was pressed.
 *
 * `res = 1` sits before the call in the CROSS arm and after it in the
 * TRIANGLE arm; GCC merged the two into the single `li a1,1` after the shared
 * SystemBankPlay() tail, so only the second one's line number survives. */
static int GameResultTopPad(void)                                       /* 410 */
{
    int res;

    res = 0;

    if (*paddat[0] == 1) {                                              /* 418 */
        res = 1;                                                        /* 419 */

        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 421 */
    }
    else {
        if (*paddat[1] == 1) {                                          /* 424 */
            SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 425 */

            res = 1;                                                    /* 427 */
        }
    }

    return res;                                                         /* 431 */
}

/* Arm the animations.  The result block starts closed and fades in; every
 * unlock message starts at STEP_END (fully closed) so that nothing shows
 * until GameResultTopMain()'s step 3 arms it; the rule starts at its own
 * step 0, which GameResultTopLineAnimCtrl() reads as "seed me". */
static void GameResultTopDispInit(void)                                 /* 441 */
{
    int i;

    game_result_top_disp.rank_anim_step  = ZERO2_ANIM2D_STEP_START;     /* 446 */
    game_result_top_disp.rank_anim_timer = 0;                           /* 447 */
    for (i = 0; i < game_result_top_ctrl.flg_msg_num; i++) {            /* 448 */
        game_result_top_disp.flg_anim_step[i]  = ZERO2_ANIM2D_STEP_END; /* 449 */
        game_result_top_disp.flg_anim_timer[i] = 0;                     /* 450 */
    }                                                                   /* 451 */
    game_result_top_disp.line_anim_step  = GAME_RESULT_TOP_LINE_STEP_START; /* 452 */
    game_result_top_disp.line_anim_timer = 0;                           /* 453 */
}

/* Draw the page.
 *
 * The rule is drawn twice: once at its resting y for the result block, and
 * again below the sliding one once the messages are running -- offset by the
 * left cap's own height so the two do not overlap.  msg_alpha is reused for
 * both halves; the ROM has one local, not two.
 *
 * `line_pos` is absent from functions.txt because float locals leave no stab,
 * but it has to exist: lines 508 and 511 carry separate $LM records and GCC
 * emits exactly one per statement, so the slide's result is stored before it
 * is used rather than being one nested call. */
void GameResultTopDisp(void)                                            /* 461 */
{
    u_char msg_alpha;
    int    i;
    float  line_pos;

    PK2SendVram((uintptr_t)GetGameResultCharPk2Addr(), -1, -1, 0);      /* 475 */

    GameResultTopTitleDisp(0, 0, 128);                                  /* 478 */

    GameResultTopLineDisp(GAME_RESULT_TOP_LINE_Y, 128);                 /* 481 */

    GameResultTopCaptionDisp(0, 0, 128);                                /* 484 */

    msg_alpha = Zero2Anim2D_InOutAnimCtrl(&game_result_top_disp.rank_anim_step,
                                          &game_result_top_disp.rank_anim_timer,
                                          GAME_RESULT_TOP_RANK_IN_TIME,
                                          GAME_RESULT_TOP_RANK_OUT_TIME); /* 488 */

    if (game_result_top_disp.rank_anim_step != ZERO2_ANIM2D_STEP_END) { /* 490 */
        GameResultTopDifficultyDisp(ingame_wrk.mDifficulty.Get(), 0, 0, msg_alpha); /* 492 */

        GameResultTopClearTime(GetPlayTime(), 0, 0, msg_alpha);         /* 495 */

        GameResultTopScoreTime(GetPlayData_TotalScore(), 0, 0, msg_alpha); /* 499 */

        GameResultTopRankDisp((int)game_result_top_ctrl.rank, 0, 0, msg_alpha); /* 502 */
    }

    if (game_result_top_ctrl.flg_msg_num != 0) {                        /* 505 */
        if (game_result_top_ctrl.step > GAME_RESULT_TOP_STEP_RESULT_OUT) { /* 506 */
            line_pos = GameResultTopLineAnimCtrl(&game_result_top_disp.line_anim_step,
                                                 &game_result_top_disp.line_anim_timer); /* 508 */

            GameResultTopLineDisp(line_pos + GAME_RESULT_TOP_LINE_Y
                                      + (float)gameclear_tex[GAME_RESULT_TEX_LINE_L].h,
                                  128);                                 /* 511 */

            for (i = 0; i < game_result_top_ctrl.flg_msg_num; i++) {    /* 513 */

                msg_alpha = Zero2Anim2D_InOutAnimCtrl(&game_result_top_disp.flg_anim_step[i],
                                                      &game_result_top_disp.flg_anim_timer[i],
                                                      GAME_RESULT_TOP_FLG_IN_TIME,
                                                      GAME_RESULT_TOP_FLG_OUT_TIME); /* 515 */

                if (game_result_top_disp.flg_anim_step[i] != ZERO2_ANIM2D_STEP_END) { /* 517 */
                    PrintMsg_Arrange(GAME_RESULT_TOP_MSG_TYPE,
                                     game_result_top_ctrl.flg_msg_id[i],
                                     GAME_RESULT_TOP_MSG_X,
                                     GAME_RESULT_TOP_MSG_Y + i * GAME_RESULT_TOP_MSG_DY,
                                     12, (int)msg_alpha, 0, 0, 0, 2);   /* 518 */
                }
            }                                                           /* 522 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

/* The horizontal rule's slide, and this file's only animation of its own.
 *
 * The table is a local: one segment from 0 to end_pos over end_time frames,
 * plus the -1 terminator, both patched from flg_msg_num so the rule stops
 * exactly above the last message.  24 px and 10 frames per message, on top of
 * 48 px and 20 frames of lead-in -- which is the same 2.4 px/frame either way,
 * and is why GameResultTopMain() can key the messages off `timer / 10 - 2`.
 *
 * end_pos carries two $LM records (546 for the 48.0f, 547 for the per-message
 * step).  That is one assignment written over two source lines rather than two
 * statements: GCC re-emits a line note whenever the line changes, even inside
 * a single expression -- clearmenu_top.o's four-term `||` at 337/343 is the
 * case that proves it.  end_time next door fits on one line. */
static float GameResultTopLineAnimCtrl(char *anim_step, char *anim_timer) /* 535 */
{
    POS_ANIM_TBL line_pos_tbl[2] =                                      /* 536 */
    {
        {  0.0f,  0.0f,  0,  0, 0 },
        { -1.0f, -1.0f, -1, -1, -1 },
    };
    float     pos;
    float     end_pos;
    short int end_time;

    end_pos = 48.0f +                                                   /* 546 */
              (float)game_result_top_ctrl.flg_msg_num * 24.0f;          /* 547 */

    end_time = (short int)(20 + game_result_top_ctrl.flg_msg_num * 10); /* 550 */

    line_pos_tbl[0].end_pos  = end_pos;                                 /* 552 */
    line_pos_tbl[0].end_time = end_time;                                /* 553 */

    pos = 0.0f;                                                         /* 555 */

    switch (*anim_step) {                                               /* 557 */
    case GAME_RESULT_TOP_LINE_STEP_START:
        *anim_timer = 0;                                                /* 559 */
        *anim_step  = GAME_RESULT_TOP_LINE_STEP_MOVE;                   /* 560 */

        /* falls through */
    case GAME_RESULT_TOP_LINE_STEP_MOVE:
        pos = Anim2D_CalcNowPos(line_pos_tbl, (int)*anim_timer);        /* 563 */

        (*anim_timer)++;                                                /* 565 */

        if (*anim_timer >= end_time) {                                  /* 567 */
            *anim_step = GAME_RESULT_TOP_LINE_STEP_END;                 /* 568 */
        }
        break;                                                          /* 570 */

    case GAME_RESULT_TOP_LINE_STEP_END:
        pos = end_pos;                                                  /* 572 */
        break;                                                          /* 573 */

    /* Neither of these is reachable: nothing in the object stores 3 or 4 into
     * line_anim_step.  Kept as found. */
    case 3:
    case 4:
        pos = 0.0f;                                                     /* 576 */
        break;                                                          /* 577 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 579 */
        break;
    }

    return pos;                                                         /* 583 */
}

/* The screen title.  gameclear_tex[13] is a degenerate record -- w = h = 0 --
 * so only [12] covers any pixels; the ROM draws both anyway. */
static void GameResultTopTitleDisp(int off_x, int off_y, u_char alpha)  /* 592 */
{
    DISP_SPRT bg_ds;
    int       i;

    for (i = 0; i < GAME_RESULT_TEX_TITLE_NUM; i++) {                   /* 598 */
        CopySprDToSpr(&bg_ds, &gameclear_tex[GAME_RESULT_TEX_TITLE + i]); /* 599 */

        bg_ds.x = bg_ds.x + (float)off_x;                               /* 600 */
        bg_ds.y = bg_ds.y + (float)off_y;                               /* 600 */

        bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7);   /* 601 */

        DispSprD(&bg_ds);                                               /* 602 */
    }                                                                   /* 603 */
}

/* The "difficulty" label and the name of the one being played. */
static void GameResultTopDifficultyDisp(int dif_label, int off_x, int off_y,
                                        u_char alpha)                   /* 615 */
{
    DISP_SPRT difficulty_ds;

    CopySprDToSpr(&difficulty_ds, &gameclear_tex[GAME_RESULT_TEX_DIFFICULTY]); /* 627 */

    difficulty_ds.x = difficulty_ds.x + (float)off_x;                   /* 628 */
    difficulty_ds.y = difficulty_ds.y + (float)off_y;                   /* 628 */

    difficulty_ds.alpha = (u_char)(((int)difficulty_ds.alpha * (int)alpha) >> 7); /* 629 */

    DispSprD(&difficulty_ds);                                           /* 630 */

    CopySprDToSpr(&difficulty_ds, &gameclear_tex[difficulty_tex_tbl[dif_label]]); /* 632 */

    difficulty_ds.x = difficulty_ds.x + (float)off_x;                   /* 633 */
    difficulty_ds.y = difficulty_ds.y + (float)off_y;                   /* 633 */

    difficulty_ds.alpha = (u_char)(((int)difficulty_ds.alpha * (int)alpha) >> 7); /* 634 */

    DispSprD(&difficulty_ds);                                           /* 635 */
}

/* The "clear time" label, h:mm:ss, and the two separators.  The three fields
 * are placed absolutely, so off_x / off_y move only the label and separators. */
static void GameResultTopClearTime(TIME_INFO clear_time, int off_x, int off_y,
                                   u_char alpha)                        /* 647 */
{
    DISP_SPRT time_ds;
    int       i;

    for (i = 0; i < GAME_RESULT_TEX_TIME_NUM; i++) {                    /* 653 */
        CopySprDToSpr(&time_ds, &gameclear_tex[GAME_RESULT_TEX_TIME + i]); /* 654 */

        time_ds.x = time_ds.x + (float)off_x;                           /* 655 */
        time_ds.y = time_ds.y + (float)off_y;                           /* 655 */

        time_ds.alpha = (u_char)(((int)time_ds.alpha * (int)alpha) >> 7); /* 656 */

        DispSprD(&time_ds);                                             /* 657 */
    }                                                                   /* 658 */

    DrawCmnNumberTex(clear_time.hour, 3, &gameclear_tex[GAME_RESULT_TEX_ZERO],
                     348, 213, alpha, 0, 1);                            /* 662 */

    DrawCmnNumberTex(clear_time.min, 2, &gameclear_tex[GAME_RESULT_TEX_ZERO],
                     419, 213, alpha, 0, 1);                            /* 665 */

    DrawCmnNumberTex(clear_time.sec, 2, &gameclear_tex[GAME_RESULT_TEX_ZERO],
                     472, 213, alpha, 0, 1);                            /* 668 */

    for (i = 0; i < GAME_RESULT_TEX_TIME_SEP_NUM; i++) {                /* 671 */
        CopySprDToSpr(&time_ds, &gameclear_tex[GAME_RESULT_TEX_TIME_SEP + i]); /* 672 */

        time_ds.x = time_ds.x + (float)off_x;                           /* 673 */
        time_ds.y = time_ds.y + (float)off_y;                           /* 673 */

        time_ds.alpha = (u_char)(((int)time_ds.alpha * (int)alpha) >> 7); /* 674 */

        DispSprD(&time_ds);                                             /* 675 */
    }                                                                   /* 676 */
}

/* The "score" label, the six-digit total, and its unit plate.  Note the last
 * argument: unlike the clear time, the score is drawn without leading zeros.
 * gameclear_tex[22] is degenerate, the same way [13] is. */
static void GameResultTopScoreTime(int score, int off_x, int off_y,
                                   u_char alpha)                        /* 688 */
{
    DISP_SPRT score_ds;
    int       i;

    for (i = 0; i < GAME_RESULT_TEX_SCORE_NUM; i++) {                   /* 694 */
        CopySprDToSpr(&score_ds, &gameclear_tex[GAME_RESULT_TEX_SCORE + i]); /* 695 */

        score_ds.x = score_ds.x + (float)off_x;                         /* 696 */
        score_ds.y = score_ds.y + (float)off_y;                         /* 696 */

        score_ds.alpha = (u_char)(((int)score_ds.alpha * (int)alpha) >> 7); /* 697 */

        DispSprD(&score_ds);                                            /* 698 */
    }                                                                   /* 699 */

    DrawCmnNumberTex(score, 6, &gameclear_tex[GAME_RESULT_TEX_ZERO],
                     366, 250, alpha, 0, 0);                            /* 703 */

    CopySprDToSpr(&score_ds, &gameclear_tex[GAME_RESULT_TEX_SCORE_UNIT]); /* 706 */

    score_ds.x = score_ds.x + (float)off_x;                             /* 707 */
    score_ds.y = score_ds.y + (float)off_y;                             /* 707 */

    score_ds.alpha = (u_char)(((int)score_ds.alpha * (int)alpha) >> 7); /* 708 */

    DispSprD(&score_ds);                                                /* 709 */
}

/* The "rank" label and the grade's own glyph. */
static void GameResultTopRankDisp(int rank, int off_x, int off_y,
                                  u_char alpha)                         /* 721 */
{
    DISP_SPRT rank_ds;

    CopySprDToSpr(&rank_ds, &gameclear_tex[GAME_RESULT_TEX_RANK]);      /* 737 */

    rank_ds.x = rank_ds.x + (float)off_x;                               /* 738 */
    rank_ds.y = rank_ds.y + (float)off_y;                               /* 738 */

    rank_ds.alpha = (u_char)(((int)rank_ds.alpha * (int)alpha) >> 7);   /* 739 */

    DispSprD(&rank_ds);                                                 /* 740 */

    CopySprDToSpr(&rank_ds, &gameclear_tex[rank_tex_tbl[rank]]);        /* 743 */

    rank_ds.x = rank_ds.x + (float)off_x;                               /* 744 */
    rank_ds.y = rank_ds.y + (float)off_y;                               /* 744 */

    rank_ds.alpha = (u_char)(((int)rank_ds.alpha * (int)alpha) >> 7);   /* 745 */

    DispSprD(&rank_ds);                                                 /* 746 */
}

/* The horizontal rule at `y`: a left cap, five copies of the middle piece
 * stepped by its own width, and the two right-hand pieces.  Only y is driven
 * -- every x comes from gameclear_tex[], so the rule slides vertically as one
 * unbroken line.
 *
 * DISP_SPRT::w is a u_int, which is why the ROM emits the halve/convert/double
 * sequence for `line_ds.w * i` rather than a plain cvt.s.w. */
static void GameResultTopLineDisp(float y, u_char alpha)                /* 756 */
{
    DISP_SPRT line_ds;
    int       i;

    CopySprDToSpr(&line_ds, &gameclear_tex[GAME_RESULT_TEX_LINE_L]);    /* 762 */

    line_ds.y = y;                                                      /* 763 */

    line_ds.alpha = (u_char)(((int)line_ds.alpha * (int)alpha) >> 7);   /* 764 */

    DispSprD(&line_ds);                                                 /* 765 */

    for (i = 0; i < GAME_RESULT_TEX_LINE_M_NUM; i++) {                  /* 767 */
        CopySprDToSpr(&line_ds, &gameclear_tex[GAME_RESULT_TEX_LINE_M]); /* 768 */

        line_ds.x = line_ds.x + (float)(line_ds.w * i);
        line_ds.y = y;                                                  /* 769 */

        line_ds.alpha = (u_char)(((int)line_ds.alpha * (int)alpha) >> 7); /* 770 */

        DispSprD(&line_ds);                                             /* 771 */
    }                                                                   /* 772 */

    for (i = 0; i < GAME_RESULT_TEX_LINE_R_NUM; i++) {                  /* 774 */
        CopySprDToSpr(&line_ds, &gameclear_tex[GAME_RESULT_TEX_LINE_R + i]); /* 775 */

        line_ds.y = y;                                                  /* 776 */

        line_ds.alpha = (u_char)(((int)line_ds.alpha * (int)alpha) >> 7); /* 777 */

        DispSprD(&line_ds);                                             /* 778 */
    }                                                                   /* 779 */
}

/* The button captions along the bottom.  off_x and off_y are read by neither
 * the ROM nor this -- the same pattern half the outgame display helpers have. */
static void GameResultTopCaptionDisp(int off_x, int off_y, u_char alpha) /* 790 */
{
    DrawCmnCapGroup_W(GAME_RESULT_TOP_CAP_GROUP, GAME_RESULT_TOP_CAP_GROUP,
                      alpha, 0);                                        /* 793 */
}
