/* ==========================================================================
 *  common/variable.h
 *
 *  The engine-wide shared globals.  This header is included almost everywhere
 *  in the game (it is one of the most-referenced headers in the build): it
 *  declares the handful of top-level work blocks that every subsystem reads and
 *  writes, together with the struct types that back them.  The single set of
 *  definitions lives in main/glob.c; every other translation unit sees them
 *  through the `extern` declarations below.
 *
 *    pad[2]          - the two controller-port pad states (raw + edge/repeat).
 *    key_now/key_bak - per-logical-button pointers into the current / previous
 *                      frame's pad data (indexed 0..31; NULL when unmapped).
 *    sys_wrk         - top-level system work (frame counter, RTC snapshot,
 *                      soft-reset state, video mode, language, game mode).
 *    opt_wrk         - user options (brightness, volume, vibration, ...).
 *    cam_custom_wrk  - the (photo) camera upgrade / charge state.
 *    debug_wrk       - debug-menu state (mode, cursor, per-tool init flags).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_VARIABLE_H
#define _COMMON_VARIABLE_H

#include <stdint.h>                 /* intptr_t (SISALG_WRK script cursors) */

#include "../sdk/scetypes.h"        /* u_char / u_short / u_int / u_long / u_long128 */
#include "../sdk/libcdvd.h"         /* sceCdCLOCK (embedded by value in SYS_WRK) */
#include "../graphics/graph3d/g3dLight.h" /* G3DLIGHT (PLYR_WRK flashlight fields) */
#include "../system/eeiop/snd3d.h"        /* SND_3D_SET (PLYR_WRK listener data) */
#include "../system/eeiop/snd_buffer.h"   /* CPLYR_SND_BUF_PLAY (death cry)      */
#include "utility2.h"                     /* PRINT_ASSERT (BIT_FLAGS range checks) */

/* --------------------------------------------------------------------------
 *  BIT_FLAGS<N> -- packed bit set over ceil(N/32) words.
 *
 *  Fully inline, so it is emitted into every translation unit that uses it;
 *  the ROM's assert banners for the range checks name this header, which is
 *  how the template is placed here rather than in the using file.  Every
 *  accessor bounds-checks and then proceeds anyway -- the assert does not
 *  return, but it also does not guard the access, matching the ROM.
 * ------------------------------------------------------------------------ */
template <int N>
struct BIT_FLAGS                    /* ceil(N/32) * 4 bytes */
{
protected:
    /* 0x0 */ int flag_32[(N + 31) / 32];

public:
    /* The ROM has a default constructor: photo_dat.o's
     * __static_initialization_and_destruction_0() runs AllDown() over
     * photo_dat_save at that object's definition line, which is where the
     * file's single .ctors entry comes from. */
    BIT_FLAGS(void) { AllDown(); }

    void AllDown(void)
    {
        for (int i = 0; i < (N + 31) / 32; i++) {                                /* 800 */
            flag_32[i] = 0;                                                      /* 801 */
        }
    }

    void AllUp(void)
    {
        for (int i = 0; i < (N + 31) / 32; i++) {                                /* 808 */
            flag_32[i] = -1;                                                     /* 809 */
        }
    }

    /* The word / bit split is two named locals in the ROM, not the folded
     * `no >> 5` / `no & 0x1f` subscripts this used to carry: functions.txt
     * lists `idx` and `bit` among photo_datFlgUp()'s locals, which they can
     * only be if FlgUp() itself declares them.  `no - idx * 32` is what the
     * ROM emits (sll + subu), not an andi. */
    void FlgUp(int no)                                                           /* 825 */
    {
        int idx = no >> 5;                                                       /* 826 */
        int bit = no - idx * 32;                                                 /* 827 */

        if (N <= no) {                                                           /* 829 */
            PRINT_ASSERT("FlgUp Illegal Access %d MAX %d", no, N);               /* 830 */
        }

        flag_32[idx] |= 1 << bit;                                                /* 833 */
    }

    void FlgDown(int no)
    {
        int idx = no >> 5;                                                       /* 839 */
        int bit = no - idx * 32;                                                 /* 840 */

        if (N <= no) {                                                           /* 843 */
            PRINT_ASSERT("FlgDown Illegal Access %d MAX %d", no, N);             /* 844 */
        }

        flag_32[idx] &= ~(1 << bit);                                             /* 847 */
    }

    int IsUp(int no)                                                             /* 852 */
    {
        int idx = no >> 5;                                                       /* 853 */
        int bit = no - idx * 32;                                                 /* 854 */

        if (N <= no) {                                                           /* 857 */
            PRINT_ASSERT("IsUp Illegal Access %d MAX %d", no, N);                /* 858 */
        }

        return flag_32[idx] & (1 << bit);                                        /* 860 */
    }
};

/* --------------------------------------------------------------------------
 *  CWaitVariable<T> -- a one-shot countdown.
 *
 *  Lived in ingame/photo/m_plyr_camera.h until fene_entry.c needed it; that
 *  header's own note asked for the move once something outside ingame/photo
 *  used one.
 *
 *  Work() is the only method the ROM ever emitted out of line -- fene_entry.o
 *  carries a linkonce CWaitVariable<int>::Work().  Note that Wait() does not
 *  restart a counter that is still running, which is why callers that mean to
 *  restart it call Reset() first.
 * ------------------------------------------------------------------------ */
/* A value that walks towards a bound by mAdd each tick.  The bounds are
 * template parameters, so they do not affect layout.  Shared with the photo
 * camera's widgets.
 *
 * Work() is the one method the ROM ever emitted out of line -- there is a
 * linkonce CWrkVariable<short,0,128>::Work() at 0x2b0da0, which is what pins
 * the clamp order (high bound first, then low) and the int-width accumulate.
 * The rest are inline; photo_dat.o's expansions place their bodies at the
 * variable.h lines annotated below.
 *
 * The method names are the ROM's own: the stabs in symbols.txt spell the whole
 * member list out (Init, SetMax, SetMin, GetMax, GetMin, GetWidth, SetAddVal,
 * Set, Work, LoopWork, Get, GetState) and their declaration order is the same
 * as the line order below, which is what tells Init(), SetMax() and SetMin()
 * apart -- the three of them expand to the same pair of stores, and for the
 * many <T,0,Max> instantiations Init() and SetMin() are byte-identical. */
template <class T, int Min, int Max>
struct CWrkVariable                 /* 2 * sizeof(T) */
{
    /* 0x0 */ T mValue;
    /* 0x1 */ T mAdd;

    CWrkVariable(void) { Init(); }                                               /* 313 */

    /* Note Init() seeds mValue with the *low bound*, not zero.  For the many
     * <T,0,Max> instantiations that is the same thing, which is why it read as
     * zero when this was first written; m_plyr_camera.o's constructor chain
     * settles it -- CNEquipTrayWrk::mRenzMarkBlink is <short,20,128> and comes
     * out of static init holding 20. */
    void Init(void)                                                              /* 317 */
    {
        mValue = (T)Min;                                                         /* 318 */
        mAdd   = 0;                                                              /* 319 */
    }

    /* Park the value at either end of the range and stop it moving.  Every ROM
     * expansion folds the bound to a literal, which is how they were found:
     * CNPlyrCamera::FinderIn() stores 0x40 into a <char,0,64> and 0x80 into a
     * <short,0,128>. */
    void SetMax(void)                                                            /* 322 */
    {
        mValue = (T)Max;                                                         /* 323 */
        mAdd   = 0;                                                              /* 324 */
    }

    void SetMin(void)                                                            /* 326 */
    {
        mValue = (T)Min;                                                         /* 327 */
        mAdd   = 0;                                                              /* 328 */
    }

    T GetMax(void) const { return (T)Max; }                                      /* 331 */
    T GetMin(void) const { return (T)Min; }                                      /* 335 */
    T GetWidth(void) const { return (T)(Max - Min); }                            /* 339 */

    void SetAddVal(T v) { mAdd = v; }                                            /* 343 */

    void Set(T v)                                                                /* 345 */
    {
        mValue = v;                                                              /* 346 */
        mAdd   = 0;                                                              /* 347 */
    }

    T Get(void) const { return mValue; }                                         /* 352 */

    /* Which way the value is moving, and whether it has arrived: 2 rising,
     * 0 parked at Max, 3 falling, 1 parked at Min.  Note this is not the same
     * numbering CFadeVariable::GetState() uses.  bonus_shot.o's combo mark is
     * the only expansion in the build -- it stretches sideways on a 3 and
     * squashes vertically otherwise. */
    int GetState(void)
    {
        if (0 < mAdd) {
            return mValue != (T)Max ? 2 : 0;
        }

        return mValue != (T)Min ? 3 : 1;
    }

    /* Work(), but the value wraps to the far end instead of sticking, and the
     * return says whether it did.  center_circle.o's ripples are what this
     * exists for: each one runs the range once and starts again at the other
     * side.  Line numbers unknown -- the only expansion in the build is the
     * linkonce <char,0,100> copy at 0x2b0d60, which carries no stabs. */
    int LoopWork(void)
    {
        int iVal = (int)mValue + (int)mAdd;

        if (Max < iVal) {
            mValue = (T)Min;
            return 1;
        }

        if (iVal < Min) {
            mValue = (T)Max;
            return 1;
        }

        mValue = (T)iVal;
        return 0;
    }

    /* Steps mValue by mAdd, clamped to [Min, Max].  The sum is computed at int
     * width, so a short walking past 32767 clamps rather than wrapping. */
    void Work(void)
    {
        int iVal = (int)mValue + (int)mAdd;

        if (Max < iVal) {
            mValue = (T)Max;
            return;
        }

        if (iVal < Min) {
            mValue = (T)Min;
            return;
        }

        mValue = (T)iVal;
    }
};

/* --------------------------------------------------------------------------
 *  CFadeVariable<T> -- a value that walks to a target at a computed speed.
 *
 *  Lived in ingame/photo/m_plyr_camera.h until n_plyr_camera.o placed it here:
 *  every expansion in that object file is tagged with a variable.h line, and
 *  the two out-of-line copies (Fade / Work) are linkonce, so the template has
 *  to be in a header this file's neighbours already see.
 *
 *  As with CWrkVariable the member list is the ROM's -- Set, Fade, Fade2,
 *  IsEnd, GetState, Work, Get -- read out of the stabs.  Fade() and Work() are
 *  the two the ROM emitted out of line (0x2b8950 / 0x2b89c8 for <float>,
 *  0x2b8ac0 / 0x2b8a30 for <int>), which is what pins their bodies exactly.
 *  IsEnd() has no expansion anywhere in n_plyr_camera.o; its body is the only
 *  thing here that is inferred rather than read.
 * ------------------------------------------------------------------------ */
template <class T>
struct CFadeVariable                /* 3 * sizeof(T) */
{
    /* 0x0 */ T mValue;
    /* 0x4 */ T mSpeed;
    /* 0x8 */ T mTarget;

    /* Steps once and stops dead on the frame it reaches the target -- note it
     * clears mSpeed as well as clamping, so a fade cannot overshoot and then
     * crawl back.  Both halves test `<`, so a fade that starts already at its
     * target simply never moves. */
    void Work(void)
    {
        mValue = mValue + mSpeed;

        if (mTarget < mValue && (T)0 < mSpeed)
        {
            mSpeed = 0;
            mValue = mTarget;
        }

        if (mValue < mTarget && mSpeed < (T)0)
        {
            mSpeed = 0;
            mValue = mTarget;
        }
    }

    /* Aims at tTarget over tTime ticks.  A zero time is not a division by zero
     * -- it means "no speed", and the mSpeed == 0 test below then snaps the
     * value across immediately. */
    void Fade(T tTarget, T tTime)
    {
        if (tTime == (T)0)
        {
            mSpeed = 0;
        }
        else
        {
            mSpeed = (T)((tTarget - mValue) / tTime);
        }

        if (mSpeed == (T)0)
        {
            mValue = tTarget;
        }

        mTarget = tTarget;
    }

    void Set(T v)                                                                /* 459 */
    {
        mValue  = v;                                                             /* 460 */
        mTarget = v;                                                             /* 461 */
        mSpeed  = 0;                                                             /* 462 */
    }

    /* Fade(), but a no-op when the target is already the one asked for -- so a
     * caller can re-issue the same request every frame without restarting the
     * ramp.  Every per-frame fade request in n_plyr_camera.c goes through this
     * rather than through Fade(). */
    void Fade2(T tTarget, T tTime)                                               /* 467 */
    {
        if (mTarget == tTarget)                                                  /* 468 */
        {
            return;
        }

        Fade(tTarget, tTime);                                                    /* 471 */
    }

    /* No expansion survives in n_plyr_camera.o -- inferred from the name and
     * from the fact that Work() parks mValue on mTarget exactly. */
    int IsEnd(void) const { return mValue == mTarget; }

    /* Which way the value is moving.  The three results are the ROM's own
     * literals; no enum for them survives in the debug info. */
    int GetState(void)                                                           /* 478 */
    {
        if (mSpeed != (T)0)                                                      /* 479 */
        {
            if ((T)0 < mSpeed)                                                   /* 480 */
            {
                return 2;                                                        /* 482 */
            }

            return 3;
        }

        return 4;                                                                /* 485 */
    }

    T Get(void) const { return mValue; }                                         /* 490 */
};

template <class T>
struct CWaitVariable                /* sizeof(T) */
{
    /* 0x0 */ T mValue;

    /* Every CWaitVariable in m_plyr_camera comes out of static init at zero
     * while its neighbours (CFadeVariable, CBlinkVariable) are left alone, so
     * the zeroing is this class's own constructor rather than the owners'. */
    CWaitVariable(void) { mValue = 0; }

    void operator=(const T& other)
    {
        mValue = other;
    }

    operator T() const { return mValue; }

    void Reset(void) { mValue = 0; }

    /* Line numbers below are photo_charger.o's, which expands Wait() and Get()
     * and calls the linkonce Work(). */
    void Wait(T v)                                                               /* 425 */
    {
        if (mValue == 0) {                                                       /* 426 */
            mValue = v;                                                          /* 427 */
        }
    }

    /* Counts down one tick; non-zero only on the frame the counter reaches 0. */
    int Work(void)                                                               /* 441 */
    {                                                                            /* 442 */
        if (mValue != 0) {                                                       /* 443 */
            mValue = mValue - 1;                                                 /* 444 */
            if (mValue == 0) {                                                   /* 445 */
                return 1;
            }
        }
        return 0;                                                                /* 449 */
    }

    T Get(void) const { return mValue; }                                         /* 433 */
};

/* --------------------------------------------------------------------------
 *  CBlinkSwitchVariable<T,Min,Max,Time,InitVal> -- a value that pulses between
 *  two bounds while it is switched on, and decays back to InitVal when it is
 *  switched off.
 *
 *  Lived in ingame/photo/m_plyr_camera.h until filament.o placed it here: its
 *  four linkonce Work() bodies carry `SOL common/variable.h` stabs, and the
 *  expansions of Init() / BlinkOn() / Get() inside CFilament are tagged with
 *  the variable.h lines annotated below.
 *
 *  `Time` is how many ticks a full Min..Max sweep takes -- the per-tick step is
 *  (Max - Min) / Time, truncated, so <char,90,118,6,90> steps by 4 and
 *  <char,75,112,17,75> by 2.  Both are compile-time constants; the ROM's Work()
 *  bodies contain no division.
 *
 *  Note the asymmetry, which is the whole point of the class: while blinking
 *  the value turns around at Min and Max, but with the blink off it walks down
 *  to *InitVal* and parks there.  filament.o's four instantiations all have
 *  InitVal == Min so they cannot tell the two apart; film_no.o's
 *  <char,70,90,15,40> is what settles it -- its off branch clamps to 40, not
 *  to 70.
 *
 *  No constructor: the ROM's four file-scope objects sit in .sdata holding
 *  zero, and filament.o has no static-initialisation function at all.  Adding
 *  one here would seed them before CFilament::Init() ever ran.
 * ------------------------------------------------------------------------ */
template <class T, int Min, int Max, int Time, int InitVal>
struct CBlinkSwitchVariable         /* 2 * sizeof(T) */
{
    /* 0x0 */ T      mValue;
    /* 0x1:0 */ u_char mOn : 1;
    /* 0x1:1 */ u_char mUp : 1;

    /* Init() leaves the blink *off* and pointing up; every ROM caller that
     * wants it running follows with BlinkOn().  GCC folds the pair into a
     * single `ori 3`, which is why CFilament::Init() shows no separate stores. */
    void Init(void)
    {
        mValue = GetInitVal();                                                   /* 640 */
        mOn    = 0;                                                              /* 641 */
        mUp    = 1;                                                              /* 642 */
    }

    void BlinkOn(void)  { mOn = 1; }                                             /* 646 */
    void BlinkOff(void) { mOn = 0; }                                             /* 649 */
    int  IsOn(void)     { return mOn; }                                          /* 651 */

    T    Get(void)      { return mValue; }                                       /* 657 */
    void Set(T v)       { mValue = v; }

    T GetMax(void)     { return (T)Max; }
    T GetMin(void)     { return (T)Min; }
    T GetInitVal(void) { return (T)InitVal; }

    /* One tick.  The steps are computed at int width and stored back through
     * T, matching the ROM's lb/addiu/sb. */
    void Work(void)
    {
        int iValue = mValue;                                                     /* 682 */

        if (mOn != 0)                                                            /* 684 */
        {
            if (mUp != 0)                                                        /* 685 */
            {
                iValue += (Max - Min) / Time;                                    /* 686 */
                if (GetMax() <= iValue)                                          /* 687 */
                {
                    iValue = GetMax();                                           /* 688 */
                    mUp    = 0;                                                  /* 689 */
                }
            }
            else
            {
                iValue -= (Max - Min) / Time;                                    /* 692 */
                if (iValue <= GetMin())                                          /* 693 */
                {
                    iValue = GetMin();                                           /* 694 */
                    mUp    = 1;                                                  /* 695 */
                }
            }
        }
        else
        {
            iValue -= (Max - Min) / Time;                                        /* 699 */
            if (iValue <= GetInitVal())                                          /* 700 */
            {
                iValue = GetInitVal();
            }
        }

        mValue = (T)iValue;                                                      /* 705 */
    }
};

/* --------------------------------------------------------------------------
 *  CMIN_MAX<T> -- an inclusive range, and the interpolation the game asks of
 *  one.
 *
 *  Placed here by spirit_gage.o: CSpiritGage::CalcDamageRate() is the only
 *  expansion of it anywhere in the build, and both of its inlined pieces carry
 *  variable.h line numbers (GetWidth at 533, GetByProportion at 536).  It sits
 *  between CFadeVariable (…523) and CBlinkVariable (553…) in the ROM's own
 *  ordering, which is why it is written here rather than beside the widget it
 *  serves.
 *
 *  The member list is the ROM's, out of types.txt -- GetWidth,
 *  GetByProportion, GetAverage, in that declaration order.  Nothing in the
 *  build expands GetAverage, so it is left out rather than invented, the same
 *  call made for CFVariable's Offset/Sub/LoopSub below.
 *
 *  Aggregate, with no constructor: CSpiritGage::aDmgMultipleTbl[3] is a
 *  brace-initialised .rodata table and spirit_gage.o carries no
 *  static-initialisation function at all.
 * ------------------------------------------------------------------------ */
template <class T>
struct CMIN_MAX                     /* 2 * sizeof(T) */
{
    /* 0x0 */ T mMin;
    /* 0x4 */ T mMax;

    T GetWidth(void) const { return (T)(mMax - mMin); }                          /* 533 */

    /* mMin at t == 0 and mMax at t == 1.  Nothing clamps t, and the spirit
     * gauge relies on that: during a shutter chance its own percentage runs
     * past the 90 it divides by, so t arrives above 1 and the result lands
     * above mMax. */
    T GetByProportion(T t) const { return (T)(mMin + t * GetWidth()); }          /* 536 */
};

/* --------------------------------------------------------------------------
 *  CBlinkVariable<T,Min,Max> -- a one-shot pulse: ramp up to Max, bounce, ramp
 *  back down and stop dead at Min.
 *
 *  Lived in ingame/photo/m_plyr_camera.h until n_equip_tray.o placed it here:
 *  its expansions inside CNEquipTrayWrk::Reset() and ::Draw() carry variable.h
 *  lines (Init at 554/555, IsOn at 559), and Blink()'s assert banner names this
 *  header at line 603.  Both Blink() and Work() are linkonce bodies in
 *  n_equip_tray.o (0x2b8678 / 0x2b86f0), so the template has to be in a header.
 *
 *  Method list is the ROM's, out of types.txt: Init, Blink, IsOn, Work, Get,
 *  Set, SetMax, SetMin, GetMax, GetMin, GetWidth.  Only the first five have an
 *  expansion anywhere in n_equip_tray.o; the trivial ones are written out for
 *  completeness because Blink()/Work() need the bounds anyway.
 *
 *  Note IsOn() is "still pulsing", not "lit" -- it tests mSpeed, and Work()
 *  parks mSpeed at zero the moment the pulse lands back on Min.
 * ------------------------------------------------------------------------ */
template <class T, int Min, int Max>
struct CBlinkVariable               /* 2 * sizeof(T) */
{
    /* 0x0 */ T mValue;
    /* 0x1 */ T mSpeed;

    /* Init() really does seed from Min, not from zero.  n_equip_tray.o could
     * not tell the two apart (both its instantiations have Min == 0), but
     * photo_charger.o's <char,50,127> settles it -- CPhotoCharger::Reset()
     * stores 0x32 here, and the linkonce Work() below clamps its low end to
     * the same 0x32. */
    void Init(void)                                                              /* 553 */
    {
        mValue = (T)Min;                                                         /* 554 */
        mSpeed = 0;                                                              /* 555 */
    }

    /* Arms one pulse that reaches Max in tTime ticks.  A zero time is an
     * assert, not a division by zero -- the ROM tests it first and skips the
     * divide entirely, so the value is left alone.
     *
     * The ROM's own order is the reverse of this: line 600 is the assignment
     * and 603 the assert, so it was written `if (tTime != 0) ... else ...`.
     * Kept this way round because the two are identical and the assert reads
     * better first; the annotations are the ROM's. */
    void Blink(T tTime)                                                          /* 598 */
    {
        if (tTime == 0)                                                          /* 599 */
        {
            PRINT_ASSERT("CBlinkVariable::Blink time is 0");                     /* 603 */
        }
        else
        {
            mSpeed = (T)((GetMax() - mValue) / tTime);                           /* 600 */
        }
    }

    int IsOn(void) { return mSpeed != 0; }                                       /* 559 */

    void Work(void)                                                              /* 610 */
    {
        int iValue;

        if (mSpeed == 0)                                                         /* 611 */
        {
            return;
        }

        iValue = (int)mValue                                                     /* 615 */
               + (int)mSpeed;                                                    /* 616 */

        if (GetMax() <= iValue && (T)0 < mSpeed)                                 /* 618 */
        {
            iValue = GetMax();                                                   /* 619 */
            mSpeed = -mSpeed;                                                    /* 620 */
        }

        if (iValue <= GetMin() && mSpeed < (T)0)                                 /* 622 */
        {
            iValue = GetMin();                                                   /* 623 */
            mSpeed = 0;                                                          /* 624 */
        }

        mValue = (T)iValue;                                                      /* 627 */
    }

    T    Get(void)     { return mValue; }                                        /* 567 */
    void Set(T v)      { mValue = v; }
    void SetMax(void)  { mValue = (T)Max; }
    void SetMin(void)  { mValue = (T)Min; }
    /* damage_disp.o's Draw() expands exactly Get() and GetMax() in
     * `iAlpha * mOneBlink.Get() / mOneBlink.GetMax()`, which is what pins
     * this one; the divisor even keeps its divide-by-zero trap because it
     * comes through a call rather than as a literal. */
    T    GetMax(void)  { return (T)Max; }                                        /* 581 */
    T    GetMin(void)  { return (T)Min; }
    T    GetWidth(void) { return (T)(Max - Min); }
};

/* --------------------------------------------------------------------------
 *  CFVariable -- a float that carries its own bounds.
 *
 *  Unlike the widget templates above the limits are members rather than
 *  template parameters, because the equip tray moves them at runtime.  Lived in
 *  ingame/photo/m_plyr_camera.h until n_equip_tray.o placed it here: every
 *  expansion in CNEquipTrayWrk::Reset(), ::Work() and ::AccumulaterDraw() is
 *  tagged with the variable.h lines annotated below.
 *
 *  The constructor is out of line and belongs to m_plyr_camera.o -- it opens
 *  the range as wide as the EE's float format goes; see the note there.
 *
 *  The ROM's method list (types.txt) also has Offset, Sub and LoopSub.  Nothing
 *  in n_equip_tray.o expands them, so they are left out rather than invented.
 * ------------------------------------------------------------------------ */
struct CFVariable                   /* 0xc */
{
    /* 0x0 */ float mValue;
    /* 0x4 */ float m_fMax;
    /* 0x8 */ float m_fMin;

    CFVariable(void);

    void  SetMin(float v) { m_fMin = v; }                                        /* 187 */
    void  SetMax(float v) { m_fMax = v; }                                        /* 188 */
    void  Set(float v)    { mValue = v; }                                        /* 190 */

    float GetMax(void)   { return m_fMax; }
    float GetMin(void)   { return m_fMin; }
    float GetWidth(void) { return m_fMax - m_fMin; }
    float Get(void)      { return mValue; }                                      /* 287 */

    /* Steps the value and clamps at the ceiling; 0 means it hit the ceiling. */
    int Add(float fAddValue)
    {
        mValue = mValue + fAddValue;                                             /* 243 */

        if (GetMax() < mValue)                                                   /* 245 */
        {
            mValue = GetMax();                                                   /* 246 */
            return 0;
        }

        return 1;                                                                /* 248 */
    }

    /* Add(), but wrapping round to the floor instead of parking at the top.
     * The ROM types this int; no expansion reads the result, so the value
     * returned here follows Add()'s convention. */
    int LoopAdd(float fAddValue)
    {
        if (Add(fAddValue) == 0)                                                 /* 272 */
        {
            mValue = GetMin();                                                   /* 273 */
            return 0;
        }

        return 1;
    }
};

/* --------------------------------------------------------------------------
 *  Controller pad state (one per port).  The DMA buffer receives the raw pad
 *  report; `now`/`old` are this/last frame's button bitmasks, `cnt`/`cnt_bak`
 *  the per-button hold counters, and `one`/`rpt` the derived edge / auto-repeat
 *  masks used by the menu code.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x1c0 */
{
    /* 0x000 */ int       port;
    /* 0x004 */ int       slot;
    /* 0x008 */ u_char    _pad008[0x38];
    /* 0x040 */ u_long128 pad_dma_buf[16];
    /* 0x140 */ u_short   now;
    /* 0x142 */ u_short   old;
    /* 0x144 */ u_short   cnt[16];
    /* 0x164 */ u_short   cnt_bak[16];
    /* 0x184 */ u_short   one;
    /* 0x186 */ u_short   rpt;
    /* 0x188 */ u_short   rpt_time;
    /* 0x18a */ u_char    pad_direct[6];
    /* 0x190 */ char      flags;
    /* 0x191 */ char      step;
    /* 0x192 */ char      id;
    /* 0x193 */ u_char    analog[4];
    /* 0x197 */ u_char    push[13];         /* [12] is the original padding byte used by pushdat_m */
    /* 0x1a4 */ u_short   an_cnt[2];
    /* 0x1a8 */ u_short   an_cnt_bak[2];
    /* 0x1ac */ float     an_rot[2];
    /* 0x1b4 */ float     an_rot_bak[2];
    /* 0x1bc */ u_char    an_dir[2];
    /* 0x1be */ u_char    an_dir_bak[2];
} PAD_STRUCT;

/* --------------------------------------------------------------------------
 *  Top-level system work block.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x20 */
{
    /* 0x00 */ u_long     count;            /* frames since boot                */
    /* 0x08 */ sceCdCLOCK rtc;              /* last RTC snapshot                */
    /* 0x10 */ short      sreset_count;     /* soft-reset combo hold counter    */
    /* 0x12 */ u_char     sreset_ng;        /* soft reset locked out            */
    /* 0x13 */ u_char     video_mode;       /* 2 = NTSC, 3 = PAL                */
    /* 0x14 */ u_char     language;
    /* 0x18 */ int        game_mode;
    /* 0x1c */ u_char     interrupt;
} SYS_WRK;

/* --------------------------------------------------------------------------
 *  User options.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ int    brightness;
    /* 0x4 */ int    snd_volume;
    /* 0x8 */ u_char pad_vib;
    /* 0x9 */ u_char pad_type;
    /* 0xa */ u_char view_vertical;
    /* 0xb */ u_char ana_replace;
    /* 0xc */ u_char credits;
    /* 0xd */ u_char snd_output;
    /* 0xe */ u_char move_operate;
    /* 0xf */ u_char pad;
} OPTION_WRK;

/* --------------------------------------------------------------------------
 *  Camera (photo) upgrade / charge state.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ u_char charge_range;
    /* 0x1 */ u_char charge_max;
    /* 0x2 */ u_char charge_speed;
    /* 0x8 */ u_long point;
} CAM_CUSTOM_WRK;

/* --------------------------------------------------------------------------
 *  Debug-menu state (per-tool one-shot init flags + current selection).
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x38 */
{
    /* 0x00 */ u_char fr_30;
    /* 0x04 */ int    mode;
    /* 0x08 */ int    menu_csr;
    /* 0x0c */ int    comp_mode;
    /* 0x10 */ int    init_movieviewer;
    /* 0x14 */ int    init_msg_viewer;
    /* 0x18 */ int    init_sndtest;
    /* 0x1c */ int    init_scntest;
    /* 0x20 */ int    init_screen_calib;
    /* 0x24 */ int    init_test2d;
    /* 0x28 */ int    init_motionviewer;
    /* 0x2c */ int    init_func;
    /* 0x30 */ int    init_subtitle_test;
    /* 0x34 */ int    dbg_menu_sw;
} DEBUG_WRK;

/* --------------------------------------------------------------------------
 *  Larger game work blocks whose owners are not reconstructed yet.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0xa0 */
{
    /* 0x00 */ float mloop;
    /* The ROM's 0x04..0x10 hole is sceVu0FVECTOR's quadword alignment, which
     * a float[4] does not carry on the host.  Without it every vector member
     * slides down 12 bytes and MOVE_BOX comes out 0x94 instead of 0xa0 --
     * which then mis-sizes PLCMN_WRK and ENE_WRK, both of which embed one. */
    /* 0x04 */ u_char _pad04[0x0c];
    /* 0x10 */ sceVu0FVECTOR pos;
    /* 0x20 */ sceVu0FVECTOR bpos;
    /* 0x30 */ sceVu0FVECTOR mv;
    /* 0x40 */ sceVu0FVECTOR bmv;
    /* 0x50 */ sceVu0FVECTOR rot;
    /* 0x60 */ sceVu0FVECTOR brot;
    /* 0x70 */ sceVu0FVECTOR spd;
    /* 0x80 */ sceVu0FVECTOR rspd;
    /* 0x90 */ sceVu0FVECTOR trot;
} MOVE_BOX;

typedef struct                      /* 0x40 */
{
    /* 0x00 */ u_long   sta;
    /* 0x08 */ u_long   sta_old;
    /* 0x10 */ u_long   mvsta;
    /* 0x18 */ u_short  hp;
    /* 0x1a */ u_short  sp;
    /* 0x1c */ u_short  hpmax;
    /* 0x1e */ u_short  spmax;
    /* 0x20 */ u_short  hp_recover_time;
    /* 0x22 */ u_short  sp_recover_time;
    /* 0x24 */ u_short  sp_down_fl;
    /* 0x26 */ u_short  dmg;
    /* 0x28 */ u_short  rhspdmg;
    /* 0x2a */ u_short  rhpdmg;
    /* 0x2c */ u_short  rspdmg;
    /* 0x2e */ u_short  dmg_old;
    /* 0x30 */ u_short  dmg_type;
    /* 0x32 */ u_char   dmg_cam_flag;
    /* 0x34 */ u_short  dwalk_tm;
    /* 0x36 */ u_short  cond;
    /* 0x38 */ u_short  cond_old;
    /* 0x3a */ u_short  cond_tm;
    /* 0x3c */ u_short  invisible_timer;
    /* 0x3e */ u_char   _pad3e[0x02];
} STATUS_DAT;

typedef struct                      /* 0x0c */
{
    /* 0x0 */ u_char area_no;
    /* 0x1 */ u_char area_old;
    /* 0x2 */ u_char room_id;
    /* 0x3 */ u_char room_old;
    /* 0x4 */ u_char camera_no;
    /* 0x5 */ u_char camera_no_old;
    /* 0x8 */ float  hight;
} PROOM_INFO;

typedef struct                      /* 0x110 */
{
    /* 0x000 */ MOVE_BOX   mbox;
    /* 0x0a0 */ STATUS_DAT st;
    /* 0x0e0 */ PROOM_INFO pr_info;
    /* 0x0ec */ u_char     mode;
    /* 0x0ed */ u_char     atk_eneno;
    /* 0x0ee */ u_char     atk_pos;
    /* 0x0ef */ u_char     atk_rot;
    /* 0x0f0 */ float      near_ene_dist;
    /* 0x0f4 */ float      near_ene_dist_old;
    /* 0x0f8 */ u_char     near_ene_no;
    /* 0x0f9 */ u_char     _pad0f9;
    /* 0x0fa */ short      floor;
    /* 0x0fc */ u_char     _pad0fc[0x04];
    /* 0x100 */ float      headpos[4];
} PLCMN_WRK;

/* Whether the camera currently has a shot lined up.  player.c latches this
 * every finder frame (pre/now on PLYR_WRK) and the camera widgets read it, so
 * it lives here rather than in a photo header the common types cannot include
 * without a cycle. */
enum SHUTTER_CHANCE_STATE
{
    SHUTTER_CHANCE_NONE        =  0,
    SHUTTER_CHANCE_NORMAL      =  1,
    SHUTTER_CHANCE_SP          =  2,
    SHUTTER_CHANCE_STATE_MAX   =  3,
    SHUTTER_CHANCE_FORCE_DWORD = -1
};

/* Keeps the camera pointed at one ghost for a countdown of frames.  Owned by
 * player.c; the ROM builds it out of the CWaitVariable / CWrkVariable
 * templates, which are modelled here as the two plain fields they expand to.
 * mTraceEne == ENE_TRACER_NONE means "not tracing". */
struct CEneTracer                   /* 0x4 */
{
    enum { ENE_TRACER_NONE = 10 };

    /* 0x0 */ short mWaitCnt;       /* CWaitVariable<short>              */
    /* 0x2 */ char  mTraceEne;      /* CWrkVariable<char,0,10>::mValue   */
    /* 0x3 */ char  mTraceEneAdd;   /* CWrkVariable<char,0,10>::mAdd     */

    /* Non-zero when a trace was in progress when it was cleared. */
    int  Init(void);
    void Work(void);
    void Req(int iEneNo, int iFrame);
};

typedef struct                      /* 0x380 */
{
    /* 0x000 */ PLCMN_WRK cmn_wrk;
    /* 0x110 */ u_char   modedead;
    /* 0x111 */ u_char   anime_no;
    /* 0x112 */ u_char   charge_num;
    /* 0x113 */ u_char   _pad113;
    /* 0x114 */ float    charge_rate;
    /* 0x118 */ float    charge_deg;
    /* 0x11c */ float    frot_x;
    /* 0x120 */ short    fp[2];
    /* 0x124 */ u_short  no_photo_tm;
    /* 0x126 */ u_short  shutter_tm;
    /* 0x128 */ u_short  vib_time_sm;
    /* 0x12a */ u_short  vib_time_bg;
    /* 0x12c */ u_char   _pad12c[0x04];
    /* 0x130 */ float    spd[4];
    /* 0x140 */ float    old_spd[4];
    /* 0x150 */ float    fhp[5][4];
    /* 0x1a0 */ float    prot;
    /* 0x1a4 */ u_short  fene_tm;
    /* 0x1a6 */ u_short  bonus_sta;
    /* 0x1a8 */ u_short  avoid_tm;
    /* 0x1aa */ u_short  avoid_flg;
    /* 0x1ac */ u_short  avoid_st;
    /* 0x1ae */ u_short  avoid_sp;
    /* 0x1b0 */ u_char   door_flg;
    /* 0x1b1 */ u_char   _pad1b1;
    /* 0x1b2 */ u_short  door_no;
    /* 0x1b4 */ u_char   _pad1b4[0x0c];
    /* 0x1c0 */ float    bwp[4];
    /* 0x1d0 */ float    spot_pos[4];
    /* 0x1e0 */ float    spot_rot[4];
    /* 0x1f0 */ G3DLIGHT fl;
    /* 0x260 */ G3DLIGHT fl2;
    /* 0x2d0 */ G3DLIGHT reflectionlight;
    /* 0x340 */ float    maplight_scale;
    /* PORT NOTE: SND_3D_SET is 0xc on target and 0x18 here -- its members hold
     * pointers -- so every offset from here down drifts by 0x10 on the host.
     * Nothing indexes PLYR_WRK by offset and nothing serialises it, so the
     * comments below are documentation of the ROM's layout, not the host's. */
    /* 0x344 */ SND_3D_SET s3d;
    /* 0x350 */ float    fl_pow;
    /* 0x354 */ int      finder_tm;
    /* 0x358 */ char     finder_lock_cnt;
    /* 0x359 */ char     move_lock_cnt;
    /* 0x35a */ char     action_lock_cnt;
    /* 0x35b */ char     shutter_lock_cnt;
    /* 0x35c */ char     run_lock_cnt;
    /* CEneTracer leads with a short, so it lands on 0x35e, not 0x360 -- what
     * types.txt reports.  The two bytes after it are ane_curse_lock's own
     * 4-byte alignment. */
    /* 0x35d */ u_char   _pad35d;
    /* 0x35e */ CEneTracer ene_tracer;
    /* 0x362 */ u_char   _pad362[0x02];
    /* 0x364 */ int      ane_curse_lock;
    /* 0x368 */ float    hit_rad;
    /* 0x36c */ SHUTTER_CHANCE_STATE preShutterChanceState;
    /* 0x370 */ SHUTTER_CHANCE_STATE nowShutterChanceState;
    /* 0x374 */ u_char   _pad374[0x0c];
} PLYR_WRK;

/* Companion ("sister") work block.  Owned by sister.c, which is not
 * reconstructed yet; the event open-condition code (ev_open.c) reads her
 * mbox / floor the same way it reads the player's, so the layout lives here
 * next to PLYR_WRK.  Offsets in the comments are the ROM's -- the two pointer
 * fields in SISALG_WRK widen on a 64-bit host, which is harmless because
 * nothing serialises this block. */
typedef union                       /* 0x8 (EE) */
{
    u_char    *pu8;
    u_short   *pu16;
    u_int     *pu32;
    u_long    *pu64;
    char      *ps8;
    short int *ps16;
    int       *ps32;
    long int  *ps64;
    /* PORT NOTE: the ROM's `long` is 8 bytes on the EE and wide enough to hold
     * a target pointer.  On MinGW `long` is 4, so sis_algo.c's opcode cursor
     * arithmetic -- comm_add.wrk = comm_add_top + offset -- would truncate a
     * host pointer to its low half.  intptr_t is the ROM's intent. */
    intptr_t   wrk;
} P_INT;

typedef struct                      /* 0xb0 */
{
    /* 0x00 */ u_char   job_no;
    /* 0x01 */ u_char   pos_no;
    /* 0x02 */ u_char   wait_time;
    /* 0x04 */ float    loop[2];
    /* 0x10 */ P_INT    comm_add;      /* opcode cursor                     */
    /* 0x18 */ intptr_t comm_add_top;  /* script base -- jumps are relative */
    /* 0x20 */ intptr_t data_addr;     /* SIS_ALG_OBJ buffer, 0 = no script */
    /* 0x28 */ u_long   stack_b[16];
    /* 0xa8 */ u_long  *stack_p;
    /* 0xac */ u_char   flag;
} SISALG_WRK;

typedef struct                      /* 0x250 */
{
    /* 0x000 */ PLCMN_WRK  cmn_wrk;
    /* 0x110 */ SISALG_WRK alg;
    /* 0x1c0 */ u_char     on;
    /* 0x1c1 */ u_char     modedead;
    /* 0x1c2 */ u_char     anime_no;
    /* 0x1c3 */ u_char     trace_dist;
    /* 0x1c4 */ u_char     trace_dist_bak;
    /* 0x1c6 */ u_short    stop_tm;
    /* 0x1c8 */ u_short    walk_tm;
    /* 0x1ca */ u_short    run_tm;
    /* 0x1cc */ u_short    scared_rcvr_tm;
    /* 0x1ce */ u_short    dmg_tm;
    /* 0x1d0 */ u_short    dmg_se_num;
    /* 0x1d2 */ u_short    dmg_se_cnt;
    /* 0x1d4 */ u_short    btl_recv_tm;
    /* 0x1d6 */ u_short    push_se_tm;
    /* 0x1d8 */ u_short    se_nanika2_tm;
    /* 0x1dc */ int        se_deadly;
    /* 0x1e0 */ u_char     se_matte;
    /* 0x1e1 */ u_char     se_konaide;
    /* 0x1e2 */ u_short    cower_tm;
    /* 0x1e4 */ u_short    se_cower_cnt;
    /* 0x1e6 */ short int  se_door_fl;
    /* 0x1e8 */ float      pl_dist;
    /* 0x1f0 */ float      wpos[4];
    /* 0x200 */ float      spd[4];
    /* 0x210 */ float      old_spd[4];
    /* 0x220 */ SND_3D_SET s3d;
    /* 0x230 */ float      bwp[4];
    /* 0x240 */ int        lock_cnt;
    /* 0x244 */ u_char     join_flg;
} SIS_WRK;

typedef struct                      /* 0xac */
{
    /* 0x00 */ int   hit_disp;
    /* 0x04 */ int   ene_stop;
    /* 0x08 */ int   fog_sw;
    /* 0x0c */ int   near;
    /* 0x10 */ int   far;
    /* 0x14 */ int   min;
    /* 0x18 */ int   max;
    /* 0x1c */ int   fog_r;
    /* 0x20 */ int   fog_g;
    /* 0x24 */ int   fog_b;
    /* 0x28 */ int   hi_spd;
    /* 0x2c */ int   perf_count_sw;
    /* 0x30 */ int   move_speed;
    /* 0x34 */ int   cut_len;
    /* 0x38 */ int   muteki;
    /* 0x3c */ int   fl_sw;
    /* 0x40 */ float fl_intens;
    /* 0x44 */ float fl_range;
    /* 0x48 */ float fl_py;
    /* 0x4c */ float fl_pz;
    /* 0x50 */ float fl_y;
    /* 0x54 */ float fl_z;
    /* 0x58 */ int   fl_line;
    /* 0x5c */ float flrf_y;
    /* 0x60 */ float flrf_z;
    /* 0x64 */ float flrf_range;
    /* 0x68 */ float flrf_si_rate;
    /* 0x6c */ float pl_amb;
    /* 0x70 */ float sis_para_r;
    /* 0x74 */ float sis_para_g;
    /* 0x78 */ float sis_para_b;
    /* 0x7c */ float fStaticDirLightColStepR;
    /* 0x80 */ float fStaticDirLightColStepG;
    /* 0x84 */ float fStaticDirLightColStepB;
    /* 0x88 */ float fYFlashlightStep;
    /* 0x8c */ float fRangeFlashlightStep;
    /* 0x90 */ float fl2_range;
    /* 0x94 */ int   shadow_model_disp;
    /* 0x98 */ int   sis_tr_point;
    /* 0x9c */ float dummy[4];
} DEBUG_VAR;

/* The ROM's own method list (types.txt) is Init, SetMax, SetMin, GetMax,
 * GetMin, GetWidth, Set, Increment, Decrement, LoopIncrement, LoopDecrement,
 * Offset, Add, Sub, LoopAdd, LoopSub, Get -- no operator=(const T&) and no
 * operator T().  Those two are the port's, kept because every existing caller
 * relies on the implicit conversion; they expand to exactly what Set() and
 * Get() do.  Offset/Add/Sub/LoopAdd/LoopSub have no expansion anywhere yet and
 * are left out rather than invented. */
template <class T, int Min, int Max>
struct CVariable                    /* sizeof(T) */
{
    /* 0x0 */ T mValue;

    /* Zero, not Min -- every CVariable in m_plyr_camera's constructor chain is
     * stored zero, including the <char,0,N> ones where the two would differ if
     * this followed CWrkVariable::Init(). */
    CVariable(void) { mValue = 0; }

    /* Park the value on a bound.  camera_power_up.o expands both -- Init() on
     * the sub-function gems and SetMax() on the lens gems -- which is what
     * pins their lines.  Every instantiation in the build is <char,0,N>, so
     * nothing here can tell Min from a literal 0; Min is written for
     * consistency with the sibling templates.
     *
     * types.txt also lists SetMin() and GetWidth() between SetMax() and
     * GetMax().  Nothing in the build expands either, so they are left out
     * rather than invented. */
    void Init(void)                                                              /* 19 */
    {
        mValue = (T)Min;                                                         /* 20 */
    }

    void SetMax(void)                                                            /* 23 */
    {
        mValue = (T)Max;                                                         /* 24 */
    }

    T GetMax(void) const { return (T)Max; }                                      /* 29 */
    T GetMin(void) const { return (T)Min; }

    /* Lines measured from setup_menu.o's expansion of <char,0,3>, which is the
     * first in the build where both bounds are non-trivial: the two tests are
     * 43 and 45 and the asserts 44 and 46.  Note the store is line 49, three
     * lines past the second assert. */
    void Set(const T& value)                                                     /* 39 */
    {
        if (value > Max)                                                         /* 43 */
        {
            PRINT_ASSERT("Set Value is Illegal");                                /* 44 */
        }
        else if (value < Min)                                                    /* 45 */
        {
            PRINT_ASSERT("Set Value is Illegal");                                /* 46 */
        }

        mValue = value;                                                          /* 49 */
    }

    /* Step one, refusing to leave the range: the value parks on the bound it
     * ran into and the call reports 0.  Note neither of these asserts -- only
     * Set() does, which is why the tray's selector walks with these. */
    int Increment(void)
    {
        int iVal = (int)mValue + 1;                                              /* 53, 54 */

        if (Max < iVal)                                                          /* 57 */
        {
            mValue = (T)Max;                                                     /* 58 */
            return 0;
        }

        mValue = (T)iVal;                                                        /* 61 */
        return 1;
    }

    int Decrement(void)
    {
        int iVal = (int)mValue - 1;                                              /* 67, 68 */

        if (iVal < Min)                                                          /* 71 */
        {
            mValue = (T)Min;                                                     /* 72 */
            return 0;
        }

        mValue = (T)iVal;
        return 1;                                                                /* 75 */
    }

    /* The wrapping pair: an Increment() that ran into Max comes back round to
     * Min, and vice versa. */
    int LoopIncrement(void)
    {
        if (Increment() == 0)                                                    /* 81 */
        {
            mValue = (T)Min;                                                     /* 83 */
            return 0;
        }

        return 1;
    }

    int LoopDecrement(void)
    {
        if (Decrement() == 0)                                                    /* 92 */
        {
            mValue = (T)Max;
            return 0;
        }

        return 1;
    }

    T Get(void) const { return mValue; }                                         /* 167 */

    void operator=(const T& other)
    {
        Set(other);
    }

    operator T() const { return mValue; }
};

/* types.txt.  DAY_INFO / TIME_INFO / DATE_INFO are declared as one group in
 * the ROM's debug info and kept together here.  play_data.o's GetSystemTime()
 * fills a DATE_INFO from the drive RTC. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int year;
    /* 0x4 */ int month;
    /* 0x8 */ int day;
} DAY_INFO;

/* Passed by value out of GetMemoryCardPlayDataPlayTime() and GetPlayTime(),
 * and into the save/load info lines, so several sides need it. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ int hour;
    /* 0x4 */ int min;
    /* 0x8 */ int sec;
} TIME_INFO;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ DAY_INFO  day;
    /* 0x0c */ TIME_INFO time;
} DATE_INFO;

struct INGAME_WRK { // 0xc
    /* 0x0 */ CVariable<char,0,24> mChapterNo;
    /* 0x1 */ CVariable<char,0,3> mDifficulty;
    /* 0x2 */ CVariable<char,0,99> mClearCnt;
    /* 0x3 */ u_char clear_save_flg;
private:
    /* 0x4 */ int mMenuLockCnt;
    /* 0x8 */ int mPauseLockCnt;

public:
    INGAME_WRK() { }
    // ~INGAME_WRK() { }
    // INGAME_WRK& operator=();
    void Init();
    void MenuLock();
    void MenuUnlock();
    int MenuIsLocked();
    void PauseLock();
    void PauseUnlock();
    int PauseIsLocked();
};

typedef struct                      /* 0x10 */
{
    /* 0x0 */ int mPlyrMdlNo;
    /* 0x4 */ int mSisterMdlNo;
    /* 0x8 */ int mPlyrAcsNo;
    /* 0xc */ int mSisterAcsNo;
} GAME_COSTUME;

typedef struct                      /* 0x8 */
{
    /* 0x0:0  */ u_short event_load      : 1;
    /* 0x0:1  */ u_short game_over       : 1;
    /* 0x0:2  */ u_short game_over_pre   : 1;
    /* 0x0:3  */ u_short ending_normal   : 1;
    /* 0x0:4  */ u_short ending_hard     : 1;
    /* 0x0:5  */ u_short plyr_damage     : 1;
    /* 0x0:6  */ u_short plyr_door       : 1;
    /* 0x0:7  */ u_short pause           : 1;
    /* 0x0:8  */ u_short menu            : 1;
    /* 0x0:9  */ u_short map             : 1;
    /* 0x0:a  */ u_short dbg_menu        : 1;
    /* 0x0:b  */ u_short ene_dead        : 1;
    /* 0x0:c  */ u_short photo           : 1;
    /* 0x0:d  */ u_short movie_room_menu : 1;
    /* 0x0:e  */ u_short _unused_flags   : 2;
    /* 0x2    */ short   scene_no;
    /* 0x4    */ short   effect_mode_time;
    /* 0x6    */ short   event_stop_cnt;
} PHASE_CHANGE_REQS;

typedef struct                      /* 0x2 */
{
    /* 0x0:0 */ u_short mission : 1;
    /* 0x0:1 */ u_short _unused : 15;
} PHASE_CANGE_REQ_OUTGAME;

/* --------------------------------------------------------------------------
 *  The shared globals (defined in main/glob.c).
 * ------------------------------------------------------------------------ */
extern PAD_STRUCT      pad[2];              /* data 316c80 */
extern u_short        *key_now[32];         /* data 317000 */
extern u_short        *key_bak[32];         /* data 317080 */
extern SYS_WRK         sys_wrk;             /* data 317100 */
extern OPTION_WRK      opt_wrk;             /* data 317120 */
extern CAM_CUSTOM_WRK  cam_custom_wrk;      /* data 317130 */
extern DEBUG_WRK       debug_wrk;           /* data 317140 */
extern DEBUG_VAR       debug_var;           /* data 2db2b0 */
extern PLYR_WRK        plyr_wrk;            /* data 33cd90 */
extern SIS_WRK         sis_wrk;             /* data 34fc00 */
extern GAME_COSTUME    GameCostume;         /* bss 4bbce0 */

/* Defined by ingame/ingame.c, not glob.c -- ZERO2.MAP places it in ingame.o's
 * .data.  The declaration stays here because the type does. */
extern INGAME_WRK      ingame_wrk;          /* data 3186b0 (owner: ingame.o) */

/* phase_change_reqs (sbss 3f4d18) and OutPhaseChangeFlg (sbss 3f4d20) are
 * deliberately absent: both are file-statics of ingame.c.  ZERO2.MAP lists no
 * global symbol for either, and every reader reaches them through the
 * SendIngame / SetIngame / CheckIngame accessors in that file.  Only the two
 * struct types are shared, which is why they are declared here. */

#endif /* _COMMON_VARIABLE_H */
