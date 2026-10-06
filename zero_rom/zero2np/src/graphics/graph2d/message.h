/* ==========================================================================
 *  graphics/graph2d/message.h
 *
 *  The font / message / string-rendering system.  Owns the runtime message
 *  work records (MES_DAT / MES_WRK / MSG_DISP_CTRL / MSG_EXE_CTRL / MSG_COLOR
 *  / MSG_WIN_DAT) and declares the public message draw / query API built on
 *  the g2d_draw.c PK2D packet ring and the DISP_STR / STR_DAT text records.
 *
 *  The message text is Shift-JIS with an embedded control-code stream: byte
 *  values 0xF0-0xFF are escape codes (bank switch, position set, window size,
 *  arrange, colour, page break, extended number, line break, terminator); the
 *  remaining bytes index the per-bank font glyph tables.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_MESSAGE_H
#define _GRAPHICS_GRAPH2D_MESSAGE_H

#include <sys/types.h>              /* u_char / u_short / u_int / u_long */
#include "g2d_draw.h"               /* DISP_STR / STR_DAT / Q_WORDDATA */
#include "../draw_env.h"            /* DRAW_ENV_5 (canonical draw-env override) */

// ──────────────────────────────────────────────────────────────────────
// Message work records (this TU owns them).

typedef struct                      /* 0x50 */
{
    /* 0x00 */ int     pri;
    /* 0x04 */ int     bx;
    /* 0x08 */ int     by;
    /* 0x0c */ u_char  r;
    /* 0x0d */ u_char  g;
    /* 0x0e */ u_char  b;
    /* 0x0f */ u_char  alp;
    /* 0x10 */ u_char *str;
    /* 0x14 */ u_char *stp;
    /* 0x18 */ int     sta;
    /* 0x1c */ int     flg;
    /* 0x20 */ int     pass;
    /* 0x24 */ int     csr;
    /* 0x28 */ int     decide;
    /* 0x2c */ int     mes_is_end;
    /* 0x30 */ int     cnt;
    /* 0x34 */ int     retst;
    /* 0x38 */ int     disptype;
    /* 0x3c */ int     fntmcnt;
    /* 0x40 */ int     fntcnt;
    /* 0x44 */ int     fntwait;
    /* 0x48 */ u_char  usrgb[4];
    /* 0x4c */ u_char  vib;
    /* 0x4d */ u_char  bx_pass;
    /* 0x4e */ u_char  bx_pass_old;
    /* 0x4f */ u_char  bx_pass_st;
} MES_DAT;

typedef struct                      /* 0x1 */
{
    /* 0x0 */ u_char texbank;
} MES_WRK;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ char   pass_btn_wait;
    /* 0x1 */ u_char disp_state;
    /* 0x2 */ u_char init_flg;
    /* 0x4 */ int    cnt;
} MSG_DISP_CTRL;

typedef struct                      /* 0x14 */
{
    /* 0x00 */ u_char disp_type;
    /* 0x04 */ int    now_bank;
    /* 0x08 */ int    start_x;
    /* 0x0c */ int    start_y;
    /* 0x10 */ u_char font_pos;
    /* 0x11 */ u_char font_w;
    /* 0x12 */ u_char font_h;
    /* 0x13 */ u_char arrange;
} MSG_EXE_CTRL;

typedef struct                      /* 0x3 */
{
    /* 0x0 */ u_char r;
    /* 0x1 */ u_char g;
    /* 0x2 */ u_char b;
} MSG_COLOR;

typedef struct                      /* 0x10 */
{
    /* 0x0 */ float x;
    /* 0x4 */ float y;
    /* 0x8 */ float w;
    /* 0xc */ float h;
} MSG_WIN_DAT;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int x;
    /* 0x4 */ int y;
} MSG_DEF_DATA;

typedef struct                      /* 0x8 */
{
    /* 0x0 */ int msg_def_id;
    /* 0x4 */ int msg_win_id;
} MSG_DISP_DATA;

/* DRAW_ENV_5 (the draw-env override used by MessageChangeDrawEnv) is the
 * canonical type from graphics/draw_env.h, included above. */

// ──────────────────────────────────────────────────────────────────────
// Public entry points (message.c).

void    InitMessage(void);
void    InitMessageEF(void);

/* integer / ASCII / formatted-string helpers */
void    SetInteger(float x, float y, int num);
void    SetInteger2(int pri, float x, float y, int type, u_char r, u_char g, u_char b, int num);
void    SetInteger3(int pri, float x, float y, int type, u_char r, u_char g, u_char b, u_char a, int num);
void    SetASCIIString(float x, float y, char *str);
void    SetASCIIString2(int pri, float x, float y, int type, u_char r, u_char g, u_char b, char *str);
void    SetASCIIString3(int pri, float x, float y, int type, u_char r, u_char g, u_char b, u_char a, char *str);
void    SetASCIIString4(int pri, float x, float y, int type, u_char r, u_char g, u_char b, char *str);
void    SetWString2(int pri, float x, float y, u_char r, u_char g, u_char b, char *str);
void    SetString(float x, float y, char *fmt, ...);
void    SetString2(int pri, float x, float y, int type, u_char r, u_char g, u_char b, char *fmt, ...);

void    MessageChangeDrawEnv(DRAW_ENV_5 *p_change_env);
void    SetFontEnv(void);

/* string measurement */
int     GetStrLength(u_char *str);
int     GetStrWidthMain(u_char *str, int type);
int     GetStrWidth(u_char *str);
void    CopyStrDToStr(DISP_STR *s, STR_DAT *d);

int     SetMessageV2_2(DISP_STR *s);
void    MsgDispCtrlInit(void);

/* default-window draw + setup */
void    PrintMsgDef_W(int msg_type, int msg_id);
void    SetMsgDefData(DISP_STR *msg_data, int msg_type);
void    SetMsgWinDefData(MSG_WIN_DAT *win_data, int msg_type);

/* PrintMsg overload set (C++ overloading) */
int     PrintMsg(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri);
int     PrintMsg(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri, int x_wide, int y_wide);
int     PrintMsg(DISP_STR *disp_addr, int one_line_flg);

int     PrintMsgOneLine(DISP_STR *disp_addr, MSG_EXE_CTRL *pCtrl, Q_WORDDATA **ppbuf);
int     PrintMsg_ArrangeOneLine(u_char *pAdrs, int x, int y, int col_label, int alpha, int pri, int x_wide, int y_wide, int arrange);

/* PrintMsg_T overload set */
int     PrintMsg_T(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri);
int     PrintMsg_T(DISP_STR *disp_addr);

int     PrintMsg_P(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri, int x_wide, int y_wide);
int     PrintMsg_W(DISP_STR *disp_addr, MSG_WIN_DAT *win_ctrl);
int     PrintMsg_TW(DISP_STR *disp_addr, MSG_WIN_DAT *win_ctrl);
int     PrintMsg_Arrange(int msg_type, int msg_id, int x, int y, int col_label, int alpha, int pri, int x_wide, int y_wide, int arrange);

void    PrintChoice(DISP_STR *msg, MSG_WIN_DAT *win, int choice_msg, int *choice, int csr);
u_char  GetMsgWinData(DISP_STR *disp_wrk, MSG_WIN_DAT *win, int *win_x, int *win_y);

/* message-table queries */
int     GetMsgPageNum(int msg_type, int msg_id);
int     GetNowMsgPageNum(void);
void    SetMsgFirstPage(void);
void    SetMsgPage(char page);
void    MsgColChange(DISP_STR *disp, u_char col_label);
void    DrawSelItemMsg(int type, int no, int x, int y, int alpha, int bWithCsr, int col_id, int pri);
int     GetMsgLineLength(u_char *msg_addr, u_char **pp_next_addr);
u_char  GetFontSize(u_char bank_label, u_char nfn);

/* legacy V2 / V3 message stream renderers */
int     SetMessageV2(DISP_STR *s);
int     SubMessageV3(u_char *s, int pri, int delflg);
int     SetMessageV3(u_char *s, int pri);
int     SetMessageV3_2(u_char *s, int pri);
void    MesPassCheck(void);
int     MesStatusCheck(void);
void    MesSetNextPage(void);
void    MesSetBeforePage(void);
u_char *GetMsgDataAddr(int msg_type, int msg_id);

/* digit drawing */
void    PrintNumber(int data, int x, int y, u_char col_label, u_char alpha, int pri, u_char type);
void    PrintNumber_N(int data, int num, int x, int y, u_char col_label, u_char alpha, int pri, u_char type, int zero_flg);
void    PrintNumber_One(int data, int x, int y, u_char col_label, u_char alpha, int pri, u_char type);

int     GetMsgIDNumMax(int msg_type);

/* debug helpers */
void    FontDispSample(void);
void    DebugPrintMsgDef_W(int msg_type, int msg_id, u_char win_flg);
void    DebugMsgCtrlInit(void);
void    DebugMsgDataCheck(void);

#endif /* _GRAPHICS_GRAPH2D_MESSAGE_H */
