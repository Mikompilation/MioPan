#ifndef MIOPAN_RT_BVH_H
#define MIOPAN_RT_BVH_H

/*
 * Bounding volume hierarchy for the ray-traced shadow proof of concept.
 *
 * Built on the CPU from world-space triangles and read by
 * resources/shaders/hlsl/miopan_rt.hlsli, which walks it per pixel inside the
 * VU1 light kernel.  There is no hardware acceleration structure here: SDL_GPU
 * exposes none, so the hierarchy is an ordinary storage buffer and the
 * traversal is ordinary shader code.  That is what keeps this vendor-neutral.
 *
 * Both layouts are mirrored by the shader and must not change on one side
 * only.
 */

#include <cstddef>
#include <cstdint>
#include <vector>

namespace MioPanRt
{

/* Two float4s.  An interior node's children are left_first and left_first + 1;
 * a leaf (count > 0) owns triangles [left_first, left_first + count). */
struct GpuNode
{
    float bmin[3];
    uint32_t left_first;
    float bmax[3];
    uint32_t count;
};

/* Three float4s: one corner and the two edges leaving it, which is what the
 * Moller-Trumbore test wants.  v0[3] carries the triangle's flag byte (see
 * kTriPlayer) as a float; e1[3] the bits of its alpha-test record, 1 + an
 * index into the hierarchy's GpuCutout array, or 0 for a solid triangle (see
 * BuildBvh()); e2[3] is unused. */
struct GpuTri
{
    float v0[4];
    float e1[4];
    float e2[4];
};

/* Two float4s: what a hit on a triangle drawn through a texture with holes
 * needs to tell hole from solid.  The texture coordinates at the first
 * corner and how they change along e1 and e2 -- the hit's barycentrics then
 * give the texel -- and where the texture's opacity bitmask starts in the
 * mask buffer, in 32-bit words, with its size (width | height << 16).  A
 * mask is one bit per texel, row-major, 1 = solid. */
struct GpuCutout
{
    float uv0[2];
    float duv1[2];
    float duv2[2];
    uint32_t mask_offset;
    uint32_t mask_size;
};

static_assert(sizeof(GpuNode) == 32, "GpuNode must be two float4s");
static_assert(sizeof(GpuTri) == 48, "GpuTri must be three float4s");
static_assert(sizeof(GpuCutout) == 32, "GpuCutout must be two float4s");

struct Bvh
{
    std::vector<GpuNode> nodes;
    std::vector<GpuTri> tris;
    /* The input triangle each tris[] slot came from, for RefitBvh(). */
    std::vector<uint32_t> order;
    /* Input triangles the build saw, dropped ones included. */
    size_t input_count = 0;
    /* Deepest leaf, for checking against the shader's traversal stack. */
    int depth = 0;
    double build_ms = 0.0;
};

/* Triangle flags.  A player triangle belongs to Mio herself: the shader skips
 * it for any light sitting on her (the flashlight is mounted behind her chest,
 * so without this she would eclipse her own beam).
 *
 * Every other triangle is ONE-SIDED unless flagged two-sided: it blocks a
 * light only when that light sees its front face, which is what a rasterised
 * shadow map would do.  The PS2 rooms rely on it -- fill lights sit under the
 * floor and behind walls, aimed through geometry that was never meant to stop
 * them.  Corners are stored so cross(e1, e2) is the front normal.  Characters,
 * and any triangle whose winding the normals could not settle, are
 * two-sided. */
constexpr unsigned char kTriPlayer = 1;
constexpr unsigned char kTriTwoSided = 2;
/* Part see-through: the triangle's texture has holes where it lands (a
 * lattice, a fence, a strip of leaves), which a solid triangle in the
 * hierarchy cannot show.  The top four bits then hold how opaque it is,
 * 0..15 (kTriOpacityShift).  Shadow and occlusion rays test a triangle that
 * has a GpuCutout record texel by texel instead, so this coarse fraction is
 * only their fallback (ambient occlusion lets a ray through with the odds of
 * a hole; shadow rays take it as solid).  The flashlight's bounce, traced on
 * the CPU, passes straight through -- otherwise a wall seen through a
 * lattice an inch in front of it comes out black. */
constexpr unsigned char kTriCutout = 4;
constexpr int kTriOpacityShift = 4;

/* `corners` holds nine floats per triangle: three world-space corners.
 * `tri_flags` is one kTri* byte per triangle, or null for none.
 * `tri_cutout` is one value per triangle -- 1 + the index of its GpuCutout
 * record, which the shader alpha-tests a hit against, or 0 for a solid
 * triangle -- or null for none; the records themselves are the caller's,
 * uploaded beside the hierarchy.  Degenerate and non-finite triangles are
 * dropped.  An empty input gives an empty hierarchy, which the shader is
 * told to skip.
 *
 * Binned SAH.  `all_axes` bins every axis at each split, for the cached
 * static hierarchy; off bins only the longest, for the one rebuilt every
 * frame.  `out`'s storage is reused. */
void BuildBvh(const float *corners, const unsigned char *tri_flags,
              const uint32_t *tri_cutout, size_t tri_count, int bins,
              bool all_axes, Bvh *out);

/* Keep `bvh`'s tree and move its triangles to the new corners, recomputing
 * every bound bottom-up -- one linear pass instead of a build.  Always
 * correct; the tree only gets looser as things move away from where it was
 * built, so the caller rebuilds now and then.  Returns false, changing
 * nothing, when tri_count is not the count `bvh` was built from. */
bool RefitBvh(const float *corners, const unsigned char *tri_flags,
              const uint32_t *tri_cutout, size_t tri_count, Bvh *bvh);

/* The nearest triangle along a ray, walked on the CPU -- for the flashlight's
 * bounce light, which needs to know where a handful of the beam's rays land.
 * The shader only ever asks "is anything in the way"; this asks "what, and
 * where".
 *
 * `dir` must be unit length; `hit->t` is the distance along it.  A one-sided
 * triangle counts only when the ray meets its FRONT face -- the face the
 * ray's origin can see, so a lamp lights the inside of a room and not the
 * back of its walls -- unless `two_sided` or the triangle is flagged so.
 * Triangles carrying any of `skip_flags` are passed straight through.  Only
 * hits nearer than `tmax` are reported, so tracing a second hierarchy with
 * the first one's `hit->t` as its `tmax` finds the nearer of the two.
 * `hit->normal` is the triangle's unit normal turned to face the origin. */
struct RayHit
{
    float t;
    float normal[3];
};
bool TraceClosest(const Bvh &bvh, const float origin[3], const float dir[3],
                  float tmax, unsigned char skip_flags, bool two_sided,
                  RayHit *hit);

} // namespace MioPanRt

#endif
