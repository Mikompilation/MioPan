/* ==========================================================================
 *  debug/debug_menu.h
 *
 *  Generic nested debug-menu structures and renderer entry points.
 *
 *  A DEBUG_MENU is a fixed 20-slot table of DEBUG_SUB_MENU rows terminated by
 *  a row named "_end_".  Each row's `attr` says what `child` points at and how
 *  the row is drawn and edited; see the DBM_ATTR_* bits below.  Menus chain
 *  upwards through `parent`, and `kai` is the nesting depth recomputed every
 *  frame by DrawDbgMenu() (it drives the per-level panel offset and priority).
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _DEBUG_DEBUG_MENU_H
#define _DEBUG_DEBUG_MENU_H

#include "../sdk/scetypes.h"

/* --------------------------------------------------------------------------
 *  DEBUG_SUB_MENU::attr bits.  The ROM's own macro names are not in the
 *  symbol table, so these names are ours; the values are read off the menu
 *  tables in .data and the bit tests in DrawDbgMenu()/DrawDbgMenuSub().
 * ------------------------------------------------------------------------ */
#define DBM_ATTR_MENU     0x00001000U   /* child is a DEBUG_MENU * to descend into */
#define DBM_ATTR_SWITCH   0x00002000U   /* child is an int * drawn as " ON"/"OFF"   */
#define DBM_ATTR_VALUE    0x00008000U   /* child is an int * (or float *) to edit   */
#define DBM_ATTR_ONESHOT  0x00010000U   /* edit on the initial press only           */
#define DBM_ATTR_FLOAT    0x00020000U   /* qualifies DBM_ATTR_VALUE: child is float* */
#define DBM_ATTR_FUNC     0x00040000U   /* child is DEBUG_MENU *(*)(char *name)     */
#define DBM_ATTR_WRAP     0x00080000U   /* wrap at the limits instead of clamping   */

/* Rows carrying a value rather than a submenu: confirm on one of these steps
 * back out to the parent instead of descending. */
#define DBM_ATTR_LEAF     0x0000e000U
/* Rows left/right edits without DBM_ATTR_ONESHOT (auto-repeat every frame). */
#define DBM_ATTR_EDITABLE 0x0000a000U

typedef struct DEBUG_MENU DEBUG_MENU;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ char  *name;
    /* 0x04 */ u_int  attr;
    /* 0x08 */ void  *child;
    /* 0x0c */ float  nmin;
    /* 0x10 */ float  nmax;
    /* 0x14 */ float  nadd;
} DEBUG_SUB_MENU;

struct DEBUG_MENU                   /* 0x200 */
{
    /* 0x000 */ DEBUG_MENU    *parent;
    /* 0x004 */ int           *off_num;   /* external cursor store, may be null */
    /* 0x008 */ char          *title;
    /* 0x00c */ DEBUG_SUB_MENU submenu[20];
    /* 0x1ec */ int            mnum;      /* rows before "_end_"                */
    /* 0x1f0 */ int            kai;       /* nesting depth (0 = root)           */
    /* 0x1f4 */ int            max;       /* widest row, in characters          */
    /* 0x1f8 */ int            pos;       /* cursor row                         */
};

extern DEBUG_MENU dbg_menu_main;
extern DEBUG_MENU dbg_disp_main;
extern DEBUG_MENU dbg_item_main;
extern DEBUG_MENU dbg_film_item;
extern DEBUG_MENU dbg_recovery_item;
extern DEBUG_MENU dbg_event_item;
extern DEBUG_MENU dbg_event_item2;
extern DEBUG_MENU dbg_event_item3;
extern DEBUG_MENU dbg_event_item4;
extern DEBUG_MENU dbg_mem_main;
extern DEBUG_MENU dbg_ene_main;

void DebugInit(void);
DEBUG_MENU *GetNowMenu(void);
void DrawDbgMenuSub(DEBUG_MENU *wlp, int fl);
int DrawDbgMenu(void);
void DbmSave(DEBUG_MENU *in, char *path, char *fname, char *label);

#endif /* _DEBUG_DEBUG_MENU_H */
