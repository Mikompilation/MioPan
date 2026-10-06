/* ==========================================================================
 *  system/mc/prg/mc.c
 *
 *  Memory-card subsystem brackets (mc.o, .text 0x1df010, 0x84 bytes).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc.h"

#include "libmc.h"
#include "mc_check_card.h"                      /* MemoryCardInfoCtrlInit   */
#include "mc_icon.h"                            /* LiberateMemoryCardIconDataMem */
#include "mc_set_data.h"                        /* SoftReset lock / head init */

void MemoryCardInit(void)
{
    sceMcInit();                                                        /* 48 */
}

void MemoryCardExeInit(void)
{
    MemoryCardSoftResetLock();                                          /* 61 */
    MemoryCardInfoCtrlInit();                                           /* 63 */
    MemoryCardPlayDataHeadInit();                                       /* 65 */
}

void MemoryCardEnd(void)
{
    LiberateMemoryCardIconDataMem();                                    /* 81 */

    /* mode 0 is the blocking wait, and the only place the folder uses it: a
     * screen must not hand the card back with a write still in flight.  Both
     * out-parameters are NULL because nothing is left to read the result. */
    sceMcSync(0, (int *)0, (int *)0);                                   /* 84 */

    MemoryCardSoftResetUnlock();                                        /* 87 */
}
