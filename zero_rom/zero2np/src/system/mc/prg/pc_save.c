/* ==========================================================================
 *  system/mc/prg/pc_save.c
 *
 *  Development save transfer (pc_save.o, .text 0x22dcd8, 0x1cc bytes).
 *
 *  Slot 1 (dir_label 0, file_label 2) to and from "host0:save_data.dat".  Both
 *  halves go through SetMemoryCardSaveDataToBuff() / DevelopMemoryCardLoadData(),
 *  so the file on the host is byte-identical to the one a card would hold --
 *  checksum included.
 *
 *  ROM BEHAVIOUR, kept: the staging buffer is leaked whenever the open fails.
 *  Both functions allocate before opening and only free inside the success
 *  branch.  It is a debug path, so nothing depended on it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "pc_save.h"

#include "fileio.h"                             /* sceOpen / Read / Write   */
#include "mc_set_data.h"

#include "../../../debug/mem_dbg.h"             /* mem_dbgGetMem / FreeMem  */

void SavePCFile(void)                                                   /* 49 */
{
    int   fd;
    void *data_buff_addr;
    char  fname[20] = "host0:save_data.dat";                            /* 51 */

    /* The size is asked for again at every use rather than kept in a local --
     * three calls in this function, and none of them is cheap. */
    data_buff_addr = mem_dbgGetMem(GetMemoryCardDataSize(0, 2));          /* 56 */

    SetMemoryCardSaveDataToBuff((char *)data_buff_addr, 0, 2);            /* 59 */

    fd = sceOpen(fname, SCE_WRONLY | SCE_CREAT);                         /* 63 */

    if (fd >= 0)                                                        /* 66 */
    {
        sceLseek(fd, 0, SCE_SEEK_SET);                                  /* 72 */
        sceWrite(fd, data_buff_addr, GetMemoryCardDataSize(0, 2));        /* 75 */
        sceClose(fd);                                                   /* 79 */

        mem_dbgFreeMem(data_buff_addr);                                  /* 82 */

        MemoryCardPrint("*****  PC Save OK!!  *****\n");                 /* 84 */
    }
}                                                                       /* 86 */

void LoadPCFile(void)                                                   /* 91 */
{
    int   fd;
    void *data_buff_addr;
    char  fname[20] = "host0:save_data.dat";                            /* 93 */

    data_buff_addr = mem_dbgGetMem(GetMemoryCardDataSize(0, 2));          /* 98 */

    fd = sceOpen(fname, SCE_RDONLY);                                    /* 102 */

    if (fd >= 0)                                                        /* 105 */
    {
        sceLseek(fd, 0, SCE_SEEK_SET);                                  /* 110 */
        sceRead(fd, data_buff_addr, GetMemoryCardDataSize(0, 2));         /* 113 */

        DevelopMemoryCardLoadData((char *)data_buff_addr, 0, 2);          /* 116 */

        sceClose(fd);                                                   /* 120 */

        mem_dbgFreeMem(data_buff_addr);                                  /* 123 */

        MemoryCardPrint("*****  PC Load OK!!  *****\n");                 /* 125 */
    }
}                                                                       /* 127 */
