// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/roku/roku_pzl.c
//
// The rokumen (six-face) puzzle: five books to be put back on a shelf in the
// right order.
//
// Three arrays hold the whole game.  have_book[i] says book i is still in
// hand; book_shelf[s] is the book in shelf slot s, or -1; and
// order_enter_book[] records the order they went in, which is what makes O
// take the last one back rather than whichever the cursor is on.  A book is
// stored as its number plus eight, so a shelf slot's value doubles as its
// puzzle_roku_tex[] index -- and that offset is why the take-back path does
// have_book[order_enter_book[i] - 8].
//
// sub_step is the screen: 0 pick a book from the hand, 1 pick a shelf slot,
// 2 read the book, 3 read the riddle, 4 quit prompt, 5 solved, 6 wrong.  It
// opens on 1, and every change goes through SixPuzzleReqNextSubStep() so the
// old screen fades out first.  Filling the last slot runs the answer check;
// getting it wrong lands on 6, where any button calls
// SixPuzzleReturnFirstState() and deals the books out again.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), roku_pzl.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Statements whose only memory access goes through
// fixed_array's inlined operator[] leave no $LM of their own -- their line is
// swallowed by fixed_array.h's 124/125 -- so a number of them are interpolated
// into a measured gap rather than read out of it.

#include "roku_pzl.h"

#include <stddef.h>                                 /* NULL                   */
#include <stdio.h>                                  /* printf                 */

#include "puzzle_roku_dat.h"                        /* six_puzzle_answer      */
#include "../puzzle.h"                              /* GetPzlTexDataAddr      */

#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* pad                    */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */

#include "../../menu/anim_2d.h"                     /* Anim2D_CalcNowAlpha    */
#include "../../photo/finder.h"                     /* FinderBankSetup        */

#include "../../../graphics/draw_env.h"             /* SetScissorRegister     */
#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnWindow          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT / DispSprD   */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg               */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../system/eeiop/snd_buffer.h"       /* SndBufIsPlaying        */
#include "../../../system/eeiop/sndbank.h"          /* SndBankPlay            */
#include "../../../system/pad/pad.h"                /* paddat/GetPadAnalogRpt */

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */

static void  SixPuzzleCtrlInit(void);
static void  SixPuzzleReqNextSubStep(char next_step);
static void  SixPuzzleSubOutAnimCheck(void);
static int   SixPuzzleClearCheck(void);
static void  SixPuzzleBookSelPad(void);
static void  SixPuzzleBookShelfPad(void);
static void  SixPuzzleBookReadPad(void);
static void  SixPuzzleMsgReadPad(void);
static void  SixPuzzleClearPad(void);
static void  SixPuzzleFailurePad(void);
static void  SixPuzzleExitPad(void);
static void  SixPuzzleEnterBook(void);
static void  SixPuzzleReturnFirstState(void);
static void  SixPuzzleExitReq(void);
static void  SixPuzzleDispInit(void);
static void  SixPuzzleAnim(char *anim_step, short *anim_timer, u_char *alpha);
static u_char SixPuzzleModeAnimCtrl(void);
static void  SixPuzzleBookSelDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookSelAnimCtrl(char *anim_step, short *anim_timer);
static void  SixPuzzleBookShelfSelDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookReadDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleMsgReadDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleClearDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleFailureDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleExitSelDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBgDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleShelfBookDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookSelWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookAnim(char *anim_step, short *anim_timer, float *pos);
static void  SixPuzzleBookTitleWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookSelArrowDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookSelArrowAnim(char *anim_step, short *anim_timer, u_char *alpha);
static void  SixPuzzleBookSelCapDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleShelfCsrDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleShelfSelCapDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleShelfMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookReadBgDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookReadWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBookReadCaptionDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleMsgReadFleaDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleMsgReadWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleClearWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleFailureWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleExitConfWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleCapWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleCmnWinDisp(int off_x, int off_y, u_char alpha);
static void  SixPuzzleBlackBgDisp(int off_x, int off_y, u_char alpha);

/* Note the ordering: slot 4 is the quit prompt and slots 5/6 the two endings,
 * which is not the order SixPuzzleDispMain()'s switch reads. */
static void (*six_pzl_pad_func[7])(void) =                  /* data 33e390 */
{
    SixPuzzleBookSelPad,
    SixPuzzleBookShelfPad,
    SixPuzzleBookReadPad,
    SixPuzzleMsgReadPad,
    SixPuzzleExitPad,
    SixPuzzleClearPad,
    SixPuzzleFailurePad,
};

static SIX_PZL_CTRL six_pzl_ctrl;                           /* bss 4bbdf8 */
static SIX_PZL_DISP six_pzl_disp;                           /* bss 4bbe40 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

void SixPuzzleExeInit(void)                                             /* 227 */
{
    SixPuzzleCtrlInit();                                                /* 228 */
    SixPuzzleDispInit();                                                /* 231 */
}

/* Opens on sub_step 1 -- the shelf cursor -- with all five books in hand. */
static void SixPuzzleCtrlInit(void)                                     /* 239 */
{                                                                       /* 240 */
    int i;

    six_pzl_ctrl.step           = 0;                                    /* 244 */
    six_pzl_ctrl.sub_step       = 1;                                    /* 245 */
    six_pzl_ctrl.next_sub_step  = 1;                                    /* 246 */
    six_pzl_ctrl.clear_flg      = 0;                                    /* 247 */
    six_pzl_ctrl.book_sel_csr   = 0;                                    /* 248 */
    six_pzl_ctrl.next_book_csr  = 0;                                    /* 249 */
    six_pzl_ctrl.book_shelf_csr = 0;                                    /* 250 */
    six_pzl_ctrl.exit_csr       = 1;                                    /* 251 */

    for (i = 0; i < 5; i++) {                                           /* 253 */
        six_pzl_ctrl.have_book[i]        = 1;
        six_pzl_ctrl.book_shelf[i]       = -1;
        six_pzl_ctrl.order_enter_book[i] = -1;                          /* 257 */
    }
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

int SixPuzzleMain(void)                                                 /* 270 */
{                                                                       /* 271 */
    int res;

    res = 1;                                                            /* 274 */

    if (six_pzl_ctrl.step == 0) {                                       /* 277 */
        SixPuzzleDispInit();                                            /* 278 */
        six_pzl_ctrl.step = 1;                                          /* 279 */
    }

    if (six_pzl_ctrl.step == 1) {                                       /* 282 */
        SixPuzzleSubOutAnimCheck();                                     /* 283 */

        if (six_pzl_disp.sub_anim_step == 2) {                          /* 286 */
            if (six_pzl_pad_func[six_pzl_ctrl.sub_step] != NULL) {      /* 288 */
                six_pzl_pad_func[six_pzl_ctrl.sub_step]();              /* 289 */
            }
        }
    }

    if ((six_pzl_ctrl.step == 2) &&                                     /* 294 */
        (six_pzl_disp.anim_step == 4) &&                                /* 295 */
        (SndBufIsPlaying(six_pzl_ctrl.snd_id) == 0)) {                  /* 297 */
        SndBankRelease(GetPzlSndBankID());                              /* 299 */
        FinderBankSetup();                                              /* 303 */
        six_pzl_ctrl.step = 3;                                          /* 304 */
    }

    if ((six_pzl_ctrl.step == 3) &&                                     /* 309 */
        (FinderBankIsReady() != 0)) {                                   /* 310 */
        SetMsgFirstPage();                                              /* 311 */
        SetNextGPhase(GID_STORY_NORMAL);                                /* 313 */
    }

    return res;                                                         /* 319 */
}

static void SixPuzzleReqNextSubStep(char next_step)
{
    six_pzl_ctrl.next_sub_step  = next_step;                            /* 329 */
    six_pzl_disp.sub_anim_step  = 3;                                    /* 330 */
    six_pzl_disp.sub_anim_timer = 0;                                    /* 331 */
}

/* The solved cue plays here rather than where the check first passes, so it
 * lands as the "cleared" screen fades in. */
static void SixPuzzleSubOutAnimCheck(void)                              /* 337 */
{                                                                       /* 338 */
    if (six_pzl_disp.sub_anim_step == 4) {                              /* 342 */
        six_pzl_disp.sub_anim_step = 0;                                 /* 343 */
        six_pzl_ctrl.sub_step = six_pzl_ctrl.next_sub_step;             /* 344 */

        SetMsgFirstPage();                                              /* 346 */

        if (SixPuzzleClearCheck() != 0) {                               /* 348 */
            six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 3, 0, 0,
                                              0x3200, 0x1000, 0,
                                              (SND_3D_SET *)0);         /* 350 */
        }
    }
}

/* Called from two places every time a book lands, so PuzzleClear() is raised
 * more than once -- harmless, the flag is idempotent. */
static int SixPuzzleClearCheck(void)                                    /* 361 */
{                                                                       /* 362 */
    int i;
    int res;

    res = 0;                                                            /* 366 */

    for (i = 0; i < 5; i++) {                                           /* 368 */
        if (six_puzzle_answer[i] != six_pzl_ctrl.book_shelf[i]) {       /* 373 */
            return res;
        }
    }

    PuzzleClear(PZL_ID_ROKU);                                           /* 376 */
    res = 1;                                                            /* 377 */

    return res;                                                         /* 382 */
}

/* --------------------------------------------------------------------------
 *  sub_step 0 -- the book carousel.
 *
 *  Left and right step to the next book still in hand, wrapping; the search
 *  runs up to five slots and gives up if the only book it finds is the one
 *  already selected (i == 5), which is what stops the carousel turning when
 *  the player is down to a single book.
 * ------------------------------------------------------------------------ */
static void SixPuzzleBookSelPad(void)                                   /* 387 */
{                                                                       /* 388 */
    int i;

    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0)) {    /* 393 */
        if (six_pzl_disp.move_anim_step == 2) {                         /* 394 */
            for (i = 1; i < 6; i++) {                                   /* 395 */
                six_pzl_ctrl.next_book_csr =
                    (char)((six_pzl_ctrl.book_sel_csr - i + 5) % 5);    /* 396 */

                if (six_pzl_ctrl.have_book[six_pzl_ctrl.next_book_csr] == 1) {
                    break;
                }
            }

            if (i != 5) {                                               /* 403 */
                six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 4, 0, 0,
                                                  0x3200, 0x1000, 0,
                                                  (SND_3D_SET *)0);     /* 405 */
                six_pzl_disp.move_rot       = 1;                        /* 408 */
                six_pzl_disp.move_anim_step = 0;                        /* 409 */
            }
        }
    } else if (((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) { /* 410 */
        if (six_pzl_disp.move_anim_step == 2) {                         /* 415 */
            for (i = 1; i < 6; i++) {                                   /* 416 */
                six_pzl_ctrl.next_book_csr =
                    (char)((six_pzl_ctrl.book_sel_csr + i) % 5);        /* 417 */

                if (six_pzl_ctrl.have_book[six_pzl_ctrl.next_book_csr] == 1) {
                    break;
                }
            }

            if (i != 5) {                                               /* 425 */
                six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 4, 0, 0,
                                                  0x3200, 0x1000, 0,
                                                  (SND_3D_SET *)0);     /* 427 */
                six_pzl_disp.move_rot       = 0;                        /* 430 */
                six_pzl_disp.move_anim_step = 0;                        /* 431 */
            }
        }
    } else if (*paddat[0] == 1) {                                       /* 432 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 1, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 438 */
        six_pzl_disp.move_anim_step = 2;                                /* 441 */
        SixPuzzleEnterBook();                                           /* 442 */
    } else if (*paddat[1] == 1) {                                       /* 444 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 5, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 447 */
        six_pzl_ctrl.book_sel_csr      = six_pzl_ctrl.next_book_csr;    /* 450 */
        six_pzl_disp.move_anim_step    = 2;                             /* 451 */
        six_pzl_disp.cap_win_anim_flg  = 0;                             /* 452 */
        six_pzl_disp.shelf_anim_timer  = 0;                             /* 453 */
        SixPuzzleReqNextSubStep(1);                                     /* 455 */
    } else if ((pad[0].one & 0x20) != 0) {                              /* 459 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 462 */
        six_pzl_ctrl.book_sel_csr     = six_pzl_ctrl.next_book_csr;     /* 463 */
        six_pzl_disp.cap_win_anim_flg = 1;                              /* 464 */
        six_pzl_disp.move_anim_step   = 2;                              /* 466 */
        SixPuzzleReqNextSubStep(2);                                     /* 467 */
    }
}

/* --------------------------------------------------------------------------
 *  sub_step 1 -- the shelf cursor.
 *
 *  X on an empty slot goes to the carousel with the first book still in hand
 *  pre-selected.  O takes the most recently placed book back off the shelf,
 *  and once there is nothing left to take back it opens the quit prompt.
 * ------------------------------------------------------------------------ */
static void SixPuzzleBookShelfPad(void)                                 /* 476 */
{                                                                       /* 477 */
    int i;
    int j;

    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0)) {    /* 483 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 4, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 486 */
        six_pzl_ctrl.book_shelf_csr =
            (char)((six_pzl_ctrl.book_shelf_csr + 4) % 5);              /* 487 */
        six_pzl_disp.shelf_anim_timer = 0;                              /* 488 */
    } else if (((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) { /* 491 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 4, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 494 */
        six_pzl_ctrl.book_shelf_csr =
            (char)((six_pzl_ctrl.book_shelf_csr + 1) % 5);              /* 495 */
        six_pzl_disp.shelf_anim_timer = 0;                              /* 496 */
    } else if (*paddat[0] == 1) {                                       /* 499 */
        /* An occupied slot simply refuses. */
        if (six_pzl_ctrl.book_shelf[six_pzl_ctrl.book_shelf_csr] != -1) { /* 504 */
            return;
        }

        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 1, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 506 */

        for (i = 0; i < 5; i++) {                                       /* 508 */
            if (six_pzl_ctrl.have_book[i] == 1) {                       /* 509 */
                six_pzl_ctrl.book_sel_csr  = (char)i;
                six_pzl_ctrl.next_book_csr = six_pzl_ctrl.book_sel_csr;
                break;
            }
        }

        six_pzl_disp.cap_win_anim_flg = 0;                              /* 515 */
        SixPuzzleReqNextSubStep(0);                                     /* 518 */
    } else if (*paddat[1] == 1) {                                       /* 522 */
        for (i = 4; i >= 0; i--) {                                      /* 523 */
            if (six_pzl_ctrl.order_enter_book[i] != -1) {               /* 528 */
                six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                                  0x3200, 0x1000, 0,
                                                  (SND_3D_SET *)0);     /* 530 */

                for (j = 0; j < 5; j++) {                               /* 533 */
                    if (six_pzl_ctrl.book_shelf[j] ==
                        six_pzl_ctrl.order_enter_book[i]) {             /* 535 */
                        six_pzl_ctrl.book_shelf[j] = -1;
                        break;
                    }
                }

                /* A shelf value is the book number plus eight. */
                six_pzl_ctrl.have_book[six_pzl_ctrl.order_enter_book[i] - 8] = 1; /* 538 */
                six_pzl_ctrl.order_enter_book[i] = -1;
                break;
            }
        }

        if (i < 0) {                                                    /* 543 */
            six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 5, 0, 0,
                                              0x3200, 0x1000, 0,
                                              (SND_3D_SET *)0);         /* 546 */
            six_pzl_disp.cap_win_anim_flg = 1;                          /* 548 */
            SixPuzzleReqNextSubStep(4);                                 /* 549 */
            six_pzl_ctrl.exit_csr = 1;                                  /* 550 */
        }
    } else if ((pad[0].one & 0x20) != 0) {                              /* 554 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 0, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 557 */
        six_pzl_disp.cap_win_anim_flg = 1;                              /* 558 */
        six_pzl_disp.msg_anim_timer   = 0;                              /* 560 */
        SixPuzzleReqNextSubStep(3);                                     /* 561 */
    }
}

/* X pages the book, O closes it.  X has no cue -- the message system's own
 * page turn is the feedback. */
static void SixPuzzleBookReadPad(void)                                  /* 570 */
{                                                                       /* 571 */
    if (*paddat[0] == 1) {                                              /* 575 */
        MesSetNextPage();                                               /* 577 */
    } else if (*paddat[1] == 1) {                                       /* 580 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 5, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 583 */
        SixPuzzleReqNextSubStep(0);                                     /* 584 */
    }
}

/* Either button closes the riddle; only O gets a cue. */
static void SixPuzzleMsgReadPad(void)                                   /* 593 */
{                                                                       /* 594 */
    if (*paddat[0] != 1) {                                              /* 598 */
        if (*paddat[1] != 1) {                                          /* 601 */
            return;
        }

        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 5, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 604 */
    }

    SixPuzzleReqNextSubStep(1);                                         /* 607 */
    six_pzl_disp.shelf_anim_timer = 0;                                  /* 608 */
}                                                                       /* 609 */

static void SixPuzzleClearPad(void)                                     /* 618 */
{                                                                       /* 619 */
    if (*paddat[0] == 1) {                                              /* 623 */
        SixPuzzleExitReq();                                             /* 625 */
    } else if (*paddat[1] == 1) {                                       /* 629 */
        SixPuzzleExitReq();                                             /* 631 */
    }
}

/* Wrong order: deal the books out again and go back to the shelf. */
static void SixPuzzleFailurePad(void)                                   /* 641 */
{                                                                       /* 642 */
    if ((*paddat[0] == 1) || (*paddat[1] == 1)) {                       /* 646 */
        SixPuzzleReturnFirstState();                                    /* 654 */
        SixPuzzleReqNextSubStep(1);                                     /* 657 */
        six_pzl_disp.shelf_anim_timer = 0;                              /* 658 */
    }
}                                                                       /* 659 */

/* The "back to the shelf" tail at 705/706 is shared by the yes-cancel and the
 * O paths; only "yes, quit" leaves early. */
static void SixPuzzleExitPad(void)                                      /* 668 */
{                                                                       /* 669 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 673 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {    /* 680 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 4, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 683 */
        six_pzl_ctrl.exit_csr ^= 1;                                     /* 684 */
        return;
    }

    if (*paddat[0] == 1) {                                              /* 687 */
        if (six_pzl_ctrl.exit_csr == 0) {                               /* 690 */
            SixPuzzleExitReq();                                         /* 691 */
            return;
        }
    } else if (*paddat[1] == 1) {                                       /* 701 */
        six_pzl_ctrl.snd_id = SndBankPlay(GetPzlSndBankID(), 5, 0, 0,
                                          0x3200, 0x1000, 0,
                                          (SND_3D_SET *)0);             /* 704 */
    } else {
        return;
    }

    SixPuzzleReqNextSubStep(1);                                         /* 705 */
    six_pzl_disp.shelf_anim_timer = 0;                                  /* 706 */
}

/* --------------------------------------------------------------------------
 *  Put the selected book into the cursor's shelf slot.  When the last slot
 *  fills, the answer is checked here as well as in SubOutAnimCheck(), so a
 *  correct final placement raises the clear flag twice.
 * ------------------------------------------------------------------------ */
static void SixPuzzleEnterBook(void)                                    /* 714 */
{                                                                       /* 715 */
    int i;

    six_pzl_ctrl.book_shelf[six_pzl_ctrl.book_shelf_csr] =
        six_pzl_ctrl.book_sel_csr + 8;                                  /* 724 */
    six_pzl_ctrl.have_book[six_pzl_ctrl.book_sel_csr] = 0;              /* 727 */

    for (i = 0; i < 5; i++) {                                           /* 729 */
        if (six_pzl_ctrl.order_enter_book[i] == -1) {                   /* 731 */
            six_pzl_ctrl.order_enter_book[i] = six_pzl_ctrl.book_sel_csr + 8; /* 735 */
            break;
        }
    }

    for (i = 0; i < 5; i++) {                                           /* 740 */
        if (six_pzl_ctrl.book_shelf[i] == -1) {                         /* 743 */
            /* Still a gap -- back to the shelf cursor. */
            six_pzl_disp.cap_win_anim_flg = 0;                          /* 744 */
            SixPuzzleReqNextSubStep(1);
            return;
        }
    }

    if (SixPuzzleClearCheck() != 0) {                                   /* 749 */
        six_pzl_disp.cap_win_anim_flg = 1;                              /* 750 */
        SixPuzzleReqNextSubStep(5);
    } else {
        six_pzl_disp.cap_win_anim_flg = 1;                              /* 755 */
        SixPuzzleReqNextSubStep(6);                                     /* 756 */
    }
}

static void SixPuzzleReturnFirstState(void)                             /* 764 */
{                                                                       /* 765 */
    int i;

    for (i = 0; i < 5; i++) {                                           /* 769 */
        six_pzl_ctrl.have_book[i]        = 1;
        six_pzl_ctrl.book_shelf[i]       = -1;
        six_pzl_ctrl.order_enter_book[i] = -1;                          /* 776 */
    }
}

static void SixPuzzleExitReq(void)
{
    six_pzl_ctrl.step       = 2;                                        /* 787 */
    six_pzl_disp.anim_step  = 3;                                        /* 788 */
    six_pzl_disp.anim_timer = 0;                                        /* 789 */
}

/* ==========================================================================
 *  Display
 * ======================================================================== */

static void SixPuzzleDispInit(void)
{
    six_pzl_disp.anim_step        = 0;                                  /* 804 */
    six_pzl_disp.anim_timer       = 0;                                  /* 805 */
    six_pzl_disp.sub_anim_step    = 0;                                  /* 806 */
    six_pzl_disp.sub_anim_timer   = 0;                                  /* 807 */
    six_pzl_disp.move_anim_step   = 2;                                  /* 808 */
    six_pzl_disp.move_anim_timer  = 0;                                  /* 809 */
    six_pzl_disp.move_rot         = 0;                                  /* 810 */
    six_pzl_disp.msg_anim_step    = 0;                                  /* 811 */
    six_pzl_disp.msg_anim_timer   = 0;                                  /* 812 */
    six_pzl_disp.shelf_anim_timer = 0;                                  /* 813 */
    six_pzl_disp.cap_win_anim_flg = 1;
}

void SixPuzzleDispMain(void)                                            /* 823 */
{                                                                       /* 824 */
    u_char alpha;
    u_char fade_alpha;

    alpha      = 0x80;                                                  /* 828 */
    fade_alpha = 0x80;                                                  /* 829 */

    if ((u_char)(six_pzl_ctrl.step - 1) < 2) {                          /* 832 */
        SixPuzzleAnim(&six_pzl_disp.anim_step, &six_pzl_disp.anim_timer,
                      &fade_alpha);                                     /* 833 */

        if (six_pzl_disp.anim_step != 4) {                              /* 835 */
            SixPuzzleBgDisp(0, 0, 0x80);                                /* 837 */
            SixPuzzleShelfBookDisp(0, 0, 0x80);                         /* 840 */

            if (six_pzl_ctrl.step == 1) {                               /* 842 */
                alpha = SixPuzzleModeAnimCtrl();                        /* 844 */
            }

            switch (six_pzl_ctrl.sub_step) {                            /* 847 */
            case 0:
                SixPuzzleBookSelDisp(0, 0, alpha);                      /* 849 */
                break;                                                  /* 850 */
            case 1:
                SixPuzzleBookShelfSelDisp(0, 0, alpha);                 /* 852 */
                break;                                                  /* 853 */
            case 2:
                SixPuzzleBookReadDisp(0, 0, alpha);                     /* 855 */
                break;                                                  /* 856 */
            case 3:
                SixPuzzleMsgReadDisp(0, 0, alpha);                      /* 858 */
                break;                                                  /* 859 */
            case 4:
                SixPuzzleExitSelDisp(0, 0, alpha);                      /* 861 */
                break;                                                  /* 862 */
            case 5:
                SixPuzzleClearDisp(0, 0, alpha);                        /* 864 */
                break;                                                  /* 865 */
            case 6:
                SixPuzzleFailureDisp(0, 0, alpha);                      /* 867 */
                break;
            }
        }

        SixPuzzleBlackBgDisp(0, 0, fade_alpha);                         /* 873 */
    }
}

static void SixPuzzleAnim(char *anim_step, short *anim_timer, u_char *alpha) /* 894 */
{
    static const ALPHA_ANIM_TBL six_out_alpha[2] =              /* rdata 3c47a0 */
    {
        {  0, 128,  0, 30 },
        { -1,  -1, -1, -1 },
    };

    if (*anim_step == 0) {                                              /* 895 */
        *anim_timer = 0;                                                /* 896 */
        *anim_step  = 1;                                                /* 897 */
    }

    if (*anim_timer < 0) {                                              /* 901 */
        PRINT_WARNING("Warning!! SixPuzzleAnim()");                     /* 902 */
    }

    if (*anim_step == 1) {                                              /* 907 */
        *alpha     = 0;                                                 /* 909 */
        *anim_step = 2;                                                 /* 911 */
    } else if (*anim_step == 2) {
        *alpha = 0;                                                     /* 914 */
    } else if (*anim_step == 3) {                                       /* 917 */
        *alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)six_out_alpha,
                                     (int)*anim_timer);                 /* 919 */
        (*anim_timer)++;                                                /* 921 */

        if (*anim_timer >= 30) {                                        /* 922 */
            *anim_step = 4;                                             /* 924 */
        }
    } else if (*anim_step == 4) {
        *alpha = 0x80;                                                  /* 926 */
    }
}                                                                       /* 929 */

static u_char SixPuzzleModeAnimCtrl(void)                               /* 939 */
{                                                                       /* 940 */
    static const ALPHA_ANIM_TBL in_alpha_tbl[2] =               /* rdata 3c4828 */
    {
        { 0, 128, 0, 7 },
        { -1, -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL out_alpha_tbl[2] =              /* rdata 3c4838 */
    {
        { 128, 0, 0, 5 },
        { -1, -1, -1, -1 },
    };
    u_char alpha;

    alpha = 0x80;                                                       /* 953 */

    if (six_pzl_disp.sub_anim_step == 0) {                              /* 955 */
        six_pzl_disp.sub_anim_timer = 0;                                /* 956 */
        six_pzl_disp.sub_anim_step  = 1;                                /* 957 */
    }

    if (six_pzl_disp.sub_anim_step == 1) {                              /* 961 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)in_alpha_tbl,
                                    (int)six_pzl_disp.sub_anim_timer);  /* 963 */
        six_pzl_disp.sub_anim_timer++;                                  /* 965 */

        if (six_pzl_disp.sub_anim_timer >= 7) {                         /* 967 */
            six_pzl_disp.sub_anim_step  = 2;                            /* 969 */
            six_pzl_disp.sub_anim_timer = 0;                            /* 970 */
        }                                                               /* 972 */
    } else if (six_pzl_disp.sub_anim_step == 2) {
        /* Settled. */
    } else if (six_pzl_disp.sub_anim_step == 3) {                       /* 977 */
        alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)out_alpha_tbl,
                                    (int)six_pzl_disp.sub_anim_timer);  /* 979 */
        six_pzl_disp.sub_anim_timer++;                                  /* 981 */

        if (six_pzl_disp.sub_anim_timer >= 5) {                         /* 982 */
            six_pzl_disp.sub_anim_timer = 0;                            /* 983 */
            six_pzl_disp.sub_anim_step  = 4;                            /* 984 */
        }                                                               /* 986 */
    } else if (six_pzl_disp.sub_anim_step == 4) {
        alpha = 0;
    } else {
        PRINT_ASSERT("Error! SixPuzzleModeAnimCtrl");                   /* 991 */
    }

    return alpha;                                                       /* 995 */
}

/* --------------------------------------------------------------------------
 *  The seven per-sub_step screens
 * ------------------------------------------------------------------------ */

/* cap_win_anim_flg pins the caption plate at full brightness when the screen
 * was entered without a fade -- so it does not blink on a same-screen change. */
static void SixPuzzleBookSelDisp(int off_x, int off_y, u_char alpha)    /* 1003 */
{                                                                       /* 1004 */
    SixPuzzleBookSelAnimCtrl(&six_pzl_disp.move_anim_step,
                             &six_pzl_disp.move_anim_timer);            /* 1007 */

    SixPuzzleBookReadBgDisp(off_x, off_y, alpha);                       /* 1010 */
    SixPuzzleBookSelWinDisp(off_x, off_y, alpha);                       /* 1013 */
    SixPuzzleBookDisp(off_x, off_y, alpha);                             /* 1016 */
    SixPuzzleBookTitleWinDisp(off_x, off_y, alpha);                     /* 1019 */
    SixPuzzleBookSelArrowDisp(off_x, off_y, alpha);                     /* 1022 */

    if (six_pzl_disp.cap_win_anim_flg == 0) {                           /* 1024 */
        SixPuzzleCapWinDisp(off_x, off_y, 0x80);                        /* 1026 */
    } else {
        SixPuzzleCapWinDisp(off_x, off_y, alpha);                       /* 1030 */
    }

    SixPuzzleBookSelCapDisp(off_x, off_y, alpha);                       /* 1034 */
    SixPuzzleBookMsgWinDisp(off_x, off_y, alpha);                       /* 1037 */
}

/* The eight-frame carousel slide; the selection is only committed when it
 * lands, which is why next_book_csr exists at all. */
static void SixPuzzleBookSelAnimCtrl(char *anim_step, short *anim_timer) /* 1049 */
{                                                                       /* 1050 */
    if (*anim_step == 0) {                                              /* 1051 */
        *anim_timer = 0;                                                /* 1052 */
        *anim_step  = 1;                                                /* 1055 */
    }

    if (*anim_step == 1) {                                              /* 1057 */
        (*anim_timer)++;                                                /* 1058 */

        if (*anim_timer > 7) {                                          /* 1059 */
            *anim_step = 2;                                             /* 1060 */
            six_pzl_ctrl.book_sel_csr = six_pzl_ctrl.next_book_csr;
        }
    }
}                                                                       /* 1067 */

/* The shelf books are drawn twice a frame -- once by DispMain at full alpha,
 * once here at the screen's -- which is what makes them brighten as the
 * shelf screen fades in. */
static void SixPuzzleBookShelfSelDisp(int off_x, int off_y, u_char alpha) /* 1079 */
{                                                                       /* 1080 */
    SixPuzzleShelfCsrDisp(off_x, off_y, alpha);                         /* 1084 */

    if (six_pzl_ctrl.step == 1) {                                       /* 1086 */
        SixPuzzleShelfBookDisp(off_x, off_y, 0x80);                     /* 1088 */
    } else {
        SixPuzzleShelfBookDisp(off_x, off_y, alpha);                    /* 1092 */
    }

    if (six_pzl_disp.cap_win_anim_flg == 0) {                           /* 1095 */
        SixPuzzleCapWinDisp(off_x, off_y, 0x80);                        /* 1097 */
    } else {
        SixPuzzleCapWinDisp(off_x, off_y, alpha);                       /* 1101 */
    }

    SixPuzzleShelfSelCapDisp(off_x, off_y, alpha);                      /* 1105 */
    SixPuzzleShelfMsgWinDisp(off_x, off_y, alpha);                      /* 1108 */
}

static void SixPuzzleBookReadDisp(int off_x, int off_y, u_char alpha)   /* 1119 */
{                                                                       /* 1120 */
    SixPuzzleBookReadBgDisp(off_x, off_y, alpha);                       /* 1124 */
    SixPuzzleBookReadWinDisp(off_x, off_y, alpha);                      /* 1127 */
    SixPuzzleBookReadCaptionDisp(off_x, off_y, alpha);                  /* 1130 */
}

static void SixPuzzleMsgReadDisp(int off_x, int off_y, u_char alpha)    /* 1140 */
{                                                                       /* 1141 */
    SixPuzzleMsgReadFleaDisp(off_x, off_y, alpha);                      /* 1145 */
    SixPuzzleMsgReadWinDisp(off_x, off_y, alpha);                       /* 1148 */
}

static void SixPuzzleClearDisp(int off_x, int off_y, u_char alpha)      /* 1157 */
{                                                                       /* 1158 */
    SixPuzzleClearWinDisp(off_x, off_y, alpha);                         /* 1162 */
}

static void SixPuzzleFailureDisp(int off_x, int off_y, u_char alpha)    /* 1173 */
{                                                                       /* 1174 */
    SixPuzzleFailureWinDisp(off_x, off_y, alpha);                       /* 1178 */
}

static void SixPuzzleExitSelDisp(int off_x, int off_y, u_char alpha)    /* 1189 */
{                                                                       /* 1190 */
    SixPuzzleExitConfWinDisp(off_x, off_y, alpha);                      /* 1193 */
}

/* -------------------------------------------------------------------------- */

static void SixPuzzleBgDisp(int off_x, int off_y, u_char alpha)         /* 1204 */
{                                                                       /* 1205 */
    DISP_SPRT bg_ds;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1211 */

    for (i = 0; i < 2; i++) {
        CopySprDToSpr(&bg_ds, &puzzle_roku_tex[i]);                     /* 1214 */
        bg_ds.x     = bg_ds.x + (float)off_x;                           /* 1215 */
        bg_ds.y     = bg_ds.y + (float)off_y;                           /* 1216 */
        bg_ds.alpha = (u_char)((bg_ds.alpha * alpha) >> 7);             /* 1217 */
        DispSprD(&bg_ds);                                               /* 1218 */
    }                                                                   /* 1219 */
}

/* A shelf slot's value is the book's sprite index, so it indexes the table
 * directly.  y is fixed; only x comes from shelf_book_x[]. */
static void SixPuzzleShelfBookDisp(int off_x, int off_y, u_char alpha)  /* 1230 */
{                                                                       /* 1231 */
    DISP_SPRT book_ds;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1237 */

    for (i = 0; i < 5; i++) {                                           /* 1240 */
        if (six_pzl_ctrl.book_shelf[i] != -1) {
            CopySprDToSpr(&book_ds, &puzzle_roku_tex[six_pzl_ctrl.book_shelf[i]]); /* 1243 */
            book_ds.x     = shelf_book_x[i] + (float)off_x;             /* 1244 */
            book_ds.y     = 174.0f;
            book_ds.alpha = (u_char)((book_ds.alpha * alpha) >> 7);     /* 1245 */
            DispSprD(&book_ds);                                         /* 1247 */
        }
    }
}

static void SixPuzzleBookSelWinDisp(int off_x, int off_y, u_char alpha) /* 1257 */
{                                                                       /* 1258 */
    SQAR_DAT  win_bg = { 209, 185, 216, 71, 0xa0, 0, 0, 0, 128 };       /* 1261 */
    DISP_SPRT win_ds;
    DISP_SQAR dsq;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1267 */

    CopySqrDToSqr(&dsq, &win_bg);                                       /* 1270 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1271 */
    DispSqrD(&dsq);                                                     /* 1272 */

    CopySprDToSpr(&win_ds, &puzzle_roku_tex[0x0d]);                     /* 1275 */
    win_ds.x     = win_ds.x + (float)off_x;                             /* 1276 */
    win_ds.y     = win_ds.y + (float)off_y;
    win_ds.alpha = (u_char)((win_ds.alpha * alpha) >> 7);               /* 1277 */
    DispSprD(&win_ds);                                                  /* 1278 */
}

/* --------------------------------------------------------------------------
 *  The carousel itself.  A scissor rectangle is installed so the outgoing and
 *  incoming books are clipped to the window, and both are drawn 209 pixels
 *  apart -- the window's own width -- sliding together.
 * ------------------------------------------------------------------------ */
static void SixPuzzleBookDisp(int off_x, int off_y, u_char alpha)       /* 1288 */
{                                                                       /* 1289 */
    static const ALPHA_ANIM_TBL move_alpha_tbl1[5] =            /* rdata 3c48b0 */
    {
        {   0,   6,  0,  2 },
        {   6,  25,  2,  4 },
        {  25,  64,  4,  6 },
        {  64, 128,  6,  8 },
        {  -1,  -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL move_alpha_tbl2[5] =            /* rdata 3c48d8 */
    {
        { 128,  64,  0,  2 },
        {  64,  25,  2,  4 },
        {  25,   6,  4,  6 },
        {   6,   0,  6,  8 },
        {  -1,  -1, -1, -1 },
    };
    DISP_SPRT win_ds;
    long      scissor_backup;
    float     anim_off_x;
    u_char    book_alpha;
    u_char    next_alpha;

    anim_off_x = 0.0f;

    scissor_backup = GET_SCISSOR_REGISTER(0);                           /* 1318 */
    SetScissorRegister(0, 0x00fb004b01a200dfULL);                       /* 1322 */

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1326 */

    if ((u_char)six_pzl_disp.move_anim_step < 2) {                      /* 1329 */
        SixPuzzleBookAnim(&six_pzl_disp.move_anim_step,
                          &six_pzl_disp.move_anim_timer, &anim_off_x);  /* 1330 */

        next_alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)move_alpha_tbl1,
                                         (int)six_pzl_disp.move_anim_timer); /* 1332 */
        book_alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)move_alpha_tbl2,
                                         (int)six_pzl_disp.move_anim_timer); /* 1333 */

        if (six_pzl_disp.move_rot == 0) {                               /* 1335 */
            CopySprDToSpr(&win_ds, &puzzle_roku_tex[six_pzl_ctrl.book_sel_csr + 0x0e]); /* 1336 */
            win_ds.x     = (win_ds.x + (float)off_x) - anim_off_x;      /* 1337 */
            win_ds.y     = win_ds.y + (float)off_y;                     /* 1338 */
            win_ds.alpha = book_alpha;                                  /* 1339 */
            DispSprD(&win_ds);                                          /* 1340 */

            CopySprDToSpr(&win_ds, &puzzle_roku_tex[six_pzl_ctrl.next_book_csr + 0x0e]); /* 1341 */
            win_ds.x     = win_ds.x + (float)off_x + (209.0f - anim_off_x); /* 1342 */
            win_ds.y     = win_ds.y + (float)off_y;                     /* 1343 */
            win_ds.alpha = next_alpha;
            DispSprD(&win_ds);
        } else {
            CopySprDToSpr(&win_ds, &puzzle_roku_tex[six_pzl_ctrl.book_sel_csr + 0x0e]); /* 1346 */
            win_ds.x     = win_ds.x + (float)off_x + anim_off_x;        /* 1347 */
            win_ds.y     = win_ds.y + (float)off_y;                     /* 1348 */
            win_ds.alpha = book_alpha;                                  /* 1349 */
            DispSprD(&win_ds);                                          /* 1350 */

            CopySprDToSpr(&win_ds, &puzzle_roku_tex[six_pzl_ctrl.next_book_csr + 0x0e]); /* 1351 */
            win_ds.x     = ((win_ds.x + (float)off_x) - 209.0f) + anim_off_x; /* 1352 */
            win_ds.y     = win_ds.y + (float)off_y;                     /* 1353 */
            win_ds.alpha = next_alpha;
            DispSprD(&win_ds);
        }
    }

    if (six_pzl_disp.move_anim_step == 2) {                             /* 1356 */
        CopySprDToSpr(&win_ds, &puzzle_roku_tex[six_pzl_ctrl.book_sel_csr + 0x0e]); /* 1357 */
        win_ds.x     = win_ds.x + (float)off_x;                         /* 1358 */
        win_ds.y     = win_ds.y + (float)off_y;
        win_ds.alpha = (u_char)((win_ds.alpha * alpha) >> 7);           /* 1359 */
        DispSprD(&win_ds);                                              /* 1360 */
    }

    SetScissorRegister(0, scissor_backup);                              /* 1365 */
}

static void SixPuzzleBookAnim(char *anim_step, short *anim_timer, float *pos) /* 1383 */
{
    static const POS_ANIM_TBL move_pos_tbl[2] =                 /* rdata 3c4900 */
    {
        {  0.0f, 209.0f,  0,  8, 0 },
        { -1.0f,  -1.0f, -1, -1, 0 },
    };

    if (*anim_timer < 0) {                                              /* 1384 */
        PRINT_WARNING("Warning!! SixPuzzleBookAnim");                   /* 1385 */
    }

    if (*anim_step == 1) {                                              /* 1389 */
        *pos = Anim2D_CalcNowPos((POS_ANIM_TBL *)move_pos_tbl,
                                 (int)*anim_timer);                     /* 1391 */
    } else if (*anim_step == 2) {                                       /* 1392 */
        *pos = 0.0f;                                                    /* 1394 */
    }
}                                                                       /* 1397 */

/* The title is the book's file text, id * 3 -- the third line of its entry. */
static void SixPuzzleBookTitleWinDisp(int off_x, int off_y, u_char alpha) /* 1408 */
{                                                                       /* 1409 */
    static const int msg_type_tbl[5] =                          /* rdata 3c4958 */
    {
        30, 32, 29, 31, 27,
    };
    SQAR_DAT  title_bg = { 250, 38, 201, 282, 0xa0, 0, 0, 0, 128 };     /* 1412 */
    DISP_SPRT win_ds;
    DISP_SQAR dsq;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1422 */

    CopySqrDToSqr(&dsq, &title_bg);                                     /* 1425 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1426 */
    DispSqrD(&dsq);                                                     /* 1427 */

    CopySprDToSpr(&win_ds, &puzzle_roku_tex[0x17]);                     /* 1430 */
    win_ds.x     = win_ds.x + (float)off_x;                             /* 1431 */
    win_ds.y     = win_ds.y + (float)off_y;
    win_ds.alpha = (u_char)((win_ds.alpha * alpha) >> 7);               /* 1432 */
    DispSprD(&win_ds);                                                  /* 1433 */

    PrintMsg_Arrange(msg_type_tbl[six_pzl_book_label[six_pzl_ctrl.book_sel_csr][0]],
                     six_pzl_book_label[six_pzl_ctrl.book_sel_csr][1] * 3,
                     0x148, 0x120, 1, (int)alpha, 0, 0, 0, 2);          /* 1439 */
}

/* Two plates always, plus a one-shot flash on whichever arrow was pressed. */
static void SixPuzzleBookSelArrowDisp(int off_x, int off_y, u_char alpha) /* 1458 */
{                                                                       /* 1459 */
    DISP_SPRT arrow_ds;
    int       i;
    u_char    flea_alpha;

    flea_alpha = 0;                                                     /* 1461 */

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1462 */

    if (six_pzl_disp.move_anim_step != 2) {                             /* 1465 */
        SixPuzzleBookSelArrowAnim(&six_pzl_disp.move_anim_step,
                                  &six_pzl_disp.move_anim_timer, &flea_alpha); /* 1466 */

        if (six_pzl_disp.move_rot == 1) {                               /* 1467 */
            CopySprDToSpr(&arrow_ds, &puzzle_roku_tex[0x13]);           /* 1468 */
            arrow_ds.x     = arrow_ds.x + (float)off_x;                 /* 1470 */
            arrow_ds.y     = arrow_ds.y + (float)off_y;
            arrow_ds.alpha = flea_alpha;                                /* 1474 */
            DispSprD(&arrow_ds);                                        /* 1475 */
        } else {
            CopySprDToSpr(&arrow_ds, &puzzle_roku_tex[0x14]);           /* 1476 */
            arrow_ds.x     = arrow_ds.x + (float)off_x;                 /* 1478 */
            arrow_ds.y     = arrow_ds.y + (float)off_y;
            arrow_ds.alpha = flea_alpha;                                /* 1483 */
            DispSprD(&arrow_ds);                                        /* 1484 */
        }
    }

    for (i = 0; i < 2; i++) {                                           /* 1485 */
        CopySprDToSpr(&arrow_ds, &puzzle_roku_tex[0x15 + i]);           /* 1486 */
        arrow_ds.x     = arrow_ds.x + (float)off_x;                     /* 1487 */
        arrow_ds.y     = arrow_ds.y + (float)off_y;
        arrow_ds.alpha = (u_char)((arrow_ds.alpha * alpha) >> 7);       /* 1488 */
        DispSprD(&arrow_ds);
    }
}

static void SixPuzzleBookSelArrowAnim(char *anim_step, short *anim_timer,
                                      u_char *alpha)                    /* 1499 */
{                                                                       /* 1500 */
    static const ALPHA_ANIM_TBL move_alpha_tbl[3] =             /* rdata 3c4970 */
    {
        { 128, 128,  0,  4 },
        { 128,   0,  4,  8 },
        {  -1,  -1, -1, -1 },
    };

    if (*anim_step == 1) {                                              /* 1510 */
        *alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)move_alpha_tbl,
                                     (int)*anim_timer);                 /* 1512 */
    } else if (*anim_step == 2) {                                       /* 1513 */
        *alpha = 0;
    }
}                                                                       /* 1518 */

static void SixPuzzleBookSelCapDisp(int off_x, int off_y, u_char alpha) /* 1534 */
{                                                                       /* 1535 */
    DISP_SPRT cap_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1538 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x19]);                     /* 1539 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1540 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1541 */
    DispSprD(&cap_ds);                                                  /* 1543 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1c]);                     /* 1544 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1545 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1546 */
    DispSprD(&cap_ds);                                                  /* 1548 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1d]);                     /* 1549 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1550 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1551 */
    DispSprD(&cap_ds);                                                  /* 1553 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x20]);                     /* 1554 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1555 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1556 */
    DispSprD(&cap_ds);
}

static void SixPuzzleBookMsgWinDisp(int off_x, int off_y, u_char alpha) /* 1573 */
{                                                                       /* 1574 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x47);                                           /* 1577 */
    SixPuzzleCmnWinDisp(off_x, off_y, alpha);                           /* 1580 */
    PrintMsg(0x47, 3, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1582 */
}

/* The shelf cursor pulses on a 40-frame loop of its own, restarted every time
 * the cursor moves. */
static void SixPuzzleShelfCsrDisp(int off_x, int off_y, u_char alpha)   /* 1591 */
{                                                                       /* 1592 */
    static const ALPHA_ANIM_TBL csr_alpha_tbl[3] =              /* rdata 3c4988 */
    {
        {  64, 115,  0, 20 },
        { 115,  64, 20, 40 },
        {  -1,  -1, -1, -1 },
    };
    DISP_SPRT shelf_ds;
    u_char    csr_alpha;

    csr_alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)csr_alpha_tbl,
                                    (int)six_pzl_disp.shelf_anim_timer); /* 1603 */

    six_pzl_disp.shelf_anim_timer++;                                    /* 1604 */

    if (six_pzl_disp.shelf_anim_timer > 39) {                           /* 1606 */
        six_pzl_disp.shelf_anim_timer = 0;                              /* 1607 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1611 */

    CopySprDToSpr(&shelf_ds, &puzzle_roku_tex[six_pzl_ctrl.book_shelf_csr + 3]); /* 1615 */
    shelf_ds.x     = shelf_ds.x + (float)off_x;                         /* 1616 */
    shelf_ds.y     = shelf_ds.y + (float)off_y;                         /* 1617 */
    /* The caller's alpha is thrown away here -- the cursor uses its own. */
    shelf_ds.alpha = csr_alpha;
    DispSprD(&shelf_ds);                                                /* 1618 */
}

/* Sprite 0x1b is the "take one back" prompt and 0x1a its greyed-out twin, so
 * an empty shelf shows the second. */
static void SixPuzzleShelfSelCapDisp(int off_x, int off_y, u_char alpha) /* 1629 */
{                                                                       /* 1630 */
    DISP_SPRT cap_ds;
    int       i;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1636 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1f]);                     /* 1640 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1641 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1642 */
    DispSprD(&cap_ds);                                                  /* 1643 */

    for (i = 0; i < 5; i++) {                                           /* 1645 */
        if (six_pzl_ctrl.book_shelf[i] != -1) {
            break;
        }
    }

    if (i < 5) {                                                        /* 1650 */
        CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1b]);                 /* 1653 */
        cap_ds.x     = cap_ds.x + (float)off_x;                         /* 1655 */
        cap_ds.y     = cap_ds.y + (float)off_y;
        cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);           /* 1656 */
        DispSprD(&cap_ds);                                              /* 1657 */
    } else {
        CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1a]);                 /* 1662 */
        cap_ds.x     = cap_ds.x + (float)off_x;                         /* 1663 */
        cap_ds.y     = cap_ds.y + (float)off_y;
        cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);           /* 1664 */
        DispSprD(&cap_ds);                                              /* 1665 */
    }

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1c]);                     /* 1669 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1670 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1671 */
    DispSprD(&cap_ds);                                                  /* 1672 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x1e]);                     /* 1675 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1676 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1677 */
    DispSprD(&cap_ds);                                                  /* 1678 */
}

static void SixPuzzleShelfMsgWinDisp(int off_x, int off_y, u_char alpha) /* 1689 */
{                                                                       /* 1690 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x47);                                           /* 1693 */
    SixPuzzleCmnWinDisp(off_x, off_y, alpha);                           /* 1696 */
    PrintMsg(0x47, 2, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1698 */
}

/* A 51/128 black wash, not the usual full one -- the shelf stays visible
 * behind the open book. */
static void SixPuzzleBookReadBgDisp(int off_x, int off_y, u_char alpha) /* 1707 */
{                                                                       /* 1708 */
    SQAR_DAT  read_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 51 };             /* 1710 */
    DISP_SQAR dsq;

    CopySqrDToSqr(&dsq, &read_bg);                                      /* 1715 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1716 */
    DispSqrD(&dsq);                                                     /* 1717 */
}

static void SixPuzzleBookReadWinDisp(int off_x, int off_y, u_char alpha) /* 1727 */
{                                                                       /* 1728 */
    DrawCmnFileWindow(six_pzl_book_label[six_pzl_ctrl.book_sel_csr][0],
                      six_pzl_book_label[six_pzl_ctrl.book_sel_csr][1],
                      0, alpha, 'f');                                   /* 1733 */
}

static void SixPuzzleBookReadCaptionDisp(int off_x, int off_y, u_char alpha) /* 1744 */
{                                                                       /* 1745 */
    DrawCmnButton(0, 394.0f, 350.0f, alpha, 0);                         /* 1749 */
    DrawCmnButton(3, 491.0f, 350.0f, alpha, 0);                         /* 1751 */
    DrawCmnCaption(11, 420.0f, 351.0f, alpha, 0);                       /* 1753 */
    DrawCmnCaption(2, 517.0f, 352.0f, alpha, 0);                        /* 1755 */
}

static void SixPuzzleMsgReadFleaDisp(int off_x, int off_y, u_char alpha) /* 1766 */
{                                                                       /* 1767 */
    static const ALPHA_ANIM_TBL flea_alpha_tbl[3] =             /* rdata 3c49b8 */
    {
        {  64, 115,  0, 20 },
        { 115,  64, 20, 40 },
        {  -1,  -1, -1, -1 },
    };
    DISP_SPRT flea_ds;
    u_char    flea_alpha;

    flea_alpha = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)flea_alpha_tbl,
                                     (int)six_pzl_disp.msg_anim_timer); /* 1778 */

    six_pzl_disp.msg_anim_timer++;                                      /* 1779 */

    if (six_pzl_disp.msg_anim_timer > 39) {                             /* 1781 */
        six_pzl_disp.msg_anim_timer = 0;                                /* 1782 */
    }

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1786 */

    CopySprDToSpr(&flea_ds, &puzzle_roku_tex[2]);                       /* 1789 */
    flea_ds.x     = flea_ds.x + (float)off_x;                           /* 1790 */
    flea_ds.y     = flea_ds.y + (float)off_y;                           /* 1791 */
    flea_ds.alpha = flea_alpha;
    DispSprD(&flea_ds);                                                 /* 1792 */
}

static void SixPuzzleMsgReadWinDisp(int off_x, int off_y, u_char alpha) /* 1803 */
{                                                                       /* 1804 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x47);                                           /* 1808 */
    SixPuzzleCmnWinDisp(off_x, off_y, alpha);                           /* 1811 */
    PrintMsg(0x47, 7, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1813 */
}

static void SixPuzzleClearWinDisp(int off_x, int off_y, u_char alpha)   /* 1823 */
{                                                                       /* 1824 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x47);                                           /* 1828 */
    SixPuzzleCmnWinDisp(off_x, off_y, alpha);                           /* 1831 */
    PrintMsg(0x47, 6, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1833 */
}

static void SixPuzzleFailureWinDisp(int off_x, int off_y, u_char alpha) /* 1843 */
{                                                                       /* 1844 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x47);                                           /* 1848 */
    SixPuzzleCmnWinDisp(off_x, off_y, alpha);                           /* 1851 */
    PrintMsg(0x47, 4, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1853 */
}

static void SixPuzzleExitConfWinDisp(int off_x, int off_y, u_char alpha) /* 1863 */
{                                                                       /* 1864 */
    DISP_STR ds;

    SetMsgDefData(&ds, 0x47);                                           /* 1868 */
    SixPuzzleCmnWinDisp(off_x, off_y, alpha);                           /* 1871 */

    DrawCmnSelCsr(0, (float)(six_pzl_ctrl.exit_csr * 0xcf + off_x + 0x9b),
                  (float)(off_y + 0x184), alpha, 0.0f, 0);              /* 1875 */
    DrawCmnSelYes(0, (float)(off_x + 0x99), (float)(off_y + 0x186), alpha); /* 1878 */
    DrawCmnSelNo(0, (float)(off_x + 0x169), (float)(off_y + 0x186), alpha); /* 1879 */

    PrintMsg(0x47, 5, ds.pos_x, ds.pos_y, 1, (int)alpha, 0);            /* 1881 */
}

static void SixPuzzleCapWinDisp(int off_x, int off_y, u_char alpha)     /* 1896 */
{                                                                       /* 1897 */
    DISP_SPRT cap_ds;

    PK2SendVram((uintptr_t)GetPzlTexDataAddr(), -1, -1, 0);             /* 1900 */

    CopySprDToSpr(&cap_ds, &puzzle_roku_tex[0x18]);                     /* 1901 */
    cap_ds.x     = cap_ds.x + (float)off_x;                             /* 1902 */
    cap_ds.y     = cap_ds.y + (float)off_y;
    cap_ds.alpha = (u_char)((cap_ds.alpha * alpha) >> 7);               /* 1903 */
    DispSprD(&cap_ds);
}

/* 0x33, not the 0x80 the other puzzles use -- this window is see-through. */
static void SixPuzzleCmnWinDisp(int off_x, int off_y, u_char alpha)     /* 1913 */
{                                                                       /* 1914 */
    MSG_WIN_DAT win_dat;

    SetMsgWinDefData(&win_dat, 0x47);                                   /* 1917 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h, alpha, 0x33); /* 1920 */
}

/* ==========================================================================
 *  The cross-fade in front of the puzzle phase
 *
 *  Only the shelf -- no books, since none has been placed yet.
 * ======================================================================== */

void SixPuzzleCrossScreenDisp(int off_x, int off_y, u_char alpha)       /* 1930 */
{                                                                       /* 1931 */
    SixPuzzleBgDisp(0, 0, 0x80);                                        /* 1934 */
    SixPuzzleBlackBgDisp(0, 0, alpha);                                  /* 1937 */
}

static void SixPuzzleBlackBgDisp(int off_x, int off_y, u_char alpha)    /* 1947 */
{                                                                       /* 1948 */
    SQAR_DAT  black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 128 };           /* 1950 */
    DISP_SQAR dsq;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 1954 */
    dsq.alpha = (u_char)((dsq.alpha * alpha) >> 7);                     /* 1955 */
    DispSqrD(&dsq);                                                     /* 1956 */
}
