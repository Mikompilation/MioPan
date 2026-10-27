#include "miopan_rml_pause.h"
#include "miopan_rml_common.h"
#include "miopan_rml_options.h"
#include "miopan_rmlui.h"
#include "../miopan_config.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/pause.rml";

enum Item
{
    ITEM_CONTINUE = 0,
    ITEM_OPTIONS,
    ITEM_TITLE,
    ITEM_COUNT,
};

constexpr const char *kItemIds[ITEM_COUNT] = {"item-continue", "item-options",
                                              "item-title"};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct PauseState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *items[ITEM_COUNT] = {};
    Rml::Element *prompts = nullptr;
    std::string shown_prompts;
    bool open = false;
    bool hidden = false;
    /* Options is up over the pause screen, which waits underneath. */
    bool in_options = false;
    int cursor = ITEM_CONTINUE;
    int result = MIOPAN_RML_PAUSE_NONE;
    MioPanRmlCommon::ConfirmDialog confirm;
};

PauseState g_pause;
Listener g_listener;

void Select(int item)
{
    g_pause.cursor = item;
    for (int i = 0; i < ITEM_COUNT; i++)
    {
        if (g_pause.items[i] != nullptr)
        {
            g_pause.items[i]->SetClass("selected", i == item);
        }
    }
}

void ShowDocument()
{
    g_pause.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
}

void Finish(int result)
{
    g_pause.result = result;
}

void Choose(int item)
{
    switch (item)
    {
    case ITEM_CONTINUE:
        MioPanRmlCommon::PlayDecide();
        Finish(MIOPAN_RML_PAUSE_RESUME);
        break;
    case ITEM_OPTIONS:
        if (MioPan_RmlOptionsOpen(MIOPAN_RML_OPTIONS_FROM_PAUSE) != 0)
        {
            g_pause.in_options = true;
            g_pause.document->Hide();
        }
        break;
    case ITEM_TITLE:
        MioPanRmlCommon::PlayDecide();
        g_pause.confirm.Open("Return to the title screen?",
                             "Anything since your last save will be lost.",
                             [] { Finish(MIOPAN_RML_PAUSE_TITLE); });
        break;
    default:
        break;
    }
}

/* The cursor goes round, as it does on the game's own pause panel. */
void Move(int direction)
{
    Select((g_pause.cursor + direction + ITEM_COUNT) % ITEM_COUNT);
    MioPanRmlCommon::PlayCursor();
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_pause.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }
    switch (key)
    {
    case Rml::Input::KI_UP: Move(-1); break;
    case Rml::Input::KI_DOWN: Move(1); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Choose(g_pause.cursor); break;
    /* Escape is START on the keyboard, and START is what closes the pause
     * screen; Triangle and Circle arrive as it too. */
    case Rml::Input::KI_ESCAPE:
        MioPanRmlCommon::PlayCancel();
        Finish(MIOPAN_RML_PAUSE_RESUME);
        break;
    /* SELECT: the ROM's debug-menu shortcut, behind the same switch the
     * game's own pause panel honours. */
    case Rml::Input::KI_TAB:
        if (MioPan_ConfigDebugMenuEnabled() != 0)
        {
            Finish(MIOPAN_RML_PAUSE_DEBUG);
        }
        break;
    default: return;
    }
    event.StopPropagation();
}

int ItemAt(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < ITEM_COUNT; i++)
        {
            if (element == g_pause.items[i])
            {
                return i;
            }
        }
    }
    return -1;
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_pause.open || g_pause.hidden || g_pause.in_options ||
        g_pause.result != MIOPAN_RML_PAUSE_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_pause.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int item = ItemAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && item >= 0 && item != g_pause.cursor)
        {
            Select(item);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        if (item >= 0)
        {
            Choose(item);
            return;
        }
        for (Rml::Element *e = target; e != nullptr; e = e->GetParentNode())
        {
            if (e->GetId() == "prompt-resume")
            {
                MioPanRmlCommon::PlayCancel();
                Finish(MIOPAN_RML_PAUSE_RESUME);
                return;
            }
        }
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_START, "Resume", "prompt-resume"},
    };
    const std::string prompts = MioPanRmlCommon::PromptsRml(kPrompts, 2);
    if (prompts != g_pause.shown_prompts && g_pause.prompts != nullptr)
    {
        g_pause.prompts->SetInnerRML(prompts);
        g_pause.shown_prompts = prompts;
    }
}
}

namespace MioPanRmlPause
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_pause.document = context->LoadDocument(kDocumentPath);
    if (g_pause.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    for (int i = 0; i < ITEM_COUNT; i++)
    {
        g_pause.items[i] = g_pause.document->GetElementById(kItemIds[i]);
    }
    g_pause.prompts = g_pause.document->GetElementById("prompts");
    g_pause.confirm.Attach(g_pause.document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        g_pause.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_pause.confirm.Detach();
    g_pause = PauseState();
}

void Frame()
{
    if (!g_pause.open)
    {
        return;
    }
    /* Back from Options: the pause screen comes up again where it was. */
    if (g_pause.in_options && MioPan_RmlOptionsIsOpen() == 0)
    {
        (void)MioPan_RmlOptionsConsumeRequest();
        g_pause.in_options = false;
        if (!g_pause.hidden)
        {
            ShowDocument();
        }
    }
    if (!g_pause.hidden && !g_pause.in_options)
    {
        RefreshPrompts();
    }
}
}

extern "C" {

int MioPan_RmlPauseOpen(void)
{
    if (g_pause.document == nullptr)
    {
        return 0;
    }
    g_pause.open = true;
    g_pause.hidden = false;
    g_pause.in_options = false;
    g_pause.result = MIOPAN_RML_PAUSE_NONE;
    g_pause.shown_prompts.clear();
    g_pause.confirm.Close();
    Select(ITEM_CONTINUE);
    ShowDocument();
    RefreshPrompts();
    return 1;
}

void MioPan_RmlPauseClose(void)
{
    if (g_pause.in_options)
    {
        MioPan_RmlOptionsClose();
        (void)MioPan_RmlOptionsConsumeRequest();
    }
    if (g_pause.document != nullptr)
    {
        g_pause.confirm.Close();
        g_pause.document->Hide();
    }
    g_pause.open = false;
    g_pause.hidden = false;
    g_pause.in_options = false;
}

int MioPan_RmlPauseIsOpen(void)
{
    return g_pause.open ? 1 : 0;
}

int MioPan_RmlPauseConsumeResult(void)
{
    const int result = g_pause.result;
    g_pause.result = MIOPAN_RML_PAUSE_NONE;
    return result;
}

void MioPan_RmlPauseSetHidden(int hidden)
{
    if (!g_pause.open || g_pause.document == nullptr ||
        (hidden != 0) == g_pause.hidden)
    {
        return;
    }
    g_pause.hidden = hidden != 0;
    if (g_pause.in_options)
    {
        MioPan_RmlOptionsSetHidden(hidden);
    }
    else if (g_pause.hidden)
    {
        g_pause.document->Hide();
    }
    else
    {
        ShowDocument();
    }
}
}
