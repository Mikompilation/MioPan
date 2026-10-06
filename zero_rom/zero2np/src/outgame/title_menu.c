// FILE: /home/zero_rom/zero2np/src/outgame/title_menu.c
//
// The title menu: a horizontal row of eight entries with a left/right arrow
// pair bracketing the selected one.  The cursor moves with LEFT/RIGHT (pad or
// analog), CROSS enters, TRIANGLE goes back to the title top, and UP/DOWN are
// the video-mode override.
//
// The two debug entries (6 DEBUG MENU, 7 CHAPTER SELECT) have no artwork and
// are printed as ASCII instead; they are also the two the arrow pair skips.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "title_menu.h"

#include "title.h"                          // GetTitleLogoTexAddr / SetTitleLoadFlg
#include "title_disp.h"                     // DispTitleZeroLogo / Cursor L,R / Caption
#include "title_movie.h"                    // MoveTitleMovieTimerRestart
#include "tim_dat/title_dat.h"              // title_top[] / title_*_x_tbl
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../common/variable.h"             // pad[] / key_now[]
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / CopySprDToSpr / DispSprD
#include "../graphics/graph2d/message.h"    // SetASCIIString2
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../ingame/loading/loading.h"      // ReleaseLoadingTexMem
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../system/os/system.h"            // SystemBankPlay / ChangeVideoMode / GetLanguage
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t

#define TITLE_MENU_ITEM_NUM  8

static TITLE_MENU_CTRL title_menu_ctrl;                                 /* sbss 3f4fe0 */

static void TitleMenuPad(void);
static void TitleMenuMovePhase(void);
static void DispTitleMenuItem(int off_x, int off_y, u_char alpha);
static void DispTitleMenuCursor(u_char rgb);

void TitleMenuCtrlInit(void)
{
    title_menu_ctrl.csr = '\0';                                         /* 76 */
}

void TitleMenuMain(void)
{
    TitleMenuPad();                                                     /* 92 */
}

/* Pad masks are the game's own remapped layout (system/pad/pad.c builds `now`
 * with the d-pad in the high byte): 0x8000 LEFT, 0x2000 RIGHT.
 * GetPadAnalogRpt() indices are 0 up, 1 down, 2 left, 3 right.
 *
 * paddat[] entries are logical action labels: 9 is cnt[0] (UP), 8 is cnt[1]
 * (DOWN), 0 is cnt[5] (CROSS) and 1 is cnt[4] (TRIANGLE). */
static void TitleMenuPad(void)
{
    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0))     /* 108 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 109 */
        title_menu_ctrl.csr = (title_menu_ctrl.csr + TITLE_MENU_ITEM_NUM - 1) %
                              TITLE_MENU_ITEM_NUM;                      /* 112 */
    }
    else if (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))/* 131 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 132 */
        title_menu_ctrl.csr = (title_menu_ctrl.csr + 1) %
                              TITLE_MENU_ITEM_NUM;                      /* 135 */
    }
    /* UP / DOWN force the video mode.  There is no confirmation and no sound;
     * this is the 60/50 Hz escape hatch for a mis-detected disc region. */
    else if (*paddat[9] == 1)                                           /* 155 */
    {
        ChangeVideoMode('\x02');                                        /* 156 */
    }
    else if (*paddat[8] == 1)                                           /* 158 */
    {
        ChangeVideoMode('\x03');                                        /* 159 */
    }
    else if (*paddat[0] == 1)                                           /* 164 */
    {
        TitleMenuMovePhase();                                           /* 165 */
    }
    else if (*paddat[1] == 1)                                           /* 168 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 169 */
        SetNextGPhase(GID_TITLE_TOP);                                   /* 171 */
    }

    /* Any face or shoulder button held keeps the attract movie away.  These
     * are key_now[] slots -- sce_pad order, so 4..7 are TRIANGLE/CROSS/SQUARE/
     * CIRCLE and 8..11 are L1/L2/R1/R2.  GCC folded the eight tests into one
     * OR chain, which is why they carry a single line number. */
    if ((*key_now[4] != 0) || (*key_now[5] != 0) ||
        (*key_now[6] != 0) || (*key_now[7] != 0) ||
        (*key_now[8] != 0) || (*key_now[9] != 0) ||
        (*key_now[10] != 0) || (*key_now[11] != 0))                     /* 176 */
    {
        MoveTitleMovieTimerRestart();                                   /* 178 */
    }
}

static void TitleMenuMovePhase(void)
{
    switch (title_menu_ctrl.csr)                                        /* 189 */
    {
    case 0:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 193 */
        SetNextGPhase(GID_TITLE_NEWGAME);                               /* 195 */
        break;

    case 1:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 198 */
        SetNextGPhase(GID_TITLE_LOADGAME);                              /* 201 */
        break;

    case 2:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 204 */
        SetNextGPhase(GID_TITLE_ALBUM);                                 /* 205 */
        break;

    case 3:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 208 */
        SetNextGPhase(GID_TITLE_GALLERY);                               /* 209 */
        break;

    case 4:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 212 */
        SetNextGPhase(GID_TITLE_OPTION);                                /* 213 */
        break;

    case 5:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 217 */
        SetNextGPhase(GID_TITLE_FRAMERATE_SEL);                         /* 218 */
        break;

    /* The debug menu takes the outgame heap for itself, so the title's own
     * "assets resident" flag has to come down on the way out. */
    case 6:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 222 */
        SetTitleLoadFlg('\0');                                          /* 224 */
        ReleaseLoadingTexMem();                                         /* 225 */
        SetNextGPhase(GID_DEBUG_MENU);                                  /* 226 */
        break;

    case 7:
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 229 */
        SetNextGPhase(GID_TITLE_CHAPTER_SEL);                           /* 230 */
        break;

    default:
        PRINT_ASSERT("Error! TitleMenuMovePhase");                      /* 241 */
        break;
    }
}

void TitleMenuDispMain(int off_x, int off_y, u_char alpha)
{
    DispTitleZeroLogo(0, 0, 0x80);                                      /* 264 */
    DispTitleTecmoLogo(0, 0, 0x80);                                     /* 267 */

    DispTitleMenuItem(off_x, off_y, alpha);                             /* 273 */
    DispTitleMenuCursor(GetTitleAnimRGB());                             /* 276 */

    TitleCaptionDisp(0, 0, 0x80);                                       /* 279 */
}

/* Only the selected entry is drawn -- title_tbl[] gives it one or two sprites
 * (a two-word caption needs two), -1 meaning "no more".
 *
 * All three parameters are dead in the ROM: neither the sprite path nor the
 * ASCII path reads off_x, off_y or alpha.  Reproduced as found. */
static void DispTitleMenuItem(int off_x, int off_y, u_char alpha)
{
    static int title_tbl[TITLE_MENU_ITEM_NUM][2] =                      /* rdata 3e6bb0 */
    {
        { 16, -1 },
        { 18, -1 },
        { 17, -1 },
        { 19, 20 },
        { 15, -1 },
        { 27, 28 },
        { -1, -1 },
        { -1, -1 },
    };
    DISP_SPRT ds;
    int i;

    (void)off_x;
    (void)off_y;
    (void)alpha;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 375 */

    for (i = 0; i < 2; i++)                                             /* 378 */
    {
        if (title_tbl[title_menu_ctrl.csr][i] != -1)                    /* 379 */
        {
            CopySprDToSpr(&ds,
                          title_top + title_tbl[title_menu_ctrl.csr][i]);/* 380 */
            ds.alphar = 0x48;                                           /* 381 */
            DispSprD(&ds);                                              /* 382 */
        }
        else if (title_menu_ctrl.csr == 6)                              /* 386 */
        {
            SetASCIIString2(0, 248.0f, 367.0f, 1,
                            0xff, 0xff, 0xff, "DEBUG MENU");            /* 387 */
            break;                                                      /* 388 */
        }
        else if (title_menu_ctrl.csr == 7)                              /* 390 */
        {
            SetASCIIString2(0, 224.0f, 367.0f, 1,
                            0xff, 0xff, 0xff, "CHAPTER SELECT");        /* 391 */
            break;                                                      /* 392 */
        }
    }                                                                   /* 403 */
}

/* No arrows on the two debug entries.  The test is
 * (u_char)(csr - 6) >= 2, so a csr outside 0..7 also draws them -- but
 * TitleMenuPad() keeps it in range. */
static void DispTitleMenuCursor(u_char rgb)
{
    if ((u_char)(title_menu_ctrl.csr - 6) >= 2)                         /* 421 */
    {
        DispTitleCursorL(
            (float)title_left_x_tbl[title_menu_ctrl.csr][GetLanguage()],
            355.0f, 0x80, rgb);                                         /* 427 */
        DispTitleCursorR(
            (float)title_right_x_tbl[title_menu_ctrl.csr][GetLanguage()],
            355.0f, 0x80, rgb);                                         /* 428 */
    }
}
