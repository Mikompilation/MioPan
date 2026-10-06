/* ==========================================================================
 *  graphics/graph2d/tim_dat/cap_group_dat.h
 *
 *  Declares the caption-group layout data (defined in cap_group_dat.c): the
 *  per-group button-control table cap_btn_group_ctrl[] and the world-space
 *  caption anchor positions cap_world_pos_x/y[][], used by the action-caption
 *  drawer (draw_caption.c) to place grouped button prompts.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPH2D_TIM_DAT_CAP_GROUP_DAT_H
#define _GRAPH2D_TIM_DAT_CAP_GROUP_DAT_H

#include "../draw_cmn.h"            /* CAP_BTN_GROUP_CTRL */

extern CAP_BTN_GROUP_CTRL cap_btn_group_ctrl[15];   /* data  2d8a78 */
extern int                cap_world_pos_x[15][5];    /* rdata 3a1d10 */
extern int                cap_world_pos_y[15][5];    /* rdata 3a1e40 */

#endif /* _GRAPH2D_TIM_DAT_CAP_GROUP_DAT_H */
