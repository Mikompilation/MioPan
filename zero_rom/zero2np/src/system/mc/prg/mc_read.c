/* ==========================================================================
 *  system/mc/prg/mc_read.c
 *
 *  sceMcRead() wrapper (mc_read.o, .text 0x1e1368, 0x1dc bytes).
 *
 *  Two steps more than the other primitives, because a failed read leaves an
 *  open descriptor: step 2 closes it and only then hands the stashed `error`
 *  back to the caller.  That is what `error` is for -- the real result has to
 *  survive the close.
 *
 *  A short read is a failure, not a partial success (error -3), which is how a
 *  file truncated by a card pulled mid-write is caught before its contents
 *  reach the game state.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_read.h"

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
} MC_FILE_READ_CTRL;

static MC_FILE_READ_CTRL mc_file_read_ctrl;     /* bss 4b5248 */

void MemoryCardFileReadInit(int fd, void *data_addr, int size)
{
    mc_file_read_ctrl.step      = 0;                                    /* 70 */
    mc_file_read_ctrl.retry_cnt = 4;                                    /* 71 */
    mc_file_read_ctrl.fd        = fd;                                   /* 72 */
    mc_file_read_ctrl.size      = size;                                 /* 73 */
    mc_file_read_ctrl.data_addr = data_addr;                            /* 74 */
    mc_file_read_ctrl.error     = 0;                                    /* 75 */
}

int MemoryCardFileReadMain(void)                                        /* 97 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 105 */
    result = 0;

    if (mc_file_read_ctrl.step == 0)                                    /* 109 */
    {
        mc_res = MemoryCardFileReadReq(mc_file_read_ctrl.fd,             /* 111 */
                                       mc_file_read_ctrl.data_addr,
                                       mc_file_read_ctrl.size);

        if (mc_res == 0)                                                /* 114 */
        {
            mc_file_read_ctrl.step = 1;                                 /* 115 */
        }
        else if (mc_file_read_ctrl.retry_cnt > 0)                       /* 120 */
        {
            if (mc_res == -100)                                         /* 122 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 124 */
            }
            else if (mc_res == -200)                                    /* 127 */
            {
                mc_file_read_ctrl.step = 3;                              /* 129 */
            }
            else
            {
                mc_file_read_ctrl.retry_cnt--;                           /* 134 */
            }
        }
        else
        {
            /* Out of retries: shut the descriptor and report through step 2. */
            mc_file_read_ctrl.error = 1;                                 /* 139 */
            MemoryCardFileCloseInit(mc_file_read_ctrl.fd);               /* 141 */
            mc_file_read_ctrl.step  = 2;                                 /* 142 */
        }
    }

    if (mc_file_read_ctrl.step == 1)                                     /* 148 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 149 */

        if (mc_res == 1)                                                 /* 152 */
        {
            if (result >= 0)                                             /* 154 */
            {
                if (mc_file_read_ctrl.size == result)                    /* 157 */
                {
                    res = 1;                                             /* 158 */
                }
                else
                {
                    /* Short read.  -3 is the code the load screens map to
                     * their own "data damaged" message. */
                    mc_file_read_ctrl.error = -3;                        /* 162 */
                    MemoryCardFileCloseInit(mc_file_read_ctrl.fd);       /* 164 */
                    mc_file_read_ctrl.step  = 2;                         /* 165 */
                }
            }
            else
            {
                switch (result)                                          /* 170 */
                {
                case -2:
                case -3:
                case -4:
                case -5:
                    res = result;                                        /* 176 */
                    break;

                default:
                    res = -20;                                           /* 178 */
                    break;
                }
            }
        }
        else if (mc_res == -1)                                           /* 183 */
        {
            mc_file_read_ctrl.step = 0;
        }
    }

    if (mc_file_read_ctrl.step == 2)                                     /* 190 */
    {
        if (MemoryCardFileCloseMain() != 0)                              /* 191 */
        {
            res = mc_file_read_ctrl.error;                               /* 194 */
        }
    }

    if (mc_file_read_ctrl.step == 3)                                     /* 200 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 201 */
        {
            mc_file_read_ctrl.step = 0;                                  /* 204 */
        }
    }

    return res;                                                          /* 211 */
}

int MemoryCardFileReadReq(int fd, void *data_addr, int size)
{
    return sceMcRead(fd, data_addr, size);                               /* 229 */
}
