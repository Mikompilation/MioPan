#include "miopan_rml_savepoint.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rmlui.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/savepoint.rml";

/* Every save point string is message type 0x4b: the three rows, then the
 * question each of them asks. */
constexpr int kMsgType = 0x4b;
constexpr int kRows = 3;
constexpr int kRowLeave = 2;
constexpr int kMsgQuestion = 3;

/* In case the game's text is not there. */
constexpr const char *kFallbackRows[kRows] = {"Save", "Album", "Leave"};
constexpr const char *kFallbackQuestions[kRows] = {"Save the game?",
                                                   "Look at the album?", ""};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct SavePointState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *frame = nullptr;
    Rml::Element *items = nullptr;
    Rml::Element *prompts = nullptr;
    Rml::Element *rows[kRows] = {};
    std::string shown_prompts;
    std::string shown_rows;

    int cursor = 0;
    bool closing = false;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_SAVEPOINT_NONE;
    MioPanRmlCommon::ConfirmDialog confirm;
};

SavePointState g_point;
Listener g_listener;

std::string Text(int msg_id, const char *fallback)
{
    std::string text = MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, msg_id));
    return text.empty() ? std::string(fallback) : text;
}

void Select(int row)
{
    g_point.cursor = row;
    for (int i = 0; i < kRows; i++)
    {
        if (g_point.rows[i] != nullptr)
        {
            g_point.rows[i]->SetClass("selected", i == row);
        }
    }
}

/* The rows in the game's words, rebuilt when its language changes them. */
void BuildRows()
{
    std::string rml;
    for (int i = 0; i < kRows; i++)
    {
        rml += "<div id=\"point-" + std::to_string(i) +
               "\" class=\"entry selectable\">" +
               MioPanRmlCommon::Escape(Text(i, kFallbackRows[i])) + "</div>";
    }
    if (rml == g_point.shown_rows)
    {
        return;
    }
    g_point.shown_rows = rml;
    g_point.items->SetInnerRML(rml);
    for (int i = 0; i < kRows; i++)
    {
        g_point.rows[i] =
            g_point.document->GetElementById("point-" + std::to_string(i));
    }
    Select(g_point.cursor);
}

void Queue(int command)
{
    g_point.command = command;
}

void Move(int direction)
{
    Select((g_point.cursor + direction + kRows) % kRows);
    MioPanRmlCommon::PlayCursor();
}

/* Leaving needs no question, as in the game; the phase plays the decide cue
 * as it goes.  Save and album ask first, and the window plays it. */
void Choose(int row)
{
    if (row < 0 || row >= kRows)
    {
        return;
    }
    Select(row);
    if (row == kRowLeave)
    {
        Queue(MIOPAN_RML_SAVEPOINT_CHOSEN);
        return;
    }
    MioPanRmlCommon::PlayDecide();
    /* The game's questions end on an aside -- "(Photo data will not be
     * saved.)" -- which goes on the window's note line. */
    std::string question = Text(kMsgQuestion + row, kFallbackQuestions[row]);
    std::string note;
    const size_t open = question.rfind('(');
    if (open != std::string::npos && open > 0 && question.back() == ')')
    {
        note = question.substr(open + 1, question.size() - open - 2);
        question.erase(question.find_last_not_of(' ', open - 1) + 1);
    }
    g_point.confirm.Open(question, note, [] { Queue(MIOPAN_RML_SAVEPOINT_CHOSEN); });
}

/* Back goes to Leave, as the game's TRIANGLE does: leaving is always a
 * choice of its own. */
void Back()
{
    MioPanRmlCommon::PlayCancel();
    Select(kRowLeave);
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_point.confirm.HandleKey(key))
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
    case Rml::Input::KI_SPACE: Choose(g_point.cursor); break;
    case Rml::Input::KI_ESCAPE: Back(); break;
    default: return;
    }
    event.StopPropagation();
}

int RowAt(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < kRows; i++)
        {
            if (element == g_point.rows[i])
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
     * while the menu closes. */
    if (!g_point.visible || g_point.closing ||
        g_point.command != MIOPAN_RML_SAVEPOINT_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_point.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int row = RowAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && row >= 0 && row != g_point.cursor)
        {
            Select(row);
            MioPanRmlCommon::PlayCursor();
        }
    }
    else if (event == Rml::EventId::Click)
    {
        if (row >= 0)
        {
            Choose(row);
        }
        else if (InElement(target, "prompt-back"))
        {
            Back();
        }
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    const std::string prompts =
        g_point.closing ? std::string() : MioPanRmlCommon::PromptsRml(kPrompts, 2);
    if (prompts != g_point.shown_prompts && g_point.prompts != nullptr)
    {
        g_point.prompts->SetInnerRML(prompts);
        g_point.shown_prompts = prompts;
    }
}
}

namespace MioPanRmlSavePoint
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_point.document = context->LoadDocument(kDocumentPath);
    if (g_point.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    Rml::ElementDocument *document = g_point.document;
    g_point.frame = document->GetElementById("frame");
    g_point.items = document->GetElementById("items");
    g_point.prompts = document->GetElementById("prompts");
    if (g_point.frame == nullptr || g_point.items == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        g_point = SavePointState();
        return false;
    }
    g_point.confirm.Attach(document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_point.confirm.Detach();
    g_point = SavePointState();
}

void Frame()
{
    if (g_point.visible && !g_point.requested)
    {
        g_point.confirm.Close();
        g_point.document->Hide();
        g_point.visible = false;
        g_point.command = MIOPAN_RML_SAVEPOINT_NONE;
    }
    g_point.shown_last_frame = g_point.visible;
    g_point.requested = false;
    if (g_point.visible)
    {
        RefreshPrompts();
    }
}
}

extern "C" {

int MioPan_RmlSavePointShow(int cursor, int closing)
{
    if (g_point.document == nullptr)
    {
        return 0;
    }
    g_point.requested = true;
    if (!g_point.visible)
    {
        /* Each opening starts where savepoint_top.c's cursor is: on Save the
         * first time, on the row left by the last visit after that. */
        g_point.cursor = cursor >= 0 && cursor < kRows ? cursor : 0;
        g_point.closing = false;
        g_point.frame->SetClass("closing", false);
        g_point.command = MIOPAN_RML_SAVEPOINT_NONE;
        g_point.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_point.visible = true;
    }
    BuildRows();
    Select(g_point.cursor);

    if ((closing != 0) != g_point.closing)
    {
        g_point.closing = closing != 0;
        g_point.frame->SetClass("closing", g_point.closing);
        if (g_point.closing)
        {
            g_point.confirm.Close();
        }
        RefreshPrompts();
    }
    return 1;
}

int MioPan_RmlSavePointIsShown(void)
{
    return g_point.shown_last_frame ? 1 : 0;
}

int MioPan_RmlSavePointConsumeCommand(void)
{
    const int command = g_point.command;
    g_point.command = MIOPAN_RML_SAVEPOINT_NONE;
    return command;
}

int MioPan_RmlSavePointCursor(void)
{
    return g_point.cursor;
}
}
