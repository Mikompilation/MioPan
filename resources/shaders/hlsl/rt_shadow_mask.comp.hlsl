// The screen-space shadow mask: ray-traced visibility of up to four lights at
// every visible fragment-lit pixel, averaged over frames.
//
// Why a pass of its own rather than tracing in mesh_spot_rt.frag: the forward
// shader runs for every fragment of every draw, overdraw included, and can
// only afford one hard-edged ray per light.  Here each visible pixel is traced
// once, and the answer is blended into the previous frame's, so a single
// jittered ray per frame converges on a soft edge.  This game mostly holds its
// cameras still, so the history is valid almost everywhere almost always.
//
// History is per pixel, not reprojected: it is kept where the G-buffer shows
// the same surface at the same pixel as last frame (the previous position is
// within uMaskTuning.x), and discarded elsewhere -- a camera move, something
// walking across.  Discarded pixels take uMaskTuning.y rays instead of one,
// so they are only noisier, never wrong.
//
// Channels hold one light each (uMaskLightPos, picked on the CPU: Mio's
// flashlight first, then the lights brightest near her).  Which channels a
// pixel needs is the G-buffer's normal.w, one bit each: the forward shader's
// own "is this lane bright enough to trace" decision, made in the prepass with
// the visible draw's own light block.  A channel that has just changed light,
// or whose light moved, blends faster or starts over (uMaskBlend).  Lanes the
// mask does not hold are traced by the forward shader as before.
//
// Ambient occlusion rides along in a texture of its own (uAoOut): a few short
// rays over the hemisphere per pixel, x = the fraction that got out, averaged
// over up to uMaskAo.w frames (y counts them) where the history holds.  The
// forward shader scales the ambient and baked light by it -- never the lamps,
// which have shadows of their own.
//
// The same texture's z and w carry the baked shadows (port addition): the
// rooms' meshes get their lamps' light -- every point and spot light but Mio's
// own -- from the prelight bake, not a live lane, so no lane's shadow reaches
// them.  w is how much light the bake
// gave this pixel from the lamps in uBakeLamp, by the bake's own law for a
// white material, in GS 0..255; z the fraction of it still getting through.
// Each frame one lamp is picked at random, weighted by its share of w, and
// one ray traced to it, so the running average of the answers is that
// fraction whatever the number of lamps.  See MioPanRtShadowBake().

#define MIOPAN_RT_COMPUTE 1

cbuffer MioPanRtMaskUniforms : register(b0, space2)
{
    // The names miopan_rt.hlsli reads, with the forward block's meanings.
    int4 uRtFlags;
    float4 uRtParams;
    float4 uRtPlayer;

    // xyz = position, w = 0 none / 1 spot / 2 point, plus 4 for one of Mio's
    // own lights, which her own triangles never shadow.
    float4 uMaskLightPos[4];
    // Weight of this frame's answer per channel; 1 discards the history.
    float4 uMaskBlend;
    // x, y = size; z = frame counter; w = 1 when last frame's mask exists.
    float4 uMaskSize;
    // x = squared distance the surface may have moved and keep its history,
    // y = rays per light where there is no history.
    float4 uMaskTuning;
    // Ambient occlusion: x = ray length (0 = off), y = rays per pixel per
    // frame, z = rays where there is no history, w = frames the average runs
    // to (0 = last frame's occlusion is not usable).
    float4 uMaskAo;
    // Baked shadows: x = lamps (0 = off), y = 1 when last frame's estimate
    // can be blended into, z = this frame's weight.
    float4 uBakeInfo;
    // Each lamp as the prelight bake read it, four rows: (position, max range),
    // (raw diffuse, 1 / (max - min range)), (beam direction, cos outside),
    // (1 / (cos inside - cos outside), 1 point / 2 spot, 0, 0).
    float4 uBakeLamp[64];
};

Texture2D<float4> uGPos      : register(t0, space0);
Texture2D<float4> uGNormal   : register(t1, space0);
Texture2D<float4> uHistory   : register(t2, space0);
Texture2D<float4> uPrevGPos  : register(t3, space0);
Texture2D<float4> uAoHistory : register(t4, space0);
RWTexture2D<float4> uMaskOut : register(u0, space1);
RWTexture2D<float4> uAoOut   : register(u1, space1);

#include "miopan_rt.hlsli"

// What the prelight bake gave P from lamp `l` (g3dCore.c's prelight form,
// white material): raw diffuse * N.L times the linear range ramp and, for a
// spot, the cone ramp -- a point outside the cone dropped, as the bake drops
// it -- capped at full on its own as the bake caps each light, in GS 0..255,
// brightest channel.
float BakedLampLight(int l, float3 P, float3 N)
{
    float4 a = uBakeLamp[l * 4];
    float4 b = uBakeLamp[l * 4 + 1];
    float4 s = uBakeLamp[l * 4 + 2];
    float4 k = uBakeLamp[l * 4 + 3];
    float3 L = a.xyz - P;
    float d = length(L);
    if (!(d > 0.0))
    {
        return 0.0;
    }
    float3 Ln = L / d;
    float ndl = dot(N, Ln);
    float ramp = saturate((a.w - d) * b.w);
    if (ndl <= 0.0 || ramp <= 0.0)
    {
        return 0.0;
    }
    float cone = 1.0;
    if (k.y > 1.5)
    {
        cone = (dot(s.xyz, -Ln) - s.w) * k.x;
        if (cone <= 0.0)
        {
            return 0.0;
        }
        cone = min(cone, 1.0);
    }
    float3 c = min(b.rgb * (ndl * ramp * cone), 1.0.xxx);
    return max(c.x, max(c.y, c.z)) * 255.0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= (uint)uMaskSize.x || id.y >= (uint)uMaskSize.y)
    {
        return;
    }
    int3 at = int3(id.xy, 0);
    float4 g = uGPos.Load(at);
    if (g.w <= 0.0)
    {
        uMaskOut[id.xy] = float4(1.0, 1.0, 1.0, 1.0);
        uAoOut[id.xy] = float4(1.0, 0.0, 1.0, 0.0);
        return;
    }
    float3 P = g.xyz;
    float4 nrm = uGNormal.Load(at);
    float3 N = normalize(nrm.xyz);
    uint wanted = (uint)(nrm.w + 0.5);

    float4 prev = uPrevGPos.Load(at);
    float3 moved = prev.xyz - P;
    bool history = uMaskSize.w > 0.0 && prev.w > 0.0 &&
                   dot(moved, moved) <= uMaskTuning.x;
    float4 old = history ? uHistory.Load(at) : float4(1.0, 1.0, 1.0, 1.0);
    int rays = history ? 1 : max((int)uMaskTuning.y, 1);

    gMioPanRtPixel = float2(id.xy);
    gMioPanRtFrame = fmod(uMaskSize.z, 64.0);

    float4 result = float4(1.0, 1.0, 1.0, 1.0);
    [unroll] for (int c = 0; c < 4; c++)
    {
        if (uMaskLightPos[c].w < 0.5)
        {
            continue;
        }
        float vis = 1.0;
        if ((wanted & (1u << (uint)c)) != 0)
        {
            // w: 1 spot / 2 point, plus 4 for one of Mio's own lights.
            vis = MioPanRtVisibilityN(P, N, uMaskLightPos[c].xyz - P, false,
                                      rays, true, uMaskLightPos[c].w > 3.5);
        }
        float w = history ? uMaskBlend[c] : 1.0;
        result[c] = lerp(old[c], vis, w);
    }
    uMaskOut[id.xy] = result;

    float4 ao = float4(1.0, 0.0, 1.0, 0.0);
    if (uMaskAo.x > 0.0)
    {
        bool aoHistory = history && uMaskAo.w > 0.0;
        float4 aoOld = aoHistory ? uAoHistory.Load(at) : float4(1.0, 0.0, 1.0, 0.0);
        int aoRays = aoHistory ? (int)uMaskAo.y : (int)uMaskAo.z;
        float open = MioPanRtAmbientVisibility(P, N, uMaskAo.x, aoRays);
        // A running mean until it holds uMaskAo.w frames, then a moving one,
        // so a still pixel settles fast and a changed one still catches up.
        float frames = aoHistory ? min(aoOld.y + 1.0, uMaskAo.w) : 1.0;
        ao.xy = float2(lerp(aoOld.x, open, 1.0 / frames), frames);
    }

    int lamps = min((int)uBakeInfo.x, 16);
    if (lamps > 0)
    {
        float share[16];
        float total = 0.0;
        [loop] for (int l = 0; l < lamps; l++)
        {
            share[l] = BakedLampLight(l, P, N);
            total += share[l];
        }
        ao.w = total;
        if (total >= uRtParams.w)
        {
            bool bakeHistory = history && uBakeInfo.y > 0.0;
            float bakeOld = bakeHistory ? uAoHistory.Load(at).z : 1.0;
            int picks = bakeHistory ? 1 : rays;
            float seen = 0.0;
            [loop] for (int k = 0; k < picks; k++)
            {
                // Its own stream, apart from the lights' and the occlusion's.
                float pick = MioPanRtNoise(float2(id.xy) + float2(41.0, 7.0) * (float)(k + 1) +
                                           3.718281 * gMioPanRtFrame) * total;
                int chosen = lamps - 1;
                float run = 0.0;
                [loop] for (int m = 0; m < lamps; m++)
                {
                    run += share[m];
                    if (pick < run)
                    {
                        chosen = m;
                        break;
                    }
                }
                seen += MioPanRtVisibilityN(P, N, uBakeLamp[chosen * 4].xyz - P,
                                            false, 1, true, false);
            }
            float vis = seen / (float)picks;
            ao.z = bakeHistory ? lerp(bakeOld, vis, uBakeInfo.z) : vis;
        }
    }
    uAoOut[id.xy] = ao;
}
