// FILE: /home/zero_rom/zero2np/src/save_load/prg/game_data_save.c
//
// The game-data save screen: pick one of five slots and write it to the card.
//
// The write-side twin of outgame/loadgame.c -- same five slots, same drawing
// layer, same shape of memory-card machine -- but where the load screen only
// reads, this one has to cope with a card that has no directory yet, a
// directory that is broken, or a card that is not formatted at all.  That is
// what turns loadgame's 15 steps into 31 here: five extra Init/Wait pairs
// (remake, dir-delete, new-make, new-make-save, format) plus a format-end
// hold, each with its own yes/no prompt.
//
// The machine is two levels.  `step` is the screen: load the paks, animate in,
// run the card, animate out.  `mc_step` is the card, and every Init state
// falls straight through into its Wait state in the switch -- so a step that
// issues a request also polls it on the same frame, which is why the screen
// is not glacial.  Same idea as the mc folder's own step machines.
//
// A save writes three files, in the order save_file_label gives them: the
// system file (0), the chosen slot (csr + 2), and the play-data header (1).
// save_file_cnt walks that list, and the slot entry is patched into index 1 of
// the table at run time -- the ROM writes into what the compiler put in
// .rodata, which is a hazard on the host and is dealt with below.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), game_data_save.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Statements whose only memory access goes through
// fixed_array's inlined operator[] leave no $LM of their own -- their line is
// swallowed by fixed_array.h's 124/125 -- so a few are interpolated into a
// measured gap rather than read out of it.

#include "game_data_save.h"

#include "save_load_disp.h"                     // SaveLoad*Disp

#include "../../common/variable.h"              // ingame_wrk / pad / TIME_INFO
#include "../../common/utility2.h"              // PRINT_ASSERT
#include "../../graphics/graph3d/ctl/fixed_array.h"  // reference_fixed_array
#include "../../ingame/clear/prg/clear_flg.h"   // ClearFlgMerging / SetClearFlgCtrl
#include "../../ingame/menu/tim_dat/map_room_dat.h"  // room_info_dat
#include "../../ingame/menu/zero2_anim2d.h"     // Zero2Anim2D_InOutAnimCtrl
#include "../../system/eeiop/cddat.h"           // SAVE_LOAD_PK2 / GetFileSize
#include "../../system/eeiop/fileload.h"        // FileLoadReqEE / FileLoadIsEnd2
#include "../../system/eeiop/snd3d.h"           // SND_3D_SET
#include "../../system/mc/prg/mc.h"             // MemoryCardExeInit / MemoryCardEnd
#include "../../system/mc/prg/mc_check.h"       // MemoryCardCheckInit / Main
#include "../../system/mc/prg/mc_check_broken.h"// MemoryCardCheck*Broken / NewFileLoad
#include "../../system/mc/prg/mc_check_card.h"  // MemoryCardCheckEveryFrame*
#include "../../system/mc/prg/mc_check_empty.h" // MemoryCardCheckEmpty(Broken)
#include "../../system/mc/prg/mc_del_dir.h"     // MemoryCardDirDel*
#include "../../system/mc/prg/mc_format.h"      // MemoryCardFormat*
#include "../../system/mc/prg/mc_load.h"        // MemoryCardFileLoad*
#include "../../system/mc/prg/mc_make.h"        // MemoryCardNewMake*
#include "../../system/mc/prg/mc_save.h"        // MemoryCardFileSave*
#include "../../system/mc/prg/mc_set_data.h"    // path / size / data-area / accessors
#include "../../system/os/system.h"             // GetLanguage / SystemBankPlay
#include "../../system/pad/pad.h"               // pad / paddat / GetPadAnalogRpt

#include <string.h>                             // memset

#define GAME_DATA_SAVE_DATA_MAX 5   /* memory-card save slots */

/* Files one save writes, in order: see save_file_label_dat below. */
#define GAME_DATA_SAVE_FILE_NUM 3

/* Memory-card file indices within the save directory: 0 is the system file,
 * 1 the header, and slot n is n + 2.  Same numbering as loadgame.c. */
#define GAME_DATA_SAVE_MC_FILE_SYSTEM 0
#define GAME_DATA_SAVE_MC_FILE_HEAD   1
#define GAME_DATA_SAVE_MC_FILE_DATA   2

/* Path buffers are memset to 0x37 bytes by every caller; that is the ROM's
 * literal size, not sizeof(). */
#define GAME_DATA_SAVE_PATH_NAME_LEN 55

/* game_data_save_ctrl.step -- the screen itself. */
enum GAME_DATA_SAVE_STEP
{
    GAME_DATA_SAVE_DISP_INIT = 0,   /* reset the display state              */
    GAME_DATA_SAVE_LOAD_WAIT = 1,   /* waiting on the two paks              */
    GAME_DATA_SAVE_MC_EXE    = 2,   /* the card machine has the screen      */
    GAME_DATA_SAVE_OUT       = 3    /* animating out; Main() then returns 1 */
};

/* game_data_save_ctrl.mc_step.  The ROM's debug info carries no enum for
 * these -- the names are taken from the step handlers' own ROM symbol names so
 * the values still read against the jump table at rodata 3b35f0. */
enum GAME_DATA_SAVE_MC_STEP
{
    GDS_MC_CHECK_INIT           = 0,
    GDS_MC_CHECK_WAIT           = 1,
    GDS_MC_HEAD_LOAD_INIT       = 2,
    GDS_MC_HEAD_LOAD_WAIT       = 3,
    GDS_MC_SNAP_LOAD_INIT       = 4,
    GDS_MC_SNAP_LOAD_WAIT       = 5,
    GDS_MC_SAVE_FILE_SEL        = 6,
    GDS_MC_SAVE_CONF            = 7,
    GDS_MC_SYSTEM_LOAD_INIT     = 8,
    GDS_MC_SYSTEM_LOAD_WAIT     = 9,
    GDS_MC_SAVE_INIT            = 10,
    GDS_MC_SAVE_WAIT            = 11,
    GDS_MC_ERROR_CONF_INIT      = 12,
    GDS_MC_ERROR_CONF_WAIT      = 13,
    GDS_MC_END_CONF             = 14,
    GDS_MC_REMAKE_CONF_INIT     = 15,
    GDS_MC_REMAKE_CONF_WAIT     = 16,
    GDS_MC_REMAKE_DIR_DEL_INIT  = 17,
    GDS_MC_REMAKE_DIR_DEL_WAIT  = 18,
    GDS_MC_NEW_MAKE_CONF_INIT   = 19,
    GDS_MC_NEW_MAKE_CONF_WAIT   = 20,
    GDS_MC_NEW_MAKE_INIT        = 21,
    GDS_MC_NEW_MAKE_WAIT        = 22,
    GDS_MC_NEW_MAKE_SAVE_INIT   = 23,
    GDS_MC_NEW_MAKE_SAVE_WAIT   = 24,
    GDS_MC_FORMAT_CONF_INIT     = 25,
    GDS_MC_FORMAT_CONF_WAIT     = 26,
    GDS_MC_FORMAT_INIT          = 27,
    GDS_MC_FORMAT_WAIT          = 28,
    GDS_MC_FORMAT_END_INIT      = 29,
    GDS_MC_FORMAT_END_WAIT      = 30
};

/* GameDataSaveInit()'s argument. */
#define GAME_DATA_SAVE_EXE_NORMAL   0   /* ordinary save                    */
#define GAME_DATA_SAVE_EXE_CLEAR    1   /* records the game as cleared      */
#define GAME_DATA_SAVE_EXE_KEEP     2   /* leaves the clear flag as it is   */

/* The format-complete message is held for two seconds before the screen goes
 * on to make the directory. */
#define GAME_DATA_SAVE_FORMAT_END_TIME 60

/* types.txt.  Private to this file, the same way loadgame.c keeps its own
 * LOAD_GAME_CTRL.  char throughout -- every ROM access to the first six
 * fields is an `lb`. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mc_step;
    /* 0x2 */ char csr;
    /* 0x3 */ char conf_csr;
    /* 0x4 */ char save_exe_label;
    /* 0x5 */ char format_end_cnt;
    /* 0x8 */ int  save_file_cnt;
} GAME_DATA_SAVE_CTRL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  msg_id;
    /* 0x4 */ char anim_step;       /* Zero2Anim2D in/out step */
    /* 0x5 */ char anim_timer;
    /* 0x6 */ char csr_timer;       /* cursor pulse */
} GAME_DATA_SAVE_DISP;

static void *(*GameDataSaveMemGet)(int);                    /* sdata 3f0e78 */
static void (*GameDataSaveMemFree)(void *);                 /* sdata 3f0e7c */

static void *save_load_tex_addr;                            /* sdata 3f0e80 */
static void *outgame_cmn_tex_addr;                          /* sdata 3f0e84 */
static void *game_data_buff_addr;                           /* sdata 3f0e88 */
static void *game_data_save_snap_addr[GAME_DATA_SAVE_DATA_MAX];  /* data 3165c8 */

/* PORT DEVIATION, and the one real hazard in this file.
 *
 * All three tables below are the backing store of a reference_fixed_array, and
 * the ROM writes through every one of them: GameDataSaveInit() stores -1 into
 * the snap table, and the two save steps patch index 1 of their file list to
 * the chosen slot.  GCC put the initialiser images in game_data_save.o's
 * .rodata (3b3730 / 3b3798 / 3b38a8) and the EE has no write protection on
 * data pages, so nothing complained.  The host does, so these must be
 * writable here.  Same deviation loadgame.c already carries for its own
 * snap table. */
static int game_data_save_snap_tex_dat[GAME_DATA_SAVE_DATA_MAX] =   /* rodata 3b38a8 */
    { -1, -1, -1, -1, -1 };

/* File number of each slot's snapshot texture, bound at static-init time --
 * this object's single .ctors entry (0x2c3bb0). */
static reference_fixed_array<int, GAME_DATA_SAVE_DATA_MAX>
    game_data_save_snap_tex(game_data_save_snap_tex_dat);           /* sbss 3f4c78 */

static GAME_DATA_SAVE_CTRL game_data_save_ctrl;                     /* bss  4af678 */
static GAME_DATA_SAVE_DISP game_data_save_disp;                     /* sbss 3f4c80 */

static void GameDataSaveCtrlInit(char exe_label);
static void GetGameDataSaveDataMem(void **tex_addr, int size);
static int  GameDataSaveTexLoadWait(void);
static void GameDataSaveOutReq(void);
static void GameDataSaveMcMain(void);
static void GameDataSaveMcCheckInit(void);
static void GameDataSaveMcCheckWait(void);
static void GameDataSaveMcHeadLoadInit(void);
static void GameDataSaveMcHeadLoadWait(void);
static void GameDataSaveMcSnapLoadInit(void);
static void GameDataSaveMcSnapLoadWait(void);
static void GameDataSaveMcSaveFileSel(void);
static void GameDataSaveMcSaveFileSelPad(void);
static void GameDataSaveMcSaveConf(void);
static void GameDataSaveMcSaveConfPad(void);
static void GameDataSaveMcSystemLoadInit(void);
static void GameDataSaveMcSystemLoadWait(void);
static void GameDataSaveMcSaveInit(void);
static void GameDataSaveMcSaveWait(void);
static void GameDataSaveMcErrorConfInit(void);
static void GameDataSaveMcErrorConfWait(void);
static void GameDataSaveMcErrorConfPad(void);
static void GameDataSaveMcEndConf(void);
static void GameDataSaveMcRemakeConfInit(void);
static void GameDataSaveMcRemakeConfWait(void);
static void GameDataSaveMcRemakeConfPad(void);
static void GameDataSaveMcRemakeDirDelInit(void);
static void GameDataSaveMcRemakeDirDelWait(void);
static void GameDataSaveMcNewMakeConfInit(void);
static void GameDataSaveMcNewMakeConfWait(void);
static void GameDataSaveMcNewMakeConfPad(void);
static void GameDataSaveMcNewMakeInit(void);
static void GameDataSaveMcNewMakeWait(void);
static void GameDataSaveMcNewMakeSaveInit(void);
static void GameDataSaveMcNewMakeSaveWait(void);
static void GameDataSaveMcFormatConfInit(void);
static void GameDataSaveMcFormatConfWait(void);
static void GameDataSaveMcFormatConfPad(void);
static void GameDataSaveMcFormatInit(void);
static void GameDataSaveMcFormatWait(void);
static void GameDataSaveMcFormatEndInit(void);
static void GameDataSaveMcFormatEndWait(void);
static void GameDataSaveMcEveryFrameCheck(void);
static void LiberateGameDataSaveMem(void **tex_addr);
static void GameDataSaveDispInit(void);
static void GameData_SaveFileSelDisp(u_char alpha);

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

void GameDataSaveInit(char exe_label)                                   /* 240 */
{
    int i;

    GameDataSaveCtrlInit(exe_label);                                    /* 246 */

    MemoryCardExeInit();                                                /* 249 */

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 252 */

    for (i = 0; i < GAME_DATA_SAVE_DATA_MAX; i++) {                     /* 253 */
        LiberateGameDataSaveMem(&game_data_save_snap_addr[i]);          /* 254 */
        game_data_save_snap_tex[i] = -1;                                /* 257 */
    }

    /* The screen's own idea of "this save counts as a clear" is mirrored into
     * ingame_wrk so the save blocks pick it up; exe_label 2 leaves whatever
     * is already there. */
    if (game_data_save_ctrl.save_exe_label == GAME_DATA_SAVE_EXE_NORMAL) { /* 259 */
        ingame_wrk.clear_save_flg = 0;                                  /* 261 */
    }
    else if (game_data_save_ctrl.save_exe_label == GAME_DATA_SAVE_EXE_CLEAR) { /* 262 */
        ingame_wrk.clear_save_flg = game_data_save_ctrl.save_exe_label; /* 264 */
    }
}

static void GameDataSaveCtrlInit(char exe_label)                        /* 276 */
{
    game_data_save_ctrl.step           = GAME_DATA_SAVE_DISP_INIT;      /* 279 */
    game_data_save_ctrl.mc_step        = GDS_MC_CHECK_INIT;             /* 280 */
    game_data_save_ctrl.csr            = 0;                             /* 281 */
    game_data_save_ctrl.conf_csr       = 1;         /* default "no" */  /* 282 */
    game_data_save_ctrl.save_exe_label = exe_label;                     /* 283 */
    game_data_save_ctrl.save_file_cnt  = 0;                             /* 284 */
    game_data_save_ctrl.format_end_cnt = 0;                             /* 285 */
}

/* ==========================================================================
 *  Assets
 *
 *  Both paks are claimed through the caller's allocator, which is what lets
 *  the same screen serve the outgame heap (setup.c) and the ingame one
 *  (savepoint.c).  The pair is one-shot: the assert fires if a second caller
 *  requests without the first having freed.
 * ======================================================================== */

void GameDataSaveBackGroundLoadReq(void *(*mem_get)(int), void (*mem_free)(void *)) /* 295 */
{
    if ((GameDataSaveMemGet != nullptr) || (GameDataSaveMemFree != nullptr)) { /* 298 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 299 */
    }
    else {
        GameDataSaveMemGet  = mem_get;                                  /* 300 */
        GameDataSaveMemFree = mem_free;
    }

    GetGameDataSaveDataMem(&save_load_tex_addr,
                           (int)GetFileSize(SAVE_LOAD_PK2 + GetLanguage())); /* 303 */
    GetGameDataSaveDataMem(&outgame_cmn_tex_addr,
                           (int)GetFileSize(OUTGAME_PK2));          /* 308 */

    FileLoadReqEE(SAVE_LOAD_PK2 + GetLanguage(), save_load_tex_addr, 6,
                  nullptr, nullptr);                                    /* 309 */
    FileLoadReqEE(OUTGAME_PK2, outgame_cmn_tex_addr, 6,
                  nullptr, nullptr);                                    /* 312 */
}

static void GetGameDataSaveDataMem(void **tex_addr, int size)           /* 323 */
{
    if (*tex_addr != nullptr) {                                         /* 326 */
        LiberateGameDataSaveMem(tex_addr);                              /* 327 */
    }

    *tex_addr = GameDataSaveMemGet(size);                               /* 331 */
}

static int GameDataSaveTexLoadWait(void)                                /* 340 */
{
    int res;

    res = 0;                                                            /* 348 */

    if (FileLoadIsEnd2(SAVE_LOAD_PK2 + GetLanguage(), save_load_tex_addr) != 0) { /* 349 */
        res = (FileLoadIsEnd2(OUTGAME_PK2, outgame_cmn_tex_addr) != 0);
    }

    return res;                                                         /* 355 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Returns non-zero once the screen has animated out.  Note that the card
 * machine is gated on the window being fully open (anim_step 2), so no card
 * traffic starts until the screen is actually visible. */
int GameDataSaveMain(void)                                              /* 367 */
{
    int res;

    res = 0;                                                            /* 374 */

    switch (game_data_save_ctrl.step) {                                 /* 377 */
    case GAME_DATA_SAVE_DISP_INIT:
        GameDataSaveDispInit();                                         /* 380 */
        game_data_save_ctrl.step = GAME_DATA_SAVE_LOAD_WAIT;            /* 382 */
        break;

    case GAME_DATA_SAVE_LOAD_WAIT:
        if (GameDataSaveTexLoadWait() != 0) {                           /* 386 */
            game_data_save_ctrl.step = GAME_DATA_SAVE_MC_EXE;           /* 388 */
            MemoryCardSetAccessPort(0);                                 /* 390 */
        }
        break;

    case GAME_DATA_SAVE_MC_EXE:
        if (game_data_save_disp.anim_step == ZERO2_ANIM2D_STEP_SHOW) {  /* 392 */
            GameDataSaveMcMain();                                       /* 394 */
        }
        break;

    case GAME_DATA_SAVE_OUT:
        if (game_data_save_disp.anim_step == ZERO2_ANIM2D_STEP_END) {   /* 396 */
            res = 1;                                                    /* 399 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 401 */
        break;
    }

    return res;                                                         /* 405 */
}

/* Every way out of the screen goes through here. */
static void GameDataSaveOutReq(void)                                    /* 411 */
{
    game_data_save_ctrl.step      = GAME_DATA_SAVE_OUT;                 /* 414 */
    game_data_save_disp.anim_step = ZERO2_ANIM2D_STEP_OUT;              /* 415 */
    game_data_save_disp.anim_timer = 0;                                 /* 416 */
}

/* The card machine.  Every Init case falls through into its Wait case, so a
 * request is issued and polled on the same frame -- that fall-through is the
 * ROM's own and is why there are 31 states rather than 31 frames of latency.
 * The six states with no Init half (file-select, both confirms, end-confirm)
 * are the ones that wait on the player instead of the card. */
static void GameDataSaveMcMain(void)                                    /* 424 */
{
    switch (game_data_save_ctrl.mc_step) {                              /* 427 */
    case GDS_MC_CHECK_INIT:          GameDataSaveMcCheckInit();         /* 429 */
        /* fall through */
    case GDS_MC_CHECK_WAIT:          GameDataSaveMcCheckWait();         /* 432 */
        break;
    case GDS_MC_HEAD_LOAD_INIT:      GameDataSaveMcHeadLoadInit();      /* 435 */
        /* fall through */
    case GDS_MC_HEAD_LOAD_WAIT:      GameDataSaveMcHeadLoadWait();      /* 438 */
        break;
    case GDS_MC_SNAP_LOAD_INIT:      GameDataSaveMcSnapLoadInit();      /* 441 */
        /* fall through */
    case GDS_MC_SNAP_LOAD_WAIT:      GameDataSaveMcSnapLoadWait();      /* 444 */
        break;
    case GDS_MC_SAVE_FILE_SEL:       GameDataSaveMcSaveFileSel();       /* 447 */
        break;
    case GDS_MC_SAVE_CONF:           GameDataSaveMcSaveConf();          /* 450 */
        break;
    case GDS_MC_SYSTEM_LOAD_INIT:    GameDataSaveMcSystemLoadInit();    /* 453 */
        /* fall through */
    case GDS_MC_SYSTEM_LOAD_WAIT:    GameDataSaveMcSystemLoadWait();    /* 456 */
        break;
    case GDS_MC_SAVE_INIT:           GameDataSaveMcSaveInit();          /* 459 */
        /* fall through */
    case GDS_MC_SAVE_WAIT:           GameDataSaveMcSaveWait();          /* 462 */
        break;
    case GDS_MC_ERROR_CONF_INIT:     GameDataSaveMcErrorConfInit();     /* 465 */
        /* fall through */
    case GDS_MC_ERROR_CONF_WAIT:     GameDataSaveMcErrorConfWait();     /* 468 */
        break;
    case GDS_MC_END_CONF:            GameDataSaveMcEndConf();           /* 471 */
        break;
    case GDS_MC_REMAKE_CONF_INIT:    GameDataSaveMcRemakeConfInit();    /* 474 */
        /* fall through */
    case GDS_MC_REMAKE_CONF_WAIT:    GameDataSaveMcRemakeConfWait();    /* 477 */
        break;
    case GDS_MC_REMAKE_DIR_DEL_INIT: GameDataSaveMcRemakeDirDelInit();  /* 480 */
        /* fall through */
    case GDS_MC_REMAKE_DIR_DEL_WAIT: GameDataSaveMcRemakeDirDelWait();  /* 483 */
        break;
    case GDS_MC_NEW_MAKE_CONF_INIT:  GameDataSaveMcNewMakeConfInit();   /* 486 */
        /* fall through */
    case GDS_MC_NEW_MAKE_CONF_WAIT:  GameDataSaveMcNewMakeConfWait();   /* 489 */
        break;
    case GDS_MC_NEW_MAKE_INIT:       GameDataSaveMcNewMakeInit();       /* 492 */
        /* fall through */
    case GDS_MC_NEW_MAKE_WAIT:       GameDataSaveMcNewMakeWait();       /* 495 */
        break;
    case GDS_MC_NEW_MAKE_SAVE_INIT:  GameDataSaveMcNewMakeSaveInit();   /* 498 */
        /* fall through */
    case GDS_MC_NEW_MAKE_SAVE_WAIT:  GameDataSaveMcNewMakeSaveWait();   /* 501 */
        break;
    case GDS_MC_FORMAT_CONF_INIT:    GameDataSaveMcFormatConfInit();    /* 504 */
        /* fall through */
    case GDS_MC_FORMAT_CONF_WAIT:    GameDataSaveMcFormatConfWait();    /* 507 */
        break;
    case GDS_MC_FORMAT_INIT:         GameDataSaveMcFormatInit();        /* 510 */
        /* fall through */
    case GDS_MC_FORMAT_WAIT:         GameDataSaveMcFormatWait();        /* 513 */
        break;
    case GDS_MC_FORMAT_END_INIT:     GameDataSaveMcFormatEndInit();     /* 517 */
        /* fall through */
    case GDS_MC_FORMAT_END_WAIT:     GameDataSaveMcFormatEndWait();     /* 521 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 524 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Card check
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcCheckInit(void)                               /* 533 */
{
    char path_name[GAME_DATA_SAVE_PATH_NAME_LEN] = "";                  /* 537 */

    MemoryCardMakeSearchDirPath(path_name, 0);                          /* 541 */
    MemoryCardCheckInit(0, 0, path_name);                               /* 543 */

    game_data_save_ctrl.save_file_cnt = 0;                              /* 545 */
    game_data_save_ctrl.mc_step       = GDS_MC_CHECK_WAIT;              /* 546 */
}

/* Where the screen decides what kind of card it is looking at.  Note that a
 * missing directory (-4) and a broken one (-6) are recoverable -- they lead to
 * the make/remake prompts -- while everything else falls through to the error
 * confirm with a message id. */
static void GameDataSaveMcCheckWait(void)                               /* 554 */
{
    int mc_res;
    int msg_id;

    mc_res = MemoryCardCheckMain();                                     /* 560 */

    if (mc_res == 1) {                                                  /* 563 */
        if (MemoryCardCheckDirBroken(0) != 0) {                         /* 565 */
            game_data_save_disp.msg_id = 0;
            game_data_save_ctrl.mc_step = GDS_MC_HEAD_LOAD_INIT;
            return;
        }

        if (MemoryCardCheckEmptyBroken(0) != 0) {                       /* 573 */
            game_data_save_disp.msg_id   = 0x16;                        /* 574 */
            game_data_save_ctrl.conf_csr = 1;
            game_data_save_ctrl.mc_step  = GDS_MC_REMAKE_CONF_INIT;     /* 576 */
            return;
        }

        msg_id = 0x19;                                                  /* 580 */
    }
    else if (mc_res < 0) {                                              /* 585 */
        msg_id = 2;

        switch (mc_res) {                                               /* 587 */
        case -1:        /* card swapped */
            game_data_save_ctrl.mc_step = GDS_MC_HEAD_LOAD_INIT;        /* 590 */
            game_data_save_disp.msg_id  = 0;                            /* 591 */
            return;

        case -2:        /* not formatted */
            game_data_save_disp.msg_id   = 3;                           /* 593 */
            game_data_save_ctrl.conf_csr = 1;                           /* 594 */
            game_data_save_ctrl.mc_step  = GDS_MC_FORMAT_CONF_INIT;     /* 596 */
            return;

        case -4:        /* no save directory */
            if (MemoryCardCheckEmpty(0) != 0) {                         /* 599 */
                game_data_save_disp.msg_id  = 0x15;                     /* 601 */
                game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_CONF_INIT; /* 602 */
                return;
            }
            msg_id = 0x17;                                              /* 609 */
            break;

        case -6:        /* directory there but unusable */
            if (MemoryCardCheckEmptyBroken(0) != 0) {                   /* 612 */
                game_data_save_disp.msg_id   = 0x16;                    /* 613 */
                game_data_save_ctrl.conf_csr = 1;                       /* 614 */
                game_data_save_ctrl.mc_step  = GDS_MC_REMAKE_CONF_INIT; /* 615 */
                return;
            }
            msg_id = 0x19;                                              /* 621 */
            break;

        case -0x14:     /* rejected four times, gave up */
            msg_id = 1;                                                 /* 625 */
            break;

        default:
            msg_id = 2;                                                 /* 629 */
            break;
        }
    }
    else {
        return;
    }

    game_data_save_disp.msg_id  = msg_id;                               /* 629 */
    game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;               /* 630 */
}

/* --------------------------------------------------------------------------
 *  Play-data header
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcHeadLoadInit(void)                            /* 640 */
{
    int  size;
    char path_name[GAME_DATA_SAVE_PATH_NAME_LEN] = "";                  /* 646 */

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 649 */

    MemoryCardSetFilePath(path_name, 0, GAME_DATA_SAVE_MC_FILE_HEAD);   /* 653 */
    size = GetMemoryCardDataSize(0, GAME_DATA_SAVE_MC_FILE_HEAD);       /* 655 */

    GetGameDataSaveDataMem(&game_data_buff_addr, size);                 /* 657 */

    MemoryCardFileLoadInit(0, 0, path_name, game_data_buff_addr, size); /* 659 */

    game_data_save_disp.msg_id  = 0;                                    /* 662 */
    game_data_save_ctrl.mc_step = GDS_MC_HEAD_LOAD_WAIT;                /* 664 */
}

/* A header that reads as "never saved to" still opens the file-select -- the
 * screen is a *save*, so an empty card is a normal thing to be looking at.
 * The scan for a used slot only decides whether the snapshots are worth
 * loading. */
static void GameDataSaveMcHeadLoadWait(void)                            /* 672 */
{
    int mc_res;
    int size;
    int i;

    mc_res = MemoryCardFileLoadMain();                                  /* 682 */

    if (mc_res == 1) {                                                  /* 685 */
        size = GetMemoryCardDataSize(0, GAME_DATA_SAVE_MC_FILE_HEAD);   /* 687 */

        if (MemoryCardCheckFileBroken(game_data_buff_addr, size) != 0) { /* 690 */
            if (MemoryCardCheckNewFileLoad(game_data_buff_addr, size) == 0) { /* 692 */
                DevelopMemoryCardLoadData((char *)game_data_buff_addr, 0,
                                          GAME_DATA_SAVE_MC_FILE_HEAD); /* 695 */

                game_data_save_ctrl.mc_step = GDS_MC_SAVE_FILE_SEL;     /* 700 */
                game_data_save_disp.msg_id  = 10;                       /* 703 */

                for (i = 0; i < GAME_DATA_SAVE_DATA_MAX; i++) {         /* 704 */
                    if (GetMemoryCardPlayDataFlg(i) == 1) {             /* 707 */
                        game_data_save_ctrl.mc_step = GDS_MC_SNAP_LOAD_INIT; /* 708 */
                        game_data_save_disp.msg_id  = 0;                /* 710 */
                        break;                                          /* 711 */
                    }
                }
            }
            else {
                game_data_save_ctrl.mc_step = GDS_MC_SAVE_FILE_SEL;     /* 719 */
                game_data_save_disp.msg_id  = 10;
            }
        }
        else if (MemoryCardCheckEmptyBroken(0) != 0) {                  /* 726 */
            game_data_save_disp.msg_id   = 0x16;                        /* 731 */
            game_data_save_ctrl.conf_csr = 1;
            game_data_save_ctrl.mc_step  = GDS_MC_REMAKE_CONF_INIT;     /* 733 */
        }
        else {
            game_data_save_disp.msg_id  = 0x19;                         /* 735 */
            game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;       /* 736 */
        }
    }
    else if (mc_res < 0) {                                              /* 738 */
        switch (mc_res) {                                               /* 741 */
        case -2:        /* not formatted */
            game_data_save_disp.msg_id   = 3;                           /* 742 */
            game_data_save_ctrl.conf_csr = 1;                           /* 743 */
            game_data_save_ctrl.mc_step  = GDS_MC_FORMAT_CONF_INIT;     /* 744 */
            break;

        case -3:        /* short read: the file is corrupt */
            if (MemoryCardCheckEmptyBroken(0) != 0) {                   /* 756 */
                game_data_save_disp.msg_id   = 0x16;
                game_data_save_ctrl.conf_csr = 1;
                game_data_save_ctrl.mc_step  = GDS_MC_REMAKE_CONF_INIT; /* 757 */
            }
            else {
                game_data_save_disp.msg_id  = 0x19;
                game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;
            }
            break;

        default:
            game_data_save_disp.msg_id  = 2;                            /* 763 */
            game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;       /* 764 */
            break;
        }
    }

    /* Any settled result -- success or failure -- releases the buffer; only a
     * still-running load (0) keeps it. */
    if (mc_res != 0) {
        LiberateGameDataSaveMem(&game_data_buff_addr);
    }
}

/* --------------------------------------------------------------------------
 *  Slot snapshots
 * ------------------------------------------------------------------------ */

/* One texture per used slot: the room's own snapshot for a game in progress,
 * or the fixed "cleared" plate (SNP_CLEAR_DATA_PK2) for a finished one.  A
 * slot whose room is not in room_info_dat gets neither -- its snap label stays
 * at whatever it was and no load is posted, which is what the null check in
 * GameData_SaveFileSelDisp() covers. */
static void GameDataSaveMcSnapLoadInit(void)                            /* 771 */
{
    int data_num;
    int i;

    for (data_num = 0; data_num < GAME_DATA_SAVE_DATA_MAX; data_num++) { /* 777 */
        if (GetMemoryCardPlayDataFlg(data_num) == 1) {                  /* 778 */
            LiberateGameDataSaveMem(&game_data_save_snap_addr[data_num]); /* 780 */

            if (GetMemoryCardClearDataFlg(data_num) == 0) {             /* 783 */
                for (i = 0; room_info_dat[i].map_label != -1; i++) {    /* 794 */
                    if (room_info_dat[i].room_label ==
                        GetMemoryCardPlayDataRoomLabel(data_num)) {     /* 795 */
                        game_data_save_snap_tex[data_num] =
                            room_info_dat[i].snap_tex_label;            /* 799 */
                        break;
                    }
                }
            }
            else {
                game_data_save_snap_tex[data_num] = SNP_CLEAR_DATA_PK2; /* 806 */
            }

            GetGameDataSaveDataMem(&game_data_save_snap_addr[data_num],
                                   (int)GetFileSize(game_data_save_snap_tex[data_num])); /* 813 */
            FileLoadReqEE(game_data_save_snap_tex[data_num],
                          game_data_save_snap_addr[data_num], 6, nullptr, nullptr); /* 816 */
        }
        else {
            LiberateGameDataSaveMem(&game_data_save_snap_addr[data_num]); /* 819 */
            game_data_save_snap_tex[data_num] = -1;                     /* 821 */
        }
    }

    MemoryCardCheckEveryFrameInit(0, 0);                                /* 822 */
    game_data_save_disp.msg_id  = 0;
    game_data_save_ctrl.mc_step = GDS_MC_SNAP_LOAD_WAIT;
}

/* Parks the cursor on the first used slot on the way through -- csr_set_flg is
 * what stops a later slot overwriting that choice. */
static void GameDataSaveMcSnapLoadWait(void)                            /* 830 */
{
    int i;
    int csr_set_flg;

    csr_set_flg = 0;                                                    /* 835 */

    for (i = 0; i < GAME_DATA_SAVE_DATA_MAX; i++) {                     /* 838 */
        if (GetMemoryCardPlayDataFlg(i) == 1) {                         /* 840 */
            if (FileLoadIsEnd2(game_data_save_snap_tex[i],
                               game_data_save_snap_addr[i]) == 0) {     /* 843 */
                game_data_save_ctrl.mc_step = GDS_MC_SNAP_LOAD_WAIT;    /* 844 */
                game_data_save_disp.msg_id  = 0;
                break;
            }

            game_data_save_ctrl.mc_step = GDS_MC_SAVE_FILE_SEL;         /* 850 */
            game_data_save_disp.msg_id  = 10;                           /* 854 */

            if (csr_set_flg == 0) {                                     /* 855 */
                game_data_save_ctrl.csr = (char)i;                      /* 856 */
                csr_set_flg = 1;                                        /* 859 */
            }
        }
    }

    GameDataSaveMcEveryFrameCheck();                                    /* 862 */
}

/* --------------------------------------------------------------------------
 *  Slot select
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcSaveFileSel(void)                             /* 870 */
{
    GameDataSaveMcSaveFileSelPad();                                     /* 874 */
    GameDataSaveMcEveryFrameCheck();                                    /* 877 */
}

/* The cursor's range is "used slots + 1", clamped to five -- so the player can
 * overwrite any existing save or start one new one, but cannot skip past the
 * first empty slot to a later empty one. */
static void GameDataSaveMcSaveFileSelPad(void)                          /* 885 */
{
    int i;
    int csr_range;

    csr_range = 0;                                                      /* 890 */

    for (i = 0; i < GAME_DATA_SAVE_DATA_MAX; i++) {                     /* 892 */
        if (GetMemoryCardPlayDataFlg(i) == 1) {                         /* 894 */
            csr_range++;
        }
    }                                                                   /* 897 */

    csr_range++;                                                        /* 898 */
    if (csr_range > GAME_DATA_SAVE_DATA_MAX) {                          /* 900 */
        csr_range = GAME_DATA_SAVE_DATA_MAX;
    }

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {                  /* 906 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 907 */
        game_data_save_ctrl.csr =
            (char)((game_data_save_ctrl.csr + csr_range - 1) % csr_range); /* 908 */
    }
    else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {             /* 911 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 912 */
        game_data_save_ctrl.csr =
            (char)((game_data_save_ctrl.csr + 1) % csr_range);          /* 913 */
    }
    else if (*paddat[0] == 1) {                                         /* 916 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 917 */

        game_data_save_ctrl.conf_csr = 1;                               /* 919 */

        /* Overwriting an existing save gets a different prompt from filling
         * an empty slot. */
        game_data_save_disp.msg_id = 0xb;                               /* 922 */
        if (GetMemoryCardPlayDataFlg((int)game_data_save_ctrl.csr) == 0) { /* 928 */
            game_data_save_disp.msg_id = 0x1f;                          /* 929 */
        }

        game_data_save_ctrl.mc_step = GDS_MC_SAVE_CONF;                 /* 933 */
    }
    else if (*paddat[1] == 1) {                                         /* 934 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 936 */
        GameDataSaveOutReq();
    }
}

static void GameDataSaveMcSaveConf(void)                                /* 945 */
{
    GameDataSaveMcSaveConfPad();                                        /* 949 */
    GameDataSaveMcEveryFrameCheck();                                    /* 952 */
}

static void GameDataSaveMcSaveConfPad(void)                             /* 959 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 963 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 984 */
        game_data_save_ctrl.conf_csr ^= 1;                              /* 988 */
    }
    else if (*paddat[0] == 1) {                                         /* 968 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 969 */

        if (game_data_save_ctrl.conf_csr == 0) {                        /* 970 */
            /* "Yes": the system file is read back first so the card's clear
             * flags can be merged into the live ones before anything is
             * written over them. */
            game_data_save_ctrl.mc_step       = GDS_MC_SYSTEM_LOAD_INIT; /* 973 */
            game_data_save_disp.msg_id        = 0x1e;                   /* 974 */
            game_data_save_ctrl.save_file_cnt = 0;                      /* 976 */
        }
        else {
            game_data_save_ctrl.mc_step = GDS_MC_SAVE_FILE_SEL;         /* 977 */
            game_data_save_disp.msg_id  = 10;                           /* 979 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 989 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 990 */
        game_data_save_ctrl.mc_step = GDS_MC_SAVE_FILE_SEL;             /* 991 */
        game_data_save_disp.msg_id  = 10;
    }
}

/* --------------------------------------------------------------------------
 *  System file: read, merge, then save
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcSystemLoadInit(void)                          /* 1000 */
{
    int  size;
    char path_name[GAME_DATA_SAVE_PATH_NAME_LEN] = "";                  /* 1006 */

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 1009 */

    MemoryCardSetFilePath(path_name, 0, GAME_DATA_SAVE_MC_FILE_SYSTEM); /* 1013 */
    size = GetMemoryCardDataSize(0, GAME_DATA_SAVE_MC_FILE_SYSTEM);     /* 1015 */

    GetGameDataSaveDataMem(&game_data_buff_addr, size);                 /* 1017 */

    MemoryCardFileLoadInit(0, 0, path_name, game_data_buff_addr, size); /* 1019 */

    game_data_save_ctrl.mc_step = GDS_MC_SYSTEM_LOAD_WAIT;              /* 1021 */
    game_data_save_disp.msg_id  = 0x1e;                                 /* 1022 */
}

/* The merge.  CLEAR_FLG_CTRL is the first thing in the system file, so the
 * copy is a straight 0x18-byte read off the head of the buffer; the result of
 * merging it with the running clear_flg_ctrl becomes the running one, and that
 * is what SetMemoryCardSaveDataToBuff() will write back out.  A file that has
 * never been saved to is skipped -- there is nothing in it to merge. */
static void GameDataSaveMcSystemLoadWait(void)                          /* 1030 */
{
    int            mc_res;
    int            size;
    CLEAR_FLG_CTRL buff_flg_ctrl;

    memset(&buff_flg_ctrl, 0, sizeof(CLEAR_FLG_CTRL));                  /* 1039 */

    mc_res = MemoryCardFileLoadMain();                                  /* 1042 */

    if (mc_res == 1) {                                                  /* 1045 */
        size = GetMemoryCardDataSize(0, GAME_DATA_SAVE_MC_FILE_SYSTEM); /* 1047 */

        if (MemoryCardCheckFileBroken(game_data_buff_addr, size) == 0) { /* 1050 */
            game_data_save_disp.msg_id  = 0x19;                         /* 1052 */
            game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;       /* 1055 */
        }
        else {
            if (MemoryCardCheckNewFileLoad(game_data_buff_addr, size) == 0) { /* 1057 */
                buff_flg_ctrl = *(CLEAR_FLG_CTRL *)game_data_buff_addr; /* 1061 */
                SetClearFlgCtrl(ClearFlgMerging(buff_flg_ctrl, clear_flg_ctrl)); /* 1064 */
            }

            game_data_save_ctrl.mc_step       = GDS_MC_SAVE_INIT;       /* 1069 */
            game_data_save_ctrl.save_file_cnt = 0;                      /* 1070 */
            game_data_save_disp.msg_id        = 0x1e;                   /* 1071 */
        }
    }
    else if (mc_res < 0) {                                              /* 1077 */
        game_data_save_disp.msg_id = 7;                                 /* 1078 */

        switch (mc_res) {                                               /* 1082 */
        case -3:        /* short read */
            game_data_save_disp.msg_id = 0x19;                          /* 1084 */
            break;
        case -2:        /* not formatted */
            game_data_save_disp.msg_id = 0xd;                           /* 1088 */
            break;
        default:
            game_data_save_disp.msg_id = 7;                             /* 1091 */
            break;
        }

        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1097 */
    }

    if (mc_res != 0) {                                                  /* 1100 */
        LiberateGameDataSaveMem(&game_data_buff_addr);                  /* 1105 */
    }
}

/* One file per visit, three visits: SaveWait sends control back here until
 * save_file_cnt reaches three.  Index 1 of the list is patched to the chosen
 * slot before the first write, so the order on the card is system, slot,
 * header. */
static void GameDataSaveMcSaveInit(void)                                /* 1113 */
{
    int  size;
    char path_name[GAME_DATA_SAVE_PATH_NAME_LEN] = "";                  /* 1120 */

    static int save_file_label_dat[GAME_DATA_SAVE_FILE_NUM] =           /* rodata 3b3730 */
    {
        GAME_DATA_SAVE_MC_FILE_SYSTEM,      /* 0 -- patched last  */
        GAME_DATA_SAVE_MC_FILE_DATA,        /* 2 -- patched below */
        GAME_DATA_SAVE_MC_FILE_HEAD,        /* 1                  */
    };
    static reference_fixed_array<int, GAME_DATA_SAVE_FILE_NUM>
        save_file_label(save_file_label_dat);                           /* sbss 3f4c68 */

    save_file_label[1] = game_data_save_ctrl.csr + GAME_DATA_SAVE_MC_FILE_DATA; /* 1127 */

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 1133 */

    switch (game_data_save_ctrl.save_exe_label) {                       /* 1136 */
    case GAME_DATA_SAVE_EXE_NORMAL:
        ingame_wrk.clear_save_flg = 0;                                  /* 1138 */
        SetMemoryCardPlayDataHead((int)game_data_save_ctrl.csr, 0);     /* 1139 */
        break;                                                          /* 1140 */

    case GAME_DATA_SAVE_EXE_CLEAR:
        ingame_wrk.clear_save_flg = game_data_save_ctrl.save_exe_label; /* 1142 */
        SetMemoryCardPlayDataHead((int)game_data_save_ctrl.csr,
                                  game_data_save_ctrl.save_exe_label);  /* 1143 */
        break;                                                          /* 1144 */

    case GAME_DATA_SAVE_EXE_KEEP:
        SetMemoryCardPlayDataHead((int)game_data_save_ctrl.csr,
                                  ingame_wrk.clear_save_flg);           /* 1146 */
        break;                                                          /* 1147 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1149 */
        break;
    }

    MemoryCardSetFilePath(path_name, 0,
                          save_file_label[game_data_save_ctrl.save_file_cnt]); /* 1158 */
    size = GetMemoryCardDataSize(0,
                          save_file_label[game_data_save_ctrl.save_file_cnt]); /* 1160 */

    GetGameDataSaveDataMem(&game_data_buff_addr, size);                 /* 1162 */

    SetMemoryCardSaveDataToBuff((char *)game_data_buff_addr, 0,
                          save_file_label[game_data_save_ctrl.save_file_cnt]); /* 1164 */

    MemoryCardFileSaveInit(0, 0, path_name, game_data_buff_addr, size); /* 1166 */

    game_data_save_disp.msg_id = 0x1e;                                  /* 1168 */
    game_data_save_ctrl.save_file_cnt++;                                /* 1169 */
    game_data_save_ctrl.mc_step = GDS_MC_SAVE_WAIT;                     /* 1170 */

    if (size < 1) {                                                     /* 1174 */
        PRINT_ASSERT("Error! %s size %d\n", __FUNCTION__, size);        /* 1175 */
    }

    if (game_data_save_ctrl.save_file_cnt > GAME_DATA_SAVE_FILE_NUM) {  /* 1177 */
        /* ROM BUG, reproduced: the format takes two arguments but only
         * save_file_cnt is passed, so %s prints whatever is in a1. */
        PRINT_ASSERT("Error! %s save_file_cnt %d\n",
                     game_data_save_ctrl.save_file_cnt);                /* 1178 */
    }
}

static void GameDataSaveMcSaveWait(void)                                /* 1186 */
{
    int mc_res;

    mc_res = MemoryCardFileSaveMain();                                  /* 1193 */

    if (mc_res == 1) {                                                  /* 1196 */
        if (game_data_save_ctrl.save_file_cnt < GAME_DATA_SAVE_FILE_NUM) { /* 1198 */
            game_data_save_disp.msg_id  = 0x1e;                         /* 1199 */
            game_data_save_ctrl.mc_step = GDS_MC_SAVE_INIT;             /* 1200 */
        }
        else {
            game_data_save_disp.msg_id  = 8;                            /* 1204 */
            game_data_save_ctrl.mc_step = GDS_MC_END_CONF;              /* 1208 */
        }
    }
    else if (mc_res < 0) {                                              /* 1209 */
        game_data_save_disp.msg_id  = 7;                                /* 1210 */
        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1215 */
    }

    if (mc_res != 0) {                                                  /* 1217 */
        LiberateGameDataSaveMem(&game_data_buff_addr);
    }
}

/* --------------------------------------------------------------------------
 *  Error and end prompts
 * ------------------------------------------------------------------------ */

/* Drops every buffer before showing the error -- whatever went wrong, the
 * screen is not going to use them again. */
static void GameDataSaveMcErrorConfInit(void)                           /* 1225 */
{
    int i;

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 1231 */

    for (i = 0; i < GAME_DATA_SAVE_DATA_MAX; i++) {                     /* 1234 */
        LiberateGameDataSaveMem(&game_data_save_snap_addr[i]);          /* 1235 */
    }

    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1236 */

    game_data_save_ctrl.mc_step       = GDS_MC_ERROR_CONF_WAIT;         /* 1239 */
    game_data_save_ctrl.save_file_cnt = 0;                              /* 1241 */
}

/* A card pulled or swapped while the error is up restarts the whole machine
 * rather than compounding the error. */
static void GameDataSaveMcErrorConfWait(void)                           /* 1249 */
{
    int mc_res;

    GameDataSaveMcErrorConfPad();                                       /* 1257 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1260 */

    if ((mc_res == -1) || (mc_res == -2)) {                             /* 1263 */
        game_data_save_disp.msg_id  = 0;                                /* 1268 */
        game_data_save_ctrl.mc_step = GDS_MC_CHECK_INIT;                /* 1269 */
    }
    else if (mc_res == 1) {                                             /* 1270 */
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 1273 */
    }
    else if (mc_res < 0) {                                              /* 1274 */
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 1277 */
    }
}

/* Both buttons do the same thing -- there is nothing to choose. */
static void GameDataSaveMcErrorConfPad(void)                            /* 1287 */
{
    if (*paddat[0] == 1) {                                              /* 1291 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1292 */
        GameDataSaveOutReq();                                           /* 1294 */
    }
    else if (*paddat[1] == 1) {                                         /* 1297 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1298 */
        GameDataSaveOutReq();                                           /* 1300 */
    }
}

/* The save-complete acknowledgement.  Byte-for-byte the same shape as
 * ErrorConfPad, but it is its own function in the ROM. */
static void GameDataSaveMcEndConf(void)                                 /* 1309 */
{
    if (*paddat[0] == 1) {                                              /* 1313 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1314 */
        GameDataSaveOutReq();                                           /* 1316 */
    }
    else if (*paddat[1] == 1) {                                         /* 1319 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1320 */
        GameDataSaveOutReq();                                           /* 1322 */
    }
}

/* --------------------------------------------------------------------------
 *  Remake: the directory is there but unusable
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcRemakeConfInit(void)                          /* 1331 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1335 */

    game_data_save_ctrl.conf_csr = 1;                                   /* 1337 */
    game_data_save_ctrl.mc_step  = GDS_MC_REMAKE_CONF_WAIT;             /* 1338 */
    game_data_save_disp.msg_id   = 0x16;                                /* 1339 */
}

static void GameDataSaveMcRemakeConfWait(void)                          /* 1346 */
{
    GameDataSaveMcRemakeConfPad();                                      /* 1350 */
    GameDataSaveMcEveryFrameCheck();                                    /* 1353 */
}

static void GameDataSaveMcRemakeConfPad(void)                           /* 1360 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1364 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1492 */
        game_data_save_ctrl.conf_csr ^= 1;                              /* 1496 */
    }
    else if (*paddat[0] == 1) {                                         /* 1369 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1370 */

        if (game_data_save_ctrl.conf_csr == 0) {                        /* 1371 */
            game_data_save_ctrl.mc_step = GDS_MC_REMAKE_DIR_DEL_INIT;   /* 1374 */
            game_data_save_disp.msg_id  = 0x12;                         /* 1375 */
        }
        else {
            GameDataSaveOutReq();                                       /* 1378 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 1379 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1380 */
        GameDataSaveOutReq();                                           /* 1384 */
    }
}

static void GameDataSaveMcRemakeDirDelInit(void)                        /* 1400 */
{
    MemoryCardDirDelInit(0, 0, 0);                                      /* 1403 */

    game_data_save_disp.msg_id  = 0x12;                                 /* 1405 */
    game_data_save_ctrl.mc_step = GDS_MC_REMAKE_DIR_DEL_WAIT;           /* 1406 */
}

/* Deleting the old directory drops straight into making a new one -- the
 * player already agreed to both with one prompt. */
static void GameDataSaveMcRemakeDirDelWait(void)                        /* 1413 */
{
    int mc_res;

    mc_res = MemoryCardDirDelMain();                                    /* 1420 */

    if (mc_res == 1) {                                                  /* 1423 */
        game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_INIT;             /* 1424 */
        game_data_save_disp.msg_id  = 0x12;                             /* 1425 */
    }
    else if (mc_res < 0) {                                              /* 1428 */
        game_data_save_disp.msg_id  = 0x13;                             /* 1429 */
        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1430 */
    }
}

/* --------------------------------------------------------------------------
 *  New make: there is no directory at all
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcNewMakeConfInit(void)                         /* 1439 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1443 */

    game_data_save_ctrl.conf_csr = 1;                                   /* 1445 */
    game_data_save_ctrl.mc_step  = GDS_MC_NEW_MAKE_CONF_WAIT;           /* 1446 */
    game_data_save_disp.msg_id   = 0x15;                                /* 1447 */
}

static void GameDataSaveMcNewMakeConfWait(void)                         /* 1454 */
{
    GameDataSaveMcNewMakeConfPad();                                     /* 1458 */
    GameDataSaveMcEveryFrameCheck();                                    /* 1461 */
}

static void GameDataSaveMcNewMakeConfPad(void)                          /* 1468 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1472 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1492 */
        game_data_save_ctrl.conf_csr ^= 1;                              /* 1496 */
    }
    else if (*paddat[0] == 1) {                                         /* 1477 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1478 */

        if (game_data_save_ctrl.conf_csr == 0) {                        /* 1479 */
            game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_INIT;         /* 1482 */
            game_data_save_disp.msg_id  = 0x12;                         /* 1483 */
        }
        else {
            GameDataSaveOutReq();                                       /* 1486 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 1487 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1488 */
        GameDataSaveOutReq();                                           /* 1499 */
    }
}

static void GameDataSaveMcNewMakeInit(void)                             /* 1508 */
{
    int buff_size;

    buff_size = GetMemoryCardDirSize(0);                                /* 1512 */

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 1515 */
    GetGameDataSaveDataMem(&game_data_buff_addr, buff_size);            /* 1517 */

    MemoryCardNewMakeInit(0, 0, 0, game_data_buff_addr, buff_size);     /* 1520 */

    game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_WAIT;                 /* 1522 */
}

/* A freshly made directory has no play-data header worth reading, so the
 * head is reset here rather than loaded, and the save goes straight on. */
static void GameDataSaveMcNewMakeWait(void)                             /* 1529 */
{
    int mc_res;

    mc_res = MemoryCardNewMakeMain();                                   /* 1536 */

    if (mc_res == 1) {                                                  /* 1539 */
        MemoryCardPlayDataHeadInit();                                   /* 1541 */

        game_data_save_disp.msg_id        = 0x1e;                       /* 1543 */
        game_data_save_ctrl.mc_step       = GDS_MC_NEW_MAKE_SAVE_INIT;  /* 1544 */
        game_data_save_ctrl.save_file_cnt = 0;                          /* 1545 */
    }
    else if (mc_res < 0) {                                              /* 1548 */
        game_data_save_disp.msg_id  = 0x13;                             /* 1549 */
        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1550 */
    }

    if (mc_res != 0) {                                                  /* 1554 */
        LiberateGameDataSaveMem(&game_data_buff_addr);                  /* 1556 */
    }
}

/* GameDataSaveMcSaveInit's twin for a brand-new directory.  Two differences:
 * the header is written for slot 0 (there is nothing else on the card yet, so
 * the cursor is not consulted) and the file list is never patched -- index 1
 * stays at GAME_DATA_SAVE_MC_FILE_DATA, i.e. slot 0. */
static void GameDataSaveMcNewMakeSaveInit(void)                         /* 1563 */
{
    int  size;
    char path_name[GAME_DATA_SAVE_PATH_NAME_LEN] = "";                  /* 1570 */

    static int save_file_label_dat[GAME_DATA_SAVE_FILE_NUM] =           /* rodata 3b3798 */
    {
        GAME_DATA_SAVE_MC_FILE_SYSTEM,
        GAME_DATA_SAVE_MC_FILE_DATA,
        GAME_DATA_SAVE_MC_FILE_HEAD,
    };
    static reference_fixed_array<int, GAME_DATA_SAVE_FILE_NUM>
        save_file_label(save_file_label_dat);                           /* sbss 3f4c70 */

    switch (game_data_save_ctrl.save_exe_label) {                       /* 1577 */
    case GAME_DATA_SAVE_EXE_NORMAL:
        ingame_wrk.clear_save_flg = 0;                                  /* 1584 */
        SetMemoryCardPlayDataHead(0, 0);                                /* 1585 */
        break;                                                          /* 1586 */

    case GAME_DATA_SAVE_EXE_CLEAR:
        ingame_wrk.clear_save_flg = game_data_save_ctrl.save_exe_label; /* 1588 */
        SetMemoryCardPlayDataHead(0, game_data_save_ctrl.save_exe_label); /* 1589 */
        break;                                                          /* 1590 */

    case GAME_DATA_SAVE_EXE_KEEP:
        SetMemoryCardPlayDataHead(0, ingame_wrk.clear_save_flg);        /* 1592 */
        break;                                                          /* 1593 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1595 */
        break;
    }

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 1602 */

    MemoryCardSetFilePath(path_name, 0,
                          save_file_label[game_data_save_ctrl.save_file_cnt]); /* 1606 */
    size = GetMemoryCardDataSize(0,
                          save_file_label[game_data_save_ctrl.save_file_cnt]); /* 1608 */

    GetGameDataSaveDataMem(&game_data_buff_addr, size);                 /* 1610 */

    SetMemoryCardSaveDataToBuff((char *)game_data_buff_addr, 0,
                          save_file_label[game_data_save_ctrl.save_file_cnt]); /* 1612 */

    MemoryCardFileSaveInit(0, 0, path_name, game_data_buff_addr, size); /* 1614 */

    game_data_save_ctrl.save_file_cnt++;                                /* 1616 */
    game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_SAVE_WAIT;            /* 1617 */

    if (size < 1) {                                                     /* 1621 */
        PRINT_ASSERT("Error! %s size %d\n", __FUNCTION__, size);        /* 1622 */
    }

    if (game_data_save_ctrl.save_file_cnt > GAME_DATA_SAVE_FILE_NUM) {  /* 1624 */
        /* Same one-argument-short format as its twin. */
        PRINT_ASSERT("Error! %s save_file_cnt %d\n",
                     game_data_save_ctrl.save_file_cnt);                /* 1625 */
    }
}

static void GameDataSaveMcNewMakeSaveWait(void)                         /* 1633 */
{
    int mc_res;

    mc_res = MemoryCardFileSaveMain();                                  /* 1640 */

    if (mc_res == 1) {                                                  /* 1643 */
        if (game_data_save_ctrl.save_file_cnt < GAME_DATA_SAVE_FILE_NUM) { /* 1645 */
            game_data_save_disp.msg_id  = 0x1e;                         /* 1646 */
            game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_SAVE_INIT;    /* 1647 */
        }
        else {
            game_data_save_disp.msg_id  = 0x14;                         /* 1651 */
            game_data_save_ctrl.mc_step = GDS_MC_END_CONF;              /* 1655 */
        }
    }
    else if (mc_res < 0) {                                              /* 1656 */
        game_data_save_disp.msg_id  = 7;                                /* 1657 */
        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1662 */
    }

    if (mc_res != 0) {                                                  /* 1663 */
        LiberateGameDataSaveMem(&game_data_buff_addr);
    }
}

/* --------------------------------------------------------------------------
 *  Format: the card itself is unusable
 * ------------------------------------------------------------------------ */

static void GameDataSaveMcFormatConfInit(void)                          /* 1671 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1675 */

    game_data_save_ctrl.conf_csr = 1;                                   /* 1677 */
    game_data_save_ctrl.mc_step  = GDS_MC_FORMAT_CONF_WAIT;             /* 1678 */
    game_data_save_disp.msg_id   = 3;                                   /* 1679 */
}

/* Unlike the other confirm waits this one does not use
 * GameDataSaveMcEveryFrameCheck(): an unformatted card reports -2 every frame,
 * and routing that through the common check would bounce the player into the
 * error screen before they could answer.  So -2 is swallowed here. */
static void GameDataSaveMcFormatConfWait(void)                          /* 1686 */
{
    int mc_res;
    int msg_id;

    GameDataSaveMcFormatConfPad();                                      /* 1694 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1697 */

    if (mc_res < 0) {                                                   /* 1699 */
        msg_id = 2;

        switch (mc_res) {                                               /* 1700 */
        case -1:        /* card swapped */
            msg_id = 1;                                                 /* 1705 */
            break;
        case -2:        /* still unformatted -- expected here */
            return;
        case -0x14:     /* gave up */
            msg_id = 1;                                                 /* 1711 */
            break;
        default:
            msg_id = 2;
            break;
        }

        game_data_save_disp.msg_id  = msg_id;                           /* 1715 */
        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1716 */
    }
}

static void GameDataSaveMcFormatConfPad(void)                           /* 1726 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1730 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1750 */
        game_data_save_ctrl.conf_csr ^= 1;                              /* 1754 */
    }
    else if (*paddat[0] == 1) {                                         /* 1735 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1736 */

        if (game_data_save_ctrl.conf_csr == 0) {                        /* 1737 */
            game_data_save_ctrl.mc_step = GDS_MC_FORMAT_INIT;           /* 1740 */
            game_data_save_disp.msg_id  = 4;                            /* 1741 */
        }
        else {
            GameDataSaveOutReq();                                       /* 1744 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 1745 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1746 */
        GameDataSaveOutReq();                                           /* 1757 */
    }
}

static void GameDataSaveMcFormatInit(void)                              /* 1766 */
{
    MemoryCardFormatInit(0, 0);                                         /* 1769 */

    game_data_save_disp.msg_id  = 4;                                    /* 1771 */
    game_data_save_ctrl.mc_step = GDS_MC_FORMAT_WAIT;                   /* 1772 */
}

static void GameDataSaveMcFormatWait(void)                              /* 1779 */
{
    int mc_res;

    mc_res = MemoryCardFormatMain();                                    /* 1786 */

    if (mc_res == 1) {                                                  /* 1789 */
        game_data_save_ctrl.mc_step = GDS_MC_FORMAT_END_INIT;           /* 1792 */
        game_data_save_disp.msg_id  = 0x39;                             /* 1793 */
    }
    else if (mc_res < 0) {                                              /* 1800 */
        game_data_save_disp.msg_id  = 5;                                /* 1801 */
        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1802 */
    }
}

static void GameDataSaveMcFormatEndInit(void)                           /* 1812 */
{
    game_data_save_ctrl.mc_step        = GDS_MC_FORMAT_END_WAIT;        /* 1815 */
    game_data_save_disp.msg_id         = 0x39;                          /* 1816 */
    game_data_save_ctrl.format_end_cnt = 0;                             /* 1817 */
}

/* Two seconds of "card formatted", then straight on to making the directory --
 * the player is not asked again. */
static void GameDataSaveMcFormatEndWait(void)                           /* 1826 */
{
    game_data_save_ctrl.format_end_cnt++;                               /* 1830 */

    if (game_data_save_ctrl.format_end_cnt > GAME_DATA_SAVE_FORMAT_END_TIME - 1) { /* 1833 */
        game_data_save_ctrl.mc_step = GDS_MC_NEW_MAKE_INIT;             /* 1835 */
        game_data_save_disp.msg_id  = 0x12;                             /* 1836 */
    }
}

/* The card watch every interactive state runs: a pull, a swap or a hard error
 * takes the screen to the error prompt with a message id. */
static void GameDataSaveMcEveryFrameCheck(void)                         /* 1845 */
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1853 */

    if (mc_res < 0) {                                                   /* 1855 */
        game_data_save_disp.msg_id = 2;

        switch (mc_res) {                                               /* 1856 */
        case -1:        /* card swapped */
            game_data_save_disp.msg_id = 1;                             /* 1861 */
            break;
        case -2:        /* not formatted */
            game_data_save_disp.msg_id = 0xd;                           /* 1866 */
            break;
        case -0x14:     /* gave up */
            game_data_save_disp.msg_id = 1;                             /* 1870 */
            break;
        default:
            game_data_save_disp.msg_id = 2;                             /* 1874 */
            break;
        }

        game_data_save_ctrl.mc_step = GDS_MC_ERROR_CONF_INIT;           /* 1875 */
    }
}

/* ==========================================================================
 *  Tear-down
 * ======================================================================== */

void GameDataSaveEnd(void)                                              /* 1887 */
{
    int i;

    LiberateGameDataSaveMem(&game_data_buff_addr);                      /* 1892 */

    for (i = 0; i < GAME_DATA_SAVE_DATA_MAX; i++) {                     /* 1894 */
        LiberateGameDataSaveMem(&game_data_save_snap_addr[i]);          /* 1895 */
    }                                                                   /* 1896 */

    MemoryCardEnd();                                                    /* 1899 */
}

void GameDataSaveTexMemFree(void)                                       /* 1906 */
{
    LiberateGameDataSaveMem(&save_load_tex_addr);                       /* 1910 */
    LiberateGameDataSaveMem(&outgame_cmn_tex_addr);                     /* 1911 */

    GameDataSaveMemGet  = nullptr;                                      /* 1913 */
    GameDataSaveMemFree = nullptr;                                      /* 1914 */
}

static void LiberateGameDataSaveMem(void **tex_addr)                    /* 1923 */
{
    if (*tex_addr != nullptr) {                                         /* 1926 */
        GameDataSaveMemFree(*tex_addr);                                 /* 1927 */
        *tex_addr = nullptr;                                            /* 1928 */
    }
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void GameDataSaveDispInit(void)                                  /* 1941 */
{
    game_data_save_disp.msg_id     = 0;                                 /* 1944 */
    game_data_save_disp.anim_step  = ZERO2_ANIM2D_STEP_START;           /* 1945 */
    game_data_save_disp.anim_timer = 0;                                 /* 1946 */
    game_data_save_disp.csr_timer  = 0;                                 /* 1947 */
}

/* Which of the four layouts is drawn is a switch on mc_step, and the grouping
 * is what tells you what each state is: 6 is the live slot list, 7 the
 * overwrite prompt, 8-11 and 14 the slot list under a progress message, the
 * five yes/no states a card-level prompt, and everything else -- including the
 * error states -- the plain card message.  Note the ROM's own fall-through
 * from the progress group into the message-draw shared with the prompt group. */
void GameDataSaveDispMain(void)                                         /* 1955 */
{
    u_char alpha;
    u_char rgb;

    rgb = 0x80;                                                         /* 1960 */

    if (game_data_save_ctrl.step < GAME_DATA_SAVE_MC_EXE) {             /* 1965 */
        return;
    }

    alpha = Zero2Anim2D_InOutAnimCtrl(&game_data_save_disp.anim_step,
                                      &game_data_save_disp.anim_timer,
                                      10, 5);                           /* 1968 */

    if (game_data_save_disp.anim_step == ZERO2_ANIM2D_STEP_END) {       /* 1970 */
        return;
    }

    SaveLoadCmnBaseDisp(0, 0, alpha, save_load_tex_addr, 0);            /* 1972 */
    SaveLoadTitleFrameDisp(0, 0, alpha, outgame_cmn_tex_addr);          /* 1975 */
    SaveLoadTitleSaveDisp(0, 0, alpha, save_load_tex_addr);             /* 1978 */

    switch (game_data_save_ctrl.mc_step) {                              /* 1980 */
    case GDS_MC_SAVE_FILE_SEL:
        GameData_SaveFileSelDisp(alpha);                                /* 1982 */
        Zero2Anim2D_CsrAnimCtrl(&game_data_save_disp.csr_timer, &rgb);  /* 1984 */
        SaveLoadCursorDisp(0, 0, alpha, rgb, save_load_tex_addr,
                           (int)game_data_save_ctrl.csr);               /* 1986 */
        SaveLoadFileSelMsgWinDisp(0, 0, alpha);                         /* 1988 */
        SaveLoadFileSelMsgDisp(0, 0, alpha, 10);                        /* 1990 */
        break;                                                          /* 1991 */

    case GDS_MC_SAVE_CONF:
        GameData_SaveFileSelDisp(alpha);                                /* 1994 */
        SaveLoadFileSelYesNoWinDisp(0, 0, alpha,
                                    (int)game_data_save_ctrl.conf_csr); /* 1996 */
        SaveLoadFileSelMsgDisp(0, 0, alpha, game_data_save_disp.msg_id); /* 1998 */
        break;                                                          /* 1999 */

    case GDS_MC_SYSTEM_LOAD_INIT:
    case GDS_MC_SYSTEM_LOAD_WAIT:
    case GDS_MC_SAVE_INIT:
    case GDS_MC_SAVE_WAIT:
    case GDS_MC_END_CONF:
        GameData_SaveFileSelDisp(alpha);                                /* 2006 */
        SaveLoadMcStateMsgWinDisp(0, 0, alpha);                         /* 2008 */
        /* fall through -- shares the message draw with the prompt group */
        SaveLoadMcStateMsgDisp(0, 0, alpha, game_data_save_disp.msg_id); /* 2011 */
        break;

    case GDS_MC_REMAKE_CONF_INIT:
    case GDS_MC_REMAKE_CONF_WAIT:
    case GDS_MC_NEW_MAKE_CONF_INIT:
    case GDS_MC_NEW_MAKE_CONF_WAIT:
    case GDS_MC_FORMAT_CONF_INIT:
    case GDS_MC_FORMAT_CONF_WAIT:
        SaveLoadMcCheckDisp(0, 0, alpha, save_load_tex_addr);           /* 2019 */
        SaveLoadMcSelYesNoWinDisp(0, 0, alpha,
                                  (int)game_data_save_ctrl.conf_csr);   /* 2021 */
        SaveLoadMcStateMsgDisp(0, 0, alpha, game_data_save_disp.msg_id); /* 2023 */
        break;                                                          /* 2024 */

    default:
        SaveLoadMcCheckDisp(0, 0, alpha, save_load_tex_addr);           /* 2027 */
        SaveLoadMcStateMsgWinDisp(0, 0, alpha);                         /* 2029 */
        SaveLoadMcStateMsgDisp(0, 0, alpha, game_data_save_disp.msg_id); /* 2031 */
        break;
    }
}

/* The five slot rows.  An empty slot gets a line and the "no clear" mask; a
 * used one gets its snapshot, its number, and either the in-progress readout
 * (chapter / room / time) or the cleared one (time only).  A slot that has
 * been cleared at least once also gets the flare and the clear-count plate. */
static void GameData_SaveFileSelDisp(u_char alpha)                      /* 2044 */
{
    int data_num;

    for (data_num = 0; data_num < GAME_DATA_SAVE_DATA_MAX; data_num++) { /* 2049 */
        if (GetMemoryCardPlayDataFlg(data_num) == 0) {                  /* 2051 */
            if (game_data_save_ctrl.csr == data_num) {                  /* 2052 */
                SaveLoadSelLineDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2054 */
            }
            else {
                SaveLoadNonSelLineDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2058 */
            }

            SaveLoadNonClearMaskDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2060 */
            SaveLoadSnapShadowDisp(0, 0, alpha, save_load_tex_addr, data_num);   /* 2062 */
        }
        else {
            if (game_data_save_snap_addr[data_num] != nullptr) {         /* 2066 */
                SaveLoadSnapShotDisp(0, 0, alpha,
                                     game_data_save_snap_addr[data_num], data_num); /* 2067 */
            }

            if (game_data_save_ctrl.csr == data_num) {                  /* 2071 */
                SaveLoadSelNoDisp(0, 0, alpha, save_load_tex_addr, data_num);     /* 2076 */
                SaveLoadSelDataNumDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2078 */

                if (GetMemoryCardClearDataFlg(data_num) == 0) {         /* 2082 */
                    SaveLoadMcPlayDataInfoDisp(0, 0, alpha,
                                               GetMemoryCardPlayDataChapter(data_num),
                                               GetMemoryCardPlayDataRoomLabel(data_num),
                                               GetMemoryCardPlayDataPlayTime(data_num)); /* 2085 */
                }
                else {
                    SaveLoadMcClearPlayDataInfoDisp(0, 0, alpha,
                                               GetMemoryCardPlayDataPlayTime(data_num)); /* 2087 */
                }
            }
            else {
                SaveLoadNonSelNoDisp(0, 0, alpha, save_load_tex_addr, data_num);      /* 2089 */
                SaveLoadNonSelDataNumDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2093 */
            }

            SaveLoadSnapShadowDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2099 */

            if (GetMemoryCardPlayDataClearNum(data_num) > 0) {          /* 2101 */
                SaveLoadClearFlareDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2105 */
                SaveLoadClearNumberDisp(GetMemoryCardPlayDataClearNum(data_num),
                                        0, 0, alpha, 0, 1, data_num,
                                        save_load_tex_addr);            /* 2109 */
            }
            else {
                SaveLoadNonClearMaskDisp(0, 0, alpha, save_load_tex_addr, data_num); /* 2111 */
            }
        }
    }                                                                   /* 2113 */

    SaveLoadSelFlareDisp(0, 0, alpha, save_load_tex_addr,
                         (int)game_data_save_ctrl.csr);                 /* 2115 */
}
