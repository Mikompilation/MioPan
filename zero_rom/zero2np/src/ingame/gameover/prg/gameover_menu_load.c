// FILE: /home/zero_rom/zero2np/src/ingame/gameover/prg/gameover_menu_load.c
//
// The game-over menu's load screen: pick one of five memory-card slots and
// resume from it.  The folder's largest translation unit (0x1b48).
//
// Read outgame/loadgame.c first.  This is that screen with a wrapper around
// it -- the same fifteen-step card machine, the same five slots, the same
// delegation of every pixel to save_load_disp.c -- and most of the work here
// was recognising the twin.  Three things genuinely differ:
//
//   * It has a screen state of its own (gameover_load_ctrl.step), so it can
//     animate in and out over the game-over menu rather than being a top-level
//     phase.  GameOverLoadReturnMenu() is how every cancel path leaves.
//   * It loads its own copy of OUTGAME_PK2 into gameover_load_cmn_tex_addr.
//     loadgame.c borrows the title screen's through GetOutGameCmnTexAddr(),
//     which does not exist here -- the title screen is not resident.
//   * A successful load goes to GID_STORY_LOAD_MISSION_SAVE, or to
//     GID_TITLE_SETUPMENU if the file has been cleared at least once.
//
// The card machine's Init steps fall through into their Wait partners, so a
// request is issued and polled on the same frame; that fall-through is the
// ROM's own, and the jump table at .rodata 3b3ea0 has separate entries for
// both halves.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
// gameover_menu_load.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.

#include "gameover_menu_load.h"

#include "../../ingame.h"                           /* IngameWrkInit          */
#include "../../loading/loading.h"                  /* GetLoadingTexMem       */
#include "../../menu/tim_dat/map_room_dat.h"        /* room_info_dat          */
#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_*          */
#include "../../../common/mem_util.h"               /* mem_utilGetMem         */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../common/variable.h"               /* ingame_wrk / opt_wrk   */
#include "../../../graphics/graph3d/ctl/fixed_array.h" /* reference_fixed_array */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../outgame/option.h"                /* OptSoundSetupRef       */
#include "../../../save_load/prg/save_load_disp.h"  /* SaveLoad*Disp          */
#include "../../../system/eeiop/cddat.h"            /* SAVE_LOAD_PK2 / file ids */
#include "../../../system/eeiop/fileload.h"         /* FileLoadReqEE          */
#include "../../../system/eeiop/snd3d.h"            /* SND_3D_SET             */
#include "../../../system/mc/prg/mc.h"              /* MemoryCardExeInit      */
#include "../../../system/mc/prg/mc_check.h"        /* MemoryCardCheckInit    */
#include "../../../system/mc/prg/mc_check_broken.h" /* MemoryCardCheck*Broken */
#include "../../../system/mc/prg/mc_check_card.h"   /* MemoryCardCheckEveryFrame* */
#include "../../../system/mc/prg/mc_load.h"         /* MemoryCardFileLoad*    */
#include "../../../system/mc/prg/mc_set_data.h"     /* path / size / accessors */
#include "../../../system/os/system.h"              /* GetLanguage / SystemBankPlay */
#include "../../../system/pad/pad.h"                /* pad / paddat           */

#define GAMEOVER_LOAD_DATA_MAX  5       /* memory-card save slots */

/* Steps of gameover_load_ctrl.mc_step.  The ROM's debug info carries no enum
 * for these -- the names are taken from the step handlers' own ROM symbol
 * names, the same convention loadgame.c uses, so the values still read
 * against the jump table at rodata 3b3ea0. */
enum GAMEOVER_LOAD_MC_STEP
{
    GAMEOVER_LOAD_MC_CHECK_INIT      = 0,
    GAMEOVER_LOAD_MC_CHECK_WAIT      = 1,
    GAMEOVER_LOAD_MC_HEAD_LOAD_INIT  = 2,
    GAMEOVER_LOAD_MC_HEAD_LOAD_WAIT  = 3,
    GAMEOVER_LOAD_MC_SNAP_LOAD_INIT  = 4,
    GAMEOVER_LOAD_MC_SNAP_LOAD_WAIT  = 5,
    GAMEOVER_LOAD_MC_LOAD_FILE_SEL   = 6,
    GAMEOVER_LOAD_MC_LOAD_CONF       = 7,
    GAMEOVER_LOAD_MC_SYS_LOAD_INIT   = 8,
    GAMEOVER_LOAD_MC_SYS_LOAD_WAIT   = 9,
    GAMEOVER_LOAD_MC_LOAD_INIT       = 10,
    GAMEOVER_LOAD_MC_LOAD_WAIT       = 11,
    GAMEOVER_LOAD_MC_ERROR_CONF_INIT = 12,
    GAMEOVER_LOAD_MC_ERROR_CONF_WAIT = 13,
    GAMEOVER_LOAD_MC_END_CONF        = 14
};

/* Memory-card file indices within the save directory: 0 is the system file,
 * 1 the header, and slot n is n + 2. */
#define GAMEOVER_LOAD_MC_FILE_SYSTEM 0
#define GAMEOVER_LOAD_MC_FILE_HEAD   1
#define GAMEOVER_LOAD_MC_FILE_DATA   2

/* Path buffers are memset to 0x37 bytes by every caller; that is the ROM's
 * literal size, not sizeof(). */
#define GAMEOVER_LOAD_PATH_NAME_LEN  55

/* Window open / close times, in frames. */
#define GAMEOVER_LOAD_ANIM_IN_TIME   10
#define GAMEOVER_LOAD_ANIM_OUT_TIME  5

static void *gameover_load_tex_addr;                        /* sdata 3f0fd0 */
static void *gameover_load_cmn_tex_addr;                    /* sdata 3f0fd4 */
static void *load_data_buff;                                /* sdata 3f0fd8 */
static void *gameover_load_snap_addr[GAMEOVER_LOAD_DATA_MAX]; /* data 316c60 */

/* Backing store for gameover_load_snap_tex below.  The ROM put this in
 * gameover_menu_load.o's .rodata (3b4070, the last 20 bytes of the section --
 * globals.txt does not list it at all) and wrote to it anyway: the EE has no
 * write protection on data pages, so nothing complained.  The host does, so
 * it has to be writable here.  loadgame.c carries the identical deviation. */
static int gameover_load_snap_tex_dat[GAMEOVER_LOAD_DATA_MAX] =
    { -1, -1, -1, -1, -1 };

/* File number of each slot's snapshot texture, bound at static-init time --
 * this object's single .ctors entry (0x2c3bb8).  A reference_fixed_array, not
 * a plain int[5]: globals.txt types it that way and every subscript in the
 * ROM carries the inlined _fixed_array_verifyrange<int>(i, 5) bounds check. */
static reference_fixed_array<int, GAMEOVER_LOAD_DATA_MAX>
    gameover_load_snap_tex(gameover_load_snap_tex_dat);     /* sbss 3f4ca0 */

static GAMEOVER_LOAD_CTRL gameover_load_ctrl;               /* sbss 3f4ca8 */
static GAMEOVER_LOAD_DISP gameover_load_disp;               /* sbss 3f4cb0 */

static void GameOverLoadCtrlInit(void);
static void GameOverLoadReturnMenu(void);
static void GameOverLoadMcMain(void);
static void GameOverLoadMcCheckInit(void);
static void GameOverLoadMcCheckWait(void);
static void GameOverLoadMcHeadLoadInit(void);
static void GameOverLoadMcHeadLoadWait(void);
static void GameOverLoadMcSnapLoadInit(void);
static void GameOverLoadMcSnapLoadWait(void);
static void GameOverLoadMcLoadFileSel(void);
static void GameOverLoadMcLoadFileSelPad(void);
static void GameOverLoadMcLoadConf(void);
static void GameOverLoadMcLoadConfPad(void);
static void GameOverLoadMcSysLoadInit(void);
static void GameOverLoadMcSysLoadWait(void);
static void GameOverLoadMcLoadInit(void);
static void GameOverLoadMcLoadWait(void);
static void GameOverLoadMcErrorConfInit(void);
static void GameOverLoadMcErrorConfWait(void);
static void GameOverLoadMcErrorConfPad(void);
static void GameOverLoadMcEndConf(void);
static void GameOverLoadMcEndConfPad(void);
static void GameOverLoadMcEveryFrameCheck(void);
static void GameOverLoadDispInit(void);
static void GameOverLoadFileSelDisp(u_char alpha);

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* Phase entry.  Clears the ingame work area, resets both control blocks and
 * drops anything a previous visit to the screen left allocated. */
void GameOverLoadInit(void)                                             /* 212 */
{
    int i;

    IngameWrkInit(0, 0);                                                /* 218 */

    GameOverLoadCtrlInit();                                             /* 221 */
    GameOverLoadDispInit();                                             /* 224 */

    MemoryCardExeInit();                                                /* 228 */

    if (load_data_buff != (void *)0) {                                  /* 232 */
        LiberateDataMemoryArea(load_data_buff);                         /* 233 */
        load_data_buff = (void *)0;                                     /* 234 */
    }

    for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                      /* 237 */
        if (gameover_load_snap_addr[i] != (void *)0) {                  /* 239 */
            LiberateDataMemoryArea(gameover_load_snap_addr[i]);         /* 240 */
            gameover_load_snap_addr[i] = (void *)0;                     /* 241 */
        }

        gameover_load_snap_tex[i] = -1;
    }                                                                   /* 245 */
}

/* wait_timer is written here and never read anywhere in the object. */
static void GameOverLoadCtrlInit(void)                                  /* 253 */
{
    gameover_load_ctrl.step       = GAMEOVER_LOAD_STEP_DATA_LOAD_WAIT;  /* 256 */
    gameover_load_ctrl.mc_step    = GAMEOVER_LOAD_MC_CHECK_INIT;        /* 257 */
    gameover_load_ctrl.csr        = 0;                                  /* 258 */
    gameover_load_ctrl.conf_csr   = 1;                                  /* 259 */
    gameover_load_ctrl.wait_timer = 0;                                  /* 260 */
}

/* ==========================================================================
 *  Texture paks
 *
 *  Two of them.  SAVE_LOAD_PK2's per-language variants sit immediately after
 *  it in the file table, so the language index is simply added to the base
 *  file number; OUTGAME_PK2 carries the shared title frame.  loadgame.c takes
 *  the latter from GetOutGameCmnTexAddr() instead -- it runs under the title
 *  screen, which owns that pak, and this screen does not.
 * ======================================================================== */

void GetGameOverLoadTexMem(void)                                        /* 268 */
{
    ReleaseGameOverLoadTexMem();                                        /* 271 */

    if (gameover_load_tex_addr == (void *)0) {                          /* 274 */
        gameover_load_tex_addr =
            mem_utilGetMem((int)GetFileSize(SAVE_LOAD_PK2 + (char)GetLanguage())); /* 276 */
    }
    else {
        /* ROM emits the SetAssertPreMessage / PrintAssertReal pair here.
         * Note the literal function name rather than __FUNCTION__ -- both asserts in
         * this function spell it out. */
        PRINT_ASSERT("Error! GetGameOverLoadTexMem");                   /* 279 */
    }

    if (gameover_load_cmn_tex_addr == (void *)0) {                      /* 282 */
        gameover_load_cmn_tex_addr =
            mem_utilGetMem((int)GetFileSize(OUTGAME_PK2));              /* 284 */
    }
    else {
        PRINT_ASSERT("Error! GetGameOverLoadTexMem");                   /* 287 */
    }
}

void GameOverLoadDataLoadReq(void)                                      /* 296 */
{
    FileLoadReqEE(SAVE_LOAD_PK2 + (char)GetLanguage(), gameover_load_tex_addr,
                  6, (FILE_LOAD_CALLBACK)0, (void *)0);                 /* 300 */
    FileLoadReqEE(OUTGAME_PK2, gameover_load_cmn_tex_addr,
                  6, (FILE_LOAD_CALLBACK)0, (void *)0);                 /* 301 */
}

/* The common pak is only tested once the language pak has arrived, so a
 * failure on the first leaves the second unexamined. */
int GameOverLoadDataLoadWait(void)                                      /* 313 */
{
    if (FileLoadIsEnd2(SAVE_LOAD_PK2 + (char)GetLanguage(),
                       gameover_load_tex_addr) != 0) {                  /* 320 */
        return FileLoadIsEnd2(OUTGAME_PK2, gameover_load_cmn_tex_addr) != 0; /* 321 */
    }

    return 0;                                                           /* 327 */
}

void ReleaseGameOverLoadTexMem(void)                                    /* 1307 */
{
    if (gameover_load_tex_addr != (void *)0) {                          /* 1310 */
        mem_utilFreeMem(gameover_load_tex_addr);                        /* 1311 */
        gameover_load_tex_addr = (void *)0;                             /* 1312 */
    }

    if (gameover_load_cmn_tex_addr != (void *)0) {                      /* 1315 */
        mem_utilFreeMem(gameover_load_cmn_tex_addr);                    /* 1316 */
        gameover_load_cmn_tex_addr = (void *)0;                         /* 1317 */
    }
}

/* ==========================================================================
 *  The screen's own three steps
 * ======================================================================== */

void GameOverLoadMain(void)                                             /* 337 */
{
    switch (gameover_load_ctrl.step) {                                  /* 340 */
    case GAMEOVER_LOAD_STEP_DATA_LOAD_WAIT:
        if (GameOverLoadDataLoadWait() != 0) {                          /* 342 */
            gameover_load_ctrl.step    = GAMEOVER_LOAD_STEP_MC;         /* 343 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_CHECK_INIT;   /* 346 */
        }
        break;

    case GAMEOVER_LOAD_STEP_MC:
        GameOverLoadMcMain();                                           /* 350 */
        break;

    case GAMEOVER_LOAD_STEP_RETURN_MENU:
        /* Wait for the screen to finish closing, then give the menu back. */
        if (gameover_load_disp.anim_step == ZERO2_ANIM2D_STEP_END) {    /* 371 */
            SetNextGPhase(GID_GAMEOVER_MENU_TOP);                       /* 373 */
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 377 */
        break;
    }
}

/* Every cancel path funnels through here: start the closing animation and let
 * step 2 above do the phase change once it has finished. */
static void GameOverLoadReturnMenu(void)                                /* 386 */
{
    gameover_load_ctrl.step       = GAMEOVER_LOAD_STEP_RETURN_MENU;     /* 389 */

    gameover_load_disp.anim_step  = ZERO2_ANIM2D_STEP_OUT;              /* 390 */
    gameover_load_disp.anim_timer = 0;                                  /* 391 */
}

/* ==========================================================================
 *  The card machine
 *
 *  Every *Init step falls through into its *Wait partner so the wait runs on
 *  the same frame it was set up.  Jump table at rodata 3b3ea0.
 * ======================================================================== */

static void GameOverLoadMcMain(void)                                    /* 400 */
{
    switch (gameover_load_ctrl.mc_step) {                               /* 403 */
    case GAMEOVER_LOAD_MC_CHECK_INIT:
        GameOverLoadMcCheckInit();                                      /* 405 */
        /* fall through */
    case GAMEOVER_LOAD_MC_CHECK_WAIT:
        GameOverLoadMcCheckWait();                                      /* 408 */
        break;

    case GAMEOVER_LOAD_MC_HEAD_LOAD_INIT:
        GameOverLoadMcHeadLoadInit();                                   /* 411 */
        /* fall through */
    case GAMEOVER_LOAD_MC_HEAD_LOAD_WAIT:
        GameOverLoadMcHeadLoadWait();                                   /* 414 */
        break;

    case GAMEOVER_LOAD_MC_SNAP_LOAD_INIT:
        GameOverLoadMcSnapLoadInit();                                   /* 417 */
        /* fall through */
    case GAMEOVER_LOAD_MC_SNAP_LOAD_WAIT:
        GameOverLoadMcSnapLoadWait();                                   /* 420 */
        break;

    case GAMEOVER_LOAD_MC_LOAD_FILE_SEL:
        GameOverLoadMcLoadFileSel();                                    /* 423 */
        break;

    case GAMEOVER_LOAD_MC_LOAD_CONF:
        GameOverLoadMcLoadConf();                                       /* 426 */
        break;

    case GAMEOVER_LOAD_MC_SYS_LOAD_INIT:
        GameOverLoadMcSysLoadInit();                                    /* 429 */
        /* fall through */
    case GAMEOVER_LOAD_MC_SYS_LOAD_WAIT:
        GameOverLoadMcSysLoadWait();                                    /* 432 */
        break;

    case GAMEOVER_LOAD_MC_LOAD_INIT:
        GameOverLoadMcLoadInit();                                       /* 435 */
        /* fall through */
    case GAMEOVER_LOAD_MC_LOAD_WAIT:
        GameOverLoadMcLoadWait();                                       /* 438 */
        break;

    case GAMEOVER_LOAD_MC_ERROR_CONF_INIT:
        GameOverLoadMcErrorConfInit();                                  /* 441 */
        /* fall through */
    case GAMEOVER_LOAD_MC_ERROR_CONF_WAIT:
        GameOverLoadMcErrorConfWait();                                  /* 444 */
        break;

    case GAMEOVER_LOAD_MC_END_CONF:
        GameOverLoadMcEndConf();                                        /* 447 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 450 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Step 0/1 -- is there a card, and is our save directory intact?
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcCheckInit(void)                               /* 459 */
{
    char path_name[GAMEOVER_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                            /* 463 */

    MemoryCardMakeSearchDirPath(path_name, 0);                          /* 467 */
    MemoryCardCheckInit(0, 0, path_name);                               /* 469 */

    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_CHECK_WAIT;           /* 471 */
}

static void GameOverLoadMcCheckWait(void)                               /* 479 */
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 485 */

    if (mc_res == 1) {                                                  /* 488 */
        if (MemoryCardCheckDirBroken(0) != 0) {                         /* 490 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_HEAD_LOAD_INIT;
            gameover_load_disp.msg_id  = 0;
            return;
        }

        gameover_load_disp.msg_id = 0x19;                               /* 498 */
    }
    else {
        if (mc_res >= 0) {                                              /* 502 */
            return;
        }

        switch (mc_res) {                                               /* 504 */
        case -1:                                                        /* 506 */
            /* A swapped card is not an error -- start over on the new one. */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_HEAD_LOAD_INIT; /* 507 */
            gameover_load_disp.msg_id  = 0;                             /* 508 */
            return;

        case -2:                                                        /* 512 */
            gameover_load_disp.msg_id = 0x18;
            break;

        case -4:                                                        /* 516 */
            gameover_load_disp.msg_id = 0x18;
            break;

        case -6:                                                        /* 520 */
            gameover_load_disp.msg_id = 0x19;
            break;

        case -0x14:                                                     /* 524 */
            gameover_load_disp.msg_id = 1;
            break;

        default:
            gameover_load_disp.msg_id = 2;                              /* 528 */
            break;
        }
    }

    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;      /* 529 */
}

/* --------------------------------------------------------------------------
 *  Step 2/3 -- the header file, which carries every slot's summary.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcHeadLoadInit(void)                            /* 539 */
{
    int  size;
    char path_name[GAMEOVER_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                            /* 545 */

    if (load_data_buff != (void *)0) {                                  /* 548 */
        LiberateDataMemoryArea(load_data_buff);                         /* 549 */
        load_data_buff = (void *)0;                                     /* 550 */
    }

    MemoryCardSetFilePath(path_name, 0, GAMEOVER_LOAD_MC_FILE_HEAD);    /* 555 */
    size = GetMemoryCardDataSize(0, GAMEOVER_LOAD_MC_FILE_HEAD);        /* 557 */
    load_data_buff = GetDataMemoryArea(size);                           /* 559 */

    MemoryCardFileLoadInit(0, 0, path_name, load_data_buff, size);      /* 561 */

    gameover_load_disp.msg_id  = 0;                                     /* 564 */
    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_HEAD_LOAD_WAIT;       /* 566 */
}

static void GameOverLoadMcHeadLoadWait(void)                            /* 574 */
{
    int mc_res;
    int size;
    int i;

    mc_res = MemoryCardFileLoadMain();                                  /* 584 */

    if (mc_res == 1) {                                                  /* 587 */
        size = GetMemoryCardDataSize(0, GAMEOVER_LOAD_MC_FILE_HEAD);    /* 589 */

        if (MemoryCardCheckFileBroken(load_data_buff, size) == 0) {     /* 592 */
            gameover_load_disp.msg_id  = 0x19;                          /* 594 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;
        }
        else if (MemoryCardCheckNewFileLoad(load_data_buff, size) != 0) { /* 602 */
            gameover_load_disp.msg_id  = 0x21;                          /* 605 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;
        }
        else {
            DevelopMemoryCardLoadData((char *)load_data_buff, 0,
                                      GAMEOVER_LOAD_MC_FILE_HEAD);      /* 606 */

            /* Default to "no save data" and let any populated slot promote us
             * to the snapshot load. */
            gameover_load_disp.msg_id  = 0x21;                          /* 609 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT; /* 610 */

            for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {              /* 611 */
                if (GetMemoryCardPlayDataFlg(i) == 1) {                 /* 612 */
                    gameover_load_disp.msg_id  = 0;                     /* 614 */
                    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_SNAP_LOAD_INIT; /* 619 */
                }
            }                                                           /* 620 */
        }
    }
    else if (mc_res < 0) {                                              /* 624 */
        switch (mc_res) {                                               /* 626 */
        case -2:                                                        /* 629 */
            gameover_load_disp.msg_id = 0x18;
            break;

        case -3:                                                        /* 632 */
            gameover_load_disp.msg_id = 0x19;
            break;

        default:
            gameover_load_disp.msg_id = 0xf;                            /* 638 */
            break;
        }

        gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;  /* 641 */
    }

    /* Any answer other than "still busy" is done with the staging buffer. */
    if ((mc_res != 0) && (load_data_buff != (void *)0)) {               /* 646 */
        LiberateDataMemoryArea(load_data_buff);                         /* 647 */
        load_data_buff = (void *)0;                                     /* 648 */
    }
}                                                                       /* 649 */

/* --------------------------------------------------------------------------
 *  Step 4/5 -- one snapshot texture per populated slot.
 *
 *  A cleared slot shows the fixed SNP_CLEAR_DATA_PK2 image; otherwise the
 *  slot's room label is looked up in room_info_dat[] to find the room's
 *  snapshot texture.  A room the table does not know leaves the previous file
 *  number in place and loads nothing, which is the ROM's behaviour.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcSnapLoadInit(void)                            /* 657 */
{
    int i;
    int j;

    for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                      /* 663 */
        if (GetMemoryCardPlayDataFlg(i) == 1) {                         /* 664 */
            if (gameover_load_snap_addr[i] != (void *)0) {              /* 666 */
                LiberateDataMemoryArea(gameover_load_snap_addr[i]);     /* 667 */
                gameover_load_snap_addr[i] = (void *)0;                 /* 668 */
            }

            if (GetMemoryCardClearDataFlg(i) != 0) {                    /* 672 */
                gameover_load_snap_tex[i] = SNP_CLEAR_DATA_PK2;         /* 683 */
            }
            else {
                for (j = 0; room_info_dat[j].map_label != -1; j++) {    /* 684 */
                    if (room_info_dat[j].room_label ==
                            GetMemoryCardPlayDataRoomLabel(i)) {        /* 688 */
                        gameover_load_snap_tex[i] =
                            room_info_dat[j].snap_tex_label;            /* 695 */
                        break;
                    }
                }
            }

            gameover_load_snap_addr[i] =
                GetDataMemoryArea((int)GetFileSize(gameover_load_snap_tex[i])); /* 702, 703 */

            FileLoadReqEE(gameover_load_snap_tex[i], gameover_load_snap_addr[i],
                          5, (FILE_LOAD_CALLBACK)0, (void *)0);         /* 704 */
        }
        else {
            if (gameover_load_snap_addr[i] != (void *)0) {              /* 709 */
                LiberateDataMemoryArea(gameover_load_snap_addr[i]);     /* 712 */
                gameover_load_snap_addr[i] = (void *)0;
            }

            gameover_load_snap_tex[i] = -1;                             /* 714 */
        }
    }

    MemoryCardCheckEveryFrameInit(0, 0);                                /* 715 */

    gameover_load_disp.msg_id  = 0;
    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_SNAP_LOAD_WAIT;
}

/* The first populated slot the loop reaches becomes the cursor's home, which
 * is what csr_set_flg exists to latch. */
static void GameOverLoadMcSnapLoadWait(void)                            /* 722 */
{
    int i;
    int csr_set_flg;

    csr_set_flg = 0;                                                    /* 727 */

    for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                      /* 730 */
        if (GetMemoryCardPlayDataFlg(i) == 1) {                         /* 732 */
            if (FileLoadIsEnd2(gameover_load_snap_tex[i],
                               gameover_load_snap_addr[i]) == 0) {      /* 735 */
                gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_SNAP_LOAD_WAIT; /* 736 */
                gameover_load_disp.msg_id  = 0;
                break;
            }

            gameover_load_disp.msg_id  = 0x22;                          /* 740 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_LOAD_FILE_SEL; /* 741 */

            if (csr_set_flg == 0) {                                     /* 742 */
                gameover_load_ctrl.csr = (char)i;                       /* 746 */
                csr_set_flg = 1;                                        /* 747 */
            }
        }
    }                                                                   /* 748 */

    GameOverLoadMcEveryFrameCheck();                                    /* 751 */
}                                                                       /* 754 */

/* --------------------------------------------------------------------------
 *  Step 6 -- pick a slot.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcLoadFileSel(void)                             /* 762 */
{
    GameOverLoadMcLoadFileSelPad();                                     /* 766 */
    GameOverLoadMcEveryFrameCheck();                                    /* 769 */
}

/* LEFT and RIGHT skip empty slots: the search runs at most five times, so a
 * card with a single populated slot simply lands back on it. */
static void GameOverLoadMcLoadFileSelPad(void)                          /* 777 */
{
    int i;

    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2)) {                  /* 782 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 783 */

        for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                  /* 784 */
            gameover_load_ctrl.csr =
                (char)((gameover_load_ctrl.csr + 4) % GAMEOVER_LOAD_DATA_MAX); /* 785 */

            if (GetMemoryCardPlayDataFlg(gameover_load_ctrl.csr) == 1) { /* 788 */
                break;                                                  /* 789 */
            }
        }
    }
    else if ((pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {             /* 791 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 794 */

        for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                  /* 795 */
            gameover_load_ctrl.csr =
                (char)((gameover_load_ctrl.csr + 1) % GAMEOVER_LOAD_DATA_MAX); /* 796 */

            if (GetMemoryCardPlayDataFlg(gameover_load_ctrl.csr) == 1) { /* 797 */
                break;                                                  /* 800 */
            }
        }
    }
    else if (*paddat[0] == 1) {                                         /* 801 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 803 */

        gameover_load_ctrl.mc_step  = GAMEOVER_LOAD_MC_LOAD_CONF;       /* 806 */
        gameover_load_ctrl.conf_csr = 1;                                /* 807 */
        gameover_load_disp.msg_id   = 0xe;                              /* 809 */
    }
    else if (*paddat[1] == 1) {                                         /* 810 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 811 */

        GameOverLoadReturnMenu();                                       /* 814 */
    }
}                                                                       /* 817 */

/* --------------------------------------------------------------------------
 *  Step 7 -- "load this file?"
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcLoadConf(void)                                /* 826 */
{
    GameOverLoadMcLoadConfPad();                                        /* 830 */
    GameOverLoadMcEveryFrameCheck();                                    /* 833 */
}

static void GameOverLoadMcLoadConfPad(void)                             /* 840 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 844 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 849 */

        gameover_load_ctrl.conf_csr =
            (char)(gameover_load_ctrl.conf_csr ^ 1);                    /* 850 */
    }
    else if (*paddat[0] == 1) {                                         /* 851 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 854 */

        if (gameover_load_ctrl.conf_csr == 0) {                         /* 855 */
            gameover_load_disp.msg_id  = 0x1d;                          /* 857 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_SYS_LOAD_INIT; /* 858 */
        }
        else {
            gameover_load_disp.msg_id  = 0x22;                          /* 859 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_LOAD_FILE_SEL;
        }
    }
    else if (*paddat[1] == 1) {                                         /* 863 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 864 */

        gameover_load_disp.msg_id  = 0x22;                              /* 868 */
        gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_LOAD_FILE_SEL;    /* 869 */
    }
}                                                                       /* 872 */

/* --------------------------------------------------------------------------
 *  Step 8/9 -- the system file, read before the slot so the option settings
 *  the save was made under are restored first.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcSysLoadInit(void)                             /* 881 */
{
    int  size;
    char path_name[GAMEOVER_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                            /* 887 */

    if (load_data_buff != (void *)0) {                                  /* 890 */
        LiberateDataMemoryArea(load_data_buff);                         /* 891 */
        load_data_buff = (void *)0;                                     /* 892 */
    }

    MemoryCardSetFilePath(path_name, 0, GAMEOVER_LOAD_MC_FILE_SYSTEM);  /* 897 */
    size = GetMemoryCardDataSize(0, GAMEOVER_LOAD_MC_FILE_SYSTEM);      /* 899 */
    load_data_buff = GetDataMemoryArea(size);                           /* 901 */

    MemoryCardFileLoadInit(0, 0, path_name, load_data_buff, size);      /* 903 */

    gameover_load_disp.msg_id  = 0x1d;                                  /* 905 */
    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_SYS_LOAD_WAIT;        /* 906 */
}

static void GameOverLoadMcSysLoadWait(void)                             /* 913 */
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();                                  /* 922 */

    if (mc_res == 1) {                                                  /* 925 */
        size = GetMemoryCardDataSize(0, GAMEOVER_LOAD_MC_FILE_SYSTEM);  /* 927 */

        if ((MemoryCardCheckFileBroken(load_data_buff, size) == 0) ||   /* 930 */
            (MemoryCardCheckNewFileLoad(load_data_buff, size) != 0)) {  /* 932 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;
            gameover_load_disp.msg_id  = 0x33;
        }
        else {
            DevelopMemoryCardLoadData((char *)load_data_buff, 0,
                                      GAMEOVER_LOAD_MC_FILE_SYSTEM);    /* 942 */

            /* Push the loaded sound settings into the live mixer. */
            OptSoundSetupRef(&opt_wrk);                                 /* 944 */

            gameover_load_disp.msg_id  = 0x20;                          /* 946 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_LOAD_INIT;    /* 947 */
        }
    }
    else if (mc_res < 0) {                                              /* 952 */
        switch (mc_res) {                                               /* 953 */
        case -2:                                                        /* 957 */
            gameover_load_disp.msg_id = 0x18;
            break;

        case -3:                                                        /* 959 */
            gameover_load_disp.msg_id = 0x33;
            break;

        default:
            gameover_load_disp.msg_id = 0xf;                            /* 962 */
            break;
        }

        gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;  /* 965 */
    }

    if ((mc_res != 0) && (load_data_buff != (void *)0)) {               /* 971 */
        LiberateDataMemoryArea(load_data_buff);                         /* 974 */
        load_data_buff = (void *)0;                                     /* 979 */
    }
}                                                                       /* 982 */

/* --------------------------------------------------------------------------
 *  Step 10/11 -- the chosen slot itself.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcLoadInit(void)                                /* 990 */
{
    int  size;
    char path_name[GAMEOVER_LOAD_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                            /* 996 */

    if (load_data_buff != (void *)0) {                                  /* 999 */
        LiberateDataMemoryArea(load_data_buff);                         /* 1000 */
        load_data_buff = (void *)0;                                     /* 1001 */
    }

    MemoryCardSetFilePath(path_name, 0,
                          gameover_load_ctrl.csr + GAMEOVER_LOAD_MC_FILE_DATA); /* 1006 */
    size = GetMemoryCardDataSize(0,
                          gameover_load_ctrl.csr + GAMEOVER_LOAD_MC_FILE_DATA); /* 1008 */
    load_data_buff = GetDataMemoryArea(size);                           /* 1010 */

    MemoryCardFileLoadInit(0, 0, path_name, load_data_buff, size);      /* 1012 */

    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_LOAD_WAIT;            /* 1014 */
    gameover_load_disp.msg_id  = 0x20;                                  /* 1015 */
}

/* On success the loading screen's own pak is requested here, so it is already
 * in flight while the player is still reading the "press a button" prompt. */
static void GameOverLoadMcLoadWait(void)                                /* 1022 */
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();                                  /* 1031 */

    if (mc_res == 1) {                                                  /* 1034 */
        size = GetMemoryCardDataSize(0,
                          gameover_load_ctrl.csr + GAMEOVER_LOAD_MC_FILE_DATA); /* 1036 */

        if ((MemoryCardCheckFileBroken(load_data_buff, size) == 0) ||   /* 1039 */
            (MemoryCardCheckNewFileLoad(load_data_buff, size) != 0)) {  /* 1041 */
            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;
            gameover_load_disp.msg_id  = 0x34;
        }
        else {
            DevelopMemoryCardLoadData((char *)load_data_buff, 0,
                          gameover_load_ctrl.csr + GAMEOVER_LOAD_MC_FILE_DATA); /* 1050 */

            GetLoadingTexMem();                                         /* 1053 */
            LoadingTexLoadReq();                                        /* 1055 */

            gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_END_CONF;     /* 1057 */
            gameover_load_disp.msg_id  = 0x10;                          /* 1058 */
        }
    }
    else if (mc_res < 0) {                                              /* 1063 */
        switch (mc_res) {                                               /* 1064 */
        case -2:                                                        /* 1068 */
            gameover_load_disp.msg_id = 0x18;
            break;

        case -3:                                                        /* 1070 */
            gameover_load_disp.msg_id = 0x34;
            break;

        default:
            gameover_load_disp.msg_id = 0xf;                            /* 1073 */
            break;
        }

        gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;  /* 1076 */
    }

    if ((mc_res != 0) && (load_data_buff != (void *)0)) {               /* 1082 */
        LiberateDataMemoryArea(load_data_buff);                         /* 1085 */
        load_data_buff = (void *)0;                                     /* 1090 */
    }
}                                                                       /* 1093 */

/* --------------------------------------------------------------------------
 *  Step 12/13 -- something went wrong; show it and go back to the menu.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcErrorConfInit(void)                           /* 1101 */
{
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1105 */

    gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_WAIT;      /* 1107 */
}

/* A card swapped while the error is up (-1) drops everything and restarts the
 * whole machine on the new card. */
static void GameOverLoadMcErrorConfWait(void)                           /* 1114 */
{
    int mc_res;
    int i;

    GameOverLoadMcErrorConfPad();                                       /* 1123 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1126 */

    if (mc_res == 1) {                                                  /* 1129 */
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 1131 */
    }
    else if (mc_res == -1) {                                            /* 1132 */
        if (load_data_buff != (void *)0) {                              /* 1133 */
            LiberateDataMemoryArea(load_data_buff);                     /* 1137 */
            load_data_buff = (void *)0;                                 /* 1138 */
        }

        for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                  /* 1139 */
            if (gameover_load_snap_addr[i] != (void *)0) {              /* 1140 */
                LiberateDataMemoryArea(gameover_load_snap_addr[i]);     /* 1142 */
                gameover_load_snap_addr[i] = (void *)0;                 /* 1144 */
            }
        }                                                               /* 1145 */

        gameover_load_disp.msg_id  = 0;                                 /* 1148 */
        gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_CHECK_INIT;       /* 1149 */
    }
    else if (mc_res < 0) {                                              /* 1152 */
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 1153 */
    }
}

/* Both buttons do the same thing -- the ROM still writes them out twice. */
static void GameOverLoadMcErrorConfPad(void)                            /* 1162 */
{
    if (*paddat[0] == 1) {                                              /* 1166 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1167 */

        GameOverLoadReturnMenu();                                       /* 1169 */
    }
    else if (*paddat[1] == 1) {                                         /* 1172 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1173 */

        GameOverLoadReturnMenu();                                       /* 1175 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 14 -- loaded; wait for the loading screen's pak, then go.
 * ------------------------------------------------------------------------ */

static void GameOverLoadMcEndConf(void)                                 /* 1184 */
{
    if (LoadingTexLoadWait() != 0) {                                    /* 1188 */
        GameOverLoadMcEndConfPad();                                     /* 1189 */
    }
}

/* Both buttons do the same thing again, and both branches pick the same
 * destination: a file that has been cleared at least once goes to the setup
 * menu (mission mode is unlocked), everything else straight back into the
 * story. */
static void GameOverLoadMcEndConfPad(void)                              /* 1198 */
{
    if (*paddat[0] == 1) {                                              /* 1202 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1203 */

        if (ingame_wrk.mClearCnt.Get() > 0) {                           /* 1207 */
            SetNextGPhase(GID_TITLE_SETUPMENU);
        }
        else {
            SetNextGPhase(GID_STORY_LOAD_MISSION_SAVE);                 /* 1210 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 1214 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1215 */

        if (ingame_wrk.mClearCnt.Get() > 0) {                           /* 1219 */
            SetNextGPhase(GID_TITLE_SETUPMENU);
        }
        else {
            SetNextGPhase(GID_STORY_LOAD_MISSION_SAVE);                 /* 1222 */
        }
    }
}

/* Watches for the card being pulled out while the screen is up.  Note -1
 * (swapped) reports as msg 1 here, where GameOverLoadMcCheckWait() treats it
 * as "start over". */
static void GameOverLoadMcEveryFrameCheck(void)                         /* 1232 */
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1240 */

    if (mc_res < 0) {                                                   /* 1242 */
        switch (mc_res) {                                               /* 1243 */
        case -1:                                                        /* 1248 */
            gameover_load_disp.msg_id = 1;
            break;

        case -2:                                                        /* 1253 */
            gameover_load_disp.msg_id = 0x18;
            break;

        case -0x14:                                                     /* 1257 */
            gameover_load_disp.msg_id = 1;
            break;

        default:
            gameover_load_disp.msg_id = 2;                              /* 1261 */
            break;
        }

        gameover_load_ctrl.mc_step = GAMEOVER_LOAD_MC_ERROR_CONF_INIT;  /* 1262 */
    }
}

/* ==========================================================================
 *  Tear-down
 * ======================================================================== */

void GameOverLoadEnd(void)                                              /* 1275 */
{
    int i;

    if (load_data_buff != (void *)0) {                                  /* 1283 */
        LiberateDataMemoryArea(load_data_buff);                         /* 1284 */
        load_data_buff = (void *)0;                                     /* 1285 */
    }

    for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                      /* 1289 */
        if (gameover_load_snap_addr[i] != (void *)0) {                  /* 1290 */
            LiberateDataMemoryArea(gameover_load_snap_addr[i]);         /* 1291 */
            gameover_load_snap_addr[i] = (void *)0;                     /* 1292 */
        }
    }                                                                   /* 1294 */

    MemoryCardEnd();                                                    /* 1298 */
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

static void GameOverLoadDispInit(void)                                  /* 1330 */
{
    gameover_load_disp.msg_id     = 0;                                  /* 1333 */
    gameover_load_disp.anim_step  = ZERO2_ANIM2D_STEP_START;            /* 1334 */
    gameover_load_disp.anim_timer = 0;                                  /* 1335 */
    gameover_load_disp.csr_timer  = 0;                                  /* 1336 */
}

/* The title frame is the one part drawn out of OUTGAME_PK2; everything else
 * comes from this screen's own SAVE_LOAD pak. */
void GameOverLoadDispMain(void)                                         /* 1344 */
{
    u_char alpha;
    u_char rgb;

    rgb = 0x80;

    if (gameover_load_ctrl.step < GAMEOVER_LOAD_STEP_MC) {              /* 1349 */
        return;
    }

    alpha = Zero2Anim2D_InOutAnimCtrl(&gameover_load_disp.anim_step,
                                      &gameover_load_disp.anim_timer,
                                      GAMEOVER_LOAD_ANIM_IN_TIME,
                                      GAMEOVER_LOAD_ANIM_OUT_TIME);     /* 1354 */

    if (gameover_load_disp.anim_step == ZERO2_ANIM2D_STEP_END) {        /* 1355 */
        return;
    }

    SaveLoadCmnBaseDisp(0, 0, alpha, gameover_load_tex_addr, 0);        /* 1357 */
    SaveLoadTitleFrameDisp(0, 0, alpha, gameover_load_cmn_tex_addr);    /* 1359 */
    SaveLoadTitleLoadDisp(0, 0, alpha, gameover_load_tex_addr);         /* 1362 */

    switch (gameover_load_ctrl.mc_step) {                               /* 1365 */
    case GAMEOVER_LOAD_MC_CHECK_INIT:
    case GAMEOVER_LOAD_MC_CHECK_WAIT:
    case GAMEOVER_LOAD_MC_HEAD_LOAD_INIT:
    case GAMEOVER_LOAD_MC_HEAD_LOAD_WAIT:
    case GAMEOVER_LOAD_MC_SNAP_LOAD_INIT:
    case GAMEOVER_LOAD_MC_SNAP_LOAD_WAIT:
    case GAMEOVER_LOAD_MC_ERROR_CONF_INIT:
    case GAMEOVER_LOAD_MC_ERROR_CONF_WAIT:
        SaveLoadMcCheckDisp(0, 0, alpha, gameover_load_tex_addr);       /* 1369 */

        SaveLoadMcStateMsgWinDisp(0, 0, alpha);                         /* 1409 */
        SaveLoadMcStateMsgDisp(0, 0, alpha, gameover_load_disp.msg_id); /* 1411 */
        break;                                                          /* 1412 */

    case GAMEOVER_LOAD_MC_LOAD_FILE_SEL:
        GameOverLoadFileSelDisp(alpha);                                 /* 1378 */

        Zero2Anim2D_CsrAnimCtrl(&gameover_load_disp.csr_timer, &rgb);   /* 1383 */
        SaveLoadCursorDisp(0, 0, alpha, rgb, gameover_load_tex_addr,
                           (int)gameover_load_ctrl.csr);                /* 1385 */

        SaveLoadFileSelMsgWinDisp(0, 0, alpha);                         /* 1387 */
        SaveLoadFileSelMsgDisp(0, 0, alpha, 0x22);                      /* 1389 */
        break;                                                          /* 1391 */

    case GAMEOVER_LOAD_MC_LOAD_CONF:
        GameOverLoadFileSelDisp(alpha);                                 /* 1393 */

        SaveLoadFileSelYesNoWinDisp(0, 0, alpha,
                                    (int)gameover_load_ctrl.conf_csr);  /* 1394 */
        SaveLoadFileSelMsgDisp(0, 0, alpha, gameover_load_disp.msg_id); /* 1396 */
        break;                                                          /* 1398 */

    case GAMEOVER_LOAD_MC_SYS_LOAD_INIT:
    case GAMEOVER_LOAD_MC_SYS_LOAD_WAIT:
    case GAMEOVER_LOAD_MC_LOAD_INIT:
    case GAMEOVER_LOAD_MC_LOAD_WAIT:
    case GAMEOVER_LOAD_MC_END_CONF:
        GameOverLoadFileSelDisp(alpha);                                 /* 1400 */

        SaveLoadMcStateMsgWinDisp(0, 0, alpha);                         /* 1401 */
        SaveLoadMcStateMsgDisp(0, 0, alpha, gameover_load_disp.msg_id);
        break;                                                          /* 1407 */

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 1414 */
        break;
    }
}

/* One row per slot.  An empty slot gets the line art and the "not cleared"
 * mask; a populated one gets its snapshot, its number plate, and -- only when
 * it is the selected row -- the chapter/room/play-time readout. */
static void GameOverLoadFileSelDisp(u_char alpha)                       /* 1428 */
{
    int i;

    for (i = 0; i < GAMEOVER_LOAD_DATA_MAX; i++) {                      /* 1433 */
        if (GetMemoryCardPlayDataFlg(i) == 0) {                         /* 1435 */
            if (gameover_load_ctrl.csr == i) {                          /* 1436 */
                SaveLoadSelLineDisp(0, 0, alpha, gameover_load_tex_addr, i);    /* 1438 */
            }
            else {
                SaveLoadNonSelLineDisp(0, 0, alpha, gameover_load_tex_addr, i); /* 1442 */
            }

            SaveLoadNonClearMaskDisp(0, 0, alpha, gameover_load_tex_addr, i);   /* 1444 */
            SaveLoadSnapShadowDisp(0, 0, alpha, gameover_load_tex_addr, i);     /* 1446 */
        }
        else {
            if (gameover_load_snap_addr[i] != (void *)0) {              /* 1449 */
                SaveLoadSnapShotDisp(0, 0, alpha,
                                     gameover_load_snap_addr[i], i);    /* 1450 */
            }

            if (gameover_load_ctrl.csr == i) {                          /* 1454 */
                SaveLoadSelNoDisp(0, 0, alpha, gameover_load_tex_addr, i);      /* 1459 */
                SaveLoadSelDataNumDisp(0, 0, alpha, gameover_load_tex_addr, i); /* 1461 */

                if (GetMemoryCardClearDataFlg(i) != 0) {                /* 1465 */
                    SaveLoadMcClearPlayDataInfoDisp(
                        0, 0, alpha, GetMemoryCardPlayDataPlayTime(i));  /* 1468 */
                }
                else {
                    SaveLoadMcPlayDataInfoDisp(
                        0, 0, alpha,
                        GetMemoryCardPlayDataChapter(i),
                        GetMemoryCardPlayDataRoomLabel(i),
                        GetMemoryCardPlayDataPlayTime(i));              /* 1470, 1472, 1476 */
                }
            }
            else {
                SaveLoadNonSelNoDisp(0, 0, alpha, gameover_load_tex_addr, i);      /* 1482 */
                SaveLoadNonSelDataNumDisp(0, 0, alpha, gameover_load_tex_addr, i); /* 1484 */
            }

            SaveLoadSnapShadowDisp(0, 0, alpha, gameover_load_tex_addr, i);     /* 1488 */

            if (GetMemoryCardPlayDataClearNum(i) < 1) {                 /* 1492 */
                SaveLoadNonClearMaskDisp(0, 0, alpha, gameover_load_tex_addr, i); /* 1494 */
            }
            else {
                SaveLoadClearFlareDisp(0, 0, alpha, gameover_load_tex_addr, i);   /* 1496 */
                SaveLoadClearNumberDisp(GetMemoryCardPlayDataClearNum(i),
                                        0, 0, alpha, 0, 1, i,
                                        gameover_load_tex_addr);        /* 1498 */
            }
        }
    }

    SaveLoadSelFlareDisp(0, 0, alpha, gameover_load_tex_addr,
                         (int)gameover_load_ctrl.csr);
}
