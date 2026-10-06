// FILE: /home/zero_rom/zero2np/src/album/prg/album_save.c
//
// The album's memory-card save screen.  All four ZERO2.MAP exports plus the
// 42 file-local helpers are reconstructed, which is every function
// functions.txt lists for the object bar the fixed_array<> boilerplate.
//
// Read album_load.c first.  It is the same screen with the writes taken out,
// it is already reconstructed, and it establishes every convention this file
// follows: the same two modes (slot select, then the card machine), the same
// Init-falls-into-Wait step pairs, the same drawing layer, the same staging
// buffer borrowed off the packet ring.  Most of the work here was recognising
// the twin -- the differences are worth naming instead.
//
//  * Nineteen more card states.  album_load has 15; this has 29, because a
//    save has to cope with an album directory that is not there yet
//    (new-make), one that is broken (remake: delete then new-make), and a card
//    with no format at all (format, then a two-second hold on "Format
//    Successful", then new-make).  Same five recovery branches
//    save_load/prg/game_data_save.c has, in the same order.
//
//  * The listing in steps 2/3 records nothing.  album_load keeps
//    album_flg[5] so its cursor can skip albums that are not on the card;
//    a save can target any of the five, so ALBUM_SAVE_CTRL has no such field
//    and AlbumSaveMcGetDirInfoWait() walks all five directories purely to
//    surface an error.  The cursor then starts at album 0 every time and moves
//    with a plain `% 5`.
//
//  * The album takes the type of the slot it is saved into.
//    AlbumSaveMcSaveInit() stores album_sel_csr into
//    album_info[GetCurrentAlbum()].album_type before the transfer, so the
//    spine art the edit view draws afterwards is the card position's, not
//    whatever the album was before.
//
//  * A save writes one file, and the file list is still a list.
//    `int save_file_label[1] = { 0 };` is a local array, save_file_cnt walks
//    it, and AlbumSaveMcSaveWait() loops back to SAVE_INIT until the count
//    reaches ALBUM_SAVE_FILE_NUM.  The loop runs exactly once; the shape is
//    game_data_save.c's three-file version with two entries taken out.
//
//  * There are two every-frame card checks, not one.
//    AlbumSaveAlbumSelMcEveryFrameCheck() is the album-select page's, and it
//    differs from AlbumSaveMcEveryFrameCheck() in exactly one case: an
//    unformatted card (-2) is ignored there rather than reported, because the
//    format prompt has its own path in and the album list is still valid.
//    AlbumSaveMcFormatConfWait() carries a third copy of the same triage for
//    the same reason.
//
// The object has no static data at all: .rodata (3a0e70, 0x415) is the
// fixed_array<> assert literal, the six __FUNCTION__ strings, the file name,
// the two banner formats, the "10ALBUM_INFO" type_info name and eight jump
// tables; .sdata (3ef420, 0x44) is the fixed_array<> type names plus
// album_save_buff_addr.  ALBUM_INFO's type_info node and the linkonce
// _fixed_array_verifyrange<ALBUM_INFO> are emitted here because this is the
// only file outside album.o that subscripts album_info[].
//
// Verified 4/4 against ZERO2.MAP's exports and 46/46 against functions.txt.
// .text is accounted for byte-for-byte -- 0x1260b8..0x128388 = 0x22d0, code
// plus one 4-byte alignment fill per body, so there is no unlisted body.
// ALBUM_SAVE_CTRL (0x9) and ALBUM_SAVE_DISP (0x8) are confirmed by an
// offsetof harness, and the machine is driven step-by-step against a
// transcription of its disassembly over every card result code.
//
// NOT YET REACHABLE.  album_edit.o is still a stub, and its two mode tables
// are the only things that call any of the four exports.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), album_save.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from symbols.txt's $LM/SOL records.  A few are interpolated into a measured
// gap: statements whose only memory access goes through fixed_array<>'s
// inlined operator[] leave no $LM of their own, and a store GCC folded into a
// branch delay slot loses its note the same way.  They say so at the site.

#include "album_save.h"

#include "album.h"                                  // album_info[] / accessors
#include "album_disp.h"                             // AlbumInOutAnimCtrl / Album*Disp
#include "album_edit.h"                             // AlbumEditMenuDelete
#include "album_mem.h"                              // AlbumMemInit / Main / Free
#include "../../common/utility2.h"                  // PRINT_ASSERT
#include "../../common/variable.h"                  // pad[] / paddat[]
#include "../../graphics/graph2d/draw_cmn.h"        // DrawCmnWindow / DrawCmnYesNoSel
#include "../../graphics/graph2d/message.h"         // PrintMsg
#include "../../graphics/graph2d/tim2.h"            // PK2SendVram
#include "../../graphics/graph3d/ctl/fixed_array.h" // fixed_array<ALBUM_INFO,2>
#include "../../ingame/menu/zero2_anim2d.h"         // Zero2Anim2D_CsrAnimCtrl / STEP_*
#include "../../system/eeiop/cddat.h"               // ALBM_SL_PK2 / ALBM_SLOT_SL_PK2
#include "../../system/eeiop/fileload.h"            // FileLoadIsEnd2
#include "../../system/mc/prg/mc.h"                 // MemoryCardExeInit / MemoryCardEnd
#include "../../system/mc/prg/mc_check.h"           // MemoryCardCheckInit / Main
#include "../../system/mc/prg/mc_check_broken.h"    // MemoryCardCheckDirBroken
#include "../../system/mc/prg/mc_check_card.h"      // MemoryCardGetCardInfo* / EveryFrame*
#include "../../system/mc/prg/mc_check_dir.h"       // MemoryCardGetDirInfo*
#include "../../system/mc/prg/mc_check_empty.h"     // MemoryCardCheckEmpty(Broken)
#include "../../system/mc/prg/mc_del_dir.h"         // MemoryCardDirDel*
#include "../../system/mc/prg/mc_format.h"          // MemoryCardFormat*
#include "../../system/mc/prg/mc_make.h"            // MemoryCardNewMake*
#include "../../system/mc/prg/mc_save.h"            // MemoryCardFileSave*
#include "../../system/mc/prg/mc_set_data.h"        // path / size / data-area
#include "../../system/os/system.h"                 // SystemBankPlay
#include "../../system/pad/pad.h"                   // GetPadAnalogRpt

#include <string.h>                                 // memset

/* The two MEMORY CARD slots the console has. */
#define ALBUM_SAVE_SLOT_MAX     2

/* Album directories a card can hold: dir_label 1..5, one per album.  0 is the
 * game-data directory, which this screen never touches. */
#define ALBUM_SAVE_ALBUM_MAX    5

/* Files one save writes: save_file_label[] below has exactly one entry. */
#define ALBUM_SAVE_FILE_NUM     1

/* Staging buffer AlbumMemInit() claims off the packet ring -- a quarter of it.
 * It is also the working buffer MemoryCardNewMakeInit() gets. */
#define ALBUM_SAVE_MEM_SIZE     0x100000

/* Path buffers are memset to 0x37 bytes by every caller; that is the ROM's
 * literal size, not a sizeof(). */
#define ALBUM_SAVE_PATH_NAME_LEN 55

/* "Format Successful." is held for two seconds before the screen goes on to
 * make the directory.  Same figure game_data_save.c uses. */
#define ALBUM_SAVE_FORMAT_END_TIME 60

/* album_save_ctrl.step -- the screen itself. */
enum ALBUM_SAVE_STEP
{
    ALBUM_SAVE_DISP_INIT = 0,   /* reset the display state, claim the buffer  */
    ALBUM_SAVE_LOAD_WAIT = 1,   /* waiting on the two paks and the buffer     */
    ALBUM_SAVE_MC_EXE    = 2,   /* one of the two modes has the screen        */
    ALBUM_SAVE_OUT       = 3    /* animating out; Main() then returns 1       */
};

/* album_save_ctrl.mode.  The two halves of the page. */
#define ALBUM_SAVE_MODE_SLOT_SEL    0   /* which MEMORY CARD slot            */
#define ALBUM_SAVE_MODE_MC_SAVE     1   /* the card machine below            */

/* album_save_ctrl.mc_step.  The ROM's debug info carries no enum for these --
 * the names are taken from the step handlers' own ROM symbol names, so the
 * values still read against the jump table at rodata 3a0f50. */
enum ALBUM_SAVE_MC_STEP
{
    ALBUM_SAVE_MC_CARD_CHECK_INIT      = 0,
    ALBUM_SAVE_MC_CARD_CHECK_WAIT      = 1,
    ALBUM_SAVE_MC_GET_DIR_INFO_INIT    = 2,
    ALBUM_SAVE_MC_GET_DIR_INFO_WAIT    = 3,
    ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT  = 4,
    ALBUM_SAVE_MC_SAVE_ALBUM_SEL_WAIT  = 5,
    ALBUM_SAVE_MC_CHECK_AGAIN_INIT     = 6,
    ALBUM_SAVE_MC_CHECK_AGAIN_WAIT     = 7,
    ALBUM_SAVE_MC_SAVE_CONF_INIT       = 8,
    ALBUM_SAVE_MC_SAVE_CONF_WAIT       = 9,
    ALBUM_SAVE_MC_SAVE_INIT            = 10,
    ALBUM_SAVE_MC_SAVE_WAIT            = 11,
    ALBUM_SAVE_MC_ERROR_CONF_INIT      = 12,
    ALBUM_SAVE_MC_ERROR_CONF_WAIT      = 13,
    ALBUM_SAVE_MC_END_CONF             = 14,
    ALBUM_SAVE_MC_REMAKE_CONF_INIT     = 15,
    ALBUM_SAVE_MC_REMAKE_CONF_WAIT     = 16,
    ALBUM_SAVE_MC_REMAKE_DIR_DEL_INIT  = 17,
    ALBUM_SAVE_MC_REMAKE_DIR_DEL_WAIT  = 18,
    ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT   = 19,
    ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT   = 20,
    ALBUM_SAVE_MC_NEW_MAKE_INIT        = 21,
    ALBUM_SAVE_MC_NEW_MAKE_WAIT        = 22,
    ALBUM_SAVE_MC_FORMAT_CONF_INIT     = 23,
    ALBUM_SAVE_MC_FORMAT_CONF_WAIT     = 24,
    ALBUM_SAVE_MC_FORMAT_INIT          = 25,
    ALBUM_SAVE_MC_FORMAT_WAIT          = 26,
    ALBUM_SAVE_MC_FORMAT_END_INIT      = 27,
    ALBUM_SAVE_MC_FORMAT_END_WAIT      = 28,
    ALBUM_SAVE_MC_STEP_MAX             = 29
};

/* Message ids in bank 0x50, decoded out of IMG_BD.BIN (CD file 0xd38, the
 * English message file) so the machine below reads.  The ROM writes them as
 * bare numbers and so does loadgame.c; the names are the port's. */
#define ALBUM_SAVE_MSG_NONE             0x00    /* "Checking memory card..."      */
#define ALBUM_SAVE_MSG_NO_CARD          0x01    /* "No memory card in slot n"     */
#define ALBUM_SAVE_MSG_READ_FAILED      0x02    /* "Failed to read memory card"   */
#define ALBUM_SAVE_MSG_UNFORMATTED_CONF 0x03    /* "...unformatted. Format it?"   */
#define ALBUM_SAVE_MSG_FORMATTING       0x04    /* "Formatting memory card."      */
#define ALBUM_SAVE_MSG_FORMAT_FAILED    0x05    /* "Format failed!"               */
#define ALBUM_SAVE_MSG_WHICH_SLOT       0x06    /* "Which MEMORY CARD slot..."    */
#define ALBUM_SAVE_MSG_SAVE_FAILED      0x07    /* "Save failed!"                 */
#define ALBUM_SAVE_MSG_SAVE_OK          0x08    /* "Save completed."              */
#define ALBUM_SAVE_MSG_OVERWRITE_CONF   0x0b    /* "OK to overwrite?"             */
#define ALBUM_SAVE_MSG_UNFORMATTED      0x0d    /* "Memory card is unformatted."  */
#define ALBUM_SAVE_MSG_NEW_MAKE_CONF    0x1a    /* "Album Data will be created."  */
#define ALBUM_SAVE_MSG_NO_SPACE         0x26    /* "Insufficient free space..."   */
#define ALBUM_SAVE_MSG_SEL_ALBUM        0x27    /* "Select Album to save to."     */
#define ALBUM_SAVE_MSG_REMAKE_CONF      0x28    /* "...corrupt. Create new file?" */
#define ALBUM_SAVE_MSG_SAVING           0x29    /* "Saving album data."           */
#define ALBUM_SAVE_MSG_CARD_ERROR       0x2b    /* "...error! Data may be corrupt."*/
#define ALBUM_SAVE_MSG_MAKE_FAILED      0x2f    /* "Save failed!" (the make copy) */
#define ALBUM_SAVE_MSG_CREATING         0x30    /* "Creating album data."         */
#define ALBUM_SAVE_MSG_FORMAT_OK        0x39    /* "Format Successful."           */

/* The album is marshalled into the staging buffer, never written to the card
 * straight out of its own pages -- SetMemoryCardSaveDataToBuff() is what fills
 * it, through save_data.c's save_album_data[] manifest. */
static void *album_save_buff_addr;                          /* sdata 3ef460 */

/* types.txt.  No album_flg here -- see the banner. */
typedef struct                      /* 0x9 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mc_step;
    /* 0x2 */ char mode;
    /* 0x3 */ char dir_check_cnt;   /* how many album dirs have been probed */
    /* 0x4 */ char slot_csr;        /* MEMORY CARD port                     */
    /* 0x5 */ char album_sel_csr;   /* album, = dir_label - 1               */
    /* 0x6 */ char conf_csr;        /* 0 yes, 1 no                          */
    /* 0x7 */ char save_file_cnt;   /* index into save_file_label[]         */
    /* 0x8 */ char format_end_cnt;  /* frames the format banner is held     */
} ALBUM_SAVE_CTRL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char anim_step;       /* Zero2Anim2D in/out step */
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char csr_anim_timer;  /* cursor pulse            */
    /* 0x4 */ int  msg_id;
} ALBUM_SAVE_DISP;

static ALBUM_SAVE_CTRL album_save_ctrl;                     /* bss  422130 */
static ALBUM_SAVE_DISP album_save_disp;                     /* sbss 3f4ac8 */

static int  AlbumSaveTexLoadWait(void);
static void AlbumSaveModeMain(void);
static void AlbumSaveSlotSelPad(void);
static void AlbumSaveOutReq(void);
static void AlbumSaveMcSave(void);
static void AlbumSaveMcCardCheckInit(void);
static void AlbumSaveMcCardCheckWait(void);
static void AlbumSaveMcGetDirInfoInit(void);
static void AlbumSaveMcGetDirInfoWait(void);
static void AlbumSaveMcSaveAlbumSelInit(void);
static void AlbumSaveMcSaveAlbumSelWait(void);
static void AlbumSaveMcSaveAlbumSelPad(void);
static void AlbumSaveAlbumSelMcEveryFrameCheck(void);
static void AlbumSaveMcCheckAgainInit(void);
static void AlbumSaveMcCheckAgainWait(void);
static void AlbumSaveMcSaveConfInit(void);
static void AlbumSaveMcSaveConfWait(void);
static void AlbumSaveMcSaveConfPad(void);
static void AlbumSaveMcSaveInit(void);
static void AlbumSaveMcSaveWait(void);
static void AlbumSaveMcErrorConfInit(void);
static void AlbumSaveMcErrorConfWait(void);
static void AlbumSaveMcErrorConfPad(void);
static void AlbumSaveMcEndConf(void);
static void AlbumSaveMcRemakeConfInit(void);
static void AlbumSaveMcRemakeConfWait(void);
static void AlbumSaveMcRemakeConfPad(void);
static void AlbumSaveMcRemakeDirDelInit(void);
static void AlbumSaveMcRemakeDirDelWait(void);
static void AlbumSaveMcNewMakeConfInit(void);
static void AlbumSaveMcNewMakeConfWait(void);
static void AlbumSaveMcNewMakeConfPad(void);
static void AlbumSaveMcNewMakeInit(void);
static void AlbumSaveMcNewMakeWait(void);
static void AlbumSaveMcFormatConfInit(void);
static void AlbumSaveMcFormatConfWait(void);
static void AlbumSaveMcFormatConfPad(void);
static void AlbumSaveMcFormatInit(void);
static void AlbumSaveMcFormatWait(void);
static void AlbumSaveMcFormatEndInit(void);
static void AlbumSaveMcFormatEndWait(void);
static void AlbumSaveMcEveryFrameCheck(void);
static void AlbumSaveDispInit(void);
static void AlbumSaveSlotSelDisp(int off_x, int off_y, u_char alpha);
static void AlbumSaveMcSaveDisp(int off_x, int off_y, u_char alpha);
static void AlbumSaveAlbumSelDisp(u_char alpha);

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* Reset everything and point system/mc's save-data marshalling at the album
 * the edit view has selected.  SetMemoryCardSaveDataToBuff() reads through the
 * address SetAlbumSaveDataAddr() parks here. */
void AlbumSaveCtrlInit(void)                                            /* 224 */
{
    album_save_ctrl.step            = ALBUM_SAVE_DISP_INIT;             /* 227 */
    album_save_ctrl.mc_step         = ALBUM_SAVE_MC_CARD_CHECK_INIT;    /* 228 */
    album_save_ctrl.mode            = ALBUM_SAVE_MODE_SLOT_SEL;         /* 229 */
    album_save_ctrl.dir_check_cnt   = 0;                                /* 230 */
    album_save_ctrl.slot_csr        = 0;                                /* 231 */
    album_save_ctrl.album_sel_csr   = 0;                                /* 232 */
    album_save_ctrl.conf_csr        = 1;            /* default "no" */   /* 233 */
    album_save_ctrl.save_file_cnt   = 0;                                /* 234 */
    album_save_ctrl.format_end_cnt  = 0;                                /* 235 */

    SetAlbumSaveDataAddr(GetAlbumDataAddr(GetCurrentAlbum()));          /* 238 */

    MemoryCardExeInit();                                                /* 241 */
}

/* Both of the screen's own paks -- album.o requested them, nothing else waits
 * on them.  Carries the same ALBM_SLOT_SL_PK2 language mismatch album_load.c
 * does, and it is benign for the same reason: FileLoadIsEnd2() answers 1 when
 * it finds no queued request for that (file_no, buffer) pair. */
static int AlbumSaveTexLoadWait(void)                                   /* 251 */
{
    void *album_sl_tex_addr;
    void *album_slot_tex_addr;
    int   res;

    album_sl_tex_addr   = GetAlbumSaveLoadTexAddr();                    /* 258 */
    album_slot_tex_addr = GetAlbumSlotSelTexAddr();                     /* 259 */

    res = 0;                                                            /* 261 */

    if (FileLoadIsEnd2(ALBM_SL_PK2, album_sl_tex_addr) != 0) {          /* 265 */
        res = (FileLoadIsEnd2(ALBM_SLOT_SL_PK2, album_slot_tex_addr) != 0); /* 266 */
    }

    return res;                                                         /* 272 */
}

/* ==========================================================================
 *  The screen
 * ======================================================================== */

/* One frame.  Returns non-zero on the frame the page is finished with, which
 * is album_edit.o's cue to leave. */
int AlbumSaveMain(void)                                                 /* 284 */
{
    int res;

    res = 0;

    switch (album_save_ctrl.step) {                                     /* 291 */

    case ALBUM_SAVE_DISP_INIT:
        AlbumSaveDispInit();                                            /* 294 */

        AlbumMemInit(ALBUM_SAVE_MEM_SIZE, "album_save.c", 297);         /* 297 */

        album_save_ctrl.step = ALBUM_SAVE_LOAD_WAIT;                    /* 300 */
        break;

    case ALBUM_SAVE_LOAD_WAIT:
        if (AlbumSaveTexLoadWait() != 0) {                              /* 302 */
            if (AlbumMemMain() != 0) {                                  /* 303 */

                album_save_ctrl.step = ALBUM_SAVE_MC_EXE;               /* 307 */
            }
        }
        break;

    case ALBUM_SAVE_MC_EXE:
        AlbumSaveModeMain();                                            /* 310 */
        break;                                                          /* 311 */

    case ALBUM_SAVE_OUT:
        SetAlbumTitleFlg(1);                                            /* 315 */

        AlbumEditMenuDelete();                                          /* 317 */

        if (album_save_disp.anim_step == ZERO2_ANIM2D_STEP_END) {       /* 318 */
            if (AlbumMemMain() != 0) {                                  /* 319 */
                res = 1;
            }
        }
        break;                                                          /* 323 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 325 */
        break;
    }

    return res;                                                         /* 329 */
}

/* The two halves of the page. */
static void AlbumSaveModeMain(void)                                     /* 335 */
{
    switch (album_save_ctrl.mode) {                                     /* 338 */

    case ALBUM_SAVE_MODE_SLOT_SEL:

        SetAlbumTitleFlg(1);                                            /* 342 */

        AlbumSaveSlotSelPad();                                          /* 345 */
        break;

    case ALBUM_SAVE_MODE_MC_SAVE:
        SetAlbumTitleFlg(0);                                            /* 350 */

        AlbumSaveMcSave();                                              /* 352 */
        break;                                                          /* 353 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 355 */
        break;
    }
}

/* UP or DOWN toggles the slot, CROSS commits it and CIRCLE leaves the page.
 * The extra SetAlbumTitleFlg(0) the load screen does not have takes the title
 * plate down a frame early, before AlbumSaveModeMain() would do it. */
static void AlbumSaveSlotSelPad(void)                                   /* 368 */
{
    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0) ||                  /* 372 */
        (pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {                  /* 378 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 379 */

        album_save_ctrl.slot_csr =
            (char)((album_save_ctrl.slot_csr + 1) % ALBUM_SAVE_SLOT_MAX); /* 381 */

    } else if (*paddat[0] == 1) {                                       /* 384 */
        album_save_ctrl.mode    = ALBUM_SAVE_MODE_MC_SAVE;              /* 385 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_CARD_CHECK_INIT;        /* 386 */

        SetAlbumTitleFlg(0);                                            /* 390 */

        MemoryCardSetAccessPort(album_save_ctrl.slot_csr);              /* 393 */

        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 395 */

    } else if (*paddat[1] == 1) {                                       /* 398 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 399 */

        AlbumSaveOutReq();                                              /* 402 */
    }
}

/* Ask the page to close, and give the staging buffer back. */
static void AlbumSaveOutReq(void)                                       /* 411 */
{
    album_save_disp.anim_step  = ZERO2_ANIM2D_STEP_OUT;                 /* 414 */
    album_save_disp.anim_timer = 0;                                     /* 415 */

    album_save_ctrl.step = ALBUM_SAVE_OUT;                              /* 417 */

    AlbumMemFree("album_save.c", 420);                                  /* 420 */
}

/* ==========================================================================
 *  The card machine
 *
 *  Every Init case falls through into its Wait case, so a request is issued
 *  and polled on the same frame.  The fall-through is the ROM's own: the jump
 *  table at rodata 3a0f50 carries a separate entry for both halves.
 * ======================================================================== */

static void AlbumSaveMcSave(void)                                       /* 428 */
{
    switch (album_save_ctrl.mc_step) {                                  /* 431 */

    case ALBUM_SAVE_MC_CARD_CHECK_INIT:
        AlbumSaveMcCardCheckInit();                                     /* 433 */

    case ALBUM_SAVE_MC_CARD_CHECK_WAIT:
        AlbumSaveMcCardCheckWait();                                     /* 436 */
        break;

    case ALBUM_SAVE_MC_GET_DIR_INFO_INIT:
        AlbumSaveMcGetDirInfoInit();                                    /* 439 */

    case ALBUM_SAVE_MC_GET_DIR_INFO_WAIT:
        AlbumSaveMcGetDirInfoWait();                                    /* 442 */
        break;

    case ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT:
        AlbumSaveMcSaveAlbumSelInit();                                  /* 445 */

    case ALBUM_SAVE_MC_SAVE_ALBUM_SEL_WAIT:
        AlbumSaveMcSaveAlbumSelWait();                                  /* 448 */
        break;

    case ALBUM_SAVE_MC_CHECK_AGAIN_INIT:
        AlbumSaveMcCheckAgainInit();                                    /* 451 */

    case ALBUM_SAVE_MC_CHECK_AGAIN_WAIT:
        AlbumSaveMcCheckAgainWait();                                    /* 454 */
        break;

    case ALBUM_SAVE_MC_SAVE_CONF_INIT:
        AlbumSaveMcSaveConfInit();                                      /* 457 */

    case ALBUM_SAVE_MC_SAVE_CONF_WAIT:
        AlbumSaveMcSaveConfWait();                                      /* 460 */
        break;

    case ALBUM_SAVE_MC_SAVE_INIT:
        AlbumSaveMcSaveInit();                                          /* 463 */

    case ALBUM_SAVE_MC_SAVE_WAIT:
        AlbumSaveMcSaveWait();                                          /* 466 */
        break;

    case ALBUM_SAVE_MC_ERROR_CONF_INIT:
        AlbumSaveMcErrorConfInit();                                     /* 469 */

    case ALBUM_SAVE_MC_ERROR_CONF_WAIT:
        AlbumSaveMcErrorConfWait();                                     /* 472 */
        break;

    case ALBUM_SAVE_MC_END_CONF:
        AlbumSaveMcEndConf();                                           /* 475 */
        break;

    case ALBUM_SAVE_MC_REMAKE_CONF_INIT:
        AlbumSaveMcRemakeConfInit();                                    /* 478 */

    case ALBUM_SAVE_MC_REMAKE_CONF_WAIT:
        AlbumSaveMcRemakeConfWait();                                    /* 481 */
        break;

    case ALBUM_SAVE_MC_REMAKE_DIR_DEL_INIT:
        AlbumSaveMcRemakeDirDelInit();                                  /* 484 */

    case ALBUM_SAVE_MC_REMAKE_DIR_DEL_WAIT:
        AlbumSaveMcRemakeDirDelWait();                                  /* 487 */
        break;

    case ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT:
        AlbumSaveMcNewMakeConfInit();                                   /* 490 */

    case ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT:
        AlbumSaveMcNewMakeConfWait();                                   /* 493 */
        break;

    case ALBUM_SAVE_MC_NEW_MAKE_INIT:
        AlbumSaveMcNewMakeInit();                                       /* 496 */

    case ALBUM_SAVE_MC_NEW_MAKE_WAIT:
        AlbumSaveMcNewMakeWait();                                       /* 499 */
        break;

    case ALBUM_SAVE_MC_FORMAT_CONF_INIT:
        AlbumSaveMcFormatConfInit();                                    /* 502 */

    case ALBUM_SAVE_MC_FORMAT_CONF_WAIT:
        AlbumSaveMcFormatConfWait();                                    /* 505 */
        break;

    case ALBUM_SAVE_MC_FORMAT_INIT:
        AlbumSaveMcFormatInit();                                        /* 508 */

    case ALBUM_SAVE_MC_FORMAT_WAIT:
        AlbumSaveMcFormatWait();                                        /* 511 */
        break;

    case ALBUM_SAVE_MC_FORMAT_END_INIT:
        AlbumSaveMcFormatEndInit();                                     /* 514 */

    case ALBUM_SAVE_MC_FORMAT_END_WAIT:
        AlbumSaveMcFormatEndWait();                                     /* 517 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 520 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Step 0/1 -- is there a card in the chosen slot, and is it formatted.
 *
 *  Unlike the load screen, an unformatted card is not the end of it: the
 *  format prompt is offered instead.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcCardCheckInit(void)                              /* 529 */
{
    MemoryCardGetCardInfoInit(album_save_ctrl.slot_csr, 0);             /* 532 */

    album_save_disp.msg_id  = ALBUM_SAVE_MSG_NONE;                      /* 534 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_CARD_CHECK_WAIT;            /* 535 */
}

static void AlbumSaveMcCardCheckWait(void)                              /* 543 */
{
    int mc_res;

    mc_res = MemoryCardGetCardInfoMain();                               /* 550 */

    if (mc_res == 1) {                                                  /* 553 */

        if (GetAccessMemoryCardFormat() == 1) {                         /* 555 */
            album_save_ctrl.dir_check_cnt = 0;                          /* 556 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_GET_DIR_INFO_INIT;  /* 557 */
        }
        else {
            album_save_ctrl.conf_csr = 1;           /* default "no" */   /* 560 */
            album_save_disp.msg_id   = ALBUM_SAVE_MSG_UNFORMATTED_CONF; /* 561 */
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_FORMAT_CONF_INIT;  /* 562 */
        }

    } else {

        if (mc_res >= 0) {                                              /* 566 */
            return;
        }

        switch (mc_res) {                                               /* 568 */

        case -1:
            album_save_disp.msg_id        = ALBUM_SAVE_MSG_NONE;        /* 570 */
            album_save_ctrl.dir_check_cnt = 0;                          /* 571 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_GET_DIR_INFO_INIT;  /* 572 */
            return;                                                     /* 573 */

        /* No format is not an error on the save side: the album list is shown
         * anyway, and the format prompt comes out of the directory check. */
        case -2:
            album_save_disp.msg_id        = ALBUM_SAVE_MSG_SEL_ALBUM;   /* 578 */
            album_save_ctrl.album_sel_csr = 0;                          /* 579 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;/* 580 */
            return;                                                     /* 586 */

        case -0x14:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 590 */
            break;

        default:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_READ_FAILED;        /* 594 */
            break;
        }
    }

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;            /* 595 */
}

/* --------------------------------------------------------------------------
 *  Step 2/3 -- walk the five album directories.
 *
 *  Nothing is recorded: the walk exists only so a card that cannot be listed
 *  at all is reported before the player picks a slot on it.  -4 and -6 mean
 *  "that album has not been saved yet", which is fine here.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcGetDirInfoInit(void)                             /* 605 */
{
    char path_name[ALBUM_SAVE_PATH_NAME_LEN];

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);                     /* 609 */

    MemoryCardMakeSearchDirPath(path_name, album_save_ctrl.dir_check_cnt + 1); /* 613 */

    MemoryCardGetDirInfoInit(GetAccessMemoryCardPort(),
                             GetAccessMemoryCardSlot(), path_name);     /* 615 */

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_GET_DIR_INFO_WAIT;          /* 617 */
}

static void AlbumSaveMcGetDirInfoWait(void)                             /* 625 */
{
    int mc_res;

    mc_res = MemoryCardGetDirInfoMain();                                /* 632 */

    if (mc_res == 1) {                                                  /* 635 */
        album_save_ctrl.dir_check_cnt++;                                /* 636 */

    } else if (mc_res < 0) {                                            /* 639 */

        switch (mc_res) {                                               /* 641 */

        case -2:
            album_save_ctrl.dir_check_cnt = 0;                          /* 646 */
            album_save_disp.msg_id        = ALBUM_SAVE_MSG_SEL_ALBUM;   /* 647 */
            album_save_ctrl.album_sel_csr = 0;                          /* 648 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;/* 649 */
            break;                                                      /* 655 */

        case -4:
        case -6:
            album_save_ctrl.dir_check_cnt++;                            /* 659 */
            break;                                                      /* 660 */

        case -0x14:
            album_save_ctrl.dir_check_cnt = 0;                          /* 662 */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_NO_CARD;           /* 663 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;    /* 664 */
            break;                                                      /* 665 */

        default:
            album_save_ctrl.dir_check_cnt = 0;                          /* 669 */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_READ_FAILED;       /* 670 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;    /* 671 */
            break;
        }
    }

    if ((mc_res == 1) || (mc_res == -4) || (mc_res == -6)) {            /* 676 */

        if (album_save_ctrl.dir_check_cnt >= ALBUM_SAVE_ALBUM_MAX) {    /* 678 */
            album_save_disp.msg_id        = ALBUM_SAVE_MSG_SEL_ALBUM;   /* 679 */
            album_save_ctrl.album_sel_csr = 0;                          /* 680 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;/* 681 */

        } else {
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_NONE;              /* 685 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_GET_DIR_INFO_INIT;  /* 686 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Step 4/5 -- pick which of the five albums on the card to write.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcSaveAlbumSelInit(void)                           /* 694 */
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);         /* 698 */

    album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;                 /* 700 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_WAIT;        /* 701 */
}

static void AlbumSaveMcSaveAlbumSelWait(void)                           /* 708 */
{
    AlbumSaveMcSaveAlbumSelPad();                                       /* 712 */

    AlbumSaveAlbumSelMcEveryFrameCheck();                               /* 717 */
}

/* LEFT / RIGHT walk all five albums -- no skipping, because any of them can be
 * written -- so the cue plays unconditionally. */
static void AlbumSaveMcSaveAlbumSelPad(void)                            /* 728 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {                  /* 732 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 733 */

        album_save_ctrl.album_sel_csr = (char)((album_save_ctrl.album_sel_csr
            + (ALBUM_SAVE_ALBUM_MAX - 1)) % ALBUM_SAVE_ALBUM_MAX);      /* 734 */

    } else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {           /* 737 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 738 */

        album_save_ctrl.album_sel_csr =
            (char)((album_save_ctrl.album_sel_csr + 1) % ALBUM_SAVE_ALBUM_MAX); /* 739 */

    } else if (*paddat[0] == 1) {                                       /* 742 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 743 */

        album_save_ctrl.conf_csr = 1;               /* default "no" */   /* 745 */
        album_save_ctrl.mc_step  = ALBUM_SAVE_MC_CHECK_AGAIN_INIT;      /* 746 */
        album_save_disp.msg_id   = ALBUM_SAVE_MSG_NONE;                 /* 747 */

    } else if (*paddat[1] == 1) {                                       /* 750 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 751 */

        album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;                   /* 753 */
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;              /* 754 */
    }
}

/* The album-select page's own card watch.  It is AlbumSaveMcEveryFrameCheck()
 * with one case changed: an unformatted card (-2) is ignored rather than
 * reported, because the album list is still a valid choice on one and the
 * format prompt is reached from the directory check instead. */
static void AlbumSaveAlbumSelMcEveryFrameCheck(void)                    /* 764 */
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 772 */

    if (mc_res < 0) {                                                   /* 774 */

        switch (mc_res) {                                               /* 775 */

        case -1:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 780 */
            break;

        case -2:
            return;

        case -0x14:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 787 */
            break;

        default:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_READ_FAILED;        /* 791 */
            break;
        }

        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 792 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 6/7 -- list the chosen album's directory, and decide from the result
 *  which of the four ways in to take.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcCheckAgainInit(void)                             /* 802 */
{
    char path_name[ALBUM_SAVE_PATH_NAME_LEN];

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);                     /* 806 */

    MemoryCardMakeSearchDirPath(path_name, album_save_ctrl.album_sel_csr + 1); /* 810 */

    MemoryCardCheckInit(album_save_ctrl.slot_csr, 0, path_name);        /* 812 */

    album_save_disp.msg_id  = ALBUM_SAVE_MSG_NONE;                      /* 814 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_CHECK_AGAIN_WAIT;           /* 815 */
}

/* The fork in the whole file.  A listed, intact directory is an overwrite; a
 * listed but broken one is a remake; "no entry" (-4) means the album has never
 * been saved, so it is a new-make if there is room and "insufficient free
 * space" if there is not; "not empty" (-6) is the broken case again. */
static void AlbumSaveMcCheckAgainWait(void)                             /* 823 */
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 830 */

    if (mc_res == 1) {                                                  /* 833 */

        if (MemoryCardCheckDirBroken(album_save_ctrl.album_sel_csr + 1) != 0) { /* 835 */
            album_save_disp.msg_id   = ALBUM_SAVE_MSG_OVERWRITE_CONF;   /* 837 */
            album_save_ctrl.conf_csr = 1;           /* default "no" */   /* 838 */
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_SAVE_CONF_INIT;    /* 839 */

        } else if (MemoryCardCheckEmptyBroken(album_save_ctrl.album_sel_csr + 1) != 0) { /* 844 */
            album_save_ctrl.conf_csr = 1;                               /* 845 */
            album_save_disp.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;      /* 846 */
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_INIT;  /* 847 */

        } else {
            /* Cross-jumped with case -6's else below, which is where these two
             * line numbers come from -- this arm emits no code of its own. */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_CARD_ERROR;        /* 887 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;    /* 888 */
        }

    } else if (mc_res < 0) {                                            /* 856 */

        switch (mc_res) {                                               /* 858 */

        case -2:
            album_save_ctrl.conf_csr = 1;                               /* 861 */
            album_save_disp.msg_id   = ALBUM_SAVE_MSG_UNFORMATTED_CONF; /* 862 */
            album_save_ctrl.mc_step  = ALBUM_SAVE_MC_FORMAT_CONF_INIT;  /* 863 */
            return;                                                     /* 864 */

        case -4:
            if (MemoryCardCheckEmpty(album_save_ctrl.album_sel_csr + 1) != 0) { /* 867 */
                album_save_ctrl.conf_csr = 1;                           /* 869 */
                album_save_disp.msg_id   = ALBUM_SAVE_MSG_NEW_MAKE_CONF;/* 870 */
                album_save_ctrl.mc_step  = ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT; /* 871 */

            } else {
                album_save_disp.msg_id  = ALBUM_SAVE_MSG_NO_SPACE;      /* 877 */
                album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;/* 878 */
            }
            return;

        case -6:
            if (MemoryCardCheckEmptyBroken(album_save_ctrl.album_sel_csr + 1) != 0) { /* 881 */
                album_save_ctrl.conf_csr = 1;                           /* 882 */
                album_save_disp.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;  /* 883 */
                album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_INIT; /* 884 */

            } else {
                album_save_disp.msg_id  = ALBUM_SAVE_MSG_CARD_ERROR;    /* 887 */
                album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;/* 888 */
            }
            return;                                                     /* 890 */

        case -1:
        case -0x14:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 895 */
            break;

        default:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_READ_FAILED;        /* 899 */
            break;
        }

        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 900 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 8/9 -- "OK to overwrite?"
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcSaveConfInit(void)                               /* 910 */
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);         /* 914 */

    album_save_ctrl.conf_csr = 1;                   /* default "no" */   /* 916 */
    album_save_disp.msg_id   = ALBUM_SAVE_MSG_OVERWRITE_CONF;           /* 917 */
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_SAVE_CONF_WAIT;            /* 918 */
}

static void AlbumSaveMcSaveConfWait(void)                               /* 926 */
{
    AlbumSaveMcSaveConfPad();                                           /* 930 */

    AlbumSaveMcEveryFrameCheck();                                       /* 933 */
}

static void AlbumSaveMcSaveConfPad(void)                                /* 941 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 945 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 950 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 951 */

        album_save_ctrl.conf_csr ^= 1;                                  /* 952 */

    } else if (*paddat[0] == 1) {                                       /* 955 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 956 */

        if (album_save_ctrl.conf_csr == 0) {                            /* 958 */
            album_save_disp.msg_id        = ALBUM_SAVE_MSG_SAVING;      /* 959 */
            album_save_ctrl.save_file_cnt = 0;                          /* 960 */
            album_save_ctrl.mc_step       = ALBUM_SAVE_MC_SAVE_INIT;    /* 961 */

        } else {
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;         /* 965 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;/* 966 */
        }

    } else if (*paddat[1] == 1) {                                       /* 970 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 971 */

        album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;             /* 972 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;    /* 973 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 10/11 -- the transfer.  One file, but written through a list.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcSaveInit(void)                                   /* 982 */
{
    int  save_file_label[ALBUM_SAVE_FILE_NUM] = { 0 };
    int  size;
    char path_name[ALBUM_SAVE_PATH_NAME_LEN];

    memset(path_name, 0, ALBUM_SAVE_PATH_NAME_LEN);                     /* 992 */

    /* The album takes the identity of the card position it is written to, so
     * the edit view draws the right spine afterwards.  Attributed entirely to
     * fixed_array.h 124/125 -- the subscript swallows the statement's own line
     * note -- so 995 is interpolated into the 993..997 gap. */
    album_info[GetCurrentAlbum()].album_type = album_save_ctrl.album_sel_csr; /* 995 */

    MemoryCardSetFilePath(path_name, album_save_ctrl.album_sel_csr + 1,
                          save_file_label[album_save_ctrl.save_file_cnt]); /* 998 */

    size = GetMemoryCardDataSize(album_save_ctrl.album_sel_csr + 1,
                                 save_file_label[album_save_ctrl.save_file_cnt]); /* 1000 */

    /* The staging buffer is a fixed 1 MB off the packet ring.  The assert does
     * not stop the save. */
    if (size > ALBUM_SAVE_MEM_SIZE) {                                   /* 1002 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1003 */
    }

    album_save_buff_addr = GetAlbumMemAddr();                           /* 1007 */

    SetMemoryCardSaveDataToBuff((char *)album_save_buff_addr,
                                album_save_ctrl.album_sel_csr + 1,
                                save_file_label[album_save_ctrl.save_file_cnt]); /* 1009 */

    MemoryCardFileSaveInit(album_save_ctrl.slot_csr, 0, path_name,
                           album_save_buff_addr, size);                 /* 1011 */

    album_save_disp.msg_id = ALBUM_SAVE_MSG_SAVING;                     /* 1013 */
    album_save_ctrl.save_file_cnt++;                                    /* 1014 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_WAIT;                  /* 1015 */
}

/* One file written.  The loop back to SAVE_INIT is what would carry a
 * multi-file save; with ALBUM_SAVE_FILE_NUM == 1 it never runs. */
static void AlbumSaveMcSaveWait(void)                                   /* 1022 */
{
    int mc_res;

    mc_res = MemoryCardFileSaveMain();                                  /* 1029 */

    if (mc_res == 1) {                                                  /* 1032 */

        if (album_save_ctrl.save_file_cnt >= ALBUM_SAVE_FILE_NUM) {     /* 1034 */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_SAVE_OK;           /* 1035 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_END_CONF;           /* 1036 */

        } else {
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_SAVING;            /* 1039 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_INIT;          /* 1040 */
        }

    } else if (mc_res < 0) {                                            /* 1044 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_SAVE_FAILED;           /* 1045 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 1046 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 12/13 -- the error screen, and step 14 -- the success one.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcErrorConfInit(void)                              /* 1055 */
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);         /* 1059 */

    album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_WAIT;            /* 1061 */
}

/* A swap (-1) restarts the machine, and so does an unformatted card (-2) --
 * the load screen's copy only takes the swap, because there the format prompt
 * has nowhere to go. */
static void AlbumSaveMcErrorConfWait(void)                              /* 1068 */
{
    int mc_res;

    AlbumSaveMcErrorConfPad();                                          /* 1076 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1079 */

    if ((mc_res == -1) ||                                               /* 1082 */
        (mc_res == -2)) {                                               /* 1087 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_NONE;                  /* 1088 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_CARD_CHECK_INIT;        /* 1089 */

    } else if (mc_res == 1) {                                           /* 1092 */
        MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);     /* 1093 */

    } else if (mc_res < 0) {                                            /* 1096 */
        MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);     /* 1097 */
    }
}

/* Both buttons do the same thing, and the ROM still spells the arm out twice
 * -- GCC cross-jumped the pair into one tail. */
static void AlbumSaveMcErrorConfPad(void)                               /* 1106 */
{
    if (*paddat[0] == 1) {                                              /* 1110 */

        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1113 */

        album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;                   /* 1114 */
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;              /* 1115 */

    } else if (*paddat[1] == 1) {                                       /* 1116 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1117 */

        album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;                   /* 1118 */
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;              /* 1119 */
    }
}

/* "Save completed." -- either button acknowledges it.  Unlike the load
 * screen's twin there is nothing to decompress afterwards, so this is
 * AlbumSaveMcErrorConfPad() a second time. */
static void AlbumSaveMcEndConf(void)                                    /* 1128 */
{
    if (*paddat[0] == 1) {                                              /* 1132 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1135 */

        album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;                   /* 1136 */
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;              /* 1137 */

    } else if (*paddat[1] == 1) {                                       /* 1138 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1139 */

        album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;                   /* 1140 */
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;              /* 1141 */
    }
}

/* --------------------------------------------------------------------------
 *  Steps 15..18 -- the remake: the directory is there but unusable, so delete
 *  it and fall into the new-make.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcRemakeConfInit(void)                             /* 1150 */
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);         /* 1154 */

    album_save_ctrl.conf_csr = 1;                   /* default "no" */   /* 1156 */
    album_save_disp.msg_id   = ALBUM_SAVE_MSG_REMAKE_CONF;              /* 1157 */
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_REMAKE_CONF_WAIT;          /* 1158 */
}

static void AlbumSaveMcRemakeConfWait(void)                             /* 1165 */
{
    AlbumSaveMcRemakeConfPad();                                         /* 1169 */

    AlbumSaveMcEveryFrameCheck();                                       /* 1172 */
}

static void AlbumSaveMcRemakeConfPad(void)                              /* 1179 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1183 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 1188 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1189 */

        album_save_ctrl.conf_csr ^= 1;                                  /* 1190 */

    } else if (*paddat[0] == 1) {                                       /* 1193 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1194 */

        if (album_save_ctrl.conf_csr == 0) {                            /* 1197 */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;          /* 1198 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_REMAKE_DIR_DEL_INIT;/* 1199 */

        } else {
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;         /* 1203 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;/* 1204 */
        }

    } else if (*paddat[1] == 1) {                                       /* 1208 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1209 */

        album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;             /* 1210 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;    /* 1211 */
    }
}

static void AlbumSaveMcRemakeDirDelInit(void)                           /* 1220 */
{
    MemoryCardDirDelInit(album_save_ctrl.slot_csr, 0,
                         album_save_ctrl.album_sel_csr + 1);            /* 1223 */

    album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;                  /* 1225 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_REMAKE_DIR_DEL_WAIT;        /* 1226 */
}

/* Both arms write the same message: on failure the screen still says "Creating
 * album data." while it diverts to the error confirm, which then overwrites
 * it.  Reproduced as found. */
static void AlbumSaveMcRemakeDirDelWait(void)                           /* 1233 */
{
    int mc_res;

    mc_res = MemoryCardDirDelMain();                                    /* 1240 */

    if (mc_res == 1) {                                                  /* 1243 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;              /* 1244 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_INIT;          /* 1245 */

    } else if (mc_res < 0) {                                            /* 1248 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;              /* 1249 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 1250 */
    }
}

/* --------------------------------------------------------------------------
 *  Steps 19..22 -- the new-make: create the album's directory and its file,
 *  then fall into the ordinary save.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcNewMakeConfInit(void)                            /* 1259 */
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);         /* 1263 */

    album_save_ctrl.conf_csr = 1;                   /* default "no" */   /* 1265 */
    album_save_disp.msg_id   = ALBUM_SAVE_MSG_NEW_MAKE_CONF;            /* 1266 */
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT;        /* 1267 */
}

static void AlbumSaveMcNewMakeConfWait(void)                            /* 1274 */
{
    AlbumSaveMcNewMakeConfPad();                                        /* 1278 */

    AlbumSaveMcEveryFrameCheck();                                       /* 1281 */
}

static void AlbumSaveMcNewMakeConfPad(void)                             /* 1288 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1292 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 1297 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1298 */

        album_save_ctrl.conf_csr ^= 1;                                  /* 1299 */

    } else if (*paddat[0] == 1) {                                       /* 1302 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1303 */

        if (album_save_ctrl.conf_csr == 0) {                            /* 1306 */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;          /* 1307 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_INIT;      /* 1308 */

        } else {
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;         /* 1312 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;/* 1313 */
        }

    } else if (*paddat[1] == 1) {                                       /* 1317 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1318 */

        album_save_disp.msg_id  = ALBUM_SAVE_MSG_SEL_ALBUM;             /* 1319 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT;    /* 1320 */
    }
}

/* The staging buffer doubles as MemoryCardNewMakeInit()'s working area -- it
 * is idle at this point, and it is the only megabyte-scale block the screen
 * has. */
static void AlbumSaveMcNewMakeInit(void)                                /* 1329 */
{
    MemoryCardNewMakeInit(album_save_ctrl.slot_csr, 0,
                          album_save_ctrl.album_sel_csr + 1,
                          GetAlbumMemAddr(), ALBUM_SAVE_MEM_SIZE);      /* 1333 */

    album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;                  /* 1335 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_WAIT;              /* 1336 */
}

static void AlbumSaveMcNewMakeWait(void)                                /* 1343 */
{
    int mc_res;

    mc_res = MemoryCardNewMakeMain();                                   /* 1350 */

    if (mc_res == 1) {                                                  /* 1353 */
        album_save_ctrl.save_file_cnt = 0;                              /* 1354 */
        album_save_disp.msg_id        = ALBUM_SAVE_MSG_SAVING;          /* 1355 */
        album_save_ctrl.mc_step       = ALBUM_SAVE_MC_SAVE_INIT;        /* 1356 */

    } else if (mc_res < 0) {                                            /* 1359 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_MAKE_FAILED;           /* 1360 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 1361 */
    }
}

/* --------------------------------------------------------------------------
 *  Steps 23..28 -- the format, and the two-second hold on its banner.
 * ------------------------------------------------------------------------ */
static void AlbumSaveMcFormatConfInit(void)                             /* 1370 */
{
    MemoryCardCheckEveryFrameInit(album_save_ctrl.slot_csr, 0);         /* 1374 */

    album_save_ctrl.conf_csr = 1;                   /* default "no" */   /* 1376 */
    album_save_disp.msg_id   = ALBUM_SAVE_MSG_UNFORMATTED_CONF;         /* 1377 */
    album_save_ctrl.mc_step  = ALBUM_SAVE_MC_FORMAT_CONF_WAIT;          /* 1378 */
}

/* A third copy of the card triage, and it swallows -2 for the same reason the
 * album-select one does: the card being unformatted is exactly what this
 * prompt is about, so reporting it would bounce the player into the error
 * screen before they could answer. */
static void AlbumSaveMcFormatConfWait(void)                             /* 1385 */
{
    int mc_res;

    AlbumSaveMcFormatConfPad();                                         /* 1393 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1396 */

    if (mc_res < 0) {                                                   /* 1398 */

        switch (mc_res) {                                               /* 1399 */

        case -1:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 1404 */
            break;

        case -2:
            return;

        case -0x14:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 1410 */
            break;

        default:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_READ_FAILED;        /* 1414 */
            break;
        }

        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 1415 */
    }
}

/* "No" here leaves the card machine altogether rather than going back to the
 * album list -- there is no usable list on an unformatted card. */
static void AlbumSaveMcFormatConfPad(void)                              /* 1425 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1429 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 1434 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1435 */

        album_save_ctrl.conf_csr ^= 1;                                  /* 1436 */

    } else if (*paddat[0] == 1) {                                       /* 1439 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1440 */

        if (album_save_ctrl.conf_csr == 0) {                            /* 1443 */
            album_save_disp.msg_id  = ALBUM_SAVE_MSG_FORMATTING;        /* 1444 */
            album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_INIT;        /* 1445 */

        } else {
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;               /* 1449 */
            album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;          /* 1450 */
        }

    } else if (*paddat[1] == 1) {                                       /* 1454 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1455 */

        album_save_disp.msg_id = ALBUM_SAVE_MSG_NONE;                   /* 1456 */
        album_save_ctrl.mode   = ALBUM_SAVE_MODE_SLOT_SEL;              /* 1457 */
    }
}

static void AlbumSaveMcFormatInit(void)                                 /* 1466 */
{
    MemoryCardFormatInit(album_save_ctrl.slot_csr, 0);                  /* 1469 */

    album_save_disp.msg_id  = ALBUM_SAVE_MSG_FORMATTING;                /* 1471 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_WAIT;                /* 1472 */
}

static void AlbumSaveMcFormatWait(void)                                 /* 1479 */
{
    int mc_res;

    mc_res = MemoryCardFormatMain();                                    /* 1486 */

    if (mc_res == 1) {                                                  /* 1489 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_FORMAT_OK;             /* 1492 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_END_INIT;        /* 1493 */

    } else if (mc_res < 0) {                                            /* 1500 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_FORMAT_FAILED;         /* 1501 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 1502 */
    }
}

static void AlbumSaveMcFormatEndInit(void)                              /* 1512 */
{
    album_save_ctrl.format_end_cnt = 0;                                 /* 1515 */
    album_save_disp.msg_id  = ALBUM_SAVE_MSG_FORMAT_OK;                 /* 1517 */
    album_save_ctrl.mc_step = ALBUM_SAVE_MC_FORMAT_END_WAIT;            /* 1518 */
}

/* Hold "Format Successful." for two seconds, then go straight to the new-make
 * -- a freshly formatted card certainly has no album directory. */
static void AlbumSaveMcFormatEndWait(void)                              /* 1527 */
{
    album_save_ctrl.format_end_cnt++;                                   /* 1531 */

    if (album_save_ctrl.format_end_cnt >= ALBUM_SAVE_FORMAT_END_TIME) { /* 1534 */
        album_save_disp.msg_id  = ALBUM_SAVE_MSG_CREATING;              /* 1536 */
        album_save_ctrl.mc_step = ALBUM_SAVE_MC_NEW_MAKE_INIT;          /* 1537 */
    }
}

/* Shared tail of the interactive steps: notice the card being pulled and
 * divert to the error confirm. */
static void AlbumSaveMcEveryFrameCheck(void)                            /* 1546 */
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1554 */

    if (mc_res < 0) {                                                   /* 1556 */

        switch (mc_res) {                                               /* 1557 */

        case -1:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 1562 */
            break;

        case -2:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_UNFORMATTED;        /* 1567 */
            break;

        case -0x14:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_NO_CARD;            /* 1571 */
            break;

        default:
            album_save_disp.msg_id = ALBUM_SAVE_MSG_READ_FAILED;        /* 1575 */
            break;
        }

        album_save_ctrl.mc_step = ALBUM_SAVE_MC_ERROR_CONF_INIT;        /* 1576 */
    }
}

/* Give the card lock back. */
void AlbumSaveEnd(void)                                                 /* 1590 */
{
    MemoryCardEnd();                                                    /* 1594 */
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

static void AlbumSaveDispInit(void)                                     /* 1606 */
{
    album_save_disp.anim_step      = ZERO2_ANIM2D_STEP_START;           /* 1609 */
    album_save_disp.anim_timer     = 0;                                 /* 1610 */
    album_save_disp.msg_id         = ALBUM_SAVE_MSG_NONE;               /* 1611 */
    album_save_disp.csr_anim_timer = 0;                                 /* 1612 */
}

void AlbumSaveDispMain(void)                                            /* 1620 */
{
    u_char alpha;

    alpha = 0x80;                                                       /* 1624 */

    if ((album_save_ctrl.step == ALBUM_SAVE_MC_EXE) ||                  /* 1629 */
        (album_save_ctrl.step == ALBUM_SAVE_OUT)) {

        AlbumInOutAnimCtrl(&album_save_disp.anim_step,
                           &album_save_disp.anim_timer, &alpha);        /* 1630 */

        if (album_save_disp.anim_step != ZERO2_ANIM2D_STEP_END) {       /* 1632 */

            AlbumBlackBgDisp(0, 0, alpha, 0x80);                        /* 1634 */

            switch (album_save_ctrl.mode) {                             /* 1636 */

            case ALBUM_SAVE_MODE_SLOT_SEL:
                AlbumSaveSlotSelDisp(0, 0, alpha);                      /* 1639 */
                break;                                                  /* 1640 */

            case ALBUM_SAVE_MODE_MC_SAVE:
                AlbumSaveMcSaveDisp(0, 0, alpha);                       /* 1643 */
                break;                                                  /* 1644 */

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 1646 */
                break;
            }
        }
    }
}

/* The slot-select window.  Identical to album_load.c's but for the caption
 * message: "Which MEMORY CARD slot will you save to?" rather than load. */
static void AlbumSaveSlotSelDisp(int off_x, int off_y, u_char alpha)    /* 1661 */
{
    void *tm2_addr;

    tm2_addr = GetAlbumSlotSelTexAddr();                                /* 1666 */

    PK2SendVram((uintptr_t)tm2_addr, -1, -1, 0);                        /* 1669 */

    AlbumSlotSelWinDisp(album_save_ctrl.slot_csr, 0, 0, alpha);         /* 1672 */

    DrawCmnWindow(0, 24.0f, 330.0f, 592.0f, 112.0f, alpha, 0x66);       /* 1676 */

    PrintMsg(0x50, ALBUM_SAVE_MSG_WHICH_SLOT, 0x44, 0x15d, 1, alpha, 0); /* 1681 */

    AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                       /* 1684 */
}

/* The card machine's own page, one arm per group of steps.  Note the two
 * differences from album_load.c's version: the yes/no window is drawn *before*
 * the message rather than after it, and only three of the seven arms end with
 * AlbumSlotSelCaptionDisp().
 *
 * Two gaps in the line numbers are real and hold no code -- 1844..1855 in the
 * confirm arm and 1900..1915 in the format arm, both between a draw call and
 * the arm's `break`. */
static void AlbumSaveMcSaveDisp(int off_x, int off_y, u_char alpha)     /* 1695 */
{
    void  *album_slot_tex_addr;
    void  *album_sl_addr;
    u_char csr_rgb;

    album_slot_tex_addr = GetAlbumSlotSelTexAddr();                     /* 1702 */
    album_sl_addr       = GetAlbumSaveLoadTexAddr();                    /* 1703 */

    csr_rgb = 0x80;                                                     /* 1705 */

    switch (album_save_ctrl.mc_step) {                                  /* 1713 */

    case ALBUM_SAVE_MC_CARD_CHECK_INIT:
    case ALBUM_SAVE_MC_CARD_CHECK_WAIT:
    case ALBUM_SAVE_MC_GET_DIR_INFO_INIT:
    case ALBUM_SAVE_MC_GET_DIR_INFO_WAIT:
        PK2SendVram((uintptr_t)album_slot_tex_addr, -1, -1, 0);         /* 1718 */

        AlbumSlotSelWinDisp(album_save_ctrl.slot_csr, off_x, off_y, alpha); /* 1721 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1724 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1727 */
        break;                                                          /* 1728 */

    case ALBUM_SAVE_MC_SAVE_ALBUM_SEL_INIT:
    case ALBUM_SAVE_MC_SAVE_ALBUM_SEL_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);               /* 1731 */

        AlbumSaveSelAlbumCsrFlareDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1734 */

        AlbumSaveAlbumSelDisp(alpha);                                   /* 1737 */

        Zero2Anim2D_CsrAnimCtrl(&album_save_disp.csr_anim_timer, &csr_rgb); /* 1740 */

        AlbumSaveSelAlbumCsrDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1743 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1746 */

        DrawCmnWindow(0, 24.0f, 330.0f, 592.0f, 112.0f, alpha, 0x66);   /* 1750 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x44, 0x15d, 1, alpha, 0); /* 1753 */
        break;                                                          /* 1754 */

    case ALBUM_SAVE_MC_CHECK_AGAIN_INIT:
    case ALBUM_SAVE_MC_CHECK_AGAIN_WAIT:
    case ALBUM_SAVE_MC_SAVE_INIT:
    case ALBUM_SAVE_MC_SAVE_WAIT:
    case ALBUM_SAVE_MC_REMAKE_DIR_DEL_INIT:
    case ALBUM_SAVE_MC_REMAKE_DIR_DEL_WAIT:
    case ALBUM_SAVE_MC_NEW_MAKE_INIT:
    case ALBUM_SAVE_MC_NEW_MAKE_WAIT:
    case ALBUM_SAVE_MC_FORMAT_INIT:
    case ALBUM_SAVE_MC_FORMAT_WAIT:
    case ALBUM_SAVE_MC_FORMAT_END_INIT:
    case ALBUM_SAVE_MC_FORMAT_END_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);               /* 1769 */

        Zero2Anim2D_CsrAnimCtrl(&album_save_disp.csr_anim_timer, &csr_rgb); /* 1772 */

        AlbumSaveSelAlbumCsrFlareDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1775 */

        AlbumSaveAlbumSelDisp(alpha);                                   /* 1778 */

        AlbumSaveSelAlbumCsrDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1781 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1784 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1788 */
        break;

    case ALBUM_SAVE_MC_END_CONF:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);               /* 1790 */

        Zero2Anim2D_CsrAnimCtrl(&album_save_disp.csr_anim_timer, &csr_rgb); /* 1793 */

        AlbumSaveSelAlbumCsrFlareDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1796 */

        AlbumSaveAlbumSelDisp(alpha);                                   /* 1799 */

        AlbumSaveSelAlbumCsrDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1802 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1805 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1808 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1810 */
        break;                                                          /* 1813 */

    case ALBUM_SAVE_MC_SAVE_CONF_INIT:
    case ALBUM_SAVE_MC_SAVE_CONF_WAIT:
    case ALBUM_SAVE_MC_REMAKE_CONF_INIT:
    case ALBUM_SAVE_MC_REMAKE_CONF_WAIT:
    case ALBUM_SAVE_MC_NEW_MAKE_CONF_INIT:
    case ALBUM_SAVE_MC_NEW_MAKE_CONF_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);               /* 1820 */

        Zero2Anim2D_CsrAnimCtrl(&album_save_disp.csr_anim_timer, &csr_rgb); /* 1823 */

        AlbumSaveSelAlbumCsrFlareDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1826 */

        AlbumSaveAlbumSelDisp(alpha);                                   /* 1829 */

        AlbumSaveSelAlbumCsrDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1832 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1836 */

        DrawCmnYesNoSel(album_save_ctrl.conf_csr, (float)(off_y + 0x127), alpha, 0); /* 1838 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1841 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1843 */
        break;

    case ALBUM_SAVE_MC_ERROR_CONF_INIT:
    case ALBUM_SAVE_MC_ERROR_CONF_WAIT:
        PK2SendVram((uintptr_t)album_slot_tex_addr, -1, -1, 0);         /* 1856 */

        AlbumSlotSelWinDisp(album_save_ctrl.slot_csr, off_x, off_y, alpha); /* 1859 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1862 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1865 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1868 */
        break;                                                          /* 1872 */

    case ALBUM_SAVE_MC_FORMAT_CONF_INIT:
    case ALBUM_SAVE_MC_FORMAT_CONF_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);               /* 1877 */

        Zero2Anim2D_CsrAnimCtrl(&album_save_disp.csr_anim_timer, &csr_rgb); /* 1880 */

        AlbumSaveSelAlbumCsrFlareDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1883 */

        AlbumSaveAlbumSelDisp(alpha);                                   /* 1886 */

        AlbumSaveSelAlbumCsrDisp(album_save_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1889 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1892 */

        DrawCmnYesNoSel(album_save_ctrl.conf_csr, (float)(off_y + 0x127), alpha, 0); /* 1894 */

        PrintMsg(0x50, album_save_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1897 */
        break;                                                          /* 1916 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1918 */
        break;
    }
}

/* The five album spines, their names and the cursor plate behind whichever is
 * selected, then the slot indicator.  No mask pass -- every album position is
 * a valid save target, so there is nothing to grey out. */
static void AlbumSaveAlbumSelDisp(u_char alpha)                         /* 1935 */
{
    int i;
    int msg_col;

    for (i = 0; i < ALBUM_SAVE_ALBUM_MAX; i++) {                        /* 1944 */

        if (i != album_save_ctrl.album_sel_csr) {                       /* 1945 */

            AlbumSaveNonSelAlbumCsrDisp(i, 0, 0, alpha, 0x80);          /* 1949 */

            msg_col = 0x17;                                             /* 1950 */

        } else {
            msg_col = 0x18;                                             /* 1952 */
        }

        AlbumSaveSelAlbumNameDisp(i, 0, 0, alpha, msg_col);             /* 1956 */

        AlbumSaveSelAlbumDisp(i, 0, 0, alpha);                          /* 1959 */
    }                                                                   /* 1960 */

    AlbumSaveSelSlotDisp(album_save_ctrl.slot_csr, 0, 0, alpha, 0x80);  /* 1963 */
}
