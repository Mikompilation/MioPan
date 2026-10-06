/* ==========================================================================
 *  ingame/puzzle/kai/kai_pzl.h
 *
 *  The kai (twin-mirror) puzzle (kai_pzl.o): two ghosts to be turned to face
 *  each other.  Unlike the other four this one has no board of its own -- it
 *  drives the live 3D scene and draws only a message and a cancel window.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_PUZZLE_KAI_KAI_PZL_H
#define _INGAME_PUZZLE_KAI_KAI_PZL_H

#include "eetypes.h"

/* One row per game mode: the mode id and the per-frame handler that runs it. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int mode;
    /* 0x4 */ int (*func)(void);
} KAIPZL_MODE;

/* Per puzzle (Kai1/Kai2) and per ghost: the pose it starts in, the two poses
 * the left/right turns move it to, and the pose that counts as solved. */
typedef struct                      /* 0x10 */
{
    /* 0x0 */ int iInit;
    /* 0x4 */ int iRotL;
    /* 0x8 */ int iRotR;
    /* 0xc */ int iClear;
} KAIPZL_ROT;

void KaiPuzzleSetFadeCmn(int iNo, int iSt);
void KaiPuzzleSetFade(int iNo, int iSt, int iTime);
void KaiPuzzleSetFade2(int iNo, int iSt, int iTime, int iNext);
void KaiPuzzleSetFadeNextMode(int iNo, int iNext);
void KaiPuzzleFadeProc(void);
void KaiPuzzleExeInit(int puzzle_id);
void KaiPzlDrawCancelWindow(u_char alpha);
int  KaiPzlProc(void);
int  KaiPzlAnim(void);
int  KaiPzlClear(void);
int  KaiPzlDrawMsg(void);
int  KaiPzlCancel(void);
int  KaiPzlRelease(void);
int  KaiPzlTerm(void);
int  KaiPzlTerm2(void);
int  KaiPuzzleMain(void);

#endif /* _INGAME_PUZZLE_KAI_KAI_PZL_H */
