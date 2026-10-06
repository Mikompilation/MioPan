// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/kai/kai_pzl.c
//
// The kai (twin-mirror) puzzle.
//
// The odd one out: it has no board.  The two ghosts stand in the live 3D room
// and the puzzle is a menu over the top of them -- pick "turn left" or "turn
// right", and both ghosts turn by their own amount.  Six tries; getting both
// to their target facing solves it.
//
// KaiPzlRotDat[game_mode][ghost] is the whole design: a starting angle, the
// two deltas the menu applies, and the angle that counts as solved.  The two
// ghosts get different deltas, which is why the puzzle is not trivial, and the
// second board (game_mode 1) changes them again.  Nothing here rotates a
// model -- ChangeEneAlgorithm() hands the turn to the ghost's own script and
// KaiPzlRotY[] just tracks where it should be.
//
// Control flow is a mode pointer rather than a switch: KaiPzlModeList[] pairs
// a mode number with its per-frame handler and KaiPuzzleSetMode() walks it.
// Every transition goes through KaiPuzzleSetFadeNextMode(), which cross-fades
// and then swaps the pointer from inside KaiPuzzleFadeProc().
//
// KaiPzlFadeSt[] is that fader: [0] elapsed, [1] length, [2] start alpha,
// [3] target alpha, [4] the value everything reads.  KaiPzlState bit 0 means
// "fading", bit 1 "and switch mode when it lands", bit 4 the cancel window is
// up and bit 5 a message is.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), kai_pzl.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Two static helpers -- the fade test at line 289 and the
// message-length helper at 255..260 -- have no out-of-line copy in the ROM and
// so do not appear in functions.txt; they are recovered from the line numbers
// their expansions carry.  Arithmetic that inherits an inlined helper's line
// is left unannotated rather than annotated wrongly.

#include "kai_pzl.h"

#include <stddef.h>                                 /* NULL                   */
#include <stdio.h>                                  /* printf                 */
#include <string.h>                                 /* memset                 */

#include "../puzzle.h"                              /* PuzzleClear            */

#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* pad                    */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */

#include "../../ingame.h"                           /* IngameCameraMain       */
#include "../../enemy/enemy.h"                      /* ChangeEneAlgorithm     */
#include "../../map/MhCtl.h"                        /* MhCtlMain              */
#include "../../photo/finder.h"                     /* FinderBankSetup        */
#include "../../plyr/player.h"                      /* PlayerMainCmn          */
#include "../../plyr/plyr_mdl.h"                    /* plyr_mdlMotionWork     */
#include "../../plyr/sis_mdl.h"                     /* sis_mdlMotionWork      */

#include "../../../graphics/effect/effect.h"
#include "../../ingame_effect.h"                   /* IgEffectMain           */
#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnWindow          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_STR / DISP_SQAR   */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg / SetString   */
#include "../../../system/eeiop/sndbank.h"          /* SndBankPlay            */
#include "../../../system/os/system.h"              /* SystemBankPlay         */
#include "../../../system/pad/pad.h"                /* paddat/GetPadAnalogRpt */

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */

static void KaiPzlDrawCap(void);
static int  KaiPzlCheckClear(void);
static int  KaiPzlGetCen(int label, int x);
static void KaiPuzzleSetMode(int mode);
static void KaiPzlPrintCen(int label, int x, int y, u_char alpha);
static int  KaiPuzzleDrawMsg(int csr, float off_x, u_char alpha);
static void KaiPuzzleDrawMsgOne(u_char alpha, int label, DISP_STR *ret);
static void KaiPuzzleBlackBgDisp(int off_x, int off_y, u_char alpha);
static void KaiPuzzleDraw4Stat(void);
static void KaiPzlCheckKai(int id, int kai_lr);

static int KaiPzlGameMode;                                  /* sdata 3f16c0 */
static int KaiPzlKaiCsl;        /* menu cursor: 1 right, 2 left, +4 quit row */
static int KaiPzlKaiYesNo;                                  /* sdata 3f16c8 */
static int KaiPzlAnimTime;                                  /* sdata 3f16cc */
static int KaiPzlCnt;           /* tries used                               */
static int KaiPzlMsgNo;                                     /* sdata 3f16d4 */
static int KaiPzlState;                                     /* sdata 3f16d8 */
static int KaiPzlEneType;                                   /* sdata 3f16dc */

static KAIPZL_MODE *KapPzlModeNowPtr;                       /* sdata 3f16e0 */

/* Mode 0 has no handler -- reaching it would crash, and nothing sets it. */
static KAIPZL_MODE KaiPzlModeList[9] =                      /* data 319328 */
{
    { 0, NULL           },
    { 1, KaiPzlProc     },      /* the menu                                */
    { 2, KaiPzlAnim     },      /* waiting out the turn                    */
    { 3, KaiPzlCancel   },      /* the quit prompt                         */
    { 6, KaiPzlRelease  },      /* giving the sound bank back              */
    { 5, KaiPzlDrawMsg  },      /* a result message                        */
    { 7, KaiPzlTerm     },
    { 8, KaiPzlTerm2    },
    { 4, KaiPzlClear    },
};

/* [game_mode][ghost]: where it starts, what "left" and "right" add, and the
 * facing that counts as solved.  Angles are whole degrees mod 360. */
static KAIPZL_ROT KaiPzlRotDat[2][2] =                      /* data 319370 */
{
    /*   iInit  iRotL  iRotR  iClear */
    { {      0,   -45,    45,     90 },
      {      0,   -45,    90,    270 } },
    { {      0,   -30,    60,     90 },
      {      0,   -45,   135,    270 } },
};

/* -1, not 0: KaiPuzzleFadeProc() would otherwise switch to mode 0 -- the one
 * with a null handler -- on any fade that lands without a mode request. */
static int KapPzlFadeNext = -1;                             /* sdata 3f16e4 */

static int KaiPzlEnemID[2];                                 /* sbss 3f4d38 */
static int KaiPzlRotY[2];                                   /* sbss 3f4d40 */

/* [0] elapsed, [1] length, [2] from, [3] to, [4] the live alpha. */
static int KaiPzlFadeSt[5];                                 /* bss 4b4150 */

/* --------------------------------------------------------------------------
 *  Two helpers the ROM inlines everywhere and never emits out of line.
 * ------------------------------------------------------------------------ */

/* Non-zero while a fade is running.  Every mode handler opens with it. */
static inline int KaiPzlIsFade(void)
{
    return ((KaiPzlState & 3) != 0);                                    /* 289 */
}

/* Pixel width of message `label` in the puzzle's own message set. */
static inline int KaiPzlGetMsgLen(int label)
{
    DISP_STR ds;

    memset(&ds, 0, sizeof(ds));                                         /* 259 */
    ds.str = GetMsgDataAddr(0x43, label);
    return GetMsgLineLength(ds.str, (u_char **)0);                      /* 260 */
}

/* ==========================================================================
 *  Small pieces
 * ======================================================================== */

/* Mode 8 is the last fade out; the button captions come off before it. */
static void KaiPzlDrawCap(void)                                         /* 217 */
{                                                                       /* 218 */
    if (KapPzlModeNowPtr->mode != 8) {                                  /* 219 */
        DrawCmnCapGroup_W(0, 0, 0x80, 0);                               /* 227 */
    }
}                                                                       /* 234 */

static int KaiPzlCheckClear(void)                                       /* 246 */
{                                                                       /* 247 */
    int i;

    for (i = 0; i < 2; i++) {                                           /* 248 */
        if (KaiPzlRotDat[KaiPzlGameMode][i].iClear != KaiPzlRotY[i]) {  /* 250 */
            return 0;                                                   /* 251 */
        }
    }

    return 1;                                                           /* 252 */
}

/* x is the window's horizontal centre, so this is "left edge of a centred
 * string". */
static int KaiPzlGetCen(int label, int x)                               /* 265 */
{                                                                       /* 266 */
    return x - KaiPzlGetMsgLen(label) / 2;                              /* 256 */
}

static void KaiPuzzleSetMode(int mode)                                  /* 274 */
{                                                                       /* 275 */
    int i;

    for (i = 0; i < 9; i++) {                                           /* 276 */
        KapPzlModeNowPtr = &KaiPzlModeList[i];

        if (KapPzlModeNowPtr->mode == mode) {
            return;
        }
    }

    /* Not found: KaiPuzzleMain() prints NO_PUZZLE_MODE_PTR and recovers. */
    KapPzlModeNowPtr = (KAIPZL_MODE *)0;                                /* 282 */
}

/* ==========================================================================
 *  The fader
 * ======================================================================== */

void KaiPuzzleSetFadeCmn(int frame, int alpha)                          /* 295 */
{
    KaiPzlFadeSt[0] = 0;                                                /* 296 */
    KaiPzlFadeSt[1] = frame;                                            /* 297 */
    /* Always ramps from wherever it currently stands. */
    KaiPzlFadeSt[2] = KaiPzlFadeSt[4];                                  /* 298 */
    KaiPzlFadeSt[3] = alpha;                                            /* 299 */
}

void KaiPuzzleSetFade(int frame, int alpha, int flg)                    /* 303 */
{
    KaiPzlState = (KaiPzlState & ~3) | flg;                             /* 304 */
    KaiPuzzleSetFadeCmn(frame, alpha);                                  /* 305 */
}                                                                       /* 307 */

void KaiPuzzleSetFade2(int frame, int alpha_st, int alpha_en, int flg)  /* 311 */
{
    KaiPzlFadeSt[4] = alpha_st;                                         /* 312 */
    KaiPuzzleSetFade(frame, alpha_en, flg);                             /* 313 */
}                                                                       /* 314 */

void KaiPuzzleSetFadeNextMode(int mode, int frame)                      /* 318 */
{
    KaiPuzzleSetFade(frame, 0, 3);                                      /* 319 */
    KapPzlFadeNext = mode;                                              /* 320 */
}                                                                       /* 321 */

/* --------------------------------------------------------------------------
 *  A mode-change fade is really two: this one to black, then -- from inside
 *  the landing -- a second one back up, which is what makes the swap
 *  invisible.  KaiPzlFadeSt[1] is an unguarded divisor.
 * ------------------------------------------------------------------------ */
void KaiPuzzleFadeProc(void)                                            /* 326 */
{                                                                       /* 327 */
    int f_alpha;

    if (KaiPzlIsFade()) {                                               /* 289 */
        f_alpha = (KaiPzlFadeSt[3] - KaiPzlFadeSt[2]) * KaiPzlFadeSt[0];
        KaiPzlFadeSt[0]++;                                              /* 334 */
        KaiPzlFadeSt[4] = f_alpha / KaiPzlFadeSt[1] + KaiPzlFadeSt[2];  /* 337 */

        if (KaiPzlFadeSt[0] >= KaiPzlFadeSt[1]) {                       /* 342 */
            KaiPzlFadeSt[4] = KaiPzlFadeSt[3];                          /* 346 */

            if ((KaiPzlState & 2) != 0) {                               /* 347 */
                f_alpha = KaiPzlState & 1;
                KaiPzlState &= ~2;                                      /* 350 */

                if (f_alpha != 0) {                                     /* 351 */
                    KaiPuzzleSetFadeCmn(6, 0x80);                       /* 353 */
                }

                KaiPuzzleSetMode(KapPzlFadeNext);                       /* 356 */
            } else {
                KaiPzlState &= ~1;                                      /* 359 */
            }
        }
    }
}

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* mode is the puzzle id: 4 is Kai1 (two sisters), 5 is Kai2 (two ghosts). */
void KaiPuzzleExeInit(int mode)                                         /* 375 */
{                                                                       /* 376 */
    int i;

    if (mode == PZL_ID_KAI1) {                                          /* 377 */
        KaiPzlEnemID[0] = 3;                                            /* 379 */
        KaiPzlEnemID[1] = 4;                                            /* 380 */
        KaiPzlEneType   = 0;                                            /* 382 */
    } else {
        KaiPzlEnemID[0] = 0x69;                                         /* 383 */
        KaiPzlEnemID[1] = 0x6a;                                         /* 384 */
        KaiPzlEneType   = 2;                                            /* 385 */
    }

    KaiPzlGameMode = (mode != PZL_ID_KAI1);                             /* 388 */

    KaiPzlRotY[0] = KaiPzlRotDat[KaiPzlGameMode][0].iInit;              /* 389 */
    KaiPzlRotY[1] = KaiPzlRotDat[KaiPzlGameMode][1].iInit;              /* 391 */

    KaiPzlKaiCsl = 2;                                                   /* 392 */
    KaiPzlCnt    = 0;                                                   /* 395 */

    for (i = 4; i >= 0; i--) {                                          /* 396 */
        KaiPzlFadeSt[i] = 0;                                            /* 397 */
    }

    KapPzlFadeNext = -1;                                                /* 398 */
    KaiPzlState    = 1;                                                 /* 399 */

    KaiPuzzleSetMode(1);                                                /* 400 */
    KaiPuzzleSetFade(6, 0x80, 1);                                       /* 401 */
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

static void KaiPzlPrintCen(int label, int x, int y, u_char alpha)       /* 417 */
{
    PrintMsg(0x43, label, KaiPzlGetCen(label, x), y, 1, (int)alpha, 0); /* 418 */
}

/* --------------------------------------------------------------------------
 *  The three-line menu, and the cursor over it.  Returns which row the cursor
 *  is on: 0 the first turn option, 1 the second, 2 the quit line.
 *
 *  Both parameters are the ROM's and neither is used -- the cursor comes from
 *  the global KaiPzlKaiCsl and off_x is never read.
 * ------------------------------------------------------------------------ */
static int KaiPuzzleDrawMsg(int csr, float off_x, u_char alpha)         /* 423 */
{                                                                       /* 424 */
    MSG_WIN_DAT win_dat;
    DISP_STR    ds;
    int         cx;
    int         cy;
    int         ret;
    float       c_size;
    int         cen;

    SetMsgWinDefData(&win_dat, 0x43);                                   /* 430 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h,
                  alpha, 0x80);                                         /* 432 */

    SetMsgDefData(&ds, 0x43);                                           /* 434 */

    cx = (int)(win_dat.x + win_dat.w * 0.5f);                           /* 436 */

    if ((KaiPzlKaiCsl & 4) != 0) {                                      /* 439 */
        cen    = KaiPzlGetCen(2, cx);                                   /* 440 */
        cy     = ds.pos_y + 0x2e;                                       /* 441 */
        /* The quit line is highlighted as a bar, not a caret. */
        c_size = (float)KaiPzlGetMsgLen(2) + 2.0f;
        ret    = 2;                                                     /* 443 */
    } else {
        c_size = 0.0f;                                                  /* 446 */
        cy     = ds.pos_y + 0x17;                                       /* 445 */

        if ((KaiPzlKaiCsl & 1) != 0) {                                  /* 449 */
            cen = KaiPzlGetCen(1, cx) + KaiPzlGetMsgLen(1) - 0x78;      /* 450 */
            ret = 0;                                                    /* 452 */
        } else {
            cen = KaiPzlGetCen(1, cx);                                  /* 455 */
            ret = 1;                                                    /* 456 */
        }
    }

    DrawCmnSelCsr(0, (float)cen, (float)cy, alpha, c_size, 0);          /* 461 */

    PrintMsg(0x43, 0, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 463 */
    ds.pos_y = ds.pos_y + 0x17;                                         /* 464 */
    KaiPzlPrintCen(1, cx, ds.pos_y, alpha);                             /* 465 */
    ds.pos_y = ds.pos_y + 0x17;                                         /* 466 */
    KaiPzlPrintCen(2, cx, ds.pos_y, alpha);                             /* 467 */

    return ret;                                                         /* 469 */
}

/* `ret` lets the caller keep the DISP_STR; every call site passes NULL. */
static void KaiPuzzleDrawMsgOne(u_char alpha, int label, DISP_STR *ret) /* 474 */
{                                                                       /* 475 */
    MSG_WIN_DAT win_dat;
    DISP_STR   *ds;
    DISP_STR    work;

    ds = &work;

    if (ret != (DISP_STR *)0) {                                         /* 479 */
        ds = ret;
    }

    SetMsgWinDefData(&win_dat, 0x43);                                   /* 482 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h,
                  alpha, 0x80);                                         /* 484 */

    SetMsgDefData(ds, 0x43);                                            /* 486 */
    PrintMsg(0x43, label, ds->pos_x, ds->pos_y, 1, (int)alpha, 0);      /* 487 */
}

static void KaiPuzzleBlackBgDisp(int off_x, int off_y, u_char alpha)    /* 491 */
{                                                                       /* 492 */
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 128 };           /* 494 */
    DISP_SQAR dsq;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 498 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 499 */
    DispSqrD(&dsq);                                                     /* 500 */
}

void KaiPzlDrawCancelWindow(u_char alpha)                               /* 505 */
{                                                                       /* 506 */
    static const int pos_x[2] = { 155, 362 };                   /* sdata 3f16e8 */

    KaiPuzzleDrawMsgOne(alpha, 6, (DISP_STR *)0);                       /* 511 */

    DrawCmnSelCsr(0, (float)pos_x[KaiPzlKaiYesNo], 388.0f, alpha, 0.0f, 0); /* 516 */
    DrawCmnSelYes(0, 153.0f, 390.0f, alpha);                            /* 520 */
    DrawCmnSelNo(0, 361.0f, 390.0f, alpha);                             /* 522 */
}

/* The two overlays that have to survive the closing fade, redrawn at full
 * brightness by the terminal modes. */
static void KaiPuzzleDraw4Stat(void)                                    /* 527 */
{                                                                       /* 528 */
    if ((KaiPzlState & 0x10) != 0) {                                    /* 529 */
        KaiPzlDrawCancelWindow(0x80);                                   /* 530 */
    } else if ((KaiPzlState & 0x20) != 0) {                             /* 531 */
        KaiPuzzleDrawMsgOne(0x80, KaiPzlMsgNo, (DISP_STR *)0);          /* 532 */
    }
}

/* kai_lr 1 turns right, 2 turns left; anything else only re-wraps. */
static void KaiPzlCheckKai(int id, int kai_lr)                          /* 543 */
{                                                                       /* 544 */
    if (kai_lr == 1) {                                                  /* 545 */
        KaiPzlRotY[id] = KaiPzlRotY[id] + KaiPzlRotDat[KaiPzlGameMode][id].iRotR; /* 546 */
    } else if (kai_lr == 2) {                                           /* 547 */
        KaiPzlRotY[id] = KaiPzlRotY[id] + KaiPzlRotDat[KaiPzlGameMode][id].iRotL; /* 550 */
    }

    KaiPzlRotY[id] = KaiPzlRotY[id] % 360;                              /* 551 */

    if (KaiPzlRotY[id] < 0) {
        KaiPzlRotY[id] = KaiPzlRotY[id] + 360;
    }
}

/* ==========================================================================
 *  Mode handlers
 *
 *  All eight return 1 except KaiPzlTerm/Term2, which return 0 once they are
 *  done -- and nothing reads the value.
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  Mode 1: the menu.  Up and down move between the two turn options and the
 *  quit line (bit 2 of KaiPzlKaiCsl); left and right choose between the two
 *  turn options (bits 0/1).
 *
 *  Note the crossover: the cursor on the right-hand word (csl bit 0) reports
 *  row 0, which becomes kai_lr 2 -- the iRotL delta.  That is what the ROM
 *  does; the words themselves come from the message file.
 * ------------------------------------------------------------------------ */
int KaiPzlProc(void)                                            /* 555 */
{                                                                       /* 556 */
    int csr;
    int kai;
    int flg;

    if (KaiPzlIsFade()) {                                               /* 289 */
        KaiPuzzleDrawMsg(KaiPzlKaiCsl, 0.0f, (u_char)KaiPzlFadeSt[4]);  /* 641 */
        return 1;                                                       /* 642 */
    }

    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0)) {    /* 562 */
        if (KaiPzlKaiCsl != 2) {                                        /* 563 */
            SndBankPlay(GetPzlSndBankID(), 0, 0, 0, 0x3200, 0x1000, 0,
                        (SND_3D_SET *)0);                               /* 567 */
            KaiPzlKaiCsl = 2;                                           /* 568 */
        }
    } else if (((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) { /* 569 */
        if (KaiPzlKaiCsl != 1) {                                        /* 570 */
            SndBankPlay(GetPzlSndBankID(), 0, 0, 0, 0x3200, 0x1000, 0,
                        (SND_3D_SET *)0);                               /* 574 */
            KaiPzlKaiCsl = 1;                                           /* 575 */
        }
    } else if (((pad[0].rpt & 0x1000) != 0) || (GetPadAnalogRpt(0) != 0)) { /* 576 */
        if ((KaiPzlKaiCsl & 4) != 0) {                                  /* 577 */
            SndBankPlay(GetPzlSndBankID(), 0, 0, 0, 0x3200, 0x1000, 0,
                        (SND_3D_SET *)0);                               /* 581 */
            KaiPzlKaiCsl &= ~4;                                         /* 582 */
        }
    } else if (((pad[0].rpt & 0x4000) != 0) || (GetPadAnalogRpt(1) != 0)) { /* 583 */
        if ((KaiPzlKaiCsl & 4) == 0) {                                  /* 584 */
            SndBankPlay(GetPzlSndBankID(), 0, 0, 0, 0x3200, 0x1000, 0,
                        (SND_3D_SET *)0);                               /* 588 */
            KaiPzlKaiCsl |= 4;                                          /* 589 */
        }
    }

    csr = KaiPuzzleDrawMsg(KaiPzlKaiCsl, 0.0f, 0x80);                   /* 596 */

    if (*paddat[0] == 1) {                                              /* 599 */
        if (csr == 0) {                                                 /* 603 */
            kai = 2;
        } else if (csr == 1) {
            kai = 1;                                                    /* 608 */
        } else {
            KaiPzlKaiYesNo = 1;
            SystemBankPlay(3, 0, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 612 */
            KaiPuzzleSetFadeNextMode(3, 6);                             /* 613 */
            return 1;                                                   /* 614 */
        }

        SndBankPlay(GetPzlSndBankID(), 2, 0, 0, 0x3200, 0x1000, 0,
                    (SND_3D_SET *)0);                                   /* 617 */
        SndBankPlay(GetPzlSndBankID(), 1, 0, 0, 0x3200, 0x1000, 0,
                    (SND_3D_SET *)0);                                   /* 618 */

        KaiPzlCheckKai(0, kai);                                         /* 619 */
        KaiPzlCheckKai(1, kai);                                         /* 620 */

        /* The ghosts do the turning; this only asks them to. */
        flg  = ChangeEneAlgorithm(KaiPzlEneType, KaiPzlEnemID[0], kai); /* 623 */
        flg &= ChangeEneAlgorithm(KaiPzlEneType, KaiPzlEnemID[1], kai); /* 624 */

        if (flg == 0) {                                                 /* 626 */
            printf("ERR! NO_REQ_MODEL_ANIM\n");                         /* 627 */
        }

        KaiPzlAnimTime = 15;                                            /* 631 */
        KaiPuzzleSetFadeNextMode(2, 6);                                 /* 632 */
    } else if (*paddat[1] == 1) {                                       /* 635 */
        KaiPzlKaiYesNo = *paddat[1];                                    /* 637 */
        SndBankPlay(GetPzlSndBankID(), 3, 0, 0, 0x3200, 0x1000, 0,
                    (SND_3D_SET *)0);                                   /* 638 */
        KaiPuzzleSetFadeNextMode(3, 6);
    }

    return 1;                                                           /* 642 */
}

/* Mode 2: hold for the ghosts' turn, then either finish or spend a try. */
int KaiPzlAnim(void)                                            /* 646 */
{                                                                       /* 647 */
    KaiPzlAnimTime--;                                                   /* 650 */

    if ((KaiPzlAnimTime < 1) && (!KaiPzlIsFade())) {                    /* 657 / 289 */
        KaiPzlCnt++;                                                    /* 660 */

        if (KaiPzlCheckClear() != 0) {                                  /* 661 */
            KaiPuzzleSetMode(4);                                        /* 663 */
        } else if (KaiPzlCnt < 6) {                                     /* 664 */
            KaiPuzzleSetFadeNextMode(1, 6);                             /* 665 */
        } else {
            KaiPzlMsgNo = 5;                                            /* 669 */
            KaiPuzzleSetFadeNextMode(5, 6);                             /* 671 */
        }
    }

    return 1;                                                           /* 672 */
}

/* Mode 4: solved.  Kai1 additionally hands the ghosts algorithm 3, which is
 * the pose they hold for the scene that follows. */
int KaiPzlClear(void)                                           /* 676 */
{                                                                       /* 677 */
    if (!KaiPzlIsFade()) {                                              /* 289 */
        if (KaiPzlGameMode == 0) {                                      /* 682 */
            PuzzleClear(PZL_ID_KAI1);                                   /* 683 */
            KaiPzlMsgNo = 3;                                            /* 684 */
            ChangeEneAlgorithm(KaiPzlEneType, KaiPzlEnemID[0], 3);      /* 686 */
            ChangeEneAlgorithm(KaiPzlEneType, KaiPzlEnemID[1], 3);      /* 687 */
        } else {
            PuzzleClear(PZL_ID_KAI2);                                   /* 689 */
            KaiPzlMsgNo = 4;                                            /* 690 */
        }

        SndBankPlay(GetPzlSndBankID(), 4, 0, 0, 0x3200, 0x1000, 0,
                    (SND_3D_SET *)0);                                   /* 693 */
        KaiPuzzleSetFadeNextMode(5, 6);                                 /* 694 */
    }

    return 1;                                                           /* 695 */
}                                                                       /* 696 */

/* Mode 5: a result message.  Bit 5 keeps it on screen through the teardown. */
int KaiPzlDrawMsg(void)                                         /* 700 */
{                                                                       /* 701 */
    KaiPuzzleDrawMsgOne((u_char)KaiPzlFadeSt[4], KaiPzlMsgNo, (DISP_STR *)0); /* 703 */

    if ((!KaiPzlIsFade()) && (*paddat[0] == 1)) {                       /* 289 / 709 */
        SystemBankPlay(3, 0, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 710 */
        KaiPzlState |= 0x20;                                            /* 711 */
        KaiPuzzleSetMode(6);                                            /* 712 */
    }

    return 1;                                                           /* 714 */
}                                                                       /* 715 */

/* --------------------------------------------------------------------------
 *  Mode 3: the quit prompt.  Answering "no" (KaiPzlKaiYesNo != 0) goes back
 *  to the menu; "yes" raises bit 4 and starts the teardown.
 *
 *  Note the left/right test folds both bits into one mask (0xa000) rather
 *  than testing them separately as the other puzzles do.
 * ------------------------------------------------------------------------ */
int KaiPzlCancel(void)                                          /* 718 */
{                                                                       /* 719 */
    u_char alpha;

    alpha = (u_char)KaiPzlFadeSt[4];                                    /* 723 */

    if (!KaiPzlIsFade()) {                                              /* 289 / 724 */
        if (((pad[0].rpt & 0xa000) != 0) || (GetPadAnalogRpt(2) != 0) ||
            (GetPadAnalogRpt(3) != 0)) {                                /* 728 */
            SndBankPlay(GetPzlSndBankID(), 0, 0, 0, 0x3200, 0x1000, 0,
                        (SND_3D_SET *)0);                               /* 748 */
            KaiPzlKaiYesNo ^= 1;                                        /* 749 */
            alpha = 0x80;                                               /* 750 */
        } else if (*paddat[0] == 1) {                                   /* 729 */
            SystemBankPlay(3, 0, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 730 */

            if (KaiPzlKaiYesNo == 0) {                                  /* 733 */
                KaiPzlState |= 0x10;                                    /* 734 */
                KaiPuzzleSetMode(6);                                    /* 735 */
            } else {
                KaiPuzzleSetFadeNextMode(1, 6);                         /* 737 */
            }

            alpha = 0x80;                                               /* 738 */
        } else if (*paddat[1] == 1) {                                   /* 740 */
            SndBankPlay(GetPzlSndBankID(), 3, 0, 0, 0x3200, 0x1000, 0,
                        (SND_3D_SET *)0);                               /* 744 */
            KaiPuzzleSetFadeNextMode(1, 6);                             /* 745 */
            alpha = 0x80;                                               /* 746 */
        } else {
            alpha = 0x80;
        }
    }

    KaiPzlDrawCancelWindow(alpha);                                      /* 748 */

    return 1;                                                           /* 750 */
}

/* Mode 6: give the puzzle's sound bank back and take the finder's. */
int KaiPzlRelease(void)                                         /* 757 */
{                                                                       /* 758 */
    KaiPuzzleDraw4Stat();                                               /* 761 */

    SndBankRelease(GetPzlSndBankID());                                  /* 764 */
    FinderBankSetup();                                                  /* 766 */

    KaiPuzzleSetFade2(30, 0, 0x80, 1);                                  /* 767 */
    KaiPuzzleSetMode(7);                                                /* 768 */

    return 1;
}

/* Mode 7: fade the black curtain in.  Quitting or running out of tries hands
 * the ghosts algorithm 4 -- the pose for an unsolved puzzle. */
int KaiPzlTerm(void)                                            /* 775 */
{                                                                       /* 776 */
    int ret;

    KaiPuzzleDraw4Stat();                                               /* 778 */
    KaiPuzzleBlackBgDisp(0, 0, (u_char)KaiPzlFadeSt[4]);                /* 784 */

    ret = 1;

    if (!KaiPzlIsFade()) {                                              /* 289 */
        if (((KaiPzlState & 0x10) != 0) || (KaiPzlMsgNo == 5)) {        /* 787 */
            ChangeEneAlgorithm(KaiPzlEneType, KaiPzlEnemID[0], 4);      /* 788 */
            ChangeEneAlgorithm(KaiPzlEneType, KaiPzlEnemID[1], 4);      /* 791 */
        }

        KaiPuzzleSetFade2(30, 0x80, 0, 1);                              /* 792 */
        KaiPuzzleSetMode(8);                                            /* 793 */
        ret = 0;                                                        /* 794 */
    }

    return ret;
}

int KaiPzlTerm2(void)                                           /* 797 */
{                                                                       /* 798 */
    int ret;

    KaiPuzzleBlackBgDisp(0, 0, (u_char)KaiPzlFadeSt[4]);                /* 801 */

    ret = 1;

    if (!KaiPzlIsFade()) {                                              /* 289 */
        if (FinderBankIsReady() != 0) {                                 /* 806 */
            SetNextGPhase(GID_STORY_NORMAL);                            /* 807 */
        }

        ret = 0;                                                        /* 810 */
    }

    return ret;                                                         /* 811 */
}

/* ==========================================================================
 *  Per-frame entry
 *
 *  Unlike the other four puzzles this one keeps the 3D room running -- the
 *  ghosts are the puzzle -- so it drives the ingame chain itself rather than
 *  drawing a board.  The three SetString() calls are debug readouts and only
 *  show with the string overlay on.
 * ======================================================================== */

int KaiPuzzleMain(void)                                                 /* 819 */
{                                                                       /* 820 */
    int area_no;

    SetString(10.0f, 50.0f, "CSR%d", KaiPzlKaiCsl);                     /* 824 */
    SetString(10.0f, 70.0f, "ROT_Y1 %d", KaiPzlRotY[0]);                /* 825 */
    SetString(10.0f, 90.0f, "ROT_Y2 %d", KaiPzlRotY[1]);                /* 826 */

    if (KapPzlModeNowPtr == (KAIPZL_MODE *)0) {                         /* 828 */
        printf("NO_PUZZLE_MODE_PTR\n");                                 /* 829 */
        KaiPuzzleSetMode(6);                                            /* 830 */
        return 1;                                                       /* 831 */
    }

    PlayerMainCmn(1);                                                   /* 834 */

    EnemyAnimOne(KaiPzlEnemID[0]);                                      /* 841 */
    EnemyAnimOne(KaiPzlEnemID[1]);                                      /* 842 */

    area_no = GetPlyrAreaNo();
    MhCtlMain(area_no);                                                 /* 846 */

    IngameCameraMain();                                                 /* 852 */

    EnemyMotionWork();                                                  /* 854 */
    sis_mdlMotionWork();                                                /* 855 */
    plyr_mdlMotionWork();                                               /* 856 */

    IgEffectMain();                                                     /* 858 */
    IngameDrawSub();                                                    /* 859 */

    KaiPzlDrawCap();                                                    /* 861 */

    KaiPuzzleFadeProc();                                                /* 863 */
    KapPzlModeNowPtr->func();                                           /* 866 */

    return 1;                                                           /* 869 */
}
