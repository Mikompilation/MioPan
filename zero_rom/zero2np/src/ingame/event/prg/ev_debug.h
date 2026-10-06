/* ==========================================================================
 *  ingame/event/prg/ev_debug.h
 *
 *  Event-system debug console (ev_debug.c).
 *
 *  The whole object file is this one entry point: it polls four debug action
 *  keys to drive the dump routines that live with the subsystems they print,
 *  and puts the player's / sister's room-local position on the TTY.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_PRG_EV_DEBUG_H
#define _INGAME_EVENT_PRG_EV_DEBUG_H

/* Nothing in the prototype calls this -- the map records no code xref, only
 * the .eh_frame entry -- so it is an entry point kept alive for whoever was
 * patching a call in by hand.  The port calls it from EventMain(). */
void EvDbgMain(void);

/* Port addition: DEBUG MENU -> "EVENT DEBUG" switch, read by EventMain(). */
extern int dbg_event_debug;

#endif /* _INGAME_EVENT_PRG_EV_DEBUG_H */
