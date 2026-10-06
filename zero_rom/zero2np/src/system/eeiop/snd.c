/* ==========================================================================
 *  system/eeiop/snd.c
 *
 *  Sound front end.  Three jobs:
 *
 *    - bring-up.  SndInit() initialises the SPU memory pool and voice
 *      allocator, tells the IOP to start its sound server, then walks the
 *      other modules in dependency order, handing each its slice of the one
 *      work buffer and returning what is left.
 *    - the per-frame pump, SndMain().
 *    - the shared mixing arithmetic every voice owner runs: SndCalcValue()
 *      turns a caller's vol/pan/pitch plus the group volume plus an optional
 *      3D handle into the two numbers the SPU actually wants, and the two
 *      fade helpers give the ramps their per-frame step.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include <stdint.h>
#include <stdio.h>

#include "snd.h"

#include "ee_iop.h"
#include "snd3d.h"
#include "snd_buffer.h"
#include "snd_pcmstream.h"
#include "snd_stream.h"
#include "snd_util.h"
#include "sndbank.h"
#include "spu_mem.h"
#include "spu_voice.h"
#include "stream_auto.h"

#include "../../common/utility2.h"  /* PRINT_ASSERT */

/* SPU2 reverb work-area sizes, indexed by sceSdEffectAttr mode: OFF, ROOM,
 * STUDIO_A, STUDIO_B, STUDIO_C, HALL, SPACE, ECHO, DELAY, PIPE. */
int eff_use_size_tbl[] =                                                     /* data 35e998 */
{
    0x00000080, 0x000026c0, 0x00001f40, 0x00004840, 0x00006fe0,
    0x0000ade0, 0x0000f6c0, 0x00018040, 0x00018040, 0x00003c00
};

static SOUND_SYS snd_sys;                                                    /* bss 4c08b8 */
static int       effect_mode[2];                                             /* sbss 3f5078 */
static int       effect_adrs[2];                                             /* sbss 3f5080 */

/* --------------------------------------------------------------------------
 *  Bring-up
 * ------------------------------------------------------------------------ */

int sndGetNeedSize(EEIOP_DEF *def)
{
    int size;

    size  = SndBankGetOneWrkSize()     * def->snd_bank_wrk_num;              /* 55 */
    size += StreamAutoGetOneWrkSize()  * def->stream_auto_wrk_num;           /* 56 */
    size += snd_utilGetOneWrkSize()    * def->auto_bd_wrk_num;               /* 57 */

    return size;                                                             /* 59 */
}

/* 72 */
void *SndInit(EEIOP_DEF *def, void *buffer)
{
    IOP_SND_INIT   snd_init;
    SET_SND_EFFECT eff;

    SPUMemoryInit();                                                         /* 74 */
    InitSPUVoice();                                                          /* 77 */

    /* media 2 is the CD/DVD path; anything else (host file server) gets 0. */
    snd_init.media      = (def->media == 2) ? 0x800 : 0;                     /* 82 */
    snd_init.mvol       = 0x3fff;                                            /* 83 */
    snd_init.stop_block = GetSPUMemory(0x10);                                /* 84 */

    iopCommandRegister(REQ_IOP_SND_INIT, (char *)&snd_init,
                       sizeof(IOP_SND_INIT));                                /* 88 */

    Snd3DInit();                                                             /* 92 */
    SndBufInit();                                                            /* 95 */
    SndStreamInit(def->snd_stream_load_priority);                            /* 98 */
    SndPCMStreamInit();                                                      /* 101 */

    /* Each of these three takes the cursor, carves out its own table and
     * hands back what is left. */
    buffer = SndBankInitAll(buffer, def->snd_bank_wrk_num,
                            def->snd_bank_load_priority);                    /* 104 */
    buffer = StreamAutoPlayInit(buffer, def->stream_auto_wrk_num);           /* 107 */
    buffer = snd_utilAutoBDInit(buffer, def->auto_bd_wrk_num);               /* 110 */

    snd_sys.mono = 0;                                                        /* 113 */

    /* Core 0 gets HALL reverb up front.  Note effect_mode[0] is left at 0, so
     * the first SndSetEffect(0, ..., 3) takes the mode-changed path and
     * re-allocates a buffer of the same size -- harmless, but that is what
     * the ROM does. */
    eff.core            = 0;                                                 /* 119 */
    eff.r_attr.core     = 0;                                                 /* 120 */
    eff.r_attr.mode     = 3;                                                 /* 122 */
    eff.r_attr.depth_L  = eff.r_attr.depth_R = 0x2fff;                       /* 123 */

    effect_adrs[0] = (int)(uintptr_t)GetSPUEffectMemory(eff_use_size_tbl[3]);           /* 125 */
    eff.end_adrs   = effect_adrs[0];

    iopCommandRegister(REQ_SET_SND_EFFECT, (char *)&eff, sizeof(SET_SND_EFFECT));       /* 129 */

    for (int i = 0; i < VOICE_TYPE_MAX; i++)                                                  /* 135 */
        SndSetGroupVolume(i, 0x100);                                         /* 136 */

    /* Two passes so both the sound-init and the effect command are actually
     * across to the IOP before anything tries to play. */
    ee_iopMain();                                                            /* 141 */
    ee_iopMain();                                                            /* 142 */

    return buffer;                                                           /* 145 */
}

void SndInitAfter_ee_iopInit(void)
{
}                                                                            /* 152 */

void SndMain(void)
{
    Snd3DMain();                                                             /* 157 */
    StreamAutoPlayMain();                                                    /* 160 */
    snd_utilAutoBDMain();                                                    /* 163 */
    SndStreamMain();                                                         /* 167 */
    SndPCMStreamMain();                                                      /* 170 */
    SndBufPlayMain();                                                        /* 173 */
}

void SndFremaAfterMain(void)
{
}                                                                            /* 181 */

/* --------------------------------------------------------------------------
 *  Reverb
 * ------------------------------------------------------------------------ */

/* 186 */
void SndSetEffect(int core, int eff_vol, int mode)
{
    SET_SND_EFFECT eff;

    if ((unsigned int)eff_vol > 0x7fff)                                      /* 190 */
    {
        printf("Set effect Volume is Illegal 0~%d\n", 0x7fff);               /* 191 */
        for (;;)                                                             /* 192 */
            ;
    }

    /* NOTE: neither branch writes r_attr.delay or r_attr.feedback, and the
     * same-mode branch leaves end_adrs alone too, so part of the 0x1c-byte
     * packet is whatever was on the stack.  Reproduced as found -- the IOP
     * side evidently only reads the fields that are set. */
    if (mode != effect_mode[core])                                           /* 196 */
    {
        ReleaseSPUEffectMemory((void *)(uintptr_t)effect_adrs[core]);                   /* 198 */

        eff.core           = eff.r_attr.core = core;                         /* 200 */
        eff.r_attr.mode    = mode;                                           /* 202 */
        eff.r_attr.depth_L = eff.r_attr.depth_R = eff_vol;                   /* 203 */

        effect_adrs[core] = (int)(uintptr_t)GetSPUEffectMemory(eff_use_size_tbl[mode]); /* 207 */
        eff.end_adrs      = effect_adrs[core];

        iopCommandRegister(REQ_SET_SND_EFFECT, (char *)&eff,
                           sizeof(SET_SND_EFFECT));                          /* 211 */
    }                                                                        /* 212 */
    else
    {
        eff.core           = eff.r_attr.core = core;                         /* 216 */
        eff.r_attr.mode    = mode;                                           /* 218 */
        eff.r_attr.depth_L = eff.r_attr.depth_R = eff_vol;                   /* 219 */

        iopCommandRegister(REQ_SET_SND_EFFECT, (char *)&eff,
                           sizeof(SET_SND_EFFECT));                          /* 222 */
    }

    effect_mode[core] = mode;                                                /* 226 */
}                                                                            /* 227 */

/* --------------------------------------------------------------------------
 *  Mixer state
 * ------------------------------------------------------------------------ */

void SndSetStereo(void)
{
    snd_sys.mono = false;                                                        /* 232 */
}

void SndSetMono(void)
{
    snd_sys.mono = true;                                                        /* 237 */
}

int SndIsMono(void)
{
    return snd_sys.mono;                                                     /* 243 */
}

/* 249 */
void SndSetGroupVolume(int type, int vol)
{
    if (type >= VOICE_TYPE_MAX) {                                            /* 251 */
        PRINT_ASSERT("SndSetGroupVolume() Illegal Voice Type");              /* 252 */
    }

    if (vol >= 0x101)                                           /* 255 */
    {
        printf("SndSetGroupVolume() Illegal Voice Volume 0x0~%d\n", 0x100);  /* 256 */
        for (;;)                                                             /* 257 */
            ;
    }

    snd_sys.type_vol[type] = vol;                                            /* 260 */
}

/* 267 */
int SndGetGroupVolume(int type)
{
    if (type >= VOICE_TYPE_MAX)                                              /* 268 */
    {
        PRINT_ASSERT("SndGetGroupVolume() Illegal Voice Type");              /* 269 */
        for (;;)
            ;
    }

    return snd_sys.type_vol[type];                                           /* 272 */
}

/* --------------------------------------------------------------------------
 *  Mixing
 * ------------------------------------------------------------------------ */

/* 279 */
void SndCalcValue(int vol, int pan, int bvol, int pitch, int bpitch, int type,
                  void *s3d, VOLSET *volset, short *pPitch, float play_speed)
{
    VOLSET s3dvol;

    /* Both scalers are fixed point: bvol is 0..0x80 and the group volume
     * 0..0x100, so the two shifts put `vol` back on its own scale. */
    vol = (vol * bvol >> 7) * SndGetGroupVolume(type) >> 8;                  /* 282, 285 */

    volset->l = vol * (0x80 - pan) >> 7;                                     /* 287 */
    volset->r = vol * pan >> 7;                                              /* 288 */

    if (s3d != (void *)0)                                                    /* 291 */
    {
        /* The 3D pair is 0..0x3fff, so it multiplies in as a >>14 fraction
         * and completely replaces whatever `pan` said. */
        Snd3DGetVal(s3d, &s3dvol, pPitch);                                   /* 294 */

        volset->l = volset->l * s3dvol.l >> 14;                              /* 296 */
        volset->r = volset->r * s3dvol.r >> 14;                              /* 297 */

        *pPitch   = bpitch * *pPitch >> 12;                                  /* 298 */
    }                                                                        /* 299 */
    else
    {
        *pPitch = bpitch;                                                    /* 302 */
    }

    *pPitch = (short)((float)(pitch * *pPitch >> 12) * play_speed);          /* 305, 307 */

    if (*pPitch > 0x3fff)                                                    /* 312 */
        printf("SndCalcValue pitch is over max\n");                          /* 313 */

    if (SndIsMono())                                                         /* 318 */
        volset->r = volset->l = (volset->l + volset->r) >> 1;                /* 319 */
}                                                                            /* 323 */

/* 326 */
short SndGetFrameAddVol(int target, int now, int time)
{
    int diff;
    int frame;
    int absdiff;

    diff    = target - now;                                                  /* 328 */
    frame   = time;                                                          /* 329 */
    absdiff = (diff < 0) ? -diff : diff;                                     /* 330 */

    /* A ramp shorter than one step per frame, or no time at all, jumps
     * straight to the target on the first frame. */
    if (absdiff < frame || frame == 0)                                       /* 332 */
        return diff;                                                         /* 335 */

    return diff / time;                                                      /* 337 */
}                                                                            /* 339 */

/* 346 */
int SndAddFadeVol(int vol, int target_vol, int spd)
{
    int new_vol;

    new_vol = vol + spd;                                                     /* 349 */

    /* The product goes non-positive the moment the step reaches or crosses
     * the target, in either direction. */
    if (spd != 0 && (target_vol - vol) * (target_vol - new_vol) > 0)         /* 351 */
        return new_vol;

    return target_vol;                                                       /* 356 */
}

void SndAllStop(void)
{
    SndBufAllStop();                                                         /* 364 */
    SndStreamAllStop();                                                      /* 366 */
}

/* --------------------------------------------------------------------------
 *  Voice census
 * ------------------------------------------------------------------------ */

/* 371 */
int GetFreeVoiceNum(void)
{
    int voice[25];
    int iRetNum;
    int iNum;
    int i;
    int j;

    iRetNum = 0;                                                             /* 373 */

    for (i = 0; i < 2; i++)                                                  /* 376 */
    {
        iNum = 0;                                                            /* 377 */

        /* There is no query, so the only way to count is to take them all.
         * voice[] has 25 slots for 24 voices plus the -1 that ends the run. */
        while (1)                                                            /* 378 */
        {
            voice[iNum] = GetSPUVoiceCore(i);                                /* 379 */
            if (voice[iNum] == -1)                                           /* 380 */
                break;

            iNum++;                                                          /* 383 */
        }

        for (j = 0; j < iNum; j++)                                           /* 385 */
            FreeSPUVoiceCore(i, voice[j]);                                   /* 386 */

        iRetNum += iNum;
    }

    return iRetNum;                                                          /* 391 */
}

int IsExistFreeVoice(void)
{
    return GetFreeVoiceNum();                                                /* 397 */
}
