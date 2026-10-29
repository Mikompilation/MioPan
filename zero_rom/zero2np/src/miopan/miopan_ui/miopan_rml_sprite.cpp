#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"
#include "../miopan_memory.h"

#include "../../graphics/graph2d/g2d_draw.h"
#include "../../graphics/graph2d/tim2.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>
#include <vector>

namespace
{
/* TEX0's fields, as the GS packs them. */
unsigned Tbp(u_long tex0)
{
    return (unsigned)(tex0 & 0x3fff);
}

unsigned Psm(u_long tex0)
{
    return (unsigned)(tex0 >> 20 & 0x3f);
}

unsigned Cbp(u_long tex0)
{
    return (unsigned)(tex0 >> 37 & 0x3fff);
}

unsigned Csa(u_long tex0)
{
    return (unsigned)(tex0 >> 56 & 0x1f);
}

constexpr unsigned kPsmT4 = 0x14;

/* The picture in the pak uploaded where the sprite samples. */
TIM2_PICTUREHEADER *FindPicture(uintptr_t pk2, const SPRT_DAT &sprite, int only)
{
    const auto *base = (const unsigned char *)MioPan_GetHostPointer(pk2);
    if (base == nullptr)
    {
        return nullptr;
    }
    /* A buffer the game has since filled with something else -- photo
     * decoding reuses the resident 2D area -- must not be walked: only
     * offsets that stay in the EE's memory, to pictures that say TIM2. */
    const int count = *(const int *)base;
    const int *offsets = (const int *)(base + 0x10);
    const uintptr_t room = 0x02000000u - (pk2 & 0x01ffffffu);
    for (int i = 0; i >= 0 && i < count && i < 256; i++)
    {
        if (only >= 0 && i != only)
        {
            continue;
        }
        if (offsets[i] <= 0 || (uintptr_t)offsets[i] + 0x100 >= room)
        {
            continue;
        }
        void *tim2 = (void *)(base + offsets[i]);
        if (std::memcmp(tim2, "TIM2", 4) != 0)
        {
            continue;
        }
        TIM2_PICTUREHEADER *picture = Tim2GetPictureHeader(tim2, 0);
        if (picture == nullptr || Tbp(picture->GsTex0) != Tbp(sprite.tex0))
        {
            continue;
        }
        if (picture->ClutColors != 0 && Cbp(picture->GsTex0) != Cbp(sprite.tex0))
        {
            continue;
        }
        if (picture->ImageWidth < sprite.u + sprite.w ||
            picture->ImageHeight < sprite.v + sprite.h)
        {
            continue;
        }
        return picture;
    }
    return nullptr;
}

/* FNV-1a over the pixels, for the image's name. */
unsigned long long Hash(const std::vector<Uint8> &rgba, int w, int h)
{
    unsigned long long hash = 1469598103934665603ull;
    auto mix = [&hash](unsigned value) {
        hash ^= value;
        hash *= 1099511628211ull;
    };
    mix((unsigned)w);
    mix((unsigned)h);
    for (const Uint8 byte : rgba)
    {
        mix(byte);
    }
    return hash;
}

/* What was asked for -- the pak, the sprite's picture and part, the tint --
 * and the image it came to. */
using Key = std::tuple<uintptr_t, u_long, int, int, int, int, int, int, int, int, int,
                       int, bool, int>;
std::map<Key, std::string> g_answers;

/* A lone TIM2's contents, and the image they came to. */
std::map<unsigned long long, std::string> g_pictures;

unsigned long long HashBytes(const unsigned char *bytes, size_t size)
{
    unsigned long long hash = 1469598103934665603ull;
    for (size_t i = 0; i < size; i++)
    {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
    return hash;
}

Uint8 Scale(u_int channel, int tint)
{
    const u_int value = channel * (u_int)tint / 0x80;
    return (Uint8)(value > 255 ? 255 : value);
}
}

namespace MioPanRmlSprite
{
std::string Image(uintptr_t pk2, const SPRT_DAT &sprite, int r, int g, int b)
{
    Look look;
    look.r = r;
    look.g = g;
    look.b = b;
    return Image(pk2, sprite, look);
}

std::string Image(uintptr_t pk2, const SPRT_DAT &sprite, const Look &look)
{
    const int r = look.r;
    const int g = look.g;
    const int b = look.b;
    const int turns = look.turns & 3;
    const Key key{pk2, sprite.tex0, sprite.u, sprite.v, sprite.w, sprite.h,
                  sprite.flip, sprite.alpha, r, g, b, turns, look.additive, look.picture};
    const auto known = g_answers.find(key);
    if (known != g_answers.end())
    {
        return known->second;
    }
    std::string &answer = g_answers[key];
    if (sprite.w == 0 || sprite.h == 0)
    {
        return answer;
    }
    TIM2_PICTUREHEADER *picture = FindPicture(pk2, sprite, look.picture);
    if (picture == nullptr)
    {
        return answer;
    }
    /* A four-bit picture's sprites pick their sixteen colours with CSA. */
    const int clut = Psm(sprite.tex0) == kPsmT4 ? (int)Csa(sprite.tex0) : 0;

    const int w = sprite.w;
    const int h = sprite.h;
    /* DispSprD()'s att bits: 2 mirrors the sprite across, 1 turns it over. */
    const bool mirror = (sprite.flip & 2) != 0;
    const bool turn = (sprite.flip & 1) != 0;
    /* A quarter turn clockwise: out (x, y) is the sprite's (y, h - 1 - x). */
    const bool quarter = (turns & 1) != 0;
    const int out_w = quarter ? h : w;
    const int out_h = quarter ? w : h;
    std::vector<Uint8> rgba((size_t)out_w * out_h * 4);
    for (int y = 0; y < out_h; y++)
    {
        for (int x = 0; x < out_w; x++)
        {
            int sx = quarter ? y : x;
            int sy = quarter ? h - 1 - x : y;
            if (turns == 2)
            {
                sx = w - 1 - x;
                sy = h - 1 - y;
            }
            else if (turns == 3)
            {
                sx = w - 1 - y;
                sy = x;
            }
            const u_int c = Tim2GetTextureColor(picture, 0, clut,
                                                sprite.u + (mirror ? w - 1 - sx : sx),
                                                sprite.v + (turn ? h - 1 - sy : sy));
            Uint8 *out = &rgba[((size_t)y * out_w + x) * 4];
            out[0] = Scale(c & 0xff, r);
            out[1] = Scale(c >> 8 & 0xff, g);
            out[2] = Scale(c >> 16 & 0xff, b);
            /* The game's alpha runs 0..0x80, and the sprite's own scales it. */
            u_int alpha = (c >> 24 & 0xff) * 2 * sprite.alpha / 0x80;
            alpha = alpha > 255 ? 255 : alpha;
            if (look.additive)
            {
                /* Premultiplied colour and no alpha: blended ONE,
                 * ONE_MINUS_SRC_ALPHA it simply adds. */
                for (int i = 0; i < 3; i++)
                {
                    out[i] = (Uint8)(out[i] * alpha / 255);
                }
                alpha = 0;
            }
            out[3] = (Uint8)alpha;
        }
    }

    char name[64];
    std::snprintf(name, sizeof(name), "%ssprite/%016llx%s", MioPanRml::kGeneratedPrefix,
                  Hash(rgba, out_w, out_h), look.additive ? "a" : "");
    if (!MioPanRml::HasGeneratedImage(name))
    {
        MioPanRml::SetGeneratedImage(name, rgba.data(), out_w, out_h, look.additive);
    }
    answer = name;
    return answer;
}

std::string Picture(const void *tim2)
{
    return Picture(tim2, 0, 0, -1, -1);
}

std::string Picture(const void *tim2, int u, int v, int w, int h)
{
    const auto *base = (const unsigned char *)MioPan_GetHostPointer((uintptr_t)tim2);
    if (base == nullptr)
    {
        return std::string();
    }
    TIM2_PICTUREHEADER *picture = Tim2GetPictureHeader((void *)base, 0);
    if (picture == nullptr || picture->ImageWidth == 0 || picture->ImageHeight == 0)
    {
        return std::string();
    }
    /* The whole picture, or the part asked for, kept inside it. */
    if (w < 0 || h < 0)
    {
        u = 0;
        v = 0;
        w = picture->ImageWidth;
        h = picture->ImageHeight;
    }
    if (u < 0 || v < 0 || w <= 0 || h <= 0 || u + w > picture->ImageWidth ||
        v + h > picture->ImageHeight)
    {
        return std::string();
    }
    /* Its header, CLUT and pixels -- all of what it looks like -- and the
     * part. */
    unsigned long long key = HashBytes((const unsigned char *)picture, picture->TotalSize);
    const int part[4] = {u, v, w, h};
    key ^= HashBytes((const unsigned char *)part, sizeof(part));
    const auto known = g_pictures.find(key);
    if (known != g_pictures.end())
    {
        return known->second;
    }
    std::vector<Uint8> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const u_int c = Tim2GetTextureColor(picture, 0, 0, u + x, v + y);
            Uint8 *out = &rgba[((size_t)y * w + x) * 4];
            out[0] = (Uint8)(c & 0xff);
            out[1] = (Uint8)(c >> 8 & 0xff);
            out[2] = (Uint8)(c >> 16 & 0xff);
            const u_int alpha = (c >> 24 & 0xff) * 2;
            out[3] = (Uint8)(alpha > 255 ? 255 : alpha);
        }
    }
    char name[64];
    std::snprintf(name, sizeof(name), "%spicture/%016llx", MioPanRml::kGeneratedPrefix,
                  key);
    if (!MioPanRml::HasGeneratedImage(name))
    {
        MioPanRml::SetGeneratedImage(name, rgba.data(), w, h);
    }
    return g_pictures[key] = name;
}

std::string Texels(uintptr_t addr, int w, int h, int stride)
{
    const auto *base = (const unsigned char *)MioPan_GetHostPointer(addr);
    if (base == nullptr || w <= 0 || h <= 0 || stride < w)
    {
        return std::string();
    }
    unsigned long long key = 1469598103934665603ull;
    for (int y = 0; y < h; y++)
    {
        key ^= HashBytes(base + (size_t)y * stride * 4, (size_t)w * 4);
        key *= 1099511628211ull;
    }
    const int size[3] = {w, h, stride};
    key ^= HashBytes((const unsigned char *)size, sizeof(size));
    const auto known = g_pictures.find(key);
    if (known != g_pictures.end())
    {
        return known->second;
    }
    std::vector<Uint8> rgba((size_t)w * h * 4);
    for (int y = 0; y < h; y++)
    {
        for (int x = 0; x < w; x++)
        {
            const unsigned char *in = base + ((size_t)y * stride + x) * 4;
            Uint8 *out = &rgba[((size_t)y * w + x) * 4];
            out[0] = in[0];
            out[1] = in[1];
            out[2] = in[2];
            /* Drawn opaque, whatever the capture left in its alpha. */
            out[3] = 255;
        }
    }
    char name[64];
    std::snprintf(name, sizeof(name), "%stexels/%016llx", MioPanRml::kGeneratedPrefix, key);
    if (!MioPanRml::HasGeneratedImage(name))
    {
        MioPanRml::SetGeneratedImage(name, rgba.data(), w, h);
    }
    return g_pictures[key] = name;
}

void Forget()
{
    g_answers.clear();
}
}
