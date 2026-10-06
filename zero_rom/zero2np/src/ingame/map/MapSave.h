/* ==========================================================================
 *  ingame/map/MapSave.h
 *
 *  Persistent per-object map state.  A room's MDAT_OBJ records only exist
 *  while the room is resident, so the five mutable fields the SET_OBJ_*
 *  event opcodes write are mirrored into a flat table that lives for the
 *  whole game and goes into the memory-card save.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPSAVE_H
#define _INGAME_MAP_MAPSAVE_H

#include "eetypes.h"
#include "../../common/save_data.h"     /* MC_SAVE_DATA */

enum
{
    /* Every save-able object in the game has one slot; the table is a fixed
     * build-time list, not a pool. */
    MAPSAVE_MAX = 673
};

/* The saved state of one placed object.  Narrower than the MDAT_OBJ fields it
 * mirrors (int there, char/short here) because the whole table is written to
 * the memory card verbatim -- MapSaveCallback() hands out its raw bytes. */
typedef struct MAPSAVE_HEAD             /* 0x10 */
{
    /* 0x0 */ int   labelID;
    /* 0x4 */ char  HitCheck;
    /* 0x5 */ char  PhotoAble;
    /* 0x6 */ char  Visible;
    /* 0x7 */ char  ActionType;
    /* 0x8 */ short Action;
    /* 0xa */ short Weight;
    /* 0xc */ int   Attribute;
} MAPSAVE_HEAD;

/* Index into the table by area.  Labels are <area>*1000 + <object>, and the
 * table is grouped by area with the labels ascending inside each group, so one
 * entry per area plus a linear walk finds any label.  The areas themselves are
 * in no particular order, which is why the jump table is built at runtime.
 *
 * Host note: `head` is 4 bytes on the EE and 8 here, so this struct is 0x10
 * on the host against the ROM's 0x8.  It is internal scratch, never saved. */
typedef struct MAPSAVE_JMP_TBL          /* 0x8 */
{
    /* 0x0 */ int           AreaID;
    /* 0x4 */ MAPSAVE_HEAD *head;
} MAPSAVE_JMP_TBL;

/* `stat` selector shared by MapSaveGetStat() / MapSaveSetStat().  The ROM
 * passes these as bare integers at every call site (ev_macro.c's SET_OBJ_*
 * handlers), so they are documented rather than named:
 *
 *      0  HitCheck    1  PhotoAble   2  Visible    3  ActionType
 *      4  Action      5  Weight      6  Attribute
 */

/* The save block for one label, or NULL when the label is not in the table.
 * MapObjGetFreeDatPtr() hangs it off the draw entry for stat-3 objects. */
void *MapSaveGetTblPtr(int label);

/* One saved field of one label.  Returns -1 for an unknown label or an
 * out-of-range `stat`.  Unreferenced in the prototype; kept because it is a
 * global in the link map. */
int MapSaveGetStat(int label, int stat);

/* Writes one saved field of one label.  Silently does nothing when the label
 * is not in the table -- events fire against objects that no longer exist. */
void MapSaveSetStat(int label, int stat, int num);

/* Seeds every object record of one registration file from its save block, so
 * the objects about to be registered come up in their persisted state. */
void MapSaveSetMstDat(int reg_id);

/* Mirrors a live MDAT_OBJ's mutable state back into its MAPSAVE_HEAD.
 * MapObjProc() runs it once per frame for every entry that has one. */
void MapSaveCopyDat(void *out, void *mst);

/* Rebuilds the per-area jump table.  Must run after any bulk write to the
 * table -- i.e. after MapSavePopFirstDat() or a memory-card load. */
void MapSaveRegist(void);

/* Memory-card hook: publishes the whole table as one contiguous block. */
void MapSaveCallback(MC_SAVE_DATA *data);

/* Snapshots the build-time table at boot, and restores it on a new game. */
void MapSavePushFirstDat(void);
void MapSavePopFirstDat(void);

#endif /* _INGAME_MAP_MAPSAVE_H */
