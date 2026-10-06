/* ==========================================================================
 *  outgame/loadgame.h
 *
 *  Load-game screen (loadgame.o).  Owns the memory-card check / header load /
 *  snapshot load / file select / confirm state machine behind
 *  GID_TITLE_LOADGAME, and draws it through the shared save_load_disp.c
 *  primitives.
 *
 *  Only the eight symbols ZERO2.MAP exports are declared here; every step
 *  handler is static to loadgame.c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_LOADGAME_H
#define _OUTGAME_LOADGAME_H

#include "eetypes.h"

void LoadGameInit(void);
void LoadGameEnd(void);
void LoadGameMain(void);
void LoadGameDispMain(void);

/* SAVE_LOAD_PK2 + GetLanguage() -- the screen's own texture pak. */
void GetLoadGameTexMem(void);
void LoadGameDataLoadReq(void);
int  LoadGameDataLoadWait(void);
void ReleaseLoadGameTexMem(void);

#endif /* _OUTGAME_LOADGAME_H */
