// FILE: /home/zero_rom/zero2np/src/ingame/item/prg/soul_list.c
//
// Ghost list ("soul list") progress.  plyr_soul_list[] is indexed by ghost
// list label and holds two things per entry: how far the player has got with
// it (never seen / obtained / read) and the best photo score they have taken
// of that ghost.
//
// Every entry point range-checks the label against 176 and asserts, so the
// ROM bodies are mostly guard.  The bound is a literal at each site (sltiu
// against 0xb0), not derived from the array.
//
// Two things here reach outside the item system: hitting 100% latches
// clear_flg_ctrl.comp_soul_list_flg, unlocks the last tempered lens and hands
// the player a scrap file; and CheckEnhancingSoulListCondition() gates the
// bonus list at 90%.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "soul_list.h"

#include "../../../common/utility2.h"   /* PRINT_ASSERT / PRINT_WARNING */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../clear/prg/clear_flg.h"  /* clear_flg_ctrl */
#include "../../menu/menu_soul.h"       /* MenuSoulListCompFlgInit */
#include "../../photo/m_plyr_camera.h"  /* m_plyr_camera */
#include "../dat/file_dat.h"            /* FILE_TYPE_SCRAP */
#include "file.h"                       /* FileGet */

#include <stdio.h>

static void SetPlyrSoulListState(int ghost_list_label, char set_state);
static void SetPlyrSoulListMaxScore(int ghost_list_label, int set_score);

/* `static` in the ROM as well -- nothing outside this file touches the storage
 * except through SetSave_PlyrSoulList(). */
static fixed_array<PLYR_SOUL_LIST, SOUL_LIST_MAX> plyr_soul_list;   /* bss 4bc0a0 */

/* --------------------------------------------------------------------------
 *  PlyrSoulListInit
 * ------------------------------------------------------------------------ */
void PlyrSoulListInit(void)
{                                                                       /* 67 */
    int i;

    for (i = 0; i < SOUL_LIST_MAX; i++)                                 /* 72 */
    {
        plyr_soul_list[i].state     = SOUL_LIST_STATE_NONE;
        plyr_soul_list[i].max_score = 0;
    }                                                                   /* 75 */

    MenuSoulListCompFlgInit();                                          /* 78 */
}

/* --------------------------------------------------------------------------
 *  GetSoulList
 *
 *  An out-of-range label is a printf and a 0 return rather than an assert --
 *  unlike every other entry point here.  Event scripts feed this function
 *  directly, so a bad label has to be survivable.
 *
 *  Both arms of the state test call SetPlyrSoulListMaxScore with the same
 *  arguments; the ROM really does duplicate the call rather than hoist it.
 * ------------------------------------------------------------------------ */
int GetSoulList(int ghost_list_label, int get_score)
{                                                                       /* 98 */
    if ((u_int)ghost_list_label >= SOUL_LIST_MAX)                       /* 100 */
    {
        printf("Not Found! Soul List Data! ghost list label[%d]\n",
               ghost_list_label);                                       /* 101 */
        return 0;                                                       /* 102 */
    }

    if (get_score < 0)                                                  /* 106 */
    {
        PRINT_ASSERT("Error! GetSoulList get_score %d", get_score);     /* 107 */
    }

    if (GetPlyrSoulListState(ghost_list_label) == SOUL_LIST_STATE_NONE) /* 113 */
    {
        SetPlyrSoulListState(ghost_list_label, SOUL_LIST_STATE_HAVE);   /* 115 */

        SetPlyrSoulListMaxScore(ghost_list_label, get_score);           /* 117 */
    }
    else
    {
        SetPlyrSoulListMaxScore(ghost_list_label, get_score);           /* 121 */
    }

    if (GetSoulListAccomplishmentRate() == 100)                         /* 124 */
    {
        clear_flg_ctrl.comp_soul_list_flg = 1;                          /* 125 */

        /* Completing the list unlocks tempered lens 9.  The call site's own
         * line is lost -- all of its code came from the BIT_FLAGS::FlgUp
         * inline, so the ROM tags it variable.h:833.  It sits between 125
         * and 129. */
        m_plyr_camera.camera_power_up.mTemperedRenzFlg.FlgUp(9);

        FileGet(FILE_TYPE_SCRAP, 30);                                   /* 129 */
    }

    return 1;                                                           /* 133 */
}

/* --------------------------------------------------------------------------
 *  ReadSoulList
 * ------------------------------------------------------------------------ */
void ReadSoulList(int ghost_list_label)
{                                                                       /* 140 */
    if ((u_int)ghost_list_label >= SOUL_LIST_MAX)                       /* 144 */
    {
        PRINT_ASSERT("Error! ReadSoulList ghost_list_label %d",
                     ghost_list_label);                                 /* 145 */
    }

    switch (GetPlyrSoulListState(ghost_list_label))                     /* 149 */
    {
    case SOUL_LIST_STATE_NONE:
        PRINT_WARNING("Warning! ReadSoulList No Have List %d",
                      ghost_list_label);                                /* 151 */
        break;

    case SOUL_LIST_STATE_HAVE:
        SetPlyrSoulListState(ghost_list_label, SOUL_LIST_STATE_READ);   /* 154 */
        break;

    case SOUL_LIST_STATE_READ:
        break;

    default:
        PRINT_ASSERT("Error! ReadSoulList");                            /* 160 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Accessors
 * ------------------------------------------------------------------------ */
char GetPlyrSoulListState(int ghost_list_label)
{                                                                       /* 175 */
    if ((u_int)ghost_list_label >= SOUL_LIST_MAX)                       /* 179 */
    {
        PRINT_ASSERT("Error! GetPlyrSoulListState ghost_list_label %d",
                     ghost_list_label);                                 /* 180 */
    }

    return plyr_soul_list[ghost_list_label].state;
}

int GetPlyrSoulListMaxScore(int ghost_list_label)
{                                                                       /* 193 */
    if ((u_int)ghost_list_label >= SOUL_LIST_MAX)                       /* 197 */
    {
        PRINT_ASSERT("Error! GetPlyrSoulListMaxScore ghost_list_label %d",
                     ghost_list_label);                                 /* 198 */
    }

    return plyr_soul_list[ghost_list_label].max_score;
}

/* --------------------------------------------------------------------------
 *  Counts
 *
 *  Both loops count anything that is not STATE_NONE, so a read entry and an
 *  unread one weigh the same.
 * ------------------------------------------------------------------------ */
int GetPlyrHaveSoulListNum(void)
{                                                                       /* 210 */
    return GetPlyrHaveBaseSoulListNum()                                 /* 217 */
         + GetPlyrHaveEnhancingSoulListNum();                           /* 218 */
}                                                                       /* 221 */

int GetPlyrHaveBaseSoulListNum(void)
{                                                                       /* 228 */
    int i;
    int have_num;

    have_num = 0;                                                       /* 233 */

    for (i = 0; i < SOUL_LIST_BASE_MAX; i++)                            /* 236 */
    {
        if (GetPlyrSoulListState(i) != SOUL_LIST_STATE_NONE)            /* 238 */
        {
            have_num++;
        }
    }                                                                   /* 241 */

    return have_num;                                                    /* 244 */
}

int GetPlyrHaveEnhancingSoulListNum(void)
{                                                                       /* 251 */
    int i;
    int have_num;

    have_num = 0;                                                       /* 256 */

    for (i = SOUL_LIST_BASE_MAX; i < SOUL_LIST_MAX; i++)                /* 259 */
    {
        if (GetPlyrSoulListState(i) != SOUL_LIST_STATE_NONE)            /* 261 */
        {
            have_num++;
        }
    }                                                                   /* 264 */

    return have_num;                                                    /* 267 */
}

/* --------------------------------------------------------------------------
 *  GetSoulListOrderScore
 *
 *  Top-`get_num` distinct scores, descending.  Copies every max_score into a
 *  176-int stack buffer, bubble-sorts it ascending, then walks it from the top
 *  taking each value that is not already in order[].  Because order[] starts
 *  zeroed and the insert only overwrites a slot smaller than the candidate,
 *  duplicates collapse and short lists stay zero-padded.
 *
 *  The sort keeps the index of the last swap and uses it as the next pass's
 *  bound, so a nearly-sorted array falls out early.
 *
 *  Line 311 seeds order[0] before testing get_num, so get_num == 0 writes one
 *  element past the caller's buffer.  That is the ROM's order; every caller
 *  passes a positive count.
 * ------------------------------------------------------------------------ */
void GetSoulListOrderScore(int *order, int get_num)
{                                                                       /* 275 */
    int i;
    int j;
    int k;
    int score[SOUL_LIST_MAX];

    for (i = 0; i < get_num; i++)                                       /* 281 */
    {
        order[i] = 0;                                                   /* 282 */
    }                                                                   /* 283 */

    for (i = 0; i < SOUL_LIST_MAX; i++)                                 /* 286 */
    {
        score[i] = GetPlyrSoulListMaxScore(i);                          /* 287 */
    }                                                                   /* 288 */

    k = SOUL_LIST_MAX - 1;                                              /* 290 */

    do
    {
        j = -1;                                                         /* 294 */

        for (i = 1; i <= k; i++)                                        /* 296 */
        {
            if (score[i] < score[i - 1])                                /* 297 */
            {
                /* The ROM's stabs name only i/j/k in this frame: GCC
                 * coalesced this temp with the load the comparison above
                 * already did, so it costs no instruction and carries no
                 * debug entry.  Hence no line number on it either. */
                int tmp;

                j = i - 1;                                              /* 298 */

                tmp      = score[j];
                score[j] = score[i];                                    /* 301 */
                score[i] = tmp;                                         /* 302 */
            }
        }                                                               /* 304 */

        k = j;                                                          /* 306 */
    }
    while (0 <= k);

    order[0] = score[SOUL_LIST_MAX - 1];                                /* 311 */

    for (i = SOUL_LIST_MAX - 1; 0 <= i; i--)                            /* 312 */
    {
        for (j = 0; j < get_num; j++)                                   /* 313 */
        {
            if (order[j] == score[i])                                   /* 315 */
            {
                break;                                                  /* 316 */
            }
        }                                                               /* 318 */

        if (j == get_num)                                               /* 321 */
        {
            for (k = 0; k < get_num; k++)                               /* 322 */
            {
                if (order[k] < score[i])                                /* 323 */
                {
                    order[k] = score[i];                                /* 324 */
                    break;                                              /* 325 */
                }
            }                                                           /* 327 */
        }
    }                                                                   /* 329 */
}

/* --------------------------------------------------------------------------
 *  GetSoulListAccomplishmentRate
 *
 *  The high clamp comes first, so the negative branch is only reachable if
 *  GetPlyrHaveSoulListNum() ever went negative -- it cannot, which is why the
 *  assert is there rather than a fix.
 * ------------------------------------------------------------------------ */
int GetSoulListAccomplishmentRate(void)
{                                                                       /* 336 */
    int rate;

    rate = GetPlyrHaveSoulListNum() * 100 / SOUL_LIST_MAX;              /* 344 */

    if (100 < rate)                                                     /* 347 */
    {
        rate = 100;                                                     /* 348 */
    }
    else if (rate < 0)                                                  /* 350 */
    {
        PRINT_ASSERT("Error! GetSoulListAccomplishmentRate rate %d",
                     rate);                                             /* 352 */

        rate = 0;                                                       /* 354 */
    }

    if (rate == 100)                                                    /* 357 */
    {
        clear_flg_ctrl.comp_soul_list_flg = 1;                          /* 358 */
    }

    return rate;                                                        /* 362 */
}

int CheckEnhancingSoulListCondition(void)
{                                                                       /* 370 */
    /* Branchless in the ROM (slti + xori), so this is one statement, not an
     * if/else pair. */
    return (SOUL_LIST_ENHANCING_RATE <= GetSoulListAccomplishmentRate());
                                                                        /* 382 */
}                                                                       /* 390 */

/* --------------------------------------------------------------------------
 *  Setters (file-local)
 * ------------------------------------------------------------------------ */
static void SetPlyrSoulListState(int ghost_list_label, char set_state)
{                                                                       /* 402 */
    if ((u_int)ghost_list_label >= SOUL_LIST_MAX)                       /* 406 */
    {
        PRINT_ASSERT("Error! SetPlyrSoulListState ghost_list_label %d",
                     ghost_list_label);                                 /* 407 */
    }

    /* Unsigned compare in the ROM, so a negative set_state slips past. */
    if ((u_char)set_state > SOUL_LIST_STATE_READ)                       /* 409 */
    {
        PRINT_ASSERT("Error! SetPlyrSoulListState set_state %d",
                     set_state);                                        /* 410 */
    }

    plyr_soul_list[ghost_list_label].state = set_state;
}

/* Clamps before it validates, so a score above the cap never trips the
 * range assert -- that is the ROM's order, not an oversight worth fixing. */
static void SetPlyrSoulListMaxScore(int ghost_list_label, int set_score)
{                                                                       /* 425 */
    if (SOUL_LIST_SCORE_MAX < set_score)                                /* 427 */
    {
        set_score = SOUL_LIST_SCORE_MAX;
    }

    if ((u_int)ghost_list_label >= SOUL_LIST_MAX)                       /* 433 */
    {
        PRINT_ASSERT("Error! SetPlyrSoulListMaxScore ghost_list_label %d",
                     ghost_list_label);                                 /* 434 */
    }

    if (set_score < 0)                                                  /* 436 */
    {
        PRINT_ASSERT("Error! SetPlyrSoulListMaxScore set_score %d",
                     set_score);                                        /* 437 */
    }

    if (GetPlyrSoulListMaxScore(ghost_list_label) < set_score)          /* 442 */
    {
        plyr_soul_list[ghost_list_label].max_score = set_score;
    }
}

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */
void SetSave_PlyrSoulList(MC_SAVE_DATA *data)
{                                                                       /* 457 */
    data->addr = (u_char *)&plyr_soul_list[0];
    data->size = sizeof(PLYR_SOUL_LIST) * SOUL_LIST_MAX;                /* 461 */
}

void DebugGetAllSoulList(void)
{                                                                       /* 472 */
    int i;

    for (i = 0; i < SOUL_LIST_MAX; i++)                                 /* 478 */
    {
        plyr_soul_list[i].state = SOUL_LIST_STATE_HAVE;
    }                                                                   /* 481 */
}
