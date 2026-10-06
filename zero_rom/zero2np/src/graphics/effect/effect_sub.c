// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_sub.c
//
// The effect system's helper module: the whole-screen colour fade, the
// billboard writers, point-visibility queries, the GS<->EE image stashes,
// the positional effect SE layer, and the falling-leaves particle system.
//
// All 36 ZERO2.MAP exports plus the 20 statics.  Line annotations are
// measured from the ROM's own debug info; statements whose only memory
// access goes through a fixed_array subscript lose their line marker to the
// inlined bounds check (fixed_array.h 124/125) and are interpolated -- those
// carry a trailing '?'.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x00165950.

#include "effect_sub.h"

#include <math.h>
#include <string.h>                             /* memset */

#include "../../common/utility.h"          /* GetDistV */

#include "effect.h"                             /* EffectGetRandom / EFFECT_MALLOC */
#include "effect_obj.h"                         /* GetCornHitCheck2 */
#include "effect_pak.h"                         /* Reserve2DPacket */

#include "../dmaVif1.h"                         /* dmaVif1CheckDMA */
#include "../draw_env.h"
#include "../graphics.h"                        /* RendererPacket3D */
#include "../graph2d/g2d_draw.h"                /* Q_WORDDATA / U32DATA / DMA */
#include "../graph2d/graph2d.h"                 /* effdat[] */
#include "miopan/rendering/miopan_renderer.h"   /* point-visibility probes */
#include "../graph3d/g3dGsWrapper.h"            /* g3dGsExecStoreImage / SyncPath */
#include "../graph3d/g3dxVu0.h"                 /* g3dxVu0CopyVector */
#include "../graph3d/gra3d.h"                   /* gra3dGetCamera */
#include "../../miopan/miopan_memory.h"         /* MioPan_GetHostPointer */
#include "../../miopan/miopan_profiler.h"
#include "../../miopan/rendering/miopan_renderer.h"
#include "../../sdk/eekernel.h"                 /* FlushCache */
#include "../../sdk/libdma.h"                   /* sceDmaGetChan / sceDmaChan */
#include "../../sdk/libgraph.h"                 /* sceGsSetDefStoreImage */
#include "../../sdk/libvu0.h"
#include "../../sdk/sce_gs.h"                   /* sceGsTex0 / sceGsStoreImage */
#include "../../system/os/system.h"             /* pdrawenv / GetPALMode */
#include "../../system/eeiop/snd3d.h"           /* SND_3D_SET */
#include "../../system/eeiop/sndbank.h"         /* SndBank* */
#include "../../system/eeiop/snd_buffer.h"      /* SndBuf* */

/* The whole-screen colour panel's control block. */
typedef struct                          /* 0x10, types.txt SCRCTRL */
{
    /* 0x0 */ int    screen_flag;       /* 0 idle, 1 fading out, 2 fading in */
    /* 0x4 */ int    time;
    /* 0x8 */ int    cnt;
    /* 0xc */ u_char col_r;
    /* 0xd */ u_char col_g;
    /* 0xe */ u_char col_b;
    /* 0xf */ u_char now_alpha;
} SCRCTRL;

static SCRCTRL sc_col;                                      /* data 2fd560 */

SINGLE_LINK_LIST LeavesList;                                /* data 2fd570 */

/* EE addresses of the image stash buffers InitEffectSub() hands out.  buf and
 * buf2 are the two PACKET2D frame buffers (g2d_draw.c pins its ring to the
 * same constants); bufz is devkit memory above the retail 32MB, where the
 * prototype parks the Z buffer during a screen effect. */
static u_long128 *buf;                                      /* sbss 3f4be4 */
static u_long128 *buf2;                                     /* sbss 3f4be8 */
static u_long128 *bufz;                                     /* sbss 3f4bec */

EFFECT_SOUND_CTRL EffectSoundCtrl;                          /* data 2fd580 */

/* The ROM's effect_sub.c forward-declares every function up front (its
 * declaration block is lines 9..119); these are the statics that need it. */
static int  EffectSndFileReadyRegisteredCheck(int FileNo);
static void EffectSndFileReadyRemove(int FileNo);
static int  EffectSndGetBankNo(int FileNo);
static int  EffectSndFileIsReady(int FileNo);
static void EffectSndFileReadyCtrl(void);
static void EffectSndFileReadyAllReleaseAndRemove(void);
static int  EffectSndFileDeleteRegisteredCheck(int FileNo);
static void EffectSndFileDeleteRemove(int FileNo);
static void EffectSndFileDeleteAllRemove(void);
static void EffectSndFileDeleteCtrl(void);
static void EffectSndPlaySub(int FileNo, int No, int Effect, int FadeTime,
                             float (*pPosition)[3], u_int DeleteKey);
static int  EffectSndFilePlayUseCheck(int FileNo);
static void EffectSndPlayCtrl(void);
static void EffectLeavesFallParticleInit(LEAVES_PARTICLE *pParticle,
                                         float *CenterPos, int Area,
                                         float *FallSpeed, short FallDistance,
                                         short *Color);
static int  EffectLeavesFallCtrlInit(LEAVES_FALL_CTRL *pLfCtrl,
                                     float *CenterPos, int Area, int FallMax,
                                     const float *FallSpeed, int Height,
                                     int StopTime, int ColR, int ColG,
                                     int ColB, int Alpha);
static void EffectLeavesDropSet(LEAVES_FALL_CTRL *pLfCtrl);
static void EffectLeavesFallExecSub(LEAVES_FALL_CTRL *pLfCtrl);
static void EffectLeavesUpdateTrans(float *leaf, float *axel, float *aim,
                                    float *FallSpeed);
static void EffectLeavesUpdateRot(float *rotation, float *axel, float *aim);
static void EffectLeavesLight(float *leaf, short *rgba, short *Color);

void InitEffectSub(void)
{
    buf  = (u_long128 *)0x1e79b00;                              /* 122 */
    buf2 = (u_long128 *)0x1f05b00;                              /* 123 */
    bufz = (u_long128 *)0x5000000;                              /* 124 */
    SingleLinkListInit(&LeavesList, sizeof(LEAVES_FALL_CTRL));  /* 127 */
}

void SetParam(int alp, int time, u_char r, u_char g, u_char b, int flag)
{
    sc_col.screen_flag = flag;                                  /* 138 */
    sc_col.time = time;                                         /* 139? */
    if (GetPALMode() != 0)                                      /* 140 */
    {
        sc_col.time = (int)((float)time / 1.1999999f);          /* 141 */
    }

    sc_col.cnt = 0;                                             /* 146 */
    sc_col.col_r = r;                                           /* 147 */
    sc_col.col_g = g;                                           /* 148 */
    sc_col.col_b = b;                                           /* 149 */
    sc_col.now_alpha = (u_char)alp;                             /* 150 */
}

int ScreenCtrl(void)
{
    if (sc_col.now_alpha != 0)                                  /* 156 */
    {
        /* Faithful to the ROM: the blue channel is passed col_g, so the
         * panel can never show the armed col_b. */
        SetPanel2(0x10, 0.0f, 0.0f, 640.0f, 448.0f, 0,
                  sc_col.col_r, sc_col.col_g, sc_col.col_g, sc_col.now_alpha);
    }

    switch (sc_col.screen_flag)                                 /* 160 */
    {
    case 1:
        sc_col.now_alpha = (u_char)(((sc_col.time - sc_col.cnt) * 128) / sc_col.time); /* 164 */
        sc_col.screen_flag = sc_col.cnt < sc_col.time;          /* 165 */
        sc_col.cnt++;                                           /* 166 */
        break;                                                  /* 167 */
    case 2:
        sc_col.now_alpha = (u_char)((sc_col.cnt * 128) / sc_col.time); /* 169 */
        if (sc_col.time <= sc_col.cnt)                          /* 170 */
        {
            sc_col.screen_flag = 0;
        }
        sc_col.cnt++;                                           /* 171 */
        break;
    }

    return sc_col.screen_flag;                                  /* 174 */
}

void SetPanel2(u_int pri, float x1, float y1, float x2, float y2, int z,
               u_char r, u_char g, u_char b, u_char a)
{
    SQAR_DAT  sq = { 640, 448, 0, 0, 0, 0, 0, 0, 0x80 };        /* 181, rdata 3a8258 */
    DISP_SQAR dq;
    int       i;

    CopySqrDToSqr(&dq, &sq);                                    /* 184 */

    dq.pri = pri; dq.z = 0xfffff - (pri & 0xfffff);             /* 185 */
    dq.zbuf = ((u_long)z << 32) | 0xa000118;                    /* 186 */
    dq.x[0] = dq.x[2] = (int)x1; dq.y[0] = dq.y[1] = (int)y1;   /* 187 */
    dq.x[1] = dq.x[3] = dq.x[0] + (int)(x2 - x1);               /* 188 */
    dq.y[2] = dq.y[3] = dq.y[0] + (int)(y2 - y1);               /* 189 */
    for (i = 0; i < 4; i++)                                     /* 190 */
    {
        dq.r[i] = r; dq.g[i] = g; dq.b[i] = b;
    }
    dq.alpha = a;                                               /* 191? */
    DispSqrD(&dq);                                              /* 192 */
}

/* One full-screen sprite textured straight from GS memory: TBP `addr`,
 * 640x256 PSMCT32 (TEX0 0x...6ba28000), drawn at the very back. */
void SetScreenZ(int addr)
{
    SPRT_DAT2 sd = { 0,                                         /* 200, rdata 3a8270 */
                     0.099999994f, 0.099999994f,
                     639.89996f, 447.9f,
                     640.0f, 448.0f,
                     -0.5f, -0.5f,
                     0xa0, 0x80 };
    DISP_SPRT2 ds;

    CopySprDToSpr2(&ds, &sd);                                   /* 203 */
    ds.tex0 = (u_long)addr | 0x200000026ba28000ULL;             /* 204 */
    ds.z = 0xfffff;                                             /* 205 */
    DispSprD2(&ds);                                             /* 207 */
}

/* --------------------------------------------------------------------------
 *  Textured quad from the effect bank, placed by a world-local matrix.
 *
 *  `wlm` positions and orients a w x h quad centred on its own origin; the
 *  callers build it as camera-facing, which is what makes these read as
 *  billboards.  `texno` indexes effdat[], and the whole texture is mapped with
 *  a 1% inset on every edge so bilinear filtering cannot pull in a neighbour.
 *
 *  This body is shared: in the ROM it is one inlined static (its own lines
 *  are 213..363, below both wrappers' opening lines 367/372, and each
 *  expansion carries a private copy of stq's .sdata image at 3f0068/3f0070).
 *  The original's name does not survive; the -Sub suffix follows
 *  EffectSndPlaySub() in this same file.
 *
 *  PORT NOTE: the GS packet built at the bottom is faithful but inert here --
 *  dmaVif1 collects DIRECT packets and never executes them.  As in
 *  MakePacket3D(), the host draw is queued first and deliberately survives the
 *  PS2-only guard band cull below, so a disagreement in the emulated
 *  fixed-point projection cannot make an otherwise visible quad vanish.
 * ------------------------------------------------------------------------ */
static inline void Set3DPosTexureSub(float (*wlm)[4], DRAW_ENV *de, int texno,
                                     float w, float h,
                                     u_char r, u_char g, u_char b, u_char a,
                                     int MonochroModeFlg)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_BILLBOARD_CPU);
    /* 2^TW / 2^TH, i.e. the GS texture dimensions the S/T values divide by. */
    static const float twoby[12] =                              /* rdata 3a82a0 */
    {
        1.0f, 2.0f, 4.0f, 8.0f, 16.0f, 32.0f,
        64.0f, 128.0f, 256.0f, 512.0f, 1024.0f, 0.0f
    };

    float          stq[2] = { 0.01f, 0.99f };
    float          slm[4][4];
    sceVu0IVECTOR  ivec[4];
    float          ppos[4][4];
    U32DATA        ts[4];
    U32DATA        tt[4];
    U32DATA        tq[4];
    DRAW_ENV_5     env;
    GRA3DCAMERA   *pCam;
    Q_WORDDATA    *pbuf;
    const sceGsTex0 *pTx0;
    u_int          clpz2;
    int            ClipFlg;
    int            ndpkt;
    int            Col;
    int            i;
    float          tw, th;

    clpz2 = 0xffffff;                                           /* 231 */

    pCam = gra3dGetCamera();                                    /* 252 */

    /* Corner order 0..3 is the PS2 triangle strip: top-left, top-right,
     * bottom-left, bottom-right. */
    for (i = 0; i < 4; i++)                                     /* 254 */
    {
        ppos[i][0] = ((i & 1) == 0 ? -w : w) * 0.5f;            /* 255 */
        ppos[i][1] = ((i / 2) == 0 ? h : -h) * 0.5f;            /* 256 */
        ppos[i][2] = 0.0f;                                      /* 257 */
        ppos[i][3] = 1.0f;                                      /* 258 */
    }                                                           /* 259 */

    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);            /* 260 */
    sceVu0RotTransPersN(ivec, slm, ppos, 4, 1);                 /* 264 */

    /* The monochrome twin sits at the next index, and effdat[] is laid out in
     * colour/mono pairs for exactly this. */
    pTx0 = (const sceGsTex0 *)&effdat[texno + MonochroModeFlg]; /* 269 */
    tw = (float)effdat[texno + MonochroModeFlg].w;              /* 272 */
    th = (float)effdat[texno + MonochroModeFlg].h;              /* 273 */

    if (MonochroModeFlg != 0)                                   /* 279 */
    {
        /* Faithful to the ROM: the /3 binds to b alone, so this is
         * r + g + b/3 rather than the (r + g + b)/3 that was surely meant.
         * Most callers pass r == g == b, where the difference only shows as an
         * overall brightening that the 255 clamp then swallows. */
        Col = (int)r + (int)g + (int)b / 3;                     /* 282 */
        if (Col >= 256)                                         /* 283 */
        {
            Col = 255;
        }
        g = (u_char)Col;                                        /* 284 */
        r = g;                                                  /* 285 */
        b = g;                                                  /* 286 */
    }

    /* Host path, queued before the PS2 guard band test below can bail out. */
    {

        env.alpha = de->alpha;
        env.tex1 = de->tex1;
        env.clamp = 0;
        env.test = de->test;
        env.zbuf = de->zbuf;
        SetDrawEnv(0, &env);

        float aWorld[4][4];

        for (i = 0; i < 4; i++)
        {
            sceVu0ApplyMatrix(aWorld[i], wlm, ppos[i]);
        }

        RendererPacket3D(aWorld, 4, r, g, b, a,
                         tw * stq[0], th * stq[0],
                         tw * (stq[1] - stq[0]), th * (stq[1] - stq[0]),
                         twoby[pTx0->TW], twoby[pTx0->TH], pTx0);
    }

    ClipFlg = 0;
    for (i = 0; i < 4; i++)                                     /* 290 */
    {
        if ((u_int)ivec[i][0] < 0x4000 || (u_int)ivec[i][0] > 0xc000) /* 291 */
        {
            ClipFlg = 1;
        }
        if ((u_int)ivec[i][1] < 0x4000 || (u_int)ivec[i][1] > 0xc000) /* 292 */
        {
            ClipFlg = 1;
        }
        if ((u_int)ivec[i][2] == 0 || (u_int)ivec[i][2] > clpz2) /* 293 */
        {
            ClipFlg = 1;
        }

        /* Perspective-correct STQ: Q is 1/w and S/T are premultiplied by it. */
        tq[i].fl32 = 1.0f / (float)ivec[i][3];                  /* 296 */
        ts[i].fl32 = (tw * stq[i % 2] * tq[i].fl32) / twoby[pTx0->TW]; /* 297 */
        tt[i].fl32 = (th * stq[i / 2] * tq[i].fl32) / twoby[pTx0->TH]; /* 298 */
    }                                                           /* 300 */

    if (ClipFlg != 0)                                           /* 301 */
    {
        return;
    }

    env.alpha = de->alpha;
    env.tex1  = de->tex1;
    env.clamp = 0;
    env.test  = de->test;
    env.zbuf  = de->zbuf;                                       /* 306 */
    SetDrawEnv(0, &env);                                        /* 313 */

    if ((MioPan_RendererGetDebugViewFlags() &
         MIOPAN_RENDERER_DEBUG_SKIP_BILLBOARD_LEGACY_PACKETS) != 0)
    {
        MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_LEGACY_SKIPS,
                                  1);
        return;
    }

    pbuf = StartDmaDirectTrans();                               /* 317 */
    Reserve2DPacket(0x10);                                      /* 319 */

    /* A+D tag for two register writes: TEXFLUSH, then this quad's TEX0. */
    pbuf[0].ul64[0] = 0x1000000000008002ULL;                    /* 321 */
    pbuf[0].ul64[1] = 0x0e;                                     /* 322 */
    pbuf[1].ul64[0] = 0;                                        /* 324 */
    pbuf[1].ul64[1] = 0x3f;                                     /* 325 */
    pbuf[2].ul64[0] = effdat[texno + MonochroModeFlg].tex0;     /* 327 */
    pbuf[2].ul64[1] = 0x06;                                     /* 328 */

    /* Then the caller's own PRIM tag: 4 vertices of ST / RGBAQ / XYZ2. */
    pbuf[3].ul64[0] = de->prim;                                 /* 330 */
    pbuf[3].ul64[1] = 0x412;                                    /* 332 */

    ndpkt = 4;
    for (i = 0; i < 4; i++)                                     /* 337 */
    {
        pbuf[ndpkt].ui32[0] = (u_int)ts[i].ui32;                /* 339 */
        pbuf[ndpkt].ui32[1] = (u_int)tt[i].ui32;                /* 340 */
        pbuf[ndpkt].ui32[2] = (u_int)tq[i].ui32;                /* 341 */
        pbuf[ndpkt].ui32[3] = 0;                                /* 342 */

        pbuf[ndpkt + 1].ui32[0] = (u_int)r;                     /* 349 */
        pbuf[ndpkt + 1].ui32[1] = (u_int)g;                     /* 350 */
        pbuf[ndpkt + 1].ui32[2] = (u_int)b;                     /* 351 */
        pbuf[ndpkt + 1].ui32[3] = (u_int)a;                     /* 352 */

        pbuf[ndpkt + 2].iv[0] = ivec[i][0];                     /* 354 */
        pbuf[ndpkt + 2].iv[1] = ivec[i][1];                     /* 355 */
        pbuf[ndpkt + 2].iv[2] = ivec[i][2] << 4;                /* 357 */
        /* ADC: the first two vertices only prime the strip. */
        pbuf[ndpkt + 2].ui32[3] = (i < 2) ? 0x8000 : 0;         /* 361 */

        ndpkt += 3;
    }                                                           /* 362 */

    EndDmaDirectTrans(pbuf + ndpkt);                            /* 363 */
}

void Set3DPosTexure(float (*wlm)[4], DRAW_ENV *de, int texno, float w, float h,
                    u_char r, u_char g, u_char b, u_char a)
{                                                               /* 367 */
    Set3DPosTexureSub(wlm, de, texno, w, h, r, g, b, a,
                      EffWrkMonochroModeGet());                 /* 368 */
}

void Set3DPosTexure2(float (*wlm)[4], DRAW_ENV *de, int texno, float w, float h,
                     u_char r, u_char g, u_char b, u_char a, int MonochroModeFlg)
{                                                               /* 372 */
    Set3DPosTexureSub(wlm, de, texno, w, h, r, g, b, a,
                      MonochroModeFlg);                         /* 373 */
}

void CamSave(void)
{
}                                                               /* 487 */

int CamChangeCheck(void)
{
    return 0;                                                   /* 493 */
}

/* --------------------------------------------------------------------------
 *  Resolve a posted batch of point-visibility queries.
 *
 *  Each point is projected exactly as GetCamI2DPos() does it, rejected if it
 *  leaves the 640x448 frame or the usable Z range, and otherwise compared
 *  against the Z already standing in the frame's depth buffer.  PS2 depth
 *  counts *upwards* towards the viewer, so a stored value at least as large as
 *  the point's own means something is drawn in front of it -- occluded.
 *
 *  The read-back fetches an 8x1 strip rather than the single pixel wanted,
 *  because a GS->EE store-image transfer moves whole quadwords and eight
 *  PSMZ16S pixels are exactly 128 bits.  Hence the split of the pixel column
 *  into n1 (the aligned strip) and n2 (the short within it).
 *
 *  PORT: the read-back block is the ROM's and is kept intact, but it cannot
 *  answer the question here -- the host resolves depth into an SDL_GPU texture
 *  that is never mirrored into emulated GS memory, so the strip reads as zeros,
 *  the PS2 "nothing drawn / far plane" value, and every on-screen point comes
 *  back unoccluded.
 *
 *  So the question is asked of the renderer first, through
 *  MioPan_RendererQueryPointOccluded(), which makes the same comparison against
 *  its own depth buffer in its own reversed-Z units.  Deliberately NOT a mirror
 *  of GS memory: mapping host float depth back into the PS2's 16-bit Z would be
 *  a second conversion to get wrong, and a wrong one hides ghosts behind
 *  nothing -- worse than the always-visible behaviour it replaces.
 *
 *  The probe answers from the PREVIOUS frame's depth (a synchronous read-back
 *  would stall the pipeline) and reports "cannot tell" until it has one, on the
 *  first frames after a room load, for a point off screen, and on any device
 *  whose depth buffer is not D32_FLOAT.  In that case the ROM's own block runs
 *  and the old degradation stands: a ghost is a finder target whenever it is on
 *  camera, where the ROM additionally demanded a clear line of sight.
 * ------------------------------------------------------------------------ */
/* PORT ADDITION.  One renderer probe slot per (batch, point) pair, so a probe
 * follows the same ghost's same query across frames -- which is what makes a
 * one-frame-late answer meaningful.  ENE_WRK_MAX batches of PP_JUDGE's ten
 * points is 100, inside MIOPAN_DEPTH_PROBE_SLOTS.  Batches are few and long-
 * lived, so a linear scan of first-seen pointers is enough; a batch that never
 * comes back simply holds its row until the table wraps. */
#define PP_JUDGE_MAX_POINTS 10
#define PP_JUDGE_MAX_BATCHES (MIOPAN_DEPTH_PROBE_SLOTS / PP_JUDGE_MAX_POINTS)

static const PP_JUDGE *s_ppj_slot_owner[PP_JUDGE_MAX_BATCHES];
static int             s_ppj_slot_next;

static int PpJudgeSlotBase(const PP_JUDGE *ppj)
{
    int i;

    for (i = 0; i < PP_JUDGE_MAX_BATCHES; i++)
    {
        if (s_ppj_slot_owner[i] == ppj)
        {
            return i * PP_JUDGE_MAX_POINTS;
        }
    }

    i = s_ppj_slot_next;
    s_ppj_slot_next = (s_ppj_slot_next + 1) % PP_JUDGE_MAX_BATCHES;
    s_ppj_slot_owner[i] = ppj;
    return i * PP_JUDGE_MAX_POINTS;
}

void CheckPointDepth(PP_JUDGE *ppj)
{
    /* Store-image descriptor for the Z-buffer read-back.  Function-local in
     * the ROM too; safe because the routine runs to completion. */
    static sceGsStoreImage gs_simage;                           /* bss 478500 */

    float          wlm[4][4];
    float          slm[4][4];
    float          fzero[4];
    sceVu0IVECTOR  ivec;
    Q_WORDDATA     q;
    GRA3DCAMERA   *pCam;
    sceDmaChan    *DmaVif;
    int            nums;
    int            i;
    int            xx;
    int            yy;
    int            n1;
    int            n2;
    int            clip;
    int            slot_base;

    slot_base = PpJudgeSlotBase(ppj);

    memset(fzero, 0, sizeof(fzero));                            /* 509 */
    fzero[3] = 1.0f;                                            /* 510 */

    pCam = gra3dGetCamera();                                    /* 511 */

    nums = ppj->num;                                            /* 513 */

    for (i = 0; i < nums; i++)                                  /* 515 */
    {
        /* Same placement matrix as GetCamI2DPos(): the 25.0f scale is dead
         * weight, since the only point put through it is the matrix's own
         * origin, which no scale can move.  Both routines were plainly cut
         * from the same billboard code; kept because the ROM has it. */
        sceVu0UnitMatrix(wlm);                                  /* 518 */
        wlm[0][0] = wlm[1][1] = wlm[2][2] = 25.0f;              /* 519 */
        sceVu0TransMatrix(wlm, wlm, ppj->p[i]);                 /* 520 */
        sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);        /* 521 */
        sceVu0RotTransPers(ivec, slm, fzero, 0);                /* 522 */

        /* 4.4 fixed point about the GS's 2048-pixel primitive origin: X spans
         * 0x6c00..0x9400 (640 px), Y 0x7200..0x8e00 (448 px).  The unsigned
         * compares fold "below the frame" into the same test as "above it". */
        clip  = ((u_int)(ivec[0] - 0x6c00) > 0x2800);           /* 526 */
        clip += ((u_int)(ivec[1] - 0x7200) > 0x1c00);           /* 527 */
        clip += ((u_int)(ivec[2] - 0x00ff) > 0xfff00);          /* 528 */

        if (clip == 0)                                          /* 531 */
        {
            /* PORT: ask the renderer, which holds the only real depth there
             * is.  Only when it has no answer does the ROM's read-back below
             * run -- and that always reports "clear", which is the degradation
             * this replaces. */
            const int occluded = MioPan_RendererQueryPointOccluded(
                slot_base + i, ppj->p[i]);
            if (occluded != MIOPAN_DEPTH_PROBE_UNAVAILABLE)
            {
                ppj->result[i] = (occluded == 0);
                continue;
            }

            /* -8 is half a pixel in 4.4, so these round to the nearest one. */
            xx = (ivec[0] - 0x6bf8) / 16;                       /* 538 */
            yy = (ivec[1] - 0x71f8) / 16;                       /* 539 */
            n1 = xx / 8;                                        /* 540 */
            n2 = xx - n1 * 8;                                   /* 541 */

            DmaVif = sceDmaGetChan(1);                          /* 544 */

            sceGsSetDefStoreImage(&gs_simage, 0x2300, 10, SCE_GS_PSMZ16S, n1 * 8, yy, 8, 1); /* 546 */
            FlushCache(0);                                      /* 547 */
            g3dGsExecStoreImage(&gs_simage, &q.ul128);          /* 548 */
            g3dGsSyncPath(0, 0);                                /* 549 */
            /* Restore VIF1 tag transfer: the store-image ran the bus the other
             * way round and left TTE clear. */
            DmaVif->chcr.bits |= 0x40;                          /* 550 */

            clip = (q.us16[n2] >= (u_short)(ivec[2] >> 4));     /* 555 */
        }

        ppj->result[i] = (clip == 0);
    }                                                           /* 561 */
}

/* --------------------------------------------------------------------------
 *  World point -> interlaced 2D screen position.
 *
 *  The point becomes the origin of a placement matrix, that matrix goes through
 *  the camera's world->screen transform, and the perspective divide plus the
 *  GS's 2048-pixel primitive offset are undone by hand, leaving a plain 0..640
 *  / 0..448 frame coordinate.  EneInDispChk() range-checks the pair to decide
 *  whether a ghost is on camera at all.
 *
 *  `pl` is the interlace field bias.  The ROM computes it as a constant zero --
 *  f21 is cleared and never written again -- so it only survives here to keep
 *  the expression matching.
 * ------------------------------------------------------------------------ */
void GetCamI2DPos(const float *pos, float *tx, float *ty)
{
    float         wlm[4][4];
    float         slm[4][4];
    float         vt[4];
    float         vtw[4];
    float         tmp_pos[4];
    GRA3DCAMERA  *pCam;
    float         pl;

    memset(vt, 0, sizeof(vt));                                  /* 653 */
    vt[3] = 1.0f;

    pCam = gra3dGetCamera();                                    /* 656 */

    sceVu0UnitMatrix(wlm);                                      /* 659 */
    wlm[0][0] = wlm[1][1] = wlm[2][2] = 25.0f;                  /* 660 */

    /* pos is const and sceVu0TransMatrix() is not; the ROM's quadword copy
     * through a scratch vector is what makes that legal. */
    g3dxVu0CopyVector(tmp_pos, pos);
    sceVu0TransMatrix(wlm, wlm, tmp_pos);                       /* 662 */
    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);            /* 663 */

    pl = 0.0f;                                                  /* 668 */

    sceVu0ApplyMatrix(vtw, slm, vt);                            /* 673 */
    sceVu0ScaleVector(vtw, vtw, 1.0f / vtw[3]);                 /* 675 */

    *tx = (vtw[0] - 2048.0f) + 320.0f;                          /* 677 */
    *ty = (vtw[1] - 2048.0f) + 224.0f + pl;                     /* 678 */
}

/* --------------------------------------------------------------------------
 *  Direction vector -> X/Y Euler pair.
 *
 *  Y is the heading, straight out of atan2(x, z).  X is the pitch: with the
 *  XZ length as the opposite side, atan2(|xz|, y) reads 0 when the vector
 *  points along +Y, so the quarter-turn bias brings "level" back to 0.  Both
 *  results leave atan2f() inside (-PI, PI] already, but the ROM wraps anyway
 *  because that bias can push X past PI.
 *
 *  The XZ length is a vsqrt / vwaitq / vaddq(vf0, Q) triple in the ROM -- the
 *  VU0 scalar square root idiom.
 * ------------------------------------------------------------------------ */
void Vector2Rot(const float *dir, float *x, float *y)
{
    float LengthXZ;

    LengthXZ = sqrtf(dir[0] * dir[0] + dir[2] * dir[2]);            /* 686 */

    *y = atan2f(dir[0], dir[2]);
    *x = atan2f(LengthXZ, dir[1]) + 1.5707963f;

    if (*x > 3.1415925f)
    {
        *x -= 6.283185f;
    }
    else if (*x < -3.1415925f)
    {
        *x += 6.283185f;
    }

    if (*y > 3.1415925f)
    {
        *y -= 6.283185f;
    }
    else if (*y < -3.1415925f)
    {
        *y += 6.283185f;
    }
}

void Get2PosRot(const float *v1, const float *v2, float *x, float *y)
{
    float dir[4];

    sceVu0SubVector(dir, (float *)v2, (float *)v1);                 /* 701 */
    Vector2Rot(dir, x, y);                                          /* 705 */
}

/* Get2PosRot() against the Y difference instead of the XZ plane: the pair of
 * rotations that lay a falling object along the way it is going. */
void Get2PosRot2(const float *v1, const float *v2, float *x, float *z)
{
    float fy;
    float fz;

    fy = v2[1] - v1[1];                                             /* 708? */
    fz = v2[2] - v1[2];

    *z = atan2f(v2[0] - v1[0], fy);                                 /* 711 */
    *x = atan2f(fz, fy);                                            /* 712 */

    if (*x > 3.1415925f)                                            /* 713 */
    {
        *x -= 6.283185f;
    }
    else if (*x < -3.1415925f)
    {
        *x += 6.283185f;
    }

    if (*z > 3.1415925f)                                            /* 716 */
    {
        *z -= 6.283185f;                                            /* 717 */
    }
    else if (*z < -3.1415925f)                                      /* 718 */
    {
        *z += 6.283185f;
    }
}                                                                   /* 719 */

/* --------------------------------------------------------------------------
 *  The rotation that turns p0 to face p1.                          ROM 726
 *
 *  `id` is a bitfield, not an axis index: bit 0 asks for the pitch and bit 1
 *  for the heading, and anything not asked for is left at the zero written up
 *  front.  Note the pitch uses GetDistV(), the *XZ* distance, so it is the
 *  angle above the ground plane rather than a full 3D one -- and it is parked
 *  in dist[3], the sub-vector's unused w slot, rather than in a local of its
 *  own.
 * ------------------------------------------------------------------------ */
void GetTrgtRotType2(float *p0, float *p1, float *rot, int id)
{
    float dist[4];

    rot[0] = 0.0f; rot[1] = 0.0f; rot[2] = 0.0f; rot[3] = 0.0f;     /* 726 */

    sceVu0SubVector(dist, p1, p0);                                  /* 728 */

    if ((id & 1) != 0)                                              /* 731 */
    {
        dist[3] = GetDistV(p0, p1);                                 /* 732 */
        rot[0]  = atan2f(dist[1], dist[3]);                         /* 733 */
    }
    if ((id & 2) != 0)                                              /* 736 */
    {
        rot[1] = atan2f(dist[0], dist[2]);                          /* 737 */
    }
}                                                                   /* 740 */

/* The ROM builds the squared length in a VU0 macro-mode dot product and then
 * takes vsqrt/vwaitq on it; with no VU on the host the same expression is
 * evaluated directly. */
float Get2PLength(const float *v1, const float *v2)
{
    float xx = v2[0] - v1[0];
    float yy = v2[1] - v1[1];
    float zz = v2[2] - v1[2];

    return sqrtf((xx * xx) + (yy * yy) + (zz * zz));
}

int GetYOffset(void)
{
    return 0x7908 - (int)((sceGsDrawEnv1 *)pdrawenv)->xyoffset1.OFY; /* 791 */
}

float GetYOffsetf(void)
{
    /* The subtraction is done in u_long, so the unsigned 64-bit -> float
     * conversion idiom is what the ROM compiles here. */
    return (float)(u_long)(0x7908 - ((sceGsDrawEnv1 *)pdrawenv)->xyoffset1.OFY)
           * 0.0625f;                                               /* 796 */
}

/* --------------------------------------------------------------------------
 *  GS local <-> EE image stashes.
 *
 *  ZtoBZ parks the 640x224 PSMZ16S depth buffer (GS 0x2300) in the EE buffer
 *  at bufz while a screen effect owns the frame; BZtoZ puts it back.  LtoBD
 *  pulls a 640x224 PSMCT24 colour buffer at GS `addr` into `outbuf`, split
 *  into a 200-row strip and a 24-row strip 0x7d000 bytes up.
 *
 *  PORT: bufz is a raw devkit EE address (InitEffectSub), so it is translated
 *  at the point of use; MioPan_GsStore()/GsUpload() then move real bytes
 *  against the emulated GS memory, so the stash genuinely works.
 * ------------------------------------------------------------------------ */
void LocalCopyZtoBZ(void)
{
    static sceGsStoreImage gs_simage1;                          /* bss 478570 */

    sceGsSetDefStoreImage(&gs_simage1, 0x2300, 10, SCE_GS_PSMZ16S,
                          0, 0, 640, 224);                      /* 813 */
    g3dGsSyncPath(0, 0);                                        /* 814 */
    dmaVif1CheckDMA();                                          /* 815 */
    FlushCache(0);                                              /* 816 */
    g3dGsExecStoreImage(&gs_simage1,
                        (u_long128 *)MioPan_GetHostPointer((uintptr_t)bufz)); /* 817 */
    g3dGsSyncPath(0, 0);                                        /* 818 */
}

void LocalCopyBZtoZ(void)
{
    static sceGsLoadImage gs_limage1;                           /* bss 4785e0 */

    sceGsSetDefLoadImage(&gs_limage1, 0x2300, 10, SCE_GS_PSMZ16S,
                         0, 0, 640, 224);                       /* 824 */
    FlushCache(0);                                              /* 825 */
    sceGsExecLoadImage(&gs_limage1,
                       (u_long128 *)MioPan_GetHostPointer((uintptr_t)bufz)); /* 826 */
    g3dGsSyncPath(0, 0);                                        /* 827 */
}

void LocalCopyLtoBD(int addr, void *outbuf)
{
    static sceGsStoreImage gs_simage1;                          /* bss 478640 */
    static sceGsStoreImage gs_simage2;                          /* bss 4786b0 */

    sceGsSetDefStoreImage(&gs_simage1, (short)addr, 10, SCE_GS_PSMCT24,
                          0, 0, 640, 200);                      /* 837 */
    sceGsSetDefStoreImage(&gs_simage2, (short)addr, 10, SCE_GS_PSMCT24,
                          0, 200, 640, 24);                     /* 838 */
    dmaVif1CheckDMA();                                          /* 839 */
    g3dGsSyncPath(0, 0);                                        /* 840 */
    FlushCache(0);                                              /* 841 */
    g3dGsExecStoreImage(&gs_simage1, (u_long128 *)outbuf);      /* 842 */
    g3dGsExecStoreImage(&gs_simage2,
                        (u_long128 *)((u_char *)outbuf + 0x7d000)); /* 843 */
    g3dGsSyncPath(0, 0);                                        /* 844 */
}

/* ==========================================================================
 *  Positional effect SE.
 * ======================================================================== */

void EffectSndInit(void)
{
    SingleLinkListInit(&EffectSoundCtrl.FileReadyList,
                       sizeof(EFFECT_SOUNDFILE_DATA));          /* 889 */
    SingleLinkListInit(&EffectSoundCtrl.FileDeleteList,
                       sizeof(EFFECT_FILEDEL_DATA));            /* 890 */
    SingleLinkListInit(&EffectSoundCtrl.PlayList,
                       sizeof(EFFECT_SOUNDPLAY_DATA));          /* 891 */
}

void EffectSndEnd(void)
{
    EffectSndAllStop();                                         /* 899 */
    EffectSndFileReadyAllReleaseAndRemove();                    /* 900 */
    EffectSndFileDeleteAllRemove();                             /* 901 */
}

void EffectSndCtrl(void)
{
    EffectSndPlayCtrl();                                        /* 909 */
    EffectSndFileReadyCtrl();                                   /* 910 */
    EffectSndFileDeleteCtrl();                                  /* 911 */
}

static int EffectSndFileReadyRegisteredCheck(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileReadyList;
    SLL_CELL         *pCell;
    int               Registered;

    Registered = 0;                                             /* 921 */
    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 925 */
    {
        EFFECT_SOUNDFILE_DATA *pFileData =
            (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell); /* 926 */
        if (pFileData->FileNo == FileNo)                        /* 928 */
        {
            Registered = 1;                                     /* 929 */
            break;
        }
    }

    return Registered;                                          /* 936 */
}

void EffectSndFileReadyReq(int FileNo)
{
    EFFECT_SOUNDFILE_DATA  TmpFileData;
    EFFECT_SOUNDFILE_DATA *pSndData;
    SLL_CELL              *pCell;

    /* A pending release of the same file is cancelled first. */
    EffectSndFileDeleteRemove(FileNo);                          /* 951 */

    if (EffectSndFileReadyRegisteredCheck(FileNo) == 0)         /* 953 */
    {
        TmpFileData.FileNo = FileNo;                            /* 955 */
        TmpFileData.BankNo = -1;                                /* 956 */
        TmpFileData.IsReady = 0;

        pCell = SingleLinkListAddEnd(&EffectSoundCtrl.FileReadyList,
                                     &TmpFileData);             /* 959 */
        if (pCell != NULL)                                      /* 960 */
        {
            pSndData = (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell); /* 962 */
            /* The bank header is always the sound file minus one. */
            pSndData->BankNo = SndBankNew(FileNo, FileNo - 1, -1); /* 964 */
        }
    }
}

static void EffectSndFileReadyRemove(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileReadyList;
    SLL_CELL         *pCell;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 972 */
    while (pCell != NULL)                                       /* 975 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);
        EFFECT_SOUNDFILE_DATA *pFileData =
            (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell); /* 977 */

        if (pFileData->FileNo == FileNo)                        /* 979 */
        {
            SingleLinkListRemove(pSLL, pCell);                  /* 980 */
        }

        pCell = pNextCell;
    }
}                                                               /* 983 */

static int EffectSndGetBankNo(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileReadyList;
    SLL_CELL         *pCell;
    int               BankNo;

    BankNo = -1;                                                /* 996 */
    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 1000 */
    {
        EFFECT_SOUNDFILE_DATA *pFileData =
            (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1001 */
        if (pFileData->FileNo == FileNo)                        /* 1003 */
        {
            BankNo = pFileData->BankNo;                         /* 1004 */
            break;
        }
    }

    return BankNo;                                              /* 1011 */
}

static int EffectSndFileIsReady(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileReadyList;
    SLL_CELL         *pCell;
    int               IsReady;

    IsReady = 0;                                                /* 1020 */
    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 1024 */
    {
        EFFECT_SOUNDFILE_DATA *pFileData =
            (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1025 */
        if (pFileData->FileNo == FileNo && pFileData->IsReady != 0) /* 1027 */
        {
            IsReady = 1;                                        /* 1028 */
            break;                                              /* 1029 */
        }
    }

    return IsReady;                                             /* 1037 */
}

static void EffectSndFileReadyCtrl(void)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileReadyList;
    SLL_CELL         *pCell;

    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 1044 */
    {
        EFFECT_SOUNDFILE_DATA *pFileData =
            (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell);
        if (pFileData->IsReady == 0 &&
            SndBankIsReady(pFileData->BankNo) != 0)             /* 1047 */
        {
            pFileData->IsReady = 1;                             /* 1048 */
        }
    }                                                           /* 1052 */
}

static void EffectSndFileReadyAllReleaseAndRemove(void)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileReadyList;
    SLL_CELL         *pCell;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1067 */
    while (pCell != NULL)                                       /* 1070 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);
        EFFECT_SOUNDFILE_DATA *pFileData =
            (EFFECT_SOUNDFILE_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1072 */
        int BankNo = EffectSndGetBankNo(pFileData->FileNo);     /* 1073 */

        if (BankNo != -1)                                       /* 1075 */
        {
            SndBankRelease(BankNo);                             /* 1076 */
        }
        SingleLinkListRemove(pSLL, pCell);                      /* 1078 */

        pCell = pNextCell;
    }
}                                                               /* 1080 */

void EffectSndFileRelease(int FileNo)
{
    EFFECT_FILEDEL_DATA TmpDelData;

    if (EffectSndFileReadyRegisteredCheck(FileNo) != 0 &&
        EffectSndFileDeleteRegisteredCheck(FileNo) == 0)        /* 1094 */
    {
        TmpDelData.FileNo = FileNo;                             /* 1095 */
        SingleLinkListAddEnd(&EffectSoundCtrl.FileDeleteList, &TmpDelData); /* 1099 */
    }
}

static int EffectSndFileDeleteRegisteredCheck(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileDeleteList;
    SLL_CELL         *pCell;
    int               Registered;

    Registered = 0;                                             /* 1108 */
    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 1112 */
    {
        EFFECT_FILEDEL_DATA *pDelData =
            (EFFECT_FILEDEL_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1113 */
        if (pDelData->FileNo == FileNo)                         /* 1115 */
        {
            Registered = 1;                                     /* 1116 */
            break;
        }
    }

    return Registered;                                          /* 1123 */
}

static void EffectSndFileDeleteRemove(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileDeleteList;
    SLL_CELL         *pCell;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1130 */
    while (pCell != NULL)                                       /* 1133 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);
        EFFECT_FILEDEL_DATA *pDelData =
            (EFFECT_FILEDEL_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1135 */

        if (pDelData->FileNo == FileNo)                         /* 1137 */
        {
            SingleLinkListRemove(pSLL, pCell);                  /* 1138 */
        }

        pCell = pNextCell;
    }
}                                                               /* 1141 */

static void EffectSndFileDeleteAllRemove(void)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileDeleteList;
    SLL_CELL         *pCell;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1149 */
    while (pCell != NULL)                                       /* 1152 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        SingleLinkListRemove(pSLL, pCell);                      /* 1155 */

        pCell = pNextCell;
    }
}                                                               /* 1157 */

/* Releases a file the moment no live cue is still playing out of it. */
static void EffectSndFileDeleteCtrl(void)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.FileDeleteList;
    SLL_CELL         *pCell;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1165 */
    while (pCell != NULL)                                       /* 1168 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);
        EFFECT_FILEDEL_DATA *pDelData =
            (EFFECT_FILEDEL_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1170 */

        if (EffectSndFilePlayUseCheck(pDelData->FileNo) == 0)   /* 1173 */
        {
            int BankNo = EffectSndGetBankNo(pDelData->FileNo);  /* 1174 */

            if (BankNo != -1)                                   /* 1176 */
            {
                SndBankRelease(BankNo);                         /* 1177 */
            }
            EffectSndFileReadyRemove(pDelData->FileNo);         /* 1180 */
            SingleLinkListRemove(pSLL, pCell);                  /* 1181 */
        }

        pCell = pNextCell;
    }
}                                                               /* 1184 */

/* --------------------------------------------------------------------------
 *  The shared cue starter.  A cue whose bank is absent or still loading is
 *  parked on the play list with PlayId -1; EffectSndPlayCtrl() starts it when
 *  the bank comes up.  FadeTime is carried but never consumed -- no fade-in
 *  path exists in this build.
 * ------------------------------------------------------------------------ */
static void EffectSndPlaySub(int FileNo, int No, int Effect, int FadeTime,
                             float (*pPosition)[3], u_int DeleteKey)
{
    EFFECT_SOUNDPLAY_DATA TmpPlayData;
    SND_3D_SET            Snd3d;
    SND_3D_SET           *pSnd3d;
    SLL_CELL             *pCell;
    int                   BankNo;
    int                   LoopFlg;
    int                   PlayId;

    BankNo = EffectSndGetBankNo(FileNo);                        /* 1196 */

    if (pPosition != NULL)                                      /* 1199 */
    {
        /* The parameter is a 3-float position but the copy is the usual
         * quadword: the fourth float travels along.  ROM behaviour. */
        g3dxVu0CopyVector(TmpPlayData.Position, (const float *)pPosition);
    }
    TmpPlayData.SetPositionFlg = (pPosition != NULL);           /* 1201 */
    TmpPlayData.PlayId = -1;                                    /* 1204 */
    TmpPlayData.FileNo = FileNo;                                /* 1206 */
    TmpPlayData.No = No;                                        /* 1207 */
    TmpPlayData.Effect = Effect;                                /* 1208 */
    TmpPlayData.FadeTime = FadeTime;                            /* 1209 */
    TmpPlayData.DeleteKey = DeleteKey;                          /* 1211 */

    if (BankNo == -1)                                           /* 1213 */
    {
        /* Not even requested yet: request it and park the cue. */
        EffectSndFileReadyReq(FileNo);                          /* 1217 */
        SingleLinkListAddEnd(&EffectSoundCtrl.PlayList, &TmpPlayData); /* 1220 */
    }                                                           /* 1221 */
    else
    {
        LoopFlg = SndBankIsLoopSnd(BankNo, No);                 /* 1224 */
        if (EffectSndFileIsReady(FileNo) == 0)                  /* 1225 */
        {
            SingleLinkListAddEnd(&EffectSoundCtrl.PlayList, &TmpPlayData); /* 1226 */
        }
        else
        {
            memset(&Snd3d, 0, sizeof(SND_3D_SET));              /* 1231 */
            if (pPosition == NULL)                              /* 1233 */
            {
                pSnd3d = NULL;
            }
            else
            {
                pSnd3d = &Snd3d;                                /* 1238 */
                Snd3d.pos = (sceVu0FVECTOR *)pPosition;
                Snd3d.vel = NULL;
                Snd3d.dir = NULL;
            }

            PlayId = SndBankPlay(BankNo, No, Effect, LoopFlg != 0,
                                 0x3200, 0x1000, 0, pSnd3d);    /* 1240 */
            TmpPlayData.PlayId = PlayId;

            pCell = SingleLinkListAddEnd(&EffectSoundCtrl.PlayList,
                                         &TmpPlayData);         /* 1245 */
            if (pCell == NULL)                                  /* 1250 */
            {
                /* No room to track it: kill the voice rather than leak it. */
                SndBufStop(PlayId);
            }
        }
    }
}                                                               /* 1252 */

void EffectSndPlay(int FileNo, int No, int Effect, int FadeTime,
                   float (*pPosition)[3])
{
    EffectSndPlaySub(FileNo, No, Effect, FadeTime, pPosition, 0); /* 1261 */
}

void EffectSndPlayDeleteKey(int FileNo, int No, int Effect, int FadeTime,
                            float (*pPosition)[3], u_int DeleteKey)
{
    EffectSndPlaySub(FileNo, No, Effect, FadeTime, pPosition, DeleteKey); /* 1269 */
}

void EffectSndStop(int FileNo, int No, int FadeFlg)
{
    SINGLE_LINK_LIST      *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL              *pCell;
    EFFECT_SOUNDPLAY_DATA *pPlayData;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1276 */
    while (pCell != NULL)                                       /* 1279 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pPlayData = (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1281 */
        if (pPlayData->FileNo == FileNo && pPlayData->No == No) /* 1283 */
        {
            if (pPlayData->PlayId == -1)                        /* 1284 */
            {
                /* Never started: just unpark it.  A started cue stays on the
                 * list; EffectSndPlayCtrl() reaps it once the voice ends. */
                SingleLinkListRemove(pSLL, pCell);              /* 1285 */
            }
            else if (FadeFlg == 0)                              /* 1286 */
            {
                SndBufStop(pPlayData->PlayId);                  /* 1289 */
            }
            else
            {
                SndBufFadeStop(pPlayData->PlayId, 60);          /* 1294 */
            }
        }

        pCell = pNextCell;
    }
}                                                               /* 1298 */

void EffectSndStopDeleteKey(u_int DeleteKey, int FadeFlg)
{
    SINGLE_LINK_LIST      *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL              *pCell;
    EFFECT_SOUNDPLAY_DATA *pPlayData;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1306 */
    while (pCell != NULL)                                       /* 1309 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pPlayData = (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1311 */
        if (pPlayData->DeleteKey == DeleteKey)                  /* 1313 */
        {
            if (pPlayData->PlayId == -1)                        /* 1314 */
            {
                SingleLinkListRemove(pSLL, pCell);              /* 1315 */
            }
            else if (FadeFlg == 0)                              /* 1316 */
            {
                SndBufStop(pPlayData->PlayId);                  /* 1319 */
            }
            else
            {
                SndBufFadeStop(pPlayData->PlayId, 60);          /* 1324 */
            }
        }

        pCell = pNextCell;
    }
}                                                               /* 1328 */

void EffectSndAllStop(void)
{
    SINGLE_LINK_LIST      *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL              *pCell;
    EFFECT_SOUNDPLAY_DATA *pPlayData;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1336 */
    while (pCell != NULL)                                       /* 1339 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pPlayData = (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1341 */
        if (pPlayData->PlayId != -1)                            /* 1343 */
        {
            SndBufStop(pPlayData->PlayId);                      /* 1344 */
        }
        SingleLinkListRemove(pSLL, pCell);                      /* 1346 */

        pCell = pNextCell;
    }
}                                                               /* 1348 */

void EffectSndAllPause(void)
{
    SINGLE_LINK_LIST      *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL              *pCell;
    EFFECT_SOUNDPLAY_DATA *pPlayData;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1356 */
    while (pCell != NULL)                                       /* 1359 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pPlayData = (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1361 */
        if (pPlayData->PlayId != -1)                            /* 1363 */
        {
            SndBufPause(pPlayData->PlayId);                     /* 1364 */
        }

        pCell = pNextCell;
    }
}                                                               /* 1367 */

void EffectSndAllRestart(void)
{
    SINGLE_LINK_LIST      *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL              *pCell;
    EFFECT_SOUNDPLAY_DATA *pPlayData;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1375 */
    while (pCell != NULL)                                       /* 1378 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pPlayData = (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1380 */
        if (pPlayData->PlayId != -1)                            /* 1382 */
        {
            SndBufRestart(pPlayData->PlayId);                   /* 1383 */
        }

        pCell = pNextCell;
    }
}                                                               /* 1386 */

static int EffectSndFilePlayUseCheck(int FileNo)
{
    SINGLE_LINK_LIST *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL         *pCell;
    int               UseFlg;

    UseFlg = 0;                                                 /* 1394 */
    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 1398 */
    {
        EFFECT_SOUNDPLAY_DATA *pPlayData =
            (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1399 */
        if (pPlayData->FileNo == FileNo)                        /* 1401 */
        {
            UseFlg = 1;                                         /* 1402 */
            break;
        }
    }

    return UseFlg;                                              /* 1408 */
}

/* Starts every parked cue whose bank has come up, and reaps every started
 * cue whose voice has ended. */
static void EffectSndPlayCtrl(void)
{
    SINGLE_LINK_LIST      *pSLL = &EffectSoundCtrl.PlayList;
    SLL_CELL              *pCell;
    EFFECT_SOUNDPLAY_DATA *pPlayData;
    SND_3D_SET             Snd3d;
    SND_3D_SET            *pSnd3d;
    int                    BankNo;
    int                    LoopFlg;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1416 */
    while (pCell != NULL)                                       /* 1419 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pPlayData = (EFFECT_SOUNDPLAY_DATA *)SingleLinkListCellBodyPtr(pCell); /* 1421 */
        if (pPlayData->PlayId == -1)                            /* 1423 */
        {
            BankNo = EffectSndGetBankNo(pPlayData->FileNo);     /* 1425 */
            if (BankNo != -1 &&
                EffectSndFileIsReady(pPlayData->FileNo) != 0)   /* 1427 */
            {
                memset(&Snd3d, 0, sizeof(SND_3D_SET));          /* 1428 */
                LoopFlg = SndBankIsLoopSnd(BankNo, pPlayData->No); /* 1433 */
                if (pPlayData->SetPositionFlg == 0)             /* 1435 */
                {
                    pSnd3d = NULL;                              /* 1436 */
                }
                else
                {
                    pSnd3d = &Snd3d;                            /* 1437 */
                    Snd3d.pos = (sceVu0FVECTOR *)pPlayData->Position; /* 1438? */
                    Snd3d.vel = NULL;
                    Snd3d.dir = NULL;                           /* 1439 */
                }

                pPlayData->PlayId = SndBankPlay(BankNo, pPlayData->No,
                                                pPlayData->Effect,
                                                LoopFlg != 0,
                                                0x3200, 0x1000, 0, pSnd3d); /* 1442 */
            }
        }
        else
        {
            if (SndBufIsPlaying(pPlayData->PlayId) == 0)        /* 1445 */
            {
                SingleLinkListRemove(pSLL, pCell);              /* 1450 */
            }
        }

        pCell = pNextCell;
    }
}                                                               /* 1455 */

/* ==========================================================================
 *  Falling leaves.
 * ======================================================================== */

void EffectLeavesFallReq(float *CenterPos, int Id)
{
    LEAVES_FALL_CTRL TmpLeavesFallCtrl;
    float            Speed[4] = { 0.08f, 5.0499997f,
                                  0.08f, 11.37f };              /* 1477, rdata 3a82d0 */

    if (EffectLeavesFallCtrlInit(&TmpLeavesFallCtrl, CenterPos, 2000, 20,
                                 Speed, 1500, 117, 58, 85, 167, 161) != 0) /* 1479 */
    {
        /* Pre-scatter the column so the first frames do not rain a sheet. */
        EffectLeavesDropSet(&TmpLeavesFallCtrl);                /* 1486 */
        TmpLeavesFallCtrl.Id = Id;                              /* 1488? */
        if (SingleLinkListAddEnd(&LeavesList, &TmpLeavesFallCtrl) == NULL) /* 1489 */
        {
            EFFECT_FREE(TmpLeavesFallCtrl.pLeavesParticle);     /* 1490 */
        }
    }
}                                                               /* 1492 */

void EffectLeavesFallCut(int Id)
{
    SINGLE_LINK_LIST *pSLL = &LeavesList;
    SLL_CELL         *pCell;
    LEAVES_FALL_CTRL *pLfCtrl;

    pCell = SingleLinkListBeginCell(pSLL);                      /* 1498 */
    while (pCell != NULL)                                       /* 1501 */
    {
        SLL_CELL *pNextCell = SingleLinkListNextCell(pCell);

        pLfCtrl = (LEAVES_FALL_CTRL *)SingleLinkListCellBodyPtr(pCell); /* 1503 */
        if (pLfCtrl->Id == Id)                                  /* 1505 */
        {
            EFFECT_FREE(pLfCtrl->pLeavesParticle);              /* 1506 */
            SingleLinkListRemove(pSLL, pCell);                  /* 1507 */
        }

        pCell = pNextCell;
    }
}                                                               /* 1510 */

/* One leaf, (re)spawned somewhere over the volume: a random point in the
 * Area x Area square, FallDistance above the ground, falling at an integer
 * 4..6 world units a frame, with a random sway target on each axis. */
static void EffectLeavesFallParticleInit(LEAVES_PARTICLE *pParticle,
                                         float *CenterPos, int Area,
                                         float *FallSpeed, short FallDistance,
                                         short *Color)
{
    pParticle->Position[0] = CenterPos[0]
        + (float)(int)EffectGetRandom(-(float)Area * 0.5f,
                                      (float)Area * 0.5f);      /* 1520 */
    pParticle->Position[2] = CenterPos[2]
        + (float)(int)EffectGetRandom(-(float)Area * 0.5f,
                                      (float)Area * 0.5f);      /* 1522 */
    pParticle->Position[3] = 1.0f;                              /* 1523 */
    pParticle->Position[1] = CenterPos[1] - (float)FallDistance; /* 1524? */

    pParticle->Aim[0] = EffectGetRandom(-FallSpeed[3], FallSpeed[3]); /* 1526 */
    pParticle->Aim[2] = EffectGetRandom(-FallSpeed[3], FallSpeed[3]); /* 1527 */

    pParticle->Accel[1] = (float)((int)(FallSpeed[1] - 1.0f)
                                  + (int)EffectGetRandom(0.0f, 3.0f)); /* 1529 */
    pParticle->Accel[0] = 0.0f;                                 /* 1530 */
    pParticle->Accel[2] = 0.0f;                                 /* 1531 */

    pParticle->Rots[0] = 0.0f;                                  /* 1533 */
    pParticle->Rots[1] = 0.0f;                                  /* 1534 */
    pParticle->Rots[2] = 0.0f;                                  /* 1535 */

    pParticle->rgba[0] = Color[0];                              /* 1537? */
    pParticle->rgba[1] = Color[1];                              /* 1538? */
    pParticle->rgba[2] = Color[2];                              /* 1539? */
    pParticle->rgba[3] = 0;                                     /* 1540? */

    pParticle->at_ground = 0;                                   /* 1542 */
    pParticle->InCount = 0;                                     /* 1543 */
}

static int EffectLeavesFallCtrlInit(LEAVES_FALL_CTRL *pLfCtrl,
                                    float *CenterPos, int Area, int FallMax,
                                    const float *FallSpeed, int Height,
                                    int StopTime, int ColR, int ColG,
                                    int ColB, int Alpha)
{
    int i;

    pLfCtrl->pLeavesParticle =
        (LEAVES_PARTICLE *)EFFECT_MALLOC(FallMax * sizeof(LEAVES_PARTICLE)); /* 1554 */
    if (pLfCtrl->pLeavesParticle == NULL)                       /* 1555 */
    {
        return 0;
    }

    g3dxVu0CopyVector(pLfCtrl->CenterPos, CenterPos);           /* 1560 */
    pLfCtrl->ParticleMax = FallMax;                             /* 1561 */
    pLfCtrl->ParticleNum = FallMax;                             /* 1562? */
    g3dxVu0CopyVector(pLfCtrl->FallSpeed, FallSpeed);
    pLfCtrl->FallDistance = (short)Height;                      /* 1563 */
    pLfCtrl->StopTime = StopTime;                               /* 1564 */
    pLfCtrl->Area = (short)Area;                                /* 1565 */
    pLfCtrl->Color[0] = (short)ColR;                            /* 1566? */
    pLfCtrl->Color[1] = (short)ColG;                            /* 1567? */
    pLfCtrl->Color[2] = (short)ColB;                            /* 1568? */
    pLfCtrl->Color[3] = (short)Alpha;                           /* 1569? */

    /* Frames between spawns: the whole fall-plus-lie time shared out over
     * the population. */
    pLfCtrl->AppearRate = ((float)pLfCtrl->FallDistance / pLfCtrl->FallSpeed[1]
                           + (float)pLfCtrl->StopTime)
                          / (float)pLfCtrl->ParticleMax;        /* 1571 */
    pLfCtrl->AppearRateCount = pLfCtrl->AppearRate;             /* 1572 */

    for (i = 0; i < FallMax; i++)                               /* 1574 */
    {
        EffectLeavesFallParticleInit(&pLfCtrl->pLeavesParticle[i],
                                     pLfCtrl->CenterPos, Area,
                                     pLfCtrl->FallSpeed,
                                     pLfCtrl->FallDistance,
                                     pLfCtrl->Color.data());    /* 1577 */
    }

    return 1;                                                   /* 1579 */
}                                                               /* 1580 */

/* Scatter the freshly-made column vertically, as if the fall had already been
 * running: each leaf starts somewhere in the spawn window, pulled down by the
 * full lie-down allowance. */
static void EffectLeavesDropSet(LEAVES_FALL_CTRL *pLfCtrl)
{
    int i;

    for (i = 0; i < pLfCtrl->ParticleMax; i++)                  /* 1589 */
    {
        pLfCtrl->pLeavesParticle[i].Position[1] +=
            (float)(int)EffectGetRandom(0.0f, pLfCtrl->FallDistance)
            - (float)pLfCtrl->StopTime * pLfCtrl->FallSpeed[1]; /* 1591 */
    }                                                           /* 1592 */
}

void EffectLeavesFallExec(void)
{
    SINGLE_LINK_LIST *pSLL = &LeavesList;
    SLL_CELL         *pCell;
    LEAVES_FALL_CTRL *pLfCtrl;

    for (pCell = SingleLinkListBeginCell(pSLL); pCell != NULL;
         pCell = SingleLinkListNextCell(pCell))                 /* 1690 */
    {
        pLfCtrl = (LEAVES_FALL_CTRL *)SingleLinkListCellBodyPtr(pCell); /* 1693 */
        EffectLeavesFallExecSub(pLfCtrl);                       /* 1694 */
    }
}                                                               /* 1696 */

/* --------------------------------------------------------------------------
 *  One volume's per-frame work: trickle the population up to ParticleMax,
 *  advance and draw every live leaf.
 *
 *  The drawing block carries ROM lines 1599..1683 -- below this function's
 *  own 1734..1793 -- so it was a separate static defined between DropSet and
 *  FallExec that GCC fully inlined at its single call site; neither its name
 *  nor its exact parameter split survives (same situation as the packet
 *  writer in effect_rain.c), so it is written out inline here.  functions.txt
 *  carries its absorbed locals: Rot, TexNo, ppos, ivec, both matrices, tw/th,
 *  mr/mg/mb, pbuf, ndpkt, tex0, pCam and the inner i.
 * ------------------------------------------------------------------------ */
static void EffectLeavesFallExecSub(LEAVES_FALL_CTRL *pLfCtrl)
{
    LEAVES_PARTICLE *pParticleTop = pLfCtrl->pLeavesParticle;
    int              i;

    if (pLfCtrl->ParticleNum < pLfCtrl->ParticleMax)            /* 1734 */
    {
        pLfCtrl->AppearRateCount += 1.0f;                       /* 1735 */
        while (pLfCtrl->AppearRate <= pLfCtrl->AppearRateCount) /* 1736 */
        {
            pLfCtrl->ParticleNum++;                             /* 1737 */
            pLfCtrl->AppearRateCount -= pLfCtrl->AppearRate;    /* 1738 */
        }
    }

    {
        DRAW_ENV_5 env = { 0x44, 0x161, 0,
                           0x5000d, 0x10a000118ULL };           /* 1743, rdata 3a82e0 */

        SetDrawEnv(0, &env);                                    /* 1750 */
    }

    for (i = 0; i < pLfCtrl->ParticleNum; i++)                  /* 1753 */
    {
        LEAVES_PARTICLE *pParticle = &pParticleTop[i];

        if (pLfCtrl->CenterPos[1] <= pParticle->Position[1])    /* 1754 */
        {
            /* On the ground: lie there fading for StopTime frames, then
             * respawn at the top. */
            pParticle->at_ground++;                             /* 1755 */
            if (pLfCtrl->StopTime != 0)                         /* 1758 */
            {
                pParticle->rgba[3] = pLfCtrl->Color[3]
                    - (short)(((int)pLfCtrl->Color[3] * pParticle->at_ground)
                              / pLfCtrl->StopTime);             /* 1759? */
            }
            if (pLfCtrl->StopTime <= pParticle->at_ground)      /* 1762 */
            {
                EffectLeavesFallParticleInit(pParticle, pLfCtrl->CenterPos,
                                             pLfCtrl->Area,
                                             pLfCtrl->FallSpeed,
                                             pLfCtrl->FallDistance,
                                             pLfCtrl->Color.data()); /* 1763? */
            }
        }
        else
        {
            /* In the air: fade in over the first 60 frames, fall, sway. */
            if (pParticle->InCount < 60)                        /* 1770 */
            {
                pParticle->rgba[3] =
                    (short)(((int)pLfCtrl->Color[3] * pParticle->InCount) / 60); /* 1772? */
                pParticle->InCount++;                           /* 1774 */
            }
            else
            {
                pParticle->rgba[3] = pLfCtrl->Color[3];         /* 1777? */
            }

            pParticle->Position[1] += pParticle->Accel[1];      /* 1780 */
            EffectLeavesUpdateTrans(pParticle->Position, pParticle->Accel,
                                    pParticle->Aim, pLfCtrl->FallSpeed); /* 1782 */
            EffectLeavesUpdateRot(pParticle->Rots, pParticle->Accel,
                                  pParticle->Aim);              /* 1784 */
        }

        EffectLeavesLight(pParticle->Position, &pParticle->rgba[0],
                          &pLfCtrl->Color[0]);                  /* 1786? */

        /* ---- the inlined draw helper, ROM lines 1599..1683 ---- */
        {
            float          matLocalWorld[4][4];
            float          matLocalScreen[4][4];
            sceVu0IVECTOR  ivec[4];
            GRA3DCAMERA   *pCam;
            Q_WORDDATA    *pbuf;
            u_long         tex0;
            float         *Rot;
            int            TexNo;
            int            tw, th;
            int            ClipFlg;
            int            ndpkt;
            u_char         mr, mg, mb;

            TexNo = EffWrkMonochroModeGet() + 10;               /* 1599 */
            float ppos[4][4] = { { -12.0f,  12.0f, 0.0f, 1.0f },
                                 {  12.0f,  12.0f, 0.0f, 1.0f },
                                 { -12.0f, -12.0f, 0.0f, 1.0f },
                                 {  12.0f, -12.0f, 0.0f, 1.0f } }; /* 1602, rdata 3a8310 */

            Rot = pParticle->Rots;
            pCam = gra3dGetCamera();                            /* 1615 */

            if (EffWrkMonochroModeGet() != 0)                   /* 1617 */
            {
                mr = mg = mb = (u_char)((pParticle->rgba.data()[0]
                                         + pParticle->rgba.data()[1]
                                         + pParticle->rgba.data()[2]) / 3); /* 1618 */
            }
            else
            {
                mr = (u_char)pParticle->rgba.data()[0];         /* 1620 */
                mg = (u_char)pParticle->rgba.data()[1];         /* 1621 */
                mb = (u_char)pParticle->rgba.data()[2];         /* 1622 */
            }

            sceVu0UnitMatrix(matLocalWorld);                    /* 1626 */
            sceVu0RotMatrixX(matLocalWorld, matLocalWorld, Rot[0]); /* 1627 */
            sceVu0RotMatrixY(matLocalWorld, matLocalWorld, Rot[1]); /* 1628 */
            sceVu0RotMatrixZ(matLocalWorld, matLocalWorld, Rot[2]); /* 1629 */
            sceVu0TransMatrix(matLocalWorld, matLocalWorld,
                              pParticle->Position);             /* 1630 */
            sceVu0MulMatrix(matLocalScreen, pCam->matWorldScreen,
                            matLocalWorld);                     /* 1631 */
            sceVu0RotTransPersN(ivec, matLocalScreen, ppos, 4, 0); /* 1634 */

            /* The ROM's inlined helper declares its own i, shadowing the
             * particle loop's -- functions.txt lists both. */
            int i;

            /* Host path first, as everywhere: the DIRECT packet below is
             * inert on this port. */
            {
                float aWorld[4][4];

                for (i = 0; i < 4; i++)
                {
                    sceVu0ApplyMatrix(aWorld[i], matLocalWorld, ppos[i]);
                }

                RendererPacket3D(aWorld, 4, mr, mg, mb,
                                 (int)pParticle->rgba.data()[3],
                                 0.0f, 0.0f,
                                 (float)effdat[TexNo].w, (float)effdat[TexNo].h,
                                 (float)effdat[TexNo].w, (float)effdat[TexNo].h,
                                 (const sceGsTex0 *)&effdat[TexNo]);
            }

            ClipFlg = 0;                                        /* 1637 */
            for (i = 0; i < 4; i++)                             /* 1638? */
            {
                if ((u_int)(ivec[i][0] - 0x4000) > 0x8000)      /* 1639 */
                {
                    ClipFlg = 1;
                }
                if ((u_int)(ivec[i][1] - 0x4000) > 0x8000)      /* 1640 */
                {
                    ClipFlg = 1;
                }
                if ((u_int)(ivec[i][2] - 0xff) > 0xffff00)      /* 1641 */
                {
                    ClipFlg = 1;
                }
            }                                                   /* 1642 */

            if (ClipFlg == 0)                                   /* 1643 */
            {
                tex0 = effdat[TexNo].tex0;                      /* 1647 */
                tw = effdat[TexNo].w << 4;                      /* 1648 */
                th = effdat[TexNo].h << 4;                      /* 1649 */

                Reserve2DPacket(0x10);                          /* 1652 */
                pbuf = StartDmaDirectTrans();                   /* 1653 */

                /* A+D tag: TEXFLUSH, then this sheet's TEX0. */
                pbuf[0].ul64[0] = 0x1000000000000002ULL;        /* 1655 */
                pbuf[0].ul64[1] = 0x0e;                         /* 1656 */
                pbuf[1].ul64[0] = 0;                            /* 1658 */
                pbuf[1].ul64[1] = 0x3f;                         /* 1659 */
                pbuf[2].ul64[0] = tex0;                         /* 1661 */
                pbuf[2].ul64[1] = 0x06;                         /* 1662 */

                /* PRIM tag: 4 vertices of RGBAQ / UV / XYZ2, strip. */
                pbuf[3].ul64[0] = 0x30aa400000008004ULL;        /* 1664 */
                pbuf[3].ul64[1] = 0x431;                        /* 1665 */

                ndpkt = 4;
                for (i = 0; i < 4; i++)                         /* 1667 */
                {
                    pbuf[ndpkt].ui32[0] = mr;                   /* 1668 */
                    pbuf[ndpkt].ui32[1] = mg;                   /* 1669 */
                    pbuf[ndpkt].ui32[2] = mb;                   /* 1670 */
                    pbuf[ndpkt].ui32[3] = (u_int)pParticle->rgba.data()[3]; /* 1671 */

                    pbuf[ndpkt + 1].ui32[0] = (i & 1) != 0 ? (u_int)tw : 0; /* 1673 */
                    pbuf[ndpkt + 1].ui32[1] = (i / 2) != 0 ? (u_int)th : 0; /* 1674 */
                    pbuf[ndpkt + 1].ui32[2] = 0;                /* 1675 */
                    pbuf[ndpkt + 1].ui32[3] = 0;                /* 1676 */

                    pbuf[ndpkt + 2].iv[0] = ivec[i][0];         /* 1678 */
                    pbuf[ndpkt + 2].iv[1] = ivec[i][1];         /* 1679 */
                    pbuf[ndpkt + 2].iv[2] = ivec[i][2];         /* 1680 */
                    /* ADC: the first two vertices only prime the strip. */
                    pbuf[ndpkt + 2].ui32[3] = (i < 2) ? 0x8000 : 0; /* 1681 */

                    ndpkt += 3;
                }                                               /* 1682 */

                EndDmaDirectTrans(pbuf + ndpkt);                /* 1683 */
            }
        }
    }
}                                                               /* 1793 */

/* --------------------------------------------------------------------------
 *  One axis pair of the sway: the leaf drifts towards its Aim, easing in for
 *  the first half of the swing and out for the second, and picks a fresh
 *  integer target the moment it arrives.  Y drifts one unit up or down at
 *  random (up with probability 4 in 10), which is what makes a column of
 *  leaves shimmer instead of falling in lockstep.
 * ------------------------------------------------------------------------ */
static void EffectLeavesUpdateTrans(float *leaf, float *axel, float *aim,
                                    float *FallSpeed)
{
    if (axel[0] < aim[0])                                       /* 1801 */
    {
        axel[0] += FallSpeed[0];                                /* 1803 */
        if (aim[0] * 0.5f <= axel[0])                           /* 1805 */
        {
            leaf[0] += aim[0] - axel[0];                        /* 1806 */
        }
        else
        {
            leaf[0] += axel[0];                                 /* 1807? */
        }
        if (aim[0] <= axel[0])                                  /* 1808 */
        {
            aim[0] = (float)(int)EffectGetRandom(-FallSpeed[3],
                                                 FallSpeed[3]); /* 1809? */
            axel[0] = 0.0f;                                     /* 1810? */
        }
    }
    else
    {
        axel[0] -= FallSpeed[0];                                /* 1816 */
        if (axel[0] <= aim[0] * 0.5f)                           /* 1818 */
        {
            leaf[0] += aim[0] - axel[0];                        /* 1819 */
        }
        else
        {
            leaf[0] += axel[0];                                 /* 1820? */
        }
        if (axel[0] <= aim[0])                                  /* 1821 */
        {
            aim[0] = (float)(int)EffectGetRandom(-FallSpeed[3],
                                                 FallSpeed[3]); /* 1823 */
            axel[0] = 0.0f;                                     /* 1824 */
        }
    }

    if ((int)EffectGetRandom(0.0f, 10.0f) + 1 < 6)              /* 1831 */
    {
        leaf[1] += 1.0f;
    }
    else
    {
        leaf[1] -= 1.0f;                                        /* 1832 */
    }

    if (axel[2] < aim[2])                                       /* 1836 */
    {
        axel[2] += FallSpeed[2];                                /* 1838 */
        if (aim[2] * 0.5f <= axel[2])                           /* 1840 */
        {
            leaf[2] += aim[2] - axel[2];                        /* 1841 */
        }
        else
        {
            leaf[2] += axel[2];                                 /* 1842? */
        }
        if (aim[2] <= axel[2])                                  /* 1844 */
        {
            aim[2] = (float)(int)EffectGetRandom(-FallSpeed[3],
                                                 FallSpeed[3]); /* 1845? */
            axel[2] = 0.0f;                                     /* 1846? */
        }
    }
    else
    {
        axel[2] -= FallSpeed[2];                                /* 1852 */
        if (axel[2] <= aim[2] * 0.5f)                           /* 1854 */
        {
            leaf[2] += aim[2] - axel[2];                        /* 1855 */
        }
        else
        {
            leaf[2] += axel[2];                                 /* 1856? */
        }
        if (axel[2] <= aim[2])                                  /* 1857 */
        {
            aim[2] = (float)(int)EffectGetRandom(-FallSpeed[3],
                                                 FallSpeed[3]); /* 1859 */
            axel[2] = 0.0f;                                     /* 1860 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Sway progress -> billboard tilt.  Each axis leans up to a quarter turn,
 *  ramping with the swing (proportional to progress in the first half, to
 *  the remaining distance in the second), and the two axes lean opposite
 *  ways.  An aim inside [-1, 1] is treated as idle -- and then it is the
 *  Y spin that is zeroed, not the axis' own tilt.  ROM behaviour, kept.
 * ------------------------------------------------------------------------ */
static void EffectLeavesUpdateRot(float *rotation, float *axel, float *aim)
{
    if (axel[0] < aim[0] && (aim[0] < -1.0f || 1.0f < aim[0]))  /* 1869 */
    {
        if (aim[0] * 0.5f <= axel[0])                           /* 1872 */
        {
            rotation[2] = ((aim[0] - axel[0]) * -1.5707963f) / (aim[0] * 0.5f); /* 1873 */
        }
        else
        {
            rotation[2] = (axel[0] * -1.5707963f) / (aim[0] * 0.5f); /* 1874? */
        }
    }
    else if (aim[0] <= axel[0] && (aim[0] < -1.0f || 1.0f < aim[0])) /* 1875 */
    {
        if (axel[0] <= aim[0] * 0.5f)                           /* 1878 */
        {
            rotation[2] = ((aim[0] - axel[0]) * 1.5707963f) / (aim[0] * 0.5f); /* 1879 */
        }
        else
        {
            rotation[2] = (axel[0] * 1.5707963f) / (aim[0] * 0.5f); /* 1880? */
        }
    }
    else
    {
        rotation[1] = 0.0f;                                     /* 1884? */
    }

    if (axel[2] < aim[2] && (aim[2] < -1.0f || 1.0f < aim[2]))  /* 1887 */
    {
        if (aim[2] * 0.5f <= axel[2])                           /* 1890 */
        {
            rotation[0] = ((aim[2] - axel[2]) * 1.5707963f) / (aim[2] * 0.5f); /* 1891 */
        }
        else
        {
            rotation[0] = (axel[2] * 1.5707963f) / (aim[2] * 0.5f); /* 1892? */
        }
    }
    else if (aim[2] <= axel[2] && (aim[2] < -1.0f || 1.0f < aim[2])) /* 1893 */
    {
        if (axel[2] <= aim[2] * 0.5f)                           /* 1896 */
        {
            rotation[0] = ((aim[2] - axel[2]) * -1.5707963f) / (aim[2] * 0.5f); /* 1897 */
        }
        else
        {
            rotation[0] = (axel[2] * -1.5707963f) / (aim[2] * 0.5f); /* 1898? */
        }
    }
    else
    {
        rotation[1] = 0.0f;                                     /* 1901 */
    }
}

/* A leaf outside every room light's cone is drawn at half colour.  tes1/tes2
 * receive the light's two rates; this caller wants only the yes/no. */
static void EffectLeavesLight(float *leaf, short *rgba, short *Color)
{
    float tes1;
    float tes2;

    tes1 = 0.0f;                                                /* 1910 */
    tes2 = 0.0f;

    if (GetCornHitCheck2(leaf, 1200.0f, &tes1, &tes2) == 0)     /* 1912 */
    {
        rgba[0] = Color[0] / 2;                                 /* 1913 */
        rgba[1] = Color[1] / 2;                                 /* 1914 */
        rgba[2] = Color[2] / 2;                                 /* 1915? */
    }
    else
    {
        rgba[0] = Color[0];                                     /* 1918 */
        rgba[1] = Color[1];                                     /* 1919 */
        rgba[2] = Color[2];                                     /* 1920 */
    }
}
