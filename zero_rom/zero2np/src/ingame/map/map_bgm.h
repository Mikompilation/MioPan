/* ==========================================================================
 *  ingame/map/map_bgm.h
 *
 *  Per-room background music.  A per-room table (map_bgm_tbl[]) gives every
 *  room its default BGM stream file; map_bgmMain() polls the
 *  player's current room each frame and switches the stream when it changes.
 *  Events can override a room's entry at runtime (map_bgmChangeTbl(), used by
 *  the EvMapStreamChange macro) and temporarily silence the room music while
 *  something else plays over it (map_bgmFadeOut()/map_bgmFadeIn(), used by
 *  EvMapStreamStop/EvMapStreamPlay).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), map_bgm.o
 *  0x001d8ce0..0x001d9260.
 * ======================================================================== */

#ifndef _INGAME_MAP_MAP_BGM_H
#define _INGAME_MAP_MAP_BGM_H

#include "eetypes.h"
#include "../../common/save_data.h"          /* MC_SAVE_DATA */

void map_bgmInit(void);

/* iStrFileNo is a CD_FILE_DAT stream index.  Below DANMATU_MALE_HXD (2025) it
 * means "no override -- fall back to map_bgm_tbl[iRoomID]"; at or above
 * BGM_END_DMY (3086) the value is out of range and the entry resets the same
 * way.  In between it replaces the room's stream file until the next load. */
void map_bgmChangeTbl(int iRoomID, int iStrFileNo);

void map_bgmMain(void);
void map_bgmFadeOut(int fade_time, int target_vol);
void map_bgmRelease(int fade_out_time);
void map_bgmFadeIn(int fade_time);

/* Hands the save system the whole 0x3c4-byte aMapWrkBGMTbl/disable_cnt block
 * (MAP_BGM_SAVE) verbatim, the same pattern SetSave_DoorCtrl() uses for the
 * door lock table. */
void map_bgmSetSave(MC_SAVE_DATA *data);

#endif /* _INGAME_MAP_MAP_BGM_H */
