// FILE: /home/zero_rom/zero2np/src/debug/test2d.c
//
// 2D drawing-layer test pattern -- the tool behind DEBUG_WRK::init_test2d.
//
// PORT ADDITION.  Like init_screen_calib beside it, init_test2d is one of the
// eight sub-tools DEBUG_WRK names and the seven the Feb 6 2004 prototype ships
// no code for: ZERO2.MAP has no test2d object and dbg_menu_data[] is three
// entries wide, so the flag and its InitDebug() clear are all that survived.
// Everything below is ours.  Same standing as debug/camera_menu.c.
//
// What it is for.  g2d_draw.c exposes five record converters, three Disp*
// entries and SetPanel(); graphics.c adds the SetLine2D / SetSquare* families.
// Between them they are what every menu, HUD and effect in the game draws
// through, and on the host every one of them ends at a second, immediate
// bridge (RendererFlatQuad / RendererQuad / RendererLine2D) alongside the GS
// packet.  A regression in one converter, one blend register or one of the two
// coordinate spaces shows up as "a menu looks wrong" three modules away.  This
// puts all of it on one screen with nothing else running.
//
// Four pages, UP/DOWN between them:
//
//     0  PRIMITIVES  one cell per record type, plus the space-agreement check
//     1  BLEND       the four ALPHA registers the game actually uses, over a
//                    black-to-white ramp
//     2  TRANSFORM   DISP_SQAR rotation and scale, static steps and live
//     3  FONT        the printable ASCII sheet in both font types
//
// THE SPACE-AGREEMENT CHECK is the one cell with a pass/fail rather than a
// look.  There are two 2D coordinate spaces in this tree and mixing them is a
// standing hazard (graphics.c 121-128): DISP_SQAR / SQAR_DAT / SetASCIIString2
// and SetLine2DPacket take the frame top-left, 0..640 x 0..448, and fold the GS
// window origin (1728, 1824) in themselves; everything reached through
// ScreenX/ScreenY -- the whole SetSquare* family -- is screen-centred and wants
// (x - 320, y - 224).  Page 0 draws a SetSquareS() plate and lays a DispSqrD()
// rect on top of it, inset by a known margin on all four sides.  If the two
// spaces agree the margin is even; if they have drifted the frame is lopsided
// by exactly the offset, and which side is thick says which way.
//
// Nothing here loads a texture, so the tool runs with no pak resident -- which
// is what lets it hang off the title-side debug menu.  It follows that the
// font is the one textured thing on screen, and page 3 is therefore also the
// check that the font bank survived ReleaseLoadingTexMem().

#include "test2d.h"

#include "zero2_debug.h"

#include "../common/variable.h"                  // pad / key_now
#include "../graphics/graph2d/g2d_draw.h"        // DISP_SQAR / *_DAT / Copy* / Disp*
#include "../graphics/graph2d/message.h"         // SetASCIIString2 / SetASCIIString3 / SetString2
#include "../graphics/graphics.h"                // SetLine2D / SetSquareS
#include "../system/pad/pad.h"                   // paddat / GetPadAnalogRpt

#include <string.h>

#define T2_SCREEN_W         640
#define T2_SCREEN_H         448

/* Priority every quad this file draws is at, and the z CopySqrDToSqr() derives
 * from it.  The test env below is ZTST ALWAYS, so ordering is submission
 * order; one layer is enough. */
#define T2_PRI              0xc0
#define T2_Z                (0xfffff - T2_PRI)

/* Debug font.  SetASCIIString3() picks the table and the advance off `type`:
 * type 0 is ascii_font_tbl2 on a fixed 12-pixel advance in a 12-pixel cell,
 * type 1 is ascii_font_tbl proportional (font_w_b0, 8..20 pixels) in a
 * 24-pixel cell.  T2_CHAR_W is the *nominal* advance the layout below reserves
 * per character -- debug_menu.c sizes its own panels with the same 0xc
 * (7160) even though it draws at type 1. */
#define T2_CHAR_W           12
#define T2_LINE_H           14
#define T2_FONT_PROP        1
#define T2_FONT_FIXED       0

#define T2_PAGE_PRIMITIVE   0
#define T2_PAGE_BLEND       1
#define T2_PAGE_TRANSFORM   2
#define T2_PAGE_FONT        3
#define T2_PAGE_NUM         4

/* Page 0 cell grid: three across, two down. */
#define T2_CELL_X0          24
#define T2_CELL_Y0          64
#define T2_CELL_W           190
#define T2_CELL_H           130
#define T2_CELL_SX          206
#define T2_CELL_SY          152

/* Page 0 space-agreement plate: the SetSquareS() plate and the inset the
 * DispSqrD() rect sits inside it by, on every side. */
#define T2_AGREE_X          232
#define T2_AGREE_Y          382
#define T2_AGREE_W          176
#define T2_AGREE_H          40
#define T2_AGREE_INSET      6

/* The four ALPHA register values the game uses, decoded.  SCE_GS_SET_ALPHA
 * packs A(0-1) B(2-3) C(4-5) D(6-7) and the output is (A - B) * C / 128 + D,
 * with 0 = Cs, 1 = Cd, 2 = zero, and C = 0 meaning As.  A survey of every
 * `alphar =` in the tree finds exactly these four: 0x48 110 times, 0x46 11,
 * 0x44 4 and 0x49 twice. */
#define T2_BLEND_NUM        4

typedef struct
{
    u_long  reg;
    char   *name;
    char   *formula;
} T2_BLEND_MODE;

typedef struct
{
    int page;
    int frozen;
    int frame;
} TEST2D_WRK;

static TEST2D_WRK test2d_wrk;

/* Printable ASCII, 0x20..0x7e, split into rows.  Built once by Test2dInit()
 * because SetASCIIString2() takes a mutable char *. */
#define T2_SHEET_COLS       24
#define T2_SHEET_ROWS       4
static char s_t2_sheet[T2_SHEET_ROWS][T2_SHEET_COLS + 1];

static char s_t2_title[]      = "TEST 2D";
static char s_t2_page_prim[]  = "1/4  PRIMITIVES";
static char s_t2_page_blend[] = "2/4  BLEND";
static char s_t2_page_xform[] = "3/4  TRANSFORM";
static char s_t2_page_font[]  = "4/4  FONT";
static char s_t2_help1[]      = "UP/DOWN PAGE   LEFT/RIGHT STEP FRAME";
static char s_t2_help2[]      = "SQUARE FREEZE   TRIANGLE EXIT";
static char s_t2_status[]     = "FRAME %5d   %s";
static char s_t2_running[]    = "RUNNING";
static char s_t2_frozen[]     = "FROZEN";

static char s_t2_sqar[]       = "SQAR_DAT  flat";
static char s_t2_gsqr[]       = "GSQR_DAT  gouraud";
static char s_t2_sqr4[]       = "SQR4_DAT  4 corner";
/* Cell labels are drawn at the cell's own x on a 206-pixel pitch, and the font
 * is proportional -- so a label has to measure under about 200 pixels or it
 * runs into the next cell's.  "GSQ4_DAT  4 cnr gour" measures 219 and did. */
static char s_t2_gsq4[]       = "GSQ4_DAT gour4";
static char s_t2_panel[]      = "SetPanel";
static char s_t2_line[]       = "SetLine2D";
static char s_t2_agree_ok[]   = "SPACE CHECK  even margin = top-left and centred agree";

/* One label per patch column rather than one header string: the columns are on
 * a 110-pixel pitch and the font is proportional, so a single run of text could
 * not be made to line up with them. */
static char s_t2_alpha_hdr[]  = "ALPHA";
static char s_t2_alpha_col[T2_BLEND_NUM][3] = { "20", "40", "60", "80" };
static char s_t2_blend44[]    = "44 NORMAL";
static char s_t2_blend48[]    = "48 ADD";
static char s_t2_blend46[]    = "46 SUB";
static char s_t2_blend49[]    = "49 BRIGHT";
static char s_t2_f44[]        = "(Cs-Cd)As/128+Cd";
static char s_t2_f48[]        = "Cs As/128+Cd";
static char s_t2_f46[]        = "Cd(1-As/128)";
static char s_t2_f49[]        = "Cd(1+As/128)";

static char s_t2_rot[]        = "ROT  0 45 90 135 180 225 270 315";
static char s_t2_scale[]      = "SCALE  0.5   1.0   1.5   2.0";
static char s_t2_live[]       = "LIVE  rot / scale / both";

static char s_t2_font_prop[]  = "TYPE 1  PROPORTIONAL  ascii_font_tbl";
static char s_t2_font_fix[]   = "TYPE 0  FIXED 12      ascii_font_tbl2";
static char s_t2_font_col[]   = "COLOUR";
static char s_t2_font_alp[]   = "ALPHA";

static const T2_BLEND_MODE s_t2_blend[T2_BLEND_NUM] =
{
    { 0x44, s_t2_blend44, s_t2_f44 },
    { 0x48, s_t2_blend48, s_t2_f48 },
    { 0x46, s_t2_blend46, s_t2_f46 },
    { 0x49, s_t2_blend49, s_t2_f49 },
};

static const u_char s_t2_blend_alpha[T2_BLEND_NUM] = { 0x20, 0x40, 0x60, 0x80 };

/* --------------------------------------------------------------------------
 *  Shared draw-environment
 *
 *  Every DISP_SQAR below is finished through this: ZTST ALWAYS so ordering is
 *  submission order, ZMSK up so the pattern leaves no z behind it, and the
 *  caller's own ALPHA register.  Split out so the blend page can vary exactly
 *  one field and nothing else.
 * ------------------------------------------------------------------------ */
static void Test2dFinish(DISP_SQAR *dq, u_long alphar)
{
    dq->zbuf   = 0x10a000118;
    dq->test   = 0x30003;
    dq->z      = T2_Z;
    dq->pri    = T2_PRI;
    dq->alphar = alphar;
    DispSqrD(dq);
}

/* Flat axis-aligned rect through SQAR_DAT / CopySqrDToSqr -- the path most of
 * the game's panels take.  alphar 0x44 at alpha 0x80 is exactly Cs, so a patch
 * reaches the framebuffer at the value asked for. */
static void Test2dRect(int x, int y, int w, int h, u_char r, u_char g, u_char b)
{
    SQAR_DAT  sq;
    DISP_SQAR dq;
    int       i;

    if (w <= 0 || h <= 0)
    {
        return;
    }

    memset(&sq, 0, sizeof(sq));
    sq.w   = (u_int)w;
    sq.h   = (u_int)h;
    sq.x   = x;
    sq.y   = y;
    sq.pri = T2_PRI;

    CopySqrDToSqr(&dq, &sq);

    for (i = 0; i < 4; i++)
    {
        dq.r[i] = r;
        dq.g[i] = g;
        dq.b[i] = b;
    }
    dq.alpha = 0x80;

    Test2dFinish(&dq, 0x44);
}

static void Test2dBox(int x, int y, int w, int h, int t,
                      u_char r, u_char g, u_char b)
{
    Test2dRect(x,         y,         w,         t,         r, g, b);
    Test2dRect(x,         y + h - t, w,         t,         r, g, b);
    Test2dRect(x,         y + t,     t,         h - t * 2, r, g, b);
    Test2dRect(x + w - t, y + t,     t,         h - t * 2, r, g, b);
}

/* --------------------------------------------------------------------------
 *  Page 0 -- one cell per record type
 * ------------------------------------------------------------------------ */

static void Test2dCellFrame(int col, int row, char *label, int *px, int *py)
{
    int x = T2_CELL_X0 + col * T2_CELL_SX;
    int y = T2_CELL_Y0 + row * T2_CELL_SY;

    Test2dBox(x, y, T2_CELL_W, T2_CELL_H, 1, 0x50, 0x50, 0x50);
    SetASCIIString2(0, (float)x, (float)(y + T2_CELL_H + 2), T2_FONT_PROP,
                    0xa0, 0xa0, 0xa0, label);

    *px = x;
    *py = y;
}

static void Test2dDrawPrimitives(void)
{
    SQAR_DAT  sq;
    GSQR_DAT  gs;
    SQR4_DAT  q4;
    GSQ4_DAT  g4;
    DISP_SQAR dq;
    int       x;
    int       y;
    int       i;

    Test2dRect(0, 0, T2_SCREEN_W, T2_SCREEN_H, 0x00, 0x00, 0x00);

    /* SQAR_DAT -- flat axis-aligned. */
    Test2dCellFrame(0, 0, s_t2_sqar, &x, &y);
    memset(&sq, 0, sizeof(sq));
    sq.x = x + 30; sq.y = y + 25; sq.w = 130; sq.h = 80;
    sq.pri = T2_PRI; sq.r = 0xd0; sq.g = 0x40; sq.b = 0x40; sq.alpha = 0x80;
    CopySqrDToSqr(&dq, &sq);
    Test2dFinish(&dq, 0x44);

    /* GSQR_DAT -- axis-aligned, one colour per corner.  Corner order here and
     * in the two 4-corner records below is TL, TR, BL, BR, which is the order
     * CopySqrDToSqr() expands a rect into. */
    Test2dCellFrame(1, 0, s_t2_gsqr, &x, &y);
    memset(&gs, 0, sizeof(gs));
    gs.x = x + 30; gs.y = y + 25; gs.w = 130; gs.h = 80;
    gs.pri = T2_PRI; gs.alpha = 0x80;
    gs.r[0] = 0xff; gs.g[0] = 0x00; gs.b[0] = 0x00;
    gs.r[1] = 0x00; gs.g[1] = 0xff; gs.b[1] = 0x00;
    gs.r[2] = 0x00; gs.g[2] = 0x00; gs.b[2] = 0xff;
    gs.r[3] = 0xff; gs.g[3] = 0xff; gs.b[3] = 0x00;
    CopyGSqDToSqr(&dq, &gs);
    Test2dFinish(&dq, 0x44);

    /* SQR4_DAT -- free corners, one colour.  A trapezoid, so a converter that
     * quietly re-derived the corners from a bounding box would show. */
    Test2dCellFrame(2, 0, s_t2_sqr4, &x, &y);
    memset(&q4, 0, sizeof(q4));
    q4.x[0] = x + 55; q4.y[0] = y + 25;
    q4.x[1] = x + 135; q4.y[1] = y + 25;
    q4.x[2] = x + 25; q4.y[2] = y + 105;
    q4.x[3] = x + 165; q4.y[3] = y + 105;
    q4.pri = T2_PRI; q4.r = 0x40; q4.g = 0xc0; q4.b = 0xd0; q4.alpha = 0x80;
    CopySq4DToSqr(&dq, &q4);
    Test2dFinish(&dq, 0x44);

    /* GSQ4_DAT -- free corners, one colour each. */
    Test2dCellFrame(0, 1, s_t2_gsq4, &x, &y);
    memset(&g4, 0, sizeof(g4));
    g4.x[0] = x + 25;  g4.y[0] = y + 40;
    g4.x[1] = x + 165; g4.y[1] = y + 20;
    g4.x[2] = x + 25;  g4.y[2] = y + 105;
    g4.x[3] = x + 165; g4.y[3] = y + 85;
    g4.pri = T2_PRI; g4.alpha = 0x80;
    g4.r[0] = 0xff; g4.g[0] = 0xff; g4.b[0] = 0xff;
    g4.r[1] = 0xff; g4.g[1] = 0x00; g4.b[1] = 0xff;
    g4.r[2] = 0x00; g4.g[2] = 0xff; g4.b[2] = 0xff;
    g4.r[3] = 0x20; g4.g[3] = 0x20; g4.b[3] = 0x20;
    CopyGS4DToSqr(&dq, &g4);
    Test2dFinish(&dq, 0x44);

    /* SetPanel -- takes floats and its own priority, and builds the SQAR_DAT
     * itself.  Drawn at half alpha over a bar so the blend it hardcodes shows. */
    Test2dCellFrame(1, 1, s_t2_panel, &x, &y);
    Test2dRect(x + 20, y + 50, 150, 30, 0x80, 0x80, 0x00);
    SetPanel(T2_PRI, (float)(x + 30), (float)(y + 25),
             (float)(x + 160), (float)(y + 105), 0x30, 0x60, 0xff, 0x40);

    /* SetLine2D -- a burst from the cell centre, which is also the check that
     * the line path and the rect path share an origin: every spoke must start
     * on the centre dot. */
    Test2dCellFrame(2, 1, s_t2_line, &x, &y);
    for (i = 0; i < 12; i++)
    {
        static const int dx[12] = {  70,  60,  35,   0, -35, -60, -70, -60, -35,   0,  35,  60 };
        static const int dy[12] = {   0,  25,  43,  50,  43,  25,   0, -25, -43, -50, -43, -25 };
        int cx = x + T2_CELL_W / 2;
        int cy = y + T2_CELL_H / 2;

        SetLine2D((float)cx, (float)cy, (float)(cx + dx[i]), (float)(cy + dy[i]),
                  (u_char)(0x40 + i * 0x10), 0xc0, (u_char)(0xff - i * 0x10), 0x80);
    }
    Test2dRect(x + T2_CELL_W / 2 - 1, y + T2_CELL_H / 2 - 1, 3, 3, 0xff, 0xff, 0xff);

    /* Space-agreement check.  SetSquareS() is screen-centred, so the plate is
     * placed by subtracting (320, 224); the rect on top is placed in the
     * top-left space directly.  Even margin on all four sides = the two spaces
     * still agree.  Drawn last on the page because SetSquare() is the one
     * primitive here that leaves ZMSK down and writes z. */
    SetSquareS(T2_PRI,
               (float)(T2_AGREE_X - 320), (float)(T2_AGREE_Y - 224),
               (float)(T2_AGREE_X + T2_AGREE_W - 320),
               (float)(T2_AGREE_Y + T2_AGREE_H - 224),
               0xff, 0x80, 0x00, 0x80);
    Test2dRect(T2_AGREE_X + T2_AGREE_INSET, T2_AGREE_Y + T2_AGREE_INSET,
               T2_AGREE_W - T2_AGREE_INSET * 2, T2_AGREE_H - T2_AGREE_INSET * 2,
               0x00, 0x00, 0x00);

    SetASCIIString2(0, 24.0f, 366.0f, T2_FONT_PROP, 0xff, 0xff, 0xff, s_t2_agree_ok);
}

/* --------------------------------------------------------------------------
 *  Page 1 -- the four ALPHA registers over a ramp
 * ------------------------------------------------------------------------ */

static void Test2dDrawBlend(void)
{
    GSQR_DAT  gs;
    SQAR_DAT  sq;
    DISP_SQAR dq;
    int       i;
    int       j;

    Test2dRect(0, 0, T2_SCREEN_W, T2_SCREEN_H, 0x00, 0x00, 0x00);

    /* Backdrop: black on the left, white on the right, so every patch is read
     * against the whole range of destination values at once.  SUB and BRIGHT
     * are the two that only separate at an end. */
    memset(&gs, 0, sizeof(gs));
    gs.x = 0; gs.y = 62; gs.w = T2_SCREEN_W; gs.h = 300;
    gs.pri = T2_PRI; gs.alpha = 0x80;
    gs.r[0] = 0x00; gs.g[0] = 0x00; gs.b[0] = 0x00;
    gs.r[1] = 0xff; gs.g[1] = 0xff; gs.b[1] = 0xff;
    gs.r[2] = 0x00; gs.g[2] = 0x00; gs.b[2] = 0x00;
    gs.r[3] = 0xff; gs.g[3] = 0xff; gs.b[3] = 0xff;
    CopyGSqDToSqr(&dq, &gs);
    Test2dFinish(&dq, 0x44);

    SetASCIIString2(0, 6.0f, 48.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_alpha_hdr);
    for (i = 0; i < T2_BLEND_NUM; i++)
    {
        SetASCIIString2(0, (float)(190 + i * 110 + 38), 48.0f, T2_FONT_PROP,
                        0xff, 0xff, 0x00, s_t2_alpha_col[i]);
    }

    for (i = 0; i < T2_BLEND_NUM; i++)
    {
        int row_y = 70 + i * 72;

        /* Row label sits at the black end of the ramp, where white reads. */
        SetASCIIString2(0, 6.0f, (float)(row_y + 12), T2_FONT_PROP,
                        0xff, 0xff, 0xff, s_t2_blend[i].name);
        SetASCIIString2(0, 6.0f, (float)(row_y + 30), T2_FONT_PROP,
                        0x90, 0x90, 0x90, s_t2_blend[i].formula);

        /* Four patches of the same mid-grey source at rising alpha.  Only the
         * ALPHA register and the alpha byte change between them. */
        for (j = 0; j < T2_BLEND_NUM; j++)
        {
            memset(&sq, 0, sizeof(sq));
            sq.x = 190 + j * 110; sq.y = row_y; sq.w = 100; sq.h = 56;
            sq.pri = T2_PRI;
            sq.r = 0x80; sq.g = 0x80; sq.b = 0x80;
            sq.alpha = s_t2_blend_alpha[j];
            CopySqrDToSqr(&dq, &sq);
            dq.alpha = s_t2_blend_alpha[j];
            Test2dFinish(&dq, s_t2_blend[i].reg);
        }
    }
}

/* --------------------------------------------------------------------------
 *  Page 2 -- DISP_SQAR rotation and scale
 * ------------------------------------------------------------------------ */

/* One 40x40 square rotated and scaled about its own centre.  DispSqrD() takes
 * crx/cry and csx/csy in the same top-left space as x/y (it subtracts 320/224
 * from all of them together), so the centre is just the square's own. */
static void Test2dXformSquare(int cx, int cy, float rot, float scale,
                              u_char r, u_char g, u_char b)
{
    SQAR_DAT  sq;
    DISP_SQAR dq;
    int       i;

    memset(&sq, 0, sizeof(sq));
    sq.x = cx - 20; sq.y = cy - 20; sq.w = 40; sq.h = 40;
    sq.pri = T2_PRI; sq.alpha = 0x80;
    CopySqrDToSqr(&dq, &sq);

    for (i = 0; i < 4; i++)
    {
        dq.r[i] = r;
        dq.g[i] = g;
        dq.b[i] = b;
    }
    dq.alpha = 0x80;
    dq.rot = rot;
    dq.crx = (float)cx;
    dq.cry = (float)cy;
    dq.scw = scale;
    dq.sch = scale;
    dq.csx = (float)cx;
    dq.csy = (float)cy;

    Test2dFinish(&dq, 0x44);

    /* Centre pip, drawn after and unrotated: a square whose transform has
     * drifted no longer sits on its own pip. */
    Test2dRect(cx - 1, cy - 1, 3, 3, 0xff, 0xff, 0xff);
}

static void Test2dDrawTransform(void)
{
    static const float scale[4] = { 0.5f, 1.0f, 1.5f, 2.0f };
    int   i;
    int   f = test2d_wrk.frame;
    int   tri;

    Test2dRect(0, 0, T2_SCREEN_W, T2_SCREEN_H, 0x00, 0x00, 0x00);

    /* Eight 45-degree steps.  The rotation is about each square's own centre,
     * so the pips stay on a straight line however far round the squares go. */
    SetASCIIString2(0, 24.0f, 62.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_rot);
    for (i = 0; i < 8; i++)
    {
        Test2dXformSquare(60 + i * 72, 122, (float)(i * 45), 1.0f,
                          0xd0, 0x60, 0x60);
    }

    /* Four scale steps.  2.0 is 80 pixels across, which is why the spacing is
     * wider than the squares look like they need. */
    SetASCIIString2(0, 24.0f, 176.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_scale);
    for (i = 0; i < 4; i++)
    {
        Test2dXformSquare(120 + i * 130, 246, 0.0f, scale[i], 0x60, 0xd0, 0x60);
    }

    /* Live: rotation off the frame counter, scale off a triangle wave, and one
     * doing both.  Frozen by SQUARE, stepped by LEFT/RIGHT while frozen -- so a
     * frame that looks wrong can be held and walked. */
    tri = f % 120;
    if (tri >= 60)
    {
        tri = 120 - tri;
    }

    SetASCIIString2(0, 24.0f, 300.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_live);
    Test2dXformSquare(160, 356, (float)(f * 3 % 360), 1.0f, 0x60, 0x60, 0xf0);
    Test2dXformSquare(320, 356, 0.0f, 0.5f + (float)tri / 60.0f, 0x60, 0x60, 0xf0);
    Test2dXformSquare(480, 356, (float)(f * 3 % 360), 0.5f + (float)tri / 60.0f,
                      0x60, 0x60, 0xf0);
}

/* --------------------------------------------------------------------------
 *  Page 3 -- the printable ASCII sheet, both font types
 * ------------------------------------------------------------------------ */

static void Test2dDrawFont(void)
{
    /* Short enough that the colour column at x=24 and the alpha column at
     * x=360 do not meet: it measures 171 pixels against the 336 available. */
    static char col_str[] = "MioPan 2D 0123";
    int i;

    Test2dRect(0, 0, T2_SCREEN_W, T2_SCREEN_H, 0x00, 0x00, 0x00);

    /* Type 1: proportional, ascii_font_tbl, 24-pixel cell.  This is what the
     * whole debug layer draws at, so a hole in this sheet is a hole in every
     * debug readout in the game. */
    SetASCIIString2(0, 24.0f, 56.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_font_prop);
    for (i = 0; i < T2_SHEET_ROWS; i++)
    {
        SetASCIIString2(0, 24.0f, (float)(76 + i * 18), T2_FONT_PROP,
                        0xff, 0xff, 0xff, s_t2_sheet[i]);
    }

    /* Type 0: fixed 12-pixel advance out of ascii_font_tbl2 -- a different
     * table, so a glyph can be present in one and missing in the other. */
    SetASCIIString2(0, 24.0f, 162.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_font_fix);
    for (i = 0; i < T2_SHEET_ROWS; i++)
    {
        SetASCIIString2(0, 24.0f, (float)(182 + i * 18), T2_FONT_FIXED,
                        0xff, 0xff, 0xff, s_t2_sheet[i]);
    }

    /* Colour: the same string in seven primaries. */
    SetASCIIString2(0, 24.0f, 268.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_font_col);
    for (i = 0; i < 7; i++)
    {
        static const u_char cr[7] = { 0xff, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff };
        static const u_char cg[7] = { 0x00, 0xff, 0x00, 0xff, 0x00, 0xff, 0xff };
        static const u_char cb[7] = { 0x00, 0x00, 0xff, 0xff, 0xff, 0x00, 0xff };

        SetASCIIString2(0, 24.0f, (float)(288 + i * 16), T2_FONT_PROP,
                        cr[i], cg[i], cb[i], col_str);
    }

    /* Alpha: SetASCIIString3 is the only text entry that takes one.  Over a
     * white bar, so the low steps are visible rather than lost on black. */
    SetASCIIString2(0, 360.0f, 268.0f, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_font_alp);
    Test2dRect(354, 284, 190, 118, 0x60, 0x60, 0x60);
    for (i = 0; i < 7; i++)
    {
        SetASCIIString3(0, 360.0f, (float)(288 + i * 16), T2_FONT_PROP,
                        0xff, 0xff, 0xff, (u_char)(0x14 + i * 0x14), col_str);
    }
}

/* --------------------------------------------------------------------------
 *  Overlay
 * ------------------------------------------------------------------------ */

static void Test2dDrawOverlay(void)
{
    static char *page_name[T2_PAGE_NUM] =
    {
        s_t2_page_prim,
        s_t2_page_blend,
        s_t2_page_xform,
        s_t2_page_font,
    };

    float y = 8.0f;

    SetASCIIString2(0, 8.0f, y, T2_FONT_PROP, 0xff, 0xff, 0x00, s_t2_title);
    SetASCIIString2(0, (float)(8 + (int)sizeof(s_t2_title) * T2_CHAR_W), y,
                    T2_FONT_PROP, 0x80, 0x80, 0x80, page_name[test2d_wrk.page]);

    y += (float)T2_LINE_H;

    /* SetString2() is a hand-rolled formatter (message.c 626): %d %f %s %c %%
     * and a leading decimal width, nothing else.  An unrecognised flag is read
     * as the first digit of a width, rejected as one, and then re-read for ever
     * -- so a "%+d" here would hang the game. */
    SetString2(0, 8.0f, y, T2_FONT_PROP, 0xff, 0xff, 0xff, s_t2_status,
               test2d_wrk.frame,
               test2d_wrk.frozen ? s_t2_frozen : s_t2_running);

    SetASCIIString2(0, 8.0f, (float)(T2_SCREEN_H - 36), T2_FONT_PROP,
                    0x80, 0x80, 0x80, s_t2_help1);
    SetASCIIString2(0, 8.0f, (float)(T2_SCREEN_H - 22), T2_FONT_PROP,
                    0x80, 0x80, 0x80, s_t2_help2);
}

/* --------------------------------------------------------------------------
 *  Pad
 * ------------------------------------------------------------------------ */

/* The folder's own idiom: the remapped d-pad word (0x1000 UP, 0x4000 DOWN,
 * 0x8000 LEFT, 0x2000 RIGHT) OR'd with the analog repeats, whose indices run
 * 0 up, 1 down, 2 left, 3 right.  paddat[1] is TRIANGLE, key_now[6] SQUARE.
 *
 * Returns non-zero on the frame the tool is left. */
static int Test2dPad(void)
{
    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))
    {
        test2d_wrk.page = (test2d_wrk.page + T2_PAGE_NUM - 1) % T2_PAGE_NUM;
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))
    {
        test2d_wrk.page = (test2d_wrk.page + 1) % T2_PAGE_NUM;
    }

    if (*key_now[6] == 1)
    {
        test2d_wrk.frozen ^= 1;
    }

    /* LEFT/RIGHT walk the animation, but only while it is held still --
     * stepping a running counter would do nothing visible. */
    if (test2d_wrk.frozen != 0)
    {
        if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0))
        {
            test2d_wrk.frame--;
        }
        else if (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))
        {
            test2d_wrk.frame++;
        }
    }
    else
    {
        test2d_wrk.frame++;
    }

    /* Keep the counter inside one full cycle of both live animations: the
     * rotation repeats every 120 frames (3 degrees a frame) and the scale
     * triangle every 120, so 120 is the whole period.  Wrapping here rather
     * than letting it run keeps the readout short and the arithmetic exact. */
    if (test2d_wrk.frame < 0)
    {
        test2d_wrk.frame += 120;
    }
    else if (test2d_wrk.frame >= 120)
    {
        test2d_wrk.frame -= 120;
    }

    return (*paddat[1] == 1);
}

/* --------------------------------------------------------------------------
 *  Entry points
 * ------------------------------------------------------------------------ */

void Test2dInit(void)
{
    int row;
    int col;
    int code;

    test2d_wrk.page   = T2_PAGE_PRIMITIVE;
    test2d_wrk.frozen = 0;
    test2d_wrk.frame  = 0;

    /* Printable ASCII, 0x20..0x7e, laid into fixed-width rows.  Built here
     * rather than written out as literals so the sheet cannot drift from the
     * range SetASCIIString3() actually maps: its guard is
     * `(u_char)(code + 0xe0) < 0xbe`, i.e. exactly 0x20..0xdd, of which
     * 0x20..0x7e is the half a debug page can show. */
    code = 0x20;
    for (row = 0; row < T2_SHEET_ROWS; row++)
    {
        for (col = 0; col < T2_SHEET_COLS; col++)
        {
            s_t2_sheet[row][col] = (code <= 0x7e) ? (char)code : ' ';
            code++;
        }
        s_t2_sheet[row][T2_SHEET_COLS] = '\0';
    }
}

int Test2dMain(void)
{
    int ret;

    ret = Test2dPad();

    if (test2d_wrk.page < 0 || test2d_wrk.page >= T2_PAGE_NUM)
    {
        test2d_wrk.page = T2_PAGE_PRIMITIVE;
    }

    switch (test2d_wrk.page)
    {
    case T2_PAGE_PRIMITIVE:
        Test2dDrawPrimitives();
        break;

    case T2_PAGE_BLEND:
        Test2dDrawBlend();
        break;

    case T2_PAGE_TRANSFORM:
        Test2dDrawTransform();
        break;

    case T2_PAGE_FONT:
        Test2dDrawFont();
        break;

    default:
        break;
    }

    Test2dDrawOverlay();

    return ret;
}
