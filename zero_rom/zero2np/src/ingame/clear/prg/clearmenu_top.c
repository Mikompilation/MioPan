// FILE: /home/zero_rom/zero2np/src/ingame/clear/prg/clearmenu_top.c
//
// The clear menu's top screen: save the game, open the album, or go back to
// the title -- each behind a yes-no confirm window.  It is what the player
// gets after the result page, once the ending is over.
//
// This is savepoint_top.o's twin, and not loosely: the two objects are the
// same source with the names changed.  Every function's $LM span matches
// savepoint_top.c's exactly (+1 up to ClearMenuTopMain, -10 from
// ClearMenuEndReq on), and the whole 11-line difference is in the pad
// handler.  gameover_menu_top.o is the third copy, and its line numbers are
// identical to this file's rather than savepoint's.
//
// The one behavioural difference from savepoint_top.o is exactly that pad
// handler: CROSS here opens the confirm window on *every* row, including
// "Return to Title", where the save point takes "leave" immediately.  That is
// why savepoint's CROSS arm has a switch on csr and this one does not.
//
// It borrows savepoint's entire drawing layer -- SavePoint_MenuWinDisp(),
// SavePointTopCaptionDisp() and SavePoint_MenuConfWinDisp() are
// savepoint_disp.o's own exports, called straight from here -- so the only
// art this file owns is the two-piece title plate in gameclear_tex[48..49].
//
// The two-level step/mode pair is what makes the confirm window work.  `step`
// is the screen (loading / open / decided) and `mode` says which pad handler
// is live while it is open: ClearMenuTopPad() for the row cursor,
// ClearMenuTopConfPad() for the yes-no.  Each is additionally gated on its
// own window having finished animating, so input is dead during both fades.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84),
// clearmenu_top.o.  All six ZERO2.MAP exports plus the ten statics.
//
// Trailing /* NNN */ comments are the original source line numbers, measured
// from the $LM stabs.  Note that GCC 2.96 emits a line note whenever the
// source line changes, *including* part-way through one statement's
// expression -- so a two-line `if (A && B)` or a call whose arguments run
// over several lines leaves two markers, not one.  ClearMenuTopConfPad()'s
// 337/343 pair is the unambiguous case: those four `||` terms cannot be two
// statements, and they carry a marker each.

#include "clearmenu_top.h"

#include "clearmenu.h"                              /* GetClearMenuTexMem     */
#include "../dat/gameclear_dat.h"                   /* gameclear_tex          */

#include "../../savepoint/savepoint_disp.h"         /* SavePoint_MenuWinDisp  */
#include "../../menu/zero2_anim2d.h"                /* Zero2Anim2D_InOut...   */
#include "../../../common/utility2.h"               /* PRINT_ASSERT           */
#include "../../../graphics/graph2d/draw_cmn.h"     /* DrawCmnSelCsr          */
#include "../../../graphics/graph2d/g2d_draw.h"     /* DISP_SPRT / DispSprD   */
#include "../../../graphics/graph2d/message.h"      /* PrintMsg               */
#include "../../../graphics/graph2d/tim2.h"         /* PK2SendVram            */
#include "../../../main/gphase.h"                   /* SetNextGPhase          */
#include "../../../system/eeiop/cddat.h"            /* CLEAR_MENU_PK2         */
#include "../../../system/eeiop/fileload.h"         /* FileLoadIsEnd2         */
#include "../../../system/eeiop/snd3d.h"            /* SND_3D_SET             */
#include "../../../system/os/system.h"              /* GetLanguage / SystemBankPlay */
#include "../../../system/pad/pad.h"                /* pad / paddat           */

/* The message group every clear-menu string comes out of.  Bank 0x24 is
 * shared with game_result_top.o, which draws its unlock lines from ids 6..17
 * while the three rows here are 0..2 and their confirm prompts 11..13. */
#define CLEAR_MENU_MSG_TYPE             0x24

/* Window open / close times, in frames, for both the menu and the confirm. */
#define CLEAR_MENU_TOP_ANIM_IN_TIME     10
#define CLEAR_MENU_TOP_ANIM_OUT_TIME    5

/* The two-piece title plate in gameclear_tex[]. */
#define CLEAR_MENU_TEX_TITLE            48
#define CLEAR_MENU_TEX_TITLE_NUM        2

static void ClearMenuTopCtrlInit(void);
static int  ClearMenuTopTexLoadWait(void);
static void ClearMenuTopPad(void);
static void ClearMenuEndReq(void);
static void ClearMenuTopConfPad(void);
static void ClearMenuTopExeDecision(void);
static void ClearMenuTopDispInit(void);
static void ClearMenuTopTitleDisp(int off_x, int off_y, u_char alpha);
static void ClearMenuTopSelMsgDisp(int off_x, int off_y, u_char alpha);
static void ClearMenuTopCsrDisp(int off_x, int off_y, u_char alpha);

static void               *clear_menu_tex_addr;         /* sdata 3ef898 */
static char                clear_menu_top_init_flg;     /* sdata 3ef89c */
static CLEAR_MENU_TOP_CTRL clear_menu_top_ctrl;         /* sbss  3f4b00 */
static CLEAR_MENU_TOP_DISP clear_menu_top_disp;         /* sbss  3f4b08 */

/* ==========================================================================
 *  Set-up
 * ======================================================================== */

/* Once per visit, from init_ClearMenu().  Steps straight past the display
 * reset to step 1 and anim_step 2, i.e. "pak already requested, window
 * already open" -- the pak was requested in the same breath and the menu is
 * meant to be there the moment the parent's fade clears, not to animate in
 * behind it. */
void ClearMenuTopFirstInit(void)                                        /* 127 */
{
    ClearMenuTopCtrlInit();                                             /* 131 */
    ClearMenuTopDispInit();                                             /* 134 */

    clear_menu_top_ctrl.step     = CLEAR_MENU_TOP_STEP_LOAD_WAIT;       /* 136 */
    clear_menu_top_ctrl.csr      = CLEAR_MENU_TOP_CSR_SAVE;             /* 137 */

    clear_menu_top_disp.anim_step = ZERO2_ANIM2D_STEP_SHOW;             /* 139 */

    clear_menu_top_init_flg      = 0;                                   /* 141 */
}

/* Once per entry to the phase.  The first entry is the one FirstInit() just
 * set up, so it only claims the flag; every later entry -- coming back from
 * the save screen or the album -- resets the control block, which sends the
 * screen back through its open animation. */
void ClearMenuTopInit(void)                                             /* 149 */
{
    if (clear_menu_top_init_flg != 0) {                                 /* 152 */
        ClearMenuTopCtrlInit();                                         /* 154 */
    }
    else {
        clear_menu_top_init_flg = 1;                                    /* 157 */
    }
}

/* csr is deliberately not reset -- coming back from the album should leave
 * the cursor where the player left it. */
static void ClearMenuTopCtrlInit(void)                                  /* 166 */
{
    clear_menu_top_ctrl.step     = CLEAR_MENU_TOP_STEP_INIT;            /* 169 */
    clear_menu_top_ctrl.mode     = CLEAR_MENU_TOP_MODE_MENU;            /* 170 */
    clear_menu_top_ctrl.conf_csr = 1;                   /* default "no" */ /* 171 */
}

/* ==========================================================================
 *  Text pak
 *
 *  CLEAR_MENU_PK2's per-language variants sit immediately after it in the
 *  file table, so the language index is simply added to the base file number.
 *  It is recomputed at all three use sites and never clamped -- the same
 *  shape title.o's logo pak has.
 * ======================================================================== */

void ClearMenuTopBackGroundLoadReq(void)                                /* 179 */
{
    if (clear_menu_tex_addr != nullptr) {                               /* 181 */
        LiberateClearMenuTexMem(&clear_menu_tex_addr);                  /* 182 */
    }

    GetClearMenuTexMem(&clear_menu_tex_addr,
                       CLEAR_MENU_PK2 + (char)GetLanguage());           /* 187 */
    ClearMenuTexLoadReq(clear_menu_tex_addr,
                        CLEAR_MENU_PK2 + (char)GetLanguage());          /* 190 */
}

static int ClearMenuTopTexLoadWait(void)                                /* 200 */
{
    if (FileLoadIsEnd2(CLEAR_MENU_PK2 + (char)GetLanguage(),
                       clear_menu_tex_addr) != 0) {                     /* 208 */
        return 1;
    }

    return 0;                                                           /* 213 */
}

/* ==========================================================================
 *  Per-frame control
 * ======================================================================== */

/* Both pad handlers are gated on their own window being fully open, so input
 * is dead through every fade.  Line 250 is a zero-length marker -- the mode
 * switch's `break` contributed no instructions of its own, so its $LM sits on
 * the same address as case 3's first test. */
void ClearMenuTopMain(void)                                             /* 223 */
{
    switch (clear_menu_top_ctrl.step) {                                 /* 226 */
    case CLEAR_MENU_TOP_STEP_INIT:
        ClearMenuTopDispInit();                                         /* 229 */

        clear_menu_top_ctrl.step = CLEAR_MENU_TOP_STEP_LOAD_WAIT;       /* 232 */
        break;

    case CLEAR_MENU_TOP_STEP_LOAD_WAIT:
        if (ClearMenuTopTexLoadWait() != 0) {                           /* 234 */
            clear_menu_top_ctrl.step = CLEAR_MENU_TOP_STEP_OPEN;        /* 237 */
        }
        break;

    case CLEAR_MENU_TOP_STEP_OPEN:
        if (clear_menu_top_disp.anim_step == ZERO2_ANIM2D_STEP_SHOW) {  /* 239 */
            switch (clear_menu_top_ctrl.mode) {                         /* 240 */
            case CLEAR_MENU_TOP_MODE_MENU:
                /* The confirm window is closed, so the rows take input. */
                if (clear_menu_top_disp.conf_anim_step ==
                        ZERO2_ANIM2D_STEP_END) {                        /* 242 */
                    ClearMenuTopPad();                                  /* 243 */
                }
                break;

            case CLEAR_MENU_TOP_MODE_CONF:
                if (clear_menu_top_disp.conf_anim_step ==
                        ZERO2_ANIM2D_STEP_SHOW) {                       /* 247 */
                    ClearMenuTopConfPad();                              /* 248 */
                }
                break;                                                  /* 250 */

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 252 */
                break;
            }
        }
        break;

    case CLEAR_MENU_TOP_STEP_DECIDED:
        /* Wait for the menu window to finish closing, then act. */
        if (clear_menu_top_disp.anim_step == ZERO2_ANIM2D_STEP_END) {   /* 257 */
            switch (clear_menu_top_ctrl.csr) {                          /* 258 */
            case CLEAR_MENU_TOP_CSR_SAVE:
                SetNextGPhase(GID_CLEARMENU_SAVE);                      /* 260 */
                break;

            case CLEAR_MENU_TOP_CSR_ALBUM:
                SetNextGPhase(GID_CLEARMENU_ALBUM);                     /* 263 */
                break;

            case CLEAR_MENU_TOP_CSR_EXIT:
                /* Unreachable in practice: the only way to reach step 3 with
                 * csr == EXIT is through ClearMenuTopExeDecision(), which
                 * gets here by calling ClearMenuEndReq() -- and that never
                 * closes the menu window, so anim_step stays at SHOW and this
                 * arm is never tested.  gameover_menu_top.o leaves the same
                 * case empty; this copy calls its EndReq a second time. */
                ClearMenuEndReq();                                      /* 266 */
                break;

            default:
                PRINT_ASSERT("Error! %s", __FUNCTION__);                /* 269 */
                break;
            }
        }
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 274 */
        break;
    }
}

/* UP/DOWN walk the three rows; CROSS opens the confirm window on whichever is
 * selected, and TRIANGLE jumps the cursor to the exit row without confirming
 * anything.  csr + 2 rather than csr - 1 so the modulo stays positive.
 *
 * This is the whole of the difference from savepoint_top.o: there, CROSS
 * switches on csr and sends "leave" straight to SavePointEndReq(); here every
 * row goes through the confirm window. */
static void ClearMenuTopPad(void)                                       /* 283 */
{
    if ((pad[0].rpt & 0x1000) || GetPadAnalogRpt(0)) {                  /* 287 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 288 */

        clear_menu_top_ctrl.csr =
            (char)((clear_menu_top_ctrl.csr + 2) % CLEAR_MENU_TOP_CSR_NUM); /* 290 */
    }
    else if ((pad[0].rpt & 0x4000) || GetPadAnalogRpt(1)) {             /* 293 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 294 */

        clear_menu_top_ctrl.csr =
            (char)((clear_menu_top_ctrl.csr + 1) % CLEAR_MENU_TOP_CSR_NUM); /* 296 */
    }
    else if (*paddat[0] == 1) {                                         /* 299 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 300 */

        clear_menu_top_ctrl.mode            = CLEAR_MENU_TOP_MODE_CONF; /* 302 */
        clear_menu_top_ctrl.conf_csr        = 1;                        /* 303 */
        clear_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_START;  /* 304 */
        clear_menu_top_disp.conf_anim_timer = 0;                        /* 305 */
    }
    else if (*paddat[1] == 1) {                                         /* 308 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 309 */

        clear_menu_top_ctrl.csr = CLEAR_MENU_TOP_CSR_EXIT;              /* 311 */
    }
}

/* The player is leaving.  step 3 stops the menu taking input while the
 * parent's fade runs; the parent is what actually changes phase. */
static void ClearMenuEndReq(void)                                       /* 320 */
{
    clear_menu_top_ctrl.step = CLEAR_MENU_TOP_STEP_DECIDED;             /* 323 */

    ClearMenuFadeOutReq();                                              /* 325 */
}

/* LEFT and RIGHT both just flip the two-way cursor, hence the xor.  CROSS on
 * "no" and TRIANGLE do the same thing -- close the window and hand the rows
 * back -- and the ROM writes that tail out twice rather than sharing it.  GCC
 * cross-jumped the two copies into the second one's code, so the first copy
 * keeps $LMs only for 359 and 360; 358 above and 361 below are inferred from
 * the shape of the twin at 367..369. */
static void ClearMenuTopConfPad(void)                                   /* 333 */
{
    if ((pad[0].rpt & 0x8000) || GetPadAnalogRpt(2) ||                  /* 337 */
        (pad[0].rpt & 0x2000) || GetPadAnalogRpt(3)) {                  /* 343 */
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 344 */

        clear_menu_top_ctrl.conf_csr =
            (char)(clear_menu_top_ctrl.conf_csr ^ 1);                   /* 346 */
    }
    else if (*paddat[0] == 1) {                                         /* 349 */
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 350 */

        if (clear_menu_top_ctrl.conf_csr == 0) {                        /* 353 */
            ClearMenuTopExeDecision();                                  /* 354 */
        }
        else {
            clear_menu_top_ctrl.mode            = CLEAR_MENU_TOP_MODE_MENU; /* 358 */
            clear_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT; /* 359 */
            clear_menu_top_disp.conf_anim_timer = 0;                    /* 360 */
        }
    }
    else if (*paddat[1] == 1) {                                         /* 364 */
        SystemBankPlay(1, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000); /* 365 */

        clear_menu_top_ctrl.mode            = CLEAR_MENU_TOP_MODE_MENU; /* 367 */
        clear_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT;    /* 368 */
        clear_menu_top_disp.conf_anim_timer = 0;                        /* 369 */
    }
}

/* "Yes" on the confirm window.  Save and Album close the menu and let step 3
 * dispatch the phase; Exit goes through ClearMenuEndReq() instead, which
 * closes the whole screen rather than just this window. */
static void ClearMenuTopExeDecision(void)                               /* 378 */
{
    switch (clear_menu_top_ctrl.csr) {                                  /* 381 */
    case CLEAR_MENU_TOP_CSR_SAVE:
    case CLEAR_MENU_TOP_CSR_ALBUM:
        clear_menu_top_ctrl.step = CLEAR_MENU_TOP_STEP_DECIDED;         /* 384 */

        clear_menu_top_disp.anim_step       = ZERO2_ANIM2D_STEP_OUT;    /* 386 */
        clear_menu_top_disp.anim_timer      = 0;                        /* 387 */
        clear_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_OUT;    /* 388 */
        clear_menu_top_disp.conf_anim_timer = 0;                        /* 389 */
        break;                                                          /* 390 */

    case CLEAR_MENU_TOP_CSR_EXIT:
        ClearMenuEndReq();                                              /* 392 */
        break;

    default:
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 395 */
        break;
    }
}

void ClearMenuTopMemFree(void)                                          /* 408 */
{
    LiberateClearMenuTexMem(&clear_menu_tex_addr);                      /* 412 */
}

/* ==========================================================================
 *  Per-frame draw
 * ======================================================================== */

/* conf_anim_step starts at END (closed) rather than START, so the confirm
 * window is not drawn until the player asks for it. */
static void ClearMenuTopDispInit(void)                                  /* 424 */
{
    clear_menu_top_disp.anim_step       = ZERO2_ANIM2D_STEP_START;      /* 427 */
    clear_menu_top_disp.anim_timer      = 0;                            /* 428 */
    clear_menu_top_disp.conf_anim_step  = ZERO2_ANIM2D_STEP_END;        /* 429 */
    clear_menu_top_disp.conf_anim_timer = 0;                            /* 430 */
}

/* PK2SendVram() re-uploads this screen's own text pak every frame, after the
 * shared window art has been drawn -- the two paks share VRAM, so the order
 * matters. */
void ClearMenuTopDisp(void)                                             /* 438 */
{
    u_char alpha;

    /* The confirm question differs per row: 11 save, 12 album, 13 exit. */
    static const int msg_id_tbl[CLEAR_MENU_TOP_CSR_NUM] =   /* rdata 3a3598 */
    {
        11,     /* Save game data?          */
        12,     /* Enter Album Mode?        */
        13,     /* Return to Title Screen?  */
    };

    if (clear_menu_top_ctrl.step >= CLEAR_MENU_TOP_STEP_OPEN &&         /* 452 */
        clear_menu_top_disp.anim_step != ZERO2_ANIM2D_STEP_END) {       /* 453 */
        alpha = Zero2Anim2D_InOutAnimCtrl(&clear_menu_top_disp.anim_step,
                                          &clear_menu_top_disp.anim_timer,
                                          CLEAR_MENU_TOP_ANIM_IN_TIME,
                                          CLEAR_MENU_TOP_ANIM_OUT_TIME); /* 455 */

        SavePoint_MenuWinDisp(0, 0, alpha);                             /* 458 */

        PK2SendVram((uintptr_t)clear_menu_tex_addr, -1, -1, 0);         /* 460 */

        ClearMenuTopTitleDisp(0, 0, alpha);                             /* 463 */
        SavePointTopCaptionDisp(0, 0, alpha);                           /* 466 */
        ClearMenuTopSelMsgDisp(0, 0, alpha);                            /* 469 */
        ClearMenuTopCsrDisp(0, 0, alpha);                               /* 472 */

        if (clear_menu_top_disp.conf_anim_step != ZERO2_ANIM2D_STEP_END) { /* 474 */
            alpha = Zero2Anim2D_InOutAnimCtrl(&clear_menu_top_disp.conf_anim_step,
                                              &clear_menu_top_disp.conf_anim_timer,
                                              CLEAR_MENU_TOP_ANIM_IN_TIME,
                                              CLEAR_MENU_TOP_ANIM_OUT_TIME); /* 476 */

            SavePoint_MenuConfWinDisp((int)clear_menu_top_ctrl.conf_csr,
                                      0, 0, alpha);                     /* 479 */

            PrintMsg(CLEAR_MENU_MSG_TYPE, msg_id_tbl[clear_menu_top_ctrl.csr],
                     68, 309, 1, (int)alpha, 0);                        /* 483 */
        }
    }
}

/* The two-piece screen title. */
static void ClearMenuTopTitleDisp(int off_x, int off_y, u_char alpha)   /* 498 */
{
    DISP_SPRT title_ds;
    int       i;

    for (i = 0; i < CLEAR_MENU_TEX_TITLE_NUM; i++) {                    /* 504 */
        CopySprDToSpr(&title_ds, &gameclear_tex[CLEAR_MENU_TEX_TITLE + i]); /* 505 */

        title_ds.x = title_ds.x + (float)off_x;                         /* 506 */
        title_ds.y = title_ds.y + (float)off_y;                         /* 506 */

        title_ds.alpha = (u_char)(((int)title_ds.alpha * (int)alpha) >> 7); /* 507 */

        DispSprD(&title_ds);                                            /* 508 */
    }                                                                   /* 509 */
}

/* The three rows, centre-arranged on x = 320.  The selected one is drawn in
 * colour 7 and the rest in 1.  Like savepoint_top.o -- and unlike
 * gameover_menu_top.o -- the y values come out of a table rather than being
 * computed, which is why this object's .rodata holds two int[3] where
 * gameover's holds one. */
static void ClearMenuTopSelMsgDisp(int off_x, int off_y, u_char alpha)  /* 520 */
{
    int i;

    static const int msg_y_tbl[CLEAR_MENU_TOP_CSR_NUM] =    /* rdata 3a35a8 */
    {
        146, 176, 206,
    };

    static const int msg_id_tbl[CLEAR_MENU_TOP_CSR_NUM] =   /* rdata 3a35b8 */
    {
        0,      /* Save Game       */
        1,      /* Album           */
        2,      /* Return to Title */
    };

    for (i = 0; i < CLEAR_MENU_TOP_CSR_NUM; i++) {                      /* 535 */
        /* One statement over several lines: the row compare is on 536 and
         * the tail of the argument list on 544.  No local for the colour
         * appears in the stabs. */
        PrintMsg_Arrange(CLEAR_MENU_MSG_TYPE, msg_id_tbl[i],
                         off_x + 320, msg_y_tbl[i] + off_y,
                         (clear_menu_top_ctrl.csr == i) ? 7 : 1,        /* 536 */
                         (int)alpha, 0, 0, 0, 2);                       /* 544 */
    }                                                                   /* 545 */
}

/* The row highlight, 272 wide, stepping 30 pixels per row from y = 142 --
 * four pixels above the message baselines in msg_y_tbl[]. */
static void ClearMenuTopCsrDisp(int off_x, int off_y, u_char alpha)     /* 556 */
{
    (void)off_x;                /* both offsets are dead in the ROM */
    (void)off_y;

    DrawCmnSelCsr(0, 184.0f,
                  (float)(clear_menu_top_ctrl.csr * 30 + 142),
                  alpha, 272.0f, 1);                                    /* 561 */
}
