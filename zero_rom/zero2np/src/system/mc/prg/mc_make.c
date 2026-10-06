/* ==========================================================================
 *  system/mc/prg/mc_make.c
 *
 *  Create a whole card directory (mc_make.o, .text 0x1e0a78, 0x180 bytes).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_make.h"

#include "mc_make_all_file.h"
#include "mc_make_dir.h"
#include "mc_set_data.h"                        /* MemoryCardSetDirName     */

#include <string.h>

typedef struct                                  /* 0x18 */
{
    /* 0x00 */ char  step;
    /* 0x04 */ int   port;
    /* 0x08 */ int   slot;
    /* 0x0c */ int   dir_label;
    /* 0x10 */ void *buff_addr;
    /* 0x14 */ int   buff_size;
} MC_NEW_MAKE_CTRL;

static MC_NEW_MAKE_CTRL mc_new_make_ctrl;       /* bss 4b5180 */

void MemoryCardNewMakeInit(int port, int slot, int dir_label,
                           void *buff_addr, int buff_size)              /* 71 */
{
    mc_new_make_ctrl.step      = 0;                                     /* 74 */
    mc_new_make_ctrl.port      = port;                                  /* 75 */
    mc_new_make_ctrl.slot      = slot;                                  /* 76 */
    mc_new_make_ctrl.dir_label = dir_label;                             /* 77 */
    mc_new_make_ctrl.buff_addr = buff_addr;                             /* 79 */
    mc_new_make_ctrl.buff_size = buff_size;                             /* 81 */

    if (buff_addr == (void *)0)                                         /* 82 */
    {
        MemoryCardAssert("Error! %s Buff Addr NULL!!", __FUNCTION__);
    }
}

int MemoryCardNewMakeMain(void)                                         /* 107 */
{
    int  res;
    int  mc_res;
    char dir_name[21];

    res = 0;

    memset(dir_name, 0, sizeof(dir_name));                              /* 115 */

    switch (mc_new_make_ctrl.step)                                      /* 118 */
    {
    case 0:
        MemoryCardSetDirName(dir_name, mc_new_make_ctrl.dir_label);      /* 121 */
        MemoryCardMakeNewDirInit(mc_new_make_ctrl.port,                  /* 123 */
                                 mc_new_make_ctrl.slot, dir_name);
        mc_new_make_ctrl.step = 1;                                      /* 125 */
        /* fall through */

    case 1:
        mc_res = MemoryCardMakeNewDirMain();                             /* 128 */

        if (mc_res == 1)                                                /* 131 */
        {
            mc_new_make_ctrl.step = 2;                                  /* 132 */
            return 0;
        }
        break;

    case 2:
        MemoryCardAllFileMakeInit(mc_new_make_ctrl.port,                 /* 142 */
                                  mc_new_make_ctrl.slot,
                                  mc_new_make_ctrl.dir_label,
                                  mc_new_make_ctrl.buff_addr,
                                  mc_new_make_ctrl.buff_size);
        mc_new_make_ctrl.step = 3;                                      /* 144 */
        /* fall through */

    case 3:
        mc_res = MemoryCardAllFileMakeMain();                            /* 147 */

        if (mc_res == 1)                                                /* 150 */
        {
            return 1;                                                   /* 151 */
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardNewMakeMain");                 /* 160 */
        return 0;
    }

    if (mc_res < 0)                                                     /* 154 */
    {
        res = mc_res;
    }

    return res;                                                         /* 164 */
}
