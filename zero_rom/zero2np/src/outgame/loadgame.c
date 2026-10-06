// FILE: /home/zero_rom/zero2np/src/outgame/loadgame.c
//
// Load-game screen.  A fifteen-step state machine that checks the memory
// card, loads the save header, pulls each slot's snapshot texture, lets the
// player pick a slot, then loads the system file and the slot itself before
// handing off to the game.  Every error path funnels into the same
// error-confirm step with a message id; MemoryCardCheckEveryFrameMain() keeps
// watching for the card being pulled out while the screen is up.
//
// Drawing is entirely delegated to save_load_disp.c, which the save screen
// shares.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "loadgame.h"

#include "option.h"                             // OptSoundSetupRef
#include "title.h"                              // GetTitleSoundID / SetTitleLoadFlg / GetOutGameCmnTexAddr
#include "../common/ol_load.h"                  // ol_loadGetHeap / ol_loadFreeHeap / GetFileSize
#include "../system/eeiop/fileload.h"           // FileLoadReqEE / FileLoadIsEnd2
#include "../common/utility2.h"                 // PRINT_ASSERT
#include "../common/variable.h"                 // pad / opt_wrk / ingame_wrk
#include "../graphics/graph3d/ctl/fixed_array.h" // reference_fixed_array
#include "../ingame/ingame.h"                   // IngameWrkInit
#include "../ingame/loading/loading.h"          // LoadingTexLoadWait
#include "../ingame/menu/tim_dat/map_room_dat.h" // room_info_dat
#include "../ingame/menu/zero2_anim2d.h"        // Zero2Anim2D_CsrAnimCtrl
#include "../main/gphase.h"                     // SetNextGPhase / GID_*
#include "../main/main_decls.h"                 // EventDataLoadReq
#include "../save_load/prg/save_load_disp.h"    // SaveLoad*Disp
#include "../system/eeiop/cddat.h"              // SAVE_LOAD_PK2 / SNP_CLEAR_DATA_PK2
#include "../system/eeiop/snd_buffer.h"         // SndBufIsPlaying
#include "../system/mc/prg/mc.h"                // MemoryCardExeInit / MemoryCardEnd
#include "../system/mc/prg/mc_check.h"          // MemoryCardCheckInit / Main
#include "../system/mc/prg/mc_check_broken.h"   // MemoryCardCheck*Broken / NewFileLoad
#include "../system/mc/prg/mc_check_card.h"     // MemoryCardCheckEveryFrame*
#include "../system/mc/prg/mc_load.h"           // MemoryCardFileLoad*
#include "../system/mc/prg/mc_set_data.h"       // path / size / data-area / accessors
#include "../system/os/system.h"                // GetLanguage / SystemBankPlay
#include "../system/pad/pad.h"                  // pad / paddat / GetPadAnalogRpt

#define LOAD_GAME_DATA_MAX 5        /* memory-card save slots */

/* types.txt.  Private to this file, like chapter_sel.c's own control block.
 * char throughout -- every ROM access to the first four fields is an `lb`,
 * and wait_timer is an `lh`. */
typedef struct                      /* 0xc */
{
    /* 0x0 */ char      step;
    /* 0x1 */ char      csr;
    /* 0x2 */ char      conf_csr;
    /* 0x3 */ char      csr_timer;
    /* 0x4 */ int       msg_id;
    /* 0x8 */ short int wait_timer;
} LOAD_GAME_CTRL;

/* Steps of load_game_ctrl.step.  The ROM's debug info carries no enum for
 * these -- the names are taken from the step handlers' own ROM symbol names so
 * the values still read against the jump tables at rodata 3ba610 / 3ba7c0. */
enum LOAD_GAME_STEP
{
    LOADGAME_MC_CHECK_INIT       = 0,
    LOADGAME_MC_CHECK_WAIT       = 1,
    LOADGAME_MC_HEAD_LOAD_INIT   = 2,
    LOADGAME_MC_HEAD_LOAD_WAIT   = 3,
    LOADGAME_MC_SNAP_LOAD_INIT   = 4,
    LOADGAME_MC_SNAP_LOAD_WAIT   = 5,
    LOADGAME_MC_LOAD_FILE_SEL    = 6,
    LOADGAME_MC_LOAD_CONF        = 7,
    LOADGAME_MC_SYSTEM_LOAD_INIT = 8,
    LOADGAME_MC_SYSTEM_LOAD_WAIT = 9,
    LOADGAME_MC_LOAD_INIT        = 10,
    LOADGAME_MC_LOAD_WAIT        = 11,
    LOADGAME_MC_ERROR_CONF_INIT  = 12,
    LOADGAME_MC_ERROR_CONF_WAIT  = 13,
    LOADGAME_MC_END_CONF         = 14
};

/* Memory-card file indices within the save directory: 0 is the system file,
 * 1 the header, and slot n is n + 2. */
#define LOADGAME_MC_FILE_SYSTEM 0
#define LOADGAME_MC_FILE_HEAD   1
#define LOADGAME_MC_FILE_DATA   2

/* Path buffers are memset to 0x37 bytes by every caller; that is the ROM's
 * literal size, not sizeof(). */
#define LOADGAME_PATH_NAME_LEN 55

static void *load_game_tex_addr;                        /* sdata 3f1890 */
static void *load_data_buff;                            /* sdata 3f1894 */
static void *load_game_snap_addr[LOAD_GAME_DATA_MAX];   /* data 3199c0 */

/* Backing store for load_game_snap_tex below.  The ROM put this in
 * loadgame.o's .rodata (3ba800) and wrote to it anyway -- the EE has no write
 * protection on data pages, so nothing complained.  The host does, so it has
 * to be writable here; that is the one deliberate deviation in this file. */
static int load_game_snap_tex_dat[LOAD_GAME_DATA_MAX] = { -1, -1, -1, -1, -1 };

/* File number of each slot's snapshot texture, bound at static-init time --
 * this object's single .ctors entry (0x2c3be4).  A reference_fixed_array, not
 * a plain int[5]: globals.txt types it that way and every subscript in the ROM
 * carries the inlined _fixed_array_verifyrange<int>(i, 5) bounds check. */
static reference_fixed_array<int, LOAD_GAME_DATA_MAX> load_game_snap_tex(load_game_snap_tex_dat);
                                                        /* sbss 3f4d60 */

static LOAD_GAME_CTRL load_game_ctrl;                   /* bss 4b4228 */

static void LoadGameCtrlInit(void);
static void LoadGameMcCheckInit(void);
static void LoadGameMcCheckWait(void);
static void LoadGameMcHeadLoadInit(void);
static void LoadGameMcHeadLoadWait(void);
static void LoadGameMcSnapLoadInit(void);
static void LoadGameMcSnapLoadWait(void);
static void LoadGameMcLoadFileSel(void);
static void LoadGameMcLoadFileSelPad(void);
static void LoadGameMcLoadConf(void);
static void LoadGameMcLoadConfPad(void);
static void LoadGameMcSystemLoadInit(void);
static void LoadGameMcSystemLoadWait(void);
static void LoadGameMcLoadInit(void);
static void LoadGameMcLoadWait(void);
static void LoadGameMcErrorConfInit(void);
static void LoadGameMcErrorConfWait(void);
static void LoadGameMcErrorConfPad(void);
static void LoadGameMcEndConf(void);
static void LoadGameMcEndConfPad(void);
static void LoadGameMcEveryFrameCheck(void);
static void LoadGameFileSelDisp(u_char alpha);

/* --------------------------------------------------------------------------
 *  LoadGameInit
 *
 *  Phase entry.  Clears the ingame work area, resets the step machine and
 *  drops anything a previous visit to the screen left allocated.
 * ------------------------------------------------------------------------ */
void LoadGameInit(void)
{                                                                       /* 179 */
    int i;

    IngameWrkInit(0, 0);                                                /* 185 */

    LoadGameCtrlInit();                                                 /* 188 */

    MemoryCardExeInit();                                                /* 192 */

    if (load_data_buff != (void *)0)                                    /* 196 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 197 */
        load_data_buff = (void *)0;                                     /* 198 */
    }

    for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                            /* 201 */
    {
        if (load_game_snap_addr[i] != (void *)0)                        /* 203 */
        {
            LiberateDataMemoryArea(load_game_snap_addr[i]);             /* 204 */
            load_game_snap_addr[i] = (void *)0;                         /* 205 */
        }

        load_game_snap_tex[i] = -1;
    }                                                                   /* 209 */
}

/* Field order below is the ROM's store order, which is declaration order. */
static void LoadGameCtrlInit(void)
{
    load_game_ctrl.step       = LOADGAME_MC_CHECK_INIT;                 /* 220 */
    load_game_ctrl.csr        = 0;                                      /* 221 */
    load_game_ctrl.conf_csr   = 1;                                      /* 222 */
    load_game_ctrl.csr_timer  = 0;                                      /* 223 */
    load_game_ctrl.msg_id     = 0;                                      /* 224 */
    load_game_ctrl.wait_timer = 0;                                      /* 225 */
}

/* --------------------------------------------------------------------------
 *  GetLoadGameTexMem / LoadGameDataLoadReq / LoadGameDataLoadWait /
 *  ReleaseLoadGameTexMem
 *
 *  The screen's texture pak.  SAVE_LOAD_PK2's per-language variants sit
 *  immediately after it in the file table, so the language index is simply
 *  added to the base file number.
 * ------------------------------------------------------------------------ */
void GetLoadGameTexMem(void)
{
    ReleaseLoadGameTexMem();                                            /* 236 */

    if (load_game_tex_addr == (void *)0)                                /* 239 */
    {
        load_game_tex_addr =
            ol_loadGetHeap(GetFileSize(SAVE_LOAD_PK2 + (char)GetLanguage())); /* 241 */
    }
    else
    {
        /* ROM emits the SetAssertPreMessage / PrintAssertReal pair here. */
        PRINT_ASSERT("Error! GetLoadGameTexMem");                       /* 244 */
    }
}

void LoadGameDataLoadReq(void)
{
    FileLoadReqEE(SAVE_LOAD_PK2 + (char)GetLanguage(), load_game_tex_addr,
                  5, (FILE_LOAD_CALLBACK)0, (void *)0);                           /* 257 */
}

int LoadGameDataLoadWait(void)
{                                                                       /* 278 */
    return FileLoadIsEnd2(SAVE_LOAD_PK2 + (char)GetLanguage(),
                          load_game_tex_addr) != 0;                     /* 283 */
}

/* --------------------------------------------------------------------------
 *  LoadGameMain
 *
 *  Every *Init step falls through into its *Wait partner so the wait runs on
 *  the same frame it was set up.  Jump table at rodata 3ba610.
 * ------------------------------------------------------------------------ */
void LoadGameMain(void)
{                                                                       /* 293 */
    switch (load_game_ctrl.step)                                        /* 297 */
    {
    case LOADGAME_MC_CHECK_INIT:
        LoadGameMcCheckInit();                                          /* 299 */
        /* fall through */
    case LOADGAME_MC_CHECK_WAIT:
        LoadGameMcCheckWait();                                          /* 302 */
        break;

    case LOADGAME_MC_HEAD_LOAD_INIT:
        LoadGameMcHeadLoadInit();                                       /* 305 */
        /* fall through */
    case LOADGAME_MC_HEAD_LOAD_WAIT:
        LoadGameMcHeadLoadWait();                                       /* 308 */
        break;

    case LOADGAME_MC_SNAP_LOAD_INIT:
        LoadGameMcSnapLoadInit();                                       /* 311 */
        /* fall through */
    case LOADGAME_MC_SNAP_LOAD_WAIT:
        LoadGameMcSnapLoadWait();                                       /* 314 */
        break;

    case LOADGAME_MC_LOAD_FILE_SEL:
        LoadGameMcLoadFileSel();                                        /* 317 */
        break;

    case LOADGAME_MC_LOAD_CONF:
        LoadGameMcLoadConf();                                           /* 320 */
        break;

    case LOADGAME_MC_SYSTEM_LOAD_INIT:
        LoadGameMcSystemLoadInit();                                     /* 323 */
        /* fall through */
    case LOADGAME_MC_SYSTEM_LOAD_WAIT:
        LoadGameMcSystemLoadWait();                                     /* 326 */
        break;

    case LOADGAME_MC_LOAD_INIT:
        LoadGameMcLoadInit();                                           /* 329 */
        /* fall through */
    case LOADGAME_MC_LOAD_WAIT:
        LoadGameMcLoadWait();                                           /* 332 */
        break;

    case LOADGAME_MC_ERROR_CONF_INIT:
        LoadGameMcErrorConfInit();                                      /* 335 */
        /* fall through */
    case LOADGAME_MC_ERROR_CONF_WAIT:
        LoadGameMcErrorConfWait();                                      /* 338 */
        break;

    case LOADGAME_MC_END_CONF:
        LoadGameMcEndConf();                                            /* 341 */
        break;

    default:
        PRINT_ASSERT("Error! LoadGameMain");                            /* 344 */
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Step 0/1 -- is there a card, and is our save directory intact?
 * ------------------------------------------------------------------------ */
static void LoadGameMcCheckInit(void)
{                                                                       /* 381 */
    char path_name[LOADGAME_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                                         /* 385 */
    MemoryCardMakeSearchDirPath(path_name, 0);                          /* 389 */
    MemoryCardCheckInit(0, 0, path_name);                               /* 391 */

    load_game_ctrl.step = LOADGAME_MC_CHECK_WAIT;                       /* 393 */
}

static void LoadGameMcCheckWait(void)
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 407 */

    if (mc_res == 1)                                                    /* 410 */
    {
        if (MemoryCardCheckDirBroken(0) != 0)                           /* 412 */
        {
            load_game_ctrl.step   = LOADGAME_MC_HEAD_LOAD_INIT;
            load_game_ctrl.msg_id = 0;
            return;
        }

        load_game_ctrl.msg_id = 0x19;                                   /* 420 */
    }
    else
    {
        if (mc_res >= 0)                                                /* 424 */
        {
            return;
        }

        switch (mc_res)                                                 /* 426 */
        {
        case -1:                                                        /* 428 */
            load_game_ctrl.step   = LOADGAME_MC_HEAD_LOAD_INIT;         /* 429 */
            load_game_ctrl.msg_id = 0;                                  /* 430 */
            return;

        case -2:                                                        /* 432 */
            load_game_ctrl.msg_id = 0x18;                               /* 434 */
            break;

        case -4:                                                        /* 436 */
            load_game_ctrl.msg_id = 0x18;                               /* 438 */
            break;

        case -6:                                                        /* 440 */
            load_game_ctrl.msg_id = 0x19;                               /* 442 */
            break;

        case -0x14:                                                     /* 444 */
            load_game_ctrl.msg_id = 1;                                  /* 446 */
            break;

        default:
            load_game_ctrl.msg_id = 2;                                  /* 450 */
            break;
        }
    }

    load_game_ctrl.step = LOADGAME_MC_ERROR_CONF_INIT;                  /* 451 */
}

/* --------------------------------------------------------------------------
 *  Step 2/3 -- the header file, which carries every slot's summary.
 * ------------------------------------------------------------------------ */
static void LoadGameMcHeadLoadInit(void)
{                                                                       /* 461 */
    int  size;
    char path_name[LOADGAME_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                                         /* 467 */

    if (load_data_buff != (void *)0)                                    /* 470 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 471 */
        load_data_buff = (void *)0;                                     /* 472 */
    }

    MemoryCardSetFilePath(path_name, 0, LOADGAME_MC_FILE_HEAD);         /* 477 */
    size = GetMemoryCardDataSize(0, LOADGAME_MC_FILE_HEAD);             /* 479 */
    load_data_buff = GetDataMemoryArea(size);                           /* 481 */

    MemoryCardFileLoadInit(0, 0, path_name, load_data_buff, size);      /* 483 */

    load_game_ctrl.msg_id = 0;                                          /* 486 */
    load_game_ctrl.step   = LOADGAME_MC_HEAD_LOAD_WAIT;                 /* 488 */
}

static void LoadGameMcHeadLoadWait(void)
{
    int mc_res;
    int size;
    int i;

    mc_res = MemoryCardFileLoadMain();                                  /* 506 */

    if (mc_res == 1)                                                    /* 509 */
    {
        size = GetMemoryCardDataSize(0, LOADGAME_MC_FILE_HEAD);         /* 511 */

        if (MemoryCardCheckFileBroken(load_data_buff, size) == 0)       /* 514 */
        {
            load_game_ctrl.msg_id = 0x19;                               /* 516 */
            load_game_ctrl.step   = LOADGAME_MC_ERROR_CONF_INIT;
        }
        else if (MemoryCardCheckNewFileLoad(load_data_buff, size) != 0) /* 520 */
        {
            load_game_ctrl.msg_id = 0x21;                               /* 525 */
            load_game_ctrl.step   = LOADGAME_MC_ERROR_CONF_INIT;
        }
        else
        {
            DevelopMemoryCardLoadData((char* )load_data_buff, 0, LOADGAME_MC_FILE_HEAD); /* 528 */

            /* Default to "no save data" and let any populated slot promote us
             * to the snapshot load. */
            load_game_ctrl.step   = LOADGAME_MC_ERROR_CONF_INIT;        /* 529 */
            load_game_ctrl.msg_id = 0x21;

            for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                    /* 532 */
            {
                if (GetMemoryCardPlayDataFlg(i) == 1)                   /* 533 */
                {
                    load_game_ctrl.msg_id = 0;                          /* 534 */
                    load_game_ctrl.step   = LOADGAME_MC_SNAP_LOAD_INIT; /* 535 */
                }
            }                                                          /* 537 */
        }
    }
    else if (mc_res < 0)                                                /* 542 */
    {
        switch (mc_res)                                                 /* 543 */
        {
        case -2:                                                        /* 547 */
            load_game_ctrl.msg_id = 0x18;                               /* 549 */
            break;

        case -3:                                                        /* 551 */
            load_game_ctrl.msg_id = 0x19;                               /* 552 */
            break;

        default:                                                        /* 554 */
            load_game_ctrl.msg_id = 0xf;                                /* 555 */
            break;
        }

        load_game_ctrl.step = LOADGAME_MC_ERROR_CONF_INIT;              /* 561 */
    }

    /* Any answer other than "still busy" is done with the staging buffer. */
    if ((mc_res != 0) && (load_data_buff != (void *)0))                 /* 569 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 570 */
        load_data_buff = (void *)0;                                     /* 571 */
    }
}                                                                       /* 572 */

/* --------------------------------------------------------------------------
 *  Step 4/5 -- one snapshot texture per populated slot.
 *
 *  A cleared slot shows the fixed SNP_CLEAR_DATA_PK2 image; otherwise the
 *  slot's room label is looked up in room_info_dat[] to find the room's
 *  snapshot texture.  A room the table does not know leaves the previous file
 *  number in place and loads nothing, which is the ROM's behaviour.
 * ------------------------------------------------------------------------ */
static void LoadGameMcSnapLoadInit(void)
{                                                                       /* 580 */
    int i;
    int j;

    for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                            /* 586 */
    {
        if (GetMemoryCardPlayDataFlg(i) == 1)                           /* 587 */
        {
            if (load_game_snap_addr[i] != (void *)0)                    /* 589 */
            {
                LiberateDataMemoryArea(load_game_snap_addr[i]);         /* 590 */
                load_game_snap_addr[i] = (void *)0;                     /* 591 */
            }

            if (GetMemoryCardClearDataFlg(i) != 0)                      /* 595 */
            {
                load_game_snap_tex[i] = SNP_CLEAR_DATA_PK2;
            }
            else
            {
                for (j = 0; room_info_dat[j].map_label != -1; j++)      /* 606 */
                {                                                       /* 607 */
                    if (room_info_dat[j].room_label ==
                        GetMemoryCardPlayDataRoomLabel(i))              /* 611 */
                    {
                        load_game_snap_tex[i] = room_info_dat[j].snap_tex_label;
                        goto snap_load_req;
                    }
                }

                continue;
            }

        snap_load_req:
            load_game_snap_addr[i] = GetDataMemoryArea(GetFileSize(load_game_snap_tex[i]));
            FileLoadReqEE(load_game_snap_tex[i], load_game_snap_addr[i],
                          5, (FILE_LOAD_CALLBACK)0, (void *)0);                   /* 618 */
        }
        else
        {
            if (load_game_snap_addr[i] != (void *)0)                    /* 625 */
            {
                LiberateDataMemoryArea(load_game_snap_addr[i]);         /* 626 */
                load_game_snap_addr[i] = (void *)0;                     /* 627 */
            }

            load_game_snap_tex[i] = -1;
        }
    }                                                                   /* 632 */

    MemoryCardCheckEveryFrameInit(0, 0);                                /* 635 */

    load_game_ctrl.step   = LOADGAME_MC_SNAP_LOAD_WAIT;                 /* 637 */
    load_game_ctrl.msg_id = 0;                                          /* 638 */
}

static void LoadGameMcSnapLoadWait(void)
{
    int i;
    int csr_set_flg;

    csr_set_flg = 0;

    for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                            /* 645 */
    {
        if (GetMemoryCardPlayDataFlg(i) == 1)                           /* 650 */
        {
            if (FileLoadIsEnd2(load_game_snap_tex[i],
                               load_game_snap_addr[i]) == 0)            /* 653 */
            {
                load_game_ctrl.step   = LOADGAME_MC_SNAP_LOAD_WAIT;     /* 655 */
                load_game_ctrl.msg_id = 0;
                break;
            }

            load_game_ctrl.msg_id = 0x22;                               /* 658 */
            load_game_ctrl.step   = LOADGAME_MC_LOAD_FILE_SEL;          /* 659 */

            /* Park the cursor on the first slot that actually has data. */
            if (csr_set_flg == 0)                                       /* 665 */
            {
                load_game_ctrl.csr = (char)i;                           /* 669 */
                csr_set_flg = 1;                                        /* 670 */
            }
        }
    }                                                                   /* 674 */

    LoadGameMcEveryFrameCheck();                                        /* 677 */
}

/* --------------------------------------------------------------------------
 *  Step 6 -- slot select.
 * ------------------------------------------------------------------------ */
static void LoadGameMcLoadFileSel(void)
{                                                                       /* 689 */
    LoadGameMcLoadFileSelPad();
    LoadGameMcEveryFrameCheck();                                        /* 692 */
}

/* The cursor skips empty slots: it steps up to five times looking for one
 * whose play-data flag is set, and only plays the move SE if it actually
 * landed somewhere new. */
static void LoadGameMcLoadFileSelPad(void)
{                                                                       /* 700 */
    int i;
    int cursor_back_up;

    cursor_back_up = load_game_ctrl.csr;                                /* 706 */

    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0))     /* 710 */
    {
        for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                        /* 711 */
        {
            load_game_ctrl.csr = (char)((load_game_ctrl.csr + 4) % LOAD_GAME_DATA_MAX); /* 712 */

            if (GetMemoryCardPlayDataFlg(load_game_ctrl.csr) == 1)      /* 715 */
            {
                break;
            }
        }

        if (cursor_back_up != load_game_ctrl.csr)                       /* 718 */
        {
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 721 */
        }
    }
    else if (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0)) /* 726 */
    {
        for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                        /* 727 */
        {
            load_game_ctrl.csr = (char)((load_game_ctrl.csr + 1) % LOAD_GAME_DATA_MAX); /* 728 */

            if (GetMemoryCardPlayDataFlg(load_game_ctrl.csr) == 1)      /* 731 */
            {
                break;
            }
        }

        if (cursor_back_up != load_game_ctrl.csr)                       /* 734 */
        {
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 737 */
        }
    }
    else if (*paddat[0] == 1)                                           /* 742 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 743 */

        load_game_ctrl.conf_csr = 1;                                    /* 745 */
        load_game_ctrl.step     = LOADGAME_MC_LOAD_CONF;                /* 746 */
        load_game_ctrl.msg_id   = 0xe;                                  /* 747 */
    }
    else if (*paddat[1] == 1)                                           /* 750 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 751 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 753 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 7 -- "load this slot?" yes/no.
 * ------------------------------------------------------------------------ */
static void LoadGameMcLoadConf(void)
{                                                                       /* 766 */
    LoadGameMcLoadConfPad();
    LoadGameMcEveryFrameCheck();                                        /* 769 */
}

static void LoadGameMcLoadConfPad(void)
{                                                                       /* 776 */
    /* Any of the four directions toggles between Yes and No. */
    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0) ||   /* 780 */
        ((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 785 */
        load_game_ctrl.conf_csr = load_game_ctrl.conf_csr ^ 1;          /* 786 */
    }
    else if (*paddat[0] == 1)                                           /* 787 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 790 */

        if (load_game_ctrl.conf_csr == 0)                               /* 791 */
        {
            load_game_ctrl.msg_id = 0x1d;                               /* 793 */
            load_game_ctrl.step   = LOADGAME_MC_SYSTEM_LOAD_INIT;       /* 794 */
        }
        else
        {
            load_game_ctrl.msg_id = 0x22;                               /* 795 */
            load_game_ctrl.step   = LOADGAME_MC_LOAD_FILE_SEL;
        }
    }
    else if (*paddat[1] == 1)                                           /* 800 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 804 */

        load_game_ctrl.msg_id = 0x22;                                   /* 805 */
        load_game_ctrl.step   = LOADGAME_MC_LOAD_FILE_SEL;              /* 807 */
    }
}                                                                       /* 808 */

/* --------------------------------------------------------------------------
 *  Step 8/9 -- the system file (options), loaded before the slot itself.
 * ------------------------------------------------------------------------ */
static void LoadGameMcSystemLoadInit(void)
{                                                                       /* 817 */
    int  size;
    char path_name[LOADGAME_PATH_NAME_LEN];

    memset(path_name, 0, 0x37);                                         /* 823 */

    if (load_data_buff != (void *)0)                                    /* 826 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 827 */
        load_data_buff = (void *)0;                                     /* 828 */
    }

    MemoryCardSetFilePath(path_name, 0, LOADGAME_MC_FILE_SYSTEM);       /* 833 */
    size = GetMemoryCardDataSize(0, LOADGAME_MC_FILE_SYSTEM);           /* 835 */
    load_data_buff = GetDataMemoryArea(size);                           /* 837 */

    MemoryCardFileLoadInit(0, 0, path_name, load_data_buff, size);      /* 839 */

    load_game_ctrl.step   = LOADGAME_MC_SYSTEM_LOAD_WAIT;               /* 841 */
    load_game_ctrl.msg_id = 0x1d;                                       /* 842 */
}

static void LoadGameMcSystemLoadWait(void)
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();                                  /* 858 */

    if (mc_res == 1)                                                    /* 861 */
    {
        size = GetMemoryCardDataSize(0, LOADGAME_MC_FILE_SYSTEM);       /* 863 */

        if (MemoryCardCheckFileBroken(load_data_buff, size) == 0)       /* 866 */
        {
            load_game_ctrl.msg_id = 0x19;                               /* 868 */
            load_game_ctrl.step   = LOADGAME_MC_ERROR_CONF_INIT;
        }
        else if (MemoryCardCheckNewFileLoad(load_data_buff, size) != 0) /* 872 */
        {
            load_game_ctrl.msg_id = 0x33;                               /* 873 */
            load_game_ctrl.step   = LOADGAME_MC_ERROR_CONF_INIT;
        }
        else
        {
            DevelopMemoryCardLoadData((char* )load_data_buff, 0, LOADGAME_MC_FILE_SYSTEM); /* 878 */

            /* Push the restored option block at the sound driver right away. */
            OptSoundSetupRef(&opt_wrk);                                 /* 880 */

            load_game_ctrl.step   = LOADGAME_MC_LOAD_INIT;              /* 882 */
            load_game_ctrl.msg_id = 0x20;                               /* 883 */
        }
    }
    else if (mc_res < 0)                                                /* 888 */
    {
        switch (mc_res)                                                 /* 889 */
        {
        case -2:                                                        /* 893 */
            load_game_ctrl.msg_id = 0x18;                               /* 895 */
            break;

        case -3:                                                        /* 897 */
            load_game_ctrl.msg_id = 0x19;                               /* 898 */
            break;

        default:                                                        /* 900 */
            load_game_ctrl.msg_id = 0xf;                                /* 901 */
            break;
        }

        load_game_ctrl.step = LOADGAME_MC_ERROR_CONF_INIT;              /* 907 */
    }

    if ((mc_res != 0) && (load_data_buff != (void *)0))                 /* 915 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 916 */
        load_data_buff = (void *)0;                                     /* 917 */
    }
}                                                                       /* 918 */

/* --------------------------------------------------------------------------
 *  Step 10/11 -- the chosen slot.  Slot n lives in memory-card file n + 2.
 * ------------------------------------------------------------------------ */
static void LoadGameMcLoadInit(void)
{                                                                       /* 926 */
    int  size;
    char path_name[LOADGAME_PATH_NAME_LEN];

    memset(path_name, 0, sizeof(path_name));                                         /* 932 */

    if (load_data_buff != (void *)0)                                    /* 935 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 936 */
        load_data_buff = (void *)0;                                     /* 937 */
    }

    MemoryCardSetFilePath(path_name, 0,
                          load_game_ctrl.csr + LOADGAME_MC_FILE_DATA);  /* 942 */
    size = GetMemoryCardDataSize(0,
                                 load_game_ctrl.csr + LOADGAME_MC_FILE_DATA); /* 944 */
    load_data_buff = GetDataMemoryArea(size);                           /* 946 */

    MemoryCardFileLoadInit(0, 0, path_name, load_data_buff, size);      /* 948 */

    load_game_ctrl.step   = LOADGAME_MC_LOAD_WAIT;                      /* 950 */
    load_game_ctrl.msg_id = 0x20;                                       /* 951 */
}

static void LoadGameMcLoadWait(void)
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();                                  /* 967 */

    if (mc_res == 1)                                                    /* 970 */
    {
        size = GetMemoryCardDataSize(0,
                                     load_game_ctrl.csr + LOADGAME_MC_FILE_DATA); /* 972 */

        if ((MemoryCardCheckFileBroken(load_data_buff, size) == 0) ||   /* 975 */
            (MemoryCardCheckNewFileLoad(load_data_buff, size) != 0))
        {
            load_game_ctrl.msg_id = 0x34;                               /* 977 */
            load_game_ctrl.step   = LOADGAME_MC_ERROR_CONF_INIT;
        }
        else
        {
            DevelopMemoryCardLoadData((char* )load_data_buff, 0,
                                      load_game_ctrl.csr + LOADGAME_MC_FILE_DATA); /* 986 */

            load_game_ctrl.step       = LOADGAME_MC_END_CONF;           /* 988 */
            load_game_ctrl.msg_id     = 0x10;                           /* 989 */
            load_game_ctrl.wait_timer = 0;                              /* 990 */
        }
    }
    else if (mc_res < 0)                                                /* 995 */
    {
        switch (mc_res)                                                 /* 996 */
        {
        case -2:                                                        /* 1000 */
            load_game_ctrl.msg_id = 0x18;                               /* 1002 */
            break;

        case -3:                                                        /* 1004 */
            load_game_ctrl.msg_id = 0x34;                               /* 1005 */
            break;

        default:                                                        /* 1007 */
            load_game_ctrl.msg_id = 0xf;                                /* 1008 */
            break;
        }

        load_game_ctrl.step = LOADGAME_MC_ERROR_CONF_INIT;              /* 1014 */
    }

    if ((mc_res != 0) && (load_data_buff != (void *)0))                 /* 1022 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 1023 */
        load_data_buff = (void *)0;                                     /* 1024 */
    }
}                                                                       /* 1025 */

/* --------------------------------------------------------------------------
 *  Step 12/13 -- error confirm.  Stays up until the card comes back (the
 *  every-frame check answers -1), at which point everything allocated is
 *  dropped and the machine restarts from the card check.
 * ------------------------------------------------------------------------ */
static void LoadGameMcErrorConfInit(void)
{                                                                       /* 1033 */
    MemoryCardCheckEveryFrameInit(0, 0);                                /* 1037 */

    load_game_ctrl.step = LOADGAME_MC_ERROR_CONF_WAIT;                  /* 1039 */
}

static void LoadGameMcErrorConfWait(void)
{
    int i;
    int mc_res;

    LoadGameMcErrorConfPad();                                           /* 1055 */

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1058 */

    if (mc_res == -1)                                                   /* 1061 */
    {
        if (load_data_buff != (void *)0)                                /* 1063 */
        {
            LiberateDataMemoryArea(load_data_buff);                     /* 1064 */
            load_data_buff = (void *)0;                                 /* 1065 */
        }

        for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                        /* 1069 */
        {
            if (load_game_snap_addr[i] != (void *)0)                    /* 1070 */
            {
                LiberateDataMemoryArea(load_game_snap_addr[i]);         /* 1071 */
                load_game_snap_addr[i] = (void *)0;                     /* 1072 */
            }
        }                                                               /* 1074 */

        load_game_ctrl.step   = LOADGAME_MC_CHECK_INIT;                 /* 1076 */
        load_game_ctrl.msg_id = 0;                                      /* 1077 */
    }
    else if (mc_res == 1)                                               /* 1080 */
    {
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 1081 */
    }
    else if (mc_res < 0)                                                /* 1084 */
    {
        MemoryCardCheckEveryFrameInit(0, 0);                            /* 1085 */
    }
}

/* Either button backs all the way out to the title menu. */
static void LoadGameMcErrorConfPad(void)
{                                                                       /* 1094 */
    if (*paddat[0] == 1)                                                /* 1098 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1099 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 1100 */
    }
    else if (*paddat[1] == 1)                                           /* 1103 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1104 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 1105 */
    }
}

/* --------------------------------------------------------------------------
 *  Step 14 -- "loaded" confirm.  Waits for the loading texture to be resident
 *  and the title jingle to finish before it will accept a button.
 * ------------------------------------------------------------------------ */
static void LoadGameMcEndConf(void)
{                                                                       /* 1118 */
    if ((LoadingTexLoadWait() != 0) && (SndBufIsPlaying(GetTitleSoundID()) == 0)) /* 1120 */
    {
        LoadGameMcEndConfPad();                                         /* 1121 */
    }
}

/* Auto-advances after 150 frames if the player does not press anything.  A
 * save with at least one clear goes to the setup menu (costume / difficulty
 * unlocks); a first playthrough goes straight into the mission load. */
static void LoadGameMcEndConfPad(void)
{                                                                       /* 1131 */
    char move_flg;

    move_flg = 0;

    if (*paddat[0] == 1)                                                /* 1139 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);
        move_flg = 1;
    }
    else if (*paddat[1] == 1)                                           /* 1142 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 1145 */
        move_flg = 1;                                                   /* 1146 */
    }

    load_game_ctrl.wait_timer++;                                        /* 1148 */

    if (load_game_ctrl.wait_timer > 0x95)                               /* 1151 */
    {
        move_flg = 1;                                                   /* 1152 */
    }

    if (move_flg != 0)                                                  /* 1157 */
    {
        if (ingame_wrk.mClearCnt < 1)                            /* 1159 */
        {
            SetTitleLoadFlg(0);                                         /* 1162 */
            SetNextGPhase(GID_STORY_LOAD_MISSION_SAVE);                 /* 1163 */
        }
        else
        {
            SetNextGPhase(GID_TITLE_SETUPMENU);
        }

        EventDataLoadReq();                                             /* 1167 */
    }
}

/* --------------------------------------------------------------------------
 *  LoadGameMcEveryFrameCheck
 *
 *  Shared tail of the interactive steps: notice the card being pulled and
 *  divert to the error confirm.
 * ------------------------------------------------------------------------ */
static void LoadGameMcEveryFrameCheck(void)
{
    int mc_res;

    mc_res = MemoryCardCheckEveryFrameMain();                           /* 1183 */

    if (mc_res < 0)                                                     /* 1185 */
    {
        switch (mc_res)                                                 /* 1186 */
        {
        case -1:                                                        /* 1189 */
            load_game_ctrl.msg_id = 1;                                  /* 1191 */
            break;

        case -2:                                                        /* 1194 */
            load_game_ctrl.msg_id = 0x18;                               /* 1196 */
            break;

        case -0x14:                                                     /* 1198 */
            load_game_ctrl.msg_id = 1;                                  /* 1200 */
            break;

        default:
            load_game_ctrl.msg_id = 2;                                  /* 1204 */
            break;
        }

        load_game_ctrl.step = LOADGAME_MC_ERROR_CONF_INIT;              /* 1205 */
    }
}

/* --------------------------------------------------------------------------
 *  LoadGameEnd
 * ------------------------------------------------------------------------ */
void LoadGameEnd(void)
{                                                                       /* 1218 */
    int i;

    if (load_data_buff != (void *)0)                                    /* 1226 */
    {
        LiberateDataMemoryArea(load_data_buff);                         /* 1227 */
        load_data_buff = (void *)0;                                     /* 1228 */
    }

    for (i = 0; i < LOAD_GAME_DATA_MAX; i++)                            /* 1232 */
    {
        if (load_game_snap_addr[i] != (void *)0)                        /* 1233 */
        {
            LiberateDataMemoryArea(load_game_snap_addr[i]);             /* 1234 */
            load_game_snap_addr[i] = (void *)0;                         /* 1235 */
        }
    }                                                                   /* 1237 */

    MemoryCardEnd();                                                    /* 1241 */
}

void ReleaseLoadGameTexMem(void)
{                                                                       /* 1250 */
    if (load_game_tex_addr != (void *)0)                                /* 1253 */
    {
        ol_loadFreeHeap(load_game_tex_addr);                            /* 1254 */
        load_game_tex_addr = (void *)0;                                 /* 1255 */
    }
}

/* --------------------------------------------------------------------------
 *  LoadGameDispMain
 *
 *  Base plate, title frame and "LOAD" caption every frame; what goes on top
 *  depends on the step.  Jump table at rodata 3ba7c0.
 * ------------------------------------------------------------------------ */
void LoadGameDispMain(void)
{                                                                       /* 1268 */
    u_char rgb;

    rgb = 0x80;

    if (load_game_tex_addr == (void *)0)                                /* 1273 */
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);
        return;
    }

    SaveLoadCmnBaseDisp(0, 0, 0x80, load_game_tex_addr, 0);             /* 1277 */
    SaveLoadTitleFrameDisp(0, 0, 0x80, GetOutGameCmnTexAddr());         /* 1279 */
    SaveLoadTitleLoadDisp(0, 0, 0x80, load_game_tex_addr);              /* 1282 */

    switch (load_game_ctrl.step)                                        /* 1285 */
    {
    case LOADGAME_MC_CHECK_INIT:
    case LOADGAME_MC_CHECK_WAIT:
    case LOADGAME_MC_HEAD_LOAD_INIT:
    case LOADGAME_MC_HEAD_LOAD_WAIT:
    case LOADGAME_MC_SNAP_LOAD_INIT:
    case LOADGAME_MC_SNAP_LOAD_WAIT:
    case LOADGAME_MC_ERROR_CONF_INIT:
    case LOADGAME_MC_ERROR_CONF_WAIT:
        /* No slot list yet -- just the card-state artwork and its message. */
        SaveLoadMcCheckDisp(0, 0, 0x80, load_game_tex_addr);            /* 1289 */
        SaveLoadMcStateMsgWinDisp(0, 0, 0x80);                          /* 1298 */
        SaveLoadMcStateMsgDisp(0, 0, 0x80, load_game_ctrl.msg_id);      /* 1303 */
        break;

    case LOADGAME_MC_LOAD_FILE_SEL:
        LoadGameFileSelDisp(0x80);                                      /* 1305 */
        Zero2Anim2D_CsrAnimCtrl(&load_game_ctrl.csr_timer, &rgb);       /* 1307 */
        SaveLoadCursorDisp(0, 0, 0x80, rgb, load_game_tex_addr,
                           (int)load_game_ctrl.csr);                    /* 1309 */
        SaveLoadFileSelMsgWinDisp(0, 0, 0x80);                          /* 1311 */
        SaveLoadFileSelMsgDisp(0, 0, 0x80, 0x22);                       /* 1313 */
        break;                                                          /* 1314 */

    case LOADGAME_MC_LOAD_CONF:
        LoadGameFileSelDisp(0x80);                                      /* 1316 */
        SaveLoadFileSelYesNoWinDisp(0, 0, 0x80,
                                    (int)load_game_ctrl.conf_csr);      /* 1318 */
        SaveLoadFileSelMsgDisp(0, 0, 0x80, load_game_ctrl.msg_id);      /* 1320 */
        break;                                                          /* 1321 */

    case LOADGAME_MC_SYSTEM_LOAD_INIT:
    case LOADGAME_MC_SYSTEM_LOAD_WAIT:
    case LOADGAME_MC_LOAD_INIT:
    case LOADGAME_MC_LOAD_WAIT:
    case LOADGAME_MC_END_CONF:
        /* Slot list stays up behind the progress message. */
        LoadGameFileSelDisp(0x80);                                      /* 1327 */
        SaveLoadMcStateMsgWinDisp(0, 0, 0x80);                          /* 1329 */
        SaveLoadMcStateMsgDisp(0, 0, 0x80, load_game_ctrl.msg_id);      /* 1331 */
        break;                                                          /* 1332 */

    default:
        PRINT_ASSERT("Error! LoadGameDispMain");                        /* 1334 */
        break;
    }
}                                                                       /* 1339 */

/* --------------------------------------------------------------------------
 *  LoadGameFileSelDisp
 *
 *  The five slot rows.  A populated slot draws its snapshot plus either the
 *  chapter/room/play-time line (normal save) or the clear-time line; an empty
 *  slot draws only the divider and the "no data" mask.  The clear-count flare
 *  and number ride on top of any slot that has been finished at least once.
 * ------------------------------------------------------------------------ */
static void LoadGameFileSelDisp(u_char alpha)
{                                                                       /* 1350 */
    int data_num;

    for (data_num = 0; data_num < LOAD_GAME_DATA_MAX; data_num++)       /* 1355 */
    {
        if (GetMemoryCardPlayDataFlg(data_num) != 0)                    /* 1357 */
        {
            if (load_game_snap_addr[data_num] != (void *)0)             /* 1358 */
            {
                SaveLoadSnapShotDisp(0, 0, alpha,
                                     load_game_snap_addr[data_num], data_num); /* 1360 */
            }

            if (load_game_ctrl.csr == data_num)                         /* 1364 */
            {
                SaveLoadSelNoDisp(0, 0, alpha, load_game_tex_addr, data_num);      /* 1366 */
                SaveLoadSelDataNumDisp(0, 0, alpha, load_game_tex_addr, data_num); /* 1368 */

                if (GetMemoryCardClearDataFlg(data_num) != 0)           /* 1370 */
                {
                    SaveLoadMcClearPlayDataInfoDisp(
                        0, 0, alpha, GetMemoryCardPlayDataPlayTime(data_num)); /* 1371 */
                }
                else
                {
                    SaveLoadMcPlayDataInfoDisp(
                        0, 0, alpha,
                        GetMemoryCardPlayDataChapter(data_num),         /* 1375 */
                        GetMemoryCardPlayDataRoomLabel(data_num),
                        GetMemoryCardPlayDataPlayTime(data_num));       /* 1380 */
                }
            }
            else
            {
                SaveLoadNonSelNoDisp(0, 0, alpha, load_game_tex_addr, data_num);      /* 1382 */
                SaveLoadNonSelDataNumDisp(0, 0, alpha, load_game_tex_addr, data_num); /* 1386 */
            }

            SaveLoadSnapShadowDisp(0, 0, alpha, load_game_tex_addr, data_num);

            if (GetMemoryCardPlayDataClearNum(data_num) >= 1)           /* 1389 */
            {
                SaveLoadClearFlareDisp(0, 0, alpha, load_game_tex_addr, data_num); /* 1391 */
                SaveLoadClearNumberDisp(GetMemoryCardPlayDataClearNum(data_num),
                                        0, 0, alpha, 0, 1, data_num,
                                        load_game_tex_addr);            /* 1393 */
            }
            else
            {
                SaveLoadNonClearMaskDisp(0, 0, alpha, load_game_tex_addr, data_num); /* 1397 */
            }
        }
        else
        {
            if (load_game_ctrl.csr == data_num)                         /* 1403 */
            {
                SaveLoadSelLineDisp(0, 0, alpha, load_game_tex_addr, data_num);    /* 1405 */
            }
            else
            {
                SaveLoadNonSelLineDisp(0, 0, alpha, load_game_tex_addr, data_num); /* 1409 */
            }

            SaveLoadNonClearMaskDisp(0, 0, alpha, load_game_tex_addr, data_num);   /* 1413 */
            SaveLoadSnapShadowDisp(0, 0, alpha, load_game_tex_addr, data_num);     /* 1415 */
        }
    }                                                                   /* 1417 */

    SaveLoadSelFlareDisp(0, 0, alpha, load_game_tex_addr,
                         (int)load_game_ctrl.csr);                      /* 1419 */
}
