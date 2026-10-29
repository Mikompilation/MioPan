#include "miopan_rml_menu_soul.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_glist_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_soul.rml";

using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Number;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

constexpr int kRows = MIOPAN_RML_MENU_SOUL_ROWS;

/* A spirit owns three messages in bank 0x3b: its name, where it was met
 * and what is known of it.  Bank 8 entry 2 is the score's "pts.", bank 0x28
 * the congratulation's lines. */
constexpr int kMsgSoul = 0x3b;
constexpr int kMsgPerSoul = 3;
constexpr int kMsgName = 0;
constexpr int kMsgPlace = 1;
constexpr int kMsgPoints = 2;
constexpr int kMsgCommon = 8;
constexpr int kMsgCongrats = 0x28;
constexpr int kCongratsLines[3] = {5, 0, 1};
constexpr int kCongratsLineY[3] = {195, 249, 273};

/* menu_glist_tex[]: the frame's four tiles and the fifth laid on its side,
 * the title's plates, the rate's digits and the stamp, the cursor's frame and
 * triangles, the photograph's place, the scrollbar's arrows and thumb, the
 * unread mark, the badges and the congratulation's plates. */
constexpr int kFrame[4] = {0, 1, 2, 3};
constexpr int kFrameSide = 4;
constexpr int kTitlePlates[2] = {5, 6};
constexpr int kDigitZero = 8;
constexpr int kCursor[3] = {18, 19, 20};
constexpr int kTriangleUp = 21;
constexpr int kTriangleDown = 22;
constexpr int kPhoto = 23;
constexpr int kArrows[2] = {24, 25};
constexpr int kThumbTop = 26;
constexpr int kThumbMiddle = 27;
constexpr int kThumbBottom = 28;
constexpr int kUnread = 29;
constexpr int kComplete = 30;
constexpr int kBadgeFirst = 31;
constexpr int kCongrats[2] = {34, 35};

/* menu_soul.c's layout: the rows 34 apart, the cursor's bed, frame and
 * triangles, the unread mark and badge each from their own top, the names;
 * the rate's digits, the score and its unit, the place, the description;
 * the scrollbar's rail; the congratulation's window. */
constexpr int kRowPitch = 34;
constexpr int kBedX = 40;
constexpr int kBedY = 116;
constexpr int kBedW = 272;
constexpr int kBedH = 30;
constexpr int kCursorY = 112;
constexpr float kCursorBodyW = 253.0f;
constexpr int kTriangleUpY = 103;
constexpr int kTriangleDownY = 148;
constexpr int kUnreadY = 117;
constexpr int kBadgeY = 108;
constexpr int kNameX = 76;
constexpr int kNameY = 114;
constexpr int kRateX = 119;
constexpr int kRateY = 72;
constexpr int kScoreRight = 555;
constexpr int kPointsX = 559;
constexpr int kInfoY = 69;
constexpr int kPlaceX = 363;
constexpr int kPlaceY = 93;
constexpr int kExplainX = 334;
constexpr int kExplainY = 274;
constexpr float kRailSize = 251.0f;
constexpr float kRailTop = 142.0f;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuSoulState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *scroll = nullptr;
    Rml::Element *rows = nullptr;
    Rml::Element *info = nullptr;
    Rml::Element *photos = nullptr;
    Rml::Element *window = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_stage;
    std::string shown_scroll;
    std::string shown_rows;
    std::string shown_info;
    std::string shown_photos;
    std::string shown_window;
    std::string shown_prompts;

    Scale scale;
    MIOPAN_RML_MENU_SOUL page = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_SOUL_NONE;
    int command_arg = 0;
};

MenuSoulState g_soul;
Listener g_listener;

uintptr_t Pak()
{
    return (uintptr_t)g_soul.page.pak;
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
                  x * g_soul.scale.x, y * g_soul.scale.y, w * g_soul.scale.x,
                  h * g_soul.scale.y);
    return style;
}

std::string Game(int type, int id)
{
    return MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(type, id));
}

/* A sprite added to what is under it, as alphar 0x48 draws it. */
std::string Added(const SPRT_DAT &sprite, float x, float y, float w, float h)
{
    MioPanRmlSprite::Look added;
    added.additive = true;
    return ArtIn(g_soul.scale, Pak(), sprite, x, y, w, h, added);
}

int Shown()
{
    return g_soul.page.count < kRows ? g_soul.page.count : kRows;
}

/* The title, and over an empty list nothing else: MenuSoulNotHaveListDisp()
 * draws only the hints.  Otherwise the scrollbar, the frame laid over it,
 * and the layers the list fills in, in MenuSoulListSelDisp()'s order. */
std::string Stage()
{
    const Scale &scale = g_soul.scale;
    std::string rml;
    for (const int plate : kTitlePlates)
    {
        rml += Art(scale, MENU_BG_TEX_ADRS, menu_glist_tex[plate]);
    }
    rml += Text(scale, "Spirit List", 41, 20, 160, "menu-title");
    if (g_soul.page.mode != MIOPAN_RML_MENU_SOUL_EMPTY)
    {
        rml += "<div id=\"scroll\" class=\"layer\"></div>";
        for (const int tile : kFrame)
        {
            rml += Art(scale, Pak(), menu_glist_tex[tile]);
        }
        /* The fifth turned a quarter back about its own corner. */
        MioPanRmlSprite::Look back;
        back.turns = 3;
        const SPRT_DAT &side = menu_glist_tex[kFrameSide];
        rml += ArtIn(scale, Pak(), side, side.x, side.y, side.h, side.w, back);
        rml += "<div id=\"rows\" class=\"layer\"></div>";
        rml += "<div id=\"info\" class=\"layer\"></div>";
        rml += "<div id=\"photos\" class=\"layer\"></div>";
    }
    rml += "<div id=\"window\" class=\"layer\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) +
           "\"></div>";
    return rml;
}

/* The arrows, pulsing, and the thumb, sized and placed from the list as
 * MenuSoulScrollDisp() does: the whole rail for a list that fits, a row's
 * share less per row past the ninth; two caps and a stretched middle, or two
 * squashed caps when it is shorter than they are. */
std::string Scroll()
{
    const Scale &scale = g_soul.scale;
    const int count = g_soul.page.count;
    std::string rml;
    for (int i = 0; i < 2; i++)
    {
        const SPRT_DAT &arrow = menu_glist_tex[kArrows[i]];
        rml += "<div id=\"arrow-" + std::to_string(i) + "\" class=\"scroll-arrow\" style=\"" +
               Place(arrow.x, arrow.y, arrow.w, arrow.h) + "\">" +
               ArtIn(scale, Pak(), arrow, 0, 0, arrow.w, arrow.h) + "</div>";
    }
    float size = kRailSize;
    float y = kRailTop;
    if (count >= kRows)
    {
        size = kRailSize - (kRailSize / (float)count) * (float)(count - kRows);
        y = (kRailSize / (float)count) * (float)g_soul.page.start + kRailTop;
    }
    const SPRT_DAT &top = menu_glist_tex[kThumbTop];
    const SPRT_DAT &middle = menu_glist_tex[kThumbMiddle];
    const SPRT_DAT &bottom = menu_glist_tex[kThumbBottom];
    const float centre = size - (float)(top.h + bottom.h);
    if (centre > 0.0f)
    {
        rml += Added(top, top.x, y, top.w, top.h);
        rml += Added(middle, middle.x, y + top.h, middle.w, centre);
        rml += Added(bottom, bottom.x, y + top.h + centre, bottom.w, bottom.h);
    }
    else
    {
        rml += Added(top, top.x, y, top.w, size * 0.5f);
        rml += Added(bottom, bottom.x, y + size * 0.5f, bottom.w, size * 0.5f);
    }
    return rml;
}

/* The rate in the frame's own digits, blank before the first that is not
 * zero; the stamp over it once the list is complete. */
std::string Rate()
{
    const Scale &scale = g_soul.scale;
    const int rate = g_soul.page.rate;
    std::string rml;
    bool started = false;
    int place = 100;
    for (int i = 0; i < 3; i++, place /= 10)
    {
        const int digit = rate / place % 10;
        started = started || digit != 0 || place == 1;
        if (started)
        {
            const SPRT_DAT &sprite = menu_glist_tex[kDigitZero + digit];
            rml += ArtIn(scale, Pak(), sprite, (float)(kRateX + i * sprite.w), kRateY,
                         sprite.w, sprite.h);
        }
    }
    if (rate >= 100)
    {
        rml += Art(scale, Pak(), menu_glist_tex[kComplete]);
    }
    return rml;
}

/* The rows: the cursor's amber bed, the unread marks, the names on their
 * shadows, the cursor's frame and triangles over them, and the badges. */
std::string Rows()
{
    const Scale &scale = g_soul.scale;
    const MIOPAN_RML_MENU_SOUL &page = g_soul.page;
    const int shown = Shown();
    std::string rml = Rate();
    if (shown <= 0)
    {
        return rml;
    }
    const float cursor_y = (float)(page.cursor * kRowPitch);
    rml += "<div class=\"soul-bed\" style=\"" +
           Place(kBedX, kBedY + cursor_y, kBedW, kBedH) + "\"></div>";
    for (int i = 0; i < shown; i++)
    {
        if (page.unread[i] != 0)
        {
            const SPRT_DAT &mark = menu_glist_tex[kUnread];
            rml += ArtIn(scale, Pak(), mark, mark.x, (float)(kUnreadY + i * kRowPitch),
                         mark.w, mark.h);
        }
    }
    for (int i = 0; i < shown; i++)
    {
        const bool lit = i == page.cursor;
        std::string cls = "soul-name";
        cls += page.enhancing[i] != 0 ? " enhancing" : "";
        cls += lit ? " lit" : "";
        rml += Text(scale, Game(kMsgSoul, page.label[i] * kMsgPerSoul + kMsgName), kNameX,
                    (float)(kNameY + i * kRowPitch), 232, cls.c_str());
    }
    const SPRT_DAT &left = menu_glist_tex[kCursor[0]];
    const SPRT_DAT &body = menu_glist_tex[kCursor[1]];
    const SPRT_DAT &right = menu_glist_tex[kCursor[2]];
    rml += Added(left, left.x, kCursorY + cursor_y, left.w, left.h);
    rml += Added(body, body.x, kCursorY + cursor_y, kCursorBodyW, body.h);
    rml += Added(right, right.x, kCursorY + cursor_y, right.w, right.h);
    rml += "<div class=\"layer pulse\">";
    for (const int piece : {kTriangleUp, kTriangleDown})
    {
        const SPRT_DAT &triangle = menu_glist_tex[piece];
        const int y = piece == kTriangleUp ? kTriangleUpY : kTriangleDownY;
        rml += Added(triangle, triangle.x, (float)y + cursor_y, triangle.w, triangle.h);
    }
    rml += "</div>";
    for (int i = 0; i < shown; i++)
    {
        if (page.order[i] >= 0 && page.order[i] < 3)
        {
            const SPRT_DAT &badge = menu_glist_tex[kBadgeFirst + page.order[i]];
            rml += Added(badge, badge.x, (float)(kBadgeY + i * kRowPitch), badge.w, badge.h);
        }
        rml += "<div id=\"row-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(kBedX, kBedY - 2 + i * kRowPitch, kBedW, kRowPitch) + "\"></div>";
    }
    return rml;
}

/* The spirit under the cursor: its best score and where it was met. */
std::string Info()
{
    const Scale &scale = g_soul.scale;
    const MIOPAN_RML_MENU_SOUL &page = g_soul.page;
    if (page.count <= 0)
    {
        return std::string();
    }
    std::string rml = Number(scale, std::to_string(page.score), kScoreRight - 100, kInfoY,
                             100, "soul-info");
    rml += Text(scale, Game(kMsgCommon, kMsgPoints), kPointsX, kInfoY, 60, "soul-info");
    rml += Text(scale, Game(kMsgSoul, page.selected * kMsgPerSoul + kMsgPlace), kPlaceX,
                kPlaceY, 250, "soul-info");
    return rml;
}

/* The cross-fade's two photographs, the outgoing one under the incoming,
 * each with what is known of its spirit fading with it. */
std::string Photos()
{
    const SPRT_DAT &place = menu_glist_tex[kPhoto];
    std::string rml;
    for (int i = 0; i < 2; i++)
    {
        const MIOPAN_RML_MENU_SOUL &page = g_soul.page;
        if (page.picture[i] == nullptr || page.picture_alpha[i] <= 0)
        {
            continue;
        }
        const float opacity =
            (float)(page.picture_alpha[i] > 0x80 ? 0x80 : page.picture_alpha[i]) / 128.0f;
        rml += MioPanRmlMenuArt::ImageIn(
            g_soul.scale,
            MioPanRmlSprite::Picture(page.picture[i], place.u, place.v, place.w, place.h),
            place.x, place.y, place.w, place.h, opacity);
        if (page.picture_msg[i] < 0)
        {
            continue;
        }
        const std::string text = MioPanRmlCommon::NameButtonIcons(
            MioPanGameText::MessagePage(kMsgSoul, page.picture_msg[i], 0, true));
        std::string lines;
        for (size_t start = 0; start <= text.size();)
        {
            const size_t end = text.find('\n', start);
            lines += MioPanRmlCommon::Escape(text.substr(
                         start, end == std::string::npos ? std::string::npos : end - start)) +
                     "<br/>";
            if (end == std::string::npos)
            {
                break;
            }
            start = end + 1;
        }
        char style[48];
        std::snprintf(style, sizeof(style), " opacity: %.3f;", opacity);
        rml += "<p class=\"soul-explain\" style=\"" + Place(kExplainX, kExplainY, 280, 130) +
               style + "\">" + lines + "</p>";
    }
    return rml;
}

/* The congratulation over the dimmed list: the window, its two plates and
 * its three lines, as MenuSoulCompMsgDisp() draws them. */
std::string Window()
{
    if (g_soul.page.mode != MIOPAN_RML_MENU_SOUL_CONGRATS)
    {
        return std::string();
    }
    const Scale &scale = g_soul.scale;
    std::string rml = "<div id=\"congrats\" class=\"soul-wash\" style=\"" +
                      Place(0, 0, 640, 448) + "\"></div>";
    rml += MioPanRmlMenuArt::TwoLineWindow(scale, 45, 104, 550, 232);
    for (const int plate : kCongrats)
    {
        rml += Art(scale, Pak(), menu_glist_tex[plate]);
    }
    for (int i = 0; i < 3; i++)
    {
        rml += Text(scale, Game(kMsgCongrats, kCongratsLines[i]), 45,
                    (float)(kCongratsLineY[i] - 3), 550, "window-text centred");
    }
    return rml;
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kList[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        {"PgUp / PgDn", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Change Page", "prompt-page",
         SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    };
    static const MioPanRmlCommon::Prompt kNotice[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "OK", nullptr},
    };
    static const MioPanRmlCommon::Prompt kEmpty[] = {
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts;
    if (!g_soul.page.closing)
    {
        switch (g_soul.page.mode)
        {
        case MIOPAN_RML_MENU_SOUL_LIST:
            prompts = MioPanRmlCommon::PromptsRml(kList, 3);
            break;
        case MIOPAN_RML_MENU_SOUL_CONGRATS:
            prompts = MioPanRmlCommon::PromptsRml(kNotice, 1);
            break;
        default:
            prompts = MioPanRmlCommon::PromptsRml(kEmpty, 1);
            break;
        }
    }
    SetRml(g_soul.prompts, g_soul.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_soul.shown_stage)
    {
        return;
    }
    g_soul.stage->SetInnerRML(rml);
    g_soul.shown_stage = rml;
    Rml::ElementDocument *document = g_soul.document;
    g_soul.scroll = document->GetElementById("scroll");
    g_soul.rows = document->GetElementById("rows");
    g_soul.info = document->GetElementById("info");
    g_soul.photos = document->GetElementById("photos");
    g_soul.window = document->GetElementById("window");
    g_soul.prompts = document->GetElementById("prompts");
    g_soul.shown_scroll.clear();
    g_soul.shown_rows.clear();
    g_soul.shown_info.clear();
    g_soul.shown_photos.clear();
    g_soul.shown_window.clear();
    g_soul.shown_prompts.clear();
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_soul.scale.x || scale.y != g_soul.scale.y)
    {
        g_soul.scale = scale;
        g_soul.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_soul.shown_stage.clear();
    }
    BuildStage();
    if (g_soul.page.mode != MIOPAN_RML_MENU_SOUL_EMPTY)
    {
        SetRml(g_soul.scroll, g_soul.shown_scroll, Scroll());
        SetRml(g_soul.rows, g_soul.shown_rows, Rows());
        SetRml(g_soul.info, g_soul.shown_info, Info());
        SetRml(g_soul.photos, g_soul.shown_photos, Photos());
    }
    SetRml(g_soul.window, g_soul.shown_window, Window());
    RefreshPrompts();
}

void Queue(int command, int arg = 0)
{
    g_soul.command = command;
    g_soul.command_arg = arg;
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool enter = key == Rml::Input::KI_RETURN ||
                       key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE;
    const bool back = key == Rml::Input::KI_ESCAPE;
    switch (g_soul.page.mode)
    {
    case MIOPAN_RML_MENU_SOUL_LIST:
        switch (key)
        {
        case Rml::Input::KI_UP: Queue(MIOPAN_RML_MENU_SOUL_UP); break;
        case Rml::Input::KI_DOWN: Queue(MIOPAN_RML_MENU_SOUL_DOWN); break;
        case Rml::Input::KI_PRIOR: Queue(MIOPAN_RML_MENU_SOUL_PAGE_UP); break;
        case Rml::Input::KI_NEXT: Queue(MIOPAN_RML_MENU_SOUL_PAGE_DOWN); break;
        default:
            if (enter)
            {
                Queue(MIOPAN_RML_MENU_SOUL_CHOOSE);
            }
            else if (back)
            {
                Queue(MIOPAN_RML_MENU_SOUL_BACK);
            }
            else
            {
                return;
            }
            break;
        }
        break;
    /* Either button dismisses it, each with its own cue. */
    case MIOPAN_RML_MENU_SOUL_CONGRATS:
        if (enter)
        {
            Queue(MIOPAN_RML_MENU_SOUL_ACK);
        }
        else if (back)
        {
            Queue(MIOPAN_RML_MENU_SOUL_BACK);
        }
        else
        {
            return;
        }
        break;
    default:
        if (!back)
        {
            return;
        }
        Queue(MIOPAN_RML_MENU_SOUL_BACK);
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
    if (click && !Owner(target, "prompt-back").empty())
    {
        Queue(MIOPAN_RML_MENU_SOUL_BACK);
        return;
    }
    switch (g_soul.page.mode)
    {
    case MIOPAN_RML_MENU_SOUL_LIST:
    {
        if (event == Rml::EventId::Mousescroll)
        {
            const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
            if (delta != 0.0f)
            {
                Queue(delta < 0.0f ? MIOPAN_RML_MENU_SOUL_UP : MIOPAN_RML_MENU_SOUL_DOWN);
            }
            return;
        }
        const std::string row = Owner(target, "row-");
        const std::string arrow = Owner(target, "arrow-");
        if (click && !Owner(target, "prompt-page").empty())
        {
            Queue(MIOPAN_RML_MENU_SOUL_PAGE_DOWN);
        }
        else if (click && !arrow.empty())
        {
            Queue(arrow == "arrow-0" ? MIOPAN_RML_MENU_SOUL_UP : MIOPAN_RML_MENU_SOUL_DOWN);
        }
        else if (!row.empty())
        {
            const int index = std::atoi(row.c_str() + 4);
            if (click)
            {
                Queue(index == g_soul.page.cursor ? MIOPAN_RML_MENU_SOUL_CHOOSE
                                                  : MIOPAN_RML_MENU_SOUL_ROW,
                      index);
            }
            else if (MioPanRml::PointerActive() && index != g_soul.page.cursor)
            {
                Queue(MIOPAN_RML_MENU_SOUL_ROW, index);
            }
        }
        break;
    }
    case MIOPAN_RML_MENU_SOUL_CONGRATS:
        if (click)
        {
            Queue(MIOPAN_RML_MENU_SOUL_ACK);
        }
        break;
    default:
        break;
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, and nothing
     * while the page fades out. */
    if (!g_soul.visible || g_soul.page.closing ||
        g_soul.command != MIOPAN_RML_MENU_SOUL_NONE)
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

namespace MioPanRmlMenuSoul
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_soul.document = context->LoadDocument(kDocumentPath);
    if (g_soul.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_soul.stage = g_soul.document->GetElementById("stage");
    if (g_soul.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_soul.document->Close();
        g_soul = MenuSoulState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_soul.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_soul = MenuSoulState();
}

void Frame()
{
    if (g_soul.visible && !g_soul.requested)
    {
        g_soul.document->Hide();
        g_soul.visible = false;
        g_soul.command = MIOPAN_RML_MENU_SOUL_NONE;
    }
    g_soul.shown_last_frame = g_soul.visible;
    g_soul.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuSoulShow(const MIOPAN_RML_MENU_SOUL *page)
{
    if (g_soul.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_soul.requested = true;
    const bool new_pak = page->pak != g_soul.page.pak;
    g_soul.page = *page;
    if (!g_soul.visible || new_pak)
    {
        /* A new visit: the page's pak may sit where another's did. */
        MioPanRmlSprite::Forget();
        g_soul.scale = MioPanRmlMenuArt::StageScale();
        g_soul.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_soul.shown_stage.clear();
    }
    if (!g_soul.visible)
    {
        g_soul.command = MIOPAN_RML_MENU_SOUL_NONE;
        g_soul.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_soul.visible = true;
    }
    g_soul.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuSoulIsShown(void)
{
    return g_soul.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuSoulConsumeCommand(int *arg)
{
    const int command = g_soul.command;
    if (arg != nullptr)
    {
        *arg = g_soul.command_arg;
    }
    g_soul.command = MIOPAN_RML_MENU_SOUL_NONE;
    g_soul.command_arg = 0;
    return command;
}
}
