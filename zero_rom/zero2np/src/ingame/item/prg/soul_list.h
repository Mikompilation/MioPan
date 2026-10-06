/* ==========================================================================
 *  ingame/item/prg/soul_list.h
 *
 *  Ghost list ("soul list") progress (soul_list.o).  One PLYR_SOUL_LIST row
 *  per ghost list label: whether the player has the entry, whether they have
 *  read it, and the best photo score ever taken of that ghost.
 *
 *  The 176 labels split at 152: 0..151 are the base list, 152..175 the
 *  "enhancing" bonus list, which is why the have-count is the sum of two
 *  separate loops rather than one.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_PRG_SOUL_LIST_H
#define _INGAME_ITEM_PRG_SOUL_LIST_H

#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "eetypes.h"

/* Capacity of plyr_soul_list.  The ROM writes the bound out as a literal at
 * every guard (sltiu against 0xb0), not derived from the array. */
#define SOUL_LIST_MAX       176

/* Labels below this are the base list, at or above it the enhancing list.
 * GetPlyrHaveBaseSoulListNum / GetPlyrHaveEnhancingSoulListNum are the split. */
#define SOUL_LIST_BASE_MAX  152

/* PLYR_SOUL_LIST::state.  The ROM's debug info carries no enum for these; the
 * names come from ReadSoulList(), which warns "No Have List" on 0 and moves
 * 1 -> 2 on a read, and from SetPlyrSoulListState() asserting on anything
 * above 2. */
#define SOUL_LIST_STATE_NONE    0       /* never photographed */
#define SOUL_LIST_STATE_HAVE    1       /* obtained, not yet looked at */
#define SOUL_LIST_STATE_READ    2       /* obtained and read */

/* SetPlyrSoulListMaxScore() clamps to this before it validates anything. */
#define SOUL_LIST_SCORE_MAX     99999

/* CheckEnhancingSoulListCondition()'s threshold, in percent. */
#define SOUL_LIST_ENHANCING_RATE 90

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char state;
    /* 0x4 */ int  max_score;
} PLYR_SOUL_LIST;

/* plyr_soul_list itself is `static` in the ROM (bss 4bc0a0) and is not
 * declared here -- everything outside soul_list.o goes through the accessors
 * below.  SetSave_PlyrSoulList() is how the save system reaches the storage. */

/* Clears every row and drops the menu's "new entry" badge. */
void PlyrSoulListInit(void);

/* Records a photo of `ghost_list_label` scoring `get_score`.  Returns 0 when
 * the label is out of range (a printf, not an assert -- event scripts pass
 * arbitrary labels), 1 otherwise.  Only raises max_score, never lowers it. */
int  GetSoulList(int ghost_list_label, int get_score);

/* Marks an obtained entry as read (HAVE -> READ).  Warns on a label the
 * player does not have; asserts on anything that is not a valid state. */
void ReadSoulList(int ghost_list_label);

char GetPlyrSoulListState(int ghost_list_label);
int  GetPlyrSoulListMaxScore(int ghost_list_label);

int  GetPlyrHaveSoulListNum(void);
int  GetPlyrHaveBaseSoulListNum(void);
int  GetPlyrHaveEnhancingSoulListNum(void);

/* Fills order[0..get_num-1] with the highest `get_num` *distinct* max_scores,
 * descending.  Entries the player has never scored contribute 0. */
void GetSoulListOrderScore(int *order, int get_num);

/* Percentage of the 176 labels obtained, clamped to 0..100.  Latches
 * clear_flg_ctrl.comp_soul_list_flg once it reaches 100. */
int  GetSoulListAccomplishmentRate(void);

/* True once the completion rate reaches SOUL_LIST_ENHANCING_RATE. */
int  CheckEnhancingSoulListCondition(void);

void SetSave_PlyrSoulList(MC_SAVE_DATA *data);
void DebugGetAllSoulList(void);

#endif /* _INGAME_ITEM_PRG_SOUL_LIST_H */
