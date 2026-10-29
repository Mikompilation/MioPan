#include "miopan_rml_menu_map.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_map_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_map.rml";

using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

/* Message 0x3d names the sheets, 0x4a the rooms. */
constexpr int kMsgArea = 0x3d;
constexpr int kMsgRoom = 0x4a;

/* menu_map_tex[]: the title's plates, the frame round the viewport with its
 * compass points, the snapshot's place, the crosshair's four arrows -- the
 * second laid a quarter back, the third a quarter on. */
constexpr int kTitlePlates[2] = {2, 3};
constexpr int kFrameFirst = 4;
constexpr int kFrameCount = 20;
constexpr int kSnap = 24;
constexpr int kArrows[4] = {46, 47, 48, 49};

/* menu_map.c's layout: the viewport the sheet is clipped to
 * (MENU_MAP_SCISSOR), the snapshot's black plate, the two names. */
constexpr int kViewX = 32;
constexpr int kViewY = 99;
constexpr int kViewW = 576;
constexpr int kViewH = 320;
constexpr int kAreaX = 57;
constexpr int kRoomX = 237;
constexpr int kNameY = 68;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuMapState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *live = nullptr;
    Rml::Element *names = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_stage;
    std::string shown_live;
    std::string shown_names;
    std::string shown_prompts;

    Scale scale;
    MIOPAN_RML_MENU_MAP page = {};

    int held = 0;
    bool dragging = false;
    float drag_last_x = 0.0f;
    float drag_last_y = 0.0f;
    float drag_dx = 0.0f;
    float drag_dy = 0.0f;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_MAP_NONE;
};

MenuMapState g_map;
Listener g_listener;

uintptr_t Pak()
{
    return (uintptr_t)g_map.page.pak;
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
                  x * g_map.scale.x, y * g_map.scale.y, w * g_map.scale.x,
                  h * g_map.scale.y);
    return style;
}

std::string Game(int type, int id)
{
    return MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(type, id));
}

/* The title and the frame round the viewport -- nothing over the viewport
 * itself, where the game draws the sheet -- with the layer the page fills
 * in and the hints. */
std::string Stage()
{
    const Scale &scale = g_map.scale;
    std::string rml;
    for (const int plate : kTitlePlates)
    {
        rml += Art(scale, MENU_BG_TEX_ADRS, menu_map_tex[plate]);
    }
    rml += Text(scale, "Map", 41, 20, 120, "menu-title");
    rml += "<div id=\"viewport\" class=\"map-viewport\" style=\"" +
           Place(kViewX, kViewY, kViewW, kViewH) + "\"></div>";
    rml += "<div id=\"live\" class=\"layer\"></div>";
    for (int i = 0; i < kFrameCount; i++)
    {
        rml += Art(scale, Pak(), menu_map_tex[kFrameFirst + i]);
    }
    rml += "<div id=\"names\" class=\"layer\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(170, 30, 460, 30) +
           "\"></div>";
    return rml;
}

/* The crosshair's arrows on their fade, the snapshot on its black plate,
 * and the names. */
std::string Live()
{
    const Scale &scale = g_map.scale;
    const MIOPAN_RML_MENU_MAP &page = g_map.page;
    std::string rml;
    if (page.arrows_alpha > 0)
    {
        char fade[40];
        std::snprintf(fade, sizeof(fade), "opacity: %.3f;",
                      (float)(page.arrows_alpha > 0x80 ? 0x80 : page.arrows_alpha) / 128.0f);
        rml += "<div class=\"layer\" style=\"" + std::string(fade) + "\">";
        for (int i = 0; i < 4; i++)
        {
            const SPRT_DAT &arrow = menu_map_tex[kArrows[i]];
            MioPanRmlSprite::Look look;
            if (i == 1 || i == 2)
            {
                /* rot 270 about (x, y + w), rot 90 about (x + h, y): either
                 * way it lies h wide and w tall from its own corner. */
                look.turns = i == 1 ? 3 : 1;
                rml += ArtIn(scale, Pak(), arrow, arrow.x, arrow.y, arrow.h, arrow.w, look);
            }
            else
            {
                rml += Art(scale, Pak(), arrow);
            }
        }
        rml += "</div>";
    }
    if (page.area >= 0)
    {
        const SPRT_DAT &snap = menu_map_tex[kSnap];
        rml += "<div class=\"map-snap-bed\" style=\"" + Place(snap.x, snap.y, snap.w, snap.h) +
               "\"></div>";
        if (page.snap != nullptr)
        {
            rml += Art(scale, (uintptr_t)page.snap, snap);
        }
    }
    return rml;
}

std::string Names()
{
    const MIOPAN_RML_MENU_MAP &page = g_map.page;
    std::string rml;
    if (page.area >= 0)
    {
        rml += Text(g_map.scale, Game(kMsgArea, page.area), kAreaX, kNameY, 178, "map-name");
    }
    if (page.room >= 0)
    {
        rml += Text(g_map.scale, Game(kMsgRoom, page.room), kRoomX, kNameY, 250, "map-name");
    }
    return rml;
}

void RefreshPrompts()
{
    std::string prompts;
    if (!g_map.page.closing)
    {
        if (g_map.page.has_map != 0)
        {
            const MioPanRmlCommon::Prompt prompt[] = {
                {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, g_map.page.zoomed ? "Reduce" : "Enlarge",
                 "prompt-zoom"},
                {"PgUp / PgDn", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Floors",
                 "prompt-floor", SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
                {"Home / End", SDL_GAMEPAD_BUTTON_INVALID, "Map", "prompt-area",
                 SDL_GAMEPAD_BUTTON_INVALID, MioPanRml::PadTriggersName()},
                {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
            };
            prompts = MioPanRmlCommon::PromptsRml(prompt, 4);
        }
        else
        {
            const MioPanRmlCommon::Prompt prompt[] = {
                {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
            };
            prompts = MioPanRmlCommon::PromptsRml(prompt, 1);
        }
    }
    SetRml(g_map.prompts, g_map.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_map.shown_stage)
    {
        return;
    }
    g_map.stage->SetInnerRML(rml);
    g_map.shown_stage = rml;
    g_map.live = g_map.document->GetElementById("live");
    g_map.names = g_map.document->GetElementById("names");
    g_map.prompts = g_map.document->GetElementById("prompts");
    g_map.shown_live.clear();
    g_map.shown_names.clear();
    g_map.shown_prompts.clear();
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_map.scale.x || scale.y != g_map.scale.y)
    {
        g_map.scale = scale;
        g_map.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_map.shown_stage.clear();
    }
    BuildStage();
    SetRml(g_map.live, g_map.shown_live, Live());
    SetRml(g_map.names, g_map.shown_names, Names());
    RefreshPrompts();
}

void Queue(int command)
{
    g_map.command = command;
}

int HeldBit(int key)
{
    switch (key)
    {
    case Rml::Input::KI_UP: return MIOPAN_RML_MENU_MAP_HELD_UP;
    case Rml::Input::KI_DOWN: return MIOPAN_RML_MENU_MAP_HELD_DOWN;
    case Rml::Input::KI_LEFT: return MIOPAN_RML_MENU_MAP_HELD_LEFT;
    case Rml::Input::KI_RIGHT: return MIOPAN_RML_MENU_MAP_HELD_RIGHT;
    default: return 0;
    }
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const int bit = HeldBit(key);
    if (event == Rml::EventId::Keyup)
    {
        g_map.held &= ~bit;
        return;
    }
    if (bit != 0)
    {
        g_map.held |= bit;
        event.StopPropagation();
        return;
    }
    if (g_map.command != MIOPAN_RML_MENU_MAP_NONE)
    {
        return;
    }
    switch (key)
    {
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_MENU_MAP_BACK); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Queue(MIOPAN_RML_MENU_MAP_ZOOM); break;
    case Rml::Input::KI_PRIOR: Queue(MIOPAN_RML_MENU_MAP_FLOOR_PREV); break;
    case Rml::Input::KI_NEXT: Queue(MIOPAN_RML_MENU_MAP_FLOOR_NEXT); break;
    case Rml::Input::KI_HOME: Queue(MIOPAN_RML_MENU_MAP_AREA_PREV); break;
    case Rml::Input::KI_END: Queue(MIOPAN_RML_MENU_MAP_AREA_NEXT); break;
    default: return;
    }
    event.StopPropagation();
}

/* Whether `element` is the one with `id` or inside it. */
bool In(Rml::Element *element, const char *id)
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

/* Context pixels to the game's. */
float GameX(float pixels)
{
    const float dp = g_map.document->GetContext()->GetDensityIndependentPixelRatio();
    return pixels / (dp > 0.0f ? dp : 1.0f) / g_map.scale.x;
}

float GameY(float pixels)
{
    const float dp = g_map.document->GetContext()->GetDensityIndependentPixelRatio();
    return pixels / (dp > 0.0f ? dp : 1.0f) / g_map.scale.y;
}

void HandlePointer(Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    const float x = event.GetParameter<float>("mouse_x", 0.0f);
    const float y = event.GetParameter<float>("mouse_y", 0.0f);
    if (event == Rml::EventId::Mouseup)
    {
        g_map.dragging = false;
        return;
    }
    if (event == Rml::EventId::Mousemove)
    {
        if (g_map.dragging)
        {
            g_map.drag_dx += GameX(x - g_map.drag_last_x);
            g_map.drag_dy += GameY(y - g_map.drag_last_y);
            g_map.drag_last_x = x;
            g_map.drag_last_y = y;
        }
        return;
    }
    if (event == Rml::EventId::Mousedown)
    {
        if (event.GetParameter<int>("button", 0) == 0 && In(target, "viewport"))
        {
            g_map.dragging = true;
            g_map.drag_last_x = x;
            g_map.drag_last_y = y;
        }
        return;
    }
    if (g_map.command != MIOPAN_RML_MENU_MAP_NONE)
    {
        return;
    }
    /* The wheel zooms, as the game's mouse has it: in at 1x, out at 2x. */
    if (event == Rml::EventId::Mousescroll)
    {
        const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
        if ((delta < 0.0f && g_map.page.zoomed == 0) || (delta > 0.0f && g_map.page.zoomed != 0))
        {
            Queue(MIOPAN_RML_MENU_MAP_ZOOM);
        }
        return;
    }
    if (event != Rml::EventId::Click)
    {
        return;
    }
    if (In(target, "prompt-back"))
    {
        Queue(MIOPAN_RML_MENU_MAP_BACK);
    }
    else if (In(target, "prompt-zoom"))
    {
        Queue(MIOPAN_RML_MENU_MAP_ZOOM);
    }
    else if (In(target, "prompt-floor"))
    {
        Queue(MIOPAN_RML_MENU_MAP_FLOOR_NEXT);
    }
    else if (In(target, "prompt-area"))
    {
        Queue(MIOPAN_RML_MENU_MAP_AREA_NEXT);
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_map.visible)
    {
        return;
    }
    /* A key let go always counts, so a way held as the page closes does not
     * stay held. */
    if (event == Rml::EventId::Keyup)
    {
        HandleKey(event);
        return;
    }
    if (g_map.page.closing)
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

namespace MioPanRmlMenuMap
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_map.document = context->LoadDocument(kDocumentPath);
    if (g_map.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_map.stage = g_map.document->GetElementById("stage");
    if (g_map.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_map.document->Close();
        g_map = MenuMapState();
        return false;
    }
    for (Rml::EventId id :
         {Rml::EventId::Keydown, Rml::EventId::Keyup, Rml::EventId::Click,
          Rml::EventId::Mousedown, Rml::EventId::Mouseup, Rml::EventId::Mousemove,
          Rml::EventId::Mousescroll})
    {
        g_map.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_map = MenuMapState();
}

void Frame()
{
    if (g_map.visible && !g_map.requested)
    {
        g_map.document->Hide();
        g_map.visible = false;
        g_map.command = MIOPAN_RML_MENU_MAP_NONE;
        g_map.held = 0;
        g_map.dragging = false;
        g_map.drag_dx = 0.0f;
        g_map.drag_dy = 0.0f;
    }
    g_map.shown_last_frame = g_map.visible;
    g_map.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuMapReady(void)
{
    return g_map.document != nullptr ? 1 : 0;
}

int MioPan_RmlMenuMapShow(const MIOPAN_RML_MENU_MAP *page)
{
    if (g_map.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_map.requested = true;
    const bool new_pak = page->pak != g_map.page.pak;
    g_map.page = *page;
    if (!g_map.visible || new_pak)
    {
        /* A new visit: the page's pak may sit where another's did. */
        MioPanRmlSprite::Forget();
        g_map.scale = MioPanRmlMenuArt::StageScale();
        g_map.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_map.shown_stage.clear();
    }
    if (!g_map.visible)
    {
        g_map.command = MIOPAN_RML_MENU_MAP_NONE;
        g_map.held = 0;
        g_map.dragging = false;
        g_map.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_map.visible = true;
    }
    g_map.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuMapIsShown(void)
{
    return g_map.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuMapHeld(void)
{
    return g_map.visible && !g_map.page.closing ? g_map.held : 0;
}

int MioPan_RmlMenuMapConsumeDrag(float *dx, float *dy)
{
    const bool moved = g_map.drag_dx != 0.0f || g_map.drag_dy != 0.0f;
    if (dx != nullptr)
    {
        *dx = g_map.drag_dx;
    }
    if (dy != nullptr)
    {
        *dy = g_map.drag_dy;
    }
    g_map.drag_dx = 0.0f;
    g_map.drag_dy = 0.0f;
    return moved ? 1 : 0;
}

int MioPan_RmlMenuMapConsumeCommand(void)
{
    const int command = g_map.command;
    g_map.command = MIOPAN_RML_MENU_MAP_NONE;
    return command;
}
}
