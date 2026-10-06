/* ==========================================================================
 *  system/mc/prg/mc_del_all_file.c
 *
 *  Empty a card directory (mc_del_all_file.o, .text 0x1dfd60, 0x298 bytes).
 *
 *  Four steps, of which 2/3 loop: `del_file_cnt` starts at 2 to skip "." and
 *  ".." and walks the listing one entry per pass.  Both the "listing arrived"
 *  and the "one file deleted" paths finish with the same test -- has the count
 *  reached the listing's length -- and GCC cross-jumped the two copies, so only
 *  the second one's line numbers survive in the object.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_del_all_file.h"

#include "mc_check_dir.h"
#include "mc_del_file.h"
#include "mc_set_data.h"

#include <string.h>

typedef struct                                  /* 0x14 */
{
    /* 0x00 */ char step;
    /* 0x04 */ int  port;
    /* 0x08 */ int  slot;
    /* 0x0c */ int  dir_label;
    /* 0x10 */ int  del_file_cnt;
} MC_DEL_ALL_FILE_CTRL;

static MC_DEL_ALL_FILE_CTRL mc_del_all_file_ctrl;   /* bss 4b4cc0 */

static void SetMemoryCardDelFilePath(char *path_name, int dir_label,
                                     int data_pos);

void MemoryCardAllFileDelInit(int port, int slot, int dir_label)
{
    mc_del_all_file_ctrl.del_file_cnt = 2;                              /* 74 */
    mc_del_all_file_ctrl.port         = port;                           /* 75 */
    mc_del_all_file_ctrl.slot         = slot;                           /* 76 */
    mc_del_all_file_ctrl.dir_label    = dir_label;                      /* 77 */
    mc_del_all_file_ctrl.step         = 0;                              /* 79 */
}

int MemoryCardAllFileDelMain(void)                                      /* 100 */
{
    int  res;
    int  mc_res;
    char path_name[55];

    res = 0;

    memset(path_name, 0, sizeof(path_name));                            /* 108 */

    switch (mc_del_all_file_ctrl.step)                                  /* 111 */
    {
    case 0:
        MemoryCardMakeSearchDirPath(path_name,                           /* 114 */
                                    mc_del_all_file_ctrl.dir_label);
        MemoryCardGetDirInfoInit(mc_del_all_file_ctrl.port,              /* 116 */
                                 mc_del_all_file_ctrl.slot, path_name);
        mc_del_all_file_ctrl.step = 1;                                  /* 118 */
        /* fall through */

    case 1:
        mc_res = MemoryCardGetDirInfoMain();                             /* 121 */

        if (mc_res == 1)                                                /* 124 */
        {
            /* Same test as case 3's; GCC merged the two copies, so this one's
             * own line numbers are gone.  An already-empty directory finishes
             * here without deleting anything. */
            if (mc_del_all_file_ctrl.del_file_cnt <                      /* 127 */
                GetMemoryCardCheckDirFileNum())
            {
                mc_del_all_file_ctrl.step = 2;
            }
            else
            {
                res = 1;
            }

            return res;
        }
        break;

    case 2:
        SetMemoryCardDelFilePath(path_name,                              /* 141 */
                                 mc_del_all_file_ctrl.dir_label,
                                 mc_del_all_file_ctrl.del_file_cnt);
        MemoryCardFileDelInit(mc_del_all_file_ctrl.port,                 /* 143 */
                              mc_del_all_file_ctrl.slot, path_name);
        mc_del_all_file_ctrl.step = 3;                                  /* 145 */
        /* fall through */

    case 3:
        mc_res = MemoryCardFileDelMain();                                /* 148 */

        if (mc_res == 1)                                                /* 151 */
        {
            mc_del_all_file_ctrl.del_file_cnt++;                          /* 153 */

            if (mc_del_all_file_ctrl.del_file_cnt <                      /* 156 */
                GetMemoryCardCheckDirFileNum())
            {
                mc_del_all_file_ctrl.step = 2;                           /* 160 */
            }
            else
            {
                res = 1;                                                 /* 157 */
            }

            return res;
        }
        break;

    default:
        MemoryCardAssert("Error! MemoryCardAllFileDelMain");              /* 169 */
        return 0;
    }

    if (mc_res < 0)                                                     /* 164 */
    {
        res = mc_res;
    }

    return res;                                                         /* 173 */
}

/* --------------------------------------------------------------------------
 *  SetMemoryCardDelFilePath
 *
 *  "/<dir>/<listing entry data_pos>".  Unlike MemoryCardSetFilePath() this one
 *  takes the name off the card rather than out of the name tables, which is
 *  what lets the delete walk cover unknown files.
 *
 *  ROM ODDITY, kept: entry_name is zeroed twice -- once by its own initialiser
 *  and again as a statement.
 * ------------------------------------------------------------------------ */
static void SetMemoryCardDelFilePath(char *path_name, int dir_label,
                                     int data_pos)                      /* 182 */
{
    char fname[55]      = "/";                                          /* 183 */
    char entry_name[55] = "";                                           /* 184 */
    char dir_name[21]   = "";                                           /* 185 */

    memset(entry_name, 0, sizeof(entry_name));                          /* 188 */
    memset(path_name, 0, 55);                                           /* 190 */

    MemoryCardSetDirName(dir_name, dir_label);                          /* 193 */
    GetMemoryCardCheckDirEntryName(entry_name, data_pos);                /* 195 */

    strcat(fname, dir_name);                                            /* 198 */
    strcat(fname, "/");                                                 /* 199 */
    strcat(fname, entry_name);                                          /* 202 */

    strcpy(path_name, fname);                                           /* 206 */
}
