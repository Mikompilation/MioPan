#ifndef MIOPAN_RMLUI_H
#define MIOPAN_RMLUI_H

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>
#include <SDL3/SDL_video.h>

namespace Rml
{
class Context;
}

/*
 * RmlUi, the port's own menus.
 *
 * Dear ImGui stays the developer overlay; RmlUi is for what a player sees, laid
 * out in .rml/.rcss documents under resources/rml.  This is its backend: the
 * SDL_GPU renderer, the input bridge, and the one Rml::Context everything is
 * shown in.  The screens themselves live beside it (miopan_rml_*.cpp).
 *
 * The context covers the game picture -- the present rect, not the window --
 * so a menu is framed with the game whatever the window's aspect.  It is laid
 * out at the output's own resolution for sharp text, with one dp set to the
 * picture's scale over the ROM's 640x448: a document written in dp units is
 * written in PS2 screen coordinates.
 *
 * While any document is visible the UI has the controls: keys, the pad and
 * the mouse go to RmlUi and the game reads nothing (MioPan_InputSetMenu-
 * Suppressed), and that hold outlasts the document until every key and pad
 * button is up, so the press that closed a menu never lands in the game.
 *
 * Drawing follows MioPanUi's overlay.  Frame() runs RmlUi on the game thread
 * and records what it drew into a frame list; display-rate smoothing presents
 * on a thread of its own, so CaptureDrawData() copies that list into the frame
 * packet, and the presenter draws the copy.  Textures RmlUi releases are kept
 * until no packet that could still name them is left.
 */
namespace MioPanRml
{
/* The renderer hands over the two rmlui shaders it loaded; they are released
 * here from then on, whether or not Init() succeeds.  `format` is the colour
 * format of the target the UI pass draws into. */
bool Init(SDL_Window *window, SDL_GPUDevice *device, SDL_GPUTextureFormat format,
          SDL_GPUShader *vertex_shader, SDL_GPUShader *fragment_shader);
void Shutdown();

/* Every event SDL delivers.  Input only reaches RmlUi while it holds the
 * controls -- see WantsInput(). */
void ProcessEvent(const SDL_Event *event);

/* Once per logical frame, on the game thread: update the documents and record
 * what they draw. */
void Frame();

/* True while a document is visible and the UI has the controls. */
bool WantsInput();

Rml::Context *GetContext();

/*
 * The frame's draw list, for the UI pass.  `snapshot` is a packet's copy on
 * the presenting thread, or nullptr for the list Frame() last recorded.
 * PrepareDrawData() uploads outside any pass and returns false when there is
 * nothing to draw; RenderDrawData() then draws inside the pass, into a target
 * of `width` x `height` output pixels.
 */
struct DrawSnapshot;
bool PrepareDrawData(const DrawSnapshot *snapshot,
                     SDL_GPUCommandBuffer *command_buffer);
void RenderDrawData(const DrawSnapshot *snapshot,
                    SDL_GPUCommandBuffer *command_buffer,
                    SDL_GPURenderPass *render_pass, Uint32 width,
                    Uint32 height);

/* Copy the recorded list into `*snapshot`, allocating it on the first call and
 * reusing it after.  Game thread only. */
void CaptureDrawData(DrawSnapshot **snapshot);
void FreeDrawData(DrawSnapshot *snapshot);
}

#endif
