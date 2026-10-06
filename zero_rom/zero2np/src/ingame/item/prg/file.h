/* ==========================================================================
 *  ingame/item/prg/file.h
 *
 *  Collected files/notes inventory (file.o).  One state byte per (file_type,
 *  file_id) pair, held in PLYR_FILE and read out through the accessors here;
 *  the matching texture ids live in the static tables of file_dat.o.
 *
 *  Every entry point range-checks both indices and asserts.  Note the checks
 *  are *signed* -- `if (FILE_TYPE_MAX <= file_type)` -- so a negative index
 *  walks straight past the guard.  That is the ROM's behaviour, unlike item.o
 *  which casts to unsigned first.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_PRG_FILE_H
#define _INGAME_ITEM_PRG_FILE_H

#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../dat/file_dat.h"                            /* FILE_TYPE_* / counts */
#include "eetypes.h"

/* Per-file progress.  The ROM's debug info carries no enum; the names come
 * from what the three mutators do -- FileGet moves NONE -> HAVE, FileRead
 * moves HAVE -> READ, FileLost drops anything back to NONE, and
 * SetPlyrFileState asserts on anything above READ. */
#define FILE_STATE_NONE 0       /* not collected */
#define FILE_STATE_HAVE 1       /* collected, not yet read */
#define FILE_STATE_READ 2       /* collected and read */

/* The player's read flags, one array per file type.  Only file.c touches the
 * instance (it is `static plyr_file` at bss 47bfe0); the layout is here
 * because it is what SetSave_PlyrFile() hands the save system. */
typedef struct                      /* 0xa0 */
{
    /* 0x00 */ fixed_array<char, FILE_POCKETBOOK_MAX> pocketbook;
    /* 0x2a */ fixed_array<char, FILE_SCRAP_MAX>      scrap;
    /* 0x54 */ fixed_array<char, FILE_OLDBOOK_MAX>    oldbook;
    /* 0x7c */ fixed_array<char, FILE_PHOTOGRAPH_MAX> photograph;
    /* 0x96 */ fixed_array<char, FILE_MAP_MAX>        map;
} PLYR_FILE;

/* Reset every file of every type to FILE_STATE_NONE. */
void AllPlyrFileInit(void);

/* Collect a file.  Warns (does not assert) if it was already collected. */
void FileGet(int file_type, int file_id);

/* Drop a file back to uncollected.  Warns if it was not held. */
void FileLost(int file_type, int file_id);

/* Mark a collected file as read.  Always returns 1 -- the ROM's `res` local
 * is initialised to 1 and never reassigned on any path. */
int  FileRead(int file_type, int file_id);

/* Texture id for a file, out of the file_dat.o tables. */
int  GetFileTexId(int file_type, int file_id);

char GetPlyrFileState(int file_type, int file_id);
void SetPlyrFileState(int file_type, int file_id, char state);

/* How many files of `file_type` the player has collected. */
int  GetPlyrFileTotalNum(int file_type);

/* How many files exist for `type`: 42/42/40/26/10 for types 0..4. */
int  GetFileTypeMaxNum(int type);

void SetSave_PlyrFile(MC_SAVE_DATA *data);
void DebugAllFileGet(void);

#endif /* _INGAME_ITEM_PRG_FILE_H */
