#include "miopan_rml_menu_art.h"
#include "miopan_rml_common.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_cmn_dat.h"
#include "../rendering/miopan_renderer.h"
#include "../../graphics/graph2d/tim_dat/msg_box.h"
#include "../../system/os/system.h"

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

/* msg_box_tex[]: the window lines' caps and middles, down and across, and the
 * two-line window's rounded ends; the frame's and the ends' alphas. */
constexpr int kBoxDownTop = 0;
constexpr int kBoxDownBottom = 2;
constexpr int kBoxAcrossLeft = 4;
constexpr int kBoxAcrossRight = 5;
constexpr int kBoxAcrossRun = 8;
constexpr int kBoxDownRun = 9;
constexpr int kBoxEndLeft = 10;
constexpr int kBoxEndRight = 11;
constexpr float kFrameAlpha = 76.0f / 128.0f;
constexpr float kGlassAlpha = 102.0f / 128.0f;

/* The picture, in context pixels: where the renderer puts the game's 640 x
 * 448 2D frame.  The context is the presented rect, and the rect spans
 * MioPan_RendererGetViewBounds() of the frame -- past 0 and 640 when the
 * screen is widened -- so the frame is centred in it at whatever shape the
 * chosen aspect gives it, as the menu mouse reads it back. */
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
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = kGameWidth;
    float y1 = kGameHeight;
    MioPan_RendererGetViewBounds(&x0, &y0, &x1, &y1);
    if (!(x1 > x0) || !(y1 > y0))
    {
        x0 = 0.0f;
        y0 = 0.0f;
        x1 = kGameWidth;
        y1 = kGameHeight;
    }
    picture.w = kGameWidth / (x1 - x0) * (float)size.x;
    picture.h = kGameHeight / (y1 - y0) * (float)size.y;
    picture.x = -x0 / (x1 - x0) * (float)size.x;
    picture.y = -y0 / (y1 - y0) * (float)size.y;
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

std::string Place(const MioPanRmlMenuArt::Scale &scale, float x, float y, float w,
                  float h)
{
    char style[128];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  x * scale.x, y * scale.y, w * scale.x, h * scale.y);
    return style;
}

/* The message-box art, decoded while the boot's resident 2D pak still holds
 * it: the game uploads it to VRAM once and later reuses that memory -- a
 * photograph is expanded there -- so it is never read again after. */
std::string g_box[12];

const std::string &BoxArt(int piece)
{
    return g_box[piece];
}

/* DrawCmnLine()'s lines out of that art: a cap, a stretched middle when
 * there is room for one, and the other cap -- 46 long across, 45 down -- at
 * the frame's 76/128. */
std::string LineAcross(const MioPanRmlMenuArt::Scale &scale, float x, float y, float w)
{
    const SPRT_DAT &left = msg_box_tex[kBoxAcrossLeft];
    const SPRT_DAT &right = msg_box_tex[kBoxAcrossRight];
    const SPRT_DAT &run = msg_box_tex[kBoxAcrossRun];
    const float run_w = w - 92.0f > 0.0f ? w - 92.0f : 0.0f;
    std::string rml = Img(BoxArt(kBoxAcrossLeft), x * scale.x,
                          y * scale.y, left.w * scale.x, left.h * scale.y, kFrameAlpha);
    rml += Img(BoxArt(kBoxAcrossRight), (x + run_w + 46.0f) * scale.x,
               y * scale.y, right.w * scale.x, right.h * scale.y, kFrameAlpha);
    if (run_w > 0.0f)
    {
        rml += Img(BoxArt(kBoxAcrossRun), (x + 46.0f) * scale.x,
                   y * scale.y, run_w * scale.x, run.h * scale.y, kFrameAlpha);
    }
    return rml;
}

std::string LineDown(const MioPanRmlMenuArt::Scale &scale, float x, float y, float h)
{
    const SPRT_DAT &top = msg_box_tex[kBoxDownTop];
    const SPRT_DAT &bottom = msg_box_tex[kBoxDownBottom];
    const SPRT_DAT &run = msg_box_tex[kBoxDownRun];
    const float run_h = h - 90.0f > 0.0f ? h - 90.0f : 0.0f;
    std::string rml = Img(BoxArt(kBoxDownTop), x * scale.x,
                          y * scale.y, top.w * scale.x, top.h * scale.y, kFrameAlpha);
    rml += Img(BoxArt(kBoxDownBottom), x * scale.x,
               (y + run_h + 45.0f) * scale.y, bottom.w * scale.x, bottom.h * scale.y,
               kFrameAlpha);
    if (run_h > 0.0f)
    {
        rml += Img(BoxArt(kBoxDownRun), x * scale.x,
                   (y + 45.0f) * scale.y, run.w * scale.x, run_h * scale.y, kFrameAlpha);
    }
    return rml;
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

std::string RowFrame(const Scale &scale, float x, float y, float w, bool lit)
{
    /* menu_cmn_dat[]: the lit half and its mirror out of the background pak,
     * the dim pair out of the play-data one. */
    const int first = lit ? 36 : 38;
    const uintptr_t pak = lit ? MENU_BG_TEX_ADRS : MENU_PLAYDATA_TEX_ADRS;
    const SPRT_DAT &left = menu_cmn_dat[first];
    const SPRT_DAT &right = menu_cmn_dat[first + 1];
    return ArtIn(scale, pak, left, x, y, w * 0.5f, left.h) +
           ArtIn(scale, pak, right, x + w * 0.5f, y, w * 0.5f, right.h);
}

std::string Window(const Scale &scale, float x, float y, float w, float h,
                   const char *id, const char *cls)
{
    /* The glass, inset 16 across and 13 down, and the frame over its edges
     * running on into the corners: across 10 in from the top and 17 up from
     * the bottom, down 13 in from the left and 17 in from the right. */
    std::string rml = "<div";
    if (id != nullptr)
    {
        rml += " id=\"" + std::string(id) + "\"";
    }
    rml += " class=\"menu-window";
    if (cls != nullptr)
    {
        rml += " " + std::string(cls);
    }
    rml += "\" style=\"" + Place(scale, x + 16.0f, y + 13.0f, w - 32.0f, h - 26.0f) +
           "\"></div>";
    rml += LineAcross(scale, x, y + 10.0f, w);
    rml += LineAcross(scale, x, y + h - (float)(msg_box_tex[kBoxAcrossLeft].h + 13), w);
    rml += LineDown(scale, x + 13.0f, y, h);
    rml += LineDown(scale, x + w - (float)(msg_box_tex[kBoxDownTop].w + 13), y, h);
    return rml;
}

void KeepResidentArt()
{
    for (int i = 0; i < 12; i++)
    {
        const std::string src = MioPanRmlSprite::Image(PACKET2D_ADDR, msg_box_tex[i]);
        if (!src.empty())
        {
            g_box[i] = src;
        }
    }
}

std::string TwoLineWindow(const Scale &scale, float x, float y, float w, float h)
{
    /* The two rounded ends stretched down the band, the glass between them,
     * and a line across above and below. */
    const SPRT_DAT &left = msg_box_tex[kBoxEndLeft];
    const SPRT_DAT &right = msg_box_tex[kBoxEndRight];
    const float band = h - 26.0f;
    std::string rml = Img(BoxArt(kBoxEndLeft), (x + 16.0f) * scale.x,
                          (y + 13.0f) * scale.y, left.w * scale.x, band * scale.y, kGlassAlpha);
    rml += Img(BoxArt(kBoxEndRight),
               (x + w - (float)(right.w + 16)) * scale.x, (y + 13.0f) * scale.y,
               right.w * scale.x, band * scale.y, kGlassAlpha);
    rml += "<div class=\"menu-glass\" style=\"" +
           Place(scale, x + 46.0f, y + 13.0f, w - 92.0f, band) + "\"></div>";
    rml += LineAcross(scale, x, y + 10.0f, w);
    rml += LineAcross(scale, x, y + h - (float)(msg_box_tex[kBoxAcrossLeft].h + 13), w);
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

extern "C" void MioPan_RmlMenuArtKeepResident(void)
{
    MioPanRmlMenuArt::KeepResidentArt();
}
