#ifndef MIOPAN_RT_HLSLI
#define MIOPAN_RT_HLSLI

// Ray-traced shadows for the VU1 light image -- a proof of concept.
//
// What it does: every positional light lane the kernel in
// miopan_vu1_lighting.hlsli evaluates per pixel is multiplied by whether the
// pixel can see that light, found by casting one shadow ray (or a few, for a
// soft edge) through a bounding volume hierarchy of the frame's world
// geometry.  Nothing else about the light law changes: the prelight bake, the
// ambient term and the PS2's attenuation are exactly as before, so the only
// difference on screen is that light no longer passes through things.
//
// How: SDL_GPU exposes no hardware ray tracing, so the hierarchy is built on
// the CPU (miopan_rt_bvh.cpp) and walked here in ordinary shader code.  It
// runs on any GPU the renderer already runs on.  Two hierarchies are bound:
// STATIC holds room geometry accumulated over many frames, so occluders the
// camera is not looking at still cast; DYNAMIC holds this frame's characters
// and anything else that moves.
//
// Layouts mirror MioPanRt::GpuNode / GpuTri (miopan_rt_bvh.h):
//   node  = two float4s: (bmin, asuint left_first), (bmax, asuint count);
//           count > 0 is a leaf over triangles [left_first, +count), otherwise
//           the children are left_first and left_first + 1.
//   tri   = three float4s: v0, v1 - v0, v2 - v0.

// Two includers, two places in SDL_GPU's binding model:
//
//   mesh_spot_rt.frag   storage buffers follow the sampled textures in space 2:
//                       uTexture, uAuxTexture, then the shadow mask, the
//                       G-buffer position it is checked against and the
//                       ambient occlusion (t0..t4).
//   rt_shadow_mask.comp read-only storage buffers follow the read-only storage
//                       textures in space 0 (five of them, t0..t4).
//
// Either way the four hierarchy buffers are t5..t8, in this order.  The
// includer provides uRtFlags / uRtParams / uRtPlayer (mikupan_common.hlsli's
// MikuPanUniforms for the fragment stage, the mask's own block for compute).
#ifdef MIOPAN_RT_COMPUTE
#define MIOPAN_RT_SPACE space0
#else
#define MIOPAN_RT_SPACE space2
#endif
StructuredBuffer<float4> uRtStaticNodes  : register(t5, MIOPAN_RT_SPACE);
StructuredBuffer<float4> uRtStaticTris   : register(t6, MIOPAN_RT_SPACE);
StructuredBuffer<float4> uRtDynamicNodes : register(t7, MIOPAN_RT_SPACE);
StructuredBuffer<float4> uRtDynamicTris  : register(t8, MIOPAN_RT_SPACE);

#ifndef MIOPAN_RT_COMPUTE
// The screen-space shadow mask (rt_shadow_mask.comp): visibility of up to four
// lights per pixel, and the world position of the surface it was traced for.
// Read with Load -- the samplers only exist because SDL_GPU binds a sampled
// texture with one.
Texture2D uRtMask : register(t2, space2);
SamplerState uRtMaskSampler : register(s2, space2);
Texture2D uRtGPos : register(t3, space2);
SamplerState uRtGPosSampler : register(s3, space2);
// Ray-traced ambient occlusion from the same pass: x = the fraction of the
// hemisphere left open, y = frames averaged.
Texture2D uRtAoTex : register(t4, space2);
SamplerState uRtAoSampler : register(s4, space2);
#endif

// Mirrors MIOPAN_RT_* in miopan_renderer.h.
#define MIOPAN_RT_SPOT        1
#define MIOPAN_RT_POINT       2
#define MIOPAN_RT_DIRECTIONAL 4
#define MIOPAN_RT_DEBUG_LIGHT 8
#define MIOPAN_RT_DEBUG_RAYS  16
// Treat every triangle as two-sided, overriding the per-triangle winding.
#define MIOPAN_RT_TWO_SIDED   32
#define MIOPAN_RT_DEBUG_BOUNCE 64
#define MIOPAN_RT_DEBUG_AO     128

// Per-triangle flags in v0.w; mirror MioPanRt::kTri* (miopan_rt_bvh.h).
#define MIOPAN_RT_TRI_PLAYER    1
#define MIOPAN_RT_TRI_TWO_SIDED 2
#define MIOPAN_RT_TRI_CUTOUT    4

// The builder caps tree depth at 30 so a left-first walk never needs more.
#define MIOPAN_RT_STACK 32
// A bad hierarchy must not hang the GPU; a ray this long is treated as clear.
#define MIOPAN_RT_MAX_STEPS 4096
// How far a directional light's ray looks.  Well past any room.
static const float kMioPanRtFar = 100000.0;

// Set by the includer before tracing; seeds the soft-shadow sample positions.
// gMioPanRtFrame moves them from one frame to the next, which is what lets
// the mask average a soft edge out of one ray per frame.
static float2 gMioPanRtPixel = float2(0.0, 0.0);
static float gMioPanRtFrame = 0.0;
// Lanes that cast a ray this pixel, how much of them was shadowed, and how
// many of those answers came from the mask -- for MIOPAN_RT_DEBUG_RAYS.
static float gMioPanRtTraced = 0.0;
static float gMioPanRtShadowed = 0.0;
static float gMioPanRtFromMask = 0.0;
// The mask at this pixel, when it was traced for this very surface.
static bool gMioPanRtMaskOk = false;
static float4 gMioPanRtMask = float4(1.0, 1.0, 1.0, 1.0);
// What this pixel's ambient light is scaled by, already shaped for the GS's
// gamma-encoded units (see MioPanRtLoadMask()).  1 where there is no answer.
static float gMioPanRtAo = 1.0;

float4 MioPanRtNode(uint which, uint index)
{
    [branch] if (which == 0)
    {
        return uRtStaticNodes[index];
    }
    return uRtDynamicNodes[index];
}

float4 MioPanRtTri(uint which, uint index)
{
    [branch] if (which == 0)
    {
        return uRtStaticTris[index];
    }
    return uRtDynamicTris[index];
}

bool MioPanRtBoxHit(float3 bmin, float3 bmax, float3 origin, float3 invDir,
                    float tmax)
{
    float3 t0 = (bmin - origin) * invDir;
    float3 t1 = (bmax - origin) * invDir;
    float3 tn = min(t0, t1);
    float3 tf = max(t0, t1);
    float tnear = max(max(tn.x, tn.y), max(tn.z, 0.0));
    float tfar = min(min(tf.x, tf.y), min(tf.z, tmax));
    return tnear <= tfar;
}

// Moller-Trumbore.  The corners are stored so cross(e1, e2) is the front
// normal, and det = -dot(dir, front): a ONE-SIDED triangle blocks the ray only
// when det < 0, i.e. when the light (along +dir) sees its front face.  That is
// the shadow-map rule, and the one the PS2's lighting rigs need -- see
// MioPanRt::kTriTwoSided.
bool MioPanRtTriHit(float3 origin, float3 dir, float3 v0, float3 e1, float3 e2,
                    float tmax, bool twoSided)
{
    float3 p = cross(dir, e2);
    float det = dot(e1, p);
    if (twoSided ? abs(det) < 1e-12 : det > -1e-12)
    {
        return false;
    }
    float inv = 1.0 / det;
    float3 s = origin - v0;
    float u = dot(s, p) * inv;
    if (u < 0.0 || u > 1.0)
    {
        return false;
    }
    float3 q = cross(s, e1);
    float v = dot(dir, q) * inv;
    if (v < 0.0 || u + v > 1.0)
    {
        return false;
    }
    float t = dot(e2, q) * inv;
    return t > 0.0 && t < tmax;
}

// Any-hit: a shadow ray only needs to know that something is in the way.
// `skipPlayer` ignores triangles flagged as Mio's (v0.w = 1), for lights she
// carries.  `bothSides` makes every triangle block from either side, for
// ambient occlusion, whose rays leave the surface rather than arrive at a
// light: there the faces that matter are the ones turned TOWARDS the ray --
// the wall beside a floor, the underside of a shelf -- which the one-sided
// shadow rule lets through.  It also lets a ray through a part see-through
// triangle (MIOPAN_RT_TRI_CUTOUT) with the odds of meeting a hole, so a
// lattice darkens what is behind it by how much of it is lattice instead of
// blacking out whatever shows through its holes.
bool MioPanRtOccludedIn(uint which, float3 origin, float3 dir, float3 invDir,
                        float tmax, bool skipPlayer, bool bothSides)
{
    uint stack[MIOPAN_RT_STACK];
    uint sp = 0;
    uint node = 0;

    [loop] for (uint step = 0; step < MIOPAN_RT_MAX_STEPS; step++)
    {
        float4 a = MioPanRtNode(which, node * 2);
        float4 b = MioPanRtNode(which, node * 2 + 1);
        if (MioPanRtBoxHit(a.xyz, b.xyz, origin, invDir, tmax))
        {
            uint first = asuint(a.w);
            uint count = asuint(b.w);
            if (count == 0)
            {
                if (sp < MIOPAN_RT_STACK)
                {
                    stack[sp] = first + 1;
                    sp++;
                }
                node = first;
                continue;
            }
            [loop] for (uint i = 0; i < count; i++)
            {
                uint t = (first + i) * 3;
                float4 v0 = MioPanRtTri(which, t);
                uint triFlags = (uint)(v0.w + 0.5);
                if (skipPlayer && (triFlags & MIOPAN_RT_TRI_PLAYER) != 0)
                {
                    continue;
                }
                if (bothSides && (triFlags & MIOPAN_RT_TRI_CUTOUT) != 0)
                {
                    // Opacity 0..15 in the top bits; the dice are this ray
                    // and this triangle, so the mask's average over frames
                    // converges on the fraction.
                    float solid = (float)(triFlags >> 4) / 15.0;
                    float dice = frac(sin(dot(dir, float3(12.9898, 78.233, 37.719)) +
                                          (float)(first + i) * 0.618034) *
                                      43758.5453);
                    if (dice >= solid)
                    {
                        continue;
                    }
                }
                bool twoSided = bothSides ||
                                (triFlags & MIOPAN_RT_TRI_TWO_SIDED) != 0 ||
                                (uRtFlags.x & MIOPAN_RT_TWO_SIDED) != 0;
                if (MioPanRtTriHit(origin, dir, v0.xyz,
                                   MioPanRtTri(which, t + 1).xyz,
                                   MioPanRtTri(which, t + 2).xyz, tmax,
                                   twoSided))
                {
                    return true;
                }
            }
        }
        if (sp == 0)
        {
            break;
        }
        sp--;
        node = stack[sp];
    }
    return false;
}

bool MioPanRtOccludedSides(float3 origin, float3 dir, float tmax,
                           bool skipPlayer, bool bothSides)
{
    // Keep each component's sign but never let it reach zero, so the slab
    // test divides into +/-huge rather than producing 0 * inf = NaN.
    float3 safe = float3(dir.x >= 0.0 ? max(dir.x, 1e-8) : min(dir.x, -1e-8),
                         dir.y >= 0.0 ? max(dir.y, 1e-8) : min(dir.y, -1e-8),
                         dir.z >= 0.0 ? max(dir.z, 1e-8) : min(dir.z, -1e-8));
    float3 invDir = 1.0 / safe;
    // Mio is only ever in the dynamic hierarchy, so the static walk never
    // needs her flag.
    if (uRtFlags.y > 0 &&
        MioPanRtOccludedIn(0, origin, dir, invDir, tmax, false, bothSides))
    {
        return true;
    }
    if (uRtFlags.z > 0 &&
        MioPanRtOccludedIn(1, origin, dir, invDir, tmax, skipPlayer,
                           bothSides))
    {
        return true;
    }
    return false;
}

bool MioPanRtOccluded(float3 origin, float3 dir, float tmax, bool skipPlayer)
{
    return MioPanRtOccludedSides(origin, dir, tmax, skipPlayer, false);
}

// A light inside the sphere around Mio is one she carries.
bool MioPanRtLightIsPlayers(float3 lightPos)
{
    float3 d = lightPos - uRtPlayer.xyz;
    return uRtPlayer.w > 0.0 && dot(d, d) < uRtPlayer.w * uRtPlayer.w;
}

float MioPanRtNoise(float2 pixel)
{
    // Interleaved gradient noise (Jimenez 2014).
    return frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
}

// Fraction of the light at `P + toLight` that P can see, from `samples` rays.
// toLight is the kernel's own unnormalised light vector for spot and point
// lanes, and the direction towards the light for a directional one.
//
// With a light radius the rays aim at points on a disc facing P.  `temporal`
// moves those points every frame (gMioPanRtFrame) instead of fixing them per
// pixel, so a single ray still gives a soft edge once the mask has averaged a
// few frames of it.
float MioPanRtVisibilityN(float3 P, float3 N, float3 toLight, bool directional,
                          int samples, bool temporal)
{
    float bias = uRtParams.x;
    float lenL = length(toLight);
    if (!(lenL > 0.0))
    {
        return 1.0;
    }
    float3 dirL = toLight / lenL;
    // Step off the surface on the side the light is on, so the ray does not
    // start inside the triangle it leaves.
    float3 n = dot(N, dirL) >= 0.0 ? N : -N;
    float3 origin = P + n * bias;

    if (directional)
    {
        return MioPanRtOccluded(origin, dirL, kMioPanRtFar, false) ? 0.0 : 1.0;
    }
    bool skipPlayer = MioPanRtLightIsPlayers(P + toLight);

    // The light's position relative to the new origin.
    float3 target = toLight - n * bias;
    samples = clamp(samples, 1, 16);
    float radius = uRtParams.z;
    if (radius <= 0.0 || (samples <= 1 && !temporal))
    {
        float dist = length(target);
        float tmax = dist - uRtParams.y;
        if (tmax <= 0.0)
        {
            return 1.0;
        }
        return MioPanRtOccluded(origin, target / dist, tmax, skipPlayer) ? 0.0
                                                                         : 1.0;
    }

    // Soft: a Vogel spiral of samples over a disc facing P, rotated per pixel
    // -- and, when temporal, re-rotated and radially jittered every frame.
    float3 axis = abs(dirL.y) < 0.99 ? float3(0.0, 1.0, 0.0)
                                     : float3(1.0, 0.0, 0.0);
    float3 u = normalize(cross(axis, dirL));
    float3 v = cross(dirL, u);
    float2 seed = gMioPanRtPixel + (temporal ? 5.588238 * gMioPanRtFrame : 0.0);
    float rotation = MioPanRtNoise(seed) * 6.28318530;
    float jitter = temporal ? MioPanRtNoise(seed.yx + float2(37.0, 11.0)) : 0.5;
    float lit = 0.0;
    [loop] for (int k = 0; k < samples; k++)
    {
        float r = sqrt(((float)k + jitter) / (float)samples) * radius;
        float theta = (float)k * 2.39996323 + rotation;
        float3 onLight = target + (u * cos(theta) + v * sin(theta)) * r;
        float dist = length(onLight);
        float tmax = dist - uRtParams.y;
        if (tmax <= 0.0 ||
            !MioPanRtOccluded(origin, onLight / dist, tmax, skipPlayer))
        {
            lit += 1.0;
        }
    }
    return lit / (float)samples;
}

float MioPanRtVisibility(float3 P, float3 N, float3 toLight, bool directional)
{
    return MioPanRtVisibilityN(P, N, toLight, directional, uRtFlags.w, false);
}

// Ambient occlusion: the fraction of `samples` rays over the hemisphere around
// N that travel `radius` without meeting anything.  Cosine-weighted -- a Vogel
// spiral on the unit disc lifted onto the hemisphere -- so each ray counts as
// much as the light arriving from its direction would, and rotated per pixel
// and per frame so the mask's average converges.  Mio's triangles count: she
// should darken the floor she stands on.
float MioPanRtAmbientVisibility(float3 P, float3 N, float radius, int samples)
{
    float3 origin = P + N * uRtParams.x;
    float3 axis = abs(N.y) < 0.99 ? float3(0.0, 1.0, 0.0)
                                  : float3(1.0, 0.0, 0.0);
    float3 t = normalize(cross(axis, N));
    float3 b = cross(N, t);
    // Offset from the light rays' seed, so the two patterns are unrelated.
    float2 seed = gMioPanRtPixel + float2(17.0, 59.0) + 5.588238 * gMioPanRtFrame;
    float rotation = MioPanRtNoise(seed) * 6.28318530;
    float jitter = MioPanRtNoise(seed.yx + float2(37.0, 11.0));
    samples = clamp(samples, 1, 16);
    float open = 0.0;
    [loop] for (int k = 0; k < samples; k++)
    {
        float r = sqrt(((float)k + jitter) / (float)samples);
        float theta = (float)k * 2.39996323 + rotation;
        float2 d = r * float2(cos(theta), sin(theta));
        float3 dir = t * d.x + b * d.y + N * sqrt(max(1.0 - dot(d, d), 0.0));
        if (!MioPanRtOccludedSides(origin, dir, radius, false, true))
        {
            open += 1.0;
        }
    }
    return open / (float)samples;
}

#ifndef MIOPAN_RT_COMPUTE
// Called once per pixel by mesh_spot.frag before the kernel runs.  The mask is
// this surface's only if the G-buffer saw this very surface at this pixel: a
// fragment the prepass did not keep -- something blended, something behind
// what ended up in front, a cut-out edge -- finds another position there and
// traces for itself instead.
void MioPanRtLoadMask(float2 pixel, float3 P, float3 N)
{
    gMioPanRtMaskOk = false;
    gMioPanRtAo = 1.0;
    if (uRtMaskFlags.x == 0)
    {
        return;
    }
    int2 base = (int2)pixel;
    float4 g = uRtGPos.Load(int3(base, 0));
    float3 d = g.xyz - P;
    // Both stages interpolate the same expression, so a match is near exact;
    // the slack covers float noise at room scale, about a millimetre.
    if (!(g.w > 0.0 && dot(d, d) < 0.25))
    {
        return;
    }
    gMioPanRtMaskOk = true;

    // A light spatial filter on top of the temporal one: four rotated taps
    // two pixels out, each kept only if it lies on this surface's plane and
    // near it, so the average never reaches across a depth edge or onto
    // another object.  Out-of-bounds loads return zero and fail the w test.
    static const int2 kTaps[4] = {int2(-2, -1), int2(1, -2), int2(2, 1),
                                  int2(-1, 2)};
    float4 sum = uRtMask.Load(int3(base, 0));
    float ao = uRtAoTex.Load(int3(base, 0)).x;
    float weight = 1.0;
    [unroll] for (int k = 0; k < 4; k++)
    {
        int3 q = int3(base + kTaps[k], 0);
        float4 gq = uRtGPos.Load(q);
        float3 dq = gq.xyz - P;
        if (gq.w > 0.0 && abs(dot(dq, N)) < 4.0 && dot(dq, dq) < 3600.0)
        {
            sum += uRtMask.Load(q);
            ao += uRtAoTex.Load(q).x;
            weight += 1.0;
        }
    }
    gMioPanRtMask = sum / weight;

    // Occlusion is a fraction of LIGHT, so it scales linear light; the
    // kernel's units are gamma encoded, where the same scaling is the
    // fraction to the 1/2.2.  uRtAmbient.x carries strength / 2.2, so
    // strength 1 is the physical amount and 2.2 the old multiply-the-colour
    // look.
    if (uRtMaskFlags.y != 0)
    {
        gMioPanRtAo = pow(saturate(ao / weight), uRtAmbient.x);
    }
}
#endif

// The kernel's entry point.  `type` is one MIOPAN_RT_* bit; `contribution` is
// the brightest channel the lane would add, in GS 0..255 units, so a lane too
// faint to see never pays for a ray.
float MioPanRtLaneVisibility(int type, float3 P, float3 N, float3 toLight,
                             float contribution, bool directional)
{
    if ((uRtFlags.x & type) == 0 || contribution < uRtParams.w)
    {
        return 1.0;
    }
#ifndef MIOPAN_RT_COMPUTE
    // One of the mask's lights: the answer is already there, averaged.  Lanes
    // carry the same positions the mask's lights were picked from, so the
    // match is exact.
    if (!directional && gMioPanRtMaskOk)
    {
        float3 lightPos = P + toLight;
        [unroll] for (int c = 0; c < 4; c++)
        {
            float3 d = lightPos - uRtMaskLight[c].xyz;
            if (uRtMaskLight[c].w > 0.0 && dot(d, d) < 1.0)
            {
                float masked = gMioPanRtMask[c];
                gMioPanRtTraced += 1.0;
                gMioPanRtShadowed += 1.0 - masked;
                gMioPanRtFromMask += 1.0;
                return masked;
            }
        }
    }
#endif
    float vis = MioPanRtVisibility(P, N, toLight, directional);
    gMioPanRtTraced += 1.0;
    gMioPanRtShadowed += 1.0 - vis;
    return vis;
}

float MioPanRtMax3(float3 value)
{
    return max(value.x, max(value.y, value.z));
}

#endif // MIOPAN_RT_HLSLI
