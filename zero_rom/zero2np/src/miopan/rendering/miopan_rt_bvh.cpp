#include "miopan_rt_bvh.h"

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <utility>

namespace MioPanRt
{
namespace
{

/* The shader's traversal stack holds 32 entries and a depth-first walk that
 * always descends left keeps at most one pending node per level, so the tree
 * may not be deeper than that.  Anything still oversized at the cap becomes a
 * large leaf: slower to test, never wrong. */
constexpr int kMaxDepth = 30;
constexpr uint32_t kMaxLeaf = 4;
constexpr int kMaxBins = 16;
/* Below this many triangles a node is split at its median instead of binned:
 * the SAH sweep's fixed cost per node dwarfs what it saves on a handful of
 * triangles, and most nodes are that small. */
constexpr uint32_t kMinBinnedCount = 24;

/* Deliberately without default member initialisers, so the per-node bin
 * arrays cost nothing until Reset() touches the bins actually in use. */
struct Aabb
{
    float mn[3];
    float mx[3];

    static Aabb Empty()
    {
        Aabb box;
        box.Reset();
        return box;
    }
    void Reset()
    {
        mn[0] = mn[1] = mn[2] = FLT_MAX;
        mx[0] = mx[1] = mx[2] = -FLT_MAX;
    }
    void Grow(const float *p)
    {
        for (int a = 0; a < 3; a++)
        {
            mn[a] = std::min(mn[a], p[a]);
            mx[a] = std::max(mx[a], p[a]);
        }
    }
    void Grow(const Aabb &b)
    {
        for (int a = 0; a < 3; a++)
        {
            mn[a] = std::min(mn[a], b.mn[a]);
            mx[a] = std::max(mx[a], b.mx[a]);
        }
    }
    float Area() const
    {
        const float dx = mx[0] - mn[0];
        const float dy = mx[1] - mn[1];
        const float dz = mx[2] - mn[2];
        if (dx < 0.0f || dy < 0.0f || dz < 0.0f)
        {
            return 0.0f;
        }
        return dx * dy + dy * dz + dz * dx;
    }
};

/* Partitioned in place, so every pass over a node's range is a linear walk
 * with no index indirection. */
struct BuildTri
{
    Aabb box;
    float centroid[3];
    uint32_t source;
};

struct Builder
{
    std::vector<BuildTri> tris;
    std::vector<GpuNode> nodes;
    int bins = 12;
    bool all_axes = true;
    int depth = 0;

    static void SetBounds(GpuNode &node, const Aabb &box, uint32_t first,
                          uint32_t count)
    {
        for (int a = 0; a < 3; a++)
        {
            node.bmin[a] = box.mn[a];
            node.bmax[a] = box.mx[a];
        }
        node.left_first = first;
        node.count = count;
    }

    void Scan(uint32_t first, uint32_t count, Aabb *box, Aabb *cbox) const
    {
        for (uint32_t i = first; i < first + count; i++)
        {
            box->Grow(tris[i].box);
            cbox->Grow(tris[i].centroid);
        }
    }

    /* Split in half along the longest centroid axis.  The fallback for a
     * range binning cannot separate -- every centroid in one bin. */
    uint32_t MedianSplit(uint32_t first, uint32_t count, const Aabb &cbox)
    {
        int axis = 0;
        for (int a = 1; a < 3; a++)
        {
            if (cbox.mx[a] - cbox.mn[a] > cbox.mx[axis] - cbox.mn[axis])
            {
                axis = a;
            }
        }
        const uint32_t half = count / 2;
        std::nth_element(tris.begin() + first, tris.begin() + first + half,
                         tris.begin() + first + count,
                         [axis](const BuildTri &l, const BuildTri &r) {
                             return l.centroid[axis] < r.centroid[axis];
                         });
        return half;
    }

    void Subdivide(uint32_t node_index, const Aabb &cbox, int level)
    {
        depth = std::max(depth, level);
        const uint32_t first = nodes[node_index].left_first;
        const uint32_t count = nodes[node_index].count;
        if (count <= 2 || level >= kMaxDepth)
        {
            return;
        }

        int axis_begin = 0;
        int axis_end = 3;
        if (count < kMinBinnedCount)
        {
            if (count <= kMaxLeaf)
            {
                return;
            }
            /* Skip the binning loop entirely; best_axis stays -1 and the
             * median split below takes over. */
            axis_end = 0;
        }
        else if (!all_axes)
        {
            /* The per-frame build: the longest axis alone is most of the
             * quality for a third of the binning. */
            int longest = 0;
            for (int a = 1; a < 3; a++)
            {
                if (cbox.mx[a] - cbox.mn[a] > cbox.mx[longest] - cbox.mn[longest])
                {
                    longest = a;
                }
            }
            axis_begin = longest;
            axis_end = longest + 1;
        }

        int best_axis = -1;
        int best_split = 0;
        float best_cost = FLT_MAX;
        Aabb best_left = Aabb::Empty();
        Aabb best_right = Aabb::Empty();
        for (int axis = axis_begin; axis < axis_end; axis++)
        {
            const float extent = cbox.mx[axis] - cbox.mn[axis];
            if (!(extent > 1e-6f))
            {
                continue;
            }
            Aabb bin_box[kMaxBins];
            for (int b = 0; b < bins; b++)
            {
                bin_box[b].Reset();
            }
            uint32_t bin_count[kMaxBins] = {};
            const float scale = (float)bins / extent;
            const float origin = cbox.mn[axis];
            for (uint32_t i = first; i < first + count; i++)
            {
                int b = (int)((tris[i].centroid[axis] - origin) * scale);
                b = std::min(std::max(b, 0), bins - 1);
                bin_count[b]++;
                bin_box[b].Grow(tris[i].box);
            }

            Aabb left_box[kMaxBins];
            Aabb right_box[kMaxBins];
            uint32_t left_count[kMaxBins];
            uint32_t right_count[kMaxBins];
            Aabb left = Aabb::Empty();
            Aabb right = Aabb::Empty();
            uint32_t left_sum = 0;
            uint32_t right_sum = 0;
            for (int i = 0; i < bins - 1; i++)
            {
                left_sum += bin_count[i];
                left.Grow(bin_box[i]);
                left_count[i] = left_sum;
                left_box[i] = left;

                right_sum += bin_count[bins - 1 - i];
                right.Grow(bin_box[bins - 1 - i]);
                right_count[bins - 2 - i] = right_sum;
                right_box[bins - 2 - i] = right;
            }
            for (int i = 0; i < bins - 1; i++)
            {
                if (left_count[i] == 0 || right_count[i] == 0)
                {
                    continue;
                }
                const float cost = (float)left_count[i] * left_box[i].Area() +
                                   (float)right_count[i] * right_box[i].Area();
                if (cost < best_cost)
                {
                    best_cost = cost;
                    best_axis = axis;
                    best_split = i;
                    best_left = left_box[i];
                    best_right = right_box[i];
                }
            }
        }

        uint32_t left_count = 0;
        Aabb left_cbox = Aabb::Empty();
        Aabb right_cbox = Aabb::Empty();
        bool boxes_known = false;
        if (best_axis >= 0)
        {
            const GpuNode &node = nodes[node_index];
            const float dx = node.bmax[0] - node.bmin[0];
            const float dy = node.bmax[1] - node.bmin[1];
            const float dz = node.bmax[2] - node.bmin[2];
            /* One traversal step against testing every triangle here. */
            const float leaf_cost = (float)count * (dx * dy + dy * dz + dz * dx);
            if (best_cost >= leaf_cost && count <= kMaxLeaf)
            {
                return;
            }
            const float scale =
                (float)bins / (cbox.mx[best_axis] - cbox.mn[best_axis]);
            const float origin = cbox.mn[best_axis];
            uint32_t i = first;
            uint32_t j = first + count;
            while (i < j)
            {
                int b = (int)((tris[i].centroid[best_axis] - origin) * scale);
                b = std::min(std::max(b, 0), bins - 1);
                if (b <= best_split)
                {
                    left_cbox.Grow(tris[i].centroid);
                    i++;
                }
                else
                {
                    --j;
                    std::swap(tris[i], tris[j]);
                    right_cbox.Grow(tris[j].centroid);
                }
            }
            left_count = i - first;
            boxes_known = left_count != 0 && left_count != count;
        }
        else if (count <= kMaxLeaf)
        {
            return;
        }

        if (!boxes_known)
        {
            left_count = MedianSplit(first, count, cbox);
            best_left = Aabb::Empty();
            best_right = Aabb::Empty();
            left_cbox = Aabb::Empty();
            right_cbox = Aabb::Empty();
            Scan(first, left_count, &best_left, &left_cbox);
            Scan(first + left_count, count - left_count, &best_right,
                 &right_cbox);
        }

        const uint32_t left = (uint32_t)nodes.size();
        nodes.emplace_back();
        nodes.emplace_back();
        SetBounds(nodes[left], best_left, first, left_count);
        SetBounds(nodes[left + 1], best_right, first + left_count,
                  count - left_count);
        nodes[node_index].left_first = left;
        nodes[node_index].count = 0;
        Subdivide(left, left_cbox, level + 1);
        Subdivide(left + 1, right_cbox, level + 1);
    }
};

bool IsFinite3(const float *p)
{
    return std::isfinite(p[0]) && std::isfinite(p[1]) && std::isfinite(p[2]);
}

void WriteTri(GpuTri &tri, const float *v, unsigned char flags,
              uint32_t cutout)
{
    for (int a = 0; a < 3; a++)
    {
        tri.v0[a] = v[a];
        tri.e1[a] = v[3 + a] - v[a];
        tri.e2[a] = v[6 + a] - v[a];
    }
    tri.v0[3] = (float)flags;
    /* Bits, not a value: the shader reads it back with asuint. */
    std::memcpy(&tri.e1[3], &cutout, sizeof(cutout));
    tri.e2[3] = 0.0f;
}

} // namespace

void BuildBvh(const float *corners, const unsigned char *tri_flags,
              const uint32_t *tri_cutout, size_t tri_count, int bins,
              bool all_axes, Bvh *out)
{
    const auto start = std::chrono::steady_clock::now();
    out->depth = 0;

    Builder builder;
    builder.bins = std::min(std::max(bins, 4), kMaxBins);
    builder.all_axes = all_axes;
    builder.tris.reserve(tri_count);
    Aabb root_box = Aabb::Empty();
    Aabb root_cbox = Aabb::Empty();
    for (size_t t = 0; t < tri_count; t++)
    {
        const float *v = corners + t * 9;
        if (!IsFinite3(v) || !IsFinite3(v + 3) || !IsFinite3(v + 6))
        {
            continue;
        }
        const float e1[3] = {v[3] - v[0], v[4] - v[1], v[5] - v[2]};
        const float e2[3] = {v[6] - v[0], v[7] - v[1], v[8] - v[2]};
        const float n[3] = {e1[1] * e2[2] - e1[2] * e2[1],
                            e1[2] * e2[0] - e1[0] * e2[2],
                            e1[0] * e2[1] - e1[1] * e2[0]};
        if (n[0] * n[0] + n[1] * n[1] + n[2] * n[2] <= 1e-12f)
        {
            continue;
        }
        BuildTri tri;
        tri.box.Reset();
        tri.box.Grow(v);
        tri.box.Grow(v + 3);
        tri.box.Grow(v + 6);
        for (int a = 0; a < 3; a++)
        {
            tri.centroid[a] = (v[a] + v[3 + a] + v[6 + a]) * (1.0f / 3.0f);
        }
        tri.source = (uint32_t)t;
        root_box.Grow(tri.box);
        root_cbox.Grow(tri.centroid);
        builder.tris.push_back(tri);
    }

    const uint32_t count = (uint32_t)builder.tris.size();
    /* Reuse the caller's storage: the dynamic hierarchy is rebuilt every
     * frame, and reallocating it each time showed up in the build time. */
    builder.nodes.swap(out->nodes);
    builder.nodes.clear();
    out->tris.clear();
    if (count != 0)
    {
        /* A binary tree over `count` leaves of at least one triangle has at
         * most 2*count - 1 nodes; reserving that keeps every node reference
         * stable while the recursion appends children. */
        builder.nodes.reserve((size_t)count * 2u);
        builder.nodes.emplace_back();
        Builder::SetBounds(builder.nodes[0], root_box, 0, count);
        builder.Subdivide(0, root_cbox, 0);

        out->tris.resize(count);
        out->order.resize(count);
        for (uint32_t i = 0; i < count; i++)
        {
            const uint32_t source = builder.tris[i].source;
            out->order[i] = source;
            WriteTri(out->tris[i], corners + (size_t)source * 9,
                     tri_flags != nullptr ? tri_flags[source] : 0,
                     tri_cutout != nullptr ? tri_cutout[source] : 0u);
        }
        out->depth = builder.depth;
    }
    else
    {
        out->order.clear();
    }
    out->nodes.swap(builder.nodes);
    out->input_count = tri_count;

    out->build_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
}

bool RefitBvh(const float *corners, const unsigned char *tri_flags,
              const uint32_t *tri_cutout, size_t tri_count, Bvh *bvh)
{
    const auto start = std::chrono::steady_clock::now();
    if (tri_count != bvh->input_count || bvh->nodes.empty() ||
        bvh->order.size() != bvh->tris.size())
    {
        return false;
    }
    /* A corner gone non-finite cannot be bounded; rebuild, which drops it. */
    for (uint32_t source : bvh->order)
    {
        const float *v = corners + (size_t)source * 9;
        if (!IsFinite3(v) || !IsFinite3(v + 3) || !IsFinite3(v + 6))
        {
            return false;
        }
    }
    for (size_t i = 0; i < bvh->tris.size(); i++)
    {
        const uint32_t source = bvh->order[i];
        WriteTri(bvh->tris[i], corners + (size_t)source * 9,
                 tri_flags != nullptr ? tri_flags[source] : 0,
                 tri_cutout != nullptr ? tri_cutout[source] : 0u);
    }

    /* Children are always appended after their parent, so walking the array
     * backwards visits both children before the node that owns them. */
    for (size_t n = bvh->nodes.size(); n-- > 0;)
    {
        GpuNode &node = bvh->nodes[n];
        Aabb box = Aabb::Empty();
        if (node.count > 0)
        {
            for (uint32_t i = node.left_first; i < node.left_first + node.count;
                 i++)
            {
                const GpuTri &tri = bvh->tris[i];
                float p[3];
                box.Grow(tri.v0);
                for (int a = 0; a < 3; a++)
                {
                    p[a] = tri.v0[a] + tri.e1[a];
                }
                box.Grow(p);
                for (int a = 0; a < 3; a++)
                {
                    p[a] = tri.v0[a] + tri.e2[a];
                }
                box.Grow(p);
            }
        }
        else
        {
            for (uint32_t c = node.left_first; c < node.left_first + 2; c++)
            {
                box.Grow(bvh->nodes[c].bmin);
                box.Grow(bvh->nodes[c].bmax);
            }
        }
        for (int a = 0; a < 3; a++)
        {
            node.bmin[a] = box.mn[a];
            node.bmax[a] = box.mx[a];
        }
    }

    bvh->build_ms = std::chrono::duration<double, std::milli>(
                        std::chrono::steady_clock::now() - start)
                        .count();
    return true;
}

namespace
{

/* Slab test against [0, tmax]; `tnear` is where the ray enters the box. */
inline bool RayBox(const GpuNode &node, const float origin[3],
                   const float inv_dir[3], float tmax, float *tnear)
{
    float tn = 0.0f;
    float tf = tmax;
    for (int a = 0; a < 3; a++)
    {
        float t0 = (node.bmin[a] - origin[a]) * inv_dir[a];
        float t1 = (node.bmax[a] - origin[a]) * inv_dir[a];
        if (t0 > t1)
        {
            std::swap(t0, t1);
        }
        tn = std::max(tn, t0);
        tf = std::min(tf, t1);
    }
    *tnear = tn;
    return tn <= tf;
}

} // namespace

bool TraceClosest(const Bvh &bvh, const float origin[3], const float dir[3],
                  float tmax, unsigned char skip_flags, bool two_sided,
                  RayHit *hit)
{
    if (bvh.nodes.empty() || bvh.tris.empty() || hit == nullptr)
    {
        return false;
    }

    /* As in the shader: keep each component's sign but never let it reach
     * zero, so the slab test divides into +/-huge instead of making NaN. */
    float inv_dir[3];
    for (int a = 0; a < 3; a++)
    {
        float d = dir[a];
        if (std::fabs(d) < 1e-20f)
        {
            d = d < 0.0f ? -1e-20f : 1e-20f;
        }
        inv_dir[a] = 1.0f / d;
    }

    float best = tmax;
    const GpuTri *best_tri = nullptr;
    float tnear;
    if (!RayBox(bvh.nodes[0], origin, inv_dir, best, &tnear))
    {
        return false;
    }

    /* Nearer child first; the farther one is stacked and tested again against
     * the closest hit so far when it comes off, which prunes most of them. */
    uint32_t stack[64];
    int sp = 0;
    uint32_t node = 0;
    for (;;)
    {
        const GpuNode &n = bvh.nodes[node];
        bool descended = false;
        if (n.count > 0)
        {
            for (uint32_t i = 0; i < n.count; i++)
            {
                const GpuTri &tri = bvh.tris[n.left_first + i];
                const unsigned flags = (unsigned)(tri.v0[3] + 0.5f);
                if ((flags & skip_flags) != 0)
                {
                    continue;
                }
                const bool both = two_sided || (flags & kTriTwoSided) != 0;
                /* Moller-Trumbore.  det = -dot(dir, front), so the origin
                 * sees the front face exactly when det > 0. */
                const float p[3] = {dir[1] * tri.e2[2] - dir[2] * tri.e2[1],
                                    dir[2] * tri.e2[0] - dir[0] * tri.e2[2],
                                    dir[0] * tri.e2[1] - dir[1] * tri.e2[0]};
                const float det = tri.e1[0] * p[0] + tri.e1[1] * p[1] +
                                  tri.e1[2] * p[2];
                if (both ? std::fabs(det) < 1e-12f : det < 1e-12f)
                {
                    continue;
                }
                const float inv = 1.0f / det;
                const float s[3] = {origin[0] - tri.v0[0],
                                    origin[1] - tri.v0[1],
                                    origin[2] - tri.v0[2]};
                const float u = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
                if (u < 0.0f || u > 1.0f)
                {
                    continue;
                }
                const float q[3] = {s[1] * tri.e1[2] - s[2] * tri.e1[1],
                                    s[2] * tri.e1[0] - s[0] * tri.e1[2],
                                    s[0] * tri.e1[1] - s[1] * tri.e1[0]};
                const float v =
                    (dir[0] * q[0] + dir[1] * q[1] + dir[2] * q[2]) * inv;
                if (v < 0.0f || u + v > 1.0f)
                {
                    continue;
                }
                const float t = (tri.e2[0] * q[0] + tri.e2[1] * q[1] +
                                 tri.e2[2] * q[2]) * inv;
                if (t > 1e-3f && t < best)
                {
                    best = t;
                    best_tri = &tri;
                }
            }
        }
        else
        {
            uint32_t near_child = n.left_first;
            uint32_t far_child = n.left_first + 1;
            float t_near_child;
            float t_far_child;
            const bool hit_near = RayBox(bvh.nodes[near_child], origin,
                                         inv_dir, best, &t_near_child);
            const bool hit_far = RayBox(bvh.nodes[far_child], origin,
                                        inv_dir, best, &t_far_child);
            if (hit_near && hit_far)
            {
                if (t_far_child < t_near_child)
                {
                    std::swap(near_child, far_child);
                }
                if (sp < 64)
                {
                    stack[sp++] = far_child;
                }
                node = near_child;
                descended = true;
            }
            else if (hit_near || hit_far)
            {
                node = hit_near ? near_child : far_child;
                descended = true;
            }
        }
        if (descended)
        {
            continue;
        }
        bool popped = false;
        while (sp > 0)
        {
            node = stack[--sp];
            if (RayBox(bvh.nodes[node], origin, inv_dir, best, &tnear))
            {
                popped = true;
                break;
            }
        }
        if (!popped)
        {
            break;
        }
    }

    if (best_tri == nullptr)
    {
        return false;
    }
    const GpuTri &tri = *best_tri;
    const float nrm[3] = {tri.e1[1] * tri.e2[2] - tri.e1[2] * tri.e2[1],
                          tri.e1[2] * tri.e2[0] - tri.e1[0] * tri.e2[2],
                          tri.e1[0] * tri.e2[1] - tri.e1[1] * tri.e2[0]};
    float len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
    if (!(len > 0.0f))
    {
        return false;
    }
    if (nrm[0] * dir[0] + nrm[1] * dir[1] + nrm[2] * dir[2] > 0.0f)
    {
        len = -len;
    }
    hit->t = best;
    for (int a = 0; a < 3; a++)
    {
        hit->normal[a] = nrm[a] / len;
    }
    return true;
}

} // namespace MioPanRt
