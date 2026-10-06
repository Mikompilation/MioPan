/* ==========================================================================
 *  ingame/item/dat/file_dat.h
 *
 *  Texture ids for the collected files/notes (file_dat.o).  Pure data, exactly
 *  like item_dat.o: the object's whole .text is fixed_array template
 *  boilerplate pulled in by the header, so file_dat.c has no code of its own.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_DAT_FILE_DAT_H
#define _INGAME_ITEM_DAT_FILE_DAT_H

#include "eetypes.h"

/* types.txt.  Plain arrays in the ROM, not fixed_arrays -- file.c reaches them
 * through a static FILE_DAT *file_dat_tbl[5] and subscripts the result. */
typedef struct                      /* 0x4 */
{
    /* 0x0 */ int tex_id;
} FILE_DAT;

/* Row counts.  These are what GetFileTypeMaxNum() answers for types 0..4 and
 * what PLYR_FILE's per-type read-flag arrays are sized to. */
#define FILE_POCKETBOOK_MAX 42
#define FILE_SCRAP_MAX      42
#define FILE_OLDBOOK_MAX    40
#define FILE_PHOTOGRAPH_MAX 26
#define FILE_MAP_MAX        10

/* File type ids.  The ROM's debug info carries no enum for these; the
 * numbering is fixed by the order of file.c's static file_dat_tbl[5]
 * (data 312230), which holds the five tables below in exactly this order,
 * and it matches PLYR_FILE's member order and GetFileTypeMaxNum()'s switch. */
#define FILE_TYPE_POCKETBOOK 0
#define FILE_TYPE_SCRAP      1
#define FILE_TYPE_OLDBOOK    2
#define FILE_TYPE_PHOTOGRAPH 3
#define FILE_TYPE_MAP        4
#define FILE_TYPE_MAX        5

extern FILE_DAT file_pocketbook[FILE_POCKETBOOK_MAX];   /* data 312248 */
extern FILE_DAT file_scrap[FILE_SCRAP_MAX];             /* data 3122f0 */
extern FILE_DAT file_oldbook[FILE_OLDBOOK_MAX];         /* data 312398 */
extern FILE_DAT file_photograph[FILE_PHOTOGRAPH_MAX];   /* data 312438 */
extern FILE_DAT file_map[FILE_MAP_MAX];                 /* data 3124a0 */

#endif /* _INGAME_ITEM_DAT_FILE_DAT_H */
