/* ==========================================================================
 *  outgame/outgame.h
 *
 *  Public interface for the "outgame" GPhase layer (outgame.c): the boot /
 *  attract flow that runs the UBI (publisher movie), Tecmo and Project Zero
 *  logo phases, and the OutGame_Main parent phase that stages every outgame
 *  screen's texture/data loads.
 *
 *  Only BackGroundLoadReq() is called from outside this module (loading.c /
 *  title.c); the per-phase init/one/end callbacks are wired into the GPhase
 *  registry by symbol and need external linkage but no consumer prototype.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_OUTGAME_H
#define _OUTGAME_OUTGAME_H

/* --------------------------------------------------------------------------
 *  Loading-screen background prep.  Queues the loading-screen texture and, on
 *  the first call, kicks the one-time ingame resource loads (IngameLoadOnce).
 * ------------------------------------------------------------------------ */
void BackGroundLoadReq(void);

#endif /* _OUTGAME_OUTGAME_H */
