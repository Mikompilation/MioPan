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
 *
 * Receiver culling, from the microcode (uShadowPlane / uShadowLight /
 * uShadowEye, set by MioPan_RendererShadowSetProjector()).  SgSuShadow_dma_main
 * does not project onto every receiver triangle: it drops one that faces away
 * from the camera (the pipeline's back-face cull), one whose camera-facing side
 * faces away from the light (face normal against matLIP's w column, -light
 * direction), and one whose three vertices all have a projector z below 0 --
 * past the caster's light-most box corner, between the caster and the light.
 * Without that, the light camera's whole depth range took the silhouette and a
 * ceiling over Mio showed a second copy of her floor shadow.  The facing test
 * is per triangle here too (the face normal from the world position's screen
 * derivatives, oriented by the camera); the depth test is per fragment, where
 * the ROM keeps a whole triangle that has one vertex at z >= 0 -- only the
 * part of such a triangle between the caster and the light differs, and a
 * real shadow never falls there.
 */
struct PSInput
{
    float4 position : SV_Position;
    float4 vShadowClip : TEXCOORD0;
    float4 vWorld : TEXCOORD1;
};

float4 main(PSInput input) : SV_Target0
{
    /* Before any discard: derivatives need the whole quad. */
    float3 world = input.vWorld.xyz;
    float3 face = cross(ddx(world), ddy(world));

    if (uShadowLight.w != 0.0)
    {
        /* The projector z, VU1 vf20.z: s_matIP's z row on the world point. */
        float pz = dot(uShadowPlane.xyz, world) + uShadowPlane.w;
        /* The camera and the light on opposite sides of the face: the side
         * the camera sees faces away from the light.  Either orientation of
         * `face` gives the same sign. */
        float facing = dot(face, uShadowLight.xyz) * dot(face, uShadowEye.xyz - world);
        if (pz < 0.0 || facing < 0.0)
        {
            if (uFlags1.z != 0)
            {
                return float4(0.3, 0.0, 0.45, 1.0);
            }
            discard;
        }
    }

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
