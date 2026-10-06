/* ==========================================================================
 *  system/mc/prg/mc_check_empty.c
 *
 *  Free-space tests (mc_check_empty.o, .text 0x1dfaa0, 0x80 bytes).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_check_empty.h"

#include "mc_check_card.h"                      /* GetAccessMemoryCardFreeCluster */
#include "mc_check_dir.h"                       /* GetMemoryCardCheckDirSize      */
#include "mc_set_data.h"                        /* GetMemoryCardDirSizeCluster    */

int MemoryCardCheckEmpty(int dir_label)                                 /* 45 */
{
    return !(GetAccessMemoryCardFreeCluster() <
             GetMemoryCardDirSizeCluster(dir_label));                    /* 50 */
}

/* The directory being replaced is about to be deleted, so its clusters are
 * available too -- which is why a re-save fits where a first save would not. */
int MemoryCardCheckEmptyBroken(int dir_label)
{
    int empty_size;

    empty_size = GetMemoryCardCheckDirSize() +                          /* 68 */
                 GetAccessMemoryCardFreeCluster();

    return !(empty_size < GetMemoryCardDirSizeCluster(dir_label));       /* 71 */
}                                                                       /* 76 */
