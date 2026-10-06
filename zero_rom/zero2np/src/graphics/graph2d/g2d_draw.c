// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/g2d_draw.c
//
// 2D primitive draw layer.  Two families of primitives are built here:
//
//   * Sprites - DISP_SPRT / DISP_SPRT2 (textured quads).  The Copy*DToSpr*
//     helpers fold a compact caller record into the full draw descriptor and
//     stuff the fixed GS register payloads; DispSprD / DispSprD2 then rotate /
//     scale the four corners, convert to GS 12.4 fixed point, and emit a GIF
//     packet through the PK2D ring.
//   * Squares - DISP_SQAR (4-corner gouraud quad).  CopySqrDToSqr / CopyGSqDToSqr
//     / CopySq4DToSqr / CopyGS4DToSqr feed it; DispSqrD draws it.  SetPanel is a
//     convenience full-/sub-screen filled rect built on top.
//
// The PK2D ring (InitPK2Dbuf / SwapPK2Dbuf / GetPK2Dbuf / EndPK2Dbuf ...) wraps
// the dmaVif1* FLUSH_DIRECT allocator over the fixed PACKET2D EE region, and the
// LocalCopy* helpers drive GS LOCAL<->HOST (BtoL/LtoB) and LOCAL->LOCAL image
// transfers.
//
// The values stored into the texa / alphar / zbuf / test / tex1 / gftg slots and
// into the GIF qwords are GS register payloads and GIFtags.  The build inlined
// them from the SCE <libgraph.h> SCE_GS_SET_* / SCE_GIF_SET_TAG value builders
// (see sdk/libgraph.h); the call sites are rebuilt against those macros, each
// verified to re-encode to the exact constant the prototype emitted.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // this TU's API + the 2D primitive cluster

#include "../../sdk/libgraph.h"     // SCE GS register / GIFtag value builders
#include "../graph3d/ctl/fixed_array.h"     // fixed_array<> template (inlined per TU)

#include <math.h>                   // sinf / cosf
#include <stdio.h>                  // printf
#include <string.h>                 // memset (host screen read-back)
#include <libvu0.h>                 // sceVu0IVECTOR + _ftoi0 / _ftoi4 VU0 macros
#include <sifdev.h>                 // FlushCache

#include "../../system/os/system.h" // sys_wrk / GetDrawEnv / SYS_WRK
#include "../../common/variable.h"  // sys_wrk
#include "../../miopan/miopan_memory.h" // MioPan_GetHostPointer
#include "../../miopan/rendering/miopan_renderer.h"
#include "../graph3d/g3dDma.h"      // dmaVif1* ring allocator (also pulls sce_gs.h: sceGsTex0)
#include "../graph3d/g3dGsWrapper.h"// sceGsSetDefStoreImage / g3dGsExecStoreImage / g3dGsSyncPath
#include "../draw_env.h"           // SetDrawEnv / SetDrawEnvNoTex / SetTexaRegister + DRAW_ENV_5 / DRAW_ENV_NOTEX
#include "../dmaVif1.h"            // dmaVif1GetPacket*/SetPacket*/AddRefTagVIF (qword * ring)

// The dmaVif1* ring hands out raw qword quadwords; the 2D layer reinterprets
// them through the Q_WORDDATA union (see g2d_draw.h), so the call sites below
// cast between qword * and Q_WORDDATA * as needed.

// ──────────────────────────────────────────────────────────────────────
// The GetDrawEnv() read-back record - only the zbuf field (0x10) is read here.
// (DRAW_ENV_5 / DRAW_ENV_NOTEX are the canonical types from draw_env.h.)
typedef struct _DRAW_ENV_PEEK
{
    /* 0x00 */ long alpha;
    /* 0x08 */ long tex1;
    /* 0x10 */ long zbuf;
} DRAW_ENV_PEEK;

// Engine assert shim: see common/utility2.h.
#include "../../common/utility2.h" // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal

// ──────────────────────────────────────────────────────────────────────
// File-scope globals.

u_char              gInterlace;     // sdata 3f0a3e : 1 = interlaced (halve Y)
PK2D_WRK            pk2d_wrk;        // data  314920 : 2D packet ring pointers
VIF1_GS_PACKET_CTRL vif1gs;         // sdata 3f0a40 : VIF1->GS packet control

// the per-TU fixed_array<> template instances (_fixed_array_assert /
// _fixed_array_verifyrange<T>) are emitted here by the compiler; see
// "../graph3d/ctl/fixed_array.h" for their definitions.

// ──────────────────────────────────────────────────────────────────────
// Init hook.  Empty in this build.

void InitG2DDraw(void)
{
}

// ──────────────────────────────────────────────────────────────────────
// Fold an SPRT_DAT caller record into a full DISP_SPRT.  Centre / scale / rot
// are reset; the GS register payloads are the fixed 2D-sprite set.

void CopySprDToSpr(DISP_SPRT *s, SPRT_DAT *d)
{
    s->u = (u_int)(u_short)d->u;
    s->v = (u_int)d->v;
    s->att = (u_int)d->flip;
    s->w = (u_int)d->w;
    s->h = (u_int)d->h;
    s->tex0 = d->tex0;
    s->sch = 1.0;
    s->z = 0xfffff - (d->pri & 0xfffff);
    s->x = (float)d->x;
    s->y = (float)d->y;
    s->crx = 0.0;
    s->cry = 0.0;
    s->csx = 0.0;
    s->csy = 0.0;
    s->scw = 1.0;
    s->rot = 0.0;
    s->texa = SCE_GS_SET_TEXA(0, 1, 0x80);
    s->alphar = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    s->zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 1);
    // SET_TEX1(LCM=1,MXL=0,MMAG=bln,MMIN=5,...): bln is a 1-bit MMAG flag OR'd onto the base
    s->tex1 = SCE_GS_SET_TEX1(1, 0, d->bln, 5, 0, 0, 0);
    s->test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    s->gftg = SCE_GS_SET_PRIM(SCE_GS_PRIM_TRISTRIP, 0, 1, 0, 1, 0, 1, 0, 0);
    s->pri = d->pri;
    s->b = 0x80;
    s->r = 0x80;
    s->g = 0x80;
    s->col = 0;
    s->alpha = d->alpha;
}

// ──────────────────────────────────────────────────────────────────────
// Fold an axis-aligned single-colour SQAR_DAT into a 4-corner DISP_SQAR.

void CopySqrDToSqr(DISP_SQAR *s, SQAR_DAT *d)
{
    s->att = 0;
    s->x[0] = d->x;
    s->x[2] = d->x;
    s->sch = 1.0;
    s->texa = SCE_GS_SET_TEXA(0, 1, 0x80);
    s->alphar = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    s->x[1] = d->x + d->w;
    s->x[3] = d->x + d->w;
    s->zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 1);
    s->test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    s->y[0] = d->y;
    s->y[1] = d->y;
    s->crx = 0.0;
    s->cry = 0.0;
    s->csx = 0.0;
    s->y[2] = d->y + d->h;
    s->y[3] = d->y + d->h;
    s->csy = 0.0;
    s->scw = 1.0;
    s->rot = 0.0;
    s->z = 0xfffff - (d->pri & 0xfffff);
    s->pri = d->pri;

    for (int i = 0; i < 4; i++)
    {
        s->r[i] = d->r;
        s->g[i] = d->g;
        s->b[i] = d->b;
    }

    s->alpha = d->alpha;
}

// ──────────────────────────────────────────────────────────────────────
// Fold an axis-aligned per-corner-colour GSQR_DAT into a DISP_SQAR.

void CopyGSqDToSqr(DISP_SQAR *s, GSQR_DAT *d)
{
    int i;

    s->att = 0;
    s->x[0] = d->x;
    s->x[2] = d->x;
    s->sch = 1.0;
    s->texa = SCE_GS_SET_TEXA(0, 1, 0x80);
    s->alphar = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    s->x[1] = d->x + d->w;
    s->x[3] = d->x + d->w;
    s->zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 1);
    s->test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    s->y[0] = d->y;
    s->y[1] = d->y;
    s->crx = 0.0;
    s->cry = 0.0;
    s->csx = 0.0;
    s->y[2] = d->y + d->h;
    s->y[3] = d->y + d->h;
    s->csy = 0.0;
    s->scw = 1.0;
    s->rot = 0.0;
    s->z = 0xfffff - (d->pri & 0xfffff);
    s->pri = d->pri;

    for (int i = 0; i < 4; i++)
    {
        s->r[i] = d->r[i];
        s->g[i] = d->g[i];
        s->b[i] = d->b[i];
    }
    
    s->alpha = d->alpha;
}

// ──────────────────────────────────────────────────────────────────────
// Fold a free 4-corner single-colour SQR4_DAT into a DISP_SQAR.

void CopySq4DToSqr(DISP_SQAR *s, SQR4_DAT *d)
{
    int i;

    s->att = 0;
    s->crx = 0.0;
    s->cry = 0.0;
    s->csx = 0.0;
    s->csy = 0.0;

    for (i = 0; i < 4; i++)
    {
        s->x[i] = d->x[i];
        s->y[i] = d->y[i];
    }

    s->z = 0xfffff - (d->pri & 0xfffff);
    s->sch = 1.0;
    s->texa = SCE_GS_SET_TEXA(0, 1, 0x80);
    s->alphar = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    s->zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 1);
    s->test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    s->pri = d->pri;
    s->scw = 1.0;
    s->rot = 0.0;

    for (i = 0; i < 4; i++)
    {
        s->r[i] = d->r;
        s->g[i] = d->g;
        s->b[i] = d->b;
    }

    s->alpha = d->alpha;
}

// ──────────────────────────────────────────────────────────────────────
// Fold a free 4-corner per-corner-colour GSQ4_DAT into a DISP_SQAR.

void CopyGS4DToSqr(DISP_SQAR *s, GSQ4_DAT *d)
{
    int i;

    s->att = 0;
    s->crx = 0.0;
    s->cry = 0.0;
    s->csx = 0.0;
    s->csy = 0.0;
    
    for (i = 0; i < 4; i++)
    {
        s->x[i] = d->x[i];
        s->y[i] = d->y[i];
    }

    s->z = 0xfffff - (d->pri & 0xfffff);
    s->sch = 1.0;
    s->texa = SCE_GS_SET_TEXA(0, 1, 0x80);
    s->alphar = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    s->zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 1);
    s->test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    s->pri = d->pri;
    s->scw = 1.0;
    s->rot = 0.0;

    for (i = 0; i < 4; i++)
    {
        s->r[i] = d->r[i];
        s->g[i] = d->g[i];
        s->b[i] = d->b[i];
    }

    s->alpha = d->alpha;
}

// ──────────────────────────────────────────────────────────────────────
// Fold an SPRT_DAT2 (float UV corners) into a DISP_SPRT2.  The UVs are scaled
// by 16 (GS 4-bit u/v fraction) and truncated to u_short.

void CopySprDToSpr2(DISP_SPRT2 *s, SPRT_DAT2 *d)
{
    s->att = 0;
    s->u1 = (u_short)(int)(d->u1 * 16.0);
    s->v1 = (u_short)(int)(d->v1 * 16.0);
    s->u2 = (u_short)(int)(d->u2 * 16.0);
    s->v2 = (u_short)(int)(d->v2 * 16.0);
    s->w = d->w;
    s->z = 0xfffff - (d->pri & 0xfffff);
    s->h = d->h;
    s->x = d->x;
    s->y = d->y;
    s->sch = 1.0;
    s->tex0 = d->tex0;
    s->tex1 = SCE_GS_SET_TEX1(1, 0, 1, 5, 0, 0, 0);
    s->texa = SCE_GS_SET_TEXA(0, 1, 0x80);
    s->alpreg = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
    s->zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 1);
    s->test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    s->clmp = SCE_GS_SET_CLAMP(SCE_GS_CLAMP_CLAMP, SCE_GS_CLAMP_CLAMP, 0, 0, 0, 0);
    s->gftg = SCE_GS_SET_PRIM(SCE_GS_PRIM_TRISTRIP, 0, 1, 0, 1, 0, 1, 0, 0);
    s->pri = d->pri;
    s->b = 0x80;
    s->crx = 0.0;
    s->cry = 0.0;
    s->csx = 0.0;
    s->csy = 0.0;
    s->scw = 1.0;
    s->rot = 0.0;
    s->r = 0x80;
    s->g = 0x80;
    s->alp = d->alpha;
    s->col = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Draw a single-texture sprite.  Corners are taken relative to screen centre
// (320,224), scaled about the scale-centre (csx,csy), rotated about the
// rotate-centre (crx,cry), shifted into GS coordinate space (+2048), and
// (when interlaced) halved in Y.  The resulting GIF packet sets TEX0/TEX1 then
// emits 4 STQ/RGBAQ/XYZ verts via the PK2D ring.

void DispSprD(DISP_SPRT *s)
{
    u_int ui;
    int i;
    int psm;
    float ss;
    float cc;
    float div;
    u_int matt;
    u_int mu;
    u_int mv;
    float mw;
    float mh;
    float mx;
    float my;
    float mcrx;
    float mcry;
    float mcsx;
    float mcsy;
    u_int mz;
    float msw;
    float msh;
    float mrot;
    u_long mtex0;
    u_long mtex1;
    u_long mtexa;
    u_long malpr;
    u_long mzbuf;
    u_long mtest;
    u_long mgftg;
    u_char mr;
    u_char mg;
    u_char mb;
    u_char ma;
    u_char mlud;
    float x[4];
    float y[4];
    float x2[4];
    float y2[4];
    u_int xx[4];
    u_int yy[4];
    u_int uu[4];
    u_int vv[4];
    sceVu0IVECTOR itmp;
    float ftmp[4];
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pp;
    float render_xy[8];
    float render_uv[8];
    int n;

    matt = s->att;
    mu = s->u;
    mv = s->v;
    mw = (float)s->w;
    mtex0 = s->tex0;
    mh = (float)s->h;
    y[1] = s->y - 224.0;
    x[2] = s->x - 320.0;
    mcrx = s->crx - 320.0;
    mcry = s->cry - 224.0;
    mlud = s->col;
    mcsy = s->csy - 224.0;
    mcsx = s->csx - 320.0;
    mz = s->z;
    mrot = (s->rot * 3.1415926) / 180.0;
    div = 0.5;
    mtex1 = s->tex1;
    msw = s->scw;
    msh = s->sch;
    mtexa = s->texa;
    malpr = s->alphar;
    mzbuf = s->zbuf;
    mtest = s->test;
    mgftg = s->gftg;
    mr = s->r;
    mg = s->g;
    mb = s->b;
    ma = s->alpha;

    if (gInterlace == 0)
    {
        div = 1.0;
    }

    psm = ((sceGsTex0 *)&mtex0)->PSM;
    x[1] = x[2] + mw;

    if (msw == 1.0)
    {
        x[0] = x[2];
    }
    else
    {
        x[0] = (x[2] - mcsx) * msw + mcsx;
        x[1] = (x[1] - mcsx) * msw + mcsx;
        x[2] = x[0];
    }

    x[3] = x[1];
    y[2] = y[1] + mh;

    if (msh == 1.0)
    {
        y[0] = y[1];
    }
    else
    {
        y[0] = (y[1] - mcsy) * msh + mcsy;
        y[2] = (y[2] - mcsy) * msh + mcsy;
        y[1] = y[0];
    }

    y[3] = y[2];
    x[0] = x[2];
    y[0] = y[1];

    i = 0;
    do
    {
        x[i] = x[i] - mcrx;
        y[i] = y[i] - mcry;
        i = i + 1;
    } while (i < 4);

    if (mrot == 0.0)
    {
        x2[0] = x[0];
        y2[0] = y[0];
        x2[1] = x[1];
        y2[1] = y[1];
        x2[2] = x[2];
        y2[2] = y[2];
        x2[3] = x[3];
        y2[3] = y[3];
    }
    else
    {
        ss = sinf(mrot);
        cc = cosf(mrot);
        x2[0] = x[0] * cc - y[0] * ss;
        y2[0] = x[0] * ss + y[0] * cc;
        x2[1] = x[1] * cc - y[1] * ss;
        y2[1] = x[1] * ss + y[1] * cc;
        x2[2] = x[2] * cc - y[2] * ss;
        y2[2] = x[2] * ss + y[2] * cc;
        x2[3] = x[3] * cc - y[3] * ss;
        y2[3] = x[3] * ss + y[3] * cc;
    }

    for (i = 0; i < 4; i++)
    {
        x2[i] = x2[i] + mcrx;
        y2[i] = y2[i] + mcry;
    }

    for (i = 0; i < 4; i++)
    {
        ftmp[0] = x2[i] + 2048.0;
        ftmp[1] = y2[i] * div + 2048.0;
        _ftoi4(itmp, ftmp);
        xx[i] = itmp[0];
        yy[i] = itmp[1];
    }

    ftmp[0] = mw;
    ftmp[1] = mh;
    _ftoi0(itmp, ftmp);
    uu[3] = (itmp[0] + mu) * 0x10;
    vv[3] = (itmp[1] + mv) * 0x10;
    uu[2] = mu << 4;

    if ((matt & 2) != 0)
    {
        uu[2] = uu[3];
        uu[3] = mu << 4;
    }

    vv[1] = mv << 4;

    if ((matt & 1) != 0)
    {
        vv[1] = vv[3];
        vv[3] = mv << 4;
    }

    {
        DRAW_ENV_5 env;

        env.alpha = malpr;
        env.clamp = 5;
        env.tex1 = mtex1;
        env.test = mtest;
        env.zbuf = mzbuf;
        uu[0] = uu[2];
        uu[1] = uu[3];
        vv[0] = vv[1];
        vv[2] = vv[3];
        SetDrawEnv(0, &env);
    }

    for (i = 0; i < 4; i++)
    {
        render_xy[i * 2 + 0] = x2[i] + 320.0f;
        render_xy[i * 2 + 1] = y2[i] + 224.0f;
        render_uv[i * 2 + 0] = (float)uu[i] / 16.0f;
        render_uv[i * 2 + 1] = (float)vv[i] / 16.0f;
    }
    // PRIM bit 4 is TME.  A sprite drawn with it clear is a flat blended fill,
    // not an image -- SubNega()'s first pass is one, and so is SubContrast3().
    // Looking a texture up for those means resolving whatever tex0 happens to
    // hold (0, i.e. GS address 0) and painting the frame with it.
    if ((mgftg & 0x10) == 0)
    {
        u_char render_rgba[4 * 4];
        int    c;

        for (c = 0; c < 4; c++)
        {
            render_rgba[c * 4 + 0] = mr;
            render_rgba[c * 4 + 1] = mg;
            render_rgba[c * 4 + 2] = mb;
            render_rgba[c * 4 + 3] = ma;
        }
        MioPan_RendererSetGs2dDepth(mz);
        MioPan_RendererDrawSolidQuad(render_xy, render_rgba);
    }
    else
    {
        /* Host: the primitive's own Z, to go with the TEST and ZBUF the
         * draw env above pushed -- see MioPan_RendererSetGs2dDepth(). */
        MioPan_RendererSetGs2dDepth(mz);
        MioPan_RendererDrawTexturedQuad((sceGsTex0 *)&mtex0,
                                        (sceGsTex1 *)&mtex1,
                                        render_xy, render_uv, mr, mg, mb, ma);
    }

    SetTexaRegister(mtexa);
    pbuf = GetPK2Dbuf();
    // A+D GIFtag: 4 register writes (TEXFLUSH, TEX0_1, [TEX0_1 / pad], TEXCLUT).
    pbuf->ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf->ul64[1] = SCE_GIF_PACKED_AD;
    pbuf[1].ul64[1] = SCE_GS_TEXFLUSH;
    pbuf[1].ul64[0] = 0;

    if (((psm == SCE_GS_PSMT4) || (psm == SCE_GS_PSMT4HH)) || (psm == SCE_GS_PSMT4HL))
    {
        // CSM/CLUT-bearing PSM: split TEX0 (with CLD/CSM bits) + the raw word.
        sceGsTex0 *tex0 = (sceGsTex0 *)&mtex0;
        pbuf[2].ul64[0] = SCE_GS_SET_TEX0(tex0->TBP0, tex0->TBW, SCE_GS_PSMT8, tex0->TW, tex0->TH, tex0->TCC, tex0->TFX, tex0->CBP, tex0->CPSM, tex0->CSM, 0, 1);
        pbuf[2].ul64[1] = SCE_GS_TEX0_1;
        pbuf[3].ul64[0] = SCE_GS_SET_TEX0(tex0->TBP0, tex0->TBW, tex0->PSM, tex0->TW, tex0->TH, tex0->TCC, tex0->TFX, tex0->CBP, tex0->CPSM, tex0->CSM, 0, 0);
        pbuf[3].ul64[1] = SCE_GS_TEX0_1;
    }
    else
    {
        pbuf[2].ul64[1] = SCE_GS_TEX0_1;
        pbuf[2].ul64[0] = mtex0;
        pbuf[3].ul64[1] = 0x7f;          // A+D address 0x7f = ignored (pad write)
        pbuf[3].ul64[0] = 0;
    }

    n = 6;
    pbuf[4].ul64[1] = SCE_GS_TEXCLUT;
    pbuf[4].ul64[0] = SCE_GS_SET_TEXCLUT(4, 0, (u_int)mlud);

    // PACKED REGS: 4 vertices, each {UV, RGBAQ, XYZF2}.
    pbuf[5].ul64[1] = (u_long)SCE_GIF_PACKED_UV
                    | ((u_long)SCE_GIF_PACKED_RGBAQ <<  4) | ((u_long)SCE_GIF_PACKED_XYZF2 <<  8)
                    | ((u_long)SCE_GIF_PACKED_UV    << 12) | ((u_long)SCE_GIF_PACKED_RGBAQ << 16)
                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 20) | ((u_long)SCE_GIF_PACKED_UV    << 24)
                    | ((u_long)SCE_GIF_PACKED_RGBAQ << 28) | ((u_long)SCE_GIF_PACKED_XYZF2 << 32)
                    | ((u_long)SCE_GIF_PACKED_UV    << 36) | ((u_long)SCE_GIF_PACKED_RGBAQ << 40)
                    | ((u_long)SCE_GIF_PACKED_XYZF2 << 44);

    // vertex GIFtag: PRIM supplied by gftg (textured gouraud tristrip), NREG=12.
    pbuf[5].ul64[0] = SCE_GIF_SET_TAG(1, 1, 1, mgftg, 0, 0xc);
    pp = pbuf + 6;

    i = 0;
    do
    {
        pp->ui32[0] = uu[i];
        n = n + 3;
        ui = vv[i];
        pp->ui32[2] = 0;
        pp->ui32[1] = ui;
        pp->ui32[3] = 0;
        pp[1].ui32[0] = (u_int)mr;
        pp[1].ui32[1] = (u_int)mg;
        pp[1].ui32[2] = (u_int)mb;
        pp[1].ui32[3] = (u_int)ma;
        pp[2].ui32[0] = xx[i];
        pp[2].ui32[1] = yy[i];
        pp[2].ui32[2] = mz;
        pp[2].ui32[3] = 0;
        pp = pp + 3;
        i = i + 1;
    } while (i < 4);

    EndPK2Dbuf(pbuf + n);
}

// ──────────────────────────────────────────────────────────────────────
// Draw a 4-corner gouraud square (no texture).  Same screen-centre / scale /
// rotate pipeline as DispSprD but with the no-texture draw env and a
// per-vertex RGBAQ + XYZ packet.

void DispSqrD(DISP_SQAR *s)
{
    int i;
    float ss;
    float cc;
    float div;
    float mx[4];
    float my[4];
    float mcrx;
    float mcry;
    float mcsx;
    float mcsy;
    u_int mz;
    float msw;
    float msh;
    float mrot;
    u_long mtexa;
    u_long malpr;
    u_long mzbuf;
    u_long mtest;
    u_char mr[4];
    u_char mg[4];
    u_char mb[4];
    u_char ma;
    float x[4];
    float y[4];
    float x2[4];
    float y2[4];
    u_int xx[4];
    u_int yy[4];
    float ftmp[4];
    sceVu0IVECTOR itmp;
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pp;
    float render_xy[8];
    unsigned char render_rgba[16];

    mcrx = s->crx - 320.0;
    mcry = s->cry - 224.0;
    mcsx = s->csx - 320.0;
    mcsy = s->csy - 224.0;

    for (i = 0; i < 4; i++)
    {
        mx[i] = (float)s->x[i] - 320.0;
        my[i] = (float)s->y[i] - 224.0;
    }

    mz = s->z;
    msw = s->scw;
    msh = s->sch;
    mrot = s->rot;
    mtexa = s->texa;
    malpr = s->alphar;
    mzbuf = s->zbuf;
    mtest = s->test;

    for (i = 0; i < 4; i++)
    {
        mr[i] = s->r[i];
        mg[i] = s->g[i];
        mb[i] = s->b[i];
    }

    div = 0.5;
    ma = s->alpha;

    if (gInterlace == 0)
    {
        div = 1.0;
    }

    if (msw == 1.0)
    {
        for (i = 0; i < 4; i++)
        {
            x[i] = mx[i];
        }
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            x[i] = (mx[i] - mcsx) * msw + mcsx;
        }
    }

    if (msh == 1.0)
    {
        for (i = 0; i < 4; i++)
        {
            y[i] = my[i];
        }
    }
    else
    {
        for (i = 0; i < 4; i++)
        {
            y[i] = (my[i] - mcsy) * msh + mcsy;
        }
    }

    ss = sinf((mrot * 3.1415926) / 180.0);
    cc = cosf((mrot * 3.1415926) / 180.0);

    for (i = 0; i < 4; i++)
    {
        x[i] = x[i] - mcrx;
        y[i] = y[i] - mcry;
    }

    if (mrot == 0.0)
    {
        x2[0] = x[0];
        y2[0] = y[0];
        x2[1] = x[1];
        y2[1] = y[1];
        x2[2] = x[2];
        y2[2] = y[2];
        x2[3] = x[3];
        y2[3] = y[3];
    }
    else
    {
        x2[0] = x[0] * cc - y[0] * ss;
        y2[0] = x[0] * ss + y[0] * cc;
        x2[1] = x[1] * cc - y[1] * ss;
        y2[1] = x[1] * ss + y[1] * cc;
        x2[2] = x[2] * cc - y[2] * ss;
        y2[2] = x[2] * ss + y[2] * cc;
        x2[3] = x[3] * cc - y[3] * ss;
        y2[3] = x[3] * ss + y[3] * cc;
    }

    for (i = 0; i < 4; i++)
    {
        x2[i] = x2[i] + mcrx;
        y2[i] = y2[i] + mcry;
    }

    for (i = 0; i < 4; i++)
    {
        ftmp[0] = x2[i] + 2048.0;
        ftmp[1] = y2[i] * div + 2048.0;
        _ftoi4(itmp, ftmp);
        xx[i] = itmp[0];
        yy[i] = itmp[1];
    }

    {
        DRAW_ENV_NOTEX env;

        env.alpha = malpr;
        env.test = mtest;
        env.zbuf = mzbuf;
        SetDrawEnvNoTex(0, &env);
    }

    for (i = 0; i < 4; i++)
    {
        render_xy[i * 2 + 0] = x2[i] + 320.0f;
        render_xy[i * 2 + 1] = y2[i] + 224.0f;
        render_rgba[i * 4 + 0] = mr[i];
        render_rgba[i * 4 + 1] = mg[i];
        render_rgba[i * 4 + 2] = mb[i];
        render_rgba[i * 4 + 3] = ma;
    }
    /* Host: the primitive's own Z, to go with the TEST and ZBUF the
     * draw env above pushed -- see MioPan_RendererSetGs2dDepth(). */
    MioPan_RendererSetGs2dDepth(mz);
    MioPan_RendererDrawSolidQuad(render_xy, render_rgba);

    SetTexaRegister(mtexa);
    pbuf = GetPK2Dbuf();

    // gouraud tristrip (no texture), abe; 4 vertices of {RGBAQ, XYZF2}.
    pbuf->ul64[0] = SCE_GIF_SET_TAG(1, 1, 1, SCE_GS_SET_PRIM(SCE_GS_PRIM_TRISTRIP, 1, 0, 0, 1, 0, 0, 0, 0), 0, 8);

    pbuf->ul64[1] = ((u_long)SCE_GIF_PACKED_RGBAQ <<  0) | ((u_long)SCE_GIF_PACKED_XYZF2 <<  4)
                  | ((u_long)SCE_GIF_PACKED_RGBAQ <<  8) | ((u_long)SCE_GIF_PACKED_XYZF2 << 12)
                  | ((u_long)SCE_GIF_PACKED_RGBAQ << 16) | ((u_long)SCE_GIF_PACKED_XYZF2 << 20)
                  | ((u_long)SCE_GIF_PACKED_RGBAQ << 24) | ((u_long)SCE_GIF_PACKED_XYZF2 << 28);
                  
    pp = pbuf;

    for (i = 0; i < 4; i++)
    {
        pp[i].ui32[0] = (u_int)mr[i];
        pp[i].ui32[1] = (u_int)mg[i];
        pp[i].ui32[2] = (u_int)mb[i];
        pp[i].ui32[3] = (u_int)ma;
        pp[i].ui32[4] = xx[i];
        pp[i].ui32[5] = yy[i];
        pp[i].ui32[6] = mz;
        pp[i].ui32[7] = 0;
    }
    
    EndPK2Dbuf(pbuf + 9);
}

// ──────────────────────────────────────────────────────────────────────
// Draw a single-texture sprite with explicit UV corners (DISP_SPRT2).  Same
// transform pipeline as DispSprD; the difference is the UVs are pre-stored
// 12.4 corners that flip with the att bits rather than being derived from w/h.

void DispSprD2(DISP_SPRT2 *s)
{
    u_int ui;
    int i;
    int psm;
    float ss;
    float cc;
    float div;
    u_int matt;
    u_int mu1;
    u_int mv1;
    u_int mu2;
    u_int mv2;
    float mw;
    float mh;
    float mx;
    float my;
    float mcrx;
    float mcry;
    float mcsx;
    float mcsy;
    u_int mz;
    float msw;
    float msh;
    float mrot;
    u_long mtex0;
    u_long mtex1;
    u_long mtexa;
    u_long malpr;
    u_long mzbuf;
    u_long mtest;
    u_long mgftg;
    u_long mclmp;
    u_char mr;
    u_char mg;
    u_char mb;
    u_char ma;
    u_char mlud;
    float x[4];
    float y[4];
    float x2[4];
    float y2[4];
    u_int xx[4];
    u_int yy[4];
    u_int uu[4];
    u_int vv[4];
    float ww;
    float hh;
    sceVu0IVECTOR itmp;
    float ftmp[4];
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pp;
    float render_xy[8];
    float render_uv[8];
    int n;

    mtex0 = s->tex0;
    y[1] = s->y - 224.0;
    x[2] = s->x - 320.0;
    mcrx = s->crx - 320.0;
    mcry = s->cry - 224.0;
    mlud = s->col;
    mcsy = s->csy - 224.0;
    mcsx = s->csx - 320.0;
    matt = s->att;
    mrot = (s->rot * 3.1415926) / 180.0;
    div = 0.5;
    mu1 = (u_int)s->u1;
    mv1 = (u_int)(u_short)s->v1;
    mu2 = (u_int)s->u2;
    mv2 = (u_int)(u_short)s->v2;
    mz = s->z;
    mtex1 = s->tex1;
    msw = s->scw;
    msh = s->sch;
    mtexa = s->texa;
    malpr = s->alpreg;
    mzbuf = s->zbuf;
    mtest = s->test;
    mgftg = s->gftg;
    mclmp = s->clmp;
    mr = s->r;
    mg = s->g;
    mb = s->b;
    ma = s->alp;
    if (gInterlace == 0)
    {
        div = 1.0;
    }
    psm = ((sceGsTex0 *)&mtex0)->PSM;
    if (msw == 1.0)
    {
        x[1] = x[2] + s->w * msw;
        x[0] = x[2];
    }
    else
    {
        x[0] = mcsx - (mcsx - x[2]) * msw;
        x[1] = mcsx + ((x[2] + s->w) - mcsx) * msw;
        x[2] = x[0];
    }
    x[3] = x[1];
    if (msh == 1.0)
    {
        y[2] = y[1] + s->h * msh;
        y[0] = y[1];
    }
    else
    {
        y[0] = mcsy - (mcsy - y[1]) * msh;
        y[2] = mcsy + ((y[1] + s->h) - mcsy) * msh;
        y[1] = y[0];
    }
    y[3] = y[2];
    x[0] = x[2];
    y[0] = y[1];
    i = 0;
    do
    {
        x[i] = x[i] - mcrx;
        y[i] = y[i] - mcry;
        i = i + 1;
    } while (i < 4);
    if (mrot == 0.0)
    {
        x2[0] = x[0];
        y2[0] = y[0];
        x2[1] = x[1];
        y2[1] = y[1];
        x2[2] = x[2];
        y2[2] = y[2];
        x2[3] = x[3];
        y2[3] = y[3];
    }
    else
    {
        ss = sinf(mrot);
        cc = cosf(mrot);
        x2[0] = x[0] * cc - y[0] * ss;
        y2[0] = x[0] * ss + y[0] * cc;
        x2[1] = x[1] * cc - y[1] * ss;
        y2[1] = x[1] * ss + y[1] * cc;
        x2[2] = x[2] * cc - y[2] * ss;
        y2[2] = x[2] * ss + y[2] * cc;
        x2[3] = x[3] * cc - y[3] * ss;
        y2[3] = x[3] * ss + y[3] * cc;
    }
    i = 0;
    do
    {
        x2[i] = x2[i] + mcrx;
        y2[i] = y2[i] + mcry;
        i = i + 1;
    } while (i < 4);
    i = 0;
    do
    {
        ftmp[0] = x2[i] + 2048.0;
        ftmp[1] = y2[i] * div + 2048.0;
        _ftoi4(itmp, ftmp);
        xx[i] = itmp[0];
        yy[i] = itmp[1];
        i = i + 1;
    } while (i < 4);
    vv[1] = mv1;
    vv[3] = mv2;
    if ((matt & 2) != 0)
    {
        uu[3] = mu1;
        uu[2] = mu2;
    }
    else
    {
        uu[3] = mu2;
        uu[2] = mu1;
    }
    if ((matt & 1) != 0)
    {
        vv[1] = mv2;
        vv[3] = mv1;
    }

    {
        DRAW_ENV_5 env;

        env.alpha = malpr;
        env.tex1 = mtex1;
        env.clamp = mclmp;
        env.test = mtest;
        env.zbuf = mzbuf;
        uu[0] = uu[2];
        uu[1] = uu[3];
        vv[0] = vv[1];
        vv[2] = vv[3];
        SetDrawEnv(0, &env);
    }

    for (i = 0; i < 4; i++)
    {
        render_xy[i * 2 + 0] = x2[i] + 320.0f;
        render_xy[i * 2 + 1] = y2[i] + 224.0f;
        render_uv[i * 2 + 0] = (float)uu[i] / 16.0f;
        render_uv[i * 2 + 1] = (float)vv[i] / 16.0f;
    }
    // PRIM bit 4 is TME.  A sprite drawn with it clear is a flat blended fill,
    // not an image -- SubNega()'s first pass is one, and so is SubContrast3().
    // Looking a texture up for those means resolving whatever tex0 happens to
    // hold (0, i.e. GS address 0) and painting the frame with it.
    if ((mgftg & 0x10) == 0)
    {
        u_char render_rgba[4 * 4];
        int    c;

        for (c = 0; c < 4; c++)
        {
            render_rgba[c * 4 + 0] = mr;
            render_rgba[c * 4 + 1] = mg;
            render_rgba[c * 4 + 2] = mb;
            render_rgba[c * 4 + 3] = ma;
        }
        MioPan_RendererSetGs2dDepth(mz);
        MioPan_RendererDrawSolidQuad(render_xy, render_rgba);
    }
    else
    {
        /* The sprite's own CLAMP register, not a fixed clamp: SubDither3()
         * and SubDither4() set it to 0 (REPEAT/REPEAT) to tile the noise
         * sheet across the frame.  Every other DispSprD2() caller keeps
         * CopySprDToSpr2()'s CLAMP/CLAMP default and is unaffected. */
        MioPan_RendererSetGs2dDepth(mz);
        MioPan_RendererDrawTexturedQuadClamp((sceGsTex0 *)&mtex0,
                                             (sceGsTex1 *)&mtex1,
                                             render_xy, render_uv, mclmp,
                                             mr, mg, mb, ma);
    }

    SetTexaRegister(mtexa);
    pbuf = GetPK2Dbuf();
    // A+D GIFtag: 4 register writes (TEXFLUSH, TEX0_1, [TEX0_1 / pad], TEXCLUT).
    pbuf->ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf->ul64[1] = SCE_GIF_PACKED_AD;
    pbuf[1].ul64[1] = SCE_GS_TEXFLUSH;
    pbuf[1].ul64[0] = 0;

    if (((psm == SCE_GS_PSMT4) || (psm == SCE_GS_PSMT4HH)) || (psm == SCE_GS_PSMT4HL))
    {
        sceGsTex0 *tex0 = (sceGsTex0 *)&mtex0;

        pbuf[2].ul64[0] = SCE_GS_SET_TEX0(tex0->TBP0, tex0->TBW, SCE_GS_PSMT8, tex0->TW, tex0->TH, tex0->TCC, tex0->TFX, tex0->CBP, tex0->CPSM, tex0->CSM, 0, SCE_GS_CLD_LOAD);
        pbuf[2].ul64[1] = SCE_GS_TEX0_1;
        pbuf[3].ul64[0] = SCE_GS_SET_TEX0(tex0->TBP0, tex0->TBW, tex0->PSM, tex0->TW, tex0->TH, tex0->TCC, tex0->TFX, tex0->CBP, tex0->CPSM, tex0->CSM, 0, SCE_GS_CLD_NO_LOAD);
        pbuf[3].ul64[1] = SCE_GS_TEX0_1;
    }
    else
    {
        pbuf[2].ul64[1] = SCE_GS_TEX0_1;
        pbuf[2].ul64[0] = mtex0;
        pbuf[3].ul64[1] = 0x7f;          // A+D address 0x7f = ignored (pad write)
        pbuf[3].ul64[0] = 0;
    }

    n = 6;
    pbuf[4].ul64[1] = SCE_GS_TEXCLUT;
    pbuf[4].ul64[0] = SCE_GS_SET_TEXCLUT(4, 0, (u_int)mlud);

    // PACKED REGS: 4 vertices, each {UV, RGBAQ, XYZF2}.
    pbuf[5].ul64[1] = 0 
                       | ((u_long)SCE_GIF_PACKED_UV    <<  0) | ((u_long)SCE_GIF_PACKED_RGBAQ <<  4) | ((u_long)SCE_GIF_PACKED_XYZF2 <<  8) 
                       | ((u_long)SCE_GIF_PACKED_UV    << 12) | ((u_long)SCE_GIF_PACKED_RGBAQ << 16) | ((u_long)SCE_GIF_PACKED_XYZF2 << 20) 
                       | ((u_long)SCE_GIF_PACKED_UV    << 24) | ((u_long)SCE_GIF_PACKED_RGBAQ << 28) | ((u_long)SCE_GIF_PACKED_XYZF2 << 32) 
                       | ((u_long)SCE_GIF_PACKED_UV    << 36) | ((u_long)SCE_GIF_PACKED_RGBAQ << 40) | ((u_long)SCE_GIF_PACKED_XYZF2 << 44);

    // vertex GIFtag: PRIM supplied by gftg (textured gouraud tristrip), NREG=12.
    pbuf[5].ul64[0] = SCE_GIF_SET_TAG(1, 1, 1, mgftg, 0, 0xc);
    pp = pbuf + 6;
    i = 0;
    do
    {
        n = n + 3;
        ui = vv[i];

        pp->ui32[0] = uu[i];
        pp->ui32[2] = 0;
        pp->ui32[1] = ui;
        pp->ui32[3] = 0;
        pp[1].ui32[0] = (u_int)mr;
        pp[1].ui32[1] = (u_int)mg;
        pp[1].ui32[2] = (u_int)mb;
        pp[1].ui32[3] = (u_int)ma;
        pp[2].ui32[0] = xx[i];
        pp[2].ui32[1] = yy[i];
        pp[2].ui32[2] = mz;
        pp[2].ui32[3] = 0;
        pp = pp + 3;
        i = i + 1;
    } while (i < 4);
    EndPK2Dbuf(pbuf + n);
}

// ──────────────────────────────────────────────────────────────────────
// Fill a screen rectangle.  Seeds a DISP_SQAR from a fixed full-screen
// SQAR_DAT template (640x448, alpha 128), then overrides the rect, depth,
// colour and alpha from the args and draws it.

void SetPanel(u_int pri, float x1, float y1, float x2, float y2,
              u_char r, u_char g, u_char b, u_char a)
{
    int i;
    SQAR_DAT sq;
    DISP_SQAR dq;

    // full-screen template: w=640 h=448 x=0 y=0 pri=0 rgb=0 alpha=128
    sq.w = 640;
    sq.h = 448;
    sq.x = 0;
    sq.y = 0;
    sq.pri = 0;
    sq.r = 0;
    sq.g = 0;
    sq.b = 0;
    sq.alpha = 0x80;
    CopySqrDToSqr(&dq, &sq);
    dq.x[0] = (int)x1;
    dq.z = 0xfffff - (pri & 0xfffff);
    dq.y[0] = (int)y1;
    dq.x[1] = dq.x[0] + (int)(x2 - x1);
    dq.zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, dq.z);
    dq.y[2] = dq.y[0] + (int)(y2 - y1);

    for (i = 0; i < 4; i++)
    {
        dq.r[i] = r;
        dq.g[i] = g;
        dq.b[i] = b;
    }

    dq.x[2] = dq.x[0];
    dq.x[3] = dq.x[1];
    dq.y[1] = dq.y[0];
    dq.y[3] = dq.y[2];
    dq.pri = pri;
    dq.alpha = a;
    DispSqrD(&dq);
}

// ──────────────────────────────────────────────────────────────────────
// Local image copy (GS LOCAL <-> EE HOST) helpers.

// Byte size of the EE buffer for a given local-copy "type".
int LocalCopyLtoBGetSize(int type)
{
    int size;

    size = 0x46000;
    if (type != 1)
    {
        if (type < 2)
        {
            if (type == 0)
            {
                return 0x118000;
            }
        }
        else
        {
            if (type == 2)
            {
                return 0x8c000;
            }
            if (type == 3)
            {
                return 0x8c000;
            }
        }
        PRINT_ASSERT("LocalCopyLtoBGetSize() type is Illegal");
        size = 0;
    }
    return size;
}

// ──────────────────────────────────────────────────────────────────────
// HOST BRIDGE for the LocalCopy* family.
//
// The GIF packets these build go into the PK2D ring and dmaVif1 throws them
// away, so on the host the copies are re-issued to the renderer, which keeps a
// capture texture per GS block.  An effect that later samples that block as a
// texture is served from the capture -- see MioPan_RendererCaptureGsBlock().
//
// Only the direction the effects actually use is bridged: frame buffer (or a
// previous capture) into a scratch page.  A copy whose *destination* is a frame
// buffer is a blit back onto the screen -- pause.c and photo.c do that -- and
// is a different operation the renderer has its own path for
// (MioPan_RendererDrawCapturedScreen); those are left alone.

// GS block addresses of the two 640x448 display buffers.
#define G2D_IS_FRAMEBUFFER_BLOCK(a) ((a) == 0x0000 || (a) == 0x1180)

// Destination rect of a LocalCopyLtoB / LocalCopyBtoL "type", matching the
// sceGsSetDefStoreImage / BITBLTBUF strips the two builders emit above.
static int LocalCopyHostBufSize(int type, int *out_w, int *out_h)
{
    switch (type)
    {
    case 0: *out_w = 640; *out_h = 448; return 1;
    case 1: *out_w = 320; *out_h = 224; return 1;
    case 2: *out_w = 320; *out_h = 448; return 1;
    case 3: *out_w = 640; *out_h = 224; return 1;
    default: return 0;
    }
}

// ──────────────────────────────────────────────────────────────────────
// PORT-ONLY: the screen-page registry, and the read-back that uses it.
//
// A capture slot answers "draw this page", not "give me its pixels".  Three
// callers need the pixels in EE memory instead -- photo.c's PictureCapture(),
// SpriteCmn.c's SpCmnGetScreen() and menu.c's backdrop grab -- and they read
// the buffer in the same call, so the GPU-side slot cannot serve them and
// neither can emulated GS memory, which no draw ever writes into.  That is
// what made a saved photograph a page of unrelated VRAM.
//
// MioPan_RendererReadbackScreen() supplies the pixels; this registry supplies
// the geometry.  Every page the game copies the screen into records *which*
// screen rectangle went in and at what size, because a LOCAL->HOST store names
// only the page: 0x2bc0 is the whole screen squashed 2:1 right after
// LocalCopyLtoL(5, ...), and the 45x15 album thumbnail right after
// MakeSmallPhotoV(), and the store that reads them is the same one.
//
// Small and fixed: at most a handful of pages are ever live (0x2bc0, 0x3aa0,
// 0x3480), and the oldest entry is recycled rather than growing the table.

typedef struct _G2D_SCREEN_PAGE
{
    int block;                          // GS block address, -1 when free
    int src_x, src_y, src_w, src_h;     // screen rect, in 640x448 frame units
    int dst_w, dst_h;                   // size it was rendered into the page at
    unsigned int seq;                   // for recycling the oldest entry
} G2D_SCREEN_PAGE;

#define G2D_SCREEN_PAGE_MAX 8

static G2D_SCREEN_PAGE g2d_screen_page[G2D_SCREEN_PAGE_MAX];
static unsigned int    g2d_screen_page_seq;
static int             g2d_screen_page_init;

static void G2dScreenPageInit(void)
{
    int i;

    if (g2d_screen_page_init)
    {
        return;
    }
    for (i = 0; i < G2D_SCREEN_PAGE_MAX; i++)
    {
        g2d_screen_page[i].block = -1;
    }
    g2d_screen_page_init = 1;
}

static G2D_SCREEN_PAGE *G2dScreenPageFind(int block)
{
    int i;

    G2dScreenPageInit();
    for (i = 0; i < G2D_SCREEN_PAGE_MAX; i++)
    {
        if (g2d_screen_page[i].block == block)
        {
            return &g2d_screen_page[i];
        }
    }
    return (G2D_SCREEN_PAGE *)0;
}

void G2dRegisterScreenPage(int block, int src_x, int src_y, int src_w,
                           int src_h, int dst_w, int dst_h)
{
    G2D_SCREEN_PAGE *slot;
    int i;

    if (dst_w <= 0 || dst_h <= 0 || src_w <= 0 || src_h <= 0)
    {
        return;
    }

    slot = G2dScreenPageFind(block);
    if (slot == (G2D_SCREEN_PAGE *)0)
    {
        slot = G2dScreenPageFind(-1);
    }
    if (slot == (G2D_SCREEN_PAGE *)0)
    {
        slot = &g2d_screen_page[0];
        for (i = 1; i < G2D_SCREEN_PAGE_MAX; i++)
        {
            if (g2d_screen_page[i].seq < slot->seq)
            {
                slot = &g2d_screen_page[i];
            }
        }
    }

    slot->block = block;
    slot->src_x = src_x;
    slot->src_y = src_y;
    slot->src_w = src_w;
    slot->src_h = src_h;
    slot->dst_w = dst_w;
    slot->dst_h = dst_h;
    slot->seq   = ++g2d_screen_page_seq;
}

void G2dForgetScreenPage(int block)
{
    G2D_SCREEN_PAGE *slot = G2dScreenPageFind(block);

    if (slot != (G2D_SCREEN_PAGE *)0)
    {
        slot->block = -1;
    }
}

// PORT-ONLY: carry out a HOST->LOCAL image transfer.
//
// The GIF packet its caller builds goes into the PK2D ring, which dmaVif1 drops
// -- so an upload only reaches emulated GS memory if it is re-issued here.
// tim2.c already does exactly this for texture uploads (Tim2HostUpload); this
// is the same bridge for the two builders that had none, and the reason a photo
// drawn back out of the album sampled unrelated VRAM.
//
// Deliberately per call site rather than a generic executor over the ring's
// last packet: tim2.c would then upload twice, and its packet carries TRXREG's
// width and height the other way round from the bridge beside it, so the second
// upload scrambles every non-square texture.
void G2dHostUpload(int dbp, int dbw, int dpsm, int dsax, int dsay,
                   int w, int h, void *src)
{
    sceGsLoadImage li;

    if (src == (void *)0 || w <= 0 || h <= 0)
    {
        return;
    }

    sceGsSetDefLoadImage(&li, (short)dbp, (short)dbw, (short)dpsm,
                         (short)dsax, (short)dsay, (short)w, (short)h);
    MioPan_GsUpload(&li, (unsigned char *)src);

    // The page no longer holds a copy of the screen, whatever it held before.
    G2dForgetScreenPage(dbp);
}

// Render a screen rectangle straight into emulated GS memory as PSMCT32.
//
// For a page the game *samples as a texture* rather than reads back into EE
// memory, the registry is not enough -- the draw goes through the ordinary
// texture path, which decodes GS memory.  DrawSpecialFurnPhoto() is the case:
// it re-draws the frame into the photo page with a GIF packet the host ring
// drops, and DispPhotoFrame1() then samples that page.  Materialising the
// result here is what puts a picture inside the frame instead of noise.
//
// Not something to do per frame -- it stalls on the read-back and then swizzles
// the result into GS memory -- but the callers are once-per-photograph.
int G2dScreenToGsPage(int block, int dbw_pages, int src_x, int src_y,
                      int src_w, int src_h, int dst_w, int dst_h)
{
    // 640x448 PSMCT32 is the largest page any caller builds, and one static
    // buffer keeps this off the game's heap, which the photo phase has already
    // carved up.
    static u_char  scratch[640 * 448 * 4];
    sceGsLoadImage load;
    u_long         reg;

    if (dst_w <= 0 || dst_h <= 0 ||
        (size_t)dst_w * (size_t)dst_h * 4 > sizeof(scratch))
    {
        return 0;
    }

    if (MioPan_RendererReadbackScreen(scratch, dst_w, src_x, src_y,
                                      src_w, src_h, dst_w, dst_h) == 0)
    {
        return 0;
    }

    memset(&load, 0, sizeof(load));
    reg = SCE_GS_SET_BITBLTBUF(0, 0, 0, block, dbw_pages, SCE_GS_PSMCT32);
    memcpy(&load.bitbltbuf, &reg, sizeof(reg));
    reg = SCE_GS_SET_TRXPOS(0, 0, 0, 0, 0);
    memcpy(&load.trxpos, &reg, sizeof(reg));
    reg = SCE_GS_SET_TRXREG(dst_w, dst_h);
    memcpy(&load.trxreg, &reg, sizeof(reg));
    reg = SCE_GS_SET_TRXDIR(0);
    memcpy(&load.trxdir, &reg, sizeof(reg));

    MioPan_GsUpload(&load, scratch);
    G2dForgetScreenPage(block);
    return 1;
}

// The two shared EE stashes LocalCopyLtoB() / LocalCopyBtoL() pick between.
// A copy that lands in one of these is the effects' frame-buffer round trip
// (EffImageHalf32 between an LtoB and a BtoL), and the renderer already carries
// that picture from capture slot to capture slot -- so the bytes are dead here
// and reading them back would spend a GPU sync per camera flash and per heavy
// ghost hit on nothing.  Every other destination is a buffer its caller owns
// and is about to read: SpCmnGetScreen(), menu.c's backdrop grab.
#define G2D_EFFECT_STASH_0  0x1e79b00
#define G2D_EFFECT_STASH_1  0x1f05b00

// Fill a `w` x `h` EE image of GS page `v_adrs` with the real screen.
//
// A display buffer is the whole frame; any other page holds the rectangle its
// registration recorded, in its top-left corner, so the rest of the buffer is
// cleared first.  Nothing reads that remainder -- CopyScreenToBuffer2() lifts
// only the rect it asked for -- but black beats leaving the store's output
// there.
static int LocalCopyScreenReadback(uintptr_t ee_adrs, int v_adrs, u_char *dst,
                                   int w, int h)
{
    const G2D_SCREEN_PAGE *page;

    if (dst == (u_char *)0 || w <= 0 || h <= 0)
    {
        return 0;
    }

    if (G2D_IS_FRAMEBUFFER_BLOCK(v_adrs))
    {
        if (ee_adrs == G2D_EFFECT_STASH_0 || ee_adrs == G2D_EFFECT_STASH_1)
        {
            return 0;
        }
        return MioPan_RendererReadbackScreen(dst, w, 0, 0, 640, 448, w, h);
    }

    page = G2dScreenPageFind(v_adrs);
    if (page == (const G2D_SCREEN_PAGE *)0)
    {
        return 0;
    }

    {
        int dw = page->dst_w < w ? page->dst_w : w;
        int dh = page->dst_h < h ? page->dst_h : h;

        if (dw <= 0 || dh <= 0)
        {
            return 0;
        }
        if (dw != w || dh != h)
        {
            memset(dst, 0, (size_t)w * (size_t)h * 4);
        }
        return MioPan_RendererReadbackScreen(dst, w, page->src_x, page->src_y,
                                             page->src_w, page->src_h, dw, dh);
    }
}

// LOCAL->HOST store: program the GS store-image descriptor(s) for the type and
// execute them into the EE buffer.
//
// ee_adrs arrives in two flavours: an EE memory-map constant from
// LocalCopyLtoB(), or a heap buffer a caller already owns (SpCmnGetScreen).
// Only the first needs resolving, and MioPan_GetHostPointer() passes the second
// through untouched -- but the read-back writes through the result either way,
// so it has to be translated before g3dGsExecStoreImage() sees it.
void LocalCopyLtoBAdrs(int type, uintptr_t ee_adrs, int v_adrs)
{
    sceGsStoreImage gs_simage[8];
    int i;
    u_char *p;
    u_char *dst;

    if (type == 1)
    {
        sceGsSetDefStoreImage(&gs_simage[0], (u_short)v_adrs, 5, 0, 0, 0, 0x140, 0xe0);
    }
    else if (type < 2)
    {
        if (type == 0)
        {
            i = 0;
            p = (u_char *)gs_simage;
            do
            {
                sceGsSetDefStoreImage((sceGsStoreImage *)p, (u_short)v_adrs, 10, 0, 0, (i * 0x400000) >> 0x10, 0x280, 0x40);
                i = i + 1;
                p = p + 0x70;
            } while (i < 7);
        }
    }
    else if (type == 2)
    {
        sceGsSetDefStoreImage(&gs_simage[0], (u_short)v_adrs, 5, 0, 0, 0, 0x140, 400);
        sceGsSetDefStoreImage(&gs_simage[1], (u_short)v_adrs, 5, 0, 0, 400, 0x140, 0x30);
    }
    else if (type == 3)
    {
        sceGsSetDefStoreImage(&gs_simage[0], (u_short)v_adrs, 10, 0, 0, 0, 0x280, 0xe0);
    }

    g3dGsSyncPath(0, 0);
    FlushCache(0);

    dst = (u_char *)MioPan_GetHostPointer(ee_adrs);

    if (type == 1)
    {
        g3dGsExecStoreImage(gs_simage, (u_long128 *)dst);
    }
    else if (type < 2)
    {
        if (type == 0)
        {
            i = 0;
            p = (u_char *)gs_simage;
            do
            {
                g3dGsExecStoreImage(p, (u_long128 *)dst);
                dst = dst + 0x28000;
                i++;
                p += 0x70;
            } while (i < 7);
        }
    }
    else if (type == 2)
    {
        g3dGsExecStoreImage(gs_simage, (u_long128 *)dst);
        g3dGsExecStoreImage(&gs_simage[1], (u_long128 *)(dst + 0x7d000));
    }
    else if (type == 3)
    {
        g3dGsExecStoreImage(gs_simage, (u_long128 *)dst);
    }

    // Host: the store above read emulated GS memory, which no draw ever writes
    // into, so what it produced is whatever texture data shares the page.  Put
    // the real frame there instead.  See the registry note above.
    {
        int w;
        int h;

        if (LocalCopyHostBufSize(type, &w, &h) != 0)
        {
            LocalCopyScreenReadback(ee_adrs, v_adrs,
                                    (u_char *)MioPan_GetHostPointer(ee_adrs),
                                    w, h);
        }
    }

    g3dGsSyncPath(0, 0);
}

// Pick the EE buffer (frame 0 or 1) for a local->host store.
void LocalCopyLtoB(int type, int no, int addr)
{
    uintptr_t ee_adrs;

    ee_adrs = 0x1f05b00;

    if (no == 0)
    {
        ee_adrs = 0x1e79b00;
    }

    LocalCopyLtoBAdrs(type, ee_adrs, addr);

    // Host: stash the frame under the EE buffer's own address, so the matching
    // LocalCopyBtoL() can hand it on.  The EE-side EffImageHalf32() between the
    // two still runs, but on memory nothing here reads -- the half-width result
    // it produces is reproduced by the destination slot's size instead.
    {
        int w;
        int h;

        if (G2D_IS_FRAMEBUFFER_BLOCK(addr) &&
            LocalCopyHostBufSize(type, &w, &h) != 0)
        {
            MioPan_RendererCaptureGsBlock((unsigned int)ee_adrs,
                                          MIOPAN_GS_CAPTURE_LIVE, w, h);
        }
    }
}

// HOST->LOCAL load: emit BITBLTBUF/TRXPOS/TRXREG/TRXDIR GIF packets in
// <=100-line strips and DMA the matching EE image rows through the VIF1 image
// transfer path.
// ee_adrs is either an EE memory-map constant or a caller-owned heap buffer;
// SetPK2DImageTrans() resolves whichever it gets.  The parameter has to be
// pointer-wide so a host buffer survives the call.
//
// The local names come from functions.txt's register list for 0019a490, and
// three of them were transposed by the earlier pass, which put the strip height
// into BITBLTBUF's DPSM: `bl` is the destination's total line count (s5, 224 or
// 448), `bline` the 100-line strip cap (s6), `oline` this strip's height (s0),
// `dbw` the BITBLTBUF DBW in 64-pixel pages (v1, 5 or 10) and `frtype` its DPSM
// (a1) -- 0 for PSMCT32 on every type the game uses, 1 for the fall-through.
// BITBLTBUF is loop-invariant in the ROM, computed once before the strip loop
// from those two; only TRXPOS and TRXREG move.  `bl - rline` is a repeated
// subexpression, not a local: the register holding it has no stab.
void LocalCopyBtoLAdrs(int type, uintptr_t ee_adrs, int v_adrs)
{
    int bline;
    int rline;
    int oline;
    int bl;
    int bw;
    int dbw;
    int frtype;
    int div;
    Q_WORDDATA *pbuf;

    bw = 0x140;
    bl = 0xe0;
    div = 4;
    frtype = 0;
    dbw = 5;

    if (type == 1)
    {
        goto strip;
    }

    if (type < 2)
    {
        bl = 0x1c0;
        bw = 0x280;
        dbw = 10;

        if (type == 0)
        {
            goto strip;
        }
    }
    else
    {
        div = 4;
        frtype = 0;
        bl = 0x1c0;
        bw = 0x140;
        dbw = 5;
        if (type == 2)
        {
            goto strip;
        }
        if (type == 3)
        {
            bl = 0xe0;
            bw = 0x280;
            dbw = 10;
            div = 8;
            goto strip;
        }
    }

    bl = 0x1c0;
    div = 4;
    bw = 0x280;
    dbw = 10;
    frtype = 1;

strip:
    bline = 100;
    rline = bl;

    while (rline > 0)
    {
        if (bline < rline)
        {
            oline = bline;
        }
        else
        {
            oline = rline;
        }

        pbuf = GetPK2Dbuf();

        // A+D GIFtag: BITBLTBUF, TRXPOS, TRXREG, TRXDIR for this <=100-line strip.
        pbuf->ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1);
        pbuf->ul64[1] = SCE_GIF_PACKED_AD;

        pbuf[1].ul64[1] = SCE_GS_BITBLTBUF;
        pbuf[1].ul64[0] = SCE_GS_SET_BITBLTBUF(0, 0, 0, v_adrs, dbw, frtype);
        pbuf[2].ul64[1] = SCE_GS_TRXPOS;
        pbuf[2].ul64[0] = SCE_GS_SET_TRXPOS(0, 0, 0, bl - rline, 0);
        pbuf[3].ul64[1] = SCE_GS_TRXREG;
        pbuf[3].ul64[0] = SCE_GS_SET_TRXREG(bw, oline);
        pbuf[4].ul64[1] = SCE_GS_TRXDIR;
        pbuf[4].ul64[0] = SCE_GS_SET_TRXDIR(0);
        EndPK2Dbuf(pbuf + 5);

        SetPK2DImageTrans(ee_adrs + (uintptr_t)((bl - rline) * bw * 4),
                          (bw * oline) / div);

        // Host: the packet above is dropped, so re-issue the transfer.  Skipped
        // for the effect stashes for the same reason the read-back skips them.
        if (ee_adrs != G2D_EFFECT_STASH_0 && ee_adrs != G2D_EFFECT_STASH_1)
        {
            G2dHostUpload(v_adrs, dbw, frtype, 0, bl - rline, bw, oline,
                          MioPan_GetHostPointer(ee_adrs +
                              (uintptr_t)((bl - rline) * bw * 4)));
        }

        rline = rline - oline;
    }
}

// Pick the EE buffer (frame 0 or 1) for a host->local load.
void LocalCopyBtoL(int type, int no, int addr)
{
    uintptr_t ee_adrs;

    ee_adrs = 0x1f05b00;
    
    if (no == 0)
    {
        ee_adrs = 0x1e79b00;
    }

    LocalCopyBtoLAdrs(type, ee_adrs, addr);

    // Host: the upload half of the EE round-trip.  The source is whatever the
    // matching LocalCopyLtoB() captured under this EE address; scaling into the
    // destination slot is what stands in for EffImageHalf32().
    {
        int w;
        int h;

        if (!G2D_IS_FRAMEBUFFER_BLOCK(addr) &&
            LocalCopyHostBufSize(type, &w, &h) != 0)
        {
            MioPan_RendererCaptureGsBlock((unsigned int)addr,
                                          (unsigned int)ee_adrs, w, h);
        }
    }
}

// LOCAL->LOCAL copy: build the TEX0/CLAMP/etc. draw env from the current draw
// env, then emit a textured-strip GIF packet that samples addr1 and draws into
// addr2.  scpw[type] caches the src/dst rect.
void LocalCopyLtoL(int type, int addr1, int addr2)
{
    // The six copy geometries, read out of the ROM's .data at 0x314830 (the
    // whole of g2d_draw.o's 0x100 .data block, scpw plus 0x10 of padding).
    // stbp/dtbp are 0 in the image because the two assignments below overwrite
    // them on every call; everything else is fixed per type and nothing else
    // ever writes it, so losing this initialiser leaves every blit copying a
    // 0x0 rect out of a zero-width buffer.
    //
    //   du/dv are the source texel extent, dw/dh the destination pixel extent,
    //   so a type is really "src WxH -> dst WxH":
    //     0  640x448 -> 640x448    3  640x448 -> 320x448  (the refraction src)
    //     1  640x448 -> 320x224    4  320x448 -> 640x448
    //     2  320x224 -> 640x448    5  640x448 -> 640x224
    static SCREEN_COPY_WRK scpw[6] =    // data 314830
    {
        /*      stbp sfbw  stw  sth  dtbp dfbw   dw   dh    du   dv */
        /* 0 */ {  0,  10,  10,   9,   0,  10, 640, 448, 640, 448 },
        /* 1 */ {  0,  10,  10,   9,   0,   5, 320, 224, 640, 448 },
        /* 2 */ {  0,   5,   9,   8,   0,  10, 640, 448, 320, 224 },
        /* 3 */ {  0,  10,  10,   9,   0,   5, 320, 448, 640, 448 },
        /* 4 */ {  0,   5,   9,   9,   0,  10, 640, 448, 320, 448 },
        /* 5 */ {  0,  10,  10,   9,   0,  10, 640, 224, 640, 448 },
    };
    SCREEN_COPY_WRK *scpp;
    Q_WORDDATA *pbuf;
    void *pNDrawEnv;
    DRAW_ENV_5 env;

    pNDrawEnv = GetDrawEnv((int)((u_int)sys_wrk.count + 1U) & 1);
    scpp = &scpw[type];

    // Host: capture the frame into the destination block at this copy's own
    // destination rect, which is the size and resolution the effect's UVs are
    // written against.  scpw[type] is the ROM's table, so a type-1 quarter-size
    // stash really is quarter-size here and softens the same way when the
    // effect stretches it back over the screen.
    if (G2D_IS_FRAMEBUFFER_BLOCK(addr1) && !G2D_IS_FRAMEBUFFER_BLOCK(addr2))
    {
        MioPan_RendererCaptureGsBlock((unsigned int)addr2,
                                      MIOPAN_GS_CAPTURE_LIVE,
                                      scpp->dw, scpp->dh);
        // ...and record the same thing for the read-back path: the whole
        // frame, at this type's destination size.  That is what lets a later
        // LOCAL->HOST store of addr2 know it is holding a squashed screen.
        G2dRegisterScreenPage(addr2, 0, 0, 640, 448, scpp->dw, scpp->dh);
    }

    if (addr2 < 0)
    {
        addr2 = addr2 + 0x1f;
    }

    scpp->stbp = addr1;
    scpp->dtbp = addr2 >> 5;
    env.alpha = SCE_GS_SET_ALPHA(1, 0, 1, 0, 0x80);
    env.tex1 = SCE_GS_SET_TEX1(1, 0, 1, 5, 0, 0, 1);

    // keep the live ZBP+PSM from the current env, force ZMSK=1 (depth-write off).
    env.zbuf = ((DRAW_ENV_PEEK *)pNDrawEnv)->zbuf & 0xf0001ff | SCE_GS_SET_ZBUF(0, 0, 1);
    env.clamp = SCE_GS_SET_CLAMP(SCE_GS_CLAMP_CLAMP, SCE_GS_CLAMP_CLAMP, 0, 0, 0, 0);
    env.test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    SetDrawEnv(0, &env);
    pbuf = GetPK2DbufWait();

    // A+D GIFtag: FRAME_1, XYOFFSET_1, TEX0_1 (TEXFLUSH leads).
    pbuf->ul64[0] = SCE_GIF_SET_TAG(4, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf->ul64[1] = SCE_GIF_PACKED_AD;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = SCE_GS_TEXFLUSH;
    pbuf[2].ul64[1] = SCE_GS_FRAME_1;
    pbuf[2].ul64[0] = SCE_GS_SET_FRAME(scpp->dtbp, scpp->dfbw, 1, 0);
    pbuf[3].ul64[1] = SCE_GS_XYOFFSET_1;
    pbuf[3].ul64[0] = SCE_GS_SET_XYOFFSET(0x6c08, 0x7908);
    pbuf[4].ul64[1] = SCE_GS_TEX0_1;
    pbuf[4].ul64[0] = SCE_GS_SET_TEX0(scpp->stbp, scpp->sfbw, 1, scpp->stw, scpp->sth, 1, 1, 0, 0, 0, 0, 0);
    // vertex GIFtag: textured sprite (FST), 2 verts of {UV, XYZF2}.
    pbuf[5].ul64[1] = (u_long)SCE_GIF_PACKED_UV | ((u_long)SCE_GIF_PACKED_XYZF2 << 4)
                    | ((u_long)SCE_GIF_PACKED_UV << 8) | ((u_long)SCE_GIF_PACKED_XYZF2 << 12);
    pbuf[5].ul64[0] = SCE_GIF_SET_TAG(1, 1, 1, SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE, 0, 1, 0, 0, 0, 1, 0, 0), 0, 4);
    pbuf[6].ui32[0] = 0;
    pbuf[6].ui32[1] = 0;
    pbuf[7].ui32[0] = 0x6c00;       /* src UV origin (stored as float bits in the build) */
    pbuf[7].ui32[1] = 0x7900;
    pbuf[7].ul64[1] = 0;
    pbuf[8].ui32[0] = scpp->du << 4;
    pbuf[8].ui32[1] = scpp->dv << 4;
    pbuf[9].ui32[0] = scpp->dw * 0x10 + 0x6c00;
    pbuf[9].ui32[1] = scpp->dh * 0x10 + 0x7900;
    pbuf[9].ul64[1] = 0;
    // A+D GIFtag: 2 register writes (restore the cached draw env words below).
    pbuf[10].ul64[0] = SCE_GIF_SET_TAG(2, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf[10].ul64[1] = SCE_GIF_PACKED_AD;
    // copy the two cached 64-bit env words into the trailing qwords
    pbuf[11].ui32[0] = ((Q_WORDDATA *)pNDrawEnv)[0].ui32[0];
    pbuf[11].ui32[1] = ((Q_WORDDATA *)pNDrawEnv)[0].ui32[1];
    pbuf[11].ui32[2] = ((Q_WORDDATA *)pNDrawEnv)[0].ui32[2];
    pbuf[11].ui32[3] = ((Q_WORDDATA *)pNDrawEnv)[0].ui32[3];
    pbuf[12].ui32[0] = ((Q_WORDDATA *)pNDrawEnv)[4].ui32[0];
    pbuf[12].ui32[1] = ((Q_WORDDATA *)pNDrawEnv)[4].ui32[1];
    pbuf[12].ui32[2] = ((Q_WORDDATA *)pNDrawEnv)[4].ui32[2];
    pbuf[12].ui32[3] = ((Q_WORDDATA *)pNDrawEnv)[4].ui32[3];
    EndPK2DbufWait(pbuf + 0xd);
}

// ──────────────────────────────────────────────────────────────────────
// PK2D packet ring.  Pinned to the fixed PACKET2D EE region.

void InitPK2Dbuf(void)
{
    pk2d_wrk.buf_top = (Q_WORDDATA *)0x1e89b00;
    pk2d_wrk.idx_top = (Q_WORDDATA *)0x1e79b00;
    pk2d_wrk.idx_now = (Q_WORDDATA *)0x1e79b00;
    pk2d_wrk.buf_now = (Q_WORDDATA *)0x1e89b00;
}

void SwapPK2Dbuf(void)
{
    pk2d_wrk.buf_now = (Q_WORDDATA *)0x21e89b00;
    pk2d_wrk.buf_top = (Q_WORDDATA *)0x21e89b00;
    pk2d_wrk.idx_now = (Q_WORDDATA *)(((u_int)sys_wrk.count & 1) * 0x8000 + 0x1e79b00 | 0x20000000);
    pk2d_wrk.idx_top = pk2d_wrk.idx_now;
}

void PK2DKick(void)
{
    dmaVif1Kick();
}

Q_WORDDATA *GetPK2Dbuf(void)
{
    return (Q_WORDDATA *)dmaVif1GetPacketFLUSH_DIRECT();
}

void EndPK2Dbuf(Q_WORDDATA *addr)
{
    dmaVif1SetPacketFLUSH_DIRECT((qword *)addr);
}

Q_WORDDATA *GetPK2DbufWait(void)
{
    return GetPK2Dbuf();
}

void EndPK2DbufWait(Q_WORDDATA *addr)
{
    dmaVif1SetPacketFLUSH_DIRECT((qword *)addr);
}

Q_WORDDATA *SetPK2DRefTag(int nloop, uintptr_t addr)
{
    dmaVif1AddRefTag((uintptr_t)MioPan_GetHostPointer(addr), nloop);
    return (Q_WORDDATA *)0x0;
}

Q_WORDDATA *TermPK2Dbuf(void)
{
    printf("kitemasu\n");
    return (Q_WORDDATA *)0x0;
}

void AddCNTtag(Q_WORDDATA *addr, int n)
{
}

// Append a VIF1 image-transfer (DIRECT) packet that DMAs nloop qwords from
// img_addr to GS local memory.
void SetPK2DImageTrans(uintptr_t img_addr, int nloop)
{
    Q_WORDDATA *pbuf;
    uintptr_t   host_addr;

    pbuf = (Q_WORDDATA *)dmaVif1GetPacketVIF();
    pbuf->ul64[0] = SCE_GIF_SET_TAG(nloop, 1, 0, 0, SCE_GIF_IMAGE, 1);
    pbuf->ui32[2] = 0xe;
    pbuf->ui32[3] = 0;
    dmaVif1SetPacketVIF((qword *)(pbuf + 1), 0, SCE_VIF1_SET_DIRECT(1, 0));
    host_addr = (uintptr_t)MioPan_GetHostPointer(img_addr);
    dmaVif1AddRefTagVIF(host_addr, nloop, 0, SCE_VIF1_SET_DIRECT(nloop, 0));
}

// ──────────────────────────────────────────────────────────────────────
// PK3D stub helpers + raw VIF1 DIRECT transfer.

Q_WORDDATA *GetPK3Dbuf(void)
{
    printf("kitemasu\n");
    return pk2d_wrk.idx_now;
}

void TermPK3Dbuf(void)
{
    printf("kitemasu\n");
    pk2d_wrk.idx_now++;
}

Q_WORDDATA *StartDmaDirectTrans(void)
{
    return (Q_WORDDATA *)dmaVif1GetPacketFLUSH_DIRECT();
}

Q_WORDDATA *EndDmaDirectTrans(Q_WORDDATA *tail)
{
    dmaVif1SetPacketFLUSH_DIRECT((qword *)tail);
    return tail;
}
