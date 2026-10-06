/* ==========================================================================
 *  ingame/item/dat/item_dat.h
 *
 *  The master item table (item_dat.o).  Pure data: the object's whole .text
 *  is fixed_array template boilerplate, so item_dat.c has no code of its own.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_DAT_ITEM_DAT_H
#define _INGAME_ITEM_DAT_ITEM_DAT_H

#include "eetypes.h"

#define ITEM_DAT_MAX 58

/* types.txt.  A plain array in the ROM, not a fixed_array -- item.c subscripts
 * it directly with no _fixed_array_verifyrange in sight, unlike plyr_item. */
typedef struct                      /* 0x2c */
{
    /* 0x00 */ int    item_id;
    /* 0x04 */ u_char type;
    /* 0x05 */ u_char get_max;
    /* 0x06 */ u_char def_use_num;
    /* 0x08 */ int    value;
    /* 0x0c */ char   item_name[32];
} ITEM_DAT;

/* ITEM_DAT::type.  The ROM's debug info carries no enum for these; the names
 * come from what ItemUse() dispatches each value to.  Type 4 is consumed with
 * no handler inside item.c -- ItemUse() just answers "used" and lets ItemLost()
 * take it, so whatever it does happens in the caller. */
#define ITEM_TYPE_NONE      0       /* no use action at all */
#define ITEM_TYPE_FILM      1       /* ItemFilmEquip -- equips, never consumed */
#define ITEM_TYPE_HP        2       /* ItemPlyrHPRecover */
#define ITEM_TYPE_SP        3       /* ItemPlyrSPRecover */
#define ITEM_TYPE_CONSUME   4       /* consumed, effect handled by the caller */
#define ITEM_TYPE_EVENT     5       /* key / event item; AllPlyrEventItemLost drops these */

extern ITEM_DAT item_dat[ITEM_DAT_MAX];     /* data 318930 */

#endif /* _INGAME_ITEM_DAT_ITEM_DAT_H */
