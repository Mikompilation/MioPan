/* ==========================================================================
 *  debug/zero2_debug.h
 *
 *  Debug menu/control entry points.
 * ======================================================================== */

#ifndef _DEBUG_ZERO2_DEBUG_H
#define _DEBUG_ZERO2_DEBUG_H

/* MEMORY_DISP submenu switches: which allocator's meter EachDebugMain() draws
 * this frame.  Nothing stops more than one being on at once -- they overlap. */
extern int dbg_spu_mem_disp;        /* sdata 3ef970 */
extern int dbg_system_mem_disp;     /* sdata 3ef974 */
extern int dbg_mdl_mem_disp;        /* sdata 3ef978 */
extern int dbg_cmn_mem_disp;        /* sdata 3ef97c */
extern int dbg_iop_mem_disp;        /* sdata 3ef980 */

/* ENEMY IN_OUT menu (dbg_ene_main): which enemy record to force in or out. */
extern int dbg_ene_no;              /* sdata 3ef984 */
extern int dbg_enemy_button;        /* sdata 3ef98c */

void InitDebug(void);
void DebugInit(void);
void DebugMenu(void);
void DebugMain(void);
int  DebugEnd(void);

/* Per-frame debug overlay, run from main.c in every phase. */
void EachDebugMain(void);

/* Rect/line callbacks the allocator meters draw through. */
void SPU_draw_rect_func(int x, int y, int w, int h, int rgba);
void SPU_draw_line_func(int x1, int y1, int x2, int y2, int rgba);

#endif /* _DEBUG_ZERO2_DEBUG_H */
