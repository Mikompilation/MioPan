#include "miopan_rml_menu_art.h"
#include "miopan_rml_common.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_cmn_dat.h"

#include <RmlUi/Core.h>

#include <cstdio>

namespace
{
constexpr float kGameWidth = 640.0f;
constexpr float kGameHeight = 448.0f;

/* menu_cmn_dat[]: the rails' caps and runs, down and across. */
constexpr int kDownTop = 40;
constexpr int kDownRun = 41;
constexpr int kDownBottom = 42;
constexpr int kAcrossLeft = 43;
constexpr int kAcrossRun = 44;
constexpr int kAcrossRight = 45;

/* The picture, in context pixels: the 4:3 the game draws its 2D into,
 * centred, as tall as the context allows. */
struct Picture
{
    float x, y, w, h, dp;
};

Picture GamePicture()
{
    Picture picture{0.0f, 0.0f, kGameWidth, kGameHeight, 1.0f};
    Rml::Context *context = MioPanRml::GetContext();
    if (context == nullptr)
    {
        return picture;
    }
    const Rml::Vector2i size = context->GetDimensions();
    picture.dp = context->GetDensityIndependentPixelRatio();
    picture.h = (float)size.y;
    picture.w = picture.h * 4.0f / 3.0f;
    if (picture.w > (float)size.x)
    {
        picture.w = (float)size.x;
        picture.h = picture.w * 3.0f / 4.0f;
    }
    picture.x = ((float)size.x - picture.w) * 0.5f;
    picture.y = ((float)size.y - picture.h) * 0.5f;
    if (picture.dp <= 0.0f)
    {
        picture.dp = 1.0f;
    }
    return picture;
}

std::string Img(const std::string &src, float x, float y, float w, float h,
                float opacity = 1.0f)
{
    if (src.empty() || w <= 0.0f || h <= 0.0f || opacity <= 0.0f)
    {
        return std::string();
    }
    char style[160];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;%s", x, y,
                  w, h, "");
    std::string css = style;
    if (opacity < 1.0f)
    {
        char fade[32];
        std::snprintf(fade, sizeof(fade), " opacity: %.3f;", opacity);
        css += fade;
    }
    return "<img class=\"art\" src=\"" + src + "\" style=\"" + css + "\"/>";
}

std::string Box(const MioPanRmlMenuArt::Scale &scale, const std::string &text,
                float x, float y, float width, const char *cls, const char *align)
{
    char style[128];
    std::snprintf(style, sizeof(style), "left: %.2fdp; top: %.2fdp; width: %.2fdp;%s",
                  x * scale.x, y * scale.y, width * scale.x, align);
    return "<span class=\"" + std::string(cls) + "\" style=\"" + style + "\">" +
           MioPanRmlCommon::Escape(text) + "</span>";
}
}

namespace MioPanRmlMenuArt
{
Scale StageScale()
{
    const Picture picture = GamePicture();
    return Scale{picture.w / kGameWidth / picture.dp,
                 picture.h / kGameHeight / picture.dp};
}

std::string StageStyle()
{
    const Picture picture = GamePicture();
    char style[160];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp; "
                  "font-size: %.3fdp;",
                  picture.x / picture.dp, picture.y / picture.dp,
                  picture.w / picture.dp, picture.h / picture.dp,
                  10.0f * picture.h / kGameHeight / picture.dp);
    return style;
}

std::string Art(const Scale &scale, uintptr_t pk2, const SPRT_DAT &sprite, int dx,
                int dy, int r, int g, int b)
{
    return Img(MioPanRmlSprite::Image(pk2, sprite, r, g, b),
               (float)(sprite.x + dx) * scale.x, (float)(sprite.y + dy) * scale.y,
               (float)sprite.w * scale.x, (float)sprite.h * scale.y);
}

std::string ArtIn(const Scale &scale, uintptr_t pk2, const SPRT_DAT &sprite, float x,
                  float y, float w, float h)
{
    return Img(MioPanRmlSprite::Image(pk2, sprite), x * scale.x, y * scale.y,
               w * scale.x, h * scale.y);
}

std::string ArtIn(const Scale &scale, uintptr_t pk2, const SPRT_DAT &sprite, float x,
                  float y, float w, float h, const MioPanRmlSprite::Look &look)
{
    return Img(MioPanRmlSprite::Image(pk2, sprite, look), x * scale.x, y * scale.y,
               w * scale.x, h * scale.y);
}

std::string ImageIn(const Scale &scale, const std::string &src, float x, float y,
                    float w, float h, float opacity)
{
    return Img(src, x * scale.x, y * scale.y, w * scale.x, h * scale.y, opacity);
}

std::string RailAcross(const Scale &scale, float x, float y, float w)
{
    const SPRT_DAT &left = menu_cmn_dat[kAcrossLeft];
    const SPRT_DAT &run = menu_cmn_dat[kAcrossRun];
    const SPRT_DAT &right = menu_cmn_dat[kAcrossRight];
    const float run_w = w - (float)(left.w + right.w);
    std::string rml = ArtIn(scale, MENU_BG_TEX_ADRS, left, x, y, left.w, left.h);
    rml += ArtIn(scale, MENU_BG_TEX_ADRS, right, x + left.w + run_w, y, right.w,
                 right.h);
    if (run_w > 0.0f)
    {
        rml += ArtIn(scale, MENU_BG_TEX_ADRS, run, x + left.w, y, run_w, run.h);
    }
    return rml;
}

std::string RailDown(const Scale &scale, float x, float y, float h)
{
    const SPRT_DAT &top = menu_cmn_dat[kDownTop];
    const SPRT_DAT &run = menu_cmn_dat[kDownRun];
    const SPRT_DAT &bottom = menu_cmn_dat[kDownBottom];
    const float run_h = h - (float)(top.h + bottom.h);
    std::string rml = ArtIn(scale, MENU_BG_TEX_ADRS, top, x, y, top.w, top.h);
    rml += ArtIn(scale, MENU_BG_TEX_ADRS, bottom, x, y + top.h + run_h, bottom.w,
                 bottom.h);
    if (run_h > 0.0f)
    {
        rml += ArtIn(scale, MENU_BG_TEX_ADRS, run, x, y + top.h, run.w, run_h);
    }
    return rml;
}

std::string Text(const Scale &scale, const std::string &text, float x, float y,
                 float width, const char *cls)
{
    return Box(scale, text, x, y, width, cls, "");
}

std::string Number(const Scale &scale, const std::string &text, float x, float y,
                   float width, const char *cls)
{
    return Box(scale, text, x, y, width, cls, " text-align: right;");
}
}
