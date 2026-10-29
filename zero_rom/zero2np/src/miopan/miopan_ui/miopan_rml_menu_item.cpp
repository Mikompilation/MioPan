#include "miopan_rml_menu_item.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_menu_status.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_item_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_item.rml";

using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Number;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

constexpr int kRows = MIOPAN_RML_MENU_ITEM_ROWS;

/* The item names are message type 0x2c, their descriptions 0x2b, the "use
 * this?" questions 0x33, the "can't use" lines 0x52, and the empty page's
 * line 0x36 entry 0 -- each by item id. */
constexpr int kMsgName = 0x2c;
constexpr int kMsgDescription = 0x2b;
constexpr int kMsgUse = 0x33;
constexpr int kMsgNoUse = 0x52;
constexpr int kMsgEmpty = 0x36;

/* Item 0, the Type-7 film, never runs out: its count is two dashes. */
constexpr int kItemUnlimited = 0;

/* menu_item[], as menu_item.c draws it: the picture's plate, the frame's
 * strips and corners, the magnifier, the scrollbar's rail, thumb and arrows,
 * the description window's first piece, a lit and a dim row's first piece,
 * the picture's place and the title's two plates. */
constexpr int kPlate = 0;
constexpr int kFrameFirst = 1;
constexpr int kFrameLast = 20;
constexpr int kLens = 21;
constexpr int kRailTop[2] = {22, 23};
constexpr int kRailTick = 24;
constexpr int kRailTail = 25;
constexpr int kThumbTop = 26;
constexpr int kThumbBottom = 27;
constexpr int kThumbMiddle = 28;
constexpr int kArrows[2] = {29, 30};
constexpr int kDocFirst = 31;
constexpr int kRowLitFirst = 51;
constexpr int kRowDimFirst = 79;
constexpr int kPicture = 100;
constexpr int kTitlePlates[2] = {103, 104};

/* menu_item.c's layout: the two black panels, the scrollbar's backing and
 * rail, the rows 35 apart -- each name and count set in the height of the
 * dim bar, from its top, the count centred in the box the bar ends in -- the
 * description's place and the status plate's origin. */
constexpr int kRowPitch = 35;
constexpr int kRowTop = 157;
constexpr int kNameX = 80;
constexpr int kCountX = 332;
constexpr int kCountW = 48;
constexpr int kRailSize = 206;
constexpr int kRailTopY = 175;
constexpr int kStatusX = 34;
constexpr int kStatusY = 57;

/* The yes / no window, as MenuItemUseSelDisp() places it. */
constexpr int kWindowX = 24;
constexpr int kWindowY = 178;
constexpr int kWindowW = 592;
constexpr int kWindowH = 112;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuItemState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *thumb = nullptr;
    Rml::Element *pictures = nullptr;
    Rml::Element *rows = nullptr;
    Rml::Element *explain = nullptr;
    Rml::Element *status = nullptr;
    Rml::Element *prompts = nullptr;
    Rml::Element *window = nullptr;
    std::string shown_stage;
    std::string shown_thumb;
    std::string shown_pictures;
    std::string shown_rows;
    std::string shown_explain;
    std::string shown_status;
    std::string shown_prompts;
    std::string shown_window;

    Scale scale;
    MIOPAN_RML_MENU_ITEM page = {};
    bool yes = false;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_ITEM_NONE;
    int command_arg = 0;
};

MenuItemState g_item;
Listener g_listener;

uintptr_t Pak()
{
    return (uintptr_t)g_item.page.pak;
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
                  x * g_item.scale.x, y * g_item.scale.y, w * g_item.scale.x,
                  h * g_item.scale.y);
    return style;
}

/* One of the game's flat squares, black at its alpha. */
std::string Square(float x, float y, float w, float h, int alpha)
{
    char style[64];
    std::snprintf(style, sizeof(style), " background-color: #000000%02x;",
                  alpha * 2 > 255 ? 255 : alpha * 2);
    return "<div class=\"square\" style=\"" + Place(x, y, w, h) + style + "\"></div>";
}

std::string Shown(int rows)
{
    return std::to_string(rows < kRows ? rows : kRows);
}

/* What only changes with the pak or the picture's size: the title, the two
 * panels, the scrollbar's rail, the frame, the magnifier and the picture's
 * plate, each in MenuItemDisp()'s order with the layers the frame fills in
 * between them. */
std::string Stage()
{
    const Scale &scale = g_item.scale;
    std::string rml;
    for (const int plate : kTitlePlates)
    {
        rml += Art(scale, MENU_BG_TEX_ADRS, menu_item[plate]);
    }
    rml += Text(scale, "Item", 41, 20, 120, "menu-title");

    /* MenuItemWindowDisp(): the panels, then the scrollbar -- its backing,
     * rail and ticks, with the thumb and arrows over them -- then the
     * frame. */
    rml += Square(57, 130, 333, 296, 0x59);
    rml += Square(395, 276, 207, 146, 0x26);
    rml += Square(37, 135, 20, 20, 0x59);
    rml += Square(37, 399, 20, 20, 0x59);
    for (const int piece : kRailTop)
    {
        rml += Art(scale, Pak(), menu_item[piece]);
    }
    for (int i = 0; i < 15; i++)
    {
        rml += Art(scale, Pak(), menu_item[kRailTick], 0, i * 10);
    }
    rml += Art(scale, Pak(), menu_item[kRailTail]);
    rml += "<div id=\"thumb\" class=\"layer\"></div>";
    rml += "<div class=\"layer\">";
    for (int i = 0; i < 2; i++)
    {
        const SPRT_DAT &arrow = menu_item[kArrows[i]];
        rml += "<div id=\"arrow-" + std::to_string(i) + "\" class=\"scroll-arrow\" style=\"" +
               Place(arrow.x, arrow.y, arrow.w, arrow.h) + "\">" +
               ArtIn(scale, Pak(), arrow, 0, 0, arrow.w, arrow.h) + "</div>";
    }
    rml += "</div>";
    for (int i = kFrameFirst; i <= kFrameLast; i++)
    {
        rml += Art(scale, Pak(), menu_item[i]);
    }

    /* The magnifier at twice its size, added to what is under it; the
     * picture over it, and the round plate over that. */
    MioPanRmlSprite::Look added;
    added.additive = true;
    const SPRT_DAT &lens = menu_item[kLens];
    rml += ArtIn(scale, Pak(), lens, lens.x, lens.y, lens.w * 2, lens.h * 2, added);
    rml += "<div id=\"pictures\" class=\"layer\"></div>";
    rml += Art(scale, Pak(), menu_item[kPlate]);

    /* The rows with the description window, the status plate, the hints
     * and the windows over everything. */
    rml += "<div id=\"rows\" class=\"layer\"></div>";
    rml += "<div id=\"explain\" class=\"layer\"></div>";
    rml += "<div id=\"status\" class=\"plate\" style=\"" +
           Place(kStatusX, kStatusY, 300, 70) + "\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" +
           Place(250, 30, 380, 30) + "\"></div>";
    rml += "<div id=\"window\" class=\"layer\"></div>";
    return rml;
}

/* The thumb, sized and placed from the list as MenuItemListScrollDisp() does:
 * the whole rail for a list that fits, a row's share less per row past the
 * seventh; two caps and a middle, or two squashed caps when it is shorter
 * than they are. */
std::string Thumb()
{
    const Scale &scale = g_item.scale;
    const int count = g_item.page.count;
    if (count <= 0)
    {
        return std::string();
    }
    int size = kRailSize;
    int y = kRailTopY;
    if (count >= kRows)
    {
        size = kRailSize - (kRailSize / count) * (count - kRows);
        y = (kRailSize / count) * g_item.page.start + kRailTopY;
    }
    const SPRT_DAT &top = menu_item[kThumbTop];
    const SPRT_DAT &bottom = menu_item[kThumbBottom];
    const SPRT_DAT &middle = menu_item[kThumbMiddle];
    const int centre = size - (top.h + bottom.h);
    std::string rml;
    if (centre >= 0)
    {
        rml += ArtIn(scale, Pak(), top, top.x, y, top.w, top.h);
        rml += ArtIn(scale, Pak(), middle, middle.x, y + top.h, middle.w, centre);
        rml += ArtIn(scale, Pak(), bottom, bottom.x, y + top.h + centre, bottom.w,
                     bottom.h);
    }
    else
    {
        rml += ArtIn(scale, Pak(), top, top.x, y, top.w, size / 2);
        rml += ArtIn(scale, Pak(), bottom, bottom.x, y + size / 2, bottom.w, size / 2);
    }
    return rml;
}

/* The two pictures of the cross-fade, the one going out under the one
 * coming in. */
std::string Pictures()
{
    const SPRT_DAT &place = menu_item[kPicture];
    std::string rml;
    for (int i = 0; i < 2; i++)
    {
        if (g_item.page.picture[i] == nullptr || g_item.page.picture_alpha[i] <= 0)
        {
            continue;
        }
        rml += MioPanRmlMenuArt::ImageIn(
            g_item.scale, MioPanRmlSprite::Picture(g_item.page.picture[i]), place.x,
            place.y, place.w, place.h,
            (float)(g_item.page.picture_alpha[i] > 0x80 ? 0x80
                                                        : g_item.page.picture_alpha[i]) /
                128.0f);
    }
    return rml;
}

/* The rows shown: the lit row's four pieces, its middle repeated twenty
 * times; a dim row's three, its middle strip turned a quarter and repeated
 * twenty-three times across -- as MenuPlyrItemDisp() lays them -- with the
 * item's name and how many. */
std::string Rows()
{
    const Scale &scale = g_item.scale;
    const MIOPAN_RML_MENU_ITEM &page = g_item.page;
    const int shown = page.count < kRows ? page.count : kRows;
    std::string rml;
    for (int i = 0; i < shown; i++)
    {
        const bool lit = i == page.cursor;
        std::string row = "<div id=\"row-" + std::to_string(i) + "\" class=\"item-row" +
                          (lit ? " selected" : "") + "\" style=\"" +
                          Place(51, 154 + i * kRowPitch, 333, 34) + "\"></div>";
        if (lit)
        {
            const int first = kRowLitFirst + i * 4;
            rml += Art(scale, Pak(), menu_item[first]);
            const SPRT_DAT &middle = menu_item[first + 2];
            for (int j = 0; j < 20; j++)
            {
                rml += Art(scale, Pak(), middle, middle.w * j, 0);
            }
            rml += Art(scale, Pak(), menu_item[first + 3]);
            rml += Art(scale, Pak(), menu_item[first + 1]);
        }
        else
        {
            const int first = kRowDimFirst + i * 3;
            rml += Art(scale, Pak(), menu_item[first]);
            const SPRT_DAT &strip = menu_item[first + 2];
            MioPanRmlSprite::Look turned;
            turned.turns = 1;
            for (int j = 0; j < 23; j++)
            {
                rml += ArtIn(scale, Pak(), strip, strip.x + strip.h * j, strip.y, strip.h,
                             strip.w, turned);
            }
            rml += Art(scale, Pak(), menu_item[first + 1]);
        }
        const char *cls = lit ? "item-name lit" : "item-name";
        rml += Text(scale, MioPanGameText::Message(kMsgName, page.item[i]), kNameX,
                    kRowTop + i * kRowPitch, 240, cls);
        std::string count = page.item[i] == kItemUnlimited ? "\xE2\x80\x93 \xE2\x80\x93"
                                                           : std::to_string(page.have[i]);
        rml += Text(scale, count, kCountX, kRowTop + i * kRowPitch, kCountW,
                    lit ? "item-count lit" : "item-count");
        rml += row;
    }
    return rml;
}

/* The description window under the picture, five strips of four pieces
 * with the second repeated eleven times, and the item's description on
 * it. */
std::string Explain()
{
    const Scale &scale = g_item.scale;
    const MIOPAN_RML_MENU_ITEM &page = g_item.page;
    if (page.count <= 0)
    {
        return std::string();
    }
    std::string rml;
    for (int i = 0; i < 5; i++)
    {
        for (int j = 0; j < 4; j++)
        {
            const SPRT_DAT &piece = menu_item[kDocFirst + i * 4 + j];
            if (j == 1)
            {
                for (int k = 0; k < 11; k++)
                {
                    rml += Art(scale, Pak(), piece, piece.w * k, 0);
                }
            }
            else
            {
                rml += Art(scale, Pak(), piece);
            }
        }
    }
    /* Between the strips' end caps, a line to each strip.  The game prints
     * from x 401, inside its glyphs' own cells; this font has none, so it
     * starts past the cap instead. */
    const SPRT_DAT &left_cap = menu_item[kDocFirst];
    const SPRT_DAT &right_cap = menu_item[kDocFirst + 3];
    const float text_x = (float)(left_cap.x + left_cap.w + 3);
    const float text_w = (float)(right_cap.x - 3) - text_x;
    char style[160];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  text_x * scale.x, (float)left_cap.y * scale.y, text_w * scale.x,
                  130 * scale.y);
    rml += "<p class=\"item-description\" style=\"" + std::string(style) + "\">" +
           MioPanRmlCommon::Escape(MioPanRmlCommon::NameButtonIcons(
               MioPanGameText::Message(kMsgDescription, page.selected))) +
           "</p>";
    return rml;
}

/* The "use this?" window with its two answers, the "can't use" one, and the
 * empty page's, each where the game opens it. */
std::string Window()
{
    const MIOPAN_RML_MENU_ITEM &page = g_item.page;
    int msg_type = -1;
    int msg_id = page.selected;
    switch (page.mode)
    {
    case MIOPAN_RML_MENU_ITEM_USE_SEL: msg_type = kMsgUse; break;
    case MIOPAN_RML_MENU_ITEM_NO_USE: msg_type = kMsgNoUse; break;
    case MIOPAN_RML_MENU_ITEM_EMPTY:
        msg_type = kMsgEmpty;
        msg_id = 0;
        break;
    default: return std::string();
    }
    /* The empty page's line in message 0x36's own window. */
    if (page.mode == MIOPAN_RML_MENU_ITEM_EMPTY)
    {
        using namespace MioPanRmlMenuArt;
        return MioPanRmlMenuArt::Window(g_item.scale, kNoticeX, kNoticeY, kNoticeW, kNoticeH,
                                        "item-window") +
               Text(g_item.scale, MioPanGameText::Message(msg_type, msg_id), kNoticeTextX,
                    kNoticeTextY, kNoticeW - (kNoticeTextX - kNoticeX), "window-text");
    }
    std::string rml = MioPanRmlMenuArt::Window(g_item.scale, kWindowX, kWindowY, kWindowW,
                                               kWindowH, "item-window");
    rml += Text(g_item.scale,
                MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(msg_type, msg_id)),
                68, 196, 520, "window-text");
    if (page.mode == MIOPAN_RML_MENU_ITEM_USE_SEL)
    {
        rml += "<span id=\"choice-yes\" class=\"window-choice" +
               std::string(g_item.yes ? " selected" : "") + "\" style=\"" +
               Place(155, 230, 120, 30) + "\">Yes</span>";
        rml += "<span id=\"choice-no\" class=\"window-choice" +
               std::string(g_item.yes ? "" : " selected") + "\" style=\"" +
               Place(362, 230, 120, 30) + "\">No</span>";
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
    static const MioPanRmlCommon::Prompt kAnswer[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kNotice[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "OK", nullptr},
    };
    static const MioPanRmlCommon::Prompt kEmpty[] = {
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts;
    if (!g_item.page.closing)
    {
        switch (g_item.page.mode)
        {
        case MIOPAN_RML_MENU_ITEM_LIST:
            prompts = MioPanRmlCommon::PromptsRml(kList, 3);
            break;
        case MIOPAN_RML_MENU_ITEM_USE_SEL:
            prompts = MioPanRmlCommon::PromptsRml(kAnswer, 2);
            break;
        case MIOPAN_RML_MENU_ITEM_NO_USE:
            prompts = MioPanRmlCommon::PromptsRml(kNotice, 1);
            break;
        default:
            prompts = MioPanRmlCommon::PromptsRml(kEmpty, 1);
            break;
        }
    }
    SetRml(g_item.prompts, g_item.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_item.shown_stage)
    {
        return;
    }
    g_item.stage->SetInnerRML(rml);
    g_item.shown_stage = rml;
    Rml::ElementDocument *document = g_item.document;
    g_item.thumb = document->GetElementById("thumb");
    g_item.pictures = document->GetElementById("pictures");
    g_item.rows = document->GetElementById("rows");
    g_item.explain = document->GetElementById("explain");
    g_item.status = document->GetElementById("status");
    g_item.prompts = document->GetElementById("prompts");
    g_item.window = document->GetElementById("window");
    g_item.shown_thumb.clear();
    g_item.shown_pictures.clear();
    g_item.shown_rows.clear();
    g_item.shown_explain.clear();
    g_item.shown_status.clear();
    g_item.shown_prompts.clear();
    g_item.shown_window.clear();
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_item.scale.x || scale.y != g_item.scale.y)
    {
        g_item.scale = scale;
        g_item.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_item.shown_stage.clear();
    }
    BuildStage();
    SetRml(g_item.thumb, g_item.shown_thumb, Thumb());
    SetRml(g_item.pictures, g_item.shown_pictures, Pictures());
    SetRml(g_item.rows, g_item.shown_rows, Rows());
    SetRml(g_item.explain, g_item.shown_explain, Explain());
    SetRml(g_item.status, g_item.shown_status, MioPanRmlMenuStatus::Rml(g_item.scale));
    SetRml(g_item.window, g_item.shown_window, Window());
    RefreshPrompts();
}

void Queue(int command, int arg = 0)
{
    g_item.command = command;
    g_item.command_arg = arg;
}

/* Yes and No in the window: the cursor is the screen's, and starts on No
 * each time the window opens, as the game's does. */
void SelectAnswer(bool yes)
{
    if (yes != g_item.yes)
    {
        g_item.yes = yes;
        MioPanRmlCommon::PlayCursor();
        SetRml(g_item.window, g_item.shown_window, Window());
    }
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool enter = key == Rml::Input::KI_RETURN ||
                       key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE;
    const bool back = key == Rml::Input::KI_ESCAPE;
    switch (g_item.page.mode)
    {
    case MIOPAN_RML_MENU_ITEM_LIST:
        switch (key)
        {
        case Rml::Input::KI_UP: Queue(MIOPAN_RML_MENU_ITEM_UP); break;
        case Rml::Input::KI_DOWN: Queue(MIOPAN_RML_MENU_ITEM_DOWN); break;
        case Rml::Input::KI_PRIOR: Queue(MIOPAN_RML_MENU_ITEM_PAGE_UP); break;
        case Rml::Input::KI_NEXT: Queue(MIOPAN_RML_MENU_ITEM_PAGE_DOWN); break;
        default:
            if (enter)
            {
                Queue(MIOPAN_RML_MENU_ITEM_CHOOSE);
            }
            else if (back)
            {
                Queue(MIOPAN_RML_MENU_ITEM_BACK);
            }
            else
            {
                return;
            }
            break;
        }
        break;
    case MIOPAN_RML_MENU_ITEM_USE_SEL:
        if (key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT)
        {
            SelectAnswer(!g_item.yes);
        }
        else if (enter)
        {
            Queue(g_item.yes ? MIOPAN_RML_MENU_ITEM_YES : MIOPAN_RML_MENU_ITEM_NO);
        }
        else if (back)
        {
            Queue(MIOPAN_RML_MENU_ITEM_BACK);
        }
        else
        {
            return;
        }
        break;
    case MIOPAN_RML_MENU_ITEM_NO_USE:
        if (enter)
        {
            Queue(MIOPAN_RML_MENU_ITEM_ACK);
        }
        else if (back)
        {
            Queue(MIOPAN_RML_MENU_ITEM_BACK);
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
        Queue(MIOPAN_RML_MENU_ITEM_BACK);
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
        Queue(MIOPAN_RML_MENU_ITEM_BACK);
        return;
    }
    switch (g_item.page.mode)
    {
    case MIOPAN_RML_MENU_ITEM_LIST:
    {
        if (event == Rml::EventId::Mousescroll)
        {
            const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
            if (delta != 0.0f)
            {
                Queue(delta < 0.0f ? MIOPAN_RML_MENU_ITEM_UP : MIOPAN_RML_MENU_ITEM_DOWN);
            }
            return;
        }
        const std::string row = Owner(target, "row-");
        const std::string arrow = Owner(target, "arrow-");
        if (click && !Owner(target, "prompt-page").empty())
        {
            Queue(MIOPAN_RML_MENU_ITEM_PAGE_DOWN);
        }
        else if (click && !arrow.empty())
        {
            Queue(arrow == "arrow-0" ? MIOPAN_RML_MENU_ITEM_UP : MIOPAN_RML_MENU_ITEM_DOWN);
        }
        else if (!row.empty())
        {
            const int index = std::atoi(row.c_str() + 4);
            if (click)
            {
                Queue(index == g_item.page.cursor ? MIOPAN_RML_MENU_ITEM_CHOOSE
                                                  : MIOPAN_RML_MENU_ITEM_ROW,
                      index);
            }
            else if (MioPanRml::PointerActive() && index != g_item.page.cursor)
            {
                Queue(MIOPAN_RML_MENU_ITEM_ROW, index);
            }
        }
        break;
    }
    case MIOPAN_RML_MENU_ITEM_USE_SEL:
    {
        const std::string choice = Owner(target, "choice-");
        if (choice.empty())
        {
            break;
        }
        const bool yes = choice == "choice-yes";
        if (click)
        {
            g_item.yes = yes;
            Queue(yes ? MIOPAN_RML_MENU_ITEM_YES : MIOPAN_RML_MENU_ITEM_NO);
        }
        else if (MioPanRml::PointerActive())
        {
            SelectAnswer(yes);
        }
        break;
    }
    case MIOPAN_RML_MENU_ITEM_NO_USE:
        if (click)
        {
            Queue(MIOPAN_RML_MENU_ITEM_ACK);
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
    if (!g_item.visible || g_item.page.closing ||
        g_item.command != MIOPAN_RML_MENU_ITEM_NONE)
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

namespace MioPanRmlMenuItem
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_item.document = context->LoadDocument(kDocumentPath);
    if (g_item.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_item.stage = g_item.document->GetElementById("stage");
    if (g_item.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_item.document->Close();
        g_item = MenuItemState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_item.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_item = MenuItemState();
}

void Frame()
{
    if (g_item.visible && !g_item.requested)
    {
        g_item.document->Hide();
        g_item.visible = false;
        g_item.command = MIOPAN_RML_MENU_ITEM_NONE;
    }
    g_item.shown_last_frame = g_item.visible;
    g_item.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuItemShow(const MIOPAN_RML_MENU_ITEM *page)
{
    if (g_item.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_item.requested = true;
    const int was_mode = g_item.page.mode;
    const bool new_pak = page->pak != g_item.page.pak;
    g_item.page = *page;
    if (!g_item.visible || new_pak)
    {
        /* A new visit: the page's pak may sit where another's did. */
        MioPanRmlSprite::Forget();
        g_item.scale = MioPanRmlMenuArt::StageScale();
        g_item.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_item.shown_stage.clear();
    }
    if (!g_item.visible)
    {
        g_item.stage->SetClass("closing", false);
        g_item.command = MIOPAN_RML_MENU_ITEM_NONE;
        g_item.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_item.visible = true;
    }
    if (page->mode == MIOPAN_RML_MENU_ITEM_USE_SEL && was_mode != page->mode)
    {
        g_item.yes = false;
    }
    g_item.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuItemIsShown(void)
{
    return g_item.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuItemConsumeCommand(int *arg)
{
    const int command = g_item.command;
    if (arg != nullptr)
    {
        *arg = g_item.command_arg;
    }
    g_item.command = MIOPAN_RML_MENU_ITEM_NONE;
    g_item.command_arg = 0;
    return command;
}
}
