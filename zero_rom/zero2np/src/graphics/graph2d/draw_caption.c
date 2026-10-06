// FILE: /home/zero_rom/zero2np/src/graphics/graph2d/draw_caption.c
//
// Common caption sprites and caption/button groups.
//
//   * DrawCmnCaption()    - draw one caption-texture entry (label 0 maps to the
//                           default caption at index 3).
//   * DrawCmnCapGroup()   - draw a whole button+caption group: walk the group's
//                           BTN_GROUP / CAP_GROUP tables (terminated by a
//                           negative label) and place each piece, offsetting by
//                           the per-language x from the table.
//   * DrawCmnCapGroup_W() - as above, positioned at the per-world caption anchor
//                           (cap_world_pos_x/y), language-indexed.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "g2d_draw.h"               // DISP_SPRT / SPRT_DAT / CopySprDToSpr / DispSprD
#include "draw_cmn.h"               // DrawCmn* prototypes

#include "../../system/os/system.h" // GetLanguage

#include "../../common/utility2.h" // PRINT_ASSERT + SetAssertPreMessage / PrintAssertReal

#include "tim_dat/caption_dat.h"    // caption_tex[] caption-icon sprites
#include "tim_dat/cap_group_dat.h"  // cap_btn_group_ctrl[] / cap_world_pos_x/y[][]

// ──────────────────────────────────────────────────────────────────────
// Draw caption `cap_label` (label 0 selects the default caption at index 3).

void DrawCmnCaption(u_char cap_label, float x, float y, u_char alp, u_int pri)
{
    DISP_SPRT cap_ds;

    // CopySprDToSpr overwrites x/y/z/pri/alpha from the sprite descriptor (whose
    // x/y are 0,0 for the caption entries), so the caller's anchor and draw
    // params must be applied AFTER the copy — otherwise the caption snaps to the
    // screen's left edge.  (The original stores x@0x24, y@0x28, pri@0x78,
    // alpha@0x7f, z@0x2c all after the CopySprDToSpr call.)
    if (cap_label == '\0')
    {
        CopySprDToSpr(&cap_ds, caption_tex + 3);
    }
    else
    {
        CopySprDToSpr(&cap_ds, caption_tex + cap_label);
    }

    cap_ds.z = 0xfffff - (pri & 0xfffff);
    cap_ds.x = x;
    cap_ds.y = y;
    cap_ds.pri = pri;
    cap_ds.alpha = alp;
    DispSprD(&cap_ds);
}

// ──────────────────────────────────────────────────────────────────────
// Draw the button row and caption row of a caption group at (x,y).

void DrawCmnCapGroup(int group_label, int x, int y, u_char alp, u_int pri)
{
    BTN_GROUP *btn_group;
    CAP_GROUP *cap_group;
    u_char     language;
    int        i;

    if (0xe < group_label)
    {
        PRINT_ASSERT("Error! %s group label[%d]");
    }

    btn_group = cap_btn_group_ctrl[group_label].btn_group;
    if (-1 < btn_group->btn_label)
    {
        i = 0;
        do
        {
            language = GetLanguage();
            DrawCmnButton(btn_group[i].btn_label,
                          (float)(x + btn_group[i].x[(char)language]),
                          (float)y, alp, pri);
            i = i + 1;
        } while (-1 < btn_group[i].btn_label);
    }

    cap_group = cap_btn_group_ctrl[group_label].cap_group;
    if (-1 < cap_group->cap_label)
    {
        i = 0;
        do
        {
            language = GetLanguage();
            DrawCmnCaption(cap_group[i].cap_label,
                           (float)(x + cap_group[i].x[(char)language]),
                           (float)y, alp, pri);
            i = i + 1;
        } while (-1 < cap_group[i].cap_label);
    }
}

// ──────────────────────────────────────────────────────────────────────
// Draw a caption group anchored at the per-world caption position.

void DrawCmnCapGroup_W(int group_label, int world_label, u_char alp, u_int pri)
{
    u_char language_x;
    u_char language_y;

    if (0xe < group_label)
    {
        PRINT_ASSERT("Error! %s group label[%d]");
    }

    if (0xe < world_label)
    {
        PRINT_ASSERT("Error! %s world label[%d]");
    }

    language_x = GetLanguage();
    language_y = GetLanguage();
    DrawCmnCapGroup(group_label,
                    cap_world_pos_x[world_label][(char)language_x],
                    cap_world_pos_y[world_label][(char)language_y],
                    alp, pri);
}
