#include "miopan_rml_menu_memo.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_cmn_dat.h"
#include "../../ingame/menu/tim_dat/menu_memo_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_memo.rml";

using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

constexpr int kRows = MIOPAN_RML_MENU_MEMO_ROWS;

/* A memo owns six messages in bank 0x30, three per revision: its title, its
 * body and the line under the list.  Bank 8 entry 3 is the dash either side
 * of the reader's title; 0x36 entry 3 the empty page's line. */
constexpr int kMsgMemo = 0x30;
constexpr int kMsgPerMemo = 6;
constexpr int kMsgPerStep = 3;
constexpr int kMsgTitle = 0;
constexpr int kMsgBody = 1;
constexpr int kMsgLine = 2;
constexpr int kMsgCommon = 8;
constexpr int kMsgDash = 3;
constexpr int kMsgEmpty = 0x36;
constexpr int kMsgEmptyMemo = 3;

/* menu_memo_tex[]: the notebook's four tiles and its two rails, drawn a
 * quarter turn back; the title's two plates; the unread bracket pair. */
constexpr int kPaper[4] = {0, 1, 2, 3};
constexpr int kRails[2] = {4, 5};
constexpr int kTitlePlates[2] = {6, 7};
constexpr int kUnread[2] = {62, 63};

/* menu_memo.c's layout: the rows on their frames, 35 apart, the title
 * centred at x 146; the caption window and its line; the reader's window,
 * its title, body and page count. */
constexpr int kRowX = 29;
constexpr int kRowY = 58;
constexpr int kRowPitch = 35;
constexpr int kRowW = 246;
constexpr int kRowH = 33;
constexpr int kRowTextCentre = 146;
constexpr int kUnreadY = 60;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuMemoState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *inks = nullptr;
    Rml::Element *rule = nullptr;
    Rml::Element *mode = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_stage;
    std::string shown_inks;
    std::string shown_rule;
    std::string shown_mode;
    std::string shown_prompts;
    float shown_mode_alpha = -1.0f;

    Scale scale;
    MIOPAN_RML_MENU_MEMO page = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_MEMO_NONE;
    int command_arg = 0;
};

MenuMemoState g_memo;
Listener g_listener;

uintptr_t Pak()
{
    return (uintptr_t)g_memo.page.pak;
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
                  x * g_memo.scale.x, y * g_memo.scale.y, w * g_memo.scale.x,
                  h * g_memo.scale.y);
    return style;
}

int MsgId(int index, int which)
{
    return g_memo.page.label[index] * kMsgPerMemo + g_memo.page.step[index] * kMsgPerStep +
           which;
}

int Selected()
{
    const int index = g_memo.page.start + g_memo.page.cursor;
    return index < g_memo.page.count ? index : 0;
}

std::string Game(int type, int id)
{
    return MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(type, id));
}

/* The notebook, the title, and the layers the page fills in over them, in
 * MenuMemoDisp()'s order. */
std::string Stage()
{
    const Scale &scale = g_memo.scale;
    std::string rml;
    for (const int tile : kPaper)
    {
        rml += Art(scale, Pak(), menu_memo_tex[tile]);
    }
    /* Each rail turned a quarter back about its own corner: it stands as
     * tall as it was long. */
    MioPanRmlSprite::Look back;
    back.turns = 3;
    for (const int piece : kRails)
    {
        const SPRT_DAT &rail = menu_memo_tex[piece];
        rml += ArtIn(scale, Pak(), rail, rail.x, rail.y, rail.h, rail.w, back);
    }
    for (const int plate : kTitlePlates)
    {
        rml += Art(scale, MENU_BG_TEX_ADRS, menu_memo_tex[plate]);
    }
    rml += Text(scale, "Memo", 41, 20, 120, "menu-title");
    rml += "<div id=\"inks\" class=\"layer\"></div>";
    rml += "<div id=\"rule\" class=\"layer\"></div>";
    rml += "<div id=\"mode\" class=\"memo-mode\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) +
           "\"></div>";
    return rml;
}

/* Every memo held, its title brushed on the notebook where it belongs, in
 * the ink menu_memo.c picks for it. */
std::string Inks()
{
    const Scale &scale = g_memo.scale;
    std::string rml;
    for (int i = 0; i < g_memo.page.count && i < MIOPAN_RML_MENU_MEMO_MAX; i++)
    {
        const int rgb = g_memo.page.ink_rgb[i];
        for (const int tile : g_memo.page.ink[i])
        {
            if (tile < 0)
            {
                break;
            }
            rml += Art(scale, Pak(), menu_memo_tex[tile], 0, 0, rgb >> 16 & 0xff,
                       rgb >> 8 & 0xff, rgb & 0xff);
        }
    }
    return rml;
}

/* The rail down the left of the list, 30 longer per row shown, as
 * MenuMemoLineDisp() sizes it. */
std::string Rule()
{
    const int count = g_memo.page.count;
    const int shown = count < kRows ? count : kRows;
    const float length = (float)(menu_cmn_dat[40].h + menu_cmn_dat[42].h + 30 * shown);
    return MioPanRmlMenuArt::RailDown(g_memo.scale, 22, 0, length);
}

/* The list: each row's frame, its memo's title centred in it, the unread
 * bracket pair, and the caption window with the line for the memo under the
 * cursor. */
std::string List()
{
    const Scale &scale = g_memo.scale;
    const MIOPAN_RML_MENU_MEMO &page = g_memo.page;
    const int shown = page.count < kRows ? page.count : kRows;
    std::string rml;
    for (int i = 0; i < shown; i++)
    {
        const int index = page.start + i;
        const bool lit = i == page.cursor;
        const float y = (float)(kRowY + i * kRowPitch);
        rml += MioPanRmlMenuArt::RowFrame(scale, kRowX, y, kRowW, lit);
        rml += Text(scale, Game(kMsgMemo, MsgId(index, kMsgTitle)), kRowTextCentre - 120,
                    y, 240, lit ? "row-text centred lit" : "row-text centred");
        if (page.unread[index] != 0)
        {
            for (const int piece : kUnread)
            {
                const SPRT_DAT &bracket = menu_memo_tex[piece];
                rml += ArtIn(scale, Pak(), bracket, bracket.x,
                             (float)(kUnreadY + i * kRowPitch), bracket.w, bracket.h);
            }
        }
        rml += "<div id=\"row-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(kRowX, y, kRowW, kRowH) + "\"></div>";
    }
    rml += MioPanRmlMenuArt::Window(scale, 24, 346, 592, 100);
    if (page.count > 0)
    {
        rml += Text(scale, Game(kMsgMemo, MsgId(Selected(), kMsgLine)), 48, 366, 544,
                    "caption-text");
    }
    return rml;
}

/* The reader: its window, the memo's title between two dashes, this page of
 * it in the game's own lines, and "- n / total -" under it. */
std::string Read()
{
    const Scale &scale = g_memo.scale;
    const MIOPAN_RML_MENU_MEMO &page = g_memo.page;
    const int index = Selected();
    const std::string dash = MioPanGameText::Message(kMsgCommon, kMsgDash);
    /* Over the notebook, which shows through it as the game's 0x66 glass
     * lets it. */
    std::string rml =
        MioPanRmlMenuArt::Window(scale, 36, 57, 572, 373, nullptr, "reader-window");
    rml += Text(scale, dash + "  " + Game(kMsgMemo, MsgId(index, kMsgTitle)) + "  " + dash,
                36, 78, 572, "reader-title");
    const std::string body = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::MessagePage(kMsgMemo, MsgId(index, kMsgBody), page.page, true));
    std::string lines;
    for (size_t start = 0; start <= body.size();)
    {
        const size_t end = body.find('\n', start);
        const std::string line =
            body.substr(start, end == std::string::npos ? std::string::npos : end - start);
        lines += MioPanRmlCommon::Escape(line) + "<br/>";
        if (end == std::string::npos)
        {
            break;
        }
        start = end + 1;
    }
    rml += "<p class=\"reader-body\" style=\"" + Place(80, 120, 480, 240) + "\">" + lines +
           "</p>";
    char count[64];
    std::snprintf(count, sizeof(count), "%s  %d / %d  %s", dash.c_str(), page.page + 1,
                  page.pages > 0 ? page.pages : 1, dash.c_str());
    rml += Text(scale, count, 36, 368, 572, "reader-count");
    rml += "<div id=\"reader-prev\" class=\"row-hit\" style=\"" + Place(36, 57, 286, 373) +
           "\"></div>";
    rml += "<div id=\"reader-next\" class=\"row-hit\" style=\"" + Place(322, 57, 286, 373) +
           "\"></div>";
    return rml;
}

std::string Empty()
{
    using namespace MioPanRmlMenuArt;
    return Window(g_memo.scale, kNoticeX, kNoticeY, kNoticeW, kNoticeH) +
           Text(g_memo.scale, Game(kMsgEmpty, kMsgEmptyMemo), kNoticeTextX, kNoticeTextY,
                kNoticeW - (kNoticeTextX - kNoticeX), "window-text");
}

std::string Mode()
{
    switch (g_memo.page.mode)
    {
    case MIOPAN_RML_MENU_MEMO_LIST: return List();
    case MIOPAN_RML_MENU_MEMO_READ: return Read();
    default: return Empty();
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kList[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        {"PgUp / PgDn", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Change Page", "prompt-page",
         SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    };
    static const MioPanRmlCommon::Prompt kReadPages[] = {
        {"Left / Right", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Turn Page", "prompt-page",
         SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kBack[] = {
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts;
    if (!g_memo.page.closing)
    {
        if (g_memo.page.mode == MIOPAN_RML_MENU_MEMO_LIST)
        {
            prompts = MioPanRmlCommon::PromptsRml(kList, 3);
        }
        else if (g_memo.page.mode == MIOPAN_RML_MENU_MEMO_READ && g_memo.page.pages > 1)
        {
            prompts = MioPanRmlCommon::PromptsRml(kReadPages, 2);
        }
        else
        {
            prompts = MioPanRmlCommon::PromptsRml(kBack, 1);
        }
    }
    SetRml(g_memo.prompts, g_memo.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_memo.shown_stage)
    {
        return;
    }
    g_memo.stage->SetInnerRML(rml);
    g_memo.shown_stage = rml;
    g_memo.inks = g_memo.document->GetElementById("inks");
    g_memo.rule = g_memo.document->GetElementById("rule");
    g_memo.mode = g_memo.document->GetElementById("mode");
    g_memo.prompts = g_memo.document->GetElementById("prompts");
    g_memo.shown_inks.clear();
    g_memo.shown_rule.clear();
    g_memo.shown_mode.clear();
    g_memo.shown_prompts.clear();
    g_memo.shown_mode_alpha = -1.0f;
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_memo.scale.x || scale.y != g_memo.scale.y)
    {
        g_memo.scale = scale;
        g_memo.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_memo.shown_stage.clear();
    }
    BuildStage();
    SetRml(g_memo.inks, g_memo.shown_inks, Inks());
    SetRml(g_memo.rule, g_memo.shown_rule,
           g_memo.page.mode == MIOPAN_RML_MENU_MEMO_EMPTY ? std::string() : Rule());
    SetRml(g_memo.mode, g_memo.shown_mode, Mode());
    /* The list and the reader fade into each other on the mode's own
     * fade. */
    const int alpha = g_memo.page.mode_alpha > 0x80 ? 0x80 : g_memo.page.mode_alpha;
    const float opacity = (float)(alpha < 0 ? 0 : alpha) / 128.0f;
    if (opacity != g_memo.shown_mode_alpha && g_memo.mode != nullptr)
    {
        char style[32];
        std::snprintf(style, sizeof(style), "opacity: %.3f;", opacity);
        g_memo.mode->SetAttribute("style", style);
        g_memo.shown_mode_alpha = opacity;
    }
    RefreshPrompts();
}

void Queue(int command, int arg = 0)
{
    g_memo.command = command;
    g_memo.command_arg = arg;
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool enter = key == Rml::Input::KI_RETURN ||
                       key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE;
    if (key == Rml::Input::KI_ESCAPE)
    {
        Queue(MIOPAN_RML_MENU_MEMO_BACK);
    }
    else if (g_memo.page.mode == MIOPAN_RML_MENU_MEMO_LIST)
    {
        switch (key)
        {
        case Rml::Input::KI_UP: Queue(MIOPAN_RML_MENU_MEMO_UP); break;
        case Rml::Input::KI_DOWN: Queue(MIOPAN_RML_MENU_MEMO_DOWN); break;
        case Rml::Input::KI_PRIOR: Queue(MIOPAN_RML_MENU_MEMO_PAGE_UP); break;
        case Rml::Input::KI_NEXT: Queue(MIOPAN_RML_MENU_MEMO_PAGE_DOWN); break;
        default:
            if (!enter)
            {
                return;
            }
            Queue(MIOPAN_RML_MENU_MEMO_CHOOSE);
            break;
        }
    }
    else if (g_memo.page.mode == MIOPAN_RML_MENU_MEMO_READ)
    {
        /* As the reader's pad: right, R1 and CROSS forward, left and L1
         * back. */
        if (enter || key == Rml::Input::KI_RIGHT || key == Rml::Input::KI_NEXT)
        {
            Queue(MIOPAN_RML_MENU_MEMO_NEXT);
        }
        else if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_PRIOR)
        {
            Queue(MIOPAN_RML_MENU_MEMO_PREV);
        }
        else
        {
            return;
        }
    }
    else
    {
        return;
    }
    event.StopPropagation();
}

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
        Queue(MIOPAN_RML_MENU_MEMO_BACK);
        return;
    }
    const float wheel = event == Rml::EventId::Mousescroll
                            ? event.GetParameter<float>("wheel_delta_y", 0.0f)
                            : 0.0f;
    if (g_memo.page.mode == MIOPAN_RML_MENU_MEMO_LIST)
    {
        if (wheel != 0.0f)
        {
            Queue(wheel < 0.0f ? MIOPAN_RML_MENU_MEMO_UP : MIOPAN_RML_MENU_MEMO_DOWN);
            return;
        }
        if (click && !Owner(target, "prompt-page").empty())
        {
            Queue(MIOPAN_RML_MENU_MEMO_PAGE_DOWN);
            return;
        }
        const std::string row = Owner(target, "row-");
        if (row.empty())
        {
            return;
        }
        const int index = std::atoi(row.c_str() + 4);
        if (click)
        {
            Queue(index == g_memo.page.cursor ? MIOPAN_RML_MENU_MEMO_CHOOSE
                                              : MIOPAN_RML_MENU_MEMO_ROW,
                  index);
        }
        else if (MioPanRml::PointerActive() && index != g_memo.page.cursor)
        {
            Queue(MIOPAN_RML_MENU_MEMO_ROW, index);
        }
    }
    else if (g_memo.page.mode == MIOPAN_RML_MENU_MEMO_READ)
    {
        /* The left half turns back, the right half on, as the game's own
         * mouse does. */
        if (wheel != 0.0f)
        {
            Queue(wheel < 0.0f ? MIOPAN_RML_MENU_MEMO_PREV : MIOPAN_RML_MENU_MEMO_NEXT);
        }
        else if (click && !Owner(target, "prompt-page").empty())
        {
            Queue(MIOPAN_RML_MENU_MEMO_NEXT);
        }
        else if (click && !Owner(target, "reader-prev").empty())
        {
            Queue(MIOPAN_RML_MENU_MEMO_PREV);
        }
        else if (click && !Owner(target, "reader-next").empty())
        {
            Queue(MIOPAN_RML_MENU_MEMO_NEXT);
        }
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, nor while the
     * page or its mode fades. */
    if (!g_memo.visible || g_memo.page.closing || g_memo.page.busy ||
        g_memo.command != MIOPAN_RML_MENU_MEMO_NONE)
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

namespace MioPanRmlMenuMemo
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_memo.document = context->LoadDocument(kDocumentPath);
    if (g_memo.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_memo.stage = g_memo.document->GetElementById("stage");
    if (g_memo.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_memo.document->Close();
        g_memo = MenuMemoState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_memo.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_memo = MenuMemoState();
}

void Frame()
{
    if (g_memo.visible && !g_memo.requested)
    {
        g_memo.document->Hide();
        g_memo.visible = false;
        g_memo.command = MIOPAN_RML_MENU_MEMO_NONE;
    }
    g_memo.shown_last_frame = g_memo.visible;
    g_memo.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuMemoReady(void)
{
    return g_memo.document != nullptr ? 1 : 0;
}

int MioPan_RmlMenuMemoShow(const MIOPAN_RML_MENU_MEMO *page)
{
    if (g_memo.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_memo.requested = true;
    const bool new_pak = page->pak != g_memo.page.pak;
    g_memo.page = *page;
    if (!g_memo.visible || new_pak)
    {
        /* A new visit: the page's pak may sit where another's did. */
        MioPanRmlSprite::Forget();
        g_memo.scale = MioPanRmlMenuArt::StageScale();
        g_memo.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_memo.shown_stage.clear();
    }
    if (!g_memo.visible)
    {
        g_memo.command = MIOPAN_RML_MENU_MEMO_NONE;
        g_memo.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_memo.visible = true;
    }
    g_memo.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuMemoIsShown(void)
{
    return g_memo.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuMemoConsumeCommand(int *arg)
{
    const int command = g_memo.command;
    if (arg != nullptr)
    {
        *arg = g_memo.command_arg;
    }
    g_memo.command = MIOPAN_RML_MENU_MEMO_NONE;
    g_memo.command_arg = 0;
    return command;
}
}
