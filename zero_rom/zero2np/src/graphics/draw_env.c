// FILE: /home/zero_rom/zero2np/src/graphics/draw_env.c
//
// GS draw-environment manager.  The GS has two drawing contexts, each with its
// own ALPHA / TEST / ZBUF / TEX1 / CLAMP / SCISSOR register, plus the shared
// TEXA register.  Every write goes out as a GIF A+D packet built in the VIF1
// ring buffer: context_packet_start[ctx]() opens a packet, the register values
// are laid down as (DATA, ADDR) quadword pairs behind a GIFtag, and
// context_packet_end[ctx]() commits it.  main.c wires both contexts to
// dmaVif1GetPacketFLUSH_DIRECT / dmaVif1SetPacketFLUSH_DIRECT.
//
// Alongside the packet, every setter caches the value it wrote in draw_env_*[],
// which is what GET_*_REGISTER() reads back -- the GS itself is write-only, so
// a caller wanting to save/restore a register (loading.c does this with SCISSOR)
// depends on these shadows.
//
// Port note: the host has no GS, and nothing walks the DIRECT packets this file
// emits, so the packets are built faithfully but are inert.  What drives the
// host renderer is a second copy of each value, forwarded from the SET_*
// helpers -- the single point every path funnels through, so ClearDrawEnv(),
// SetDrawEnv(), SetDrawEnvNoTex() and the individual setters are all covered:
//
//   TEST     alpha test          -> discard in the fragment shader
//   ALPHA    blend equation      -> a pipeline blend-state variant
//   ZBUF     ZMSK only           -> depth writes on/off
//   SCISSOR  clip box            -> SDL_SetGPUScissor
//
// TEX1 and CLAMP are not forwarded.  The renderer already picks its filtering
// and address mode per draw from values the caller hands it, and those paths
// carry decisions the register alone does not -- MapSky's dome clamps even
// though CLAMP says REPEAT, because tiling its soft-edged page puts a seam at
// every boundary.  TEXA is a texture-decode input (alpha expansion for the
// 16- and 24-bit texel formats), so it belongs to the upload path, not here.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "draw_env.h"

#include "../sdk/libgraph.h"                     // SCE GS register numbers / value builders
#include "miopan/rendering/miopan_renderer.h"    // host GS draw-environment shadows

// ──────────────────────────────────────────────────────────────────────
// Per-context packet allocator / committer, installed by InitDrawEnv().

/* sdata 3efba8 */ qword *(*context_packet_start[2])(void);
/* sdata 3efbb0 */ void   (*context_packet_end[2])(qword *);

// ──────────────────────────────────────────────────────────────────────
// Write-back shadows of the registers, indexed by context.

/* bss 423168 */ static u_long draw_env_alpha[2];
/* bss 423178 */ static u_long draw_env_test[2];
/* bss 423188 */ static u_long draw_env_zbuf[2];
/* bss 423198 */ static u_long draw_env_tex1[2];
/* bss 4231a8 */ static u_long draw_env_clamp[2];
/* bss 4231b8 */ static u_long draw_env_scissor[2];
/* sbss 3f4b60 */ static u_long draw_env_texa;

/* The SET_*_REGISTER() helpers write one A+D pair into an already-open packet.
 * They are defined near the bottom of the file, after the functions that only
 * call them, which is why ClearDrawEnv() / SetDrawEnv() / SetDrawEnvNoTex()
 * call them out of line while the single-register setters below inline them. */
static void SET_ALPHA_REGISTER(u_long *base, int context_no, u_long alpha);
static void SET_TEST_REGISTER(u_long *base, int context_no, u_long test);
static void SET_ZBUF_REGISTER(u_long *base, int context_no, u_long zbuf);
static void SET_TEX1_REGISTER(u_long *base, int context_no, u_long tex1);
static void SET_CLAMP_REGISTER(u_long *base, int context_no, u_long clamp);
static void SET_SCISSOR_REGISTER(u_long *base, int context_no, u_long scissor);
static void SET_TEXA_REGISTER(u_long *base, u_long texa);

// ──────────────────────────────────────────────────────────────────────
// Lifecycle.

/* ---------------------------------------------------------------------------
 *  Reset both contexts to the engine's default draw environment: source-alpha
 *  blending, alpha test on GREATER 0 (throw away fully transparent fragments),
 *  bilinear filtering, depth test on GEQUAL, and no clamping / scissoring.
 *  Then clear the shared TEXA in a packet of its own, because TEXA has no
 *  per-context twin and so does not fit the six-register loop above.
 * ------------------------------------------------------------------------ */
void ClearDrawEnv(void)
{
    int    i;
    qword *base;

    for (i = 0; i < 2; i++)                                             /* 68 */
    {
        base = context_packet_start[i]();                               /* 70 */

        SET_ALPHA_REGISTER((u_long *)&base[1], i,
                           SCE_GS_SET_ALPHA(0, 1, 0, 1, 0));            /* 73 */
        SET_TEST_REGISTER((u_long *)&base[2], i,
                          SCE_GS_SET_TEST(1, 6, 0, 0, 0, 0, 1, 2));     /* 75 */
        SET_TEX1_REGISTER((u_long *)&base[3], i,
                          SCE_GS_SET_TEX1(1, 0, 1, 5, 0, 0, 0));        /* 77 */
        /* Clamp, zbuf and scissor all clear to zero.  main.c installs the real
         * scissor box from the sceGsDrawEnv right after init_super(); zero on
         * its own is a 1x1 box that would clip the whole screen away. */
        SET_CLAMP_REGISTER((u_long *)&base[4], i, 0);                   /* 79 */

        SET_ZBUF_REGISTER((u_long *)&base[5], i, 0);                    /* 82 */
        SET_SCISSOR_REGISTER((u_long *)&base[6], i, 0);                 /* 84 */

        *(u_long *)base       = SCE_GIF_SET_TAG(6, 1, 0, 0, 0, 1);      /* 87 */
        *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                      /* 88 */
        context_packet_end[i](base + 7);                                /* 89 */
    }                                                                   /* 90 */

    base = context_packet_start[0]();                                   /* 94 */
    SET_TEXA_REGISTER((u_long *)&base[1], 0);                           /* 95 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 98 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 99 */
    context_packet_end[0](base + 2);                                    /* 100 */
}

/* ---------------------------------------------------------------------------
 *  Install the per-context packet allocator / committer pair and lay down the
 *  default environment.  Called once from init_super().
 * ------------------------------------------------------------------------ */
void InitDrawEnv(qword *(*c1_start)(void), qword *(*c2_start)(void),
                 void (*c1_end)(qword *), void (*c2_end)(qword *))
{
    context_packet_start[0] = c1_start;                                 /* 106 */
    context_packet_start[1] = c2_start;                                 /* 107 */
    context_packet_end[0]   = c1_end;                                   /* 108 */
    context_packet_end[1]   = c2_end;                                   /* 109 */
    ClearDrawEnv();                                                     /* 110 */
}

// ──────────────────────────────────────────────────────────────────────
// Whole-environment push.

/* ---------------------------------------------------------------------------
 *  Push all five textured-draw registers in one five-register A+D packet.
 *
 *  Note the emission order -- alpha, tex1, clamp, test, zbuf -- which is the
 *  DRAW_ENV_5 member order, not the order ClearDrawEnv() uses.
 *
 *  new_env is dereferenced without a null check; that is the ROM's behaviour.
 * ------------------------------------------------------------------------ */
void SetDrawEnv(int context_no, const DRAW_ENV_5 *new_env)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 118 */

    SET_ALPHA_REGISTER((u_long *)&base[1], context_no, new_env->alpha);  /* 125 */
    SET_TEX1_REGISTER((u_long *)&base[2], context_no, new_env->tex1);    /* 131 */
    SET_CLAMP_REGISTER((u_long *)&base[3], context_no, new_env->clamp);  /* 137 */
    SET_TEST_REGISTER((u_long *)&base[4], context_no, new_env->test);    /* 143 */
    SET_ZBUF_REGISTER((u_long *)&base[5], context_no, new_env->zbuf);    /* 149 */

    *(u_long *)base       = SCE_GIF_SET_TAG(5, 1, 0, 0, 0, 1);          /* 168 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 169 */
    context_packet_end[context_no](base + 6);                           /* 171 */
}

/* ---------------------------------------------------------------------------
 *  The untextured three-register variant: alpha, test, zbuf.
 * ------------------------------------------------------------------------ */
void SetDrawEnvNoTex(int context_no, const DRAW_ENV_NOTEX *new_env)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 179 */

    SET_ALPHA_REGISTER((u_long *)&base[1], context_no, new_env->alpha);  /* 186 */
    SET_TEST_REGISTER((u_long *)&base[2], context_no, new_env->test);    /* 192 */
    SET_ZBUF_REGISTER((u_long *)&base[3], context_no, new_env->zbuf);    /* 198 */

    *(u_long *)base       = SCE_GIF_SET_TAG(3, 1, 0, 0, 0, 1);          /* 216 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 217 */
    context_packet_end[context_no](base + 4);                           /* 219 */
}

// ──────────────────────────────────────────────────────────────────────
// Single-register access.  Each register gets a getter that reads the shadow,
// a static helper that fills one A+D pair, and a public setter that wraps the
// helper in a one-register packet.

u_long GET_ALPHA_REGISTER(int context_no)                                 /* 227 */
{
    return draw_env_alpha[context_no];                                  /* 228 */
}

void SetAlphaRegister(int context_no, u_long alpha)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 244 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 246 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 247 */

    SET_ALPHA_REGISTER((u_long *)&base[1], context_no, alpha);

    context_packet_end[context_no](base + 2);                           /* 251 */
}

u_long GET_TEST_REGISTER(int context_no)                                  /* 257 */
{
    return draw_env_test[context_no];                                   /* 258 */
}

void SetTestRegister(int context_no, u_long test)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 273 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 275 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 276 */

    SET_TEST_REGISTER((u_long *)&base[1], context_no, test);

    context_packet_end[context_no](base + 2);                           /* 280 */
}

u_long GET_ZBUF_REGISTER(int context_no)                                  /* 286 */
{
    return draw_env_zbuf[context_no];                                   /* 287 */
}

void SetZbufRegister(int context_no, u_long zbuf)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 302 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 304 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 305 */

    SET_ZBUF_REGISTER((u_long *)&base[1], context_no, zbuf);

    context_packet_end[context_no](base + 2);                           /* 309 */
}

u_long GET_TEX1_REGISTER(int context_no)                                  /* 315 */
{
    return draw_env_tex1[context_no];                                   /* 316 */
}

void SetTex1Register(int context_no, u_long tex1)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 331 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 333 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 334 */

    SET_TEX1_REGISTER((u_long *)&base[1], context_no, tex1);

    context_packet_end[context_no](base + 2);                           /* 338 */
}

u_long GET_CLAMP_REGISTER(int context_no)                                 /* 344 */
{
    return draw_env_clamp[context_no];                                  /* 345 */
}

void SetClampRegister(int context_no, u_long clamp)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 360 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 362 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 363 */

    SET_CLAMP_REGISTER((u_long *)&base[1], context_no, clamp);

    context_packet_end[context_no](base + 2);                           /* 367 */
}

u_long GET_SCISSOR_REGISTER(int context_no)                               /* 373 */
{
    return draw_env_scissor[context_no];                                /* 374 */
}

void SetScissorRegister(int context_no, u_long scissor)
{
    qword *base;

    base = context_packet_start[context_no]();                          /* 389 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 391 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 392 */

    SET_SCISSOR_REGISTER((u_long *)&base[1], context_no, scissor);

    context_packet_end[context_no](base + 2);                           /* 396 */
}

u_long GET_TEXA_REGISTER(void)                                            /* 402 */
{
    return draw_env_texa;                                               /* 403 */
}

/* TEXA is shared between the contexts, so it always goes out on context 0. */
void SetTexaRegister(u_long texa)
{
    qword *base;

    base = context_packet_start[0]();                                   /* 415 */

    *(u_long *)base       = SCE_GIF_SET_TAG(1, 1, 0, 0, 0, 1);          /* 417 */
    *((u_long *)base + 1) = SCE_GIF_PACKED_AD;                          /* 418 */

    SET_TEXA_REGISTER((u_long *)&base[1], texa);

    context_packet_end[0](base + 2);                                    /* 422 */
}

// ──────────────────────────────────────────────────────────────────────
// A+D pair writers.  base[0] is the register value, base[1] the register
// number; the context selects between the _1 and _2 twin.

static void SET_ALPHA_REGISTER(u_long *base, int context_no, u_long alpha)  /* 231 */
{
    draw_env_alpha[context_no] = alpha;                                 /* 232 */
    base[0] = alpha;                                                    /* 233 */
    if (context_no == 0) { base[1] = SCE_GS_ALPHA_1; }                   /* 234 */
    else                 { base[1] = SCE_GS_ALPHA_2; }                   /* 236 */

    /* Port: the blend equation.  The host bakes blending into its pipelines,
     * so the renderer classifies (A-B)*C+D into the shapes it can express and
     * keeps the result as the current blend mode. */
    MioPan_RendererSetGsAlphaRegister((unsigned long long)alpha);
}

static void SET_TEST_REGISTER(u_long *base, int context_no, u_long test)   /* 261 */
{
    draw_env_test[context_no] = test;                                   /* 262 */
    base[0] = test;                                                     /* 263 */
    if (context_no == 0) { base[1] = SCE_GS_TEST_1; }                    /* 264 */
    else                 { base[1] = SCE_GS_TEST_2; }                    /* 266 */

    /* Port: the funnel every TEST write passes through, and the only draw_env
     * register the host renderer models.  Without the alpha test, cut-out
     * textures (foliage, fences, hair) blend the colour stored behind the
     * cut-out into every silhouette edge instead of discarding it.  Only the
     * alpha-test half is forwarded; ZTE/ZTST stay with the pipeline's own depth
     * state, and the two contexts collapse into one shadow because the host
     * renderer has no second context. */
    MioPan_RendererSetGsTestRegister((unsigned long long)test);
}

static void SET_ZBUF_REGISTER(u_long *base, int context_no, u_long zbuf)   /* 290 */
{
    draw_env_zbuf[context_no] = zbuf;                                   /* 291 */
    base[0] = zbuf;                                                     /* 292 */
    if (context_no == 0) { base[1] = SCE_GS_ZBUF_1; }                    /* 293 */
    else                 { base[1] = SCE_GS_ZBUF_2; }                    /* 295 */

    /* Port: only ZMSK is used, to keep a transparent pass from writing depth
     * and occluding whatever is drawn behind it later in the frame. */
    MioPan_RendererSetGsZbufRegister((unsigned long long)zbuf);
}

static void SET_TEX1_REGISTER(u_long *base, int context_no, u_long tex1)   /* 319 */
{
    draw_env_tex1[context_no] = tex1;                                   /* 320 */
    base[0] = tex1;                                                     /* 321 */
    if (context_no == 0) { base[1] = SCE_GS_TEX1_1; }                    /* 322 */
    else                 { base[1] = SCE_GS_TEX1_2; }                    /* 324 */
}

static void SET_CLAMP_REGISTER(u_long *base, int context_no, u_long clamp) /* 348 */
{
    draw_env_clamp[context_no] = clamp;                                 /* 349 */
    base[0] = clamp;                                                    /* 350 */
    if (context_no == 0) { base[1] = SCE_GS_CLAMP_1; }                   /* 351 */
    else                 { base[1] = SCE_GS_CLAMP_2; }                   /* 353 */
}

static void SET_SCISSOR_REGISTER(u_long *base, int context_no, u_long scissor) /* 377 */
{
    draw_env_scissor[context_no] = scissor;                             /* 378 */
    base[0] = scissor;                                                  /* 379 */
    if (context_no == 0) { base[1] = SCE_GS_SCISSOR_1; }                 /* 380 */
    else                 { base[1] = SCE_GS_SCISSOR_2; }                 /* 382 */

    /* Port: the clip box, which the renderer maps into swapchain pixels. */
    MioPan_RendererSetGsScissorRegister((unsigned long long)scissor);
}

static void SET_TEXA_REGISTER(u_long *base, u_long texa)                   /* 406 */
{
    draw_env_texa = base[0] = texa;                                     /* 407 */
    base[1] = SCE_GS_TEXA;                                              /* 408 */
}
