#include "miopan_rml_menu_cam_edit.h"
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
constexpr const char *kDocumentPath = "resources/rml/menu_cam_edit.rml";

using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

/* The camera menu's lines are message 0x31's. */
constexpr int kMsgCamera = 0x31;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuCamEditState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    std::string shown_stage;

    Scale scale;
    MIOPAN_RML_MENU_CAM_EDIT page = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_CAM_EDIT_NONE;
    int command_arg = 0;
};

MenuCamEditState g_edit;
Listener g_listener;

std::string Place(float x, float y, float w, float h)
{
    char style[128];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  x * g_edit.scale.x, y * g_edit.scale.y, w * g_edit.scale.x,
                  h * g_edit.scale.y);
    return style;
}

std::string Prompts()
{
    const MIOPAN_RML_MENU_CAM_EDIT &page = g_edit.page;
    if (page.closing)
    {
        return std::string();
    }
    if (page.kind == MIOPAN_RML_MENU_CAM_EDIT_NOTICE)
    {
        static const MioPanRmlCommon::Prompt kNotice[] = {
            {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "OK", nullptr},
        };
        return MioPanRmlCommon::PromptsRml(kNotice, 1);
    }
    static const MioPanRmlCommon::Prompt kPick[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    return MioPanRmlCommon::PromptsRml(kPick, 2);
}

/* The title over the game's plate, the window along the bottom with its
 * line, a question's Yes and No, the editor's places for the pointer, and
 * the hints -- the rest of the page is the game's drawing under it. */
std::string Stage()
{
    const Scale &scale = g_edit.scale;
    const MIOPAN_RML_MENU_CAM_EDIT &page = g_edit.page;
    std::string rml = Text(scale, "Function Upgrades", 30, 20, 240, "menu-title long");
    rml += MioPanRmlMenuArt::Window(scale, 24, 352, 592, 100);
    if (page.line >= 0)
    {
        rml += "<p class=\"caption-text\" style=\"" + Place(48, 370, 544, 30) + "\">" +
               MioPanRmlCommon::Escape(MioPanRmlCommon::NameButtonIcons(
                   MioPanGameText::Message(kMsgCamera, page.line))) +
               "</p>";
    }
    if (page.kind == MIOPAN_RML_MENU_CAM_EDIT_CONFIRM)
    {
        rml += "<span id=\"answer-0\" class=\"window-choice" +
               std::string(page.answer == 0 ? " selected" : "") + "\" style=\"" +
               Place(155, 399, 120, 30) + "\">Yes</span>";
        rml += "<span id=\"answer-1\" class=\"window-choice" +
               std::string(page.answer != 0 ? " selected" : "") + "\" style=\"" +
               Place(361, 399, 120, 30) + "\">No</span>";
    }
    if (page.kind == MIOPAN_RML_MENU_CAM_EDIT_PICK)
    {
        for (int i = 0; i < page.hits && i < MIOPAN_RML_MENU_CAM_EDIT_HITS; i++)
        {
            rml += "<div id=\"place-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
                   Place(page.hit[i][0], page.hit[i][1], page.hit[i][2], page.hit[i][3]) +
                   "\"></div>";
        }
    }
    rml += "<div class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) + "\">" +
           Prompts() + "</div>";
    return rml;
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_edit.scale.x || scale.y != g_edit.scale.y)
    {
        g_edit.scale = scale;
        g_edit.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_edit.shown_stage.clear();
    }
    const std::string rml = Stage();
    if (rml != g_edit.shown_stage)
    {
        g_edit.stage->SetInnerRML(rml);
        g_edit.shown_stage = rml;
    }
}

void Queue(int command, int arg = 0)
{
    g_edit.command = command;
    g_edit.command_arg = arg;
}

void HandleKey(Rml::Event &event)
{
    switch (event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN))
    {
    case Rml::Input::KI_UP: Queue(MIOPAN_RML_MENU_CAM_EDIT_UP); break;
    case Rml::Input::KI_DOWN: Queue(MIOPAN_RML_MENU_CAM_EDIT_DOWN); break;
    case Rml::Input::KI_LEFT: Queue(MIOPAN_RML_MENU_CAM_EDIT_LEFT); break;
    case Rml::Input::KI_RIGHT: Queue(MIOPAN_RML_MENU_CAM_EDIT_RIGHT); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Queue(MIOPAN_RML_MENU_CAM_EDIT_CHOOSE); break;
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_MENU_CAM_EDIT_BACK); break;
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
    const MIOPAN_RML_MENU_CAM_EDIT &page = g_edit.page;
    if (click && !Owner(target, "prompt-back").empty())
    {
        Queue(MIOPAN_RML_MENU_CAM_EDIT_BACK);
        return;
    }
    if (event == Rml::EventId::Mousescroll)
    {
        const float delta = event.GetParameter<float>("wheel_delta_y", 0.0f);
        if (delta != 0.0f && page.kind == MIOPAN_RML_MENU_CAM_EDIT_PICK)
        {
            if (page.across != 0)
            {
                Queue(delta < 0.0f ? MIOPAN_RML_MENU_CAM_EDIT_LEFT
                                   : MIOPAN_RML_MENU_CAM_EDIT_RIGHT);
            }
            else
            {
                Queue(delta < 0.0f ? MIOPAN_RML_MENU_CAM_EDIT_UP
                                   : MIOPAN_RML_MENU_CAM_EDIT_DOWN);
            }
        }
        return;
    }
    switch (page.kind)
    {
    case MIOPAN_RML_MENU_CAM_EDIT_PICK:
    {
        const std::string place = Owner(target, "place-");
        if (place.empty())
        {
            return;
        }
        const int index = std::atoi(place.c_str() + 6);
        if (click)
        {
            Queue(index == page.hit_at ? MIOPAN_RML_MENU_CAM_EDIT_CHOOSE
                                       : MIOPAN_RML_MENU_CAM_EDIT_ROW,
                  index);
        }
        else if (MioPanRml::PointerActive() && index != page.hit_at)
        {
            Queue(MIOPAN_RML_MENU_CAM_EDIT_ROW, index);
        }
        break;
    }
    case MIOPAN_RML_MENU_CAM_EDIT_CONFIRM:
    {
        const std::string answer = Owner(target, "answer-");
        if (answer.empty())
        {
            return;
        }
        const int index = answer == "answer-0" ? 0 : 1;
        if (click)
        {
            Queue(MIOPAN_RML_MENU_CAM_EDIT_ANSWER, index);
        }
        else if (MioPanRml::PointerActive() && index != page.answer)
        {
            Queue(MIOPAN_RML_MENU_CAM_EDIT_LEFT);
        }
        break;
    }
    default:
        if (click)
        {
            Queue(MIOPAN_RML_MENU_CAM_EDIT_CHOOSE);
        }
        break;
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_edit.visible || g_edit.page.closing || g_edit.page.busy ||
        g_edit.command != MIOPAN_RML_MENU_CAM_EDIT_NONE)
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

namespace MioPanRmlMenuCamEdit
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_edit.document = context->LoadDocument(kDocumentPath);
    if (g_edit.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_edit.stage = g_edit.document->GetElementById("stage");
    if (g_edit.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_edit.document->Close();
        g_edit = MenuCamEditState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_edit.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_edit = MenuCamEditState();
}

void Frame()
{
    if (g_edit.visible && !g_edit.requested)
    {
        g_edit.document->Hide();
        g_edit.visible = false;
        g_edit.command = MIOPAN_RML_MENU_CAM_EDIT_NONE;
    }
    g_edit.shown_last_frame = g_edit.visible;
    g_edit.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuCamEditReady(void)
{
    return g_edit.document != nullptr ? 1 : 0;
}

int MioPan_RmlMenuCamEditShow(const MIOPAN_RML_MENU_CAM_EDIT *page)
{
    if (g_edit.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_edit.requested = true;
    g_edit.page = *page;
    if (!g_edit.visible)
    {
        g_edit.scale = MioPanRmlMenuArt::StageScale();
        g_edit.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_edit.shown_stage.clear();
        g_edit.command = MIOPAN_RML_MENU_CAM_EDIT_NONE;
        g_edit.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_edit.visible = true;
    }
    g_edit.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuCamEditIsShown(void)
{
    return g_edit.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuCamEditConsumeCommand(int *arg)
{
    const int command = g_edit.command;
    if (arg != nullptr)
    {
        *arg = g_edit.command_arg;
    }
    g_edit.command = MIOPAN_RML_MENU_CAM_EDIT_NONE;
    g_edit.command_arg = 0;
    return command;
}
}
