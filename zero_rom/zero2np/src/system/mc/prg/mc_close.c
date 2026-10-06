/* ==========================================================================
 *  system/mc/prg/mc_close.c
 *
 *  sceMcClose() wrapper (mc_close.o, .text 0x1dfb20, 0x164 bytes).
 *
 *  The narrowest of the primitives: no path, no buffer, and its control block
 *  is small enough (8 bytes) that the linker put it in .sbss rather than .bss.
 *  It is also the only primitive both mc_read and mc_write call directly -- a
 *  failed transfer still has to give the descriptor back.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_close.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

typedef struct                                  /* 0x8 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char retry_cnt;
    /* 0x4 */ int  fd;
} MC_FILE_CLOSE_CTRL;

static MC_FILE_CLOSE_CTRL mc_file_close_ctrl;   /* sbss 3f4da8 */

void MemoryCardFileCloseInit(int fd)
{
    mc_file_close_ctrl.step      = 0;                                   /* 63 */
    mc_file_close_ctrl.retry_cnt = 4;                                   /* 64 */
    mc_file_close_ctrl.fd        = fd;                                  /* 65 */
}

int MemoryCardFileCloseMain(void)                                       /* 86 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 94 */
    result = 0;

    if (mc_file_close_ctrl.step == 0)                                   /* 98 */
    {
        mc_res = MemoryCardFileCloseReq(mc_file_close_ctrl.fd);          /* 99 */

        if (mc_res == 0)                                                /* 102 */
        {
            mc_file_close_ctrl.step = 1;                                /* 103 */
        }
        else if (mc_file_close_ctrl.retry_cnt > 0)                      /* 108 */
        {
            if (mc_res == -100)                                         /* 110 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 112 */
            }
            else if (mc_res == -200)                                    /* 115 */
            {
                mc_file_close_ctrl.step = 2;                             /* 117 */
                mc_file_close_ctrl.retry_cnt--;                          /* 118 */
            }
            else
            {
                mc_file_close_ctrl.retry_cnt--;                          /* 123 */
            }
        }
        else
        {
            res = -10;                                                   /* 128 */
        }
    }

    if (mc_file_close_ctrl.step == 1)                                    /* 134 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 135 */

        if (mc_res == 1)                                                 /* 138 */
        {
            res = 1;

            if (result != 0)                                             /* 140 */
            {
                switch (result)                                          /* 145 */
                {
                case -2:
                case -4:
                case -5:
                    res = result;                                        /* 150 */
                    break;

                default:
                    res = -20;
                    break;
                }
            }
        }
        else if (mc_res == -1)                                           /* 157 */
        {
            mc_file_close_ctrl.step = 0;
        }
    }

    if (mc_file_close_ctrl.step == 2)                                    /* 164 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 165 */
        {
            mc_file_close_ctrl.step = 0;                                 /* 168 */
        }
    }

    return res;                                                          /* 175 */
}

int MemoryCardFileCloseReq(int fd)
{
    return sceMcClose(fd);                                               /* 191 */
}
