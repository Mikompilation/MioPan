// FILE: /home/zero_rom/zero2np/src/debug/screen_calib.c
//
// Screen calibration -- the tool behind DEBUG_WRK::init_screen_calib.
//
// PORT ADDITION, but not an invented one: the *asset* it exists to show is on
// the disc.  cddat.h names CD file 3622 CALIBRATION_TM2, and it sits between
// UBI_ROGO_EFF and VCITEST_PK2 -- the boot-logo and test-pattern corner of the
// file table.  Pulled out of IMG_BD.BIN it is a 640x448 32-bit TIM2, 1146944
// bytes, and it is the standard monitor-calibration reference: a colour
// photograph on the left, an eleven-step numbered greyscale wedge down the
// middle, red/green/blue primary patches, and two resolution star wheels, one
// black-on-white and one white-on-black.  Exactly the image a 2004 debug menu
// would have put on screen and left there while somebody adjusted a CRT.
//
// What the prototype has no code for is the screen that shows it.  ZERO2.MAP
// has no screen_calib object, dbg_menu_data[] is three entries wide, and
// nothing anywhere references CALIBRATION_TM2 -- so the asset shipped and its
// viewer did not, which is exactly what DEBUG_WRK::init_screen_calib being a
// flag with nothing behind it says.  Everything below is ours; the picture is
// theirs.
//
// The tool is also the place to set opt_wrk.brightness.  effect_scr.c's
// BrightnessAdjustmentFilterDraw() applies it to every ingame frame as a
// whole-screen darken (alphar 0x44) below 0x80 and a brighten (alphar 0x49,
// Cd*(1+As/128)) above it, and this calls that very filter last -- so the
// wedge is judged through the filter that will be applied in play.
//
// THE TIM2 IS NOT ONE OF THE GAME'S OWN.  Every other *_TM2 entry on the disc
// begins 40 04 01 00 -- the sprite-file container PK2SendVram()/SetSprFile()
// read.  3622 begins "TIM2": it is a raw file straight out of an authoring
// tool, never run through the game's packer.  Two consequences drive the code
// below:
//
//   - Its GsTex0 carries TBP0 = 0 and TBW = 0.  A TBP0 of 0 would land a
//     640x448x32 image on top of the frame buffer, and a TBW of 0 is not a
//     buffer width at all, so MakeTim2Direct() -- which forwards sgtx0.TBW
//     to the upload verbatim -- cannot be used.  Tim2LoadTexture() takes both
//     explicitly and is used instead, with the width Tim2CalcBufWidth() gives
//     for a 640-wide PSMCT32 surface: 10.
//   - The TEX0 the sprite draws with is therefore built here rather than read
//     out of the file.  TW/TH are 10/9 -- the powers of two enclosing 640x448,
//     which is what the file's own header says too.
//
// VRAM.  The image needs 640*448*4 = 0x118000 bytes = 0x1180 GS pages, and it
// goes at page 0x2bc0 -- the tree's large scratch base, the one
// SendEneVram() uploads a character's textures to during play and which
// nothing holds on a title-side debug screen.  That places it at
// 0x2bc0..0x3d40 out of 0x4000, which clears both halves of the font: the six
// fntdat[] banks image at 0x0000..0x1460 and their CLUTs at 0x3f08..0x3f4c.
// Checked, not assumed -- a collision here would corrupt the very text that
// reports what is wrong.
//
// Three pages.  The image is the tool; the other two are what it cannot show.
//
//     0  IMAGE      CALIBRATION_TM2, full screen
//     1  GEOMETRY   overscan frame, 90% / 80% safe areas, centre cross, grid
//     2  GREYSCALE  black-crush and white-clip strips, finer than the wedge
//
// Page 2 is also the fallback: if the file cannot be loaded -- no disc image,
// no heap -- page 0 says so and the synthetic pages still work.
//
// Everything drawn here other than the picture is DISP_SQAR through DispSqrD()
// and text through SetASCIIString2(), which take the frame top-left, 0..640 x
// 0..448, and fold the GS window origin (1728, 1824) in themselves.  That is
// also SPRT_DAT's space -- DispSprD() does its own centring, which is why
// photo_make.c's DrawPhotoBuffer() converting to screen-centred and straight
// back is dead code.  It is NOT SetSquare*()'s space; those go through
// ScreenX/ScreenY (graphics.c 121-128).

#include "screen_calib.h"

#include "zero2_debug.h"

#include "../common/ol_load.h"                   // ol_loadGetHeap / ol_loadFreeHeap
#include "../common/variable.h"                  // pad / key_now / opt_wrk
#include "../graphics/effect/effect.h"           // BrightnessAdjustmentFilterDraw
#include "../graphics/graph2d/g2d_draw.h"        // DISP_SQAR / SPRT_DAT / Disp*
#include "../graphics/graph2d/message.h"         // SetASCIIString2 / SetString2
#include "../graphics/graph2d/tim2.h"            // Tim2GetPictureHeader / Tim2LoadTexture
#include "../system/eeiop/cddat.h"               // CALIBRATION_TM2 / GetFileSize
#include "../system/eeiop/fileload.h"            // FileLoadReqEE / FileLoadIsEnd2
#include "../system/os/system.h"                 // GetPALMode
#include "../system/pad/pad.h"                   // paddat / GetPadAnalogRpt

#include <libgraph.h>                            // SCE_GS_SET_TEX0
#include <string.h>

/* Framebuffer the game draws into, both video modes.  PAL differs in timing
 * and pixel aspect, not in the size of this buffer -- which is exactly the
 * thing the geometry page is there to show. */
#define SC_SCREEN_W         640
#define SC_SCREEN_H         448

#define SC_CENTRE_X         (SC_SCREEN_W / 2)
#define SC_CENTRE_Y         (SC_SCREEN_H / 2)

/* Priority every quad here is drawn at, and the z CopySqrDToSqr() would derive
 * from it.  One layer for the whole tool: the test env below has ZTST ALWAYS,
 * so ordering is submission order and the pages are painted back to front. */
#define SC_PRI              0xc0
#define SC_Z                (0xfffff - SC_PRI)

/* Debug font: 12 pixels per character is what debug_menu.c sizes its panels
 * with (7160).  The real type-1 advance is proportional, 8..20 from
 * font_w_b0 -- this is the nominal figure the layout reserves. */
#define SC_CHAR_W           12
#define SC_LINE_H           14

#define SC_PAGE_IMAGE       0
#define SC_PAGE_GEOMETRY    1
#define SC_PAGE_GREYSCALE   2
#define SC_PAGE_NUM         3

/* Action-safe and title-safe insets: the conventional 90% and 80% of each
 * axis, halved because the inset is taken off both edges. */
#define SC_SAFE_ACTION_X    (SC_SCREEN_W  * 5 / 100)
#define SC_SAFE_ACTION_Y    (SC_SCREEN_H  * 5 / 100)
#define SC_SAFE_TITLE_X     (SC_SCREEN_W * 10 / 100)
#define SC_SAFE_TITLE_Y     (SC_SCREEN_H * 10 / 100)

#define SC_BRIGHT_NEUTRAL   0x80
#define SC_BRIGHT_MAX       0xff

/* The picture, and where it goes in GS memory.  See the VRAM note above. */
#define SC_TEX_TBP          0x2bc0
#define SC_TEX_TBW          10          /* Tim2CalcBufWidth(PSMCT32, 640)     */
#define SC_TEX_PSM          0           /* SCE_GS_PSMCT32                     */
#define SC_TEX_TW           10          /* log2 1024, the width  enclosing 640 */
#define SC_TEX_TH           9           /* log2  512, the height enclosing 448 */

/* Frames to wait for the load before calling the file absent. */
#define SC_LOAD_TIMEOUT     600

#define SC_TEX_IDLE         0
#define SC_TEX_LOADING      1
#define SC_TEX_READY        2
#define SC_TEX_FAILED       3

typedef struct
{
    int    page;
    int    tex_state;
    int    load_timer;
    void  *tm2_addr;
    u_long tex0;
} SCREEN_CALIB_WRK;

static SCREEN_CALIB_WRK screen_calib_wrk;

static char s_sc_title[]      = "SCREEN CALIB";
static char s_sc_page_img[]   = "1/3  IMAGE";
static char s_sc_page_geo[]   = "2/3  GEOMETRY";
static char s_sc_page_grey[]  = "3/3  GREYSCALE";
static char s_sc_help1[]      = "UP/DOWN PAGE   LEFT/RIGHT BRIGHTNESS";
static char s_sc_help2[]      = "L1/R1 x8   SQUARE RESET   TRIANGLE EXIT";
static char s_sc_frame[]      = "FRAME 640x448";
static char s_sc_action[]     = "ACTION SAFE 90%";
static char s_sc_title_safe[] = "TITLE SAFE 80%";
static char s_sc_crush[]      = "BLACK CRUSH  0 2 4 6 8 10 12 14";
static char s_sc_clip[]       = "WHITE CLIP  241 243 245 247 249 251 253 255";
static char s_sc_loading[]    = "LOADING CALIBRATION_TM2 (file 3622)   %d";
static char s_sc_failed[]     = "CALIBRATION_TM2 NOT AVAILABLE";
static char s_sc_failed2[]    = "the synthetic pages still work -- DOWN for GEOMETRY";
static char s_sc_credit[]     = "CALIBRATION_TM2  640x448  file 3622";

/* SetString2() is a hand-rolled formatter (message.c 626): %d %f %s %c %% and
 * a leading decimal width, and nothing else.  An unrecognised flag is read as
 * the first digit of a width, rejected as one, and then re-read for ever -- so
 * a "%+4d" hangs the game.  Hence the sign is a string of its own. */
static char s_sc_darken[]     = "DARKEN";
static char s_sc_brighten[]   = "BRIGHTEN";
static char s_sc_neutral[]    = "NEUTRAL";
static char s_sc_ntsc[]       = "NTSC";
static char s_sc_pal[]        = "PAL";
static char s_sc_sign_neg[]   = "-";
static char s_sc_sign_pos[]   = "+";
static char s_sc_sign_zero[]  = " ";
static char s_sc_readout[]    = "BRIGHTNESS %3d   OFFSET %s%3d   %s   %s";

/* --------------------------------------------------------------------------
 *  Drawing primitives
 * ------------------------------------------------------------------------ */

/* One filled rect.  The draw-environment fields are debug.c's own
 * SPU_draw_rect_func() set, with one change: ZBUF keeps ZMSK up so the pattern
 * leaves no z behind it.
 *
 * alphar 0x44 is SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80) -- (Cs - Cd) * As/128 + Cd
 * -- so an alpha of 0x80 is exactly Cs and every patch reaches the framebuffer
 * at the value asked for.  That is load-bearing: a ramp step blended against
 * whatever was underneath would not be a ramp. */
static void ScreenCalibFill(int x, int y, int w, int h,
                            u_char r, u_char g, u_char b)
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
    sq.pri = SC_PRI;

    CopySqrDToSqr(&dq, &sq);

    dq.zbuf   = 0x10a000118;
    dq.test   = 0x30003;
    dq.z      = SC_Z;
    dq.alphar = 0x44;
    dq.pri    = SC_PRI;

    for (i = 0; i < 4; i++)
    {
        dq.r[i] = r;
        dq.g[i] = g;
        dq.b[i] = b;
    }
    dq.alpha = 0x80;

    DispSqrD(&dq);
}

/* A rectangle outline, drawn as four rects so the corners are exact. */
static void ScreenCalibBox(int x, int y, int w, int h, int t,
                           u_char r, u_char g, u_char b)
{
    ScreenCalibFill(x,         y,         w,         t,         r, g, b);
    ScreenCalibFill(x,         y + h - t, w,         t,         r, g, b);
    ScreenCalibFill(x,         y + t,     t,         h - t * 2, r, g, b);
    ScreenCalibFill(x + w - t, y + t,     t,         h - t * 2, r, g, b);
}

/* --------------------------------------------------------------------------
 *  The picture
 * ------------------------------------------------------------------------ */

static void ScreenCalibReleaseTex(void)
{
    if (screen_calib_wrk.tm2_addr != (void *)0)
    {
        ol_loadFreeHeap(screen_calib_wrk.tm2_addr);
        screen_calib_wrk.tm2_addr = (void *)0;
    }
    screen_calib_wrk.tex_state = SC_TEX_IDLE;
}

static void ScreenCalibBeginLoad(void)
{
    unsigned int size = GetFileSize(CALIBRATION_TM2);

    screen_calib_wrk.load_timer = 0;

    if (size == 0)
    {
        screen_calib_wrk.tex_state = SC_TEX_FAILED;
        return;
    }

    screen_calib_wrk.tm2_addr = ol_loadGetHeap((int)size);
    if (screen_calib_wrk.tm2_addr == (void *)0)
    {
        screen_calib_wrk.tex_state = SC_TEX_FAILED;
        return;
    }

    FileLoadReqEE(CALIBRATION_TM2, screen_calib_wrk.tm2_addr, 5,
                  (FILE_LOAD_CALLBACK)0, (void *)0);
    screen_calib_wrk.tex_state = SC_TEX_LOADING;
}

/* Poll, and on the frame the file lands push it to GS memory once.
 *
 * Tim2LoadTexture() rather than MakeTim2Direct(): this TIM2's own TBW is 0 and
 * its TBP0 is 0, neither of which can be used (see the header note).  Both are
 * supplied here instead, and the TEX0 built below has to agree with them --
 * they are the same two constants. */
static void ScreenCalibPollLoad(void)
{
    TIM2_PICTUREHEADER *ph;
    void               *image;

    screen_calib_wrk.load_timer++;

    if (FileLoadIsEnd2(CALIBRATION_TM2, screen_calib_wrk.tm2_addr) == 0)
    {
        if (screen_calib_wrk.load_timer > SC_LOAD_TIMEOUT)
        {
            screen_calib_wrk.tex_state = SC_TEX_FAILED;
        }
        return;
    }

    if (Tim2CheckFileHeaer(screen_calib_wrk.tm2_addr) == 0)
    {
        screen_calib_wrk.tex_state = SC_TEX_FAILED;
        return;
    }

    ph = Tim2GetPictureHeader(screen_calib_wrk.tm2_addr, 0);
    if (ph == (TIM2_PICTUREHEADER *)0 || ph->ImageType != TIM2_RGB32)
    {
        screen_calib_wrk.tex_state = SC_TEX_FAILED;
        return;
    }

    image = Tim2GetImage(ph, 0);
    if (image == (void *)0)
    {
        screen_calib_wrk.tex_state = SC_TEX_FAILED;
        return;
    }

    Tim2LoadTexture(SC_TEX_PSM, SC_TEX_TBP, SC_TEX_TBW,
                    (int)ph->ImageWidth, (int)ph->ImageHeight,
                    (u_long128 *)image);

    /* TCC 0 ignores the texture's alpha (every texel is 0x80 anyway) and TFX 0
     * is MODULATE, which CopySprDToSpr()'s vertex colour of 0x80 makes an
     * identity -- so the picture reaches the framebuffer unaltered, which is
     * the whole point of a calibration image. */
    screen_calib_wrk.tex0 = SCE_GS_SET_TEX0(SC_TEX_TBP, SC_TEX_TBW, SC_TEX_PSM,
                                            SC_TEX_TW, SC_TEX_TH, 0, 0,
                                            0, 0, 0, 0, 0);
    screen_calib_wrk.tex_state = SC_TEX_READY;
}

static void ScreenCalibDrawImage(void)
{
    SPRT_DAT  sd;
    DISP_SPRT ds;

    ScreenCalibFill(0, 0, SC_SCREEN_W, SC_SCREEN_H, 0x00, 0x00, 0x00);

    switch (screen_calib_wrk.tex_state)
    {
    case SC_TEX_READY:
        memset(&sd, 0, sizeof(sd));
        sd.tex0  = screen_calib_wrk.tex0;
        sd.u     = 0;
        sd.v     = 0;
        sd.w     = SC_SCREEN_W;
        sd.h     = SC_SCREEN_H;
        sd.x     = 0;
        sd.y     = 0;
        sd.pri   = SC_PRI;
        sd.alpha = 0x80;
        sd.flip  = 0;
        sd.bln   = 0;

        CopySprDToSpr(&ds, &sd);
        ds.zbuf = 0x10a000118;
        ds.test = 0x30003;
        ds.z    = SC_Z;
        ds.pri  = SC_PRI;
        DispSprD(&ds);

        SetASCIIString2(0, 8.0f, (float)(SC_SCREEN_H - 52), 1,
                        0x60, 0x60, 0x60, s_sc_credit);
        break;

    case SC_TEX_LOADING:
        SetString2(0, 40.0f, 200.0f, 1, 0xff, 0xff, 0xff, s_sc_loading,
                   screen_calib_wrk.load_timer);
        break;

    default:
        SetASCIIString2(0, 40.0f, 196.0f, 1, 0xff, 0x60, 0x60, s_sc_failed);
        SetASCIIString2(0, 40.0f, 216.0f, 1, 0x80, 0x80, 0x80, s_sc_failed2);
        break;
    }
}

/* --------------------------------------------------------------------------
 *  Synthetic pages -- what the picture does not carry
 * ------------------------------------------------------------------------ */

/* Everything on this page is about what the display keeps.  The grid is drawn
 * first and dim so the frames read over it; the outer box sits on the literal
 * edge of the framebuffer, so any part of it that is missing is being cropped.
 * The photograph cannot show this: it has no reference to its own edges. */
static void ScreenCalibDrawGeometry(void)
{
    int i;

    ScreenCalibFill(0, 0, SC_SCREEN_W, SC_SCREEN_H, 0x00, 0x00, 0x00);

    /* 32-pixel grid.  Both axes are whole multiples of 32 (20 x 14 cells), so
     * the runs stop one step short of the edge and the framebuffer border
     * below draws that line itself, in white. */
    for (i = 32; i < SC_SCREEN_W; i += 32)
    {
        ScreenCalibFill(i, 0, 1, SC_SCREEN_H, 0x28, 0x28, 0x28);
    }
    for (i = 32; i < SC_SCREEN_H; i += 32)
    {
        ScreenCalibFill(0, i, SC_SCREEN_W, 1, 0x28, 0x28, 0x28);
    }

    /* Centre cross, then the two safe areas, then the framebuffer edge. */
    ScreenCalibFill(SC_CENTRE_X - 40, SC_CENTRE_Y,      81, 1, 0xff, 0xff, 0xff);
    ScreenCalibFill(SC_CENTRE_X,      SC_CENTRE_Y - 40, 1, 81, 0xff, 0xff, 0xff);

    ScreenCalibBox(SC_SAFE_TITLE_X, SC_SAFE_TITLE_Y,
                   SC_SCREEN_W - SC_SAFE_TITLE_X * 2,
                   SC_SCREEN_H - SC_SAFE_TITLE_Y * 2,
                   1, 0x00, 0xa0, 0xff);
    ScreenCalibBox(SC_SAFE_ACTION_X, SC_SAFE_ACTION_Y,
                   SC_SCREEN_W - SC_SAFE_ACTION_X * 2,
                   SC_SCREEN_H - SC_SAFE_ACTION_Y * 2,
                   1, 0x00, 0xff, 0x60);
    ScreenCalibBox(0, 0, SC_SCREEN_W, SC_SCREEN_H, 1, 0xff, 0xff, 0xff);

    /* Corner brackets, 48 pixels along each arm and four thick, so a corner
     * survives being clipped by a pixel or two and still reads as a corner. */
    for (i = 0; i < 4; i++)
    {
        int cx = (i & 1) ? SC_SCREEN_W - 48 : 0;
        int cy = (i & 2) ? SC_SCREEN_H - 4  : 0;
        int vx = (i & 1) ? SC_SCREEN_W - 4  : 0;
        int vy = (i & 2) ? SC_SCREEN_H - 48 : 0;

        ScreenCalibFill(cx, cy, 48, 4, 0xff, 0x00, 0x00);
        ScreenCalibFill(vx, vy, 4, 48, 0xff, 0x00, 0x00);
    }

    /* The three keys, stacked inside the title-safe box and each in its own
     * box's colour.  They sit low and left: clear of the overlay readout at the
     * top, of the centre cross, and of the control legend at the bottom. */
    SetASCIIString2(0, (float)(SC_SAFE_TITLE_X + 4), 340.0f,
                    1, 0xff, 0xff, 0xff, s_sc_frame);
    SetASCIIString2(0, (float)(SC_SAFE_TITLE_X + 4), 356.0f,
                    1, 0x00, 0xff, 0x60, s_sc_action);
    SetASCIIString2(0, (float)(SC_SAFE_TITLE_X + 4), 372.0f,
                    1, 0x00, 0xa0, 0xff, s_sc_title_safe);
}

/* The picture's wedge is eleven steps over the whole range; these two strips
 * are eight steps over the last sixteen values at each end, which is where a
 * whole-frame add or subtract shows first.  Set brightness so the crush
 * patches are the last that can still be told apart from their black field. */
static void ScreenCalibDrawGreyscale(void)
{
    int i;
    int step_w;

    ScreenCalibFill(0, 0, SC_SCREEN_W, SC_SCREEN_H, 0x00, 0x00, 0x00);

    /* 16 steps of 0x11 across the full width: 0, 17, 34 ... 255. */
    step_w = SC_SCREEN_W / 16;
    for (i = 0; i < 16; i++)
    {
        u_char v = (u_char)(i * 0x11);
        ScreenCalibFill(i * step_w, 96, step_w, 112, v, v, v);
    }
    ScreenCalibBox(0, 96, SC_SCREEN_W, 112, 1, 0x80, 0x80, 0x80);

    /* Black crush: eight patches 0..14 on their own black field. */
    ScreenCalibFill(64, 236, 512, 60, 0x00, 0x00, 0x00);
    for (i = 0; i < 8; i++)
    {
        u_char v = (u_char)(i * 2);
        ScreenCalibFill(72 + i * 64, 244, 48, 44, v, v, v);
    }

    /* White clip: eight patches 241..255 on a white field. */
    ScreenCalibFill(64, 320, 512, 60, 0xff, 0xff, 0xff);
    for (i = 0; i < 8; i++)
    {
        u_char v = (u_char)(241 + i * 2);
        ScreenCalibFill(72 + i * 64, 328, 48, 44, v, v, v);
    }

    SetASCIIString2(0, 64.0f, 218.0f, 1, 0xff, 0xff, 0xff, s_sc_crush);
    SetASCIIString2(0, 64.0f, 302.0f, 1, 0xff, 0xff, 0xff, s_sc_clip);
}

/* Title, page name, live readout and the control legend.  Drawn over whatever
 * page ran, and under the brightness filter. */
static void ScreenCalibDrawOverlay(void)
{
    static char *page_name[SC_PAGE_NUM] =
    {
        s_sc_page_img,
        s_sc_page_geo,
        s_sc_page_grey,
    };

    int    bright = opt_wrk.brightness;
    int    delta  = bright - SC_BRIGHT_NEUTRAL;
    char  *sign   = s_sc_sign_zero;
    char  *mode   = s_sc_neutral;
    float  y      = 8.0f;

    if (delta < 0)
    {
        sign  = s_sc_sign_neg;
        mode  = s_sc_darken;
        delta = -delta;
    }
    else if (delta > 0)
    {
        sign = s_sc_sign_pos;
        mode = s_sc_brighten;
    }

    SetASCIIString2(0, 8.0f, y, 1, 0xff, 0xff, 0x00, s_sc_title);
    SetASCIIString2(0, (float)(8 + (int)sizeof(s_sc_title) * SC_CHAR_W), y,
                    1, 0x80, 0x80, 0x80, page_name[screen_calib_wrk.page]);

    y += (float)SC_LINE_H;

    /* Brightness as the raw setting, as an offset from the filter's neutral,
     * and with the mode it will be applied in -- below 0x80 the filter darkens
     * (alphar 0x44), above it brightens (0x49). */
    SetString2(0, 8.0f, y, 1, 0xff, 0xff, 0xff, s_sc_readout,
               bright, sign, delta, mode,
               GetPALMode() ? s_sc_pal : s_sc_ntsc);

    SetASCIIString2(0, 8.0f, (float)(SC_SCREEN_H - 36), 1, 0x80, 0x80, 0x80, s_sc_help1);
    SetASCIIString2(0, 8.0f, (float)(SC_SCREEN_H - 22), 1, 0x80, 0x80, 0x80, s_sc_help2);
}

/* --------------------------------------------------------------------------
 *  Pad
 * ------------------------------------------------------------------------ */

/* The folder's own idiom: the remapped d-pad word (0x1000 UP, 0x4000 DOWN,
 * 0x8000 LEFT, 0x2000 RIGHT) OR'd with the analog repeats, whose indices run
 * 0 up, 1 down, 2 left, 3 right.  paddat[1] is TRIANGLE and key_now[6] SQUARE,
 * key_now[8]/[10] L1/R1.
 *
 * Returns non-zero on the frame the tool is left. */
static int ScreenCalibPad(void)
{
    int step;

    if (((pad[0].rpt & 0x1000U) != 0) || (GetPadAnalogRpt(0) != 0))
    {
        screen_calib_wrk.page =
            (screen_calib_wrk.page + SC_PAGE_NUM - 1) % SC_PAGE_NUM;
    }
    else if (((pad[0].rpt & 0x4000U) != 0) || (GetPadAnalogRpt(1) != 0))
    {
        screen_calib_wrk.page = (screen_calib_wrk.page + 1) % SC_PAGE_NUM;
    }

    step = ((*key_now[8] != 0) || (*key_now[10] != 0)) ? 8 : 1;

    if (((pad[0].rpt & 0x8000U) != 0) || (GetPadAnalogRpt(2) != 0))
    {
        opt_wrk.brightness -= step;
    }
    else if (((pad[0].rpt & 0x2000U) != 0) || (GetPadAnalogRpt(3) != 0))
    {
        opt_wrk.brightness += step;
    }

    if (*key_now[6] == 1)
    {
        opt_wrk.brightness = SC_BRIGHT_NEUTRAL;
    }

    if (opt_wrk.brightness < 0)
    {
        opt_wrk.brightness = 0;
    }
    else if (opt_wrk.brightness > SC_BRIGHT_MAX)
    {
        opt_wrk.brightness = SC_BRIGHT_MAX;
    }

    return (*paddat[1] == 1);
}

/* --------------------------------------------------------------------------
 *  Entry points
 * ------------------------------------------------------------------------ */

void ScreenCalibInit(void)
{
    memset(&screen_calib_wrk, 0, sizeof(screen_calib_wrk));
    screen_calib_wrk.page = SC_PAGE_IMAGE;

    /* The out-game heap is already live -- lang_sel.c reset it at boot and the
     * title released its own buffers on the way in here -- so this claims out
     * of it rather than resetting it.  scn_test.c resets because it is about
     * to load whole scenes; one 1.1 MB file does not need a clean slate, and
     * resetting would drop whatever else is holding a block. */
    ScreenCalibBeginLoad();
}

int ScreenCalibMain(void)
{
    int ret;

    ret = ScreenCalibPad();

    if (screen_calib_wrk.page < 0 || screen_calib_wrk.page >= SC_PAGE_NUM)
    {
        screen_calib_wrk.page = SC_PAGE_IMAGE;
    }

    if (screen_calib_wrk.tex_state == SC_TEX_LOADING)
    {
        ScreenCalibPollLoad();
    }

    switch (screen_calib_wrk.page)
    {
    case SC_PAGE_IMAGE:
        ScreenCalibDrawImage();
        break;

    case SC_PAGE_GEOMETRY:
        ScreenCalibDrawGeometry();
        break;

    case SC_PAGE_GREYSCALE:
        ScreenCalibDrawGreyscale();
        break;

    default:
        break;
    }

    ScreenCalibDrawOverlay();

    /* Last, so the pattern is judged through the same whole-frame filter the
     * game applies -- which is the setting this tool exists to choose. */
    BrightnessAdjustmentFilterDraw();

    if (ret != 0)
    {
        ScreenCalibReleaseTex();
    }

    return ret;
}
