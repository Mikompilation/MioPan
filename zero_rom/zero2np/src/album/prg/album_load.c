// FILE: /home/zero_rom/zero2np/src/album/prg/album_load.c
//
// The album's memory-card load screen.  All four ZERO2.MAP exports plus the
// 28 file-local helpers are reconstructed, which is every function
// functions.txt lists for the object bar the fixed_array<> boilerplate.
//
// The page is reached from the edit view's "Load" row and runs in two modes:
//
//   * mode 0 -- the slot-select window.  UP / DOWN toggle between the two
//     MEMORY CARD slots, CROSS commits and CIRCLE leaves.
//   * mode 1 -- the card machine.  Query the card, walk its five album
//     directories, let the player pick one of the ones that exist, confirm,
//     read it back into the current album.  CIRCLE anywhere in here returns to
//     mode 0 rather than leaving the page.
//
// The machine is two levels, the same shape save_load/prg/game_data_save.c and
// outgame/loadgame.c use.  `step` is the screen: reset the display state, wait
// for the two paks and the staging buffer, run the mode, animate out.
// `mc_step` is the card, and every Init state falls straight through into its
// Wait state in AlbumLoadMcLoad()'s switch -- the jump table at rodata 3a0b90
// has an entry for both halves and case N's code runs into case N+1's -- so a
// request issued this frame is polled on the same frame.
//
// Things worth knowing before touching it:
//
//  * album_flg[5] is the *listing*, not a selection.  Steps 2/3 run
//    MemoryCardGetDirInfo{Init,Main} once per album directory, raising
//    album_flg[n] when directory n+1 answers and clearing it on "no entry"
//    (-4) or "not empty"/"no such file" (-6).  Only when all five have been
//    probed does the screen move on -- and if none of them exists it reports
//    "No Project Zero II album data present" and diverts to the error confirm.
//    That is what dir_check_cnt counts.
//
//  * Two index spaces, as everywhere in system/mc.  `slot_csr` is the console
//    MEMORY CARD *port* (0 or 1; the card slot is always 0), and
//    `album_sel_csr` is the album, which becomes `dir_label = csr + 1`
//    because dir_label 0 is the game-data directory.
//
//  * The album-select cursor *skips* directories that do not exist.  Both
//    arms of AlbumLoadMcLoadAlbumSelPad() walk in a bounded loop until
//    album_flg says the entry is real, and the move cue only plays if the
//    cursor actually ended up somewhere else -- which is also what happens
//    when exactly one album exists, so a lone album gives the error cue.
//    Same idiom outgame/setup_menu.c uses for its locked costume rows.
//
//  * The staging buffer is album_mem.o's, borrowed off the top of the 3D DMA
//    packet ring.  AlbumLoadMain() asks for 0x100000 on its first frame and
//    gates step 1 on AlbumMemMain(); AlbumLoadOutReq() gives it back, and
//    step 3 waits on AlbumMemMain() a second time before reporting done.
//
//  * Nothing here writes the album directly.  MemoryCardFileLoadInit() reads
//    the card file into the staging buffer and DevelopMemoryCardLoadData()
//    unpacks it through system/mc/dat/save_data.c's save_album_data[]
//    manifest -- whose destination AlbumLoadCtrlInit() set by handing
//    GetAlbumDataAddr(GetCurrentAlbum()) to SetAlbumSaveDataAddr().
//
// The object has no static data at all: .rodata (3a0ab0, 0x31c) is the
// fixed_array<> assert literal, the six __FUNCTION__ strings, the file name,
// the two banner formats and five jump tables, and .sdata (3ef3d8, 0x44) is
// the fixed_array<> type names plus album_load_buff_addr.  The .ctors entry
// (2c3b3c) points at an eight-byte, empty
// __static_initialization_and_destruction_0 -- there is no file-scope object
// with a constructor here.  globals.txt is right to list only the three below.
//
// Verified 4/4 against ZERO2.MAP's exports and 32/32 against functions.txt.
// .text is accounted for byte-for-byte -- 6828 bytes of code in 38 bodies plus
// twenty-one 4-byte alignment fills is exactly the section's 0x1b00, so there
// is no unlisted body.  Both structs are confirmed member-by-member and by
// size with an offsetof harness, and the two non-trivial loops -- the
// directory listing in AlbumLoadMcGetDirInfoWait() and the album-select cursor
// walk in AlbumLoadMcLoadAlbumSelPad() -- are driven against a transcription
// of their disassembly over their whole input domains (2560 cases, zero
// mismatches).
//
// NOT YET REACHABLE.  album_edit.o is still a stub, and its two mode tables
// (data 2d7de8 / 2d7e40) are the only things that call any of the four exports
// -- so nothing in the port gets here yet.  album_edit.o is the natural next
// target for the folder; album_save.o, this file's write-side twin, is the
// other one still missing.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), album_load.o
// (.text 0x124410..0x125f10 = 0x1b00).
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from symbols.txt's $LM/SOL records.  A handful are interpolated into a
// measured gap rather than read out of one: statements whose only memory
// access goes through fixed_array<>'s inlined operator[] are attributed to
// fixed_array.h 124/125 and leave no $LM of their own, and a store GCC folded
// into a branch delay slot loses its note the same way.  They say so at the
// site.

#include "album_load.h"

#include "album.h"                                  // GetCurrentAlbum / tex accessors
#include "album_disp.h"                             // AlbumInOutAnimCtrl / Album*Disp
#include "album_edit.h"                             // AlbumEditMenuDelete / UncompressPhotoReq
#include "album_mem.h"                              // AlbumMemInit / Main / Free
#include "../../common/utility2.h"                  // PRINT_ASSERT
#include "../../common/variable.h"                  // pad[] / paddat[]
#include "../../graphics/graph2d/draw_cmn.h"        // DrawCmnWindow / DrawCmnYesNoSel
#include "../../graphics/graph2d/message.h"         // PrintMsg
#include "../../graphics/graph2d/tim2.h"            // PK2SendVram
#include "../../graphics/graph3d/ctl/fixed_array.h" // fixed_array<char,5>
#include "../../ingame/menu/zero2_anim2d.h"         // Zero2Anim2D_CsrAnimCtrl / STEP_*
#include "../../system/eeiop/cddat.h"               // ALBM_SL_PK2 / ALBM_SLOT_SL_PK2
#include "../../system/eeiop/fileload.h"            // FileLoadIsEnd2
#include "../../system/mc/prg/mc.h"                 // MemoryCardExeInit / MemoryCardEnd
#include "../../system/mc/prg/mc_check.h"           // MemoryCardCheckInit / Main
#include "../../system/mc/prg/mc_check_broken.h"    // MemoryCardCheck*Broken / NewFileLoad
#include "../../system/mc/prg/mc_check_card.h"      // MemoryCardGetCardInfo* / EveryFrame*
#include "../../system/mc/prg/mc_check_dir.h"       // MemoryCardGetDirInfo*
#include "../../system/mc/prg/mc_load.h"            // MemoryCardFileLoad*
#include "../../system/mc/prg/mc_set_data.h"        // path / size / data-area
#include "../../system/os/system.h"                 // SystemBankPlay
#include "../../system/pad/pad.h"                   // GetPadAnalogRpt

#include <string.h>                                 // memset

/* The two MEMORY CARD slots the console has.  slot_csr walks them with a real
 * `% 2`, not a xor -- the ROM emits the signed modulo bias sequence. */
#define ALBUM_LOAD_SLOT_MAX     2

/* Album directories a card can hold: dir_label 1..5, one per album.  0 is the
 * game-data directory, which this screen never touches. */
#define ALBUM_LOAD_ALBUM_MAX    5

/* Staging buffer AlbumMemInit() claims off the packet ring -- a quarter of it.
 * AlbumLoadMcLoadInit() asserts if the card file is bigger than this. */
#define ALBUM_LOAD_MEM_SIZE     0x100000

/* Path buffers are memset to 0x37 bytes by every caller; that is the ROM's
 * literal size, not a sizeof(). */
#define ALBUM_LOAD_PATH_NAME_LEN 55

/* album_load_ctrl.step -- the screen itself. */
enum ALBUM_LOAD_STEP
{
    ALBUM_LOAD_DISP_INIT = 0,   /* reset the display state, claim the buffer  */
    ALBUM_LOAD_LOAD_WAIT = 1,   /* waiting on the two paks and the buffer     */
    ALBUM_LOAD_MC_EXE    = 2,   /* one of the two modes has the screen        */
    ALBUM_LOAD_OUT       = 3    /* animating out; Main() then returns 1       */
};

/* album_load_ctrl.mode.  The two halves of the page. */
#define ALBUM_LOAD_MODE_SLOT_SEL    0   /* which MEMORY CARD slot            */
#define ALBUM_LOAD_MODE_MC_LOAD     1   /* the card machine below            */

/* album_load_ctrl.mc_step.  The ROM's debug info carries no enum for these --
 * the names are taken from the step handlers' own ROM symbol names, so the
 * values still read against the jump table at rodata 3a0b90. */
enum ALBUM_LOAD_MC_STEP
{
    ALBUM_LOAD_MC_CARD_CHECK_INIT      = 0,
    ALBUM_LOAD_MC_CARD_CHECK_WAIT      = 1,
    ALBUM_LOAD_MC_GET_DIR_INFO_INIT    = 2,
    ALBUM_LOAD_MC_GET_DIR_INFO_WAIT    = 3,
    ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT  = 4,
    ALBUM_LOAD_MC_LOAD_ALBUM_SEL_WAIT  = 5,
    ALBUM_LOAD_MC_CHECK_AGAIN_INIT     = 6,
    ALBUM_LOAD_MC_CHECK_AGAIN_WAIT     = 7,
    ALBUM_LOAD_MC_LOAD_CONF_INIT       = 8,
    ALBUM_LOAD_MC_LOAD_CONF_WAIT       = 9,
    ALBUM_LOAD_MC_LOAD_INIT            = 10,
    ALBUM_LOAD_MC_LOAD_WAIT            = 11,
    ALBUM_LOAD_MC_ERROR_CONF_INIT      = 12,
    ALBUM_LOAD_MC_ERROR_CONF_WAIT      = 13,
    ALBUM_LOAD_MC_END_CONF             = 14,
    ALBUM_LOAD_MC_STEP_MAX             = 15
};

/* Message ids in bank 0x50, decoded out of IMG_BD.BIN (CD file 0xd38, the
 * English message file) so the machine below reads.  The ROM writes them as
 * bare numbers and so does loadgame.c; the names are the port's. */
#define ALBUM_LOAD_MSG_NONE             0x00    /* "Checking memory card..."   */
#define ALBUM_LOAD_MSG_NO_CARD          0x01    /* "No memory card in slot n"  */
#define ALBUM_LOAD_MSG_READ_FAILED      0x02    /* "Failed to read memory card"*/
#define ALBUM_LOAD_MSG_WHICH_SLOT       0x0c    /* "Which MEMORY CARD slot..." */
#define ALBUM_LOAD_MSG_LOAD_NOW         0x0e    /* "Load now?"                 */
#define ALBUM_LOAD_MSG_LOAD_FAILED      0x0f    /* "Load failed!"              */
#define ALBUM_LOAD_MSG_LOAD_OK          0x10    /* "Load successful."          */
#define ALBUM_LOAD_MSG_SEL_ALBUM        0x2a    /* "Select Album to be loaded."*/
#define ALBUM_LOAD_MSG_CARD_ERROR       0x2b    /* "...error! Data may be corrupt." */
#define ALBUM_LOAD_MSG_LOADING          0x2c    /* "Loading data."             */
#define ALBUM_LOAD_MSG_NO_ALBUM_DATA    0x2d    /* "No ... album data present" */

/* The card file is read into the staging buffer, never straight into the
 * album -- DevelopMemoryCardLoadData() is what moves it on. */
static void *album_load_buff_addr;                          /* sdata 3ef418 */

/* types.txt.  album_flg is a fixed_array<>, not a plain char[5]: every
 * subscript in the object carries an inlined _fixed_array_verifyrange<char>. */
typedef struct                      /* 0xd */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mc_step;
    /* 0x2 */ char mode;
    /* 0x3 */ char dir_check_cnt;   /* how many album dirs have been probed */
    /* 0x4 */ char slot_csr;        /* MEMORY CARD port                     */
    /* 0x5 */ char album_sel_csr;   /* album, = dir_label - 1               */
    /* 0x6 */ char conf_csr;        /* 0 yes, 1 no                          */
    /* 0x7 */ char load_file_cnt;
    /* 0x8 */ fixed_array<char, ALBUM_LOAD_ALBUM_MAX> album_flg;
} ALBUM_LOAD_CTRL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char anim_step;       /* Zero2Anim2D in/out step */
    /* 0x1 */ char anim_timer;
    /* 0x2 */ char csr_anim_timer;  /* cursor pulse            */
    /* 0x4 */ int  msg_id;
} ALBUM_LOAD_DISP;

static ALBUM_LOAD_CTRL album_load_ctrl;                     /* bss  422110 */
static ALBUM_LOAD_DISP album_load_disp;                     /* sbss 3f4ac0 */

static int  AlbumLoadTexLoadWait(void);
static void AlbumLoadModeMain(void);
static void AlbumLoadSlotSelPad(void);
static void AlbumLoadOutReq(void);
static void AlbumLoadMcLoad(void);
static void AlbumLoadMcCardCheckInit(void);
static void AlbumLoadMcCardCheckWait(void);
static void AlbumLoadMcGetDirInfoInit(void);
static void AlbumLoadMcGetDirInfoWait(void);
static void AlbumLoadMcLoadAlbumSelInit(void);
static void AlbumLoadMcLoadAlbumSelWait(void);
static void AlbumLoadMcLoadAlbumSelPad(void);
static void AlbumLoadMcCheckAgainInit(void);
static void AlbumLoadMcCheckAgainWait(void);
static void AlbumLoadMcLoadConfInit(void);
static void AlbumLoadMcLoadConfWait(void);
static void AlbumLoadMcLoadConfPad(void);
static void AlbumLoadMcLoadInit(void);
static void AlbumLoadMcLoadWait(void);
static void AlbumLoadMcErrorConfInit(void);
static void AlbumLoadMcErrorConfWait(void);
static void AlbumLoadMcErrorConfPad(void);
static void AlbumLoadMcEndConf(void);
static void AlbumLoadMcEveryFrameCheck(void);
static void AlbumLoadDispInit(void);
static void AlbumLoadSlotSelDisp(int off_x, int off_y, u_char alpha);
static void AlbumLoadMcLoadDisp(int off_x, int off_y, u_char alpha);
static void AlbumLoadAlbumSelDisp(u_char alpha);

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* Reset everything and point system/mc's save-data marshalling at the album
 * the edit view has selected.  That last pair is what makes the load land in
 * the right album: save_album_data[]'s callbacks write through the address
 * SetAlbumSaveDataAddr() parks. */
void AlbumLoadCtrlInit(void)                                            /* 188 */
{
    int i;

    album_load_ctrl.step           = ALBUM_LOAD_DISP_INIT;              /* 193 */
    album_load_ctrl.mc_step        = ALBUM_LOAD_MC_CARD_CHECK_INIT;     /* 194 */
    album_load_ctrl.mode           = ALBUM_LOAD_MODE_SLOT_SEL;          /* 195 */
    album_load_ctrl.dir_check_cnt  = 0;                                 /* 196 */
    album_load_ctrl.slot_csr       = 0;                                 /* 197 */
    album_load_ctrl.album_sel_csr  = 0;                                 /* 198 */
    album_load_ctrl.conf_csr       = 1;             /* default "no" */   /* 199 */
    album_load_ctrl.load_file_cnt  = 0;                                 /* 200 */

    for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++) {                        /* 202 */
        album_load_ctrl.album_flg[i] = 0;                               /* 203 */
    }                                                                   /* 204 */

    SetAlbumSaveDataAddr(GetAlbumDataAddr(GetCurrentAlbum()));          /* 207 */

    MemoryCardExeInit();                                                /* 210 */
}

/* Both of the screen's own paks.  album.o requested them in
 * AlbumBackGroundLoadReq() and nothing else waits on them, so this is the only
 * gate.
 *
 * ROM inconsistency, reproduced: album.c posts the slot-select pak as
 * ALBM_SLOT_SL_PK2 + GetLanguage() but the wait below is on the bare
 * ALBM_SLOT_SL_PK2 -- there is no GetLanguage() call anywhere in this
 * function.  It is benign because FileLoadIsEnd2() answers 1 when it finds no
 * queued request with that (file_no, buffer) pair, so on a non-English build
 * the second term is simply not a wait at all. */
static int AlbumLoadTexLoadWait(void)                                   /* 220 */
{
    void *album_sl_tex_addr;
    void *album_slot_tex_addr;
    int   res;

    album_sl_tex_addr   = GetAlbumSaveLoadTexAddr();                    /* 227 */
    album_slot_tex_addr = GetAlbumSlotSelTexAddr();                     /* 228 */

    res = 0;                                                            /* 230 */

    if (FileLoadIsEnd2(ALBM_SL_PK2, album_sl_tex_addr) != 0) {          /* 234 */
        res = (FileLoadIsEnd2(ALBM_SLOT_SL_PK2, album_slot_tex_addr) != 0); /* 235 */
    }

    return res;                                                         /* 241 */
}

/* ==========================================================================
 *  The screen
 * ======================================================================== */

/* One frame.  Returns non-zero on the frame the page is finished with, which
 * is album_edit.o's cue to leave.
 *
 * Step 3 waits on AlbumMemMain() as well as on the fade: AlbumLoadOutReq()
 * only clears album_mem.o's control block, and the packet ring is not actually
 * back until a later AlbumMemMain() has re-issued the resize. */
int AlbumLoadMain(void)                                                 /* 253 */
{
    int res;

    res = 0;

    switch (album_load_ctrl.step) {                                     /* 260 */

    case ALBUM_LOAD_DISP_INIT:
        AlbumLoadDispInit();                                            /* 263 */

        AlbumMemInit(ALBUM_LOAD_MEM_SIZE, "album_load.c", 266);         /* 266 */

        album_load_ctrl.step = ALBUM_LOAD_LOAD_WAIT;                    /* 269 */
        break;

    case ALBUM_LOAD_LOAD_WAIT:
        if (AlbumLoadTexLoadWait() != 0) {                              /* 271 */
            if (AlbumMemMain() != 0) {                                  /* 272 */

                album_load_ctrl.step = ALBUM_LOAD_MC_EXE;               /* 276 */
            }
        }
        break;

    case ALBUM_LOAD_MC_EXE:
        AlbumLoadModeMain();                                            /* 279 */
        break;                                                          /* 280 */

    case ALBUM_LOAD_OUT:
        SetAlbumTitleFlg(1);                                            /* 284 */

        AlbumEditMenuDelete();                                          /* 286 */

        if (album_load_disp.anim_step == ZERO2_ANIM2D_STEP_END) {       /* 287 */
            if (AlbumMemMain() != 0) {                                  /* 288 */
                res = 1;
            }
        }
        break;                                                          /* 292 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 294 */
        break;
    }

    return res;                                                         /* 298 */
}

/* The two halves of the page.  The title plate stays up over the slot-select
 * window and comes down for the card machine, which draws its own message
 * window in the same place. */
static void AlbumLoadModeMain(void)                                     /* 304 */
{
    switch (album_load_ctrl.mode) {                                     /* 307 */

    case ALBUM_LOAD_MODE_SLOT_SEL:

        SetAlbumTitleFlg(1);                                            /* 311 */

        AlbumLoadSlotSelPad();                                          /* 314 */
        break;

    case ALBUM_LOAD_MODE_MC_LOAD:
        SetAlbumTitleFlg(0);                                            /* 319 */

        AlbumLoadMcLoad();                                              /* 321 */
        break;                                                          /* 322 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 324 */
        break;
    }
}

/* UP or DOWN toggles the slot, CROSS commits it and CIRCLE leaves the page.
 * Committing is what binds the port for every later card call --
 * MemoryCardSetAccessPort() -- so the rest of the machine can pass slot_csr
 * around without re-deciding. */
static void AlbumLoadSlotSelPad(void)                                   /* 337 */
{
    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0) ||                  /* 341 */
        (pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {                  /* 347 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 348 */

        album_load_ctrl.slot_csr =
            (char)((album_load_ctrl.slot_csr + 1) % ALBUM_LOAD_SLOT_MAX); /* 350 */

    } else if (*paddat[0] == 1) {                                       /* 353 */
        album_load_ctrl.mode    = ALBUM_LOAD_MODE_MC_LOAD;              /* 354 */
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_CARD_CHECK_INIT;        /* 355 */

        MemoryCardSetAccessPort(album_load_ctrl.slot_csr);              /* 358 */

        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 360 */

    } else if (*paddat[1] == 1) {                                       /* 363 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 364 */

        AlbumLoadOutReq();                                              /* 367 */
    }
}

/* Ask the page to close.  Giving the staging buffer back here rather than in
 * AlbumLoadEnd() is what lets AlbumLoadMain()'s step 3 hold the phase until
 * the packet ring is whole again. */
static void AlbumLoadOutReq(void)                                       /* 376 */
{
    album_load_disp.anim_step  = ZERO2_ANIM2D_STEP_OUT;                 /* 379 */
    album_load_disp.anim_timer = 0;                                     /* 380 */

    album_load_ctrl.step = ALBUM_LOAD_OUT;                              /* 382 */

    AlbumMemFree("album_load.c", 385);                                  /* 385 */
}

/* ==========================================================================
 *  The card machine
 *
 *  Every Init case falls through into its Wait case, so a request is issued
 *  and polled on the same frame.  The fall-through is the ROM's own: the jump
 *  table at rodata 3a0b90 carries a separate entry for both halves.
 * ======================================================================== */

static void AlbumLoadMcLoad(void)                                       /* 393 */
{
    switch (album_load_ctrl.mc_step) {                                  /* 396 */

    case ALBUM_LOAD_MC_CARD_CHECK_INIT:
        AlbumLoadMcCardCheckInit();                                     /* 398 */

    case ALBUM_LOAD_MC_CARD_CHECK_WAIT:
        AlbumLoadMcCardCheckWait();                                     /* 401 */
        break;

    case ALBUM_LOAD_MC_GET_DIR_INFO_INIT:
        AlbumLoadMcGetDirInfoInit();                                    /* 404 */

    case ALBUM_LOAD_MC_GET_DIR_INFO_WAIT:
        AlbumLoadMcGetDirInfoWait();                                    /* 407 */
        break;

    case ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT:
        AlbumLoadMcLoadAlbumSelInit();                                  /* 410 */

    case ALBUM_LOAD_MC_LOAD_ALBUM_SEL_WAIT:
        AlbumLoadMcLoadAlbumSelWait();                                  /* 413 */
        break;

    case ALBUM_LOAD_MC_CHECK_AGAIN_INIT:
        AlbumLoadMcCheckAgainInit();                                    /* 416 */

    case ALBUM_LOAD_MC_CHECK_AGAIN_WAIT:
        AlbumLoadMcCheckAgainWait();                                    /* 419 */
        break;

    case ALBUM_LOAD_MC_LOAD_CONF_INIT:
        AlbumLoadMcLoadConfInit();                                      /* 422 */

    case ALBUM_LOAD_MC_LOAD_CONF_WAIT:
        AlbumLoadMcLoadConfWait();                                      /* 425 */
        break;

    case ALBUM_LOAD_MC_LOAD_INIT:
        AlbumLoadMcLoadInit();                                          /* 428 */

    case ALBUM_LOAD_MC_LOAD_WAIT:
        AlbumLoadMcLoadWait();                                          /* 431 */
        break;

    case ALBUM_LOAD_MC_ERROR_CONF_INIT:
        AlbumLoadMcErrorConfInit();                                     /* 434 */

    case ALBUM_LOAD_MC_ERROR_CONF_WAIT:
        AlbumLoadMcErrorConfWait();                                     /* 437 */
        break;

    case ALBUM_LOAD_MC_END_CONF:
        AlbumLoadMcEndConf();                                           /* 440 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 443 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Step 0/1 -- is there a card in the chosen slot, and is it formatted.
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcCardCheckInit(void)                              /* 452 */
{
    MemoryCardGetCardInfoInit(album_load_ctrl.slot_csr, 0);             /* 455 */

    album_load_disp.msg_id  = ALBUM_LOAD_MSG_NONE;                      /* 457 */
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_CARD_CHECK_WAIT;            /* 458 */
}

/* A swapped card (-1) is not an error here: the listing simply runs again for
 * whatever is in the slot now. */
static void AlbumLoadMcCardCheckWait(void)                              /* 466 */
{
    int mc_res;

    mc_res = MemoryCardGetCardInfoMain();                               /* 473 */

    if (mc_res == 1) {                                                  /* 476 */

        if (GetAccessMemoryCardFormat() == 1) {                         /* 478 */
            album_load_ctrl.dir_check_cnt = 0;                          /* 479 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_GET_DIR_INFO_INIT;  /* 480 */
            return;
        }

        album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;          /* 484 */

    } else {

        if (mc_res >= 0) {                                              /* 488 */
            return;
        }

        switch (mc_res) {                                               /* 490 */

        case -1:
            album_load_disp.msg_id        = ALBUM_LOAD_MSG_NONE;        /* 492 */
            album_load_ctrl.dir_check_cnt = 0;                          /* 493 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_GET_DIR_INFO_INIT;  /* 494 */
            return;                                                     /* 495 */

        case -2:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;      /* 499 */
            break;

        case -0x14:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_CARD;            /* 503 */
            break;

        default:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_READ_FAILED;        /* 507 */
            break;
        }
    }

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;            /* 508 */
}

/* --------------------------------------------------------------------------
 *  Step 2/3 -- the five album directories, one per frame pair.
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcGetDirInfoInit(void)                             /* 518 */
{
    char path_name[ALBUM_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);                     /* 522 */

    MemoryCardMakeSearchDirPath(path_name, album_load_ctrl.dir_check_cnt + 1); /* 526 */

    MemoryCardGetDirInfoInit(GetAccessMemoryCardPort(),
                             GetAccessMemoryCardSlot(), path_name);     /* 528 */

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_GET_DIR_INFO_WAIT;          /* 530 */
}

/* One directory answered.  -4 ("no such entry") and -6 ("not empty") are not
 * errors -- they mean that album simply has not been saved yet -- so they
 * clear the flag and walk on with the successful path; anything else stops the
 * whole listing.
 *
 * Once all five have been probed the first album that does exist becomes the
 * cursor's start, and a card with none of them at all reports "No Project Zero
 * II album data present" rather than showing an empty list. */
static void AlbumLoadMcGetDirInfoWait(void)                             /* 538 */
{
    int mc_res;
    int i;

    mc_res = MemoryCardGetDirInfoMain();                                /* 547 */

    if (mc_res == 1) {                                                  /* 550 */
        album_load_ctrl.album_flg[album_load_ctrl.dir_check_cnt] = 1;   /* 552 */
        album_load_ctrl.dir_check_cnt++;                                /* 553 */

    } else if (mc_res < 0) {                                            /* 555 */

        switch (mc_res) {                                               /* 557 */

        case -2:
            album_load_ctrl.dir_check_cnt = 0;                          /* 559 */
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_NO_ALBUM_DATA;     /* 560 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;    /* 561 */
            break;                                                      /* 562 */

        case -4:
        case -6:
            /* No $LM of its own: the subscript is a fixed_array<> and swallows
             * the statement's line note.  Interpolated into the 564..566 gap. */
            album_load_ctrl.album_flg[album_load_ctrl.dir_check_cnt] = 0; /* 565 */
            album_load_ctrl.dir_check_cnt++;                            /* 566 */
            break;                                                      /* 567 */

        case -0x14:
            album_load_ctrl.dir_check_cnt = 0;                          /* 569 */
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_NO_CARD;           /* 570 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;    /* 571 */
            break;                                                      /* 572 */

        default:
            album_load_ctrl.dir_check_cnt = 0;                          /* 576 */
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_READ_FAILED;       /* 577 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;    /* 578 */
            break;
        }
    }

    if ((mc_res == 1) || (mc_res == -4) || (mc_res == -6)) {            /* 583 */

        if (album_load_ctrl.dir_check_cnt >= ALBUM_LOAD_ALBUM_MAX) {    /* 585 */

            for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++) {                /* 587 */
                if (album_load_ctrl.album_flg[i] == 1) {                /* 588 */
                    album_load_ctrl.album_sel_csr = (char)i;            /* 589 */
                    break;
                }
            }                                                           /* 592 */

            /* GCC threaded this test into the loop's own exit -- there is no
             * compare against i after the loop -- so it leaves no code and no
             * note of its own. */
            if (i >= ALBUM_LOAD_ALBUM_MAX) {                            /* 594 */

                album_load_disp.msg_id  = ALBUM_LOAD_MSG_NO_ALBUM_DATA; /* 596 */
                album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;/* 597 */

            } else {
                album_load_disp.msg_id  = ALBUM_LOAD_MSG_SEL_ALBUM;     /* 600 */
                album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT; /* 601 */
            }

        } else {
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_NONE;              /* 605 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_GET_DIR_INFO_INIT;  /* 606 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Step 4/5 -- pick one of the albums that exist.
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcLoadAlbumSelInit(void)                           /* 614 */
{
    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);         /* 618 */

    album_load_disp.msg_id  = ALBUM_LOAD_MSG_SEL_ALBUM;                 /* 620 */
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_WAIT;        /* 621 */
}

static void AlbumLoadMcLoadAlbumSelWait(void)                           /* 628 */
{
    AlbumLoadMcLoadAlbumSelPad();                                       /* 632 */

    AlbumLoadMcEveryFrameCheck();                                       /* 635 */
}

/* LEFT / RIGHT walk the five albums, skipping the ones the listing said are
 * not there.  The bounded loop is what stops a card with no albums spinning --
 * though the screen cannot get here in that case anyway.
 *
 * The move cue only plays if the cursor actually moved, so with exactly one
 * album on the card the loop comes back to where it started and no sound is
 * made at all.  Same shape outgame/setup_menu.c's locked-row walk has, minus
 * its error cue. */
static void AlbumLoadMcLoadAlbumSelPad(void)                            /* 643 */
{
    int  i;
    char csr_back_up;

    csr_back_up = album_load_ctrl.album_sel_csr;                        /* 648 */

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {                  /* 652 */

        for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++) {                    /* 653 */
            album_load_ctrl.album_sel_csr = (char)((album_load_ctrl.album_sel_csr
                + (ALBUM_LOAD_ALBUM_MAX - 1)) % ALBUM_LOAD_ALBUM_MAX);  /* 654 */

            /* Swallowed by fixed_array<>'s operator[]; interpolated. */
            if (album_load_ctrl.album_flg[album_load_ctrl.album_sel_csr] == 1) { /* 656 */
                break;
            }
        }                                                               /* 660 */

        if (album_load_ctrl.album_sel_csr != csr_back_up) {             /* 662 */
            SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);           /* 663 */
        }

    } else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {           /* 667 */

        for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++) {                    /* 668 */
            album_load_ctrl.album_sel_csr =
                (char)((album_load_ctrl.album_sel_csr + 1) % ALBUM_LOAD_ALBUM_MAX); /* 669 */

            /* Swallowed by fixed_array<>'s operator[]; interpolated. */
            if (album_load_ctrl.album_flg[album_load_ctrl.album_sel_csr] == 1) { /* 671 */
                break;
            }
        }                                                               /* 675 */

        if (album_load_ctrl.album_sel_csr != csr_back_up) {             /* 677 */
            SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);           /* 678 */
        }

    } else if (*paddat[0] == 1) {                                       /* 682 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 683 */

        album_load_ctrl.mc_step = ALBUM_LOAD_MC_CHECK_AGAIN_INIT;       /* 685 */
        album_load_disp.msg_id  = ALBUM_LOAD_MSG_NONE;                  /* 686 */

    } else if (*paddat[1] == 1) {                                       /* 689 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 690 */

        album_load_disp.msg_id = ALBUM_LOAD_MSG_NONE;                   /* 692 */
        album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;              /* 693 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 6/7 -- re-list the chosen album's directory, this time to check it is
 *  intact.  The listing from steps 2/3 is long gone: mc_check_dir.o keeps one
 *  buffer and every probe overwrote it.
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcCheckAgainInit(void)                             /* 702 */
{
    char path_name[ALBUM_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);                     /* 706 */

    MemoryCardMakeSearchDirPath(path_name, album_load_ctrl.album_sel_csr + 1); /* 710 */

    MemoryCardCheckInit(album_load_ctrl.slot_csr, 0, path_name);        /* 712 */

    album_load_disp.msg_id  = ALBUM_LOAD_MSG_NONE;                      /* 714 */
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_CHECK_AGAIN_WAIT;           /* 715 */
}

static void AlbumLoadMcCheckAgainWait(void)                             /* 723 */
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 730 */

    if (mc_res == 1) {                                                  /* 733 */

        if (MemoryCardCheckDirBroken(album_load_ctrl.album_sel_csr + 1) != 0) { /* 735 */
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_LOAD_NOW;          /* 737 */
            album_load_ctrl.conf_csr = 1;           /* default "no" */   /* 738 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_CONF_INIT;     /* 739 */

        } else {
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_CARD_ERROR;        /* 743 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;    /* 744 */
        }

    } else if (mc_res < 0) {                                            /* 748 */

        switch (mc_res) {                                               /* 750 */

        case -2:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;      /* 754 */
            break;

        case -4:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;      /* 758 */
            break;

        case -6:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_CARD_ERROR;         /* 761 */
            break;

        case -1:
        case -0x14:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_CARD;            /* 765 */
            break;

        default:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_READ_FAILED;        /* 769 */
            break;
        }

        album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;        /* 772 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 8/9 -- "Load now?"
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcLoadConfInit(void)                               /* 781 */
{
    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);         /* 785 */

    album_load_ctrl.conf_csr = 1;                   /* default "no" */   /* 787 */
    album_load_disp.msg_id   = ALBUM_LOAD_MSG_LOAD_NOW;                 /* 788 */
    album_load_ctrl.mc_step  = ALBUM_LOAD_MC_LOAD_CONF_WAIT;            /* 789 */
}

static void AlbumLoadMcLoadConfWait(void)                               /* 797 */
{
    AlbumLoadMcLoadConfPad();                                           /* 801 */

    AlbumLoadMcEveryFrameCheck();                                       /* 804 */
}

/* "No" and CIRCLE both go back to the album list, which is why their two arms
 * are identical.  Only "yes" starts the transfer. */
static void AlbumLoadMcLoadConfPad(void)                                /* 812 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 816 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 821 */

        SystemBankPlay(0, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 822 */

        album_load_ctrl.conf_csr ^= 1;                                  /* 823 */

    } else if (*paddat[0] == 1) {                                       /* 826 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 827 */

        if (album_load_ctrl.conf_csr == 0) {                            /* 829 */
            album_load_disp.msg_id        = ALBUM_LOAD_MSG_LOADING;     /* 830 */
            album_load_ctrl.load_file_cnt = 0;                          /* 831 */
            album_load_ctrl.mc_step       = ALBUM_LOAD_MC_LOAD_INIT;    /* 832 */

        } else {
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_SEL_ALBUM;         /* 836 */
            album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT;/* 837 */
        }

    } else if (*paddat[1] == 1) {                                       /* 841 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 842 */

        album_load_disp.msg_id  = ALBUM_LOAD_MSG_SEL_ALBUM;             /* 843 */
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT;    /* 844 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 10/11 -- the transfer itself: one file, the whole album.
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcLoadInit(void)                                   /* 853 */
{
    int  size;
    char path_name[ALBUM_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, ALBUM_LOAD_PATH_NAME_LEN);                     /* 859 */

    MemoryCardSetFilePath(path_name, album_load_ctrl.album_sel_csr + 1, 0); /* 863 */

    size = GetMemoryCardDataSize(album_load_ctrl.album_sel_csr + 1, 0); /* 865 */

    /* The staging buffer is a fixed 1 MB off the packet ring, so an album
     * bigger than that would overrun it.  The assert does not stop the load. */
    if (size > ALBUM_LOAD_MEM_SIZE) {                                   /* 867 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 868 */
    }

    album_load_buff_addr = GetAlbumMemAddr();                           /* 872 */

    MemoryCardFileLoadInit(album_load_ctrl.slot_csr, 0, path_name,
                           album_load_buff_addr, size);                 /* 874 */

    album_load_disp.msg_id  = ALBUM_LOAD_MSG_LOADING;                   /* 876 */
    album_load_ctrl.mc_step = ALBUM_LOAD_MC_LOAD_WAIT;                  /* 877 */
}

/* The file is on the card and in the staging buffer; the checksum and the
 * never-written-to test both have to pass before it is unpacked into the
 * album.  MemoryCardCheckNewFileLoad() answering non-zero means the file is
 * the all-zero, checksum-0xffffffff image MemoryCardAllFileMakeMain() writes
 * -- an album directory that exists but has never been saved into. */
static void AlbumLoadMcLoadWait(void)                                   /* 884 */
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();                                  /* 893 */

    if (mc_res == 1) {                                                  /* 896 */

        size = GetMemoryCardDataSize(album_load_ctrl.album_sel_csr + 1, 0); /* 898 */

        if ((MemoryCardCheckFileBroken(album_load_buff_addr, size) != 0) && /* 901 */
            (MemoryCardCheckNewFileLoad(album_load_buff_addr, size) == 0)) { /* 903 */

            DevelopMemoryCardLoadData((char *)album_load_buff_addr,
                                      album_load_ctrl.album_sel_csr + 1, 0); /* 912 */

            album_load_ctrl.mc_step = ALBUM_LOAD_MC_END_CONF;           /* 914 */
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_LOAD_OK;           /* 915 */

        } else {

            album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;    /* 920 */
            album_load_disp.msg_id  = ALBUM_LOAD_MSG_CARD_ERROR;        /* 921 */
        }

    } else if (mc_res < 0) {                                            /* 925 */

        switch (mc_res) {                                               /* 927 */

        case -2:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;      /* 930 */
            break;

        case -3:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_CARD_ERROR;         /* 933 */
            break;

        default:

            album_load_disp.msg_id = ALBUM_LOAD_MSG_LOAD_FAILED;        /* 939 */
            break;
        }

        album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;        /* 941 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 12/13 -- the error screen, and step 14 -- the success one.
 * ------------------------------------------------------------------------ */
static void AlbumLoadMcErrorConfInit(void)                              /* 950 */
{
    MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);         /* 954 */

    album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_WAIT;            /* 956 */
}

/* Unlike AlbumLoadMcEveryFrameCheck(), a card swapped while the error message
 * is up restarts the whole machine rather than reporting again -- which is how
 * "no memory card in slot n" clears itself when one is inserted. */
static void AlbumLoadMcErrorConfWait(void)                              /* 963 */
{
    int mc_res;

    AlbumLoadMcErrorConfPad();                                          /* 971 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 974 */

    if (mc_res == -1) {                                                 /* 977 */
        album_load_disp.msg_id  = ALBUM_LOAD_MSG_NONE;                  /* 978 */
        album_load_ctrl.mc_step = ALBUM_LOAD_MC_CARD_CHECK_INIT;        /* 979 */

    } else if (mc_res == 1) {                                           /* 982 */
        MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);     /* 983 */

    } else if (mc_res < 0) {                                            /* 986 */
        MemoryCardCheckEveryFrameInit(album_load_ctrl.slot_csr, 0);     /* 987 */
    }
}

/* Both buttons do the same thing, and the ROM still spells the arm out twice
 * -- GCC cross-jumped the pair into one tail, which is why only the second
 * copy's line numbers survive. */
static void AlbumLoadMcErrorConfPad(void)                               /* 996 */
{
    if (*paddat[0] == 1) {                                              /* 1000 */

        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1003 */

        album_load_disp.msg_id = ALBUM_LOAD_MSG_NONE;                   /* 1004 */
        album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;              /* 1005 */

    } else if (*paddat[1] == 1) {                                       /* 1006 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1007 */

        album_load_disp.msg_id = ALBUM_LOAD_MSG_NONE;                   /* 1008 */
        album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;              /* 1009 */
    }
}

/* "Load successful." -- either button acknowledges it, and both then ask
 * album_edit.o to decompress the album that just arrived. */
static void AlbumLoadMcEndConf(void)                                    /* 1018 */
{
    if (*paddat[0] == 1) {                                              /* 1022 */
        SystemBankPlay(3, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1023 */

        album_load_disp.msg_id = ALBUM_LOAD_MSG_NONE;                   /* 1024 */
        album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;              /* 1025 */

        AlbumEditUncompressPhotoReq();                                  /* 1027 */

    } else if (*paddat[1] == 1) {                                       /* 1030 */
        SystemBankPlay(1, 1, 0, 0, NULL, 0x3200, 0x1000);               /* 1031 */

        album_load_disp.msg_id = ALBUM_LOAD_MSG_NONE;                   /* 1032 */
        album_load_ctrl.mode   = ALBUM_LOAD_MODE_SLOT_SEL;              /* 1033 */

        AlbumEditUncompressPhotoReq();                                  /* 1035 */
    }
}

/* Shared tail of the interactive steps: notice the card being pulled and
 * divert to the error confirm.  A swap (-1) reports "no memory card" here
 * rather than restarting, because the album list on screen belongs to the card
 * that just left. */
static void AlbumLoadMcEveryFrameCheck(void)                            /* 1044 */
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1052 */

    if (mc_res < 0) {                                                   /* 1054 */

        switch (mc_res) {                                               /* 1055 */

        case -1:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_CARD;            /* 1059 */
            break;

        case -2:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_ALBUM_DATA;      /* 1063 */
            break;

        case -0x14:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_NO_CARD;            /* 1066 */
            break;

        default:
            album_load_disp.msg_id = ALBUM_LOAD_MSG_READ_FAILED;        /* 1070 */
            break;
        }

        album_load_ctrl.mc_step = ALBUM_LOAD_MC_ERROR_CONF_INIT;        /* 1072 */
    }
}

/* Give the card lock back.  The paks belong to album.o and the staging buffer
 * went back in AlbumLoadOutReq(), so there is nothing else to undo. */
void AlbumLoadEnd(void)                                                 /* 1085 */
{
    MemoryCardEnd();                                                    /* 1089 */
}

/* ==========================================================================
 *  Drawing
 * ======================================================================== */

static void AlbumLoadDispInit(void)                                     /* 1101 */
{
    album_load_disp.anim_step      = ZERO2_ANIM2D_STEP_START;           /* 1104 */
    album_load_disp.anim_timer     = 0;                                 /* 1105 */
    album_load_disp.msg_id         = ALBUM_LOAD_MSG_NONE;               /* 1106 */
    album_load_disp.csr_anim_timer = 0;                                 /* 1107 */
}

/* One frame of the page.  Nothing is drawn until the card machine has the
 * screen, and nothing once the closing fade has finished -- the black
 * backdrop included, which is what lets the edit view show through again. */
void AlbumLoadDispMain(void)                                            /* 1115 */
{
    u_char alpha;

    alpha = 0x80;                                                       /* 1119 */

    if ((album_load_ctrl.step == ALBUM_LOAD_MC_EXE) ||                  /* 1124 */
        (album_load_ctrl.step == ALBUM_LOAD_OUT)) {

        AlbumInOutAnimCtrl(&album_load_disp.anim_step,
                           &album_load_disp.anim_timer, &alpha);        /* 1125 */

        if (album_load_disp.anim_step != ZERO2_ANIM2D_STEP_END) {       /* 1127 */

            AlbumBlackBgDisp(0, 0, alpha, 0x80);                        /* 1129 */

            switch (album_load_ctrl.mode) {                             /* 1131 */

            case ALBUM_LOAD_MODE_SLOT_SEL:
                AlbumLoadSlotSelDisp(0, 0, alpha);                      /* 1134 */
                break;                                                  /* 1135 */

            case ALBUM_LOAD_MODE_MC_LOAD:
                AlbumLoadMcLoadDisp(0, 0, alpha);                       /* 1138 */
                break;                                                  /* 1139 */

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 1141 */
                break;
            }
        }
    }
}

/* The slot-select window, plus the caption bar.  The window itself is drawn at
 * a literal 0, 0 -- only the caption honours the page offset, and both callers
 * pass 0, 0 anyway. */
static void AlbumLoadSlotSelDisp(int off_x, int off_y, u_char alpha)    /* 1156 */
{
    void *tm2_addr;

    tm2_addr = GetAlbumSlotSelTexAddr();                                /* 1161 */

    PK2SendVram((uintptr_t)tm2_addr, -1, -1, 0);                 /* 1164 */

    AlbumSlotSelWinDisp(album_load_ctrl.slot_csr, 0, 0, alpha);         /* 1167 */

    DrawCmnWindow(0, 24.0f, 330.0f, 592.0f, 112.0f, alpha, 0x66);       /* 1171 */

    PrintMsg(0x50, ALBUM_LOAD_MSG_WHICH_SLOT, 0x44, 0x15d, 1, alpha, 0); /* 1176 */

    AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                       /* 1179 */
}

/* The card machine's own page, one arm per group of steps.  Which of the two
 * paks is uploaded is the real difference between them: steps 0..3 and 12/13
 * still show the slot-select window, everything else the album list.
 *
 * The cursor pulse is drawn twice over -- once as a flare and once as the
 * cursor itself -- and which of the two carries Zero2Anim2D_CsrAnimCtrl()'s
 * intensity is what marks the list as live (steps 6..11, 14) or as merely
 * showing the choice already made (steps 4/5, 8/9). */
static void AlbumLoadMcLoadDisp(int off_x, int off_y, u_char alpha)     /* 1190 */
{
    void  *album_slot_tex_addr;
    void  *album_sl_addr;
    u_char csr_rgb;

    album_slot_tex_addr = GetAlbumSlotSelTexAddr();                     /* 1197 */
    album_sl_addr       = GetAlbumSaveLoadTexAddr();                    /* 1198 */

    csr_rgb = 0x80;                                                     /* 1200 */

    switch (album_load_ctrl.mc_step) {                                  /* 1202 */

    case ALBUM_LOAD_MC_CARD_CHECK_INIT:
    case ALBUM_LOAD_MC_CARD_CHECK_WAIT:
    case ALBUM_LOAD_MC_GET_DIR_INFO_INIT:
    case ALBUM_LOAD_MC_GET_DIR_INFO_WAIT:
        PK2SendVram((uintptr_t)album_slot_tex_addr, -1, -1, 0);  /* 1207 */

        AlbumSlotSelWinDisp(album_load_ctrl.slot_csr, off_x, off_y, alpha); /* 1210 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1213 */

        PrintMsg(0x50, album_load_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1216 */
        break;                                                          /* 1217 */

    case ALBUM_LOAD_MC_LOAD_ALBUM_SEL_INIT:
    case ALBUM_LOAD_MC_LOAD_ALBUM_SEL_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);        /* 1220 */

        AlbumSaveSelAlbumCsrFlareDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1223 */

        AlbumLoadAlbumSelDisp(alpha);                                   /* 1226 */

        Zero2Anim2D_CsrAnimCtrl(&album_load_disp.csr_anim_timer, &csr_rgb); /* 1229 */

        AlbumSaveSelAlbumCsrDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1232 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1235 */

        DrawCmnWindow(0, 24.0f, 330.0f, 592.0f, 112.0f, alpha, 0x66);   /* 1239 */

        PrintMsg(0x50, album_load_disp.msg_id, 0x44, 0x15d, 1, alpha, 0); /* 1244 */
        break;                                                          /* 1246 */

    case ALBUM_LOAD_MC_CHECK_AGAIN_INIT:
    case ALBUM_LOAD_MC_CHECK_AGAIN_WAIT:
    case ALBUM_LOAD_MC_LOAD_INIT:
    case ALBUM_LOAD_MC_LOAD_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);        /* 1251 */

        Zero2Anim2D_CsrAnimCtrl(&album_load_disp.csr_anim_timer, &csr_rgb); /* 1254 */

        AlbumSaveSelAlbumCsrFlareDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1257 */

        AlbumLoadAlbumSelDisp(alpha);                                   /* 1260 */

        AlbumSaveSelAlbumCsrDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1263 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1266 */

        PrintMsg(0x50, album_load_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1269 */
        break;                                                          /* 1270 */

    case ALBUM_LOAD_MC_END_CONF:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);        /* 1272 */

        Zero2Anim2D_CsrAnimCtrl(&album_load_disp.csr_anim_timer, &csr_rgb); /* 1275 */

        AlbumSaveSelAlbumCsrFlareDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1278 */

        AlbumLoadAlbumSelDisp(alpha);                                   /* 1281 */

        AlbumSaveSelAlbumCsrDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1284 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1287 */

        PrintMsg(0x50, album_load_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1290 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1293 */
        break;                                                          /* 1295 */

    case ALBUM_LOAD_MC_LOAD_CONF_INIT:
    case ALBUM_LOAD_MC_LOAD_CONF_WAIT:
        PK2SendVram((uintptr_t)album_sl_addr, -1, -1, 0);        /* 1298 */

        Zero2Anim2D_CsrAnimCtrl(&album_load_disp.csr_anim_timer, &csr_rgb); /* 1301 */

        AlbumSaveSelAlbumCsrFlareDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, csr_rgb); /* 1304 */

        AlbumLoadAlbumSelDisp(alpha);                                   /* 1307 */

        AlbumSaveSelAlbumCsrDisp(album_load_ctrl.album_sel_csr, 0, 0, alpha, 0x80); /* 1310 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1314 */

        PrintMsg(0x50, album_load_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1317 */

        DrawCmnYesNoSel(album_load_ctrl.conf_csr, (float)(off_y + 0x127), alpha, 0); /* 1319 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1335 */
        break;

    case ALBUM_LOAD_MC_ERROR_CONF_INIT:
    case ALBUM_LOAD_MC_ERROR_CONF_WAIT:
        PK2SendVram((uintptr_t)album_slot_tex_addr, -1, -1, 0);  /* 1338 */

        AlbumSlotSelWinDisp(album_load_ctrl.slot_csr, off_x, off_y, alpha); /* 1341 */

        AlbumMcMsgWinDisp(off_x, off_y, alpha);                         /* 1344 */

        PrintMsg(0x50, album_load_disp.msg_id, 0x5c, 0x8e, 1, alpha, 0); /* 1347 */

        AlbumSlotSelCaptionDisp(off_x, off_y, alpha);                   /* 1350 */
        break;                                                          /* 1351 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1353 */
        break;
    }
}

/* The five album spines, their names and the cursor plate behind whichever is
 * selected, then the slot indicator.  An album the listing did not find is
 * covered by a mask rather than left out, so the row of five is always the
 * same shape. */
static void AlbumLoadAlbumSelDisp(u_char alpha)                         /* 1370 */
{
    int i;
    int msg_col;

    for (i = 0; i < ALBUM_LOAD_ALBUM_MAX; i++) {                        /* 1379 */

        if (i != album_load_ctrl.album_sel_csr) {                       /* 1380 */

            AlbumSaveNonSelAlbumCsrDisp(i, 0, 0, alpha, 0x80);          /* 1384 */

            msg_col = 0x17;                                             /* 1385 */

        } else {
            msg_col = 0x18;                                             /* 1387 */
        }

        AlbumSaveSelAlbumNameDisp(i, 0, 0, alpha, msg_col);             /* 1391 */

        AlbumSaveSelAlbumDisp(i, 0, 0, alpha);                          /* 1394 */

        /* Swallowed by fixed_array<>'s operator[]; interpolated into the
         * 1395..1398 gap. */
        if (album_load_ctrl.album_flg[i] == 0) {                        /* 1397 */
            AlbumSaveAlbumMaskDisp(i, 0, 0, alpha, 0x80);               /* 1398 */
        }
    }                                                                   /* 1400 */

    AlbumSaveSelSlotDisp(album_load_ctrl.slot_csr, 0, 0, alpha, 0x80);  /* 1403 */
}
