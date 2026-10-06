// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_radio.c
//
// The in-game menu's crystal-radio page (menu_radio.o).  Thirty-six
// functions: seven exports and twenty-nine statics.
//
// The page is the standard menu shape -- a *_CTRL work block with a
// MENU_REF_CTRL inside it, a menu_wrk.step ladder, an anim_step/anim_timer
// fade, and a Get/LoadReq/LoadWait/Liberate/LoadCancel texture quintet.  Read
// menu_soul.c first; it is the same skeleton and this file only differs in
// what hangs off it.  What is its own is the recording: pressing CROSS on a
// row starts an ADPCM stream, and the page then sits in a second sub-state
// with its own animation until the stream ends or the player stops it.
//
// Facts worth knowing before touching it:
//
//  * disp_crystal_data[] is a *compacted* copy of the 40 crystal labels, not
//    a view of them.  MenuRadioSetDispData() walks the labels once and
//    copies the ones the player holds into consecutive entries, so a row is
//    disp_crystal_data[disp_start_pos + i] and the list walks without
//    re-testing anything.  It is built once, on entry.
//
//  * `sub_step` is the page's inner state: 0 the list, 1 a recording is
//    playing, 2 "you have no spirit stones".  Like menu_soul.o there are no
//    dispatch tables -- MenuRadio(), MenuRadioDisp() and
//    MenuRadioLoadCrystalDisp() each switch on it directly, and this object
//    has no .data section at all as a result.
//
//  * MENU_RADIO_CTRL::stream_id is DEAD.  types.txt has it at 0x10 and the
//    struct really is 0x14 bytes, but nothing in the object touches
//    menu_radio_ctrl + 0x10 -- an offset scan over the whole .text finds no
//    reference.  The live handle is the .sdata static menu_radio_stream_id,
//    which is what MenuRadioStreamStop() clears.  It is kept because the ROM
//    reserves it.
//
//  * The crystal picture has its own fade, independent of the page's.
//    MENU_RADIO_DISP carries a second (step, timer) pair plus two alphas:
//    the stone brightens from 89 to 128 over 80 frames while its recording
//    plays and drops back over 20, and a halo behind it does 0 -> 128 over
//    the same schedule.  MenuRadioDispInit() opens the page parked at
//    CRYSTAL_ANIM_END, which is the "dim, and scaled by the page fade" case.
//
//  * Both the in tables are function-local statics in .rodata and both the
//    out tables are stack locals.  That is not an accident: case OUT
//    *patches* crystal_out_alpha_tbl[0].start_alpha to wherever the fade had
//    got to, so a recording stopped early bleeds down from its current
//    brightness rather than snapping to full.  The in tables never need
//    patching, so they can be shared.
//
//  * The picture is not in the page's pak.  MenuCrossFadeInStart() loads
//    (crystal_id + RADIO_CRYSTAL_01_PK2) into one of menu_cmn.o's two
//    cross-fade slots and both slots are drawn each frame, the outgoing one
//    first -- the same machinery menu_soul.o and menu_item.o use for their
//    pictures.
//
//  * There is one pak per language (MENU_RADIO_PK2 + GetLanguage()) because
//    the button caption plates carry baked words, and the three
//    *_cap_tbl[] tables in tim_dat/menu_radio_dat.c move each plate to suit
//    the word's length.
//
//  * The subtitle under a playing recording comes from crystal.o's
//    MOVIE_TITLE_DAT table, walked by title_timer -- a plain frame count
//    that MenuRadioCrystalPlayDisp() increments and nothing scales.  It is
//    NOT the stream's own position, so the caption and the audio only stay
//    in step because both run at the video rate.
//
//  * MenuRadioCrystalTitleDebugPad() is an authoring tool, not gameplay:
//    holding one pad button prints the current title_timer with a trailing
//    comma and releasing it prints the timer and a newline, which is exactly
//    the "start, end" pair a MOVIE_TITLE_DAT row wants.  That is what the
//    two-line banner printf'd when a recording starts is heading.
//
//  * Only three of the four caption groups are pad-driven.  The list page
//    draws group 5 (PLAY), the playing page group 6 (STOP) and the empty
//    page group 0xc; there is no cancel caption anywhere, though TRIANGLE
//    leaves from all three.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_radio.o.
// All 7 ZERO2.MAP exports plus the 29 statics, verified 7/7 against the map
// and 36/37 against functions.txt (its 37th entry is the DISP_CRYSTAL_DATA
// type_info function, a compiler artifact).  .text is accounted for
// byte-for-byte -- 0x203fc0..0x206588 = 0x25c8 = 9672 bytes: forty-two
// bodies totalling 9576 (the thirty-six real ones, the four fixed_array
// boilerplate ones and the empty static-init pair) plus twenty-four 4-byte
// alignment fills, with no gap of 8 bytes or more anywhere.
//
// The object has NO static data of its own beyond the four scalars and
// disp_crystal_data.  Its .rodata (0x1fc) holds the fixed_array assert
// literal, the report banners, the two __FUNCTION__-style strings, the two
// in tables, the two out tables' initialiser images, MenuRadioLoadCrystalDisp's
// five-slot jump table and the DISP_CRYSTAL_DATA type name; its .sdata
// (0x66) is the "void*" / "char*" type names, three scalars and the two
// debug printf formats.  All of it was read out of the ELF -- the size looks
// like there ought to be a table and there is not.
//
// The page's sprites live in tim_dat/menu_radio_dat.c, new with this pass:
// menu_radio_tex[37] and the three caption tables are byte-identical to the
// ROM's .data / .rodata.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Three things routinely leave a statement with no line
// of its own and those annotations are interpolated into the measured gap:
//
//  * any statement whose first act is a disp_crystal_data[] subscript -- the
//    inlined operator[] reports fixed_array.h 124/125 and the caller's own
//    note is dropped.  All eight subscripts in the file are like that;
//
//  * a store the scheduler put in a jr/jal delay slot: MenuRadioDisp()'s
//    `alpha = 0`, MenuRadioNoHaveCrystalDisp()'s `msg_data.alpha`,
//    MenuRadioLoadCrystalDisp()'s `fade_alpha[0]`, and the `load_flg = 1` of
//    every cursor-move arm;
//
//  * the four cursor-move arms of MenuRadioPad().  All four end in the same
//    `load_flg = 1` + SystemBankPlay() pair and GCC cross-jumped three of
//    them onto the L1 arm's copy, so only that copy carries line numbers
//    (459/460); the up and down arms are placed by the arms' exact 7-line
//    stride and their surviving branch notes (411 / 418).
//
// Two spans hold no code at all and nothing of them is recoverable: 1314..1348
// between MenuRadioCaptionDisp() and MenuRadioCrystalPlayCaptionDisp() (35
// lines), and 1369..1400 before MenuRadioSelFrameDisp() (32 lines).

#include "menu_radio.h"

#include "menu.h"                               /* menu_wrk / MENU_BG_TEX_ADRS */
#include "menu_cmn.h"                           /* MenuRefMove* / cross-fade  */
#include "tim_dat/menu_radio_dat.h"             /* menu_radio_tex[]           */
#include "zero2_anim2d.h"                       /* Zero2Anim2D_CsrAnimCtrl    */
#include "anim_2d.h"                            /* ALPHA_ANIM_TBL             */

#include "../item/prg/crystal.h"                /* GetPlyrCrystalState        */
#include "../../common/mem_util.h"              /* mem_utilGetMem             */
#include "../../common/utility2.h"              /* PRINT_ASSERT / PRINT_WARNING */
#include "../../graphics/graph2d/draw_cmn.h"    /* DrawCmnCapGroup_W          */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / DISP_STR       */
#include "../../graphics/graph2d/message.h"     /* PrintMsg / PrintNumber     */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/movie/movie_title.h"   /* MovieTitleDispMain         */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_RADIO_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE              */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET                 */
#include "../../system/eeiop/stream_auto.h"     /* StreamAutoPlay             */
#include "../../system/os/system.h"             /* SystemBankPlay / GetLanguage */
#include "../../system/pad/pad.h"               /* pad / paddat               */

#include <stdio.h>                              /* printf                     */
#include <string.h>                             /* memset                     */

/* --------------------------------------------------------------------------
 *  Constants
 * ------------------------------------------------------------------------ */

/* menu_wrk.step values, the same ladder every page uses. */
#define MENU_RADIO_STEP_INIT        0
#define MENU_RADIO_STEP_LOAD        1
#define MENU_RADIO_STEP_MAIN        2
#define MENU_RADIO_STEP_OUT         3

/* MENU_RADIO_DISP::anim_step.  Only OUT and END are written by name here;
 * the rest of the ladder is MenuInOutAnimCtrl()'s business. */
#define MENU_RADIO_ANIM_START       0
#define MENU_RADIO_ANIM_OUT         3
#define MENU_RADIO_ANIM_END         4

/* MENU_RADIO_CTRL::sub_step -- what all three switches key on. */
#define MENU_RADIO_SUB_SEL          0   /* the list                          */
#define MENU_RADIO_SUB_PLAY         1   /* a recording is playing            */
#define MENU_RADIO_SUB_NO_CRYSTAL   2   /* "you have no spirit stones"       */

/* MENU_RADIO_DISP::crystal_anim_step -- the picture's own fade, which runs
 * independently of the page's.  START only ever falls straight through into
 * IN; KEEP holds full brightness for as long as the recording lasts. */
#define MENU_RADIO_CRYSTAL_ANIM_START   0
#define MENU_RADIO_CRYSTAL_ANIM_IN      1
#define MENU_RADIO_CRYSTAL_ANIM_KEEP    2
#define MENU_RADIO_CRYSTAL_ANIM_OUT     3
#define MENU_RADIO_CRYSTAL_ANIM_END     4

/* How long each half of that fade runs, in frames. */
#define MENU_RADIO_CRYSTAL_IN_TIME      80
#define MENU_RADIO_CRYSTAL_OUT_TIME     20

/* The picture's two alphas.  MIN is what a stone sits at while the page is
 * merely open; MAX is what it reaches while its recording plays. */
#define MENU_RADIO_CRYSTAL_ALPHA_MIN    89
#define MENU_RADIO_CRYSTAL_ALPHA_MAX    128

/* How many rows the list shows, and how many entries it can hold. */
#define MENU_RADIO_DISP_NUM         6
#define DISP_CRYSTAL_NUM            CRYSTAL_MAX

/* The list's geometry.  Every row-relative widget steps by the same pitch
 * and differs only in where it starts. */
#define MENU_RADIO_ROW_STEP         35
#define MENU_RADIO_NAME_X           64
#define MENU_RADIO_NAME_Y           85
#define MENU_RADIO_NON_HEAR_X       44
#define MENU_RADIO_NON_HEAR_Y       83

/* Message banks.  A crystal owns one name and one explanation, both keyed
 * directly by crystal_id. */
#define RADIO_MSG_NAME              0x49
#define RADIO_MSG_EXP               0x48
#define RADIO_MSG_NO_CRYSTAL        0x36

/* MovieTitleDispMain()'s bank for a recording's subtitle, and where it puts
 * the first line. */
#define RADIO_TITLE_MSG_TYPE        9
#define RADIO_TITLE_Y               0x123
#define RADIO_TITLE_COL             0x10

/* Colour labels: which ink a row's name is drawn in. */
#define RADIO_COL_SEL               4
#define RADIO_COL_NON_SEL           3

/* DrawCmnCapGroup_W() caption groups -- one per sub_step that has one. */
#define RADIO_CAP_GROUP_SEL         5
#define RADIO_CAP_GROUP_PLAY        6
#define RADIO_CAP_GROUP_NO_CRYSTAL  0xc

/* The scrollbar rail and thumb, in pixels. */
#define RADIO_SCROLL_RAIL_SIZE      185.0f
#define RADIO_SCROLL_RAIL_TOP       94.0f
#define RADIO_RAIL_MID_SIZE         126.0f

/* The two row frames.  The selected one is stretched through scw, the
 * unselected one rotated 90 and stretched through sch -- which is why they
 * need different lengths for the same visual width. */
#define RADIO_SEL_FRAME_SIZE        138.0f
#define RADIO_NON_SEL_FRAME_SIZE    164.0f
#define RADIO_NON_HEAR_FRAME_SIZE   255.0f

/* The message window under the list. */
#define RADIO_MSG_WIN_X             24.0f
#define RADIO_MSG_WIN_Y             322.0f
#define RADIO_MSG_WIN_W             592.0f
#define RADIO_MSG_WIN_H             124.0f

/* menu_radio_tex[] indices.  See tim_dat/menu_radio_dat.c for the layout;
 * 34..36 are unreferenced and have no name here. */
#define MR_BG                   0    /* the full-screen plate                */
#define MR_BG_SIDE              1    /* [1..3] the right-hand column         */
#define MR_BG_ROT               4    /* [4],[5] drawn rot 270                */
#define MR_TITLE                6    /* [6],[7] out of the radio pak         */
#define MR_TITLE_PLATE          8    /* [8],[9] out of the MENU_BG pak       */
#define MR_SCROLL_ARROW_UP      10
#define MR_SCROLL_ARROW_DOWN    11
#define MR_RAIL_TOP             12
#define MR_RAIL_BOTTOM          13
#define MR_RAIL_MID             14
#define MR_THUMB_TOP            15
#define MR_THUMB_MID            16
#define MR_THUMB_BOTTOM         17
#define MR_NON_SEL_LEFT         18
#define MR_NON_SEL_MID          19
#define MR_NON_SEL_RIGHT        20
#define MR_SEL_LEFT             21
#define MR_SEL_MID              22
#define MR_SEL_RIGHT            23
#define MR_NON_HEAR_LEFT        24
#define MR_NON_HEAR_RIGHT       25
#define MR_CRYSTAL              26
#define MR_CRYSTAL_FLARE        27   /* [27..30]                             */
#define MR_CAP_PLAY             31
#define MR_CAP_STOP_1           32
#define MR_CAP_STOP_2           33

/* The recording.  The stream's own header file is always its file number
 * minus one, the same convention subtitle.o and enemy.o use. */
#define RADIO_STREAM_PRIORITY   0xe     /* STREAM_PRIORITY_CRYSTAL       */
#define RADIO_STREAM_VOL        0x3200
#define RADIO_STREAM_FADE_OUT   3

/* SystemBankPlay() cue numbers, as everywhere else in the menus. */
#define SE_CURSOR   0
#define SE_CANCEL   1

/* pad[0] bits in the remapped layout, plus the analogue equivalents. */
#define PAD_RPT_UP              0x1000
#define PAD_RPT_DOWN            0x4000
#define PAD_ONE_L1              0x4
#define PAD_ONE_R1              0x8
#define PAD_ANALOG_UP           0
#define PAD_ANALOG_DOWN         1

/* The hold used by MenuRadioCrystalTitleDebugPad() to bracket a subtitle
 * row.  Left as the raw mask the ROM tests, the way roku_pzl.c and
 * menu_top.c leave their own -- the remapped layout has no name for it. */
#define PAD_NOW_TITLE_DEBUG     0x20

/* --------------------------------------------------------------------------
 *  Work
 * ------------------------------------------------------------------------ */

static void *menu_radio_tex_addr;                           /* sdata 3f2f08 */

/* The recording currently playing, -1 when none is.  This -- not
 * MENU_RADIO_CTRL::stream_id, which nothing touches -- is the live handle. */
static int menu_radio_stream_id;                            /* sdata 3f2f0c */

/* Debounce for the subtitle-authoring hold. */
static u_char crystal_title_debug_flg;                      /* sdata 3f2f10 */

static fixed_array<DISP_CRYSTAL_DATA, DISP_CRYSTAL_NUM>
       disp_crystal_data;                                   /* bss   4b5cc0 */

static MENU_RADIO_CTRL menu_radio_ctrl;                     /* bss   4b5e00 */
static MENU_RADIO_DISP menu_radio_disp;                     /* bss   4b5e18 */

static void MenuRadioInit(void);
static void MenuRadioCtrlInit(void);
static int  MenuRadioTexLoadWait(void);
static void MenuRadioSetDispData(void);
static void MenuRadioPad(void);
static void MenuRadioCrystalPlayPad(void);
static void MenuRadioNoHaveCrystalPad(void);
static void MenuRadioDispInit(void);

static void MenuRadioBgDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioTitleDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioCrystalSelDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioCrystalPlayDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioNoHaveCrystalDisp(u_char alpha);
static void MenuRadioCrystalNameFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioNonHearFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioScrollFrameDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioScrollDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioLoadCrystalDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioCrystalDisp(int off_x, int off_y, u_char alpha,
                                 void *pk2_addr);
static void MenuRadioCrystalFlareDisp(int off_x, int off_y, u_char alpha,
                                      void *pk2_addr);
static void MenuRadioMsgWinDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioTopMsgDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioCrystalPlayCaptionDisp(int off_x, int off_y, u_char alpha);
static void MenuRadioSelFrameDisp(float x, float y, u_char alpha);
static void MenuRadioNonSelFrameDisp(float x, float y, u_char alpha);
static void MenuRadioNoHearFrameDisp(float x, float y, u_char alpha);
static void MenuRadioCrystalTitleDebugPad(void);
static void MenuRadioCrystalTitleTimerDebugDisp(void);

/* --------------------------------------------------------------------------
 *  Entry / exit
 * ------------------------------------------------------------------------ */

/* The pak is normally claimed and loaded by menu_top.c as the hub fades out.
 * If it is not resident by the time the page opens, this warns and starts
 * the load anyway -- so the page still works, one frame later. */
static void MenuRadioInit(void)                                          /* 177 */
{
    menu_wrk.cursor = 0;                                                /* 180 */

    MenuRadioCtrlInit();                                                /* 184 */

    menu_radio_stream_id = -1;                                          /* 186 */
    memset(&disp_crystal_data, 0, sizeof(disp_crystal_data));           /* 188 */

    MenuCrossFadeInit();                                                /* 191 */

    if (menu_radio_tex_addr == nullptr) {                               /* 194 */
        PRINT_WARNING("Menu Radio Tex Back reading failure\n");         /* 195 */
        MenuRadioTexLoadReq();                                          /* 196 */
    }

    crystal_title_debug_flg = 0;                                        /* 200 */
}

static void MenuRadioCtrlInit(void)                                      /* 209 */
{
    menu_radio_ctrl.sub_step       = MENU_RADIO_SUB_SEL;                /* 213 */
    menu_radio_ctrl.cross_fade_flg = 0;

    MenuRefCtrlInit(&menu_radio_ctrl.ref_ctrl, GetPlyrHaveCrystalNum()); /* 216 */
}

void GetMenuRadioTexMem(void)                                            /* 222 */
{
    if (menu_radio_tex_addr != nullptr) {                               /* 226 */
        LiberateMenuRadioTexMem();                                      /* 227 */
    }

    if (menu_radio_tex_addr == nullptr) {                               /* 231 */
        menu_radio_tex_addr =
            mem_utilGetMem((int)GetFileSize(MENU_RADIO_PK2 + GetLanguage())); /* 232 */
    }
}

void MenuRadioTexLoadReq(void)                                           /* 240 */
{
    if (menu_radio_tex_addr == nullptr) {                               /* 244 */
        GetMenuRadioTexMem();                                           /* 246 */
    }

    FileLoadReqEE(MENU_RADIO_PK2 + GetLanguage(), menu_radio_tex_addr,
                  2, nullptr, nullptr);                                 /* 251 */
}

static int MenuRadioTexLoadWait(void)                                    /* 260 */
{
    if (FileLoadIsEnd2(MENU_RADIO_PK2 + GetLanguage(),
                       menu_radio_tex_addr) != 0) {                     /* 269 */
        return 1;
    }

    return 0;                                                           /* 274 */
}

/* Build the compacted list.  Called once, on entry -- nothing the page does
 * can add or remove a row, because a crystal being listened to only moves it
 * from HAVE to HEARD. */
static void MenuRadioSetDispData(void)                                   /* 279 */
{
    int i;
    int count;
    int state;

    count = 0;                                                          /* 286 */

    for (i = 0; i < DISP_CRYSTAL_NUM; i++) {                            /* 290 */
        state = GetPlyrCrystalState(i);                                 /* 292 */

        if (state != CRYSTAL_STATE_NONE) {                              /* 295 */
            disp_crystal_data[count].crystal_id = i;                    /* 297 */
            disp_crystal_data[count].state      = state;                /* 298 */
            count++;                                                    /* 299 */
        }
    }                                                                   /* 301 */
}

/* One frame of the page.  The load step opens the first crystal's picture
 * before it hands over, so the picture is already fading up when the page
 * itself does. */
void MenuRadio(void)                                                     /* 312 */
{
    if (menu_wrk.step == MENU_RADIO_STEP_INIT) {                        /* 316 */
        MenuRadioInit();                                                /* 317 */
        MenuRadioSetDispData();                                         /* 319 */

        menu_wrk.step = MENU_RADIO_STEP_LOAD;                           /* 321 */
    }

    if (menu_wrk.step == MENU_RADIO_STEP_LOAD) {                        /* 324 */
        if (MenuRadioTexLoadWait() != 0) {                              /* 326 */
            MenuRadioDispInit();                                        /* 328 */
            menu_wrk.step = MENU_RADIO_STEP_MAIN;                       /* 329 */

            if (menu_radio_ctrl.ref_ctrl.data_num == 0) {               /* 332 */
                menu_radio_ctrl.sub_step = MENU_RADIO_SUB_NO_CRYSTAL;   /* 333 */
            }
            else {
                MenuCrossFadeInStart(
                    menu_radio_ctrl.cross_fade_flg,
                    disp_crystal_data[menu_radio_ctrl.ref_ctrl.data_pos].crystal_id
                        + RADIO_CRYSTAL_01_PK2);                        /* 337 */

                menu_radio_ctrl.sub_step = MENU_RADIO_SUB_SEL;          /* 339 */
            }
        }
    }

    if (menu_wrk.step == MENU_RADIO_STEP_MAIN) {                        /* 344 */
        switch (menu_radio_ctrl.sub_step) {                             /* 345 */
        case MENU_RADIO_SUB_SEL:
            MenuRadioPad();                                             /* 347 */
            break;                                                      /* 348 */

        case MENU_RADIO_SUB_PLAY:
            MenuRadioCrystalPlayPad();                                  /* 350 */

            MenuRadioCrystalTitleDebugPad();                            /* 352 */
            break;                                                      /* 354 */

        case MENU_RADIO_SUB_NO_CRYSTAL:
            MenuRadioNoHaveCrystalPad();                                /* 356 */
            break;                                                      /* 357 */

        default:
            PRINT_WARNING("Error!! MenuRadio()");                       /* 359 */
            break;
        }

        /* An empty page has no picture to pump, and CheckCrossFadeDisp()
         * would report on a slot that was never started. */
        if (menu_radio_ctrl.sub_step != MENU_RADIO_SUB_NO_CRYSTAL) {    /* 362 */
            MenuCmnCrossFade();                                         /* 364 */
        }
    }

    if (menu_wrk.step == MENU_RADIO_STEP_OUT) {                         /* 368 */
        if (menu_radio_disp.anim_step == MENU_RADIO_ANIM_END) {         /* 369 */
            MenuRadioStreamStop();                                      /* 371 */

            LiberateMenuRadioTexMem();                                  /* 374 */

            SetNextMenuStep(MENU_STEP_TOP);                             /* 377 */

            LiberateAllMenuCrossFadeTexMem();                           /* 380 */
        }
    }
}

/* The list.  CROSS starts the selected crystal's recording -- but only once
 * its picture has finished loading, because HearCrystal() retiring the
 * unread bracket and the picture fading up have to land together. */
static void MenuRadioPad(void)                                           /* 390 */
{
    short disp_num;
    char  load_flg;

    disp_num = menu_radio_ctrl.ref_ctrl.data_num;                       /* 397 */
    load_flg = 0;                                                       /* 398 */

    if (MENU_RADIO_DISP_NUM < disp_num) {                               /* 402 */
        disp_num = MENU_RADIO_DISP_NUM;
    }

    if ((pad[0].rpt & PAD_RPT_UP) || GetPadAnalogRpt(PAD_ANALOG_UP)) {  /* 408 */
        if (MenuRefMovePadLup(&menu_radio_ctrl.ref_ctrl, &menu_wrk.cursor,
                              disp_num, MENU_RADIO_DISP_NUM)) {         /* 409 */
            load_flg = 1;
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr,
                           RADIO_STREAM_VOL, 0x1000);                   /* 411 */
        }
    }
    else if ((pad[0].rpt & PAD_RPT_DOWN)
             || GetPadAnalogRpt(PAD_ANALOG_DOWN)) {                     /* 415 */
        if (MenuRefMovePadLdown(&menu_radio_ctrl.ref_ctrl, &menu_wrk.cursor,
                                disp_num, MENU_RADIO_DISP_NUM)) {       /* 416 */
            load_flg = 1;
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr,
                           RADIO_STREAM_VOL, 0x1000);                   /* 418 */
        }
    }
    else if (*paddat[0] == 1) {                                         /* 422 */
        if (CheckCrossFadeDisp(menu_radio_ctrl.cross_fade_flg)) {       /* 423 */
            int play_stream_data;

            HearCrystal(
                disp_crystal_data[menu_radio_ctrl.ref_ctrl.data_pos].crystal_id); /* 425 */

            play_stream_data = GetCrystalStreamID(
                disp_crystal_data[menu_radio_ctrl.ref_ctrl.data_pos].crystal_id); /* 427 */

            MenuRadioStreamStop();                                      /* 430 */

            menu_radio_stream_id =
                StreamAutoPlay(play_stream_data, play_stream_data - 1,
                               RADIO_STREAM_PRIORITY, 0, 0,
                               RADIO_STREAM_VOL, 0, nullptr);           /* 434 */

            menu_radio_disp.title_timer        = 0;                     /* 437 */

            menu_radio_disp.crystal_anim_step  = MENU_RADIO_CRYSTAL_ANIM_START; /* 439 */
            menu_radio_disp.crystal_anim_timer = 0;                     /* 440 */

            /* The heading for MenuRadioCrystalTitleDebugPad()'s output --
             * a fresh "start, end" column per recording. */
            printf("\n\n**********  Crystal Title Data  **********\n");  /* 443 */
            printf(" start,    end\n");                                 /* 444 */

            menu_radio_ctrl.sub_step = MENU_RADIO_SUB_PLAY;             /* 447 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 451 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, nullptr,
                       RADIO_STREAM_VOL, 0x1000);                       /* 452 */

        menu_wrk.step               = MENU_RADIO_STEP_OUT;              /* 453 */
        menu_radio_disp.anim_step   = MENU_RADIO_ANIM_OUT;              /* 454 */
        menu_radio_disp.anim_timer  = 0;                                /* 455 */
    }
    else if (pad[0].one & PAD_ONE_L1) {                                 /* 458 */
        if (MenuRefMovePageUp(&menu_radio_ctrl.ref_ctrl, &menu_wrk.cursor,
                              disp_num, MENU_RADIO_DISP_NUM)) {         /* 459 */
            load_flg = 1;
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr,
                           RADIO_STREAM_VOL, 0x1000);                   /* 460 */
        }
    }
    else if (pad[0].one & PAD_ONE_R1) {                                 /* 465 */
        if (MenuRefMovePageDown(&menu_radio_ctrl.ref_ctrl, &menu_wrk.cursor,
                                disp_num, MENU_RADIO_DISP_NUM)) {       /* 466 */
            load_flg = 1;
            SystemBankPlay(SE_CURSOR, 1, 0, 0, nullptr,
                           RADIO_STREAM_VOL, 0x1000);                   /* 467 */
        }
    }

    /* The selection moved, so swap cross-fade slots: the old picture fades
     * out of the one we are leaving and the new one loads into the other. */
    if (load_flg == 1) {                                                /* 473 */
        MenuCrossFadeOutStart(menu_radio_ctrl.cross_fade_flg);          /* 475 */

        menu_radio_ctrl.cross_fade_flg ^= 1;                            /* 476 */

        MenuCrossFadeInStart(
            menu_radio_ctrl.cross_fade_flg,
            disp_crystal_data[menu_radio_ctrl.ref_ctrl.data_pos].crystal_id
                + RADIO_CRYSTAL_01_PK2);                                /* 477 */

        menu_radio_disp.crystal_anim_step = MENU_RADIO_CRYSTAL_ANIM_END; /* 479 */
    }
}

/* A recording is playing.  Either button stops it; so does the stream
 * running out, which is the only way this page ever advances by itself. */
static void MenuRadioCrystalPlayPad(void)                                /* 486 */
{
    if (*paddat[0] == 1 || *paddat[1] == 1) {                           /* 491 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, nullptr,
                       RADIO_STREAM_VOL, 0x1000);                       /* 492 */

        MenuRadioStreamStop();                                          /* 495 */

        menu_radio_disp.crystal_anim_step  = MENU_RADIO_CRYSTAL_ANIM_OUT; /* 497 */
        menu_radio_disp.crystal_anim_timer = 0;                         /* 498 */

        menu_radio_ctrl.sub_step = MENU_RADIO_SUB_SEL;                  /* 500 */
    }
    else if (StreamAutoIsPlaying(menu_radio_stream_id) == 0) {          /* 503 */
        menu_radio_stream_id = -1;                                      /* 504 */

        menu_radio_disp.crystal_anim_step  = MENU_RADIO_CRYSTAL_ANIM_OUT; /* 506 */
        menu_radio_disp.crystal_anim_timer = 0;                         /* 507 */

        menu_radio_ctrl.sub_step = MENU_RADIO_SUB_SEL;                  /* 509 */
    }
}

/* The empty page.  TRIANGLE is the only way out -- CROSS does nothing,
 * because there is nothing to select. */
static void MenuRadioNoHaveCrystalPad(void)                              /* 518 */
{
    if (*paddat[1] == 1) {                                              /* 523 */
        SystemBankPlay(SE_CANCEL, 1, 0, 0, nullptr,
                       RADIO_STREAM_VOL, 0x1000);                       /* 524 */

        menu_wrk.step              = MENU_RADIO_STEP_OUT;               /* 525 */
        menu_radio_disp.anim_step  = MENU_RADIO_ANIM_OUT;               /* 526 */
        menu_radio_disp.anim_timer = 0;                                 /* 527 */
    }
}

void MenuRadioStreamStop(void)                                           /* 540 */
{
    if (menu_radio_stream_id != -1) {                                   /* 544 */
        StreamAutoFadeOut(menu_radio_stream_id, RADIO_STREAM_FADE_OUT); /* 546 */
        menu_radio_stream_id = -1;                                      /* 548 */
    }
}

void LiberateMenuRadioTexMem(void)                                       /* 556 */
{
    if (menu_radio_tex_addr != nullptr) {                               /* 560 */
        mem_utilFreeMem(menu_radio_tex_addr);                           /* 561 */
        menu_radio_tex_addr = nullptr;                                  /* 562 */
    }
}

void MenuRadioTexLoadCancel(void)                                        /* 570 */
{
    if (MenuRadioTexLoadWait() == 0) {                                  /* 575 */
        FileLoadCancel2(MENU_RADIO_PK2 + GetLanguage(), menu_radio_tex_addr,
                        nullptr, nullptr);                              /* 576 */
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

/* Note the picture opens parked at CRYSTAL_ANIM_END rather than START: the
 * page fade brings the dim stone in, and only a recording starting kicks the
 * separate brighten. */
static void MenuRadioDispInit(void)                                      /* 588 */
{
    menu_radio_disp.anim_step            = MENU_RADIO_ANIM_START;       /* 592 */
    menu_radio_disp.anim_timer           = 0;                           /* 593 */

    menu_radio_disp.crystal_anim_step    = MENU_RADIO_CRYSTAL_ANIM_END; /* 594 */
    menu_radio_disp.crystal_anim_timer   = 0;                           /* 595 */
    menu_radio_disp.crystal_alpha        = MENU_RADIO_CRYSTAL_ALPHA_MIN; /* 596 */
    menu_radio_disp.crystal_flare_alpha  = 0;                           /* 597 */

    menu_radio_disp.rgb                  = 0x40;                        /* 598 */
    menu_radio_disp.scroll_timer         = 0;                           /* 599 */

    menu_radio_disp.title_timer          = 0;                           /* 600 */
}

void MenuRadioDisp(void)                                                 /* 607 */
{
    u_char alpha;

    alpha = 0;

    if (menu_wrk.step == MENU_RADIO_STEP_MAIN
        || menu_wrk.step == MENU_RADIO_STEP_OUT) {                      /* 615 */
        Zero2Anim2D_CsrAnimCtrl(&menu_radio_disp.scroll_timer,
                                &menu_radio_disp.rgb);                  /* 617 */

        if (menu_radio_disp.anim_step != MENU_RADIO_ANIM_END) {         /* 619 */
            MenuInOutAnimCtrl(&menu_radio_disp.anim_step,
                              &menu_radio_disp.anim_timer, &alpha);     /* 621 */

            MenuRadioBgDisp(0, 0, alpha);                               /* 624 */

            MenuRadioTitleDisp(0, 0, alpha);                            /* 627 */

            switch (menu_radio_ctrl.sub_step) {                         /* 629 */
            case MENU_RADIO_SUB_SEL:
                MenuRadioCrystalSelDisp(0, 0, alpha);                   /* 631 */
                break;                                                  /* 632 */

            case MENU_RADIO_SUB_PLAY:
                MenuRadioCrystalPlayDisp(0, 0, alpha);                  /* 634 */
                break;                                                  /* 635 */

            case MENU_RADIO_SUB_NO_CRYSTAL:
                MenuRadioNoHaveCrystalDisp(alpha);                      /* 637 */
                break;                                                  /* 638 */

            default:
                PRINT_WARNING("Error!! MenuRadioDisp()");               /* 640 */
                break;
            }
        }
    }
}

/* The backdrop: one full-screen plate, three tiles down the right-hand
 * column, then two more laid on their side. */
static void MenuRadioBgDisp(int off_x, int off_y, u_char alpha)          /* 654 */
{
    int       i;
    DISP_SPRT bg_ds;

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 660 */

    CopySprDToSpr(&bg_ds, &menu_radio_tex[MR_BG]);                      /* 663 */
    bg_ds.x += (float)off_x;   bg_ds.y += (float)off_y;                 /* 664 */
    bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);                   /* 665 */
    DispSprD(&bg_ds);                                                   /* 666 */

    for (i = 0; i < 3; i++) {                                           /* 669 */
        CopySprDToSpr(&bg_ds, &menu_radio_tex[MR_BG_SIDE + i]);         /* 670 */
        bg_ds.x += (float)off_x;   bg_ds.y += (float)off_y;             /* 671 */
        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 672 */
        DispSprD(&bg_ds);                                               /* 673 */
    }                                                                   /* 674 */

    for (i = 0; i < 2; i++) {                                           /* 675 */
        CopySprDToSpr(&bg_ds, &menu_radio_tex[MR_BG_ROT + i]);          /* 676 */

        bg_ds.x += (float)off_x;
        bg_ds.y  = bg_ds.y + (float)bg_ds.w + (float)off_y;             /* 677 */

        bg_ds.crx = bg_ds.x;                                            /* 678 */
        bg_ds.cry = bg_ds.y;
        bg_ds.rot = 270.0f;

        bg_ds.alpha = (u_char)(bg_ds.alpha * alpha >> 7);               /* 679 */
        DispSprD(&bg_ds);                                               /* 680 */
    }                                                                   /* 681 */
}

/* The page title: the shared plate out of the MENU_BG pak, one half
 * mirrored, and then the page's own word out of the radio pak.  Two paks, so
 * two PK2SendVram() calls. */
static void MenuRadioTitleDisp(int off_x, int off_y, u_char alpha)       /* 691 */
{
    int       i;
    DISP_SPRT title_ds;

    PK2SendVram(MENU_BG_TEX_ADRS, -1, -1, 0);                           /* 697 */

    for (i = 0; i < 2; i++) {                                           /* 700 */
        CopySprDToSpr(&title_ds, &menu_radio_tex[MR_TITLE_PLATE + i]);  /* 701 */
        title_ds.x += (float)off_x;   title_ds.y += (float)off_y;       /* 702 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 703 */
        DispSprD(&title_ds);                                            /* 704 */
    }                                                                   /* 705 */

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 707 */

    for (i = 0; i < 2; i++) {                                           /* 710 */
        CopySprDToSpr(&title_ds, &menu_radio_tex[MR_TITLE + i]);        /* 711 */
        title_ds.x += (float)off_x;   title_ds.y += (float)off_y;       /* 712 */
        title_ds.alpha = (u_char)(title_ds.alpha * alpha >> 7);         /* 713 */
        DispSprD(&title_ds);                                            /* 714 */
    }                                                                   /* 715 */
}

/* The list page, back to front. */
static void MenuRadioCrystalSelDisp(int off_x, int off_y, u_char alpha)  /* 725 */
{
    MenuRadioCrystalNameFrameDisp(off_x, off_y, alpha);                 /* 730 */

    MenuRadioNonHearFrameDisp(off_x, off_y, alpha);                     /* 733 */

    MenuRadioScrollFrameDisp(off_x, off_y, alpha);                      /* 736 */

    MenuRadioScrollDisp(off_x, off_y, alpha);                           /* 739 */

    MenuRadioLoadCrystalDisp(off_x, off_y, alpha);                      /* 742 */

    MenuRadioMsgWinDisp(off_x, off_y, alpha);                           /* 745 */

    MenuRadioTopMsgDisp(off_x, off_y, alpha);                           /* 748 */

    MenuRadioCaptionDisp(off_x, off_y, alpha);                          /* 751 */
}

/* The playing page.  Same parts as the list, plus the recording's subtitle
 * and its own caption group -- and it is what advances title_timer, so the
 * caption only walks while this is the page being drawn. */
static void MenuRadioCrystalPlayDisp(int off_x, int off_y, u_char alpha) /* 761 */
{
    MOVIE_TITLE_DAT *data;
    int              tbl_pos;

    data = GetCrystalTitleDat(
        disp_crystal_data[menu_radio_ctrl.ref_ctrl.data_pos].crystal_id); /* 768 */

    tbl_pos = GetMovieTitleDatTblPos(data, menu_radio_disp.title_timer); /* 772 */

    MenuRadioCrystalNameFrameDisp(off_x, off_y, alpha);                 /* 776 */

    MenuRadioNonHearFrameDisp(off_x, off_y, alpha);                     /* 779 */

    MenuRadioScrollFrameDisp(off_x, off_y, alpha);                      /* 782 */

    MenuRadioScrollDisp(off_x, off_y, alpha);                           /* 785 */

    MenuRadioLoadCrystalDisp(off_x, off_y, alpha);                      /* 788 */

    if (tbl_pos != -1) {                                                /* 790 */
        MovieTitleDispMain(RADIO_TITLE_MSG_TYPE, data[tbl_pos].msg_id,
                           RADIO_TITLE_Y, RADIO_TITLE_COL, 1);          /* 794 */
    }

    MenuRadioMsgWinDisp(off_x, off_y, alpha);                           /* 799 */

    MenuRadioTopMsgDisp(off_x, off_y, alpha);                           /* 802 */

    MenuRadioCrystalPlayCaptionDisp(off_x, off_y, alpha);               /* 805 */

    MenuRadioCrystalTitleTimerDebugDisp();                              /* 809 */

    menu_radio_disp.title_timer++;                                      /* 813 */
}

/* The empty page: one centred message in its own window, and nothing else.
 * Neither the list nor the picture is drawn. */
static void MenuRadioNoHaveCrystalDisp(u_char alpha)                     /* 820 */
{
    DISP_STR    msg_data;
    MSG_WIN_DAT msg_win;

    SetMsgDefData(&msg_data, RADIO_MSG_NO_CRYSTAL);                     /* 826 */
    SetMsgWinDefData(&msg_win, RADIO_MSG_NO_CRYSTAL);                   /* 827 */

    msg_data.alpha = alpha;

    DrawCmnWindow(0, msg_win.x, msg_win.y, msg_win.w, msg_win.h,
                  alpha, 0x66);                                         /* 834 */

    PrintMsg(RADIO_MSG_NO_CRYSTAL, 4, msg_data.pos_x, msg_data.pos_y,
             1, alpha, 0);                                              /* 838 */

    DrawCmnCapGroup_W(RADIO_CAP_GROUP_NO_CRYSTAL,
                      RADIO_CAP_GROUP_NO_CRYSTAL, alpha, 0);            /* 842 */
}

/* The six visible rows: a frame per row and the crystal's name inside it.
 * Which of the two frames is drawn is what marks the selection -- the ink
 * changes with it, but nothing moves. */
static void MenuRadioCrystalNameFrameDisp(int off_x, int off_y,
                                          u_char alpha)                 /* 858 */
{
    short disp_num;
    int   col_label;
    int   i;

    disp_num = menu_radio_ctrl.ref_ctrl.data_num;                       /* 865 */

    if (MENU_RADIO_DISP_NUM < disp_num) {                               /* 867 */
        disp_num = MENU_RADIO_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 874 */
        if (i == menu_wrk.cursor) {                                     /* 876 */
            MenuRadioSelFrameDisp(
                (float)(menu_radio_tex[MR_SEL_LEFT].x + off_x),
                (float)(menu_radio_tex[MR_SEL_LEFT].y
                        + i * MENU_RADIO_ROW_STEP + off_y), alpha);     /* 879 */

            col_label = RADIO_COL_SEL;                                  /* 881 */
        }
        else {
            MenuRadioNonSelFrameDisp(
                (float)(menu_radio_tex[MR_NON_SEL_LEFT].x + off_x),
                (float)(menu_radio_tex[MR_NON_SEL_LEFT].y
                        + i * MENU_RADIO_ROW_STEP + off_y), alpha);     /* 887 */

            col_label = RADIO_COL_NON_SEL;                              /* 889 */
        }

        PrintMsg(RADIO_MSG_NAME,
                 disp_crystal_data[menu_radio_ctrl.ref_ctrl.disp_start_pos
                                   + i].crystal_id,
                 off_x + MENU_RADIO_NAME_X,
                 off_y + MENU_RADIO_NAME_Y + i * MENU_RADIO_ROW_STEP,
                 col_label, alpha, 0xa0);                               /* 895 */
    }                                                                   /* 898 */
}

/* The "not listened to yet" bracket, on every row still at HAVE.  It asks
 * crystal.o live rather than reading DISP_CRYSTAL_DATA::state, which is why
 * pressing CROSS retires a row's bracket on the very next frame. */
static void MenuRadioNonHearFrameDisp(int off_x, int off_y, u_char alpha) /* 908 */
{
    short disp_num;
    int   i;

    disp_num = menu_radio_ctrl.ref_ctrl.data_num;                       /* 914 */

    if (MENU_RADIO_DISP_NUM < disp_num) {                               /* 916 */
        disp_num = MENU_RADIO_DISP_NUM;
    }

    for (i = 0; i < disp_num; i++) {                                    /* 921 */
        if (GetPlyrCrystalState(
                disp_crystal_data[menu_radio_ctrl.ref_ctrl.disp_start_pos
                                  + i].crystal_id) == CRYSTAL_STATE_HAVE) { /* 923 */
            MenuRadioNoHearFrameDisp(
                (float)(off_x + MENU_RADIO_NON_HEAR_X),
                (float)(off_y + MENU_RADIO_NON_HEAR_Y
                        + i * MENU_RADIO_ROW_STEP), alpha);             /* 926 */
        }
    }                                                                   /* 928 */
}

/* The scrollbar's rail -- three pieces, the middle one stretched, all drawn
 * additively.  It is the page's only additive blend. */
static void MenuRadioScrollFrameDisp(int off_x, int off_y, u_char alpha) /* 938 */
{
    float     frame_scl;
    DISP_SPRT scroll_ds;

    frame_scl = RADIO_RAIL_MID_SIZE / (float)menu_radio_tex[MR_RAIL_MID].h; /* 944 */

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 946 */

    CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_RAIL_TOP]);            /* 949 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 950 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 951 */
    scroll_ds.alphar = 0x48;                                            /* 952 */
    DispSprD(&scroll_ds);                                               /* 953 */

    CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_RAIL_MID]);            /* 956 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 957 */

    scroll_ds.csx = scroll_ds.x;                                        /* 958 */
    scroll_ds.csy = scroll_ds.y;
    scroll_ds.scw = 1.0f;
    scroll_ds.sch = frame_scl;

    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 959 */
    scroll_ds.alphar = 0x48;                                            /* 960 */
    DispSprD(&scroll_ds);                                               /* 961 */

    CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_RAIL_BOTTOM]);         /* 964 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 965 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 966 */
    scroll_ds.alphar = 0x48;                                            /* 967 */
    DispSprD(&scroll_ds);                                               /* 968 */
}

/* The thumb and the two arrows.  A list that fits the window gets a
 * full-length thumb; a longer one gets a thumb whose length and position are
 * both derived from the entry count.
 *
 * A thumb shorter than its own two caps has no middle at all, so the else
 * arm draws the two caps overlapping and squashed to half the length each --
 * which is what keeps a 40-entry list's thumb from inverting. */
static void MenuRadioScrollDisp(int off_x, int off_y, u_char alpha)      /* 978 */
{
    float     scroll_size;
    float     scroll_y;
    float     center_size;
    float     scroll_scl;
    DISP_SPRT scroll_ds;

    if (menu_radio_ctrl.ref_ctrl.data_num < MENU_RADIO_DISP_NUM) {      /* 990 */
        scroll_size = RADIO_SCROLL_RAIL_SIZE;                           /* 991 */
        scroll_y    = RADIO_SCROLL_RAIL_TOP;                            /* 992 */
    }
    else {
        scroll_size = RADIO_SCROLL_RAIL_SIZE
                      - (RADIO_SCROLL_RAIL_SIZE
                         / (float)menu_radio_ctrl.ref_ctrl.data_num)
                        * (float)(menu_radio_ctrl.ref_ctrl.data_num
                                  - MENU_RADIO_DISP_NUM);               /* 996 */

        scroll_y = (RADIO_SCROLL_RAIL_SIZE
                    / (float)menu_radio_ctrl.ref_ctrl.data_num)
                   * (float)menu_radio_ctrl.ref_ctrl.disp_start_pos
                   + RADIO_SCROLL_RAIL_TOP;                             /* 998 */
    }

    center_size = scroll_size - (float)(menu_radio_tex[MR_THUMB_TOP].h
                                        + menu_radio_tex[MR_THUMB_BOTTOM].h); /* 1001 */

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 1003 */

    if (0.0f < center_size) {                                           /* 1005 */
        scroll_scl = center_size / (float)menu_radio_tex[MR_THUMB_MID].h; /* 1006 */

        CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_THUMB_TOP]);       /* 1009 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1010 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1011 */
        DispSprD(&scroll_ds);                                           /* 1012 */

        scroll_y += (float)menu_radio_tex[MR_THUMB_TOP].h;              /* 1014 */

        CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_THUMB_MID]);       /* 1017 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1018 */

        scroll_ds.csx = scroll_ds.x;                                    /* 1019 */
        scroll_ds.csy = scroll_ds.y;
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = scroll_scl;

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1020 */
        DispSprD(&scroll_ds);                                           /* 1021 */

        scroll_y += center_size;                                        /* 1023 */

        CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_THUMB_BOTTOM]);    /* 1026 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1027 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1028 */
        DispSprD(&scroll_ds);                                           /* 1029 */
    }
    else {
        CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_THUMB_TOP]);       /* 1032 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + (float)off_y;                         /* 1033 */
        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1034 */

        scroll_ds.csx = scroll_ds.x;                                    /* 1035 */
        scroll_ds.csy = scroll_ds.y;
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (scroll_size * 0.5f) / (float)scroll_ds.h;

        DispSprD(&scroll_ds);                                           /* 1036 */

        CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_THUMB_BOTTOM]);    /* 1037 */
        scroll_ds.x += (float)off_x;
        scroll_ds.y  = scroll_y + scroll_size * 0.5f + (float)off_y;    /* 1038 */

        scroll_ds.csx = scroll_ds.x;                                    /* 1039 */
        scroll_ds.csy = scroll_ds.y;
        scroll_ds.scw = 1.0f;
        scroll_ds.sch = (scroll_size * 0.5f) / (float)scroll_ds.h;

        scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);       /* 1040 */
        DispSprD(&scroll_ds);                                           /* 1041 */
    }

    CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_SCROLL_ARROW_UP]);     /* 1045 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 1046 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1047 */
    scroll_ds.r = menu_radio_disp.rgb;                                  /* 1048 */
    scroll_ds.g = menu_radio_disp.rgb;
    scroll_ds.b = menu_radio_disp.rgb;
    DispSprD(&scroll_ds);                                               /* 1049 */

    CopySprDToSpr(&scroll_ds, &menu_radio_tex[MR_SCROLL_ARROW_DOWN]);   /* 1051 */
    scroll_ds.x += (float)off_x;   scroll_ds.y += (float)off_y;         /* 1052 */
    scroll_ds.alpha = (u_char)(scroll_ds.alpha * alpha >> 7);           /* 1053 */
    scroll_ds.r = menu_radio_disp.rgb;                                  /* 1054 */
    scroll_ds.g = menu_radio_disp.rgb;
    scroll_ds.b = menu_radio_disp.rgb;
    DispSprD(&scroll_ds);                                               /* 1055 */
}

/* The crystal picture and its flare -- the page's only real animation, and
 * the reason MENU_RADIO_DISP carries a second (step, timer) pair.
 *
 * The two out tables are stack copies because case OUT rewrites their
 * start_alpha to wherever the fade had got to; the two in tables never need
 * patching and so are shared statics in .rodata. */
static void MenuRadioLoadCrystalDisp(int off_x, int off_y, u_char alpha) /* 1065 */
{
    static const ALPHA_ANIM_TBL crystal_in_alpha_tbl[2] =               /* rdata 3bea90 */
    {
        { MENU_RADIO_CRYSTAL_ALPHA_MIN, MENU_RADIO_CRYSTAL_ALPHA_MAX,
          0, MENU_RADIO_CRYSTAL_IN_TIME },
        { -1, -1, -1, -1 }
    };

    ALPHA_ANIM_TBL crystal_out_alpha_tbl[2] =                           /* 1079 */
    {
        { MENU_RADIO_CRYSTAL_ALPHA_MAX, MENU_RADIO_CRYSTAL_ALPHA_MIN,
          0, MENU_RADIO_CRYSTAL_OUT_TIME },
        { -1, -1, -1, -1 }
    };

    static const ALPHA_ANIM_TBL crystal_flare_in_alpha_tbl[2] =         /* rdata 3beab0 */
    {
        { 0, MENU_RADIO_CRYSTAL_ALPHA_MAX, 0, MENU_RADIO_CRYSTAL_IN_TIME },
        { -1, -1, -1, -1 }
    };

    ALPHA_ANIM_TBL crystal_flare_out_alpha_tbl[2] =                     /* 1091 */
    {
        { MENU_RADIO_CRYSTAL_ALPHA_MAX, 0, 0, MENU_RADIO_CRYSTAL_OUT_TIME },
        { -1, -1, -1, -1 }
    };

    int disp_num = menu_radio_ctrl.ref_ctrl.data_num;                   /* 1098 */

    if (MENU_RADIO_DISP_NUM < disp_num) {                               /* 1100 */
        disp_num = MENU_RADIO_DISP_NUM;
    }

    u_char fade_alpha[2] = { 0, 0 };                                    /* 1105 */

    u_char crystal_alpha = MENU_RADIO_CRYSTAL_ALPHA_MIN;                /* 1108 */
    u_char flare_alpha   = 0;                                           /* 1109 */

    switch (menu_radio_disp.crystal_anim_step) {                        /* 1112 */
    case MENU_RADIO_CRYSTAL_ANIM_START:
        menu_radio_disp.crystal_anim_step   = MENU_RADIO_CRYSTAL_ANIM_IN; /* 1114 */
        menu_radio_disp.crystal_anim_timer  = 0;                        /* 1115 */
        menu_radio_disp.crystal_alpha       = MENU_RADIO_CRYSTAL_ALPHA_MIN; /* 1116 */
        menu_radio_disp.crystal_flare_alpha = 0;                        /* 1117 */

        /* falls through -- the first frame of the brighten runs here */

    case MENU_RADIO_CRYSTAL_ANIM_IN:
        menu_radio_disp.crystal_alpha =
            Anim2D_CalcNowAlpha(crystal_in_alpha_tbl,
                                menu_radio_disp.crystal_anim_timer);    /* 1121 */

        menu_radio_disp.crystal_flare_alpha =
            Anim2D_CalcNowAlpha(crystal_flare_in_alpha_tbl,
                                menu_radio_disp.crystal_anim_timer);    /* 1123 */

        crystal_alpha = menu_radio_disp.crystal_alpha;                  /* 1125 */
        flare_alpha   = menu_radio_disp.crystal_flare_alpha;            /* 1126 */

        menu_radio_disp.crystal_anim_timer++;                           /* 1128 */

        if (MENU_RADIO_CRYSTAL_IN_TIME
            <= menu_radio_disp.crystal_anim_timer) {                    /* 1129 */
            menu_radio_disp.crystal_anim_step = MENU_RADIO_CRYSTAL_ANIM_KEEP; /* 1130 */
        }
        break;                                                          /* 1132 */

    case MENU_RADIO_CRYSTAL_ANIM_KEEP:
        crystal_alpha = MENU_RADIO_CRYSTAL_ALPHA_MAX;                   /* 1134 */
        flare_alpha   = MENU_RADIO_CRYSTAL_ALPHA_MAX;                   /* 1135 */
        break;                                                          /* 1136 */

    case MENU_RADIO_CRYSTAL_ANIM_OUT:
        /* Start the ramp from where the brighten actually got to, so a
         * recording stopped early does not snap to full first. */
        crystal_out_alpha_tbl[0].start_alpha =
            menu_radio_disp.crystal_alpha;                              /* 1138 */
        crystal_flare_out_alpha_tbl[0].start_alpha =
            menu_radio_disp.crystal_flare_alpha;                        /* 1139 */

        crystal_alpha =
            Anim2D_CalcNowAlpha(crystal_out_alpha_tbl,
                                menu_radio_disp.crystal_anim_timer);    /* 1142 */

        flare_alpha =
            Anim2D_CalcNowAlpha(crystal_flare_out_alpha_tbl,
                                menu_radio_disp.crystal_anim_timer);    /* 1144 */

        menu_radio_disp.crystal_anim_timer++;                           /* 1146 */

        if (MENU_RADIO_CRYSTAL_OUT_TIME
            <= menu_radio_disp.crystal_anim_timer) {                    /* 1147 */
            menu_radio_disp.crystal_anim_step = MENU_RADIO_CRYSTAL_ANIM_END; /* 1148 */
        }
        break;                                                          /* 1150 */

    case MENU_RADIO_CRYSTAL_ANIM_END:
        crystal_alpha = (u_char)(alpha * MENU_RADIO_CRYSTAL_ALPHA_MIN >> 7); /* 1152 */
        flare_alpha   = 0;                                              /* 1153 */
        break;                                                          /* 1154 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1156 */
        break;
    }

    switch (menu_radio_ctrl.sub_step) {                                 /* 1160 */
    case MENU_RADIO_SUB_SEL:
        if (disp_num == 0) {                                            /* 1162 */
            break;
        }

        GetMenuCrossFadeAlpha(fade_alpha);                              /* 1164 */

        /* While the page itself is fading out the outgoing picture has to
         * go with it rather than keep cross-fading, so its slot is muted. */
        if (menu_wrk.step != MENU_RADIO_STEP_MAIN) {                    /* 1166 */
            fade_alpha[menu_radio_ctrl.cross_fade_flg ^ 1] = 0;         /* 1167 */
        }

        if (CheckCrossFadeDisp(menu_radio_ctrl.cross_fade_flg ^ 1)) {   /* 1170 */
            if (menu_radio_disp.crystal_anim_step
                == MENU_RADIO_CRYSTAL_ANIM_END) {                       /* 1171 */
                MenuRadioCrystalDisp(
                    off_x, off_y,
                    (u_char)(fade_alpha[menu_radio_ctrl.cross_fade_flg ^ 1]
                             * MENU_RADIO_CRYSTAL_ALPHA_MIN >> 7),
                    GetCrossFadeDataAddr(menu_radio_ctrl.cross_fade_flg ^ 1)); /* 1174 */
            }
        }

        if (menu_wrk.step != MENU_RADIO_STEP_MAIN) {                    /* 1178 */
            fade_alpha[menu_radio_ctrl.cross_fade_flg] = alpha;         /* 1179 */
        }

        if (CheckCrossFadeDisp(menu_radio_ctrl.cross_fade_flg)) {       /* 1182 */
            if (menu_radio_disp.crystal_anim_step
                == MENU_RADIO_CRYSTAL_ANIM_END) {                       /* 1183 */
                MenuRadioCrystalDisp(
                    off_x, off_y,
                    (u_char)(fade_alpha[menu_radio_ctrl.cross_fade_flg]
                             * MENU_RADIO_CRYSTAL_ALPHA_MIN >> 7),
                    GetCrossFadeDataAddr(menu_radio_ctrl.cross_fade_flg)); /* 1186 */
            }
            else {
                /* The brighten is still running, so the picture carries the
                 * animation's own alpha and the flare comes with it.  GCC
                 * cross-jumped this pair onto case PLAY's copy below, so
                 * these two lines have no measured numbers of their own. */
                MenuRadioCrystalDisp(
                    off_x, off_y, crystal_alpha,
                    GetCrossFadeDataAddr(menu_radio_ctrl.cross_fade_flg)); /* 1190 */

                MenuRadioCrystalFlareDisp(
                    off_x, off_y, flare_alpha,
                    GetCrossFadeDataAddr(menu_radio_ctrl.cross_fade_flg)); /* 1192 */
            }
        }
        break;                                                          /* 1194 */

    case MENU_RADIO_SUB_PLAY:
        if (CheckCrossFadeDisp(menu_radio_ctrl.cross_fade_flg)) {       /* 1198 */
            MenuRadioCrystalDisp(
                off_x, off_y, crystal_alpha,
                GetCrossFadeDataAddr(menu_radio_ctrl.cross_fade_flg));  /* 1200 */

            MenuRadioCrystalFlareDisp(
                off_x, off_y, flare_alpha,
                GetCrossFadeDataAddr(menu_radio_ctrl.cross_fade_flg));  /* 1202 */
        }
        break;                                                          /* 1204 */

    case MENU_RADIO_SUB_NO_CRYSTAL:
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1208 */
        break;
    }
}

/* The stone itself, out of whichever cross-fade slot the caller picked --
 * which is why this takes the pak address rather than reading one. */
static void MenuRadioCrystalDisp(int off_x, int off_y, u_char alpha,
                                 void *pk2_addr)                        /* 1221 */
{
    DISP_SPRT crystal_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 1226 */

    CopySprDToSpr(&crystal_ds, &menu_radio_tex[MR_CRYSTAL]);            /* 1230 */
    crystal_ds.x += (float)off_x;   crystal_ds.y += (float)off_y;       /* 1231 */
    crystal_ds.alpha = (u_char)(crystal_ds.alpha * alpha >> 7);         /* 1232 */
    DispSprD(&crystal_ds);                                              /* 1233 */
}

/* The halo behind the stone plus the three word plates under it -- four
 * sprites on one alpha, so they all arrive with the recording. */
static void MenuRadioCrystalFlareDisp(int off_x, int off_y, u_char alpha,
                                      void *pk2_addr)                   /* 1244 */
{
    int       i;
    DISP_SPRT flare_ds;

    PK2SendVram((uintptr_t)pk2_addr, -1, -1, 0);                        /* 1250 */

    for (i = 0; i < 4; i++) {                                           /* 1254 */
        CopySprDToSpr(&flare_ds, &menu_radio_tex[MR_CRYSTAL_FLARE + i]); /* 1255 */
        flare_ds.x += (float)off_x;   flare_ds.y += (float)off_y;       /* 1256 */
        flare_ds.alpha = (u_char)(flare_ds.alpha * alpha >> 7);         /* 1257 */
        DispSprD(&flare_ds);                                            /* 1258 */
    }                                                                   /* 1259 */
}

static void MenuRadioMsgWinDisp(int off_x, int off_y, u_char alpha)      /* 1269 */
{
    DrawCmnWindow(0, RADIO_MSG_WIN_X, RADIO_MSG_WIN_Y,
                  RADIO_MSG_WIN_W, RADIO_MSG_WIN_H, alpha, 0x80);       /* 1273 */
}

/* The selected crystal's explanation, in the window above.  Keyed by
 * crystal_id, not by the row. */
static void MenuRadioTopMsgDisp(int off_x, int off_y, u_char alpha)      /* 1283 */
{
    PrintMsg(RADIO_MSG_EXP,
             disp_crystal_data[menu_radio_ctrl.ref_ctrl.data_pos].crystal_id,
             off_x + 0x30, off_y + 0x15a, 1, alpha, 0);                 /* 1289 */
}

/* The list page's button captions: the shared group plus the PLAY plate,
 * whose x depends on how long the word is in this language. */
static void MenuRadioCaptionDisp(int off_x, int off_y, u_char alpha)     /* 1298 */
{
    DISP_SPRT cap_ds;

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 1303 */

    DrawCmnCapGroup_W(RADIO_CAP_GROUP_SEL, RADIO_CAP_GROUP_SEL, alpha, 0); /* 1307 */

    CopySprDToSpr(&cap_ds, &menu_radio_tex[MR_CAP_PLAY]);               /* 1310 */
    cap_ds.x  = (float)(play_cap_tbl[GetLanguage()] + off_x);
    cap_ds.y += (float)off_y;                                           /* 1311 */
    cap_ds.alpha = (u_char)(cap_ds.alpha * alpha >> 7);                 /* 1312 */
    DispSprD(&cap_ds);                                                  /* 1313 */
}

/* The same, for the playing page: two STOP plates rather than one PLAY. */
static void MenuRadioCrystalPlayCaptionDisp(int off_x, int off_y,
                                            u_char alpha)               /* 1348 */
{
    DISP_SPRT cap_ds;

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 1353 */

    DrawCmnCapGroup_W(RADIO_CAP_GROUP_PLAY, RADIO_CAP_GROUP_PLAY, alpha, 0); /* 1357 */

    CopySprDToSpr(&cap_ds, &menu_radio_tex[MR_CAP_STOP_1]);             /* 1360 */
    cap_ds.x  = (float)(stop_cap_tbl_1[GetLanguage()] + off_x);
    cap_ds.y += (float)off_y;                                           /* 1361 */
    cap_ds.alpha = (u_char)(cap_ds.alpha * alpha >> 7);                 /* 1362 */
    DispSprD(&cap_ds);                                                  /* 1363 */

    CopySprDToSpr(&cap_ds, &menu_radio_tex[MR_CAP_STOP_2]);             /* 1365 */
    cap_ds.x  = (float)(stop_cap_tbl_2[GetLanguage()] + off_x);
    cap_ds.y += (float)off_y;                                           /* 1366 */
    cap_ds.alpha = (u_char)(cap_ds.alpha * alpha >> 7);                 /* 1367 */
    DispSprD(&cap_ds);                                                  /* 1368 */
}

/* The selected row's frame: left cap, a body stretched sideways to 138 px,
 * and the left cap again mirrored. */
static void MenuRadioSelFrameDisp(float x, float y, u_char alpha)        /* 1401 */
{
    float     center_scl;
    DISP_SPRT frame_ds;

    center_scl = RADIO_SEL_FRAME_SIZE
                 / (float)menu_radio_tex[MR_SEL_MID].w;                 /* 1408 */

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 1410 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_SEL_LEFT]);             /* 1414 */
    frame_ds.x = x;   frame_ds.y = y;                                   /* 1415 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1416 */
    DispSprD(&frame_ds);                                                /* 1417 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_SEL_MID]);              /* 1420 */
    frame_ds.x = x + (float)menu_radio_tex[MR_SEL_LEFT].w;
    frame_ds.y = y;                                                     /* 1421 */

    frame_ds.csx = frame_ds.x;                                          /* 1422 */
    frame_ds.csy = frame_ds.y;
    frame_ds.scw = center_scl;
    frame_ds.sch = 1.0f;

    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1423 */
    DispSprD(&frame_ds);                                                /* 1424 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_SEL_RIGHT]);            /* 1427 */
    frame_ds.x = x + (float)menu_radio_tex[MR_SEL_LEFT].w
                 + RADIO_SEL_FRAME_SIZE;
    frame_ds.y = y;                                                     /* 1428 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1429 */
    DispSprD(&frame_ds);                                                /* 1430 */
}

/* An unselected row's frame.  Same three-piece shape, but the body is a
 * short wide strip rotated 90 and stretched through sch, so its rotation
 * centre has to sit at the far end of the stretched length. */
static void MenuRadioNonSelFrameDisp(float x, float y, u_char alpha)     /* 1440 */
{
    float     center_scl;
    DISP_SPRT frame_ds;

    center_scl = RADIO_NON_SEL_FRAME_SIZE
                 / (float)menu_radio_tex[MR_NON_SEL_MID].h;             /* 1447 */

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 1449 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_NON_SEL_LEFT]);         /* 1453 */
    frame_ds.x = x;   frame_ds.y = y;                                   /* 1454 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1455 */
    DispSprD(&frame_ds);                                                /* 1456 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_NON_SEL_MID]);          /* 1459 */
    frame_ds.x = x + (float)menu_radio_tex[MR_NON_SEL_LEFT].w
                 + (float)frame_ds.h * center_scl;
    frame_ds.y = y;                                                     /* 1460 */

    frame_ds.crx = frame_ds.x;                                          /* 1461 */
    frame_ds.cry = frame_ds.y;
    frame_ds.rot = 90.0f;

    frame_ds.csx = frame_ds.x;                                          /* 1462 */
    frame_ds.csy = frame_ds.y;
    frame_ds.scw = 1.0f;
    frame_ds.sch = center_scl;

    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1463 */
    DispSprD(&frame_ds);                                                /* 1464 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_NON_SEL_RIGHT]);        /* 1467 */
    frame_ds.x = x + (float)menu_radio_tex[MR_NON_SEL_LEFT].w
                 + RADIO_NON_SEL_FRAME_SIZE;
    frame_ds.y = y;                                                     /* 1468 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1469 */
    DispSprD(&frame_ds);                                                /* 1470 */
}

/* The unread bracket: two ticks, 255 px apart, marking a row the player has
 * picked the stone up but never played it. */
static void MenuRadioNoHearFrameDisp(float x, float y, u_char alpha)     /* 1480 */
{
    DISP_SPRT frame_ds;

    PK2SendVram((uintptr_t)menu_radio_tex_addr, -1, -1, 0);             /* 1485 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_NON_HEAR_LEFT]);        /* 1489 */
    frame_ds.x = x;   frame_ds.y = y;                                   /* 1490 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1491 */
    DispSprD(&frame_ds);                                                /* 1492 */

    CopySprDToSpr(&frame_ds, &menu_radio_tex[MR_NON_HEAR_RIGHT]);       /* 1494 */
    frame_ds.x = x + RADIO_NON_HEAR_FRAME_SIZE;
    frame_ds.y = y;                                                     /* 1495 */
    frame_ds.alpha = (u_char)(frame_ds.alpha * alpha >> 7);             /* 1496 */
    DispSprD(&frame_ds);                                                /* 1497 */
}

/* --------------------------------------------------------------------------
 *  The subtitle-authoring tools
 *
 *  These two exist to fill in crystal_dat.c.  Holding the debug button
 *  prints the current title_timer with a trailing comma and releasing it
 *  prints it with a newline, so a whole session's output pastes straight
 *  into a MOVIE_TITLE_DAT table -- which is what the "start,    end" banner
 *  MenuRadioPad() printfs at the top of a recording is heading.
 * ------------------------------------------------------------------------ */

static void MenuRadioCrystalTitleDebugPad(void)                          /* 1509 */
{
    if (pad[0].now & PAD_NOW_TITLE_DEBUG) {                             /* 1514 */
        if (crystal_title_debug_flg == 0) {                             /* 1515 */
            printf("%6d, ", menu_radio_disp.title_timer);               /* 1516 */
        }

        crystal_title_debug_flg = 1;                                    /* 1519 */
    }
    else {
        if (crystal_title_debug_flg != 0) {                             /* 1522 */
            printf("%6d\n", menu_radio_disp.title_timer);               /* 1523 */
            crystal_title_debug_flg = 0;                                /* 1524 */
        }
    }
}

static void MenuRadioCrystalTitleTimerDebugDisp(void)                    /* 1533 */
{
    SetASCIIString2(0, 470.0f, 10.0f, 1, 0xff, 0xff, 0xff, "TIMER");    /* 1538 */

    PrintNumber(menu_radio_disp.title_timer, 0x226, 10, 0, 0x80, 0, 1); /* 1539 */
}
