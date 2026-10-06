// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/draw_cmn_win.c
//
// Common UI windows and selection widgets, built from the shared message-box
// texture (msg_box_tex) / button texture (btn_tex), the g2d_draw square/sprite
// layer and the message system.
//
//   * DrawCmnWindow()        - a framed translucent background box.
//   * DrawCmnTwoLineWindow() - a wider box with rounded left/right end caps.
//   * DrawCmnFileWindow()    - the file/notebook reader window (title, body,
//                              page counter) for a given file_type / file_id.
//   * DrawCmnYesNoSel()      - the YES/NO selector row with a moving cursor.
//   * DrawCmnSelFrame() / DrawCmnSelYes() / DrawCmnSelNo() - one selection cell.
//   * DrawCmnSelCsr()        - the selection-highlight cursor (stretchable).
//   * DrawCmnTriCsrL/R()     - the left/right triangular page cursors.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // DISP_SPRT / DISP_SQAR / SQAR_DAT / Copy* / Disp*
#include "draw_cmn.h"               // this family's prototypes
#include "message.h"                // GetMsgDataAddr / GetMsgLineLength / GetMsgPageNum / PrintMsg* / PrintNumber_N
#include "tim_dat/msg_box.h"        // msg_box_tex[] frame/cap pieces
#include "tim_dat/btn_sprt_dat.h"   // btn_tex[] selection/cursor pieces

#include "../../sdk/libgraph.h"     // SCE GS register value builders (SCE_GS_SET_*)

#include <string.h>                 // memset

// ──────────────────────────────────────────────────────────────────────
// Framed translucent window.  The background square is inset from the frame;
// the frame itself is four common lines (top, bottom, left, right).

void DrawCmnWindow(u_int pri, float x, float y, float sizew, float sizeh,
                   u_char alpha, u_char max_alpha)
{
    int       line_yoko;            // line alpha accumulator
    DISP_SQAR dsq;
    SQAR_DAT  msg_bg;
    u_char    bg_alpha;
    u_char    frame_alpha;

    memset(&msg_bg, 0, sizeof(SQAR_DAT));

    msg_bg.w = (u_int)(sizew - 32.0);
    msg_bg.x = (int)(x + 16.0);
    msg_bg.y = (int)(y + 13.0);
    msg_bg.h = (u_int)(sizeh - 26.0);

    bg_alpha = (u_char)(((u_int)max_alpha * (u_int)alpha) >> 7);
    msg_bg.pri = pri;
    msg_bg.alpha = bg_alpha;

    CopySqrDToSqr(&dsq, &msg_bg);
    dsq.zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 0);
    dsq.test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    dsq.alpha = bg_alpha;
    DispSqrD(&dsq);

    // frame alpha = alpha * 76 / 128
    line_yoko = (((u_int)alpha * 4 + (u_int)alpha) * 4 - (u_int)alpha) * 4;
    if (line_yoko < 0)
    {
        line_yoko = line_yoko + 0x7f;
    }
    frame_alpha = (u_char)(line_yoko >> 7);

    DrawCmnLine(x, y + 10.0, sizew, 1, frame_alpha, pri);
    DrawCmnLine(x, (y + sizeh) - (float)((u_short)msg_box_tex[6].h + 0xd), sizew, 1, frame_alpha, pri);
    DrawCmnLine(x + 13.0, y, sizeh, 0, frame_alpha, pri);
    DrawCmnLine((x + (sizew - 92.0) + 92.0) - (float)((u_short)msg_box_tex[1].w + 0xd), y, sizeh, 0, frame_alpha, pri);
}

// ──────────────────────────────────────────────────────────────────────
// Wider window with stretched left/right rounded end caps (msg_box_tex[10]/
// [11]) bridged by a translucent background square, plus top/bottom lines.

void DrawCmnTwoLineWindow(u_int pri, float x, float y, float sizew, float sizeh,
                          u_char alpha, u_char max_alpha)
{
    int       line_yoko;
    float     size_ita_h;
    DISP_SPRT ds;
    DISP_SQAR dsq;
    SQAR_DAT  msg_bg;
    u_int     z;
    u_char    cap_alpha;
    u_char    frame_alpha;
    float     cap_y;

    memset(&msg_bg, 0, sizeof(SQAR_DAT));

    msg_bg.w = (u_int)(sizew - 92.0);
    size_ita_h = (sizeh - 26.0) / (float)(u_short)msg_box_tex[10].h;
    msg_bg.x = (int)(x + 46.0);
    msg_bg.y = (int)(y + 13.0);
    msg_bg.h = (u_int)(sizeh - 26.0);
    msg_bg.alpha = (u_char)(((u_int)max_alpha * (u_int)alpha) >> 7);
    msg_bg.pri = pri;
    CopySqrDToSqr(&dsq, &msg_bg);

    z = 0xfffff - (pri & 0xfffff);
    cap_alpha = (u_char)((u_int)alpha * 0x33 >> 6);
    cap_y = y + 13.0;

    // left end cap
    CopySprDToSpr(&ds, msg_box_tex + 10);
    ds.csx = x + 16.0;
    ds.scw = 1.0;
    ds.test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    ds.zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 0);
    ds.csy = cap_y;
    ds.x = ds.csx;
    ds.y = cap_y;
    ds.z = z;
    ds.sch = size_ita_h;
    ds.pri = pri;
    ds.alpha = cap_alpha;
    DispSprD(&ds);

    // right end cap
    CopySprDToSpr(&ds, msg_box_tex + 0xb);
    ds.csx = (x + sizew) - (float)((u_short)msg_box_tex[0xb].w + 0x10);
    ds.test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    ds.zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 0);
    ds.csy = cap_y;
    ds.x = ds.csx;
    ds.y = cap_y;
    ds.z = z;
    ds.scw = 1.0;
    ds.sch = size_ita_h;
    ds.pri = pri;
    ds.alpha = cap_alpha;
    DispSprD(&ds);

    // background square between the caps
    dsq.zbuf = SCE_GS_SET_ZBUF(0x118, 0x0a, 0);
    dsq.test = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 1);
    dsq.alpha = cap_alpha;
    DispSqrD(&dsq);

    // frame alpha = alpha * 76 / 128
    line_yoko = ((u_int)alpha * 0x14 - (u_int)alpha) * 4;
    if (line_yoko < 0)
    {
        line_yoko = line_yoko + 0x7f;
    }
    frame_alpha = (u_char)(line_yoko >> 7);

    DrawCmnLine(x, y + 10.0, sizew, '\x01', frame_alpha, pri);
    DrawCmnLine(x, (y + sizeh) - (float)((u_short)msg_box_tex[6].h + 0xd), sizew, '\x01', frame_alpha, pri);
}

// ──────────────────────────────────────────────────────────────────────
// File / notebook reader window.  msg_type_tbl maps the file_type to the
// message-table id; each file occupies 3 message ids (title, ?, body).

void DrawCmnFileWindow(int file_type, int file_id, u_int pri, u_char alpha, u_char max_alpha)
{
    int msg_type_tbl[5];
    int msg_length;
    int msg_id;
    int msg_type;
    u_char *msg_addr;
    int page;

    msg_type_tbl[0] = 30;
    msg_type_tbl[1] = 32;
    msg_type_tbl[2] = 29;
    msg_type_tbl[3] = 31;
    msg_type_tbl[4] = 27;

    msg_id = file_id * 3;
    msg_type = msg_type_tbl[file_type];

    msg_addr = GetMsgDataAddr(msg_type, msg_id);
    msg_length = GetMsgLineLength(msg_addr, (u_char **)0x0);

    DrawCmnWindow(pri, 36.0, 42.0, 572.0, 373.0, alpha, max_alpha);

    PrintMsg(8, 3, 0x132 - msg_length / 2, 0x46, 1, (u_int)alpha, pri);
    PrintMsg(8, 3, msg_length / 2 + 0x140, 0x46, 1, (u_int)alpha, pri);
    PrintMsg_Arrange(msg_type, msg_id, 0x13f, 0x46, 1, (u_int)alpha, pri, 0, 0, 2);
    PrintMsg_P(msg_type, msg_id + 2, 0x50, 0x6c, 1, (u_int)alpha, pri, 0, 0);
    PrintMsg(8, 3, 0x118, 0x165, 1, (u_int)alpha, pri);
    PrintMsg(8, 1, 0x138, 0x166, 1, (u_int)alpha, pri);
    PrintMsg(8, 3, 0x158, 0x165, 1, (u_int)alpha, pri);

    page = GetNowMsgPageNum();
    PrintNumber_N(page + 1, 1, 0x126, 0x166, '\x01', alpha, 0, '\x01', 1);
    page = GetMsgPageNum(msg_type, msg_id + 2);
    PrintNumber_N(page, 1, 0x146, 0x166, '\x01', alpha, 0, '\x01', 1);
}

// ──────────────────────────────────────────────────────────────────────
// YES/NO selector: cursor over the selected cell, then the two cells.

void DrawCmnYesNoSel(int cursor, float y, u_char alpha, u_int pri)
{
    DrawCmnSelCsr(pri, (float)cursor * 206.0 + 156.0, y - 2.0, alpha, 0.0, 0);
    DrawCmnSelYes(pri, 154.0, y, alpha);
    DrawCmnSelNo(pri, 360.0, y, alpha);
}

// ──────────────────────────────────────────────────────────────────────
// One selection-cell frame: a top piece (btn_tex[7]) and a bottom piece
// (btn_tex[8]); when w is non-zero the pieces are stretched to width w.

void DrawCmnSelFrame(u_int pri, float x, float y, u_char alpha, float w)
{
    DISP_SPRT sel_ds;
    float     scr;

    scr = 1.0;
    if (w != 0.0)
    {
        scr = w / (float)(u_short)btn_tex[7].w;
    }

    CopySprDToSpr(&sel_ds, btn_tex + 7);
    if (w != 0.0)
    {
        sel_ds.sch = 1.0;
        sel_ds.csx = x;
        sel_ds.csy = y;
        sel_ds.scw = scr;
    }
    sel_ds.x = x;
    sel_ds.y = y;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);

    CopySprDToSpr(&sel_ds, btn_tex + 8);
    sel_ds.y = y + 23.0;
    if (w != 0.0)
    {
        sel_ds.sch = 1.0;
        sel_ds.csx = x;
        sel_ds.csy = sel_ds.y;
        sel_ds.scw = scr;
    }
    sel_ds.x = x;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);
}

// ──────────────────────────────────────────────────────────────────────
// The YES cell: a frame plus the centred "YES" message.

void DrawCmnSelYes(u_int pri, float x, float y, u_char alpha)
{
    DrawCmnSelFrame(pri, x, y, alpha, 0.0);
    PrintMsg_Arrange(7, 1, (int)(x + (float)((u_short)btn_tex[7].w >> 1)),
                     (int)(y + 2.0), 0, (u_int)alpha, pri, 0, 0, 2);
}

// ──────────────────────────────────────────────────────────────────────
// The NO cell: a frame plus the centred "NO" message.

void DrawCmnSelNo(u_int pri, float x, float y, u_char alpha)
{
    DrawCmnSelFrame(pri, x, y, alpha, 0.0);
    PrintMsg_Arrange(7, 2, (int)(x + (float)((u_short)btn_tex[7].w >> 1)),
                     (int)(y + 2.0), 0, (u_int)alpha, pri, 0, 0, 2);
}

// ──────────────────────────────────────────────────────────────────────
// Selection highlight cursor: left half (btn_tex[9]) + right half (btn_tex[10]),
// optionally stretched to width w.  flg forces the additive alpha register.

void DrawCmnSelCsr(u_int pri, float x, float y, u_char alpha, float w, u_char flg)
{
    DISP_SPRT sel_ds;
    float     scr;

    scr = 1.0;
    if (w != 0.0)
    {
        scr = w / (float)((u_int)(u_short)btn_tex[9].w << 1);
    }

    CopySprDToSpr(&sel_ds, btn_tex + 9);
    if (w != 0.0)
    {
        sel_ds.sch = 1.0;
        sel_ds.csx = x;
        sel_ds.csy = y;
        sel_ds.scw = scr;
    }
    if (flg != '\0')
    {
        sel_ds.alphar = 0x48;
    }
    sel_ds.x = x;
    sel_ds.y = y;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);

    CopySprDToSpr(&sel_ds, btn_tex + 10);
    sel_ds.x = x + (float)(u_short)btn_tex[9].w;
    if (w != 0.0)
    {
        sel_ds.sch = 1.0;
        sel_ds.csx = x + (float)(u_short)btn_tex[9].w * scr;
        sel_ds.csy = y;
        sel_ds.x = sel_ds.csx;
        sel_ds.scw = scr;
    }
    if (flg != '\0')
    {
        sel_ds.alphar = 0x48;
    }
    sel_ds.y = y;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);
}

// ──────────────────────────────────────────────────────────────────────
// Left page-turn triangle cursor: a drop-shadow piece (btn_tex[12]) under the
// main triangle (btn_tex[11]), tinted by rgb.

void DrawCmnTriCsrL(u_int pri, float x, float y, u_char alpha, u_char rgb)
{
    DISP_SPRT sel_ds;

    CopySprDToSpr(&sel_ds, btn_tex + 0xc);
    sel_ds.x = x - 3.0;
    sel_ds.y = y - 2.0;
    sel_ds.r = rgb;
    sel_ds.g = rgb;
    sel_ds.b = rgb;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);

    CopySprDToSpr(&sel_ds, btn_tex + 0xb);
    sel_ds.x = x;
    sel_ds.y = y;
    sel_ds.r = rgb;
    sel_ds.g = rgb;
    sel_ds.b = rgb;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);
}

// ──────────────────────────────────────────────────────────────────────
// Right page-turn triangle cursor: shadow (btn_tex[14]) under the main
// triangle (btn_tex[13]), tinted by rgb.

void DrawCmnTriCsrR(u_int pri, float x, float y, u_char alpha, u_char rgb)
{
    DISP_SPRT sel_ds;

    CopySprDToSpr(&sel_ds, btn_tex + 0xe);
    sel_ds.x = x - 3.0;
    sel_ds.y = y - 2.0;
    sel_ds.r = rgb;
    sel_ds.g = rgb;
    sel_ds.b = rgb;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);

    CopySprDToSpr(&sel_ds, btn_tex + 0xd);
    sel_ds.x = x;
    sel_ds.y = y;
    sel_ds.r = rgb;
    sel_ds.g = rgb;
    sel_ds.b = rgb;
    sel_ds.alpha = alpha;
    DispSprD(&sel_ds);
}
