#include "miopan_graph3d.h"

#include "graphics/graph3d/gra3d.h"
#include "graphics/graph3d/gra3dSGD.h"
#include "graphics/graph3d/gra3dTRI2.h"
#include "graphics/graph3d/g3dCore.h"
#include "common/packfile.h"
#include "miopan/gs/miopan_gs.h"
#include "miopan/gs/miopan_gs_c.h"
#include "miopan/miopan_profiler.h"
#include "miopan_renderer.h"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_mutex.h>
#include <SDL3/SDL_thread.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <cstdlib>
#include <deque>
#include <unordered_map>
#include <vector>

/* Index of the apProcUnitHead[] chain currently being drawn, set by the
 * dispatch loop in gra3dSGD.c.  Used to colour each part in F3 mode. */
extern "C" int g_dbgBlockId = -1;

/* F3 flat-colour debug view; toggled in miopan_renderer.cpp */
extern "C" bool g_dbg_flatcolor = false;

/* F4 -- skip the realtime per-vertex lighting below and submit the flat white
 * the runtime path used before it existed.  Separates "the model is unlit" from
 * "the model is not being drawn" without having to rebuild. */
extern "C" bool g_dbg_nolighting = false;




/* TEMPORARY DIAGNOSTIC -- tally every way a mesh can be dropped below.  All
 * four paths are silent, so partial geometry shows up as missing meshes with
 * no assert anywhere. */


namespace
{
constexpr int kMeshBufferVertexLimit = 1024 * 1024;

int FloatBitsIsOne(float value)
{
    u_int bits;
    std::memcpy(&bits, &value, sizeof(bits));
    return bits == 1;
}

void FixUV(SGDVUMESHST *uv, int num)
{
    if (uv == nullptr)
    {
        return;
    }

    for (int i = 2; i < num; i++)
    {
        if (FloatBitsIsOne(uv[i].fT))
        {
            uv[i].fT = uv[i - 2].fT;
        }
    }
}

void FixColors(VECTOR3 *colors, int num)
{
    float *c = (float *)colors;

    if (colors == nullptr)
    {
        return;
    }

    for (int i = 2; i < num; i++)
    {
        if (FloatBitsIsOne(c[i * 3]))
        {
            c[i * 3 + 0] = c[(i - 2) * 3 + 0];
            c[i * 3 + 1] = c[(i - 2) * 3 + 1];
            c[i * 3 + 2] = c[(i - 2) * 3 + 2];
        }
    }
}

u_int *GetNextUnpackAddr(u_int *prim)
{
    while (prim != nullptr && (*prim & 0x60000000) != 0x60000000)
    {
        prim++;
    }
    return prim;
}

/* TEX0 is GS register state, not per-primitive data: a textured mesh whose
 * packet carries no TEX0 keeps whatever was last set on the GS.  Remember the
 * most recent one so those meshes render with the right texture instead of
 * being treated as untextured. */
sceGsTex0 g_last_tex0;
bool      g_last_tex0_valid = false;

void RememberTex0(const sceGsTex0 &tex0)
{
    g_last_tex0       = tex0;
    g_last_tex0_valid = true;
}

bool Tex0FormatSupported(const sceGsTex0 &tex0)
{
    switch (tex0.PSM)
    {
        case MioPan::GS::PSMCT32:
        case MioPan::GS::PSMCT16:
        case MioPan::GS::PSMCT16S:
        case MioPan::GS::PSMT8:
        case MioPan::GS::PSMT8H:
        case MioPan::GS::PSMT4:
        case MioPan::GS::PSMT4HL:
        case MioPan::GS::PSMT4HH:
            return true;
        default:
            return false;
    }
}

bool Tex0LooksUsable(const sceGsTex0 &tex0)
{
    const int width = 1 << tex0.TW;
    const int height = 1 << tex0.TH;

    return width > 1 && height > 1 && width <= 4096 && height <= 4096 &&
           tex0.TBW > 0 && Tex0FormatSupported(tex0);
}

bool ReadAdTex0(const unsigned char *ad, const unsigned char *end,
                sceGsTex0 *out)
{
    uint64_t data;
    uint64_t addr;
    sceGsTex0 candidate;

    if (ad == nullptr || out == nullptr || ad + 16 > end)
    {
        return false;
    }

    std::memcpy(&data, ad, sizeof(data));
    std::memcpy(&addr, ad + 8, sizeof(addr));
    if ((addr & 0x7f) != SCE_GS_TEX0_1 && (addr & 0x7f) != SCE_GS_TEX0_2)
    {
        return false;
    }

    std::memcpy(&candidate, &data, sizeof(candidate));
    if (!Tex0LooksUsable(candidate))
    {
        return false;
    }

    *out = candidate;
    return true;
}

bool FindTex0InMeshPacket(const unsigned char *start, const unsigned char *end,
                          sceGsTex0 *out)
{
    bool found = false;

    if (start == nullptr || end == nullptr || start >= end || out == nullptr)
    {
        return false;
    }

    for (const unsigned char *p = start; p + 16 <= end; p += 8)
    {
        sceGsTex0 candidate;
        if (ReadAdTex0(p, end, &candidate))
        {
            *out = candidate;
            found = true;
        }
    }

    return found;
}

/*
 * Pick the GS TEST register out of the same A+D stream the TEX0 scan walks.
 *
 * TEST is where a cut-out mesh carries its alpha test, and like TEX0 it is GS
 * state rather than per-primitive data: the value stands until something writes
 * the register again, so it is forwarded to the renderer's shadow rather than
 * returned.  A mesh whose packet carries no TEST inherits whatever the last one
 * set, which is exactly what the hardware would have done.
 */
void ScanMeshPacketGsTest(const unsigned char *start, const unsigned char *end)
{
    if (start == nullptr || end == nullptr || start >= end)
    {
        return;
    }

    for (const unsigned char *p = start; p + 16 <= end; p += 8)
    {
        uint64_t data;
        uint64_t addr;

        std::memcpy(&data, p, sizeof(data));
        std::memcpy(&addr, p + 8, sizeof(addr));

        /*
         * Stricter than the TEX0 scan above, which masks the address to 7 bits
         * and tolerates the odd false positive because the worst case is the
         * wrong texture.  A false positive here would switch on an alpha test
         * nobody asked for and delete the mesh, so require the address word to
         * be exactly the register and the register's reserved bits (19 and up)
         * to be clear, as a real A+D write has them.
         */
        if ((addr != SCE_GS_TEST_1 && addr != SCE_GS_TEST_2) ||
            (data >> 19) != 0)
        {
            continue;
        }

        MioPan_RendererSetGsTestRegister(data);
    }
}

bool ReadTex0Data(const unsigned char *data, const unsigned char *end,
                  sceGsTex0 *out)
{
    sceGsTex0 candidate;

    if (data == nullptr || out == nullptr || data + sizeof(candidate) > end)
    {
        return false;
    }

    std::memcpy(&candidate, data, sizeof(candidate));
    if (!Tex0LooksUsable(candidate))
    {
        return false;
    }

    *out = candidate;
    return true;
}

/* The TEX0 a textured runtime unit carries in its own packet -- no side
 * effects, so the resolved-texture scan and the draw read it the same way. */
bool ReadRuntimeUnitTex0(const SGDPROCUNITHEADER *mesh, sceGsTex0 *out)
{
    const unsigned char *packet_start = (const unsigned char *)&mesh[1];
    const unsigned char *packet_end = (const unsigned char *)&mesh[4];
    const unsigned char *fixed_tex0 = packet_start + 0x18;

    return ReadTex0Data(fixed_tex0, packet_end, out) ||
           ReadAdTex0(fixed_tex0, packet_end, out) ||
           FindTex0InMeshPacket(packet_start, packet_end, out);
}

const sceGsTex0 *FindRuntimeMeshTex0(SGDPROCUNITHEADER *mesh,
                                     sceGsTex0 *tex0_value)
{
    const unsigned char *packet_start;
    const unsigned char *packet_end;

    if (mesh == nullptr || tex0_value == nullptr ||
        (mesh->VUMeshDesc.ucMeshType & 0x02) == 0)
    {
        return nullptr;
    }

    packet_start = (const unsigned char *)&mesh[1];
    packet_end = (const unsigned char *)&mesh[4];

    ScanMeshPacketGsTest(packet_start, packet_end);

    if (ReadRuntimeUnitTex0(mesh, tex0_value))
    {
        RememberTex0(*tex0_value);
        return tex0_value;
    }

    /* textured mesh with no TEX0 of its own: inherit the GS's current one */
    if (g_last_tex0_valid)
    {
        *tex0_value = g_last_tex0;
        return tex0_value;
    }

    return nullptr;
}

/*
 * GS colour register -> shader multiplier.
 *
 * TFX=MODULATE computes (texel * Cf) >> 7 and clamps at 255, so Cf == 128 is
 * "texture unchanged" and the register's top half, 129..255, is real overbright
 * headroom -- up to twice the texel.  Clamping the result at 1.0 here used to
 * throw that half away, which is most of what a flashlight beam is: the baked
 * room colours already sit at 50..125, so the light's whole contribution lands
 * above 128.  The shader multiply saturates in the render target instead, which
 * is where the GS saturates too.
 */
float Ps2ColorFloat(float value)
{
    if (!std::isfinite(value))
    {
        return 1.0f;
    }

    return std::max(0.0f, std::min(2.0f, value / 128.0f));
}

/*
 * GS alpha register -> shader alpha.
 *
 * The blend is (((A - B) * C) >> 7) + D, so As == 0x80 is 1.0 whatever PRIM's
 * TME bit says -- alpha always takes the 128 divisor, unlike colour.  And
 * unlike colour there is no overbright half worth keeping: the blend
 * saturates at As == 1, so this clamps there rather than at 2.
 */
float Ps2AlphaFloat(float value)
{
    if (!std::isfinite(value))
    {
        return 1.0f;
    }

    return std::max(0.0f, std::min(1.0f, value / 128.0f));
}

struct MeshDecodeScratch
{
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uv;
    std::vector<float> rgba;
    std::vector<float> lighting;
    std::vector<unsigned int> indices;
};

thread_local MeshDecodeScratch s_mesh_decode_scratch;

class MeshStreamScope
{
public:
    MeshStreamScope(const sceGsTex0 *tex0, int max_vertices,
                    const float *local_world,
                    const MioPanLightState *fragment_lights = nullptr,
                    const void *texture = nullptr)
        : token_(MioPan_RendererBeginMeshStream(
              tex0, max_vertices, local_world, fragment_lights, texture))
    {
    }

    ~MeshStreamScope()
    {
        if (token_ != 0)
        {
            MioPan_RendererAbortMeshStream(token_);
        }
    }

    unsigned int token() const
    {
        return token_;
    }

    explicit operator bool() const
    {
        return token_ != 0;
    }

    void Commit()
    {
        if (token_ != 0)
        {
            MioPan_RendererCommitMeshStream(token_);
            token_ = 0;
        }
    }

private:
    unsigned int token_;
};

void PushPresetColor(std::vector<float> &rgba,
                     const float *color,
                     int index)
{
    if (g_dbg_flatcolor)
    {
        /* one distinct opaque colour per block, so each body part is
         * identifiable and nothing can be hidden by texture alpha */
        static const float pal[8][3] = {
            {1.0f, 0.2f, 0.2f}, {0.2f, 1.0f, 0.2f}, {0.3f, 0.5f, 1.0f},
            {1.0f, 1.0f, 0.2f}, {1.0f, 0.3f, 1.0f}, {0.2f, 1.0f, 1.0f},
            {1.0f, 0.6f, 0.1f}, {1.0f, 1.0f, 1.0f},
        };
        int b = g_dbgBlockId < 0 ? 7 : (g_dbgBlockId % 8);

        rgba.push_back(pal[b][0]);
        rgba.push_back(pal[b][1]);
        rgba.push_back(pal[b][2]);
        rgba.push_back(1.0f);
        return;
    }

    const float *preset_color = color;
    if (preset_color != nullptr)
    {
        const int offset = index * 4;
        rgba.push_back(preset_color[offset + 0]);
        rgba.push_back(preset_color[offset + 1]);
        rgba.push_back(preset_color[offset + 2]);
        /* The material alpha BuildPresetVertexColors() took from the VU1
         * material packet.  A preset mesh is lit by the same directional
         * block a runtime one is, so it carries alpha the same way -- which
         * is what lets the placed effect models ManmdlSetAlpha() drives fade
         * as well as the characters. */
        rgba.push_back(preset_color[offset + 3]);
    }
    else
    {
        rgba.push_back(1.0f);
        rgba.push_back(1.0f);
        rgba.push_back(1.0f);
        rgba.push_back(1.0f);
    }
}

void AppendTriangleStripIndices(std::vector<unsigned int> &indices,
                                unsigned int first_vertex,
                                int vertex_count)
{
    if (vertex_count < 3)
    {
        return;
    }

    indices.reserve(indices.size() + (size_t)(vertex_count - 2) * 3);
    for (int j = 0; j < vertex_count - 2; j++)
    {
        const unsigned int a = first_vertex + (unsigned int)j;
        const unsigned int b = a + 1;
        const unsigned int c = a + 2;
        if ((j & 1) == 0)
        {
            indices.push_back(a);
            indices.push_back(b);
        }
        else
        {
            indices.push_back(b);
            indices.push_back(a);
        }
        indices.push_back(c);
    }
}

int PresetMeshStreamCapacity(SGDPROCUNITHEADER *vuvn,
                             SGDPROCUNITHEADER *mesh,
                             int num_mesh)
{
    if (vuvn == nullptr || mesh == nullptr || num_mesh <= 0)
    {
        return 0;
    }
    SGDVUMESHDATA_PRESET *mesh_data = (SGDVUMESHDATA_PRESET *)&mesh[1];
    if (mesh_data->sOffsetToPrim == 0)
    {
        return 0;
    }
    _SGDVUMESHCOLORDATA *color_data =
        (_SGDVUMESHCOLORDATA *)(&mesh->pNext + mesh_data->sOffsetToPrim);
    const int max_vertices = vuvn->VUVNDesc.sNumVertex;
    int unique_vertices = 0;
    int expanded_vertices = 0;
    for (int i = 0; i < num_mesh; i++)
    {
        _SGDVUMESHCOLORDATA *unpack =
            (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr((u_int *)color_data);
        if (unpack == nullptr)
        {
            break;
        }
        const int count = unpack->VifUnpack.NUM;
        if (count <= 0 || unique_vertices > max_vertices - count ||
            count > (std::numeric_limits<int>::max() / 3) + 2)
        {
            return 0;
        }
        const int strip_vertices = count >= 3 ? (count - 2) * 3 : 0;
        if (expanded_vertices > std::numeric_limits<int>::max() -
                                    strip_vertices)
        {
            return 0;
        }
        unique_vertices += count;
        expanded_vertices += strip_vertices;
        color_data =
            (_SGDVUMESHCOLORDATA *)&unpack->avColor[count];
    }
    return expanded_vertices;
}

void AppendPresetStripToStream(unsigned int stream,
                               SGDVUVNDATA_PRESET *vuvn_data,
                               const SGDVUVNDESC *vuvn_desc,
                               int mesh_type,
                               int strip_index,
                               int vertex_offset,
                               int vertex_count,
                               const SGDVUMESHST *st,
                               const float *rgba)
{
    if (stream == 0 || vuvn_data == nullptr || vuvn_desc == nullptr ||
        vertex_count < 3 || rgba == nullptr)
    {
        return;
    }
    const float *normal_base = (const float *)(vuvn_data->aui + 10);
    const float *position_base =
        normal_base + vuvn_desc->sNumNormal * 3;
    for (int triangle_index = 0; triangle_index < vertex_count - 2;
         triangle_index++)
    {
        int strip_indices[3] = {
            triangle_index, triangle_index + 1, triangle_index + 2};
        if ((triangle_index & 1) != 0)
        {
            std::swap(strip_indices[0], strip_indices[1]);
        }

        MioPanMeshVertexInput triangle[3]{};
        for (int corner = 0; corner < 3; corner++)
        {
            const int local_index = strip_indices[corner];
            const int absolute_index = vertex_offset + local_index;
            triangle[corner].position = mesh_type == iMT_2F
                ? position_base + (size_t)absolute_index * 3u
                : vuvn_data->avt2[absolute_index].vVertex;
            triangle[corner].normal = mesh_type == iMT_2F
                ? normal_base + (size_t)strip_index * 3u
                : vuvn_data->avt2[absolute_index].vNormal;
            triangle[corner].rgb = rgba + (size_t)local_index * 4u;
            triangle[corner].s = st != nullptr ? st[local_index].fS : 0.0f;
            triangle[corner].t = st != nullptr ? st[local_index].fT : 0.0f;
            triangle[corner].alpha = rgba[(size_t)local_index * 4u + 3u];
        }
        MioPan_RendererAppendMeshTriangle(stream, triangle);
    }
}

void DrawIndexedFallback(const sceGsTex0 *tex0,
                         const std::vector<float> &positions,
                         const std::vector<float> &normals,
                         const std::vector<float> &uv,
                         const std::vector<float> &rgba,
                         const std::vector<unsigned int> &indices,
                         const float *local_world,
                         const MioPanLightState *fragment_lights,
                         const void *texture = nullptr)
{
    const size_t vertex_count = positions.size() / 3;
    if (positions.size() != vertex_count * 3 ||
        normals.size() < vertex_count * 3 ||
        uv.size() < vertex_count * 2 || rgba.size() < vertex_count * 4 ||
        indices.empty() || (indices.size() % 3) != 0 ||
        indices.size() > (size_t)std::numeric_limits<int>::max())
    {
        return;
    }

    /* The old expansion discarded the whole mesh if any index was invalid,
     * so validate transactionally before appending anything to g_vertices. */
    for (unsigned int index : indices)
    {
        if ((size_t)index >= vertex_count)
        {
            return;
        }
    }

    MeshStreamScope stream(tex0, (int)indices.size(), local_world,
                           fragment_lights, texture);
    if (!stream)
    {
        return;
    }
    for (size_t triangle_index = 0; triangle_index < indices.size();
         triangle_index += 3)
    {
        MioPanMeshVertexInput triangle[3]{};
        for (int corner = 0; corner < 3; corner++)
        {
            const size_t index = indices[triangle_index + (size_t)corner];
            triangle[corner].position = &positions[index * 3];
            triangle[corner].normal = &normals[index * 3];
            triangle[corner].rgb = &rgba[index * 4];
            triangle[corner].s = uv[index * 2 + 0];
            triangle[corner].t = uv[index * 2 + 1];
            triangle[corner].alpha = rgba[index * 4 + 3];
        }
        MioPan_RendererAppendMeshTriangle(stream.token(), triangle);
    }
    stream.Commit();
}

bool BuildPresetStaticGeometry(SGDPROCUNITHEADER *vuvn,
                               SGDPROCUNITHEADER *mesh,
                               int mesh_type,
                               int num_mesh,
                               std::vector<float> &positions,
                               std::vector<float> &normals,
                               std::vector<float> &uv,
                               std::vector<unsigned int> &indices,
                               int *out_vertex_count)
{
    SGDVUVNDESC *vuvn_desc = &vuvn->VUVNDesc;
    SGDVUVNDATA_PRESET *vuvn_data = (SGDVUVNDATA_PRESET *)&vuvn[1];
    SGDVUMESHDATA_PRESET *mesh_data = (SGDVUMESHDATA_PRESET *)&mesh[1];
    if (mesh_data->sOffsetToPrim == 0)
    {
        return false;
    }

    SGDVUMESHSTDATA *st_data = mesh_data->sOffsetToST != 0
        ? (SGDVUMESHSTDATA *)((uintptr_t)&mesh->VUVNDesc +
                              (intptr_t)mesh_data->sOffsetToST * 4 + 4)
        : nullptr;
    _SGDVUMESHCOLORDATA *color_data =
        (_SGDVUMESHCOLORDATA *)(&mesh->pNext + mesh_data->sOffsetToPrim);
    const float *normal_base = (const float *)(vuvn_data->aui + 10);
    const float *position_base =
        normal_base + vuvn_desc->sNumNormal * 3;

    positions.clear();
    normals.clear();
    uv.clear();
    indices.clear();
    positions.reserve((size_t)vuvn_desc->sNumVertex * 3);
    normals.reserve((size_t)vuvn_desc->sNumVertex * 3);
    uv.reserve((size_t)vuvn_desc->sNumVertex * 2);
    indices.reserve((size_t)vuvn_desc->sNumVertex * 3);

    int vertex_offset = 0;
    for (int i = 0; i < num_mesh; i++)
    {
        _SGDVUMESHCOLORDATA *unpack_color =
            (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr((u_int *)color_data);
        if (unpack_color == nullptr)
        {
            break;
        }

        const int vertex_count = unpack_color->VifUnpack.NUM;
        if (vertex_count <= 0 || vertex_offset < 0 ||
            vertex_offset + vertex_count > vuvn_desc->sNumVertex ||
            vertex_offset + vertex_count > kMeshBufferVertexLimit)
        {
            break;
        }

        SGDVUMESHST *st = st_data != nullptr ? st_data->astData : nullptr;
        FixUV(st, vertex_count);
        for (int j = 0; j < vertex_count; j++)
        {
            const float *position = mesh_type == iMT_2F
                ? position_base + (vertex_offset + j) * 3
                : vuvn_data->avt2[vertex_offset + j].vVertex;
            const float *normal = mesh_type == iMT_2F
                ? normal_base + i * 3
                : vuvn_data->avt2[vertex_offset + j].vNormal;
            positions.push_back(position[0]);
            positions.push_back(position[1]);
            positions.push_back(position[2]);
            normals.push_back(normal[0]);
            normals.push_back(normal[1]);
            normals.push_back(normal[2]);
            uv.push_back(st != nullptr ? st[j].fS : 0.0f);
            uv.push_back(st != nullptr ? st[j].fT : 0.0f);
        }
        AppendTriangleStripIndices(indices, (unsigned int)vertex_offset,
                                   vertex_count);

        vertex_offset += vertex_count;
        if (st_data != nullptr)
        {
            st_data = (SGDVUMESHSTDATA *)&st_data->astData[vertex_count];
        }
        color_data =
            (_SGDVUMESHCOLORDATA *)&unpack_color->avColor[vertex_count];
    }

    if (out_vertex_count != nullptr)
    {
        *out_vertex_count = vertex_offset;
    }
    return vertex_offset > 0 &&
           positions.size() / 3 == (size_t)vertex_offset &&
           normals.size() / 3 == (size_t)vertex_offset &&
           uv.size() / 2 == (size_t)vertex_offset && !indices.empty();
}

/* ==========================================================================
 *  Realtime per-vertex lighting.
 *
 *  On the PS2 the VU1 microprogram lit every vertex it transformed and wrote
 *  the result into the GIF packet's colour register, which the GS then
 *  MODULATEd with the texture.  The host has no VU1, so the runtime path used
 *  to submit flat white and characters came out unshaded.
 *
 *  The same per-vertex colour is produced here instead, through the engine's
 *  own CPU mirror of that kernel -- gra3dCalcVertexColor(), which the
 *  prelighting pass in gra3dSGD.c already uses to bake room geometry.  The
 *  result travels to the GPU in the vertex colour stream, where the fragment
 *  shader's texture * colour is the GS modulate, exactly as on hardware.
 *
 *  RUNTIME_VERTEX_COLORS holds one colour per VUVN vertex rather than per
 *  emitted triangle vertex: a strip visits each vertex about three times, and
 *  the lighting only depends on the vertex.
 * ======================================================================== */

/* Colours indexed by VUVN vertex id, already scaled to the shader's 0..1.
 * Empty when lighting is off or the block carries no usable normals, which the
 * push helpers read as "submit white".
 *
 * Four floats per vertex, not three: the fourth is the alpha the VU1's
 * directional block emits from DIRCOLDIF[0].w, i.e. the material alpha
 * ManmdlSetAlpha() drives.  It rides with the colour because that is where the
 * microcode produces it, and because carrying it separately would have to
 * assume one material per submission -- true today, but not something the
 * walker guarantees. */
using RUNTIME_VERTEX_COLORS = std::vector<float>;

const float *RuntimeVertexColor(const RUNTIME_VERTEX_COLORS &colors, int index)
{
    if (index < 0 || (size_t)(index + 1) * 4 > colors.size())
    {
        return nullptr;
    }

    return &colors[(size_t)index * 4];
}

/* Installs a local->world matrix for the duration of a lighting pass and puts
 * the previous one back.  gra3dCalcVertexColor() lights through GRA3DTS_WORLD;
 * the realtime walker never maintains that transform (it carries the matrix in
 * its coordinate/light packet instead, which is why GetHostRuntimeMeshTransform
 * exists in gra3dSGD.c), and restoring it keeps this off engine state. */
struct HostWorldTransform
{
    float saved[4][4];

    explicit HostWorldTransform(const float *local_world)
    {
        std::memcpy(saved, &gra3dGetTransformRef(GRA3DTS_WORLD), sizeof(saved));
        gra3dSetTransform(GRA3DTS_WORLD, (float (*)[4])local_world);
    }

    ~HostWorldTransform()
    {
        gra3dSetTransform(GRA3DTS_WORLD, saved);
    }
};

/* Expand the engine's three-lane VU1 snapshot into the renderer's wide block.
 *
 * A field-for-field copy, not a memcpy: the two layouts diverged when the
 * positional arrays were widened past the VU1's three lanes (see
 * MIOPAN_VU1_MAX_LANES).  The scalar per-lane terms -- bTimes, and the spot
 * cone pair -- move from three parallel float4s into one float4 per lane,
 * which is what lets a lane be addressed with a single index in the shader.
 *
 * Lanes beyond the snapshot's three are left zeroed and `counts` bounds the
 * loops, so this reproduces the hardware exactly; widening the SET of lights is
 * BuildWideLightLanes()'s job, not this one. */
void ExpandVu1Snapshot(MioPanLightState *state,
                       const GRA3DVU1LIGHTSNAPSHOT &vu)
{
    std::memset(state, 0, sizeof(*state));

    for (int c = 0; c < 4; c++)
    {
        state->config[c]  = vu.aiConfig[c];
        state->ambient[c] = vu.vAmbient[c];
    }
    state->counts[0] = 3;
    state->counts[1] = 3;

    for (int i = 0; i < 3; i++)
    {
        for (int c = 0; c < 4; c++)
        {
            state->directional_diffuse_dir[i][c]  = vu.avDirLightDif[i][c];
            state->directional_specular_dir[i][c] = vu.avDirLightSpc[i][c];
            state->directional_diffuse[i][c]      = vu.avDirColDif[i][c];
            state->directional_specular[i][c]     = vu.avDirColSpc[i][c];

            state->spot_position[i][c]  = vu.avSpotPos[i][c];
            state->spot_direction[i][c] = vu.avSpotDir[i][c];
            state->spot_diffuse[i][c]   = vu.avSpotColDif[i][c];
            state->spot_specular[i][c]  = vu.avSpotColSpc[i][c];

            state->point_position[i][c] = vu.avPointPos[i][c];
            state->point_diffuse[i][c]  = vu.avPointColDif[i][c];
            state->point_specular[i][c] = vu.avPointColSpc[i][c];
        }

        state->spot_params[i][0]  = vu.vSpotBTimes[i];
        state->spot_params[i][1]  = vu.vSpotIntens[i];
        state->spot_params[i][2]  = vu.vSpotIntensB[i];
        state->point_params[i][0] = vu.vPointBTimes[i];
    }
}

/* Decide where this draw's lighting is evaluated, and hand the fragment stage
 * the light image if any of it belongs there.  Returns the MIOPAN_VU1_TERM_*
 * mask the shader will run, or 0 for "the CPU does all of it" -- which is what
 * MIOPAN_LIGHTING_VERTEX, the debug overrides and a disabled spot type all come
 * out as.
 *
 * This is the ONE place the lighting mode is read.  Everything downstream keys
 * off the returned mask, so a draw cannot end up with the CPU and the GPU both
 * adding the same term, or neither adding it.
 *
 * Same source as the vertex path -- gra3dSnapshotVu1Lighting() -- so the two
 * evaluate one law rather than two, and the packing is the microcode's: an
 * unused lane carries a zero colour and a zero bTimes and so contributes
 * nothing. */
int BuildFragmentLightState(MioPanLightState *state,
                            const float *local_world)
{
    if (state == nullptr || g_dbg_nolighting || g_dbg_flatcolor)
    {
        return 0;
    }

    const int mode = MioPan_RendererGetLightingMode();
    if (mode != MIOPAN_LIGHTING_FRAGMENT &&
        mode != MIOPAN_LIGHTING_FRAGMENT_ALL)
    {
        return 0;
    }

    GRA3DVU1LIGHTSNAPSHOT vu{};
    gra3dSnapshotVu1Lighting(&vu);

    int terms;
    if (mode == MIOPAN_LIGHTING_FRAGMENT_ALL)
    {
        /* The shader ANDs SPOT and POINT against the type enables in config.x
         * itself, exactly as gra3dCalcVu1VertexColor() does, so the mask stays
         * unconditional here.  Directional has no type enable -- on hardware
         * its block is part of the vertex kernel rather than a callee. */
        terms = MIOPAN_VU1_TERM_ALL;
    }
    else
    {
        /* Spot only, and the type enable is a hard gate rather than a lane
         * mask: with the type disabled the microcode never runs CalcIntens at
         * all and VU1 memory keeps stale lights, so fall back to the vertex
         * path for the whole mesh rather than adding a term the hardware would
         * not. */
        if ((vu.aiConfig[0] & 1) == 0)
        {
            return 0;
        }
        terms = MIOPAN_VU1_TERM_SPOT;
    }

    ExpandVu1Snapshot(state, vu);
    state->config[2] = terms;

    /* Widen whichever positional types the shader is about to evaluate, past
     * the three lanes _SelectLightByType() left in the snapshot.  Anything the
     * CPU still owns keeps the engine's own three, because the CPU pass reads
     * them through gra3dCalcVu1VertexColor() and that is a three-lane path.
     *
     * The reference position is the mesh's world origin -- row 3 of its
     * local->world -- which stands in for the bounding-box centre the engine
     * ranks against.  It only matters if more lights are enabled than there are
     * lanes; see the note on gra3dCalcVu1WideLanes(). */
    {
        static const float kOrigin[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const float *ref = local_world != nullptr ? &local_world[12] : kOrigin;
        GRA3DVU1LANE lanes[MIOPAN_VU1_MAX_LANES];

        if ((terms & MIOPAN_VU1_TERM_SPOT) != 0)
        {
            const int n = gra3dCalcVu1WideLanes(lanes, MIOPAN_VU1_MAX_LANES,
                                                G3DLIGHT_SPOT, ref);
            state->counts[0] = n;
            for (int i = 0; i < n; i++)
            {
                std::memcpy(state->spot_position[i], lanes[i].vPosition,
                            sizeof(state->spot_position[i]));
                std::memcpy(state->spot_direction[i], lanes[i].vDirection,
                            sizeof(state->spot_direction[i]));
                std::memcpy(state->spot_diffuse[i], lanes[i].vColDif,
                            sizeof(state->spot_diffuse[i]));
                std::memcpy(state->spot_specular[i], lanes[i].vColSpc,
                            sizeof(state->spot_specular[i]));
                std::memcpy(state->spot_params[i], lanes[i].vParams,
                            sizeof(state->spot_params[i]));
            }
            for (int i = n; i < MIOPAN_VU1_MAX_LANES; i++)
            {
                std::memset(state->spot_position[i], 0,
                            sizeof(state->spot_position[i]));
                std::memset(state->spot_direction[i], 0,
                            sizeof(state->spot_direction[i]));
                std::memset(state->spot_diffuse[i], 0,
                            sizeof(state->spot_diffuse[i]));
                std::memset(state->spot_specular[i], 0,
                            sizeof(state->spot_specular[i]));
                std::memset(state->spot_params[i], 0,
                            sizeof(state->spot_params[i]));
            }
        }

        if ((terms & MIOPAN_VU1_TERM_POINT) != 0)
        {
            const int n = gra3dCalcVu1WideLanes(lanes, MIOPAN_VU1_MAX_LANES,
                                                G3DLIGHT_POINT, ref);
            state->counts[1] = n;
            for (int i = 0; i < n; i++)
            {
                std::memcpy(state->point_position[i], lanes[i].vPosition,
                            sizeof(state->point_position[i]));
                std::memcpy(state->point_diffuse[i], lanes[i].vColDif,
                            sizeof(state->point_diffuse[i]));
                std::memcpy(state->point_specular[i], lanes[i].vColSpc,
                            sizeof(state->point_specular[i]));
                std::memcpy(state->point_params[i], lanes[i].vParams,
                            sizeof(state->point_params[i]));
            }
            for (int i = n; i < MIOPAN_VU1_MAX_LANES; i++)
            {
                std::memset(state->point_position[i], 0,
                            sizeof(state->point_position[i]));
                std::memset(state->point_diffuse[i], 0,
                            sizeof(state->point_diffuse[i]));
                std::memset(state->point_specular[i], 0,
                            sizeof(state->point_specular[i]));
                std::memset(state->point_params[i], 0,
                            sizeof(state->point_params[i]));
            }
        }
    }

    return terms;
}

/* Take out of a CPU-side snapshot whatever the fragment stage is going to add,
 * so the two halves compose to exactly one evaluation of the light image.
 *
 * Clearing the light TYPE enable is the faithful way to say it: the microcode
 * skips a disabled type's whole kernel, and gra3dCalcVu1VertexColor() gates on
 * the same bit -- so a masked snapshot is indistinguishable from a draw where
 * the type was never on.
 *
 * This replaced HostSpotLightsSuppressed(), which disabled the g3d core's spot
 * SLOTS and called g3dApplyLight().  That could not have worked.  The CPU pass
 * reads gra3d's s_Vu1LightImage / s_Vu1MaterialSpotImage, whose only writer is
 * g3dSetVu1LightData() -- called from the SGD walker's COORDINATE-block handler
 * (_SetCoordData, itself gated on CheckCoordCache) well before any mesh block,
 * and never between the suppression and the snapshot.  g3dApplyLight() writes
 * s_pObject's own bank, which nothing on this path reads.  So the spots
 * survived the suppression and were added twice, once per vertex and once per
 * pixel.  Masking the local copy is correct by construction and has no engine
 * side effects at all, which is what the old class was trying to achieve. */
void MaskCpuLightTerms(GRA3DVU1LIGHTSNAPSHOT *vu, int fragment_terms)
{
    if ((fragment_terms & MIOPAN_VU1_TERM_SPOT) != 0)
    {
        vu->aiConfig[0] &= ~1;
    }
    if ((fragment_terms & MIOPAN_VU1_TERM_POINT) != 0)
    {
        vu->aiConfig[0] &= ~2;
    }
    /* Directional has no type bit -- its block is part of the vertex kernel
     * rather than a callee -- so a split that puts it on the GPU skips the CPU
     * pass outright instead of masking it.  Both callers test for that first. */
}

/* Snapshot the VU1 light image for the per-VERTEX path -- the one the four
 * microprograms in vu1/ actually read, not g3dCalcVertexColor()'s prelight
 * mirror.  The graph walker keeps changing that state (a coordinate block
 * refreshes the lights, a material block the colours), so each queued draw
 * owns its copy.
 *
 * Three lanes, because this path exists to reproduce the hardware; widening
 * the set is the fragment path's business. */
bool BuildAnimatedVertexLightState(MioPanLightState *state)
{
    if (state == nullptr)
    {
        return false;
    }
    std::memset(state, 0, sizeof(*state));

    if (MioPan_RendererGetAnimatedLightingBackend() !=
        MIOPAN_ANIMATED_LIGHTING_GPU)
    {
        return false;
    }

    /* These modes intentionally submit white.  They still use the indexed
     * animated fast path; config.y bit 0 stays clear and the shader bypasses
     * the light block. */
    if (g_dbg_nolighting || g_dbg_flatcolor)
    {
        return true;
    }

    /* No monotone flag: _MakeColorToMonotone() runs inside
     * gra3dCalcVu1MaterialData*(), so the colours in this snapshot are already
     * grey when monotone draw is on.  Applying it again in the shader would be
     * a second average of an already-flat triple -- harmless, but it would
     * imply the VU1 does something it does not. */
    GRA3DVU1LIGHTSNAPSHOT core{};
    gra3dSnapshotVu1Lighting(&core);

    ExpandVu1Snapshot(state, core);
    return true;
}


const float *RuntimeVectorPosition(const _VECTORDATA *vector_data,
                                   const float *post_positions, int index);

const float *RuntimeVectorNormal(const _VECTORDATA *vector_data,
                                 const float *post_normals,
                                 int index)
{
    const sceVu0FVECTOR *normal;

    if (index < 0)
    {
        return nullptr;
    }

    /* Same pairing as RuntimeVectorPosition: once SetVUVNDataPost has skinned
     * the block, the packet's normals are the ones the VU consumed. */
    if (post_normals != nullptr)
    {
        return &post_normals[(size_t)index * 4];
    }

    if (vector_data == nullptr)
    {
        return nullptr;
    }

    normal = vector_data[index].vAddress.pNormal;
    if (normal == nullptr)
    {
        return nullptr;
    }

    return (*normal);
}

/*
 * Light `count` vertices into `colors`.
 *
 * gra3dCalcVertexColor() lights through GRA3DTS_WORLD, so the caller's
 * local->world matrix is installed for the call and put back afterwards.  The
 * realtime walker does not otherwise maintain that transform -- it carries the
 * matrix in its coordinate/light packet instead, which is why
 * GetHostRuntimeMeshTransform() in gra3dSGD.c exists -- but restoring it keeps
 * this from being a side effect on engine state.
 */
void BuildRuntimeVertexColors(RUNTIME_VERTEX_COLORS &colors,
                              int count,
                              const _VECTORDATA *vector_data,
                              const SGDVUVNDATA_PRESET *preloaded,
                              const float *post_positions,
                              const float *post_normals,
                              const float *local_world,
                              const MioPanLightState *fragment_lights)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_LIGHTING);
    colors.clear();

    if (g_dbg_nolighting || g_dbg_flatcolor || count <= 0 ||
        local_world == nullptr)
    {
        return;
    }

    /* MIOPAN_LIGHTING_FRAGMENT_ALL: the shader runs every kernel per pixel, so
     * this pass has nothing left to compute -- no world transform, no snapshot,
     * no per-vertex kernel.  A runtime VUVN packet carries no colour of its
     * own, so the seed the shader starts from is black; the only thing that
     * still has to come from here is alpha, which is never lit (DIRCOLDIF[0].w,
     * the term ManmdlSetAlpha() drives to fade a ghost out). */
    if (fragment_lights != nullptr &&
        (fragment_lights->config[2] & MIOPAN_VU1_TERM_DIRECTIONAL) != 0)
    {
        const float alpha =
            Ps2AlphaFloat(fragment_lights->directional_diffuse[0][3]);

        colors.assign((size_t)count * 4, 0.0f);
        for (int i = 0; i < count; i++)
        {
            colors[(size_t)i * 4 + 3] = alpha;
        }
        return;
    }

    HostWorldTransform world(local_world);

    /* The VU1's own kernels, from vu1/ff2_00.vsm -- not the prelight model
     * gra3dCalcVertexColor() implements.  One snapshot for the whole mesh:
     * the walker cannot change the light image between vertices. */
    GRA3DVU1LIGHTSNAPSHOT vu{};
    gra3dSnapshotVu1Lighting(&vu);
    MaskCpuLightTerms(&vu, fragment_lights != nullptr
                               ? fragment_lights->config[2] : 0);

    colors.resize((size_t)count * 4, 1.0f);

    for (int i = 0; i < count; i++)
    {
        const float *position;
        const float *normal;
        float        vVertex[4];
        float        vNormal[4];
        float        vColor[4];

        if (preloaded != nullptr)
        {
            position = preloaded->avt2[i].vVertex;
            normal   = preloaded->avt2[i].vNormal;
        }
        else
        {
            position = RuntimeVectorPosition(vector_data, post_positions, i);
            normal   = RuntimeVectorNormal(vector_data, post_normals, i);
        }

        if (position == nullptr || normal == nullptr)
        {
            continue;                       /* leave this vertex white */
        }

        vVertex[0] = position[0];
        vVertex[1] = position[1];
        vVertex[2] = position[2];
        vVertex[3] = 1.0f;
        vNormal[0] = normal[0];
        vNormal[1] = normal[1];
        vNormal[2] = normal[2];
        vNormal[3] = 0.0f;

        /* The realtime packet carries no per-vertex colour of its own; the VU1
         * produced the whole thing, so the accumulation starts from black. */
        gra3dCalcVu1VertexColor(vColor, &vu, vVertex, vNormal, nullptr);

        colors[(size_t)i * 4 + 0] = Ps2ColorFloat(vColor[0]);
        colors[(size_t)i * 4 + 1] = Ps2ColorFloat(vColor[1]);
        colors[(size_t)i * 4 + 2] = Ps2ColorFloat(vColor[2]);
        colors[(size_t)i * 4 + 3] = Ps2AlphaFloat(vColor[3]);
    }
}

/*
 * Colours for one preset sub-mesh: the baked vertex colour, plus the live
 * lighting the PS2 added on top of it.
 *
 * A preset mesh is "pre-lit", but that is not the whole story on hardware.  The
 * preset walker re-selects lights per bounding box (SelectLight), uploads a
 * light packet per coordinate (_SetCoordData) and a point/spot material packet
 * per material (SetMaterialDataVU) -- all of it pointless if the VU1 only
 * replayed the baked colour.  What it is for is the player's torch:
 * MapDrawRoomOne() turns *every* light off and re-enables only the flashlight
 * before drawing a room, precisely because the static lights are already baked
 * into the vertices.  Without this pass the beam does not exist.
 *
 * The composition is the engine's own -- gra3dCalcVertexColor() with the baked
 * colour as the source, which is exactly how SetPreRenderMeshData() folds one
 * light batch onto the previous one during prelighting.
 */
void BuildPresetVertexColors(RUNTIME_VERTEX_COLORS &colors,
                             SGDVUVNDATA_PRESET *vuvn_data,
                             const SGDVUVNDESC *vuvn_desc,
                             int mesh_type,
                             int strip_index,
                             int vertex_offset,
                             int vertex_count,
                             const VECTOR3 *baked,
                             const float *local_world,
                             const MioPanLightState *fragment_lights)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_LIGHTING);
    const float *c = (const float *)baked;

    colors.clear();

    if (vertex_count <= 0)
    {
        return;
    }
    if (g_dbg_flatcolor)
    {
        /* PushPresetColor emits the per-block palette directly. */
        return;
    }

    /* The baked colour is three components; alpha stays at the resize default
     * until the lighting pass below reads the material's own out of the VU1
     * material packet. */
    colors.resize((size_t)vertex_count * 4, 1.0f);

    for (int i = 0; i < vertex_count; i++)
    {
        colors[(size_t)i * 4 + 0] = c != nullptr ? Ps2ColorFloat(c[i * 3 + 0]) : 1.0f;
        colors[(size_t)i * 4 + 1] = c != nullptr ? Ps2ColorFloat(c[i * 3 + 1]) : 1.0f;
        colors[(size_t)i * 4 + 2] = c != nullptr ? Ps2ColorFloat(c[i * 3 + 2]) : 1.0f;
    }

    if (g_dbg_nolighting || local_world == nullptr ||
        vuvn_data == nullptr || vuvn_desc == nullptr)
    {
        return;                                  /* baked colour only */
    }

    /* MIOPAN_LIGHTING_FRAGMENT_ALL: the shader seeds from this colour and runs
     * every kernel itself, so all that is left here is to hand over the seed --
     * no world transform, no snapshot, no per-vertex kernel.  Deleting that
     * loop is the point of the mode; it is the whole MESH_LIGHTING profile
     * zone.
     *
     * Two things still have to happen on this side.  The monotone collapse is
     * applied to the SOURCE rather than to the result (see the PORT DEVIATION
     * note in gra3dCalcVu1VertexColor), so moving it into the shader would
     * change what it means.  And alpha is not lit at all: it is DIRCOLDIF[0].w,
     * which the kernel would otherwise have copied straight through. */
    if (fragment_lights != nullptr &&
        (fragment_lights->config[2] & MIOPAN_VU1_TERM_DIRECTIONAL) != 0)
    {
        const float alpha =
            Ps2AlphaFloat(fragment_lights->directional_diffuse[0][3]);
        const bool monotone = gra3dIsMonotoneDrawEnable() != 0;

        for (int i = 0; i < vertex_count; i++)
        {
            /* A NULL colour record seeds from black, which is what a runtime
             * VUVN packet carries -- not from the white the unlit fallback
             * above writes. */
            float vSeed[3] = {
                c != nullptr ? c[i * 3 + 0] : 0.0f,
                c != nullptr ? c[i * 3 + 1] : 0.0f,
                c != nullptr ? c[i * 3 + 2] : 0.0f,
            };
            if (monotone)
            {
                const float fAverage = (vSeed[0] + vSeed[1] + vSeed[2]) / 3.0f;
                vSeed[0] = vSeed[1] = vSeed[2] = fAverage;
            }

            colors[(size_t)i * 4 + 0] = Ps2ColorFloat(vSeed[0]);
            colors[(size_t)i * 4 + 1] = Ps2ColorFloat(vSeed[1]);
            colors[(size_t)i * 4 + 2] = Ps2ColorFloat(vSeed[2]);
            colors[(size_t)i * 4 + 3] = alpha;
        }
        return;
    }

    HostWorldTransform world(local_world);

    /* A preset draw is still a VU1 draw, and it runs the SAME three kernels:
     * RotTransPersInner (the TYPE0 vertex kernel) carries the directional
     * block verbatim, and CalcEL0EX reaches CalcIntens/CalcPoint through the
     * same vi13 dispatch TYPE2 uses.  CalcIntens and CalcPoint are byte-
     * identical across ff2_00/01/02.  Only the prelight bake runs on the EE
     * with the other law. */
    GRA3DVU1LIGHTSNAPSHOT vu{};
    gra3dSnapshotVu1Lighting(&vu);
    MaskCpuLightTerms(&vu, fragment_lights != nullptr
                               ? fragment_lights->config[2] : 0);


    for (int i = 0; i < vertex_count; i++)
    {
        const VECTOR3 *position;
        const VECTOR3 *normal;
        int            abs_index = vertex_offset + i;
        float          vVertex[4];
        float          vNormal[4];
        float          vSource[4];
        float          vColor[4];

        if (mesh_type == iMT_2F)
        {
            /* Flat shaded: the block is one normal per strip followed by the
             * positions, both packed at 12 bytes -- not the interleaved avt2[]
             * form.  Same addressing as SetPreRenderMeshData(). */
            normal   = &vuvn_data->vt2f.avNormal[strip_index];
            position = &vuvn_data->vt2f.avNormal[abs_index + vuvn_desc->sNumNormal];
        }
        else
        {
            position = &vuvn_data->avt2[abs_index].vVertex;
            normal   = &vuvn_data->avt2[abs_index].vNormal;
        }

        vVertex[0] = (*position)[0];
        vVertex[1] = (*position)[1];
        vVertex[2] = (*position)[2];
        vVertex[3] = 1.0f;
        vNormal[0] = (*normal)[0];
        vNormal[1] = (*normal)[1];
        vNormal[2] = (*normal)[2];
        vNormal[3] = 0.0f;

        vSource[0] = c != nullptr ? c[i * 3 + 0] : 0.0f;
        vSource[1] = c != nullptr ? c[i * 3 + 1] : 0.0f;
        vSource[2] = c != nullptr ? c[i * 3 + 2] : 0.0f;
        vSource[3] = 0.0f;

        gra3dCalcVu1VertexColor(vColor, &vu, vVertex, vNormal, vSource);

        colors[(size_t)i * 4 + 0] = Ps2ColorFloat(vColor[0]);
        colors[(size_t)i * 4 + 1] = Ps2ColorFloat(vColor[1]);
        colors[(size_t)i * 4 + 2] = Ps2ColorFloat(vColor[2]);
        colors[(size_t)i * 4 + 3] = Ps2AlphaFloat(vColor[3]);
    }
}

bool PushRuntimeStripTriangle(unsigned int stream,
                              const float *p0,
                              const float *p1,
                              const float *p2,
                              const float *n0,
                              const float *n1,
                              const float *n2,
                              const float *c0,
                              const float *c1,
                              const float *c2,
                              const SGDVUMESHST *st,
                              int i0,
                              int i1,
                              int i2)
{
    /* The old three-vector path rolled the complete triangle back if any
     * address-table entry was null.  Keep that distinction from a nonfinite
     * position, which reaches the renderer and is counted as clipped. */
    if (p0 == nullptr || p1 == nullptr || p2 == nullptr)
    {
        return false;
    }

    const float *positions[3] = {p0, p1, p2};
    const float *normals[3] = {n0, n1, n2};
    const float *colors[3] = {c0, c1, c2};
    const int indices[3] = {i0, i1, i2};
    MioPanMeshVertexInput triangle[3]{};
    for (int corner = 0; corner < 3; corner++)
    {
        const int index = indices[corner];
        triangle[corner].position = positions[corner];
        /* Only read when the draw is fragment-lit, where it is octahedrally
         * packed into the spare UV pair.  A null one encodes as +Z, which is
         * what an unlit stream has always sent. */
        triangle[corner].normal = normals[corner];
        triangle[corner].rgb = colors[corner];
        triangle[corner].s = st != nullptr ? st[index].fS : 0.0f;
        triangle[corner].t = st != nullptr ? st[index].fT : 0.0f;
        /* Component 3 of the same record: the material alpha the VU1's
         * directional block emits.  A null colour record means lighting was
         * skipped for this block, which submits opaque white as it always
         * has. */
        triangle[corner].alpha =
            colors[corner] != nullptr ? colors[corner][3] : 1.0f;
    }
    MioPan_RendererAppendMeshTriangle(stream, triangle);
    return true;
}

const float *RuntimeVectorPosition(const _VECTORDATA *vector_data,
                                   const float *post_positions,
                                   int index)
{
    const sceVu0FVECTOR *vertex;

    if (index < 0)
    {
        return nullptr;
    }

    /*
     * SetVUVNDataPost has already resolved and, for weighted vectors,
     * skinned this vertex into the DMA work packet.  The VU consumes this
     * packet rather than the original self-relative address table.
     */
    if (post_positions != nullptr)
    {
        return &post_positions[(size_t)index * 4];
    }

    if (vector_data == nullptr)
    {
        return nullptr;
    }

    vertex = vector_data[index].vAddress.pVertex;
    if (vertex == nullptr)
    {
        return nullptr;
    }

    return (*vertex);
}

int RuntimeMeshStreamCapacity(SGDPROCUNITHEADER *vuvn,
                              SGDPROCUNITHEADER *mesh,
                              int *out_unique_vertices)
{
    if (out_unique_vertices != nullptr)
    {
        *out_unique_vertices = 0;
    }
    if (vuvn == nullptr || mesh == nullptr)
    {
        return 0;
    }

    const int num_mesh = mesh->VUMeshDesc.ucNumMesh;
    SGDVUMESHPOINTNUM *point_num = (SGDVUMESHPOINTNUM *)&mesh[4];
    const size_t max_vertices =
        (size_t)kMeshBufferVertexLimit -
        ((size_t)kMeshBufferVertexLimit % 3u);
    size_t capacity = 0;
    int vertex_offset = 0;
    for (int i = 0; i < num_mesh; i++)
    {
        const int vertex_count = (int)point_num[i].uiPointNum;
        if (vertex_count <= 0)
        {
            continue;
        }
        if (vertex_offset + vertex_count > vuvn->VUVNDesc.sNumVertex)
        {
            break;
        }
        if (vertex_count >= 3)
        {
            const size_t strip_vertices =
                (size_t)(vertex_count - 2) * 3u;
            if (strip_vertices > max_vertices - capacity)
            {
                capacity = max_vertices;
                break;
            }
            capacity += strip_vertices;
        }
        vertex_offset += vertex_count;
    }
    if (out_unique_vertices != nullptr)
    {
        *out_unique_vertices = vertex_offset;
    }
    return (int)capacity;
}

bool BuildRuntimeAnimatedGeometry(std::vector<float> &positions,
                                  std::vector<float> &normals,
                                  SGDPROCUNITHEADER *vuvn,
                                  int mesh_family,
                                  int vertex_count,
                                  const float *post_positions,
                                  const float *post_normals)
{
    positions.clear();
    normals.clear();
    if (vuvn == nullptr || vertex_count <= 0 ||
        vertex_count > vuvn->VUVNDesc.sNumVertex)
    {
        return false;
    }

    const bool preloaded = mesh_family == 0x80 || mesh_family == 0x82;
    const SGDVUVNDATA_PRESET *preloaded_data = preloaded
        ? (const SGDVUVNDATA_PRESET *)&vuvn[1] : nullptr;
    const _VECTORDATA *vector_data = preloaded
        ? nullptr : (const _VECTORDATA *)&vuvn[3];
    positions.reserve((size_t)vertex_count * 3);
    normals.reserve((size_t)vertex_count * 3);
    for (int i = 0; i < vertex_count; i++)
    {
        const float *position = preloaded
            ? preloaded_data->avt2[i].vVertex
            : RuntimeVectorPosition(vector_data, post_positions, i);
        const float *normal = preloaded
            ? preloaded_data->avt2[i].vNormal
            : RuntimeVectorNormal(vector_data, post_normals, i);
        if (position == nullptr || !std::isfinite(position[0]) ||
            !std::isfinite(position[1]) || !std::isfinite(position[2]) ||
            normal == nullptr || !std::isfinite(normal[0]) ||
            !std::isfinite(normal[1]) || !std::isfinite(normal[2]))
        {
            positions.clear();
            normals.clear();
            return false;
        }
        positions.push_back(position[0]);
        positions.push_back(position[1]);
        positions.push_back(position[2]);
        normals.push_back(normal[0]);
        normals.push_back(normal[1]);
        normals.push_back(normal[2]);
    }
    return true;
}

bool BuildRuntimeAnimatedTopology(SGDPROCUNITHEADER *vuvn,
                                  SGDPROCUNITHEADER *mesh,
                                  int expected_vertex_count,
                                  std::vector<float> &uv,
                                  std::vector<unsigned int> &indices)
{
    uv.clear();
    indices.clear();
    if (vuvn == nullptr || mesh == nullptr || expected_vertex_count <= 0)
    {
        return false;
    }

    const int num_mesh = mesh->VUMeshDesc.ucNumMesh;
    const bool textured = (mesh->VUMeshDesc.ucMeshType & 0x02) != 0;
    SGDVUMESHPOINTNUM *point_num = (SGDVUMESHPOINTNUM *)&mesh[4];
    SGDVUMESHSTREGSET *st_regset =
        (SGDVUMESHSTREGSET *)&point_num[num_mesh];
    SGDVUMESHSTDATA *st_data =
        (SGDVUMESHSTDATA *)&st_regset->auiVifCode[3];

    uv.reserve((size_t)expected_vertex_count * 2);
    indices.reserve((size_t)expected_vertex_count * 3);
    int vertex_offset = 0;
    size_t emitted_vertices = 0;
    for (int i = 0; i < num_mesh; i++)
    {
        const int strip_count = (int)point_num[i].uiPointNum;
        if (strip_count <= 0)
        {
            continue;
        }
        if (vertex_offset + strip_count > vuvn->VUVNDesc.sNumVertex ||
            (size_t)strip_count >
                ((size_t)kMeshBufferVertexLimit - emitted_vertices) / 3)
        {
            break;
        }

        SGDVUMESHST *st = textured && st_data != nullptr
            ? st_data->astData : nullptr;
        if (textured)
        {
            FixUV(st, strip_count);
        }
        for (int j = 0; j < strip_count; j++)
        {
            const float s = st != nullptr ? st[j].fS : 0.0f;
            const float t = st != nullptr ? st[j].fT : 0.0f;
            /* The direct stream intentionally forwards malformed UVs.  Do
             * not let the cache's defensive sanitization turn them into a
             * permanently different mesh; just keep this draw on the old
             * path instead. */
            if (!std::isfinite(s) || !std::isfinite(t))
            {
                uv.clear();
                indices.clear();
                return false;
            }
            uv.push_back(s);
            uv.push_back(t);
        }
        AppendTriangleStripIndices(indices, (unsigned int)vertex_offset,
                                   strip_count);
        if (strip_count >= 3)
        {
            emitted_vertices += (size_t)(strip_count - 2) * 3;
        }
        vertex_offset += strip_count;
        if (st_data != nullptr)
        {
            st_data = (SGDVUMESHSTDATA *)&st_data->astData[strip_count];
        }
    }

    return vertex_offset == expected_vertex_count &&
           uv.size() == (size_t)expected_vertex_count * 2 &&
           !indices.empty();
}

void AppendRuntimeIndexedMesh(unsigned int stream,
                              SGDPROCUNITHEADER *vuvn,
                              SGDPROCUNITHEADER *mesh,
                              const float *post_positions,
                              const float *post_normals,
                              const RUNTIME_VERTEX_COLORS &colors)
{
    _VECTORDATA       *vector_data;
    SGDVUMESHPOINTNUM *point_num;
    SGDVUMESHSTREGSET *st_regset;
    SGDVUMESHSTDATA   *st_data;
    int                num_mesh;
    int                vertex_offset;
    size_t             emitted_vertices;

    num_mesh = mesh->VUMeshDesc.ucNumMesh;
    vector_data = (_VECTORDATA *)&vuvn[3];
    point_num = (SGDVUMESHPOINTNUM *)&mesh[4];
    st_regset = (SGDVUMESHSTREGSET *)&point_num[num_mesh];
    st_data = (SGDVUMESHSTDATA *)&st_regset->auiVifCode[3];
    vertex_offset = 0;
    emitted_vertices = 0;

    for (int i = 0; i < num_mesh; i++)
    {
        SGDVUMESHST *st;
        int          textured = (mesh->VUMeshDesc.ucMeshType & 0x02) != 0;
        int          vertex_count = (int)point_num[i].uiPointNum;

        if (vertex_count <= 0)
        {
            continue;
        }
        if (vertex_offset + vertex_count > vuvn->VUVNDesc.sNumVertex ||
            (size_t)vertex_count >
                ((size_t)kMeshBufferVertexLimit - emitted_vertices) / 3)
        {
            break;
        }

        st = textured && st_data != nullptr ? st_data->astData : nullptr;
        if (textured)
        {
            FixUV(st, vertex_count);
        }

        if (vertex_count >= 3)
        {
            for (int j = 0; j < vertex_count - 2; j++)
            {
                int i0;
                int i1;
                int i2;

                if ((j & 1) == 0)
                {
                    i0 = j;
                    i1 = j + 1;
                    i2 = j + 2;
                }
                else
                {
                    i0 = j + 1;
                    i1 = j;
                    i2 = j + 2;
                }

                if (PushRuntimeStripTriangle(
                    stream,
                    RuntimeVectorPosition(vector_data, post_positions,
                                          vertex_offset + i0),
                    RuntimeVectorPosition(vector_data, post_positions,
                                          vertex_offset + i1),
                    RuntimeVectorPosition(vector_data, post_positions,
                                          vertex_offset + i2),
                    RuntimeVectorNormal(vector_data, post_normals,
                                        vertex_offset + i0),
                    RuntimeVectorNormal(vector_data, post_normals,
                                        vertex_offset + i1),
                    RuntimeVectorNormal(vector_data, post_normals,
                                        vertex_offset + i2),
                    RuntimeVertexColor(colors, vertex_offset + i0),
                    RuntimeVertexColor(colors, vertex_offset + i1),
                    RuntimeVertexColor(colors, vertex_offset + i2),
                    st, i0, i1, i2))
                {
                    emitted_vertices += 3;
                }
            }
        }

        vertex_offset += vertex_count;
        st_data = (SGDVUMESHSTDATA *)&st_data->astData[vertex_count];
    }
}

void AppendRuntimePreloadedMesh(unsigned int stream,
                                SGDPROCUNITHEADER *vuvn,
                                SGDPROCUNITHEADER *mesh,
                                const RUNTIME_VERTEX_COLORS &colors)
{
    SGDVUVNDATA_PRESET *vuvn_data;
    SGDVUMESHPOINTNUM  *point_num;
    SGDVUMESHSTREGSET  *st_regset;
    SGDVUMESHSTDATA    *st_data;
    int                 num_mesh;
    int                 vertex_offset;
    size_t              emitted_vertices;

    num_mesh = mesh->VUMeshDesc.ucNumMesh;
    vuvn_data = (SGDVUVNDATA_PRESET *)&vuvn[1];
    point_num = (SGDVUMESHPOINTNUM *)&mesh[4];
    st_regset = (SGDVUMESHSTREGSET *)&point_num[num_mesh];
    st_data = (SGDVUMESHSTDATA *)&st_regset->auiVifCode[3];
    vertex_offset = 0;
    emitted_vertices = 0;

    for (int i = 0; i < num_mesh; i++)
    {
        SGDVUMESHST *st;
        int          textured = (mesh->VUMeshDesc.ucMeshType & 0x02) != 0;
        int          vertex_count = (int)point_num[i].uiPointNum;

        if (vertex_count <= 0)
        {
            continue;
        }
        if (vertex_offset + vertex_count > vuvn->VUVNDesc.sNumVertex ||
            (size_t)vertex_count >
                ((size_t)kMeshBufferVertexLimit - emitted_vertices) / 3)
        {
            break;
        }

        st = textured && st_data != nullptr ? st_data->astData : nullptr;
        if (textured)
        {
            FixUV(st, vertex_count);
        }

        if (vertex_count >= 3)
        {
            for (int j = 0; j < vertex_count - 2; j++)
            {
                int i0;
                int i1;
                int i2;

                if ((j & 1) == 0)
                {
                    i0 = j;
                    i1 = j + 1;
                    i2 = j + 2;
                }
                else
                {
                    i0 = j + 1;
                    i1 = j;
                    i2 = j + 2;
                }

                if (PushRuntimeStripTriangle(
                    stream,
                    vuvn_data->avt2[vertex_offset + i0].vVertex,
                    vuvn_data->avt2[vertex_offset + i1].vVertex,
                    vuvn_data->avt2[vertex_offset + i2].vVertex,
                    vuvn_data->avt2[vertex_offset + i0].vNormal,
                    vuvn_data->avt2[vertex_offset + i1].vNormal,
                    vuvn_data->avt2[vertex_offset + i2].vNormal,
                    RuntimeVertexColor(colors, vertex_offset + i0),
                    RuntimeVertexColor(colors, vertex_offset + i1),
                    RuntimeVertexColor(colors, vertex_offset + i2),
                    st, i0, i1, i2))
                {
                    emitted_vertices += 3;
                }
            }
        }

        vertex_offset += vertex_count;
        st_data = (SGDVUMESHSTDATA *)&st_data->astData[vertex_count];
    }
}

/* ==========================================================================
 *  Resident preset meshes.
 *
 *  A preset (prelit) SGD -- a room, a piece of furniture, a door -- has
 *  geometry that never changes once it is loaded.  Nothing in the game writes
 *  a preset unit's positions or normals (cloth and MIME write the unique
 *  vertex pool, which only runtime meshes read), and the one thing that moves
 *  its UVs, gra3dChangeST(), announces itself through
 *  MioPan_Graph3dInvalidateSkinCache() before it edits anything.  So the first
 *  time a model is drawn, every one of its units is decoded into a single
 *  resident mesh -- back to back, in the order the walker visits them -- and
 *  from then on a unit's draw names its slice of that mesh rather than
 *  decoding it again.
 *
 *  Nothing else moves.  The walker still culls, selects lights and loads
 *  TRI2s; this bridge still scans each unit's TEX0 and TEST, lights its
 *  vertices and queues it in walk order with its own GS state.  What stops is
 *  the geometry itself travelling every frame -- and because consecutive units
 *  now reference consecutive slices, the renderer can fold a run of them that
 *  share all of that state into one draw.  A room's four or five hundred
 *  units come down to one draw per such run.
 *
 *  Colour still goes up every frame.  It carries the realtime light -- the
 *  flashlight is exactly what moves -- but at 16 bytes a vertex, against the
 *  streamed path's 144 bytes a triangle.
 *
 *  The decode is BuildPresetStaticGeometry(), the streamed path's own, so a
 *  resident unit is the streamed unit by construction; all that changes is
 *  where its vertices live.
 * ======================================================================== */
struct ResidentPresetUnit
{
    /* The VUVN latched when this unit was decoded.  A walker that hands over
     * a different one gets the unit streamed rather than trusted. */
    const SGDPROCUNITHEADER *vuvn;
    unsigned int first_vertex;
    unsigned int vertex_count;
    unsigned int first_index;
    unsigned int index_count;
};

/* One colour mode's worth of a model's resolved textures -- see "Resident
 * preset textures" below. */
struct ResidentTextureBinding
{
    bool complete = false;
    /* A TEX0 would not resolve, a draw named one outside the set, or the
     * model's own sends do not cover what its TEX0s read.  This model keeps
     * sending its textures to the GS in this mode from now on. */
    bool refused = false;
    std::unordered_map<uint64_t, const void *> textures;
};

/* A TRI2 or monotone TRI2 unit as LoadTRI2Files() hands it over: its first
 * header, and how many headers gra3dLoadTRI2FileToVRAM() sends from there. */
struct ResidentTri2Unit
{
    const void *head;
    int num_texture;
};

struct ResidentPresetModel
{
    /* Geometry: the resident mesh and each unit's slice of it. */
    bool geometry_tried = false;
    unsigned int mesh = 0;              /* renderer handle; 0 = stream it */
    unsigned long long built_frame = 0;
    std::unordered_map<const SGDPROCUNITHEADER *, ResidentPresetUnit> units;

    /* Textures: every TEX0 the model's own units carry, its TRI2 and
     * monotone TRI2 units, and what those TEX0s resolved to -- once for
     * normal draws, once for monotone ones, which upload the second kind over
     * the first. */
    std::vector<uint64_t> tex0_values;
    std::vector<ResidentTri2Unit> tri2_units;
    std::vector<ResidentTri2Unit> mono_units;
    /* Whether what this model sends in each colour mode covers every texel
     * and CLUT entry its TEX0s read.  Four furniture models send a CLUT and
     * nothing else, sampling texels their sibling sent just before. */
    bool self_covered[2] = {true, true};
    /* TEX0s a model with no TRI2 of its own drew while this one's were the
     * last sent -- the room's shadow-source model -- resolved alongside the
     * model's own whenever it captures. */
    std::vector<uint64_t> borrowed_tex0;
    ResidentTextureBinding textures[2];

    unsigned long long used_frame = 0;
};

bool HasTri2Head(const std::vector<ResidentTri2Unit> &units, const void *head)
{
    for (const ResidentTri2Unit &unit : units)
    {
        if (unit.head == head)
        {
            return true;
        }
    }
    return false;
}

/* The model sends nothing at all in this colour mode, so its draws sample
 * whatever the last TRI2s sent left in GS memory. */
bool SendsNoTri2(const ResidentPresetModel &model, int monotone)
{
    return model.tri2_units.empty() &&
           (monotone == 0 || model.mono_units.empty());
}

/* Keyed by the SGD's address, which is safe here for the reason it was not
 * for the old mesh cache: every model the game draws was put where it is by
 * sgdRemap(), which calls MioPan_Graph3dInvalidateSkinCache() on that address
 * before anything can walk it.  A different model at a reused address cannot
 * be drawn without first retiring the old entry. */
std::unordered_map<const SGDFILEHEADER *, ResidentPresetModel>
    s_resident_models;

/* Owners invalidated within a frame of being built -- gra3dChangeST() every
 * frame, i.e. scrolling water and light shafts.  Rebuilding one of those each
 * frame costs more than streaming it, so it streams until the frame stored
 * here. */
std::unordered_map<const SGDFILEHEADER *, unsigned long long>
    s_resident_volatile_until;

/* 30 fps logical frames.  A model the walker has not drawn for a minute is
 * dropped, and rebuilt if it is drawn again: that bounds the GPU copies of
 * models whose memory the game reused without a remap (the next remap at that
 * address would retire them too; this only covers the ones that never get
 * one). */
constexpr unsigned long long kResidentIdleFrames = 30u * 60u;
constexpr unsigned long long kResidentVolatileFrames = 30u * 10u;
/* A model that could not be made resident is retried after this long. */
constexpr unsigned long long kResidentRetryFrames = 30u * 5u;
constexpr unsigned long long kResidentSweepInterval = 64u;
unsigned long long s_resident_last_sweep;

void SweepIdleResidentModels(unsigned long long frame)
{
    if (frame < s_resident_last_sweep + kResidentSweepInterval)
    {
        return;
    }
    s_resident_last_sweep = frame;

    for (auto it = s_resident_models.begin(); it != s_resident_models.end();)
    {
        if (frame > it->second.used_frame + kResidentIdleFrames)
        {
            MioPan_RendererReleaseResidentMesh(it->second.mesh);
            it = s_resident_models.erase(it);
        }
        else
        {
            ++it;
        }
    }
    for (auto it = s_resident_volatile_until.begin();
         it != s_resident_volatile_until.end();)
    {
        if (frame >= it->second)
        {
            it = s_resident_volatile_until.erase(it);
        }
        else
        {
            ++it;
        }
    }
}

/* The streamed path's normal, as EncodeMeshNormal() leaves it: unit length,
 * and +Z for anything non-finite or degenerate.  Only the fragment-lit shader
 * reads it. */
void StoreResidentNormal(float *out, const float *normal)
{
    float x = 0.0f;
    float y = 0.0f;
    float z = 1.0f;
    if (std::isfinite(normal[0]) && std::isfinite(normal[1]) &&
        std::isfinite(normal[2]))
    {
        const float length = std::sqrt(normal[0] * normal[0] +
                                       normal[1] * normal[1] +
                                       normal[2] * normal[2]);
        if (length > 0.000001f)
        {
            x = normal[0] / length;
            y = normal[1] / length;
            z = normal[2] / length;
        }
    }
    out[0] = x;
    out[1] = y;
    out[2] = z;
    out[3] = 0.0f;
}

/* Decode every preset unit of `owner` into one resident mesh.  The walk is
 * gra3dsgdDrawPresetDataObject()'s over blocks 1..n-1, but it visits every
 * unit where the walker would stop at a culled bounding box -- the mesh has
 * to hold what a later frame may see.  Returns false, with no mesh, when
 * nothing could be made resident; every unit then streams. */
bool BuildResidentPresetModel(SGDFILEHEADER *owner, ResidentPresetModel &model)
{
    /* No profile scope of its own: the only caller runs inside
     * MioPan_Graph3dDrawPresetMesh()'s MESH_CPU scope, and a nested scope of
     * the same phase would count the build twice. */
    std::vector<MioPanResidentVertex> vertices;
    std::vector<unsigned int> indices;
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uv;
    std::vector<unsigned int> unit_indices;

    SGDSELF32<SGDPROCUNITHEADER> *pk = owner->apProcUnitHead;
    const unsigned int num_block = owner->uiNumBlock;
    /* Latched across blocks, as _SetVUVNPRIM() is: nothing resets it between
     * two blocks, so a block may open on a mesh that uses the last one's. */
    SGDPROCUNITHEADER *vuvn = nullptr;

    for (unsigned int block = 1; block < num_block; block++)
    {
        for (SGDPROCUNITHEADER *unit = pk[block]; unit != nullptr;
             unit = unit->pNext)
        {
            if (unit->iCategory == VUVN)
            {
                vuvn = unit;
                continue;
            }
            if (unit->iCategory != MESH || vuvn == nullptr)
            {
                continue;
            }

            const int mesh_type = unit->VUMeshDesc.ucMeshType;
            const int num_mesh = unit->VUMeshDesc.ucNumMesh;
            if ((mesh_type != iMT_0 && mesh_type != iMT_2 &&
                 mesh_type != iMT_2F) ||
                num_mesh <= 0 || num_mesh > 256)
            {
                continue;
            }

            int vertex_count = 0;
            if (!BuildPresetStaticGeometry(vuvn, unit, mesh_type, num_mesh,
                                           positions, normals, uv,
                                           unit_indices, &vertex_count) ||
                vertex_count <= 0)
            {
                continue;
            }
            if (vertices.size() + (size_t)vertex_count >
                    (size_t)std::numeric_limits<int>::max() ||
                indices.size() + unit_indices.size() >
                    (size_t)std::numeric_limits<int>::max())
            {
                continue;
            }

            const unsigned int first_vertex = (unsigned int)vertices.size();
            const unsigned int first_index = (unsigned int)indices.size();
            vertices.resize(vertices.size() + (size_t)vertex_count);
            for (int i = 0; i < vertex_count; i++)
            {
                MioPanResidentVertex &vertex =
                    vertices[(size_t)first_vertex + (size_t)i];
                vertex.uv[0] = uv[(size_t)i * 2 + 0];
                vertex.uv[1] = uv[(size_t)i * 2 + 1];
                vertex.uv[2] = 0.0f;
                vertex.uv[3] = 0.0f;
                vertex.position[0] = positions[(size_t)i * 3 + 0];
                vertex.position[1] = positions[(size_t)i * 3 + 1];
                vertex.position[2] = positions[(size_t)i * 3 + 2];
                vertex.position[3] = 1.0f;
                StoreResidentNormal(vertex.normal, &normals[(size_t)i * 3]);
            }

            /* The streamed path drops a triangle with a non-finite corner as
             * it appends it.  Preset positions never change, so the same
             * triangles are dropped here, once. */
            for (size_t t = 0; t + 2 < unit_indices.size(); t += 3)
            {
                bool finite = true;
                for (int corner = 0; corner < 3; corner++)
                {
                    const float *p =
                        &positions[(size_t)unit_indices[t + corner] * 3];
                    finite = finite && std::isfinite(p[0]) &&
                             std::isfinite(p[1]) && std::isfinite(p[2]);
                }
                if (!finite)
                {
                    continue;
                }
                indices.push_back(first_vertex + unit_indices[t + 0]);
                indices.push_back(first_vertex + unit_indices[t + 1]);
                indices.push_back(first_vertex + unit_indices[t + 2]);
            }

            ResidentPresetUnit &range = model.units[unit];
            range.vuvn = vuvn;
            range.first_vertex = first_vertex;
            range.vertex_count = (unsigned int)vertex_count;
            range.first_index = first_index;
            range.index_count = (unsigned int)indices.size() - first_index;
        }
    }

    if (!vertices.empty() && !indices.empty())
    {
        model.mesh = MioPan_RendererCreateResidentMesh(
            vertices.data(), (unsigned int)vertices.size(), indices.data(),
            (unsigned int)indices.size());
    }
    if (model.mesh == 0)
    {
        model.units.clear();
        return false;
    }
    return true;
}

/* The TEX0 a textured preset unit carries in its own packet, read exactly as
 * MioPan_Graph3dDrawPresetMesh() reads it -- the fixed slot first, then a scan
 * of the A+D stream up to the colour blocks.  False means the unit carries
 * none and the GS's current TEX0 applies. */
bool ReadPresetUnitTex0(const SGDPROCUNITHEADER *mesh, sceGsTex0 *out)
{
    const SGDVUMESHDATA_PRESET *mesh_data =
        (const SGDVUMESHDATA_PRESET *)&mesh[1];
    if (mesh_data->sOffsetToPrim == 0)
    {
        return false;
    }
    const unsigned char *packet_start = (const unsigned char *)&mesh[1];
    const unsigned char *packet_end =
        (const unsigned char *)(&mesh->pNext + mesh_data->sOffsetToPrim);
    return ReadAdTex0(packet_start + 0x28, packet_end, out) ||
           FindTex0InMeshPacket(packet_start, packet_end, out);
}

uint64_t Tex0Key(const sceGsTex0 &tex0)
{
    uint64_t key;
    std::memcpy(&key, &tex0, sizeof(key));
    return key;
}

/* ==========================================================================
 *  Resident preset textures.
 *
 *  A room, a piece of furniture or a door carries its own textures -- one or
 *  more TRI2 units in block 0, GS image transfers into the shared texture
 *  window -- and the walker sends them again every time the model is drawn,
 *  because on the PS2 the window was 4 MB shared by everything and the model
 *  drawn before this one had overwritten it.  The port replays that: every
 *  model, every frame, swizzled into emulated GS memory, then every texture it
 *  names hashed again because the upload invalidated it.
 *
 *  None of that is needed once the model's textures are known.  So the first
 *  time a model draws right after its own upload -- nothing else written to GS
 *  memory in between -- every TEX0 its units carry is resolved, through the
 *  ordinary lookup and while its texels are exactly where the hardware would
 *  have had them.  From then on its draws are handed those textures directly
 *  and gra3dLoadTRI2FileToVRAM() stops sending its TRI2s at all.
 *
 *  Resolving by TEX0 value rather than by unit keeps the GS's own inheritance
 *  exact: a unit that carries no TEX0 draws with the last one set, which
 *  depends on which blocks were culled.  The value it ends up with is always
 *  one of the model's own -- measured over all 529 preset models on the disc,
 *  every block opens on a unit that carries its own TEX0, so inheritance never
 *  reaches past the model -- and the lookup refuses, and the upload comes back,
 *  if that ever stops being true.
 *
 *  Monotone draws upload a second set of CLUTs over the first, so they bind
 *  separately.  The texture a handle names is the one the ordinary lookup
 *  returns; nothing here decodes anything.
 *
 *  Two kinds of model sample texels some other model sent, and skipping that
 *  other model's TRI2s took them away:
 *
 *  - The room's shadow-source model (file 1 of the room pak; 82 of them) sends
 *    no TRI2 at all.  MapDrawRoomOne() draws it straight after the room, and
 *    its TEX0s name the room's sheet -- 224 of its 624 textures ones the
 *    room's own units never use.  Its draws resolve through the binding of
 *    the model whose TRI2s were sent last, provided nothing was sent since;
 *    a TEX0 that binding lacks is resolved on the spot in that model's
 *    context, its TRI2s re-sent first if they were skipped, and kept.
 *  - Four furniture models (f109, f210, f229, f357) send a CLUT and nothing
 *    else, drawn over the texels their sibling (f108, ...) sent just before.
 *    The coverage check at scan time catches them; they are never bound, and
 *    if the run before theirs was skipped it is re-sent first.
 *
 *  Both follow from the note below, which records every TRI2 run -- sent or
 *  skipped -- so what the GS would hold at any point is known.
 * ======================================================================== */

/* The most recent run of TRI2 sends: one model's, back to back with nothing
 * else written to GS memory between them, and how many GS uploads the frame
 * had made when the last one finished.  `materialized` is false when any of
 * the run was skipped, i.e. GS memory does not hold what the run would have
 * left there.  A model's texels are known to be in GS memory exactly while
 * this still names it, holds every TRI2 its colour mode sends, is
 * materialized, and nothing has been uploaded since. */
struct Tri2UploadNote
{
    const SGDFILEHEADER *owner = nullptr;
    std::vector<const void *> heads;
    unsigned long long frame = 0;
    uint64_t gs_uploads = 0;
    int monotone = 0;
    bool materialized = true;
};
Tri2UploadNote s_tri2_note;
/* The frame's GS upload count as the TRI2 being sent now began. */
uint64_t s_tri2_uploads_before;

bool HeadsUploaded(const std::vector<ResidentTri2Unit> &units)
{
    for (const ResidentTri2Unit &unit : units)
    {
        if (std::find(s_tri2_note.heads.begin(), s_tri2_note.heads.end(),
                      unit.head) == s_tri2_note.heads.end())
        {
            return false;
        }
    }
    return true;
}

/* The note still describes the last thing written to GS memory, this frame
 * and in this colour mode -- what a draw now samples is what that run left. */
bool Tri2NoteIsCurrent(unsigned long long frame, int monotone)
{
    return s_tri2_note.owner != nullptr && s_tri2_note.frame == frame &&
           s_tri2_note.monotone == monotone &&
           s_tri2_note.gs_uploads == MioPan_GsGetUploadCountLive();
}

/* Extend the note with one more TRI2 of the model being walked, or start a
 * new run.  `sent` is false for one skipped because the model is bound. */
void RecordTri2Run(const SGDFILEHEADER *owner, const void *head, bool sent)
{
    const unsigned long long frame = MioPan_RendererGetFrameIndex();
    const int monotone = gra3dIsMonotoneDrawEnable() != 0 ? 1 : 0;
    if (s_tri2_note.owner != owner || s_tri2_note.frame != frame ||
        s_tri2_note.monotone != monotone ||
        s_tri2_note.gs_uploads != s_tri2_uploads_before)
    {
        s_tri2_note.heads.clear();
        s_tri2_note.materialized = true;
    }
    s_tri2_note.owner = owner;
    s_tri2_note.heads.push_back(head);
    s_tri2_note.materialized = s_tri2_note.materialized && sent;
    s_tri2_note.frame = frame;
    s_tri2_note.gs_uploads = MioPan_GsGetUploadCountLive();
    s_tri2_note.monotone = monotone;
}

/* Send the noted run after all -- every unit of it, as
 * gra3dLoadTRI2FileToVRAM() would have -- now that a draw needs what it
 * leaves in GS memory.  It is still the last thing written, so this puts GS
 * memory exactly where the original had it. */
bool MaterializeTri2Note()
{
    const auto found = s_resident_models.find(s_tri2_note.owner);
    if (found == s_resident_models.end())
    {
        return false;
    }
    const ResidentPresetModel &model = found->second;
    for (const void *head : s_tri2_note.heads)
    {
        int num_texture = 0;
        for (const std::vector<ResidentTri2Unit> *units :
             {&model.tri2_units, &model.mono_units})
        {
            for (const ResidentTri2Unit &unit : *units)
            {
                if (unit.head == head)
                {
                    num_texture = unit.num_texture;
                }
            }
        }
        if (num_texture <= 0)
        {
            return false;
        }
        gra3dHostLoadTRI2(num_texture, (SGDTRI2FILEHEADER *)head);
    }
    s_tri2_note.materialized = true;
    s_tri2_note.gs_uploads = MioPan_GsGetUploadCountLive();
    MioPan_RendererCountTextureReplay();
    return true;
}

int MonotoneIndex()
{
    return gra3dIsMonotoneDrawEnable() != 0 ? 1 : 0;
}

void AddImageToCoverage(sceGsLoadImage *image, void *coverage)
{
    static_cast<MioPan::GS::UploadCoverage *>(coverage)->Add(*image);
}

bool CoversAll(const MioPan::GS::UploadCoverage &coverage,
               const std::vector<uint64_t> &values)
{
    for (uint64_t key : values)
    {
        sceGsTex0 tex0;
        std::memcpy(&tex0, &key, sizeof(tex0));
        if (!coverage.Covers(tex0))
        {
            return false;
        }
    }
    return true;
}

/* The block 0 TRI2 units, the TEX0 values of every textured unit, and
 * whether the model's own sends cover those TEX0s in each colour mode. */
void ScanResidentPresetTextures(SGDFILEHEADER *owner,
                                ResidentPresetModel &model)
{
    SGDSELF32<SGDPROCUNITHEADER> *pk = owner->apProcUnitHead;
    const unsigned int num_block = owner->uiNumBlock;
    if (num_block == 0)
    {
        return;
    }

    /* What LoadTRI2Files() hands gra3dLoadTRI2FileToVRAM(): the first header
     * of the unit, past its padding.  Every preset model on the disc has
     * exactly one of each kind -- which matters, because the walker's
     * save_tri2_pointer keeps only the last one it meets. */
    for (SGDPROCUNITHEADER *unit = pk[0]; unit != nullptr; unit = unit->pNext)
    {
        if (unit->iCategory != TRI2 && unit->iCategory != MonotoneTRI2)
        {
            continue;
        }
        const void *head = (const void *)((uintptr_t)&unit[1].pNext +
                                          (intptr_t)unit->TexDesc.iPaddingSize);
        (unit->iCategory == TRI2 ? model.tri2_units : model.mono_units)
            .push_back({head, unit->TexDesc.iNumTexture});
    }

    for (unsigned int block = 1; block < num_block; block++)
    {
        for (SGDPROCUNITHEADER *unit = pk[block]; unit != nullptr;
             unit = unit->pNext)
        {
            if (unit->iCategory != MESH)
            {
                continue;
            }
            const int mesh_type = unit->VUMeshDesc.ucMeshType;
            sceGsTex0 tex0;
            if ((mesh_type == iMT_2 || mesh_type == iMT_2F) &&
                ReadPresetUnitTex0(unit, &tex0))
            {
                const uint64_t key = Tex0Key(tex0);
                if (std::find(model.tex0_values.begin(),
                              model.tex0_values.end(),
                              key) == model.tex0_values.end())
                {
                    model.tex0_values.push_back(key);
                }
            }
        }
    }

    /* Exactly what its sends write against exactly what its TEX0s read.  A
     * TEX0 that reads anything else depends on what was sent before the
     * model, so resolving it straight after the model's own sends proves
     * nothing, and the model is never bound in that mode.  Over every preset
     * model on the disc this flags the four that send only a CLUT; checked
     * against decoding over three different backgrounds, it agrees on every
     * TEX0 in both modes. */
    if (model.tri2_units.empty() || model.tex0_values.empty())
    {
        return;
    }
    MioPan::GS::UploadCoverage coverage;
    for (const ResidentTri2Unit &unit : model.tri2_units)
    {
        gra3dHostForEachTRI2Image(unit.num_texture,
                                  (SGDTRI2FILEHEADER *)unit.head,
                                  AddImageToCoverage, &coverage);
    }
    model.self_covered[0] = CoversAll(coverage, model.tex0_values);
    for (const ResidentTri2Unit &unit : model.mono_units)
    {
        gra3dHostForEachTRI2Image(unit.num_texture,
                                  (SGDTRI2FILEHEADER *)unit.head,
                                  AddImageToCoverage, &coverage);
    }
    model.self_covered[1] = CoversAll(coverage, model.tex0_values);
    for (int mode = 0; mode < 2; mode++)
    {
        model.textures[mode].refused = !model.self_covered[mode];
    }
}

/* The model's entry, created -- and its textures scanned -- on first sight. */
ResidentPresetModel &RegisterResidentPresetModel(SGDFILEHEADER *owner,
                                                 unsigned long long frame)
{
    auto found = s_resident_models.find(owner);
    if (found == s_resident_models.end())
    {
        ResidentPresetModel model;
        ScanResidentPresetTextures(owner, model);
        found = s_resident_models.emplace(owner, std::move(model)).first;
    }
    found->second.used_frame = frame;
    return found->second;
}

/* The resident slice for one unit, building its model's mesh on first sight.
 * Returns false -- stream this unit -- whenever there is any doubt: resident
 * meshes switched off, a model that rebuilds every frame, a unit the build did
 * not decode, or a walker latched onto a different VUVN than the build was. */
bool FindResidentPresetUnit(SGDFILEHEADER *owner,
                            const SGDPROCUNITHEADER *vuvn,
                            const SGDPROCUNITHEADER *mesh,
                            unsigned int *out_mesh,
                            ResidentPresetUnit *out_unit)
{
    if (owner == nullptr || !MioPan_RendererGetResidentMeshes())
    {
        return false;
    }

    const unsigned long long frame = MioPan_RendererGetFrameIndex();
    SweepIdleResidentModels(frame);

    const auto volatile_owner = s_resident_volatile_until.find(owner);
    if (volatile_owner != s_resident_volatile_until.end())
    {
        if (frame < volatile_owner->second)
        {
            return false;
        }
        s_resident_volatile_until.erase(volatile_owner);
    }

    ResidentPresetModel &model = RegisterResidentPresetModel(owner, frame);
    if (model.geometry_tried && model.mesh == 0 &&
        frame >= model.built_frame + kResidentRetryFrames)
    {
        model.geometry_tried = false;
    }
    if (!model.geometry_tried)
    {
        /* Tried once whether or not it works, so a model that cannot be made
         * resident is not decoded in full on every draw until the retry. */
        model.geometry_tried = true;
        model.built_frame = frame;
        BuildResidentPresetModel(owner, model);
    }
    if (model.mesh == 0)
    {
        return false;
    }

    const auto unit = model.units.find(mesh);
    if (unit == model.units.end() || unit->second.vuvn != vuvn)
    {
        return false;
    }
    *out_mesh = model.mesh;
    *out_unit = unit->second;
    return true;
}

/* Resolve every TEX0 in `values` now, while their texels are in GS memory,
 * and whichever of `borrowed` resolve -- those are only a head start for the
 * models that draw through this one's binding, which resolve any that are
 * missing themselves. */
void CaptureResidentTextures(const std::vector<uint64_t> &values,
                             ResidentTextureBinding &binding,
                             const std::vector<uint64_t> *borrowed = nullptr)
{
    binding.textures.clear();
    for (uint64_t key : values)
    {
        sceGsTex0 tex0;
        std::memcpy(&tex0, &key, sizeof(tex0));
        const void *texture = MioPan_RendererResolveTexture(&tex0);
        if (texture == nullptr)
        {
            /* Nothing to hand the draw; it has to keep resolving this TEX0
             * itself, so the texels have to keep arriving. */
            binding.refused = true;
            binding.textures.clear();
            return;
        }
        binding.textures.emplace(key, texture);
    }
    if (borrowed != nullptr)
    {
        for (uint64_t key : *borrowed)
        {
            sceGsTex0 tex0;
            std::memcpy(&tex0, &key, sizeof(tex0));
            const void *texture = MioPan_RendererResolveTexture(&tex0);
            if (texture != nullptr)
            {
                binding.textures.emplace(key, texture);
            }
        }
    }
    binding.complete = true;
    MioPan_RendererCountTextureCapture();
}

/* ResolveResidentTexture() for a model that sends no TRI2 in this colour
 * mode: what it samples is whatever the last TRI2 run left in GS memory, so it
 * draws through the binding of the model that run belonged to -- in practice
 * the room, for the room's shadow-source model.  NULL resolves `tex0` from GS
 * memory as always, which is right whenever the run was really sent. */
const void *ResolveBorrowedTexture(const SGDFILEHEADER *owner,
                                   const sceGsTex0 &tex0, int monotone,
                                   unsigned long long frame)
{
    if (!Tri2NoteIsCurrent(frame, monotone) || s_tri2_note.owner == owner)
    {
        /* Something else was written since -- GS memory is all there is. */
        return nullptr;
    }
    const auto found = s_resident_models.find(s_tri2_note.owner);
    if (found == s_resident_models.end())
    {
        return nullptr;
    }
    ResidentPresetModel &provider = found->second;
    ResidentTextureBinding &binding = provider.textures[monotone];
    const uint64_t key = Tex0Key(tex0);
    if (binding.complete)
    {
        const auto texture = binding.textures.find(key);
        if (texture != binding.textures.end())
        {
            return texture->second;
        }
    }

    /* Not in the provider's binding yet.  Remember it, so the provider's
     * next capture resolves it with its own. */
    if (std::find(provider.borrowed_tex0.begin(), provider.borrowed_tex0.end(),
                  key) == provider.borrowed_tex0.end() &&
        std::find(provider.tex0_values.begin(), provider.tex0_values.end(),
                  key) == provider.tex0_values.end())
    {
        provider.borrowed_tex0.push_back(key);
    }
    if (!binding.complete)
    {
        /* The provider is not bound, so its run really was sent. */
        return nullptr;
    }
    /* Bound, so its run was skipped unless an earlier draw already sent it
     * this frame.  Send it now if need be, resolve this TEX0 in exactly the
     * GS memory the original sampled it from, and keep the answer. */
    if (!s_tri2_note.materialized && !MaterializeTri2Note())
    {
        return nullptr;
    }
    const void *texture = MioPan_RendererResolveTexture(&tex0);
    if (texture != nullptr)
    {
        binding.textures.emplace(key, texture);
    }
    return texture;
}

/* The texture a textured preset unit of `owner` should draw with instead of
 * resolving `tex0` itself, or NULL to resolve it as always.  Captures the
 * model's set the first time that is safe. */
const void *ResolveResidentTexture(SGDFILEHEADER *owner, const sceGsTex0 &tex0)
{
    if (owner == nullptr || !MioPan_RendererGetResidentTextures())
    {
        return nullptr;
    }

    const unsigned long long frame = MioPan_RendererGetFrameIndex();
    ResidentPresetModel &model = RegisterResidentPresetModel(owner, frame);
    const int monotone = MonotoneIndex();
    if (SendsNoTri2(model, monotone))
    {
        return ResolveBorrowedTexture(owner, tex0, monotone, frame);
    }
    ResidentTextureBinding &binding = model.textures[monotone];

    if (!binding.complete && !binding.refused && !model.tri2_units.empty() &&
        Tri2NoteIsCurrent(frame, monotone) && s_tri2_note.owner == owner &&
        s_tri2_note.materialized && HeadsUploaded(model.tri2_units) &&
        (monotone == 0 || HeadsUploaded(model.mono_units)))
    {
        CaptureResidentTextures(model.tex0_values, binding,
                                &model.borrowed_tex0);
    }
    if (!binding.complete)
    {
        return nullptr;
    }

    const auto found = binding.textures.find(Tex0Key(tex0));
    if (found != binding.textures.end())
    {
        return found->second;
    }
    /* A TEX0 outside the model's own -- inherited from another model, which
     * the data never does.  Stop skipping this model's uploads rather than
     * guess; this one draw resolves against whatever GS memory holds. */
    binding.complete = false;
    binding.refused = true;
    binding.textures.clear();
    return nullptr;
}

/* ==========================================================================
 *  Resident character and item textures.
 *
 *  The same idea for the models that do not carry their textures: characters,
 *  ghosts and items keep theirs as TIM2 pictures in their own pack, and
 *  SendEneVram() / SendEneVramMono() / SendItemVram() send every one of them
 *  into the texture window immediately before each draw -- the player, the
 *  sister, every ghost, the camera, every frame.
 *
 *  So those three bracket their sends (MioPan_Graph3dBeginTim2Upload / End).
 *  A send that goes through leaves a note; the first draw of any SGD in that
 *  pack straight after it, with nothing else uploaded in between, resolves
 *  every TEX0 the pack's SGDs carry.  From then on a send for a mode that is
 *  resolved is dropped inside the GS layer, and the draws are handed their
 *  textures.  The EE side is untouched either way: SetManmdlTm2() still builds
 *  its packets.
 *
 *  Keyed by the pack -- the character's model pack, or the item's pk2 -- and
 *  every SGD in it maps back to that key.  The TEX0s are re-based in place
 *  around every event scene (SetEneVram -> MpkAddTexOffset), and a remap
 *  means the memory holds another model, so both retire the pack's entry and
 *  the next send starts over.  Measured on the disc: no character or item mesh
 *  inherits its TEX0, so every TEX0 a draw can name is one of the pack's own.
 *  The colour mode is the send's -- the caller picks the monotone CLUT set --
 *  and the draws use whichever mode their pack was last sent in.
 *
 *  A pack is only bound when its own send covers every texel and CLUT entry
 *  its TEX0s read; the send's transfers are watched to know.  On the disc that
 *  is every pack but ch041_kurorei, one of whose TEX0s names texels nothing in
 *  its pack sends -- it keeps sending, and sampling GS memory, as before.
 * ======================================================================== */
struct Tim2Source
{
    std::vector<SGDFILEHEADER *> owners;
    std::vector<uint64_t> tex0_values;
    ResidentTextureBinding textures[2];
    /* The colour mode of this pack's most recent send. */
    int mode = 0;
};

std::unordered_map<const void *, Tim2Source> s_tim2_sources;
std::unordered_map<const SGDFILEHEADER *, const void *> s_tim2_owner_source;

/* The last send that went through, and the frame's GS upload count when it
 * finished; the pack's texels are in GS memory while nothing has followed.
 * `coverage` is what that send wrote, when it was watched. */
struct Tim2UploadNote
{
    const void *source = nullptr;
    unsigned long long frame = 0;
    uint64_t gs_uploads = 0;
    int mode = 0;
    bool watched = false;
    MioPan::GS::UploadCoverage coverage;
};
Tim2UploadNote s_tim2_note;

/* The send in progress, between Begin and End, and what it has written so
 * far when it is being watched. */
struct Tim2UploadBracket
{
    const void *source = nullptr;
    int mode = 0;
    bool suppressed = false;
    bool watched = false;
    bool active = false;
};
Tim2UploadBracket s_tim2_bracket;
MioPan::GS::UploadCoverage s_tim2_send_coverage;

void WatchTim2Upload(const sceGsLoadImage *image, void *coverage)
{
    static_cast<MioPan::GS::UploadCoverage *>(coverage)->Add(*image);
}

/* Every TEX0 a textured unit of `owner` carries.  The runtime walker visits
 * every block but the first, and the first holds no meshes. */
void ScanRuntimeOwnerTextures(SGDFILEHEADER *owner,
                              std::vector<uint64_t> &values)
{
    SGDSELF32<SGDPROCUNITHEADER> *pk = owner->apProcUnitHead;
    for (unsigned int block = 0; block < owner->uiNumBlock; block++)
    {
        for (SGDPROCUNITHEADER *unit = pk[block]; unit != nullptr;
             unit = unit->pNext)
        {
            sceGsTex0 tex0;
            if (unit->iCategory != MESH ||
                (unit->VUMeshDesc.ucMeshType & 0x02) == 0 ||
                !ReadRuntimeUnitTex0(unit, &tex0))
            {
                continue;
            }
            const uint64_t key = Tex0Key(tex0);
            if (std::find(values.begin(), values.end(), key) == values.end())
            {
                values.push_back(key);
            }
        }
    }
}

/* The SGDs a pack holds: every unit of a character's MPK -- MpkMapUnit()'s
 * walk, ending on a zero or top-bit size -- or an item pk2's entry 0. */
void CollectTim2Owners(void *source, int kind,
                       std::vector<SGDFILEHEADER *> &owners)
{
    if (kind == MIOPAN_TIM2_SOURCE_ITEM)
    {
        u_int *sgd = Pk2GetAddr((u_int *)source, 0);
        if (sgd != nullptr)
        {
            owners.push_back((SGDFILEHEADER *)sgd);
        }
        return;
    }
    u_int *unit = (u_int *)source + 4;
    for (int guard = 0; guard < 256 && unit[0] - 1u < 0x7fffffffu; guard++)
    {
        owners.push_back((SGDFILEHEADER *)&unit[4]);
        unit += unit[0] / 4u + 4u;
    }
}

Tim2Source &RegisterTim2Source(void *source, int kind)
{
    auto found = s_tim2_sources.find(source);
    if (found != s_tim2_sources.end())
    {
        return found->second;
    }

    Tim2Source entry;
    std::vector<SGDFILEHEADER *> owners;
    CollectTim2Owners(source, kind, owners);
    for (SGDFILEHEADER *owner : owners)
    {
        /* Only SGDs the game has remapped are walked -- the same test
         * _gra3dDrawSGD() makes before drawing one. */
        if (owner->uiVersionId != SGD_VALID_VERSIONID ||
            owner->ucMapFlag == 0)
        {
            continue;
        }
        entry.owners.push_back(owner);
        ScanRuntimeOwnerTextures(owner, entry.tex0_values);
    }
    found = s_tim2_sources.emplace(source, std::move(entry)).first;
    for (SGDFILEHEADER *owner : found->second.owners)
    {
        s_tim2_owner_source[owner] = source;
    }
    return found->second;
}

void RetireTim2Source(const void *source)
{
    const auto found = s_tim2_sources.find(source);
    if (found == s_tim2_sources.end())
    {
        return;
    }
    for (SGDFILEHEADER *owner : found->second.owners)
    {
        const auto mapped = s_tim2_owner_source.find(owner);
        if (mapped != s_tim2_owner_source.end() && mapped->second == source)
        {
            s_tim2_owner_source.erase(mapped);
        }
    }
    s_tim2_sources.erase(found);
}

/* ResolveResidentTexture()'s counterpart for the runtime walker. */
const void *ResolveTim2Texture(const SGDFILEHEADER *owner,
                               const sceGsTex0 &tex0)
{
    if (owner == nullptr || !MioPan_RendererGetResidentTextures())
    {
        return nullptr;
    }
    const auto mapped = s_tim2_owner_source.find(owner);
    if (mapped == s_tim2_owner_source.end())
    {
        return nullptr;
    }
    const auto found = s_tim2_sources.find(mapped->second);
    if (found == s_tim2_sources.end())
    {
        return nullptr;
    }
    Tim2Source &source = found->second;
    ResidentTextureBinding &binding = source.textures[source.mode];

    if (!binding.complete && !binding.refused &&
        !source.tex0_values.empty() &&
        s_tim2_note.source == mapped->second &&
        s_tim2_note.frame == MioPan_RendererGetFrameIndex() &&
        s_tim2_note.mode == source.mode &&
        s_tim2_note.gs_uploads == MioPan_GsGetUploadCountLive() &&
        s_tim2_note.watched)
    {
        if (CoversAll(s_tim2_note.coverage, source.tex0_values))
        {
            CaptureResidentTextures(source.tex0_values, binding);
        }
        else
        {
            /* Some TEX0 reads what the pack's own send does not write. */
            binding.refused = true;
        }
    }
    if (!binding.complete)
    {
        return nullptr;
    }

    const auto texture = binding.textures.find(Tex0Key(tex0));
    if (texture != binding.textures.end())
    {
        return texture->second;
    }
    binding.complete = false;
    binding.refused = true;
    binding.textures.clear();
    return nullptr;
}
}

/*
 * Hand the engine's own camera to the renderer.
 *
 * There is no host-side camera any more.  gra3dApplyCamera() already builds a
 * world->view matrix (sceVu0CameraMatrix) and a real view->clip projection
 * (g3dCalcViewClipMatrix* into matViewClipObject), both row-vector, which is
 * the convention ApplyMatrixRowVector() transforms with -- so they drop
 * straight in.  The pair this replaced was numerically identical to them apart
 * from the Y sign, which now lives in g3dCamera.c where the projection is
 * built, and it had to be kept in step by hand every time the camera changed.
 *
 * A degenerate camera (zero fov, near == far) makes the engine's projection
 * non-finite.  Drop it rather than substitute different numbers: keeping the
 * previous frame's camera is a much smaller lie than silently rendering with a
 * field of view the game never asked for.
 *
 * REVERSED-Z.  The matrix handed to the renderer has its depth row rewritten so
 * clip z runs near -> w, far -> 0 instead of the engine's near -> -w, far -> +w.
 * The GS worked this way round too (larger Z is nearer, ZTST GEQUAL), so this
 * is the hardware's convention rather than a host invention -- but the reason
 * it is load-bearing here is precision.  The camera's real near plane is 0.1
 * against a far of 65535, and screen depth goes as 1/z, so a forward [0,1]
 * buffer spends nearly all its codes in the first unit in front of the lens:
 * with the D32_FLOAT target the renderer picks, depth values sit up against 1.0
 * where float ulp is 6e-8, and z is unresolvable past a few hundred units.
 * Reversed, the scene sits near 0 where float32 has exponent range to spare and
 * relative precision stays ~1e-7 all the way out.  (This only became visible
 * once fNearZ was corrected from a transcribed 10.0f to the ROM's 0.1f: the
 * wrong value was accidentally giving 100x the depth precision.)
 *
 * It is done here and not in g3dCalcViewClipMatrixPerspective() because the
 * engine's own matViewClipObject feeds gra3dVu0ClipFlags(), whose VU0 CLIP
 * emulation tests z against +-w.  Rewriting the engine matrix to a [0,w] volume
 * would leave both of its z bits permanently clear and silently retire the
 * near/far half of the bounding-box cull.  The renderer gets its own copy.
 */
extern "C" void MioPan_Graph3dApplyCamera(const GRA3DCAMERA *camera,
                                          const float (*view)[4])
{
    if (camera == nullptr || view == nullptr)
    {
        return;
    }

    const float *view_f = &view[0][0];
    const float *projection_f = &camera->matViewClipObject[0][0];

    for (int i = 0; i < 16; i++)
    {
        if (!std::isfinite(view_f[i]) || !std::isfinite(projection_f[i]))
        {
            return;
        }
    }

    float projection[16];
    std::memcpy(projection, projection_f, sizeof(projection));

    /* Row-vector layout, so the depth output is column 2: clip.z picks up
     * mat[2][2] from the view depth and mat[3][2] from w. */
    const float fNear = camera->fNearZ;
    const float fFar  = camera->fFarZ;

    if (camera->type == PT_PERSPECTIVE && std::isfinite(fNear) &&
        std::isfinite(fFar) && fFar > fNear)
    {
        /* Solved from near/far rather than folded out of the assembled matrix.
         * The forward coefficient is (f+n)/(f-n), which for these values is
         * 1.000003 -- subtracting that from 1.0 in float throws away four of
         * its significant digits, and the whole benefit with it. */
        projection[2 * 4 + 2] = -fNear / (fFar - fNear);
        projection[3 * 4 + 2] = (fFar * fNear) / (fFar - fNear);
    }
    else
    {
        /* Orthographic, or a camera whose depth range we cannot trust: fall
         * back to the generic column identity z' = 0.5w - 0.5z.  Exact for any
         * [-w,w] projection, and the cancellation that makes it unusable for
         * perspective does not arise when the terms are not near-equal.  No
         * camera in this build selects PT_ORTHO. */
        for (int row = 0; row < 4; row++)
        {
            projection[row * 4 + 2] =
                0.5f * projection[row * 4 + 3] - 0.5f * projection[row * 4 + 2];
        }
    }

    for (int i = 0; i < 16; i++)
    {
        if (!std::isfinite(projection[i]))
        {
            return;
        }
    }

    MioPan_RendererSet3DViewProjection(view_f, projection);
}

extern "C" void MioPan_Graph3dDrawPresetMesh(SGDFILEHEADER *owner,
                                             SGDPROCUNITHEADER *vuvn,
                                             SGDPROCUNITHEADER *mesh,
                                             const float *local_world)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_CPU);
    SGDVUVNDESC          *vuvn_desc;
    SGDVUVNDATA_PRESET   *vuvn_data;
    SGDVUMESHDATA_PRESET *mesh_data;
    _SGDVUMESHCOLORDATA  *color_data;
    sceGsTex0             tex0_value;
    const sceGsTex0      *tex0;
    std::vector<float>   &positions = s_mesh_decode_scratch.positions;
    std::vector<float>   &normals = s_mesh_decode_scratch.normals;
    std::vector<float>   &uv = s_mesh_decode_scratch.uv;
    std::vector<float>   &rgba = s_mesh_decode_scratch.rgba;
    std::vector<unsigned int> &indices = s_mesh_decode_scratch.indices;
    RUNTIME_VERTEX_COLORS &lit = s_mesh_decode_scratch.lighting;
    int                   mesh_type;
    int                   num_mesh;
    int                   vertex_offset;
    int                   static_vertex_count;
    int                   fragment_terms;
    bool                  fragment_lighting;
    MioPanLightState fragment_lights{};

    if (owner == nullptr || vuvn == nullptr || mesh == nullptr ||
        local_world == nullptr)
    {
        return;
    }

    positions.clear();
    normals.clear();
    uv.clear();
    rgba.clear();
    indices.clear();
    lit.clear();

    mesh_type = mesh->VUMeshDesc.ucMeshType;
    if (mesh_type != iMT_0 && mesh_type != iMT_2 && mesh_type != iMT_2F)
    {
        return;
    }

    num_mesh = mesh->VUMeshDesc.ucNumMesh;
    if (num_mesh <= 0 || num_mesh > 256)
    {
        return;
    }

    vuvn_desc = &vuvn->VUVNDesc;
    vuvn_data = (SGDVUVNDATA_PRESET *)&vuvn[1];
    mesh_data = (SGDVUMESHDATA_PRESET *)&mesh[1];
    if (mesh_data->sOffsetToPrim == 0)
    {
        return;
    }

    color_data = (_SGDVUMESHCOLORDATA *)(&mesh->pNext +
                                         mesh_data->sOffsetToPrim);
    tex0 = nullptr;
    if ((mesh_type & 0x02) != 0)
    {
        const unsigned char *packet_start = (const unsigned char *)&mesh[1];
        const unsigned char *packet_end = (const unsigned char *)color_data;

        ScanMeshPacketGsTest(packet_start, packet_end);

        /* The same read ScanResidentPresetTextures() makes, so the set a
         * model binds is exactly the set its draws ask for. */
        if (ReadPresetUnitTex0(mesh, &tex0_value))
        {
            RememberTex0(tex0_value);
            tex0 = &tex0_value;
        }
        else if (g_last_tex0_valid)
        {
            /* inherit the GS's current TEX0, as the hardware would */
            tex0_value = g_last_tex0;
            tex0       = &tex0_value;
        }
    }

    fragment_terms = BuildFragmentLightState(&fragment_lights, local_world);
    fragment_lighting = fragment_terms != 0;

    /* The model's resolved texture for this TEX0, when its uploads have
     * stopped; NULL resolves the TEX0 as always.  Resident or streamed, the
     * draw has to use it -- the texels are no longer in GS memory. */
    const void *bound_texture =
        tex0 != nullptr ? ResolveResidentTexture(owner, *tex0) : nullptr;

    /* Resident geometry when there is some; otherwise decode as before.  The
     * colour pass below is the same either way -- it is what carries the
     * light, and the light is per frame. */
    unsigned int resident_mesh = 0;
    ResidentPresetUnit resident{};
    const bool have_resident =
        FindResidentPresetUnit(owner, vuvn, mesh, &resident_mesh, &resident);

    static_vertex_count = 0;
    if (!have_resident &&
        !BuildPresetStaticGeometry(vuvn, mesh, mesh_type, num_mesh,
                                   positions, normals, uv, indices,
                                   &static_vertex_count))
    {
        return;
    }
    rgba.reserve((size_t)vuvn_desc->sNumVertex * 4);

    {
        vertex_offset = 0;
        for (int i = 0; i < num_mesh; i++)
        {
            _SGDVUMESHCOLORDATA  *unpack_color;
            VECTOR3              *colors;
            int                   vertex_count;

            unpack_color = (_SGDVUMESHCOLORDATA *)GetNextUnpackAddr((u_int *)color_data);
            if (unpack_color == nullptr)
            {
                break;
            }
            vertex_count = unpack_color->VifUnpack.NUM;
            if (vertex_count <= 0 || vertex_offset < 0 ||
                vertex_offset + vertex_count > vuvn_desc->sNumVertex ||
                vertex_offset + vertex_count > kMeshBufferVertexLimit)
            {
                break;
            }

            colors = unpack_color->avColor;
            FixColors(colors, vertex_count);

            BuildPresetVertexColors(lit, vuvn_data, vuvn_desc, mesh_type, i,
                                    vertex_offset, vertex_count, colors,
                                    local_world,
                                    fragment_lighting ? &fragment_lights
                                                      : nullptr);

            for (int j = 0; j < vertex_count; j++)
            {
                PushPresetColor(rgba, lit.data(), j);
            }

            vertex_offset += vertex_count;
            color_data =
                (_SGDVUMESHCOLORDATA *)&unpack_color->avColor[vertex_count];
        }
    }

    if (have_resident)
    {
        /* A unit whose every triangle was non-finite has nothing to draw; the
         * streamed path would have clipped them all too. */
        if (resident.index_count == 0 &&
            (unsigned int)vertex_offset == resident.vertex_count)
        {
            return;
        }
        if (vertex_offset > 0 &&
            (unsigned int)vertex_offset == resident.vertex_count &&
            rgba.size() / 4 == (size_t)vertex_offset &&
            MioPan_RendererDrawResidentMesh(
                resident_mesh, resident.first_vertex, resident.vertex_count,
                resident.first_index, resident.index_count, rgba.data(), tex0,
                local_world, fragment_lighting ? &fragment_lights : nullptr,
                bound_texture))
        {
            return;
        }
        /* The renderer declined, or this frame's colour walk disagrees with
         * what was decoded.  Either way the unit is drawn exactly as it was
         * before resident meshes existed. */
        if (!BuildPresetStaticGeometry(vuvn, mesh, mesh_type, num_mesh,
                                       positions, normals, uv, indices,
                                       &static_vertex_count))
        {
            return;
        }
    }

    if (vertex_offset <= 0 || rgba.size() / 4 != (size_t)vertex_offset ||
        static_vertex_count != vertex_offset ||
        positions.size() / 3 != (size_t)vertex_offset ||
        normals.size() / 3 != (size_t)vertex_offset ||
        uv.size() / 2 != (size_t)vertex_offset || indices.empty())
    {
        return;
    }

    DrawIndexedFallback(tex0, positions, normals, uv, rgba, indices,
                        local_world,
                        fragment_lighting ? &fragment_lights : nullptr,
                        bound_texture);
}

void DrawRuntimeMesh(SGDFILEHEADER *owner,
                     SGDPROCUNITHEADER *vuvn,
                     SGDPROCUNITHEADER *mesh,
                     const float *local_world,
                     const float *post_positions,
                     const float *post_normals)
{
    sceGsTex0             tex0_value;
    const sceGsTex0      *tex0;
    RUNTIME_VERTEX_COLORS &colors = s_mesh_decode_scratch.lighting;
    std::vector<float>   &positions = s_mesh_decode_scratch.positions;
    std::vector<float>   &normals = s_mesh_decode_scratch.normals;
    std::vector<float>   &uv = s_mesh_decode_scratch.uv;
    std::vector<unsigned int> &indices = s_mesh_decode_scratch.indices;
    MioPanLightState vertex_lights{};
    MioPanLightState fragment_lights{};
    int                   mesh_type;
    int                   mesh_family;
    int                   num_mesh;
    int                   stream_capacity;
    int                   unique_vertex_count;

    if (vuvn == nullptr || mesh == nullptr || local_world == nullptr)
    {
        return;
    }

    mesh_type = mesh->VUMeshDesc.ucMeshType;
    mesh_family = mesh_type & 0xd3;

    if (mesh_family != 0x00 && mesh_family != 0x02 && mesh_family != 0x42 &&
        mesh_family != 0x80 && mesh_family != 0x82)
    {
        return;
    }

    num_mesh = mesh->VUMeshDesc.ucNumMesh;
    if (num_mesh <= 0 || num_mesh > 256 ||
        vuvn->VUVNDesc.sNumVertex <= 0)
    {
        return;
    }

    tex0 = FindRuntimeMeshTex0(mesh, &tex0_value);
    /* The pack's resolved texture, once its sends have stopped -- see
     * "Resident character and item textures". */
    const void *bound_texture =
        tex0 != nullptr ? ResolveTim2Texture(owner, *tex0) : nullptr;
    stream_capacity = RuntimeMeshStreamCapacity(
        vuvn, mesh, &unique_vertex_count);
    if (stream_capacity <= 0 || unique_vertex_count <= 0)
    {
        return;
    }

    /* Characters take the same split room geometry does.  The streamed layout
     * already carries a normal -- octahedrally packed into the spare UV pair --
     * so there is nothing structural in the way; what was missing was that
     * PushRuntimeStripTriangle() never filled it in. */
    const bool fragment_lighting =
        BuildFragmentLightState(&fragment_lights, local_world) != 0;
    const MioPanLightState *fragment_lights_p =
        fragment_lighting ? &fragment_lights : nullptr;

    bool cpu_colors_ready = false;
    const auto build_cpu_colors = [&]()
    {
        if (cpu_colors_ready)
        {
            return;
        }
        if (mesh_family == 0x80 || mesh_family == 0x82)
        {
            BuildRuntimeVertexColors(
                colors, unique_vertex_count, nullptr,
                (const SGDVUVNDATA_PRESET *)&vuvn[1], nullptr, nullptr,
                local_world, fragment_lights_p);
        }
        else
        {
            /* Family 0x42 is the VU "common" path: its mesh packet has no
             * matching VUVN process unit and consumes the VUVN selected by the
             * walker immediately before it. */
            BuildRuntimeVertexColors(
                colors, unique_vertex_count,
                (const _VECTORDATA *)&vuvn[3], nullptr,
                post_positions, post_normals, local_world,
                fragment_lights_p);
        }
        cpu_colors_ready = true;
    };

    const auto draw_streamed_fallback = [&]()
    {
        /* Lighting is deliberately lazy: the common indexed path now sends
         * normals to the GPU, while only exceptional decode/cache/upload
         * fallbacks pay the old per-vertex CPU cost. */
        build_cpu_colors();
        MeshStreamScope stream(tex0, stream_capacity, local_world,
                               fragment_lights_p, bound_texture);
        if (!stream)
        {
            return;
        }
        if (mesh_family == 0x80 || mesh_family == 0x82)
        {
            AppendRuntimePreloadedMesh(stream.token(), vuvn, mesh, colors);
        }
        else
        {
            AppendRuntimeIndexedMesh(stream.token(), vuvn, mesh,
                                     post_positions, post_normals, colors);
        }
        stream.Commit();
    };

    /* A pose with a null/nonfinite address must retain the direct stream's
     * per-triangle clipping semantics.  Valid poses take the indexed path and
     * upload one live position/normal record per strip vertex. */
    if (owner == nullptr ||
        !BuildRuntimeAnimatedGeometry(positions, normals, vuvn, mesh_family,
                                      unique_vertex_count, post_positions,
                                      post_normals) ||
        !BuildAnimatedVertexLightState(&vertex_lights))
    {
        draw_streamed_fallback();
        return;
    }

    draw_streamed_fallback();
}

SGDPROCUNITHEADER *s_cachedPostVUVNSource;
std::vector<float> s_cachedPostVUVNPositions;
/* The second qword of each skinned DVECTOR.  SetVUVNDataPost blends the normal
 * by the same bone pair as the vertex, so this is the world-space normal the
 * VU1 would have lit with -- the object-space one in the SGD is useless once
 * the block is skinned. */
std::vector<float> s_cachedPostVUVNNormals;

/*
 * An SGD allocation is about to be reused or unmapped.
 *
 * The only thing left to drop is the post-skin position/normal cache above,
 * which is keyed by SGDPROCUNITHEADER pointer.  Model arenas hand out the same
 * addresses again, so keeping a post-skin packet across a remap can pair a new
 * VUVN unit with the previous model's vertices.
 *
 * `owner` is unused now that there is no per-owner mesh cache, but the callers
 * are ROM-reconstruction sites that already have it to hand, and keeping the
 * parameter leaves room to key this cache by owner should it ever need to be.
 */
extern "C" void MioPan_Graph3dInvalidateSkinCache(SGDFILEHEADER *owner)
{
    s_cachedPostVUVNSource = nullptr;
    s_cachedPostVUVNPositions.clear();
    s_cachedPostVUVNNormals.clear();

    /* A character or item SGD being remapped retires its whole pack: the
     * memory is about to hold something else. */
    const auto tim2_owner = s_tim2_owner_source.find(owner);
    if (tim2_owner != s_tim2_owner_source.end())
    {
        RetireTim2Source(tim2_owner->second);
    }

    /* And everything resident about this model -- its mesh and its resolved
     * textures.  The callers are sgdRemap() and sgdRemapInverse(), before a
     * model's memory is repurposed, so none of it can be trusted afterwards.
     * Draws already queued keep the mesh they were given; the next draw
     * starts over. */
    if (s_tri2_note.owner == owner)
    {
        /* Its TRI2s are about to be somewhere else's memory; a draw must not
         * re-send them from here. */
        s_tri2_note = Tri2UploadNote{};
    }
    const auto found = s_resident_models.find(owner);
    if (found == s_resident_models.end())
    {
        return;
    }
    const unsigned long long frame = MioPan_RendererGetFrameIndex();
    if (found->second.mesh != 0 && frame <= found->second.built_frame + 1u)
    {
        /* Rebuilt last frame and invalidated already.  Stream it rather than
         * rebuild it every frame. */
        s_resident_volatile_until[owner] = frame + kResidentVolatileFrames;
    }
    MioPan_RendererReleaseResidentMesh(found->second.mesh);
    s_resident_models.erase(found);
}

extern "C" void MioPan_Graph3dNotifyUVChange(SGDFILEHEADER *owner)
{
    s_cachedPostVUVNSource = nullptr;
    s_cachedPostVUVNPositions.clear();
    s_cachedPostVUVNNormals.clear();

    /* gra3dChangeST() moves UVs and nothing else, so the geometry goes and
     * the textures stay: they are named by TEX0, which it does not touch. */
    const auto found = s_resident_models.find(owner);
    if (found == s_resident_models.end() || !found->second.geometry_tried)
    {
        return;
    }
    ResidentPresetModel &model = found->second;
    const unsigned long long frame = MioPan_RendererGetFrameIndex();
    if (model.mesh != 0 && frame <= model.built_frame + 1u)
    {
        /* Rebuilt last frame and scrolled again already: scrolling water and
         * light shafts do this every frame.  Stream their geometry rather than
         * rebuild it every frame. */
        s_resident_volatile_until[owner] = frame + kResidentVolatileFrames;
    }
    MioPan_RendererReleaseResidentMesh(model.mesh);
    model.mesh = 0;
    model.units.clear();
    model.geometry_tried = false;
}

extern "C" int MioPan_Graph3dTextureUploadBound(const void *tri2_head)
{
    /* Latched for MioPan_Graph3dNoteTextureUpload(), which has to know
     * whether anything else reached GS memory between two TRI2s. */
    s_tri2_uploads_before = MioPan_GsGetUploadCountLive();

    if (tri2_head == nullptr || !MioPan_RendererGetResidentTextures())
    {
        return 0;
    }
    /* The walker is inside this model's _gra3dDrawSGD().  Only a preset
     * model's own TRI2s are ever skipped -- which also keeps a caller outside
     * a walk, where the current SGD is whatever was drawn last, from being
     * skipped.  Registered here rather than at its first draw, so the coverage
     * check has run before its very first send. */
    SGDFILEHEADER *owner = gra3dsgdGetData();
    if (owner == nullptr || owner->uiVersionId != SGD_VALID_VERSIONID ||
        (owner->ucModelType & 1) == 0)
    {
        return 0;
    }
    const unsigned long long frame = MioPan_RendererGetFrameIndex();
    ResidentPresetModel &model = RegisterResidentPresetModel(owner, frame);
    if (!HasTri2Head(model.tri2_units, tri2_head) &&
        !HasTri2Head(model.mono_units, tri2_head))
    {
        return 0;
    }
    const int monotone = MonotoneIndex();
    if (!model.self_covered[monotone])
    {
        /* It draws over texels the run before its own left behind.  If that
         * run was skipped and nothing has been written since, send it now --
         * the GS then holds what the original had when this model's CLUT went
         * on top. */
        if (s_tri2_note.owner != owner && !s_tri2_note.materialized &&
            s_tri2_note.frame == frame && s_tri2_note.monotone == monotone &&
            s_tri2_note.gs_uploads == s_tri2_uploads_before &&
            MaterializeTri2Note())
        {
            s_tri2_uploads_before = MioPan_GsGetUploadCountLive();
        }
        return 0;
    }
    if (!model.textures[monotone].complete)
    {
        return 0;
    }
    RecordTri2Run(owner, tri2_head, false);
    MioPan_RendererCountSkippedTextureUpload();
    return 1;
}

extern "C" int MioPan_Graph3dBeginTim2Upload(void *source, int kind,
                                             int monotone)
{
    /* A bracket left open can only be the caller's bug, but it must not
     * leave every later upload in the process suppressed, or watched. */
    if (s_tim2_bracket.suppressed)
    {
        MioPan_GsSetUploadSuppressed(0);
    }
    if (s_tim2_bracket.watched)
    {
        MioPan_GsSetUploadObserver(nullptr, nullptr);
    }
    s_tim2_bracket = Tim2UploadBracket{};
    s_tim2_bracket.active = true;
    s_tim2_bracket.source = source;
    s_tim2_bracket.mode = monotone != 0 ? 1 : 0;

    if (source == nullptr || !MioPan_RendererGetResidentTextures())
    {
        return 0;
    }
    Tim2Source &entry = RegisterTim2Source(source, kind);
    entry.mode = s_tim2_bracket.mode;
    const ResidentTextureBinding &binding = entry.textures[entry.mode];
    if (!binding.complete)
    {
        if (!binding.refused)
        {
            /* It may be bound straight after this send: learn what the send
             * writes, to check it covers what the pack's TEX0s read. */
            s_tim2_send_coverage.Clear();
            MioPan_GsSetUploadObserver(WatchTim2Upload, &s_tim2_send_coverage);
            s_tim2_bracket.watched = true;
        }
        return 0;
    }
    s_tim2_bracket.suppressed = true;
    MioPan_GsSetUploadSuppressed(1);
    MioPan_RendererCountSkippedTextureUpload();
    return 1;
}

extern "C" void MioPan_Graph3dEndTim2Upload(void)
{
    if (!s_tim2_bracket.active)
    {
        return;
    }
    if (s_tim2_bracket.watched)
    {
        MioPan_GsSetUploadObserver(nullptr, nullptr);
    }
    if (s_tim2_bracket.suppressed)
    {
        MioPan_GsSetUploadSuppressed(0);
    }
    else if (s_tim2_bracket.source != nullptr)
    {
        s_tim2_note.source = s_tim2_bracket.source;
        s_tim2_note.frame = MioPan_RendererGetFrameIndex();
        s_tim2_note.gs_uploads = MioPan_GsGetUploadCountLive();
        s_tim2_note.mode = s_tim2_bracket.mode;
        s_tim2_note.watched = s_tim2_bracket.watched;
        if (s_tim2_bracket.watched)
        {
            std::swap(s_tim2_note.coverage, s_tim2_send_coverage);
        }
    }
    s_tim2_bracket = Tim2UploadBracket{};
}

extern "C" void MioPan_Graph3dNotifyTex0Rebase(void *source)
{
    RetireTim2Source(source);
}

extern "C" void MioPan_Graph3dNoteTextureUpload(const void *tri2_head)
{
    RecordTri2Run(gra3dsgdGetData(), tri2_head, true);
}


void MioPan_Graph3dDrawRuntimeMesh(SGDFILEHEADER *owner,
                                   SGDPROCUNITHEADER *vuvn,
                                   SGDPROCUNITHEADER *mesh,
                                   const float *local_world)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_CPU);
    DrawRuntimeMesh(owner, vuvn, mesh, local_world, nullptr, nullptr);
}

void MioPan_Graph3dDrawRuntimeMeshPost(
    SGDFILEHEADER *owner,
    SGDPROCUNITHEADER *vuvn,
    SGDPROCUNITHEADER *mesh,
    const float *local_world,
    const DVECTOR *post_vuvn)
{
    MioPanProfileScope profile(MIOPAN_PROFILE_MESH_CPU);
    const float *post_positions = nullptr;
    const float *post_normals = nullptr;
    int          vertex_count;

    if (vuvn == nullptr)
    {
        return;
    }

    vertex_count = vuvn->VUVNDesc.sNumVertex;
    if (post_vuvn != nullptr && vertex_count > 0)
    {
        s_cachedPostVUVNPositions.resize((size_t)vertex_count * 4);
        s_cachedPostVUVNNormals.resize((size_t)vertex_count * 4);
        for (int i = 0; i < vertex_count; i++)
        {
            std::memcpy(&s_cachedPostVUVNPositions[(size_t)i * 4],
                        post_vuvn[i][0], sizeof(post_vuvn[i][0]));
            std::memcpy(&s_cachedPostVUVNNormals[(size_t)i * 4],
                        post_vuvn[i][1], sizeof(post_vuvn[i][1]));
        }
        s_cachedPostVUVNSource = vuvn;
    }

    /*
     * Family 0x42 is the original VU "common" path.  It deliberately carries
     * no VUVN transfer and reuses the packet uploaded by the preceding mesh.
     * Keep a host-owned copy so the emulated DMA ring may safely advance.
     */
    if (s_cachedPostVUVNSource == vuvn &&
        s_cachedPostVUVNPositions.size() >= (size_t)vertex_count * 4)
    {
        post_positions = s_cachedPostVUVNPositions.data();
        post_normals   = s_cachedPostVUVNNormals.data();
    }

    DrawRuntimeMesh(owner, vuvn, mesh, local_world,
                    post_positions, post_normals);
}

extern "C" void MioPan_Graph3dUploadGsImage(SGDPROCUNITHEADER *image_unit)
{
    const unsigned char *packet;
    const unsigned char *packet_end;
    sceGsLoadImage       load{};
    bool                 have_bitbltbuf = false;
    bool                 have_trxpos = false;
    bool                 have_trxreg = false;
    bool                 have_trxdir = false;
    int                  qword_count;

    if (image_unit == nullptr || image_unit->iCategory != SPC_GSIMAGE)
    {
        return;
    }

    qword_count = image_unit->GSImageDesc.iQWordSize;
    if (qword_count <= 0 || qword_count > 0x100000)
    {
        return;
    }

    packet = (const unsigned char *)&image_unit[1];
    packet_end = packet + (size_t)qword_count * 16;

    /*
     * An SGD GS-image unit is a VIF DIRECT packet containing a regular GIF
     * host-to-local transfer: a PACKED A+D register list followed by an IMAGE
     * GIFtag and its texel qwords.  The PS2 consumed this when the VIF1 DMA
     * chain ran.  The host DMA shim only records the reference, so decode the
     * same register stream here and mirror the transfer into host GS memory.
     */
    for (const unsigned char *qw = packet; qw + 16 <= packet_end; qw += 16)
    {
        uint64_t data;
        uint64_t high;
        unsigned int address;

        std::memcpy(&data, qw, sizeof(data));
        std::memcpy(&high, qw + 8, sizeof(high));
        address = (unsigned int)(high & 0x7f);

        switch (address)
        {
        case SCE_GS_BITBLTBUF:
            std::memcpy(&load.bitbltbuf, &data, sizeof(data));
            load.bitbltbufaddr = SCE_GS_BITBLTBUF;
            have_bitbltbuf = true;
            break;
        case SCE_GS_TRXPOS:
            std::memcpy(&load.trxpos, &data, sizeof(data));
            load.trxposaddr = SCE_GS_TRXPOS;
            have_trxpos = true;
            break;
        case SCE_GS_TRXREG:
            std::memcpy(&load.trxreg, &data, sizeof(data));
            load.trxregaddr = SCE_GS_TRXREG;
            have_trxreg = true;
            break;
        case SCE_GS_TRXDIR:
            std::memcpy(&load.trxdir, &data, sizeof(data));
            load.trxdiraddr = SCE_GS_TRXDIR;
            have_trxdir = true;
            break;
        default:
            break;
        }

        const unsigned int gif_mode = (unsigned int)((data >> 58) & 3);
        const unsigned int image_qwords = (unsigned int)(data & 0x7fff);
        if (gif_mode == SCE_GIF_IMAGE && image_qwords != 0 &&
            qw + 16 + (size_t)image_qwords * 16 <= packet_end &&
            have_bitbltbuf && have_trxpos && have_trxreg && have_trxdir &&
            load.trxdir.XDR == 0 && load.trxreg.RRW != 0 &&
            load.trxreg.RRH != 0)
        {
            std::memcpy(&load.giftag1, qw, sizeof(load.giftag1));
            MioPan_GsUpload(&load, (unsigned char *)(qw + 16));
            return;
        }
    }
}
