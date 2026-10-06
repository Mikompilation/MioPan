// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_timer.c
//
// Event timers.
//
// ev_timer_ctrl.event_timer is a free-running frame counter that wraps at
// 400,000,000.  A registration does not store a countdown -- CalTimerFill()
// folds the requested delay onto that same modulus, so a slot holds the
// absolute counter value it should fire on and EvTimerExe() can test it with
// a plain ==.  That is exact as long as the counter only ever advances by one
// per tick, which is why EvTimerCount() resets to 0 rather than masking.
//
// The counter is frozen while an event is holding the game (GetEvWrkWaitFlg),
// so a delay is measured in frames of actual play; expiry is still checked
// every frame.  Note that a zero-frame delay therefore never matches: the
// counter has already moved past the registered value by the time
// EvTimerExe() next looks at it, and the slot sits there until the counter
// wraps all the way round.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_timer.h"

#include <stdio.h>                                  // printf (debug traces)

#include "ev_change.h"                              // Req_CompulsionSetEventState
#include "ev_get.h"                                 // GetEvWrkWaitFlg
#include "../../../graphics/graph3d/ctl/fixed_array.h"

/* Counter wrap.  The ROM spells both constants out: the test is against
 * EV_TIMER_LOOP - 1 and the fold-back subtracts EV_TIMER_LOOP. */
#define EV_TIMER_LOOP 400000000

typedef struct                      /* 0x1e4 */
{
    /* 0x000 */ u_int event_timer;
    /* 0x004 */ fixed_array<REGIST_TIMER, REGIST_TIMER_MAX> regist_timer;
} EV_TIMER_CTRL;

static void  RegistTimerInit(REGIST_TIMER *timer);
static void  EvTimerCount(void);
static void  EvTimerExe(void);
static int   GetEmptyRegistTimer(void);
static u_int CalTimerFill(u_int timer);

/* Counter and slots are one save block, so a pending timer survives a save
 * and reload -- which is the whole point of storing an absolute deadline. */
static EV_TIMER_CTRL ev_timer_ctrl;                                     /* bss 47bd80 */

void EvTimerCtrlInit(void)
{                                                                       /* 45 */
    int i;

    ev_timer_ctrl.event_timer = 0;                                      /* 50 */
    for (i = 0; i < REGIST_TIMER_MAX; i++) {                            /* 51 */
        RegistTimerInit(&ev_timer_ctrl.regist_timer[i]);
    }                                                                   /* 53 */
}

static void RegistTimerInit(REGIST_TIMER *timer)
{
    timer->event_id     = -1;                                           /* 66 */
    timer->timer        = 0;                                            /* 67 */
    timer->req_event_id = -1;                                           /* 68 */
    timer->req_state    = 0;                                            /* 69 */
}

void EvTimerMain(void)
{
    if (GetEvWrkWaitFlg() == 0) {                                       /* 89 */
        EvTimerCount();                                                 /* 90 */
    }

    EvTimerExe();                                                       /* 94 */
}

static void EvTimerCount(void)
{
    ev_timer_ctrl.event_timer++;                                        /* 107 */

    if (ev_timer_ctrl.event_timer > EV_TIMER_LOOP - 1) {                /* 110 */
        ev_timer_ctrl.event_timer = 0;                                  /* 111 */
    }
}

/* Fire every slot whose deadline is this exact tick. */
static void EvTimerExe(void)
{                                                                       /* 121 */
    int i;

    for (i = 0; i < REGIST_TIMER_MAX; i++) {                            /* 128 */
        if (ev_timer_ctrl.regist_timer[i].event_id != -1) {             /* 132 */

            if (ev_timer_ctrl.regist_timer[i].timer == ev_timer_ctrl.event_timer) { /* 134 */
                printf("TIMER COMPRETE!\n");                            /* 135 */

                Req_CompulsionSetEventState(ev_timer_ctrl.regist_timer[i].event_id,
                                            ev_timer_ctrl.regist_timer[i].req_event_id,
                                            ev_timer_ctrl.regist_timer[i].req_state); /* 137 */

                RegistTimerInit(&ev_timer_ctrl.regist_timer[i]);        /* 139 */
            }
        }
    }                                                                   /* 142 */
}

void EvTimerRegist(int event_id, u_int timer, int req_event_id, u_char state)
{
    int empty_area;

    empty_area = GetEmptyRegistTimer();                                 /* 165 */

    if (empty_area == -1) {                                             /* 169 */
        printf("ERROR!! TIMER REGIST NO EMPTY!!\n");                    /* 170 */
        return;
    }

    ev_timer_ctrl.regist_timer[empty_area].event_id     = event_id;     /* 175 */
    ev_timer_ctrl.regist_timer[empty_area].timer        = CalTimerFill(timer); /* 176 */
    ev_timer_ctrl.regist_timer[empty_area].req_event_id = req_event_id; /* 177 */
    ev_timer_ctrl.regist_timer[empty_area].req_state    = state;        /* 178 */
}

/* First free slot, or -1 when all 30 are taken. */
static int GetEmptyRegistTimer(void)
{                                                                       /* 190 */
    int empty_res;
    int i;

    empty_res = -1;                                                     /* 195 */

    for (i = 0; i < REGIST_TIMER_MAX; i++) {                            /* 199 */

        if (ev_timer_ctrl.regist_timer[i].event_id == -1) {
            empty_res = i;                                              /* 202 */
            break;
        }
    }                                                                   /* 205 */

    return empty_res;                                                   /* 208 */
}

/* Delay -> absolute deadline, folded back onto the counter's modulus so a
 * timer registered just before the wrap still compares equal. */
static u_int CalTimerFill(u_int timer)
{                                                                       /* 216 */
    u_int fill_time;

    fill_time = ev_timer_ctrl.event_timer + timer;                      /* 225 */

    printf("set time %d\n", timer);                                     /* 228 */
    printf("fill_time %d\n", fill_time);                                /* 229 */

    if (fill_time > EV_TIMER_LOOP - 1) {                                /* 232 */
        fill_time -= EV_TIMER_LOOP;                                     /* 233 */
    }

    return fill_time;                                                   /* 237 */
}

void EvTimerRelease(int event_id)
{                                                                       /* 249 */
    for (int i = 0; i < REGIST_TIMER_MAX; i++) {                        /* 255 */

        if (ev_timer_ctrl.regist_timer[i].event_id == event_id) {

            RegistTimerInit(&ev_timer_ctrl.regist_timer[i]);
        }
    }                                                                   /* 261 */
}

/* The save table in save_data.o collects this block by pointer; 0x1e4 is the
 * ROM's literal, and sizeof(EV_TIMER_CTRL) agrees on the host. */
void SetSave_EvTimerCtrl(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&ev_timer_ctrl;                              /* 278 */
    data->size = sizeof(ev_timer_ctrl);                                 /* 279 */
}
