/* ==========================================================================
 *  system/mc/prg/mc_open.c
 *
 *  sceMcOpen() wrapper (mc_open.o, .text 0x1e1190, 0x1d4 bytes).
 *
 *  The three steps are not exclusive: step 0's block and step 1's block are
 *  consecutive `if`s, not an if/else, so a request issued this frame is polled
 *  in the same frame.  Step 2 exists for the sceMcErrBusy case -- something
 *  else's request is still running, so this one waits for *that* to finish
 *  before going back to step 0 and re-issuing.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_open.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

typedef struct                                  /* 0x14 */
{
    /* 0x00 */ char  step;
    /* 0x01 */ char  retry_cnt;
    /* 0x04 */ int   port;
    /* 0x08 */ int   slot;
    /* 0x0c */ int   mode;
    /* 0x10 */ char *name;
} MC_FILE_OPEN_CTRL;

static MC_FILE_OPEN_CTRL mc_file_open_ctrl;     /* bss 4b5230 */

/* Unlike its siblings this one keeps the caller's pointer rather than copying
 * the string -- the path always lives in the calling job's control block, which
 * outlives the open. */
void MemoryCardFileOpenInit(int port, int slot, char *name, int mode)
{
    mc_file_open_ctrl.step      = 0;                                    /* 69 */
    mc_file_open_ctrl.retry_cnt = 4;                                    /* 70 */
    mc_file_open_ctrl.port      = port;                                 /* 71 */
    mc_file_open_ctrl.slot      = slot;                                 /* 72 */
    mc_file_open_ctrl.name      = name;                                 /* 73 */
    mc_file_open_ctrl.mode      = mode;                                 /* 74 */
}

int MemoryCardFileOpenMain(int *fd)                                     /* 99 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 105 */
    result = 0;                                                        /* 107 */

    if (mc_file_open_ctrl.step == 0)                                    /* 111 */
    {
        *fd = -1;                                                       /* 113 */

        mc_res = MemoryCardFileOpenReq(mc_file_open_ctrl.port,          /* 116 */
                                       mc_file_open_ctrl.slot,
                                       mc_file_open_ctrl.name,
                                       mc_file_open_ctrl.mode);

        if (mc_res == 0)                                                /* 119 */
        {
            mc_file_open_ctrl.step = 1;                                 /* 120 */
        }
        else if (mc_file_open_ctrl.retry_cnt > 0)                       /* 125 */
        {
            if (mc_res == -100)                                         /* 127 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 129 */
            }
            else if (mc_res == -210)                                    /* 132 */
            {
                MemoryCardAssert("Error! Directory Name Error!");        /* 134 */
            }
            else if (mc_res == -200)                                     /* 137 */
            {
                mc_file_open_ctrl.step = 2;                              /* 139 */
                mc_file_open_ctrl.retry_cnt--;                           /* 140 */
            }
            else
            {
                mc_file_open_ctrl.retry_cnt--;                           /* 145 */
            }
        }
        else
        {
            res = -10;                                                   /* 150 */
        }
    }

    if (mc_file_open_ctrl.step == 1)                                     /* 156 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 157 */

        if (mc_res == 1)                                                 /* 160 */
        {
            if (result >= 0)                                             /* 162 */
            {
                res = 1;
                *fd = result;                                            /* 165 */
            }
            else
            {
                /* Pass the card's own diagnosis through for the codes the
                 * screens have a message for; everything else becomes the
                 * generic -20. */
                switch (result)                                          /* 169 */
                {
                case -2:
                case -3:
                case -4:
                case -5:
                case -7:
                    res = result;                                        /* 176 */
                    break;

                default:
                    res = -20;
                    break;
                }
            }
        }
        else if (mc_res == -1)                                           /* 183 */
        {
            mc_file_open_ctrl.step = 0;
        }
    }

    if (mc_file_open_ctrl.step == 2)                                     /* 190 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 191 */
        {
            mc_file_open_ctrl.step = 0;                                  /* 194 */
        }
    }

    return res;                                                          /* 201 */
}

int MemoryCardFileOpenReq(int port, int slot, char *name, int mode)
{
    return sceMcOpen(port, slot, name, mode);                            /* 219 */
}
