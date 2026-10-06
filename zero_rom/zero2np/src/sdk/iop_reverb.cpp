/* ==========================================================================
 *  iop_reverb.cpp  (SPU2 reverb — PC-port implementation)
 *
 *  See iop_reverb.h for what this is and why it was missing.
 *
 *  The network is the PlayStation reverb, unchanged on SPU2 apart from the
 *  clock: two IIR "reflection" stages (same-side and cross-side), a four-tap
 *  comb early echo, and two all-pass sections, all reading and writing one
 *  circular work area that advances by a single sample per tick.
 *
 *  Three things about the numbers are worth knowing before touching them:
 *
 *    - The address registers count EIGHT BYTES, not one.  A preset's taps are
 *      therefore reg * 4 samples, and psx-spx's [mLSAME-2] -- a byte offset --
 *      is one sample back.  The conversion is not cosmetic: taken at face
 *      value a preset would use a quarter of the buffer the game allocates.
 *    - The presets are verifiable against the ROM.  eff_use_size_tbl[] in the
 *      EE's snd.c lists all ten work-area sizes, and for seven of the ten the
 *      longest tap times eight lands within 0x20 bytes of the ROM's own
 *      figure.  That is what pins mode 3 to Studio Medium.
 *    - Volumes are signed Q15, so 0x8000 is -1.0 and not +1.0.  vLIN/vRIN are
 *      0x8000 in every preset: the send is phase-inverted going in, which is
 *      inaudible on its own and matters only if you "fix" it.
 *
 *  Deviations from hardware, both in the resampling rather than the network:
 *  the 48 kHz send is decimated to the reverb's 24 kHz by averaging sample
 *  pairs, and the return is held for two output frames rather than filtered
 *  back up.  The images that leaves sit at the top of the band.
 * ======================================================================== */

#include "iop_reverb.h"

#include "libsd.h"                  /* MioPan_SpuRamPointer */

#include <SDL3/SDL_mutex.h>

#include <string.h>

typedef short s16;
typedef int   s32;

/* --------------------------------------------------------------------------
 *  Presets
 *
 *  Register order is the hardware's, rev00..rev1F.  Indices into each row are
 *  named by RVB_* below so the network reads like the published formula.
 * ------------------------------------------------------------------------ */

enum
{
    RVB_dAPF1 = 0, RVB_dAPF2,
    RVB_vIIR,
    RVB_vCOMB1, RVB_vCOMB2, RVB_vCOMB3, RVB_vCOMB4,
    RVB_vWALL, RVB_vAPF1, RVB_vAPF2,
    RVB_mLSAME,  RVB_mRSAME,
    RVB_mLCOMB1, RVB_mRCOMB1, RVB_mLCOMB2, RVB_mRCOMB2,
    RVB_dLSAME,  RVB_dRSAME,
    RVB_mLDIFF,  RVB_mRDIFF,
    RVB_mLCOMB3, RVB_mRCOMB3, RVB_mLCOMB4, RVB_mRCOMB4,
    RVB_dLDIFF,  RVB_dRDIFF,
    RVB_mLAPF1,  RVB_mRAPF1,  RVB_mLAPF2,  RVB_mRAPF2,
    RVB_vLIN,    RVB_vRIN,
    RVB_REG_NUM
};

#define REVERB_MODE_NUM 10

/* Indexed by the mode SndSetEffect() passes, which is the ROM's own ordering:
 * off, room, studio A/B/C, hall, space, echo, delay, pipe. */
static const u_short reverb_preset[REVERB_MODE_NUM][RVB_REG_NUM] =
{
    /* 0: off */
    { 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
      0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001, 0x0001,
      0x0000, 0x0000, 0x0001, 0x0001, 0x0001, 0x0001, 0x0000, 0x0000 },
    /* 1: room */
    { 0x007D, 0x005B, 0x6D80, 0x54B8, 0xBED0, 0x0000, 0x0000, 0xBA80,
      0x5800, 0x5300, 0x04D6, 0x0333, 0x03F0, 0x0227, 0x0374, 0x01EF,
      0x0334, 0x01B5, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x01B4, 0x0136, 0x00B8, 0x005C, 0x8000, 0x8000 },
    /* 2: studio A (small) */
    { 0x0033, 0x0025, 0x70F0, 0x4FA8, 0xBCE0, 0x4410, 0xC0F0, 0x9C00,
      0x5280, 0x4EC0, 0x03E4, 0x031B, 0x03A4, 0x02AF, 0x0372, 0x0266,
      0x031C, 0x025D, 0x025C, 0x018E, 0x022F, 0x0135, 0x01D2, 0x00B7,
      0x018F, 0x00B5, 0x00B4, 0x0080, 0x004C, 0x0026, 0x8000, 0x8000 },
    /* 3: studio B (medium) -- the only preset Fatal Frame ever selects */
    { 0x00B1, 0x007F, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xB4C0,
      0x5280, 0x4EC0, 0x0904, 0x076B, 0x0824, 0x065F, 0x07A2, 0x0616,
      0x076C, 0x05ED, 0x05EC, 0x042E, 0x050F, 0x0305, 0x0462, 0x02B7,
      0x042F, 0x0265, 0x0264, 0x01B2, 0x0100, 0x0080, 0x8000, 0x8000 },
    /* 4: studio C (large) */
    { 0x00E3, 0x00A9, 0x6F60, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0xA680,
      0x5680, 0x52C0, 0x0DFB, 0x0B58, 0x0D09, 0x0A3C, 0x0BD9, 0x0973,
      0x0B59, 0x08DA, 0x08D9, 0x05E9, 0x07EC, 0x04B0, 0x06EF, 0x03D2,
      0x05EA, 0x031D, 0x031C, 0x0238, 0x0154, 0x00AA, 0x8000, 0x8000 },
    /* 5: hall */
    { 0x01A5, 0x0139, 0x6000, 0x5000, 0x4C00, 0xB800, 0xBC00, 0xC000,
      0x6000, 0x5C00, 0x15BA, 0x11BB, 0x14C2, 0x10BD, 0x11BC, 0x0DC1,
      0x11C0, 0x0DC3, 0x0DC0, 0x09C1, 0x0BC4, 0x07C1, 0x0A00, 0x06CD,
      0x09C2, 0x05C1, 0x05C0, 0x041A, 0x0274, 0x013A, 0x8000, 0x8000 },
    /* 6: space echo */
    { 0x033D, 0x0231, 0x7E00, 0x5000, 0xB400, 0xB000, 0x4C00, 0xB000,
      0x6000, 0x5400, 0x1ED6, 0x1A31, 0x1D14, 0x183B, 0x1BC2, 0x16B2,
      0x1A32, 0x15EF, 0x15EE, 0x1055, 0x1334, 0x0F2D, 0x11F6, 0x0C5D,
      0x1056, 0x0AE1, 0x0AE0, 0x07A2, 0x0464, 0x0232, 0x8000, 0x8000 },
    /* 7: echo (chaos echo) */
    { 0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x8100,
      0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
      0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000 },
    /* 8: delay */
    { 0x0001, 0x0001, 0x7FFF, 0x7FFF, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x1FFF, 0x0FFF, 0x1005, 0x0005, 0x0000, 0x0000,
      0x1005, 0x0005, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000, 0x0000,
      0x0000, 0x0000, 0x1004, 0x1002, 0x0004, 0x0002, 0x8000, 0x8000 },
    /* 9: pipe (half echo) */
    { 0x0017, 0x0013, 0x70F0, 0x4FA8, 0xBCE0, 0x4510, 0xBEF0, 0x8500,
      0x5F80, 0x54C0, 0x0371, 0x02AF, 0x02E5, 0x01DF, 0x02B0, 0x01D7,
      0x0358, 0x026A, 0x01D6, 0x011E, 0x012D, 0x00B1, 0x011F, 0x0059,
      0x01A0, 0x00E3, 0x0058, 0x0040, 0x0028, 0x0014, 0x8000, 0x8000 }
};

/* Work-area size per mode, in bytes.  Identical to eff_use_size_tbl[] on the
 * EE side -- which is the point: the EE allocates from that table and hands
 * the result over as SD_A_EEA, so the two must agree or the buffer this reads
 * is not the buffer the game reserved. */
static const u_int reverb_size[REVERB_MODE_NUM] =
{
    0x00000080, 0x000026c0, 0x00001f40, 0x00004840, 0x00006fe0,
    0x0000ade0, 0x0000f6c0, 0x00018040, 0x00018040, 0x00003c00
};

/* --------------------------------------------------------------------------
 *  State
 * ------------------------------------------------------------------------ */

typedef struct
{
    int    enabled;                 /* SD_C_EFFECT_ENABLE                   */

    s16   *buf;                     /* into sd_ram, or NULL if EEA was bad  */
    u_int  samples;                 /* work area length, in samples         */
    u_int  cursor;                  /* moving base, in samples              */

    s16    evol_l, evol_r;          /* SD_P_EVOLL / SD_P_EVOLR              */

    s32    v[RVB_REG_NUM];          /* volumes as Q15, offsets as samples   */
} IopReverb;

static IopReverb  rvb[IOP_REVERB_CORES];
static SDL_Mutex *rvb_mutex;

void MioPan_ReverbLock(void)   { if (rvb_mutex) SDL_LockMutex(rvb_mutex); }
void MioPan_ReverbUnlock(void) { if (rvb_mutex) SDL_UnlockMutex(rvb_mutex); }

void MioPan_ReverbInit(void)
{
    if (rvb_mutex == NULL)
        rvb_mutex = SDL_CreateMutex();

    MioPan_ReverbLock();
    memset(rvb, 0, sizeof(rvb));
    MioPan_ReverbUnlock();
}

/* --------------------------------------------------------------------------
 *  Fixed point
 * ------------------------------------------------------------------------ */

static s32 MulVol(s32 sample, s32 vol)
{
    return (s32)(((long long)sample * vol) >> 15);
}

static s16 ClampToS16(s32 v)
{
    if (v >  32767) return  32767;
    if (v < -32768) return -32768;
    return (s16)v;
}

/* The offset is in samples and may be negative -- the formula's "-2" byte
 * steps, and mLAPF1-dAPF1 -- so the wrap has to be done on a signed value. */
static u_int RvbWrap(const IopReverb *r, s32 off)
{
    s32 pos = (s32)r->cursor + off;

    pos %= (s32)r->samples;
    if (pos < 0)
        pos += (s32)r->samples;

    return (u_int)pos;
}

static s32 RvbRead(const IopReverb *r, s32 off)
{
    return r->buf[RvbWrap(r, off)];
}

static void RvbWrite(IopReverb *r, s32 off, s32 value)
{
    r->buf[RvbWrap(r, off)] = ClampToS16(value);
}

/* --------------------------------------------------------------------------
 *  Configuration
 * ------------------------------------------------------------------------ */

void MioPan_ReverbSetPreset(int core, int mode, u_int eea)
{
    if (core < 0 || core >= IOP_REVERB_CORES)
        return;
    if (mode < 0 || mode >= REVERB_MODE_NUM)
        return;

    MioPan_ReverbLock();
    {
        IopReverb  *r    = &rvb[core];
        const u_int size = reverb_size[mode];

        r->buf     = NULL;
        r->samples = 0;
        r->cursor  = 0;

        /* EEA is the last byte of the area, so the base is one past the end
         * minus the preset's size.  A mode with no network (0) or an EEA the
         * EE never managed to allocate leaves the core inactive rather than
         * scribbling over sample data somewhere else in SPU RAM. */
        if (mode != 0 && eea + 1 >= size)
        {
            const u_int base = eea + 1 - size;
            void       *mem  = MioPan_SpuRamPointer(base, size);

            if (mem != NULL)
            {
                r->buf     = (s16 *)mem;
                r->samples = size / (u_int)sizeof(s16);

                /* The authoritative clear.  sceSdClearEffectWorkArea() runs
                 * before the new EEA is written, so it cannot know this
                 * extent; whatever the previous preset left behind would
                 * otherwise decay out of the new network as a burst. */
                memset(r->buf, 0, (size_t)size);
            }
        }

        for (int i = 0; i < RVB_REG_NUM; i++)
            r->v[i] = (s32)(s16)reverb_preset[mode][i];

        /* Volumes stay as the signed Q15 they already are; the address
         * registers become sample offsets.  Everything from RVB_mLSAME to
         * RVB_mRAPF2 is an address, and so are the two APF displacements. */
        for (int i = RVB_mLSAME; i <= RVB_mRAPF2; i++)
            r->v[i] = (s32)reverb_preset[mode][i] * 4;

        r->v[RVB_dAPF1] = (s32)reverb_preset[mode][RVB_dAPF1] * 4;
        r->v[RVB_dAPF2] = (s32)reverb_preset[mode][RVB_dAPF2] * 4;
    }
    MioPan_ReverbUnlock();
}

void MioPan_ReverbSetDepth(int core, u_short evoll, u_short evolr)
{
    if (core < 0 || core >= IOP_REVERB_CORES)
        return;

    MioPan_ReverbLock();
    rvb[core].evol_l = (s16)evoll;
    rvb[core].evol_r = (s16)evolr;
    MioPan_ReverbUnlock();
}

void MioPan_ReverbSetEnable(int core, int on)
{
    if (core < 0 || core >= IOP_REVERB_CORES)
        return;

    MioPan_ReverbLock();
    rvb[core].enabled = on ? 1 : 0;
    MioPan_ReverbUnlock();
}

void MioPan_ReverbClearWorkArea(int core)
{
    if (core < 0 || core >= IOP_REVERB_CORES)
        return;

    MioPan_ReverbLock();
    {
        IopReverb *r = &rvb[core];

        if (r->buf != NULL)
            memset(r->buf, 0, (size_t)r->samples * sizeof(s16));

        r->cursor = 0;
    }
    MioPan_ReverbUnlock();
}

int MioPan_ReverbIsActive(int core)
{
    int active;

    if (core < 0 || core >= IOP_REVERB_CORES)
        return 0;

    MioPan_ReverbLock();
    {
        const IopReverb *r = &rvb[core];

        active = r->enabled && r->buf != NULL && r->samples > 0 &&
                 (r->evol_l != 0 || r->evol_r != 0);
    }
    MioPan_ReverbUnlock();

    return active;
}

/* --------------------------------------------------------------------------
 *  The network
 * ------------------------------------------------------------------------ */

void MioPan_ReverbTick(int core, int in_l, int in_r, int *out_l, int *out_r)
{
    *out_l = 0;
    *out_r = 0;

    if (core < 0 || core >= IOP_REVERB_CORES)
        return;

    IopReverb *r = &rvb[core];

    if (!r->enabled || r->buf == NULL || r->samples == 0)
        return;

    const s32 *v = r->v;

    /* The send bus carries the sum of every voice routed to it, so it can
     * exceed one sample's worth.  Hardware clamps at the mixer; clamping here
     * keeps a loud scene from driving the IIR stages into their own limits and
     * ringing. */
    const s32 Lin = MulVol(ClampToS16(in_l), v[RVB_vLIN]);
    const s32 Rin = MulVol(ClampToS16(in_r), v[RVB_vRIN]);

    /* Same-side reflection: left feeds left, right feeds right.  The -1 is
     * psx-spx's [mLSAME-2], one sample back. */
    {
        const s32 prev_l = RvbRead(r, v[RVB_mLSAME] - 1);
        const s32 prev_r = RvbRead(r, v[RVB_mRSAME] - 1);

        RvbWrite(r, v[RVB_mLSAME],
                 MulVol(Lin + MulVol(RvbRead(r, v[RVB_dLSAME]), v[RVB_vWALL]) - prev_l,
                        v[RVB_vIIR]) + prev_l);
        RvbWrite(r, v[RVB_mRSAME],
                 MulVol(Rin + MulVol(RvbRead(r, v[RVB_dRSAME]), v[RVB_vWALL]) - prev_r,
                        v[RVB_vIIR]) + prev_r);
    }

    /* Cross-side reflection: the left store is fed from the right delay and
     * vice versa, which is what widens the tail. */
    {
        const s32 prev_l = RvbRead(r, v[RVB_mLDIFF] - 1);
        const s32 prev_r = RvbRead(r, v[RVB_mRDIFF] - 1);

        RvbWrite(r, v[RVB_mLDIFF],
                 MulVol(Lin + MulVol(RvbRead(r, v[RVB_dRDIFF]), v[RVB_vWALL]) - prev_l,
                        v[RVB_vIIR]) + prev_l);
        RvbWrite(r, v[RVB_mRDIFF],
                 MulVol(Rin + MulVol(RvbRead(r, v[RVB_dLDIFF]), v[RVB_vWALL]) - prev_r,
                        v[RVB_vIIR]) + prev_r);
    }

    /* Early echo. */
    s32 Lout = MulVol(RvbRead(r, v[RVB_mLCOMB1]), v[RVB_vCOMB1])
             + MulVol(RvbRead(r, v[RVB_mLCOMB2]), v[RVB_vCOMB2])
             + MulVol(RvbRead(r, v[RVB_mLCOMB3]), v[RVB_vCOMB3])
             + MulVol(RvbRead(r, v[RVB_mLCOMB4]), v[RVB_vCOMB4]);
    s32 Rout = MulVol(RvbRead(r, v[RVB_mRCOMB1]), v[RVB_vCOMB1])
             + MulVol(RvbRead(r, v[RVB_mRCOMB2]), v[RVB_vCOMB2])
             + MulVol(RvbRead(r, v[RVB_mRCOMB3]), v[RVB_vCOMB3])
             + MulVol(RvbRead(r, v[RVB_mRCOMB4]), v[RVB_vCOMB4]);

    /* Two all-pass sections.  Each reads its delayed tap ONCE: the formula
     * spells [mLAPF1-dAPF1] twice, but the write to [mLAPF1] in between cannot
     * change it unless dAPF1 is zero, and reading the pre-write value back
     * afterwards is what makes the section all-pass rather than a comb. */
    {
        const s32 tap_l = RvbRead(r, v[RVB_mLAPF1] - v[RVB_dAPF1]);
        const s32 tap_r = RvbRead(r, v[RVB_mRAPF1] - v[RVB_dAPF1]);

        Lout -= MulVol(tap_l, v[RVB_vAPF1]);
        Rout -= MulVol(tap_r, v[RVB_vAPF1]);

        RvbWrite(r, v[RVB_mLAPF1], Lout);
        RvbWrite(r, v[RVB_mRAPF1], Rout);

        Lout = MulVol(Lout, v[RVB_vAPF1]) + tap_l;
        Rout = MulVol(Rout, v[RVB_vAPF1]) + tap_r;
    }

    {
        const s32 tap_l = RvbRead(r, v[RVB_mLAPF2] - v[RVB_dAPF2]);
        const s32 tap_r = RvbRead(r, v[RVB_mRAPF2] - v[RVB_dAPF2]);

        Lout -= MulVol(tap_l, v[RVB_vAPF2]);
        Rout -= MulVol(tap_r, v[RVB_vAPF2]);

        RvbWrite(r, v[RVB_mLAPF2], Lout);
        RvbWrite(r, v[RVB_mRAPF2], Rout);

        Lout = MulVol(Lout, v[RVB_vAPF2]) + tap_l;
        Rout = MulVol(Rout, v[RVB_vAPF2]) + tap_r;
    }

    /* EVOL is the return level, and the only thing map_reverb.c varies from
     * one area to the next. */
    *out_l = MulVol(Lout, r->evol_l);
    *out_r = MulVol(Rout, r->evol_r);

    r->cursor++;
    if (r->cursor >= r->samples)
        r->cursor = 0;
}
