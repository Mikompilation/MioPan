// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/draw_line.c
//
// Common decorative line primitive.  Draws a vertical (tate) or horizontal
// (yoko) line built from three sprite pieces of the shared message-box texture
// (msg_box_tex): a head cap, a tail cap, and a middle segment stretched to fill
// the remaining length.
//
//   * DrawCmnLine()     - dispatch on type (0 = vertical, 1 = horizontal).
//   * DrawCmnLineTate() - vertical line   (caps at top/bottom, stretch in Y).
//   * DrawCmnLineYoko() - horizontal line (caps at left/right, stretch in X).
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // DISP_SPRT / SPRT_DAT / CopySprDToSpr / DispSprD
#include "draw_cmn.h"               // DrawCmnLine prototype
#include "tim_dat/msg_box.h"        // msg_box_tex[] frame/cap/line pieces

#include <stdio.h>                  // printf

// ──────────────────────────────────────────────────────────────────────
// The line pieces live at fixed indices of msg_box_tex[]: [0]/[2] vertical
// caps, [9] vertical middle; [4]/[5] horizontal caps, [8] horizontal middle.

static void DrawCmnLineTate(float x, float y, float h, u_char alp, u_int pri);
static void DrawCmnLineYoko(float x, float y, float w, u_char alp, u_int pri);

// ──────────────────────────────────────────────────────────────────────
// Draw a common line of the given type.

void DrawCmnLine(float x, float y, float size, u_char type, u_char alp, u_int pri)
{
    if (type == '\0')
    {
        DrawCmnLineTate(x, y, size, alp, pri);
        return;
    }

    if (type == '\x01')
    {
        DrawCmnLineYoko(x, y, size, alp, pri);
        return;
    }

    printf("Error!! DrawCmnLine()\n");
}

// ──────────────────────────────────────────────────────────────────────
// Vertical line.  Head cap at (x,y), tail cap below it, and (when the line is
// longer than the two 45px caps) a middle piece scaled in Y to bridge them.

static void DrawCmnLineTate(float x, float y, float h, u_char alp, u_int pri)
{
    DISP_SPRT ds;
    float line_h;
    float line_scr;

    line_h = h - 90.0;
    line_scr = 1.0;
    if (line_h < 0.0)
    {
        line_h = 0.0;
    }
    else
    {
        line_scr = line_h / (float)(u_short)msg_box_tex[9].h;
    }

    CopySprDToSpr(&ds, msg_box_tex);
    ds.x = x;
    ds.y = y;
    ds.z = 0xfffff - (pri & 0xfffff);
    ds.pri = pri;
    ds.alpha = alp;
    DispSprD(&ds);

    CopySprDToSpr(&ds, msg_box_tex + 2);
    ds.y = y + line_h + 45.0;
    ds.x = x;
    ds.z = 0xfffff - (pri & 0xfffff);
    ds.pri = pri;
    ds.alpha = alp;
    DispSprD(&ds);

    if (0.0 < line_h)
    {
        CopySprDToSpr(&ds, msg_box_tex + 9);
        ds.csy = y + 45.0;
        ds.scw = 1.0;
        ds.csx = x;
        ds.x = x;
        ds.y = ds.csy;
        ds.z = 0xfffff - (pri & 0xfffff);
        ds.sch = line_scr;
        ds.pri = pri;
        ds.alpha = alp;
        DispSprD(&ds);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Horizontal line.  Head cap at (x,y), tail cap to its right, and (when longer
// than the two 46px caps) a middle piece scaled in X to bridge them.

static void DrawCmnLineYoko(float x, float y, float w, u_char alp, u_int pri)
{
    DISP_SPRT ds;
    float line_w;
    float line_scr;

    line_w = w - 92.0;
    line_scr = 1.0;
    if (line_w < 0.0)
    {
        line_w = 0.0;
    }
    else
    {
        line_scr = line_w / (float)(u_short)msg_box_tex[8].w;
    }

    CopySprDToSpr(&ds, msg_box_tex + 4);
    ds.x = x;
    ds.y = y;
    ds.z = 0xfffff - (pri & 0xfffff);
    ds.pri = pri;
    ds.alpha = alp;
    DispSprD(&ds);

    CopySprDToSpr(&ds, msg_box_tex + 5);
    ds.x = x + line_w + 46.0;
    ds.y = y;
    ds.z = 0xfffff - (pri & 0xfffff);
    ds.pri = pri;
    ds.alpha = alp;
    DispSprD(&ds);

    if (0.0 < line_w)
    {
        CopySprDToSpr(&ds, msg_box_tex + 8);
        ds.csx = x + 46.0;
        ds.sch = 1.0;
        ds.csy = y;
        ds.x = ds.csx;
        ds.y = y;
        ds.z = 0xfffff - (pri & 0xfffff);
        ds.scw = line_scr;
        ds.pri = pri;
        ds.alpha = alp;
        DispSprD(&ds);
    }
}
