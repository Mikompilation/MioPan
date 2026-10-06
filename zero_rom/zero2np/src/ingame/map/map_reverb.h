/* ==========================================================================
 *  ingame/map/map_reverb.h
 *
 *  Per-room reverb depth.  A per-room table (map_reverb_tbl[]) gives every
 *  area its baked reverb depth; map_reverbMain() polls the player's current
 *  area each frame and re-applies SndSetEffect() when it changes.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_reverb.o
 *  0x001dee78..0x001df010.
 * ======================================================================== */

#ifndef _INGAME_MAP_MAP_REVERB_H
#define _INGAME_MAP_MAP_REVERB_H

void map_reverbInit(void);
void map_reverbMain(void);

/* Re-applies the current area's reverb depth right after a memory-card load
 * restores rev_room_id_save, since map_reverbMain()'s "unchanged, skip" test
 * would otherwise leave the GS effect at whatever the title screen left it. */
void map_reverbAfterMCLoadInit(void);

#endif /* _INGAME_MAP_MAP_REVERB_H */
