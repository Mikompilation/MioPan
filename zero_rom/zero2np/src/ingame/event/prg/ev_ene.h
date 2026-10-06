/* ==========================================================================
 *  ingame/event/prg/ev_ene.h
 *
 *  Per-room enemy residency (ev_ene.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_ENE_H
#define _INGAME_EVENT_PRG_EV_ENE_H

#include "../../../common/save_data.h"

/* One resident enemy: which enemy program and which of its data sets. */
typedef struct _ENE_DATS            /* 0x8 */
{
    /* 0x0 */ int ene_type;
    /* 0x4 */ int dat_no;
} ENE_DATS;

typedef struct _ENE_LOAD_DATS       /* 0x8 */
{
    /* 0x0 */ ENE_DATS dats;
} ENE_LOAD_DATS;

void ev_eneInit(void);
void ev_eneSetSave(MC_SAVE_DATA *save);
void ev_eneRelease(void);
void ev_eneRegisterFile(int room_id, int ene_type, int dat_no);
void ev_eneDeleteFile(int room_id, int ene_type, int dat_no);
void ev_eneChangeRoom(int room_id);
int  ev_eneIsReady(void);

#endif /* _INGAME_EVENT_PRG_EV_ENE_H */
