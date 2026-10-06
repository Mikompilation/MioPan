/* ==========================================================================
 *  ingame/map/foot_se.h
 *
 *  Footstep sound banks.  A room declares which surface sounds it needs; this
 *  module keeps up to WRK_MAX of them resident and plays one whenever the
 *  animation code fires a footstep.
 *
 *  The chain is: MapLoadRegistReq() calls foot_seSetRoom() with the room that
 *  is going away and the one arriving, foot_se.c works out the union of the
 *  two surface lists, and foot_seSetNewFiles() releases what neither room
 *  wants and claims what is missing.  At play time MrecSetSEInfo() /
 *  MrecGetSeNo() (map_rectangle.o) say which surface the foot landed on.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), foot_se.o.
 * ======================================================================== */

#ifndef _INGAME_FOOT_SE_H
#define _INGAME_FOOT_SE_H

void foot_seInit(void);
void foot_seRelease(void);

/* Both ids are room indices -- (labelID - RY00_PK2) / 5 -- and index
 * se_footDatList[] directly. */
void foot_seSetRoom(int room_id_new, int room_id_now);

/* Exported, but foot_seSetRoom() is its only caller in the whole ROM.
 * `file_array` holds CD file numbers, already mapped through
 * foot_se_label_tbl[]; entries of -1 are skipped. */
void foot_seSetNewFiles(const int *file_array, int file_num);

/* `vol` and `pitch` are the animation code's nominal values; both get a random
 * spread here so repeated steps do not sound identical. */
void foot_sePlay(float *pos, int vol, int pitch);

#endif /* _INGAME_FOOT_SE_H */
