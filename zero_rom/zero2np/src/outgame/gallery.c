// FILE: /home/zero_rom/zero2np/src/outgame/gallery.c
//
// The gallery: an eight-row menu whose rows unlock as the game is cleared,
// leading either to a picture viewer (three sets) or to one of the ending
// movies.  gallery_disp.c draws all of it; this file is the state machine and
// the loads.
//
// Two dispatch tables drive it -- GalleryCtrlModule[] and GalleryDispModule[]
// indexed by GAL_CTRL::now_place.  Both have a fourth, null slot: place 3 is
// the exit, and the fade-out animation is what actually leaves, so the null is
// never called.
//
// Textures are pushed to VRAM through GalPK2SendVram(), which remembers what
// is already there in GAL_CTRL::now_tex -- the top page alone would otherwise
// re-upload three paks per frame.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "gallery.h"

#include "title.h"                          // Get/SetTitleStreamID / SetTitleBgSendLock
#include "../common/ol_load.h"              // ol_loadGetHeap / ol_loadFreeHeap
#include "../common/variable.h"             // pad[]
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../graphics/movie/movie.h"        // PlayMovieWithTitle / EndMovieWithTitle
#include "../ingame/clear/prg/clear_flg.h"  // clear_flg_ctrl
#include "../main/gphase.h"                 // SetNextGPhase / GID_TITLE_MENU
#include "../system/eeiop/cddat.h"          // OUTGAME_PK2 / GetFileSize
#include "../system/eeiop/fileload.h"       // FileLoadReqEE / IsEnd2 / Cancel2
#include "../system/eeiop/stream_auto.h"    // StreamAutoPlay / FadeOut
#include "../system/os/system.h"            // GetLanguage / SystemBankPlay
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t
#include <stdio.h>                          // printf

/* Per-language gallery paks. */
#define GAL_CMN_PK2         0x112a          /* + language */
#define GAL_TOP_PK2         0x112f          /* + language */
#define GAL_VIEW_PK2        0x1134

/* First cd label of each picture set. */
#define GAL_PIC_SET0_TOP    0x1153
#define GAL_PIC_SET1_TOP    0x1149
#define GAL_PIC_SET2_TOP    0x1135

static void GalleryTopPad(void);
static void GalleryViewPad(void);
static void GalleryMoviePad(void);
static void GetGalleryTexMem(void **tex_addr, int data_label);
static void GalleryTexLoadReq(void *tex_addr, int data_label);
static int  GalleryTexLoadWait(void);
static void LiberateGalleryTexMem(void **tex_addr);
static void GalleryTexLoadCancel(void *tex_addr, int data_label);

static void (*GalleryCtrlModule[3])(void) =                             /* data 315670 */
{
    GalleryTopPad,
    GalleryViewPad,
    GalleryMoviePad,
};

static void (*GalleryDispModule[3])(void) =                             /* data 315680 */
{
    GalleryDispTop,
    GalleryDispView,
    GalleryDispMovie,
};

GAL_CTRL  gal_ctrl;                                                     /* data  315690 */
GAL_CTRL *gc;                                                           /* sdata 3f0df0 */

void *gal_og_tex_addr;                                                  /* sdata 3f0dd0 */
void *gal_cmn_tex_addr;                                                 /* sdata 3f0dd4 */
void *gal_top_tex_addr;                                                 /* sdata 3f0dd8 */
void *gal_view_tex_addr;                                                /* sdata 3f0ddc */
void *gal_pic_tex_addr;                                                 /* sdata 3f0de0 */

/* Rows 3, 4 and 5 are always available; the other five are unlocked by the
 * clear flags.  The cursor starts on row 0 if the game has been cleared and on
 * row 3 (the first always-available row) if it has not. */
void GalleryInit(void)
{
    int i;

    gc = &gal_ctrl;

    gal_ctrl.main_step = 0;                                             /* 116 */
    gal_ctrl.now_place = GAL_PLACE_TOP;                                 /* 117 */
    gal_ctrl.next_place = GAL_PLACE_TOP;                                /* 118 */
    gal_ctrl.now_tex = GAL_TEX_NONE;                                    /* 119 */
    gal_ctrl.anm_step = GAL_ANM_FADE_IN;                                /* 120 */
    gal_ctrl.anm_alpha = 0;                                             /* 121 */
    gal_ctrl.cursor = 0;                                                /* 122 */
    gal_ctrl.old_csr = 0;                                               /* 123 */
    gal_ctrl.next_csr = 0;                                              /* 124 */
    gal_ctrl.pic_mode = 0;                                              /* 125 */
    gal_ctrl.pic_step = GAL_PIC_LOAD_REQ;                               /* 126 */
    gal_ctrl.pic_anm_alpha = 0;                                         /* 127 */
    gal_ctrl.pic_no = 0;                                                /* 128 */
    gal_ctrl.next_pic_no = 0;                                           /* 129 */
    gal_ctrl.pic_max = 0;                                               /* 130 */
    gal_ctrl.movie_no = 0;                                              /* 131 */
    gal_ctrl.end1_mov_cnt = 0;                                          /* 132 */

    for (i = 0; i < GAL_CSR_NUM; i++)                                   /* 134 */
    {
        gc->csr_map[i] = 0;                                             /* 135 */
    }

    gc->csr_map[3] = 1;                                                 /* 137 */
    gc->csr_map[4] = 1;                                                 /* 138 */
    gc->csr_map[5] = 1;                                                 /* 139 */

    if (clear_flg_ctrl.clear_flg == '\x01')                             /* 141 */
    {
        gc->csr_map[0] = 1;                                             /* 142 */
        gc->csr_map[1] = 1;                                             /* 144 */
        gc->game_clear_flg = 1;                                         /* 145 */
        gc->cursor = 0;                                                 /* 147 */
    }
    else
    {
        gc->game_clear_flg = 0;                                         /* 148 */
        gc->cursor = 3;                                                 /* 153 */
    }

    if (clear_flg_ctrl.ending_movie_flg.IsUp(0) != 0)                   /* 155 */
    {
        gc->csr_map[6] = 1;                                             /* 159 */
        gc->ending1_mov_flg = 1;                                        /* 160 */
    }
    else
    {
        gc->ending1_mov_flg = 0;                                        /* 162 */
    }

    if (clear_flg_ctrl.ending_movie_flg.IsUp(1) != 0)                   /* 165 */
    {
        gc->csr_map[7] = 1;                                             /* 166 */
        gc->ending2_mov_flg = 1;                                        /* 167 */
    }
    else
    {
        gc->ending2_mov_flg = 0;                                        /* 169 */
    }

    if (clear_flg_ctrl.comp_soul_list_flg != '\0')
    {
        gc->csr_map[2] = 1;
        gc->setup_pic_flg = 1;
    }
    else
    {
        gc->setup_pic_flg = 0;
    }

    gc->next_csr = gc->cursor;                                          /* 186 */
    gc->old_csr = gc->cursor;                                           /* 187 */
}

/* The picture manager only runs on the viewer page, and nothing runs at all
 * while a place transition is animating. */
void GalleryMain(void)
{
    int end_flg = 0;                                                    /* 197 */

    if (gc->main_step == 0)                                             /* 200 */
    {
        if (GalleryTexLoadWait() != 0)                                  /* 202 */
        {
            gc->main_step = 1;                                          /* 203 */
        }
    }
    else if (gc->main_step == 1)                                        /* 205 */
    {
        if (gc->anm_step == 0)                                          /* 208 */
        {
            (*GalleryCtrlModule[gc->now_place])();                      /* 209 */

            if (gc->now_place == GAL_PLACE_VIEW)                        /* 210 */
            {
                GalPictureManage();                                     /* 211 */
            }
        }
    }

    if (gc->main_step != 0)                                             /* 215 */
    {
        end_flg = GalAnimation();                                       /* 218 */

        gc->now_tex = GAL_TEX_NONE;                                     /* 219 */
        (*GalleryDispModule[gc->now_place])();                          /* 220 */
    }

    if (end_flg != 0)                                                   /* 224 */
    {
        SetNextGPhase(GID_TITLE_MENU);                                  /* 225 */
    }
}

void GalleryEnd(void)
{
    EndMovieWithTitle();                                                /* 243 */
}

/* UP / DOWN walk the unlocked rows only -- the search loop skips any row whose
 * csr_map entry is 0, and rows 3..5 guarantee it terminates.
 *
 * Rows 0..2 are picture sets, 3..7 are movies.  The ROM reads the movie number
 * out of mov_no[cursor - 3]; GCC merged the three stack arrays, so the
 * decompiler renders that as pic_mode[cursor + 1]. */
static void GalleryTopPad(void)
{
    int pic_num[3] = { 3, 10, 20 };                                     /* 261 */
    int pic_mode[3] = { 0, 1, 2 };                                      /* 264 */
    int mov_no[5] = { 66, 67, 68, 51, 52 };                             /* 267 */
    int ncsr;
    int end_flg = 0;

    if (*paddat[1] == 1)                                                /* 274 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 275 */
        gc->next_place = GAL_PLACE_END;                                 /* 276 */
        gc->anm_step = GAL_ANM_FADE_OUT;                                /* 277 */
    }
    else if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))/* 278 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 289 */

        ncsr = gc->cursor - 1;                                          /* 290 */
        do
        {
            if (ncsr < 0)                                               /* 292 */
            {
                ncsr = GAL_CSR_NUM - 1;                                 /* 293 */
            }
            if (gc->csr_map[ncsr] == 0)                                 /* 294 */
            {
                ncsr--;
            }
            else
            {
                end_flg = 1;
            }
        } while (end_flg == 0);

        gc->cursor = ncsr;                                              /* 296 */
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))/* 299 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 302 */

        ncsr = gc->cursor + 1;
        do
        {
            if (ncsr > GAL_CSR_NUM - 1)
            {
                ncsr = 0;
            }
            if (gc->csr_map[ncsr] == 0)
            {
                ncsr++;
            }
            else
            {
                end_flg = 1;
            }
        } while (end_flg == 0);

        gc->cursor = ncsr;
    }
    else if (*paddat[0] == 1)                                           /* 317 */
    {
        gc->old_csr = gc->cursor;                                       /* 318 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 319 */

        if (gc->cursor < 3)                                             /* 320 */
        {
            gc->next_place = GAL_PLACE_VIEW;                            /* 321 */
            gc->anm_step = GAL_ANM_FADE_OUT;                            /* 322 */
            gc->pic_mode = pic_mode[gc->cursor];                        /* 324 */
            gc->pic_step = GAL_PIC_LOAD_REQ;                            /* 327 */
            gc->pic_max = pic_num[gc->cursor];                          /* 330 */
            gc->pic_no = 0;                                             /* 331 */
        }
        else
        {
            StreamAutoFadeOut(GetTitleStreamID(), 30);                  /* 332 */

            gc->anm_step = GAL_ANM_FADE_OUT;                            /* 333 */
            gc->next_place = GAL_PLACE_MOVIE;                           /* 334 */
            gc->movie_no = mov_no[gc->cursor - 3];                      /* 335 */
        }
    }
}

/* LEFT / RIGHT page through the set; the fade-out step is what triggers the
 * next load, so a held direction cannot outrun the disc. */
static void GalleryViewPad(void)
{
    if (gc->pic_step > GAL_PIC_LOAD_WAIT)                               /* 358 */
    {
        if (*paddat[1] == 1)                                            /* 363 */
        {
            SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 364 */
            gc->anm_step = GAL_ANM_FADE_OUT;                            /* 365 */
            gc->next_place = GAL_PLACE_TOP;                             /* 366 */
        }
        else if ((pad[0].one & 0x2000U) != 0)                           /* 367 */
        {
            gc->next_pic_no = gc->pic_no + 1;                           /* 369 */
            if (gc->next_pic_no >= gc->pic_max)                         /* 370 */
            {
                gc->next_pic_no = 0;                                    /* 371 */
            }
            gc->pic_step = GAL_PIC_FADE_OUT;                            /* 373 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 374 */
        }
        else if ((pad[0].one & 0x8000U) != 0)                           /* 377 */
        {
            gc->next_pic_no = gc->pic_no - 1;                           /* 378 */
            if (gc->next_pic_no < 0)                                    /* 379 */
            {
                gc->next_pic_no = gc->pic_max - 1;                      /* 381 */
            }
            gc->pic_step = GAL_PIC_FADE_OUT;                            /* 382 */
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);/* 384 */
        }
    }
}

/* The picture cross-fade.  Step 4 is the only one that frees the texture, and
 * it does the free *and* clears the pointer twice -- LiberateGalleryTexMem()
 * already nulls it.  Reproduced as found. */
void GalPictureManage(void)
{
    switch (gc->pic_step)                                               /* 393 */
    {
    case GAL_PIC_LOAD_REQ:
        gc->file_no = GalPictureLoadReq(gc->pic_mode, gc->pic_no);      /* 395 */
        gc->pic_step = GAL_PIC_LOAD_WAIT;                               /* 397 */
        gc->pic_anm_alpha = 0;                                          /* 398 */
        break;                                                          /* 400 */

    case GAL_PIC_LOAD_WAIT:
        if (GalPictureLoadWait(gc->file_no) != 0)                       /* 402 */
        {
            gc->pic_step = GAL_PIC_FADE_IN;                             /* 404 */
            gc->pic_anm_alpha = 0;                                      /* 406 */
        }
        break;                                                          /* 407 */

    case GAL_PIC_SHOWN:
        break;                                                          /* 408 */

    case GAL_PIC_FADE_IN:
        gc->pic_anm_alpha += 12;                                        /* 409 */
        if (gc->pic_anm_alpha > 0x7f)                                   /* 411 */
        {
            gc->pic_step = GAL_PIC_SHOWN;                               /* 413 */
            gc->pic_anm_alpha = 0x80;                                   /* 414 */
        }
        break;                                                          /* 415 */

    case GAL_PIC_FADE_OUT:
        gc->pic_anm_alpha -= 12;                                        /* 416 */
        if (gc->pic_anm_alpha < 1)                                      /* 417 */
        {
            gc->pic_no = gc->next_pic_no;                               /* 418 */
            gc->pic_anm_alpha = 0;                                      /* 419 */
            gc->pic_step = GAL_PIC_LOAD_REQ;                            /* 420 */

            GalleryTexLoadCancel(gal_pic_tex_addr, gc->file_no);
            LiberateGalleryTexMem(&gal_pic_tex_addr);
            gal_pic_tex_addr = (void *)0;
        }
        break;

    default:
        break;                                                          /* 424 */
    }
}

/* Ending 1 is two movies back to back: 0x42 then 0x35.  end1_mov_cnt is what
 * distinguishes the first pass from the second. */
static void GalleryMoviePad(void)
{
    int end_flg;

    if (PlayMovieWithTitle() != 0)                                      /* 438 */
    {
        EndMovieWithTitle();                                            /* 440 */

        end_flg = 1;                                                    /* 441 */

        if ((gc->cursor == 6) && (gc->end1_mov_cnt == 0))               /* 442 */
        {
            gc->anm_step = GAL_ANM_FADE_OUT;                            /* 443 */
            gc->movie_no = 0x35;                                        /* 444 */
            gc->next_place = GAL_PLACE_MOVIE;                           /* 445 */
            gc->end1_mov_cnt = 1;                                       /* 446 */
            end_flg = 0;                                                /* 449 */
        }

        if (end_flg != 0)                                               /* 450 */
        {
            SetTitleBgSendLock('\0');                                   /* 453 */
            SetTitleStreamID(StreamAutoPlay(0xa13, 0xa12, 0xc, 0, 1,
                                            0x3200, 0,
                                            (SND_3D_SET *)0));          /* 454 */

            gc->anm_step = GAL_ANM_FADE_IN;                             /* 455 */
            gc->end1_mov_cnt = 0;                                       /* 456 */
            gc->next_place = GAL_PLACE_TOP;                             /* 457 */
            gc->now_place = GAL_PLACE_TOP;
            gc->anm_alpha = 0;                                          /* 458 */
        }
    }
}

/* A null texture is a warning, not an assert -- a picture that failed to load
 * simply does not draw. */
void GalPK2SendVram(int tex_id, void *tex_addr)
{
    if (tex_addr == (void *)0)                                          /* 474 */
    {
        printf("Warning NULL tex pointer!!\n");                         /* 477 */
        return;
    }

    if (tex_id != gc->now_tex)                                          /* 480 */
    {
        PK2SendVram((uintptr_t)tex_addr, -1, -1, 0);                    /* 481 */
        gc->now_tex = tex_id;                                           /* 483 */
    }
}

int GalPictureLoadReq(int id, int no)
{
    int pic_top[3] = { GAL_PIC_SET0_TOP, GAL_PIC_SET1_TOP,
                       GAL_PIC_SET2_TOP };                              /* 507 */
    int file_no;

    file_no = pic_top[id] + no;                                         /* 512 */

    if (gal_pic_tex_addr != (void *)0)                                  /* 513 */
    {
        LiberateGalleryTexMem(&gal_pic_tex_addr);                       /* 514 */
    }

    GetGalleryTexMem(&gal_pic_tex_addr, file_no);                       /* 515 */
    GalleryTexLoadReq(gal_pic_tex_addr, file_no);                       /* 516 */

    return file_no;
}

int GalPictureLoadWait(int file_no)
{
    return (FileLoadIsEnd2(file_no, gal_pic_tex_addr) != 0);            /* 527 */
}

void GalPictureMemFree(void)
{
    GalleryTexLoadCancel(gal_pic_tex_addr, gc->file_no);                /* 540 */
    LiberateGalleryTexMem(&gal_pic_tex_addr);                           /* 541 */
}

/* Claimed while the title mode is loading, so the gallery has no wait of its
 * own beyond GalleryTexLoadWait(). */
void GalleryBackGroundLoadReq(void)
{
    if (gal_og_tex_addr != (void *)0)                                   /* 553 */
    {
        LiberateGalleryTexMem(&gal_og_tex_addr);
    }
    if (gal_top_tex_addr != (void *)0)                                  /* 554 */
    {
        LiberateGalleryTexMem(&gal_top_tex_addr);
    }
    if (gal_cmn_tex_addr != (void *)0)                                  /* 555 */
    {
        LiberateGalleryTexMem(&gal_cmn_tex_addr);
    }
    if (gal_view_tex_addr != (void *)0)                                 /* 556 */
    {
        LiberateGalleryTexMem(&gal_view_tex_addr);
    }

    GetGalleryTexMem(&gal_og_tex_addr, OUTGAME_PK2);                    /* 559 */
    GetGalleryTexMem(&gal_top_tex_addr, GAL_TOP_PK2 + GetLanguage());   /* 560 */
    GetGalleryTexMem(&gal_cmn_tex_addr, GAL_CMN_PK2 + GetLanguage());   /* 561 */
    GetGalleryTexMem(&gal_view_tex_addr, GAL_VIEW_PK2);                 /* 562 */

    GalleryTexLoadReq(gal_og_tex_addr, OUTGAME_PK2);                    /* 564 */
    GalleryTexLoadReq(gal_top_tex_addr, GAL_TOP_PK2 + GetLanguage());   /* 565 */
    GalleryTexLoadReq(gal_cmn_tex_addr, GAL_CMN_PK2 + GetLanguage());   /* 566 */
    GalleryTexLoadReq(gal_view_tex_addr, GAL_VIEW_PK2);                 /* 567 */
}

static void GetGalleryTexMem(void **tex_addr, int data_label)
{
    if (*tex_addr != (void *)0)                                         /* 577 */
    {
        LiberateGalleryTexMem(tex_addr);
    }

    *tex_addr = ol_loadGetHeap(GetFileSize(data_label));                /* 578 */
}

/* No null check, exactly like TitleTexLoadReq(). */
static void GalleryTexLoadReq(void *tex_addr, int data_label)
{
    FileLoadReqEE(data_label, tex_addr, 5, (FILE_LOAD_CALLBACK)0, (void *)0); /* 589 */
}

static int GalleryTexLoadWait(void)
{
    int res = 0;                                                        /* 597 */

    if ((FileLoadIsEnd2(OUTGAME_PK2, gal_og_tex_addr) != 0) &&
        (FileLoadIsEnd2(GAL_TOP_PK2 + GetLanguage(),
                        gal_top_tex_addr) != 0) &&
        (FileLoadIsEnd2(GAL_CMN_PK2 + GetLanguage(),
                        gal_cmn_tex_addr) != 0) &&
        (FileLoadIsEnd2(GAL_VIEW_PK2, gal_view_tex_addr) != 0))         /* 601 */
    {
        res = 1;
    }

    return res;                                                         /* 608 */
}

/* The picture texture is freed here but never cancelled -- it is the one load
 * that may still be in flight, and GalPictureMemFree() is the path that
 * cancels it. */
void GalleryMemFree(void)
{
    GalleryTexLoadCancel(gal_og_tex_addr, OUTGAME_PK2);                 /* 620 */
    GalleryTexLoadCancel(gal_top_tex_addr, GAL_TOP_PK2 + GetLanguage());/* 621 */
    GalleryTexLoadCancel(gal_cmn_tex_addr, GAL_CMN_PK2 + GetLanguage());/* 622 */
    GalleryTexLoadCancel(gal_view_tex_addr, GAL_VIEW_PK2);              /* 623 */

    LiberateGalleryTexMem(&gal_og_tex_addr);                            /* 626 */
    LiberateGalleryTexMem(&gal_top_tex_addr);                           /* 627 */
    LiberateGalleryTexMem(&gal_cmn_tex_addr);                           /* 628 */
    LiberateGalleryTexMem(&gal_view_tex_addr);                          /* 629 */
    LiberateGalleryTexMem(&gal_pic_tex_addr);                           /* 630 */
}

static void LiberateGalleryTexMem(void **tex_addr)
{
    if (*tex_addr != (void *)0)                                         /* 640 */
    {
        ol_loadFreeHeap(*tex_addr);                                     /* 641 */
        *tex_addr = (void *)0;                                          /* 642 */
    }
}

static void GalleryTexLoadCancel(void *tex_addr, int data_label)
{
    if ((tex_addr != (void *)0) &&
        (FileLoadIsEnd2(data_label, tex_addr) == 0))                    /* 654 */
    {
        FileLoadCancel2(data_label, tex_addr,
                        (FILE_LOAD_CALLBACK)0, (void *)0);              /* 655 */
    }
}
