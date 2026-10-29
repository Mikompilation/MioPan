#ifndef MIOPAN_CDTABLE_H
#define MIOPAN_CDTABLE_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Which release of the game the data folder holds, and the CD table to match.
 *
 * system.c's cd_dat_tbl is the Feb 2004 prototype's, as recovered from its
 * SLES_523.84.  The EU retail release has the same 4491 rows in the same
 * order -- the same files present, the same ones packed, the same
 * file_ext_tbl -- so every name and CD_FILE_DAT number holds for both.  What
 * moved, measured file by file against the retail disc:
 *
 *   * where each file sits in IMG_BD.BIN: every start sector;
 *   * 58 textures under data/tim packed to other sizes, and 4 more whose
 *     bytes differ at the same size;
 *   * 12 files of other sizes: the five msg*.obj (the text), s1020.pss and
 *     s1030.pss (encoded again), and the five option_top*.pk2;
 *   * 350 sound headers re-mixed: four words of each .hxd.  No recording
 *     changed -- every .str and .bd body is the prototype's.
 *
 * 3212 of the 3636 files are byte for byte the prototype's.
 * miopan_cdtable_eu_retail.inc holds the retail rows, generated from the disc
 * by tools/cd_table_from_disc.py; its executable is stripped, with neither
 * symbols nor names, so the names stay the prototype's.
 *
 * The retail rows are the default: they are what a player's disc has.  The
 * prototype's stay for its own IMG_BD.BIN, the one the reconstruction was
 * made against.
 */

/* Pick the rows for the files the data folder holds and write them into
 * `tbl`, a CD_DAT_TBL[] of the prototype's `rom_file_num` rows.  `names` (a
 * FNAME_DAT[]) and `paths` (the directories FNAME_DAT::path_no selects) name
 * them.  Called once at boot, before anything reads the table. */
void MioPan_CdTableSelect(void *tbl, const void *names, char **paths, int rom_file_num);

/* The release the table is for, once selected: "EU retail" or "prototype". */
const char *MioPan_CdTableRelease(void);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_CDTABLE_H */
