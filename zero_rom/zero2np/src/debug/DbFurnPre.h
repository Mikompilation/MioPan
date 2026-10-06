/* ==========================================================================
 *  debug/DbFurnPre.h
 *
 *  Furniture pre-light debug editor (DbFurnPre.o).  Steps through the room's
 *  registered furniture models and re-bakes the vertex lighting of one placed
 *  instance at a time, so an artist can see a single object's prelight against
 *  the room's real light set.
 *
 *  All 5 ZERO2.MAP .text exports are implemented in DbFurnPre.c, plus the two
 *  file-local helpers (DbFurnPreGetObjNum(int), DbFurnPreGetObjName) that
 *  ZERO2.MAP omits.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_DBFURNPRE_H
#define _DEBUG_DBFURNPRE_H

#include <stdint.h>

/* Counts the placements of `name` in registration `reg_id` whose record type
 * is `type` (3 object, 7 door, 11 put-item), matching on the first four
 * characters the way every FurnCtl lookup does.
 *
 * `flg` selects between the two things this does.  Negative counts the whole
 * list and returns the count; zero or positive stops at the (flg + 1)'th match
 * and returns THAT RECORD's address instead.
 *
 * PORT DEVIATION: the ROM's return type is `int` -- a 4-byte MB_OUT_SECTION *
 * handed back as an integer on the one path, a count on the other.  Widened to
 * uintptr_t so the pointer survives on a 64-bit host, exactly as MapDoorAdd()
 * is.  Both callers are in DbFurnPre.c. */
uintptr_t DbFurnPreGetNumOneType(int reg_id, int type, char *name, int flg);

/* Total placements of `name` in `buff_id`'s room.  A name beginning 'd' is a
 * door and only type 7 is searched; anything else is counted across both the
 * put-item (11) and object (3) lists. */
int DbFurnPreGetObjNum(int buff_id, char *name);

/* The `id`'th placement of `name`, as a pointer to its record's Pos[3].  The
 * put-item list is indexed first and the object list takes over past its end,
 * which is the same order DbFurnPreGetObjNum() sums them in. */
float *DbFurnPreGetDat(int buff_id, char *name, int id);

/* Points the editor at a room buffer and re-counts its distinct models.
 * Called from MapDrawInitFurn() as the room registers. */
void DbFurnPreSetBuffID(int buff_id);

/* One frame of the editor: pad handling, the bake, and the readout.  Called
 * from MhCtlDraw() while mhdb.predb_mode is set. */
void DbFurnPreProc(void);

#endif /* _DEBUG_DBFURNPRE_H */
