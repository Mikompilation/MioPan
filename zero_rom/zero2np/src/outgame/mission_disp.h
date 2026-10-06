/* ==========================================================================
 *  outgame/mission_disp.h
 *
 *  The mission-mode HUD and the four result screens (mission_disp.o).  Every
 *  symbol in the object is exported; there are no statics beyond the three
 *  timer variables, and no work block.
 *
 *  Note two signatures the earlier pass guessed wrong: MisDispNum() takes the
 *  digit count *before* the offsets, and MisDispTime() takes h/m/s rather than
 *  a packed frame count.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_MISSION_DISP_H
#define _OUTGAME_MISSION_DISP_H

#include <sys/types.h>              /* u_char */

#include "../ingame/menu/anim_2d.h" /* ALPHA_ANIM_TBL */

/* MisDispSetFlg() / MisDispDeleteFlg() bits.  Only bit 1 is read -- it is what
 * gates the in-mission readout -- and mission_sel.c clears bit 0 and bit 1
 * together when a mission starts. */
#define MISDISP_FLG_TIMER   2

/* The banner/result animation counter, shared by the start banner and all four
 * result screens (each *Init() resets it, each draw advances it). */
void MisDispSetTime(int iTime);                             /* 0x2142c8 */
int  MisDispGetTime(void);                                  /* 0x2142d0 */

/* Right-aligned number in the mission-mode digit sprites.  `iFlg` non-zero
 * forces all `iKeta` digits (leading zeros); zero stops at the first zero
 * quotient. */
void MisDispNum(int iNum, int iKeta, int iOffX, int iOffY,
                u_char ucAlpha, int iFlg);                  /* 0x2142d8 */

/* h:m:s in the same digits, at the result screens' fixed columns. */
void MisDispTime(int iHour, int iMin, int iSec, int iOffX, int iOffY,
                 u_char ucAlpha);                           /* 0x2143b0 */

/* Anim2D_CalcNowAlpha() with the ends held rather than extrapolated. */
u_char MisDispGetAnimAlpha(const ALPHA_ANIM_TBL *pAnimList, int iTime); /* 0x214450 */

/* The in-mission timer/score readout. */
void MisDispTimeInit(void);                                 /* 0x2144e0 */
int  MisDispGetTimerCnt(void);                              /* 0x2144e8 */
void MisDispSetFlg(int iFlg);                               /* 0x2144f0 */
void MisDispDeleteFlg(int iFlg);                            /* 0x214500 */
void MisDispTimeProc(void);                                 /* 0x214518 */

/* The mission-start banner. */
void MisDispStartInit(void);                                /* 0x2146a0 */
void MisDispStart(u_char ucAlpha, void *pTexPtr);           /* 0x2146a8 */

/* The four result screens. */
void MisDispClearInit(void);                                /* 0x2148c8 */
void MisDispClear(u_char ucAlpha, int iTime, int iScore, int iShot,
                  void *pMissionTex, void *pRsCmnTex);      /* 0x2148d0 */
void MisDispClearAll(u_char ucAlpha, int iTime, int iScore, int iShot,
                     void *pMissionTex, void *pRsCmnTex);   /* 0x214d50 */
void MisDispClearAllS(u_char ucAlpha, int iTime, int iScore, int iShot,
                      void *pMissionTex, void *pRsCmnTex);  /* 0x214e98 */
void MisDispBadEnd(u_char ucAlpha, int iTime, int iScore, int iShot,
                   void *pMissionTex, void *pRsCmnTex);     /* 0x214f88 */

#endif /* _OUTGAME_MISSION_DISP_H */
