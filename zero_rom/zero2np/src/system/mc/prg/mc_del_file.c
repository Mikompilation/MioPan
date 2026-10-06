/* ==========================================================================
 *  system/mc/prg/mc_del_file.c
 *
 *  sceMcDelete() wrapper (mc_del_file.o, .text 0x1e0120, 0x20c bytes).
 *
 *  "Already gone" is not a failure: result -4 (no entry) only logs a warning
 *  and still reports success, which is what lets MemoryCardAllFileDelMain()
 *  walk a directory listing and delete every entry without caring whether the
 *  listing went stale under it.
 *
 *  Unlike mc_open / mc_make_dir / mc_format, the busy path here does *not*
 *  spend a retry -- it only parks in step 2 to wait the other request out.
 *  That asymmetry is the ROM's; mc_check_card.c and mc_check_dir.c share it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_del_file.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert/Warning */

#include <string.h>

typedef struct                                  /* 0x44 */
{
    /* 0x00 */ char step;
    /* 0x01 */ char retry_cnt;
    /* 0x04 */ int  port;
    /* 0x08 */ int  slot;
    /* 0x0c */ char name[55];
} MC_FILE_DEL_CTRL;

static MC_FILE_DEL_CTRL mc_file_del_ctrl;       /* bss 4b4ce8 */

void MemoryCardFileDelInit(int port, int slot, char *name)              /* 65 */
{
    mc_file_del_ctrl.retry_cnt = 4;                                     /* 68 */
    mc_file_del_ctrl.step      = 0;                                     /* 69 */
    mc_file_del_ctrl.port      = port;                                  /* 70 */
    mc_file_del_ctrl.slot      = slot;                                  /* 71 */

    memset(mc_file_del_ctrl.name, 0, sizeof(mc_file_del_ctrl.name));     /* 72 */
    strcpy(mc_file_del_ctrl.name, name);                                /* 73 */
}

int MemoryCardFileDelMain(void)                                         /* 94 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 102 */
    result = 0;

    if (mc_file_del_ctrl.step == 0)                                     /* 106 */
    {
        mc_res = MemoryCardFileDelReq(mc_file_del_ctrl.port,             /* 108 */
                                      mc_file_del_ctrl.slot,
                                      mc_file_del_ctrl.name);

        if (mc_res == 0)                                                /* 111 */
        {
            mc_file_del_ctrl.step = 1;                                  /* 112 */
        }
        else if (mc_file_del_ctrl.retry_cnt > 0)                        /* 117 */
        {
            if (mc_res == -100)                                         /* 119 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 121 */
            }
            else if (mc_res == -210)                                    /* 124 */
            {
                MemoryCardAssert("Error! Directory Name Error!");        /* 126 */
            }
            else if (mc_res == -200)                                    /* 129 */
            {
                mc_file_del_ctrl.step = 2;                               /* 131 */
            }
            else
            {
                mc_file_del_ctrl.retry_cnt--;                            /* 136 */
            }
        }
        else
        {
            res = -10;                                                   /* 141 */
        }
    }

    if (mc_file_del_ctrl.step == 1)                                      /* 147 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 148 */

        if (mc_res == 1)                                                 /* 151 */
        {
            res = 1;

            if (result != 0)                                             /* 153 */
            {
                if (result == -4)                                        /* 157 */
                {
                    /* The entry was not there.  Nothing to do, and the caller
                     * gets its success -- res is already 1. */
                    MemoryCardWarning("Warning! MemoryCardFileDelMain");  /* 160 */
                }                                                        /* 161 */
                else
                {
                    switch (result)                                      /* 165 */
                    {
                    case -2:
                    case -5:
                    case -6:
                        res = result;                                    /* 170 */
                        break;

                    default:
                        res = -20;
                        break;
                    }
                }
            }
        }
        else if (mc_res == -1)                                           /* 177 */
        {
            mc_file_del_ctrl.step = 0;
        }
    }

    if (mc_file_del_ctrl.step == 2)                                      /* 184 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 185 */
        {
            mc_file_del_ctrl.step = 0;                                   /* 188 */
        }
    }

    return res;                                                          /* 195 */
}

int MemoryCardFileDelReq(int port, int slot, char *name)
{
    return sceMcDelete(port, slot, name);                                /* 213 */
}
