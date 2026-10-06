/* ==========================================================================
 *  graphics/graph2d/draw_cmn.h
 *
 *  Common 2D UI drawing primitives shared by the menu / HUD code: decorative
 *  lines, button icons, captions and caption groups, textured numbers, and the
 *  common window / selection widgets.  These are thin helpers built on the
 *  g2d_draw.c sprite/square layer.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_GRAPH2D_DRAW_CMN_H
#define _GRAPHICS_GRAPH2D_DRAW_CMN_H

#include "g2d_draw.h"               /* SPRT_DAT (number strip arg) */

/* --------------------------------------------------------------------------
 *  Caption / button group tables (graph2d/tim_dat/cap_group_dat.c).
 *
 *  A group is a list of pieces terminated by a negative label; each piece
 *  carries a texture label (index into caption_tex[] / btn_tex[]) and a
 *  per-language x offset (x[GetLanguage()]).  cap_btn_group_ctrl[] pairs the
 *  caption list and the button list for each of the 15 group ids.
 * ------------------------------------------------------------------------ */
typedef struct                      /* 0x18 */
{
    /* 0x00 */ char cap_label;
    /* 0x04 */ int  x[5];
} CAP_GROUP;

typedef struct                      /* 0x18 */
{
    /* 0x00 */ char btn_label;
    /* 0x04 */ int  x[5];
} BTN_GROUP;

typedef struct                      /* 0x08 */
{
    /* 0x00 */ CAP_GROUP *cap_group;
    /* 0x04 */ BTN_GROUP *btn_group;
} CAP_BTN_GROUP_CTRL;

/* --------------------------------------------------------------------------
 *  Lines (draw_line.c)
 * ------------------------------------------------------------------------ */
void DrawCmnLine(float x, float y, float size, u_char type, u_char alp, u_int pri);

/* --------------------------------------------------------------------------
 *  Button icons (draw_btn.c)
 * ------------------------------------------------------------------------ */
void DrawCmnButton(u_char btn_label, float x, float y, u_char alp, u_int pri);

/* --------------------------------------------------------------------------
 *  Captions and caption/button groups (draw_caption.c)
 * ------------------------------------------------------------------------ */
void DrawCmnCaption(u_char cap_label, float x, float y, u_char alp, u_int pri);
void DrawCmnCapGroup(int group_label, int x, int y, u_char alp, u_int pri);
void DrawCmnCapGroup_W(int group_label, int world_label, u_char alp, u_int pri);

/* --------------------------------------------------------------------------
 *  Textured numbers (draw_cmn_num.c)
 * ------------------------------------------------------------------------ */
void DrawCmnNumberTex(int data, int num, SPRT_DAT *zero_dat, int x, int y,
                      u_char alpha, int pri, u_char zero_flg);

/* --------------------------------------------------------------------------
 *  Common window / selection widgets (draw_cmn_win.c)
 * ------------------------------------------------------------------------ */
void DrawCmnWindow(u_int pri, float x, float y, float sizew, float sizeh,
                   u_char alpha, u_char max_alpha);
void DrawCmnTwoLineWindow(u_int pri, float x, float y, float sizew, float sizeh,
                          u_char alpha, u_char max_alpha);
void DrawCmnFileWindow(int file_type, int file_id, u_int pri, u_char alpha,
                       u_char max_alpha);
void DrawCmnYesNoSel(int cursor, float y, u_char alpha, u_int pri);
void DrawCmnSelFrame(u_int pri, float x, float y, u_char alpha, float w);
void DrawCmnSelYes(u_int pri, float x, float y, u_char alpha);
void DrawCmnSelNo(u_int pri, float x, float y, u_char alpha);
void DrawCmnSelCsr(u_int pri, float x, float y, u_char alpha, float w, u_char flg);
void DrawCmnTriCsrL(u_int pri, float x, float y, u_char alpha, u_char rgb);
void DrawCmnTriCsrR(u_int pri, float x, float y, u_char alpha, u_char rgb);

#endif /* _GRAPHICS_GRAPH2D_DRAW_CMN_H */
