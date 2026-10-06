/* ==========================================================================
 *  system/eeiop/snd_util.c
 *
 *  Fire-and-forget bank playback.  Each AUTO_BD_WRK is a tiny state machine:
 *  LOAD_WAIT while snd_bank.c pulls the two files in, PLAYEND_WAIT while the
 *  voice runs, then back to NO_USE with the bank released.  Because nothing
 *  outside holds a handle, everything the play needs -- the 3D position,
 *  volume, pitch, fade -- is copied into the slot at request time.
 *
 *  snd_utilAutoRelease() reuses the same slots for the other direction: a
 *  caller that started its own voice can hand the bank over here and let the
 *  PLAYEND_WAIT arm free it.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdio.h>
#include <string.h>

#include "snd_util.h"

#include "snd3d.h"
#include "snd_buffer.h"
#include "sndbank.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT, PRINT_WARNING */
#include "../../sdk/libvu0.h"

static AUTO_BD_WRK *auto_bd_wrk;                                             /* sbss 3f5098 */
static int          auto_bd_wrk_max;                                         /* sbss 3f509c */

/* Also inlined at both call sites below, which is why their line spans start
 * back here at 46. */
int GetVacantAutoBDWrk(void)
{
    int i;

    for (i = 0; i < auto_bd_wrk_max; i++)                                    /* 46 */
    {
        if (auto_bd_wrk[i].mode == AUTO_BD_MODE_NO_USE)                      /* 47 */
            return i;                                                        /* 48 */
    }

    return -1;                                                               /* 51 */
}                                                                            /* 52 */

int snd_utilGetOneWrkSize(void)
{
    return sizeof(AUTO_BD_WRK);                                              /* 56 */
}

/* 65 */
void *snd_utilAutoBDInit(void *buffer, int num)
{
    int   i;
    void *ret;

    auto_bd_wrk     = (AUTO_BD_WRK *)buffer;
    auto_bd_wrk_max = num;

    ret = (char *)buffer + snd_utilGetOneWrkSize() * num;                    /* 67 */

    for (i = 0; i < auto_bd_wrk_max; i++)                                    /* 69 */
        auto_bd_wrk[i].mode = AUTO_BD_MODE_NO_USE;                           /* 70 */

    return ret;                                                              /* 73 */
}

/* 79 */
AUTO_BD_ERR snd_utilAutoRelease(int snd_buf_id, int bank_id)
{
    int wrk_no;

    wrk_no = GetVacantAutoBDWrk();

    if (wrk_no < 0)                                                          /* 83 */
        return AUTO_BD_ERR_NO_WRK;

    /* Straight into PLAYEND_WAIT -- the bank is already loaded and the voice
     * already running, so only the teardown arm is wanted. */
    auto_bd_wrk[wrk_no].bank_no = bank_id;                                   /* 87 */
    auto_bd_wrk[wrk_no].mode    = AUTO_BD_MODE_PLAYEND_WAIT;                 /* 89 */
    auto_bd_wrk[wrk_no].play_id = snd_buf_id;                                /* 91 */

    return AUTO_BD_ERR_OK;                                                   /* 92 */
}

/* 98 */
AUTO_BD_ERR snd_utilAutoBDPlay(int file_no, int header_file_no, int effect, int loop,
                               int vol, int pitch, int in_time, SND_3D_SET *s3s)
{
    int wrk_no;

    wrk_no = GetVacantAutoBDWrk();

    if (wrk_no < 0)                                                          /* 102 */
        return AUTO_BD_ERR_NO_WRK;                                           /* 103 */

    auto_bd_wrk[wrk_no].effect  = effect;                                    /* 107 */
    auto_bd_wrk[wrk_no].loop    = loop;                                      /* 108 */
    auto_bd_wrk[wrk_no].vol     = vol;                                       /* 109 */
    auto_bd_wrk[wrk_no].pitch   = pitch;                                     /* 110 */
    auto_bd_wrk[wrk_no].in_time = in_time;

    if (s3s != (SND_3D_SET *)0)                                              /* 112 */
    {
        auto_bd_wrk[wrk_no].s3d = 1;                                         /* 113 */

        sceVu0CopyVector(auto_bd_wrk[wrk_no].pos, *s3s->pos);                /* 114 */

        if (s3s->vel != (sceVu0FVECTOR *)0)                                  /* 115 */
            sceVu0CopyVector(auto_bd_wrk[wrk_no].vel, *s3s->vel);            /* 116 */

        if (s3s->dir != (sceVu0FVECTOR *)0)                                  /* 117 */
            sceVu0CopyVector(auto_bd_wrk[wrk_no].dir, *s3s->dir);            /* 118 */
    }                                                                        /* 119 */
    else
    {
        auto_bd_wrk[wrk_no].s3d = 0;                                         /* 120 */
    }

    auto_bd_wrk[wrk_no].bank_no = SndBankNew(file_no, header_file_no, -1);   /* 122 */
    auto_bd_wrk[wrk_no].mode    = AUTO_BD_MODE_LOAD_WAIT;                    /* 125 */

    return AUTO_BD_ERR_OK;                                                   /* 127 */
}                                                                            /* 128 */

/* 132 */
void snd_utilAutoBDMain(void)
{
    int         i;
    SND_3D_SET *ptr;
    SND_3D_SET  set;
    SOUND_INFO *info;
    int         num;

    for (i = 0; i < auto_bd_wrk_max; i++)                                    /* 135 */
    {
        switch (auto_bd_wrk[i].mode)                                         /* 136 */
        {
        case AUTO_BD_MODE_LOAD_WAIT:
            if (SndBankIsReady(auto_bd_wrk[i].bank_no))                      /* 140 */
            {
                /* One bank per auto play: sample 0 is the only one that ever
                 * gets started, so a multi-sample BD is a data error. */
                SndBankGetInfo(auto_bd_wrk[i].bank_no, &num, &info);         /* 146 */
                if (num > 1)                                                 /* 147 */
                    PRINT_WARNING("snd_utilAutoBDMain() BD Num is %d over 1", num); /* 148 */

                ptr = (SND_3D_SET *)0;                                       /* 152 */
                if (auto_bd_wrk[i].s3d)                                      /* 153 */
                {
                    memset(&set, 0, sizeof(SND_3D_SET));

                    set.pos = (sceVu0FVECTOR *)auto_bd_wrk[i].pos;           /* 156 */
                    set.vel = (sceVu0FVECTOR *)auto_bd_wrk[i].vel;           /* 157 */
                    set.dir = (sceVu0FVECTOR *)auto_bd_wrk[i].dir;           /* 158 */

                    ptr = &set;                                              /* 160 */
                }

                /* A looping sample would never reach PLAYEND_WAIT, so the
                 * slot -- and the bank -- would never come back. */
                if (SndBankIsLoopSnd(auto_bd_wrk[i].bank_no, 0))             /* 166 */
                {
                    PRINT_ASSERT("snd_utilAutoBDMain Not Use Loop Snd");     /* 167 */

                    SndBankRelease(auto_bd_wrk[i].bank_no);                  /* 170 */
                    auto_bd_wrk[i].mode = AUTO_BD_MODE_NO_USE;
                }
                else
                {
                    auto_bd_wrk[i].play_id =
                        SndBankPlay(auto_bd_wrk[i].bank_no, 0,
                                    auto_bd_wrk[i].effect, auto_bd_wrk[i].loop,
                                    auto_bd_wrk[i].vol, auto_bd_wrk[i].pitch,
                                    auto_bd_wrk[i].in_time, ptr);            /* 172, 179 */

                    auto_bd_wrk[i].mode = AUTO_BD_MODE_PLAYEND_WAIT;
                }
            }
            break;

        case AUTO_BD_MODE_PLAYEND_WAIT:
            if (SndBufIsPlaying(auto_bd_wrk[i].play_id) == 0)                /* 182 */
            {
                SndBankRelease(auto_bd_wrk[i].bank_no);                      /* 183 */
                auto_bd_wrk[i].mode = AUTO_BD_MODE_NO_USE;                   /* 184 */
            }
            break;
        }
    }
}                                                                            /* 189 */
