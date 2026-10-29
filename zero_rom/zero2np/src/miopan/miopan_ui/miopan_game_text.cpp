#include "miopan_game_text.h"

#include "../../graphics/graph2d/message.h"
#include "../../system/mc/prg/mc_check_card.h"

namespace
{
/*
 * The message font's glyphs, read off its atlas (vram_tex.pk2, the 512x256
 * picture fntdat[1] names: 21 cells of 24 to a row).  A plain byte is one of
 * these.  The digits are drawn twice, at 53 and at 63; 73..76 are the pad's
 * circle, cross, triangle and square; the kanji are the handful the Japanese
 * script borrows into this bank.  "" is a cell with nothing in it.
 */
constexpr const char *kBank1[] = {
    " ",  "A",  "B",  "C",  "D",  "E",  "F",  "G",  "H",  "I",  "J",  "K",
    "L",  "M",  "N",  "O",  "P",  "Q",  "R",  "S",  "T",  "U",  "V",  "W",
    "X",  "Y",  "Z",  "a",  "b",  "c",  "d",  "e",  "f",  "g",  "h",  "i",
    "j",  "k",  "l",  "m",  "n",  "o",  "p",  "q",  "r",  "s",  "t",  "u",
    "v",  "w",  "x",  "y",  "z",  "0",  "1",  "2",  "3",  "4",  "5",  "6",
    "7",  "8",  "9",  "0",  "1",  "2",  "3",  "4",  "5",  "6",  "7",  "8",
    "9",  "○", "✕", "△", "□", "(", ")", ",", "?", "!",
    "/",  "·", ":", "*", "~", "-", "’", ".", " ", "”",
    "追", "痺", "遅", "零", "視", "封", "滅",
    "刻", "連", "感", "瞬", "避", "獲", "換",
    "報", "測", "拡", "&", "II", "“", "!?",
    "À", "Á", "Â", "Ä", "È", "É", "Ê",
    "Ë", "Ì", "Í", "Î", "Ï", "Ò", "Ó",
    "Ô", "Ö", "Ù", "Ú", "Û", "Ü", "Ñ",
    "ß", "Œ", "Ch", "Ll",
    "à", "á", "â", "ä", "è", "é", "ê",
    "ë", "ì", "í", "î", "ï", "ò", "ó",
    "ô", "ö", "ù", "ú", "û", "ü", "rr",
    "(",  ")",  "¡", "¿", "…", ";", "「", "」",
    "✓", "×", "pts", "ç", "º", "œ", "○", "=",
    "„", "®", "ñ", "Ç", "+", "“", "”",
};
constexpr int kBank1Count = (int)(sizeof(kBank1) / sizeof(kBank1[0]));

/* The 0xf0 bank (fntdat[2]) is the Japanese script's; the European text
 * borrows only its full-width punctuation. */
const char *Bank2(unsigned glyph)
{
    switch (glyph)
    {
    case 35: return "?";
    case 36: return "!";
    case 37: return "/";
    case 38: return "·";
    case 39: return ":";
    case 166: return "…";
    default: return "";
    }
}
}

namespace MioPanGameText
{
std::string Message(int type, int id)
{
    return MessagePage(type, id, 0, false);
}

std::string MessagePage(int type, int id, int page, bool keep_breaks)
{
    if ((unsigned)type > 0x52 || id < 0 || id >= GetMsgIDNumMax(type))
    {
        return {};
    }
    const u_char *s = GetMsgDataAddr(type, id);
    if (s == nullptr)
    {
        return {};
    }

    std::string out;
    bool space = true;  /* no space at the start, none doubled */
    int at_page = 0;    /* the page `s` is in */

    /* The lengths are what PrintMsgOneLine()'s handlers step over when they
     * draw without a window, as these lines are drawn. */
    std::string number;
    for (int guard = 0; guard < 1024; guard++)
    {
        const u_char c = *s;
        const char *text;
        switch (c)
        {
        case 0xf0:
            text = Bank2(s[1]);
            s += 2;
            break;
        case 0xf1:
        case 0xf2:
        case 0xf3:
            text = "";
            s += 2;
            break;
        case 0xfd:          /* MesColExe: a colour */
            s += 2;
            continue;
        case 0xf7:          /* MesSetExe: a pen position */
            s += 5;
            continue;
        case 0xf8:          /* MesWinSizeExe: a window */
            s += 9;
            continue;
        case 0xf9:          /* MesArrangeExe */
            s += 3;
            continue;
        case 0xfc:          /* MesExNumExe: its only number, the card slot */
            number = std::to_string(GetAccessMemoryCardPort() + 1);
            text = number.c_str();
            s += 2;
            break;
        case 0xfa:
        case 0xfe:
            text = keep_breaks ? "\n" : " ";
            s += 1;
            break;
        case 0xfb:          /* the end of a page */
            if (at_page++ >= page)
            {
                guard = 1024;
            }
            s += 1;
            continue;
        case 0xff:
            guard = 1024;
            continue;
        default:
            text = c < kBank1Count ? kBank1[c] : "";
            s += 1;
            break;
        }

        if (at_page != page)
        {
            continue;
        }
        if (text[0] == '\n')
        {
            while (!out.empty() && out.back() == ' ')
            {
                out.pop_back();
            }
            out += '\n';
            space = true;
        }
        else if (text[0] == ' ' && text[1] == '\0')
        {
            if (!space)
            {
                out += ' ';
                space = true;
            }
        }
        else if (text[0] != '\0')
        {
            out += text;
            space = false;
        }
    }

    while (!out.empty() && (out.back() == ' ' || out.back() == '\n'))
    {
        out.pop_back();
    }
    return out;
}
}
