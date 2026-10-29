#include "miopan_rml_menu_photo.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_photo_dat.h"
#include "../../ingame/photo/photo_make.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_photo.rml";

using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Number;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

constexpr int kSlots = MIOPAN_RML_MENU_PHOTO_SLOTS;
constexpr int kStateUse = 0x1;
constexpr int kStateProtect = 0x2;

/* Message 0x4a names the rooms, 0x38 asks before a delete, 0x36 entry 1 is
 * the empty album's line, and bank 8's 0, 1 and 2 are ":", "/" and
 * "pts.". */
constexpr int kMsgRoom = 0x4a;
constexpr int kMsgAsk = 0x38;
constexpr int kMsgEmpty = 0x36;
constexpr int kMsgEmptyPhoto = 1;
constexpr int kMsgCommon = 8;

/* menu_photo[]: the title's plates; the data window's pieces, its rules laid
 * on their side; the enlarged picture's frame -- the stretched ones and their
 * stretches -- and its protect corners; each slot's empty plate, number,
 * highlight and padlock; the shot's menu's lit and dim rows and their words;
 * the sort menu's plate, rows and words. */
constexpr int kTitlePlates[2] = {185, 186};
constexpr int kDataFirst = 0;
constexpr int kDataCount = 7;
constexpr int kDataRulesFirst = 154;
constexpr int kDataRulesCount = 15;
constexpr int kDataSidesFirst = 169;
constexpr int kDataSidesCount = 4;
constexpr int kDataLastFirst = 173;
constexpr int kDataLastCount = 11;
constexpr float kFrameTall = 239.0f;
constexpr float kFrameWide = 375.0f;
constexpr int kProtectCorners = 79;
constexpr int kSlotPlate = 83;
constexpr int kSlotPadlock = 99;
constexpr int kSlotLit = 115;
constexpr int kSlotNumbers[kSlots][2] = {
    {131, -1}, {132, -1}, {133, -1}, {134, -1}, {135, -1}, {136, -1},
    {137, -1}, {138, -1}, {139, -1}, {140, 141}, {142, 143}, {144, 145},
    {146, 147}, {148, 149}, {150, 151}, {152, 153},
};
constexpr int kLargeNumbers[kSlots][2] = {
    {46, -1}, {47, -1}, {48, -1}, {49, -1}, {50, -1}, {51, -1}, {52, -1}, {53, -1},
    {54, -1}, {55, 56}, {57, 58}, {59, 60}, {61, 62}, {63, 64}, {65, 66}, {67, 68},
};
constexpr int kMenuLit[3][2] = {{195, 196}, {197, 198}, {199, 200}};
constexpr int kMenuDim[3][2] = {{201, 202}, {203, 204}, {205, 206}};
constexpr int kMenuWords = 188;
constexpr int kSortPlate[2] = {207, 208};
constexpr int kSortLit[3][2] = {{209, 210}, {211, 212}, {213, 214}};
constexpr int kSortDim[3][2] = {{215, 216}, {217, 218}, {219, 220}};
constexpr int kSortWords = 191;

/* menu_photo.c's layout: the cells, 34 apart in two columns; the enlarged
 * picture; the data window's lines; the windows that ask. */
constexpr int kCellX[2] = {32, 87};
constexpr int kCellY = 74;
constexpr int kCellPitch = 34;
constexpr int kCellW = 45;
constexpr int kCellH = 30;
constexpr int kLargeX = 181;
constexpr int kLargeY = 78;
constexpr int kLargeW = 384;
constexpr int kLargeH = 256;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuPhotoState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *album = nullptr;
    Rml::Element *popup = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_stage;
    std::string shown_album;
    std::string shown_popup;
    std::string shown_prompts;

    Scale scale;
    MIOPAN_RML_MENU_PHOTO page = {};
    bool yes = true;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_PHOTO_NONE;
    int command_arg = 0;
};

MenuPhotoState g_photo;
Listener g_listener;

uintptr_t Pak()
{
    return (uintptr_t)g_photo.page.pak;
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
                  x * g_photo.scale.x, y * g_photo.scale.y, w * g_photo.scale.x,
                  h * g_photo.scale.y);
    return style;
}

std::string Game(int type, int id)
{
    return MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(type, id));
}

std::string Art(int sprite, uintptr_t pak = 0)
{
    return MioPanRmlMenuArt::Art(g_photo.scale, pak != 0 ? pak : Pak(), menu_photo[sprite]);
}

std::string Stretched(int sprite, float w, float h)
{
    const SPRT_DAT &piece = menu_photo[sprite];
    return ArtIn(g_photo.scale, Pak(), piece, piece.x, piece.y, w, h);
}

std::string Added(int sprite, float x, float y, float w, float h, uintptr_t pak = 0)
{
    MioPanRmlSprite::Look added;
    added.additive = true;
    return ArtIn(g_photo.scale, pak != 0 ? pak : Pak(), menu_photo[sprite], x, y, w, h,
                 added);
}

std::string Two(int value)
{
    char text[8];
    std::snprintf(text, sizeof(text), "%02d", value % 100);
    return text;
}

/* The cells: a photograph's thumbnail, or an empty slot's plate and number;
 * the highlight on the selected one, pulsing while the grid has the pad; the
 * padlocks. */
std::string Cells()
{
    const MIOPAN_RML_MENU_PHOTO &page = g_photo.page;
    std::string rml;
    for (int i = 0; i < kSlots; i++)
    {
        const float x = (float)kCellX[i / 8];
        const float y = (float)(kCellY + (i % 8) * kCellPitch);
        if ((page.state[i] & kStateUse) != 0 && page.thumb[i] != 0)
        {
            rml += MioPanRmlMenuArt::ImageIn(
                g_photo.scale,
                MioPanRmlSprite::Texels(page.thumb[i], SPHOTO_W, SPHOTO_H, 64), x, y,
                kCellW, kCellH);
        }
        else
        {
            rml += Art(kSlotPlate + i);
            for (const int number : kSlotNumbers[i])
            {
                if (number >= 0)
                {
                    rml += Art(number);
                }
            }
        }
        if (i == page.selected)
        {
            const SPRT_DAT &lit = menu_photo[kSlotLit + i];
            const bool pulse = page.mode == MIOPAN_RML_MENU_PHOTO_GRID;
            rml += std::string("<div class=\"layer") + (pulse ? " pulse" : "") + "\">" +
                   Added(kSlotLit + i, lit.x, lit.y, lit.w, lit.h) + "</div>";
        }
        rml += "<div id=\"cell-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(x - 2, y - 2, kCellW + 4, kCellPitch) + "\"></div>";
    }
    for (int i = 0; i < kSlots; i++)
    {
        if ((page.state[i] & kStateProtect) != 0)
        {
            rml += Art(kSlotPadlock + i);
        }
    }
    return rml;
}

/* The data window and what the selected photograph knows of itself: its
 * room, when it was taken, its score and what it caught, in the ink the
 * game writes them in. */
std::string Data()
{
    const Scale &scale = g_photo.scale;
    const MIOPAN_RML_MENU_PHOTO &page = g_photo.page;
    std::string rml;
    for (int i = 0; i < kDataCount; i++)
    {
        rml += Art(kDataFirst + i);
    }
    for (int i = 0; i < kDataRulesCount; i++)
    {
        rml += Art(kDataRulesFirst + i);
    }
    MioPanRmlSprite::Look back;
    back.turns = 3;
    for (int i = 0; i < kDataSidesCount; i++)
    {
        const SPRT_DAT &side = menu_photo[kDataSidesFirst + i];
        rml += ArtIn(scale, Pak(), side, side.x, side.y, side.h, side.w, back);
    }
    for (int i = 0; i < kDataLastCount; i++)
    {
        rml += Art(kDataLastFirst + i);
    }
    if ((page.state[page.selected] & kStateUse) == 0)
    {
        return rml;
    }
    if (page.room >= 0)
    {
        rml += Text(scale, Game(kMsgRoom, page.room), 78, 349, 280, "photo-ink");
    }
    const std::string slash = MioPanGameText::Message(kMsgCommon, 1);
    const std::string colon = MioPanGameText::Message(kMsgCommon, 0);
    rml += Text(scale, Two(page.day) + slash + Two(page.month) + slash + Two(page.year), 78,
                374, 120, "photo-ink");
    rml += Text(scale,
                Two(page.hour) + colon + Two(page.minute) + colon + Two(page.second), 204,
                374, 120, "photo-ink");
    rml += Number(scale, std::to_string(page.score), 182, 398, 88, "photo-ink");
    rml += Text(scale, MioPanGameText::Message(kMsgCommon, 2), 274, 398, 60, "photo-ink");
    const int first_y[4] = {0x162, 0x17a, 0x16e, 0x162};
    float y = (float)first_y[page.subjects > 3 ? 3 : page.subjects] - 4.0f;
    for (int i = 0; i < page.subjects && i < MIOPAN_RML_MENU_PHOTO_SUBJECTS; i++, y += 24.0f)
    {
        rml += Text(scale, Game(page.subject_type[i], page.subject_id[i]), 0x16f, y, 240,
                    "photo-ink");
    }
    return rml;
}

/* The enlarged picture in its frame -- the dark bed it is laid on, the
 * frame's pieces and stretches, the slot's number -- and the protect
 * corners over a protected one. */
std::string Large()
{
    const MIOPAN_RML_MENU_PHOTO &page = g_photo.page;
    std::string rml;
    for (int i = 0; i < 12; i++)
    {
        rml += Art(7 + i);
    }
    rml += "<div class=\"photo-bed\" style=\"" + Place(177, 78, 392, 256) + "\"></div>";
    if (page.large != 0)
    {
        rml += MioPanRmlMenuArt::ImageIn(
            g_photo.scale, MioPanRmlSprite::Texels(page.large, PHOTO_W, PHOTO_H, PHOTO_W),
            kLargeX, kLargeY, kLargeW, kLargeH);
    }
    for (int i = 0; i < 3; i++)
    {
        rml += Art(69 + i);
    }
    rml += Stretched(72, menu_photo[72].w, kFrameTall);
    rml += Stretched(73, kFrameWide, menu_photo[73].h);
    for (int i = 0; i < 3; i++)
    {
        rml += Art(74 + i);
    }
    rml += Stretched(78, menu_photo[78].w, kFrameTall);
    rml += Stretched(77, kFrameWide, menu_photo[77].h);
    for (int i = 0; i < 3; i++)
    {
        rml += Art(43 + i);
    }
    for (const int number : kLargeNumbers[page.selected])
    {
        if (number >= 0)
        {
            rml += Art(number);
        }
    }
    if ((page.state[page.selected] & kStateProtect) != 0)
    {
        for (int i = 0; i < 4; i++)
        {
            rml += Art(kProtectCorners + i);
        }
    }
    return rml;
}

/* A plate drawn as two halves, each scaled from the middle out: the left
 * about its right edge, the right about its left. */
std::string Halves(const int pair[2], float scale_x, bool added = false)
{
    std::string rml;
    const SPRT_DAT &left = menu_photo[pair[0]];
    const SPRT_DAT &right = menu_photo[pair[1]];
    MioPanRmlSprite::Look look;
    look.additive = added;
    rml += ArtIn(g_photo.scale, MENU_BG_TEX_ADRS, left, left.x + left.w * (1.0f - scale_x),
                 left.y, left.w * scale_x, left.h, look);
    rml += ArtIn(g_photo.scale, MENU_BG_TEX_ADRS, right, right.x, right.y, right.w * scale_x,
                 right.h, look);
    return rml;
}

/* The shot's menu: its three rows growing in, the one under the cursor lit
 * and pulsing while the menu has the pad, and their words. */
std::string Menu()
{
    const MIOPAN_RML_MENU_PHOTO &page = g_photo.page;
    char fade[48];
    std::snprintf(fade, sizeof(fade), "opacity: %.3f;",
                  (float)(page.menu_alpha > 0x80 ? 0x80 : page.menu_alpha) / 128.0f);
    std::string rml = "<div class=\"layer\" style=\"" + std::string(fade) + "\">";
    for (int i = 0; i < MIOPAN_RML_MENU_PHOTO_ROWS; i++)
    {
        if (i == page.menu_row)
        {
            const bool pulse = page.mode == MIOPAN_RML_MENU_PHOTO_MENU;
            rml += std::string("<div class=\"layer") + (pulse ? " pulse" : "") + "\">" +
                   Halves(kMenuLit[i], page.menu_scale) + "</div>";
        }
        else
        {
            rml += Halves(kMenuDim[i], page.menu_dim_scale);
        }
    }
    for (int i = 0; i < MIOPAN_RML_MENU_PHOTO_ROWS; i++)
    {
        rml += Art(kMenuWords + i);
        rml += "<div id=\"mrow-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(453, 94 + 35 * i, 164, 35) + "\"></div>";
    }
    return rml + "</div>";
}

/* The sort menu over it: its plate, the lit row held bigger and added on,
 * pulsing, the dim ones, and the words. */
std::string Sort()
{
    const MIOPAN_RML_MENU_PHOTO &page = g_photo.page;
    std::string rml;
    for (const int plate : kSortPlate)
    {
        rml += Art(plate, MENU_BG_TEX_ADRS);
    }
    for (int i = 0; i < MIOPAN_RML_MENU_PHOTO_ROWS; i++)
    {
        const bool lit = i == page.sort_row;
        const int *pair = lit ? kSortLit[i] : kSortDim[i];
        const float scale_x = lit ? 0.8f : 0.7f;
        MioPanRmlSprite::Look look;
        look.additive = lit;
        std::string row;
        for (int j = 0; j < 2; j++)
        {
            const SPRT_DAT &half = menu_photo[pair[j]];
            row += ArtIn(g_photo.scale, MENU_BG_TEX_ADRS, half, half.x, half.y,
                         half.w * scale_x, half.h, look);
        }
        rml += lit ? "<div class=\"layer pulse\">" + row + "</div>" : row;
    }
    for (int i = 0; i < 4; i++)
    {
        const SPRT_DAT &word = menu_photo[kSortWords + i];
        rml += Added(kSortWords + i, word.x, word.y, word.w, word.h);
    }
    for (int i = 0; i < MIOPAN_RML_MENU_PHOTO_ROWS; i++)
    {
        rml += "<div id=\"srow-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(469, 129 + 35 * i, 148, 35) + "\"></div>";
    }
    return rml;
}

/* "Delete this photo?" with its answers, or "it is protected". */
std::string Ask(bool answers)
{
    std::string rml = MioPanRmlMenuArt::Window(g_photo.scale, 24, 178, 592, 112);
    rml += Text(g_photo.scale, Game(kMsgAsk, answers ? 0 : 1), 68, 194, 520, "window-text");
    if (answers)
    {
        rml += "<span id=\"choice-yes\" class=\"window-choice" +
               std::string(g_photo.yes ? " selected" : "") + "\" style=\"" +
               Place(155, 230, 120, 30) + "\">Yes</span>";
        rml += "<span id=\"choice-no\" class=\"window-choice" +
               std::string(g_photo.yes ? "" : " selected") + "\" style=\"" +
               Place(362, 230, 120, 30) + "\">No</span>";
    }
    return rml;
}

std::string Popup()
{
    switch (g_photo.page.mode)
    {
    case MIOPAN_RML_MENU_PHOTO_MENU: return Menu();
    case MIOPAN_RML_MENU_PHOTO_DELETE: return Menu() + Ask(true);
    case MIOPAN_RML_MENU_PHOTO_PROTECTED: return Menu() + Ask(false);
    case MIOPAN_RML_MENU_PHOTO_SORT: return Sort();
    default: return std::string();
    }
}

std::string Album()
{
    if (g_photo.page.mode == MIOPAN_RML_MENU_PHOTO_EMPTY)
    {
        using namespace MioPanRmlMenuArt;
        return Window(g_photo.scale, kNoticeX, kNoticeY, kNoticeW, kNoticeH) +
               Text(g_photo.scale, Game(kMsgEmpty, kMsgEmptyPhoto), kNoticeTextX,
                    kNoticeTextY, kNoticeW - (kNoticeTextX - kNoticeX), "window-text");
    }
    return Cells() + Data() + Large();
}

std::string Stage()
{
    std::string rml;
    for (const int plate : kTitlePlates)
    {
        rml += Art(plate, MENU_BG_TEX_ADRS);
    }
    rml += Text(g_photo.scale, "Photo", 41, 20, 120, "menu-title");
    rml += "<div id=\"album\" class=\"layer\"></div>";
    rml += "<div id=\"popup\" class=\"layer\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) +
           "\"></div>";
    return rml;
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kGrid[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Menu", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kPopup[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kNotice[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "OK", nullptr},
    };
    static const MioPanRmlCommon::Prompt kBack[] = {
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts;
    if (!g_photo.page.closing)
    {
        switch (g_photo.page.mode)
        {
        case MIOPAN_RML_MENU_PHOTO_GRID: prompts = MioPanRmlCommon::PromptsRml(kGrid, 2); break;
        case MIOPAN_RML_MENU_PHOTO_PROTECTED:
            prompts = MioPanRmlCommon::PromptsRml(kNotice, 1);
            break;
        case MIOPAN_RML_MENU_PHOTO_EMPTY: prompts = MioPanRmlCommon::PromptsRml(kBack, 1); break;
        default: prompts = MioPanRmlCommon::PromptsRml(kPopup, 2); break;
        }
    }
    SetRml(g_photo.prompts, g_photo.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_photo.shown_stage)
    {
        return;
    }
    g_photo.stage->SetInnerRML(rml);
    g_photo.shown_stage = rml;
    g_photo.album = g_photo.document->GetElementById("album");
    g_photo.popup = g_photo.document->GetElementById("popup");
    g_photo.prompts = g_photo.document->GetElementById("prompts");
    g_photo.shown_album.clear();
    g_photo.shown_popup.clear();
    g_photo.shown_prompts.clear();
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_photo.scale.x || scale.y != g_photo.scale.y)
    {
        g_photo.scale = scale;
        g_photo.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_photo.shown_stage.clear();
    }
    BuildStage();
    SetRml(g_photo.album, g_photo.shown_album, Album());
    SetRml(g_photo.popup, g_photo.shown_popup, Popup());
    RefreshPrompts();
}

void Queue(int command, int arg = 0)
{
    g_photo.command = command;
    g_photo.command_arg = arg;
}

void SelectAnswer(bool yes)
{
    if (yes != g_photo.yes)
    {
        g_photo.yes = yes;
        MioPanRmlCommon::PlayCursor();
        SetRml(g_photo.popup, g_photo.shown_popup, Popup());
    }
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool enter = key == Rml::Input::KI_RETURN ||
                       key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE;
    const bool back = key == Rml::Input::KI_ESCAPE;
    const bool side = key == Rml::Input::KI_LEFT || key == Rml::Input::KI_RIGHT;
    switch (g_photo.page.mode)
    {
    case MIOPAN_RML_MENU_PHOTO_DELETE:
        if (side)
        {
            SelectAnswer(!g_photo.yes);
            break;
        }
        if (!enter && !back)
        {
            return;
        }
        Queue(back ? MIOPAN_RML_MENU_PHOTO_BACK
                   : (g_photo.yes ? MIOPAN_RML_MENU_PHOTO_YES : MIOPAN_RML_MENU_PHOTO_NO));
        break;
    case MIOPAN_RML_MENU_PHOTO_PROTECTED:
        if (!enter && !back)
        {
            return;
        }
        Queue(back ? MIOPAN_RML_MENU_PHOTO_BACK : MIOPAN_RML_MENU_PHOTO_ACK);
        break;
    case MIOPAN_RML_MENU_PHOTO_EMPTY:
        if (!back)
        {
            return;
        }
        Queue(MIOPAN_RML_MENU_PHOTO_BACK);
        break;
    default:
        if (key == Rml::Input::KI_UP)
        {
            Queue(MIOPAN_RML_MENU_PHOTO_UP);
        }
        else if (key == Rml::Input::KI_DOWN)
        {
            Queue(MIOPAN_RML_MENU_PHOTO_DOWN);
        }
        else if (side && g_photo.page.mode == MIOPAN_RML_MENU_PHOTO_GRID)
        {
            Queue(MIOPAN_RML_MENU_PHOTO_SIDE);
        }
        else if (enter || back)
        {
            Queue(back ? MIOPAN_RML_MENU_PHOTO_BACK : MIOPAN_RML_MENU_PHOTO_CHOOSE);
        }
        else
        {
            return;
        }
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
    const int mode = g_photo.page.mode;
    if (click && !Owner(target, "prompt-back").empty())
    {
        Queue(MIOPAN_RML_MENU_PHOTO_BACK);
        return;
    }
    if (event == Rml::EventId::Mousescroll)
    {
        const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
        if (delta != 0.0f && (mode == MIOPAN_RML_MENU_PHOTO_GRID ||
                              mode == MIOPAN_RML_MENU_PHOTO_MENU ||
                              mode == MIOPAN_RML_MENU_PHOTO_SORT))
        {
            Queue(delta < 0.0f ? MIOPAN_RML_MENU_PHOTO_UP : MIOPAN_RML_MENU_PHOTO_DOWN);
        }
        return;
    }
    const char *prefix = mode == MIOPAN_RML_MENU_PHOTO_GRID   ? "cell-"
                         : mode == MIOPAN_RML_MENU_PHOTO_MENU ? "mrow-"
                         : mode == MIOPAN_RML_MENU_PHOTO_SORT ? "srow-"
                                                              : nullptr;
    if (prefix != nullptr)
    {
        const std::string hit = Owner(target, prefix);
        if (hit.empty())
        {
            return;
        }
        const int index = std::atoi(hit.c_str() + 5);
        const int at = mode == MIOPAN_RML_MENU_PHOTO_GRID   ? g_photo.page.selected
                       : mode == MIOPAN_RML_MENU_PHOTO_MENU ? g_photo.page.menu_row
                                                            : g_photo.page.sort_row;
        const int move = mode == MIOPAN_RML_MENU_PHOTO_GRID ? MIOPAN_RML_MENU_PHOTO_CELL
                                                            : MIOPAN_RML_MENU_PHOTO_ROW;
        if (click)
        {
            Queue(index == at ? MIOPAN_RML_MENU_PHOTO_CHOOSE : move, index);
        }
        else if (MioPanRml::PointerActive() && index != at)
        {
            Queue(move, index);
        }
        return;
    }
    if (mode == MIOPAN_RML_MENU_PHOTO_DELETE)
    {
        const std::string choice = Owner(target, "choice-");
        if (choice.empty())
        {
            return;
        }
        const bool yes = choice == "choice-yes";
        if (click)
        {
            g_photo.yes = yes;
            Queue(yes ? MIOPAN_RML_MENU_PHOTO_YES : MIOPAN_RML_MENU_PHOTO_NO);
        }
        else if (MioPanRml::PointerActive())
        {
            SelectAnswer(yes);
        }
    }
    else if (mode == MIOPAN_RML_MENU_PHOTO_PROTECTED && click)
    {
        Queue(MIOPAN_RML_MENU_PHOTO_ACK);
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, and nothing
     * while the page fades out. */
    if (!g_photo.visible || g_photo.page.closing ||
        g_photo.command != MIOPAN_RML_MENU_PHOTO_NONE)
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

namespace MioPanRmlMenuPhoto
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_photo.document = context->LoadDocument(kDocumentPath);
    if (g_photo.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_photo.stage = g_photo.document->GetElementById("stage");
    if (g_photo.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_photo.document->Close();
        g_photo = MenuPhotoState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_photo.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_photo = MenuPhotoState();
}

void Frame()
{
    if (g_photo.visible && !g_photo.requested)
    {
        g_photo.document->Hide();
        g_photo.visible = false;
        g_photo.command = MIOPAN_RML_MENU_PHOTO_NONE;
    }
    g_photo.shown_last_frame = g_photo.visible;
    g_photo.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuPhotoReady(void)
{
    return g_photo.document != nullptr ? 1 : 0;
}

int MioPan_RmlMenuPhotoShow(const MIOPAN_RML_MENU_PHOTO *page)
{
    if (g_photo.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_photo.requested = true;
    const int was_mode = g_photo.page.mode;
    const bool new_pak = page->pak != g_photo.page.pak;
    g_photo.page = *page;
    if (!g_photo.visible || new_pak)
    {
        /* A new visit: the page's pak may sit where another's did. */
        MioPanRmlSprite::Forget();
        g_photo.scale = MioPanRmlMenuArt::StageScale();
        g_photo.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_photo.shown_stage.clear();
    }
    if (!g_photo.visible)
    {
        g_photo.command = MIOPAN_RML_MENU_PHOTO_NONE;
        g_photo.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_photo.visible = true;
    }
    /* The window asks with its cursor on Yes, as MenuYesNoCtrlInit() puts
     * it. */
    if (page->mode == MIOPAN_RML_MENU_PHOTO_DELETE && was_mode != page->mode)
    {
        g_photo.yes = true;
    }
    g_photo.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuPhotoIsShown(void)
{
    return g_photo.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuPhotoConsumeCommand(int *arg)
{
    const int command = g_photo.command;
    if (arg != nullptr)
    {
        *arg = g_photo.command_arg;
    }
    g_photo.command = MIOPAN_RML_MENU_PHOTO_NONE;
    g_photo.command_arg = 0;
    return command;
}
}
