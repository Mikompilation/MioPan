/* ==========================================================================
 *  system/mc/dat/save_data.h
 *
 *  The four save manifests: NULL-terminated lists of "describe one block of
 *  live game state" callbacks.  SetMemoryCardSaveDataInfo() walks the list for
 *  a (dir_label, file_label) pair, calls each entry with an MC_SAVE_DATA slot,
 *  and ends up with an address/size table describing the whole card file.
 *
 *  Not to be confused with common/save_data.h, which is where MC_SAVE_DATA
 *  itself lives; the ROM has both files under those names.
 *
 *  Data-only: save_data.o's .text holds nothing but the fixed_array template
 *  boilerplate that every translation unit carries.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_DAT_SAVE_DATA_H
#define _SYSTEM_MC_DAT_SAVE_DATA_H

#include "../../../common/save_data.h"          /* MC_SAVE_DATA */

/* Port's name for the entry type -- the ROM's own typedef is not recoverable
 * from the debug info, which reports these tables as bare arrays of
 * `void (*)()`. */
typedef void (*MC_SET_SAVE_FUNC)(MC_SAVE_DATA *data);

/* game_file_name[0] -- "Zero2System": options plus the clear flags, the two
 * things that survive a new game. */
extern MC_SET_SAVE_FUNC save_system_data[3];    /* data 33e3b0 */

/* game_file_name[1] -- the play-data header the load screen reads first, to
 * fill in the five slot summaries without touching the slots themselves. */
extern MC_SET_SAVE_FUNC save_play_data_head[2]; /* sdata 3f3c10 */

/* game_file_name[2..6] -- a whole save slot: 49 blocks covering the player,
 * the sister, every event and enemy work area, the camera, the map, and the
 * mission-mode records.  All five slots share this one manifest. */
extern MC_SET_SAVE_FUNC save_game_data[50];     /* data 33e3c0 */

/* albumN_file_name[0] -- a photo album: its info block and its picture pages. */
extern MC_SET_SAVE_FUNC save_album_data[3];     /* data 33e488 */

#endif /* _SYSTEM_MC_DAT_SAVE_DATA_H */
