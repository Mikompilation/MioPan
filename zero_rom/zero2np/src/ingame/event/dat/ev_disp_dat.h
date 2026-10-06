/* ==========================================================================
 *  ingame/event/dat/ev_disp_dat.h
 *
 *  Declarations for the three tables ev_disp_dat.c defines.  The ROM records
 *  no header for this file, so this mirrors the source-tree layout the way
 *  ev_talk_dat.h does.  ev_disp.c is the only consumer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_EVENT_DAT_EV_DISP_DAT_H
#define _INGAME_EVENT_DAT_EV_DISP_DAT_H

#include "../../../graphics/graph2d/g2d_draw.h"          /* SPRT_DAT */
#include "../../../graphics/graph3d/ctl/fixed_array.h"   /* reference_fixed_array */

/* The two framings the full-screen event image can be placed with; the
 * base_label argument of EvDisp2DStartReq() picks one.  x/y here are only
 * defaults -- EvDisp2DStartReq() overwrites both from its own arguments. */
extern SPRT_DAT ev_disp2d_dat[2];                       /* data 30dbf8 */

/* The chapter title card, as two side-by-side halves of one 255x256 texture.
 * Unlike the above these are drawn exactly as written. */
extern SPRT_DAT ev_chapter_dat[2];                      /* data 30dc38 */

/* Chapter title TIM2 file, indexed by chapter number.  GetLanguage() is added
 * to the entry, so the five language variants of a chapter sit consecutively
 * and the entries are spaced five apart. */
extern reference_fixed_array<int, 11> chapter_tim_file; /* sdata 3f0330 */

#endif /* _INGAME_EVENT_DAT_EV_DISP_DAT_H */
