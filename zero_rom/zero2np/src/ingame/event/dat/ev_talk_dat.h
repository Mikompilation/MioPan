/* ==========================================================================
 *  ingame/event/dat/ev_talk_dat.h
 *
 *  Declaration for the one object ev_talk_dat.c defines.  The ROM has no
 *  header recorded for this file (ZERO2.MAP only tracks translation units),
 *  so this mirrors the source-tree layout: the .c sits in event/dat/, and so
 *  does its header.  ev_talk.c is the only consumer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_DAT_EV_TALK_DAT_H
#define _INGAME_EVENT_DAT_EV_TALK_DAT_H

#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* reference_fixed_array */

/* Message type each of the 8 talk tables prints through.  Indexed by the
 * tbl_id every ev_talk.c entry point takes; the value is the msg_type
 * argument of PrintMsgDef_W(), which selects the text bank and, through
 * SetMsgWinDefData(), the window style the line is drawn in. */
extern reference_fixed_array<int, 8> talk_info;             /* sdata 3f0628 */

#endif /* _INGAME_EVENT_DAT_EV_TALK_DAT_H */
