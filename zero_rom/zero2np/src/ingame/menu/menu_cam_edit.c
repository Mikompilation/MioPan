// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_cam_edit.c
//
// The camera menu's upgrade editor (menu_cam_edit.o).  Eighty-four functions
// -- the three exports and eighty-one statics -- two .rodata tables at file
// scope and nine more inside the functions that use them, two local SQAR_DAT
// initialisers, two twelve-slot dispatch tables in .data, and four work
// blocks split between .bss and .sbss.  At 0x62d0 it is the largest
// translation unit in the menu folder.
//
// It is the screen behind the camera portrait on the top page: the camera's
// three basic performances and the lenses fitted to it down the left, the
// player's level gems and spirit score on the right, and three things the
// player can do to them -- fit a lens into a sub-function slot, spend a
// level gem, and spend spirit points to raise a grade.
//
// Facts worth knowing before touching it:
//
//  * There is one `mode` and it drives everything.  It indexes both
//    menu_cam_edit_pad[12] and menu_cam_edit_disp_func[12] in step, and the
//    twelve slots are not twelve screens: three of them are the real pages
//    (the top menu, the lens selector, the two item selectors) and the rest
//    are confirm and error windows drawn over whichever page opened them.
//    Six of the twelve share MenuCamEditErrorPad(), whose whole body is a
//    switch saying which mode each error returns to.
//
//  * There are two selectors and they share one list.  `edit_sel_csr` walks
//    a combined list -- 0..2 are the three basic performances and 3.. the
//    lens rows below them -- so MenuCamEditItemSelPad() has two completely
//    separate cursor ladders in it, one per half, and every drawing function
//    that places a row tests `edit_sel_csr < 3` to decide which half it is
//    in.  MENU_REF_CTRL only tracks the lens half.
//
//  * The lens list is compacted.  MenuCamEditSetDispLensData() walks bits
//    1..9 of CCameraPowerUp::mTemperedRenzFlg and packs the ones that are up
//    into disp_lens_data[], so `data_pos` indexes owned lenses rather than
//    CAMERA_SUB_FUNC_ENUM values, and every use of it goes through
//    `disp_lens_data[data_pos].lens_label` to get back to the enum.
//
//  * `sp_equip_init_flg` and `edit_init_flg` are one-shot latches that each
//    say "the OTHER selector was open last, so reset my scroll".  A trip
//    into the gem selector and back therefore leaves the lens list where it
//    was, and a trip through the lens equip screen resets it.
//
//  * The three sub-function slots collapse to one without camera part 1.
//    mCamPartsSetFlg bit 1 is what opens slots 1 and 2, so it is tested in
//    nine places: it decides whether CROSS on the lens row opens the slot
//    picker (mode 2) or goes straight to the lens list (mode 3), how many
//    positions the slot cursor has, which window art is drawn, and which
//    tray entry a chosen lens lands in.
//
//  * `gem_anim_flg` stops the whole page.  MenuCamEditMain() skips the pad
//    dispatch entirely while it is up, and the only things that lower it are
//    MenuCamEditBaseGemDisp() and MenuCamEditLensGemDisp() -- so the award
//    animation is driven by the *drawing* side and the page is frozen until
//    it finishes.  Both gem controls are seeded by memset(-1), which is why
//    `data_pos == -1` is the idle test.
//
//  * MenuCamEditBaseDisp()'s base_tex_tbl and MenuCamEditLensNameDisp()'s
//    lens_name_tbl are FUNCTION-LOCAL statics (they carry GCC's `__tmp_N`
//    construction guard), while base_msg_tbl and lens_msg_tbl are file
//    scope.  globals.txt lists only the latter two; functions.txt is what
//    places the other pair.  See [[unlisted-data-is-a-function-local-static]].
//
//  * MENU_REF_CTRL's four movement helpers in menu_cmn.o are never called.
//    Every one of the three cursor ladders here writes the disp_start_pos /
//    data_pos arithmetic out by hand, and a jal scan finds no call to
//    MenuRefMovePadLup() and friends anywhere in the object.  Only
//    MenuRefCtrlInit() is used.
//
// Line numbers: measured, not guessed.  Every function's *opening* line is
// the `$LM` symbols.txt interleaves immediately before its PROC/STATICPROC
// record.  Three GCC habits shape the annotations:
//
//  * A statement whose only work is an inlined accessor leaves NO note of
//    its own.  BIT_FLAGS::IsUp() reports variable.h 852-858,
//    CVariable::Get() 167, CVariable::GetMax() 29, CVariable::Increment()
//    52-61 and fixed_array::operator[] 124/125.  Those are the gaps in the
//    sequences below.
//
//  * `x--` on a field GCC still has in a register compiles to a *store of
//    the old value*, not a subtract.  The three cursor ladders all use the
//    `pos++; if (pos > limit) { pos--; }` shape and every one of them shows
//    up as two stores of a saved register.
//
//  * Identical call tails are cross-jumped, so only one copy carries line
//    notes.  MenuCamEditSelMenuPadDecision()'s two "reset the other
//    selector" blocks are one block in the object; the surviving copy is
//    case 2's (572-582) and case 1's own lines (556-566) were eliminated.
//    Same for the paired SystemBankPlay() calls that differ only in the cue
//    number.  See [[gcc-cross-jumps-identical-call-tails]].
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_cam_edit.o.
// All 3 ZERO2.MAP exports plus the 81 statics, verified 3/3 against the map
// and 84/84 against functions.txt.  .text is accounted for byte-for-byte --
// 0x1e7340..0x1ed610 is 0x622c of code in 90 bodies plus 41 four-byte
// alignment fills, so there is no unlisted body anywhere in the object.
// MENU_CAM_EDIT_CTRL (0x18), MENU_CAM_EDIT_DISP (0x6), DISP_LENS_DATA (0x4)
// and GEM_ANIM_CTRL (0x4) are confirmed member-by-member by an offsetof
// harness, and eleven of the thirteen tables are byte-identical to the ROM,
// diffed out of the compiled .obj.
//
// The two that are not are MenuCamEditSelFrameBgDisp()'s `sel_bg` and
// MenuCamEditFrameBlackBgDisp()'s `non_sel_bg`, and the difference is host
// codegen rather than the values: both are LOCAL SQAR_DAT initialisers,
// which EE GCC kept as one 24-byte .rodata blob and MinGW splits into a
// partial blob plus immediates for the zero members.  Every field matches --
// the w/h pair and the packed colour word both appear verbatim in the .obj.
// Same shape as mis_sel_disp.o's `iMovY[2]`; see
// [[rodata-blob-into-stack-is-a-local-initialiser]].

#include "menu_cam_edit.h"

#include "anim_2d.h"                            /* ALPHA_ANIM_TBL / SCL_ANIM_TBL */
#include "menu_cam_main.h"                      /* MenuCamGoToTopReq / the widgets */
#include "menu_cmn.h"                           /* MENU_REF_CTRL              */
#include "play_data.h"                          /* GetPlayData_Score          */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_InOutAnimCtrl  */
#include "tim_dat/cam_level_up_point.h"         /* the two cost ladders       */
#include "tim_dat/menu_camera_dat.h"            /* menu_camera_tex[]          */

#include "../item/prg/level_gem.h"              /* GetPlyrLevelGemNum / LostLevelGem */
#include "../photo/m_plyr_camera.h"             /* m_plyr_camera              */
#include "../photo/n_equip_tray.h"              /* Get/SetSubFuncArray        */
#include "../../common/utility2.h"              /* PRINT_ASSERT               */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow / CapGroup_W */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / PrintNumber_N   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array           */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay             */
#include "../../system/pad/pad.h"               /* pad / paddat               */

#include <string.h>                             /* memset                     */

/* --------------------------------------------------------------------------
 *  Constants
 *
 *  All of these names are the port's; the ROM writes the literals.
 * ------------------------------------------------------------------------ */

/* MENU_CAM_EDIT_CTRL::step.  There is no load step -- menu_cam_main.o has
 * both paks resident before this page is ever dispatched to. */
#define MENU_CAM_EDIT_STEP_INIT     0
#define MENU_CAM_EDIT_STEP_MAIN     2
#define MENU_CAM_EDIT_STEP_OUT      3

/* MENU_CAM_EDIT_DISP::anim_step.  Only OUT and END are written by name here;
 * the rest of the ladder is Zero2Anim2D_InOutAnimCtrl()'s business. */
#define MENU_CAM_EDIT_ANIM_OUT      3
#define MENU_CAM_EDIT_ANIM_END      4

/* The in and out durations handed to Zero2Anim2D_InOutAnimCtrl(). */
#define MENU_CAM_EDIT_FADE_IN_TIME  10
#define MENU_CAM_EDIT_FADE_OUT_TIME 5

/* MENU_CAM_EDIT_CTRL::mode -- the index into both dispatch tables.  Three of
 * these are pages and the other nine are windows drawn over one. */
#define MENU_CAM_EDIT_MODE_SEL_MENU             0   /* the three-row menu     */
#define MENU_CAM_EDIT_MODE_LENS_NOTHING_ERR     1   /* "you have no lenses"   */
#define MENU_CAM_EDIT_MODE_SEL_LENS_EQUIP_POS   2   /* pick a tray slot       */
#define MENU_CAM_EDIT_MODE_EQUIP_LENS_SEL       3   /* pick a lens for it     */
#define MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS      4   /* pick what to gem       */
#define MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS     5   /* pick what to raise     */
#define MENU_CAM_EDIT_MODE_GEM_ADD_CONF         6
#define MENU_CAM_EDIT_MODE_GEM_ADD_ERR          7   /* already at three gems  */
#define MENU_CAM_EDIT_MODE_GEM_NOTHING_ERR      8   /* no level gem to spend  */
#define MENU_CAM_EDIT_MODE_POWER_UP_CONF        9
#define MENU_CAM_EDIT_MODE_POWER_UP_LV_MAX_ERR  10  /* not enough gems        */
#define MENU_CAM_EDIT_MODE_POWER_UP_NOTHING_ERR 11  /* not enough points      */
#define MENU_CAM_EDIT_MODE_NUM                  12

/* MENU_CAM_EDIT_CTRL::menu_csr -- the three rows of the page's own menu. */
#define MENU_CAM_EDIT_ROW_EQUIP_LENS    0
#define MENU_CAM_EDIT_ROW_GEM_ADD       1
#define MENU_CAM_EDIT_ROW_POWER_UP      2
#define MENU_CAM_EDIT_ROW_NUM           3

/* The three basic performances, in GetMenuCamBasicLv()'s order.  They are
 * also edit_sel_csr 0..2 and cam_base_status_point[]'s rows. */
#define MENU_CAM_BASIC_RADIUS       0
#define MENU_CAM_BASIC_STOCK        1
#define MENU_CAM_BASIC_SENSITIVE    2
#define MENU_CAM_BASIC_NUM          3

/* Every grade and every gem count runs 0..3. */
#define MENU_CAM_LV_MAX             3

/* How many sub-functions there are (CAMERA_SUB_FUNC_ENUM 0..9, 0 being
 * NONE), and how many tray slots the camera has. */
#define REINFORCED_LENS_NUM         10
#define EQUIP_SPECIAL_NUM           3

/* The camera part that opens tray slots 1 and 2. */
#define EQUIP_FUNC_SPECIAL_SLOT     1

/* How many rows of either list the window shows at once. */
#define MENU_CAM_EDIT_LIST_DISP_NUM 3

/* SystemBankPlay() cue numbers.  0..3 are the menus' shared four; 5 and 7
 * are this page's own "gem spent" and "upgrade bought". */
#define SE_CURSOR       0
#define SE_CANCEL       1
#define SE_ERROR        2
#define SE_DECIDE       3
#define SE_GEM_ADD      5
#define SE_POWER_UP     7

/* pad[0].rpt bits in the remapped layout, plus the analogue equivalents. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_RIGHT           0x2000
#define PAD_RPT_DOWN            0x4000
#define PAD_RPT_LEFT            0x8000
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3

/* The message bank both camera pages draw their caption from, and where the
 * caption sits inside the message window. */
#define MENU_CAM_MSG_TYPE       0x31
#define MSG_OFF_X               0x30
#define MSG_OFF_Y               0x176

/* Fixed caption ids.  The two computed ones -- the basic-performance and
 * lens descriptions -- are `base + row * 3 + lv`, laid out three per row. */
#define MSG_LENS_NOTHING        0x55
#define MSG_SEL_LENS_EQUIP_POS  0x56
#define MSG_POWER_UP_LV_MAX     0x57
#define MSG_GEM_ADD_CONF_BASE   0x28
#define MSG_GEM_ADD_CONF_LENS   0x29
#define MSG_POWER_UP_CONF_BASE  0x2a
#define MSG_POWER_UP_CONF_LENS  0x2b
#define MSG_GEM_NOTHING         0x2c
#define MSG_POWER_UP_NOTHING    0x2d
#define MSG_GEM_ADD_ERR         0x2e
#define MSG_LV_MAX              0x2f
#define MSG_BASIC_DESC_BASE     0x31    /* + edit_sel_csr * 3 + lv          */
#define MSG_LENS_DESC_BASE      0x37    /* + lens_label   * 3 + lv          */

/* menu_camera_tex[] indices.  Everything this page draws comes out of the
 * two camera paks; the names are the port's. */
#define MCE_TEX_BG                  0x9d    /* the whole-page backdrop        */
#define MCE_TEX_WIN_FIRST           0x9e    /* 0x9e..0xa6, the window panels  */
#define MCE_TEX_WIN_LAST            0xa6
#define MCE_TEX_ROW_BG_FIRST        0xa7    /* 0xa7..0xab, one list row's bed */
#define MCE_TEX_ROW_BG_LAST         0xab
#define MCE_TEX_THREE_SLOT          0xac    /* one wide plate, part 1 fitted  */
#define MCE_TEX_ONE_SLOT_FIRST      0xad    /* 0xad..0xb0, the small one      */
#define MCE_TEX_ONE_SLOT_LAST       0xb0
#define MCE_TEX_WIN2_FIRST          0xb1    /* 0xb1..0xc0, the rest of it     */
#define MCE_TEX_WIN2_LAST           0xc0
#define MCE_TEX_BASE_FRAME_FIRST    0xc1    /* 0xc1..0xcf, three rows of five */
#define MCE_TEX_BASE_FRAME_LAST     0xcf
#define MCE_TEX_LENS_FRAME_FIRST    0xd0    /* 0xd0..0xde, ditto              */
#define MCE_TEX_TITLE               0xdf
#define MCE_TEX_MENU_ITEM_OFF       0xe0    /* 0xe0..0xe2 unselected          */
#define MCE_TEX_MENU_ITEM_ON        0xe3    /* 0xe3..0xe5 selected            */
#define MCE_TEX_TITLE_FRAME         0xe6    /* 0xe6..0xe7                     */
#define MCE_TEX_NONSEL_FRAME_L      0xe8
#define MCE_TEX_NONSEL_FRAME_R      0xe9
#define MCE_TEX_SEL_FRAME_L         0xea
#define MCE_TEX_SEL_FRAME_R         0xeb
#define MCE_TEX_ASSIST              0xee    /* the "reinforced lens" label    */
#define MCE_TEX_EQUIP               0xef    /* the "equipped" label           */
#define MCE_TEX_LV_NUM              0xf0    /* + lv, the small level digits   */
#define MCE_TEX_STATUS_FIRST        0xf4    /* 0xf4..0xf8, the two readouts   */
#define MCE_TEX_STATUS_LAST         0xf8
#define MCE_TEX_BASE_ITEM_FIRST     0xf9    /* 0xf9..0xfb, the three names    */
#define MCE_TEX_BASE_ITEM_LAST      0xfb
#define MCE_TEX_SEL_CSR_FIRST       0x10b   /* 0x10b..0x10f, the row cursor   */
#define MCE_TEX_SEL_CSR_LAST        0x10f
#define MCE_TEX_SCROLL_BAR          0x105   /* the rail                       */
#define MCE_TEX_SCROLL_ARROW        0x106   /* 0x106..0x107, the two arrows   */
#define MCE_TEX_SCROLL_KNOB_TOP     0x108
#define MCE_TEX_SCROLL_KNOB_MID     0x109
#define MCE_TEX_SCROLL_KNOB_BTM     0x10a
#define MCE_TEX_GEM_EMPTY           0x110
#define MCE_TEX_GEM_FULL            0x111
#define MCE_TEX_LV_PLATE            0x112   /* the word "Lv"                  */
#define MCE_TEX_LV_DIGIT            0x113   /* + lv                           */
#define MCE_TEX_SLOT_FRAME_FIRST    0x118   /* 0x118..0x11b                   */
#define MCE_TEX_SLOT_FRAME_LAST     0x11b
#define MCE_TEX_SLOT_CSR_L          0x11c
#define MCE_TEX_SLOT_CSR_R          0x11d
#define MCE_TEX_GEM_BLUE_FLARE      0x11e
#define MCE_TEX_GEM_YELLOW_FLARE    0x11f
#define MCE_TEX_NEXT_PTS_A          0x120   /* the "next" plate, two colours  */
#define MCE_TEX_NEXT_PTS_B          0x121

/* PrintNumber_N() colour labels.  0x11 is the highlighted row's, 0x12 every
 * other row's -- MenuCamEditNextPointDisp() picks its "next" plate off the
 * same value. */
#define MCE_COL_SELECTED    0x11
#define MCE_COL_NORMAL      0x12

/* Layout.  The page is two stacks of three rows, 32 pixels apart: the basic
 * performances at 133 and the lenses at 255. */
#define ROW_PITCH               32.0f
#define BASE_ROW_TOP            133.0f
#define LENS_ROW_TOP            255.0f
#define BASE_GEM_ROW_TOP        139.0f
#define LENS_GEM_ROW_TOP        260.0f
#define GEM_PITCH               28.0f
#define GEM_X                   309.0f
#define LENS_NAME_X             195.0f
#define LENS_NAME_TOP           257.0f
#define SEL_CSR_X               190.0f
#define BASE_CSR_TOP            130.0f
#define LENS_CSR_TOP            252.0f
#define NEXT_PTS_X              0x1d2
#define LENS_NEXT_PTS_TOP       0x102
#define SLOT_PITCH              50.0f
#define SLOT_FRAME_X            186.0f
#define SLOT_FRAME_Y            35.0f
#define ONE_SLOT_FRAME_X        258.0f
#define ONE_SLOT_FRAME_Y        50.0f
#define SLOT_CSR_X              182.0f
#define SLOT_CSR_Y              61.0f
#define MENU_ROW_PITCH          35.0f
#define MENU_ROW_TOP            65.0f
#define MENU_ROW_X              22.0f
#define MENU_ROW_W              161.0f
#define ROW_BG_X                0xc0
#define ROW_BG_W                0x187
#define ROW_BG_H                0x1b

/* The idle shimmer that runs across the gems, and the award animation's own
 * length.  Both are frame counts. */
#define GEM_IDLE_ANIM_TIME      74
#define GEM_AWARD_ANIM_TIME     49

/* The scrollbar's rail is 80 pixels tall whatever the list length. */
#define SCROLL_RAIL_LEN         80.0f
#define SCROLL_TOP              257.0f

/* --------------------------------------------------------------------------
 *  Forward declarations
 * ------------------------------------------------------------------------ */

static void MenuCamEditCtrlInit(void);
static void MenuCamEditSetDispLensData(void);
static void MenuCamEditSelMenuPad(void);
static void MenuCamEditSelMenuPadDecision(void);
static void MenuCamEditMoveTop(void);
static void MenuCamEditSelLensEquipPosPad(void);
static void MenuCamEditEquipLensSelPad(void);
static void MenuCamEditItemSelPad(void);
static void MenuCamEditItemSelDecision(void);
static void MenuCamEditGemAddPosDecision(void);
static void MenuCamEditPowerUpPosDecision(void);
static void MenuCamEditGemAddConfPad(void);
static void MenuCamGemAdd(void);
static void MenuCamEditPowerUpConfPad(void);
static void MenuCamPowerUp(void);
static void MenuCamEditErrorPad(void);
static int  GetMenuCamBasicLv(int base_label);
static int  GetMenuCamBasicLvMax(int base_label);
static int  GetMenuCamBasicGemNum(int base_label);
static int  GetMenuCamBasicGemNumMax(int base_label);
static void MenuCamEditBaseGemAnimReq(char label, char lv);
static void MenuCamEditLensGemAnimReq(char label, char lv);
static void MenuCamEditDispInit(void);
static void MenuCamEditSelMenuDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditLensNothingErrorDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditSelLensEquipPosDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditSelEquipLensDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditSelGemAddPosDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditGemAddConfDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditGemAddErrorDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditGemNothingErrorDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditSelPowerUpPosDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditPowerUpConfDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditPowerUpLvMaxErrorDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditPowerUpNothingErrorDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditCmnDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditGemAddCmnDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditPowerUpCmnDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditItemFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditTitleFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBgDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditMenuFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditSelFrameDisp(float x, float y, float w, u_char alpha,
                                    u_int pri);
static void MenuCamEditNonSelFrameDisp(float x, float y, float w, u_char alpha,
                                       u_int pri);
static void MenuCamEditMenuItemDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditWinDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditReinforcedLensDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditEquipDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditEquipReinforcedLensDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditEquipLensLvDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditHaveStatusDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseFrameBgDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditLensFrameBgDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseItemDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseLvDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseNextPointDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditBaseGemDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditLensFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditHaveLensNameDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditLensNameDisp(float x, float y, u_char alpha,
                                    int lens_label);
static void MenuCamEditLensLvDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditLensNextPointDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditOneLensNextPointDisp(int lens_label, int x, int y,
                                            u_char alpha, int col_label);
static void MenuCamEditLensGemDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditSelEquipLensPosFrameDisp(float x, float y, u_char alpha);
static void MenuCamEditEquipLensPosCsrDisp(float x, float y, u_char alpha,
                                           u_char rgb);
static void MenuCamEditGemDisp(float x, float y, u_char alpha, u_char flg);
static void MenuCamEditGemAnimDisp(float x, float y, u_char alpha,
                                   short int timer);
static void MenuCamEditLvDisp(float y, u_char alpha, int lv);
static void MenuCamEditNextPointDisp(int x, int y, u_char alpha, int col_label,
                                     int next);
static void MenuCamEditSelFrameBgDisp(int y, u_char alpha);
static void MenuCamEditFrameBlackBgDisp(int y, u_char alpha);
static void MenuCamEditFrameBgDisp(float y, u_char alpha);
static void MenuCamEditSelCsrDisp(float x, float y, u_char alpha);
static void MenuCamEditScrollDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditMsgWindowDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditConfYesNoDisp(int off_x, int off_y, u_char alpha);
static void MenuCamEditCaptionDisp(int off_x, int off_y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Data
 * ------------------------------------------------------------------------ */

/* The two dispatch tables, both indexed by MENU_CAM_EDIT_CTRL::mode.  Note
 * how few distinct pad handlers there are: six of the twelve modes are error
 * windows and share MenuCamEditErrorPad(), and the two item selectors share
 * MenuCamEditItemSelPad() outright -- which is why MenuCamEditItemSelDecision()
 * exists, to fork on the mode the shared handler cannot see. */
static void (*menu_cam_edit_pad[MENU_CAM_EDIT_MODE_NUM])(void) = /* data 321670 */
{
    MenuCamEditSelMenuPad,
    MenuCamEditErrorPad,
    MenuCamEditSelLensEquipPosPad,
    MenuCamEditEquipLensSelPad,
    MenuCamEditItemSelPad,
    MenuCamEditItemSelPad,
    MenuCamEditGemAddConfPad,
    MenuCamEditErrorPad,
    MenuCamEditErrorPad,
    MenuCamEditPowerUpConfPad,
    MenuCamEditErrorPad,
    MenuCamEditErrorPad,
};

static void (*menu_cam_edit_disp_func[MENU_CAM_EDIT_MODE_NUM])(int, int, u_char) =
{                                                               /* data 3216a0 */
    MenuCamEditSelMenuDisp,
    MenuCamEditLensNothingErrorDisp,
    MenuCamEditSelLensEquipPosDisp,
    MenuCamEditSelEquipLensDisp,
    MenuCamEditSelGemAddPosDisp,
    MenuCamEditSelPowerUpPosDisp,
    MenuCamEditGemAddConfDisp,
    MenuCamEditGemAddErrorDisp,
    MenuCamEditGemNothingErrorDisp,
    MenuCamEditPowerUpConfDisp,
    MenuCamEditPowerUpLvMaxErrorDisp,
    MenuCamEditPowerUpNothingErrorDisp,
};

/* The caption for each of the three menu rows, and for each lens.  Both are
 * reference_fixed_array in the ROM -- a bare pointer in .sbss patched to a
 * .rodata table by the file's global constructor.  lens_msg_tbl's slot 0 is
 * CAMERA_SUB_FUNC_NONE and is -1, which every reader tests for. */
static int base_msg_tbl_data[MENU_CAM_BASIC_NUM] =              /* rodata 3bcd98 */
{
    0x1b, 0x1c, 0x30
};

static int lens_msg_tbl_data[REINFORCED_LENS_NUM] =             /* rodata 3bcda8 */
{
    -1, 0x21, 0x22, 0x23, 0x24, 0x20, 0x1d, 0x1f, 0x1e, 0x25
};

static reference_fixed_array<int, MENU_CAM_BASIC_NUM>
    base_msg_tbl(base_msg_tbl_data);                            /* sbss 3f4dc0 */

static reference_fixed_array<int, REINFORCED_LENS_NUM>
    lens_msg_tbl(lens_msg_tbl_data);                            /* sbss 3f4dc8 */

/* The compacted list of lenses the player owns.  Rebuilt on every entry. */
static fixed_array<DISP_LENS_DATA, REINFORCED_LENS_NUM>
    disp_lens_data;                                             /* bss 4b5448 */

static MENU_CAM_EDIT_CTRL menu_cam_edit_ctrl;                   /* bss 4b5470 */
static MENU_CAM_EDIT_DISP menu_cam_edit_disp;                   /* sbss 3f4dd0 */

/* One award animation each.  They are separate because a basic performance
 * and a lens can be at different rows of different lists, but only one can
 * ever be running -- gem_anim_flg is shared. */
static GEM_ANIM_CTRL base_gem_anim_ctrl;                        /* sbss 3f4dd8 */
static GEM_ANIM_CTRL lens_gem_anim_ctrl;                        /* sbss 3f4de0 */

/* --------------------------------------------------------------------------
 *  Entry
 * ------------------------------------------------------------------------ */

void MenuCamEditInit(void)                                              /* 378 */
{
    MenuCamEditCtrlInit();                                              /* 382 */

    MenuCamEditSetDispLensData();                                       /* 384 */

    /* Both fields of both controls go to 0xff, so `timer` starts at -1 as
     * well as `data_pos`.  The two Req functions zero it. */
    memset(&base_gem_anim_ctrl, -1, sizeof(GEM_ANIM_CTRL));             /* 386 */
    memset(&lens_gem_anim_ctrl, -1, sizeof(GEM_ANIM_CTRL));             /* 387 */
}

static void MenuCamEditCtrlInit(void)                                   /* 395 */
{
    menu_cam_edit_ctrl.mode              = MENU_CAM_EDIT_MODE_SEL_MENU; /* 398 */
    menu_cam_edit_ctrl.step              = MENU_CAM_EDIT_STEP_INIT;     /* 399 */
    menu_cam_edit_ctrl.menu_csr          = 0;                           /* 400 */
    menu_cam_edit_ctrl.equip_pos_csr     = 0;                           /* 401 */
    menu_cam_edit_ctrl.lens_csr          = 0;                           /* 402 */
    menu_cam_edit_ctrl.edit_sel_csr      = 0;                           /* 403 */
    menu_cam_edit_ctrl.conf_csr          = 0;                           /* 404 */
    menu_cam_edit_ctrl.sp_equip_init_flg = 0;                           /* 405 */
    menu_cam_edit_ctrl.edit_init_flg     = 0;                           /* 406 */
    menu_cam_edit_ctrl.gem_anim_flg      = 0;                           /* 407 */

    MenuRefCtrlInit(&menu_cam_edit_ctrl.ref_ctrl,
                    GetHaveReinforcedLensNum());                        /* 409 */
}

/* Pack the sub-functions the player owns into disp_lens_data[], so the list
 * has no holes and `data_pos` can index it directly.  Slot 0 is seeded with
 * NONE first, which is what an empty list reads back as. */
static void MenuCamEditSetDispLensData(void)                            /* 416 */
{
    int i;
    int count;

    disp_lens_data[0].lens_label = 0;                                   /* 421 */
    count = 0;                                                          /* 422 */

    for (i = 1; i < REINFORCED_LENS_NUM; i++) {                         /* 425 */
        if (m_plyr_camera.camera_power_up.mTemperedRenzFlg.IsUp(i)) {
            disp_lens_data[count++].lens_label = i;                     /* 430 */
        }
    }                                                                   /* 432 */
}

/* One frame.  Returns non-zero once the page has finished closing, which is
 * what lets MenuCamModeMain() release the parked page-change request.
 *
 * Note the pad is skipped entirely while gem_anim_flg is up: the award
 * animation is driven by MenuCamEditBaseGemDisp() / MenuCamEditLensGemDisp()
 * and the page is frozen until one of them lowers the flag. */
int MenuCamEditMain(void)                                               /* 446 */
{
    int res;

    res = 0;                                                            /* 450 */

    if (menu_cam_edit_ctrl.step == MENU_CAM_EDIT_STEP_INIT) {           /* 452 */
        MenuCamEditDispInit();                                          /* 454 */

        menu_cam_edit_ctrl.step = MENU_CAM_EDIT_STEP_MAIN;              /* 456 */
    }

    if (menu_cam_edit_ctrl.step == MENU_CAM_EDIT_STEP_MAIN) {           /* 460 */
        if (menu_cam_edit_ctrl.gem_anim_flg == 0) {                     /* 461 */
            if (menu_cam_edit_pad[menu_cam_edit_ctrl.mode] != nullptr) { /* 463 */
                (*menu_cam_edit_pad[menu_cam_edit_ctrl.mode])();        /* 464 */
            }
        }
    }

    if (menu_cam_edit_ctrl.step == MENU_CAM_EDIT_STEP_OUT) {            /* 469 */
        res = (menu_cam_edit_disp.anim_step == MENU_CAM_EDIT_ANIM_END); /* 470 */
    }

    return res;                                                         /* 476 */
}

/* --------------------------------------------------------------------------
 *  The page's own menu -- three rows, and what CROSS on each opens
 * ------------------------------------------------------------------------ */

static void MenuCamEditSelMenuPad(void)                                 /* 482 */
{
    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 486 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 487 */

        menu_cam_edit_ctrl.menu_csr =
            (menu_cam_edit_ctrl.menu_csr + 2) % MENU_CAM_EDIT_ROW_NUM;  /* 488 */
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 491 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 492 */

        menu_cam_edit_ctrl.menu_csr =
            (menu_cam_edit_ctrl.menu_csr + 1) % MENU_CAM_EDIT_ROW_NUM;  /* 493 */
    }
    else if (*paddat[0] == 1) {                                         /* 496 */
        MenuCamEditSelMenuPadDecision();                                /* 497 */
    }
    else if (*paddat[1] == 1) {                                         /* 500 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 501 */

        MenuCamEditMoveTop();                                           /* 504 */
    }
}

/* CROSS on one of the three rows.
 *
 * The two selector rows share a tail -- "if the other selector was open
 * last, reset my scroll" -- which GCC cross-jumped into one block.  Only
 * case 2's copy carries line numbers (572-582); case 1's own were at
 * 556-566 and were eliminated, which is the gap between its cue at 554 and
 * its mode store at 567. */
static void MenuCamEditSelMenuPadDecision(void)                         /* 513 */
{
    switch (menu_cam_edit_ctrl.menu_csr) {                              /* 516 */

    case MENU_CAM_EDIT_ROW_EQUIP_LENS:
        if (GetHaveReinforcedLensNum() > 0) {                           /* 519 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 520 */

            /* With camera part 1 fitted there are three tray slots to pick
             * between first; without it there is only slot 0, so the slot
             * picker is skipped and the lens list opens directly. */
            if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                    EQUIP_FUNC_SPECIAL_SLOT)) {
                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_SEL_LENS_EQUIP_POS;              /* 525 */
            }
            else {
                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_EQUIP_LENS_SEL;                  /* 529 */
            }

            menu_cam_edit_ctrl.equip_pos_csr    = 0;                    /* 533 */
            menu_cam_edit_disp.csr_anim_timer   = 0;                    /* 534 */

            if (menu_cam_edit_ctrl.sp_equip_init_flg != 0) {            /* 536 */
                menu_cam_edit_ctrl.lens_csr                = 0;         /* 538 */
                menu_cam_edit_ctrl.ref_ctrl.disp_start_pos = 0;         /* 539 */
                menu_cam_edit_ctrl.ref_ctrl.data_pos       = 0;         /* 540 */

                menu_cam_edit_ctrl.sp_equip_init_flg       = 0;         /* 542 */
            }

            menu_cam_edit_ctrl.edit_init_flg = 1;                       /* 545 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 548 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_LENS_NOTHING_ERR;                    /* 552 */
        }
        break;

    case MENU_CAM_EDIT_ROW_GEM_ADD:
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 554 */

        menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS;   /* 567 */

        /* cross-jumped with case 2's copy below -- no line notes survive */
        if (menu_cam_edit_ctrl.edit_init_flg != 0) {
            menu_cam_edit_ctrl.edit_sel_csr            = 0;
            menu_cam_edit_ctrl.ref_ctrl.disp_start_pos = 0;
            menu_cam_edit_ctrl.ref_ctrl.data_pos       = 0;

            menu_cam_edit_ctrl.edit_init_flg           = 0;
        }

        menu_cam_edit_ctrl.sp_equip_init_flg = 1;
        break;

    case MENU_CAM_EDIT_ROW_POWER_UP:
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 569 */

        menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS;  /* 570 */

        if (menu_cam_edit_ctrl.edit_init_flg != 0) {                    /* 572 */
            menu_cam_edit_ctrl.edit_sel_csr            = 0;             /* 574 */
            menu_cam_edit_ctrl.ref_ctrl.disp_start_pos = 0;             /* 575 */
            menu_cam_edit_ctrl.ref_ctrl.data_pos       = 0;             /* 576 */

            menu_cam_edit_ctrl.edit_init_flg           = 0;             /* 578 */
        }

        menu_cam_edit_ctrl.sp_equip_init_flg = 1;                       /* 582 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 584 */
        break;
    }
}

/* Start closing, and park the request to go back to the top page. */
static void MenuCamEditMoveTop(void)                                    /* 593 */
{
    menu_cam_edit_ctrl.step       = MENU_CAM_EDIT_STEP_OUT;             /* 596 */
    menu_cam_edit_disp.anim_step  = MENU_CAM_EDIT_ANIM_OUT;             /* 597 */
    menu_cam_edit_disp.anim_timer = 0;                                  /* 598 */

    MenuCamGoToTopReq();                                                /* 601 */
}

/* --------------------------------------------------------------------------
 *  The lens equip screen -- pick a tray slot, then pick a lens for it
 * ------------------------------------------------------------------------ */

/* LEFT/RIGHT walk the tray slots.  There are as many as the player has
 * lenses fitted plus one (the first empty one), capped at three; without
 * camera part 1 there is only slot 0, and both arms snap the cursor back to
 * it rather than wrapping. */
static void MenuCamEditSelLensEquipPosPad(void)                         /* 610 */
{
    int csr_range;

    csr_range = GetEquipReinforcedLensNum() + 1;                        /* 615 */

    if (csr_range > EQUIP_SPECIAL_NUM) {                                /* 617 */
        csr_range = EQUIP_SPECIAL_NUM;
    }

    if ((pad[0].rpt & PAD_RPT_LEFT) || GetPadAnalogRpt(PAD_ANALOG_LEFT)) { /* 623 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 624 */

        if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                EQUIP_FUNC_SPECIAL_SLOT)) {
            menu_cam_edit_ctrl.equip_pos_csr =
                (menu_cam_edit_ctrl.equip_pos_csr + csr_range - 1)
                % csr_range;                                            /* 627 */
        }
        else {
            menu_cam_edit_ctrl.equip_pos_csr = 0;
        }
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 634 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 635 */

        if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                EQUIP_FUNC_SPECIAL_SLOT)) {
            menu_cam_edit_ctrl.equip_pos_csr =
                (menu_cam_edit_ctrl.equip_pos_csr + 1) % csr_range;     /* 638 */
        }
        else {
            menu_cam_edit_ctrl.equip_pos_csr = 0;
        }
    }
    else if (*paddat[0] == 1) {                                         /* 641 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 645 */

        menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_EQUIP_LENS_SEL;    /* 646 */
    }
    else if (*paddat[1] == 1) {                                         /* 648 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 651 */

        menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_MENU;          /* 652 */
    }
}                                                                       /* 654 */

/* UP/DOWN walk the lens list, CROSS fits the highlighted lens.
 *
 * The cue at the bottom is gated on data_pos having actually changed, which
 * is what data_pos_back_up is for -- a cursor already at an end of the list
 * makes no sound.  Both movement arms then share one SystemBankPlay() with
 * the CROSS arm's error cue, cross-jumped because they differ only in the
 * cue number. */
static void MenuCamEditEquipLensSelPad(void)                            /* 663 */
{
    int  i;
    int  disp_num;
    int  data_pos_back_up;
    fixed_array<char, EQUIP_SPECIAL_NUM> equip_special;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 671 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 673 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    data_pos_back_up = menu_cam_edit_ctrl.ref_ctrl.data_pos;            /* 677 */

    m_plyr_camera.eq_tray.GetSubFuncArray(&equip_special[0]);

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 683 */
        menu_cam_edit_ctrl.lens_csr--;                                  /* 684 */

        if (menu_cam_edit_ctrl.lens_csr < 0) {                          /* 685 */
            menu_cam_edit_ctrl.lens_csr = 0;                            /* 686 */

            menu_cam_edit_ctrl.ref_ctrl.disp_start_pos--;               /* 689 */

            if (menu_cam_edit_ctrl.ref_ctrl.disp_start_pos < 0) {       /* 691 */
                menu_cam_edit_ctrl.ref_ctrl.disp_start_pos = 0;
            }

            menu_cam_edit_ctrl.ref_ctrl.data_pos--;                     /* 696 */

            if (menu_cam_edit_ctrl.ref_ctrl.data_pos < 0) {             /* 697 */
                menu_cam_edit_ctrl.ref_ctrl.data_pos = 0;               /* 698 */
            }
        }
        else {
            menu_cam_edit_ctrl.ref_ctrl.data_pos--;                     /* 703 */

            if (menu_cam_edit_ctrl.ref_ctrl.data_pos < 0) {             /* 704 */
                menu_cam_edit_ctrl.ref_ctrl.data_pos = 0;
            }
        }

        if (menu_cam_edit_ctrl.ref_ctrl.data_pos != data_pos_back_up) { /* 710 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 711 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 715 */
        menu_cam_edit_ctrl.lens_csr++;                                  /* 716 */

        if (menu_cam_edit_ctrl.lens_csr >= disp_num) {                  /* 718 */
            menu_cam_edit_ctrl.lens_csr = disp_num - 1;                 /* 719 */

            menu_cam_edit_ctrl.ref_ctrl.disp_start_pos++;               /* 722 */

            if (menu_cam_edit_ctrl.ref_ctrl.disp_start_pos
                > menu_cam_edit_ctrl.ref_ctrl.data_num
                  - MENU_CAM_EDIT_LIST_DISP_NUM) {                      /* 723 */
                menu_cam_edit_ctrl.ref_ctrl.disp_start_pos--;           /* 724 */
            }
        }

        menu_cam_edit_ctrl.ref_ctrl.data_pos++;                         /* 729 */

        if (menu_cam_edit_ctrl.ref_ctrl.data_pos
            >= menu_cam_edit_ctrl.ref_ctrl.data_num) {                  /* 730 */
            menu_cam_edit_ctrl.ref_ctrl.data_pos--;                     /* 731 */
        }

        if (menu_cam_edit_ctrl.ref_ctrl.data_pos != data_pos_back_up) { /* 735 */
            SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 736 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 740 */
        /* A lens already on the tray cannot be fitted twice, whichever slot
         * it is in.  The scan runs over all three slots even when only slot
         * 0 is usable. */
        for (i = 0; i < EQUIP_SPECIAL_NUM; i++) {                       /* 742 */
            if (equip_special[i]
                == disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos]
                       .lens_label) {                                   /* 747 */
                break;
            }
        }

        if (i >= EQUIP_SPECIAL_NUM) {
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 751 */

            if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                    EQUIP_FUNC_SPECIAL_SLOT)) {
                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_SEL_LENS_EQUIP_POS;              /* 756 */
                equip_special[menu_cam_edit_ctrl.equip_pos_csr] =
                    (char)disp_lens_data[
                        menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;
            }
            else {
                menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_MENU;  /* 760 */
                equip_special[0] =
                    (char)disp_lens_data[
                        menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;
            }

            m_plyr_camera.eq_tray.SetSubFuncArray(&equip_special[0]);
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 767 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 771 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 772 */

        if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
                EQUIP_FUNC_SPECIAL_SLOT)) {
            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_SEL_LENS_EQUIP_POS;                  /* 776 */
        }
        else {
            menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_MENU;
        }
    }
}                                                                       /* 779 */

/* --------------------------------------------------------------------------
 *  The two item selectors -- shared pad, forked decision
 *
 *  Modes 4 and 5 run the same handler over the same combined list; the only
 *  difference is what CROSS does, which is why MenuCamEditItemSelDecision()
 *  exists.  `edit_sel_csr` 0..2 are the three basic performances (which do
 *  not scroll) and 3.. the lens rows (which do), so the cursor ladder is
 *  written out twice -- once per half -- with the crossings between them
 *  spelled out in full.
 * ------------------------------------------------------------------------ */

static void MenuCamEditItemSelPad(void)                                 /* 789 */
{
    int disp_num;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 793 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 795 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 801 */
        if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {     /* 803 */
            menu_cam_edit_ctrl.edit_sel_csr--;                          /* 804 */

            if (menu_cam_edit_ctrl.edit_sel_csr < 0) {                  /* 806 */
                menu_cam_edit_ctrl.edit_sel_csr = 0;                    /* 807 */
            }
            else {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 810 */
            }
        }
        else {
            menu_cam_edit_ctrl.edit_sel_csr--;                          /* 815 */

            /* Crossing back out of the lens half into the basic half: the
             * cursor parks on the first lens row and the window scrolls
             * instead, unless the list is already at its top -- in which
             * case the cursor really does leave, onto basic row 2. */
            if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) { /* 816 */
                menu_cam_edit_ctrl.edit_sel_csr = MENU_CAM_BASIC_NUM;   /* 817 */

                menu_cam_edit_ctrl.ref_ctrl.disp_start_pos--;           /* 820 */

                if (menu_cam_edit_ctrl.ref_ctrl.disp_start_pos < 0) {   /* 822 */
                    menu_cam_edit_ctrl.ref_ctrl.disp_start_pos = 0;
                }

                menu_cam_edit_ctrl.ref_ctrl.data_pos--;                 /* 827 */

                if (menu_cam_edit_ctrl.ref_ctrl.data_pos < 0) {         /* 828 */
                    menu_cam_edit_ctrl.ref_ctrl.data_pos = 0;           /* 829 */
                    menu_cam_edit_ctrl.edit_sel_csr =
                        MENU_CAM_BASIC_NUM - 1;                         /* 830 */
                }

                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 833 */
            }
            else {
                menu_cam_edit_ctrl.ref_ctrl.data_pos--;                 /* 837 */

                if (menu_cam_edit_ctrl.ref_ctrl.data_pos < 0) {         /* 838 */
                    menu_cam_edit_ctrl.ref_ctrl.data_pos = 0;           /* 839 */
                }
                else {
                    SystemBankPlay(SE_CURSOR, 1, 0, 0,
                                   (SND_3D_SET *)nullptr,
                                   0x3200, 0x1000);                     /* 842 */
                }
            }
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 848 */
        if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {     /* 850 */
            menu_cam_edit_ctrl.edit_sel_csr++;                          /* 851 */

            /* Falling off the bottom of the basic half with no lenses at
             * all: park back on the last basic row, silently. */
            if (menu_cam_edit_ctrl.edit_sel_csr >= MENU_CAM_BASIC_NUM) { /* 853 */
                if (disp_num <= 0) {                                    /* 855 */
                    menu_cam_edit_ctrl.edit_sel_csr =
                        MENU_CAM_BASIC_NUM - 1;                         /* 856 */
                }
                else {
                    SystemBankPlay(SE_CURSOR, 1, 0, 0,
                                   (SND_3D_SET *)nullptr,
                                   0x3200, 0x1000);                     /* 859 */
                }
            }
            else {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 863 */
            }
        }
        else {
            menu_cam_edit_ctrl.edit_sel_csr++;                          /* 868 */

            if (menu_cam_edit_ctrl.edit_sel_csr
                >= disp_num + MENU_CAM_BASIC_NUM) {                     /* 870 */
                menu_cam_edit_ctrl.edit_sel_csr =
                    (char)(disp_num + MENU_CAM_BASIC_NUM - 1);          /* 871 */

                menu_cam_edit_ctrl.ref_ctrl.disp_start_pos++;           /* 874 */

                if (menu_cam_edit_ctrl.ref_ctrl.disp_start_pos
                    > menu_cam_edit_ctrl.ref_ctrl.data_num
                      - MENU_CAM_EDIT_LIST_DISP_NUM) {                  /* 875 */
                    menu_cam_edit_ctrl.ref_ctrl.disp_start_pos--;       /* 876 */
                }
            }

            menu_cam_edit_ctrl.ref_ctrl.data_pos++;                     /* 881 */

            if (menu_cam_edit_ctrl.ref_ctrl.data_pos
                >= menu_cam_edit_ctrl.ref_ctrl.data_num) {              /* 882 */
                menu_cam_edit_ctrl.ref_ctrl.data_pos--;                 /* 883 */
            }
            else {
                SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 886 */
            }
        }
    }
    else if (*paddat[0] == 1) {                                         /* 891 */
        MenuCamEditItemSelDecision();                                   /* 892 */

        menu_cam_edit_ctrl.conf_csr = 1;                                /* 895 */
    }
    else if (*paddat[1] == 1) {                                         /* 898 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 899 */

        menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_MENU;          /* 901 */
    }
}

/* The fork the shared pad handler cannot make for itself. */
static void MenuCamEditItemSelDecision(void)                            /* 910 */
{
    if (menu_cam_edit_ctrl.mode == MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS) { /* 914 */
        MenuCamEditGemAddPosDecision();                                 /* 916 */
    }
    else if (menu_cam_edit_ctrl.mode
             == MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS) {
        MenuCamEditPowerUpPosDecision();                                /* 919 */
    }
    else {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 922 */
    }
}

/* CROSS on the gem selector: refuse if the slot is already at three gems or
 * the player has none to spend, otherwise open the confirm window.
 *
 * The two "already at three" arms are one block in the object -- they differ
 * only in how the count is fetched -- so only the basic half's copy carries
 * line numbers. */
static void MenuCamEditGemAddPosDecision(void)                          /* 931 */
{
    int lens_label;

    lens_label =
        disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 940 */
        if (GetMenuCamBasicGemNum(menu_cam_edit_ctrl.edit_sel_csr)
            == GetMenuCamBasicGemNumMax(
                   menu_cam_edit_ctrl.edit_sel_csr)) {                  /* 942 */
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 943 */

            menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_GEM_ADD_ERR;   /* 946 */
            return;
        }
    }
    else {
        if (m_plyr_camera.camera_power_up.mSubFuncGem[lens_label].Get()
            == m_plyr_camera.camera_power_up.mSubFuncGem[lens_label]
                   .GetMax()) {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);

            menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_GEM_ADD_ERR;
            return;
        }
    }

    if (GetPlyrLevelGemNum() < 1) {                                     /* 968 */
        SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 969 */

        menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_GEM_NOTHING_ERR;   /* 973 */
    }
    else {
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 974 */

        menu_cam_edit_ctrl.mode     = MENU_CAM_EDIT_MODE_GEM_ADD_CONF;  /* 976 */
        menu_cam_edit_ctrl.conf_csr = 1;                                /* 977 */
    }
}                                                                       /* 982 */

/* CROSS on the power-up selector.  Three refusals, in order: already at the
 * top grade (which plays the error cue and opens no window at all), not
 * enough gems banked for the next grade, and not enough spirit points.
 *
 * The final yes/no block is written out in both halves and cross-jumped into
 * one, which is why the basic half's copy leaves only the branch at 1021 and
 * the lens half's carries 1045-1052.  `functions.txt` listing no flag local
 * is what proves it really is two copies rather than one shared tail. */
static void MenuCamEditPowerUpPosDecision(void)                         /* 993 */
{
    int lens_label;

    lens_label =
        disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1002 */
        if (GetMenuCamBasicLv(menu_cam_edit_ctrl.edit_sel_csr)
            >= GetMenuCamBasicLvMax(
                   menu_cam_edit_ctrl.edit_sel_csr)) {                  /* 1004 */
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1005 */
            return;
        }

        if (GetMenuCamBasicLv(menu_cam_edit_ctrl.edit_sel_csr)
            >= GetMenuCamBasicGemNum(
                   menu_cam_edit_ctrl.edit_sel_csr)) {                  /* 1008 */
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1009 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_POWER_UP_LV_MAX_ERR;                 /* 1012 */
            return;
        }

        if (GetPlayData_Score()
            >= cam_base_status_point[menu_cam_edit_ctrl.edit_sel_csr]
                   [GetMenuCamBasicLv(menu_cam_edit_ctrl.edit_sel_csr)
                    + 1]) {                                             /* 1016 */
            /* cross-jumped with the lens half's copy at 1045-1048 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);

            menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_POWER_UP_CONF;
            menu_cam_edit_ctrl.conf_csr = 1;
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1021 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_POWER_UP_NOTHING_ERR;
        }
    }
    else {
        if (m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get()
            == m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label]
                   .GetMax()) {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1034 */
            return;
        }

        if (m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get()
            >= m_plyr_camera.camera_power_up.mSubFuncGem[lens_label]
                   .Get()) {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1038 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_POWER_UP_LV_MAX_ERR;                 /* 1040 */
            return;
        }

        if (GetPlayData_Score()
            >= cam_sp_shot_point_tbl[lens_label]
                   [m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label]
                        .Get() + 1]) {                                  /* 1044 */
            SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1045 */

            menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_POWER_UP_CONF; /* 1047 */
            menu_cam_edit_ctrl.conf_csr = 1;                            /* 1048 */
        }
        else {
            SystemBankPlay(SE_ERROR, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1051 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_POWER_UP_NOTHING_ERR;                /* 1052 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  The two confirm windows, and what saying yes does
 *
 *  Both handlers are the same code twice, statement for statement -- the
 *  line offsets inside them agree exactly.  Note the outer test: LEFT and
 *  RIGHT are checked first and the buttons only reach the pad at all when
 *  neither is down, so the yes/no toggle takes priority over CROSS in the
 *  frame they arrive together.
 * ------------------------------------------------------------------------ */

static void MenuCamEditGemAddConfPad(void)                              /* 1063 */
{
    if (((pad[0].rpt & PAD_RPT_LEFT) == 0)
        && (GetPadAnalogRpt(PAD_ANALOG_LEFT) == 0)
        && ((pad[0].rpt & PAD_RPT_RIGHT) == 0)
        && (GetPadAnalogRpt(PAD_ANALOG_RIGHT) == 0)) {                  /* 1067 */
        if (*paddat[0] == 1) {                                          /* 1072 */
            if (menu_cam_edit_ctrl.conf_csr == 0) {                     /* 1073 */
                SystemBankPlay(SE_GEM_ADD, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 1074 */

                MenuCamGemAdd();                                        /* 1077 */

                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS;                 /* 1079 */
            }
            else {
                SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 1080 */

                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS;                 /* 1083 */
            }
        }
        else if (*paddat[1] == 1) {                                     /* 1085 */
            SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1091 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS;                     /* 1095 */
        }
    }
    else {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1096 */

        menu_cam_edit_ctrl.conf_csr ^= 1;                               /* 1098 */
    }
}

/* Spend one level gem.  The assert at the top is a should-never-happen --
 * MenuCamEditGemAddPosDecision() has already refused an empty purse -- and
 * it does not stop the increment; LostLevelGem() runs whatever it says. */
static void MenuCamGemAdd(void)                                         /* 1107 */
{
    int lens_label;

    lens_label =
        disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;

    if (GetPlyrLevelGemNum() < 1) {                                     /* 1115 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1116 */
    }

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1122 */
        switch (menu_cam_edit_ctrl.edit_sel_csr) {                      /* 1123 */

        case MENU_CAM_BASIC_RADIUS:
            m_plyr_camera.camera_power_up.mRadiusGem.Increment();
            break;

        case MENU_CAM_BASIC_STOCK:
            m_plyr_camera.camera_power_up.mAccumGem.Increment();
            break;

        case MENU_CAM_BASIC_SENSITIVE:
            m_plyr_camera.camera_power_up.mSensiteiveGem.Increment();
            break;

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 1134 */
            break;
        }
    }
    else {
        m_plyr_camera.camera_power_up.mSubFuncGem[lens_label].Increment();
    }

    LostLevelGem();                                                     /* 1143 */
}

static void MenuCamEditPowerUpConfPad(void)                             /* 1151 */
{
    if (((pad[0].rpt & PAD_RPT_LEFT) == 0)
        && (GetPadAnalogRpt(PAD_ANALOG_LEFT) == 0)
        && ((pad[0].rpt & PAD_RPT_RIGHT) == 0)
        && (GetPadAnalogRpt(PAD_ANALOG_RIGHT) == 0)) {                  /* 1155 */
        if (*paddat[0] == 1) {                                          /* 1160 */
            if (menu_cam_edit_ctrl.conf_csr == 0) {                     /* 1161 */
                SystemBankPlay(SE_POWER_UP, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 1162 */

                MenuCamPowerUp();                                       /* 1165 */

                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS;                /* 1167 */
            }
            else {
                SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                               0x3200, 0x1000);                         /* 1168 */

                menu_cam_edit_ctrl.mode =
                    MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS;                /* 1171 */
            }
        }
        else if (*paddat[1] == 1) {                                     /* 1173 */
            SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 1179 */

            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS;                    /* 1183 */
        }
    }
    else {
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1184 */

        menu_cam_edit_ctrl.conf_csr ^= 1;                               /* 1186 */
    }
}

/* Raise one grade: bump it, charge the spirit points the NEW grade costs
 * (the increment happens first, so the subscript is the grade just reached),
 * and arm the gem award animation for the gem one below it. */
static void MenuCamPowerUp(void)                                        /* 1195 */
{
    int lens_label;

    lens_label =
        disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1203 */
        switch (menu_cam_edit_ctrl.edit_sel_csr) {                      /* 1204 */

        case MENU_CAM_BASIC_RADIUS:
            m_plyr_camera.camera_power_up.mRadiusGrade.Increment();
            PlayData_ScoreCount(
                -cam_base_status_point[MENU_CAM_BASIC_RADIUS]
                    [m_plyr_camera.camera_power_up.mRadiusGrade.Get()]); /* 1209 */
            break;                                                      /* 1210 */

        case MENU_CAM_BASIC_STOCK:
            m_plyr_camera.eq_tray.mSave.mStockGrade.Increment();
            PlayData_ScoreCount(
                -cam_base_status_point[MENU_CAM_BASIC_STOCK]
                    [m_plyr_camera.eq_tray.mSave.mStockGrade.Get()]);   /* 1215 */
            break;                                                      /* 1216 */

        case MENU_CAM_BASIC_SENSITIVE:
            m_plyr_camera.camera_power_up.mSensitiveGrade.Increment();
            PlayData_ScoreCount(
                -cam_base_status_point[MENU_CAM_BASIC_SENSITIVE]
                    [m_plyr_camera.camera_power_up.mSensitiveGrade
                         .Get()]);                                      /* 1221 */
            break;                                                      /* 1222 */

        default:
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 1224 */
            break;
        }

        MenuCamEditBaseGemAnimReq(
            menu_cam_edit_ctrl.edit_sel_csr,
            (char)(GetMenuCamBasicLv(menu_cam_edit_ctrl.edit_sel_csr)
                   - 1));                                               /* 1228 */
    }
    else {
        m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Increment();

        PlayData_ScoreCount(
            -cam_sp_shot_point_tbl[lens_label]
                [m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label]
                     .Get()]);                                          /* 1235 */

        MenuCamEditLensGemAnimReq(
            (char)lens_label,
            (char)(m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label]
                       .Get() - 1));
    }

    menu_cam_edit_ctrl.gem_anim_flg = 1;                                /* 1241 */
}

/* Every error window's pad.  Either button dismisses it; which mode it goes
 * back to is the switch, and the six modes that are not error windows are
 * real empty cases in the ROM's jump table rather than a default. */
static void MenuCamEditErrorPad(void)                                   /* 1249 */
{
    char pad_flg;

    pad_flg = 0;

    if (*paddat[0] == 1) {                                              /* 1257 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1258 */

        pad_flg = 1;                                                    /* 1260 */
    }
    else if (*paddat[1] == 1) {                                         /* 1263 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1264 */

        pad_flg = 1;                                                    /* 1266 */
    }

    if (pad_flg != 0) {                                                 /* 1270 */
        switch (menu_cam_edit_ctrl.mode) {                              /* 1272 */

        case MENU_CAM_EDIT_MODE_LENS_NOTHING_ERR:
            menu_cam_edit_ctrl.mode = MENU_CAM_EDIT_MODE_SEL_MENU;      /* 1275 */
            break;

        case MENU_CAM_EDIT_MODE_SEL_LENS_EQUIP_POS:
        case MENU_CAM_EDIT_MODE_EQUIP_LENS_SEL:
        case MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS:
        case MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS:
        case MENU_CAM_EDIT_MODE_GEM_ADD_CONF:
        case MENU_CAM_EDIT_MODE_POWER_UP_CONF:
            break;

        case MENU_CAM_EDIT_MODE_GEM_ADD_ERR:
        case MENU_CAM_EDIT_MODE_GEM_NOTHING_ERR:
            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_SEL_GEM_ADD_POS;                     /* 1279 */
            break;

        case MENU_CAM_EDIT_MODE_POWER_UP_LV_MAX_ERR:
        case MENU_CAM_EDIT_MODE_POWER_UP_NOTHING_ERR:
            menu_cam_edit_ctrl.mode =
                MENU_CAM_EDIT_MODE_SEL_POWER_UP_POS;                    /* 1282 */
            break;
        }
    }
}                                                                       /* 1283 */

/* --------------------------------------------------------------------------
 *  The four basic-performance accessors
 *
 *  There is no table behind these: each is a switch over the three rows,
 *  because the three values live in two different objects (the stock grade
 *  is the equip tray's, the other two the camera's).  All four assert on an
 *  out-of-range row and return 0.
 * ------------------------------------------------------------------------ */

static int GetMenuCamBasicLv(int base_label)                            /* 1293 */
{
    int lv;

    lv = 0;                                                             /* 1297 */

    switch (base_label) {                                               /* 1300 */

    case MENU_CAM_BASIC_RADIUS:
        lv = m_plyr_camera.camera_power_up.mRadiusGrade.Get();          /* 1303 */
        break;

    case MENU_CAM_BASIC_STOCK:
        lv = m_plyr_camera.eq_tray.mSave.mStockGrade.Get();             /* 1306 */
        break;

    case MENU_CAM_BASIC_SENSITIVE:
        lv = m_plyr_camera.camera_power_up.mSensitiveGrade.Get();       /* 1309 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1311 */
        break;
    }

    return lv;                                                          /* 1315 */
}

static int GetMenuCamBasicLvMax(int base_label)                         /* 1323 */
{
    int lv;

    lv = 0;                                                             /* 1327 */

    switch (base_label) {                                               /* 1330 */

    case MENU_CAM_BASIC_RADIUS:
    case MENU_CAM_BASIC_STOCK:
    case MENU_CAM_BASIC_SENSITIVE:
        lv = MENU_CAM_LV_MAX;
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1339 */
        break;
    }

    return lv;                                                          /* 1345 */
}

static int GetMenuCamBasicGemNum(int base_label)                        /* 1353 */
{
    int gem_num;

    gem_num = 0;                                                        /* 1357 */

    switch (base_label) {                                               /* 1360 */

    case MENU_CAM_BASIC_RADIUS:
        gem_num = m_plyr_camera.camera_power_up.mRadiusGem.Get();       /* 1363 */
        break;

    case MENU_CAM_BASIC_STOCK:
        gem_num = m_plyr_camera.camera_power_up.mAccumGem.Get();        /* 1366 */
        break;

    case MENU_CAM_BASIC_SENSITIVE:
        gem_num = m_plyr_camera.camera_power_up.mSensiteiveGem.Get();   /* 1369 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1371 */
        break;
    }

    return gem_num;                                                     /* 1375 */
}

static int GetMenuCamBasicGemNumMax(int base_label)                     /* 1383 */
{
    int gem_num;

    gem_num = 0;                                                        /* 1387 */

    switch (base_label) {                                               /* 1390 */

    case MENU_CAM_BASIC_RADIUS:
    case MENU_CAM_BASIC_STOCK:
    case MENU_CAM_BASIC_SENSITIVE:
        gem_num = MENU_CAM_LV_MAX;
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1399 */
        break;
    }

    return gem_num;                                                     /* 1405 */
}

/* --------------------------------------------------------------------------
 *  Arming the gem award animation
 *
 *  Both refuse while one is already running -- `data_pos == -1` is the idle
 *  state MenuCamEditInit()'s memset leaves behind, and the drawing side is
 *  what puts it back.
 * ------------------------------------------------------------------------ */

static void MenuCamEditBaseGemAnimReq(char label, char lv)              /* 1417 */
{
    if (base_gem_anim_ctrl.data_pos == -1) {                            /* 1421 */
        base_gem_anim_ctrl.data_pos = label;                            /* 1422 */
        base_gem_anim_ctrl.lv       = lv;                               /* 1423 */
        base_gem_anim_ctrl.timer    = 0;                                /* 1424 */
    }
}

static void MenuCamEditLensGemAnimReq(char label, char lv)              /* 1435 */
{
    if (lens_gem_anim_ctrl.data_pos == -1) {                            /* 1439 */
        lens_gem_anim_ctrl.data_pos = label;                            /* 1440 */
        lens_gem_anim_ctrl.lv       = lv;                               /* 1441 */
        lens_gem_anim_ctrl.timer    = 0;                                /* 1442 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

static void MenuCamEditDispInit(void)                                   /* 1455 */
{
    menu_cam_edit_disp.anim_step         = 0;                           /* 1458 */
    menu_cam_edit_disp.anim_timer        = 0;                           /* 1459 */
    menu_cam_edit_disp.csr_anim_timer    = 0;                           /* 1460 */
    menu_cam_edit_disp.scroll_anim_timer = 0;                           /* 1461 */
    menu_cam_edit_disp.gem_anim_timer    = 0;                           /* 1462 */
}

void MenuCamEditDisp(void)                                              /* 1470 */
{
    u_char alpha;

    if (menu_cam_edit_ctrl.step == MENU_CAM_EDIT_STEP_MAIN
        || menu_cam_edit_ctrl.step == MENU_CAM_EDIT_STEP_OUT) {         /* 1478 */
        alpha = Zero2Anim2D_InOutAnimCtrl(&menu_cam_edit_disp.anim_step,
                                          &menu_cam_edit_disp.anim_timer,
                                          MENU_CAM_EDIT_FADE_IN_TIME,
                                          MENU_CAM_EDIT_FADE_OUT_TIME); /* 1479 */

        if (menu_cam_edit_disp.anim_step != MENU_CAM_EDIT_ANIM_END) {   /* 1481 */
            if (menu_cam_edit_disp_func[menu_cam_edit_ctrl.mode]
                != nullptr) {                                           /* 1482 */
                (*menu_cam_edit_disp_func[menu_cam_edit_ctrl.mode])(
                    0, 0, alpha);                                       /* 1483 */
            }
        }
    }
}

/* --------------------------------------------------------------------------
 *  The twelve mode drawing functions
 *
 *  Each one lays the shared page down and then adds whatever its mode needs
 *  on top, ending with the caption.  There is no common prologue helper for
 *  the first six calls -- they are written out in every mode that needs
 *  them, which is why MenuCamEditSelMenuDisp() and
 *  MenuCamEditLensNothingErrorDisp() are the same function but for the
 *  message id.
 * ------------------------------------------------------------------------ */

static void MenuCamEditSelMenuDisp(int off_x, int off_y, u_char alpha)   /* 1501 */
{
    static int msg_id[MENU_CAM_EDIT_ROW_NUM] =                  /* rodata 3bcbb8 */
    {
        0x18, 0x19, 0x1a
    };

    MenuCamEditCmnDisp(off_x, off_y, alpha);                            /* 1511 */
    MenuCamEditBaseNextPointDisp(off_x, off_y, alpha);                  /* 1514 */
    MenuCamEditLensNextPointDisp(off_x, off_y, alpha);                  /* 1517 */
    MenuCamEditItemFrameDisp(off_x, off_y, alpha);                      /* 1520 */
    MenuCamEditBaseGemDisp(off_x, off_y, alpha);                        /* 1523 */
    MenuCamEditLensGemDisp(off_x, off_y, alpha);                        /* 1526 */

    PrintMsg(MENU_CAM_MSG_TYPE, msg_id[menu_cam_edit_ctrl.menu_csr],
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1530 */
}

static void MenuCamEditLensNothingErrorDisp(int off_x, int off_y,
                                            u_char alpha)               /* 1541 */
{
    MenuCamEditCmnDisp(off_x, off_y, alpha);                            /* 1545 */
    MenuCamEditBaseNextPointDisp(off_x, off_y, alpha);                  /* 1548 */
    MenuCamEditLensNextPointDisp(off_x, off_y, alpha);                  /* 1551 */
    MenuCamEditItemFrameDisp(off_x, off_y, alpha);                      /* 1554 */
    MenuCamEditBaseGemDisp(off_x, off_y, alpha);                        /* 1557 */
    MenuCamEditLensGemDisp(off_x, off_y, alpha);                        /* 1560 */

    PrintMsg(MENU_CAM_MSG_TYPE, MSG_LENS_NOTHING,
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1564 */
}

/* Picking which tray slot the next lens goes into.  The three list rows are
 * blacked out while the slot cursor is up, so the eye stays on the tray. */
static void MenuCamEditSelLensEquipPosDisp(int off_x, int off_y,
                                           u_char alpha)                /* 1575 */
{
    int    i;
    u_char rgb;

    Zero2Anim2D_CsrAnimCtrl(&menu_cam_edit_disp.csr_anim_timer, &rgb);  /* 1581 */

    MenuCamEditCmnDisp(off_x, off_y, alpha);                            /* 1585 */
    MenuCamEditBaseNextPointDisp(off_x, off_y, alpha);                  /* 1588 */
    MenuCamEditLensNextPointDisp(off_x, off_y, alpha);                  /* 1591 */

    for (i = 0; i < MENU_CAM_EDIT_LIST_DISP_NUM; i++) {                 /* 1594 */
        MenuCamEditFrameBlackBgDisp(
            (int)((float)i * ROW_PITCH + BASE_ROW_TOP), alpha);         /* 1595 */
    }                                                                   /* 1596 */

    MenuCamEditSelEquipLensPosFrameDisp(
        (float)menu_cam_edit_ctrl.equip_pos_csr * SLOT_PITCH
            + SLOT_FRAME_X,
        SLOT_FRAME_Y, alpha);                                           /* 1600 */

    MenuCamEditEquipLensPosCsrDisp(
        (float)menu_cam_edit_ctrl.equip_pos_csr * SLOT_PITCH + SLOT_CSR_X,
        SLOT_CSR_Y, alpha, rgb);                                        /* 1604 */

    MenuCamEditItemFrameDisp(off_x, off_y, alpha);                      /* 1607 */
    MenuCamEditBaseGemDisp(off_x, off_y, alpha);                        /* 1610 */
    MenuCamEditLensGemDisp(off_x, off_y, alpha);                        /* 1613 */

    PrintMsg(MENU_CAM_MSG_TYPE, MSG_SEL_LENS_EQUIP_POS,
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1616 */
}

/* Picking the lens itself.  Note the caption is skipped entirely when the
 * highlighted lens has no message -- only slot 0 (NONE) has -1, and it can
 * never be highlighted, so the test never fires in practice. */
static void MenuCamEditSelEquipLensDisp(int off_x, int off_y,
                                        u_char alpha)                   /* 1627 */
{
    int i;
    int disp_num;
    int lens_label;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 1633 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 1635 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    MenuCamEditCmnDisp(off_x, off_y, alpha);                            /* 1642 */
    MenuCamEditBaseNextPointDisp(off_x, off_y, alpha);                  /* 1645 */

    for (i = 0; i < disp_num; i++) {                                    /* 1647 */
        lens_label =
            disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.disp_start_pos + i]
                .lens_label;

        if (lens_label
            == disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos]
                   .lens_label) {
            MenuCamEditOneLensNextPointDisp(
                lens_label, NEXT_PTS_X, LENS_NEXT_PTS_TOP + i * 32,
                alpha, MCE_COL_SELECTED);                               /* 1654 */

            MenuCamEditSelFrameBgDisp(
                (int)((float)i * ROW_PITCH + LENS_ROW_TOP), alpha);     /* 1657 */
        }
        else {
            MenuCamEditOneLensNextPointDisp(
                lens_label, NEXT_PTS_X, LENS_NEXT_PTS_TOP + i * 32,
                alpha, MCE_COL_NORMAL);                                 /* 1662 */
        }
    }                                                                   /* 1665 */

    for (i = 0; i < MENU_CAM_EDIT_LIST_DISP_NUM; i++) {                 /* 1668 */
        MenuCamEditFrameBlackBgDisp(
            (int)((float)i * ROW_PITCH + BASE_ROW_TOP), alpha);         /* 1669 */
    }                                                                   /* 1670 */

    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
            EQUIP_FUNC_SPECIAL_SLOT)) {
        MenuCamEditSelEquipLensPosFrameDisp(
            (float)menu_cam_edit_ctrl.equip_pos_csr * SLOT_PITCH
                + SLOT_FRAME_X,
            SLOT_FRAME_Y, alpha);                                       /* 1676 */
    }
    else {
        MenuCamEditSelEquipLensPosFrameDisp(ONE_SLOT_FRAME_X,
                                            ONE_SLOT_FRAME_Y, alpha);   /* 1679 */
    }

    MenuCamEditItemFrameDisp(off_x, off_y, alpha);                      /* 1683 */
    MenuCamEditBaseGemDisp(off_x, off_y, alpha);                        /* 1686 */
    MenuCamEditLensGemDisp(off_x, off_y, alpha);                        /* 1689 */

    MenuCamEditSelCsrDisp(
        SEL_CSR_X,
        (float)menu_cam_edit_ctrl.lens_csr * ROW_PITCH + LENS_CSR_TOP,
        alpha);                                                         /* 1692 */

    if (lens_msg_tbl[disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos]
                         .lens_label] != -1) {
        PrintMsg(MENU_CAM_MSG_TYPE,
                 lens_msg_tbl[disp_lens_data[
                     menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label],
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);
    }
}

/* The gem selector's caption: whichever half of the combined list the cursor
 * is in names the thing under it. */
static void MenuCamEditSelGemAddPosDisp(int off_x, int off_y,
                                        u_char alpha)                   /* 1708 */
{
    int lens_label;

    lens_label =
        disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;

    MenuCamEditGemAddCmnDisp(off_x, off_y, alpha);                      /* 1716 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1719 */
        PrintMsg(MENU_CAM_MSG_TYPE,
                 base_msg_tbl[menu_cam_edit_ctrl.edit_sel_csr],
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);
    }
    else if (lens_msg_tbl[lens_label] != -1) {
        PrintMsg(MENU_CAM_MSG_TYPE, lens_msg_tbl[lens_label],
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);
    }
}

static void MenuCamEditGemAddConfDisp(int off_x, int off_y,
                                      u_char alpha)                     /* 1740 */
{
    MenuCamEditGemAddCmnDisp(off_x, off_y, alpha);                      /* 1744 */
    MenuCamEditConfYesNoDisp(off_x, off_y, alpha);                      /* 1747 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1750 */
        PrintMsg(MENU_CAM_MSG_TYPE, MSG_GEM_ADD_CONF_BASE,
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);    /* 1752 */
    }
    else {
        PrintMsg(MENU_CAM_MSG_TYPE, MSG_GEM_ADD_CONF_LENS,
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);
    }
}

static void MenuCamEditGemAddErrorDisp(int off_x, int off_y,
                                       u_char alpha)                    /* 1769 */
{
    MenuCamEditGemAddCmnDisp(off_x, off_y, alpha);                      /* 1773 */

    PrintMsg(MENU_CAM_MSG_TYPE, MSG_GEM_ADD_ERR,
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1776 */
}

static void MenuCamEditGemNothingErrorDisp(int off_x, int off_y,
                                           u_char alpha)                /* 1787 */
{
    MenuCamEditGemAddCmnDisp(off_x, off_y, alpha);                      /* 1791 */

    PrintMsg(MENU_CAM_MSG_TYPE, MSG_GEM_NOTHING,
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1794 */
}

/* The power-up selector's caption names what the NEXT grade would give, so
 * it is `base + row * 3 + current grade` -- three descriptions per row, one
 * per grade the row can still reach.  A row already at the top grade gets
 * the shared "maximum" line instead. */
static void MenuCamEditSelPowerUpPosDisp(int off_x, int off_y,
                                         u_char alpha)                  /* 1805 */
{
    int lens_label;

    lens_label =
        disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.data_pos].lens_label;

    MenuCamEditPowerUpCmnDisp(off_x, off_y, alpha);                     /* 1813 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1816 */
        if (GetMenuCamBasicLv(menu_cam_edit_ctrl.edit_sel_csr)
            == MENU_CAM_LV_MAX) {                                       /* 1818 */
            PrintMsg(MENU_CAM_MSG_TYPE, MSG_LV_MAX,
                     off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0); /* 1820 */
        }
        else {
            PrintMsg(MENU_CAM_MSG_TYPE,
                     MSG_BASIC_DESC_BASE
                         + menu_cam_edit_ctrl.edit_sel_csr * 3
                         + GetMenuCamBasicLv(
                               menu_cam_edit_ctrl.edit_sel_csr),
                     off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0); /* 1824 */
        }
    }
    else {
        if (m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get()
            == MENU_CAM_LV_MAX) {
            PrintMsg(MENU_CAM_MSG_TYPE, MSG_LV_MAX,
                     off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0); /* 1832 */
        }
        else {
            PrintMsg(MENU_CAM_MSG_TYPE,
                     MSG_LENS_DESC_BASE + lens_label * 3
                         + m_plyr_camera.eq_tray.mSave
                               .mSubFuncLv[lens_label].Get(),
                     off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0); /* 1836 */
        }
    }
}

static void MenuCamEditPowerUpConfDisp(int off_x, int off_y,
                                       u_char alpha)                    /* 1849 */
{
    MenuCamEditPowerUpCmnDisp(off_x, off_y, alpha);                     /* 1853 */
    MenuCamEditConfYesNoDisp(off_x, off_y, alpha);                      /* 1856 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 1859 */
        PrintMsg(MENU_CAM_MSG_TYPE, MSG_POWER_UP_CONF_BASE,
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);    /* 1861 */
    }
    else {
        PrintMsg(MENU_CAM_MSG_TYPE, MSG_POWER_UP_CONF_LENS,
                 off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);
    }
}

static void MenuCamEditPowerUpLvMaxErrorDisp(int off_x, int off_y,
                                             u_char alpha)              /* 1878 */
{
    MenuCamEditPowerUpCmnDisp(off_x, off_y, alpha);                     /* 1882 */

    PrintMsg(MENU_CAM_MSG_TYPE, MSG_POWER_UP_LV_MAX,
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1885 */
}

static void MenuCamEditPowerUpNothingErrorDisp(int off_x, int off_y,
                                               u_char alpha)            /* 1896 */
{
    MenuCamEditPowerUpCmnDisp(off_x, off_y, alpha);                     /* 1900 */

    PrintMsg(MENU_CAM_MSG_TYPE, MSG_POWER_UP_NOTHING,
             off_x + MSG_OFF_X, off_y + MSG_OFF_Y, 1, alpha, 0);        /* 1903 */
}

/* --------------------------------------------------------------------------
 *  The shared page
 *
 *  Twenty-two draws in a fixed order, with four PK2SendVram() switches
 *  between the two camera paks threaded through them -- the page is built
 *  from both, and the switches are what decide which one each following
 *  group of sprites resolves its TEX0 against.  The background is only drawn
 *  when the menu was opened from inside the game; Mission Mode's setup
 *  screen has its own behind it already.
 * ------------------------------------------------------------------------ */

static void MenuCamEditCmnDisp(int off_x, int off_y, u_char alpha)      /* 1917 */
{
    if (GetMenuCamInitType() == 0) {                                    /* 1921 */
        PK2SendVram((uintptr_t)GetMenuCameraEdtPk2Addr(), -1, -1, 0);   /* 1922 */

        MenuCamEditBgDisp(off_x, off_y, alpha);                         /* 1925 */
    }

    PK2SendVram((uintptr_t)GetMenuCameraPk2Addr(), -1, -1, 0);          /* 1928 */

    MenuCamEditTitleFrameDisp(off_x, off_y, alpha);                     /* 1931 */
    MenuCamEditMenuFrameDisp(off_x, off_y, alpha);                      /* 1934 */

    PK2SendVram((uintptr_t)GetMenuCameraEdtPk2Addr(), -1, -1, 0);       /* 1936 */

    MenuCamEditTitleDisp(off_x, off_y, alpha);                          /* 1939 */
    MenuCamEditMenuItemDisp(off_x, off_y, alpha);                       /* 1942 */
    MenuCamEditWinDisp(off_x, off_y, alpha);                            /* 1945 */
    MenuCamEditBaseDisp(off_x, off_y, alpha);                           /* 1948 */
    MenuCamEditReinforcedLensDisp(off_x, off_y, alpha);                 /* 1951 */
    MenuCamEditBaseFrameBgDisp(off_x, off_y, alpha);                    /* 1954 */
    MenuCamEditLensFrameBgDisp(off_x, off_y, alpha);                    /* 1957 */

    PK2SendVram((uintptr_t)GetMenuCameraPk2Addr(), -1, -1, 0);          /* 1959 */

    MenuCamEditEquipReinforcedLensDisp(off_x, off_y, alpha);            /* 1962 */

    PK2SendVram((uintptr_t)GetMenuCameraEdtPk2Addr(), -1, -1, 0);       /* 1964 */

    MenuCamEditEquipLensLvDisp(off_x, off_y, alpha);                    /* 1967 */
    MenuCamEditHaveStatusDisp(off_x, off_y, alpha);                     /* 1970 */

    PK2SendVram((uintptr_t)GetMenuCameraEdtPk2Addr(), -1, -1, 0);       /* 1972 */

    MenuCamEditBaseItemDisp(off_x, off_y, alpha);                       /* 1975 */
    MenuCamEditBaseLvDisp(off_x, off_y, alpha);                         /* 1978 */
    MenuCamEditHaveLensNameDisp(off_x, off_y, alpha);                   /* 1981 */
    MenuCamEditLensLvDisp(off_x, off_y, alpha);                         /* 1984 */
    MenuCamEditScrollDisp(off_x, off_y, alpha);                         /* 1987 */
    MenuCamEditMsgWindowDisp(off_x, off_y, alpha);                      /* 1990 */
    MenuCamEditCaptionDisp(off_x, off_y, alpha);                        /* 1993 */

    /* The idle shimmer that runs along the gems.  It is advanced here, on
     * the drawing side, so it keeps time with whatever the page draws. */
    menu_cam_edit_disp.gem_anim_timer++;                                /* 1995 */

    if (menu_cam_edit_disp.gem_anim_timer > GEM_IDLE_ANIM_TIME) {       /* 1997 */
        menu_cam_edit_disp.gem_anim_timer = 0;                          /* 1998 */
    }
}

/* The shared page plus the item selector's own highlight and cursor.  The
 * two are the same function twice -- the gem one and the power-up one differ
 * only in name -- and both place their highlight in whichever half of the
 * combined list the cursor is in. */
static void MenuCamEditGemAddCmnDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 2010 */
{
    MenuCamEditCmnDisp(off_x, off_y, alpha);                            /* 2014 */
    MenuCamEditBaseNextPointDisp(off_x, off_y, alpha);                  /* 2017 */
    MenuCamEditLensNextPointDisp(off_x, off_y, alpha);                  /* 2020 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 2023 */
        MenuCamEditSelFrameBgDisp(
            (int)((float)menu_cam_edit_ctrl.edit_sel_csr * ROW_PITCH
                  + BASE_ROW_TOP), alpha);                              /* 2025 */
    }
    else {
        MenuCamEditSelFrameBgDisp(
            (int)((float)(menu_cam_edit_ctrl.edit_sel_csr
                          - MENU_CAM_BASIC_NUM) * ROW_PITCH
                  + LENS_ROW_TOP), alpha);                              /* 2030 */
    }

    MenuCamEditItemFrameDisp(off_x, off_y, alpha);                      /* 2034 */
    MenuCamEditBaseGemDisp(off_x, off_y, alpha);                        /* 2037 */
    MenuCamEditLensGemDisp(off_x, off_y, alpha);                        /* 2040 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 2043 */
        MenuCamEditSelCsrDisp(
            SEL_CSR_X,
            (float)menu_cam_edit_ctrl.edit_sel_csr * ROW_PITCH
                + BASE_CSR_TOP, alpha);                                 /* 2045 */
    }
    else {
        MenuCamEditSelCsrDisp(
            SEL_CSR_X,
            (float)(menu_cam_edit_ctrl.edit_sel_csr - MENU_CAM_BASIC_NUM)
                * ROW_PITCH + LENS_CSR_TOP, alpha);                     /* 2050 */
    }
}

static void MenuCamEditPowerUpCmnDisp(int off_x, int off_y,
                                      u_char alpha)                    /* 2062 */
{
    MenuCamEditCmnDisp(off_x, off_y, alpha);                            /* 2066 */
    MenuCamEditBaseNextPointDisp(off_x, off_y, alpha);                  /* 2069 */
    MenuCamEditLensNextPointDisp(off_x, off_y, alpha);                  /* 2072 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 2075 */
        MenuCamEditSelFrameBgDisp(
            (int)((float)menu_cam_edit_ctrl.edit_sel_csr * ROW_PITCH
                  + BASE_ROW_TOP), alpha);                              /* 2077 */
    }
    else {
        MenuCamEditSelFrameBgDisp(
            (int)((float)(menu_cam_edit_ctrl.edit_sel_csr
                          - MENU_CAM_BASIC_NUM) * ROW_PITCH
                  + LENS_ROW_TOP), alpha);                              /* 2082 */
    }

    MenuCamEditItemFrameDisp(off_x, off_y, alpha);                      /* 2086 */
    MenuCamEditBaseGemDisp(off_x, off_y, alpha);                        /* 2089 */
    MenuCamEditLensGemDisp(off_x, off_y, alpha);                        /* 2092 */

    if (menu_cam_edit_ctrl.edit_sel_csr < MENU_CAM_BASIC_NUM) {         /* 2095 */
        MenuCamEditSelCsrDisp(
            SEL_CSR_X,
            (float)menu_cam_edit_ctrl.edit_sel_csr * ROW_PITCH
                + BASE_CSR_TOP, alpha);                                 /* 2097 */
    }
    else {
        MenuCamEditSelCsrDisp(
            SEL_CSR_X,
            (float)(menu_cam_edit_ctrl.edit_sel_csr - MENU_CAM_BASIC_NUM)
                * ROW_PITCH + LENS_CSR_TOP, alpha);                     /* 2102 */
    }
}

/* Both lists' frame art.  It comes out of the editor pak, and the page has
 * switched to the other one by the time this runs, so the switch is here
 * rather than at the call site. */
static void MenuCamEditItemFrameDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 2114 */
{
    PK2SendVram((uintptr_t)GetMenuCameraEdtPk2Addr(), -1, -1, 0);       /* 2117 */

    MenuCamEditBaseFrameDisp(off_x, off_y, alpha);                      /* 2120 */
    MenuCamEditLensFrameDisp(off_x, off_y, alpha);                      /* 2123 */
}

/* --------------------------------------------------------------------------
 *  The page furniture
 * ------------------------------------------------------------------------ */

/* The two halves of the title bar, both stretched 1.1x wide. */
static void MenuCamEditTitleFrameDisp(int off_x, int off_y,
                                      u_char alpha)                    /* 2134 */
{
    int       i;
    DISP_SPRT title_ds;

    for (i = 0; i < 2; i++) {                                           /* 2141 */
        CopySprDToSpr(&title_ds, &menu_camera_tex[MCE_TEX_TITLE_FRAME + i]); /* 2142 */

        title_ds.x = title_ds.x + off_x;
        title_ds.y = title_ds.y + off_y;                                /* 2143 */

        title_ds.scw = 1.0999999f;   title_ds.sch = 1.0f;
        title_ds.csx = title_ds.x;   title_ds.csy = title_ds.y;         /* 2144 */

        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 2145 */

        DispSprD(&title_ds);                                            /* 2146 */
    }                                                                   /* 2147 */
}

static void MenuCamEditTitleDisp(int off_x, int off_y, u_char alpha)   /* 2158 */
{
    DISP_SPRT title_ds;

    CopySprDToSpr(&title_ds, &menu_camera_tex[MCE_TEX_TITLE]);          /* 2164 */

    title_ds.x = title_ds.x + off_x;
    title_ds.y = title_ds.y + off_y;                                    /* 2165 */

    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 2166 */

    DispSprD(&title_ds);                                                /* 2167 */
}

static void MenuCamEditBgDisp(int off_x, int off_y, u_char alpha)      /* 2178 */
{
    DISP_SPRT bg_ds;

    CopySprDToSpr(&bg_ds, &menu_camera_tex[MCE_TEX_BG]);                /* 2184 */

    bg_ds.x = bg_ds.x + off_x;
    bg_ds.y = bg_ds.y + off_y;                                          /* 2185 */

    bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);                   /* 2186 */

    DispSprD(&bg_ds);                                                   /* 2187 */
}

/* The three menu rows' frames.  Both helpers ignore off_x / off_y -- the
 * coordinates are literals here -- which is why this is the only place in
 * the file that hands a widget bare numbers. */
static void MenuCamEditMenuFrameDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 2198 */
{
    int i;

    for (i = 0; i < MENU_CAM_EDIT_ROW_NUM; i++) {                       /* 2204 */
        if (i == menu_cam_edit_ctrl.menu_csr) {                         /* 2206 */
            MenuCamEditSelFrameDisp(MENU_ROW_X,
                                    (float)i * MENU_ROW_PITCH
                                        + MENU_ROW_TOP,
                                    MENU_ROW_W, alpha, 0);              /* 2209 */
        }
        else {
            MenuCamEditNonSelFrameDisp(MENU_ROW_X,
                                       (float)i * MENU_ROW_PITCH
                                           + MENU_ROW_TOP,
                                       MENU_ROW_W, alpha, 0);           /* 2214 */
        }
    }                                                                   /* 2216 */
}

/* One row frame is two halves, each stretched to half the asked-for width
 * and butted together -- the same trick menu_cmn_disp.o's row frames use.
 * The `z` is the priority inverted into the GS's range, which is what puts
 * a higher `pri` behind. */
static void MenuCamEditSelFrameDisp(float x, float y, float w, u_char alpha,
                                    u_int pri)                          /* 2229 */
{
    DISP_SPRT frame_ds;
    float     one_size;
    float     frame_scl_l;
    float     frame_scl_r;

    one_size = w * 0.5f;                                                /* 2237 */

    frame_scl_l =
        one_size / (float)menu_camera_tex[MCE_TEX_SEL_FRAME_L].w;       /* 2240 */
    frame_scl_r =
        one_size / (float)menu_camera_tex[MCE_TEX_SEL_FRAME_R].w;       /* 2241 */

    CopySprDToSpr(&frame_ds, &menu_camera_tex[MCE_TEX_SEL_FRAME_L]);    /* 2245 */

    frame_ds.x = x;   frame_ds.y = y;                                   /* 2246 */
    frame_ds.scw = frame_scl_l;   frame_ds.sch = 1.0f;
    frame_ds.csx = x;             frame_ds.csy = y;                     /* 2247 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 2248 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 2249 */

    DispSprD(&frame_ds);                                                /* 2250 */

    CopySprDToSpr(&frame_ds, &menu_camera_tex[MCE_TEX_SEL_FRAME_R]);    /* 2253 */

    frame_ds.x = x + (float)menu_camera_tex[MCE_TEX_SEL_FRAME_L].w
                     * frame_scl_l;
    frame_ds.y = y;                                                     /* 2254 */
    frame_ds.scw = frame_scl_r;   frame_ds.sch = 1.0f;
    frame_ds.csx = frame_ds.x;    frame_ds.csy = y;                     /* 2255 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 2256 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 2257 */

    DispSprD(&frame_ds);                                                /* 2258 */
}

static void MenuCamEditNonSelFrameDisp(float x, float y, float w,
                                       u_char alpha, u_int pri)         /* 2271 */
{
    DISP_SPRT frame_ds;
    float     one_size;
    float     frame_scl_l;
    float     frame_scl_r;

    one_size = w * 0.5f;                                                /* 2279 */

    frame_scl_l =
        one_size / (float)menu_camera_tex[MCE_TEX_NONSEL_FRAME_L].w;    /* 2282 */
    frame_scl_r =
        one_size / (float)menu_camera_tex[MCE_TEX_NONSEL_FRAME_R].w;    /* 2283 */

    CopySprDToSpr(&frame_ds, &menu_camera_tex[MCE_TEX_NONSEL_FRAME_L]); /* 2287 */

    frame_ds.x = x;   frame_ds.y = y;                                   /* 2288 */
    frame_ds.scw = frame_scl_l;   frame_ds.sch = 1.0f;
    frame_ds.csx = x;             frame_ds.csy = y;                     /* 2289 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 2290 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 2291 */

    DispSprD(&frame_ds);                                                /* 2292 */

    CopySprDToSpr(&frame_ds, &menu_camera_tex[MCE_TEX_NONSEL_FRAME_R]); /* 2294 */

    frame_ds.x = x + (float)menu_camera_tex[MCE_TEX_NONSEL_FRAME_L].w
                     * frame_scl_l;
    frame_ds.y = y;                                                     /* 2295 */
    frame_ds.scw = frame_scl_r;   frame_ds.sch = 1.0f;
    frame_ds.csx = frame_ds.x;    frame_ds.csy = y;                     /* 2296 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 2297 */
    frame_ds.pri = pri;   frame_ds.z = 0xfffff - (pri & 0xfffff);       /* 2298 */

    DispSprD(&frame_ds);                                                /* 2299 */
}

/* The three row labels -- a separate lit sprite per row rather than a tint. */
static void MenuCamEditMenuItemDisp(int off_x, int off_y,
                                    u_char alpha)                      /* 2310 */
{
    int       i;
    DISP_SPRT item_ds;

    for (i = 0; i < MENU_CAM_EDIT_ROW_NUM; i++) {                       /* 2317 */
        if (i == menu_cam_edit_ctrl.menu_csr) {                         /* 2319 */
            CopySprDToSpr(&item_ds,
                          &menu_camera_tex[MCE_TEX_MENU_ITEM_ON + i]);  /* 2320 */
        }
        else {
            CopySprDToSpr(&item_ds,
                          &menu_camera_tex[MCE_TEX_MENU_ITEM_OFF + i]); /* 2323 */
        }

        item_ds.x = item_ds.x + off_x;
        item_ds.y = item_ds.y + off_y;                                  /* 2326 */

        item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);           /* 2327 */

        DispSprD(&item_ds);                                             /* 2328 */
    }                                                                   /* 2329 */
}

/* The window panels.  Two things worth noting: the first run is drawn at a
 * flat 0x59/128 of the caller's alpha when the menu was opened from Mission
 * Mode's setup screen (so the game behind shows through less), and the tray
 * plate is one wide sprite with camera part 1 fitted and a small one plus
 * the "equipped" label without it. */
static void MenuCamEditWinDisp(int off_x, int off_y, u_char alpha)      /* 2340 */
{
    int       i;
    DISP_SPRT bg_ds;

    for (i = MCE_TEX_WIN_FIRST; i <= MCE_TEX_WIN_LAST; i++) {           /* 2347 */
        CopySprDToSpr(&bg_ds, &menu_camera_tex[i]);                     /* 2348 */

        bg_ds.x = bg_ds.x + off_x;
        bg_ds.y = bg_ds.y + off_y;                                      /* 2349 */

        if (GetMenuCamInitType() == 0) {                                /* 2352 */
            bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);           /* 2353 */
        }
        else {
            bg_ds.alpha = (u_char)(alpha * 0x59 >> 7);                  /* 2357 */
        }

        DispSprD(&bg_ds);                                               /* 2360 */
    }                                                                   /* 2361 */

    for (i = MCE_TEX_WIN2_FIRST; i <= MCE_TEX_WIN2_LAST; i++) {         /* 2364 */
        CopySprDToSpr(&bg_ds, &menu_camera_tex[i]);                     /* 2365 */

        bg_ds.x = bg_ds.x + off_x;
        bg_ds.y = bg_ds.y + off_y;                                      /* 2366 */

        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 2367 */

        DispSprD(&bg_ds);                                               /* 2368 */
    }                                                                   /* 2369 */

    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
            EQUIP_FUNC_SPECIAL_SLOT)) {
        CopySprDToSpr(&bg_ds, &menu_camera_tex[MCE_TEX_THREE_SLOT]);    /* 2373 */

        bg_ds.x = bg_ds.x + off_x;
        bg_ds.y = bg_ds.y + off_y;                                      /* 2374 */

        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 2375 */

        DispSprD(&bg_ds);                                               /* 2376 */
    }
    else {
        for (i = MCE_TEX_ONE_SLOT_FIRST; i <= MCE_TEX_ONE_SLOT_LAST;
             i++) {                                                     /* 2379 */
            CopySprDToSpr(&bg_ds, &menu_camera_tex[i]);                 /* 2380 */

            bg_ds.x = bg_ds.x + off_x;
            bg_ds.y = bg_ds.y + off_y;                                  /* 2381 */

            bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);           /* 2382 */

            DispSprD(&bg_ds);                                           /* 2383 */
        }                                                               /* 2384 */

        MenuCamEditEquipDisp(off_x, off_y, alpha);                      /* 2387 */
    }
}

/* The camera portrait, keyed by mode: modes 2 and 3 -- the two lens equip
 * screens -- get a different pose from every other mode. */
static void MenuCamEditBaseDisp(int off_x, int off_y, u_char alpha)     /* 2399 */
{
    /* rodata 3bcbc8, a reference_fixed_array constructed on first use. */
    static int base_tex_tbl_data[MENU_CAM_EDIT_MODE_NUM] =
    {
        0xec, 0xec, 0xed, 0xed, 0xec, 0xec,
        0xec, 0xec, 0xec, 0xec, 0xec, 0xec
    };
    static reference_fixed_array<int, MENU_CAM_EDIT_MODE_NUM>
        base_tex_tbl(base_tex_tbl_data);                        /* sbss 3f4db0 */

    DISP_SPRT base_ds;

    CopySprDToSpr(&base_ds,
                  &menu_camera_tex[base_tex_tbl[menu_cam_edit_ctrl.mode]]); /* 2422 */

    base_ds.x = base_ds.x + off_x;
    base_ds.y = base_ds.y + off_y;                                      /* 2423 */

    base_ds.alpha = (u_char)(base_ds.alpha * alpha >> 7);               /* 2424 */

    DispSprD(&base_ds);                                                 /* 2425 */
}

static void MenuCamEditReinforcedLensDisp(int off_x, int off_y,
                                          u_char alpha)                /* 2436 */
{
    DISP_SPRT assist_ds;

    CopySprDToSpr(&assist_ds, &menu_camera_tex[MCE_TEX_ASSIST]);        /* 2441 */

    assist_ds.x = assist_ds.x + off_x;
    assist_ds.y = assist_ds.y + off_y;                                  /* 2442 */

    assist_ds.alpha = (u_char)(assist_ds.alpha * alpha >> 7);           /* 2443 */

    DispSprD(&assist_ds);                                               /* 2444 */
}

static void MenuCamEditEquipDisp(int off_x, int off_y, u_char alpha)   /* 2455 */
{
    DISP_SPRT equip_ds;

    CopySprDToSpr(&equip_ds, &menu_camera_tex[MCE_TEX_EQUIP]);          /* 2461 */

    equip_ds.x = equip_ds.x + off_x;
    equip_ds.y = equip_ds.y + off_y;                                    /* 2462 */

    equip_ds.alpha = (u_char)(equip_ds.alpha * alpha >> 7);             /* 2463 */

    DispSprD(&equip_ds);                                                /* 2464 */
}

/* --------------------------------------------------------------------------
 *  The tray readout
 *
 *  Both of these are the same shape: fetch the three sub-function slots, and
 *  draw either all three (part 1 fitted) or just slot 0.  A slot holding
 *  CAMERA_SUB_FUNC_NONE draws nothing at all.
 * ------------------------------------------------------------------------ */

static void MenuCamEditEquipReinforcedLensDisp(int off_x, int off_y,
                                               u_char alpha)           /* 2475 */
{
    int i;
    fixed_array<char, EQUIP_SPECIAL_NUM> equip_special;

    m_plyr_camera.eq_tray.GetSubFuncArray(&equip_special[0]);           /* 2481 */

    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
            EQUIP_FUNC_SPECIAL_SLOT) == 0) {
        if (equip_special[0] != 0) {
            MenuCamCmnReinforcedLensDisp(269.0f, 60.0f, alpha,
                                         equip_special[0]);             /* 2488 */
        }
    }
    else {
        for (i = 0; i < EQUIP_SPECIAL_NUM; i++) {
            if (equip_special[i] != 0) {
                MenuCamCmnReinforcedLensDisp((float)i * SLOT_PITCH + 197.0f,
                                             46.0f, alpha,
                                             equip_special[i]);         /* 2494 */
            }
        }
    }
}

/* The little level plate beside each fitted lens. */
static void MenuCamEditEquipLensLvDisp(int off_x, int off_y,
                                       u_char alpha)                   /* 2513 */
{
    int       i;
    fixed_array<char, EQUIP_SPECIAL_NUM> equip_special;
    DISP_SPRT lv_ds;

    m_plyr_camera.eq_tray.GetSubFuncArray(&equip_special[0]);           /* 2520 */

    if (m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(
            EQUIP_FUNC_SPECIAL_SLOT) == 0) {
        if (equip_special[0] != 0) {                                    /* 2527 */
            CopySprDToSpr(&lv_ds,
                          &menu_camera_tex[MCE_TEX_LV_NUM
                              + m_plyr_camera.eq_tray.mSave
                                    .mSubFuncLv[equip_special[0]].Get()]);

            lv_ds.x = 308.0f;   lv_ds.y = 83.0f;                        /* 2533 */

            lv_ds.alpha = (u_char)(lv_ds.alpha * alpha >> 7);           /* 2534 */

            DispSprD(&lv_ds);                                           /* 2535 */
        }
    }
    else {
        for (i = 0; i < EQUIP_SPECIAL_NUM; i++) {
            if (equip_special[i] != 0) {
                CopySprDToSpr(&lv_ds,
                              &menu_camera_tex[MCE_TEX_LV_NUM
                                  + m_plyr_camera.eq_tray.mSave
                                        .mSubFuncLv[equip_special[i]]
                                        .Get()]);

                lv_ds.x = (float)i * SLOT_PITCH + 208.0f;
                lv_ds.y = 85.0f;                                        /* 2545 */

                lv_ds.alpha = (u_char)(lv_ds.alpha * alpha >> 7);       /* 2546 */

                DispSprD(&lv_ds);                                       /* 2547 */
            }
        }
    }
}

/* The two right-hand readouts: level gems held, and spirit points. */
static void MenuCamEditHaveStatusDisp(int off_x, int off_y,
                                      u_char alpha)                    /* 2560 */
{
    int       i;
    DISP_SPRT status_ds;

    for (i = MCE_TEX_STATUS_FIRST; i <= MCE_TEX_STATUS_LAST; i++) {     /* 2567 */
        CopySprDToSpr(&status_ds, &menu_camera_tex[i]);                 /* 2568 */

        status_ds.x = status_ds.x + off_x;
        status_ds.y = status_ds.y + off_y;                              /* 2569 */

        status_ds.alpha = (u_char)(status_ds.alpha * alpha >> 7);       /* 2570 */

        DispSprD(&status_ds);                                           /* 2571 */
    }                                                                   /* 2572 */

    MenuCamNumberDisp(GetPlyrLevelGemNum(), 2, 499, 0x3e, alpha, 0,
                      0, 1);                                            /* 2575 */

    MenuCamNumberDisp(GetPlayData_Score(), 6, 0x1d7, 0x51, alpha, 0,
                      0, 0);                                            /* 2578 */
}

/* --------------------------------------------------------------------------
 *  The two three-row lists
 *
 *  Both are laid out identically -- a bed, a frame, a name, a level, a next
 *  cost and a row of gems -- and differ only in where they start and what
 *  they read.
 * ------------------------------------------------------------------------ */

static void MenuCamEditBaseFrameBgDisp(int off_x, int off_y,
                                       u_char alpha)                   /* 2589 */
{
    int i;

    for (i = 0; i < MENU_CAM_EDIT_LIST_DISP_NUM; i++) {                 /* 2595 */
        MenuCamEditFrameBgDisp((float)i * ROW_PITCH + BASE_ROW_TOP,
                               alpha);                                  /* 2596 */
    }                                                                   /* 2597 */
}

static void MenuCamEditLensFrameBgDisp(int off_x, int off_y,
                                       u_char alpha)                   /* 2608 */
{
    int i;

    for (i = 0; i < MENU_CAM_EDIT_LIST_DISP_NUM; i++) {                 /* 2633 */
        MenuCamEditFrameBgDisp((float)i * ROW_PITCH + LENS_ROW_TOP,
                               alpha);                                  /* 2634 */
    }                                                                   /* 2635 */
}

/* Three rows of five sprites.  Two of the five in each row are stretched --
 * the second to 80 pixels and the fourth to 161 -- and the ROM writes the
 * scale block out in both arms of the switch, which GCC then cross-jumped
 * after the divide. */
static void MenuCamEditBaseFrameDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 2646 */
{
    int       i;
    DISP_SPRT frame_ds;

    for (i = MCE_TEX_BASE_FRAME_FIRST; i <= MCE_TEX_BASE_FRAME_LAST;
         i++) {                                                         /* 2653 */
        CopySprDToSpr(&frame_ds, &menu_camera_tex[i]);                  /* 2654 */

        frame_ds.x = frame_ds.x + off_x;
        frame_ds.y = frame_ds.y + off_y;                                /* 2656 */

        switch (i) {                                                    /* 2658 */

        case 0xc2:
        case 0xc7:
        case 0xcc:
            frame_ds.scw = 80.0f / (float)frame_ds.w;                   /* 2662 */
            frame_ds.sch = 1.0f;
            frame_ds.csx = frame_ds.x;   frame_ds.csy = frame_ds.y;     /* 2663 */
            break;

        case 0xc4:
        case 0xc9:
        case 0xce:
            frame_ds.scw = 161.0f / (float)frame_ds.w;                  /* 2667 */
            frame_ds.sch = 1.0f;
            frame_ds.csx = frame_ds.x;   frame_ds.csy = frame_ds.y;
            break;
        }

        frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);         /* 2671 */

        DispSprD(&frame_ds);                                            /* 2672 */
    }                                                                   /* 2673 */
}

static void MenuCamEditBaseItemDisp(int off_x, int off_y,
                                    u_char alpha)                      /* 2684 */
{
    int       i;
    DISP_SPRT item_ds;

    for (i = MCE_TEX_BASE_ITEM_FIRST; i <= MCE_TEX_BASE_ITEM_LAST;
         i++) {                                                         /* 2691 */
        CopySprDToSpr(&item_ds, &menu_camera_tex[i]);                   /* 2692 */

        item_ds.x = item_ds.x + off_x;
        item_ds.y = item_ds.y + off_y;                                  /* 2693 */

        item_ds.alpha = (u_char)(item_ds.alpha * alpha >> 7);           /* 2694 */

        DispSprD(&item_ds);                                             /* 2695 */
    }                                                                   /* 2696 */
}

/* The three grades.  Note the stock grade is the equip tray's, not the
 * camera's -- the same split GetMenuCamBasicLv() has. */
static void MenuCamEditBaseLvDisp(int off_x, int off_y, u_char alpha)  /* 2707 */
{
    MenuCamEditLvDisp(135.0f, alpha,
                      m_plyr_camera.camera_power_up.mRadiusGrade.Get()); /* 2711 */
    MenuCamEditLvDisp(167.0f, alpha,
                      m_plyr_camera.eq_tray.mSave.mStockGrade.Get());   /* 2714 */
    MenuCamEditLvDisp(199.0f, alpha,
                      m_plyr_camera.camera_power_up.mSensitiveGrade
                          .Get());                                      /* 2717 */
}

/* What the next grade of each basic performance costs.  A row already at the
 * top grade prints nothing. */
static void MenuCamEditBaseNextPointDisp(int off_x, int off_y,
                                         u_char alpha)                 /* 2728 */
{
    if (m_plyr_camera.camera_power_up.mRadiusGrade.Get()
        != m_plyr_camera.camera_power_up.mRadiusGrade.GetMax()) {
        MenuCamEditNextPointDisp(
            NEXT_PTS_X, 0x88, alpha, MCE_COL_NORMAL,
            cam_base_status_point[MENU_CAM_BASIC_RADIUS]
                [m_plyr_camera.camera_power_up.mRadiusGrade.Get() + 1]); /* 2735 */
    }

    if (m_plyr_camera.eq_tray.mSave.mStockGrade.Get()
        != m_plyr_camera.eq_tray.mSave.mStockGrade.GetMax()) {
        MenuCamEditNextPointDisp(
            NEXT_PTS_X, 0xa8, alpha, MCE_COL_NORMAL,
            cam_base_status_point[MENU_CAM_BASIC_STOCK]
                [m_plyr_camera.eq_tray.mSave.mStockGrade.Get() + 1]);    /* 2742 */
    }

    if (m_plyr_camera.camera_power_up.mSensitiveGrade.Get()
        != m_plyr_camera.camera_power_up.mSensitiveGrade.GetMax()) {
        MenuCamEditNextPointDisp(
            NEXT_PTS_X, 0xc8, alpha, MCE_COL_NORMAL,
            cam_base_status_point[MENU_CAM_BASIC_SENSITIVE]
                [m_plyr_camera.camera_power_up.mSensitiveGrade.Get()
                 + 1]);                                                 /* 2749 */
    }
}

/* Six runs of gems, two per row: the empty face for gems banked and the
 * filled face for grades already spent, drawn over the top of them.  Then
 * the award animation, if one is running on this list -- and it is the
 * *drawing* side that retires it and unfreezes the page. */
static void MenuCamEditBaseGemDisp(int off_x, int off_y, u_char alpha) /* 2761 */
{
    int i;

    for (i = 0; i < m_plyr_camera.camera_power_up.mRadiusGem.Get(); i++) {
        MenuCamEditGemDisp((float)i * GEM_PITCH + GEM_X,
                           BASE_GEM_ROW_TOP, alpha, 0);                 /* 2769 */
    }                                                                   /* 2770 */

    for (i = 0; i < m_plyr_camera.camera_power_up.mRadiusGrade.Get();
         i++) {
        MenuCamEditGemDisp((float)i * GEM_PITCH + GEM_X,
                           BASE_GEM_ROW_TOP, alpha, 1);                 /* 2773 */
    }                                                                   /* 2774 */

    for (i = 0; i < m_plyr_camera.camera_power_up.mAccumGem.Get(); i++) {
        MenuCamEditGemDisp((float)i * GEM_PITCH + GEM_X,
                           BASE_GEM_ROW_TOP + ROW_PITCH, alpha, 0);     /* 2779 */
    }                                                                   /* 2780 */

    for (i = 0; i < m_plyr_camera.eq_tray.mSave.mStockGrade.Get(); i++) {
        MenuCamEditGemDisp((float)i * GEM_PITCH + GEM_X,
                           BASE_GEM_ROW_TOP + ROW_PITCH, alpha, 1);     /* 2783 */
    }                                                                   /* 2784 */

    for (i = 0; i < m_plyr_camera.camera_power_up.mSensiteiveGem.Get();
         i++) {
        MenuCamEditGemDisp((float)i * GEM_PITCH + GEM_X,
                           BASE_GEM_ROW_TOP + ROW_PITCH * 2.0f,
                           alpha, 0);                                   /* 2789 */
    }                                                                   /* 2790 */

    for (i = 0; i < m_plyr_camera.camera_power_up.mSensitiveGrade.Get();
         i++) {
        MenuCamEditGemDisp((float)i * GEM_PITCH + GEM_X,
                           BASE_GEM_ROW_TOP + ROW_PITCH * 2.0f,
                           alpha, 1);                                   /* 2793 */
    }                                                                   /* 2794 */

    if (base_gem_anim_ctrl.data_pos != -1) {                            /* 2797 */
        for (i = 0; i < MENU_CAM_BASIC_NUM; i++) {                      /* 2799 */
            if (base_gem_anim_ctrl.data_pos == i) {                     /* 2800 */
                MenuCamEditGemAnimDisp(
                    (float)base_gem_anim_ctrl.lv * GEM_PITCH + GEM_X,
                    (float)i * ROW_PITCH + BASE_GEM_ROW_TOP,
                    alpha, base_gem_anim_ctrl.timer);                   /* 2802 */

                base_gem_anim_ctrl.timer++;                             /* 2804 */

                if (base_gem_anim_ctrl.timer > GEM_AWARD_ANIM_TIME) {   /* 2805 */
                    memset(&base_gem_anim_ctrl, -1,
                           sizeof(GEM_ANIM_CTRL));                      /* 2807 */

                    menu_cam_edit_ctrl.gem_anim_flg = 0;                /* 2808 */
                }
            }
        }                                                               /* 2811 */
    }
}

/* The lens list's frames, three rows of five like the basic list's -- the
 * only difference is that its runs come out of a table because the three
 * rows are not contiguous in the sheet. */
static void MenuCamEditLensFrameDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 2823 */
{
    static int frame_tex_tbl[MENU_CAM_EDIT_LIST_DISP_NUM][2] = /* rodata 3bcc38 */
    {
        { 0xd0, 0xd4 },
        { 0xd5, 0xd9 },
        { 0xda, 0xde },
    };

    int       i;
    int       j;
    DISP_SPRT frame_ds;

    for (i = 0; i < MENU_CAM_EDIT_LIST_DISP_NUM; i++) {                 /* 2836 */
        for (j = frame_tex_tbl[i][0]; j <= frame_tex_tbl[i][1]; j++) {  /* 2837 */
            CopySprDToSpr(&frame_ds, &menu_camera_tex[j]);              /* 2838 */

            frame_ds.x = frame_ds.x + off_x;
            frame_ds.y = frame_ds.y + off_y;                            /* 2840 */

            switch (j) {                                                /* 2842 */

            case 0xd1:
            case 0xd6:
            case 0xdb:
                frame_ds.scw = 80.0f / (float)frame_ds.w;               /* 2846 */
                frame_ds.sch = 1.0f;
                frame_ds.csx = frame_ds.x;   frame_ds.csy = frame_ds.y; /* 2847 */
                break;

            case 0xd3:
            case 0xd8:
            case 0xdd:
                frame_ds.scw = 161.0f / (float)frame_ds.w;              /* 2851 */
                frame_ds.sch = 1.0f;
                frame_ds.csx = frame_ds.x;   frame_ds.csy = frame_ds.y;
                break;
            }

            frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);     /* 2855 */

            DispSprD(&frame_ds);                                        /* 2856 */
        }                                                               /* 2857 */
    }                                                                   /* 2858 */
}

static void MenuCamEditHaveLensNameDisp(int off_x, int off_y,
                                        u_char alpha)                  /* 2869 */
{
    int i;
    int disp_num;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 2874 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 2876 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 2881 */
        MenuCamEditLensNameDisp(
            LENS_NAME_X, (float)i * ROW_PITCH + LENS_NAME_TOP, alpha,
            disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.disp_start_pos + i]
                .lens_label);                                           /* 2884 */
    }
}

/* One lens's name plate.  Slot 0 (NONE) has no plate and draws nothing --
 * the -1 test is provision for it, and it is the only entry that has one. */
static void MenuCamEditLensNameDisp(float x, float y, u_char alpha,
                                    int lens_label)                     /* 2896 */
{
    /* rodata 3bcc88, a reference_fixed_array constructed on first use. */
    static int lens_name_tbl_data[REINFORCED_LENS_NUM] =
    {
        -1, 0x122, 0x123, 0x124, 0x125,
        0x126, 0x127, 0x128, 0x129, 0x12a
    };
    static reference_fixed_array<int, REINFORCED_LENS_NUM>
        lens_name_tbl(lens_name_tbl_data);                      /* sbss 3f4db8 */

    DISP_SPRT lens_ds;

    if (lens_name_tbl[lens_label] != -1) {                              /* 2913 */
        CopySprDToSpr(&lens_ds,
                      &menu_camera_tex[lens_name_tbl[lens_label]]);

        lens_ds.alpha = (u_char)(lens_ds.alpha * alpha >> 7);           /* 2920 */

        lens_ds.x = x;   lens_ds.y = y;                                 /* 2921 */

        DispSprD(&lens_ds);                                             /* 2922 */
    }
}

static void MenuCamEditLensLvDisp(int off_x, int off_y, u_char alpha)  /* 2934 */
{
    int i;
    int disp_num;
    int lens_label;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 2940 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 2942 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 2947 */
        lens_label =
            disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.disp_start_pos + i]
                .lens_label;

        MenuCamEditLvDisp(
            (float)i * ROW_PITCH + LENS_NAME_TOP, alpha,
            m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get());  /* 2953 */
    }
}

static void MenuCamEditLensNextPointDisp(int off_x, int off_y,
                                         u_char alpha)                 /* 2964 */
{
    int i;
    int disp_num;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 2970 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 2972 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 2978 */
        MenuCamEditOneLensNextPointDisp(
            disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.disp_start_pos + i]
                .lens_label,
            NEXT_PTS_X, LENS_NEXT_PTS_TOP + i * 32, alpha,
            MCE_COL_NORMAL);                                            /* 2982 */
    }                                                                   /* 2983 */
}

static void MenuCamEditOneLensNextPointDisp(int lens_label, int x, int y,
                                            u_char alpha,
                                            int col_label)              /* 2996 */
{
    if (m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get()
        != m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].GetMax()) {
        MenuCamEditNextPointDisp(
            x, y, alpha, col_label,
            cam_sp_shot_point_tbl[lens_label]
                [m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get()
                 + 1]);                                                 /* 3001 */
    }
}

/* The lens list's gems.  Same two runs per row as the basic list, plus the
 * award animation -- but here the retire test is outside the row loop,
 * because a running animation belongs to a lens rather than a row and the
 * row it is on may have scrolled out of the window. */
static void MenuCamEditLensGemDisp(int off_x, int off_y, u_char alpha) /* 3013 */
{
    int i;
    int j;
    int disp_num;
    int lens_label;

    disp_num = menu_cam_edit_ctrl.ref_ctrl.data_num;                    /* 3019 */

    if (disp_num > MENU_CAM_EDIT_LIST_DISP_NUM) {                       /* 3021 */
        disp_num = MENU_CAM_EDIT_LIST_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 3026 */
        lens_label =
            disp_lens_data[menu_cam_edit_ctrl.ref_ctrl.disp_start_pos + i]
                .lens_label;

        for (j = 0;
             j < m_plyr_camera.camera_power_up.mSubFuncGem[lens_label]
                     .Get();
             j++) {
            MenuCamEditGemDisp((float)j * GEM_PITCH + GEM_X,
                               (float)i * ROW_PITCH + LENS_GEM_ROW_TOP,
                               alpha, 0);                               /* 3032 */
        }                                                               /* 3033 */

        for (j = 0;
             j < m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens_label].Get();
             j++) {                                                     /* 3036 */
            MenuCamEditGemDisp((float)j * GEM_PITCH + GEM_X,
                               (float)i * ROW_PITCH + LENS_GEM_ROW_TOP,
                               alpha, 1);                               /* 3038 */
        }                                                               /* 3039 */

        if (lens_gem_anim_ctrl.data_pos != -1) {                        /* 3042 */
            if (lens_label == lens_gem_anim_ctrl.data_pos) {            /* 3043 */
                MenuCamEditGemAnimDisp(
                    (float)lens_gem_anim_ctrl.lv * GEM_PITCH + GEM_X,
                    (float)i * ROW_PITCH + LENS_GEM_ROW_TOP,
                    alpha, lens_gem_anim_ctrl.timer);                   /* 3045 */
            }
        }
    }                                                                   /* 3048 */

    if (lens_gem_anim_ctrl.data_pos != -1) {                            /* 3052 */
        lens_gem_anim_ctrl.timer++;                                     /* 3054 */

        if (lens_gem_anim_ctrl.timer > GEM_AWARD_ANIM_TIME) {           /* 3056 */
            memset(&lens_gem_anim_ctrl, -1, sizeof(GEM_ANIM_CTRL));     /* 3058 */

            menu_cam_edit_ctrl.gem_anim_flg = 0;                        /* 3060 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  The leaf widgets
 * ------------------------------------------------------------------------ */

/* The four-piece frame around the selected tray slot, drawn additively. */
static void MenuCamEditSelEquipLensPosFrameDisp(float x, float y,
                                                u_char alpha)          /* 3071 */
{
    int       i;
    DISP_SPRT frame_ds;

    for (i = MCE_TEX_SLOT_FRAME_FIRST; i <= MCE_TEX_SLOT_FRAME_LAST;
         i++) {                                                         /* 3077 */
        CopySprDToSpr(&frame_ds, &menu_camera_tex[i]);                  /* 3078 */

        frame_ds.x = frame_ds.x + x;
        frame_ds.y = frame_ds.y + y;                                    /* 3079 */

        frame_ds.alphar = 0x48;                                         /* 3080 */

        frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);         /* 3081 */

        DispSprD(&frame_ds);                                            /* 3082 */
    }                                                                   /* 3083 */
}

/* The two arrow heads either side of the slot cursor.  `rgb` is
 * Zero2Anim2D_CsrAnimCtrl()'s pulse, applied to all three channels. */
static void MenuCamEditEquipLensPosCsrDisp(float x, float y, u_char alpha,
                                           u_char rgb)                  /* 3095 */
{
    DISP_SPRT csr_ds;

    CopySprDToSpr(&csr_ds, &menu_camera_tex[MCE_TEX_SLOT_CSR_L]);       /* 3100 */

    csr_ds.alphar = 0x48;                                               /* 3101 */
    csr_ds.alpha  = (u_char)(csr_ds.alpha * alpha >> 7);                /* 3102 */
    csr_ds.x = x;   csr_ds.y = y;                                       /* 3103 */
    csr_ds.r = rgb;   csr_ds.g = rgb;   csr_ds.b = rgb;                 /* 3104 */

    DispSprD(&csr_ds);                                                  /* 3105 */

    CopySprDToSpr(&csr_ds, &menu_camera_tex[MCE_TEX_SLOT_CSR_R]);       /* 3107 */

    csr_ds.alphar = 0x48;                                               /* 3108 */
    csr_ds.alpha  = (u_char)(csr_ds.alpha * alpha >> 7);                /* 3109 */
    csr_ds.x = x + 60.0f;   csr_ds.y = y;                               /* 3110 */
    csr_ds.r = rgb;   csr_ds.g = rgb;   csr_ds.b = rgb;                 /* 3111 */

    DispSprD(&csr_ds);                                                  /* 3112 */
}

/* One gem.  `flg` picks the empty face from the filled one. */
static void MenuCamEditGemDisp(float x, float y, u_char alpha,
                               u_char flg)                              /* 3128 */
{
    DISP_SPRT gem_ds;

    if (flg == 0) {                                                     /* 3134 */
        CopySprDToSpr(&gem_ds, &menu_camera_tex[MCE_TEX_GEM_EMPTY]);    /* 3135 */
    }
    else {
        CopySprDToSpr(&gem_ds, &menu_camera_tex[MCE_TEX_GEM_FULL]);     /* 3138 */
    }

    gem_ds.alpha = (u_char)(gem_ds.alpha * alpha >> 7);                 /* 3141 */

    gem_ds.x = x;   gem_ds.y = y;                                       /* 3142 */

    DispSprD(&gem_ds);                                                  /* 3143 */
}

/* The gem award: five sprites over one 50-frame envelope, all read out of
 * the five tables below.  The order matters -- the empty gem first, then the
 * filled one fading up under it, then the two flares, then the filled gem
 * again on top additively, which is what makes the gem look like it is lit
 * from inside rather than merely drawn brighter.
 *
 * `under_gem_alpha` and `yellow_flare_alpha` are the port's names: the ROM's
 * stabs list only three of the five locals here, and those two are the ones
 * they omit. */
static void MenuCamEditGemAnimDisp(float x, float y, u_char alpha,
                                   short int timer)                     /* 3155 */
{
    static ALPHA_ANIM_TBL top_gem_anim_tbl[4] =             /* rodata 3bccb0 */
    {
        {   0,   0,  0, 25 },
        {   0, 128, 25, 35 },
        { 128,   0, 35, 50 },
        {  -1,  -1, -1, -1 },
    };

    static ALPHA_ANIM_TBL blue_flare_alpha_anim_tbl[5] =    /* rodata 3bccd0 */
    {
        {   0,   0,  0,  8 },
        {   0, 128,  8, 18 },
        { 128,   0, 18, 32 },
        {   0,   0, 32, 50 },
        {  -1,  -1, -1, -1 },
    };

    static SCL_ANIM_TBL blue_flare_scl_anim_tbl[4] =        /* rodata 3bccf8 */
    {
        { 0.0f, 0.0f,  0,  8 },
        { 0.0f, 3.0f,  8, 32 },
        { 3.0f, 3.0f, 32, 50 },
        { -1.0f, -1.0f, -1, -1 },
    };

    static ALPHA_ANIM_TBL yellow_flare_alpha_anim_tbl[4] =  /* rodata 3bcd28 */
    {
        {   0, 128,  0, 25 },
        { 128,   0, 25, 40 },
        {   0,   0, 40, 50 },
        {  -1,  -1, -1, -1 },
    };

    static ALPHA_ANIM_TBL under_gem_anim_tbl[4] =           /* rodata 3bcd48 */
    {
        {   0,   0,  0, 13 },
        {   0, 128, 13, 23 },
        { 128, 128, 23, 50 },
        {  -1,  -1, -1, -1 },
    };

    DISP_SPRT gem_ds;
    u_char    top_gem_alpha;
    u_char    under_gem_alpha;
    u_char    blue_flare_alpha;
    u_char    yellow_flare_alpha;
    float     blue_flare_scl;

    top_gem_alpha      = Anim2D_CalcNowAlpha(top_gem_anim_tbl, timer);  /* 3206 */
    under_gem_alpha    = Anim2D_CalcNowAlpha(under_gem_anim_tbl, timer); /* 3207 */
    blue_flare_alpha   = Anim2D_CalcNowAlpha(blue_flare_alpha_anim_tbl,
                                             timer);                    /* 3209 */
    yellow_flare_alpha = Anim2D_CalcNowAlpha(yellow_flare_alpha_anim_tbl,
                                             timer);                    /* 3210 */
    blue_flare_scl     = Anim2D_CalcNowScale(blue_flare_scl_anim_tbl,
                                             timer);                    /* 3212 */

    CopySprDToSpr(&gem_ds, &menu_camera_tex[MCE_TEX_GEM_EMPTY]);        /* 3215 */

    gem_ds.alpha = (u_char)(gem_ds.alpha * alpha >> 7);                 /* 3216 */
    gem_ds.x = x;   gem_ds.y = y;                                       /* 3217 */

    DispSprD(&gem_ds);                                                  /* 3218 */

    CopySprDToSpr(&gem_ds, &menu_camera_tex[MCE_TEX_GEM_FULL]);         /* 3220 */

    gem_ds.alpha = (u_char)(gem_ds.alpha * under_gem_alpha / 128
                            * alpha / 128);                             /* 3221 */
    gem_ds.x = x;   gem_ds.y = y;                                       /* 3222 */

    DispSprD(&gem_ds);                                                  /* 3223 */

    CopySprDToSpr(&gem_ds,
                  &menu_camera_tex[MCE_TEX_GEM_YELLOW_FLARE]);          /* 3226 */

    gem_ds.x = x - 16.0f;   gem_ds.y = y - 16.0f;                       /* 3227 */
    gem_ds.alpha = (u_char)(gem_ds.alpha * yellow_flare_alpha / 128
                            * alpha / 128);                             /* 3228 */

    DispSprD(&gem_ds);                                                  /* 3229 */

    CopySprDToSpr(&gem_ds, &menu_camera_tex[MCE_TEX_GEM_BLUE_FLARE]);   /* 3232 */

    gem_ds.x = x - 4.0f;   gem_ds.y = y - 5.0f;                         /* 3233 */
    gem_ds.csx = gem_ds.x + (float)gem_ds.w * 0.5f;
    gem_ds.csy = gem_ds.y + (float)gem_ds.h * 0.5f;                     /* 3234 */
    gem_ds.alpha = (u_char)(gem_ds.alpha * blue_flare_alpha / 128
                            * alpha / 128);                             /* 3235 */
    gem_ds.alphar = 0x48;
    gem_ds.scw = blue_flare_scl;   gem_ds.sch = blue_flare_scl;         /* 3236 */

    DispSprD(&gem_ds);                                                  /* 3237 */

    CopySprDToSpr(&gem_ds, &menu_camera_tex[MCE_TEX_GEM_FULL]);         /* 3239 */

    gem_ds.alpha = (u_char)(gem_ds.alpha * top_gem_alpha / 128
                            * alpha / 128);                             /* 3240 */
    gem_ds.alphar = 0x48;                                               /* 3241 */
    gem_ds.x = x;   gem_ds.y = y;                                       /* 3242 */

    DispSprD(&gem_ds);                                                  /* 3243 */
}

/* The "Lv" plate and its digit.  Neither carries an x -- the sprites' own
 * coordinates place them and only the row's y varies. */
static void MenuCamEditLvDisp(float y, u_char alpha, int lv)            /* 3254 */
{
    DISP_SPRT lv_ds;

    CopySprDToSpr(&lv_ds, &menu_camera_tex[MCE_TEX_LV_PLATE]);          /* 3260 */

    lv_ds.alpha = (u_char)(lv_ds.alpha * alpha >> 7);                   /* 3261 */
    lv_ds.y = y;                                                        /* 3262 */

    DispSprD(&lv_ds);                                                   /* 3263 */

    CopySprDToSpr(&lv_ds, &menu_camera_tex[MCE_TEX_LV_DIGIT + lv]);     /* 3265 */

    lv_ds.alpha = (u_char)(lv_ds.alpha * alpha >> 7);                   /* 3266 */
    lv_ds.y = y;                                                        /* 3267 */

    DispSprD(&lv_ds);                                                   /* 3268 */
}

/* What the next grade costs, plus the little "next" plate after it.  The
 * plate comes in the same two colours the number does, picked off the same
 * col_label. */
static void MenuCamEditNextPointDisp(int x, int y, u_char alpha,
                                     int col_label, int next)           /* 3280 */
{
    DISP_SPRT pts_ds;

    PrintNumber_N(next, 6, x, y, (u_char)col_label, alpha, 0, 1, 0);    /* 3285 */

    if (col_label == MCE_COL_NORMAL) {                                  /* 3288 */
        CopySprDToSpr(&pts_ds, &menu_camera_tex[MCE_TEX_NEXT_PTS_B]);   /* 3289 */

        pts_ds.y = (float)(y + 7);                                      /* 3290 */

        pts_ds.alpha = (u_char)(pts_ds.alpha * alpha >> 7);             /* 3291 */

        DispSprD(&pts_ds);                                              /* 3292 */
    }
    else {
        CopySprDToSpr(&pts_ds, &menu_camera_tex[MCE_TEX_NEXT_PTS_A]);   /* 3295 */

        pts_ds.y = (float)(y + 7);                                      /* 3296 */

        pts_ds.alpha = (u_char)(pts_ds.alpha * alpha >> 7);             /* 3297 */

        DispSprD(&pts_ds);                                              /* 3298 */
    }
}

/* The highlighted row's bed: one 391x27 quad slid to (192, y).  The ROM
 * writes all eight corner updates on ONE source line, so this was either a
 * macro or a deliberately-packed group; only the whole of it is line 3321. */
static void MenuCamEditSelFrameBgDisp(int y, u_char alpha)              /* 3312 */
{
    DISP_SQAR dsq;

    SQAR_DAT sel_bg = { ROW_BG_W, ROW_BG_H, 0, 0, 0,
                        0xff, 0x72, 0x06, 0x0f };                       /* 3314 */

    CopySqrDToSqr(&dsq, &sel_bg);                                       /* 3320 */

    dsq.x[1] = ROW_BG_X + (dsq.x[1] - dsq.x[0]);   dsq.x[0] = ROW_BG_X;
    dsq.x[3] = ROW_BG_X + (dsq.x[3] - dsq.x[2]);   dsq.x[2] = ROW_BG_X;
    dsq.y[2] = y + (dsq.y[2] - dsq.y[0]);          dsq.y[0] = y;
    dsq.y[3] = y + (dsq.y[3] - dsq.y[1]);          dsq.y[1] = y;        /* 3321 */

    dsq.alpha  = (u_char)(dsq.alpha * alpha >> 7);                      /* 3322 */
    dsq.alphar = 0x48;                                                  /* 3323 */

    DispSqrD(&dsq);                                                     /* 3324 */
}

/* The same quad in near-black, used to take the list rows away while the
 * tray cursor is up.  0x46 rather than 0x48 -- this one covers, it does not
 * add. */
static void MenuCamEditFrameBlackBgDisp(int y, u_char alpha)            /* 3334 */
{
    DISP_SQAR dsq;

    SQAR_DAT non_sel_bg = { ROW_BG_W, ROW_BG_H, 0, 0, 0,
                            0x03, 0x01, 0x02, 0x4c };                   /* 3336 */

    CopySqrDToSqr(&dsq, &non_sel_bg);                                   /* 3342 */

    dsq.x[1] = ROW_BG_X + (dsq.x[1] - dsq.x[0]);   dsq.x[0] = ROW_BG_X;
    dsq.x[3] = ROW_BG_X + (dsq.x[3] - dsq.x[2]);   dsq.x[2] = ROW_BG_X;
    dsq.y[2] = y + (dsq.y[2] - dsq.y[0]);          dsq.y[0] = y;
    dsq.y[3] = y + (dsq.y[3] - dsq.y[1]);          dsq.y[1] = y;        /* 3343 */

    dsq.alpha  = (u_char)(dsq.alpha * alpha >> 7);                      /* 3344 */
    dsq.alphar = 0x46;                                                  /* 3345 */

    DispSqrD(&dsq);                                                     /* 3346 */
}

/* One list row's bed art -- five sprites that only need their y setting. */
static void MenuCamEditFrameBgDisp(float y, u_char alpha)              /* 3356 */
{
    int       i;
    DISP_SPRT bg_ds;

    for (i = MCE_TEX_ROW_BG_FIRST; i <= MCE_TEX_ROW_BG_LAST; i++) {     /* 3363 */
        CopySprDToSpr(&bg_ds, &menu_camera_tex[i]);                     /* 3364 */

        bg_ds.alphar = 0x48;                                            /* 3365 */

        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 3366 */

        bg_ds.y = y;                                                    /* 3367 */

        DispSprD(&bg_ds);                                               /* 3368 */
    }                                                                   /* 3369 */
}

/* The row cursor: five sprites laid end to end, two of them stretched to a
 * fixed width and the rest taking their own.  `pos_x` walks along as each
 * one is placed, which is why the two stretched cases have to add their
 * stretched width rather than the sprite's. */
static void MenuCamEditSelCsrDisp(float x, float y, u_char alpha)       /* 3429 */
{
    int       i;
    float     pos_x;
    DISP_SPRT csr_ds;

    pos_x = x;                                                          /* 3435 */

    for (i = MCE_TEX_SEL_CSR_FIRST; i <= MCE_TEX_SEL_CSR_LAST; i++) {   /* 3439 */
        CopySprDToSpr(&csr_ds, &menu_camera_tex[i]);                    /* 3440 */

        csr_ds.x = pos_x;   csr_ds.y = y;                               /* 3441 */

        if (i == MCE_TEX_SEL_CSR_FIRST + 1) {                           /* 3443 */
            csr_ds.scw = 88.0f / (float)csr_ds.w;                       /* 3445 */
            csr_ds.sch = 1.0f;
            csr_ds.csx = pos_x;   csr_ds.csy = y;
            pos_x = pos_x + 88.0f;                                      /* 3447 */
        }
        else if (i == MCE_TEX_SEL_CSR_FIRST + 3) {
            csr_ds.scw = 174.0f / (float)csr_ds.w;                      /* 3449 */
            csr_ds.sch = 1.0f;
            csr_ds.csx = pos_x;   csr_ds.csy = y;
            pos_x = pos_x + 174.0f;                                     /* 3450 */
        }
        else {
            pos_x = pos_x + (float)csr_ds.w;                            /* 3453 */
        }

        csr_ds.alpha  = (u_char)(csr_ds.alpha * alpha >> 7);            /* 3456 */
        csr_ds.alphar = 0x48;                                           /* 3457 */

        DispSprD(&csr_ds);                                              /* 3458 */
    }                                                                   /* 3459 */
}

/* The lens list's scrollbar.  The knob is three pieces -- a fixed cap at
 * each end and a stretched middle -- and it has two shapes: with room for a
 * middle piece it is drawn in three, and without one the two caps are simply
 * scaled to half the knob each.  Everything is rotated 270 degrees, so the
 * sprites' *widths* run down the screen and the y arithmetic uses `w`. */
static void MenuCamEditScrollDisp(int off_x, int off_y, u_char alpha)   /* 3470 */
{
    int       i;
    DISP_SPRT scroll_ds;
    float     scroll_size;
    float     scroll_y;
    float     center_size;
    float     scroll_scl;
    u_char    rgb;

    Zero2Anim2D_CsrAnimCtrl(&menu_cam_edit_disp.scroll_anim_timer,
                            &rgb);                                      /* 3481 */

    if (menu_cam_edit_ctrl.ref_ctrl.data_num
        < MENU_CAM_EDIT_LIST_DISP_NUM) {                                /* 3485 */
        scroll_size = SCROLL_RAIL_LEN;                                  /* 3486 */
        scroll_y    = SCROLL_TOP;                                       /* 3487 */
    }
    else {
        scroll_size = (SCROLL_RAIL_LEN
                       / (float)menu_cam_edit_ctrl.ref_ctrl.data_num)
                      * (float)MENU_CAM_EDIT_LIST_DISP_NUM;             /* 3490 */

        scroll_y = (SCROLL_RAIL_LEN
                    / (float)menu_cam_edit_ctrl.ref_ctrl.data_num)
                   * (float)menu_cam_edit_ctrl.ref_ctrl.disp_start_pos
                   + SCROLL_TOP;                                        /* 3491 */
    }

    center_size = scroll_size
                  - (float)(menu_camera_tex[MCE_TEX_SCROLL_KNOB_TOP].w
                            + menu_camera_tex[MCE_TEX_SCROLL_KNOB_BTM].w); /* 3493 */

    CopySprDToSpr(&scroll_ds,
                  &menu_camera_tex[MCE_TEX_SCROLL_BAR]);                /* 3497 */

    scroll_ds.crx = scroll_ds.x + off_x;
    scroll_ds.cry = scroll_ds.y + (float)scroll_ds.w + off_y;           /* 3498 */
    scroll_ds.rot = 270.0f;                                             /* 3499 */
    scroll_ds.alphar = 0x48;                                            /* 3500 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 3501 */
    scroll_ds.x = scroll_ds.crx;   scroll_ds.y = scroll_ds.cry;

    DispSprD(&scroll_ds);                                               /* 3502 */

    for (i = MCE_TEX_SCROLL_ARROW; i <= MCE_TEX_SCROLL_ARROW + 1; i++) { /* 3505 */
        CopySprDToSpr(&scroll_ds, &menu_camera_tex[i]);                 /* 3506 */

        scroll_ds.x = scroll_ds.x + off_x;
        scroll_ds.y = scroll_ds.y + off_y;                              /* 3507 */

        scroll_ds.r = rgb;   scroll_ds.g = rgb;   scroll_ds.b = rgb;    /* 3508 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 3509 */

        DispSprD(&scroll_ds);                                           /* 3510 */
    }                                                                   /* 3511 */

    if (center_size > 0.0f) {                                           /* 3514 */
        CopySprDToSpr(&scroll_ds,
                      &menu_camera_tex[MCE_TEX_SCROLL_KNOB_TOP]);       /* 3517 */

        scroll_ds.crx = scroll_ds.x + off_x;
        scroll_ds.cry = scroll_y + (float)scroll_ds.w + off_y;          /* 3518 */
        scroll_ds.rot = 270.0f;                                         /* 3519 */
        scroll_ds.alphar = 0x48;                                        /* 3520 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 3521 */
        scroll_ds.x = scroll_ds.crx;   scroll_ds.y = scroll_ds.cry;

        DispSprD(&scroll_ds);                                           /* 3522 */

        scroll_y = scroll_y
                   + (float)menu_camera_tex[MCE_TEX_SCROLL_KNOB_TOP].w; /* 3524 */

        CopySprDToSpr(&scroll_ds,
                      &menu_camera_tex[MCE_TEX_SCROLL_KNOB_MID]);       /* 3526 */

        scroll_scl = center_size
                     / (float)menu_camera_tex[MCE_TEX_SCROLL_KNOB_MID].w;
        scroll_ds.crx = scroll_ds.x + off_x;
        scroll_ds.cry = scroll_y + (float)scroll_ds.w * scroll_scl
                        + off_y;                                        /* 3527 */
        scroll_ds.rot = 270.0f;                                         /* 3528 */
        scroll_ds.alphar = 0x48;                                        /* 3529 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 3530 */
        scroll_ds.scw = scroll_scl;   scroll_ds.sch = 1.0f;
        scroll_ds.csx = scroll_ds.crx;   scroll_ds.csy = scroll_ds.cry;
        scroll_ds.x = scroll_ds.crx;     scroll_ds.y = scroll_ds.cry;   /* 3531 */

        DispSprD(&scroll_ds);                                           /* 3532 */

        scroll_y = scroll_y + center_size;                              /* 3534 */

        CopySprDToSpr(&scroll_ds,
                      &menu_camera_tex[MCE_TEX_SCROLL_KNOB_BTM]);       /* 3536 */

        scroll_ds.crx = scroll_ds.x + off_x;
        scroll_ds.cry = scroll_y + (float)scroll_ds.w + off_y;          /* 3537 */
        scroll_ds.rot = 270.0f;                                         /* 3538 */
        scroll_ds.alphar = 0x48;                                        /* 3539 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 3540 */
        scroll_ds.x = scroll_ds.crx;   scroll_ds.y = scroll_ds.cry;

        DispSprD(&scroll_ds);                                           /* 3541 */
    }
    else {
        scroll_scl = (scroll_size * 0.5f)
                     / (float)menu_camera_tex[MCE_TEX_SCROLL_KNOB_TOP].w; /* 3545 */

        CopySprDToSpr(&scroll_ds,
                      &menu_camera_tex[MCE_TEX_SCROLL_KNOB_TOP]);       /* 3547 */

        scroll_ds.crx = scroll_ds.x + off_x;
        scroll_ds.cry = scroll_y + (float)scroll_ds.w * scroll_scl
                        + off_y;                                        /* 3548 */
        scroll_ds.rot = 270.0f;                                         /* 3549 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 3550 */
        scroll_ds.scw = scroll_scl;   scroll_ds.sch = 1.0f;
        scroll_ds.csx = scroll_ds.crx;   scroll_ds.csy = scroll_ds.cry;
        scroll_ds.x = scroll_ds.crx;     scroll_ds.y = scroll_ds.cry;   /* 3551 */

        DispSprD(&scroll_ds);                                           /* 3552 */

        scroll_y = scroll_y + (float)scroll_ds.w * scroll_scl;

        scroll_scl = (scroll_size * 0.5f)
                     / (float)menu_camera_tex[MCE_TEX_SCROLL_KNOB_BTM].w; /* 3554 */

        CopySprDToSpr(&scroll_ds,
                      &menu_camera_tex[MCE_TEX_SCROLL_KNOB_BTM]);       /* 3557 */

        scroll_ds.crx = scroll_ds.x + off_x;
        scroll_ds.cry = scroll_y + (float)scroll_ds.w * scroll_scl
                        + off_y;                                        /* 3558 */
        scroll_ds.rot = 270.0f;                                         /* 3559 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 3560 */
        scroll_ds.scw = scroll_scl;   scroll_ds.sch = 1.0f;
        scroll_ds.csx = scroll_ds.crx;   scroll_ds.csy = scroll_ds.cry;
        scroll_ds.x = scroll_ds.crx;     scroll_ds.y = scroll_ds.cry;   /* 3561 */

        DispSprD(&scroll_ds);                                           /* 3562 */
    }
}

/* The three shared frames at the bottom of the page.  All three ignore
 * off_x / off_y, the folder's usual habit; every caller passes 0, 0. */
static void MenuCamEditMsgWindowDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 3574 */
{
    DrawCmnWindow(0, (float)(off_x + 0x18), (float)(off_y + 0x160),
                  592.0f, 100.0f, alpha, 0x66);                         /* 3578 */
}

static void MenuCamEditConfYesNoDisp(int off_x, int off_y,
                                     u_char alpha)                     /* 3589 */
{
    DrawCmnSelCsr(0,
                  (float)(menu_cam_edit_ctrl.conf_csr * 0xce + off_x + 0x9b),
                  (float)(off_y + 0x193), alpha, 0.0f, 0);              /* 3594 */

    DrawCmnSelYes(0, (float)(off_x + 0x99), (float)(off_y + 0x195),
                  alpha);                                               /* 3597 */

    DrawCmnSelNo(0, (float)(off_x + 0x169), (float)(off_y + 0x195),
                 alpha);                                                /* 3598 */
}

static void MenuCamEditCaptionDisp(int off_x, int off_y, u_char alpha) /* 3609 */
{
    DrawCmnCapGroup_W(0, 0, alpha, 0);                                  /* 3612 */
}
