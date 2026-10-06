// FILE: /home/zero_rom/zero2np/src/ingame/puzzle/puzzle.c
//
// The puzzle driver.
//
// Everything between the event stream saying "solve this" and one of the five
// puzzle modules owning the screen, plus the six clear flags that say it was
// solved.
//
// PuzzleStartReq() seeds pzl_exe_ctrl and jumps to GID_PUZZLE_INCONF, which
// keeps the world running underneath while the driver walks three steps: the
// yes/no prompt (step 0), the message that follows the answer (step 1), and
// the wait for the puzzle's texture pak (step 2).  Only Hina and Roku start at
// step 0 -- PzlExeCtrlInit() drops the other four straight to step 2, so they
// never ask.  Answering "no" is only possible for those two, and it is
// PzlExePuzzleLoadWait() that turns it back into GID_STORY_NORMAL.
//
// GID_PUZZLE_CROSSFADE then holds for 30 frames while the puzzle's own
// CrossScreenDisp() fades its board in over the live 3D world and the sound
// bank loads, and PzlCrossMovePuzzlePhase() hands over to the puzzle's phase.
// Each puzzle phase is a three-line wrapper -- init/Main+Disp/end -- and every
// end goes through PuzzleEndCmnExe() to put the story contrast filter back the
// way PzlExeCtrlInit() found it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), puzzle.o.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Statements whose only memory access goes through
// fixed_array's inlined operator[] leave no $LM of their own -- their line is
// swallowed by fixed_array.h's 124/125 -- and a store GCC parked in a delay
// slot can lose its note the same way; those few carry no annotation rather
// than a guessed one.

#include "puzzle.h"

#include <stddef.h>                                 /* NULL                   */

#include "puzzle_dat.h"                             /* pzl_conf_msg           */
#include "hina/hina_pzl.h"                          /* HinaPuzzle*            */
#include "kai/kai_pzl.h"                            /* KaiPuzzle*             */
#include "kaza/kaza_pzl.h"                          /* KazaPuzzle*            */
#include "kaza2/kaza2_pzl.h"                        /* KazaPuzzle2*           */
#include "roku/roku_pzl.h"                          /* SixPuzzle*             */

#include "../../common/mem_util.h"                  /* mem_utilGetMem         */
#include "../../common/utility2.h"                  /* PRINT_ASSERT           */
#include "../../common/variable.h"                  /* pad / plyr_wrk         */
#include "../../graphics/graph3d/ctl/fixed_array.h" /* fixed_array            */
#include "../../main/gphase.h"                      /* SetNextGPhase          */

#include "../ingame.h"                              /* IngameCameraMain       */
#include "../ingame_effect.h"                       /* IgEffectStoryMain...   */
#include "../enemy/enemy.h"                         /* AutoEnemyMain          */
#include "../event/prg/ev_disp.h"                   /* EvDispMain             */
#include "../map/MapFog.h"                          /* MapFogProc             */
#include "../map/MhCtl.h"                           /* MhCtlMain              */
#include "../map/map_bgm.h"                         /* map_bgmMain            */
#include "../menu/anim_2d.h"                        /* Anim2D_CalcNowAlpha    */
#include "../menu/play_data.h"                      /* PlayData_PlayTimeCount */
#include "../photo/finder.h"                        /* FilamentDrawLock       */
#include "../plyr/player.h"                         /* PlayerMainCmn          */
#include "../plyr/sis_mdl.h"                        /* sis_mdlMotionWork      */
#include "../plyr/sister.h"                         /* SisterMain             */

#include "../../graphics/effect/effect.h"           /* SetEffects_* / CutEffects*/
#include "../../graphics/graph2d/draw_cmn.h"        /* DrawCmnWindow          */
#include "../../graphics/graph2d/fade.h"            /* FadeInReq / FadeOutReq */
#include "../../graphics/graph2d/graph2d.h"         /* Graph2dMain            */
#include "../../graphics/graph2d/message.h"         /* PrintMsg               */
#include "../../graphics/graph3d/gra3d.h"           /* gra3dDraw              */
#include "../../system/eeiop/cddat.h"               /* GetFileSize / file ids */
#include "../../system/eeiop/snd_buffer.h"          /* SndBufIsPlaying        */
#include "../../system/eeiop/sndbank.h"             /* SndBankNew             */
#include "../../system/os/eecdvd.h"                 /* LoadReq / IsLoadEndAll */
#include "../../system/os/system.h"                 /* SystemBankPlay         */
#include "../../system/pad/pad.h"                   /* paddat / GetPadAnalogRpt */

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */

/* One flag per puzzle, raised by PuzzleClear() and read back by the event
 * system's PUZZLE_CLEAR condition.  It is handed to the memory card verbatim
 * by SetSave_ClearPuzzle(), which is why it is a plain byte array. */
static fixed_array<u_char, PZL_ID_MAX> clear_puzzle;        /* sbss 3f4f20 */

static PZL_EXE_CTRL pzl_exe_ctrl;                           /* bss  4bbde0 */

/* The puzzle's texture pak, allocated by PzlTexDataLoadReq() and handed out
 * to the puzzle modules through GetPzlTexDataAddr(). */
static void *pzl_tex_addr;                                  /* sbss 3f4f28 */

static void PzlExeCtrlInit(int puzzle_id);
static void PzlTexDataLoadReq(int puzzle_id);
static int  PzlExeConfMain(void);
static void PzlExeSelPad(void);
static void PzlExeConfDecision(void);
static void PzlExeConfMsgDispPad(void);
static int  PzlExePuzzleLoadWait(void);
static void PzlExeConfDispMain(void);
static void PzlExeConfWinDisp(int off_x, int off_y, u_char alpha);
static void PzlExeConfAfterMsgWinDisp(int off_x, int off_y, u_char alpha);
static void PzlExeSelCmnWinDisp(int off_x, int off_y, u_char alpha);
static void PzlCrossFadeMain(void);
static void PzlCrossFadeDispMain(void);
static void PzlCrossMovePuzzlePhase(void);
static void PuzzleEndCmnExe(void);

/* ==========================================================================
 *  Start-up
 * ======================================================================== */

void PuzzleInit(void)                                                   /* 152 */
{                                                                       /* 153 */
    int i;

    pzl_tex_addr = NULL;                                                /* 157 */

    for (i = 0; i < PZL_ID_MAX; i++) {                                  /* 159 */
        clear_puzzle[i] = 0;                                            /* 161 */
    }
}

/* Entry point from the event stream (ev_macro.c's PUZZLE_START). */
void PuzzleStartReq(int puzzle_id)                                      /* 177 */
{                                                                       /* 178 */
    if ((u_int)puzzle_id >= PZL_ID_MAX) {
        PRINT_ASSERT("Error! PuzzleStartReq puzzle_id %d", puzzle_id);  /* 179 */
    }

    PzlExeCtrlInit(puzzle_id);                                          /* 185 */
    SetNextGPhase(GID_PUZZLE_INCONF);                                   /* 188 */
}

/* --------------------------------------------------------------------------
 *  Which step a puzzle opens on is the whole difference between "are you sure?"
 *  and "just start".  Hina and Roku ask; the pinwheels and the two rotation
 *  locks are entered straight from the object the player used, so they do not.
 *
 *  The range check above the switch and the switch's own default are both
 *  present, and the >= 6 path falls through the first assert into the second.
 * ------------------------------------------------------------------------ */
static void PzlExeCtrlInit(int puzzle_id)                               /* 197 */
{                                                                       /* 198 */
    if ((u_int)puzzle_id >= PZL_ID_MAX) {                               /* 201 */
        PRINT_ASSERT("Error! PzlExeCtrlInit puzzle_id %d", puzzle_id);  /* 202 */
    }

    switch (puzzle_id) {                                                /* 207 */
    case PZL_ID_HINA:                                                   /* 210 */
    case PZL_ID_ROKU:
        pzl_exe_ctrl.step = 0;                                          /* 211 */
        break;

    case PZL_ID_KAZA:                                                   /* 224 */
    case PZL_ID_KAZA2:
    case PZL_ID_KAI1:
    case PZL_ID_KAI2:
        pzl_exe_ctrl.step = 2;                                          /* 225 */
        break;

    default:
        PRINT_ASSERT("Error! PzlExeCtrlInit");                          /* 227 */
        break;
    }

    pzl_exe_ctrl.conf_csr   = 1;                                        /* 230 */
    pzl_exe_ctrl.puzzle_id  = puzzle_id;                                /* 231 */
    pzl_exe_ctrl.fade_timer = 0;

    /* Latch the story contrast filter so PuzzleEndCmnExe() can restore it. */
    pzl_exe_ctrl.con_color = IgEffectStoryMainContrastColorGet();       /* 235 */
    pzl_exe_ctrl.con_alpha = IgEffectStoryMainContrastAlphaGet();       /* 236 */
}

/* --------------------------------------------------------------------------
 *  Each puzzle's board art is one pak, in five language variants laid out
 *  consecutively, so the file number is the table entry plus GetLanguage().
 *
 *  The table's kaza entries are crossed over relative to the puzzle ids: id 2
 *  (GID_PUZZLE_KAZA) loads puzzle_kaza_2.pk2 and id 3 (GID_PUZZLE_KAZA2) loads
 *  puzzle_kaza.pk2.  That is what the ROM stores; the two Kai puzzles share
 *  the latter as well, since kai_pzl.o draws only windows and messages.
 * ------------------------------------------------------------------------ */
static void PzlTexDataLoadReq(int puzzle_id)                            /* 255 */
{                                                                       /* 256 */
    static const int tex_data_tbl[PZL_ID_MAX] =                         /* rdata 3c43a0 */
    {
        PUZZLE_HINA_PK2,        /* 0 hina  */
        PUZZLE_ROKU_PK2,        /* 1 roku  */
        PUZZLE_KAZA_2_PK2,      /* 2 kaza  */
        PUZZLE_KAZA_PK2,        /* 3 kaza2 */
        PUZZLE_KAZA_PK2,        /* 4 kai1  */
        PUZZLE_KAZA_PK2,        /* 5 kai2  */
    };
    int file_no;

    if ((u_int)puzzle_id >= PZL_ID_MAX) {
        PRINT_ASSERT("Error! PzlTexDataLoadReq puzzle_id %d", puzzle_id); /* 257 */
    }

    if (pzl_tex_addr == NULL) {                                         /* 262 */
        file_no = tex_data_tbl[puzzle_id];
        pzl_tex_addr = mem_utilGetMem(GetFileSize(file_no + (char)GetLanguage())); /* 264 */
        LoadReq(file_no + (char)GetLanguage(), (uintptr_t)pzl_tex_addr); /* 266 */
    }
}

/* ==========================================================================
 *  GID_PUZZLE_INCONF -- the confirmation prompt
 * ======================================================================== */

/* Non-zero means "the prompt is still up, draw it". */
static int PzlExeConfMain(void)                                         /* 282 */
{                                                                       /* 283 */
    int res;

    res = 1;                                                            /* 286 */

    if (pzl_exe_ctrl.step == 0) {                                       /* 290 */
        PzlExeSelPad();                                                 /* 292 */
    } else if (pzl_exe_ctrl.step == 1) {                                /* 295 */
        PzlExeConfMsgDispPad();                                         /* 297 */
    } else if (pzl_exe_ctrl.step == 2) {                                /* 299 */
        res = PzlExePuzzleLoadWait();                                   /* 300 */
    } else {
        res = 1;
    }

    return res;                                                         /* 304 */
}

/* Left/right (stick or d-pad) toggles between yes and no; the ROM does not
 * separate the two directions, both just flip the cursor. */
static void PzlExeSelPad(void)                                          /* 309 */
{                                                                       /* 310 */
    if (((pad[0].rpt & 0x8000) != 0) || (GetPadAnalogRpt(2) != 0) ||    /* 314 */
        ((pad[0].rpt & 0x2000) != 0) || (GetPadAnalogRpt(3) != 0)) {    /* 319 */
        pzl_exe_ctrl.snd_id =
            SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 320 */
        pzl_exe_ctrl.conf_csr ^= 1;                                     /* 321 */
    } else if (*paddat[0] == 1) {                                       /* 324 */
        pzl_exe_ctrl.snd_id =
            SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 325 */

        if (pzl_exe_ctrl.conf_csr == 0) {                               /* 328 */
            PzlExeConfDecision();                                       /* 329 */
            return;
        }

        pzl_exe_ctrl.step = 2;                                          /* 333 */
    } else if (*paddat[1] == 1) {                                       /* 337 */
        pzl_exe_ctrl.snd_id =
            SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000); /* 338 */
        pzl_exe_ctrl.conf_csr = 1;                                      /* 339 */
        pzl_exe_ctrl.step     = 2;                                      /* 340 */
    }
}

/* Yes was chosen.  Hina goes on to show a message before loading; everything
 * else skips straight to the load wait. */
static void PzlExeConfDecision(void)                                    /* 361 */
{
    static const char next_step_tbl[PZL_ID_MAX] = { 1, 2, 2, 2, 2, 2 }; /* sdata 3f3af8 */

    pzl_exe_ctrl.step = next_step_tbl[pzl_exe_ctrl.puzzle_id];          /* 362 */
}

static void PzlExeConfMsgDispPad(void)                                  /* 374 */
{                                                                       /* 375 */
    int msg_state;

    msg_state = MesStatusCheck();                                       /* 379 */

    if (msg_state == 0) {                                               /* 380 */
        pzl_exe_ctrl.step = 2;
    } else if ((msg_state == 1) && (*paddat[3] == 1)) {                 /* 383 */
        MesSetNextPage();                                               /* 384 */
    }
}                                                                       /* 385 */

/* --------------------------------------------------------------------------
 *  Always returns 0, so the caller stops drawing the prompt the frame the
 *  load wait begins.
 *
 *  Answering "no" is only reachable for ids 0 and 1 -- the other four never
 *  visit step 0 -- which is why the cancel path sits inside the id < 2 arm.
 *  For "yes" the driver additionally waits out the decide cue so the puzzle
 *  does not open over the top of it.
 * ------------------------------------------------------------------------ */
static int PzlExePuzzleLoadWait(void)                                   /* 403 */
{
    if (IsLoadEndAll() != 0) {                                          /* 404 */
        if ((pzl_exe_ctrl.puzzle_id >= 0) &&
            (pzl_exe_ctrl.puzzle_id < PZL_ID_KAZA)) {                   /* 405 */
            if (pzl_exe_ctrl.conf_csr == 0) {                           /* 409 */
                if (SndBufIsPlaying(pzl_exe_ctrl.snd_id) != 0) {        /* 411 */
                    return 0;
                }
                SetNextGPhase(GID_PUZZLE_CROSSFADE);                    /* 412 */
            } else {
                SetNextGPhase(GID_STORY_NORMAL);                        /* 417 */
                PuzzleRelease();                                        /* 419 */
            }                                                           /* 421 */
        } else if (pzl_exe_ctrl.puzzle_id < PZL_ID_MAX) {
            SetNextGPhase(GID_PUZZLE_CROSSFADE);                        /* 427 */
        } else {
            PRINT_ASSERT("Error! PzlExePuzzleLoadWait");                /* 429 */
        }
    }

    return 0;                                                           /* 436 */
}

static void PzlExeConfDispMain(void)                                    /* 441 */
{                                                                       /* 442 */
    if (pzl_exe_ctrl.step == 0) {                                       /* 445 */
        PzlExeConfWinDisp(0, 0, 0x80);                                  /* 447 */
    } else if (pzl_exe_ctrl.step == 1) {                                /* 449 */
        PzlExeConfAfterMsgWinDisp(0, 0, 0x80);                          /* 450 */
    }
}

static void PzlExeConfWinDisp(int off_x, int off_y, u_char alpha)       /* 461 */
{                                                                       /* 462 */
    DISP_STR ds;

    SetMsgDefData(&ds, pzl_conf_msg[pzl_exe_ctrl.puzzle_id][0]);        /* 466 */

    PzlExeSelCmnWinDisp(off_x, off_y, alpha);                           /* 469 */

    /* 0xcf is the yes-to-no spacing, so conf_csr just scales it. */
    DrawCmnSelCsr(0, (float)(pzl_exe_ctrl.conf_csr * 0xcf + off_x + 0x9b),
                  (float)(off_y + 0x184), alpha, 0.0f, 0);              /* 473 */
    DrawCmnSelYes(0, (float)(off_x + 0x99), (float)(off_y + 0x186), alpha); /* 476 */
    DrawCmnSelNo(0, (float)(off_x + 0x169), (float)(off_y + 0x186), alpha); /* 477 */

    PrintMsg(pzl_conf_msg[pzl_exe_ctrl.puzzle_id][0],
             pzl_conf_msg[pzl_exe_ctrl.puzzle_id][1],
             ds.pos_x, ds.pos_y, 1, (int)alpha, 0);                     /* 479 */
}

/* The three arguments are the ROM's; none of them is used -- the after-answer
 * message is drawn at its own default position. */
static void PzlExeConfAfterMsgWinDisp(int off_x, int off_y, u_char alpha) /* 489 */
{                                                                       /* 490 */
    PrintMsgDef_W(0x44, 6);                                             /* 494 */
}

static void PzlExeSelCmnWinDisp(int off_x, int off_y, u_char alpha)     /* 504 */
{                                                                       /* 505 */
    MSG_WIN_DAT win_dat;

    SetMsgWinDefData(&win_dat, pzl_conf_msg[pzl_exe_ctrl.puzzle_id][0]); /* 508 */
    DrawCmnWindow(0, win_dat.x, win_dat.y, win_dat.w, win_dat.h, alpha, 0x80); /* 511 */
}

/* ==========================================================================
 *  GID_PUZZLE_CROSSFADE -- 30 frames of board-over-world
 * ======================================================================== */

/* The bank load is allowed to overrun the fade: the timer is pinned at 30
 * until SndBankIsReady(), so the board simply holds fully faded in. */
static void PzlCrossFadeMain(void)                                      /* 528 */
{                                                                       /* 529 */
    PzlCrossFadeDispMain();                                             /* 531 */

    pzl_exe_ctrl.fade_timer++;                                          /* 534 */

    if (pzl_exe_ctrl.fade_timer > 29) {                                 /* 536 */
        if (SndBankIsReady(pzl_exe_ctrl.snd_bank_id) != 0) {            /* 538 */
            PzlCrossMovePuzzlePhase();
        } else {
            pzl_exe_ctrl.fade_timer = 30;                               /* 541 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Two alpha ramps over the same 30 frames: in_alpha_tbl fades the board in
 *  from 128 to 0 (the puzzle's own CrossScreenDisp() reads it as "how much
 *  world still shows"), and con_alpha_tbl fades the story contrast filter out
 *  from wherever it stands.  The second table is a local because its start
 *  value has to be patched in at run time.
 *
 *  Kai (ids 4 and 5) has no board to fade and no contrast to drop -- its two
 *  switch arms are empty and the contrast request below is skipped -- so the two
 *  ramps are computed and thrown away for those.
 * ------------------------------------------------------------------------ */
static void PzlCrossFadeDispMain(void)                                  /* 550 */
{                                                                       /* 551 */
    static const ALPHA_ANIM_TBL in_alpha_tbl[2] =                       /* rdata 3c4430 */
    {
        { 128, 0, 0, 30 },
        {  -1, -1, -1, -1 },
    };
    ALPHA_ANIM_TBL con_alpha_tbl[2] =                                   /* 560 */
    {
        { 0, 0, 0, 30 },
        { -1, -1, -1, -1 },
    };
    u_char alpha;
    u_char con_alpha;

    con_alpha_tbl[0].start_alpha = (short)IgEffectStoryMainContrastAlphaGet(); /* 566 */

    alpha     = Anim2D_CalcNowAlpha((ALPHA_ANIM_TBL *)in_alpha_tbl,
                                    (int)pzl_exe_ctrl.fade_timer);      /* 568 */
    con_alpha = Anim2D_CalcNowAlpha(con_alpha_tbl,
                                    (int)pzl_exe_ctrl.fade_timer);      /* 569 */

    switch (pzl_exe_ctrl.puzzle_id) {                                   /* 572 */
    case PZL_ID_HINA:
        HinaPuzzleCrossScreenDisp(0, 0, alpha);                         /* 574 */
        break;                                                          /* 575 */
    case PZL_ID_ROKU:
        SixPuzzleCrossScreenDisp(0, 0, alpha);                          /* 577 */
        break;                                                          /* 578 */
    case PZL_ID_KAZA:
        KazaPuzzleCrossScreenDisp(0, 0, alpha);                         /* 580 */
        break;                                                          /* 581 */
    case PZL_ID_KAZA2:
        KazaPuzzle2CrossScreenDisp(0, 0, alpha);                        /* 583 */
        break;                                                          /* 584 */
    case PZL_ID_KAI1:
    case PZL_ID_KAI2:
        break;
    default:
        /* The ROM's own copy-paste: the message names PuzzleStartReq. */
        PRINT_ASSERT("Error! PuzzleStartReq");                          /* 590 */
        break;
    }

    if ((u_int)(pzl_exe_ctrl.puzzle_id - PZL_ID_KAI1) > 1) {            /* 597 */
        SetEffectsStoryContrast(IgEffectStoryMainContrastTypeGet(), 1,
                                pzl_exe_ctrl.con_color, con_alpha);     /* 600 */
    }
}

static void PzlCrossMovePuzzlePhase(void)                               /* 608 */
{                                                                       /* 609 */
    switch (pzl_exe_ctrl.puzzle_id) {                                   /* 612 */
    case PZL_ID_HINA:
        SetNextGPhase(GID_PUZZLE_HINA);                                 /* 614 */
        break;
    case PZL_ID_ROKU:
        SetNextGPhase(GID_PUZZLE_ROKU);                                 /* 617 */
        break;
    case PZL_ID_KAZA:
        SetNextGPhase(GID_PUZZLE_KAZA);                                 /* 620 */
        break;
    case PZL_ID_KAZA2:
        SetNextGPhase(GID_PUZZLE_KAZA2);                                /* 623 */
        break;
    case PZL_ID_KAI1:
        SetNextGPhase(GID_PUZZLE_KAI1);                                 /* 626 */
        break;
    case PZL_ID_KAI2:
        SetNextGPhase(GID_PUZZLE_KAI2);                                 /* 629 */
        break;
    default:
        PRINT_ASSERT("Error! PuzzleStartReq");                          /* 632 */
        break;
    }
}

/* ==========================================================================
 *  Shared teardown
 * ======================================================================== */

/* Put the story contrast filter back exactly as PzlExeCtrlInit() found it. */
static void PuzzleEndCmnExe(void)                                       /* 650 */
{
    SetEffectsStoryContrast(IgEffectStoryMainContrastTypeGet(), 1,
                            pzl_exe_ctrl.con_color, pzl_exe_ctrl.con_alpha); /* 651 */
    EffScreenEffectStatusSet(0);                                        /* 653 */
    PuzzleRelease();                                                    /* 656 */
}

void PuzzleRelease(void)                                                /* 663 */
{                                                                       /* 664 */
    if (pzl_tex_addr != NULL) {                                         /* 667 */
        mem_utilFreeMem(pzl_tex_addr);                                  /* 668 */
        pzl_tex_addr = NULL;                                            /* 669 */
    }
}

void *GetPzlTexDataAddr(void)
{
    return pzl_tex_addr;                                                /* 688 */
}

int GetPzlSndBankID(void)
{
    return pzl_exe_ctrl.snd_bank_id;                                    /* 699 */
}

/* ==========================================================================
 *  Clear flags
 * ======================================================================== */

void PuzzleClear(int puzzle_id)                                         /* 705 */
{
    clear_puzzle[puzzle_id] = 1;                                        /* 706 */
}

u_char GetPuzzleClearInfo(int puzzle_id)                                /* 719 */
{
    if ((u_int)puzzle_id >= PZL_ID_MAX) {
        PRINT_ASSERT("Error! GetPuzzleClearInfo puzzle_id %d", puzzle_id); /* 720 */
    }

    return clear_puzzle[puzzle_id];                                     /* 721 */
}

void SetSave_ClearPuzzle(MC_SAVE_DATA *data)                            /* 736 */
{
    data->addr = &clear_puzzle[0];                                      /* 737 */
    data->size = PZL_ID_MAX;                                            /* 741 */
}

/* ==========================================================================
 *  GPhase callbacks
 *
 *  InConf and CrossFade both keep the whole ingame frame running underneath,
 *  which is why the puzzle opens over a live room rather than a still.  The
 *  five puzzle phases run nothing but their own module -- the world is gone by
 *  then -- plus the play-time counter, which never stops.
 * ======================================================================== */

void init_Puzzle_InConf(void)                                           /* 752 */
{                                                                       /* 753 */
    PzlTexDataLoadReq(pzl_exe_ctrl.puzzle_id);                          /* 755 */
    SetPlyrAnime(0, 10);                                                /* 758 */
}

GPHASE_ENUM one_Puzzle_InConf(GPHASE_ENUM dummy)                        /* 760 */
{                                                                       /* 761 */
    PlayerMainCmn(1);                                                   /* 762 */

    SisterMain();                                                       /* 765 */

    AutoEnemyMain();                                                    /* 768 */

    map_bgmMain();                                                      /* 771 */

    MhCtlMain(GetPlyrAreaNo());                                         /* 774 */

    IngameCameraMain();                                                 /* 777 */

    PlayData_PlayTimeCount();                                           /* 782 */

    EnemyMotionWork();                                                  /* 784 */
    sis_mdlMotionWork();                                                /* 785 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,     /* 788 */
               plyr_wrk.cmn_wrk.mbox.pos);
    gra3dDraw();                                                        /* 789 */

    InitEffectsEF();                                                    /* 790 */
    EffectControl(5);                                                   /* 791 */
    BrightnessAdjustmentFilterDraw();                                   /* 792 */

    EvDispMain();                                                       /* 793 */

    Graph2dMain();                                                      /* 795 */

    if (PzlExeConfMain() != 0) {                                        /* 798 */
        PzlExeConfDispMain();                                           /* 799 */
    }

    return GPHASE_CONTINUE;                                             /* 802 */
}

void end_Puzzle_InConf(void)                                            /* 805 */
{                                                                       /* 806 */
}

void init_Puzzle_CrossFade(void)                                        /* 814 */
{                                                                       /* 815 */
    static const int snd_label_tbl[PZL_ID_MAX][2] =                     /* rdata 3c44f8 */
    {
        { PZL_HINADAN_BD,    PZL_HINADAN_HXD    },  /* 0 hina  */
        { PZL_ROKUMEN_BD,    PZL_ROKUMEN_HXD    },  /* 1 roku  */
        { PZL_KAZAGURUMA_BD, PZL_KAZAGURUMA_HXD },  /* 2 kaza  */
        { PZL_KAZAGURUMA_BD, PZL_KAZAGURUMA_HXD },  /* 3 kaza2 */
        { PZL_FUTAGO_BD,     PZL_FUTAGO_HXD     },  /* 4 kai1  */
        { PZL_FUTAGO_BD,     PZL_FUTAGO_HXD     },  /* 5 kai2  */
    };

    FilamentDrawLock();                                                 /* 828 */

    /* Only these two need their board set up before the fade can show it;
     * the rest build theirs in their own phase init. */
    if (pzl_exe_ctrl.puzzle_id == PZL_ID_HINA) {                        /* 831 */
        HinaPuzzleCrossDispInit();                                      /* 832 */
    } else if (pzl_exe_ctrl.puzzle_id == PZL_ID_KAZA2) {                /* 834 */
        KazaPuzzle2ExeInit();                                           /* 835 */
    }

    if ((u_int)(pzl_exe_ctrl.puzzle_id - PZL_ID_KAI1) > 1) {            /* 839 */
        EffScreenEffectStatusSet(2);                                    /* 841 */
    }

    FinderBankRelease();                                                /* 846 */
    pzl_exe_ctrl.snd_bank_id = SndBankNew(snd_label_tbl[pzl_exe_ctrl.puzzle_id][0],
                                          snd_label_tbl[pzl_exe_ctrl.puzzle_id][1],
                                          -1);                          /* 850 */

    pzl_exe_ctrl.fade_timer = 0;                                        /* 852 */

    SetEffects_OVERLAP(2, 20);                                          /* 854 */
}

GPHASE_ENUM one_Puzzle_CrossFade(GPHASE_ENUM dummy)                     /* 862 */
{                                                                       /* 863 */
    PlayerMainCmn(1);                                                   /* 864 */

    SisterMain();                                                       /* 867 */

    AutoEnemyMain();                                                    /* 870 */

    map_bgmMain();                                                      /* 873 */

    MhCtlMain(GetPlyrAreaNo());                                         /* 876 */

    IngameCameraMain();                                                 /* 879 */

    PlayData_PlayTimeCount();                                           /* 884 */

    EnemyMotionWork();                                                  /* 886 */
    sis_mdlMotionWork();                                                /* 887 */

    MapFogProc(GetPlyrAreaNo(), (int)(short)plyr_wrk.cmn_wrk.floor,     /* 890 */
               plyr_wrk.cmn_wrk.mbox.pos);
    gra3dDraw();                                                        /* 891 */

    InitEffectsEF();                                                    /* 892 */
    EffectControl(5);                                                   /* 893 */
    BrightnessAdjustmentFilterDraw();                                   /* 894 */

    EvDispMain();                                                       /* 895 */

    PzlCrossFadeMain();                                                 /* 896 */

    Graph2dMain();                                                      /* 903 */

    return GPHASE_CONTINUE;                                             /* 906 */
}

void end_Puzzle_CrossFade(void)                                         /* 912 */
{                                                                       /* 913 */
    FilamentDrawUnlock();

    SetEffectsStoryContrast(IgEffectStoryMainContrastTypeGet(), 1,
                            pzl_exe_ctrl.con_color, 0);                 /* 916 */
    CutEffects(8);                                                      /* 919 */
}

/* -------------------------------------------------------------------------- */

void init_Puzzle_Hina(void)
{
    HinaPuzzleExeInit();                                                /* 930 */
}

GPHASE_ENUM one_Puzzle_Hina(GPHASE_ENUM dummy)                          /* 936 */
{                                                                       /* 937 */
    if (HinaPuzzleMain() != 0) {                                        /* 938 */
        HinaPuzzleDispMain();
    }

    PlayData_PlayTimeCount();                                           /* 942 */

    return GPHASE_CONTINUE;                                             /* 944 */
}

/* Hina is the only puzzle that fades out and back in on the way out -- it
 * leaves straight into a cut-scene, so the seam has to be hidden. */
void end_Puzzle_Hina(void)                                              /* 951 */
{                                                                       /* 952 */
    PuzzleEndCmnExe();

    FadeOutReq(0, 0, 0, 0);                                             /* 956 */
    FadeInReq(0, 0, 0, 30);                                             /* 959 */
}

/* -------------------------------------------------------------------------- */

void init_Puzzle_Roku(void)
{
    SixPuzzleExeInit();                                                 /* 972 */
}

GPHASE_ENUM one_Puzzle_Roku(GPHASE_ENUM dummy)                          /* 978 */
{                                                                       /* 979 */
    if (SixPuzzleMain() != 0) {                                         /* 980 */
        SixPuzzleDispMain();
    }

    PlayData_PlayTimeCount();                                           /* 984 */

    return GPHASE_CONTINUE;                                             /* 986 */
}

void end_Puzzle_Roku(void)
{
    PuzzleEndCmnExe();                                                  /* 994 */
}

/* -------------------------------------------------------------------------- */

void init_Puzzle_Kaza(void)
{
    KazaPuzzleExeInit();                                                /* 1005 */
}

GPHASE_ENUM one_Puzzle_Kaza(GPHASE_ENUM dummy)                          /* 1011 */
{                                                                       /* 1012 */
    if (KazaPuzzleMain() != 0) {                                        /* 1013 */
        KazaPuzzleDispMain();
    }

    PlayData_PlayTimeCount();                                           /* 1017 */

    return GPHASE_CONTINUE;                                             /* 1019 */
}

/* The pinwheel puzzles print a message while they run, so the message box has
 * to be rewound before the story picks it up again. */
void end_Puzzle_Kaza(void)                                              /* 1026 */
{
    PuzzleEndCmnExe();                                                  /* 1027 */
    SetMsgFirstPage();                                                  /* 1029 */
}

/* -------------------------------------------------------------------------- */

void init_Puzzle_Kaza2(void)
{
    KazaPuzzle2ExeInit();                                               /* 1040 */
}

GPHASE_ENUM one_Puzzle_Kaza2(GPHASE_ENUM dummy)                         /* 1046 */
{                                                                       /* 1047 */
    if (KazaPuzzle2Main() != 0) {                                       /* 1048 */
        KazaPuzzle2DispMain();
    }

    PlayData_PlayTimeCount();                                           /* 1052 */

    return GPHASE_CONTINUE;                                             /* 1054 */
}

void end_Puzzle_Kaza2(void)                                             /* 1061 */
{
    PuzzleEndCmnExe();                                                  /* 1062 */
    SetMsgFirstPage();                                                  /* 1064 */
}

/* -------------------------------------------------------------------------- */

void init_Puzzle_Kai1(void)                                             /* 1074 */
{
    KaiPuzzleExeInit(PZL_ID_KAI1);                                      /* 1075 */
}

GPHASE_ENUM one_Puzzle_Kai1(GPHASE_ENUM dummy)                          /* 1081 */
{
    KaiPuzzleMain();                                                    /* 1082 */

    PlayData_PlayTimeCount();                                           /* 1085 */

    return GPHASE_CONTINUE;                                             /* 1087 */
}

void end_Puzzle_Kai1(void)
{
    PuzzleEndCmnExe();                                                  /* 1095 */
}

/* -------------------------------------------------------------------------- */

void init_Puzzle_Kai2(void)                                             /* 1105 */
{
    KaiPuzzleExeInit(PZL_ID_KAI2);                                      /* 1106 */
}

GPHASE_ENUM one_Puzzle_Kai2(GPHASE_ENUM dummy)                          /* 1112 */
{
    KaiPuzzleMain();                                                    /* 1113 */

    PlayData_PlayTimeCount();                                           /* 1116 */

    return GPHASE_CONTINUE;                                             /* 1118 */
}

void end_Puzzle_Kai2(void)
{
    PuzzleEndCmnExe();                                                  /* 1126 */
}
