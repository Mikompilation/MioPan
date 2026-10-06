// FILE: /home/zero_rom/zero2np/src/outgame/newgame.c
//
// The new-game screen: pick a difficulty, then fade to black and start the
// story.  Two items only, so LEFT and RIGHT are the same toggle.
//
// The hand-off is gated on three separate things -- 30 frames of black, the
// loading texture being resident, and the confirm SE having finished playing.
// The last of those is why NewGameDifficultySelPad() routes its confirm sound
// through the *title* bank (SndBankPlay + SetTitleSoundID) rather than the
// system bank: it needs a play id it can poll.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "newgame.h"

#include "mission_sel.h"                    // MissionSelTblInit
#include "title.h"                          // Get/SetTitle* accessors
#include "title_disp.h"                     // DispTitleZeroLogo / Cursor L,R / Caption
#include "tim_dat/title_dat.h"              // title_top[] / newgame_*_x_tbl
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../common/variable.h"             // pad[]
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../ingame/ingame.h"               // InitCostume / IngameWrkInit
#include "../ingame/loading/loading.h"      // LoadingTexLoadWait
#include "../ingame/menu/anim_2d.h"         // ALPHA_ANIM_TBL / Anim2D_CalcNowAlpha
#include "../ingame/event/prg/ev_main.h"    // EventDataLoadReq
#include "../main/gphase.h"                 // SetNextGPhase / GID_*
#include "../system/eeiop/snd_buffer.h"     // SndBufIsPlaying
#include "../system/eeiop/sndbank.h"        // SndBankPlay
#include "../system/os/system.h"            // SystemBankPlay / GetLanguage
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t

#define NEW_GAME_ITEM_NUM   2
#define NEW_GAME_BLACK_TIME 30

static NEW_GAME_CTRL new_game_ctrl;                                     /* sbss 3f4ea0 */

static void NewGameDifficultySelPad(void);
static void NewGameItemDisp(int off_x, int off_y, u_char alpha);
static void NewGameDifficultyDisp(int off_x, int off_y, u_char alpha);
static void NewGameCursorDisp(int off_x, int off_y, u_char alpha);
static void NewGameBlackBgDisp(int off_x, int off_y, u_char alpha);
static u_char NewGameBlackOutAnimCtrl(int timer);

/* The cursor starts on 1 -- Normal, not Easy. */
void NewGameCtrlInit(void)
{
    new_game_ctrl.cursor = '\x01';                                      /* 95 */
    new_game_ctrl.mode = '\0';                                          /* 96 */
    new_game_ctrl.wait_timer = 0;                                       /* 97 */
}

void NewGameMain(void)
{
    if (new_game_ctrl.mode == '\0')                                     /* 112 */
    {
        NewGameDifficultySelPad();                                      /* 114 */
    }
    else if (new_game_ctrl.mode == '\x01')                              /* 116 */
    {
        new_game_ctrl.wait_timer++;                                     /* 117 */

        if (new_game_ctrl.wait_timer > NEW_GAME_BLACK_TIME - 1)         /* 119 */
        {
            if (LoadingTexLoadWait() != 0)                              /* 121 */
            {
                if (SndBufIsPlaying(GetTitleSoundID()) == 0)            /* 123 */
                {
                    if (new_game_ctrl.cursor == '\0')                   /* 125 */
                    {
                        InitCostume();                                  /* 127 */
                        MissionSelTblInit();                            /* 128 */
                        IngameWrkInit(0, 0);                            /* 129 */
                    }
                    else if (new_game_ctrl.cursor == '\x01')            /* 132 */
                    {
                        InitCostume();                                  /* 134 */
                        MissionSelTblInit();                            /* 135 */
                        IngameWrkInit(0, 1);                            /* 136 */
                    }
                    else
                    {
                        PRINT_ASSERT("Error! NewGameMain");             /* 140 */
                    }

                    SetTitleLoadFlg('\0');                              /* 143 */
                    EventDataLoadReq();                                 /* 146 */
                    SetNextGPhase(GID_STORY_LOAD_MISSION);              /* 148 */
                }
            }
        }
    }
}

/* With two items the LEFT and RIGHT bodies are identical, so GCC merged them;
 * the two remaining line markers (164, 170) are what is left of the pair. */
static void NewGameDifficultySelPad(void)
{
    if (((pad[0].rpt & 0x8000U) != 0) ||
        (GetPadAnalogRpt(2) != 0) ||                                    /* 164 */
        ((pad[0].rpt & 0x2000U) != 0) ||
        (GetPadAnalogRpt(3) != 0))                                      /* 170 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 171 */
        new_game_ctrl.cursor = (char)((new_game_ctrl.cursor + 1) %
                                      NEW_GAME_ITEM_NUM);               /* 173 */
    }
    else if (*paddat[0] == 1)                                           /* 176 */
    {
        SetTitleSoundID(SndBankPlay(GetTitleSoundBankID(), 0, 0, 0,
                                    0x3200, 0x1000, 0,
                                    (SND_3D_SET *)0));                  /* 178 */
        new_game_ctrl.mode = '\x01';                                    /* 180 */
        new_game_ctrl.wait_timer = 0;                                   /* 181 */
    }
    else if (*paddat[1] == 1)                                           /* 184 */
    {
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 185 */
        SetNextGPhase(GID_TITLE_MENU);                                  /* 188 */
    }
}

void NewGameDispMain(void)
{
    DispTitleZeroLogo(0, 0, 0x80);                                      /* 215 */

    NewGameItemDisp(0, 0, 0x80);                                        /* 218 */
    NewGameDifficultyDisp(0, 0, 0x80);                                  /* 221 */
    NewGameCursorDisp(0, 0, 0x80);                                      /* 224 */

    TitleCaptionDisp(0, 0, 0x80);                                       /* 227 */

    if (new_game_ctrl.mode == '\x01')                                   /* 229 */
    {
        NewGameBlackBgDisp(0, 0,
                           NewGameBlackOutAnimCtrl(new_game_ctrl.wait_timer)); /* 231 */
    }
}

/* The screen's heading.  Parameters dead, as everywhere else in the title
 * screens. */
static void NewGameItemDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT ds;

    (void)off_x;
    (void)off_y;
    (void)alpha;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 250 */

    CopySprDToSpr(&ds, title_top + 0x10);                               /* 260 */
    ds.alphar = 0x48;                                                   /* 261 */
    DispSprD(&ds);                                                      /* 262 */
}

/* The two difficulty plates -- the unselected one at a flat alpha >> 1, the
 * same convention framerate.c uses for its inactive label. */
static void NewGameDifficultyDisp(int off_x, int off_y, u_char alpha)
{
    static int dif_tex_tbl[NEW_GAME_ITEM_NUM] = { 23, 24 };             /* sdata 3f35b0 */
    DISP_SPRT dif_ds;
    int i;

    PK2SendVram((uintptr_t)GetTitleLogoTexAddr(), -1, -1, 0);           /* 290 */

    for (i = 0; i < NEW_GAME_ITEM_NUM; i++)                             /* 293 */
    {
        CopySprDToSpr(&dif_ds, title_top + dif_tex_tbl[i]);             /* 303 */
        dif_ds.x += (float)off_x;                                       /* 304 */
        dif_ds.y += (float)off_y;                                       /* 305 */
        dif_ds.alphar = 0x48;                                           /* 306 */
        if (i == new_game_ctrl.cursor)                                  /* 307 */
        {
            dif_ds.alpha = (u_char)(((int)dif_ds.alpha * (int)alpha) >> 7);
        }
        else
        {
            dif_ds.alpha = (u_char)(alpha >> 1);
        }
        DispSprD(&dif_ds);                                              /* 310 */
    }                                                                   /* 313 */
}

/* Both arrows sit at y 385, the same row framerate.c uses. */
static void NewGameCursorDisp(int off_x, int off_y, u_char alpha)
{
    (void)off_x;
    (void)off_y;
    (void)alpha;

    DispTitleCursorL(
        (float)newgame_left_x_tbl[new_game_ctrl.cursor][GetLanguage()],
        385.0f, 0x80, GetTitleAnimRGB());                               /* 330 */
    DispTitleCursorR(
        (float)newgame_right_x_tbl[new_game_ctrl.cursor][GetLanguage()],
        385.0f, 0x80, GetTitleAnimRGB());                               /* 331 */
}

/* The record's own alpha of 0x80 is overwritten straight after the copy -- the
 * ROM builds the descriptor with a constant and then substitutes the animated
 * value, rather than putting `alpha` in the initialiser. */
static void NewGameBlackBgDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT black_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };           /* 358 */

    (void)off_x;
    (void)off_y;

    CopySqrDToSqr(&dsq, &black_bg);                                     /* 363 */
    dsq.alpha = alpha;                                                  /* 365 */
    DispSqrD(&dsq);                                                     /* 365 */
}

/* 0 -> 128 over the first 30 frames, then held.  The table's second entry is
 * the all -1 terminator Anim2D_CalcNowAlpha() stops on. */
static u_char NewGameBlackOutAnimCtrl(int timer)
{
    static ALPHA_ANIM_TBL alpha_tbl[2] =                                /* rdata 3c2160 */
    {
        {   0, 128,  0, 30 },
        {  -1,  -1, -1, -1 },
    };
    u_char alpha;

    if (timer < NEW_GAME_BLACK_TIME)                                    /* 385 */
    {
        alpha = Anim2D_CalcNowAlpha(alpha_tbl, timer);                  /* 387 */
    }
    else
    {
        alpha = 0x80;                                                   /* 390 */
    }

    return alpha;                                                       /* 394 */
}
