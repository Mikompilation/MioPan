// FILE: /home/zero_rom/zero2np/src/outgame/gallery_disp.c
//
// Everything the gallery draws, plus the fade that carries it between pages.
//
// Two conventions run through the file:
//   * a lit menu row is the unlit one plus 0xd, so the whole row set is one
//     table with a constant offset rather than two;
//   * locked rows swap their sprite for a greyed replacement out of
//     0x4c..0x51 before the loop runs, which is why GalDispMenuCsr() edits its
//     own local index arrays.
//
// Several plates are drawn rotated 270 degrees about their own position and a
// few are scaled on one axis only -- the art is stored as one horizontal strip
// and stretched into the frame rules at draw time.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "gallery.h"

#include "tim_dat/gallery_dat.h"            // gallery_tex[]
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnWindow / DrawCmnCapGroup_W
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/message.h"    // PrintMsg
#include "../graphics/movie/movie.h"        // InitMovieWithTitle
#include "../system/os/system.h"            // GetLanguage
#include "title.h"                          // SetTitleBgSendLock

#include <string.h>                         // memset

/* Fade step, both directions. */
#define GAL_ANM_SPD     12

static void GalDispPicture(int alp);
static void GalDispViewPage(int alp);
static void GalDispMenuCsr(int csr, int alp);
static void GalDispTitle(int place, int mx, int my, int alp);
static void GalDispCaption(int place, int alp);
static void GalDispWinMsg(int csr, int alp);
static void GalDispBgMask(int alp);

/* The place change happens at the bottom of the fade-out, which is why the
 * movie is started -- and the viewer's texture freed -- from inside here
 * rather than from the pad handlers that asked for the change.
 *
 * Returns 1 exactly once, on the frame the fade-out for place 3 completes. */
int GalAnimation(void)
{
    int ret = 0;

    if (gc->anm_step == GAL_ANM_FADE_IN)                                /* 82 */
    {
        if (gc->anm_alpha < 0x80)                                       /* 84 */
        {
            gc->anm_alpha += GAL_ANM_SPD;                               /* 85 */
            if (gc->anm_alpha >= 0x80)                                  /* 86 */
            {
                gc->anm_alpha = 0x80;                                   /* 87 */
                gc->anm_step = 0;                                       /* 91 */
            }
        }
    }
    else if (gc->anm_step == GAL_ANM_FADE_OUT)                          /* 93 */
    {
        if (gc->anm_alpha > 0)                                          /* 94 */
        {
            gc->anm_alpha -= GAL_ANM_SPD;                               /* 95 */
            if (gc->anm_alpha <= 0)                                     /* 96 */
            {
                gc->anm_alpha = 0;                                      /* 98 */
                gc->anm_step = GAL_ANM_FADE_IN;                         /* 99 */

                if (gc->next_place == GAL_PLACE_END)                    /* 100 */
                {
                    ret = 1;                                            /* 101 */
                }
                else
                {
                    if (gc->next_place == GAL_PLACE_MOVIE)              /* 102 */
                    {
                        InitMovieWithTitle(gc->movie_no, 1);            /* 103 */
                        SetTitleBgSendLock('\x01');                     /* 105 */
                    }
                    else if (gc->now_place == GAL_PLACE_VIEW)           /* 106 */
                    {
                        GalPictureMemFree();                            /* 108 */
                    }

                    gc->now_place = gc->next_place;                     /* 112 */
                }
            }
        }
    }
    else if (gc->anm_step == 0)                                         /* 114 */
    {
        gc->anm_alpha = 0x80;
    }

    return ret;                                                         /* 118 */
}

/* Sprites 0x14/0x15 are stretched horizontally by 16.83 and 0x16/0x17 by 7.1
 * and turned on their side -- the two frame rules.  The whole page's alpha is
 * scaled by 0.15 on the way in, so the plates never reach full opacity. */
void GalleryDispTop(void)
{
    SQAR_DAT SqarDat = { 0x1f9, 0xd5, 0x43, 0x56, 0, 0x6e, 0x6e, 0x6e, 0 }; /* 128 */
    DISP_SQAR DispSqar;
    DISP_SPRT spr;
    int i;
    int alpha;

    alpha = gc->anm_alpha;                                              /* 132 */

    GalDispBgMask(alpha);                                               /* 134 */
    GalPK2SendVram(GAL_TEX_TOP, gal_top_tex_addr);                      /* 136 */

    for (i = 0x10; i < 0x18; i++)                                       /* 137 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 138 */

        if ((i == 0x14) || (i == 0x15))                                 /* 139 */
        {
            spr.csx = spr.x;                                            /* 140 */
            spr.csy = spr.y;
            spr.scw = 16.829999f;                                       /* 142 */
            spr.sch = 1.0f;                                             /* 143 */
        }
        if ((i == 0x16) || (i == 0x17))                                 /* 144 */
        {
            spr.crx = spr.x;                                            /* 146 */
            spr.cry = spr.y;
            spr.csx = spr.x;                                            /* 147 */
            spr.csy = spr.y;
            spr.scw = 7.0999999f;                                       /* 148 */
            spr.sch = 1.0f;                                             /* 149 */
            spr.rot = 270.0f;                                           /* 150 */
        }

        spr.alpha = (u_char)((float)alpha * 0.14999999f);               /* 151 */
        DispSprD(&spr);                                                 /* 152 */
    }                                                                   /* 153 */

    CopySqrDToSqr(&DispSqar, &SqarDat);                                 /* 154 */
    DispSqar.alpha = (u_char)((float)alpha * 0.14999999f);              /* 155 */
    DispSqrD(&DispSqar);                                                /* 156 */

    for (i = 0x18; i < 0x24; i++)                                       /* 157 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 158 */

        if (i == 0x19)                                                  /* 159 */
        {
            spr.csx = spr.x;
            spr.csy = spr.y;
            spr.scw = 16.469999f;
            spr.sch = 1.0f;
        }
        else if (i == 0x1c)
        {
            spr.csx = spr.x;
            spr.csy = spr.y;
            spr.scw = 10.469999f;
            spr.sch = 1.0f;
        }
        else if (i == 0x1f)
        {
            spr.csx = spr.x;
            spr.csy = spr.y;
            spr.scw = 2.3699999f;
            spr.sch = 1.0f;
        }
        else if (i == 0x22)
        {
            spr.csx = spr.x;
            spr.csy = spr.y;
            spr.scw = 7.5299997f;
            spr.sch = 1.0f;
        }

        if (i > 0x1d)                                                   /* 163 */
        {
            spr.rot = 270.0f;                                           /* 164 */
            spr.crx = spr.x;
            spr.cry = spr.y;
        }

        spr.alpha = (u_char)alpha;                                      /* 167 */
        DispSprD(&spr);                                                 /* 168 */
    }                                                                   /* 169 */

    for (i = 0x2b; i < 0x30; i++)                                       /* 170 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 171 */

        if ((i == 0x2c) || (i == 0x2e) || (i == 0x2f))                  /* 174 */
        {
            spr.crx = spr.x;
            spr.cry = spr.y;
            spr.rot = 270.0f;                                           /* 177 */
        }

        spr.alpha = (u_char)alpha;                                      /* 178 */
        DispSprD(&spr);
    }

    GalDispMenuCsr(gc->cursor, alpha);                                  /* 180 */
    GalDispTitle(gc->now_place, 0, 0, alpha);                           /* 182 */
    GalDispCaption(gc->now_place, alpha);                               /* 184 */
    GalDispWinMsg(gc->cursor, alpha);                                   /* 186 */
}

/* 0x60 and 0x61 are the two page arrows and are drawn additively. */
void GalleryDispView(void)
{
    DISP_SPRT spr;
    int i;
    int alpha;

    alpha = gc->anm_alpha;                                              /* 198 */

    GalDispBgMask(alpha);                                               /* 200 */
    GalPK2SendVram(GAL_TEX_VIEW, gal_view_tex_addr);                    /* 202 */

    for (i = 0x5e; i < 100; i++)                                        /* 203 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 204 */

        if ((i == 0x60) || (i == 0x61))                                 /* 205 */
        {
            spr.alphar = 0x48;                                          /* 208 */
        }

        spr.alpha = (u_char)alpha;                                      /* 209 */
        DispSprD(&spr);                                                 /* 210 */
    }                                                                   /* 212 */

    GalDispViewPage(alpha);                                             /* 214 */
    GalDispPicture(alpha);                                              /* 216 */
    GalDispTitle(gc->now_place, 0, 0, alpha);                           /* 218 */
    GalDispCaption(gc->now_place, alpha);
}

/* The picture itself is a single full-frame plate, but three of the twenty-odd
 * entries need special handling: set 0 picture 2 is two plates (the second
 * rotated), and set 2 picture 0x10 has its own caption. */
static void GalDispPicture(int alp)
{
    DISP_SPRT spr;
    u_char alpha;

    alpha = (u_char)alp;                                                /* 230 */
    if (gc->anm_step == 0)                                              /* 231 */
    {
        alpha = (u_char)gc->pic_anm_alpha;                              /* 233 */
    }

    if (gc->pic_step < GAL_PIC_SHOWN)                                   /* 235 */
    {
        return;                                                         /* 236 */
    }

    GalPK2SendVram(GAL_TEX_PIC, gal_pic_tex_addr);                      /* 238 */

    if (gc->pic_mode == 0)                                              /* 239 */
    {
        if (gc->pic_no == 0)                                            /* 242 */
        {
            CopySprDToSpr(&spr, gallery_tex + 0x72);                    /* 243 */
        }
        else if (gc->pic_no == 1)                                       /* 246 */
        {
            CopySprDToSpr(&spr, gallery_tex + 0x73);                    /* 247 */
        }
        else if (gc->pic_no == 2)                                       /* 249 */
        {
            CopySprDToSpr(&spr, gallery_tex + 0x74);                    /* 250 */
            spr.alpha = alpha;                                          /* 251 */
            DispSprD(&spr);                                             /* 253 */

            CopySprDToSpr(&spr, gallery_tex + 0x75);                    /* 254 */
            spr.rot = 270.0f;                                           /* 257 */
            spr.crx = spr.x;
            spr.cry = spr.y;
        }
        else
        {
            return;
        }
    }
    else if (gc->pic_mode == 1)                                         /* 259 */
    {
        CopySprDToSpr(&spr, gallery_tex + 0x71);                        /* 261 */
    }
    else if (gc->pic_mode == 2)                                         /* 262 */
    {
        if (gc->pic_no == 0x10)                                         /* 264 */
        {
            CopySprDToSpr(&spr, gallery_tex + 0x70);                    /* 265 */
        }
        else
        {
            CopySprDToSpr(&spr, gallery_tex + 0x6f);                    /* 267 */
        }

        spr.alpha = alpha;
        DispSprD(&spr);
        return;
    }
    else
    {
        return;
    }

    spr.alpha = alpha;
    DispSprD(&spr);                                                     /* 270 */
}

/* "nn / nn".  The tens digit is only drawn when there is one, so a single
 * digit sits where the units would be rather than as "0n". */
static void GalDispViewPage(int alp)
{
    DISP_SPRT spr;

    if (gc->pic_no + 1 > 9)                                             /* 285 */
    {
        CopySprDToSpr(&spr, gallery_tex + 100 + (gc->pic_no + 1) / 10); /* 286 */
        spr.x = 284.0f;                                                 /* 288 */
        spr.y = 28.0f;
        spr.alpha = (u_char)alp;                                        /* 290 */
        DispSprD(&spr);                                                 /* 291 */
    }

    CopySprDToSpr(&spr, gallery_tex + 100 + (gc->pic_no + 1) % 10);     /* 293 */
    spr.x = 298.0f;                                                     /* 295 */
    spr.y = 28.0f;
    spr.alpha = (u_char)alp;                                            /* 297 */
    DispSprD(&spr);                                                     /* 299 */

    CopySprDToSpr(&spr, gallery_tex + 0x6e);                            /* 300 */
    spr.alpha = (u_char)alp;                                            /* 301 */
    DispSprD(&spr);                                                     /* 303 */

    if (gc->pic_max > 9)                                                /* 305 */
    {
        CopySprDToSpr(&spr, gallery_tex + 100 + gc->pic_max / 10);      /* 306 */
        spr.x = 326.0f;
        spr.y = 28.0f;
        spr.alpha = (u_char)alp;
        DispSprD(&spr);                                                 /* 307 */
    }

    CopySprDToSpr(&spr, gallery_tex + 100 + gc->pic_max % 10);          /* 308 */
    spr.x = 340.0f;
    spr.y = 28.0f;
    spr.alpha = (u_char)alp;
    DispSprD(&spr);
}

/* The movie plays through the movie decoder, not the 2D layer. */
void GalleryDispMovie(void)
{
}

/* The eight menu rows.  The first three (picture sets) are one plate each; the
 * five movie rows are two.  A locked row swaps in its greyed replacement, and
 * the selected row is drawn from the same index plus 0xd. */
static void GalDispMenuCsr(int csr, int alp)
{
    int pic_menu_sp_no[3] = { 0x32, 0x31, 0x30 };
    int mov_menu_sp_no[5] = { 0x33, 0x35, 0x37, 0x39, 0x3b };
    int csr_pos_left[GAL_CSR_NUM] =
    {
        0x14c, 0x151, 0x156, 0x159, 0x15e, 0x163, 0x168, 0x16d,
    };
    int csr_pos_right[GAL_CSR_NUM][5] =                                 /* rdata 3b3400 */
    {
        { 552, 552, 510, 522, 552 },
        { 447, 447, 441, 472, 525 },
        { 571, 571, 571, 565, 571 },
        { 561, 561, 557, 517, 561 },
        { 567, 567, 563, 523, 567 },
        { 571, 571, 567, 527, 571 },
        { 577, 577, 577, 577, 577 },
        { 565, 565, 565, 565, 565 },
    };
    int csr_pos_y[GAL_CSR_NUM] =
    {
        0x49, 99, 0x7d, 0xb4, 0xce, 0xe8, 0x102, 0x11c,
    };
    SPRT_DAT *spp;
    DISP_SPRT spr;
    int i;

    if (gc->game_clear_flg == 0)                                        /* 358 */
    {
        pic_menu_sp_no[0] = 0x4c;
        pic_menu_sp_no[1] = 0x4d;
    }
    if (gc->setup_pic_flg == 0)                                         /* 369 */
    {
        pic_menu_sp_no[2] = 0x4e;
    }
    if (gc->ending1_mov_flg == 0)                                       /* 381 */
    {
        mov_menu_sp_no[2] = 0x37;
        mov_menu_sp_no[3] = 0x4f;
    }
    if (gc->ending2_mov_flg == 0)                                       /* 397 */
    {
        mov_menu_sp_no[4] = 0x51;
    }

    for (i = 0; i < 3; i++)                                             /* 398 */
    {
        /* Row 2's unlocked art is 0x30, which is also its locked index, so it
         * needs the flag tested a second time here. */
        if ((pic_menu_sp_no[i] == 0x30) && (gc->setup_pic_flg == 1))    /* 399 */
        {
            spp = gallery_tex + 0x30;
        }
        else
        {
            spp = gallery_tex + pic_menu_sp_no[i];                      /* 401 */
        }

        CopySprDToSpr(&spr, (i == csr) ? spp + 0xd : spp);              /* 404 */
        spr.alpha = (u_char)alp;                                        /* 405 */
        DispSprD(&spr);                                                 /* 407 */
    }                                                                   /* 408 */

    for (i = 0; i < 5; i++)                                             /* 411 */
    {
        spp = gallery_tex + mov_menu_sp_no[i];                          /* 412 */
        if (i + 3 == csr)                                               /* 415 */
        {
            spp += 0xd;
        }

        CopySprDToSpr(&spr, spp);                                       /* 417 */
        spr.alpha = (u_char)alp;                                        /* 420 */
        DispSprD(&spr);                                                 /* 422 */

        CopySprDToSpr(&spr, spp + 1);                                   /* 423 */
        spr.alpha = (u_char)alp;                                        /* 424 */
        DispSprD(&spr);                                                 /* 425 */
    }                                                                   /* 426 */

    CopySprDToSpr(&spr, gallery_tex + 0x4a);                            /* 429 */
    spr.x = (float)csr_pos_left[csr];                                   /* 431 */
    spr.y = (float)csr_pos_y[csr];                                      /* 433 */
    spr.alpha = (u_char)alp;                                            /* 435 */
    DispSprD(&spr);                                                     /* 436 */

    CopySprDToSpr(&spr, gallery_tex + 0x4b);                            /* 448 */
    spr.x = (float)csr_pos_right[csr][GetLanguage()];                   /* 449 */
    spr.y = (float)csr_pos_y[csr];                                      /* 450 */
    spr.alpha = (u_char)alp;                                            /* 451 */
    DispSprD(&spr);                                                     /* 453 */
}

/* Two plates from the shared outgame pak and two from the gallery's own.
 * `place`, `mx` and `my` are all dead. */
static void GalDispTitle(int place, int mx, int my, int alp)
{
    DISP_SPRT spr;
    int i;

    (void)place;
    (void)mx;
    (void)my;

    GalPK2SendVram(GAL_TEX_OG, gal_og_tex_addr);                        /* 469 */

    for (i = 0; i < 2; i++)                                             /* 470 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 471 */
        spr.alpha = (u_char)alp;                                        /* 472 */
        DispSprD(&spr);                                                 /* 473 */
    }                                                                   /* 474 */

    GalPK2SendVram(GAL_TEX_CMN, gal_cmn_tex_addr);                      /* 475 */

    for (i = 2; i < 4; i++)                                             /* 476 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 477 */
        spr.alpha = (u_char)alp;                                        /* 478 */
        DispSprD(&spr);                                                 /* 479 */
    }                                                                   /* 480 */
}

static void GalDispCaption(int place, int alp)
{
    if (place == GAL_PLACE_TOP)                                         /* 490 */
    {
        DrawCmnCapGroup_W(0, 0, (u_char)alp, 0);                        /* 493 */
    }
    else
    {
        DrawCmnCapGroup_W(0xc, 0xc, (u_char)alp, 0);                    /* 515 */
    }
}

/* The help window under the menu.  Only rows 3, 4 and 5 have an illustration
 * to go with the text; the rest get the message alone. */
static void GalDispWinMsg(int csr, int alp)
{
    int msg_id[10] = { 2, 1, 0, 3, 4, 5, 6, 7, 8, 9 };                  /* 533 */
    SPRT_DAT *spp;
    DISP_SPRT spr;
    int i;

    DrawCmnWindow(0, 15.0f, 324.0f, 610.0f, 120.0f, 0x58, (u_char)alp); /* 550 */

    if (csr == 3)                                                       /* 552 */
    {
        spp = gallery_tex + 0x55;                                       /* 554 */
    }
    else if (csr == 4)                                                  /* 555 */
    {
        spp = gallery_tex + 0x58;                                       /* 557 */
    }
    else if (csr == 5)                                                  /* 558 */
    {
        spp = gallery_tex + 0x5b;                                       /* 560 */
    }
    else
    {
        spp = (SPRT_DAT *)0;                                            /* 561 */
    }

    if (spp != (SPRT_DAT *)0)                                           /* 566 */
    {
        GalPK2SendVram(GAL_TEX_TOP, gal_top_tex_addr);                  /* 567 */

        for (i = 0; i < 3; i++)                                         /* 568 */
        {
            CopySprDToSpr(&spr, spp + i);                               /* 569 */
            spr.alpha = (u_char)alp;                                    /* 571 */
            DispSprD(&spr);                                             /* 572 */
        }
    }

    PrintMsg(0x22, msg_id[csr], 0x28, 0x15c, 1, alp, 0);                /* 576 */
}

/* The full-screen dim plus the frame that every page shares.  SqarDat is
 * memset first and only w/h written, so the quad is black at 0.4 * alpha. */
static void GalDispBgMask(int alp)
{
    DISP_SQAR DispSqar;
    SQAR_DAT SqarDat;
    DISP_SPRT spr;
    int i;

    memset(&SqarDat, 0, sizeof(SqarDat));
    SqarDat.w = 640;
    SqarDat.h = 448;

    CopySqrDToSqr(&DispSqar, &SqarDat);
    DispSqar.alpha = (u_char)((float)alp * 0.39999998f);
    DispSqrD(&DispSqar);

    GalPK2SendVram(GAL_TEX_CMN, gal_cmn_tex_addr);

    for (i = 4; i < 0xc; i++)
    {
        CopySprDToSpr(&spr, gallery_tex + i);
        spr.alpha = (u_char)alp;
        DispSprD(&spr);
    }

    if (gc->now_place == GAL_PLACE_TOP)
    {
        GalPK2SendVram(GAL_TEX_TOP, gal_top_tex_addr);

        for (i = 0x24; i < 0x2b; i++)
        {
            CopySprDToSpr(&spr, gallery_tex + i);

            if ((i == 0x27) || (i == 0x29))
            {
                spr.crx = spr.x;
                spr.cry = spr.y;
                spr.rot = 270.0f;
            }

            spr.alpha = (u_char)alp;
            DispSprD(&spr);
        }
    }

    GalPK2SendVram(GAL_TEX_CMN, gal_cmn_tex_addr);                      /* 617 */

    for (i = 0xc; i < 0x10; i++)                                        /* 618 */
    {
        CopySprDToSpr(&spr, gallery_tex + i);                           /* 619 */
        spr.alpha = (u_char)alp;                                        /* 621 */
        DispSprD(&spr);                                                 /* 621 */
    }                                                                   /* 622 */
}
