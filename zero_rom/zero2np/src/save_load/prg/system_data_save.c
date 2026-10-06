// FILE: /home/zero_rom/zero2np/src/save_load/prg/system_data_save.c
//
// The system-file save screen: writes file 0 of the game-data directory, the
// one that holds the option settings, the language and the clear record.
//
// Read game_data_save.c first.  This is the same machine with the slot list
// taken out -- same two-level step/mc_step pair, same fall-through from every
// Init case into its Wait case, same five recovery branches (remake,
// dir-delete, new-make, format, format-end) with their own yes/no prompts.
// What is missing is everything to do with picking a slot: there is no
// cursor, no snapshot loading, no play-data header, and only one file is
// written.  That takes game_data_save's 31 card states down to 25 and its
// four screen steps down to three -- there is no load-wait step either,
// because option.c already has both paks resident.
//
// The screen is put up by option.c when the player leaves the option menu:
// window 2 is "Save the settings?", and the answer decides which of two very
// different exits Main() reports.  exit_state 0 means "done with the option
// screen" -- a completed save, an acknowledged error, or CROSS on "no" -- and
// takes option.c to place 4.  exit_state 1 means "backed out" (TRIANGLE) and
// leaves the player on the option page.  So answering "no" still leaves the
// menu; only cancelling keeps it.
//
// The save itself is one file and it is a read-modify-write: the card's copy
// of the system file is loaded first (mc_step 4/5) so ClearFlgMerging() can
// fold its clear record into the running one before the write goes out.  That
// is the same reason game_data_save.c reads file 0 back before overwriting
// it -- a save must not lose a costume or an ending the other playthrough
// unlocked.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), system_data_save.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "system_data_save.h"

#include "save_load_disp.h"                     // SaveLoadMc*Disp

#include "../../common/utility2.h"              // PRINT_ASSERT
#include "../../ingame/clear/prg/clear_flg.h"   // ClearFlgMerging / SetClearFlgCtrl
#include "../../ingame/menu/zero2_anim2d.h"     // Zero2Anim2D_InOutAnimCtrl
#include "../../outgame/option.h"               // OptSetOptWrk
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
#include "../../system/mc/prg/mc_set_data.h"    // path / size / data-area helpers
#include "../../system/os/system.h"             // SystemBankPlay
#include "../../system/pad/pad.h"               // pad / paddat / GetPadAnalogRpt

#include <string.h>                             // memset

/* The system file is file 0 of directory 0. */
#define SYSTEM_DATA_SAVE_MC_DIR       0
#define SYSTEM_DATA_SAVE_MC_FILE      0

/* The ROM's literal size, not sizeof(). */
#define SYSTEM_DATA_SAVE_PATH_NAME_LEN 55

/* The format-complete message is held for two seconds before the screen goes
 * on to make the directory. */
#define SYSTEM_DATA_SAVE_FORMAT_END_TIME 60

/* SystemDataSaveOutReq()'s argument, and what Main() then reports. */
#define SYSTEM_DATA_SAVE_EXIT_DONE   0  /* -> Main() returns  1 */
#define SYSTEM_DATA_SAVE_EXIT_CANCEL 1  /* -> Main() returns -1 */

/* system_data_save_ctrl.step -- the screen itself. */
enum SYSTEM_DATA_SAVE_STEP
{
    SYSTEM_DATA_SAVE_DISP_INIT = 0,     /* reset the display state          */
    SYSTEM_DATA_SAVE_MC_EXE    = 1,     /* the card machine has the screen  */
    SYSTEM_DATA_SAVE_OUT       = 2      /* animating out; Main() then reports */
};

/* system_data_save_ctrl.mc_step.  The ROM's debug info carries no enum for
 * these -- the names are taken from the step handlers' own ROM symbol names
 * so the values still read against the jump table at rodata 3e6140. */
enum SYSTEM_DATA_SAVE_MC_STEP
{
    SDS_MC_CHECK_INIT           = 0,
    SDS_MC_CHECK_WAIT           = 1,
    SDS_MC_SAVE_CONF_INIT       = 2,
    SDS_MC_SAVE_CONF_WAIT       = 3,
    SDS_MC_LOAD_INIT            = 4,
    SDS_MC_LOAD_WAIT            = 5,
    SDS_MC_SAVE_INIT            = 6,
    SDS_MC_SAVE_WAIT            = 7,
    SDS_MC_ERROR_CONF_INIT      = 8,
    SDS_MC_ERROR_CONF_WAIT      = 9,
    SDS_MC_END_CONF             = 10,
    SDS_MC_REMAKE_CONF_INIT     = 11,
    SDS_MC_REMAKE_CONF_WAIT     = 12,
    SDS_MC_REMAKE_DIR_DEL_INIT  = 13,
    SDS_MC_REMAKE_DIR_DEL_WAIT  = 14,
    SDS_MC_NEW_MAKE_CONF_INIT   = 15,
    SDS_MC_NEW_MAKE_CONF_WAIT   = 16,
    SDS_MC_NEW_MAKE_INIT        = 17,
    SDS_MC_NEW_MAKE_WAIT        = 18,
    SDS_MC_FORMAT_CONF_INIT     = 19,
    SDS_MC_FORMAT_CONF_WAIT     = 20,
    SDS_MC_FORMAT_INIT          = 21,
    SDS_MC_FORMAT_WAIT          = 22,
    SDS_MC_FORMAT_END_INIT      = 23,
    SDS_MC_FORMAT_END_WAIT      = 24
};

/* types.txt.  Private to this file. */
typedef struct                      /* 0x5 */
{
    /* 0x0 */ char step;
    /* 0x1 */ char mc_step;
    /* 0x2 */ char conf_csr;
    /* 0x3 */ char exit_state;
    /* 0x4 */ char format_end_cnt;
} SYSTEM_DATA_SAVE_CTRL;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int  msg_id;
    /* 0x4 */ char anim_step;       /* Zero2Anim2D in/out step */
    /* 0x5 */ char anim_timer;
} SYSTEM_DATA_SAVE_DISP;

static void *(*SystemDataSaveMemGet)(int);                  /* sdata 3f4700 */
static void (*SystemDataSaveMemFree)(void *);               /* sdata 3f4704 */

static void *system_data_buff_addr;                         /* sdata 3f4708 */

static SYSTEM_DATA_SAVE_CTRL system_data_save_ctrl;         /* sbss  3f4fc0 */
static SYSTEM_DATA_SAVE_DISP system_data_save_disp;         /* sbss  3f4fc8 */

static void SystemDataSaveCtrlInit(void);
static void GetSystemDataSaveDataMem(void **tex_addr, int size);
static void SystemDataSaveOutReq(char exit_state);
static void SystemDataSaveMcMain(void);
static void SystemDataSaveMcCheckInit(void);
static void SystemDataSaveMcCheckWait(void);
static void SystemDataSaveMcSaveConfInit(void);
static void SystemDataSaveMcSaveConfWait(void);
static void SystemDataSaveMcSaveConfPad(void);
static void SystemDataSaveMcLoadInit(void);
static void SystemDataSaveMcLoadWait(void);
static void SystemDataSaveMcSaveInit(void);
static void SystemDataSaveMcSaveWait(void);
static void SystemDataSaveMcErrorConfInit(void);
static void SystemDataSaveMcErrorConfWait(void);
static void SystemDataSaveMcErrorConfPad(void);
static void SystemDataSaveMcEndConf(void);
static void SystemDataSaveMcRemakeConfInit(void);
static void SystemDataSaveMcRemakeConfWait(void);
static void SystemDataSaveMcRemakeConfPad(void);
static void SystemDataSaveMcRemakeDirDelInit(void);
static void SystemDataSaveMcRemakeDirDelWait(void);
static void SystemDataSaveMcNewMakeConfInit(void);
static void SystemDataSaveMcNewMakeConfWait(void);
static void SystemDataSaveMcNewMakeConfPad(void);
static void SystemDataSaveMcNewMakeInit(void);
static void SystemDataSaveMcNewMakeWait(void);
static void SystemDataSaveMcFormatConfInit(void);
static void SystemDataSaveMcFormatConfWait(void);
static void SystemDataSaveMcFormatConfPad(void);
static void SystemDataSaveMcFormatInit(void);
static void SystemDataSaveMcFormatWait(void);
static void SystemDataSaveMcFormatEndInit(void);
static void SystemDataSaveMcFormatEndWait(void);
static void SystemDataSaveMcEveryFrameCheck(void);
static void LiberateSystemDataSaveMem(void **tex_addr);
static void SystemDataSaveDispInit(void);

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* One-shot: the assert fires if a second caller arrives without the first
 * having released through SystemDataSaveEnd().  Note the test is written the
 * other way round from GameDataSaveBackGroundLoadReq()'s -- here the
 * assignment is the `if` arm and the assert the `else`. */
void SystemDataSaveInit(void *(*mem_get)(int), void (*mem_free)(void *)) /* 200 */
{
    if ((SystemDataSaveMemGet == nullptr) && (SystemDataSaveMemFree == nullptr)) { /* 203 */
        SystemDataSaveMemGet  = mem_get;                                /* 204 */
        SystemDataSaveMemFree = mem_free;                               /* 205 */
    }
    else {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 208 */
    }

    SystemDataSaveCtrlInit();                                           /* 213 */

    MemoryCardExeInit();                                                /* 216 */

    LiberateSystemDataSaveMem(&system_data_buff_addr);                  /* 219 */

    MemoryCardSetAccessPort(0);                                         /* 222 */
}

static void SystemDataSaveCtrlInit(void)                                /* 230 */
{
    system_data_save_ctrl.step       = SYSTEM_DATA_SAVE_DISP_INIT;      /* 233 */
    system_data_save_ctrl.mc_step    = SDS_MC_CHECK_INIT;               /* 234 */
    system_data_save_ctrl.conf_csr   = 1;           /* default "no" */  /* 235 */
    system_data_save_ctrl.exit_state = SYSTEM_DATA_SAVE_EXIT_DONE;      /* 236 */

    system_data_save_ctrl.format_end_cnt = 0;                           /* 238 */
}

static void GetSystemDataSaveDataMem(void **tex_addr, int size)         /* 248 */
{
    if (*tex_addr != nullptr) {                                         /* 251 */
        LiberateSystemDataSaveMem(tex_addr);                            /* 252 */
    }

    *tex_addr = SystemDataSaveMemGet(size);                             /* 256 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Returns 1 once the screen has finished with the option menu, -1 if the
 * player backed out, 0 while it is still up.  The card machine is gated on
 * the window being fully open, so no card traffic starts until the prompt is
 * actually visible. */
int SystemDataSaveMain(void)                                            /* 270 */
{
    int res;

    res = 0;

    switch (system_data_save_ctrl.step) {                               /* 277 */
    case SYSTEM_DATA_SAVE_DISP_INIT:
        SystemDataSaveDispInit();                                       /* 280 */
        system_data_save_ctrl.step = SYSTEM_DATA_SAVE_MC_EXE;           /* 283 */
        break;

    case SYSTEM_DATA_SAVE_MC_EXE:
        if (system_data_save_disp.anim_step == ZERO2_ANIM2D_STEP_SHOW) { /* 285 */
            SystemDataSaveMcMain();                                     /* 287 */
        }
        break;                                                          /* 289 */

    case SYSTEM_DATA_SAVE_OUT:
        if (system_data_save_disp.anim_step == ZERO2_ANIM2D_STEP_END) { /* 291 */
            if (system_data_save_ctrl.exit_state == SYSTEM_DATA_SAVE_EXIT_DONE) { /* 292 */
                res = 1;
            }
            else if (system_data_save_ctrl.exit_state == SYSTEM_DATA_SAVE_EXIT_CANCEL) {
                res = -1;                                               /* 298 */
            }
            else {
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 300 */
            }
        }
        break;                                                          /* 303 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 305 */
        break;
    }

    return res;                                                         /* 309 */
}

/* Every way out of the screen goes through here.  exit_state is bounds-checked
 * against the two values Main() knows, and stored last. */
static void SystemDataSaveOutReq(char exit_state)                       /* 316 */
{
    if ((u_char)exit_state > SYSTEM_DATA_SAVE_EXIT_CANCEL) {            /* 319 */
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 320 */
    }

    system_data_save_ctrl.step      = SYSTEM_DATA_SAVE_OUT;             /* 325 */
    system_data_save_disp.anim_step = ZERO2_ANIM2D_STEP_OUT;            /* 326 */
    system_data_save_disp.anim_timer = 0;                               /* 327 */

    system_data_save_ctrl.exit_state = exit_state;                      /* 329 */
}

/* The card machine.  Every Init case falls through into its Wait case, so a
 * request is issued and polled on the same frame -- that fall-through is the
 * ROM's own.  The three states with no Init half (both confirms and the
 * end-confirm) are the ones that wait on the player instead of the card. */
static void SystemDataSaveMcMain(void)                                  /* 337 */
{
    switch (system_data_save_ctrl.mc_step) {                            /* 340 */
    case SDS_MC_CHECK_INIT:          SystemDataSaveMcCheckInit();       /* 342 */
        /* fall through */
    case SDS_MC_CHECK_WAIT:          SystemDataSaveMcCheckWait();       /* 345 */
        break;
    case SDS_MC_SAVE_CONF_INIT:      SystemDataSaveMcSaveConfInit();    /* 348 */
        /* fall through */
    case SDS_MC_SAVE_CONF_WAIT:      SystemDataSaveMcSaveConfWait();    /* 351 */
        break;
    case SDS_MC_LOAD_INIT:           SystemDataSaveMcLoadInit();        /* 354 */
        /* fall through */
    case SDS_MC_LOAD_WAIT:           SystemDataSaveMcLoadWait();        /* 357 */
        break;
    case SDS_MC_SAVE_INIT:           SystemDataSaveMcSaveInit();        /* 360 */
        /* fall through */
    case SDS_MC_SAVE_WAIT:           SystemDataSaveMcSaveWait();        /* 363 */
        break;
    case SDS_MC_ERROR_CONF_INIT:     SystemDataSaveMcErrorConfInit();   /* 366 */
        /* fall through */
    case SDS_MC_ERROR_CONF_WAIT:     SystemDataSaveMcErrorConfWait();   /* 369 */
        break;
    case SDS_MC_END_CONF:            SystemDataSaveMcEndConf();         /* 372 */
        break;
    case SDS_MC_REMAKE_CONF_INIT:    SystemDataSaveMcRemakeConfInit();  /* 375 */
        /* fall through */
    case SDS_MC_REMAKE_CONF_WAIT:    SystemDataSaveMcRemakeConfWait();  /* 378 */
        break;
    case SDS_MC_REMAKE_DIR_DEL_INIT: SystemDataSaveMcRemakeDirDelInit(); /* 381 */
        /* fall through */
    case SDS_MC_REMAKE_DIR_DEL_WAIT: SystemDataSaveMcRemakeDirDelWait(); /* 384 */
        break;
    case SDS_MC_NEW_MAKE_CONF_INIT:  SystemDataSaveMcNewMakeConfInit(); /* 387 */
        /* fall through */
    case SDS_MC_NEW_MAKE_CONF_WAIT:  SystemDataSaveMcNewMakeConfWait(); /* 390 */
        break;
    case SDS_MC_NEW_MAKE_INIT:       SystemDataSaveMcNewMakeInit();     /* 393 */
        /* fall through */
    case SDS_MC_NEW_MAKE_WAIT:       SystemDataSaveMcNewMakeWait();     /* 396 */
        break;
    case SDS_MC_FORMAT_CONF_INIT:    SystemDataSaveMcFormatConfInit();  /* 399 */
        /* fall through */
    case SDS_MC_FORMAT_CONF_WAIT:    SystemDataSaveMcFormatConfWait();  /* 402 */
        break;
    case SDS_MC_FORMAT_INIT:         SystemDataSaveMcFormatInit();      /* 405 */
        /* fall through */
    case SDS_MC_FORMAT_WAIT:         SystemDataSaveMcFormatWait();      /* 408 */
        break;
    case SDS_MC_FORMAT_END_INIT:     SystemDataSaveMcFormatEndInit();   /* 412 */
        /* fall through */
    case SDS_MC_FORMAT_END_WAIT:     SystemDataSaveMcFormatEndWait();   /* 415 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 418 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Card check
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcCheckInit(void)                             /* 427 */
{
    char path_name[SYSTEM_DATA_SAVE_PATH_NAME_LEN] = "";                /* 431 */

    MemoryCardMakeSearchDirPath(path_name, SYSTEM_DATA_SAVE_MC_DIR);    /* 435 */
    MemoryCardCheckInit(0, 0, path_name);                               /* 437 */

    system_data_save_ctrl.mc_step = SDS_MC_CHECK_WAIT;                  /* 439 */
}

/* Where the screen decides what kind of card it is looking at.  A usable
 * directory goes straight to the save prompt; a missing (-4) or broken (-6)
 * one leads to the make/remake prompts; everything else is an error.
 *
 * Note that case -1 -- the card was swapped -- repeats the whole
 * DirBroken/EmptyBroken triage rather than short-circuiting, which is what
 * game_data_save.c's own -1 case does.  A swap is not an error here, it just
 * means the answer has to be worked out again for the new card. */
static void SystemDataSaveMcCheckWait(void)                             /* 447 */
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 453 */

    if (mc_res == 1) {                                                  /* 456 */
        if (MemoryCardCheckDirBroken(SYSTEM_DATA_SAVE_MC_DIR) != 0) {   /* 458 */
            system_data_save_disp.msg_id  = 0x1b;                       /* 461 */
            system_data_save_ctrl.mc_step = SDS_MC_SAVE_CONF_INIT;
            return;
        }

        if (MemoryCardCheckEmptyBroken(SYSTEM_DATA_SAVE_MC_DIR) != 0) { /* 466 */
            system_data_save_ctrl.conf_csr = 1;                         /* 467 */
            system_data_save_disp.msg_id   = 0x16;                      /* 468 */
            system_data_save_ctrl.mc_step  = SDS_MC_REMAKE_CONF_INIT;   /* 469 */
            return;
        }

        system_data_save_disp.msg_id  = 0x19;                           /* 473 */
        system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
    }
    else if (mc_res < 0) {                                              /* 478 */
        switch (mc_res) {                                               /* 480 */
        case -1:        /* card swapped -- re-triage the new card */
            if (MemoryCardCheckDirBroken(SYSTEM_DATA_SAVE_MC_DIR) != 0) { /* 483 */
                system_data_save_disp.msg_id  = 0x1b;                   /* 486 */
                system_data_save_ctrl.mc_step = SDS_MC_SAVE_CONF_INIT;
                return;
            }

            if (MemoryCardCheckEmptyBroken(SYSTEM_DATA_SAVE_MC_DIR) != 0) { /* 491 */
                system_data_save_ctrl.conf_csr = 1;                     /* 492 */
                system_data_save_disp.msg_id   = 0x16;                  /* 493 */
                system_data_save_ctrl.mc_step  = SDS_MC_REMAKE_CONF_INIT; /* 494 */
                return;
            }

            system_data_save_disp.msg_id  = 0x19;                       /* 501 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;

        case -2:        /* not formatted */
            system_data_save_ctrl.conf_csr = 1;                         /* 503 */
            system_data_save_disp.msg_id   = 3;                         /* 504 */
            system_data_save_ctrl.mc_step  = SDS_MC_FORMAT_CONF_INIT;   /* 506 */
            break;

        case -4:        /* no save directory */
            if (MemoryCardCheckEmpty(SYSTEM_DATA_SAVE_MC_DIR) != 0) {   /* 509 */
                system_data_save_ctrl.conf_csr = 1;                     /* 511 */
                system_data_save_disp.msg_id   = 0x15;                  /* 512 */
                system_data_save_ctrl.mc_step  = SDS_MC_NEW_MAKE_CONF_INIT; /* 513 */
                return;
            }

            system_data_save_disp.msg_id  = 0x17;                       /* 520 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;

        case -6:        /* directory there but unusable */
            if (MemoryCardCheckEmptyBroken(SYSTEM_DATA_SAVE_MC_DIR) != 0) { /* 523 */
                system_data_save_ctrl.conf_csr = 1;                     /* 524 */
                system_data_save_disp.msg_id   = 0x16;                  /* 525 */
                system_data_save_ctrl.mc_step  = SDS_MC_REMAKE_CONF_INIT; /* 526 */
                return;
            }

            system_data_save_disp.msg_id  = 0x19;
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;

        case -0x14:     /* rejected four times, gave up */
            system_data_save_disp.msg_id  = 1;                          /* 536 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;

        default:
            system_data_save_disp.msg_id  = 2;                          /* 540 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;     /* 541 */
            break;
        }
    }
}

/* --------------------------------------------------------------------------
 *  "Save the settings?"
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcSaveConfInit(void)                          /* 551 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 555 */

    system_data_save_ctrl.mc_step = SDS_MC_SAVE_CONF_WAIT;              /* 557 */
}

static void SystemDataSaveMcSaveConfWait(void)                          /* 565 */
{
    SystemDataSaveMcSaveConfPad();                                      /* 569 */

    SystemDataSaveMcEveryFrameCheck();                                  /* 572 */
}

/* CROSS on "yes" starts the read-modify-write; CROSS on "no" leaves the
 * option screen without saving; TRIANGLE backs out to the option page. */
static void SystemDataSaveMcSaveConfPad(void)                           /* 579 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 583 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 588 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 589 */
        system_data_save_ctrl.conf_csr ^= 1;                            /* 590 */
    }
    else if (*paddat[0] == 1) {                                         /* 593 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 594 */

        if (system_data_save_ctrl.conf_csr == 0) {                      /* 596 */
            system_data_save_disp.msg_id  = 0x1c;                       /* 597 */
            system_data_save_ctrl.mc_step = SDS_MC_LOAD_INIT;           /* 598 */
        }
        else {
            SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);           /* 602 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 606 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 607 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_CANCEL);             /* 608 */
    }
}

/* --------------------------------------------------------------------------
 *  Read the card's copy back, merge, write it out again
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcLoadInit(void)                              /* 617 */
{
    int  size;
    char path_name[SYSTEM_DATA_SAVE_PATH_NAME_LEN] = "";                /* 623 */

    LiberateSystemDataSaveMem(&system_data_buff_addr);                  /* 626 */

    MemoryCardSetFilePath(path_name, SYSTEM_DATA_SAVE_MC_DIR,
                          SYSTEM_DATA_SAVE_MC_FILE);                    /* 630 */
    size = GetMemoryCardDataSize(SYSTEM_DATA_SAVE_MC_DIR,
                                 SYSTEM_DATA_SAVE_MC_FILE);             /* 632 */

    GetSystemDataSaveDataMem(&system_data_buff_addr, size);             /* 634 */

    MemoryCardFileLoadInit(0, 0, path_name, system_data_buff_addr, size); /* 636 */

    system_data_save_disp.msg_id  = 0x1c;                               /* 638 */
    system_data_save_ctrl.mc_step = SDS_MC_LOAD_WAIT;                   /* 639 */
}

/* The merge.  CLEAR_FLG_CTRL is the first thing in the system file, so the
 * copy is a straight 0x18-byte read off the head of the buffer; the result of
 * merging it with the running clear_flg_ctrl becomes the running one, and
 * that is what SetMemoryCardSaveDataToBuff() will write back out.  A file
 * that has never been saved to is skipped -- there is nothing in it to
 * merge -- but the save still goes ahead. */
static void SystemDataSaveMcLoadWait(void)                              /* 647 */
{
    int            mc_res;
    int            size;
    CLEAR_FLG_CTRL buff_flg_ctrl;

    memset(&buff_flg_ctrl, 0, sizeof(CLEAR_FLG_CTRL));                  /* 656 */

    mc_res = MemoryCardFileLoadMain();                                  /* 659 */

    if (mc_res == 1) {                                                  /* 662 */
        size = GetMemoryCardDataSize(SYSTEM_DATA_SAVE_MC_DIR,
                                     SYSTEM_DATA_SAVE_MC_FILE);         /* 664 */

        if (MemoryCardCheckFileBroken(system_data_buff_addr, size) != 0) { /* 667 */
            if (MemoryCardCheckNewFileLoad(system_data_buff_addr, size) == 0) { /* 669 */
                buff_flg_ctrl = *(CLEAR_FLG_CTRL *)system_data_buff_addr; /* 677 */
                SetClearFlgCtrl(ClearFlgMerging(buff_flg_ctrl, clear_flg_ctrl)); /* 680 */
            }

            system_data_save_ctrl.mc_step = SDS_MC_SAVE_INIT;           /* 682 */
            system_data_save_disp.msg_id  = 0x1c;                       /* 683 */
        }
        else {
            system_data_save_disp.msg_id  = 0x19;                       /* 688 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;     /* 689 */
        }
    }
    else if (mc_res < 0) {                                              /* 693 */
        switch (mc_res) {                                               /* 695 */
        case -2:        /* not formatted */
            system_data_save_disp.msg_id  = 0xd;                        /* 699 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;
        case -3:        /* short read: the file is corrupt */
            system_data_save_disp.msg_id  = 0x19;                       /* 702 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;
        default:
            system_data_save_disp.msg_id  = 7;                          /* 708 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;     /* 711 */
            break;
        }
    }

    /* Any settled result -- success or failure -- releases the buffer; only a
     * still-running load (0) keeps it. */
    if (mc_res != 0) {                                                  /* 716 */
        LiberateSystemDataSaveMem(&system_data_buff_addr);              /* 717 */
    }
}

/* OptSetOptWrk() is what makes the write meaningful: it flushes the option
 * screen's working copy into the live option block first, so the bytes
 * SetMemoryCardSaveDataToBuff() marshals are the settings the player just
 * chose rather than the ones they arrived with. */
static void SystemDataSaveMcSaveInit(void)                              /* 724 */
{
    int  size;
    char path_name[SYSTEM_DATA_SAVE_PATH_NAME_LEN] = "";                /* 730 */

    LiberateSystemDataSaveMem(&system_data_buff_addr);                  /* 733 */

    OptSetOptWrk();                                                     /* 736 */

    MemoryCardSetFilePath(path_name, SYSTEM_DATA_SAVE_MC_DIR,
                          SYSTEM_DATA_SAVE_MC_FILE);                    /* 744 */
    size = GetMemoryCardDataSize(SYSTEM_DATA_SAVE_MC_DIR,
                                 SYSTEM_DATA_SAVE_MC_FILE);             /* 746 */

    GetSystemDataSaveDataMem(&system_data_buff_addr, size);             /* 748 */

    SetMemoryCardSaveDataToBuff((char *)system_data_buff_addr,
                                SYSTEM_DATA_SAVE_MC_DIR,
                                SYSTEM_DATA_SAVE_MC_FILE);              /* 750 */

    MemoryCardFileSaveInit(0, 0, path_name, system_data_buff_addr, size); /* 752 */

    system_data_save_disp.msg_id  = 0x1c;                               /* 754 */
    system_data_save_ctrl.mc_step = SDS_MC_SAVE_WAIT;                   /* 755 */
}

static void SystemDataSaveMcSaveWait(void)                              /* 763 */
{
    int mc_res;

    mc_res = MemoryCardFileSaveMain();                                  /* 770 */

    if (mc_res == 1) {                                                  /* 773 */
        system_data_save_disp.msg_id  = 8;                              /* 775 */
        system_data_save_ctrl.mc_step = SDS_MC_END_CONF;
    }
    else if (mc_res < 0) {                                              /* 778 */
        system_data_save_disp.msg_id  = 7;                              /* 779 */
        system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;         /* 780 */
    }

    if (mc_res != 0) {                                                  /* 785 */
        LiberateSystemDataSaveMem(&system_data_buff_addr);              /* 787 */
    }
}

/* --------------------------------------------------------------------------
 *  Error and end prompts
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcErrorConfInit(void)                         /* 795 */
{
    LiberateSystemDataSaveMem(&system_data_buff_addr);                  /* 799 */

    MemoryCardCheckEveryFrameInit(0, 0);                                /* 802 */

    system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_WAIT;             /* 804 */
}

/* A card pulled or swapped while the error is up restarts the whole machine
 * rather than compounding the error. */
static void SystemDataSaveMcErrorConfWait(void)                         /* 811 */
{
    int mc_res;

    SystemDataSaveMcErrorConfPad();                                     /* 819 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 822 */

    if ((mc_res == -1) ||                                               /* 825 */
        (mc_res == -2)) {                                               /* 830 */
        system_data_save_disp.msg_id  = 0;                              /* 831 */
        system_data_save_ctrl.mc_step = SDS_MC_CHECK_INIT;              /* 832 */
    }
    else if (mc_res == 1) {                                             /* 835 */
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 836 */
    }
    else if (mc_res < 0) {                                              /* 839 */
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 840 */
    }
}

/* Both buttons do the same thing -- there is nothing to choose.  Either way
 * the option screen is left, not returned to. */
static void SystemDataSaveMcErrorConfPad(void)                          /* 849 */
{
    if (*paddat[0] == 1) {                                              /* 853 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 854 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);               /* 856 */
    }
    else if (*paddat[1] == 1) {                                         /* 859 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 860 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);               /* 862 */
    }
}

/* The save-complete acknowledgement.  Byte-for-byte the same shape as
 * ErrorConfPad, and still its own function in the ROM. */
static void SystemDataSaveMcEndConf(void)                               /* 871 */
{
    if (*paddat[0] == 1) {                                              /* 875 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 876 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);               /* 878 */
    }
    else if (*paddat[1] == 1) {                                         /* 881 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 882 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);               /* 884 */
    }
}

/* --------------------------------------------------------------------------
 *  Remake: the directory is there but unusable
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcRemakeConfInit(void)                        /* 893 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 897 */

    system_data_save_ctrl.conf_csr = 1;                                 /* 899 */
    system_data_save_disp.msg_id   = 0x16;                              /* 900 */
    system_data_save_ctrl.mc_step  = SDS_MC_REMAKE_CONF_WAIT;           /* 901 */
}

static void SystemDataSaveMcRemakeConfWait(void)                        /* 908 */
{
    SystemDataSaveMcRemakeConfPad();                                    /* 912 */

    SystemDataSaveMcEveryFrameCheck();                                  /* 915 */
}

static void SystemDataSaveMcRemakeConfPad(void)                         /* 922 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 926 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 931 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 932 */
        system_data_save_ctrl.conf_csr ^= 1;                            /* 933 */
    }
    else if (*paddat[0] == 1) {                                         /* 936 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 937 */

        if (system_data_save_ctrl.conf_csr == 0) {                      /* 940 */
            system_data_save_disp.msg_id  = 0x12;                       /* 941 */
            system_data_save_ctrl.mc_step = SDS_MC_REMAKE_DIR_DEL_INIT; /* 942 */
        }
        else {
            SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);           /* 946 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 950 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 951 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_CANCEL);             /* 953 */
    }
}

static void SystemDataSaveMcRemakeDirDelInit(void)                      /* 962 */
{
    MemoryCardDirDelInit(0, 0, SYSTEM_DATA_SAVE_MC_DIR);                /* 965 */

    system_data_save_disp.msg_id  = 0x12;                               /* 967 */
    system_data_save_ctrl.mc_step = SDS_MC_REMAKE_DIR_DEL_WAIT;         /* 968 */
}

/* Deleting the old directory drops straight into making a new one -- the
 * player already agreed to both with one prompt. */
static void SystemDataSaveMcRemakeDirDelWait(void)                      /* 975 */
{
    int mc_res;

    mc_res = MemoryCardDirDelMain();                                    /* 982 */

    if (mc_res == 1) {                                                  /* 985 */
        system_data_save_disp.msg_id  = 0x12;                           /* 987 */
        system_data_save_ctrl.mc_step = SDS_MC_NEW_MAKE_INIT;
    }
    else if (mc_res < 0) {                                              /* 990 */
        system_data_save_disp.msg_id  = 0x13;                           /* 991 */
        system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;         /* 992 */
    }
}

/* --------------------------------------------------------------------------
 *  New make: there is no directory at all
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcNewMakeConfInit(void)                       /* 1001 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1005 */

    system_data_save_ctrl.conf_csr = 1;                                 /* 1007 */
    system_data_save_disp.msg_id   = 0x15;                              /* 1008 */
    system_data_save_ctrl.mc_step  = SDS_MC_NEW_MAKE_CONF_WAIT;         /* 1009 */
}

static void SystemDataSaveMcNewMakeConfWait(void)                       /* 1016 */
{
    SystemDataSaveMcNewMakeConfPad();                                   /* 1020 */

    SystemDataSaveMcEveryFrameCheck();                                  /* 1023 */
}

static void SystemDataSaveMcNewMakeConfPad(void)                        /* 1030 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1034 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 1039 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1040 */
        system_data_save_ctrl.conf_csr ^= 1;                            /* 1041 */
    }
    else if (*paddat[0] == 1) {                                         /* 1044 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1045 */

        if (system_data_save_ctrl.conf_csr == 0) {                      /* 1048 */
            system_data_save_disp.msg_id  = 0x12;                       /* 1049 */
            system_data_save_ctrl.mc_step = SDS_MC_NEW_MAKE_INIT;       /* 1050 */
        }
        else {
            SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);           /* 1054 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 1058 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1059 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_CANCEL);             /* 1061 */
    }
}

static void SystemDataSaveMcNewMakeInit(void)                           /* 1070 */
{
    int buff_size;

    buff_size = GetMemoryCardDirSize(SYSTEM_DATA_SAVE_MC_DIR);          /* 1074 */

    LiberateSystemDataSaveMem(&system_data_buff_addr);                  /* 1077 */
    GetSystemDataSaveDataMem(&system_data_buff_addr, buff_size);        /* 1079 */

    MemoryCardNewMakeInit(0, 0, SYSTEM_DATA_SAVE_MC_DIR,
                          system_data_buff_addr, buff_size);            /* 1082 */

    system_data_save_ctrl.mc_step = SDS_MC_NEW_MAKE_WAIT;               /* 1084 */
}

/* A freshly made directory's system file is all zeroes with a -1 checksum, so
 * there is nothing to merge -- the machine goes straight to the write rather
 * than reading it back first. */
static void SystemDataSaveMcNewMakeWait(void)                           /* 1091 */
{
    int mc_res;

    mc_res = MemoryCardNewMakeMain();                                   /* 1098 */

    if (mc_res == 1) {                                                  /* 1101 */
        system_data_save_disp.msg_id  = 0x1c;                           /* 1103 */
        system_data_save_ctrl.mc_step = SDS_MC_SAVE_INIT;
    }
    else if (mc_res < 0) {                                              /* 1106 */
        system_data_save_disp.msg_id  = 0x13;                           /* 1107 */
        system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;         /* 1108 */
    }

    if (mc_res != 0) {                                                  /* 1112 */
        LiberateSystemDataSaveMem(&system_data_buff_addr);              /* 1114 */
    }
}

/* --------------------------------------------------------------------------
 *  Format: the card itself is unusable
 * ------------------------------------------------------------------------ */

static void SystemDataSaveMcFormatConfInit(void)                        /* 1121 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1125 */

    system_data_save_ctrl.conf_csr = 1;                                 /* 1127 */
    system_data_save_disp.msg_id   = 3;                                 /* 1128 */
    system_data_save_ctrl.mc_step  = SDS_MC_FORMAT_CONF_WAIT;           /* 1129 */
}

/* Unlike the other confirm waits this one does not use
 * SystemDataSaveMcEveryFrameCheck(): an unformatted card reports -2 every
 * frame, and routing that through the common check would bounce the player
 * into the error screen before they could answer.  So -2 is swallowed here
 * and nowhere else. */
static void SystemDataSaveMcFormatConfWait(void)                        /* 1136 */
{
    int mc_res;

    SystemDataSaveMcFormatConfPad();                                    /* 1144 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1147 */

    if (mc_res < 0) {                                                   /* 1149 */
        switch (mc_res) {                                               /* 1150 */
        case -1:        /* card swapped */
            system_data_save_disp.msg_id  = 1;                          /* 1155 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;

        case -2:        /* still unformatted -- expected here */
            break;

        case -0x14:     /* gave up */
            system_data_save_disp.msg_id  = 1;                          /* 1161 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;

        default:
            system_data_save_disp.msg_id  = 2;                          /* 1165 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;     /* 1166 */
            break;
        }
    }
}

static void SystemDataSaveMcFormatConfPad(void)                         /* 1176 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 1180 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 1185 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1186 */
        system_data_save_ctrl.conf_csr ^= 1;                            /* 1187 */
    }
    else if (*paddat[0] == 1) {                                         /* 1190 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1191 */

        if (system_data_save_ctrl.conf_csr == 0) {                      /* 1194 */
            system_data_save_disp.msg_id  = 4;                          /* 1195 */
            system_data_save_ctrl.mc_step = SDS_MC_FORMAT_INIT;         /* 1196 */
        }
        else {
            SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_DONE);           /* 1200 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 1204 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 1205 */
        SystemDataSaveOutReq(SYSTEM_DATA_SAVE_EXIT_CANCEL);             /* 1207 */
    }
}

static void SystemDataSaveMcFormatInit(void)                            /* 1216 */
{
    MemoryCardFormatInit(0, 0);                                         /* 1219 */

    system_data_save_disp.msg_id  = 4;                                  /* 1221 */
    system_data_save_ctrl.mc_step = SDS_MC_FORMAT_WAIT;                 /* 1222 */
}

static void SystemDataSaveMcFormatWait(void)                            /* 1229 */
{
    int mc_res;

    mc_res = MemoryCardFormatMain();                                    /* 1236 */

    if (mc_res == 1) {                                                  /* 1239 */
        system_data_save_disp.msg_id  = 0x39;                           /* 1243 */
        system_data_save_ctrl.mc_step = SDS_MC_FORMAT_END_INIT;
    }
    else if (mc_res < 0) {                                              /* 1250 */
        system_data_save_disp.msg_id  = 5;                              /* 1251 */
        system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;         /* 1252 */
    }
}

/* The format_end_cnt store is in the `jr ra` delay slot, so it carries the
 * function's first line rather than its own -- it is the first statement, not
 * the last.  See [[empty-looking-functions-may-have-delay-slot-bodies]]. */
static void SystemDataSaveMcFormatEndInit(void)                         /* 1262 */
{
    system_data_save_ctrl.format_end_cnt = 0;                           /* 1265 */

    system_data_save_disp.msg_id         = 0x39;                        /* 1267 */
    system_data_save_ctrl.mc_step        = SDS_MC_FORMAT_END_WAIT;      /* 1268 */
}

/* Two seconds of "card formatted", then straight on to making the directory --
 * the player is not asked again. */
static void SystemDataSaveMcFormatEndWait(void)                         /* 1277 */
{
    system_data_save_ctrl.format_end_cnt++;                             /* 1281 */

    if (system_data_save_ctrl.format_end_cnt >
        SYSTEM_DATA_SAVE_FORMAT_END_TIME - 1) {                         /* 1284 */
        system_data_save_disp.msg_id  = 0x12;                           /* 1286 */
        system_data_save_ctrl.mc_step = SDS_MC_NEW_MAKE_INIT;           /* 1287 */
    }
}

/* The card watch every interactive state runs: a pull, a swap or a hard error
 * takes the screen to the error prompt with a message id. */
static void SystemDataSaveMcEveryFrameCheck(void)                       /* 1296 */
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1304 */

    if (mc_res < 0) {                                                   /* 1306 */
        switch (mc_res) {                                               /* 1307 */
        case -1:        /* card swapped */
            system_data_save_disp.msg_id  = 1;                          /* 1312 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;
        case -2:        /* not formatted */
            system_data_save_disp.msg_id  = 0xd;                        /* 1317 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;
        case -0x14:     /* gave up */
            system_data_save_disp.msg_id  = 1;                          /* 1321 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;
            break;
        default:
            system_data_save_disp.msg_id  = 2;                          /* 1325 */
            system_data_save_ctrl.mc_step = SDS_MC_ERROR_CONF_INIT;     /* 1326 */
            break;
        }
    }
}

/* ==========================================================================
 *  Tear-down
 * ======================================================================== */

void SystemDataSaveEnd(void)                                            /* 1338 */
{
    LiberateSystemDataSaveMem(&system_data_buff_addr);                  /* 1342 */

    MemoryCardEnd();                                                    /* 1345 */

    SystemDataSaveMemGet  = nullptr;                                    /* 1347 */
    SystemDataSaveMemFree = nullptr;                                    /* 1348 */
}

static void LiberateSystemDataSaveMem(void **tex_addr)                  /* 1356 */
{
    if (*tex_addr != nullptr) {                                         /* 1359 */
        SystemDataSaveMemFree(*tex_addr);                               /* 1360 */
        *tex_addr = nullptr;                                            /* 1361 */
    }
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void SystemDataSaveDispInit(void)                                /* 1374 */
{
    system_data_save_disp.msg_id     = 0;                               /* 1377 */
    system_data_save_disp.anim_step  = ZERO2_ANIM2D_STEP_START;         /* 1378 */
    system_data_save_disp.anim_timer = 0;                               /* 1379 */
}

/* Two layouts: the four yes/no states get the selection window, everything
 * else the plain card message window, and both then get the message text.
 * There is no step guard here -- unlike GameDataSaveDispMain() this runs
 * whenever option.c's window is 2, which it only is once Main() has been
 * entered at least once. */
void SystemDataSaveDispMain(void)                                       /* 1387 */
{
    u_char alpha;

    alpha = Zero2Anim2D_InOutAnimCtrl(&system_data_save_disp.anim_step,
                                      &system_data_save_disp.anim_timer,
                                      10, 5);                           /* 1396 */

    switch (system_data_save_ctrl.mc_step) {                            /* 1398 */
    case SDS_MC_SAVE_CONF_INIT:
    case SDS_MC_SAVE_CONF_WAIT:
    case SDS_MC_REMAKE_CONF_INIT:
    case SDS_MC_REMAKE_CONF_WAIT:
    case SDS_MC_NEW_MAKE_CONF_INIT:
    case SDS_MC_NEW_MAKE_CONF_WAIT:
    case SDS_MC_FORMAT_CONF_INIT:
    case SDS_MC_FORMAT_CONF_WAIT:
        SaveLoadMcSelYesNoWinDisp(0, 0, alpha,
                                  (int)system_data_save_ctrl.conf_csr); /* 1408 */
        break;                                                          /* 1409 */

    default:
        SaveLoadMcStateMsgWinDisp(0, 0, alpha);                         /* 1412 */
        break;
    }

    SaveLoadMcStateMsgDisp(0, 0, alpha, system_data_save_disp.msg_id);  /* 1416 */
}
