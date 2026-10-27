#ifndef MIOPAN_RML_HELLO_H
#define MIOPAN_RML_HELLO_H

namespace Rml
{
class Context;
}

/* The first RmlUi screen: a modal "Hello, world" shown once at boot, which
 * proves the backend end to end -- fonts, layout, the GPU pass, mouse, keys
 * and the pad.  OK (click, Enter, Cross) or Escape / Circle closes it.
 * resources/rml/hello_world.rml. */
namespace MioPanRmlHello
{
bool Init(Rml::Context *context);
void Shutdown();
void Show();
void Hide();
bool IsVisible();
}

#endif
