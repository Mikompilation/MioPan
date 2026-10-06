/* ==========================================================================
 *  system/mc/prg/mc_icon_sys.c
 *
 *  Write icon.sys onto the card (mc_icon_sys.o, .text 0x1e0678, 0x208 bytes).
 *
 *  Only two steps, and it needs no staging buffer: the structure it writes is
 *  the one sitting in its own control block.  Init() is nearly all struct copy
 *  -- MemoryCardSetIconSysData() returns 964 bytes by value, which the ROM
 *  moves through a stack temporary before landing it in .bss.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_icon_sys.h"

#include "libmc.h"
#include "mc_make_file.h"
#include "mc_set_data.h"                        /* MemoryCardSetIconSys*    */

#include <string.h>

typedef struct                                  /* 0x3d4 */
{
    /* 0x000 */ char         step;
    /* 0x004 */ int          port;
    /* 0x008 */ int          slot;
    /* 0x00c */ int          dir_label;
    /* 0x010 */ sceMcIconSys icon_sys;
} MC_ICON_SYS_CTRL;

static MC_ICON_SYS_CTRL mc_icon_sys_ctrl;       /* bss 4b4d58 */

void MemoryCardIconSysInit(int port, int slot, int dir_label)            /* 69 */
{
    mc_icon_sys_ctrl.step      = 0;                                     /* 72 */
    mc_icon_sys_ctrl.port      = port;                                  /* 73 */
    mc_icon_sys_ctrl.slot      = slot;                                  /* 74 */
    mc_icon_sys_ctrl.dir_label = dir_label;                             /* 75 */

    mc_icon_sys_ctrl.icon_sys  = MemoryCardSetIconSysData(dir_label);    /* 76 */
}

int MemoryCardIconSysMain(void)                                         /* 100 */
{
    int  res;
    int  mc_res;
    char path_name[55];

    res = 0;

    memset(path_name, 0, sizeof(path_name));                            /* 108 */

    if (mc_icon_sys_ctrl.step == 0)                                     /* 111 */
    {
        MemoryCardSetIconSysPath(path_name, mc_icon_sys_ctrl.dir_label);  /* 114 */

        MemoryCardMakeNewFileInit(mc_icon_sys_ctrl.port,                  /* 116 */
                                  mc_icon_sys_ctrl.slot, path_name,
                                  &mc_icon_sys_ctrl.icon_sys,
                                  sizeof(mc_icon_sys_ctrl.icon_sys));

        mc_icon_sys_ctrl.step = 1;                                      /* 118 */
    }
    else if (mc_icon_sys_ctrl.step != 1)                                /* 121 */
    {
        MemoryCardAssert("Error! MemoryCardIconSysMain");                 /* 124 */
        return 0;                                                       /* 125 */
    }

    mc_res = MemoryCardMakeNewFileMain();                               /* 128 */

    if (mc_res == 1)                                                    /* 132 */
    {
        res = 1;
    }
    else if (mc_res < 0)                                                /* 134 */
    {
        res = mc_res;
    }

    return res;                                                         /* 138 */
}
