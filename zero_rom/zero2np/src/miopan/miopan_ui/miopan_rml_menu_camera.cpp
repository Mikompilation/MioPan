#include "miopan_rml_menu_camera.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rmlui.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_camera.rml";

using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

/* The camera menu's lines are message 0x31's. */
constexpr int kMsgCamera = 0x31;

/* menu_cam_top.c's layout: the window along the bottom and its line; the
 * boxes MenuCamTopCsrBaseDisp() lights -- the camera and the three rows --
 * the film plate's halves, and the two selectors' slots, 34 and 36 apart. */
constexpr float kTopBox[4][4] = {
    {33.0f, 79.0f, 119.0f, 229.0f},
    {459.0f, 79.0f, 150.0f, 58.0f},
    {476.0f, 173.0f, 133.0f, 62.0f},
    {459.0f, 271.0f, 150.0f, 58.0f},
};
constexpr float kFilmHalves[2][4] = {
    {462.0f, 196.0f, 77.0f, 45.0f},
    {539.0f, 196.0f, 77.0f, 45.0f},
};
constexpr float kAddX = 463.0f;
constexpr float kAddY = 100.0f;
constexpr float kAddStep = 34.0f;
constexpr float kEquipX = 447.0f;
constexpr float kEquipY = 296.0f;
constexpr float kEquipStep = 36.0f;
constexpr float kSlotH = 42.0f;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuCameraState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    std::string shown_stage;

    Scale scale;
    MIOPAN_RML_MENU_CAMERA page = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_CAMERA_NONE;
    int command_arg = 0;
};

MenuCameraState g_cam;
Listener g_listener;

std::string Place(float x, float y, float w, float h)
{
    char style[128];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  x * g_cam.scale.x, y * g_cam.scale.y, w * g_cam.scale.x,
                  h * g_cam.scale.y);
    return style;
}

std::string Hit(const char *prefix, int index, float x, float y, float w, float h)
{
    return std::string("<div id=\"") + prefix + std::to_string(index) +
           "\" class=\"row-hit\" style=\"" + Place(x, y, w, h) + "\"></div>";
}

/* Where the pointer can take the cursor in the mode up now. */
std::string Hits()
{
    const MIOPAN_RML_MENU_CAMERA &page = g_cam.page;
    std::string rml;
    switch (page.mode)
    {
    case MIOPAN_RML_MENU_CAMERA_TOP:
        for (int i = 0; i < 4; i++)
        {
            rml += Hit("item-", i, kTopBox[i][0], kTopBox[i][1], kTopBox[i][2], kTopBox[i][3]);
        }
        break;
    case MIOPAN_RML_MENU_CAMERA_FILM:
        for (int i = 0; i < 2; i++)
        {
            rml += Hit("half-", i, kFilmHalves[i][0], kFilmHalves[i][1], kFilmHalves[i][2],
                       kFilmHalves[i][3]);
        }
        break;
    case MIOPAN_RML_MENU_CAMERA_ADD_FUNC:
    case MIOPAN_RML_MENU_CAMERA_EQUIP_FUNC:
    {
        const bool add = page.mode == MIOPAN_RML_MENU_CAMERA_ADD_FUNC;
        for (int i = 0; i < 4; i++)
        {
            if ((page.owned & (1 << i)) != 0)
            {
                rml += Hit("item-", i, (add ? kAddX : kEquipX) + i * (add ? kAddStep : kEquipStep),
                           add ? kAddY : kEquipY, add ? kAddStep : kEquipStep, kSlotH);
            }
        }
        break;
    }
    default: break;
    }
    return rml;
}

std::string Prompts()
{
    const MIOPAN_RML_MENU_CAMERA &page = g_cam.page;
    if (page.closing)
    {
        return std::string();
    }
    switch (page.mode)
    {
    case MIOPAN_RML_MENU_CAMERA_FILM:
    {
        static const MioPanRmlCommon::Prompt kFilm[] = {
            {"Left / Right", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Change Film", nullptr,
             SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
            {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        };
        return MioPanRmlCommon::PromptsRml(kFilm, 2);
    }
    case MIOPAN_RML_MENU_CAMERA_ADD_FUNC:
    {
        static const MioPanRmlCommon::Prompt kAdd[] = {
            {"Left / Right", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Select", nullptr,
             SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
            {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        };
        return MioPanRmlCommon::PromptsRml(kAdd, 2);
    }
    case MIOPAN_RML_MENU_CAMERA_EQUIP_FUNC:
    {
        const MioPanRmlCommon::Prompt equip[] = {
            {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, page.fitted ? "Remove" : "Equip", nullptr},
            {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        };
        return MioPanRmlCommon::PromptsRml(equip, 2);
    }
    default:
    {
        static const MioPanRmlCommon::Prompt kTop[] = {
            {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
            {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        };
        return MioPanRmlCommon::PromptsRml(kTop, 2);
    }
    }
}

/* The title over the game's plate, the window along the bottom with the
 * line for what is under the cursor, the pointer's places and the hints --
 * and nothing else: the rest of the page is the game's drawing under it. */
std::string Stage()
{
    const Scale &scale = g_cam.scale;
    std::string rml = Text(scale, "Camera", 41, 20, 160, "menu-title");
    rml += MioPanRmlMenuArt::Window(scale, 24, 352, 592, 100);
    rml += "<p class=\"caption-text\" style=\"" + Place(48, 370, 544, 70) + "\">" +
           MioPanRmlCommon::Escape(MioPanRmlCommon::NameButtonIcons(
               MioPanGameText::Message(kMsgCamera, g_cam.page.line))) +
           "</p>";
    rml += Hits();
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) +
           "\">" + Prompts() + "</div>";
    return rml;
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_cam.scale.x || scale.y != g_cam.scale.y)
    {
        g_cam.scale = scale;
        g_cam.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_cam.shown_stage.clear();
    }
    const std::string rml = Stage();
    if (rml != g_cam.shown_stage)
    {
        g_cam.stage->SetInnerRML(rml);
        g_cam.shown_stage = rml;
    }
}

void Queue(int command, int arg = 0)
{
    g_cam.command = command;
    g_cam.command_arg = arg;
}

void HandleKey(Rml::Event &event)
{
    switch (event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN))
    {
    case Rml::Input::KI_UP: Queue(MIOPAN_RML_MENU_CAMERA_UP); break;
    case Rml::Input::KI_DOWN: Queue(MIOPAN_RML_MENU_CAMERA_DOWN); break;
    case Rml::Input::KI_LEFT: Queue(MIOPAN_RML_MENU_CAMERA_LEFT); break;
    case Rml::Input::KI_RIGHT: Queue(MIOPAN_RML_MENU_CAMERA_RIGHT); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Queue(MIOPAN_RML_MENU_CAMERA_CHOOSE); break;
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_MENU_CAMERA_BACK); break;
    default: return;
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
    const int mode = g_cam.page.mode;
    if (click && !Owner(target, "prompt-back").empty())
    {
        Queue(MIOPAN_RML_MENU_CAMERA_BACK);
        return;
    }
    if (event == Rml::EventId::Mousescroll)
    {
        const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
        if (delta == 0.0f)
        {
            return;
        }
        if (mode == MIOPAN_RML_MENU_CAMERA_TOP)
        {
            Queue(delta < 0.0f ? MIOPAN_RML_MENU_CAMERA_UP : MIOPAN_RML_MENU_CAMERA_DOWN);
        }
        else
        {
            Queue(delta < 0.0f ? MIOPAN_RML_MENU_CAMERA_LEFT : MIOPAN_RML_MENU_CAMERA_RIGHT);
        }
        return;
    }
    /* The film plate's halves step the film back and on. */
    const std::string half = Owner(target, "half-");
    if (!half.empty())
    {
        if (click)
        {
            Queue(half == "half-0" ? MIOPAN_RML_MENU_CAMERA_LEFT : MIOPAN_RML_MENU_CAMERA_RIGHT);
        }
        return;
    }
    const std::string item = Owner(target, "item-");
    if (item.empty())
    {
        return;
    }
    const int index = std::atoi(item.c_str() + 5);
    if (click)
    {
        /* As the game's mouse: the cursor there, and on the top page and the
         * parts' selector a click is CROSS. */
        if (index != g_cam.page.cursor)
        {
            Queue(MIOPAN_RML_MENU_CAMERA_SET, index);
        }
        else if (mode != MIOPAN_RML_MENU_CAMERA_ADD_FUNC)
        {
            Queue(MIOPAN_RML_MENU_CAMERA_CHOOSE);
        }
    }
    else if (MioPanRml::PointerActive() && index != g_cam.page.cursor)
    {
        Queue(MIOPAN_RML_MENU_CAMERA_SET, index);
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_cam.visible || g_cam.page.closing ||
        g_cam.command != MIOPAN_RML_MENU_CAMERA_NONE)
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

namespace MioPanRmlMenuCamera
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_cam.document = context->LoadDocument(kDocumentPath);
    if (g_cam.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_cam.stage = g_cam.document->GetElementById("stage");
    if (g_cam.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_cam.document->Close();
        g_cam = MenuCameraState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_cam.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_cam = MenuCameraState();
}

void Frame()
{
    if (g_cam.visible && !g_cam.requested)
    {
        g_cam.document->Hide();
        g_cam.visible = false;
        g_cam.command = MIOPAN_RML_MENU_CAMERA_NONE;
    }
    g_cam.shown_last_frame = g_cam.visible;
    g_cam.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuCameraShow(const MIOPAN_RML_MENU_CAMERA *page)
{
    if (g_cam.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_cam.requested = true;
    g_cam.page = *page;
    if (!g_cam.visible)
    {
        g_cam.scale = MioPanRmlMenuArt::StageScale();
        g_cam.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_cam.shown_stage.clear();
        g_cam.command = MIOPAN_RML_MENU_CAMERA_NONE;
        g_cam.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_cam.visible = true;
    }
    g_cam.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuCameraIsShown(void)
{
    return g_cam.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuCameraConsumeCommand(int *arg)
{
    const int command = g_cam.command;
    if (arg != nullptr)
    {
        *arg = g_cam.command_arg;
    }
    g_cam.command = MIOPAN_RML_MENU_CAMERA_NONE;
    g_cam.command_arg = 0;
    return command;
}
}
