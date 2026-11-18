#define MIKUPAN_CUSTOM_LIGHT_BLOCK
// The same beam mesh_spot.frag uses -- the softened cone, or the physically lit
// one -- so the mask is traced wherever the flashlight reaches.
#define MIOPAN_VU1_SOFT_FLASHLIGHT 1
#include "mikupan_common.hlsli"
#include "miopan_vu1_lighting.hlsli"

// Ray-traced shadow mask, G-buffer prepass.  Writes where the visible
// fragment-lit surface is (SV_Target0, w = 1 for "something here"), which way
// it faces (SV_Target1.xyz), and which of the mask's four lights the forward
// shader would cast a ray for here (SV_Target1.w, one bit per channel).
// rt_shadow_mask.comp traces only those; the forward shader compares its own
// position against SV_Target0 to know whether the mask is its own.
//
// The bits are the forward shader's own decision, made with this draw's own
// light block -- the colours there carry the material's scale, which differs
// several times over between materials -- so the mask traces exactly the rays
// the forward pass would have, once per pixel instead of once per fragment.
//
// Cut-outs are discarded the way mesh_spot.frag discards them, so a fence or a
// paper screen does not hide the surface behind its holes.  What is left that
// the forward pass would still discard (the sharpened alpha edge) only costs
// those pixels a fallback trace -- the comparison catches it.

cbuffer MioPanFragmentLightBlock : register(b1, MIKUPAN_UNIFORM_SPACE)
{
    MioPanVu1LightImage uVu1;
};

struct PSInput
{
    float4 position : SV_Position;
    float4 uvAlpha : TEXCOORD0;
    float4 worldPosition : TEXCOORD1;
    float4 worldNormal : TEXCOORD2;
};

struct PSOutput
{
    float4 position : SV_Target0;
    float4 normal : SV_Target1;
};

// MIOPAN_RT_* type bits, as miopan_rt.hlsli defines them.
#define GBUFFER_RT_SPOT  1
#define GBUFFER_RT_POINT 2

int MaskChannelOf(float3 lightPos)
{
    [unroll] for (int c = 0; c < 4; c++)
    {
        float3 d = lightPos - uRtMaskLight[c].xyz;
        if (uRtMaskLight[c].w > 0.0 && dot(d, d) < 1.0)
        {
            return c;
        }
    }
    return -1;
}

float MaskMax3(float3 v)
{
    return max(v.x, max(v.y, v.z));
}

// The lane contributions MioPanVu1Spot()/MioPanVu1Point() compute right before
// they ask for a ray, and the same threshold.
uint MaskChannels(float3 P, float3 N)
{
    uint bits = 0;
    int terms = uVu1.config.z;
    float threshold = uRtParams.w;

    if ((terms & MIOPAN_VU1_TERM_SPOT) != 0 && (uVu1.config.x & 1) != 0 &&
        (uRtFlags.x & GBUFFER_RT_SPOT) != 0)
    {
        int count = min(uVu1.counts.x, MIOPAN_VU1_MAX_LANES);
        for (int i = 0; i < count; i++)
        {
            int c = MaskChannelOf(uVu1.spotPos[i].xyz);
            if (c < 0)
            {
                continue;
            }
            float3 L = uVu1.spotPos[i].xyz - P;
            float len2 = dot(L, L);
            if (len2 <= 0.0)
            {
                continue;
            }
            float invLen2 = 1.0 / len2;
            if (MioPanVu1FlashPhysical(uVu1.spotParams[i]))
            {
                float3 beam = MioPanVu1FlashBeam(uVu1, i, P, N);
                if (MioPanVu1FlashContribution(beam) >= threshold)
                {
                    bits |= 1u << (uint)c;
                }
                continue;
            }
            float coneDot = max(-dot(uVu1.spotDir[i].xyz, L), 0.0);
            float cone = max(coneDot * coneDot * invLen2 - uVu1.spotParams[i].y,
                             0.0) * uVu1.spotParams[i].z;
            cone = MioPanVu1SoftCone(coneDot, invLen2, uVu1.spotParams[i], cone);
            float k = MioPanVu1Coefficient(N, L, invLen2, uVu1.spotParams[i].x);
            float e8 = k * k;
            e8 = e8 * e8;
            e8 = e8 * e8;
            float contribution = MaskMax3(uVu1.spotColDif[i].rgb * (k * cone) +
                                          uVu1.spotColSpc[i].rgb * (cone * e8));
            if (contribution >= threshold)
            {
                bits |= 1u << (uint)c;
            }
        }
    }

    if ((terms & MIOPAN_VU1_TERM_POINT) != 0 && (uVu1.config.x & 2) != 0 &&
        (uRtFlags.x & GBUFFER_RT_POINT) != 0)
    {
        int count = min(uVu1.counts.y, MIOPAN_VU1_MAX_LANES);
        for (int i = 0; i < count; i++)
        {
            int c = MaskChannelOf(uVu1.pointPos[i].xyz);
            if (c < 0)
            {
                continue;
            }
            float3 L = uVu1.pointPos[i].xyz - P;
            float len2 = dot(L, L);
            if (len2 <= 0.0)
            {
                continue;
            }
            float invLen2 = 1.0 / len2;
            // The ROM's lane-2 transpose bug, as MioPanVu1Point() keeps it.
            float3 Ldot = L;
            if ((i % 3) == 2)
            {
                Ldot.y = uVu1.pointPos[i - 1].y - P.y;
            }
            float k = min(max(dot(N, Ldot), 0.0) * uVu1.pointParams[i].x * invLen2,
                          1.0);
            float e4 = k * k;
            e4 = e4 * e4;
            float contribution = MaskMax3(uVu1.pointColDif[i].rgb * k +
                                          uVu1.pointColSpc[i].rgb * e4);
            if (contribution >= threshold)
            {
                bits |= 1u << (uint)c;
            }
        }
    }
    return bits;
}

PSOutput main(PSInput input)
{
    float4 texel = uTexture.Sample(uTextureSampler, input.uvAlpha.xy);
    if (MikuPanAlphaCutoffFails(texel.a))
    {
        discard;
    }
    float alpha = texel.a * input.uvAlpha.z;
    if (MikuPanAlphaTestFails(alpha) || alpha <= 0.0)
    {
        discard;
    }

    float3 N = normalize(input.worldNormal.xyz);
    PSOutput output;
    output.position = float4(input.worldPosition.xyz, 1.0);
    output.normal = float4(N, (float)MaskChannels(input.worldPosition.xyz, N));
    return output;
}
