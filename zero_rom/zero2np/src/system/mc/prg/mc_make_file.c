/* ==========================================================================
 *  system/mc/prg/mc_make_file.c
 *
 *  Create a card file and write it (mc_make_file.o, .text 0x1e1058, 0x1a8).
 *
 *  Identical in shape to mc_load.c and mc_save.c; see mc_load.c for why the
 *  even cases fall through.  Only the open mode differs.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_make_file.h"

#include "libmc.h"
#include "mc_close.h"
#include "mc_open.h"
#include "mc_set_data.h"                        /* MemoryCardAssert         */
#include "mc_write.h"

#include <string.h>

typedef struct                                  /* 0x50 */
{
    /* 0x00 */ char  step;
    /* 0x04 */ int   port;
    /* 0x08 */ int   slot;
    /* 0x0c */ int   size;
    /* 0x10 */ int   fd;
    /* 0x14 */ void *data_addr;
    /* 0x18 */ char  name[55];
} MC_MAKE_FILE_CTRL;

static MC_MAKE_FILE_CTRL mc_make_file_ctrl;     /* bss 4b51e0 */

void MemoryCardMakeNewFileInit(int port, int slot, char *name,
                               void *data_addr, int size)               /* 75 */
{
    mc_make_file_ctrl.fd        = -1;                                   /* 78 */
    mc_make_file_ctrl.step      = 0;                                    /* 79 */
    mc_make_file_ctrl.port      = port;                                 /* 80 */
    mc_make_file_ctrl.slot      = slot;                                 /* 81 */
    mc_make_file_ctrl.size      = size;                                 /* 82 */
    mc_make_file_ctrl.data_addr = data_addr;                            /* 83 */

    memset(mc_make_file_ctrl.name, 0, sizeof(mc_make_file_ctrl.name));   /* 84 */
    strcpy(mc_make_file_ctrl.name, name);                               /* 85 */
}

int MemoryCardMakeNewFileMain(void)                                     /* 109 */
{
    int res;
    int mc_res;

    res = 0;                                                           /* 114 */

    switch (mc_make_file_ctrl.step)                                     /* 118 */
    {
    case 0:
        /* 0x203 = create + read + write. */
        MemoryCardFileOpenInit(mc_make_file_ctrl.port,                   /* 121 */
                               mc_make_file_ctrl.slot,
                               mc_make_file_ctrl.name, 0x203);
        mc_make_file_ctrl.step = 1;                                     /* 122 */
        /* fall through */

    case 1:
        mc_res = MemoryCardFileOpenMain(&mc_make_file_ctrl.fd);          /* 126 */

        if (mc_res == 1)                                                /* 129 */
        {
            mc_make_file_ctrl.step = 2;                                 /* 130 */
            return 0;
        }
        break;

    case 2:
        MemoryCardFileWriteInit(mc_make_file_ctrl.fd,                    /* 140 */
                                mc_make_file_ctrl.data_addr,
                                mc_make_file_ctrl.size);
        mc_make_file_ctrl.step = 3;                                     /* 141 */
        /* fall through */

    case 3:
        mc_res = MemoryCardFileWriteMain();                             /* 145 */

        if (mc_res == 1)                                                /* 148 */
        {
            mc_make_file_ctrl.step = 4;                                 /* 149 */
            return 0;
        }
        break;

    case 4:
        MemoryCardFileCloseInit(mc_make_file_ctrl.fd);                    /* 159 */
        mc_make_file_ctrl.step = 5;                                     /* 160 */
        /* fall through */

    case 5:
        mc_res = MemoryCardFileCloseMain();                             /* 163 */

        if (mc_res == 1)                                                /* 166 */
        {
            return 1;                                                   /* 167 */
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardMakeNewFileMain");            /* 176 */
        return 0;
    }

    if (mc_res < 0)                                                     /* 170 */
    {
        res = mc_res;
    }

    return res;                                                         /* 180 */
}
