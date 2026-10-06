/* ==========================================================================
 *  ingame/menu/tim_dat/cam_level_up_point.h
 *
 *  What a camera upgrade costs (cam_level_up_point.o).
 *
 *  Two tables of spirit-point thresholds, indexed by the grade the upgrade
 *  would move *to* -- so entry 0 is always 0 (the stock grade costs nothing)
 *  and menu_cam_edit.o always reads `[grade + 1]`.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).  The object's
 *  whole .text is fixed_array boilerplate, so these two tables are the entire
 *  translation unit.
 * ======================================================================== */

#ifndef _INGAME_MENU_TIM_DAT_CAM_LEVEL_UP_POINT_H
#define _INGAME_MENU_TIM_DAT_CAM_LEVEL_UP_POINT_H

/* The three basic-performance ladders, in GetMenuCamBasicLv()'s order:
 * 0 capture radius, 1 stock grade (the accumulator), 2 sensitivity. */
extern int cam_base_status_point[3][4];     /* rodata 3a1b40 */

/* One ladder per sub-function lens, indexed by CAMERA_SUB_FUNC_ENUM. */
extern int cam_sp_shot_point_tbl[10][4];    /* rodata 3a1b70 */

#endif /* _INGAME_MENU_TIM_DAT_CAM_LEVEL_UP_POINT_H */
