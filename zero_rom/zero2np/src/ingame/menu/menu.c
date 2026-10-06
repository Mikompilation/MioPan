// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu.c
//
// The in-game menu's spine: the nine-page dispatcher, and the animated shoji
// backdrop every page draws over.
//
// The shape is a pair of parallel machines that only meet in two places.
// menu_wrk.menu_step picks a row of menu_ctrl[9] and MenuMain() / MenuDispMain()
// call that row's two callbacks; a page swaps itself out by handing the next
// row to SetNextMenuStep(), which SetMenuStep() applies at the top of the next
// frame.  Separately menu_disp.menu_bg_anim runs the backdrop through
// NONE -> IN -> IN_END -> OUT -> OUT_END, and MenuDispMain() only lets the
// page draw during IN_END and OUT.  The two meet at MenuOutReq() (a page asks
// the backdrop to leave) and MenuOutCheck() (the backdrop tells MenuMain() it
// has gone).
//
// The backdrop is six layers, drawn back to front by MenuDispMain():
//
//   MenuCaptureDataDisp   the frame the game was on, blitted back out of the
//                         scratch VRAM page -- drawn only while the shoji are
//                         still moving, so the room shows through the gap
//   Left/RightShojiDisp   the two paper screens, sliding in from +-320
//   TourouDisp            the lantern: two pattern sheets scrolling at
//                         different rates, four corner vignettes, four amber
//                         motes and a black fill, all on one 1860-frame cycle
//   Left/RightShadowDisp  the vignette down each screen's inner edge
//   WallDisp              the wall the screens sit against
//
// Only the lantern layer animates once the menu is open; the other five are
// static after the 16-frame slide.  Its timer wraps 1859 -> 960 rather than to
// zero, so the first 960 frames are an intro the loop never plays again.
//
// bganim_in_to_out is what makes a menu closed mid-slide look right: the shoji
// have not reached their marks, so the out animation cannot simply play the in
// tables backwards.  Instead each layer fills in an out_alpha_tbl on the stack
// with its *current* alpha as the start value and fades that down over the
// remaining frames.  MenuDispCtrlInit() therefore seeds the flag to 1, and
// MenuBg_AnimCtrl() clears it the moment the slide completes.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Where a draw carries several assignments on one line
// (781, 1064, ...) that is what the stabs say: a sprite's x/y update and its
// scale/centre update are one source line each.
//
// Eleven of them are interpolated rather than measured, all where GCC dropped
// a note that was immediately followed by another: 914, 1041, 1043, 1045,
// 1047, 1059, 1070, 1083, 1104 and 1113 in MenuBg_TourouDisp(), and 1324 in
// MenuBg_WallDisp().  Each sits in a run whose neighbours are measured and
// whose shape repeats elsewhere in the file, so the number is forced; every
// other annotation here comes straight off a stab.

#include "menu.h"

#include "anim_2d.h"                            /* Anim2D_CalcNow*        */
#include "menu_cam.h"                           /* MenuCam / MenuCamDisp  */
#include "menu_cam_main.h"                      /* MenuCamMainBackGround* */
#include "menu_cmn.h"                           /* MenuDBuffCtrlInit      */
#include "menu_file.h"                          /* MenuFile*              */
#include "menu_item.h"                          /* MenuItem*              */
#include "menu_map.h"                           /* MenuMap*               */
#include "menu_memo.h"                          /* MenuMemo*              */
#include "menu_photo.h"                         /* MenuPhoto*             */
#include "menu_radio.h"                         /* MenuRadio*             */
#include "menu_soul.h"                          /* MenuSoul*              */
#include "menu_top.h"                           /* MenuTop*               */
#include "tim_dat/menu_top_dat.h"               /* menu_top[]             */

#include "../subtitle/subtitle.h"               /* SubTitleMain           */
#include "../../common/mem_util.h"              /* mem_utilGetMem         */
#include "../../common/utility2.h"              /* PRINT_ASSERT           */
#include "../../common/variable.h"              /* sys_wrk                */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / LocalCopy* */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram            */
#include "../../system/eeiop/cddat.h"           /* MENYU_STR / MENYU_HXD  */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET             */
#include "../../system/eeiop/stream_auto.h"     /* StreamAuto*            */

#include <string.h>                             /* memset                 */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* Where the frame behind the menu is parked.  MenuInDispCapture() reads the
 * back buffer (LocalCopyLtoB) into a heap block once, and SetMenuCaptureData()
 * pushes it back into this VRAM page (LocalCopyBtoL) every frame the picture
 * is still wanted -- which is why the still survives the menu drawing over it.
 * Same page movie.c decodes into, and the same 0x1180 page stride the pause
 * screens use. */
#define MENU_FRAME_BUF_ADRS     0x1180
#define MENU_CAPTURE_ADRS       0x2bc0

/* The still is a full 640x448 PSMCT32 page at TBP0 = MENU_CAPTURE_ADRS, point
 * sampled.  Neither register is in SPRT_DAT, so both are written by hand after
 * CopySprDToSpr(); movie.c writes the identical pair for the same page. */
#define MENU_CAPTURE_TEX0       0x200578026802abc0ULL
#define MENU_CAPTURE_TEX1       0x161

/* Frames the shoji take to slide in, and to slide back out of a fully open
 * menu.  A menu closed mid-slide gets 4 instead (see bganim_in_to_out). */
#define MENU_BG_OUT_TIME        15
#define MENU_BG_IN_TO_OUT_TIME  4

/* GS ALPHA settings the backdrop uses.  0x46 is Cd * (1 - As), the darkening
 * pass the two edge vignettes need; 0x48 is Cs * As + Cd, plain additive. */
#define MENU_ALPHA_DARKEN       0x46
#define MENU_ALPHA_ADD          0x48

/* The menu BGM, held for the whole visit.  0x3200 is the usual full volume
 * and 10 the fade-out length MenuMain() / MenuRelease() ask for. */
#define MENU_BGM_PRIORITY       20
#define MENU_BGM_VOLUME         0x3200
#define MENU_BGM_FADE_TIME      10

/* menu_top[] indices.  The two screens are the same five parts mirrored, and
 * 0/1 versus 6/7 are the left and right halves of the vignette and wall. */
#define MT_SHADOW_L             0
#define MT_WALL_L               1
#define MT_SHOJI_L_EDGE         2
#define MT_SHOJI_L_BODY         3
#define MT_SHADOW_R             6
#define MT_WALL_R               7
#define MT_SHOJI_R_EDGE         8
#define MT_SHOJI_R_BODY         9
#define MT_TOUROU_BODY          71
#define MT_TOUROU_TOP           72
#define MT_TOUROU_FLEA          73  /* [73..76] the four amber motes         */
#define MT_TOUROU_SHDW          77  /* [77..80] the four corner vignettes    */
#define MT_TOUROU_MOYOU1        81  /* 256x256 pattern sheet, slow scroll    */
#define MT_TOUROU_MOYOU2        82  /* 256x256 pattern sheet, fast scroll    */

static void MenuWrkInit(void);
static void MenuTexBackGroundLoad(void);
static void MenuInDispCapture(void);
static void MenuDispCtrlInit(void);
static void MenuBg_AnimCtrl(void);
static void SetMenuCaptureData(void);
static void MenuCaptureDataDisp(void);
static void MenuBg_LeftShojiDisp(int off_x, int off_y, u_char alpha);
static void MenuBg_RightShojiDisp(int off_x, int off_y, u_char alpha);
static void MenuBg_TourouDisp(int off_x, int off_y, u_char alpha);
static void MenuBg_LeftShadowDisp(int off_x, int off_y, u_char alpha);
static void MenuBg_RightShadowDisp(int off_x, int off_y, u_char alpha);
static void MenuBg_WallDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Data
 * ------------------------------------------------------------------------ */

/* The nine pages, indexed by menu_wrk.menu_step.  Both halves are tested for
 * NULL at every call even though no row has one. */
typedef struct                      /* 0x8 on the EE; pointers widen here */
{
    void (*menu_func)(void);
    void (*menu_disp)(void);
} MENU_CTRL;

static MENU_CTRL menu_ctrl[MENU_STEP_NUM] =                 /* data 321618 */
{
    { MenuMap,   MenuMapDisp   },       /* 0 map          */
    { MenuItem,  MenuItemDisp  },       /* 1 items        */
    { MenuCam,   MenuCamDisp   },       /* 2 camera       */
    { MenuPhoto, MenuPhotoDisp },       /* 3 photo album  */
    { MenuFile,  MenuFileDisp  },       /* 4 files        */
    { MenuMemo,  MenuMemoDisp  },       /* 5 notes        */
    { MenuRadio, MenuRadioDisp },       /* 6 radio        */
    { MenuSoul,  MenuSoulDisp  },       /* 7 ghost list   */
    { MenuTop,   MenuTopDisp   },       /* 8 the hub      */
};

/* Heap block holding the frame the menu opened over.  Claimed once and kept
 * for the whole visit; MenuRelease() gives it back. */
static void *menu_caption_adrs;                             /* sdata 3f2ae0 */

MENU_WRK menu_wrk;                                          /* data  321660 */
char     map_view_flg;                                      /* sdata 3f2b00 */

static MENU_DISP_CTRL menu_disp;                            /* bss   4b5430 */

/* ==========================================================================
 *  The frame loop
 * ======================================================================== */

int MenuMain(void)                                                      /* 144 */
{
    int menu_out = 0;

    SetMenuStep();                                                      /* 151 */

    if (menu_wrk.menu_sys_flg == 1) {                                   /* 154 */
        if (menu_ctrl[menu_wrk.menu_step].menu_func != nullptr) {       /* 156 */
            (*menu_ctrl[menu_wrk.menu_step].menu_func)();               /* 158 */
        }
        else {
            printf("ERROR!! MenuMain()\n");                             /* 162 */
        }
    }

    if (MenuOutCheck() == 1) {                                          /* 167 */
        /* A menu closed mid-slide never got as far as starting the BGM,
         * so there is nothing to fade and stream_id is still -1. */
        if (menu_disp.bganim_in_to_out == 0) {                          /* 168 */
            StreamAutoFadeOut(menu_wrk.stream_id, MENU_BGM_FADE_TIME);  /* 170 */
            menu_wrk.stream_id = -1;                                    /* 171 */
        }

        StreamAutoSetExclusiveMode(0, MENU_BGM_FADE_TIME);              /* 177 */

        menu_out = 1;                                                   /* 178 */
    }

    SubTitleMain(0);                                                    /* 181 */

    return menu_out;                                                    /* 184 */
}

/* ==========================================================================
 *  Opening
 * ======================================================================== */

void MenuIn(void)                                                       /* 195 */
{
    MenuWrkInit();                                                      /* 199 */

    MenuDispInit();                                                     /* 201 */

    MenuTexBackGroundLoad();                                            /* 208 */

    /* Take the exclusive streaming slot before anything else asks for it --
     * MenuBg_AnimCtrl() starts the BGM 16 frames from now. */
    StreamAutoSetExclusiveMode(1, MENU_BGM_FADE_TIME);                  /* 211 */

    menu_wrk.stream_id = -1;                                            /* 214 */

    map_view_flg = 0;                                                   /* 216 */
}

/* Lines 228..253 hold no code -- a comment or a disabled older reset. */
static void MenuWrkInit(void)                                           /* 224 */
{
    menu_wrk.menu_step = MENU_STEP_TOP;                                 /* 227 */

    menu_wrk.top_cursor = 0;                                            /* 254 */
    menu_wrk.cursor = 0;                                                /* 255 */
    menu_wrk.step = 0;                                                  /* 256 */
    menu_wrk.next_menu_step = -1;                                       /* 257 */
    menu_wrk.menu_out_flg = 0;                                          /* 258 */
    menu_wrk.menu_sys_flg = 1;                                          /* 259 */
}

/* Only the chapter-title plate is loaded here; the two backdrop paks are
 * already resident from outgame.c's boot-time IngameLoadOnce().  Lines
 * 274..301 hold no code.  The camera page gets the outgame heap's get/free
 * pair because its own background is claimed and dropped per visit. */
static void MenuTexBackGroundLoad(void)                                 /* 267 */
{
    GetMenuChapterTitleTexMem();                                        /* 271 */
    MenuChapterTitleTexLoadReq();                                       /* 273 */

    MenuCamMainBackGroundLoadReq(mem_utilGetMem, mem_utilFreeMem);      /* 302 */
}

/* ==========================================================================
 *  Closing
 * ======================================================================== */

/* A page asking to leave.  The two arms are the two ways the backdrop can be
 * standing when the request lands: fully open, which gets the full 15-frame
 * out; or still sliding in, which gets 4 and raises bganim_in_to_out so every
 * layer fades from where it actually is.  Any other state (already leaving,
 * or never started) falls through to the tail with no timer set at all. */
void MenuOutReq(void)                                                   /* 315 */
{
    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {                /* 319 */
        menu_disp.bg_anim_out_timer = MENU_BG_OUT_TIME;                 /* 320 */
        menu_disp.bganim_in_to_out = 0;                                 /* 321 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {               /* 323 */
        menu_disp.bg_anim_out_timer = MENU_BG_IN_TO_OUT_TIME;           /* 324 */
        menu_disp.bganim_in_to_out = 1;                                 /* 325 */
        menu_disp.menu_bg_anim = MENU_BG_ANIM_OUT;                      /* 326 */

        /* ROM BUG, reproduced: the unconditional store at 332 overwrites this
         * two instructions later, so the lantern plays its out animation even
         * on the mid-slide path that meant to skip it. */
        menu_disp.tourou_anim_step = MENU_TOUROU_ANIM_OUT_END;          /* 328 */
    }

    menu_disp.tourou_anim_step = MENU_TOUROU_ANIM_OUT;                  /* 332 */

    menu_disp.tourou_out_timer = 0;                                     /* 344 */

    /* Stop calling the page's own frame function; only its drawing runs from
     * here on, and MenuDispMain() drops that too once the fade passes OUT. */
    menu_wrk.menu_sys_flg = 0;                                          /* 346 */
    menu_wrk.menu_out_flg = 1;                                          /* 347 */
}

int MenuOutCheck(void)                                                  /* 357 */
{
    int res = 0;

    if (menu_wrk.menu_out_flg == 1) {                                   /* 365 */
        res = (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT_END);         /* 367 */
    }

    return res;                                                         /* 373 */
}

/* ==========================================================================
 *  Page switching
 * ======================================================================== */

void SetNextMenuStep(int next_step)                                     /* 385 */
{
    menu_wrk.next_menu_step = (char)next_step;                          /* 388 */
}

/* Applied at the top of MenuMain(), so a page that switches away still gets
 * to finish the frame it asked on. */
void SetMenuStep(void)                                                  /* 397 */
{
    if (menu_wrk.next_menu_step != -1) {                                /* 400 */
        menu_wrk.menu_step = (u_char)menu_wrk.next_menu_step;           /* 401 */
        menu_wrk.step = 0;                                              /* 402 */
        menu_wrk.next_menu_step = -1;                                   /* 403 */
    }
}

void MenuRelease(void)                                                  /* 416 */
{
    /* Cancel first: a page whose load is still in flight must not have its
     * buffer freed underneath the loader. */
    MenuItemTexLoadCancel();                                            /* 419 */
    MenuPhotoTexLoadCancel();                                           /* 420 */
    MenuRadioTexLoadCancel();                                           /* 421 */
    MenuMemoTexLoadCancel();                                            /* 422 */
    MenuSoulTexLoadCancel();                                            /* 427 */

    if (menu_caption_adrs != nullptr) {                                 /* 430 */
        mem_utilFreeMem(menu_caption_adrs);                             /* 432 */
        menu_caption_adrs = nullptr;                                    /* 433 */
    }

    /* Normally MenuMain() has already faded this out and parked -1; the test
     * covers a release from somewhere that never ran the out animation. */
    if (menu_wrk.stream_id != -1) {                                     /* 435 */
        StreamAutoFadeOut(menu_wrk.stream_id, MENU_BGM_FADE_TIME);      /* 436 */
    }

    MenuDBuffCtrlInit();                                                /* 439 */

    MenuMapRelease();                                                   /* 441 */

    MenuFileMemRelease();                                               /* 443 */

    LiberateMenuChapterTitleTexMem();                                   /* 445 */
    LiberateMenuItemTexMem();                                           /* 446 */
    LiberateMenuPhotoTexMem();                                          /* 447 */
    LiberateMenuRadioTexMem();                                          /* 448 */
    LiberateMenuMemoTexMem();                                           /* 449 */

    MenuCamMainMemFree();                                               /* 454 */

    LiberateMenuSoulTexMem();                                           /* 456 */
    LiberateAllMenuCrossFadeTexMem();                                   /* 457 */

    MenuRadioStreamStop();                                              /* 459 */
}

/* ==========================================================================
 *  Backdrop -- set-up
 * ======================================================================== */

void MenuDispInit(void)                                                 /* 477 */
{
    MenuInDispCapture();                                                /* 482 */

    MenuDispCtrlInit();                                                 /* 485 */
}

/* Claim the still-frame buffer and fill it from the back buffer -- the one
 * the game has just finished drawing, hence the +1 on the frame counter.
 *
 * The assert names MenuInDispCaption(), not MenuInDispCapture(); the ROM's own
 * typo, and only the message carries it -- __FUNCTION__ is spelt right. */
static void MenuInDispCapture(void)                                     /* 494 */
{
    if (menu_caption_adrs == nullptr) {                                 /* 497 */
        menu_caption_adrs = mem_utilGetMem(LocalCopyLtoBGetSize(0));    /* 498 */
    }
    else {
        PRINT_ASSERT("MenuInDispCaption() adrs Is not NULL");           /* 500 */
    }

    LocalCopyLtoBAdrs(0, (uintptr_t)menu_caption_adrs,
                      (int)(((sys_wrk.count + 1) & 1) * MENU_FRAME_BUF_ADRS)); /* 502 */
}

static void MenuDispCtrlInit(void)                                      /* 511 */
{
    menu_disp.anim_step = 1;                                            /* 514 */
    menu_disp.bg_anim_timer = 0;                                        /* 515 */
    menu_disp.bg_anim_out_timer = 0;                                    /* 516 */
    menu_disp.menu_bg_anim = MENU_BG_ANIM_NONE;                         /* 517 */
    menu_disp.tourou_anim_step = MENU_TOUROU_ANIM_NONE;                 /* 518 */
    menu_disp.tourou_out_timer = 0;                                     /* 519 */
    menu_disp.tourou_anim_timer = 0;                                    /* 520 */
    menu_disp.moyou1_anim_timer = 0;                                    /* 521 */
    menu_disp.moyou2_anim_timer = 0;                                    /* 522 */

    /* Starts raised: until the slide-in completes, a close has to fade from
     * wherever the layers happen to be.  MenuBg_AnimCtrl() clears it. */
    menu_disp.bganim_in_to_out = 1;                                     /* 523 */
}

/* ==========================================================================
 *  Backdrop -- one frame
 * ======================================================================== */

void MenuDispMain(void)                                                 /* 536 */
{
    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_NONE) {                  /* 540 */
        menu_disp.bg_anim_timer = 0;                                    /* 541 */
        menu_disp.menu_bg_anim = MENU_BG_ANIM_IN;                       /* 543 */
    }

    SetMenuCaptureData();                                               /* 550 */

    /* The still is only wanted while the shoji have not met -- once they are
     * closed there is nothing of the room left to see through them. */
    if (menu_disp.menu_bg_anim != MENU_BG_ANIM_IN_END) {                /* 553 */
        MenuCaptureDataDisp();                                          /* 555 */
    }

    MenuBg_LeftShojiDisp(0, 0, 0x80);                                   /* 564 */
    MenuBg_RightShojiDisp(0, 0, 0x80);                                  /* 566 */

    MenuBg_TourouDisp(0, 0, 0x80);                                      /* 569 */

    MenuBg_LeftShadowDisp(0, 0, 0x80);                                  /* 572 */
    MenuBg_RightShadowDisp(0, 0, 0x80);                                 /* 574 */

    MenuBg_WallDisp(0, 0, 0x80);                                        /* 577 */

    /* A page draws only over a settled backdrop, and never over one that is
     * leaving from a slide it never finished. */
    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END ||
        menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {                   /* 579 */
        if (menu_disp.bganim_in_to_out == 0) {                          /* 580 */
            if (menu_ctrl[menu_wrk.menu_step].menu_disp != nullptr) {   /* 582 */
                (*menu_ctrl[menu_wrk.menu_step].menu_disp)();           /* 584 */
            }
            else {
                PRINT_ASSERT("ERROR!! MenuDispMain()");                 /* 588 */
            }
        }
    }

    MenuBg_AnimCtrl();                                                  /* 594 */
}

/* The backdrop's state machine.  Note the BGM does not start until the shoji
 * have closed -- a menu opened and cancelled inside 16 frames plays nothing,
 * which is what MenuMain()'s bganim_in_to_out test is guarding. */
static void MenuBg_AnimCtrl(void)                                       /* 603 */
{
    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {                    /* 607 */
        menu_disp.bg_anim_timer++;                                      /* 608 */
        if (menu_disp.bg_anim_timer > 15) {                             /* 610 */
            menu_wrk.stream_id = StreamAutoPlay(MENYU_STR, MENYU_HXD,
                                                MENU_BGM_PRIORITY, 0, 1,
                                                MENU_BGM_VOLUME, 0,
                                                (SND_3D_SET *)nullptr); /* 613 */
            menu_disp.menu_bg_anim = MENU_BG_ANIM_IN_END;               /* 614 */
            menu_disp.bganim_in_to_out = 0;                             /* 615 */
        }
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {           /* 619 */
        if (menu_disp.tourou_anim_step == MENU_TOUROU_ANIM_LOOP) {      /* 620 */
            menu_disp.moyou1_anim_timer++;                              /* 622 */
            if (menu_disp.moyou1_anim_timer > 899) {                    /* 623 */
                menu_disp.moyou1_anim_timer = 0;                        /* 624 */
            }

            menu_disp.moyou2_anim_timer++;                              /* 626 */
            if (menu_disp.moyou2_anim_timer > 599) {                    /* 627 */
                menu_disp.moyou2_anim_timer = 0;                        /* 628 */
            }

            /* The authored curves run 0..1859, but the wrap is to 960, so
             * the first 960 frames are an intro that never comes back. */
            menu_disp.tourou_anim_timer++;                              /* 631 */
            if (menu_disp.tourou_anim_timer > 1859) {                   /* 633 */
                menu_disp.tourou_anim_timer = 960;                      /* 634 */
            }
        }
        else if (menu_disp.tourou_anim_step == MENU_TOUROU_ANIM_OUT) {  /* 638 */
            menu_disp.moyou1_anim_timer++;                              /* 640 */
            if (menu_disp.moyou1_anim_timer > 899) {                    /* 641 */
                menu_disp.moyou1_anim_timer = 0;                        /* 642 */
            }

            menu_disp.moyou2_anim_timer++;                              /* 644 */
            if (menu_disp.moyou2_anim_timer > 599) {                    /* 645 */
                menu_disp.moyou2_anim_timer = 0;                        /* 646 */
            }

            menu_disp.tourou_out_timer++;                               /* 649 */
            if (menu_disp.tourou_out_timer > 7) {                       /* 651 */
                menu_disp.menu_bg_anim = MENU_BG_ANIM_OUT;              /* 653 */

                menu_disp.tourou_anim_step = MENU_TOUROU_ANIM_OUT_END;  /* 657 */
            }
        }
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {              /* 662 */
        menu_disp.bg_anim_out_timer--;                                  /* 663 */
        if (menu_disp.bg_anim_out_timer <= 0) {                         /* 665 */
            menu_disp.menu_bg_anim = MENU_BG_ANIM_OUT_END;              /* 666 */
        }
    }
}

/* ==========================================================================
 *  Backdrop -- the captured frame
 * ======================================================================== */

/* Push the still back into the scratch VRAM page, which the GS then samples
 * from.  Run every frame the menu is up, because whatever the game drew last
 * frame is still sitting in that page otherwise. */
static void SetMenuCaptureData(void)                                    /* 676 */
{
    LocalCopyBtoLAdrs(0, (uintptr_t)menu_caption_adrs,
                      MENU_CAPTURE_ADRS);                               /* 682 */
}

static void MenuCaptureDataDisp(void)                                   /* 692 */
{
    SPRT_DAT  sd = { 0, 0, 0, 640, 448, 0, 0, 0xe0, 0x80, 0, 1 };       /* 694 */
    DISP_SPRT ds;

    memset(&ds, 0, sizeof(ds));                                         /* 698 */

    CopySprDToSpr(&ds, &sd);                                            /* 701 */

    ds.tex0 = MENU_CAPTURE_TEX0;                                        /* 702 */
    ds.tex1 = MENU_CAPTURE_TEX1;                                        /* 704 */

    DispSprD(&ds);                                                      /* 705 */
}

/* ==========================================================================
 *  Backdrop -- the two shoji
 * ======================================================================== */

/* The left paper screen: a body, two rails and an edge strip, all sliding in
 * together from x = -320.
 *
 * The three arms of the chain pick where shouji_off_x and shouji_alpha come
 * from -- the in tables against bg_anim_timer, the settled values, or (when
 * leaving) either the same tables run against bg_anim_out_timer or, if the
 * slide never finished, a two-entry fade from wherever the alpha had got to. */
static void MenuBg_LeftShojiDisp(int off_x, int off_y, u_char alpha)     /* 718 */
{
    DISP_SPRT bg_ds;
    int       i;
    float     shouji_off_x = -320.0f;                                   /* 727 */
    u_char    shouji_alpha = 0;

    static int shoji_tbl[2] = { 4, 5 };                     /* sdata 3f2af0 */

    static const POS_ANIM_TBL shouji_x_tbl[3] =             /* rdata 3bc5b8 */
    {
        { -320.0f, -320.0f,  0,  4,  1 },
        { -320.0f,    0.0f,  4, 16,  1 },
        {   -1.0f,   -1.0f, -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL shouji_alpha_tbl[3] =       /* rdata 3bc5e8 */
    {
        {  0,   0,  0,  4 },
        {  0, 128,  4, 16 },
        { -1,  -1, -1, -1 },
    };

    ALPHA_ANIM_TBL in_to_out_alpha_tbl[2] =                             /* 741 */
    {
        {  0,  0,  0,  5 },
        { -1, -1, -1, -1 },
    };

    (void)off_x;                /* all three parameters are dead in the ROM */
    (void)off_y;
    (void)alpha;

    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {                    /* 747 */
        shouji_off_x = Anim2D_CalcNowPos(shouji_x_tbl,
                                         menu_disp.bg_anim_timer);      /* 749 */
        shouji_alpha = Anim2D_CalcNowAlpha(shouji_alpha_tbl,
                                           menu_disp.bg_anim_timer);    /* 751 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {           /* 753 */
        shouji_off_x = 0.0f;                                            /* 754 */
        shouji_alpha = 0x80;                                            /* 755 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {              /* 757 */
        if (menu_disp.bganim_in_to_out != 0) {                          /* 758 */
            shouji_off_x = Anim2D_CalcNowPos(shouji_x_tbl,
                                             menu_disp.bg_anim_timer);  /* 760 */
            in_to_out_alpha_tbl[0].end_alpha =
                Anim2D_CalcNowAlpha(shouji_alpha_tbl,
                                    menu_disp.bg_anim_timer);           /* 762 */

            shouji_alpha = Anim2D_CalcNowAlpha(in_to_out_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 766 */
        }
        else {
            shouji_off_x = Anim2D_CalcNowPos(shouji_x_tbl,
                                            menu_disp.bg_anim_out_timer); /* 770 */
            shouji_alpha = Anim2D_CalcNowAlpha(shouji_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 772 */
        }
    }

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 777 */

    /* The body, upright and doubled. */
    CopySprDToSpr(&bg_ds, &menu_top[MT_SHOJI_L_BODY]);                  /* 779 */

    bg_ds.x = bg_ds.x + shouji_off_x;                                   /* 780 */

    bg_ds.csx = bg_ds.x;                                                /* 781 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = shouji_alpha;                                         /* 782 */

    DispSprD(&bg_ds);                                                   /* 783 */

    /* The two rails, laid on their sides and stacked down the screen -- with
     * rot 270 the sprite's own w is what steps y. */
    for (i = 0; i < 2; i++) {                                           /* 785 */
        CopySprDToSpr(&bg_ds, &menu_top[shoji_tbl[i]]);                 /* 786 */

        bg_ds.x = bg_ds.x + shouji_off_x;                               /* 787 */
        bg_ds.y = bg_ds.y + (float)bg_ds.w;

        bg_ds.crx = bg_ds.x;                                            /* 788 */
        bg_ds.cry = bg_ds.y;
        bg_ds.rot = 270.0f;

        bg_ds.alpha = shouji_alpha;                                     /* 789 */

        DispSprD(&bg_ds);                                               /* 790 */
    }                                                                   /* 791 */

    /* The edge strip, two rail-widths further down and doubled like the body. */
    CopySprDToSpr(&bg_ds, &menu_top[MT_SHOJI_L_EDGE]);                  /* 793 */

    bg_ds.x = bg_ds.x + shouji_off_x;                                   /* 794 */
    bg_ds.y = bg_ds.y + (float)bg_ds.w * 2.0f;

    bg_ds.crx = bg_ds.x;                                                /* 795 */
    bg_ds.cry = bg_ds.y;
    bg_ds.rot = 270.0f;

    bg_ds.csx = bg_ds.x;                                                /* 796 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = shouji_alpha;                                         /* 797 */

    DispSprD(&bg_ds);                                                   /* 798 */
}

/* The right screen, mirrored -- same five parts from menu_top[8..11] sliding
 * in from +320.
 *
 * It is NOT quite the same function twice: the OUT test here is a fresh `if`
 * rather than the third arm of the chain, so the IN and IN_END arms fall into
 * it.  Neither can be OUT, so it makes no difference at run time, but the
 * compiler reloads menu_bg_anim for it and the left-hand version does not. */
static void MenuBg_RightShojiDisp(int off_x, int off_y, u_char alpha)    /* 811 */
{
    DISP_SPRT bg_ds;
    int       i;
    float     shouji_off_x = 320.0f;                                    /* 820 */
    u_char    shouji_alpha = 0;

    static int shoji_tbl[2] = { 10, 11 };                   /* sdata 3f2af8 */

    static const POS_ANIM_TBL shouji_x_tbl[3] =             /* rdata 3bc610 */
    {
        { 320.0f, 320.0f,  0,  4,  1 },
        { 320.0f,   0.0f,  4, 16,  1 },
        {  -1.0f,  -1.0f, -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL shouji_alpha_tbl[3] =       /* rdata 3bc640 */
    {
        {  0,   0,  0,  4 },
        {  0, 128,  4, 16 },
        { -1,  -1, -1, -1 },
    };

    ALPHA_ANIM_TBL in_to_out_alpha_tbl[2] =                             /* 834 */
    {
        {  0,  0,  0,  5 },
        { -1, -1, -1, -1 },
    };

    (void)off_x;
    (void)off_y;
    (void)alpha;

    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {                    /* 840 */
        shouji_off_x = Anim2D_CalcNowPos(shouji_x_tbl,
                                         menu_disp.bg_anim_timer);      /* 842 */
        shouji_alpha = Anim2D_CalcNowAlpha(shouji_alpha_tbl,
                                           menu_disp.bg_anim_timer);    /* 844 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {           /* 846 */
        shouji_off_x = 0.0f;                                            /* 847 */
        shouji_alpha = 0x80;                                            /* 848 */
    }

    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {                   /* 850 */
        if (menu_disp.bganim_in_to_out != 0) {                          /* 851 */
            shouji_off_x = Anim2D_CalcNowPos(shouji_x_tbl,
                                             menu_disp.bg_anim_timer);  /* 853 */
            in_to_out_alpha_tbl[0].end_alpha =
                Anim2D_CalcNowAlpha(shouji_alpha_tbl,
                                    menu_disp.bg_anim_timer);           /* 855 */

            shouji_alpha = Anim2D_CalcNowAlpha(in_to_out_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 859 */
        }
        else {
            shouji_off_x = Anim2D_CalcNowPos(shouji_x_tbl,
                                            menu_disp.bg_anim_out_timer); /* 863 */
            shouji_alpha = Anim2D_CalcNowAlpha(shouji_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 865 */
        }
    }

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 870 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_SHOJI_R_BODY]);                  /* 872 */

    bg_ds.x = bg_ds.x + shouji_off_x;                                   /* 873 */

    bg_ds.csx = bg_ds.x;                                                /* 874 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = shouji_alpha;                                         /* 875 */

    DispSprD(&bg_ds);                                                   /* 876 */

    for (i = 0; i < 2; i++) {                                           /* 878 */
        CopySprDToSpr(&bg_ds, &menu_top[shoji_tbl[i]]);                 /* 879 */

        bg_ds.x = bg_ds.x + shouji_off_x;                               /* 880 */
        bg_ds.y = bg_ds.y + (float)bg_ds.w;

        bg_ds.crx = bg_ds.x;                                            /* 881 */
        bg_ds.cry = bg_ds.y;
        bg_ds.rot = 270.0f;

        bg_ds.alpha = shouji_alpha;                                     /* 882 */

        DispSprD(&bg_ds);                                               /* 883 */
    }                                                                   /* 884 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_SHOJI_R_EDGE]);                  /* 886 */

    bg_ds.x = bg_ds.x + shouji_off_x;                                   /* 887 */
    bg_ds.y = bg_ds.y + (float)bg_ds.w * 2.0f;

    bg_ds.crx = bg_ds.x;                                                /* 888 */
    bg_ds.cry = bg_ds.y;
    bg_ds.rot = 270.0f;

    bg_ds.csx = bg_ds.x;                                                /* 889 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = shouji_alpha;                                         /* 890 */

    DispSprD(&bg_ds);                                                   /* 891 */
}

/* ==========================================================================
 *  Backdrop -- the lantern
 * ======================================================================== */

/* The lantern behind the screens, and the only layer that keeps moving once
 * the menu is open.
 *
 * Five values come off one clock: two pattern sheets that scroll on 900- and
 * 600-frame loops, the corner vignettes, the amber motes and the black fill,
 * each with its own hand-authored curve against tourou_anim_timer.  The two
 * sheets are drawn twice apiece, the second copy 640 to the left, so the wrap
 * never shows a seam.
 *
 * The out animation reuses one stack table five times: each element's current
 * alpha is written into out_alpha_tbl[0].start_alpha and faded to zero over
 * the eight frames tourou_out_timer counts.  Only the 1041/1043/1045/1047
 * writes lost their line markers -- 1049 kept one, which is what pins the
 * pattern down as two statements per element rather than one.
 *
 * Lines 918..1004 are the seven static tables' own declarations. */
static void MenuBg_TourouDisp(int off_x, int off_y, u_char alpha)        /* 904 */
{
    DISP_SPRT bg_ds;
    int       i;
    float     moyou1_x;
    float     moyou2_x;
    u_char    moyou1_alpha = 0;                                         /* 911 */
    u_char    moyou2_alpha = 0;                                         /* 912 */
    u_char    shdw_alpha = 0;                                           /* 913 */
    u_char    flea_alpha = 0;                                           /* 914 */
    u_char    bg_alpha = 0;                                             /* 915 */
    DISP_SQAR dsq;
    SQAR_DAT  bg_sqar = { 279, 448, 545, 0, 160, 0, 0, 0, 0 };          /* 917 */

    static const POS_ANIM_TBL moyou1_x_tbl[2] =             /* rdata 3bc680 */
    {
        {  0.0f, 640.0f,  0, 900,  0 },
        { -1.0f,  -1.0f, -1,  -1, -1 },
    };

    static const POS_ANIM_TBL moyou2_x_tbl[2] =             /* rdata 3bc6a0 */
    {
        {  0.0f, 640.0f,  0, 600,  0 },
        { -1.0f,  -1.0f, -1,  -1, -1 },
    };

    static const ALPHA_ANIM_TBL moyou1_alpha_tbl[14] =      /* rdata 3bc6c0 */
    {
        {  0,  0,    0,   60 },
        {  0, 38,   60,  240 },
        { 38, 51,  240,  360 },
        { 51, 25,  360,  450 },
        { 25, 51,  450,  510 },
        { 51, 83,  510,  660 },
        { 83, 19,  660,  960 },
        { 19, 51,  960, 1110 },
        { 51, 12, 1110, 1210 },
        { 12, 57, 1210, 1400 },
        { 57, 12, 1400, 1680 },
        { 12, 25, 1680, 1810 },
        { 25, 19, 1810, 1860 },
        { -1, -1,   -1,   -1 },
    };

    static const ALPHA_ANIM_TBL moyou2_alpha_tbl[16] =      /* rdata 3bc730 */
    {
        {  0,  0,    0,   60 },
        {  0, 38,   60,  240 },
        { 38, 51,  240,  360 },
        { 51, 12,  360,  450 },
        { 12, 64,  450,  480 },
        { 64, 38,  480,  600 },
        { 38, 76,  600,  750 },
        { 76, 38,  750,  960 },
        { 38, 64,  960, 1080 },
        { 64, 12, 1080, 1150 },
        { 12, 38, 1150, 1320 },
        { 38, 83, 1320, 1440 },
        { 83,  6, 1440, 1660 },
        {  6, 51, 1660, 1835 },
        { 51, 38, 1835, 1860 },
        { -1, -1,   -1,   -1 },
    };

    static const ALPHA_ANIM_TBL shdw_alpha_tbl[10] =        /* rdata 3bc7b0 */
    {
        {   0, 128,    0,   60 },
        { 128, 102,   60,  180 },
        { 102, 128,  180,  450 },
        { 128,  89,  450,  600 },
        {  89,  96,  600,  960 },
        {  96,  70,  960, 1320 },
        {  70, 108, 1320, 1445 },
        { 108, 115, 1445, 1560 },
        { 115,  96, 1560, 1860 },
        {  -1,  -1,   -1,   -1 },
    };

    static const ALPHA_ANIM_TBL flea_alpha_tbl[18] =        /* rdata 3bc800 */
    {
        {   0,   0,    0,   60 },
        {   0,  19,   60,   66 },
        {  19,   0,   66,   70 },
        {   0,  12,   70,  100 },
        {  12,  25,  100,  108 },
        {  25,  19,  108,  112 },
        {  19,  25,  112,  131 },
        {  25,  19,  131,  137 },
        {  19,  51,  137,  210 },
        {  51,   6,  210,  450 },
        {   6, 108,  450,  680 },
        { 108,  19,  680,  960 },
        {  19,   0,  960, 1190 },
        {   0,  12, 1190, 1280 },
        {  12,  76, 1280, 1380 },
        {  76,   6, 1380, 1575 },
        {   6,  19, 1575, 1860 },
        {  -1,  -1,   -1,   -1 },
    };

    static const ALPHA_ANIM_TBL bg_alpha_tbl[3] =           /* rdata 3bc890 */
    {
        {   0, 128,   0,   60 },
        { 128, 128,  60, 1860 },
        {  -1,  -1,  -1,   -1 },
    };

    ALPHA_ANIM_TBL out_alpha_tbl[2] =                                   /* 1005 */
    {
        { 128,  0,  0,  8 },
        {  -1, -1, -1, -1 },
    };

    (void)off_x;
    (void)off_y;
    (void)alpha;

    /* Straight from NONE to LOOP -- step 1 is never used. */
    if (menu_disp.tourou_anim_step == MENU_TOUROU_ANIM_NONE) {          /* 1011 */
        menu_disp.tourou_anim_timer = 0;                                /* 1013 */
        menu_disp.moyou1_anim_timer = 0;                                /* 1014 */
        menu_disp.moyou2_anim_timer = 0;                                /* 1015 */
        menu_disp.tourou_anim_step = MENU_TOUROU_ANIM_LOOP;             /* 1016 */
    }

    moyou1_x = Anim2D_CalcNowPos(moyou1_x_tbl,
                                 menu_disp.moyou1_anim_timer);          /* 1020 */
    moyou2_x = Anim2D_CalcNowPos(moyou2_x_tbl,
                                 menu_disp.moyou2_anim_timer);          /* 1021 */

    if (menu_disp.tourou_anim_step == MENU_TOUROU_ANIM_LOOP) {          /* 1023 */
        moyou1_alpha = Anim2D_CalcNowAlpha(moyou1_alpha_tbl,
                                           menu_disp.tourou_anim_timer); /* 1025 */
        moyou2_alpha = Anim2D_CalcNowAlpha(moyou2_alpha_tbl,
                                           menu_disp.tourou_anim_timer); /* 1026 */
        shdw_alpha = Anim2D_CalcNowAlpha(shdw_alpha_tbl,
                                         menu_disp.tourou_anim_timer);  /* 1027 */
        flea_alpha = Anim2D_CalcNowAlpha(flea_alpha_tbl,
                                         menu_disp.tourou_anim_timer);  /* 1028 */
        bg_alpha = Anim2D_CalcNowAlpha(bg_alpha_tbl,
                                       menu_disp.tourou_anim_timer);    /* 1029 */
    }
    else if (menu_disp.tourou_anim_step == MENU_TOUROU_ANIM_OUT) {      /* 1032 */
        moyou1_alpha = Anim2D_CalcNowAlpha(moyou1_alpha_tbl,
                                           menu_disp.tourou_anim_timer); /* 1034 */
        moyou2_alpha = Anim2D_CalcNowAlpha(moyou2_alpha_tbl,
                                           menu_disp.tourou_anim_timer); /* 1035 */
        shdw_alpha = Anim2D_CalcNowAlpha(shdw_alpha_tbl,
                                         menu_disp.tourou_anim_timer);  /* 1036 */
        flea_alpha = Anim2D_CalcNowAlpha(flea_alpha_tbl,
                                         menu_disp.tourou_anim_timer);  /* 1037 */
        bg_alpha = Anim2D_CalcNowAlpha(bg_alpha_tbl,
                                       menu_disp.tourou_anim_timer);    /* 1038 */

        out_alpha_tbl[0].start_alpha = moyou1_alpha;                    /* 1041 */
        moyou1_alpha = Anim2D_CalcNowAlpha(out_alpha_tbl,
                                           menu_disp.tourou_out_timer); /* 1042 */

        out_alpha_tbl[0].start_alpha = moyou2_alpha;                    /* 1043 */
        moyou2_alpha = Anim2D_CalcNowAlpha(out_alpha_tbl,
                                           menu_disp.tourou_out_timer); /* 1044 */

        out_alpha_tbl[0].start_alpha = shdw_alpha;                      /* 1045 */
        shdw_alpha = Anim2D_CalcNowAlpha(out_alpha_tbl,
                                         menu_disp.tourou_out_timer);   /* 1046 */

        out_alpha_tbl[0].start_alpha = flea_alpha;                      /* 1047 */
        flea_alpha = Anim2D_CalcNowAlpha(out_alpha_tbl,
                                         menu_disp.tourou_out_timer);   /* 1048 */

        out_alpha_tbl[0].start_alpha = bg_alpha;                        /* 1049 */
        bg_alpha = Anim2D_CalcNowAlpha(out_alpha_tbl,
                                       menu_disp.tourou_out_timer);     /* 1050 */
    }

    /* The lantern has its own pak, loaded at boot beside the backdrop's. */
    PK2SendVram(MENU_TOUROU_TEX_ADRS, -1, -1, 0);                       /* 1054 */

    /* The lantern body, and its cap laid on its side above it. */
    CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_BODY]);                   /* 1057 */

    bg_ds.csx = bg_ds.x;                                                /* 1058 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.01999998f;    /* lit4 3ee508, i.e. 2.02f */
    bg_ds.sch = 2.01999998f;

    bg_ds.alpha = bg_alpha;                                             /* 1059 */

    DispSprD(&bg_ds);                                                   /* 1060 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_TOP]);                    /* 1061 */

    bg_ds.x = bg_ds.x + (float)bg_ds.h * 2.01999998f;                   /* 1062 */

    bg_ds.crx = bg_ds.x;                                                /* 1063 */
    bg_ds.cry = bg_ds.y;
    bg_ds.rot = 90.0f;

    bg_ds.csx = bg_ds.x;                                                /* 1064 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.01999998f;
    bg_ds.sch = 2.01999998f;

    bg_ds.alpha = bg_alpha;                                             /* 1065 */

    DispSprD(&bg_ds);                                                   /* 1066 */

    /* Pattern sheet 2, drawn twice: the second copy 640 to the left, so the
     * pair covers the screen at every point of the scroll.  Both are stretched
     * to a full 640x448 whatever the source size is. */
    CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_MOYOU2]);                 /* 1069 */

    bg_ds.x = moyou2_x;                                                 /* 1070 */

    bg_ds.scw = 640.0f / (float)bg_ds.w;                                /* 1071 */
    bg_ds.sch = 448.0f / (float)bg_ds.h;
    bg_ds.csx = bg_ds.x;
    bg_ds.csy = bg_ds.y;

    bg_ds.alpha = moyou2_alpha;                                         /* 1072 */
    bg_ds.alphar = MENU_ALPHA_ADD;                                      /* 1073 */

    DispSprD(&bg_ds);                                                   /* 1074 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_MOYOU2]);                 /* 1075 */

    bg_ds.x = moyou2_x - 640.0f;                                        /* 1076 */

    bg_ds.scw = 640.0f / (float)bg_ds.w;                                /* 1077 */
    bg_ds.sch = 448.0f / (float)bg_ds.h;
    bg_ds.csx = bg_ds.x;
    bg_ds.csy = bg_ds.y;

    bg_ds.alpha = moyou2_alpha;                                         /* 1078 */
    bg_ds.alphar = MENU_ALPHA_ADD;                                      /* 1079 */

    DispSprD(&bg_ds);                                                   /* 1080 */

    /* Pattern sheet 1, the same pair on the slower clock. */
    CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_MOYOU1]);                 /* 1082 */

    bg_ds.x = moyou1_x;                                                 /* 1083 */

    bg_ds.scw = 640.0f / (float)bg_ds.w;                                /* 1084 */
    bg_ds.sch = 448.0f / (float)bg_ds.h;
    bg_ds.csx = bg_ds.x;
    bg_ds.csy = bg_ds.y;

    bg_ds.alpha = moyou1_alpha;                                         /* 1085 */
    bg_ds.alphar = MENU_ALPHA_ADD;                                      /* 1086 */

    DispSprD(&bg_ds);                                                   /* 1087 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_MOYOU1]);                 /* 1088 */

    bg_ds.x = moyou1_x - 640.0f;                                        /* 1089 */

    bg_ds.scw = 640.0f / (float)bg_ds.w;                                /* 1090 */
    bg_ds.sch = 448.0f / (float)bg_ds.h;
    bg_ds.csx = bg_ds.x;
    bg_ds.csy = bg_ds.y;

    bg_ds.alpha = moyou1_alpha;                                         /* 1091 */
    bg_ds.alphar = MENU_ALPHA_ADD;                                      /* 1092 */

    DispSprD(&bg_ds);                                                   /* 1093 */

    /* The four corner vignettes, 4.5x and forced black. */
    for (i = 0; i < 4; i++) {                                           /* 1096 */
        CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_SHDW + i]);           /* 1097 */

        bg_ds.csx = bg_ds.x;                                            /* 1098 */
        bg_ds.csy = bg_ds.y;
        bg_ds.scw = 4.5f;
        bg_ds.sch = 4.5f;

        bg_ds.alpha = shdw_alpha;                                       /* 1099 */

        bg_ds.r = 0;    bg_ds.g = 0;    bg_ds.b = 0;                    /* 1100 */

        DispSprD(&bg_ds);                                               /* 1101 */
    }                                                                   /* 1102 */

    /* The black fill down the right of the lantern, on the vignettes' alpha. */
    CopySqrDToSqr(&dsq, &bg_sqar);                                      /* 1103 */

    dsq.alpha = shdw_alpha;                                             /* 1104 */

    DispSqrD(&dsq);                                                     /* 1105 */

    /* The four amber motes, 2.4x and additive. */
    for (i = 0; i < 4; i++) {                                           /* 1108 */
        CopySprDToSpr(&bg_ds, &menu_top[MT_TOUROU_FLEA + i]);           /* 1109 */

        bg_ds.r = 0xff; bg_ds.g = 121;  bg_ds.b = 62;                   /* 1110 */

        bg_ds.alpha = flea_alpha;                                       /* 1111 */

        bg_ds.csx = bg_ds.x;                                            /* 1112 */
        bg_ds.csy = bg_ds.y;
        bg_ds.scw = 2.39999986f;    /* lit4 3ee510, i.e. 2.4f */
        bg_ds.sch = 2.39999986f;

        bg_ds.alphar = MENU_ALPHA_ADD;                                  /* 1113 */

        DispSprD(&bg_ds);                                               /* 1114 */
    }                                                                   /* 1115 */
}

/* ==========================================================================
 *  Backdrop -- the edge vignettes and the wall
 * ======================================================================== */

/* The shading down the left screen's inner edge.  One sprite, rotated 90 and
 * pushed right by two of its own heights, darkening whatever is under it
 * (alphar 0x46 = Cd * (1 - As), which never reads the source colour). */
static void MenuBg_LeftShadowDisp(int off_x, int off_y, u_char alpha)    /* 1128 */
{
    DISP_SPRT bg_ds;
    float     shadow_off_x = -320.0f;                                   /* 1131 */
    u_char    shadow_alpha = 0;

    static const POS_ANIM_TBL shadow_x_tbl[3] =             /* rdata 3bc8b8 */
    {
        { -320.0f, -320.0f,  0,  4,  1 },
        { -320.0f,    0.0f,  4, 16,  1 },
        {   -1.0f,   -1.0f, -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL shadow_alpha_tbl[3] =       /* rdata 3bc8e8 */
    {
        {  0,   0,  0,  4 },
        {  0, 128,  4, 16 },
        { -1,  -1, -1, -1 },
    };

    ALPHA_ANIM_TBL in_to_out_alpha_tbl[2] =                             /* 1145 */
    {
        {  0,  0,  0,  5 },
        { -1, -1, -1, -1 },
    };

    (void)off_x;
    (void)off_y;
    (void)alpha;

    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {                    /* 1151 */
        shadow_off_x = Anim2D_CalcNowPos(shadow_x_tbl,
                                         menu_disp.bg_anim_timer);      /* 1153 */
        shadow_alpha = Anim2D_CalcNowAlpha(shadow_alpha_tbl,
                                           menu_disp.bg_anim_timer);    /* 1155 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {           /* 1157 */
        shadow_off_x = 0.0f;                                            /* 1158 */
        shadow_alpha = 0x80;                                            /* 1159 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {              /* 1161 */
        if (menu_disp.bganim_in_to_out != 0) {                          /* 1162 */
            shadow_off_x = Anim2D_CalcNowPos(shadow_x_tbl,
                                             menu_disp.bg_anim_timer);  /* 1164 */
            in_to_out_alpha_tbl[0].end_alpha =
                Anim2D_CalcNowAlpha(shadow_alpha_tbl,
                                    menu_disp.bg_anim_timer);           /* 1166 */

            shadow_alpha = Anim2D_CalcNowAlpha(in_to_out_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1170 */
        }
        else {
            shadow_off_x = Anim2D_CalcNowPos(shadow_x_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1174 */
            shadow_alpha = Anim2D_CalcNowAlpha(shadow_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1176 */
        }
    }

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1181 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_SHADOW_L]);                      /* 1183 */

    bg_ds.x = bg_ds.x + shadow_off_x + (float)(bg_ds.h * 2);            /* 1184 */

    bg_ds.crx = bg_ds.x;                                                /* 1185 */
    bg_ds.cry = bg_ds.y;
    bg_ds.rot = 90.0f;

    bg_ds.csx = bg_ds.x;                                                /* 1186 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = shadow_alpha;                                         /* 1187 */
    bg_ds.alphar = MENU_ALPHA_DARKEN;                                   /* 1188 */

    DispSprD(&bg_ds);                                                   /* 1189 */
}

/* The right screen's edge shading -- rotated 270 instead, and stepped down in
 * y by two of its widths rather than along x. */
static void MenuBg_RightShadowDisp(int off_x, int off_y, u_char alpha)   /* 1202 */
{
    DISP_SPRT bg_ds;
    float     shadow_off_x = 320.0f;                                    /* 1205 */
    u_char    shadow_alpha = 0;

    static const POS_ANIM_TBL shadow_x_tbl[3] =             /* rdata 3bc910 */
    {
        { 320.0f, 320.0f,  0,  4,  1 },
        { 320.0f,   0.0f,  4, 16,  1 },
        {  -1.0f,  -1.0f, -1, -1, -1 },
    };

    static const ALPHA_ANIM_TBL shadow_alpha_tbl[3] =       /* rdata 3bc940 */
    {
        {  0,   0,  0,  4 },
        {  0, 128,  4, 16 },
        { -1,  -1, -1, -1 },
    };

    ALPHA_ANIM_TBL in_to_out_alpha_tbl[2] =                             /* 1219 */
    {
        {  0,  0,  0,  5 },
        { -1, -1, -1, -1 },
    };

    (void)off_x;
    (void)off_y;
    (void)alpha;

    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {                    /* 1225 */
        shadow_off_x = Anim2D_CalcNowPos(shadow_x_tbl,
                                         menu_disp.bg_anim_timer);      /* 1227 */
        shadow_alpha = Anim2D_CalcNowAlpha(shadow_alpha_tbl,
                                           menu_disp.bg_anim_timer);    /* 1229 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {           /* 1231 */
        shadow_off_x = 0.0f;                                            /* 1232 */
        shadow_alpha = 0x80;                                            /* 1233 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {              /* 1235 */
        if (menu_disp.bganim_in_to_out != 0) {                          /* 1237 */
            shadow_off_x = Anim2D_CalcNowPos(shadow_x_tbl,
                                             menu_disp.bg_anim_timer);  /* 1239 */
            in_to_out_alpha_tbl[0].end_alpha =
                Anim2D_CalcNowAlpha(shadow_alpha_tbl,
                                    menu_disp.bg_anim_timer);           /* 1241 */

            shadow_alpha = Anim2D_CalcNowAlpha(in_to_out_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1245 */
        }
        else {
            shadow_off_x = Anim2D_CalcNowPos(shadow_x_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1249 */
            shadow_alpha = Anim2D_CalcNowAlpha(shadow_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1251 */
        }
    }

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1256 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_SHADOW_R]);                      /* 1258 */

    bg_ds.x = bg_ds.x + shadow_off_x;                                   /* 1259 */
    bg_ds.y = bg_ds.y + (float)(bg_ds.w * 2);

    bg_ds.crx = bg_ds.x;                                                /* 1260 */
    bg_ds.cry = bg_ds.y;
    bg_ds.rot = 270.0f;

    bg_ds.csx = bg_ds.x;                                                /* 1261 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = shadow_alpha;                                         /* 1262 */
    bg_ds.alphar = MENU_ALPHA_DARKEN;                                   /* 1263 */

    DispSprD(&bg_ds);                                                   /* 1264 */
}

/* The wall the screens sit against: two halves, both at 2x, drawn last so
 * they cover the shoji's outer edges.  It never slides -- only its alpha is
 * animated, and on a shorter 8/16-frame curve than everything else. */
static void MenuBg_WallDisp(int off_x, int off_y, u_char alpha)          /* 1278 */
{
    DISP_SPRT bg_ds;
    u_char    wall_alpha = 0;

    static const ALPHA_ANIM_TBL wall_alpha_tbl[3] =         /* rdata 3bc968 */
    {
        {   0, 128,  0,  8 },
        { 128, 128,  8, 16 },
        {  -1,  -1, -1, -1 },
    };

    ALPHA_ANIM_TBL in_to_out_alpha_tbl[2] =                             /* 1289 */
    {
        {  0,  0,  0,  5 },
        { -1, -1, -1, -1 },
    };

    (void)off_x;
    (void)off_y;
    (void)alpha;

    if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN) {                    /* 1295 */
        wall_alpha = Anim2D_CalcNowAlpha(wall_alpha_tbl,
                                         menu_disp.bg_anim_timer);      /* 1297 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_IN_END) {           /* 1299 */
        wall_alpha = 0x80;                                              /* 1300 */
    }
    else if (menu_disp.menu_bg_anim == MENU_BG_ANIM_OUT) {              /* 1302 */
        if (menu_disp.bganim_in_to_out != 0) {                          /* 1304 */
            in_to_out_alpha_tbl[0].end_alpha =
                Anim2D_CalcNowAlpha(wall_alpha_tbl,
                                    menu_disp.bg_anim_timer);           /* 1306 */

            wall_alpha = Anim2D_CalcNowAlpha(in_to_out_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1310 */
        }
        else {
            wall_alpha = Anim2D_CalcNowAlpha(wall_alpha_tbl,
                                            menu_disp.bg_anim_out_timer); /* 1314 */
        }
    }

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1319 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_WALL_L]);                        /* 1322 */

    bg_ds.csx = bg_ds.x;                                                /* 1323 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = wall_alpha;                                           /* 1324 */

    DispSprD(&bg_ds);                                                   /* 1325 */

    CopySprDToSpr(&bg_ds, &menu_top[MT_WALL_R]);                        /* 1328 */

    bg_ds.csx = bg_ds.x;                                                /* 1329 */
    bg_ds.csy = bg_ds.y;
    bg_ds.scw = 2.0f;
    bg_ds.sch = 2.0f;

    bg_ds.alpha = wall_alpha;                                           /* 1330 */

    DispSprD(&bg_ds);                                                   /* 1331 */
}

/* ==========================================================================
 *  Map view
 * ======================================================================== */

/* GID_STORY_MAP's init: the same menu, opened straight on the map page.
 * Everything else about it is ordinary -- ingame.c's one_Story_Map() runs
 * MenuMain() / MenuDispMain() exactly as one_Story_Menu() does.
 *
 * The body was stubbed in ingame/map/MapView.c by an earlier pass; ZERO2.MAP
 * puts it here, at the end of menu.o. */
void MapViewInit(void)                                                  /* 1346 */
{
    MenuIn();                                                           /* 1350 */

    menu_wrk.menu_step = MENU_STEP_MAP;                                 /* 1353 */

    map_view_flg = 1;                                                   /* 1355 */
}
