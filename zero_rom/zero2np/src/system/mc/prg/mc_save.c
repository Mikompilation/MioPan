/* ==========================================================================
 *  system/mc/prg/mc_save.c
 *
 *  Overwrite an existing card file (mc_save.o, .text 0x1e15b8, 0x1a8 bytes).
 *
 *  Identical in shape to mc_load.c; see that file for why the even cases fall
 *  through.  The only differences are the open mode and the middle primitive.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_save.h"

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
} MC_SAVE_CTRL;

static MC_SAVE_CTRL mc_save_ctrl;               /* bss 4b5260 */

void MemoryCardFileSaveInit(int port, int slot, char *name,
                            void *data_addr, int size)                  /* 74 */
{
    mc_save_ctrl.fd        = -1;                                        /* 77 */
    mc_save_ctrl.step      = 0;                                         /* 78 */
    mc_save_ctrl.port      = port;                                      /* 79 */
    mc_save_ctrl.slot      = slot;                                      /* 80 */
    mc_save_ctrl.size      = size;                                      /* 81 */
    mc_save_ctrl.data_addr = data_addr;                                 /* 82 */

    memset(mc_save_ctrl.name, 0, sizeof(mc_save_ctrl.name));            /* 83 */
    strcpy(mc_save_ctrl.name, name);                                    /* 84 */
}

int MemoryCardFileSaveMain(void)                                        /* 108 */
{
    int res;
    int mc_res;

    res = 0;                                                           /* 113 */

    switch (mc_save_ctrl.step)                                          /* 117 */
    {
    case 0:
        /* Mode 3 is read+write on an existing file -- no create bit. */
        MemoryCardFileOpenInit(mc_save_ctrl.port, mc_save_ctrl.slot,     /* 120 */
                               mc_save_ctrl.name, 3);
        mc_save_ctrl.step = 1;                                          /* 121 */
        /* fall through */

    case 1:
        mc_res = MemoryCardFileOpenMain(&mc_save_ctrl.fd);              /* 125 */

        if (mc_res == 1)                                                /* 128 */
        {
            mc_save_ctrl.step = 2;                                      /* 129 */
            return 0;
        }
        break;

    case 2:
        MemoryCardFileWriteInit(mc_save_ctrl.fd, mc_save_ctrl.data_addr,  /* 139 */
                                mc_save_ctrl.size);
        mc_save_ctrl.step = 3;                                          /* 140 */
        /* fall through */

    case 3:
        mc_res = MemoryCardFileWriteMain();                             /* 144 */

        if (mc_res == 1)                                                /* 147 */
        {
            mc_save_ctrl.step = 4;                                      /* 148 */
            return 0;
        }
        break;

    case 4:
        MemoryCardFileCloseInit(mc_save_ctrl.fd);                        /* 158 */
        mc_save_ctrl.step = 5;                                          /* 159 */
        /* fall through */

    case 5:
        mc_res = MemoryCardFileCloseMain();                             /* 162 */

        if (mc_res == 1)                                                /* 165 */
        {
            return 1;                                                   /* 166 */
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardFileSaveMain");              /* 175 */
        return 0;
    }

    if (mc_res < 0)                                                     /* 169 */
    {
        res = mc_res;
    }

    return res;                                                         /* 179 */
}
