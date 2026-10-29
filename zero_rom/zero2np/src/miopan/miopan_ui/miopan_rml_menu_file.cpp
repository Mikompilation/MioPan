#include "miopan_rml_menu_file.h"
#include "miopan_game_text.h"
#include "miopan_rml_common.h"
#include "miopan_rml_menu_art.h"
#include "miopan_rml_sprite.h"
#include "miopan_rmlui.h"

#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_file_dat.h"

#include <RmlUi/Core.h>

#include <SDL3/SDL_log.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
constexpr const char *kDocumentPath = "resources/rml/menu_file.rml";

using MioPanRmlMenuArt::ArtIn;
using MioPanRmlMenuArt::Scale;
using MioPanRmlMenuArt::Text;

constexpr int kRows = MIOPAN_RML_MENU_FILE_ROWS;

/* A file owns three messages in its tab's bank: its name, its line under
 * the list and its body.  Bank 8 entry 1 is the reader's slash; 0x36 entry 2
 * the empty page's line. */
constexpr int kMsgPerFile = 3;
constexpr int kMsgName = 0;
constexpr int kMsgLine = 1;
constexpr int kMsgBody = 2;
constexpr int kMsgEmpty = 0x36;
constexpr int kMsgEmptyFile = 2;

/* menu_file_tex[]: the title's plates; the list's window, tabs, lens, tab
 * arrows, dim and lit rows, unread brackets, scrollbar and its rail, and the
 * picture; the reader's window and page arrows; the photograph viewer's
 * window, picture and thumbnails; the map viewer's. */
constexpr int kTitlePlates[2] = {1, 2};
constexpr int kListWindowFirst = 3;
constexpr int kListWindowCount = 8;
constexpr int kTabFirst = 11;
constexpr int kLens = 16;
constexpr int kTabArrows[2] = {17, 18};
constexpr int kDim[4] = {19, 20, 21, 22};
constexpr int kDimMiddles = 20;
constexpr int kLit[3] = {23, 24, 25};
constexpr int kLitMiddles = 18;
constexpr int kUnread[2] = {26, 27};
constexpr int kThumbMarks[2] = {28, 29};
constexpr int kThumbTop = 30;
constexpr int kThumbMiddle = 31;
constexpr int kThumbBottom = 32;
constexpr int kRailTop = 33;
constexpr int kRailBottom = 34;
constexpr int kRailMiddle = 35;
constexpr int kRailEnd = 36;
constexpr int kRailMiddles = 12;
constexpr int kListPicture = 37;
constexpr int kReaderWindowFirst = 38;
constexpr int kReaderWindowCount = 7;
constexpr int kPageArrows[2] = {45, 46};
constexpr int kPageArrowPlates[2] = {47, 48};
constexpr int kPhotoWindowFirst = 49;
constexpr int kPhotoWindowCount = 3;
constexpr int kPhotoPicture = 52;
constexpr int kPhotoThumbs[2] = {53, 54};
constexpr int kMapWindowFirst = 55;
constexpr int kMapWindowCount = 13;
constexpr int kMapArrows[2] = {68, 69};
constexpr int kMapArrowGlows[2] = {70, 71};
constexpr int kMapPicture = 72;
constexpr int kMapThumbs[2] = {73, 74};

/* menu_file.c's layout: the rows 35 apart and their names, the brackets 280
 * apart; the scrollbar's 180 from 129; the list's and the reader's black
 * panels; the window under the list and its line; the reader's title, body
 * and page count; the viewers' page arrows and titles. */
constexpr int kRowPitch = 35;
constexpr int kNameX = 95;
constexpr int kNameY = 115;
constexpr int kUnreadSpan = 280;
constexpr int kScrollRun = 180;
constexpr int kScrollTop = 129;
constexpr int kWindowX = 24;
constexpr int kWindowY = 346;
constexpr int kWindowW = 592;
constexpr int kWindowH = 100;
constexpr int kLineX = 48;
constexpr int kLineY = 366;

class Listener final : public Rml::EventListener
{
public:
    void ProcessEvent(Rml::Event &event) override;
};

struct MenuFileState
{
    Rml::ElementDocument *document = nullptr;
    Rml::Element *stage = nullptr;
    Rml::Element *mode = nullptr;
    Rml::Element *prompts = nullptr;
    std::string shown_stage;
    std::string shown_mode;
    std::string shown_prompts;
    float shown_mode_alpha = -1.0f;

    Scale scale;
    MIOPAN_RML_MENU_FILE page = {};

    bool visible = false;
    bool shown_last_frame = false;
    bool requested = false;
    int command = MIOPAN_RML_MENU_FILE_NONE;
    int command_arg = 0;
};

MenuFileState g_file;
Listener g_listener;

void SetRml(Rml::Element *element, std::string &shown, const std::string &rml)
{
    if (element != nullptr && rml != shown)
    {
        element->SetInnerRML(rml);
        shown = rml;
    }
}

std::string Place(float x, float y, float w, float h)
{
    char style[128];
    std::snprintf(style, sizeof(style),
                  "left: %.2fdp; top: %.2fdp; width: %.2fdp; height: %.2fdp;",
                  x * g_file.scale.x, y * g_file.scale.y, w * g_file.scale.x,
                  h * g_file.scale.y);
    return style;
}

/* A message of the tab's bank, by file and which of its three. */
std::string FileText(int file, int which)
{
    return MioPanRmlCommon::NameButtonIcons(
        MioPanGameText::Message(g_file.page.bank, file * kMsgPerFile + which));
}

/* A message in the game's own lines, for a <p>. */
std::string Lines(const std::string &text)
{
    std::string lines;
    for (size_t start = 0; start <= text.size();)
    {
        const size_t end = text.find('\n', start);
        lines += MioPanRmlCommon::Escape(
            text.substr(start, end == std::string::npos ? std::string::npos : end - start));
        if (end == std::string::npos)
        {
            break;
        }
        lines += "<br/>";
        start = end + 1;
    }
    return lines;
}

/* A piece of the page's art at its own place or at x, y, w, h, out of the
 * first of the mode's paks that holds it -- as each part sends its own pak
 * before it draws. */
std::string Piece(const SPRT_DAT &sprite, float x, float y, float w, float h,
                  const MioPanRmlSprite::Look &look = MioPanRmlSprite::Look())
{
    const MIOPAN_RML_MENU_FILE &page = g_file.page;
    const void *paks[3] = {page.pak_view, page.pak_top, page.pak_cmn};
    if (page.mode == MIOPAN_RML_MENU_FILE_LIST)
    {
        paks[0] = page.pak_top;
        paks[1] = page.pak_cmn;
        paks[2] = nullptr;
    }
    for (const void *pak : paks)
    {
        if (pak == nullptr)
        {
            continue;
        }
        const std::string src = MioPanRmlSprite::Image((uintptr_t)pak, sprite, look);
        if (!src.empty())
        {
            return MioPanRmlMenuArt::ImageIn(g_file.scale, src, x, y, w, h);
        }
    }
    return std::string();
}

std::string Piece(const SPRT_DAT &sprite)
{
    return Piece(sprite, sprite.x, sprite.y, sprite.w, sprite.h);
}

std::string Added(const SPRT_DAT &sprite, float x, float y, float w, float h)
{
    MioPanRmlSprite::Look added;
    added.additive = true;
    return Piece(sprite, x, y, w, h, added);
}

/* One of the game's flat squares, black at its alpha. */
std::string Square(float x, float y, float w, float h, int alpha)
{
    char style[64];
    std::snprintf(style, sizeof(style), " background-color: #000000%02x;",
                  alpha * 2 > 255 ? 255 : alpha * 2);
    return "<div class=\"file-square\" style=\"" + Place(x, y, w, h) + style + "\"></div>";
}

/* The cross-fade's pictures over sprite `place`'s spot, out of the part of
 * each file it samples. */
std::string Pictures(const SPRT_DAT &place)
{
    std::string rml;
    for (int i = 0; i < 2; i++)
    {
        const int alpha = g_file.page.picture_alpha[i];
        if (g_file.page.picture[i] == nullptr || alpha <= 0)
        {
            continue;
        }
        rml += MioPanRmlMenuArt::ImageIn(
            g_file.scale,
            MioPanRmlSprite::Picture(g_file.page.picture[i], place.u, place.v, place.w,
                                     place.h),
            place.x, place.y, place.w, place.h, (float)(alpha > 0x80 ? 0x80 : alpha) / 128.0f);
    }
    return rml;
}

/* A page arrow: its soft plate pulsing behind the crisp arrow. */
std::string PageArrow(int side, float x, float y)
{
    static const int kInset[2][2] = {{4, 5}, {6, 5}};
    const SPRT_DAT &plate = menu_file_tex[kPageArrowPlates[side]];
    const SPRT_DAT &arrow = menu_file_tex[kPageArrows[side]];
    return "<div id=\"page-" + std::to_string(side) + "\" class=\"file-arrow\" style=\"" +
           Place(x, y, plate.w, plate.h) + "\"><div class=\"layer pulse\">" +
           Piece(plate, 0, 0, plate.w, plate.h) + "</div>" +
           Piece(arrow, (float)kInset[side][0], (float)kInset[side][1], arrow.w, arrow.h) +
           "</div>";
}

/* The window under the list or a viewer with the file's line in it. */
std::string LineWindow()
{
    return MioPanRmlMenuArt::Window(g_file.scale, kWindowX, kWindowY, kWindowW, kWindowH) +
           "<p class=\"caption-text\" style=\"" + Place(kLineX, kLineY, 544, 70) + "\">" +
           Lines(MioPanRmlCommon::NameButtonIcons(MioPanGameText::MessagePage(
               g_file.page.bank, g_file.page.selected * kMsgPerFile + kMsgLine, 0, true))) +
           "</p>";
}

/* A viewer's title, centred as PrintMsg_Arrange() centres it on x 321. */
std::string Title(float y)
{
    return Text(g_file.scale, FileText(g_file.page.selected, kMsgName), 121, y, 400,
                "file-title");
}

/* The list: its picture under the window, the black panel, the window and
 * its tabs, the scrollbar, the lens over the picture, the tab arrows, the
 * rows, and the line under them, in MenuFileTopDisp()'s order. */
std::string List()
{
    const MIOPAN_RML_MENU_FILE &page = g_file.page;
    std::string rml = Pictures(menu_file_tex[kListPicture]);
    rml += Square(41, 97, 349, 243, 0x59);
    for (int i = 0; i < kListWindowCount; i++)
    {
        rml += Piece(menu_file_tex[kListWindowFirst + i]);
    }

    /* The rail: its cap, twelve tiles, the end and the bottom cap. */
    rml += Piece(menu_file_tex[kRailTop]);
    const SPRT_DAT &rail = menu_file_tex[kRailMiddle];
    for (int i = 0; i < kRailMiddles; i++)
    {
        rml += Piece(rail, rail.x, (float)(rail.y + rail.h * i), rail.w, rail.h);
    }
    rml += Piece(menu_file_tex[kRailEnd]);
    rml += Piece(menu_file_tex[kRailBottom]);

    /* The thumb, in whole tiles and a part one as MenuFileTopScrollDisp()
     * builds it, and the two marks at the rail's ends, pulsing. */
    int size = kScrollRun;
    int y = kScrollTop;
    if (page.count >= kRows)
    {
        size = kScrollRun - kScrollRun / page.count * (page.count - kRows);
        y = kScrollRun / page.count * page.start + kScrollTop;
    }
    const SPRT_DAT &top = menu_file_tex[kThumbTop];
    const SPRT_DAT &middle = menu_file_tex[kThumbMiddle];
    const SPRT_DAT &bottom = menu_file_tex[kThumbBottom];
    int centre = size - (top.h + bottom.h);
    if (centre >= 0)
    {
        rml += Piece(top, top.x, (float)y, top.w, top.h);
        y += top.h;
        while (centre - middle.h >= 0)
        {
            centre -= middle.h;
            rml += Piece(middle, middle.x, (float)y, middle.w, middle.h);
            y += middle.h;
        }
        rml += Piece(middle, middle.x, (float)y, middle.w, (float)centre);
        y += centre;
        rml += Piece(bottom, bottom.x, (float)y, bottom.w, bottom.h);
    }
    else
    {
        rml += Piece(top, top.x, (float)y, top.w, (float)(size / 2));
        rml += Piece(bottom, bottom.x, (float)(y + size / 2), bottom.w, (float)(size / 2));
    }
    rml += "<div class=\"layer pulse\">";
    for (const int mark : kThumbMarks)
    {
        rml += Piece(menu_file_tex[mark]);
    }
    rml += "</div>";

    /* The magnifier at twice its size, added to the picture under it; the
     * tab on top; the arrows either side of the tabs. */
    const SPRT_DAT &lens = menu_file_tex[kLens];
    rml += Added(lens, lens.x, lens.y, lens.w * 2.0f, lens.h * 2.0f);
    rml += Piece(menu_file_tex[kTabFirst + page.tab]);
    for (int i = 0; i < 2; i++)
    {
        const SPRT_DAT &arrow = menu_file_tex[kTabArrows[i]];
        rml += "<div id=\"tab-arrow-" + std::to_string(i) +
               "\" class=\"scroll-arrow\" style=\"" +
               Place(arrow.x, arrow.y, arrow.w, arrow.h) + "\">" +
               Piece(arrow, 0, 0, arrow.w, arrow.h) + "</div>";
    }
    for (int i = 0; i < MIOPAN_RML_MENU_FILE_TABS; i++)
    {
        const SPRT_DAT &tab = menu_file_tex[kTabFirst + i];
        rml += "<div id=\"tab-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(tab.x, tab.y, tab.w, tab.h) + "\"></div>";
    }

    /* The rows: a lit frame -- a cap, eighteen middles and the other cap --
     * or a dim one of twenty, a bracket either end of an unread file, and
     * its name. */
    const int shown = page.count < kRows ? page.count : kRows;
    for (int i = 0; i < shown; i++)
    {
        const bool lit = i == page.cursor;
        const float dy = (float)(i * kRowPitch);
        if (lit)
        {
            const SPRT_DAT &left = menu_file_tex[kLit[0]];
            const SPRT_DAT &run = menu_file_tex[kLit[1]];
            float x = left.x;
            rml += Piece(left, x, left.y + dy, left.w, left.h);
            x += left.w;
            for (int j = 0; j < kLitMiddles; j++, x += run.w)
            {
                rml += Piece(run, x, left.y + dy, run.w, run.h);
            }
            rml += Piece(menu_file_tex[kLit[2]], x, left.y + dy, menu_file_tex[kLit[2]].w,
                         menu_file_tex[kLit[2]].h);
        }
        else
        {
            const SPRT_DAT &left = menu_file_tex[kDim[0]];
            const SPRT_DAT &run = menu_file_tex[kDim[1]];
            float x = left.x;
            rml += Piece(left, x, left.y + dy, left.w, left.h);
            x += left.w;
            for (int j = 0; j < kDimMiddles; j++, x += run.w)
            {
                rml += Piece(run, x, left.y + dy, run.w, run.h);
            }
            for (int j = 2; j < 4; j++)
            {
                const SPRT_DAT &end = menu_file_tex[kDim[j]];
                rml += Piece(end, x, left.y + dy, end.w, end.h);
                x += end.w;
            }
        }
        if (page.unread[i] != 0)
        {
            for (int j = 0; j < 2; j++)
            {
                const SPRT_DAT &mark = menu_file_tex[kUnread[j]];
                rml += Piece(mark, (float)(menu_file_tex[kUnread[0]].x + j * kUnreadSpan),
                             mark.y + dy, mark.w, mark.h);
            }
        }
        rml += Text(g_file.scale, FileText(page.file[i], kMsgName), kNameX, kNameY + dy, 260,
                    lit ? "row-text lit" : "row-text");
        rml += "<div id=\"row-" + std::to_string(i) + "\" class=\"row-hit\" style=\"" +
               Place(73, 116 + dy, 260, kRowPitch) + "\"></div>";
    }
    if (page.caption != 0)
    {
        rml += LineWindow();
    }
    return rml;
}

/* The reader: its black panel and window, the page arrows of a file that
 * has more than one page, the title over its shadow, this page in the game's
 * own lines, and "n / total". */
std::string Document()
{
    const MIOPAN_RML_MENU_FILE &page = g_file.page;
    std::string rml = Square(70, 121, 500, 272, 0x59);
    for (int i = 0; i < kReaderWindowCount; i++)
    {
        rml += Piece(menu_file_tex[kReaderWindowFirst + i]);
    }
    if (page.pages > 1)
    {
        rml += PageArrow(0, 16, 243);
        rml += PageArrow(1, 595, 243);
    }
    rml += Title(58);
    rml += "<p class=\"file-body\" style=\"" + Place(80, 134, 480, 250) + "\">" +
           Lines(MioPanRmlCommon::NameButtonIcons(MioPanGameText::MessagePage(
               page.bank, page.selected * kMsgPerFile + kMsgBody, page.page, true))) +
           "</p>";
    char count[48];
    std::snprintf(count, sizeof(count), "%d / %d", page.page + 1,
                  page.pages > 0 ? page.pages : 1);
    rml += Text(g_file.scale, count, 220, 388, 200, "file-count");
    return rml;
}

/* A viewer's thumbnail of a neighbour, out of the thumbnails' pak, the one
 * picture of it PK2SendVramOne() would send. */
std::string Thumb(const SPRT_DAT &place, int file)
{
    if (file < 0 || g_file.page.pak_small == nullptr)
    {
        return std::string();
    }
    MioPanRmlSprite::Look one;
    one.picture = file;
    return MioPanRmlMenuArt::ImageIn(
        g_file.scale, MioPanRmlSprite::Image((uintptr_t)g_file.page.pak_small, place, one),
        place.x, place.y, place.w, place.h);
}

/* The photograph or the map viewer: its window, the neighbours' thumbnails,
 * the picture, the arrows of a list of more than one, the line window and
 * the title. */
std::string Viewer(bool map)
{
    const MIOPAN_RML_MENU_FILE &page = g_file.page;
    std::string rml;
    const int first = map ? kMapWindowFirst : kPhotoWindowFirst;
    const int count = map ? kMapWindowCount : kPhotoWindowCount;
    for (int i = 0; i < count; i++)
    {
        rml += Piece(menu_file_tex[first + i]);
    }
    const int *thumbs = map ? kMapThumbs : kPhotoThumbs;
    rml += Thumb(menu_file_tex[thumbs[0]], page.prev_file);
    rml += Thumb(menu_file_tex[thumbs[1]], page.next_file);
    rml += Pictures(menu_file_tex[map ? kMapPicture : kPhotoPicture]);
    if (page.count > 1)
    {
        if (map)
        {
            for (int i = 0; i < 2; i++)
            {
                const SPRT_DAT &glow = menu_file_tex[kMapArrowGlows[i]];
                const SPRT_DAT &arrow = menu_file_tex[kMapArrows[i]];
                rml += "<div id=\"page-" + std::to_string(i) +
                       "\" class=\"file-arrow\" style=\"" +
                       Place(glow.x, glow.y, glow.w, glow.h) + "\"><div class=\"layer pulse\">" +
                       Piece(glow, 0, 0, glow.w, glow.h) + "</div>" +
                       Piece(arrow, (float)(arrow.x - glow.x), (float)(arrow.y - glow.y),
                             arrow.w, arrow.h) +
                       "</div>";
            }
        }
        else
        {
            rml += PageArrow(0, 27, 210);
            rml += PageArrow(1, 584, 210);
        }
    }
    rml += LineWindow();
    rml += Title(map ? 55.0f : 57.0f);
    return rml;
}

std::string Empty()
{
    using namespace MioPanRmlMenuArt;
    return Window(g_file.scale, kNoticeX, kNoticeY, kNoticeW, kNoticeH) +
           Text(g_file.scale,
                MioPanRmlCommon::NameButtonIcons(MioPanGameText::Message(kMsgEmpty,
                                                                         kMsgEmptyFile)),
                kNoticeTextX, kNoticeTextY, kNoticeW - (kNoticeTextX - kNoticeX),
                "window-text");
}

std::string Mode()
{
    switch (g_file.page.mode)
    {
    case MIOPAN_RML_MENU_FILE_LIST: return List();
    case MIOPAN_RML_MENU_FILE_DOCUMENT: return Document();
    case MIOPAN_RML_MENU_FILE_PHOTO: return Viewer(false);
    case MIOPAN_RML_MENU_FILE_MAP: return Viewer(true);
    default: return Empty();
    }
}

/* The title and the layer the modes draw on, under the hints. */
std::string Stage()
{
    const Scale &scale = g_file.scale;
    std::string rml;
    for (const int plate : kTitlePlates)
    {
        rml += MioPanRmlMenuArt::Art(scale, MENU_BG_TEX_ADRS, menu_file_tex[plate]);
    }
    rml += Text(scale, "File", 41, 20, 120, "menu-title");
    rml += "<div id=\"mode\" class=\"layer\"></div>";
    rml += "<div id=\"prompts\" class=\"menu-prompts\" style=\"" + Place(250, 30, 380, 30) +
           "\"></div>";
    return rml;
}

void RefreshPrompts()
{
    static const MioPanRmlCommon::Prompt kList[] = {
        {"Enter", SDL_GAMEPAD_BUTTON_SOUTH, "Select", nullptr},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
        {"Left / Right", SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, "Change Tab", "prompt-tab",
         SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    };
    static const MioPanRmlCommon::Prompt kPaged[] = {
        {"Left / Right", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Turn Page", "prompt-page",
         SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kBrowse[] = {
        {"Left / Right", SDL_GAMEPAD_BUTTON_DPAD_LEFT, "Browse", "prompt-page",
         SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    static const MioPanRmlCommon::Prompt kBack[] = {
        {"Esc", SDL_GAMEPAD_BUTTON_NORTH, "Back", "prompt-back"},
    };
    const MIOPAN_RML_MENU_FILE &page = g_file.page;
    std::string prompts;
    if (!page.closing)
    {
        switch (page.mode)
        {
        case MIOPAN_RML_MENU_FILE_LIST:
            prompts = MioPanRmlCommon::PromptsRml(kList, 3);
            break;
        case MIOPAN_RML_MENU_FILE_DOCUMENT:
            prompts = page.pages > 1 ? MioPanRmlCommon::PromptsRml(kPaged, 2)
                                     : MioPanRmlCommon::PromptsRml(kBack, 1);
            break;
        case MIOPAN_RML_MENU_FILE_PHOTO:
        case MIOPAN_RML_MENU_FILE_MAP:
            prompts = page.count > 1 ? MioPanRmlCommon::PromptsRml(kBrowse, 2)
                                     : MioPanRmlCommon::PromptsRml(kBack, 1);
            break;
        default:
            prompts = MioPanRmlCommon::PromptsRml(kBack, 1);
            break;
        }
    }
    SetRml(g_file.prompts, g_file.shown_prompts, prompts);
}

void BuildStage()
{
    const std::string rml = Stage();
    if (rml == g_file.shown_stage)
    {
        return;
    }
    g_file.stage->SetInnerRML(rml);
    g_file.shown_stage = rml;
    g_file.mode = g_file.document->GetElementById("mode");
    g_file.prompts = g_file.document->GetElementById("prompts");
    g_file.shown_mode.clear();
    g_file.shown_prompts.clear();
    g_file.shown_mode_alpha = -1.0f;
}

void Refresh()
{
    const Scale scale = MioPanRmlMenuArt::StageScale();
    if (scale.x != g_file.scale.x || scale.y != g_file.scale.y)
    {
        g_file.scale = scale;
        g_file.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_file.shown_stage.clear();
    }
    BuildStage();
    SetRml(g_file.mode, g_file.shown_mode, Mode());
    /* The list and the viewers fade into each other on the mode's own
     * fade. */
    const int alpha = g_file.page.mode_alpha > 0x80 ? 0x80 : g_file.page.mode_alpha;
    const float opacity = (float)(alpha < 0 ? 0 : alpha) / 128.0f;
    if (opacity != g_file.shown_mode_alpha && g_file.mode != nullptr)
    {
        char style[32];
        std::snprintf(style, sizeof(style), "opacity: %.3f;", opacity);
        g_file.mode->SetAttribute("style", style);
        g_file.shown_mode_alpha = opacity;
    }
    RefreshPrompts();
}

void Queue(int command, int arg = 0)
{
    g_file.command = command;
    g_file.command_arg = arg;
}

void HandleKey(Rml::Event &event)
{
    const int key =
        event.GetParameter<int>("key_identifier", Rml::Input::KI_UNKNOWN);
    const bool enter = key == Rml::Input::KI_RETURN ||
                       key == Rml::Input::KI_NUMPADENTER || key == Rml::Input::KI_SPACE;
    const bool left = key == Rml::Input::KI_LEFT || key == Rml::Input::KI_PRIOR;
    const bool right = key == Rml::Input::KI_RIGHT || key == Rml::Input::KI_NEXT;
    if (key == Rml::Input::KI_ESCAPE)
    {
        Queue(MIOPAN_RML_MENU_FILE_BACK);
    }
    else if (g_file.page.mode == MIOPAN_RML_MENU_FILE_LIST)
    {
        if (key == Rml::Input::KI_UP)
        {
            Queue(MIOPAN_RML_MENU_FILE_UP);
        }
        else if (key == Rml::Input::KI_DOWN)
        {
            Queue(MIOPAN_RML_MENU_FILE_DOWN);
        }
        else if (left || right)
        {
            Queue(left ? MIOPAN_RML_MENU_FILE_TAB_PREV : MIOPAN_RML_MENU_FILE_TAB_NEXT);
        }
        else if (enter)
        {
            Queue(MIOPAN_RML_MENU_FILE_CHOOSE);
        }
        else
        {
            return;
        }
    }
    else if (g_file.page.mode == MIOPAN_RML_MENU_FILE_EMPTY)
    {
        return;
    }
    /* The reader turns on CROSS too; the viewers do not. */
    else if (right || (enter && g_file.page.mode == MIOPAN_RML_MENU_FILE_DOCUMENT))
    {
        Queue(MIOPAN_RML_MENU_FILE_NEXT);
    }
    else if (left)
    {
        Queue(MIOPAN_RML_MENU_FILE_PREV);
    }
    else
    {
        return;
    }
    event.StopPropagation();
}

/* The id of `element` or the nearest ancestor that has one starting with
 * `prefix`. */
std::string Owner(Rml::Element *element, const char *prefix)
{
    for (; element != nullptr; element = element->GetParentNode())
    {
        const std::string &id = element->GetId();
        if (id.rfind(prefix, 0) == 0)
        {
            return id;
        }
    }
    return std::string();
}

void HandlePointer(Rml::Event &event)
{
    Rml::Element *target = event.GetTargetElement();
    const bool click = event == Rml::EventId::Click;
    if (click && !Owner(target, "prompt-back").empty())
    {
        Queue(MIOPAN_RML_MENU_FILE_BACK);
        return;
    }
    const float wheel = event == Rml::EventId::Mousescroll
                            ? event.GetParameter<float>("wheel_delta_y", 0.0f)
                            : 0.0f;
    if (g_file.page.mode == MIOPAN_RML_MENU_FILE_LIST)
    {
        if (wheel != 0.0f)
        {
            Queue(wheel < 0.0f ? MIOPAN_RML_MENU_FILE_UP : MIOPAN_RML_MENU_FILE_DOWN);
            return;
        }
        if (!click && Owner(target, "row-").empty())
        {
            return;
        }
        const std::string tab_arrow = Owner(target, "tab-arrow-");
        const std::string tab = Owner(target, "tab-");
        const std::string row = Owner(target, "row-");
        if (click && !Owner(target, "prompt-tab").empty())
        {
            Queue(MIOPAN_RML_MENU_FILE_TAB_NEXT);
        }
        else if (click && !tab_arrow.empty())
        {
            Queue(tab_arrow == "tab-arrow-0" ? MIOPAN_RML_MENU_FILE_TAB_PREV
                                             : MIOPAN_RML_MENU_FILE_TAB_NEXT);
        }
        else if (click && !tab.empty())
        {
            Queue(MIOPAN_RML_MENU_FILE_TAB, std::atoi(tab.c_str() + 4));
        }
        else if (!row.empty())
        {
            const int index = std::atoi(row.c_str() + 4);
            if (click)
            {
                Queue(index == g_file.page.cursor ? MIOPAN_RML_MENU_FILE_CHOOSE
                                                  : MIOPAN_RML_MENU_FILE_ROW,
                      index);
            }
            else if (MioPanRml::PointerActive() && index != g_file.page.cursor)
            {
                Queue(MIOPAN_RML_MENU_FILE_ROW, index);
            }
        }
        return;
    }
    if (g_file.page.mode == MIOPAN_RML_MENU_FILE_EMPTY)
    {
        return;
    }
    /* The viewers fill the page: the wheel, the arrows and the hint turn
     * pages, and so does a click on either half, as the game's mouse has
     * it. */
    if (wheel != 0.0f)
    {
        Queue(wheel < 0.0f ? MIOPAN_RML_MENU_FILE_PREV : MIOPAN_RML_MENU_FILE_NEXT);
    }
    else if (click)
    {
        const std::string arrow = Owner(target, "page-");
        if (arrow == "page-0")
        {
            Queue(MIOPAN_RML_MENU_FILE_PREV);
        }
        else if (!arrow.empty() || !Owner(target, "prompt-page").empty())
        {
            Queue(MIOPAN_RML_MENU_FILE_NEXT);
        }
        else
        {
            const float x = event.GetParameter<float>("mouse_x", 0.0f);
            const Rml::Vector2f stage = g_file.stage->GetAbsoluteOffset();
            const float middle = stage.x + g_file.stage->GetClientWidth() * 0.5f;
            Queue(x < middle ? MIOPAN_RML_MENU_FILE_PREV : MIOPAN_RML_MENU_FILE_NEXT);
        }
    }
}

void Listener::ProcessEvent(Rml::Event &event)
{
    /* Nothing more until the phase has taken the last answer, nor while the
     * page or its mode fades. */
    if (!g_file.visible || g_file.page.closing || g_file.page.busy ||
        g_file.command != MIOPAN_RML_MENU_FILE_NONE)
    {
        return;
    }
    if (event == Rml::EventId::Keydown)
    {
        HandleKey(event);
        return;
    }
    HandlePointer(event);
}
}

namespace MioPanRmlMenuFile
{
bool Init(Rml::Context *context)
{
    if (context == nullptr)
    {
        return false;
    }
    g_file.document = context->LoadDocument(kDocumentPath);
    if (g_file.document == nullptr)
    {
        SDL_Log("MioPan RmlUi: could not load %s", kDocumentPath);
        return false;
    }
    g_file.stage = g_file.document->GetElementById("stage");
    if (g_file.stage == nullptr)
    {
        SDL_Log("MioPan RmlUi: %s is missing its elements", kDocumentPath);
        g_file.document->Close();
        g_file = MenuFileState();
        return false;
    }
    for (Rml::EventId id : {Rml::EventId::Keydown, Rml::EventId::Click,
                            Rml::EventId::Mouseover, Rml::EventId::Mousescroll})
    {
        g_file.document->AddEventListener(id, &g_listener);
    }
    return true;
}

void Shutdown()
{
    g_file = MenuFileState();
}

void Frame()
{
    if (g_file.visible && !g_file.requested)
    {
        g_file.document->Hide();
        g_file.visible = false;
        g_file.command = MIOPAN_RML_MENU_FILE_NONE;
    }
    g_file.shown_last_frame = g_file.visible;
    g_file.requested = false;
}
}

extern "C" {

int MioPan_RmlMenuFileReady(void)
{
    return g_file.document != nullptr ? 1 : 0;
}

int MioPan_RmlMenuFileShow(const MIOPAN_RML_MENU_FILE *page)
{
    if (g_file.document == nullptr || page == nullptr)
    {
        return 0;
    }
    g_file.requested = true;
    const bool new_pak = page->pak_view != g_file.page.pak_view ||
                         page->pak_small != g_file.page.pak_small ||
                         page->pak_top != g_file.page.pak_top;
    g_file.page = *page;
    if (!g_file.visible || new_pak)
    {
        /* A new visit, or a viewer's paks: they may sit where another's
         * did. */
        MioPanRmlSprite::Forget();
        g_file.scale = MioPanRmlMenuArt::StageScale();
        g_file.stage->SetAttribute("style", MioPanRmlMenuArt::StageStyle());
        g_file.shown_stage.clear();
    }
    if (!g_file.visible)
    {
        g_file.command = MIOPAN_RML_MENU_FILE_NONE;
        g_file.document->Show(Rml::ModalFlag::Modal, Rml::FocusFlag::Document);
        g_file.visible = true;
    }
    g_file.stage->SetClass("closing", page->closing != 0);
    Refresh();
    return 1;
}

int MioPan_RmlMenuFileIsShown(void)
{
    return g_file.shown_last_frame ? 1 : 0;
}

int MioPan_RmlMenuFileConsumeCommand(int *arg)
{
    const int command = g_file.command;
    if (arg != nullptr)
    {
        *arg = g_file.command_arg;
    }
    g_file.command = MIOPAN_RML_MENU_FILE_NONE;
    g_file.command_arg = 0;
    return command;
}
}
