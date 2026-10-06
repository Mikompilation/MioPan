/* ==========================================================================
 *  ingame/item/prg/memo.h
 *
 *  Memo entries (memo.o).  Twenty slots, each holding a read state and which
 *  of the memo's two revisions the player has seen.
 *
 *  Unlike file.o and crystal.o, the range checks here cast to unsigned before
 *  comparing, so a negative label is caught.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_ITEM_PRG_MEMO_H
#define _INGAME_ITEM_PRG_MEMO_H

#include "../../../common/save_data.h"                  /* MC_SAVE_DATA */
#include "eetypes.h"

#define MEMO_MAX 20

/* A memo has two revisions: the first sighting and a later, fuller version.
 * UpdateMemo() asserts on anything above step 1, and the two debug helpers
 * exist precisely to hand out one or the other. */
#define MEMO_MSG_STEP_MAX 1

/* The ROM's debug info carries no enum.  Names follow the same scheme as the
 * neighbouring inventories -- ReadMemo() moves anything held to READ, and
 * UpdateMemo() drops a slot back to HAVE when a newer revision arrives. */
#define MEMO_STATE_NONE 0       /* never seen */
#define MEMO_STATE_HAVE 1       /* holding an unread revision */
#define MEMO_STATE_READ 2       /* current revision has been read */

/* types.txt.  Only memo.c touches the instance (`static plyr_memo` at
 * bss 4b5390); the layout is here because SetSave_PlyrMemo() hands the whole
 * array to the save system. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int state;
    /* 0x4 */ int msg_step;
} PLYR_MEMO;

void PlyrMemoInit(void);

/* Record a memo at `msg_step`.  Only takes effect if the slot is empty or the
 * new revision is later than the stored one -- and when it does, the slot goes
 * back to unread even if it had already been read. */
void UpdateMemo(int memo_label, u_char msg_step);

/* Mark a held memo read.  Does nothing if the slot is empty. */
void ReadMemo(int memo_label);

int  GetMemoState(int memo_label);
int  GetMemoMsgStep(int memo_label);

/* How many slots hold anything (HAVE and READ both count). */
int  GetMemoHaveNum(void);

void SetSave_PlyrMemo(MC_SAVE_DATA *data);

/* Hand out every memo at revision 0 / revision 1 respectively. */
void DebugAllFirstMemoGet(void);
void DebugAllSecondMemoGet(void);

#endif /* _INGAME_ITEM_PRG_MEMO_H */
