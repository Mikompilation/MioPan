/* ==========================================================================
 *  ingame/item/prg/crystal.h
 *
 *  Spirit-stone (crystal) inventory (crystal.o).  One state byte per crystal
 *  label: not found / found / listened to.  Each label also names an ADPCM
 *  stream and a subtitle table, both looked up here.
 *
 *  Every entry point range-checks crystal_label and asserts.  The check is
 *  signed -- `if (CRYSTAL_MAX <= crystal_label)` -- and PrintAssertReal
 *  returns, so label 40 really does read the fallback row that both lookup
 *  tables carry.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_PRG_CRYSTAL_H
#define _INGAME_ITEM_PRG_CRYSTAL_H

#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "../dat/crystal_dat.h"                         /* MOVIE_TITLE_DAT */
#include "eetypes.h"

#define CRYSTAL_MAX 40

/* The ROM's debug info carries no enum; the names come from what the three
 * mutators do -- GetCrystal moves NONE -> HAVE, HearCrystal moves anything
 * held to HEARD, LostCrystal drops back to NONE.  Unlike file.o there is no
 * SetPlyrCrystalState(), so nothing validates the value. */
#define CRYSTAL_STATE_NONE  0       /* not found */
#define CRYSTAL_STATE_HAVE  1       /* found, not yet played */
#define CRYSTAL_STATE_HEARD 2       /* played through */

void PlyrCrystalInit(void);

/* Collect a crystal.  Silently does nothing if already held -- no warning
 * here, unlike FileGet(). */
void GetCrystal(int crystal_label);

/* Drop a crystal.  Writes unconditionally; does not check it was held. */
void LostCrystal(int crystal_label);

/* Mark a held crystal as listened to.  Does nothing if not held. */
void HearCrystal(int crystal_label);

char GetPlyrCrystalState(int crystal_label);

/* ADPCM stream id for a crystal's recording. */
int  GetCrystalStreamID(int crystal_label);

/* Subtitle table for a crystal's recording. */
MOVIE_TITLE_DAT *GetCrystalTitleDat(int crystal_label);

/* How many crystals the player has found (HAVE and HEARD both count). */
int  GetPlyrHaveCrystalNum(void);

void SetSave_PlyrCrystal(MC_SAVE_DATA *data);
void DebugAllCrystalGet(void);

#endif /* _INGAME_ITEM_PRG_CRYSTAL_H */
