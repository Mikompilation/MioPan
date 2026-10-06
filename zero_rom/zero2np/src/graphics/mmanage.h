/* ==========================================================================
 *  graphics/mmanage.h
 *
 *  Model manager: the thin, typed face over the OL_LOAD slot table.
 *
 *  Three resource families share one loader, each mapping a small per-family
 *  index onto its block of CD file numbers:
 *
 *      character model  mdl_no -> CH000_MIO_MDL        + mdl_no   (303)
 *      character anim   mdl_no -> CH000_MIO_ANM        + mdl_no   (382)
 *      item model       mdl_no -> I000_PLAY_CAMERA_PK2 + mdl_no   (2015)
 *
 *  Each family gets the same Req / IsReady / Clear triple.  IsReady() collapses
 *  OL_LOAD_READY into a plain 0/1 and performs the family's one-off fixup the
 *  first time the data lands.
 *
 *  CORRECTION: the trailing marks 9, 11, 13 ... 29 are NOT this header's line
 *  numbers.  They are the SI(n) field of mmanage.o's PROC records, which is a
 *  symbol-table index counting 1, 3, 5, 7 ... in the order bodies reach .text
 *  -- these start at 9 only because four fixed_array statics consumed 1..7
 *  first.  man_data.o disproves the line reading outright (its three
 *  _fixed_array_verifyrange instantiations of one template read 3, 5, 7, and
 *  MAN_DATA::Setup's PROC says 29 while its own $LMs measure its inline body
 *  at man_data.h 18-20).  Nothing here recovers the ROM header's layout; the
 *  marks are kept only because mmanage.c's annotations refer to them.  A
 *  header's real line map comes from tallying SOL/$LM pairs across every
 *  object that expands its inlines, and a function's opening brace from the
 *  $LM immediately before its PROC record -- that part still holds, and those
 *  are the lines annotated in mmanage.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_MMANAGE_H
#define _GRAPHICS_MMANAGE_H

#include "../common/ol_load.h"          /* OL_LOAD_ERR / OL_LOAD_READY */

/* Both of these are the ROM's own -- types.txt carries them immediately after
 * the OL_LOAD family, which is what places them in this header. */
typedef OL_LOAD_ERR MMANAGE_ERR;

/* Entry order inside an item model pack (a "pk2" offset-table container). */
enum ITEM_MODEL_PACK_ORDER
{
    ITEM_MODEL_PACK_ORDER_SGD = 0,
    ITEM_MODEL_PACK_ORDER_TM2 = 1,
    ITEM_MODEL_PACK_ORDER_BWC = 2
};

/* PORT: every mdl_pp is `int *` in the ROM and `void **` here.  IsReady()
 * stores a pointer through it, which does not fit a 32-bit int on the host. */

/* Frees whatever it can when the load heap is exhausted: the float ghost
 * first, then the preloaded enemies, then -- after dumping the loader state
 * and asserting -- every enemy.  wrk_no is the enemy work slot to spare. */
void ModelMemoryFree(int wrk_no);                                       /* 9 */

int *GetItemSgdAddr(int *pDataTop);                                     /* 11 */

MMANAGE_ERR mmanageReqItemMdl(int mdl_no);                              /* 13 */

/* bForceFree is defaulted in the ROM's own header: man_data.o's G3DASSERT
 * stringizes its condition as "(mmanageIsReadyItemMdl(mAcsNo, &mpAcsMdl))"
 * -- two arguments -- while the emitted call passes a2 = 0. */
int mmanageIsReadyItemMdl(int mdl_no, void **mdl_pp, int bForceFree = 0); /* 15 */

void mmanageClearItemMdl(int mdl_no);                                   /* 17 */

MMANAGE_ERR mmanageReqMdl(int mdl_no);                                  /* 19 */

int mmanageIsReadyMdl(int mdl_no, void **mdl_pp, int bForceFree);       /* 21 */

void mmanageClearMdl(int mdl_no);                                       /* 23 */

MMANAGE_ERR mmanageReqAnm(int mdl_no);                                  /* 25 */

int mmanageIsReadyAnm(int mdl_no, void **mdl_pp, int bForceFree);       /* 27 */

void mmanageClearAnm(int mdl_no);                                       /* 29 */

#endif /* _GRAPHICS_MMANAGE_H */
