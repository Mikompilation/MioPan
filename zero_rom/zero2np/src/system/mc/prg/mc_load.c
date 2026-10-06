/* ==========================================================================
 *  system/mc/prg/mc_load.c
 *
 *  Read a whole card file (mc_load.o, .text 0x1e0880, 0x1a8 bytes).
 *
 *  Six steps in three pairs -- issue then pump, for open, read and close.  The
 *  even cases deliberately fall through into the odd ones, so a step change
 *  costs no frame; only a *completed* sub-job returns to the caller with 0 and
 *  waits for the next frame.
 *
 *  mc_save.c and mc_make_file.c are the same function with a different middle
 *  primitive and a different open mode.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_load.h"

#include "libmc.h"
#include "mc_close.h"
#include "mc_open.h"
#include "mc_read.h"
#include "mc_set_data.h"                        /* MemoryCardAssert         */

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
} MC_LOAD_CTRL;

static MC_LOAD_CTRL mc_load_ctrl;               /* bss 4b5130 */

void MemoryCardFileLoadInit(int port, int slot, char *name,
                            void *data_addr, int size)                  /* 74 */
{
    mc_load_ctrl.fd        = -1;                                        /* 77 */
    mc_load_ctrl.step      = 0;                                         /* 78 */
    mc_load_ctrl.port      = port;                                      /* 79 */
    mc_load_ctrl.slot      = slot;                                      /* 80 */
    mc_load_ctrl.size      = size;                                      /* 81 */
    mc_load_ctrl.data_addr = data_addr;                                 /* 82 */

    memset(mc_load_ctrl.name, 0, sizeof(mc_load_ctrl.name));            /* 83 */
    strcpy(mc_load_ctrl.name, name);                                    /* 84 */
}

int MemoryCardFileLoadMain(void)                                        /* 107 */
{
    int res;
    int mc_res;

    res = 0;                                                           /* 112 */

    switch (mc_load_ctrl.step)                                          /* 116 */
    {
    case 0:
        /* Mode 1 is read-only: the file has to be there already. */
        MemoryCardFileOpenInit(mc_load_ctrl.port, mc_load_ctrl.slot,     /* 119 */
                               mc_load_ctrl.name, 1);
        mc_load_ctrl.step = 1;                                          /* 120 */
        /* fall through */

    case 1:
        mc_res = MemoryCardFileOpenMain(&mc_load_ctrl.fd);              /* 124 */

        if (mc_res == 1)                                                /* 127 */
        {
            mc_load_ctrl.step = 2;                                      /* 128 */
            return 0;
        }
        break;

    case 2:
        MemoryCardFileReadInit(mc_load_ctrl.fd, mc_load_ctrl.data_addr,   /* 138 */
                               mc_load_ctrl.size);
        mc_load_ctrl.step = 3;                                          /* 139 */
        /* fall through */

    case 3:
        mc_res = MemoryCardFileReadMain();                              /* 143 */

        if (mc_res == 1)                                                /* 146 */
        {
            mc_load_ctrl.step = 4;                                      /* 147 */
            return 0;
        }
        break;

    case 4:
        MemoryCardFileCloseInit(mc_load_ctrl.fd);                        /* 157 */
        mc_load_ctrl.step = 5;                                          /* 158 */
        /* fall through */

    case 5:
        mc_res = MemoryCardFileCloseMain();                             /* 161 */

        if (mc_res == 1)                                                /* 164 */
        {
            return 1;                                                   /* 165 */
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardFileLoadMain");              /* 174 */
        return 0;
    }

    /* Still working, or the sub-job failed -- pass its code up unchanged. */
    if (mc_res < 0)                                                     /* 168 */
    {
        res = mc_res;
    }

    return res;                                                         /* 178 */
}
