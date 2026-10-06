// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_cam.c
//
// The in-game menu's camera page (menu_cam.o) -- menu_ctrl[] row 2.
//
// Two functions and no data at all: the whole object is the three-step
// bracket that hands GID_STORY_MENU's camera row over to menu_cam_main.o and
// takes the player back to the hub when it reports done.  Note the page's own
// `menu_wrk.step` ladder is a *third* one on top of menu_cam_main.o's two,
// and that MenuCamMainInit() is called with init_type 0 here -- setup.c's
// Mission Mode screen is the 1.
//
// Nothing here draws: MenuCamDisp() only forwards, and only in step 1.  Step
// 2 draws nothing at all, so the frame the page reports done on is blank --
// harmless, because SetNextMenuStep() has already queued the hub for it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_cam.o.
// Both ZERO2.MAP exports, verified 2/2 against the map and 2/2 against
// functions.txt (neither has a local).  .text is accounted for byte-for-byte
// -- 0x1e7288..0x1e733c = 0xb4 = 180 bytes: 132 + 44 plus one 4-byte
// alignment fill.

#include "menu_cam.h"

#include "menu.h"                               /* menu_wrk / SetNextMenuStep */
#include "menu_cam_main.h"                      /* MenuCamMainInit / Main / Disp */

void MenuCam(void)                                                      /* 52 */
{
    if (menu_wrk.step == 0) {                                           /* 56 */
        MenuCamMainInit(0);                                             /* 57 */

        menu_wrk.step = 1;                                              /* 59 */
    }

    if (menu_wrk.step == 1) {                                           /* 63 */
        if (MenuCamMain() != 0) {                                       /* 64 */
            menu_wrk.step = 2;                                          /* 65 */
        }
    }

    if (menu_wrk.step == 2) {                                           /* 70 */
        SetNextMenuStep(MENU_STEP_TOP);                                 /* 72 */
    }
}

void MenuCamDisp(void)                                                  /* 85 */
{
    if (menu_wrk.step == 1) {                                           /* 88 */
        MenuCamMainDisp();                                              /* 90 */
    }
}
