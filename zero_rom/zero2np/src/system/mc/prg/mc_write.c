/* ==========================================================================
 *  system/mc/prg/mc_write.c
 *
 *  sceMcWrite() wrapper (mc_write.o, .text 0x1e2db0, 0x1c4 bytes).
 *
 *  mc_read.c's twin, with two differences worth noting.  The out-of-retries
 *  path stashes -10 rather than 1, so an exhausted write reports the same code
 *  a rejected request does.  And a short write is *not* an error: any
 *  result >= 0 is success, unlike the read which insists on the full count --
 *  the card reports a full device through result -3 instead.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_write.h"

#include "libmc.h"
#include "mc_close.h"                           /* MemoryCardFileClose*     */
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

typedef struct                                  /* 0x14 */
{
    /* 0x00 */ char  step;
    /* 0x01 */ char  retry_cnt;
    /* 0x04 */ int   fd;
    /* 0x08 */ int   size;
    /* 0x0c */ int   error;
    /* 0x10 */ void *data_addr;
} MC_FILE_WRITE_CTRL;

static MC_FILE_WRITE_CTRL mc_file_write_ctrl;   /* bss 4b5338 */

void MemoryCardFileWriteInit(int fd, void *data_addr, int size)
{
    mc_file_write_ctrl.step      = 0;                                   /* 70 */
    mc_file_write_ctrl.retry_cnt = 4;                                   /* 71 */
    mc_file_write_ctrl.fd        = fd;                                  /* 72 */
    mc_file_write_ctrl.size      = size;                                /* 73 */
    mc_file_write_ctrl.data_addr = data_addr;                           /* 74 */
    mc_file_write_ctrl.error     = 0;                                   /* 75 */
}

int MemoryCardFileWriteMain(void)                                       /* 98 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 106 */
    result = 0;

    if (mc_file_write_ctrl.step == 0)                                   /* 110 */
    {
        mc_res = MemoryCardFileWriteReq(mc_file_write_ctrl.fd,           /* 112 */
                                        mc_file_write_ctrl.data_addr,
                                        mc_file_write_ctrl.size);

        if (mc_res == 0)                                                /* 115 */
        {
            mc_file_write_ctrl.step = 1;                                /* 116 */
        }
        else if (mc_file_write_ctrl.retry_cnt > 0)                      /* 121 */
        {
            if (mc_res == -100)                                         /* 123 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 125 */
            }
            else if (mc_res == -200)                                    /* 128 */
            {
                mc_file_write_ctrl.step = 3;                             /* 130 */
            }
            else
            {
                mc_file_write_ctrl.retry_cnt--;                           /* 135 */
            }
        }
        else
        {
            mc_file_write_ctrl.error = -10;                               /* 140 */
            MemoryCardFileCloseInit(mc_file_write_ctrl.fd);               /* 142 */
            mc_file_write_ctrl.step  = 2;                                /* 143 */
        }
    }

    if (mc_file_write_ctrl.step == 1)                                    /* 149 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 150 */

        if (mc_res == 1)                                                 /* 153 */
        {
            res = 1;

            if (result < 0)                                              /* 155 */
            {
                switch (result)                                          /* 160 */
                {
                case -2:
                case -3:
                case -4:
                case -5:
                case -8:
                    res = result;                                        /* 167 */
                    break;

                default:
                    res = -20;
                    break;
                }
            }
        }
        else if (mc_res == -1)                                           /* 174 */
        {
            mc_file_write_ctrl.step = 0;
        }
    }

    if (mc_file_write_ctrl.step == 2)                                    /* 181 */
    {
        if (MemoryCardFileCloseMain() != 0)                              /* 182 */
        {
            res = mc_file_write_ctrl.error;                               /* 185 */
        }
    }

    if (mc_file_write_ctrl.step == 3)                                    /* 191 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 192 */
        {
            mc_file_write_ctrl.step = 0;                                 /* 195 */
        }
    }

    return res;                                                          /* 202 */
}

int MemoryCardFileWriteReq(int fd, void *data_addr, int size)
{
    return sceMcWrite(fd, data_addr, size);                              /* 220 */
}
