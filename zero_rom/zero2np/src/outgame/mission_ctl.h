/* ==========================================================================
 *  outgame/mission_ctl.h
 *
 *  Mission mode's controller (mission_ctl.c): the start banner, the score /
 *  time accumulators and the four result screens.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_MISSION_CTL_H
#define _OUTGAME_MISSION_CTL_H

/* Run the installed state once.  Returns -1 when the sequence is over, 0
 * otherwise. */
int  MisProc(void);                 /* 0x213798 */

/* Per-shot scoring, called from the camera. */
void MisSetScore(int iTotalScore);  /* 0x213800 */
int  MisGetScore(void);             /* 0x213c70 */
int  MisGetShot(void);              /* 0x213c78 */

/* 0 = failed, non-zero = cleared.  Latches the clear time and works out the
 * rank the result screen shows. */
void MisSetClearType(int iType);    /* 0x2138c8 */
int  MisGetRankLast(int iMissionID, int iTime, int iScore, int iShot); /* 0x213820 */

/* The start-banner sequence. */
int  MisStInit(void);               /* 0x213a90 */
void MisStTerm(void);               /* 0x213b08 */
void *MisGetTexPtr(void);           /* 0x213c68 */

/* The ending sequence. */
int  MisEnInit(void);               /* 0x214180 */
void MisEnTerm(void);               /* 0x2141d0 */

/* Which two result screens are being cross-faded: `iInDisp` is coming in and
 * `iOutDisp` going out; -1 means "nothing". */
void MisCtlSetDisp(int iInDisp, int iOutDisp);  /* 0x213b30 */

/* Non-zero when every mission is cleared and the flag has not been awarded. */
int  MisCheckClearAll(void);        /* 0x213d00 */

/* The individual states, exported because the ROM exports them; nothing
 * outside the file installs one directly. */
int  MisStSubInit(void);            /* 0x213978 */
int  MisStSubLoadWait(void);        /* 0x2139b0 */
int  MisStSubEnd(void);             /* 0x2139f0 */
int  MisStSubExec(void);            /* 0x213a18 */
int  MisEnSubInit(void);            /* 0x213db0 */
int  MisEnSubLoadWait(void);        /* 0x213e08 */
int  MisEnSubEnd(void);             /* 0x213f10 */
int  MisEnSubMiss(void);            /* 0x213f40 */
int  MisEnSubExec(void);            /* 0x213fa0 */
int  MisEnSubAllClear(void);        /* 0x214088 */
int  MisEnSubAllClearS(void);       /* 0x214120 */

#endif /* _OUTGAME_MISSION_CTL_H */
