#include "miopan_rml_menu_top.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_menu_status.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../common/variable.h"
#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/play_data.h"
#include "../../ingame/menu/tim_dat/menu_cmn_dat.h"
#include "../../ingame/menu/tim_dat/menu_top_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_top.rml";

using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Number;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

/* The pages, in MENU_STEP_* order -- the hub's own order down the screen --
 * as the game's plates name them. */
constexpr int kPages = 8;
constexpr const char *kPageLabels[kPages] = {"Map",  "Item", "Camera", "Photo",
                                             "File", "Memo", "Radio",
                                             "Spirit List"};

/* The hub's line for each page is message type 0x3a, in page order. */
constexpr int kMsgPageHelp = 0x3a;

/* menu_top.c's layout: the rows 35 apart from y 68, each a 164-wide frame
 * from x 32 with its plate from x 47; the status plate's origin; the
 * caption window and its line. */
constexpr int kRowX = 32;
constexpr int kRowY = 68;
constexpr int kRowPitch = 35;
constexpr int kRowW = 164;
constexpr int kLabelX = 47;
constexpr int kLabelW = 119;
constexpr int kStatusX = 206;
constexpr int kStatusY = 74;
constexpr int kCaptionX = 24;
constexpr int kCaptionY = 346;
constexpr int kCaptionW = 592;
constexpr int kCaptionH = 100;
constexpr int kCaptionTextX = 48;
constexpr int kCaptionTextY = 366;

/* menu_top[]: the title's two plates; the play-data sheet and the cap over
 * the chapter's pillar.  menu_cmn_dat[]: a row's frame, lit and dim, each a
 * half and its mirror. */
constexpr int kTitlePlates[2] = {32, 33};
constexpr int kPlayData[2] = {12, 13};
constexpr int kRowLit[2] = {36, 37};
constexpr int kRowDim[2] = {38, 39};

/* The rail beside the list grows a row's worth per page, from the two caps'
 * height, as MenuTopTitleDisp() measures it. */
constexpr int kRailStep = 30;

/* Frames between clock reads, as MenuTopNowDateDisp() spaces them. */
constexpr int kClockFrames = 15;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuTopState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *status = nullptr;
    Rml::Element *play = nullptr;
    Rml::Element *help = nullptr;
    Rml::Element *prompts = nullptr;
    Rml::Element *rows[kPages] = {};
    std::string shown_stage;
    std::string shown_status;
    std::string shown_play;
    std::string shown_help;
    std::string shown_prompts;

    Scale scale;
    unsigned open = 0;
    const void *chapter_pk2 = nullptr;
    int chapter_sprite = -1;
    int cursor = 0;
    bool closing = false;
    int clock_frames = 0;
    DATE_INFO now = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_TOP_NONE;
};

MenuTopState g_top;
Listener g_listener;

void SetRml(Rml::Element *element, std::string &shown, const std::string &rml)
{
    if (element != nullptr && rml != shown)
    {
        element->SetInnerRML(rml);
        shown = rml;
    }
}

bool IsOpen(int page)
{
    return page >= 0 && page < kPages && (g_top.open >> page & 1) != 0;
}

std::string Place(float x, float y, float w, float h)
{
    char style[128];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  x * g_top.scale.x, y * g_top.scale.y, w * g_top.scale.x,
                  h * g_top.scale.y);
    return style;
}

/* One page's row: the dim frame and the lit one, the cursor showing one of
 * them, and the page's name where the game puts its plate. */
std::string Row(int page, int shown_row)
{
    const Scale &scale = g_top.scale;
    const SPRT_DAT &half = menu_cmn_dat[kRowLit[0]];
    const float y = (float)(kRowY + shown_row * kRowPitch);
    std::string rml = "<div id=\"page-" + std::to_string(page) +
                      "\" class=\"page\" style=\"" + Place(kRowX, y, kRowW, half.h) +
                      "\">";
    rml += "<div class=\"bar-dim\">";
    rml += ArtIn(scale, MENU_PLAYDATA_TEX_ADRS, menu_cmn_dat[kRowDim[0]], 0, 0,
                 kRowW / 2, half.h);
    rml += ArtIn(scale, MENU_PLAYDATA_TEX_ADRS, menu_cmn_dat[kRowDim[1]], kRowW / 2, 0,
                 kRowW / 2, half.h);
    rml += "</div><div class=\"bar-lit\">";
    rml += ArtIn(scale, MENU_BG_TEX_ADRS, menu_cmn_dat[kRowLit[0]], 0, 0, kRowW / 2,
                 half.h);
    rml += ArtIn(scale, MENU_BG_TEX_ADRS, menu_cmn_dat[kRowLit[1]], kRowW / 2, 0,
                 kRowW / 2, half.h);
    rml += "</div>";
    rml += Number(scale, kPageLabels[page], kLabelX - kRowX, 0, kLabelW, "page-label");
    return rml + "</div>";
}

/* Everything that only changes with the pages open, the chapter or the
 * picture's size, drawn in MenuTopDisp()'s order. */
std::string Stage()
{
    const Scale &scale = g_top.scale;
    std::string rml;

    /* The title: its two plates, the word, and the rails across and down
     * the list. */
    for (const int plate : kTitlePlates)
    {
        rml += Art(scale, MENU_BG_TEX_ADRS, menu_top[plate]);
    }
    int rail = menu_cmn_dat[40].h + menu_cmn_dat[42].h;
    int shown_rows = 0;
    for (int i = 0; i < kPages; i++)
    {
        if (IsOpen(i))
        {
            rail += kRailStep;
            shown_rows++;
        }
    }
    rml += MioPanRmlMenuArt::RailAcross(scale, 0, 56, 307);
    rml += MioPanRmlMenuArt::RailDown(scale, 22, 0, (float)rail);
    rml += Text(scale, "Menu", 42, 20, 120, "menu-title");

    /* The open pages only, in the game's order; a closed page takes no
     * place. */
    int row = 0;
    for (int i = 0; i < kPages; i++)
    {
        if (IsOpen(i))
        {
            rml += Row(i, row++);
        }
    }

    /* The play-data sheet and the chapter's pillar, its plate out of the
     * chapter's own pak; the sheet's numbers and the status plate over them,
     * each filled in every frame. */
    for (const int plate : kPlayData)
    {
        rml += Art(scale, MENU_PLAYDATA_TEX_ADRS, menu_top[plate]);
    }
    if (g_top.chapter_pk2 != nullptr && g_top.chapter_sprite >= 0)
    {
        for (int i = 0; i < 3; i++)
        {
            rml += Art(scale, (uintptr_t)g_top.chapter_pk2,
                       menu_top[g_top.chapter_sprite + i]);
        }
    }
    rml += "<div id=\"play\" class=\"layer\"></div>";
    rml += "<div id=\"status\" class=\"plate\" style=\"" +
           Place(kStatusX, kStatusY, 300, 70) + "\"></div>";

    /* The caption window along the bottom, and the hints over the top. */
    rml += MioPanRmlMenuArt::Window(scale, kCaptionX, kCaptionY, kCaptionW, kCaptionH);
    rml += "<p id=\"help\" class=\"caption-text\" style=\"" +
           Place(kCaptionTextX, kCaptionTextY, kCaptionW - 2 * (kCaptionTextX - kCaptionX), 70) +
           "\"></p>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" +
           Place(330, 30, 300, 30) + "\"></div>";
    return rml;
}

void Select(int page)
{
    g_top.cursor = page;
    for (int i = 0; i < kPages; i++)
    {
        if (g_top.rows[i] != nullptr)
        {
            g_top.rows[i]->SetClass("selected", i == page);
        }
    }
    std::string help = MioPanRmlCommon::Escape(MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgPageHelp, page)));
    SetRml(g_top.help, g_top.shown_help, help);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_top.shown_stage)
    {
        return;
    }
    g_top.stage->SetInnerRML(rml);
    g_top.shown_stage = rml;
    g_top.status = g_top.document->GetElementById("status");
    g_top.play = g_top.document->GetElementById("play");
    g_top.help = g_top.document->GetElementById("help");
    g_top.prompts = g_top.document->GetElementById("prompts");
    for (int i = 0; i < kPages; i++)
    {
        g_top.rows[i] = g_top.document->GetElementById("page-" + std::to_string(i));
    }
    g_top.shown_status.clear();
    g_top.shown_play.clear();
    g_top.shown_help.clear();
    g_top.shown_prompts.clear();
}

void Queue(int command)
{
    g_top.command = command;
}

/* The next open page that way, going round, as MenuTopPad() steps. */
void Move(int direction)
{
    for (int step = 1; step <= kPages; step++)
    {
        const int page = (g_top.cursor + direction * step + kPages * 2) % kPages;
        if (IsOpen(page))
        {
            if (page != g_top.cursor)
            {
                Select(page);
                MioPanRmlCommon::PlayCursor();
            }
            return;
        }
    }
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    switch (key)
    {
    case Rml::Input::KI_UP: Move(-1); break;
    case Rml::Input::KI_DOWN: Move(1); break;
    /* The phase plays the cues as it opens the page or closes the menu. */
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Queue(MIOPAN_RML_MENU_TOP_CHOSEN); break;
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_MENU_TOP_BACK); break;
    default: return;
    }
    event.StopPropagation();
}

int RowAt(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < kPages; i++)
        {
            if (g_top.rows[i] != nullptr && element == g_top.rows[i])
            {
                return i;
            }
        }
    }
    return -1;
}

bool InElement(Rml::Element *element, const char *id)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element->GetId() == id)
        {
            return true;
        }
    }
    return false;
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, and nothing
     * while the hub fades out. */
    if (!g_top.visible || g_top.closing ||
        g_top.command != MIOPAN_RML_MENU_TOP_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    Rml::Element *target = event.GetTargetElement();
    const int row = RowAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && row >= 0 && row != g_top.cursor)
        {
            Select(row);
            MioPanRmlCommon::PlayCursor();
        }
    }
    else if (event == Rml::EventId::Click)
    {
        if (row >= 0)
        {
            Select(row);
            Queue(MIOPAN_RML_MENU_TOP_CHOSEN);
        }
        else if (InElement(target, "prompt-back"))
        {
            Queue(MIOPAN_RML_MENU_TOP_BACK);
        }
    }
}

std::string Two(int value)
{
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "%02d", value);
    return buffer;
}

/* What MenuTopPlayDataDisp() prints on the sheet, where it prints it: the
 * sheet carries the labels and the "pts.", the game only the numbers. */
std::string PlayData()
{
    const Scale &scale = g_top.scale;
    const TIME_INFO played = GetPlayTime();
    std::string rml;
    rml += Number(scale,
                  std::to_string(played.hour) + ":" + Two(played.min) + ":" +
                      Two(played.sec),
                  396, 170, 120, "sheet-num");
    rml += Number(scale, std::to_string(GetPlayData_TotalScore()), 432, 194, 84,
                  "sheet-num");
    rml += Number(scale, std::to_string(GetPhotoNum()), 446, 218, 70, "sheet-num");
    rml += Number(scale, std::to_string(GetBusterGhostNum()), 460, 242, 56,
                  "sheet-num");
    rml += Number(scale, std::to_string(GetMaxScore()), 446, 266, 70, "sheet-num");

    /* The console's clock, day / month / year then the time, each pair
     * centred in the cell the game's fixed-width digits fill, so it meets the
     * sheet's own slashes as the game's does. */
    if (g_top.clock_frames-- <= 0)
    {
        GetSystemTime(&g_top.now);
        g_top.clock_frames = kClockFrames;
    }
    const DATE_INFO &now = g_top.now;
    const int parts[6] = {now.day.day,    now.day.month, now.day.year,
                          now.time.hour,  now.time.min,  now.time.sec};
    const int part_x[6] = {299, 338, 377, 421, 460, 499};
    for (int i = 0; i < 6; i++)
    {
        rml += Text(scale, Two(parts[i]), part_x[i], 310, 28, "sheet-num date");
    }
    rml += Text(scale, ":", 449, 310, 10, "sheet-num date");
    rml += Text(scale, ":", 488, 310, 10, "sheet-num date");
    return rml;
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    SetRml(g_top.prompts, g_top.shown_prompts,
           g_top.closing ? std::string() : MioPanRmlCommon::PromptsRml(kPrompts, 2));
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_top.scale.x || scale.y != g_top.scale.y)
    {
        g_top.scale = scale;
        g_top.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        BuildStage();
        Select(g_top.cursor);
    }
    SetRml(g_top.status, g_top.shown_status, MioPanRmlMenuStatus::Rml(g_top.scale));
    SetRml(g_top.play, g_top.shown_play, PlayData());
    RefreshPrompts();
}
}

namespace MioPanRmlMenuTop
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_top.document = context->LoadDocument(kDocumentPath);
    if (g_top.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_top.stage = g_top.document->GetElementById("stage");
    if (g_top.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_top.document->Close();
        g_top = MenuTopState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        g_top.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_top = MenuTopState();
}

void Frame()
{
    if (g_top.visible && !g_top.requested)
    {
        g_top.document->Hide();
        g_top.visible = false;
        g_top.command = MIOPAN_RML_MENU_TOP_NONE;
    }
    g_top.shown_last_frame = g_top.visible;
    g_top.requested = false;
    if (g_top.visible)
    {
        Refresh();
    }
}
}

extern "C" {

int MioPan_RmlMenuTopShow(int cursor, unsigned open, int closing,
                          const void *chapter_pk2, int chapter_sprite)
{
    if (g_top.document == nullptr)
    {
        return 0;
    }
    g_top.requested = true;
    const bool rebuild = open != g_top.open || chapter_pk2 != g_top.chapter_pk2 ||
                         chapter_sprite != g_top.chapter_sprite;
    g_top.open = open;
    g_top.chapter_pk2 = chapter_pk2;
    g_top.chapter_sprite = chapter_sprite;
    if (!g_top.visible)
    {
        /* A new visit: the chapter's pak may sit where another chapter's
         * did. */
        MioPanRmlSprite::Forget();
        g_top.scale = MioPanRmlMenuArt::StageScale();
        g_top.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_top.shown_stage.clear();
        BuildStage();
        /* Each opening starts where menu_top.c's cursor is: on the page the
         * hub last opened. */
        g_top.closing = false;
        g_top.stage->SetClass("closing", false);
        g_top.command = MIOPAN_RML_MENU_TOP_NONE;
        g_top.clock_frames = 0;
        g_top.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_top.visible = true;
        Select(IsOpen(cursor) ? cursor : 0);
        Refresh();
    }
    else
    {
        if (rebuild)
        {
            BuildStage();
        }
        Select(g_top.cursor);
    }
    if ((closing != 0) != g_top.closing)
    {
        g_top.closing = closing != 0;
        g_top.stage->SetClass("closing", g_top.closing);
        RefreshPrompts();
    }
    return 1;
}

int MioPan_RmlMenuTopIsShown(void)
{
    return g_top.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuTopConsumeCommand(void)
{
    const int command = g_top.command;
    g_top.command = MIOPAN_RML_MENU_TOP_NONE;
    return command;
}

int MioPan_RmlMenuTopCursor(void)
{
    return g_top.cursor;
}
}
