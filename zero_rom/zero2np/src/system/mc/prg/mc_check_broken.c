/* ==========================================================================
 *  system/mc/prg/mc_check_broken.c
 *
 *  Integrity tests (mc_check_broken.o, .text 0x1df218, 0x200 bytes).
 *
 *  The stored checksum is read out byte-by-byte rather than as an int, because
 *  it sits at buffer + size - 4 and `size` is the sum of arbitrary save-block
 *  lengths -- it is not word-aligned in general.  Both file tests do the same
 *  four-byte gather, which is why they are near-identical functions.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_check_broken.h"

#include "mc_check_dir.h"                       /* GetMemoryCardCheckDir*   */
#include "mc_cmn.h"                             /* CalcMemoryCardDataCheckSum */
#include "mc_set_data.h"                        /* GetMemoryCard*Num / Print */

/* --------------------------------------------------------------------------
 *  MemoryCardCheckDirBroken
 *
 *  Two comparisons against the card's own directory: the entry count and the
 *  cluster cost.  Both come out equal for an intact directory -- see the note
 *  on GetMemoryCardCheckDirSize() for why the arithmetic agrees exactly.
 * ------------------------------------------------------------------------ */
int MemoryCardCheckDirBroken(int dir_label)                              /* 42 */
{
    int res;

    res = 0;

    if (GetMemoryCardAllFileNum(dir_label) == GetMemoryCardCheckDirFileNum()) /* 48 */
    {
        res = (GetMemoryCardDirSizeCluster(dir_label) ==                 /* 50 */
               GetMemoryCardCheckDirSize());
    }

    return res;                                                         /* 56 */
}

int MemoryCardCheckFileBroken(void *data_addr, int size)                /* 67 */
{
    int   i;
    int   res;
    int   check_sum;
    int   load_check_sum;
    char *addr;
    char *check_sum_addr;

    load_check_sum = 0;
    check_sum_addr = (char *)&load_check_sum;                           /* 79 */

    addr = (char *)data_addr;                                           /* 82 */
    size = size - 4;                                                    /* 84 */

    check_sum = CalcMemoryCardDataCheckSum((char *)data_addr, size);      /* 88 */

    addr = addr + size;                                                 /* 92 */

    for (i = 3; i >= 0; i--)                                            /* 93 */
    {
        *check_sum_addr = *addr;                                        /* 94 */
        check_sum_addr++;
        addr++;
    }                                                                   /* 95 */

    MemoryCardPrint("load check sum %d data check sum %d\n",            /* 97 */
                    load_check_sum, check_sum);

    res = (check_sum == load_check_sum);                                /* 100 */

    /* A file that MemoryCardAllFileMakeMain() created and nothing has written
     * yet: all zeroes, so the sum is 0, with -1 stamped into the checksum.  It
     * is valid, just empty. */
    if (check_sum == 0 && load_check_sum == -1)                          /* 105 */
    {
        res = 1;
    }

    return res;                                                         /* 110 */
}

/* --------------------------------------------------------------------------
 *  MemoryCardCheckNewFileLoad
 *
 *  The same gather, but the answer is "is this file still untouched": checksum
 *  -1 with a sum of 0, and then a byte-by-byte scan that rules the file out the
 *  moment it finds anything non-zero.  The scan is what makes this stricter
 *  than the test above rather than a duplicate of it.
 * ------------------------------------------------------------------------ */
int MemoryCardCheckNewFileLoad(void *data_addr, int size)               /* 121 */
{
    int   i;
    int   res;
    int   check_sum;
    int   load_check_sum;
    char *addr;
    char *check_sum_addr;

    res            = 0;
    load_check_sum = 0;
    check_sum_addr = (char *)&load_check_sum;                           /* 130 */

    addr = (char *)data_addr;                                           /* 133 */
    size = size - 4;                                                    /* 136 */

    check_sum = CalcMemoryCardDataCheckSum((char *)data_addr, size);      /* 138 */

    addr = addr + size;                                                 /* 142 */

    for (i = 3; i >= 0; i--)                                            /* 146 */
    {
        *check_sum_addr = *addr;                                        /* 147 */
        check_sum_addr++;
        addr++;
    }                                                                   /* 148 */

    MemoryCardPrint("load check sum %d data check sum %d\n",            /* 151 */
                    load_check_sum, check_sum);

    if (check_sum == 0)                                                 /* 154 */
    {
        res = (load_check_sum == -1);
    }

    for (i = 0; i < size; i++)                                          /* 161 */
    {
        if (((char *)data_addr)[i] != 0)                                /* 162 */
        {
            res = 0;                                                    /* 163 */
            break;
        }
    }                                                                   /* 164 */

    return res;                                                         /* 171 */
}
