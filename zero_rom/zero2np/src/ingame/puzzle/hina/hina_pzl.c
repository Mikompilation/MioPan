// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/hina/hina_pzl.c
//
// The hina-dan puzzle: a 4x4 sliding-tile board of festival dolls.
//
// hina_pos[][] is the board.  Nine numbered dolls, one empty slot (-2) and
// four holes (-1) where the tiered shelf has no cell.  The cursor walks the
// grid, X asks for the doll under it to slide into the empty slot, and the
// puzzle is solved when the two "8" dolls both sit on the top shelf.  Two of
// the nine -- the pair in no_move_hina[] -- are scenery the cursor skips.
//
// The cursor walk is the bulk of the file.  Each of the four directions is
// written out in full: step one cell at a time, keep stepping while the cell
// is the empty slot, give up at a hole or the edge, and refuse a fixed doll.
// Every giving-up path plays sample 3 and puts the cursor back, and GCC
// cross-jumped all nine of those calls plus the four success calls into the
// single SndBankPlay() at the end -- which is why only the last direction
// block's success line (589) survives in the debug info.
//
// Two state machines drive the display.  hina_pzl_disp.sub_anim_* cross-fades
// between sub_steps (board / quit prompt / cleared / timed out): nothing
// changes sub_step directly, it goes through HinaPuzzleReqNextSubStep(), which
// fades the old one out and lets HinaPuzzleSubOutAnimCheck() swap at the
// bottom.  hina_pzl_disp.anim_* is the black curtain that ends the puzzle.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), hina_pzl.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Statements whose only memory access goes through
// fixed_array's inlined operator[], and stores GCC parked in a delay slot,
// leave no $LM of their own; a handful of those are interpolated into a gap
// with only one candidate line, and the rest carry no annotation.

#include "hina_pzl.h"

#include <stddef.h>                                 /* NULL                   */
#include <stdio.h>                                  /* printf (PRINT_WARNING) */

#include "puzzle_hina_dat.h"                        /* hina_first_pos         */
#include "../puzzle.h"                              /* GetPzlTexDataAddr      */

#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* pad                    */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */

#include "../../menu/anim_2d.h"                     /* Anim2D_CalcNowAlpha    */
#include "../../photo/finder.h"                     /* FinderBankSetup        */
#include "../../subtitle/subtitle.h"                /* SubTitleReq            */

#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnWindow          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT / DispSprD   */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg               */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../system/eeiop/snd_buffer.h"       /* SndBufIsPlaying        */
#include "../../../system/eeiop/sndbank.h"          /* SndBankPlay            */
#include "../../../system/os/system.h"              /* GetPALMode             */
#include "../../../system/pad/pad.h"                /* paddat/GetPadAnalogRpt */

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */

static void HinaPuzzleCtrlInit(void);
static void HinaPuzzleSubOutAnimCheck(void);
static void HinaPuzzleReqNextSubStep(char next_step);
static int  HinaPuzzleClearCheck(void);
static void HinaPuzzleDollSelPad(void);
static void HinaPuzzleDollPosChange(void);
static int  HinaPuzzleChangeCheck(int csr_tate, int csr_yoko);
static void HinaPuzzleGetBlankPos(char *tate, char *yoko);
static void HinaPuzzleChangeDataPos(int tate_a, int yoko_a, int tate_b, int yoko_b);
static void HinaPuzzleExitSelPad(void);
static void HinaPuzzleClearPad(void);
static void HinaPuzzleTimer(void);
static void HinaPuzzleExitReq(void);
static void HinaPuzzleDispInit(void);
static u_char HinaPuzzleModeAnimCtrl(void);
static void HinaPuzzleDollSelDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleExitSelDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleClearDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleTimeOverDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleBgDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleStandFleaDisp(int off_x, int off_y, u_char alpha);
static u_char HinaPuzzleStandFleaAnim(void);
static void HinaPuzzleDollsFleaDisp(int off_x, int off_y, u_char alpha);
static u_char HinaPuzzleDollsFleaAnim(void);
static void HinaPuzzleDollsDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleSmokeScreenDisp(int off_x, int off_y, u_char alpha);
static u_char HinaPuzzleSmokeScreenAnim(short *timer);
static void HinaPuzzleRemainderTimeDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleCaptionDisp(int off_x, int off_y, u_char alpha);
static u_char HinaPuzzleWarningTimeAnim(int timer);
static void HinaPuzzleBlackBgDisp(void);
static void HinaPuzzleAnim(char *anim_step, short *anim_timer, u_char *alpha);
static void HinaPuzzleExitConfWinDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleClearWinDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleTimeOverWinDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleCmnWinDisp(int off_x, int off_y, u_char alpha);
static void HinaPuzzleCrossDollsDisp(int off_x, int off_y, u_char alpha);

/* One pad handler and one display handler per sub_step.  Slots 2 and 3 --
 * cleared and timed out -- share a pad handler that only waits for a button. */
static void (*hina_pad_func[4])(void) =                     /* data 318690 */
{
    HinaPuzzleDollSelPad,
    HinaPuzzleExitSelPad,
    HinaPuzzleClearPad,
    HinaPuzzleClearPad,
};

static void (*hina_disp_func[4])(int, int, u_char) =        /* data 3186a0 */
{
    HinaPuzzleDollSelDisp,
    HinaPuzzleExitSelDisp,
    HinaPuzzleClearDisp,
    HinaPuzzleTimeOverDisp,
};

static HINA_PZL_CTRL       hina_pzl_ctrl;                   /* bss  4b3fd0 */
static HINA_PZL_DISP       hina_pzl_disp;                   /* bss  4b4028 */
static HINA_PZL_CROSS_DISP hina_pzl_cross_disp;             /* sbss 3f4d08 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

void HinaPuzzleExeInit(void)
{
    HinaPuzzleCtrlInit();                                               /* 226 */
}

/* Called from init_Puzzle_CrossFade(), before the puzzle phase exists, so the
 * smoke overlay is already running when the board fades in. */
void HinaPuzzleCrossDispInit(void)
{
    hina_pzl_cross_disp.anim_timer = 0;                                 /* 241 */
}

static void HinaPuzzleCtrlInit(void)                                    /* 247 */
{                                                                       /* 248 */
    int i;
    int j;

    hina_pzl_ctrl.step          = 0;                                    /* 252 */
    hina_pzl_ctrl.sub_step      = 0;                                    /* 253 */
    hina_pzl_ctrl.next_sub_step = 0;

    /* 30 real seconds either way. */
    if (GetPALMode() != 0) {                                            /* 257 */
        hina_pzl_ctrl.timer = 1500;
    } else {
        hina_pzl_ctrl.timer = 1800;                                     /* 261 */
    }

    for (i = 0; i < 4; i++) {                                           /* 264 */
        for (j = 0; j < 4; j++) {                                       /* 265 */
            hina_pzl_ctrl.hina_pos[i][j] = hina_first_pos[i][j];         /* 266 */
        }                                                               /* 267 */
    }                                                                   /* 268 */

    /* The cursor opens on the cell next to the empty slot. */
    hina_pzl_ctrl.csr_tate    = 2;                                      /* 270 */
    hina_pzl_ctrl.csr_yoko    = 1;                                      /* 271 */
    hina_pzl_ctrl.clear_flg   = 0;                                      /* 272 */
    hina_pzl_ctrl.exit_csr    = 1;                                      /* 273 */
    hina_pzl_ctrl.in_anim_flg = 0;                                      /* 274 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Always returns 1 -- the caller draws every frame. */
int HinaPuzzleMain(void)                                                /* 287 */
{                                                                       /* 288 */
    int res;

    res = 1;                                                            /* 291 */

    if (hina_pzl_ctrl.step == 0) {                                      /* 294 */
        SubTitleReq(0x1a);                                              /* 295 */
        HinaPuzzleDispInit();                                           /* 297 */
        hina_pzl_ctrl.step = 1;                                         /* 298 */
    }

    if (hina_pzl_ctrl.step == 1) {                                      /* 301 */
        HinaPuzzleSubOutAnimCheck();                                    /* 302 */

        /* Only take input once the incoming sub_step has finished fading. */
        if (hina_pzl_disp.sub_anim_step == 2) {                         /* 305 */
            if (hina_pad_func[hina_pzl_ctrl.sub_step] != NULL) {        /* 307 */
                hina_pad_func[hina_pzl_ctrl.sub_step]();                /* 308 */
            }
        }

        if (hina_pzl_ctrl.sub_step == 0) {                              /* 313 */
            if (hina_pzl_ctrl.timer > 0) {                              /* 314 */
                HinaPuzzleTimer();                                      /* 316 */
            }
        }
    }

    /* Leaving: wait out the black curtain and the last cue, then take the
     * finder's sound bank back off the puzzle's. */
    if ((hina_pzl_ctrl.step == 2) &&                                    /* 322 */
        (hina_pzl_disp.anim_step == 4) &&                               /* 323 */
        (SndBufIsPlaying(hina_pzl_ctrl.snd_id) == 0)) {                 /* 325 */
        SubTitleStop();                                                 /* 327 */
        SndBankRelease(GetPzlSndBankID());                              /* 330 */
        FinderBankSetup();                                              /* 334 */
        hina_pzl_ctrl.step = 3;                                         /* 335 */
    }

    if ((hina_pzl_ctrl.step == 3) &&                                    /* 340 */
        (FinderBankIsReady() != 0)) {                                   /* 341 */
        SetNextGPhase(GID_STORY_NORMAL);                                /* 342 */
    }

    return res;                                                         /* 348 */
}

/* The outgoing sub_step has finished fading out: swap in the requested one. */
static void HinaPuzzleSubOutAnimCheck(void)
{
    if (hina_pzl_disp.sub_anim_step == 4) {                             /* 358 */
        hina_pzl_disp.sub_anim_step = 0;                                /* 359 */
        hina_pzl_ctrl.sub_step = hina_pzl_ctrl.next_sub_step;           /* 360 */
    }
}

static void HinaPuzzleReqNextSubStep(char next_step)
{
    hina_pzl_ctrl.next_sub_step  = next_step;                           /* 373 */
    hina_pzl_disp.sub_anim_step  = 3;                                   /* 374 */
    hina_pzl_disp.sub_anim_timer = 0;                                   /* 375 */
}

/* --------------------------------------------------------------------------
 *  Solved when both cells of the top shelf hold doll 8.  Row 0 has only those
 *  two cells -- [0][0] and [0][3] are holes -- so this is the whole test.
 * ------------------------------------------------------------------------ */
static int HinaPuzzleClearCheck(void)                                   /* 383 */
{                                                                       /* 384 */
    int res;

    res = 0;

    if ((hina_pzl_ctrl.hina_pos[0][1] == 8) &&                          /* 391 */
        (hina_pzl_ctrl.hina_pos[0][2] == 8)) {
        hina_pzl_ctrl.clear_flg = 1;                                    /* 394 */
        PuzzleClear(PZL_ID_HINA);                                       /* 396 */
        HinaPuzzleReqNextSubStep(2);                                    /* 399 */
        res = 1;                                                        /* 401 */
    }

    return res;                                                         /* 405 */
}

/* --------------------------------------------------------------------------
 *  Board input.
 *
 *  All four directions do the same thing, written out four times: step until
 *  a cell that is neither the empty slot nor a hole, then reject it if it
 *  holds one of the two fixed dolls.  Failure restores the cursor and plays
 *  sample 3; success plays sample 0.
 * ------------------------------------------------------------------------ */
static void HinaPuzzleDollSelPad(void)                                  /* 410 */
{                                                                       /* 411 */
    int  i;
    char before_csr;

    if (((pad[0].rpt & 0x1000) != 0) || (GetPadAnalogRpt(0) != 0)) {    /* 418 */
        before_csr = hina_pzl_ctrl.csr_tate;                            /* 419 */

        if (hina_pzl_ctrl.csr_tate < 0) {                               /* 421 */
            return;
        }

        do {
            hina_pzl_ctrl.csr_tate--;                                   /* 422 */

            if (hina_pzl_ctrl.csr_tate < 0) {                           /* 425 */
                hina_pzl_ctrl.csr_tate = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 428 */
                return;                                                 /* 429 */
            }
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == -1) { /* 432 */
                hina_pzl_ctrl.csr_tate = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 435 */
                return;                                                 /* 436 */
            }
        } while (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                       [hina_pzl_ctrl.csr_yoko] == -2); /* 439 */

        for (i = 0; i < 2; i++) {                                       /* 443 */
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == no_move_hina[i]) { /* 444 */
                break;
            }
        }                                                               /* 447 */

        if (i >= 2) {                                                   /* 449 */
            /* GCC folded this call, and the two below it, into the one at
             * the bottom of the RIGHT block -- see line 589. */
            hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                               0x3200, 0x1000, 0,
                                               (SND_3D_SET *)0);        /* 451 */
            return;
        }

        hina_pzl_ctrl.csr_tate = before_csr;
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 457 */
        return;                                                         /* 458 */
    } else if (((pad[0].rpt & 0x4000) != 0) || (GetPadAnalogRpt(1) != 0)) { /* 464 */
        before_csr = hina_pzl_ctrl.csr_tate;                            /* 465 */

        if (hina_pzl_ctrl.csr_tate >= 4) {                              /* 467 */
            return;
        }

        do {
            hina_pzl_ctrl.csr_tate++;                                   /* 468 */

            if (hina_pzl_ctrl.csr_tate > 3) {                           /* 471 */
                hina_pzl_ctrl.csr_tate = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 474 */
                return;                                                 /* 475 */
            }
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == -1) { /* 478 */
                hina_pzl_ctrl.csr_tate = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 481 */
                return;                                                 /* 482 */
            }
        } while (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                       [hina_pzl_ctrl.csr_yoko] == -2); /* 485 */

        for (i = 0; i < 2; i++) {                                       /* 489 */
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == no_move_hina[i]) { /* 490 */
                break;
            }
        }                                                               /* 493 */

        if (i >= 2) {                                                   /* 495 */
            hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                               0x3200, 0x1000, 0,
                                               (SND_3D_SET *)0);        /* 497 */
            return;
        }

        hina_pzl_ctrl.csr_tate = before_csr;
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 503 */
        return;                                                         /* 504 */
    } else if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0)) { /* 510 */
        before_csr = hina_pzl_ctrl.csr_yoko;                            /* 511 */

        if (hina_pzl_ctrl.csr_yoko < 0) {                               /* 513 */
            return;
        }

        do {
            hina_pzl_ctrl.csr_yoko--;                                   /* 514 */

            if (hina_pzl_ctrl.csr_yoko < 0) {                           /* 517 */
                hina_pzl_ctrl.csr_yoko = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 520 */
                return;                                                 /* 521 */
            }
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == -1) { /* 524 */
                hina_pzl_ctrl.csr_yoko = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 527 */
                return;                                                 /* 528 */
            }
        } while (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                       [hina_pzl_ctrl.csr_yoko] == -2); /* 531 */

        for (i = 0; i < 2; i++) {                                       /* 535 */
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == no_move_hina[i]) { /* 536 */
                break;
            }
        }                                                               /* 539 */

        if (i >= 2) {                                                   /* 541 */
            hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                               0x3200, 0x1000, 0,
                                               (SND_3D_SET *)0);        /* 543 */
            return;
        }

        hina_pzl_ctrl.csr_yoko = before_csr;
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 549 */
        return;                                                         /* 550 */
    } else if (((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) { /* 556 */
        before_csr = hina_pzl_ctrl.csr_yoko;                            /* 557 */

        if (hina_pzl_ctrl.csr_yoko >= 4) {                              /* 559 */
            return;
        }

        do {
            hina_pzl_ctrl.csr_yoko++;                                   /* 560 */

            if (hina_pzl_ctrl.csr_yoko > 3) {                           /* 563 */
                hina_pzl_ctrl.csr_yoko = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 566 */
                return;                                                 /* 567 */
            }
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == -1) { /* 570 */
                hina_pzl_ctrl.csr_yoko = before_csr;
                hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                                   0x3200, 0x1000, 0,
                                                   (SND_3D_SET *)0);    /* 573 */
                return;                                                 /* 574 */
            }
        } while (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                       [hina_pzl_ctrl.csr_yoko] == -2); /* 577 */

        for (i = 0; i < 2; i++) {                                       /* 581 */
            if (hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate]
                                      [hina_pzl_ctrl.csr_yoko] == no_move_hina[i]) { /* 582 */
                break;
            }
        }                                                               /* 585 */

        if (i >= 2) {                                                   /* 587 */
            hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                               0x3200, 0x1000, 0,
                                               (SND_3D_SET *)0);        /* 589 */
            return;                                                     /* 590 */
        }

        hina_pzl_ctrl.csr_yoko = before_csr;
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 595 */
        return;                                                         /* 596 */
    } else if (*paddat[0] == 1) {                                       /* 602 */
        HinaPuzzleDollPosChange();                                      /* 604 */
    } else if (*paddat[1] == 1) {                                       /* 607 */
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 609 */
        HinaPuzzleReqNextSubStep(1);                                    /* 613 */
    }
}

/* Sample 1 for a move, 4 for the move that solves it, 3 for a refusal. */
static void HinaPuzzleDollPosChange(void)                               /* 622 */
{                                                                       /* 623 */
    char blank_tate;
    char blank_yoko;

    HinaPuzzleGetBlankPos(&blank_tate, &blank_yoko);                    /* 628 */

    if (HinaPuzzleChangeCheck((int)hina_pzl_ctrl.csr_tate,
                              (int)hina_pzl_ctrl.csr_yoko) != 0) {      /* 630 */
        HinaPuzzleChangeDataPos((int)hina_pzl_ctrl.csr_tate,
                                (int)hina_pzl_ctrl.csr_yoko,
                                (int)blank_tate, (int)blank_yoko);      /* 631 */

        /* The cursor follows the doll, so it now sits on the empty slot. */
        hina_pzl_ctrl.csr_tate = blank_tate;                            /* 632 */
        hina_pzl_ctrl.csr_yoko = blank_yoko;                            /* 633 */

        if (HinaPuzzleClearCheck() != 0) {                              /* 636 */
            hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 4, 0, 0,
                                               0x3200, 0x1000, 0,
                                               (SND_3D_SET *)0);        /* 638 */
        } else {
            hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 1, 0, 0,
                                               0x3200, 0x1000, 0,
                                               (SND_3D_SET *)0);        /* 642 */
        }
    } else {
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 648 */
    }
}

/* Adjacent to the empty slot -- orthogonally, one cell. */
static int HinaPuzzleChangeCheck(int csr_tate, int csr_yoko)            /* 661 */
{                                                                       /* 662 */
    int  res;
    char blank_tate;
    char blank_yoko;

    res = 0;

    HinaPuzzleGetBlankPos(&blank_tate, &blank_yoko);                    /* 670 */

    if (blank_tate == csr_tate) {                                       /* 673 */
        if ((blank_yoko - 1 == csr_yoko) || (blank_yoko + 1 == csr_yoko)) { /* 674 */
            res = 1;                                                    /* 676 */
        }
    } else if (blank_yoko == csr_yoko) {                                /* 679 */
        if ((blank_tate - 1 == csr_tate) || (blank_tate + 1 == csr_tate)) { /* 680 */
            res = 1;                                                    /* 682 */
        }
    }

    return res;                                                         /* 687 */
}

static void HinaPuzzleGetBlankPos(char *tate, char *yoko)               /* 699 */
{                                                                       /* 700 */
    int  i;
    int  j;
    char found_flg;

    found_flg = 0;                                                      /* 702 */

    for (i = 0; i < 4; i++) {                                           /* 703 */
        for (j = 0; j < 4; j++) {                                       /* 705 */
            if (hina_pzl_ctrl.hina_pos[i][j] == -2) {                   /* 706 */
                *tate     = (char)i;                                    /* 707 */
                *yoko     = (char)j;                                    /* 708 */
                found_flg = 1;                                          /* 709 */
                break;
            }
        }                                                               /* 711 */

        if (found_flg != 0) {                                           /* 713 */
            break;
        }
    }                                                                   /* 716 */
}

static void HinaPuzzleChangeDataPos(int tate_a, int yoko_a,
                                    int tate_b, int yoko_b)             /* 727 */
{                                                                       /* 728 */
    int tmp;

    if (((u_int)tate_a > 3) || ((u_int)yoko_a > 3) ||
        ((u_int)tate_b > 3) || ((u_int)yoko_b > 3)) {                   /* 732 */
        PRINT_ASSERT("Error! HinaPuzzleChangeDataPos");
    }

    tmp = hina_pzl_ctrl.hina_pos[tate_a][yoko_a];                       /* 736 */
    hina_pzl_ctrl.hina_pos[tate_a][yoko_a] =
        hina_pzl_ctrl.hina_pos[tate_b][yoko_b];                         /* 740 */
    hina_pzl_ctrl.hina_pos[tate_b][yoko_b] = tmp;                       /* 741 */
}                                                                       /* 742 */

/* --------------------------------------------------------------------------
 *  The quit prompt (sub_step 1), and the two terminal states (2 and 3).
 * ------------------------------------------------------------------------ */
static void HinaPuzzleExitSelPad(void)                                  /* 749 */
{                                                                       /* 750 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 754 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {    /* 761 */
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 763 */
        hina_pzl_ctrl.exit_csr ^= 1;                                    /* 765 */
    } else if (*paddat[0] == 1) {                                       /* 768 */
        if (hina_pzl_ctrl.exit_csr == 0) {                              /* 771 */
            HinaPuzzleExitReq();                                        /* 772 */
        } else {
            HinaPuzzleReqNextSubStep(0);                                /* 776 */
        }
    } else if (*paddat[1] == 1) {                                       /* 780 */
        hina_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                           0x3200, 0x1000, 0,
                                           (SND_3D_SET *)0);            /* 782 */
        HinaPuzzleReqNextSubStep(0);                                    /* 784 */
    }
}

/* Either button leaves; the puzzle is over either way. */
static void HinaPuzzleClearPad(void)                                    /* 794 */
{                                                                       /* 795 */
    if (*paddat[0] == 1) {                                              /* 799 */
        HinaPuzzleExitReq();                                            /* 801 */
    } else if (*paddat[1] == 1) {                                       /* 804 */
        HinaPuzzleExitReq();                                            /* 806 */
    }
}

static void HinaPuzzleTimer(void)                                       /* 815 */
{                                                                       /* 816 */
    if (hina_pzl_ctrl.clear_flg == 0) {                                 /* 819 */
        hina_pzl_ctrl.timer--;                                          /* 821 */

        if (hina_pzl_ctrl.timer < 1) {                                  /* 822 */
            hina_pzl_ctrl.timer = 0;                                    /* 823 */
            HinaPuzzleReqNextSubStep(3);                                /* 825 */
        }
    }
}

static void HinaPuzzleExitReq(void)
{
    hina_pzl_ctrl.step       = 2;                                       /* 838 */
    hina_pzl_disp.anim_step  = 3;                                       /* 839 */
    hina_pzl_disp.anim_timer = 0;                                       /* 840 */
}

/* ==========================================================================
 *  Display
 * ======================================================================== */

/* The smoke overlay carries its phase over from the cross-fade, so it does
 * not restart when the puzzle phase takes over. */
static void HinaPuzzleDispInit(void)
{
    hina_pzl_disp.anim_step        = 0;                                 /* 854 */
    hina_pzl_disp.anim_timer       = 0;                                 /* 855 */
    hina_pzl_disp.sub_anim_step    = 0;                                 /* 856 */
    hina_pzl_disp.sub_anim_timer   = 0;                                 /* 857 */
    hina_pzl_disp.csr_anim_timer   = 0;                                 /* 858 */
    hina_pzl_disp.smoke_anim_timer = hina_pzl_cross_disp.anim_timer;    /* 859 */
    hina_pzl_disp.stand_anim_timer = 0;                                 /* 860 */
}

void HinaPuzzleDispMain(void)                                           /* 870 */
{                                                                       /* 871 */
    u_char alpha;

    if ((u_char)(hina_pzl_ctrl.step - 1) < 2) {                         /* 877 */
        if (hina_pzl_disp.anim_step != 4) {                             /* 878 */
            HinaPuzzleBgDisp(0, 0, 0x80);                               /* 880 */
            HinaPuzzleSmokeScreenDisp(
                0, 0,
                HinaPuzzleSmokeScreenAnim(&hina_pzl_disp.smoke_anim_timer)); /* 883 */
            HinaPuzzleStandFleaDisp(0, 0, 0x80);                        /* 886 */
            HinaPuzzleDollsDisp(0, 0, 0x80);                            /* 889 */

            alpha = HinaPuzzleModeAnimCtrl();                           /* 891 */

            HinaPuzzleRemainderTimeDisp(0, 0, alpha);                   /* 894 */

            SubTitleMain(1);                                            /* 897 */

            if (hina_disp_func[hina_pzl_ctrl.sub_step] != NULL) {       /* 899 */
                hina_disp_func[hina_pzl_ctrl.sub_step](0, 0, alpha);    /* 900 */
            }
        }

        HinaPuzzleBlackBgDisp();                                        /* 906 */
    }
}

/* --------------------------------------------------------------------------
 *  The sub_step cross-fade: 7 frames in, 5 frames out, and a settled state
 *  that also raises in_anim_flg -- which is what tells the timer readout to
 *  stop dimming with the rest of the overlay.
 * ------------------------------------------------------------------------ */
static u_char HinaPuzzleModeAnimCtrl(void)                              /* 916 */
{                                                                       /* 917 */
    static const ALPHA_ANIM_TBL in_alpha_tbl[2] =               /* rdata 3b8ef0 */
    {
        { 0, 128, 0, 7 },
        { -1, -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL out_alpha_tbl[2] =              /* rdata 3b8f00 */
    {
        { 128, 0, 0, 5 },
        { -1, -1, -1, -1 },
    };
    u_char alpha;

    alpha = 0x80;                                                       /* 930 */

    if (hina_pzl_disp.sub_anim_step == 0) {                             /* 932 */
        hina_pzl_disp.sub_anim_timer = 0;                               /* 933 */
        hina_pzl_disp.sub_anim_step  = 1;                               /* 934 */
    }

    if (hina_pzl_disp.sub_anim_step == 1) {                             /* 938 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)in_alpha_tbl,
                                    (int)hina_pzl_disp.sub_anim_timer); /* 940 */
        hina_pzl_disp.sub_anim_timer++;                                 /* 942 */

        if (hina_pzl_disp.sub_anim_timer >= 7) {                        /* 944 */
            hina_pzl_disp.sub_anim_step  = 2;                           /* 946 */
            hina_pzl_disp.sub_anim_timer = 0;                           /* 947 */
        }                                                               /* 949 */
    } else if (hina_pzl_disp.sub_anim_step == 2) {
        hina_pzl_ctrl.in_anim_flg = 1;                                  /* 951 */
    } else if (hina_pzl_disp.sub_anim_step == 3) {                      /* 953 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)out_alpha_tbl,
                                    (int)hina_pzl_disp.sub_anim_timer); /* 955 */
        hina_pzl_disp.sub_anim_timer++;                                 /* 957 */

        if (hina_pzl_disp.sub_anim_timer >= 5) {                        /* 959 */
            hina_pzl_disp.sub_anim_timer = 0;                           /* 960 */
            hina_pzl_disp.sub_anim_step  = 4;                           /* 961 */
        }                                                               /* 964 */
    } else if (hina_pzl_disp.sub_anim_step == 4) {
        alpha = 0;
    } else {
        PRINT_ASSERT("Error! HinaPuzzleModeAnimCtrl");                  /* 969 */
    }

    return alpha;                                                       /* 973 */
}

/* -------------------------------------------------------------------------- */

static void HinaPuzzleDollSelDisp(int off_x, int off_y, u_char alpha)   /* 982 */
{                                                                       /* 983 */
    HinaPuzzleCaptionDisp(0, 0, alpha);                                 /* 987 */
}

static void HinaPuzzleExitSelDisp(int off_x, int off_y, u_char alpha)   /* 997 */
{                                                                       /* 998 */
    HinaPuzzleExitConfWinDisp(off_x, off_y, alpha);                     /* 1002 */
}

static void HinaPuzzleClearDisp(int off_x, int off_y, u_char alpha)     /* 1011 */
{                                                                       /* 1012 */
    HinaPuzzleClearWinDisp(off_x, off_y, alpha);                        /* 1016 */
}

static void HinaPuzzleTimeOverDisp(int off_x, int off_y, u_char alpha)  /* 1026 */
{                                                                       /* 1027 */
    HinaPuzzleTimeOverWinDisp(off_x, off_y, alpha);                     /* 1031 */
}

/* The shelf is two sprites side by side, 510 + 130 pixels. */
static void HinaPuzzleBgDisp(int off_x, int off_y, u_char alpha)        /* 1040 */
{                                                                       /* 1041 */
    DISP_SPRT bg_ds;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1047 */

    for (i = 0; i < 2; i++) {
        CopySprDToSpr(&bg_ds, &puzzle_hina_tex[i]);                     /* 1050 */
        bg_ds.x     = bg_ds.x + (float)off_x;                           /* 1051 */
        bg_ds.y     = bg_ds.y + (float)off_y;                           /* 1052 */
        bg_ds.alpha = (u_char)((bg_ds.alpha * alpha) >> 7);             /* 1053 */
        DispSprD(&bg_ds);                                               /* 1054 */
    }                                                                   /* 1055 */
}

/* The two candle stands, pulsing between grey 44 and white. */
static void HinaPuzzleStandFleaDisp(int off_x, int off_y, u_char alpha) /* 1073 */
{                                                                       /* 1074 */
    DISP_SPRT stand_ds;
    u_char    rgb;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1074 */

    rgb = HinaPuzzleStandFleaAnim();                                    /* 1076 */

    if (hina_pzl_ctrl.timer > 0) {                                      /* 1079 */
        for (i = 0; i < 2; i++) {                                       /* 1081 */
            CopySprDToSpr(&stand_ds, &puzzle_hina_tex[0x2b + i]);       /* 1082 */
            stand_ds.x     = stand_ds.x + (float)off_x;                 /* 1083 */
            stand_ds.y     = stand_ds.y + (float)off_y;
            stand_ds.alpha = (u_char)((stand_ds.alpha * alpha) >> 7);   /* 1084 */
            stand_ds.r     = rgb;                                       /* 1085 */
            stand_ds.g     = rgb;
            stand_ds.b     = rgb;
            DispSprD(&stand_ds);                                        /* 1086 */
        }                                                               /* 1087 */
    }
}

static u_char HinaPuzzleStandFleaAnim(void)                             /* 1097 */
{                                                                       /* 1098 */
    static const RGB_ANIM_TBL flea_rgb_tbl[3] =                 /* rdata 3b8f30 */
    {
        {  44, 128,  0, 40 },
        { 128,  44, 40, 80 },
        {  -1,  -1, -1, -1 },
    };
    u_char rgb;

    rgb = Anim2D_CalcNowRGB((RGB_ANIM_TBL *)flea_rgb_tbl,
                            (int)hina_pzl_disp.stand_anim_timer);       /* 1110 */

    hina_pzl_disp.stand_anim_timer++;                                   /* 1112 */

    if (hina_pzl_disp.stand_anim_timer > 79) {                          /* 1114 */
        hina_pzl_disp.stand_anim_timer = 0;                             /* 1115 */
    }

    return rgb;                                                         /* 1119 */
}

/* --------------------------------------------------------------------------
 *  The glow behind the selected doll.  Its sprite is the doll number plus
 *  nine, and it is only drawn for a doll that can actually be moved.
 * ------------------------------------------------------------------------ */
static void HinaPuzzleDollsFleaDisp(int off_x, int off_y, u_char alpha) /* 1138 */
{                                                                       /* 1139 */
    static const float scale_tbl[4] =                           /* rdata 3b8f48 */
    {
        0.93f, 0.96f, 1.0f, 1.03f,
    };
    DISP_SPRT flea_ds;
    u_char    rgb;
    int       i;
    int       no;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1139 */

    rgb = HinaPuzzleDollsFleaAnim();                                    /* 1141 */

    no = hina_pzl_ctrl.hina_pos[hina_pzl_ctrl.csr_tate][hina_pzl_ctrl.csr_yoko];

    if ((no != -1) && (no != -2)) {                                     /* 1143 */
        for (i = 0; i < 2; i++) {                                       /* 1146 */
            if (no == no_move_hina[i]) {                                /* 1147 */
                break;
            }
        }                                                               /* 1150 */

        if (i >= 2) {                                                   /* 1152 */
            CopySprDToSpr(&flea_ds, &puzzle_hina_tex[no + 9]);          /* 1154 */
            flea_ds.x  = hina_flea_pos_x[hina_pzl_ctrl.csr_tate]
                                        [hina_pzl_ctrl.csr_yoko] + (float)off_x; /* 1155 */
            flea_ds.y  = hina_flea_pos_y[hina_pzl_ctrl.csr_tate]
                                        [hina_pzl_ctrl.csr_yoko] + (float)off_y;
            flea_ds.alpha = (u_char)((flea_ds.alpha * alpha) >> 7);     /* 1157 */
            flea_ds.r     = rgb;                                        /* 1158 */
            flea_ds.g     = rgb;
            flea_ds.b     = rgb;
            /* 0x48 is additive -- the glow lightens whatever is under it. */
            flea_ds.alphar = 0x48;                                      /* 1159 */
            flea_ds.scw = scale_tbl[hina_pzl_ctrl.csr_tate];            /* 1160 */
            flea_ds.sch = flea_ds.scw;
            flea_ds.csx = flea_ds.x + (float)flea_ds.w * 0.5f;
            flea_ds.csy = flea_ds.y + (float)flea_ds.h * 0.5f;
            DispSprD(&flea_ds);                                         /* 1161 */
        }
    }
}

static u_char HinaPuzzleDollsFleaAnim(void)                             /* 1172 */
{                                                                       /* 1173 */
    static const RGB_ANIM_TBL flea_rgb_tbl[3] =                 /* rdata 3b8f58 */
    {
        {  64, 115,  0, 30 },
        { 115,  64, 30, 60 },
        {  -1,  -1, -1, -1 },
    };
    u_char rgb;

    rgb = Anim2D_CalcNowRGB((RGB_ANIM_TBL *)flea_rgb_tbl,
                            (int)hina_pzl_disp.csr_anim_timer);         /* 1185 */

    hina_pzl_disp.csr_anim_timer++;                                     /* 1187 */

    if (hina_pzl_disp.csr_anim_timer > 59) {                            /* 1189 */
        hina_pzl_disp.csr_anim_timer = 0;                               /* 1190 */
    }

    return rgb;                                                         /* 1194 */
}

/* The glow is drawn from inside this loop, so it lands under its own doll
 * rather than on top of the whole board. */
static void HinaPuzzleDollsDisp(int off_x, int off_y, u_char alpha)     /* 1202 */
{                                                                       /* 1203 */
    static const float scale_tbl[4] =                           /* rdata 3b8f70 */
    {
        0.93f, 0.96f, 1.0f, 1.03f,
    };
    DISP_SPRT doll_ds;
    int       i;
    int       j;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1213 */

    for (i = 0; i < 4; i++) {                                           /* 1215 */
        for (j = 0; j < 4; j++) {                                       /* 1216 */
            if ((hina_pzl_ctrl.hina_pos[i][j] != -1) &&
                (hina_pzl_ctrl.hina_pos[i][j] != -2)) {                 /* 1217 */
                if ((hina_pzl_ctrl.csr_tate == i) &&
                    (hina_pzl_ctrl.csr_yoko == j) &&
                    (hina_pzl_ctrl.timer > 0)) {                        /* 1221 */
                    HinaPuzzleDollsFleaDisp(off_x, off_y, alpha);       /* 1223 */
                }

                CopySprDToSpr(&doll_ds,
                              &puzzle_hina_tex[hina_pzl_ctrl.hina_pos[i][j]]); /* 1225 */
                doll_ds.x   = hina_pos_x[i][j] + (float)off_x;          /* 1230 */
                doll_ds.y   = hina_pos_y[i][j] + (float)off_y;          /* 1231 */
                doll_ds.csx = doll_ds.x + (float)doll_ds.w * 0.5f;      /* 1232 */
                doll_ds.csy = doll_ds.y + (float)doll_ds.h * 0.5f;      /* 1233 */
                /* Lower shelves are nearer the camera, so they draw larger. */
                doll_ds.scw = scale_tbl[i];                             /* 1234 */
                doll_ds.sch = doll_ds.scw;
                doll_ds.alpha = (u_char)((doll_ds.alpha * alpha) >> 7); /* 1236 */
                DispSprD(&doll_ds);                                     /* 1237 */
            }
        }
    }
}

/* One 254x254 sprite stretched over the whole screen -- the haze that sits
 * between the shelf and the dolls. */
static void HinaPuzzleSmokeScreenDisp(int off_x, int off_y, u_char alpha) /* 1253 */
{                                                                       /* 1254 */
    DISP_SPRT smoke_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1257 */

    CopySprDToSpr(&smoke_ds, &puzzle_hina_tex[0x2a]);                   /* 1258 */
    smoke_ds.x      = smoke_ds.x + (float)off_x;                        /* 1259 */
    smoke_ds.y      = smoke_ds.y + (float)off_y;
    smoke_ds.csx    = smoke_ds.x;
    smoke_ds.csy    = smoke_ds.y;
    smoke_ds.scw    = 640.0f / (float)smoke_ds.w;                       /* 1260 */
    smoke_ds.sch    = 448.0f / (float)smoke_ds.h;
    smoke_ds.alphar = 0x46;
    smoke_ds.alpha  = alpha;                                            /* 1261 */
    DispSprD(&smoke_ds);                                                /* 1262 */
}

/* A 120-frame loop, and the +102 floor keeps the haze from ever clearing. */
static u_char HinaPuzzleSmokeScreenAnim(short *timer)                   /* 1327 */
{                                                                       /* 1328 */
    static const ALPHA_ANIM_TBL smoke_alpha_tbl[3] =            /* rdata 3b8f80 */
    {
        {  0, 38,  0,  60 },
        { 38,  0, 60, 120 },
        { -1, -1, -1,  -1 },
    };
    u_char alpha;

    alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)smoke_alpha_tbl, (int)*timer); /* 1330 */

    (*timer)++;                                                         /* 1332 */

    if (*timer > 119) {                                                 /* 1333 */
        *timer = 0;                                                     /* 1336 */
    }

    return alpha + 102;                                                 /* 1339 */
}

/* --------------------------------------------------------------------------
 *  The countdown readout: a two-piece plate and two two-digit fields.
 *
 *  The left field is timer / 1800 and the right (timer % 1800) / 30, so the
 *  30 real seconds on the clock are shown as a minute counting down.  PAL
 *  divides by 1500 and 25 instead, which lands on the same reading.
 *
 *  in_anim_flg pins the plate to full brightness once the board has faded in
 *  for the first time, so it stops dimming with the rest of the overlay.
 * ------------------------------------------------------------------------ */
static void HinaPuzzleRemainderTimeDisp(int off_x, int off_y, u_char alpha) /* 1348 */
{                                                                       /* 1349 */
    DISP_SPRT time_ds;
    u_char    num_alpha;

    num_alpha = alpha;                                                  /* 1354 */

    if (hina_pzl_ctrl.in_anim_flg != 0) {                               /* 1356 */
        alpha = 0x80;
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1361 */

    CopySprDToSpr(&time_ds, &puzzle_hina_tex[0x12]);                    /* 1364 */
    time_ds.x     = time_ds.x + (float)off_x;                           /* 1365 */
    time_ds.y     = time_ds.y + (float)off_y;
    time_ds.alpha = (u_char)((time_ds.alpha * alpha) >> 7);             /* 1366 */
    DispSprD(&time_ds);                                                 /* 1367 */

    CopySprDToSpr(&time_ds, &puzzle_hina_tex[0x13]);                    /* 1370 */
    time_ds.x     = time_ds.x + (float)off_x;                           /* 1371 */
    time_ds.y     = time_ds.y + (float)off_y;
    time_ds.alpha = (u_char)((time_ds.alpha * alpha) >> 7);             /* 1372 */
    DispSprD(&time_ds);                                                 /* 1373 */

    if (GetPALMode() != 0) {                                            /* 1376 */
        if (hina_pzl_ctrl.timer < 251) {                                /* 1377 */
            num_alpha = HinaPuzzleWarningTimeAnim(hina_pzl_ctrl.timer); /* 1378 */
        }

        HinaPuzzleNumberDisp(hina_pzl_ctrl.timer / 1500, 2,
                             486.0f, 33.0f, num_alpha, 0xa0, 1);        /* 1384 */
        HinaPuzzleNumberDisp((hina_pzl_ctrl.timer % 1500) / 25, 2,
                             542.0f, 33.0f, num_alpha, 0xa0, 1);        /* 1388 */
    } else {
        if (hina_pzl_ctrl.timer < 301) {                                /* 1391 */
            num_alpha = HinaPuzzleWarningTimeAnim(hina_pzl_ctrl.timer); /* 1392 */
        }

        HinaPuzzleNumberDisp(hina_pzl_ctrl.timer / 1800, 2,
                             486.0f, 33.0f, num_alpha, 0xa0, 1);        /* 1398 */
        HinaPuzzleNumberDisp((hina_pzl_ctrl.timer % 1800) / 30, 2,
                             542.0f, 33.0f, num_alpha, 0xa0, 1);        /* 1402 */
    }
}

static void HinaPuzzleCaptionDisp(int off_x, int off_y, u_char alpha)   /* 1419 */
{                                                                       /* 1420 */
    DISP_SPRT time_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1423 */

    CopySprDToSpr(&time_ds, &puzzle_hina_tex[0x28]);                    /* 1424 */
    time_ds.x     = time_ds.x + (float)off_x;                           /* 1425 */
    time_ds.y     = time_ds.y + (float)off_y;
    time_ds.alpha = (u_char)((time_ds.alpha * alpha) >> 7);             /* 1426 */
    DispSprD(&time_ds);                                                 /* 1429 */

    CopySprDToSpr(&time_ds, &puzzle_hina_tex[0x29]);                    /* 1430 */
    time_ds.x     = time_ds.x + (float)off_x;                           /* 1431 */
    time_ds.y     = time_ds.y + (float)off_y;
    time_ds.alpha = (u_char)((time_ds.alpha * alpha) >> 7);             /* 1432 */
    DispSprD(&time_ds);
}

/* --------------------------------------------------------------------------
 *  `num` digits of `data`, most significant first, 18 pixels apart.
 *  zero_flg == 1 prints leading zeroes; otherwise a digit only starts
 *  printing once a non-zero one has been seen -- except that a single-digit
 *  zero still prints.
 *
 *  GCC kept the x offset as an induction variable rather than a named local,
 *  so the (num - i) * 0x12 below is the port's spelling of it; functions.txt
 *  lists exactly the five locals used here and no sixth.
 * ------------------------------------------------------------------------ */
void HinaPuzzleNumberDisp(int data, int num, float x, float y, u_char alpha,
                          int pri, u_char zero_flg)                     /* 1447 */
{                                                                       /* 1448 */
    int    i;
    int    j;
    int    tmp;
    int    ten_tmp;
    u_char set_flg;

    ten_tmp = 1;                                                        /* 1460 */
    set_flg = (zero_flg == 1);                                          /* 1462 */

    for (i = num; 0 < i; i--) {                                         /* 1467 */
        ten_tmp = 1;                                                    /* 1468 */

        for (j = i - 1; 0 < j; j--) {                                   /* 1469 */
            ten_tmp = ten_tmp * 10;                                     /* 1470 */
        }                                                               /* 1471 */

        if (data / ten_tmp != 0) {                                      /* 1473 */
            set_flg = 1;
        }

        if ((zero_flg == 0) && (data == 0) && (i == 1)) {               /* 1477 */
            set_flg = 1;
        }

        if (i == 1) {                                                   /* 1482 */
            tmp = data % 10;                                            /* 1483 */
        } else {
            tmp = (data / ten_tmp) % 10;                                /* 1486 */
        }

        if (set_flg == 1) {                                             /* 1489 */
            HinaPuzzleNumberDisp_One(tmp, x + (float)((num - i) * 0x12),
                                     y, alpha, pri);                    /* 1491 */
        }                                                               /* 1494 */
    }                                                                   /* 1496 */
}

/* White digits normally, red once the clock is inside the last 15 seconds. */
void HinaPuzzleNumberDisp_One(int data, float x, float y, u_char alpha, int pri) /* 1510 */
{                                                                       /* 1511 */
    DISP_SPRT num_ds;

    if (data > 9) {                                                     /* 1515 */
        PRINT_ASSERT("Error!! HinaPuzzleNumberDisp_One()");             /* 1516 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1519 */

    if (GetPALMode() != 0) {                                            /* 1523 */
        if (hina_pzl_ctrl.timer > 750) {                                /* 1524 */
            CopySprDToSpr(&num_ds, &puzzle_hina_tex[data + 0x14]);      /* 1529 */
        } else {
            /* GCC cross-jumped this onto the NTSC arm's copy at 1534. */
            CopySprDToSpr(&num_ds, &puzzle_hina_tex[data + 0x1e]);
        }
    } else {
        if (hina_pzl_ctrl.timer > 900) {                                /* 1533 */
            CopySprDToSpr(&num_ds, &puzzle_hina_tex[data + 0x14]);      /* 1538 */
        } else {
            CopySprDToSpr(&num_ds, &puzzle_hina_tex[data + 0x1e]);      /* 1534 */
        }
    }

    num_ds.x     = x;                                                   /* 1542 */
    num_ds.y     = y;
    num_ds.alpha = alpha;                                               /* 1543 */
    num_ds.pri   = pri;                                                 /* 1544 */
    num_ds.z     = 0xfffff - (pri & 0xfffff);
    DispSprD(&num_ds);                                                  /* 1546 */
}

/* Under five seconds the digits pulse once a displayed second; under one they
 * stop pulsing and stay lit. */
static u_char HinaPuzzleWarningTimeAnim(int timer)                      /* 1574 */
{
    static const ALPHA_ANIM_TBL num_alpha_tbl[3] =              /* rdata 3b8fe0 */
    {
        {   0, 128,  0,  4 },
        { 128, 128,  4, 26 },
        { 128,   0, 26, 30 },
    };
    static const ALPHA_ANIM_TBL num_alpha_tbl_pal[3] =          /* rdata 3b8ff8 */
    {
        {   0, 128,  0,  4 },
        { 128, 128,  4, 21 },
        { 128,   0, 21, 30 },
    };
    u_char alpha;

    if (GetPALMode() == 0) {                                            /* 1575 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)num_alpha_tbl, timer % 30); /* 1576 */

        if (timer < 26) {                                               /* 1578 */
            alpha = 0x80;                                               /* 1579 */
        }
    } else {
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)num_alpha_tbl_pal, timer % 25); /* 1583 */

        if (timer < 21) {                                               /* 1585 */
            alpha = 0x80;
        }
    }

    return alpha;                                                       /* 1591 */
}

/* --------------------------------------------------------------------------
 *  The black curtain.  Drawn every frame of the puzzle at alpha 0, so the
 *  only thing it ever does is fade the screen out on the way to step 3.
 * ------------------------------------------------------------------------ */
static void HinaPuzzleBlackBgDisp(void)                                 /* 1596 */
{                                                                       /* 1597 */
    SQAR_DAT  hina_pzl_bg = { 640, 448, 0, 0, 0xa0, 0, 0, 0, 0 };       /* 1600 */
    DISP_SQAR dsq;
    u_char    alpha;

    alpha = 0;

    HinaPuzzleAnim(&hina_pzl_disp.anim_step, &hina_pzl_disp.anim_timer,
                   &alpha);                                             /* 1604 */

    CopySqrDToSqr(&dsq, &hina_pzl_bg);                                  /* 1608 */
    dsq.alpha = alpha;                                                  /* 1609 */
    DispSqrD(&dsq);                                                     /* 1610 */
}

static void HinaPuzzleAnim(char *anim_step, short *anim_timer, u_char *alpha) /* 1629 */
{                                                                       /* 1630 */
    static const ALPHA_ANIM_TBL hina_out_alpha[2] =             /* rdata 3b9028 */
    {
        {  0, 128,  0, 30 },
        { -1,  -1, -1, -1 },
    };

    if (*anim_step == 0) {                                              /* 1630 */
        *anim_timer = 0;                                                /* 1631 */
        *anim_step  = 1;                                                /* 1632 */
    }

    if (*anim_timer < 0) {                                              /* 1636 */
        PRINT_WARNING("Warning!! HinaPuzzleAnim()");                    /* 1637 */
    }

    if (*anim_step == 1) {                                              /* 1642 */
        *alpha     = 0;                                                 /* 1644 */
        *anim_step = 2;                                                 /* 1646 */
    } else if (*anim_step == 2) {
        *alpha = 0;                                                     /* 1649 */
    } else if (*anim_step == 3) {
        *alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)hina_out_alpha,
                                     (int)*anim_timer);                 /* 1652 */
        (*anim_timer)++;                                                /* 1654 */

        if (*anim_timer >= 30) {                                        /* 1656 */
            *anim_step = 4;                                             /* 1657 */
        }
    }
}                                                                       /* 1661 */

/* -------------------------------------------------------------------------- */

static void HinaPuzzleExitConfWinDisp(int off_x, int off_y, u_char alpha) /* 1672 */
{                                                                       /* 1673 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x44);                                           /* 1677 */

    HinaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1680 */

    DrawCmnSelCsr(0, (float)(hina_pzl_ctrl.exit_csr * 0xcf + off_x + 0x9b),
                  (float)(off_y + 0x184), alpha, 0.0f, 0);              /* 1684 */
    DrawCmnSelYes(0, (float)(off_x + 0x99), (float)(off_y + 0x186), alpha); /* 1687 */
    DrawCmnSelNo(0, (float)(off_x + 0x169), (float)(off_y + 0x186), alpha); /* 1688 */

    PrintMsg(0x44, 4, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1690 */
}

static void HinaPuzzleClearWinDisp(int off_x, int off_y, u_char alpha)  /* 1700 */
{                                                                       /* 1701 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x44);                                           /* 1705 */
    HinaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1708 */
    PrintMsg(0x44, 3, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1710 */
}

static void HinaPuzzleTimeOverWinDisp(int off_x, int off_y, u_char alpha) /* 1720 */
{                                                                       /* 1721 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x44);                                           /* 1725 */
    HinaPuzzleCmnWinDisp(off_x, off_y, alpha);                          /* 1728 */
    PrintMsg(0x44, 1, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1730 */
}

static void HinaPuzzleCmnWinDisp(int off_x, int off_y, u_char alpha)    /* 1740 */
{                                                                       /* 1741 */
    MSG_WIN_DAT win_dat;

    SetMsgWinDefData(&win_dat, 0x44);                                   /* 1744 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h, alpha, 0x80); /* 1747 */
}

/* ==========================================================================
 *  The cross-fade in front of the puzzle phase
 *
 *  Same board, drawn from hina_first_pos[][] because hina_pzl_ctrl does not
 *  exist yet, under a black quad whose alpha is what puzzle.c is ramping.
 * ======================================================================== */

void HinaPuzzleCrossScreenDisp(int off_x, int off_y, u_char alpha)      /* 1757 */
{                                                                       /* 1758 */
    SQAR_DAT  cross_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 128 };           /* 1760 */
    DISP_SQAR dsq;

    HinaPuzzleBgDisp(off_x, off_y, 0x80);                               /* 1764 */
    HinaPuzzleSmokeScreenDisp(
        off_x, off_y,
        HinaPuzzleSmokeScreenAnim(&hina_pzl_cross_disp.anim_timer));    /* 1767 */
    HinaPuzzleCrossDollsDisp(0, 0, 0x80);                               /* 1770 */

    CopySqrDToSqr(&dsq, &cross_bg);                                     /* 1773 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1774 */
    DispSqrD(&dsq);                                                     /* 1775 */
}

static void HinaPuzzleCrossDollsDisp(int off_x, int off_y, u_char alpha) /* 1785 */
{                                                                       /* 1786 */
    static const float scale_tbl[4] =                           /* rdata 3b90a0 */
    {
        0.93f, 0.96f, 1.0f, 1.03f,
    };
    DISP_SPRT doll_ds;
    int       i;
    int       j;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1796 */

    for (i = 0; i < 4; i++) {                                           /* 1798 */
        for (j = 0; j < 4; j++) {                                       /* 1799 */
            if ((hina_first_pos[i][j] != -1) && (hina_first_pos[i][j] != -2)) { /* 1800 */
                CopySprDToSpr(&doll_ds, &puzzle_hina_tex[hina_first_pos[i][j]]); /* 1803 */
                doll_ds.x   = hina_pos_x[i][j] + (float)off_x;          /* 1804 */
                doll_ds.y   = hina_pos_y[i][j] + (float)off_y;          /* 1805 */
                doll_ds.csx = doll_ds.x + (float)doll_ds.w * 0.5f;      /* 1806 */
                doll_ds.csy = doll_ds.y + (float)doll_ds.h * 0.5f;      /* 1807 */
                doll_ds.scw = scale_tbl[i];
                doll_ds.sch = doll_ds.scw;
                doll_ds.alpha = (u_char)((doll_ds.alpha * alpha) >> 7); /* 1809 */
                DispSprD(&doll_ds);                                     /* 1810 */
            }
        }
    }
}
