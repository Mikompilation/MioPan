/* ==========================================================================
 *  system/mc/prg/mc_check_dir.c
 *
 *  Directory listing (mc_check_dir.o, .text 0x1df778, 0x328 bytes).
 *
 *  Everything that has to know what is really on the card goes through here:
 *  the integrity check compares the listing's entry count and cluster cost
 *  against what the save layout predicts, and the delete-all job walks the
 *  listing rather than the name tables, so it removes files this build does
 *  not know about too.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_check_dir.h"

#include "libmc.h"
#include "mc_cmn.h"                             /* MemoryCardExeEndSync     */
#include "mc_set_data.h"                        /* MemoryCardAssert         */

#include <string.h>

typedef struct                                  /* 0x44 */
{
    /* 0x00 */ char step;
    /* 0x01 */ char retry_cnt;
    /* 0x04 */ int  port;
    /* 0x08 */ int  slot;
    /* 0x0c */ char name[55];
} MC_CHECK_DIR_CTRL;

typedef struct                                  /* 0x4c0 */
{
    /* 0x000 */ sceMcTblGetDir table[18];
    /* 0x480 */ short          get_filenum;
} MC_DIR_INFO;

static MC_CHECK_DIR_CTRL mc_check_dir_ctrl;     /* bss 4b4780 */
static MC_DIR_INFO       mc_dir_info;           /* bss 4b4800 */

void MemoryCardDirInfoCtrlInit(void)                                    /* 71 */
{
    memset(&mc_dir_info, 0, sizeof(mc_dir_info));                       /* 74 */
}

void MemoryCardGetDirInfoInit(int port, int slot, char *name)           /* 84 */
{
    mc_check_dir_ctrl.retry_cnt = 4;                                    /* 87 */
    mc_check_dir_ctrl.step      = 0;                                    /* 88 */
    mc_check_dir_ctrl.port      = port;                                 /* 89 */
    mc_check_dir_ctrl.slot      = slot;                                 /* 90 */

    memset(mc_check_dir_ctrl.name, 0, sizeof(mc_check_dir_ctrl.name));   /* 91 */
    strcpy(mc_check_dir_ctrl.name, name);                               /* 92 */
}

int MemoryCardGetDirInfoMain(void)                                      /* 114 */
{
    int res;
    int mc_res;
    int result;

    res    = 0;                                                        /* 122 */
    result = 0;

    if (mc_check_dir_ctrl.step == 0)                                    /* 126 */
    {
        mc_res = MemoryCardGetDirInfoReq(mc_check_dir_ctrl.port,          /* 127 */
                                         mc_check_dir_ctrl.slot,
                                         mc_check_dir_ctrl.name);

        if (mc_res == 0)                                                /* 130 */
        {
            mc_check_dir_ctrl.step = 1;                                 /* 131 */
        }
        else if (mc_check_dir_ctrl.retry_cnt > 0)                        /* 136 */
        {
            if (mc_res == -100)                                         /* 138 */
            {
                MemoryCardAssert("Error! sceMcInit forgets");            /* 140 */
            }
            else if (mc_res == -210)                                    /* 143 */
            {
                MemoryCardAssert("PATH Error! MemoryCardGetDirInfoMain"); /* 145 */
            }
            else if (mc_res == -200)                                    /* 148 */
            {
                mc_check_dir_ctrl.step = 2;                              /* 150 */
            }
            else
            {
                mc_check_dir_ctrl.retry_cnt--;                            /* 155 */
            }
        }
        else
        {
            res = -10;                                                   /* 160 */
        }
    }

    if (mc_check_dir_ctrl.step == 1)                                     /* 166 */
    {
        mc_res = MemoryCardExeEndSync(&result);                          /* 167 */

        if (mc_res == 1)                                                 /* 170 */
        {
            if (result >= 0)                                             /* 172 */
            {
                /* On success the result *is* the entry count. */
                mc_dir_info.get_filenum = result;                        /* 174 */
                res = 1;                                                 /* 176 */
            }
            else
            {
                switch (result)                                          /* 180 */
                {
                case -2:
                case -4:
                case -5:
                case -6:
                    res = result;                                        /* 186 */
                    break;

                default:
                    res = -20;
                    break;
                }
            }
        }
        else if (mc_res == -1)                                           /* 193 */
        {
            mc_check_dir_ctrl.step = 0;
        }
    }

    if (mc_check_dir_ctrl.step == 2)                                     /* 200 */
    {
        if (MemoryCardExeEndSync(&result) != 0)                          /* 201 */
        {
            mc_check_dir_ctrl.step = 0;                                  /* 204 */
        }
    }

    return res;                                                          /* 211 */
}

int MemoryCardGetDirInfoReq(int port, int slot, char *name)             /* 221 */
{
    return sceMcGetDir(port, slot, name, 0, 18, &mc_dir_info.table[0]);   /* 229 */
}

int GetMemoryCardCheckDirFileNum(void)
{
    return mc_dir_info.get_filenum;                                     /* 247 */
}

void GetMemoryCardCheckDirEntryName(char *name, int data_pos)
{
    if (data_pos >= GetMemoryCardCheckDirFileNum())                     /* 257 */
    {
        MemoryCardAssert("Error! GetMemoryCardCheckDirEntryName");        /* 258 */
    }

    memset(name, 0, 55);                                                /* 261 */
    strcpy(name, (char *)mc_dir_info.table[data_pos].EntryName);        /* 264 */
}

/* --------------------------------------------------------------------------
 *  GetMemoryCardCheckDirSize
 *
 *  Cluster cost of the directory as the card holds it: every file rounded up
 *  to 1 KB, plus one cluster per two directory entries for the entry table
 *  itself, plus two for the directory.  The loop starts at 2 to skip "." and
 *  "..", whose FileSizeByte is meaningless.
 *
 *  This has to come out equal to GetMemoryCardDirSizeCluster()'s prediction --
 *  MemoryCardCheckDirBroken() is exactly that comparison -- and it does: the
 *  "(entries - 1) / 2" term reproduces the 5 (game data, 11 entries) or 2
 *  (album, 5 entries) that the predictor adds as a literal.
 * ------------------------------------------------------------------------ */
int GetMemoryCardCheckDirSize(void)
{
    int i;
    int size;

    size = 0;

    for (i = 2; i < mc_dir_info.get_filenum; i++)                       /* 281 */
    {
        size += (mc_dir_info.table[i].FileSizeByte + 1023) / 1024;      /* 282 */
    }                                                                   /* 283 */

    size += (mc_dir_info.get_filenum - 1) / 2;                          /* 287 */

    return size + 2;                                                    /* 293 */
}
