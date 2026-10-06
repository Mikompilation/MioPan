/* ==========================================================================
 *  iop_reverb.h  (SPU2 reverb — PC-port implementation)
 *
 *  The effect unit iop_libsd.cpp used to discard.  One instance per SPU2 core:
 *  a comb/all-pass network running out of a work area at the top of SPU RAM,
 *  fed by the voices whose SD_S_VMIXEL / SD_S_VMIXER bit is set and returned
 *  to the mix scaled by SD_P_EVOLL / SD_P_EVOLR.
 *
 *  The ROM drives all of this already and always has -- map_reverb.c carries a
 *  66-entry per-area depth table and re-applies it on every area change, and
 *  iopSndInit() sizes and clears the work area at boot.  Only the DSP was
 *  missing, so every interior in the game has been playing anechoic.
 *
 *  Fatal Frame uses exactly one preset: SndSetEffect() is called with mode 3
 *  from map_reverb.c, outgame.c and SndInit(), and with mode 0 (off) nowhere.
 *  The other nine are here because the ROM's own eff_use_size_tbl[] enumerates
 *  all ten and SndSetEffect() would accept any of them.
 * ======================================================================== */

#ifndef _SDK_IOP_REVERB_H
#define _SDK_IOP_REVERB_H

#include "scetypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#define IOP_REVERB_CORES    2

/* The reverb runs at half the output rate, as it does on hardware (22.05 kHz
 * against the PS1's 44.1; 24 kHz against SPU2's 48).  The mixer therefore
 * ticks it every other output frame -- see MioPan_ReverbTick(). */
#define IOP_REVERB_RATE_DIV 2

void MioPan_ReverbInit(void);

/* sceSdSetEffectAttr().  `eea` is the core's SD_A_EEA, which the ROM writes
 * immediately before and which is the *last byte* of the work area (spu_mem.c
 * hands out `final_end_adrs - 1`); the start is derived from the preset size.
 * Passing mode 0 leaves the core with no preset. */
void MioPan_ReverbSetPreset(int core, int mode, u_int eea);

/* SD_P_EVOLL / SD_P_EVOLR: the return level, and the only thing map_reverb.c
 * varies from room to room. */
void MioPan_ReverbSetDepth(int core, u_short evoll, u_short evolr);

/* SD_C_EFFECT_ENABLE. */
void MioPan_ReverbSetEnable(int core, int on);

/* sceSdClearEffectWorkArea(): zero the buffer and the filter state.  The ROM
 * calls this before every preset change, and spins on it at boot. */
void MioPan_ReverbClearWorkArea(int core);

/* "Enabled, with a preset loaded and a work area that fits."  The mixer skips
 * the whole wet path when this is false, so a core the game never enables
 * costs nothing. */
int MioPan_ReverbIsActive(int core);

/* One 24 kHz step.  `in_l`/`in_r` are the summed sends at the mixer's internal
 * scale (s16-ish, but not clamped -- the send bus can exceed one voice); the
 * outputs are the wet return already scaled by EVOL.
 *
 * Call under MioPan_ReverbLock(): the parameter setters above run on the IOP's
 * threads while this runs on the audio thread. */
void MioPan_ReverbTick(int core, int in_l, int in_r, int *out_l, int *out_r);

/* Held across a whole mixed block rather than taken per tick.  Lock order is
 * voice mutex first, then this -- the mixer holds both, nothing else holds the
 * voice mutex under this one. */
void MioPan_ReverbLock(void);
void MioPan_ReverbUnlock(void);

#ifdef __cplusplus
}
#endif

#endif /* _SDK_IOP_REVERB_H */
