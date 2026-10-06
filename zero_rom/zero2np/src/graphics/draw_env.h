/* ==========================================================================
 *  graphics/draw_env.h
 *
 *  Public interface for the GS draw-environment manager (draw_env.c).  The two
 *  drawing contexts each own an alpha / test / zbuf / tex1 / clamp / scissor
 *  register set; ClearDrawEnv() resets them, the Set*Register() setters patch a
 *  single register (rebuilding the context's DMA env packet), and SetDrawEnv()
 *  / SetDrawEnvNoTex() push a whole DRAW_ENV_5 / DRAW_ENV_NOTEX override at once.
 *  SetTexaRegister() sets the shared TEXA (alpha-expansion) register.
 *
 *  DRAW_ENV_5 / DRAW_ENV_NOTEX are the canonical engine types (types.txt); the
 *  local copies previously carried in g2d_draw.c and message.h are mirrors of
 *  these.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_DRAW_ENV_H
#define _GRAPHICS_DRAW_ENV_H

#include "eetypes.h"
#include "../sdk/scetypes.h"        /* qword */

/* Textured draw-env override (0x28): alpha / tex1 / clamp / test / zbuf. */
typedef struct _DRAW_ENV_5          /* 0x28 */
{
    u_long alpha;                   /* 0x00 */
    u_long tex1;                    /* 0x08 */
    u_long clamp;                   /* 0x10 */
    u_long test;                    /* 0x18 */
    u_long zbuf;                    /* 0x20 */
} DRAW_ENV_5;

/* Untextured draw-env override (0x18): alpha / test / zbuf. */
typedef struct _DRAW_ENV_NOTEX      /* 0x18 */
{
    u_long alpha;                   /* 0x00 */
    u_long test;                    /* 0x08 */
    u_long zbuf;                    /* 0x10 */
} DRAW_ENV_NOTEX;

/* Effect-side draw env (0x30).  Same GS registers as DRAW_ENV_5 in a
 * different order, plus the PRIM the effect wants its packet drawn with;
 * Set3DPosTexure() copies the first five into a DRAW_ENV_5 and emits `prim`
 * into the GIF packet itself. */
typedef struct _DRAW_ENV            /* 0x30 */
{
    u_long tex1;                    /* 0x00 */
    u_long alpha;                   /* 0x08 */
    u_long zbuf;                    /* 0x10 */
    u_long test;                    /* 0x18 */
    u_long clamp;                   /* 0x20 */
    u_long prim;                    /* 0x28 */
} DRAW_ENV;

/* ---- lifecycle --------------------------------------------------------- */
void InitDrawEnv(qword *(*c1_start)(void), qword *(*c2_start)(void),
                 void (*c1_end)(qword *), void (*c2_end)(qword *));
void ClearDrawEnv(void);

/* ---- whole-env push ----------------------------------------------------- *
 * const per the ROM's mangled names, SetDrawEnv__FiPC11_DRAW_ENV_5 and
 * SetDrawEnvNoTex__FiPC15_DRAW_ENV_NOTEX.                                    */
void SetDrawEnv(int context_no, const DRAW_ENV_5 *new_env);
void SetDrawEnvNoTex(int context_no, const DRAW_ENV_NOTEX *new_env);

/* ---- per-register setters (context 0/1) --------------------------------- *
 * The ROM declares every register value `long int`, which is 64 bits on the
 * EE.  `long` is 32 bits on this host, so it is spelled u_long here: ZBUF's
 * ZMSK is bit 32, SCISSOR's SCAY0/SCAY1 are bits 32..63 and ALPHA's FIX is
 * bits 32..39, all of which a 32-bit parameter drops on the floor.           */
void SetAlphaRegister(int context_no, u_long alpha);
void SetTestRegister(int context_no, u_long test);
void SetZbufRegister(int context_no, u_long zbuf);
void SetTex1Register(int context_no, u_long tex1);
void SetClampRegister(int context_no, u_long clamp);
void SetScissorRegister(int context_no, u_long scissor);

/* ---- shared TEXA register ---------------------------------------------- */
void SetTexaRegister(u_long texa);

/* ---- per-register getters (context 0/1) -------------------------------- */
u_long GET_ALPHA_REGISTER(int context_no);
u_long GET_TEST_REGISTER(int context_no);
u_long GET_ZBUF_REGISTER(int context_no);
u_long GET_TEX1_REGISTER(int context_no);
u_long GET_CLAMP_REGISTER(int context_no);
u_long GET_SCISSOR_REGISTER(int context_no);
u_long GET_TEXA_REGISTER(void);

#endif /* _GRAPHICS_DRAW_ENV_H */
