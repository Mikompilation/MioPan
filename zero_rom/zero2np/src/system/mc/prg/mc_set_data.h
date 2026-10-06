/* ==========================================================================
 *  system/mc/prg/mc_set_data.h
 *
 *  Save-data marshalling, path building and per-slot summaries (mc_set_data.o,
 *  .text 0x1e16f0 -- 0x16c0 bytes, the largest module in system/mc).
 *
 *  Everything that turns a (dir_label, file_label) pair into a card path, a
 *  byte count, or a live block of game state goes through here, plus the three
 *  reporting helpers the rest of the folder calls instead of printf.
 *
 *  Two index spaces run through the whole folder and are worth naming once:
 *
 *    dir_label   0      the game-data directory   (7 files)
 *                1..5   the five photo albums     (1 file each)
 *    file_label  within dir_label 0: 0 system, 1 play-data header,
 *                2..6 the five save slots; within an album: always 0.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_MC_PRG_MC_SET_DATA_H
#define _SYSTEM_MC_PRG_MC_SET_DATA_H

#include "libmc.h"                              /* sceMcIconSys */

#include "../../../common/save_data.h"          /* MC_SAVE_DATA */
#include "../../../common/variable.h"           /* TIME_INFO    */

/* ---- soft-reset bracket ------------------------------------------------ */
/* Held across every card access: pulling a card mid-write is survivable, a
 * reset is not. */
void MemoryCardSoftResetLock(void);                             /* 0x1e17c8 */
void MemoryCardSoftResetUnlock(void);                           /* 0x1e17e8 */

/* ---- path builders ----------------------------------------------------- */
/* "/<dir>/<file>" -- what sceMcOpen() and sceMcDelete() take.  path_name must
 * have room for 55 bytes; the builders zero all of it. */
void MemoryCardSetFilePath(char *path_name, int dir_label, int file_label);   /* 0x1e1808 */
/* Just the directory name, 21 bytes, for sceMcMkdir(). */
void MemoryCardSetDirName(char *dir_name, int dir_label);                     /* 0x1e2428 */
/* "<dir>/*" -- the wildcard sceMcGetDir() takes. */
void MemoryCardMakeSearchDirPath(char *path_name, int dir_label);             /* 0x1e24a0 */
void MemoryCardSetIconSysPath(char *path_name, int dir_label);                /* 0x1e24f8 */
void MemoryCardSetIconFileName(char *icon_name, int dir_label, int icon_type);/* 0x1e28b0 */
void MemoryCardSetIconFilePath(char *icon_path, int dir_label, int icon_type);/* 0x1e2928 */

/* ---- sizes ------------------------------------------------------------- */
/* Byte size of one card file: the sum of its save blocks plus the four-byte
 * checksum.  Also the size of the staging buffer the caller must allocate. */
int GetMemoryCardDataSize(int dir_label, int file_label);        /* 0x1e1dd0 */
/* Whole-directory footprint, in bytes and in 1 KB clusters.  The cluster form
 * is what MemoryCardCheckEmpty() and MemoryCardCheckDirBroken() compare
 * against the card's own numbers. */
int GetMemoryCardDirSize(int dir_label);                         /* 0x1e1bd0 */
int GetMemoryCardDirSizeCluster(int dir_label);                  /* 0x1e1ca8 */
/* Data files in a directory (7 or 1), and the whole entry count the card's
 * directory listing should show -- data files + icon + icon.sys + "." + "..". */
int GetMemoryCardDataFileNum(int dir_label);                     /* 0x1e2028 */
int GetMemoryCardAllFileNum(int dir_label);                      /* 0x1e20a8 */

/* ---- staging buffer ---------------------------------------------------- */
/* Straight onto the general heap; separate names only so the call sites read
 * as card traffic. */
void *GetDataMemoryArea(int size);                              /* 0x1e1fa8 */
void  LiberateDataMemoryArea(void *data_addr);                  /* 0x1e1fc0 */

/* ---- block marshalling ------------------------------------------------- */
/* Gather every live block named by the manifest for (dir_label, file_label)
 * into `addr0`, then stamp the checksum into its last four bytes. */
void SetMemoryCardSaveDataToBuff(char *addr0, int dir_label, int file_label); /* 0x1e19c0 */
/* The reverse: scatter a verified staging buffer back over the live state. */
void DevelopMemoryCardLoadData(char *addr0, int dir_label, int file_label);   /* 0x1e1ac8 */

/* ---- icon data -------------------------------------------------------- */
/* The icon model is an ordinary CD file, loaded through the normal loader. */
void MemoryCardDataLoadReq(void *addr, int data_label);         /* 0x1e1fe0 */
int  MemoryCardDataLoadWait(void);                              /* 0x1e2008 */
int  GetIconDataLabel(int dir_label, int icon_type);            /* 0x1e2a20 */
int  GetIconDataSize(int dir_label, int icon_type);             /* 0x1e2a48 */
/* Build a complete icon.sys for a directory.  Returned by value -- the ROM
 * does the same, copying 0x3c4 bytes through the caller's storage. */
sceMcIconSys MemoryCardSetIconSysData(int dir_label);           /* 0x1e25b0 */
void MemoryCardSetIconTitleName(char *title, int dir_label);    /* 0x1e2810 */

/* ---- play-data header ------------------------------------------------- */
/* The five-slot summary the load screen lists.  Lives in this module's .bss
 * and is itself one of the save blocks (SetSave_PlayDataHead). */
void MemoryCardPlayDataHeadInit(void);                          /* 0x1e2128 */
void SetMemoryCardPlayDataHead(int data_num, char clear_flg);   /* 0x1e2150 */
void SetSave_PlayDataHead(MC_SAVE_DATA *data);                  /* 0x1e2410 */

int       GetMemoryCardPlayDataFlg(int data_num);               /* 0x1e2280 */
int       GetMemoryCardClearDataFlg(int data_num);              /* 0x1e22b8 */
int       GetMemoryCardPlayDataChapter(int data_num);           /* 0x1e22f0 */
int       GetMemoryCardPlayDataClearNum(int data_num);          /* 0x1e2328 */
int       GetMemoryCardPlayDataRoomLabel(int data_num);         /* 0x1e2360 */
TIME_INFO GetMemoryCardPlayDataPlayTime(int data_num);          /* 0x1e2398 */

/* ---- debug ------------------------------------------------------------ */
/* Prints the cluster cost of the game-data directory and album 1 at boot.
 * main.c calls it right after MemoryCardInit(). */
void MemoryCardDebugReqSizeDisp(void);                          /* 0x1e2a68 */

/* ---- reporting -------------------------------------------------------- */
/* The folder's own three reporters.  All three vsprintf into a 1000-byte
 * stack buffer first, so a caller can format freely; MemoryCardAssert() then
 * routes the result through PRINT_ASSERT and MemoryCardWarning() through
 * PRINT_WARNING, which is why they carry mc_set_data.c's line numbers rather
 * than the caller's. */
void MemoryCardPrint(char *str, ...);                           /* 0x1e2bc0 */
void MemoryCardAssert(char *str, ...);                          /* 0x1e2c40 */
void MemoryCardWarning(char *str, ...);                         /* 0x1e2ce8 */

#endif /* _SYSTEM_MC_PRG_MC_SET_DATA_H */
