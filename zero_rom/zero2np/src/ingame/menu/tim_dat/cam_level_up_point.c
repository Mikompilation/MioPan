// FILE: /home/zero_rom/zero2np/src/ingame/menu/tim_dat/cam_level_up_point.c
//
// What a camera upgrade costs (cam_level_up_point.o).  A data-only
// translation unit: its whole 0xd4 of .text is the fixed_array assert
// boilerplate every TU that includes fixed_array.h carries, so these two
// tables are the file.
//
// Both are indexed by the grade the upgrade would move *to*, which is why
// entry 0 is 0 in every row and menu_cam_edit.o always subscripts with
// `grade + 1`.  A grade-3 item has no next threshold and is tested for by
// name before the lookup.
//
// The ROM declares neither `const` -- globals.txt lists them at file scope
// with external linkage and they land in .rodata only because the linker
// puts read-only initialised data there.
//
// Read straight out of SLES_523.84's .rodata at 0x3a1b40 / 0x3a1b70.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "cam_level_up_point.h"

/* rodata 3a1b40 */
int cam_base_status_point[3][4] =
{
    {    0,  6000, 20000, 30000 },      /* capture radius   */
    {    0,  4000, 10000, 21000 },      /* stock grade      */
    {    0,  7000, 24000, 38000 },      /* sensitivity      */
};

/* rodata 3a1b70.
 *
 * Row 0 is CAMERA_SUB_FUNC_NONE and is dead: the only subscripts that ever
 * reach here come out of menu_cam_edit.o's disp_lens_data[], which is built
 * by walking bits 1..9 of CCameraPowerUp::mTemperedRenzFlg.  It duplicates
 * row 1, the way rows 8 and 9 duplicate each other. */
int cam_sp_shot_point_tbl[10][4] =
{
    {    0,  8000, 17000, 28000 },      /* 0 NONE -- unreachable */
    {    0,  8000, 17000, 28000 },      /* 1                */
    {    0,  8000, 19000, 30000 },      /* 2                */
    {    0,  8000, 18000, 29000 },      /* 3                */
    {    0, 30000, 44000, 55000 },      /* 4                */
    {    0, 35000, 50000, 65000 },      /* 5                */
    {    0, 10000, 20000, 30000 },      /* 6                */
    {    0, 12000, 25000, 34000 },      /* 7                */
    {    0, 45000, 70000, 90000 },      /* 8                */
    {    0, 45000, 70000, 90000 },      /* 9                */
};
