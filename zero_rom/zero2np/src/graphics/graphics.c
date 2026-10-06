// FILE: /home/zero_rom/zero2np/src/graphics/graphics.c
//
// Immediate-mode debug and utility graphics.  The original routines still
// build their GS packets; the PC port also mirrors each visible primitive to
// the host renderer because VIF1 DIRECT packets are not interpreted there.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "graphics.h"

#include "../common/utility.h"
#include "../common/utility2.h"
#include "../common/variable.h"
#include "../miopan/miopan_profiler.h"
#include "../miopan/rendering/miopan_renderer.h"
#include "draw_env.h"
#include "effect/effect.h"
#include "effect/effect_pak.h"      // Reserve2DPacket
#include "graph2d/g2d_draw.h"
#include "graph3d/g3dMath.h"
#include "graph3d/g3dxVu0.h"
#include "graph3d/gra3d.h"
#include "graph3d/gra3dConst.h"

#include <libgraph.h>
#include <libvu0.h>

#include <math.h>
#include <string.h>

static const float kPi = 3.1415927f;

static const u_char kCircleRgb[12][6] = {
    {128, 128, 160, 144, 144, 196},
    {160, 160, 128, 196, 196, 144},
    {160, 128, 128, 196, 144, 144},
    {128, 160, 128, 144, 196, 144},
    {160,  96, 160, 196, 144, 196},
    {255, 200,  64, 196, 144, 144},
    {255, 128, 255, 196, 144, 144},
    {255, 200, 200, 196, 144, 144},
    {255,  64,  64, 196, 144, 144},
    { 64,   0,   0, 196, 144, 144},
    {  0,   0,   0, 196, 144, 144},
    {255, 255, 255, 196, 144, 144},
};

static int save_cross_line_cnt;
static float save_cross_line_pos[10][4];

/* MOVED: Reserve2DPacket() now lives in its owning module, effect/effect_pak.c
 * (ROM 0x0015c030).  It is still empty there -- the calls only preserve the
 * ROM's statement order. */

/* Vertex colour for a *textured* primitive, and vertex alpha everywhere: the
 * GS modulate is Cv = Ct*Cs>>7 and AS is a 0..128 fraction, so 128 is unity.
 * Matches Ps2ColorToFloat() / Ps2AlphaToFloat() on the renderer side. */
static float HostColor(int value)
{
    float result = (float)value / 128.0f;
    if (result < 0.0f)
    {
        return 0.0f;
    }
    if (result > 1.0f)
    {
        return 1.0f;
    }
    return result;
}

/* Vertex RGB for an *untextured* primitive.  With TME clear the GS puts the
 * vertex colour in the framebuffer as it stands, so the divisor is 255 -- the
 * same split Ps2UntexturedColorToFloat() makes on the renderer side, and the
 * one every u_char bridge in this file already takes.  Everything that reaches
 * RendererWorldTriangles() is TME=0 (Draw3DSquare's GIFtag PRIM is 0x44,
 * EmitCirclePacket's 0x01/0x04), and DrawHitCircle2D draws the identical
 * kCircleRgb entries through MioPan_RendererDrawSolidTriangles2D(), which
 * scales them by 255.  At /128 nine of those twelve entries clip at least one
 * channel and seven collapse to the same pure white. */
static float HostFillColor(int value)
{
    float result = (float)value / 255.0f;
    if (result < 0.0f)
    {
        return 0.0f;
    }
    if (result > 1.0f)
    {
        return 1.0f;
    }
    return result;
}

static void SetPacketColor(Q_WORDDATA *p, int r, int g, int b, int a)
{
    p->ui32[0] = (u_int)r;
    p->ui32[1] = (u_int)g;
    p->ui32[2] = (u_int)b;
    p->ui32[3] = (u_int)a;
}

static void SetPacketPosition(Q_WORDDATA *p, int x, int y, int z, int adc)
{
    p->iv[0] = x;
    p->iv[1] = y;
    p->iv[2] = z;
    p->ui32[3] = (u_int)adc;
}

static int ScreenX(float x)
{
    return (int)((x + 2048.0f) * 16.0f);
}

static int ScreenY(float y, float div)
{
    return (int)((y / div + 2048.0f) * 16.0f);
}

// The builders below are handed *screen-centred* coordinates -- SceneSetSquare()
// subtracts (320, 224) before calling in, and ScreenX/ScreenY put the centre
// back by adding 2048 against an XYOFFSET of (2048 - 320, 2048 - 224).  The
// host bridges take the 640x448 frame with its origin at the top left instead,
// so the same points need the centre added back before they are handed over.
// SetLine2DPacket() and the debug circle already work in that space and pass
// their points straight through; everything reached through ScreenX/ScreenY
// does not.

static float HostX(float x)
{
    return x + 320.0f;
}

static float HostY(float y)
{
    return y + 224.0f;
}

/* PORT-ONLY.  Publish a primitive's GS draw environment to the host renderer.
 *
 * The host bridges below are a second, immediate queue, and each one snapshots
 * the live ALPHA / TEST / ZBUF / SCISSOR state at the moment the draw is
 * queued.  On hardware that pairing is automatic: SetDrawEnvNoTex() writes the
 * registers into the same DMA packet as the primitive that follows, so where
 * the call sits among the builder's other statements does not matter.  Here it
 * does -- the 3D builders call it *after* the bridge (and DrawHitCircle()
 * calls it behind the fixed-point cull), which left every one of them drawn
 * with the previous primitive's ALPHA.
 *
 * That is not a subtle error.  Two of the blend shapes the engine uses,
 * GS_BLEND_DST_ADD (Cd*As + Cd) and GS_BLEND_DST_DECAY (Cd*(1-As)), never read
 * the source colour at all, so a shape inheriting one of them is drawn as a
 * brightened or darkened copy of whatever is behind it -- in a room with fog,
 * the fog colour, for every affected primitive whatever its own RGB.
 *
 * Publishing here rather than moving the ROM's own call leaves the DMA packet
 * the builder emits byte-identical: this touches only the host mirror, and the
 * SetDrawEnvNoTex() that follows re-publishes the same three values.
 *
 * One deviation comes with it, and it is the right one: the publish sits ahead
 * of each builder's fixed-point cull, so a primitive the PS2 would have thrown
 * away still leaves its env in the host mirror where the ROM would have left
 * the previous one.  The host draw is already deliberately independent of that
 * cull -- see Draw3DSquare() -- so the env it is drawn with has to be too. */
static void PublishDrawEnvToHost(const DRAW_ENV_NOTEX *env)
{
    MioPan_RendererSetGsAlphaRegister(env->alpha);
    MioPan_RendererSetGsTestRegister(env->test);
    MioPan_RendererSetGsZbufRegister(env->zbuf);
}

static void RendererQuad(const float *xy,
                         const u_char *r, const u_char *g,
                         const u_char *b, u_char a)
{
    u_char rgba[16];

    for (int i = 0; i < 4; i++)
    {
        rgba[i * 4 + 0] = r[i];
        rgba[i * 4 + 1] = g[i];
        rgba[i * 4 + 2] = b[i];
        rgba[i * 4 + 3] = a;
    }
    MioPan_RendererDrawSolidQuad(xy, rgba);
}

static void RendererFlatQuad(const float *xy, u_char r, u_char g,
                             u_char b, u_char a)
{
    const u_char rr[4] = {r, r, r, r};
    const u_char gg[4] = {g, g, g, g};
    const u_char bb[4] = {b, b, b, b};
    RendererQuad(xy, rr, gg, bb, a);
}

static void RendererTriangle(const float *xy, u_char r, u_char g,
                             u_char b, u_char a)
{
    u_char rgba[12];

    for (int i = 0; i < 3; i++)
    {
        rgba[i * 4 + 0] = r;
        rgba[i * 4 + 1] = g;
        rgba[i * 4 + 2] = b;
        rgba[i * 4 + 3] = a;
    }
    MioPan_RendererDrawSolidTriangles2D(xy, rgba, 3);
}

static void RendererLine2D(float x1, float y1, float x2, float y2,
                           u_char r1, u_char g1, u_char b1, u_char a1,
                           u_char r2, u_char g2, u_char b2, u_char a2)
{
    const float xy[4] = {x1, y1, x2, y2};
    const u_char rgba[8] = {r1, g1, b1, a1, r2, g2, b2, a2};
    MioPan_RendererDrawLine2D(xy, rgba, 1.0f);
}

static int ProjectWorldToHostClip(float *clip, const float *position)
{
    GRA3DCAMERA *camera = gra3dGetCamera();

    if (clip == NULL || position == NULL || camera == NULL)
    {
        return 0;
    }

    /* matWorldClipObject is matViewClipObject composed with the view, i.e. the
     * same world->clip transform the renderer uses for meshes in x, y and w, so
     * a point put through it lands in host clip space directly.  The Y flip
     * that used to be undone here now lives in
     * g3dCalcViewClipMatrixPerspective().
     *
     * Not in z: this matrix keeps the engine's symmetric depth, which the
     * renderer's reversed-Z projection does not share.  Converting that z
     * would cancel it to a few digits, so the clip bridges ignore it and
     * rebuild the depth from w -- the view depth -- with the meshes' own depth
     * row.  w is the coordinate that has to be right here. */
    sceVu0ApplyMatrix(clip, camera->matWorldClipObject, position);

    return isfinite(clip[0]) && isfinite(clip[1]) &&
           isfinite(clip[2]) && isfinite(clip[3]);
}

/* A debug primitive's position is a point, so it has to be projected with
 * w = 1 whatever its 4th component happens to hold.  The ROM never reads that
 * component: DrawLinePacket() and TransformDebugPosition() feed the vector to
 * sceVu0TransMatrix(), a vec3 add into the matrix translation, and then project
 * the origin (0,0,0,1) through it.  Callers rely on that -- sis_trpoint.c's
 * trace points keep the floor number in [3] (11.0f), and sis_trace.p[] inherits
 * it -- so passing the raw vector to sceVu0ApplyMatrix() would scale the
 * world->clip translation row by 11 and put the primitive off screen. */
static void HostPoint4(float *out, const float *position)
{
    out[0] = position[0];
    out[1] = position[1];
    out[2] = position[2];
    out[3] = 1.0f;
}

/* PORT-ONLY.  Host counterpart of a world-space GS line: projects both ends
 * and hands SDL a screen-space quad, gouraud from endpoint 1 to endpoint 2.
 * Colours are raw PS2 0..128 components.  Non-static so the effect packet
 * builders (EffectRainParticleDraw) can queue the same host draw. */
void RendererWorldLine(const float *p1, const float *p2,
                       u_char r1, u_char g1, u_char b1, u_char a1,
                       u_char r2, u_char g2, u_char b2, u_char a2,
                       int depth_test)
{
    float positions[8];
    float world1[4];
    float world2[4];
    const u_char rgba[8] = {r1, g1, b1, a1, r2, g2, b2, a2};

    if (p1 == NULL || p2 == NULL)
    {
        return;
    }
    HostPoint4(world1, p1);
    HostPoint4(world2, p2);
    if (!ProjectWorldToHostClip(positions + 0, world1) ||
        !ProjectWorldToHostClip(positions + 4, world2))
    {
        return;
    }

    MioPan_RendererDrawClipLine(positions, rgba, 1.0f, depth_test);
}

static void RendererWorldPoint(const float *position, const u_char *rgba,
                               int depth_test)
{
    float clip[4];
    float world[4];

    if (position == NULL)
    {
        return;
    }
    HostPoint4(world, position);
    if (!ProjectWorldToHostClip(clip, world))
    {
        return;
    }
    MioPan_RendererDrawClipPoint(clip, rgba, 1.0f, depth_test);
}

static void RendererWorldTriangles(const float *positions, int vertex_count,
                                   u_char r, u_char g, u_char b, u_char a)
{
    float clip_positions[108 * 4];
    float rgba[108 * 4];

    if (positions == NULL || vertex_count <= 0 || vertex_count > 108 ||
        (vertex_count % 3) != 0)
    {
        return;
    }
    for (int i = 0; i < vertex_count; i++)
    {
        float world[4] = {
            positions[i * 3 + 0],
            positions[i * 3 + 1],
            positions[i * 3 + 2],
            1.0f,
        };

        if (!ProjectWorldToHostClip(clip_positions + i * 4, world))
        {
            return;
        }
        rgba[i * 4 + 0] = HostFillColor(r);
        rgba[i * 4 + 1] = HostFillColor(g);
        rgba[i * 4 + 2] = HostFillColor(b);
        rgba[i * 4 + 3] = HostColor(a);
    }
    MioPan_RendererDrawClipTriangles(NULL, clip_positions, NULL, rgba,
                                     vertex_count, 1);
}

static void CopyVector4(float *dst, const float *src)
{
    memcpy(dst, src, sizeof(float) * 4);
}

static void TransformDebugPosition(sceVu0IVECTOR result, const float *position)
{
    sceVu0FMATRIX wlm;
    sceVu0FMATRIX slm;
    sceVu0FVECTOR pos;
    sceVu0FVECTOR origin = {0.0f, 0.0f, 0.0f, 1.0f};
    GRA3DCAMERA *camera = gra3dGetCamera();

    CopyVector4(pos, position);
    sceVu0UnitMatrix(wlm);
    wlm[0][0] = 25.0f;
    wlm[1][1] = 25.0f;
    wlm[2][2] = 25.0f;
    sceVu0TransMatrix(wlm, wlm, pos);
    sceVu0MulMatrix(slm, camera->matWorldScreen, wlm);
    sceVu0RotTransPers(result, slm, origin, 0);
}

static int LineVertexVisible(const sceVu0IVECTOR v)
{
    return (u_int)(v[0] - 0x300) <= 0xfa00U &&
           (u_int)(v[1] - 0x300) <= 0xfa00U &&
           (u_int)(v[2] - 0x0f) <= 0xffff0U;
}

static int QuadVertexVisible(const sceVu0IVECTOR v)
{
    return (u_int)(v[0] - 0x300) <= 0xfa00U &&
           (u_int)(v[1] - 0x300) <= 0xfa00U &&
           (u_int)(v[2] - 0x0f) <= 0xffffff0U;
}

static int PointVisible(const sceVu0IVECTOR v)
{
    return (u_int)(v[0] - 0x6c00) <= 0x2800U &&
           (u_int)(v[1] - 0x7900) <= 0x0e00U &&
           (u_int)(v[2] - 0xff) <= 0xffff00U;
}

static void BuildCircle(float ncf[38][4], float trf[4][4], float rad,
                        int xz_plane)
{
    ncf[0][0] = 0.0f;
    ncf[0][1] = 0.0f;
    ncf[0][2] = 0.0f;
    ncf[0][3] = 1.0f;

    for (int i = 0; i < 36; i++)
    {
        float angle = ((float)i * 10.0f * kPi) / 180.0f;
        ncf[i + 1][0] = rad * g3dCosf(angle);
        ncf[i + 1][1] = xz_plane ? 0.0f : rad * g3dSinf(angle);
        ncf[i + 1][2] = xz_plane ? rad * g3dSinf(angle) : 0.0f;
        ncf[i + 1][3] = 1.0f;
    }
    CopyVector4(ncf[37], ncf[1]);

    CopyVector4(trf[0], ncf[1]);
    CopyVector4(trf[1], ncf[14]);
    CopyVector4(trf[2], ncf[0]);
    CopyVector4(trf[3], ncf[24]);
}

static void EmitCirclePacket(const sceVu0IVECTOR nci[38],
                             const sceVu0IVECTOR tri[4],
                             u_char col, u_char alp,
                             const DRAW_ENV_NOTEX *env)
{
    Q_WORDDATA *pbuf;
    int tail;

    SetDrawEnvNoTex(0, (DRAW_ENV_NOTEX *)env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x1022c00000008001ULL;
    pbuf[0].ul64[1] = 1;
    SetPacketColor(pbuf + 1, kCircleRgb[col][0], kCircleRgb[col][1],
                   kCircleRgb[col][2], alp);
    pbuf[2].ul64[0] = 0x1022c00000008000ULL | 38ULL;
    pbuf[2].ul64[1] = 4;

    for (int i = 0; i < 38; i++)
    {
        SetPacketPosition(pbuf + 3 + i, nci[i][0], nci[i][1], nci[i][2],
                          i < 2 ? 0x8000 : 0);
    }

    tail = 41;
    pbuf[tail].ul64[0] = 0x7022c00000008001ULL;
    pbuf[tail].ul64[1] = 0x4444441ULL;
    SetPacketColor(pbuf + tail + 1, kCircleRgb[col][3], kCircleRgb[col][4],
                   kCircleRgb[col][5], alp);
    SetPacketPosition(pbuf + tail + 2, tri[0][0], tri[0][1], tri[0][2], 0x8000);
    SetPacketPosition(pbuf + tail + 3, tri[1][0], tri[1][1], tri[1][2], 0x8000);
    SetPacketPosition(pbuf + tail + 4, tri[2][0], tri[2][1], tri[2][2], 0);
    SetPacketPosition(pbuf + tail + 5, tri[0][0], tri[0][1], tri[0][2], 0x8000);
    SetPacketPosition(pbuf + tail + 6, tri[3][0], tri[3][1], tri[3][2], 0x8000);
    SetPacketPosition(pbuf + tail + 7, tri[2][0], tri[2][1], tri[2][2], 0);
    EndPK2Dbuf(pbuf + tail + 8);
}

void DrawHitCircle(float *mpos, float rot_y, int adj_y,
                   u_char col, u_char alp, float rad)
{
    float ncf[38][4];
    float trf[4][4];
    float world_circle[38][4];
    float world_tri[4][4];
    float world_matrix1[4][4];
    float world_matrix2[4][4];
    float slm1[4][4];
    float slm2[4][4];
    float wpos[4];
    sceVu0IVECTOR nci[38];
    sceVu0IVECTOR tri[4];
    DRAW_ENV_NOTEX env;
    GRA3DCAMERA *camera;
    float positions[108 * 3];
    float marker_positions[6 * 3];

    BuildCircle(ncf, trf, rad, 0);
    CopyVector4(wpos, mpos);
    wpos[1] -= (float)adj_y + 10.0f;

    sceVu0UnitMatrix(world_matrix1);
    sceVu0RotMatrixX(world_matrix1, world_matrix1, kPi * 0.5f);
    sceVu0RotMatrixY(world_matrix1, world_matrix1, CombRotate(rot_y));
    sceVu0TransMatrix(world_matrix1, world_matrix1, wpos);

    wpos[1] -= (float)adj_y + 3.0f;
    sceVu0UnitMatrix(world_matrix2);
    sceVu0RotMatrixX(world_matrix2, world_matrix2, kPi * 0.5f);
    sceVu0RotMatrixY(world_matrix2, world_matrix2, CombRotate(rot_y));
    sceVu0TransMatrix(world_matrix2, world_matrix2, wpos);

    camera = gra3dGetCamera();
    sceVu0MulMatrix(slm1, camera->matWorldScreen, world_matrix1);
    sceVu0MulMatrix(slm2, camera->matWorldScreen, world_matrix2);
    sceVu0RotTransPersN(nci, slm1, ncf, 38, 0);
    sceVu0RotTransPersN(tri, slm2, trf, 4, 0);

    env.alpha = 0x84;
    env.test = 0x5000d;
    env.zbuf = 0x0a000118;
    if ((u_int)nci[0][0] >= 0x5a80U && (u_int)nci[0][0] <= 0xa580U &&
        (u_int)nci[0][1] >= 0x6700U && (u_int)nci[0][1] <= 0x9900U &&
        nci[0][2] != 0 && (u_int)nci[0][2] <= 0xffffffU)
    {
        EmitCirclePacket(nci, tri, col, alp, &env);
    }

    for (int i = 0; i < 38; i++)
    {
        sceVu0ApplyMatrix(world_circle[i], world_matrix1, ncf[i]);
    }
    for (int i = 0; i < 4; i++)
    {
        sceVu0ApplyMatrix(world_tri[i], world_matrix2, trf[i]);
    }
    for (int i = 0; i < 36; i++)
    {
        const float *v[3] = {world_circle[0], world_circle[i + 1],
                             world_circle[i + 2]};
        for (int j = 0; j < 3; j++)
        {
            positions[(i * 3 + j) * 3 + 0] = v[j][0];
            positions[(i * 3 + j) * 3 + 1] = v[j][1];
            positions[(i * 3 + j) * 3 + 2] = v[j][2];
        }
    }
    /* The packet path above sits behind the on-screen test, so on a circle
     * whose centre is culled nothing has published this env yet. */
    PublishDrawEnvToHost(&env);
    RendererWorldTriangles(positions, 108, kCircleRgb[col][0],
                           kCircleRgb[col][1], kCircleRgb[col][2], alp);

    const int marker_index[6] = {0, 1, 2, 0, 3, 2};
    for (int i = 0; i < 6; i++)
    {
        marker_positions[i * 3 + 0] = world_tri[marker_index[i]][0];
        marker_positions[i * 3 + 1] = world_tri[marker_index[i]][1];
        marker_positions[i * 3 + 2] = world_tri[marker_index[i]][2];
    }
    RendererWorldTriangles(marker_positions, 6, kCircleRgb[col][3],
                           kCircleRgb[col][4], kCircleRgb[col][5], alp);
}

void SetLine2DPacket(float x1, float y1, float x2, float y2,
                     u_char r, u_char g, u_char b, u_char a)
{
    float temp[4] = {
        x1 + 2048.0f - 320.0f,
        x2 + 2048.0f - 320.0f,
        y1 + 2048.0f - 224.0f,
        y2 + 2048.0f - 224.0f,
    };
    int itmp[4];
    Q_WORDDATA *pbuf;

    for (int i = 0; i < 4; i++)
    {
        itmp[i] = (int)(temp[i] * 16.0f);
    }

    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x3020c00000008001ULL;
    pbuf[0].ul64[1] = 0x441;
    SetPacketColor(pbuf + 1, r, g, b, a);
    SetPacketPosition(pbuf + 2, itmp[0], itmp[2], 1, 0);
    SetPacketPosition(pbuf + 3, itmp[1], itmp[3], 1, 0);
    EndPK2Dbuf(pbuf + 4);

    RendererLine2D(x1, y1, x2, y2, r, g, b, a, r, g, b, a);
}

void SetLine2D(float x1, float y1, float x2, float y2,
               u_char r, u_char g, u_char b, u_char a)
{
    DRAW_ENV_NOTEX env;

    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x5000d;
    env.zbuf = 0x10a000118;
    SetDrawEnvNoTex(0, &env);
    SetLine2DPacket(x1, y1, x2, y2, r, g, b, a);
}

void DrawHitCircle2D(float *mpos, float rot_y, int adj_y,
                     u_char col, u_char alp, float rad)
{
    float ncf[38][4];
    float trf[4][4];
    float transformed[38][4];
    float transformed_tri[4][4];
    float slm1[4][4];
    float slm2[4][4];
    float wpos[4];
    sceVu0IVECTOR nci[38] = {};
    sceVu0IVECTOR tri[4] = {};
    DRAW_ENV_NOTEX env;
    float xy[108 * 2];
    u_char rgba[108 * 4];
    float marker_xy[6 * 2];
    u_char marker_rgba[6 * 4];
    (void)adj_y;

    BuildCircle(ncf, trf, rad, 1);
    CopyVector4(wpos, mpos);
    wpos[0] *= 0.1f;
    wpos[2] *= 0.1f;

    sceVu0UnitMatrix(slm1);
    slm1[0][0] = slm1[1][1] = slm1[2][2] = 0.1f;
    sceVu0RotMatrixY(slm1, slm1, -CombRotate(rot_y));
    sceVu0TransMatrix(slm1, slm1, wpos);
    sceVu0UnitMatrix(slm2);
    slm2[0][0] = slm2[1][1] = slm2[2][2] = 0.1f;
    sceVu0RotMatrixY(slm2, slm2, -CombRotate(rot_y));
    sceVu0TransMatrix(slm2, slm2, wpos);

    for (int i = 0; i < 38; i++)
    {
        sceVu0ApplyMatrix(transformed[i], slm1, ncf[i]);
        nci[i][0] = (int)((transformed[i][0] + 2048.0f) * 16.0f);
        nci[i][1] = (int)((2048.0f - transformed[i][2]) * 16.0f);
        nci[i][2] = 0;
    }
    for (int i = 0; i < 4; i++)
    {
        sceVu0ApplyMatrix(transformed_tri[i], slm2, trf[i]);
        tri[i][0] = (int)((transformed_tri[i][0] + 2048.0f) * 16.0f);
        tri[i][1] = (int)((2048.0f - transformed_tri[i][2]) * 16.0f);
        tri[i][2] = 0;
    }

    env.alpha = 0x84;
    env.test = 0x30003;
    env.zbuf = 0x10a000118;
    EmitCirclePacket(nci, tri, col, alp, &env);

    for (int i = 0; i < 36; i++)
    {
        const int index[3] = {0, i + 1, i + 2};
        for (int j = 0; j < 3; j++)
        {
            int out = i * 3 + j;
            xy[out * 2 + 0] = transformed[index[j]][0] + 320.0f;
            xy[out * 2 + 1] = -transformed[index[j]][2] + 224.0f;
            rgba[out * 4 + 0] = kCircleRgb[col][0];
            rgba[out * 4 + 1] = kCircleRgb[col][1];
            rgba[out * 4 + 2] = kCircleRgb[col][2];
            rgba[out * 4 + 3] = alp;
        }
    }
    MioPan_RendererDrawSolidTriangles2D(xy, rgba, 108);

    const int marker_index[6] = {0, 1, 2, 0, 3, 2};
    for (int i = 0; i < 6; i++)
    {
        marker_xy[i * 2 + 0] = transformed_tri[marker_index[i]][0] + 320.0f;
        marker_xy[i * 2 + 1] = -transformed_tri[marker_index[i]][2] + 224.0f;
        marker_rgba[i * 4 + 0] = kCircleRgb[col][3];
        marker_rgba[i * 4 + 1] = kCircleRgb[col][4];
        marker_rgba[i * 4 + 2] = kCircleRgb[col][5];
        marker_rgba[i * 4 + 3] = alp;
    }
    MioPan_RendererDrawSolidTriangles2D(marker_xy, marker_rgba, 6);
}

void Draw3DSquare(float *mpos1, float *mpos2, float *mpos3, float *mpos4,
                  u_char r, u_char g, u_char b, u_char a)
{
    const float *points[4] = {mpos1, mpos2, mpos3, mpos4};
    sceVu0IVECTOR ivec[4];
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;
    float positions[18];
    const int indices[6] = {1, 0, 2, 2, 0, 3};

    /* Host rendering has its own camera and clip-space checks.  Do not make it
     * depend on the emulated GS fixed-point cull below: the latter is retained
     * only for the original DIRECT packet path. */
    for (int i = 0; i < 6; i++)
    {
        positions[i * 3 + 0] = points[indices[i]][0];
        positions[i * 3 + 1] = points[indices[i]][1];
        positions[i * 3 + 2] = points[indices[i]][2];
    }
    /* The three env stores move ahead of the bridge so it can be published;
     * they are writes to a stack local, so the packet stream is unchanged and
     * SetDrawEnvNoTex() stays where the ROM calls it, below the cull. */
    env.alpha = 0x44;
    env.test = 0x5000d;
    env.zbuf = 0x0a000118;
    PublishDrawEnvToHost(&env);
    RendererWorldTriangles(positions, 6, r, g, b, a);

    for (int i = 0; i < 4; i++)
    {
        TransformDebugPosition(ivec[i], points[i]);
        if (!QuadVertexVisible(ivec[i]))
        {
            return;
        }
    }

    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x5022400000008001ULL;
    pbuf[0].ul64[1] = 0x44441;
    SetPacketColor(pbuf + 1, r, g, b, a);
    SetPacketPosition(pbuf + 2, ivec[1][0], ivec[1][1], ivec[1][2], 0);
    SetPacketPosition(pbuf + 3, ivec[0][0], ivec[0][1], ivec[0][2], 0);
    SetPacketPosition(pbuf + 4, ivec[2][0], ivec[2][1], ivec[2][2], 0);
    SetPacketPosition(pbuf + 5, ivec[3][0], ivec[3][1], ivec[3][2], 0);
    EndPK2Dbuf(pbuf + 6);

}

void DrawCrossLineA(float *bpos, u_char alp)
{
    float p1[4];
    float p2[4];

    for (int axis = 0; axis < 3; axis++)
    {
        CopyVector4(p1, bpos);
        CopyVector4(p2, bpos);
        p1[axis] -= 20.0f;
        p2[axis] += 20.0f;
        DrawLine(p1, 0x80, 0x80, 0x80, alp,
                 p2, 0x80, 0x80, 0x80, alp);
    }
}

void DrawCrossLine(float *bpos)
{
    DrawCrossLineA(bpos, 0x80);
}

static void DrawLinePacket(const float *mpos1,
                           u_char r1, u_char g1, u_char b1, u_char a1,
                           const float *mpos2,
                           u_char r2, u_char g2, int b2, int a2,
                           const DRAW_ENV_NOTEX *envp)
{
    sceVu0IVECTOR ivec[2];
    Q_WORDDATA *pbuf;

    PublishDrawEnvToHost(envp);
    RendererWorldLine(mpos1, mpos2, r1, g1, b1, a1,
                      r2, g2, (u_char)b2, (u_char)a2,
                      envp->test != 0x30003);

    TransformDebugPosition(ivec[0], mpos1);
    TransformDebugPosition(ivec[1], mpos2);
    if (!LineVertexVisible(ivec[0]) || !LineVertexVisible(ivec[1]))
    {
        return;
    }

    SetDrawEnvNoTex(0, (DRAW_ENV_NOTEX *)envp);
    pbuf = GetPK2Dbuf();
    Reserve2DPacket(0x10);
    pbuf[0].ul64[0] = 0x4024c00000008001ULL;
    pbuf[0].ul64[1] = 0x4141;
    SetPacketColor(pbuf + 1, r1, g1, b1, a1);
    SetPacketPosition(pbuf + 2, ivec[0][0], ivec[0][1], ivec[0][2], 0);
    SetPacketColor(pbuf + 3, r2, g2, b2, a2);
    SetPacketPosition(pbuf + 4, ivec[1][0], ivec[1][1], ivec[1][2], 0);
    EndPK2Dbuf(pbuf + 5);

}

void DrawLine(float *mpos1, u_char r1, u_char g1, u_char b1, u_char a1,
              float *mpos2, u_char r2, u_char g2, int b2, int a2)
{
    DRAW_ENV_NOTEX env = {0x44, 0x5000d, 0x0a000118};
    DrawLinePacket(mpos1, r1, g1, b1, a1,
                   mpos2, r2, g2, b2, a2, &env);
}

void DrawLineTestOff(float *mpos1, u_char r1, u_char g1, u_char b1, u_char a1,
                     float *mpos2, u_char r2, u_char g2, int b2, int a2)
{
    DRAW_ENV_NOTEX env = {0x44, 0x30003, 0x0a000118};
    DrawLinePacket(mpos1, r1, g1, b1, a1,
                   mpos2, r2, g2, b2, a2, &env);
}

void DrawCrossLineLast(float *bpos)
{
    if (save_cross_line_cnt >= 10)
    {
        PRINT_ASSERT("DrawCrossLineLast() OVER");
    }
    CopyVector4(save_cross_line_pos[save_cross_line_cnt], bpos);
    save_cross_line_cnt++;
}

void DrawCrossLineLastReal(void)
{
    for (int i = 0; i < save_cross_line_cnt; i++)
    {
        float p1[4];
        float p2[4];

        for (int axis = 0; axis < 3; axis++)
        {
            CopyVector4(p1, save_cross_line_pos[i]);
            CopyVector4(p2, save_cross_line_pos[i]);
            p1[axis] -= 20.0f;
            p2[axis] += 20.0f;
            DrawLineTestOff(p1, 0x80, 0x80, 0x80, 0x80,
                            p2, 0x80, 0x80, 0x80, 0x80);
        }
    }
    save_cross_line_cnt = 0;
}

void SetSquareS(int pri, float x1, float y1, float x4, float y4,
                u_char r, u_char g, u_char b, u_char a)
{
    SetSquare(pri, x1, y1, x1, y4, x4, y1, x4, y4, r, g, b, a);
}

void SetSquare(int pri, float x1, float y1, float x2, float y2,
               float x3, float y3, float x4, float y4,
               u_char r, u_char g, u_char b, u_char a)
{
    float div = g_bInterlace ? 2.0f : 1.0f;
    int mpri = pri > 0 ? pri : 0x10;
    int z = 0xfffff - (mpri & 0xfffff);
    const float sx[4] = {x1, x2, x3, x4};
    const float sy[4] = {y1, y2, y3, y4};
    const float xy[8] = {HostX(x1), HostY(y1), HostX(x2), HostY(y2),
                         HostX(x3), HostY(y3), HostX(x4), HostY(y4)};
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    Reserve2DPacket((u_int)mpri);

    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x5000d;
    env.zbuf = 0x0a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x5022400000008001ULL;
    pbuf[0].ul64[1] = 0x44441;
    SetPacketColor(pbuf + 1, r, g, b, 0x80);
    for (int i = 0; i < 4; i++)
    {
        SetPacketPosition(pbuf + 2 + i, ScreenX(sx[i]), ScreenY(sy[i], div), z, 0);
    }
    EndPK2Dbuf(pbuf + 6);
    RendererFlatQuad(xy, r, g, b, a);
}

void SetSquare2s(int pri, float x1, float y1, float x4, float y4,
                 u_char r1, u_char g1, u_char b1,
                 u_char r2, u_char g2, u_char b2, u_char a)
{
    float div = g_bInterlace ? 2.0f : 1.0f;
    int mpri = pri > 0 ? pri : 0x10;
    int z = 0xfffff - (mpri & 0xfffff);
    const float sx[4] = {x1, x4, x1, x4};
    const float sy[4] = {y1, y1, y4, y4};
    const float xy[8] = {HostX(x1), HostY(y1), HostX(x4), HostY(y1),
                         HostX(x1), HostY(y4), HostX(x4), HostY(y4)};
    const u_char rr[4] = {r1, r1, r2, r2};
    const u_char gg[4] = {g1, g1, g2, g2};
    const u_char bb[4] = {b1, b1, b2, b2};
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    Reserve2DPacket((u_int)mpri);

    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x5000d;
    env.zbuf = 0x10a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x8026400000008001ULL;
    pbuf[0].ul64[1] = 0x41414141;
    for (int i = 0; i < 4; i++)
    {
        SetPacketColor(pbuf + 1 + i * 2, rr[i], gg[i], bb[i], 0x80);
        SetPacketPosition(pbuf + 2 + i * 2, ScreenX(sx[i]),
                          ScreenY(sy[i], div), z, 0);
    }
    EndPK2Dbuf(pbuf + 9);
    RendererQuad(xy, rr, gg, bb, a);
}

void SetSquareZ(int pri, float x1, float y1, float x4, float y4, int z)
{
    float div = g_bInterlace ? 2.0f : 1.0f;
    const float xy[8] = {HostX(x1), HostY(y1), HostX(x4), HostY(y1),
                         HostX(x1), HostY(y4), HostX(x4), HostY(y4)};
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    Reserve2DPacket(0);

    env.alpha = ((u_long)(u_int)pri << 32) | 0x64;
    env.test = 0x50003;
    env.zbuf = 0x10a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x3023400000008001ULL;
    pbuf[0].ul64[1] = 0x441;
    SetPacketColor(pbuf + 1, 0x80, 0x80, 0x80, 0x80);
    SetPacketPosition(pbuf + 2, ScreenX(x1), ScreenY(y1, div), z, 0);
    SetPacketPosition(pbuf + 3, ScreenX(x4), ScreenY(y4, div), z, 0);
    EndPK2Dbuf(pbuf + 4);
    RendererFlatQuad(xy, 0x80, 0x80, 0x80, (u_char)pri);
}

void SetSquareSN(int pri, float x1, float y1, float x4, float y4,
                 u_char r, u_char g, u_char b, u_char a)
{
    SetSquare(pri, x1, y1, x1, y4, x4, y1, x4, y4, r, g, b, a);
}

void SetSquareN(int pri, float x1, float y1, float x2, float y2,
                float x3, float y3, float x4, float y4,
                u_char r, u_char g, u_char b, u_char a)
{
    (void)pri; (void)x1; (void)y1; (void)x2; (void)y2;
    (void)x3; (void)y3; (void)x4; (void)y4;
    (void)r; (void)g; (void)b; (void)a;
}

void SetTriangle(int pri, float x1, float y1, float x2, float y2,
                 float x3, float y3, u_char r, u_char g, u_char b, u_char a)
{
    float div = g_bInterlace ? 2.0f : 1.0f;
    int mpri = pri > 0 ? pri : 0x10;
    int z = 0xfffff - (mpri & 0xfffff);
    const float sx[3] = {x1, x2, x3};
    const float sy[3] = {y1, y2, y3};
    const float xy[6] = {HostX(x1), HostY(y1), HostX(x2), HostY(y2),
                         HostX(x3), HostY(y3)};
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    Reserve2DPacket((u_int)mpri);

    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x5000d;
    env.zbuf = 0x10a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x4022400000008001ULL;
    pbuf[0].ul64[1] = 0x4441;
    SetPacketColor(pbuf + 1, r, g, b, 0x80);
    for (int i = 0; i < 3; i++)
    {
        SetPacketPosition(pbuf + 2 + i, ScreenX(sx[i]), ScreenY(sy[i], div), z, 0);
    }
    EndPK2Dbuf(pbuf + 5);
    RendererTriangle(xy, r, g, b, a);
}

void SetTriangleZ(int pri, float x1, float y1, float z1,
                  float x2, float y2, float z2,
                  float x3, float y3, float z3,
                  u_char r, u_char g, u_char b, u_char a)
{
    float div = g_bInterlace ? 2.0f : 1.0f;
    const float sx[3] = {x1, x2, x3};
    const float sy[3] = {y1, y2, y3};
    const float sz[3] = {z1, z2, z3};
    const float xy[6] = {HostX(x1), HostY(y1), HostX(x2), HostY(y2),
                         HostX(x3), HostY(y3)};
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    if (pri <= 0)
    {
        pri = 0x10;
    }
    Reserve2DPacket((u_int)pri);
    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x50000;
    env.zbuf = 0x0a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x4022400000008001ULL;
    pbuf[0].ul64[1] = 0x4441;
    SetPacketColor(pbuf + 1, r, g, b, 0x80);
    for (int i = 0; i < 3; i++)
    {
        SetPacketPosition(pbuf + 2 + i, ScreenX(sx[i]), ScreenY(sy[i], div),
                          (int)(u_int)sz[i], 0);
    }
    EndPK2Dbuf(pbuf + 5);
    RendererTriangle(xy, r, g, b, a);
}

void SetLine(int pri, float x1, float y1, float x2, float y2,
             u_char r, u_char g, u_char b, u_char a)
{
    float div = g_bInterlace ? 0.5f : 1.0f;
    int mpri = pri > 0 ? pri : 0x10;
    int z = 0xfffff - (mpri & 0xfffff);
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    Reserve2DPacket((u_int)mpri);

    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x5000d;
    env.zbuf = 0x10a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x3020c00000008001ULL;
    pbuf[0].ul64[1] = 0x441;
    SetPacketColor(pbuf + 1, r, g, b, 0x80);
    SetPacketPosition(pbuf + 2, ScreenX(x1),
                      (int)((y1 * div + 2048.0f) * 16.0f), z, 0);
    SetPacketPosition(pbuf + 3, ScreenX(x2),
                      (int)((y2 * div + 2048.0f) * 16.0f), z, 0);
    EndPK2Dbuf(pbuf + 4);
    RendererLine2D(HostX(x1), HostY(y1), HostX(x2), HostY(y2),
                   r, g, b, a, r, g, b, a);
}

void SetLine2(int pri, float x1, float y1, float x2, float y2,
              u_char r, u_char g, u_char b, u_char a)
{
    float diff = y1 - y2;

    if (diff == 0.0f) {
        diff = 1.0f;
    }
    float dw = g3dAtanf((x1 - x2) / (diff));
    float d = (dw * 180.0f) / kPi;
    u_char rr;
    u_char gg;
    u_char bb;

    if (d > 45.0f || d < -45.0f)
    {
        rr = (u_char)((float)r * 0.5f);
        gg = (u_char)((float)g * 0.5f);
        bb = (u_char)((float)b * 0.5f);
    }
    else if (d > 30.0f || d < -30.0f)
    {
        if (d < 0.0f)
        {
            d = -d;
        }
        dw = ((45.0f - d) * 0.5f) / 15.0f;
        rr = (u_char)((float)r * (dw + 0.5f));
        gg = (u_char)((float)g * (dw + 0.5f));
        bb = (u_char)((float)b * (dw + 0.5f));
    }
    else
    {
        rr = r;
        gg = g;
        bb = b;
    }
    SetLine(pri, x1, y1, x2, y2, rr, gg, bb, a);
}

void SetLine2PC(int pri, float x1, float y1, u_char r1, u_char g1, u_char b1,
                float x2, float y2, u_char r2, u_char g2, u_char b2, u_char a)
{
    float div = g_bInterlace ? 2.0f : 1.0f;
    int mpri = pri > 0 ? pri : 0x10;
    int z = 0xfffff - (mpri & 0xfffff);
    DRAW_ENV_NOTEX env;
    Q_WORDDATA *pbuf;

    Reserve2DPacket((u_int)mpri);

    env.alpha = ((u_long)a << 32) | 0x64;
    env.test = 0x5000d;
    env.zbuf = 0x10a000118;
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    pbuf[0].ul64[0] = 0x4024c00000008001ULL;
    pbuf[0].ul64[1] = 0x4141;
    SetPacketColor(pbuf + 1, r1, g1, b1, 0x80);
    SetPacketPosition(pbuf + 2, ScreenX(x1), ScreenY(y1, div), z, 0);
    SetPacketColor(pbuf + 3, r2, g2, b2, 0x80);
    SetPacketPosition(pbuf + 4, ScreenX(x2), ScreenY(y2, div), z, 0);
    EndPK2Dbuf(pbuf + 5);
    RendererLine2D(HostX(x1), HostY(y1), HostX(x2), HostY(y2),
                   r1, g1, b1, a, r2, g2, b2, a);
}

void DrawPoint(float *mpos, int no)
{
    sceVu0IVECTOR ivec;
    DRAW_ENV_NOTEX env = {0x44, 0x30003, 0x10a000118};
    Q_WORDDATA *pbuf;
    u_char rgba[4] = {0xff, (u_char)(no * 0xff), (u_char)(no * 0xff), 0x80};

    PublishDrawEnvToHost(&env);
    RendererWorldPoint(mpos, rgba, 0);
    TransformDebugPosition(ivec, mpos);
    if (!PointVisible(ivec))
    {
        return;
    }
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    Reserve2DPacket(0x10);
    pbuf[0].ul64[0] = 0x2060400000008001ULL;
    pbuf[0].ul64[1] = 0x41;
    SetPacketColor(pbuf + 1, 0xff, no * 0xff, no * 0xff, 0x80);
    SetPacketPosition(pbuf + 2, ivec[0], ivec[1], ivec[2], 0);
    EndPK2Dbuf(pbuf + 3);
}

void DrawPoint2(float *mpos, u_char r, u_char g, u_char b, u_char a)
{
    sceVu0IVECTOR ivec;
    DRAW_ENV_NOTEX env = {0x44, 0x5000d, 0x10a000118};
    Q_WORDDATA *pbuf;
    u_char rgba[4] = {r, g, b, a};

    PublishDrawEnvToHost(&env);
    RendererWorldPoint(mpos, rgba, 1);
    TransformDebugPosition(ivec, mpos);
    if (!PointVisible(ivec))
    {
        return;
    }
    SetDrawEnvNoTex(0, &env);
    pbuf = GetPK2Dbuf();
    Reserve2DPacket(0x10);
    pbuf[0].ul64[0] = 0x2020400000008001ULL;
    pbuf[0].ul64[1] = 0x41;
    SetPacketColor(pbuf + 1, r, g, b, a);
    SetPacketPosition(pbuf + 2, ivec[0], ivec[1], ivec[2], 0);
    EndPK2Dbuf(pbuf + 3);
}

void DrawSphere(float fRange, float x, float y, float z,
                u_char r, u_char g, u_char b, u_char a, float *vScale)
{
    static const float a0 = 0.5257311f;
    static const float b0 = 0.8506508f;
    static const float vdata[12][4] = {
        {-a0, 0.0f,  b0, 1.0f}, { a0, 0.0f,  b0, 1.0f},
        {-a0, 0.0f, -b0, 1.0f}, { a0, 0.0f, -b0, 1.0f},
        {0.0f,  b0,  a0, 1.0f}, {0.0f,  b0, -a0, 1.0f},
        {0.0f, -b0,  a0, 1.0f}, {0.0f, -b0, -a0, 1.0f},
        { b0,  a0, 0.0f, 1.0f}, {-b0,  a0, 0.0f, 1.0f},
        { b0, -a0, 0.0f, 1.0f}, {-b0, -a0, 0.0f, 1.0f},
    };
    static const u_char tindices[30][2] = {
        {0,4},{4,1},{1,0},{0,9},{9,4},{9,5},{5,4},{5,8},{8,4},{8,1},
        {8,10},{10,1},{3,10},{5,3},{3,8},{5,2},{2,3},{2,7},{7,3},{7,10},
        {7,6},{6,10},{7,11},{11,6},{11,0},{0,6},{1,6},{11,9},{11,2},{2,9},
    };
    static const u_char bunkatu[20][3] = {
        {0,1,2},{0,3,4},{4,5,6},{6,7,8},{1,8,9},{9,10,11},{10,12,14},
        {7,12,13},{13,15,16},{16,17,18},{12,18,19},{19,20,21},
        {20,22,23},{23,24,25},{2,25,26},{11,21,26},{3,24,27},
        {27,28,29},{5,15,29},{17,22,28},
    };
    float midpoint[30][4];

    for (int i = 0; i < 30; i++)
    {
        float p1[4];
        float p2[4];
        float len;
        float scale;

        for (int j = 0; j < 3; j++)
        {
            p1[j] = vdata[tindices[i][0]][j] * fRange;
            p2[j] = vdata[tindices[i][1]][j] * fRange;
            midpoint[i][j] = (p1[j] + p2[j]) * 0.5f;
        }
        p1[3] = p2[3] = midpoint[i][3] = 1.0f;
        len = g3dxVu0Sqrt(sceVu0InnerProduct(midpoint[i], midpoint[i]));
        scale = fRange / len;
        for (int j = 0; j < 4; j++)
        {
            midpoint[i][j] *= scale;
            if (vScale != 0)
            {
                midpoint[i][j] *= vScale[j];
            }
        }
        p1[0] += x; p1[1] += y; p1[2] += z;
        p2[0] += x; p2[1] += y; p2[2] += z;
        midpoint[i][0] += x;
        midpoint[i][1] += y;
        midpoint[i][2] += z;
        DrawLine(p1, r, g, b, a, midpoint[i], r, g, b, a);
        DrawLine(p2, r, g, b, a, midpoint[i], r, g, b, a);
    }

    for (int i = 0; i < 20; i++)
    {
        float *p0 = midpoint[bunkatu[i][0]];
        float *p1 = midpoint[bunkatu[i][1]];
        float *p2 = midpoint[bunkatu[i][2]];
        DrawLine(p0, r, g, b, a, p1, r, g, b, a);
        DrawLine(p1, r, g, b, a, p2, r, g, b, a);
        DrawLine(p2, r, g, b, a, p0, r, g, b, a);
    }
}

void DrawTube(float *p1, float *p2, float rad,
              u_char r, u_char g, u_char b, u_char a)
{
    static const float c = 0.923879445f;
    static const float d = 0.3826834f;
    static const float e = 0.70710665f;
    static const float vdata[16][4] = {
        { 1.0f,0.0f, 0.0f,1.0f},{ c,0.0f, d,1.0f},{ e,0.0f, e,1.0f},
        { d,0.0f, c,1.0f},{0.0f,0.0f, 1.0f,1.0f},{-d,0.0f, c,1.0f},
        {-e,0.0f, e,1.0f},{-c,0.0f, d,1.0f},{-1.0f,0.0f,0.0f,1.0f},
        {-c,0.0f,-d,1.0f},{-e,0.0f,-e,1.0f},{-d,0.0f,-c,1.0f},
        {0.0f,0.0f,-1.0f,1.0f},{ d,0.0f,-c,1.0f},{ e,0.0f,-e,1.0f},
        { c,0.0f,-d,1.0f},
    };
    sceVu0FMATRIX mtx0;
    sceVu0FMATRIX mtx1;
    sceVu0FVECTOR y_axis = {0.0f, 1.0f, 0.0f, 0.0f};
    sceVu0FVECTOR tmp;
    sceVu0FVECTOR normal;
    float rot_z;
    float rot_x;

    sceVu0UnitMatrix(mtx0);
    sceVu0UnitMatrix(mtx1);
    sceVu0SubVector(tmp, p1, p2);
    tmp[2] = 0.0f;
    sceVu0Normalize(tmp, tmp);
    rot_z = g3dAcosf(sceVu0InnerProduct(y_axis, tmp));
    sceVu0OuterProduct(normal, y_axis, tmp);
    if (normal[2] > 0.0f)
    {
        rot_z = -rot_z;
    }
    sceVu0RotMatrixZ(mtx0, mtx0, rot_z);
    sceVu0RotMatrixZ(mtx1, mtx1, rot_z);

    sceVu0SubVector(tmp, p1, p2);
    tmp[0] = 0.0f;
    sceVu0Normalize(tmp, tmp);
    rot_x = g3dAcosf(sceVu0InnerProduct(y_axis, tmp));
    sceVu0OuterProduct(normal, y_axis, tmp);
    if (normal[0] < 0.0f)
    {
        rot_x = -rot_x;
    }
    sceVu0RotMatrixX(mtx0, mtx0, rot_x);
    sceVu0RotMatrixX(mtx1, mtx1, rot_x);
    sceVu0TransMatrix(mtx0, mtx0, p1);
    sceVu0TransMatrix(mtx1, mtx1, p2);

    for (int i = 0; i < 16; i++)
    {
        int next = (i + 1) & 15;
        float local0[4];
        float local1[4];
        float a0[4], a1[4], b0[4], b1[4];

        for (int j = 0; j < 3; j++)
        {
            local0[j] = vdata[i][j] * rad;
            local1[j] = vdata[next][j] * rad;
        }
        local0[3] = local1[3] = 1.0f;
        sceVu0ApplyMatrix(a0, mtx0, local0);
        sceVu0ApplyMatrix(a1, mtx0, local1);
        sceVu0ApplyMatrix(b0, mtx1, local0);
        sceVu0ApplyMatrix(b1, mtx1, local1);
        DrawLine(a0, r, g, b, a, a1, r, g, b, a);
        DrawLine(b0, r, g, b, a, b1, r, g, b, a);
        DrawLine(a0, r, g, b, a, b0, r, g, b, a);
    }
    DrawSphere(rad, p1[0], p1[1], p1[2], r, g, b, a, 0);
    DrawSphere(rad, p2[0], p2[1], p2[2], r, g, b, a, 0);
}

void CaptureScreen(u_int addr)
{
    MioPan_RendererCaptureScreen(addr);
}

void DrawScreen(u_int pri, u_int addr, u_char r, u_char g, u_char b, u_char a)
{
    SPRT_DAT2 sd = {};
    DISP_SPRT2 ds;
    (void)pri;

    sd.tex0 = 0;
    sd.u1 = 0.1f;
    sd.v1 = 0.1f;
    sd.u2 = 639.9f;
    sd.v2 = 447.9f;
    sd.w = 640.0f;
    sd.h = 448.0f;
    sd.x = -0.5f;
    sd.y = -0.5f;
    sd.pri = 0xa0;
    sd.alpha = 0x80;
    CopySprDToSpr2(&ds, &sd);
    ds.tex0 = 0x200000026812abc0ULL;
    ds.zbuf = 0x10a000118;
    ds.test = 0x30003;
    ds.pri = 0x10;
    ds.z = 0xfff00;
    ds.r = r;
    ds.g = g;
    ds.b = b;
    ds.alpreg = ((u_long)a << 32) | 0x64;
    DispSprD2(&ds);
    MioPan_RendererDrawCapturedScreen(addr, r, g, b, a);
}

/* PORT-ONLY.  The host counterpart of a 3D textured GS packet: projects the
 * world-space corners itself and hands the triangles to SDL, because the DIRECT
 * packet the ROM builds alongside is never executed here.  Non-static so the
 * effect packet builders (Set3DPosTexure) can queue the same host draw. */
void RendererPacket3DUV(float (*positions)[4], int count, const float *uv,
                        int r, int g, int b, int a, const sceGsTex0 *tex0,
                        int z_offset)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_BILLBOARD_HOST_BRIDGE);
    const int index4[6] = {0, 1, 2, 1, 3, 2};
    const int index3[3] = {0, 1, 2};
    const int *indices;
    int vertex_count;
    float host_positions[24];
    float host_uv[12];
    float host_rgba[24];

    MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_REQUESTS, 1);

    if ((MioPan_RendererGetDebugViewFlags() &
         MIOPAN_RENDERER_DEBUG_DISABLE_BILLBOARD_HOST) != 0)
    {
        MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_SUPPRESSED,
                                  1);
        return;
    }

    if (count < 3 || uv == NULL)
    {
        MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_REJECTED, 1);
        return;
    }

    indices = count >= 4 ? index4 : index3;
    vertex_count = count >= 4 ? 6 : 3;
    for (int i = 0; i < vertex_count; i++)
    {
        int source = indices[i];

        if (!ProjectWorldToHostClip(host_positions + i * 4,
                                    positions[source]))
        {
            MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_REJECTED,
                                      1);
            return;
        }
        host_uv[i * 2 + 0] = uv[source * 2 + 0];
        host_uv[i * 2 + 1] = uv[source * 2 + 1];
        host_rgba[i * 4 + 0] = HostColor(r);
        host_rgba[i * 4 + 1] = HostColor(g);
        host_rgba[i * 4 + 2] = HostColor(b);
        host_rgba[i * 4 + 3] = HostColor(a);
    }
    /* The GS-Z bias the DIRECT packet applies below, carried across as an NDC
     * offset.  The draw environment that uses it is PSMZ24, so the GS depth
     * range is 0..0xffffff.
     *
     * NEGATED, because the two spaces run opposite ways here.  The renderer's
     * own GS->NDC conversion is Gs2dZToNdc(): `1 - 2 * (gs_z / 65535)`, so a
     * LARGER GS Z -- which is nearer on the GS -- becomes a SMALLER NDC z.
     * Carrying the ROM's +100 across unchanged pushed the movie-room screen
     * the wrong way and parked it behind the projector screen mesh it is
     * coplanar with, which is exactly where it was invisible.
     *
     * Without a bias at all it draws at its exact world depth and loses the
     * tie to that mesh regardless of which of them is drawn first. */
    MioPan_RendererDrawBillboardTriangles(tex0, host_positions, host_uv,
                                          host_rgba, vertex_count, 1,
                                          -(float)z_offset / 16777215.0f);
}

void RendererGouraudTriangles3D(float (*positions)[4], const u_char *rgba,
                                int vertex_count)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_BILLBOARD_HOST_BRIDGE);
    static float clip_positions[108 * 4];
    static float host_rgba[108 * 4];

    if (positions == NULL || rgba == NULL || vertex_count <= 0 ||
        vertex_count > 108 || (vertex_count % 3) != 0)
    {
        return;
    }

    for (int i = 0; i < vertex_count; i++)
    {
        if (!ProjectWorldToHostClip(clip_positions + i * 4, positions[i]))
        {
            return;
        }
        /* TME is clear on this primitive, so colour divides by 255 and alpha
         * by 128 -- the split HostFillColor/HostColor exist for. */
        host_rgba[i * 4 + 0] = HostFillColor(rgba[i * 4 + 0]);
        host_rgba[i * 4 + 1] = HostFillColor(rgba[i * 4 + 1]);
        host_rgba[i * 4 + 2] = HostFillColor(rgba[i * 4 + 2]);
        host_rgba[i * 4 + 3] = HostColor(rgba[i * 4 + 3]);
    }

    MioPan_RendererDrawClipTriangles(NULL, clip_positions, NULL, host_rgba,
                                     vertex_count, 1);
}

void RendererPacket3D(float (*positions)[4], int count,
                      int r, int g, int b, int a,
                      float ux, float uy, float uw, float uh,
                      float tw, float th, const sceGsTex0 *tex0,
                      int z_offset)
{
    /* The sub-rectangle spread over the strip's TL, TR, BL, BR corners. */
    const float base_uv[8] = {
        ux / tw,        uy / th,
        (ux + uw) / tw, uy / th,
        ux / tw,        (uy + uh) / th,
        (ux + uw) / tw, (uy + uh) / th,
    };

    RendererPacket3DUV(positions, count, base_uv, r, g, b, a, tex0, z_offset);
}

void MakePacket3D(float (*pa3DPos)[4], int iNum,
                  int iR, int iG, int iB, int iA,
                  float fUX, float fUY, float fUW, float fUH,
                  sceGsTex0 Tex0, int iZOffset)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_BILLBOARD_CPU);
    GRA3DCAMERA *camera = gra3dGetCamera();
    Q_WORDDATA *pbuf;
    int ndpkt = 4;
    int remaining = iNum;
    float fTW = (float)(1U << Tex0.TW);
    float fTH = (float)(1U << Tex0.TH);
    u_long tex0_value;

    /* SDL projects and clips these vertices independently of the original GS
     * packet.  Queue that path first so a disagreement in the emulated PS2
     * fixed-point projection cannot make an otherwise visible host billboard
     * disappear. */
    {
        int host_remaining = iNum;
        float (*host_pos)[4] = pa3DPos;

        while (host_remaining > 0)
        {
            int wrk = host_remaining < 4 ? host_remaining : 4;
            RendererPacket3D(host_pos, wrk, iR, iG, iB, iA,
                             fUX, fUY, fUW, fUH, fTW, fTH, &Tex0, iZOffset);
            host_remaining -= wrk;
            host_pos += wrk;
        }
    }

    if ((MioPan_RendererGetDebugViewFlags() &
         MIOPAN_RENDERER_DEBUG_SKIP_BILLBOARD_LEGACY_PACKETS) != 0)
    {
        MioPan_ProfilerAddCounter(MIOPAN_PROFILER_COUNTER_BILLBOARD_LEGACY_SKIPS,
                                  1);
        return;
    }

    /* The ROM abandons the whole DIRECT packet when any projected vertex is
     * clipped.  Validate before opening the DMA packet so an early return
     * cannot leave its allocator transaction open.  Host drawing above is an
     * independent path and deliberately survives this PS2-only cull. */
    {
        int check_remaining = iNum;
        float (*check_pos)[4] = pa3DPos;

        while (check_remaining > 0)
        {
            int wrk = check_remaining < 4 ? check_remaining : 4;
            sceVu0IVECTOR ivec[4] = {};

            sceVu0RotTransPersN(ivec, camera->matWorldScreen, check_pos,
                                wrk, 1);

            for (int i = 0; i < wrk; i++)
            {
                if ((u_int)ivec[i][0] < 0x4000U ||
                    (u_int)ivec[i][0] > 0xc000U ||
                    (u_int)ivec[i][1] < 0x4000U ||
                    (u_int)ivec[i][1] > 0xc000U || ivec[i][2] == 0 ||
                    (u_int)ivec[i][2] > 0xffffffU)
                {
                    return;
                }
            }
            check_remaining -= wrk;
            check_pos += wrk;
        }
    }

    pbuf = StartDmaDirectTrans();
    memcpy(&tex0_value, &Tex0, sizeof(tex0_value));
    pbuf[0].ul64[0] = 0x1000000000008002ULL;
    pbuf[0].ul64[1] = 0x0e;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = 0x3f;
    pbuf[2].ul64[0] = tex0_value;
    pbuf[2].ul64[1] = 6;
    pbuf[3].ul64[0] = 0x302a400000008000ULL | (u_long)iNum;
    pbuf[3].ul64[1] = 0x412;

    while (remaining > 0)
    {
        int wrk = remaining < 4 ? remaining : 4;
        sceVu0IVECTOR ivec[4] = {};
        float tq[4] = {};
        float ts[4] = {};
        float tt[4] = {};
        int clipped = 0;

        sceVu0RotTransPersN(ivec, camera->matWorldScreen, pa3DPos, wrk, 1);
        for (int i = 0; i < wrk; i++)
        {
            if ((u_int)ivec[i][0] < 0x4000U || (u_int)ivec[i][0] > 0xc000U ||
                (u_int)ivec[i][1] < 0x4000U || (u_int)ivec[i][1] > 0xc000U ||
                ivec[i][2] == 0 || (u_int)ivec[i][2] > 0xffffffU)
            {
                clipped = 1;
            }
            tq[i] = 1.0f / (float)ivec[i][3];
        }
        if (clipped)
        {
            return;
        }

        const float sbase[4] = {fUX, fUX + fUW, fUX, fUX + fUW};
        const float tbase[4] = {fUY, fUY, fUY + fUH, fUY + fUH};
        for (int i = 0; i < wrk; i++)
        {
            ts[i] = (sbase[i] * tq[i]) / fTW;
            tt[i] = (tbase[i] * tq[i]) / fTH;
            pbuf[ndpkt].fl32[0] = ts[i];
            pbuf[ndpkt].fl32[1] = tt[i];
            pbuf[ndpkt].fl32[2] = tq[i];
            pbuf[ndpkt].ui32[3] = 0;
            SetPacketColor(pbuf + ndpkt + 1, iR, iG, iB, iA);
            SetPacketPosition(pbuf + ndpkt + 2, ivec[i][0], ivec[i][1],
                              (ivec[i][2] + iZOffset) * 16,
                              i < 2 ? 0x8000 : 0);
            ndpkt += 3;
        }
        remaining -= 4;
        pa3DPos += 4;
    }
    EndDmaDirectTrans(pbuf + ndpkt);
}

void MakeBillBoardPacket(float *Pos, float fWidth, float fHeight,
                         int iR, int iG, int iB, int iA,
                         float fUX, float fUY, float fUW, float fUH,
                         sceGsTex0 Tex0, int iZOffset)
{
    GRA3DCAMERA *camera = gra3dGetCamera();
    float aPos[4][4];
    float temp[4];
    float add_w[4];
    float add_h[4];

    sceVu0ScaleVector(add_h, g_v0100, fHeight * 0.5f);
    sceVu0ScaleVector(add_w, camera->matCoord[0], fWidth * 0.5f);
    sceVu0SubVector(temp, Pos, add_h);
    sceVu0SubVector(aPos[0], temp, add_w);
    sceVu0AddVector(aPos[1], temp, add_w);
    sceVu0AddVector(temp, Pos, add_h);
    sceVu0SubVector(aPos[2], temp, add_w);
    sceVu0AddVector(aPos[3], temp, add_w);

    if (*key_now[10] == 0)
    {
        MakePacket3D(aPos, 4, iR, iG, iB, iA,
                     fUX, fUY, fUW, fUH, Tex0, iZOffset);
    }
    else
    {
        Draw3DSquare(aPos[0], aPos[1], aPos[2], aPos[3],
                     (u_char)iR, (u_char)iG, (u_char)iB, (u_char)iA);
    }
}
