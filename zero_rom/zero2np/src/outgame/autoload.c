// FILE: /home/zero_rom/zero2np/src/outgame/autoload.c
//
// The boot-time system-file probe.  Before the title comes up, the game looks
// for its system save on memory card slot 1, loads it if it is there, and puts
// up a windowed message for every way that can go wrong.  On the way out it
// always ends up in GID_UBI_MODE (the publisher screen) whatever happened.
//
// It is a nine-state machine on AUTO_LOAD_CTRL::step:
//   0 check init   1 check wait   2 load init   3 load wait   4 load confirm
//   5 error confirm   6 "start with defaults?" yes/no
//   7 card-empty error   8 card-empty yes/no
// Steps 0 and 2 fall through into 1 and 3, so a check or load begins on the
// same frame it is asked for.
//
// msg_id values are message-table ids passed to PrintMsg(0x50, ...); they are
// left as the ROM's literals because the table itself is not reconstructed.
//
// Some line numbers inside the two error switches are approximate: GCC
// cross-jumped the per-case stores into one shared tail, so several cases have
// no `$LM` of their own.  The control flow and every constant are exact.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "outgame.h"

#include "option.h"                         // InitOptionSetup / OptSoundSetupRef
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../common/variable.h"             // pad[] / opt_wrk
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnTwoLineWindow / DrawCmnYesNoSel
#include "../graphics/graph2d/message.h"    // PrintMsg
#include "../main/gphase.h"                 // SetNextGPhase / GID_UBI_MODE
#include "../main/phasefunc.h"              // GPHASE_ENUM + phase-callback prototypes
#include "../system/mc/prg/mc.h"            // MemoryCardExeInit / MemoryCardEnd
#include "../system/mc/prg/mc_check.h"      // MemoryCardCheckInit / Main
#include "../system/mc/prg/mc_check_broken.h" // Dir/File broken, NewFileLoad
#include "../system/mc/prg/mc_check_card.h" // MemoryCardSetAccessPort
#include "../system/mc/prg/mc_check_empty.h" // MemoryCardCheckEmpty
#include "../system/mc/prg/mc_load.h"       // MemoryCardFileLoadInit / Main
#include "../system/mc/prg/mc_set_data.h"   // path/size/Develop/Get/LiberateDataMemoryArea
#include "../system/os/system.h"            // SystemBankPlay
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <string.h>                         // memset

/* AUTO_LOAD_CTRL::step */
#define AL_CHECK_INIT     0
#define AL_CHECK_WAIT     1
#define AL_LOAD_INIT      2
#define AL_LOAD_WAIT      3
#define AL_LOAD_CONF      4
#define AL_ERROR_CONF     5
#define AL_DEF_START_CONF 6
#define AL_EMPTY_ERROR    7
#define AL_EMPTY_WARNING  8

/* Frames the "loaded" confirmation sits on screen before moving on. */
#define AL_LOAD_CONF_TIME 150

/* autoload.c's only work block; the type is file-local because nothing else
 * in the ROM refers to it. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char  step;
    /* 0x1 */ char  csr;            /* yes/no cursor; 1 = No, the default */
    /* 0x2 */ short move_timer;
    /* 0x4 */ int   msg_id;
} AUTO_LOAD_CTRL;

static void *auto_load_data_buff;                                       /* sdata 3ef520 */
static AUTO_LOAD_CTRL auto_load_ctrl;                                   /* sbss  3f4ae8 */

static void AutoLoadInit(void);
static void AutoLoadMain(void);
static void AutoLoadMcCheckInit(void);
static void AutoLoadMcCheckWait(void);
static void AutoLoadMcLoadInit(void);
static void AutoLoadMcLoadWait(void);
static void AutoLoadMcLoadConf(void);
static void AutoLoadMcErrorConf(void);
static void AutoLoadMcDefStartConf(void);
static void AutoLoadMcEmptyError(void);
static void AutoLoadMcEmptyWarning(void);
static void AutoLoadDispMain(void);

static void AutoLoadInit(void)
{
    auto_load_ctrl.msg_id = 0;                                          /* 114 */
    auto_load_ctrl.csr = '\x01';                                        /* 117 */
    auto_load_ctrl.step = AL_CHECK_INIT;                                /* 118 */
    auto_load_ctrl.move_timer = 0;                                      /* 120 */

    if (auto_load_data_buff != (void *)0)                               /* 123 */
    {
        LiberateDataMemoryArea(auto_load_data_buff);                    /* 124 */
        auto_load_data_buff = (void *)0;                                /* 125 */
    }

    MemoryCardSetAccessPort(0);                                         /* 129 */
}

/* path_name is declared and zeroed here and then never used -- the two states
 * that need one build their own.  Reproduced as found. */
static void AutoLoadMain(void)
{
    char path_name[55];

    memset(path_name, 0, sizeof(path_name));                            /* 149 */

    switch (auto_load_ctrl.step)                                        /* 164 */
    {
    case AL_CHECK_INIT:
        AutoLoadMcCheckInit();                                          /* 166 */
        /* fall through */
    case AL_CHECK_WAIT:
        AutoLoadMcCheckWait();                                          /* 169 */
        break;                                                          /* 170 */

    case AL_LOAD_INIT:
        AutoLoadMcLoadInit();                                           /* 172 */
        /* fall through */
    case AL_LOAD_WAIT:
        AutoLoadMcLoadWait();                                           /* 175 */
        break;                                                          /* 176 */

    case AL_LOAD_CONF:
        AutoLoadMcLoadConf();                                           /* 178 */
        break;                                                          /* 179 */

    case AL_ERROR_CONF:
        AutoLoadMcErrorConf();                                          /* 181 */
        break;                                                          /* 182 */

    case AL_DEF_START_CONF:
        AutoLoadMcDefStartConf();                                       /* 184 */
        break;                                                          /* 185 */

    case AL_EMPTY_ERROR:
        AutoLoadMcEmptyError();                                         /* 187 */
        break;                                                          /* 188 */

    case AL_EMPTY_WARNING:
        AutoLoadMcEmptyWarning();                                       /* 190 */
        break;                                                          /* 191 */

    default:
        PRINT_ASSERT("Error! AutoLoadMain");                            /* 193 */
        break;
    }
}

static void AutoLoadMcCheckInit(void)
{
    char path_name[55];

    memset(path_name, 0, sizeof(path_name));                            /* 207 */

    MemoryCardMakeSearchDirPath(path_name, 0);                          /* 211 */
    MemoryCardCheckInit(0, 0, path_name);                               /* 213 */

    auto_load_ctrl.step = AL_CHECK_WAIT;                                /* 215 */
}

/* MemoryCardCheckMain() answers 1 on success, 0 while busy and a negative
 * error otherwise.  -1 ("no directory") is not an error here -- it is the
 * first-boot path, and it goes to the load step exactly like a good check
 * whose directory turned out to be intact. */
static void AutoLoadMcCheckWait(void)
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 229 */

    if (mc_res == 1)                                                    /* 232 */
    {
        if (MemoryCardCheckDirBroken(0) == 0)                           /* 234 */
        {
            auto_load_ctrl.step = AL_ERROR_CONF;                        /* 241 */
            auto_load_ctrl.msg_id = 0x19;                               /* 242 */
        }
        else
        {
            auto_load_ctrl.step = AL_LOAD_INIT;                         /* 251 */
            auto_load_ctrl.msg_id = 0x1d;                               /* 252 */
        }
    }
    else if (mc_res < 0)                                                /* 246 */
    {
        switch (mc_res)                                                 /* 248 */
        {
        case -1:                    /* no directory -- first boot */
            auto_load_ctrl.step = AL_LOAD_INIT;                         /* 251 */
            auto_load_ctrl.msg_id = 0x1d;                               /* 252 */
            return;                                                     /* 253 */

        case -2:                    /* no card */
            auto_load_ctrl.msg_id = 0x18;                               /* 255 */
            break;                                                      /* 257 */

        case -4:                    /* unformatted / full */
            if (MemoryCardCheckEmpty(0) == 0)                           /* 260 */
            {
                auto_load_ctrl.step = AL_EMPTY_ERROR;                   /* 267 */
                auto_load_ctrl.msg_id = 0x37;                           /* 270 */
                return;
            }
            auto_load_ctrl.msg_id = 0x18;                               /* 262 */
            break;                                                      /* 263 */

        case -6:                    /* directory unreadable */
            auto_load_ctrl.msg_id = 0x19;                               /* 272 */
            break;                                                      /* 274 */

        case -20:                   /* card removed mid-operation */
            auto_load_ctrl.msg_id = 0x38;                               /* 276 */
            break;                                                      /* 278 */

        default:
            auto_load_ctrl.msg_id = 2;                                  /* 282 */
            break;
        }

        auto_load_ctrl.step = AL_ERROR_CONF;                            /* 283 */
    }
}

static void AutoLoadMcLoadInit(void)
{
    int size;
    char path_name[55];

    memset(path_name, 0, sizeof(path_name));                            /* 299 */

    MemoryCardSetFilePath(path_name, 0, 0);                             /* 303 */

    size = GetMemoryCardDataSize(0, 0);                                 /* 305 */
    auto_load_data_buff = GetDataMemoryArea(size);                      /* 307 */

    MemoryCardFileLoadInit(0, 0, path_name, auto_load_data_buff, size); /* 309 */

    auto_load_ctrl.msg_id = 0x1d;                                       /* 312 */
    auto_load_ctrl.step = AL_LOAD_WAIT;                                 /* 313 */
}

/* The buffer is released on any frame the load is no longer busy -- success,
 * failure or a broken-file verdict alike -- which is why the free sits after
 * the whole if/else rather than in each arm. */
static void AutoLoadMcLoadWait(void)
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();

    if (mc_res == 1)
    {
        size = GetMemoryCardDataSize(0, 0);

        if (MemoryCardCheckFileBroken(auto_load_data_buff, size) == 0)
        {
            auto_load_ctrl.step = AL_ERROR_CONF;
            auto_load_ctrl.msg_id = 0x19;
        }
        else if (MemoryCardCheckNewFileLoad(auto_load_data_buff, size) != 0)
        {
            /* Written by a newer build than this one. */
            auto_load_ctrl.step = AL_ERROR_CONF;
            auto_load_ctrl.msg_id = 0x33;
        }
        else
        {
            DevelopMemoryCardLoadData((char *)auto_load_data_buff, 0, 0);
            OptSoundSetupRef(&opt_wrk);

            auto_load_ctrl.step = AL_LOAD_CONF;
            auto_load_ctrl.msg_id = 0x10;
            auto_load_ctrl.move_timer = 0;
        }
    }
    else if (mc_res < 0)
    {
        auto_load_ctrl.msg_id = 0xf;

        switch (mc_res)
        {
        case -3:
            auto_load_ctrl.msg_id = 0x33;
            break;

        case -2:
            auto_load_ctrl.msg_id = 0x18;
            break;

        default:
            auto_load_ctrl.msg_id = 0xf;
            break;
        }

        auto_load_ctrl.step = AL_ERROR_CONF;
    }

    if ((mc_res != 0) && (auto_load_data_buff != (void *)0))
    {
        LiberateDataMemoryArea(auto_load_data_buff);
        auto_load_data_buff = (void *)0;
    }
}

/* Confirmed by CROSS or by 150 frames going past, whichever comes first. */
static void AutoLoadMcLoadConf(void)
{
    if (*paddat[0] == 1)                                                /* 403 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 404 */
        SetNextGPhase(GID_UBI_MODE);                                    /* 405 */
    }

    auto_load_ctrl.move_timer++;                                        /* 409 */

    if (auto_load_ctrl.move_timer > AL_LOAD_CONF_TIME - 1)              /* 410 */
    {
        SetNextGPhase(GID_UBI_MODE);                                    /* 411 */
    }
}

static void AutoLoadMcErrorConf(void)
{
    if (*paddat[0] == 1)                                                /* 425 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 426 */

        auto_load_ctrl.csr = '\x01';                                    /* 427 */
        auto_load_ctrl.step = AL_DEF_START_CONF;                        /* 428 */
        auto_load_ctrl.msg_id = 0x23;                                   /* 429 */
    }
}

/* "Start with the default settings?"  No (csr 1, the default) sends the whole
 * machine back to step 0 and tries the card again. */
static void AutoLoadMcDefStartConf(void)
{
    if (((pad[0].rpt & 0x8000U) != 0) ||
        (GetPadAnalogRpt(2) != 0) ||                                    /* 442 */
        ((pad[0].rpt & 0x2000U) != 0) ||
        (GetPadAnalogRpt(3) != 0))                                      /* 447 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 448 */
        auto_load_ctrl.csr ^= 1;                                        /* 449 */
    }
    else if (*paddat[0] == 1)                                           /* 452 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 453 */

        if (auto_load_ctrl.csr == '\0')                                 /* 455 */
        {
            auto_load_ctrl.msg_id = 0x25;                               /* 456 */
            auto_load_ctrl.step = AL_LOAD_CONF;                         /* 457 */
            auto_load_ctrl.move_timer = 0;                              /* 458 */

            InitOptionSetup(&opt_wrk);                                  /* 461 */
        }
        else
        {
            auto_load_ctrl.msg_id = 0;                                  /* 466 */
            auto_load_ctrl.step = AL_CHECK_INIT;
        }
    }
    else if (*paddat[1] == 1)                                           /* 470 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 471 */
        auto_load_ctrl.msg_id = 0;                                      /* 472 */
        auto_load_ctrl.step = AL_CHECK_INIT;                            /* 473 */
    }
}

static void AutoLoadMcEmptyError(void)
{
    if (*paddat[0] == 1)                                                /* 486 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 487 */

        auto_load_ctrl.csr = '\x01';                                    /* 488 */
        auto_load_ctrl.step = AL_EMPTY_WARNING;                         /* 489 */
        auto_load_ctrl.msg_id = 0x24;                                   /* 490 */
    }
}

/* Same shape as AutoLoadMcDefStartConf() but without the InitOptionSetup() --
 * there was nothing to load, so there is nothing to reset. */
static void AutoLoadMcEmptyWarning(void)
{
    if (((pad[0].rpt & 0x8000U) != 0) ||
        (GetPadAnalogRpt(2) != 0) ||                                    /* 502 */
        ((pad[0].rpt & 0x2000U) != 0) ||
        (GetPadAnalogRpt(3) != 0))                                      /* 507 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 508 */
        auto_load_ctrl.csr ^= 1;                                        /* 509 */
    }
    else if (*paddat[0] == 1)                                           /* 512 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 513 */

        if (auto_load_ctrl.csr == '\0')                                 /* 515 */
        {
            auto_load_ctrl.msg_id = 0x25;                               /* 517 */
            auto_load_ctrl.step = AL_LOAD_CONF;                         /* 518 */
            auto_load_ctrl.move_timer = 0;
        }
        else
        {
            auto_load_ctrl.msg_id = 0;                                  /* 523 */
            auto_load_ctrl.step = AL_CHECK_INIT;
        }
    }
    else if (*paddat[1] == 1)                                           /* 527 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 528 */
        auto_load_ctrl.msg_id = 0;                                      /* 529 */
        auto_load_ctrl.step = AL_CHECK_INIT;                            /* 530 */
    }
}

/* Two window widths, both 550 -- separate .lit4 slots, one per expansion. */
static void AutoLoadDispMain(void)
{
    switch (auto_load_ctrl.step)                                        /* 543 */
    {
    case AL_CHECK_INIT:
    case AL_CHECK_WAIT:
    case AL_LOAD_INIT:
    case AL_LOAD_WAIT:
    case AL_LOAD_CONF:
    case AL_ERROR_CONF:
    case AL_EMPTY_ERROR:
        DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f,
                             0x80, 0x80);                               /* 554 */
        PrintMsg(0x50, auto_load_ctrl.msg_id, 0x5c, 0x8e, 1, 0x80, 0);  /* 564 */
        break;                                                          /* 566 */

    case AL_DEF_START_CONF:
    case AL_EMPTY_WARNING:
        DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f,
                             0x80, 0x80);                               /* 572 */
        DrawCmnYesNoSel((int)auto_load_ctrl.csr, 295.0f, 0x80, 0);      /* 574 */
        PrintMsg(0x50, auto_load_ctrl.msg_id, 0x5c, 0x8e, 1, 0x80, 0);  /* 576 */
        break;

    default:
        PRINT_ASSERT("Error! AutoLoadDispMain");                        /* 580 */
        break;
    }
}

// ──────────────────────────────────────────────────────────────────────
// GPhase per-phase callbacks, invoked via the main/gphase.c dispatch tables.

void init_AutoLoad_Main(void)
{
    MemoryCardExeInit();                                                /* 592 */
    AutoLoadInit();                                                     /* 595 */
}

GPHASE_ENUM one_AutoLoad_Main(GPHASE_ENUM dummy)
{
    (void)dummy;

    AutoLoadMain();                                                     /* 600 */
    AutoLoadDispMain();                                                 /* 603 */

    return GPHASE_CONTINUE;                                             /* 605 */
}

void end_AutoLoad_Main(void)
{
    if (auto_load_data_buff != (void *)0)                               /* 608 */
    {
        LiberateDataMemoryArea(auto_load_data_buff);                    /* 609 */
        auto_load_data_buff = (void *)0;                                /* 610 */
    }

    MemoryCardEnd();                                                    /* 615 */
}
