// FILE: /home/zero_rom/zero2np/src/ingame/clear/prg/game_result.c
//
// The game-clear phase's spine, and the per-phase callbacks for it and its one
// child.
//
// GID_GAMERESULT is a parent phase: gphase.c runs pre_GameResult() before and
// after_GameResult() after GID_GAMERESULT_TOP, so the background and the two
// black fades belong here and the result page only draws over them.  The step
// machine has an "open" state it never leaves on its own --
// GameResultFadeOutReq(), from game_result_top.c, is what pushes it to
// FADE_OUT, and step 4 hands over to GID_CLEARMENU_TOP.
//
// It is clearmenu.o's twin (and savepoint_main.o's before that), with three
// differences that matter:
//
//   * everything on screen is chosen by ingame_wrk.mDifficulty -- 0/1 take the
//     A background pak and gameclear_tex[0..5], 2/3 the B pak and
//     gameclear_tex[6..11].  All three of the difficulty dispatches assert on
//     anything else;
//   * the background is this file's own six-sprite draw rather than a borrowed
//     one, and four of the six are drawn rotated 270 degrees about their own
//     already-offset position;
//   * this is where the clear BGM starts, and the stream id is handed straight
//     to clearmenu.o -- the same stream plays on under the clear menu, which is
//     why GameResultFadeOutReq() does not fade it and ClearMenuFadeOutReq()
//     does.
//
// The fades are asymmetric on purpose.  Step 2 runs Zero2Anim2D_FadeOut over
// the black quad (black clearing off, revealing the page); step 4 runs FadeIn
// (black closing over it).  Both share game_result_ctrl.anim_timer, and
// GameResultMain() flips the step the same frame the counter completes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), game_result.o.
// All 9 ZERO2.MAP exports plus the 12 statics.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  One place is not fully recoverable and is flagged at the
// site: GameResultTexLoadWait()'s pak selection, where GCC merged the two
// difficulty arms into one call sequence.

#include "game_result.h"

#include "clearmenu.h"                              /* SetClearMenuStreamID   */
#include "game_result_top.h"                        /* GameResultTop*         */
#include "../dat/gameclear_dat.h"                   /* gameclear_tex          */

#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_Fade*      */
#include "../../../common/ol_load.h"                /* ol_loadGetHeap         */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* ingame_wrk             */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT / DISP_SQAR  */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../main/phasefunc.h"                /* GPHASE_ENUM            */
#include "../../../system/eeiop/cddat.h"            /* GetFileSize / file ids */
#include "../../../system/eeiop/fileload.h"         /* FileLoadReqEE          */
#include "../../../system/eeiop/snd3d.h"            /* SND_3D_SET             */
#include "../../../system/eeiop/stream_auto.h"      /* StreamAutoPlay         */
#include "../../../system/os/system.h"              /* GetLanguage            */

/* Frames each of the two black fades takes.  clearmenu.o uses the same 20;
 * savepoint_main.o uses 30. */
#define GAME_RESULT_FADE_TIME       20

static void GameResultInit(void);
static void GameResultCtrlInit(void);
static void GameResultBackGroundLoadReq(void);
static void GetGameResultTexMem(void **tex_addr, int data_label);
static void GameResultTexLoadReq(void *tex_addr, int data_label);
static int  GameResultTexLoadWait(void);
static void GameResultMain(void);
static void GameResultMemFree(void);
static void LiberateGameResultTexMem(void **tex_addr);
static void GameResultDisp(void);
static void GameResultBgDisp(int off_x, int off_y, u_char alpha);
static void GameResultFadeBlackBgDisp(int off_x, int off_y, u_char alpha);

static void            *clear_bg_tex_addr;                  /* sdata 3f0ed8 */
static void            *clear_char_tex_addr;                /* sdata 3f0edc */
static GAME_RESULT_CTRL game_result_ctrl;                   /* sbss  3f4c88 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* No DispInit half here -- unlike clearmenu.o, this phase's only timer lives
 * in the control block, so GameResultCtrlInit() covers both. */
static void GameResultInit(void)                                        /* 102 */
{
    GameResultCtrlInit();                                               /* 106 */
}

static void GameResultCtrlInit(void)                                    /* 114 */
{
    game_result_ctrl.step       = GAME_RESULT_STEP_ENTRY;               /* 117 */
    game_result_ctrl.anim_timer = 0;                                    /* 118 */
}

/* ==========================================================================
 *  The two paks
 *
 *  A background pak chosen by difficulty, and a character/text pak chosen by
 *  language.  Neither Get/LoadReq/Liberate is exported -- game_result_top.o
 *  loads nothing of its own and reaches the text pak through
 *  GetGameResultCharPk2Addr().
 * ======================================================================== */

static void GameResultBackGroundLoadReq(void)                           /* 126 */
{
    if (clear_bg_tex_addr != nullptr) {                                 /* 128 */
        LiberateGameResultTexMem(&clear_bg_tex_addr);                   /* 129 */
    }
    if (clear_char_tex_addr != nullptr) {                               /* 131 */
        LiberateGameResultTexMem(&clear_char_tex_addr);                 /* 132 */
    }

    /* The switch itself leaves no marker of its own: the inlined
     * CVariable::Get() moved the line to variable.h 167 and the statement got
     * no fresh note.  See [[inlined-accessor-line-leaks-onto-caller]]. */
    switch (ingame_wrk.mDifficulty.Get()) {
    case 0:     /* easy   */
    case 1:     /* normal */
        GetGameResultTexMem(&clear_bg_tex_addr, GAMECLEAR_BG_A_PK2);    /* 140 */
        GameResultTexLoadReq(clear_bg_tex_addr, GAMECLEAR_BG_A_PK2);    /* 143 */
        break;                                                          /* 144 */

    case 2:     /* hard      */
    case 3:     /* nightmare */
        GetGameResultTexMem(&clear_bg_tex_addr, GAMECLEAR_BG_B_PK2);    /* 148 */
        GameResultTexLoadReq(clear_bg_tex_addr, GAMECLEAR_BG_B_PK2);    /* 151 */
        break;                                                          /* 152 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 154 */
        break;
    }

    /* GetLanguage() really is called twice -- functions.txt lists no local for
     * it, and the object has two jal sites. */
    GetGameResultTexMem(&clear_char_tex_addr,
                        GAMECLEAR_CHARA_PK2 + GetLanguage());           /* 157 */
    GameResultTexLoadReq(clear_char_tex_addr,
                         GAMECLEAR_CHARA_PK2 + GetLanguage());          /* 160 */
}

static void GetGameResultTexMem(void **tex_addr, int data_label)        /* 170 */
{
    /* Redundant against the caller above, which already freed both; kept
     * because it is what the ROM does. */
    if (*tex_addr != nullptr) {                                         /* 173 */
        LiberateGameResultTexMem(tex_addr);                             /* 174 */
    }

    *tex_addr = ol_loadGetHeap((int)GetFileSize(data_label));           /* 178 */
}

/* Priority 6, the same figure clearmenu.o uses -- the lowest anything in the
 * tree asks for. */
static void GameResultTexLoadReq(void *tex_addr, int data_label)        /* 187 */
{
    FileLoadReqEE(data_label, tex_addr, 6, nullptr, nullptr);           /* 191 */
}

/* Both paks have to be resident before the fade starts.
 *
 * PARTIALLY RECOVERED.  The difficulty dispatch here is emitted as one call
 * sequence with only the pak literal differing between the two arms, so the
 * second arm's line notes were eliminated and the switch cannot be laid out
 * from the stabs the way GameResultBackGroundLoadReq()'s can.  What is
 * measured: res is seeded at 205, the surviving arm's literal at 217, the two
 * FileLoadIsEnd2 calls at 220 and 221, a `return res` at 225, the assert at
 * 227 and a second `return res` at 231 that the assert path falls into.  The
 * pak number itself is not in the stabs (only `res` is), so its spelling is
 * the port's. */
static int GameResultTexLoadWait(void)                                  /* 201 */
{
    int res;
    int bg_pk2_no;

    res = 0;                                                            /* 205 */

    switch (ingame_wrk.mDifficulty.Get()) {
    case 0:     /* easy   */
    case 1:     /* normal */
        bg_pk2_no = GAMECLEAR_BG_A_PK2;                                 /* 217 */
        break;

    case 2:     /* hard      */
    case 3:     /* nightmare */
        bg_pk2_no = GAMECLEAR_BG_B_PK2;
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 227 */
        return res;                                                     /* 231 */
    }

    if (FileLoadIsEnd2(bg_pk2_no, clear_bg_tex_addr) != 0) {            /* 220 */
        if (FileLoadIsEnd2(GAMECLEAR_CHARA_PK2 + GetLanguage(),
                           clear_char_tex_addr) != 0) {                 /* 221 */
            res = 1;
        }
    }

    return res;                                                         /* 225 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Step 3 is a real, empty case: while the result page is open there is nothing
 * for the parent to do, and the transition out of it comes from
 * GameResultFadeOutReq().  The jump table at .rodata 0x3b39b0 has a slot for it
 * pointing straight at the break label, and the default arm asserts.
 *
 * Step 4 leaves for GID_CLEARMENU_TOP -- the clear menu is a sibling phase, not
 * a child, so the hand-off is a plain SetNextGPhase and the BGM survives it. */
static void GameResultMain(void)                                        /* 241 */
{
    switch (game_result_ctrl.step) {                                    /* 244 */
    case GAME_RESULT_STEP_ENTRY:
        game_result_ctrl.step = GAME_RESULT_STEP_LOAD_WAIT;             /* 246 */
        break;                                                          /* 247 */

    case GAME_RESULT_STEP_LOAD_WAIT:
        if (GameResultTexLoadWait() != 0) {                             /* 249 */
            game_result_ctrl.step       = GAME_RESULT_STEP_FADE_IN;     /* 250 */
            game_result_ctrl.anim_timer = 0;                            /* 251 */
        }
        break;                                                          /* 253 */

    case GAME_RESULT_STEP_FADE_IN:
        if (game_result_ctrl.anim_timer >= GAME_RESULT_FADE_TIME) {     /* 255 */
            game_result_ctrl.step = GAME_RESULT_STEP_OPEN;              /* 256 */
        }
        break;                                                          /* 258 */

    case GAME_RESULT_STEP_OPEN:
        break;

    case GAME_RESULT_STEP_FADE_OUT:
        if (game_result_ctrl.anim_timer >= GAME_RESULT_FADE_TIME) {     /* 262 */
            SetNextGPhase(GID_CLEARMENU_TOP);                           /* 264 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 268 */
        break;
    }
}

/* The player has chosen to leave.  No StreamAutoFadeOut here -- the clear BGM
 * carries straight on into GID_CLEARMENU_TOP. */
void GameResultFadeOutReq(void)                                         /* 277 */
{
    game_result_ctrl.step       = GAME_RESULT_STEP_FADE_OUT;            /* 280 */
    game_result_ctrl.anim_timer = 0;                                    /* 281 */
}

void *GetGameResultCharPk2Addr(void)                                    /* 294 */
{
    return clear_char_tex_addr;                                         /* 298 */
}

static void GameResultMemFree(void)                                     /* 308 */
{
    LiberateGameResultTexMem(&clear_bg_tex_addr);                       /* 312 */
    LiberateGameResultTexMem(&clear_char_tex_addr);                     /* 313 */
}

static void LiberateGameResultTexMem(void **tex_addr)                   /* 321 */
{
    if (*tex_addr != nullptr) {                                         /* 324 */
        ol_loadFreeHeap(*tex_addr);                                     /* 325 */
        *tex_addr = nullptr;                                            /* 326 */
    }
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

/* From step 2 on -- so the background is already there underneath the fade-up
 * rather than appearing when it finishes.  The 0x80 is a fixed half alpha, not
 * a fade: the fade is the black quad over the top. */
static void GameResultDisp(void)                                        /* 339 */
{
    if (game_result_ctrl.step >= GAME_RESULT_STEP_FADE_IN) {            /* 343 */
        GameResultBgDisp(0, 0, 0x80);                                   /* 345 */
    }
}

/* Six sprites per difficulty set: four drawn upright, then two rotated 270
 * degrees about their own already-offset position.  Easy and normal take
 * gameclear_tex[0..5], hard and nightmare the second copy at [6..11].
 *
 * The rotated pair is the same idiom setup_menu.o's cursor uses -- the centre
 * is (x, y + (float)w), and DISP_SPRT::w is u_int, which is why the ROM emits
 * the halve/convert/double sequence for the unsigned int-to-float. */
static void GameResultBgDisp(int off_x, int off_y, u_char alpha)        /* 357 */
{
    DISP_SPRT bg_ds;
    int       i;

    PK2SendVram((uintptr_t)clear_bg_tex_addr, -1, -1, 0);               /* 362 */

    switch (ingame_wrk.mDifficulty.Get()) {
    case 0:     /* easy   */
    case 1:     /* normal */

        for (i = 0; i < 4; i++) {                                       /* 369 */
            CopySprDToSpr(&bg_ds, &gameclear_tex[i]);                   /* 370 */

            bg_ds.x = bg_ds.x + (float)off_x;                           /* 371 */
            bg_ds.y = bg_ds.y + (float)off_y;                           /* 371 */

            bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7); /* 372 */

            DispSprD(&bg_ds);                                           /* 373 */
        }                                                               /* 374 */

        for (i = 4; i < 6; i++) {                                       /* 376 */
            CopySprDToSpr(&bg_ds, &gameclear_tex[i]);                   /* 377 */

            bg_ds.x = bg_ds.x + (float)off_x;                           /* 378 */
            bg_ds.y = bg_ds.y + (float)bg_ds.w + (float)off_y;          /* 378 */

            bg_ds.rot = 270.0f;                                         /* 379 */
            bg_ds.crx = bg_ds.x;                                        /* 379 */
            bg_ds.cry = bg_ds.y;                                        /* 379 */

            bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7); /* 380 */

            DispSprD(&bg_ds);                                           /* 381 */
        }                                                               /* 382 */
        break;                                                          /* 383 */

    case 2:     /* hard      */
    case 3:     /* nightmare */

        for (i = 6; i < 10; i++) {                                      /* 386 */
            CopySprDToSpr(&bg_ds, &gameclear_tex[i]);                   /* 387 */

            bg_ds.x = bg_ds.x + (float)off_x;                           /* 388 */
            bg_ds.y = bg_ds.y + (float)off_y;                           /* 388 */

            bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7); /* 389 */

            DispSprD(&bg_ds);                                           /* 390 */
        }                                                               /* 391 */

        for (i = 10; i < 12; i++) {                                     /* 393 */
            CopySprDToSpr(&bg_ds, &gameclear_tex[i]);                   /* 394 */

            bg_ds.x = bg_ds.x + (float)off_x;                           /* 395 */
            bg_ds.y = bg_ds.y + (float)bg_ds.w + (float)off_y;          /* 395 */

            bg_ds.rot = 270.0f;                                         /* 396 */
            bg_ds.crx = bg_ds.x;                                        /* 396 */
            bg_ds.cry = bg_ds.y;                                        /* 396 */

            bg_ds.alpha = (u_char)(((int)bg_ds.alpha * (int)alpha) >> 7); /* 397 */

            DispSprD(&bg_ds);                                           /* 398 */
        }                                                               /* 399 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 402 */
        break;
    }
}

/* The full-screen black quad both fades run through -- byte-for-byte the same
 * record clearmenu.o's ClearMenuFadeBlackBgDisp() builds, from this object's
 * own .rodata blob at 0x3b39e0.
 *
 * off_x / off_y are dead: both call sites pass 0 and neither is read. */
static void GameResultFadeBlackBgDisp(int off_x, int off_y, u_char alpha) /* 415 */
{
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };          /* 417 */
    DISP_SQAR dsq;

    (void)off_x;
    (void)off_y;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 421 */

    /* A shift, not a divide: the ROM emits a bare `sra` with none of
     * expand_divmod's bias sequence. */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 422 */

    DispSqrD(&dsq);                                                     /* 423 */
}

/* ==========================================================================
 *  GID_GAMERESULT -- the parent phase
 * ======================================================================== */

/* This is where the clear BGM starts, and the stream id goes straight to
 * clearmenu.o: the same stream plays on under GID_CLEARMENU_TOP, and
 * ClearMenuFadeOutReq() is what finally takes it down.  One statement -- the
 * jal to SetClearMenuStreamID carries no marker of its own and functions.txt
 * lists no local, so the call is nested. */
void init_GameResult(void)                                              /* 435 */
{
    GameResultInit();                                                   /* 437 */

    GameResultBackGroundLoadReq();                                      /* 440 */

    SetClearMenuStreamID(StreamAutoPlay(BGM006_CLEAR_STR, BGM006_CLEAR_HXD,
                                        17, 0, 1, 0x3200, 0,
                                        (SND_3D_SET *)nullptr));        /* 444 */
}

GPHASE_ENUM pre_GameResult(GPHASE_ENUM dummy)                           /* 448 */
{
    (void)dummy;

    GameResultMain();                                                   /* 450 */
    GameResultDisp();                                                   /* 453 */

    return GPHASE_CONTINUE;                                             /* 455 */
}

GPHASE_ENUM after_GameResult(GPHASE_ENUM result)                        /* 458 */
{
    u_char alpha;

    (void)result;

    if (game_result_ctrl.step == GAME_RESULT_STEP_FADE_IN) {            /* 465 */
        alpha = Zero2Anim2D_FadeOutAnimCtrl(&game_result_ctrl.anim_timer,
                                            GAME_RESULT_FADE_TIME);     /* 466 */
        GameResultFadeBlackBgDisp(0, 0, alpha);                         /* 468 */
    }
    else if (game_result_ctrl.step == GAME_RESULT_STEP_FADE_OUT) {      /* 470 */
        alpha = Zero2Anim2D_FadeInAnimCtrl(&game_result_ctrl.anim_timer,
                                           GAME_RESULT_FADE_TIME);      /* 471 */
        GameResultFadeBlackBgDisp(0, 0, alpha);                         /* 473 */
    }

    return GPHASE_CONTINUE;                                             /* 477 */
}

/* Only this file's own two paks.  The clear menu claims its own in
 * init_ClearMenu(), which runs after this. */
void end_GameResult(void)                                               /* 480 */
{
    GameResultMemFree();                                                /* 482 */
}

/* ==========================================================================
 *  GID_GAMERESULT_TOP -- the result page
 * ======================================================================== */

void init_GameResult_Top(void)                                          /* 488 */
{
    GameResultTopInit();                                                /* 490 */
}

/* Note the asymmetry against clearmenu.o's twin, which steps its menu on
 * anything below FADE_OUT: here the page is *drawn* from step 2 on but only
 * *stepped* at step 3 exactly, so it neither takes input during the opening
 * fade nor during the closing one. */
GPHASE_ENUM one_GameResult_Top(GPHASE_ENUM dummy)                       /* 493 */
{
    (void)dummy;

    if (game_result_ctrl.step >= GAME_RESULT_STEP_FADE_IN) {            /* 494 */

        if (game_result_ctrl.step == GAME_RESULT_STEP_OPEN) {           /* 496 */
            GameResultTopMain();                                        /* 497 */
        }

        GameResultTopDisp();                                            /* 501 */
    }

    return GPHASE_CONTINUE;                                             /* 504 */
}

void end_GameResult_Top(void)                                           /* 507 */
{
}
