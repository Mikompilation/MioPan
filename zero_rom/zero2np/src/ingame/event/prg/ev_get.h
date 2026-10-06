/* ==========================================================================
 *  ingame/event/prg/ev_get.h
 *
 *  Event-binary readers (ev_get.c).  Two groups of entry points:
 *
 *    * stream primitives -- fetch a byte / half-word / word out of packed
 *      event data, and relocate a 4-byte packed offset against a stream base.
 *      The message system's control-code interpreter and the open-condition
 *      evaluator both walk event data through these.
 *
 *    * macro-pak lookups -- reach the seven per-event tables inside the event
 *      macro pak (EVENT_OBJ, file 0xd35) that EventDataLoadReq() pulls to
 *      EVENT_DATA_ADDR, plus the parent / sub-event id lists two of them hold.
 *
 *  Macro-pak layout, measured in EVENT_OBJ:
 *
 *      +0x00                u_int tbl_off[EV_TBL_MAX]
 *      tbl_off[t] + ev*4    u_int stream_off          one per event id
 *
 *  Every stored word is a byte offset from the pak base -- which is exactly
 *  what EvBinChangeAddr4() adds it to.  Each table carries one entry per event
 *  id, and the table count is EVENT_STATE_MAX (1931), so the seven tables are
 *  7*4 + 7*1931*4 bytes of header before the first stream.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_GET_H
#define _INGAME_EVENT_PRG_EV_GET_H

#include "eetypes.h"

/* Which per-event table EvGetTblAddr() / EvGetExeAddr() should index.  Both
 * range-check against EV_TBL_MAX and answer NULL above it.  0/1/3 come from
 * ev_open.c and ev_exe.c's init path; 2 and 4 are pinned by the use_table
 * argument SetEventExeStatus() / SetEventEndExeStatus() pass down to
 * SetEventExeCtrl(); 5 and 6 by EvGetParentID() / EvGetSubId() below. */
#define EV_TBL_OPEN_COND   0
#define EV_TBL_INIT_PRG    1
#define EV_TBL_EXE_PRG     2
#define EV_TBL_CLOSE_COND  3
#define EV_TBL_END_PRG     4
#define EV_TBL_PARENT_ID   5
#define EV_TBL_SUB_ID      6
#define EV_TBL_MAX         7

/* Capacity of the sub-event id table EvGetSubId() fills. */
#define EV_SUB_ID_MAX      50

u_char  Get1Byte(u_char *dat_addr);
u_short Get2Byte(u_char *dat_addr);
u_int   Get4Byte(u_char *dat_addr);
u_char *EvBinChangeAddr4(u_char *top_addr, u_char *dat_addr);

/* Start of table tbl_type inside the macro pak, or NULL when tbl_type is out
 * of range (or the pak is not resident -- see ev_get.c). */
u_char *EvGetTblAddr(int tbl_type);

/* Start of event ev_no's `tbl_type` program, or NULL when it has none. */
u_char *EvGetExeAddr(int tbl_type, int ev_no);

/* Last id in event_id's parent list, or -1 when it has no parent. */
int     EvGetParentID(int event_id);

/* Fills id_tbl[EV_SUB_ID_MAX] from a -1 terminated id stream; unused slots
 * hold -1. */
void    EvSetSubId(u_char *dat_addr, int *id_tbl);

/* Fills id_tbl[EV_SUB_ID_MAX] with event_no's sub-event ids; unused slots
 * hold -1. */
void    EvGetSubId(int event_no, int *id_tbl);

u_char  GetEvState(int event_id);
u_char  GetEvWrkWaitFlg(void);

/* Degrees (as authored in the event macros) to radians, wrapped to (-PI, PI]. */
float   EvGetRot360(short int rot360);

#endif /* _INGAME_EVENT_PRG_EV_GET_H */
