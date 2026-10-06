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
    // z = SPOTINTENSB (1 / sin^2), w unused.
    float4 spotParams[MIOPAN_VU1_MAX_LANES];

    float4 pointPos[MIOPAN_VU1_MAX_LANES];
    float4 pointColDif[MIOPAN_VU1_MAX_LANES];
    float4 pointColSpc[MIOPAN_VU1_MAX_LANES];
    // x = POINTBTIMES, yzw unused.
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

    float3 result = img.ambient.xyz;
    result += img.dirColDif[0].rgb * nd.x +
              img.dirColDif[1].rgb * nd.y +
              img.dirColDif[2].rgb * nd.z;
    result += img.dirColSpc[0].rgb * ns.x +
              img.dirColSpc[1].rgb * ns.y +
              img.dirColSpc[2].rgb * ns.z;
    return result;
}

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

        // The coefficient is capped BEFORE the cone multiplies it, and the
        // cone itself is never capped (VU: MINIw at 0x178, then MUL at 0x1b0).
        float c = MioPanVu1Coefficient(N, L, invLen2, img.spotParams[i].x);

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

        result += img.pointColDif[i].rgb * c;

        // Point specular is the coefficient to the FOURTH -- two squarings in
        // the microcode against the spot kernel's three.
        float e = c * c;
        e = e * e;                      // ^4
        result += img.pointColSpc[i].rgb * e;
    }

    return result;
}

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

    if ((terms & MIOPAN_VU1_TERM_DIRECTIONAL) != 0)
    {
        result += MioPanVu1Directional(img, N);
    }
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
