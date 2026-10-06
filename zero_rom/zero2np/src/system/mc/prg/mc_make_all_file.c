/* ==========================================================================
 *  system/mc/prg/mc_make_all_file.c
 *
 *  Populate a fresh card directory (mc_make_all_file.o, .text 0x1e0c28,
 *  0x270 bytes).
 *
 *  Six steps, but case 0/1 is a loop: each pass makes one data file and, if
 *  make_data_file_num has not reached GetMemoryCardDataFileNum() yet, resets
 *  the step to 0 so the next frame makes the next one.  Only when they are all
 *  there does it go on to the icon (2/3) and icon.sys (4/5).
 *
 *  Every file is written zero-filled with 0xffffffff as its checksum.  That is
 *  deliberate, not padding: the sum of the zeroes is 0, and
 *  MemoryCardCheckFileBroken() special-cases sum 0 with a stored -1 as intact,
 *  while MemoryCardCheckNewFileLoad() reads the same pattern as "empty".
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_make_all_file.h"

#include "mc_icon.h"
#include "mc_icon_sys.h"
#include "mc_make_file.h"
#include "mc_set_data.h"

#include <string.h>

typedef struct                                  /* 0x20 */
{
    /* 0x00 */ char  step;
    /* 0x04 */ int   port;
    /* 0x08 */ int   slot;
    /* 0x0c */ int   dir_label;
    /* 0x10 */ int   data_file_num;
    /* 0x14 */ int   make_data_file_num;
    /* 0x18 */ void *buff_addr;
    /* 0x1c */ int   buff_size;
} MC_MAKE_ALL_FILE_CTRL;

static MC_MAKE_ALL_FILE_CTRL mc_make_all_file_ctrl;   /* bss 4b5198 */

void MemoryCardAllFileMakeInit(int port, int slot, int dir_label,
                               void *buff_addr, int buff_size)           /* 76 */
{
    mc_make_all_file_ctrl.step               = 0;                       /* 79 */
    mc_make_all_file_ctrl.port               = port;                    /* 80 */
    mc_make_all_file_ctrl.slot               = slot;                    /* 81 */
    mc_make_all_file_ctrl.dir_label          = dir_label;               /* 83 */
    mc_make_all_file_ctrl.data_file_num      = GetMemoryCardDataFileNum(dir_label); /* 84 */
    mc_make_all_file_ctrl.make_data_file_num = 0;                       /* 86 */
    mc_make_all_file_ctrl.buff_addr          = buff_addr;               /* 88 */
    mc_make_all_file_ctrl.buff_size          = buff_size;               /* 89 */

    if (buff_addr == (void *)0)
    {
        MemoryCardAssert("Error! %s Buff Addr NULL!!", __FUNCTION__);
    }
}

int MemoryCardAllFileMakeMain(void)                                     /* 114 */
{
    int   res;
    int   mc_res;
    int   data_file_size;
    char *check_sum_addr;
    char  path_name[55];

    res = 0;

    memset(path_name, 0, sizeof(path_name));                            /* 124 */

    switch (mc_make_all_file_ctrl.step)                                 /* 130 */
    {
    case 0:
        MemoryCardSetFilePath(path_name, mc_make_all_file_ctrl.dir_label, /* 133 */
                              mc_make_all_file_ctrl.make_data_file_num);

        data_file_size = GetMemoryCardDataSize(                          /* 135 */
            mc_make_all_file_ctrl.dir_label,
            mc_make_all_file_ctrl.make_data_file_num);

        if (mc_make_all_file_ctrl.buff_size < data_file_size)            /* 138 */
        {
            MemoryCardAssert("Error! %s Buff Size Over!!", __FUNCTION__);  /* 139 */
        }

        memset(mc_make_all_file_ctrl.buff_addr, 0, data_file_size);      /* 143 */

        check_sum_addr = (char *)mc_make_all_file_ctrl.buff_addr;         /* 145 */
        check_sum_addr = check_sum_addr + data_file_size - 4;            /* 146 */

        memset(check_sum_addr, -1, 4);                                   /* 148 */

        MemoryCardMakeNewFileInit(mc_make_all_file_ctrl.port,             /* 152 */
                                  mc_make_all_file_ctrl.slot, path_name,
                                  mc_make_all_file_ctrl.buff_addr,
                                  data_file_size);

        mc_make_all_file_ctrl.step = 1;                                 /* 154 */
        /* fall through */

    case 1:
        mc_res = MemoryCardMakeNewFileMain();                            /* 157 */

        if (mc_res == 1)                                                /* 160 */
        {
            mc_make_all_file_ctrl.make_data_file_num++;                  /* 161 */

            /* Re-reads the count rather than using the cached
             * data_file_num -- the ROM's own redundancy, kept. */
            if (mc_make_all_file_ctrl.make_data_file_num <               /* 164 */
                GetMemoryCardDataFileNum(mc_make_all_file_ctrl.dir_label))
            {
                mc_make_all_file_ctrl.step = 0;
                return 0;
            }

            mc_make_all_file_ctrl.step = 2;                             /* 165 */
            return 0;
        }
        break;

    case 2:
        MemoryCardIconInit(mc_make_all_file_ctrl.port,                    /* 180 */
                           mc_make_all_file_ctrl.slot,
                           mc_make_all_file_ctrl.dir_label, 0);
        mc_make_all_file_ctrl.step = 3;                                 /* 181 */
        /* fall through */

    case 3:
        mc_res = MemoryCardIconMain();                                  /* 185 */

        if (mc_res == 1)                                                /* 188 */
        {
            mc_make_all_file_ctrl.step = 4;                             /* 189 */
            return 0;
        }
        break;

    case 4:
        MemoryCardIconSysInit(mc_make_all_file_ctrl.port,                 /* 198 */
                              mc_make_all_file_ctrl.slot,
                              mc_make_all_file_ctrl.dir_label);
        mc_make_all_file_ctrl.step = 5;                                 /* 199 */
        /* fall through */

    case 5:
        mc_res = MemoryCardIconSysMain();                               /* 203 */

        if (mc_res == 1)                                                /* 206 */
        {
            return 1;                                                   /* 207 */
        }
        break;

    default:
        MemoryCardAssert("Error! %s", __FUNCTION__);                     /* 215 */
        return 0;
    }

    if (mc_res < 0)                                                     /* 209 */
    {
        res = mc_res;
    }

    return res;                                                         /* 219 */
}
