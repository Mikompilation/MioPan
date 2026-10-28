#include "miopan_rml_load.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_saves.h"
#include "miopan_rmlui.h"

#include "../../system/mc/prg/mc_set_data.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/load.rml";

using MioPanRmlSaves::kSlots;

/* Memory-card messages, type 0x50: "load this?". */
constexpr int kMsgType = 0x50;
constexpr int kMsgLoadThis = 0x0e;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct LoadState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *message = nullptr;
    Rml::Element *prompts = nullptr;
    MioPanRmlSaves::SaveRow saves;
    std::string shown_prompts;
    std::string shown_message;

    int mode = -1;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_LOAD_NONE;
    MioPanRmlCommon::ConfirmDialog confirm;
};

LoadState g_load;
Listener g_listener;

void SetText(Rml::Element *element, const std::string &text)
{
    if (element != nullptr)
    {
        element->SetInnerRML(MioPanRmlCommon::Escape(text));
    }
}

void Queue(int command)
{
    g_load.command = command;
}

/* The next save with data that way, as the game's cursor steps; it stays put
 * when there is none. */
void Move(int direction)
{
    const int slot = g_load.saves.Step(direction);
    if (slot >= 0)
    {
        g_load.saves.Select(slot);
        MioPanRmlCommon::PlayCursor();
    }
}

void Choose(int slot)
{
    if (!g_load.saves.IsOpen(slot))
    {
        return;
    }
    g_load.saves.Select(slot);
    MioPanRmlCommon::PlayDecide();
    std::string question = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, kMsgLoadThis));
    if (question.empty())
    {
        question = "Load this save?";
    }
    /* The phase loads it; the window has played the decide cue. */
    g_load.confirm.Open(question, "", [] { Queue(MIOPAN_RML_LOAD_CHOSEN); });
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_load.mode == MIOPAN_RML_LOAD_NOTICE)
    {
        switch (key)
        {
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        case Rml::Input::KI_SPACE:
        case Rml::Input::KI_ESCAPE:
            Queue(MIOPAN_RML_LOAD_ACK);
            event.StopPropagation();
            break;
        default:
            break;
        }
        return;
    }
    if (g_load.mode != MIOPAN_RML_LOAD_SELECT)
    {
        return;
    }
    if (g_load.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }
    switch (key)
    {
    case Rml::Input::KI_LEFT:
    case Rml::Input::KI_UP: Move(-1); break;
    case Rml::Input::KI_RIGHT:
    case Rml::Input::KI_DOWN: Move(1); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Choose(g_load.saves.Cursor()); break;
    /* The phase plays the cancel cue as it goes back. */
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_LOAD_BACK); break;
    default: return;
    }
    event.StopPropagation();
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
    /* Nothing more until the phase has taken the last answer. */
    if (!g_load.visible || g_load.command != MIOPAN_RML_LOAD_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_load.mode == MIOPAN_RML_LOAD_NOTICE)
    {
        if (event == Rml::EventId::Click)
        {
            Queue(MIOPAN_RML_LOAD_ACK);
        }
        return;
    }
    if (g_load.mode != MIOPAN_RML_LOAD_SELECT || g_load.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int slot = g_load.saves.SlotAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && slot >= 0 &&
            slot != g_load.saves.Cursor() && g_load.saves.IsOpen(slot))
        {
            g_load.saves.Select(slot);
            MioPanRmlCommon::PlayCursor();
        }
    }
    else if (event == Rml::EventId::Click)
    {
        if (slot >= 0)
        {
            Choose(slot);
        }
        else if (InElement(target, "prompt-back"))
        {
            Queue(MIOPAN_RML_LOAD_BACK);
        }
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kSelect[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kNotice[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "OK", nullptr},
    };
    std::string prompts;
    if (g_load.mode == MIOPAN_RML_LOAD_SELECT)
    {
        prompts = MioPanRmlCommon::PromptsRml(kSelect, 2);
    }
    else if (g_load.mode == MIOPAN_RML_LOAD_NOTICE)
    {
        prompts = MioPanRmlCommon::PromptsRml(kNotice, 1);
    }
    if (prompts != g_load.shown_prompts && g_load.prompts != nullptr)
    {
        g_load.prompts->SetInnerRML(prompts);
        g_load.shown_prompts = prompts;
    }
}

/* The saves with data, a bit each: the only ones there is anything to load
 * from. */
unsigned SavesWithData(bool list)
{
    unsigned open = 0;
    for (int i = 0; list && i < kSlots; i++)
    {
        if (GetMemoryCardPlayDataFlg(i) == 1)
        {
            open |= 1u << i;
        }
    }
    return open;
}
}

namespace MioPanRmlLoad
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_load.document = context->LoadDocument(kDocumentPath);
    if (g_load.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    Rml::ElementDocument *document = g_load.document;
    g_load.message = document->GetElementById("message");
    g_load.prompts = document->GetElementById("prompts");
    if (!g_load.saves.Attach(document) || g_load.message == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        g_load = LoadState();
        return false;
    }

    g_load.confirm.Attach(document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_load.confirm.Detach();
    g_load = LoadState();
}

void Frame()
{
    if (g_load.visible && !g_load.requested)
    {
        g_load.confirm.Close();
        g_load.document->Hide();
        g_load.visible = false;
        g_load.mode = -1;
        g_load.shown_message.clear();
        g_load.command = MIOPAN_RML_LOAD_NONE;
        g_load.saves.Reset();
    }
    g_load.shown_last_frame = g_load.visible;
    g_load.requested = false;
    if (g_load.visible)
    {
        RefreshPrompts();
    }
}
}

extern "C" {

int MioPan_RmlLoadShow(int mode, int msg_id, int cursor, int list)
{
    if (g_load.document == nullptr)
    {
        return 0;
    }
    g_load.requested = true;
    if (!g_load.visible)
    {
        g_load.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_load.visible = true;
        g_load.command = MIOPAN_RML_LOAD_NONE;
    }

    MioPanRmlSaves::SaveRow &saves = g_load.saves;
    int select = saves.Cursor();
    if (mode != g_load.mode)
    {
        /* Choosing starts where loadgame.c put the cursor: on the first save
         * with data. */
        if (mode == MIOPAN_RML_LOAD_SELECT)
        {
            select = cursor >= 0 && cursor < kSlots ? cursor : 0;
        }
        if (mode != MIOPAN_RML_LOAD_SELECT)
        {
            g_load.confirm.Close();
        }
        g_load.mode = mode;
        g_load.message->SetClass("notice", mode == MIOPAN_RML_LOAD_NOTICE);
        RefreshPrompts();
    }
    /* Its button icons follow the pad or keyboard in use. */
    const std::string message = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, msg_id));
    if (message != g_load.shown_message)
    {
        g_load.shown_message = message;
        SetText(g_load.message, message);
    }

    saves.Refresh(list != 0, SavesWithData(list != 0));
    saves.Select(select);
    return 1;
}

int MioPan_RmlLoadIsShown(void)
{
    return g_load.shown_last_frame ? 1 : 0;
}

void MioPan_RmlLoadSetPicture(int slot, const void *pk2, int label)
{
    g_load.saves.SetPicture(slot, pk2, label);
}

int MioPan_RmlLoadConsumeCommand(void)
{
    const int command = g_load.command;
    g_load.command = MIOPAN_RML_LOAD_NONE;
    return command;
}

int MioPan_RmlLoadCursor(void)
{
    return g_load.saves.Cursor();
}
}
