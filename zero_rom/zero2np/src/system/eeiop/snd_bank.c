/* ==========================================================================
 *  system/eeiop/snd_bank.c
 *
 *  Sound banks.  SndBankInitAll() carves the one work buffer into three
 *  parallel arrays of `num` entries each -- SND_BANK, then the BD file table,
 *  then the HXD file table -- which is why one bank costs 0x34 bytes.
 *
 *  The file tables are the interesting part: SndBankFileSearch() means two
 *  banks naming the same file share one SND_BANK_FILE and one load, and the
 *  reference count decides whether a release actually frees anything.  A
 *  release that lands while the load is still in flight cancels it instead,
 *  handing the loader an interrupt-time callback that frees the buffer once
 *  the cancel takes.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sndbank.h"

#include "cddat.h"                  /* GetFileSize, GetFileName */
#include "ee_iop.h"                 /* ee_iopMalloc / ee_iopFree */
#include "fileload.h"
#include "hxd.h"
#include "snd3d.h"
#include "snd_buffer.h"
#include "spu_mem.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT, GetAlignUp */

static int            snd_bank_max;                                          /* sbss 3f503c */
static SND_BANK      *snd_bank;                                              /* sbss 3f5040 */
static SND_BANK_FILE *snd_bd_file;                                           /* sbss 3f5044 */
static SND_BANK_FILE *snd_hxd_file;                                          /* sbss 3f5048 */
static int            snd_bank_load_priority;                                /* sbss 3f504c */

static void           intrSndBankFileBD(void *buffer, void *arg);
static void           intrSndBankFileHXD(void *buffer, void *arg);
static int            SndBankFileReqBD(SND_BANK *p_Sb, int file_no);
static int            SndBankFileReqHXD(SND_BANK *p_Sb, int file_no);
static void           ReleaseSndBDFile(SND_BANK_FILE *pSndBDFile);
static void           ReleaseSndHXDFile(SND_BANK_FILE *pSndHXDFile);
static HXD_HEADER    *snd_bankGet_pHxdHeader(SND_BANK *p_Sb);
static SOUND_INFO    *snd_bankGet_pSOUND_INFO(SND_BANK *p_Sb);
static int            snd_bankIsReady(SND_BANK *p_Sb);

void SndBankSetLoadPriority(int iNewPriority)
{
    snd_bank_load_priority = iNewPriority;                                   /* 72 */
}

int SndBankGetLoadPriority(void)
{
    return snd_bank_load_priority;                                           /* 76 */
}

/* SND_BANK + one BD file record + one HXD file record. */
int SndBankGetOneWrkSize(void)
{
    return sizeof(SND_BANK) + sizeof(SND_BANK_FILE) * 2;                      /* 81 */
}

/* 87 */
void *SndBankInitAll(void *wrk_buffer, int num, int load_priority)
{
    int i;

    snd_bank               = (SND_BANK *)wrk_buffer;                         /* 88 */
    snd_bank_max           = num;                                            /* 87 */
    snd_bank_load_priority = load_priority;                                  /* 88 */

    for (i = 0; i < num; i++)                                                /* 91 */
    {
        snd_bank[i].use         = 0;                                         /* 92 */
        snd_bank[i].pSndBDFile  = (SND_BANK_FILE *)0;                        /* 93 */
        snd_bank[i].pSndHXDFile = (SND_BANK_FILE *)0;                        /* 94 */
        snd_bank[i].id          = i;                                         /* 95 */
    }

    snd_bd_file = (SND_BANK_FILE *)&snd_bank[num];                           /* 99 */

    for (i = 0; i < num; i++)                                                /* 100 */
    {
        snd_bd_file[i].m_pAdrs  = (void *)0;                                 /* 101 */
        snd_bd_file[i].m_FileNo = -1;                                        /* 102 */
        snd_bd_file[i].m_Size   = 0;                                         /* 103 */
        snd_bd_file[i].m_RefCnt = 0;                                         /* 104 */
        snd_bd_file[i].m_Ready  = 0;                                         /* 105 */
    }

    snd_hxd_file = &snd_bd_file[num];                                        /* 109 */

    for (i = 0; i < num; i++)                                                /* 110 */
    {
        snd_hxd_file[i].m_pAdrs  = (void *)0;                                /* 111 */
        snd_hxd_file[i].m_FileNo = -1;                                       /* 112 */
        snd_hxd_file[i].m_Size   = 0;                                        /* 113 */
        snd_hxd_file[i].m_RefCnt = 0;                                        /* 114 */
        snd_hxd_file[i].m_Ready  = 0;                                        /* 115 */
    }

    return &snd_hxd_file[num];                                               /* 118 */
}

int SndBankGetFreeBankNum(void)
{
    int i;
    int iCnt = 0;

    for (i = 0; i < snd_bank_max; i++)                                       /* 130 */
    {
        if (snd_bank[i].use == 0)                                            /* 132 */
            iCnt++;
    }

    return iCnt;                                                             /* 136 */
}

static int GetVacantSndBank(void)
{
    int i;

    for (i = 0; i < snd_bank_max; i++)                                       /* 145 */
    {
        if (snd_bank[i].use == 0)                                            /* 147 */
            return i;
    }

    return -1;                                                               /* 151 */
}                                                                            /* 152 */

/* 156 */
int SndBankNew(int file_no, int header_file_no, int size)
{
    int       bank_no;
    SND_BANK *p_Sb;

    bank_no = GetVacantSndBank();                                            /* 156 */

    if (file_no < 0)                                                         /* 160 */
        PRINT_ASSERT("FileNo Is Under Zero");
    if (header_file_no < 0)                                                  /* 161 */
        PRINT_ASSERT("FileNo Is Under Zero");

    if (bank_no == -1)                                                       /* 164 */
    {
        PRINT_ASSERT("SndBankNew() There is No Free SndBank Wrk");           /* 165 */
        return -1;                                                           /* 166 */
    }

    p_Sb = &snd_bank[bank_no];                                               /* 169 */

    if (SndBankFileReqBD(p_Sb, file_no) == 0)                                /* 171 */
        return -1;                                                           /* 172 */

    /* The header failing after the body succeeded has to undo the body, or
     * the SPU memory leaks with no bank left holding it. */
    if (SndBankFileReqHXD(p_Sb, header_file_no) == 0)                        /* 174 */
    {
        ReleaseSndBDFile(p_Sb->pSndBDFile);                                  /* 175 */
        return -1;                                                           /* 176 */
    }

    p_Sb->s3d = (void *)0;                                                   /* 179 */
    p_Sb->use = 1;                                                           /* 180 */

    return bank_no;                                                          /* 182 */
}                                                                            /* 183 */

/* 188 */
int SndBankIsReady(int bank_no)
{
    if (bank_no == -1)                                                       /* 189 */
        return 1;

    if (bank_no < -1 || snd_bank_max <= bank_no)                             /* 194 */
        PRINT_ASSERT("SndBankIsReady bank_no is Illegal");                   /* 195 */

    return snd_bankIsReady(&snd_bank[bank_no]);                              /* 196 */
}                                                                            /* 199 */

/* 205 */
void SndBankPrintStatus(void)
{
    int i;

    printf("_____________________\n");                                       /* 207 */
    printf("\n");                                                            /* 208 */
    printf("<<Now SndBank Status>>\n");                                      /* 209 */

    for (i = 0; i < snd_bank_max; i++)                                       /* 210 */
    {
        if (snd_bank[i].use)                                                 /* 211 */
            printf("GET_FILE_NAME(snd_bank[i].pSndBDFile->m_FileNo) = %s\n",
                   GetFileName(snd_bank[i].pSndBDFile->m_FileNo));           /* 212 */
    }

    printf("_____________________\n");                                       /* 215 */
}

/* 221 */
int SndBankGetInfo(int bank_no, int *num, SOUND_INFO **info)
{
    SND_BANK *p_Sb;

    if (bank_no == -1)                                                       /* 224 */
    {
        /* NOTE: the ROM range-checks `snd_bank_max` here, not `bank_no` --
         * the guard above has already established bank_no == -1, so this
         * assert can only fire if the module was never initialised. */
        if (snd_bank_max < 0)                                                /* 226 */
            PRINT_ASSERT("SndBankGetInfo bank_no is Illegal");               /* 227 */

        return SND_BANK_ILLEGAL_SOUND_NO;                                    /* 231 */
    }

    p_Sb = &snd_bank[bank_no];                                               /* 233 */

    if (snd_bankIsReady(p_Sb) == 0)                                          /* 237 */
        return SND_BANK_NOT_READY;                                           /* 238 */

    *info = snd_bankGet_pSOUND_INFO(p_Sb);                                   /* 240 */
    *num  = snd_bankGet_pHxdHeader(p_Sb)->num;                               /* 241 */

    return SND_BANK_OK;                                                      /* 243 */
}                                                                            /* 244 */

/* --------------------------------------------------------------------------
 *  File release
 * ------------------------------------------------------------------------ */

/* Load-cancel completions.  These run at interrupt time from fileload.c once
 * the cancelled request has actually been withdrawn, which is the only point
 * at which the buffer is safe to give back. */
/* 247 */
static void intr_LoadCancelHXDFunc(void *buffer, void *dummy)
{
    printf("(int)buffer = 0x%x\n", buffer);                                  /* 249 */
    ee_iopFree(buffer);                                                      /* 250 */
}

static void intr_LoadCancelBDFunc(void *buffer, void *dummy)
{
    ReleaseSPUMemory(buffer);                                                /* 256 */
}

/* 261 */
static void ReleaseSndHXDFile(SND_BANK_FILE *pSndHXDFile)
{
    if (pSndHXDFile->m_RefCnt >= 2)                                          /* 263 */
    {
        pSndHXDFile->m_RefCnt--;                                             /* 280 */
        return;
    }

    if (pSndHXDFile->m_pAdrs == (void *)0)                                   /* 265 */
        PRINT_ASSERT("ReleaseSndHXDFile Adrs Is Null");                      /* 266 */

    if (pSndHXDFile->m_Ready == 0)                                           /* 270 */
    {
        FileLoadCancel2(pSndHXDFile->m_FileNo, pSndHXDFile->m_pAdrs,
                        intr_LoadCancelHXDFunc, (void *)0);                  /* 271 */
    }
    else                                                                     /* 272 */
    {
        printf("load end Free %x\n");                                        /* 273 */
        ee_iopFree(pSndHXDFile->m_pAdrs);                                    /* 274 */
    }

    pSndHXDFile->m_pAdrs  = (void *)0;                                       /* 278 */
    pSndHXDFile->m_RefCnt = 0;                                               /* 279 */
    pSndHXDFile->m_FileNo = -1;                                              /* 281 */
}

/* 286 */
static void ReleaseSndBDFile(SND_BANK_FILE *pSndBDFile)
{
    if (pSndBDFile->m_RefCnt >= 2)                                           /* 288 */
    {
        pSndBDFile->m_RefCnt--;                                              /* 304 */
        return;
    }

    if (pSndBDFile->m_pAdrs == (void *)0)                                    /* 290 */
        PRINT_ASSERT("ReleaseSndBDFile Adrs Is Null");                       /* 291 */

    if (pSndBDFile->m_Ready == 0)                                            /* 295 */
    {
        FileLoadCancelSPU2(pSndBDFile->m_FileNo, pSndBDFile->m_pAdrs,
                           intr_LoadCancelBDFunc, (void *)0);                /* 296 */
    }
    else                                                                     /* 297 */
    {
        ReleaseSPUMemory(pSndBDFile->m_pAdrs);                               /* 298 */
    }

    pSndBDFile->m_pAdrs  = (void *)0;                                        /* 302 */
    pSndBDFile->m_RefCnt = 0;                                                /* 303 */
    pSndBDFile->m_FileNo = -1;                                               /* 305 */
}

/* 310 */
int SndBankRelease(int bank_no)
{
    SND_BANK *p_Sb;

    if (bank_no == -1)                                                       /* 313 */
        return SND_BANK_OK;

    if (bank_no < -1 || snd_bank_max <= bank_no)                             /* 318 */
        PRINT_ASSERT("SndBankRelease bank_no is Illegal");                   /* 319 */

    p_Sb = &snd_bank[bank_no];                                               /* 322 */

    if (p_Sb->pSndHXDFile != (SND_BANK_FILE *)0)                             /* 326 */
    {
        ReleaseSndHXDFile(p_Sb->pSndHXDFile);                                /* 327 */
        p_Sb->pSndHXDFile = (SND_BANK_FILE *)0;                              /* 328 */
    }

    if (p_Sb->pSndBDFile != (SND_BANK_FILE *)0)                              /* 331 */
    {
        ReleaseSndBDFile(p_Sb->pSndBDFile);                                  /* 332 */
        p_Sb->pSndBDFile = (SND_BANK_FILE *)0;                               /* 333 */
    }

    if (p_Sb->s3d != (void *)0)                                              /* 337 */
    {
        Snd3DFreeWrk(p_Sb->s3d);                                             /* 338 */
        p_Sb->s3d = (void *)0;                                               /* 339 */
    }

    p_Sb->use = 0;                                                           /* 343 */

    return SND_BANK_OK;                                                      /* 345 */
}                                                                            /* 346 */

/* 351 */
int SndBankSet3D(int bank_no, float *pos)
{
    SND_BANK  *p_Sb;
    SND_3D_SET s3s;

    if (bank_no == -1)                                                       /* 354 */
        return SND_BANK_OK;

    if (bank_no < -1 || snd_bank_max <= bank_no)                             /* 359 */
        PRINT_ASSERT("SndBankSet3D bank_no is Illegal");                     /* 360 */

    p_Sb = &snd_bank[bank_no];                                               /* 363 */

    if (snd_bankIsReady(p_Sb) == 0)                                          /* 367 */
        return SND_BANK_NOT_READY;                                           /* 368 */

    /* The bank's own handle is created lazily -- the first Set3D is what
     * turns the bank into a positional source. */
    if (p_Sb->s3d == (void *)0)                                              /* 371 */
    {
        memset(&s3s, 0, sizeof(SND_3D_SET));                                 /* 372 */
        s3s.pos = (sceVu0FVECTOR *)pos;                                      /* 374 */
        s3s.vel = (sceVu0FVECTOR *)0;                                        /* 375 */
        s3s.dir = (sceVu0FVECTOR *)0;                                        /* 376 */

        p_Sb->s3d = Snd3DCreateWrk(&s3s);                                    /* 378 */
    }                                                                        /* 379 */
    else
    {
        if (pos == (float *)0)                                               /* 381 */
            return SND_BANK_OK;

        Snd3DSetPosition(p_Sb->s3d, pos);                                    /* 382 */
    }

    return SND_BANK_OK;                                                      /* 385 */
}                                                                            /* 386 */

/* 389 */
int SndBankIsLoopSnd(int bank_no, int no)
{
    SND_BANK *p_Sb;

    if (bank_no == -1)                                                       /* 392 */
        return 0;

    if (bank_no < -1 || snd_bank_max <= bank_no)                             /* 397 */
        PRINT_ASSERT("SndBankIsLoopSnd bank_no is Illegal");                 /* 398 */

    p_Sb = &snd_bank[bank_no];                                               /* 401 */

    if (snd_bankIsReady(p_Sb) == 0)                                          /* 405 */
        return 0;

    if (no >= snd_bankGet_pHxdHeader(p_Sb)->num)                             /* 409 */
    {
        PRINT_ASSERT("SndBankIsLoopSnd no %d is illegal in file %d",
                     no, p_Sb->pSndBDFile->m_FileNo);                        /* 410 */
        return 0;                                                            /* 411 */
    }

    /* ROM BUG, reproduced: `no` is bounds-checked but never used as an index,
     * so this always answers for sample 0.  The disassembly loads attr from
     * offset 0x10 of the array base with no scaling. */
    return snd_bankGet_pSOUND_INFO(p_Sb)->attr.loop;                         /* 415 */
}                                                                            /* 416 */

/* 421 */
int SndBankPlay(int bank_no, int no, int effect, int loop,
                int vol, int pitch, int fade_time, SND_3D_SET *s3s)
{
    SND_BANK     *p_Sb;
    SOUND_INFO   *info;
    unsigned int  top_adrs;
    int           core;
    int           Ret;
    void         *s3d;
    int           s3d_free;

    s3d_free = 0;                                                            /* 422 */
    s3d      = (void *)0;                                                    /* 427 */

    if (bank_no == -1)                                                       /* 430 */
        return CSND_BUF_PLAY_NO_ID;

    if (bank_no < -1 || snd_bank_max <= bank_no)                             /* 435 */
        PRINT_ASSERT("SndBankPlay bank_no is Illegal");                      /* 436 */

    p_Sb = &snd_bank[bank_no];                                               /* 439 */

    if (snd_bankIsReady(p_Sb) == 0)                                          /* 443 */
        return CSND_BUF_PLAY_NO_ID;

    if (no >= snd_bankGet_pHxdHeader(p_Sb)->num)                             /* 448 */
    {
        PRINT_ASSERT("SndBankPlay no %d is illegal in file %d",
                     no, p_Sb->pSndBDFile->m_FileNo);                        /* 449 */
        return CSND_BUF_PLAY_NO_ID;                                          /* 450 */
    }

    info = snd_bankGet_pSOUND_INFO(p_Sb);                                    /* 455 */

    if (info[no].attr.s3d)                                                   /* 459 */
    {
        /* A caller-supplied set gets its own handle, which snd_buffer.c then
         * owns and frees; without one the bank's shared handle is borrowed. */
        if (s3s != (SND_3D_SET *)0)                                          /* 460 */
        {
            s3d      = Snd3DCreateWrk(s3s);                                  /* 461 */
            s3d_free = 1;
        }                                                                    /* 463 */
        else
        {
            s3d      = p_Sb->s3d;                                            /* 464 */
            s3d_free = 0;                                                    /* 465 */
        }
    }

    /* Effect samples live on core 0, everything else on core 1. */
    core     = (info[no].attr.effect == 0);                                  /* 470 */
    top_adrs = (unsigned int)(uintptr_t)p_Sb->pSndBDFile->m_pAdrs +
               info[no].offset;                                              /* 479 */

    Ret = SndBufPlay(top_adrs, core, effect, vol, info[no].vol,
                     pitch, info[no].pitch, info[no].pan, fade_time,
                     info[no].attr.loop, info[no].attr.type, s3d, s3d_free,
                     info[no].adsr1, info[no].adsr2,
                     top_adrs + info[no].loopstart,
                     top_adrs + info[no].loopend);                           /* 483, 490 */

    /* Its own core was full, so fall back to the other one -- which cannot
     * carry the reverb send, hence effect 0. */
    if (Ret == CSND_BUF_PLAY_NO_ID)                                          /* 495 */
    {
        Ret = SndBufPlay(top_adrs, core ^ 1, 0, vol, info[no].vol,
                         pitch, info[no].pitch, info[no].pan, fade_time,
                         info[no].attr.loop, info[no].attr.type, s3d, s3d_free,
                         info[no].adsr1, info[no].adsr2,
                         top_adrs + info[no].loopstart,
                         top_adrs + info[no].loopend);                       /* 496 */
    }

    return Ret;                                                              /* 502 */
}                                                                            /* 503 */

/* 506 */
SND_BANK_STATUS SndBankGetFileNo(int bank_no, int *file_no)
{
    SND_BANK *p_Sb;

    if (bank_no == -1)                                                       /* 509 */
    {
        printf("bank_no is Illegal\n");                                      /* 510 */
        return SND_BANK_OK;                                                  /* 511 */
    }

    if (bank_no < -1 || snd_bank_max <= bank_no)                             /* 514 */
        PRINT_ASSERT("bank_no is Illegal");                                  /* 515 */

    p_Sb = &snd_bank[bank_no];                                               /* 518 */

    if (snd_bankIsReady(p_Sb) == 0)                                          /* 522 */
        return SND_BANK_NOT_READY;                                           /* 523 */

    *file_no = p_Sb->pSndBDFile->m_FileNo;                                   /* 525 */

    return SND_BANK_OK;                                                      /* 527 */
}                                                                            /* 528 */

/* --------------------------------------------------------------------------
 *  File table
 * ------------------------------------------------------------------------ */

static SND_BANK_FILE *SndBankFileSearch(SND_BANK_FILE *a_snd_file, int FileNo)
{
    int i;

    for (i = 0; i < snd_bank_max; i++)                                       /* 544 */
    {
        if (a_snd_file[i].m_RefCnt != 0 && a_snd_file[i].m_FileNo == FileNo) /* 545 */
            return &a_snd_file[i];                                           /* 550 */
    }

    return (SND_BANK_FILE *)0;                                               /* 551 */
}

/* 555 */
static SND_BANK_FILE *SndBankFileGet(SND_BANK_FILE *a_snd_file)
{
    int i;

    for (i = 0; i < snd_bank_max; i++)                                       /* 557 */
    {
        if (a_snd_file[i].m_RefCnt == 0)                                     /* 558 */
            return &a_snd_file[i];
    }

    PRINT_ASSERT("SndBankFileBDGet Illegal");                                /* 562 */

    return (SND_BANK_FILE *)0;                                               /* 564 */
}

/* 567 */
static int SndBankFileReqBD(SND_BANK *p_Sb, int file_no)
{
    unsigned int   i_FileSize;
    SND_BANK_FILE *file;

    /* Already resident (or already loading): just take another reference. */
    file = SndBankFileSearch(snd_bd_file, file_no);                          /* 569 */
    p_Sb->pSndBDFile = file;

    if (file != (SND_BANK_FILE *)0)                                          /* 570 */
    {
        file->m_RefCnt++;                                                    /* 571 */
        return 1;                                                            /* 572 */
    }

    file = SndBankFileGet(snd_bd_file);                                      /* 574 */
    p_Sb->pSndBDFile = file;

    i_FileSize   = GetAlignUp(GetFileSize(file_no), 6);                      /* 579 */
    file->m_Size = i_FileSize;                                               /* 581 */

    file->m_pAdrs = GetSPUMemory(i_FileSize);                                /* 582 */
    if (file->m_pAdrs == (void *)0)                                          /* 583 */
    {
        printf("SndBankFileReqBD() SPU No Memory\n");                        /* 584 */
        return 0;                                                            /* 585 */
    }

    file->m_FileNo = file_no;                                                /* 589 */
    file->m_Ready  = 0;                                                      /* 590 */

    FileLoadReqSPU(file_no, file->m_pAdrs, snd_bank_load_priority,
                   intrSndBankFileBD, file);                                 /* 591 */

    file->m_RefCnt = 1;                                                      /* 594 */

    return 1;                                                                /* 597 */
}                                                                            /* 598 */

/* 601 */
static int SndBankFileReqHXD(SND_BANK *p_Sb, int file_no)
{
    unsigned int   i_FileSize;
    SND_BANK_FILE *file;

    file = SndBankFileSearch(snd_hxd_file, file_no);                         /* 603 */
    p_Sb->pSndHXDFile = file;

    if (file != (SND_BANK_FILE *)0)                                          /* 604 */
    {
        file->m_RefCnt++;                                                    /* 605 */
        return 1;                                                            /* 606 */
    }

    file = SndBankFileGet(snd_hxd_file);                                     /* 608 */
    p_Sb->pSndHXDFile = file;

    i_FileSize   = GetAlignUp(GetFileSize(file_no), 6);                      /* 613 */
    file->m_Size = i_FileSize;                                               /* 615 */

    file->m_pAdrs = ee_iopMalloc(i_FileSize);                                /* 616 */
    if (file->m_pAdrs == (void *)0)                                          /* 617 */
    {
        printf("SndBankFileHXDNew() EE No Memory\n");                        /* 618 */
        return 0;                                                            /* 619 */
    }

    file->m_FileNo = file_no;                                                /* 623 */
    file->m_Ready  = 0;                                                      /* 624 */

    FileLoadReqEE(file_no, file->m_pAdrs, snd_bank_load_priority,
                  intrSndBankFileHXD, file);                                 /* 625 */

    file->m_RefCnt = 1;                                                      /* 628 */

    return 1;                                                                /* 631 */
}                                                                            /* 632 */

/* Load completions, again at interrupt time. */
static void intrSndBankFileBD(void *buffer, void *arg)
{
    ((SND_BANK_FILE *)arg)->m_Ready = 1;                                     /* 639 */
}                                                                            /* 640 */

/* 644 */
static void intrSndBankFileHXD(void *buffer, void *arg)
{
    CheckHXDData((HXD_HEADER *)buffer, 1);                                   /* 647 */
    ((SND_BANK_FILE *)arg)->m_Ready = 1;                                     /* 649 */
}                                                                            /* 650 */

static HXD_HEADER *snd_bankGet_pHxdHeader(SND_BANK *p_Sb)
{
    return (HXD_HEADER *)p_Sb->pSndHXDFile->m_pAdrs;                         /* 654 */
}

/* The SOUND_INFO array follows the header. */
static SOUND_INFO *snd_bankGet_pSOUND_INFO(SND_BANK *p_Sb)
{
    return (SOUND_INFO *)((char *)p_Sb->pSndHXDFile->m_pAdrs +
                          sizeof(HXD_HEADER));                               /* 658 */
}

/* 662 */
static int snd_bankIsReady(SND_BANK *p_Sb)
{
    if (p_Sb->pSndHXDFile != (SND_BANK_FILE *)0 &&
        p_Sb->pSndHXDFile->m_Ready != 0 &&
        p_Sb->pSndBDFile != (SND_BANK_FILE *)0 &&
        p_Sb->pSndBDFile->m_Ready != 0)                                      /* 667 */
        return 1;

    return 0;
}                                                                            /* 673 */
