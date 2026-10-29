#ifndef MIOPAN_GAME_TEXT_H
#define MIOPAN_GAME_TEXT_H

#include <string>

/*
 * The game's own strings, as UTF-8, for the screens the port draws itself --
 * a chapter title, a room name, a memory-card message -- so they read exactly
 * as the game words them, in whichever language it runs.
 *
 * Message data is glyph codes in the game's font: one byte a glyph, or a bank
 * byte (0xf0..0xf3) and one.  The glyphs are mapped as the font's atlas draws
 * them (miopan_game_text.cpp).  Control codes are skipped and a line break
 * becomes a space.  The pad's button icons come out as the shapes on the
 * DUALSHOCK -- circle, cross, triangle, square, U+25CB U+2715 U+25B3 U+25A1 --
 * for a screen to name for the pad in use
 * (MioPanRmlCommon::NameButtonIcons()).
 */
namespace MioPanGameText
{
/* Message `id` of `type`, as GetMsgDataAddr() finds it; "" when there is
 * none.  Only its first page: a message the game pages (0xfb between pages)
 * is read a page at a time with MessagePage(). */
std::string Message(int type, int id);
/* Page `page` of it, from 0.  With `keep_breaks` the game's own line breaks
 * come out as '\n' rather than spaces, for text laid out line by line to fit
 * its window -- a note, a document. */
std::string MessagePage(int type, int id, int page, bool keep_breaks);
}

#endif
