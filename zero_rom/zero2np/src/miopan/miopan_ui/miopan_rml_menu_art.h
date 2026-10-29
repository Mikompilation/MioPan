#ifndef MIOPAN_RML_MENU_ART_H
#define MIOPAN_RML_MENU_ART_H

#include "miopan_rml_sprite.h"

#include "../../graphics/graph2d/g2d_draw.h"   /* SPRT_DAT */

#include <cstdint>
#include <string>

/*
 * The in-game menu pages are composed the way the game composes them: its own
 * plates, frames and icons where its sprite tables put them, and RmlUi's text
 * over them where the game prints.  The game draws its 2D into a 4:3 picture
 * of 640 x 448 pixels that are not square, centred in the window; a page's
 * stage is that picture, and these build its pieces as RML for it, in its
 * pixels.  menu.rcss gives .stage, .art and the text classes their look.
 */
namespace MioPanRmlMenuArt
{
/* dp per game pixel, across and down. */
struct Scale
{
    float x = 1.0f;
    float y = 1.0f;
};

/* The game's 2D picture in the RmlUi context right now: its scale, and the
 * style that places a stage over it.  The stage's font-size is 10 game
 * pixels down, so text sized in em follows the picture. */
Scale StageScale();
std::string StageStyle();

/* An <img> of `sprite` out of the pak at `pk2`, at the sprite's own x, y moved
 * by dx, dy, at its own size.  "" when the art is not there. */
std::string Art(const Scale &scale, uintptr_t pk2, const SPRT_DAT &sprite,
                int dx = 0, int dy = 0, int r = 0x80, int g = 0x80, int b = 0x80);
/* The same art stretched over x, y, w, h, as the game scales a frame --
 * turned or added as `look` asks. */
std::string ArtIn(const Scale &scale, uintptr_t pk2, const SPRT_DAT &sprite,
                  float x, float y, float w, float h);
std::string ArtIn(const Scale &scale, uintptr_t pk2, const SPRT_DAT &sprite,
                  float x, float y, float w, float h,
                  const MioPanRmlSprite::Look &look);
/* An <img> of `src` over x, y, w, h, at `opacity`. */
std::string ImageIn(const Scale &scale, const std::string &src, float x, float y,
                    float w, float h, float opacity = 1.0f);
/* The menu's gold rails, as MenuCmnLineYokoDisp() and MenuCmnLineTateDisp()
 * draw them: a cap, a stretched run and the other cap. */
std::string RailAcross(const Scale &scale, float x, float y, float w);
std::string RailDown(const Scale &scale, float x, float y, float h);
/* A list row's frame, lit under the cursor or dim, as MenuCmnSelFrameDisp()
 * and MenuCmnNonSelFrameDisp() draw it: a half and its mirror, each scaled
 * to half of `w`. */
std::string RowFrame(const Scale &scale, float x, float y, float w, bool lit);
/* DrawCmnWindow()'s window over x, y, w, h: its glass -- menu.rcss's
 * .menu-window, with `id` and `cls` on it when they are given -- and its gold
 * frame. */
std::string Window(const Scale &scale, float x, float y, float w, float h,
                   const char *id = nullptr, const char *cls = nullptr);
/* Decode the windows' frame art out of the boot's resident 2D pak while it
 * still holds it -- MioPan_RmlMenuArtKeepResident(), from the boot. */
void KeepResidentArt();
/* DrawCmnTwoLineWindow()'s: a band with rounded ends and a line above and
 * below. */
std::string TwoLineWindow(const Scale &scale, float x, float y, float w, float h);
/* Where the game opens the window a page with nothing in it shows its line
 * in -- "No items.", "No notes." -- message 0x36's own window, and where the
 * line starts in it. */
constexpr float kNoticeX = 145.0f;
constexpr float kNoticeY = 207.0f;
constexpr float kNoticeW = 353.0f;
constexpr float kNoticeH = 62.0f;
constexpr float kNoticeTextX = 175.0f;
constexpr float kNoticeTextY = 222.0f;
/* `text` (UTF-8, escaped here) in a box at x, y, `width` wide, with `cls` for
 * its look -- where the game prints a number or a line. */
std::string Text(const Scale &scale, const std::string &text, float x, float y,
                 float width, const char *cls);
/* Text() right-aligned in its box, as the game's digit runs are. */
std::string Number(const Scale &scale, const std::string &text, float x, float y,
                   float width, const char *cls);
}

extern "C" void MioPan_RmlMenuArtKeepResident(void);

#endif
