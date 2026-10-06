/* ==========================================================================
 *  iop_libsd.cpp  (SPU2 sound library — PC-port implementation)
 *
 *  A register model, not a synthesiser.  It records every write the game makes
 *  and answers reads consistently, which is enough for the IOP reconstruction
 *  to run end to end: voices key on and off, ENDX latches, streams advance
 *  their ring buffers, and the EE gets a status block that changes.  No audio
 *  comes out yet.
 *
 *  Two behaviours have to be modelled rather than stored, because the ROM
 *  waits on them and would otherwise hang:
 *
 *    - SD_VA_NAX, the play position.  IsInStopBlock() polls it until two reads
 *      agree and StreamReleaseSub() loops until a voice lands inside the stop
 *      block, so a NAX frozen at its start address is an infinite loop.  Here
 *      a keyed-on voice's NAX walks forward from its start address and then
 *      sits at its loop address, which is what parking on the stop block looks
 *      like from the outside.
 *
 *    - SD_S_ENDX.  EndVoiceFindWork() edge-detects it to retire auto-release
 *      voices; a voice that never raises it is never released and its slot
 *      leaks.  A voice reaching its loop point raises its bit here.
 *
 *  AUDIO now exists.  iop_voice.cpp is the synthesiser: this file still owns
 *  the registers and SPU RAM, and forwards every write that matters (key on and
 *  off, SSA/LSAX, the SD_VP_* parameters, master volume) to the voice engine,
 *  which decodes that RAM and drives an audio device.  The two placeholders
 *  this file used to fake are now real:
 *
 *    - SD_VA_NAX comes from the engine's actual play position.
 *    - SD_S_ENDX latches from MioPan_SdSetVoiceEnd(), called when a voice
 *      reaches a loop-end block it is not repeating.
 *    - SD_A_IRQA fires the SPU2 handler from MioPan_SdVoiceReachedAddress(),
 *      called as playback crosses the armed address, so the ADPCM streamer is
 *      paced by playback rather than free-running.
 *
 *  WHAT IS STILL MISSING:
 *
 *    - Nothing of sceSdBlockTrans() any more: the auto-DMA plays for real
 *      through iop_voice.cpp, which is what makes movie audio audible.  Note
 *      the game's own PCM streamer never reaches it -- PCMStreamMain() is
 *      unreachable in the ROM -- so the movie player is its only user.
 *    - Nothing.  Reverb is live too: iop_reverb.cpp is the effect unit, and
 *      this file feeds it the six registers that drive it -- SD_A_EEA and
 *      sceSdSetEffectAttr() for the work area and preset, SD_P_EVOLL/EVOLR for
 *      the per-area depth, SD_C_EFFECT_ENABLE for the switch, and
 *      SD_S_VMIXEL/VMIXER for which voices are sent to it.
 * ======================================================================== */

#include "libsd.h"

#include "iop_voice.h"
#include "iop_reverb.h"
#include "iop_host.h"           /* MioPan_IopMemIsBareOffset */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SPU_CORE_NUM    2
#define SPU_VOICE_NUM   24
#define SPU_MEM_SIZE    0x00200000

/* Register entries are 16 bits: bit 0 the core, bits 1..5 the voice, and the
 * rest the register address.
 *
 * The register field therefore has to be masked with ~0x3f, not 0xff00: the
 * SD_VA_* selectors carry a 0x40 bit (SD_VA_SSA is 0x2040) which 0xff00 throws
 * away, so every case label in sceSdSetAddr()/sceSdGetAddr() missed and every
 * address write was silently discarded.  That was invisible while nothing read
 * the addresses back -- start_adrs stayed 0 and the modelled NAX still satisfied
 * the ROM's settle loops -- and became a voice decoding silence from SPU
 * address 0 the moment iop_voice.cpp started using them for real. */
static int EntryCore(u_short entry)  { return entry & 1; }
static int EntryVoice(u_short entry) { return (entry >> 1) & 0x1f; }
static int EntryReg(u_short entry)   { return entry & 0xffc0; }

typedef struct
{
    u_int start_adrs;               /* SD_VA_SSA */
    u_int loop_adrs;                /* SD_VA_LSAX */
    u_int now_adrs;                 /* SD_VA_NAX, modelled */
    int   keyed_on;
    u_short param[8];               /* by register index, SD_VP_* >> 8 */
} SdVoice;

typedef struct
{
    SdVoice voice[SPU_VOICE_NUM];
    u_int   endx;                   /* SD_S_ENDX, one bit per voice */
    u_int   switches[0x20];         /* by register index, SD_S_* >> 8 */
    u_short param[0x20];            /* per-core, by register index    */
    u_int   irqa;
    u_short attr[0x10];
    void   *trans_handler;
    void   *trans_arg;
    /* The auto-DMA ring: where it starts and how long it is.  iop_voice.cpp
     * plays out of it and reports how far it has got, which is what
     * sceSdBlockTransStatus() turns back into an address -- every caller
     * subtracts its own ring base from the answer and treats the difference as
     * a position. */
    u_int   block_trans_adrs;
    u_int   block_trans_size;
    /* SD_A_EEA: the last byte of the reverb work area.  The ROM writes it
     * immediately before every sceSdSetEffectAttr(), which is the only reader. */
    u_int   eea;
} SdCore;

static SdCore  sd_core[SPU_CORE_NUM];
static void   *sd_spu2_handler;
static void   *sd_spu2_arg;
static int     sd_initialized;

/* SPU RAM.  The SPU2 had 2 MB of its own memory that the EE could not address
 * directly -- everything got there by DMA.  Sample banks, streamed ADPCM
 * packets and the stop block all live in it, and SPU addresses (0x5050, ...)
 * are offsets into this, not host pointers.
 *
 * It is modelled for real, even though nothing decodes it yet, because the
 * addresses are already flowing: the sound bank loader hands a bank body an
 * SPU destination, and without somewhere to put it that address would be
 * dereferenced as a pointer. */
static u_char sd_ram[SPU_MEM_SIZE];

void *MioPan_SpuRamPointer(u_int spu_adrs, u_int size)
{
    if (spu_adrs >= SPU_MEM_SIZE || size > SPU_MEM_SIZE - spu_adrs)
        return NULL;

    return sd_ram + spu_adrs;
}

/* How far NAX advances per read.  Any non-zero value satisfies the ROM's
 * "poll until two reads agree" loops; this is small enough that a voice does
 * not appear to finish before the game has keyed it on. */
#define NAX_STEP 0x40

int sceSdInit(int flag)
{
    (void)flag;

    memset(sd_core, 0, sizeof(sd_core));
    sd_initialized = 1;

    MioPan_VoiceInit();
    MioPan_ReverbInit();

    return 0;
}

/* --------------------------------------------------------------------------
 *  Parameters
 * ------------------------------------------------------------------------ */

void sceSdSetParam(u_short entry, u_short value)
{
    SdCore *core = &sd_core[EntryCore(entry)];
    int     reg  = EntryReg(entry) >> 8;

    if (EntryReg(entry) < SD_P_MMIX)
    {
        core->voice[EntryVoice(entry)].param[reg & 7] = value;
        MioPan_VoiceSetParam(EntryCore(entry), EntryVoice(entry), reg & 7, value);
    }
    else
    {
        core->param[reg & 0x1f] = value;

        /* PORT: these two comparisons used `& 0xff00`, which fails for exactly
         * the reason EntryReg() itself is `& ~0x3f` -- SD_P_MVOLL is 0x0980 and
         * masking it down to 0x0900 can never equal EntryReg()'s 0x0980.  So
         * master volume had never once reached the voice engine.  It looked
         * correct only by coincidence: the engine's own default is 0x3fff,
         * which is the value iopSndInit() passes as si->mvol. */
        if (EntryReg(entry) == EntryReg(SD_P_MVOLL) ||
            EntryReg(entry) == EntryReg(SD_P_MVOLR))
        {
            MioPan_VoiceSetMasterVolume(EntryCore(entry),
                                        core->param[(SD_P_MVOLL >> 8) & 0x1f],
                                        core->param[(SD_P_MVOLR >> 8) & 0x1f]);
        }

        /* EVOLL/EVOLR are the reverb return level.  map_reverb.c re-sends this
         * on every area change and nothing else about the effect moves, so
         * this is the register that carries the game's per-room reverb. */
        if (EntryReg(entry) == EntryReg(SD_P_EVOLL) ||
            EntryReg(entry) == EntryReg(SD_P_EVOLR))
        {
            MioPan_ReverbSetDepth(EntryCore(entry),
                                  core->param[(SD_P_EVOLL >> 8) & 0x1f],
                                  core->param[(SD_P_EVOLR >> 8) & 0x1f]);
        }

        /* BVOLL/BVOLR are the *external input* level, which is the auto-DMA's
         * -- playpss.c's changeInputVolume() is the only writer, and it is how
         * a movie fades and pauses without touching game audio. */
        if (EntryReg(entry) == EntryReg(SD_P_BVOLL) ||
            EntryReg(entry) == EntryReg(SD_P_BVOLR))
        {
            MioPan_VoiceAutoDmaSetVolume(EntryCore(entry),
                                         core->param[(SD_P_BVOLL >> 8) & 0x1f],
                                         core->param[(SD_P_BVOLR >> 8) & 0x1f]);
        }
    }
}

u_short sceSdGetParam(u_short entry)
{
    SdCore *core = &sd_core[EntryCore(entry)];
    int     reg  = EntryReg(entry) >> 8;

    if (EntryReg(entry) < SD_P_MMIX)
        return core->voice[EntryVoice(entry)].param[reg & 7];

    return core->param[reg & 0x1f];
}

/* --------------------------------------------------------------------------
 *  Switches
 * ------------------------------------------------------------------------ */

void sceSdSetSwitch(u_short entry, u_int value)
{
    SdCore *core = &sd_core[EntryCore(entry)];
    int     reg  = EntryReg(entry);

    core->switches[(reg >> 8) & 0x1f] = value;

    /* The mixer routing.  VMIXL/VMIXR are the dry sends, which iopSndInit()
     * turns on for every voice once and never touches again; VMIXEL/VMIXER are
     * the reverb sends, which EffectMix() maintains per voice from the sound
     * data's own `effect` bit and FrameWrkVoices() flushes once per command
     * frame. */
    if (reg == SD_S_VMIXL  || reg == SD_S_VMIXR ||
        reg == SD_S_VMIXEL || reg == SD_S_VMIXER)
    {
        MioPan_VoiceSetMixMasks(EntryCore(entry),
                                core->switches[(SD_S_VMIXL  >> 8) & 0x1f],
                                core->switches[(SD_S_VMIXR  >> 8) & 0x1f],
                                core->switches[(SD_S_VMIXEL >> 8) & 0x1f],
                                core->switches[(SD_S_VMIXER >> 8) & 0x1f]);
    }

    if (reg == SD_S_KON)
    {
        for (int v = 0; v < SPU_VOICE_NUM; v++)
        {
            if ((value >> v) & 1)
            {
                SdVoice *voice = &core->voice[v];
                voice->keyed_on = 1;
                voice->now_adrs = voice->start_adrs;
                /* Keying on clears this voice's end flag -- FrameWrkVoices()
                 * assumes exactly that when it drops the bit from
                 * pre_end_voice[]. */
                core->endx &= ~(1u << v);
            }
        }

        MioPan_VoiceKeyOn(EntryCore(entry), value);
    }
    else if (reg == SD_S_KOFF)
    {
        for (int v = 0; v < SPU_VOICE_NUM; v++)
        {
            if ((value >> v) & 1)
                core->voice[v].keyed_on = 0;
        }

        MioPan_VoiceKeyOff(EntryCore(entry), value);
    }
}

u_int sceSdGetSwitch(u_short entry)
{
    SdCore *core = &sd_core[EntryCore(entry)];

    if (EntryReg(entry) == SD_S_ENDX)
        return core->endx;

    return core->switches[(EntryReg(entry) >> 8) & 0x1f];
}

/* --------------------------------------------------------------------------
 *  Addresses
 * ------------------------------------------------------------------------ */

void sceSdSetAddr(u_short entry, u_int value)
{
    SdCore *core = &sd_core[EntryCore(entry)];

    switch (EntryReg(entry))
    {
    case SD_VA_SSA:
        core->voice[EntryVoice(entry)].start_adrs = value;
        MioPan_VoiceSetStartAddr(EntryCore(entry), EntryVoice(entry), value);
        break;
    case SD_VA_LSAX:
        core->voice[EntryVoice(entry)].loop_adrs = value;
        MioPan_VoiceSetLoopAddr(EntryCore(entry), EntryVoice(entry), value);
        break;
    case SD_VA_NAX:
        core->voice[EntryVoice(entry)].now_adrs = value;
        break;
    case SD_A_IRQA:
        core->irqa = value;
        break;
    case SD_A_EEA:
        /* The last byte of the reverb work area.  Written immediately before
         * every sceSdSetEffectAttr(), which is what turns it into a base. */
        core->eea = value;
        break;
    default:
        break;
    }
}

u_int sceSdGetAddr(u_short entry)
{
    SdCore  *core  = &sd_core[EntryCore(entry)];
    SdVoice *voice = &core->voice[EntryVoice(entry)];

    switch (EntryReg(entry))
    {
    case SD_VA_SSA:
        return voice->start_adrs;

    case SD_VA_LSAX:
        return voice->loop_adrs;

    case SD_VA_NAX:
        /* Actively decoding: the real play position, straight from the voice
         * engine.  The ROM's "poll until two reads agree" loops (IsInStopBlock,
         * MyPauseVoice) work against this exactly as they did against hardware,
         * because it genuinely advances while it is being read.
         *
         * Otherwise the voice is idling on its loop address, which is what an
         * SPU2 voice does once it has ended -- and crucially it must keep
         * following LSAX *after* that, because the ROM's stop sequence is
         * "end the voice, then point LSAX at the stop block and wait for NAX to
         * arrive there" (SetLoopAdrsStopBlock + iop_stream.c's release).
         *
         * This deliberately does not test keyed_on.  A voice that has been
         * keyed off is exactly the case the stop sequence is waiting on, and
         * gating on keyed_on left it reporting a stale mid-buffer address for
         * ever -- "stop voice wait id N adrs ..." spinning until the stream
         * slot, its voices and the next stream were all wedged behind it. */
        if (MioPan_VoiceIsPlaying(EntryCore(entry), EntryVoice(entry)))
            voice->now_adrs = MioPan_VoiceGetNowAddr(EntryCore(entry), EntryVoice(entry));
        else
            voice->now_adrs = voice->loop_adrs;

        return voice->now_adrs;

    case SD_A_IRQA:
        return core->irqa;

    default:
        return 0;
    }
}

void sceSdSetCoreAttr(u_short entry, u_short value)
{
    sd_core[EntryCore(entry)].attr[(entry & 0xff) >> 1] = value;

    /* The core bit is bit 0, so mask it off before identifying the attribute. */
    if ((entry & 0xfe) == SD_C_EFFECT_ENABLE)
        MioPan_ReverbSetEnable(EntryCore(entry), value != 0);
}

/* --------------------------------------------------------------------------
 *  Transfers
 *
 *  SPU RAM is not modelled, so an upload is accepted and discarded.  What must
 *  be right is the completion: every caller either polls
 *  sceSdVoiceTransStatus() or waits on the semaphore the transfer handler
 *  signals, so the handler has to fire.
 * ------------------------------------------------------------------------ */

int sceSdVoiceTrans(short chan, u_int mode, u_char *iopaddr, u_int spuaddr, u_int size)
{
    (void)mode;

    void *dst = MioPan_SpuRamPointer(spuaddr, size);

    /* PORT: the source is an IOP address the ROM carried through an int, so a
     * caller that computed it from a base that has since been zeroed hands over
     * a bare offset -- non-NULL, small, and mapped to nothing, which the plain
     * NULL test below waves straight through into the memcpy.  0x12000 is
     * StreamTransSub()'s rb_top + rb_voice * rb_size with rb_top gone.
     *
     * Refuse it and name it.  One line beats a segfault three frames later in
     * code with nothing to do with sound, and the transfer handler still fires
     * below, so the caller is not left waiting on a semaphore for ever.  Note
     * this is a net, not the fix: StreamAbort() in iop_stream.c is where the
     * base actually goes missing. */
    if (MioPan_IopMemIsBareOffset(iopaddr))
    {
        static int reported;

        if (reported < 8)
        {
            reported++;
            printf("iop: sceSdVoiceTrans core=%d src=%p size=%#x is not IOP memory"
                   " -- transfer dropped\n", chan & 1, (void *)iopaddr, size);
        }

        iopaddr = NULL;
    }

    if (dst != NULL && iopaddr != NULL)
        memcpy(dst, iopaddr, size);

    int core = chan & 1;
    if (sd_core[core].trans_handler != NULL)
        ((int (*)(int, void *))sd_core[core].trans_handler)(core, sd_core[core].trans_arg);

    return 0;
}

/* Current auto-DMA read address, or the ring base if nothing is playing. */
static int BlockTransNowAddr(int core)
{
    if (sd_core[core].block_trans_size == 0)
        return (int)sd_core[core].block_trans_adrs;

    return (int)(sd_core[core].block_trans_adrs +
                 MioPan_VoiceAutoDmaGetOffset(core));
}

int sceSdBlockTrans(short chan, u_short mode, u_char *iopaddr, u_int size, u_int start_addr)
{
    int core = chan & 1;

    if (mode == SD_BLOCK_TRANS_STAT)
    {
        /* Stop and report.  audioDecPause() reads the answer as the play
         * position and parks it so audioDecResume() can pick up there. */
        int now = BlockTransNowAddr(core);

        MioPan_VoiceAutoDmaStop(core);
        return now;
    }

    /* Arm it.  The ring is real host memory now -- sceSifAllocIopHeap() serves
     * it out of the IOP arena -- so the player can read straight out of the
     * bytes audiodec.c's sendToIOP() put there. */
    sd_core[core].block_trans_adrs = (u_int)(uintptr_t)iopaddr;
    sd_core[core].block_trans_size = size;

    if (iopaddr != NULL && size != 0)
    {
        u_int off = (mode == SD_BLOCK_TRANS_CONT)
                  ? (start_addr - (u_int)(uintptr_t)iopaddr) : 0u;

        MioPan_VoiceAutoDmaStart(core, iopaddr, size, off);
    }

    if (sd_core[core].trans_handler != NULL)
        ((int (*)(int, void *))sd_core[core].trans_handler)(core, sd_core[core].trans_arg);

    return 0;
}

int sceSdVoiceTransStatus(short chan, short flag)
{
    (void)chan;
    (void)flag;

    return 1;                       /* always idle */
}

/* `flag` 0 reports where the auto-DMA is reading, which is what audiodec.c
 * wants -- it masks the answer with 0xffffff and subtracts its own ring base
 * to get a position.  Nothing streams here, so the answer is wherever the ring
 * was last pointed: position 0, rather than the negative one a bare 0 would
 * give.  Any other flag is a busy query, and the transfer is always done. */
int sceSdBlockTransStatus(short chan, short flag)
{
    int core = chan & 1;

    if (flag != 0)
        return 0;

    return BlockTransNowAddr(core);
}

void *sceSdSetTransIntrHandler(int core, void *func, void *arg)
{
    if (core < 0 || core >= SPU_CORE_NUM)
        return NULL;

    void *old = sd_core[core].trans_handler;
    sd_core[core].trans_handler = func;
    sd_core[core].trans_arg     = arg;

    return old;
}

void *sceSdSetSpu2IntrHandler(void *func, void *arg)
{
    void *old = sd_spu2_handler;
    sd_spu2_handler = func;
    sd_spu2_arg     = arg;

    return old;
}

/* --------------------------------------------------------------------------
 *  Callbacks from the voice engine
 * ------------------------------------------------------------------------ */

/* Playback has advanced to `nax` on this core.  The engine calls this once per
 * decoded block, so the comparison is against a block boundary rather than an
 * exact sample -- IRQA is always armed at one, since it is a buffer address.
 *
 * The handler runs on the engine's decode thread, which is the closest thing
 * here to the SPU2 interrupt it stands in for: it signals sema_Voice_array and
 * StreamVoiceThread() wakes up to refill the half that just finished. */
/* One block's worth of tolerance: the decoder steps 16 bytes at a time, so an
 * IRQA that is not exactly on the step lands inside this window instead of
 * being missed entirely. */
static int IrqaMatches(int core, u_int nax)
{
    const u_int irqa = sd_core[core].irqa;

    return (sd_core[core].attr[SD_C_IRQ_ENABLE >> 1] & 1) &&
           nax >= irqa && nax < irqa + 16;
}

static u_int sd_irq_serial;

u_int MioPan_SdIrqSerial(void)
{
    return sd_irq_serial;
}

static void FireSpu2Irq(int core, u_int nax)
{
    (void)nax;

    sd_irq_serial++;

    ((void (*)(int, void *))sd_spu2_handler)(core == 1 ? 2 : 1, sd_spu2_arg);
}

void MioPan_SdVoiceReachedAddress(int core, u_int nax)
{
    if (core < 0 || core >= SPU_CORE_NUM || sd_spu2_handler == NULL)
        return;

    /* The hardware-faithful case: IRQA is a per-core register and fires for a
     * voice on that core. */
    if (IrqaMatches(core, nax))
    {
        FireSpu2Irq(core, nax);
        return;
    }

    /* Cross-core fallback, and a deliberate deviation from hardware.
     *
     * StreamVoiceThread() arms IRQA as `SD_A_IRQA | stp->irq_core` but with
     * *channel 0's* buffer address -- and irq_core does not come from the sound
     * data at all.  SetIRQCore() hands it out of its own two-entry
     * irq_core_source[] pool, i.e. it is really a stream-slot index, while the
     * core a channel actually plays on is wrk->p.attr[i].core out of the HXD.
     * The two only agree by convention.  When they disagree the interrupt can
     * never fire on hardware terms, the refill never runs, and the stream
     * repeats whatever is in its buffer forever.
     *
     * Stream buffers are distinct SPU allocations, so matching the address on
     * the other core cannot collide with an unrelated stream; taking the match
     * is far safer than hanging the stream.  If this ever fires it is worth
     * knowing about, hence the banner. */
    const int other = core ^ 1;

    if (IrqaMatches(other, nax))
    {
        static int warned;
        if (!warned)
        {
            warned = 1;
            printf("SPU: IRQA armed on core %d but matched by a voice on core %d "
                   "(adrs %x) -- irq_core/attr.core disagree; see "
                   "SetIRQCore() in snd_stream.c\n", other, core, nax);
        }

        FireSpu2Irq(other, nax);
    }
}

void MioPan_SdSetVoiceEnd(int core, int voice)
{
    if (core < 0 || core >= SPU_CORE_NUM ||
        voice < 0 || voice >= SPU_VOICE_NUM)
        return;

    sd_core[core].endx |= 1u << voice;
}

/* --------------------------------------------------------------------------
 *  Reverb
 * ------------------------------------------------------------------------ */

int sceSdSetEffectAttr(int core, void *attr)
{
    const sceSdEffectAttrSdk *a = (const sceSdEffectAttrSdk *)attr;

    if (core < 0 || core >= SPU_CORE_NUM || a == NULL)
        return -1;

    /* The work area is sized from the preset and anchored on the SD_A_EEA the
     * ROM wrote a moment earlier -- that ordering is the ROM's, in both
     * iopSndInit() and iopSndSetEffect(), and it is what makes the end address
     * available here.
     *
     * `delay` and `feedback` are ignored: they mean something only to the echo
     * and delay presets, and SndSetEffect() never initialises either field --
     * both arrive as whatever was on the EE's stack.  See the note at its two
     * call sites; the game only ever asks for mode 3. */
    MioPan_ReverbSetPreset(core, (int)a->mode, sd_core[core].eea);
    MioPan_ReverbSetDepth(core, (u_short)a->depth_L, (u_short)a->depth_R);

    /* Keep the register mirror in step.  iopSndSetEffect()'s same-mode path
     * sets the depth through sceSdSetParam(SD_P_EVOLL) instead of coming
     * through here, and sceSdGetParam() has to answer alike either way. */
    sd_core[core].param[(SD_P_EVOLL >> 8) & 0x1f] = (u_short)a->depth_L;
    sd_core[core].param[(SD_P_EVOLR >> 8) & 0x1f] = (u_short)a->depth_R;

    return 0;
}

int sceSdClearEffectWorkArea(int core, int channel, int effect_mode)
{
    (void)channel;      /* the core borrowed to run the clearing transfer */
    (void)effect_mode;

    if (core < 0 || core >= SPU_CORE_NUM)
        return 0;

    /* Clears the area the core is configured for *now*.  The ROM calls this
     * before writing the new EEA and preset, so on a mode change the area it
     * names here is the outgoing one; the incoming buffer is zeroed by
     * MioPan_ReverbSetPreset() instead, which is the only point at which its
     * extent is actually known.  Both are inside the reverb's own allocation,
     * so neither can reach sample data.
     *
     * iopSndInit() retries until this answers 0. */
    MioPan_ReverbClearWorkArea(core);

    return 0;
}
