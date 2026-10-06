// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/draw_btn.c
//
// Common button-icon sprite.  Draws one entry of the shared button texture
// table (btn_tex) at (x,y) with the given alpha and priority.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // DISP_SPRT / SPRT_DAT / CopySprDToSpr / DispSprD
#include "draw_cmn.h"               // DrawCmnButton prototype
#include "tim_dat/btn_sprt_dat.h"   // btn_tex[] button-icon sprite table

// ──────────────────────────────────────────────────────────────────────
// Draw button icon `btn_label`.

void DrawCmnButton(u_char btn_label, float x, float y, u_char alp, u_int pri)
{
    DISP_SPRT btn_ds;

    CopySprDToSpr(&btn_ds, btn_tex + btn_label);
    btn_ds.z = 0xfffff - (pri & 0xfffff);
    btn_ds.x = x;
    btn_ds.y = y;
    btn_ds.pri = pri;
    btn_ds.alpha = alp;
    DispSprD(&btn_ds);
}
