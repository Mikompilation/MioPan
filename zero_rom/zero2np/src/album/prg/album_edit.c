// FILE: /home/zero_rom/zero2np/src/album/prg/album_edit.c
//
// The album's edit page.  All five ZERO2.MAP exports plus the 46 file-local
// helpers are reconstructed, which is every function functions.txt lists for
// the object bar the fixed_array<> boilerplate.
//
// This is the album's real screen: both albums drawn at once as two rows of
// eight thumbnails each, one enlarged photo, its date / score / subject panel,
// and a five-row action menu.  It is also the folder's hub -- album_save.o,
// album_load.o and album_view.o are all reached from here, and two of them are
// run *inside* this module's own mode machine rather than beside it.
//
// The machine is two levels, the same shape album_load.c and album_save.c use.
// `step` is the page (AlbumEditMain()); `mode` is which of the ten sub-screens
// owns the frame, dispatched through four parallel tables at data 2d7dc8:
//
//     mode                 init                  main                   end            disp
//   0 TOP            AlbumEditTopInit      AlbumEditModeTop         --        AlbumEditTopDisp
//   1 MENU                 --              AlbumEditModeMenu        --        AlbumEditTopMenuDisp
//   2 SORT                 --              AlbumEditSortPad         --        AlbumEditSortDisp
//   3 COPY_POS_SEL   AlbumEditCopyPosSelInit AlbumEditCopyPosSelMain --       AlbumEditCopyPosSelDisp
//   4 COPY_CONF      AlbumEditCopyConfInit AlbumEditCopyConfPad     --        AlbumEditCopyConfDisp
//   5 DEL_CONF       AlbumEditConfInit     AlbumEditDelConfPad      --        AlbumEditDelConfDisp
//   6 LOAD_CONF      AlbumEditConfInit     AlbumEditLoadConfPad     --        AlbumEditLoadConfDisp
//   7 SAVE           AlbumSaveCtrlInit     AlbumEditModeSave    AlbumSaveEnd  AlbumEditSaveDisp
//   8 LOAD           AlbumLoadCtrlInit     AlbumEditModeLoad    AlbumLoadEnd  AlbumEditLoadDisp
//   9 EXIT_CONF      AlbumEditConfInit     AlbumEditExitConfPad     --        AlbumEditExitConfDisp
//
// Modes 7 and 8 are the interesting rows: their init/end slots point straight
// into album_save.o and album_load.o, and AlbumEditModeSave/Load do nothing but
// pump those screens' Main() and step the mode on when they report done.  That
// is the whole coupling -- the card screens never see this module's state.
//
// Things worth knowing before touching it:
//
//  * The grid is 2 rows of 8, and that is why UP and DOWN are the same
//    operation.  Every cursor move is a real signed modulo: up/down is
//    (photo_no + 8) % 16 and left/right is (photo_no / 8) * 8 +
//    (photo_no +- 1) % 8, i.e. the row is preserved and the column wraps
//    inside it.  GCC emits the bias-and-shift sequence for all of them, so
//    they are `%` in the source and not a mask.
//
//  * AlbumEditTopPad() has four separate movement arms in the source, not
//    three.  UP's body and DOWN's body are identical -- with two rows they
//    have to be -- so GCC cross-jumped them and only DOWN's line numbers
//    (521-527) survive; UP keeps just its test at 512.  The proof that they
//    were written out separately is AlbumEditCopyPosSelPad(), whose identical
//    pair did *not* get merged and still has both bodies.  See
//    [[gcc-cross-jumps-identical-call-tails]].
//
//  * `photo_flg` is a one-frame cache, not a state.  AlbumEditPhotoDisp()
//    inflates the enlargement only when it is 0 and then raises it;
//    AlbumEditUncompressPhotoReq() drops it back.  Every cursor move, album
//    flip, sort and card load calls that -- which is why it is exported: the
//    load screen has to drop the cache once new pages land.
//
//  * album_type 5 (the camera's own photo file) is the read-only album.  Four
//    of the five menu rows test for it: Copy needs the *other* album not to be
//    5, Delete needs *this* one not to be, and Save/Load need this one not to
//    be.  Only Sort works on it.  That is also the only album whose photos can
//    carry the protect bit, which is why AlbumEditThumbnailProtectionFrameDisp()
//    tests album_type before drawing anything.
//
//  * conf_csr 0 is Yes and 1 is No, and AlbumEditConfInit() seeds it to 1 --
//    every confirm window opens on No.  LEFT and RIGHT toggle it with a plain
//    xor in all four windows.
//
//  * The menu cursor *skips* rows whose condition fails.  Both arms of
//    AlbumEditMenuPad() walk in a bounded loop (up to five tries) until
//    CheckAlbumMenuCondition() passes, and the move cue only plays if the
//    cursor actually ended up somewhere else.  Unlike outgame/setup_menu.c
//    there is no error cue on the failing path -- it just returns.
//
//  * AlbumEditMenuDisp() throws its `alpha` parameter away.  It stores the
//    incoming value into the parameter's own stack slot, immediately
//    overwrites it with 128, and hands that address to
//    AlbumEditMenuAnimCtrl() -- so the menu always fades on its own animation
//    and never on the page's.  AlbumEditSortMenuDisp(), which is otherwise the
//    same function, does *not* do this and honours its caller.
//
// Two ROM quirks reproduced rather than fixed, both commented at the site:
// AlbumEditThumbnailProtectionFrameDisp() indexes its *data* by data_label but
// draws at album A's grid unconditionally (harmless -- album B is never type
// 5, and every call site passes 0); and AlbumEditSortMenuDisp() draws the
// selected row's frame with non_sel_frame_scl, so the sort rows never scale up
// even though the scale was computed for them.
//
// AlbumEditLoadConfPad() opens with a GetAlbumPhotoNo() whose result is never
// used -- a dead call the ROM makes every frame the window is up.  Kept.
//
// Static data: four 10-entry dispatch tables in .data (2d7dc8, 0xa0);
// thumbnail_x_tbl / thumbnail_y_tbl / sort_x_tbl in .sdata (3ef3c0..3ef3d8);
// menu_item_x[5] and the six __FUNCTION__ strings in .rodata (3a08d0, 0x1d3),
// the rest of which is the fixed_array<> assert literal, the file name, the
// two banner formats and the three type_info names.  All of it is read out of
// the ELF and reproduced byte-for-byte.
//
// Verified 5/5 against ZERO2.MAP's exports and 57/57 against functions.txt.
// All three structs come from types.txt (they are anonymous typedefs there,
// which is why a `struct ALBUM_EDIT_CTRL` grep finds nothing) and match the
// .bss/.sbss allocation: ALBUM_EDIT_CTRL 0x9 and ALBUM_EDIT_DISP 0x9 at
// 4220f0/422100 with the linker's 7-byte alignment gap between them, and
// ALBUM_COPY_CTRL 0x4 in .sbss at 3f4ab8.
//
// NOT YET RUN IN-GAME.  Every body here was a stub before this, and
// AlbumEditMain() asked the album to close on its first frame -- so the edit
// page has never drawn a pixel, and album_save.o, album_load.o and
// album_view.o have never been reachable at all.  With this the whole album
// folder is live for the first time.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), album_edit.o
// (.text 0x120178..0x124410 = 0x4298).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM records in the object's disassembly.  Coverage is partial and
// deliberately so: statements whose only memory access goes through
// fixed_array<>'s inlined operator[] are attributed to fixed_array.h 124/125
// and leave no $LM of their own, and GCC cross-jumped several identical call
// tails, so a body can carry its twin's numbers or none at all.  Where a line
// is interpolated into a measured gap rather than read out of one, it says so.

#include "album_edit.h"

#include "album.h"                                  // GetCurrentAlbum / album_info / tex accessors
#include "album_disp.h"                             // AlbumInOutAnimCtrl / Album*Disp
#include "album_load.h"                             // AlbumLoadMain / AlbumLoadDispMain
#include "album_save.h"                             // AlbumSaveMain / AlbumSaveDispMain
#include "album_view.h"                             // AlbumViewBackGroundLoadReq
#include "../dat/album_dat.h"                       // album_tex[]
#include "../../common/utility2.h"                  // PRINT_ASSERT
#include "../../common/variable.h"                  // pad[] / paddat[]
#include "../../graphics/graph2d/g2d_draw.h"        // DISP_SPRT / CopySprDToSpr / DispSprD
#include "../../graphics/graph2d/message.h"         // PrintMsg / PrintNumber_N / GetMsgIDNumMax
#include "../../graphics/graph2d/tim2.h"            // PK2SendVram
#include "../../graphics/graph3d/ctl/fixed_array.h" // fixed_array<ALBUM_INFO,2>
#include "../../ingame/menu/zero2_anim2d.h"         // Zero2Anim2D_CsrAnimCtrl / STEP_*
#include "../../ingame/menu/play_data.h"            // DATE_INFO / SetDateInfoType
#include "../../ingame/photo/photo.h"               // PICTURE_WRK / SortPhotoData_*
#include "../../ingame/photo/photo_make.h"          // UncompressPhoto / DrawPhoto*
#include "../../system/os/system.h"                 // SystemBankPlay
#include "../../system/pad/pad.h"                   // GetPadAnalogRpt

/* ---- geometry ----------------------------------------------------------- *
 * Sixteen slots per album, laid out two rows of eight.  Everything about the
 * cursor arithmetic falls out of these two numbers. */
#define ALBUM_EDIT_PHOTO_MAX        16
#define ALBUM_EDIT_THUMBNAIL_ROW    2
#define ALBUM_EDIT_THUMBNAIL_COL    8

/* Thumbnail cell pitch, in pixels. */
#define ALBUM_EDIT_THUMBNAIL_STEP_X 0x32    /* 50 */
#define ALBUM_EDIT_THUMBNAIL_STEP_Y 0x23    /* 35 */

/* A thumbnail is stored 45x30 and drawn at that size here (the enlarged view
 * is the only place a picture is scaled). */
#define ALBUM_EDIT_THUMBNAIL_W      0x2d    /* 45 */
#define ALBUM_EDIT_THUMBNAIL_H      0x1e    /* 30 */

/* Rows in the action menu, and the pitch the drawn ones are stacked at. */
#define ALBUM_EDIT_MENU_MAX         5
#define ALBUM_EDIT_MENU_STEP_Y      0x23    /* 35 */

/* Sort rows: newest/oldest by date, best/worst by score. */
#define ALBUM_EDIT_SORT_MAX         2

/* Subjects a picture can name.  PICTURE_WRK::maSubject is this long. */
#define ALBUM_EDIT_SUBJECT_MAX      3

/* album_edit_ctrl.step -- the page itself.  There is no step 1: the ROM's own
 * switch in AlbumEditMain() has cases 0, 2 and 3 and asserts on anything else,
 * and nothing ever writes a 1. */
enum ALBUM_EDIT_STEP
{
    ALBUM_EDIT_DISP_INIT = 0,   /* reset the display state, seed the copy ctrl */
    ALBUM_EDIT_MODE_EXE  = 2,   /* one of the ten modes has the screen         */
    ALBUM_EDIT_OUT       = 3    /* animating out; Main() then returns 1        */
};

/* album_edit_ctrl.mode / next_mode.  The ROM's debug info carries no enum for
 * these -- the names are taken from the mode handlers' own ROM symbol names,
 * so the values still read against the four tables at data 2d7dc8. */
enum ALBUM_EDIT_MODE
{
    ALBUM_EDIT_MODE_TOP          = 0,
    ALBUM_EDIT_MODE_MENU         = 1,
    ALBUM_EDIT_MODE_SORT         = 2,
    ALBUM_EDIT_MODE_COPY_POS_SEL = 3,
    ALBUM_EDIT_MODE_COPY_CONF    = 4,
    ALBUM_EDIT_MODE_DEL_CONF     = 5,
    ALBUM_EDIT_MODE_LOAD_CONF    = 6,
    ALBUM_EDIT_MODE_SAVE         = 7,
    ALBUM_EDIT_MODE_LOAD         = 8,
    ALBUM_EDIT_MODE_EXIT_CONF    = 9,
    ALBUM_EDIT_MODE_MAX          = 10
};

/* album_edit_ctrl.sub_step.  AlbumEditModeMain() runs one of the three table
 * slots per frame; the mode's own handler is what advances it. */
#define ALBUM_EDIT_SUB_INIT     0
#define ALBUM_EDIT_SUB_MAIN     1
#define ALBUM_EDIT_SUB_END      2

/* album_edit_ctrl.menu_csr.  The order is the order of the four condition
 * tests and of menu_item_x[] below. */
enum ALBUM_EDIT_MENU
{
    ALBUM_EDIT_MENU_COPY = 0,
    ALBUM_EDIT_MENU_DEL  = 1,
    ALBUM_EDIT_MENU_SORT = 2,
    ALBUM_EDIT_MENU_SAVE = 3,
    ALBUM_EDIT_MENU_LOAD = 4
};

/* ALBUM_INFO::album_type for the camera's own photo file -- the read-only
 * album, and the only one whose pictures carry the protect bit.  album.c
 * keeps its own copy of this constant; it is not in album.h. */
#define ALBUM_TYPE_PHOTO_FILE   5

/* album_edit_ctrl.conf_csr.  AlbumEditConfInit() seeds it to NO. */
#define ALBUM_EDIT_CONF_YES     0
#define ALBUM_EDIT_CONF_NO      1

/* PICTURE_WRK::status bits, spelled here because this file is the only one
 * that tests both of them together. */
#define ALBUM_EDIT_PIC_IN_USE   0x1
#define ALBUM_EDIT_PIC_PROTECT  0x2

/* Message ids the four confirm windows print, bank 0.  The ROM writes them as
 * bare numbers; the names are the port's. */
#define ALBUM_EDIT_MSG_LOAD_CONF    7   /* "Load an album from a memory card?" */
#define ALBUM_EDIT_MSG_EXIT_CONF    8   /* "Close the album?"                  */
#define ALBUM_EDIT_MSG_DEL_CONF     9   /* "Delete this photo?"                */
#define ALBUM_EDIT_MSG_COPY_CONF    0xd /* "Copy this photo?"                  */

/* SystemBankPlay() cues, in the order the folder uses everywhere. */
#define ALBUM_EDIT_SE_MOVE      0
#define ALBUM_EDIT_SE_CANCEL    1
#define ALBUM_EDIT_SE_ERROR     2
#define ALBUM_EDIT_SE_DECIDE    3

/* ---- state -------------------------------------------------------------- *
 * All three layouts are types.txt's, which carries them as anonymous typedefs
 * -- a `struct ALBUM_EDIT_CTRL` grep finds nothing, which is why they look
 * missing.  Every member is a char and the sizes match the .bss/.sbss
 * allocation exactly. */

typedef struct                                  /* 0x9 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char sub_step;
    /* 0x2 */ char mode;
    /* 0x3 */ char next_mode;
    /* 0x4 */ char photo_flg;    /* the one-frame enlargement cache */
    /* 0x5 */ char menu_csr;
    /* 0x6 */ char sort_csr;
    /* 0x7 */ char sort_flg;     /* which direction the sort row runs next */
    /* 0x8 */ char conf_csr;
} ALBUM_EDIT_CTRL;

/* Four independent animation counters: the page's own fade, the action
 * menu's, the sort rows', and three cursor pulses. */
typedef struct                                  /* 0x9 */
{
    /* 0x0 */ char anim_step;
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char menu_anim_step;
    /* 0x3 */ char menu_anim_timer;
    /* 0x4 */ char sort_anim_step;
    /* 0x5 */ char sort_anim_timer;
    /* 0x6 */ char copy_csr_timer;
    /* 0x7 */ char del_csr_timer;
    /* 0x8 */ char album_flare_timer;
} ALBUM_EDIT_DISP;

typedef struct                                  /* 0x4 */
{
    /* 0x0 */ char album_data;      /* the album the picture comes from   */
    /* 0x1 */ char copy_photo_no;   /* its slot in that album             */
    /* 0x2 */ char photo_no;        /* the destination slot being picked  */
    /* 0x3 */ char short_cut_flg;   /* entered by R2 from the top view    */
} ALBUM_COPY_CTRL;

/* bss 4220f0 / 422100.  The linker leaves a 7-byte alignment gap between them;
 * both structs are 9 bytes, and .bss for the object is 0x19. */
static ALBUM_EDIT_CTRL album_edit_ctrl;
static ALBUM_EDIT_DISP album_edit_disp;

/* sbss 3f4ab8.  The copy job's own four bytes: which album the picture is
 * coming from, which slot it is, where it is going, and whether the player
 * took the R2 shortcut straight from the top view (which skips both the menu
 * and the confirm window). */
static ALBUM_COPY_CTRL album_copy_ctrl;

/* ---- forward declarations ------------------------------------------------ *
 * In ROM source order, which is also the order the bodies appear below. */

static void AlbumEditCopyCtrlInit(void);
static void AlbumEditTopInit(void);
static void AlbumEditCopyPosSelInit(void);
static void AlbumEditConfInit(void);
static void AlbumEditCopyConfInit(void);
static int  AlbumEditModeMain(void);
static void AlbumEditChangeMode(void);
static void AlbumEditModeTop(void);
static void AlbumEditTopPad(void);
static void AlbumEditMoveMenuReq(void);
static void AlbumEditMoveCopyPosSelReq(char short_cut_flg);
static void AlbumEditOutReq(void);
static void AlbumEditMoveViewReq(void);
static void AlbumEditModeMenu(void);
static void AlbumEditMenuPad(void);
static void AlbumEditMenuPadDecision(void);
static void AlbumEditSortPad(void);
static void AlbumEditCopyPosSelMain(void);
static void AlbumEditCopyPosSelPad(void);
static void AlbumEditCopyConfPad(void);
static void AlbumEditDelConfPad(void);
static void AlbumEditLoadConfPad(void);
static void AlbumEditExitConfPad(void);
static void AlbumPhotoCopy(PICTURE_WRK *copy_data, int copy_album,
                           PICTURE_WRK *data, int data_album);
static void AlbumPhotoDelete(int del_album, int photo_no);
static void AlbumEditModeSave(void);
static void AlbumEditModeLoad(void);
static int  CheckAlbumMenuCondition(int menu_label);
static int  CheckAlbumMenuCopyCondition(void);
static int  CheckAlbumMenuDelCondition(void);
static int  CheckAlbumMenuSortCondition(void);
static int  CheckAlbumMenuSaveCondition(void);
static int  CheckAlbumMenuLoadCondition(void);
static void AlbumEditDispInit(void);
static void AlbumEditBaseDisp(int album_data, int photo_no, int off_x, int off_y,
                              u_char alpha);
static void AlbumEditAlbumThumbnailDisp(int data_label, int off_x, int off_y,
                                        u_char alpha);
static void AlbumEditThumbnailProtectionFrameDisp(int data_label, int off_x,
                                                  int off_y, u_char alpha);
static void AlbumEditPhotoDisp(int album_data, int photo_no, int off_x, int off_y,
                               u_char alpha);
static void AlbumEditPhotoInfoDisp(int album_data, int photo_no, int off_x,
                                   int off_y, u_char alpha);
static void AlbumEditMenuDisp(int album_data, int album_type, int off_x, int off_y,
                              u_char alpha);
static void AlbumEditTopDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditTopMenuDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditSortDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditSortMenuDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditCopyPosSelDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditCopyConfDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditDelConfDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditLoadConfDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditSaveDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditLoadDisp(int off_x, int off_y, u_char alpha);
static void AlbumEditExitConfDisp(int off_x, int off_y, u_char alpha);

/* ---- the mode tables ----------------------------------------------------- *
 * data 2d7dc8, 2d7df0, 2d7e18, 2d7e40 -- four 10-entry tables, 0xa0 in all,
 * read out of the ELF and resolved through ZERO2.MAP.  A NULL slot means the
 * mode has nothing to do at that point, and AlbumEditModeMain() tests for it.
 *
 * Rows 7 and 8 are the ones that reach outside this object: their init and end
 * slots are album_save.o's and album_load.o's own entry points, so those two
 * screens are brought up and torn down by this table rather than by any code
 * here.
 *
 * Two host differences, both benign: a pointer is 8 bytes here rather than 4,
 * so the four tables are 0x140 instead of the ROM's 0xa0; and nothing ever
 * writes them, so the host compiler puts them in .rdata where the ROM had
 * them in .data.  The NULL pattern -- init 1/2 empty, end only 7/8 filled --
 * is checked against the ROM's tables and matches. */

static void (*album_edit_mode_init_func[ALBUM_EDIT_MODE_MAX])(void) =
{
    AlbumEditTopInit,           /* 0 TOP          */
    NULL,                       /* 1 MENU         */
    NULL,                       /* 2 SORT         */
    AlbumEditCopyPosSelInit,    /* 3 COPY_POS_SEL */
    AlbumEditCopyConfInit,      /* 4 COPY_CONF    */
    AlbumEditConfInit,          /* 5 DEL_CONF     */
    AlbumEditConfInit,          /* 6 LOAD_CONF    */
    AlbumSaveCtrlInit,          /* 7 SAVE         */
    AlbumLoadCtrlInit,          /* 8 LOAD         */
    AlbumEditConfInit           /* 9 EXIT_CONF    */
};

static void (*album_edit_mode_func[ALBUM_EDIT_MODE_MAX])(void) =
{
    AlbumEditModeTop,
    AlbumEditModeMenu,
    AlbumEditSortPad,
    AlbumEditCopyPosSelMain,
    AlbumEditCopyConfPad,
    AlbumEditDelConfPad,
    AlbumEditLoadConfPad,
    AlbumEditModeSave,
    AlbumEditModeLoad,
    AlbumEditExitConfPad
};

static void (*album_edit_mode_end_func[ALBUM_EDIT_MODE_MAX])(void) =
{
    NULL, NULL, NULL, NULL, NULL, NULL, NULL,
    AlbumSaveEnd,               /* 7 SAVE */
    AlbumLoadEnd,               /* 8 LOAD */
    NULL
};

static void (*album_edit_mode_disp[ALBUM_EDIT_MODE_MAX])(int, int, u_char) =
{
    AlbumEditTopDisp,
    AlbumEditTopMenuDisp,
    AlbumEditSortDisp,
    AlbumEditCopyPosSelDisp,
    AlbumEditCopyConfDisp,
    AlbumEditDelConfDisp,
    AlbumEditLoadConfDisp,
    AlbumEditSaveDisp,
    AlbumEditLoadDisp,
    AlbumEditExitConfDisp
};

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* album.c calls this once as the page is entered. */
void AlbumEditCtrlInit(void)                                            /* 292 */
{
    album_edit_ctrl.step      = ALBUM_EDIT_DISP_INIT;                   /* 295 */
    album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_INIT;                    /* 296 */
    album_edit_ctrl.mode      = ALBUM_EDIT_MODE_TOP;                    /* 297 */
    album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;                    /* 298 */

    AlbumEditUncompressPhotoReq();                                      /* 301 */

    album_edit_ctrl.menu_csr = ALBUM_EDIT_MENU_COPY;                    /* 303 */
    album_edit_ctrl.sort_csr = 0;                                       /* 304 */
    album_edit_ctrl.sort_flg = 0;                                       /* 305 */
    album_edit_ctrl.conf_csr = ALBUM_EDIT_CONF_NO;                      /* 306 */
}

static void AlbumEditCopyCtrlInit(void)                                 /* 317 */
{
    album_copy_ctrl.album_data     = 0;                                 /* 317 */
    album_copy_ctrl.copy_photo_no  = 0;                                 /* 318 */
    album_copy_ctrl.photo_no       = 0;                                 /* 319 */
    album_copy_ctrl.short_cut_flg  = 0;                                 /* 320 */
}

/* Coming back to the top view only needs the enlargement dropped if the copy
 * job that just finished was the R2 shortcut -- every other path through the
 * menu has already dropped it. */
static void AlbumEditTopInit(void)                                      /* 328 */
{
    if (album_copy_ctrl.short_cut_flg == 1) {                           /* 331 */
        AlbumEditUncompressPhotoReq();                                  /* 332 */
    }
}

static void AlbumEditCopyPosSelInit(void)                               /* 342 */
{
    album_copy_ctrl.album_data    = (char)GetCurrentAlbum();            /* 345 */
    album_copy_ctrl.copy_photo_no = (char)GetAlbumPhotoNo();            /* 347 */
    album_copy_ctrl.photo_no      = 0;                                  /* 349 */

    AlbumEditUncompressPhotoReq();                                      /* 353 */
}

/* Shared by DEL_CONF, LOAD_CONF and EXIT_CONF -- every confirm window opens
 * on No. */
static void AlbumEditConfInit(void)                                     /* 364 */
{
    album_edit_ctrl.conf_csr = ALBUM_EDIT_CONF_NO;                      /* 364 */
}

/* COPY_CONF additionally drops the enlargement, because the window is drawn
 * over the destination album rather than the source one. */
static void AlbumEditCopyConfInit(void)                                 /* 375 */
{
    AlbumEditConfInit();                                                /* 375 */
    AlbumEditUncompressPhotoReq();                                      /* 378 */
}

/* ==========================================================================
 *  The page machine
 * ======================================================================== */

/* Returns non-zero on the frame the closing fade has finished, which is what
 * takes album.c out of the edit page -- either to album_view.o or off the
 * screen entirely. */
int AlbumEditMain(void)                                                 /* 392 */
{
    int res = 0;

    switch (album_edit_ctrl.step) {                                     /* 399 */
    case ALBUM_EDIT_DISP_INIT:
        AlbumEditDispInit();                                            /* 402 */
        AlbumEditCopyCtrlInit();                                        /* 404 */
        album_edit_ctrl.step = ALBUM_EDIT_MODE_EXE;                     /* 407 */
        break;

    case ALBUM_EDIT_MODE_EXE:
        AlbumEditModeMain();                                            /* 410 */
        break;                                                          /* 411 */

    case ALBUM_EDIT_OUT:
        if (album_edit_disp.anim_step == ZERO2_ANIM2D_STEP_END) {       /* 413 */
            res = 1;                                                    /* 416 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 418 */
        break;
    }

    return res;                                                         /* 422 */
}

/* One frame of whichever mode owns the screen.  The three slots are tested
 * with separate `if`s rather than an if/else chain, so a mode whose init
 * raises sub_step to MAIN runs its main handler in the same frame. */
static int AlbumEditModeMain(void)                                      /* 430 */
{
    AlbumEditChangeMode();                                              /* 437 */

    if (album_edit_ctrl.sub_step == ALBUM_EDIT_SUB_INIT) {              /* 440 */
        if (album_edit_mode_init_func[album_edit_ctrl.mode] != NULL) {  /* 441 */
            (*album_edit_mode_init_func[album_edit_ctrl.mode])();       /* 442 */
        }
        album_edit_ctrl.sub_step = ALBUM_EDIT_SUB_MAIN;                 /* 445 */
    }

    if (album_edit_ctrl.sub_step == ALBUM_EDIT_SUB_MAIN &&              /* 448 */
        album_edit_mode_func[album_edit_ctrl.mode] != NULL) {           /* 449 */
        (*album_edit_mode_func[album_edit_ctrl.mode])();                /* 450 */
    }

    if (album_edit_ctrl.sub_step == ALBUM_EDIT_SUB_END &&               /* 454 */
        album_edit_mode_end_func[album_edit_ctrl.mode] != NULL) {       /* 455 */
        (*album_edit_mode_end_func[album_edit_ctrl.mode])();            /* 456 */
    }

    return 0;                                                           /* 461 */
}

/* A mode change is latched rather than applied directly, so a handler can ask
 * for one and still finish its own frame. */
static void AlbumEditChangeMode(void)                                   /* 471 */
{
    if (album_edit_ctrl.mode != album_edit_ctrl.next_mode) {            /* 471 */
        album_edit_ctrl.mode     = album_edit_ctrl.next_mode;           /* 472 */
        album_edit_ctrl.sub_step = ALBUM_EDIT_SUB_INIT;                 /* 474 */
    }
}

static void AlbumEditModeTop(void)                                      /* 491 */
{
    AlbumEditTopPad();                                                  /* 491 */
}

/* ==========================================================================
 *  Mode 0 -- the top view
 * ======================================================================== */

/* The grid cursor, plus the four buttons that leave the top view.
 *
 * UP and DOWN are two separate arms in the source with identical bodies --
 * with two rows, moving up and moving down are both (photo_no + 8) % 16 --
 * and GCC cross-jumped them, so only DOWN's body carries line numbers.
 * LEFT and RIGHT wrap inside the row: the row index (photo_no / 8) * 8 is
 * preserved and the column steps by a real signed % 8. */
static void AlbumEditTopPad(void)                                       /* 506 */
{
    int current_album = GetCurrentAlbum();                              /* 506 */
    int photo_no      = GetAlbumPhotoNo();                              /* 508 */
    int i;

    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0)) {                  /* 512 */
        /* Cross-joined with the DOWN arm below; the body is the same. */
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        photo_no = (photo_no + ALBUM_EDIT_THUMBNAIL_COL) % ALBUM_EDIT_PHOTO_MAX;
        AlbumEditUncompressPhotoReq();

    } else if ((pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {           /* 521 */
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 522 */
        photo_no = (photo_no + ALBUM_EDIT_THUMBNAIL_COL) % ALBUM_EDIT_PHOTO_MAX; /* 524 */
        AlbumEditUncompressPhotoReq();                                  /* 527 */

    } else if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {           /* 530 */
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 531 */
        photo_no = (photo_no / ALBUM_EDIT_THUMBNAIL_COL) * ALBUM_EDIT_THUMBNAIL_COL
                 + (photo_no + ALBUM_EDIT_THUMBNAIL_COL - 1) % ALBUM_EDIT_THUMBNAIL_COL; /* 533 */
        AlbumEditUncompressPhotoReq();                                  /* 536 */

    } else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {           /* 539 */
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 540 */
        photo_no = (photo_no / ALBUM_EDIT_THUMBNAIL_COL) * ALBUM_EDIT_THUMBNAIL_COL
                 + (photo_no + 1) % ALBUM_EDIT_THUMBNAIL_COL;           /* 542 */
        AlbumEditUncompressPhotoReq();                                  /* 545 */

    } else if (*paddat[0x18] == 1) {                                    /* 548 */
        /* Enlarge: only if the album has something in it at all. */
        if (album_info[current_album].album_info.pic_num != 0) {        /* 124 */
            SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 551 */
            AlbumEditMoveViewReq();                                     /* 554 */
        }

    } else if (*paddat[1] == 1) {                                       /* 561 */
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000); /* 562 */
        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;                 /* 564 */
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_EXIT_CONF;          /* 565 */

    } else if (*paddat[0x12] == 1) {                                    /* 568 */
        /* Open the menu, but only if at least one row is usable. */
        for (i = 0; i < ALBUM_EDIT_MENU_MAX; i++) {                     /* 569 574 */
            if (CheckAlbumMenuCondition(i)) {                           /* 571 */
                break;
            }
        }

        if (i < ALBUM_EDIT_MENU_MAX) {
            SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 581 */
            AlbumEditMoveMenuReq();                                     /* 584 */
        } else {
            SystemBankPlay(ALBUM_EDIT_SE_ERROR, 1, 0, 0, NULL, 0x3200, 0x1000); /* 597 */
        }

    } else if (*paddat[0x13] == 1) {                                    /* 588 */
        /* The copy shortcut: straight from the top view to slot select,
         * skipping the menu and the confirm window. */
        if (CheckAlbumMenuCopyCondition()) {                            /* 590 */
            SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 591 */
            AlbumEditMoveCopyPosSelReq(1);                              /* 594 */
        } else {
            /* Cross-joined with the menu arm's error cue above. */
            SystemBankPlay(ALBUM_EDIT_SE_ERROR, 1, 0, 0, NULL, 0x3200, 0x1000); /* 597 */
        }

    } else if (pad[0].one & 0xc) {                                      /* 601 */
        /* L1 or R1 -- flip to the other album.  The two bits arrive as one
         * 64-bit load of pad[0] at 0x180 masked with 0xc00000000, which is
         * GCC merging the pair of 16-bit tests. */
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 602 */
        ChengeCurrentAlbum();                                           /* 605 */
        AlbumEditUncompressPhotoReq();                                  /* 608 */
    }

    SetAlbumPhotoNo((char)photo_no);                                    /* 613 */
}

/* ==========================================================================
 *  Mode transitions
 * ======================================================================== */

/* Drop the cached enlargement.  The next AlbumEditPhotoDisp() re-inflates
 * whatever the cursor now points at. */
void AlbumEditUncompressPhotoReq(void)                                  /* 623 */
{
    album_edit_ctrl.photo_flg = 0;                                      /* 623 */
}

/* Open the action menu with the cursor parked on the first usable row. */
static void AlbumEditMoveMenuReq(void)                                  /* 631 */
{
    int menu_label;

    album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_MENU;                   /* 636 */

    for (menu_label = 0; menu_label < ALBUM_EDIT_MENU_MAX; menu_label++) { /* 638 */
        if (CheckAlbumMenuCondition(menu_label)) {                      /* 640 */
            album_edit_ctrl.menu_csr = (char)menu_label;                /* 641 */
            break;
        }
    }

    album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_START;           /* 646 */
    album_edit_disp.menu_anim_timer = 0;                                /* 647 */
}

static void AlbumEditMoveCopyPosSelReq(char short_cut_flg)              /* 656 */
{
    album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;                     /* 659 */
    album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_COPY_POS_SEL;           /* 660 */

    album_copy_ctrl.short_cut_flg = short_cut_flg;                      /* 662 */
    album_copy_ctrl.album_data    = (char)GetCurrentAlbum();            /* 665 */
    album_copy_ctrl.copy_photo_no = (char)GetAlbumPhotoNo();            /* 666 */
}

/* Ask the page to close for good. */
static void AlbumEditOutReq(void)                                       /* 674 */
{
    album_edit_disp.anim_step  = ZERO2_ANIM2D_STEP_OUT;                 /* 677 */
    album_edit_disp.anim_timer = 0;                                     /* 678 */

    AlbumOutReq();                                                      /* 681 */
}

/* Hand over to album_view.o.  The page still animates out, but step OUT plus
 * the view screen's own load request is what makes AlbumEditMain() report done
 * into a viewer rather than into nothing. */
static void AlbumEditMoveViewReq(void)                                  /* 689 */
{
    album_edit_ctrl.step       = ALBUM_EDIT_OUT;                        /* 692 */
    album_edit_disp.anim_step  = ZERO2_ANIM2D_STEP_OUT;                 /* 693 */
    album_edit_disp.anim_timer = 0;                                     /* 694 */

    AlbumViewBackGroundLoadReq(album_info[GetCurrentAlbum()].album_type); /* 697 */
}

/* ==========================================================================
 *  Mode 1 -- the action menu
 * ======================================================================== */

/* The menu only takes input once its open animation has finished, and it hands
 * the page back to the top view once the close animation has. */
static void AlbumEditModeMenu(void)                                     /* 705 */
{
    if (album_edit_disp.menu_anim_step == ZERO2_ANIM2D_STEP_SHOW) {     /* 708 */
        AlbumEditMenuPad();                                             /* 710 */
    }

    if (album_edit_disp.menu_anim_step == ZERO2_ANIM2D_STEP_END) {      /* 713 */
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;                /* 714 */
    }
}

/* UP and DOWN walk the cursor past rows whose condition fails, up to five
 * tries.  The cue plays only if the cursor actually ended up somewhere else,
 * which is also what happens when exactly one row is usable -- so a lone row
 * is silent rather than giving the error cue. */
static void AlbumEditMenuPad(void)                                      /* 723 */
{
    int  i;
    char csr_back_up;

    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0)) {                  /* 730 */
        csr_back_up = album_edit_ctrl.menu_csr;                         /* 731 */

        for (i = 0; i < ALBUM_EDIT_MENU_MAX; i++) {                     /* 733 740 */
            album_edit_ctrl.menu_csr =
                (char)((album_edit_ctrl.menu_csr + ALBUM_EDIT_MENU_MAX - 1)
                       % ALBUM_EDIT_MENU_MAX);                          /* 734 */
            if (CheckAlbumMenuCondition(album_edit_ctrl.menu_csr)) {    /* 737 */
                break;
            }
        }

        if (album_edit_ctrl.menu_csr != csr_back_up) {                  /* 742 */
            SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 743 */
        }

    } else if ((pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {           /* 747 */
        csr_back_up = album_edit_ctrl.menu_csr;                         /* 748 */

        for (i = 0; i < ALBUM_EDIT_MENU_MAX; i++) {                     /* 750 757 */
            album_edit_ctrl.menu_csr =
                (char)((album_edit_ctrl.menu_csr + 1) % ALBUM_EDIT_MENU_MAX); /* 751 */
            if (CheckAlbumMenuCondition(album_edit_ctrl.menu_csr)) {    /* 754 */
                break;
            }
        }

        if (album_edit_ctrl.menu_csr != csr_back_up) {                  /* 759 */
            SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000); /* 760 */
        }

    } else if (*paddat[0] == 1) {                                       /* 764 */
        AlbumEditMenuPadDecision();                                     /* 765 */

    } else if (*paddat[1] == 1) {                                       /* 768 */
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000); /* 769 */
        album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_OUT;        /* 771 */
        album_edit_disp.menu_anim_timer = 0;                            /* 772 */
    }
}

/* CROSS on a menu row.  Copy and Delete re-test that the slot really holds a
 * picture -- the row condition already said so, but the cursor can have moved
 * since the menu opened. */
static void AlbumEditMenuPadDecision(void)                              /* 781 */
{
    int current_album = GetCurrentAlbum();
    int photo_no      = GetAlbumPhotoNo();

    switch (album_edit_ctrl.menu_csr) {
    case ALBUM_EDIT_MENU_COPY:
        if ((album_info[current_album].album_info.pic[photo_no].status
             & ALBUM_EDIT_PIC_IN_USE) == 0) {
            SystemBankPlay(ALBUM_EDIT_SE_ERROR, 1, 0, 0, NULL, 0x3200, 0x1000);
            return;
        }
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);
        AlbumEditMoveCopyPosSelReq(0);
        return;

    case ALBUM_EDIT_MENU_DEL:
        if ((album_info[current_album].album_info.pic[photo_no].status
             & ALBUM_EDIT_PIC_IN_USE) == 0) {
            SystemBankPlay(ALBUM_EDIT_SE_ERROR, 1, 0, 0, NULL, 0x3200, 0x1000);
            return;
        }
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.sub_step        = ALBUM_EDIT_SUB_END;
        album_edit_ctrl.next_mode       = ALBUM_EDIT_MODE_DEL_CONF;
        album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_OUT;
        album_edit_disp.menu_anim_timer = 0;
        break;

    case ALBUM_EDIT_MENU_SORT:
        /* Sort is the only row that switches mode directly rather than
         * through next_mode alone -- the sort rows are drawn over the menu,
         * so the menu never animates out. */
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.next_mode       = ALBUM_EDIT_MODE_SORT;
        album_edit_ctrl.mode            = ALBUM_EDIT_MODE_SORT;
        album_edit_ctrl.sort_csr        = 0;
        album_edit_disp.sort_anim_step  = ZERO2_ANIM2D_STEP_START;
        album_edit_disp.sort_anim_timer = 0;
        break;

    case ALBUM_EDIT_MENU_SAVE:
        /* Save goes straight to the card screen; only Load asks first. */
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_SAVE;
        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;
        break;

    case ALBUM_EDIT_MENU_LOAD:
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_LOAD_CONF;
        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 841 */
        break;
    }
}

/* ==========================================================================
 *  Mode 2 -- sort
 * ======================================================================== */

/* Two rows, each a toggle: pressing CROSS on a row applies its sort and then
 * flips sort_flg, so the next press on the same row sorts the other way.
 * Moving between rows resets the flag, which is why a row always starts on its
 * first direction. */
static void AlbumEditSortPad(void)                                      /* 850 */
{
    int current_album;

    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0) ||
        (pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {

        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.sort_flg = 0;
        album_edit_ctrl.sort_csr =
            (char)((album_edit_ctrl.sort_csr + 1) % ALBUM_EDIT_SORT_MAX);

    } else if (*paddat[0] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);

        if (album_edit_ctrl.sort_csr == 0) {
            if (album_edit_ctrl.sort_flg == 0) {
                current_album = GetCurrentAlbum();
                SortPhotoData_NewTime(&album_info[current_album].album_info);
            } else {
                current_album = GetCurrentAlbum();
                SortPhotoData_OldTime(&album_info[current_album].album_info);
            }
        } else if (album_edit_ctrl.sort_csr == 1) {
            if (album_edit_ctrl.sort_flg == 0) {
                current_album = GetCurrentAlbum();
                SortPhotoData_BigScore(&album_info[current_album].album_info);
            } else {
                current_album = GetCurrentAlbum();
                SortPhotoData_SmallScore(&album_info[current_album].album_info);
            }
        } else {
            PRINT_ASSERT("Error! %s", __FUNCTION__);                    /* 889 */
        }

        album_edit_ctrl.sort_flg = (album_edit_ctrl.sort_flg == 0);
        AlbumEditUncompressPhotoReq();

    } else if (*paddat[1] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_MENU;
        album_edit_ctrl.mode      = ALBUM_EDIT_MODE_MENU;
    }
}

/* ==========================================================================
 *  Mode 3 -- pick the destination slot
 * ======================================================================== */

static void AlbumEditCopyPosSelMain(void)                               /* 920 */
{
    AlbumEditCopyPosSelPad();                                           /* 920 */
}

/* Same grid arithmetic as the top view, but driven on album_copy_ctrl's own
 * cursor and against the *other* album.  Here UP and DOWN survive as two
 * separate bodies -- GCC did not cross-join them the way it did in
 * AlbumEditTopPad(), which is what proves that pair was written out twice. */
static void AlbumEditCopyPosSelPad(void)                                /* 928 */
{
    int copy_photo_no;
    int data_photo_no;

    GetCurrentAlbum();      /* result unused -- the ROM's own dead call */

    data_photo_no = album_copy_ctrl.photo_no;
    copy_photo_no = album_copy_ctrl.copy_photo_no;

    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0)) {
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_copy_ctrl.photo_no =
            (char)((album_copy_ctrl.photo_no + ALBUM_EDIT_THUMBNAIL_COL)
                   % ALBUM_EDIT_PHOTO_MAX);
        AlbumEditUncompressPhotoReq();
        return;
    }

    if ((pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_copy_ctrl.photo_no =
            (char)((album_copy_ctrl.photo_no + ALBUM_EDIT_THUMBNAIL_COL)
                   % ALBUM_EDIT_PHOTO_MAX);
        AlbumEditUncompressPhotoReq();
        return;
    }

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_copy_ctrl.photo_no =
            (char)((album_copy_ctrl.photo_no / ALBUM_EDIT_THUMBNAIL_COL)
                       * ALBUM_EDIT_THUMBNAIL_COL
                   + (album_copy_ctrl.photo_no + ALBUM_EDIT_THUMBNAIL_COL - 1)
                       % ALBUM_EDIT_THUMBNAIL_COL);
        AlbumEditUncompressPhotoReq();
        return;
    }

    if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {
        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_copy_ctrl.photo_no =
            (char)((album_copy_ctrl.photo_no / ALBUM_EDIT_THUMBNAIL_COL)
                       * ALBUM_EDIT_THUMBNAIL_COL
                   + (album_copy_ctrl.photo_no + 1) % ALBUM_EDIT_THUMBNAIL_COL);
        AlbumEditUncompressPhotoReq();
        return;
    }

    if (*paddat[0] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);

        if (album_copy_ctrl.short_cut_flg == 1) {
            /* The R2 shortcut copies without asking. */
            AlbumPhotoCopy(&album_info[album_copy_ctrl.album_data ^ 1]
                                .album_info.pic[data_photo_no],
                           album_copy_ctrl.album_data ^ 1,
                           &album_info[album_copy_ctrl.album_data]
                                .album_info.pic[copy_photo_no],
                           album_copy_ctrl.album_data);

            album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;
            album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;
        } else {
            album_edit_ctrl.next_mode       = ALBUM_EDIT_MODE_COPY_CONF;
            album_edit_ctrl.sub_step        = ALBUM_EDIT_SUB_END;
            album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_OUT;
            album_edit_disp.menu_anim_timer = 0;
        }

    } else if (*paddat[1] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);

        /* Backing out of the shortcut returns to the top view; backing out of
         * the menu route returns to the menu. */
        album_edit_ctrl.next_mode = (album_copy_ctrl.short_cut_flg == 0);

        if (album_edit_ctrl.next_mode) {
            album_edit_ctrl.mode = ALBUM_EDIT_MODE_MENU;
        } else {
            album_edit_ctrl.sub_step = ALBUM_EDIT_SUB_END;
        }

        AlbumEditUncompressPhotoReq();
    }
}

/* ==========================================================================
 *  Modes 4, 5, 6, 9 -- the confirm windows
 * ======================================================================== */

static void AlbumEditCopyConfPad(void)                                  /* 1020 */
{
    int copy_photo_no;
    int data_photo_no;

    data_photo_no = album_copy_ctrl.photo_no;
    copy_photo_no = album_copy_ctrl.copy_photo_no;

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {

        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.conf_csr = album_edit_ctrl.conf_csr ^ 1;

    } else if (*paddat[0] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);

        if (album_edit_ctrl.conf_csr != ALBUM_EDIT_CONF_YES) {
            /* No -- back to the slot picker, already open. */
            album_edit_ctrl.sub_step        = ALBUM_EDIT_SUB_MAIN;
            album_edit_ctrl.next_mode       = ALBUM_EDIT_MODE_COPY_POS_SEL;
            album_edit_ctrl.mode            = ALBUM_EDIT_MODE_COPY_POS_SEL;
            album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_START;
            album_edit_disp.menu_anim_timer = 0;
            AlbumEditUncompressPhotoReq();
            return;
        }

        AlbumPhotoCopy(&album_info[album_copy_ctrl.album_data ^ 1]
                            .album_info.pic[data_photo_no],
                       album_copy_ctrl.album_data ^ 1,
                       &album_info[album_copy_ctrl.album_data]
                            .album_info.pic[copy_photo_no],
                       album_copy_ctrl.album_data);

        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;

    } else if (*paddat[1] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.next_mode       = ALBUM_EDIT_MODE_MENU;
        album_edit_ctrl.mode            = ALBUM_EDIT_MODE_MENU;
        album_edit_ctrl.sub_step        = ALBUM_EDIT_SUB_MAIN;
        album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_START;
        album_edit_disp.menu_anim_timer = 0;
        AlbumEditUncompressPhotoReq();
    }
}

/* "No" and CIRCLE share the tail here -- both land on the same four stores,
 * which is why they sit after the if/else rather than inside each arm. */
static void AlbumEditDelConfPad(void)                                   /* 1092 */
{
    int photo_no = GetAlbumPhotoNo();

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {

        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.conf_csr = album_edit_ctrl.conf_csr ^ 1;
        return;
    }

    if (*paddat[0] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);

        if (album_edit_ctrl.conf_csr == ALBUM_EDIT_CONF_YES) {
            AlbumPhotoDelete(GetCurrentAlbum(), photo_no);
            album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;
            album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;
            AlbumEditUncompressPhotoReq();
            return;
        }
    } else if (*paddat[1] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);
    } else {
        return;
    }

    album_edit_ctrl.next_mode       = ALBUM_EDIT_MODE_MENU;
    album_edit_ctrl.sub_step        = ALBUM_EDIT_SUB_MAIN;
    album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_START;
    album_edit_disp.menu_anim_timer = 0;
}

/* The GetAlbumPhotoNo() at the top is dead -- nothing in the body reads it.
 * Kept as found. */
static void AlbumEditLoadConfPad(void)                                  /* 1153 */
{
    GetAlbumPhotoNo();

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {

        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.conf_csr = album_edit_ctrl.conf_csr ^ 1;

    } else if (*paddat[0] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);

        if (album_edit_ctrl.conf_csr == ALBUM_EDIT_CONF_YES) {
            album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_LOAD;
            album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;
        } else {
            album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_MENU;
            album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_MAIN;
        }

    } else if (*paddat[1] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_MENU;
        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_MAIN;
    }
}

static void AlbumEditExitConfPad(void)                                  /* 1197 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {

        SystemBankPlay(ALBUM_EDIT_SE_MOVE, 1, 0, 0, NULL, 0x3200, 0x1000);
        album_edit_ctrl.conf_csr = album_edit_ctrl.conf_csr ^ 1;
        return;
    }

    if (*paddat[0] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_DECIDE, 1, 0, 0, NULL, 0x3200, 0x1000);

        if (album_edit_ctrl.conf_csr == ALBUM_EDIT_CONF_YES) {
            AlbumEditOutReq();
            return;
        }
    } else if (*paddat[1] == 1) {
        SystemBankPlay(ALBUM_EDIT_SE_CANCEL, 1, 0, 0, NULL, 0x3200, 0x1000);
    } else {
        return;
    }

    album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_MAIN;
    album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;
    album_edit_ctrl.mode      = ALBUM_EDIT_MODE_TOP;
}

/* ==========================================================================
 *  The two operations on album contents
 * ======================================================================== */

/* Copy one picture between albums.  Both pages move -- the 0xd360 compressed
 * image at (base + adr_no * 0xd360 + 0x10000) and the 0x1000 thumbnail at
 * (base + adr_no * 0x1000) -- and the *destination's* own adr_no is kept, so
 * the picture lands in whichever data page that slot already owned.  That is
 * what lets the album reorder without ever moving image bytes.
 *
 * The two copies are one memcpy each in the source; GCC expanded both inline
 * with an alignment test, which is why the disassembly has each loop twice. */
static void AlbumPhotoCopy(PICTURE_WRK *copy_data, int copy_album,
                           PICTURE_WRK *data, int data_album)           /* 1244 */
{
    void *data_addr;
    void *copy_addr;
    int   i;

    data_addr = GetAlbumDataAddr(data_album);
    copy_addr = GetAlbumDataAddr(copy_album);

    if ((data->status & ALBUM_EDIT_PIC_IN_USE) == 0) {
        return;
    }

    memcpy((char *)copy_addr + copy_data->adr_no * 0xd360 + 0x10000,
           (char *)data_addr + data->adr_no * 0xd360 + 0x10000,
           0xd360);

    memcpy((char *)copy_addr + copy_data->adr_no * 0x1000,
           (char *)data_addr + data->adr_no * 0x1000,
           0x1000);

    /* Filling a slot that was empty is what grows the album's count; copying
     * over an occupied one leaves it alone. */
    if (copy_data->status == 0) {
        album_info[copy_album].album_info.pic_num++;
    }

    copy_data->chp_no = data->chp_no;

    for (i = 0; i < ALBUM_EDIT_SUBJECT_MAX; i++) {
        copy_data->maSubject[i] = data->maSubject[i];
    }

    copy_data->score  = data->score;
    copy_data->time   = data->time;
    copy_data->room   = data->room;
    copy_data->status = ALBUM_EDIT_PIC_IN_USE;
}

/* A protected picture survives deletion silently -- the caller has already
 * asked and been answered yes. */
static void AlbumPhotoDelete(int del_album, int photo_no)               /* 1298 */
{
    if (photo_no >= ALBUM_EDIT_PHOTO_MAX) {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1302 */
    }

    if ((album_info[del_album].album_info.pic[photo_no].status
         & ALBUM_EDIT_PIC_IN_USE) != 0) {

        if ((album_info[del_album].album_info.pic[photo_no].status
             & ALBUM_EDIT_PIC_PROTECT) == 0) {

            album_info[del_album].album_info.pic[photo_no].status = 0;
            album_info[del_album].album_info.pic_num--;
        }
    }
}

/* ==========================================================================
 *  Modes 7 and 8 -- the card screens
 * ======================================================================== */

/* Both of these are the whole coupling to album_save.o / album_load.o: the
 * mode tables brought the screen up and will tear it down, and all that is
 * left is to pump its Main() and step the mode on when it reports done. */
static void AlbumEditModeSave(void)                                     /* 1327 */
{
    if (AlbumSaveMain()) {                                              /* 1327 */
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;                /* 1328 */
        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;                 /* 1329 */
    }
}

static void AlbumEditModeLoad(void)                                     /* 1342 */
{
    if (AlbumLoadMain()) {                                              /* 1342 */
        album_edit_ctrl.next_mode = ALBUM_EDIT_MODE_TOP;                /* 1346 */
        album_edit_ctrl.sub_step  = ALBUM_EDIT_SUB_END;                 /* 1347 */
    }
}

/* Close the action menu.  album_load.o and album_save.o call this on their way
 * out so the page comes back to a bare top view rather than to a menu that was
 * still open when the card screen took over. */
void AlbumEditMenuDelete(void)                                          /* 1358 */
{
    album_edit_disp.menu_anim_step  = ZERO2_ANIM2D_STEP_END;            /* 1358 */
    album_edit_disp.menu_anim_timer = 0;                                /* 1359 */
}

/* ==========================================================================
 *  Menu row conditions
 * ======================================================================== */

static int CheckAlbumMenuCondition(int menu_label)                      /* 1374 */
{
    int res;

    switch (menu_label) {
    case ALBUM_EDIT_MENU_COPY:
        res = CheckAlbumMenuCopyCondition();
        break;
    case ALBUM_EDIT_MENU_DEL:
        res = CheckAlbumMenuDelCondition();
        break;
    case ALBUM_EDIT_MENU_SORT:
        res = CheckAlbumMenuSortCondition();
        break;
    case ALBUM_EDIT_MENU_SAVE:
        res = CheckAlbumMenuSaveCondition();
        break;
    case ALBUM_EDIT_MENU_LOAD:
        res = CheckAlbumMenuLoadCondition();
        break;
    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1413 */
        return 0;
    }

    return (res != 0);                                                  /* 1417 */
}

/* Copy needs a picture here and somewhere to put it -- the *other* album must
 * not be the camera's own read-only file. */
static int CheckAlbumMenuCopyCondition(void)                            /* 1425 */
{
    int res = 0;
    int current_album = GetCurrentAlbum();

    if ((album_info[current_album].album_info.pic[GetAlbumPhotoNo()].status
         & ALBUM_EDIT_PIC_IN_USE) != 0) {
        res = (album_info[current_album ^ 1].album_type != ALBUM_TYPE_PHOTO_FILE);
    }

    return res;                                                         /* 1443 */
}

/* Delete needs a picture here and this album not to be the read-only one. */
static int CheckAlbumMenuDelCondition(void)                             /* 1451 */
{
    int res = 0;
    int current_album = GetCurrentAlbum();

    if ((album_info[current_album].album_info.pic[GetAlbumPhotoNo()].status
         & ALBUM_EDIT_PIC_IN_USE) != 0) {
        res = (album_info[current_album].album_type != ALBUM_TYPE_PHOTO_FILE);
    }

    return res;                                                         /* 1469 */
}

/* Sort is the only row the camera's own file is allowed. */
static int CheckAlbumMenuSortCondition(void)                            /* 1483 */
{
    int current_album = GetCurrentAlbum();

    return (album_info[current_album].album_info.pic_num != 0);         /* 1492 */
}

static int CheckAlbumMenuSaveCondition(void)                            /* 1506 */
{
    int current_album = GetCurrentAlbum();

    return (album_info[current_album].album_type != ALBUM_TYPE_PHOTO_FILE); /* 1515 */
}

static int CheckAlbumMenuLoadCondition(void)                            /* 1529 */
{
    int current_album = GetCurrentAlbum();

    return (album_info[current_album].album_type != ALBUM_TYPE_PHOTO_FILE); /* 1538 */
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

static void AlbumEditDispInit(void)                                     /* 1551 */
{
    album_edit_disp.anim_step         = ZERO2_ANIM2D_STEP_START;         /* 1551 */
    album_edit_disp.anim_timer        = 0;                              /* 1552 */
    album_edit_disp.menu_anim_step    = ZERO2_ANIM2D_STEP_START;         /* 1553 */
    album_edit_disp.menu_anim_timer   = 0;                              /* 1554 */
    album_edit_disp.sort_anim_step    = ZERO2_ANIM2D_STEP_START;         /* 1555 */
    album_edit_disp.sort_anim_timer   = 0;                              /* 1556 */
    album_edit_disp.copy_csr_timer    = 0;                              /* 1557 */
    album_edit_disp.del_csr_timer     = 0;                              /* 1558 */
    album_edit_disp.album_flare_timer = 0;                              /* 1559 */
}

/* The page draws only while a mode owns it or it is fading out, and the mode's
 * own draw routine is skipped once the fade has finished -- so the last frame
 * is the backdrop alone. */
void AlbumEditDispMain(void)                                            /* 1572 */
{
    u_char alpha = 0x80;
    void  *album_cmn_tex_addr = GetAlbumCmnTexAddr();

    if (album_edit_ctrl.step == ALBUM_EDIT_MODE_EXE ||
        album_edit_ctrl.step == ALBUM_EDIT_OUT) {                       /* 1576 */

        AlbumInOutAnimCtrl(&album_edit_disp.anim_step,
                           &album_edit_disp.anim_timer, &alpha);        /* 1578 */

        PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);              /* 1580 */

        if (album_edit_disp.anim_step != ZERO2_ANIM2D_STEP_END &&
            album_edit_mode_disp[album_edit_ctrl.mode] != NULL) {       /* 1583 */
            (*album_edit_mode_disp[album_edit_ctrl.mode])(0, 0, alpha); /* 1585 */
        }
    }
}

/* Everything every mode draws: both albums' plates and thumbnails, then the
 * current album's info window, its enlarged picture and -- for the camera's
 * own file only -- the protect frame over that picture. */
static void AlbumEditBaseDisp(int album_data, int photo_no, int off_x, int off_y,
                              u_char alpha)                             /* 1601 */
{
    void *album_cmn_tex_addr  = GetAlbumCmnTexAddr();
    void *album_edit_tex_addr = GetAlbumEditTexAddr();
    int   i;

    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    for (i = 0; i < 2; i++) {
        AlbumThumbnailBaseDisp(i, 0, 0, alpha);
        AlbumThumbnailBaseNumberDisp(i, 0, 0, alpha);
        AlbumEditAlbumDisp(i, album_info[i].album_type, 0, 0, alpha);
        AlbumEditAlbumThumbnailDisp(i, 0, 0, alpha);
    }

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);
    AlbumEditAlbumInfoWinDisp(album_info[album_data].album_type, 0, 0, alpha);

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);
    AlbumEditInfoNoDisp(album_info[album_data].album_type, 0, 0, alpha);

    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);
    AlbumEditInfoPhotoNoDisp(album_info[album_data].album_type, photo_no,
                             0xdb, 0x9c, alpha);

    AlbumEditFrameDisp(0, 0, alpha);
    AlbumEditPhotoDisp(album_data, photo_no, 0, 0, alpha);

    if (album_info[album_data].album_type == ALBUM_TYPE_PHOTO_FILE) {
        if ((album_info[album_data].album_info.pic[photo_no].status
             & ALBUM_EDIT_PIC_IN_USE) != 0) {
            if ((album_info[album_data].album_info.pic[photo_no].status
                 & ALBUM_EDIT_PIC_PROTECT) != 0) {
                AlbumEditPhotoProtectionFrameDisp(0, 0, alpha);
            }
        }
    }
}

/* One album's sixteen thumbnails, two rows of eight.  The two base positions
 * are the only per-album data the page has. */
static void AlbumEditAlbumThumbnailDisp(int data_label, int off_x, int off_y,
                                        u_char alpha)                   /* 1672 */
{
    /* sdata 3ef3c0 / 3ef3c8 -- album A's grid and album B's. */
    static int thumbnail_x_tbl[2] = { 207, 36 };
    static int thumbnail_y_tbl[2] = { 80, 338 };

    int i;
    int j;
    int photo_num = 0;

    for (i = 0; i < ALBUM_EDIT_THUMBNAIL_ROW; i++) {
        for (j = 0; j < ALBUM_EDIT_THUMBNAIL_COL; j++) {
            if ((album_info[data_label].album_info.pic[photo_num].status
                 & ALBUM_EDIT_PIC_IN_USE) != 0) {

                DrawSPhotoFromSmallPhotoAreaAddr2(
                    (uintptr_t)GetAlbumDataAddr(data_label),
                    album_info[data_label].album_info.pic[photo_num].adr_no,
                    0, 0,
                    thumbnail_x_tbl[data_label] + off_x
                        + j * ALBUM_EDIT_THUMBNAIL_STEP_X,
                    thumbnail_y_tbl[data_label] + off_y
                        + i * ALBUM_EDIT_THUMBNAIL_STEP_Y,
                    ALBUM_EDIT_THUMBNAIL_W, ALBUM_EDIT_THUMBNAIL_H,
                    alpha);
            }
            photo_num++;
        }
    }
}

/* The little padlock over each protected thumbnail.
 *
 * ROM QUIRK, reproduced: the *data* is indexed by data_label but the grid is
 * album A's unconditionally -- (0xd0, 0x51), one pixel inside A's thumbnail
 * origin at (207, 80) -- and off_y is never added.  It is harmless because
 * album B is never the camera's own file and every call site passes 0, but it
 * is not what the parameter list promises. */
static void AlbumEditThumbnailProtectionFrameDisp(int data_label, int off_x,
                                                  int off_y, u_char alpha) /* 1715 */
{
    DISP_SPRT protect_ds;
    int       i;
    int       j;
    int       photo_num = 0;
    float     x;

    if (album_info[data_label].album_type != ALBUM_TYPE_PHOTO_FILE) {
        return;
    }

    for (i = 0; i < ALBUM_EDIT_THUMBNAIL_ROW; i++) {
        for (j = 0; j < ALBUM_EDIT_THUMBNAIL_COL; j++) {
            if ((album_info[data_label].album_info.pic[photo_num].status
                 & ALBUM_EDIT_PIC_IN_USE) != 0 &&
                (album_info[data_label].album_info.pic[photo_num].status
                 & ALBUM_EDIT_PIC_PROTECT) != 0) {

                x = (float)(0xd0 + j * ALBUM_EDIT_THUMBNAIL_STEP_X);

                CopySprDToSpr(&protect_ds, &album_tex[0x7d]);
                protect_ds.x     = x + (float)off_x;
                protect_ds.y     = (float)(0x51 + i * ALBUM_EDIT_THUMBNAIL_STEP_Y);
                protect_ds.alpha = (u_char)((protect_ds.alpha * alpha) >> 7);
                DispSprD(&protect_ds);
            }
            photo_num++;
        }
    }
}

/* The enlarged picture.  photo_flg is what stops this re-inflating the same
 * image every frame: the request is filed once, and the draw only runs on the
 * frames after it. */
static void AlbumEditPhotoDisp(int album_data, int photo_no, int off_x, int off_y,
                               u_char alpha)                            /* 1759 */
{
    if ((album_info[album_data].album_info.pic[photo_no].status
         & ALBUM_EDIT_PIC_IN_USE) == 0) {
        return;
    }

    if (album_edit_ctrl.photo_flg == 0) {
        UncompressPhoto(album_info[album_data].album_info.pic[photo_no].adr_no);
        album_edit_ctrl.photo_flg = 1;
    }

    if (album_edit_ctrl.photo_flg == 1) {
        DrawPhotoFromWorkAreaAddr((uintptr_t)GetAlbumDataAddr(album_data),
                                  0, 1, 0x40, 0xbe, 0xc4, 0x80, alpha);

        AlbumEditPhotoFrameDisp(0, 0, alpha, GetAlbumCmnTexAddr());
    }
}

/* The panel beside the enlargement: room name, date, time, score and up to
 * three subjects.  The subject list is vertically centred on y = 268 -- one
 * row starts at 0x10c, two at 0x100, three at 0xf4, each 0x18 apart -- which
 * is what the switch on subject_num is for. */
static void AlbumEditPhotoInfoDisp(int album_data, int photo_no, int off_x,
                                   int off_y, u_char alpha)             /* 1794 */
{
    DATE_INFO    date;
    PICTURE_WRK *pic;
    void        *album_edit_tex_addr = GetAlbumEditTexAddr();
    int          i;
    int          subject_num = 0;
    int          msg_y;

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);
    AlbumEditAlbumInfoWinItemDisp(album_info[album_data].album_type, 0, 0, alpha);

    pic = &album_info[album_data].album_info.pic[photo_no];

    if ((pic->status & ALBUM_EDIT_PIC_IN_USE) == 0) {
        return;
    }

    SetDateInfoType(&date, &pic->time);

    if ((u_short)pic->room < 0xf0) {
        PrintMsg(0x4a, pic->room, off_x + 0x14e, off_y + 0xad, 2, alpha, 0);
    }

    PrintNumber_N(date.day.year,  2, off_x + 0x1a4, off_y + 199, 2, alpha, 0, 1, 1);
    PrintMsg(8, 1, off_x + 0x16c, off_y + 199, 2, alpha, 0xa0);
    PrintNumber_N(date.day.month, 2, off_x + 0x17a, off_y + 199, 2, alpha, 0, 1, 1);
    PrintMsg(8, 1, off_x + 0x196, off_y + 199, 2, alpha, 0xa0);
    PrintNumber_N(date.day.day,   2, off_x + 0x150, off_y + 199, 2, alpha, 0, 1, 1);

    PrintNumber_N(date.time.hour, 2, off_x + 0x1d1, off_y + 199, 2, alpha, 0, 1, 1);
    PrintMsg(8, 0, off_x + 0x1ee, off_y + 199, 2, alpha, 0xa0);
    PrintNumber_N(date.time.min,  2, off_x + 0x1f9, off_y + 199, 2, alpha, 0, 1, 1);
    PrintMsg(8, 0, off_x + 0x216, off_y + 199, 2, alpha, 0xa0);
    PrintNumber_N(date.time.sec,  2, off_x + 0x221, off_y + 199, 2, alpha, 0, 1, 1);

    PrintNumber_N(pic->score, 5, off_x + 0x1c0, off_y + 0xdf, 2, alpha, 0, 0, 0);
    PrintMsg(8, 2, off_x + 0x21c, off_y + 0xdf, 2, alpha, 0);

    for (i = 0; i < ALBUM_EDIT_SUBJECT_MAX; i++) {
        if (pic->maSubject[i].type < 0) {
            break;
        }
        subject_num++;
    }

    switch (subject_num) {
    case 1:  msg_y = 0x10c; break;
    case 2:  msg_y = 0x100; break;
    case 3:  msg_y = 0xf4;  break;
    default: msg_y = 0xf4;  break;
    }
    msg_y += off_y;

    for (i = 0; i < ALBUM_EDIT_SUBJECT_MAX; i++) {
        if (pic->maSubject[i].type < 0) {
            return;
        }

        if (pic->maSubject[i].obj_no < 0 ||
            pic->maSubject[i].obj_no >= GetMsgIDNumMax(pic->maSubject[i].type)) {
            PRINT_ASSERT("Error! %s msg_id %d", __FUNCTION__,
                         pic->maSubject[i].obj_no);                     /* 1905 */
        }

        PrintMsg(pic->maSubject[i].type, pic->maSubject[i].obj_no,
                 off_x + 0x14e, msg_y, 2, alpha, 0);

        msg_y += 0x18;
    }
}

/* The action menu.  Rows whose condition fails are not drawn at all, and the
 * stack is bottom-aligned -- both origins move up by 0x11 per drawn row -- so
 * a three-row menu and a five-row one end at the same place.
 *
 * ROM QUIRK, reproduced: `alpha` is overwritten with 128 before the animation
 * controller sees it, so the caller's value is discarded and the menu fades
 * only on its own animation.  AlbumEditSortMenuDisp() below is the same
 * function without that store. */
static void AlbumEditMenuDisp(int album_data, int album_type, int off_x, int off_y,
                              u_char alpha)                             /* 1926 */
{
    /* rdata 3a0a58.  All five rows share one x. */
    static const int menu_item_x[ALBUM_EDIT_MENU_MAX] = { 9, 9, 9, 9, 9 };

    void  *album_edit_tex_addr = GetAlbumEditTexAddr();
    int    i;
    int    menu_num = 0;
    int    frame_top_y;
    int    item_top_y;
    float  sel_frame_scl;
    float  non_sel_frame_scl;

    for (i = 0; i < ALBUM_EDIT_MENU_MAX; i++) {
        if (CheckAlbumMenuCondition(i)) {
            menu_num++;
        }
    }

    frame_top_y = 0xee - menu_num * 0x11;
    item_top_y  = 0xf2 - menu_num * 0x11;

    alpha             = 0x80;
    sel_frame_scl     = 1.3f;
    non_sel_frame_scl = 1.0f;

    AlbumEditMenuAnimCtrl(&album_edit_disp.menu_anim_step,
                          &album_edit_disp.menu_anim_timer,
                          &alpha, &sel_frame_scl, &non_sel_frame_scl);

    for (i = 0; i < ALBUM_EDIT_MENU_MAX; i++) {
        if (!CheckAlbumMenuCondition(i)) {
            continue;
        }

        if (album_edit_ctrl.menu_csr == i) {
            AlbumMenuSelFrameDisp(album_data, 0xc, frame_top_y, alpha,
                                  sel_frame_scl, 0x80);
        } else {
            AlbumMenuNonSelFrameDisp(album_data, 0xc, frame_top_y, alpha,
                                     non_sel_frame_scl);
        }
        frame_top_y += ALBUM_EDIT_MENU_STEP_Y;

        PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);

        AlbumMenuItemDisp(i, menu_item_x[i], item_top_y, alpha);
        item_top_y += ALBUM_EDIT_MENU_STEP_Y;
    }
}

/* ---- the ten mode draw routines ----------------------------------------- *
 * The first four share a skeleton: the base view, the current album's static
 * cursor and flare, the protect frames, then whatever the mode adds, then the
 * info panel and the caption. */

static void AlbumEditTopDisp(int off_x, int off_y, u_char alpha)        /* 2014 */
{
    void *album_cmn_tex_addr = GetAlbumCmnTexAddr();
    int   current_album      = GetCurrentAlbum();
    int   photo_no           = GetAlbumPhotoNo();

    AlbumEditBaseDisp(current_album, photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    if (current_album == 0) {
        AlbumEditAlbumACursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditPhotoInfoDisp(current_album, photo_no, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);
}

static void AlbumEditTopMenuDisp(int off_x, int off_y, u_char alpha)    /* 2066 */
{
    void *album_cmn_tex_addr = GetAlbumCmnTexAddr();
    int   current_album      = GetCurrentAlbum();
    int   photo_no           = GetAlbumPhotoNo();

    AlbumEditBaseDisp(current_album, photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    if (current_album == 0) {
        AlbumEditAlbumACursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);

    AlbumEditMenuDisp(GetCurrentAlbum(),
                      album_info[GetCurrentAlbum()].album_type,
                      off_x, off_y, alpha);

    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);
    AlbumEditPhotoInfoDisp(current_album, photo_no, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);
}

static void AlbumEditSortDisp(int off_x, int off_y, u_char alpha)       /* 2123 */
{
    void *album_cmn_tex_addr = GetAlbumCmnTexAddr();
    int   current_album      = GetCurrentAlbum();
    int   photo_no           = GetAlbumPhotoNo();

    AlbumEditBaseDisp(current_album, photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    if (current_album == 0) {
        AlbumEditAlbumACursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditSortMenuDisp(0, 0, alpha);
    AlbumEditPhotoInfoDisp(current_album, photo_no, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);
}

/* The sort rows, drawn over the menu with a header plate above them.
 *
 * ROM QUIRK, reproduced: the selected row's frame is drawn with
 * non_sel_frame_scl, not sel_frame_scl -- so the sort rows never scale up
 * even though the scale was computed for them.  Only the header plate above
 * the rows uses sel_frame_scl. */
static void AlbumEditSortMenuDisp(int off_x, int off_y, u_char alpha)   /* 2168 */
{
    /* sdata 3ef3d0.  Both rows share one x, as the menu's do. */
    static int sort_x_tbl[ALBUM_EDIT_SORT_MAX] = { 9, 9 };

    DISP_SPRT sort_ds;
    int       i;
    int       current_album = GetCurrentAlbum();
    void     *album_edit_tex_addr = GetAlbumEditTexAddr();
    float     sel_frame_scl;
    float     non_sel_frame_scl;
    int       y;

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);

    sel_frame_scl     = 1.3f;
    non_sel_frame_scl = 1.0f;

    AlbumEditMenuAnimCtrl(&album_edit_disp.sort_anim_step,
                          &album_edit_disp.sort_anim_timer,
                          &alpha, &sel_frame_scl, &non_sel_frame_scl);

    AlbumMenuSelFrameDisp(current_album, off_x + 0xc, off_y + 0xbd, alpha,
                          sel_frame_scl, 0x80);

    CopySprDToSpr(&sort_ds, &album_tex[0x6f]);
    sort_ds.x     = (float)(off_x + 9);
    sort_ds.y     = (float)(off_y + 0xc1);
    sort_ds.alpha = (u_char)((sort_ds.alpha * alpha) >> 7);
    DispSprD(&sort_ds);

    y = 0xe0;

    for (i = 0; i < ALBUM_EDIT_SORT_MAX; i++) {
        if (album_edit_ctrl.sort_csr == i) {
            AlbumMenuSelFrameDisp(current_album, 0xc, y, alpha,
                                  non_sel_frame_scl, 0x80);
        } else {
            AlbumMenuNonSelFrameDisp(current_album, 0xc, y, alpha,
                                     non_sel_frame_scl);
        }

        CopySprDToSpr(&sort_ds, &album_tex[0x72 + i]);
        sort_ds.x     = (float)(sort_x_tbl[i] + off_x);
        sort_ds.y     = (float)(y + 4);
        sort_ds.alpha = (u_char)((sort_ds.alpha * alpha) >> 7);
        DispSprD(&sort_ds);

        y += ALBUM_EDIT_MENU_STEP_Y;
    }
}

/* The slot picker draws the *destination* album as the base view, keeps a
 * static cursor on the source picture and a pulsing one on the target slot. */
static void AlbumEditCopyPosSelDisp(int off_x, int off_y, u_char alpha) /* 2236 */
{
    void  *album_cmn_tex_addr = GetAlbumCmnTexAddr();
    int    photo_no           = album_copy_ctrl.photo_no;
    u_char csr_rgb            = 0x80;

    AlbumEditBaseDisp(album_copy_ctrl.album_data ^ 1,
                      album_copy_ctrl.photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    if (album_copy_ctrl.album_data == 0) {
        AlbumEditAlbumACursorDisp(album_copy_ctrl.copy_photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(album_copy_ctrl.copy_photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    Zero2Anim2D_CsrAnimCtrl(&album_edit_disp.copy_csr_timer, &csr_rgb);

    if (album_copy_ctrl.album_data == 1) {
        AlbumEditAlbumACursorDisp(album_copy_ctrl.photo_no, 0, 0, alpha, csr_rgb);
    } else {
        AlbumEditAlbumBCursorDisp(album_copy_ctrl.photo_no, 0, 0, alpha, csr_rgb);
    }

    /* The menu stays up behind the picker unless the player came in on the
     * R2 shortcut, which never opened it. */
    if (album_copy_ctrl.short_cut_flg == 0) {
        AlbumEditMenuDisp(album_copy_ctrl.album_data,
                          album_info[album_copy_ctrl.album_data].album_type,
                          off_x, off_y, alpha);
        PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditPhotoInfoDisp(album_copy_ctrl.album_data ^ 1, photo_no, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);
}

/* The copy confirm window draws the *source* album as its base view -- the
 * opposite of the picker above -- so the player sees the picture being copied
 * rather than where it will land. */
static void AlbumEditCopyConfDisp(int off_x, int off_y, u_char alpha)   /* 2307 */
{
    void  *album_cmn_tex_addr  = GetAlbumCmnTexAddr();
    void  *album_edit_tex_addr = GetAlbumEditTexAddr();
    u_char csr_rgb             = 0x80;

    AlbumEditBaseDisp(album_copy_ctrl.album_data,
                      album_copy_ctrl.copy_photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    if (album_copy_ctrl.album_data == 0) {
        AlbumEditAlbumACursorDisp(album_copy_ctrl.copy_photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(album_copy_ctrl.copy_photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    Zero2Anim2D_CsrAnimCtrl(&album_edit_disp.copy_csr_timer, &csr_rgb);

    if (album_copy_ctrl.album_data == 1) {
        AlbumEditAlbumACursorDisp(album_copy_ctrl.photo_no, 0, 0, alpha, csr_rgb);
    } else {
        AlbumEditAlbumBCursorDisp(album_copy_ctrl.photo_no, 0, 0, alpha, csr_rgb);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);

    if (album_copy_ctrl.short_cut_flg == 0) {
        AlbumEditMenuDisp(album_copy_ctrl.album_data,
                          album_info[album_copy_ctrl.album_data].album_type,
                          off_x, off_y, alpha);
    }

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);
    AlbumConfYesNoDisp(album_edit_ctrl.conf_csr, 0, 0, alpha, csr_rgb);
    PrintMsg(0, ALBUM_EDIT_MSG_COPY_CONF, 0x11c, 0xb4, 0x20, alpha, 0);
}

/* Delete pulses the cursor over the picture that is about to go. */
static void AlbumEditDelConfDisp(int off_x, int off_y, u_char alpha)    /* 2391 */
{
    void  *album_cmn_tex_addr  = GetAlbumCmnTexAddr();
    void  *album_edit_tex_addr = GetAlbumEditTexAddr();
    int    current_album       = GetCurrentAlbum();
    int    photo_no            = GetAlbumPhotoNo();
    u_char csr_rgb             = 0x80;

    AlbumEditBaseDisp(current_album, photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    Zero2Anim2D_CsrAnimCtrl(&album_edit_disp.del_csr_timer, &csr_rgb);

    if (current_album == 0) {
        AlbumEditAlbumACursorDisp(photo_no, 0, 0, alpha, csr_rgb);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(photo_no, 0, 0, alpha, csr_rgb);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);

    AlbumEditMenuDisp(current_album, album_info[current_album].album_type,
                      off_x, off_y, alpha);

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);
    AlbumConfYesNoDisp(album_edit_ctrl.conf_csr, 0, 0, alpha, csr_rgb);
    PrintMsg(0, ALBUM_EDIT_MSG_DEL_CONF, 0x125, 0xb4, 0x20, alpha, 0);
}

/* Load pulses the album *frame* rather than the cursor -- the whole album is
 * about to be replaced, not one picture. */
static void AlbumEditLoadConfDisp(int off_x, int off_y, u_char alpha)   /* 2460 */
{
    void  *album_cmn_tex_addr  = GetAlbumCmnTexAddr();
    void  *album_edit_tex_addr = GetAlbumEditTexAddr();
    int    current_album       = GetCurrentAlbum();
    int    photo_no            = GetAlbumPhotoNo();
    u_char rgb                 = 0x80;

    AlbumEditBaseDisp(current_album, photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    Zero2Anim2D_CsrAnimCtrl(&album_edit_disp.album_flare_timer, &rgb);

    if (current_album == 0) {
        AlbumEditAlbumACursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, rgb);
    } else {
        AlbumEditAlbumBCursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, rgb);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);

    AlbumEditMenuDisp(current_album, album_info[current_album].album_type,
                      off_x, off_y, alpha);

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);
    AlbumConfYesNoDisp(album_edit_ctrl.conf_csr, 0, 0, alpha, rgb);
    PrintMsg(0, ALBUM_EDIT_MSG_LOAD_CONF, 0x125, 0xb4, 0x20, alpha, 0);
}

/* The two card screens draw this module's page underneath and then let the
 * card screen draw over it.  Save reuses the menu view; Load reuses its own
 * confirm view, so the album keeps flaring while the card is read. */
static void AlbumEditSaveDisp(int off_x, int off_y, u_char alpha)       /* 2519 */
{
    AlbumEditTopMenuDisp(off_x, off_y, alpha);                          /* 2519 */
    AlbumSaveDispMain();                                                /* 2526 */
}

static void AlbumEditLoadDisp(int off_x, int off_y, u_char alpha)       /* 2541 */
{
    AlbumEditLoadConfDisp(off_x, off_y, alpha);                         /* 2541 */
    AlbumLoadDispMain();                                                /* 2548 */
}

/* The exit window is the only one with no menu behind it -- it is reached
 * from the top view, where no menu was ever open. */
static void AlbumEditExitConfDisp(int off_x, int off_y, u_char alpha)   /* 2570 */
{
    void  *album_cmn_tex_addr  = GetAlbumCmnTexAddr();
    void  *album_edit_tex_addr = GetAlbumEditTexAddr();
    int    current_album       = GetCurrentAlbum();
    int    photo_no            = GetAlbumPhotoNo();
    u_char rgb                 = 0x80;

    AlbumEditBaseDisp(current_album, photo_no, 0, 0, alpha);
    PK2SendVram((uintptr_t)album_cmn_tex_addr, -1, -1, 0);

    if (current_album == 0) {
        AlbumEditAlbumACursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumA_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    } else {
        AlbumEditAlbumBCursorDisp(photo_no, 0, 0, alpha, 0x80);
        AlbumB_CurrentFrameFlareDisp(0, 0, alpha, 0x80);
    }

    AlbumEditThumbnailProtectionFrameDisp(0, 0, 0, alpha);
    AlbumEditCaptionDisp(0, 0, alpha);

    PK2SendVram((uintptr_t)album_edit_tex_addr, -1, -1, 0);

    Zero2Anim2D_CsrAnimCtrl(&album_edit_disp.album_flare_timer, &rgb);
    AlbumConfYesNoDisp(album_edit_ctrl.conf_csr, 0, 0, alpha, rgb);
    PrintMsg(0, ALBUM_EDIT_MSG_EXIT_CONF, 0x118, 0xb4, 0x20, alpha, 0);
}
