/* ==========================================================================
 *  miopan_xbox_convert.cpp  --  Xbox Director's Cut models into PS2 ones
 *
 *  WHAT THE TWO FORMATS SHARE.  The Xbox port kept the PS2 scene graph: the
 *  same SGD tree (version 0x1070 against the PS2's 0x1050), the same
 *  coordinates and bounding boxes, the same skinning pools (weighted vertices
 *  with their bone pairs, the vertex lists that drive CPU skinning) and the
 *  same unit chains -- COORD, BBOX, MAT, then the meshes.  What it replaced is
 *  everything the VU1 did:
 *
 *    * A PS2 mesh is a VUVN + MESH unit pair, a VIF packet for the VU1: a
 *      header qword [type, first strip, GIF address, strip count], the
 *      vertex/normal pairs (inline for rigid meshes, as indices into the SGD's
 *      pools otherwise), then per strip a point count and its STs.  The Xbox
 *      one is a D3D vertex buffer in the .ppd plus a stitched strip in the .mdl.
 *    * PS2 textures are TIM2s (PSMT8 + a CSM1 CLUT, alpha 0..0x80) streamed into
 *      VRAM at fixed TBP/CBPs; Xbox ones are swizzled P8 + D3DCOLOR palettes in
 *      an XPR, with the bits in the .ppd.
 *    * PS2 materials carry a prebuilt GIF packet with their TEX0; Xbox
 *      materials carry a texture index.
 *
 *  Mesh kinds, Xbox -> PS2 (counted over every costume pair the two discs
 *  share):
 *
 *      cat 1 fmt 0  pos+normal          ->  VUVN preset + MESH 0x80
 *      cat 1 fmt 1  pos+normal+uv       ->  VUVN preset + MESH 0x82
 *      cat 0 fmt 1  unique-pool indices ->  VUVN type 0 + MESH 0x02
 *      cat 0 fmt 5  skinned indices     ->  VUVN type 4 + MESH 0x0a
 *
 *  HOW IT WAS CHECKED.  Textures convert bit-exactly: on ch000_mio, which both
 *  discs carry, all eight images and all 2048 CLUT entries match the PS2 file
 *  once unswizzled, and the mono (sepia) CLUTs are (r+g+b)/3 with alpha kept --
 *  2048/2048 again.  The geometry is a later revision than the Feb 2004
 *  prototype's, so it was compared by shape: converted ch001_may matches the
 *  PS2 file's 1533 rigid triangles one for one, winding included, ch000_mio
 *  1766 of 1767, and every pool-indexed unit of both matches exactly.  Every
 *  PS2 packet template below is the one all 8263 character and item meshes on
 *  the PS2 disc carry.
 *
 *  Errors do not throw: every read is bounds-checked and sets a sticky error,
 *  and each stage checks it before going on.
 * ======================================================================== */

#include "miopan_xbox_convert.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace MioPanXboxConvert
{
namespace
{

/* ---------------------------------------------------------------------------
 *  Bytes in and out
 * ------------------------------------------------------------------------ */

struct Ctx
{
    std::string error;
    int strips = 0;
    int batches = 0;

    bool ok() const
    {
        return error.empty();
    }
    void Fail(const char *fmt, long long a = 0, long long b = 0)
    {
        if (error.empty())
        {
            char text[160];
            std::snprintf(text, sizeof(text), fmt, a, b);
            error = text;
        }
    }
};

uint32_t U32(Ctx &c, const Bytes &b, size_t o)
{
    if (o + 4 > b.size())
    {
        c.Fail("read past the end of the file at %#llx", (long long)o);
        return 0;
    }
    return (uint32_t)b[o] | ((uint32_t)b[o + 1] << 8) |
           ((uint32_t)b[o + 2] << 16) | ((uint32_t)b[o + 3] << 24);
}

uint16_t U16(Ctx &c, const Bytes &b, size_t o)
{
    if (o + 2 > b.size())
    {
        c.Fail("read past the end of the file at %#llx", (long long)o);
        return 0;
    }
    return (uint16_t)(b[o] | (b[o + 1] << 8));
}

float F32(Ctx &c, const Bytes &b, size_t o)
{
    const uint32_t v = U32(c, b, o);
    float f;
    std::memcpy(&f, &v, 4);
    return f;
}

bool Tag(const Bytes &b, size_t o, const char *tag)
{
    const size_t n = std::strlen(tag);
    return o + n <= b.size() && std::memcmp(&b[o], tag, n) == 0;
}

size_t Align(size_t n, size_t a = 16)
{
    return (n + a - 1) & ~(a - 1);
}

void Pad16(Bytes &out)
{
    out.resize(Align(out.size()), 0);
}

void Put32(Bytes &out, uint32_t v)
{
    out.push_back((uint8_t)v);
    out.push_back((uint8_t)(v >> 8));
    out.push_back((uint8_t)(v >> 16));
    out.push_back((uint8_t)(v >> 24));
}

void Put16(Bytes &out, uint16_t v)
{
    out.push_back((uint8_t)v);
    out.push_back((uint8_t)(v >> 8));
}

void Put64(Bytes &out, uint64_t v)
{
    Put32(out, (uint32_t)v);
    Put32(out, (uint32_t)(v >> 32));
}

void PutF(Bytes &out, float f)
{
    uint32_t v;
    std::memcpy(&v, &f, 4);
    Put32(out, v);
}

void Poke32(Bytes &out, size_t o, uint32_t v)
{
    out[o] = (uint8_t)v;
    out[o + 1] = (uint8_t)(v >> 8);
    out[o + 2] = (uint8_t)(v >> 16);
    out[o + 3] = (uint8_t)(v >> 24);
}

void Append(Bytes &out, const Bytes &b, size_t from, size_t to)
{
    out.insert(out.end(), b.begin() + (std::ptrdiff_t)from, b.begin() + (std::ptrdiff_t)to);
}

/* ---------------------------------------------------------------------------
 *  Xbox containers
 *
 *  .mdl  record pak ('pk3'): count, magic, 0x10, size; records 16-aligned,
 *        each behind [size, type, 0x10, 0].  Type 5 is the model pack ('pk1',
 *        one record per SGD unit), type 6 the 'xvd' XPR0 texture headers.
 *  .pk2  ('pk2'): count, magic, then byte offsets from word 4.  Entry 0 is
 *        the 'xvd' record, entry 1 the SGD.
 *  .ppd  'pk2' as well.  Every entry is [0x10, 'xpd', size, skip] + skip
 *        bytes of padding + data: entry 0 is the texture bits, entry k the
 *        vertex buffer of SGD k-1.
 * ------------------------------------------------------------------------ */

struct Record
{
    size_t off;
    uint32_t size;
    uint32_t kind;
};

std::vector<Record> XboxRecords(Ctx &c, const Bytes &b, size_t base)
{
    std::vector<Record> out;
    const uint32_t n = U32(c, b, base);
    size_t off = base + 0x10;
    for (uint32_t i = 0; i < n && c.ok(); i++)
    {
        const uint32_t size = U32(c, b, off);
        const uint32_t kind = U32(c, b, off + 4);
        if (off + 16 + size > b.size())
        {
            c.Fail("record %lld runs past the end of the file", i);
            break;
        }
        out.push_back({off + 16, size, kind});
        off = Align(off + 16 + size);
    }
    return out;
}

std::vector<uint32_t> XboxPk2(Ctx &c, const Bytes &b)
{
    std::vector<uint32_t> out;
    const uint32_t n = U32(c, b, 0);
    if (n > 4096)
    {
        c.Fail("implausible entry count %lld", n);
        return out;
    }
    for (uint32_t i = 0; i < n && c.ok(); i++)
    {
        out.push_back(U32(c, b, 16 + 4 * i));
    }
    return out;
}

struct Span
{
    size_t start;
    size_t end;
};

std::vector<Span> PpdEntries(Ctx &c, const Bytes &p)
{
    std::vector<Span> out;
    for (uint32_t o : XboxPk2(c, p))
    {
        if (!Tag(p, o + 4, "xpd"))
        {
            c.Fail("ppd entry at %#llx is not 'xpd'", o);
            break;
        }
        const uint32_t size = U32(c, p, o + 8);
        const uint32_t skip = U32(c, p, o + 12);
        if ((size_t)o + 16 + size > p.size() || skip > size)
        {
            c.Fail("ppd entry at %#llx runs past the end of the file", o);
            break;
        }
        out.push_back({(size_t)o + 16 + skip, (size_t)o + 16 + size});
    }
    return out;
}

struct XTexture
{
    uint32_t data;
    int format;
    int w;
    int h;
    int levels;
    bool has_pal;
    uint32_t pal;
    int pal_colors;
};

/* The 'xvd' record: [0x18, 'xvd', size] + an XPR0 whose resources are
 * D3DTexture (20 bytes) and D3DPalette (12 bytes) headers, 0xffffffff-ended.
 * Data offsets are relative to the texture bits in .ppd entry 0. */
std::vector<XTexture> XprTextures(Ctx &c, const Bytes &b, size_t o)
{
    std::vector<XTexture> out;
    if (!Tag(b, o + 4, "xvd") || !Tag(b, o + 12, "XPR0"))
    {
        c.Fail("texture record at %#llx is not an 'xvd' XPR0", (long long)o);
        return out;
    }
    size_t p = o + 24;
    while (c.ok())
    {
        const uint32_t common = U32(c, b, p);
        if (common == 0xffffffffu)
        {
            break;
        }
        const uint32_t kind = (common >> 16) & 7;
        if (kind == 4)                          /* D3DCOMMON_TYPE_TEXTURE */
        {
            const uint32_t data = U32(c, b, p + 4);
            const uint32_t fmt = U32(c, b, p + 12);
            out.push_back({data, (int)((fmt >> 8) & 0xff), 1 << ((fmt >> 20) & 0xf),
                           1 << ((fmt >> 24) & 0xf), (int)((fmt >> 16) & 0xf),
                           false, 0, 256});
            p += 20;
        }
        else if (kind == 3)                     /* D3DCOMMON_TYPE_PALETTE */
        {
            if (out.empty())
            {
                c.Fail("palette before any texture");
                break;
            }
            out.back().has_pal = true;
            out.back().pal = U32(c, b, p + 4);
            out.back().pal_colors = 256 >> ((common >> 30) & 3);
            p += 12;
        }
        else
        {
            c.Fail("XPR resource type %lld is not handled", kind);
        }
    }
    return out;
}

/* ---------------------------------------------------------------------------
 *  Textures
 * ------------------------------------------------------------------------ */

constexpr int kXboxP8 = 0x0b;
constexpr uint64_t kPsmt8 = 0x13;
constexpr int kVramWindowBlocks = 0x4000 - 0x2bc0; /* SendEneVram's window */

/* PSMT8 block arrangement inside a 128x64 page. */
constexpr int kPsmt8Block[4][8] = {
    {0, 1, 4, 5, 16, 17, 20, 21},
    {2, 3, 6, 7, 18, 19, 22, 23},
    {8, 9, 12, 13, 24, 25, 28, 29},
    {10, 11, 14, 15, 26, 27, 30, 31},
};

/* TBW in 64-pixel units; PSMT8 buffers must be an even number of them, which
 * is why every PS2 texture narrower than 256 carries TBW 2. */
int Psmt8Tbw(int w)
{
    return std::max(2, (w + 127) / 128 * 2);
}

/* Blocks a PSMT8 texture touches from its TBP0.  The PS2 exporter just added
 * w*h/256 per texture, which is the same number for every shape it ever met;
 * a 64x256 image (the Xbox kagerou costume has one) only fills the left half
 * of each page and would overlap its successor that way. */
int Psmt8Span(int w, int h)
{
    const int pages_per_row = Psmt8Tbw(w) / 2;
    int top = 0;
    for (int by = 0; by < std::max(1, h / 16); by++)
    {
        for (int bx = 0; bx < std::max(1, w / 16); bx++)
        {
            const int page = (by / 4) * pages_per_row + bx / 8;
            top = std::max(top, page * 32 + kPsmt8Block[by % 4][bx % 8]);
        }
    }
    return top + 1;
}

int Log2(int v)
{
    int n = 0;
    while ((1 << (n + 1)) <= v)
    {
        n++;
    }
    return n;
}

uint64_t Tex0(int tbp, int tbw, int w, int h, int cbp, int tcc)
{
    return (uint64_t)tbp | ((uint64_t)tbw << 14) | (kPsmt8 << 20) |
           ((uint64_t)Log2(w) << 26) | ((uint64_t)Log2(h) << 30) |
           ((uint64_t)tcc << 34) | ((uint64_t)cbp << 37) | (1ull << 61); /* CLD 1 */
}

/* Xbox textures are Morton-ordered: x and y bits interleave from bit 0, x
 * first, until the shorter side runs out. */
Bytes Unswizzle(const uint8_t *src, int w, int h)
{
    std::vector<int> xbits;
    std::vector<int> ybits;
    int bit = 0;
    for (int sx = 1, sy = 1; sx < w || sy < h;)
    {
        if (sx < w)
        {
            xbits.push_back(bit++);
            sx <<= 1;
        }
        if (sy < h)
        {
            ybits.push_back(bit++);
            sy <<= 1;
        }
    }
    auto spread = [](int v, const std::vector<int> &bits) {
        int o = 0;
        for (size_t i = 0; i < bits.size(); i++)
        {
            if (v & (1 << i))
            {
                o |= 1 << bits[i];
            }
        }
        return o;
    };
    std::vector<int> xs((size_t)w);
    for (int x = 0; x < w; x++)
    {
        xs[(size_t)x] = spread(x, xbits);
    }
    Bytes out((size_t)w * (size_t)h);
    for (int y = 0; y < h; y++)
    {
        const int yo = spread(y, ybits);
        for (int x = 0; x < w; x++)
        {
            out[(size_t)y * (size_t)w + (size_t)x] = src[yo | xs[(size_t)x]];
        }
    }
    return out;
}

/* GS CSM1 CLUT order: entries 8-15 and 16-23 of every 32 swap places. */
int Csm1(int i)
{
    return (i & ~0x18) | ((i & 8) << 1) | ((i & 0x10) >> 1);
}

/* D3DCOLOR (B, G, R, A) -> PSMCT32 (R, G, B, A) with PS2 alpha 0..0x80, stored
 * in CSM1 order.  (a + 1) / 2 reproduces every PS2 entry exactly. */
void ConvertPalette(const uint8_t *pal, int colors, Bytes &clut, Bytes &mono)
{
    clut.assign(1024, 0);
    mono.assign(1024, 0);
    for (int i = 0; i < 256; i++)
    {
        uint8_t r = 0, g = 0, b = 0, a = 0;
        if (i < colors)
        {
            b = pal[4 * i];
            g = pal[4 * i + 1];
            r = pal[4 * i + 2];
            a = (uint8_t)((pal[4 * i + 3] + 1) / 2);
        }
        const size_t j = (size_t)Csm1(i) * 4;
        const uint8_t y = (uint8_t)((r + g + b) / 3);
        clut[j] = r;
        clut[j + 1] = g;
        clut[j + 2] = b;
        clut[j + 3] = a;
        mono[j] = y;
        mono[j + 1] = y;
        mono[j + 2] = y;
        mono[j + 3] = a;
    }
}

/* A one-picture TIM2: 8-bit indexed (ImageType 5), 32-bit CLUT (ClutType 3),
 * the same header every PS2 model texture carries.  The BWC (mono) copy is the
 * same thing with the image left out. */
Bytes Tim2(const Bytes &image, int w, int h, const Bytes &clut, uint64_t tex0)
{
    Bytes out;
    const char magic[] = {'T', 'I', 'M', '2', 4, 0, 1, 0};
    out.insert(out.end(), magic, magic + 8);
    out.resize(16, 0);
    const uint32_t img = (uint32_t)image.size();
    Put32(out, 0x30 + img + (uint32_t)clut.size());
    Put32(out, (uint32_t)clut.size());
    Put32(out, img);
    Put16(out, 0x30);
    Put16(out, 256);
    out.push_back(0);                   /* PictFormat */
    out.push_back(1);                   /* MipMapTextures */
    out.push_back(3);                   /* ClutType: PSMCT32 */
    out.push_back(5);                   /* ImageType: 8-bit indexed */
    Put16(out, (uint16_t)(img ? w : 0));
    Put16(out, (uint16_t)(img ? h : 0));
    Put64(out, tex0);
    Put64(out, 0x260);                  /* TEX1: bilinear */
    Put32(out, 0);
    Put32(out, 0);
    out.insert(out.end(), image.begin(), image.end());
    out.insert(out.end(), clut.begin(), clut.end());
    return out;
}

struct Textures
{
    std::vector<Bytes> normal;
    std::vector<Bytes> mono;
    std::vector<uint64_t> mesh_tex0;
    int blocks = 0;
};

Textures ConvertTextures(Ctx &c, const std::vector<XTexture> &textures,
                         const Bytes &ppd, Span bits)
{
    Textures out;
    std::vector<int> placed;
    int tbp = 0;
    for (size_t i = 0; i < textures.size(); i++)
    {
        const XTexture &t = textures[i];
        if (t.format != kXboxP8)
        {
            c.Fail("texture %lld has D3D format %#llx; only P8 is handled", (long long)i,
                   t.format);
            return out;
        }
        if (!t.has_pal)
        {
            c.Fail("texture %lld has no palette", (long long)i);
            return out;
        }
        placed.push_back(tbp);
        tbp += Psmt8Span(t.w, t.h);
    }
    const int cbp0 = tbp;
    out.blocks = cbp0 + 4 * (int)textures.size();
    if (out.blocks > kVramWindowBlocks)
    {
        c.Fail("textures need %lld VRAM blocks; the character window holds %lld",
               out.blocks, kVramWindowBlocks);
        return out;
    }

    for (size_t i = 0; i < textures.size(); i++)
    {
        const XTexture &t = textures[i];
        const size_t img = bits.start + t.data;
        const size_t pal = bits.start + t.pal;
        if (img + (size_t)t.w * (size_t)t.h > bits.end ||
            pal + 4 * (size_t)t.pal_colors > bits.end)
        {
            c.Fail("texture %lld lies outside the texture data", (long long)i);
            return out;
        }
        const int tbw = Psmt8Tbw(t.w);
        const int cbp = cbp0 + 4 * (int)i;
        const Bytes image = Unswizzle(&ppd[img], t.w, t.h);
        Bytes clut;
        Bytes mono;
        ConvertPalette(&ppd[pal], t.pal_colors, clut, mono);
        const uint64_t file_tex0 = Tex0(placed[i], tbw, t.w, t.h, cbp, 0);
        out.normal.push_back(Tim2(image, t.w, t.h, clut, file_tex0));
        out.mono.push_back(Tim2(Bytes(), t.w, t.h, mono, file_tex0));
        out.mesh_tex0.push_back(Tex0(placed[i], tbw, t.w, t.h, cbp, 1));
    }
    return out;
}

/* ---------------------------------------------------------------------------
 *  Strips
 * ------------------------------------------------------------------------ */

using Strip = std::vector<int>;

bool Degenerate(int a, int b, int c)
{
    return a == b || b == c || a == c;
}

/* The Xbox stitches every mesh's strips into one, joining them with degenerate
 * triangles and an extra duplicate wherever the next strip would otherwise
 * start on an odd triangle.  Cut it back into strips at every degenerate
 * triangle.  A run that starts on an odd triangle keeps its winding by gaining
 * one leading duplicate, so its first real triangle lands on an odd slot
 * again. */
std::vector<Strip> SplitStrips(const std::vector<int> &idx)
{
    std::vector<Strip> strips;
    const size_t n = idx.size();
    size_t t = 0;
    while (t + 2 < n)
    {
        if (Degenerate(idx[t], idx[t + 1], idx[t + 2]))
        {
            t++;
            continue;
        }
        const size_t start = t;
        while (t + 3 < n && !Degenerate(idx[t + 1], idx[t + 2], idx[t + 3]))
        {
            t++;
        }
        Strip strip(idx.begin() + (std::ptrdiff_t)start, idx.begin() + (std::ptrdiff_t)(t + 3));
        if (start & 1)
        {
            strip.insert(strip.begin(), strip[0]);
        }
        strips.push_back(std::move(strip));
        t++;
    }
    return strips;
}

/* A strip longer than one batch can hold is cut into overlapping pieces.
 * Pieces advance by an even count so each starts on an even triangle. */
std::vector<Strip> ChopStrip(const Strip &strip, int limit)
{
    if ((int)strip.size() <= limit)
    {
        return {strip};
    }
    const int step = (limit - 2) & ~1;
    std::vector<Strip> out;
    for (size_t o = 0; o + 2 < strip.size(); o += (size_t)step)
    {
        const size_t end = std::min(strip.size(), o + (size_t)step + 2);
        out.emplace_back(strip.begin() + (std::ptrdiff_t)o, strip.begin() + (std::ptrdiff_t)end);
    }
    return out;
}

/* ---------------------------------------------------------------------------
 *  PS2 units
 * ------------------------------------------------------------------------ */

constexpr int kVuMaxVerts = 90;     /* largest batch on the PS2 disc */
constexpr int kVuMaxQwords = 454;   /* highest VU address any batch reaches */

constexpr uint32_t kVifStcycl44 = 0x01000404;
constexpr uint32_t kVifStcycl31 = 0x01000103;
constexpr uint32_t kVifStmask = 0x20000000;

struct Batch
{
    bool textured;
    int stride;
    std::vector<Strip> strips;
    int verts = 0;
    int qwords;

    Batch(bool tex) : textured(tex), stride(tex ? 3 : 2), qwords(1 + (tex ? 2 : 0))
    {
    }
    int Cost(int count) const
    {
        return 2 * count + 1 + stride * count;
    }
    bool Fits(int count) const
    {
        return verts + count <= kVuMaxVerts && qwords + Cost(count) <= kVuMaxQwords;
    }
    void Add(const Strip &s)
    {
        strips.push_back(s);
        verts += (int)s.size();
        qwords += Cost((int)s.size());
    }
};

std::vector<Batch> MakeBatches(const std::vector<Strip> &strips, bool textured)
{
    const Batch probe(textured);
    int limit = kVuMaxVerts;
    while (!probe.Fits(limit))
    {
        limit--;
    }
    std::vector<Batch> batches;
    Batch cur(textured);
    for (const Strip &s : strips)
    {
        for (const Strip &piece : ChopStrip(s, limit))
        {
            if (!cur.Fits((int)piece.size()))
            {
                batches.push_back(std::move(cur));
                cur = Batch(textured);
            }
            cur.Add(piece);
        }
    }
    if (!cur.strips.empty())
    {
        batches.push_back(std::move(cur));
    }
    return batches;
}

struct VuLayout
{
    int gif;
    int first;
    std::vector<int> addrs;
};

VuLayout Layout(const Batch &batch)
{
    VuLayout l;
    l.gif = 1 + 2 * batch.verts;
    l.first = batch.textured ? l.gif + 2 : l.gif;
    int a = l.first;
    for (const Strip &s : batch.strips)
    {
        l.addrs.push_back(a);
        a += 1 + batch.stride * (int)s.size();
    }
    return l;
}

/* A process unit: [pNext, iCategory, 8-byte descriptor] + payload, padded to a
 * quadword.  pNext is the unit's own size. */
void Unit(Bytes &out, int category, const uint8_t desc[8], Bytes body)
{
    Pad16(body);
    Put32(out, (uint32_t)(16 + body.size()));
    Put32(out, (uint32_t)category);
    out.insert(out.end(), desc, desc + 8);
    out.insert(out.end(), body.begin(), body.end());
}

void MatUnit(Bytes &out, uint32_t index)
{
    Put32(out, 16);
    Put32(out, 2);
    Put32(out, index);
    Put32(out, 0);
}

void VuHeader(Bytes &out, uint32_t x, const Batch &batch)
{
    const VuLayout l = Layout(batch);
    Put32(out, x);
    Put32(out, (uint32_t)l.first);
    Put32(out, (uint32_t)l.gif);
    Put32(out, (uint32_t)batch.strips.size());
}

struct Vertex
{
    uint32_t pos[3];
    uint32_t nrm[3];
    uint32_t uv[2];
};

/* MESH: [GIF tag + TEX0 if textured], per strip its point count, then per
 * strip its STs one qword in three (STCYCL CL=3 WL=1) -- the other two are the
 * VU's scratch for that vertex. */
void MeshUnit(Bytes &out, const Batch &batch, uint8_t mesh_type, uint64_t tex0,
              const std::vector<const Vertex *> &seq)
{
    const VuLayout l = Layout(batch);
    Bytes body;
    if (batch.textured)
    {
        Put32(body, kVifStcycl44);
        Put32(body, 0x6c028000u | (uint32_t)l.gif);
        Put32(body, 0x00000001);                /* GIF tag: NLOOP 1, A+D */
        Put32(body, 0x10000000);
        Put32(body, 0x0000000e);
        Put32(body, 0);
        Put64(body, tex0);
        Put32(body, 0x06);                      /* TEX0_1 */
        Put32(body, 0);
    }
    Put32(body, kVifStmask);
    Put32(body, 0xfcfcfcfc);
    for (size_t i = 0; i < batch.strips.size(); i++)
    {
        Put32(body, 0x70018000u | (uint32_t)l.addrs[i]);
        Put32(body, (uint32_t)batch.strips[i].size());
    }
    if (batch.textured)
    {
        Put32(body, kVifStmask);
        Put32(body, 0xd0d0d0d0);
        Put32(body, kVifStcycl31);
        size_t k = 0;
        for (size_t i = 0; i < batch.strips.size(); i++)
        {
            const uint32_t n = (uint32_t)batch.strips[i].size();
            Put32(body, 0x74008000u | (n << 16) | (uint32_t)(l.addrs[i] + 1));
            for (uint32_t j = 0; j < n; j++, k++)
            {
                Put32(body, seq[k]->uv[0]);
                Put32(body, seq[k]->uv[1]);
            }
        }
    }
    Pad16(body);
    const int qwc = (int)(body.size() / 16);
    const uint8_t desc[8] = {(uint8_t)qwc, (uint8_t)(qwc >> 8), (uint8_t)(qwc >> 16),
                             (uint8_t)(qwc >> 24), 0, mesh_type,
                             (uint8_t)batch.strips.size(), 0};
    Unit(out, 1, desc, std::move(body));
}

/* Rigid geometry inline: the header qword, then V3-32 vertex/normal pairs
 * with W from the row register (STMASK 0x40404040). */
void VuvnPresetUnit(Bytes &out, const Batch &batch, const std::vector<const Vertex *> &seq)
{
    const int n = batch.verts;
    Bytes body;
    Put32(body, kVifStcycl44);
    Put32(body, 0x6c018000);
    VuHeader(body, batch.textured ? 0x12 : 0x10, batch);
    Put32(body, kVifStcycl44);
    Put32(body, kVifStmask);
    Put32(body, 0x40404040);
    Put32(body, 0x78008001u | ((uint32_t)(2 * n) << 16));
    for (const Vertex *v : seq)
    {
        for (uint32_t w : v->pos)
        {
            Put32(body, w);
        }
        for (uint32_t w : v->nrm)
        {
            Put32(body, w);
        }
    }
    Pad16(body);
    const uint8_t desc[8] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)n, (uint8_t)(n >> 8),
                             (uint8_t)(body.size() / 16), 0, 5, 0};
    Unit(out, 0, desc, std::move(body));
}

/* Pool geometry: SetVUVNData() builds the packet at draw time out of these
 * (vertex id, normal id) pairs, so the file holds only the two template
 * qwords and the ids. */
void VuvnIndexedUnit(Bytes &out, const Batch &batch, uint8_t vector_type,
                     const std::vector<std::pair<uint16_t, uint16_t>> &ids)
{
    const int n = batch.verts;
    Bytes body;
    Put32(body, 0);
    Put32(body, 0);
    Put32(body, kVifStcycl44);
    Put32(body, 0x6c008000u | ((uint32_t)(2 * n + 1) << 16));
    VuHeader(body, 1, batch);
    for (const auto &id : ids)
    {
        Put32(body, id.first);
        Put32(body, id.second);
    }
    const uint8_t desc[8] = {(uint8_t)n, (uint8_t)(n >> 8), (uint8_t)n, (uint8_t)(n >> 8),
                             (uint8_t)(2 * n + 3), vector_type, 6, 0};
    Unit(out, 0, desc, std::move(body));
}

/* ---------------------------------------------------------------------------
 *  SGD
 * ------------------------------------------------------------------------ */

constexpr uint32_t kXboxSgdVersion = 0x1070;
constexpr uint32_t kPs2SgdVersion = 0x1050;
constexpr size_t kXboxMat = 0x90;
constexpr size_t kPs2Mat = 0xb0;

void ConvertMaterial(Ctx &c, const Bytes &b, size_t o, const std::vector<uint64_t> &tex0s,
                     Bytes &out, bool &textured)
{
    const int32_t tex_index = (int32_t)U32(c, b, o + 0x48);
    textured = tex_index >= 0;
    if (textured && tex_index >= (int32_t)tex0s.size())
    {
        c.Fail("material names texture %lld of %lld", tex_index, (long long)tex0s.size());
        return;
    }
    const uint64_t t0 = textured ? tex0s[(size_t)tex_index] : 0;

    char name[12] = {};
    for (size_t i = 0; i < 11 && o + 0x4c + i < b.size() && b[o + 0x4c + i] != 0; i++)
    {
        name[i] = (char)b[o + 0x4c + i];
    }

    float spec[3];
    for (int i = 0; i < 3; i++)
    {
        spec[i] = F32(c, b, o + 0x20 + 4 * (size_t)i);
    }
    /* The PS2 specular power word is not in the Xbox file (which says 60.0
     * everywhere).  On the disc it is 0 whenever the specular colour is 0 and
     * 50 for 12 of the 15 materials that have one; use that. */
    const uint32_t power = std::max({spec[0], spec[1], spec[2]}) > 0.0f ? 50 : 0;

    const size_t start = out.size();
    Put32(out, textured ? 1 : 0);
    out.insert(out.end(), name, name + 12);
    for (int i = 0; i < 3; i++)                         /* vAmbient */
    {
        Put32(out, U32(c, b, o + 0x10 + 4 * (size_t)i));
    }
    PutF(out, 0.0f);
    for (int i = 0; i < 3; i++)                         /* vDiffuse */
    {
        Put32(out, U32(c, b, o + 4 * (size_t)i));
    }
    PutF(out, F32(c, b, o + 12) * 128.0f);
    for (int i = 0; i < 3; i++)                         /* vSpecular + power */
    {
        Put32(out, U32(c, b, o + 0x20 + 4 * (size_t)i));
    }
    Put32(out, power);
    for (int i = 0; i < 4; i++)                         /* vEmission */
    {
        Put32(out, U32(c, b, o + 0x30 + 4 * (size_t)i));
    }
    /* iCacheStatus, iTagAddressOld, iSizeOld, then DIRECT 2: the material's own
     * GIF packet (NLOOP 1 EOP, A+D TEX0) and the TEX1/MIPTBP slots. */
    const uint32_t cache[] = {0, 0, 0, 0x51000002, 0x00008001, 0x10000000, 0x0000000e, 0};
    for (uint32_t w : cache)
    {
        Put32(out, w);
    }
    Put64(out, t0);
    Put32(out, 0x06);
    Put32(out, 0);
    for (uint32_t reg : {0x14u, 0x34u, 0x36u})
    {
        Put32(out, 0);
        Put32(out, 0);
        Put32(out, reg);
        Put32(out, 0);
    }
    if (out.size() - start != kPs2Mat)
    {
        c.Fail("material came out %lld bytes", (long long)(out.size() - start));
    }
}

/* Xbox mesh unit: [next, cat, 0, vb offset, 0, vertices, 0, index offset, 0,
 * indices, 0, format, -1, 0], indices from the SGD-relative index offset, and
 * for the pool kinds one (vertex id, normal id) u16 pair per vertex after
 * them. */
void ConvertMesh(Ctx &c, const Bytes &b, size_t base, size_t p, const Bytes &ppd, size_t vb,
                 bool textured, uint64_t tex0, Bytes &out)
{
    const uint32_t cat = U32(c, b, p + 4);
    const uint32_t vb_off = U32(c, b, p + 12);
    const uint32_t nverts = U32(c, b, p + 20);
    const uint32_t idx_off = U32(c, b, p + 28);
    const uint32_t nidx = U32(c, b, p + 36);
    const uint32_t fmt = U32(c, b, p + 44);
    if (!c.ok())
    {
        return;
    }

    enum
    {
        kPreset,
        kUnique,
        kWeighted
    } kind;
    size_t stride;
    if (cat == 1 && (fmt == 0 || fmt == 1))
    {
        kind = kPreset;
        stride = fmt == 0 ? 24 : 32;
    }
    else if (cat == 0 && (fmt == 1 || fmt == 5))
    {
        kind = fmt == 1 ? kUnique : kWeighted;
        stride = 32;
    }
    else
    {
        c.Fail("mesh category %lld format %lld is not handled", cat, fmt);
        return;
    }
    if (kind != kPreset && !textured)
    {
        /* The pool kinds are textured everywhere on both discs (MESH 0x02 and
         * 0x0a).  An untextured unique mesh is MESH 0x00, which one PS2 ghost
         * has; an untextured skinned one has no PS2 counterpart to copy. */
        if (kind == kWeighted)
        {
            c.Fail("untextured skinned mesh: no PS2 mesh type for it");
            return;
        }
    }

    std::vector<int> indices(nidx);
    for (uint32_t i = 0; i < nidx; i++)
    {
        indices[i] = U16(c, b, base + idx_off + 2 * (size_t)i);
        if (c.ok() && (uint32_t)indices[i] >= nverts)
        {
            c.Fail("index past the vertex buffer");
        }
    }

    std::vector<Vertex> verts(nverts);
    for (uint32_t i = 0; i < nverts && c.ok(); i++)
    {
        const size_t o = vb + vb_off + stride * i;
        Vertex &v = verts[i];
        for (int k = 0; k < 3; k++)
        {
            v.pos[k] = U32(c, ppd, o + 4 * (size_t)k);
            v.nrm[k] = U32(c, ppd, o + 12 + 4 * (size_t)k);
        }
        v.uv[0] = stride == 32 ? U32(c, ppd, o + 24) : 0;
        v.uv[1] = stride == 32 ? U32(c, ppd, o + 28) : 0;
    }
    std::vector<std::pair<uint16_t, uint16_t>> ids;
    if (kind != kPreset)
    {
        const size_t o = base + idx_off + 2 * (size_t)nidx;
        for (uint32_t i = 0; i < nverts; i++)
        {
            ids.emplace_back(U16(c, b, o + 4 * (size_t)i), U16(c, b, o + 4 * (size_t)i + 2));
        }
    }
    if (!c.ok())
    {
        return;
    }

    const std::vector<Strip> strips = SplitStrips(indices);
    const std::vector<Batch> batches = MakeBatches(strips, textured);
    c.strips += (int)strips.size();
    c.batches += (int)batches.size();

    for (const Batch &batch : batches)
    {
        std::vector<const Vertex *> seq;
        std::vector<std::pair<uint16_t, uint16_t>> seq_ids;
        for (const Strip &s : batch.strips)
        {
            for (int i : s)
            {
                seq.push_back(&verts[(size_t)i]);
                if (kind != kPreset)
                {
                    seq_ids.push_back(ids[(size_t)i]);
                }
            }
        }
        uint8_t mesh_type;
        if (kind == kPreset)
        {
            VuvnPresetUnit(out, batch, seq);
            mesh_type = textured ? 0x82 : 0x80;
        }
        else if (kind == kUnique)
        {
            VuvnIndexedUnit(out, batch, 0, seq_ids);
            mesh_type = textured ? 0x02 : 0x00;
        }
        else
        {
            VuvnIndexedUnit(out, batch, 4, seq_ids);
            mesh_type = 0x0a;
        }
        MeshUnit(out, batch, mesh_type, tex0, seq);
    }
}

/* One Xbox SGD unit -> one PS2 SGD.  Layout of both, in order: header + block
 * table, coordinates (uiNumBlock - 1 of them), materials, vector info, the
 * vector pools, the unit chains.  Only the header and the materials change
 * size, so the pools move as one blob and the offsets in the vector info move
 * by the same delta. */
Bytes ConvertSgd(Ctx &c, const Bytes &b, size_t base, const Bytes &ppd, size_t vb,
                 const std::vector<uint64_t> &tex0s)
{
    Bytes out;
    uint32_t h[10];
    for (int i = 0; i < 10; i++)
    {
        h[i] = U32(c, b, base + 4 * (size_t)i);
    }
    if (!c.ok())
    {
        return out;
    }
    const uint32_t ver = h[0], model_type = h[2], nmat = h[3], size = h[4];
    const uint32_t coord = h[6], mat = h[7], vinfo = h[8], nblk = h[9];
    if (ver != kXboxSgdVersion)
    {
        c.Fail("SGD version %#llx, expected %#llx", ver, kXboxSgdVersion);
        return out;
    }
    if (nblk == 0 || nblk > 512 || nmat > 4096)
    {
        c.Fail("implausible SGD header (%lld blocks, %lld materials)", nblk, nmat);
        return out;
    }
    std::vector<uint32_t> heads(nblk);
    uint32_t chains_start = 0;
    for (uint32_t i = 0; i < nblk; i++)
    {
        heads[i] = U32(c, b, base + 0x28 + 4 * (size_t)i);
        if (heads[i] != 0 && (chains_start == 0 || heads[i] < chains_start))
        {
            chains_start = heads[i];
        }
    }

    const size_t ps2_hdr = Align(0x18 + 4 * (size_t)nblk);
    const size_t xb_hdr = Align(0x28 + 4 * (size_t)nblk);
    const size_t ncoord = coord ? nblk - 1 : 0;
    if (coord && coord != xb_hdr)
    {
        c.Fail("coordinates do not follow the header");
        return out;
    }
    const size_t xb_mat = mat ? mat : xb_hdr + 0xe0 * ncoord;
    if (mat && xb_mat != xb_hdr + 0xe0 * ncoord)
    {
        c.Fail("materials do not follow the coordinates");
        return out;
    }
    const size_t xb_after_mat = xb_mat + kXboxMat * nmat;
    if (vinfo && vinfo != xb_after_mat)
    {
        c.Fail("vector info does not follow the materials");
        return out;
    }
    if (base + xb_after_mat > b.size())
    {
        c.Fail("SGD runs past the end of the file");
        return out;
    }

    out.resize(ps2_hdr, 0);
    const size_t ps2_coord = coord ? out.size() : 0;
    Append(out, b, base + coord, base + coord + 0xe0 * ncoord);
    const size_t ps2_mat = mat ? out.size() : 0;
    std::vector<bool> mat_textured(nmat);
    std::vector<int32_t> mat_texture(nmat);
    for (uint32_t k = 0; k < nmat && c.ok(); k++)
    {
        bool textured = false;
        ConvertMaterial(c, b, base + xb_mat + kXboxMat * k, tex0s, out, textured);
        mat_textured[k] = textured;
        mat_texture[k] = (int32_t)U32(c, b, base + xb_mat + kXboxMat * k + 0x48);
    }

    size_t ps2_vinfo = 0;
    if (vinfo)
    {
        ps2_vinfo = out.size();
        const uint32_t naddr = U32(c, b, base + vinfo);
        const size_t vsize = Align(4 + 16 * (size_t)naddr);
        const size_t blob_end = chains_start ? chains_start : size;
        if (naddr > 16 || vinfo + vsize > blob_end || base + blob_end > b.size())
        {
            c.Fail("vector info is malformed");
            return out;
        }
        const long long delta = (long long)ps2_vinfo - (long long)vinfo;
        const size_t vi = out.size();
        Append(out, b, base + vinfo, base + vinfo + vsize);
        for (uint32_t k = 0; k < naddr; k++)
        {
            for (int f = 1; f < 4; f++)
            {
                const size_t o = 4 + 16 * (size_t)k + 4 * (size_t)f;
                const uint32_t v = U32(c, b, base + vinfo + o);
                if (v == 0)
                {
                    continue;
                }
                if (v < vinfo + vsize || v >= blob_end)
                {
                    c.Fail("vector pool offset %#llx outside the pools", v);
                    return out;
                }
                Poke32(out, vi + o, (uint32_t)((long long)v + delta));
            }
        }
        Append(out, b, base + vinfo + vsize, base + blob_end);
        /* Xbox unit chains are only word aligned; PS2 ones sit on quadwords. */
        Pad16(out);
    }
    else if (chains_start && chains_start != xb_after_mat)
    {
        c.Fail("unexpected data before the unit chains");
        return out;
    }
    if (out.size() % 16)
    {
        c.Fail("SGD sections are not quadword aligned");
        return out;
    }

    std::vector<uint32_t> new_heads(nblk, 0);
    for (uint32_t i = 0; i < nblk && c.ok(); i++)
    {
        if (heads[i] == 0)
        {
            continue;
        }
        new_heads[i] = (uint32_t)out.size();
        size_t p = base + heads[i];
        int64_t cur_mat = -1;
        int64_t last_mat_written = -1;
        bool cur_tex = false;
        uint64_t cur_tex0 = 0;
        for (int guard = 0; c.ok(); guard++)
        {
            const uint32_t next = U32(c, b, p);
            if (next == 0 || !c.ok())
            {
                break;
            }
            if (guard > 100000)
            {
                c.Fail("unit chain does not end");
                break;
            }
            const uint32_t cat = U32(c, b, p + 4);
            if (cat == 3)                               /* COORDINATE */
            {
                if (p + 16 > b.size())
                {
                    c.Fail("coordinate unit runs past the end of the file");
                    break;
                }
                Append(out, b, p, p + 16);
            }
            else if (cat == 4)                          /* BOUNDING_BOX */
            {
                if (p + 0x90 > b.size())
                {
                    c.Fail("bounding box runs past the end of the file");
                    break;
                }
                Append(out, b, p, p + 0x90);
            }
            else if (cat == 2)                          /* MATERIAL */
            {
                const uint32_t m = U32(c, b, p + 8);
                if (m >= nmat)
                {
                    c.Fail("material unit names %lld of %lld", m, nmat);
                    break;
                }
                cur_mat = m;
                cur_tex = mat_textured[m];
                cur_tex0 = cur_tex ? tex0s[(size_t)mat_texture[m]] : 0;
            }
            else if (cat == 0 || cat == 1)
            {
                if (cur_mat < 0)
                {
                    c.Fail("mesh before any material");
                    break;
                }
                if (cur_mat != last_mat_written)
                {
                    MatUnit(out, (uint32_t)cur_mat);
                    last_mat_written = cur_mat;
                }
                ConvertMesh(c, b, base, p, ppd, vb, cur_tex, cur_tex0, out);
            }
            else
            {
                c.Fail("unit category %lld is not handled", cat);
                break;
            }
            p += next;
        }
        out.resize(out.size() + 16, 0);                 /* the terminator unit */
    }
    if (!c.ok())
    {
        return Bytes();
    }

    Poke32(out, 0, kPs2SgdVersion);
    out[4] = 0;                                         /* ucMapFlag */
    out[5] = (uint8_t)model_type;
    out[6] = (uint8_t)nmat;
    out[7] = (uint8_t)(nmat >> 8);
    Poke32(out, 8, (uint32_t)ps2_coord);
    Poke32(out, 12, (uint32_t)ps2_mat);
    Poke32(out, 16, (uint32_t)ps2_vinfo);
    Poke32(out, 20, nblk);
    for (uint32_t i = 0; i < nblk; i++)
    {
        Poke32(out, 0x18 + 4 * (size_t)i, new_heads[i]);
    }
    return out;
}

/* ---------------------------------------------------------------------------
 *  PS2 containers
 * ------------------------------------------------------------------------ */

/* count, then [size, type, 0, 0] + data per record, then a 0xff header -- the
 * terminator MpkMapUnit() and GetFileInPak() walkers stop on. */
Bytes RecordPak(const std::vector<std::pair<uint32_t, Bytes>> &records)
{
    Bytes out;
    Put32(out, (uint32_t)records.size());
    out.resize(16, 0);
    for (const auto &r : records)
    {
        const size_t padded = Align(r.second.size());
        Put32(out, (uint32_t)padded);
        Put32(out, r.first);
        Put32(out, 0);
        Put32(out, 0);
        out.insert(out.end(), r.second.begin(), r.second.end());
        out.resize(out.size() + (padded - r.second.size()), 0);
    }
    out.resize(out.size() + 16, 0xff);
    return out;
}

std::vector<std::pair<uint32_t, Bytes>> Untyped(const std::vector<Bytes> &files)
{
    std::vector<std::pair<uint32_t, Bytes>> out;
    for (const Bytes &f : files)
    {
        out.emplace_back(0, f);
    }
    return out;
}

/* count, three zero words, then byte offsets from word 4. */
Bytes Pk2(const std::vector<Bytes> &entries)
{
    const size_t n = entries.size();
    Bytes out;
    Put32(out, (uint32_t)n);
    out.resize(16, 0);
    size_t off = Align(16 + 4 * n);
    for (const Bytes &e : entries)
    {
        Put32(out, (uint32_t)off);
        off += Align(e.size());
    }
    Pad16(out);
    for (const Bytes &e : entries)
    {
        out.insert(out.end(), e.begin(), e.end());
        Pad16(out);
    }
    return out;
}

constexpr uint32_t kMdlDatMpk = 5;
constexpr uint32_t kMdlDatPk2 = 6;
constexpr uint32_t kMdlDatBwc = 7;

Result Finish(Ctx &c, Bytes data, const Textures &tex, int units)
{
    Result r;
    if (!c.ok())
    {
        r.error = c.error;
        return r;
    }
    r.data = std::move(data);
    r.units = units;
    r.textures = (int)tex.normal.size();
    r.vram_blocks = tex.blocks;
    r.strips = c.strips;
    r.batches = c.batches;
    return r;
}

} // namespace

Result Character(const Bytes &b, const Bytes &p)
{
    Ctx c;
    Textures tex;
    if (!Tag(b, 4, "pk3"))
    {
        c.Fail("not an Xbox character pack");
        return Finish(c, Bytes(), tex, 0);
    }
    const std::vector<Span> spans = PpdEntries(c, p);
    size_t mpk = 0;
    bool have_mpk = false;
    std::vector<XTexture> textures;
    bool have_textures = false;
    for (const Record &r : XboxRecords(c, b, 0))
    {
        if (r.kind == kMdlDatMpk)
        {
            mpk = r.off;
            have_mpk = true;
        }
        else if (r.kind == kMdlDatPk2)
        {
            textures = XprTextures(c, b, r.off);
            have_textures = true;
        }
        else
        {
            c.Fail("record type %lld is not handled", r.kind);
        }
    }
    if (c.ok() && (!have_mpk || !have_textures || spans.empty()))
    {
        c.Fail("model pack or textures missing");
    }
    if (!c.ok())
    {
        return Finish(c, Bytes(), tex, 0);
    }

    tex = ConvertTextures(c, textures, p, spans[0]);
    const std::vector<Record> units = XboxRecords(c, b, mpk);
    if (c.ok() && spans.size() != units.size() + 1)
    {
        c.Fail("%lld SGD units but %lld vertex buffers", (long long)units.size(),
               (long long)spans.size() - 1);
    }
    std::vector<Bytes> sgds;
    for (size_t k = 0; k < units.size() && c.ok(); k++)
    {
        sgds.push_back(ConvertSgd(c, b, units[k].off, p, spans[k + 1].start, tex.mesh_tex0));
    }
    if (!c.ok())
    {
        return Finish(c, Bytes(), tex, 0);
    }
    Bytes data = RecordPak({
        {kMdlDatMpk, RecordPak(Untyped(sgds))},
        {kMdlDatPk2, RecordPak(Untyped(tex.normal))},
        {kMdlDatBwc, RecordPak(Untyped(tex.mono))},
    });
    return Finish(c, std::move(data), tex, (int)sgds.size());
}

Result Item(const Bytes &b, const Bytes &p)
{
    Ctx c;
    Textures tex;
    if (!Tag(b, 4, "pk2"))
    {
        c.Fail("not an Xbox item pack");
        return Finish(c, Bytes(), tex, 0);
    }
    const std::vector<uint32_t> offs = XboxPk2(c, b);
    const std::vector<Span> spans = PpdEntries(c, p);
    if (c.ok() && (offs.size() != 2 || spans.size() != 2))
    {
        c.Fail("an item pack has a texture record and one SGD; this has %lld entries",
               (long long)offs.size());
    }
    if (!c.ok())
    {
        return Finish(c, Bytes(), tex, 0);
    }
    const std::vector<XTexture> textures = XprTextures(c, b, offs[0]);
    if (c.ok())
    {
        tex = ConvertTextures(c, textures, p, spans[0]);
    }
    Bytes sgd;
    if (c.ok())
    {
        sgd = ConvertSgd(c, b, offs[1], p, spans[1].start, tex.mesh_tex0);
    }
    if (!c.ok())
    {
        return Finish(c, Bytes(), tex, 0);
    }
    Bytes data = Pk2({sgd, RecordPak(Untyped(tex.normal)), RecordPak(Untyped(tex.mono))});
    return Finish(c, std::move(data), tex, 1);
}

} // namespace MioPanXboxConvert
