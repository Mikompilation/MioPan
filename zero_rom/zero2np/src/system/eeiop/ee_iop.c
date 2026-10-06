/* ==========================================================================
 *  system/eeiop/ee_iop.c
 *
 *  The EE side of the EE<->IOP link.  Three jobs:
 *
 *    - bring-up.  ee_iopInit() reboots the IOP from the IOPRP image, reads
 *      the HIL index of the game's IRX modules, pushes the DIL image holding
 *      them into IOP memory and starts each one; then ee_iopInitSub() binds
 *      the RPC channel and hands the one EE work buffer to fileload.c and
 *      snd.c in turn.
 *    - the command queue.  Everything the EE wants from the IOP is appended
 *      to iop_com_buffer by iopCommandRegister() as a (command, payload)
 *      pair, and the whole batch goes across once a frame.
 *    - the status block.  The reply to that call is an IOP_RET_STATUS, and
 *      the five accessors at the bottom are how the sound modules read it.
 *
 *  PORT NOTE: there is no IOP.  The bring-up is reconstructed and compiled
 *  but skipped at runtime (see ee_iop_boot_iop), and the SIF transport under
 *  src/sdk is a set of succeed-and-do-nothing shims, so iop_ret never
 *  actually fills.  The EE-side half -- the work-buffer layout, the command
 *  queue and the frame pump -- is live.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdio.h>
#include <string.h>

#include "ee_iop.h"                 /* this file's public API + EEIOP_DEF */

#include "ee_iop_q.h"               /* ee_iopQueryInit                    */
#include "file_stream.h"            /* FileStreamInit                     */
#include "fileload.h"               /* FileLoadGetNeedSize / FileLoadInit */
#include "snd.h"                    /* sndGetNeedSize / SndInit / SndMain */

#include "../../common/utility2.h"  /* PRINT_ASSERT, GetAlignUp           */
#include "../../sdk/eekernel.h"     /* FlushCache                         */
#include "../../sdk/fileio.h"       /* sceOpen / sceRead / sceLseek       */
#include "../../sdk/libcdvd.h"      /* sceCdInit / sceCdMmode             */
#include "../../sdk/sif.h"          /* sceSifAllocSysMemory               */
#include "../../sdk/sifdev.h"       /* the loadfile / iopheap family      */
#include "../../sdk/sifrpc.h"       /* sceSifBindRpc / sceSifCallRpc      */

/* PORT: gate on the IOP bring-up.  Everything under it is the ROM's own code
 * and is still compiled and type-checked, but on the host it can only fail --
 * the IOPRP and HIL/DIL images live on the PS2 disc at "cdrom0:\\...", and
 * the ROM treats a failed open or module start as fatal (`return 0`), which
 * would take the EE-side init below down with it.  Set to 1 the day an IOP
 * emulation exists. */
static const int ee_iop_boot_iop = 0;

static void *(*ee_iop_malloc)(int size);                                     /* sbss 3f5018 */
static void  (*ee_iop_free)(void *adrs);                                     /* sbss 3f501c */

/* Both are SIF DMA endpoints and sit on 64-byte boundaries in the ROM
 * (0x4be840 / 0x4bfa00); iop_ret_buffer is additionally read back through an
 * IOP_RET_STATUS *, so its alignment is not decorative. */
ATTRIBUTE_ALIGNED(64, static char             iop_com_buffer[4096]); /* bss 4be840 */  
static IOP_RET_STATUS   iop_ret;                                             /* bss 4bf840 */
ATTRIBUTE_ALIGNED(64, static char             iop_ret_buffer[448]); /* bss 4bfa00 */   
static int              iop_com_offset;                                      /* sbss 3f5020 */
static sceSifClientData sif_cli_data;                                        /* bss 4bfbc0 */

/* --------------------------------------------------------------------------
 *  Allocation.  Both indirect through EEIOP_DEF, so the caller's heap owns
 *  everything this module and its dependants allocate.
 * ------------------------------------------------------------------------ */

void *ee_iopMalloc(int size)
{
    return ee_iop_malloc(size);                                              /* 84 */
}

void ee_iopFree(void *adrs)
{
    ee_iop_free(adrs);                                                       /* 90 */
}

int ee_iopGetNeedSize(EEIOP_DEF *def)
{
    /* Both halves have to be counted: ee_iopInitSub() lays the file loader's
     * tables down first and SndInit() carves the rest out of the same block. */
    return sndGetNeedSize(def) +                                             /* 96 */
           FileLoadGetNeedSize(def);                                         /* 97, 99 */
}

/* --------------------------------------------------------------------------
 *  RPC bring-up and the EE-side work-buffer hand-off
 * ------------------------------------------------------------------------ */

static void *ee_iopInitSub(EEIOP_DEF *def, void *buffer)
{
    int   i;
    void *next;

    /* The IOP's server may not have registered its service yet, so bind, spin
     * a while, and retry until sif_cli_data.serve comes back set. */
    do
    {
        if (sceSifBindRpc(&sif_cli_data, 1, 0) < 0)
        {
            printf("error: sceSifBindRpc\n");
            for (;;)
                ;
        }

        for (i = 0x270b; i >= 0; i -= 4)
            ;
    } while (sif_cli_data.serve == (struct _sif_serve_data *)0);

    /* One synchronous round trip carrying just REQ_IOP_REBOOT, which is what
     * tells the IOP side its own subsystems may start. */
    iopCommandFrameInit();
    iopCommandRegister(REQ_IOP_REBOOT, (char *)0, 0);
    iopCommandRegister(IOP_COM_END, (char *)0, 0);

    FlushCache(0);
    sceSifCallRpc(&sif_cli_data, 0, 0, iop_com_buffer,
                  GetAlignUp(iop_com_offset, 4), (void *)0, 0,
                  (sceSifEndFunc)0, (void *)0);

    ee_iopQueryInit();
    printf("rpc_query_ok\n");

    next = FileLoadInit(def, buffer);
    printf("rpc_file_load ok\n");

    FileStreamInit();
    printf("rpc_file_stream ok\n");

    iopCommandFrameInit();

    return next;
}

/* 114 */
int ee_iopInit(EEIOP_DEF *def)
{
    int             size;
    HIL_FORMAT     *hil;
    HIL_ONE_FORMAT *work;
    void           *iopaddr;
    int             i;
    int             fd;
    void           *buffer;
    char            temp[9];

    ee_iop_malloc = def->malloc64;                                           /* 114 */
    ee_iop_free   = def->free64;                                             /* 115 */

    if (ee_iop_boot_iop)
    {
        if (def->rom_boot)                                                   /* 117 */
        {
            sceSifInitRpc(0);                                                /* 120 */
            sceCdInit(0);                                                    /* 121 */

            while (sceSifRebootIop(def->iop_def_module) == 0)                /* 122 */
                ;
            while (sceSifSyncIop() == 0)                                     /* 123 */
                ;
        }

        sceSifInitRpc(0);                                                    /* 127 */
        sceSifLoadFileReset();                                               /* 128 */
        sceFsReset();                                                        /* 129 */

        sceCdInit(0);                                                        /* 132 */
        sceCdMmode(def->media);                                              /* 133 */

        sceSifInitIopHeap();                                                 /* 135 */
        printf("(int)(sceSifAllocSysMemory(1, 0x600000, 0 )) = 0x%x\n", sceSifAllocSysMemory(1, 0x600000, (void *)0));                /* 137 */

        /* The HIL is an index: a count plus one 16-byte record per IRX giving
         * its name and its offset into the DIL image. */
        fd = sceOpen(def->hil_file_name, SCE_RDONLY);                        /* 142 */
        if (fd < 0)                                                          /* 143 */
        {
            printf("can't open %s\n", def->hil_file_name);                   /* 144 */
            return 0;                                                        /* 145 */
        }

        size = sceLseek(fd, 0, SCE_SEEK_END);                                /* 147 */
        sceLseek(fd, 0, SCE_SEEK_SET);                                       /* 148 */

        hil = (HIL_FORMAT *)ee_iopMalloc(size);                              /* 149 */
        if (size != sceRead(fd, hil, size))                                  /* 150 */
        {
            printf("Read Err\n");                                            /* 152 */
            return 0;                                                        /* 153 */
        }
        sceClose(fd);                                                        /* 155 */

        /* 0x120000 is where the IOP-side layout expects the module image; a
         * failure there is an assert rather than fatal, and the retry takes
         * whatever the heap will give. */
        iopaddr = sceSifAllocSysMemory(2, hil->dil_size, (void *)0x120000);  /* 169 */
        printf("(int)iopaddr = 0x%x\n", iopaddr);                            /* 170 */
        if (iopaddr == (void *)0)                                            /* 171 */
        {
            PRINT_ASSERT("Sif Missed Appointed Memory Alloc");               /* 172 */
            iopaddr = sceSifAllocSysMemory(0, hil->dil_size, (void *)0);     /* 173 */
        }

        if (sceSifLoadIopHeap(def->dil_file_name, iopaddr) < 0)              /* 177 */
        {
            printf("sceSifLoadIopHeap() Err %s\n", def->dil_file_name);      /* 178, 179 */
            return 0;
        }

        printf("pre_LoadModuleBuffer\n");                                    /* 185 */
        printf("sceSifQueryMaxFreeMemSize() = 0x%x\n", sceSifQueryMaxFreeMemSize()); /* 186 */
        printf("sceSifQueryTotalFreeMemSize() = 0x%x\n", sceSifQueryTotalFreeMemSize());                               /* 187 */

        work = (HIL_ONE_FORMAT *)(hil + 1);
        for (i = 0; i < hil->num; i++)                                       /* 192 */
        {
            /* irx_name is a bare 8-char field, so it has to be terminated
             * before it can be printed. */
            temp[8] = '\0';                                                  /* 196 */
            memcpy(temp, work[i].irx_name, 8);                               /* 197 */

            if (sceSifLoadModuleBuffer((char *)iopaddr + work[i].offset,
                                       0, (char *)0) < 0)                    /* 202 */
            {
                printf("irx start err %s\n", temp);                          /* 204 */
                return 0;                                                    /* 207 */
            }
        }

        printf("after_LoadModuleBuffer\n");                                  /* 210 */
        printf("sceSifQueryMaxFreeMemSize() = 0x%x\n", sceSifQueryMaxFreeMemSize()); /* 211 */
        printf("sceSifQueryTotalFreeMemSize() = 0x%x\n", sceSifQueryTotalFreeMemSize()); /* 212 */

        /* The index and the staging image are both dead once the modules are
         * running out of IOP memory. */
        ee_iopFree(hil);                                                     /* 215 */
        sceSifFreeIopHeap(iopaddr);                                          /* 216 */
    }

    /* One buffer for both halves; ee_iopInitSub() returns the cursor past the
     * file loader's tables and SndInit() carves the sound side out of that. */
    buffer = ee_iopMalloc(ee_iopGetNeedSize(def));                           /* 230 */

    printf("pre ee_iopInit\n");                                              /* 232 */
    printf("sceSifQueryMaxFreeMemSize() = 0x%x\n", sceSifQueryMaxFreeMemSize());       /* 233 */
    printf("sceSifQueryTotalFreeMemSize() = 0x%x\n", sceSifQueryTotalFreeMemSize());   /* 234 */

    buffer = ee_iopInitSub(def, buffer);                                     /* 236 */
    printf("pre SndInit\n");                                                 /* 237 */
    printf("sceSifQueryMaxFreeMemSize() = 0x%x\n", sceSifQueryMaxFreeMemSize()); /* 238 */
    printf("sceSifQueryTotalFreeMemSize() = 0x%x\n", sceSifQueryTotalFreeMemSize()); /* 239 */

    SndInit(def, buffer);                                                    /* 241 */
    printf("after SndInit\n");                                               /* 242 */
    printf("sceSifQueryMaxFreeMemSize() = 0x%x\n", sceSifQueryMaxFreeMemSize());  /* 243 */
    printf("sceSifQueryTotalFreeMemSize() = 0x%x\n", sceSifQueryTotalFreeMemSize()); /* 244 */

    return 1;                                                                /* 246 */
}                                                                            /* 247 */

/* --------------------------------------------------------------------------
 *  Frame pump
 * ------------------------------------------------------------------------ */

void WaitMainRpc(void)
{
    while (sceSifCheckStatRpc(&sif_cli_data.rpcd) != 0)
        ;
}

/* 255 */
void ee_iopMain(void)
{
    SndMain();                                                               /* 258 */

    /* The previous frame's call has to have landed before its reply is read
     * or the batch below overwrites the buffer it is still reading from. */
    WaitMainRpc();                                                           /* 259 */

    iop_ret = *(IOP_RET_STATUS *)iop_ret_buffer;                             /* 261, 263 */

    iopCommandRegister(IOP_COM_END, (char *)0, 0);                           /* 267 */

    /* The queue is written through the cache and read by the IOP over the
     * bus, so it has to be flushed before the call goes out. */
    FlushCache(0);                                                           /* 269 */

    sceSifCallRpc(&sif_cli_data, 0, 1,
                  iop_com_buffer, GetAlignUp(iop_com_offset, 4),
                  iop_ret_buffer, GetAlignUp(sizeof(IOP_RET_STATUS), 4),
                  (sceSifEndFunc)0, (void *)0);                              /* 271 */

    iopCommandFrameInit();                                                   /* 278 */
}

/* --------------------------------------------------------------------------
 *  Command queue
 * ------------------------------------------------------------------------ */

void iopCommandFrameInit(void)
{
    iop_com_offset = 0;                                                      /* 284 */
}

/* 290 */
int iopCommandRegister(IOP_COMMAND_ENUM command, char *buf, int size2)
{
    /* The IOP reads the queue as words, so a payload that is not a multiple
     * of four would misalign everything appended after it. */
    if ((size2 & 3) != 0)                                                    /* 298 */
        PRINT_ASSERT("EEIOP COMMON Struct Is Not 4Byte Align!!");            /* 299 */

    if (iop_com_offset + size2 + 4 >= (int)sizeof(iop_com_buffer))           /* 302 */
    {
        printf("iop_command_buffer over\n");
        return 0;
    }

    IOP_COMMAND_ENUM *cp = (IOP_COMMAND_ENUM *) (iop_com_buffer + iop_com_offset);
    int size = iop_com_offset + 4;
    iop_com_offset += 4;
    *cp             = command;

    if (size2 != 0)
    {
        memcpy(iop_com_buffer + size, buf, size2);
        iop_com_offset += size2;
    }

    return 1;
}

void SetCDReadMode(int mode)
{
    CD_READ_MODE_CHANGE crm;

    crm.mode = mode;
    iopCommandRegister(REQ_CD_READ_MODE_CHANGE, (char *)&crm, sizeof(crm));
}

/* --------------------------------------------------------------------------
 *  IOP status block
 *
 *  One load each -- the work is all in ee_iopMain()'s copy above.  Until the
 *  SIF transport is real that copy brings back zeros, so CheckEndPointThrough()
 *  always answers "not finished" and every stream reads ST_STREAM_NO_USE.
 * ------------------------------------------------------------------------ */

int CheckEndPointThrough(int core, int voice_no)
{
    return iop_ret.voice_end[core] & (1 << voice_no);
}

IOP_STREAM_RET *GetStreamWrkRet(int wrk_id)
{
    return &iop_ret.stream_ret[wrk_id];
}

IOP_STREAM_RET *GetPCMStreamWrkRet(int wrk_id)
{
    return &iop_ret.pcm_stream_ret[wrk_id];
}

int GetVoiceNowAdrs(int core, int voice)
{
    return iop_ret.mpNowAdrs[core][voice];
}

int GetVoiceLoopAdrs(int core, int voice)
{
    return iop_ret.mpLoopAdrs[core][voice];
}
