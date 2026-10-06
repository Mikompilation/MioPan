/* ==========================================================================
 *  system/mc/prg/mc_set_data.c
 *
 *  Save-data marshalling, path building and per-slot summaries (mc_set_data.o,
 *  .text 0x1e16f0, 0x16c0 bytes -- the largest module in system/mc).
 *
 *  Three things live here and it is worth separating them:
 *
 *    1. The path builders.  Every card path is assembled from
 *       game_dir_name[dir_label] plus one of the file-name tables, into a
 *       fixed 55-byte buffer that the builder zeroes first.
 *
 *    2. The block walker.  SetMemoryCardSaveDataInfo() runs a manifest from
 *       system/mc/dat/save_data.c to produce an {addr, size} table describing
 *       one card file; the four public entry points on top of it either sum
 *       the sizes (GetMemoryCardDataSize), gather the bytes
 *       (SetMemoryCardSaveDataToBuff) or scatter them back
 *       (DevelopMemoryCardLoadData).
 *
 *    3. The play-data header.  Five slot summaries in this module's .bss, which
 *       is itself one of the save blocks -- that is why the load screen can
 *       list every slot after reading a single small file.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "mc_set_data.h"

#include "mc_cmn.h"                             /* Calc/SetMemoryCardDataCheckSum */

#include "../dat/mc_iconsys_dat.h"              /* mc_bgcolor / mc_light*   */
#include "../dat/mc_title_dat.h"                /* game_dir_name / *_file_name */
#include "../dat/save_data.h"                   /* game/album manifests     */

#include "../../../common/mem_util.h"           /* mem_utilGetMem / FreeMem */
#include "../../../common/utility2.h"           /* PRINT_ASSERT / PRINT_WARNING */
#include "../../../graphics/graph3d/ctl/fixed_array.h"
#include "../../../ingame/menu/play_data.h"     /* GetPlayTime              */
#include "../../../ingame/menu/plyr_room_info.h"/* GetPlyrRoomLabel         */
#include "../../../main/main.h"                 /* SoftResetLock / Unlock   */
#include "../../../system/eeiop/cddat.h"        /* GetFileSize              */
#include "../../../system/os/eecdvd.h"          /* LoadReq / IsLoadEndAll   */

/* PORT: pointer fixups for the four save blocks that hold live pointers. */
#include "../../../ingame/enemy/fene_entry.h"   /* FeneEntrySavePtrFixup    */
#include "../../../ingame/event/prg/ev_exe.h"   /* EvExeCtrlSavePtrFixup    */
#include "../../../ingame/event/prg/ev_open.h"  /* EvCtrlCenterSavePtrFixup */
#include "../../../ingame/plyr/sister.h"        /* SisMotionSavePtrFixup    */

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  Static state
 * ------------------------------------------------------------------------ */

/* One save-slot summary each, so the load screen can draw all five rows from
 * the header file alone.  fixed_array rather than plain arrays because that is
 * what the ROM has: every subscript below carries an inlined
 * _fixed_array_verifyrange<T>(data_num, 5). */
typedef struct                                  /* 0x84 */
{
    /* 0x00 */ fixed_array<int, 5>       chapter;
    /* 0x14 */ fixed_array<int, 5>       clear_num;
    /* 0x28 */ fixed_array<int, 5>       room_label;
    /* 0x3c */ fixed_array<TIME_INFO, 5> play_time;
    /* 0x78 */ fixed_array<char, 5>      clear_data_flg;
    /* 0x7d */ fixed_array<char, 5>      data_flg;
} MC_PLAY_DATA_HEAD;

static MC_PLAY_DATA_HEAD mc_play_data_head;     /* bss 4b52b0 */

/* Manifest per file_label inside dir_label 0, and per file_label inside an
 * album.  Both carry a trailing NULL, which is the "no such file" answer for
 * a file_label one past the last real one. */
static MC_SET_SAVE_FUNC *game_save_data[8] =    /* data 31e3a8 */
{
    save_system_data,                           /* 0  Zero2System           */
    save_play_data_head,                        /* 1  play-data header      */
    save_game_data,                             /* 2  save slot 1           */
    save_game_data,                             /* 3  save slot 2           */
    save_game_data,                             /* 4  save slot 3           */
    save_game_data,                             /* 5  save slot 4           */
    save_game_data,                             /* 6  save slot 5           */
    (MC_SET_SAVE_FUNC *)0
};

static MC_SET_SAVE_FUNC *album_save_data[2] =   /* sdata 3f1b28 */
{
    save_album_data,
    (MC_SET_SAVE_FUNC *)0
};

static void SetMemoryCardSaveDataInfo(MC_SAVE_DATA *buff, int dir_label,
                                      int file_label);

/* --------------------------------------------------------------------------
 *  PORT DEVIATION -- no ROM counterpart.
 *
 *  Four of the game-slot blocks hold live pointers:
 *
 *      block  5  SIS_MOTION::sat          -> one of ten static tables
 *      block 14  EV_CTRL_CENTER::exe_addr -> the event macro pak
 *      block 15  EV_EXE_CTRL::event_addr  -> the event macro pak
 *      block 47  CFEneEntry::mpFD         -> one of the fene_dat* ghost sets
 *
 *  On the PS2 those addresses are fixed by the link map and by the pak always
 *  loading to EVENT_DATA_ADDR, so writing them into the file and reading them
 *  back is sound.  On the host neither base survives the process: the event
 *  pak lives in the emulated EE RAM, an ordinary allocation placed somewhere
 *  new every run, and the motion tables and the drifting-ghost sets live in
 *  this executable's image.  A save written in one run and loaded in the next
 *  therefore restored pointers that were stale by the base delta -- the event
 *  interpreter resumed on a garbage cursor (EvPushPad() reading a pad label of
 *  0x7b00 out of it), Mayu's motion driver followed a dangling table, and
 *  CFEneEntry::Release() walked mGhostLabel[] off a dead ghost set.
 *
 *  So each owner converts its own pointers to something process-independent
 *  before the block is written and back afterwards: an EE address for the two
 *  event blocks, a table index for the motion and ghost-set ones.  None of
 *  them changes a block's size, so the file layout is exactly as before -- and
 *  the two event blocks now hold the same EE addresses the PS2's file held.
 *
 *  Only the five save slots carry these blocks; the system file, the play-data
 *  header and the albums have their own manifests.
 * ------------------------------------------------------------------------ */
#define MC_SLOT_HAS_POINTER_BLOCKS(dir_label, file_label) \
    ((dir_label) == 0 && (file_label) >= 2 && (file_label) <= 6)

static void MemoryCardFixupSavePointers(int to_host)
{
    SisMotionSavePtrFixup(to_host);
    EvCtrlCenterSavePtrFixup(to_host);
    EvExeCtrlSavePtrFixup(to_host);
    FeneEntrySavePtrFixup(to_host);
}

/* --------------------------------------------------------------------------
 *  Soft-reset bracket
 *
 *  MemoryCardExeInit() takes the lock for the whole time a screen owns the
 *  card and MemoryCardEnd() drops it.  A card pulled mid-write leaves a bad
 *  checksum, which the load path detects; a reset mid-write leaves the card's
 *  own directory half-updated, which it cannot.
 * ------------------------------------------------------------------------ */
void MemoryCardSoftResetLock(void)
{
    SoftResetLock();                                                    /* 112 */
}

void MemoryCardSoftResetUnlock(void)
{
    SoftResetUnlock();                                                  /* 124 */
}

/* --------------------------------------------------------------------------
 *  MemoryCardSetFilePath
 *
 *  "/<dir>/<file>".  The switch picks the file-name table: dir_label 0 has
 *  seven names, each album exactly one.  A table of tables would have done,
 *  but the ROM spells all six cases out.
 * ------------------------------------------------------------------------ */
void MemoryCardSetFilePath(char *path_name, int dir_label, int file_label)   /* 135 */
{
    /* Both of these are declaration initialisers, not statements: GCC emits
     * the two literal bytes plus one memset of the tail for the first and a
     * single whole-array memset for the second, each on its own line. */
    char fname[55]    = "/";                                            /* 136 */
    char dir_name[21] = "";                                             /* 137 */

    memset(path_name, 0, 55);                                           /* 141 */

    MemoryCardSetDirName(dir_name, dir_label);                          /* 144 */

    strcat(fname, dir_name);                                            /* 147 */
    strcat(fname, "/");                                                 /* 148 */

    switch (dir_label)                                                  /* 150 */
    {
    case 0:                                                             /* 152 */
        strcat(fname, game_file_name[file_label]);                      /* 153 */
        break;

    case 1:                                                             /* 155 */
        strcat(fname, album1_file_name[file_label]);                    /* 156 */
        break;

    case 2:                                                             /* 158 */
        strcat(fname, album2_file_name[file_label]);                    /* 159 */
        break;

    case 3:                                                             /* 161 */
        strcat(fname, album3_file_name[file_label]);                    /* 162 */
        break;

    case 4:                                                             /* 164 */
        strcat(fname, album4_file_name[file_label]);                    /* 165 */
        break;

    case 5:                                                             /* 167 */
        strcat(fname, album5_file_name[file_label]);                    /* 168 */
        break;

    default:
        MemoryCardAssert("Error! MemoryCardSetFileName");               /* 170 */
        break;
    }

    strcpy(path_name, fname);                                           /* 175 */
}

/* --------------------------------------------------------------------------
 *  SetMemoryCardSaveDataToBuff
 *
 *  Concatenate every live block into `addr0` and stamp the checksum over the
 *  last four bytes.  The gather is byte-at-a-time because the blocks are
 *  arbitrary structs at arbitrary addresses and the destination cursor never
 *  realigns.
 * ------------------------------------------------------------------------ */
void SetMemoryCardSaveDataToBuff(char *addr0, int dir_label, int file_label) /* 184 */
{
    char *addr1;
    char *data_start_addr;
    int   i;
    int   j;
    int   size;
    MC_SAVE_DATA save_data_buff[100];

    data_start_addr = addr0;

    size = GetMemoryCardDataSize(dir_label, file_label);                /* 193 */

    SetMemoryCardSaveDataInfo(save_data_buff, dir_label, file_label);    /* 203 */

    /* PORT: see MemoryCardFixupSavePointers().  Converted in place around the
     * copy and converted straight back, so the running game never sees the
     * encoded form. */
    if (MC_SLOT_HAS_POINTER_BLOCKS(dir_label, file_label))
    {
        MemoryCardFixupSavePointers(0);
    }

    for (i = 0; i < 100 && !(save_data_buff[i].addr == (u_char *)0 &&
                             save_data_buff[i].size == -1); i++)        /* 208 */
    {
        addr1 = (char *)save_data_buff[i].addr;                          /* 214 */

        for (j = 0; j < save_data_buff[i].size; j++)                    /* 216 */
        {
            *addr0 = *addr1;                                            /* 215 */
            addr0++;
            addr1++;
        }
    }                                                                   /* 217 */

    if (MC_SLOT_HAS_POINTER_BLOCKS(dir_label, file_label))
    {
        MemoryCardFixupSavePointers(1);
    }

    /* The ROM computes the sum first and passes it straight on; the last four
     * bytes of the file are the only ones not covered by it. */
    SetMemoryCardDataCheckSum(addr0,                                    /* 223 */
        CalcMemoryCardDataCheckSum(data_start_addr, size - 4));          /* 220 */
}

/* --------------------------------------------------------------------------
 *  DevelopMemoryCardLoadData
 *
 *  The reverse of the above: walk the same manifest and scatter the buffer
 *  back over the live blocks.
 *
 *  ROM BEHAVIOUR, kept: the NULL-address warning at line 251 does not skip the
 *  block -- control falls straight into the copy with addr1 == NULL.  It only
 *  triggers on a manifest entry whose callback left addr NULL while leaving
 *  size something other than -1, which no shipped callback does.
 * ------------------------------------------------------------------------ */
void DevelopMemoryCardLoadData(char *addr0, int dir_label, int file_label)   /* 235 */
{
    u_char *addr1;
    int     i;
    int     j;
    MC_SAVE_DATA save_data_buff[100];

    SetMemoryCardSaveDataInfo(save_data_buff, dir_label, file_label);    /* 242 */

    for (i = 0; i < 100 && !(save_data_buff[i].addr == (u_char *)0 &&
                             save_data_buff[i].size == -1); i++)        /* 247 */
    {                                                                   /* 248 */
        if (save_data_buff[i].addr == (u_char *)0)                      /* 250 */
        {
            MemoryCardWarning("Warning %s", __FUNCTION__);              /* 251 */
        }

        addr1 = save_data_buff[i].addr;                                 /* 254 */

        /* TEMP PROBE -- remove.  What every block restores, and the before /
         * after bytes, so a block writing into a C++ object's vtable pointer
         * is visible. */
        {
            static int dbg_load;
            if (dbg_load < 200)
            {
                dbg_load++;
                printf("[LOAD] block %2d addr=%p size=%d before=%08x%08x",
                       i, (void *)addr1, save_data_buff[i].size,
                       addr1 ? ((u_int *)addr1)[1] : 0,
                       addr1 ? ((u_int *)addr1)[0] : 0);
                printf(" incoming=%08x\n",
                       save_data_buff[i].size >= 4 ? *(u_int *)addr0 : 0);
                fflush(stdout);
            }
        }

        for (j = 0; j < save_data_buff[i].size; j++)                    /* 257 */
        {
            *addr1 = *addr0;                                            /* 258 */
            addr1++;
            addr0++;                                                    /* 259 */
        }
    }                                                                   /* 260 */

    /* PORT: see MemoryCardFixupSavePointers().  The blocks just restored hold
     * EE addresses and a table index; turn them back into live pointers before
     * anything runs off them. */
    if (MC_SLOT_HAS_POINTER_BLOCKS(dir_label, file_label))
    {
        MemoryCardFixupSavePointers(1);
    }
}

/* --------------------------------------------------------------------------
 *  GetMemoryCardDirSize / GetMemoryCardDirSizeCluster
 *
 *  What a whole directory costs on the card.  The two are the same expression,
 *  once in bytes and once with every term rounded up to a 1 KB cluster:
 *
 *      icon.sys (0x3c4)  +  each data file  +  the icon model
 *      +  5 clusters (game data) or 2 (album)  +  2 for the directory itself
 *
 *  The cluster form is the one that matters: MemoryCardCheckDirBroken()
 *  compares it against GetMemoryCardCheckDirSize(), which derives the same
 *  number from the card's own directory listing, and the two agree exactly
 *  because that function's "+ (entries - 1) / 2 + 2" reproduces the 5 + 2.
 *
 *  The loop bounds 7 and 1 are GetMemoryCardDataFileNum()'s table values
 *  written out as literals -- the ROM does not call it here.
 * ------------------------------------------------------------------------ */
int GetMemoryCardDirSize(int dir_label)                                 /* 271 */
{
    int dir_size;
    int i;

    dir_size = sizeof(sceMcIconSys);                                    /* 279 */

    if (dir_label == 0)                                                 /* 281 */
    {
        for (i = 0; i < 7; i++)                                         /* 284 */
        {                                                              /* 285 */
            dir_size += GetMemoryCardDataSize(0, i);                    /* 286 */
        }

        dir_size += GetIconDataSize(0, 0) + 1024 * 5;                   /* 289 */
    }
    else if (dir_label < 0 || dir_label > 5)                            /* 294 */
    {
        MemoryCardAssert("Error! %s", __FUNCTION__);                    /* 301 */
    }
    else
    {
        for (i = 0; i < 1; i++)                                         /* 306 */
        {
            dir_size += GetMemoryCardDataSize(dir_label, i);
        }

        dir_size += GetIconDataSize(dir_label, 0) + 1024 * 2;           /* 311 */
    }                                                                   /* 313 */

    return dir_size + 1024 * 2;                                         /* 320 */
}

int GetMemoryCardDirSizeCluster(int dir_label)                          /* 328 */
{
    int dir_size;
    int i;

    dir_size = (sizeof(sceMcIconSys) + 1023) / 1024;                    /* 336 */

    if (dir_label == 0)                                                 /* 338 */
    {
        for (i = 0; i < 7; i++)                                         /* 341 */
        {                                                              /* 342 */
            dir_size += (GetMemoryCardDataSize(0, i) + 1023) / 1024;    /* 343 */
        }

        dir_size += (GetIconDataSize(0, 0) + 1023) / 1024 + 5;          /* 346 */
    }
    else if (dir_label < 0 || dir_label > 5)                            /* 351 */
    {
        MemoryCardAssert("Error! %s", __FUNCTION__);                    /* 358 */
    }
    else
    {
        for (i = 0; i < 1; i++)                                         /* 363 */
        {
            dir_size += (GetMemoryCardDataSize(dir_label, i) + 1023) / 1024;
        }

        dir_size += (GetIconDataSize(dir_label, 0) + 1023) / 1024 + 2;  /* 368 */
    }                                                                   /* 370 */

    return dir_size + 2;                                                /* 377 */
}

/* --------------------------------------------------------------------------
 *  GetMemoryCardDataSize
 *
 *  Sum of the manifest's block sizes plus four for the checksum.  A file whose
 *  manifest is empty is still 4 bytes -- that is the value the ROM returns
 *  from its early-out, and it keeps the checksum machinery uniform.
 * ------------------------------------------------------------------------ */
int GetMemoryCardDataSize(int dir_label, int file_label)                /* 387 */
{
    int i;
    int size;
    MC_SAVE_DATA save_data_buff[100];

    size = 0;                                                           /* 389 */

    SetMemoryCardSaveDataInfo(save_data_buff, dir_label, file_label);    /* 394 */

    for (i = 0; i < 100 && !(save_data_buff[i].addr == (u_char *)0 &&
                             save_data_buff[i].size == -1); i++)        /* 399 */
    {                                                                   /* 400 */
        size += save_data_buff[i].size;                                  /* 402 */
    }                                                                   /* 403 */

    return size + 4;                                                    /* 409 */
}

/* --------------------------------------------------------------------------
 *  SetMemoryCardSaveDataInfo
 *
 *  Run a manifest into `buff`.  Each callback fills one {addr, size} slot; the
 *  slot after the last one gets {NULL, -1}, which is the terminator the three
 *  walkers above test for.  A NULL *manifest* (file_label past the end of the
 *  table) stops without writing a terminator at all, leaving whatever the
 *  caller's uninitialised stack held -- harmless in practice, since every
 *  caller passes a file_label the table covers.
 * ------------------------------------------------------------------------ */
static void SetMemoryCardSaveDataInfo(MC_SAVE_DATA *buff, int dir_label,
                                      int file_label)                   /* 418 */
{
    int  i;
    char data_end;

    data_end = 0;                                                       /* 423 */

    for (i = 0; i < 100; i++)                                           /* 425 */
    {
        if (dir_label == 0)                                             /* 426 */
        {
            if (game_save_data[file_label] == (MC_SET_SAVE_FUNC *)0)
            {
                data_end = 1;                                           /* 428 */
            }
            else if (game_save_data[file_label][i] == (MC_SET_SAVE_FUNC)0) /* 433 */
            {
                buff->addr = (u_char *)0;
                data_end   = 1;
                buff->size = -1;
            }
            else
            {
                (*game_save_data[file_label][i])(buff);
                buff++;
            }
        }
        else if (dir_label >= 0 && dir_label < 6)
        {
            if (album_save_data[file_label] == (MC_SET_SAVE_FUNC *)0)    /* 447 */
            {
                data_end = 1;                                           /* 448 */
            }
            else if (album_save_data[file_label][i] == (MC_SET_SAVE_FUNC)0) /* 450 */
            {
                buff->addr = (u_char *)0;
                data_end   = 1;                                         /* 458 */
                buff->size = -1;                                        /* 460 */
            }
            else
            {
                (*album_save_data[file_label][i])(buff);                /* 451 */
                buff++;                                                 /* 452 */
            }
        }
        else
        {
            MemoryCardAssert("Error! SetMemoryCardSaveDataInfo");        /* 462 */
        }

        if (data_end != 0)                                              /* 465 */
        {
            break;
        }
    }                                                                   /* 469 */
}

/* --------------------------------------------------------------------------
 *  Staging buffer
 * ------------------------------------------------------------------------ */
void *GetDataMemoryArea(int size)
{
    return mem_utilGetMem(size);                                        /* 490 */
}

void LiberateDataMemoryArea(void *data_addr)                            /* 503 */
{
    if (data_addr != (void *)0)                                         /* 504 */
    {
        mem_utilFreeMem(data_addr);                                     /* 505 */
    }
}

/* --------------------------------------------------------------------------
 *  Icon-model load.  Plain CD traffic -- the icon is an ordinary file and goes
 *  through the ordinary loader, not through the card.
 * ------------------------------------------------------------------------ */
void MemoryCardDataLoadReq(void *addr, int data_label)                  /* 520 */
{
    LoadReq(data_label, (uintptr_t)addr);                               /* 523 */
}

int MemoryCardDataLoadWait(void)                                        /* 539 */
{
    return (IsLoadEndAll() != 0);                                       /* 544 */
}

/* --------------------------------------------------------------------------
 *  GetMemoryCardDataFileNum / GetMemoryCardAllFileNum
 *
 *  Both read a six-entry table that the compiler materialises on the stack
 *  from .rodata, so neither is a file-scope static.  The "all" count adds the
 *  icon and icon.sys (that is where 7 -> 9 and 1 -> 3 comes from) and then the
 *  two clusters for "." and ".." -- which is what makes it directly comparable
 *  with the card's own directory entry count.
 *
 *  Neither bounds-checks a negative dir_label; the assert only catches > 5.
 * ------------------------------------------------------------------------ */
int GetMemoryCardDataFileNum(int dir_label)                             /* 556 */
{
    int data_file_num[6] = { 7, 1, 1, 1, 1, 1 };                        /* 557 */

    if (dir_label > 5)                                                  /* 568 */
    {
        MemoryCardAssert("Error! GetMemoryCardDataFileNum");            /* 569 */
    }

    return data_file_num[dir_label];                                    /* 574 */
}

int GetMemoryCardAllFileNum(int dir_label)                              /* 583 */
{
    int dir_file_num[6] = { 9, 3, 3, 3, 3, 3 };                         /* 584 */

    if (dir_label > 5)                                                  /* 595 */
    {
        MemoryCardAssert("Error! GetMemoryCardAllFileNum");             /* 596 */
    }

    return dir_file_num[dir_label] + 2;                                 /* 602 */
}

/* --------------------------------------------------------------------------
 *  Play-data header
 * ------------------------------------------------------------------------ */
void MemoryCardPlayDataHeadInit(void)                                   /* 612 */
{
    memset(&mc_play_data_head, 0, sizeof(mc_play_data_head));           /* 615 */
}

/* Snapshot the current game into slot `data_num`.  Called on save, not on
 * load, so everything read here is the live state. */
void SetMemoryCardPlayDataHead(int data_num, char clear_flg)
{
    mc_play_data_head.data_flg[data_num]       = 1;                     /* 624 */
    mc_play_data_head.chapter[data_num]        = ingame_wrk.mChapterNo.Get();
    mc_play_data_head.clear_num[data_num]      = ingame_wrk.mClearCnt.Get();
    mc_play_data_head.room_label[data_num]     = GetPlyrRoomLabel();
    mc_play_data_head.play_time[data_num]      = GetPlayTime();         /* 634 */
    mc_play_data_head.clear_data_flg[data_num] = clear_flg;             /* 635 */
}

int GetMemoryCardPlayDataFlg(int data_num)
{
    return mc_play_data_head.data_flg[data_num];                        /* 645 */
}

int GetMemoryCardClearDataFlg(int data_num)
{
    return mc_play_data_head.clear_data_flg[data_num];                  /* 656 */
}

int GetMemoryCardPlayDataChapter(int data_num)
{
    return mc_play_data_head.chapter[data_num];                         /* 667 */
}

int GetMemoryCardPlayDataClearNum(int data_num)
{
    return mc_play_data_head.clear_num[data_num];                       /* 678 */
}

int GetMemoryCardPlayDataRoomLabel(int data_num)
{
    return mc_play_data_head.room_label[data_num];                      /* 689 */
}

TIME_INFO GetMemoryCardPlayDataPlayTime(int data_num)                   /* 700 */
{
    return mc_play_data_head.play_time[data_num];                       /* 705 */
}

/* The header is itself a save block -- this is the callback
 * save_play_data_head[] points at. */
void SetSave_PlayDataHead(MC_SAVE_DATA *data)
{
    data->addr = (u_char *)&mc_play_data_head;                          /* 714 */
    data->size = sizeof(mc_play_data_head);                             /* 715 */
}

/* --------------------------------------------------------------------------
 *  Name and path builders
 * ------------------------------------------------------------------------ */
void MemoryCardSetDirName(char *dir_name, int dir_label)                /* 728 */
{
    memset(dir_name, 0, 21);                                            /* 731 */

    if (dir_label < 6)                                                  /* 733 */
    {
        strcpy(dir_name, game_dir_name[dir_label]);                     /* 735 */
    }
    else
    {
        MemoryCardAssert("Error! MemoryCardSetDirName");                /* 740 */
    }
}

/* "<dir>/*" -- sceMcGetDir()'s wildcard.  No leading slash, unlike the file
 * paths: the ROM builds this one straight into the caller's buffer. */
void MemoryCardMakeSearchDirPath(char *path_name, int dir_label)         /* 753 */
{
    memset(path_name, 0, 55);                                           /* 756 */
    MemoryCardSetDirName(path_name, dir_label);                          /* 759 */
    strcat(path_name, "/*");                                            /* 761 */
}

void MemoryCardSetIconSysPath(char *path_name, int dir_label)           /* 775 */
{
    char fname[55]    = "/";                                            /* 776 */
    char dir_name[21] = "";                                             /* 777 */

    MemoryCardSetDirName(dir_name, dir_label);                          /* 781 */

    strcat(fname, dir_name);                                            /* 785 */
    strcat(fname, "/");                                                 /* 786 */
    strcat(fname, "icon.sys");                                          /* 788 */

    strcpy(path_name, fname);                                           /* 792 */
}

/* --------------------------------------------------------------------------
 *  MemoryCardSetIconSysData
 *
 *  Build the icon.sys the browser reads.  Returned by value, which is what the
 *  ROM does too -- 964 bytes through the caller's storage.
 *
 *  OffsLF 28 is the byte offset of the line break in TitleName, and
 *  mc_icon_title is exactly 28 bytes, so the two-row title splits between the
 *  game name and the per-directory sub-title.
 * ------------------------------------------------------------------------ */
sceMcIconSys MemoryCardSetIconSysData(int dir_label)                    /* 800 */
{
    sceMcIconSys icon;

    memset(&icon, 0, sizeof(icon));                                     /* 805 */

    /* Five bytes into the four of Head: the NUL lands on Reserv1, which the
     * memset above has already zeroed.  The ROM writes it the same way.
     * PORT: aimed at the struct rather than at Head, which writes the same
     * bytes.  A fortified strcpy (Ubuntu's GCC enables _FORTIFY_SOURCE by
     * default) checks against the member it is handed and would abort every
     * save with "*** buffer overflow detected ***". */
    strcpy((char *)&icon, "PS2D");                                      /* 808 */

    MemoryCardSetIconTitleName((char *)icon.TitleName, dir_label);      /* 811 */

    icon.OffsLF    = 28;                                                /* 813 */
    icon.TransRate = 96;                                                /* 814 */

    memcpy(icon.BgColor,    mc_bgcolor,  sizeof(mc_bgcolor));           /* 815 */
    memcpy(icon.LightDir,   mc_lightdir, sizeof(mc_lightdir));          /* 816 */
    memcpy(icon.LightColor, mc_lightcol, sizeof(mc_lightcol));          /* 817 */
    memcpy(icon.Ambient,    mc_ambient,  sizeof(mc_ambient));           /* 818 */

    MemoryCardSetIconFileName((char *)icon.FnameView, dir_label, 0);    /* 821 */
    MemoryCardSetIconFileName((char *)icon.FnameCopy, dir_label, 1);    /* 822 */
    MemoryCardSetIconFileName((char *)icon.FnameDel,  dir_label, 2);    /* 823 */

    return icon;                                                        /* 826 */
}

/* Two full-width Shift-JIS rows: the game title, then the directory's own
 * label.  68 bytes is sceMcIconSys::TitleName's whole field. */
void MemoryCardSetIconTitleName(char *title, int dir_label)
{
    char title_name[68] = "";                                           /* 834 */

    memset(title, 0, 68);                                               /* 835 */

    strcpy(title_name, mc_icon_title);                                  /* 839 */

    if (dir_label >= 0 && dir_label < 6)                                /* 842 */
    {
        strcat(title_name, mc_icon_sub_title[dir_label]);               /* 844 */
    }
    else
    {
        MemoryCardAssert("Error! MemoryCardSetIconTitleName");          /* 846 */
    }                                                                   /* 851 */

    strcpy(title, title_name);                                          /* 856 */
}

void MemoryCardSetIconFileName(char *icon_name, int dir_label, int icon_type)  /* 870 */
{
    memset(icon_name, 0, 31);                                           /* 873 */
    strcpy(icon_name, mc_icon_name[dir_label][icon_type]);              /* 876 */
}

void MemoryCardSetIconFilePath(char *icon_path, int dir_label, int icon_type) /* 888 */
{
    char fname[55]    = "/";                                            /* 889 */
    char dir_name[21] = "";                                             /* 890 */

    memset(icon_path, 0, 55);                                           /* 894 */

    MemoryCardSetDirName(dir_name, dir_label);                          /* 896 */

    strcat(fname, dir_name);                                            /* 900 */
    strcat(fname, "/");                                                 /* 901 */
    strcat(fname, mc_icon_name[dir_label][icon_type]);                  /* 903 */

    strcpy(icon_path, fname);                                           /* 907 */
}

/* --------------------------------------------------------------------------
 *  Icon data
 * ------------------------------------------------------------------------ */
int GetIconDataLabel(int dir_label, int icon_type)
{
    return icon_data_label[dir_label][icon_type];                       /* 920 */
}

int GetIconDataSize(int dir_label, int icon_type)                       /* 933 */
{
    int file_no;

    file_no = GetIconDataLabel(dir_label, icon_type);

    return GetFileSize(file_no);                                        /* 936 */
}

/* --------------------------------------------------------------------------
 *  MemoryCardDebugReqSizeDisp
 *
 *  Boot-time cluster budget, printed from main.c.  Only the first two rows are
 *  printed although the name table holds six -- the game-data directory and
 *  album 1 are the two whose cost the layout actually had to be checked
 *  against; albums 2..5 are copies of album 1.
 * ------------------------------------------------------------------------ */
void MemoryCardDebugReqSizeDisp(void)                                   /* 946 */
{
    /* A stack array, not a static: the initialiser is a .rodata blob the
     * compiler copies in.  21 entries with only six set, matching the
     * dir_name[21] buffers the path builders use. */
    char *dir_name[21] =                                                /* 949 */
    {
        (char *)"ZERO2 GAME DATA",
        (char *)"ZERO2 ALBUM DATA",
        (char *)"ZERO2 ALBUM DATA",
        (char *)"ZERO2 ALBUM DATA",
        (char *)"ZERO2 ALBUM DATA",
        (char *)"ZERO2 ALBUM DATA"
    };
    int i;

    printf("******************** MemoryCard Data Size ********************\n"); /* 959 */

    for (i = 0; i < 2; i++)                                             /* 961 */
    {
        printf("%s Req Size %d Cluster\n", dir_name[i],
               GetMemoryCardDirSizeCluster(i));                         /* 962 */
    }

    printf("**************************************************************\n"); /* 963 */
}                                                                       /* 966 */

/* --------------------------------------------------------------------------
 *  Reporting
 *
 *  All three format into a 1000-byte stack buffer first so that the message
 *  reaches the reporter as a single already-expanded string.  That is why the
 *  banner PRINT_ASSERT / PRINT_WARNING stamp names mc_set_data.c and lines
 *  1010 / 1034 rather than the call site -- a ROM line number recovered for
 *  one of the 36 MemoryCardAssert() call sites belongs to the caller, not to
 *  the banner.
 * ------------------------------------------------------------------------ */
void MemoryCardPrint(char *str, ...)                                    /* 975 */
{
    va_list ap;
    char    buf[1000];

    va_start(ap, str);                                                  /* 981 */
    vsprintf(buf, str, ap);                                             /* 982 */
    va_end(ap);

    printf(buf);                                                        /* 986 */
}

void MemoryCardAssert(char *str, ...)                                   /* 997 */
{
    va_list ap;
    char    buf[1000];

    va_start(ap, str);                                                  /* 1003 */
    vsprintf(buf, str, ap);                                             /* 1004 */
    va_end(ap);

    printf("MemoryCardAssert!!\n");                                     /* 1009 */
    PRINT_ASSERT(buf);                                                  /* 1010 */
}

void MemoryCardWarning(char *str, ...)                                  /* 1021 */
{
    va_list ap;
    char    buf[1000];

    va_start(ap, str);                                                  /* 1027 */
    vsprintf(buf, str, ap);                                             /* 1028 */
    va_end(ap);

    printf("MemoryCardWarning\n");                                      /* 1033 */
    PRINT_WARNING(buf);                                                 /* 1034 */
}
