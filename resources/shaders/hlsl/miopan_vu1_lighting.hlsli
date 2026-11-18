#ifndef MIOPAN_VU1_LIGHTING_HLSLI
#define MIOPAN_VU1_LIGHTING_HLSLI

// The VU1's three realtime lighting kernels, transcribed from the microcode in
// vu1/ff2_00.vsm.  Read vu1/LIGHTING.md before touching any constant here; the
// short version is that this is NOT the model g3dCalcVertexColor() (the
// prelight bake) implements, and the two are not meant to agree.
//
//   directional  PLOOP_TYPE2 0x6c8-0x7d8, and CalcParallel in the P programs
//                (RotTransPersInner carries the same body for the preset
//                draw types, so a preset draw runs these kernels too)
//   spot         CalcIntens  0x018-0x208
//   point        CalcPoint   0x220-0x3e0
//
// ONE body, shared by every stage that evaluates it, so the vertex and
// fragment paths cannot drift apart.  The C++ mirror -- for the exceptional
// CPU fallbacks -- is gra3dCalcVu1VertexColor() in gra3d.c and
// EvaluateAnimatedVertexLighting() in miopan_renderer.cpp; all three implement
// the same arithmetic and any change here belongs in those as well.
//
// Everything is in GS 0..255 units.  gra3dCalcVu1MaterialData*() has already
// folded the 128/192/255/43/86 scales -- and monotone mode -- into the colours
// this block carries, so there is nothing to scale or grey here; the one
// closing min() against ambient.w (255) is the microcode's only clamp.

// LANE COUNT.  The VU1 had three lanes per positional type, so the engine
// ranks its bank by power at the bounding box and throws the rest away
// (_SelectLightByType, gra3dSGD.c).  Because that runs per bounding box, the
// surviving three change as objects move and lights visibly pop.  The host has
// no such register file, so it carries the room's whole selected set instead
// and the popping goes away.
//
// gra3d's bank holds 19 point and 17 spot, but MapLightSelect() has already
// thinned the room to MAP_LIGHT_SELECT_MAX (14) by power at the listener, so 16
// covers every case with headroom.  Keep in step with MIOPAN_VU1_MAX_LANES in
// miopan_renderer.h -- the C++ struct and this one must agree exactly.
#define MIOPAN_VU1_MAX_LANES 16

// Which terms this draw wants evaluated, in config.z.  The CPU supplies
// whatever is NOT set, already folded into the seed colour, so the two halves
// compose to exactly one evaluation of the whole light image.  Mirrors
// MIOPAN_VU1_TERM_* in miopan_renderer.h.
#define MIOPAN_VU1_TERM_DIRECTIONAL 1   // the ambient term and the three directional lanes
#define MIOPAN_VU1_TERM_SPOT        2
#define MIOPAN_VU1_TERM_POINT       4

// Ray-traced shadows (port addition, proof of concept).  Defined only by
// mesh_spot_rt.frag; every other stage compiles exactly the kernel below with
// the #ifdef blocks absent.  Each block scales one lane by the fraction of
// that light the point can see -- see miopan_rt.hlsli.
#ifdef MIOPAN_VU1_SHADOW_RAYS
#include "miopan_rt.hlsli"
// World position of the point being lit, for the directional lanes, whose
// kernel signature has none.  Set by MioPanVu1Evaluate().
static float3 gMioPanRtPosition = float3(0.0, 0.0, 0.0);
#endif

// Field-for-field mirror of MioPanLightState (miopan_renderer.h).  Every member
// is a float4/int4, so the struct packs identically in a cbuffer and in C.
// Derived from GRA3DVU1LIGHTSNAPSHOT (gra3dTypes.h), which is where the VU
// memory addresses and the derivation of each term are documented -- but WIDER
// than it in the two positional arrays, per the lane-count note above.
struct MioPanVu1LightImage
{
    // x = light-type enables, bit 0 spot / bit 1 point.  These are the
    //     microcode's own: a disabled TYPE means the kernel never ran and
    //     VU1 memory kept whatever the previous draw left in it.
    // y = flags, bit 0 "lighting enabled".
    // z = MIOPAN_VU1_TERM_* mask the FRAGMENT stage is responsible for
    //     (renderer-owned; the engine snapshot leaves it zero).  The vertex
    //     stage ignores it and always evaluates the whole image.
    int4 config;
    // x = live spot lanes, y = live point lanes.  Both <= MIOPAN_VU1_MAX_LANES.
    // The loops below run to these, not to the array bound, so a room with two
    // spots costs two lanes rather than sixteen.
    int4 counts;
    // GLOBALAMBIENT (VU 46): xyz ambient term, w = 255.
    float4 ambient;

    // Directional is not widened: the engine's own bank has exactly three
    // (GRA3D_NUM_LIGHT_DIRECTIONAL) and SelectLight() does not rank them.
    float4 dirLightDif[3];  // VU 25..27
    float4 dirLightSpc[3];  // VU 28..30, per-frame half-vector
    float4 dirColDif[3];    // VU 47..49
    float4 dirColSpc[3];    // VU 50..52

    float4 spotPos[MIOPAN_VU1_MAX_LANES];
    float4 spotDir[MIOPAN_VU1_MAX_LANES];
    float4 spotColDif[MIOPAN_VU1_MAX_LANES];
    float4 spotColSpc[MIOPAN_VU1_MAX_LANES];
    // x = SPOTBTIMES (fMaxRange * SetMaxColor255's divisor),
    // y = SPOTINTENS (cos^2 of the cone half-angle),
    // z = SPOTINTENSB (1 / sin^2),
    // w = 0, or for Mio's flashlight (port tag; see MioPanVu1SoftCone(), and
    // her body never shadows it) its bTimes against a white material, at
    // least 1 -- what MioPanVu1FlashBeam() measures the beam's reach by.
    float4 spotParams[MIOPAN_VU1_MAX_LANES];

    float4 pointPos[MIOPAN_VU1_MAX_LANES];
    float4 pointColDif[MIOPAN_VU1_MAX_LANES];
    float4 pointColSpc[MIOPAN_VU1_MAX_LANES];
    // x = POINTBTIMES, yz unused, w = 1 for one of Mio's own lights (port tag:
    // her body never shadows it).
    float4 pointParams[MIOPAN_VU1_MAX_LANES];
};

float3 MioPanVu1SafeNormalize(float3 value)
{
    float length2 = dot(value, value);
    return length2 > 0.0 ? value * rsqrt(length2) : 0.0.xxx;
}

// CalcIntens / CalcPoint both keep the light vector UNNORMALISED and divide by
// its squared length once, so the coefficient below is bTimes/|L| -- an
// inverse-distance law with no cutoff and no min-range band.  bTimes is
// fMaxRange * fDiv, where fDiv is SetMaxColor255()'s divisor and the light
// colour has been divided by the same fDiv; the two cancel in the product,
// which is the whole point.  What survives is the saturation POINT: min(...,1)
// fires exactly when the light's brightest channel would reach 255, not when
// the coefficient reaches 1.
//
//   VU: MAXx.xyz (N.L >= 0), MUL by bTimes, MUL by 1/|L|^2, MINIw against 1.0
float MioPanVu1Coefficient(float3 normal, float3 toLight, float invLen2,
                           float bTimes)
{
    return min(max(dot(normal, toLight), 0.0) * bTimes * invLen2, 1.0);
}

// The ambient term plus the three directional lanes.
//
// dirLightSpc is a real half-vector, but built once per frame from the camera
// FORWARD axis (normalize(lightDir + -camera.matCoord[2])), not per vertex or
// per pixel from the eye position.  Do not "fix" that: recomputing it from the
// true eye vector would be right for a general renderer and wrong for this
// game.  Fixed eighth power -- the microcode squares the dot three times over
// and gra3dSetMaterial() pins fPower to 1.0, so there is no material exponent
// to read.
float3 MioPanVu1Directional(MioPanVu1LightImage img, float3 N)
{
    float3 nd, ns;
    nd.x = dot(img.dirLightDif[0].xyz, N);
    nd.y = dot(img.dirLightDif[1].xyz, N);
    nd.z = dot(img.dirLightDif[2].xyz, N);
    ns.x = dot(img.dirLightSpc[0].xyz, N);
    ns.y = dot(img.dirLightSpc[1].xyz, N);
    ns.z = dot(img.dirLightSpc[2].xyz, N);
    nd = max(nd, 0.0.xxx);
    ns = max(ns, 0.0.xxx);
    ns = ns * ns;
    ns = ns * ns;
    ns = ns * ns;                       // ^8

#ifdef MIOPAN_VU1_SHADOW_RAYS
    // dirLightDif points towards the light (the dot above is N.L).
    [unroll] for (int k = 0; k < 3; k++)
    {
        float contribution = MioPanRtMax3(img.dirColDif[k].rgb * nd[k] +
                                          img.dirColSpc[k].rgb * ns[k]);
        float vis = MioPanRtLaneVisibility(MIOPAN_RT_DIRECTIONAL,
                                           gMioPanRtPosition, N,
                                           img.dirLightDif[k].xyz,
                                           contribution, true, false);
        nd[k] *= vis;
        ns[k] *= vis;
    }
#endif

    float3 result = img.ambient.xyz;
    result += img.dirColDif[0].rgb * nd.x +
              img.dirColDif[1].rgb * nd.y +
              img.dirColDif[2].rgb * nd.z;
    result += img.dirColSpc[0].rgb * ns.x +
              img.dirColSpc[1].rgb * ns.y +
              img.dirColSpc[2].rgb * ns.z;
    return result;
}

#ifdef MIOPAN_VU1_SOFT_FLASHLIGHT
// PORT ADDITION -- Mio's flashlight with a soft beam.  Per-pixel stages only
// (mesh_spot.frag, mesh_spot_rt.frag and the shadow mask's prepass, which must
// agree with them); the vertex path keeps the PS2 cone.
//
// The microcode's cone is max(cos^2 - cos^2(a), 0) / sin^2(a), which with
// x = sin(theta) / sin(a) -- 0 on the beam's axis, 1 on its rim -- is just
// 1 - x^2: a ramp that hits zero at the rim with a crease.  The coefficient in
// front of it saturates over most of a lit wall, so on screen that reads as a
// flat bright disc with a sharp edge, which is not how a torch looks.
//
// Two dials (uFlashlight):
//   softness  blends that ramp into a smooth bell, 1 - smoothstep(0, R, x),
//             whose reach R runs from 1 (the rim) to 1.5 -- flat on the axis,
//             no crease, fading out past where the PS2 beam stopped.  At 0.5
//             the bell carries about the PS2 beam's total light.
//   spill     adds a faint wide halo, the light a real reflector throws
//             outside its hotspot, out to `reach` half-angles.
// Both 0 is exactly the PS2 cone.  Nothing behind the light's plane is lit.
float MioPanVu1SoftCone(float coneDot, float invLen2, float4 params, float cone)
{
    if (params.w < 0.5 || uFlashlight.w < 0.5 || coneDot <= 0.0)
    {
        return cone;
    }
    float cos2 = min(coneDot * coneDot * invLen2, 1.0);
    float x = sqrt(max(1.0 - cos2, 0.0) * params.z);
    float softness = saturate(uFlashlight.x);
    float bell = 1.0 - smoothstep(0.0, 1.0 + 0.5 * softness, x);
    float spill = uFlashlight.y *
                  (1.0 - smoothstep(0.0, max(uFlashlight.z, 1.0), x));
    return lerp(cone, bell, saturate(softness * 2.0)) + spill;
}

// PORT ADDITION -- Mio's flashlight lit physically (uFlashBeam.x), in place of
// the cone above.  Per-pixel stages only, like it.
//
// The PS2's beam is a flat disc.  Its coefficient, bTimes/|L| capped at 1,
// stays saturated out to bTimes -- 3420 units, about 7 m, for a white wall
// indoors -- so inside the cone a surface is at full brightness whatever its
// distance or the way it faces, and only the cone's rim shapes the light.
// Here the beam is a real lamp's, worked in LINEAR light:
//
//   E = brightness * profile(angle off the axis) * N.L * ref^2 / (d^2 + lens^2)
//
// Inverse square from a lamp the size of a torch's lens, Lambert's cosine,
// and a reflector's profile (MioPanVu1FlashProfile()): a hotspot, a dimmer
// ring ending about where the PS2 cone did, and a faint wide spill out to some
// 2.5 cone half-angles.  `ref`, where a white surface square to the beam gets
// `brightness`, is a fixed share of the lane's white-material bTimes
// (spotParams.w), so the shorter outdoor beam stays shorter.  A material's
// colours are read against white at the same distance and decoded, so a dark
// plank stays dark.  The highlight is Blinn-Phong off the material's specular
// colour, seen from the camera.
//
// None of it goes into the kernel's GS sum: gMioPanFlashLinear carries it,
// shadow included, to mesh_spot_frag.hlsli, which adds it to the rest in
// linear light as it does the bounce.  MioPanVu1FlashProfile() has a C++
// mirror, FlashProfile() in miopan_renderer.cpp, for the bounce; keep them in
// step.
static const float kMioPanFlashRef = 0.85;      // `ref`, in white bTimes
static const float kMioPanFlashLens2 = 100.0;   // lens^2: about 2 cm
static const float kMioPanFlashGloss = 24.0;    // the highlight's exponent

static float3 gMioPanFlashLinear = float3(0.0, 0.0, 0.0);

bool MioPanVu1FlashPhysical(float4 params)
{
    return params.w > 0.5 && uFlashBeam.x > 0.5;
}

// The beam's light at `cosT` off its axis, 1 at the centre.  `cos2Half` is
// SPOTINTENS, cos^2 of the PS2 cone's half-angle.  A torch's reflector throws
// a hotspot, flat in the middle and gone by three quarters of the way out; a
// dimmer ring round it, whose soft outer edge sits where the PS2 cone's rim
// was (both scale with uFlashBeam.z); and a faint spill well past that.
// Nothing behind the lamp.
float MioPanVu1FlashProfile(float cosT, float cos2Half)
{
    float halfAngle = max(acos(sqrt(saturate(cos2Half))), 1e-3);
    float t = acos(clamp(cosT, -1.0, 1.0)) / halfAngle;
    float u = t / max(uFlashBeam.z, 0.05);
    float hot = 1.0 - smoothstep(0.2, 0.75, u);
    float ring = 1.0 - smoothstep(0.85, 1.25, u);
    float spill = uFlashBeam.w * exp(-t * t * (0.5 / (1.4 * 1.4)));
    return (0.88 * hot + 0.12 * ring + spill) * saturate(cosT * 10.0);
}

// Lane `i`'s light at P, LINEAR, before any shadow.
float3 MioPanVu1FlashBeam(MioPanVu1LightImage img, int i, float3 P, float3 N)
{
    float3 L = img.spotPos[i].xyz - P;
    float d2 = dot(L, L);
    if (!(d2 > 0.0))
    {
        return 0.0.xxx;
    }
    float3 Ln = L * rsqrt(d2);
    float ndl = dot(N, Ln);
    if (ndl <= 0.0)
    {
        return 0.0.xxx;
    }
    // spotDir is the beam (see MioPanVu1Spot()), so the angle off it is
    // measured from the lamp out to P.
    float profile = MioPanVu1FlashProfile(-dot(img.spotDir[i].xyz, Ln),
                                          img.spotParams[i].y);
    if (profile <= 0.0)
    {
        return 0.0.xxx;
    }
    float white = img.spotParams[i].w;
    float ref = kMioPanFlashRef * white;
    float E = uFlashBeam.y * profile * ndl * (ref * ref) /
              (d2 + kMioPanFlashLens2);
    // colour * bTimes is the same at any material scale; over white's it is
    // this material's share of the light, in the GS's gamma units.
    float scale = img.spotParams[i].x / (255.0 * white);
    float3 result = pow(max(img.spotColDif[i].rgb * scale, 0.0.xxx), 2.2);
    if (uFlashEye.w > 0.5)
    {
        float3 V = MioPanVu1SafeNormalize(uFlashEye.xyz - P);
        float3 H = MioPanVu1SafeNormalize(Ln + V);
        result += pow(max(img.spotColSpc[i].rgb * scale, 0.0.xxx), 2.2) *
                  pow(saturate(dot(N, H)), kMioPanFlashGloss);
    }
    return result * E;
}

// What the shadow rays' threshold compares: the beam's light as GS units.
float MioPanVu1FlashContribution(float3 linearLight)
{
    float peak = max(linearLight.x, max(linearLight.y, linearLight.z));
    return peak > 0.0 ? 128.0 * pow(peak, 1.0 / 2.2) : 0.0;
}
#endif

// spot: CalcIntens 0x018-0x208, once per live lane.
//
// On the VU an unused lane arrived with a cleared position and zero
// colours/bTimes and so contributed nothing without a per-light mask.  Here the
// lane count does that job instead, which is what keeps a two-light room from
// paying for sixteen.
float3 MioPanVu1Spot(MioPanVu1LightImage img, float3 P, float3 N)
{
    float3 result = 0.0.xxx;
    int count = min(img.counts.x, MIOPAN_VU1_MAX_LANES);

    for (int i = 0; i < count; i++)
    {
        // L stays UNNORMALISED; the kernel divides by its squared length once,
        // which is what makes the attenuation bTimes/|L| with no cutoff.
        float3 L = img.spotPos[i].xyz - P;
        float len2 = dot(L, L);
        if (len2 <= 0.0)
        {
            continue;
        }
        float invLen2 = 1.0 / len2;

#ifdef MIOPAN_VU1_SOFT_FLASHLIGHT
        // Mio's flashlight lit physically: none of the cone below, and its
        // light goes out LINEAR instead of into the sum.
        if (MioPanVu1FlashPhysical(img.spotParams[i]))
        {
            float3 beam = MioPanVu1FlashBeam(img, i, P, N);
#ifdef MIOPAN_VU1_SHADOW_RAYS
            beam *= MioPanRtLaneVisibility(MIOPAN_RT_SPOT, P, N, L,
                                           MioPanVu1FlashContribution(beam),
                                           false, true);
#endif
            gMioPanFlashLinear += beam;
            continue;
        }
#endif

        // PORT DEVIATION, and the one place the spot convention is reconciled.
        // The microcode dots vDirection against `light - vertex`, so on
        // hardware the cone opens along -vDirection.  The port keeps
        // vDirection as the BEAM everywhere -- the authored room spots, the
        // prelight's cone ramp, _IsBBLightingupSpot()'s bounding-box gate and
        // every install site read it that way -- so the negation lives here
        // instead of at the install sites.  Converting only some of those
        // sites made meshes flip between lit and black as the camera moved.
        // vu1/LIGHTING.md section 3.3.
        float coneDot = max(-dot(img.spotDir[i].xyz, L), 0.0);
        float cone = max(coneDot * coneDot * invLen2 - img.spotParams[i].y,
                         0.0) * img.spotParams[i].z;
#ifdef MIOPAN_VU1_SOFT_FLASHLIGHT
        cone = MioPanVu1SoftCone(coneDot, invLen2, img.spotParams[i], cone);
#endif

        // The coefficient is capped BEFORE the cone multiplies it, and the
        // cone itself is never capped (VU: MINIw at 0x178, then MUL at 0x1b0).
        float c = MioPanVu1Coefficient(N, L, invLen2, img.spotParams[i].x);

#ifdef MIOPAN_VU1_SHADOW_RAYS
        // Both spot terms carry `cone`, so scaling it shadows the lane whole.
        {
            float e8 = c * c;
            e8 = e8 * e8;
            e8 = e8 * e8;
            float contribution = MioPanRtMax3(img.spotColDif[i].rgb * (c * cone) +
                                              img.spotColSpc[i].rgb * (cone * e8));
            cone *= MioPanRtLaneVisibility(MIOPAN_RT_SPOT, P, N, L,
                                           contribution, false,
                                           img.spotParams[i].w > 0.5);
        }
#endif

        result += img.spotColDif[i].rgb * (c * cone);

        // Spot specular is the clamped diffuse coefficient to the eighth,
        // cone-modulated -- there is no half-vector here.  SPOTLIGHTSPC
        // (VU 39..41) is uploaded and never read.
        float e = c * c;
        e = e * e;
        e = e * e;                      // ^8
        result += (img.spotColSpc[i].rgb * cone) * e;
    }

    return result;
}

// point: CalcPoint 0x220-0x3e0, once per live lane.
float3 MioPanVu1Point(MioPanVu1LightImage img, float3 P, float3 N)
{
    float3 result = 0.0.xxx;
    int count = min(img.counts.y, MIOPAN_VU1_MAX_LANES);

    for (int i = 0; i < count; i++)
    {
        float3 L = img.pointPos[i].xyz - P;
        float len2 = dot(L, L);
        if (len2 <= 0.0)
        {
            continue;
        }
        float invLen2 = 1.0 / len2;

        // ROM BUG, reproduced.  CalcPoint omits the `MR32.z vf15, vf14` that
        // CalcIntens has at 0x0e0, so its transpose leaves vf15.z holding
        // L1.z where it needs L2.y: light 2's diffuse dot is
        // L2.x*Nx + L1.z*Ny + L2.z*Nz.  See vu1/LIGHTING.md section 3.2.
        //
        // It is a property of the VU's THREE-LANE transpose, so it belongs to
        // lane 2 of each group of three and not to "the third light in the
        // room" -- with the lanes widened past three the pattern repeats every
        // group, which is what the hardware would have done had it been given
        // more lights to place.  Lanes 0 and 1 of every group are unaffected.
        float3 Ldot = L;
        if ((i % 3) == 2)
        {
            Ldot.y = img.pointPos[i - 1].y - P.y;
        }

        float c = min(max(dot(N, Ldot), 0.0) * img.pointParams[i].x * invLen2,
                      1.0);

#ifdef MIOPAN_VU1_SHADOW_RAYS
        // The ray goes to where the light actually is -- L, not the ROM-bug
        // Ldot, which only ever fed the dot product.
        {
            float e4 = c * c;
            e4 = e4 * e4;
            float3 lane = img.pointColDif[i].rgb * c + img.pointColSpc[i].rgb * e4;
            result += lane * MioPanRtLaneVisibility(MIOPAN_RT_POINT, P, N, L,
                                                    MioPanRtMax3(lane), false,
                                                    img.pointParams[i].w > 0.5);
            continue;
        }
#endif

        result += img.pointColDif[i].rgb * c;

        // Point specular is the coefficient to the FOURTH -- two squarings in
        // the microcode against the spot kernel's three.
        float e = c * c;
        e = e * e;                      // ^4
        result += img.pointColSpc[i].rgb * e;
    }

    return result;
}

#ifdef MIOPAN_VU1_SHADOW_RAYS
// Baked shadows (port addition).  The rooms' meshes get their lamps' light --
// every point and spot light but Mio's own -- from the prelight bake, done on
// the CPU when a room loads, not from a live lane: their light blocks carry no
// point lanes at all, and no spot but her flashlight.  So a lane's shadow
// never reaches them.  The mask pass estimates, per pixel, how much
// light the bake's lamps gave it (gMioPanRtBakeTotal, GS 0..255, by the bake's
// own law for a white material) and the fraction of that still getting
// through (gMioPanRtBakeVis); see rt_shadow_mask.comp.
//
// Where the bake holds at least that much, the blocked share comes straight
// off.  Where it holds less -- a darker material than the estimate's white,
// or the bake's own cap -- the bake is taken to be those lamps alone and
// scaled by the fraction still lit, so it never goes below zero.  A
// character's seed is black (it has no bake), so this leaves it alone.
float3 MioPanRtShadowBake(float3 seed)
{
    if (!gMioPanRtBakeOk || uRtMaskFlags.z == 0 || MioPanRtMax3(seed) <= 0.0 ||
        gMioPanRtBakeTotal < uRtParams.w)
    {
        return seed;
    }
    float total = gMioPanRtBakeTotal;
    float vis = saturate(gMioPanRtBakeVis);
    gMioPanRtTraced += 1.0;
    gMioPanRtShadowed += 1.0 - vis;
    gMioPanRtFromMask += 1.0;
    float3 removed = seed - total * (1.0 - vis);
    float3 scaled = seed * vis;
    return max(lerp(scaled, removed, step(total.xxx, seed)), 0.0.xxx);
}
#endif

// The whole light image, in GS 0..255 units, seeded from `seed255`.
//
// The seed is the accumulator the microcode starts from: a preset mesh folds
// its baked prelight colour in there (which is what CalcParallel does on the P
// programs -- it adds to the vf18 the packet carried), while a runtime mesh
// starts from black because its VUVN packet has no colour of its own.  When
// only some terms are selected, the seed also carries whatever the CPU already
// evaluated, so the two halves compose to one pass over the image.
//
// `P` and `N` are WORLD space.  LWLOOP2 pre-transforms every vertex position
// into world space before the vertex loop, and CalcIntens/CalcPoint rotate the
// normal with LWMATRIX, so both spot and point run in world space against
// world-space light positions.
//
// `terms` is passed rather than read from config.z so each stage states what
// it is responsible for at the call site: the vertex path always evaluates the
// whole image, and only the fragment path takes the renderer's split.
float3 MioPanVu1Evaluate(MioPanVu1LightImage img, float3 P, float3 N,
                         float3 seed255, int terms)
{
    float3 result = seed255;

#ifdef MIOPAN_VU1_SHADOW_RAYS
    gMioPanRtPosition = P;
    // The seed is the prelight bake; its lamps' shadows come out of it here.
    result = MioPanRtShadowBake(result);
#endif

    if ((terms & MIOPAN_VU1_TERM_DIRECTIONAL) != 0)
    {
        result += MioPanVu1Directional(img, N);
    }
#ifdef MIOPAN_VU1_SHADOW_RAYS
    // Ambient occlusion (port addition): everything so far -- the prelight
    // bake, the ambient term and the three fill lights -- is light from all
    // round, so it is what a crease or a corner hides.  The spot and point
    // lanes below keep their own ray-traced shadows instead.
    result *= gMioPanRtAo;
#endif
    if ((terms & MIOPAN_VU1_TERM_SPOT) != 0 && (img.config.x & 1) != 0)
    {
        result += MioPanVu1Spot(img, P, N);
    }
    if ((terms & MIOPAN_VU1_TERM_POINT) != 0 && (img.config.x & 2) != 0)
    {
        result += MioPanVu1Point(img, P, N);
    }

    // MINIw.xyz vf18, vf18, vf19w -- one clamp at the end, against
    // GLOBALAMBIENT.w (255).  Nothing caps the individual lights.
    return clamp(result, 0.0.xxx, img.ambient.www);
}

// The GS MODULATE register treats 128 as unity, so a 0..255 colour is a
// 0..1.99 multiplier -- the PS2's real overbright headroom.  Cf. Ps2ColorFloat()
// in miopan_graph3d.cpp, which is the same conversion on the CPU side.
static const float kMioPanGsModulateUnity = 1.0 / 128.0;

float3 MioPanVu1ToModulate(float3 gs255)
{
    return max(gs255, 0.0.xxx) * kMioPanGsModulateUnity;
}

float3 MioPanVu1FromModulate(float3 modulate)
{
    return modulate * 128.0;
}

#endif // MIOPAN_VU1_LIGHTING_HLSLI
