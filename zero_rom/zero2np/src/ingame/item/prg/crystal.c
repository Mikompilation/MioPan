// FILE: /home/zero_rom/zero2np/src/ingame/item/prg/crystal.c
//
// Spirit-stone (crystal) inventory.  plyr_crystal[] is one state byte per
// crystal label; crystal_stream[] and crystal_dat.o's crystal_title_dat[] turn
// the same label into the recording's stream id and subtitle table.
//
// Both lookup tables are sized 41 for 40 labels.  The extra row is what the
// range assert lets through: the guard is a signed compare and PrintAssertReal
// returns rather than halting, so label 40 falls into crystal_stream[40] and
// crystal_title_dummy.  Anything past 40 is a genuine overrun.
//
// Unlike file.o there is no SetPlyrCrystalState() -- the three mutators write
// plyr_crystal directly, so nothing validates the state value.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "crystal.h"

#include "../../../common/utility2.h"   /* PRINT_ASSERT */
#include "../../../graphics/graph3d/ctl/fixed_array.h"

#include <string.h>                     /* memset */

/* Stream id per crystal label; [40] is the out-of-range fallback that pairs
 * with crystal_title_dat[40]. */
static int crystal_stream[CRYSTAL_TITLE_DAT_MAX] =                  /* data 2d8db8 */
{
    2635, 2637, 2625, 2629, 2639, 2641, 2631, 2645,
    2627, 2633, 2643, 2415, 2413, 2417, 2647, 2423,
    2651, 2649, 2653, 2655, 2401, 2657, 2403, 2405,
    3067, 3069, 2407, 2421, 2409, 2411, 3071, 3073,
    3075, 2665, 2667, 2669, 2671, 2659, 2661, 2663,
    2158,
};

static fixed_array<char, CRYSTAL_MAX> plyr_crystal;                 /* bss 423070 */
                                                                    /* 30 */

/* --------------------------------------------------------------------------
 *  Init
 * ------------------------------------------------------------------------ */
void PlyrCrystalInit(void)
{                                                                       /* 54 */
    /* A memset rather than the per-element loop the other inventories use;
     * the statement's own line is lost because all of its code came from the
     * fixed_array::operator[] inline behind &plyr_crystal[0]. */
    memset(&plyr_crystal[0], 0, sizeof(plyr_crystal));
}

/* --------------------------------------------------------------------------
 *  Get / Lost / Hear
 * ------------------------------------------------------------------------ */
void GetCrystal(int crystal_label)
{                                                                       /* 76 */
    if (CRYSTAL_MAX <= crystal_label)                                   /* 80 */
    {
        PRINT_ASSERT("Error! GetCrystal crystal_label %d", crystal_label);
                                                                        /* 81 */
    }

    if (GetPlyrCrystalState(crystal_label) == CRYSTAL_STATE_NONE)       /* 87 */
    {
        plyr_crystal[crystal_label] = CRYSTAL_STATE_HAVE;
    }
}

void LostCrystal(int crystal_label)
{                                                                       /* 98 */
    if (CRYSTAL_MAX <= crystal_label)                                   /* 102 */
    {
        PRINT_ASSERT("Error! LostCrystal crystal_label %d", crystal_label);
                                                                        /* 103 */
    }

    /* No state test -- unlike FileLost(), this writes unconditionally. */
    plyr_crystal[crystal_label] = CRYSTAL_STATE_NONE;
}

void HearCrystal(int crystal_label)
{                                                                       /* 118 */
    if (CRYSTAL_MAX <= crystal_label)                                   /* 122 */
    {
        PRINT_ASSERT("Error! HearCrystal crystal_label %d", crystal_label);
                                                                        /* 123 */
    }

    if (GetPlyrCrystalState(crystal_label) != CRYSTAL_STATE_NONE)       /* 129 */
    {
        plyr_crystal[crystal_label] = CRYSTAL_STATE_HEARD;
    }
}

/* --------------------------------------------------------------------------
 *  Accessors
 * ------------------------------------------------------------------------ */
char GetPlyrCrystalState(int crystal_label)
{                                                                       /* 145 */
    if (CRYSTAL_MAX <= crystal_label)                                   /* 149 */
    {
        PRINT_ASSERT("Error! GetPlyrCrystalState crystal_label %d",
                     crystal_label);                                    /* 150 */
    }

    return plyr_crystal[crystal_label];
}

int GetCrystalStreamID(int crystal_label)
{                                                                       /* 164 */
    if (CRYSTAL_MAX <= crystal_label)                                   /* 168 */
    {
        PRINT_ASSERT("Error! GetCrystalStreamID crystal_label %d",
                     crystal_label);                                    /* 169 */
    }

    return crystal_stream[crystal_label];                               /* 175 */
}

MOVIE_TITLE_DAT *GetCrystalTitleDat(int crystal_label)
{                                                                       /* 183 */
    if (CRYSTAL_MAX <= crystal_label)                                   /* 187 */
    {
        PRINT_ASSERT("Error! GetCrystalTitleDat crystal_label %d",
                     crystal_label);                                    /* 188 */
    }

    return crystal_title_dat[crystal_label];                            /* 194 */
}

int GetPlyrHaveCrystalNum(void)
{                                                                       /* 201 */
    int i;
    int crystal_num;

    crystal_num = 0;                                                    /* 206 */

    for (i = 0; i < CRYSTAL_MAX; i++)                                   /* 209 */
    {
        if (plyr_crystal[i] != CRYSTAL_STATE_NONE)
        {
            crystal_num++;
        }
    }                                                                   /* 214 */

    return crystal_num;                                                 /* 217 */
}

/* --------------------------------------------------------------------------
 *  Save / debug
 * ------------------------------------------------------------------------ */
void SetSave_PlyrCrystal(MC_SAVE_DATA *data)
{                                                                       /* 228 */
    data->addr = (u_char *)&plyr_crystal[0];
    data->size = sizeof(plyr_crystal);                                  /* 232 */
}

/* Sets HAVE outright rather than going through GetCrystal(), so it also
 * downgrades anything already at HEARD. */
void DebugAllCrystalGet(void)
{                                                                       /* 244 */
    int i;

    for (i = 0; i < CRYSTAL_MAX; i++)                                   /* 250 */
    {
        plyr_crystal[i] = CRYSTAL_STATE_HAVE;
    }                                                                   /* 252 */
}
