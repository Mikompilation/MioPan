/* ==========================================================================
 *  system/mc/prg/mc_icon.c
 *
 *  Write the save icon onto the card (mc_icon.o, .text 0x1e04a8, 0x1cc bytes).
 *
 *  Four steps: request the icon file off the CD, wait for it, make the card
 *  file, pump it.  The buffer is freed on the way out of *either* outcome, and
 *  Init() frees any leftover first -- so the only way to leak it is to abandon
 *  the job midway, which MemoryCardEnd() also covers.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_icon.h"

#include "mc_make_file.h"
#include "mc_set_data.h"                        /* GetIconData* / MemoryCardAssert */

#include <string.h>

typedef struct                                  /* 0x14 */
{
    /* 0x00 */ char step;
    /* 0x04 */ int  port;
    /* 0x08 */ int  slot;
    /* 0x0c */ int  dir_label;
    /* 0x10 */ int  icon_type;
} MC_ICON_CTRL;

static void        *icon_data_addr;              /* sdata 3f1ae0 */
static MC_ICON_CTRL mc_icon_ctrl;                /* bss 4b4d40  */

void MemoryCardIconInit(int port, int slot, int dir_label, int icon_type) /* 73 */
{
    mc_icon_ctrl.step      = 0;                                         /* 76 */
    mc_icon_ctrl.port      = port;                                      /* 77 */
    mc_icon_ctrl.slot      = slot;                                      /* 78 */
    mc_icon_ctrl.dir_label = dir_label;                                 /* 79 */
    mc_icon_ctrl.icon_type = icon_type;                                 /* 80 */

    LiberateMemoryCardIconDataMem();                                    /* 83 */
}

int MemoryCardIconMain(void)                                            /* 107 */
{
    int  res;
    int  mc_res;
    char path_name[55];

    res = 0;                                                           /* 113 */

    memset(path_name, 0, sizeof(path_name));                            /* 115 */

    switch (mc_icon_ctrl.step)                                          /* 118 */
    {
    case 0:
        icon_data_addr = GetDataMemoryArea(                              /* 121 */
            GetIconDataSize(mc_icon_ctrl.dir_label, mc_icon_ctrl.icon_type));

        MemoryCardDataLoadReq(icon_data_addr,                            /* 123 */
            GetIconDataLabel(mc_icon_ctrl.dir_label, mc_icon_ctrl.icon_type));

        mc_icon_ctrl.step = 1;                                          /* 125 */
        /* fall through */

    case 1:
        if (MemoryCardDataLoadWait() != 0)                              /* 129 */
        {
            mc_icon_ctrl.step = 2;                                      /* 132 */
        }
        break;

    case 2:
        MemoryCardSetIconFilePath(path_name, mc_icon_ctrl.dir_label,      /* 135 */
                                  mc_icon_ctrl.icon_type);

        MemoryCardMakeNewFileInit(mc_icon_ctrl.port, mc_icon_ctrl.slot,   /* 138 */
                                  path_name, icon_data_addr,
                                  GetIconDataSize(mc_icon_ctrl.dir_label,
                                                  mc_icon_ctrl.icon_type));

        mc_icon_ctrl.step = 3;                                          /* 140 */
        /* fall through */

    case 3:
        mc_res = MemoryCardMakeNewFileMain();                            /* 143 */

        if (mc_res == 1)                                                /* 146 */
        {
            res = 1;                                                    /* 147 */
        }
        else if (mc_res < 0)                                            /* 150 */
        {
            res = mc_res;
        }
        break;                                                          /* 154 */

    default:
        MemoryCardAssert("Error! MemoryCardIconMain");                    /* 156 */
        break;
    }

    /* Non-zero res means finished, one way or the other. */
    if (res != 0)                                                       /* 161 */
    {
        LiberateMemoryCardIconDataMem();                                /* 163 */
    }

    return res;                                                         /* 166 */
}

void LiberateMemoryCardIconDataMem(void)                                /* 172 */
{
    if (icon_data_addr != (void *)0)                                    /* 175 */
    {
        LiberateDataMemoryArea(icon_data_addr);                          /* 177 */
        icon_data_addr = (void *)0;                                     /* 178 */
    }
}
