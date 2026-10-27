#include "miopan_rml_options.h"
#include "miopan_rml_common.h"
#include "miopan_rmlui.h"
#include "miopan_ui.h"
#include "../io/miopan_input.h"
#include "../rendering/miopan_renderer.h"
#include "iop_voice.h"

#include "../../outgame/option.h"
#include "../../system/eeiop/snd.h"
#include "../../system/pad/pad.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/options.rml";

/* How long switching vibration on buzzes the pad, as outgame/option.c does. */
constexpr int kVibrationTestFrames = 20;

/* UTF-8 for the step arrows either side of a value. */
constexpr const char *kArrowLeft = "\xE2\x80\xB9";
constexpr const char *kArrowRight = "\xE2\x80\xBA";

using Text = std::function<std::string()>;

enum class RowKind
{
    Heading,
    Choice,
    Slider,
    Action,
};

struct Row
{
    RowKind kind = RowKind::Heading;
    std::string label;
    Text help;
    /* Greyed and passed over by the cursor while this says no. */
    std::function<bool()> enabled;
    /* A setting miopan_input owns, which has to arm the save itself. */
    bool input_setting = false;

    /* Choice: an index into names (or name_of / count_of when the choices
     * depend on the hardware or on another setting). */
    std::vector<std::string> names;
    std::function<std::string(int)> name_of;
    std::function<int()> count_of;
    std::function<int()> get;
    std::function<void(int)> set;

    /* Slider.  `step` is added -- or, logarithmic, multiplied. */
    std::function<float()> get_value;
    std::function<void(float)> set_value;
    float min = 0.0f;
    float max = 1.0f;
    float step = 0.05f;
    bool logarithmic = false;
    std::function<std::string(float)> format;

    /* Action. */
    std::string action;
    std::function<void()> run;

    /* Built elements, and what they show, so a refresh only touches what
     * changed. */
    Rml::Element *element = nullptr;
    Rml::Element *value = nullptr;
    Rml::Element *track = nullptr;
    Rml::Element *fill = nullptr;
    std::string shown_value;
    float shown_fill = -1.0f;
    int shown_enabled = -1;

    bool Selectable() const
    {
        return kind != RowKind::Heading && IsEnabled();
    }
    bool IsEnabled() const
    {
        return !enabled || enabled();
    }
    int Count() const
    {
        return count_of ? count_of() : (int)names.size();
    }
    std::string NameOf(int index) const
    {
        if (name_of)
        {
            return name_of(index);
        }
        return index >= 0 && index < (int)names.size() ? names[index] : "";
    }
};

struct Page
{
    const char *id;
    const char *name;
    std::vector<Row> rows;
    Rml::Element *element = nullptr;
    Rml::Element *tab = nullptr;
    int selected = -1;
};

struct RowRef
{
    int page;
    int row;
};

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct OptionsState
{
    Rml::Context *context = nullptr;
    Rml::ElementDocument *document = nullptr;
    Rml::Element *tabs = nullptr;
    Rml::Element *pages_root = nullptr;
    Rml::Element *help = nullptr;
    Rml::Element *prompts = nullptr;
    std::vector<Page> pages;
    std::unordered_map<Rml::Element *, RowRef> row_of;
    int page = 0;
    int from = MIOPAN_RML_OPTIONS_FROM_OVERLAY;
    bool open = false;
    bool hidden = false;
    int request = MIOPAN_RML_OPTIONS_REQ_NONE;
    int vibration_frames = 0;
    RowRef dragging = {-1, -1};
    std::string shown_help;
    std::string shown_prompts;
    MioPanRmlCommon::ConfirmDialog confirm;
};

OptionsState g_opt;
Listener g_listener;

/* ---------------------------------------------------------------------------
 *  Rows.
 * ------------------------------------------------------------------------ */

Text Fixed(const char *text)
{
    return [text] { return std::string(text); };
}

Row Heading(const char *label)
{
    Row row;
    row.kind = RowKind::Heading;
    row.label = label;
    return row;
}

Row Choice(const char *label, Text help, std::vector<std::string> names,
           std::function<int()> get, std::function<void(int)> set)
{
    Row row;
    row.kind = RowKind::Choice;
    row.label = label;
    row.help = std::move(help);
    row.names = std::move(names);
    row.get = std::move(get);
    row.set = std::move(set);
    return row;
}

Row Toggle(const char *label, Text help, std::function<bool()> get,
           std::function<void(bool)> set, const char *off = "Off",
           const char *on = "On")
{
    return Choice(label, std::move(help), {off, on},
                  [get] { return get() ? 1 : 0; },
                  [set](int value) { set(value != 0); });
}

Row Slider(const char *label, Text help, float min, float max, float step,
           std::function<float()> get, std::function<void(float)> set,
           std::function<std::string(float)> format)
{
    Row row;
    row.kind = RowKind::Slider;
    row.label = label;
    row.help = std::move(help);
    row.min = min;
    row.max = max;
    row.step = step;
    row.get_value = std::move(get);
    row.set_value = std::move(set);
    row.format = std::move(format);
    return row;
}

Row Action(const char *label, Text help, const char *action,
           std::function<void()> run)
{
    Row row;
    row.kind = RowKind::Action;
    row.label = label;
    row.help = std::move(help);
    row.action = action;
    row.run = std::move(run);
    return row;
}

Row &When(Row &&row, std::function<bool()> enabled)
{
    row.enabled = std::move(enabled);
    return row;
}

std::string Printf(const char *format, double value)
{
    char text[64];
    SDL_snprintf(text, sizeof(text), format, value);
    return text;
}

std::function<std::string(float)> Format(const char *format)
{
    return [format](float value) { return Printf(format, value); };
}

std::function<std::string(float)> Percent(float scale)
{
    return [scale](float value) {
        return Printf("%.0f%%", (double)(value * scale));
    };
}

/* ---------------------------------------------------------------------------
 *  The game's own options.
 *
 *  opt_wrk is what the game reads; optm is the option screen's working copy,
 *  which OptionEnd() writes back over opt_wrk whenever the two differ.  Every
 *  edit goes to both, so it is live at once and OptionEnd() has nothing to
 *  undo.  Sound output and volume reach the driver straight away, as they do
 *  from the original screen's pad handler.
 * ------------------------------------------------------------------------ */

void SetGameVibration(bool on)
{
    const bool was = opt_wrk.pad_vib != 0;
    opt_wrk.pad_vib = optm.pad_vib = on ? 1 : 0;
    /* Switching it on buzzes the pad, so the setting can be felt. */
    g_opt.vibration_frames = (on && !was) ? kVibrationTestFrames : 0;
}

void SetGameSoundOutput(int mono)
{
    opt_wrk.snd_output = optm.snd_output = (u_char)mono;
    if (mono != 0)
    {
        SndSetMono();
    }
    else
    {
        SndSetStereo();
    }
}

void SetGameVolume(float volume)
{
    const int value = std::clamp((int)std::lround(volume), 0, 0x100);
    opt_wrk.snd_volume = optm.snd_volume = value;
    SndSetGroupVolume(1, value);
    SndSetGroupVolume(0, value);
}

void RestoreGameDefaults()
{
    InitOptionSetup(&optm);
    const bool vibration_was = opt_wrk.pad_vib != 0;
    opt_wrk = optm;
    OptSoundSetupRef(&opt_wrk);
    if (!vibration_was && opt_wrk.pad_vib != 0)
    {
        g_opt.vibration_frames = kVibrationTestFrames;
    }
}

Page GamePage()
{
    Page page{"game", "Game", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Toggle(
        "Vibration", Fixed("Let the controller shake when the game calls for it."),
        [] { return opt_wrk.pad_vib != 0; }, SetGameVibration));
    rows.push_back(Toggle(
        "Subtitles", Fixed("Captions under the spoken lines of the movies."),
        [] { return opt_wrk.credits != 0; },
        [](bool on) { opt_wrk.credits = optm.credits = on ? 1 : 0; }));
    rows.push_back(Choice(
        "Sound", Fixed("Stereo, or both speakers carrying the same mix."),
        {"Stereo", "Mono"}, [] { return opt_wrk.snd_output != 0 ? 1 : 0; },
        SetGameSoundOutput));
    rows.push_back(Slider(
        "Volume", Fixed("The game's own level for music, voices and effects."),
        0.0f, 256.0f, 8.0f, [] { return (float)opt_wrk.snd_volume; },
        SetGameVolume, Percent(100.0f / 256.0f)));
    rows.push_back(Slider(
        "Brightness",
        Fixed("The game's brightness filter. Raise it until the darkest "
              "corners of a room just show."),
        16.0f, 144.0f, 4.0f, [] { return (float)opt_wrk.brightness; },
        [](float value) {
            opt_wrk.brightness = optm.brightness =
                std::clamp((int)std::lround(value), 0x10, 0x90);
        },
        [](float value) {
            return Printf("%.0f", (double)((value - 16.0f) / 4.0f + 1.0f));
        }));

    rows.push_back(Heading("Controls"));
    rows.push_back(Choice(
        "Movement",
        Fixed("Camera-relative: Mio heads where you push, as the camera sees "
              "it. Tank: left and right turn her and up walks forward."),
        {"Camera-relative", "Tank"},
        [] { return opt_wrk.move_operate != 0 ? 1 : 0; },
        [](int value) {
            opt_wrk.move_operate = optm.move_operate = (u_char)value;
        }));
    rows.push_back(Choice(
        "Viewfinder vertical", Fixed("Which way up and down turn the Camera Obscura."),
        {"Normal", "Inverted"}, [] { return opt_wrk.view_vertical != 0 ? 1 : 0; },
        [](int value) {
            opt_wrk.view_vertical = optm.view_vertical = (u_char)value;
        }));
    rows.push_back(Choice(
        "Viewfinder aim",
        Fixed("The stick that aims the Camera Obscura; the other one "
              "sidesteps."),
        {"Left stick", "Right stick"},
        [] { return opt_wrk.ana_replace != 0 ? 1 : 0; },
        [](int value) {
            opt_wrk.ana_replace = optm.ana_replace = (u_char)value;
        }));
    rows.push_back(Choice(
        "Button layout", Fixed("The game's three DUALSHOCK 2 button arrangements."),
        {"Type A", "Type B", "Type C"},
        [] { return std::clamp((int)opt_wrk.pad_type, 0, 2); },
        [](int value) { opt_wrk.pad_type = optm.pad_type = (u_char)value; }));

    rows.push_back(Heading(""));
    rows.push_back(Action(
        "Restore defaults", Fixed("Put every setting on this page back as it was."),
        "Restore", [] {
            g_opt.confirm.Open("Restore the game's default settings?", "",
                               RestoreGameDefaults);
        }));
    if (g_opt.from == MIOPAN_RML_OPTIONS_FROM_TITLE)
    {
        rows.push_back(Action(
            "Save to memory card",
            Fixed("Write these settings to the system file, so the game "
                  "starts with them."),
            "Save", [] { g_opt.request = MIOPAN_RML_OPTIONS_REQ_SAVE; }));
    }
    return page;
}

/* ---------------------------------------------------------------------------
 *  The port's settings -- the overlay's Settings menu.
 * ------------------------------------------------------------------------ */

bool HdrActive()
{
    return MioPan_RendererGetHdrActive() != 0;
}

void SetGrade(int which, float value)
{
    float grade[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    MioPan_RendererGetGrade(&grade[0], &grade[1], &grade[2], &grade[3]);
    grade[which] = value;
    MioPan_RendererSetGrade(grade[0], grade[1], grade[2], grade[3]);
}

float GetGrade(int which)
{
    float grade[4] = {1.0f, 1.0f, 1.0f, 1.0f};
    MioPan_RendererGetGrade(&grade[0], &grade[1], &grade[2], &grade[3]);
    return grade[which];
}

std::string HdrHelp()
{
    float white = 0.0f;
    float peak = 0.0f;
    MioPan_RendererGetHdrDisplayInfo(&white, &peak);
    std::string text = HdrActive() ? "Presenting in HDR." : "Presenting in SDR.";
    if (white > 0.0f)
    {
        char note[96];
        SDL_snprintf(note, sizeof(note),
                     " The display reports SDR white at %.0f nits and a "
                     "%.0f nit peak.",
                     (double)white, (double)peak);
        text += note;
    }
    return text;
}

Page DisplayPage()
{
    Page page{"display", "Display", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Choice(
        "Window", Fixed("A window on the desktop, or the whole screen. Alt+Enter "
                        "switches too."),
        {"Windowed", "Borderless fullscreen"},
        [] { return MioPan_RendererGetWindowMode(); },
        [](int mode) { MioPan_RendererSetWindowMode(mode); }));
    {
        static const int kModes[] = {MIOPAN_VSYNC_ON, MIOPAN_VSYNC_OFF,
                                     MIOPAN_VSYNC_MAILBOX};
        rows.push_back(Choice(
            "Vertical sync",
            Fixed("On never tears. Off never waits, and tears. Mailbox shows "
                  "the newest frame without either, where the driver has it."),
            {"On", "Off", "Mailbox"},
            [] {
                const int mode = MioPan_RendererGetVsync();
                for (int i = 0; i < 3; i++)
                {
                    if (kModes[i] == mode)
                    {
                        return i;
                    }
                }
                return 0;
            },
            [](int index) { MioPan_RendererSetVsync(kModes[index]); }));
    }
    rows.push_back(Choice(
        "Aspect ratio",
        Fixed("Auto fills the window and shows more of the world at the "
              "sides. Original keeps the PS2's 640x448 framing."),
        {"Auto", "Original (PS2)", "4:3", "16:9", "16:10", "Custom"},
        [] {
            int mode = MIOPAN_ASPECT_AUTO;
            MioPan_RendererGetAspectMode(&mode, nullptr);
            return mode;
        },
        [](int mode) {
            float custom = 16.0f / 9.0f;
            MioPan_RendererGetAspectMode(nullptr, &custom);
            MioPan_RendererSetAspectMode(mode, custom);
        }));
    rows.push_back(When(
        Slider("Custom ratio", Fixed("Width over height, for Custom."), 1.0f,
               3.0f, 0.05f,
               [] {
                   float custom = 16.0f / 9.0f;
                   MioPan_RendererGetAspectMode(nullptr, &custom);
                   return custom;
               },
               [](float ratio) {
                   MioPan_RendererSetAspectMode(MIOPAN_ASPECT_CUSTOM, ratio);
               },
               Format("%.2f : 1")),
        [] {
            int mode = MIOPAN_ASPECT_AUTO;
            MioPan_RendererGetAspectMode(&mode, nullptr);
            return mode == MIOPAN_ASPECT_CUSTOM;
        }));

    rows.push_back(Heading("Picture"));
    static const char *const kGradeNames[] = {"Brightness", "Contrast",
                                              "Gamma", "Saturation"};
    static const float kGradeRange[4][2] = {
        {0.25f, 2.0f}, {0.5f, 2.0f}, {0.5f, 2.5f}, {0.0f, 2.0f}};
    static const char *const kGradeHelp[] = {
        "Brightness of the finished picture, after the game's own filter.",
        "Spread between the darkest and lightest tones.",
        "Lifts or deepens the mid-tones without touching black or white.",
        "Strength of colour; 0 is black and white."};
    for (int i = 0; i < 4; i++)
    {
        rows.push_back(Slider(
            kGradeNames[i], Fixed(kGradeHelp[i]), kGradeRange[i][0],
            kGradeRange[i][1], 0.05f, [i] { return GetGrade(i); },
            [i](float value) { SetGrade(i, value); }, Format("%.2f")));
    }
    rows.push_back(Action(
        "Reset picture", Fixed("All four back to 1.00, which skips the pass."),
        "Reset", [] { MioPan_RendererSetGrade(1.0f, 1.0f, 1.0f, 1.0f); }));

    rows.push_back(Heading("HDR"));
    rows.push_back(Choice(
        "HDR output", HdrHelp, {"Off", "Auto", "scRGB", "HDR10"},
        [] { return MioPan_RendererGetHdrMode(); },
        [](int mode) { MioPan_RendererSetHdrMode(mode); }));
    rows.push_back(When(
        Choice("Paper white",
               Fixed("How bright the game's white is in HDR: the system's SDR "
                     "white, or a level of your own."),
               {"Follow system", "Manual"},
               [] { return MioPan_RendererGetHdrPaperWhite() > 0.0f ? 1 : 0; },
               [](int manual) {
                   float paper = 0.0f;
                   MioPan_RendererGetHdrEffective(&paper, nullptr);
                   MioPan_RendererSetHdrPaperWhite(
                       manual != 0 ? (paper > 0.0f ? paper : 200.0f) : 0.0f);
               }),
        HdrActive));
    rows.push_back(When(
        Slider("Paper white level", Fixed("Nits for the game's white."), 50.0f,
               1000.0f, 10.0f,
               [] {
                   float paper = 0.0f;
                   MioPan_RendererGetHdrEffective(&paper, nullptr);
                   return paper;
               },
               [](float nits) { MioPan_RendererSetHdrPaperWhite(nits); },
               Format("%.0f nits")),
        [] { return HdrActive() && MioPan_RendererGetHdrPaperWhite() > 0.0f; }));
    rows.push_back(When(
        Choice("Peak brightness",
               Fixed("The brightest the picture may go: what the display "
                     "reports, or a level of your own."),
               {"Follow display", "Manual"},
               [] { return MioPan_RendererGetHdrPeak() > 0.0f ? 1 : 0; },
               [](int manual) {
                   float peak = 0.0f;
                   MioPan_RendererGetHdrEffective(nullptr, &peak);
                   MioPan_RendererSetHdrPeak(
                       manual != 0 ? (peak > 0.0f ? peak : 1000.0f) : 0.0f);
               }),
        HdrActive));
    rows.push_back(When(
        Slider("Peak level", Fixed("Nits at the top of the range."), 100.0f,
               4000.0f, 50.0f,
               [] {
                   float peak = 0.0f;
                   MioPan_RendererGetHdrEffective(nullptr, &peak);
                   return peak;
               },
               [](float nits) { MioPan_RendererSetHdrPeak(nits); },
               Format("%.0f nits")),
        [] { return HdrActive() && MioPan_RendererGetHdrPeak() > 0.0f; }));
    rows.push_back(When(
        Slider("Highlight expansion",
               Fixed("Stretches the brightest tones into the HDR headroom. "
                     "0 invents nothing -- the faithful setting."),
               0.0f, 1.0f, 0.05f,
               [] {
                   float strength = 0.0f;
                   float knee = 0.0f;
                   MioPan_RendererGetHdrExpansion(&strength, &knee);
                   return strength;
               },
               [](float strength) {
                   float old_strength = 0.0f;
                   float knee = 0.0f;
                   MioPan_RendererGetHdrExpansion(&old_strength, &knee);
                   MioPan_RendererSetHdrExpansion(strength, knee);
               },
               Format("%.2f")),
        HdrActive));
    rows.push_back(When(
        Slider("Expansion knee", Fixed("Where in the range the stretch begins."),
               0.5f, 1.0f, 0.05f,
               [] {
                   float strength = 0.0f;
                   float knee = 0.0f;
                   MioPan_RendererGetHdrExpansion(&strength, &knee);
                   return knee;
               },
               [](float knee) {
                   float strength = 0.0f;
                   float old_knee = 0.0f;
                   MioPan_RendererGetHdrExpansion(&strength, &old_knee);
                   MioPan_RendererSetHdrExpansion(strength, knee);
               },
               Format("%.2f")),
        HdrActive));
    return page;
}

int Log2(int value)
{
    int bits = 0;
    while (value > 1)
    {
        value >>= 1;
        bits++;
    }
    return bits;
}

bool Smoothing()
{
    return MioPan_RendererGetSmoothingMode() == MIOPAN_SMOOTHING_DISPLAY ||
           MioPan_RendererGetExtraPresents() != 0;
}

void GetFinder(int *enable, float values[4])
{
    MioPan_RendererGetFinderMask(enable, &values[0], &values[1], &values[2],
                                 &values[3]);
}

void SetFinderValue(int which, float value)
{
    int enable = 0;
    float values[4] = {};
    GetFinder(&enable, values);
    values[which] = value;
    MioPan_RendererSetFinderMask(enable, values[0], values[1], values[2],
                                 values[3]);
}

bool FinderOn()
{
    int enable = 0;
    float values[4] = {};
    GetFinder(&enable, values);
    return enable != 0;
}

Page GraphicsPage()
{
    Page page{"graphics", "Graphics", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Choice(
        "Render resolution",
        Fixed("Match the window, the PS2's own 640x448 times a scale, or the "
              "window times a scale."),
        {"Match window", "Native PS2", "Window scale"},
        [] {
            int mode = MIOPAN_RENDER_RES_MATCH_WINDOW;
            MioPan_RendererGetRenderResolution(&mode, nullptr);
            return mode;
        },
        [](int mode) {
            float scale = 1.0f;
            MioPan_RendererGetRenderResolution(nullptr, &scale);
            MioPan_RendererSetRenderResolution(mode, scale);
        }));
    rows.push_back(When(
        Slider("Resolution scale",
               [] {
                   int width = 0;
                   int height = 0;
                   MioPan_RendererGetRenderSize(&width, &height);
                   char text[96];
                   SDL_snprintf(text, sizeof(text),
                                "The multiplier for the mode above. The game "
                                "is drawn at %dx%d now.",
                                width, height);
                   return std::string(text);
               },
               0.25f, 4.0f, 0.25f,
               [] {
                   float scale = 1.0f;
                   MioPan_RendererGetRenderResolution(nullptr, &scale);
                   return scale;
               },
               [](float scale) {
                   int mode = MIOPAN_RENDER_RES_MATCH_WINDOW;
                   MioPan_RendererGetRenderResolution(&mode, nullptr);
                   MioPan_RendererSetRenderResolution(mode, scale);
               },
               Format("%.2fx")),
        [] {
            int mode = MIOPAN_RENDER_RES_MATCH_WINDOW;
            MioPan_RendererGetRenderResolution(&mode, nullptr);
            return mode != MIOPAN_RENDER_RES_MATCH_WINDOW;
        }));
    rows.push_back(Choice(
        "Upscaling", Fixed("How a smaller picture is enlarged to the window."),
        {"Sharp", "Smooth"}, [] { return MioPan_RendererGetUpscaleFilter(); },
        [](int filter) { MioPan_RendererSetUpscaleFilter(filter); }));
    rows.push_back(Toggle(
        "Smooth sprites",
        Fixed("Bilinear filtering on text, sprites and effects. Off samples "
              "them as the PS2 did."),
        [] { return MioPan_RendererGetSmooth2d() != 0; },
        [](bool on) { MioPan_RendererSetSmooth2d(on ? 1 : 0); }));
    {
        Row msaa = Choice(
            "Anti-aliasing",
            Fixed("Multisampling smooths the edges of the world. Costly in "
                  "scenes full of effects, and the depth read-back that "
                  "stops a ghost being shot through a wall is off with it."),
            {"Off", "2x", "4x", "8x"},
            [] { return Log2(MioPan_RendererGetMsaaSamples()); },
            [](int index) { MioPan_RendererSetMsaaSamples(1 << index); });
        msaa.count_of = [] {
            return std::clamp(Log2(MioPan_RendererGetMsaaMaxSamples()) + 1, 1, 4);
        };
        rows.push_back(std::move(msaa));
    }
    rows.push_back(Choice(
        "Texture filtering",
        Fixed("Anisotropic filtering keeps floors sharp at grazing angles. "
              "Close to free."),
        {"Off", "2x", "4x", "8x", "16x"},
        [] { return std::clamp(Log2(MioPan_RendererGetAnisotropy()), 0, 4); },
        [](int index) { MioPan_RendererSetAnisotropy(1 << index); }));

    rows.push_back(Heading("Motion"));
    rows.push_back(Toggle(
        "Display-rate smoothing",
        Fixed("Present once per monitor refresh, each frame blended to the "
              "moment it reaches the screen. The game still runs at its own "
              "pace."),
        [] { return MioPan_RendererGetSmoothingMode() == MIOPAN_SMOOTHING_DISPLAY; },
        [](bool on) {
            MioPan_RendererSetSmoothingMode(on ? MIOPAN_SMOOTHING_DISPLAY
                                               : MIOPAN_SMOOTHING_FIELD);
        }));
    {
        Row extra = Choice(
            "Extra frames",
            Fixed("Without display-rate smoothing: frames shown between the "
                  "game's own, at up to 60 a second."),
            {"Off", "1 extra", "2 extra", "3 extra"},
            [] { return MioPan_RendererGetExtraPresents(); },
            [](int extra) { MioPan_RendererSetExtraPresents(extra); });
        extra.count_of = [] {
            return std::clamp(MioPan_RendererGetMaxExtraPresents() + 1, 1, 4);
        };
        extra.enabled = [] {
            return MioPan_RendererGetSmoothingMode() != MIOPAN_SMOOTHING_DISPLAY;
        };
        rows.push_back(std::move(extra));
    }
    rows.push_back(When(
        Toggle("Interpolate camera",
               Fixed("Move the camera between the game's frames. Off repeats "
                     "the same picture."),
               [] { return MioPan_RendererGetPresentInterpolation() != 0; },
               [](bool on) { MioPan_RendererSetPresentInterpolation(on ? 1 : 0); }),
        Smoothing));
    rows.push_back(When(
        Toggle("Interpolate motion",
               Fixed("Move characters and objects between the game's frames "
                     "too, not only the camera."),
               [] { return MioPan_RendererGetGeometryInterpolation() != 0; },
               [](bool on) { MioPan_RendererSetGeometryInterpolation(on ? 1 : 0); }),
        [] { return Smoothing() && MioPan_RendererGetPresentInterpolation() != 0; }));

    rows.push_back(Heading("Lighting and effects"));
    rows.push_back(Choice(
        "Lighting",
        Fixed("Vertex lights every vertex on the CPU, as the PS2 did. The "
              "per-pixel modes light spotlights, or everything, per pixel."),
        {"Vertex (PS2)", "Per-pixel spotlights", "Per-pixel, all lights"},
        [] { return MioPan_RendererGetLightingMode(); },
        [](int mode) { MioPan_RendererSetLightingMode(mode); }));
    rows.push_back(Choice(
        "Shadows", Fixed("Hard is the PS2's one-tap edge; soft averages it."),
        {"Hard (PS2)", "Soft"}, [] { return MioPan_RendererGetShadowFilter(); },
        [](int filter) { MioPan_RendererSetShadowFilter(filter); }));
    rows.push_back(Choice(
        "Film grain",
        Fixed("The grain over the picture: off, the PS2's sheet at its own "
              "size, or one grain per screen pixel."),
        {"Off", "PS2", "Native resolution"},
        [] { return MioPan_RendererGetFilmGrain(); },
        [](int mode) { MioPan_RendererSetFilmGrain(mode); }));
    rows.push_back(Slider(
        "Cut-out sharpening",
        Fixed("Restores the crisp alpha edge on foliage, fences and hair "
              "when the picture is drawn above the PS2's size."),
        0.0f, 1.0f, 0.05f, [] { return MioPan_RendererGetAlphaSharpen(); },
        [](float value) { MioPan_RendererSetAlphaSharpen(value); },
        Format("%.2f")));
    rows.push_back(Slider(
        "Cut-out threshold",
        Fixed("Discards any texel fainter than this. A blunter tool; 0 is "
              "off."),
        0.0f, 1.0f, 0.05f, [] { return MioPan_RendererGetAlphaCutoff(); },
        [](float value) { MioPan_RendererSetAlphaCutoff(value); },
        Format("%.2f")));

    rows.push_back(Heading("Viewfinder surround"));
    rows.push_back(Toggle(
        "Surround",
        Fixed("Treat the world beyond the PS2's 640x448 while the Camera "
              "Obscura is raised -- a port addition."),
        FinderOn, [](bool on) {
            int enable = 0;
            float values[4] = {};
            GetFinder(&enable, values);
            MioPan_RendererSetFinderMask(on ? 1 : 0, values[0], values[1],
                                         values[2], values[3]);
        }));
    static const char *const kFinderNames[] = {"Blur", "Darken", "Crimson",
                                               "Frame size"};
    static const char *const kFinderHelp[] = {
        "How far out of focus the surround falls.",
        "How dark it is; full with no blur is plain black.",
        "How much of the Camera Obscura's red it takes on.",
        "1.00 is the original frame; smaller covers part of the picture."};
    for (int i = 0; i < 4; i++)
    {
        const bool frame = i == 3;
        rows.push_back(When(
            Slider(kFinderNames[i], Fixed(kFinderHelp[i]),
                   frame ? 0.25f : 0.0f, frame ? 2.0f : 1.0f, 0.05f,
                   [i] {
                       int enable = 0;
                       float values[4] = {};
                       GetFinder(&enable, values);
                       return values[i];
                   },
                   [i](float value) { SetFinderValue(i, value); },
                   Format("%.2f")),
            FinderOn));
    }
    return page;
}

Page AudioPage()
{
    Page page{"audio", "Audio", {}};
    std::vector<Row> &rows = page.rows;

    rows.push_back(Slider(
        "Master volume",
        Fixed("Everything, movies included, on top of the game's own Volume "
              "on the Game page."),
        0.0f, 1.0f, 0.05f, [] { return MioPan_VoiceGetOutputVolume(); },
        [](float volume) { MioPan_VoiceSetOutputVolume(volume); },
        Percent(100.0f)));

    rows.push_back(Heading("In the background"));
    rows.push_back(Toggle(
        "Mute",
        Fixed("Silence the game while the window is minimised or another "
              "window has the focus."),
        [] { return MioPan_RendererGetMuteInBackground() != 0; },
        [](bool on) { MioPan_RendererSetMuteInBackground(on ? 1 : 0); }));
    rows.push_back(Toggle(
        "Pause",
        Fixed("Hold the whole game -- sound and movies too -- and carry on "
              "from the same frame when the window comes back."),
        [] { return MioPan_RendererGetPauseInBackground() != 0; },
        [](bool on) { MioPan_RendererSetPauseInBackground(on ? 1 : 0); }));
    return page;
}

Row InputChoice(const char *label, Text help, std::vector<std::string> names,
                std::function<int()> get, std::function<void(int)> set)
{
    Row row = Choice(label, std::move(help), std::move(names), std::move(get),
                     std::move(set));
    row.input_setting = true;
    return row;
}

Row InputToggle(const char *label, Text help, std::function<bool()> get,
                std::function<void(bool)> set)
{
    Row row = Toggle(label, std::move(help), std::move(get), std::move(set));
    row.input_setting = true;
    return row;
}

Row InputSlider(const char *label, Text help, float min, float max,
                std::function<float()> get, std::function<void(float)> set)
{
    /* Sensitivities span two orders of magnitude, so each step multiplies. */
    Row row = Slider(label, std::move(help), min, max, 1.1f, std::move(get),
                     std::move(set), Format("%.2fx"));
    row.logarithmic = true;
    row.input_setting = true;
    return row;
}

Row InputAction(const char *label, Text help, const char *action,
                std::function<void()> run)
{
    Row row = Action(label, std::move(help), action, std::move(run));
    row.input_setting = true;
    return row;
}

std::string GameSchemeName(int index, const char *game_label)
{
    if (index == 0)
    {
        return std::string("Game setting (") + game_label + ")";
    }
    return index == 1 ? "Camera-relative" : "Tank";
}

std::string GyroHelp()
{
    MioPan_InputGyroStatus status;
    MioPan_InputGetGyroStatus(&status);
    std::string text = "Aim the raised Camera Obscura by moving the "
                       "controller, alongside the stick. ";
    if (status.has_gamepad == 0)
    {
        text += "No controller is connected.";
    }
    else if (status.has_gyro == 0)
    {
        text += "This controller has no gyro.";
    }
    else if (status.active != 0 && status.calibration < 1.0f)
    {
        text += "Learning its drift: set it down and keep it still.";
    }
    else
    {
        text += "Its drift is learned whenever it lies still.";
    }
    return text;
}

Page ControlsPage()
{
    Page page{"controls", "Controls", {}};
    std::vector<Row> &rows = page.rows;

    Row stick = InputChoice(
        "Stick steering",
        Fixed("Overrides the game's Movement for the left stick alone. "
              "Whichever control is pushing steers."),
        {}, [] { return MioPan_InputGetStickMoveScheme(); },
        [](int scheme) { MioPan_InputSetStickMoveScheme(scheme); });
    stick.count_of = [] { return 3; };
    stick.name_of = [](int index) {
        return GameSchemeName(index, opt_wrk.move_operate != 0 ? "tank"
                                                               : "camera-relative");
    };
    rows.push_back(std::move(stick));

    Row dpad = InputChoice(
        "D-pad steering",
        Fixed("Overrides the game's Movement for the d-pad alone. In the "
              "game's camera-relative mode the d-pad does not walk."),
        {}, [] { return MioPan_InputGetDpadMoveScheme(); },
        [](int scheme) { MioPan_InputSetDpadMoveScheme(scheme); });
    dpad.count_of = [] { return 3; };
    dpad.name_of = [](int index) {
        return GameSchemeName(index, opt_wrk.move_operate != 0 ? "tank"
                                                               : "does not walk");
    };
    rows.push_back(std::move(dpad));

    Row aim = InputChoice(
        "Viewfinder sticks",
        Fixed("Overrides the game's Viewfinder aim: which stick aims and "
              "which sidesteps."),
        {}, [] { return MioPan_InputGetFinderAimStick(); },
        [](int stick_choice) { MioPan_InputSetFinderAimStick(stick_choice); });
    aim.count_of = [] { return 3; };
    aim.name_of = [](int index) -> std::string {
        if (index == 0)
        {
            return opt_wrk.ana_replace != 0 ? "Game setting (right aims)"
                                            : "Game setting (left aims)";
        }
        return index == 1 ? "Left aims" : "Right aims";
    };
    rows.push_back(std::move(aim));
    rows.push_back(When(
        InputAction("Reset steering", Fixed("All three back to the game's setting."),
                    "Reset", [] { MioPan_InputResetMoveSettings(); }),
        [] { return MioPan_InputMoveSettingsAreDefault() == 0; }));

    rows.push_back(Heading("Mouse"));
    rows.push_back(InputToggle(
        "Mouse aiming",
        Fixed("The cursor is captured while the Camera Obscura is raised and "
              "the mouse turns it."),
        [] { return MioPan_InputGetMouseLook() != 0; },
        [](bool on) { MioPan_InputSetMouseLook(on ? 1 : 0); }));
    const auto mouse_on = [] { return MioPan_InputGetMouseLook() != 0; };
    rows.push_back(When(
        InputSlider("Mouse sensitivity", Fixed("How far one movement turns the view."),
                    MIOPAN_MOUSE_SENSITIVITY_MIN, MIOPAN_MOUSE_SENSITIVITY_MAX,
                    [] { return MioPan_InputGetMouseSensitivity(); },
                    [](float value) { MioPan_InputSetMouseSensitivity(value); }),
        mouse_on));
    rows.push_back(When(
        InputToggle("Invert mouse X", Fixed("Swap left and right."),
                    [] { return MioPan_InputGetMouseInvertX() != 0; },
                    [](bool on) { MioPan_InputSetMouseInvertX(on ? 1 : 0); }),
        mouse_on));
    rows.push_back(When(
        InputToggle("Invert mouse Y", Fixed("Swap up and down."),
                    [] { return MioPan_InputGetMouseInvertY() != 0; },
                    [](bool on) { MioPan_InputSetMouseInvertY(on ? 1 : 0); }),
        mouse_on));
    rows.push_back(When(
        InputAction("Reset mouse", Fixed("Mouse aiming back to its defaults."),
                    "Reset", [] { MioPan_InputResetMouseSettings(); }),
        [] { return MioPan_InputMouseSettingsAreDefault() == 0; }));

    rows.push_back(Heading("Gyro"));
    rows.push_back(InputToggle("Gyro aiming", GyroHelp,
                               [] { return MioPan_InputGetGyroLook() != 0; },
                               [](bool on) { MioPan_InputSetGyroLook(on ? 1 : 0); }));
    const auto gyro_on = [] { return MioPan_InputGetGyroLook() != 0; };
    rows.push_back(When(
        InputSlider("Gyro sensitivity",
                    Fixed("1.00x turns the view as far as the controller turns."),
                    MIOPAN_GYRO_SENSITIVITY_MIN, MIOPAN_GYRO_SENSITIVITY_MAX,
                    [] { return MioPan_InputGetGyroSensitivity(); },
                    [](float value) { MioPan_InputSetGyroSensitivity(value); }),
        gyro_on));
    rows.push_back(When(
        InputChoice("Turn with",
                    Fixed("Yaw: point the controller left and right. Roll: tip "
                          "it sideways like a wheel."),
                    {"Yaw", "Roll"}, [] { return MioPan_InputGetGyroTurnAxis(); },
                    [](int axis) { MioPan_InputSetGyroTurnAxis(axis); }),
        gyro_on));
    rows.push_back(When(
        InputToggle("Invert gyro X", Fixed("Swap left and right."),
                    [] { return MioPan_InputGetGyroInvertX() != 0; },
                    [](bool on) { MioPan_InputSetGyroInvertX(on ? 1 : 0); }),
        gyro_on));
    rows.push_back(When(
        InputToggle("Invert gyro Y", Fixed("Swap up and down."),
                    [] { return MioPan_InputGetGyroInvertY() != 0; },
                    [](bool on) { MioPan_InputSetGyroInvertY(on ? 1 : 0); }),
        gyro_on));
    rows.push_back(When(
        Action("Recalibrate gyro",
               Fixed("Forget the learned drift; set the controller down and "
                     "keep it still to learn it again."),
               "Recalibrate", [] { MioPan_InputGyroRecalibrate(); }),
        gyro_on));
    rows.push_back(When(
        InputAction("Reset gyro", Fixed("Gyro aiming back to its defaults."),
                    "Reset", [] { MioPan_InputResetGyroSettings(); }),
        [] { return MioPan_InputGyroSettingsAreDefault() == 0; }));

    rows.push_back(Heading("Bindings"));
    rows.push_back(When(
        InputAction("Reset all bindings",
                    Fixed("Every key, mouse button and controller button back "
                          "to the defaults. F1 opens the overlay to rebind "
                          "them one by one."),
                    "Reset", [] {
                        g_opt.confirm.Open(
                            "Reset every binding to its default?", "", [] {
                                MioPan_InputResetBindings();
                                MioPanUi::NoteSettingsChanged();
                            });
                    }),
        [] { return MioPan_InputBindingsAreDefault() == 0; }));
    return page;
}

/* ---------------------------------------------------------------------------
 *  Building the document.
 * ------------------------------------------------------------------------ */

std::string RowId(int page, int row)
{
    return "row-" + std::to_string(page) + "-" + std::to_string(row);
}

std::string RowRml(const Row &row, int page, int index)
{
    using MioPanRmlCommon::Escape;
    const std::string id = RowId(page, index);
    if (row.kind == RowKind::Heading)
    {
        return "<div class=\"heading\" id=\"" + id + "\">" + Escape(row.label) +
               "</div>";
    }

    const char *kind = row.kind == RowKind::Choice   ? "choice"
                       : row.kind == RowKind::Slider ? "slider"
                                                     : "action";
    std::string rml = "<div class=\"row selectable " + std::string(kind) +
                      "\" id=\"" + id + "\"><span class=\"row-label\">" +
                      Escape(row.label) + "</span><span class=\"row-value\">";
    if (row.kind == RowKind::Action)
    {
        rml += "<span class=\"value action-value\" id=\"" + id + "-value\">" +
               Escape(row.action) + "</span>";
    }
    else
    {
        rml += "<span class=\"arrow left\">" + std::string(kArrowLeft) + "</span>";
        if (row.kind == RowKind::Slider)
        {
            rml += "<span class=\"track\" id=\"" + id +
                   "-track\"><span class=\"fill\" id=\"" + id +
                   "-fill\"></span></span>";
        }
        rml += "<span class=\"value\" id=\"" + id + "-value\"></span>";
        rml += "<span class=\"arrow right\">" + std::string(kArrowRight) + "</span>";
    }
    rml += "</span></div>";
    return rml;
}

void BuildPages()
{
    g_opt.pages.clear();
    g_opt.pages.push_back(GamePage());
    g_opt.pages.push_back(DisplayPage());
    g_opt.pages.push_back(GraphicsPage());
    g_opt.pages.push_back(AudioPage());
    g_opt.pages.push_back(ControlsPage());

    using MioPanRmlCommon::Escape;
    std::string tabs;
    std::string pages;
    for (int p = 0; p < (int)g_opt.pages.size(); p++)
    {
        const Page &page = g_opt.pages[p];
        tabs += "<span class=\"tab\" id=\"tab-" + std::to_string(p) + "\">" +
                Escape(page.name) + "</span>";
        pages += "<div class=\"page hidden\" id=\"page-" + std::string(page.id) +
                 "\">";
        for (int r = 0; r < (int)page.rows.size(); r++)
        {
            pages += RowRml(page.rows[r], p, r);
        }
        pages += "</div>";
    }
    g_opt.tabs->SetInnerRML(tabs);
    g_opt.pages_root->SetInnerRML(pages);

    g_opt.row_of.clear();
    for (int p = 0; p < (int)g_opt.pages.size(); p++)
    {
        Page &page = g_opt.pages[p];
        page.element = g_opt.document->GetElementById(std::string("page-") + page.id);
        page.tab = g_opt.document->GetElementById("tab-" + std::to_string(p));
        page.selected = -1;
        for (int r = 0; r < (int)page.rows.size(); r++)
        {
            Row &row = page.rows[r];
            const std::string id = RowId(p, r);
            row.element = g_opt.document->GetElementById(id);
            row.value = g_opt.document->GetElementById(id + "-value");
            row.track = g_opt.document->GetElementById(id + "-track");
            row.fill = g_opt.document->GetElementById(id + "-fill");
            row.shown_value.clear();
            row.shown_fill = -1.0f;
            row.shown_enabled = -1;
            if (row.element != nullptr)
            {
                g_opt.row_of[row.element] = {p, r};
            }
        }
    }
}

/* ---------------------------------------------------------------------------
 *  Values.
 * ------------------------------------------------------------------------ */

float SliderFraction(const Row &row, float value)
{
    if (row.logarithmic)
    {
        return (std::log(value) - std::log(row.min)) /
               (std::log(row.max) - std::log(row.min));
    }
    return (value - row.min) / (row.max - row.min);
}

float SliderFromFraction(const Row &row, float fraction)
{
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (row.logarithmic)
    {
        return std::exp(std::log(row.min) +
                        fraction * (std::log(row.max) - std::log(row.min)));
    }
    const float value = row.min + fraction * (row.max - row.min);
    return row.min + std::round((value - row.min) / row.step) * row.step;
}

void Changed(const Row &row)
{
    if (row.input_setting)
    {
        MioPanUi::NoteSettingsChanged();
    }
}

void SetSlider(Row &row, float value)
{
    value = std::clamp(value, row.min, row.max);
    if (value != row.get_value())
    {
        row.set_value(value);
        Changed(row);
    }
}

/* Left (-1) or right (+1).  Choices go round; sliders stop at their ends,
 * and the tick only sounds when something moved. */
void Adjust(Row &row, int direction)
{
    if (!row.IsEnabled())
    {
        return;
    }
    if (row.kind == RowKind::Choice)
    {
        const int count = row.Count();
        if (count <= 1)
        {
            return;
        }
        const int current = std::clamp(row.get(), 0, count - 1);
        row.set((current + direction + count) % count);
        Changed(row);
        MioPanRmlCommon::PlayCursor();
    }
    else if (row.kind == RowKind::Slider)
    {
        const float before = row.get_value();
        float value;
        if (row.logarithmic)
        {
            value = before * std::pow(row.step, (float)direction);
        }
        else
        {
            /* On the step grid, so repeated steps do not drift. */
            const float steps = std::round((before - row.min) / row.step);
            value = row.min + (steps + (float)direction) * row.step;
        }
        SetSlider(row, value);
        if (row.get_value() != before)
        {
            MioPanRmlCommon::PlayCursor();
        }
    }
}

void Activate(Row &row)
{
    if (!row.IsEnabled())
    {
        return;
    }
    if (row.kind == RowKind::Choice)
    {
        Adjust(row, 1);
    }
    else if (row.kind == RowKind::Action)
    {
        MioPanRmlCommon::PlayDecide();
        row.run();
        Changed(row);
    }
}

std::string ValueText(const Row &row)
{
    switch (row.kind)
    {
    case RowKind::Choice:
        return row.NameOf(std::clamp(row.get(), 0, std::max(row.Count() - 1, 0)));
    case RowKind::Slider:
        return row.format(row.get_value());
    case RowKind::Action:
        return row.action;
    default:
        return "";
    }
}

void RefreshRow(Row &row)
{
    if (row.element == nullptr || row.kind == RowKind::Heading)
    {
        return;
    }
    const int enabled = row.IsEnabled() ? 1 : 0;
    if (enabled != row.shown_enabled)
    {
        row.element->SetClass("disabled", enabled == 0);
        row.shown_enabled = enabled;
    }
    const std::string text = ValueText(row);
    if (row.value != nullptr && text != row.shown_value)
    {
        row.value->SetInnerRML(MioPanRmlCommon::Escape(text));
        row.shown_value = text;
    }
    if (row.fill != nullptr)
    {
        const float fraction =
            std::clamp(SliderFraction(row, row.get_value()), 0.0f, 1.0f);
        if (std::fabs(fraction - row.shown_fill) > 0.001f)
        {
            row.fill->SetProperty("width", Printf("%.2f%%", fraction * 100.0));
            row.shown_fill = fraction;
        }
    }
}

/* ---------------------------------------------------------------------------
 *  The cursor and the sections.
 * ------------------------------------------------------------------------ */

Page &CurrentPage()
{
    return g_opt.pages[g_opt.page];
}

Row *SelectedRow()
{
    Page &page = CurrentPage();
    if (page.selected < 0 || page.selected >= (int)page.rows.size())
    {
        return nullptr;
    }
    return &page.rows[page.selected];
}

void Select(int index, bool scroll)
{
    Page &page = CurrentPage();
    if (page.selected >= 0 && page.selected < (int)page.rows.size() &&
        page.rows[page.selected].element != nullptr)
    {
        page.rows[page.selected].element->SetClass("selected", false);
    }
    page.selected = index;
    if (index >= 0 && index < (int)page.rows.size() &&
        page.rows[index].element != nullptr)
    {
        Rml::Element *element = page.rows[index].element;
        element->SetClass("selected", true);
        if (scroll)
        {
            /* The heading above a section's first row comes into view with
             * it, so the cursor never sits under an unlabelled group. */
            Rml::Element *target = element;
            if (index > 0 && page.rows[index - 1].kind == RowKind::Heading &&
                page.rows[index - 1].element != nullptr)
            {
                target = page.rows[index - 1].element;
            }
            target->ScrollIntoView(Rml::ScrollAlignment::Nearest);
            element->ScrollIntoView(Rml::ScrollAlignment::Nearest);
        }
    }
}

int NextSelectable(const Page &page, int from, int direction)
{
    for (int i = from + direction; i >= 0 && i < (int)page.rows.size();
         i += direction)
    {
        if (page.rows[i].Selectable())
        {
            return i;
        }
    }
    return -1;
}

void MoveCursor(int direction)
{
    Page &page = CurrentPage();
    const int next = NextSelectable(page, page.selected, direction);
    if (next >= 0)
    {
        Select(next, true);
        MioPanRmlCommon::PlayCursor();
    }
}

void ShowPage(int index, bool sound)
{
    const int count = (int)g_opt.pages.size();
    index = (index % count + count) % count;
    for (int p = 0; p < count; p++)
    {
        Page &page = g_opt.pages[p];
        if (page.element != nullptr)
        {
            page.element->SetClass("hidden", p != index);
        }
        if (page.tab != nullptr)
        {
            page.tab->SetClass("active", p == index);
        }
    }
    g_opt.page = index;
    Page &page = CurrentPage();
    for (Row &row : page.rows)
    {
        RefreshRow(row);
    }
    int selected = page.selected;
    if (selected < 0 || !page.rows[selected].Selectable())
    {
        selected = NextSelectable(page, -1, 1);
    }
    Select(selected, true);
    if (sound)
    {
        MioPanRmlCommon::PlayCursor();
    }
}

void CloseByPlayer()
{
    MioPanRmlCommon::PlayCancel();
    g_opt.request = MIOPAN_RML_OPTIONS_REQ_CLOSE;
    MioPan_RmlOptionsClose();
}

/* ---------------------------------------------------------------------------
 *  Input.
 * ------------------------------------------------------------------------ */

void HandleKey(Rml::Event &event)
{
    const int key = event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool shift = event.GetParameter<int>("shift_key", 0) != 0;
    if (g_opt.confirm.HandleKey(key))
    {
        event.StopPropagation();
        return;
    }

    Row *row = SelectedRow();
    switch (key)
    {
    case Rml::Input::KI_UP: MoveCursor(-1); break;
    case Rml::Input::KI_DOWN: MoveCursor(1); break;
    case Rml::Input::KI_LEFT:
        if (row != nullptr)
        {
            Adjust(*row, -1);
        }
        break;
    case Rml::Input::KI_RIGHT:
        if (row != nullptr)
        {
            Adjust(*row, 1);
        }
        break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE:
        if (row != nullptr)
        {
            Activate(*row);
        }
        break;
    case Rml::Input::KI_HOME:
        Select(NextSelectable(CurrentPage(), -1, 1), true);
        break;
    case Rml::Input::KI_END:
        Select(NextSelectable(CurrentPage(), (int)CurrentPage().rows.size(), -1),
               true);
        break;
    case Rml::Input::KI_PRIOR:
    case Rml::Input::KI_Q: ShowPage(g_opt.page - 1, true); break;
    case Rml::Input::KI_NEXT:
    case Rml::Input::KI_E: ShowPage(g_opt.page + 1, true); break;
    case Rml::Input::KI_TAB: ShowPage(g_opt.page + (shift ? -1 : 1), true); break;
    case Rml::Input::KI_ESCAPE: CloseByPlayer(); break;
    default: return;
    }
    event.StopPropagation();
}

/* The row an event happened in, walking up from its target. */
Row *RowAt(Rml::Element *element, RowRef *ref_out)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        auto found = g_opt.row_of.find(element);
        if (found != g_opt.row_of.end())
        {
            if (ref_out != nullptr)
            {
                *ref_out = found->second;
            }
            return &g_opt.pages[found->second.page].rows[found->second.row];
        }
    }
    return nullptr;
}

bool HasAncestorClass(Rml::Element *element, const char *name, Rml::Element **found)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        if (element->IsClassSet(name))
        {
            if (found != nullptr)
            {
                *found = element;
            }
            return true;
        }
    }
    return false;
}

void DragTo(Row &row, float mouse_x)
{
    if (row.track == nullptr)
    {
        return;
    }
    const float left = row.track->GetAbsoluteOffset(Rml::BoxArea::Border).x;
    const float width = row.track->GetBox().GetSize(Rml::BoxArea::Border).x;
    if (width <= 0.0f)
    {
        return;
    }
    SetSlider(row, SliderFromFraction(row, (mouse_x - left) / width));
}

void HandlePointer(Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    const float mouse_x = event.GetParameter<float>("mouse_x", 0.0f);

    if (event == Rml::EventId::Mouseup)
    {
        g_opt.dragging = {-1, -1};
        return;
    }
    if (event == Rml::EventId::Mousemove)
    {
        if (g_opt.dragging.page == g_opt.page && g_opt.dragging.row >= 0)
        {
            DragTo(CurrentPage().rows[g_opt.dragging.row], mouse_x);
        }
        return;
    }
    if (g_opt.confirm.IsOpen())
    {
        return;
    }

    RowRef ref{-1, -1};
    Row *row = RowAt(target, &ref);
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && row != nullptr &&
            ref.page == g_opt.page && row->Selectable() &&
            ref.row != CurrentPage().selected)
        {
            Select(ref.row, false);
        }
        return;
    }
    if (event == Rml::EventId::Mousedown)
    {
        if (row != nullptr && row->kind == RowKind::Slider && row->IsEnabled() &&
            HasAncestorClass(target, "track", nullptr))
        {
            g_opt.dragging = ref;
            DragTo(*row, mouse_x);
        }
        return;
    }

    /* Click. */
    Rml::Element *tagged = nullptr;
    if (HasAncestorClass(target, "tab", &tagged))
    {
        const int index = std::atoi(tagged->GetId().c_str() + 4);
        if (index != g_opt.page)
        {
            ShowPage(index, true);
        }
        return;
    }
    if (HasAncestorClass(target, "prompt", &tagged))
    {
        const Rml::String &id = tagged->GetId();
        if (id == "prompt-back")
        {
            CloseByPlayer();
        }
        else if (id == "prompt-prev")
        {
            ShowPage(g_opt.page - 1, true);
        }
        else if (id == "prompt-next")
        {
            ShowPage(g_opt.page + 1, true);
        }
        return;
    }
    if (row == nullptr || ref.page != g_opt.page)
    {
        return;
    }
    if (HasAncestorClass(target, "arrow", &tagged))
    {
        Adjust(*row, tagged->IsClassSet("left") ? -1 : 1);
    }
    else if (row->kind != RowKind::Slider)
    {
        Activate(*row);
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    if (!g_opt.open || g_opt.hidden)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
    }
    else
    {
        HandlePointer(event);
    }
}

void RefreshHelpAndPrompts()
{
    std::string help;
    if (g_opt.confirm.IsOpen())
    {
        help.clear();
    }
    else if (const Row *row = SelectedRow())
    {
        help = row->help ? row->help() : "";
        if (!row->IsEnabled() && !help.empty())
        {
            help += " (Not available with the current settings.)";
        }
    }
    if (help != g_opt.shown_help && g_opt.help != nullptr)
    {
        g_opt.help->SetInnerRML(MioPanRmlCommon::Escape(help));
        g_opt.shown_help = help;
    }

    static const MioPanRmlCommon::Prompt kPrompts[] = {
        {"Q / E", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Section", "prompt-next",
         SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    std::string prompts = MioPanRmlCommon::PromptsRml(kPrompts, 3);
    if (prompts != g_opt.shown_prompts && g_opt.prompts != nullptr)
    {
        g_opt.prompts->SetInnerRML(prompts);
        g_opt.shown_prompts = prompts;
    }
}
}

namespace MioPanRmlOptions
{
bool Init(Rml::Context *context)
{
    g_opt.context = context;
    if (context == nullptr)
    {
        return false;
    }
    g_opt.document = context->LoadDocument(kDocumentPath);
    if (g_opt.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_opt.tabs = g_opt.document->GetElementById("tabs");
    g_opt.pages_root = g_opt.document->GetElementById("pages");
    g_opt.help = g_opt.document->GetElementById("help");
    g_opt.prompts = g_opt.document->GetElementById("prompts");
    if (g_opt.tabs == nullptr || g_opt.pages_root == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing #tabs or #pages", kDocumentPath);
        g_opt.document->Close();
        g_opt.document = nullptr;
        return false;
    }
    g_opt.confirm.Attach(g_opt.document, "confirm");
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousedown,
                            Rml::EventId::Mousemove, Rml::EventId::Mouseup})
    {
        g_opt.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_opt.confirm.Detach();
    /* The context owns the document and takes it down with it. */
    g_opt = OptionsState();
}

void Frame()
{
    if (!g_opt.open)
    {
        return;
    }
    if (g_opt.vibration_frames > 0)
    {
        VibrateRequest(0, 0xb4, 0xb4);
        g_opt.vibration_frames--;
    }
    if (g_opt.hidden)
    {
        return;
    }

    /* Every frame, because a setting can move under the screen -- Alt+Enter,
     * the overlay -- and one that enables another greys or frees it. */
    Page &page = CurrentPage();
    for (Row &row : page.rows)
    {
        RefreshRow(row);
    }
    if (const Row *row = SelectedRow(); row == nullptr || !row->Selectable())
    {
        const int next = NextSelectable(page, page.selected, 1);
        Select(next >= 0 ? next : NextSelectable(page, page.selected, -1), true);
    }
    RefreshHelpAndPrompts();
}
}

extern "C" {

int MioPan_RmlOptionsOpen(int from)
{
    if (g_opt.document == nullptr)
    {
        return 0;
    }
    g_opt.from = from;
    g_opt.request = MIOPAN_RML_OPTIONS_REQ_NONE;
    g_opt.hidden = false;
    g_opt.dragging = {-1, -1};
    g_opt.shown_help.clear();
    g_opt.shown_prompts.clear();
    g_opt.confirm.Close();
    /* Rebuilt on every open: what the pages hold depends on where the screen
     * was opened from, and on the hardware. */
    BuildPages();
    g_opt.open = true;
    g_opt.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
    ShowPage(0, false);
    RefreshHelpAndPrompts();
    MioPanRmlCommon::PlayDecide();
    return 1;
}

void MioPan_RmlOptionsClose(void)
{
    if (g_opt.document != nullptr)
    {
        g_opt.confirm.Close();
        g_opt.document->Hide();
    }
    g_opt.open = false;
    g_opt.hidden = false;
    g_opt.dragging = {-1, -1};
}

int MioPan_RmlOptionsIsOpen(void)
{
    return g_opt.open ? 1 : 0;
}

int MioPan_RmlOptionsConsumeRequest(void)
{
    const int request = g_opt.request;
    g_opt.request = MIOPAN_RML_OPTIONS_REQ_NONE;
    return request;
}

void MioPan_RmlOptionsSetHidden(int hidden)
{
    if (!g_opt.open || g_opt.document == nullptr || (hidden != 0) == g_opt.hidden)
    {
        return;
    }
    g_opt.hidden = hidden != 0;
    if (g_opt.hidden)
    {
        g_opt.document->Hide();
    }
    else
    {
        g_opt.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        ShowPage(g_opt.page, false);
    }
}
}
