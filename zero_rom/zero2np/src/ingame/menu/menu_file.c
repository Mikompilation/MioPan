// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_file.c
//
// The in-game menu's collected-documents page (menu_file.o).  Sixty-two
// functions -- four exports and fifty-eight statics -- two 7-entry dispatch
// tables in .data, one .rodata lookup and five small .sdata tables.  At 0x4c88
// it is the largest page in the menu.
//
// The page is the standard menu shape -- a *_CTRL work block with a
// MENU_REF_CTRL inside it, a menu_wrk.step ladder, an anim_step/anim_timer
// fade, and a texture quintet.  What is its own is that everything is
// multiplied by five: five tabs, five lists, five cursors, five message banks,
// and seven texture slots instead of one.
//
// Facts worth knowing before touching it:
//
//  * `tag_csr` IS the file type.  It selects the tab, the list, the cursor,
//    the message bank and the loaded pak, all with the same value, and
//    file_dat.h's FILE_TYPE_* numbering is what it takes.  There is no
//    separate "which tab" variable anywhere.
//
//  * `mode` is the second index and it is NOT the file type, though its first
//    five values coincide with it.  It says what the page is showing:
//    0/1/2 the document reader (the three text types share one), 3 the
//    photograph viewer, 4 the map viewer, 5 the tab list, 6 the "you are not
//    carrying anything" message.  Both menu_file_pad[] and file_mode_disp[]
//    are indexed by it, and the three text modes point at the same pair of
//    handlers -- which is the whole reason the tables have seven slots.
//
//  * disp_file_data holds five *compacted* lists, not views.
//    MenuFileSetDispData() walks all five types' ids and copies the ones the
//    player holds into consecutive entries, so a row is
//    list[disp_start_pos + i] and the list walks without re-testing anything.
//    Unlike menu_memo.o's, both fields are read back -- through
//    GetMenuFileDispFileID() and GetMenuFileDispFileState().
//
//  * The page has TWO independent open/close fades, exactly as menu_memo.o
//    does.  anim_step/anim_timer is the page's and drives the title;
//    sub_anim_step/sub_anim_timer is the mode's and drives whatever
//    file_mode_disp[mode] draws.  MenuFileDisp() runs MenuInOutAnimCtrl()
//    once for each in that order, and the second call overwrites `alpha`.
//
//  * The picture beside the list is a cross-fade, not a texture slot.  All
//    three viewers (top, photograph, map) hand a CD file number to
//    MenuCrossFadeInStart() and then draw whichever of the two slots
//    GetMenuCrossFadeAlpha() gives them a non-zero alpha for, with the
//    sprite's TEX0 patched to the scratch VRAM page.  That is why moving the
//    cursor is an Out/flip/In on cross_fade_flg rather than a load.
//
//  * The full-size photograph and map pictures are ONE FILE EACH, not a pak:
//    PHT_DTL_000_TM2 + file_id and PIC_DTL_000_TM2 + file_id.  The thumbnails
//    beside them are paks (file_tex_pack[], tim_dat/file_tex_dat.c) reached
//    through PK2SendVramOne().  The tab list's picture is neither -- it is
//    GetFileTexId(), out of file_dat.o's tables.
//
//  * MenuFilePhotoSmallTexDisp() and MenuFileMapSmallTexDisp() draw the
//    previous and next thumbnails by *moving the cursor*: Lup, draw, Ldown,
//    Ldown, draw, Lup.  The four MenuRefMove* calls in a drawing function are
//    not a mistake -- the page has no other way to name a neighbour, and the
//    cursor is back where it started by the time it returns.
//
//  * MenuFileTopListFrameDisp() honours off_x/off_y only in its PrintMsg().
//    The three frame helpers get literal screen coordinates.  Both callers
//    pass (0, 0) so it never shows.
//
//  * MenuFileTopPictureDisp(), MenuFilePhotoSmallTexDisp() and
//    MenuFileMapSmallTexDisp() each call GetMenuFileDispFileID() once and
//    throw the result away.  functions.txt lists no local to receive it in
//    any of the three; kept as found.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_file.o.
// All 4 ZERO2.MAP exports plus the 58 statics, verified 4/4 against the map
// and 68/69 against functions.txt (the one absent entry is the MENU_FILE_DATA
// type_info node, a compiler artifact).  .text is accounted for byte-for-byte
// -- 0x1f42f8..0x1f8f80 = 0x4c88 = 19592 bytes: 74 bodies totalling 19464
// (the 62 real ones plus the four fixed_array boilerplate ones, the
// verifyrange trio and the static-init pair) plus 32 four-byte alignment
// fills, with no gap of 8 bytes or more anywhere.  MENU_FILE_DATA (0x8),
// DISP_FILE_DATA (0x500), MENU_FILE_CTRL (0x58) and MENU_FILE_DISP (0x6) are
// confirmed member-by-member by an offsetof harness.
//
// msg_type_tbl, all five .sdata tables and both SQAR_DAT local initialisers
// are byte-identical to the ROM, diffed out of the compiled .obj; the sprite
// table is in tim_dat/menu_file_dat.c (2400 bytes, likewise byte-identical)
// and the two thumbnail pak numbers in tim_dat/file_tex_dat.c.  The one
// expected DIFF is MenuFileNoReadFrameDisp()'s frame_tbl: MinGW inlines the
// two-iteration loop and folds { 26, 27 } into the two sprite offsets, so
// the table never reaches the object.  Both offsets (0x340, 0x360) and the
// 280.0f step are in the emitted code.
//
// The geometry cross-checks the sprite indices: menu_file_tex[30] sits at
// y = 129 and [32] at 295 = 129 + 180 - 14, which is exactly the scrollbar's
// MENU_FILE_SCROLL_TOP and MENU_FILE_SCROLL_H; the list rows' caps butt at
// 73 + 64 + 18*10 = 317 and 76 + 48 + 20*10 = 324; and the two unread
// brackets are 361 - 81 = 280 apart, which is MenuFileNoReadFrameDisp()'s
// own step.
//
// Function order follows the ROM's, so the /* NNN */ annotations ascend
// monotonically down the file (810 of them).  Every int-returning function
// here carries one note on its shared epilogue -- the closing brace -- so the
// number on each `return` is the epilogue's, the same convention menu_memo.c
// uses for the identical MenuMemoTexLoadWait() shape.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Two things routinely leave a statement with no line of
// its own and those annotations are interpolated into the measured gap:
//
//  * any statement whose first act is a menu_file_ctrl.ref_ctrl[] or
//    .top_csr[] subscript -- the inlined operator[] reports fixed_array.h
//    124/125 and the caller's own note is dropped.  Nearly every function
//    here has at least one, because `file_id = GetMenuFileDispFileID(
//    file_type, menu_file_ctrl.ref_ctrl[file_type].data_pos)` is the file's
//    single most common statement;
//
//  * a store the scheduler put in a jr/jal delay slot (MenuFileOutReq()'s and
//    MenuFileDispInit()'s last field write, and MenuFileDisp()'s `alpha = 0`).
//
// MenuFileDocumentCaptionDisp() measures across a 23-line gap between its two
// arms (2046 and 2070) with nothing emitted in between, and the three
// PrintMsg_Arrange() calls span ten lines each for their ten arguments -- all
// four carry a range rather than a number.  MenuFileTopScrollFrameDisp()'s
// twelve-tile loop carries no annotation on the `for` itself: its `i = 0` was
// folded into the prologue, so the only measurable numbers are the body's.

#include "menu_file.h"

#include "menu.h"                               /* menu_wrk / MENU_BG_TEX_ADRS */
#include "menu_cmn.h"                           /* MenuRefMove* / cross-fade  */
#include "tim_dat/file_tex_dat.h"               /* file_tex_pack[]            */
#include "tim_dat/menu_file_dat.h"              /* menu_file_tex[]            */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_CsrAnimCtrl    */

#include "../item/prg/file.h"                   /* GetPlyrFileState / FileRead */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnWindow              */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_SQAR      */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / GetMsgPageNum   */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array           */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_FILE_*  */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_FILE_STEP_INIT     0
#define MENU_FILE_STEP_LOAD     1
#define MENU_FILE_STEP_MAIN     2
#define MENU_FILE_STEP_OUT      3

/* MENU_FILE_CTRL::sub_step -- the mode's own ladder, deliberately numbered so
 * that MAIN and MOVE line up with menu_wrk.step's.  MenuFileCtrlInit() opens
 * on MAIN, so the first mode never runs INIT/LOAD; every later mode change
 * goes MOVE -> INIT -> LOAD -> MAIN. */
#define MENU_FILE_SUB_INIT      0
#define MENU_FILE_SUB_LOAD      1
#define MENU_FILE_SUB_MAIN      2
#define MENU_FILE_SUB_MOVE      3

/* MENU_FILE_DISP::anim_step / sub_anim_step.  Only START, OUT and END are
 * named here; the rest of the ladder is MenuInOutAnimCtrl()'s business. */
#define MENU_FILE_ANIM_START    0
#define MENU_FILE_ANIM_SHOW     2
#define MENU_FILE_ANIM_OUT      3
#define MENU_FILE_ANIM_END      4

/* MENU_FILE_CTRL::mode -- the index into both dispatch tables.  0..4 shadow
 * FILE_TYPE_*, but 5 and 6 are not file types and mode is never used as one. */
#define MENU_FILE_MODE_POCKETBOOK   0   /* all three text types share one    */
#define MENU_FILE_MODE_SCRAP        1   /*   reader, so the first three      */
#define MENU_FILE_MODE_OLDBOOK      2   /*   slots hold the same pair        */
#define MENU_FILE_MODE_PHOTOGRAPH   3
#define MENU_FILE_MODE_MAP          4
#define MENU_FILE_MODE_TOP          5   /* the tab list                      */
#define MENU_FILE_MODE_NO_HAVE      6   /* "you are not carrying anything"   */
#define MENU_FILE_MODE_NUM          7

/* How many rows a list shows at once. */
#define MENU_FILE_DISP_NUM      6

/* menu_ctrl[] row the page hands back to when it closes. */
#define MENU_STEP_TOP           8

/* Message ids.  A file owns three, consecutive: the name on the row and in
 * the reader, the one-liner under the list, and the body. */
#define FILE_MSG_PER_FILE       3
#define FILE_MSG_NAME           0
#define FILE_MSG_EXP            1
#define FILE_MSG_DATA           2

/* The shared banks: 8 is the common one the reader's page marker comes from,
 * 0x36 the "you are not carrying any" message menu_memo.o also uses. */
#define MSG_BANK_CMN            8
#define MSG_BANK_NO_FILE        0x36

/* menu_file_tex[] indices.  See tim_dat/menu_file_dat.c for the layout. */
#define MF_TITLE                0    /* the word, out of the file-cmn pak    */
#define MF_TITLE_PLATE          1    /* [1],[2] out of the MENU_BG pak       */
#define MF_TOP_WIN              3    /* [3..10]                              */
#define MF_TOP_WIN_NUM          8
#define MF_TAG                  11   /* [11..15], + tag_csr                  */
#define MF_LENS                 16   /* over the picture, 2x and additive    */
#define MF_NONSEL_FRAME_L       19   /* [20] x20 middle, [21] R, [22] tail   */
#define MF_NONSEL_FRAME_M       20
#define MF_NONSEL_FRAME_R       21
#define MF_NONSEL_FRAME_END     22
#define MF_NONSEL_FRAME_NUM     20
#define MF_SEL_FRAME_L          23   /* [24] x18 middle, [25] R              */
#define MF_SEL_FRAME_M          24
#define MF_SEL_FRAME_R          25
#define MF_SEL_FRAME_NUM        18
#define MF_SCROLL_CSR           28   /* [28],[29] -- the pulse-tinted pair   */
#define MF_SCROLL_TOP           30
#define MF_SCROLL_MID           31
#define MF_SCROLL_BTM           32
#define MF_SCROLL_FRAME_TOP     33
#define MF_SCROLL_FRAME_BTM     34
#define MF_SCROLL_FRAME_MID     35
#define MF_SCROLL_FRAME_END     36
#define MF_SCROLL_FRAME_NUM     12
#define MF_TOP_PICTURE          37
#define MF_DOC_WIN              38   /* [38..44]                             */
#define MF_DOC_WIN_NUM          7
#define MF_PHOTO_WIN            49   /* [49..51]                             */
#define MF_PHOTO_WIN_NUM        3
#define MF_PHOTO_PICTURE        52
#define MF_PHOTO_SMALL          53   /* [53] previous, [54] next             */
#define MF_MAP_WIN              55   /* [55..67]                             */
#define MF_MAP_WIN_NUM          13
#define MF_MAP_ARROW            68   /* [68],[69] the crisp pair             */
#define MF_MAP_ARROW_GLOW       70   /* [70],[71] the pair that pulses       */
#define MF_MAP_ARROW_NUM        2
#define MF_MAP_PICTURE          72
#define MF_MAP_SMALL            73   /* [73] previous, [74] next             */

/* The cross-faded picture's scratch VRAM page.  Same TBP as menu_soul.o's
 * ghost photo; the CBP and TEX0 differ because the picture is bigger. */
#define FILE_PICT_TBP           0x2bc0
#define FILE_PICT_CBP           14000
#define FILE_PICT_TEX0          0x2006d6066932abc0ULL

/* The scrollbar's travel: 180 px of bar starting 129 px down.  A list of six
 * or fewer fills the whole thing. */
#define MENU_FILE_SCROLL_H      180
#define MENU_FILE_SCROLL_TOP    129

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1
#define SE_DECIDE   3
#define SE_PAGE     6

/* pad[0] bits in the remapped layout, plus the analogue equivalents.  The tab
 * list walks tabs on auto-repeat (rpt) where the readers turn pages on a
 * single press (one), so both spellings of LEFT/RIGHT are needed. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_DOWN            0x4000
#define PAD_RPT_LEFT            0x8000
#define PAD_RPT_RIGHT           0x2000
#define PAD_ONE_LEFT            0x8000
#define PAD_ONE_RIGHT           0x2000
#define PAD_ONE_L1              0x4
#define PAD_ONE_R1              0x8
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1
#define PAD_ANALOG_LEFT         2
#define PAD_ANALOG_RIGHT        3

/* --------------------------------------------------------------------------
 *  Work
 * ------------------------------------------------------------------------ */

/* Seven texture slots.  The first two are claimed by menu_top.o before the
 * page opens and live for the whole visit; the other five are claimed and
 * released per mode by MenuFileLoadReq() / MenuFileModeMoveLiberate(). */
static void *file_cmn_tex_addr;                             /* sdata 3f2d48 */
static void *file_top_tex_addr;                             /* sdata 3f2d4c */
static void *file_doc_tex_addr;                             /* sdata 3f2d50 */
static void *file_photo_tex_addr;                           /* sdata 3f2d54 */
static void *file_map_tex_addr;                             /* sdata 3f2d58 */
static void *photo_small_tex_addr;                          /* sdata 3f2d5c */
static void *map_small_tex_addr;                            /* sdata 3f2d60 */

static void MenuFilePocketBookPad(void);
static void MenuFilePhotoGraphPad(void);
static void MenuFileMapPad(void);
static void MenuFileTopPad(void);
static void MenuFileNoHavePad(void);

static void MenuFilePocketBookDisp(u_char alpha);
static void MenuFilePhotoDisp(u_char alpha);
static void MenuFileMapDisp(u_char alpha);
static void MenuFileTopDisp(u_char alpha);
static void MenuFileNoHaveDisp(u_char alpha);

/* Both indexed by MENU_FILE_CTRL::mode, and in step with each other.  The
 * first three slots repeat because pocketbook, scrap and oldbook are all read
 * through the same paged document reader. */
static void (*menu_file_pad[MENU_FILE_MODE_NUM])(void) =     /* data 324228 */
{
    MenuFilePocketBookPad,
    MenuFilePocketBookPad,
    MenuFilePocketBookPad,
    MenuFilePhotoGraphPad,
    MenuFileMapPad,
    MenuFileTopPad,
    MenuFileNoHavePad,
};

static void (*file_mode_disp[MENU_FILE_MODE_NUM])(u_char) =  /* data 324248 */
{
    MenuFilePocketBookDisp,
    MenuFilePocketBookDisp,
    MenuFilePocketBookDisp,
    MenuFilePhotoDisp,
    MenuFileMapDisp,
    MenuFileTopDisp,
    MenuFileNoHaveDisp,
};

static MENU_FILE_CTRL menu_file_ctrl;                        /* bss  4b5498 */
static DISP_FILE_DATA disp_file_data;                        /* bss  4b54f0 */
static MENU_FILE_DISP menu_file_disp;                        /* sbss 3f4e20 */

/* Which message bank each file type's text lives in, keyed by tag_csr.  Not
 * in ascending order -- the banks were authored in a different sequence from
 * the tabs. */
static const int msg_type_tbl[FILE_TYPE_MAX] =             /* rodata 3bdb00 */
{
    30,     /* pocketbook */
    32,     /* scrap      */
    29,     /* oldbook    */
    31,     /* photograph */
    27,     /* map        */
};

static void  MenuFileInit(void);
static void  MenuFileCtrlInit(void);
static void  GetMenuFileTexMem(void **tex_addr, int data_label);
static void  MenuFileTexLoadReq(void *tex_addr, int data_label);
static int   MenuFileTexLoadWait(void *tex_addr, int data_label);
static void  MenuFileLoadReq(char mode);
static int   MenuFileLoadWait(char mode);
static void  MenuFileSetDispData(void);
static void  MenuFileMode(void);
static void  MenuFileModeMoveLiberate(char mode);
static void  MenuFileModeMoveReq(char next_mode);
static void  MenuFileOutReq(void);
static MENU_FILE_DATA *GetMenuFileDispData(int file_type, int num);
static int   GetMenuFileDispFileID(int file_type, int num);
static char  GetMenuFileDispFileState(int file_type, int num);
static void  LiberateMenuFileTexMem(void **tex_addr);
static void  MenuFileTexLoadCancel(void *tex_addr, int data_label);
static void  MenuFileDispInit(void);

static void  MenuFileTitleDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopScrollFrameDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopScrollDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopLensDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopPictureDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopTagDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopArrowDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopListFrameDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileTopCaptionDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileDocumentWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileDocumentArrowDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileDocumentCaptionDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileDocumentNameDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileDocumentDataDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileDocumentPageDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoSmallTexDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoCenterTexDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoArrowDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoCaptionDisp(int off_x, int off_y, u_char alpha);
static void  MenuFilePhotoNameDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileMapWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileMapSmallTexDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileMapCenterTexDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileMapArrowDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileMapMsgWinDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileCaptionDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileMapNameDisp(int off_x, int off_y, u_char alpha);
static void  MenuFileNoReadFrameDisp(float x, float y, u_char alpha);
static void  MenuFileBigArrowDisp(float x, float y, u_char alpha, u_char rgb,
                                  char flg);
static void  MenuFileSmallArrowDisp(float x, float y, u_char alpha, u_char rgb,
                                    char flg);
static void  MenuFileSelFrameDisp(float x, float y, u_char alpha);
static void  MenuFileNonSelFrameDisp(float x, float y, u_char alpha);

/* --------------------------------------------------------------------------
 *  Entry / texture
 * ------------------------------------------------------------------------ */

/* Both always-resident paks should already be loaded -- menu_top.c calls
 * MenuFileTexBackGroundLoad() as the hub fades out -- so a null here means the
 * load never happened.  Note the recovery re-requests into the *null* pointer
 * rather than claiming a buffer first; that is the ROM's own code and it can
 * only fire if the hub never ran. */
static void MenuFileInit(void)                                          /* 309 */
{
    MenuFileCtrlInit();                                                 /* 313 */

    MenuCrossFadeInit();                                                /* 316 */

    if (file_cmn_tex_addr == nullptr) {                                 /* 319 */
        PRINT_WARNING("Menu File Cmn Tex Back reading failure\n");      /* 320 */
        MenuFileTexLoadReq(file_cmn_tex_addr,
                           MENU_FILE_CMN_PK2 + GetLanguage());          /* 322 */
    }

    if (file_top_tex_addr == nullptr) {                                 /* 324 */
        PRINT_WARNING("Menu File Top Tex Back reading failure\n");      /* 325 */
        MenuFileTexLoadReq(file_top_tex_addr,
                           MENU_FILE_TOP_PK2 + GetLanguage());          /* 326 */
    }
}

/* Open on the tab list with sub_step already at MAIN, so the first mode skips
 * its own INIT/LOAD steps -- the tab list has no pak of its own to wait for. */
static void MenuFileCtrlInit(void)                                      /* 335 */
{
    int i;

    menu_file_ctrl.sub_step       = MENU_FILE_SUB_MAIN;                 /* 340 */
    menu_file_ctrl.mode           = MENU_FILE_MODE_TOP;                 /* 341 */
    menu_file_ctrl.next_mode      = MENU_FILE_MODE_TOP;                 /* 342 */
    menu_file_ctrl.tag_csr        = 0;                                  /* 343 */
    menu_file_ctrl.cross_fade_flg = 0;                                  /* 344 */

    for (i = 0; i < FILE_TYPE_MAX; i++) {                               /* 346 */
        menu_file_ctrl.top_csr[i] = 0;                                  /* 347 */
        MenuRefCtrlInit(&menu_file_ctrl.ref_ctrl[i],
                        GetPlyrFileTotalNum(i));                        /* 348 */
    }                                                                   /* 349 */
}

/* The two paks the page can never be without.  Called from menu_top.c while
 * the hub is still on screen, so both are resident by the time MenuFile()
 * runs. */
void MenuFileTexBackGroundLoad(void)                                    /* 357 */
{
    if (file_cmn_tex_addr != nullptr) {                                 /* 360 */
        LiberateMenuFileTexMem(&file_cmn_tex_addr);                     /* 361 */
    }

    GetMenuFileTexMem(&file_cmn_tex_addr,
                      MENU_FILE_CMN_PK2 + GetLanguage());               /* 363 */
    MenuFileTexLoadReq(file_cmn_tex_addr,
                       MENU_FILE_CMN_PK2 + GetLanguage());              /* 364 */

    if (file_top_tex_addr != nullptr) {                                 /* 366 */
        LiberateMenuFileTexMem(&file_top_tex_addr);                     /* 367 */
    }

    GetMenuFileTexMem(&file_top_tex_addr,
                      MENU_FILE_TOP_PK2 + GetLanguage());               /* 369 */
    MenuFileTexLoadReq(file_top_tex_addr,
                       MENU_FILE_TOP_PK2 + GetLanguage());              /* 370 */
}

static void GetMenuFileTexMem(void **tex_addr, int data_label)          /* 380 */
{
    if (*tex_addr != nullptr) {                                         /* 383 */
        LiberateMenuFileTexMem(tex_addr);                               /* 384 */
    }

    *tex_addr = mem_utilGetMem((int)GetFileSize(data_label));           /* 388 */
}

static void MenuFileTexLoadReq(void *tex_addr, int data_label)          /* 398 */
{
    FileLoadReqEE(data_label, tex_addr, 2, nullptr, nullptr);           /* 402 */
}

static int MenuFileTexLoadWait(void *tex_addr, int data_label)          /* 414 */
{
    if (FileLoadIsEnd2(data_label, tex_addr) != 0) {                    /* 422 */
        return 1;
    }

    return 0;                                                           /* 427 */
}

/* Claim and start whatever the mode about to open needs.  The three text
 * modes share one pak; the photograph and map modes need two each -- the
 * window art and the thumbnail pak -- and cancel any load still outstanding
 * on the slots first, because a mode can be re-entered before its previous
 * load finished. */
static void MenuFileLoadReq(char mode)                                  /* 434 */
{
    switch (mode) {                                                     /* 437 */
    case MENU_FILE_MODE_POCKETBOOK:
    case MENU_FILE_MODE_SCRAP:
    case MENU_FILE_MODE_OLDBOOK:
        if (file_doc_tex_addr != nullptr) {                             /* 443 */
            LiberateMenuFileTexMem(&file_doc_tex_addr);                 /* 444 */
        }

        GetMenuFileTexMem(&file_doc_tex_addr, MENU_FILE_TXT_PK2);       /* 447 */
        MenuFileTexLoadReq(file_doc_tex_addr, MENU_FILE_TXT_PK2);       /* 449 */
        break;

    case MENU_FILE_MODE_PHOTOGRAPH:
        if (file_photo_tex_addr != nullptr) {                           /* 453 */
            MenuFileTexLoadCancel(file_photo_tex_addr,
                                  MENU_FILE_PHOTO_PK2);                 /* 454 */
            LiberateMenuFileTexMem(&file_photo_tex_addr);               /* 455 */
        }

        if (photo_small_tex_addr != nullptr) {                          /* 457 */
            MenuFileTexLoadCancel(photo_small_tex_addr,
                                  file_tex_pack[0]);                    /* 458 */
            LiberateMenuFileTexMem(&photo_small_tex_addr);              /* 459 */
        }

        GetMenuFileTexMem(&file_photo_tex_addr, MENU_FILE_PHOTO_PK2);   /* 462 */
        MenuFileTexLoadReq(file_photo_tex_addr, MENU_FILE_PHOTO_PK2);   /* 464 */

        GetMenuFileTexMem(&photo_small_tex_addr, file_tex_pack[0]);     /* 467 */
        MenuFileTexLoadReq(photo_small_tex_addr, file_tex_pack[0]);     /* 469 */
        break;

    case MENU_FILE_MODE_MAP:
        if (file_map_tex_addr != nullptr) {                             /* 473 */
            MenuFileTexLoadCancel(file_map_tex_addr, MENU_FILE_MAP_PK2); /* 474 */
            LiberateMenuFileTexMem(&file_map_tex_addr);                 /* 475 */
        }

        if (map_small_tex_addr != nullptr) {                            /* 477 */
            MenuFileTexLoadCancel(map_small_tex_addr, file_tex_pack[1]); /* 478 */
            LiberateMenuFileTexMem(&map_small_tex_addr);                /* 479 */
        }

        GetMenuFileTexMem(&file_map_tex_addr, MENU_FILE_MAP_PK2);       /* 483 */
        MenuFileTexLoadReq(file_map_tex_addr, MENU_FILE_MAP_PK2);       /* 485 */

        GetMenuFileTexMem(&map_small_tex_addr, file_tex_pack[1]);       /* 488 */
        MenuFileTexLoadReq(map_small_tex_addr, file_tex_pack[1]);       /* 490 */
        break;

    case MENU_FILE_MODE_TOP:
    case MENU_FILE_MODE_NO_HAVE:
        break;

    default:
        PRINT_ASSERT("Error!! MenuFileLoadReq");                        /* 496 */
        break;
    }
}

/* The other half.  The tab list waits on its own pak (which is already
 * resident, so it answers at once) and the "no files" mode waits on nothing.
 * The two-pak modes insist on both. */
static int MenuFileLoadWait(char mode)                                  /* 509 */
{
    int res = 0;

    switch (mode) {                                                     /* 516 */
    case MENU_FILE_MODE_TOP:
        if (MenuFileTexLoadWait(file_top_tex_addr,
                                MENU_FILE_TOP_PK2 + GetLanguage()) != 0) { /* 519 */
            res = 1;
        }
        break;                                                          /* 522 */

    case MENU_FILE_MODE_POCKETBOOK:
    case MENU_FILE_MODE_SCRAP:
    case MENU_FILE_MODE_OLDBOOK:
        if (MenuFileTexLoadWait(file_doc_tex_addr,
                                MENU_FILE_TXT_PK2) != 0) {              /* 527 */
            res = 1;
        }
        break;                                                          /* 530 */

    case MENU_FILE_MODE_PHOTOGRAPH:
        if (MenuFileTexLoadWait(file_photo_tex_addr,
                                MENU_FILE_PHOTO_PK2) != 0) {            /* 532 */
            if (MenuFileTexLoadWait(photo_small_tex_addr,
                                    file_tex_pack[0]) != 0) {           /* 533 */
                res = 1;
            }
        }
        break;                                                          /* 537 */

    case MENU_FILE_MODE_MAP:
        if (MenuFileTexLoadWait(file_map_tex_addr,
                                MENU_FILE_MAP_PK2) != 0) {              /* 539 */
            if (MenuFileTexLoadWait(map_small_tex_addr,
                                    file_tex_pack[1]) != 0) {           /* 540 */
                res = 1;
            }
        }
        break;

    /* Every arm's `res = 1` was cross-jumped onto this one, which is why the
     * other five carry no line of their own. */
    case MENU_FILE_MODE_NO_HAVE:
        res = 1;                                                        /* 544 */
        break;

    default:
        PRINT_ASSERT("Error!! MenuFileLoadWait");                       /* 550 */
        break;
    }

    return res;                                                         /* 554 */
}

/* Build all five compacted lists.  Called on entry and again at the bottom of
 * every mode change -- the reader can mark a file read, which changes the
 * state byte the list carries. */
static void MenuFileSetDispData(void)                                   /* 560 */
{
    MENU_FILE_DATA *menu_file_data_tbl[FILE_TYPE_MAX] =
    {
        &disp_file_data.pocketbook[0],
        &disp_file_data.scrap[0],
        &disp_file_data.oldbook[0],
        &disp_file_data.photograph[0],
        &disp_file_data.map[0],
    };

    int  file_type;
    int  file_id;
    int  count;
    char state;

    for (file_type = 0; file_type < FILE_TYPE_MAX; file_type++) {       /* 578 */
        count = 0;                                                      /* 580 */

        for (file_id = 0;
             file_id < GetFileTypeMaxNum(file_type); file_id++) {       /* 582 */
            state = GetPlyrFileState(file_type, file_id);               /* 584 */

            if (state != FILE_STATE_NONE) {                             /* 587 */
                menu_file_data_tbl[file_type][count].file_id = file_id; /* 588 */
                menu_file_data_tbl[file_type][count].state   = state;   /* 589 */

                count++;                                                /* 590 */
            }
        }                                                               /* 592 */
    }                                                                   /* 593 */
}

/* --------------------------------------------------------------------------
 *  Per-frame
 * ------------------------------------------------------------------------ */

/* Step 0 picks the opening tab: the first type the player holds anything of.
 * If none of the five has an entry the page opens on the "no files" message
 * instead, and nothing is cross-faded in. */
void MenuFile(void)                                                     /* 605 */
{
    int i;
    int file_type;
    int file_id;

    if (menu_wrk.step == MENU_FILE_STEP_INIT) {                         /* 615 */
        MenuFileInit();                                                 /* 616 */

        MenuFileSetDispData();                                          /* 617 */

        for (i = 0; i < FILE_TYPE_MAX; i++) {                           /* 619 */
            if (menu_file_ctrl.ref_ctrl[i].data_num != 0) {             /* 620 */
                menu_file_ctrl.tag_csr = i;                             /* 622 */
                break;                                                  /* 623 */
            }
        }

        file_type = menu_file_ctrl.tag_csr;                             /* 627 */
        file_id   = GetMenuFileDispFileID(
                        file_type,
                        menu_file_ctrl.ref_ctrl[file_type].data_pos);   /* 629 */

        if (FILE_TYPE_MAX <= i) {                                       /* 631 */
            menu_file_ctrl.mode = MENU_FILE_MODE_NO_HAVE;               /* 632 */
        }
        else {
            MenuCrossFadeInStart(menu_file_ctrl.cross_fade_flg,
                                 GetFileTexId(file_type, file_id));     /* 635 */
        }

        menu_wrk.step = MENU_FILE_STEP_LOAD;                            /* 638 */
    }

    if (menu_wrk.step == MENU_FILE_STEP_LOAD) {                         /* 641 */
        if (MenuFileTexLoadWait(file_cmn_tex_addr,
                                MENU_FILE_CMN_PK2 + GetLanguage()) != 0) { /* 643 */
            if (MenuFileTexLoadWait(file_top_tex_addr,
                                    MENU_FILE_TOP_PK2 + GetLanguage()) != 0) { /* 645 */
                MenuFileDispInit();                                     /* 647 */

                menu_wrk.step = MENU_FILE_STEP_MAIN;                    /* 649 */
            }
        }
    }

    if (menu_wrk.step == MENU_FILE_STEP_MAIN) {                         /* 654 */
        MenuFileMode();                                                 /* 656 */
    }

    if (menu_wrk.step == MENU_FILE_STEP_OUT) {                          /* 659 */
        if (menu_file_disp.anim_step == MENU_FILE_ANIM_END) {           /* 660 */
            SetNextMenuStep(MENU_STEP_TOP);                             /* 662 */

            MenuFileMemRelease();                                       /* 665 */

            MenuCrossFadeTexLoadCancel(0);                              /* 668 */
            MenuCrossFadeTexLoadCancel(1);                              /* 669 */

            LiberateAllMenuCrossFadeTexMem();                           /* 671 */
        }
    }
}

/* The mode's own four-step ladder, run once a frame while the page is up.
 *
 * Step 1 is where the picture beside the list is chosen, and the three viewers
 * each name it differently: the tab list by texture id, the photograph and map
 * viewers by CD file number.  A mode with no picture parks load_data at -1 and
 * nothing is started. */
static void MenuFileMode(void)                                          /* 682 */
{
    int file_type;
    int file_id;
    int load_data;

    if (menu_file_ctrl.sub_step == MENU_FILE_SUB_INIT) {                /* 693 */
        MenuFileSetDispData();                                          /* 695 */

        MenuCrossFadeInit();                                            /* 697 */
        menu_file_ctrl.cross_fade_flg = 0;                              /* 698 */

        menu_file_ctrl.sub_step = MENU_FILE_SUB_LOAD;                   /* 700 */
    }

    if (menu_file_ctrl.sub_step == MENU_FILE_SUB_LOAD) {                /* 703 */
        if (MenuFileLoadWait(menu_file_ctrl.mode) != 0) {               /* 705 */
            menu_file_disp.sub_anim_step  = MENU_FILE_ANIM_START;       /* 706 */
            menu_file_disp.sub_anim_timer = 0;                          /* 707 */

            file_type = menu_file_ctrl.tag_csr;                         /* 709 */
            file_id   = GetMenuFileDispFileID(
                            file_type,
                            menu_file_ctrl.ref_ctrl[file_type].data_pos); /* 711 */

            switch (menu_file_ctrl.mode) {                              /* 713 */
            case MENU_FILE_MODE_TOP:
                load_data = GetFileTexId(file_type, file_id);           /* 715 */
                break;                                                  /* 716 */

            case MENU_FILE_MODE_PHOTOGRAPH:
                load_data = file_id + PHT_DTL_000_TM2;                  /* 719 */
                break;

            case MENU_FILE_MODE_MAP:
                load_data = file_id + PIC_DTL_000_TM2;                  /* 722 */
                break;

            default:
                load_data = -1;
                break;
            }

            if (load_data != -1) {                                      /* 727 */
                MenuCrossFadeInStart(menu_file_ctrl.cross_fade_flg,
                                     load_data);                       /* 729 */
            }

            menu_file_ctrl.sub_step = MENU_FILE_SUB_MAIN;               /* 732 */
        }
    }

    if (menu_file_ctrl.sub_step == MENU_FILE_SUB_MAIN) {                /* 736 */
        if (menu_file_pad[menu_file_ctrl.mode] != nullptr) {            /* 738 */
            (*menu_file_pad[menu_file_ctrl.mode])();                    /* 739 */
        }

        /* The "no files" mode has no picture, so nothing to pump. */
        if (menu_file_ctrl.mode != MENU_FILE_MODE_NO_HAVE) {            /* 742 */
            MenuCmnCrossFade();                                         /* 744 */
        }
    }

    if (menu_file_ctrl.sub_step == MENU_FILE_SUB_MOVE) {                /* 748 */
        if (menu_file_disp.sub_anim_step == MENU_FILE_ANIM_END) {       /* 749 */
            /* Rewind the reader -- the page counter is shared with every
             * other paged message in the game. */
            SetMsgFirstPage();                                          /* 751 */

            MenuFileModeMoveLiberate(menu_file_ctrl.mode);              /* 754 */

            MenuCrossFadeTexLoadCancel(0);                              /* 757 */
            MenuCrossFadeTexLoadCancel(1);                              /* 758 */

            LiberateAllMenuCrossFadeTexMem();                           /* 761 */

            menu_file_ctrl.sub_step = MENU_FILE_SUB_INIT;               /* 763 */
            menu_file_ctrl.mode     = menu_file_ctrl.next_mode;         /* 764 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Pad
 * ------------------------------------------------------------------------ */

/* The tab list.  Up/down walk the rows, left/right (and L1/R1) walk the tabs,
 * skipping any tab the player holds nothing in -- the bounded loop gives up
 * after five tries and leaves load_flg clear, which is what happens if the
 * player somehow has nothing at all. */
static void MenuFileTopPad(void)                                        /* 774 */
{
    int    file_type;
    int    file_id;
    int    disp_num;
    u_char load_flg;
    int    i;

    file_type = menu_file_ctrl.tag_csr;                                 /* 782 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 784 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 786 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    load_flg = 0;                                                       /* 790 */

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 794 */
        if (MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                              &menu_file_ctrl.top_csr[file_type],
                              disp_num, MENU_FILE_DISP_NUM) != 0) {     /* 795 */
            load_flg = 1;                                               /* 796 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 800 */
        if (MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                                &menu_file_ctrl.top_csr[file_type],
                                disp_num, MENU_FILE_DISP_NUM) != 0) {   /* 801 */
            load_flg = 1;                                               /* 802 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_LEFT) || (pad[0].one & PAD_ONE_L1)
             || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {                     /* 806 */
        for (i = 0; i < FILE_TYPE_MAX; i++) {                           /* 807 */
            menu_file_ctrl.tag_csr =
                (menu_file_ctrl.tag_csr + FILE_TYPE_MAX - 1)
                % FILE_TYPE_MAX;                                        /* 808 */

            if (menu_file_ctrl.ref_ctrl[menu_file_ctrl.tag_csr].data_num
                != 0) {                                                 /* 810 */
                break;                                                  /* 811 */
            }
        }                                                               /* 813 */

        if (i != FILE_TYPE_MAX) {                                       /* 815 */
            load_flg = 1;                                               /* 816 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT) || (pad[0].one & PAD_ONE_R1)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 820 */
        for (i = 0; i < FILE_TYPE_MAX; i++) {                           /* 821 */
            menu_file_ctrl.tag_csr =
                (menu_file_ctrl.tag_csr + 1) % FILE_TYPE_MAX;           /* 822 */

            if (menu_file_ctrl.ref_ctrl[menu_file_ctrl.tag_csr].data_num
                != 0) {                                                 /* 824 */
                break;                                                  /* 825 */
            }
        }                                                               /* 827 */

        if (i != FILE_TYPE_MAX) {                                       /* 829 */
            load_flg = 1;                                               /* 830 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 834 */
        SystemBankPlay(SE_DECIDE, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 835 */

        file_id = GetMenuFileDispFileID(
                      file_type,
                      menu_file_ctrl.ref_ctrl[file_type].data_pos);     /* 838 */

        /* Opening a file is what marks it read; the list is rebuilt at the
         * bottom of the mode change, so the bracket disappears behind it. */
        FileRead(file_type, file_id);                                   /* 840 */

        MenuFileModeMoveReq((char)menu_file_ctrl.tag_csr);              /* 841 */
    }
    else if (*paddat[1] == 1) {                                         /* 844 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 845 */
        MenuFileOutReq();                                               /* 846 */
    }

    /* Any move -- row or tab -- re-cross-fades the picture. */
    if (load_flg != 0) {                                                /* 850 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 851 */

        file_type = menu_file_ctrl.tag_csr;                             /* 853 */
        file_id   = GetMenuFileDispFileID(
                        file_type,
                        menu_file_ctrl.ref_ctrl[file_type].data_pos);   /* 854 */

        MenuCrossFadeOutStart(menu_file_ctrl.cross_fade_flg);           /* 856 */
        menu_file_ctrl.cross_fade_flg ^= 1;                             /* 857 */

        MenuCrossFadeInStart(menu_file_ctrl.cross_fade_flg,
                             GetFileTexId(file_type, file_id));         /* 858 */
    }
}

/* The paged document reader.  Left and right are single presses, not
 * auto-repeat, and both wrap: page 0 goes back to the last page and the last
 * page goes forward to 0.  The cue only fires if the page actually changed,
 * which is why the count is latched before the move and compared after. */
static void MenuFilePocketBookPad(void)                                 /* 866 */
{
    int file_type;
    int file_id;
    int page_num;

    file_type = menu_file_ctrl.tag_csr;                                 /* 872 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 874 */

    page_num = GetNowMsgPageNum();                                      /* 876 */

    if ((pad[0].one & PAD_ONE_LEFT) || (pad[0].one & PAD_ONE_L1)
        || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {                          /* 880 */
        if (GetNowMsgPageNum() == 0) {                                  /* 882 */
            SetMsgPage((char)(GetMsgPageNum(
                                  msg_type_tbl[file_type],
                                  file_id * FILE_MSG_PER_FILE
                                      + FILE_MSG_DATA) - 1));           /* 883 */
        }
        else {
            MesSetBeforePage();                                         /* 886 */
        }

        if (page_num != GetNowMsgPageNum()) {                           /* 889 */
            SystemBankPlay(SE_PAGE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 890 */
        }
    }
    else if ((*paddat[0] == 1) || (pad[0].one & PAD_ONE_RIGHT)
             || (pad[0].one & PAD_ONE_R1)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 894 */
        if (GetNowMsgPageNum()
            == GetMsgPageNum(msg_type_tbl[file_type],
                             file_id * FILE_MSG_PER_FILE
                                 + FILE_MSG_DATA) - 1) {                /* 896 */
            SetMsgPage(0);                                              /* 897 */
        }
        else {
            MesSetNextPage();                                           /* 900 */
        }

        if (page_num != GetNowMsgPageNum()) {                           /* 903 */
            SystemBankPlay(SE_PAGE, 1, 0, 0, (SND_3D_SET *)nullptr,
                           0x3200, 0x1000);                             /* 904 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 908 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 909 */
        MenuFileModeMoveReq(MENU_FILE_MODE_TOP);                        /* 910 */
    }
}

/* The photograph viewer.  Left and right step through the list rather than
 * paging, and every step marks the newly shown photograph read -- there is no
 * confirm button here because looking at it IS reading it. */
static void MenuFilePhotoGraphPad(void)                                 /* 919 */
{
    u_char load_flg;
    int    file_type;
    int    file_id;
    int    disp_num;

    file_type = menu_file_ctrl.tag_csr;                                 /* 926 */

    load_flg = 0;                                                       /* 927 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 929 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 931 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    if ((pad[0].rpt & PAD_RPT_LEFT) || (pad[0].one & PAD_ONE_L1)
        || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {                          /* 939 */
        if (MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                              &menu_file_ctrl.top_csr[file_type],
                              disp_num, MENU_FILE_DISP_NUM) != 0) {     /* 942 */
            load_flg = 1;
        }

        file_id = GetMenuFileDispFileID(
                      file_type,
                      menu_file_ctrl.ref_ctrl[file_type].data_pos);     /* 945 */
        FileRead(file_type, file_id);                                   /* 947 */
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT) || (pad[0].one & PAD_ONE_R1)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 950 */
        if (MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                                &menu_file_ctrl.top_csr[file_type],
                                disp_num, MENU_FILE_DISP_NUM) != 0) {   /* 953 */
            load_flg = 1;
        }

        file_id = GetMenuFileDispFileID(
                      file_type,
                      menu_file_ctrl.ref_ctrl[file_type].data_pos);     /* 956 */
        FileRead(file_type, file_id);                                   /* 958 */
    }
    else if (*paddat[1] == 1) {                                         /* 961 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 962 */
        MenuFileModeMoveReq(MENU_FILE_MODE_TOP);                        /* 963 */
    }

    if (load_flg != 0) {                                                /* 967 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 968 */

        file_type = menu_file_ctrl.tag_csr;                             /* 969 */
        file_id   = GetMenuFileDispFileID(
                        file_type,
                        menu_file_ctrl.ref_ctrl[file_type].data_pos);   /* 970 */

        MenuCrossFadeOutStart(menu_file_ctrl.cross_fade_flg);           /* 972 */
        menu_file_ctrl.cross_fade_flg ^= 1;                             /* 973 */

        MenuCrossFadeInStart(menu_file_ctrl.cross_fade_flg,
                             file_id + PHT_DTL_000_TM2);                /* 974 */
    }
}

/* The map viewer.  Identical to the photograph viewer bar the picture's file
 * number; the ROM has both written out in full. */
static void MenuFileMapPad(void)                                        /* 981 */
{
    u_char load_flg;
    int    file_type;
    int    file_id;
    int    disp_num;

    file_type = menu_file_ctrl.tag_csr;                                 /* 988 */

    load_flg = 0;                                                       /* 989 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 990 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 992 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    if ((pad[0].rpt & PAD_RPT_LEFT) || (pad[0].one & PAD_ONE_L1)
        || GetPadAnalogRpt(PAD_ANALOG_LEFT)) {                          /* 1000 */
        if (MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                              &menu_file_ctrl.top_csr[file_type],
                              disp_num, MENU_FILE_DISP_NUM) != 0) {     /* 1003 */
            load_flg = 1;
        }

        file_id = GetMenuFileDispFileID(
                      file_type,
                      menu_file_ctrl.ref_ctrl[file_type].data_pos);     /* 1006 */
        FileRead(file_type, file_id);                                   /* 1008 */
    }
    else if ((pad[0].rpt & PAD_RPT_RIGHT) || (pad[0].one & PAD_ONE_R1)
             || GetPadAnalogRpt(PAD_ANALOG_RIGHT)) {                    /* 1011 */
        if (MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                                &menu_file_ctrl.top_csr[file_type],
                                disp_num, MENU_FILE_DISP_NUM) != 0) {   /* 1014 */
            load_flg = 1;
        }

        file_id = GetMenuFileDispFileID(
                      file_type,
                      menu_file_ctrl.ref_ctrl[file_type].data_pos);     /* 1017 */
        FileRead(file_type, file_id);                                   /* 1019 */
    }
    else if (*paddat[1] == 1) {                                         /* 1022 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1023 */
        MenuFileModeMoveReq(MENU_FILE_MODE_TOP);                        /* 1024 */
    }

    if (load_flg != 0) {                                                /* 1028 */
        SystemBankPlay(SE_CURSOR, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1029 */

        file_type = menu_file_ctrl.tag_csr;                             /* 1030 */
        file_id   = GetMenuFileDispFileID(
                        file_type,
                        menu_file_ctrl.ref_ctrl[file_type].data_pos);   /* 1031 */

        MenuCrossFadeOutStart(menu_file_ctrl.cross_fade_flg);           /* 1033 */
        menu_file_ctrl.cross_fade_flg ^= 1;                             /* 1034 */

        MenuCrossFadeInStart(menu_file_ctrl.cross_fade_flg,
                             file_id + PIC_DTL_000_TM2);                /* 1035 */
    }
}

/* Nothing to walk: the only thing this mode can do is leave. */
static void MenuFileNoHavePad(void)                                     /* 1042 */
{
    if (*paddat[1] == 1) {                                              /* 1046 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, (SND_3D_SET *)nullptr,
                       0x3200, 0x1000);                                 /* 1047 */
        MenuFileOutReq();                                               /* 1048 */
    }
}

/* --------------------------------------------------------------------------
 *  Mode change
 * ------------------------------------------------------------------------ */

/* Give back whatever the mode that is closing claimed.  The mirror of
 * MenuFileLoadReq(), and the same seven-way switch. */
static void MenuFileModeMoveLiberate(char mode)                         /* 1057 */
{
    switch (mode) {                                                     /* 1060 */
    case MENU_FILE_MODE_POCKETBOOK:
    case MENU_FILE_MODE_SCRAP:
    case MENU_FILE_MODE_OLDBOOK:
        if (file_doc_tex_addr != nullptr) {                             /* 1066 */
            MenuFileTexLoadCancel(file_doc_tex_addr, MENU_FILE_TXT_PK2); /* 1067 */
            LiberateMenuFileTexMem(&file_doc_tex_addr);                 /* 1068 */
        }
        break;

    case MENU_FILE_MODE_PHOTOGRAPH:
        if (file_photo_tex_addr != nullptr) {                           /* 1072 */
            MenuFileTexLoadCancel(file_photo_tex_addr,
                                  MENU_FILE_PHOTO_PK2);                 /* 1073 */
            LiberateMenuFileTexMem(&file_photo_tex_addr);               /* 1074 */
        }

        if (photo_small_tex_addr != nullptr) {                          /* 1076 */
            MenuFileTexLoadCancel(photo_small_tex_addr,
                                  file_tex_pack[0]);                    /* 1077 */
            LiberateMenuFileTexMem(&photo_small_tex_addr);              /* 1078 */
        }
        break;

    case MENU_FILE_MODE_MAP:
        if (file_map_tex_addr != nullptr) {                             /* 1082 */
            MenuFileTexLoadCancel(file_map_tex_addr, MENU_FILE_MAP_PK2); /* 1083 */
            LiberateMenuFileTexMem(&file_map_tex_addr);                 /* 1084 */
        }

        if (map_small_tex_addr != nullptr) {                            /* 1086 */
            MenuFileTexLoadCancel(map_small_tex_addr, file_tex_pack[1]); /* 1087 */
            LiberateMenuFileTexMem(&map_small_tex_addr);                /* 1088 */
        }
        break;

    case MENU_FILE_MODE_TOP:
    case MENU_FILE_MODE_NO_HAVE:
        break;

    default:
        PRINT_ASSERT("Error! MenuFileModeMoveLiberate");                /* 1094 */
        break;
    }
}

/* Start the cross-fade to another mode.  The new mode's pak load is kicked
 * here, so it has the whole fade-out to arrive; the pad is parked for its
 * duration because MenuFileMode() runs nothing at all while sub_step is
 * MOVE. */
static void MenuFileModeMoveReq(char next_mode)                         /* 1104 */
{
    MenuFileLoadReq(next_mode);                                         /* 1107 */

    menu_file_ctrl.next_mode = next_mode;                               /* 1110 */
    menu_file_ctrl.sub_step  = MENU_FILE_SUB_MOVE;                      /* 1111 */

    menu_file_disp.sub_anim_step  = MENU_FILE_ANIM_OUT;                 /* 1113 */
    menu_file_disp.sub_anim_timer = 0;                                  /* 1114 */
}

/* Close the page.  Both fades are driven out together -- the mode's as well
 * as the page's -- so whatever reader is open goes with the title. */
static void MenuFileOutReq(void)                                        /* 1121 */
{
    menu_wrk.step = MENU_FILE_STEP_OUT;                                 /* 1124 */

    menu_file_disp.anim_step      = MENU_FILE_ANIM_OUT;                 /* 1125 */
    menu_file_disp.anim_timer     = 0;                                  /* 1126 */
    menu_file_disp.sub_anim_step  = MENU_FILE_ANIM_OUT;                 /* 1127 */
    menu_file_disp.sub_anim_timer = 0;                                  /* 1128 */
}

/* --------------------------------------------------------------------------
 *  List access
 * ------------------------------------------------------------------------ */

/* The five lists are separate fixed_arrays with different bounds, so this is a
 * switch rather than a table lookup -- each arm carries its own inlined bounds
 * check. */
static MENU_FILE_DATA *GetMenuFileDispData(int file_type, int num)      /* 1138 */
{
    MENU_FILE_DATA *data = nullptr;                                     /* 1141 */

    switch (file_type) {                                                /* 1143 */
    case FILE_TYPE_POCKETBOOK:
        data = &disp_file_data.pocketbook[num];                         /* 1146 */
        break;
    case FILE_TYPE_SCRAP:
        data = &disp_file_data.scrap[num];                              /* 1149 */
        break;
    case FILE_TYPE_OLDBOOK:
        data = &disp_file_data.oldbook[num];                            /* 1152 */
        break;
    case FILE_TYPE_PHOTOGRAPH:
        data = &disp_file_data.photograph[num];                         /* 1155 */
        break;
    case FILE_TYPE_MAP:
        data = &disp_file_data.map[num];                                /* 1158 */
        break;
    default:
        PRINT_ASSERT("Error! GetMenuFileDispData");                     /* 1160 */
        break;
    }

    return data;                                                        /* 1164 */
}

static int GetMenuFileDispFileID(int file_type, int num)                /* 1173 */
{
    return GetMenuFileDispData(file_type, num)->file_id;                /* 1177 */
}                                                                       /* 1179 */

static char GetMenuFileDispFileState(int file_type, int num)            /* 1188 */
{
    return GetMenuFileDispData(file_type, num)->state;                  /* 1192 */
}                                                                       /* 1194 */

/* --------------------------------------------------------------------------
 *  Release
 * ------------------------------------------------------------------------ */

/* Cancel every outstanding load first, then free every slot.  menu.c's
 * MenuRelease() calls this, and so does MenuFile() at the bottom of the page's
 * own fade-out. */
void MenuFileMemRelease(void)                                           /* 1204 */
{
    MenuFileTexLoadCancel(photo_small_tex_addr, file_tex_pack[0]);      /* 1206 */
    MenuFileTexLoadCancel(map_small_tex_addr,   file_tex_pack[1]);      /* 1207 */

    MenuFileTexLoadCancel(file_cmn_tex_addr,
                          MENU_FILE_CMN_PK2 + GetLanguage());           /* 1209 */
    MenuFileTexLoadCancel(file_top_tex_addr,
                          MENU_FILE_TOP_PK2 + GetLanguage());           /* 1210 */
    MenuFileTexLoadCancel(file_doc_tex_addr,   MENU_FILE_TXT_PK2);      /* 1211 */
    MenuFileTexLoadCancel(file_photo_tex_addr, MENU_FILE_PHOTO_PK2);    /* 1212 */
    MenuFileTexLoadCancel(file_map_tex_addr,   MENU_FILE_MAP_PK2);      /* 1213 */

    LiberateMenuFileTexMem(&photo_small_tex_addr);                      /* 1217 */
    LiberateMenuFileTexMem(&map_small_tex_addr);                        /* 1218 */

    LiberateMenuFileTexMem(&file_cmn_tex_addr);                         /* 1220 */
    LiberateMenuFileTexMem(&file_top_tex_addr);                         /* 1221 */
    LiberateMenuFileTexMem(&file_doc_tex_addr);                         /* 1222 */
    LiberateMenuFileTexMem(&file_photo_tex_addr);                       /* 1223 */
    LiberateMenuFileTexMem(&file_map_tex_addr);                         /* 1224 */
}

static void LiberateMenuFileTexMem(void **tex_addr)                     /* 1234 */
{
    if (*tex_addr != nullptr) {                                         /* 1237 */
        mem_utilFreeMem(*tex_addr);                                     /* 1238 */
        *tex_addr = nullptr;                                            /* 1239 */
    }
}

static void MenuFileTexLoadCancel(void *tex_addr, int data_label)       /* 1250 */
{
    if (tex_addr != nullptr) {                                          /* 1253 */
        if (MenuFileTexLoadWait(tex_addr, data_label) == 0) {           /* 1255 */
            FileLoadCancel2(data_label, tex_addr, nullptr, nullptr);    /* 1256 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

static void MenuFileDispInit(void)                                      /* 1270 */
{
    menu_file_disp.anim_step      = MENU_FILE_ANIM_START;               /* 1273 */
    menu_file_disp.anim_timer     = 0;                                  /* 1274 */
    menu_file_disp.rgb            = 0x40;                               /* 1275 */
    menu_file_disp.scroll_timer   = 0;                                  /* 1276 */
    menu_file_disp.sub_anim_step  = MENU_FILE_ANIM_START;               /* 1277 */
    menu_file_disp.sub_anim_timer = 0;                                  /* 1278 */
}

/* Two fades, run in order: the page's drives the title, the mode's drives
 * whatever file_mode_disp[mode] draws.  The second MenuInOutAnimCtrl() call
 * overwrites `alpha`, which is what puts the reader on the inner fade and
 * leaves the title on the outer one. */
void MenuFileDisp(void)                                                 /* 1285 */
{
    u_char alpha;

    alpha = 0;                                                          /* 1289 */

    if (menu_wrk.step == MENU_FILE_STEP_MAIN
        || menu_wrk.step == MENU_FILE_STEP_OUT) {                       /* 1292 */
        if (menu_file_disp.anim_step != MENU_FILE_ANIM_END) {           /* 1293 */
            MenuInOutAnimCtrl(&menu_file_disp.anim_step,
                              &menu_file_disp.anim_timer, &alpha);      /* 1295 */

            Zero2Anim2D_CsrAnimCtrl(&menu_file_disp.scroll_timer,
                                    &menu_file_disp.rgb);               /* 1298 */

            MenuFileTitleDisp(0, 0, alpha);                             /* 1301 */

            MenuInOutAnimCtrl(&menu_file_disp.sub_anim_step,
                              &menu_file_disp.sub_anim_timer, &alpha);  /* 1304 */

            if (menu_file_ctrl.sub_step == MENU_FILE_SUB_MAIN
                || menu_file_ctrl.sub_step == MENU_FILE_SUB_MOVE) {     /* 1306 */
                if (menu_file_disp.sub_anim_step != MENU_FILE_ANIM_END) { /* 1307 */
                    if (file_mode_disp[menu_file_ctrl.mode] != nullptr) { /* 1308 */
                        (*file_mode_disp[menu_file_ctrl.mode])(alpha);  /* 1309 */
                    }
                }
            }
        }
    }
}

/* The header.  The plate is two mirrored halves out of the shared menu
 * background pak; the word itself is in the file-common pak, so the VRAM page
 * has to be swapped between them.  Unlike menu_memo.o's, this one writes the
 * two plate halves out in full rather than looping. */
static void MenuFileTitleDisp(int off_x, int off_y, u_char alpha)        /* 1326 */
{
    DISP_SPRT title_ds;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 1330 */

    CopySprDToSpr(&title_ds, &menu_file_tex[MF_TITLE_PLATE]);           /* 1333 */

    title_ds.x = title_ds.x + (float)off_x;
    title_ds.y = title_ds.y + (float)off_y;                             /* 1334 */

    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 1335 */

    DispSprD(&title_ds);                                                /* 1336 */

    CopySprDToSpr(&title_ds, &menu_file_tex[MF_TITLE_PLATE + 1]);       /* 1337 */

    title_ds.x = title_ds.x + (float)off_x;
    title_ds.y = title_ds.y + (float)off_y;                             /* 1338 */

    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 1339 */

    DispSprD(&title_ds);                                                /* 1340 */

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 1343 */

    CopySprDToSpr(&title_ds, &menu_file_tex[MF_TITLE]);                 /* 1345 */

    title_ds.x = title_ds.x + (float)off_x;
    title_ds.y = title_ds.y + (float)off_y;                             /* 1346 */

    title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);             /* 1347 */

    DispSprD(&title_ds);                                                /* 1348 */
}

/* --------------------------------------------------------------------------
 *  The five per-mode drawing bodies
 *
 *  file_mode_disp[] runs exactly one of these a frame, in the ROM's own order:
 *  the tab list first, then the document reader, the photograph viewer, the
 *  map viewer and the "nothing collected" message.  The helpers each is built
 *  from follow further down, grouped by mode as the ROM has them.
 * ------------------------------------------------------------------------ */

/* The one-liner under the list is only drawn for a row that actually holds a
 * file; an empty tab draws the frame and nothing in it. */
static void MenuFileTopDisp(u_char alpha)                               /* 1357 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1360 */

    MenuFileTopPictureDisp(0, 0, alpha);                                /* 1363 */
    MenuFileTopWinDisp(0, 0, alpha);                                    /* 1366 */
    MenuFileTopScrollFrameDisp(0, 0, alpha);                            /* 1369 */
    MenuFileTopScrollDisp(0, 0, alpha);                                 /* 1372 */
    MenuFileTopLensDisp(0, 0, alpha);                                   /* 1375 */
    MenuFileTopTagDisp(0, 0, alpha);                                    /* 1378 */
    MenuFileTopArrowDisp(0, 0, alpha);                                  /* 1381 */
    MenuFileTopListFrameDisp(0, 0, alpha);                              /* 1384 */

    if (GetMenuFileDispFileState(file_type,
                                 menu_file_ctrl.top_csr[file_type])
        != FILE_STATE_NONE) {                                           /* 1387 */
        MenuFileTopMsgWinDisp(0, 0, alpha);                             /* 1389 */
    }

    MenuFileTopCaptionDisp(0, 0, alpha);                                /* 1393 */
}

/* The page arrows are only drawn for a document that actually has more than
 * one page. */
static void MenuFilePocketBookDisp(u_char alpha)                        /* 1403 */
{
    int file_type;
    int file_id;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1408 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 1410 */

    MenuFileDocumentWinDisp(0, 0, alpha);                               /* 1413 */

    if (1 < GetMsgPageNum(msg_type_tbl[file_type],
                          file_id * FILE_MSG_PER_FILE + FILE_MSG_DATA)) { /* 1416 */
        MenuFileDocumentArrowDisp(0, 0, alpha);                         /* 1418 */
    }

    MenuFileDocumentCaptionDisp(0, 0, alpha);                           /* 1422 */
    MenuFileDocumentNameDisp(0, 0, alpha);                              /* 1425 */
    MenuFileDocumentDataDisp(0, 0, alpha);                              /* 1428 */
    MenuFileDocumentPageDisp(0, 0, alpha);                              /* 1431 */
}

static void MenuFilePhotoDisp(u_char alpha)                             /* 1441 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1445 */

    MenuFilePhotoWinDisp(0, 0, alpha);                                  /* 1449 */
    MenuFilePhotoSmallTexDisp(0, 0, alpha);                             /* 1452 */
    MenuFilePhotoCenterTexDisp(0, 0, alpha);                            /* 1455 */

    if (1 < menu_file_ctrl.ref_ctrl[file_type].data_num) {              /* 1457 */
        MenuFilePhotoArrowDisp(0, 0, alpha);                            /* 1459 */
    }

    MenuFilePhotoMsgWinDisp(0, 0, alpha);                               /* 1463 */
    MenuFilePhotoCaptionDisp(0, 0, alpha);                              /* 1466 */
    MenuFilePhotoNameDisp(0, 0, alpha);                                 /* 1469 */
}

static void MenuFileMapDisp(u_char alpha)                               /* 1479 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1483 */

    MenuFileMapWinDisp(0, 0, alpha);                                    /* 1487 */
    MenuFileMapSmallTexDisp(0, 0, alpha);                               /* 1490 */
    MenuFileMapCenterTexDisp(0, 0, alpha);                              /* 1493 */

    if (1 < menu_file_ctrl.ref_ctrl[file_type].data_num) {              /* 1495 */
        MenuFileMapArrowDisp(0, 0, alpha);                              /* 1497 */
    }

    MenuFileMapMsgWinDisp(0, 0, alpha);                                 /* 1501 */
    MenuFileCaptionDisp(0, 0, alpha);                                   /* 1504 */
    MenuFileMapNameDisp(0, 0, alpha);                                   /* 1507 */
}

/* The shared "you are not carrying any" window; menu_memo.o draws message 3 of
 * the same bank and this one message 2. */
static void MenuFileNoHaveDisp(u_char alpha)                            /* 1516 */
{
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, MSG_BANK_NO_FILE);                         /* 1522 */
    SetMsgWinDefData(&msg_win, MSG_BANK_NO_FILE);                       /* 1523 */

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 0x66);                                         /* 1526 */

    PrintMsg(MSG_BANK_NO_FILE, 2, msg_data.pos_x, msg_data.pos_y,
             1, alpha, 0);                                              /* 1530 */

    DrawCmnCapGroup_W(12, 12, alpha, 0);                                /* 1534 */
}

/* --------------------------------------------------------------------------
 *  Mode 5 -- the tab list's own parts
 * ------------------------------------------------------------------------ */

/* The list panel: a black wash under it, then eight frame tiles over it. */
static void MenuFileTopWinDisp(int off_x, int off_y, u_char alpha)       /* 1556 */
{
    DISP_SPRT win_ds;
    DISP_SQAR dsq;
    SQAR_DAT  file_list_bg = { 349, 243, 41, 97, 160, 0, 0, 0, 0x59 };  /* 1560 */
    int       i;

    PK2SendVram((uintptr_t)file_top_tex_addr, -1, -1, 0);               /* 1565 */

    CopySqrDToSqr(&dsq, &file_list_bg);                                 /* 1568 */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 1569 */
    DispSqrD(&dsq);                                                     /* 1570 */

    for (i = 0; i < MF_TOP_WIN_NUM; i++) {                              /* 1573 */
        CopySprDToSpr(&win_ds, &menu_file_tex[MF_TOP_WIN + i]);         /* 1574 */

        win_ds.x = win_ds.x + (float)off_x;
        win_ds.y = win_ds.y + (float)off_y;                             /* 1575 */

        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 1576 */

        DispSprD(&win_ds);                                              /* 1577 */
    }                                                                   /* 1578 */
}

/* The scrollbar's rail: a cap at each end and twelve tiles between them, the
 * tail plate last so it sits over the top cap. */
static void MenuFileTopScrollFrameDisp(int off_x, int off_y, u_char alpha) /* 1589 */
{
    DISP_SPRT scroll_ds;
    int       i;

    PK2SendVram((uintptr_t)file_top_tex_addr, -1, -1, 0);               /* 1594 */

    CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_FRAME_TOP]);     /* 1597 */

    scroll_ds.x = scroll_ds.x + (float)off_x;
    scroll_ds.y = scroll_ds.y + (float)off_y;                           /* 1598 */

    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1599 */

    DispSprD(&scroll_ds);                                               /* 1600 */

    for (i = 0; i < MF_SCROLL_FRAME_NUM; i++) {
        CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_FRAME_MID]); /* 1604 */

        scroll_ds.x = scroll_ds.x + (float)off_x;
        scroll_ds.y = scroll_ds.y + (float)(scroll_ds.h * i)
                      + (float)off_y;                                   /* 1605 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1606 */

        DispSprD(&scroll_ds);                                           /* 1607 */
    }                                                                   /* 1608 */

    CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_FRAME_END]);     /* 1611 */

    scroll_ds.x = scroll_ds.x + (float)off_x;
    scroll_ds.y = scroll_ds.y + (float)off_y;                           /* 1612 */

    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1613 */

    DispSprD(&scroll_ds);                                               /* 1614 */

    CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_FRAME_BTM]);     /* 1617 */

    scroll_ds.x = scroll_ds.x + (float)off_x;
    scroll_ds.y = scroll_ds.y + (float)off_y;                           /* 1618 */

    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1619 */

    DispSprD(&scroll_ds);                                               /* 1620 */
}

/* The scrollbar thumb.
 *
 * A list of six or fewer fills the whole 180-pixel rail; a longer one shrinks
 * by one rail-step per hidden row and slides by one per scrolled row.  The
 * thumb is a top cap, a run of whole middle tiles, a part tile and a bottom
 * cap -- unless the two caps alone are already taller than the thumb, in which
 * case both are squashed to half its height and the middle is skipped
 * entirely.  That is what the `center_size < 0` arm is.
 *
 * The two cursor plates at the end ride on the shared pulse rather than on
 * `alpha`, so they keep blinking while the page is otherwise still. */
static void MenuFileTopScrollDisp(int off_x, int off_y, u_char alpha)    /* 1631 */
{
    DISP_SPRT scroll_ds;
    int       scroll_size;
    int       scroll_y;
    int       center_size;
    int       file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1640 */

    if (menu_file_ctrl.ref_ctrl[file_type].data_num
        < MENU_FILE_DISP_NUM) {                                         /* 1642 */
        scroll_size = MENU_FILE_SCROLL_H;                               /* 1643 */
        scroll_y    = MENU_FILE_SCROLL_TOP;                             /* 1644 */
    }
    else {
        scroll_size = MENU_FILE_SCROLL_H
                      - MENU_FILE_SCROLL_H
                          / menu_file_ctrl.ref_ctrl[file_type].data_num
                          * (menu_file_ctrl.ref_ctrl[file_type].data_num
                             - MENU_FILE_DISP_NUM);                     /* 1647 */
        scroll_y    = MENU_FILE_SCROLL_H
                          / menu_file_ctrl.ref_ctrl[file_type].data_num
                          * menu_file_ctrl.ref_ctrl[file_type].disp_start_pos
                      + MENU_FILE_SCROLL_TOP;                           /* 1649 */
    }

    center_size = scroll_size - (menu_file_tex[MF_SCROLL_TOP].h
                                + menu_file_tex[MF_SCROLL_BTM].h);      /* 1652 */

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 1654 */

    if (0 <= center_size) {                                             /* 1656 */
        CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_TOP]);       /* 1658 */

        scroll_ds.x = scroll_ds.x + (float)off_x;
        scroll_ds.y = (float)(scroll_y + off_y);                        /* 1659 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1660 */

        DispSprD(&scroll_ds);                                           /* 1661 */

        scroll_y = scroll_y + menu_file_tex[MF_SCROLL_TOP].h;           /* 1663 */

        while (0 <= center_size - (int)menu_file_tex[MF_SCROLL_MID].h) { /* 1667 */
            center_size = center_size - menu_file_tex[MF_SCROLL_MID].h; /* 1671 */

            CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_MID]);   /* 1674 */

            scroll_ds.x = scroll_ds.x + (float)off_x;
            scroll_ds.y = (float)(scroll_y + off_y);                    /* 1675 */

            scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);   /* 1676 */

            DispSprD(&scroll_ds);                                       /* 1677 */

            scroll_y = scroll_y + menu_file_tex[MF_SCROLL_MID].h;       /* 1679 */
        }                                                               /* 1680 */

        /* The remainder: the same tile squashed to whatever is left. */
        CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_MID]);       /* 1682 */

        scroll_ds.x = scroll_ds.x + (float)off_x;
        scroll_ds.y = (float)(scroll_y + off_y);                        /* 1683 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1684 */

        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (float)center_size / (float)scroll_ds.h;
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;     /* 1685 */

        DispSprD(&scroll_ds);                                           /* 1686 */

        scroll_y = scroll_y + center_size;                              /* 1688 */

        CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_BTM]);       /* 1691 */

        scroll_ds.x = scroll_ds.x + (float)off_x;
        scroll_ds.y = (float)(scroll_y + off_y);                        /* 1692 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1693 */

        DispSprD(&scroll_ds);                                           /* 1694 */
    }
    else {
        CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_TOP]);       /* 1697 */

        scroll_ds.x = scroll_ds.x + (float)off_x;
        scroll_ds.y = (float)(scroll_y + off_y);                        /* 1698 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1699 */

        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (float)(scroll_size / 2 / scroll_ds.h);
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;     /* 1700 */

        DispSprD(&scroll_ds);                                           /* 1701 */

        CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_BTM]);       /* 1702 */

        scroll_ds.x = scroll_ds.x + (float)off_x;
        scroll_ds.y = (float)(scroll_y + scroll_size / 2 + off_y);      /* 1703 */

        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (float)(scroll_size / 2 / scroll_ds.h);
        scroll_ds.csx = scroll_ds.x;   scroll_ds.csy = scroll_ds.y;     /* 1704 */

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1705 */

        DispSprD(&scroll_ds);                                           /* 1706 */
    }

    CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_CSR]);           /* 1710 */

    scroll_ds.x = scroll_ds.x + (float)off_x;
    scroll_ds.y = scroll_ds.y + (float)off_y;                           /* 1711 */

    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1712 */

    scroll_ds.r = menu_file_disp.rgb;
    scroll_ds.g = menu_file_disp.rgb;
    scroll_ds.b = menu_file_disp.rgb;                                   /* 1713 */

    DispSprD(&scroll_ds);                                               /* 1714 */

    CopySprDToSpr(&scroll_ds, &menu_file_tex[MF_SCROLL_CSR + 1]);       /* 1716 */

    scroll_ds.x = scroll_ds.x + (float)off_x;
    scroll_ds.y = scroll_ds.y + (float)off_y;                           /* 1717 */

    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1718 */

    scroll_ds.r = menu_file_disp.rgb;
    scroll_ds.g = menu_file_disp.rgb;
    scroll_ds.b = menu_file_disp.rgb;                                   /* 1719 */

    DispSprD(&scroll_ds);                                               /* 1720 */
}

/* The glare over the picture: one plate drawn at twice size, additive. */
static void MenuFileTopLensDisp(int off_x, int off_y, u_char alpha)      /* 1731 */
{
    DISP_SPRT lens_ds;

    PK2SendVram((uintptr_t)file_top_tex_addr, -1, -1, 0);               /* 1736 */

    CopySprDToSpr(&lens_ds, &menu_file_tex[MF_LENS]);                   /* 1739 */

    lens_ds.x = lens_ds.x + (float)off_x;
    lens_ds.y = lens_ds.y + (float)off_y;                               /* 1740 */

    lens_ds.scw = 2.0f;   lens_ds.sch = 2.0f;
    lens_ds.csx = lens_ds.x;   lens_ds.csy = lens_ds.y;                 /* 1741 */

    lens_ds.alpha = (u_char)(lens_ds.alpha * alpha >> 7);               /* 1742 */

    lens_ds.alphar = 0x48;                                              /* 1743 */

    DispSprD(&lens_ds);                                                 /* 1744 */
}

/* The picture beside the list.  Both cross-fade slots are drawn, the outgoing
 * one first; outside MENU_FILE_STEP_MAIN the page's own fade takes over, which
 * is what the two `menu_wrk.step` tests do.  An empty tab draws nothing.
 *
 * The GetMenuFileDispFileID() call is the ROM's -- its result is discarded and
 * functions.txt lists no local for it. */
static void MenuFileTopPictureDisp(int off_x, int off_y, u_char alpha)   /* 1756 */
{
    DISP_SPRT file_ds;
    int       disp_num;
    int       file_type;
    u_char    fade_alpha[MENU_CROSS_FADE_NUM];

    file_type = menu_file_ctrl.tag_csr;                                 /* 1765 */

    GetMenuFileDispFileID(file_type,
                          menu_file_ctrl.ref_ctrl[file_type].data_pos); /* 1766 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 1767 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 1769 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    fade_alpha[0] = 0;                                                  /* 1773 */
    fade_alpha[1] = 0;                                                  /* 1774 */

    if (disp_num != 0) {                                                /* 1778 */
        GetMenuCrossFadeAlpha(fade_alpha);                              /* 1780 */

        if (menu_wrk.step != MENU_FILE_STEP_MAIN) {                     /* 1782 */
            fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] = 0;          /* 1783 */
        }

        if (CheckCrossFadeDisp(menu_file_ctrl.cross_fade_flg ^ 1) != 0  /* 1787 */
            && fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] != 0) {    /* 1788 */
            MenuTim2SendVram(
                (u_int *)GetCrossFadeDataAddr(
                             menu_file_ctrl.cross_fade_flg ^ 1),
                FILE_PICT_TBP, FILE_PICT_CBP);                          /* 1789 */

            CopySprDToSpr(&file_ds, &menu_file_tex[MF_TOP_PICTURE]);    /* 1791 */
            file_ds.tex0 = FILE_PICT_TEX0;                              /* 1792 */
            file_ds.alpha = (u_char)(
                file_ds.alpha
                * fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] >> 7);  /* 1794 */
            DispSprD(&file_ds);                                         /* 1795 */
        }

        if (menu_wrk.step != MENU_FILE_STEP_MAIN) {                     /* 1799 */
            fade_alpha[menu_file_ctrl.cross_fade_flg] = alpha;          /* 1800 */
        }

        if (CheckCrossFadeDisp(menu_file_ctrl.cross_fade_flg) != 0      /* 1804 */
            && fade_alpha[menu_file_ctrl.cross_fade_flg] != 0) {        /* 1805 */
            MenuTim2SendVram(
                (u_int *)GetCrossFadeDataAddr(menu_file_ctrl.cross_fade_flg),
                FILE_PICT_TBP, FILE_PICT_CBP);                          /* 1806 */

            CopySprDToSpr(&file_ds, &menu_file_tex[MF_TOP_PICTURE]);    /* 1808 */
            file_ds.tex0 = FILE_PICT_TEX0;                              /* 1809 */
            file_ds.alpha = (u_char)(
                file_ds.alpha
                * fade_alpha[menu_file_ctrl.cross_fade_flg] >> 7);      /* 1811 */
            DispSprD(&file_ds);                                         /* 1812 */
        }
    }
}

/* The tab head.  One plate per file type, picked straight off tag_csr. */
static void MenuFileTopTagDisp(int off_x, int off_y, u_char alpha)       /* 1827 */
{
    DISP_SPRT tag_ds;

    PK2SendVram((uintptr_t)file_top_tex_addr, -1, -1, 0);               /* 1831 */

    CopySprDToSpr(&tag_ds,
                  &menu_file_tex[MF_TAG + menu_file_ctrl.tag_csr]);     /* 1834 */

    tag_ds.x = tag_ds.x + (float)off_x;
    tag_ds.y = tag_ds.y + (float)off_y;                                 /* 1835 */

    tag_ds.alpha = (u_char)(tag_ds.alpha * alpha >> 7);                 /* 1836 */

    DispSprD(&tag_ds);                                                  /* 1837 */
}

/* The L1/R1 hints either side of the tab row. */
static void MenuFileTopArrowDisp(int off_x, int off_y, u_char alpha)     /* 1848 */
{
    MenuFileSmallArrowDisp(41.0f, 66.0f, alpha, menu_file_disp.rgb, 0); /* 1853 */
    MenuFileSmallArrowDisp(579.0f, 66.0f, alpha, menu_file_disp.rgb, 1); /* 1855 */
}

/* The six visible rows: a frame, the unread bracket if the file has not been
 * opened, and the name.
 *
 * off_x / off_y reach only the PrintMsg(); the three frame helpers are given
 * literal screen coordinates.  Both callers pass (0, 0), so it never shows. */
static void MenuFileTopListFrameDisp(int off_x, int off_y, u_char alpha) /* 1866 */
{
    int i;
    int file_type;
    int disp_num;
    int col_label;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1874 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 1875 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 1877 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 1884 */
        if (i == menu_file_ctrl.top_csr[file_type]) {                   /* 1886 */
            MenuFileSelFrameDisp(73.0f, (float)(i * 35 + 116), alpha);  /* 1887 */
            col_label = 4;                                              /* 1888 */
        }
        else {
            MenuFileNonSelFrameDisp(76.0f, (float)(i * 35 + 117), alpha); /* 1891 */
            col_label = 3;                                              /* 1892 */
        }

        if (GetMenuFileDispFileState(
                file_type,
                menu_file_ctrl.ref_ctrl[file_type].disp_start_pos + i)
            == FILE_STATE_HAVE) {                                       /* 1896 */
            MenuFileNoReadFrameDisp(81.0f, (float)(i * 35 + 117), alpha); /* 1897 */
        }

        PrintMsg(msg_type_tbl[file_type],
                 GetMenuFileDispFileID(
                     file_type,
                     menu_file_ctrl.ref_ctrl[file_type].disp_start_pos + i)
                     * FILE_MSG_PER_FILE + FILE_MSG_NAME,
                 off_x + 95, i * 35 + 119 + off_y,
                 col_label, alpha, 0xa0);                               /* 1904 */
    }                                                                   /* 1906 */
}

/* The one-liner under the list. */
static void MenuFileTopMsgWinDisp(int off_x, int off_y, u_char alpha)    /* 1918 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 1923 */

    DrawCmnWindow(0xa0, (float)(off_x + 24), (float)(off_y + 346),
                  592.0f, 100.0f, alpha, 0x66);                         /* 1926 */

    PrintMsg(msg_type_tbl[file_type],
             GetMenuFileDispFileID(
                 file_type, menu_file_ctrl.ref_ctrl[file_type].data_pos)
                 * FILE_MSG_PER_FILE + FILE_MSG_EXP,
             off_x + 48, off_y + 370, 1, alpha, 0xa0);                  /* 1932 */
}

static void MenuFileTopCaptionDisp(int off_x, int off_y, u_char alpha)   /* 1944 */
{
    DrawCmnCapGroup_W(4, 4, alpha, 0);                                  /* 1947 */
}

/* --------------------------------------------------------------------------
 *  Modes 0..2 -- the paged document reader
 * ------------------------------------------------------------------------ */

static void MenuFileDocumentWinDisp(int off_x, int off_y, u_char alpha)  /* 1984 */
{
    DISP_SPRT win_ds;
    DISP_SQAR dsq;
    SQAR_DAT  document_bg = { 500, 272, 70, 121, 160, 0, 0, 0, 0x59 };  /* 1988 */
    int       i;

    PK2SendVram((uintptr_t)file_doc_tex_addr, -1, -1, 0);               /* 1993 */

    CopySqrDToSqr(&dsq, &document_bg);                                  /* 1996 */
    dsq.alpha = (u_char)(dsq.alpha * alpha >> 7);                       /* 1997 */
    DispSqrD(&dsq);                                                     /* 1998 */

    for (i = 0; i < MF_DOC_WIN_NUM; i++) {                              /* 2001 */
        CopySprDToSpr(&win_ds, &menu_file_tex[MF_DOC_WIN + i]);         /* 2002 */

        win_ds.x = win_ds.x + (float)off_x;
        win_ds.y = win_ds.y + (float)off_y;                             /* 2003 */

        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 2004 */

        DispSprD(&win_ds);                                              /* 2005 */
    }                                                                   /* 2006 */
}

static void MenuFileDocumentArrowDisp(int off_x, int off_y, u_char alpha) /* 2015 */
{
    MenuFileBigArrowDisp(16.0f, 243.0f, alpha, menu_file_disp.rgb, 0);  /* 2019 */
    MenuFileBigArrowDisp(595.0f, 243.0f, alpha, menu_file_disp.rgb, 1); /* 2021 */
}

/* Caption group 9 mentions the page buttons, 12 does not.  Lines 2047..2069
 * hold no code -- a 23-line gap between the two arms. */
static void MenuFileDocumentCaptionDisp(int off_x, int off_y, u_char alpha) /* 2033 */
{
    int file_type;
    int file_id;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2038 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 2040 */

    if (1 < GetMsgPageNum(msg_type_tbl[file_type],
                          file_id * FILE_MSG_PER_FILE + FILE_MSG_DATA)) { /* 2043 */
        DrawCmnCapGroup_W(9, 9, alpha, 0);                              /* 2046 */
    }
    else {
        DrawCmnCapGroup_W(12, 12, alpha, 0);                            /* 2070 */
    }
}

/* The title, brushed three times a pixel apart -- the first two in the shadow
 * colour and the last in the ink one, which is what gives it its outline. */
static void MenuFileDocumentNameDisp(int off_x, int off_y, u_char alpha) /* 2090 */
{
    int i;
    int file_type;
    int file_id;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2097 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 2099 */

    for (i = 0; i < 3; i++) {                                           /* 2102 */
        PrintMsg_Arrange(msg_type_tbl[file_type],
                         file_id * FILE_MSG_PER_FILE + FILE_MSG_NAME,
                         off_x + 322 - i, off_y + 71 - i,
                         (i == 2) ? 10 : 11,
                         alpha, 0xa0, 0, 0, 2);                         /* 2103-2112 */
    }                                                                   /* 2113 */
}

static void MenuFileDocumentDataDisp(int off_x, int off_y, u_char alpha) /* 2124 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2129 */

    PrintMsg_P(msg_type_tbl[file_type],
               GetMenuFileDispFileID(
                   file_type, menu_file_ctrl.ref_ctrl[file_type].data_pos)
                   * FILE_MSG_PER_FILE + FILE_MSG_DATA,
               off_x + 80, off_y + 137, 1, alpha, 0xa0, 0, 0);          /* 2135 */
}

/* The "<n> / <total>" readout at the foot of the page; the slash is message 1
 * of the common bank. */
static void MenuFileDocumentPageDisp(int off_x, int off_y, u_char alpha) /* 2144 */
{
    int file_type;
    int file_id;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2149 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 2151 */

    PrintNumber_N(GetNowMsgPageNum() + 1, 1, off_x + 294, off_y + 395,
                  1, alpha, 0, 1, 1);                                   /* 2155 */

    PrintMsg(MSG_BANK_CMN, 1, off_x + 312, off_y + 395, 1, alpha, 0xa0); /* 2158 */

    PrintNumber_N(GetMsgPageNum(msg_type_tbl[file_type],
                                file_id * FILE_MSG_PER_FILE
                                    + FILE_MSG_DATA),
                  1, off_x + 326, off_y + 395, 1, alpha, 0, 1, 1);      /* 2161 */
}

/* --------------------------------------------------------------------------
 *  Mode 3 -- the photograph viewer
 * ------------------------------------------------------------------------ */

static void MenuFilePhotoWinDisp(int off_x, int off_y, u_char alpha)     /* 2175 */
{
    DISP_SPRT win_ds;
    int       i;

    PK2SendVram((uintptr_t)file_photo_tex_addr, -1, -1, 0);             /* 2180 */

    for (i = 0; i < MF_PHOTO_WIN_NUM; i++) {                            /* 2183 */
        CopySprDToSpr(&win_ds, &menu_file_tex[MF_PHOTO_WIN + i]);       /* 2184 */

        win_ds.x = win_ds.x + (float)off_x;
        win_ds.y = win_ds.y + (float)off_y;                             /* 2185 */

        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 2186 */

        DispSprD(&win_ds);                                              /* 2187 */
    }                                                                   /* 2188 */
}

/* The thumbnails either side of the picture.
 *
 * There is no "peek at the neighbour" accessor, so the ROM moves the cursor
 * instead: up one, draw the previous photograph, down twice, draw the next
 * one, up one to put it back.  A list of one has no neighbours and is skipped
 * entirely.
 *
 * The first GetMenuFileDispFileID() call is the ROM's and its result is
 * discarded; functions.txt lists no local for it. */
static void MenuFilePhotoSmallTexDisp(int off_x, int off_y, u_char alpha) /* 2197 */
{
    DISP_SPRT photo_ds;
    int       file_type;
    int       disp_num;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2204 */

    GetMenuFileDispFileID(file_type,
                          menu_file_ctrl.ref_ctrl[file_type].data_pos); /* 2206 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 2207 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 2209 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    if (1 < menu_file_ctrl.ref_ctrl[file_type].data_num) {              /* 2211 */
        MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                          &menu_file_ctrl.top_csr[file_type],
                          disp_num, MENU_FILE_DISP_NUM);                /* 2214 */

        PK2SendVramOne((uintptr_t)photo_small_tex_addr,
                       GetMenuFileDispFileID(
                           file_type,
                           menu_file_ctrl.ref_ctrl[file_type].data_pos),
                       -1, -1, 0);                                      /* 2220 */

        CopySprDToSpr(&photo_ds, &menu_file_tex[MF_PHOTO_SMALL]);       /* 2221 */

        photo_ds.x = photo_ds.x + (float)off_x;
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 2222 */

        photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7);         /* 2223 */

        DispSprD(&photo_ds);                                            /* 2224 */

        MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                            &menu_file_ctrl.top_csr[file_type],
                            disp_num, MENU_FILE_DISP_NUM);              /* 2227 */
        MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                            &menu_file_ctrl.top_csr[file_type],
                            disp_num, MENU_FILE_DISP_NUM);              /* 2229 */

        PK2SendVramOne((uintptr_t)photo_small_tex_addr,
                       GetMenuFileDispFileID(
                           file_type,
                           menu_file_ctrl.ref_ctrl[file_type].data_pos),
                       -1, -1, 0);                                      /* 2233 */

        CopySprDToSpr(&photo_ds, &menu_file_tex[MF_PHOTO_SMALL + 1]);   /* 2234 */

        photo_ds.x = photo_ds.x + (float)off_x;
        photo_ds.y = photo_ds.y + (float)off_y;                         /* 2235 */

        photo_ds.alpha = (u_char)(photo_ds.alpha * alpha >> 7);         /* 2236 */

        DispSprD(&photo_ds);                                            /* 2237 */

        MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                          &menu_file_ctrl.top_csr[file_type],
                          disp_num, MENU_FILE_DISP_NUM);                /* 2240 */
    }
}

/* Same cross-fade shape as MenuFileTopPictureDisp(), but gated on the *mode's*
 * step rather than the page's -- the viewer's picture rides the inner fade. */
static void MenuFilePhotoCenterTexDisp(int off_x, int off_y, u_char alpha) /* 2252 */
{
    int       file_type;
    DISP_SPRT photo_ds;
    u_char    fade_alpha[MENU_CROSS_FADE_NUM];

    file_type = menu_file_ctrl.tag_csr;                                 /* 2260 */

    GetMenuFileDispFileID(file_type,
                          menu_file_ctrl.ref_ctrl[file_type].data_pos); /* 2262 */

    fade_alpha[0] = 0;                                                  /* 2263 */
    fade_alpha[1] = 0;                                                  /* 2264 */

    GetMenuCrossFadeAlpha(fade_alpha);                                  /* 2268 */

    if (menu_file_ctrl.sub_step != MENU_FILE_SUB_MAIN) {                /* 2270 */
        fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] = 0;              /* 2271 */
    }

    if (CheckCrossFadeDisp(menu_file_ctrl.cross_fade_flg ^ 1) != 0      /* 2275 */
        && fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] != 0) {        /* 2276 */
        MenuTim2SendVram(
            (u_int *)GetCrossFadeDataAddr(menu_file_ctrl.cross_fade_flg ^ 1),
            FILE_PICT_TBP, FILE_PICT_CBP);                              /* 2277 */

        CopySprDToSpr(&photo_ds, &menu_file_tex[MF_PHOTO_PICTURE]);     /* 2279 */
        photo_ds.tex0 = FILE_PICT_TEX0;                                 /* 2280 */
        photo_ds.alpha = (u_char)(
            photo_ds.alpha
            * fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] >> 7);      /* 2282 */
        DispSprD(&photo_ds);                                            /* 2283 */
    }

    if (menu_file_ctrl.sub_step != MENU_FILE_SUB_MAIN) {                /* 2287 */
        fade_alpha[menu_file_ctrl.cross_fade_flg] = alpha;              /* 2288 */
    }

    if (CheckCrossFadeDisp(menu_file_ctrl.cross_fade_flg) != 0          /* 2292 */
        && fade_alpha[menu_file_ctrl.cross_fade_flg] != 0) {            /* 2293 */
        MenuTim2SendVram(
            (u_int *)GetCrossFadeDataAddr(menu_file_ctrl.cross_fade_flg),
            FILE_PICT_TBP, FILE_PICT_CBP);                              /* 2294 */

        CopySprDToSpr(&photo_ds, &menu_file_tex[MF_PHOTO_PICTURE]);     /* 2296 */
        photo_ds.tex0 = FILE_PICT_TEX0;                                 /* 2297 */
        photo_ds.alpha = (u_char)(
            photo_ds.alpha
            * fade_alpha[menu_file_ctrl.cross_fade_flg] >> 7);          /* 2299 */
        DispSprD(&photo_ds);                                            /* 2300 */
    }
}

static void MenuFilePhotoArrowDisp(int off_x, int off_y, u_char alpha)   /* 2313 */
{
    MenuFileBigArrowDisp(27.0f, 210.0f, alpha, menu_file_disp.rgb, 0);  /* 2317 */
    MenuFileBigArrowDisp(584.0f, 210.0f, alpha, menu_file_disp.rgb, 1); /* 2319 */
}

static void MenuFilePhotoMsgWinDisp(int off_x, int off_y, u_char alpha)  /* 2331 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2336 */

    DrawCmnWindow(0xa0, (float)(off_x + 24), (float)(off_y + 346),
                  592.0f, 100.0f, alpha, 0x66);                         /* 2339 */

    PrintMsg(msg_type_tbl[file_type],
             GetMenuFileDispFileID(
                 file_type, menu_file_ctrl.ref_ctrl[file_type].data_pos)
                 * FILE_MSG_PER_FILE + FILE_MSG_EXP,
             off_x + 48, off_y + 370, 1, alpha, 0xa0);                  /* 2345 */
}

static void MenuFilePhotoCaptionDisp(int off_x, int off_y, u_char alpha) /* 2357 */
{
    DrawCmnCapGroup_W(11, 11, alpha, 0);                                /* 2360 */
}

static void MenuFilePhotoNameDisp(int off_x, int off_y, u_char alpha)    /* 2388 */
{
    int i;
    int file_type;
    int file_id;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2395 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 2397 */

    for (i = 0; i < 3; i++) {                                           /* 2400 */
        PrintMsg_Arrange(msg_type_tbl[file_type],
                         file_id * FILE_MSG_PER_FILE + FILE_MSG_NAME,
                         off_x + 321, off_y + 70 - i,
                         (i == 2) ? 10 : 11,
                         alpha, 0xa0, 0, 0, 2);                         /* 2401-2410 */
    }                                                                   /* 2411 */
}

/* --------------------------------------------------------------------------
 *  Mode 4 -- the map viewer
 * ------------------------------------------------------------------------ */

static void MenuFileMapWinDisp(int off_x, int off_y, u_char alpha)       /* 2426 */
{
    DISP_SPRT win_ds;
    int       i;

    PK2SendVram((uintptr_t)file_map_tex_addr, -1, -1, 0);               /* 2431 */

    for (i = 0; i < MF_MAP_WIN_NUM; i++) {                              /* 2434 */
        CopySprDToSpr(&win_ds, &menu_file_tex[MF_MAP_WIN + i]);         /* 2435 */

        win_ds.x = win_ds.x + (float)off_x;
        win_ds.y = win_ds.y + (float)off_y;                             /* 2436 */

        win_ds.alpha = (u_char)(win_ds.alpha * alpha >> 7);             /* 2437 */

        DispSprD(&win_ds);                                              /* 2438 */
    }                                                                   /* 2439 */
}

/* The map viewer's thumbnails.  MenuFilePhotoSmallTexDisp() written out again
 * with the other pak and the other pair of sprites. */
static void MenuFileMapSmallTexDisp(int off_x, int off_y, u_char alpha)  /* 2450 */
{
    DISP_SPRT map_ds;
    int       file_type;
    int       disp_num;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2457 */

    GetMenuFileDispFileID(file_type,
                          menu_file_ctrl.ref_ctrl[file_type].data_pos); /* 2459 */

    disp_num = menu_file_ctrl.ref_ctrl[file_type].data_num;             /* 2460 */

    if (MENU_FILE_DISP_NUM < disp_num) {                                /* 2462 */
        disp_num = MENU_FILE_DISP_NUM;
    }

    if (1 < menu_file_ctrl.ref_ctrl[file_type].data_num) {              /* 2464 */
        MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                          &menu_file_ctrl.top_csr[file_type],
                          disp_num, MENU_FILE_DISP_NUM);                /* 2467 */

        PK2SendVramOne((uintptr_t)map_small_tex_addr,
                       GetMenuFileDispFileID(
                           file_type,
                           menu_file_ctrl.ref_ctrl[file_type].data_pos),
                       -1, -1, 0);                                      /* 2472 */

        CopySprDToSpr(&map_ds, &menu_file_tex[MF_MAP_SMALL]);           /* 2473 */

        map_ds.x = map_ds.x + (float)off_x;
        map_ds.y = map_ds.y + (float)off_y;                             /* 2474 */

        map_ds.alpha = (u_char)(map_ds.alpha * alpha >> 7);             /* 2475 */

        DispSprD(&map_ds);                                              /* 2476 */

        MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                            &menu_file_ctrl.top_csr[file_type],
                            disp_num, MENU_FILE_DISP_NUM);              /* 2479 */
        MenuRefMovePadLdown(&menu_file_ctrl.ref_ctrl[file_type],
                            &menu_file_ctrl.top_csr[file_type],
                            disp_num, MENU_FILE_DISP_NUM);              /* 2481 */

        PK2SendVramOne((uintptr_t)map_small_tex_addr,
                       GetMenuFileDispFileID(
                           file_type,
                           menu_file_ctrl.ref_ctrl[file_type].data_pos),
                       -1, -1, 0);                                      /* 2485 */

        CopySprDToSpr(&map_ds, &menu_file_tex[MF_MAP_SMALL + 1]);       /* 2486 */

        map_ds.x = map_ds.x + (float)off_x;
        map_ds.y = map_ds.y + (float)off_y;                             /* 2487 */

        map_ds.alpha = (u_char)(map_ds.alpha * alpha >> 7);             /* 2488 */

        DispSprD(&map_ds);                                              /* 2489 */

        MenuRefMovePadLup(&menu_file_ctrl.ref_ctrl[file_type],
                          &menu_file_ctrl.top_csr[file_type],
                          disp_num, MENU_FILE_DISP_NUM);                /* 2492 */
    }
}

static void MenuFileMapCenterTexDisp(int off_x, int off_y, u_char alpha) /* 2504 */
{
    int       file_type;
    DISP_SPRT map_ds;
    u_char    fade_alpha[MENU_CROSS_FADE_NUM];

    file_type = menu_file_ctrl.tag_csr;                                 /* 2512 */

    GetMenuFileDispFileID(file_type,
                          menu_file_ctrl.ref_ctrl[file_type].data_pos); /* 2514 */

    fade_alpha[0] = 0;                                                  /* 2515 */
    fade_alpha[1] = 0;                                                  /* 2516 */

    GetMenuCrossFadeAlpha(fade_alpha);                                  /* 2520 */

    if (menu_file_ctrl.sub_step != MENU_FILE_SUB_MAIN) {                /* 2522 */
        fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] = 0;              /* 2523 */
    }

    if (CheckCrossFadeDisp(menu_file_ctrl.cross_fade_flg ^ 1) != 0      /* 2527 */
        && fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] != 0) {        /* 2528 */
        MenuTim2SendVram(
            (u_int *)GetCrossFadeDataAddr(menu_file_ctrl.cross_fade_flg ^ 1),
            FILE_PICT_TBP, FILE_PICT_CBP);                              /* 2529 */

        CopySprDToSpr(&map_ds, &menu_file_tex[MF_MAP_PICTURE]);         /* 2531 */
        map_ds.tex0 = FILE_PICT_TEX0;                                   /* 2532 */
        map_ds.alpha = (u_char)(
            map_ds.alpha
            * fade_alpha[menu_file_ctrl.cross_fade_flg ^ 1] >> 7);      /* 2534 */
        DispSprD(&map_ds);                                              /* 2535 */
    }

    if (menu_file_ctrl.sub_step != MENU_FILE_SUB_MAIN) {                /* 2539 */
        fade_alpha[menu_file_ctrl.cross_fade_flg] = alpha;              /* 2540 */
    }

    if (CheckCrossFadeDisp(menu_file_ctrl.cross_fade_flg) != 0          /* 2544 */
        && fade_alpha[menu_file_ctrl.cross_fade_flg] != 0) {            /* 2545 */
        MenuTim2SendVram(
            (u_int *)GetCrossFadeDataAddr(menu_file_ctrl.cross_fade_flg),
            FILE_PICT_TBP, FILE_PICT_CBP);                              /* 2546 */

        CopySprDToSpr(&map_ds, &menu_file_tex[MF_MAP_PICTURE]);         /* 2548 */
        map_ds.tex0 = FILE_PICT_TEX0;                                   /* 2549 */
        map_ds.alpha = (u_char)(
            map_ds.alpha
            * fade_alpha[menu_file_ctrl.cross_fade_flg] >> 7);          /* 2551 */
        DispSprD(&map_ds);                                              /* 2552 */
    }
}

/* The map page's arrows are sprites of its own rather than the shared big
 * ones, and it is the *glow* pair that takes the cursor pulse -- and only
 * while the mode's fade is idle, which is what the sub_anim_step test does. */
static void MenuFileMapArrowDisp(int off_x, int off_y, u_char alpha)     /* 2565 */
{
    int       i;
    DISP_SPRT arrow_ds;
    u_char    arrow_alpha;

    if (menu_file_disp.sub_anim_step == MENU_FILE_ANIM_SHOW) {          /* 2571 */
        arrow_alpha = menu_file_disp.rgb;                               /* 2572 */
    }
    else {
        arrow_alpha = alpha;                                            /* 2575 */
    }

    PK2SendVram((uintptr_t)file_map_tex_addr, -1, -1, 0);               /* 2578 */

    for (i = 0; i < MF_MAP_ARROW_NUM; i++) {                            /* 2581 */
        CopySprDToSpr(&arrow_ds, &menu_file_tex[MF_MAP_ARROW_GLOW + i]); /* 2582 */

        arrow_ds.x = arrow_ds.x + (float)off_x;
        arrow_ds.y = arrow_ds.y + (float)off_y;                         /* 2583 */

        arrow_ds.alpha = (u_char)(arrow_ds.alpha * arrow_alpha >> 7);   /* 2584 */

        DispSprD(&arrow_ds);                                            /* 2585 */
    }                                                                   /* 2586 */

    for (i = 0; i < MF_MAP_ARROW_NUM; i++) {                            /* 2589 */
        CopySprDToSpr(&arrow_ds, &menu_file_tex[MF_MAP_ARROW + i]);     /* 2590 */

        arrow_ds.x = arrow_ds.x + (float)off_x;
        arrow_ds.y = arrow_ds.y + (float)off_y;                         /* 2591 */

        arrow_ds.alpha = (u_char)(arrow_ds.alpha * alpha >> 7);         /* 2592 */

        DispSprD(&arrow_ds);                                            /* 2593 */
    }                                                                   /* 2594 */
}

static void MenuFileMapMsgWinDisp(int off_x, int off_y, u_char alpha)    /* 2605 */
{
    int file_type;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2610 */

    DrawCmnWindow(0xa0, (float)(off_x + 24), (float)(off_y + 346),
                  592.0f, 100.0f, alpha, 0x66);                         /* 2613 */

    PrintMsg(msg_type_tbl[file_type],
             GetMenuFileDispFileID(
                 file_type, menu_file_ctrl.ref_ctrl[file_type].data_pos)
                 * FILE_MSG_PER_FILE + FILE_MSG_EXP,
             off_x + 48, off_y + 370, 1, alpha, 0xa0);                  /* 2619 */
}

/* The map page's caption.  Identical to MenuFilePhotoCaptionDisp() and still a
 * function of its own -- and the only one whose name does not say which mode
 * it belongs to. */
static void MenuFileCaptionDisp(int off_x, int off_y, u_char alpha)      /* 2631 */
{
    DrawCmnCapGroup_W(11, 11, alpha, 0);                                /* 2634 */
}

static void MenuFileMapNameDisp(int off_x, int off_y, u_char alpha)      /* 2662 */
{
    int i;
    int file_type;
    int file_id;

    file_type = menu_file_ctrl.tag_csr;                                 /* 2669 */
    file_id   = GetMenuFileDispFileID(
                    file_type,
                    menu_file_ctrl.ref_ctrl[file_type].data_pos);       /* 2671 */

    for (i = 0; i < 3; i++) {                                           /* 2674 */
        PrintMsg_Arrange(msg_type_tbl[file_type],
                         file_id * FILE_MSG_PER_FILE + FILE_MSG_NAME,
                         off_x + 321, off_y + 68 - i,
                         (i == 2) ? 10 : 11,
                         alpha, 0xa0, 0, 0, 2);                         /* 2675-2684 */
    }                                                                   /* 2685 */
}

/* --------------------------------------------------------------------------
 *  Shared row / arrow art
 * ------------------------------------------------------------------------ */

/* The unread bracket: one plate and its mirror, 280 pixels apart. */
static void MenuFileNoReadFrameDisp(float x, float y, u_char alpha)      /* 2700 */
{
    static int frame_tbl[2] = { 26, 27 };                    /* sdata 3f2d68 */

    DISP_SPRT frame_ds;
    int       i;

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 2708 */

    for (i = 0; i < 2; i++) {                                           /* 2711 */
        CopySprDToSpr(&frame_ds, &menu_file_tex[frame_tbl[i]]);         /* 2712 */

        frame_ds.x = x + (float)(i * 280);
        frame_ds.y = y;                                                 /* 2713 */

        frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);         /* 2714 */

        DispSprD(&frame_ds);                                            /* 2715 */
    }                                                                   /* 2716 */
}

/* A page arrow: the soft plate behind it takes the cursor pulse, the crisp
 * arrow on top is drawn plain and inset by the per-side offset. */
static void MenuFileBigArrowDisp(float x, float y, u_char alpha, u_char rgb,
                                 char flg)                              /* 2729 */
{
    static int arrow_tbl[2]  = { 45, 46 };                   /* sdata 3f2d70 */
    static int shadow_tbl[2] = { 47, 48 };                   /* sdata 3f2d78 */
    static int off_x_tbl[2]  = {  4,  6 };                   /* sdata 3f2d80 */
    static int off_y_tbl[2]  = {  5,  5 };                   /* sdata 3f2d88 */

    DISP_SPRT arrow_ds;

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 2746 */

    CopySprDToSpr(&arrow_ds, &menu_file_tex[shadow_tbl[flg]]);          /* 2749 */

    arrow_ds.x = x;   arrow_ds.y = y;                                   /* 2750 */

    arrow_ds.alpha = (u_char)(arrow_ds.alpha * alpha >> 7);             /* 2751 */

    arrow_ds.r = rgb;   arrow_ds.g = rgb;   arrow_ds.b = rgb;           /* 2752 */

    DispSprD(&arrow_ds);                                                /* 2753 */

    CopySprDToSpr(&arrow_ds, &menu_file_tex[arrow_tbl[flg]]);           /* 2756 */

    arrow_ds.x = x + (float)off_x_tbl[flg];
    arrow_ds.y = y + (float)off_y_tbl[flg];                             /* 2757 */

    arrow_ds.alpha = (u_char)(arrow_ds.alpha * alpha >> 7);             /* 2758 */

    DispSprD(&arrow_ds);                                                /* 2759 */
}

/* A tab arrow.  One plate, drawn in the pulse colour; its table is a LOCAL
 * array whose initialiser GCC parked in .sdata -- globals.txt is right not to
 * list it, and the two entries are the only thing in the object's last eight
 * .sdata bytes. */
static void MenuFileSmallArrowDisp(float x, float y, u_char alpha, u_char rgb,
                                   char flg)                            /* 2772 */
{
    DISP_SPRT arrow_ds;
    int       arrow_tbl[2] = { 17, 18 };                     /* sdata 3f2d90 */

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 2779 */

    CopySprDToSpr(&arrow_ds, &menu_file_tex[arrow_tbl[flg]]);           /* 2782 */

    arrow_ds.x = x;   arrow_ds.y = y;                                   /* 2783 */

    arrow_ds.alpha = (u_char)(arrow_ds.alpha * alpha >> 7);             /* 2784 */

    arrow_ds.r = rgb;   arrow_ds.g = rgb;   arrow_ds.b = rgb;           /* 2785 */

    DispSprD(&arrow_ds);                                                /* 2786 */
}

/* The selected row's frame: a left cap, eighteen middle tiles butted together
 * by their own width, and a right cap.  The running x is not a source local --
 * `ds.x` carries it, and each tile is placed from the previous one's width. */
static void MenuFileSelFrameDisp(float x, float y, u_char alpha)         /* 2798 */
{
    int       i;
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 2803 */

    CopySprDToSpr(&ds, &menu_file_tex[MF_SEL_FRAME_L]);                 /* 2806 */

    ds.x = x;   ds.y = y;                                               /* 2807 */

    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 2808 */

    DispSprD(&ds);                                                      /* 2809 */

    x = x + (float)ds.w;                                                /* 2810 */

    for (i = 0; i < MF_SEL_FRAME_NUM; i++) {                            /* 2812 */
        CopySprDToSpr(&ds, &menu_file_tex[MF_SEL_FRAME_M]);             /* 2813 */

        ds.x = x;   ds.y = y;                                           /* 2814 */

        ds.alpha = (u_char)(ds.alpha * alpha >> 7);                     /* 2815 */

        DispSprD(&ds);                                                  /* 2816 */

        x = x + (float)ds.w;                                            /* 2817 */
    }                                                                   /* 2818 */

    CopySprDToSpr(&ds, &menu_file_tex[MF_SEL_FRAME_R]);                 /* 2820 */

    ds.x = x;   ds.y = y;                                               /* 2821 */

    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 2822 */

    DispSprD(&ds);                                                      /* 2823 */
}

/* The unselected row's frame.  Two more middle tiles than the selected one and
 * a fourth plate on the end, because the art is narrower. */
static void MenuFileNonSelFrameDisp(float x, float y, u_char alpha)      /* 2835 */
{
    int       i;
    DISP_SPRT ds;

    PK2SendVram((uintptr_t)file_cmn_tex_addr, -1, -1, 0);               /* 2840 */

    CopySprDToSpr(&ds, &menu_file_tex[MF_NONSEL_FRAME_L]);              /* 2843 */

    ds.x = x;   ds.y = y;                                               /* 2844 */

    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 2845 */

    DispSprD(&ds);                                                      /* 2846 */

    x = x + (float)ds.w;                                                /* 2847 */

    for (i = 0; i < MF_NONSEL_FRAME_NUM; i++) {                         /* 2849 */
        CopySprDToSpr(&ds, &menu_file_tex[MF_NONSEL_FRAME_M]);          /* 2850 */

        ds.x = x;   ds.y = y;                                           /* 2851 */

        ds.alpha = (u_char)(ds.alpha * alpha >> 7);                     /* 2852 */

        DispSprD(&ds);                                                  /* 2853 */

        x = x + (float)ds.w;                                            /* 2854 */
    }                                                                   /* 2855 */

    CopySprDToSpr(&ds, &menu_file_tex[MF_NONSEL_FRAME_R]);              /* 2857 */

    ds.x = x;   ds.y = y;                                               /* 2858 */

    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 2859 */

    DispSprD(&ds);                                                      /* 2860 */

    x = x + (float)ds.w;                                                /* 2861 */

    CopySprDToSpr(&ds, &menu_file_tex[MF_NONSEL_FRAME_END]);            /* 2863 */

    ds.x = x;   ds.y = y;                                               /* 2864 */

    ds.alpha = (u_char)(ds.alpha * alpha >> 7);                         /* 2865 */

    DispSprD(&ds);                                                      /* 2866 */
}
