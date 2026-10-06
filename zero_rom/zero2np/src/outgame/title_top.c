// FILE: /home/zero_rom/zero2np/src/outgame/title_top.c
//
// The title top screen: the ZERO / TECMO logos plus the PRESS START plate,
// and the one input test that moves on to the title menu.
//
// The whole file is four functions and no state -- the pulse the PRESS START
// plate is drawn with comes from title.c's TITLE_DISP_CTRL, and the logos are
// title_disp.o's.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "title_top.h"

#include "title.h"                          // GetTitleLogoTexAddr / GetTitleAnimRGB
#include "title_disp.h"                     // DispTitleZeroLogo / DispTitleTecmoLogo
#include "tim_dat/title_dat.h"              // title_top[]
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / CopySprDToSpr / DispSprD
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../main/gphase.h"                 // SetNextGPhase / GID_TITLE_MENU
#include "../system/os/system.h"            // SystemBankPlay
#include "../system/pad/pad.h"              // paddat

#include <stdint.h>                         // uintptr_t

static void TitleTopPad(void);
static void TitleTopPressStartDisp(u_char rgb);

void TitleTopMain(void)
{
    TitleTopPad();                                                      /* 59 */
}

/* paddat[] entries are logical action labels, not keys: on every pad type in
 * key_cnf.c, label 7 is cnt[12] (START) and label 0 is cnt[5] (CROSS).  The
 * counters are hold counts, so `== 1` is "pressed this frame". */
static void TitleTopPad(void)
{
    if ((*paddat[7] == 1) || (*paddat[0] == 1))                         /* 70 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 71 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 73 */
    }
}

void TitleTopDispMain(int off_x, int off_y, u_char alpha)
{
    DispTitleZeroLogo(off_x, off_y, alpha);                             /* 92 */

    DispTitleTecmoLogo(off_x, off_y, alpha);                            /* 94 */

    TitleTopPressStartDisp(GetTitleAnimRGB());                          /* 97 */
}

/* The PRESS START plate.  It is additively blended (alphar 0x48) and tinted
 * by the shared cursor pulse, which is what makes it breathe rather than
 * blink. */
static void TitleTopPressStartDisp(u_char rgb)
{
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 111 */

    CopySprDToSpr(&ds, title_top + 0xe);                                /* 122 */
    ds.r = ds.g = ds.b = rgb;                                           /* 123 */
    ds.alphar = 0x48;                                                   /* 124 */
    DispSprD(&ds);                                                      /* 125 */
}
