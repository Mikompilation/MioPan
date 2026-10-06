// FILE: /home/zero_rom/zero2np/src/ingame/loading/loading.c
//
// In-game loading screen: the swirl/cloud background and "NOW LOADING"
// caption shown while ingame streams a room in.  loading_ctrl runs a small
// step machine (0 = waiting on the texture load, 1 = showing the screen);
// entering step 1 fades in from black (anim_step 0) over 15 frames before
// settling into the looping display (anim_step 1).  anim_timer drives the
// scroll/caption-pulse animation curves and free-wraps at 450 frames.
//
// GetLoadingTexMem / LoadingTexLoadReq / LoadingTexLoadWait /
// ReleaseLoadingTexMem are the Get*TexMem/*TexLoadReq/*TexLoadWait/
// Release*TexMem quad other screens use (see logo.c, loadgame.c); outgame.c's
// BackGroundLoadReq() drives the first two.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "loading.h"

#include "loading_dat.h"                        // loading_tex[]
#include "../menu/anim_2d.h"                    // Anim2D_CalcNowPos / Anim2D_CalcNowAlpha
#include "../../common/ol_load.h"               // ol_loadGetHeap / ol_loadFreeHeap
#include "../../common/utility2.h"              // PRINT_ASSERT
#include "../../graphics/draw_env.h"             // GET_SCISSOR_REGISTER / SetScissorRegister
#include "../../graphics/graph2d/g2d_draw.h"     // DISP_SPRT / DISP_SQAR / Copy*ToSpr / Disp*D
#include "../../graphics/graph2d/tim2.h"         // PK2SendVram
#include "../../outgame/outgame.h"               // BackGroundLoadReq
#include "../../system/eeiop/cddat.h"            // LOADING_PK2 / GetFileSize
#include "../../system/eeiop/fileload.h"         // FileLoadReqEE / FileLoadIsEnd2
#include "../../system/os/system.h"              // GetLanguage

// ──────────────────────────────────────────────────────────────────────
// Playback state.  Field order below is the ROM's store order, which is
// declaration order.

typedef struct                      /* 0x6 */
{
    /* 0x0 */ char       step;         // 0 = loading, 1 = showing
    /* 0x1 */ char       anim_step;    // 0 = fading in from black, 1 = steady
    /* 0x2 */ short int  anim_timer;   // drives scroll / caption-pulse curves
    /* 0x4 */ short int  fade_timer;   // frames since anim_step went to 1
} LOADING_CTRL;

static LOADING_CTRL loading_ctrl;     // sbss 3f4d68
static void        *loading_tex_addr; // sdata 3f18d8

// ──────────────────────────────────────────────────────────────────────
// Forward declarations for the file-static per-frame helpers.

static void   LoadingAnimCtrl(void);
static u_char CalcLoadingBlackFadeAlpha(int timer);
static void   LoadingBlackBgDisp(u_char alpha);
static void   LoadingBgDisp(int off_x, int off_y, u_char alpha);
static void   LoadingNowLoadingDisp(int off_x, int off_y, u_char alpha);

// ──────────────────────────────────────────────────────────────────────
// Module init: drop the texture-buffer pointer.

void LoadingInit(void)
{
    loading_tex_addr = (void *)0;                                       /* 113 */
}

// ──────────────────────────────────────────────────────────────────────
// Allocate the loading-screen texture buffer from the outgame load heap.

void GetLoadingTexMem(void)
{
    if (loading_tex_addr == (void *)0)                                  /* 123 */
    {
        loading_tex_addr = ol_loadGetHeap(
            GetFileSize(LOADING_PK2 + (char)GetLanguage()));            /* 125 */
    }
    else
    {
        PRINT_ASSERT("Error! GetLoadingTexMem");                        /* 129 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Queue the async load of the loading-screen texture into its buffer.

void LoadingTexLoadReq(void)
{
    if (loading_tex_addr != (void *)0)                                  /* 141 */
    {
        FileLoadReqEE(LOADING_PK2 + (char)GetLanguage(), loading_tex_addr,
                      5, (FILE_LOAD_CALLBACK)0, (void *)0);              /* 142 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Poll the loading-screen texture load (1 = complete).

int LoadingTexLoadWait(void)
{
    return FileLoadIsEnd2(LOADING_PK2 + (char)GetLanguage(),
                          loading_tex_addr) != 0;                       /* 166 */
}

// ──────────────────────────────────────────────────────────────────────
// Reset the step machine.  Only kicks a fresh texture load if nothing has
// claimed the buffer yet -- BackGroundLoadReq() is idempotent-by-caller, not
// by itself, so LoadingCtrlInit is what keeps re-entering the loading phase
// from re-issuing the load every time.

void LoadingCtrlInit(void)
{
    loading_ctrl.anim_step  = 0;                                        /* 176 */
    loading_ctrl.anim_timer = 0;                                        /* 177 */
    loading_ctrl.fade_timer = 0;                                        /* 178 */

    if (loading_tex_addr == (void *)0)                                  /* 182 */
    {
        BackGroundLoadReq();                                            /* 183 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Per-frame step machine.  Step 0 waits for the texture; step 1 runs the
// fade-in envelope for the first 15 frames (anim_step 0), then hands off to
// LoadingAnimCtrl for the steady-state scroll/caption timer every frame.

void LoadingCtrlMain(void)
{
    if (loading_ctrl.step == 0)                                         /* 195 */
    {
        if (LoadingTexLoadWait() != 0)                                  /* 196 */
        {
            loading_ctrl.step       = 1;                                /* 197 */
            loading_ctrl.fade_timer = 0;                                 /* 199 */
            loading_ctrl.anim_step  = 0;                                 /* 199 */
        }
    }
    else if (loading_ctrl.step == 1)                                    /* 202 */
    {
        if (loading_ctrl.anim_step == 0)                                /* 203 */
        {
            loading_ctrl.fade_timer++;                                  /* 204 */
            if (loading_ctrl.fade_timer > 14)                           /* 206 */
            {
                loading_ctrl.anim_step  = loading_ctrl.step;             /* 207 */
                loading_ctrl.fade_timer = 0;                             /* 208 */
            }
        }

        LoadingAnimCtrl();                                               /* 213 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Advance the scroll/caption-pulse timer, free-wrapping at 450 frames.

static void LoadingAnimCtrl(void)
{
    loading_ctrl.anim_timer++;                                          /* 225 */
    if (loading_ctrl.anim_timer > 449)                                  /* 226 */
    {
        loading_ctrl.anim_timer = 0;                                    /* 227 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Per-frame draw.  Steady display is background + cloud + caption; while
// still fading in (anim_step 0) an extra black rect at the fade curve's
// alpha is drawn on top to dim everything else back down.

void LoadingDispMain(void)
{
    if (loading_ctrl.step == 1)                                        /* 247 */
    {
        LoadingBlackBgDisp(0x80);                                       /* 249 */
        LoadingBgDisp(0, 0, 0x80);                                      /* 252 */
        LoadingNowLoadingDisp(0, 0, 0x80);                              /* 255 */

        if (loading_ctrl.anim_step == 0)                                /* 257 */
        {
            LoadingBlackBgDisp(
                CalcLoadingBlackFadeAlpha((int)loading_ctrl.fade_timer)); /* 258 */
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Fade-in alpha for the extra black rect: full black until 15 frames have
// elapsed, then eased down to transparent by alpha_tbl.

static u_char CalcLoadingBlackFadeAlpha(int timer)
{
    static ALPHA_ANIM_TBL alpha_tbl[2] =        // rdata 3ba8e0
    {
        { 128,   0,   0,  15 },
        {  -1,  -1,  -1,  -1 },
    };
    u_char alpha;

    alpha = 0x80;                                                       /* 282 */
    if (timer < 15)                                                     /* 285 */
    {
        alpha = Anim2D_CalcNowAlpha(alpha_tbl, timer);                  /* 287 */
    }

    return alpha;                                                       /* 294 */
}

// ──────────────────────────────────────────────────────────────────────
// Full-screen black backdrop rect at the given alpha (scaled by loading_bg's
// own baked alpha).

static void LoadingBlackBgDisp(u_char alpha)
{
    DISP_SQAR dsq;
    SQAR_DAT loading_bg =                       // rdata 3ba8f0
    {
        640, 448, 0, 0, 0, 0, 0, 0, 128,
    };

    CopySqrDToSqr(&dsq, &loading_bg);                                   /* 308 */
    dsq.alpha = (u_char)(((int)dsq.alpha * (int)alpha) >> 7);           /* 309 */
    DispSqrD(&dsq);                                                     /* 310 */
}

// ──────────────────────────────────────────────────────────────────────
// Swirl background (loading_tex[0]/[1]) plus scrolling cloud layer
// (loading_tex[2]).  The swirl is drawn twice at a width offset for a
// seamless horizontal wrap; the cloud layer uses the sprite's scroll-region
// fields (csx/csy/scw/sch) rather than moving x/y, so it tiles continuously
// while off_x/off_y (the screen shake offset) still applies to its anchor.

static void LoadingBgDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT loading_ds;
    u_long    scissor_backup;
    float     moyou_off_x;
    float     kumo_off_x;
    int       i;
    POS_ANIM_TBL moyou_x_tbl[2] =               // rdata 3ba908
    {
        { 0.0f, 510.0f,   0, 450, 0 },
        { -1.0f, -1.0f,  -1,  -1, -1 },
    };
    POS_ANIM_TBL kumo_x_tbl[2] =                // rdata 3ba928
    {
        { 0.0f, 510.0f,   0, 450, 0 },
        { -1.0f, -1.0f,  -1,  -1, -1 },
    };

    scissor_backup = GET_SCISSOR_REGISTER(0);                           /* 338 */
    SetScissorRegister(0, 0x1c000c2027f0082);                           /* 342 */

    moyou_off_x = Anim2D_CalcNowPos(moyou_x_tbl, loading_ctrl.anim_timer); /* 346 */
    kumo_off_x  = Anim2D_CalcNowPos(kumo_x_tbl, loading_ctrl.anim_timer);  /* 347 */
    PK2SendVram((uintptr_t)loading_tex_addr, -1, -1, 0);                /* 350 */

    for (i = 0; i < 2; i++)                                              /* 358 */
    {
        CopySprDToSpr(&loading_ds, loading_tex);                        /* 354 */
        loading_ds.x = (loading_ds.x + (float)off_x + moyou_off_x) -
                       (float)(loading_ds.w * i);                       /* 355 */
        loading_ds.y = loading_ds.y + (float)off_y;                     /* 355 */
        loading_ds.alpha = (u_char)(((int)loading_ds.alpha * (int)alpha) >> 7); /* 356 */
        DispSprD(&loading_ds);                                          /* 357 */
    }

    CopySprDToSpr(&loading_ds, loading_tex + 1);                        /* 361 */
    loading_ds.x = loading_ds.x + (float)off_x;                        /* 362 */
    loading_ds.y = loading_ds.y + (float)off_y;                        /* 362 */
    loading_ds.alpha = (u_char)(((int)loading_ds.alpha * (int)alpha) >> 7); /* 363 */
    DispSprD(&loading_ds);                                              /* 364 */

    for (i = 0; i < 2; i++)                                             /* 373 */
    {
        CopySprDToSpr(&loading_ds, loading_tex + 2);                    /* 367 */
        loading_ds.csy = loading_ds.y + (float)off_y;                   /* 369 */
        loading_ds.csx = ((loading_ds.x + (float)off_x) - kumo_off_x) +
                         (float)(i << 9);                                /* 369 */
        loading_ds.alpha = (u_char)(((int)loading_ds.alpha * (int)alpha) >> 7); /* 370 */
        loading_ds.scw = (float)(0x200 / (int)loading_ds.w);            /* 371 */
        loading_ds.sch = 1.0f;                                          /* 372 */
        loading_ds.x = loading_ds.csx;                                  /* 372 */
        loading_ds.y = loading_ds.csy;                                  /* 372 */
        DispSprD(&loading_ds);                                          /* 372 */
    }

    SetScissorRegister(0, scissor_backup);                              /* 377 */
}

// ──────────────────────────────────────────────────────────────────────
// "NOW LOADING" caption (loading_tex[3]) plus its trailing glyph
// (loading_tex[4]); both pulse together on a 60-frame alpha loop.

static void LoadingNowLoadingDisp(int off_x, int off_y, u_char alpha)
{
    DISP_SPRT loading_ds;
    int       i;
    ALPHA_ANIM_TBL loading_alpha_tbl[3] =       // rdata 3ba948
    {
        {  38, 128,   0,  30 },
        { 128,  38,  30,  60 },
        {  -1,  -1,  -1,  -1 },
    };

    alpha = Anim2D_CalcNowAlpha(loading_alpha_tbl,
                                loading_ctrl.anim_timer % 60);          /* 398 */
    PK2SendVram((uintptr_t)loading_tex_addr, -1, -1, 0);                /* 401 */

    for (i = 0; i < 2; i++)                                             /* 409 */
    {
        CopySprDToSpr(&loading_ds, loading_tex + 3 + i);                /* 404 */
        loading_ds.x = loading_ds.x + (float)off_x;                    /* 405 */
        loading_ds.y = loading_ds.y + (float)off_y;                    /* 405 */
        loading_ds.alpha = alpha;                                       /* 406 */
        loading_ds.alphar = 0x48;                                       /* 407 */
        DispSprD(&loading_ds);                                          /* 408 */
    }
}

// ──────────────────────────────────────────────────────────────────────
// Free the loading-screen texture buffer.

void ReleaseLoadingTexMem(void)
{
    if (loading_tex_addr != (void *)0)                                 /* 424 */
    {
        ol_loadFreeHeap(loading_tex_addr);                              /* 426 */
        loading_tex_addr = (void *)0;                                   /* 427 */
    }
}
