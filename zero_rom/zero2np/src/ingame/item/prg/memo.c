// FILE: /home/zero_rom/zero2np/src/ingame/item/prg/memo.c
//
// Memo entries.  plyr_memo[] is twenty slots of (state, msg_step): whether the
// player holds the memo and has read it, and which of its two revisions they
// have.
//
// The interesting rule is in UpdateMemo(): a memo already read drops back to
// unread when a *later* revision arrives, so the menu badge reappears.  That
// is why state and msg_step are stored together rather than as two arrays.
//
// The range checks here cast to unsigned, unlike file.o and crystal.o which
// compare signed and let a negative index through.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "memo.h"

#include "../../../common/utility2.h"   /* PRINT_ASSERT */
#include "../../../graphics/graph3d/ctl/fixed_array.h"

static fixed_array<PLYR_MEMO, MEMO_MAX> plyr_memo;                  /* bss 4b5390 */
                                                                    /* 42 */

void PlyrMemoInit(void)
{                                                                       /* 55 */
    int i;

    for (i = 0; i < MEMO_MAX; i++)                                      /* 60 */
    {
        plyr_memo[i].state    = MEMO_STATE_NONE;
        plyr_memo[i].msg_step = 0;
    }                                                                   /* 63 */
}

/* --------------------------------------------------------------------------
 *  UpdateMemo
 *
 *  Writing msg_step before state is the ROM's order and matters: the state
 *  write is what clears a previous read, so the pair has to land together.
 * ------------------------------------------------------------------------ */
void UpdateMemo(int memo_label, u_char msg_step)
{                                                                       /* 81 */
    if ((u_int)memo_label >= MEMO_MAX)                                  /* 85 */
    {
        PRINT_ASSERT("Error! UpdateMemo memo_label %d", memo_label);    /* 86 */
    }

    if (msg_step > MEMO_MSG_STEP_MAX)                                   /* 88 */
    {
        PRINT_ASSERT("Error! UpdateMemo msg_step %d", msg_step);        /* 89 */
    }

    if ((plyr_memo[memo_label].state == MEMO_STATE_NONE) ||
        (plyr_memo[memo_label].msg_step < (int)msg_step))
    {
        plyr_memo[memo_label].msg_step = msg_step;
        plyr_memo[memo_label].state    = MEMO_STATE_HAVE;
    }
}

/* The only assert in the file that formats __FUNCTION__ through %s rather than
 * spelling the name out in the literal. */
void ReadMemo(int memo_label)
{                                                                       /* 113 */
    if ((u_int)memo_label >= MEMO_MAX)                                  /* 116 */
    {
        PRINT_ASSERT("Error! %s memo_label %d\n", __FUNCTION__,
                     memo_label);                                       /* 117 */
    }

    if (plyr_memo[memo_label].state != MEMO_STATE_NONE)
    {
        plyr_memo[memo_label].state = MEMO_STATE_READ;
    }
}

/* --------------------------------------------------------------------------
 *  Accessors
 * ------------------------------------------------------------------------ */
int GetMemoState(int memo_label)
{                                                                       /* 138 */
    if ((u_int)memo_label >= MEMO_MAX)                                  /* 142 */
    {
        PRINT_ASSERT("Error! GetMemoState memo_label %d\n", memo_label);/* 143 */
    }

    return plyr_memo[memo_label].state;
}

int GetMemoMsgStep(int memo_label)
{                                                                       /* 158 */
    if ((u_int)memo_label >= MEMO_MAX)                                  /* 162 */
    {
        /* "GetMemoMsgID" is what the ROM's string actually says -- a leftover
         * from an earlier name for this function.  Kept verbatim. */
        PRINT_ASSERT("Error! GetMemoMsgID memo_label %d\n", memo_label);/* 163 */
    }

    return plyr_memo[memo_label].msg_step;
}

int GetMemoHaveNum(void)
{                                                                       /* 176 */
    int i;
    int num;

    num = 0;                                                            /* 181 */

    for (i = 0; i < MEMO_MAX; i++)                                      /* 184 */
    {
        if (plyr_memo[i].state != MEMO_STATE_NONE)
        {
            num++;
        }
    }                                                                   /* 189 */

    return num;                                                         /* 192 */
}

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */
void SetSave_PlyrMemo(MC_SAVE_DATA *data)
{                                                                       /* 203 */
    data->addr = (u_char *)&plyr_memo[0];
    data->size = sizeof(PLYR_MEMO) * MEMO_MAX;                          /* 207 */
}

/* Both write state directly instead of going through UpdateMemo(), so they
 * also clear msg_step rather than only ever raising it. */
void DebugAllFirstMemoGet(void)
{                                                                       /* 218 */
    int i;

    for (i = 0; i < MEMO_MAX; i++)                                      /* 223 */
    {
        plyr_memo[i].state    = MEMO_STATE_HAVE;
        plyr_memo[i].msg_step = 0;
    }                                                                   /* 226 */
}

void DebugAllSecondMemoGet(void)
{                                                                       /* 234 */
    int i;

    for (i = 0; i < MEMO_MAX; i++)                                      /* 239 */
    {
        plyr_memo[i].state    = MEMO_STATE_HAVE;
        plyr_memo[i].msg_step = 1;
    }                                                                   /* 242 */
}
