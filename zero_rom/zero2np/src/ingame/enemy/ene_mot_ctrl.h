/* ==========================================================================
 *  ingame/enemy/ene_mot_ctrl.h
 *
 *  The ghost animation event track.  A clip can carry per-frame events --
 *  open or close a shutter-chance window, hide the spirit gauge, fire a
 *  sound -- and EneMotAlgCtrl() applies whichever of them the clip stepped
 *  over since the previous frame.
 *
 *  ENE_MOT_WRK / ENE_MOT_CTRL are the ROM's (types.txt); the original
 *  declared them in the .c, since ene_mot_ctrl.o is the only object that
 *  instantiates their type_info.  They are here because SetEneMotAttr()
 *  takes one by pointer and enemy.c has to see the declaration.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), ene_mot_ctrl.o.
 * ======================================================================== */

#ifndef _INGAME_ENEMY_ENE_MOT_CTRL_H
#define _INGAME_ENEMY_ENE_MOT_CTRL_H

#include "eetypes.h"

struct ENE_WRK;

/* How many animation paks the character table covers.  Indexed by
 * ENE_DAT_COMMON::anm_no, so this is an anm-pak count and not a model count. */
#define ENE_MOT_CHAR_MAX 63

/* One event on a clip's track (ROM 0x8).  A track is an array of these in
 * ascending `frm` order, terminated by frm == -1.
 *
 *  attr  effect                                     `sub`
 *   0    clear ENE_WRK::st.sta 0x1000  -- shutter-chance window closes
 *   1    set   ENE_WRK::st.sta 0x1000  -- shutter-chance window opens
 *   2    clear ENE_WRK::st.sta 0x2000  -- fatal-frame window closes
 *   3    set   ENE_WRK::st.sta 0x2000  -- fatal-frame window opens
 *   4    clear ENE_WRK::st.sta 0x80000 -- spirit gauge shown again
 *   5    set   ENE_WRK::st.sta 0x80000 -- spirit gauge suppressed
 *   6    play a sound from the ghost's own bank                sound number
 *
 * The two window bits are the pair ShutterChanceChk() reads out of sta_old:
 * 0x2000 together with 0x80 (inside the ring) is the fatal-frame shot, 0x1000
 * with 0x80 an ordinary chance. */
struct ENE_MOT_WRK                      /* 0x8 */
{
    /* 0x0 */ u_int attr;
    /* 0x4 */ short frm;
    /* 0x6 */ short sub;
};

/* Per-ghost-slot playback cursor (ROM 0x4): what clip was playing last frame
 * and how far into it, so the next call knows which events are new. */
struct ENE_MOT_CTRL                     /* 0x4 */
{
    /* 0x0 */ short old_mot;
    /* 0x2 */ short old_frm;
};

/* Reset a slot's cursor.  Exported but never called anywhere in the ROM. */
void InitEneMotAlgCtrl(ENE_WRK *ew);

/* Apply the events the ghost's current clip crossed this frame.  EneRule()
 * runs it after the algorithm step, and unlike the algorithm it is not gated
 * on the "no algorithm" status bits. */
void EneMotAlgCtrl(ENE_WRK *ew);

/* Drop all three bits SetEneMotAttr() can raise.  Exported but never called
 * anywhere in the ROM. */
void ClearEneMotAttr(ENE_WRK *ew);

/* Apply one track event to the ghost. */
void SetEneMotAttr(ENE_WRK *ew, ENE_MOT_WRK *emw);

#endif /* _INGAME_ENEMY_ENE_MOT_CTRL_H */
