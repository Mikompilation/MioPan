// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_map.c
//
// The in-game menu's map page (menu_map.o).  Forty-six bodies -- six exports
// and forty statics -- plus one .rodata table and two function-local ones.
// At 0x4310 of .text it is the largest translation unit in ingame/menu/.
//
// It is the standard menu shape (a menu_wrk.step ladder, an anim_step/
// anim_timer fade, a Get/LoadReq/LoadWait/Liberate texture quintet), wrapped
// round two things of its own: a scrolling sheet of room art, and a crosshair
// fixed at the middle of the screen that the sheet is moved underneath.
//
// Facts worth knowing before touching it:
//
//  * THE CURSOR DOES NOT MOVE -- the map does.  MenuMapRoomHitCheck() is
//    called every frame with the constants (320, 259), the centre of the map
//    viewport, and asks which room quad in map_area_dat[] contains that point
//    once the scroll offset and zoom have been applied.  That answer is
//    menu_map_ctrl.hit_room, and it drives the room name, the snapshot and
//    the highlight.  Nothing anywhere holds a cursor position.
//
//  * There are two independent axes of paging, and they index one table.
//    map_label_tbl[8][3] is areas down, floors across: MenuMapChange() (L2 /
//    R2) steps the row and MenuMapFloorChange() (L1 / R1) steps the column,
//    both wrapping and both skipping entries that are -1 or that
//    MenuMapInCheck() says the player has never been in.  Each first has to
//    find where it currently is, which is why both open with the same
//    nested search and the same "Error!!" assert if the sheet on screen is
//    not in the table at all.
//
//  * Both change functions are a SWITCH on flg with the flg == 1 case written
//    FIRST.  Two compares sit back-to-back under a single line marker, which
//    is the switch signature (see title.o's one_Title_Top), and the case
//    bodies are in code at 747 (next) then 769 (previous).  An if/else chain
//    would have tested in the other order.
//
//  * A sheet is drawn twice.  MenuMapMapDisp() walks room_info_dat[] once,
//    drawing every seen room that is NOT part of the highlighted group as a
//    flat black silhouette (alphar 0x46), and collecting the ones that ARE
//    into sel_room_group[20]; then it walks that list and draws each of them
//    twice more -- once tinted (0xf2, 0x96, 0x58) and once additively at
//    alpha * 0x26 / 128, which is the glow.  Twenty is a hard limit and the
//    ROM asserts past it rather than growing.
//
//  * A door is drawn if EITHER of its two rooms has been seen, and the door
//    type picks the art.  Types 0..4 are one leaf, 5..8 are two (the second
//    offset by the sprite's own w and/or h), and TYPE 9 IS A REAL EMPTY CASE
//    -- the jump table at 0x3be060 has a slot for it pointing at the break
//    label while `default` goes to the assert, so it is a door the map
//    deliberately does not mark.
//
//  * MenuMapDoubleDoorDisp()'s type 7 has no un-offset leaf: leaf 0 is drawn
//    at (x + w, y) and leaf 1 at (x, y + h).  Its three siblings all put leaf
//    0 at (x, y).  Measured from the line numbers (1926/1927 against
//    1914/1915), not inferred -- reproduced as found.
//
//  * The zoom is 1x or 2x and it is NOT a sprite scale on the sheet.
//    CalMapDispStartPos() reflects the scroll offset about the viewport
//    centre and doubles it, and every draw multiplies its own coordinates by
//    map_scall_dat[].big instead of .normal, so the whole sheet is laid out
//    at the larger size rather than magnified.  Only the player marker and
//    the save-point mark take a literal scw/sch of 2.0.
//
//  * MenuMapSetPosition() and several of the draws write the SAME tail for
//    both zoom levels and GCC cross-jumped it, keeping the chosen
//    MAP_SCALL_DAT member's ADDRESS in a register.  That is why Ghidra shows
//    a `(MAP_SCALL_DAT *)&...big` cast being dereferenced as `->normal`; the
//    source reads .big in the 2x arm and .normal in the 1x one.
//
//  * MenuMapDisp() declares an `int i` (functions.txt, register s4) that the
//    body never uses -- GCC put a rematerialised -1 there for one of the four
//    `disp_map_label != -1` tests and a second -1 in s0 for two more.  It is
//    omitted here rather than reproduced as an unused variable.
//
// The one texture the page does not own is the sheet itself: it arrives
// through menu_cmn.o's two cross-fade slots, which is what makes changing
// area or floor a dissolve rather than a cut, and MenuMapDisp() uploads
// whichever slot is live with PK2SendVram() before the walk.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_map.o.
// Line numbers in the trailing /* NNN */ comments are measured from the
// object's own $LM markers; where a statement had none (every subscript
// through fixed_array::operator[] is attributed to fixed_array.h 124/125
// instead) it carries no annotation.

#include "menu_map.h"

#include <stdio.h>

#include "menu.h"                               /* menu_wrk / MENU_BG_TEX_ADRS */
#include "menu_cmn.h"                           /* the cross-fade pair        */
#include "plyr_room_info.h"                     /* GetRoomInfo                */
#include "ghost_seal_door.h"                    /* GetGhostSealDoorState      */
#include "anim_2d.h"                            /* Anim2D_CalcNowAlpha        */
#include "tim_dat/menu_map_dat.h"               /* menu_map_tex[]             */
#include "tim_dat/menu_map_mapdata_dat.h"       /* menu_map_mapdata_tex[]     */
#include "tim_dat/door_point_dat.h"             /* menu_map_door_data[]       */
#include "tim_dat/save_point_dat.h"             /* map_save_point[]           */
#include "tim_dat/map_area_dat.h"               /* map_area_dat[]             */
#include "tim_dat/map_room_dat.h"               /* room_info_dat[] etc        */
#include "tim_dat/map_size_dat.h"               /* map_size_dat / map_scall_dat */
#include "tim_dat/ghost_seal_door_dat.h"        /* ghost_seal_door_data[]     */

#include "eetypes.h"                            /* u_int / u_long             */
#include "../map/hit_check_base.h"              /* HcBaseIsInTriXZ            */
#include "../plyr/player.h"                     /* GetPlyrAreaNo / GetPlyrFloor */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../common/variable.h"              /* pad / plyr_wrk             */
#include "../../graphics/draw_env.h"            /* GET_SCISSOR_REGISTER       */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnCapGroup_W          */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg                   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_MAP_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_MAP_STEP_INIT      0
#define MENU_MAP_STEP_LOAD      1
#define MENU_MAP_STEP_MAIN      2
#define MENU_MAP_STEP_MOVE      3

/* MENU_MAP_DISP::anim_step.  Only MAIN and END are tested by name; the rest
 * of the ladder is MenuInOutAnimCtrl()'s business. */
#define MENU_MAP_ANIM_MAIN      2
#define MENU_MAP_ANIM_OUT       3
#define MENU_MAP_ANIM_END       4

/* MENU_MAP_LOAD_CTRL::step. */
#define MENU_MAP_LOAD_REQ       0
#define MENU_MAP_LOAD_WAIT      1
#define MENU_MAP_LOAD_END       2

/* MENU_MAP_DISP::tri_anim_step -- the scroll arrows' own three-state fade. */
#define MENU_MAP_TRI_ANIM_OFF   0
#define MENU_MAP_TRI_ANIM_IN    1
#define MENU_MAP_TRI_ANIM_LOOP  2

/* MENU_MAP_DISP::map_scall_flg. */
#define MENU_MAP_SCALL_NORMAL   0
#define MENU_MAP_SCALL_BIG      1

/* How many map sheets exist.  Also the "is this a real sheet" test, because
 * an unmapped area comes back as -1 and reads as 255 unsigned. */
#define MAP_LABEL_NUM           17

/* map_label_tbl[] is areas down, floors across. */
#define MAP_AREA_NUM            8
#define MAP_FLOOR_NUM           3

/* MenuMapChange() / MenuMapFloorChange() direction. */
#define MENU_MAP_CHANGE_PREV    0
#define MENU_MAP_CHANGE_NEXT    1

/* The point the sheet is tested against: the centre of the map viewport.
 * 320 is the horizontal middle of the screen, 259 sits a little below the
 * vertical one because the window frame is not symmetric. */
#define MENU_MAP_CENTER_X       320.0f
#define MENU_MAP_CENTER_Y       259.0f

/* How far the sheet may be scrolled, and by how much per frame. */
#define MENU_MAP_SCROLL_SPEED   3.0f
#define MENU_MAP_SCROLL_MAX_X   420.0f
#define MENU_MAP_SCROLL_MAX_Y   359.0f
#define MENU_MAP_SCROLL_MIN_X   220.0f
#define MENU_MAP_SCROLL_MIN_Y   159.0f

/* menu_map_tex[] groups, named for what MenuMapMapDisp()'s helpers pull. */
#define MENU_MAP_TEX_TITLE          0   /* 0..1 own pak, 2..3 background pak */
#define MENU_MAP_TEX_WINDOW         4   /* 4..23                             */
#define MENU_MAP_TEX_WINDOW_NUM     20
#define MENU_MAP_TEX_SNAP           0x18
#define MENU_MAP_TEX_PLYR           0x19
#define MENU_MAP_TEX_SAVE_POINT     0x1c
#define MENU_MAP_TEX_BASE           0x1d    /* 0x1d..0x23                    */
#define MENU_MAP_TEX_SEAL_MARK      0x2c
#define MENU_MAP_TEX_TRI            0x2e    /* 0x2e..0x31, the four arrows   */
#define MENU_MAP_TEX_TRI_END        0x31
#define MENU_MAP_TEX_GLOW           0x37    /* 0x37..0x38                    */

/* How many rooms may be highlighted at once. */
#define SEL_ROOM_GROUP_NUM      20

/* The map viewport, as a SCISSOR_1 word: x 32..610, y 99..355.  MenuMapDisp()
 * clips the sheet to it and puts the old value back afterwards, because the
 * frame and the readouts are drawn outside it. */
#define MENU_MAP_SCISSOR        0x01a3006302600020ULL

/* The four scroll arrows, one per direction, indexing tri_pad_flg[]. */
#define MENU_MAP_TRI_UP         0
#define MENU_MAP_TRI_LEFT       1
#define MENU_MAP_TRI_RIGHT      2
#define MENU_MAP_TRI_DOWN       3
#define MENU_MAP_TRI_NUM        4

/* The arrows' fade: 20 frames in, 20 out, and 20 held at the top. */
#define MENU_MAP_TRI_ANIM_TIME  20
#define MENU_MAP_TRI_LOOP_TIME  39

/* menu_ctrl[] row the page hands back to when it closes. */
#define MENU_STEP_TOP           8

/* --------------------------------------------------------------------------
 *  File statics
 * ------------------------------------------------------------------------ */

static void *menu_map_bg_addr;                              /* sdata 3f2e20 */

/* Claimed by nothing.  MenuMapRelease() frees it and no other function in the
 * object touches it -- the sheet arrives through menu_cmn.o's cross-fade
 * slots instead, so this buffer was left over when that changed. */
static void *map_data_addr;                                 /* sdata 3f2e24 */

static void *snap_data_addr;                                /* sdata 3f2e28 */

static MENU_MAP_DISP menu_map_disp;                         /* bss   4b5bd0 */
static MENU_MAP_CTRL menu_map_ctrl;                         /* bss   4b5be8 */

/* Which sheet is which area on which floor.  A row is an area -- the grounds,
 * then the four houses, then the three Kurosawa sheets -- and a column is a
 * floor within it; -1 means that area has no sheet for that floor.  Row 0 is
 * the grounds, whose "second floor" is the basement (13) rather than a
 * middle storey, which is why its middle entry is the empty one. */
static const int map_label_tbl[MAP_AREA_NUM][MAP_FLOOR_NUM] =    /* rdata 3be168 */
{
    /* 0 */ {  0, -1, 13 },
    /* 1 */ {  1,  2,  3 },
    /* 2 */ {  4,  5,  6 },
    /* 3 */ {  7,  8,  9 },
    /* 4 */ { 10, 11, 12 },
    /* 5 */ { 14, -1, -1 },
    /* 6 */ { 15, -1, -1 },
    /* 7 */ { 16, -1, -1 },
};

/* --------------------------------------------------------------------------
 *  Forward declarations -- the object defines these after their callers.
 * ------------------------------------------------------------------------ */

static void MenuMapInit(void);
static void MenuMapCtrlInit(void);
static void MenuMapLoadCtrlInit(MENU_MAP_LOAD_CTRL *load_ctrl);
static void GetMenuMapTexMem(void **tex_addr, int data_label);
static void MenuMapTexLoadReq(void *tex_addr, int data_label);
static void MenuMapSnapLoadReq(void);
static int  MenuMapBgLoadWait(void);
static void MenuMapSnapLoadExe(void);
static void MenuMapPad(void);
static void MenuMapOutReq(void);
static int  MenuMapChange(int flg);
static int  MenuMapFloorChange(int flg);
static int  MenuMapInCheck(int map_label);
static int  MenuMapRoomHitCheck(float x, float y);
static void MenuMapSetPosition(float *pos, float x, float y);
static int  MenuMapCheckHouseCondition(int room_label);
static void LiberateMenuMapTexMem(void **tex_addr);
static void MenuMapDispInit(void);
static void MenuMapDispStartPos(void);
static void MenuMapTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuMapBaseDisp(int off_x, int off_y, u_char alpha);
static void MenuMapMapDisp(int map_label, int off_x, int off_y, u_char alpha);
static int  MenuMapDispAreaCheck(int map_label, float x, float y,
                                 float w, float h);
static int  MenuMapRoomGroupCheck(int room_group_label);
static void CalMapDispStartPos(float *map_x, float *map_y, char scall_flg);
static void MenuMapSavePointDisp(int map_label, float map_x, float map_y,
                                 u_char alpha);
static void MenuMapDoorDisp(int map_label, float map_x, float map_y,
                            u_char alpha);
static void MenuMapNormalDoorDisp(int map_label, float map_x, float map_y,
                                  u_char alpha,
                                  const MAP_DOOR_POINT *door_data);
static void MenuMapDoubleDoorDisp(int map_label, float map_x, float map_y,
                                  u_char alpha,
                                  const MAP_DOOR_POINT *door_data);
static void MenuMapGhostSealNormalDoorDisp(int map_label, float map_x,
                                           float map_y, u_char alpha,
                                           const MAP_DOOR_POINT *door_data);
static void MenuMapGhostSealDoubleDoorDisp(int map_label, float map_x,
                                           float map_y, u_char alpha,
                                           const MAP_DOOR_POINT *door_data);
static void MenuMapGhostSealMarkDisp(int map_label, float map_x, float map_y,
                                     u_char alpha, int ghost_seal_door_label);
static void MenuMapPlyrPosDisp(int map_label, float map_x, float map_y,
                               u_char alpha);
static void MenuMapCenterDisp(int off_x, int off_y, u_char alpha);
static void MenuMapCenterAnim(void);
static void MenuMapSnapShotDisp(int off_x, int off_y, u_char alpha);
static void MenuMapWindowDisp(int off_x, int off_y, u_char alpha);
static void MenuMapInfoDisp(int off_x, int off_y, u_char alpha);
static void MenuMapCaptionDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Setup
 * ------------------------------------------------------------------------ */

/* The page's own pak is per-language, and the file number is computed fresh
 * at both use sites rather than held -- the same shape title.o's logo has. */
static void MenuMapInit(void)                                           /* 262 */
{
    MenuMapCtrlInit();                                                  /* 266 */

    MenuCrossFadeInit();                                                /* 269 */

    GetMenuMapTexMem(&menu_map_bg_addr, MENU_MAP_PK2 + GetLanguage());  /* 272 */
    MenuMapTexLoadReq(menu_map_bg_addr, MENU_MAP_PK2 + GetLanguage());  /* 274 */
}

static void MenuMapCtrlInit(void)                                       /* 282 */
{
    menu_map_ctrl.plyr_map = (char)GetPlyrMapLabel();                   /* 285 */
    menu_map_ctrl.cross_fade_flg = 0;                                   /* 287 */
    menu_map_ctrl.hit_room = -1;                                        /* 288 */

    MenuMapLoadCtrlInit(&menu_map_ctrl.snap_load);                      /* 289 */

    menu_map_ctrl.map_area_id = 0;
}

static void MenuMapLoadCtrlInit(MENU_MAP_LOAD_CTRL *load_ctrl)          /* 298 */
{
    load_ctrl->step = MENU_MAP_LOAD_REQ;                                /* 301 */
    load_ctrl->tex_label = -1;                                          /* 302 */
}

/* Claim a buffer the size of the file, dropping whatever was there first --
 * the snapshot slot is re-claimed every time the crosshair crosses into a
 * different room. */
static void GetMenuMapTexMem(void **tex_addr, int data_label)           /* 312 */
{
    if (*tex_addr != nullptr) {                                         /* 315 */
        LiberateMenuMapTexMem(tex_addr);                                /* 316 */
    }

    *tex_addr = mem_utilGetMem(GetFileSize(data_label));                /* 320 */
}

static void MenuMapTexLoadReq(void *tex_addr, int data_label)           /* 330 */
{
    FileLoadReqEE(data_label, tex_addr, 2, nullptr, nullptr);           /* 334 */
}

/* Post a load for the snapshot of whatever room the crosshair is over.  The
 * walk stops on the room, not on the map, because room_info_dat[] is keyed by
 * room and the same room only ever appears once. */
static void MenuMapSnapLoadReq(void)                                    /* 342 */
{
    if (snap_data_addr != nullptr) {                                    /* 347 */
        LiberateMenuMapTexMem(&snap_data_addr);                         /* 348 */
    }

    for (int i = 0; room_info_dat[i].map_label != -1; i++) {            /* 355 */
        if (room_info_dat[i].room_label == menu_map_ctrl.hit_room) {    /* 356 */
            menu_map_ctrl.snap_load.tex_label =
                room_info_dat[i].snap_tex_label;                        /* 360 */
            menu_map_ctrl.snap_load.step = MENU_MAP_LOAD_REQ;           /* 362 */

            break;                                                      /* 363 */
        }
    }
}

static int MenuMapBgLoadWait(void)                                      /* 375 */
{
    if (FileLoadIsEnd2(MENU_MAP_PK2 + GetLanguage(), menu_map_bg_addr) != 0) { /* 383 */
        return 1;
    }

    return 0;                                                           /* 388 */
}

/* --------------------------------------------------------------------------
 *  Per-frame
 * ------------------------------------------------------------------------ */

void MenuMap(void)                                                      /* 398 */
{
    int room_label;

    switch (menu_wrk.step) {                                            /* 405 */
    case MENU_MAP_STEP_INIT:
        MenuMapInit();                                                  /* 408 */
        MenuMapDispInit();                                              /* 410 */

        /* An area with no sheet of its own leaves plyr_map at -1, which
         * reads as 255 through the unsigned compare and skips the lot. */
        if ((u_char)menu_map_ctrl.plyr_map < MAP_LABEL_NUM) {           /* 413 */
            MenuCrossFadeInStart(menu_map_ctrl.cross_fade_flg,
                                 map_info_dat[menu_map_disp.disp_map_label].map_tex_id); /* 415 */

            menu_map_ctrl.hit_room = MenuMapRoomHitCheck(MENU_MAP_CENTER_X,
                                                         MENU_MAP_CENTER_Y); /* 419 */

            if (menu_map_ctrl.hit_room != -1) {                         /* 422 */
                MenuMapSnapLoadReq();                                   /* 424 */
            }
        }

        menu_wrk.step = MENU_MAP_STEP_LOAD;                             /* 429 */
        break;                                                          /* 430 */

    case MENU_MAP_STEP_LOAD:
        if (MenuMapBgLoadWait() != 0) {                                 /* 433 */
            menu_wrk.step = MENU_MAP_STEP_MAIN;                         /* 436 */
        }
        break;                                                          /* 439 */

    case MENU_MAP_STEP_MAIN:
        if (menu_map_disp.anim_step == MENU_MAP_ANIM_MAIN) {            /* 441 */
            MenuMapPad();                                               /* 443 */

            if ((u_char)menu_map_ctrl.plyr_map < MAP_LABEL_NUM) {
                MenuCmnCrossFade();                                     /* 445 */
                MenuMapSnapLoadExe();                                   /* 448 */

                room_label = MenuMapRoomHitCheck(MENU_MAP_CENTER_X,
                                                 MENU_MAP_CENTER_Y);    /* 452 */

                if (room_label != menu_map_ctrl.hit_room) {             /* 455 */
                    menu_map_ctrl.hit_room = room_label;

                    if (room_label != -1) {                             /* 460 */
                        if (GetRoomInfo(room_label) == 1) {             /* 462 */
                            MenuMapSnapLoadReq();                       /* 464 */
                        }
                    }
                }
            }
        }
        break;

    case MENU_MAP_STEP_MOVE:
        if (menu_map_disp.anim_step == MENU_MAP_ANIM_END) {             /* 472 */
            MenuMapRelease();                                           /* 473 */

            MenuCrossFadeTexLoadCancel(0);                              /* 476 */
            MenuCrossFadeTexLoadCancel(1);                              /* 477 */
            LiberateAllMenuCrossFadeTexMem();                           /* 479 */

            SetNextMenuStep(MENU_STEP_TOP);                             /* 482 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 486 */
        break;
    }
}

/* The snapshot's own little load machine.  Its request step warns rather
 * than asserting, because a crosshair that has just left every room is a
 * normal thing for one frame. */
static void MenuMapSnapLoadExe(void)                                    /* 496 */
{
    switch (menu_map_ctrl.snap_load.step) {                             /* 499 */
    case MENU_MAP_LOAD_REQ:
        if (menu_map_ctrl.hit_room == -1) {                             /* 502 */
            PRINT_WARNING("Warning! hit_room %d\n",
                          menu_map_ctrl.hit_room);                      /* 504 */
            break;
        }

        GetMenuMapTexMem(&snap_data_addr,
                         menu_map_ctrl.snap_load.tex_label);            /* 506 */
        MenuMapTexLoadReq(snap_data_addr,
                          menu_map_ctrl.snap_load.tex_label);           /* 508 */

        menu_map_ctrl.snap_load.step = MENU_MAP_LOAD_WAIT;              /* 511 */
        break;

    case MENU_MAP_LOAD_WAIT:
        if (FileLoadIsEnd2(menu_map_ctrl.snap_load.tex_label,
                           snap_data_addr) != 0) {                      /* 515 */
            menu_map_ctrl.snap_load.step = MENU_MAP_LOAD_END;           /* 518 */
        }
        break;

    case MENU_MAP_LOAD_END:
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 522 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Input
 * ------------------------------------------------------------------------ */

/* The D-pad scrolls, CROSS zooms, L1/R1 change floor and L2/R2 change area.
 * Each of the four page arms is the same fifteen lines -- test the pad, wait
 * for the cross-fade to have settled, call the change, and if it reported a
 * change start the next dissolve -- and GCC cross-jumped all four tails into
 * one.  The R1 arm carries a six-line gap (629..634) where its three
 * siblings have none; nothing of it survives in the object. */
static void MenuMapPad(void)                                            /* 531 */
{
    int    i;
    float  map_size_w;
    float  map_size_h;
    u_char fade_alpha[2];

    for (i = 0; i < MENU_MAP_TRI_NUM; i++) {                            /* 545 */
        menu_map_disp.tri_pad_flg[i] = 0;                               /* 547 */
    }

    fade_alpha[0] = 0;                                                  /* 550 */
    fade_alpha[1] = 0;

    if ((u_char)menu_map_ctrl.plyr_map < MAP_LABEL_NUM) {               /* 554 */
        GetMenuCrossFadeAlpha(fade_alpha);                              /* 556 */

        if ((pad[0].now & 0x1000) != 0
            || (pad[0].id == 0x79 && pad[0].analog[3] < 0x59)) {        /* 559 */
            menu_map_disp.map_off_y += MENU_MAP_SCROLL_SPEED;           /* 560 */

            if (menu_map_disp.map_off_y > MENU_MAP_SCROLL_MAX_Y) {      /* 562 */
                menu_map_disp.map_off_y = MENU_MAP_SCROLL_MAX_Y;        /* 563 */
            }

            menu_map_disp.tri_pad_flg[MENU_MAP_TRI_UP] = 1;
        }

        if ((pad[0].now & 0x4000) != 0
            || (pad[0].id == 0x79 && pad[0].analog[3] > 0x9d)) {        /* 569 */
            menu_map_disp.map_off_y -= MENU_MAP_SCROLL_SPEED;           /* 570 */

            map_size_h = (float)map_size_dat[menu_map_disp.disp_map_label].h
                         * map_scall_dat[menu_map_disp.disp_map_label].normal; /* 572 */

            if (menu_map_disp.map_off_y
                    < MENU_MAP_SCROLL_MIN_Y - map_size_h) {             /* 574 */
                menu_map_disp.map_off_y = MENU_MAP_SCROLL_MIN_Y - map_size_h; /* 575 */
            }

            menu_map_disp.tri_pad_flg[MENU_MAP_TRI_DOWN] = 1;
        }

        if ((pad[0].now & 0x8000) != 0
            || (pad[0].id == 0x79 && pad[0].analog[2] < 0x59)) {        /* 582 */
            menu_map_disp.map_off_x += MENU_MAP_SCROLL_SPEED;           /* 583 */

            if (menu_map_disp.map_off_x > MENU_MAP_SCROLL_MAX_X) {      /* 585 */
                menu_map_disp.map_off_x = MENU_MAP_SCROLL_MAX_X;        /* 586 */
            }

            menu_map_disp.tri_pad_flg[MENU_MAP_TRI_LEFT] = 1;
        }

        if ((pad[0].now & 0x2000) != 0
            || (pad[0].id == 0x79 && pad[0].analog[2] > 0x9d)) {        /* 593 */
            menu_map_disp.map_off_x -= MENU_MAP_SCROLL_SPEED;           /* 594 */

            map_size_w = (float)map_size_dat[menu_map_disp.disp_map_label].w
                         * map_scall_dat[menu_map_disp.disp_map_label].normal; /* 596 */

            if (menu_map_disp.map_off_x
                    < MENU_MAP_SCROLL_MIN_X - map_size_w) {             /* 598 */
                menu_map_disp.map_off_x = MENU_MAP_SCROLL_MIN_X - map_size_w; /* 599 */
            }

            menu_map_disp.tri_pad_flg[MENU_MAP_TRI_RIGHT] = 1;
        }

        /* Paging is locked out until the snapshot has arrived, so the page
         * cannot be changed while a load is still in flight. */
        if (menu_map_ctrl.snap_load.step == MENU_MAP_LOAD_END) {        /* 605 */
            if (*paddat[0] == 1) {                                      /* 607 */
                menu_map_disp.map_scall_flg ^= 1;                       /* 608 */
            }
            else if ((pad[0].one & 0x0004) != 0) {          /* L1 */    /* 611 */
                if (CheckCrossFadeDisp(menu_map_ctrl.cross_fade_flg) != 0 /* 612 */
                    && fade_alpha[menu_map_ctrl.cross_fade_flg] == 0x80) { /* 613 */
                    if (MenuMapFloorChange(MENU_MAP_CHANGE_PREV) != 0) { /* 614 */
                        MenuCrossFadeOutStart(menu_map_ctrl.cross_fade_flg); /* 615 */
                        menu_map_ctrl.cross_fade_flg ^= 1;              /* 616 */
                        MenuCrossFadeInStart(menu_map_ctrl.cross_fade_flg,
                                             map_info_dat[menu_map_disp.disp_map_label].map_tex_id); /* 617 */

                        MenuMapDispStartPos();                          /* 620 */
                    }
                }
            }
            else if ((pad[0].one & 0x0008) != 0) {         /* R1 */     /* 626 */
                if (CheckCrossFadeDisp(menu_map_ctrl.cross_fade_flg) != 0 /* 627 */
                    && fade_alpha[menu_map_ctrl.cross_fade_flg] == 0x80) { /* 628 */
                    if (MenuMapFloorChange(MENU_MAP_CHANGE_NEXT) != 0) { /* 635 */
                        MenuCrossFadeOutStart(menu_map_ctrl.cross_fade_flg);
                        menu_map_ctrl.cross_fade_flg ^= 1;
                        MenuCrossFadeInStart(menu_map_ctrl.cross_fade_flg,
                                             map_info_dat[menu_map_disp.disp_map_label].map_tex_id);

                        MenuMapDispStartPos();
                    }
                }
            }
            else if ((pad[0].one & 0x0001) != 0) {         /* L2 */     /* 641 */
                if (CheckCrossFadeDisp(menu_map_ctrl.cross_fade_flg) != 0 /* 642 */
                    && fade_alpha[menu_map_ctrl.cross_fade_flg] == 0x80) { /* 643 */
                    if (MenuMapChange(MENU_MAP_CHANGE_PREV) != 0) {     /* 644 */
                        MenuCrossFadeOutStart(menu_map_ctrl.cross_fade_flg); /* 650 */
                        menu_map_ctrl.cross_fade_flg ^= 1;
                        MenuCrossFadeInStart(menu_map_ctrl.cross_fade_flg,
                                             map_info_dat[menu_map_disp.disp_map_label].map_tex_id);

                        MenuMapDispStartPos();
                    }
                }
            }
            else if ((pad[0].one & 0x0002) != 0) {         /* R2 */     /* 656 */
                if (CheckCrossFadeDisp(menu_map_ctrl.cross_fade_flg) != 0 /* 657 */
                    && fade_alpha[menu_map_ctrl.cross_fade_flg] == 0x80) { /* 658 */
                    if (MenuMapChange(MENU_MAP_CHANGE_NEXT) != 0) {     /* 659 */
                        MenuCrossFadeOutStart(menu_map_ctrl.cross_fade_flg); /* 665 */
                        menu_map_ctrl.cross_fade_flg ^= 1;
                        MenuCrossFadeInStart(menu_map_ctrl.cross_fade_flg,
                                             map_info_dat[menu_map_disp.disp_map_label].map_tex_id);

                        MenuMapDispStartPos();
                    }
                }
            }
        }
    }

    if (*paddat[1] == 1) {                                              /* 674 */
        SystemBankPlay(1, 1, 0, 0, nullptr, 0x3200, 0x1000);            /* 675 */

        MenuMapOutReq();                                                /* 678 */
    }
}

/* Leaving the page.  map_view_flg is up when GID_STORY_MAP put the map on
 * screen directly rather than the menu hub, and in that case the whole menu
 * has to close rather than fall back to the hub. */
static void MenuMapOutReq(void)                                         /* 688 */
{
    menu_wrk.step = MENU_MAP_STEP_MOVE;                                 /* 691 */

    menu_map_disp.anim_step = MENU_MAP_ANIM_OUT;                        /* 692 */
    menu_map_disp.anim_timer = 0;

    if (map_view_flg == 1) {                                            /* 696 */
        MenuOutReq();                                                   /* 697 */
    }
}

/* --------------------------------------------------------------------------
 *  Paging
 * ------------------------------------------------------------------------ */

/* Step to the next or previous AREA, keeping the floor where possible.  The
 * outer loop walks up to all eight areas so a run of unvisited ones is
 * skipped in one press; the inner one tries the current floor first and then
 * the two others, which is what lets a two-storey house be entered from a
 * one-storey neighbour without losing the page. */
static int MenuMapChange(int flg)                                       /* 710 */
{
    int    i;
    int    j;
    int    area;
    int    floor;
    int    disp_map_label;
    int    res;
    u_char find_flg;

    res = 0;                                                            /* 719 */
    find_flg = 0;                                                       /* 721 */
    disp_map_label = 0;                                                 /* 722 */

    for (area = 0; area < MAP_AREA_NUM; area++) {                       /* 725 */
        for (floor = 0; floor < MAP_FLOOR_NUM; floor++) {               /* 726 */
            if (map_label_tbl[area][floor] == menu_map_disp.disp_map_label) { /* 727 */
                disp_map_label = map_label_tbl[area][floor];            /* 729 */
                find_flg = 1;

                break;                                                  /* 730 */
            }
        }

        if (find_flg == 1) {                                            /* 734 */
            break;
        }
    }                                                                   /* 737 */

    if (find_flg == 0) {
        PRINT_ASSERT("Error!! MenuMapChange");                          /* 741 */
    }

    switch (flg) {                                                      /* 745 */
    case MENU_MAP_CHANGE_NEXT:
        for (i = 0; i < MAP_AREA_NUM; i++) {                            /* 747 */
            area = (area + 1) % MAP_AREA_NUM;                           /* 748 */

            for (j = 0; j < MAP_FLOOR_NUM; j++) {                       /* 750 */
                if (map_label_tbl[area][(floor + j) % MAP_FLOOR_NUM] != -1 /* 751 */
                    && MenuMapInCheck(map_label_tbl[area][(floor + j) % MAP_FLOOR_NUM]) == 1) { /* 753,755 */
                    disp_map_label =
                        map_label_tbl[area][(floor + j) % MAP_FLOOR_NUM]; /* 756 */
                    find_flg = 1;                                       /* 757 */

                    break;
                }
            }                                                           /* 760 */

            if (find_flg == 1) {                                        /* 763 */
                break;
            }
        }                                                               /* 766 */
        break;

    case MENU_MAP_CHANGE_PREV:
        for (i = 0; i < MAP_AREA_NUM; i++) {                            /* 769 */
            area = (area + (MAP_AREA_NUM - 1)) % MAP_AREA_NUM;          /* 770 */

            for (j = 0; j < MAP_FLOOR_NUM; j++) {                       /* 772 */
                if (map_label_tbl[area][(floor + j) % MAP_FLOOR_NUM] != -1 /* 773 */
                    && MenuMapInCheck(map_label_tbl[area][(floor + j) % MAP_FLOOR_NUM]) == 1) { /* 775,777 */
                    disp_map_label =
                        map_label_tbl[area][(floor + j) % MAP_FLOOR_NUM]; /* 778 */
                    find_flg = 1;                                       /* 779 */

                    break;
                }
            }                                                           /* 782 */

            if (find_flg == 1) {                                        /* 785 */
                break;
            }
        }                                                               /* 788 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 791 */
        break;
    }

    if (disp_map_label != menu_map_disp.disp_map_label) {               /* 795 */
        menu_map_disp.before_disp_map = menu_map_disp.disp_map_label;   /* 796 */
        menu_map_disp.disp_map_label = (char)disp_map_label;            /* 797 */

        res = 1;                                                        /* 798 */
    }

    return res;                                                         /* 802 */
}

/* Step to the next or previous FLOOR of the area already on screen.  Three
 * tries, one per column, and the wrap is a real signed modulo -- the area
 * never changes here. */
static int MenuMapFloorChange(int flg)                                  /* 812 */
{
    int    i;
    int    area;
    int    floor;
    int    disp_map_label;
    int    res;
    u_char find_flg;

    res = 0;                                                            /* 820 */
    find_flg = 0;                                                       /* 821 */
    disp_map_label = 0;                                                 /* 822 */

    for (area = 0; area < MAP_AREA_NUM; area++) {                       /* 825 */
        for (floor = 0; floor < MAP_FLOOR_NUM; floor++) {               /* 826 */
            if (map_label_tbl[area][floor] == menu_map_disp.disp_map_label) { /* 827 */
                disp_map_label = map_label_tbl[area][floor];            /* 829 */
                find_flg = 1;

                break;                                                  /* 830 */
            }
        }

        if (find_flg == 1) {                                            /* 834 */
            break;
        }
    }                                                                   /* 837 */

    if (find_flg == 0) {
        PRINT_ASSERT("Error!! MenuMapFloorChange");                     /* 841 */
    }

    switch (flg) {                                                      /* 845 */
    case MENU_MAP_CHANGE_NEXT:
        for (i = 0; i < MAP_FLOOR_NUM; i++) {                           /* 847 */
            floor = (floor + 1) % MAP_FLOOR_NUM;                        /* 848 */

            if (map_label_tbl[area][floor] != -1                        /* 850 */
                && MenuMapInCheck(map_label_tbl[area][floor]) == 1) {   /* 852 */
                disp_map_label = map_label_tbl[area][floor];            /* 853 */

                break;
            }
        }                                                               /* 857 */
        break;

    case MENU_MAP_CHANGE_PREV:
        for (i = 0; i < MAP_FLOOR_NUM; i++) {                           /* 860 */
            floor = (floor + (MAP_FLOOR_NUM - 1)) % MAP_FLOOR_NUM;      /* 861 */

            if (map_label_tbl[area][floor] != -1                        /* 863 */
                && MenuMapInCheck(map_label_tbl[area][floor]) == 1) {   /* 865 */
                disp_map_label = map_label_tbl[area][floor];            /* 867 */

                break;
            }
        }                                                               /* 870 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 873 */
        break;
    }

    if (disp_map_label != menu_map_disp.disp_map_label) {               /* 877 */
        menu_map_disp.before_disp_map = menu_map_disp.disp_map_label;   /* 878 */
        menu_map_disp.disp_map_label = (char)disp_map_label;            /* 879 */

        res = 1;                                                        /* 880 */
    }

    return res;                                                         /* 884 */
}

/* Has the player been in ANY room on this sheet?  A sheet nobody has set
 * foot on is not worth paging to, which is what keeps the unexplored half of
 * the estate off the L2/R2 rotation. */
static int MenuMapInCheck(int map_label)                                /* 893 */
{
    int res;

    res = 0;

    for (int i = 0; room_info_dat[i].map_label != -1; i++) {            /* 903 */
        if (room_info_dat[i].map_label == map_label) {                  /* 904 */
            if (GetRoomInfo(room_info_dat[i].room_label) == 1) {        /* 908 */
                res = 1;                                                /* 910 */

                break;                                                  /* 913 */
            }
        }
    }

    return res;                                                         /* 919 */
}

/* --------------------------------------------------------------------------
 *  Geometry
 * ------------------------------------------------------------------------ */

/* Which room is under (x, y) on the sheet currently displayed.  The quads in
 * map_area_dat[] are in unscaled map units, so each corner has to go through
 * MenuMapSetPosition() -- scale plus scroll -- before the test, and the whole
 * thing is redone every frame because the sheet is what moves. */
static int MenuMapRoomHitCheck(float x, float y)                        /* 927 */
{
    int   res;
    float target[4];
    float tri0[4];
    float tri1[4];
    float tri2[4];
    float tri3[4];

    res = -1;                                                           /* 937 */

    target[0] = x;                                                      /* 939 */
    target[1] = 0.0f;
    target[2] = y;                                                      /* 941 */
    target[3] = 1.0f;                                                   /* 942 */

    for (int i = 0; map_area_dat[i].map_label != -1; i++) {             /* 947 */
        if (map_area_dat[i].map_label == menu_map_disp.disp_map_label) { /* 948 */
            MenuMapSetPosition(tri0, map_area_dat[i].pos[0][0],
                               map_area_dat[i].pos[0][1]);              /* 951 */
            MenuMapSetPosition(tri1, map_area_dat[i].pos[1][0],
                               map_area_dat[i].pos[1][1]);              /* 953 */
            MenuMapSetPosition(tri2, map_area_dat[i].pos[2][0],
                               map_area_dat[i].pos[2][1]);              /* 954 */
            MenuMapSetPosition(tri3, map_area_dat[i].pos[3][0],
                               map_area_dat[i].pos[3][1]);              /* 955 */

            if (MenuMapHitCheck(target, tri0, tri1, tri2, tri3) != 0) { /* 956 */
                res = map_area_dat[i].room_label;                       /* 960 */

                break;                                                  /* 962 */
            }
        }
    }

    return res;                                                         /* 968 */
}

/* Map units to screen, into the y == 0 plane HcBaseIsInTriXZ() wants.  Both
 * zoom arms write the same y expression and GCC cross-jumped them, keeping
 * the chosen MAP_SCALL_DAT member's address rather than its value -- which is
 * why the decompiler shows `.big` being read through `->normal`. */
static void MenuMapSetPosition(float *pos, float x, float y)            /* 977 */
{
    float map_x;
    float map_y;

    map_x = 0.0f;                                                       /* 982 */
    map_y = 0.0f;

    CalMapDispStartPos(&map_x, &map_y, menu_map_disp.map_scall_flg);    /* 986 */

    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 990 */
        pos[0] = x * map_scall_dat[menu_map_disp.disp_map_label].normal
                 + map_x;                                               /* 991 */
        pos[1] = 0.0f;                                                  /* 992 */
        pos[2] = y * map_scall_dat[menu_map_disp.disp_map_label].normal
                 + map_y;                                               /* 993 */
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 996 */
        pos[0] = x * map_scall_dat[menu_map_disp.disp_map_label].big
                 + map_x;                                               /* 997 */
        pos[1] = 0.0f;                                                  /* 998 */
        pos[2] = y * map_scall_dat[menu_map_disp.disp_map_label].big
                 + map_y;                                               /* 999 */
    }

    pos[3] = 1.0f;                                                      /* 1002 */
}

/* The map quads arrive as four corners in winding order, so the diagonal that
 * splits them is 2-0.  Both halves are tested in the floor plane: the caller
 * builds its points with y == 0 and the height component is meaningless here
 * anyway, since these are map-sheet coordinates, not world ones. */
int MenuMapHitCheck(float *target, float *tri0, float *tri1,            /* 1016 */
                    float *tri2, float *tri3)
{
    int res;

    res = HcBaseIsInTriXZ(target, tri0, tri1, tri2);                    /* 1023 */

    if (res == 0) {                                                     /* 1024 */
        res = HcBaseIsInTriXZ(target, tri2, tri3, tri0);                /* 1025 */
    }

    return res;                                                         /* 1029 */
}

/* Is the house this room belongs to worth showing at all?  A house is a
 * contiguous run of room labels in house_info_dat[]; if the player has been
 * in any of them the whole house is drawn, and a room that belongs to no
 * house listed there is always drawn.  That is what stops half a building
 * appearing before the player has found the front door. */
static int MenuMapCheckHouseCondition(int room_label)                   /* 1039 */
{
    int  j;
    int  res;
    char check_flg;
    char end_flg;

    res = 0;                                                            /* 1046 */
    check_flg = 0;                                                      /* 1047 */
    end_flg = 0;                                                        /* 1048 */

    for (int i = 0; house_info_dat[i].room_label != -1; i++) {          /* 1054 */
        if (house_info_dat[i].room_label == room_label) {               /* 1056 */
            check_flg = 1;                                              /* 1059 */

            for (j = house_info_dat[i].start_room_label;                /* 1063 */
                 j <= house_info_dat[i].end_room_label; j++) {          /* 1064 */
                if (GetRoomInfo(j) == 1) {                              /* 1067 */
                    res = 1;                                            /* 1068 */
                    end_flg = 1;                                        /* 1070 */

                    break;                                              /* 1071 */
                }
            }

            if (end_flg == 1) {                                         /* 1074 */
                break;
            }
        }
    }

    if (check_flg == 0) {                                               /* 1082 */
        res = 1;
    }

    return res;                                                         /* 1085 */
}

int GetPlyrMapLabel(void)                                               /* 1096 */
{
    return GetMapLabelFromAreaLabel(GetPlyrAreaNo(), GetPlyrFloor());   /* 1101 */
}

/* area_map_tbl[] is walked to its -1 terminator rather than indexed, because
 * the areas are not dense: one area can appear several times, once per floor.
 * Below area 13 the mapping is one-to-one and the floor is ignored; from 13
 * on (the Kurosawa house and the villages beyond it) the same area spans
 * several sheets and the floor picks between them. */
int GetMapLabelFromAreaLabel(int area_label, int floor_label)           /* 1113 */
{
    int map_label;

    for (int i = 0; ; i++) {                                            /* 1121 */
        if (area_map_tbl[i].area_label == -1) {                         /* 1123 */
            /* Bare printf, not PRINT_ERROR -- the ROM emits no banner here. */
            printf("Error! GetMapLabelFromAreaLabel:area[%d] floor[%d]\n", /* 1128 */
                   area_label, floor_label);
            map_label = -1;                                             /* 1130 */
            break;
        }

        if (area_map_tbl[i].area_label == area_label) {                 /* 1135 */
            if (((u_int)area_label < 13) ||                             /* 1138 */
                (area_map_tbl[i].floor_label == floor_label)) {         /* 1144 */
                map_label = area_map_tbl[i].map_label;                  /* 1146 */
                break;
            }
        }
    }

    return map_label;                                                   /* 1152 */
}

void MenuMapRelease(void)                                               /* 1162 */
{
    LiberateMenuMapTexMem(&menu_map_bg_addr);                           /* 1166 */
    LiberateMenuMapTexMem(&map_data_addr);                              /* 1168 */
    LiberateMenuMapTexMem(&snap_data_addr);                             /* 1170 */
}

static void LiberateMenuMapTexMem(void **tex_addr)                      /* 1179 */
{
    if (*tex_addr != nullptr) {                                         /* 1182 */
        mem_utilFreeMem(*tex_addr);                                     /* 1183 */
        *tex_addr = nullptr;                                            /* 1184 */
    }
}

/* --------------------------------------------------------------------------
 *  Display
 * ------------------------------------------------------------------------ */

static void MenuMapDispInit(void)                                       /* 1197 */
{
    int i;

    menu_map_disp.anim_step = 0;                                        /* 1202 */
    menu_map_disp.anim_timer = 0;                                       /* 1203 */

    menu_map_disp.tri_anim_step = MENU_MAP_TRI_ANIM_OFF;                /* 1205 */
    menu_map_disp.tri_timer = 0;                                        /* 1206 */
    menu_map_disp.tri_alpha = 0;                                        /* 1207 */

    for (i = 0; i < MENU_MAP_TRI_NUM; i++) {                            /* 1209 */
        menu_map_disp.tri_pad_flg[i] = 0;
    }

    menu_map_disp.map_scall_flg = MENU_MAP_SCALL_NORMAL;                /* 1211 */

    menu_map_disp.disp_map_label = (char)GetPlyrMapLabel();             /* 1213 */
    menu_map_disp.before_disp_map = menu_map_disp.disp_map_label;       /* 1216 */

    if (menu_map_disp.disp_map_label == -1) {                           /* 1219 */
        menu_map_disp.map_off_x = 0.0f;                                 /* 1221 */
        menu_map_disp.map_off_y = 0.0f;
    }
    else {
        MenuMapDispStartPos();                                          /* 1224 */
    }
}

/* Where the sheet starts.  On the player's own sheet the scroll is set so the
 * player sits under the crosshair; on any other one the sheet is simply
 * centred, which is why the else arm never reads map_plyr_x / map_plyr_y at
 * all.  Both arms write the same two subtractions and GCC cross-jumped them,
 * so each arm's own 259.0f literal has its own .lit4 slot (3ee570 and
 * 3ee574) -- two expansions of one constant, not two different values. */
static void MenuMapDispStartPos(void)                                   /* 1234 */
{
    float map_plyr_x;
    float map_plyr_y;

    ChangeWorldPosToWinPos(&map_plyr_x, &map_plyr_y,
                           menu_map_disp.disp_map_label,
                           plyr_wrk.cmn_wrk.mbox.pos, 0);               /* 1240 */

    if (menu_map_disp.disp_map_label == menu_map_ctrl.plyr_map) {       /* 1243 */
        menu_map_disp.map_off_x = MENU_MAP_CENTER_X - map_plyr_x;       /* 1245 */
        menu_map_disp.map_off_y = MENU_MAP_CENTER_Y - map_plyr_y;       /* 1246 */
    }
    else {
        menu_map_disp.map_off_x = MENU_MAP_CENTER_X
            - (float)map_size_dat[menu_map_disp.disp_map_label].w
              * map_scall_dat[menu_map_disp.disp_map_label].normal
              * 0.5f;                                                   /* 1250 */
        menu_map_disp.map_off_y = MENU_MAP_CENTER_Y
            - (float)map_size_dat[menu_map_disp.disp_map_label].h
              * map_scall_dat[menu_map_disp.disp_map_label].normal
              * 0.5f;                                                   /* 1252 */
    }
}

void MenuMapDisp(void)                                                  /* 1262 */
{
    u_char alpha;
    u_long scissor_backup;
    u_char fade_alpha[2];

    alpha = 0x80;                                                       /* 1269 */

    fade_alpha[0] = 0;                                                  /* 1272 */
    fade_alpha[1] = 0;                                                  /* 1273 */

    if ((u_char)(menu_wrk.step - MENU_MAP_STEP_MAIN) < 2
        && menu_map_disp.anim_step != MENU_MAP_ANIM_END) {              /* 1278 */
        MenuInOutAnimCtrl(&menu_map_disp.anim_step,
                          &menu_map_disp.anim_timer, &alpha);           /* 1280 */

        MenuMapTitleDisp(0, 0, alpha);                                  /* 1282 */
        MenuMapBaseDisp(0, 0, alpha);                                   /* 1285 */

        /* The sheet is the only thing clipped to the viewport; the frame,
         * the snapshot and the readouts are all drawn outside it. */
        scissor_backup = GET_SCISSOR_REGISTER(0);                       /* 1288 */
        SetScissorRegister(0, MENU_MAP_SCISSOR);                        /* 1291 */

        GetMenuCrossFadeAlpha(fade_alpha);                              /* 1293 */

        if (menu_map_disp.disp_map_label != -1) {                       /* 1296 */
            /* Outside the main step the page is itself fading, so the
             * cross-fade's own alpha is replaced by the page's. */
            if (menu_wrk.step != MENU_MAP_STEP_MAIN) {                  /* 1299 */
                fade_alpha[menu_map_ctrl.cross_fade_flg] = alpha;       /* 1300 */
            }

            if (CheckCrossFadeDisp(menu_map_ctrl.cross_fade_flg) != 0) { /* 1303 */
                PK2SendVram((uintptr_t)(uintptr_t)GetCrossFadeDataAddr(
                                menu_map_ctrl.cross_fade_flg),
                            -1, -1, 0);                                 /* 1305 */

                MenuMapMapDisp(menu_map_disp.disp_map_label, 0, 0,
                               fade_alpha[menu_map_ctrl.cross_fade_flg]); /* 1307 */
            }
        }

        SetScissorRegister(0, scissor_backup);                          /* 1311 */

        MenuMapCenterDisp(0, 0, alpha);                                 /* 1314 */

        if (menu_map_disp.disp_map_label != -1) {                       /* 1316 */
            MenuMapSnapShotDisp(0, 0, alpha);                           /* 1318 */
        }

        MenuMapWindowDisp(0, 0, alpha);                                 /* 1322 */

        if (menu_map_disp.disp_map_label != -1) {                       /* 1324 */
            MenuMapInfoDisp(0, 0, alpha);                               /* 1326 */
        }

        MenuMapCaptionDisp(0, 0, alpha);                                /* 1330 */
    }
}

/* The title is drawn twice, once out of each pak: 2/3 come from the shared
 * menu background and 0/1 from the map page's own language pak, and the
 * second pair lands on top. */
static void MenuMapTitleDisp(int off_x, int off_y, u_char alpha)        /* 1350 */
{
    DISP_SPRT title_ds;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1354 */

    CopySprDToSpr(&title_ds, &menu_map_tex[2]);                         /* 1358 */
    title_ds.x = title_ds.x + (float)off_x;                             /* 1359 */
    title_ds.y = title_ds.y + (float)off_y;                             /* 1360 */
    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 1361 */

    DispSprD(&title_ds);                                                /* 1362 */

    CopySprDToSpr(&title_ds, &menu_map_tex[3]);                         /* 1363 */
    title_ds.x = title_ds.x + (float)off_x;                             /* 1364 */
    title_ds.y = title_ds.y + (float)off_y;
    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 1365 */

    DispSprD(&title_ds);

    PK2SendVram((uintptr_t)(uintptr_t)menu_map_bg_addr, -1, -1, 0);         /* 1368 */

    CopySprDToSpr(&title_ds, &menu_map_tex[0]);                         /* 1371 */
    title_ds.x = title_ds.x + (float)off_x;                             /* 1372 */
    title_ds.y = title_ds.y + (float)off_y;                             /* 1373 */
    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 1374 */

    DispSprD(&title_ds);                                                /* 1375 */

    CopySprDToSpr(&title_ds, &menu_map_tex[1]);                         /* 1376 */
    title_ds.x = title_ds.x + (float)off_x;                             /* 1377 */
    title_ds.y = title_ds.y + (float)off_y;
    title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 1378 */

    DispSprD(&title_ds);
}

/* The translucent plates behind the sheet.  Three of them repeat sideways by
 * their own width, then a single, then four, then a single again -- so the
 * table only carries one copy of each strip.  The last two are the additive
 * glows along the top and bottom edges. */
static void MenuMapBaseDisp(int off_x, int off_y, u_char alpha)         /* 1389 */
{
    DISP_SPRT base_ds;
    int       i;

    PK2SendVram((uintptr_t)(uintptr_t)menu_map_bg_addr, -1, -1, 0);         /* 1394 */

    for (i = 0; i < 3; i++) {                                           /* 1398 */
        CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_BASE]);      /* 1399 */
        base_ds.x = base_ds.x + (float)(i * base_ds.w) + (float)off_x;  /* 1400 */
        base_ds.y = base_ds.y + (float)off_y;
        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 1401 */

        DispSprD(&base_ds);                                             /* 1402 */
    }                                                                   /* 1403 */

    /* GCC reversed this one -- `i` is only the counter, so the object counts
     * down from 1 while the table pointer walks forward. */
    for (i = 0; i < 2; i++) {                                           /* 1406 */
        CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_BASE + 1 + i]); /* 1405 */
        base_ds.x = base_ds.x + (float)off_x;                           /* 1407 */
        base_ds.y = base_ds.y + (float)off_y;
        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 1408 */

        DispSprD(&base_ds);                                             /* 1409 */
    }                                                                   /* 1410 */

    for (i = 0; i < 4; i++) {                                           /* 1412 */
        CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_BASE + 3]);  /* 1413 */
        base_ds.x = base_ds.x + (float)(i * base_ds.w) + (float)off_x;  /* 1414 */
        base_ds.y = base_ds.y + (float)off_y;
        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 1415 */

        DispSprD(&base_ds);                                             /* 1416 */
    }                                                                   /* 1417 */

    CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_BASE + 4]);      /* 1418 */
    base_ds.x = base_ds.x + (float)off_x;                               /* 1419 */
    base_ds.y = base_ds.y + (float)off_y;                               /* 1420 */
    base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7);   /* 1421 */

    DispSprD(&base_ds);                                                 /* 1423 */

    for (i = 0; i < 4; i++) {                                           /* 1424 */
        CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_BASE + 5]);  /* 1425 */
        base_ds.x = base_ds.x + (float)(i * base_ds.w) + (float)off_x;  /* 1426 */
        base_ds.y = base_ds.y + (float)off_y;
        base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7); /* 1427 */

        DispSprD(&base_ds);                                             /* 1428 */
    }                                                                   /* 1429 */

    CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_BASE + 6]);      /* 1430 */
    base_ds.x = base_ds.x + (float)off_x;                               /* 1431 */
    base_ds.y = base_ds.y + (float)off_y;
    base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7);   /* 1432 */

    DispSprD(&base_ds);

    CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_GLOW]);          /* 1435 */
    base_ds.x = base_ds.x + (float)off_x;                               /* 1436 */
    base_ds.y = base_ds.y + (float)off_y;
    base_ds.alphar = 0x48;                                              /* 1437 */
    base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7);   /* 1438 */

    DispSprD(&base_ds);                                                 /* 1439 */

    CopySprDToSpr(&base_ds, &menu_map_tex[MENU_MAP_TEX_GLOW + 1]);      /* 1442 */
    base_ds.x = base_ds.x + (float)off_x;                               /* 1443 */
    base_ds.y = base_ds.y + (float)off_y;
    base_ds.alphar = 0x48;                                              /* 1444 */
    base_ds.alpha = (u_char)(((int)base_ds.alpha * (int)alpha) >> 7);   /* 1445 */

    DispSprD(&base_ds);                                                 /* 1446 */
}

/* One sheet.  Two passes: everything seen and not highlighted goes down as a
 * flat black silhouette, and the highlighted room group is collected and then
 * drawn tinted plus additively glowed.  MenuMapDispAreaCheck() culls each
 * sprite against the screen before it is submitted, because a sheet can be
 * several times the viewport. */
static void MenuMapMapDisp(int map_label, int off_x, int off_y, u_char alpha) /* 1458 */
{
    DISP_SPRT map_ds;
    int       i;
    float     map_x;
    float     map_y;
    int       sel_room_group[SEL_ROOM_GROUP_NUM];
    int       buff_cnt;

    buff_cnt = 0;                                                       /* 1468 */

    memset(sel_room_group, -1, sizeof(sel_room_group));                 /* 1471 */

    map_x = 0.0f;                                                       /* 1475 */
    map_y = 0.0f;

    CalMapDispStartPos(&map_x, &map_y, menu_map_disp.map_scall_flg);    /* 1481 */

    for (i = 0; room_info_dat[i].map_label != -1; i++) {                /* 1482 */
        /* The house test applies ONLY on sheet 0, the grounds: that sheet
         * carries every building's footprint, and a footprint must not appear
         * before the player has been inside.  On a building's own sheet the
         * player is already there, so the test is skipped. */
        if (room_info_dat[i].map_label != map_label                     /* 1486 */
            || (map_label == 0
                && MenuMapCheckHouseCondition(room_info_dat[i].room_label) == 0) /* 1488,1490 */
            || GetRoomInfo(room_info_dat[i].room_label) != 1) {         /* 1492 */
            continue;
        }

        CopySprDToSpr(&map_ds,
                      &menu_map_mapdata_tex[room_info_dat[i].room_tex_label]); /* 1492 */
        map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7); /* 1497 */

        if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {     /* 1498 */
            map_ds.scw = map_scall_dat[map_label].normal;               /* 1499 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = map_ds.x * map_ds.scw + map_x;                 /* 1502 */
            map_ds.csy = map_ds.y * map_ds.scw + map_y;                 /* 1503 */
            map_ds.x = map_ds.csx;                                      /* 1505 */
            map_ds.y = map_ds.csy;
        }
        else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {   /* 1508 */
            map_ds.scw = map_scall_dat[map_label].big;                  /* 1509 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = map_ds.x * map_ds.scw + map_x;                 /* 1511 */
            map_ds.csy = map_ds.y * map_ds.scw + map_y;
            map_ds.x = map_ds.csx;                                      /* 1515 */
            map_ds.y = map_ds.csy;
        }

        if (room_info_dat[i].room_label == menu_map_ctrl.hit_room       /* 1520 */
            || MenuMapRoomGroupCheck(room_info_dat[i].room_group_label) != 0) { /* 1521 */
            /* Deferred: the highlight has to be drawn over every
             * silhouette, and the walk has not finished laying them yet. */
            sel_room_group[buff_cnt] = room_info_dat[i].room_tex_label; /* 1522 */
            buff_cnt++;                                                 /* 1526 */
        }
        else {
            map_ds.alphar = 0x46;                                       /* 1527 */
            map_ds.r = 0;                                               /* 1529 */
            map_ds.g = 0;
            map_ds.b = 0;

            if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                                     (float)map_ds.w, (float)map_ds.h) != 0) { /* 1530 */
                DispSprD(&map_ds);                                      /* 1535 */
            }
        }
    }

    if (buff_cnt > SEL_ROOM_GROUP_NUM - 1) {                            /* 1539 */
        PRINT_ASSERT("Error! %s Buffer Size Over!!", __FUNCTION__);     /* 1540 */
    }

    for (i = 0; i < SEL_ROOM_GROUP_NUM; i++) {                          /* 1545 */
        if (sel_room_group[i] == -1) {                                  /* 1546 */
            continue;
        }

        CopySprDToSpr(&map_ds, &menu_map_mapdata_tex[sel_room_group[i]]); /* 1547 */

        if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {     /* 1549 */
            map_ds.scw = map_scall_dat[map_label].normal;               /* 1552 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = map_ds.x * map_ds.scw + map_x;                 /* 1555 */
            map_ds.csy = map_ds.y * map_ds.scw + map_y;                 /* 1556 */
            map_ds.x = map_ds.csx;                                      /* 1558 */
            map_ds.y = map_ds.csy;
        }
        else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {   /* 1560 */
            map_ds.scw = map_scall_dat[map_label].big;                  /* 1561 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = map_ds.x * map_ds.scw + map_x;                 /* 1563 */
            map_ds.csy = map_ds.y * map_ds.scw + map_y;
            map_ds.x = map_ds.csx;                                      /* 1564 */
            map_ds.y = map_ds.csy;
        }

        map_ds.r = 0xf2;                                                /* 1567 */
        map_ds.g = 0x96;
        map_ds.b = 0x58;
        map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7); /* 1568 */

        if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                                 (float)map_ds.w, (float)map_ds.h) != 0) { /* 1570 */
            DispSprD(&map_ds);                                          /* 1571 */
        }

        /* The same sprite again, additively, at a fifth of the alpha -- the
         * glow that separates the highlighted room from its neighbours. */
        map_ds.alphar = 0x48;                                           /* 1574 */
        map_ds.alpha = (u_char)(((int)alpha * 0x26) >> 7);              /* 1576 */

        if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                                 (float)map_ds.w, (float)map_ds.h) != 0) { /* 1579 */
            DispSprD(&map_ds);                                          /* 1582 */
        }
    }

    PK2SendVram((uintptr_t)(uintptr_t)menu_map_bg_addr, -1, -1, 0);         /* 1585 */

    MenuMapSavePointDisp(map_label, map_x, map_y, alpha);
    MenuMapDoorDisp(map_label, map_x, map_y, alpha);

    if (menu_map_ctrl.plyr_map == map_label) {                          /* 1587 */
        MenuMapPlyrPosDisp(map_label, map_x, map_y, alpha);
    }
}

/* Cheap screen cull.  Note it takes the sprite's UNSCALED w/h and applies the
 * zoom itself, because every caller passes map_ds.w / map_ds.h straight out
 * of the table while map_ds.x / map_ds.y have already been scaled. */
static int MenuMapDispAreaCheck(int map_label, float x, float y,        /* 1603 */
                                float w, float h)
{
    int   res;
    float tmp_w;
    float tmp_h;

    res = 0;                                                            /* 1610 */

    tmp_w = 0.0f;                                                       /* 1611 */
    tmp_h = 0.0f;

    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 1614 */
        tmp_w = w * map_scall_dat[map_label].normal;                    /* 1615 */
        tmp_h = h * map_scall_dat[map_label].normal;                    /* 1616 */
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 1619 */
        tmp_w = w * map_scall_dat[map_label].big;                       /* 1620 */
        tmp_h = h * map_scall_dat[map_label].big;                       /* 1621 */
    }

    if (x + tmp_w > 0.0f && x < 640.0f
        && y + tmp_h > 0.0f && y < 448.0f) {                            /* 1625 */
        res = 1;
    }

    return res;                                                         /* 1630 */
}

/* Is this room part of the group the crosshair is currently inside?  Group 0
 * is "no group", so a room with no group is never highlighted by proximity --
 * only by being the hit room itself. */
static int MenuMapRoomGroupCheck(int room_group_label)                  /* 1639 */
{
    int res;
    int hit_room_group;

    res = 0;                                                            /* 1645 */
    hit_room_group = -1;                                                /* 1646 */

    if (menu_map_ctrl.hit_room != -1                                    /* 1651 */
        && GetRoomInfo(menu_map_ctrl.hit_room) != 0                     /* 1655 */
        && room_group_label != 0) {                                     /* 1659 */
        for (int i = 0; room_info_dat[i].map_label != -1; i++) {        /* 1666 */
            if (room_info_dat[i].room_label == menu_map_ctrl.hit_room) { /* 1667 */
                hit_room_group = room_info_dat[i].room_group_label;     /* 1670 */

                break;                                                  /* 1672 */
            }
        }

        if (room_group_label == hit_room_group) {                       /* 1677 */
            res = 1;
        }
    }

    return res;                                                         /* 1683 */
}

/* Turn the scroll offset into the sheet's screen origin.  At 2x the offset is
 * reflected about the viewport centre and doubled, so the point under the
 * crosshair stays under the crosshair when the zoom is toggled -- the two
 * additions are GCC's strength reduction of a multiply by two. */
static void CalMapDispStartPos(float *map_x, float *map_y, char scall_flg) /* 1692 */
{
    if (scall_flg == MENU_MAP_SCALL_NORMAL) {                           /* 1697 */
        *map_x = menu_map_disp.map_off_x;                               /* 1698 */
        *map_y = menu_map_disp.map_off_y;                               /* 1699 */
    }
    else if (scall_flg == MENU_MAP_SCALL_BIG) {                         /* 1702 */
        *map_x = MENU_MAP_CENTER_X
                 - (MENU_MAP_CENTER_X - menu_map_disp.map_off_x) * 2.0f; /* 1703 */
        *map_y = MENU_MAP_CENTER_Y
                 - (MENU_MAP_CENTER_Y - menu_map_disp.map_off_y) * 2.0f; /* 1704 */
    }
}

/* The save-point lanterns.  Unlike the sheet these take a literal 2.0 scale
 * at the big zoom rather than the sheet's own factor, so the mark stays the
 * same relative size instead of growing with the art. */
static void MenuMapSavePointDisp(int map_label, float map_x, float map_y, /* 1718 */
                                 u_char alpha)
{
    DISP_SPRT map_ds;

    for (int i = 0; map_save_point[i].map_label != -1; i++) {           /* 1724 */
        if (map_save_point[i].map_label != map_label) {                 /* 1726 */
            continue;
        }

        if (GetRoomInfo(map_save_point[i].room_label) != 1) {           /* 1728 */
            continue;
        }

        CopySprDToSpr(&map_ds, &menu_map_tex[MENU_MAP_TEX_SAVE_POINT]); /* 1730 */
        map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7); /* 1732 */

        if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {     /* 1734 */
            map_ds.x = map_save_point[i].x * map_scall_dat[map_label].normal
                       + map_x;                                         /* 1736 */
            map_ds.y = map_save_point[i].y * map_scall_dat[map_label].normal
                       + map_y;                                         /* 1738 */
        }
        else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {   /* 1740 */
            map_ds.scw = 2.0f;                                          /* 1742 */
            map_ds.sch = 2.0f;
            map_ds.csx = map_save_point[i].x * map_scall_dat[map_label].big
                         + map_x;                                       /* 1744 */
            map_ds.csy = map_save_point[i].y * map_scall_dat[map_label].big
                         + map_y;                                       /* 1746 */
            map_ds.x = map_ds.csx;                                      /* 1748 */
            map_ds.y = map_ds.csy;
        }

        if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                                 (float)map_ds.w, (float)map_ds.h) != 0) { /* 1750 */
            DispSprD(&map_ds);                                          /* 1752 */
        }
    }
}

/* Every door on the sheet.  A door is shown as soon as EITHER of its two
 * rooms has been seen, which is what stops the map having holes along the
 * edge of explored ground; the type then picks single or double leaf, and the
 * seal label picks the ordinary or the sealed art. */
static void MenuMapDoorDisp(int map_label, float map_x, float map_y,    /* 1764 */
                            u_char alpha)
{
    int                   i;
    const MAP_DOOR_POINT *door_point_dat;

    door_point_dat = menu_map_door_data[map_label];                     /* 1770 */

    if (door_point_dat == nullptr) {                                    /* 1773 */
        return;
    }

    for (i = 0; door_point_dat[i].room_label1 != -1                     /* 1774 */
                || door_point_dat[i].room_label2 != -1; i++) {          /* 1776 */
        if (GetRoomInfo(door_point_dat[i].room_label1) == 1
            || GetRoomInfo(door_point_dat[i].room_label2) == 1) {       /* 1781 */
            switch (door_point_dat[i].door_type_label) {                /* 1782 */
            case 0:
            case 1:
            case 2:
            case 3:
            case 4:
                if (door_point_dat[i].ghost_seal_door_label == -1) {    /* 1789 */
                    MenuMapNormalDoorDisp(map_label, map_x, map_y, alpha,
                                          &door_point_dat[i]);          /* 1790 */
                }
                else {
                    MenuMapGhostSealNormalDoorDisp(map_label, map_x, map_y,
                                                   alpha,
                                                   &door_point_dat[i]); /* 1793 */
                }
                break;                                                  /* 1795 */

            case 5:
            case 6:
            case 7:
            case 8:
                if (door_point_dat[i].ghost_seal_door_label == -1) {    /* 1801 */
                    MenuMapDoubleDoorDisp(map_label, map_x, map_y, alpha,
                                          &door_point_dat[i]);          /* 1802 */
                }
                else {
                    MenuMapGhostSealDoubleDoorDisp(map_label, map_x, map_y,
                                                   alpha,
                                                   &door_point_dat[i]); /* 1805 */
                }
                break;                                                  /* 1807 */

            /* A real empty case, not a hole: the range check is `< 10` and
             * the jump table has its own slot for 9 pointing at the break
             * label, while out-of-range reaches the assert.  Type 9 is a
             * door the map deliberately does not mark. */
            case 9:
                break;

            default:
                PRINT_ASSERT("Error! map_door_point.door_type_label");  /* 1812 */
                break;
            }
        }
    }                                                                   /* 1816 */
}

/* One leaf.  Five door types, five sprites -- note 0 and 1 are the other way
 * round from what the numbering suggests (0 takes the horizontal bar and 1
 * the vertical one). */
static void MenuMapNormalDoorDisp(int map_label, float map_x, float map_y, /* 1831 */
                                  u_char alpha,
                                  const MAP_DOOR_POINT *door_data)
{
    DISP_SPRT map_ds;

    switch (door_data->door_type_label) {                               /* 1838 */
    case 0:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x27]);                    /* 1841 */
        break;

    case 1:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x26]);                    /* 1844 */
        break;

    case 2:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x29]);                    /* 1847 */
        break;

    case 3:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x2b]);                    /* 1850 */
        break;

    case 4:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x2d]);                    /* 1853 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1856 */
        break;
    }

    map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7);     /* 1860 */
    map_ds.alphar = 0x48;                                               /* 1861 */

    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 1863 */
        map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].normal; /* 1864 */
        map_ds.sch = map_ds.scw;
        map_ds.csx = door_data->x * map_ds.scw + map_x;                 /* 1866 */
        map_ds.csy = door_data->y * map_ds.scw + map_y;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 1868 */
        map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].big;   /* 1869 */
        map_ds.sch = map_ds.scw;
        map_ds.csx = door_data->x * map_ds.scw + map_x;
        map_ds.csy = door_data->y * map_ds.scw + map_y;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
    }

    if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                             (float)map_ds.w, (float)map_ds.h) != 0) {  /* 1870 */
        DispSprD(&map_ds);                                              /* 1872 */
    }
}

/* Two leaves, drawn from the same sprite twice with the second offset by its
 * own width and/or height.  Type 7 is the odd one: it has NO un-offset leaf
 * -- leaf 0 goes at (x + w, y) and leaf 1 at (x, y + h) -- while 5, 6 and 8
 * all put leaf 0 at (x, y).  Reproduced as found. */
static void MenuMapDoubleDoorDisp(int map_label, float map_x, float map_y, /* 1887 */
                                  u_char alpha,
                                  const MAP_DOOR_POINT *door_data)
{
    DISP_SPRT map_ds;
    int       i;
    float     tmp_x;
    float     tmp_y;

    tmp_x = 0.0f;                                                       /* 1893 */
    tmp_y = 0.0f;                                                       /* 1894 */

    for (i = 0; i < 2; i++) {                                           /* 1897 */
        switch (door_data->door_type_label) {                           /* 1898 */
        case 5:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x27]);                /* 1900 */

            if (i == 0) {                                               /* 1902 */
                tmp_x = door_data->x;                                   /* 1903 */
                tmp_y = door_data->y;                                   /* 1904 */
            }
            else {
                tmp_x = door_data->x + (float)map_ds.w;                 /* 1907 */
                tmp_y = door_data->y;                                   /* 1908 */
            }
            break;                                                      /* 1910 */

        case 6:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x26]);                /* 1912 */

            if (i == 0) {                                               /* 1914 */
                tmp_x = door_data->x;                                   /* 1915 */
                tmp_y = door_data->y;                                   /* 1916 */
            }
            else {
                tmp_x = door_data->x;                                   /* 1932 */
                tmp_y = door_data->y + (float)map_ds.h;                 /* 1934 */
            }
            break;

        case 7:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x29]);                /* 1924 */

            if (i == 0) {                                               /* 1926 */
                tmp_x = door_data->x + (float)map_ds.w;                 /* 1927 */
                tmp_y = door_data->y;                                   /* 1928 */
            }
            else {
                tmp_x = door_data->x;                                   /* 1932 */
                tmp_y = door_data->y + (float)map_ds.h;                 /* 1934 */
            }
            break;

        case 8:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x2b]);                /* 1936 */

            if (i == 0) {                                               /* 1938 */
                tmp_x = door_data->x;                                   /* 1939 */
                tmp_y = door_data->y;                                   /* 1940 */
            }
            else {
                tmp_x = door_data->x + (float)map_ds.w;                 /* 1943 */
                tmp_y = door_data->y + (float)map_ds.h;                 /* 1944 */
            }
            break;                                                      /* 1946 */

        default:
            PRINT_ASSERT("Error! MenuMapDoubleDoorDisp");               /* 1948 */
            break;
        }

        map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7); /* 1950 */
        map_ds.alphar = 0x48;                                           /* 1951 */

        if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {     /* 1953 */
            map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].normal; /* 1954 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = tmp_x * map_ds.scw + map_x;                    /* 1956 */
            map_ds.csy = tmp_y * map_ds.scw + map_y;
            map_ds.x = map_ds.csx;
            map_ds.y = map_ds.csy;
        }
        else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {   /* 1959 */
            map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].big; /* 1960 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = tmp_x * map_ds.scw + map_x;                    /* 1962 */
            map_ds.csy = tmp_y * map_ds.scw + map_y;
            map_ds.x = map_ds.csx;
            map_ds.y = map_ds.csy;
        }

        if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                                 (float)map_ds.w, (float)map_ds.h) != 0) { /* 1964 */
            DispSprD(&map_ds);                                          /* 1966 */
        }
    }                                                                   /* 1968 */
}

/* A sealed door.  States 0 and 3 -- never sealed, and already broken -- fall
 * straight back to the ordinary draw; the sealed art only covers types 0..3,
 * and state 1 adds the seal mark on top. */
static void MenuMapGhostSealNormalDoorDisp(int map_label, float map_x,  /* 1982 */
                                           float map_y, u_char alpha,
                                           const MAP_DOOR_POINT *door_data)
{
    DISP_SPRT map_ds;
    int       seal_state;

    seal_state = GetGhostSealDoorState(door_data->ghost_seal_door_label); /* 1988 */

    if (seal_state == 0 || seal_state == 3) {                           /* 1990 */
        MenuMapNormalDoorDisp(map_label, map_x, map_y, alpha, door_data); /* 1992 */

        return;
    }

    switch (door_data->door_type_label) {                               /* 1996 */
    case 0:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x25]);                    /* 1999 */
        break;

    case 1:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x24]);                    /* 2002 */
        break;

    case 2:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x28]);                    /* 2005 */
        break;

    case 3:
        CopySprDToSpr(&map_ds, &menu_map_tex[0x2a]);                    /* 2008 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 2010 */
        break;
    }

    map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7);     /* 2014 */
    map_ds.alphar = 0x48;                                               /* 2015 */

    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 2017 */
        map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].normal; /* 2018 */
        map_ds.sch = map_ds.scw;
        map_ds.csx = door_data->x * map_ds.scw + map_x;                 /* 2020 */
        map_ds.csy = door_data->y * map_ds.scw + map_y;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 2022 */
        map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].big;   /* 2023 */
        map_ds.sch = map_ds.scw;
        map_ds.csx = door_data->x * map_ds.scw + map_x;
        map_ds.csy = door_data->y * map_ds.scw + map_y;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
    }

    if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                             (float)map_ds.w, (float)map_ds.h) != 0) {  /* 2026 */
        DispSprD(&map_ds);                                              /* 2028 */
    }

    if (seal_state == 1) {                                              /* 2032 */
        MenuMapGhostSealMarkDisp(map_label, map_x, map_y, alpha,
                                 door_data->ghost_seal_door_label);     /* 2034 */
    }
}

/* The double-leaf sealed door.  Same shape as MenuMapDoubleDoorDisp() with
 * the sealed sprites, including type 7's missing un-offset leaf, and the
 * seal mark added once at the end rather than per leaf. */
static void MenuMapGhostSealDoubleDoorDisp(int map_label, float map_x,  /* 2050 */
                                           float map_y, u_char alpha,
                                           const MAP_DOOR_POINT *door_data)
{
    DISP_SPRT map_ds;
    int       i;
    int       seal_state;
    float     tmp_x;
    float     tmp_y;

    tmp_x = 0.0f;                                                       /* 2058 */
    tmp_y = 0.0f;

    seal_state = GetGhostSealDoorState(door_data->ghost_seal_door_label); /* 2062 */

    if (seal_state == 0 || seal_state == 3) {                           /* 2064 */
        MenuMapDoubleDoorDisp(map_label, map_x, map_y, alpha, door_data); /* 2066 */

        return;
    }

    for (i = 0; i < 2; i++) {                                           /* 2070 */
        switch (door_data->door_type_label) {                           /* 2072 */
        case 5:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x25]);                /* 2074 */

            if (i == 0) {                                               /* 2076 */
                tmp_x = door_data->x;                                   /* 2077 */
                tmp_y = door_data->y;                                   /* 2078 */
            }
            else {
                tmp_x = door_data->x + (float)map_ds.w;                 /* 2081 */
                tmp_y = door_data->y;                                   /* 2082 */
            }
            break;

        case 6:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x24]);                /* 2086 */

            if (i == 0) {                                               /* 2088 */
                tmp_x = door_data->x;                                   /* 2089 */
                tmp_y = door_data->y;                                   /* 2090 */
            }
            else {
                tmp_x = door_data->x;                                   /* 2106 */
                tmp_y = door_data->y + (float)map_ds.h;                 /* 2108 */
            }
            break;

        case 7:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x28]);                /* 2098 */

            if (i == 0) {                                               /* 2100 */
                tmp_x = door_data->x + (float)map_ds.w;                 /* 2101 */
                tmp_y = door_data->y;                                   /* 2102 */
            }
            else {
                tmp_x = door_data->x;                                   /* 2106 */
                tmp_y = door_data->y + (float)map_ds.h;                 /* 2108 */
            }
            break;

        case 8:
            CopySprDToSpr(&map_ds, &menu_map_tex[0x2a]);                /* 2110 */

            if (i == 0) {                                               /* 2112 */
                tmp_x = door_data->x;                                   /* 2113 */
                tmp_y = door_data->y;                                   /* 2114 */
            }
            else {
                tmp_x = door_data->x + (float)map_ds.w;                 /* 2117 */
                tmp_y = door_data->y + (float)map_ds.h;                 /* 2118 */
            }
            break;

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 2122 */
            break;
        }

        map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7); /* 2126 */
        map_ds.alphar = 0x48;                                           /* 2127 */

        if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {     /* 2129 */
            map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].normal; /* 2130 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = tmp_x * map_ds.scw + map_x;                    /* 2132 */
            map_ds.csy = tmp_y * map_ds.scw + map_y;
            map_ds.x = map_ds.csx;
            map_ds.y = map_ds.csy;
        }
        else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {   /* 2134 */
            map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].big; /* 2135 */
            map_ds.sch = map_ds.scw;
            map_ds.csx = tmp_x * map_ds.scw + map_x;                    /* 2137 */
            map_ds.csy = tmp_y * map_ds.scw + map_y;
            map_ds.x = map_ds.csx;
            map_ds.y = map_ds.csy;
        }

        if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                                 (float)map_ds.w, (float)map_ds.h) != 0) { /* 2139 */
            DispSprD(&map_ds);                                          /* 2141 */
        }
    }

    if (seal_state == 1) {                                              /* 2143 */
        MenuMapGhostSealMarkDisp(map_label, map_x, map_y, alpha,
                                 door_data->ghost_seal_door_label);     /* 2144 */
    }
}

/* The seal mark itself, placed from ghost_seal_door_data[] rather than from
 * the door record -- the mark sits beside the door, not on it. */
static void MenuMapGhostSealMarkDisp(int map_label, float map_x, float map_y, /* 2160 */
                                     u_char alpha, int ghost_seal_door_label)
{
    DISP_SPRT map_ds;

    CopySprDToSpr(&map_ds, &menu_map_tex[MENU_MAP_TEX_SEAL_MARK]);      /* 2165 */
    map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7);     /* 2167 */

    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 2169 */
        map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].normal; /* 2170 */
        map_ds.sch = map_ds.scw;
        map_ds.csx = ghost_seal_door_data[ghost_seal_door_label].pos_x
                     * map_ds.scw + map_x;                              /* 2172 */
        map_ds.csy = ghost_seal_door_data[ghost_seal_door_label].pos_y
                     * map_ds.scw + map_y;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 2174 */
        map_ds.scw = map_scall_dat[menu_map_disp.disp_map_label].big;   /* 2175 */
        map_ds.sch = map_ds.scw;
        map_ds.csx = ghost_seal_door_data[ghost_seal_door_label].pos_x
                     * map_ds.scw + map_x;
        map_ds.csy = ghost_seal_door_data[ghost_seal_door_label].pos_y
                     * map_ds.scw + map_y;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
    }

    if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                             (float)map_ds.w, (float)map_ds.h) != 0) {  /* 2179 */
        DispSprD(&map_ds);                                              /* 2181 */
    }
}

/* The player arrow.  Its position comes back from ChangeWorldPosToWinPos()
 * already in sheet units, and the rotation is the player's own Y facing
 * turned into degrees with 360 added so the sprite never sees a negative
 * angle.  Note the sprite is offset by half its size and the rotation centre
 * put back at the middle -- 24/44 at 1x, 48/88 at 2x. */
static void MenuMapPlyrPosDisp(int map_label, float map_x, float map_y, /* 2194 */
                               u_char alpha)
{
    float     pos_x;
    float     pos_y;
    DISP_SPRT map_ds;

    pos_x = 0.0f;                                                       /* 2199 */
    pos_y = 0.0f;

    ChangeWorldPosToWinPos(&pos_x, &pos_y, map_label,
                           plyr_wrk.cmn_wrk.mbox.pos,
                           menu_map_disp.map_scall_flg);                /* 2202 */

    CopySprDToSpr(&map_ds, &menu_map_tex[MENU_MAP_TEX_PLYR]);           /* 2205 */

    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 2207 */
        map_ds.x = (pos_x + map_x) - 24.0f;                             /* 2208 */
        map_ds.y = (pos_y + map_y) - 44.0f;
        map_ds.crx = map_ds.x + 24.0f;                                  /* 2210 */
        map_ds.cry = map_ds.y + 44.0f;
        map_ds.rot = (plyr_wrk.cmn_wrk.mbox.rot[1] * 180.0f) / 3.1415925f
                     + 360.0f; /* 2212 */
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 2214 */
        map_ds.scw = 2.0f;                                              /* 2215 */
        map_ds.sch = 2.0f;
        map_ds.csx = (pos_x + map_x) - 48.0f;                           /* 2217 */
        map_ds.csy = (pos_y + map_y) - 88.0f;
        map_ds.x = map_ds.csx;
        map_ds.y = map_ds.csy;
        map_ds.crx = map_ds.csx + 48.0f;                                /* 2219 */
        map_ds.cry = map_ds.csy + 88.0f;
        map_ds.rot = (plyr_wrk.cmn_wrk.mbox.rot[1] * 180.0f) / 3.1415925f
                     + 360.0f; /* 2220 */
    }

    map_ds.alpha = (u_char)(((int)map_ds.alpha * (int)alpha) >> 7);     /* 2221 */

    if (MenuMapDispAreaCheck(map_label, map_ds.x, map_ds.y,
                             (float)map_ds.w, (float)map_ds.h) != 0) {  /* 2222 */
        DispSprD(&map_ds);                                              /* 2223 */
    }
}

/* The four scroll arrows round the crosshair.  One sprite per direction; the
 * left and right ones are the same art rotated 270 and 90 about their own
 * leading edge, which is why their rotation centre is offset by w or h.  The
 * whole group is invisible outside the main step -- alpha is forced to 0.
 * Lines 2249..2277 hold no code. */
static void MenuMapCenterDisp(int off_x, int off_y, u_char alpha)       /* 2236 */
{
    DISP_SPRT center_ds;
    int       i;

    PK2SendVram((uintptr_t)(uintptr_t)menu_map_bg_addr, -1, -1, 0);         /* 2242 */

    MenuMapCenterAnim();                                                /* 2245 */

    if (menu_wrk.step == MENU_MAP_STEP_MAIN) {                          /* 2247 */
        alpha = menu_map_disp.tri_alpha;                                /* 2248 */
    }
    else {
        alpha = 0;
    }

    for (i = MENU_MAP_TEX_TRI; i <= MENU_MAP_TEX_TRI_END; i++) {        /* 2278 */
        CopySprDToSpr(&center_ds, &menu_map_tex[i]);                    /* 2282 */
        center_ds.alpha = (u_char)(((int)center_ds.alpha * (int)alpha) >> 7); /* 2283 */

        if (i == MENU_MAP_TEX_TRI + 1) {                                /* 2284 */
            center_ds.rot = 270.0f;                                     /* 2285 */
            center_ds.crx = center_ds.x + (float)off_x;                 /* 2286 */
            center_ds.cry = center_ds.y + (float)center_ds.w + (float)off_y;
            center_ds.x = center_ds.crx;
            center_ds.y = center_ds.cry;
        }
        else if (i == MENU_MAP_TEX_TRI + 2) {                           /* 2288 */
            center_ds.rot = 90.0f;                                      /* 2289 */
            center_ds.crx = center_ds.x + (float)center_ds.h + (float)off_x; /* 2290 */
            center_ds.cry = center_ds.y + (float)off_y;
            center_ds.x = center_ds.crx;
            center_ds.y = center_ds.cry;
        }
        else {
            center_ds.x = center_ds.x + (float)off_x;                   /* 2293 */
            center_ds.y = center_ds.y + (float)off_y;
        }

        DispSprD(&center_ds);                                           /* 2295 */
    }                                                                   /* 2299 */
}

/* The arrows' fade.  Three states: off until any direction is held, a
 * twenty-frame fade in, then a pulse that runs 0..19 up, holds one frame at
 * full, and 21..39 back down.  Releasing the pad during the pulse lets the
 * cycle finish and then drops back to off, which is why the "is anything
 * held" scan appears three times. */
static void MenuMapCenterAnim(void)                                     /* 2320 */
{
    int i;

    /* Both tables are the same 20-frame 0 -> 128 ramp; the ROM emitted two
     * copies because they are two separate declarations. */
    static const ALPHA_ANIM_TBL csr_in_alpha_tbl[2] =        /* rdata 3be130 */
    {
        {  0, 128,  0, 20 },
        { -1,  -1, -1, -1 },
    };
    static const ALPHA_ANIM_TBL csr_loop_alpha_tbl[2] =      /* rdata 3be140 */
    {
        {  0, 128,  0, 20 },
        { -1,  -1, -1, -1 },
    };

    menu_map_disp.tri_alpha = 0;                                        /* 2333 */

    if (menu_map_disp.tri_anim_step == MENU_MAP_TRI_ANIM_OFF) {         /* 2336 */
        for (i = 0; i < MENU_MAP_TRI_NUM; i++) {                        /* 2338 */
            if (menu_map_disp.tri_pad_flg[i] == 1) {                    /* 2344 */
                menu_map_disp.tri_timer = 0;                            /* 2340 */
                menu_map_disp.tri_anim_step = MENU_MAP_TRI_ANIM_IN;     /* 2341 */

                break;
            }
        }
    }

    if (menu_map_disp.tri_anim_step == MENU_MAP_TRI_ANIM_IN) {          /* 2347 */
        for (i = 0; i < MENU_MAP_TRI_NUM; i++) {                        /* 2349 */
            if (menu_map_disp.tri_pad_flg[i] == 1) {                    /* 2353 */
                break;
            }
        }

        /* Nothing held any more: run the same ramp backwards. */
        if (i == MENU_MAP_TRI_NUM) {                                    /* 2356 */
            menu_map_disp.tri_alpha =
                Anim2D_CalcNowAlpha(csr_in_alpha_tbl,
                                    menu_map_disp.tri_timer);           /* 2358 */
            menu_map_disp.tri_timer--;                                  /* 2360 */

            if (menu_map_disp.tri_timer <= 0) {                         /* 2362 */
                menu_map_disp.tri_anim_step = MENU_MAP_TRI_ANIM_OFF;    /* 2363 */
            }
        }
        else {
            menu_map_disp.tri_alpha =
                Anim2D_CalcNowAlpha(csr_in_alpha_tbl,
                                    menu_map_disp.tri_timer);           /* 2370 */
            menu_map_disp.tri_timer++;                                  /* 2372 */

            if (menu_map_disp.tri_timer > MENU_MAP_TRI_ANIM_TIME - 1) { /* 2374 */
                menu_map_disp.tri_timer = MENU_MAP_TRI_ANIM_TIME;       /* 2375 */
                menu_map_disp.tri_anim_step = MENU_MAP_TRI_ANIM_LOOP;   /* 2376 */
            }
        }
    }
    else if (menu_map_disp.tri_anim_step == MENU_MAP_TRI_ANIM_LOOP) {   /* 2380 */
        if (menu_map_disp.tri_timer < MENU_MAP_TRI_ANIM_TIME) {         /* 2381 */
            menu_map_disp.tri_alpha =
                Anim2D_CalcNowAlpha(csr_loop_alpha_tbl,
                                    menu_map_disp.tri_timer);           /* 2383 */
            menu_map_disp.tri_timer++;                                  /* 2385 */
        }
        else if (menu_map_disp.tri_timer == MENU_MAP_TRI_ANIM_TIME) {   /* 2387 */
            menu_map_disp.tri_timer = MENU_MAP_TRI_ANIM_TIME + 1;       /* 2388 */
            menu_map_disp.tri_alpha = 0x80;                             /* 2390 */
        }
        else {
            /* Past the hold the ramp is read backwards, which is what turns
             * one 20-frame table into a 40-frame pulse. */
            menu_map_disp.tri_alpha =
                Anim2D_CalcNowAlpha(csr_loop_alpha_tbl,
                                    MENU_MAP_TRI_ANIM_TIME
                                        - menu_map_disp.tri_timer % MENU_MAP_TRI_ANIM_TIME); /* 2394 */
            menu_map_disp.tri_timer++;                                  /* 2396 */

            if (menu_map_disp.tri_timer > MENU_MAP_TRI_LOOP_TIME) {     /* 2398 */
                for (i = 0; i < MENU_MAP_TRI_NUM; i++) {                /* 2400 */
                    if (menu_map_disp.tri_pad_flg[i] == 1) {            /* 2404 */
                        break;
                    }
                }

                if (i == MENU_MAP_TRI_NUM) {                            /* 2407 */
                    menu_map_disp.tri_anim_step = MENU_MAP_TRI_ANIM_OFF; /* 2408 */
                }
                else {
                    menu_map_disp.tri_timer = 0;                        /* 2412 */
                }
            }
        }
    }
}

/* The room snapshot.  The black plate under it is always drawn, so an empty
 * frame is what the player sees before the load lands or over a room they
 * have not been in; the picture itself needs the room seen, the house
 * condition met and the load finished. */
static void MenuMapSnapShotDisp(int off_x, int off_y, u_char alpha)     /* 2428 */
{
    DISP_SPRT snap_ds;
    DISP_SQAR dsq;
    SQAR_DAT  snap_bg = { 105, 62, 499, 66, 160, 0, 0, 0, 0x80 };       /* 2431 */

    CopySqrDToSqr(&dsq, &snap_bg);                                      /* 2437 */
    dsq.alpha = (u_char)(((int)dsq.alpha * (int)alpha) >> 7);           /* 2438 */

    DispSqrD(&dsq);                                                     /* 2439 */

    if (menu_map_disp.disp_map_label != -1                              /* 2442 */
        && menu_map_ctrl.hit_room != -1                                 /* 2444 */
        && menu_map_ctrl.snap_load.step == MENU_MAP_LOAD_END            /* 2446 */
        && MenuMapCheckHouseCondition(menu_map_ctrl.hit_room) != 0      /* 2448 */
        && GetRoomInfo(menu_map_ctrl.hit_room) == 1) {                  /* 2450 */
        PK2SendVram((uintptr_t)(uintptr_t)snap_data_addr, -1, -1, 0);       /* 2451 */

        CopySprDToSpr(&snap_ds, &menu_map_tex[MENU_MAP_TEX_SNAP]);      /* 2454 */
        snap_ds.x = snap_ds.x + (float)off_x;                           /* 2455 */
        snap_ds.y = snap_ds.y + (float)off_y;
        snap_ds.alpha = (u_char)(((int)snap_ds.alpha * (int)alpha) >> 7); /* 2456 */

        DispSprD(&snap_ds);                                             /* 2457 */
    }
}

static void MenuMapWindowDisp(int off_x, int off_y, u_char alpha)       /* 2473 */
{
    DISP_SPRT win_ds;
    int       i;

    PK2SendVram((uintptr_t)(uintptr_t)menu_map_bg_addr, -1, -1, 0);         /* 2479 */

    for (i = 0; i < MENU_MAP_TEX_WINDOW_NUM; i++) {                     /* 2483 */
        CopySprDToSpr(&win_ds, &menu_map_tex[MENU_MAP_TEX_WINDOW + i]); /* 2484 */
        win_ds.x = win_ds.x + (float)off_x;                             /* 2485 */
        win_ds.y = win_ds.y + (float)off_y;
        win_ds.alpha = (u_char)(((int)win_ds.alpha * (int)alpha) >> 7); /* 2486 */

        DispSprD(&win_ds);                                              /* 2487 */
    }                                                                   /* 2488 */
}

/* The two readouts: the sheet's name on the left and the room's on the right.
 * Each is drawn three times, offset a pixel at a time, with the first two
 * passes in the shadow colour -- which is what gives the text its outline. */
static void MenuMapInfoDisp(int off_x, int off_y, u_char alpha)         /* 2500 */
{
    int i;
    int col_label;

    for (i = 0; i < 3; i++) {                                           /* 2508 */
        if (i == 2) {                                                   /* 2509 */
            col_label = 10;
        }
        else {
            col_label = 11;
        }

        PrintMsg(0x3d, menu_map_disp.disp_map_label,
                 0x39 - i, 0x48 - i, col_label, alpha, 0xa0);           /* 2518 */

        /* Same asymmetry as MenuMapMapDisp(): the house test gates the room
         * NAME only on sheet 0, the grounds. */
        if ((menu_map_disp.disp_map_label != 0                          /* 2521 */
             || MenuMapCheckHouseCondition(menu_map_ctrl.hit_room) != 0) /* 2523 */
            && menu_map_ctrl.hit_room != -1                             /* 2530 */
            && GetRoomInfo(menu_map_ctrl.hit_room) == 1) {              /* 2532 */
            PrintMsg(0x4a, menu_map_ctrl.hit_room,
                     0xed - i, 0x48 - i, col_label, alpha, 0xa0);       /* 2535 */
        }
    }                                                                   /* 2538 */
}

/* The button prompts along the bottom.  Caption group 1 at 1x and 2 at 2x --
 * the zoom changes what CROSS is offering. */
static void MenuMapCaptionDisp(int off_x, int off_y, u_char alpha)      /* 2549 */
{
    if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_NORMAL) {         /* 2553 */
        DrawCmnCapGroup_W(1, 1, alpha, 0);                              /* 2554 */
    }
    else if (menu_map_disp.map_scall_flg == MENU_MAP_SCALL_BIG) {       /* 2557 */
        DrawCmnCapGroup_W(2, 2, alpha, 0);                              /* 2558 */
    }
}
