/* ==========================================================================
 *  ingame/menu/tim_dat/menu_top_dat.h
 *
 *  The in-game menu's shared sprite table (menu_top_dat.o).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_MENU_TOP_DAT_H
#define _INGAME_MENU_TIM_DAT_MENU_TOP_DAT_H

#include "../../../graphics/graph2d/g2d_draw.h"      /* SPRT_DAT */

/* Every menu page draws from this one table; menu.c owns 0..11 and 71..82,
 * menu_top.c the rest.  All of it samples the pak at MENU_BG_TEX_ADRS except
 * 71..82, which come from MENU_TOUROU_TEX_ADRS. */
extern SPRT_DAT menu_top[83];       /* data 32ae38 */

#endif /* _INGAME_MENU_TIM_DAT_MENU_TOP_DAT_H */
