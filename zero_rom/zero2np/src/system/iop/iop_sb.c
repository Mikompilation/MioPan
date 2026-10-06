/* ==========================================================================
 *  system/iop/iop_sb.c
 *
 *  Sound-buffer commands.  This is the receiving end of the EE's
 *  snd_buffer.c: six handlers, one per REQ_SB_* command, each of which only
 *  unpacks its payload onto iop_snd.c's voice primitives.  All the state --
 *  which voice is free, what is fading, when something ended -- lives on the
 *  EE; the IOP just does as it is told.
 *
 *  Note the two argument conventions in play.  The MyOnVoice, MyOffVoice and
 *  VolSet families take
 *  `core` and `voice_no` separately, while PitchSet() and AdsrSet() take the
 *  packed key `(voice_no << 1) | core` -- `voice_shift`/`set` in the ROM's
 *  own locals.  Getting those the wrong way round addresses the wrong voice
 *  silently.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 *  No source line numbers are available for this module, so there are no
 *  trailing ROM-line annotations.
 * ======================================================================== */

#include "iop_sb.h"

#include "iop_snd.h"

void SoundBufferPlay(SOUND_BUF_PLAY *sbp)
{
    int            voice_no;
    int            core;
    unsigned short set;
    int            voice_shift;

    voice_no = sbp->voice_no;
    core     = sbp->attr.core;

    /* Direct rather than ramped: a voice that is about to be keyed on has no
     * previous level to ramp from. */
    VolSetDirect(core, voice_no, sbp->vol);

    /* Packed through a u16, which is where the ROM's `<< 1` picks up its
     * 0x7fff mask -- the shift is truncated to 16 bits before `core` goes in. */
    set         = (unsigned short)voice_no;
    voice_shift = (unsigned short)(set << 1) | core;

    PitchSet(voice_shift, sbp->pitch);
    AdsrSet(voice_shift, sbp->adsr1, sbp->adsr2);

    EffectMix(core, 1 << voice_no, sbp->attr.effect);

    /* A looping sample has to be stopped explicitly, so it must not arm the
     * auto-release; a one-shot retires itself at its end point, which is what
     * the EE's CheckEndPointThrough() later reports. */
    if (sbp->attr.loop)
        MyOnVoice(core, voice_no, sbp->adrs);
    else
        MyOnVoiceAutoRelease(core, voice_no, sbp->adrs);
}

void SoundBufferStop(SOUND_BUF_STOP *sbp)
{
    int voice_no;
    int core;

    voice_no = sbp->voice_no;
    core     = sbp->core;

    MyOffVoice(core, voice_no);

    /* Park the loop point on the silent block, so a voice that keeps running
     * past the key-off never reads sample memory that has been handed back. */
    SetLoopAdrsStopBlock(core, voice_no);
}

void SoundBufferPause(SOUND_BUF_PAUSE *sbp)
{
    MyPauseVoice(sbp->core, sbp->voice_no);
}

void SoundBufferRestart(SOUND_BUF_RESTART *sbp)
{
    MyRestartVoice(sbp->core, sbp->voice_no);
}

void SoundBufferSetVol(SOUND_BUF_SETVOL *sbp)
{
    /* Ramped, unlike the Play() path. */
    VolSet(sbp->core, sbp->voice_no, sbp->vol);
}

void SoundBufferSetPitch(SOUND_BUF_SETPITCH *sbp)
{
    unsigned short set;

    set = (unsigned short)(((sbp->voice_no << 1) | sbp->core) & 0xffff);

    PitchSet(set, sbp->pitch);
}
