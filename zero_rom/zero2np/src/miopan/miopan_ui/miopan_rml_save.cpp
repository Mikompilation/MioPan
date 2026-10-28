#include "miopan_rml_save.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_saves.h"
#include "miopan_rmlui.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/save.rml";

using MioPanRmlSaves::kSlots;

/* Memory-card messages, type 0x50: the two questions before saving, as
 * GameDataSaveMcSaveFileSelPad() asks them -- over a save, or into a free
 * slot. */
constexpr int kMsgType = 0x50;
constexpr int kMsgOverwrite = 0x0b;
constexpr int kMsgSaveHere = 0x1f;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct SaveState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *message = nullptr;
    Rml::Element *prompts = nullptr;
    MioPanRmlSaves::SaveRow saves;
    std::string shown_prompts;
    std::string shown_message;

    int mode = -1;
    /* The card's question the window was opened on, so a new one reopens it
     * and an answered one stays answered. */
    int question = -1;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_SAVE_NONE;
    MioPanRmlCommon::ConfirmDialog confirm;
};

SaveState g_save;
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
    g_save.command = command;
}

std::string MessageText(int msg_id)
{
    /* Its button icons follow the pad or keyboard in use. */
    return MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, msg_id));
}

/* The next slot that way the cursor can take, going round: the saves in use
 * and the first free one, as game_data_save.c's cursor steps. */
void Move(int direction)
{
    const int slot = g_save.saves.Step(direction);
    if (slot >= 0)
    {
        g_save.saves.Select(slot);
        MioPanRmlCommon::PlayCursor();
    }
}

void Choose(int slot)
{
    if (!g_save.saves.IsOpen(slot))
    {
        return;
    }
    g_save.saves.Select(slot);
    MioPanRmlCommon::PlayDecide();
    const bool over = g_save.saves.HasData(slot);
    std::string question = MessageText(over ? kMsgOverwrite : kMsgSaveHere);
    if (question.empty())
    {
        question = over ? "Save over this game?" : "Save here?";
    }
    /* The phase saves it; the window has played the decide cue. */
    g_save.confirm.Open(question, "", [] { Queue(MIOPAN_RML_SAVE_CHOSEN); });
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_save.mode == MIOPAN_RML_SAVE_NOTICE)
    {
        switch (key)
        {
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        case Rml::Input::KI_SPACE:
        case Rml::Input::KI_ESCAPE:
            Queue(MIOPAN_RML_SAVE_ACK);
            event.StopPropagation();
            break;
        default:
            break;
        }
        return;
    }
    if (g_save.mode == MIOPAN_RML_SAVE_QUESTION)
    {
        if (g_save.confirm.HandleKey(key))
        {
            event.StopPropagation();
        }
        return;
    }
    if (g_save.mode != MIOPAN_RML_SAVE_SELECT)
    {
        return;
    }
    if (g_save.confirm.HandleKey(key))
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
    case Rml::Input::KI_SPACE: Choose(g_save.saves.Cursor()); break;
    /* The phase plays the cancel cue as it leaves. */
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_SAVE_BACK); break;
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
    if (!g_save.visible || g_save.command != MIOPAN_RML_SAVE_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_save.mode == MIOPAN_RML_SAVE_NOTICE)
    {
        if (event == Rml::EventId::Click)
        {
            Queue(MIOPAN_RML_SAVE_ACK);
        }
        return;
    }
    /* The question's window takes its own clicks. */
    if (g_save.mode != MIOPAN_RML_SAVE_SELECT || g_save.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int slot = g_save.saves.SlotAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && slot >= 0 &&
            slot != g_save.saves.Cursor() && g_save.saves.IsOpen(slot))
        {
            g_save.saves.Select(slot);
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
            Queue(MIOPAN_RML_SAVE_BACK);
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
    if (g_save.mode == MIOPAN_RML_SAVE_SELECT)
    {
        prompts = MioPanRmlCommon::PromptsRml(kSelect, 2);
    }
    else if (g_save.mode == MIOPAN_RML_SAVE_NOTICE)
    {
        prompts = MioPanRmlCommon::PromptsRml(kNotice, 1);
    }
    if (prompts != g_save.shown_prompts && g_save.prompts != nullptr)
    {
        g_save.prompts->SetInnerRML(prompts);
        g_save.shown_prompts = prompts;
    }
}

/* The card's question in the yes / no window, once per question.  The
 * message line stays empty under it rather than saying it twice. */
void RefreshQuestion(int msg_id)
{
    if (g_save.mode != MIOPAN_RML_SAVE_QUESTION)
    {
        g_save.question = -1;
        return;
    }
    if (msg_id == g_save.question || g_save.command != MIOPAN_RML_SAVE_NONE)
    {
        return;
    }
    g_save.question = msg_id;
    g_save.confirm.Open(MessageText(msg_id), "",
                        [] { Queue(MIOPAN_RML_SAVE_YES); },
                        [] { Queue(MIOPAN_RML_SAVE_NO); });
}
}

namespace MioPanRmlSave
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_save.document = context->LoadDocument(kDocumentPath);
    if (g_save.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    Rml::ElementDocument *document = g_save.document;
    g_save.message = document->GetElementById("message");
    g_save.prompts = document->GetElementById("prompts");
    if (!g_save.saves.Attach(document) || g_save.message == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        g_save = SaveState();
        return false;
    }

    g_save.confirm.Attach(document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_save.confirm.Detach();
    g_save = SaveState();
}

void Frame()
{
    if (g_save.visible && !g_save.requested)
    {
        g_save.confirm.Close();
        g_save.document->Hide();
        g_save.visible = false;
        g_save.mode = -1;
        g_save.question = -1;
        g_save.shown_message.clear();
        g_save.command = MIOPAN_RML_SAVE_NONE;
        g_save.saves.Reset();
    }
    g_save.shown_last_frame = g_save.visible;
    g_save.requested = false;
    if (g_save.visible)
    {
        RefreshPrompts();
    }
}
}

extern "C" {

int MioPan_RmlSaveShow(int mode, int msg_id, int cursor, int list,
                       unsigned open)
{
    if (g_save.document == nullptr)
    {
        return 0;
    }
    g_save.requested = true;
    if (!g_save.visible)
    {
        g_save.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_save.visible = true;
        g_save.command = MIOPAN_RML_SAVE_NONE;
    }

    MioPanRmlSaves::SaveRow &saves = g_save.saves;
    saves.Refresh(list != 0, open);
    int select = saves.Cursor();
    if (mode != g_save.mode)
    {
        /* Choosing starts where game_data_save.c put the cursor: on the
         * first save in use. */
        if (mode == MIOPAN_RML_SAVE_SELECT)
        {
            select = cursor >= 0 && cursor < kSlots ? cursor : 0;
        }
        g_save.confirm.Close();
        g_save.question = -1;
        g_save.mode = mode;
        g_save.message->SetClass("notice", mode == MIOPAN_RML_SAVE_NOTICE);
        RefreshPrompts();
    }
    RefreshQuestion(msg_id);

    const std::string message =
        mode == MIOPAN_RML_SAVE_QUESTION ? std::string() : MessageText(msg_id);
    if (message != g_save.shown_message)
    {
        g_save.shown_message = message;
        SetText(g_save.message, message);
    }

    saves.Select(select);
    return 1;
}

int MioPan_RmlSaveIsShown(void)
{
    return g_save.shown_last_frame ? 1 : 0;
}

void MioPan_RmlSaveSetPicture(int slot, const void *pk2, int label)
{
    g_save.saves.SetPicture(slot, pk2, label);
}

int MioPan_RmlSaveConsumeCommand(void)
{
    const int command = g_save.command;
    g_save.command = MIOPAN_RML_SAVE_NONE;
    return command;
}

int MioPan_RmlSaveCursor(void)
{
    return g_save.saves.Cursor();
}
}
