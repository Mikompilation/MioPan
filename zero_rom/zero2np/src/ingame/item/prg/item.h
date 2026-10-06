/* ==========================================================================
 *  ingame/item/prg/item.h
 *
 *  Player inventory (item.o).  One PLYR_ITEM row per item id, indexed by the
 *  id itself, against the static item_dat[] table that says what each id is
 *  and what using it does.
 *
 *  Every entry point range-checks item_id and asserts, which is why the ROM
 *  bodies are longer than they look.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_PRG_ITEM_H
#define _INGAME_ITEM_PRG_ITEM_H

#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../dat/item_dat.h"                            /* ITEM_DAT / item_dat */

#define PLYR_ITEM_MAX 58

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int item_id;
    /* 0x4 */ int have_num;
} PLYR_ITEM;

/* The whole inventory: one row per item id.  A fixed_array, not a plain
 * array -- globals.txt types it that way and every subscript in the ROM
 * carries the inlined _fixed_array_verifyrange<PLYR_ITEM>(i, 58).  The item
 * debug submenus (debug/debug_menu.c) point their rows straight at have_num. */
extern fixed_array<PLYR_ITEM, PLYR_ITEM_MAX> plyr_item;     /* data 318760 */

/* Reset one row to "not carried" (item_id 0xff, have_num 0). */
void PlyrItemInit(PLYR_ITEM *plyr_item_addr);

/* Reset the whole inventory. */
void AllPlyrItemInit(void);

/* Zero the count of every ITEM_TYPE_EVENT row, leaving film and consumables
 * alone.  Chapter transitions take the key items back this way. */
void AllPlyrEventItemLost(void);

/* Add get_num of item_id, clamped to item_dat[].get_max.  Does nothing when
 * the player is already at the cap. */
void   ItemGet(int item_id, u_char get_num);
u_char ItemGetPossible(int item_id);

/* Use / discard.  ItemUse() dispatches on item_dat[].type and only calls
 * ItemLost() when the handler answers 1, which is why equipping film never
 * consumes it. */
u_char ItemUse(int item_id, u_char use_num);
u_char ItemUsePossible(int item_id, u_char use_num);
void   ItemLost(int item_id, u_char use_num);

/* Carried count for item_id, 0 when the player has none. */
int GetPlyrItemHaveNum(int item_id);

/* How many distinct ids the player is carrying at least one of. */
int GetHaveItemTypeNum(void);

int       GetItemType(int item_id);
ITEM_DAT *GetItemDatAddr(int item_id);

/* ItemUse() handlers.  Each answers 1 when it did something -- and so when the
 * item should be consumed. */
int ItemPlyrHPRecover(int item_id);
int ItemPlyrSPRecover(int item_id);
int ItemFilmEquip(int item_id);

/* Point a save-block descriptor at the inventory. */
void SetSave_PlyrItem(MC_SAVE_DATA *data);

/* Debug. */
void DebugAllItemGet(void);
void ItemDbgPlyrItemPrint(void);

#endif /* _INGAME_ITEM_PRG_ITEM_H */
