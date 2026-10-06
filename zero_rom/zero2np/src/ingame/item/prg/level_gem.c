// FILE: /home/zero_rom/zero2np/src/ingame/item/prg/level_gem.c
//
// Level gem count.  The smallest module in the item system: one `char` of
// state and six one-liners around it.
//
// Nothing here range-checks an index because there is no index -- the two
// mutators just saturate.  GetLevelGem() stops at 99; LostLevelGem() lets the
// count go negative, notices, warns and clamps, rather than testing first.
//
// level_gem.o's .sdata also holds ten 4-byte compiler temporaries
// (_$tmp_0.692 .. _$tmp_9.776) ahead of the fixed_array boilerplate.  Those
// are GCC scratch slots, not source variables; plyr_level_gem is the only
// state the file declares.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "level_gem.h"

#include "../../../common/utility2.h"   /* PRINT_WARNING */

#include <stdio.h>

static char plyr_level_gem;                                         /* sbss 3f4d58 */

void PlyrLevelGemInit(void)
{                                                                       /* 39 */
    plyr_level_gem = 0;                                                 /* 42 */
}

void GetLevelGem(void)
{                                                                       /* 59 */
    plyr_level_gem++;                                                   /* 62 */

    if (LEVEL_GEM_MAX < plyr_level_gem)                                 /* 64 */
    {
        plyr_level_gem = LEVEL_GEM_MAX;                                 /* 65 */
    }
}

/* The decrement is committed before the test, so the count really does sit at
 * -1 for the length of the warning.  Kept in that order -- it is what the ROM
 * emits, and the clamp puts it back either way. */
void LostLevelGem(void)
{                                                                       /* 74 */
    plyr_level_gem--;                                                   /* 77 */

    if (plyr_level_gem < 0)                                             /* 79 */
    {
        PRINT_WARNING("Warning! %s\n", __FUNCTION__);                   /* 80 */

        plyr_level_gem = 0;                                             /* 81 */
    }
}

char GetPlyrLevelGemNum(void)
{                                                                       /* 94 */
    return plyr_level_gem;                                              /* 98 */
}

void SetSave_PlyrLevelGem(MC_SAVE_DATA *data)
{                                                                       /* 109 */
    data->addr = (u_char *)&plyr_level_gem;                             /* 112 */
    data->size = sizeof(plyr_level_gem);                                /* 113 */
}

void DebugSetLevelGemMaxNum(void)
{                                                                       /* 124 */
    plyr_level_gem = LEVEL_GEM_MAX;                                     /* 128 */
}
