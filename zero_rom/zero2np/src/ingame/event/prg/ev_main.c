// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_main.c
//
// Event runtime top level.  EventInit() clears the per-event state table and
// then hands off to each event subsystem's own init; EventMain() is the
// per-frame driver ingame.c calls from every story phase that runs events.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_main.h"

#include "ev_change.h"                      // CompulsionSetEventStateMain / EvChangeCtrlInit
#include "ev_debug.h"                       // EvDbgMain / dbg_event_debug
#include "ev_disp.h"                        // EvDispInit
#include "ev_exe.h"                         // EventExeInit / EventExe
#include "ev_macro.h"                       // EventEnd_StreamRelease
#include "ev_open.h"                        // EvCtrlCenterInit / EventSetOpenCondition / ...
#include "ev_talk.h"                        // EvTalkInit
#include "ev_timer.h"                       // EvTimerCtrlInit / EvTimerMain
#include "../../map/map_rectangle.h"        // DrawEventRect
#include "../../../common/variable.h"       // plyr_wrk
#include "../../../system/os/eecdvd.h"      // LoadReq
#include "../../../system/os/system.h"      // GetPALMode

EV_WRK              ev_wrk;                 /* data 30fb80 */
CEventSisterGazeWrk ev_sister_gaze;         /* data 3119c0 */

/* Event macro data is language-dependent: the PAL build loads a different
 * pak, but both land at the same fixed EE address. */
#define EVENT_MACRO_NTSC_PK2 0xd35
#define EVENT_MACRO_PAL_PK2  0xd36
#define EVENT_MACRO_ADDR     0xd4ec00

void EventInit(void)
{
    ev_wrk.step = 0;
    ev_wrk.ev_no = 0;
    ev_wrk.wait_flg = 0;

    for (int i = 0; i < EVENT_STATE_MAX; i++) {
        ev_wrk.ev_state[i].state = 0;
    }

    EvCtrlCenterInit();
    EvCondCtrlInit();
    EventExeInit();
    EvTimerCtrlInit();
    EvChangeCtrlInit();
    EvPhotoObjInit();
    EvTalkInit();
    EvDispInit();
    ev_sister_gaze.Init();
}

void ev_gazeSisSetSave(MC_SAVE_DATA *save)
{
    save->size = sizeof(ev_sister_gaze);
    save->addr = (u_char *)&ev_sister_gaze;
}

void EventDataLoadReq(void)
{
    if (GetPALMode() != 0) {
        LoadReq(EVENT_MACRO_PAL_PK2, EVENT_MACRO_ADDR);
        return;
    }

    LoadReq(EVENT_MACRO_NTSC_PK2, EVENT_MACRO_ADDR);
}

void EventRootStart(void)
{
    EventSetOpenCondition(0);
}

void EventMain(void)
{
    sceVu0FVECTOR rect;

    CompulsionSetEventStateMain();
    EvTimerMain();
    EventCtrlCenterMain();
    EventExe();

    /* Debug event-trigger rectangle, drawn 5 units below the player's feet. */
    sceVu0CopyVector(rect, plyr_wrk.cmn_wrk.mbox.pos);
    rect[1] = rect[1] - 5.0f;
    DrawEventRect(rect);

    ev_sister_gaze.Work();

    /* Port addition.  ev_debug.o's EvDbgMain() has no caller anywhere in the
     * prototype, and this is the event system's only per-frame driver, so it
     * is where the console belongs.  Kept behind a switch rather than called
     * outright because EvDbgMain() draws the event rectangles a second time,
     * at the player's feet instead of 5 below -- with HIT RECTANGLE on, an
     * unconditional call would z-fight with the pass above. */
    if (dbg_event_debug != 0) {
        EvDbgMain();
    }
}

void EventEnd(void)
{
    EventEnd_StreamRelease();
}

void SetSave_EvWrk(MC_SAVE_DATA *data)
{
    data->size = sizeof(ev_wrk);
    data->addr = (u_char *)&ev_wrk;
}
