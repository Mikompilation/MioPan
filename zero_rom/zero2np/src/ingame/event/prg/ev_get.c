// FILE: /home/zero_rom/zero2np/src/ingame/event/prg/ev_get.c
//
// Event-binary readers.  The packed-stream primitives (Get1/2/4Byte,
// EvBinChangeAddr4) plus the lookups that index the event macro pak --
// EVENT_OBJ, file 0xd35, which EventDataLoadReq() pulls to EVENT_DATA_ADDR.
// The pak's table layout is documented in ev_get.h.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "ev_get.h"

#include <string.h>

#include "ev_main.h"                        // ev_wrk
#include "../../plyr/unit_ctl.h"            // RotLimitChk
#include "../../../common/utility2.h"       // PRINT_ASSERT
#include "../../../miopan/miopan_memory.h"  // MioPan_GetHostPointer
#include "../../../system/os/system.h"      // EVENT_DATA_ADDR

/* Terminator of the parent / sub-event id lists (tables 5 and 6). */
#define EV_ID_END   0xffffffff

/* The ROM keeps this in .lit4 (0x3ee25c) for the degrees->radians scale. */
#define EV_GET_PI   3.1415927f

/* PORT: the ROM reads the macro pak straight off its fixed EE address
 * (0xd4ec00).  On the host the loader writes it into the emulated RAM block
 * instead, so the base has to be translated.  MioPan_GetHostPointer() is
 * idempotent by range check, so evaluating this per use costs nothing but a
 * range test and keeps the ROM's local variable set intact. */
#define EV_MACRO_TOP  ((u_char *)MioPan_GetHostPointer(EVENT_DATA_ADDR))

u_char GetEvState(int event_id)
{                                                                    /* 38 */
    /* fixed_array::operator[] supplies the bounds check the ROM inlines here
     * as _fixed_array_verifyrange<EVENT_STATE>(event_id, 1931). */
    return ev_wrk.ev_state[event_id].state;                          /* 46 */
}

u_char GetEvWrkWaitFlg(void)
{
    return ev_wrk.wait_flg;                                          /* 58 */
}

u_char *EvBinChangeAddr4(u_char *top_addr, u_char *dat_addr)
{                                                                    /* 77 */
    /* The stored value is a byte offset from the stream base, not a pointer. */
    return top_addr + Get4Byte(dat_addr);                        /* 82, 88 */
}

u_char *EvGetTblAddr(int tbl_type)
{                                                                    /* 97 */
    u_char *tbl_addr;

    if ((u_int)tbl_type < EV_TBL_MAX) {                              /* 108 */
        tbl_addr = EvBinChangeAddr4(EV_MACRO_TOP,
                                    EV_MACRO_TOP + tbl_type * 4);   /* 110, 112 */

        /* PORT: the ROM stops here -- on the EE the macro pak is resident from
         * boot, so the header word is always a real offset.  The host reaches
         * this before EventDataLoadReq() has run in some phases, and the
         * emulated RAM is zero-filled, which would hand every caller a pointer
         * at the pak header and let them parse zeroes as event programs.
         *
         * Offset 0 is unambiguous as "not loaded": in EVENT_OBJ the smallest
         * table offset is 0x1c and the smallest per-event stream offset is
         * 0xd350, so no valid entry is ever 0.  Answering NULL is the same
         * "no table" result the ROM's own out-of-range path produces, and
         * every caller already handles it. */
        if (tbl_addr == EV_MACRO_TOP) {
            tbl_addr = (u_char *)0;
        }
    } else {
        tbl_addr = (u_char *)0;                                      /* 116 */
    }

    return tbl_addr;                                                 /* 120 */
}

u_char *EvGetExeAddr(int tbl_type, int ev_no)
{                                                                    /* 128 */
    u_char *tbl_addr;
    u_char *exe_addr;

    /* Range-checked twice: EvGetTblAddr() repeats the test.  Faithful. */
    if ((u_int)tbl_type < EV_TBL_MAX) {                              /* 138 */
        tbl_addr = EvGetTblAddr(tbl_type);                           /* 140 */
    } else {
        tbl_addr = (u_char *)0;                                      /* 143 */
    }

    if (tbl_addr != (u_char *)0) {                                   /* 147 */
        exe_addr = EvBinChangeAddr4(EV_MACRO_TOP,
                                    tbl_addr + ev_no * 4);           /* 154 */
    } else {
        exe_addr = (u_char *)0;                                      /* 157 */
    }

    return exe_addr;                                                 /* 160 */
}

/* Walk event_id's parent list and return its last entry -- the immediate
 * parent.  The root event's list is empty, so it keeps the initial -1. */
int EvGetParentID(int event_id)
{                                                                    /* 168 */
    int     parent_id;
    u_char *parent_addr;

    parent_id   = -1;
    parent_addr = EvGetExeAddr(EV_TBL_PARENT_ID, event_id);          /* 179 */

    /* PORT: EvGetExeAddr() cannot answer NULL on the EE (table 5 always
     * exists); on the host it does when the macro pak is not resident. */
    if (parent_addr == (u_char *)0) {
        return parent_id;
    }

    while (Get4Byte(parent_addr) != EV_ID_END) {                     /* 182 */
        parent_id    = (int)Get4Byte(parent_addr);                   /* 184 */
        parent_addr += 4;                                            /* 185 */
    }                                                                /* 186 */

    return parent_id;                                                /* 189 */
}

void EvSetSubId(u_char *dat_addr, int *id_tbl)
{                                                                    /* 197 */
    int   i;
    u_int ev_id;

    /* Clear first: callers iterate all EV_SUB_ID_MAX slots and read -1 as
     * "empty", so the tail past the stream has to be filled even when the
     * stream itself is short. */
    for (i = EV_SUB_ID_MAX - 1; i >= 0; i--) {                       /* 203 */
        id_tbl[i] = -1;                                              /* 204 */
    }                                                                /* 205 */

    /* PORT: null stream -- see EvGetParentID().  The table is already cleared,
     * which is the answer callers want for "this event has no sub-events". */
    if (dat_addr == (u_char *)0) {
        return;
    }

    ev_id = Get4Byte(dat_addr);                                      /* 206 */
    dat_addr += 4;

    i = 0;                                                           /* 210 */

    while (ev_id != EV_ID_END) {
        if (i >= EV_SUB_ID_MAX) {                                    /* 212 */
            PRINT_ASSERT("ERROR!! SubEventId MAX OVER!!  EvSetSubId()\n"); /* 214 */
            break;                                                   /* 216 */
        }
        i++;

        *id_tbl++ = (int)ev_id;                                      /* 218 */
        ev_id     = Get4Byte(dat_addr);                              /* 219 */
        dat_addr += 4;                                               /* 220 */
    }
}

void EvGetSubId(int event_no, int *id_tbl)
{                                                                    /* 228 */
    u_char *dat_addr;

    dat_addr = EvGetExeAddr(EV_TBL_SUB_ID, event_no);                /* 232 */
    EvSetSubId(dat_addr, id_tbl);                                    /* 233 */
}

/* Event data is packed, so nothing in a stream is guaranteed aligned.  The
 * ROM copies the bytes through a stack slot (Get2Byte) or uses the unaligned
 * lwl/lwr pair (Get4Byte) for exactly that reason; memcpy is the portable
 * equivalent and folds to a single load wherever alignment allows. */

u_char Get1Byte(u_char *dat_addr)
{
    return *dat_addr;                                               /* 261 */
}

u_short Get2Byte(u_char *dat_addr)
{
    u_short dat;

    dat = 0;                                                        /* 273 */
    memcpy(&dat, dat_addr, sizeof(u_short));                        /* 277 */

    return dat;                                                     /* 284 */
}

u_int Get4Byte(u_char *dat_addr)
{
    u_int dat;

    dat = 0;                                                        /* 296 */
    memcpy(&dat, dat_addr, sizeof(u_int));                          /* 300 */

    return dat;                                                     /* 309 */
}

float EvGetRot360(short int rot360)
{                                                                    /* 321 */
    float rot;

    rot = ((float)rot360 / 180.0f) * EV_GET_PI;                      /* 326 */
    RotLimitChk(&rot);                                               /* 330 */

    return rot;                                                      /* 333 */
}
