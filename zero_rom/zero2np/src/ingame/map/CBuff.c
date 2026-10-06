// FILE: /home/zero_rom/zero2np/src/ingame/map/CBuff.c
//
// Deduplicating string table, used by FurnLoad.c to collect the distinct model
// names a room needs.  Storage is one flat block of max_num fixed-width slots
// rather than a pointer array, so a slot's address is pure arithmetic and the
// whole table is one allocation and one free.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), CBuff.o
// 0x00100258..0x001004cb.

#include "CBuff.h"

#include "../../common/heapctrl.h"      /* SAFE_MALLOC / heapCtrlFree */
#include "../../system/os/system.h"     /* GetSystemHeapWrkP */

#include <string.h>

static int   CBuffNum;                                                  /* sdata 3eeac0 */
static int   CBuffMaxLen;                                               /* sdata 3eeac4 */
static int   CBuffMaxNum;                                               /* sdata 3eeac8 */
static char *CBuffStr;                                                  /* sbss 3f4a80 */

int CBuffGetRegistNum(void)
{
    return CBuffNum;
}

char *CBuffGetStr(int id)                                               /* 33 */
{
    if ((id >= 0) && (id < CBuffNum))                                   /* 34 */
    {
        return &CBuffStr[CBuffMaxLen * id];                             /* 35 */
    }

    return (char *)0;
}

/* Linear scan -- the table is tens of entries, and it is only walked while a
 * room is being registered.
 *
 * NOTE: there is no check against CBuffMaxNum before the append.  The only
 * caller, FurnLoadRegID(), sizes the table at a flat 512 entries and trusts no
 * room names more distinct models than that, so an overflow is a content error
 * rather than a runtime condition.  Kept as-is: a guard here would silently
 * drop a model instead of tripping the way the original does. */
int CBuffSetStr(char *str)                                              /* 41 */
{
    for (int i = 0; i < CBuffNum; i++)                                  /* 44 */
    {
        if (strcmp(&CBuffStr[CBuffMaxLen * i], str) == 0)               /* 45 */
        {
            return 1;                                                   /* 46 */
        }
    }

    /* The scan above always leaves i == CBuffNum, which is the append slot. */
    strcpy(&CBuffStr[CBuffMaxLen * CBuffNum], str);                     /* 49 */
    CBuffNum++;                                                         /* 50 */

    return 0;                                                           /* 51 */
}

/* Only the first byte of each slot is cleared -- every entry is a C string, so
 * truncating it to empty is enough, and this stays O(max_num) rather than
 * touching the whole block. */
void CBuffReset(void)                                                   /* 62 */
{
    for (int i = 0; i < CBuffMaxNum; i++)                               /* 63 */
    {
        CBuffStr[CBuffMaxLen * i] = '\0';                               /* 64 */
    }

    CBuffNum = 0;                                                       /* 65 */
}

void CBuffInit(int max_len, int max_num)
{
    CBuffStr = (char *)SAFE_MALLOC(GetSystemHeapWrkP(), (void *)0,      /* 72 */
                                   max_len * max_num);

    CBuffMaxLen = max_len;                                              /* 76 */
    CBuffMaxNum = max_num;

    CBuffReset();                                                       /* 78 */
}

/* NOTE: CBuffNum / CBuffMaxLen / CBuffMaxNum are deliberately left alone --
 * only the pointer is cleared.  Calling CBuffGetStr() after CBuffTerm() and
 * before the next CBuffInit() therefore indexes off a null CBuffStr.  Every
 * caller pairs Init and Term around one room load, so the window never opens
 * in practice; this matches the ROM. */
void CBuffTerm(void)                                                    /* 83 */
{
    if (CBuffStr != (char *)0)                                          /* 84 */
    {
        heapCtrlFree(GetSystemHeapWrkP(), CBuffStr);
        CBuffStr = (char *)0;
    }
}
