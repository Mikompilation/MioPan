#include "miopan_rml_gamefiles.h"

#include "miopan_rml_common.h"
#include "miopan_rml_language.h"
#include "miopan_rmlui.h"
#include "../io/miopan_input.h"
#include "../io/miopan_paths.h"
#include "../miopan_config.h"
#include "../rendering/miopan_renderer.h"
#include "system/os/system.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_dialog.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_locale.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <atomic>
#include <mutex>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/gamefiles.rml";

/* How long "found" stays up before the fade, and the fade itself, which is
 * gamefiles.rcss's. */
constexpr Uint64 kFoundMs = 1100;
constexpr Uint64 kFadeMs = 450;

/* The game's own frame pacing is not running yet, and a vsync that is off
 * would otherwise let this screen spin a core. */
constexpr Uint64 kTickNs = SDL_NS_PER_SECOND / 60;

constexpr int kActionChoose = 0;
constexpr int kActionQuit = 1;
constexpr int kActions = 2;

enum class Stage
{
    Undecided,  /* nothing has looked yet                 */
    Asking,     /* the screen is up, waiting on the player */
    Found,      /* chosen and saved: saying so, then fading */
    Done,       /* the game can start                     */
};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct ScreenState
{
    Stage stage = Stage::Undecided;
    Rml::ElementDocument *document = nullptr;
    Rml::Element *frame = nullptr;
    Rml::Element *veil = nullptr;
    Rml::Element *actions[kActions] = {};
    Rml::Element *status = nullptr;
    Rml::Element *status_text = nullptr;
    Rml::Element *status_file = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_prompts;
    std::string shown_choose;
    int cursor = kActionChoose;
    Uint64 found_at = 0;
    Uint64 last_tick = 0;
};

/* The dialog in flight.  SDL hands the choice back on a thread of its own, so
 * it waits here for the game thread to take it. */
struct Pick
{
    std::mutex mutex;
    std::string path;           /* guarded by mutex: the file chosen    */
    std::string error;          /* guarded by mutex: why it failed      */
    std::atomic<bool> ready{false};
    std::atomic<bool> open{false};
};

ScreenState g_screen;
Pick g_pick;
Listener g_listener;

void SDLCALL Picked(void *userdata, const char *const *filelist, int filter)
{
    (void)userdata;
    (void)filter;
    /* A null list is an error and an empty one is a cancel. */
    if (filelist == nullptr || filelist[0] != nullptr)
    {
        std::lock_guard<std::mutex> lock(g_pick.mutex);
        g_pick.path = filelist != nullptr ? filelist[0] : "";
        g_pick.error = filelist != nullptr ? "" : SDL_GetError();
        g_pick.ready = true;
    }
    g_pick.open = false;
}

void Quit()
{
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

/* The name the player knows the file by, whichever way the path was written. */
std::string BaseName(const std::string &path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

/* "problem", "found" or "" for a plain note. */
void ShowStatus(const char *kind, const std::string &text, const std::string &file)
{
    g_screen.status->SetClass("problem", SDL_strcmp(kind, "problem") == 0);
    g_screen.status->SetClass("found", SDL_strcmp(kind, "found") == 0);
    g_screen.status_text->SetInnerRML(MioPanRmlCommon::Escape(text));
    g_screen.status_file->SetInnerRML(MioPanRmlCommon::Escape(file));
}

const char *ProblemText(int result)
{
    switch (result)
    {
    case MIOPAN_GAMEFILE_NOT_AN_IMAGE:
        return "That file is not one MioPan can read. Choose an .iso (a .chd or "
               ".cso has to be turned back into an .iso first) or IMG_BD.BIN.";
    case MIOPAN_GAMEFILE_NOT_THE_GAME:
        return "That disc image does not hold Fatal Frame II. Choose an image of "
               "your own copy of the game.";
    case MIOPAN_GAMEFILE_CUT_SHORT:
        return "That disc image is cut short: the game's files run past the end "
               "of it. Copy or dump the disc again.";
    case MIOPAN_GAMEFILE_OTHER_RELEASE:
        return "That is not a copy of Fatal Frame II that MioPan can read. It "
               "needs the European release, SLES-52384.";
    case MIOPAN_GAMEFILE_UNREADABLE:
    default:
        return "That file could not be read. Check that it is still there and "
               "not in use, then choose it again.";
    }
}

void OpenDialog()
{
    /* These have to outlive the call: SDL reads them until the dialog answers.
     * "All files" too: dumps are not always named .iso. */
    static std::string s_game_files;
    static std::string s_all_files;
    static SDL_DialogFileFilter s_filters[2];
    if (g_pick.open.load())
    {
        return;
    }
    s_game_files = MioPanRmlLanguage::Translate("Game files");
    s_game_files += " (*.iso, IMG_BD.BIN)";
    s_all_files = MioPanRmlLanguage::Translate("All files");
    s_filters[0] = {s_game_files.c_str(), "iso;bin"};
    s_filters[1] = {s_all_files.c_str(), "*"};

    g_pick.open = true;
    SDL_ShowOpenFileDialog(Picked, nullptr, MioPanRml::GetWindow(), s_filters,
                           (int)SDL_arraysize(s_filters), nullptr, false);
}

/* Game thread: what the dialog handed back, if it has. */
void TakePick()
{
    if (!g_pick.ready.exchange(false))
    {
        return;
    }
    std::string path;
    std::string error;
    {
        std::lock_guard<std::mutex> lock(g_pick.mutex);
        path = g_pick.path;
        error = g_pick.error;
    }

    /* Nothing was chosen because nothing could be: a desktop with neither a
     * portal nor zenity, say.  The setting is still there to be edited. */
    if (path.empty())
    {
        SDL_Log("MioPan: the file dialog failed: %s", error.c_str());
        const char *ini = MioPan_ConfigPath();
        ShowStatus("problem",
                   "The file dialog could not be opened. Add the path as "
                   "data_folder under [paths] in this file, then start MioPan "
                   "again:",
                   ini != nullptr ? ini : "miopan.ini");
        return;
    }

    const int result = MioPan_PathUseGameFile(path.c_str());
    if (result != MIOPAN_GAMEFILE_OK)
    {
        ShowStatus("problem", ProblemText(result), BaseName(path));
        return;
    }

    g_screen.stage = Stage::Found;
    g_screen.found_at = SDL_GetTicks();
    g_screen.frame->SetClass("found", true);
    ShowStatus("found", "Found it. Starting the game...", BaseName(path));
}

void Select(int action)
{
    g_screen.cursor = action;
    for (int i = 0; i < kActions; i++)
    {
        g_screen.actions[i]->SetClass("selected", i == action);
    }
}

void Activate(int action)
{
    if (action == kActionQuit)
    {
        Quit();
    }
    else
    {
        OpenDialog();
    }
}

int ActionOf(Rml::Element *element)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        for (int i = 0; i < kActions; i++)
        {
            if (element == g_screen.actions[i])
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

void HandleKey(Rml::Event &event)
{
    switch (event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN))
    {
    case Rml::Input::KI_LEFT:
    case Rml::Input::KI_UP: Select(kActionChoose); break;
    case Rml::Input::KI_RIGHT:
    case Rml::Input::KI_DOWN: Select(kActionQuit); break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE: Activate(g_screen.cursor); break;
    /* Leaving is a choice of its own, as it is on the title screen. */
    case Rml::Input::KI_ESCAPE: Select(kActionQuit); break;
    default: return;
    }
    event.StopPropagation();
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* The dialog holds the choice while it is open. */
    if (g_screen.stage != Stage::Asking || g_pick.open.load())
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }

    Rml::Element *target = event.GetTargetElement();
    if (event == Rml::EventId::Click && InElement(target, "prompt-back"))
    {
        Select(kActionQuit);
        return;
    }
    const int action = ActionOf(target);
    if (action < 0)
    {
        return;
    }
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive())
        {
            Select(action);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        Select(action);
        Activate(action);
    }
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kPrompts[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    const std::string prompts = g_screen.stage == Stage::Asking && !g_pick.open.load()
                                    ? MioPanRmlCommon::PromptsRml(kPrompts, 2)
                                    : std::string();
    if (prompts != g_screen.shown_prompts)
    {
        g_screen.shown_prompts = prompts;
        g_screen.prompts->SetInnerRML(prompts);
    }
}

void RefreshChoosing()
{
    const bool choosing = g_pick.open.load();
    const std::string label = choosing ? "Choosing..." : "Choose Game File";
    if (label != g_screen.shown_choose)
    {
        g_screen.shown_choose = label;
        g_screen.actions[kActionChoose]->SetInnerRML(label);
        g_screen.frame->SetClass("choosing", choosing);
    }
}

/*
 * The game's language is not known yet: lang_sel.c asks once the game is
 * running, and until init_Boot_Init() runs nothing else cares.  The desktop's
 * is the best guess there is for a screen shown before all of that, and
 * MioPanRmlLanguage reads it from the same place the menus' words do.
 */
void UseDesktopLanguage()
{
    static const struct
    {
        const char *code;
        u_char language;
    } kLanguages[] = {{"en", 0}, {"fr", 1}, {"de", 2}, {"es", 3}, {"it", 4}};

    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    for (int i = 0; locales != nullptr && i < count; i++)
    {
        for (const auto &language : kLanguages)
        {
            if (SDL_strcasecmp(locales[i]->language, language.code) == 0)
            {
                SetLanguage(language.language);
                SDL_free(locales);
                return;
            }
        }
    }
    SDL_free(locales);
}

bool Open()
{
    Rml::Context *context = MioPanRml::GetContext();
    if (context == nullptr)
    {
        return false;
    }

    /* Ahead of the load: a document's words are translated as it is built. */
    UseDesktopLanguage();

    Rml::ElementDocument *document = context->LoadDocument(kDocumentPath);
    if (document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }

    g_screen.frame = document->GetElementById("frame");
    g_screen.veil = document->GetElementById("veil");
    g_screen.actions[kActionChoose] = document->GetElementById("choose");
    g_screen.actions[kActionQuit] = document->GetElementById("quit");
    g_screen.status = document->GetElementById("status");
    g_screen.status_text = document->GetElementById("status-text");
    g_screen.status_file = document->GetElementById("status-file");
    g_screen.prompts = document->GetElementById("prompts");
    if (g_screen.frame == nullptr || g_screen.veil == nullptr ||
        g_screen.actions[kActionChoose] == nullptr ||
        g_screen.actions[kActionQuit] == nullptr || g_screen.status == nullptr ||
        g_screen.status_text == nullptr || g_screen.status_file == nullptr ||
        g_screen.prompts == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        document->Close();
        return false;
    }
    g_screen.document = document;

    for (Rml::EventId id :
         {Rml::EventId::Keydown, Rml::EventId::Click, Rml::EventId::Mouseover})
    {
        document->AddEventListener(id, &g_listener);
    }
    document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);

    /* A setting that has stopped working is worth saying so, or the screen
     * reads as if the player had never chosen. */
    if (miopan_config.paths.data_folder[0] != '\0')
    {
        ShowStatus("", "The game is no longer where the settings say it was:",
                   miopan_config.paths.data_folder);
    }
    return true;
}

void Close()
{
    g_screen.document->Close();
    g_screen.document = nullptr;
    g_screen.stage = Stage::Done;
}

/* Found: hold, fade, and hand over. */
void AdvanceFound()
{
    const Uint64 elapsed = SDL_GetTicks() - g_screen.found_at;
    if (elapsed >= kFoundMs)
    {
        g_screen.veil->SetClass("closing", true);
    }
    if (elapsed >= kFoundMs + kFadeMs)
    {
        Close();
    }
}

void WaitForNextTick()
{
    const Uint64 now = SDL_GetTicksNS();
    const Uint64 spent = now - g_screen.last_tick;
    if (spent < kTickNs)
    {
        SDL_DelayNS(kTickNs - spent);
    }
    g_screen.last_tick = SDL_GetTicksNS();
}
}

extern "C" {

int MioPan_RmlGameFilesTick(void)
{
    if (g_screen.stage == Stage::Done)
    {
        return 0;
    }

    if (g_screen.stage == Stage::Undecided)
    {
        if (MioPan_PathHasGameFiles() != 0)
        {
            g_screen.stage = Stage::Done;
            return 0;
        }
        SDL_Log("MioPan: no game files found; asking the player for them");
    }

    /* The first of these is what brings the window and RmlUi up, so the screen
     * cannot open ahead of it.  The pad is otherwise opened by the game's own
     * polling, which has not started, and it may be all the player has. */
    MioPan_RendererBeginFrame();
    MioPan_InputOpenGamepad();
    if (g_screen.stage == Stage::Undecided)
    {
        if (!Open())
        {
            SDL_Log("MioPan: the game-files screen is unavailable; starting "
                    "without game files");
            MioPan_RendererEndFrame();
            g_screen.stage = Stage::Done;
            return 0;
        }
        g_screen.stage = Stage::Asking;
    }

    TakePick();
    if (g_screen.stage == Stage::Found)
    {
        AdvanceFound();
    }
    if (g_screen.stage != Stage::Done)
    {
        RefreshPrompts();
        RefreshChoosing();
    }
    MioPan_RendererEndFrame();
    WaitForNextTick();
    return g_screen.stage == Stage::Done ? 0 : 1;
}
}
