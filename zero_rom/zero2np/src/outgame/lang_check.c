// FILE: /home/zero_rom/zero2np/src/outgame/lang_check.c
//
// The boot-time language probe.  Structurally a trimmed autoload.c: it runs
// the same memory-card check/load pair, but instead of confirming settings it
// takes the language out of the saved system file, commits it, and pulls the
// language data.  If the card cannot be read at all it falls through to the
// language-select screen instead of putting a yes/no window up.
//
// Eight states on LANG_CHECK_CTRL::step:
//   0 check init   1 check wait   2 load init   3 load wait   4 load confirm
//   5 commit + request language data   6 wait for it   7 error -> LangSel
// Steps 0, 2 and 5 fall through into their wait states.
//
// Some line numbers inside the error switch are approximate for the same
// reason autoload.c's are: GCC cross-jumped the per-case stores.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "lang_sel.h"                       // LoadLangSetUp / LangData_Load*
#include "option.h"                         // OptSoundSetupRef
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../common/variable.h"             // pad[] / opt_wrk
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnTwoLineWindow
#include "../graphics/graph2d/message.h"    // PrintMsg
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../main/phasefunc.h"              // GPHASE_ENUM + phase-callback prototypes
#include "../system/mc/prg/mc.h"            // MemoryCardExeInit / MemoryCardEnd
#include "../system/mc/prg/mc_check.h"      // MemoryCardCheckInit / Main
#include "../system/mc/prg/mc_check_broken.h" // Dir/File broken, NewFileLoad
#include "../system/mc/prg/mc_check_empty.h" // MemoryCardCheckEmpty
#include "../system/mc/prg/mc_load.h"       // MemoryCardFileLoadInit / Main
#include "../system/mc/prg/mc_set_data.h"   // path/size/Develop/Get/LiberateDataMemoryArea
#include "../system/os/system.h"            // SystemBankPlay
#include "../system/pad/pad.h"              // paddat

#include <string.h>                         // memset

/* LANG_CHECK_CTRL::step */
#define LC_CHECK_INIT   0
#define LC_CHECK_WAIT   1
#define LC_LOAD_INIT    2
#define LC_LOAD_WAIT    3
#define LC_LOAD_CONF    4
#define LC_DATA_REQ     5
#define LC_DATA_WAIT    6
#define LC_ERROR_CONF   7

#define LC_LOAD_CONF_TIME 150

/* lang_check.c's work block; file-local because nothing else refers to it. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ char  step;
    /* 0x2 */ short move_timer;
    /* 0x4 */ int   msg_id;
} LANG_CHECK_CTRL;

static void *lang_check_data_buff;                                      /* sdata 3f17b8 */
static LANG_CHECK_CTRL lang_check_ctrl;                                 /* sbss  3f4d48 */

static void LangCheckInit(void);
static void LangCheckMain(void);
static void LangCheckMcCheckInit(void);
static void LangCheckMcCheckWait(void);
static void LangCheckMcLoadInit(void);
static void LangCheckMcLoadWait(void);
static void LangCheckMcLoadConf(void);
static void LangCheckMcErrorConf(void);
static void LangCheckDispMain(void);

/* No csr here -- there is no yes/no window in this screen, so unlike
 * AutoLoadInit() there are only three fields to reset. */
static void LangCheckInit(void)
{
    lang_check_ctrl.msg_id = 0;                                         /* 109 */
    lang_check_ctrl.step = LC_CHECK_INIT;                               /* 112 */
    lang_check_ctrl.move_timer = 0;                                     /* 114 */

    if (lang_check_data_buff != nullptr)                              /* 117 */
    {
        LiberateDataMemoryArea(lang_check_data_buff);                   /* 118 */
        lang_check_data_buff = nullptr;                               /* 119 */
    }
}

static void LangCheckMain(void)
{
    char path_name[55];

    memset(path_name, 0, sizeof(path_name));                            /* 140 */

    switch (lang_check_ctrl.step)                                       /* 143 */
    {
    case LC_CHECK_INIT:
        LangCheckMcCheckInit();                                         /* 145 */
        /* fall through */
    case LC_CHECK_WAIT:
        LangCheckMcCheckWait();                                         /* 148 */
        break;                                                          /* 149 */

    case LC_LOAD_INIT:
        LangCheckMcLoadInit();                                          /* 151 */
        /* fall through */
    case LC_LOAD_WAIT:
        LangCheckMcLoadWait();                                          /* 154 */
        break;                                                          /* 155 */

    case LC_LOAD_CONF:
        LangCheckMcLoadConf();                                          /* 157 */
        break;                                                          /* 158 */

    case LC_DATA_REQ:
        LoadLangSetUp();                                                /* 160 */
        LangData_LoadReq();                                             /* 162 */
        lang_check_ctrl.step = LC_DATA_WAIT;                            /* 164 */
        /* fall through */
    case LC_DATA_WAIT:
        if (LangData_LoadWait() != 0)                                   /* 167 */
        {
            SetNextGPhase(GID_UBI_MODE);                                /* 168 */
        }
        break;                                                          /* 171 */

    case LC_ERROR_CONF:
        LangCheckMcErrorConf();                                         /* 173 */
        break;                                                          /* 174 */

    default:
        PRINT_ASSERT("Error! LangCheckMain");                           /* 176 */
        break;
    }
}

static void LangCheckMcCheckInit(void)
{
    char path_name[55];

    memset(path_name, 0, sizeof(path_name));                            /* 190 */

    MemoryCardMakeSearchDirPath(path_name, 0);                          /* 194 */
    MemoryCardCheckInit(0, 0, path_name);                               /* 196 */

    lang_check_ctrl.step = LC_CHECK_WAIT;                               /* 198 */
}

/* Same error map as autoload.c's, minus the empty-card special case: here a
 * full card just gets msg 0x37 and still goes to the error step, because the
 * error step is only ever a route to the language-select screen. */
static void LangCheckMcCheckWait(void)
{
    int mc_res;

    mc_res = MemoryCardCheckMain();                                     /* 213 */

    if (mc_res == 1)                                                    /* 216 */
    {
        if (MemoryCardCheckDirBroken(0) == 0)                           /* 218 */
        {
            lang_check_ctrl.step = LC_ERROR_CONF;                       /* 225 */
            lang_check_ctrl.msg_id = 0x19;                              /* 226 */
        }
        else
        {
            lang_check_ctrl.step = LC_LOAD_INIT;                        /* 235 */
            lang_check_ctrl.msg_id = 0x1d;                              /* 236 */
        }
    }
    else if (mc_res < 0)                                                /* 230 */
    {
        switch (mc_res)                                                 /* 232 */
        {
        case -1:                    /* no directory -- first boot */
            lang_check_ctrl.step = LC_LOAD_INIT;                        /* 235 */
            lang_check_ctrl.msg_id = 0x1d;                              /* 236 */
            return;                                                     /* 237 */

        case -2:
            lang_check_ctrl.msg_id = 0x18;                              /* 239 */
            break;                                                      /* 241 */

        case -4:
            if (MemoryCardCheckEmpty(0) == 0)                           /* 244 */
            {
                lang_check_ctrl.msg_id = 0x37;                          /* 251 */
            }
            else
            {
                lang_check_ctrl.msg_id = 0x18;                          /* 246 */
            }
            break;                                                      /* 247 */

        case -6:
            lang_check_ctrl.msg_id = 0x19;                              /* 256 */
            break;                                                      /* 258 */

        case -20:
            lang_check_ctrl.msg_id = 0x38;                              /* 260 */
            break;                                                      /* 262 */

        default:
            lang_check_ctrl.msg_id = 2;                                 /* 266 */
            break;
        }

        lang_check_ctrl.step = LC_ERROR_CONF;                           /* 267 */
    }
}

static void LangCheckMcLoadInit(void)
{
    int size;
    char path_name[55];

    memset(path_name, 0, sizeof(path_name));                            /* 283 */

    MemoryCardSetFilePath(path_name, 0, 0);                             /* 287 */

    size = GetMemoryCardDataSize(0, 0);                                 /* 289 */
    lang_check_data_buff = GetDataMemoryArea(size);                     /* 291 */

    MemoryCardFileLoadInit(0, 0, path_name, lang_check_data_buff, size);/* 293 */

    lang_check_ctrl.msg_id = 0x1d;                                      /* 296 */
    lang_check_ctrl.step = LC_LOAD_WAIT;                                /* 297 */
}

static void LangCheckMcLoadWait(void)
{
    int mc_res;
    int size;

    mc_res = MemoryCardFileLoadMain();                                  /* 316 */

    if (mc_res == 1)                                                    /* 318 */
    {
        size = GetMemoryCardDataSize(0, 0);                             /* 321 */

        if (MemoryCardCheckFileBroken(lang_check_data_buff, size) == 0) /* 323 */
        {
            lang_check_ctrl.step = LC_ERROR_CONF;
            lang_check_ctrl.msg_id = 0x19;                              /* 328 */
        }
        else if (MemoryCardCheckNewFileLoad(lang_check_data_buff,
                                            size) != 0)                 /* 329 */
        {
            /* Written by a newer build than this one. */
            lang_check_ctrl.step = LC_ERROR_CONF;
            lang_check_ctrl.msg_id = 0x33;                              /* 334 */
        }
        else
        {
            DevelopMemoryCardLoadData((char *)lang_check_data_buff, 0, 0); /* 336 */
            OptSoundSetupRef(&opt_wrk);                                 /* 337 */

            lang_check_ctrl.step = LC_LOAD_CONF;                        /* 339 */
            lang_check_ctrl.msg_id = 0x10;
            lang_check_ctrl.move_timer = 0;
        }
    }
    else if (mc_res < 0)                                                /* 344 */
    {
        lang_check_ctrl.msg_id = 0xf;                                   /* 345 */

        switch (mc_res)                                                 /* 349 */
        {
        case -3:
            lang_check_ctrl.msg_id = 0x33;                              /* 351 */
            break;

        case -2:
            lang_check_ctrl.msg_id = 0x18;                              /* 354 */
            break;

        default:
            lang_check_ctrl.msg_id = 0xf;                               /* 357 */
            break;
        }

        lang_check_ctrl.step = LC_ERROR_CONF;                           /* 364 */
    }

    if ((mc_res != 0) && (lang_check_data_buff != nullptr))           /* 372 */
    {
        LiberateDataMemoryArea(lang_check_data_buff);                   /* 373 */
        lang_check_data_buff = nullptr;                               /* 374 */
    }
}

/* CROSS or 150 frames, then commit the language that came off the card. */
static void LangCheckMcLoadConf(void)
{
    if (*paddat[0] == 1)                                                /* 387 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 388 */
        lang_check_ctrl.step = LC_DATA_REQ;                             /* 389 */
    }

    lang_check_ctrl.move_timer++;                                       /* 392 */

    if (lang_check_ctrl.move_timer > LC_LOAD_CONF_TIME - 1)             /* 393 */
    {
        lang_check_ctrl.step = LC_DATA_REQ;                             /* 394 */
    }
}

/* No confirmation and no message -- a card that cannot be read simply means
 * the player has to pick a language by hand. */
static void LangCheckMcErrorConf(void)
{
    SetNextGPhase(GID_LANGSEL_MAIN);                                    /* 409 */
}

/* Steps 5 and 6 draw nothing: the window would be up for the one or two
 * frames the language data takes, so the ROM leaves them blank. */
static void LangCheckDispMain(void)
{
    if ((lang_check_ctrl.step >= LC_CHECK_INIT) &&
        (lang_check_ctrl.step < LC_DATA_REQ))                           /* 437 */
    {
        DrawCmnTwoLineWindow(0, 45.0f, 126.0f, 550.0f, 216.0f,
                             0x80, 0x80);                               /* 447 */
        PrintMsg(0x50, lang_check_ctrl.msg_id, 0x5c, 0x8e, 1, 0x80, 0); /* 448 */
    }
    else if ((lang_check_ctrl.step < LC_CHECK_INIT) ||
             (lang_check_ctrl.step > LC_ERROR_CONF))
    {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                       /* 462 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// GPhase per-phase callbacks, invoked via the main/gphase.c dispatch tables.

void init_LangData_Check(void)
{
    MemoryCardExeInit();                                                /* 474 */
    LangCheckInit();                                                    /* 477 */
}

GPHASE_ENUM one_LangData_Check(GPHASE_ENUM dummy)
{
    (void)dummy;

    LangCheckMain();                                                    /* 482 */
    LangCheckDispMain();                                                /* 485 */

    return GPHASE_CONTINUE;                                             /* 487 */
}

void end_LangData_Check(void)
{
    if (lang_check_data_buff != nullptr)                              /* 490 */
    {
        LiberateDataMemoryArea(lang_check_data_buff);                   /* 491 */
        lang_check_data_buff = nullptr;                               /* 492 */
    }

    MemoryCardEnd();                                                    /* 497 */
}
