/* ==========================================================================
 *  system/mc/prg/mc_check.c
 *
 *  Card + directory check (mc_check.o, .text 0x1df0f8, 0x180 bytes).
 *
 *  Four steps: query the card, then list the directory.  Worth noting that a
 *  card-query result of -1 (the card was swapped since the last access) is
 *  *not* an error here -- it means the query did reach a card, so the check
 *  goes straight on to the listing.  Only the every-frame watch cares about the
 *  swap, and it reports it separately.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_check.h"

#include "mc_check_card.h"
#include "mc_check_dir.h"
#include "mc_set_data.h"                        /* MemoryCardAssert         */

#include <string.h>

typedef struct                                  /* 0x44 */
{
    /* 0x00 */ char step;
    /* 0x04 */ int  port;
    /* 0x08 */ int  slot;
    /* 0x0c */ char name[55];
} MC_CHECK_CTRL;

static MC_CHECK_CTRL mc_check_ctrl;             /* bss 4b4708 */

void MemoryCardCheckInit(int port, int slot, char *name)                 /* 67 */
{
    mc_check_ctrl.step = 0;                                             /* 70 */
    mc_check_ctrl.port = port;                                          /* 71 */
    mc_check_ctrl.slot = slot;                                          /* 72 */

    memset(mc_check_ctrl.name, 0, sizeof(mc_check_ctrl.name));           /* 73 */
    strcpy(mc_check_ctrl.name, name);                                   /* 74 */
}

int MemoryCardCheckMain(void)                                           /* 97 */
{
    int res;
    int mc_res;

    res = 0;                                                           /* 102 */

    switch (mc_check_ctrl.step)                                         /* 106 */
    {
    case 0:
        MemoryCardGetCardInfoInit(mc_check_ctrl.port,                    /* 108 */
                                  mc_check_ctrl.slot);
        mc_check_ctrl.step = 1;                                         /* 109 */
        /* fall through */

    case 1:
        mc_res = MemoryCardGetCardInfoMain();                            /* 112 */

        if (mc_res != 1)                                                /* 115 */
        {
            if (mc_res >= 0)                                            /* 119 */
            {
                break;                                                  /* still working */
            }

            if (mc_res != -1)                                           /* 121 */
            {
                res = mc_res;
                break;
            }
        }

        /* Reached the card -- swapped or not -- so go on to the listing. */
        mc_check_ctrl.step = 2;                                         /* 123 */
        break;

    case 2:
        MemoryCardGetDirInfoInit(mc_check_ctrl.port, mc_check_ctrl.slot,  /* 132 */
                                 mc_check_ctrl.name);
        mc_check_ctrl.step = 3;                                         /* 133 */
        /* fall through */

    case 3:
        mc_res = MemoryCardGetDirInfoMain();                             /* 136 */

        if (mc_res == 1)                                                /* 139 */
        {
            res = 1;                                                    /* 140 */
        }
        else if (mc_res < 0)                                            /* 143 */
        {
            res = mc_res;                                               /* 147 */
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardCheckMain");                   /* 149 */
        break;
    }

    return res;                                                         /* 153 */
}
