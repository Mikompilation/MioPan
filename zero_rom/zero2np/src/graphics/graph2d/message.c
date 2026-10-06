// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/message.c
//
// The font / message / string-rendering system.  Renders Shift-JIS message
// streams (with an embedded 0xF0-0xFF control-code grammar: bank switch,
// position, window size, arrange, colour, page break, extended number, line
// break and terminator) into GS font packets through the g2d_draw.c PK2D ring,
// plus the ASCII / integer / formatted-string helpers, the message-table
// queries, and the digit-drawing helpers.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "message.h"                // this TU's API + message work records
#include "g2d_draw.h"               // Q_WORDDATA / DISP_STR / STR_DAT / PK2D ring
#include "draw_cmn.h"               // DrawCmn* widgets used by some message draws
#include "tim2.h"                   // GS texture / PK2SendVram helpers

#include "../../sdk/libgraph.h"     // SCE GS register / GIFtag value builders
#include "../../miopan/rendering/miopan_renderer.h" // MioPan_RendererDrawTexturedQuad (PC glyph draw)
#include "../../miopan/miopan_memory.h"

#include "../graph3d/ctl/fixed_array.h"     // fixed_array<> template (inlined per TU)

#include <stdarg.h>                 // va_list / va_start / va_arg / va_end
#include <stdio.h>                  // sprintf / printf
#include <string.h>                 // memset / strlen

#include "../../system/os/system.h" // GetLanguage

// ──────────────────────────────────────────────────────────────────────
// Cross-module helpers (each via its owning module's header).

#include "../draw_env.h"                        // SetDrawEnv / SetTexaRegister (+ DRAW_ENV_5)
#include "../../ingame/event/prg/ev_get.h"      // Get1Byte / Get2Byte / EvBinChangeAddr4
#include "../../system/mc/prg/mc_check_card.h"  // GetAccessMemoryCardPort
#include "../../ingame/plyr/player.h"           // ClearPlyrMoveStatus / SetPlyrAnime
#include "../../ingame/enemy/enemy.h"           // EnemyAnimLock / EnemyAnimUnlock
#include "../../ingame/plyr/sister.h"           // IsSisWrk / GetSisStandAnm / SetSisterAnime

// ──────────────────────────────────────────────────────────────────────
// Cross-module data tables (each via its owning module's header).

#include "graph2d.h"                // fntdat[] font texture descriptors (g2d_main.c)
#include "msg_col_tbl.h"            // msg_col[] colour palette
#include "msg_def_dat.h"            // msg_def_data[] default positions
#include "msg_disp_dat.h"           // msg_disp_data[] per-type def/win ids
#include "msg_win_dat.h"            // msg_win_data[] default window rects
#include "../../system/pad/pad.h"   // paddat pad-data table

// gInterlace lives in g2d_draw.c (declared in g2d_draw.h, included above).

// ──────────────────────────────────────────────────────────────────────
// Font tables.  The glyph-index tables (ascii_font_tbl*, wbyte_font_tbl0..5),
// the per-glyph advance-width tables (font_w_b0..4, static to this TU) and the
// message-box sprite descriptors (mesbox) are real FF2 data recovered from the
// original PS2 ELF (SLES_523.84) by tools/extract_font_data.py — see
// font_data.h.  These were previously zero-filled stubs, so no glyph resolved
// and text rendered nothing.

#include "font_data.h"              // ascii_font_tbl* / wbyte_font_tbl* / font_w_b* / mesbox

static int   *mes_ex_nums[1];       // sdata 3f2fe0 : extended-number arg pointers

static DRAW_ENV_5 *change_msg_env;  // sbss  3f4e40 : optional draw-env override
static int    msg_type_max_tbl[83] = // rdata 3bfbd0 : per-type max message id
{
    14, 300, 300, 11, 1, 1, 304, 10,
    4, 347, 29, 8, 14, 61, 31, 31,
    31, 51, 31, 31, 31, 31, 31, 31,
    106, 17, 6, 30, 17, 120, 126, 78,
    126, 361, 10, 6, 18, 150, 82, 100,
    6, 50, 44, 58, 58, 1, 5, 1,
    120, 88, 1, 11, 1, 1, 5, 1,
    2, 1, 8, 543, 92, 17, 1, 1,
    1, 56, 6, 7, 7, 10, 2, 9,
    40, 40, 240, 7, 9, 558, 31, 1,
    58, 17, 58,
};

static MES_DAT       msdat;         // bss   4b63f8 : the active message record
u_char              *save_mes_addr; // sdata 3f3038 : saved message stream pointer
static MES_WRK       mes_wrk;       // sbss  3f4e48 : font texture-bank state
static MSG_DISP_CTRL msg_disp_ctrl; // sbss  3f4e50 : message-box display control

// the per-TU fixed_array<> template instances (_fixed_array_assert /
// _fixed_array_verifyrange<T>) are emitted here by the compiler; see
// "../graph3d/ctl/fixed_array.h" for their definitions.

// ──────────────────────────────────────────────────────────────────────
// Engine assert shim: see common/utility2.h.
#include "../../common/utility2.h" // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal

// ──────────────────────────────────────────────────────────────────────
// Internal (static) helpers — forward declarations.

static Q_WORDDATA *SetFontPacketHeader(Q_WORDDATA *pbuf, int num, int type, u_char alp);
static Q_WORDDATA *SetFont(Q_WORDDATA *pbuf, int pri, int type, int no, int x, int y,
                           u_char r, u_char g, int b, int a);
static Q_WORDDATA *SetFontTex(Q_WORDDATA *pbuf, int flg, int bank);
static Q_WORDDATA *SetFontPat(Q_WORDDATA *pbuf, int type, int pri, int fn, int x, int y,
                              int fw, u_char r, int g, int b, int a);
static Q_WORDDATA *SetUnderLine(Q_WORDDATA *pbuf, int sw, int pri, int x, int y, int fw,
                                u_char r, u_char g, int b, int a);
static void MsgExeCtrlInit(DISP_STR *data, MSG_EXE_CTRL *ctrl, u_char disp_type);
static void MesBankExe(DISP_STR *data, MSG_EXE_CTRL *ctrl);
static void MesSetExe(DISP_STR *data, MSG_EXE_CTRL *ctrl);
static void MesWinSizeExe(DISP_STR *data, MSG_WIN_DAT *win);
static void MesColExe(DISP_STR *data, MSG_EXE_CTRL *ctrl);
static void MesArrangeExe(DISP_STR *data, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl);
static void MesRetExe(DISP_STR *data, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl);
static void MesExNumExe(DISP_STR *data, MSG_EXE_CTRL *ctrl, Q_WORDDATA **ppbuf);
static int  GetAccessMcSlot_Msg(void);
static void SetMsgFont(Q_WORDDATA **pbuf, DISP_STR *disp_addr, int nbank, u_char nfn,
                       u_char nfw, u_char nfh);
static void MesKeyCheck(void);

static int  (*mes_ex_num_tbl[1])() = {GetAccessMcSlot_Msg}; // sdata 3f2fe8 : extended-number getters

// ──────────────────────────────────────────────────────────────────────
// Reset the whole message subsystem: clear the display control, mark the
// font texture bank invalid, and drop any draw-env override.

void InitMessage(void)
{
    MsgDispCtrlInit();
    mes_wrk.texbank = 0xff;
    change_msg_env = (DRAW_ENV_5 *)0;
}

// ──────────────────────────────────────────────────────────────────────
// Lightweight re-init (effects path): only invalidate the font texture bank.

void InitMessageEF(void)
{
    mes_wrk.texbank = 0xff;
}

// ──────────────────────────────────────────────────────────────────────
// Draw an integer at (x,y) with default grey colour (bank 0 ASCII).

void SetInteger(float x, float y, int num)
{
    SetInteger2(0, x, y, 0, 0x80, 0x80, 0x80, num);
}

// ──────────────────────────────────────────────────────────────────────
// Format an integer with "%5d" and draw it as an ASCII string (no alpha).

void SetInteger2(int pri, float x, float y, int type, u_char r, u_char g, u_char b, int num)
{
    char cwo[16];

    sprintf(cwo, "%5d", num);
    SetASCIIString2(pri, x, y, type, r, g, b, cwo);
}

// ──────────────────────────────────────────────────────────────────────
// Format an integer with "%d" and draw it as an ASCII string (with alpha).

void SetInteger3(int pri, float x, float y, int type, u_char r, u_char g, u_char b, u_char a, int num)
{
    char cwo[16];

    sprintf(cwo, "%d", num);
    SetASCIIString3(pri, x, y, type, r, g, b, a, cwo);
}

// ──────────────────────────────────────────────────────────────────────
// Draw an ASCII string at (x,y) with default grey colour and full alpha.

void SetASCIIString(float x, float y, char *str)
{
    SetASCIIString3(0, x, y, 0, 0x80, 0x80, 0x80, 0x80, str);
}

// ──────────────────────────────────────────────────────────────────────
// Draw an ASCII string with explicit colour; alpha defaults to full.

void SetASCIIString2(int pri, float x, float y, int type, u_char r, u_char g, u_char b, char *str)
{
    SetASCIIString3(pri, x, y, type, r, g, b, 0x80, str);
}

// ──────────────────────────────────────────────────────────────────────
// Install / clear a draw-env override used by the message font drawing.

void MessageChangeDrawEnv(DRAW_ENV_5 *p_change_env)
{
    change_msg_env = p_change_env;
}

// ──────────────────────────────────────────────────────────────────────
// Draw an ASCII string (colour + alpha).  Half-width ASCII glyphs from
// ascii_font_tbl / ascii_font_tbl2; the dakuten (0xDE) / handakuten (0xDF)
// combining marks fold into the preceding kana glyph.

void SetASCIIString3(int pri, float x, float y, int type, u_char r, u_char g, u_char b, u_char a, char *str)
{
    int            i;
    int            n;
    int            code;
    u_char         c1;
    u_char         c2;
    u_short       *tbl;
    int            ft;
    int            offset;
    int            xoff;
    Q_WORDDATA    *pbuf;
    Q_WORDDATA    *pbuf_top;

    i = 0;          // glyph count
    offset = 0;     // byte index into str
    xoff = 0;       // pixel x cursor
    c1 = *str;
    while (c1 != '\0')
    {
        offset = offset + (int)((u_char)(str[offset + 1] + 0x22) < 2) + 1;
        c1 = str[offset];
        i++;
    }

    offset = 0;
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontPacketHeader(pbuf_top, i, (type != 0), 0x7f);

    if (*str != '\0')
    {
        i = 10;
        if ((type & 2) == 0)
        {
            i = 0;
        }
        ft = i;
        c2 = (u_char)str[1];
        do
        {
            code = (u_char)str[offset];
            if (c2 == 0xde)
            {
                n = code + 0xb;
                offset = offset + 1;
                if (!(((u_char)(code + 0x4d)) < 0x1c))
                {
                    n = code - 0x20;
                }
            }
            else if (c2 == 0xdf)
            {
                n = code + 0x10;
                offset = offset + 1;
                if (!(((u_char)(code + 0x36)) < 5))
                {
                    n = code - 0x20;
                }
            }
            else
            {
                n = 0;
                if (((u_char)(code + 0xe0)) < 0xbe)
                {
                    n = code - 0x20;
                }
            }

            if ((type & 1) == 0)
            {
                tbl = ascii_font_tbl2;
            }
            else
            {
                tbl = ascii_font_tbl;
            }
            code = (tbl[n] + ft) & 0xff;
            pbuf = SetFont(pbuf, pri, type, code, (int)(x + (float)xoff), (int)y, r, g, b, a);
            if ((type & 1) == 0)
            {
                xoff = xoff + 0xc;
            }
            else
            {
                xoff = xoff + font_w_b0[code];
            }
            offset = offset + 1;
            if (str[offset] == '\0')
            {
                break;
            }
            c2 = (u_char)str[offset + 1];
        } while (1);
    }

    EndPK2Dbuf(pbuf);
    AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
}

// ──────────────────────────────────────────────────────────────────────
// Draw an ASCII string with per-glyph wide-byte bank dispatch (banks 1..5
// via SetFontTex) when proportional rendering (type & 1) is requested.

void SetASCIIString4(int pri, float x, float y, int type, u_char r, u_char g, u_char b, char *str)
{
    int            i;          // byte index into str
    int            code;       // raw byte / glyph low byte (no)
    u_char         c0;
    u_char         c1;
    u_char         c2;
    u_short       *tbl;
    u_short        nfn;        // 16-bit glyph (bank<<8 | no)
    int            offset;     // pixel x cursor
    int            xoff;       // glyph-table index
    Q_WORDDATA    *pbuf;
    Q_WORDDATA    *pbuf_top;
    int            ft;
    int            y_00;

    offset = 0;
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    i = 0;
    pbuf = pbuf_top;
    if (((type ^ 1) & 1) != 0)
    {
        pbuf = SetFontPacketHeader(pbuf_top, 0, 0, 0x7f);
    }
    ft = 10;
    if ((type & 2) == 0)
    {
        ft = 0;
    }
    if ((type & 1) == 0)
    {
        tbl = ascii_font_tbl2;
    }
    else
    {
        tbl = ascii_font_tbl;
    }
    if (*str == '\0')
    {
        EndPK2Dbuf(pbuf);
        AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
        return;
    }

    y_00 = (int)y;
    c1 = (u_char)str[1];
    do
    {
        code = (u_char)str[i];
        if (c1 == 0xde)
        {
            xoff = code + 0xb;
            i = i + 1;
            if (!(((u_char)(code + 0x4d)) < 0x1c))
            {
                xoff = code - 0x20;
            }
        }
        else if (c1 == 0xdf)
        {
            xoff = code + 0x10;
            i = i + 1;
            if (!(((u_char)(code + 0x36)) < 5))
            {
                xoff = code - 0x20;
            }
        }
        else
        {
            xoff = 0;
            if (((u_char)(code + 0xe0)) < 0xbe)
            {
                xoff = code - 0x20;
            }
        }

        nfn = (u_short)(tbl[xoff] + ft);
        code = nfn & 0xff;
        if ((nfn >> 8) == 0xf0)
        {
            if ((type & 1) != 0)
            {
                pbuf = SetFontTex(pbuf, 1, 2);
            }
            pbuf = SetFont(pbuf, pri, type, code, (int)(x + (float)offset), y_00, r, g, b, 0x80);
            if ((type & 1) != 0)
            {
                offset = offset + font_w_b1[code];
            }
            else
            {
                offset = offset + 0xc;
            }
        }
        else if ((nfn >> 8) == 0xf1)
        {
            if ((type & 1) != 0)
            {
                pbuf = SetFontTex(pbuf, 1, 3);
            }
            pbuf = SetFont(pbuf, pri, type, code, (int)(x + (float)offset), y_00, r, g, b, 0x80);
            if ((type & 1) != 0)
            {
                offset = offset + font_w_b2[code];
            }
            else
            {
                offset = offset + 0xc;
            }
        }
        else if ((nfn >> 8) == 0xf2)
        {
            if ((type & 1) != 0)
            {
                pbuf = SetFontTex(pbuf, 1, 4);
            }
            pbuf = SetFont(pbuf, pri, type, code, (int)(x + (float)offset), y_00, r, g, b, 0x80);
            if ((type & 1) != 0)
            {
                offset = offset + font_w_b3[code];
            }
            else
            {
                offset = offset + 0xc;
            }
        }
        else if ((nfn >> 8) == 0xf3)
        {
            if ((type & 1) != 0)
            {
                pbuf = SetFontTex(pbuf, 1, 5);
            }
            pbuf = SetFont(pbuf, pri, type, code, (int)(x + (float)offset), y_00, r, g, b, 0x80);
            if ((type & 1) != 0)
            {
                offset = offset + font_w_b4[code];
            }
            else
            {
                offset = offset + 0xc;
            }
        }
        else
        {
            if ((type & 1) != 0)
            {
                pbuf = SetFontTex(pbuf, 1, 1);
            }
            pbuf = SetFont(pbuf, pri, type, code, (int)(x + (float)offset), y_00, r, g, b, 0x80);
            if ((type & 1) == 0)
            {
                offset = offset + 0xc;
            }
            else
            {
                offset = offset + font_w_b0[code];
            }
        }

        i = i + 1;
        if (str[i] == '\0')
        {
            break;
        }
        c1 = (u_char)str[i + 1];
    } while (1);

    EndPK2Dbuf(pbuf);
    AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
}

// ──────────────────────────────────────────────────────────────────────
// Wide (full-width kanji) string draw — stub in this prototype build.

void SetWString2(int pri, float x, float y, u_char r, u_char g, u_char b, char *str)
{
}

// ──────────────────────────────────────────────────────────────────────
// printf-style ASCII string draw.  Walks the format string itself (no libc
// vfprintf): scans for '%', honours an optional field width, formats the
// argument via sprintf into a scratch buffer (cwo) and left-pads with '+'
// to the requested width before appending to the assembled string (str).

void SetString(float x, float y, char *fmt, ...)
{
    va_list  ap;
    int      i;
    int      n;
    int      len;
    char     cw;
    char     cwo[256];
    char     str[256];
    char    *buf;

    va_start(ap, fmt);
    str[0] = '\0';
    buf = str;
    cw = *fmt;
    do
    {
        while (1)
        {
            if (cw == '\0')
            {
                *buf = '\0';
                SetASCIIString(x, y, str);
                va_end(ap);
                return;
            }
            if (cw == '%')
            {
                break;
            }
            *buf = cw;
            buf = buf + 1;
            fmt = fmt + 1;
            cw = *fmt;
        }

        fmt = fmt + 1;
        n = -1;
        cw = *fmt;
        i = 0;
        do
        {
            len = n;
            if (cw == 'd')
            {
                sprintf(cwo, "%d", va_arg(ap, int));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len--;
                        *buf = '+';
                        buf++;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf++;
                    len++;
                }
                i = 1;
                fmt++;
            }
            else if (cw == '%')
            {
                *buf = '%';
                buf++;
                fmt++;
                i = 1;
            }
            else if (cw == 'c')
            {
                sprintf(cwo, "%c", va_arg(ap, int));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len = len - 1;
                        *buf = '+';
                        buf = buf + 1;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf = buf + 1;
                    len = len + 1;
                }
                i = 1;
                fmt = fmt + 1;
            }
            else if (cw == 's')
            {
                sprintf(cwo, "%s", va_arg(ap, char *));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len = len - 1;
                        *buf = '+';
                        buf = buf + 1;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf = buf + 1;
                    len = len + 1;
                }
                i = 1;
                fmt = fmt + 1;
            }
            else
            {
                n = cw - '0';
                fmt = fmt + 1;
                if (((u_char)n) < 10)
                {
                    cw = *fmt;
                    while ((u_int)(cw - '0') < 10)
                    {
                        fmt = fmt + 1;
                        n = n * 10 + cw - '0';
                        cw = *fmt;
                    }
                }
            }
        } while (i == 0);
        cw = *fmt;
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// printf-style ASCII string draw with colour/priority.  As SetString but
// supports "%f" (".02f") as well, pads with spaces, and forwards colour to
// SetASCIIString2.

void SetString2(int pri, float x, float y, int type, u_char r, u_char g, u_char b, char *fmt, ...)
{
    va_list  ap;
    int      i;
    int      n;
    int      len;
    char     cw;
    char     cwo[256];
    char     str[256];
    char    *buf;

    va_start(ap, fmt);
    str[0] = '\0';
    buf = str;
    cw = *fmt;
    do
    {
        while (1)
        {
            if (cw == '\0')
            {
                *buf = '\0';
                SetASCIIString2(pri, x, y, type, r, g, b, str);
                va_end(ap);
                return;
            }
            if (cw == '%')
            {
                break;
            }
            *buf = cw;
            buf = buf + 1;
            fmt = fmt + 1;
            cw = *fmt;
        }

        fmt = fmt + 1;
        n = -1;
        cw = *fmt;
        i = 0;
        do
        {
            len = n;
            if (cw == 'd')
            {
                sprintf(cwo, "%d", va_arg(ap, int));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len = len - 1;
                        *buf = ' ';
                        buf = buf + 1;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf = buf + 1;
                    len = len + 1;
                }
                i = 1;
                fmt = fmt + 1;
            }
            else if (cw == 'f')
            {
                sprintf(cwo, "%.02f", va_arg(ap, double));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len = len - 1;
                        *buf = ' ';
                        buf = buf + 1;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf = buf + 1;
                    len = len + 1;
                }
                i = 1;
                fmt = fmt + 1;
            }
            else if (cw == 's')
            {
                sprintf(cwo, "%s", va_arg(ap, char *));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len = len - 1;
                        *buf = ' ';
                        buf = buf + 1;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf = buf + 1;
                    len = len + 1;
                }
                i = 1;
                fmt = fmt + 1;
            }
            else if (cw == '%')
            {
                *buf = '%';
                buf = buf + 1;
                fmt = fmt + 1;
                i = 1;
            }
            else if (cw == 'c')
            {
                sprintf(cwo, "%c", va_arg(ap, int));
                if (((int)strlen(cwo) < n) && (len = len - (int)strlen(cwo), 0 < len))
                {
                    do
                    {
                        len = len - 1;
                        *buf = ' ';
                        buf = buf + 1;
                    } while (len != 0);
                }
                len = 0;
                while ((int)strlen(cwo) > len)
                {
                    *buf = cwo[len];
                    buf = buf + 1;
                    len = len + 1;
                }
                i = 1;
                fmt = fmt + 1;
            }
            else
            {
                n = cw - '0';
                fmt = fmt + 1;
                if (((u_char)n) < 10)
                {
                    cw = *fmt;
                    while ((u_int)(cw - '0') < 10)
                    {
                        fmt = fmt + 1;
                        n = n * 10 + cw - '0';
                        cw = *fmt;
                    }
                }
            }
        } while (i == 0);
        cw = *fmt;
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// Emit the GIFtag/texture-state header for a font packet (delegates to the
// texture-bank setter; num/alp are unused in this build).

static Q_WORDDATA *SetFontPacketHeader(Q_WORDDATA *pbuf, int num, int type, u_char alp)
{
    return SetFontTex(pbuf, 1, type);
}

// ──────────────────────────────────────────────────────────────────────
// Append one glyph sprite to the packet, picking the font cell size from
// the proportional/normal type flag, then delegating to SetFontPat.

static Q_WORDDATA *SetFont(Q_WORDDATA *pbuf, int pri, int type, int no, int x, int y,
                           u_char r, u_char g, int b, int a)
{
    int fw;

    fw = 0x18;
    if (type == 0)
    {
        fw = 0xc;
    }
    return SetFontPat(pbuf, type, pri, no, x, y, fw, r, g, b & 0xff, a & 0xff);
}

// ──────────────────────────────────────────────────────────────────────
// Build the TEX0 / register-load packet selecting font texture bank `bank`.
// CLUT-bearing banks (psm 0x14/0x2c/0x24) get a CSM/CLD load; others a plain
// TEX0.  Records the active bank in mes_wrk.

static Q_WORDDATA *SetFontTex(Q_WORDDATA *pbuf, int flg, int bank)
{
    sceGsTex0 Load;
    sceGsTex0 Change;
    sceGsTex0 sgt;
    int       n;
    u_long    tex0;

    tex0 = fntdat[bank].tex0;
    n = ((sceGsTex0 *)&tex0)->PSM;
    mes_wrk.texbank = (u_char)bank;
    MioPan_RendererSetFontTexture(bank, (sceGsTex0 *)&fntdat[bank].tex0);
    // A+D GIFtag: 3 register writes (TEXFLUSH, then TEX0_1 [+ TEX0_1 / pad]).
    pbuf->ul64[0] = SCE_GIF_SET_TAG(3, 1, 0, 0, SCE_GIF_PACKED, 1);
    pbuf->ul64[1] = SCE_GIF_PACKED_AD;
    *(u_long *)((uintptr_t)pbuf + 0x18) = SCE_GS_TEXFLUSH;
    pbuf[1].ul64[0] = 0;
    
    if (((n == 0x14) || (n == 0x2c)) || (n == 0x24))
    {
        // CSM/CLUT-bearing PSM: split TEX0 (with CLD/CSM bits) + the raw word.
        pbuf[2].ul64[0] = tex0 & 0xfffffffc0fffff | 0x2000000001300000;
        *(u_long *)((uintptr_t)pbuf + 0x28) = SCE_GS_TEX0_1;
        *(u_long *)((uintptr_t)pbuf + 0x38) = SCE_GS_TEX0_1;
        pbuf[3].ul64[0] = tex0 & 0xffffffffffffff;
    }
    else
    {
        *(u_long *)((uintptr_t)pbuf + 0x28) = SCE_GS_TEX0_1;
        pbuf[2].ul64[0] = tex0;
        *(u_long *)((uintptr_t)pbuf + 0x38) = 0x7f;          // A+D address 0x7f = ignored (pad write)
        pbuf[3].ul64[0] = 0;
    }
    return pbuf + 4;
}

// ──────────────────────────────────────────────────────────────────────
// Set the message-layer draw env (alpha/tex1/clamp/test/zbuf).  Uses the
// installed override if any, else the built-in font-text 5-register env.

void SetFontEnv(void)
{
    DRAW_ENV_5 env;

    if (change_msg_env == (DRAW_ENV_5 *)0)
    {
        env.alpha = SCE_GS_SET_ALPHA(0, 1, 0, 1, 0x80);
        env.tex1  = SCE_GS_SET_TEX1(1, 0, 0, 5, 0, 0, 0);
        env.clamp = SCE_GS_SET_CLAMP(SCE_GS_CLAMP_REPEAT, SCE_GS_CLAMP_REPEAT, 0, 0, 0, 0);
        env.test  = SCE_GS_SET_TEST(1, 1, 0, 0, 0, 0, 1, 2);
        env.zbuf  = SCE_GS_SET_ZBUF(0x118, 0x0a, 0);
        SetDrawEnv(0, &env);
    }
    else
    {
        SetDrawEnv(0, change_msg_env);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Emit the sprite primitive (UV + XYZ vertices) for one font cell `fn` at
// (x,y) into the packet.  Cell geometry differs for proportional (type!=0,
// 0x15 cells/row, 24px) and normal (type==0, 0x2a cells/row, 12px) fonts.

static Q_WORDDATA *SetFontPat(Q_WORDDATA *pbuf, int type, int pri, int fn, int x, int y,
                              int fw, u_char r, int g, int b, int a)
{
    int    px2;
    int    py2;
    u_int  tw1;
    u_int  th1;
    u_int  tw2;
    u_int  th2;
    u_int  Font_W;
    u_int  Font_H;
    u_int  Num_W;
    int    div;
    int    dx;
    int    dy;
    int    dw;
    int    dh;
    u_int  z;
    int    off_w;
    int    off_h;

    z = (x + 0x6c0) * 0x10;
    if (type != 0)
    {
        Num_W = 0x18;
        Font_H = 0x15;
        Font_W = 0x18;
        off_h = 0;
    }
    else
    {
        Font_H = 0x2a;
        Num_W = 0xe;
        off_h = 8;
        Font_W = 0xc;
    }
    off_w = (type == 0);
    div = 2;
    if (gInterlace == 0)
    {
        div = 1;
    }
    if (div == 0)
    {
        trap(7);
    }

    // textured sprite (abe, FST), 2 verts; REGS: RGBAQ, UV, XYZF2, UV, XYZF2.
    pbuf->ul64[0] = SCE_GIF_SET_TAG(1, 1, 1, SCE_GS_SET_PRIM(SCE_GS_PRIM_SPRITE, 0, 1, 0, 1, 0, 1, 0, 0), 0, 5);
    pbuf->ul64[1] = (u_long)SCE_GIF_PACKED_RGBAQ | ((u_long)SCE_GIF_PACKED_UV << 4)
                  | ((u_long)SCE_GIF_PACKED_XYZF2 << 8) | ((u_long)SCE_GIF_PACKED_UV << 12)
                  | ((u_long)SCE_GIF_PACKED_XYZF2 << 16);
    *(u_int *)((uintptr_t)pbuf + 0x1c) = a & 0xff;
    py2 = 0xfffff - (pri & 0xfffff);
    *(u_int *)(pbuf + 1) = (u_int)r;
    *(u_int *)((uintptr_t)pbuf + 0x14) = g & 0xff;
    *(u_int *)((uintptr_t)pbuf + 0x18) = b & 0xff;
    *(u_int *)((uintptr_t)pbuf + 0x2c) = 0;
    *(u_int *)((uintptr_t)pbuf + 0x28) = 0;
    dy = (fn / (uintptr_t)Font_H) * Num_W;
    dx = (fn % (uintptr_t)Font_H) * Font_W;
    px2 = ((y + -0xe0) / div + 0x800) * 0x10;
    pbuf[2].ui32[0] = (dx + off_h) * 0x10;
    *(u_int *)((uintptr_t)pbuf + 0x24) = (dy + off_w) * 0x10;
    *(u_int *)((uintptr_t)pbuf + 0x3c) = 0;
    pbuf[3].ui32[0] = z;
    *(int *)((uintptr_t)pbuf + 0x38) = py2;
    *(int *)((uintptr_t)pbuf + 0x34) = px2;
    pbuf[4].ui32[0] = (dx + fw + off_h) * 0x10;
    *(u_int *)((uintptr_t)pbuf + 0x44) = ((dy + Num_W) - off_w) * 0x10;
    *(u_int *)((uintptr_t)pbuf + 0x4c) = 0;
    *(u_int *)((uintptr_t)pbuf + 0x48) = 0;
    pbuf[5].ui32[0] = z + fw * 0x10;
    *(u_int *)((uintptr_t)pbuf + 0x54) = px2 + (Num_W / div) * 0x10;
    *(int *)((uintptr_t)pbuf + 0x58) = py2;
    *(u_int *)((uintptr_t)pbuf + 0x5c) = 0;

    // PC render bridge.  The GS SPRITE packet above is built for a real GS and
    // is discarded by the host's no-op DMA ring, so emit the glyph through the
    // MioPan SDL renderer as well — the same retrofit DispSprD/DispSqrD carry.
    // The glyph's screen quad is (x,y)..(x+fw, y+Num_W/div) in progressive
    // pixels (div==1 on PC); its source cell is (dx,dy) in the active font
    // bank's atlas, cropped to the glyph advance width `fw`.  Vertices are
    // TL, TR, BL, BR to match MioPan_RendererDrawTexturedQuad's index order,
    // with UVs in texels (the renderer normalises by the atlas size).
    {
        float render_xy[8];
        float render_uv[8];
        int   dhh = (int)(Num_W / (u_int)div);
        int   su0 = dx + off_h;
        int   sv0 = dy + off_w;
        int   su1 = dx + fw + off_h;
        int   sv1 = dy + (int)Num_W - off_w;

        render_xy[0] = (float)x;        render_xy[1] = (float)y;          // TL
        render_xy[2] = (float)(x + fw); render_xy[3] = (float)y;          // TR
        render_xy[4] = (float)x;        render_xy[5] = (float)(y + dhh);  // BL
        render_xy[6] = (float)(x + fw); render_xy[7] = (float)(y + dhh);  // BR

        render_uv[0] = (float)su0; render_uv[1] = (float)sv0;
        render_uv[2] = (float)su1; render_uv[3] = (float)sv0;
        render_uv[4] = (float)su0; render_uv[5] = (float)sv1;
        render_uv[6] = (float)su1; render_uv[7] = (float)sv1;

        MioPan_RendererDrawFontQuad(
            render_xy, render_uv, (u_char)r, (u_char)(g & 0xff),
            (u_char)(b & 0xff), (u_char)(a & 0xff));
    }

    return pbuf + 6;
}

// ──────────────────────────────────────────────────────────────────────
// Emit a filled-rect (underline) sprite under a glyph run, `fw` wide, when
// the switch flag `sw` is set; otherwise pass the buffer through untouched.

static Q_WORDDATA *SetUnderLine(Q_WORDDATA *pbuf, int sw, int pri, int x, int y, int fw,
                                u_char r, u_char g, int b, int a)
{
    int   px2;
    int   py2;
    int   div;
    int   dx;
    int   dw;
    int   dh;
    u_int z;
    Q_WORDDATA *ret;

    z = (x + 0x6c0) * 0x10;
    ret = pbuf;
    if (sw != 0)
    {
        div = 2;
        if (gInterlace == 0)
        {
            div = 1;
        }
        py2 = 0xfffff - (pri & 0xfffff);
        ret = pbuf + 4;
        if (div == 0)
        {
            trap(7);
        }

        // line primitive (abe), 2 verts; REGS: RGBAQ, XYZF2, XYZF2.
        pbuf->ul64[0] = SCE_GIF_SET_TAG(1, 1, 1, SCE_GS_SET_PRIM(SCE_GS_PRIM_LINE, 0, 0, 0, 1, 0, 0, 0, 0), 0, 3);
        pbuf->ul64[1] = (u_long)SCE_GIF_PACKED_RGBAQ | ((u_long)SCE_GIF_PACKED_XYZF2 << 4) | ((u_long)SCE_GIF_PACKED_XYZF2 << 8);

        *(u_int *)((uintptr_t)pbuf + 0x1c) = a & 0xff;
        *(u_int *)(pbuf + 1) = (u_int)r;
        *(u_int *)((uintptr_t)pbuf + 0x14) = (u_int)g;
        *(u_int *)((uintptr_t)pbuf + 0x18) = b & 0xff;
        pbuf[2].ui32[0] = z;
        *(u_int *)((uintptr_t)pbuf + 0x2c) = 0;
        *(int *)((uintptr_t)pbuf + 0x28) = py2;
        dx = (0x18 / div + (y + -0xe0) / div + 0x800) * 0x10 + -8;
        *(int *)((uintptr_t)pbuf + 0x24) = dx;
        pbuf[3].ui32[0] = z + fw * 0x10;
        *(int *)((uintptr_t)pbuf + 0x34) = dx;
        *(int *)((uintptr_t)pbuf + 0x38) = py2;
        *(u_int *)((uintptr_t)pbuf + 0x3c) = 0;
    }
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// Count printable glyphs in a message stream, walking the 0xF0-0xFF control
// codes (each consumes a fixed number of operand bytes); stops at 0xFF.

int GetStrLength(u_char *str)
{
    u_char *c;
    int     num;
    int     loop;

    num = 0;
    loop = 1;
    c = str;
    do
    {
        switch (*c)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
            c = c + 2;
            num = num + 1;
            break;
        case 0xf4:
            c = c + 1;
            num = num + 1;
            break;
        case 0xf5:
        case 0xfd:
            c = c + 4;
            break;
        case 0xf6:
            c = c + 5;
            break;
        case 0xf7:
        case 0xf8:
            c = c + 2;
            break;
        case 0xf9:
        case 0xfb:
        case 0xfc:
            break;
        case 0xfa:
        case 0xfe:
            c = c + 1;
            break;
        case 0xff:
            c = c + 1;
            loop = 0;
            break;
        default:
            c = c + 1;
            num = num + 1;
            break;
        }
        if (loop == 0)
        {
            return num;
        }
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// Sum the pixel width of a message stream, using the per-bank glyph-width
// tables for the wide-byte banks (0xF0-0xF3) and bank 0 for plain glyphs.

int GetStrWidthMain(u_char *str, int type)
{
    u_char *c;
    u_char  n;
    int     loop;
    int     w;

    loop = 1;
    w = 0;
    c = str;
    do
    {
        n = *c;
        switch ((u_int)n)
        {
        case 0xf0:
            w = w + font_w_b1[c[1]];
            c = c + 2;
            break;
        case 0xf1:
            w = w + font_w_b2[c[1]];
            c = c + 2;
            break;
        case 0xf2:
            w = w + font_w_b3[c[1]];
            c = c + 2;
            break;
        case 0xf3:
            w = w + font_w_b4[c[1]];
            c = c + 2;
            break;
        case 0xf5:
        case 0xfd:
            c = c + 4;
            break;
        case 0xf6:
            c = c + 5;
            break;
        case 0xf7:
        case 0xf8:
            c = c + 2;
            break;
        case 0xf9:
        case 0xfb:
        case 0xfc:
            break;
        case 0xfa:
        case 0xfe:
        case 0xff:
            c = c + 1;
            loop = 0;
            break;
        case 0xf4:
        default:
            w = w + font_w_b0[n];
            c = c + 1;
            break;
        }
        if (loop == 0)
        {
            return w;
        }
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// Pixel width of a message stream in the default (non-proportional) font.

int GetStrWidth(u_char *str)
{
    return GetStrWidthMain(str, 0);
}

// ──────────────────────────────────────────────────────────────────────
// Copy a STR_DAT caller record into a DISP_STR draw record and clear the
// runtime fields (state / wide / cursor).

void CopyStrDToStr(DISP_STR *s, STR_DAT *d)
{
    s->pos_x  = d->pos_x;
    s->str    = d->str;
    s->pos_y  = d->pos_y;
    s->type   = d->type;
    s->r      = d->r;
    s->g      = d->g;
    s->b      = d->b;
    s->alpha  = d->alpha;
    s->st     = 0;
    s->pri    = d->pri;
    s->x_wide = 0;
    s->y_wide = 0;
    s->csr    = 0;
}

// ──────────────────────────────────────────────────────────────────────
// Legacy V2 message-set entry — stub (returns 0) in this prototype build.

int SetMessageV2_2(DISP_STR *s)
{
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Reset the message-box display control block.

void MsgDispCtrlInit(void)
{
    msg_disp_ctrl.cnt = 0;
    msg_disp_ctrl.pass_btn_wait = '\0';
    msg_disp_ctrl.disp_state = '\0';
    msg_disp_ctrl.init_flg = '\0';
}

// ──────────────────────────────────────────────────────────────────────
// Draw a message by (type,id) into its default window: validate, pull the
// default text/window records, point at the stream and hand off to
// PrintMsg_W.

void PrintMsgDef_W(int msg_type, int msg_id)
{
    DISP_STR    ds;
    MSG_WIN_DAT win_ctrl;

    if (0x52 < (u_int)msg_type)
    {
        PRINT_ASSERT("Error! PrintMsgDef_W msg_type %d", msg_type);
    }
    if ((msg_id < 0) || (GetMsgIDNumMax(msg_type) <= msg_id))
    {
        PRINT_ASSERT("Error! PrintMsgDef_W msg_id %d", msg_id);
    }
    SetMsgDefData(&ds, msg_type);
    SetMsgWinDefData(&win_ctrl, msg_type);
    ds.str = GetMsgDataAddr(msg_type, msg_id);
    PrintMsg_W(&ds, &win_ctrl);
}

// ──────────────────────────────────────────────────────────────────────
// Fill a DISP_STR with the per-type default text record (position from the
// msg_def_data table, pri 0x20, alpha 100, colour label 1).

void SetMsgDefData(DISP_STR *msg_data, int msg_type)
{
    STR_DAT sd;
    int     msg_def_id;

    sd.str   = (u_char *)0;
    sd.pos_x = 0;
    sd.pos_y = 0;
    sd.type  = 0;
    sd.r     = 0x80;
    sd.g     = 0x80;
    sd.b     = 0x80;
    sd.alpha = 0x80;
    sd.pri   = 0;

    if (0x52 < (u_int)msg_type)
    {
        PRINT_ASSERT("Error! SetMsgDefData msg_type %d", msg_type);
    }

    msg_def_id = msg_disp_data[msg_type].msg_def_id;
    CopyStrDToStr(msg_data, &sd);
    msg_data->pri = 0x20;
    msg_data->pos_x = msg_def_data[msg_def_id].x;
    msg_data->alpha = 100;
    msg_data->pos_y = msg_def_data[msg_def_id].y;
    msg_data->x_wide = 0;
    MsgColChange(msg_data, '\x01');
}

// ──────────────────────────────────────────────────────────────────────
// Fill a MSG_WIN_DAT with the per-type default window rectangle.

void SetMsgWinDefData(MSG_WIN_DAT *win_data, int msg_type)
{
    int msg_win_id;

    if (0x52 < (u_int)msg_type)
    {
        PRINT_ASSERT("Error! SetMsgWinDefData msg_type %d", msg_type);
    }
    msg_win_id = msg_disp_data[msg_type].msg_win_id;
    win_data->x = msg_win_data[msg_win_id].x;
    win_data->y = msg_win_data[msg_win_id].y;
    win_data->w = msg_win_data[msg_win_id].w;
    win_data->h = msg_win_data[msg_win_id].h;
}

// ──────────────────────────────────────────────────────────────────────
// Public PrintMsg (7-arg): validate (type,id) and forward to PrintMsg_Arrange.

int PrintMsg(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri)
{
    if (0x52 < (u_int)msg_type)
    {
        PRINT_ASSERT("Error! PrintMsg msg_type %d", msg_type);
    }
    if ((msg_id < 0) || (GetMsgIDNumMax(msg_type) <= msg_id))
    {
        PRINT_ASSERT("Error! PrintMsg msg_id %d", msg_id);
    }
    PrintMsg_Arrange(msg_type, msg_id, x, y, col_label, alpha, pri, 0, 0, 0);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Public PrintMsg (9-arg): as above plus per-glyph x/y spacing widths.

int PrintMsg(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
             int x_wide, int y_wide)
{
    if (0x52 < (u_int)msg_type)
    {
        PRINT_ASSERT("Error! PrintMsg msg_type %d", msg_type);
    }
    if ((msg_id < 0) || (GetMsgIDNumMax(msg_type) <= msg_id))
    {
        PRINT_ASSERT("Error! PrintMsg msg_id %d", msg_id);
    }
    PrintMsg_Arrange(msg_type, msg_id, x, y, col_label, alpha, pri, x_wide, y_wide, 0);
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Render one logical line of a message stream: dispatch the leading control
// code (bank/set/winsize/arrange/colour/exnum/return/terminator), then draw
// the glyph and advance the pen.  Returns a code controlling the line loop.

int PrintMsgOneLine(DISP_STR *disp_addr, MSG_EXE_CTRL *pCtrl, Q_WORDDATA **ppbuf)
{
    int     loop;
    int     draw_flg;
    u_char *dat_addr;
    u_char  nfw;

    draw_flg = 0;
    loop = 1;
    pCtrl->now_bank = 1;
    dat_addr = disp_addr->str;
    switch (*dat_addr)
    {
    case 0xf0:
    case 0xf1:
    case 0xf2:
    case 0xf3:
        MesBankExe(disp_addr, pCtrl);
        draw_flg = 1;
        break;
    case 0xf7:
        MesSetExe(disp_addr, pCtrl);
        break;
    case 0xf8:
        MesWinSizeExe(disp_addr, (MSG_WIN_DAT *)0);
        break;
    case 0xf9:
        MesArrangeExe(disp_addr, (MSG_WIN_DAT *)0, pCtrl);
        break;
    case 0xfb:
    case 0xff:
        disp_addr->str = dat_addr + 1;
        loop = 0;
        disp_addr->st = disp_addr->st | 0x40;
        break;
    case 0xfc:
        MesExNumExe(disp_addr, pCtrl, ppbuf);
        break;
    case 0xfd:
        MesColExe(disp_addr, pCtrl);
        break;
    case 0xfe:
        loop = 2;
        MesRetExe(disp_addr, (MSG_WIN_DAT *)0, pCtrl);
        break;
    default:
        draw_flg = 1;
        pCtrl->font_pos = Get1Byte(dat_addr);
        disp_addr->str = disp_addr->str + 1;
        break;
    }
    if (draw_flg != 0)
    {
        nfw = GetFontSize((u_char)pCtrl->now_bank, pCtrl->font_pos);
        pCtrl->font_w = nfw;
        SetMsgFont(ppbuf, disp_addr, pCtrl->now_bank, pCtrl->font_pos, nfw, pCtrl->font_h);
        disp_addr->pos_x = disp_addr->pos_x + (u_int)pCtrl->font_w + disp_addr->x_wide;
    }
    return loop;
}

// ──────────────────────────────────────────────────────────────────────
// Public PrintMsg (record form): init the exe control, open a font packet,
// then drive PrintMsgOneLine until the line/page loop ends.  one_line_flg
// selects single-line vs full-stream termination.

int PrintMsg(DISP_STR *disp_addr, int one_line_flg)
{
    MSG_EXE_CTRL ctrl;
    int          mask_loop;
    Q_WORDDATA  *pbuf;
    Q_WORDDATA  *pbuf_top;

    mask_loop = 1;
    if (one_line_flg == 0)
    {
        mask_loop = 3;
    }
    MsgExeCtrlInit(disp_addr, &ctrl, '\0');
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);
    do
    {
    } while ((PrintMsgOneLine(disp_addr, &ctrl, &pbuf) & mask_loop) != 0);
    EndPK2Dbuf(pbuf);
    AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
    return disp_addr->csr + 1;
}

// ──────────────────────────────────────────────────────────────────────
// Draw a raw message-stream pointer on one line at (x,y) with horizontal
// arrangement: 0 left, 1 right (x is the right edge), 2 centre.

int PrintMsg_ArrangeOneLine(u_char *pAdrs, int x, int y, int col_label, int alpha, int pri,
                            int x_wide, int y_wide, int arrange)
{
    DISP_STR disp_wrk;

    memset(&disp_wrk, 0, sizeof(disp_wrk));
    disp_wrk.pri = (pri & 0xf) << 4;
    disp_wrk.str = pAdrs;
    disp_wrk.pos_x = x;
    disp_wrk.pos_y = y;
    disp_wrk.alpha = alpha;
    MsgColChange(&disp_wrk, (u_char)col_label);
    disp_wrk.x_wide = x_wide;
    disp_wrk.y_wide = y_wide;

    if (arrange == 1)
    {
        disp_wrk.pos_x = x - GetMsgLineLength(disp_wrk.str, (u_char **)0);
    }
    else if (arrange == 2)
    {
        disp_wrk.pos_x = x - GetMsgLineLength(disp_wrk.str, (u_char **)0) / 2;
    }
    else if (arrange == 0)
    {
        disp_wrk.pos_x = x;
    }
    else
    {
        PRINT_ASSERT("Error! PringMsg_Arrange", "");
        disp_wrk.pos_x = disp_wrk.pos_x;
    }

    PrintMsg(&disp_wrk, 1);
    return disp_wrk.csr + 1;
}

// ──────────────────────────────────────────────────────────────────────
// Initialise the per-draw message exe control: record the start position
// and reset bank/font cell to bank 1, 24px.

static void MsgExeCtrlInit(DISP_STR *data, MSG_EXE_CTRL *ctrl, u_char disp_type)
{
    ctrl->start_x = data->pos_x;
    ctrl->disp_type = disp_type;
    ctrl->start_y = data->pos_y;
    ctrl->font_h = '\x18';
    ctrl->now_bank = 1;
    ctrl->font_w = '\x18';
    ctrl->font_pos = '\0';
    ctrl->arrange = '\0';
}

// ──────────────────────────────────────────────────────────────────────
// Handle the bank-switch control code (0xF0-0xF3): map the bank operand
// through bank_tbl (1..5) and load the next glyph index.

static void MesBankExe(DISP_STR *data, MSG_EXE_CTRL *ctrl)
{
    u_char bank_tbl[5];
    u_char b0;

    bank_tbl[0] = 1;
    bank_tbl[1] = 2;
    bank_tbl[2] = 3;
    bank_tbl[3] = 4;
    bank_tbl[4] = 5;
    b0 = Get1Byte(data->str);
    data->str = data->str + 1;
    ctrl->font_pos = Get1Byte(data->str);
    ctrl->now_bank = (u_int)bank_tbl[(int)(char)b0 + 0x11 & 0xff];
    data->str = data->str + 1;
}

// ──────────────────────────────────────────────────────────────────────
// Handle the position-set control code (0xF7): read absolute x,y (2 bytes
// each) and reset both the pen and the line-start origin.

static void MesSetExe(DISP_STR *data, MSG_EXE_CTRL *ctrl)
{
    data->str = data->str + 1;
    data->pos_x = (int)Get2Byte(data->str);
    data->str = data->str + 2;
    data->pos_y = (int)Get2Byte(data->str);
    ctrl->start_x = data->pos_x;
    data->str = data->str + 2;
    ctrl->start_y = data->pos_y;
}

// ──────────────────────────────────────────────────────────────────────
// Handle the window-size control code (0xF8): when measuring (win==0) just
// skip the 8 operand bytes; otherwise read x,y,w,h into the window record.

static void MesWinSizeExe(DISP_STR *data, MSG_WIN_DAT *win)
{
    if (win == (MSG_WIN_DAT *)0)
    {
        data->str = data->str + 9;
    }
    else
    {
        data->str = data->str + 1;
        win->x = (float)(int)Get2Byte(data->str);
        data->str = data->str + 2;
        win->y = (float)(int)Get2Byte(data->str);
        data->str = data->str + 2;
        win->w = (float)(int)Get2Byte(data->str);
        data->str = data->str + 2;
        win->h = (float)(int)Get2Byte(data->str);
        data->str = data->str + 2;
    }
}

// ──────────────────────────────────────────────────────────────────────
// Handle the colour control code (0xFD): read the colour label and apply it.

static void MesColExe(DISP_STR *data, MSG_EXE_CTRL *ctrl)
{
    u_char col_label;

    data->str = data->str + 1;
    col_label = Get1Byte(data->str);
    data->str = data->str + 1;
    MsgColChange(data, col_label);
}

// ──────────────────────────────────────────────────────────────────────
// Handle the arrange control code (0xF9): set the line alignment and, when
// a window is given, reposition the pen for centre (2) / right (1) / left
// (0) relative to the window rectangle and the next line's measured width.

static void MesArrangeExe(DISP_STR *data, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl)
{
    u_char *dat_addr;
    int     len;
    float   off;

    dat_addr = data->str + 1;
    data->str = dat_addr;
    if (win == (MSG_WIN_DAT *)0)
    {
        data->str = data->str + 2;
    }
    else
    {
        ctrl->arrange = Get1Byte(dat_addr);
        data->str = data->str + 1;
        if (ctrl->arrange == '\x02')
        {
            len = GetMsgLineLength(data->str, (u_char **)0);
            data->pos_x = (int)((win->x + win->w * 0.5) - (float)(len / 2));
        }
        else if (ctrl->arrange == '\x01')
        {
            len = GetMsgLineLength(data->str, (u_char **)0);
            off = win->x - (float)ctrl->start_x;
            data->pos_x = (int)(((float)ctrl->start_x + win->w + off + off) - (float)len);
        }
        else if (ctrl->arrange == '\0')
        {
            data->pos_x = ctrl->start_x;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Handle the line-break control code (0xFE): re-apply the line arrangement
// for the new line, then advance the pen by one font cell along the layout
// axis (vertical for disp_type 0, horizontal for disp_type 1).

static void MesRetExe(DISP_STR *data, MSG_WIN_DAT *win, MSG_EXE_CTRL *ctrl)
{
    u_char *msg_addr;
    int     len;
    u_char  disp_type;
    float   off;

    msg_addr = data->str + 1;
    data->str = msg_addr;
    if (win == (MSG_WIN_DAT *)0)
    {
        disp_type = ctrl->disp_type;
        if (disp_type == '\0')
        {
            data->pos_x = ctrl->start_x;
            disp_type = ctrl->disp_type;
        }
        else if (disp_type == '\x01')
        {
            data->pos_y = ctrl->start_y;
        }
    }
    else
    {
        disp_type = ctrl->arrange;
        if (disp_type == '\x02')
        {
            len = GetMsgLineLength(msg_addr, (u_char **)0);
            data->pos_x = (int)((win->x + win->w * 0.5) - (float)(len / 2));
            disp_type = ctrl->disp_type;
        }
        else if (disp_type == '\x01')
        {
            len = GetMsgLineLength(msg_addr, (u_char **)0);
            off = win->x - (float)ctrl->start_x;
            data->pos_x = (int)(((float)ctrl->start_x + win->w + off + off) - (float)len);
            disp_type = ctrl->disp_type;
        }
        else if (disp_type == '\0')
        {
            data->pos_x = ctrl->start_x;
            disp_type = ctrl->disp_type;
        }
        else
        {
            disp_type = ctrl->disp_type;
        }
    }
    if (disp_type == '\0')
    {
        data->pos_y = data->pos_y + (u_int)ctrl->font_h + data->y_wide;
    }
    else if (disp_type == '\x01')
    {
        data->pos_x = data->pos_x - ((u_int)ctrl->font_w + data->x_wide);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Handle the extended-number control code (0xFC): invoke the registered
// getter, format the value with "%d", then draw each digit using the
// normal_tbl glyph indices and advance the pen.

static void MesExNumExe(DISP_STR *data, MSG_EXE_CTRL *ctrl, Q_WORDDATA **ppbuf)
{
    int           num;
    int           i;
    char          cwo[256];
    static u_char normal_tbl[10] = { 63, 64, 65, 66, 67, 68, 69, 70, 71, 72 };
    u_char       *puVar1;
    u_char        ex;
    u_char        nfw;

    puVar1 = data->str;
    data->str = puVar1 + 1;
    ex = Get1Byte(puVar1 + 1);
    data->str = data->str + 1;
    num = 0;
    if ((mes_ex_num_tbl[(char)ex] == 0) || (0 < (char)ex))
    {
        PRINT_ASSERT("Error! %s\n", __FUNCTION__);
    }
    else
    {
        num = (*mes_ex_num_tbl[(char)ex])();
    }
    sprintf(cwo, "%d", num);
    i = 0;
    if (cwo[0] != '\0')
    {
        while (1)
        {
            ctrl->font_pos = normal_tbl[cwo[i] - '0'];
            nfw = GetFontSize((u_char)ctrl->now_bank, ctrl->font_pos);
            ctrl->font_w = nfw;
            SetMsgFont(ppbuf, data, ctrl->now_bank, ctrl->font_pos, nfw, ctrl->font_h);
            if (ctrl->disp_type == '\0')
            {
                data->pos_x = data->pos_x + (u_int)ctrl->font_w + data->x_wide;
            }
            else if (ctrl->disp_type == '\x01')
            {
                data->pos_y = data->pos_y + (u_int)ctrl->font_h + data->y_wide;
            }
            if (cwo[i + 1] == '\0')
            {
                break;
            }
            i = i + 1;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// Memory-card slot accessor for the message subsystem: the active port + 1.

static int GetAccessMcSlot_Msg(void)
{
    return GetAccessMemoryCardPort() + 1;
}
// ──────────────────────────────────────────────────────────────────────
// PrintMsg_T (overload 1): look up a message by type/id, build a work
// DISP_STR record and hand it to the DISP_STR* overload below.  "_T" =
// tate-gaki (vertical writing): the line advances downward.
int PrintMsg_T(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri)
{
    DISP_STR msg_data;
    DISP_STR disp_wrk;

    if ((u_int)msg_type > 0x52)
    {
        PRINT_ASSERT("Error! PrintMsg_T msg_type %d", msg_type);
    }
    if (msg_id < 0 || GetMsgIDNumMax(msg_type) <= msg_id)
    {
        PRINT_ASSERT("Error! PrintMsg_T msg_id %d", msg_id);
    }

    memset(&disp_wrk, 0, sizeof(DISP_STR));
    memset(&msg_data, 0, sizeof(DISP_STR));
    msg_data.str = GetMsgDataAddr(msg_type, msg_id);
    msg_data.pri = (pri & 0xf) << 4;
    msg_data.pos_x = x;
    msg_data.pos_y = y;
    msg_data.alpha = alpha;
    MsgColChange(&msg_data, (u_char)col_label);
    disp_wrk.str = msg_data.str;
    disp_wrk.pos_x = msg_data.pos_x;
    disp_wrk.pos_y = msg_data.pos_y;
    disp_wrk.type = msg_data.type;
    disp_wrk.r = msg_data.r;
    disp_wrk.g = msg_data.g;
    disp_wrk.b = msg_data.b;
    disp_wrk.alpha = msg_data.alpha;
    disp_wrk.pri = msg_data.pri;
    disp_wrk.x_wide = msg_data.x_wide;
    disp_wrk.y_wide = msg_data.y_wide;
    disp_wrk.brnch_num = msg_data.brnch_num;
    PrintMsg_T(&disp_wrk);
    return msg_data.csr + 1;
}

// ──────────────────────────────────────────────────────────────────────
// PrintMsg_T (overload 2): the actual vertical-writing renderer.  Walks the
// 0xF0–0xFF control grammar, drawing one glyph per text byte and advancing
// pos_y by the glyph height.
int PrintMsg_T(DISP_STR *disp_addr)
{
    int loop;
    MSG_EXE_CTRL ctrl;
    u_char bank_label;
    u_char draw_flg;
    Q_WORDDATA *pbuf;
    u_char bank_tbl[5];
    Q_WORDDATA *pbuf_top;
    u_char *str;

    bank_tbl[0] = 1;
    bank_tbl[1] = 2;
    bank_tbl[2] = 3;
    bank_tbl[3] = 4;
    bank_tbl[4] = 5;
    loop = 1;
    MsgExeCtrlInit(disp_addr, &ctrl, 1);
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);
    do
    {
        draw_flg = 0;
        str = disp_addr->str;
        ctrl.now_bank = 1;
        switch (*str)
        {
        case 0xf0:
            bank_label = Get1Byte(str);
            str = disp_addr->str;
            disp_addr->str = str + 1;
            ctrl.font_pos = Get1Byte(str + 1);
            disp_addr->str = disp_addr->str + 1;
            if (ctrl.font_pos == 0x1a)
            {
                printf("NOT FOUND FONT DATA!!\n");
                ctrl.now_bank = bank_tbl[(u_char)(bank_label + 0x11)];
            }
            else
            {
                ctrl.now_bank = bank_tbl[(u_char)(bank_label + 0x11)];
            }
            draw_flg = 1;
            break;
        case 0xf1:
        case 0xf2:
        case 0xf3:
            MesBankExe(disp_addr, &ctrl);
            draw_flg = 1;
            break;
        case 0xf7:
            MesSetExe(disp_addr, &ctrl);
            break;
        case 0xf8:
            MesWinSizeExe(disp_addr, (MSG_WIN_DAT *)0);
            break;
        case 0xf9:
            disp_addr->str = str + 2;
            break;
        case 0xfb:
        case 0xff:
            disp_addr->str = str + 1;
            loop = 0;
            disp_addr->st = disp_addr->st | 0x40;
            break;
        case 0xfc:
            MesExNumExe(disp_addr, &ctrl, &pbuf);
            break;
        case 0xfd:
            MesColExe(disp_addr, &ctrl);
            break;
        case 0xfe:
            MesRetExe(disp_addr, (MSG_WIN_DAT *)0, &ctrl);
            break;
        default:
            draw_flg = 1;
            ctrl.font_pos = Get1Byte(str);
            disp_addr->str = disp_addr->str + 1;
            break;
        }
        if (draw_flg)
        {
            ctrl.font_w = GetFontSize((u_char)ctrl.now_bank, ctrl.font_pos);
            SetMsgFont(&pbuf, disp_addr, ctrl.now_bank, ctrl.font_pos, ctrl.font_w, ctrl.font_h);
            disp_addr->pos_y = disp_addr->pos_y + ctrl.font_h + disp_addr->y_wide;
        }
        if (!loop)
        {
            EndPK2Dbuf(pbuf);
            AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
            return disp_addr->csr + 1;
        }
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// PrintMsg_P: paged horizontal message.  Like the window variant but with no
// window frame; honours the per-page button-wait via msg_disp_ctrl.
int PrintMsg_P(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
               int x_wide, int y_wide)
{
    DISP_STR msg_data;
    DISP_STR disp_wrk;
    MSG_EXE_CTRL ctrl;
    u_char draw_flg;
    u_char btn_wait_cnt;
    int loop;
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pbuf_top;

    if ((u_int)msg_type > 0x52)
    {
        PRINT_ASSERT("Error! PrintMsg_P msg_type %d", msg_type);
    }
    if (msg_id < 0 || GetMsgIDNumMax(msg_type) <= msg_id)
    {
        PRINT_ASSERT("Error! PrintMsg_P msg_id %d", msg_id);
    }
    memset(&disp_wrk, 0, sizeof(DISP_STR));
    memset(&msg_data, 0, sizeof(DISP_STR));
    msg_data.str = GetMsgDataAddr(msg_type, msg_id);
    msg_data.pri = (pri & 0xf) << 4;
    loop = 1;
    btn_wait_cnt = 0;
    msg_data.pos_x = x;
    msg_data.pos_y = y;
    msg_data.alpha = alpha;
    MsgColChange(&msg_data, (u_char)col_label);
    disp_wrk.str = msg_data.str;
    disp_wrk.pos_x = msg_data.pos_x;
    disp_wrk.pos_y = msg_data.pos_y;
    disp_wrk.type = msg_data.type;
    disp_wrk.r = msg_data.r;
    disp_wrk.g = msg_data.g;
    disp_wrk.b = msg_data.b;
    disp_wrk.alpha = msg_data.alpha;
    disp_wrk.csr = msg_data.csr;
    disp_wrk.st = msg_data.st;
    disp_wrk.x_wide = x_wide;
    disp_wrk.pri = msg_data.pri;
    disp_wrk.brnch_num = msg_data.brnch_num;
    disp_wrk.y_wide = y_wide;
    MsgExeCtrlInit(&disp_wrk, &ctrl, 0);
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);
    do
    {
        draw_flg = 0;
        ctrl.now_bank = 1;
        switch (*disp_wrk.str)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
            MesBankExe(&disp_wrk, &ctrl);
            draw_flg = 1;
            break;
        case 0xf7:
            MesSetExe(&disp_wrk, &ctrl);
            break;
        case 0xf8:
            MesWinSizeExe(&disp_wrk, (MSG_WIN_DAT *)0);
            break;
        case 0xf9:
            MesArrangeExe(&disp_wrk, (MSG_WIN_DAT *)0, &ctrl);
            break;
        case 0xfb:
            btn_wait_cnt = (u_char)(btn_wait_cnt + 1);
            disp_wrk.str = disp_wrk.str + 1;
            disp_wrk.pos_x = ctrl.start_x;
            disp_wrk.pos_y = ctrl.start_y;
            if (msg_disp_ctrl.pass_btn_wait < btn_wait_cnt)
            {
                loop = 0;
                msg_disp_ctrl.disp_state = 1;
            }
            break;
        case 0xfc:
            MesExNumExe(&disp_wrk, &ctrl, &pbuf);
            break;
        case 0xfd:
            MesColExe(&disp_wrk, &ctrl);
            break;
        case 0xfe:
            MesRetExe(&disp_wrk, (MSG_WIN_DAT *)0, &ctrl);
            break;
        case 0xff:
            disp_wrk.str = disp_wrk.str + 1;
            msg_disp_ctrl.pass_btn_wait = msg_disp_ctrl.pass_btn_wait - 1;
            if ((char)msg_disp_ctrl.pass_btn_wait < 0)
            {
                msg_disp_ctrl.pass_btn_wait = 0;
            }
            loop = 0;
            msg_disp_ctrl.disp_state = 0;
            disp_wrk.st = disp_wrk.st | 0x40;
            disp_wrk.pos_x = ctrl.start_x;
            disp_wrk.pos_y = ctrl.start_y;
            msg_disp_ctrl.init_flg = 0;
            break;
        default:
            draw_flg = 1;
            ctrl.font_pos = Get1Byte(disp_wrk.str);
            disp_wrk.str = disp_wrk.str + 1;
            break;
        }
        if (msg_disp_ctrl.pass_btn_wait == btn_wait_cnt && loop)
        {
            msg_disp_ctrl.disp_state = 2;
            if (draw_flg)
            {
                ctrl.font_w = GetFontSize((u_char)ctrl.now_bank, ctrl.font_pos);
                SetMsgFont(&pbuf, &disp_wrk, ctrl.now_bank, ctrl.font_pos, ctrl.font_w, ctrl.font_h);
                disp_wrk.pos_x = disp_wrk.pos_x + ctrl.font_w + disp_wrk.x_wide;
            }
        }
    } while (loop);
    EndPK2Dbuf(pbuf);
    AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
    return disp_wrk.csr + 1;
}

// ──────────────────────────────────────────────────────────────────────
// PrintMsg_W: windowed horizontal message.  Draws the common window frame
// (if win_ctrl supplied), drives the player/enemy "talking" animation locks,
// then renders the page text.
int PrintMsg_W(DISP_STR *disp_addr, MSG_WIN_DAT *win_ctrl)
{
    int loop;
    MSG_EXE_CTRL ctrl;
    u_char draw_flg;
    u_char btn_wait_cnt;
    int win_x;
    int win_y;
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pbuf_top;
    DISP_STR disp_wrk;
    MSG_WIN_DAT win_wrk;
    u_char *str;

    MsgExeCtrlInit(disp_addr, &ctrl, 0);
    if (win_ctrl != (MSG_WIN_DAT *)0)
    {
        disp_wrk.str = disp_addr->str;
        disp_wrk.pos_x = disp_addr->pos_x;
        disp_wrk.pos_y = disp_addr->pos_y;
        disp_wrk.type = disp_addr->type;
        disp_wrk.r = disp_addr->r;
        disp_wrk.g = disp_addr->g;
        disp_wrk.b = disp_addr->b;
        disp_wrk.alpha = disp_addr->alpha;
        disp_wrk.pri = disp_addr->pri;
        disp_wrk.x_wide = disp_addr->x_wide;
        disp_wrk.y_wide = disp_addr->y_wide;
        disp_wrk.brnch_num = disp_addr->brnch_num;
        disp_wrk.csr = disp_addr->csr;
        disp_wrk.st = disp_addr->st;
        win_wrk.x = win_ctrl->x;
        win_wrk.y = win_ctrl->y;
        win_wrk.w = win_ctrl->w;
        win_wrk.h = win_ctrl->h;
        if (GetMsgWinData(&disp_wrk, &win_wrk, &win_x, &win_y) != 0)
        {
            DrawCmnWindow(0xa0, (float)win_x, (float)win_y, win_ctrl->w, win_ctrl->h, 0x80, 'f');
        }
    }
    loop = 1;
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);
    btn_wait_cnt = 0;
    if (msg_disp_ctrl.init_flg == 0)
    {
        ClearPlyrMoveStatus();
        SetPlyrAnime(0, 0xa);
        EnemyAnimLock();
        if (IsSisWrk() != 0 && GetSisStandAnm() != 0)
        {
            SetSisterAnime(0, 0x14);
        }
        msg_disp_ctrl.init_flg = 1;
        msg_disp_ctrl.pass_btn_wait = 0;
    }
    do
    {
        draw_flg = 0;
        str = disp_addr->str;
        ctrl.now_bank = 1;
        switch (*str)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
            MesBankExe(disp_addr, &ctrl);
            draw_flg = 1;
            break;
        case 0xf7:
            MesSetExe(disp_addr, &ctrl);
            break;
        case 0xf8:
            MesWinSizeExe(disp_addr, win_ctrl);
            break;
        case 0xf9:
            MesArrangeExe(disp_addr, win_ctrl, &ctrl);
            break;
        case 0xfb:
            disp_addr->str = str + 1;
            disp_addr->pos_x = ctrl.start_x;
            btn_wait_cnt = (u_char)(btn_wait_cnt + 1);
            disp_addr->pos_y = ctrl.start_y;
            if (msg_disp_ctrl.pass_btn_wait < btn_wait_cnt)
            {
                loop = 0;
                msg_disp_ctrl.disp_state = 1;
            }
            break;
        case 0xfc:
            MesExNumExe(disp_addr, &ctrl, &pbuf);
            break;
        case 0xfd:
            MesColExe(disp_addr, &ctrl);
            break;
        case 0xfe:
            MesRetExe(disp_addr, win_ctrl, &ctrl);
            break;
        case 0xff:
            disp_addr->str = str + 1;
            msg_disp_ctrl.pass_btn_wait = msg_disp_ctrl.pass_btn_wait - 1;
            if ((char)msg_disp_ctrl.pass_btn_wait < 0)
            {
                msg_disp_ctrl.pass_btn_wait = 0;
            }
            msg_disp_ctrl.disp_state = 0;
            loop = 0;
            msg_disp_ctrl.init_flg = 0;
            EnemyAnimUnlock();
            disp_addr->st = disp_addr->st | 0x40;
            disp_addr->pos_x = ctrl.start_x;
            disp_addr->pos_y = ctrl.start_y;
            break;
        default:
            draw_flg = 1;
            ctrl.font_pos = Get1Byte(str);
            disp_addr->str = disp_addr->str + 1;
            break;
        }
        if (msg_disp_ctrl.pass_btn_wait == btn_wait_cnt)
        {
            if (loop)
            {
                msg_disp_ctrl.disp_state = 2;
            }
            if (draw_flg)
            {
                ctrl.font_w = GetFontSize((u_char)ctrl.now_bank, ctrl.font_pos);
                SetMsgFont(&pbuf, disp_addr, ctrl.now_bank, ctrl.font_pos, ctrl.font_w, ctrl.font_h);
                disp_addr->pos_x = disp_addr->pos_x + ctrl.font_w + disp_addr->x_wide;
            }
        }
    } while (loop);
    EndPK2Dbuf(pbuf);
    AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
    return disp_addr->csr + 1;
}

// ──────────────────────────────────────────────────────────────────────
// PrintMsg_TW: windowed vertical-writing message.  Combines the window frame
// and animation locks of _W with the downward line-advance of _T.
int PrintMsg_TW(DISP_STR *disp_addr, MSG_WIN_DAT *win_ctrl)
{
    int loop;
    MSG_EXE_CTRL ctrl;
    u_char bank_label;
    u_char draw_flg;
    u_char btn_wait_cnt;
    int win_x;
    int win_y;
    Q_WORDDATA *pbuf;
    u_char bank_tbl[5];
    Q_WORDDATA *pbuf_top;
    DISP_STR disp_wrk;
    MSG_WIN_DAT win_wrk;
    u_char *str;

    loop = 1;
    bank_tbl[0] = 1;
    bank_tbl[1] = 2;
    bank_tbl[2] = 3;
    bank_tbl[3] = 4;
    bank_tbl[4] = 5;
    btn_wait_cnt = 0;
    MsgExeCtrlInit(disp_addr, &ctrl, 1);
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);
    if (win_ctrl != (MSG_WIN_DAT *)0)
    {
        disp_wrk.str = disp_addr->str;
        disp_wrk.pos_x = disp_addr->pos_x;
        disp_wrk.pos_y = disp_addr->pos_y;
        disp_wrk.type = disp_addr->type;
        disp_wrk.r = disp_addr->r;
        disp_wrk.g = disp_addr->g;
        disp_wrk.b = disp_addr->b;
        disp_wrk.alpha = disp_addr->alpha;
        disp_wrk.pri = disp_addr->pri;
        disp_wrk.x_wide = disp_addr->x_wide;
        disp_wrk.y_wide = disp_addr->y_wide;
        disp_wrk.brnch_num = disp_addr->brnch_num;
        disp_wrk.csr = disp_addr->csr;
        disp_wrk.st = disp_addr->st;
        win_wrk.x = win_ctrl->x;
        win_wrk.y = win_ctrl->y;
        win_wrk.w = win_ctrl->w;
        win_wrk.h = win_ctrl->h;
        if (GetMsgWinData(&disp_wrk, &win_wrk, &win_x, &win_y) != 0)
        {
            DrawCmnWindow(0xa0, (float)win_x, (float)win_y, win_ctrl->w, win_ctrl->h, 0x80, 'f');
        }
    }
    if (msg_disp_ctrl.init_flg == 0)
    {
        ClearPlyrMoveStatus();
        SetPlyrAnime(0, 0xa);
        if (IsSisWrk() != 0 && GetSisStandAnm() != 0)
        {
            SetSisterAnime(0, 0x14);
        }
        EnemyAnimLock();
        msg_disp_ctrl.pass_btn_wait = 0;
        msg_disp_ctrl.init_flg = 1;
    }
    do
    {
        draw_flg = 0;
        str = disp_addr->str;
        ctrl.now_bank = 1;
        switch (*str)
        {
        case 0xf0:
            bank_label = Get1Byte(str);
            str = disp_addr->str;
            disp_addr->str = str + 1;
            ctrl.font_pos = Get1Byte(str + 1);
            disp_addr->str = disp_addr->str + 1;
            if (ctrl.font_pos == 0x1a)
            {
                printf("NOT FOUND FONT DATA!!\n");
                ctrl.now_bank = bank_tbl[(u_char)(bank_label + 0x11)];
            }
            else
            {
                ctrl.now_bank = bank_tbl[(u_char)(bank_label + 0x11)];
            }
            draw_flg = 1;
            break;
        case 0xf1:
        case 0xf2:
        case 0xf3:
            MesBankExe(disp_addr, &ctrl);
            draw_flg = 1;
            break;
        case 0xf7:
            MesSetExe(disp_addr, &ctrl);
            break;
        case 0xf8:
            MesWinSizeExe(disp_addr, win_ctrl);
            break;
        case 0xf9:
            disp_addr->str = str + 2;
            break;
        case 0xfb:
            disp_addr->str = str + 1;
            disp_addr->pos_x = ctrl.start_x;
            btn_wait_cnt = (u_char)(btn_wait_cnt + 1);
            disp_addr->pos_y = ctrl.start_y;
            if (msg_disp_ctrl.pass_btn_wait < btn_wait_cnt)
            {
                loop = 0;
                msg_disp_ctrl.disp_state = 1;
            }
            break;
        case 0xfc:
            MesExNumExe(disp_addr, &ctrl, &pbuf);
            break;
        case 0xfd:
            MesColExe(disp_addr, &ctrl);
            break;
        case 0xfe:
            MesRetExe(disp_addr, (MSG_WIN_DAT *)0, &ctrl);
            break;
        case 0xff:
            disp_addr->str = str + 1;
            disp_addr->pos_x = ctrl.start_x;
            disp_addr->pos_y = ctrl.start_y;
            EnemyAnimUnlock();
            msg_disp_ctrl.pass_btn_wait = msg_disp_ctrl.pass_btn_wait - 1;
            if ((char)msg_disp_ctrl.pass_btn_wait < 0)
            {
                msg_disp_ctrl.pass_btn_wait = 0;
            }
            msg_disp_ctrl.disp_state = 0;
            loop = 0;
            msg_disp_ctrl.init_flg = 0;
            disp_addr->st = disp_addr->st | 0x40;
            break;
        default:
            draw_flg = 1;
            ctrl.font_pos = Get1Byte(str);
            disp_addr->str = disp_addr->str + 1;
            break;
        }
        if (msg_disp_ctrl.pass_btn_wait == btn_wait_cnt && loop)
        {
            msg_disp_ctrl.disp_state = 2;
            if (draw_flg)
            {
                SetMsgFont(&pbuf, disp_addr, ctrl.now_bank, ctrl.font_pos, ctrl.font_w, ctrl.font_h);
                disp_addr->pos_y = disp_addr->pos_y + ctrl.font_h + disp_addr->y_wide;
            }
        }
        if (!loop)
        {
            EndPK2Dbuf(pbuf);
            AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
            return disp_addr->csr + 1;
        }
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// PrintMsg_Arrange: print one line aligned left (0), right (1) or centred (2)
// about x, by measuring the line width with GetMsgLineLength first.
int PrintMsg_Arrange(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri,
                     int x_wide, int y_wide, int arrange)
{
    DISP_STR msg_data;
    DISP_STR disp_wrk;

    if ((u_int)msg_type > 0x52)
    {
        PRINT_ASSERT("Error! PrintMsg_Arrange msg_type %d", msg_type);
    }
    if (msg_id < 0 || GetMsgIDNumMax(msg_type) <= msg_id)
    {
        PRINT_ASSERT("Error! PrintMsg_Arrange msg_id %d", msg_id);
    }
    memset(&disp_wrk, 0, sizeof(DISP_STR));
    memset(&msg_data, 0, sizeof(DISP_STR));
    msg_data.str = GetMsgDataAddr(msg_type, msg_id);
    msg_data.pri = (pri & 0xf) << 4;
    msg_data.pos_x = x;
    msg_data.pos_y = y;
    msg_data.alpha = alpha;
    MsgColChange(&msg_data, (u_char)col_label);
    disp_wrk.str = msg_data.str;
    disp_wrk.pos_x = msg_data.pos_x;
    disp_wrk.pos_y = msg_data.pos_y;
    disp_wrk.type = msg_data.type;
    disp_wrk.r = msg_data.r;
    disp_wrk.g = msg_data.g;
    disp_wrk.b = msg_data.b;
    disp_wrk.alpha = msg_data.alpha;
    disp_wrk.csr = msg_data.csr;
    disp_wrk.st = msg_data.st;
    disp_wrk.x_wide = x_wide;
    disp_wrk.pri = msg_data.pri;
    disp_wrk.brnch_num = msg_data.brnch_num;
    disp_wrk.y_wide = y_wide;
    switch (arrange)
    {
    case 0:
        disp_wrk.pos_x = x;
        break;
    case 1:
        disp_wrk.pos_x = x - GetMsgLineLength(msg_data.str, (u_char **)0);
        break;
    case 2:
        disp_wrk.pos_x = x - GetMsgLineLength(msg_data.str, (u_char **)0) / 2;
        break;
    default:
        PRINT_ASSERT("Error! PringMsg_Arrange", "");
        break;
    }

    PrintMsg(&disp_wrk, 0);
    return disp_wrk.csr + 1;
}

// ──────────────────────────────────────────────────────────────────────
// PrintChoice: render a choice prompt window plus the YES/NO or 3-way
// selection row, with a moving cursor at index csr.
void PrintChoice(DISP_STR *msg, MSG_WIN_DAT *win, int choice_msg, int *choice, int csr)
{
    DISP_STR msg_wrk;
    MSG_WIN_DAT win_wrk;
    u_char choice_num;
    short int choice_y;
    int choice_length[3];
    int max_length;
    int i;

    if (msg == (DISP_STR *)0)
    {
        SetMsgDefData(&msg_wrk, 7);
    }
    else
    {
        msg_wrk.str = msg->str;
        msg_wrk.pos_x = msg->pos_x;
        msg_wrk.pos_y = msg->pos_y;
        msg_wrk.type = msg->type;
        msg_wrk.r = msg->r;
        msg_wrk.g = msg->g;
        msg_wrk.b = msg->b;
        msg_wrk.alpha = msg->alpha;
        msg_wrk.pri = msg->pri;
        msg_wrk.x_wide = msg->x_wide;
        msg_wrk.y_wide = msg->y_wide;
        msg_wrk.brnch_num = msg->brnch_num;
        msg_wrk.csr = msg->csr;
        msg_wrk.st = msg->st;
    }
    msg_wrk.str = GetMsgDataAddr(7, choice_msg);
    if (win == (MSG_WIN_DAT *)0)
    {
        SetMsgWinDefData(&win_wrk, 7);
    }
    else
    {
        win_wrk.x = win->x;
        win_wrk.y = win->y;
        win_wrk.w = win->w;
        win_wrk.h = win->h;
    }
    choice_num = 0;
    max_length = 0;
    for (i = 0; i < 3; i++)
    {
        if (choice[i] == 0)
        {
            choice_length[i] = 0;
        }
        else
        {
            choice_length[i] = GetMsgLineLength(GetMsgDataAddr(7, choice[i]), (u_char **)0);
            choice_num = (u_char)(choice_num + 1);
            if (max_length < choice_length[i])
            {
                max_length = choice_length[i];
            }
        }
    }
    PrintMsg_W(&msg_wrk, &win_wrk);
    if (choice_num == 2)
    {
        choice_y = (short int)(msg_wrk.pos_y + 0x30);
        DrawCmnYesNoSel(csr, (float)choice_y, 0x80, 0);
    }
    else if (choice_num == 3)
    {
        choice_y = (short int)(msg_wrk.pos_y + 0x30);
        DrawCmnSelCsr(0, ((float)csr * 160.0f + 160.0f - (float)(max_length / 2)) - 20.0f,
                      (float)choice_y, 0x80, (float)(max_length + 0x28), 0);
        DrawCmnSelFrame(0, (160.0f - (float)(max_length / 2)) - 20.0f, (float)choice_y, 0x80,
                        (float)(max_length + 0x28));
        DrawCmnSelFrame(0, (320.0f - (float)(max_length / 2)) - 20.0f, (float)choice_y, 0x80,
                        (float)(max_length + 0x28));
        DrawCmnSelFrame(0, (480.0f - (float)(max_length / 2)) - 20.0f, (float)choice_y, 0x80,
                        (float)(max_length + 0x28));
        PrintMsg_Arrange(7, choice[0], 0xa0, choice_y + 2, 0, 0x80, 0, 0, 0, 2);
        PrintMsg_Arrange(7, choice[1], 0x140, choice_y + 2, 0, 0x80, 0, 0, 0, 2);
        PrintMsg_Arrange(7, choice[2], 0x1e0, choice_y + 2, 0, 0x80, 0, 0, 0, 2);
    }
    else
    {
        printf("ERROR!! PrintChoice(), The number of choices is illegal\n");
    }
    return;
}

// ──────────────────────────────────────────────────────────────────────
// GetMsgWinData: scan a message stream for its 0xF8 window-size control code,
// returning the resolved window origin (win_x/win_y).  Always returns 1.
u_char GetMsgWinData(DISP_STR *disp_wrk, MSG_WIN_DAT *win, int *win_x, int *win_y)
{
    u_char loop;
    u_char *str;
    float fx;
    float fy;

    loop = 1;
    str = disp_wrk->str;
    fx = (float)win->x;
    fy = (float)win->y;
    do
    {
        switch (*str)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
        case 0xf9:
        case 0xfd:
            str = str + 2;
            break;
        case 0xf4:
        case 0xf5:
        case 0xf6:
        case 0xfa:
        case 0xfe:
            str = str + 1;
            break;
        case 0xf7:
            Get2Byte(str + 1);
            Get2Byte(str + 3);
            str = str + 5;
            break;
        case 0xf8:
            fx = (float)(int)Get2Byte(str + 1);
            fy = (float)(int)Get2Byte(str + 3);
            Get2Byte(str + 5);
            Get2Byte(str + 7);
            str = str + 9;
            break;
        case 0xfb:
        case 0xff:
            str = str + 1;
            loop = 0;
            break;
        case 0xfc:
            str = str + 3;
            break;
        default:
            str = str + 1;
            break;
        }
    } while (loop);
    *win_x = (int)fx;
    *win_y = (int)fy;
    return 1;
}

// ──────────────────────────────────────────────────────────────────────
// GetMsgPageNum: count the number of pages (0xFB page breaks) in a message.
int GetMsgPageNum(int msg_type, int msg_id)
{
    u_char loop;
    u_char *msg_addr;
    int page_num;

    if ((u_int)msg_type > 0x52)
    {
        PRINT_ASSERT("Error! GetMsgPageNum msg_type %d", msg_type);
    }
    if (msg_id < 0 || GetMsgIDNumMax(msg_type) <= msg_id)
    {
        PRINT_ASSERT("Error! GetMsgPageNum msg_id %d", msg_id);
    }
    msg_addr = GetMsgDataAddr(msg_type, msg_id);
    loop = 1;
    page_num = 0;
    do
    {
        switch (*msg_addr)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
        case 0xf9:
        case 0xfd:
            msg_addr = msg_addr + 2;
            break;
        case 0xf4:
        case 0xf5:
        case 0xf6:
        case 0xfa:
        case 0xfe:
            msg_addr = msg_addr + 1;
            break;
        case 0xf7:
            msg_addr = msg_addr + 5;
            break;
        case 0xf8:
            msg_addr = msg_addr + 9;
            break;
        case 0xfb:
            msg_addr = msg_addr + 1;
            page_num = page_num + 1;
            break;
        case 0xfc:
            msg_addr = msg_addr + 3;
            break;
        case 0xff:
            msg_addr = msg_addr + 1;
            loop = 0;
            break;
        default:
            msg_addr = msg_addr + 1;
            break;
        }
    } while (loop);
    return page_num;
}

// ──────────────────────────────────────────────────────────────────────
// GetNowMsgPageNum: current page index of the active message.
int GetNowMsgPageNum()
{
    return (int)msg_disp_ctrl.pass_btn_wait;
}

// ──────────────────────────────────────────────────────────────────────
// SetMsgFirstPage: reset the active message back to its first page.
void SetMsgFirstPage()
{
    msg_disp_ctrl.pass_btn_wait = 0;
}

// ──────────────────────────────────────────────────────────────────────
// SetMsgPage: jump directly to page `page`.
void SetMsgPage(char page)
{
    msg_disp_ctrl.pass_btn_wait = page;
}

// ──────────────────────────────────────────────────────────────────────
// MsgColChange: apply palette entry col_label to a DISP_STR's RGB.
void MsgColChange(DISP_STR *disp, u_char col_label)
{
    disp->r = msg_col[col_label].r;
    disp->g = msg_col[col_label].g;
    disp->b = msg_col[col_label].b;
}

// ──────────────────────────────────────────────────────────────────────
// SetMsgFont: emit one glyph into the PK2D buffer for the given bank/pos at
// the DISP_STR's current pen position and colour.
static void SetMsgFont(Q_WORDDATA **pbuf, DISP_STR *disp_addr, int nbank, u_char nfn, u_char nfw,
                       u_char nfh)
{
    *pbuf = SetFontTex(*pbuf, 1, nbank);
    *pbuf = SetFontPat(*pbuf, 1, disp_addr->pri, nfn, disp_addr->pos_x, disp_addr->pos_y, nfw,
                       (u_char)disp_addr->r, (u_char)disp_addr->g, (u_char)disp_addr->b,
                       (u_char)disp_addr->alpha);
}

// ──────────────────────────────────────────────────────────────────────
// DrawSelItemMsg: draw a centred selectable item message with a selection
// frame (and optional cursor) sized to the measured line width.
void DrawSelItemMsg(int type, int no, int x, int y, int alpha, int bWithCsr, int col_id, int pri)
{
    int msg_length;

    msg_length = GetMsgLineLength(GetMsgDataAddr(type, no), (u_char **)0);
    if (bWithCsr != 0)
    {
        DrawCmnSelCsr(pri, (float)(x - msg_length / 2), (float)y, (u_char)alpha, (float)msg_length, 0);
    }
    DrawCmnSelFrame(pri, (float)(x - msg_length / 2), (float)y, (u_char)alpha, (float)msg_length);
    PrintMsg_Arrange(type, no, x, y, col_id, alpha, pri, 0, 0, 2);
}

// ──────────────────────────────────────────────────────────────────────
// GetMsgLineLength: measure the pixel width of one message line, summing the
// per-glyph widths and walking the control-code grammar (font bank switch,
// embedded numbers expanded via mes_ex_num_tbl, etc).
int GetMsgLineLength(u_char *msg_addr, u_char **pp_next_addr)
{
    static u_char normal_tbl[10] = { 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 };
    u_char *wrk_addr;
    int nbank;
    int i;
    int loop;
    u_char nfn;
    char cwo[256];
    u_char bank_label;
    u_char draw_flg;
    int str_width;
    u_char bank_tbl[5];
    int num;

    nfn = 0;
    str_width = 0;
    loop = 1;
    bank_tbl[0] = 1;
    bank_tbl[1] = 2;
    bank_tbl[2] = 3;
    bank_tbl[3] = 4;
    bank_tbl[4] = 5;
    do
    {
        draw_flg = 0;
        nbank = 1;
        switch (*msg_addr)
        {
        case 0xf0:
        case 0xf1:
        case 0xf2:
        case 0xf3:
            bank_label = Get1Byte(msg_addr);
            nfn = Get1Byte(msg_addr + 1);
            draw_flg = 1;
            msg_addr = msg_addr + 2;
            nbank = bank_tbl[(u_char)(bank_label + 0x11)];
            break;
        case 0xf7:
            msg_addr = msg_addr + 5;
            break;
        case 0xf8:
            msg_addr = msg_addr + 9;
            break;
        case 0xf9:
        case 0xfd:
            msg_addr = msg_addr + 2;
            break;
        case 0xfb:
        case 0xff:
            if (pp_next_addr != (u_char **)0)
            {
                *pp_next_addr = (u_char *)0;
            }
            loop = 0;
            break;
        case 0xfc:
            wrk_addr = msg_addr + 1;
            msg_addr = msg_addr + 2;
            bank_label = Get1Byte(wrk_addr);
            if (mes_ex_num_tbl[(char)bank_label] == 0 || 0 < (char)bank_label)
            {
                PRINT_ASSERT("Error! %s\n");
            }
            else
            {
                num = (*mes_ex_num_tbl[(char)bank_label])();
            }
            sprintf(cwo, "%d", num);
            if (cwo[0] != '\0')
            {
                i = 0;
                do
                {
                    nfn = normal_tbl[cwo[i] - '0'];
                    str_width = str_width + (char)GetFontSize(0, nfn);
                    i = i + 1;
                } while (cwo[i] != '\0');
            }
            break;
        case 0xfe:
            if (pp_next_addr != (u_char **)0)
            {
                *pp_next_addr = msg_addr + 1;
            }
            loop = 0;
            break;
        default:
            nfn = Get1Byte(msg_addr);
            draw_flg = 1;
            msg_addr = msg_addr + 1;
            break;
        }
        if (draw_flg)
        {
            str_width = str_width + (char)GetFontSize((u_char)nbank, nfn);
        }
        if (!loop)
        {
            return str_width;
        }
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// GetFontSize: per-bank glyph width lookup.
u_char GetFontSize(u_char bank_label, u_char nfn)
{
    int font_size;

    switch (bank_label)
    {
    case 2:
        font_size = (u_char)font_w_b1[nfn];
        break;
    case 3:
        font_size = (u_char)font_w_b2[nfn];
        break;
    case 4:
        font_size = (u_char)font_w_b3[nfn];
        break;
    case 5:
        font_size = (u_char)font_w_b4[nfn];
        break;
    default:
        font_size = (u_char)font_w_b0[nfn];
        break;
    }
    return (u_char)font_size;
}

// ──────────────────────────────────────────────────────────────────────
// SetMessageV2: low-level renderer that draws a DISP_STR directly (no paging,
// no window).  Each glyph is fixed 0x18 high; banks are selected per 0xF0-0xF3.
int SetMessageV2(DISP_STR *s)
{
    u_char *c;
    int n;
    int m;
    int loop;
    int npri;
    int nx;
    int ny;
    int nxw;
    int nyw;
    u_char nr;
    u_char ng;
    u_char nb;
    u_char na;
    u_char nfn;
    u_char nfw;
    char cwo[256];
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pbuf_top;
    int iter;

    loop = 1;
    nxw = s->x_wide;
    c = s->str;
    nyw = s->y_wide;
    nr = (u_char)s->r;
    ng = (u_char)s->g;
    nb = (u_char)s->b;
    na = (u_char)s->alpha;
    npri = s->pri;
    nx = s->pos_x;
    ny = s->pos_y;
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);
    do
    {
        n = *c;
        iter = loop;
        switch (n)
        {
        case 0xf0:
            nfn = c[1];
            c = c + 2;
            pbuf = SetFontTex(pbuf, 1, 2);
            pbuf = SetFontPat(pbuf, 1, npri, nfn, nx, ny, 0x18, nr, ng, nb, na);
            nx = nx + nxw + 0x18;
            break;
        case 0xf1:
            nfn = c[1];
            c = c + 2;
            pbuf = SetFontTex(pbuf, 1, 3);
            pbuf = SetFontPat(pbuf, 1, npri, nfn, nx, ny, 0x18, nr, ng, nb, na);
            nx = nx + nxw + 0x18;
            break;
        case 0xf2:
            nfn = c[1];
            c = c + 2;
            pbuf = SetFontTex(pbuf, 1, 4);
            pbuf = SetFontPat(pbuf, 1, npri, nfn, nx, ny, 0x18, nr, ng, nb, na);
            nx = nx + nxw + 0x18;
            break;
        case 0xf3:
            nfn = c[1];
            c = c + 2;
            pbuf = SetFontTex(pbuf, 1, 5);
            pbuf = SetFontPat(pbuf, 1, npri, nfn, nx, ny, 0x18, nr, ng, nb, na);
            nx = nx + nxw + 0x18;
            break;
        case 0xfb:
        case 0xfe:
            c = c + 1;
            nx = s->pos_x;
            ny = ny + nyw + 0x18;
            break;
        case 0xfc:
            nfn = c[1];
            c = c + 3;
            sprintf(cwo, "%d", m);
            for (n = 0; cwo[n] != '\0' && loop != 2; n++)
            {
                if (nfn == 0)
                {
                    m = (u_char)(cwo[n] + 0xf);
                }
                else
                {
                    m = (u_char)(cwo[n] + 5);
                }
                nfw = (u_char)font_w_b0[m];
                pbuf = SetFontTex(pbuf, 1, 1);
                pbuf = SetFontPat(pbuf, 1, npri, m, nx, ny, nfw, nr, ng, nb, na);
                nx = nx + nfw + nxw;
                if (cwo[n + 1] == '\0')
                {
                    break;
                }
                if (loop == 2)
                {
                    iter = 2;
                    break;
                }
            }
            break;
        case 0xfd:
            nr = c[1];
            ng = c[2];
            nb = c[3];
            c = c + 4;
            break;
        case 0xff:
            c = c + 1;
            loop = 0;
            s->st = s->st | 0x40;
            iter = loop;
            break;
        default:
            nfw = (u_char)font_w_b0[n];
            pbuf = SetFontTex(pbuf, 1, 1);
            c = c + 1;
            pbuf = SetFontPat(pbuf, 1, npri, n, nx, ny, nfw, nr, ng, nb, na);
            nx = nx + nfw + nxw;
            break;
        }
        if (iter == 0)
        {
            EndPK2Dbuf(pbuf);
            AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
            return s->csr + 1;
        }
    } while (1);
}

// ──────────────────────────────────────────────────────────────────────
// SubMessageV3: the typewriter message engine (msdat state machine).  Draws
// the message a glyph at a time up to fntcnt, handles colour/underline runs,
// blink (sta), end-fade (alp) and the per-frame font-reveal counter.
int SubMessageV3(u_char *s, int pri, int delflg)
{
    u_char *c;
    int n;
    int m;
    int loop;
    int npri;
    int nx;
    int ny;
    u_char nr;
    u_char ng;
    u_char nb;
    u_char na;
    u_char nfn;
    u_char nfw;
    u_char nul;
    char cwo[256];
    Q_WORDDATA *pbuf;
    Q_WORDDATA *pbuf_top;
    int fade;

    if (msdat.pass != 0)
    {
        return 0;
    }

    if (msdat.flg == 0)
    {
        if (s == (u_char *)0)
        {
            printf("message address is NULL!!\n", pri, delflg);
            return 0;
        }
        msdat.stp = s + 6;
        msdat.bx = (u_int)s[1] * 0x100 + (u_int)s[0];
        msdat.by = (u_int)s[3] * 0x100 + (u_int)s[2];
        msdat.pri = (int)s[4];
        msdat.flg = 1;
        msdat.usrgb[3] = 0x80;
        msdat.disptype = (int)s[5];
        msdat.sta = 0;
        msdat.retst = 0;
        msdat.csr = 0;
        msdat.decide = 0;
        msdat.mes_is_end = 0;
        msdat.cnt = 0;
        msdat.r = 0x80;
        msdat.g = 0x80;
        msdat.b = 0x80;
        msdat.usrgb[0] = 0;
        msdat.usrgb[1] = 0x80;
        msdat.usrgb[2] = 0x80;
        msdat.vib = 0;
        msdat.alp = 0;

        if (s[5] == 0)
        {
            msdat.fntmcnt = 0xffff;
        }
        else
        {
            msdat.fntmcnt = 0;
        }
        msdat.fntwait = 0;
    }
    c = msdat.stp;
    na = msdat.alp;
    nr = msdat.r;
    ny = msdat.by;
    nx = msdat.bx;
    npri = msdat.pri;
    loop = 0;
    msdat.fntcnt = 0;
    nul = msdat.usrgb[0];
    ng = msdat.g;
    nb = msdat.b;
    SetFontEnv();
    pbuf_top = GetPK2Dbuf();
    pbuf = SetFontTex(pbuf_top, 0, 1);

    do
    {
        n = *c;
        switch (n)
        {
        case 0xf0:
            nfn = c[1];
            c = c + 2;
            nfw = (u_char)font_w_b1[nfn];
            pbuf = SetFontTex(pbuf, 1, 2);
            m = nfn;
            goto draw_glyph;
        case 0xf1:
            nfn = c[1];
            c = c + 2;
            nfw = (u_char)font_w_b2[nfn];
            pbuf = SetFontTex(pbuf, 1, 3);
            m = nfn;
            goto draw_glyph;
        case 0xf2:
            nfn = c[1];
            c = c + 2;
            nfw = (u_char)font_w_b3[nfn];
            pbuf = SetFontTex(pbuf, 1, 4);
            m = nfn;
            goto draw_glyph;
        case 0xf3:
            nfn = c[1];
            c = c + 2;
            nfw = (u_char)font_w_b4[nfn];
            pbuf = SetFontTex(pbuf, 1, 5);
            m = nfn;
            goto draw_glyph;
        case 0xfa:
            nul = c[1];
            msdat.usrgb[1] = c[2];
            msdat.usrgb[2] = c[3];
            msdat.usrgb[3] = c[4];
            c = c + 5;
            break;
        case 0xfb:
            c = c + 1;
            if ((msdat.sta & 1) == 0)
            {
                msdat.sta = msdat.sta | 0x10;
            }
            else
            {
                msdat.sta = msdat.sta ^ 0x1f;
                if (msdat.disptype != 0)
                {
                    msdat.fntmcnt = 0;
                    msdat.fntwait = 0;
                }
                msdat.vib = 0;
                msdat.g = ng;
                msdat.b = nb;
                msdat.usrgb[0] = nul;
                nx = msdat.bx;
                ny = msdat.by;
                msdat.r = nr;
                msdat.stp = c;
            }
            loop = 1;
            break;
        case 0xfc:
            nfn = c[1];
            c = c + 3;
            sprintf(cwo, "%d", m);
            n = 0;
            while (cwo[n] != 0 && loop != 2)
            {
                if (nfn == 0)
                {
                    m = (u_char)(cwo[n] + 0xf);
                }
                else
                {
                    m = (u_char)(cwo[n] + 5);
                }
                nfw = (u_char)font_w_b0[m];
                pbuf = SetFontTex(pbuf, 1, 1);
                n = n + 1;
                pbuf = SetFontPat(pbuf, 1, npri, m, nx, ny, nfw, nr, ng, nb, na);
                pbuf = SetUnderLine(pbuf, nul, npri, nx, ny, nfw, msdat.usrgb[1], msdat.usrgb[2],
                                    msdat.usrgb[3], na);
                msdat.fntcnt = msdat.fntcnt + 1;
                nx = nx + nfw;
                if (msdat.fntmcnt <= msdat.fntcnt)
                {
                    loop = 2;
                }
            }
            break;
        case 0xfd:
            nr = c[1];
            ng = c[2];
            nb = c[3];
            c = c + 4;
            break;
        case 0xfe:
            c = c + 1;
            ny = ny + 0x18;
            nx = msdat.bx;
            break;
        case 0xff:
            if (msdat.mes_is_end == 0)
            {
                if ((msdat.sta & 1) == 0)
                {
                    msdat.sta = msdat.sta | 0x40;
                }
                else
                {
                    msdat.mes_is_end = 1;
                }
            }
            loop = 1;
            break;
        default:
            nfw = (u_char)font_w_b0[n];
            pbuf = SetFontTex(pbuf, 1, 1);
            m = n;
            c = c + 1;
        draw_glyph:
            pbuf = SetFontPat(pbuf, 1, npri, m, nx, ny, nfw, nr, ng, nb, na);
            pbuf = SetUnderLine(pbuf, nul, npri, nx, ny, nfw, msdat.usrgb[1], msdat.usrgb[2],
                                msdat.usrgb[3], na);
            msdat.fntcnt = msdat.fntcnt + 1;
            nx = nx + nfw;
            break;
        }
        if (msdat.fntmcnt <= msdat.fntcnt)
        {
            loop = 2;
        }
    } while (loop == 0);
    if (loop == 2 && msdat.fntwait-- < 1)
    {
        msdat.fntmcnt = msdat.fntmcnt + 1;
        msdat.fntwait = 4;
    }
    if ((msdat.sta & 0x10) != 0)
    {
        msdat.cnt = msdat.cnt + 1;
    }
    if (msdat.mes_is_end == 1)
    {
        if (msdat.alp == 0)
        {
            msdat.mes_is_end = 2;
        }
        else
        {
            fade = msdat.alp - 8;
            if (fade < 0)
            {
                fade = 0;
            }
            msdat.alp = (u_char)fade;
        }
    }
    else if (msdat.mes_is_end == 0 && (char)msdat.alp >= 0)
    {
        fade = msdat.alp + 8;
        if (0x80 < fade)
        {
            fade = 0x80;
        }
        msdat.alp = (u_char)fade;
    }
    EndPK2Dbuf(pbuf);
    AddCNTtag(pbuf_top, ((ptrdiff_t)pbuf - (ptrdiff_t)pbuf_top >> 4) - 1);
    MesKeyCheck();
    msdat.pass = 1;
    return msdat.retst;
}

// ──────────────────────────────────────────────────────────────────────
// SetMessageV3: typewriter message, page deleted on completion.
int SetMessageV3(u_char *s, int pri)
{
    return SubMessageV3(s, pri, 1);
}

// ──────────────────────────────────────────────────────────────────────
// SetMessageV3_2: typewriter message, page retained on completion.
int SetMessageV3_2(u_char *s, int pri)
{
    return SubMessageV3(s, pri, 0);
}

// ──────────────────────────────────────────────────────────────────────
// MesPassCheck: per-frame guard — clears the once-per-frame pass latch (and
// the flg if SubMessageV3 was never called this frame).
void MesPassCheck()
{
    if (msdat.pass == 0)
    {
        msdat.flg = 0;
    }
    msdat.pass = 0;
}

// ──────────────────────────────────────────────────────────────────────
// MesKeyCheck: latch the "advance" bit when the player presses the confirm
// button during a blink (sta&0x10) or end (sta&0x40) state.
static void MesKeyCheck()
{
    if ((msdat.sta & 0x10) != 0 && *paddat[3] == 1)
    {
        msdat.sta = msdat.sta | 1;
    }
    if ((msdat.sta & 0x40) != 0)
    {
        msdat.sta = msdat.sta | 1;
    }
}

// ──────────────────────────────────────────────────────────────────────
// MesStatusCheck: report the windowed-message display state.
int MesStatusCheck()
{
    return (int)msg_disp_ctrl.disp_state;
}

// ──────────────────────────────────────────────────────────────────────
// MesSetNextPage: advance the windowed message to the next page when waiting.
void MesSetNextPage()
{
    if (msg_disp_ctrl.disp_state == 1)
    {
        msg_disp_ctrl.pass_btn_wait++;
    }
}

// ──────────────────────────────────────────────────────────────────────
// MesSetBeforePage: step the windowed message back one page when waiting.
void MesSetBeforePage()
{
    if (msg_disp_ctrl.disp_state == 1)
    {
        msg_disp_ctrl.pass_btn_wait--;
        if ((char)msg_disp_ctrl.pass_btn_wait < 0)
        {
            msg_disp_ctrl.pass_btn_wait = 0;
        }
    }
}

// ──────────────────────────────────────────────────────────────────────
// GetMsgDataAddr: resolve type/id into the message byte stream via the
// event-binary relocation table (base 0xd9ec00).
u_char *GetMsgDataAddr(int msg_type, int msg_id)
{
    u_char *addr;

    if ((u_int)msg_type > 0x52)
    {
        PRINT_ASSERT("Error! GetMsgDataAddr msg_type %d", msg_type);
    }

    if (msg_id < 0 || GetMsgIDNumMax(msg_type) <= msg_id)
    {
        PRINT_ASSERT("Error! GetMsgDataAddr msg_id %d", msg_id);
    }

    addr = EvBinChangeAddr4((u_char *)MioPan_GetHostPointer(0xd9ec00), (u_char *)(msg_type * 4 + (uintptr_t)MioPan_GetHostPointer(0xd9ec00)));
    addr = EvBinChangeAddr4((u_char *)MioPan_GetHostPointer(0xd9ec00), addr + msg_id * 4);

    return addr;
}

// ──────────────────────────────────────────────────────────────────────
// PrintNumber: draw a right-justified integer (up to 6 digits) suppressing
// leading zeros.  type 0 = full-width (0x18 pitch), 1 = half-width (0xc).
void PrintNumber(int data, int x, int y, u_char col_label, u_char alpha, int pri, u_char type)
{
    int i;
    int j;
    int ten_tmp;
    u_char set_flg;
    int x_half;

    set_flg = 0;
    if (data < 0)
    {
        PRINT_ASSERT("Error! PrintNumber data %d", data);
    }
    if (0x2c < col_label)
    {
        PRINT_ASSERT("Error! PrintNumber col_label %d", col_label);
    }

    i = 7;
    x_half = x;

    do
    {
        i = i - 1;
        ten_tmp = 1;
        if (1 <= i)
        {
            for (j = i; j != 0; j--)
            {
                ten_tmp = ten_tmp * 10;
            }
        }
        j = data / ten_tmp;
        if (j != 0)
        {
            set_flg = 1;
        }
        if (set_flg)
        {
            if (type == 0)
            {
                PrintNumber_One(j, x, y, col_label, alpha, pri, 0);
            }
            else if (type == 1)
            {
                PrintNumber_One(j, x_half, y, col_label, alpha, pri, 1);
            }
            x_half = x_half + 0xc;
            x = x + 0x18;
        }
    } while (0 < i);
}

// ──────────────────────────────────────────────────────────────────────
// PrintNumber_N: like PrintNumber but with a fixed digit count `num`.  When
// zero_flg is 0 the last digit of a zero value is still printed.
void PrintNumber_N(int data, int num, int x, int y, u_char col_label, u_char alpha, int pri,
                   u_char type, int zero_flg)
{
    int i;
    int j;
    int ten_tmp;
    u_char set_flg;
    int x_half;

    if (data < 0)
    {
        PRINT_ASSERT("Error! PrintNumber_N data %d", data);
    }

    if (num < 0)
    {
        PRINT_ASSERT("Error! PrintNumber_N num %d", num);
    }

    if (0x2c < col_label)
    {
        PRINT_ASSERT("Error! PrintNumber_N col_label %d", col_label);
    }
    set_flg = (u_char)((zero_flg & 0xff) == 1);
    x_half = x;
    if (0 < num)
    {
        do
        {
            ten_tmp = 1;
            i = num - 1;
            if (1 <= i)
            {
                for (j = i; j != 0; j--)
                {
                    ten_tmp = ten_tmp * 10;
                }
            }
            j = data / ten_tmp;
            if (j != 0)
            {
                set_flg = 1;
            }
            if ((zero_flg & 0xff) == 0 && data == 0 && num == 1)
            {
                set_flg = 1;
            }
            if (set_flg)
            {
                if (type == 0)
                {
                    PrintNumber_One(j, x, y, col_label, alpha, pri, 0);
                }
                else if (type == 1)
                {
                    PrintNumber_One(j, x_half, y, col_label, alpha, pri, 1);
                }
            }
            x = x + 0x12;
            num = i;
            x_half = x_half + 0xe;
        } while (0 < i);
    }
}

// ──────────────────────────────────────────────────────────────────────
// PrintNumber_One: draw a single digit glyph via the message renderer.
// type 0 selects the full-width digit glyphs, type 1 the half-width ones.
void PrintNumber_One(int data, int x, int y, u_char col_label, u_char alpha, int pri, u_char type)
{
    u_char str[2];
    DISP_STR msg_data;
    u_char em_size_tbl[10] = { 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e };
    u_char normal_tbl[10] = { 0x3f, 0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48 };

    msg_data.str = str;
    msg_data.pri = (pri & 0xf) << 4;

    if (type == 0)
    {
        str[0] = em_size_tbl[data % 10];
    }
    else if (type == 1)
    {
        str[0] = normal_tbl[data % 10];
    }

    msg_data.alpha = (int)alpha;
    str[1] = 0xff;
    msg_data.pos_x = x;
    msg_data.pos_y = y;
    MsgColChange(&msg_data, col_label);
    PrintMsg(&msg_data, 0);
}

// ──────────────────────────────────────────────────────────────────────
// GetMsgIDNumMax: number of message IDs defined for a message type.
int GetMsgIDNumMax(int msg_type)
{
    if (0x52 < msg_type)
    {
        PRINT_ASSERT("Error! GetMsgIDNumMax msg_type %d", msg_type);
    }
    
    return msg_type_max_tbl[msg_type];
}

// ──────────────────────────────────────────────────────────────────────
// FontDispSample: debug helper — build a 0..0xEF glyph table (line-broken
// every 0x18 chars) and draw it via SetMessageV2.
void FontDispSample()
{
    DISP_STR ds;
    STR_DAT sd = { 0, 0, 0, 1, 255, 255, 255, 128, 80 };
    int i;
    int n;
    u_char stra[250];

    n = 0;
    i = 0;
    do
    {
        if (i % 0x18 == 0)
        {
            stra[n] = 0xfe;
            n++;
        }
        stra[n] = (u_char)i;
        i++;
        n++;
    } while (i < 0xf0);

    stra[n] = 0xff;
    CopyStrDToStr(&ds, &sd);
    ds.pri = 0x20;
    ds.pos_x = 0x28;
    ds.pos_y = 0x5a;
    ds.str = stra;
    SetMessageV2(&ds);
}

// ──────────────────────────────────────────────────────────────────────
// DebugPrintMsgDef_W: debug helper to print one message with default window
// data, optionally inside the common window frame.
void DebugPrintMsgDef_W(int msg_type, int msg_id, u_char win_flg)
{
    DISP_STR ds;
    MSG_WIN_DAT win_ctrl;

    SetMsgDefData(&ds, msg_type);
    SetMsgWinDefData(&win_ctrl, msg_type);
    ds.str = GetMsgDataAddr(msg_type, msg_id);
    msg_disp_ctrl.init_flg = 1;
    if (win_flg == 0)
    {
        PrintMsg_W(&ds, (MSG_WIN_DAT *)0);
    }
    else
    {
        PrintMsg_W(&ds, &win_ctrl);
    }
}

// ──────────────────────────────────────────────────────────────────────
// DebugMsgCtrlInit: reset the windowed message display control block.
void DebugMsgCtrlInit()
{
    msg_disp_ctrl.init_flg = 1;
    msg_disp_ctrl.disp_state = 0;
    msg_disp_ctrl.pass_btn_wait = 0;
}

// ──────────────────────────────────────────────────────────────────────
// DebugMsgDataCheck: sanity-check that the message tables for key label types
// contain at least the expected number of entries.
void DebugMsgDataCheck()
{

    if (GetMsgIDNumMax(3) < 0xb)
    {
        PRINT_ASSERT("CHAPTER_NAME_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x1e) / 3 < 0x2a)
    {
        PRINT_ASSERT("FILE_PBOOK_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x20) / 3 < 0x2a)
    {
        PRINT_ASSERT("FILE_SCRAP_MES_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x1d) / 3 < 0x28)
    {
        PRINT_ASSERT("FILE_OLD_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x1f) / 3 < 0x1a)
    {
        PRINT_ASSERT("FILE_PHOTO_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x1b) / 3 < 10)
    {
        PRINT_ASSERT("FILE_OLD_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x4a) < 0xf0)
    {
        PRINT_ASSERT("ROOM_NAME_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0xb) < 7)
    {
        PRINT_ASSERT("DOOR_LOCK_MES_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x2c) < 0x3a)
    {
        PRINT_ASSERT("ITEM_NAME_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x2b) < 0x3a)
    {
        PRINT_ASSERT("ITEM_MSG_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x52) < 0x3a)
    {
        PRINT_ASSERT("USE_ITEM_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x3d) < 0x11)
    {
        PRINT_ASSERT("M_MAP_NAME_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x51) < 0x11)
    {
        PRINT_ASSERT("S_MAP_NAME_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x48) < 0x28)
    {
        PRINT_ASSERT("REISEKI_MSG_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x49) < 0x28)
    {
        PRINT_ASSERT("REISEKI_NAME_LBL Msg Data Error!", "");
    }
    if (GetMsgIDNumMax(0x3b) / 3 < 0xb5)
    {
        PRINT_ASSERT("MES_GLIST_LBL Msg Data Error!", "");
    }
}
