// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_cam_top.c
//
// The camera menu's top page (menu_cam_top.o).  Forty-two functions -- the
// four exports and thirty-eight statics -- one .rodata table at file scope,
// six more inside the functions that use them, two dispatch tables in .data
// and two work blocks in .sbss.
//
// It is the screen the camera menu opens on: the camera itself down the left
// of the page, and on the right the three things the player can change --
// the addition functions, the loaded film, and the fitted camera parts --
// over the basic-performance ladder, the lens slots, the level gems and the
// spirit score.
//
// Facts worth knowing before touching it:
//
//  * There is one `mode` and it means two different things.  As a dispatch
//    index it selects both menu_cam_top_pad[] and menu_cam_top_disp_func[],
//    and slot 3 is the page proper while 0/1/2 are the three sub-selectors
//    it can open.  So the page spends most of its life at mode 3 and drops
//    to 0/1/2 for as long as a selector is up; there is no separate
//    "sub-mode" flag and no step change when one opens.
//
//  * The cursor is two columns.  csr_yoko 0 is the camera portrait -- CROSS
//    there hands off to the upgrade editor -- and 1 is the three rows, where
//    csr_tate walks 0..2 and maps one-to-one onto the three sub-selector
//    modes.  MenuCamTopPadDecision() is the whole of that: a switch on
//    csr_tate whose three arms open the matching mode, with a default that
//    asserts.
//
//  * Every widget that varies with the cursor derives ONE index from the
//    pair -- `csr_yoko ? csr_tate + 1 : 0` -- and looks it up in a four-entry
//    table.  MenuCamModeTopDisp()'s msg_id[], MenuCamTopCsrBaseDisp()'s
//    base_tex_tbl[] and sqar_dat[], and MenuCamTopCsrLineDisp()'s
//    line_tex_tbl[] are all keyed by it.  That expression is written out at
//    all three sites rather than held anywhere.
//
//  * The two sub-selector cursors skip what the player does not own.  Both
//    MenuCamTopAdditionalFunctionSelPad() and MenuCamTopEquipFuncSelPad()
//    step their cursor in a bounded `for (i = 1; i < 5; i++)` until
//    BIT_FLAGS::IsUp() says the slot is owned, and then play the move cue
//    only `if (i < 4)` -- four steps of +-1 mod 4 is the identity, so i == 4
//    means the cursor came back to itself and nothing actually moved.  Same
//    shape setup_menu.o uses for its costume and difficulty rows.
//
//  * MenuCamTopFilmSelPad() does not move a cursor at all.  LEFT and RIGHT
//    walk the five film types and call ItemUse() on the first one the player
//    is actually carrying, so the "cursor" IS the equipped film.  It then
//    asks GetPlyrEquipmentFilmType() again and picks the cue from whether
//    the answer changed.
//
//  * Fitting and removing a camera part is a straight BIT_FLAGS toggle on
//    mCamPartsSetFlg, with one special case: taking off part 1 clears
//    equip_special[1] and [2] and writes the tray back, because that part is
//    what the second and third sub-function slots hang off.  Slot 0 is left
//    alone.
//
//  * MenuCamTopCmnDisp(), MenuCamTopLevelGemNumDisp(),
//    MenuCamTopGhostPowerDisp() and MenuCamTopCaptionDisp() all take off_x /
//    off_y and read neither -- the folder's usual habit.  Every caller in
//    the build passes 0, 0, so nothing shows.
//
// Line numbers: measured, not guessed.  Every function's *opening* line is
// the `$LM` symbols.txt interleaves immediately before its PROC/STATICPROC
// record, which is not the first line Ghidra reports.  Two GCC habits shape
// the rest of the annotations here:
//
//  * A loop's increment carries the line of the body's CLOSING BRACE, not
//    the `for`.  MenuCamTopEquipFuncSelPad()'s inner `equip_special[i] = 0`
//    loop is the clean proof -- 628 `for`, no note on the body (the
//    fixed_array subscript swallows it), 630 on the `i++`.  So a `/* N */`
//    on a `}` below is a measured number, not decoration.
//
//  * A statement whose only work is an inlined accessor leaves NO note of
//    its own: BIT_FLAGS::IsUp() reports variable.h 852-858, FlgUp 825-833,
//    FlgDown 838-847, and fixed_array::operator[] 124/125.  Those are the
//    gaps in the sequences below; the annotations either side of one are
//    measured and the statement between them is not.
//
// Header lines this object pins: variable.h 825-833 / 838-847 / 852-858
// (BIT_FLAGS FlgUp / FlgDown / IsUp -- the three asserts carry 0x33e, 0x34c
// and 0x35a) and fixed_array.h 124/125.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_cam_top.o.
// All 4 ZERO2.MAP exports plus the 38 statics, verified 4/4 against the map
// and 42/42 against functions.txt (its five remaining entries are the
// fixed_array boilerplate and the CVariable<char,0,3> type_info node).
// MENU_CAM_TOP_CTRL (0x7) and MENU_CAM_TOP_DISP (0x3) are the ROM's own
// types.txt records; film_name_tbl and all six function-local .rodata tables
// are read straight out of the ELF, and both .lit4 floats round-trip
// bit-for-bit (1.0999999f is the truncated 1.1f -- see
// [[ee-gcc-truncates-float-literals]]).

#include "menu_cam_top.h"

#include "menu_cam_main.h"                      /* MenuCamExitReq / the widgets */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_InOutAnimCtrl  */
#include "tim_dat/menu_camera_dat.h"            /* menu_camera_tex[]          */

#include "play_data.h"                          /* GetPlayData_Score          */

#include "../item/prg/item.h"                   /* GetPlyrItemHaveNum / ItemUse */
#include "../item/prg/level_gem.h"              /* GetPlyrLevelGemNum         */
#include "../photo/m_plyr_camera.h"             /* m_plyr_camera              */
#include "../photo/n_equip_tray.h"              /* Get/SetSubFuncArray        */
#include "../plyr/player.h"                     /* GetPlyrEquipmentFilmType   */
#include "../../common/utility2.h"              /* PRINT_ASSERT               */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow / CapGroup_W */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg                   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array           */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay             */
#include "../../system/pad/pad.h"               /* pad / paddat               */

/* --------------------------------------------------------------------------
 *  Constants
 *
 *  All of these names are the port's; the ROM writes the literals.
 * ------------------------------------------------------------------------ */

/* MENU_CAM_TOP_CTRL::step.  There is no load step -- menu_cam_main.o has
 * both paks resident before this page is ever dispatched to. */
#define MENU_CAM_TOP_STEP_INIT      0
#define MENU_CAM_TOP_STEP_MAIN      2
#define MENU_CAM_TOP_STEP_OUT       3

/* MENU_CAM_TOP_DISP::anim_step.  Only END is tested by name here; the rest
 * of the ladder is Zero2Anim2D_InOutAnimCtrl()'s business. */
#define MENU_CAM_TOP_ANIM_OUT       3
#define MENU_CAM_TOP_ANIM_END       4

/* The in and out durations handed to Zero2Anim2D_InOutAnimCtrl(). */
#define MENU_CAM_TOP_FADE_IN_TIME   10
#define MENU_CAM_TOP_FADE_OUT_TIME  5

/* MENU_CAM_TOP_CTRL::mode -- the index into both dispatch tables.  0..2 are
 * the sub-selectors and agree with csr_tate one for one; 3 is the page. */
#define MENU_CAM_TOP_MODE_ADD_FUNC_SEL      0
#define MENU_CAM_TOP_MODE_FILM_SEL          1
#define MENU_CAM_TOP_MODE_EQUIP_FUNC_SEL    2
#define MENU_CAM_TOP_MODE_TOP               3
#define MENU_CAM_TOP_MODE_NUM               4

/* MENU_CAM_TOP_CTRL::csr_yoko -- which column the cursor is in. */
#define MENU_CAM_TOP_CSR_CAMERA     0   /* the portrait; CROSS opens the editor */
#define MENU_CAM_TOP_CSR_MENU       1   /* the three rows                     */

/* MENU_CAM_TOP_CTRL::csr_tate -- the row inside the right-hand column. */
#define MENU_CAM_TOP_ROW_ADD_FUNC       0
#define MENU_CAM_TOP_ROW_FILM           1
#define MENU_CAM_TOP_ROW_EQUIP_FUNC     2
#define MENU_CAM_TOP_ROW_NUM            3

/* How many of each the camera has.  Both are BIT_FLAGS<4>, which is what
 * makes the IsUp() range assert fold away wherever the bound is this. */
#define ADD_FUNC_NUM        4
#define EQUIP_FUNC_NUM      4

/* The three sub-function slots the tray carries. */
#define EQUIP_SPECIAL_NUM   3

/* The camera part whose removal invalidates sub-function slots 1 and 2. */
#define EQUIP_FUNC_SPECIAL_SLOT     1

/* Film types, as GetPlyrEquipmentFilmType() reports them.  07 is the
 * unlimited film -- its count draws as two dashes -- and the fifth is
 * unnumbered and draws a word plate instead of a number. */
#define FILM_TYPE_07        0
#define FILM_TYPE_SPECIAL   4
#define FILM_TYPE_NUM       5

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_ERROR    2
#define SE_DECIDE   3

/* pad[0].rpt bits in the remapped layout, plus the analogue equivalents. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_RIGHT           0x2000
#define PAD_RPT_DOWN            0x4000
#define PAD_RPT_LEFT            0x8000
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3

/* The message bank every caption on this page comes out of. */
#define MENU_CAM_MSG_TYPE       0x31

/* menu_camera_tex[] indices referenced on their own rather than as a range.
 * See tim_dat/menu_camera_dat.c for the whole sheet. */
#define MC_BASE_PLATE       0    /* [0] the page ground, [1] its rotated edge */
#define MC_BASE_EDGE        1
#define MC_BASE_REST        2    /* [2]..[6]                                 */
#define MC_TITLE            9    /* [9],[10] the page title                  */
#define MC_ITEM             61   /* [61]..[66] the four row labels           */
#define MC_BASIC_ITEM       67   /* [67]..[69] the basic-performance labels  */
#define MC_LENS_EMPTY       70   /* [70],[71],[72] one per lens slot         */
#define MC_LENS_EMPTY_ONE   71   /* the single-slot case uses the middle one */
#define MC_BASIC_RAIL       73   /* the three grade rails                    */
#define MC_BASIC_PIP        74   /* one pip per grade step                   */
#define MC_LV_LABEL         85   /* "Lv", with [86]+lv the digit after it    */
#define MC_LV_DIGIT         86
#define MC_FILM_PLATE       110  /* [110],[111] the equipped-film plate      */
#define MC_FILM_SEL_PLATE   122  /* [122],[123] the same, film-selector lit  */
#define MC_FILM_SEL_CSR     124  /* [124],[125] the film selector's cursor   */
#define MC_FILM_SPECIAL     126  /* the unnumbered film's word plate         */
#define MC_FILM_SPECIAL_SEL 127
#define MC_FILM_DASH        128  /* drawn twice for the unlimited film       */
#define MC_FILM_DASH_SEL    129
#define MC_ADD_FUNC_SLOT    134  /* the empty addition-function slot line    */
#define MC_ADD_FUNC_SEL_CSR 136  /* [136],[137] tinted; [138]..[141] plain   */
#define MC_ADD_FUNC_SEL_BOX 138
#define MC_EQUIP_FUNC_SLOT  150  /* the empty camera-part slot line          */
#define MC_EQUIP_FUNC_CSR   151  /* [151],[152] tinted; [153]..[156] plain   */
#define MC_EQUIP_FUNC_BOX   153
#define MC_TITLE_FRAME      230  /* [230],[231], stretched 1.1x across       */

/* menu_camera_tex[] indices that MenuCamTopCsrBaseDisp() treats specially:
 * two that rotate 270 degrees and one that stretches to the plate height. */
#define MC_CSR_BASE_ROT_A   11
#define MC_CSR_BASE_STRETCH 16
#define MC_CSR_BASE_ROT_B   23

/* The height MC_CSR_BASE_STRETCH is scaled to -- the tall row plate's own
 * height, which is also sqar_dat[0].h. */
#define CSR_BASE_STRETCH_H  229.0f

/* The cursor plate's tint and its share of the master alpha. */
#define CSR_BASE_R      0xae
#define CSR_BASE_G      0x77
#define CSR_BASE_B      0x3a
#define CSR_BASE_ALPHA_RATE 51

/* Layout constants the drawing half writes out as literals. */
#define MSG_OFF_X           0x30    /* the caption's offset inside the window */
#define MSG_OFF_Y           0x176
#define ADD_FUNC_STEP       34.0f   /* pitch of the addition-function row    */
#define ADD_FUNC_BASE_X     475.0f  /* .lit4 3ee558                          */
#define ADD_FUNC_ICON_Y     108.0f
#define ADD_FUNC_LINE_Y     117.0f
#define EQUIP_FUNC_STEP     36.0f   /* pitch of the camera-part row          */
#define EQUIP_FUNC_BASE_X   458.0f
#define EQUIP_FUNC_ICON_Y   304.0f
#define EQUIP_FUNC_LINE_Y   314.0f
#define LENS_ONE_X          80.0f   /* the single lens slot                  */
#define LENS_BASE_X         38.0f   /* the three-slot row                    */
#define LENS_STEP           42.0f
#define LENS_Y              253.0f
#define LENS_LV_ONE_X       0x4f    /* where the "Lv" plate goes, per layout */
#define LENS_LV_BASE_X      0x25
#define LENS_LV_STEP        0x2a
#define LENS_LV_Y           0x124
#define BASIC_ROW_STEP      0x23    /* pitch of the three grade rails        */
#define BASIC_RADIUS_Y      0x86
#define BASIC_STOCK_Y       0xa9
#define BASIC_SENSITIVE_Y   0xcc
#define BASIC_PIP_STEP      16
#define GEM_NUM_X           0x43
#define GEM_NUM_Y           0x141
#define SCORE_NUM_X         0x6f
#define SCORE_NUM_Y         0x141
#define FILM_NAME_X         0x20d
#define FILM_NAME_Y         0xd0
#define FILM_NUM_X          0x23b
#define FILM_NUM_Y          0xd0

/* MenuCamNumberDisp() digit faces: 1 is the 17px light one the film
 * selector uses, 2 the 17px dark one the page uses. */
#define NUM_TYPE_SMALL      0
#define NUM_TYPE_LIGHT      1
#define NUM_TYPE_DARK       2

/* --------------------------------------------------------------------------
 *  File statics
 * ------------------------------------------------------------------------ */

/* Which film-name number each film type prints, and the -1 the search stops
 * on.  The names are the numbers themselves -- Type-07, -14, -61, -90 --
 * except FILM_TYPE_SPECIAL, whose 0 is never used because that type draws a
 * word plate instead. */
static int film_name_tbl[6][2] =                        /* rdata 3bd2e0 */
{
    {  0,   7 },
    {  1,  14 },
    {  2,  61 },
    {  3,  90 },
    {  4,   0 },
    { -1,  -1 },
};

/* --------------------------------------------------------------------------
 *  Forward declarations of the file's statics
 * ------------------------------------------------------------------------ */
static void MenuCamTopCtrlInit(void);
static void MenuCamTopPad(void);
static void MenuCamTopPadDecision(void);
static void MenuCamTopMoveEdit(void);
static void MenuCamTopOutReq(void);
static void MenuCamTopFilmSelPad(void);
static void MenuCamTopAdditionalFunctionSelPad(void);
static void MenuCamTopEquipFuncSelPad(void);
static void MenuCamTopDispInit(void);
static void MenuCamModeTopDisp(int off_x, int off_y, u_char alpha);
static void MenuCamModeFilmSelDisp(int off_x, int off_y, u_char alpha);
static void MenuCamModeAddFuncSelDisp(int off_x, int off_y, u_char alpha);
static void MenuCamModeEquipFuncSelDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopCmnDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopTitleFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopBaseDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopCsrBaseDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopItemDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopBasicPerformaneItemDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopCsrLineDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopEquipReinforcedLensDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopReinforcedLensLvNumDisp(int x, int y, u_char alpha, int lv);
static void MenuCamTopLevelGemNumDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopGhostPowerDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopEquipFilmDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopPlyrHaveAddFuncDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopAddFuncLineDisp(float x, float y, u_char alpha, u_char flg);
static void MenuCamTopPlyrHaveEquipFuncDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopEquipFuncLineDisp(float x, float y, u_char alpha);
static void MenuCamTopMsgWindowDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopEquipFilmSelDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopFilmSelCsrDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopAddFuncSelCsrDisp(int off_x, int off_y, u_char alpha);
static void MenuCamTopEquipFuncSelCsrDisp(int off_x, int off_y, u_char alpha);

/* The two dispatch tables, both indexed by MENU_CAM_TOP_CTRL::mode.  Slot 3
 * -- the page itself -- is last, which is why MenuCamTopCtrlInit() opens on
 * a mode of 3 rather than 0. */
static void (*menu_cam_top_pad[MENU_CAM_TOP_MODE_NUM])(void) =   /* data 3216d0 */
{
    MenuCamTopAdditionalFunctionSelPad,
    MenuCamTopFilmSelPad,
    MenuCamTopEquipFuncSelPad,
    MenuCamTopPad,
};

static void (*menu_cam_top_disp_func[MENU_CAM_TOP_MODE_NUM])(int, int, u_char) =
{                                                                /* data 3216e0 */
    MenuCamModeAddFuncSelDisp,
    MenuCamModeFilmSelDisp,
    MenuCamModeEquipFuncSelDisp,
    MenuCamModeTopDisp,
};

static MENU_CAM_TOP_CTRL menu_cam_top_ctrl;             /* sbss 3f4e10 */
static MENU_CAM_TOP_DISP menu_cam_top_disp;             /* sbss 3f4e18 */

/* --------------------------------------------------------------------------
 *  Entry
 * ------------------------------------------------------------------------ */

/* The first-entry variant.  It differs from MenuCamTopInit() only in seeding
 * csr_yoko_backup, which every later entry then restores the cursor from --
 * so a trip into the upgrade editor and back leaves the cursor where it was,
 * but the first opening of the menu always starts on the right-hand column. */
void MenuCamTopFirstInit(void)                                          /* 208 */
{
    menu_cam_top_ctrl.csr_yoko_backup = MENU_CAM_TOP_CSR_MENU;          /* 211 */

    MenuCamTopCtrlInit();                                               /* 214 */
}

void MenuCamTopInit(void)                                               /* 222 */
{
    MenuCamTopCtrlInit();                                               /* 226 */
}

static void MenuCamTopCtrlInit(void)                                    /* 234 */
{
    menu_cam_top_ctrl.mode      = MENU_CAM_TOP_MODE_TOP;                /* 237 */
    menu_cam_top_ctrl.step      = MENU_CAM_TOP_STEP_INIT;               /* 238 */
    menu_cam_top_ctrl.csr_yoko  = menu_cam_top_ctrl.csr_yoko_backup;    /* 239 */
    menu_cam_top_ctrl.csr_tate  = MENU_CAM_TOP_ROW_FILM;                /* 240 */
    menu_cam_top_ctrl.add_csr   = 0;                                    /* 241 */
    menu_cam_top_ctrl.equip_csr = 0;                                    /* 242 */
}

/* One frame.  Non-zero once the page has finished closing; that is what puts
 * MenuCamModeMain()'s sub_step at 3 and releases the parked request. */
int MenuCamTopMain(void)                                                /* 256 */
{
    int res;

    res = 0;                                                            /* 260 */

    if (menu_cam_top_ctrl.step == MENU_CAM_TOP_STEP_INIT) {             /* 262 */
        MenuCamTopDispInit();                                           /* 264 */

        menu_cam_top_ctrl.step = MENU_CAM_TOP_STEP_MAIN;                /* 266 */
    }

    if (menu_cam_top_ctrl.step == MENU_CAM_TOP_STEP_MAIN) {             /* 270 */
        if (menu_cam_top_pad[menu_cam_top_ctrl.mode] != nullptr) {      /* 272 */
            (*menu_cam_top_pad[menu_cam_top_ctrl.mode])();              /* 273 */
        }
    }

    if (menu_cam_top_ctrl.step == MENU_CAM_TOP_STEP_OUT) {              /* 277 */
        res = (menu_cam_top_disp.anim_step == MENU_CAM_TOP_ANIM_END);   /* 278 */
    }

    return res;                                                         /* 284 */
}

/* --------------------------------------------------------------------------
 *  The page's own pad
 *
 *  Two columns.  UP/DOWN only do anything in the right-hand one; LEFT and
 *  RIGHT are what cross between them.
 * ------------------------------------------------------------------------ */

static void MenuCamTopPad(void)                                         /* 294 */
{
    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 298 */
        if (menu_cam_top_ctrl.csr_yoko == MENU_CAM_TOP_CSR_MENU) {      /* 299 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 300 */

            menu_cam_top_ctrl.csr_tate =
                (menu_cam_top_ctrl.csr_tate + 2) % MENU_CAM_TOP_ROW_NUM; /* 301 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 305 */
        if (menu_cam_top_ctrl.csr_yoko == MENU_CAM_TOP_CSR_MENU) {      /* 306 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 307 */

            menu_cam_top_ctrl.csr_tate =
                (menu_cam_top_ctrl.csr_tate + 1) % MENU_CAM_TOP_ROW_NUM; /* 308 */
        }
    }
    else if ((pad[0].one & PAD_RPT_LEFT)
             || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {                     /* 312 */
        if (menu_cam_top_ctrl.csr_yoko == MENU_CAM_TOP_CSR_MENU) {      /* 313 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 314 */

            menu_cam_top_ctrl.csr_yoko = MENU_CAM_TOP_CSR_CAMERA;       /* 315 */
        }
    }
    else if ((pad[0].one & PAD_RPT_RIGHT)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 319 */
        if (menu_cam_top_ctrl.csr_yoko == MENU_CAM_TOP_CSR_CAMERA) {    /* 320 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 321 */

            menu_cam_top_ctrl.csr_yoko = MENU_CAM_TOP_CSR_MENU;         /* 322 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 326 */
        /* No cue here -- MenuCamTopPadDecision() picks between the decide
         * and error ones once it knows whether the row can be entered. */
        menu_cam_top_ctrl.csr_yoko_backup = menu_cam_top_ctrl.csr_yoko; /* 327 */

        MenuCamTopPadDecision();                                        /* 329 */
    }
    else if (*paddat[1] == 1) {                                         /* 332 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 333 */

        MenuCamTopOutReq();                                             /* 336 */
    }
}

/* CROSS.  On the camera column it hands off to the upgrade editor; on the
 * three rows it opens the matching sub-selector, refusing with the error cue
 * if the player owns nothing that row could show. */
static void MenuCamTopPadDecision(void)                                 /* 345 */
{
    int i;

    if (menu_cam_top_ctrl.csr_yoko == MENU_CAM_TOP_CSR_CAMERA) {        /* 351 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 352 */

        MenuCamTopMoveEdit();                                           /* 355 */

        return;
    }

    switch (menu_cam_top_ctrl.csr_tate) {                               /* 358 */
    case MENU_CAM_TOP_ROW_ADD_FUNC:
        if (GetHaveAddFuncNum() > 0) {                                  /* 361 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 362 */

            /* Park the sub-cursor on the first function the player owns. */
            for (i = 0; i < ADD_FUNC_NUM; i++) {                        /* 365 */
                if (m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(i) != 0) {
                    menu_cam_top_ctrl.add_csr = i;                      /* 368 */
                    break;                                              /* 369 */
                }
            }                                                           /* 371 */

            menu_cam_top_disp.csr_anim_timer = 0;                       /* 373 */
            menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_ADD_FUNC_SEL;    /* 374 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 377 */
        }
        break;

    case MENU_CAM_TOP_ROW_FILM:
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 381 */

        menu_cam_top_disp.csr_anim_timer = 0;                           /* 383 */

        /* The ROM stores csr_tate's register rather than a fresh literal
         * here -- it is provably 1 in this arm and still live, so CSE took
         * it.  The two sibling arms materialise their literals because they
         * have already reused that register as a loop counter. */
        menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_FILM_SEL;            /* 385 */
        break;

    case MENU_CAM_TOP_ROW_EQUIP_FUNC:
        if (GetHaveEquipFuncNum() > 0) {                                /* 388 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 389 */

            for (i = 0; i < EQUIP_FUNC_NUM; i++) {                      /* 392 */
                if (m_plyr_camera.camera_power_up.mCamPartsFlg.IsUp(i) != 0) {
                    menu_cam_top_ctrl.equip_csr = i;                    /* 395 */
                    break;                                              /* 396 */
                }
            }                                                           /* 398 */

            menu_cam_top_disp.csr_anim_timer = 0;                       /* 399 */
            menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_EQUIP_FUNC_SEL;  /* 400 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 403 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 407 */
        break;
    }
}

/* The two ways out.  Both start the fade and park step at OUT; what differs
 * is only which request menu_cam_main.o ends up acting on. */
static void MenuCamTopMoveEdit(void)                                    /* 417 */
{
    menu_cam_top_ctrl.step = MENU_CAM_TOP_STEP_OUT;                     /* 420 */
    menu_cam_top_disp.anim_step  = MENU_CAM_TOP_ANIM_OUT;               /* 421 */
    menu_cam_top_disp.anim_timer = 0;                                   /* 422 */

    MenuCamGoToEditReq();                                               /* 425 */
}

static void MenuCamTopOutReq(void)                                      /* 433 */
{
    menu_cam_top_ctrl.step = MENU_CAM_TOP_STEP_OUT;                     /* 436 */
    menu_cam_top_disp.anim_step  = MENU_CAM_TOP_ANIM_OUT;               /* 437 */
    menu_cam_top_disp.anim_timer = 0;                                   /* 438 */

    MenuCamExitReq();                                                   /* 441 */
}

/* --------------------------------------------------------------------------
 *  The three sub-selectors
 * ------------------------------------------------------------------------ */

/* The film selector has no cursor of its own: LEFT and RIGHT walk the five
 * film types and load the first one the player is carrying, so the equipped
 * film IS the cursor.  Which cue plays is decided afterwards, by asking
 * whether the equipped type actually changed. */
static void MenuCamTopFilmSelPad(void)                                  /* 449 */
{
    int i;
    int equip_film;
    int before_equip_film;

    before_equip_film = GetPlyrEquipmentFilmType();                     /* 457 */
    equip_film        = before_equip_film;                              /* 458 */

    /* ROM ODDITY, reproduced: the result is discarded.  functions.txt lists
     * no local for it, so this really is a bare call. */
    GetPlyrItemHaveNum(equip_film);                                     /* 459 */

    if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {
        for (i = 0; i < FILM_TYPE_NUM + 1; i++) {                       /* 464 */
            if (equip_film == FILM_TYPE_07) {                           /* 465 */
                equip_film = FILM_TYPE_SPECIAL;
            }
            else {
                equip_film--;
            }

            if (GetPlyrItemHaveNum(equip_film) > 0) {                   /* 473 */
                ItemUse(equip_film, 1);                                 /* 475 */
                break;                                                  /* 476 */
            }
        }                                                               /* 478 */

        if (before_equip_film != GetPlyrEquipmentFilmType()) {          /* 481 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 482 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 485 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 489 */
        for (i = 0; i < FILM_TYPE_NUM + 1; i++) {                       /* 490 */
            if (equip_film == FILM_TYPE_SPECIAL) {                      /* 491 */
                equip_film = FILM_TYPE_07;
            }
            else {
                equip_film++;
            }

            if (GetPlyrItemHaveNum(equip_film) > 0) {                   /* 499 */
                ItemUse(equip_film, 1);                                 /* 501 */
                break;                                                  /* 502 */
            }
        }                                                               /* 504 */

        if (before_equip_film != GetPlyrEquipmentFilmType()) {          /* 507 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 508 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 511 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 515 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 518 */

        menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_TOP;
    }
    else if (*paddat[1] == 1) {                                         /* 521 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 522 */

        menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_TOP;                 /* 524 */
    }
}

/* The addition-function selector.  LEFT/RIGHT step add_csr to the next slot
 * the player owns; four steps is the identity, so `i < 4` is "the cursor
 * actually moved" and is what gates the cue. */
static void MenuCamTopAdditionalFunctionSelPad(void)                    /* 533 */
{
    int i;

    if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT)) { /* 539 */
        for (i = 1; i < ADD_FUNC_NUM + 1; i++) {                        /* 540 */
            menu_cam_top_ctrl.add_csr =
                (menu_cam_top_ctrl.add_csr + 3) % ADD_FUNC_NUM;         /* 541 */

            if (m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(
                    menu_cam_top_ctrl.add_csr) != 0) {
                break;
            }
        }                                                               /* 550 */

        /* Cross-jumped into the RIGHT arm's copy below. */
        if (i < ADD_FUNC_NUM) {
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
        }
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 553 */
        for (i = 1; i < ADD_FUNC_NUM + 1; i++) {                        /* 554 */
            menu_cam_top_ctrl.add_csr =
                (menu_cam_top_ctrl.add_csr + 1) % ADD_FUNC_NUM;         /* 555 */

            if (m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(
                    menu_cam_top_ctrl.add_csr) != 0) {
                break;
            }
        }

        if (i < ADD_FUNC_NUM) {                                         /* 559 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 560 */
        }
    }                                                                   /* 562 */
    else if (*paddat[1] == 1) {                                         /* 567 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 568 */

        menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_TOP;                 /* 570 */
    }
}

/* The camera-part selector.  Same cursor walk as its sibling, plus CROSS,
 * which toggles the part on or off. */
static void MenuCamTopEquipFuncSelPad(void)                             /* 579 */
{
    int                   i;
    fixed_array<char, EQUIP_SPECIAL_NUM> equip_special;

    m_plyr_camera.eq_tray.GetSubFuncArray(&equip_special[0]);           /* 585 */

    if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT)) { /* 590 */
        for (i = 1; i < EQUIP_FUNC_NUM + 1; i++) {                      /* 591 */
            menu_cam_top_ctrl.equip_csr =
                (menu_cam_top_ctrl.equip_csr + 3) % EQUIP_FUNC_NUM;     /* 592 */

            if (m_plyr_camera.camera_power_up.mCamPartsFlg.IsUp(
                    menu_cam_top_ctrl.equip_csr) != 0) {
                break;
            }
        }                                                               /* 601 */

        /* Cross-jumped into the RIGHT arm's copy below. */
        if (i < EQUIP_FUNC_NUM) {
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);
        }
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 604 */
        for (i = 1; i < EQUIP_FUNC_NUM + 1; i++) {                      /* 605 */
            menu_cam_top_ctrl.equip_csr =
                (menu_cam_top_ctrl.equip_csr + 1) % EQUIP_FUNC_NUM;     /* 606 */

            if (m_plyr_camera.camera_power_up.mCamPartsFlg.IsUp(
                    menu_cam_top_ctrl.equip_csr) != 0) {
                break;
            }
        }

        if (i < EQUIP_FUNC_NUM) {                                       /* 610 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 611 */
        }
    }                                                                   /* 613 */
    else if (*paddat[0] == 1) {                                         /* 618 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 619 */

        if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                menu_cam_top_ctrl.equip_csr) != 0) {
            m_plyr_camera.camera_power_up.mCamPartsSetFlg.FlgDown(
                menu_cam_top_ctrl.equip_csr);

            /* Slots 1 and 2 of the tray hang off this part, so taking it
             * off empties them.  Slot 0 is left alone. */
            if (menu_cam_top_ctrl.equip_csr == EQUIP_FUNC_SPECIAL_SLOT) { /* 626 */
                for (i = 1; i < EQUIP_SPECIAL_NUM; i++) {               /* 628 */
                    equip_special[i] = 0;
                }                                                       /* 630 */

                m_plyr_camera.eq_tray.SetSubFuncArray(&equip_special[0]);
            }
        }
        else {
            m_plyr_camera.camera_power_up.mCamPartsSetFlg.FlgUp(
                menu_cam_top_ctrl.equip_csr);
        }
    }
    else if (*paddat[1] == 1) {                                         /* 641 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 642 */

        menu_cam_top_ctrl.mode = MENU_CAM_TOP_MODE_TOP;                 /* 644 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing -- the frame
 * ------------------------------------------------------------------------ */

static void MenuCamTopDispInit(void)                                    /* 657 */
{
    menu_cam_top_disp.anim_step  = 0;                                   /* 660 */
    menu_cam_top_disp.anim_timer = 0;                                   /* 661 */
}

void MenuCamTopDisp(void)                                               /* 669 */
{
    u_char alpha;

    if ((menu_cam_top_ctrl.step == MENU_CAM_TOP_STEP_MAIN)
        || (menu_cam_top_ctrl.step == MENU_CAM_TOP_STEP_OUT)) {         /* 677 */
        alpha = Zero2Anim2D_InOutAnimCtrl(&menu_cam_top_disp.anim_step,
                                          &menu_cam_top_disp.anim_timer,
                                          MENU_CAM_TOP_FADE_IN_TIME,
                                          MENU_CAM_TOP_FADE_OUT_TIME);  /* 678 */

        if (menu_cam_top_disp.anim_step != MENU_CAM_TOP_ANIM_END) {     /* 680 */
            PK2SendVram((uintptr_t)(uintptr_t)GetMenuCameraPk2Addr(),
                        -1, -1, 0);                                     /* 681 */

            MenuCamTopCmnDisp(0, 0, alpha);                             /* 684 */

            if (menu_cam_top_disp_func[menu_cam_top_ctrl.mode] != nullptr) { /* 686 */
                (*menu_cam_top_disp_func[menu_cam_top_ctrl.mode])(0, 0, alpha);
            }                                                           /* 687 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Drawing -- the four modes
 *
 *  Each draws the equipped-film readout, then its own cursor, then the
 *  caption for whatever is selected.  The page's own caption is the one that
 *  varies with the cursor pair.
 * ------------------------------------------------------------------------ */

static void MenuCamModeTopDisp(int off_x, int off_y, u_char alpha)       /* 701 */
{
    int disp_data;

    static int msg_id[4] =                                  /* rdata 3bd1c8 */
    {
        0, 2, 1, 3,
    };

    disp_data = 0;                                                      /* 712 */

    if (menu_cam_top_ctrl.csr_yoko != MENU_CAM_TOP_CSR_CAMERA) {
        disp_data = menu_cam_top_ctrl.csr_tate + 1;                     /* 717 */
    }

    MenuCamTopEquipFilmDisp(0, 0, alpha);                               /* 722 */

    PrintMsg(MENU_CAM_MSG_TYPE, msg_id[disp_data],
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 725 */
}

static void MenuCamModeFilmSelDisp(int off_x, int off_y, u_char alpha)   /* 736 */
{
    int equip_film;
    int i;

    /* Which caption each film type gets.  Keyed the same way film_name_tbl
     * is, and terminated the same way. */
    static int film_msg_tbl[6][2] =                         /* rdata 3bd1d8 */
    {
        {  0,  4 },
        {  1,  5 },
        {  2,  6 },
        {  3,  7 },
        {  4,  8 },
        { -1, -1 },
    };

    equip_film = GetPlyrEquipmentFilmType();                            /* 753 */

    MenuCamTopEquipFilmSelDisp(off_x, off_y, alpha);                    /* 757 */

    MenuCamTopFilmSelCsrDisp(off_x, off_y, alpha);                      /* 760 */

    for (i = 0; film_msg_tbl[i][0] != -1; i++) {                        /* 763 */
        if (film_msg_tbl[i][0] == equip_film) {                         /* 767 */
            PrintMsg(MENU_CAM_MSG_TYPE, film_msg_tbl[i][1],
                     off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0); /* 769 */
        }
    }
}

static void MenuCamModeAddFuncSelDisp(int off_x, int off_y, u_char alpha) /* 782 */
{
    static int msg_id[4] =                                  /* rdata 3bd208 */
    {
        11, 12, 13, 14,
    };

    MenuCamTopEquipFilmDisp(off_x, off_y, alpha);                       /* 794 */

    MenuCamTopAddFuncSelCsrDisp(off_x, off_y, alpha);                   /* 797 */

    PrintMsg(MENU_CAM_MSG_TYPE, msg_id[menu_cam_top_ctrl.add_csr],
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 800 */
}

static void MenuCamModeEquipFuncSelDisp(int off_x, int off_y,
                                        u_char alpha)                    /* 811 */
{
    static int msg_id[4] =                                  /* rdata 3bd218 */
    {
        16, 17, 18, 19,
    };

    MenuCamTopEquipFilmDisp(off_x, off_y, alpha);                       /* 825 */

    MenuCamTopEquipFuncSelCsrDisp(off_x, off_y, alpha);                 /* 828 */

    PrintMsg(MENU_CAM_MSG_TYPE, msg_id[menu_cam_top_ctrl.equip_csr],
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 831 */
}

/* Everything all four modes draw underneath their own layer.  Takes off_x /
 * off_y and passes 0, 0 to all fourteen. */
static void MenuCamTopCmnDisp(int off_x, int off_y, u_char alpha)        /* 842 */
{
    MenuCamTopTitleFrameDisp(0, 0, alpha);                              /* 846 */

    MenuCamTopTitleDisp(0, 0, alpha);                                   /* 849 */

    MenuCamTopBaseDisp(0, 0, alpha);                                    /* 852 */

    MenuCamTopCsrBaseDisp(0, 0, alpha);                                 /* 855 */

    MenuCamTopItemDisp(0, 0, alpha);                                    /* 858 */

    MenuCamTopBasicPerformaneItemDisp(0, 0, alpha);                     /* 861 */

    MenuCamTopCsrLineDisp(0, 0, alpha);                                 /* 864 */

    MenuCamTopEquipReinforcedLensDisp(0, 0, alpha);                     /* 867 */

    MenuCamTopLevelGemNumDisp(0, 0, alpha);                             /* 870 */

    MenuCamTopGhostPowerDisp(0, 0, alpha);                              /* 873 */

    MenuCamTopPlyrHaveAddFuncDisp(0, 0, alpha);                         /* 876 */

    MenuCamTopPlyrHaveEquipFuncDisp(0, 0, alpha);                       /* 879 */

    MenuCamTopMsgWindowDisp(0, 0, alpha);                               /* 882 */

    MenuCamTopCaptionDisp(0, 0, alpha);                                 /* 885 */
}

/* --------------------------------------------------------------------------
 *  Drawing -- the page furniture
 * ------------------------------------------------------------------------ */

/* The title bar, stretched 1.1x across.  The scale centre is re-stored per
 * sprite because CopySprDToSpr() resets it. */
static void MenuCamTopTitleFrameDisp(int off_x, int off_y, u_char alpha)  /* 896 */
{
    int       i;
    DISP_SPRT title_ds;

    for (i = 0; i < 2; i++) {                                           /* 903 */
        CopySprDToSpr(&title_ds, &menu_camera_tex[MC_TITLE_FRAME + i]); /* 904 */

        title_ds.x = title_ds.x + off_x;
        title_ds.y = title_ds.y + off_y;                                /* 905 */
        title_ds.scw = 1.0999999f;   title_ds.sch = 1.0f;
        title_ds.csx = title_ds.x;   title_ds.csy = title_ds.y;         /* 906 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 907 */

        DispSprD(&title_ds);                                            /* 908 */
    }                                                                   /* 909 */
}

static void MenuCamTopTitleDisp(int off_x, int off_y, u_char alpha)       /* 920 */
{
    int       i;
    DISP_SPRT title_ds;

    for (i = 0; i < 2; i++) {                                           /* 927 */
        CopySprDToSpr(&title_ds, &menu_camera_tex[MC_TITLE + i]);       /* 928 */

        title_ds.x = title_ds.x + off_x;
        title_ds.y = title_ds.y + off_y;                                /* 929 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 930 */

        DispSprD(&title_ds);                                            /* 931 */
    }                                                                   /* 932 */
}

/* The page ground: one plate, one edge rotated 270 degrees about its own
 * far corner, then five more plain pieces. */
static void MenuCamTopBaseDisp(int off_x, int off_y, u_char alpha)        /* 943 */
{
    int       i;
    DISP_SPRT base_ds;

    CopySprDToSpr(&base_ds, &menu_camera_tex[MC_BASE_PLATE]);           /* 950 */

    base_ds.x = base_ds.x + off_x;
    base_ds.y = base_ds.y + off_y;                                      /* 951 */
    base_ds.alpha = (u_char)(base_ds.alpha * alpha >> 7);               /* 952 */

    DispSprD(&base_ds);                                                 /* 953 */

    CopySprDToSpr(&base_ds, &menu_camera_tex[MC_BASE_EDGE]);            /* 955 */

    /* DISP_SPRT::w is u_int, which is why the ROM emits the halve / convert
     * / double sequence for the cast. */
    base_ds.x = base_ds.x + off_x;
    base_ds.y = base_ds.y + (float)base_ds.w + off_y;                   /* 956 */
    base_ds.rot = 270.0f;
    base_ds.crx = base_ds.x;   base_ds.cry = base_ds.y;                 /* 957 */
    base_ds.alpha = (u_char)(base_ds.alpha * alpha >> 7);               /* 958 */

    DispSprD(&base_ds);                                                 /* 959 */

    for (i = 0; i < 5; i++) {                                           /* 962 */
        CopySprDToSpr(&base_ds, &menu_camera_tex[MC_BASE_REST + i]);    /* 963 */

        base_ds.x = base_ds.x + off_x;
        base_ds.y = base_ds.y + off_y;                                  /* 964 */
        base_ds.alpha = (u_char)(base_ds.alpha * alpha >> 7);           /* 965 */

        DispSprD(&base_ds);                                             /* 966 */
    }                                                                   /* 967 */
}

/* The highlight behind whichever row the cursor is on -- a tinted quad, then
 * the row's own frame pieces.  Two of those rotate and one stretches to the
 * plate height; the rest are drawn as they sit. */
static void MenuCamTopCsrBaseDisp(int off_x, int off_y, u_char alpha)     /* 978 */
{
    int       i;
    int       disp_data;
    DISP_SPRT base_ds;
    DISP_SQAR dsq;

    static int base_tex_tbl[4][2] =                         /* rdata 3bd228 */
    {
        { 11, 16 },
        { 17, 22 },
        { 23, 29 },
        { 30, 35 },
    };

    /* A local aggregate initialiser, not a static -- the ROM copies this
     * 0x60 blob out of .rodata 3bd248 into the stack frame every call.
     * See [[rodata-blob-into-stack-is-a-local-initialiser]]. */
    SQAR_DAT sqar_dat[4] =
    {
        { 119, 229,  33,  79, 0, 0, 0, 0 },
        { 150,  58, 459,  79, 0, 0, 0, 0 },
        { 133,  62, 476, 173, 0, 0, 0, 0 },
        { 150,  58, 459, 271, 0, 0, 0, 0 },
    };                                                                  /* 989 */

    disp_data = 0;                                                      /* 999 */

    if (menu_cam_top_ctrl.csr_yoko != MENU_CAM_TOP_CSR_CAMERA) {
        disp_data = menu_cam_top_ctrl.csr_tate + 1;                     /* 1004 */
    }

    CopySqrDToSqr(&dsq, &sqar_dat[disp_data]);                          /* 1008 */

    for (i = 0; i < 4; i++) {                                           /* 1009 */
        dsq.r[i] = CSR_BASE_R;
        dsq.g[i] = CSR_BASE_G;
        dsq.b[i] = CSR_BASE_B;
    }

    dsq.alpha  = (u_char)(alpha * CSR_BASE_ALPHA_RATE >> 7);            /* 1010 */
    dsq.alphar = 0x48;                                                  /* 1011 */

    DispSqrD(&dsq);                                                     /* 1012 */

    for (i = base_tex_tbl[disp_data][0];
         i <= base_tex_tbl[disp_data][1]; i++) {                        /* 1015 */
        CopySprDToSpr(&base_ds, &menu_camera_tex[i]);                   /* 1016 */

        switch (i) {                                                    /* 1019 */
        case MC_CSR_BASE_ROT_A:
        case MC_CSR_BASE_ROT_B:
            base_ds.x = base_ds.x + off_x;
            base_ds.y = base_ds.y + (float)base_ds.w + off_y;           /* 1022 */
            base_ds.rot = 270.0f;
            base_ds.crx = base_ds.x;   base_ds.cry = base_ds.y;         /* 1023 */
            break;                                                      /* 1024 */

        case MC_CSR_BASE_STRETCH:
            base_ds.x = base_ds.x + off_x;
            base_ds.y = base_ds.y + off_y;                              /* 1026 */
            base_ds.scw = 1.0f;
            base_ds.sch = CSR_BASE_STRETCH_H / (float)base_ds.h;
            base_ds.csx = base_ds.x;   base_ds.csy = base_ds.y;         /* 1027 */
            break;                                                      /* 1028 */

        default:
            base_ds.x = base_ds.x + off_x;
            base_ds.y = base_ds.y + off_y;                              /* 1030 */
            break;
        }

        base_ds.alpha  = (u_char)(base_ds.alpha * alpha >> 7);          /* 1033 */
        base_ds.alphar = 0x48;                                          /* 1034 */

        DispSprD(&base_ds);                                             /* 1035 */
    }                                                                   /* 1036 */
}

static void MenuCamTopItemDisp(int off_x, int off_y, u_char alpha)       /* 1047 */
{
    int       i;
    DISP_SPRT item_ds;

    for (i = 0; i < 6; i++) {                                           /* 1054 */
        CopySprDToSpr(&item_ds, &menu_camera_tex[MC_ITEM + i]);         /* 1055 */

        item_ds.x = item_ds.x + off_x;
        item_ds.y = item_ds.y + off_y;                                  /* 1056 */
        item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);           /* 1057 */

        DispSprD(&item_ds);                                             /* 1058 */
    }                                                                   /* 1059 */
}

/* The three basic-performance rows: their labels, their rails, and one pip
 * per grade step the camera has reached. */
static void MenuCamTopBasicPerformaneItemDisp(int off_x, int off_y,
                                              u_char alpha)              /* 1070 */
{
    int       i;
    DISP_SPRT basic_ds;

    for (i = 0; i < 3; i++) {                                           /* 1077 */
        CopySprDToSpr(&basic_ds, &menu_camera_tex[MC_BASIC_ITEM + i]);  /* 1078 */

        basic_ds.x = basic_ds.x + off_x;
        basic_ds.y = basic_ds.y + off_y;                                /* 1079 */
        basic_ds.alpha = (u_char)(basic_ds.alpha * alpha >> 7);         /* 1080 */

        DispSprD(&basic_ds);                                            /* 1081 */
    }                                                                   /* 1082 */

    for (i = 0; i < 3; i++) {                                           /* 1085 */
        CopySprDToSpr(&basic_ds, &menu_camera_tex[MC_BASIC_RAIL]);      /* 1086 */

        basic_ds.x = basic_ds.x + off_x;
        basic_ds.y = basic_ds.y + (float)(i * BASIC_ROW_STEP) + off_y;  /* 1087 */
        basic_ds.alpha = (u_char)(basic_ds.alpha * alpha >> 7);         /* 1088 */

        DispSprD(&basic_ds);                                            /* 1089 */
    }                                                                   /* 1090 */

    for (i = 0; i < m_plyr_camera.camera_power_up.mRadiusGrade.Get(); i++) { /* 1093 */
        CopySprDToSpr(&basic_ds, &menu_camera_tex[MC_BASIC_PIP]);       /* 1094 */

        basic_ds.x = basic_ds.x + (float)(i * BASIC_PIP_STEP) + off_x;
        basic_ds.y = (float)(off_y + BASIC_RADIUS_Y);                   /* 1095 */
        basic_ds.alpha = (u_char)(basic_ds.alpha * alpha >> 7);         /* 1096 */

        DispSprD(&basic_ds);                                            /* 1097 */
    }                                                                   /* 1098 */

    for (i = 0; i < m_plyr_camera.eq_tray.mSave.mStockGrade.Get(); i++) { /* 1101 */
        CopySprDToSpr(&basic_ds, &menu_camera_tex[MC_BASIC_PIP]);       /* 1102 */

        basic_ds.x = basic_ds.x + (float)(i * BASIC_PIP_STEP) + off_x;
        basic_ds.y = (float)(off_y + BASIC_STOCK_Y);                    /* 1103 */
        basic_ds.alpha = (u_char)(basic_ds.alpha * alpha >> 7);         /* 1104 */

        DispSprD(&basic_ds);                                            /* 1105 */
    }                                                                   /* 1106 */

    for (i = 0; i < m_plyr_camera.camera_power_up.mSensitiveGrade.Get(); i++) { /* 1109 */
        CopySprDToSpr(&basic_ds, &menu_camera_tex[MC_BASIC_PIP]);       /* 1110 */

        basic_ds.x = basic_ds.x + (float)(i * BASIC_PIP_STEP) + off_x;
        basic_ds.y = (float)(off_y + BASIC_SENSITIVE_Y);                /* 1111 */
        basic_ds.alpha = (u_char)(basic_ds.alpha * alpha >> 7);         /* 1112 */

        DispSprD(&basic_ds);                                            /* 1113 */
    }                                                                   /* 1114 */
}

/* The bright rule under the selected row.  Same four-entry lookup off the
 * cursor pair, drawn additively. */
static void MenuCamTopCsrLineDisp(int off_x, int off_y, u_char alpha)    /* 1125 */
{
    int       i;
    int       disp_data;
    DISP_SPRT line_ds;

    static int line_tex_tbl[4][2] =                         /* rdata 3bd2a8 */
    {
        { 36, 45 },
        { 46, 50 },
        { 51, 55 },
        { 56, 60 },
    };

    disp_data = 0;                                                      /* 1139 */

    if (menu_cam_top_ctrl.csr_yoko != MENU_CAM_TOP_CSR_CAMERA) {
        disp_data = menu_cam_top_ctrl.csr_tate + 1;                     /* 1144 */
    }

    for (i = line_tex_tbl[disp_data][0];
         i <= line_tex_tbl[disp_data][1]; i++) {                        /* 1149 */
        CopySprDToSpr(&line_ds, &menu_camera_tex[i]);                   /* 1150 */

        line_ds.x = line_ds.x + off_x;
        line_ds.y = line_ds.y + off_y;                                  /* 1151 */
        line_ds.alpha  = (u_char)(line_ds.alpha * alpha >> 7);          /* 1152 */
        line_ds.alphar = 0x48;                                          /* 1153 */

        DispSprD(&line_ds);                                             /* 1154 */
    }                                                                   /* 1155 */
}

/* --------------------------------------------------------------------------
 *  Drawing -- the lens slots
 * ------------------------------------------------------------------------ */

/* One slot until the camera part that widens the tray is fitted, then three.
 * A filled slot draws its lens icon and its level; an empty one draws the
 * blank plate. */
static void MenuCamTopEquipReinforcedLensDisp(int off_x, int off_y,
                                              u_char alpha)              /* 1166 */
{
    int       i;
    fixed_array<char, EQUIP_SPECIAL_NUM> equip_special;
    DISP_SPRT ds;

    m_plyr_camera.eq_tray.GetSubFuncArray(&equip_special[0]);           /* 1173 */

    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
            EQUIP_FUNC_SPECIAL_SLOT) != 0) {
        for (i = 0; i < EQUIP_SPECIAL_NUM; i++) {                       /* 1180 */
            MenuCamCmnReinforcedLensDisp((float)i * LENS_STEP + LENS_BASE_X,
                                         LENS_Y, alpha, equip_special[i]);

            if (equip_special[i] == 0) {
                CopySprDToSpr(&ds, &menu_camera_tex[MC_LENS_EMPTY + i]); /* 1188 */

                ds.x = ds.x + off_x;
                ds.y = ds.y + off_y;                                    /* 1189 */
                ds.alpha = (u_char)(ds.alpha * alpha >> 7);             /* 1190 */

                DispSprD(&ds);                                          /* 1191 */
            }
            else {
                MenuCamTopReinforcedLensLvNumDisp(
                    off_x + LENS_LV_BASE_X + i * LENS_LV_STEP,
                    off_y + LENS_LV_Y, alpha,
                    m_plyr_camera.eq_tray.mSave.mSubFuncLv[
                        equip_special[i]].Get());
            }
        }                                                               /* 1193 */
    }
    else {
        MenuCamCmnReinforcedLensDisp(LENS_ONE_X, LENS_Y, alpha,
                                     equip_special[0]);

        if (equip_special[0] == 0) {
            CopySprDToSpr(&ds, &menu_camera_tex[MC_LENS_EMPTY_ONE]);    /* 1203 */

            ds.x = ds.x + off_x;
            ds.y = ds.y + off_y;                                        /* 1204 */
            ds.alpha = (u_char)(ds.alpha * alpha >> 7);                 /* 1205 */

            DispSprD(&ds);                                              /* 1206 */
        }
        else {
            MenuCamTopReinforcedLensLvNumDisp(
                off_x + LENS_LV_ONE_X, off_y + LENS_LV_Y, alpha,
                m_plyr_camera.eq_tray.mSave.mSubFuncLv[
                    equip_special[0]].Get());
        }
    }
}

/* "Lv" plus one digit.  The digit's x comes off the label's own width, out
 * of the sheet rather than a layout constant. */
static void MenuCamTopReinforcedLensLvNumDisp(int x, int y, u_char alpha,
                                              int lv)                    /* 1220 */
{
    DISP_SPRT lv_ds;

    CopySprDToSpr(&lv_ds, &menu_camera_tex[MC_LV_LABEL]);               /* 1225 */

    lv_ds.x = (float)x;   lv_ds.y = (float)y;                           /* 1226 */
    lv_ds.alpha = (u_char)(lv_ds.alpha * alpha >> 7);                   /* 1227 */

    DispSprD(&lv_ds);                                                   /* 1228 */

    CopySprDToSpr(&lv_ds, &menu_camera_tex[MC_LV_DIGIT + lv]);          /* 1231 */

    lv_ds.x = (float)(menu_camera_tex[MC_LV_LABEL].w + x);
    lv_ds.y = (float)y;                                                 /* 1232 */
    lv_ds.alpha = (u_char)(lv_ds.alpha * alpha >> 7);                   /* 1233 */

    DispSprD(&lv_ds);                                                   /* 1234 */
}

/* Both readouts ignore off_x / off_y and place themselves. */
static void MenuCamTopLevelGemNumDisp(int off_x, int off_y,
                                      u_char alpha)                      /* 1245 */
{
    MenuCamNumberDisp(GetPlyrLevelGemNum(), 2, GEM_NUM_X, GEM_NUM_Y,
                      alpha, 0, NUM_TYPE_SMALL, 1);                     /* 1249 */
}

static void MenuCamTopGhostPowerDisp(int off_x, int off_y,
                                     u_char alpha)                       /* 1260 */
{
    MenuCamNumberDisp(GetPlayData_Score(), 6, SCORE_NUM_X, SCORE_NUM_Y,
                      alpha, 0, NUM_TYPE_SMALL, 0);                     /* 1264 */
}

/* --------------------------------------------------------------------------
 *  Drawing -- the equipped film
 *
 *  Two copies of one routine: this one for the page, and
 *  MenuCamTopEquipFilmSelDisp() below for the film selector, differing only
 *  in which sprites and which digit face they use.
 * ------------------------------------------------------------------------ */

static void MenuCamTopEquipFilmDisp(int off_x, int off_y, u_char alpha)  /* 1275 */
{
    int       i;
    int       equip_film;
    int       equip_film_num;
    DISP_SPRT film_ds;

    equip_film     = GetPlyrEquipmentFilmType();                        /* 1283 */
    equip_film_num = GetPlyrItemHaveNum(equip_film);                    /* 1284 */

    for (i = 0; film_name_tbl[i][0] != -1; i++) {                       /* 1288 */
        if (film_name_tbl[i][0] == equip_film) {                        /* 1293 */
            break;                                                      /* 1294 */
        }
    }                                                                   /* 1296 */

    if (equip_film == FILM_TYPE_SPECIAL) {                              /* 1301 */
        /* The unnumbered film has a word plate instead of a number. */
        CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_SPECIAL]);     /* 1302 */

        film_ds.x = film_ds.x + off_x;
        film_ds.y = film_ds.y + off_y;                                  /* 1303 */
        film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);           /* 1304 */

        DispSprD(&film_ds);                                             /* 1305 */
    }
    else {
        MenuCamNumberDisp(film_name_tbl[i][1], 2, FILM_NAME_X, FILM_NAME_Y,
                          alpha, 0, NUM_TYPE_DARK, 1);                  /* 1308 */
    }

    CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_PLATE]);           /* 1311 */

    film_ds.x = film_ds.x + off_x;
    film_ds.y = film_ds.y + off_y;                                      /* 1312 */
    film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);               /* 1313 */

    DispSprD(&film_ds);                                                 /* 1314 */

    CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_PLATE + 1]);       /* 1316 */

    film_ds.x = film_ds.x + off_x;
    film_ds.y = film_ds.y + off_y;                                      /* 1317 */
    film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);               /* 1318 */

    DispSprD(&film_ds);                                                 /* 1319 */

    if (equip_film == FILM_TYPE_07) {                                   /* 1322 */
        /* Type-07 is unlimited, so its count is two dashes -- one sprite
         * drawn twice, the second offset by its own width. */
        CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_DASH]);        /* 1323 */

        film_ds.x = film_ds.x + off_x;
        film_ds.y = film_ds.y + off_y;                                  /* 1324 */
        film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);           /* 1325 */

        DispSprD(&film_ds);                                             /* 1326 */

        CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_DASH]);        /* 1328 */

        film_ds.x = film_ds.x + (float)film_ds.w + off_x;
        film_ds.y = film_ds.y + off_y;                                  /* 1329 */
        film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);           /* 1330 */

        DispSprD(&film_ds);                                             /* 1331 */
    }
    else {
        MenuCamNumberDisp(equip_film_num, 2, FILM_NUM_X, FILM_NUM_Y,
                          alpha, 0, NUM_TYPE_DARK, 1);                  /* 1335 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing -- what the camera has
 * ------------------------------------------------------------------------ */

/* Four addition-function slots.  An owned one draws its icon, an empty one
 * the divider line -- with the extra piece, because on this row the empty
 * slot needs both halves of the rule. */
static void MenuCamTopPlyrHaveAddFuncDisp(int off_x, int off_y,
                                          u_char alpha)                  /* 1347 */
{
    int add_label;

    for (add_label = 0; add_label < ADD_FUNC_NUM; add_label++) {        /* 1352 */
        if (m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(add_label) != 0) {
            MenuCamCmnAdditionalFunctionDisp(
                (float)add_label * ADD_FUNC_STEP + ADD_FUNC_BASE_X,
                ADD_FUNC_ICON_Y, alpha, add_label);                     /* 1355 */
        }
        else {
            MenuCamTopAddFuncLineDisp(
                (float)add_label * ADD_FUNC_STEP + ADD_FUNC_BASE_X,
                ADD_FUNC_LINE_Y, alpha, 0);                             /* 1359 */
        }
    }                                                                   /* 1361 */
}

/* The empty-slot rule, rotated 270 degrees about its own far end.  `flg`
 * adds the second piece on top of the first. */
static void MenuCamTopAddFuncLineDisp(float x, float y, u_char alpha,
                                      u_char flg)                        /* 1373 */
{
    DISP_SPRT add_ds;

    if (flg != 0) {                                                     /* 1379 */
        CopySprDToSpr(&add_ds, &menu_camera_tex[MC_ADD_FUNC_SLOT + 1]); /* 1380 */

        add_ds.x = x;   add_ds.y = y + (float)add_ds.w;                 /* 1381 */
        add_ds.rot = 270.0f;
        add_ds.crx = add_ds.x;   add_ds.cry = add_ds.y;                 /* 1382 */
        add_ds.alpha = (u_char)(add_ds.alpha * alpha >> 7);             /* 1383 */

        DispSprD(&add_ds);                                              /* 1384 */
    }

    CopySprDToSpr(&add_ds, &menu_camera_tex[MC_ADD_FUNC_SLOT]);         /* 1387 */

    add_ds.x = x;   add_ds.y = y + (float)add_ds.w;                     /* 1388 */
    add_ds.rot = 270.0f;
    add_ds.crx = add_ds.x;   add_ds.cry = add_ds.y;                     /* 1389 */
    add_ds.alpha = (u_char)(add_ds.alpha * alpha >> 7);                 /* 1390 */

    DispSprD(&add_ds);                                                  /* 1391 */
}

/* Four camera-part slots.  An owned part draws its icon, twice over: the
 * fitted and unfitted calls are two separate statements in the ROM, each
 * with its own literal `flg`, cross-jumped onto one `jal`.  A slot the
 * player does not own draws the divider line instead. */
static void MenuCamTopPlyrHaveEquipFuncDisp(int off_x, int off_y,
                                            u_char alpha)                /* 1402 */
{
    int parts_label;

    for (parts_label = 0; parts_label < EQUIP_FUNC_NUM; parts_label++) { /* 1407 */
        if (m_plyr_camera.camera_power_up.mCamPartsFlg.IsUp(parts_label) != 0) {
            if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                    parts_label) != 0) {
                MenuCamCmnEquipFunctionDisp(
                    (float)parts_label * EQUIP_FUNC_STEP + EQUIP_FUNC_BASE_X,
                    EQUIP_FUNC_ICON_Y, alpha, parts_label, 1);          /* 1413 */
            }
            else {
                MenuCamCmnEquipFunctionDisp(
                    (float)parts_label * EQUIP_FUNC_STEP + EQUIP_FUNC_BASE_X,
                    EQUIP_FUNC_ICON_Y, alpha, parts_label, 0);          /* 1416 */
            }
        }
        else {
            MenuCamTopEquipFuncLineDisp(
                (float)parts_label * EQUIP_FUNC_STEP + EQUIP_FUNC_BASE_X,
                EQUIP_FUNC_LINE_Y, alpha);                              /* 1420 */
        }
    }                                                                   /* 1422 */
}

static void MenuCamTopEquipFuncLineDisp(float x, float y, u_char alpha)  /* 1433 */
{
    DISP_SPRT add_ds;

    CopySprDToSpr(&add_ds, &menu_camera_tex[MC_EQUIP_FUNC_SLOT]);       /* 1438 */

    add_ds.x = x;   add_ds.y = y + (float)add_ds.w;                     /* 1439 */
    add_ds.rot = 270.0f;
    add_ds.crx = add_ds.x;   add_ds.cry = add_ds.y;                     /* 1440 */
    add_ds.alpha = (u_char)(add_ds.alpha * alpha >> 7);                 /* 1441 */

    DispSprD(&add_ds);                                                  /* 1442 */
}

/* --------------------------------------------------------------------------
 *  Drawing -- the caption window
 * ------------------------------------------------------------------------ */

static void MenuCamTopMsgWindowDisp(int off_x, int off_y, u_char alpha)  /* 1453 */
{
    DrawCmnWindow(0, (float)(off_x + 0x18), (float)(off_y + 0x160),
                  592.0f, 100.0f, alpha, 102);                          /* 1457 */
}

static void MenuCamTopCaptionDisp(int off_x, int off_y, u_char alpha)    /* 1468 */
{
    DrawCmnCapGroup_W(0, 0, alpha, 0);                                  /* 1472 */
}

/* --------------------------------------------------------------------------
 *  Drawing -- the film selector
 * ------------------------------------------------------------------------ */

/* MenuCamTopEquipFilmDisp() with the selector's own art and the light digit
 * face.  Kept as two functions, as the ROM has it. */
static void MenuCamTopEquipFilmSelDisp(int off_x, int off_y,
                                       u_char alpha)                     /* 1499 */
{
    int       i;
    int       equip_film;
    int       equip_film_num;
    DISP_SPRT film_ds;

    equip_film     = GetPlyrEquipmentFilmType();                        /* 1507 */
    equip_film_num = GetPlyrItemHaveNum(equip_film);                    /* 1508 */

    for (i = 0; film_name_tbl[i][0] != -1; i++) {                       /* 1512 */
        if (film_name_tbl[i][0] == equip_film) {                        /* 1517 */
            break;                                                      /* 1518 */
        }
    }                                                                   /* 1520 */

    if (equip_film == FILM_TYPE_SPECIAL) {                              /* 1525 */
        CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_SPECIAL_SEL]); /* 1526 */

        film_ds.x = film_ds.x + off_x;
        film_ds.y = film_ds.y + off_y;                                  /* 1527 */
        film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);           /* 1528 */

        DispSprD(&film_ds);                                             /* 1529 */
    }
    else {
        MenuCamNumberDisp(film_name_tbl[i][1], 2, FILM_NAME_X, FILM_NAME_Y,
                          alpha, 0, NUM_TYPE_LIGHT, 1);                 /* 1532 */
    }

    CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_SEL_PLATE]);       /* 1535 */

    film_ds.x = film_ds.x + off_x;
    film_ds.y = film_ds.y + off_y;                                      /* 1536 */
    film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);               /* 1537 */

    DispSprD(&film_ds);                                                 /* 1538 */

    CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_SEL_PLATE + 1]);   /* 1540 */

    film_ds.x = film_ds.x + off_x;
    film_ds.y = film_ds.y + off_y;                                      /* 1541 */
    film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);               /* 1542 */

    DispSprD(&film_ds);                                                 /* 1543 */

    if (equip_film == FILM_TYPE_07) {                                   /* 1546 */
        CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_DASH_SEL]);    /* 1547 */

        film_ds.x = film_ds.x + off_x;
        film_ds.y = film_ds.y + off_y;                                  /* 1548 */
        film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);           /* 1549 */

        DispSprD(&film_ds);                                             /* 1550 */

        CopySprDToSpr(&film_ds, &menu_camera_tex[MC_FILM_DASH_SEL]);    /* 1552 */

        film_ds.x = film_ds.x + (float)film_ds.w + off_x;
        film_ds.y = film_ds.y + off_y;                                  /* 1553 */
        film_ds.alpha = (u_char)(film_ds.alpha * alpha >> 7);           /* 1554 */

        DispSprD(&film_ds);                                             /* 1555 */
    }
    else {
        MenuCamNumberDisp(equip_film_num, 2, FILM_NUM_X, FILM_NUM_Y,
                          alpha, 0, NUM_TYPE_LIGHT, 1);                 /* 1559 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing -- the three sub-selector cursors
 *
 *  All three take their tint from one Zero2Anim2D_CsrAnimCtrl() pulse driven
 *  off menu_cam_top_disp.csr_anim_timer, which MenuCamTopPadDecision()
 *  zeroes as the selector opens.
 * ------------------------------------------------------------------------ */

static void MenuCamTopFilmSelCsrDisp(int off_x, int off_y, u_char alpha) /* 1571 */
{
    int       i;
    u_char    rgb;
    DISP_SPRT csr_ds;

    Zero2Anim2D_CsrAnimCtrl(&menu_cam_top_disp.csr_anim_timer, &rgb);   /* 1578 */

    for (i = 0; i < 2; i++) {                                           /* 1581 */
        CopySprDToSpr(&csr_ds, &menu_camera_tex[MC_FILM_SEL_CSR + i]);  /* 1582 */

        csr_ds.x = csr_ds.x + off_x;
        csr_ds.y = csr_ds.y + off_y;                                    /* 1583 */
        csr_ds.alpha = (u_char)(csr_ds.alpha * alpha >> 7);             /* 1584 */
        csr_ds.r = rgb;   csr_ds.g = rgb;   csr_ds.b = rgb;             /* 1585 */

        DispSprD(&csr_ds);                                              /* 1586 */
    }                                                                   /* 1587 */
}

/* Two passes: the plain box first, then the two tinted pieces on top.  Both
 * slide with add_csr at the row's own pitch. */
static void MenuCamTopAddFuncSelCsrDisp(int off_x, int off_y,
                                        u_char alpha)                    /* 1602 */
{
    int       i;
    u_char    rgb;
    DISP_SPRT csr_ds;

    Zero2Anim2D_CsrAnimCtrl(&menu_cam_top_disp.csr_anim_timer, &rgb);   /* 1609 */

    for (i = 0; i < 4; i++) {                                           /* 1612 */
        CopySprDToSpr(&csr_ds, &menu_camera_tex[MC_ADD_FUNC_SEL_BOX + i]); /* 1613 */

        csr_ds.x = csr_ds.x
                 + (float)menu_cam_top_ctrl.add_csr * ADD_FUNC_STEP + off_x;
        csr_ds.y = csr_ds.y + off_y;                                    /* 1614 */
        csr_ds.alpha = (u_char)(csr_ds.alpha * alpha >> 7);             /* 1615 */

        DispSprD(&csr_ds);                                              /* 1616 */
    }                                                                   /* 1617 */

    for (i = 0; i < 2; i++) {                                           /* 1619 */
        CopySprDToSpr(&csr_ds, &menu_camera_tex[MC_ADD_FUNC_SEL_CSR + i]); /* 1620 */

        csr_ds.x = csr_ds.x
                 + (float)menu_cam_top_ctrl.add_csr * ADD_FUNC_STEP + off_x;
        csr_ds.y = csr_ds.y + off_y;                                    /* 1621 */
        csr_ds.alpha = (u_char)(csr_ds.alpha * alpha >> 7);             /* 1622 */
        csr_ds.r = rgb;   csr_ds.g = rgb;   csr_ds.b = rgb;             /* 1623 */

        DispSprD(&csr_ds);                                              /* 1624 */
    }                                                                   /* 1625 */
}

static void MenuCamTopEquipFuncSelCsrDisp(int off_x, int off_y,
                                          u_char alpha)                  /* 1640 */
{
    int       i;
    u_char    rgb;
    DISP_SPRT csr_ds;

    Zero2Anim2D_CsrAnimCtrl(&menu_cam_top_disp.csr_anim_timer, &rgb);   /* 1647 */

    for (i = 0; i < 4; i++) {                                           /* 1650 */
        CopySprDToSpr(&csr_ds, &menu_camera_tex[MC_EQUIP_FUNC_BOX + i]); /* 1651 */

        csr_ds.x = csr_ds.x
                 + (float)menu_cam_top_ctrl.equip_csr * EQUIP_FUNC_STEP + off_x;
        csr_ds.y = csr_ds.y + off_y;                                    /* 1652 */
        csr_ds.alpha = (u_char)(csr_ds.alpha * alpha >> 7);             /* 1653 */

        DispSprD(&csr_ds);                                              /* 1654 */
    }                                                                   /* 1655 */

    for (i = 0; i < 2; i++) {                                           /* 1657 */
        CopySprDToSpr(&csr_ds, &menu_camera_tex[MC_EQUIP_FUNC_CSR + i]); /* 1658 */

        csr_ds.x = csr_ds.x
                 + (float)menu_cam_top_ctrl.equip_csr * EQUIP_FUNC_STEP + off_x;
        csr_ds.y = csr_ds.y + off_y;                                    /* 1659 */
        csr_ds.alpha = (u_char)(csr_ds.alpha * alpha >> 7);             /* 1660 */
        csr_ds.r = rgb;   csr_ds.g = rgb;   csr_ds.b = rgb;             /* 1661 */

        DispSprD(&csr_ds);                                              /* 1662 */
    }                                                                   /* 1663 */
}
