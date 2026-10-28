#ifndef MIOPAN_RML_MENU_STATUS_H
#define MIOPAN_RML_MENU_STATUS_H

#include "miopan_rml_menu_art.h"

#include <string>

/*
 * The player's status plate as the in-game menu shows it -- the hub and the
 * item page both carry it, where the game draws MenuPlyrDataDisp(): the
 * plate's own frame, Mio's portrait in its autumn leaves, the health bar, the
 * Power-Up Lenses fitted with their levels, and the film loaded with how much
 * is left.  The art is the game's (miopan_rml_sprite.h), placed at the
 * offsets MenuPlyrDataDisp() gives each piece; the numbers are RmlUi's own
 * text where the game prints its digits.  The look is menu.rcss's .plate.
 */
namespace MioPanRmlMenuStatus
{
/* The plate's contents, as RML to put inside an absolutely placed
 * <div class="plate"> at the plate's origin -- MenuPlyrDataDisp()'s x, y --
 * on a stage of `scale`.  The same state gives the same string, so a screen
 * can rebuild only when it changes. */
std::string Rml(const MioPanRmlMenuArt::Scale &scale);
}

#endif
