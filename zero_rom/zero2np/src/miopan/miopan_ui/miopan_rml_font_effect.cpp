#include "miopan_rml_font_effect.h"

#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/FontEffect.h>
#include <RmlUi/Core/FontEffectInstancer.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{

class BevelEffect final : public Rml::FontEffect
{
public:
    explicit BevelEffect(float amount) : strength(std::clamp(amount, 0.0f, 1.0f))
    {
        SetLayer(Layer::Front);
    }

    bool HasUniqueTexture() const override { return true; }

    bool GetGlyphMetrics(Rml::Vector2i &, Rml::Vector2i &, const Rml::FontGlyph &glyph) const override
    {
        return glyph.bitmap_data && glyph.color_format == Rml::ColorFormat::A8 &&
               glyph.bitmap_dimensions.x > 0 && glyph.bitmap_dimensions.y > 0;
    }

    void GenerateGlyphTexture(Rml::byte *dst, Rml::Vector2i dimensions, int stride,
                              const Rml::FontGlyph &glyph) const override
    {
        if (!glyph.bitmap_data || glyph.color_format != Rml::ColorFormat::A8) return;
        const int w = glyph.bitmap_dimensions.x, h = glyph.bitmap_dimensions.y;
        if (w <= 0 || h <= 0 || dimensions.x != w || dimensions.y != h) return;
        const float radius = std::clamp(h / 28.0f, 1.0f, 4.0f);
        const int reach = (int)std::ceil(radius + 0.5f);
        struct Offset { int x, y; float distance; };
        std::vector<Offset> offsets;
        for (int y = -reach; y <= reach; ++y)
            for (int x = -reach; x <= reach; ++x) {
                float d = std::max(0.0f, std::hypot((float)x, (float)y) - 0.5f);
                if (d < radius) offsets.push_back({x, y, d});
            }
        std::vector<float> height(w * h), smooth(w * h);
        auto alpha = [&](int x, int y) {
            return x >= 0 && y >= 0 && x < w && y < h ? glyph.bitmap_data[y * w + x] : 0;
        };
        // Same distance-based rounded profile as the offline HD-font generator.
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                if (alpha(x, y) < 128) continue;
                float distance = radius;
                for (const auto &offset : offsets)
                    if (alpha(x + offset.x, y + offset.y) < 128)
                        distance = std::min(distance, offset.distance);
                height[y * w + x] = std::sin(distance / radius * 1.57079633f);
            }
        auto sample = [&](const std::vector<float> &field, int x, int y) {
            return x >= 0 && y >= 0 && x < w && y < h ? field[y * w + x] : 0.0f;
        };
        const float weights[3] = {0.1664f, 0.6672f, 0.1664f};
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx)
                        smooth[y * w + x] += sample(height, x + dx, y + dy) * weights[dx + 1] * weights[dy + 1];
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float nx = -(sample(smooth, x + 1, y) - sample(smooth, x - 1, y)) * radius * 0.5f;
                float ny = -(sample(smooth, x, y + 1) - sample(smooth, x, y - 1)) * radius * 0.5f;
                float light = (-0.5f * nx - 0.75f * ny + 1.0f) / std::sqrt(nx * nx + ny * ny + 1.0f);
                float shade = strength * (light - 1.0f);
                unsigned a = (unsigned)std::lround(std::min(1.0f, std::abs(shade)) * alpha(x, y));
                auto *pixel = dst + y * stride + x * 4;
                // A cached premultiplied overlay preserves the base text's CSS colour.
                const unsigned colour[3] = {shade >= 0 ? 255u : 16u, shade >= 0 ? 247u : 10u, shade >= 0 ? 224u : 6u};
                for (int c = 0; c < 3; ++c) pixel[c] = (Rml::byte)((colour[c] * a + 127) / 255);
                pixel[3] = (Rml::byte)a;
            }
    }
private:
    float strength;
};

class BevelInstancer final : public Rml::FontEffectInstancer
{
public:
    BevelInstancer()
    {
        strength = RegisterProperty("strength", "0.55", true).AddParser("number").GetId();
        RegisterShorthand("font-effect", "strength", Rml::ShorthandType::FallThrough);
    }
    Rml::SharedPtr<Rml::FontEffect> InstanceFontEffect(const Rml::String &, const Rml::PropertyDictionary &properties) override
    {
        return Rml::MakeShared<BevelEffect>(properties.GetProperty(strength)->Get<float>());
    }
private:
    Rml::PropertyId strength;
};
}

namespace MioPanRmlFontEffect
{
void Register()
{
    static BevelInstancer instancer;
    Rml::Factory::RegisterFontEffectInstancer("pan-bevel", &instancer);
}
}
