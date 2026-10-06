#include "mikupan_common.hlsli"

/*
 * Receiver pass fragment.  Samples the shadow map through the projector and
 * blends a dark fragment over the already-drawn room, which is what the ROM's
 * SRT_MAPSHADOW pass does on VU1.
 *
 * uParams0.y is the projector strength -- gra3dShadow.c's _CalcColor() derives
 * it from the light's diffuse and attenuation at the shadow target, capped at
 * half (alpha = length(rgb)/sqrt(3) * 0.5), and the host is handed that same
 * number by MioPan_RendererShadowSetStrength().
 *
 * uShadowSize is this shadow's tile in the shadow atlas: xy scale, zw offset.
 * Several shadows are in flight per frame -- the player, the sister, and one
 * per object block under the flashlight -- and they share one texture, because
 * unlike the ROM the host does not consume each map before building the next.
 */
struct PSInput
{
    float4 position : SV_Position;
    float4 vShadowClip : TEXCOORD0;
};

float4 main(PSInput input) : SV_Target0
{
    if (input.vShadowClip.w <= 0.0)
    {
        if (uFlags1.z != 0)
        {
            return float4(0.0, 0.0, 0.45, 1.0);
        }
        discard;
    }

    float3 sndc = input.vShadowClip.xyz / input.vShadowClip.w;

    /* Flip y: the shadow map is an offscreen target and is top-down here, the
     * same convention textured_mesh_lighted.frag notes. */
    float2 suv = float2(sndc.x * 0.5 + 0.5, 0.5 - sndc.y * 0.5);

    /* Reversed-Z, so the light's clip depth runs 1 at the near plane to 0 at
     * the far one -- not the [-1,1] the MikuPan original tested.  See
     * MioPan_Graph3dApplyCamera(). */
    if (suv.x < 0.0 || suv.x > 1.0 ||
        suv.y < 0.0 || suv.y > 1.0 ||
        sndc.z < 0.0 || sndc.z > 1.0)
    {
        if (uFlags1.z != 0)
        {
            return float4(0.0, 0.0, 0.45, 1.0);
        }
        discard;
    }

    /* Into this shadow's atlas tile.  Clamped a half-texel inside it so a
     * linear tap at the edge cannot reach the neighbouring shadow -- the
     * caster pass already leaves a two-pixel gutter, this keeps the sample
     * inside it. */
    float2 tile_uv = clamp(suv, 0.0.xx, 1.0.xx) * uShadowSize.xy +
                     uShadowSize.zw;

    /* The map holds COVERAGE, not depth, so softening it is a plain average --
     * there is no depth comparison to do per tap and no acne to bias against.
     * uShadowFilter.y is one atlas texel; the 3x3 stays inside the caster
     * pass's two-pixel gutter, so widening the kernel cannot reach the
     * neighbouring tile.
     *
     * PORT DEVIATION, and an opt-in one: the ROM projected a hard-edged
     * sprite.  MIOPAN_SHADOW_FILTER_NONE is the faithful single tap. */
    float occluded;
    if (uShadowFilter.x > 0.5 && uShadowFilter.y > 0.0)
    {
        float step = uShadowFilter.y;
        float sum = 0.0;
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            [unroll]
            for (int x = -1; x <= 1; x++)
            {
                float2 tap = tile_uv + float2((float)x, (float)y) * step;
                sum += uAuxTexture.Sample(uAuxTextureSampler, tap).r;
            }
        }
        occluded = sum * (1.0 / 9.0);
    }
    else
    {
        occluded = uAuxTexture.Sample(uAuxTextureSampler, tile_uv).r;
    }
    if (uFlags1.z != 0)
    {
        return float4(occluded, 0.35 + 0.65 * (1.0 - occluded), 0.0, 1.0);
    }

    float alpha = occluded * uParams0.y;
    if (alpha <= 0.001)
    {
        discard;
    }

    return float4(0.0, 0.0, 0.0, alpha);
}
