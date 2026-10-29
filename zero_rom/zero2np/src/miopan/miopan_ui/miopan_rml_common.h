#ifndef MIOPAN_RML_COMMON_H
#define MIOPAN_RML_COMMON_H

#include <RmlUi/Core/EventListener.h>
#include <SDL3/SDL_gamepad.h>

#include <functional>
#include <string>

namespace Rml
{
class Element;
class ElementDocument;
}

/* What the RmlUi screens share: the game's menu sounds, button prompts, and
 * the yes / no window.  The look they share is resources/rml/theme.rcss. */
namespace MioPanRmlCommon
{
/* The game's own menu sounds -- the cursor tick, the decide chime and the
 * cancel -- the same SystemBankPlay calls its option screen makes. */
void PlayCursor();
void PlayDecide();
void PlayCancel();

/* `text` made safe to put inside RML. */
std::string Escape(const std::string &text);

/* The pad's button icons in the game's own text (MioPanGameText) named for
 * whichever the player last used: the pad's own lettering, or on the
 * keyboard the keys these screens answer to -- Enter for cross, Esc for the
 * ways back. */
std::string NameButtonIcons(const std::string &text);

/* One button prompt: what the key is on the keyboard, which pad button it is
 * (and a second, for a pair like L1 / R1), and what it does.  `id` names the
 * prompt's element, so a screen can take a click on it as the press; it may
 * be null. */
struct Prompt
{
    const char *key;
    SDL_GamepadButton pad;
    const char *action;
    const char *id;
    SDL_GamepadButton pad_pair = SDL_GAMEPAD_BUTTON_INVALID;
    /* Shown for a pad instead of the buttons' names -- the triggers, which
     * are not buttons. */
    const char *pad_text = nullptr;
};

/* The prompts as RML, labelled for whichever the player used last: the pad,
 * in its own lettering, or the keyboard. */
std::string PromptsRml(const Prompt *prompts, int count);

/*
 * The yes / no window the game asks its questions in.  Its markup lives in the
 * document, so each screen can word and place it:
 *
 *   <div id="ID" class="confirm hidden"><div class="confirm-box">
 *     <p class="confirm-text"></p><p class="confirm-note"></p>
 *     <div class="confirm-choices">
 *       <span id="ID-yes" class="confirm-choice selectable">Yes</span>
 *       <span id="ID-no" class="confirm-choice selectable">No</span>
 *     </div></div></div>
 *
 * The cursor opens on No, as it does in the game's own windows; left and right
 * move it, Enter answers and Escape is No.  `on_no` is for a question the
 * game itself is waiting on an answer to; most only need the yes.
 */
class ConfirmDialog final : public Rml::EventListener
{
public:
    void Attach(Rml::ElementDocument *document, const char *id);
    void Detach();

    void Open(const std::string &text, const std::string &note,
              std::function<void()> on_yes, std::function<void()> on_no = nullptr);
    void Close();
    bool IsOpen() const;

    /* A key from the screen's own handler: true when the window took it,
     * which it does with every key while it is open. */
    bool HandleKey(int key);

    void ProcessEvent(Rml::Event &event) override;

private:
    void Select(bool yes);
    void Answer(bool yes);

    Rml::Element *root_ = nullptr;
    Rml::Element *text_ = nullptr;
    Rml::Element *note_ = nullptr;
    Rml::Element *yes_ = nullptr;
    Rml::Element *no_ = nullptr;
    bool yes_selected_ = false;
    std::function<void()> on_yes_;
    std::function<void()> on_no_;
};
}

#endif
