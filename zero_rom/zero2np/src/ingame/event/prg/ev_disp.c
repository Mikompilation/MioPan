// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_disp.c
//
// Event 2D overlays.  Three independent layers, each gated by its own
// disp_flg, all ticked from EvDispMain(): the full-screen event image
// (ev_disp2d), the chapter title card, and the item-name banner.
//
// The first two follow the same shape -- a `step` that walks load-request ->
// wait -> draw -> release, and an `anim_step` that runs the fade underneath
// it.  Each owns one TIM2 buffer taken from mem_util, and the release step is
// what hands it back, so a layer that is never ended holds onto its picture.
// They differ in where the timings come from: the event image fades over
// whatever the caller asked for, while the chapter card's are baked in
// (10 frames in, 90 held, 30 out).
//
// The item-name banner has no step at all: nothing is loaded, the text comes
// from the message system, and the layer ends by clearing its own disp_flg.
//
// PORT NOTE: every PRINT_ASSERT here sits on a path the ROM carries on from --
// PrintAssertReal() returns and the function keeps going, which is why e.g.
// EvDisp2DDataLoadReq() still issues its load after complaining.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_disp.h"

#include <stdio.h>                                  // printf (warning banners)
#include <string.h>                                 // memset

#include "../dat/ev_disp_dat.h"                     // ev_disp2d_dat / ev_chapter_dat / chapter_tim_file
#include "../../../common/mem_util.h"               // mem_utilGetMem / mem_utilFreeMem
#include "../../../common/utility2.h"               // PRINT_ASSERT / PRINT_WARNING
#include "../../../graphics/graph2d/draw_cmn.h"     // DrawCmnWindow / DrawCmnTwoLineWindow
#include "../../../graphics/graph2d/message.h"      // GetMsgDataAddr / GetMsgLineLength / PrintMsg_Arrange / GetMsgIDNumMax
#include "../../../graphics/graph2d/tim2.h"         // Tim2GetPictureHeader / MakeTim2Direct / MakeClutDirect
#include "../../../system/eeiop/cddat.h"            // GetFileSize
#include "../../../system/eeiop/fileload.h"         // FileLoadReqEE / FileLoadIsEnd2
#include "../../../system/os/system.h"              // GetLanguage

static EV_DISP2D_CTRL    ev_disp2d_ctrl;    /* bss 478fa8 */
static EV_CHAPTER_DISP   ev_chapter_disp;   /* sbss 3f4c20 */
static EV_ITEM_NAME_DISP ev_item_name_disp; /* bss 478fd8 */
static void             *ev_disp2d_addr;    /* sbss 3f4c28 */
static void             *chapter_load_addr; /* sbss 3f4c2c */

/* GS TEX0 the overlay sprites are drawn with.  The chapter card's *picture
 * header* gets a slightly different value from its sprite -- the ROM writes
 * 0x...9882... into the TIM2 header and 0x...9886... into the draw record. */
#define EV_DISP_TEX0        0x200598866932abc0ull
#define EV_CHAPTER_PKT_TEX0 0x200598826932abc0ull

/* VRAM the overlay TIM2s are uploaded to (texture base / CLUT base). */
#define EV_DISP_TBP 0x2bc0
#define EV_DISP_CBP 0x2cc4

/* Alpha the layers ramp to.  0x80 is fully opaque in the 2D pipeline. */
#define EV_DISP_ALPHA_MAX 0x80

/* Chapter card timings, in frames. */
#define EV_CHAPTER_FADE_IN_STEP 12  /* alpha added per frame while fading in  */
#define EV_CHAPTER_FADE_IN_END  10
#define EV_CHAPTER_HOLD_END     90
#define EV_CHAPTER_FADE_OUT     30  /* timer reload; alpha is timer * 4       */

static void EvDisp2DMain(void);
static void EvDisp2DDataLoadReq(void);
static int  EvDisp2DDataLoadWait(void);
static void EvDisp2DExe(void);
static void EvDisp2DDataMakePkt(void);
static void EvChapterDispMain(void);
static void EvChapterDataLoadReq(void);
static int  EvChapterDataLoadWait(void);
static void EvChapterDispExe(void);
static void EvChapterDataMakePkt(void);
static void EvItemNameDispMain(void);

void EvDispInit(void)
{                                                                       /* 136 */
    memset(&ev_disp2d_ctrl, 0, sizeof(ev_disp2d_ctrl));                 /* 140 */
    memset(&ev_chapter_disp, 0, sizeof(ev_chapter_disp));               /* 141 */
    memset(&ev_item_name_disp, 0, sizeof(ev_item_name_disp));           /* 142 */

    ev_disp2d_addr    = nullptr;                                        /* 144 */
    chapter_load_addr = nullptr;                                        /* 145 */
}

void EvDispMain(void)
{                                                                       /* 158 */
    if (ev_disp2d_ctrl.disp_flg != 0)                                   /* 161 */
    {
        EvDisp2DMain();                                                 /* 162 */
    }

    if (ev_chapter_disp.disp_flg != 0)                                  /* 165 */
    {
        EvChapterDispMain();                                            /* 166 */
    }

    if (ev_item_name_disp.disp_flg != 0)                                /* 169 */
    {
        EvItemNameDispMain();                                           /* 170 */
    }
}

/* ---------------------------------------------------------------------------
 *  Full-screen event image
 * ------------------------------------------------------------------------ */

static void EvDisp2DMain(void)
{                                                                       /* 183 */
    if (ev_disp2d_ctrl.step == 0)                                       /* 186 */
    {
        EvDisp2DDataLoadReq();                                          /* 187 */
        ev_disp2d_ctrl.step = 1;                                        /* 188 */
    }

    if (ev_disp2d_ctrl.step == 1 && EvDisp2DDataLoadWait() != 0)        /* 191, 192 */
    {
        ev_disp2d_ctrl.step = 2;                                        /* 193 */
    }

    if (ev_disp2d_ctrl.step == 2)                                       /* 197 */
    {
        EvDisp2DExe();                                                  /* 198 */
    }

    /* Step 3 is set by the fade-out, so the release lands on the frame after
     * the image has gone fully transparent. */
    if (ev_disp2d_ctrl.step == 3)                                       /* 201 */
    {
        EvDisp2DEndRelease();                                           /* 202 */
    }
}

static void EvDisp2DDataLoadReq(void)
{                                                                       /* 211 */
    if (ev_disp2d_addr == nullptr)                                      /* 213 */
    {
        ev_disp2d_addr = mem_utilGetMem(GetFileSize(ev_disp2d_ctrl.file_label));     /* 215 */
    }
    else
    {
        PRINT_ASSERT("Error!! EvDisp2DDataLoadReq()");                  /* 218 */
    }

    FileLoadReqEE(ev_disp2d_ctrl.file_label, ev_disp2d_addr, 3, nullptr, nullptr);   /* 223 */
}

static int EvDisp2DDataLoadWait(void)
{                                                                       /* 233 */
    return (FileLoadIsEnd2(ev_disp2d_ctrl.file_label, ev_disp2d_addr) != 0);         /* 236 */
}

int CheckEvDisp2DDataLoad(void)
{
    return (ev_disp2d_ctrl.step == 2);                                  /* 255 */
}

static void EvDisp2DExe(void)
{                                                                       /* 266 */
    DISP_SPRT ds;
    u_char    alpha = EV_DISP_ALPHA_MAX;

    EvDisp2DDataMakePkt();                                              /* 272 */
    memset(&ds, 0, sizeof(ds));                                         /* 274 */

    switch (ev_disp2d_ctrl.anim_step)                                   /* 277 */
    {
    case 0:
        ev_disp2d_ctrl.timer = 0;
        if (ev_disp2d_ctrl.fade_time < 1)                               /* 280 */
        {
            /* No fade asked for: jump straight to held, fully opaque. */
            ev_disp2d_ctrl.anim_step = 2;                               /* 281 */
            break;                                                      /* 282 */
        }
        ev_disp2d_ctrl.anim_step = 1;                                   /* 285 */
        /* fall through -- the first fade-in frame runs immediately */

    case 1:
        alpha = (u_char)((EV_DISP_ALPHA_MAX / ev_disp2d_ctrl.fade_time) /* 289 */
                         * ev_disp2d_ctrl.timer);
        ev_disp2d_ctrl.timer++;                                         /* 290 */
        if (ev_disp2d_ctrl.timer >= ev_disp2d_ctrl.fade_time)           /* 291 */
        {
            ev_disp2d_ctrl.anim_step = 2;                               /* 292 */
        }
        break;                                                          /* 294 */

    case 2:
        /* Held: alpha stays at its initial EV_DISP_ALPHA_MAX. */
        break;

    case 3:
        if (ev_disp2d_ctrl.fade_time < 1)                               /* 299 */
        {
            ev_disp2d_ctrl.step = ev_disp2d_ctrl.anim_step;             /* 300 */
            alpha = 0;                                                  /* 301 */
        }
        else
        {
            alpha = (u_char)((EV_DISP_ALPHA_MAX / ev_disp2d_ctrl.fade_time)  /* 304 */
                             * ev_disp2d_ctrl.timer);
            ev_disp2d_ctrl.timer--;                                     /* 305 */
            if (ev_disp2d_ctrl.timer < 1)                               /* 306 */
            {
                ev_disp2d_ctrl.step = ev_disp2d_ctrl.anim_step;         /* 307 */
            }
        }
        break;
    }

    if (ev_disp2d_ctrl.win_flg != 0)                                    /* 313 */
    {
        DrawCmnWindow(0xa0, 176.0f, 45.0f, 293.0f, 262.0f, alpha, 0x6c);    /* 315 */
    }

    CopySprDToSpr(&ds, &ev_disp2d_ctrl.sprt);                           /* 319 */
    ds.tex0  = EV_DISP_TEX0;                                            /* 320 */
    ds.alpha = alpha;                                                   /* 322 */
    DispSprD(&ds);                                                      /* 323 */
}

/* Point the loaded TIM2's picture header at our VRAM slot and queue the
 * texture and CLUT uploads.  Re-run every frame the image is drawn. */
static void EvDisp2DDataMakePkt(void)
{                                                                       /* 333 */
    TIM2_PICTUREHEADER *ph;

    if (ev_disp2d_addr == nullptr)                                      /* 339 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 340 */
    }

    ph = Tim2GetPictureHeader(ev_disp2d_addr, 0);                       /* 344 */
    ph->GsTex0 = EV_DISP_TEX0;                                          /* 347 */

    MakeTim2Direct((u_int *)ev_disp2d_addr, EV_DISP_TBP, 0);            /* 352 */
    MakeClutDirect((u_int *)ev_disp2d_addr, EV_DISP_CBP, 0);            /* 354 */
}

void EvDisp2DEndRelease(void)
{                                                                       /* 362 */
    if (ev_disp2d_addr != nullptr)                                      /* 366 */
    {
        mem_utilFreeMem(ev_disp2d_addr);                                /* 367 */
        ev_disp2d_addr = nullptr;                                       /* 368 */
    }

    memset(&ev_disp2d_ctrl, 0, sizeof(ev_disp2d_ctrl));                 /* 371 */
}

void EvDisp2DStartReq(int x, int y, int file_label, int fade_in_time, u_char win_flg, int base_label)
{                                                                       /* 387 */
    /* Only a warning, not an assert: the second request is dropped and the
     * image already up keeps playing. */
    if (ev_disp2d_ctrl.disp_flg == 1)                                   /* 390 */
    {
        PRINT_WARNING("Error!! EvDisp2DStartReq()");                    /* 391 */
        return;
    }

    ev_disp2d_ctrl.step       = 0;                                      /* 396 */
    ev_disp2d_ctrl.anim_step  = 0;                                      /* 397 */
    ev_disp2d_ctrl.disp_flg   = 1;                                      /* 398 */
    ev_disp2d_ctrl.win_flg    = win_flg;                                /* 399 */
    ev_disp2d_ctrl.timer      = 0;                                      /* 400 */
    ev_disp2d_ctrl.fade_time  = fade_in_time;                           /* 401 */
    ev_disp2d_ctrl.file_label = file_label;                             /* 402 */

    ev_disp2d_ctrl.sprt   = ev_disp2d_dat[base_label];                  /* 403 */
    ev_disp2d_ctrl.sprt.x = x;                                          /* 405 */
    ev_disp2d_ctrl.sprt.y = y;                                          /* 406 */
}

void EvDisp2DEndReq(int fade_out_time)
{
    if (fade_out_time < 1)                                              /* 417 */
    {
        EvDisp2DEndRelease();                                           /* 419 */
        return;
    }

    if (ev_disp2d_ctrl.disp_flg == 1)                                   /* 422 */
    {
        ev_disp2d_ctrl.anim_step = 3;                                   /* 423 */
        ev_disp2d_ctrl.fade_time = fade_out_time;                       /* 424 */
        ev_disp2d_ctrl.timer     = fade_out_time;                       /* 425 */
    }
    else
    {
        /* Ending something that was never started -- release anyway so a
         * half-set-up layer cannot hold its buffer. */
        PRINT_WARNING("Warning! %s", __FUNCTION__);                     /* 428 */
        EvDisp2DEndRelease();                                           /* 430 */
    }
}

/* ---------------------------------------------------------------------------
 *  Chapter title card
 * ------------------------------------------------------------------------ */

static void EvChapterDispMain(void)
{                                                                       /* 444 */
    if (ev_chapter_disp.step == 0)                                      /* 447 */
    {
        EvChapterDataLoadReq();                                         /* 448 */
        ev_chapter_disp.step = 1;                                       /* 449 */
    }

    if (ev_chapter_disp.step == 1 && EvChapterDataLoadWait() != 0)      /* 452, 453 */
    {
        ev_chapter_disp.step = 2;                                       /* 454 */
    }

    if (ev_chapter_disp.step == 2)                                      /* 458 */
    {
        EvChapterDispExe();                                             /* 459 */
    }

    if (ev_chapter_disp.step == 3)                                      /* 462 */
    {
        EvChapterDispEndRelease();                                      /* 463 */
    }
}

static void EvChapterDataLoadReq(void)
{                                                                       /* 472 */
    if (chapter_load_addr == nullptr)                                   /* 474 */
    {
        chapter_load_addr = mem_utilGetMem(                             /* 476 */
            GetFileSize(chapter_tim_file[ev_chapter_disp.chapter_num] + (char)GetLanguage()));
    }
    else
    {
        PRINT_ASSERT("Error!! EvChapterDataLoadReq()");                 /* 479 */
    }

    FileLoadReqEE(chapter_tim_file[ev_chapter_disp.chapter_num] + (char)GetLanguage(),   /* 484 */
                  chapter_load_addr, 3, nullptr, nullptr);
}

static int EvChapterDataLoadWait(void)
{
    return (FileLoadIsEnd2(chapter_tim_file[ev_chapter_disp.chapter_num] + (char)GetLanguage(),
                           chapter_load_addr) != 0);                    /* 497 */
}

static void EvChapterDispExe(void)
{                                                                       /* 507 */
    DISP_SPRT ds;
    u_char    alpha = EV_DISP_ALPHA_MAX;

    EvChapterDataMakePkt();                                             /* 513 */
    memset(&ds, 0, sizeof(ds));                                         /* 515 */

    switch (ev_chapter_disp.anim_step)                                  /* 518 */
    {
    case 0:
        ev_chapter_disp.timer     = 0;                                  /* 520 */
        ev_chapter_disp.anim_step = 1;                                  /* 521 */
        /* fall through */

    case 1:
        alpha = (u_char)(ev_chapter_disp.timer * EV_CHAPTER_FADE_IN_STEP);  /* 524 */
        ev_chapter_disp.timer++;                                        /* 525 */
        if (ev_chapter_disp.timer >= EV_CHAPTER_FADE_IN_END)            /* 526 */
        {
            ev_chapter_disp.timer     = 0;                              /* 527 */
            ev_chapter_disp.anim_step = 2;                              /* 528 */
        }
        break;                                                          /* 530 */

    case 2:
        /* Held fully opaque, then hand 30 frames to the fade-out. */
        ev_chapter_disp.timer++;                                        /* 532 */
        if (ev_chapter_disp.timer >= EV_CHAPTER_HOLD_END)               /* 534 */
        {
            ev_chapter_disp.timer     = EV_CHAPTER_FADE_OUT;            /* 535 */
            ev_chapter_disp.anim_step = 3;                              /* 536 */
        }
        break;                                                          /* 538 */

    case 3:
        alpha = (u_char)(ev_chapter_disp.timer * 4);                    /* 540 */
        ev_chapter_disp.timer--;                                        /* 541 */
        if (ev_chapter_disp.timer < 1)                                  /* 542 */
        {
            ev_chapter_disp.step = ev_chapter_disp.anim_step;           /* 543 */
        }
        break;
    }

    /* Left half, then right half of the same texture.  ds is not re-cleared
     * between them, and only the second sets test -- the ROM's order. */
    CopySprDToSpr(&ds, &ev_chapter_dat[0]);                             /* 549 */
    ds.tex0  = EV_DISP_TEX0;                                            /* 550 */
    ds.alpha = alpha;
    DispSprD(&ds);                                                      /* 553 */

    CopySprDToSpr(&ds, &ev_chapter_dat[1]);                             /* 554 */
    ds.tex0  = EV_DISP_TEX0;                                            /* 555 */
    ds.alpha = alpha;                                                   /* 557 */
    ds.test  = 0x30003;                                                 /* 559 */
    DispSprD(&ds);                                                      /* 560 */
}

static void EvChapterDataMakePkt(void)
{                                                                       /* 568 */
    TIM2_PICTUREHEADER *ph;

    if (chapter_load_addr == nullptr)                                   /* 573 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 574 */
    }

    ph = Tim2GetPictureHeader(chapter_load_addr, 0);                    /* 578 */
    ph->GsTex0 = EV_CHAPTER_PKT_TEX0;                                   /* 581 */

    MakeTim2Direct((u_int *)chapter_load_addr, EV_DISP_TBP, 0);         /* 586 */
    MakeClutDirect((u_int *)chapter_load_addr, EV_DISP_CBP, 0);         /* 588 */
}

void EvChapterDispEndRelease(void)
{                                                                       /* 596 */
    if (chapter_load_addr != nullptr)                                   /* 600 */
    {
        mem_utilFreeMem(chapter_load_addr);                             /* 601 */
        chapter_load_addr = nullptr;                                    /* 602 */
    }

    memset(&ev_chapter_disp, 0, sizeof(ev_chapter_disp));               /* 604 */
}

void EvChapterDispStartReq(u_char chapter_num)
{                                                                       /* 614 */
    if (chapter_num >= EV_CHAPTER_MAX)                                  /* 617 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 618 */
    }

    if (ev_chapter_disp.disp_flg == 1)                                  /* 622 */
    {
        PRINT_ASSERT("ERROR!! EvChapterDispStartReq()\n");              /* 623 */
    }

    ev_chapter_disp.step        = 0;                                    /* 627 */
    ev_chapter_disp.anim_step   = 0;                                    /* 628 */
    ev_chapter_disp.chapter_num = chapter_num;                          /* 629 */
    ev_chapter_disp.disp_flg    = 1;                                    /* 630 */
    ev_chapter_disp.timer       = 0;                                    /* 631 */
}

int EvChapterIsDisp(void)
{
    return ev_chapter_disp.disp_flg;                                    /* 641 */
}

/* ---------------------------------------------------------------------------
 *  Item-name banner
 * ------------------------------------------------------------------------ */

static void EvItemNameDispMain(void)
{                                                                       /* 651 */
    u_char alpha = 0;
    int    msg_length;

    /* The window is sized to the text, so the length is measured every frame
     * rather than cached at request time. */
    msg_length = GetMsgLineLength(GetMsgDataAddr(ev_item_name_disp.msg_type,        /* 659 */
                                                 ev_item_name_disp.msg_id), nullptr);

    switch (ev_item_name_disp.anim_step)                                /* 661 */
    {
    case 0:
        ev_item_name_disp.anim_step = 1;
        ev_item_name_disp.timer     = 0;                                /* 663 */
        /* fall through */

    case 1:
        alpha = (u_char)((EV_DISP_ALPHA_MAX / ev_item_name_disp.fade_time)  /* 668 */
                         * ev_item_name_disp.timer);
        ev_item_name_disp.timer++;                                      /* 670 */
        if (ev_item_name_disp.timer >= ev_item_name_disp.fade_time)     /* 672 */
        {
            ev_item_name_disp.anim_step = 2;                            /* 673 */
        }
        break;                                                          /* 675 */

    case 2:
        alpha = EV_DISP_ALPHA_MAX;                                      /* 678 */
        break;

    case 3:
        alpha = (u_char)(EV_DISP_ALPHA_MAX                              /* 681 */
                         - (EV_DISP_ALPHA_MAX / ev_item_name_disp.fade_time)
                           * ev_item_name_disp.timer);
        ev_item_name_disp.timer++;                                      /* 683 */
        if (ev_item_name_disp.timer >= ev_item_name_disp.fade_time)     /* 684 */
        {
            /* Nothing to release, so clearing the flag is the whole teardown. */
            ev_item_name_disp.disp_flg = 0;                             /* 687 */
            ev_item_name_disp.timer    = 0;                             /* 689 */
        }
        break;

    default:
        PRINT_ASSERT("Error! EvItemNameDispMain");                      /* 691 */
        break;
    }

    DrawCmnTwoLineWindow(0, (float)(300 - msg_length / 2), 289.0f,      /* 696 */
                         (float)(msg_length + 40), 60.0f, alpha, EV_DISP_ALPHA_MAX);
    PrintMsg_Arrange(ev_item_name_disp.msg_type, ev_item_name_disp.msg_id,  /* 700 */
                     320, 304, 1, alpha, 0, 0, 0, 2);
}

void EvItemNameDispStartReq(int msg_type, int msg_id, int fade_in_time)
{                                                                       /* 711 */
    if ((u_int)msg_type > 0x52)                                         /* 715 */
    {
        PRINT_ASSERT("Error! %s msg_type %d", __FUNCTION__, msg_type);  /* 716 */
    }

    if (msg_id < 0 || GetMsgIDNumMax(msg_type) <= msg_id)               /* 718 */
    {
        PRINT_ASSERT("Error! %s msg_id %d", __FUNCTION__, msg_id);      /* 719 */
    }

    /* Unlike the event image this only warns and then overwrites, so a second
     * request replaces the banner already up. */
    if (ev_item_name_disp.disp_flg == 1)                                /* 723 */
    {
        PRINT_WARNING("ERROR!! EvItemNameDispStartReq");                /* 724 */
    }

    ev_item_name_disp.anim_step = 0;                                    /* 728 */
    ev_item_name_disp.msg_type  = msg_type;                             /* 729 */
    ev_item_name_disp.msg_id    = msg_id;                               /* 730 */
    ev_item_name_disp.fade_time = fade_in_time;                         /* 731 */
    ev_item_name_disp.disp_flg  = 1;                                    /* 732 */
    ev_item_name_disp.timer     = 0;                                    /* 733 */
}

void EvItemNameDispEndReq(int fade_out_time)
{
    if (fade_out_time < 1)                                              /* 745 */
    {
        ev_item_name_disp.disp_flg = 0;                                 /* 747 */
        return;
    }

    ev_item_name_disp.anim_step = 3;                                    /* 750 */
    ev_item_name_disp.fade_time = fade_out_time;                        /* 751 */
    ev_item_name_disp.timer     = 0;                                    /* 752 */
}
