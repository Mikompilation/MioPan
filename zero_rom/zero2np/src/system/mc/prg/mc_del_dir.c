/* ==========================================================================
 *  system/mc/prg/mc_del_dir.c
 *
 *  Delete a whole card directory (mc_del_dir.o, .text 0x1dfff0, 0x150 bytes).
 *
 *  Note that step 2 deletes the *directory* through mc_del_file.c: on the card
 *  a directory entry is deleted the same way a file is, which is why there is
 *  no separate rmdir primitive in the folder.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_del_dir.h"

#include "mc_del_all_file.h"
#include "mc_del_file.h"
#include "mc_set_data.h"                        /* MemoryCardSetDirName     */

#include <string.h>

typedef struct                                  /* 0x10 */
{
    /* 0x0 */ char step;
    /* 0x4 */ int  port;
    /* 0x8 */ int  slot;
    /* 0xc */ int  dir_label;
} MC_DIR_DEL_CTRL;

static MC_DIR_DEL_CTRL mc_dir_del_ctrl;         /* bss 4b4cd8 */

void MemoryCardDirDelInit(int port, int slot, int dir_label)
{
    mc_dir_del_ctrl.dir_label = dir_label;                              /* 70 */
    mc_dir_del_ctrl.port      = port;                                   /* 71 */
    mc_dir_del_ctrl.slot      = slot;                                   /* 72 */
    mc_dir_del_ctrl.step      = 0;                                      /* 73 */
}

int MemoryCardDirDelMain(void)                                          /* 95 */
{
    int  res;
    int  mc_res;
    char dir_name[21];

    res = 0;

    memset(dir_name, 0, sizeof(dir_name));                              /* 103 */

    switch (mc_dir_del_ctrl.step)                                       /* 106 */
    {
    case 0:
        MemoryCardAllFileDelInit(mc_dir_del_ctrl.port,                   /* 108 */
                                 mc_dir_del_ctrl.slot,
                                 mc_dir_del_ctrl.dir_label);
        mc_dir_del_ctrl.step = 1;                                       /* 110 */
        /* fall through */

    case 1:
        mc_res = MemoryCardAllFileDelMain();                             /* 113 */

        if (mc_res == 1)                                                /* 116 */
        {
            mc_dir_del_ctrl.step = 2;                                   /* 117 */
            return 0;
        }
        break;

    case 2:
        MemoryCardSetDirName(dir_name, mc_dir_del_ctrl.dir_label);       /* 126 */
        MemoryCardFileDelInit(mc_dir_del_ctrl.port, mc_dir_del_ctrl.slot, /* 128 */
                              dir_name);
        mc_dir_del_ctrl.step = 3;                                       /* 130 */
        /* fall through */

    case 3:
        mc_res = MemoryCardFileDelMain();                                /* 133 */

        if (mc_res == 1)                                                /* 136 */
        {
            return 1;                                                   /* 137 */
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardDirDelMain");                  /* 146 */
        return 0;
    }

    if (mc_res < 0)                                                     /* 140 */
    {
        res = mc_res;
    }

    return res;                                                         /* 150 */
}
