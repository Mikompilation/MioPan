// FILE: /home/zero_rom/zero2np/src/debug/debug.c
//
// Top-level debug phase/menu glue, plus EachDebugMain() -- the always-on debug
// overlay main.c runs every frame regardless of phase.  The memory meters the
// MEMORY_DISP submenu switches on are drawn from there.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "zero2_debug.h"

#include "debug_menu.h"
#include "scn_test.h"
#include "screen_calib.h"
#include "test2d.h"
#include "motion_viewer.h"

#include "../common/heapctrl.h"                  // heapCtrl* / HEAPMEM_LEAVE_SIZE
#include "../common/mem_util.h"                  // mem_util*
#include "../common/ol_load.h"                   // ol_load*
#include "../common/variable.h"                  // debug_wrk / key_now
#include "../graphics/graph2d/g2d_draw.h"        // DISP_SQAR / SQAR_DAT / DispSqrD
#include "../graphics/graph2d/message.h"         // SetASCIIString2
#include "../graphics/graphics.h"                // SetLine2DPacket
#include "../graphics/draw_env.h"                // SetDrawEnvNoTex / DRAW_ENV_NOTEX
#include "../ingame/item/prg/item.h"             // ItemGet
#include "../ingame/menu/play_data.h"            // PlayData_ScoreCount
#include "../ingame/photo/m_plyr_camera.h"       // m_plyr_camera
#include "../main/gphase.h"                      // SetNextGPhase / GID_*
#include "../main/phasefunc.h"                   // GPHASE_ENUM + phase callbacks
#include "../system/os/system.h"                 // GetSystemHeapWrkP
#include "../system/pad/pad.h"                   // paddat

#include <stdio.h>

typedef struct
{
    char *str;
    int (*func)(void);
    void (*ini_func)(void);
} DEBUG_MENU_DATA;

static int SceneTest(void);
static int ScreenCalib(void);
static int Test2d(void);
static int MotionViewer(void);

static char dbg_menu_scene_test[] = "Scene Test";
static char dbg_menu_screen_calib[] = "Screen Calib";
static char dbg_menu_test2d[] = "Test 2D";
static char dbg_menu_motion_viewer[] = "Motion Viewer";
static char dbg_menu_return[] = "Return";
static char dbg_menu_end[] = "";
static char dbg_menu_cursor[] = "o";

static char s_dbg_max_free[] = "MaxFreeSize 0X%x %d KB";
static char s_dbg_total_free[] = "TotalFreeSize 0X%x %d KB";

int dbg_spu_mem_disp;                                                 /* sdata 3ef970 */
int dbg_system_mem_disp;                                                /* sdata 3ef974 */
int dbg_mdl_mem_disp;                                                   /* sdata 3ef978 */
int dbg_cmn_mem_disp;                                                   /* sdata 3ef97c */
int dbg_iop_mem_disp;                                                   /* sdata 3ef980 */
int dbg_ene_no;                                                         /* sdata 3ef984 */
static int dbg_enemy_button_pre;                                        /* sdata 3ef988 */
int dbg_enemy_button;                                                   /* sdata 3ef98c */

/* The ROM's table is three entries -- Scene Test, Return, terminator -- which
 * is why it occupies only 0x24 of the 0x40 between 0x2d9c70 and s_ivBGColor at
 * 0x2d9ca0.  Screen Calib, Test 2D and Motion Viewer are port additions ahead of
 * Return; both DebugMenu() and DebugMenuDataCount() walk to the empty-string
 * terminator, so growing the table needs no other change.  Same reasoning as
 * debug_menu.c's EVENT DEBUG / CAMERA MENU rows. */
static DEBUG_MENU_DATA dbg_menu_data[6] =                               /* data 2d9c70 */
{
    { dbg_menu_scene_test,   SceneTest,   (void (*)(void))0 },
    { dbg_menu_screen_calib, ScreenCalib, (void (*)(void))0 },
    { dbg_menu_test2d,       Test2d,      (void (*)(void))0 },
    { dbg_menu_motion_viewer, MotionViewer, (void (*)(void))0 },
    { dbg_menu_return,       DebugEnd,    (void (*)(void))0 },
    { dbg_menu_end,          (int (*)(void))0, (void (*)(void))0 },
};

static int SceneTest(void)
{
    int ret;

    if (debug_wrk.init_scntest == 0)
    {
        debug_wrk.init_scntest = 1;
        SceneTestInit();
    }

    ret = SceneTestMain();
    if (ret != 0)
    {
        debug_wrk.init_scntest = 0;
    }

    return ret != 0;
}

/* PORT ADDITION.  debug_wrk.init_screen_calib is the ROM's own field (it is in
 * DEBUG_WRK and InitDebug() clears it) but the prototype ships no code behind
 * it; this is the same one-shot-init wrapper SceneTest() above has. */
static int ScreenCalib(void)
{
    int ret;

    if (debug_wrk.init_screen_calib == 0)
    {
        debug_wrk.init_screen_calib = 1;
        ScreenCalibInit();
    }

    ret = ScreenCalibMain();
    if (ret != 0)
    {
        debug_wrk.init_screen_calib = 0;
    }

    return ret != 0;
}

/* PORT ADDITION, same standing as ScreenCalib() above: debug_wrk.init_test2d
 * is the ROM's own field, with no ROM code behind it. */
static int Test2d(void)
{
    int ret;

    if (debug_wrk.init_test2d == 0)
    {
        debug_wrk.init_test2d = 1;
        Test2dInit();
    }

    ret = Test2dMain();
    if (ret != 0)
    {
        debug_wrk.init_test2d = 0;
    }

    return ret != 0;
}

/* PORT ADDITION, same standing again: debug_wrk.init_motionviewer is the ROM's
 * own field with no ROM code behind it.  Unlike the two above, the tool this
 * one runs claims real resources -- two mmanage slots and a block of the load
 * heap -- and MotionViewerMain() gives them all back on the frame it reports
 * the exit, so there is nothing to unwind here. */
static int MotionViewer(void)
{
    int ret;

    if (debug_wrk.init_motionviewer == 0)
    {
        debug_wrk.init_motionviewer = 1;
        MotionViewerInit();
    }

    ret = MotionViewerMain();
    if (ret != 0)
    {
        debug_wrk.init_motionviewer = 0;
    }

    return ret != 0;
}

/* Filled-rect callback the memory meters draw their blocks with.  rgba packs
 * r/g/b/a most-significant byte first. */
void SPU_draw_rect_func(int x, int y, int w, int h, int rgba)           /* 90 */
{
    int i;
    SQAR_DAT sq;
    DISP_SQAR dq;

    sq.w = w;
    sq.h = h;
    sq.x = x;
    sq.y = y;
    sq.pri = 0;
    sq.r = 0;
    sq.g = 0;
    sq.b = 0;
    sq.alpha = 0x80;

    CopySqrDToSqr(&dq, &sq);

    dq.zbuf = 0xa000118;
    dq.test = 0x30003;
    dq.z = 0xfff3f;
    dq.alphar = 0x44;
    dq.pri = 0xc0;

    for (i = 0; i < 4; i++)
    {
        dq.r[i] = (u_char)((u_int)rgba >> 0x18);
        dq.g[i] = (u_char)((u_int)rgba >> 0x10);
        dq.b[i] = (u_char)((u_int)rgba >> 8);
    }
    dq.alpha = (u_char)rgba;

    DispSqrD(&dq);
}

/* Line callback for the same meters (the block grid). */
void SPU_draw_line_func(int x1, int y1, int x2, int y2, int rgba)       /* 114 */
{
    /* Untextured env the line draws under: blend on, always-pass test, no z
     * write. */
    static DRAW_ENV_NOTEX env =                                         /* rdata 3a3980 */
    {
        0x44,
        0x30003,
        0xa000118,
    };

    SetDrawEnvNoTex(0, &env);                                           /* 120 */
    SetLine2DPacket((float)x1, (float)y1, (float)x2, (float)y2,         /* 124 */
                    (u_char)((u_int)rgba >> 0x18),
                    (u_char)((u_int)rgba >> 0x10),
                    (u_char)((u_int)rgba >> 8),
                    (u_char)rgba);
}

/* Runs every frame from main.c, in every phase.  Holds the cheat combos and
 * whichever memory meter the MEMORY_DISP submenu has switched on. */
void EachDebugMain(void)                                                /* 130 */
{
    HEAP_WRK *hwp;
    u_int max_free;
    u_int total_free;

    /* L1 + R1 + CROSS: max out the camera and hand over every consumable. */
    if (*key_now[8] != 0 && *key_now[10] != 0 && *key_now[6] == 1)      /* 135 */
    {
        CCameraPowerUp::AllRelease(&m_plyr_camera.camera_power_up);     /* 137 */
        CNEquipTrayWrk::SetAbsorbMultiRate(&m_plyr_camera.eq_tray, 1.5f); /* 138 */
        ItemGet(10, 1);                                                 /* 140 */
        ItemGet(0, 1);                                                  /* 141 */
        ItemGet(1, 99);                                                 /* 142 */
        ItemGet(2, 99);                                                 /* 143 */
        ItemGet(3, 99);                                                 /* 144 */
        ItemGet(4, 99);                                                 /* 145 */
        ItemGet(5, 98);                                                 /* 146 */
        PlayData_ScoreCount(999999);                                    /* 148 */
    }

    /* PRESERVED ORIGINAL.  The voice / sound-bank status overlay and the SPU
     * meter need the sound module -- IsExistFreeVoice, SndBankPrintStatus,
     * SndBankGetFreeBankNum, GetVoiceNowAdrs, GetVoiceLoopAdrs,
     * SPUQueryMaxFreeSize, SPUQueryTotalFreeSize, DrawSPUMemory -- none of
     * which is reconstructed.  Statement order and the ROM line numbers are
     * authoritative; drop the #if when the sound module lands.  The three heap
     * meters that do link are implemented below. */
#if 0
    static int iNoVoiceCnt;
    static int mFlgOpen;

    if (IsExistFreeVoice() == 0)                                        /* 158 */
    {
        iNoVoiceCnt++;                                                  /* 159 */
        if (iNoVoiceCnt > 200)                                          /* 160 */
        {
            SetASCIIString2(0, 200.0f, 200.0f, 1, 0x80, 0, 0,
                            "There is no Free Voice");                  /* 161 */
        }
    }
    else
    {
        iNoVoiceCnt = 0;
    }

    /* L1 + R1 + SQUARE toggles the per-voice address dump. */
    if (*key_now[8] != 0 && *key_now[10] != 0 && *key_now[7] == 1)      /* 173 */
    {
        mFlgOpen ^= 1;                                                  /* 174 */
        SndBankPrintStatus();                                           /* 175 */
    }

    if (mFlgOpen != 0)                                                  /* 178 */
    {
        int y = 10;                                                     /* 179 */
        int i;

        for (i = 0; i < 24; i++)                                        /* 181 */
        {
            SetString2(0, 30.0f, (float)y, 1, 0x80, 0x80, 0x80,
                       "Now %d Loop %d ",
                       GetVoiceNowAdrs(0, i), GetVoiceLoopAdrs(0, i));  /* 182 */
            y += 15;
        }

        SetString2(0, 300.0f, 380.0f, 1, 0x80, 0, 0,
                   "FreeVoiceNum %d", IsExistFreeVoice());              /* 184 */
        SetString2(0, 300.0f, 410.0f, 1, 0x80, 0, 0,
                   "FreeSndBankNum %d", SndBankGetFreeBankNum());       /* 185 */
    }

    if (dbg_spu_mem_disp != 0)                                          /* 215 */
    {
        char msg[300];

        max_free = SPUQueryMaxFreeSize();                               /* 216 */
        total_free = SPUQueryTotalFreeSize();                           /* 217 */
        sprintf(msg, s_dbg_max_free, max_free, max_free >> 10);         /* 220 */
        SetASCIIString2(0, 40.0f, 360.0f, 1, 0x80, 0x80, 0x80, msg);    /* 221 */
        sprintf(msg, s_dbg_total_free, total_free, total_free >> 10);   /* 222 */
        SetASCIIString2(0, 40.0f, 400.0f, 1, 0x80, 0x80, 0x80, msg);    /* 223 */
        DrawSPUMemory(SPU_draw_rect_func, SPU_draw_line_func,
                      80, 40, 100, 300);                                /* 227 */
    }
#endif

    if (dbg_system_mem_disp != 0)                                       /* 231 */
    {
        char msg[300];

        hwp = GetSystemHeapWrkP();
        max_free = heapCtrlQueryMaxOneSize(hwp);                        /* 232 */
        hwp = GetSystemHeapWrkP();
        total_free = heapCtrlMemSize(hwp, HEAPMEM_LEAVE_SIZE);          /* 233 */
        sprintf(msg, s_dbg_max_free, max_free, max_free >> 10);         /* 235 */
        SetASCIIString2(0, 40.0f, 360.0f, 1, 0x80, 0x80, 0x80, msg);    /* 236 */
        sprintf(msg, s_dbg_total_free, total_free, total_free >> 10);   /* 237 */
        SetASCIIString2(0, 40.0f, 400.0f, 1, 0x80, 0x80, 0x80, msg);    /* 238 */
        hwp = GetSystemHeapWrkP();                                      /* 243 */
        heapCtrlDrawMemory(hwp, SPU_draw_rect_func, SPU_draw_line_func,
                           80, 40, 100, 300);
    }

    if (dbg_mdl_mem_disp != 0)                                          /* 247 */
    {
        char msg[300];

        max_free = ol_loadQueryMaxFreeSize();                           /* 248 */
        total_free = ol_loadQueryTotalFreeSize();                       /* 249 */
        sprintf(msg, s_dbg_max_free, max_free, max_free >> 10);         /* 252 */
        SetASCIIString2(0, 40.0f, 360.0f, 1, 0x80, 0x80, 0x80, msg);    /* 253 */
        sprintf(msg, s_dbg_total_free, total_free, total_free >> 10);   /* 254 */
        SetASCIIString2(0, 40.0f, 400.0f, 1, 0x80, 0x80, 0x80, msg);    /* 255 */
        ol_loadDrawMemory(SPU_draw_rect_func, SPU_draw_line_func,
                          80, 40, 100, 300);                            /* 259 */
    }

    if (dbg_cmn_mem_disp != 0)                                          /* 263 */
    {
        char msg[300];

        max_free = mem_utilQueryMaxFreeSize();                          /* 264 */
        total_free = mem_utilQueryTotalFreeSize();                      /* 265 */
        sprintf(msg, s_dbg_max_free, max_free, max_free >> 10);         /* 268 */
        SetASCIIString2(0, 40.0f, 360.0f, 1, 0x80, 0x80, 0x80, msg);    /* 269 */
        sprintf(msg, s_dbg_total_free, total_free, total_free >> 10);   /* 270 */
        SetASCIIString2(0, 40.0f, 400.0f, 1, 0x80, 0x80, 0x80, msg);    /* 271 */
        mem_utiDrawMemory(SPU_draw_rect_func, SPU_draw_line_func,
                          80, 40, 100, 300);                            /* 275 */
    }

#if 0
    /* PRESERVED ORIGINAL -- needs the SIF memory queries. */
    if (dbg_iop_mem_disp != 0)                                          /* 280 */
    {
        SetString2(0, 30.0f, 350.0f, 1, 0x80, 0, 0,
                   "sif max %d", sceSifQueryMaxFreeMemSize());          /* 281 */
        SetString2(0, 30.0f, 380.0f, 1, 0x80, 0, 0,
                   "sif total %d", sceSifQueryTotalFreeMemSize());      /* 282 */
    }
#endif

    dbg_enemy_button_pre = dbg_enemy_button;                            /* 292 */

    DrawCrossLineLastReal();                                            /* 293 */
}

static int DebugPadPressed(int label)
{
    if (label < 0 || label >= 32 || paddat == (u_short **)0 || paddat[label] == (u_short *)0)
    {
        return 0;
    }

    return *paddat[label] == 1;
}

static int DebugMenuDataCount(void)
{
    int n = 0;
    while (*dbg_menu_data[n].str != '\0')
    {
        n++;
    }

    return n;
}

void InitDebug(void)
{
    debug_wrk.init_subtitle_test = 0;
    debug_wrk.mode = 0;
    debug_wrk.menu_csr = 0;
    debug_wrk.comp_mode = 0;
    debug_wrk.init_movieviewer = 0;
    debug_wrk.init_msg_viewer = 0;
    debug_wrk.init_sndtest = 0;
    debug_wrk.init_scntest = 0;
    debug_wrk.init_screen_calib = 0;
    debug_wrk.init_test2d = 0;
    debug_wrk.init_motionviewer = 0;
}

void DebugMain(void)
{
    int ret;

    if (debug_wrk.mode != 0)
    {
        if (debug_wrk.mode == 1)
        {
            if (debug_wrk.menu_csr < 0 || debug_wrk.menu_csr >= DebugMenuDataCount())
            {
                debug_wrk.menu_csr = 0;
            }

            ret = 1;
            if (dbg_menu_data[debug_wrk.menu_csr].func != (int (*)(void))nullptr)
            {
                ret = dbg_menu_data[debug_wrk.menu_csr].func();
            }

            if (ret != 0)
            {
                debug_wrk.mode = 0;
            }
        }
        else
        {
            printf("*****(DEBUG MODE) ERROR MODE = %d *****\n", debug_wrk.mode);
        }

        return;
    }

    DebugMenu();
}

int DebugEnd(void)
{
    SetNextGPhase(GID_TITLE_MODE);
    return 1;
}

void init_Debug_Menu(void)
{
    InitDebug();
}

void end_Debug_Menu(void)
{
}

GPHASE_ENUM one_Debug_Menu(GPHASE_ENUM dummy)
{
    (void)dummy;

    DebugMain();
    return GPHASE_CONTINUE;
}

void DebugMenu(void)
{
    DEBUG_MENU_DATA *pDVar3;
    int oneline;
    int sbjnum;
    int n;

    sbjnum = 0;
    pDVar3 = dbg_menu_data;
    if (*dbg_menu_data[0].str != '\0')
    {
        oneline = 0x28;
        do
        {
            SetASCIIString2(0, 200.0f, (float)oneline, 1, 0x80, 0x80, 0x80, pDVar3->str);
            oneline += 0x20;
            pDVar3++;
            sbjnum++;
        } while (*pDVar3->str != '\0');
    }

    if (sbjnum != 0)
    {
        if (debug_wrk.menu_csr < 0 || debug_wrk.menu_csr >= sbjnum)
        {
            debug_wrk.menu_csr = 0;
        }

        SetASCIIString2(0, 170.0f, (float)(debug_wrk.menu_csr * 0x20 + 0x28),
                        1, 0x80, 0x80, 0x80, dbg_menu_cursor);
    }

    if (DebugPadPressed(9) != 0 && sbjnum != 0)
    {
        debug_wrk.menu_csr = (debug_wrk.menu_csr + sbjnum - 1) % sbjnum;
    }

    if (DebugPadPressed(8) != 0 && sbjnum != 0)
    {
        debug_wrk.menu_csr = (debug_wrk.menu_csr + 1) % sbjnum;
    }

    if (DebugPadPressed(1) == 1)
    {
        SetNextGPhase(GID_TITLE_MODE);
        return;
    }

    if (DebugPadPressed(0) == 1)
    {
        n = DebugMenuDataCount();
        if (n != 0)
        {
            if (debug_wrk.menu_csr < 0 || debug_wrk.menu_csr >= n)
            {
                debug_wrk.menu_csr = 0;
            }

            debug_wrk.init_func = 0;
            debug_wrk.mode = 1;
        }
    }
}
