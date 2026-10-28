#include "miopan_rml_setup.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rmlui.h"
#include "../io/miopan_xbox_assets.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/setup.rml";

/* The setup menu's captions are message type 0x4c: one per row on the left,
 * one per setting (both accessories share theirs), Mission Select's own, and
 * the question before going back to the title. */
constexpr int kMsgType = 0x4c;
constexpr int kMenuCaption[3] = {0, 1, 2};
constexpr int kSettingCaption[5] = {3, 4, 4, 5, 6};
constexpr int kMsgMissionStart = 7;
constexpr int kMsgExitQuestion = 8;

constexpr int kMenuRows = 3;
constexpr int kMenuMission = 1;
constexpr int kMenuExit = 2;
constexpr int kSettingRows = 5;

/* The ROM's costumes before the Xbox ones: the pair the game ships with, then
 * TYPE A..H, as the pak's plates name them. */
constexpr int kRomCostumes = 9;

/* What the game's plates say: the rows on the left, and the four
 * difficulties. */
constexpr const char *kMenuLabels[kMenuRows] = {"Story", "Mission",
                                                "Return to Title"};
constexpr const char *kDifficulties[4] = {"Easy", "Normal", "Hard",
                                          "Nightmare"};

/* The settings' elements, in setup_csr order. */
constexpr const char *kSettingIds[kSettingRows] = {
    "row-costume", "row-mio", "row-mayu", "row-difficulty", "row-start"};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct SetupState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *frame = nullptr;
    Rml::Element *menu[kMenuRows] = {};
    Rml::Element *settings_box = nullptr;
    Rml::Element *settings[kSettingRows] = {};
    Rml::Element *values[kSettingRows] = {};
    Rml::Element *help = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_prompts;
    std::string shown_help;
    std::string shown_values[kSettingRows];

    MIOPAN_RML_SETUP setup = {};
    int mode = -1;
    int menu_cursor = 0;
    int setting_cursor = 0;

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_SETUP_NONE;
    int command_arg = 0;
    MioPanRmlCommon::ConfirmDialog confirm;
};

SetupState g_setup;
Listener g_listener;

void SetText(Rml::Element *element, const std::string &text)
{
    if (element != nullptr)
    {
        element->SetInnerRML(MioPanRmlCommon::Escape(text));
    }
}

std::string Caption(int msg_id)
{
    return MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(kMsgType, msg_id));
}

std::string CostumeName(int costume)
{
    if (costume <= 0)
    {
        return "Normal";
    }
    if (costume < kRomCostumes)
    {
        return std::string("Type ") + (char)('A' + costume - 1);
    }
    /* The Xbox costumes go on from TYPE I, as the Director's Cut names
     * them. */
    const int extra = costume - kRomCostumes;
    std::string name = std::string("Type ") + (char)('I' + extra);
    const char *dc = MioPan_XboxCostumeName(extra);
    if (dc != nullptr && dc[0] != '\0')
    {
        name += " \xC2\xB7 ";
        name += dc;
    }
    return name;
}

std::string AccessoryName(int accessory)
{
    if (accessory <= 0)
    {
        return "Off";
    }
    if (accessory == 1)
    {
        return "Glasses";
    }
    const char *name = MioPan_XboxAccessoryName(accessory - 2);
    return name != nullptr ? name : "?";
}

void Queue(int command, int arg = 0)
{
    g_setup.command = command;
    g_setup.command_arg = arg;
}

bool RowShown(int row)
{
    return row != MIOPAN_RML_SETUP_ROW_DIFFICULTY || g_setup.setup.has_difficulty;
}

/* Whether the cursor can stop on a row: a save made before the clear keeps
 * the difficulty it was played at, shown but fixed. */
bool RowOpen(int row)
{
    return RowShown(row) && (row != MIOPAN_RML_SETUP_ROW_DIFFICULTY ||
                             g_setup.setup.difficulty_open);
}

void RefreshCursors()
{
    const bool settings = g_setup.mode == MIOPAN_RML_SETUP_SETTINGS;
    for (int i = 0; i < kMenuRows; i++)
    {
        if (g_setup.menu[i] != nullptr)
        {
            g_setup.menu[i]->SetClass("selected", i == g_setup.menu_cursor);
        }
    }
    for (int i = 0; i < kSettingRows; i++)
    {
        if (g_setup.settings[i] != nullptr)
        {
            g_setup.settings[i]->SetClass("selected",
                                          settings && i == g_setup.setting_cursor);
        }
    }
}

/* What each row is for, in the game's words, under the rule. */
void RefreshHelp()
{
    int msg_id;
    if (g_setup.mode == MIOPAN_RML_SETUP_SETTINGS)
    {
        msg_id = kSettingCaption[g_setup.setting_cursor];
        if (g_setup.setting_cursor == MIOPAN_RML_SETUP_ROW_START &&
            g_setup.setup.menu_csr == kMenuMission)
        {
            msg_id = kMsgMissionStart;
        }
    }
    else
    {
        msg_id = kMenuCaption[g_setup.menu_cursor];
    }
    const std::string help = Caption(msg_id);
    if (help != g_setup.shown_help)
    {
        g_setup.shown_help = help;
        SetText(g_setup.help, help);
    }
}

void RefreshValues()
{
    const MIOPAN_RML_SETUP &setup = g_setup.setup;
    const std::string values[kSettingRows] = {
        CostumeName(setup.costume),
        AccessoryName(setup.mio_acs),
        AccessoryName(setup.mayu_acs),
        kDifficulties[setup.difficulty & 3],
        setup.menu_csr == kMenuMission ? "Mission Select" : "Game Start",
    };
    for (int i = 0; i < kSettingRows; i++)
    {
        if (values[i] != g_setup.shown_values[i])
        {
            g_setup.shown_values[i] = values[i];
            SetText(g_setup.values[i], values[i]);
        }
    }
    if (Rml::Element *difficulty = g_setup.settings[MIOPAN_RML_SETUP_ROW_DIFFICULTY])
    {
        difficulty->SetClass("hidden", !setup.has_difficulty);
        difficulty->SetClass("disabled", !setup.difficulty_open);
    }
}

void MoveMenu(int direction)
{
    g_setup.menu_cursor = (g_setup.menu_cursor + direction + kMenuRows) % kMenuRows;
    MioPanRmlCommon::PlayCursor();
    RefreshCursors();
    RefreshHelp();
}

/* Up and down go round the settings, over the difficulty where it cannot be
 * changed, as setup_menu.c's cursor does. */
void MoveSetting(int direction)
{
    int row = g_setup.setting_cursor;
    do
    {
        row = (row + direction + kSettingRows) % kSettingRows;
    } while (!RowOpen(row));
    g_setup.setting_cursor = row;
    MioPanRmlCommon::PlayCursor();
    RefreshCursors();
    RefreshHelp();
}

void SelectSetting(int row)
{
    if (row == g_setup.setting_cursor || !RowOpen(row))
    {
        return;
    }
    g_setup.setting_cursor = row;
    MioPanRmlCommon::PlayCursor();
    RefreshCursors();
    RefreshHelp();
}

/* Story and Mission open their settings, and the phase plays the decide cue
 * as they do; going back to the title asks first. */
void ChooseMenu(int row)
{
    g_setup.menu_cursor = row;
    RefreshCursors();
    RefreshHelp();
    if (row != kMenuExit)
    {
        Queue(MIOPAN_RML_SETUP_ENTER);
        return;
    }
    MioPanRmlCommon::PlayDecide();
    std::string question = Caption(kMsgExitQuestion);
    if (question.empty())
    {
        question = "Return to the title?";
    }
    g_setup.confirm.Open(question, "", [] { Queue(MIOPAN_RML_SETUP_EXIT); });
}

/* Back on the left goes to Return to Title, as the game's TRIANGLE does:
 * leaving is always a choice of its own. */
void MenuBack()
{
    MioPanRmlCommon::PlayCancel();
    g_setup.menu_cursor = kMenuExit;
    RefreshCursors();
    RefreshHelp();
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    if (g_setup.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }
    if (g_setup.mode == MIOPAN_RML_SETUP_MENU)
    {
        switch (key)
        {
        case Rml::Input::KI_UP: MoveMenu(-1); break;
        case Rml::Input::KI_DOWN: MoveMenu(1); break;
        case Rml::Input::KI_RETURN:
        case Rml::Input::KI_NUMPADENTER:
        case Rml::Input::KI_SPACE: ChooseMenu(g_setup.menu_cursor); break;
        case Rml::Input::KI_ESCAPE: MenuBack(); break;
        default: return;
        }
        event.StopPropagation();
        return;
    }
    if (g_setup.mode != MIOPAN_RML_SETUP_SETTINGS)
    {
        return;
    }
    const bool start = g_setup.setting_cursor == MIOPAN_RML_SETUP_ROW_START;
    switch (key)
    {
    case Rml::Input::KI_UP: MoveSetting(-1); break;
    case Rml::Input::KI_DOWN: MoveSetting(1); break;
    /* The phase steps the setting, over the locked ones, with its cue. */
    case Rml::Input::KI_LEFT:
        if (!start)
        {
            Queue(MIOPAN_RML_SETUP_STEP, -1);
        }
        break;
    case Rml::Input::KI_RIGHT:
        if (!start)
        {
            Queue(MIOPAN_RML_SETUP_STEP, 1);
        }
        break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE:
        if (start)
        {
            Queue(MIOPAN_RML_SETUP_START);
        }
        break;
    case Rml::Input::KI_ESCAPE: Queue(MIOPAN_RML_SETUP_BACK); break;
    default: return;
    }
    event.StopPropagation();
}

template <int N>
int IndexIn(Rml::Element *element, Rml::Element *const (&rows)[N])
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < N; i++)
        {
            if (element == rows[i])
            {
                return i;
            }
        }
    }
    return -1;
}

Rml::Element *WithClass(Rml::Element *element, const char *name)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element->IsClassSet(name))
        {
            return element;
        }
    }
    return nullptr;
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

void HandlePointer(Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    if (event == Rml::EventId::Click && InElement(target, "prompt-back"))
    {
        if (g_setup.mode == MIOPAN_RML_SETUP_MENU)
        {
            MenuBack();
        }
        else
        {
            Queue(MIOPAN_RML_SETUP_BACK);
        }
        return;
    }

    const int menu_row = IndexIn(target, g_setup.menu);
    const int setting_row = IndexIn(target, g_setup.settings);
    if (g_setup.mode == MIOPAN_RML_SETUP_MENU)
    {
        if (menu_row < 0)
        {
            return;
        }
        if (event == Rml::EventId::Mouseover)
        {
            if (MioPanRml::PointerActive() && menu_row != g_setup.menu_cursor)
            {
                MoveMenu(menu_row - g_setup.menu_cursor);
            }
        }
        else if (event == Rml::EventId::Click)
        {
            ChooseMenu(menu_row);
        }
        return;
    }

    /* In the settings a click on the left is the way back to it. */
    if (event == Rml::EventId::Click && menu_row >= 0)
    {
        Queue(MIOPAN_RML_SETUP_BACK);
        return;
    }
    if (setting_row < 0)
    {
        return;
    }
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive())
        {
            SelectSetting(setting_row);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        SelectSetting(setting_row);
        if (setting_row == MIOPAN_RML_SETUP_ROW_START)
        {
            Queue(MIOPAN_RML_SETUP_START);
        }
        else if (Rml::Element *arrow = WithClass(target, "arrow"))
        {
            Queue(MIOPAN_RML_SETUP_STEP, arrow->IsClassSet("left") ? -1 : 1);
        }
        else
        {
            /* The row itself steps on, as the game's RIGHT. */
            Queue(MIOPAN_RML_SETUP_STEP, 1);
        }
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, and nothing
     * while the menu closes. */
    if (!g_setup.visible || g_setup.setup.closing ||
        g_setup.command != MIOPAN_RML_SETUP_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    if (g_setup.confirm.IsOpen())
    {
        return;
    }
    HandlePointer(event);
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kMenu[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kSettings[] = {
        {"Left / Right", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Change", nullptr,
         SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Start", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts;
    if (!g_setup.setup.closing)
    {
        prompts = g_setup.mode == MIOPAN_RML_SETUP_SETTINGS
                      ? MioPanRmlCommon::PromptsRml(kSettings, 3)
                      : MioPanRmlCommon::PromptsRml(kMenu, 2);
    }
    if (prompts != g_setup.shown_prompts && g_setup.prompts != nullptr)
    {
        g_setup.prompts->SetInnerRML(prompts);
        g_setup.shown_prompts = prompts;
    }
}
}

namespace MioPanRmlSetup
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_setup.document = context->LoadDocument(kDocumentPath);
    if (g_setup.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    Rml::ElementDocument *document = g_setup.document;
    g_setup.frame = document->GetElementById("frame");
    g_setup.settings_box = document->GetElementById("settings");
    g_setup.help = document->GetElementById("help");
    g_setup.prompts = document->GetElementById("prompts");
    bool complete = g_setup.frame != nullptr && g_setup.settings_box != nullptr;

    Rml::Element *items = document->GetElementById("menu-items");
    if (items != nullptr)
    {
        std::string rml;
        for (int i = 0; i < kMenuRows; i++)
        {
            rml += "<div id=\"menu-" + std::to_string(i) +
                   "\" class=\"entry selectable\">" +
                   MioPanRmlCommon::Escape(kMenuLabels[i]) + "</div>";
        }
        items->SetInnerRML(rml);
    }
    for (int i = 0; i < kMenuRows; i++)
    {
        g_setup.menu[i] = document->GetElementById("menu-" + std::to_string(i));
        complete = complete && g_setup.menu[i] != nullptr;
    }
    for (int i = 0; i < kSettingRows; i++)
    {
        const std::string id = kSettingIds[i];
        g_setup.settings[i] = document->GetElementById(id);
        g_setup.values[i] = document->GetElementById(id + "-value");
        complete = complete && g_setup.settings[i] != nullptr &&
                   g_setup.values[i] != nullptr;
    }
    if (!complete)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        g_setup = SetupState();
        return false;
    }

    g_setup.confirm.Attach(document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_setup.confirm.Detach();
    g_setup = SetupState();
}

void Frame()
{
    if (g_setup.visible && !g_setup.requested)
    {
        g_setup.confirm.Close();
        g_setup.document->Hide();
        g_setup.visible = false;
        g_setup.mode = -1;
        g_setup.command = MIOPAN_RML_SETUP_NONE;
        g_setup.frame->SetClass("closing", false);
    }
    g_setup.shown_last_frame = g_setup.visible;
    g_setup.requested = false;
    if (g_setup.visible)
    {
        RefreshPrompts();
        RefreshHelp();
    }
}
}

extern "C" {

int MioPan_RmlSetupShow(const MIOPAN_RML_SETUP *setup)
{
    if (g_setup.document == nullptr || setup == nullptr)
    {
        return 0;
    }
    g_setup.requested = true;
    g_setup.setup = *setup;
    if (!g_setup.visible)
    {
        g_setup.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_setup.visible = true;
        g_setup.command = MIOPAN_RML_SETUP_NONE;
    }

    if (setup->mode != g_setup.mode)
    {
        /* Each side starts where setup_menu.c's cursor is. */
        g_setup.mode = setup->mode;
        g_setup.menu_cursor =
            setup->menu_csr >= 0 && setup->menu_csr < kMenuRows ? setup->menu_csr : 0;
        g_setup.setting_cursor =
            setup->setup_csr >= 0 && setup->setup_csr < kSettingRows &&
                    RowOpen(setup->setup_csr)
                ? setup->setup_csr
                : 0;
        g_setup.confirm.Close();
        const bool settings = g_setup.mode == MIOPAN_RML_SETUP_SETTINGS;
        g_setup.frame->SetClass("settings", settings);
        g_setup.frame->SetClass("menu", !settings);
        RefreshPrompts();
    }
    if (g_setup.frame->IsClassSet("closing") != (setup->closing != 0))
    {
        g_setup.frame->SetClass("closing", setup->closing != 0);
        if (setup->closing != 0)
        {
            g_setup.confirm.Close();
        }
        RefreshPrompts();
    }

    RefreshValues();
    RefreshCursors();
    RefreshHelp();
    return 1;
}

int MioPan_RmlSetupIsShown(void)
{
    return g_setup.shown_last_frame ? 1 : 0;
}

int MioPan_RmlSetupConsumeCommand(int *arg)
{
    const int command = g_setup.command;
    if (arg != nullptr)
    {
        *arg = g_setup.command_arg;
    }
    g_setup.command = MIOPAN_RML_SETUP_NONE;
    g_setup.command_arg = 0;
    return command;
}

int MioPan_RmlSetupMenuCursor(void)
{
    return g_setup.menu_cursor;
}

int MioPan_RmlSetupSettingCursor(void)
{
    return g_setup.setting_cursor;
}
}
