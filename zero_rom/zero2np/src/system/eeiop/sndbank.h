/* ==========================================================================
 *  system/eeiop/sndbank.h
 *
 *  Sound-bank control (snd_bank.c).  A bank is a pair of files -- the BD body
 *  (ADPCM, loaded straight into SPU RAM) and the HXD header (loaded into EE
 *  RAM) -- plus an optional 3D handle.  SndBankNew() takes both files and
 *  hands back a bank number; SndBankPlay() picks one SOUND_INFO record out of
 *  the header and starts a voice on it.
 *
 *  The two file tables are reference-counted and shared: two banks asking for
 *  the same BD file get the same SND_BANK_FILE, so the sample data is loaded
 *  once.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SNDBANK_H
#define _SYSTEM_EEIOP_SNDBANK_H

#include "snd3d.h"                  /* SND_3D_SET */
#include "snd_def.h"                /* SOUND_INFO */

/* Recovered from the stabs entry for _SND_BANK_STATUS in symbols.txt. */
typedef enum _SND_BANK_STATUS
{
    SND_BANK_NOT_USE           = 0,
    SND_BANK_NOT_READY         = 1,
    SND_BANK_USE               = 2,
    SND_BANK_ILLEGAL_SOUND_NO  = 3,
    SND_BANK_OK                = 4
} SND_BANK_STATUS;

/* One loaded file, shared between every bank that asked for it.  `m_Ready` is
 * raised from the loader's completion interrupt. */
typedef struct _SND_BANK_FILE       /* 0x10 */
{
    /* 0x0 */ short m_Ready;
    /* 0x2 */ short m_RefCnt;
    /* 0x4 */ int   m_FileNo;
    /* 0x8 */ void *m_pAdrs;        /* SPU address for BD, EE pointer for HXD */
    /* 0xc */ int   m_Size;
} SND_BANK_FILE;

/* PORT NOTE: three pointers, so everything from 0x04 on drifts on the host. */
typedef struct _SND_BANK            /* 0x14 */
{
    /* 0x00:0 */ unsigned int   use : 1;
    /* 0x04 */   void          *s3d;
    /* 0x08 */   SND_BANK_FILE *pSndBDFile;
    /* 0x0c */   SND_BANK_FILE *pSndHXDFile;
    /* 0x10 */   int            id;
} SND_BANK;

void  SndBankSetLoadPriority(int iNewPriority);
int   SndBankGetLoadPriority(void);
int   SndBankGetOneWrkSize(void);
void *SndBankInitAll(void *wrk_buffer, int num, int load_priority);
int   SndBankGetFreeBankNum(void);

/* Returns the bank number, or -1 if no slot was free or either load failed. */
int   SndBankNew(int file_no, int header_file_no, int size);
int   SndBankRelease(int bank_no);
int   SndBankIsReady(int bank_no);

SND_BANK_STATUS SndBankGetFileNo(int bank_no, int *file_no);
int   SndBankGetInfo(int bank_no, int *num, SOUND_INFO **info);
int   SndBankIsLoopSnd(int bank_no, int no);
int   SndBankSet3D(int bank_no, float *pos);

/* Starts sample `no` from the bank.  Returns a snd_buffer play id, or
 * CSND_BUF_PLAY_NO_ID.  `loop` is declared but unused -- the loop flag comes
 * from the sample's own SOUND_INFO. */
int   SndBankPlay(int bank_no, int no, int effect, int loop,
                  int vol, int pitch, int fade_time, SND_3D_SET *s3s);

void  SndBankPrintStatus(void);

#endif /* _SYSTEM_EEIOP_SNDBANK_H */
