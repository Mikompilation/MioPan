#include "miopan_rml_menu_radio.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_radio_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_radio.rml";

using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

constexpr int kRows = MIOPAN_RML_MENU_RADIO_ROWS;

/* A stone's name is message 0x49 and what is known of it 0x48, both by the
 * stone; its recording's lines are bank 9's; the empty page's line is 0x36
 * entry 4. */
constexpr int kMsgName = 0x49;
constexpr int kMsgExplain = 0x48;
constexpr int kMsgSubtitle = 9;
constexpr int kMsgEmpty = 0x36;
constexpr int kMsgEmptyRadio = 4;

/* menu_radio_tex[]: the bench and the radio, the column down the right and
 * its two pieces laid on their side, the title's plates, the scrollbar's
 * arrows, rail and thumb, a dim and a lit row's frame, the unheard bracket,
 * the stone and its halo, and the subtitle's plate. */
constexpr int kBench = 0;
constexpr int kColumn[3] = {1, 2, 3};
constexpr int kColumnSide[2] = {4, 5};
constexpr int kTitlePlates[2] = {8, 9};
constexpr int kArrows[2] = {10, 11};
constexpr int kRailTop = 12;
constexpr int kRailBottom = 13;
constexpr int kRailMiddle = 14;
constexpr int kThumbTop = 15;
constexpr int kThumbMiddle = 16;
constexpr int kThumbBottom = 17;
constexpr int kDim[3] = {18, 19, 20};
constexpr int kLit[3] = {21, 22, 23};
constexpr int kUnheard[2] = {24, 25};
constexpr int kStone = 26;
constexpr int kFlare[4] = {27, 28, 29, 30};
constexpr int kPlate[3] = {34, 35, 36};

/* menu_radio.c's layout: the rows 35 apart, the names in them, the
 * brackets 255 apart; the frames' stretched middles; the rail and the thumb's
 * run; the window under the list and its line; the subtitle's first line and
 * its pitch. */
constexpr int kRowPitch = 35;
constexpr int kNameX = 64;
constexpr int kNameY = 80;
constexpr int kUnheardX = 44;
constexpr int kUnheardY = 83;
constexpr float kUnheardSpan = 255.0f;
constexpr float kLitMiddle = 138.0f;
constexpr float kDimMiddle = 164.0f;
constexpr float kRailMiddleSize = 126.0f;
constexpr float kThumbRun = 185.0f;
constexpr float kThumbTopY = 94.0f;
constexpr int kWindowX = 24;
constexpr int kWindowY = 322;
constexpr int kWindowW = 592;
constexpr int kWindowH = 124;
constexpr int kExplainX = 48;
constexpr int kExplainY = 342;
constexpr int kSubtitleY = 291;
constexpr int kSubtitlePitch = 29;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuRadioState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *rows = nullptr;
    Rml::Element *scroll = nullptr;
    Rml::Element *stone = nullptr;
    Rml::Element *subtitle = nullptr;
    Rml::Element *explain = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_stage;
    std::string shown_rows;
    std::string shown_scroll;
    std::string shown_stone;
    std::string shown_subtitle;
    std::string shown_explain;
    std::string shown_prompts;

    Scale scale;
    MIOPAN_RML_MENU_RADIO page = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_RADIO_NONE;
    int command_arg = 0;
};

MenuRadioState g_radio;
Listener g_listener;

uintptr_t Pak()
{
    return (uintptr_t)g_radio.page.pak;
}

void SetRml(Rml::Element *element, std::string &shown, const std::string &rml)
{
    if (element != nullptr && rml != shown)
    {
        element->SetInnerRML(rml);
        shown = rml;
    }
}

std::string Place(float x, float y, float w, float h)
{
    char style[128];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  x * g_radio.scale.x, y * g_radio.scale.y, w * g_radio.scale.x,
                  h * g_radio.scale.y);
    return style;
}

std::string Game(int type, int id)
{
    return MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(type, id));
}

/* A message in the game's own lines, for a <p>. */
std::string Lines(int type, int id)
{
    const std::string text = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::MessagePage(type, id, 0, true));
    std::string lines;
    for (size_t start = 0; start <= text.size();)
    {
        const size_t end = text.find('\n', start);
        lines += MioPanRmlCommon::Escape(
            text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        lines += "<br/>";
        start = end + 1;
    }
    return lines;
}

std::string Added(const SPRT_DAT &sprite, float x, float y, float w, float h)
{
    MioPanRmlSprite::Look added;
    added.additive = true;
    return ArtIn(g_radio.scale, Pak(), sprite, x, y, w, h, added);
}

/* The bench and the radio, the title, and -- unless there is nothing to
 * list -- the scrollbar's rail, with the layers the page fills in, in
 * MenuRadioDisp()'s order. */
std::string Stage()
{
    const Scale &scale = g_radio.scale;
    const bool empty = g_radio.page.mode == MIOPAN_RML_MENU_RADIO_EMPTY;
    std::string rml = Art(scale, Pak(), menu_radio_tex[kBench]);
    for (const int tile : kColumn)
    {
        rml += Art(scale, Pak(), menu_radio_tex[tile]);
    }
    /* Each turned a quarter back about its own corner. */
    MioPanRmlSprite::Look back;
    back.turns = 3;
    for (const int piece : kColumnSide)
    {
        const SPRT_DAT &side = menu_radio_tex[piece];
        rml += ArtIn(scale, Pak(), side, side.x, side.y, side.h, side.w, back);
    }
    for (const int plate : kTitlePlates)
    {
        rml += Art(scale, MENU_BG_TEX_ADRS, menu_radio_tex[plate]);
    }
    rml += Text(scale, "Radio", 41, 20, 120, "menu-title");
    if (!empty)
    {
        rml += "<div id=\"rows\" class=\"layer\"></div>";
        const SPRT_DAT &top = menu_radio_tex[kRailTop];
        const SPRT_DAT &middle = menu_radio_tex[kRailMiddle];
        const SPRT_DAT &bottom = menu_radio_tex[kRailBottom];
        rml += Added(top, top.x, top.y, top.w, top.h);
        rml += Added(middle, middle.x, middle.y, middle.w, kRailMiddleSize);
        rml += Added(bottom, bottom.x, bottom.y, bottom.w, bottom.h);
        rml += "<div id=\"scroll\" class=\"layer\"></div>";
        rml += "<div id=\"stone\" class=\"layer\"></div>";
        rml += "<div id=\"subtitle\" class=\"layer\"></div>";
    }
    rml += "<div id=\"explain\" class=\"layer\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) +
           "\"></div>";
    return rml;
}

/* The rows shown: the lit frame under the cursor -- a cap, its middle
 * stretched and the cap mirrored -- a dim one on the others, its middle strip
 * turned a quarter and stretched across, each with its stone's name; then the
 * brackets on the unheard ones. */
std::string Rows()
{
    const Scale &scale = g_radio.scale;
    const MIOPAN_RML_MENU_RADIO &page = g_radio.page;
    const int shown = page.count < kRows ? page.count : kRows;
    std::string rml;
    for (int i = 0; i < shown; i++)
    {
        const bool lit = i == page.cursor;
        const float dy = (float)(i * kRowPitch);
        if (lit)
        {
            const SPRT_DAT &left = menu_radio_tex[kLit[0]];
            const SPRT_DAT &middle = menu_radio_tex[kLit[1]];
            const SPRT_DAT &right = menu_radio_tex[kLit[2]];
            const float x = left.x;
            rml += ArtIn(scale, Pak(), left, x, left.y + dy, left.w, left.h);
            rml += ArtIn(scale, Pak(), middle, x + left.w, left.y + dy, kLitMiddle, middle.h);
            rml += ArtIn(scale, Pak(), right, x + left.w + kLitMiddle, left.y + dy, right.w,
                         right.h);
        }
        else
        {
            const SPRT_DAT &left = menu_radio_tex[kDim[0]];
            const SPRT_DAT &middle = menu_radio_tex[kDim[1]];
            const SPRT_DAT &right = menu_radio_tex[kDim[2]];
            const float x = left.x;
            MioPanRmlSprite::Look turned;
            turned.turns = 1;
            rml += ArtIn(scale, Pak(), left, x, left.y + dy, left.w, left.h);
            rml += ArtIn(scale, Pak(), middle, x + left.w, left.y + dy, kDimMiddle, middle.w,
                         turned);
            rml += ArtIn(scale, Pak(), right, x + left.w + kDimMiddle, left.y + dy, right.w,
                         right.h);
        }
        rml += Text(scale, Game(kMsgName, page.crystal[i]), kNameX, kNameY + dy, 236,
                    lit ? "row-text lit" : "row-text");
    }
    for (int i = 0; i < shown; i++)
    {
        if (page.unheard[i] == 0)
        {
            continue;
        }
        const float y = (float)(kUnheardY + i * kRowPitch);
        const SPRT_DAT &left = menu_radio_tex[kUnheard[0]];
        const SPRT_DAT &right = menu_radio_tex[kUnheard[1]];
        rml += ArtIn(scale, Pak(), left, kUnheardX, y, left.w, left.h);
        rml += ArtIn(scale, Pak(), right, kUnheardX + kUnheardSpan, y, right.w, right.h);
    }
    for (int i = 0; i < shown; i++)
    {
        rml += "<div id=\"row-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(44, 80 + i * kRowPitch, 266, kRowPitch) + "\"></div>";
    }
    return rml;
}

/* The thumb, sized and placed from the list as MenuRadioScrollDisp() does,
 * and the two arrows, pulsing. */
std::string Scroll()
{
    const Scale &scale = g_radio.scale;
    const int count = g_radio.page.count;
    float size = kThumbRun;
    float y = kThumbTopY;
    if (count >= kRows)
    {
        size = kThumbRun - (kThumbRun / (float)count) * (float)(count - kRows);
        y = (kThumbRun / (float)count) * (float)g_radio.page.start + kThumbTopY;
    }
    const SPRT_DAT &top = menu_radio_tex[kThumbTop];
    const SPRT_DAT &middle = menu_radio_tex[kThumbMiddle];
    const SPRT_DAT &bottom = menu_radio_tex[kThumbBottom];
    const float centre = size - (float)(top.h + bottom.h);
    std::string rml;
    if (centre > 0.0f)
    {
        rml += ArtIn(scale, Pak(), top, top.x, y, top.w, top.h);
        rml += ArtIn(scale, Pak(), middle, middle.x, y + top.h, middle.w, centre);
        rml += ArtIn(scale, Pak(), bottom, bottom.x, y + top.h + centre, bottom.w, bottom.h);
    }
    else
    {
        rml += ArtIn(scale, Pak(), top, top.x, y, top.w, size * 0.5f);
        rml += ArtIn(scale, Pak(), bottom, bottom.x, y + size * 0.5f, bottom.w, size * 0.5f);
    }
    for (int i = 0; i < 2; i++)
    {
        const SPRT_DAT &arrow = menu_radio_tex[kArrows[i]];
        rml += "<div id=\"arrow-" + std::to_string(i) + "\" class=\"scroll-arrow\" style=\"" +
               Place(arrow.x, arrow.y, arrow.w, arrow.h) + "\">" +
               ArtIn(scale, Pak(), arrow, 0, 0, arrow.w, arrow.h) + "</div>";
    }
    return rml;
}

/* A piece out of a stone's own pak, at its place, faded to `alpha` (0..0x80)
 * by opacity so the fade makes no new image. */
std::string StonePiece(const void *pak, const SPRT_DAT &sprite, int alpha)
{
    return MioPanRmlMenuArt::ImageIn(
        g_radio.scale, MioPanRmlSprite::Image((uintptr_t)pak, sprite), sprite.x, sprite.y,
        sprite.w, sprite.h, (float)(alpha > 0x80 ? 0x80 : alpha) / 128.0f);
}

/* The stone in the radio's window at the alpha menu_radio.c worked out, and
 * the halo and plates of a playing one. */
std::string Stone()
{
    const MIOPAN_RML_MENU_RADIO &page = g_radio.page;
    std::string rml;
    for (int i = 0; i < 2; i++)
    {
        if (page.stone[i] != nullptr && page.stone_alpha[i] > 0)
        {
            rml += StonePiece(page.stone[i], menu_radio_tex[kStone], page.stone_alpha[i]);
        }
    }
    if (page.flare != nullptr && page.flare_alpha > 0)
    {
        for (const int piece : kFlare)
        {
            rml += StonePiece(page.flare, menu_radio_tex[piece], page.flare_alpha);
        }
    }
    return rml;
}

/* The recording's line now, centred on its plate as MovieTitleDispMain()
 * prints it, a line to a plate. */
std::string Subtitle()
{
    const int id = g_radio.page.subtitle;
    if (id < 0)
    {
        return std::string();
    }
    const std::string text = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::MessagePage(kMsgSubtitle, id, 0, true));
    std::string decorator = "tiled-horizontal(";
    for (int i = 0; i < 3; i++)
    {
        decorator += (i > 0 ? ", " : "") +
                     MioPanRmlSprite::Image(Pak(), menu_radio_tex[kPlate[i]]);
    }
    decorator += ")";
    std::string rml;
    int line = 0;
    for (size_t start = 0; start <= text.size(); line++)
    {
        const size_t end = text.find('\n', start);
        const std::string piece =
            text.substr(start, end == std::string::npos ? std::string::npos : end - start);
        rml += "<div class=\"radio-subtitle\" style=\"" +
               Place(0, (float)(kSubtitleY - 2 + line * kSubtitlePitch), 640,
                     kSubtitlePitch) +
               "\"><span style=\"decorator: " + decorator + ";\">" +
               MioPanRmlCommon::Escape(piece) + "</span></div>";
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    return rml;
}

/* The window under the list with what is known of the stone, or the empty
 * page's line in its own. */
std::string Explain()
{
    const Scale &scale = g_radio.scale;
    const MIOPAN_RML_MENU_RADIO &page = g_radio.page;
    if (page.mode == MIOPAN_RML_MENU_RADIO_EMPTY)
    {
        using namespace MioPanRmlMenuArt;
        return Window(scale, kNoticeX, kNoticeY, kNoticeW, kNoticeH) +
               Text(scale, Game(kMsgEmpty, kMsgEmptyRadio), kNoticeTextX, kNoticeTextY,
                    kNoticeW - (kNoticeTextX - kNoticeX), "window-text");
    }
    std::string rml = MioPanRmlMenuArt::Window(scale, kWindowX, kWindowY, kWindowW, kWindowH);
    if (page.count > 0)
    {
        rml += "<p class=\"caption-text\" style=\"" +
               Place(kExplainX, kExplainY, 560, 90) + "\">" +
               Lines(kMsgExplain, page.selected) + "</p>";
    }
    return rml;
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kList[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Play", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        {"PgUp / PgDn", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Change Page", "prompt-page",
         SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    };
    static const MioPanRmlCommon::Prompt kPlaying[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Stop", "prompt-stop"},
    };
    static const MioPanRmlCommon::Prompt kEmpty[] = {
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts;
    if (!g_radio.page.closing)
    {
        switch (g_radio.page.mode)
        {
        case MIOPAN_RML_MENU_RADIO_LIST:
            prompts = MioPanRmlCommon::PromptsRml(kList, 3);
            break;
        case MIOPAN_RML_MENU_RADIO_PLAY:
            prompts = MioPanRmlCommon::PromptsRml(kPlaying, 1);
            break;
        default:
            prompts = MioPanRmlCommon::PromptsRml(kEmpty, 1);
            break;
        }
    }
    SetRml(g_radio.prompts, g_radio.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_radio.shown_stage)
    {
        return;
    }
    g_radio.stage->SetInnerRML(rml);
    g_radio.shown_stage = rml;
    Rml::ElementDocument *document = g_radio.document;
    g_radio.rows = document->GetElementById("rows");
    g_radio.scroll = document->GetElementById("scroll");
    g_radio.stone = document->GetElementById("stone");
    g_radio.subtitle = document->GetElementById("subtitle");
    g_radio.explain = document->GetElementById("explain");
    g_radio.prompts = document->GetElementById("prompts");
    g_radio.shown_rows.clear();
    g_radio.shown_scroll.clear();
    g_radio.shown_stone.clear();
    g_radio.shown_subtitle.clear();
    g_radio.shown_explain.clear();
    g_radio.shown_prompts.clear();
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_radio.scale.x || scale.y != g_radio.scale.y)
    {
        g_radio.scale = scale;
        g_radio.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_radio.shown_stage.clear();
    }
    BuildStage();
    if (g_radio.page.mode != MIOPAN_RML_MENU_RADIO_EMPTY)
    {
        SetRml(g_radio.rows, g_radio.shown_rows, Rows());
        SetRml(g_radio.scroll, g_radio.shown_scroll, Scroll());
        SetRml(g_radio.stone, g_radio.shown_stone, Stone());
        SetRml(g_radio.subtitle, g_radio.shown_subtitle, Subtitle());
    }
    SetRml(g_radio.explain, g_radio.shown_explain, Explain());
    RefreshPrompts();
}

void Queue(int command, int arg = 0)
{
    g_radio.command = command;
    g_radio.command_arg = arg;
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool enter = key == Rml::Input::KI_RETURN ||
                       key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE;
    const bool back = key == Rml::Input::KI_ESCAPE;
    switch (g_radio.page.mode)
    {
    case MIOPAN_RML_MENU_RADIO_LIST:
        switch (key)
        {
        case Rml::Input::KI_UP: Queue(MIOPAN_RML_MENU_RADIO_UP); break;
        case Rml::Input::KI_DOWN: Queue(MIOPAN_RML_MENU_RADIO_DOWN); break;
        case Rml::Input::KI_PRIOR: Queue(MIOPAN_RML_MENU_RADIO_PAGE_UP); break;
        case Rml::Input::KI_NEXT: Queue(MIOPAN_RML_MENU_RADIO_PAGE_DOWN); break;
        default:
            if (enter)
            {
                Queue(MIOPAN_RML_MENU_RADIO_CHOOSE);
            }
            else if (back)
            {
                Queue(MIOPAN_RML_MENU_RADIO_BACK);
            }
            else
            {
                return;
            }
            break;
        }
        break;
    /* Either button stops it. */
    case MIOPAN_RML_MENU_RADIO_PLAY:
        if (!enter && !back)
        {
            return;
        }
        Queue(MIOPAN_RML_MENU_RADIO_STOP);
        break;
    default:
        if (!back)
        {
            return;
        }
        Queue(MIOPAN_RML_MENU_RADIO_BACK);
        break;
    }
    event.StopPropagation();
}

/* The id of `element` or the nearest ancestor that has one starting with
 * `prefix`. */
std::string Owner(Rml::Element *element, const char *prefix)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        const std::string &id = element->GetId();
        if (id.rfind(prefix, 0) == 0)
        {
            return id;
        }
    }
    return std::string();
}

void HandlePointer(Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    const bool click = event == Rml::EventId::Click;
    switch (g_radio.page.mode)
    {
    case MIOPAN_RML_MENU_RADIO_LIST:
    {
        if (click && !Owner(target, "prompt-back").empty())
        {
            Queue(MIOPAN_RML_MENU_RADIO_BACK);
            return;
        }
        if (event == Rml::EventId::Mousescroll)
        {
            const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
            if (delta != 0.0f)
            {
                Queue(delta < 0.0f ? MIOPAN_RML_MENU_RADIO_UP : MIOPAN_RML_MENU_RADIO_DOWN);
            }
            return;
        }
        const std::string row = Owner(target, "row-");
        const std::string arrow = Owner(target, "arrow-");
        if (click && !Owner(target, "prompt-page").empty())
        {
            Queue(MIOPAN_RML_MENU_RADIO_PAGE_DOWN);
        }
        else if (click && !arrow.empty())
        {
            Queue(arrow == "arrow-0" ? MIOPAN_RML_MENU_RADIO_UP : MIOPAN_RML_MENU_RADIO_DOWN);
        }
        else if (!row.empty())
        {
            const int index = std::atoi(row.c_str() + 4);
            if (click)
            {
                Queue(index == g_radio.page.cursor ? MIOPAN_RML_MENU_RADIO_CHOOSE
                                                   : MIOPAN_RML_MENU_RADIO_ROW,
                      index);
            }
            else if (MioPanRml::PointerActive() && index != g_radio.page.cursor)
            {
                Queue(MIOPAN_RML_MENU_RADIO_ROW, index);
            }
        }
        break;
    }
    /* A click anywhere stops it, as the game's mouse does. */
    case MIOPAN_RML_MENU_RADIO_PLAY:
        if (click)
        {
            Queue(MIOPAN_RML_MENU_RADIO_STOP);
        }
        break;
    default:
        if (click && !Owner(target, "prompt-back").empty())
        {
            Queue(MIOPAN_RML_MENU_RADIO_BACK);
        }
        break;
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, and nothing
     * while the page fades out. */
    if (!g_radio.visible || g_radio.page.closing ||
        g_radio.command != MIOPAN_RML_MENU_RADIO_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    HandlePointer(event);
}
}

namespace MioPanRmlMenuRadio
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_radio.document = context->LoadDocument(kDocumentPath);
    if (g_radio.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_radio.stage = g_radio.document->GetElementById("stage");
    if (g_radio.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_radio.document->Close();
        g_radio = MenuRadioState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_radio.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_radio = MenuRadioState();
}

void Frame()
{
    if (g_radio.visible && !g_radio.requested)
    {
        g_radio.document->Hide();
        g_radio.visible = false;
        g_radio.command = MIOPAN_RML_MENU_RADIO_NONE;
    }
    g_radio.shown_last_frame = g_radio.visible;
    g_radio.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuRadioReady(void)
{
    return g_radio.document != nullptr ? 1 : 0;
}

int MioPan_RmlMenuRadioShow(const MIOPAN_RML_MENU_RADIO *page)
{
    if (g_radio.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_radio.requested = true;
    const bool new_pak = page->pak != g_radio.page.pak;
    g_radio.page = *page;
    if (!g_radio.visible || new_pak)
    {
        /* A new visit: the page's pak may sit where another's did. */
        MioPanRmlSprite::Forget();
        g_radio.scale = MioPanRmlMenuArt::StageScale();
        g_radio.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_radio.shown_stage.clear();
    }
    if (!g_radio.visible)
    {
        g_radio.command = MIOPAN_RML_MENU_RADIO_NONE;
        g_radio.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_radio.visible = true;
    }
    g_radio.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuRadioIsShown(void)
{
    return g_radio.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuRadioConsumeCommand(int *arg)
{
    const int command = g_radio.command;
    if (arg != nullptr)
    {
        *arg = g_radio.command_arg;
    }
    g_radio.command = MIOPAN_RML_MENU_RADIO_NONE;
    g_radio.command_arg = 0;
    return command;
}
}
