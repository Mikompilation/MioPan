/* ==========================================================================
 *  system/mc/prg/mc_cmn.c
 *
 *  Shared memory-card helpers (mc_cmn.o, .text 0x1dfc88, 0xac bytes).
 *
 *  MemoryCardExeEndSync() is the busiest function in the folder -- eighteen
 *  call sites -- because it is the polling half of libmc's async handshake and
 *  every step machine in system/mc runs on it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_cmn.h"

#include "libmc.h"
#include "mc_check_card.h"                      /* GetAccessMemoryCardFreeCluster */

/* --------------------------------------------------------------------------
 *  MemoryCardExeEndSync
 *
 *  mode 1 is the non-blocking poll, so this never stalls the frame.  `cmd` is
 *  passed as NULL: the game never cares which function completed, only that
 *  one did, because a single request is outstanding at a time.
 * ------------------------------------------------------------------------ */
int MemoryCardExeEndSync(int *result)
{
    return sceMcSync(1, (int *)0, result);                               /* 48 */
}

/* --------------------------------------------------------------------------
 *  GetMemoryCardFreeSizeForBrowser
 *
 *  Two clusters below the card's real free count.  That is what the browser
 *  itself reserves to create a directory, so quoting the raw figure would let
 *  the player start a save that cannot be finished.
 * ------------------------------------------------------------------------ */
int GetMemoryCardFreeSizeForBrowser(void)                               /* 51 */
{
    int size;

    size = GetAccessMemoryCardFreeCluster() - 2;                        /* 66 */

    if (size < 0)                                                       /* 68 */
    {
        size = 0;
    }

    return size;                                                        /* 73 */
}

/* --------------------------------------------------------------------------
 *  CalcMemoryCardDataCheckSum
 *
 *  The plain sum of `size` bytes, read as *signed* char (the ROM uses `lb`),
 *  accumulated into an unsigned int and returned as int.  Deliberately weak:
 *  it only has to catch a half-written file, and MemoryCardCheckFileBroken()
 *  additionally treats sum 0 with a stored -1 as "made but never written".
 * ------------------------------------------------------------------------ */
int CalcMemoryCardDataCheckSum(char *data_addr, int size)
{
    u_int check_sum;
    int   i;

    check_sum = 0;

    for (i = 0; i < size; i++)                                          /* 93 */
    {
        check_sum += *data_addr;                                        /* 94 */
        data_addr++;
    }                                                                   /* 95 */

    return check_sum;                                                   /* 98 */
}

/* --------------------------------------------------------------------------
 *  SetMemoryCardDataCheckSum
 *
 *  Copies the four bytes of `check_sum` out byte-by-byte rather than storing
 *  an int, so it does not care whether `addr` is word-aligned -- and it never
 *  is: it points at the last four bytes of a file whose length is the sum of
 *  the save blocks.  Byte order therefore follows the host's, which on the EE
 *  meant little-endian.
 * ------------------------------------------------------------------------ */
void SetMemoryCardDataCheckSum(char *addr, int check_sum)               /* 106 */
{
    char *check_sum_addr;
    int   i;

    check_sum_addr = (char *)&check_sum;                                /* 111 */

    for (i = 3; i >= 0; i--)                                            /* 115 */
    {
        *addr = *check_sum_addr;                                        /* 116 */
        addr++;
        check_sum_addr++;
    }                                                                   /* 117 */
}
