// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_scr.c
//
// The screen-filter machines: whole-frame blur, focus, deform meshes,
// contrast, nega, dither, overlap, fade frame, the screen saver and the
// brightness filter.  Everything here redraws the frame buffer over itself
// through DispSprD2()/DispSqrD() sprites or raw GS packets.
//
// All 42 ZERO2.MAP exports plus the statics.  Line annotations give each
// function's measured ROM range; per-statement numbers are only marked where
// they were pinned individually.
//
// PORT NOTE: the sprite paths (contrast, nega, dither, fade frame, focus,
// blur, overlap) draw through DispSprD2(), which has a host bridge; they
// sample the frame buffer copies the ROM parks in GS memory, so their look
// depends on how much of the GS frame the port mirrors.  The deform machines
// (SetDeform0/2/3/4/5/6) build raw DIRECT packets, which dmaVif1 collects
// and never executes on the host -- faithful but inert, same as every other
// packet writer in this folder.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015e760.

#include "effect_scr.h"

#include <math.h>
#include <string.h>

#include "effect.h"
#include "effect_sub.h"                  /* SetParam / ScreenCtrl */

#include "../draw_env.h"
#include "../obj_draw_ctrl.h"
#include "effect_pak.h"                  /* Reserve2DPacket */
#include "../graph2d/g2d_draw.h"         /* SetPanel / DispSprD2 / LocalCopy */
#include "../graph2d/graph2d.h"          /* effdat[] */
#include "../graph2d/tim2.h"             /* SetSprFile */
#include "../graph3d/g3dGsWrapper.h"
#include "../graph3d/g3dxVu0.h"
#include "../graph3d/gra3d.h"
#include "../../common/variable.h"       /* pad / key_now / opt_wrk / ingame_wrk */
#include "../../ingame/camera/map_camera.h" /* GetApproachCameraCrossFade */
#include "../../ingame/plyr/player.h"    /* PlayerModeIsFinder */
#include "../../miopan/miopan_memory.h"  /* MioPan_GetHostPointer */
#include "../../miopan/rendering/miopan_renderer.h"
#include "../../sdk/eekernel.h"          /* FlushCache */
#include "../../sdk/libgraph.h"
#include "../../sdk/libvu0.h"
#include "../../system/eeiop/cddat.h"    /* GetFileSize */
#include "../../system/eeiop/sndbank.h"  /* SndBank* */
#include "../../system/eeiop/snd_buffer.h" /* SndBuf* */
#include "../../system/os/eecdvd.h"      /* LoadReq / IsLoadEnd */
#include "../../system/os/system.h"      /* sys_wrk */

/* --------------------------------------------------------------------------
 *  PORT-ONLY.  Grow a whole-screen filter's rectangle to the real window.
 *
 *  The ROM authors these filters at (-0.5, -0.5) 640x448 and the renderer
 *  contracts every 2D draw back into the original 4:3 box, so on a window that
 *  is not 4:3 a filter left at those coordinates stops at the pillarbox and
 *  the extra world either side is drawn unfiltered -- with a hard edge where
 *  the filter ends.  MioPan_RendererGetViewBounds() reports that same frame
 *  grown to the window, still in 640x448 coordinates, which is exactly what
 *  ApplyOriginalAspectToVertices() maps back out.
 *
 *  Only for filters.  A full-frame *image* -- a photo, a movie frame -- keeps
 *  its proportions and must not come through here, which is why this is opted
 *  into per call site rather than applied in the renderer to every full-frame
 *  textured quad.
 *
 *  The ROM's half-pixel bias is preserved rather than snapped to the bounds:
 *  dropping it would shift the filter half a pixel against everything else.
 * ------------------------------------------------------------------------ */
static void EffScrFullScreenRect(SPRT_DAT2 *sd)
{
    float x0, y0, x1, y1;

    MioPan_RendererGetViewBounds(&x0, &y0, &x1, &y1);

    sd->x = x0 - 0.5f;
    sd->y = y0 - 0.5f;
    sd->w = x1 - x0;
    sd->h = y1 - y0;
}

/* The blur/focus envelopes and the deform bookkeeping (types.txt). */
EFF_BLUR   eff_blur = { 3, 0, 0, 0, 0, 0, 1.0f, 180.0f, 320.0f, 224.0f };
                                                            /* data 2fd500 */
EFF_FOCUS  eff_focus = { 3, 0, 0, 0, 0, 0 };                /* data 2fd528 */
EFF_DEFORM eff_deform;                                      /* sdata 3eff80 */

/* [0] is raised by SetOverRap() each frame it runs; [1] is last frame's
 * value, shifted down by InitEffectScrEF().  A frame without an overlap
 * forces the stashed picture to be recaptured. */
short overlap_passflg[2];                                   /* sdata 3eff88 */

static int SSC_BankNo;                                      /* sdata 3eff8c */

/* Function-scope statics, declared ahead as in the ROM source. */
typedef struct                          /* types.txt, 0x3 */
{
    u_char OldAlpha;
    u_char OldColor;
    u_char MakeFlg;
} MAKE_DITHER_PATTERN_CTRL_;
#define MAKE_DITHER_PATTERN_CTRL MAKE_DITHER_PATTERN_CTRL_

typedef struct                          /* types.txt, 0x2c */
{
    /* 0x00 */ float Scale;
    /* 0x04 */ int   BasePosX;
    /* 0x08 */ int   BasePosY;
    /* 0x0c */ int   PosX;
    /* 0x10 */ int   PosY;
    /* 0x14 */ int   AlphaInTime;
    /* 0x18 */ int   AlphaKeepTime;
    /* 0x1c */ int   AlphaOutTime;
    /* 0x20 */ int   DispTime;
    /* 0x24 */ int   DispTimeAll;
    /* 0x28 */ int   Alpha;
} SCREEN_SAVER_TEX;

typedef struct                          /* types.txt, 0x58 */
{
    /* 0x00 */ fixed_array<SCREEN_SAVER_TEX, 1> TexData;
    /* 0x2c */ u_int *pTexBuf;
    /* 0x30 */ int   Counter;
    /* 0x34 */ int   IntervalTime;
    /* 0x38 */ int   DispTime;
    /* 0x3c */ int   LoadStatus;
    /* 0x40 */ int   LoadId;
    /* 0x44 */ int   ScreenEffectNo;
    /* 0x48 */ int   DitherInterval;
    /* 0x4c */ float DitherAlpha;
    /* 0x50 */ int   DitherChangeTime;
    /* 0x54 */ int   PlayId;
} SCREEN_SAVER_CTRL;

static SCREEN_SAVER_CTRL ScreenSaverCtrl;                   /* bss 473720 */
static MAKE_DITHER_PATTERN_CTRL MakeDitherPatternCtrl;      /* sbss 3f4be0 */

/* One deform-grid vertex's polar decomposition (types.txt DEFWORK), and one
 * packed vertex of the MakeScrDeformPacket() strips (types.txt SCRDEF). */
typedef struct                          /* 0x18 */
{
    /* 0x00 */ float rrr;
    /* 0x04 */ float lll;
    /* 0x08 */ float mm1;
    /* 0x0c */ float mm2;
    /* 0x10 */ float sss;
    /* 0x14 */ float ccc;
} DEFWORK;

typedef struct                          /* 0x20 */
{
    /* 0x00 */ float stq[4];
    /* 0x10 */ float vtw[4];
} SCRDEF;

static DEFWORK dw[25][33];                                  /* bss 473778 */

static void SubFocus(int ef);
static void _SetScrData(Q_WORDDATA *dst, SCRDEF *src);
static void MakeScrDeformPacket(int pnumw, int pnumh, u_long tex0,
                                SCRDEF (*scrdef)[33], int alp);
static void SetDeform0(int type, float rate, u_char alp);
static void SetDeform2(int type, float rate, u_char alp);
static void SetDeform3(int type, float rate, u_char alp);
static void SetDeform4(int type, float rate, u_char alp);
static void SetDeform5(int type, float rate, u_char alp);
static void SetDeform6(int type, float rate, u_char alp);
static void MakeDitherPattern(u_int alpmx, u_int colmx);
static void MakeRDither3(u_char alpmx, u_char colmx);
static void SubDither4(float alp, float spd, int alpmx, int colmx);
static void ScreenSaverInit(void);
static void ScreenSaverOneTexInit(SCREEN_SAVER_TEX *pTex, int DispTime);
static int  ScreenSaverTexFileNoGet(void);
static void ScreenSaverMain(void);
static void ScreenSaverSetDITHER(EFFECT_CONT *ec, int DitherType, float Alpha,
                                 float Speed, int AlphaMax, int ColorMax);

void InitEffectScr(void)                                    /* ROM 168..202 */
{
    MakeDitherPatternCtrl.OldAlpha = 0x40;
    MakeDitherPatternCtrl.OldColor = 0x80;
    MakeDitherPatternCtrl.MakeFlg = 1;
    MakeDitherPattern(0x40, 0x80);

    SetParam(0, 0, 0, 0, 0, 0);

    eff_blur.flow = 3;
    eff_blur.cnt = 0;
    eff_blur.in = 0;
    eff_blur.keep = 0;
    eff_blur.out = 0;
    eff_blur.alp = 0;
    eff_blur.scl = 1.0f;
    eff_blur.rot = 180.0f;
    eff_blur.cx = 320.0f;
    eff_blur.cy = 224.0f;

    eff_focus.flow = 3;
    eff_focus.cnt = 0;
    eff_focus.in = 0;
    eff_focus.keep = 0;
    eff_focus.out = 0;
    eff_focus.max = 0;

    eff_deform.init = 1;
    eff_deform.pass = 0;
    eff_deform.type = 0;
    eff_deform.otype = 0;

    ScreenSaverInit();
}

void InitEffectScrEF(void)                                  /* ROM 209..219 */
{
    overlap_passflg[1] = overlap_passflg[0];
    overlap_passflg[0] = 0;

    /* A frame the deform never ran in starts the wobble phase over. */
    if (eff_deform.pass == 0)
    {
        eff_deform.init = 1;
    }
    eff_deform.pass = 0;
    eff_deform.otype = eff_deform.type;

    ScreenSaverMain();
}

/* ---- the whole-screen colour fades, all SetParam() wrappers ------------- */

void SetWhiteOut(void)                                      /* ROM 229..230 */
{
    SetParam(0, 32, 0xff, 0xff, 0xff, 2);
}

void SetWhiteIn(void)                                       /* ROM 236..237 */
{
    SetParam(0x80, 32, 0xff, 0xff, 0xff, 1);
}

void SetBlackOut(void)                                      /* ROM 243..244 */
{
    SetParam(0, 32, 0, 0, 0, 2);
}

void SetBlackIn(void)                                       /* ROM 250..251 */
{
    SetParam(0x80, 32, 0, 0, 0, 1);
}

void SetWhiteOut2(int time)                                 /* ROM 257..258 */
{
    SetParam(0, time, 0xff, 0xff, 0xff, 2);
}

void SetWhiteIn2(int time)                                  /* ROM 264..265 */
{
    SetParam(0x80, time, 0xff, 0xff, 0xff, 1);
}

void SetBlackOut2(int time)                                 /* ROM 271..272 */
{
    SetParam(0, time, 0, 0, 0, 2);
}

void SetBlackIn2(int time)                                  /* ROM 278..279 */
{
    SetParam(0x80, time, 0, 0, 0, 1);
}

void SetFlash(void)                                         /* ROM 285..286 */
{
    SetParam(0x80, 18, 0xff, 0xff, 0xff, 1);
}

void SetBlackFilter(EFFECT_CONT *ec)                        /* ROM 296..300 */
{
    SetPanel(0, 0.0f, 0.0f, 640.0f, 448.0f, 0, 0, 0, ec->dat.uc8[2]);
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* --------------------------------------------------------------------------
 *  Blur: the frame buffer redrawn over itself, scaled and rotated about
 *  (cx, cy).  bPhotoType selects the photo work area as the source instead
 *  of the previous frame.                                    ROM 314..352
 * ------------------------------------------------------------------------ */
void SubBlur(int type, u_char alpha, float scale, float rot,
             float cx, float cy, int bPhotoType)
{
    SPRT_DAT2 sd2 = { 0, 1.0f, 1.0f, 639.0f, 447.0f,
                      640.0f, 448.0f, 0.0f, 0.0f, 0, 0x80 };
    SPRT_DAT2 sd3 = { 0, 0.5f, 0.5f, 319.5f, 223.5f,
                      640.0f, 448.0f, 0.0f, 0.0f, 0, 0x80 };
    DISP_SPRT2 ds;
    u_char     colcol;

    (void)cx; (void)cy;

    if (bPhotoType == 0)
    {
        CopySprDToSpr2(&ds, &sd3);
        /* The 320x224 photo blur work area at GS 0x1144. */
        ds.tex0 = 0x2000000224117aa0ULL;
    }
    else
    {
        CopySprDToSpr2(&ds, &sd2);
        /* The other frame buffer: the picture just displayed. */
        ds.tex0 = (u_long)((sys_wrk.count + 1 & 1) * 0x1180) | 0x2000000268128000ULL;
    }

    ds.rot = rot - 180.0f;
    ds.tex1 = 0x161;
    ds.pri = 0xa0;
    ds.z = 0xfff00;
    ds.crx = 320.0f;
    ds.cry = 224.0f;
    ds.csx = 319.5f;
    ds.csy = 223.5f;
    ds.x = -0.5f;
    ds.y = -0.5f;
    ds.scw = scale;
    ds.sch = scale;

    /* The three types differ only in how bright the copy is laid back. */
    if (type == 0)
    {
        colcol = 0x80;
    }
    else if (type == 1)
    {
        colcol = 0x78;
    }
    else if (type == 2)
    {
        colcol = 0x88;
    }
    else
    {
        colcol = 0x80;
    }
    ds.r = colcol;
    ds.g = colcol;
    ds.b = colcol;
    ds.alp = alpha;

    DispSprD2(&ds);
}

void SetBlur(EFFECT_CONT *ec)                               /* ROM 358..365 */
{
    if (ec->pnt[0] != nullptr)
    {
        SubBlur(ec->dat.uc8[2], *(u_char *)ec->pnt[0],
                (float)ec->dat.ui32[2] / 1000.0f,
                (float)ec->dat.ui32[3] / 10.0f,
                ec->fw[0], ec->fw[1], 0);
    }
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* The stand-alone blur envelope CallBlur() arms, run every frame from
 * EffectControl() regardless of the EFFECT_CONT slots.       ROM 373..417 */
void RunBlur(EFFECT_CONT *ec)
{
    float phase;

    (void)ec;

    if (eff_blur.flow == 0)
    {
        eff_blur.cnt++;
        if (eff_blur.in != 0)
        {
            phase = ((float)eff_blur.cnt * 90.0f) / (float)eff_blur.in;
            SubBlur(0, (u_char)(int)(sinf((phase * 3.1415925f) / 180.0f)
                                     * (float)eff_blur.alp),
                    eff_blur.scl, eff_blur.rot, eff_blur.cx, eff_blur.cy, 0);
        }
        if (eff_blur.cnt < eff_blur.in)
        {
            return;
        }
        if (eff_blur.keep == 0)
        {
            eff_blur.flow = 2;
            if (eff_blur.out == 0)
            {
                eff_blur.flow = 3;
            }
        }
        else
        {
            eff_blur.flow = 1;
        }
        eff_blur.cnt = 0;
    }
    else if (eff_blur.flow == 1)
    {
        eff_blur.cnt++;
        phase = 90.0f;
        SubBlur(0, (u_char)(int)(sinf((phase * 3.1415925f) / 180.0f)
                                 * (float)eff_blur.alp),
                eff_blur.scl, eff_blur.rot, eff_blur.cx, eff_blur.cy, 0);
        if (eff_blur.cnt < eff_blur.keep)
        {
            return;
        }
        eff_blur.flow = 2;
        if (eff_blur.out == 0)
        {
            eff_blur.flow = 3;
        }
        eff_blur.cnt = 0;
    }
    else if (eff_blur.flow == 2)
    {
        eff_blur.cnt++;
        if (eff_blur.out != 0)
        {
            phase = ((float)eff_blur.cnt * 90.0f) / (float)eff_blur.out + 90.0f;
            SubBlur(0, (u_char)(int)(sinf((phase * 3.1415925f) / 180.0f)
                                     * (float)eff_blur.alp),
                    eff_blur.scl, eff_blur.rot, eff_blur.cx, eff_blur.cy, 0);
        }
        if (eff_blur.cnt < eff_blur.out)
        {
            return;
        }
        eff_blur.flow = 3;
        eff_blur.cnt = 0;
    }
}

void CallBlur(int type, int wait, u_char alpha, float scale, float rot)
{                                                           /* ROM 435..447 */
    eff_blur.flow = 3;
    if (wait != 0)
    {
        eff_blur.flow = (type != 0);
    }
    eff_blur.cnt = 0;
    eff_blur.in = 30;
    eff_blur.keep = wait;
    eff_blur.out = 30;
    eff_blur.alp = alpha;
    eff_blur.scl = scale;
    eff_blur.rot = rot;
    eff_blur.cx = 320.0f;
    eff_blur.cy = 224.0f;
}

void CallBlur2(int in, int keep, int out, u_char alpha, float scale, float rot)
{                                                           /* ROM 465..478 */
    eff_blur.flow = 0;
    if (in < 1)
    {
        if (keep < 1)
        {
            eff_blur.flow = 2;
            if (out < 1)
            {
                eff_blur.flow = 3;
            }
        }
        else
        {
            eff_blur.flow = 1;
        }
    }
    eff_blur.cnt = 0;
    eff_blur.in = in;
    eff_blur.keep = keep;
    eff_blur.out = out;
    eff_blur.alp = alpha;
    eff_blur.scl = scale;
    eff_blur.rot = rot;
    eff_blur.cx = 320.0f;
    eff_blur.cy = 224.0f;
}

void CallBlur3(int in, int keep, int out, u_char alpha, float scale, float rot,
               float cx, float cy)
{                                                           /* ROM 497..510 */
    eff_blur.flow = 0;
    if (in < 1)
    {
        if (keep < 1)
        {
            eff_blur.flow = 2;
            if (out < 1)
            {
                eff_blur.flow = 3;
            }
        }
        else
        {
            eff_blur.flow = 1;
        }
    }
    eff_blur.cnt = 0;
    eff_blur.in = in;
    eff_blur.keep = keep;
    eff_blur.out = out;
    eff_blur.alp = alpha;
    eff_blur.scl = scale;
    eff_blur.rot = rot;
    eff_blur.cx = cx;
    eff_blur.cy = cy;
}

/* --------------------------------------------------------------------------
 *  Focus: four quarter-pixel-offset copies of the frame, additively soft.
 *  `ef` is the offset in fortieths of a pixel.               ROM 517..542
 * ------------------------------------------------------------------------ */
static void SubFocus(int ef)
{
    SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 639.89996f, 447.9f,
                     640.0f, 448.0f, 0.0f, 0.0f, 0xa0, 0x80 };
    DISP_SPRT2 ds;
    float      hw;

    if (0 < ef)
    {
        CopySprDToSpr2(&ds, &sd);
        if (ef < 11)
        {
            ds.alp = (u_char)(ef << 2);
        }
        else
        {
            ds.alp = 40;
        }
        ds.z = 0xfff00;
        ds.tex0 = (u_long)((sys_wrk.count & 1) * 0x1180) | 0x2000000268128000ULL;

        hw = (float)ef / 40.0f;

        ds.x = -0.5f - hw;
        ds.y = -0.5f - hw;
        DispSprD2(&ds);
        ds.x = hw + -0.5f;
        ds.y = -0.5f - hw;
        DispSprD2(&ds);
        ds.x = -0.5f - hw;
        ds.y = hw + -0.5f;
        DispSprD2(&ds);
        ds.x = hw + -0.5f;
        ds.y = hw + -0.5f;
        DispSprD2(&ds);
    }
}

void SetFocus(EFFECT_CONT *ec)                              /* ROM 548..556 */
{
    /* Only the fixed part draws here; the envelope in RunFocus() owns the
     * slot while it is running. */
    if (ec->dat.uc8[2] != 0 && eff_focus.flow == 3)
    {
        SubFocus(ec->dat.uc8[2]);
        if ((ec->dat.uc8[1] & 1) != 0)
        {
            ResetEffects(ec);
        }
    }
}

void RunFocus(EFFECT_CONT *ec)                              /* ROM 561..605 */
{
    float phase;

    if (eff_focus.flow == 0)
    {
        eff_focus.cnt++;
        if (eff_focus.in != 0)
        {
            phase = ((float)eff_focus.cnt * 90.0f) / (float)eff_focus.in;
            SubFocus(ec->dat.uc8[2]
                     + (int)(sinf((phase * 3.1415925f) / 180.0f)
                             * (float)eff_focus.max));
        }
        if (eff_focus.cnt < eff_focus.in)
        {
            return;
        }
        if (eff_focus.keep == 0)
        {
            eff_focus.flow = 2;
            if (eff_focus.out == 0)
            {
                eff_focus.flow = 3;
            }
        }
        else
        {
            eff_focus.flow = 1;
        }
        eff_focus.cnt = 0;
    }
    else if (eff_focus.flow == 1)
    {
        eff_focus.cnt++;
        phase = 90.0f;
        SubFocus(ec->dat.uc8[2]
                 + (int)(sinf((phase * 3.1415925f) / 180.0f)
                         * (float)eff_focus.max));
        if (eff_focus.cnt < eff_focus.keep)
        {
            return;
        }
        if (eff_focus.out != 0)
        {
            eff_focus.flow = 2;
            eff_focus.cnt = 0;
            return;
        }
        eff_focus.flow = 3;
        eff_focus.cnt = 0;
    }
    else if (eff_focus.flow == 2)
    {
        eff_focus.cnt++;
        if (eff_focus.out != 0)
        {
            phase = ((float)eff_focus.cnt * 90.0f) / (float)eff_focus.out + 90.0f;
            SubFocus(ec->dat.uc8[2]
                     + (int)(sinf((phase * 3.1415925f) / 180.0f)
                             * (float)eff_focus.max));
        }
        if (eff_focus.cnt < eff_focus.out)
        {
            return;
        }
        eff_focus.flow = 3;
        eff_focus.cnt = 0;
    }
}

void CallFocus(int type, int wait, int gap)                 /* ROM 622..628 */
{
    eff_focus.flow = (type != 0);
    eff_focus.cnt = 0;
    eff_focus.in = 30;
    eff_focus.keep = wait;
    eff_focus.out = 30;
    eff_focus.max = gap;
}

void CallFocus2(int in, int keep, int out, int max)         /* ROM 645..651 */
{
    eff_focus.flow = 0;
    if (in < 1)
    {
        if (keep < 1)
        {
            eff_focus.flow = 2;
            if (out < 1)
            {
                eff_focus.flow = 3;
            }
        }
        else
        {
            eff_focus.flow = 1;
        }
    }
    eff_focus.cnt = 0;
    eff_focus.in = in;
    eff_focus.keep = keep;
    eff_focus.out = out;
    eff_focus.max = max;
}

/* ---- deform ------------------------------------------------------------- */

void SubDeform(int type, float rate, u_char alp)            /* ROM 658..680 */
{
    switch (type)
    {
    case 1:
        SetDeform0(type, rate, alp);
        break;
    case 2:
        SetDeform0(type, rate, alp);
        break;
    case 3:
        SetDeform2(type, rate, alp);
        break;
    case 4:
        SetDeform3(type, rate, alp);
        break;
    case 5:
        SetDeform4(type, rate, alp);
        break;
    case 6:
        SetDeform5(type, rate, alp);
        break;
    case 7:
        SetDeform6(type, rate, alp);
        break;
    }
}

void SetDeform(EFFECT_CONT *ec)                             /* ROM 687..728 */
{
    float phase;
    float ef;

    ef = 0.0f;

    /* A type change mid-run restarts the machine's own state. */
    if (ec->dat.uc8[2] != eff_deform.otype)
    {
        eff_deform.init = 1;
    }

    if ((ec->dat.uc8[1] & 4) != 0)
    {
        if (ec->flow == 0)
        {
            if (ec->in != 0)
            {
                phase = ((float)ec->cnt * 90.0f) / (float)ec->in;
                ef = sinf((phase * 3.1415925f) / 180.0f)
                     * (float)ec->dat.uc8[3];
            }
        }
        else if (ec->flow == 1)
        {
            phase = 90.0f;
            ef = sinf((phase * 3.1415925f) / 180.0f) * (float)ec->dat.uc8[3];
        }
        else if (ec->flow == 2)
        {
            if (ec->out != 0)
            {
                phase = ((float)ec->cnt * 90.0f) / (float)ec->out + 90.0f;
                ef = sinf((phase * 3.1415925f) / 180.0f)
                     * (float)ec->dat.uc8[3];
            }
        }
        else if (ec->flow == 3)
        {
            ResetEffects(ec);
            return;
        }
        EffInKeepOutFlowCtrl(ec);
    }
    else
    {
        ef = (float)ec->dat.uc8[3];
    }

    SubDeform(ec->dat.uc8[2], ef, 0x80);
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

void CallDeform2(int in, int keep, int out, int type, int max)
{                                                           /* ROM 746..747 */
    SetEffects_DEFORM(4, type, max, (u_int)in, (u_int)keep, (u_int)out);
}

/* --------------------------------------------------------------------------
 *  The deform mesh writers.  A SCRDEF is one vertex (STQ quadword + screen
 *  XYZ); _SetScrData() converts one to a packed ST/XYZ2 pair with the VU0
 *  ftoi4 idiom.  The ROM's is inline COP2 asm; the host does the same
 *  arithmetic directly (4-bit fixed point, truncating).      ROM 782
 * ------------------------------------------------------------------------ */
static void _SetScrData(Q_WORDDATA *dst, SCRDEF *src)
{
    dst[0].iv[0] = (int)(src->stq[0] * 16.0f);
    dst[0].iv[1] = (int)(src->stq[1] * 16.0f);
    dst[0].iv[2] = (int)(src->stq[2] * 16.0f);
    dst[0].iv[3] = (int)(src->stq[3] * 16.0f);
    dst[1].iv[0] = (int)(src->vtw[0] * 16.0f);
    dst[1].iv[1] = (int)(src->vtw[1] * 16.0f);
    dst[1].iv[2] = (int)(src->vtw[2] * 16.0f);
    dst[1].iv[3] = (int)(src->vtw[3] * 16.0f);
}

/* One long sprite strip over the pnumw x pnumh grid: per row, the top/bottom
 * vertex pair primes the strip and every following pair extends it.
 *                                                            ROM 800..861 */
static void MakeScrDeformPacket(int pnumw, int pnumh, u_long tex0,
                                SCRDEF (*scrdef)[33], int alp)
{
    Q_WORDDATA *ppbuf;
    int         i;
    int         j;

    {
        DRAW_ENV_5 env;

        env.alpha = 0x8000000044ULL;
        env.tex1 = 0x161;
        env.clamp = 0;
        env.test = 0x30003;
        env.zbuf = 0x10a000118ULL;
        SetDrawEnv(0, &env);
    }

    Reserve2DPacket(0x10);
    ppbuf = StartDmaDirectTrans();

    /* A+D: TEXFLUSH, TEX0, RGBAQ; then the UV strip PRIM tag. */
    ppbuf[0].ul64[0] = 0x1000000000008003ULL;
    ppbuf[0].ul64[1] = 0x0e;
    ppbuf[1].ul64[0] = 0;
    ppbuf[1].ul64[1] = 0x3f;
    ppbuf[2].ul64[0] = tex0;
    ppbuf[2].ul64[1] = 0x06;
    ppbuf[3].ul64[0] = ((u_long)alp << 24) | 0x100808080ULL;
    ppbuf[3].ul64[1] = 1;
    ppbuf[4].ul64[0] = (u_long)(pnumh * (pnumw * 2 + 2)) | 0x20ae400000008000ULL;
    ppbuf[4].ul64[1] = 0x43;
    ppbuf += 5;

    for (i = 0; i < pnumh; i++)
    {
        _SetScrData(ppbuf, &scrdef[i][0]);
        _SetScrData(ppbuf + 2, &scrdef[i + 1][0]);
        /* ADC on both: the first column only primes the strip. */
        ppbuf[1].ui32[3] = 0x8000;
        ppbuf[3].ui32[3] = 0x8000;
        ppbuf += 4;

        for (j = 1; j <= pnumw; j++)
        {
            _SetScrData(ppbuf, &scrdef[i][j]);
            _SetScrData(ppbuf + 2, &scrdef[i + 1][j]);
            ppbuf += 4;
        }
    }

    EndDmaDirectTrans(ppbuf);

    /* PORT: the strip above is inert -- dmaVif1 discards it -- so the same grid
     * goes to the renderer as a triangle list.  Two triangles per cell, and the
     * per-row ADC pair the ROM uses to break the strip between rows simply does
     * not arise when the cells are emitted individually.
     *
     * vtw holds GS window coordinates, so the XYOFFSET the 3D env installs
     * comes back off: 1728 = 2048 - 320 and 1824 = 2048 - 224, the 640x448
     * frame centred in the GS's 4096x4096 space.  stq is already in the copy's
     * own texel space (0..320 x 0..448), which is what the bridge wants. */
    {
        static float xy[32 * 24 * 6 * 2];
        static float uv[32 * 24 * 6 * 2];
        static u_char rgba[32 * 24 * 6 * 4];
        int n = 0;

        for (i = 0; i < pnumh; i++)
        {
            for (j = 0; j < pnumw; j++)
            {
                static const int cell[6][2] =
                {
                    {0, 0}, {0, 1}, {1, 0},     /* first triangle of the cell  */
                    {1, 0}, {0, 1}, {1, 1},     /* second                      */
                };
                int c;

                for (c = 0; c < 6; c++)
                {
                    SCRDEF *p = &scrdef[i + cell[c][1]][j + cell[c][0]];

                    xy[n * 2 + 0] = p->vtw[0] - 1728.0f;
                    xy[n * 2 + 1] = p->vtw[1] - 1824.0f;
                    uv[n * 2 + 0] = p->stq[0];
                    uv[n * 2 + 1] = p->stq[1];
                    rgba[n * 4 + 0] = 0x80;
                    rgba[n * 4 + 1] = 0x80;
                    rgba[n * 4 + 2] = 0x80;
                    rgba[n * 4 + 3] = (u_char)alp;
                    n++;
                }
            }
        }

        MioPan_RendererDrawTexturedTriangles2D((sceGsTex0 *)&tex0, xy, uv, rgba,
                                               nullptr, n, 0, 0, 0);
    }
}

/* --------------------------------------------------------------------------
 *  Deform types 1/2: a 25x17 grid over the previous frame, interior
 *  vertices swept up (and, for type 2, sideways too) by a travelling sine.
 *  The source is the current frame halved into the work area first.
 *                                                            ROM 868..1003
 * ------------------------------------------------------------------------ */
static void SetDeform0(int type, float rate, u_char alp)
{
    static float r;                                         /* sdata 3eff90 */
    static float add = 6.0f;                                /* sdata 3eff94 */
    static int   swch;                                      /* sdata 3eff98 */

    float tx[17][25];
    float ty[17][25];
    float vtw[17][25][4];
    Q_WORDDATA *pbuf;
    int   ndpkt;
    int   i;
    int   j;
    int   c;
    float fw;
    float ll;
    u_long deform_tex0;

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    for (i = 0; i < 17; i++)
    {
        ll = ((float)i * 448.0f) / 16.0f;
        for (j = 0; j < 25; j++)
        {
            vtw[i][j][0] = ((float)j * 640.0f) / 24.0f + 1728.0f + -0.5f;
            vtw[i][j][1] = ll + 1824.0f + -0.5f;
            vtw[i][j][2] = 0.0f;
            vtw[i][j][3] = 1.0f;
            tx[i][j] = ((float)j * 320.0f) / 24.0f;
            ty[i][j] = ll;

            /* Pull the border texels half a pixel in so filtering cannot
             * wrap. */
            if (j == 0)
            {
                tx[i][0] += 1.0f;
            }
            if (j == 24)
            {
                tx[i][j] -= 1.0f;
            }
            if (i == 0)
            {
                ty[0][j] += 1.0f;
            }
            if (i == 16)
            {
                ty[i][j] -= 1.0f;
            }
        }
    }

    fw = rate / 10.0f;
    add = 2.0f;
    for (i = 0; i < 17; i++)
    {
        for (j = 0; j < 25; j++)
        {
            if (j != 0 && j != 24 && i != 0 && i != 16)
            {
                vtw[i][j][0] += sinf(((r + (float)i * 50.0f) * 3.1415925f)
                                     / 180.0f) * fw;
                if (type == 2)
                {
                    vtw[i][j][1] += sinf(((r + (float)j * 50.0f) * 3.1415925f)
                                         / 180.0f) * fw;
                }
                else
                {
                    vtw[i][j][1] += sinf(((r + (float)i * 50.0f) * 3.1415925f)
                                         / 180.0f) * fw;
                }
            }
        }
    }

    if (EffWrkStopFlgGet() == 0)
    {
        r += add;
        if (r > 360.0f)
        {
            r -= 360.0f;
        }
    }

    {
        DRAW_ENV_5 env;

        env.alpha = 0x8000000044ULL;
        env.tex1 = 0x161;
        env.clamp = 0;
        env.test = 0x30003;
        env.zbuf = 0x10a000118ULL;
        SetDrawEnv(0, &env);
    }

    pbuf = StartDmaDirectTrans();
    Reserve2DPacket(0x10);

    pbuf[0].ul64[0] = 0x1000000000008002ULL;
    pbuf[0].ul64[1] = 0x0e;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = 0x3f;
    /* The halved copy in the work area at GS 0x16bc.  Held in a local as well,
       because the host bridge below needs it after the ring has been committed
       and the ring's storage is not ours to read back. */
    deform_tex0 = 0x2000000264016bc0ULL;
    pbuf[2].ul64[0] = deform_tex0;
    pbuf[2].ul64[1] = 0x06;

    /* swch selects the debug look: flat white or a per-column ramp. */
    if (swch == 0)
    {
        pbuf[3].ul64[0] = 0x30ae400000000000ULL | 0x8320;
    }
    else
    {
        pbuf[3].ul64[0] = 0x30e2400000000000ULL | 0x8320;
    }
    pbuf[3].ul64[1] = 0x431;

    ndpkt = 4;
    for (i = 0; i < 16; i++)
    {
        c = 0;
        for (j = 0; j < 50; j++)
        {
            int row = (j & 1) + i;
            int col = j / 2;

            if (swch == 0)
            {
                pbuf[ndpkt].ui32[0] = 0x80;
                pbuf[ndpkt].ui32[1] = 0x80;
                pbuf[ndpkt].ui32[2] = 0x80;
            }
            else
            {
                pbuf[ndpkt].ui32[0] = c;
                pbuf[ndpkt].ui32[1] = c;
                pbuf[ndpkt].ui32[2] = c;
            }
            pbuf[ndpkt].ui32[3] = alp;

            pbuf[ndpkt + 1].ui32[0] = (int)(tx[row][col] * 16.0f);
            pbuf[ndpkt + 1].ui32[1] = (int)(ty[row][col] * 16.0f);
            pbuf[ndpkt + 1].ui32[2] = 0;
            pbuf[ndpkt + 1].ui32[3] = 0;

            pbuf[ndpkt + 2].iv[0] = (int)(vtw[row][col][0] * 16.0f);
            pbuf[ndpkt + 2].iv[1] = (int)(vtw[row][col][1] * 16.0f);
            pbuf[ndpkt + 2].iv[2] = (int)(vtw[row][col][2] * 16.0f);
            pbuf[ndpkt + 2].ui32[3] = (j < 2) ? 0x8000 : 0;

            ndpkt += 3;
            c += 2;
        }
    }

    eff_deform.type = (u_char)type;
    eff_deform.pass = 1;
    eff_deform.init = 1;
    EndDmaDirectTrans(pbuf + ndpkt);

    /* PORT: the strip above is inert -- dmaVif1 discards it -- so the same grid
     * goes to the renderer as a triangle list.  Identical topology to
     * MakeScrDeformPacket's: the strip walks columns emitting the (row, row+1)
     * pair, so each cell is two triangles and the ADC pair that breaks the
     * strip between row bands does not arise when cells are emitted one by one.
     *
     * vtw holds GS window coordinates, so the 3D env's XYOFFSET comes back off
     * (1728 = 2048 - 320, 1824 = 2048 - 224).  tx/ty are already in the copy's
     * own texel space (0..320 x 0..448).
     *
     * `swch` is the ROM's debug toggle: 0 draws flat white, non-zero a per
     * column ramp.  In the strip the ramp counter advances two per vertex, so
     * at (row, col) it is 4 * col + 2 * (row - band) -- reproduced here rather
     * than dropped, because the strip order it came from is gone. */
    {
        static float  xy[16 * 24 * 6 * 2];
        static float  uv[16 * 24 * 6 * 2];
        static u_char rgba[16 * 24 * 6 * 4];
        int n = 0;

        for (i = 0; i < 16; i++)
        {
            for (j = 0; j < 24; j++)
            {
                static const int cell[6][2] =           /* {col, row} offsets */
                {
                    {0, 0}, {0, 1}, {1, 0},
                    {1, 0}, {0, 1}, {1, 1},
                };

                for (c = 0; c < 6; c++)
                {
                    int row = i + cell[c][1];
                    int col = j + cell[c][0];
                    int ramp = 4 * col + 2 * cell[c][1];

                    xy[n * 2 + 0] = vtw[row][col][0] - 1728.0f;
                    xy[n * 2 + 1] = vtw[row][col][1] - 1824.0f;
                    uv[n * 2 + 0] = tx[row][col];
                    uv[n * 2 + 1] = ty[row][col];
                    rgba[n * 4 + 0] = (u_char)(swch == 0 ? 0x80 : ramp);
                    rgba[n * 4 + 1] = (u_char)(swch == 0 ? 0x80 : ramp);
                    rgba[n * 4 + 2] = (u_char)(swch == 0 ? 0x80 : ramp);
                    rgba[n * 4 + 3] = alp;
                    n++;
                }
            }
        }

        MioPan_RendererDrawTexturedTriangles2D(
            (sceGsTex0 *)&deform_tex0, xy, uv, rgba, nullptr, n, 0, 0, 0);
    }
}

/* --------------------------------------------------------------------------
 *  Deform type 3: a ripple over a 25000-unit plane hung 2000 units in front
 *  of the camera, projected per-vertex through the world->screen matrix.
 *  The polar decomposition of the 33x25 grid is cached in file statics on
 *  init.                                                     ROM 1012..1130
 * ------------------------------------------------------------------------ */
static void SetDeform2(int type, float rate, u_char alp)
{
    static float rrr[825];                                  /* bss 46e8f0 */
    static float lll[825];                                  /* bss 46f5d8 */
    static float mm1[825];                                  /* bss 4702c0 */
    static float mm2[825];                                  /* bss 470fa8 */
    static float sss[825];                                  /* bss 471c90 */
    static float ccc[825];                                  /* bss 472978 */
    static float r;                                         /* sdata 3eff9c */
    static float add = 6.0f;                                /* sdata 3effa0 */
    static int   swch;                                      /* sdata 3effa4 */

    float tx[825];
    float ty[825];
    float slm[4][4];
    float wlm[4][4];
    float pos[4];
    static float vt[825][4];   /* PORT: 26KB of vertices; the ROM stacks them */
    static float vtw[825][4];
    float rot_x, rot_y;
    GRA3DCAMERA *pCam;
    Q_WORDDATA  *pbuf;
    int   ndpkt;
    int   i;
    int   wix, wiy;
    float fw;
    u_long deform_tex0;

    pos[0] = 25000.0f;
    pos[1] = -800.0f;
    pos[2] = 6500.0f;
    pos[3] = 1.0f;

    pCam = gra3dGetCamera();
    Vector2Rot(gra3dcamGetDirection(), &rot_x, &rot_y);
    {
        float *cpos = gra3dcamGetPosition();
        float *cdir = gra3dcamGetDirection();

        pos[0] = cpos[0] + cdir[0] * 2000.0f;
        pos[1] = cpos[1] + cdir[1] * 2000.0f;
        pos[2] = cpos[2] + cdir[2] * 2000.0f;
    }

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    sceVu0UnitMatrix(wlm);
    wlm[0][0] = wlm[1][1] = wlm[2][2] = 25.0f;
    sceVu0TransMatrix(wlm, wlm, pos);
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

    /* NOT widened, unlike the CalcStqXYZ() callers' identical stqparam[1] box
       (see EffectSetScreenSampleClamp in effect_obj.h).  This one reaches the
       bridge as projected = 0, so its geometry is grown about the frame centre
       and its UVs span the whole capture -- window onto window, with the ROM's
       640x448 standing for the output whatever its aspect.  The ROM's bounds
       are already the right ones here. */

    /* Project the grid and turn it into clamped half-scale texels. */
    for (i = 0; i < 825; i++)
    {
        wiy = i / 33;
        wix = i % 33;
        vt[i][0] = (float)(wix * 2) - 32.0f;
        vt[i][1] = (float)(wiy * 2) - 24.0f;
        vt[i][2] = 0.0f;
        vt[i][3] = 1.0f;
        sceVu0ApplyMatrix(vtw[i], slm, vt[i]);
        sceVu0DivVector(vtw[i], vtw[i], vtw[i][3]);

        tx[i] = (vtw[i][0] - 2048.0f) + 320.0f;
        ty[i] = (vtw[i][1] - 2048.0f) + 224.0f;
        if (tx[i] < 0.0f)
        {
            tx[i] = 0.0f;
        }
        else if (tx[i] > 639.0f)
        {
            tx[i] = 639.0f;
        }
        if (ty[i] < 0.0f)
        {
            ty[i] = 0.0f;
        }
        else if (ty[i] > 447.0f)
        {
            ty[i] = 447.0f;
        }
        tx[i] *= 0.5f;
    }

    fw = rate / 25.0f;

    if (eff_deform.init != 0)
    {
        for (i = 0; i < 825; i++)
        {
            float fx;
            float fy;

            wiy = i / 33;
            wix = i % 33;
            fx = (float)(wix * 2) - 32.0f;
            fy = (float)(wiy * 2) - 24.0f;

            lll[i] = sqrtf(fx * fx + fy * fy);
            if (wix == 16 && wiy == 12)
            {
                rrr[i] = 0.0f;
            }
            else
            {
                rrr[i] = atan2f(fx, fy);
            }
            mm1[i] = (lll[i] * 3.1415925f * 12.0f) / lll[0];
            mm2[i] = (lll[0] - lll[i]) / lll[0];
            sss[i] = sinf(rrr[i]);
            ccc[i] = cosf(rrr[i]);
        }
        eff_deform.init = 0;
    }

    /* Re-lay the plane in polar space, swung by the travelling sine. */
    for (i = 0; i < 825; i++)
    {
        float lm = sinf(mm1[i] - r) * fw * mm2[i];

        vt[i][0] = sss[i] * lll[i] - ccc[i] * lm;
        vt[i][1] = ccc[i] * lll[i] + sss[i] * lm;
    }

    add = 0.08f;
    if (EffWrkStopFlgGet() == 0)
    {
        r += add;
        if (r > 6.283185f)
        {
            r -= 6.283185f;
        }
    }

    for (i = 0; i < 825; i++)
    {
        sceVu0ApplyMatrix(vtw[i], slm, vt[i]);
        sceVu0DivVector(vtw[i], vtw[i], vtw[i][3]);
    }

    {
        DRAW_ENV_5 env;

        env.alpha = 0x8000000044ULL;
        env.tex1 = 0x161;
        env.clamp = 0;
        env.test = 0x30003;
        env.zbuf = 0x10a000118ULL;
        SetDrawEnv(0, &env);
    }

    Reserve2DPacket(0x10);
    pbuf = StartDmaDirectTrans();

    pbuf[0].ul64[0] = 0x1000000000008002ULL;
    pbuf[0].ul64[1] = 0x0e;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = 0x3f;
    deform_tex0 = 0x2000000264016bc0ULL;
    pbuf[2].ul64[0] = deform_tex0;
    pbuf[2].ul64[1] = 0x06;
    if (swch == 0)
    {
        pbuf[3].ul64[0] = ((0x60ae0000ULL | 0x4000) << 32) | 0x8318;
    }
    else
    {
        pbuf[3].ul64[0] = ((0x60e20000ULL | 0x4000) << 32) | 0x8318;
    }
    pbuf[3].ul64[1] = 0x413413;

    /* 792 column pairs of ST/RGBAQ/XYZ2, two vertices per pair. */
    ndpkt = 4;
    for (i = 0; i < 792; i++)
    {
        pbuf[ndpkt].ui32[0] = (u_int)(int)(tx[i] * 16.0f);
        pbuf[ndpkt].ui32[1] = (u_int)(int)(ty[i] * 16.0f);
        pbuf[ndpkt].ui32[2] = 0;
        pbuf[ndpkt].ui32[3] = 0;
        if (swch == 0)
        {
            pbuf[ndpkt + 1].ui32[0] = 0x80;
            pbuf[ndpkt + 1].ui32[1] = 0x80;
            pbuf[ndpkt + 1].ui32[2] = 0x80;
        }
        else
        {
            pbuf[ndpkt + 1].iv[0] = i % 255;
            pbuf[ndpkt + 1].iv[1] = i % 255;
            pbuf[ndpkt + 1].iv[2] = i % 255;
        }
        pbuf[ndpkt + 1].ui32[3] = 0x80;
        pbuf[ndpkt + 2].iv[0] = (int)(vtw[i][0] * 16.0f);
        pbuf[ndpkt + 2].iv[1] = (int)(vtw[i][1] * 16.0f);
        pbuf[ndpkt + 2].iv[2] = (int)(vtw[i][2] * 16.0f);
        pbuf[ndpkt + 2].ui32[3] = (i % 33 == 0) ? 0x8000 : 0;

        pbuf[ndpkt + 3].ui32[0] = (u_int)(int)(tx[i + 33] * 16.0f);
        pbuf[ndpkt + 3].ui32[1] = (u_int)(int)(ty[i + 33] * 16.0f);
        pbuf[ndpkt + 3].ui32[2] = 0;
        pbuf[ndpkt + 3].ui32[3] = 0;
        if (swch == 0)
        {
            pbuf[ndpkt + 4].ui32[0] = 0x80;
            pbuf[ndpkt + 4].ui32[1] = 0x80;
            pbuf[ndpkt + 4].ui32[2] = 0x80;
        }
        else
        {
            pbuf[ndpkt + 4].iv[0] = (i + 33) % 255;
            pbuf[ndpkt + 4].iv[1] = (i + 33) % 255;
            pbuf[ndpkt + 4].iv[2] = (i + 33) % 255;
        }
        pbuf[ndpkt + 4].ui32[3] = alp;
        pbuf[ndpkt + 5].iv[0] = (int)(vtw[i + 33][0] * 16.0f);
        pbuf[ndpkt + 5].iv[1] = (int)(vtw[i + 33][1] * 16.0f);
        pbuf[ndpkt + 5].iv[2] = (int)(vtw[i + 33][2] * 16.0f);
        pbuf[ndpkt + 5].ui32[3] = (i % 33 == 0) ? 0x8000 : 0;

        ndpkt += 6;
    }

    EndDmaDirectTrans(pbuf + ndpkt);

    /* PORT: the strip above is inert -- dmaVif1 discards it -- so the same
     * 33x25 grid goes to the renderer as a triangle list.  The strip emits the
     * pair (i, i + 33) at each step and restarts on the first column, so the
     * cell left of column c is bounded by i-1, i, i+32 and i+33 -- the same
     * shape MakePartsDeformPacket uses.
     *
     * vtw is the *deformed* projection and tx/ty the *undeformed* one: the
     * texture stays put while the geometry swings, which is the whole warp.
     * Both are already in the right spaces -- vtw in GS window coordinates
     * (so the 1728/1824 XYOFFSET comes off) and tx/ty in the copy's own
     * 320x448 texel space.
     *
     * ROM ASYMMETRY, reproduced: the top vertex of every pair is given a fixed
     * 0x80 alpha and only the bottom one gets `alp`.  A vertex is emitted twice
     * -- as the bottom of one row band and the top of the next -- so the alpha
     * it carries depends on which band's triangle is using it, not on the
     * vertex itself. */
    {
        static float  xy[24 * 32 * 6 * 2];
        static float  uv[24 * 32 * 6 * 2];
        static u_char rgba[24 * 32 * 6 * 4];
        int nv = 0;

        for (i = 0; i < 792; i++)
        {
            int tri[2][3];
            int band_row = i / 33;
            int t;

            if (i % 33 == 0)
            {
                continue;               /* first column only restarts the strip */
            }

            tri[0][0] = i - 1;  tri[0][1] = i + 32;  tri[0][2] = i;
            tri[1][0] = i + 32; tri[1][1] = i;       tri[1][2] = i + 33;

            for (t = 0; t < 2; t++)
            {
                int c;

                for (c = 0; c < 3; c++)
                {
                    int v = tri[t][c];
                    u_char shade = (u_char)(swch == 0 ? 0x80 : v % 255);

                    xy[nv * 2 + 0] = vtw[v][0] - 1728.0f;
                    xy[nv * 2 + 1] = vtw[v][1] - 1824.0f;
                    uv[nv * 2 + 0] = tx[v];
                    uv[nv * 2 + 1] = ty[v];
                    rgba[nv * 4 + 0] = shade;
                    rgba[nv * 4 + 1] = shade;
                    rgba[nv * 4 + 2] = shade;
                    rgba[nv * 4 + 3] = (v / 33 == band_row) ? 0x80 : alp;
                    nv++;
                }
            }
        }

        MioPan_RendererDrawTexturedTriangles2D(
            (sceGsTex0 *)&deform_tex0, xy, uv, rgba, nullptr, nv, 0, 0, 0);
    }

    eff_deform.type = (u_char)type;
    eff_deform.pass = 1;
}

/* Deform type 4: the same ripple in pure screen space, about the centre of
 * the frame, drawn as horizontal strip rows over the halved work-area copy.
 *                                                            ROM 1236..1360 */
static void SetDeform3(int type, float rate, u_char alp)
{
    static float r;                                         /* sdata 3effa8 */
    static float add = 6.0f;                                /* sdata 3effac */
    static int   swch;                                      /* sdata 3effb0 */

    static float tx[25][33];   /* PORT: 26KB of grids; the ROM stacks them */
    static float ty[25][33];
    static float vt[25][33][4];
    static float vtw[25][33][4];
    Q_WORDDATA *pbuf;
    int   ndpkt;
    int   i;
    int   j;
    int   c;
    u_long deform_tex0;

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            vt[i][j][0] = 2048.0f;
            vt[i][j][1] = 2048.0f;
            vt[i][j][2] = 0.0f;
            vt[i][j][3] = 1.0f;
            tx[i][j] = ((float)j * 320.0f) / 32.0f;
            ty[i][j] = ((float)i * 448.0f) / 24.0f;
            if (j == 0)
            {
                tx[i][0] += 1.0f;
            }
            if (j == 32)
            {
                tx[i][j] -= 1.0f;
            }
            if (i == 0)
            {
                ty[0][j] += 1.0f;
            }
            if (i == 24)
            {
                ty[i][j] -= 1.0f;
            }
        }
    }

    if (eff_deform.init != 0)
    {
        for (i = 0; i < 25; i++)
        {
            float fy = ((float)i * 18.666666f - 224.0f) + -0.5f;

            for (j = 0; j < 33; j++)
            {
                float fx = ((float)j * 20.0f - 320.0f) + -0.5f;

                dw[i][j].lll = sqrtf(fx * fx + fy * fy);
                dw[i][j].rrr = atan2f(fx, fy);
                dw[i][j].mm1 = (dw[i][j].lll * 3.1415925f * 2.0f) / dw[0][0].lll;
                dw[i][j].mm2 = (dw[0][0].lll - dw[i][j].lll) / dw[0][0].lll;
                dw[i][j].sss = sinf(dw[i][j].rrr);
                dw[i][j].ccc = cosf(dw[i][j].rrr);
            }
        }
        eff_deform.init = 0;
    }

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            float lm = sinf(dw[i][j].mm1 - r) * rate * 0.25f * dw[i][j].mm2;

            vtw[i][j][0] = dw[i][j].sss * dw[i][j].lll - dw[i][j].ccc * lm;
            vtw[i][j][1] = dw[i][j].ccc * dw[i][j].lll + dw[i][j].sss * lm;
            vtw[i][j][2] = 0.0f;
        }
    }

    add = 0.2f;
    if (EffWrkStopFlgGet() == 0)
    {
        r += add;
        if (r > 6.283185f)
        {
            r -= 6.283185f;
        }
    }

    {
        DRAW_ENV_5 env;

        env.alpha = 0x8000000044ULL;
        env.tex1 = 0x161;
        env.clamp = 0;
        env.test = 0x30003;
        env.zbuf = 0x10a000118ULL;
        SetDrawEnv(0, &env);
    }

    pbuf = StartDmaDirectTrans();
    Reserve2DPacket(0x10);

    pbuf[0].ul64[0] = 0x1000000000008002ULL;
    pbuf[0].ul64[1] = 0x0e;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = 0x3f;
    deform_tex0 = 0x2000000264016bc0ULL;
    pbuf[2].ul64[0] = deform_tex0;
    pbuf[2].ul64[1] = 0x06;
    if (swch == 0)
    {
        pbuf[3].ul64[0] = 0x30ae400000000000ULL | 0x8630;
    }
    else
    {
        pbuf[3].ul64[0] = 0x30e2400000000000ULL | 0x8630;
    }
    pbuf[3].ul64[1] = 0x431;

    ndpkt = 4;
    for (i = 0; i < 24; i++)
    {
        c = 0;
        for (j = 0; j < 66; j++)
        {
            int row = (j & 1) + i;
            int col = j / 2;

            if (swch == 0)
            {
                pbuf[ndpkt].ui32[0] = 0x80;
                pbuf[ndpkt].ui32[1] = 0x80;
                pbuf[ndpkt].ui32[2] = 0x80;
            }
            else
            {
                pbuf[ndpkt].ui32[0] = c;
                pbuf[ndpkt].ui32[1] = c;
                pbuf[ndpkt].ui32[2] = c;
            }
            pbuf[ndpkt].ui32[3] = alp;

            pbuf[ndpkt + 1].ui32[0] = (u_int)(int)(tx[row][col] * 16.0f);
            pbuf[ndpkt + 1].ui32[1] = (u_int)(int)(ty[row][col] * 16.0f);
            pbuf[ndpkt + 1].ui32[2] = 0;
            pbuf[ndpkt + 1].ui32[3] = 0;

            pbuf[ndpkt + 2].iv[0] = (int)((vt[row][col][0] + vtw[row][col][0]) * 16.0f);
            pbuf[ndpkt + 2].iv[1] = (int)((vt[row][col][1] + vtw[row][col][1]) * 16.0f);
            pbuf[ndpkt + 2].iv[2] = (int)((vt[row][col][2] + vtw[row][col][2]) * 16.0f);
            pbuf[ndpkt + 2].ui32[3] = (j < 2) ? 0x8000 : 0;

            ndpkt += 3;
            c += 1;
        }
    }

    EndDmaDirectTrans(pbuf + ndpkt);

    /* PORT: the strip above is inert -- dmaVif1 discards it -- so the same
     * 25x33 grid goes to the renderer as a triangle list.  Same shape as
     * SetDeform0's, one row wider: the strip walks columns emitting the
     * (row, row+1) pair and the ADC pair restarts each band.
     *
     * Position is vt + vtw, and the split is the point: vt is the flat 2048
     * screen centre and vtw the polar ripple offset about it, so the sum is a
     * GS window coordinate and the 1728/1824 XYOFFSET comes back off as usual.
     * tx/ty are already the copy's own 320x448 texel space.
     *
     * The `swch` debug ramp advances *one* per strip vertex here, where
     * SetDeform0's advances two -- so at (row, col) it is 2 * col + (row - band).
     * Alpha is uniform, unlike SetDeform2's. */
    {
        static float  xy[24 * 32 * 6 * 2];
        static float  uv[24 * 32 * 6 * 2];
        static u_char rgba[24 * 32 * 6 * 4];
        int nv = 0;

        for (i = 0; i < 24; i++)
        {
            for (j = 0; j < 32; j++)
            {
                static const int cell[6][2] =           /* {col, row} offsets */
                {
                    {0, 0}, {0, 1}, {1, 0},
                    {1, 0}, {0, 1}, {1, 1},
                };

                for (c = 0; c < 6; c++)
                {
                    int row = i + cell[c][1];
                    int col = j + cell[c][0];
                    int ramp = 2 * col + cell[c][1];
                    u_char shade = (u_char)(swch == 0 ? 0x80 : ramp);

                    xy[nv * 2 + 0] = vt[row][col][0] + vtw[row][col][0] - 1728.0f;
                    xy[nv * 2 + 1] = vt[row][col][1] + vtw[row][col][1] - 1824.0f;
                    uv[nv * 2 + 0] = tx[row][col];
                    uv[nv * 2 + 1] = ty[row][col];
                    rgba[nv * 4 + 0] = shade;
                    rgba[nv * 4 + 1] = shade;
                    rgba[nv * 4 + 2] = shade;
                    rgba[nv * 4 + 3] = alp;
                    nv++;
                }
            }
        }

        MioPan_RendererDrawTexturedTriangles2D(
            (sceGsTex0 *)&deform_tex0, xy, uv, rgba, nullptr, nv, 0, 0, 0);
    }

    eff_deform.type = (u_char)type;
    eff_deform.pass = 1;
}

/* Deform types 5/6/7: the screen-space ripple as a SCRDEF grid through
 * MakeScrDeformPacket(), with a random shimmer on every vertex.  The three
 * differ in wave count, ST layout and the second axis' chirality.
 *                                                            ROM 1379..1692 */
static void SetDeform4(int type, float rate, u_char alp)
{
    static float r;                                         /* sdata 3effb4 */
    static float add = 6.0f;                                /* sdata 3effb8 */

    static SCRDEF scrdef[25][33];  /* PORT: 26KB; the ROM stacks it */
    float lm;
    int   i;
    int   j;

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            scrdef[i][j].stq[0] = (float)j * 320.0f * 0.03125f;
            scrdef[i][j].stq[1] = ((float)i * 448.0f) / 24.0f;
        }
        scrdef[i][0].stq[0] += 1.0f;
        scrdef[i][32].stq[0] -= 1.0f;
    }
    for (j = 0; j < 33; j++)
    {
        scrdef[0][j].stq[1] += 1.0f;
        scrdef[24][j].stq[1] -= 1.0f;
    }

    lm = sqrtf(152576.0f);          /* sqrt(320^2 + 224^2), the corner */

    if (eff_deform.init != 0)
    {
        for (i = 0; i < 25; i++)
        {
            float fy = ((float)i * 18.666666f - 224.0f) + -0.5f;

            for (j = 0; j < 33; j++)
            {
                float fx = ((float)j * 20.0f - 320.0f) + -0.5f;

                dw[i][j].lll = sqrtf(fx * fx + fy * fy);
                dw[i][j].rrr = atan2f(fx, fy);
                dw[i][j].mm2 = (lm - dw[i][j].lll) * (1.0f / lm);
                dw[i][j].mm1 = dw[i][j].lll * 3.1415925f * 12.0f * (1.0f / lm);
                dw[i][j].sss = sinf(dw[i][j].rrr);
                dw[i][j].ccc = cosf(dw[i][j].rrr);
            }
        }
        eff_deform.init = 0;
    }

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            float disp = sinf(dw[i][j].mm1 - r) * (rate * 0.25f) * dw[i][j].mm2
                         * sinf((r + dw[i][j].rrr) * 18.0f)
                         * (EffectGetRandom(0.0f, 1.0f) + 1.0f);

            scrdef[i][j].vtw[0] = (dw[i][j].sss * dw[i][j].lll
                                   - dw[i][j].ccc * disp) + 2048.0f;
            scrdef[i][j].vtw[1] = (dw[i][j].ccc * dw[i][j].lll
                                   - dw[i][j].sss * disp) + 2048.0f;
            scrdef[i][j].vtw[2] = 0.0f;
            scrdef[i][j].vtw[3] = 0.0f;
        }
    }

    add = 0.02f;
    if (EffWrkStopFlgGet() == 0)
    {
        r += add;
        if (r > 6.283185f)
        {
            r -= 6.283185f;
        }
    }

    MakeScrDeformPacket(32, 24, 0x2000000264016bc0ULL, scrdef, alp);
    eff_deform.type = (u_char)type;
    eff_deform.pass = 1;
}

static void SetDeform5(int type, float rate, u_char alp)
{
    static float r;                                         /* sdata 3effbc */
    static float add = 6.0f;                                /* sdata 3effc0 */

    static SCRDEF scrdef[25][33];  /* PORT: 26KB; the ROM stacks it */
    float lm;
    int   i;
    int   j;

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            scrdef[i][j].stq[0] = (float)j * 10.0f;
            scrdef[i][j].stq[1] = (float)i * 18.666666f;
        }
        scrdef[i][0].stq[0] += 1.0f;
        scrdef[i][32].stq[0] -= 1.0f;
    }
    for (j = 0; j < 33; j++)
    {
        scrdef[0][j].stq[1] += 1.0f;
        scrdef[24][j].stq[1] -= 1.0f;
    }

    lm = sqrtf(152576.0f);

    if (eff_deform.init != 0)
    {
        for (i = 0; i < 25; i++)
        {
            float fy = ((float)i * 18.666666f - 224.0f) + -0.5f;

            for (j = 0; j < 33; j++)
            {
                float fx = ((float)j * 20.0f - 320.0f) + -0.5f;

                dw[i][j].lll = sqrtf(fx * fx + fy * fy);
                dw[i][j].rrr = atan2f(fx, fy);
                dw[i][j].mm2 = (lm - dw[i][j].lll) * (1.0f / lm);
                dw[i][j].mm1 = (dw[i][j].lll * 3.1415925f * 2.0f) * (1.0f / lm);
                dw[i][j].sss = sinf(dw[i][j].rrr);
                dw[i][j].ccc = cosf(dw[i][j].rrr);
            }
        }
        eff_deform.init = 0;
    }

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            float disp = sinf(dw[i][j].mm1 - r) * rate * 0.25f * dw[i][j].mm2
                         * sinf((r + dw[i][j].rrr) * 18.0f)
                         * (EffectGetRandom(0.0f, 1.0f) + 1.0f);

            scrdef[i][j].vtw[0] = (dw[i][j].sss * dw[i][j].lll
                                   - dw[i][j].ccc * disp) + 2048.0f;
            scrdef[i][j].vtw[1] = (dw[i][j].ccc * dw[i][j].lll
                                   - dw[i][j].sss * disp) + 2048.0f;
            scrdef[i][j].vtw[2] = 0.0f;
            scrdef[i][j].vtw[3] = 0.0f;
        }
    }

    add = 0.02f;
    if (EffWrkStopFlgGet() == 0)
    {
        r += add;
        if (r > 6.283185f)
        {
            r -= 6.283185f;
        }
    }

    MakeScrDeformPacket(32, 24, 0x2000000264016bc0ULL, scrdef, alp);
    eff_deform.type = (u_char)type;
    eff_deform.pass = 1;
}

static void SetDeform6(int type, float rate, u_char alp)
{
    static float r;                                         /* sdata 3effc4 */
    static float add = 6.0f;                                /* sdata 3effc8 */

    static SCRDEF scrdef[25][33];  /* PORT: 26KB; the ROM stacks it */
    float lm;
    int   i;
    int   j;

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            scrdef[i][j].stq[0] = (float)j * 320.0f * 0.03125f;
            scrdef[i][j].stq[1] = ((float)i * 448.0f) / 24.0f;
        }
        scrdef[i][0].stq[0] += 1.0f;
        scrdef[i][32].stq[0] -= 1.0f;
    }
    for (j = 0; j < 33; j++)
    {
        scrdef[0][j].stq[1] += 1.0f;
        scrdef[24][j].stq[1] -= 1.0f;
    }

    lm = sqrtf(152576.0f);

    if (eff_deform.init != 0)
    {
        for (i = 0; i < 25; i++)
        {
            float fy = ((float)i * 18.666666f - 224.0f) + -0.5f;

            for (j = 0; j < 33; j++)
            {
                float fx = ((float)j * 20.0f - 320.0f) + -0.5f;

                dw[i][j].lll = sqrtf(fx * fx + fy * fy);
                dw[i][j].rrr = atan2f(fx, fy);
                dw[i][j].mm2 = (lm - dw[i][j].lll) * (1.0f / lm);
                dw[i][j].mm1 = (dw[i][j].lll * 3.1415925f * 2.0f) * (1.0f / lm);
                dw[i][j].sss = sinf(dw[i][j].rrr);
                dw[i][j].ccc = cosf(dw[i][j].rrr);
            }
        }
        eff_deform.init = 0;
    }

    for (i = 0; i < 25; i++)
    {
        for (j = 0; j < 33; j++)
        {
            float disp = sinf(dw[i][j].mm1 - r) * rate * 0.25f * dw[i][j].mm2
                         * sinf((r + dw[i][j].rrr) * 18.0f)
                         * (EffectGetRandom(0.0f, 1.0f) + 1.0f);

            /* The one chirality flip in the family: +s*disp on Y. */
            scrdef[i][j].vtw[0] = (dw[i][j].sss * dw[i][j].lll
                                   - dw[i][j].ccc * disp) + 2048.0f;
            scrdef[i][j].vtw[1] = dw[i][j].ccc * dw[i][j].lll
                                  + dw[i][j].sss * disp + 2048.0f;
            scrdef[i][j].vtw[2] = 0.0f;
            scrdef[i][j].vtw[3] = 0.0f;
        }
    }

    add = 0.02f;
    if (EffWrkStopFlgGet() == 0)
    {
        r += add;
        if (r > 6.283185f)
        {
            r -= 6.283185f;
        }
    }

    MakeScrDeformPacket(32, 24, 0x2000000264016bc0ULL, scrdef, alp);
    eff_deform.type = (u_char)type;
    eff_deform.pass = 1;
}

/* ---- contrast / nega ----------------------------------------------------- */

/* The frame multiplied over itself (ALPHA 0x48 with FIX col): brightens the
 * mids without touching black.                               ROM 1695..1706 */
void SubContrast2(u_char col, u_char alp)
{
    SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 639.89996f, 447.9f,
                     640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
    DISP_SPRT2 ds;

    CopySprDToSpr2(&ds, &sd);
    ds.tex0 = (u_long)((sys_wrk.count & 1) * 0x1180) | 0x2000000268128000ULL;
    ds.z = 0xfff00;
    ds.alpreg = 0x48;
    ds.tex1 = 0x141;
    ds.r = col;
    ds.g = col;
    ds.b = col;
    ds.alp = alp;
    DispSprD2(&ds);
}

void SetContrast2(EFFECT_CONT *ec)                          /* ROM 1714..1717 */
{
    SubContrast2(ec->dat.uc8[2], ec->dat.uc8[3]);
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* The same lift drawn twice, subtractively (ALPHA 0x84).     ROM 1725..1736 */
void SubContrast3(u_char col, u_char alp)
{
    SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 639.89996f, 447.9f,
                     640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
    DISP_SPRT2 ds;

    CopySprDToSpr2(&ds, &sd);
    ds.gftg = 0x144;
    ds.z = 0xfff00;
    ds.alpreg = 0x84;
    ds.r = col;
    ds.g = col;
    ds.b = col;
    ds.alp = alp;
    DispSprD2(&ds);
    DispSprD2(&ds);
}

void SetContrast3(EFFECT_CONT *ec)                          /* ROM 1745..1748 */
{
    SubContrast3(ec->dat.uc8[2], ec->dat.uc8[3]);
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* Colour inversion: subtract the tint, then blend the half-frame copy back
 * in over it by alp2.  Uses the 320-wide work copy at GS 0x16bc.
 *                                                            ROM 1756..1778 */
void SubNega(u_char r, u_char g, u_char b, u_char alp, u_char alp2)
{
    SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 319.9f, 447.9f,
                     640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
    DISP_SPRT2 ds;

    LocalCopyLtoL(3, (sys_wrk.count & 1) * 0x1180, 0x2bc0);

    CopySprDToSpr2(&ds, &sd);
    ds.z = 0xfff00;
    ds.alpreg = 0x84;
    ds.gftg = 0x144;
    ds.r = r;
    ds.g = g;
    ds.b = b;
    ds.alp = alp;
    DispSprD2(&ds);

    ds.r = 0x80;
    ds.g = 0x80;
    ds.b = 0x80;
    ds.alp = (u_char)(0x80 - alp2);
    if ((int)((u_int)alp2 << 24) < 0)
    {
        ds.alp = 0;
    }
    ds.alpreg = 0x44;
    ds.test = 0x30003;
    ds.gftg = 0x154;
    ds.tex0 = 0x2000000264116bc0ULL;
    DispSprD2(&ds);
}

void SetNega(EFFECT_CONT *ec)                               /* ROM 1790..1817 */
{
    u_char col;
    u_char alp;
    u_char alp2;

    alp2 = 0;
    col = ec->dat.uc8[2];
    alp = ec->dat.uc8[3];

    if ((ec->dat.uc8[1] & 4) != 0)
    {
        if (ec->flow == 0)
        {
            alp2 = (u_char)((int)(ec->cnt * 128) / (int)ec->in);
        }
        else if (ec->flow == 1)
        {
            alp2 = 0x80;
        }
        else if (ec->flow == 2)
        {
            alp2 = (u_char)((int)((ec->out - ec->cnt) * 128) / (int)ec->out);
        }
        else if (ec->flow == 3)
        {
            ResetEffects(ec);
        }
        EffInKeepOutFlowCtrl(ec);
    }
    else
    {
        alp2 = *(u_char *)ec->pnt[0];
    }

    SubNega(col, col, col, alp, alp2);
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

void *CallNega2(int in, int keep, int out)                  /* ROM 1833 */
{
    return SetEffects_NEGA(4, 0x40, 0xc4,
                           (u_int)in, (u_int)keep, (u_int)out, nullptr);
}

void *CallNega(int time)                                    /* ROM 1834..1839 */
{
    return CallNega2(0, time, 0);
}

/* --------------------------------------------------------------------------
 *  Overlap: the previous scene held on screen and cross-faded into the new
 *  one over uc8[2] frames.  A camera jump of more than 128 units on any axis
 *  (or an approach-camera handoff) recaptures the picture into the EE-side
 *  stash; while alp runs down, the stash blends back over the live frame.
 *                                                            ROM 1840..1945
 * ------------------------------------------------------------------------ */
void SetOverRap(EFFECT_CONT *ec)
{
    static float cx;                                        /* sdata 3effcc */
    static float cy;                                        /* sdata 3effd0 */
    static float cz;                                        /* sdata 3effd4 */
    static float alp;                                       /* sdata 3effd8 */

    float *cpos;
    float  x, y, z;
    float  fn;
    int    fl;

    fl = 0;
    overlap_passflg[0] = 1;

    cpos = gra3dcamGetPosition();
    x = cpos[0];
    y = cpos[1];
    z = cpos[2];

    if (fabs(cx - x) > 128.0)
    {
        fl = 1;
    }
    else if (fabs(cy - y) > 128.0)
    {
        fl = 1;
    }
    else if (fabs(cz - z) > 128.0)
    {
        fl = 1;
    }

    if (GetApproachCameraCrossFade() != 0)
    {
        fl = 1;
        ApproachCameraCrossFadeSW(0);
    }

    cx = x;
    cy = y;
    cz = z;

    if ((fl != 0 && alp <= 0.0f) || overlap_passflg[0] != overlap_passflg[1])
    {
        /* Grab the frame just displayed and halve it into the EE stash. */
        LocalCopyLtoB(0, 0, (sys_wrk.count + 1 & 1) * 0x1180);
        EffImageHalf32((u_int *)MioPan_GetHostPointer(0x1e79b00), 640, 448);
        alp = 128.0f;
    }

    if (alp == 128.0f)
    {
        /* First frame: the old picture still owns the screen outright. */
        SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 639.89996f, 447.9f,
                         640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
        DISP_SPRT2 ds;

        {
            DRAW_ENV_5 env;

            env.alpha = 0x8000000011ULL;
            env.tex1 = 0x100000161ULL;
            env.clamp = 5;
            env.test = 0x30003;
            env.zbuf = 0x10a000118ULL;
            SetDrawEnv(0, &env);
        }

        CopySprDToSpr2(&ds, &sd);
        ds.z = 0xfff00;
        ds.alpreg = 0x44;
        ds.tex1 = 0x141;
        ds.tex0 = (u_long)((sys_wrk.count + 1 & 1) * 0x1180) | 0x2000000268128000ULL;
        DispSprD2(&ds);
    }
    else if (alp > 0.0f)
    {
        /* Cross-fade: upload the stash into the work area and blend it. */
        SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 319.9f, 447.9f,
                         640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
        DISP_SPRT2 ds;

        LocalCopyBtoL(2, 0, 0x2bc0);

        CopySprDToSpr2(&ds, &sd);
        ds.tex0 = 0x2000000264116bc0ULL;
        ds.z = 0xfff00;
        ds.alpreg = ((u_long)((u_int)alp & 0xff) << 32) | 0x64;
        DispSprD2(&ds);
    }

    if (ec->dat.uc8[2] == 0)
    {
        fn = 8.0f;
    }
    else
    {
        fn = 128.0f / (float)ec->dat.uc8[2];
    }
    alp -= fn;
    if (alp <= 0.0f)
    {
        alp = 0.0f;
    }

    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* --------------------------------------------------------------------------
 *  Depth-of-field fake: two darkening copies of the frame laid at the Z of
 *  points 1.5x and 2.2x along the camera->target ray, so geometry beyond the
 *  focus plane picks up one or both.                         ROM 1954..2005
 * ------------------------------------------------------------------------ */
void SetForcusDepth(EFFECT_CONT *ec)
{
    float bai[2] = { 1.5f, 2.1999998f };                    /* rdata blob */
    int   zi[2];
    float wlm[4][4];
    float slm[4][4];
    float vt[4];
    float vtww[4];
    sceVu0IVECTOR ivec;
    GRA3DCAMERA  *pCam;
    float *cam_pos;
    int   i;

    memset(vt, 0, sizeof(vt));
    vt[3] = 1.0f;

    pCam = gra3dGetCamera();
    cam_pos = gra3dcamGetPosition();

    for (i = 0; i < 2; i++)
    {
        vtww[0] = (pCam->vTarget[0] - cam_pos[0]) * bai[i] + cam_pos[0];
        vtww[1] = (pCam->vTarget[1] - cam_pos[1]) * bai[i] + cam_pos[1];
        vtww[2] = (pCam->vTarget[2] - cam_pos[2]) * bai[i] + cam_pos[2];
        vtww[3] = 1.0f;

        sceVu0UnitMatrix(wlm);
        wlm[0][0] = wlm[1][1] = wlm[2][2] = 25.0f;
        sceVu0TransMatrix(wlm, wlm, vtww);
        sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);
        sceVu0RotTransPers(ivec, slm, vt, 0);
        zi[i] = ivec[2];
    }

    {
        SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 639.89996f, 447.9f,
                         640.0f, 448.0f, -0.5f, 0.0f, 0xa0, 0x80 };
        DISP_SPRT2 ds;

        CopySprDToSpr2(&ds, &sd);
        ds.zbuf = 0xa000118;        /* Z writes ON: the copies stack */
        ds.test = 0x5000d;
        ds.tex0 = (u_long)((sys_wrk.count & 1) * 0x1180) | 0x2000000268128000ULL;

        ds.alp = 0x40;
        ds.z = zi[1];
        DispSprD2(&ds);

        ds.alp = 0x28;
        ds.z = zi[0];
        DispSprD2(&ds);
    }

    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* ---- dither -------------------------------------------------------------- */

/* Regenerates the 128x128 random pattern only when the maxima change (and
 * one frame late, so a slider drag does not rebuild it every frame). */
static void MakeDitherPattern(u_int alpmx, u_int colmx)     /* ROM 2013..2027 */
{
    if (alpmx < 0x100 && colmx < 0x100)
    {
        if (MakeDitherPatternCtrl.OldAlpha == alpmx &&
            MakeDitherPatternCtrl.OldColor == colmx)
        {
            if (MakeDitherPatternCtrl.MakeFlg != 0)
            {
                MakeDitherPatternCtrl.MakeFlg = 0;
                MakeRDither3((u_char)alpmx, (u_char)colmx);
            }
            if (MakeDitherPatternCtrl.OldAlpha == alpmx &&
                MakeDitherPatternCtrl.OldColor == colmx)
            {
                MakeDitherPatternCtrl.OldAlpha = (u_char)alpmx;
                MakeDitherPatternCtrl.OldColor = (u_char)colmx;
                return;
            }
        }
        MakeDitherPatternCtrl.MakeFlg = 1;
        MakeDitherPatternCtrl.OldAlpha = (u_char)alpmx;
        MakeDitherPatternCtrl.OldColor = (u_char)colmx;
    }
}

/* 128x128 8-bit noise (alpha up to alpmx) plus its 256-entry grey CLUT
 * (up to colmx), loaded to GS 0x2200 / 0x3ffc.               ROM ~2035..2057 */
static void MakeRDither3(u_char alpmx, u_char colmx)
{
    static sceGsLoadImage gs_limage1;                       /* bss 473660 */
    static sceGsLoadImage gs_limage2;                       /* bss 4736c0 */

    /* PORT: 16KB pattern; the ROM stacks it. */
    static u_char pat[16384];
    static u_int  pal[256];
    int    i;
    float  f;

    for (i = 0; i < 16384; i++)
    {
        f = (float)alpmx * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF);
        pat[i] = (u_char)(u_int)f;
    }
    for (i = 0; i < 256; i++)
    {
        u_int c;

        f = (float)colmx * ((float)MioPan_Rand() / MIOPAN_RAND_MAXF);
        c = (u_int)f & 0xff;
        pal[i] = ((u_int)i << 24) | c | (c << 8) | (c << 16);
    }

    /* PSMT8H, not PSMT8 -- the ROM's own `li a3,0x1b` at 0x163278, and the
     * only format that agrees with the TEX0 SubDither3() draws under.  An
     * 8-bit transfer in an "H" format lands each index in the top byte of a
     * PSMCT32 word, so the sheet occupies a 64KB span rather than a packed
     * 16KB one -- which is the point: 0x2200..0x2300 is the tail of frame
     * buffer 1, and a PSMCT24 buffer never touches the byte the pattern
     * lives in.  Uploaded as plain PSMT8 the noise went into the packed T8
     * buffer instead, so only a quarter of the texels the sampler reads had
     * been written and the rest resolved to index 0, i.e. alpha 0.  Measured
     * against the host GS helper: mean texel alpha 15.9 with a quarter of
     * the sheet non-zero, against 63.3 and effectively all of it here. */
    sceGsSetDefLoadImage(&gs_limage1, 0x2200, 2, SCE_GS_PSMT8H, 0, 0, 128, 128);
    sceGsSetDefLoadImage(&gs_limage2, 0x3ffc, 1, SCE_GS_PSMCT32, 0, 0, 16, 16);
    FlushCache(0);
    sceGsExecLoadImage(&gs_limage1, (u_long128 *)pat);
    sceGsExecLoadImage(&gs_limage2, (u_long128 *)pal);
    g3dGsSyncPath(0, 0);
}

/* --------------------------------------------------------------------------
 *  Three copies of the noise sheet scrolled against the camera pan, alphas
 *  phased 120 degrees apart -- the film-grain crawl.  Types recolour or
 *  change the blend: 1 normal, 2 additive, 3/4/5 tinted, 6/7 the darker
 *  pair.                                                     ROM ~2065..2201
 * ------------------------------------------------------------------------ */
void SubDither3(int type, float alp, float spd, u_char alpmx, u_char colmx)
{
    static float old_cam_i[4];                              /* data 2fd540 */
    static float cnf;                                       /* sdata 3effe8 */
    static float cx;                                        /* sdata 3effec */
    static float cy;                                        /* sdata 3efff0 */
    static int   fl = 1;                                    /* sdata 3efff4 */

    SPRT_DAT2 sd = { 0, 0.0f, 0.0f, 0.0f, 0.0f,
                     640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
    DISP_SPRT2 ds;
    GRA3DCAMERA *pCam;
    u_long alpreg;
    u_char r, g, b;
    float  tx, ty;
    float  otx, oty;
    float  mvx, mvy;
    float  fa;

    pCam = gra3dGetCamera();

    /* PORT: the host toggle sits beside the ROM's own dither switch rather
       than replacing it, so an event script that raises dith_off still works
       and neither flag can be surprised by the other.  Returning here is what
       the ROM's switch does too -- before MakeDitherPattern(), so the noise
       sheet is not rebuilt either -- and SetDither3() has already run the
       effect's in/keep/out flow, so its slot is still released on time.
       Note this also takes the grain out of effect_oth.c's door-seal
       dissolve, which draws through this same function. */
    if (EffWrkDithOffGet() != 0 ||
        MioPan_RendererGetFilmGrain() == MIOPAN_FILM_GRAIN_OFF)
    {
        return;
    }

    /* The finder freezes the reference target so the grain does not slide
     * while the player aims; a camera cut re-seeds it. */
    if (PlayerModeIsFinder() != 0)
    {
        if (fl == 0)
        {
            g3dxVu0CopyVector(old_cam_i, pCam->vTarget);
        }
        fl = 1;
    }
    else
    {
        if (fl == 1 || CamChangeCheck() != 0)
        {
            g3dxVu0CopyVector(old_cam_i, pCam->vTarget);
        }
        fl = 0;
    }

    r = 0x80;
    g = 0x80;
    b = 0x80;
    alpreg = 0x44;
    switch (type)
    {
    case 1:
        alpreg = 0x44;
        break;
    case 2:
        alpreg = 0x48;
        break;
    case 3:
        alpreg = 0x41;
        break;
    case 4:
        g = 0;
        b = 0x80;
        alpreg = 0x41;
        break;
    case 5:
        g = 0x80;
        b = 0;
        alpreg = 0x41;
        break;
    case 6:
        alpreg = 0x49;
        break;
    case 7:
        alpreg = 0x42;
        break;
    }

    MakeDitherPattern(alpmx, colmx);

    GetCamI2DPos(pCam->vTarget, &tx, &ty);
    GetCamI2DPos(old_cam_i, &otx, &oty);
    g3dxVu0CopyVector(old_cam_i, pCam->vTarget);

    if (tx < -2048.0f)
    {
        tx = 0.0f;
    }
    if (ty < -2048.0f)
    {
        ty = 0.0f;
    }
    if (otx < -2048.0f)
    {
        otx = 0.0f;
    }
    if (oty < -2048.0f)
    {
        oty = 0.0f;
    }
    if (tx > 2048.0f)
    {
        tx = 0.0f;
    }
    if (ty > 2048.0f)
    {
        ty = 0.0f;
    }
    if (otx > 2048.0f)
    {
        otx = 0.0f;
    }
    if (oty > 2048.0f)
    {
        oty = 0.0f;
    }

    if (isnan(tx) == 0 && isnan(ty) == 0 && isnan(otx) == 0 && isnan(oty) == 0)
    {
        mvx = (tx - otx) * 0.19999999f;
        mvy = (ty - oty) * 0.099999994f;
    }
    else
    {
        mvx = 0.0f;
        mvy = 0.0f;
    }

    if (EffWrkStopFlgGet() == 0)
    {
        cx = (cx + mvx) - (float)((int)((cx + mvx) * 0.0078125f) << 7);
        cy = (cy + mvy) - (float)((int)((cy + mvy) * 0.0078125f) << 7);
        while (cx > 128.0f)
        {
            cx -= 128.0f;
        }
        while (cx < 0.0f)
        {
            cx += 128.0f;
        }
        while (cy > 128.0f)
        {
            cy -= 128.0f;
        }
        while (cy < 0.0f)
        {
            cy += 128.0f;
        }
    }

    /* PORT: the grain tiles (CLAMP is REPEAT here), so widening the quad alone
       would stretch the noise rather than show more of it.  Scaling the UV span
       by the same factor keeps the texel density the ROM chose and just lays
       down more tiles across the wider frame.  Both are 1.0 on a 4:3 window, so
       the spans stay the ROM's 640 and 512 there. */
    float uspan, vspan;

    EffScrFullScreenRect(&sd);
    {
        float ext_x, ext_y;

        MioPan_RendererGetViewExtend(&ext_x, &ext_y);
        uspan = 640.0f * ext_x;
        vspan = 512.0f * ext_y;
    }

    CopySprDToSpr2(&ds, &sd);
    ds.tex0 = 0x2007ff85ddb0a200ULL;    /* the 128x128 PSMT8 + CLUT 0x3ffc */
    ds.tex1 = 0x141;
    ds.z = 0xfff00;
    ds.zbuf = 0x10a000118ULL;
    ds.clmp = 0;
    ds.alpreg = alpreg;
    ds.r = 0x80;
    ds.g = g;
    ds.b = b;
    (void)r;

    /* PORT: in NATIVE mode the three quads below come off a host sheet at one
       texel per output pixel instead of the ROM's 128x128 one.  Nothing about
       the draws themselves changes -- same packet, same blend, same UVs, same
       three phased alphas -- the renderer rescales the UVs against the bigger
       sheet.  The GS sheet is still built above either way: PS2 mode draws from
       it, and MakeRDither3() has to keep consuming its own 16640 samples out of
       MioPan_Rand() or every later draw from that stream moves. */
    MioPan_RendererFilmGrainBegin(alpmx, colmx);

    ds.u1 = (u_short)(u_int)(cx * 16.0f);
    ds.v1 = (u_short)(u_int)(cy * 16.0f);
    ds.u2 = (u_short)(u_int)((cx + uspan) * 16.0f);
    ds.v2 = (u_short)(u_int)((cy + vspan) * 16.0f);
    fa = sinf((cnf * 3.1415925f) / 180.0f) * alp + alp;
    ds.alp = (u_char)(u_int)fa;
    DispSprD2(&ds);

    ds.u1 = (u_short)(u_int)((cx + 64.0f) * 16.0f);
    ds.v1 = (u_short)(u_int)(cy * 16.0f);
    ds.u2 = (u_short)(u_int)((cx + 64.0f + uspan) * 16.0f);
    ds.v2 = (u_short)(u_int)((cy + vspan) * 16.0f);
    fa = sinf(((cnf + 120.0f) * 3.1415925f) / 180.0f) * alp + alp;
    ds.alp = (u_char)(u_int)fa;
    DispSprD2(&ds);

    ds.u1 = (u_short)(u_int)(cx * 16.0f);
    ds.v1 = (u_short)(u_int)((cy + 64.0f) * 16.0f);
    ds.u2 = (u_short)(u_int)((cx + uspan) * 16.0f);
    ds.v2 = (u_short)(u_int)((cy + 64.0f + vspan) * 16.0f);
    fa = sinf(((cnf + 240.0f) * 3.1415925f) / 180.0f) * alp + alp;
    ds.alp = (u_char)(u_int)fa;
    DispSprD2(&ds);

    MioPan_RendererFilmGrainEnd();

    if (EffWrkStopFlgGet() == 0)
    {
        cnf += spd;
    }
}

void SetDither3(EFFECT_CONT *ec)                            /* ROM 2212..2243 */
{
    int    type;
    float  alp;
    float  spd;
    u_char alpmx;
    u_char colmx;

    type = ec->dat.uc8[2];
    alp = ec->dat.fl32[2];
    spd = ec->dat.fl32[3];
    alpmx = ec->dat.uc8[3];
    colmx = ec->dat.uc8[4];

    if ((ec->dat.uc8[1] & 4) != 0)
    {
        if (ec->flow == 0)
        {
            alp = (alp * (float)ec->cnt) / (float)(int)ec->in;
        }
        else if (ec->flow == 1)
        {
            /* keep: alp stays as armed */
        }
        else if (ec->flow == 2)
        {
            alp = (alp * (float)(ec->out - ec->cnt)) / (float)(int)ec->out;
        }
        else
        {
            alp = 0.0f;
            if (ec->flow == 3)
            {
                ResetEffects(ec);
            }
        }
        EffInKeepOutFlowCtrl(ec);
    }

    if (type < 8)
    {
        SubDither3(type, alp, spd, alpmx, colmx);
    }
    else
    {
        SubDither4(alp, spd, alpmx, colmx);
    }

    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

/* The type-8 variant: no finder freeze, doubled parallax, swapped axes.
 *                                                            ROM ~2255..2333 */
static void SubDither4(float alp, float spd, int alpmx, int colmx)
{
    static float old_cam_i[4];                              /* data 2fd550 */
    static float cnf;                                       /* sdata 3efff8 */
    static float cx;                                        /* sdata 3efffc */
    /* The odd initialiser is the ROM's own .sdata word; together with the
     * -1024 clamp below it is parked debug logic that never fires. */
    static float cy = -9908638.0f;                          /* sdata 3f0000 */
    static int   fl = 1;                                    /* sdata 3f0004 */

    SPRT_DAT2 sd = { 0, 0.0f, 0.0f, 0.0f, 0.0f,
                     640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
    DISP_SPRT2 ds;
    GRA3DCAMERA *pCam;
    float  tx, ty;
    float  otx, oty;
    float  mvx, mvy;
    float  fa;

    pCam = gra3dGetCamera();

    /* PORT: type 8 has no dith_off test of its own in the ROM -- that is the
       original's own asymmetry, and it is left alone -- so the host toggle
       stands by itself here. */
    if (MioPan_RendererGetFilmGrain() == MIOPAN_FILM_GRAIN_OFF)
    {
        return;
    }

    if (!(cy - 1024.0f <= -9908638.0f))
    {
        cy = cy - 1024.0f;
    }

    if (fl == 1 || CamChangeCheck() != 0)
    {
        g3dxVu0CopyVector(old_cam_i, pCam->vTarget);
    }
    fl = 0;

    GetCamI2DPos(pCam->vTarget, &tx, &ty);
    GetCamI2DPos(old_cam_i, &otx, &oty);
    g3dxVu0CopyVector(old_cam_i, pCam->vTarget);

    if (isnan(tx) == 0 && isnan(ty) == 0 && isnan(otx) == 0 && isnan(oty) == 0)
    {
        mvx = (tx - otx) * 0.19999999f;
        mvy = (ty - oty) * 0.099999994f;
    }
    else
    {
        mvx = 0.0f;
        mvy = 0.0f;
    }

    cx = cx + mvx + mvx;
    if (cx > 128.0f)
    {
        cx -= 128.0f;
    }
    else if (cx < 0.0f)
    {
        cx += 128.0f;
    }
    cy = cy + mvy + mvy;
    if (cy > 128.0f)
    {
        cy -= 128.0f;
    }
    else if (cy < 0.0f)
    {
        cy += 128.0f;
    }

    MakeDitherPattern(alpmx, colmx);

    /* PORT: same as SubDither3 -- the grain tiles, so the UV span grows with
       the quad instead of the noise being stretched across it. */
    float uspan, vspan;

    EffScrFullScreenRect(&sd);
    {
        float ext_x, ext_y;

        MioPan_RendererGetViewExtend(&ext_x, &ext_y);
        uspan = 640.0f * ext_x;
        vspan = 512.0f * ext_y;
    }

    CopySprDToSpr2(&ds, &sd);
    ds.tex1 = 0x141;
    ds.zbuf = 0x10a000118ULL;
    ds.z = 0xfff00;
    ds.tex0 = 0x2007ff85ddb0a200ULL;
    ds.clmp = 0;

    /* PORT: in NATIVE mode the three quads below come off a host sheet at one
       texel per output pixel instead of the ROM's 128x128 one.  Nothing about
       the draws themselves changes -- same packet, same blend, same UVs, same
       three phased alphas -- the renderer rescales the UVs against the bigger
       sheet.  The GS sheet is still built above either way: PS2 mode draws from
       it, and MakeRDither3() has to keep consuming its own 16640 samples out of
       MioPan_Rand() or every later draw from that stream moves. */
    MioPan_RendererFilmGrainBegin(alpmx, colmx);

    ds.u1 = (u_short)(u_int)(cx * 16.0f);
    ds.v1 = (u_short)(u_int)(cy * 16.0f);
    ds.u2 = (u_short)(u_int)((cx + uspan) * 16.0f);
    ds.v2 = (u_short)(u_int)((cy + vspan) * 16.0f);
    fa = sinf((cnf * 3.1415925f) / 180.0f) * alp + alp;
    ds.alp = (u_char)(u_int)fa;
    DispSprD2(&ds);

    ds.u1 = (u_short)(u_int)((cx + 64.0f) * 16.0f);
    ds.v1 = (u_short)(u_int)(cy * 16.0f);
    ds.u2 = (u_short)(u_int)((cx + 64.0f + uspan) * 16.0f);
    ds.v2 = (u_short)(u_int)((cy + vspan) * 16.0f);
    fa = sinf(((cnf + 120.0f) * 3.1415925f) / 180.0f) * alp + alp;
    ds.alp = (u_char)(u_int)fa;
    DispSprD2(&ds);

    ds.u1 = (u_short)(u_int)(cx * 16.0f);
    ds.v1 = (u_short)(u_int)((cy + 64.0f) * 16.0f);
    ds.u2 = (u_short)(u_int)((cx + uspan) * 16.0f);
    ds.v2 = (u_short)(u_int)((cy + 64.0f + vspan) * 16.0f);
    fa = sinf(((cnf + 240.0f) * 3.1415925f) / 180.0f) * alp + alp;
    ds.alp = (u_char)(u_int)fa;
    DispSprD2(&ds);

    MioPan_RendererFilmGrainEnd();

    if (EffWrkStopFlgGet() == 0)
    {
        cnf += spd;
    }
}

/* The vignette frame: effdat[0x4a] blended over the border. ROM 2341..2350 */
void SubFadeFrame(u_char alp, u_int pri)
{
    SPRT_DAT2 sd = { 0, 0.099999994f, 0.099999994f, 0.0f, 0.0f,
                     640.0f, 448.0f, -0.5f, -0.5f, 0xa0, 0x80 };
    DISP_SPRT2 ds;

    (void)pri;

    /* PORT: a vignette that stops at the pillarbox leaves the widened sides
       unshaded and a hard edge down each one.  The UVs are left alone, so the
       gradient stretches to the window -- which is what a vignette wants. */
    EffScrFullScreenRect(&sd);

    CopySprDToSpr2(&ds, &sd);
    ds.u1 = 0;
    ds.v1 = 0;
    ds.u2 = (u_short)(u_int)((float)effdat[0x4a].w * 16.0f);
    ds.v2 = (u_short)(u_int)((float)effdat[0x4a].h * 16.0f);
    ds.tex0 = effdat[0x4a].tex0;
    ds.alp = alp;
    DispSprD2(&ds);
}

void SetFadeFrame(EFFECT_CONT *ec)                          /* ROM 2356..2359 */
{
    SubFadeFrame(ec->dat.uc8[2], ec->dat.ui32[1]);
    if ((ec->dat.uc8[1] & 1) != 0)
    {
        ResetEffects(ec);
    }
}

void ChangeMonochrome(int sw)                               /* ROM 2377 */
{
    (void)sw;
}

/* ---- the screen saver ---------------------------------------------------- */

static void ScreenSaverInit(void)                           /* ROM 2386..2391 */
{
    memset(&ScreenSaverCtrl, 0, sizeof(ScreenSaverCtrl));
    ScreenSaverCtrl.LoadStatus = 0;
    ScreenSaverCtrl.PlayId = -1;
    ScreenSaverCtrl.LoadId = -1;
}

/* One float of the crimson butterfly: random size, spot and alpha envelope.
 *                                                            ROM ~2420..2459 */
static void ScreenSaverOneTexInit(SCREEN_SAVER_TEX *pTex, int DispTime)
{
    pTex->Scale = EffectGetRandom(1.93f, 1.93f + 0.67999995f);
    pTex->BasePosX = (int)EffectGetRandom(0.0f, 590.0f);
    pTex->PosX = pTex->BasePosX;
    pTex->BasePosY = (int)EffectGetRandom(0.0f, 398.0f);
    pTex->PosY = pTex->BasePosY;
    pTex->DispTime = DispTime;
    pTex->DispTimeAll = DispTime;
    pTex->AlphaInTime = (int)EffectGetRandom(5.0f, 67.0f);
    pTex->AlphaKeepTime = (int)EffectGetRandom(3.0f, 40.0f);
    pTex->AlphaOutTime = (int)EffectGetRandom(10.0f, 46.0f);
    pTex->Alpha = 0;
}

/* Odd chapters use the alternate sheet. */
static int ScreenSaverTexFileNoGet(void)                    /* ROM ~2670..2674 */
{
    if ((ingame_wrk.mChapterNo.Get() & 1) == 0)
    {
        return 0x15;
    }
    return 0x14;
}

/* --------------------------------------------------------------------------
 *  150 seconds of no input starts the screen saver: load the butterfly
 *  sheet and its cue bank, then float the butterfly around and pulse the
 *  dither, chirping every few hundred frames.  Any input tears it down.
 *                                                            ROM ~2480..2745
 * ------------------------------------------------------------------------ */
static void ScreenSaverMain(void)
{
    int i;

    if ((u_int)*key_now[0] + (u_int)*key_now[7] + (u_int)*key_now[3] +
        (u_int)*key_now[5] + (u_int)*key_now[1] + (u_int)*key_now[2] +
        (u_int)*key_now[4] + (u_int)*key_now[6] + (u_int)*key_now[8] +
        (u_int)*key_now[9] + (u_int)*key_now[10] + (u_int)*key_now[11] +
        (u_int)*key_now[13] + (u_int)*key_now[12] +
        (u_int)((u_int)(pad[0].analog[0] - 0x45) < 0x77 ? 0 : 1) +
        (u_int)((u_int)(pad[0].analog[1] - 0x45) < 0x77 ? 0 : 1) +
        (u_int)((u_int)(pad[0].analog[2] - 0x45) < 0x77 ? 0 : 1) +
        (u_int)((u_int)(pad[0].analog[3] - 0x45) < 0x77 ? 0 : 1) == 0)
    {
        ScreenSaverCtrl.Counter++;
    }
    else
    {
        ScreenSaverCtrl.Counter = 0;
    }

    if (ScreenSaverCtrl.LoadStatus == 0)
    {
        if (ScreenSaverCtrl.Counter > 8999)
        {
            ScreenSaverCtrl.Counter = 9000;
            ScreenSaverCtrl.pTexBuf =
                (u_int *)EFFECT_MALLOC(GetFileSize(ScreenSaverTexFileNoGet()));
            if (ScreenSaverCtrl.pTexBuf != nullptr)
            {
                ScreenSaverCtrl.LoadId =
                    LoadReq(ScreenSaverTexFileNoGet(),
                            (uintptr_t)ScreenSaverCtrl.pTexBuf);
                if (SSC_BankNo == -1)
                {
                    SSC_BankNo = SndBankNew(0xd17, 0xd16, -1);
                }
                ScreenSaverCtrl.LoadStatus = 1;
            }
        }
    }
    else if (ScreenSaverCtrl.LoadStatus == 1)
    {
        if (IsLoadEnd(ScreenSaverCtrl.LoadId) != 0 &&
            SndBankIsReady(SSC_BankNo) != 0)
        {
            ScreenSaverCtrl.ScreenEffectNo = EffectGetScreenEffectNo();
            ScreenSaverCtrl.IntervalTime = 0;
            ScreenSaverCtrl.DispTime = (int)EffectGetRandom(16.0f, 174.0f);
            ScreenSaverOneTexInit(&ScreenSaverCtrl.TexData[0],
                                  ScreenSaverCtrl.DispTime);
            ScreenSaverCtrl.LoadStatus = 2;
            ScreenSaverCtrl.DitherAlpha = 42.0f;
            ScreenSaverCtrl.DitherChangeTime = 0;
            ScreenSaverCtrl.DitherInterval = (int)EffectGetRandom(150.0f, 390.0f);
        }
    }
    else if (ScreenSaverCtrl.LoadStatus == 2)
    {
        if (ScreenSaverCtrl.Counter < 9000)
        {
            /* Input arrived: free everything and go back to counting. */
            EFFECT_FREE(ScreenSaverCtrl.pTexBuf);
            if (ScreenSaverCtrl.PlayId != -1 &&
                SndBufIsPlaying(ScreenSaverCtrl.PlayId) != 0)
            {
                SndBufStop(ScreenSaverCtrl.PlayId);
            }
            ScreenSaverCtrl.pTexBuf = nullptr;
            ScreenSaverCtrl.PlayId = -1;
            ScreenSaverCtrl.LoadStatus = 0;
            ScreenSaverCtrl.LoadId = -1;
        }
        else
        {
            ScreenSaverCtrl.Counter = 9000;

            for (i = 0; i < 1; i++)
            {
                SCREEN_SAVER_TEX *pTex = &ScreenSaverCtrl.TexData[i];

                if (pTex->DispTime != 0)
                {
                    int   done = pTex->DispTimeAll - pTex->DispTime;
                    float a;

                    pTex->PosX = (int)((float)pTex->BasePosX
                                       + EffectGetRandom(0.0f, 10.0f));
                    pTex->PosY = (int)((float)pTex->BasePosY
                                       + EffectGetRandom(0.0f, 6.0f));

                    if (done < pTex->AlphaInTime)
                    {
                        if (pTex->AlphaInTime == 0)
                        {
                            a = 1.0f;
                        }
                        else
                        {
                            a = (float)done / (float)pTex->AlphaInTime;
                        }
                    }
                    else if (done < pTex->AlphaInTime + pTex->AlphaKeepTime)
                    {
                        a = 1.0f;
                    }
                    else
                    {
                        int outbase = pTex->AlphaInTime + pTex->AlphaKeepTime;

                        a = 0.0f;
                        if (done < outbase + pTex->AlphaOutTime &&
                            pTex->AlphaOutTime != 0)
                        {
                            a = 1.0f - (float)(done - outbase)
                                       / (float)pTex->AlphaOutTime;
                        }
                    }
                    pTex->DispTime--;
                    pTex->Alpha = (int)((a * 2688.0f) / 100.0f);
                }
            }

            if (ScreenSaverCtrl.DispTime == 0)
            {
                if (ScreenSaverCtrl.IntervalTime == 0 ||
                    --ScreenSaverCtrl.IntervalTime == 0)
                {
                    ScreenSaverCtrl.DispTime = (int)EffectGetRandom(16.0f, 174.0f);
                    ScreenSaverOneTexInit(&ScreenSaverCtrl.TexData[0],
                                          ScreenSaverCtrl.DispTime);
                }
            }
            else
            {
                ScreenSaverCtrl.DispTime--;
                if (ScreenSaverCtrl.DispTime == 0)
                {
                    ScreenSaverCtrl.IntervalTime = (int)EffectGetRandom(8.0f, 216.0f);
                }
            }

            if (ScreenSaverCtrl.DitherChangeTime == 0)
            {
                if (ScreenSaverCtrl.DitherInterval == 0 ||
                    --ScreenSaverCtrl.DitherInterval == 0)
                {
                    ScreenSaverCtrl.DitherChangeTime = 30;
                    ScreenSaverCtrl.PlayId =
                        SndBankPlay(SSC_BankNo, 0, 1, 0, 0x3200, 0x1000, 0,
                                    nullptr);
                }
                ScreenSaverCtrl.DitherAlpha = 42.0f;
            }
            else
            {
                int t;
                float a;

                ScreenSaverCtrl.DitherChangeTime--;
                if (ScreenSaverCtrl.DitherChangeTime == 0)
                {
                    ScreenSaverCtrl.DitherInterval = (int)EffectGetRandom(150.0f, 390.0f);
                }

                t = 30 - ScreenSaverCtrl.DitherChangeTime;
                if (t < 5)
                {
                    a = (float)t / 5.0f;
                }
                else if (t <= 24)
                {
                    a = 1.0f;
                }
                else if (t < 30)
                {
                    a = 1.0f - (float)(5 - ScreenSaverCtrl.DitherChangeTime) / 5.0f;
                }
                else
                {
                    a = 0.0f;
                }
                ScreenSaverCtrl.DitherAlpha = a * 85.0f + 42.0f;
            }
        }
    }
}

static void ScreenSaverSetDITHER(EFFECT_CONT *ec, int DitherType, float Alpha,
                                 float Speed, int AlphaMax, int ColorMax)
{                                                           /* ROM 2751..2757 */
    ec->dat.uc8[0] = 2;
    ec->dat.uc8[1] = 2;
    ec->dat.uc8[2] = (u_char)DitherType;
    ec->dat.fl32[2] = Alpha;
    ec->dat.fl32[3] = Speed;
    ec->dat.uc8[3] = (u_char)AlphaMax;
    ec->dat.uc8[4] = (u_char)ColorMax;
}

void ScreenSaverDraw(void)                                  /* ROM ~2770..2812 */
{
    SPRT_DAT    sprt_dat;
    DISP_SPRT   DispSprt;
    EFFECT_CONT EffectCont;

    sprt_dat.tex0 = 0x2005980221312bc0ULL;
    sprt_dat.u = 0;
    sprt_dat.v = 0;
    sprt_dat.w = 0x100;
    sprt_dat.h = 0x100;
    sprt_dat.x = 0;
    sprt_dat.y = 0;
    sprt_dat.pri = 0;
    sprt_dat.alpha = 0x80;
    sprt_dat.flip = 0;
    sprt_dat.bln = 0;

    if (ScreenSaverCtrl.LoadStatus == 2)
    {
        SetSprFile((uintptr_t)ScreenSaverCtrl.pTexBuf);

        if (ScreenSaverCtrl.TexData[0].DispTime != 0)
        {
            CopySprDToSpr(&DispSprt, &sprt_dat);
            DispSprt.csx = (float)ScreenSaverCtrl.TexData[0].PosX;
            DispSprt.csy = (float)ScreenSaverCtrl.TexData[0].PosY;
            DispSprt.x = (float)(ScreenSaverCtrl.TexData[0].PosX
                                 - (sprt_dat.w >> 1));
            DispSprt.y = (float)(ScreenSaverCtrl.TexData[0].PosY
                                 - (sprt_dat.h >> 1));
            DispSprt.pri = 0x10;
            DispSprt.tex1 = 0x161;
            DispSprt.z = 0xfffef;
            DispSprt.zbuf = 0x10a000118ULL;
            DispSprt.alphar = 0x48;
            DispSprt.alpha = (u_char)ScreenSaverCtrl.TexData[0].Alpha;
            DispSprt.scw = ScreenSaverCtrl.TexData[0].Scale;
            DispSprt.sch = ScreenSaverCtrl.TexData[0].Scale;
            DispSprD(&DispSprt);
        }

        /* Pulse the saver's own dither over whatever look is armed. */
        if ((u_int)(EffectGetScreenEffectParamPtr(5)->Dither - 1) < 8)
        {
            ScreenSaverSetDITHER(&EffectCont, 8, ScreenSaverCtrl.DitherAlpha,
                                 8.0f, 0x40, 0x32);
            SetDither3(&EffectCont);
        }
    }
}

/* Moved here from effect.c: effect_scr.o's own last export (0x165108).  The
 * option screen's brightness slider, applied as a whole-frame add or
 * subtract every frame.                                      ROM 2821..2838 */
void BrightnessAdjustmentFilterDraw(void)
{
    DISP_SQAR DispSqar;
    SQAR_DAT  SqarDat;
    int       BrightnessFilterAlpha;

    memset(&SqarDat, 0, sizeof(SqarDat));
    BrightnessFilterAlpha = opt_wrk.brightness;
    SqarDat.w = 640;
    SqarDat.h = 448;
    CopySqrDToSqr(&DispSqar, &SqarDat);
    DispSqar.test = 0x30003;
    if (BrightnessFilterAlpha <= 0x80)
    {
        DispSqar.alpha = (u_char)(0x80 - BrightnessFilterAlpha);
        DispSqar.alphar = 0x44;
    }
    else
    {
        DispSqar.alpha = (u_char)(BrightnessFilterAlpha * 2 - 1);
        DispSqar.alphar = 0x49;
    }
    DispSqrD(&DispSqar);
}
