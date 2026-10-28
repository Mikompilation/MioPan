#include "miopan_rml_title.h"
#include "miopan_rml_common.h"
#include "miopan_rmlui.h"
#include "../miopan_config.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>

#include <string>
#include <vector>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/title.rml";

/* title_menu.c's entry numbers, and the port's own Exit Game. */
constexpr int kEntryExit = -1;

struct Entry
{
    int csr;
    const char *label;
    bool developer;
};

/* In the game's own order; the developer pair sits last, above Exit Game. */
constexpr Entry kEntries[] = {
    {0, "New Game", false},       {1, "Load Game", false},
    {2, "Album", false},          {3, "Gallery", false},
    {4, "Options", false},        {5, "Frame Rate", false},
    {7, "Chapter Select", true},  {6, "Debug Menu", true},
    {kEntryExit, "Exit Game", false},
};

enum Mode
{
    MODE_NONE,
    MODE_PRESS,
    MODE_MENU,
};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct TitleState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *press = nullptr;
    Rml::Element *press_text = nullptr;
    Rml::Element *menu = nullptr;
    Rml::Element *items = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_prompts;
    std::string shown_press;

    /* The entries on show, and the cursor among them. */
    std::vector<const Entry *> entries;
    std::vector<Rml::Element *> rows;
    int cursor = 0;

    Mode mode = MODE_NONE;
    bool visible = false;
    /* Asked for since the last Frame(). */
    bool requested = false;
    int command = MIOPAN_RML_TITLE_NONE;
    MioPanRmlCommon::ConfirmDialog confirm;
};

TitleState g_title;
Listener g_listener;

void Select(int index)
{
    g_title.cursor = index;
    for (size_t i = 0; i < g_title.rows.size(); i++)
    {
        g_title.rows[i]->SetClass("selected", (int)i == index);
    }
}

/* The list, rebuilt each time the menu opens: [developer] debug_menu can
 * change in between, from the overlay. */
void BuildEntries(int csr)
{
    const bool developer = MioPan_ConfigDebugMenuEnabled() != 0;
    g_title.entries.clear();
    std::string rml;
    for (const Entry &entry : kEntries)
    {
        if (entry.developer && !developer)
        {
            continue;
        }
        rml += "<div id=\"entry-" + std::to_string(g_title.entries.size()) +
               "\" class=\"entry selectable";
        rml += entry.developer ? " developer\">" : "\">";
        rml += MioPanRmlCommon::Escape(entry.label);
        rml += "</div>";
        g_title.entries.push_back(&entry);
    }
    g_title.items->SetInnerRML(rml);

    g_title.rows.clear();
    int start = 0;
    for (size_t i = 0; i < g_title.entries.size(); i++)
    {
        g_title.rows.push_back(
            g_title.document->GetElementById("entry-" + std::to_string(i)));
        if (g_title.entries[i]->csr == csr)
        {
            start = (int)i;
        }
    }
    Select(start);
}

void Queue(int command)
{
    g_title.command = command;
}

void Quit()
{
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

void Choose(int index)
{
    if (index < 0 || index >= (int)g_title.entries.size())
    {
        return;
    }
    Select(index);
    if (g_title.entries[index]->csr == kEntryExit)
    {
        MioPanRmlCommon::PlayDecide();
        g_title.confirm.Open("Exit the game?", "", Quit);
        return;
    }
    /* The phase plays the decide cue as it moves on. */
    Queue(MIOPAN_RML_TITLE_CHOOSE);
}

/* The cursor goes round, as the game's own row of entries did. */
void Move(int direction)
{
    const int count = (int)g_title.entries.size();
    if (count == 0)
    {
        return;
    }
    Select((g_title.cursor + direction + count) % count);
    MioPanRmlCommon::PlayCursor();
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_title.mode == MODE_PRESS)
    {
        /* START or CROSS on the pad, as the game asks; both arrive here as
         * Escape and Enter. */
        switch (key)
        {
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        case Rml::Input::KI_SPACE:
        case Rml::Input::KI_ESCAPE:
            Queue(MIOPAN_RML_TITLE_START);
            event.StopPropagation();
            break;
        default:
            break;
        }
        return;
    }

    if (g_title.confirm.HandleKey(key))
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
    case Rml::Input::KI_SPACE: Choose(g_title.cursor); break;
    /* The phase plays the cancel cue as it goes back. */
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_TITLE_BACK); break;
    default: return;
    }
    event.StopPropagation();
}

int RowAt(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (size_t i = 0; i < g_title.rows.size(); i++)
        {
            if (element == g_title.rows[i])
            {
                return (int)i;
            }
        }
    }
    return -1;
}

bool InPrompt(Rml::Element *element, const char *id)
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
    if (!g_title.visible || g_title.command != MIOPAN_RML_TITLE_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_title.mode == MODE_PRESS)
    {
        /* A click anywhere is the press PRESS START asks for. */
        if (event == Rml::EventId::Click)
        {
            Queue(MIOPAN_RML_TITLE_START);
        }
        return;
    }
    if (g_title.confirm.IsOpen())
    {
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    const int row = RowAt(target);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && row >= 0 && row != g_title.cursor)
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
        else if (InPrompt(target, "prompt-back"))
        {
            Queue(MIOPAN_RML_TITLE_BACK);
        }
    }
}

void RefreshPress()
{
    /* What to press, on whichever the player last used. */
    std::string text = "Press ";
    text += MioPanRml::UsingPad()
                ? MioPanRml::PadButtonName(SDL_GAMEPAD_BUTTON_START)
                : "Enter";
    if (text != g_title.shown_press && g_title.press_text != nullptr)
    {
        g_title.press_text->SetInnerRML(MioPanRmlCommon::Escape(text));
        g_title.shown_press = text;
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    const std::string prompts = g_title.mode == MODE_MENU
                                    ? MioPanRmlCommon::PromptsRml(kPrompts, 2)
                                    : std::string();
    if (prompts != g_title.shown_prompts && g_title.prompts != nullptr)
    {
        g_title.prompts->SetInnerRML(prompts);
        g_title.shown_prompts = prompts;
    }
}

/* Up for this frame, in `mode`. */
int Show(Mode mode, int csr)
{
    if (g_title.document == nullptr)
    {
        return 0;
    }
    g_title.requested = true;
    if (g_title.visible && g_title.mode == mode)
    {
        return 1;
    }

    g_title.mode = mode;
    g_title.command = MIOPAN_RML_TITLE_NONE;
    g_title.confirm.Close();
    g_title.press->SetClass("hidden", mode != MODE_PRESS);
    g_title.menu->SetClass("hidden", mode != MODE_MENU);
    if (mode == MODE_MENU)
    {
        BuildEntries(csr);
    }
    if (!g_title.visible)
    {
        g_title.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_title.visible = true;
    }
    RefreshPress();
    RefreshPrompts();
    return 1;
}
}

namespace MioPanRmlTitle
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_title.document = context->LoadDocument(kDocumentPath);
    if (g_title.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_title.press = g_title.document->GetElementById("press");
    g_title.press_text = g_title.document->GetElementById("press-text");
    g_title.menu = g_title.document->GetElementById("menu");
    g_title.items = g_title.document->GetElementById("items");
    g_title.prompts = g_title.document->GetElementById("prompts");
    if (g_title.press == nullptr || g_title.menu == nullptr ||
        g_title.items == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_title.document->Close();
        g_title = TitleState();
        return false;
    }
    g_title.confirm.Attach(g_title.document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        g_title.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_title.confirm.Detach();
    g_title = TitleState();
}

void Frame()
{
    if (g_title.visible && !g_title.requested)
    {
        g_title.confirm.Close();
        g_title.document->Hide();
        g_title.visible = false;
        g_title.mode = MODE_NONE;
        g_title.command = MIOPAN_RML_TITLE_NONE;
    }
    g_title.requested = false;
    if (g_title.visible)
    {
        RefreshPress();
        RefreshPrompts();
    }
}
}

extern "C" {

int MioPan_RmlTitleShowPress(void)
{
    return Show(MODE_PRESS, 0);
}

int MioPan_RmlTitleShowMenu(int csr)
{
    return Show(MODE_MENU, csr);
}

int MioPan_RmlTitleConsumeCommand(void)
{
    const int command = g_title.command;
    g_title.command = MIOPAN_RML_TITLE_NONE;
    return command;
}

int MioPan_RmlTitleMenuCursor(void)
{
    if (g_title.cursor < 0 || g_title.cursor >= (int)g_title.entries.size() ||
        g_title.entries[g_title.cursor]->csr == kEntryExit)
    {
        return 0;
    }
    return g_title.entries[g_title.cursor]->csr;
}
}
