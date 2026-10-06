// FILE: /home/zero_rom/zero2np/src/outgame/framerate.c
//
// The frame-rate (50/60 Hz) selection screen.  Two labels side by side, the
// active one at full brightness and the other at half, with the title's arrow
// pair bracketing whichever is current.
//
// There is no cursor state: GetPALMode() *is* the selection, LEFT or RIGHT
// simply toggles the video mode, and the arrows are placed from the same test.
//
// framerate.o has no static data beyond the fixed_array<> assert boilerplate.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "framerate.h"

#include "title.h"                          // GetTitleLogoTexAddr / GetTitleAnimRGB
#include "title_disp.h"                     // DispTitleZeroLogo / Cursor L,R / Caption
#include "tim_dat/title_dat.h"              // title_top[]
#include "../common/variable.h"             // pad[]
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / CopySprDToSpr / DispSprD
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../main/gphase.h"                 // SetNextGPhase / GID_TITLE_MENU
#include "../system/os/system.h"            // SystemBankPlay / ChangeVideoMode / GetPALMode
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t

static void FrameRateSelPad(void);
static void FrameRateSelTitleDisp(int off_x, int off_y, u_char alpha);
static void FrameRateSelItemDisp(int off_x, int off_y, u_char alpha);
static void FrameRateSelCursorDisp(int off_x, int off_y, u_char alpha);

void FrameRateSelMain(void)
{
    FrameRateSelPad();                                                  /* 63 */
}

/* LEFT or RIGHT does not move a cursor -- it flips the video mode outright,
 * so the two labels swap brightness on the same frame. */
static void FrameRateSelPad(void)
{
    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0) ||
        ((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))     /* 75 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 77 */

        if (GetPALMode() == 0)                                          /* 80 */
        {
            ChangeVideoMode('\x03');                                    /* 82 */
        }
        else
        {
            ChangeVideoMode('\x02');                                    /* 86 */
        }
    }
    else if (*paddat[0] == 1)                                           /* 90 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 91 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 94 */
    }
    else if (*paddat[1] == 1)                                           /* 97 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 98 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 101 */
    }
}

/* No TECMO logo here -- only the ZERO one. */
void FrameRateSelDispMain(void)
{
    DispTitleZeroLogo(0, 0, 0x80);                                      /* 125 */

    FrameRateSelTitleDisp(0, 0, 0x80);                                  /* 128 */
    FrameRateSelItemDisp(0, 0, 0x80);                                   /* 131 */
    FrameRateSelCursorDisp(0, 0, 0x80);                                 /* 134 */

    TitleCaptionDisp(0, 0, 0x80);                                       /* 137 */
}

/* The screen's heading, two plates.  All three parameters are dead -- the same
 * quirk DispTitleMenuItem() has. */
static void FrameRateSelTitleDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT ds;
    int i;

    (void)off_x;
    (void)off_y;
    (void)alpha;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 153 */

    for (i = 0; i < 2; i++)                                             /* 156 */
    {
        CopySprDToSpr(&ds, title_top + 0x1b + i);                       /* 158 */
        ds.alphar = 0x48;                                               /* 159 */
        DispSprD(&ds);                                                  /* 160 */
    }                                                                   /* 161 */
}

/* The two rate labels.  The inactive one is drawn at a flat alpha >> 1 rather
 * than through the sprite's own alpha, so it dims to exactly half of what the
 * caller asked for regardless of the plate. */
static void FrameRateSelItemDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT rate_ds;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 176 */

    CopySprDToSpr(&rate_ds, title_top + 0x19);                          /* 181 */
    rate_ds.x += (float)off_x;                                          /* 182 */
    rate_ds.y += (float)off_y;                                          /* 183 */
    rate_ds.alphar = 0x48;
    if (GetPALMode() != 0)                                              /* 185 */
    {
        rate_ds.alpha = (u_char)(((int)rate_ds.alpha * (int)alpha) >> 7);/* 186 */
    }
    else
    {
        rate_ds.alpha = (u_char)(alpha >> 1);                           /* 189 */
    }
    DispSprD(&rate_ds);                                                 /* 191 */

    CopySprDToSpr(&rate_ds, title_top + 0x1a);                          /* 194 */
    rate_ds.x += (float)off_x;                                          /* 195 */
    rate_ds.y += (float)off_y;                                          /* 196 */
    rate_ds.alphar = 0x48;
    if (GetPALMode() == 0)                                              /* 198 */
    {
        rate_ds.alpha = (u_char)(((int)rate_ds.alpha * (int)alpha) >> 7);/* 199 */
    }
    else
    {
        rate_ds.alpha = (u_char)(alpha >> 1);                           /* 202 */
    }
    DispSprD(&rate_ds);                                                 /* 204 */
}

/* Both y coordinates are 385.0 -- two separate .lit4 slots holding the same
 * value, one per expansion.  off_x / off_y / alpha are dead again. */
static void FrameRateSelCursorDisp(int off_x, int off_y, u_char alpha)
{
    (void)off_x;
    (void)off_y;
    (void)alpha;

    if (GetPALMode() != 0)                                              /* 217 */
    {
        DispTitleCursorL(189.0f, 385.0f, 0x80, GetTitleAnimRGB());      /* 221 */
        DispTitleCursorR(299.0f, 385.0f, 0x80, GetTitleAnimRGB());      /* 222 */
    }
    else
    {
        DispTitleCursorL(321.0f, 385.0f, 0x80, GetTitleAnimRGB());      /* 226 */
        DispTitleCursorR(431.0f, 385.0f, 0x80, GetTitleAnimRGB());      /* 227 */
    }
}
