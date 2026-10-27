#include "miopan_rml_hello.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/hello_world.rml";

Rml::ElementDocument *g_document;

class HelloListener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override
    {
        if (event == Rml::EventId::Click ||
            (event == Rml::EventId::Keydown &&
             event.GetParameter<int>("key_identifier",
                                     Rml::Input::KI_UNKNOWN) ==
                 Rml::Input::KI_ESCAPE))
        {
            MioPanRmlHello::Hide();
        }
    }
};

HelloListener g_listener;
}

namespace MioPanRmlHello
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_document = context->LoadDocument(kDocumentPath);
    if (g_document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }

    if (Rml::Element *ok = g_document->GetElementById("ok"))
    {
        ok->AddEventListener(Rml::EventId::Click, &g_listener);
    }
    /* Escape and Circle, from wherever focus is. */
    g_document->AddEventListener(Rml::EventId::Keydown, &g_listener);
    return true;
}

void Shutdown()
{
    /* The context owns the document and takes it down with it. */
    g_document = nullptr;
}

void Show()
{
    if (g_document != nullptr)
    {
        /* Auto puts focus on the autofocus button, so Enter and Cross
         * answer it straight away. */
        g_document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Auto);
    }
}

void Hide()
{
    if (g_document != nullptr)
    {
        g_document->Hide();
    }
}

bool IsVisible()
{
    return g_document != nullptr && g_document->IsVisible();
}
}
