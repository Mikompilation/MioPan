/* ==========================================================================
 *  outgame/SpriteCmn.h
 *
 *  Public interface for outgame/SpriteCmn.c: the shared out-game sprite / UI
 *  helper layer.  A screen registers its SPRT_DAT source table with
 *  SpCmnStart(), then draws by label through the SpCmnDraw* family; the
 *  remaining helpers cover full-screen fills, drop-shadowed text/number
 *  printing, message centring, texture-buffer (de)allocation and back-buffer
 *  snapshot / restore.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _OUTGAME_SPRITECMN_H
#define _OUTGAME_SPRITECMN_H

#include <sys/types.h>              /* u_char / u_int */
#include "../graphics/graph2d/g2d_draw.h"   /* DISP_SPRT / SPRT_DAT */

/* Register the active sprite source table (indexed by label below). */
void  SpCmnStart(SPRT_DAT *pSprDat);

/* Resolve a label into ds, applying alpha / offset; iFlg selects additive alpha. */
void  SpCmnSetSprite(DISP_SPRT *ds, int iLabel, int off_x, int off_y,
                     u_char alpha, int iFlg);

/* Set + draw one sprite (optionally scaled about its origin). */
void  SpCmnDrawSprite(int iLabel, int off_x, int off_y, u_char alpha, int iFlg);
void  SpCmnDrawSpriteScale(int iLabel, int iOffX, int iOffY, float fScaleW, float fScaleH,
                           u_char ucAlpha, int iFlg);

/* Draw a contiguous run of labels [st..en]. */
void  SpCmnDrawRange(int st, int en, int off_x, int off_y, u_char alpha, int iFlg);

/* Full-screen black quad at the given alpha. */
void  SpCmnBlackOut(u_char ucAlpha);

/* Drop-shadowed number / message (shadow pass in colour 9). */
void  SpCmnPrintNumber_NK(int iData, int iNum, int iX, int iY, u_char ucColLabel, u_char ucAlpha,
                          int iPri, u_char ucMsgType, int ucZeroFlg);
void  SpCmnPrintMsg_K(int iMsgType, int iMsgID, int iX, int iY, int iColLabel, int iAlpha, int iPri);

/* Left edge that centres a message about iCenX. */
int   SpCmnGetCenterX(int iGroupLabel, int iMsgLabel, int iCenX);

/* Texture-buffer (de)allocation via the mem_util heap. */
void *SpCmnTexMemLoad(int iFileNo);
void *SpCmnTexMemReleaseSub(void *pTexAddr);

/* Back-buffer snapshot (EE) and full-screen restore (to VRAM). */
void *SpCmnGetScreen(void);
void  SpCmnDrawScreen(void *pScrAddr, int iVramAddr);

#endif /* _OUTGAME_SPRITECMN_H */
