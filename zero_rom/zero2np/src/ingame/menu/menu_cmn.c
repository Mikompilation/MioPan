// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_cmn.c
//
// The machinery every in-game menu page shares.  Four services that have
// nothing to do with each other beyond all being needed by more than one page:
//
//   MenuRefMove*        the scrolling-list cursor arithmetic
//   MenuCmn*Pad         the yes/no and confirm windows' pad handling
//   MenuCrossFade*      two texture slots, each with its own loader and fade
//   MenuDBuff*          two heap slots and a flag saying which is current
//
// The cross-fade is the substantial one.  Each slot runs two independent step
// machines over the same MENU_CROSS_FADE record: `load_step` walks
// release -> claim -> request -> wait -> idle, pumped by MenuCmnCrossFade();
// `anim_step` walks the ZERO2_ANIM2D_STEP_* ladder, pumped by
// GetMenuCrossFadeAlpha(), which is also what hands the caller this frame's
// alpha.  Both park on 4, which is why menu_cross_fade[]'s .data image is
// { 4, 4, 0, -1, NULL } twice -- a slot that has never been asked for anything
// is already "finished" on both counts and neither pump touches it.
//
// The two are only loosely coupled: GetMenuCrossFadeAlpha() refuses to animate
// a slot whose load_step is not 4, and frees the texture the frame the fade
// reaches 4.  So a page can start a fade before the file is off the disc and
// the picture simply appears when it arrives.
//
// Six of the 29 exports have no call site anywhere in the loadable segments
// (a jal *and* j scan over the whole 0x100000..0x2ae9c4 .text, so tail calls
// are covered).  Five of them are the whole double-buffer API bar its release
// chain -- nothing calls MenuDBuffLoadReq(), MenuDBuffChange(),
// GetMenuDBuffFlg() or GetMenuDBuffAddr(), so both slots are permanently NULL
// and MenuRelease()'s MenuDBuffCtrlInit() walks a pair of empty pointers.  The
// sixth is MenuCsrAnimCtrl(), superseded by zero2_anim2d.o's own
// CsrAnimCtrl(); this one ramps 64 -> 128 -> 64 over 45 frames where that one
// holds at the top.  MenuLoadWait() is dead too.  All are kept as found.
//
// The object's .sdata runs 0x3f2c40..0x3f2c81, but the first 0x28 bytes of it
// are zero and no instruction in the object references them -- globals.txt and
// functions.txt both list nothing there.  Only the tail is accounted for: the
// fixed_array assert's `str` at 0x3f2c68, the "void*"/"char*" type names, and
// menu_yes_no_ctrl at 0x3f2c80.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_cmn.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.
//
// Two shapes recur and are worth naming once:
//
//  * The four MenuRefMove* helpers and the two *Wait helpers end in a compare
//    that GCC folded into a single `sltu` with no branch -- jump.c's
//    "if (c) return 1; return 0;" -> "return c" rewrite.  Both source
//    spellings produce it, so which literal sat on which line is not
//    recoverable; the value is.  They are written here as the if/return pair
//    the two line markers imply.
//
//  * A store scheduled into a branch delay slot carries no line note of its
//    own, so six assignments here are interpolated rather than measured:
//    238, 466, 548, 572, 756 and 774.  Each sits in a run whose neighbours
//    are measured; every other annotation comes straight off a stab.

#include "menu_cmn.h"

#include "anim_2d.h"                            /* Anim2D_CalcNow*        */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_InOutAnim  */

#include "../../common/mem_util.h"              /* mem_utilGetMem         */
#include "../../common/utility2.h"              /* PRINT_ASSERT           */
#include "../../graphics/graph2d/tim2.h"        /* TIM2_PICTUREHEADER     */
#include "../../sdk/libgraph.h"                 /* SCE_GS_SET_TEX0        */
#include "../../system/eeiop/cddat.h"           /* GetFileSize            */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE          */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET             */
#include "../../system/os/eecdvd.h"             /* LoadReq / IsLoadEndAll */
#include "../../system/pad/pad.h"               /* pad / paddat           */
#include "../../system/os/system.h"             /* SystemBankPlay         */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* MENU_CROSS_FADE::load_step.  4 is both "idle" and "finished"; nothing
 * distinguishes a slot that has never been used from one that is done. */
#define MENU_CF_LOAD_START      0   /* release whatever is there, go to CLAIM */
#define MENU_CF_LOAD_CLAIM      1   /* take a heap block the file's size      */
#define MENU_CF_LOAD_REQ        2   /* post the read                          */
#define MENU_CF_LOAD_WAIT       3   /* poll it                                */
#define MENU_CF_LOAD_IDLE       4   /* nothing outstanding                    */

/* MENU_CROSS_FADE::anim_step.  Deliberately the same ladder
 * zero2_anim2d.h documents, so IN/SHOW/OUT/END read the same everywhere. */
#define MENU_CF_ANIM_START      0   /* seed: reset the timer and go to IN     */
#define MENU_CF_ANIM_IN         1
#define MENU_CF_ANIM_SHOW       2
#define MENU_CF_ANIM_OUT        3
#define MENU_CF_ANIM_END        4

/* Frames each half of the cross-fade takes.  Both tables end here. */
#define MENU_CF_ANIM_TIME       20

/* The menu window fade MenuInOutAnimCtrl() hands to zero2_anim2d.c. */
#define MENU_IN_ANIM_TIME       10
#define MENU_OUT_ANIM_TIME      5

/* One turn of the cursor pulse. */
#define MENU_CSR_ANIM_TIME      45

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3

/* pad[0].rpt bits, in the remapped layout: 0x8000 is LEFT and 0x2000 RIGHT,
 * with GetPadAnalogRpt(2) / (3) covering the stick. */
#define PAD_RPT_LEFT            0x8000
#define PAD_RPT_RIGHT           0x2000
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3

/* Every menu texture is an 8-bit paletted 640-wide page; only TBP0 and CBP
 * vary, which is what MenuTim2SendVram() patches in. */
#define MENU_TIM2_TBW           10
#define MENU_TIM2_TW            10
#define MENU_TIM2_TH            9

static void GetMenuCrossFadeMem(int buff_label, int data_label);
static void MenuCrossFadeTexLoadReq(int buff_label, int data_label);
static int  MenuCrossFadeTexLoadWait(int buff_label);

/* --------------------------------------------------------------------------
 *  Data
 * ------------------------------------------------------------------------ */

/* One cross-fade slot.  `tex_label` is the CD file the slot holds (-1 when
 * empty) and doubles as the load's own handle, which is why cancelling needs
 * both it and data_addr. */
typedef struct                      /* 0xc on the EE; data_addr widens here */
{
    /* 0x0 */ char  load_step;      /* MENU_CF_LOAD_*                        */
    /* 0x1 */ char  anim_step;      /* MENU_CF_ANIM_*                        */
    /* 0x2 */ char  anim_timer;
    /* 0x4 */ int   tex_label;
    /* 0x8 */ void *data_addr;
} MENU_CROSS_FADE;

/* The two double-buffer slots, and which of them is current. */
typedef struct                      /* 0xc on the EE; both pointers widen   */
{
    /* 0x0 */ u_char dbuff_flg;     /* 0 = slot 1 is current, 1 = slot 2     */
    /* 0x4 */ void  *data_addr_1;
    /* 0x8 */ void  *data_addr_2;
} MENU_DBUFF_CTRL;

static MENU_CROSS_FADE menu_cross_fade[MENU_CROSS_FADE_NUM] = /* data 323c50 */
{
    { MENU_CF_LOAD_IDLE, MENU_CF_ANIM_END, 0, -1, nullptr },
    { MENU_CF_LOAD_IDLE, MENU_CF_ANIM_END, 0, -1, nullptr },
};

MENU_YES_NO_CTRL menu_yes_no_ctrl;                          /* sdata 3f2c80 */

static MENU_DBUFF_CTRL menu_dbuff_ctrl;                     /* bss   4b5488 */

/* ==========================================================================
 *  Yes/no window
 * ======================================================================== */

void MenuYesNoCtrlInit(int csr)                                          /* 93 */
{
    menu_yes_no_ctrl.csr = (char)csr;                                    /* 94 */
}

/* ==========================================================================
 *  Scrolling-list cursor
 * ======================================================================== */

void MenuRefCtrlInit(MENU_REF_CTRL *ref_ctrl, int data_num)              /* 116 */
{
    ref_ctrl->disp_start_pos = 0;                                       /* 119 */
    ref_ctrl->data_pos = 0;                                             /* 120 */
    ref_ctrl->data_num = data_num;                                      /* 121 */
}

/* One row up.  The window only moves once the cursor has run off the top of
 * it, and only wraps to the bottom of the list when the list is longer than
 * the window (disp_num == disp_num_max). */
int MenuRefMovePadLup(MENU_REF_CTRL *ref_ctrl, int *cursor,
                      int disp_num, int disp_num_max)                   /* 134 */
{
    int data_pos_back_up;

    data_pos_back_up = ref_ctrl->data_pos;                              /* 142 */

    (*cursor)--;                                                        /* 146 */

    if (*cursor < 0) {                                                  /* 149 */
        *cursor = 0;                                                    /* 150 */

        ref_ctrl->disp_start_pos--;                                     /* 153 */

        if (ref_ctrl->disp_start_pos < 0) {                             /* 155 */
            if (disp_num == disp_num_max) {                             /* 157 */
                ref_ctrl->disp_start_pos = ref_ctrl->data_num - disp_num; /* 158 */
            }
            else {
                ref_ctrl->disp_start_pos = 0;
            }

            *cursor = disp_num - 1;                                     /* 166 */
        }

        ref_ctrl->data_pos--;                                           /* 170 */
        if (ref_ctrl->data_pos < 0) {                                   /* 172 */
            ref_ctrl->data_pos = ref_ctrl->data_num - 1;                /* 174 */
        }
    }
    else {
        ref_ctrl->data_pos--;                                           /* 179 */
        if (ref_ctrl->data_pos < 0) {                                   /* 182 */
            ref_ctrl->data_pos = 0;
        }
    }

    if (data_pos_back_up != ref_ctrl->data_pos) {                       /* 188 */
        return 1;
    }

    return 0;                                                           /* 196 */
}

/* One row down.  The mirror of the above, except that the wrap test is
 * against `data_num - disp_num_max` rather than zero. */
int MenuRefMovePadLdown(MENU_REF_CTRL *ref_ctrl, int *cursor,
                        int disp_num, int disp_num_max)                 /* 208 */
{
    int data_pos_back_up;

    data_pos_back_up = ref_ctrl->data_pos;                              /* 216 */

    (*cursor)++;                                                        /* 220 */

    if (*cursor >= disp_num) {                                          /* 223 */
        *cursor = disp_num - 1;                                         /* 224 */

        ref_ctrl->disp_start_pos++;                                     /* 227 */
        if (ref_ctrl->disp_start_pos > ref_ctrl->data_num - disp_num_max) { /* 228 */
            ref_ctrl->disp_start_pos = 0;                               /* 229 */
            *cursor = 0;                                                /* 230 */
        }
    }

    ref_ctrl->data_pos++;                                               /* 235 */
    if (ref_ctrl->data_pos >= ref_ctrl->data_num) {                     /* 237 */
        ref_ctrl->data_pos = 0;                                         /* 238 */
    }

    if (data_pos_back_up != ref_ctrl->data_pos) {                       /* 243 */
        return 1;
    }

    return 0;                                                           /* 251 */
}

/* A whole window up (L1).  Running off the top parks on the first entry
 * rather than wrapping -- which is the difference from PadLup. */
int MenuRefMovePageUp(MENU_REF_CTRL *ref_ctrl, int *cursor,
                      int disp_num, int disp_num_max)                   /* 263 */
{
    int data_pos_back_up;

    data_pos_back_up = ref_ctrl->data_pos;                              /* 271 */

    ref_ctrl->data_pos -= disp_num_max;                                 /* 275 */

    if (ref_ctrl->data_pos <= 0) {                                      /* 278 */
        ref_ctrl->disp_start_pos = 0;                                   /* 279 */
        *cursor = 0;                                                    /* 280 */

        ref_ctrl->data_pos = 0;                                         /* 282 */
    }
    else {
        ref_ctrl->disp_start_pos -= disp_num_max;                       /* 285 */

        /* The window overshot the top; disp_start_pos is negative here, so
         * adding it is what pulls the cursor back by the overshoot. */
        if (ref_ctrl->disp_start_pos < 0) {                             /* 287 */
            *cursor += ref_ctrl->disp_start_pos;                        /* 288 */
            ref_ctrl->disp_start_pos = 0;                               /* 289 */
        }
    }

    if (data_pos_back_up != ref_ctrl->data_pos) {                       /* 295 */
        return 1;
    }

    return 0;                                                           /* 303 */
}

/* A whole window down (R1).  Running off the bottom parks on the last entry,
 * and `disp_num < disp_num_max` -- a list shorter than the window -- is what
 * keeps the window itself at zero. */
int MenuRefMovePageDown(MENU_REF_CTRL *ref_ctrl, int *cursor,
                        int disp_num, int disp_num_max)                 /* 315 */
{
    int data_pos_back_up;

    data_pos_back_up = ref_ctrl->data_pos;                              /* 323 */

    ref_ctrl->data_pos += disp_num_max;                                 /* 327 */

    if (ref_ctrl->data_pos >= ref_ctrl->data_num) {                     /* 330 */
        ref_ctrl->data_pos = ref_ctrl->data_num - 1;                    /* 331 */

        if (disp_num >= disp_num_max) {                                 /* 333 */
            ref_ctrl->disp_start_pos = ref_ctrl->data_num - disp_num_max; /* 334 */
        }
        else {
            ref_ctrl->disp_start_pos = 0;                               /* 337 */
        }

        *cursor = disp_num - 1;                                         /* 340 */
    }
    else {
        ref_ctrl->disp_start_pos += disp_num_max;                       /* 343 */

        if (ref_ctrl->disp_start_pos > ref_ctrl->data_num - disp_num_max) { /* 345 */
            ref_ctrl->disp_start_pos = ref_ctrl->data_num - disp_num_max; /* 346 */
            *cursor = ref_ctrl->data_pos - ref_ctrl->disp_start_pos;    /* 348 */
        }
    }

    if (data_pos_back_up != ref_ctrl->data_pos) {                       /* 353 */
        return 1;
    }

    return 0;                                                           /* 361 */
}

/* ==========================================================================
 *  Shared window pad handlers
 * ======================================================================== */

/* The confirm window has one action, so both buttons dismiss it and only the
 * cue differs.  GCC cross-jumped the two SystemBankPlay() calls, which is why
 * one shared tail carries the second arm's `res = 1` line. */
int MenuCmnConfirmPad(void)                                             /* 369 */
{
    int res = 0;

    if (*paddat[0] == 1) {                                              /* 375 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 376 */
        res = 1;                                                        /* 377 */
    }
    else if (*paddat[1] == 1) {                                         /* 380 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 381 */
        res = 1;                                                        /* 382 */
    }

    return res;                                                         /* 386 */
}

/* The yes/no window.  Left and right both just toggle, so the two directions
 * share one arm; CROSS reports which side the cursor was on and TRIANGLE
 * reports "no" whatever it was on. */
int MenuCmnYesNoPad(void)                                               /* 395 */
{
    int res;

    res = 0;                                                            /* 399 */

    if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT) || /* 403 */
        (pad[0].rpt & PAD_RPT_RIGHT) || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) { /* 408 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 409 */
        menu_yes_no_ctrl.csr ^= 1;                                      /* 410 */
    }
    else if (*paddat[0] == 1) {                                         /* 412 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 413 */

        if (menu_yes_no_ctrl.csr == 0) {                                /* 414 */
            res = 1;
        }
        else {
            res = 2;                                                    /* 418 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 421 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 422 */
        res = 2;
    }

    return res;                                                         /* 427 */
}

int MenuLoadWait(void)                                                  /* 435 */
{
    if (IsLoadEndAll() == 0) {                                          /* 442 */
        return 0;
    }

    return 1;                                                           /* 447 */
}

/* ==========================================================================
 *  Cross-fade -- the loader
 * ======================================================================== */

void MenuCrossFadeInit(void)                                            /* 457 */
{
    int i;

    for (i = 0; i < MENU_CROSS_FADE_NUM; i++) {                         /* 462 */
        menu_cross_fade[i].load_step = MENU_CF_LOAD_IDLE;               /* 463 */
        menu_cross_fade[i].anim_step = MENU_CF_ANIM_END;                /* 464 */
        menu_cross_fade[i].anim_timer = 0;                              /* 465 */
        menu_cross_fade[i].tex_label = -1;                              /* 466 */

        if (menu_cross_fade[i].data_addr != nullptr) {                  /* 469 */
            LiberateMenuCrossFadeTexMem(i);                             /* 470 */
            menu_cross_fade[i].data_addr = nullptr;                     /* 471 */
        }
    }                                                                   /* 473 */
}

/* Claim a block the size of the file about to be read into it.  The second
 * test is not redundant with the first: LiberateMenuCrossFadeTexMem() is what
 * clears data_addr, so a slot that was holding something arrives here empty. */
static void GetMenuCrossFadeMem(int buff_label, int data_label)          /* 483 */
{
    if (menu_cross_fade[buff_label].data_addr != nullptr) {              /* 486 */
        LiberateMenuCrossFadeTexMem(buff_label);                         /* 487 */
    }

    if (menu_cross_fade[buff_label].data_addr == nullptr) {              /* 491 */
        menu_cross_fade[buff_label].data_addr =
            mem_utilGetMem((int)GetFileSize(data_label));                /* 492 */
    }
}

static void MenuCrossFadeTexLoadReq(int buff_label, int data_label)      /* 502 */
{
    FileLoadReqEE(data_label, menu_cross_fade[buff_label].data_addr,
                  2, nullptr, nullptr);                                  /* 509 */

    menu_cross_fade[buff_label].tex_label = data_label;                  /* 510 */
}

static int MenuCrossFadeTexLoadWait(int buff_label)                      /* 521 */
{
    if (FileLoadIsEnd2(menu_cross_fade[buff_label].tex_label,
                       menu_cross_fade[buff_label].data_addr) == 0) {    /* 529 */
        return 0;
    }

    return 1;                                                            /* 534 */
}

/* One frame of both slots' loaders.  Each step falls through to the next on
 * the following frame, so a texture takes four frames plus the read itself. */
void MenuCmnCrossFade(void)                                              /* 540 */
{
    int i;

    for (i = 0; i < MENU_CROSS_FADE_NUM; i++) {                          /* 545 */
        if (menu_cross_fade[i].load_step == MENU_CF_LOAD_START) {        /* 546 */
            menu_cross_fade[i].anim_step = MENU_CF_ANIM_START;           /* 547 */
            menu_cross_fade[i].anim_timer = 0;                           /* 548 */

            if (menu_cross_fade[i].data_addr != nullptr) {               /* 550 */
                LiberateMenuCrossFadeTexMem(i);                          /* 551 */
            }

            menu_cross_fade[i].load_step = MENU_CF_LOAD_CLAIM;           /* 554 */
        }
        else if (menu_cross_fade[i].load_step == MENU_CF_LOAD_CLAIM) {   /* 556 */
            GetMenuCrossFadeMem(i, menu_cross_fade[i].tex_label);        /* 558 */

            menu_cross_fade[i].load_step = MENU_CF_LOAD_REQ;             /* 560 */
        }
        else if (menu_cross_fade[i].load_step == MENU_CF_LOAD_REQ) {     /* 562 */
            MenuCrossFadeTexLoadReq(i, menu_cross_fade[i].tex_label);    /* 564 */

            menu_cross_fade[i].load_step = MENU_CF_LOAD_WAIT;            /* 566 */
        }
        else if (menu_cross_fade[i].load_step == MENU_CF_LOAD_WAIT) {    /* 568 */
            if (MenuCrossFadeTexLoadWait(i) != 0) {                      /* 570 */
                menu_cross_fade[i].load_step = MENU_CF_LOAD_IDLE;        /* 572 */
            }
        }
    }                                                                    /* 574 */
}

/* ==========================================================================
 *  Cross-fade -- requests
 * ======================================================================== */

void MenuCrossFadeInStart(int buff_label, int data_label)                /* 584 */
{
    if ((u_int)buff_label >= MENU_CROSS_FADE_NUM) {                      /* 588 */
        PRINT_ASSERT("Error! MenuCrossFadeInStart buff_label %d", buff_label); /* 589 */
    }

    if (menu_cross_fade[buff_label].data_addr != nullptr) {              /* 594 */
        LiberateMenuCrossFadeTexMem(buff_label);                         /* 596 */
    }

    menu_cross_fade[buff_label].load_step = MENU_CF_LOAD_START;          /* 600 */
    menu_cross_fade[buff_label].anim_step = MENU_CF_ANIM_START;          /* 601 */
    menu_cross_fade[buff_label].tex_label = data_label;                  /* 602 */
}

/* Asking a slot that has already finished to fade out is a no-op; that is
 * what stops a second request restarting the ramp from full. */
void MenuCrossFadeOutStart(int buff_label)                               /* 611 */
{
    if ((u_int)buff_label >= MENU_CROSS_FADE_NUM) {                      /* 615 */
        PRINT_ASSERT("Error! MenuCrossFadeOutStart buff_label %d", buff_label); /* 616 */
    }

    if (menu_cross_fade[buff_label].anim_step != MENU_CF_ANIM_END) {     /* 622 */
        menu_cross_fade[buff_label].anim_step = MENU_CF_ANIM_OUT;        /* 623 */
        menu_cross_fade[buff_label].anim_timer = 0;                      /* 624 */
    }
}

void LiberateAllMenuCrossFadeTexMem(void)                                /* 633 */
{
    LiberateMenuCrossFadeTexMem(0);                                      /* 639 */
    LiberateMenuCrossFadeTexMem(1);                                      /* 640 */
}

void LiberateMenuCrossFadeTexMem(int buff_label)                         /* 649 */
{
    if ((u_int)buff_label >= MENU_CROSS_FADE_NUM) {                      /* 653 */
        PRINT_ASSERT("Error! LiberateMenuCrossFadeTexMem buff_label %d",
                     buff_label);                                        /* 654 */
    }

    /* Cancel first -- the loader must not be left writing into a block that
     * is about to go back to the heap. */
    MenuCrossFadeTexLoadCancel(buff_label);                              /* 659 */

    if (menu_cross_fade[buff_label].data_addr != nullptr) {              /* 661 */
        mem_utilFreeMem(menu_cross_fade[buff_label].data_addr);          /* 662 */
        menu_cross_fade[buff_label].data_addr = nullptr;                 /* 663 */

        menu_cross_fade[buff_label].tex_label = -1;                      /* 665 */
    }

    menu_cross_fade[buff_label].load_step = MENU_CF_LOAD_IDLE;           /* 668 */
    menu_cross_fade[buff_label].anim_step = MENU_CF_ANIM_END;            /* 669 */
}

/* Withdraw an outstanding read.  All three tests have to pass before the
 * cancel is worth posting: a block, a file, and a read still in flight. */
void MenuCrossFadeTexLoadCancel(int buff_label)                          /* 678 */
{
    if ((u_int)buff_label >= MENU_CROSS_FADE_NUM) {                      /* 682 */
        PRINT_ASSERT("Error! MenuCrossFadeTexLoadCancel buff_label %d",
                     buff_label);                                        /* 683 */
    }

    if (menu_cross_fade[buff_label].data_addr != nullptr) {              /* 687 */
        if (menu_cross_fade[buff_label].tex_label != -1) {               /* 688 */
            if (MenuCrossFadeTexLoadWait(buff_label) == 0) {             /* 690 */
                FileLoadCancel2(menu_cross_fade[buff_label].tex_label,
                                menu_cross_fade[buff_label].data_addr,
                                nullptr, nullptr);                       /* 691 */
            }
        }
    }

    menu_cross_fade[buff_label].load_step = MENU_CF_LOAD_IDLE;           /* 696 */
    menu_cross_fade[buff_label].anim_step = MENU_CF_ANIM_END;            /* 697 */
    menu_cross_fade[buff_label].tex_label = -1;                          /* 698 */
}

/* ==========================================================================
 *  Cross-fade -- the fade
 * ======================================================================== */

/* One frame of both slots' fades, and this frame's alpha for each.
 *
 * The slot's texture is given back the frame the fade-out completes (765-769),
 * so a page that has faded a picture away does not have to release it.  A slot
 * whose loader is still running contributes 0 and is left alone entirely. */
void GetMenuCrossFadeAlpha(u_char *fade_alpha)                           /* 707 */
{
    int i;

    static const ALPHA_ANIM_TBL cross_fade_in[2] =           /* rdata 3bd4f8 */
    {
        {  0, 128,  0, MENU_CF_ANIM_TIME },
        { -1,  -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL cross_fade_out[2] =          /* rdata 3bd508 */
    {
        { 128,  0,  0, MENU_CF_ANIM_TIME },
        {  -1, -1, -1, -1 },
    };

    fade_alpha[0] = fade_alpha[1] = 0;                                   /* 721 */

    for (i = 0; i < MENU_CROSS_FADE_NUM; i++) {                          /* 725 */
        if (menu_cross_fade[i].load_step == MENU_CF_LOAD_IDLE) {         /* 727 */
            if (menu_cross_fade[i].anim_step != MENU_CF_ANIM_END) {      /* 728 */
                if (menu_cross_fade[i].anim_step == MENU_CF_ANIM_START) { /* 730 */
                    menu_cross_fade[i].anim_timer = 0;                   /* 731 */
                    menu_cross_fade[i].anim_step = MENU_CF_ANIM_IN;      /* 732 */
                }

                switch (menu_cross_fade[i].anim_step) {                  /* 735 */
                case MENU_CF_ANIM_IN:
                    fade_alpha[i] = Anim2D_CalcNowAlpha(cross_fade_in,
                                        menu_cross_fade[i].anim_timer);  /* 738 */

                    menu_cross_fade[i].anim_timer++;                     /* 740 */
                    if (menu_cross_fade[i].anim_timer >= MENU_CF_ANIM_TIME) { /* 741 */
                        menu_cross_fade[i].anim_timer = 0;               /* 742 */
                        menu_cross_fade[i].anim_step = MENU_CF_ANIM_SHOW; /* 743 */
                    }

                    break;                                               /* 746 */

                case MENU_CF_ANIM_SHOW:
                    fade_alpha[i] = 0x80;                                /* 748 */
                    break;                                               /* 749 */

                case MENU_CF_ANIM_OUT:
                    fade_alpha[i] = Anim2D_CalcNowAlpha(cross_fade_out,
                                        menu_cross_fade[i].anim_timer);  /* 752 */

                    menu_cross_fade[i].anim_timer++;                     /* 754 */
                    if (menu_cross_fade[i].anim_timer >= MENU_CF_ANIM_TIME) { /* 755 */
                        menu_cross_fade[i].anim_step = MENU_CF_ANIM_END; /* 756 */
                    }

                    break;                                               /* 759 */

                default:
                    PRINT_ASSERT("Error! GetMenuCrossFadeAlpha");        /* 761 */
                    break;
                }
            }
            else {
                fade_alpha[i] = 0;                                       /* 765 */

                if (menu_cross_fade[i].data_addr != nullptr) {           /* 767 */
                    LiberateMenuCrossFadeTexMem(i);                      /* 769 */
                }
            }
        }
        else {
            fade_alpha[i] = 0;                                           /* 774 */
        }
    }                                                                    /* 776 */
}

void *GetCrossFadeDataAddr(int buff_label)                               /* 786 */
{
    if ((u_int)buff_label >= MENU_CROSS_FADE_NUM) {                      /* 790 */
        PRINT_ASSERT("Error! GetCrossFadeDataAddr buff_label %d", buff_label); /* 791 */
    }

    return menu_cross_fade[buff_label].data_addr;                        /* 797 */
}

int CheckCrossFadeDisp(int buff_label)                                   /* 806 */
{
    int res;

    if ((u_int)buff_label >= MENU_CROSS_FADE_NUM) {                      /* 812 */
        PRINT_ASSERT("Error! CheckCrossFadeDisp buff_label %d", buff_label); /* 813 */
    }

    res = 0;                                                             /* 817 */

    if (menu_cross_fade[buff_label].data_addr != nullptr) {              /* 819 */
        if (menu_cross_fade[buff_label].load_step == MENU_CF_LOAD_IDLE) { /* 821 */
            res = (menu_cross_fade[buff_label].anim_step != MENU_CF_ANIM_START &&
                   menu_cross_fade[buff_label].anim_step != MENU_CF_ANIM_END); /* 822 */
        }
    }

    return res;                                                          /* 830 */
}

/* ==========================================================================
 *  Double buffer
 * ======================================================================== */

void MenuDBuffCtrlInit(void)                                             /* 840 */
{
    MenuDBuffAllRelease();                                               /* 843 */

    menu_dbuff_ctrl.dbuff_flg = 0;                                       /* 845 */
    menu_dbuff_ctrl.data_addr_1 = nullptr;                               /* 846 */
    menu_dbuff_ctrl.data_addr_2 = nullptr;                               /* 847 */
}

void MenuDBuffChange(void)                                               /* 854 */
{
    menu_dbuff_ctrl.dbuff_flg ^= 1;                                      /* 858 */
}

/* Load into whichever slot is current.  The two arms are the same code twice
 * rather than a slot pointer, so the assert names the same line twice over. */
void MenuDBuffLoadReq(int load_file)                                     /* 866 */
{
    if (menu_dbuff_ctrl.dbuff_flg != 0) {                                /* 869 */
        if (menu_dbuff_ctrl.data_addr_2 == nullptr) {                    /* 870 */
            menu_dbuff_ctrl.data_addr_2 =
                mem_utilGetMem((int)GetFileSize(load_file));             /* 872 */
        }
        else {
            PRINT_ASSERT("Error!! MenuDBuffLoadReq()");                  /* 875 */
        }

        LoadReq(load_file, (uintptr_t)menu_dbuff_ctrl.data_addr_2);      /* 878 */
    }
    else {
        if (menu_dbuff_ctrl.data_addr_1 == nullptr) {                    /* 881 */
            menu_dbuff_ctrl.data_addr_1 =
                mem_utilGetMem((int)GetFileSize(load_file));             /* 883 */
        }
        else {
            PRINT_ASSERT("Error!! MenuDBuffLoadReq()");                  /* 886 */
        }

        LoadReq(load_file, (uintptr_t)menu_dbuff_ctrl.data_addr_1);      /* 889 */
    }
}

void MenuDBuffRelease(u_char flg)                                        /* 900 */
{
    if (flg != 0) {                                                      /* 904 */
        if (menu_dbuff_ctrl.data_addr_2 != nullptr) {                    /* 905 */
            mem_utilFreeMem(menu_dbuff_ctrl.data_addr_2);                /* 906 */
            menu_dbuff_ctrl.data_addr_2 = nullptr;                       /* 907 */
        }
    }
    else {
        if (menu_dbuff_ctrl.data_addr_1 != nullptr) {                    /* 911 */
            mem_utilFreeMem(menu_dbuff_ctrl.data_addr_1);                /* 912 */
            menu_dbuff_ctrl.data_addr_1 = nullptr;                       /* 913 */
        }
    }
}

void MenuDBuffAllRelease(void)                                           /* 924 */
{
    MenuDBuffRelease(0);                                                 /* 930 */
    MenuDBuffRelease(1);                                                 /* 931 */
}

u_char GetMenuDBuffFlg(void)                                             /* 941 */
{
    return menu_dbuff_ctrl.dbuff_flg;                                    /* 945 */
}

u_int *GetMenuDBuffAddr(u_char flg)                                      /* 953 */
{
    u_int *addr;

    if (flg != 0) {                                                      /* 957 */
        addr = (u_int *)menu_dbuff_ctrl.data_addr_2;                     /* 958 */
    }
    else {
        addr = (u_int *)menu_dbuff_ctrl.data_addr_1;                     /* 961 */
    }

    return addr;                                                         /* 965 */
}

/* ==========================================================================
 *  Odds and ends
 * ======================================================================== */

/* Rewrite the picture's TEX0 for the pages the caller wants it in, then queue
 * the image and the CLUT.  Every menu texture is PSMT8 at 640 wide with a
 * 1024x512 sampler window, so only TBP0 and CBP are ever variable -- which is
 * why the whole register is rebuilt rather than patched. */
void MenuTim2SendVram(u_int *tim2_addr, int tbp, int cbp)                /* 978 */
{
    if (tim2_addr != nullptr) {                                          /* 981 */
        Tim2GetPictureHeader(tim2_addr, 0)->GsTex0 =                     /* 986 */
            SCE_GS_SET_TEX0(tbp, MENU_TIM2_TBW, SCE_GS_PSMT8,
                            MENU_TIM2_TW, MENU_TIM2_TH, 1, 0,
                            cbp, 0, 0, 0, 1);                            /* 989 */

        MakeTim2Direct(tim2_addr, tbp, 0);                               /* 994 */
        MakeClutDirect(tim2_addr, cbp, 0);                               /* 996 */
    }
}                                                                        /* 1000 */

void MenuInOutAnimCtrl(char *anim_step, char *anim_timer, u_char *alpha) /* 1012 */
{
    *alpha = Zero2Anim2D_InOutAnimCtrl(anim_step, anim_timer,
                                       MENU_IN_ANIM_TIME,
                                       MENU_OUT_ANIM_TIME);              /* 1016 */
}

/* The menu's own cursor pulse.  Unlike Zero2Anim2D_CsrAnimCtrl() this one
 * owns its table and its 45-frame wrap, and it ramps 64 -> 128 -> 64 rather
 * than holding at the top. */
void MenuCsrAnimCtrl(char *timer, u_char *rgb)                           /* 1026 */
{
    static const RGB_ANIM_TBL rgb_tbl[4] =                   /* rdata 3bd5f8 */
    {
        {  64, 128,  0, 15 },
        { 128, 128, 15, 30 },
        { 128,  64, 30, 45 },
        {  -1,  -1, -1, -1 },
    };

    *rgb = Anim2D_CalcNowRGB(rgb_tbl, *timer);                           /* 1036 */

    (*timer)++;                                                          /* 1038 */
    if (*timer >= MENU_CSR_ANIM_TIME) {                                  /* 1041 */
        *timer = 0;                                                      /* 1042 */
    }
}
