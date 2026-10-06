/* ==========================================================================
 *  system/mc/prg/mc_make_dir.c
 *
 *  sceMcMkdir() wrapper (mc_make_dir.o, .text 0x1e0e80, 0x1cc bytes).
 *
 *  The only primitive with no per-code error mapping: any non-zero result
 *  becomes the generic -20.  Its .text carries a ten-line gap where every
 *  sibling has its switch, so the distinction was either dropped or never
 *  written -- there is no code left to recover.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_make_dir.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

#include <string.h>

typedef struct                                  /* 0x24 */
{
    /* 0x00 */ char step;
    /* 0x01 */ char retry_cnt;
    /* 0x04 */ int  port;
    /* 0x08 */ int  slot;
    /* 0x0c */ char name[21];
} MC_MAKE_DIR_CTRL;

static MC_MAKE_DIR_CTRL mc_make_dir_ctrl;       /* bss 4b51b8 */

void MemoryCardMakeNewDirInit(int port, int slot, char *name)           /* 65 */
{
    mc_make_dir_ctrl.retry_cnt = 4;                                     /* 68 */
    mc_make_dir_ctrl.step      = 0;                                     /* 69 */
    mc_make_dir_ctrl.port      = port;                                  /* 70 */
    mc_make_dir_ctrl.slot      = slot;                                  /* 71 */

    memset(mc_make_dir_ctrl.name, 0, sizeof(mc_make_dir_ctrl.name));     /* 72 */
    strcpy(mc_make_dir_ctrl.name, name);                                /* 73 */
}

int MemoryCardMakeNewDirMain(void)                                      /* 95 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 103 */
    result = 0;

    if (mc_make_dir_ctrl.step == 0)                                     /* 107 */
    {
        mc_res = MemoryCardMakeNewDirReq(mc_make_dir_ctrl.port,           /* 108 */
                                         mc_make_dir_ctrl.slot,
                                         mc_make_dir_ctrl.name);

        if (mc_res == 0)                                                /* 111 */
        {
            mc_make_dir_ctrl.step = 1;                                  /* 112 */
        }
        else if (mc_make_dir_ctrl.retry_cnt > 0)                        /* 117 */
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
                mc_make_dir_ctrl.step = 2;                               /* 131 */
                mc_make_dir_ctrl.retry_cnt--;                            /* 132 */
            }
            else
            {
                mc_make_dir_ctrl.retry_cnt--;                            /* 137 */
            }
        }
        else
        {
            res = -10;                                                   /* 142 */
        }
    }

    if (mc_make_dir_ctrl.step == 1)                                      /* 148 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 149 */

        if (mc_res == 1)                                                 /* 152 */
        {
            res = 1;

            if (result != 0)                                             /* 154 */
            {
                res = -20;                                               /* 166 */
            }
        }
        else if (mc_res == -1)                                           /* 171 */
        {
            mc_make_dir_ctrl.step = 0;
        }
    }

    if (mc_make_dir_ctrl.step == 2)                                      /* 178 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 179 */
        {
            mc_make_dir_ctrl.step = 0;                                   /* 182 */
        }
    }

    return res;                                                          /* 189 */
}

int MemoryCardMakeNewDirReq(int port, int slot, char *name)
{
    return sceMcMkdir(port, slot, name);                                 /* 207 */
}
