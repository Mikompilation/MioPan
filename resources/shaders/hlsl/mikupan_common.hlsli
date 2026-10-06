#ifndef MIKUPAN_HLSL_COMMON
#define MIKUPAN_HLSL_COMMON

#ifdef MIKUPAN_VERTEX_STAGE
#define MIKUPAN_UNIFORM_SPACE space1
#else
#define MIKUPAN_UNIFORM_SPACE space3
#endif

cbuffer MikuPanUniforms : register(b0, MIKUPAN_UNIFORM_SPACE)
{
    column_major float4x4 model;
    column_major float4x4 view;
    column_major float4x4 projection;
    column_major float4x4 mvp;
    column_major float4x4 modelView;
    column_major float4x4 viewProj;
    column_major float4x4 uShadowMatrix;
    column_major float4x4 uWorldClipView;

    column_major float3x3 normalMatrix;
    column_major float3x3 viewNormalMatrix;

    float4 uColor;
    float4 uFog;
    float4 uFogColor;
    float4 uShadowSize;
    float4 uTextureSize;
    float4 uOutputSize;
    float4 uPhotoNegativeContentRect;
    float4 uPhotoNegativeRect;
    float4 uFramebufferUvOffset;
    float4 uFramebufferUvScale;
    float4 uFramebufferContentUvMax;
    float4 uRenderSize;

    float4 uParams0; // x=normal length, y=shadow strength, z=brightness, w=gamma
    float4 uCrt0;    // x=strength, y=curvature, z=overscan, w=scanline strength
    float4 uCrt1;    // x=scanline scale, y=thickness, z=mask strength, w=mask scale
    float4 uCrt2;    // x=vignette strength, y=size, z=chroma offset, w=blend strength
    float4 uCrt3;    // x=blend radius, y=noise, z=flicker, w=glow
    float4 uParams1; // x=time, y=photo negative strength, z=contrast, w=shadow depth
    float4 uPs2Feedback; // x=strength, y=burn, z=saturation, w=ghost
    float4 uScreenNegative; // rgb=Himuro negative colour, a=strength

    int4 uFlags0; // x=renderNormals, y=disableLighting, z=staticLighting, w=meshLightingMode
    int4 uFlags1; // x=mirrorSurfacePass, y=shadowEnabled, z=shadowDebugView, w=crtEnabled
    int4 uFlags2; // x=blackWhiteMode, y=photoNegativeEnabled, z=photoNegativeSourceEnabled, w=screenCopyMode
    int4 uPadFlags; // x=ps2FeedbackEnabled, y=ps2FeedbackPreviousEnabled, z=ditherSoftMode, w=hdrEnabled

    float4 uHdrOutput; // x=paper white in output colorspace, y=usable headroom

    // GS TEST register, alpha-test half only.  x=ATE, y=AREF already divided by
    // the PS2's 128 so it compares against this shader's 0..1 alpha, z=ATST
    // (0 NEVER, 1 ALWAYS, 2 LESS, 3 LEQUAL, 4 EQUAL, 5 GEQUAL, 6 GREATER,
    // 7 NOTEQUAL), w spare.  Appended so bytecode built before it still reads
    // every member above at the right offset.
    float4 uAlphaTest;

    // Projected-shadow sampling.  x = filter mode (0 single tap, 1 soft),
    // y = one shadow-atlas texel in the same UV space uShadowSize works in, so
    // the receiver never has to know the tile size, zw spare.  Appended for the
    // same reason as uAlphaTest.
    float4 uShadowFilter;

    // The viewfinder surround (finder_mask.frag).  xy = the aperture's centre
    // and zw its half size, both in the ROM's own 640x448 frame units -- the
    // space plyr_wrk.fp and photo_frame_tbl are written in.  Appended for the
    // same reason as uAlphaTest.
    float4 uFinderMask;
    // x = feather width in those same frame units, y = how far the surround is
    // darkened (0 none, 1 black), zw = the defocus radius as a fraction of the
    // render target's width and height.  Appended likewise.
    float4 uFinderMask2;

    // Cut-out edges.  x = sharpening strength (0 off, 1 full),
    // y = hard cutoff threshold (0 off), zw spare.
    // A port deviation, not a GS register: the ROM's own alpha test is
    // "discard fully transparent" everywhere (measured -- the highest AREF in
    // the whole game is 0x01), which was exactly enough at 640x448 and is not
    // once the same texel step is magnified across several output pixels.
    // Appended for the same reason as uAlphaTest.
    float4 uAlphaSharpen;

    // Clip-space depth convention of the vertices a sprite-path draw supplies.
    // x = 0: the engine's symmetric z (near -w, far +w), which
    // MikuPanFixClipZ() converts.  x = 1: already the host's reversed z,
    // passed through untouched -- the world-space billboard bridges rebuild
    // their depth that way so it is not lost to the conversion (see
    // MikuPanFixClipZ()).  yzw spare.  Appended for the same reason as
    // uAlphaTest.
    float4 uClipZ;
};

// GS alpha test.  Returns true when the fragment should be thrown away, which
// is what AFAIL=KEEP asks for; the renderer only ever enables the test for that
// mode, so the other three AFAIL values arrive here as ATE=0.
bool MikuPanAlphaTestFails(float alpha)
{
    if (uAlphaTest.x == 0.0)
    {
        return false;
    }

    int  func = (int)uAlphaTest.z;
    float ref = uAlphaTest.y;

    if (func == 0) { return true;                     } // NEVER
    if (func == 1) { return false;                    } // ALWAYS
    if (func == 2) { return !(alpha <  ref);          } // LESS
    if (func == 3) { return !(alpha <= ref);          } // LEQUAL
    if (func == 4) { return !(alpha == ref);          } // EQUAL
    if (func == 5) { return !(alpha >= ref);          } // GEQUAL
    if (func == 6) { return !(alpha >  ref);          } // GREATER
    return !(alpha != ref);                             // NOTEQUAL
}

// Undo the alpha-edge softening that bilinear MAGNIFICATION adds to a cut-out,
// without touching anything the artist drew soft.
//
// The GS ran this game at 640x448 with one texel per pixel, so a one-texel
// alpha step landed in one pixel.  Rendering the same texture three times
// larger spreads that step across three pixels, and the transparent texels'
// RGB bleeds into the silhouette with it -- the fog-coloured rim on foliage.
//
// Three properties are deliberate:
//   * At 1:1 or under (native, or minified) this is the identity, so a
//     native-resolution frame still matches the hardware exactly.
//   * The ramp is narrowed by at most the magnification factor -- precisely
//     the sharpness that texel step had at 1:1, never more.  It restores the
//     original look rather than imposing a harder one.
//   * Flat alpha is left alone.  Contrast-stretching a uniformly 25%-opaque
//     pane about 0.5 would erase it, so the effect is gated on alpha actually
//     moving across the pixel.
//
// `uv` is the same coordinate the texture was sampled with.
float MikuPanSharpenAlpha(float alpha, float2 uv)
{
    float strength = saturate(uAlphaSharpen.x);
    if (strength <= 0.0)
    {
        return alpha;
    }

    // Texels crossed per output pixel; below one, the sampler is magnifying.
    float2 texel = uv * max(uTextureSize.xy, 1.0.xx);
    float  texels_per_pixel = max(length(fwidth(texel)), 1e-6);
    float  magnification = 1.0 / texels_per_pixel;
    if (magnification <= 1.0)
    {
        return alpha;
    }

    // How fast alpha moves here.  Near zero means a flat region with no edge
    // to restore -- see the third property above.
    float edge = saturate(fwidth(alpha) * 8.0);
    if (edge <= 0.0)
    {
        return alpha;
    }

    float sharp = saturate((alpha - 0.5) * magnification + 0.5);
    return lerp(alpha, sharp, edge * strength);
}

// A hard cut-out threshold -- discard anything below it outright.
//
// Independent of both MikuPanSharpenAlpha() above and the GS's own alpha
// test, and a blunter instrument than either: it aliases the edge, where
// the sharpener keeps it antialiased, and it can thin or erase very fine
// geometry (hair, distant foliage) that is legitimately semi-transparent
// all the way across.  0 disables it.
//
// Applied to the TEXTURE alpha rather than the final fragment alpha, for
// the same reason the sharpener is: testing after the vertex-colour
// modulate would make a billboard being faded out vanish the moment the
// fade crossed the threshold, instead of fading.
bool MikuPanAlphaCutoffFails(float alpha)
{
    float cutoff = uAlphaSharpen.y;
    return cutoff > 0.0 && alpha < cutoff;
}

#ifndef MIKUPAN_CUSTOM_LIGHT_BLOCK
cbuffer LightBlock : register(b1, MIKUPAN_UNIFORM_SPACE)
{
    float4 uAmbient;

    int4 uParCount;
    float4 uParDir[3];
    float4 uParDiffuse[3];
    float4 uParSpecular[3];
    float4 uParHalfway[3];

    int4 uPointCount;
    float4 uPointPos[3];
    float4 uPointDiffuse[3];
    float4 uPointSpecular[3];
    float4 uPointPower[3];

    int4 uSpotCount;
    float4 uSpotPos[3];
    float4 uSpotDir[3];
    float4 uSpotDiffuse[3];
    float4 uSpotSpecular[3];
    float4 uSpotPower[3];
    float4 uSpotIntens[3];
    float4 uMaterialAlpha;
};
#endif

cbuffer MaterialBlock : register(b2, MIKUPAN_UNIFORM_SPACE)
{
    float4 uMatAmbient;
    float4 uMatDiffuse;
    float4 uMatSpecular;
    float4 uMatEmission;
};

#ifndef MIKUPAN_VERTEX_STAGE
Texture2D uTexture : register(t0, space2);
SamplerState uTextureSampler : register(s0, space2);
Texture2D uAuxTexture : register(t1, space2);
SamplerState uAuxTextureSampler : register(s1, space2);
#endif

static const float kGsModulateScale = 255.0 / 128.0;

/*
 * Convert a GL-convention clip z (near -> -w, far -> +w) into the reversed
 * [0, w] the host depth buffer runs on: near -> w, far -> 0.
 *
 * ONLY for geometry the CPU handed over already in clip space, built from the
 * *engine's* matrices, which are still in the ROM's symmetric convention --
 * the sprite paths, the flame/spark billboards that carry a real depth
 * (MioPan_RendererDrawTexturedQuadDepth), and the clip-space triangle and line
 * bridges. Anything transformed here by `projection` / `mvp` / `viewProj` must
 * NOT call this: MioPan_Graph3dApplyCamera() already reversed those.
 *
 * A plain 2D sprite passes z = 0, w = 1 and lands on 0.5 either way, so the
 * conversion is invisible to the UI paths that neither test nor write depth.
 *
 * On the cancellation: `0.5*w - 0.5*z` subtracts two values that are both about
 * the view depth, so the small result keeps only an absolute ulp of that depth
 * -- the same precision forward-Z had. That is exactly why the *scene's*
 * reversal is folded into the projection matrix instead of being done here.
 * For a world-space billboard it is NOT acceptable, however small the error
 * looks near the origin: the CPU's clip z and w each carry an ulp of the WORLD
 * coordinates fed through the matrix, and in the outdoor maps, ~10^4 units
 * out, that leaves the depth wrong by 4 / 12 / 33 / 107 units (median) at 2k /
 * 5k / 10k / 18k -- enough to flip a haze, glow or flame in front of or behind
 * whatever it overlaps every time anything moves.  So those bridges rebuild
 * the reversed z from w with the meshes' own depth row and raise uClipZ.x,
 * and this returns their position untouched.
 */
float4 MikuPanFixClipZ(float4 clip)
{
    if (uClipZ.x != 0.0)
    {
        return clip;
    }
    clip.z = clip.w * 0.5 - clip.z * 0.5;
    return clip;
}

float3 ApplyGsModulate(float3 textureColor, float3 ps2Color255)
{
    return clamp(clamp(ps2Color255, 0.0.xxx, 1.0.xxx) *
                 textureColor * kGsModulateScale, 0.0.xxx, 1.0.xxx);
}

/*
 * Snap a fog factor to the GS's own fog resolution.
 *
 * `f` is in the 0..1 this shader works in, i.e. the GS's 0..255 F already
 * divided by 255 (see MioPan_RendererSetFog()); the result is the largest
 * whole GS unit not above it, back in that same 0..1.  MikuPanApplyGsFog()
 * below carries the reasoning for why the truncation is the hardware's own
 * and not an approximation of it.
 */
float MikuPanQuantizeGsFog(float f)
{
    return floor(f * 255.0) * (1.0 / 255.0);
}

/*
 * GS fogging, the stage PRIM's FOGE bit selects.
 *
 * On hardware VU1 computed one 8-bit F per vertex out of the fog block
 * gra3dApplyFog() uploaded, the GS interpolated it, and the blend was
 *
 *     C = lerp(FOGCOL, C, F / 255)
 *
 * so F == 255 is untouched scene and F == 0 is pure fog colour.  There is no
 * VU here, so the same ramp is evaluated per fragment instead:
 *
 *     F = clamp(FA + FB / viewZ, fMin, fMax)      (g3dCalcFA / g3dCalcFB)
 *
 * which is exact rather than interpolated -- the ROM's own derivation puts
 * F == fMax at fNear and F == fMin at fFar.
 *
 * `viewDepth` is the clip w the vertex stage hands over in an interpolant of
 * its own (`fogW.x`).  g3dCalcViewClipMatrixPerspective() builds a row-vector
 * projection with mat[2][3] == 1 and mat[3][3] == 0, so clip w *is* the
 * view-space depth, and interpolated perspective-correctly it is the exact
 * depth at this pixel -- its reciprocal is linear in screen space, which is
 * the same 1/w the VU1 fed MADDq and the GS then interpolated.
 *
 * NOT SV_Position.w, which this used to read: that is 1/w on Vulkan and Metal
 * but the clip w itself on Direct3D 12 (DXC's -fvk-use-dx-position-w exists
 * for exactly this), so on D3D12 every fragment computed FA + FB * w, clamped
 * to fMax, and the fog lost its distance falloff entirely.
 *
 * The ramp is then QUANTISED, because the hardware's was.  F left VU1 as a
 * float but never reached the GS as one: every one of the four microprograms
 * ends the fog kernel with
 *
 *     MADDq.w    vf31, vf17, Q       F = FA + FB * (1/w)
 *     MAXx.w     vf31, vf31, vf17x   F = max(F, fMin)
 *     MINIy.w    vf31, vf31, vf17y   F = min(F, fMax)
 *     FTOI4.xyzw vf26, vf31          *16, truncated
 *
 * (ff2_00 0x710..0x788, ff2_01 0x3d60, ff2_02 0x6520, and their siblings).
 * FTOI4 is there for XY's 12.4 screen coordinates, but it runs on the whole
 * register, and that is deliberate: a PACKED XYZF2 vertex takes F from bits
 * 4..11 of its fourth word, so the same *16 drops a WHOLE fog unit into the
 * GS's 8-bit F field.  fMin and fMax arrive as integers too -- MapFog.c keeps
 * the whole MAP_FOG_HEAD as ints and hands gra3dSetFog() 50 and 200 -- so
 * hardware blended with one of 256 discrete levels, and a surface only changed
 * its fog when its depth crossed a whole unit.
 *
 * Evaluating the ramp at float precision instead makes the blend a continuous
 * function of view depth, so anything that moves that depth a little moves the
 * fog with it: sub-unit camera motion, and the per-subframe rewrite of the mvp
 * that ReprojectDrawsAt() does, both show up as the fog level sliding around,
 * with neighbouring surfaces trading places in the depth cue it gives, where
 * the hardware sat still.  floor() in GS units restores that.
 *
 * Quantising BEFORE the clamp rather than after is not a reordering: fMin and
 * fMax are whole units, so floor() commutes with a clamp against them, and
 * this order keeps the two endpoints exactly the values the uniform holds
 * rather than a round-trip error away from them.
 *
 * uFog is (fMin, fMax, FA, FB) already divided by 255, and uFogColor.rgb is
 * the FOGCOL register in the 0..1 the framebuffer holds.  uFogColor.w is the
 * enable: the renderer raises it only for the transformed 3D draws the ROM
 * set FOGE on, so 2D sprites -- whose clip w is a meaningless 1.0 -- are left
 * alone.
 */
float3 MikuPanApplyGsFog(float3 color, float viewDepth)
{
    if (uFogColor.w <= 0.0)
    {
        return color;
    }

    /* Clipping keeps every rasterised fragment in front of the near plane, so
     * this only guards a degenerate input against a 0/0. */
    float f = MikuPanQuantizeGsFog(uFog.z + uFog.w / max(viewDepth, 1e-6));
    f = clamp(f, uFog.x, uFog.y);
    return lerp(uFogColor.rgb, color, f);
}

#ifndef MIKUPAN_CUSTOM_LIGHT_BLOCK
float3 CalcPS2LitColor(float4 normal, float4 viewPos, float3 vertexColor)
{
    float3 N = normalize(normal.xyz);
    const float parallel_shininess = 4.0;
    float3 vc = vertexColor + uAmbient.rgb;

    for (int i = 0; i < uParCount.x; i++)
    {
        float NdotL = max(dot(N, uParDir[i].xyz), 0.0);
        float NdotH = max(dot(N, uParHalfway[i].xyz), 0.0);
        vc += uParDiffuse[i].rgb * NdotL;
        vc += uParSpecular[i].rgb * pow(NdotH, parallel_shininess);
    }

    for (int j = 0; j < uPointCount.x; j++)
    {
        float3 L = uPointPos[j].xyz - viewPos.xyz;
        float dist2 = dot(L, L);
        if (dist2 <= 0.0)
        {
            continue;
        }

        float inv_dist = rsqrt(dist2);
        float3 Ldir = L * inv_dist;
        float NdotL = max(dot(N, Ldir), 0.0);
        float intensity = min(NdotL * uPointPower[j].x * inv_dist, 1.0);

        vc += uPointDiffuse[j].rgb * intensity;

        float spec = intensity * intensity;
        spec *= spec;
        spec *= spec;
        vc += uPointSpecular[j].rgb * spec;
    }

    for (int k = 0; k < uSpotCount.x; k++)
    {
        float3 L = uSpotPos[k].xyz - viewPos.xyz;
        float dist2 = dot(L, L);
        if (dist2 <= 0.0)
        {
            continue;
        }

        float inv_dist = rsqrt(dist2);
        float3 Ldir = L * inv_dist;
        float3 Z = normalize(uSpotDir[k].xyz);

        // The dot is NEGATED, matching MioPanVu1Spot() in
        // miopan_vu1_lighting.hlsli and gra3dCalcVu1VertexColor() in gra3d.c.
        // `Ldir` runs surface -> light while uSpotDir is the BEAM (the port's
        // engine-wide convention; the VU1 microcode is the other way round and
        // every host transcription reconciles it here), so without the negation
        // a surface lit BY the beam gives dot = -1 and is culled, while one
        // BEHIND the lamp gives +1 and is lit -- the cone 180 degrees out.
        //
        // Clamping before the square is what keeps it to ONE cone: cos^2 is
        // even, so squaring an unclamped dot lights both hemispheres.  Do not
        // reorder these two lines.
        //
        // This block is not reachable today -- the eight shaders that call
        // CalcPS2LitColor (mesh_0x2*, mesh_0xA*, mesh_0x12,
        // textured_mesh_lighted.frag) are not registered in miopan_renderer.cpp,
        // and the two that are live define MIKUPAN_CUSTOM_LIGHT_BLOCK and use
        // MioPanVu1Evaluate() instead.  It is corrected anyway so that reviving
        // one of them does not reintroduce a half-converted spot convention,
        // which is what made the cutscene flashlight bidirectional.
        // vu1/LIGHTING.md section 3.3.
        float cd = max(-dot(Ldir, Z), 0.0);
        float cos2 = cd * cd;
        float gate = max(cos2 - uSpotIntens[k].x, 0.0) * uSpotIntens[k].y;
        gate = clamp(gate, 0.0, 1.0);
        if (cos2 <= uSpotIntens[k].x)
        {
            continue;
        }

        float NdotL = max(dot(N, Ldir), 0.0);
        float intensity = min(NdotL * uSpotPower[k].x * inv_dist, 1.0);

        vc += uSpotDiffuse[k].rgb * intensity * gate;

        float spec = intensity * intensity;
        spec *= spec;
        spec *= spec;
        vc += uSpotSpecular[k].rgb * spec * gate;
    }

    return vc;
}
#endif

float3 ToBlackWhite(float3 color)
{
    float gray = (color.r + color.g + color.b) / 3.0;
    return gray.xxx;
}

float3 ApplyBlackWhiteLightOnly(float3 color)
{
    return uFlags2.x != 0 ? ToBlackWhite(color) : color;
}

#endif
