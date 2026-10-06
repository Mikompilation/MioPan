/* ==========================================================================
 *  iop_voice.cpp  (SPU2 voice engine — PC-port implementation)
 *
 *  See iop_voice.h for the split between this and iop_libsd.cpp, and for the
 *  ways this departs from the MikuPan implementation it started as.
 *
 *  Shape of the thing:
 *
 *    - ONE audio stream, and a mixer.  Every voice is resampled, enveloped and
 *      summed here into a single interleaved block, per core, with a dry and a
 *      wet accumulator; the wet pair goes through iop_reverb.cpp and comes back
 *      scaled by EVOL.  A block is 256 frames, and the loop tops the device up
 *      to about 42 ms every 2 ms.
 *    - The resampler is ours: a 12-bit fractional counter stepped by the voice's
 *      SD_VP_PITCH (0x1000 == 48000 Hz), with 4-point interpolation.
 *    - ADSR runs per OUTPUT sample, in Q16 over the SPU2's 0..0x7fff level.
 *
 *  It used to be one SDL_AudioStream per voice, with SDL summing them and
 *  SDL_SetAudioStreamFrequencyRatio() doing the resampling.  That could not
 *  carry reverb, and the reason is worth keeping: reverb needs a shared send
 *  bus, a bus needs its contributors aligned in output time, and with the
 *  resampling happening inside SDL there was no way to know where a voice's
 *  samples landed.  Each voice also carried its own 0..42 ms decode backlog, so
 *  a bus fed from decode time would have put the tail ahead of its own
 *  transient by a wandering margin.  Shrinking the backlog to hide that just
 *  trades it for underruns.
 *
 *  Three earlier workarounds went away with it, which is the other half of why
 *  this shape is worth the rewrite:
 *
 *    - Channels of one stream no longer need a pairing to stay sample-aligned;
 *      every voice steps off the same block counter, so the drift that made
 *      speech flange cannot happen.  MioPan_VoiceSetStereoPair() survives only
 *      to tie the two channels' lifetimes together.
 *    - The envelope no longer runs at a pitch-dependent rate.  It used to step
 *      once per SOURCE sample, so a voice pitched up ran its ADSR fast.
 *    - There is no queue-depth pacing heuristic per voice, and so no storm of
 *      SDL_GetAudioStreamQueued() calls contending with the mixing thread.
 *
 *  Everything is guarded by one mutex.  Register writes arrive on the IOP's
 *  threads, the mixer walks the same voices, and the ROM polls NAX from a
 *  third -- so the lock is not optional.  The mixer takes the reverb's lock
 *  under it, and never the other way round.
 * ======================================================================== */

#include "iop_voice.h"

#include "libsd.h"
#include "iop_reverb.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* MioPan's scetypes.h has no signed short/int aliases (the ROM never needed
 * them).  The decode and envelope maths below is ported verbatim from MikuPan,
 * which does, so keep its spelling rather than re-typing every expression. */
typedef int16_t s16;
typedef int32_t s32;

/* An ADPCM block: one 2-byte header then 14 bytes of nibbles, 28 samples. */
#define ADPCM_BLOCK_BYTES       16
#define ADPCM_BLOCK_SAMPLES     28

#define VOICE_SAMPLE_RATE       48000

/* One mixed block.  Even, because the reverb runs at half rate and its sample
 * pairs must not straddle a block boundary. */
#define MIX_BLOCK_FRAMES        256

/* Keep about this much stereo PCM on the device -- the same ~42 ms the
 * per-voice queues used to hold, so the lead between a voice's play position
 * and what is audible has not changed. */
#define MIX_TARGET_BLOCKS       8
#define MIX_TARGET_BYTES        (MIX_TARGET_BLOCKS * MIX_BLOCK_FRAMES * 2 * \
                                 (int)sizeof(s16))

/* And no more than this in one pass, so a stall is made up in a couple of
 * passes rather than one enormous one.  42 ms, which is what a single voice
 * could recover per pass under the old per-voice budget.
 *
 * The correctness bound is separate and is enforced by watching
 * MioPan_SdIrqSerial() instead: a pass ends the moment an SPU2 interrupt fires.
 * The mixer holds the voice lock for a whole pass, so until it lets go the IOP
 * cannot service that interrupt and re-arm SD_A_IRQA -- and a voice that
 * crossed a second armed address in the meantime would match nothing at all and
 * simply lose that refill.  Bounding by block count alone cannot express this:
 * how far a voice travels through its buffer depends on its pitch. */
#define MIX_MAX_BLOCKS_PER_PASS 8

#define ADSR_LEVEL_BITS         15
#define ADSR_LEVEL_MAX          ((1 << ADSR_LEVEL_BITS) - 1)
#define ADSR_FP_SHIFT           16
#define ADSR_LEVEL_MAX_FP       ((s32)((long long)ADSR_LEVEL_MAX << ADSR_FP_SHIFT))
#define ADSR_MIN_STEP_FP        (1 << 8)

/* Pitch is 12.12: 0x1000 is one source sample per output sample. */
#define PITCH_ONE               0x1000

typedef enum
{
    VOICE_ADSR_OFF = 0,
    VOICE_ADSR_ATTACK,
    VOICE_ADSR_DECAY,
    VOICE_ADSR_SUSTAIN,
    VOICE_ADSR_RELEASE
} VOICE_ADSR_PHASE;

typedef struct
{
    int      index;                 /* core * 24 + voice */
    int      playing;

    u_int    ssa;                   /* SD_VA_SSA, bytes */
    u_int    lsa;                   /* SD_VA_LSAX, bytes */
    int      lsa_host_set;          /* a host write owns lsa; see FillAdpcmHeader */
    u_int    nax;                   /* SD_VA_NAX, bytes */
    u_short  header;                /* header halfword of the current block   */

    s32      hist1, hist2;          /* ADPCM predictor history */

    s16      block[ADPCM_BLOCK_SAMPLES];
    int      block_pos;             /* next sample; == SAMPLES means spent    */

    s16      interp[4];             /* resampler window, interp[3] newest     */
    u_int    phase;                 /* 0..0xfff between interp[1] and [2]     */

    u_short  voll, volr;
    u_short  pitch;
    u_short  adsr1, adsr2;

    s32      adsr_level;            /* Q16 over 0..0x7fff */
    VOICE_ADSR_PHASE adsr_phase;

    /* Index of the other channel of a multi-channel stream, or -1.  Armed by
     * MioPan_VoiceSetStereoPair() and consumed by the next key-on, so a pairing
     * survives exactly the playback it was declared for and an unrelated SE
     * reusing the slots later cannot inherit it. */
    int      pair;
    int      pair_armed;
} IopVoice;

static IopVoice        voices[IOP_VOICE_NUM];
static SDL_AudioDeviceID audio_dev;
static SDL_AudioStream *out_stream;
static SDL_Mutex      *voice_mutex;
static SDL_Thread     *voice_thread;
static int             voice_quit;
static int             voice_ready;
static u_short         master_voll[IOP_VOICE_CORES];
static u_short         master_volr[IOP_VOICE_CORES];

/* SD_S_VMIXL / VMIXR / VMIXEL / VMIXER, one bit per voice.
 *
 * The dry pair defaults to every voice on.  iopSndInit() sets exactly that and
 * never changes it, so the default only matters if the write were ever missed
 * -- in which case silence would be a far worse failure than an unmodelled
 * mask.  The effect sends default off, which is also what the ROM starts from:
 * EffectMix() builds them a voice at a time out of the sound data. */
static u_int           mix_dry_l[IOP_VOICE_CORES];
static u_int           mix_dry_r[IOP_VOICE_CORES];
static u_int           mix_wet_l[IOP_VOICE_CORES];
static u_int           mix_wet_r[IOP_VOICE_CORES];

/* Per-block accumulators.  The voice loop is the outer one so a voice's gains
 * and masks can be hoisted out of the frame loop; these are what it sums into. */
static s32 acc_dry_l[IOP_VOICE_CORES][MIX_BLOCK_FRAMES];
static s32 acc_dry_r[IOP_VOICE_CORES][MIX_BLOCK_FRAMES];
static s32 acc_wet_l[IOP_VOICE_CORES][MIX_BLOCK_FRAMES];
static s32 acc_wet_r[IOP_VOICE_CORES][MIX_BLOCK_FRAMES];
static s32 acc_rvb_l[IOP_VOICE_CORES][MIX_BLOCK_FRAMES];
static s32 acc_rvb_r[IOP_VOICE_CORES][MIX_BLOCK_FRAMES];
static s16 out_block[MIX_BLOCK_FRAMES * 2];

static void LockVoices(void)   { if (voice_mutex) SDL_LockMutex(voice_mutex); }
static void UnlockVoices(void) { if (voice_mutex) SDL_UnlockMutex(voice_mutex); }

static int VoiceCore(const IopVoice *v)  { return v->index / IOP_VOICE_PER_CORE; }
static int VoiceNumber(const IopVoice *v){ return v->index % IOP_VOICE_PER_CORE; }

/* --------------------------------------------------------------------------
 *  ADPCM
 *
 *  The PlayStation ADPCM block: a 4-bit shift and a 4-bit filter index in the
 *  header, then 28 nibbles each reconstructed against the previous two output
 *  samples.  Straight from MikuPan's mikupan_audio.h.
 * ------------------------------------------------------------------------ */

static const s32 tbl_adpcm_filter[16][2] =
{
    {   0,   0},
    {  60,   0},
    { 115, -52},
    {  98, -55},
    { 122, -60}
};

static s32 ClampS32(s32 val, s32 lo, s32 hi)
{
    return val > hi ? hi : (val < lo ? lo : val);
}

static s16 ClampToS16(s32 value)
{
    if (value > 32767)  return 32767;
    if (value < -32768) return -32768;
    return (s16)value;
}

static void DecodeAdpcmBlock(s16 *out, const u_char *block, s32 *prev1, s32 *prev2)
{
    const s32 header = (s32)(u_short)(block[0] | (block[1] << 8));
    const s32 shift  = (header & 0x0f) + 16;
    const int id     = (header >> 4) & 0x0f;
    const s32 pred1  = tbl_adpcm_filter[id][0];
    const s32 pred2  = tbl_adpcm_filter[id][1];

    const signed char *p   = (const signed char *)&block[2];
    const signed char *end = p + 13;

    for (; p <= end; ++p)
    {
        s32 data = ((s32)(*p) << 28) & (s32)0xf0000000;
        s32 pcm  = (data >> shift) + (((pred1 * *prev1) + (pred2 * *prev2) + 32) >> 6);

        pcm = ClampS32(pcm, -0x8000, 0x7fff);
        *out++ = (s16)pcm;

        data = ((s32)(*p) << 24) & (s32)0xf0000000;
        s32 pcm2 = (data >> shift) + (((pred1 * pcm) + (pred2 * *prev1) + 32) >> 6);

        pcm2 = ClampS32(pcm2, -0x8000, 0x7fff);
        *out++ = (s16)pcm2;

        *prev2 = pcm;
        *prev1 = pcm2;
    }
}

/* The block header carries the loop flags.  Bit 10 (loop start) defines the
 * voice's repeat address -- but only when software has not set LSAX itself.
 *
 * SPU2 latches a host write to LSAX and stops the header bit overriding it,
 * and that latch matters enormously here: iop_stream.c drives a streamed voice
 * by rewriting LSAX every cycle to swing it between the two SPU buffers
 * (StreamVoiceThread, and again on each loop-packet transition).  The stream
 * packets themselves carry loop-start flags, so honouring bit 10 unconditionally
 * -- which is what MikuPan does, since its music streamer does not drive LSAX
 * this way -- overwrites the ROM's loop address with a block inside the buffer
 * that is already playing.  The voice then loops back into the packet it is on
 * and the stream repeats its first packet forever. */
static void FillAdpcmHeader(IopVoice *v)
{
    u_int aligned = v->nax & ~(u_int)(ADPCM_BLOCK_BYTES - 1);
    const u_char *p = (const u_char *)MioPan_SpuRamPointer(aligned, 2);

    if (p == NULL)
    {
        v->header = 0;
        return;
    }

    v->header = (u_short)(p[0] | (p[1] << 8));

    if ((v->header & (1 << 10)) && !v->lsa_host_set)
        v->lsa = aligned;
}

/* --------------------------------------------------------------------------
 *  ADSR
 *
 *  SPU2 steps its envelope off hardware counters; this approximates the
 *  register bit layout and the curve shapes at the host sample rate.
 * ------------------------------------------------------------------------ */

static int  AdsrConfigured(const IopVoice *v)   { return v->adsr1 != 0 || v->adsr2 != 0; }
static int  AdsrAttackRate(const IopVoice *v)   { return (v->adsr1 >> 8) & 0x7f; }
static int  AdsrAttackExp(const IopVoice *v)    { return (v->adsr1 & 0x8000) != 0; }
static int  AdsrDecayRate(const IopVoice *v)    { return (v->adsr1 >> 4) & 0x0f; }
static int  AdsrReleaseRate(const IopVoice *v)  { return v->adsr2 & 0x1f; }
static int  AdsrReleaseExp(const IopVoice *v)   { return (v->adsr2 & 0x20) != 0; }
static int  AdsrSustainRate(const IopVoice *v)  { return (v->adsr2 >> 6) & 0x7f; }
static int  AdsrSustainDecrease(const IopVoice *v) { return (v->adsr2 & 0x2000) != 0; }
static int  AdsrSustainMode(const IopVoice *v)  { return (v->adsr2 >> 14) & 0x03; }

static void BeginVoiceEnd(IopVoice *v);

static s32 AdsrSustainLevel(const IopVoice *v)
{
    const int level = v->adsr1 & 0x0f;
    return (s32)(((long long)(level + 1) * ADSR_LEVEL_MAX_FP) / 16);
}

static s32 ClampAdsrLevel(long long level)
{
    if (level <= 0)                  return 0;
    if (level >= ADSR_LEVEL_MAX_FP)  return ADSR_LEVEL_MAX_FP;
    return (s32)level;
}

static s32 AdsrRateStep(int rate, int max_rate, int fastest_samples)
{
    const int fastness = max_rate - rate + 1;

    long long step = ((long long)fastness * fastness * ADSR_LEVEL_MAX_FP)
                   / ((long long)(max_rate + 1) * (max_rate + 1) * fastest_samples);

    if (step < ADSR_MIN_STEP_FP)
        step = ADSR_MIN_STEP_FP;

    return (s32)step;
}

static s32 AdsrCurvedStep(s32 base_step, s32 level, int increasing)
{
    const s32 curve = increasing ? ADSR_LEVEL_MAX_FP - level : level;
    long long step  = ((long long)base_step * curve) / ADSR_LEVEL_MAX_FP;

    if (step < ADSR_MIN_STEP_FP)
        step = ADSR_MIN_STEP_FP;

    return (s32)step;
}

static void AdvanceVoiceAdsr(IopVoice *v)
{
    s32       step;
    const s32 sustain_level = AdsrSustainLevel(v);

    if (!AdsrConfigured(v))
    {
        v->adsr_level = ADSR_LEVEL_MAX_FP;
        v->adsr_phase = VOICE_ADSR_SUSTAIN;
        return;
    }

    switch (v->adsr_phase)
    {
    case VOICE_ADSR_ATTACK:
        step = AdsrRateStep(AdsrAttackRate(v), 0x7f, 32);
        if (AdsrAttackExp(v))
            step = AdsrCurvedStep(step, v->adsr_level, 1);

        v->adsr_level = ClampAdsrLevel((long long)v->adsr_level + step);
        if (v->adsr_level >= ADSR_LEVEL_MAX_FP)
        {
            v->adsr_level = ADSR_LEVEL_MAX_FP;
            v->adsr_phase = VOICE_ADSR_DECAY;
        }
        break;

    case VOICE_ADSR_DECAY:
        if (v->adsr_level <= sustain_level)
        {
            v->adsr_level = sustain_level;
            v->adsr_phase = VOICE_ADSR_SUSTAIN;
            break;
        }

        step = AdsrRateStep(AdsrDecayRate(v), 0x0f, 64);
        step = AdsrCurvedStep(step, v->adsr_level, 0);
        if (v->adsr_level - sustain_level <= step)
        {
            v->adsr_level = sustain_level;
            v->adsr_phase = VOICE_ADSR_SUSTAIN;
        }
        else
        {
            v->adsr_level -= step;
        }
        break;

    case VOICE_ADSR_SUSTAIN:
        if (AdsrSustainMode(v) == 0)
            break;

        step = AdsrRateStep(AdsrSustainRate(v), 0x7f, 64);
        if (AdsrSustainDecrease(v))
        {
            if (AdsrSustainMode(v) >= 2)
                step = AdsrCurvedStep(step, v->adsr_level, 0);
            v->adsr_level = ClampAdsrLevel((long long)v->adsr_level - step);
        }
        else
        {
            if (AdsrSustainMode(v) >= 2)
                step = AdsrCurvedStep(step, v->adsr_level, 1);
            v->adsr_level = ClampAdsrLevel((long long)v->adsr_level + step);
        }

        if (v->adsr_level == 0)
        {
            v->adsr_phase = VOICE_ADSR_OFF;
            BeginVoiceEnd(v);
        }
        break;

    case VOICE_ADSR_RELEASE:
        step = AdsrRateStep(AdsrReleaseRate(v), 0x1f, 64);
        if (AdsrReleaseExp(v))
            step = AdsrCurvedStep(step, v->adsr_level, 0);

        if (v->adsr_level <= step)
        {
            v->adsr_level = 0;
            v->adsr_phase = VOICE_ADSR_OFF;
            BeginVoiceEnd(v);
        }
        else
        {
            v->adsr_level -= step;
        }
        break;

    case VOICE_ADSR_OFF:
    default:
        v->adsr_level = 0;
        break;
    }
}

/* Reads the level for this sample, then steps the envelope.  Called once per
 * OUTPUT sample, which is where the hardware runs it -- stepping it per source
 * sample, as the per-voice-stream design did, made a pitched-up voice run its
 * envelope fast. */
static s32 GetVoiceAdsrLevel(IopVoice *v)
{
    if (!AdsrConfigured(v))
    {
        v->adsr_level = ADSR_LEVEL_MAX_FP;
        v->adsr_phase = VOICE_ADSR_SUSTAIN;
        return ADSR_LEVEL_MAX;
    }

    if (v->adsr_phase == VOICE_ADSR_OFF)
    {
        v->adsr_level = 0;
        return 0;
    }

    v->adsr_level = ClampAdsrLevel(v->adsr_level);

    const s32 level = v->adsr_level >> ADSR_FP_SHIFT;

    AdvanceVoiceAdsr(v);
    v->adsr_level = ClampAdsrLevel(v->adsr_level);

    return level;
}

/* --------------------------------------------------------------------------
 *  Volume
 * ------------------------------------------------------------------------ */

/* An SPU2 volume word with bit 15 set is a sweep, not a level; the ROM only
 * ever writes plain levels, so treat a sweep as silence rather than reading
 * the sweep parameters as an amplitude. */
static s32 DecodeVolume(u_short val)
{
    if (val & 0x8000)
        return 0;

    return (s32)(val & 0x7fff);
}

static s32 ApplyEnvelope(s32 volume, s32 envelope)
{
    return (s32)(((long long)volume * envelope) / ADSR_LEVEL_MAX);
}

static s32 ApplyVolume(s32 sample, s32 vol)
{
    return (s32)(((long long)sample * vol) >> 15);
}

/* --------------------------------------------------------------------------
 *  Lifetime
 * ------------------------------------------------------------------------ */

static void StopVoicePlayback(IopVoice *v)
{
    if (!v->playing && v->block_pos >= ADPCM_BLOCK_SAMPLES)
        return;

    v->playing   = 0;
    v->block_pos = ADPCM_BLOCK_SAMPLES;     /* drop the rest of the block too */

    /* Channels of one stream share a lifetime.  The alignment reason for
     * pairing is gone -- the mixer steps every voice off the same counter --
     * but this half still matters: a channel left sounding after its partner
     * stopped keeps MioPan_VoiceIsPlaying() answering yes, which pins
     * SD_VA_NAX to a frozen position instead of the loop address.  The ROM then
     * waits forever for the stop block, the stream slot never releases, and the
     * next stream hangs on "StreamAutoIsPreload() Wait Other Stream End".
     * Recursion terminates on the early-out above. */
    if (v->pair >= 0)
        StopVoicePlayback(&voices[v->pair]);
    /* Deliberately does NOT clear `pair`: clearing one side leaves the other
     * still pointing here.  A stale pairing is retired at the next key-on
     * instead -- see KeyOnUnlocked(). */
}

/* The sample has run out.  ENDX latches now -- EndVoiceFindWork() edge-detects
 * it -- but whatever is already mixed stays on the device, so the tail is heard
 * without any of the per-voice drain bookkeeping the old design needed. */
static void BeginVoiceEnd(IopVoice *v)
{
    if (!v->playing)
        return;

    v->playing = 0;

    /* Park the play position on the loop address.  A real SPU2 voice that ends
     * jumps to LSAX and idles there, so NAX reads that address from then on --
     * and iop_snd.c's SetLoopAdrsStopBlock() points a retiring voice's LSAX at
     * the silent stop block precisely so IsInStopBlock() can watch for it.
     *
     * Leaving nax frozen wherever decoding happened to stop instead is fatal:
     * the address never becomes the stop block, IsInStopBlock() never answers
     * yes, and iop_stream.c's release spins on "stop voice wait id N adrs ..."
     * forever.  That wedges the stream slot, so the next stream never starts
     * ("Wait Other Stream End") and its voices are never handed back. */
    v->nax = v->lsa;
    FillAdpcmHeader(v);

    MioPan_SdSetVoiceEnd(VoiceCore(v), VoiceNumber(v));

    /* End the other channel with it, for the reason in StopVoicePlayback().
     * The early-out at the top of this function stops the mutual recursion. */
    if (v->pair >= 0)
        BeginVoiceEnd(&voices[v->pair]);
}

/* --------------------------------------------------------------------------
 *  Decode and resample
 * ------------------------------------------------------------------------ */

/* One block into the voice's own buffer; returns 0 if the voice had to stop. */
static int DecodeVoiceBlock(IopVoice *v)
{
    const u_char *src = (const u_char *)MioPan_SpuRamPointer(v->nax, ADPCM_BLOCK_BYTES);

    if (src == NULL)
    {
        StopVoicePlayback(v);
        return 0;
    }

    DecodeAdpcmBlock(v->block, src, &v->hist1, &v->hist2);
    v->nax += ADPCM_BLOCK_BYTES;

    const int loop_end    = (v->header & (1 << 8)) != 0;
    const int loop_repeat = (v->header & (1 << 9)) != 0;

    if (loop_end)
    {
        v->nax = v->lsa;

        /* ENDX latches on *any* loop-end block, repeating or not -- that is the
         * hardware behaviour, and EndVoiceFindWork() edge-detects it to retire
         * an auto-release voice.  (MikuPan raises it only on the non-repeating
         * case; Fatal Frame parks finished one-shots in a self-repeating stop
         * block, so that would never latch and the voice slot would leak.) */
        MioPan_SdSetVoiceEnd(VoiceCore(v), VoiceNumber(v));

        if (!loop_repeat)
            BeginVoiceEnd(v);
    }

    FillAdpcmHeader(v);

    /* Report the new position before returning: iop_stream.c arms SD_A_IRQA at
     * the buffer it is about to refill and parks until playback crosses it.
     * This runs the ROM's SPU2 handler inline, on this thread, under the voice
     * lock -- as it did before the mixer. */
    MioPan_SdVoiceReachedAddress(VoiceCore(v), v->nax);

    return 1;
}

/* Next decoded source sample, or 0 when the voice has nothing left.  A voice
 * that ended mid-block still plays out the block it was in, which is the tail
 * the old design kept by leaving PCM queued on the voice's own stream. */
static int NextVoiceSample(IopVoice *v, s16 *out)
{
    if (v->block_pos >= ADPCM_BLOCK_SAMPLES)
    {
        if (!v->playing)
            return 0;
        if (!DecodeVoiceBlock(v))
            return 0;

        v->block_pos = 0;
    }

    *out = v->block[v->block_pos++];
    return 1;
}

/* 4-point Catmull-Rom between interp[1] and interp[2].
 *
 * SPU2 interpolates with a 512-entry gaussian FIR held in hardware ROM, which
 * is not something that can be derived; this is the closest practical stand-in
 * and is a good deal gentler than the linear interpolation that would be the
 * obvious alternative.  It replaces SDL's resampler, which was band-limited and
 * better still -- but which ran inside SDL, where the mixer cannot see it. */
static s32 InterpolateSample(const s16 *p, u_int frac)
{
    const float t  = (float)frac * (1.0f / (float)PITCH_ONE);
    const float a0 = (float)p[0];
    const float a1 = (float)p[1];
    const float a2 = (float)p[2];
    const float a3 = (float)p[3];

    const float c1 = 0.5f * (a2 - a0);
    const float c2 = a0 - 2.5f * a1 + 2.0f * a2 - 0.5f * a3;
    const float c3 = 0.5f * (a3 - a0) + 1.5f * (a1 - a2);

    return (s32)(((c3 * t + c2) * t + c1) * t + a1);
}

/* One output sample from `v`, or 0 when it has run dry. */
static int VoiceNextOutputSample(IopVoice *v, s32 *out)
{
    /* Pitch 0 would freeze the voice on one sample for ever, and nothing would
     * ever retire it.  The per-voice-stream code mapped it to 1.0 for the same
     * reason (its ratio guard); keep that. */
    u_int step = (v->pitch != 0) ? (u_int)v->pitch : (u_int)PITCH_ONE;

    while (v->phase >= (u_int)PITCH_ONE)
    {
        s16 s;

        if (!NextVoiceSample(v, &s))
            return 0;

        v->interp[0] = v->interp[1];
        v->interp[1] = v->interp[2];
        v->interp[2] = v->interp[3];
        v->interp[3] = s;

        v->phase -= (u_int)PITCH_ONE;
    }

    *out = InterpolateSample(v->interp, v->phase);
    v->phase += step;

    return 1;
}

static int VoiceSounding(const IopVoice *v)
{
    return v->playing || v->block_pos < ADPCM_BLOCK_SAMPLES;
}

/* --------------------------------------------------------------------------
 *  Auto-DMA (SPU2 "external input")
 *
 *  A voice reads ADPCM out of SPU RAM; this reads 16-bit PCM straight out of
 *  IOP memory and loops over it.  The movie player is the only user -- see
 *  audiodec.c, whose whole pump exists to keep this ring ahead of the read
 *  head, and sceSdBlockTrans() in iop_libsd.cpp, which arms it.
 *
 *  Stereo arrives as alternating 512-byte blocks, left first: that is the
 *  SPU2's own transfer granularity, and the movies' SShd headers report it as
 *  interSize.  One frame is therefore 1024 bytes = 256 samples per channel.
 *
 *  It goes into the core's DRY accumulator and nowhere else.  MMIX says so:
 *  iopSndInit() writes 0x0fc0 / 0x0fcc, which has the external-input dry bits
 *  set and the external-input wet bits clear -- so movie audio takes no reverb.
 *
 *  The read offset advances one sample per output frame, because audiodec.c
 *  subtracts it from its own write cursor: reporting how much has been *pushed*
 *  rather than how much has been *heard* would let the writer lap the reader
 *  and the movie would tear.
 * ------------------------------------------------------------------------ */

#define AUTODMA_BLOCK       512                     /* per channel, per frame */
#define AUTODMA_FRAME       (AUTODMA_BLOCK * 2)
#define AUTODMA_SAMPLES     (AUTODMA_BLOCK / 2)     /* per channel, per frame */

typedef struct
{
    const u_char *ring;
    u_int         size;
    u_int         read_off;         /* start of the frame being consumed     */
    int           frame_pos;        /* sample within that frame              */
    u_int         consumed;         /* bytes of ring mixed since arming      */
    int           armed;
    u_short       voll, volr;
} AutoDma;

static AutoDma auto_dma[IOP_VOICE_CORES];

/* --------------------------------------------------------------------------
 *  The mixer
 * ------------------------------------------------------------------------ */

/* Called from the mixer thread with the voice lock held. */
static void MixVoices(void)
{
    for (int i = 0; i < IOP_VOICE_NUM; i++)
    {
        IopVoice *v = &voices[i];

        if (!VoiceSounding(v))
            continue;

        const int   c   = VoiceCore(v);
        const u_int bit = 1u << VoiceNumber(v);

        /* Hoisted: the ROM can only change these between command frames, and
         * the register write would have to go through the same lock. */
        const s32 vol_l   = DecodeVolume(v->voll);
        const s32 vol_r   = DecodeVolume(v->volr);
        const int dry_l   = (mix_dry_l[c] & bit) != 0;
        const int dry_r   = (mix_dry_r[c] & bit) != 0;
        const int wet_l   = (mix_wet_l[c] & bit) != 0;
        const int wet_r   = (mix_wet_r[c] & bit) != 0;
        for (int f = 0; f < MIX_BLOCK_FRAMES; f++)
        {
            s32 s;

            if (!VoiceNextOutputSample(v, &s))
                break;

            const s32 env = GetVoiceAdsrLevel(v);
            const s32 cl  = ApplyVolume(s, ApplyEnvelope(vol_l, env));
            const s32 cr  = ApplyVolume(s, ApplyEnvelope(vol_r, env));

            if (dry_l) acc_dry_l[c][f] += cl;
            if (dry_r) acc_dry_r[c][f] += cr;
            if (wet_l) acc_wet_l[c][f] += cl;
            if (wet_r) acc_wet_r[c][f] += cr;
        }
    }
}

/* Called from the mixer thread with the voice lock held. */
static void MixAutoDma(void)
{
    for (int c = 0; c < IOP_VOICE_CORES; c++)
    {
        AutoDma *ad = &auto_dma[c];

        if (!ad->armed || ad->ring == NULL || ad->size < AUTODMA_FRAME)
            continue;

        const s32 vl = (s32)ad->voll;
        const s32 vr = (s32)ad->volr;

        for (int f = 0; f < MIX_BLOCK_FRAMES; f++)
        {
            const short *l = (const short *)(ad->ring + ad->read_off);
            const short *r = (const short *)(ad->ring + ad->read_off + AUTODMA_BLOCK);

            acc_dry_l[c][f] += ((s32)l[ad->frame_pos] * vl) >> 15;
            acc_dry_r[c][f] += ((s32)r[ad->frame_pos] * vr) >> 15;

            if (++ad->frame_pos >= AUTODMA_SAMPLES)
            {
                ad->frame_pos = 0;
                ad->read_off  = (ad->read_off + AUTODMA_FRAME) % ad->size;
                ad->consumed += AUTODMA_FRAME;
            }
        }
    }
}

/* The wet return, at the reverb's own half rate.
 *
 * The send is decimated by averaging each sample pair rather than dropping one
 * -- a two-tap box is a poor anti-alias filter but an enormously better one
 * than none -- and the return is held across both frames of the pair.  Neither
 * is what the hardware's resamplers do; the images that leaves sit at the top
 * of the band, where the reverb has almost no energy anyway.
 *
 * Called with the voice lock held; takes the reverb lock under it. */
static void MixReverb(void)
{
    MioPan_ReverbLock();

    for (int c = 0; c < IOP_VOICE_CORES; c++)
    {
        if (!MioPan_ReverbIsActive(c))
        {
            memset(acc_rvb_l[c], 0, sizeof(acc_rvb_l[c]));
            memset(acc_rvb_r[c], 0, sizeof(acc_rvb_r[c]));
            continue;
        }

        for (int f = 0; f < MIX_BLOCK_FRAMES; f += IOP_REVERB_RATE_DIV)
        {
            int wl = 0, wr = 0;

            MioPan_ReverbTick(c,
                              (acc_wet_l[c][f] + acc_wet_l[c][f + 1]) / 2,
                              (acc_wet_r[c][f] + acc_wet_r[c][f + 1]) / 2,
                              &wl, &wr);

            acc_rvb_l[c][f] = acc_rvb_l[c][f + 1] = wl;
            acc_rvb_r[c][f] = acc_rvb_r[c][f + 1] = wr;
        }
    }

    MioPan_ReverbUnlock();
}

/* One 256-frame block: mix, reverberate, and hand it to the device.
 *
 * The master volume is applied per core at the end, where the hardware applies
 * it -- to the dry mix and the wet return together.  The core-1-feeds-core-2
 * chain (SD_P_AVOLL/AVOLR, and MMIX's SIN bits) is deliberately NOT modelled:
 * both cores' output is simply summed.  On hardware core 0 would pass through
 * core 1's master volume as well and end up 6 dB below core 1's own voices;
 * reproducing that would rebalance every sound in the game, which is a separate
 * question from whether reverb exists.
 *
 * Called from the mixer thread with the voice lock held. */
static void MixBlock(void)
{
    memset(acc_dry_l, 0, sizeof(acc_dry_l));
    memset(acc_dry_r, 0, sizeof(acc_dry_r));
    memset(acc_wet_l, 0, sizeof(acc_wet_l));
    memset(acc_wet_r, 0, sizeof(acc_wet_r));

    MixVoices();
    MixAutoDma();
    MixReverb();

    for (int f = 0; f < MIX_BLOCK_FRAMES; f++)
    {
        s32 l = 0;
        s32 r = 0;

        for (int c = 0; c < IOP_VOICE_CORES; c++)
        {
            l += ApplyVolume(acc_dry_l[c][f] + acc_rvb_l[c][f],
                             DecodeVolume(master_voll[c]));
            r += ApplyVolume(acc_dry_r[c][f] + acc_rvb_r[c][f],
                             DecodeVolume(master_volr[c]));
        }

        out_block[f * 2]     = ClampToS16(l);
        out_block[f * 2 + 1] = ClampToS16(r);
    }

    SDL_PutAudioStreamData(out_stream, out_block, (int)sizeof(out_block));
}

static int SDLCALL VoiceThreadMain(void *arg)
{
    (void)arg;

    /* This is an audio thread: if it loses the CPU for longer than the device
     * has PCM queued, playback underruns and the listener hears it.  MioPan's
     * loader and decompression threads are heavy enough during room and scene
     * transitions to starve it at default priority. */
    SDL_SetCurrentThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    while (!voice_quit)
    {
        LockVoices();
        {
            /* One query per pass, not one per voice per block: SDL takes the
             * stream's own lock inside this, and that is the lock its mixing
             * thread needs to pull playable audio. */
            int queued = SDL_GetAudioStreamQueued(out_stream);

            const u_int irq_at_entry = MioPan_SdIrqSerial();

            for (int b = 0; b < MIX_MAX_BLOCKS_PER_PASS &&
                            queued < MIX_TARGET_BYTES; b++)
            {
                MixBlock();
                queued += (int)sizeof(out_block);

                /* An interrupt landed inside that block.  Give the lock back so
                 * the IOP can refill and re-arm before anything advances
                 * further -- see MIX_MAX_BLOCKS_PER_PASS. */
                if (MioPan_SdIrqSerial() != irq_at_entry)
                    break;
            }
        }
        UnlockVoices();

        SDL_Delay(2);
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  Key on / off
 * ------------------------------------------------------------------------ */

static void KeyOnUnlocked(IopVoice *v)
{
    /* No device means no mixer thread, so a voice keyed on here would sit with
     * its play position frozen and the ROM would wait on the stop block for
     * ever.  Refusing to start it leaves MioPan_VoiceIsPlaying() answering no,
     * which is what makes those waits resolve -- the same outcome the old
     * per-voice EnsureVoiceStream() failure produced. */
    if (audio_dev == 0 || out_stream == NULL)
    {
        StopVoicePlayback(v);
        return;
    }

    /* Keep a pairing declared for this playback; drop a stale one. */
    if (v->pair_armed)
        v->pair_armed = 0;
    else
        v->pair = -1;

    v->hist1     = 0;
    v->hist2     = 0;
    v->nax       = v->ssa;
    v->block_pos = ADPCM_BLOCK_SAMPLES;
    v->adsr_level = 0;
    v->adsr_phase = VOICE_ADSR_ATTACK;

    /* Four source samples are pulled in before the first output sample, to
     * prime the interpolation window. */
    memset(v->interp, 0, sizeof(v->interp));
    v->phase = 4u * (u_int)PITCH_ONE;

    v->playing = 1;
    FillAdpcmHeader(v);
}

static void KeyOffUnlocked(IopVoice *v)
{
    /* Without an envelope there is nothing to release into, so the voice just
     * stops -- which is what the ROM expects from a raw REQ_VOICE_STOP.  Unlike
     * the per-voice-stream design this cannot also un-queue what was already
     * mixed, so up to the device's ~42 ms of lead is still heard.  That is the
     * more faithful of the two: hardware cannot retract a sample either. */
    if (!v->playing || !AdsrConfigured(v))
    {
        StopVoicePlayback(v);
        v->adsr_level = 0;
        v->adsr_phase = VOICE_ADSR_OFF;
        return;
    }

    if (v->adsr_level <= 0 || v->adsr_level > ADSR_LEVEL_MAX_FP)
        v->adsr_level = ADSR_LEVEL_MAX_FP;

    v->adsr_phase = VOICE_ADSR_RELEASE;
}

/* --------------------------------------------------------------------------
 *  Public entry points
 * ------------------------------------------------------------------------ */

void MioPan_VoiceInit(void)
{
    if (voice_ready)
        return;

    if (voice_mutex == NULL)
        voice_mutex = SDL_CreateMutex();

    for (int i = 0; i < IOP_VOICE_NUM; i++)
    {
        IopVoice *v = &voices[i];

        memset(v, 0, sizeof(*v));
        v->index     = i;
        v->pair      = -1;
        v->pitch     = PITCH_ONE;
        v->block_pos = ADPCM_BLOCK_SAMPLES;
    }

    for (int c = 0; c < IOP_VOICE_CORES; c++)
    {
        master_voll[c] = 0x3fff;
        master_volr[c] = 0x3fff;
        mix_dry_l[c]   = 0x00ffffff;
        mix_dry_r[c]   = 0x00ffffff;
        mix_wet_l[c]   = 0;
        mix_wet_r[c]   = 0;
    }

    if (!SDL_InitSubSystem(SDL_INIT_AUDIO))
    {
        printf("iop_voice: SDL_INIT_AUDIO failed: %s -- running silent\n", SDL_GetError());
        voice_ready = 1;
        return;
    }

    SDL_AudioSpec spec;
    SDL_zero(spec);
    spec.channels = 2;
    spec.format   = SDL_AUDIO_S16;
    spec.freq     = VOICE_SAMPLE_RATE;

    audio_dev = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec);
    if (audio_dev == 0)
    {
        printf("iop_voice: SDL_OpenAudioDevice failed: %s -- running silent\n", SDL_GetError());
        voice_ready = 1;
        return;
    }

    out_stream = SDL_CreateAudioStream(&spec, &spec);
    if (out_stream == NULL || !SDL_BindAudioStream(audio_dev, out_stream))
    {
        printf("iop_voice: output stream failed: %s -- running silent\n", SDL_GetError());
        if (out_stream != NULL)
        {
            SDL_DestroyAudioStream(out_stream);
            out_stream = NULL;
        }
        SDL_CloseAudioDevice(audio_dev);
        audio_dev   = 0;
        voice_ready = 1;
        return;
    }

    SDL_ResumeAudioDevice(audio_dev);

    voice_quit   = 0;
    voice_thread = SDL_CreateThread(VoiceThreadMain, "iop_voice", NULL);
    if (voice_thread == NULL)
        printf("iop_voice: SDL_CreateThread failed: %s\n", SDL_GetError());

    voice_ready = 1;
    printf("iop_voice: audio device %u open, %d voices, mixed\n",
           (unsigned)audio_dev, IOP_VOICE_NUM);
}

void MioPan_VoiceShutdown(void)
{
    if (!voice_ready)
        return;

    voice_quit = 1;
    if (voice_thread != NULL)
    {
        SDL_WaitThread(voice_thread, NULL);
        voice_thread = NULL;
    }

    LockVoices();
    for (int i = 0; i < IOP_VOICE_NUM; i++)
        voices[i].playing = 0;

    if (out_stream != NULL)
    {
        SDL_DestroyAudioStream(out_stream);
        out_stream = NULL;
    }
    UnlockVoices();

    if (audio_dev != 0)
    {
        SDL_CloseAudioDevice(audio_dev);
        audio_dev = 0;
    }

    voice_ready = 0;
}

static IopVoice *VoiceAt(int core, int voice)
{
    if (core < 0 || core >= IOP_VOICE_CORES ||
        voice < 0 || voice >= IOP_VOICE_PER_CORE)
        return NULL;

    return &voices[core * IOP_VOICE_PER_CORE + voice];
}

void MioPan_VoiceKeyOn(int core, u_int mask)
{
    if (!voice_ready)
        return;

    LockVoices();
    for (int i = 0; i < IOP_VOICE_PER_CORE; i++)
    {
        if ((mask >> i) & 1)
        {
            IopVoice *v = VoiceAt(core, i);
            if (v != NULL)
                KeyOnUnlocked(v);
        }
    }
    UnlockVoices();
}

void MioPan_VoiceKeyOff(int core, u_int mask)
{
    if (!voice_ready)
        return;

    LockVoices();
    for (int i = 0; i < IOP_VOICE_PER_CORE; i++)
    {
        if ((mask >> i) & 1)
        {
            IopVoice *v = VoiceAt(core, i);
            if (v != NULL)
                KeyOffUnlocked(v);
        }
    }
    UnlockVoices();
}

void MioPan_VoiceSetStartAddr(int core, int voice, u_int adrs)
{
    if (!voice_ready)
        return;

    LockVoices();
    IopVoice *v = VoiceAt(core, voice);
    if (v != NULL)
        v->ssa = adrs;
    UnlockVoices();
}

void MioPan_VoiceSetLoopAddr(int core, int voice, u_int adrs)
{
    if (!voice_ready)
        return;

    LockVoices();
    IopVoice *v = VoiceAt(core, voice);
    if (v != NULL)
    {
        v->lsa          = adrs;
        v->lsa_host_set = 1;        /* from here the header bit must not win */
    }
    UnlockVoices();
}

void MioPan_VoiceSetParam(int core, int voice, int reg, u_short value)
{
    if (!voice_ready)
        return;

    LockVoices();
    IopVoice *v = VoiceAt(core, voice);
    if (v != NULL)
    {
        switch (reg)
        {
        case SD_VP_VOLL  >> 8: v->voll  = value; break;
        case SD_VP_VOLR  >> 8: v->volr  = value; break;
        case SD_VP_PITCH >> 8: v->pitch = value; break;
        case SD_VP_ADSR1 >> 8: v->adsr1 = value; break;
        case SD_VP_ADSR2 >> 8: v->adsr2 = value; break;
        default:                                break;
        }
    }
    UnlockVoices();
}

/* SD_S_VMIXL / VMIXR / VMIXEL / VMIXER for one core, as iop_libsd.cpp holds
 * them.  The dry pair decides what reaches the output; the effect pair decides
 * what is additionally sent to the reverb, which is EffectMix()'s business and
 * ultimately the `effect` bit in the sound data. */
void MioPan_VoiceSetMixMasks(int core, u_int dry_l, u_int dry_r,
                             u_int wet_l, u_int wet_r)
{
    if (core < 0 || core >= IOP_VOICE_CORES)
        return;

    LockVoices();
    mix_dry_l[core] = dry_l;
    mix_dry_r[core] = dry_r;
    mix_wet_l[core] = wet_l;
    mix_wet_r[core] = wet_r;
    UnlockVoices();
}

/* Declare two voices to be channels of one multi-channel stream.
 *
 * This used to be what kept the pair sample-aligned, and getting it wrong was
 * audible as flanging on speech.  The mixer aligns every voice by construction,
 * so what is left is the lifetime tie: the two channels stop together, which is
 * what keeps a surviving channel from pinning SD_VA_NAX and wedging the stream
 * release.  iop_stream.c's StreamPlay() still calls it -- a PORT: line -- and
 * should keep doing so.  Retired at the next key-on. */
void MioPan_VoiceSetStereoPair(int core0, int voice0, int core1, int voice1)
{
    if (!voice_ready)
        return;

    LockVoices();
    IopVoice *a = VoiceAt(core0, voice0);
    IopVoice *b = VoiceAt(core1, voice1);

    if (a != NULL && b != NULL && a != b)
    {
        a->pair = b->index;
        b->pair = a->index;
        a->pair_armed = 1;
        b->pair_armed = 1;
    }
    UnlockVoices();
}

void MioPan_VoiceSetMasterVolume(int core, u_short voll, u_short volr)
{
    if (core < 0 || core >= IOP_VOICE_CORES)
        return;

    master_voll[core] = voll;
    master_volr[core] = volr;
}

u_int MioPan_VoiceGetNowAddr(int core, int voice)
{
    if (!voice_ready)
        return 0;

    LockVoices();
    IopVoice *v = VoiceAt(core, voice);
    u_int adrs = (v != NULL) ? v->nax : 0;
    UnlockVoices();

    return adrs;
}

/* "Still advancing its own play position", NOT "still making sound".
 *
 * A voice that has ended but whose final block is still being mixed out is
 * deliberately reported as not playing.  Its nax has stopped moving, and
 * sceSdGetAddr()'s SD_VA_NAX case uses this to decide whether to report the
 * engine's position or the voice's current loop address.  An ended SPU2 voice
 * idles on LSAX and so follows a *later* LSAX write -- which is exactly what
 * iop_stream.c relies on when it stops a stream: the voice ends first, and only
 * then does SetLoopAdrsStopBlock() move LSAX to the stop block for
 * IsInStopBlock() to find.  Answering "playing" here pins NAX to a stale
 * snapshot taken before that write, the stop block never arrives, and the
 * release spins forever on "stop voice wait id N adrs ...". */
int MioPan_VoiceIsPlaying(int core, int voice)
{
    if (!voice_ready)
        return 0;

    LockVoices();
    IopVoice *v = VoiceAt(core, voice);
    int playing = (v != NULL) ? v->playing : 0;
    UnlockVoices();

    return playing;
}

void MioPan_VoiceAutoDmaStart(int core, void *ring, u_int size, u_int start_off)
{
    if (core < 0 || core >= IOP_VOICE_CORES || ring == NULL ||
        size < AUTODMA_FRAME)
        return;

    LockVoices();
    {
        AutoDma *ad = &auto_dma[core];

        ad->ring      = (const u_char *)ring;
        ad->size      = size - (size % AUTODMA_FRAME);
        ad->read_off  = (ad->size != 0) ? (start_off % ad->size) : 0;
        ad->read_off -= ad->read_off % AUTODMA_FRAME;
        ad->frame_pos = 0;
        ad->consumed  = 0;
        ad->armed     = 1;
        /* No default level: audioDecResume() writes BVOLL/BVOLR immediately
         * before arming, and inventing one here would make a movie the game
         * has muted audible. */
    }
    UnlockVoices();
}

void MioPan_VoiceAutoDmaStop(int core)
{
    if (core < 0 || core >= IOP_VOICE_CORES)
        return;

    LockVoices();
    auto_dma[core].armed = 0;
    UnlockVoices();
}

u_int MioPan_VoiceAutoDmaGetOffset(int core)
{
    u_int off = 0;

    if (core < 0 || core >= IOP_VOICE_CORES)
        return 0;

    LockVoices();
    {
        AutoDma *ad = &auto_dma[core];

        if (ad->size != 0)
        {
            /* What has been heard = what the mixer has consumed, less what is
             * still queued on the device.  One output frame is four bytes of
             * the device queue and four bytes of the ring alike, so the queue
             * depth converts one for one. */
            int queued = (out_stream != NULL)
                       ? SDL_GetAudioStreamQueued(out_stream) : 0;
            u_int behind = (queued > 0) ? (u_int)queued : 0u;

            if (behind > ad->consumed)
                behind = ad->consumed;

            off = (ad->read_off + ad->size - (behind % ad->size)) % ad->size;
        }
    }
    UnlockVoices();

    return off;
}

void MioPan_VoiceAutoDmaSetVolume(int core, u_short voll, u_short volr)
{
    if (core < 0 || core >= IOP_VOICE_CORES)
        return;

    LockVoices();
    auto_dma[core].voll = voll;
    auto_dma[core].volr = volr;
    UnlockVoices();
}
