/* ==========================================================================
 *  outgame/tim_dat/title_dat.h
 *
 *  Title-screen sprite source data.  These compact SPRT_DAT records are baked
 *  asset coordinates recovered from SLES_523.84.
 * ======================================================================== */

#ifndef _OUTGAME_TIM_DAT_TITLE_DAT_H
#define _OUTGAME_TIM_DAT_TITLE_DAT_H

#include "../../graphics/graph2d/g2d_draw.h"  /* SPRT_DAT */

extern SPRT_DAT title_top[29];       /* data 35e5e0 */
extern int      title_left_x_tbl[8][5];
extern int      title_right_x_tbl[8][5];
extern int      newgame_left_x_tbl[2][5];    /* rdata 3e68d0 */
extern int      newgame_right_x_tbl[2][5];   /* rdata 3e68f8 */

#endif /* _OUTGAME_TIM_DAT_TITLE_DAT_H */
