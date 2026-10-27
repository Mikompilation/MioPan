#include "miopan_rml_common.h"
#include "miopan_rmlui.h"

#include "../../system/os/system.h"

#include <RmlUi/Core.h>

namespace
{
/* SystemBankPlay's menu bank: 0 the cursor, 1 cancel, 3 decide, at the level
 * and pitch outgame/option.c plays them. */
void PlayBank(int no)
{
    SystemBankPlay(no, 1, 0, 0, (SND_3D_SET *)nullptr, 0x3200, 0x1000);
}
}

namespace MioPanRmlCommon
{
void PlayCursor()
{
    PlayBank(0);
}

void PlayDecide()
{
    PlayBank(3);
}

void PlayCancel()
{
    PlayBank(1);
}

std::string Escape(const std::string &text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

std::string PromptsRml(const Prompt *prompts, int count)
{
    const bool pad = MioPanRml::UsingPad();
    std::string rml;
    for (int i = 0; i < count; i++)
    {
        const Prompt &prompt = prompts[i];
        rml += "<span class=\"prompt\"";
        if (prompt.id != nullptr)
        {
            rml += " id=\"";
            rml += prompt.id;
            rml += "\"";
        }
        std::string key = pad ? MioPanRml::PadButtonName(prompt.pad) : prompt.key;
        if (pad && prompt.pad_pair != SDL_GAMEPAD_BUTTON_INVALID)
        {
            key += " / ";
            key += MioPanRml::PadButtonName(prompt.pad_pair);
        }
        rml += "><span class=\"key\">";
        rml += Escape(key);
        rml += "</span>";
        rml += Escape(prompt.action);
        rml += "</span>";
    }
    return rml;
}

void ConfirmDialog::Attach(Rml::ElementDocument *document, const char *id)
{
    Detach();
    if (document == nullptr)
    {
        return;
    }
    root_ = document->GetElementById(id);
    const std::string base = id;
    yes_ = document->GetElementById(base + "-yes");
    no_ = document->GetElementById(base + "-no");
    if (root_ == nullptr || yes_ == nullptr || no_ == nullptr)
    {
        root_ = yes_ = no_ = nullptr;
        return;
    }
    Rml::ElementList found;
    root_->GetElementsByClassName(found, "confirm-text");
    text_ = found.empty() ? nullptr : found[0];
    found.clear();
    root_->GetElementsByClassName(found, "confirm-note");
    note_ = found.empty() ? nullptr : found[0];

    for (Rml::Element *choice : {yes_, no_})
    {
        choice->AddEventListener(Rml::EventId::Click, this);
        choice->AddEventListener(Rml::EventId::Mouseover, this);
    }
}

void ConfirmDialog::Detach()
{
    for (Rml::Element *choice : {yes_, no_})
    {
        if (choice != nullptr)
        {
            choice->RemoveEventListener(Rml::EventId::Click, this);
            choice->RemoveEventListener(Rml::EventId::Mouseover, this);
        }
    }
    root_ = text_ = note_ = yes_ = no_ = nullptr;
    on_yes_ = nullptr;
}

void ConfirmDialog::Open(const std::string &text, const std::string &note,
                         std::function<void()> on_yes)
{
    if (root_ == nullptr)
    {
        return;
    }
    if (text_ != nullptr)
    {
        text_->SetInnerRML(Escape(text));
    }
    if (note_ != nullptr)
    {
        note_->SetInnerRML(Escape(note));
        note_->SetClass("hidden", note.empty());
    }
    on_yes_ = std::move(on_yes);
    Select(false);
    root_->SetClass("hidden", false);
}

void ConfirmDialog::Close()
{
    if (root_ != nullptr)
    {
        root_->SetClass("hidden", true);
    }
    on_yes_ = nullptr;
}

bool ConfirmDialog::IsOpen() const
{
    return root_ != nullptr && !root_->IsClassSet("hidden");
}

bool ConfirmDialog::HandleKey(int key)
{
    if (!IsOpen())
    {
        return false;
    }
    switch (key)
    {
    case Rml::Input::KI_LEFT:
    case Rml::Input::KI_RIGHT:
        Select(!yes_selected_);
        PlayCursor();
        break;
    case Rml::Input::KI_RETURN:
    case Rml::Input::KI_NUMPADENTER:
    case Rml::Input::KI_SPACE:
        Answer(yes_selected_);
        break;
    case Rml::Input::KI_ESCAPE:
        Answer(false);
        break;
    default:
        break;
    }
    return true;
}

void ConfirmDialog::ProcessEvent(Rml::Event &event)
{
    if (!IsOpen())
    {
        return;
    }
    const bool on_yes = event.GetCurrentElement() == yes_;
    if (event == Rml::EventId::Mouseover)
    {
        if (MioPanRml::PointerActive() && on_yes != yes_selected_)
        {
            Select(on_yes);
        }
    }
    else if (event == Rml::EventId::Click)
    {
        Answer(on_yes);
    }
}

void ConfirmDialog::Select(bool yes)
{
    yes_selected_ = yes;
    yes_->SetClass("selected", yes);
    no_->SetClass("selected", !yes);
}

void ConfirmDialog::Answer(bool yes)
{
    std::function<void()> on_yes = std::move(on_yes_);
    Close();
    if (yes)
    {
        PlayDecide();
        if (on_yes)
        {
            on_yes();
        }
    }
    else
    {
        PlayCancel();
    }
}
}
