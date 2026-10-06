/* ==========================================================================
 *  system/eeiop/snd_util.h
 *
 *  Fire-and-forget sound-bank playback (snd_util.c).  snd_utilAutoBDPlay()
 *  takes a BD/HXD pair, loads them into a fresh bank and plays sample 0 as
 *  soon as the load lands, then releases the bank once the voice stops --
 *  all without the caller holding anything.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND_UTIL_H
#define _SYSTEM_EEIOP_SND_UTIL_H

#include "../../sdk/libvu0.h"       /* sceVu0FVECTOR */
#include "snd3d.h"                  /* SND_3D_SET */

typedef enum _AUTO_BD_ERR
{
    AUTO_BD_ERR_OK     = 0,
    AUTO_BD_ERR_NO_WRK = 1
} AUTO_BD_ERR;

typedef enum _AUTO_BD_MODE
{
    AUTO_BD_MODE_NO_USE       = 0,
    AUTO_BD_MODE_MEM_WAIT     = 1,  /* declared, never reached */
    AUTO_BD_MODE_LOAD_WAIT    = 2,
    AUTO_BD_MODE_PLAYEND_WAIT = 3
} AUTO_BD_MODE;

/* The 3D vectors are copied in rather than referenced, because the caller is
 * not expected to keep anything alive. */
typedef struct _AUTO_BD_WRK         /* 0x60 */
{
    /* 0x00 */ int   mode;
    /* 0x04 */ int   file_no;
    /* 0x08 */ int   header_file_no;
    /* 0x0c */ int   bank_no;
    /* 0x10 */ int   play_id;
    /* 0x14 */ short pitch;
    /* 0x16 */ short vol;
    /* 0x18 */ char  padding0[8];    /* pos is quadword aligned on the EE */
    /* 0x20 */ sceVu0FVECTOR pos;
    /* 0x30 */ sceVu0FVECTOR vel;
    /* 0x40 */ sceVu0FVECTOR dir;
    /* 0x50 */ int   in_time;
    /* 0x54:0 */ unsigned int s3d     : 1;
    /* 0x54:1 */ unsigned int effect  : 1;
    /* 0x54:2 */ unsigned int loop    : 1;
    /* 0x54:3 */ unsigned int use     : 1;
    /* 0x54:4 */ unsigned int start   : 1;
    /* 0x54:5 */ unsigned int playing : 1;
    /* 0x54:6 */ unsigned int end     : 1;
    /* 0x54:7 */ unsigned int resume  : 1;
    /* 0x55:0 */ unsigned int pause   : 1;
    /* 0x58 */ char padding1[8];      /* quadword tail, from the pos/vel/dir */
} AUTO_BD_WRK;

int   snd_utilGetOneWrkSize(void);
void *snd_utilAutoBDInit(void *buffer, int num);
void  snd_utilAutoBDMain(void);

AUTO_BD_ERR snd_utilAutoBDPlay(int file_no, int header_file_no, int effect, int loop,
                               int vol, int pitch, int in_time, SND_3D_SET *s3s);

/* Queues `bank_id` for release once `snd_buf_id` has finished playing.
 * AUTO_BD_ERR_OK means the release was taken on, so the caller may forget
 * the bank id. */
AUTO_BD_ERR snd_utilAutoRelease(int snd_buf_id, int bank_id);

int   GetVacantAutoBDWrk(void);

#endif /* _SYSTEM_EEIOP_SND_UTIL_H */
