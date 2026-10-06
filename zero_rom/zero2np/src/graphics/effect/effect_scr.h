/* ==========================================================================
 *  graphics/effect/effect_scr.h
 *
 *  The screen-filter machines: whole-frame blur / focus / deform / contrast /
 *  nega / dither / overlap / fade frame, the whole-screen colour fades, the
 *  screen saver and the brightness filter.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015e760.
 * ======================================================================== */

#ifndef _INGAME_SCREEN_SAVER_H
#define _INGAME_SCREEN_SAVER_H

#include <sys/types.h>                      /* u_char / u_int */

#include "effect.h"                         /* EFFECT_CONT */

/* The blur/focus envelopes and the deform bookkeeping (types.txt). */
struct EFF_BLUR                     /* 0x28 */
{
    /* 0x00 */ u_int flow;
    /* 0x04 */ u_int cnt;
    /* 0x08 */ u_int in;
    /* 0x0c */ u_int keep;
    /* 0x10 */ u_int out;
    /* 0x14 */ u_int alp;
    /* 0x18 */ float scl;
    /* 0x1c */ float rot;
    /* 0x20 */ float cx;
    /* 0x24 */ float cy;
};

struct EFF_FOCUS                    /* 0x18 */
{
    /* 0x00 */ u_int flow;
    /* 0x04 */ u_int cnt;
    /* 0x08 */ u_int in;
    /* 0x0c */ u_int keep;
    /* 0x10 */ u_int out;
    /* 0x14 */ u_int max;
};

struct EFF_DEFORM                   /* 0x4 */
{
    /* 0x0 */ u_char type;
    /* 0x1 */ u_char otype;
    /* 0x2 */ u_char init;
    /* 0x3 */ u_char pass;
};

extern EFF_BLUR   eff_blur;                 /* data 2fd500 */
extern EFF_FOCUS  eff_focus;                /* data 2fd528 */
extern EFF_DEFORM eff_deform;               /* sdata 3eff80 */
extern short      overlap_passflg[2];       /* sdata 3eff88 */

void InitEffectScr(void);
void InitEffectScrEF(void);

/* ---- whole-screen colour fades (SetParam wrappers) ---------------------- */
void SetWhiteOut(void);
void SetWhiteIn(void);
void SetBlackOut(void);
void SetBlackIn(void);
void SetWhiteOut2(int time);
void SetWhiteIn2(int time);
void SetBlackOut2(int time);
void SetBlackIn2(int time);
void SetFlash(void);

/* ---- the per-slot machines EffectControl() drives ------------------------ */
void SetBlackFilter(EFFECT_CONT *ec);
void SetBlur(EFFECT_CONT *ec);
void RunBlur(EFFECT_CONT *ec);
void SetFocus(EFFECT_CONT *ec);
void RunFocus(EFFECT_CONT *ec);
void SetDeform(EFFECT_CONT *ec);
void SetContrast2(EFFECT_CONT *ec);
void SetContrast3(EFFECT_CONT *ec);
void SetNega(EFFECT_CONT *ec);
void SetOverRap(EFFECT_CONT *ec);
void SetForcusDepth(EFFECT_CONT *ec);
void SetDither3(EFFECT_CONT *ec);
void SetFadeFrame(EFFECT_CONT *ec);

/* ---- the request wrappers ------------------------------------------------ */
void CallBlur(int type, int wait, u_char alpha, float scale, float rot);
void CallBlur2(int in, int keep, int out, u_char alpha, float scale, float rot);
void CallBlur3(int in, int keep, int out, u_char alpha, float scale, float rot,
               float cx, float cy);
void CallFocus(int type, int wait, int gap);
void CallFocus2(int in, int keep, int out, int max);

/* The screen warp while the player is being held -- the ROM feeds it the
 * remaining grab time as its strength, so it eases off as she breaks free.
 * The last two parameters are accepted and dropped; the ROM's own call
 * passes only the envelope through SetEffects_DEFORM(4, ...). */
void CallDeform2(int in, int keep, int out, int type, int max);

/* The colour inversion a ghost's blow leaves behind. */
void *CallNega2(int in, int keep, int out);
void *CallNega(int time);

/* ---- the direct drawing helpers ------------------------------------------ *
 * SubBlur() is the radial smear of the shutter flash -- `alpha` is its
 * strength, `scale` and `rot` how far and which way the copies are thrown,
 * (cx, cy) the centre they are thrown from, and bPhotoType selects the
 * photo-phase variant.  SubContrast2() lifts the whole frame, and
 * SubFadeFrame() darkens the border of it at the given priority. */
void SubBlur(int type, u_char alpha, float scale, float rot,
             float cx, float cy, int bPhotoType);
void SubContrast2(u_char col, u_char alp);
void SubContrast3(u_char col, u_char alp);
void SubNega(u_char r, u_char g, u_char b, u_char alp, u_char alp2);
void SubDeform(int type, float rate, u_char alp);
void SubDither3(int type, float alp, float spd, u_char alpmx, u_char colmx);
void SubFadeFrame(u_char alp, u_int pri);

/* Declared by the ROM and empty in this prototype. */
void ChangeMonochrome(int sw);

void ScreenSaverDraw(void);

/* effect_scr.o's own last export (0x165108): the option screen's brightness
 * slider, applied as a whole-frame add or subtract every frame. */
void BrightnessAdjustmentFilterDraw(void);

#endif /* _INGAME_SCREEN_SAVER_H */
