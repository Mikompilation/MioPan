/* ==========================================================================
 *  ingame/map/MapLBuff.h
 *
 *  Map load buffer: the per-map table of extra files an event has asked to be
 *  kept resident.  Each of the 66 maps owns 16 slots, and a slot is a label ID
 *  plus a reference count, so the same file requested by two overlapping
 *  events is only released once both have let go of it.
 *
 *  The event macros LOAD_REQUEST / RELEASE_REQUEST are the only live callers
 *  (ev_macro.c) -- they take and drop references.  The half that would act on
 *  the table, MapLBuffRegist() / MapLBuffLoad(), is unfinished in this
 *  prototype; see MapLBuff.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_MAPLBUFF_H
#define _INGAME_MAP_MAPLBUFF_H

/* Clears every slot of every map to "free".  Never called anywhere in the ROM
 * -- see the note in MapLBuff.c before wiring it up. */
void MapLBuffInit(void);

/* Takes a reference on `label` in map `map_id`, claiming a free slot if the
 * file is not already listed.  Returns 0, or -1 when all 16 slots are taken. */
int MapLBuffSetLoadFile(int map_id, int label);

/* Drops a reference, freeing the slot when the count reaches zero.  Returns 0,
 * or -1 when `label` was not listed for that map. */
int MapLBuffDeleteFile(int map_id, int label);

/* Walk the map's slots and act on each resident file.  Both are hollow in this
 * build: they classify every listed label and return 0.  Neither is called. */
int MapLBuffRegist(int map_id);
int MapLBuffLoad(int map_id);

#endif /* _INGAME_MAP_MAPLBUFF_H */
