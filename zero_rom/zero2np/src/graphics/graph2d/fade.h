/* ==========================================================================
 *  graphics/graph2d/fade.h
 *
 *  Full-screen colour fade (fade.c): the fade-control state block and the
 *  request / per-frame API.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_FADE_H
#define _GRAPHICS_GRAPH2D_FADE_H

#include <sys/types.h>              /* u_char / u_int */

/* --------------------------------------------------------------------------
 *  Fade-control block.  fade_state: 0 idle, 1 fading in, 3 fading out.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0xc */
{
    /* 0x0 */ u_char fade_state;
    /* 0x1 */ u_char alpha;
    /* 0x2 */ u_char change_alp;
    /* 0x3 */ u_char r;
    /* 0x4 */ u_char g;
    /* 0x5 */ u_char b;
    /* 0x8 */ int    timer;
} FADE_MODE_CTRL;

extern FADE_MODE_CTRL fade_ctrl;

/* --------------------------------------------------------------------------
 *  API.
 * ------------------------------------------------------------------------ */
void FadeCtrlInit(void);
void FadeMain(void);
int  GetFadeState(void);
void FadeInReq(u_char r, u_char g, u_char b, u_int fade_in_time);
void FadeOutReq(u_char r, u_char g, u_char b, u_int fade_out_time);

#endif /* _GRAPHICS_GRAPH2D_FADE_H */
