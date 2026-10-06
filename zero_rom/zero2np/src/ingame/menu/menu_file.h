/* ==========================================================================
 *  ingame/menu/menu_file.h
 *
 *  The in-game menu's collected-documents page (menu_file.o) -- five tabbed
 *  lists of everything the player has picked up to read, plus the three
 *  readers that open on top of them: the paged document reader, the
 *  photograph viewer and the map viewer.
 *
 *  Four exports; the fifty-eight helpers they are built from are static and
 *  stay in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MENU_MENU_FILE_H
#define _INGAME_MENU_MENU_FILE_H

#include "eetypes.h"
#include "menu_cmn.h"                           /* MENU_REF_CTRL */

#include "../item/dat/file_dat.h"               /* FILE_*_MAX / FILE_TYPE_MAX */
#include "../../graphics/graph3d/ctl/fixed_array.h"

/* One list row.  MenuFileSetDispData() rebuilds all five lists as compacted
 * copies of the per-type state arrays -- only the files the player holds, in
 * file-id order -- so a list walks without re-testing every id.
 *
 * Unlike DISP_MEMO_DATA's latched fields, both of these are read back:
 * GetMenuFileDispFileID() and GetMenuFileDispFileState() are the only way the
 * page reaches a row. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  file_id;
    /* 0x4 */ char state;           /* FILE_STATE_HAVE / FILE_STATE_READ    */
} MENU_FILE_DATA;

/* The five lists, one per file type, in FILE_TYPE_* order.  Sized to the
 * per-type maxima rather than to a common bound, which is what makes
 * GetMenuFileDispData() a five-way switch rather than a table lookup. */
typedef struct                      /* 0x500 */
{
    /* 0x000 */ fixed_array<MENU_FILE_DATA, FILE_POCKETBOOK_MAX> pocketbook;
    /* 0x150 */ fixed_array<MENU_FILE_DATA, FILE_SCRAP_MAX>      scrap;
    /* 0x2a0 */ fixed_array<MENU_FILE_DATA, FILE_OLDBOOK_MAX>    oldbook;
    /* 0x3e0 */ fixed_array<MENU_FILE_DATA, FILE_PHOTOGRAPH_MAX> photograph;
    /* 0x4b0 */ fixed_array<MENU_FILE_DATA, FILE_MAP_MAX>        map;
} DISP_FILE_DATA;

/* The page's own state.
 *
 * `tag_csr` is which of the five tabs is up, and it doubles as the file type
 * everywhere -- every list, cursor and message bank is indexed by it.
 *
 * `mode` is what the page is showing and indexes both menu_file_pad[] and
 * file_mode_disp[]: 0..4 are the four readers (with 0/1/2 sharing the
 * document reader), 5 the tab list itself and 6 the "you are not carrying
 * anything" message.  `sub_step` is the mode's own step ladder, which reuses
 * menu_wrk.step's numbering.
 *
 * top_csr[] and ref_ctrl[] are per tab, so switching tabs keeps each list
 * where it was. */
typedef struct                      /* 0x58 */
{
    /* 0x00 */ char   sub_step;             /* MENU_FILE_SUB_*              */
    /* 0x01 */ char   mode;                 /* MENU_FILE_MODE_*             */
    /* 0x02 */ char   next_mode;            /* what the fade is moving to   */
    /* 0x03 */ u_char cross_fade_flg;       /* live picture slot, 0 or 1    */
    /* 0x04 */ int    tag_csr;              /* the tab == the file type     */
    /* 0x08 */ fixed_array<int, FILE_TYPE_MAX>           top_csr;
    /* 0x1c */ fixed_array<MENU_REF_CTRL, FILE_TYPE_MAX> ref_ctrl;
} MENU_FILE_CTRL;

/* Two independent open/close fades: the page's own, and the inner one a
 * reader rides in on.  Same shape as MENU_MEMO_DISP. */
typedef struct                      /* 0x6 */
{
    /* 0x0 */ char   anim_step;         /* MENU_FILE_ANIM_*                 */
    /* 0x1 */ char   anim_timer;
    /* 0x2 */ u_char rgb;               /* the cursor pulse                 */
    /* 0x3 */ char   scroll_timer;
    /* 0x4 */ char   sub_anim_step;
    /* 0x5 */ char   sub_anim_timer;
} MENU_FILE_DISP;

/* menu_ctrl[] row 4: one frame of the page, and its drawing. */
void MenuFile(void);                        /* 0x1f4ad0 */
void MenuFileDisp(void);                    /* 0x1f5dd8 */

/* The page owns seven texture slots rather than one, so it has a single
 * release rather than the usual Liberate/Cancel pair.  menu.c's
 * MenuRelease() calls it. */
void MenuFileMemRelease(void);              /* 0x1f5c40 */

/* Claim and start the two always-resident paks -- the file-common art and the
 * tab page's own.  menu_top.c calls it as the hub fades out; the four reader
 * paks are loaded on demand instead. */
void MenuFileTexBackGroundLoad(void);       /* 0x1f4528 */

#endif /* _INGAME_MENU_MENU_FILE_H */
