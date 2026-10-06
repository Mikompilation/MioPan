// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_exe.c
//
// Event execution: the list of macro programs currently running.
//
// ev_exe_ctrl[150] is that list; EventExe() steps every live entry once a
// frame through EventExeFuncCall() (ev_macro.c).  Everything else here either
// puts an event on the list (SetEventExeCtrl, via the three Set*Status entry
// points) or takes it off again (EventExeRelease).
//
// The init program is the exception: SetEventInitStatus() runs it immediately
// off a stack-local EV_EXE_CTRL rather than entering it into the list, so it
// completes within the same frame the open condition passed.
//
// Each transition also re-parents the event's sub-events, which is why three
// functions here walk EvGetSubId() and switch on GetEvState().  The rule is
// always the same: a sub-event still waiting to open follows its parent, one
// already running is left to finish on its own terms.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_exe.h"

#include <stdio.h>                                  // printf (queue-full banner)

#include "ev_change.h"                              // SetEventState
#include "ev_get.h"                                 // GetEvState / EvGetSubId / EvGetExeAddr / EV_TBL_* / EV_SUB_ID_MAX
#include "ev_macro.h"                               // EventMacroInit / EventExeFuncCall
#include "ev_main.h"                                // EV_STATE_*
#include "ev_open.h"                                // EventDelCondition / EventSetOpenCondition
#include "../../../common/utility2.h"               // PRINT_ASSERT
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../miopan/miopan_memory.h"                 // MioPan_GetPs2Address

static void EventExeCtrlInit(EV_EXE_CTRL *ctrl, int event_id, u_char *event_addr);
static int  GetExeCtrlEmpty(int event_id);

/* The execution list.  Saved to the memory card as one block. */
static fixed_array<EV_EXE_CTRL, EV_EXE_CTRL_MAX> ev_exe_ctrl;   /* bss 479850 */

void EventExeInit(void)
{                                                                       /* 105 */
    int i;

    for (i = 0; i < EV_EXE_CTRL_MAX; i++) {                             /* 110 */
        EventExeCtrlInit(&ev_exe_ctrl[i], -1, (u_char *)0);
    }                                                                   /* 112 */

    EventMacroInit();                                                   /* 115 */
}

static void EventExeCtrlInit(EV_EXE_CTRL *ctrl, int event_id, u_char *event_addr)
{
    ctrl->event_id   = event_id;                                        /* 130 */
    ctrl->stop_timer = 0;                                               /* 131 */
    ctrl->event_addr = event_addr;                                      /* 132 */
    ctrl->if_state   = 0;                                               /* 133 */
    ctrl->process    = 0;                                               /* 134 */
}

void SetEventInitStatus(int event_id)
{                                                                       /* 150 */
    fixed_array<int, EV_SUB_ID_MAX> id_tbl;
    EV_EXE_CTRL init_event;
    int i;

    EvGetSubId(event_id, &id_tbl[0]);

    EventExeRelease(event_id);                                          /* 167 */
    SetEventState(event_id, EV_STATE_INIT);                             /* 169 */
    EventDelCondition(event_id);                                        /* 171 */

    /* The init program runs here and now, off a stack-local control block --
     * it never joins ev_exe_ctrl[], so it cannot span frames. */
    EventExeCtrlInit(&init_event, event_id,
                     EvGetExeAddr(EV_TBL_INIT_PRG, event_id));          /* 173 */
    EventExeFuncCall(&init_event);                                      /* 175 */

    /* Arm the sub-events: the parent opening is what makes them eligible. */
    for (i = 0; i < EV_SUB_ID_MAX; i++) {
        if (id_tbl[i] == -1) {
            continue;
        }

        switch (GetEvState(id_tbl[i])) {                                /* 190 */
        case EV_STATE_SUSPEND:
            /* Parked when the parent last ended -- re-arm it first. */
            SetEventState(id_tbl[i], EV_STATE_WAIT_OPEN);
            /* fall through */
        case EV_STATE_WAIT_OPEN:
            EventSetOpenCondition(id_tbl[i]);
            break;                                                      /* 198 */

        default:
            PRINT_ASSERT("ERROR!! SetEventInitStatus event id = %d, sub event id = %d\n", event_id, id_tbl[i]);   /* 207 */
            /* fall through */
        case EV_STATE_INIT:
        case EV_STATE_EXE:
        case EV_STATE_WAIT_END:
        case EV_STATE_END:
        case EV_STATE_LOCK:
            break;
        }
    }                                                                   /* 210 */
}

void SetEventExeStatus(int event_id)
{
    EventExeRelease(event_id);                                          /* 234 */
    EventDelCondition(event_id);                                        /* 236 */
    SetEventExeCtrl(event_id, EV_TBL_EXE_PRG);                          /* 238 */
    SetEventState(event_id, EV_STATE_EXE);                              /* 240 */
}

void SetEventEndExeStatus(int event_id)
{
    EventDelCondition(event_id);                                        /* 264 */
    EventExeRelease(event_id);                                          /* 266 */
    SetEventState(event_id, EV_STATE_END);                              /* 268 */
    SetEventExeCtrl(event_id, EV_TBL_END_PRG);                          /* 270 */
    EventEnd_SubStateChange(event_id);                                  /* 273 */
}

void EventEnd_SubStateChange(int event_id)
{                                                                       /* 283 */
    fixed_array<int, EV_SUB_ID_MAX> id_tbl;
    int i;

    EvGetSubId(event_id, &id_tbl[0]);

    for (i = 0; i < EV_SUB_ID_MAX; i++) {
        if (id_tbl[i] == -1) {
            continue;
        }

        switch (GetEvState(id_tbl[i])) {                                /* 300 */
        case EV_STATE_WAIT_OPEN:
            /* Still waiting: retire its conditions and park it, so nothing
             * can trigger it until the parent opens again. */
            EventDelCondition(id_tbl[i]);
            SetEventState(id_tbl[i], EV_STATE_SUSPEND);
            break;                                                      /* 306 */

        case EV_STATE_INIT:
        case EV_STATE_EXE:
        case EV_STATE_WAIT_END:
            /* Already running: leave it alone, but push the same treatment
             * down to its own sub-events. */
            EventEnd_SubStateChange(id_tbl[i]);
            break;                                                      /* 312 */

        default:
            PRINT_ASSERT("ERROR!! EventEnd_SubStateChange() event id = %d, sub event id = %d\n", event_id, id_tbl[i]);   /* 319 */
            /* fall through */
        case EV_STATE_SUSPEND:
        case EV_STATE_END:
        case EV_STATE_LOCK:
            break;
        }
    }                                                                   /* 322 */
}

void SetEventExeCtrl(int event_id, u_char use_table)
{
    int empty_area;

    empty_area = GetExeCtrlEmpty(event_id);                             /* 340 */

    if (empty_area >= 0) {                                              /* 344 */
        EventExeCtrlInit(&ev_exe_ctrl[empty_area], event_id,
                         EvGetExeAddr(use_table, event_id));
    } else if (empty_area == -1) {                                      /* 348 */
        /* -2 (already running) is the quiet case; only a genuinely full
         * list is worth shouting about. */
        printf("**************************************************************\n"); /* 349 */
        printf("*             ERROR!! EVENT EXE CTRL NO EMPTY!!!             *\n"); /* 350 */
        printf("*  The event cannot be started at the same time any further  *\n"); /* 351 */
        printf("**************************************************************\n"); /* 352 */
        PRINT_ASSERT("ERROR!! EVENT EXE CTRL NO EMPTY!!!");             /* 353 */
    }
}

/* Slot SetEventExeCtrl() should write: >= 0 is a free index, -1 means the
 * list is full, -2 means event_id is already on it (so it must not be
 * entered twice). */
static int GetExeCtrlEmpty(int event_id)
{                                                                       /* 367 */
    int empty;
    int i;

    empty = -1;                                                         /* 372 */

    for (i = 0; i < EV_EXE_CTRL_MAX; i++) {                             /* 376 */
        if (ev_exe_ctrl[i].event_id == event_id) {
            empty = -2;                                                 /* 378 */
            break;
        }
    }                                                                   /* 381 */

    if (empty != -2) {                                                  /* 383 */
        for (i = 0; i < EV_EXE_CTRL_MAX; i++) {                         /* 385 */
            if (ev_exe_ctrl[i].event_id == -1) {
                empty = i;                                              /* 388 */
                break;
            }
        }                                                               /* 391 */
    }

    return empty;                                                       /* 395 */
}

void EventExe(void)
{                                                                       /* 409 */
    int i;

    for (i = 0; i < EV_EXE_CTRL_MAX; i++) {                             /* 415 */
        if (ev_exe_ctrl[i].event_id != -1) {
            EventExeFuncCall(&ev_exe_ctrl[i]);
        }
    }                                                                   /* 424 */
}

void EventExeRelease(int event_id)
{                                                                       /* 441 */
    int i;

    for (i = 0; i < EV_EXE_CTRL_MAX; i++) {                             /* 446 */
        if (ev_exe_ctrl[i].event_id == event_id) {
            EventExeCtrlInit(&ev_exe_ctrl[i], -1, (u_char *)0);
            return;                                                     /* 450 */
        }
    }
}

int CheckEventExeEntry(int event_id)
{                                                                       /* 464 */
    int entry_res;

    entry_res = -1;                                                     /* 469 */

    for (int i = 0; i < EV_EXE_CTRL_MAX; i++) {                         /* 472 */
        if (ev_exe_ctrl[i].event_id == event_id) {
            entry_res = 1;                                              /* 475 */
            break;
        }
    }                                                                   /* 478 */

    return entry_res;                                                   /* 481 */
}

void SetSave_EvExeCtrl(MC_SAVE_DATA *data)
{                                                                       /* 492 */
    /* PORT: the ROM hard-codes 0x960 (150 * 0x10).  EV_EXE_CTRL carries a
     * live pointer, so the entry is wider on a 64-bit host and the literal
     * would describe only two thirds of the block.  Same deviation as
     * SetSave_EvCtrlCenter() in ev_open.c. */
    data->size = sizeof(EV_EXE_CTRL) * EV_EXE_CTRL_MAX;                                   /* 496 */
    data->addr = (u_char *)&ev_exe_ctrl[0];
}

/* PORT DEVIATION -- no ROM counterpart.
 *
 * event_addr is a live pointer into the event macro pak, and this block goes
 * into the save file verbatim.  On the PS2 that is fine: the pak is always at
 * EVENT_DATA_ADDR, so the saved pointer is still valid after the file is read
 * back.  On the host the pak lives in the emulated EE RAM, which is an
 * ordinary allocation whose base differs in every process -- so a pointer
 * saved in one run and restored in the next is stale by that base delta, and
 * the interpreter runs off the end of the world.  (The observed failure was
 * EvPushPad() reading pad_label 0x7b00 out of a restored cursor.)
 *
 * So the pointer is converted to the EE address the ROM would have stored on
 * the way out and back to a host pointer on the way in.  The file then holds
 * exactly what the PS2's file held.  Called from mc_set_data.c around the two
 * marshalling loops; to_host == 0 converts for writing, 1 after reading. */
void EvExeCtrlSavePtrFixup(int to_host)
{
    for (int i = 0; i < EV_EXE_CTRL_MAX; i++) {
        u_char *p = ev_exe_ctrl[i].event_addr;

        if (p == nullptr) {
            continue;
        }

        if (to_host != 0) {
            ev_exe_ctrl[i].event_addr =
                (u_char *)MioPan_GetHostPointer((uintptr_t)p);
        } else {
            uintptr_t ee = MioPan_GetPs2Address(p);

            /* 0 means "not in emulated RAM" -- leave anything unrecognised
             * alone rather than zeroing a pointer we do not understand. */
            if (ee != 0) {
                ev_exe_ctrl[i].event_addr = (u_char *)ee;
            }
        }
    }
}
