/* ==========================================================================
 *  system/mc/dat/mc_title_dat.h
 *
 *  Names the memory-card browser and the card filesystem see: directory
 *  names, file names, icon file names, and the two-line title the browser
 *  shows.  All of it is data-only (mc_title_dat.o has no .text).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_DAT_MC_TITLE_DAT_H
#define _SYSTEM_MC_DAT_MC_TITLE_DAT_H

/* Directory names, indexed by dir_label.  0 is the game data directory and
 * 1..5 the five photo albums.  The "BE" prefix is the PAL region code, so
 * these are exactly what the PS2 browser lists. */
extern char *game_dir_name[6];                  /* data 31e3c8 */

/* File names inside dir_label 0, indexed by file_label:
 *   0  system data (options, clear flags)
 *   1  the play-data header -- same name as the directory itself
 *   2..6  the five save slots
 * Note [1]: the header file is deliberately named after the directory, which
 * is the PS2 convention for the entry the browser treats as the save's icon
 * owner.  It is game_dir_name[0], not a string of its own. */
extern char *game_file_name[7];                  /* data 31e3e0 */

/* One-entry file-name tables for the album directories, the parallel of
 * game_file_name[] for dir_label 1..5.  MemoryCardSetFilePath() picks between
 * them with a switch on dir_label rather than a table of tables. */
extern char *album1_file_name[1];                /* sdata 3f1b40 */
extern char *album2_file_name[1];                /* sdata 3f1b48 */
extern char *album3_file_name[1];                /* sdata 3f1b50 */
extern char *album4_file_name[1];                /* sdata 3f1b58 */
extern char *album5_file_name[1];                /* sdata 3f1b60 */

/* Icon file names, [dir_label][icon_type] where icon_type is 0 view, 1 copy,
 * 2 delete.  All three are the same file per directory -- the game ships one
 * icon and lets icon.sys point every state at it. */
extern char mc_icon_name[6][3][31];              /* rdata 3bbd60 */

/* Browser title, Shift-JIS full-width: "Project Zero 2".  icon.sys's OffsLF
 * is 28, which breaks the line right after it, so the sub-title below lands on
 * the browser's second row. */
extern char mc_icon_title[29];                   /* rdata 3bbf90 */

/* Second title row per directory, Shift-JIS full-width: "GameData",
 * "AlbumData1" .. "AlbumData5". */
extern char mc_icon_sub_title[6][64];            /* rdata 3bbfb0 */

/* CD file numbers for the icon models, [dir_label][icon_type].  GetIconDataSize()
 * turns these into byte sizes through GetFileSize(). */
extern int icon_data_label[6][3];                /* rdata 3bc130 */

#endif /* _SYSTEM_MC_DAT_MC_TITLE_DAT_H */
