// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_spr.c
//
// COMPLETE.  Both ZERO2.MAP .text symbols (0x001652b0 SetEffSQITex,
// 0x001655b0 SetEffSQTex); the object's other three entries are the
// fixed_array boilerplate GCC emits into every object that instantiates the
// template.  The module has no static data beyond SetEffSQTex()'s function
// static (bss 0x4784d0) and its init guard.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x001651d8.

#include "effect_spr.h"

#include "effect.h"                             /* EffWrkMonochroModeGet / g_bInterlace */
#include "effect_pak.h"                         /* Reserve2DPacket */

#include "../draw_env.h"                        /* DRAW_ENV_5 / SetDrawEnv */
#include "../graph2d/g2d_draw.h"                /* Q_WORDDATA / StartDmaDirectTrans */
#include "../graph2d/graph2d.h"                 /* effdat[] */
#include "../../miopan/rendering/miopan_renderer.h"
#include "../../sdk/sce_gs.h"                   /* sceGsTex0 / sceGsTex1 */

/* --------------------------------------------------------------------------
 *  PORT-ONLY.  The DIRECT packet both functions build is faithful but inert --
 *  dmaVif1 collects DIRECT packets and never executes them -- so the quad also
 *  goes out through the host bridge.  GS window coordinates are 1/16 pixel
 *  biased by 2048 (ScreenX()/ScreenY() in graphics.c build them that way), and
 *  MioPan_RendererDrawTexturedQuad() wants top-left-origin screen pixels, so
 *  2048 comes off and the half-screen bias goes on.  UVs are 1/16 texel.
 *
 *  Corner order is the ROM's triangle strip: i & 1 picks the x pair and i / 2
 *  the y pair, i.e. 0 = TL, 1 = TR, 2 = BL, 3 = BR -- which is the winding the
 *  bridge expects.
 * ------------------------------------------------------------------------ */
static void EffSQHostQuad(int n, int x0, int x1, int y0, int y1,
                          int tw, int th,
                          u_char r, u_char g, u_char b, u_char a)
{
    u_long tex1 = 0x161;                     /* the env both callers set */
    float  xy[8];
    float  uv[8];
    int    i;

    for (i = 0; i < 4; i++)
    {
        xy[i * 2 + 0] = (float)((i % 2) ? x1 : x0) / 16.0f - 2048.0f + 320.0f;
        xy[i * 2 + 1] = (float)((i / 2) ? y1 : y0) / 16.0f - 2048.0f + 224.0f;
        uv[i * 2 + 0] = (float)((i % 2) ? tw - 8 : 8) / 16.0f;
        uv[i * 2 + 1] = (float)((i / 2) ? th - 8 : 8) / 16.0f;
    }

    MioPan_RendererDrawTexturedQuad((const sceGsTex0 *)&effdat[n].tex0,
                                    (const sceGsTex1 *)&tex1,
                                    xy, uv, r, g, b, a);
}

/* --------------------------------------------------------------------------
 *  Integer-position effect quad.                                    ROM 215
 *
 *  No guard-band test: the callers hand this one positions they have already
 *  placed on screen.  The GS state is a stack DRAW_ENV_5 built per call, so
 *  the blend mode really does follow `tp` (contrast SetEffSQTex below).
 * ------------------------------------------------------------------------ */
void SetEffSQITex(int n, int *v, int tp, float w, float h,
                  u_char r, u_char g, u_char b, u_char a)
{
    DRAW_ENV_5  env;
    Q_WORDDATA *pbuf;
    u_long      tx0;
    u_char      rr, gg, bb;
    int         xx[2];
    int         yy[2];
    int         tw, th;
    int         ndpkt;
    int         i;
    float       div;

    xx[0] = v[0] - (int)(w * 16.0f);                                /* 225 */
    xx[1] = v[0] + (int)(w * 16.0f);                                /* 226 */

    /* Interlaced fields are half height, so a sprite asked for `h` pixels has
     * to be drawn half as tall to keep its aspect. */
    div = g_bInterlace ? 2.0f : 1.0f;                               /* 227 */

    yy[0] = v[1] - (int)((h / div) * 16.0f);                        /* 228 */
    yy[1] = v[1] + (int)((h / div) * 16.0f);                        /* 229 */

    if (EffWrkMonochroModeGet() != 0)                               /* 231 */
    {
        /* Note this is the correct (r + g + b) / 3, unlike the sibling
         * expression in Set3DPosTexure() where the /3 binds to b alone. */
        rr = gg = bb = (u_char)(((int)r + (int)g + (int)b) / 3);    /* 232 */
    }
    else
    {
        rr = r;                                                     /* 234 */
        gg = g;                                                     /* 235 */
        bb = b;                                                     /* 236 */
    }

    /* The ROM's line notes put the two blend-factor ternaries on 239 and 240
     * and every one of the five env stores on 247, and stab neither `env` nor
     * either ternary result -- i.e. one statement spanning 239..247.  Written
     * out per member here; the emitted code is the same.
     *
     * ALPHA is (A - B) * C + D with A = Cs and C = As, so B/D of 1 select Cd
     * and 2 select 0: tp == 0 is the ordinary lerp, bit 1 makes it additive. */
    env.alpha = ((u_long)((tp & 2) ? 2 : 1) << 2) |                 /* 239 */
                ((u_long)((tp & 4) ? 2 : 1) << 6);                  /* 240 */
    env.tex1  = 0x161;                       /* LCM fixed, MMAG/MMIN linear */
    env.clamp = 0;                                                  /* 247 */
    env.test  = 0x5000d;                     /* ATE GEQUAL 0, ZTE GEQUAL */
    env.zbuf  = ((u_long)(tp & 1) << 32) | 0x0a000118; /* PSMZ24, ZMSK = tp&1 */

    /* Straight effdat[n]: no + EffWrkMonochroModeGet() here, which is why the
     * desaturation above has to happen in the vertex colour. */
    tx0 = effdat[n].tex0;                                           /* 242 */
    tw  = effdat[n].w * 16;                                         /* 243 */
    th  = effdat[n].h * 16;                                         /* 244 */

    SetDrawEnv(0, &env);                                            /* 254 */

    /* PORT: host draw, queued alongside the inert DIRECT packet below. */
    EffSQHostQuad(n, xx[0], xx[1], yy[0], yy[1], tw, th, rr, gg, bb, a);

    pbuf = StartDmaDirectTrans();                                   /* 257 */
    Reserve2DPacket(0x10);                                          /* 258 */

    /* A+D tag for two register writes: TEXFLUSH, then this quad's TEX0. */
    pbuf[0].ul64[0] = 0x1000000000008002ULL;                        /* 260 */
    pbuf[0].ul64[1] = 0x0e;                                         /* 261 */
    pbuf[1].ul64[0] = 0;                                            /* 263 */
    pbuf[1].ul64[1] = 0x3f;                                         /* 264 */
    pbuf[2].ul64[0] = tx0;                                          /* 266 */
    pbuf[2].ul64[1] = 0x06;                                         /* 267 */

    /* PRIM: triangle strip, textured, flat, alpha blend, FST (UV) mode. */
    pbuf[3].ul64[0] = 0x30aa400000008004ULL;                        /* 269 */
    pbuf[3].ul64[1] = 0x431;                 /* REGS: RGBAQ, UV, XYZF2 */  /* 270 */

    ndpkt = 4;
    for (i = 0; i < 4; i++)                                         /* 272 */
    {
        pbuf[ndpkt].ui32[0] = (u_int)rr;                            /* 273 */
        pbuf[ndpkt].ui32[1] = (u_int)gg;                            /* 274 */
        pbuf[ndpkt].ui32[2] = (u_int)bb;                            /* 275 */
        pbuf[ndpkt].ui32[3] = (u_int)a;                             /* 276 */

        /* Half a texel in from each edge, so bilinear never reaches a
         * neighbouring sprite in the shared effect texture page. */
        pbuf[ndpkt + 1].ui32[0] = (i % 2) ? (u_int)(tw - 8) : 8;    /* 278 */
        pbuf[ndpkt + 1].ui32[1] = (i / 2) ? (u_int)(th - 8) : 8;    /* 279 */
        pbuf[ndpkt + 1].ui32[2] = 0;                                /* 280 */
        pbuf[ndpkt + 1].ui32[3] = 0;                                /* 281 */

        pbuf[ndpkt + 2].iv[0] = xx[i % 2];                          /* 283 */
        pbuf[ndpkt + 2].iv[1] = yy[i / 2];                          /* 284 */
        pbuf[ndpkt + 2].iv[2] = v[2];                               /* 285 */
        /* ADC: the first two vertices only prime the strip. */
        pbuf[ndpkt + 2].ui32[3] = (i < 2) ? 0x8000 : 0;             /* 286 */

        ndpkt += 3;
    }                                                               /* 287 */

    EndDmaDirectTrans(pbuf + ndpkt);                                /* 290 */
}

/* --------------------------------------------------------------------------
 *  Float-position effect quad.                                      ROM 298
 * ------------------------------------------------------------------------ */
void SetEffSQTex(int n, float *v, int tp, float w, float h,
                 u_char r, u_char g, u_char b, u_char a)
{
    Q_WORDDATA *pbuf;
    u_long      tx0;
    u_char      rr, gg, bb;
    int         xx[4];
    int         yy[4];
    int         tw, th;
    int         ndpkt;
    int         ClipFlg;
    int         div;
    int         i;

    xx[0] = xx[2] = (int)((v[0] - w) * 16.0f);                      /* 307 */
    xx[1] = xx[3] = (int)((v[0] + w) * 16.0f);                      /* 308 */

    div = g_bInterlace ? 2 : 1;                                     /* 309 */

    yy[0] = yy[1] = (int)((v[1] - h / (float)div) * 16.0f);         /* 310 */
    yy[2] = yy[3] = (int)((v[1] + h / (float)div) * 16.0f);         /* 311 */

    ClipFlg = 0;                                                    /* 314 */

    /* Guard band: GCC folds each `< lo || > hi` pair into one biased unsigned
     * compare, so a negative coordinate wraps high and is caught too. */
    for (i = 0; i < 4; i++)                                         /* 315 */
    {
        if (xx[i] < 0x4000 || xx[i] > 0xc000)                       /* 316 */
        {
            ClipFlg = 1;
        }
        if (yy[i] < 0x4000 || yy[i] > 0xc000)                       /* 317 */
        {
            ClipFlg = 1;
        }
    }                                                               /* 318 */

    if ((int)(v[2] * 16.0f) < 0xff || (int)(v[2] * 16.0f) > 0xfffffff) /* 319 */
    {
        ClipFlg = 1;
    }

    if (ClipFlg != 0)                                               /* 320 */
    {
        return;
    }

    if (EffWrkMonochroModeGet() != 0)                               /* 324 */
    {
        rr = gg = bb = (u_char)(((int)r + (int)g + (int)b) / 3);    /* 325 */
    }
    else
    {
        rr = r;                                                     /* 327 */
        gg = g;                                                     /* 328 */
        bb = b;                                                     /* 329 */
    }

    tx0 = effdat[n].tex0;                                           /* 332 */
    tw  = effdat[n].w * 16;                                         /* 333 */
    th  = effdat[n].h * 16;                                         /* 334 */

    /* ROM BUG, reproduced.  This is a *function static* with a runtime
     * initialiser, so GCC guards it: the whole struct -- ZBUF's ZMSK included
     * -- is built from whichever `tp` reached here first, and every later call
     * silently inherits it.  The guard variable is the ROM's own (sdata, read
     * through gp-0x57d0); only the ZMSK bit actually varies, so in practice
     * the first caller decides whether effect quads write depth. */
    static DRAW_ENV_5 env =                                         /* 343 */
    {
        0x44,                                /* ALPHA: (Cs-Cd)*As+Cd */
        0x161,
        0,
        0x5000d,
        ((u_long)tp << 32) | 0x0a000118
    };

    SetDrawEnv(0, &env);                                            /* 344 */

    /* PORT: host draw, queued alongside the inert DIRECT packet below.
     * xx[0]==xx[2] and xx[1]==xx[3] (likewise yy), so the pair form the
     * helper takes describes this quad exactly. */
    EffSQHostQuad(n, xx[0], xx[1], yy[0], yy[2], tw, th, rr, gg, bb, a);

    pbuf = StartDmaDirectTrans();                                   /* 347 */
    Reserve2DPacket(0x10);                                          /* 348 */

    pbuf[0].ul64[0] = 0x1000000000008002ULL;                        /* 350 */
    pbuf[0].ul64[1] = 0x0e;                                         /* 351 */
    pbuf[1].ul64[0] = 0;                                            /* 353 */
    pbuf[1].ul64[1] = 0x3f;                                         /* 354 */
    pbuf[2].ul64[0] = tx0;                                          /* 356 */
    pbuf[2].ul64[1] = 0x06;                                         /* 357 */

    pbuf[3].ul64[0] = 0x30aa400000008004ULL;                        /* 359 */
    pbuf[3].ul64[1] = 0x431;                                        /* 360 */

    ndpkt = 4;
    for (i = 0; i < 4; i++)                                         /* 362 */
    {
        pbuf[ndpkt].ui32[0] = (u_int)rr;                            /* 363 */
        pbuf[ndpkt].ui32[1] = (u_int)gg;                            /* 364 */
        pbuf[ndpkt].ui32[2] = (u_int)bb;                            /* 365 */
        pbuf[ndpkt].ui32[3] = (u_int)a;                             /* 366 */

        pbuf[ndpkt + 1].ui32[0] = (i % 2) ? (u_int)(tw - 8) : 8;    /* 368 */
        pbuf[ndpkt + 1].ui32[1] = (i / 2) ? (u_int)(th - 8) : 8;    /* 369 */
        pbuf[ndpkt + 1].ui32[2] = 0;                                /* 370 */
        pbuf[ndpkt + 1].ui32[3] = 0;                                /* 371 */

        /* Unlike SetEffSQITex(), xx/yy here are per-vertex rather than a
         * two-entry pair, so the strip order is baked into the arrays. */
        pbuf[ndpkt + 2].iv[0] = xx[i];                              /* 373 */
        pbuf[ndpkt + 2].iv[1] = yy[i];                              /* 374 */
        pbuf[ndpkt + 2].iv[2] = (int)(v[2] * 16.0f);                /* 375 */
        pbuf[ndpkt + 2].ui32[3] = (i < 2) ? 0x8000 : 0;             /* 376 */

        ndpkt += 3;
    }                                                               /* 377 */

    EndDmaDirectTrans(pbuf + ndpkt);                                /* 379 */
}
