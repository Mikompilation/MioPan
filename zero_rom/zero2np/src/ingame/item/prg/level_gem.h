/* ==========================================================================
 *  ingame/item/prg/level_gem.h
 *
 *  Level gem count (level_gem.o).  The whole module is one `char` of state --
 *  a bare counter with a saturating increment and a floored decrement, no
 *  per-slot inventory like the other item modules.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_PRG_LEVEL_GEM_H
#define _INGAME_ITEM_PRG_LEVEL_GEM_H

#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "eetypes.h"

/* GetLevelGem() saturates here and DebugSetLevelGemMaxNum() jumps straight
 * to it.  The ROM writes 0x63 out as a literal at both sites. */
#define LEVEL_GEM_MAX 99

void PlyrLevelGemInit(void);

/* Add one, saturating at LEVEL_GEM_MAX. */
void GetLevelGem(void);

/* Spend one.  Underflow warns and clamps to 0 rather than asserting -- the
 * count is already stored decremented by the time the check runs. */
void LostLevelGem(void);

char GetPlyrLevelGemNum(void);

void SetSave_PlyrLevelGem(MC_SAVE_DATA *data);
void DebugSetLevelGemMaxNum(void);

#endif /* _INGAME_ITEM_PRG_LEVEL_GEM_H */
