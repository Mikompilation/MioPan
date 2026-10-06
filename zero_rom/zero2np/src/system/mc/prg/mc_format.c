/* ==========================================================================
 *  system/mc/prg/mc_format.c
 *
 *  sceMcFormat() wrapper (mc_format.o, .text 0x1e0350, 0x174 bytes).
 *
 *  The only primitive with no -210 branch, because a format takes no name.
 *  Its single interesting result is -5 (denied): that is the one the "cannot
 *  format this card" message hangs off, so it passes through while every other
 *  failure collapses to the generic -20.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_format.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

typedef struct                                  /* 0xc */
{
    /* 0x0 */ char step;
    /* 0x1 */ char retry_cnt;
    /* 0x4 */ int  port;
    /* 0x8 */ int  slot;
} MC_FORMAT_CTRL;

static MC_FORMAT_CTRL mc_format_ctrl;           /* bss 4b4d30 */

void MemoryCardFormatInit(int port, int slot)
{
    mc_format_ctrl.step      = 0;                                       /* 65 */
    mc_format_ctrl.retry_cnt = 4;                                       /* 66 */
    mc_format_ctrl.port      = port;                                    /* 67 */
    mc_format_ctrl.slot      = slot;                                    /* 68 */
}

int MemoryCardFormatMain(void)                                          /* 87 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 95 */
    result = 0;

    if (mc_format_ctrl.step == 0)                                       /* 99 */
    {
        mc_res = MemoryCardFormatReq(mc_format_ctrl.port,                /* 100 */
                                     mc_format_ctrl.slot);

        if (mc_res == 0)                                                /* 103 */
        {
            mc_format_ctrl.step = 1;                                    /* 104 */
        }
        else if (mc_format_ctrl.retry_cnt > 0)                          /* 109 */
        {
            if (mc_res == -100)                                         /* 111 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 113 */
            }
            else if (mc_res == -200)                                    /* 116 */
            {
                mc_format_ctrl.step = 2;                                 /* 118 */
                mc_format_ctrl.retry_cnt--;                              /* 119 */
            }
            else
            {
                mc_format_ctrl.retry_cnt--;                              /* 124 */
            }
        }
        else
        {
            res = -10;                                                   /* 129 */
        }
    }

    if (mc_format_ctrl.step == 1)                                        /* 135 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 136 */

        if (mc_res == 1)                                                 /* 139 */
        {
            res = 1;

            if (result != 0)                                             /* 141 */
            {
                if (result == -5)
                {
                    res = result;                                        /* 149 */
                }
                else
                {
                    res = -20;                                           /* 146 */
                }
            }
        }
        else if (mc_res == -1)                                           /* 156 */
        {
            mc_format_ctrl.step = 0;
        }
    }

    if (mc_format_ctrl.step == 2)                                        /* 163 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 164 */
        {
            mc_format_ctrl.step = 0;                                     /* 167 */
        }
    }

    return res;                                                          /* 174 */
}

int MemoryCardFormatReq(int port, int slot)
{
    return sceMcFormat(port, slot);                                      /* 191 */
}
