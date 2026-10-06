/* ==========================================================================
 *  ingame/menu/tim_dat/menu_radio_dat.h
 *
 *  The in-game menu's crystal-radio page sprites and caption positions
 *  (menu_radio_dat.o).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_MENU_RADIO_DAT_H
#define _INGAME_MENU_TIM_DAT_MENU_RADIO_DAT_H

#include "../../../graphics/graph2d/g2d_draw.h"      /* SPRT_DAT */

extern SPRT_DAT menu_radio_tex[37];         /* data   32a998 */

/* Where the button caption plate sits, per GetLanguage() -- 0 English,
 * 1 French, 2 German, 3 Spanish, 4 Italian, the same order the five
 * MENU_RADIO_*_PK2 files are in.  Only the x moves; the y stays with the
 * sprite record.
 *
 * `const` at namespace scope needs the extern declaration to keep external
 * linkage in C++, which is why these are declared here rather than in the
 * .c -- they really are .rodata in the ROM. */
extern const int play_cap_tbl[5];           /* rdata  3beb40 */
extern const int stop_cap_tbl_1[5];         /* rdata  3beb58 */
extern const int stop_cap_tbl_2[5];         /* rdata  3beb70 */

#endif /* _INGAME_MENU_TIM_DAT_MENU_RADIO_DAT_H */
