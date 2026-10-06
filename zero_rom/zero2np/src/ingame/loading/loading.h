/* ==========================================================================
 *  ingame/loading/loading.h
 *
 *  In-game loading screen: step machine (LoadingCtrlInit/Main), per-frame
 *  draw (LoadingDispMain), and the texture get/req/wait/release quad shared
 *  with the other screen modules (loading.c).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_LOADING_LOADING_H
#define _INGAME_LOADING_LOADING_H

void LoadingInit(void);
void LoadingCtrlInit(void);
void LoadingCtrlMain(void);
void LoadingDispMain(void);
void GetLoadingTexMem(void);
void LoadingTexLoadReq(void);
int  LoadingTexLoadWait(void);
void ReleaseLoadingTexMem(void);

#endif /* _INGAME_LOADING_LOADING_H */
