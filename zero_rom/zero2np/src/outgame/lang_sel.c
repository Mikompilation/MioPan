// FILE: /home/zero_rom/zero2np/src/outgame/lang_sel.c
//
// The language-select screen: five flags in a column, the selected one at full
// brightness and the rest at 0x40, over the title's own scrolling background.
//
// It is the first screen with a heap, so init_LangSel_Main() is where the
// outgame heap is created; everything after this point claims out of it.
//
// The bottom half of the file is the language *setting* rather than the
// screen: LoadLangSetUp() commits the choice, LangData_LoadReq() pulls the
// per-language message and font paks, and SetSave_Language() hands the byte to
// the memory-card save builder.  lang_check.c calls three of those directly.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "lang_sel.h"

#include "title.h"                          // GetTitleTexMem / TitleTexLoadReq / Liberate
#include "title_disp.h"                     // DispTitleBack
#include "tim_dat/lang_sl_dat.h"            // lang_sl_tex[]
#include "../common/ol_load.h"              // ol_loadHeapReset
#include "../common/utility2.h"             // PRINT_ASSERT
#include "../common/variable.h"             // pad[]
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/tim2.h"       // PK2SendVram
#include "../main/gphase.h"                 // SetNextGPhase / GID_AUTOLOAD_MAIN
#include "../main/main.h"                   // SoftResetLock / SoftResetUnlock
#include "../main/phasefunc.h"              // GPHASE_ENUM + phase-callback prototypes
#include "../system/eeiop/cddat.h"          // TITLE_BG_PK2
#include "../system/eeiop/fileload.h"       // FileLoadIsEnd2
#include "../system/os/eecdvd.h"            // LoadReq / IsLoadEndAll
#include "../system/os/system.h"            // GetLanguage / SetLanguage / SystemBankPlay
#include "../system/pad/pad.h"              // paddat / GetPadAnalogRpt

#include <stdint.h>                         // uintptr_t

#define LANG_NUM            5

/* The per-language flag/name pak.  0x1162 == 4450. */
#define LANGSEL_TEX_PK2     0x1162

/* The outgame heap.  Created here and used by every outgame screen after. */
#define OL_LOAD_HEAP_ADDR   ((void *)0x5a6c00)
#define OL_LOAD_HEAP_SIZE   0x7a8000

/* Where the two language data files land.  Fixed addresses, not heap. */
#define LANG_MSG_ADDR       0x1e79b00
#define LANG_FONT_ADDR      0xd9ec00

/* LANG_SEL_CTRL::step */
#define LS_MEM_GET      0
#define LS_LOAD_REQ     1
#define LS_LOAD_WAIT    2
#define LS_SELECT       3
#define LS_DATA_REQ     4
#define LS_DATA_WAIT    5

static void *lang_sel_bg_addr;                                          /* sdata 3f1800 */
static void *lang_sel_tex_addr;                                         /* sdata 3f1804 */
static u_char set_language;                                             /* sdata 3f1808 */
static LANG_SEL_CTRL lang_sel_ctrl;                                     /* sbss  3f4d50 */

static void LangSelCtrlInit(void);
static int  LangSelTexLoadWait(void);
static void LangSelMain(void);
static void LangSelMainPad(void);
static void LangSelDispMain(void);
static void LangSelBlackBgDisp(int off_x, int off_y, u_char alpha);
static void LangSelNationalFlagDisp(int off_x, int off_y, u_char alpha);
static void LangSelCsrFlareDisp(int off_x, int off_y, u_char alpha);
static void LangSelCountryNameDisp(int off_x, int off_y, u_char alpha);

/* The cursor starts on whatever the disc's default language already is. */
static void LangSelCtrlInit(void)
{
    lang_sel_ctrl.anim_timer = 0;                                       /* 109 */
    lang_sel_ctrl.step = LS_MEM_GET;                                    /* 112 */
    lang_sel_ctrl.csr = GetLanguage();                                  /* 115 */
}

static int LangSelTexLoadWait(void)
{
    int res;

    if (FileLoadIsEnd2(TITLE_BG_PK2, lang_sel_bg_addr) == 0)            /* 133 */
    {
        res = 0;
    }
    else
    {
        res = (FileLoadIsEnd2(LANGSEL_TEX_PK2 + GetLanguage(),
                              lang_sel_tex_addr) != 0);                 /* 134 */
    }

    return res;                                                         /* 140 */
}

/* Steps 4 and 5 are the hand-off: the screen's own textures go back before the
 * language data is pulled, because the two would not both fit. */
static void LangSelMain(void)
{
    switch (lang_sel_ctrl.step)                                         /* 150 */
    {
    case LS_MEM_GET:
        if (lang_sel_bg_addr != (void *)0)                              /* 153 */
        {
            LiberateTitleTexMem(&lang_sel_bg_addr);
        }
        if (lang_sel_tex_addr != (void *)0)                             /* 156 */
        {
            LiberateTitleTexMem(&lang_sel_tex_addr);                    /* 157 */
        }

        GetTitleTexMem(&lang_sel_tex_addr,
                       LANGSEL_TEX_PK2 + GetLanguage());                /* 159 */
        GetTitleTexMem(&lang_sel_bg_addr, TITLE_BG_PK2);                /* 160 */

        lang_sel_ctrl.step = LS_LOAD_REQ;                               /* 164 */
        break;                                                          /* 165 */

    case LS_LOAD_REQ:
        TitleTexLoadReq(lang_sel_tex_addr,
                        LANGSEL_TEX_PK2 + GetLanguage());               /* 167 */
        TitleTexLoadReq(lang_sel_bg_addr, TITLE_BG_PK2);                /* 168 */

        lang_sel_ctrl.step = LS_LOAD_WAIT;                              /* 171 */
        break;                                                          /* 172 */

    case LS_LOAD_WAIT:
        if (LangSelTexLoadWait() != 0)                                  /* 174 */
        {
            lang_sel_ctrl.step = LS_SELECT;                             /* 175 */
        }
        break;                                                          /* 177 */

    case LS_SELECT:
        LangSelMainPad();                                               /* 180 */
        break;

    case LS_DATA_REQ:
        if (lang_sel_bg_addr != (void *)0)                              /* 183 */
        {
            LiberateTitleTexMem(&lang_sel_bg_addr);
        }
        if (lang_sel_tex_addr != (void *)0)                             /* 187 */
        {
            LiberateTitleTexMem(&lang_sel_tex_addr);                    /* 188 */
        }

        LangData_LoadReq();                                             /* 190 */
        lang_sel_ctrl.step = LS_DATA_WAIT;                              /* 191 */
        /* fall through */

    case LS_DATA_WAIT:
        if (LangData_LoadWait() != 0)                                   /* 194 */
        {
            SetNextGPhase(GID_AUTOLOAD_MAIN);                           /* 196 */
        }
        break;                                                          /* 199 */

    default:
        /* The only PRINT_ASSERT in the folder that passes __FUNCTION__ as an
         * argument rather than spelling the name out. */
        PRINT_ASSERT("Error! %s", "LangSelMain");                       /* 204 */
        break;
    }
}

/* Vertical list, so UP (0x1000) / DOWN (0x4000) and analog 0 / 1. */
static void LangSelMainPad(void)
{
    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))     /* 217 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 218 */
        lang_sel_ctrl.csr = (char)((lang_sel_ctrl.csr + LANG_NUM - 1) %
                                   LANG_NUM);                           /* 219 */
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))/* 222 */
    {
        SystemBankPlay(0, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 223 */
        lang_sel_ctrl.csr = (char)((lang_sel_ctrl.csr + 1) % LANG_NUM); /* 224 */
    }
    else if (*paddat[0] == 1)                                           /* 227 */
    {
        SystemBankPlay(3, 1, 0, 0, (SND_3D_SET *)0, 0x3200, 0x1000);    /* 228 */

        set_language = lang_sel_ctrl.csr;                               /* 230 */
        LoadLangSetUp();                                                /* 233 */

        lang_sel_ctrl.step = LS_DATA_REQ;                               /* 235 */
    }
}

static void LangSelDispMain(void)
{
    if (lang_sel_ctrl.step == LS_SELECT)                                /* 255 */
    {
        DispTitleBack(&lang_sel_ctrl.anim_timer, lang_sel_bg_addr);     /* 257 */
        LangSelBlackBgDisp(0, 0, 0x80);                                 /* 259 */

        PK2SendVram((uintptr_t)lang_sel_tex_addr, -1, -1, 0);           /* 261 */

        LangSelNationalFlagDisp(0, 0, 0x80);                            /* 264 */
        LangSelCsrFlareDisp(0, 0, 0x80);                                /* 267 */
        LangSelCountryNameDisp(0, 0, 0x80);                             /* 270 */
    }
}

/* A half-strength black wash so the flags read over the moving background.
 * The alpha parameter is dead -- the record's own 0x40 is what is used. */
static void LangSelBlackBgDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT lang_sel_bg = { 640, 448, 0, 0, 0, 0, 0, 0, 0x40 };        /* 286 */

    (void)off_x;
    (void)off_y;
    (void)alpha;

    CopySqrDToSqr(&dsq, &lang_sel_bg);                                  /* 291 */
    DispSqrD(&dsq);                                                     /* 292 */
}

/* Unselected entries are tinted to 0x40 rather than faded, which keeps the
 * flag colours readable while still marking the choice. */
static void LangSelNationalFlagDisp(int off_x, int off_y, u_char alpha)
{
    static int tex_label_tbl[LANG_NUM] = { 0, 1, 2, 3, 4 };             /* rdata 3ba3f0 */
    DISP_SPRT ds;
    int i;

    for (i = 0; i < LANG_NUM; i++)                                      /* 317 */
    {
        CopySprDToSpr(&ds, lang_sl_tex + tex_label_tbl[i]);             /* 318 */
        ds.x += (float)off_x;                                           /* 319 */
        ds.y += (float)off_y;                                           /* 320 */
        ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);         /* 322 */

        if (lang_sel_ctrl.csr != i)                                     /* 323 */
        {
            ds.r = ds.g = ds.b = 0x40;
        }

        DispSprD(&ds);                                                  /* 326 */
    }                                                                   /* 327 */
}

/* Only the selected row gets a flare; French needs two plates, everything else
 * one, hence the -1 terminator column. */
static void LangSelCsrFlareDisp(int off_x, int off_y, u_char alpha)
{
    static int flare_tex_tbl[LANG_NUM][2] =                             /* rdata 3ba408 */
    {
        { 11, -1 },
        { 12, 13 },
        { 14, -1 },
        { 15, -1 },
        { 16, -1 },
    };
    DISP_SPRT ds;
    int i;

    for (i = 0; i < 2; i++)                                             /* 353 */
    {
        if (flare_tex_tbl[lang_sel_ctrl.csr][i] != -1)                  /* 354 */
        {
            CopySprDToSpr(&ds,
                          lang_sl_tex + flare_tex_tbl[lang_sel_ctrl.csr][i]); /* 355 */
            ds.x += (float)off_x;                                       /* 356 */
            ds.y += (float)off_y;                                       /* 357 */
            ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);     /* 358 */
            DispSprD(&ds);                                              /* 360 */
        }
    }
}

/* All five names, tinted the same way as the flags. */
static void LangSelCountryNameDisp(int off_x, int off_y, u_char alpha)
{
    static int tex_label_tbl[LANG_NUM][2] =                             /* rdata 3ba430 */
    {
        { 5, -1 },
        { 6,  7 },
        { 8, -1 },
        { 9, -1 },
        { 10, -1 },
    };
    DISP_SPRT ds;
    int i;
    int j;

    for (i = 0; i < LANG_NUM; i++)
    {
        for (j = 0; j < 2; j++)
        {
            if (tex_label_tbl[i][j] != -1)
            {
                CopySprDToSpr(&ds, lang_sl_tex + tex_label_tbl[i][j]);
                ds.x += (float)off_x;
                ds.y += (float)off_y;
                ds.alpha = (u_char)(((int)ds.alpha * (int)alpha) >> 7);

                if (lang_sel_ctrl.csr != i)
                {
                    ds.r = ds.g = ds.b = 0x40;
                }

                DispSprD(&ds);
            }
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// The language setting itself.  lang_check.c calls all three of the first
// group directly; SetSave_Language() is wired into the memory-card save
// builder's descriptor list.

void LoadLangSetUp(void)
{
    SetLanguage(set_language);                                          /* 420 */
}

/* Both files land at fixed addresses rather than on the outgame heap -- they
 * have to survive every screen for the rest of the run. */
void LangData_LoadReq(void)
{
    LoadReq(GetLanguage() + VRAM_TEX_PK2, LANG_MSG_ADDR);                          /* 431 */
    LoadReq(GetLanguage() + MSG_OBJ, LANG_FONT_ADDR);                     /* 432 */
}

int LangData_LoadWait(void)
{
    int res;

    res = IsLoadEndAll();                                               /* 447 */
    if (res != 0)
    {
        PK2SendVram(LANG_MSG_ADDR, -1, -1, 0);                          /* 448 */
    }

    return (res != 0);                                                  /* 454 */
}

void Set_McSaveLanguage(u_char language)
{
    set_language = language;                                            /* 468 */
}

void SetSave_Language(MC_SAVE_DATA *data)
{
    data->addr = &set_language;                                         /* 480 */
    data->size = 1;                                                     /* 481 */
}

// ──────────────────────────────────────────────────────────────────────
// GPhase per-phase callbacks, invoked via the main/gphase.c dispatch tables.

void init_LangSel_Main(void)
{
    SoftResetLock();                                                    /* 491 */

    /* The outgame heap is born here. */
    ol_loadHeapReset(OL_LOAD_HEAP_ADDR, OL_LOAD_HEAP_SIZE);             /* 494 */

    LangSelCtrlInit();                                                  /* 497 */
}

GPHASE_ENUM one_LangSel_Main(GPHASE_ENUM dummy)
{
    (void)dummy;

    LangSelMain();                                                      /* 502 */
    LangSelDispMain();                                                  /* 504 */

    return GPHASE_CONTINUE;                                             /* 506 */
}

void end_LangSel_Main(void)
{
    SoftResetUnlock();                                                  /* 511 */
}
