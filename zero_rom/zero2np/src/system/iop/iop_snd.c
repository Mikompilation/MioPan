/* ==========================================================================
 *  system/iop/iop_snd.c
 *
 *  Everything that touches an SPU2 voice.  iop_sb.c, iop_stream.c and
 *  iop_pcmstream.c all come through here, and nothing else in the module
 *  writes a voice register directly.
 *
 *  The design is a one-frame deferral.  Callers never key a voice on or off
 *  themselves -- they set a bit in key_on_voices[] / key_off_voices[] and
 *  FrameWrkVoices() flushes both to SD_S_KON / SD_S_KOFF once per iopCommand()
 *  call.  That is what makes KeyOnVoices()/KeyOffVoices() cancel each other:
 *  a voice keyed off and back on inside the same frame simply clears the
 *  pending bit, so the SPU never sees the gap.
 *
 *  Volumes are deferred differently.  VolSet() only records a target, and
 *  iopSndMain() -- a very low priority thread woken by a 500 us hard timer --
 *  walks all 48 voices and steps now_vol_* towards it.  The same walk drives
 *  the four-state pause machine in now_pause_phase[], so a pause is a fade to
 *  silence followed by a key-off rather than an abrupt stop.
 *
 *  The other recurring idea is the "stop block": four silent, self-looping
 *  ADPCM blocks uploaded to SPU RAM by iopSndInit().  A voice that is finished
 *  with gets its loop address pointed at them, so it keeps running but reads
 *  known-good memory forever instead of whatever the allocator hands out next.
 *  IsInStopBlock() is then the module's definition of "this voice has ended",
 *  and that bitmask is what the EE receives in IOP_RET_STATUS::voice_end.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_snd.h"

#include <stdio.h>                  /* printf                          */
#include <intrman.h>                /* CpuSuspendIntr / CpuResumeIntr  */
#include <libsd.h>                  /* sceSd*                          */
#include <thbase.h>                 /* CreateThread / SleepThread      */
#include <timrman.h>                /* the hard timer iopSndMain runs on */

#include "iop.h"                    /* SetEndVoices, WaitSpuTransSema  */
#include "utility2i.h"              /* PrintAssertReal                 */

/* libsd register entries.  The low bits carry the voice key, (voice_no << 1)
 * | core, or just the core for the per-core registers -- which is why `core`
 * is OR'd straight in everywhere below.  The ROM's own diagnostic text in
 * iopSndInit() names them this way ("SPU_CORE_1 | SD_P_MMIX"). */
#define SD_VP_VOLL          0x0000
#define SD_VP_VOLR          0x0100
#define SD_VP_PITCH         0x0200
#define SD_VP_ADSR1         0x0300
#define SD_VP_ADSR2         0x0400
#define SD_P_MMIX           0x0800
#define SD_P_MVOLL          0x0980
#define SD_P_MVOLR          0x0a80
#define SD_P_EVOLL          0x0b80
#define SD_P_EVOLR          0x0c80
#define SD_P_AVOLL          0x0d80
#define SD_P_AVOLR          0x0e80
#define SD_S_KON            0x1500
#define SD_S_KOFF           0x1600
#define SD_S_ENDX           0x1700
#define SD_S_VMIXL          0x1800
#define SD_S_VMIXEL         0x1900  /* dry -> effect send, left       */
#define SD_S_VMIXR          0x1a00
#define SD_S_VMIXER         0x1b00  /* dry -> effect send, right      */
#define SD_A_EEA            0x1d00  /* effect work area end address   */
#define SD_VA_SSA           0x2040  /* sample start address           */
#define SD_VA_LSAX          0x2140  /* loop start address             */
#define SD_VA_NAX           0x2240  /* current play address           */

/* sceSdSetCoreAttr() entries. */
#define SD_C_EFFECT_ENABLE  0x02
#define SD_C_SPDIF_MODE     0x0a

#define SPU_VOICE_NUM       24
#define SPU_ALL_VOICES      0x00ffffff  /* one bit per voice          */
#define SPU_MEM_END         0x001fffff  /* top of the 2 MB of SPU RAM */

#define VOL_MAX             0x7fff

/* The master mix for each core.  Core 2's differs because it additionally
 * takes core 1's output in through AVOL. */
#define MMIX_CORE_1         0x0fc0
#define MMIX_CORE_2         0x0fcc

/* Reverb preset used only to size the work area that gets cleared; no voice
 * is routed to the effect bus until iopSndSetEffect() asks for one. */
#define EFFECT_MODE_INIT    3

/* 0x02000000 is TH_C. */
#define SND_TH_ATTR         0x02000000
#define SND_TH_STACK        0x800
/* One tick per 500 us.  Everything iopSndMain() does is a volume step, so the
 * ramp rates below are per half-millisecond, not per frame. */
#define SND_TIMER_USEC      500
#define SND_TIMER_SOURCE    1
#define SND_TIMER_SIZE      32
#define SND_TIMER_PRESCALE  1

/* Volume ramp rates, in SPU volume units per tick.  Fading out is deliberately
 * much faster than fading in -- iop_pcmstream.c's iopPauseSubB() uses exactly
 * double both of these for the same reason. */
#define VOL_STEP_UP         0x96    /* 150 */
#define VOL_STEP_DOWN       400

static int            effect_voices[SPU_CORE_NUM];                           /* bss 1d18 */
static int            key_on_voices[SPU_CORE_NUM];                           /* bss 1d20 */
static int            key_off_voices[SPU_CORE_NUM];                          /* bss 1d28 */
static int            auto_release_voices[SPU_CORE_NUM];                     /* bss 1d30 */
static unsigned char  now_pause_phase[SPU_CORE_NUM][SPU_VOICE_NUM];          /* bss 1d38 */
static unsigned int   save_voice_adrs[SPU_CORE_NUM][SPU_VOICE_NUM];          /* bss 1d68 */
static short          target_vol_l[SPU_CORE_NUM][SPU_VOICE_NUM];             /* bss 1e28 */
static short          target_vol_r[SPU_CORE_NUM][SPU_VOICE_NUM];             /* bss 1e88 */
static short          now_vol_l[SPU_CORE_NUM][SPU_VOICE_NUM];                /* bss 1ee8 */
static short          now_vol_r[SPU_CORE_NUM][SPU_VOICE_NUM];                /* bss 1f48 */
static int            spu_stop_block_adrs;                                   /* bss 1fa8 */
static int            spu_stop_block_end_adrs;                               /* bss 1fac */
static int            pre_end_voice[SPU_CORE_NUM];                           /* bss 1fb0 */
int                   now_effect_mode[SPU_CORE_NUM];                         /* data 310 */
static SysClock       clock;                                                 /* bss 1fb8 */

static void   KeyOnVoices(int core, int voice_no);
static void   KeyOffVoices(int core, int voice_no);
static void   EndVoiceFindWork(int core);
static unsigned int _intr_iopSndTimer(void *common);
static void   iopSndSetTimer(void *common);
static int    iopPauseSub(int set, short *now_vol, short target_vol);

/* --------------------------------------------------------------------------
 *  Deferred key on / key off
 * ------------------------------------------------------------------------ */

void FrameInitVoices(void)
{
    int i;
    int j;

    for (i = 0; i < SPU_CORE_NUM; i++)
    {
        key_on_voices[i]       = 0;
        key_off_voices[i]      = 0;
        pre_end_voice[i]       = 0;
        auto_release_voices[i] = 0;

        for (j = 0; j < SPU_VOICE_NUM; j++)
            now_pause_phase[i][j] = PAUSE_PHASE_NONE;
    }
}

/* `voice_shift` is a 1 << voice_no mask here, not a packed voice key -- this
 * one takes a whole set of voices at a time. */
void EffectMix(int core, int voice_shift, int on)
{
    if (on != 0)
        effect_voices[core] |=  voice_shift;
    else
        effect_voices[core] &= ~voice_shift;
}

/* A key-on that cancels a key-off still pending from this same frame leaves
 * the voice running rather than restarting it -- the SPU never sees either. */
static void KeyOnVoices(int core, int voice_no)
{
    int voice_shift = 1 << voice_no;
    int iTmpAdrs;

    if ((key_off_voices[core] & voice_shift) != 0)
    {
        key_off_voices[core] &= ~voice_shift;
        return;
    }

    /* Refuse to key on a voice whose saved address is outside SPU RAM or
     * inside the stop block; those are not real samples. */
    iTmpAdrs = save_voice_adrs[core][voice_no];
    if (iTmpAdrs <= SPU_MEM_END && spu_stop_block_end_adrs <= iTmpAdrs)
        key_on_voices[core] |= voice_shift;
}

static void KeyOffVoices(int core, int voice_no)
{
    int voice_shift = 1 << voice_no;

    if ((key_on_voices[core] & voice_shift) != 0)
    {
        key_on_voices[core] &= ~voice_shift;
        return;
    }

    key_off_voices[core] |= voice_shift;
}

/* --------------------------------------------------------------------------
 *  The stop block
 * ------------------------------------------------------------------------ */

void SetLoopAdrsStopBlock(int core, int voice_no)
{
    int oldstat;

    CpuSuspendIntr(&oldstat);
    sceSdSetAddr(SD_VA_LSAX | core | (voice_no << 1), spu_stop_block_adrs);
    CpuResumeIntr(oldstat);
}

/* Empty in this build.  iop_stream.c's StreamStart() still calls it, so it is
 * a no-op there rather than a missing step. */
void SetAdrsStopBlock(int core, int voice_no)
{
}

/* "Has this voice finished?"  Only voices already parked on the stop block can
 * answer yes, so a voice still playing real sample data never reports ended. */
int IsInStopBlock(int core, int voice_no)
{
    int iTmpAdrs;
    int iAdrs;

    core |= voice_no << 1;

    if (sceSdGetAddr(SD_VA_LSAX | core) != spu_stop_block_adrs)
        return 0;

    /* NAX advances while it is being read, so a single read can catch a
     * half-updated value.  Read until two in a row agree. */
    iAdrs = -1;
    while ((iTmpAdrs = sceSdGetAddr(SD_VA_NAX | core)) != iAdrs)
        iAdrs = iTmpAdrs;

    if (iAdrs < spu_stop_block_adrs)
        return 0;

    if (iAdrs < spu_stop_block_end_adrs)
        return 1;

    return 0;
}

/* --------------------------------------------------------------------------
 *  Per-frame work, called from iopCommand()
 * ------------------------------------------------------------------------ */

static void EndVoiceFindWork(int core)
{
    int stop_block_voice;
    int set_end_voice;
    int voice;
    int now_end_voice;
    int temp2;
    int end_register;

    stop_block_voice = 0;
    for (voice = 0; voice < SPU_VOICE_NUM; voice++)
    {
        if (IsInStopBlock(core, voice))
            stop_block_voice |=  (1 << voice);
        else
            stop_block_voice &= ~(1 << voice);
    }

    /* ENDX latches, so the interesting quantity is which bits went up since
     * last frame: changed & now.  A voice that was armed with
     * MyOnVoiceAutoRelease() and has just reached its end point is retired
     * onto the stop block here, without the EE having to ask. */
    end_register  = sceSdGetSwitch(SD_S_ENDX | core);
    now_end_voice = pre_end_voice[core];

    pre_end_voice[core] = end_register;

    temp2         = now_end_voice ^ end_register;
    set_end_voice = auto_release_voices[core] & temp2 & (temp2 ^ now_end_voice);

    if (set_end_voice != 0)
    {
        for (voice = 0; voice < SPU_VOICE_NUM; voice++)
        {
            if ((set_end_voice >> voice) & 1)
                SetLoopAdrsStopBlock(core, voice);
        }
    }

    SetEndVoices(core, stop_block_voice);
}

void FrameWrkVoices(void)
{
    int i;
    int v;
    int oldstat;
    int adrs;

    for (i = 0; i < SPU_CORE_NUM; i++)
    {
        sceSdSetSwitch(SD_S_VMIXEL | i, effect_voices[i]);
        sceSdSetSwitch(SD_S_VMIXER | i, effect_voices[i]);

        EndVoiceFindWork(i);

        if (key_on_voices[i] != 0)
        {
            /* The start address and the key-on have to reach the SPU without
             * an interrupt in between, or a voice can be started before its
             * address has landed. */
            CpuSuspendIntr(&oldstat);

            for (v = 0; v < SPU_VOICE_NUM; v++)
            {
                if ((key_on_voices[i] >> v) & 1)
                {
                    adrs = save_voice_adrs[i][v];
                    sceSdSetAddr(SD_VA_SSA | i | (v << 1), adrs);
                }
            }

            sceSdSetSwitch(SD_S_KON | i, key_on_voices[i]);

            CpuResumeIntr(oldstat);

            /* A voice that has just been keyed on has not ended, whatever
             * ENDX said a moment ago. */
            pre_end_voice[i] &= ~key_on_voices[i];
            key_on_voices[i]  = 0;
        }

        if (key_off_voices[i] != 0)
        {
            sceSdSetSwitch(SD_S_KOFF | i, key_off_voices[i]);
            key_off_voices[i] = 0;
        }
    }
}

/* --------------------------------------------------------------------------
 *  Voice parameters
 * ------------------------------------------------------------------------ */

/* Writes the registers now instead of ramping.  The SPU is only touched when
 * the new volume is silence -- anything louder is left for iopSndMain()'s next
 * pass, which will find now_vol already equal to the target and write it. */
void VolSetDirect(int core, int voice_no, VOLSET volset)
{
    target_vol_l[core][voice_no] = volset.l;
    target_vol_r[core][voice_no] = volset.r;
    now_vol_l[core][voice_no]    = volset.l;
    now_vol_r[core][voice_no]    = volset.r;

    if (volset.l == 0 && volset.r == 0)
    {
        sceSdSetParam(core | (voice_no << 1), 0);
        sceSdSetParam(core | (voice_no << 1) | SD_VP_VOLR, 0);
    }
}

void VolSet(int core, int voice_no, VOLSET volset)
{
    target_vol_l[core][voice_no] = volset.l;
    target_vol_r[core][voice_no] = volset.r;
}

void SndVoiceStop(VOICE_STOP *sbp)
{
    SetLoopAdrsStopBlock(sbp->core, sbp->voice_no);
}

void SndVoiceLoopSet(VOICE_LOOP_SET *slp)
{
    int oldstat;

    printf("LoopAdrs Set core[%d] voice[%d] adrs[%x]\n",
           slp->core, slp->voice_no, slp->loop_adrs);

    CpuSuspendIntr(&oldstat);
    sceSdSetAddr(SD_VA_LSAX | slp->core | (slp->voice_no << 1), slp->loop_adrs);
    CpuResumeIntr(oldstat);
}

/* `set` here is the packed key, (voice_no << 1) | core -- not core and voice
 * separately the way the entry points above take them. */
void PitchSet(int set, short pitch)
{
    sceSdSetParam(set | SD_VP_PITCH, pitch);
}

void AdsrSet(int set, unsigned short adsr1, unsigned short adsr2)
{
    sceSdSetParam(set | SD_VP_ADSR1, adsr1);
    sceSdSetParam(set | SD_VP_ADSR2, adsr2);
}

/* --------------------------------------------------------------------------
 *  Bring-up
 * ------------------------------------------------------------------------ */

void iopSndInit(IOP_SND_INIT *si)
{
    int             i;
    int             oldstat;
    sceSdEffectAttr attr;
    int             trans_spu_core;
    int             stop_block_size;
    int             core;
    int             voice;
    ThreadParam     param;

    /* Four silent blocks that loop on themselves.  SPU_STOP_BLOCK is
     * loop-start, loop-end and end at once, so a voice parked here plays
     * silence forever without ever running off the end. */
    static SPU_BLOCK_DATA stop_block[4] =                                    /* data 2d0 */
    {
        { { 0, SPU_STOP_BLOCK }, { 0, 0, 0, 0, 0, 0, 0 } },
        { { 0, SPU_STOP_BLOCK }, { 0, 0, 0, 0, 0, 0, 0 } },
        { { 0, SPU_STOP_BLOCK }, { 0, 0, 0, 0, 0, 0, 0 } },
        { { 0, SPU_STOP_BLOCK }, { 0, 0, 0, 0, 0, 0, 0 } }
    };
    static int th;                                                           /* bss 1d10 */

    /* Read back before sceSdInit() wipes them -- this is the only record of
     * what the boot ROM left the mixer set to. */
    printf("sceSdGetParam(SPU_CORE_1 | SD_P_MMIX) = 0x%x\n",
           sceSdGetParam(SPU_CORE_1 | SD_P_MMIX));
    printf("sceSdGetParam(SPU_CORE_2 | SD_P_MMIX) = 0x%x\n",
           sceSdGetParam(SPU_CORE_2 | SD_P_MMIX));
    printf("sceSdGetParam(SPU_CORE_2 | SD_P_AVOLL) = 0x%x\n",
           sceSdGetParam(SPU_CORE_2 | SD_P_AVOLL));

    sceSdInit(0);

    sceSdSetParam(SPU_CORE_1 | SD_P_MMIX, MMIX_CORE_1);
    sceSdSetParam(SPU_CORE_2 | SD_P_MMIX, MMIX_CORE_2);

    CpuSuspendIntr(&oldstat);
    sceSdSetCoreAttr(SD_C_SPDIF_MODE, si->media | 0x80);
    CpuResumeIntr(oldstat);

    attr.mode     = EFFECT_MODE_INIT;
    attr.depth_L  = 0;
    attr.depth_R  = 0;
    attr.delay    = 0;
    attr.feedback = 0;

    for (i = 0; i < SPU_CORE_NUM; i++)
    {
        attr.core = i;

        /* The effect area runs down from the top of SPU RAM; setting the end
         * address first is what tells libsd how much there is to clear. */
        sceSdSetAddr(SD_A_EEA | i, SPU_MEM_END);
        sceSdSetEffectAttr(i, &attr);

        while (sceSdClearEffectWorkArea(i, 0, EFFECT_MODE_INIT) != 0)
            ;
    }

    for (i = 0; i < SPU_CORE_NUM; i++)
    {
        sceSdSetParam(i | SD_P_MVOLL, si->mvol);
        sceSdSetParam(i | SD_P_MVOLR, si->mvol);

        /* Every voice is in the dry mix permanently; only the effect send is
         * ever switched, by EffectMix(). */
        sceSdSetSwitch(i | SD_S_VMIXL, SPU_ALL_VOICES);
        sceSdSetSwitch(i | SD_S_VMIXR, SPU_ALL_VOICES);

        CpuSuspendIntr(&oldstat);
        sceSdSetCoreAttr(i | SD_C_EFFECT_ENABLE, 0);
        CpuResumeIntr(oldstat);

        now_effect_mode[i] = 0;
    }

    /* Core 2 mixes core 1's output in through AVOL, so this is the join
     * between the two cores rather than an output level. */
    sceSdSetParam(SPU_CORE_2 | SD_P_AVOLL, VOL_MAX);
    sceSdSetParam(SPU_CORE_2 | SD_P_AVOLR, VOL_MAX);

    stop_block_size = sizeof(stop_block);

    trans_spu_core = WaitSpuTransSema();
    sceSdSetTransIntrHandler(trans_spu_core, _intr_SignalTransCore, 0);

    while (sceSdVoiceTrans((short)trans_spu_core, 0, (unsigned char *)stop_block,
                           (unsigned int)si->stop_block, stop_block_size) < 0)
    {
        printf("cannot trans core[%d]\n", trans_spu_core);
        DelayThread(100);
    }

    WaitSPUTransEnd(trans_spu_core);

    spu_stop_block_adrs     = (int)si->stop_block;
    spu_stop_block_end_adrs = spu_stop_block_adrs + stop_block_size;

    /* Every voice starts out parked, so a voice keyed on before anyone has set
     * its loop address cannot wander off into unallocated SPU RAM. */
    for (core = 0; core < SPU_CORE_NUM; core++)
    {
        for (voice = 0; voice < SPU_VOICE_NUM; voice++)
            sceSdSetAddr(SD_VA_LSAX | core | (voice << 1), spu_stop_block_adrs);
    }

    param.attr         = SND_TH_ATTR;
    param.option       = 0;
    param.entry        = (void *)iopSndMain;
    param.stackSize    = SND_TH_STACK;
    param.initPriority = PRI_IOP_SND;

    th = CreateThread(&param);

    if (th > 0)
        StartThread(th, 0);

    /* `th` is static because the timer handler is handed its address and
     * dereferences it at interrupt time. */
    iopSndSetTimer(&th);

    printf("iopSndInit() Good! mvol[%d] \n", si->mvol);
}

void iopSndSetEffect(SET_SND_EFFECT *eff)
{
    int core;
    int oldstat;

    if (eff->r_attr.mode == 0)
    {
        printf("effect off\n");

        CpuSuspendIntr(&oldstat);
        sceSdSetCoreAttr(eff->core | SD_C_EFFECT_ENABLE, 0);
        CpuResumeIntr(oldstat);
    }
    else if (now_effect_mode[eff->core] == eff->r_attr.mode)
    {
        /* Same reverb type as last time: only the depth needs updating, and
         * the work area can be left alone. */
        sceSdSetParam(eff->core | SD_P_EVOLL, eff->r_attr.depth_L);
        sceSdSetParam(eff->core | SD_P_EVOLR, eff->r_attr.depth_R);
    }
    else
    {
        /* Changing type means the work area holds the wrong thing, and
         * clearing it is a transfer, so a core has to be borrowed for it. */
        core = WaitSpuTransSema();
        sceSdClearEffectWorkArea(eff->core, core, eff->r_attr.mode);
        SignalSpuTransSema(core);

        sceSdSetAddr(SD_A_EEA | eff->core, eff->end_adrs);
        sceSdSetEffectAttr(eff->core, &eff->r_attr);

        CpuSuspendIntr(&oldstat);
        sceSdSetCoreAttr(eff->core | SD_C_EFFECT_ENABLE, 1);
        CpuResumeIntr(oldstat);
    }

    now_effect_mode[eff->core] = eff->r_attr.mode;
}

/* --------------------------------------------------------------------------
 *  The volume / pause thread
 * ------------------------------------------------------------------------ */

/* Returns the next compare value, which is how a timrman handler asks to be
 * called again at the same interval. */
static unsigned int _intr_iopSndTimer(void *common)
{
    iWakeupThread(*(int *)common);

    return clock.lo;
}

static void iopSndSetTimer(void *common)
{
    int timer_id;

    USec2SysClock(SND_TIMER_USEC, &clock);

    timer_id = AllocHardTimer(SND_TIMER_SOURCE, SND_TIMER_SIZE, SND_TIMER_PRESCALE);
    if (timer_id <= 0)
        PrintAssertReal("Can NOT allocate hard timer ...\n");

    if (SetTimerHandler(timer_id, clock.lo, _intr_iopSndTimer, common) != 0)
        PrintAssertReal("Can NOT set timeup timer handler ...\n");

    if (SetupHardTimer(timer_id, SND_TIMER_SOURCE, 0, SND_TIMER_PRESCALE) != 0)
        PrintAssertReal("Can NOT setup hard timer ...\n");

    if (StartHardTimer(timer_id) != 0)
        PrintAssertReal("Can NOT start hard timer ...\n");
}

void iopSndMain(void)
{
    int core;
    int v;
    int ret;

    for (;;)
    {
        for (core = 0; core < SPU_CORE_NUM; core++)
        {
            for (v = 0; v < SPU_VOICE_NUM; v++)
            {
                int set = core | (v << 1);

                switch (now_pause_phase[core][v])
                {
                case PAUSE_PHASE_IN:
                    /* `&`, not `&&`: both channels must be stepped every pass,
                     * and iopPauseSub() answers 1 only on the step that lands
                     * exactly on the target. */
                    ret = iopPauseSub(set, &now_vol_l[core][v], 0)
                        & iopPauseSub(set | SD_VP_VOLR, &now_vol_r[core][v], 0);

                    if (ret != 0)
                    {
                        /* Silent now, so the key-off is inaudible. */
                        now_pause_phase[core][v] = PAUSE_PHASE_KEEP;
                        KeyOffVoices(core, v);
                    }
                    break;

                case PAUSE_PHASE_KEEP:
                    iopPauseSub(set, &now_vol_l[core][v], 0);
                    iopPauseSub(set | SD_VP_VOLR, &now_vol_r[core][v], 0);
                    break;

                case PAUSE_PHASE_OUT:
                    /* Restart from where the pause caught it.  MyOnVoice()
                     * drops the phase back to NONE, so this arm runs once. */
                    MyOnVoice(core, v, save_voice_adrs[core][v]);
                    /* fall through */

                case PAUSE_PHASE_NONE:
                    iopPauseSub(set, &now_vol_l[core][v],
                                target_vol_l[core][v]);
                    iopPauseSub(set | SD_VP_VOLR, &now_vol_r[core][v],
                                target_vol_r[core][v]);
                    break;
                }
            }
        }

        /* Woken again by _intr_iopSndTimer(). */
        SleepThread();
    }
}

/* One volume step.  Returns 1 once `now_vol` has reached `target_vol`, 0 while
 * still moving. */
static int iopPauseSub(int set, short *now_vol, short target_vol)
{
    int ret;

    ret = (short)(target_vol - *now_vol);

    if (ret > 0)
    {
        if (ret >= VOL_STEP_UP + 1)
        {
            *now_vol += VOL_STEP_UP;
            sceSdSetParam(set, *now_vol & VOL_MAX);
            return 0;
        }
    }
    else
    {
        if (ret <= -(VOL_STEP_DOWN + 1))
        {
            *now_vol -= VOL_STEP_DOWN;
            sceSdSetParam(set, *now_vol & VOL_MAX);
            return 0;
        }
    }

    /* Close enough to land on it exactly. */
    *now_vol = target_vol;
    sceSdSetParam(set, target_vol & VOL_MAX);

    return 1;
}

/* --------------------------------------------------------------------------
 *  Voice state changes
 * ------------------------------------------------------------------------ */

void MyPauseVoice(int core, int voice_no)
{
    int iTmpAdrs;
    int iAdrs;

    if (now_pause_phase[core][voice_no] == PAUSE_PHASE_OUT ||
        now_pause_phase[core][voice_no] == PAUSE_PHASE_KEEP)
    {
        /* Already paused, or on its way back up -- cancel the restart. */
        now_pause_phase[core][voice_no] = PAUSE_PHASE_KEEP;
    }
    else
    {
        /* Remember where the voice is so MyRestartVoice() can resume from
         * there.  NAX moves as it is read, so read until it settles. */
        iAdrs = -1;
        while ((iTmpAdrs = sceSdGetAddr(SD_VA_NAX | core | (voice_no << 1))) != iAdrs)
            iAdrs = iTmpAdrs;

        if (iTmpAdrs <= SPU_MEM_END && spu_stop_block_end_adrs <= iTmpAdrs)
            save_voice_adrs[core][voice_no] = iTmpAdrs;

        now_pause_phase[core][voice_no] = PAUSE_PHASE_IN;
    }
}

void MyRestartVoice(int core, int voice_no)
{
    if (now_pause_phase[core][voice_no] == PAUSE_PHASE_KEEP ||
        now_pause_phase[core][voice_no] == PAUSE_PHASE_OUT)
        now_pause_phase[core][voice_no] = PAUSE_PHASE_OUT;
    else
        /* Was never actually silenced, so there is nothing to key back on. */
        now_pause_phase[core][voice_no] = PAUSE_PHASE_NONE;
}

/* Restarts `voice_no` at a fixed distance from where another voice was paused.
 * That is how a stereo pair stays in step: the second channel resumes at the
 * first one's saved position plus the interleave gap between them. */
void MyRestartVoiceOffset(int core, int voice_no, int target_core,
                          int target_voice_no, int offset)
{
    if (now_pause_phase[core][voice_no] == PAUSE_PHASE_KEEP ||
        now_pause_phase[core][voice_no] == PAUSE_PHASE_OUT)
        now_pause_phase[core][voice_no] = PAUSE_PHASE_OUT;
    else
        now_pause_phase[core][voice_no] = PAUSE_PHASE_NONE;

    save_voice_adrs[core][voice_no] =
        save_voice_adrs[target_core][target_voice_no] + offset;
}

void MyOnVoice(int core, int voice_no, int adrs)
{
    auto_release_voices[core] &= ~(1 << voice_no);
    now_pause_phase[core][voice_no] = PAUSE_PHASE_NONE;

    if (adrs <= SPU_MEM_END && spu_stop_block_end_adrs <= adrs)
        save_voice_adrs[core][voice_no] = adrs;

    KeyOnVoices(core, voice_no);
}

/* As MyOnVoice(), but arms the voice so EndVoiceFindWork() parks it on the
 * stop block the frame after it raises ENDX. */
void MyOnVoiceAutoRelease(int core, int voice_no, int adrs)
{
    auto_release_voices[core] |= 1 << voice_no;
    now_pause_phase[core][voice_no] = PAUSE_PHASE_NONE;

    if (adrs <= SPU_MEM_END && spu_stop_block_end_adrs <= adrs)
        save_voice_adrs[core][voice_no] = adrs;

    KeyOnVoices(core, voice_no);
}

void MyOffVoice(int core, int voice_no)
{
    now_pause_phase[core][voice_no] = PAUSE_PHASE_NONE;
    KeyOffVoices(core, voice_no);
}

/* Empty in this build, and nothing calls it. */
void iopSndVoiceStop(VOICE_STOP *vs)
{
}

/* The address test the five sites above write out by hand.  Nothing calls
 * this -- it looks like a tidy-up that never got applied. */
int IsValidVoice(int iTmpAdrs)
{
    return iTmpAdrs <= SPU_MEM_END && spu_stop_block_end_adrs <= iTmpAdrs;
}

/* MyOnVoice() without the auto-release bookkeeping.  Also uncalled. */
void MyOnVoiceSub(int core, int voice_no, int adrs)
{
    now_pause_phase[core][voice_no] = PAUSE_PHASE_NONE;

    if (adrs <= SPU_MEM_END && spu_stop_block_end_adrs <= adrs)
        save_voice_adrs[core][voice_no] = adrs;

    KeyOnVoices(core, voice_no);
}
