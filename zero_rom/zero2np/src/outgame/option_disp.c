// FILE: /home/zero_rom/zero2np/src/outgame/option_disp.c
//
// Everything the option screen draws, plus the fade between its pages.
//
// Three conventions worth knowing:
//   * every text line goes through OptPrintMsgShdw(), which prints the same
//     message three times -- twice offset by a pixel in colour 0x1a and once
//     on top in the real colour.  That is the drop shadow;
//   * the selected line is colour 0x1f (or 0x1d on the main page's first
//     group) and everything else 0x19, so "highlighted" is a colour swap, not
//     a separate plate;
//   * several rules are one narrow plate stretched by scw, which is why the
//     button page carries a 35-entry scale table.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
// Trailing /* NNN */ are ROM source line numbers, measured from
// disassemble_function.

#include "option.h"

#include "tim_dat/option_dat.h"             // option_tex[] / option_sqr[]
#include "../common/variable.h"             // optm is in option.h
#include "../graphics/graph2d/draw_cmn.h"   // DrawCmnWindow / YesNoSel / CapGroup_W
#include "../graphics/graph2d/g2d_draw.h"   // DISP_SPRT / DISP_SQAR / Copy* / Disp*
#include "../graphics/graph2d/message.h"    // PrintMsg / PrintMsg_Arrange

#include <string.h>                         // memset

#define OPT_ANM_SPD     12

/* Text colours: highlighted, plain, and the drop shadow. */
#define OPT_COL_SEL     0x1f
#define OPT_COL_SEL_2   0x1d
#define OPT_COL_PLAIN   0x19
#define OPT_COL_SHADOW  0x1a

static void OptDispButtonSetupText(int alp);
static void OptDispCursor(int place, int csr, int alp);
static void OptDispAHeadText(int place, int csr, int alp);
static void OptDispBracket(int place, int csr, int alp);
static void OptDispIcon(int place, int csr, int alpha);
static void OptDispTitle(int place, int mx, int my, int alp);
static void OptDispCaption(int place, int csr, int alp);
static void OptDispWinMsg(int place, int csr, int alp);
static void OptPrintMsgShdw(int msg_id, int x, int y, int col, int alp,
                            int pri, int mode);
static void OptDispCautionMsg(int msg_id, int alp);
static void OptBrightnessAdjustmentFilterDraw(int alp);

/* Identical in shape to GalAnimation(), including the "place 4 is the exit"
 * convention -- but this one also carries the cursor across, because the
 * option pages remember where you came from. */
int OptAnimation(void)
{
    int ret = 0;

    if (oc->anm_step == 1)                                              /* 87 */
    {
        if (oc->anm_alpha < 0x80)                                       /* 88 */
        {
            oc->anm_alpha += OPT_ANM_SPD;                               /* 89 */
            if (oc->anm_alpha >= 0x80)                                  /* 90 */
            {
                oc->anm_alpha = 0x80;                                   /* 94 */
                oc->anm_step = 0;
            }
        }
    }
    else if (oc->anm_step == 2)                                         /* 96 */
    {
        if (oc->anm_alpha > 0)                                          /* 97 */
        {
            oc->anm_alpha -= OPT_ANM_SPD;                               /* 98 */
            if (oc->anm_alpha <= 0)                                     /* 100 */
            {
                oc->anm_alpha = 0;                                      /* 101 */
                oc->anm_step = 1;                                       /* 102 */

                if (oc->next_place == OPT_PLACE_END)                    /* 104 */
                {
                    ret = 1;                                            /* 105 */
                }
                else
                {
                    oc->now_place = oc->next_place;                     /* 109 */
                    oc->cursor = oc->next_csr;                          /* 111 */
                }
            }
        }
    }
    else if (oc->anm_step == 0)
    {
        oc->anm_alpha = 0x80;
    }

    return ret;                                                         /* 115 */
}

/* The main list.  Sprites 0xc/0x12/0x18 and 0xf/0x15/0x1a are the two rule
 * lengths, 0x1d the long one under the last row. */
void OptionMainDisp(void)
{
    DISP_SPRT spr;
    DISP_SQAR sqr;
    int i;
    int alpha;

    alpha = oc->anm_alpha;                                              /* 129 */

    OptDispTitle(oc->now_place, 0, 0, alpha);                           /* 131 */
    OptPK2SendVram(1, opt_top_tex_addr);                                /* 133 */

    for (i = 0xb; i < 0x1f; i++)                                        /* 134 */
    {
        CopySprDToSpr(&spr, option_tex + i);                            /* 135 */

        if ((i == 0xc) || (i == 0x12) || (i == 0x18))                   /* 136 */
        {
            spr.scw = 22.599998f;                                       /* 139 */
            spr.sch = 1.0f;                                             /* 140 */
        }
        else if ((i == 0xf) || (i == 0x15) || (i == 0x1a))              /* 143 */
        {
            spr.scw = 22.699999f;                                       /* 144 */
            spr.sch = 1.0f;                                             /* 145 */
        }
        else if (i == 0x1d)                                             /* 148 */
        {
            spr.scw = 46.299999f;                                       /* 149 */
            spr.sch = 1.0f;
        }

        spr.alpha = (u_char)alpha;                                      /* 151 */
        DispSprD(&spr);                                                 /* 153 */
    }                                                                   /* 154 */

    OptDispBracket(oc->now_place, oc->cursor, alpha);                   /* 155 */

    for (i = 0; i < 11; i++)                                            /* 156 */
    {
        CopySqrDToSqr(&sqr, option_sqr + i);                            /* 158 */
        DispSqrD(&sqr);                                                 /* 160 */
    }                                                                   /* 161 */

    OptDispIcon(oc->now_place, oc->cursor, alpha);                      /* 163 */

    OptPK2SendVram(1, opt_top_tex_addr);                                /* 164 */

    CopySprDToSpr(&spr, option_tex + 0x25);                             /* 165 */
    spr.alphar = 0x48;                                                  /* 166 */
    spr.alpha = (u_char)alpha;                                          /* 167 */
    DispSprD(&spr);                                                     /* 168 */

    /* The volume knob.  0x100 of range across 174 pixels. */
    CopySprDToSpr(&spr, option_tex + 0x26);                             /* 169 */
    spr.x += (float)(int)((float)optm.snd_volume * 0.5f * 1.359375f);   /* 171 */
    spr.alpha = (u_char)alpha;                                          /* 173 */
    DispSprD(&spr);                                                     /* 174 */

    OptDispCursor(oc->now_place, oc->cursor, alpha);                    /* 176 */
    OptPK2SendVram(1, opt_top_tex_addr);
    OptDispCaption(oc->now_place, oc->cursor, alpha);
    OptDispAHeadText(oc->now_place, oc->cursor, alpha);
    OptDispWinMsg(oc->now_place, oc->cursor, alpha);                    /* 178 */
}

/* Same shape as the main page, one sprite block further along. */
void OptionOperateDisp(void)
{
    DISP_SPRT spr;
    DISP_SQAR sqr;
    int i;
    int alpha;

    alpha = oc->anm_alpha;                                              /* 194 */

    OptDispTitle(oc->now_place, 0, 0, alpha);                           /* 196 */
    OptPK2SendVram(1, opt_top_tex_addr);                                /* 198 */

    for (i = 0x53; i < 0x68; i++)                                       /* 199 */
    {
        CopySprDToSpr(&spr, option_tex + i);                            /* 200 */

        if ((i == 0x54) || (i == 0x5a) || (i == 0x60))                  /* 201 */
        {
            spr.scw = 22.599998f;                                       /* 204 */
            spr.sch = 1.0f;                                             /* 205 */
        }
        else if ((i == 0x57) || (i == 0x5d) || (i == 0x63))             /* 208 */
        {
            spr.scw = 22.699999f;                                       /* 209 */
            spr.sch = 1.0f;                                             /* 210 */
        }
        else if (i == 0x66)                                             /* 213 */
        {
            spr.scw = 46.299999f;                                       /* 214 */
            spr.sch = 1.0f;
        }

        spr.alpha = (u_char)alpha;                                      /* 216 */
        DispSprD(&spr);                                                 /* 217 */
    }                                                                   /* 218 */

    for (i = 0; i < 10; i++)                                            /* 219 */
    {
        CopySqrDToSqr(&sqr, option_sqr + 0xb + i);                      /* 220 */
        DispSqrD(&sqr);                                                 /* 222 */
    }

    OptDispBracket(oc->now_place, oc->cursor, alpha);                   /* 224 */
    OptDispIcon(oc->now_place, oc->cursor, alpha);                      /* 226 */
    OptDispCursor(oc->now_place, oc->cursor, alpha);                    /* 228 */
    OptDispCaption(oc->now_place, oc->cursor, alpha);
    OptDispAHeadText(oc->now_place, oc->cursor, alpha);
    OptDispWinMsg(oc->now_place, oc->cursor, alpha);                    /* 230 */
}

/* The one page drawn without OptDispBgMask(), so the sample image behind the
 * filter is the real screen. */
void OptionBrightnessDisp(void)
{
    DISP_SPRT spr;
    int i;
    int alpha;

    alpha = oc->anm_alpha;                                              /* 245 */

    OptPK2SendVram(2, opt_brn_tex_addr);                                /* 248 */

    CopySprDToSpr(&spr, option_tex + 0xd7);                             /* 249 */
    spr.alpha = (u_char)alpha;                                          /* 251 */
    DispSprD(&spr);                                                     /* 253 */

    OptBrightnessAdjustmentFilterDraw(alpha);                           /* 255 */

    OptDispTitle(oc->now_place, 0, 0, alpha);                           /* 257 */
    OptPK2SendVram(2, opt_brn_tex_addr);                                /* 258 */

    for (i = 0xd8; i < 0xe0; i++)                                       /* 259 */
    {
        CopySprDToSpr(&spr, option_tex + i);                            /* 260 */

        if ((i == 0xde) || (i == 0xdf))                                 /* 262 */
        {
            spr.csx = spr.x;
            spr.csy = spr.y;
            spr.scw = 1.0f;
            spr.sch = 4.0f;                                             /* 265 */
        }

        spr.alpha = (u_char)alpha;                                      /* 266 */
        DispSprD(&spr);                                                 /* 268 */
    }

    for (i = 0xe0; i < 0xe6; i++)                                       /* 269 */
    {
        CopySprDToSpr(&spr, option_tex + i);                            /* 270 */

        if ((i == 0xe0) || (i == 0xe1))                                 /* 271 */
        {
            spr.rot = 270.0f;                                           /* 272 */
            spr.crx = spr.x;
            spr.cry = spr.y;
        }
        else if ((i == 0xe4) || (i == 0xe5))                            /* 276 */
        {
            spr.alphar = 0x48;                                          /* 277 */
        }

        spr.alpha = (u_char)alpha;                                      /* 278 */
        DispSprD(&spr);                                                 /* 280 */
    }

    /* 0x10..0x90 of range across 380 pixels. */
    CopySprDToSpr(&spr, option_tex + 0xe6);                             /* 281 */
    spr.x += (float)(int)((float)(optm.brightness - 0x10) * 2.96875f);  /* 282 */
    spr.alpha = (u_char)alpha;                                          /* 283 */
    DispSprD(&spr);                                                     /* 285 */

    OptDispCaption(oc->now_place, oc->cursor, alpha);
    OptDispWinMsg(oc->now_place, oc->cursor, alpha);                    /* 287 */
}

/* The pad diagram.  Every plate carries its own scale pair, most of them
 * (1,1) -- only the leader lines are stretched. */
void OptionButtonSetupDisp(void)
{
    static const float scl[35][2] =                                     /* rdata 3c2448 */
    {
        { 1.0f, 1.0f },       { 2.2999999f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 1.9599999f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 3.0f, 1.0f },       { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 1.0f, 1.0f },       { 4.0f, 1.0f },
        { 1.0f, 1.0f },       { 1.0f, 1.0f },       { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 3.2599999f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 2.9199998f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 2.8999998f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 2.6399998f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 3.1399998f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.0f },       { 3.3399999f, 1.0f }, { 1.0f, 1.0f },
        { 1.0f, 1.4375f },    { 1.0f, 1.0f },
    };
    DISP_SPRT spr;
    int i;
    int alpha;

    alpha = oc->anm_alpha;                                              /* 312 */

    OptDispTitle(oc->now_place, 0, 0, alpha);                           /* 314 */
    OptPK2SendVram(3, opt_key_tex_addr);                                /* 316 */

    for (i = 0x9e; i < 0xb0; i++)                                       /* 317 */
    {
        CopySprDToSpr(&spr, option_tex + i);                            /* 318 */

        if (i == 0x9f)                                                  /* 319 */
        {
            spr.rot = 270.0f;                                           /* 320 */
            spr.crx = spr.x;
            spr.cry = spr.y;
        }
        else if ((i == 0xac) || (i == 0xad))                            /* 321 */
        {
            spr.alphar = 0x48;                                          /* 326 */
        }

        spr.alpha = (u_char)alpha;                                      /* 327 */
        DispSprD(&spr);                                                 /* 329 */
    }

    /* The three layout names live back to back at 0xb0. */
    CopySprDToSpr(&spr, option_tex + 0xb0 + optm.pad_type);             /* 331 */
    spr.alpha = (u_char)alpha;                                          /* 333 */
    DispSprD(&spr);                                                     /* 334 */

    for (i = 0; i < 35; i++)                                            /* 335 */
    {
        CopySprDToSpr(&spr, option_tex + 0xb3 + i);                     /* 336 */
        spr.csx = spr.x;
        spr.csy = spr.y;
        spr.scw = scl[i][0];                                            /* 338 */
        spr.sch = scl[i][1];                                            /* 339 */
        spr.alpha = (u_char)alpha;                                      /* 341 */
        DispSprD(&spr);
    }

    OptDispButtonSetupText(alpha);                                      /* 343 */
    OptDispCaption(oc->now_place, oc->cursor, alpha);
}

/* Fifteen labels on the pad diagram, whose message ids depend on which of the
 * three layouts is selected.  btn_txt[layout][slot] is {colour group, msg}. */
static void OptDispButtonSetupText(int alp)
{
    static const int btn_txt[3][15][2] =                                /* rdata 3c2560 */
    {
        { {2,50},{2,24},{0,31},{0,32},{2,51},{0,56},{1,26},{0,36},
          {2,29},{0,33},{1,27},{0,35},{1,28},{0,34},{1,26} },
        { {2,50},{2,24},{0,31},{0,32},{2,51},{0,33},{1,27},{0,36},
          {2,29},{0,56},{1,26},{0,35},{1,28},{0,34},{1,26} },
        { {2,50},{1,28},{0,31},{0,32},{2,51},{0,56},{1,26},{0,36},
          {2,29},{0,33},{1,27},{0,35},{2,24},{0,34},{1,26} },
    };
    static const int txt_pos[15][2] =                                   /* rdata 3c26c8 */
    {
        {  66,  70 }, {  66, 106 }, {  66, 288 }, {  66, 335 },
        { 576,  68 }, { 576, 104 }, { 576, 128 }, { 586, 163 },
        { 586, 187 }, { 586, 223 }, { 586, 247 }, { 586, 343 },
        { 586, 367 }, { 586, 283 }, { 586, 307 },
    };
    int i;
    int col;

    for (i = 0; i < 15; i++)
    {
        col = btn_txt[optm.pad_type][i][0];
        OptPrintMsgShdw(btn_txt[optm.pad_type][i][1],
                        txt_pos[i][0], txt_pos[i][1],
                        col, alp, 0, 1);
    }
}

void OptionInitialyzeDisp(void)
{
    OptDispCautionMsg(0x31, 0x80);                                      /* 518 */
    DrawCmnYesNoSel(oc->yn_csr, 220.0f, 0x80, 0);                       /* 519 */
}

/* The selected row's bracket pair.  Rows that hold a value get the four-plate
 * cursor (0x33/0x37/0x3b/0x3f) and rows that open a sub-page get none; the
 * highlight rule underneath is 0x43.. for a plain row and 0x46.. for a value
 * row, stretched to the row width and moved to the row's y. */
static void OptDispCursor(int place, int csr, int alp)
{
    SPRT_DAT *tri_sp_dat;
    DISP_SPRT spr;
    int i;
    int csr_y;
    int csr_type;

    if (place == OPT_PLACE_MAIN)
    {
        OptPK2SendVram(1, opt_top_tex_addr);

        tri_sp_dat = (SPRT_DAT *)0;
        csr_type = 0;
        csr_y = 0x42;

        switch (csr)
        {
        case 0:  csr_type = 0; csr_y = 0x42;  break;
        case 1:  csr_type = 1; tri_sp_dat = option_tex + 0x33; csr_y = 99;    break;
        case 2:  csr_type = 0; csr_y = 0x8d;  break;
        case 3:  csr_type = 1; tri_sp_dat = option_tex + 0x37; csr_y = 0xae;  break;
        case 4:  csr_type = 1; tri_sp_dat = option_tex + 0x3b; csr_y = 0xd8;  break;
        case 5:  csr_type = 1; tri_sp_dat = option_tex + 0x3f; csr_y = 0xf9;  break;
        case 6:  csr_type = 0; csr_y = 0x124; break;
        }

        if (tri_sp_dat != (SPRT_DAT *)0)
        {
            for (i = 0; i < 4; i++)
            {
                CopySprDToSpr(&spr, tri_sp_dat + i);
                if (i > 1)
                {
                    spr.alphar = 0x48;
                }
                spr.alpha = (u_char)alp;
                DispSprD(&spr);
            }
        }

        if (csr_type != 0)
        {
            for (i = 0x46; i < 0x4c; i++)
            {
                CopySprDToSpr(&spr, option_tex + i);
                if ((i == 0x47) || (i == 0x4a))
                {
                    spr.csx = spr.x;
                    spr.csy = (float)csr_y;
                    spr.scw = 13.069999f;
                    spr.sch = 1.0f;
                }
                spr.alphar = 0x48;
                spr.y = (float)csr_y;
                spr.alpha = (u_char)alp;
                DispSprD(&spr);
            }
        }
        else
        {
            for (i = 0x43; i < 0x46; i++)
            {
                CopySprDToSpr(&spr, option_tex + i);
                if (i == 0x44)
                {
                    spr.csx = spr.x;
                    spr.csy = (float)csr_y;
                    spr.scw = 28.669998f;
                    spr.sch = 1.0f;
                }
                spr.alphar = 0x48;
                spr.y = (float)csr_y;
                spr.alpha = (u_char)alp;
                DispSprD(&spr);
            }
        }
    }
    else if (place == OPT_PLACE_OPERATE)
    {
        OptPK2SendVram(1, opt_top_tex_addr);

        csr_type = 0;
        csr_y = 0x42;

        if (csr == 0)      { csr_y = 0x42;  }
        else if (csr == 1) { csr_y = 0x8d;  }
        else if (csr == 2) { csr_y = 0xd8;  }
        else if (csr == 3) { csr_type = 1; csr_y = 0x124; }

        if (csr_type != 0)
        {
            for (i = 0x94; i < 0x97; i++)
            {
                CopySprDToSpr(&spr, option_tex + i);
                if (i == 0x95)
                {
                    spr.csx = spr.x;
                    spr.csy = (float)csr_y;
                    spr.scw = 28.669998f;
                    spr.sch = 1.0f;
                }
                spr.alphar = 0x48;
                spr.y = (float)csr_y;
                spr.alpha = (u_char)alp;
                DispSprD(&spr);
            }
        }
        else
        {
            for (i = 0x97; i < 0x9a; i++)
            {
                CopySprDToSpr(&spr, option_tex + i);
                if (i == 0x98)
                {
                    spr.csx = spr.x;
                    spr.csy = (float)csr_y;
                    spr.scw = 28.669998f;
                    spr.sch = 1.0f;
                }
                spr.alphar = 0x48;
                spr.y = (float)csr_y;
                spr.alpha = (u_char)alp;
                DispSprD(&spr);
            }
        }
    }
}

/* Every label on the two list pages.  text_dat rows 0..6 are the main page's
 * headings, 7..12 its ON/OFF values (two rows each, picked by the setting),
 * 13..16 the operate page's headings, 17..22 its values and 23..25 the three
 * pad-layout names. */
static void OptDispAHeadText(int place, int csr, int alp)
{
    static const int text_dat[26][3] =                                  /* rdata 3c2760 */
    {
        {  0, 371,  68 }, {  1, 254, 101 }, {  2, 371, 142 },
        {  3, 254, 176 }, {  4, 254, 217 }, {  7, 254, 251 },
        {  8, 371, 294 }, { 10, 488, 101 }, {  9, 498, 101 },
        { 10, 488, 176 }, {  9, 498, 176 }, {  5, 488, 218 },
        {  6, 488, 218 }, { 11, 371,  68 }, { 14, 371, 143 },
        { 17, 371, 217 }, { 20, 331, 294 }, { 12, 254, 102 },
        { 13, 488, 102 }, { 15, 254, 176 }, { 16, 488, 176 },
        { 18, 255, 252 }, { 19, 489, 252 }, { 21, 371, 294 },
        { 22, 371, 294 }, { 23, 371, 294 },
    };
    int i;
    int col;
    const int *txp;

    if (place == OPT_PLACE_MAIN)
    {
        OptPK2SendVram(1, opt_top_tex_addr);

        for (i = 0; i < 7; i++)
        {
            col = (csr == i) ? OPT_COL_SEL_2 : OPT_COL_PLAIN;
            OptPrintMsgShdw(text_dat[i][0], text_dat[i][1], text_dat[i][2],
                            col, alp, 0, 2);
        }

        col = (csr == 1) ? OPT_COL_SEL : OPT_COL_PLAIN;
        txp = text_dat[optm.pad_vib + 7];
        OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);

        col = (csr == 3) ? OPT_COL_SEL : OPT_COL_PLAIN;
        txp = text_dat[optm.credits + 9];
        OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);

        col = (csr == 4) ? OPT_COL_SEL : OPT_COL_PLAIN;
        txp = text_dat[optm.snd_output + 0xb];
        OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);
    }
    else if (place == OPT_PLACE_OPERATE)
    {
        OptPK2SendVram(1, opt_top_tex_addr);

        for (i = 0; i < 4; i++)
        {
            col = (csr == i) ? OPT_COL_SEL : OPT_COL_PLAIN;
            txp = text_dat[i + 13];
            OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);
        }

        /* The three ON/OFF pairs: both halves are drawn, and only the one
         * matching the current setting is highlighted. */
        for (i = 0; i < 2; i++)
        {
            col = ((csr == 0) && (i == optm.move_operate))
                      ? OPT_COL_SEL : OPT_COL_PLAIN;
            txp = text_dat[i + 17];
            OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);
        }
        for (i = 0; i < 2; i++)
        {
            col = ((csr == 1) && (i == optm.view_vertical))
                      ? OPT_COL_SEL : OPT_COL_PLAIN;
            txp = text_dat[i + 19];
            OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);
        }
        for (i = 0; i < 2; i++)
        {
            col = ((csr == 2) && (i == optm.ana_replace))
                      ? OPT_COL_SEL : OPT_COL_PLAIN;
            txp = text_dat[i + 21];
            OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);
        }

        col = (csr == 3) ? OPT_COL_SEL : OPT_COL_PLAIN;
        txp = text_dat[optm.pad_type + 0x17];
        OptPrintMsgShdw(txp[0], txp[1], txp[2], col, alp, 0, 2);
    }
}

/* The [ ] pair around each ON/OFF value.  On the operate page which of the
 * two positions gets the bracket is the setting itself, which is why
 * bracket_lr[] is read straight out of optm. */
static void OptDispBracket(int place, int csr, int alp)
{
    SPRT_DAT *bracket_sp[3][2] =
    {
        { option_tex + 112, option_tex + 114 },
        { option_tex + 116, option_tex + 118 },
        { option_tex + 120, option_tex + 122 },
    };
    int bracket_lr[3];
    SPRT_DAT *spd;
    DISP_SPRT spr;
    int i;
    int j;

    bracket_lr[0] = (int)optm.move_operate;                             /* 841 */
    bracket_lr[1] = (int)optm.view_vertical;
    bracket_lr[2] = (int)optm.ana_replace;

    OptPK2SendVram(1, opt_top_tex_addr);                                /* 845 */

    if (place == OPT_PLACE_MAIN)                                        /* 846 */
    {
        for (i = 0x20; i < 0x25; i++)                                   /* 848 */
        {
            CopySprDToSpr(&spr, option_tex + i);                        /* 849 */
            spr.alpha = (u_char)alp;                                    /* 851 */
            DispSprD(&spr);                                             /* 852 */
        }
    }
    else if (place == OPT_PLACE_OPERATE)                                /* 853 */
    {
        for (i = 0; i < 3; i++)                                         /* 855 */
        {
            spd = bracket_sp[i][bracket_lr[i]];                         /* 856 */
            for (j = 0; j < 2; j++)                                     /* 857 */
            {
                CopySprDToSpr(&spr, spd + j);                           /* 859 */
                spr.alpha = (u_char)alp;                                /* 860 */
                DispSprD(&spr);                                         /* 861 */
            }
        }

        for (i = 0; i < 3; i++)                                         /* 863 */
        {
            if (csr == i)                                               /* 864 */
            {
                spd = option_tex + 0x7c + i * 8;                        /* 865 */
                if (bracket_lr[i] != 0)                                 /* 866 */
                {
                    spd = option_tex + 0x80 + i * 8;                    /* 867 */
                }

                for (j = 0; j < 4; j++)                                 /* 868 */
                {
                    CopySprDToSpr(&spr, spd + j);                       /* 869 */
                    if (j > 1)                                          /* 870 */
                    {
                        spr.alphar = 0x48;                              /* 871 */
                    }
                    spr.alpha = (u_char)alp;                            /* 872 */
                    DispSprD(&spr);                                     /* 873 */
                }
            }
        }
    }
}

/* The four glyphs down the left of each list.  The unselected ones are drawn
 * at half alpha, and the selected group also gets the three-plate frame from
 * 0x2d moved down 75 pixels per row. */
static void OptDispIcon(int place, int csr, int alpha)
{
    SPRT_DAT *icon[8][2] =
    {
        { option_tex + 39,  (SPRT_DAT *)0  },
        { option_tex + 40,  option_tex + 41 },
        { option_tex + 42,  (SPRT_DAT *)0  },
        { option_tex + 43,  option_tex + 44 },
        { option_tex + 104, (SPRT_DAT *)0  },
        { option_tex + 105, (SPRT_DAT *)0  },
        { option_tex + 106, option_tex + 107 },
        { option_tex + 108, option_tex + 109 },
    };
    DISP_SPRT spr;
    int alp;
    int i;
    int j;
    int icon_ofs = 0;
    int icon3_id = 0x31;

    OptPK2SendVram(1, opt_top_tex_addr);                                /* 897 */

    if (place == OPT_PLACE_MAIN)                                        /* 899 */
    {
        /* Six list rows collapse onto four icon groups. */
        switch (csr)                                                    /* 900 */
        {
        case 0:
        case 1:  csr = 0; break;
        case 2:
        case 3:  csr = 1; break;
        case 4:
        case 5:  csr = 2; break;
        case 6:  csr = 3; break;
        }

        icon3_id = 0x31;                                                /* 901 */
        icon_ofs = 0;
    }
    else if (place == OPT_PLACE_OPERATE)                                /* 904 */
    {
        icon3_id = 0x6e;                                                /* 907 */
        icon_ofs = 4;
    }

    for (i = 0; i < 4; i++)                                             /* 910 */
    {
        if (i == csr)                                                   /* 912 */
        {
            if (i == 3)                                                 /* 915 */
            {
                CopySprDToSpr(&spr, option_tex + icon3_id);             /* 916 */
                spr.alpha = (u_char)alpha;                              /* 917 */
                DispSprD(&spr);                                         /* 918 */

                CopySprDToSpr(&spr, option_tex + icon3_id + 1);         /* 919 */
                spr.alpha = (u_char)alpha;                              /* 922 */
                DispSprD(&spr);                                         /* 924 */
            }
            else
            {
                for (j = 0; j < 4; j++)                                 /* 925 */
                {
                    CopySprDToSpr(&spr, option_tex + 0x2d + j);         /* 926 */
                    spr.y += (float)(csr * 75);                         /* 928 */
                    spr.alpha = (u_char)alpha;                          /* 929 */
                    DispSprD(&spr);                                     /* 931 */
                }
            }
        }

        for (j = 0; j < 2; j++)                                         /* 933 */
        {
            if (icon[i + icon_ofs][j] != (SPRT_DAT *)0)                 /* 934 */
            {
                CopySprDToSpr(&spr, icon[i + icon_ofs][j]);             /* 935 */

                alp = alpha;                                            /* 936 */
                if (i != csr)                                           /* 937 */
                {
                    alp = alpha / 2;                                    /* 938 */
                }

                spr.alpha = (u_char)alp;                                /* 941 */
                spr.alphar = 0x48;                                      /* 942 */
                DispSprD(&spr);                                         /* 943 */
            }
        }                                                               /* 946 */
    }                                                                   /* 950 */
}

/* Two shared plates stretched to 1.1, then the page's own heading. */
static void OptDispTitle(int place, int mx, int my, int alp)
{
    DISP_SPRT spr;
    int i;
    int ttl_no;

    (void)mx;
    (void)my;

    OptPK2SendVram(0, opt_og_tex_addr);                                 /* 962 */

    for (i = 8; i < 10; i++)                                            /* 963 */
    {
        CopySprDToSpr(&spr, option_tex + i);                            /* 964 */
        spr.csx = spr.x;                                                /* 967 */
        spr.csy = spr.y;
        spr.scw = 1.0999999f;                                           /* 969 */
        spr.sch = 1.0f;                                                 /* 970 */
        spr.alpha = (u_char)alp;                                        /* 971 */
        DispSprD(&spr);                                                 /* 972 */
    }                                                                   /* 973 */

    OptPK2SendVram(1, opt_top_tex_addr);                                /* 974 */

    if (place == OPT_PLACE_OPERATE)      { ttl_no = 0x52; }             /* 975 */
    else if (place == OPT_PLACE_MAIN)    { ttl_no = 10; }
    else if (place == OPT_PLACE_BUTTON)  { ttl_no = 0x9d; }
    else if (place == OPT_PLACE_BRIGHT)  { ttl_no = 0xd6; }
    else                                 { ttl_no = 10; }               /* 976 */

    CopySprDToSpr(&spr, option_tex + ttl_no);                           /* 977 */
    spr.alpha = (u_char)alp;                                            /* 979 */
    DispSprD(&spr);                                                     /* 981 */
}

/* The button-prompt strip.  The main page uses its own plates (0x4c..0x51 for
 * rows that open something, 0x4c..0x4f otherwise); the sub-pages use the
 * shared draw_cmn group. */
static void OptDispCaption(int place, int csr, int alp)
{
    DISP_SPRT cap_ds;
    int i;

    if (place == OPT_PLACE_MAIN)
    {
        if ((csr == 0) || (csr == 2) || (csr == 6))
        {
            for (i = 0x4c; i < 0x52; i++)
            {
                CopySprDToSpr(&cap_ds, option_tex + i);
                cap_ds.alpha = (u_char)(((int)cap_ds.alpha * alp) >> 7);
                DispSprD(&cap_ds);
            }
        }
        else
        {
            for (i = 0x4c; i < 0x50; i++)
            {
                CopySprDToSpr(&cap_ds, option_tex + i);
                cap_ds.alpha = (u_char)(((int)cap_ds.alpha * alp) >> 7);
                DispSprD(&cap_ds);
            }
        }
    }
    else if (place == OPT_PLACE_OPERATE)
    {
        if (csr == 3)
        {
            DrawCmnCapGroup_W(0, 0, (u_char)alp, 0);
        }
        else
        {
            DrawCmnCapGroup_W(0xc, 0xc, (u_char)alp, 0);
        }
    }
    else
    {
        DrawCmnCapGroup_W(0xc, 0xc, (u_char)alp, 0);
    }
}

/* The help line.  The operate page's analog-replace row has two different
 * messages depending on the setting, which is the one special case. */
static void OptDispWinMsg(int place, int csr, int alp)
{
    int msg_id[14] =
    {
        0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b,
        0x31, 0x2c, 0x2d, 0x36, 0x2f, 0x37, 0x30,
    };
    int msg_no;

    DrawCmnWindow(0, 15.0f, 324.0f, 610.0f, 118.0f, 0x58, (u_char)alp); /* 1098 */

    if (place == OPT_PLACE_MAIN)                                        /* 1099 */
    {
        msg_no = msg_id[csr];
    }
    else if (place == OPT_PLACE_OPERATE)                                /* 1101 */
    {
        if (csr == 2)                                                   /* 1106 */
        {
            if (optm.ana_replace == '\0')                               /* 1108 */
            {
                msg_no = 0x28;                                          /* 1109 */
            }
            else
            {
                msg_no = msg_id[0xc];
            }
        }
        else
        {
            msg_no = msg_id[csr + 8];
        }
    }
    else if (place == OPT_PLACE_BRIGHT)                                 /* 1113 */
    {
        msg_no = msg_id[0xd];
    }
    else
    {
        msg_no = msg_id[0];
    }

    PrintMsg(0x41, msg_no, 0x28, 0x15a, 1, alp, 0);                     /* 1118 */
}

/* The full-screen dim plus the frame; skipped entirely on the brightness page
 * so the filter there is judged against the real screen. */
void OptDispBgMask(void)
{
    DISP_SQAR DispSqar;
    SQAR_DAT SqarDat;
    DISP_SPRT spr;
    int i;

    memset(&SqarDat, 0, sizeof(SqarDat));                               /* 1129 */
    SqarDat.w = 640;
    SqarDat.h = 448;

    CopySqrDToSqr(&DispSqar, &SqarDat);                                 /* 1133 */
    DispSqar.alpha = 0x40;                                              /* 1135 */
    DispSqrD(&DispSqar);                                                /* 1136 */

    if (oc->now_place != OPT_PLACE_BRIGHT)                              /* 1138 */
    {
        OptPK2SendVram(3, opt_key_tex_addr);                            /* 1139 */

        for (i = 0; i < 8; i++)                                         /* 1140 */
        {
            CopySprDToSpr(&spr, option_tex + i);                        /* 1141 */
            DispSprD(&spr);                                             /* 1142 */
        }                                                               /* 1143 */
    }
}

/* Every line on the option screen is printed three times: twice in the shadow
 * colour offset by one and two pixels, then once on top. */
static void OptPrintMsgShdw(int msg_id, int x, int y, int col, int alp,
                            int pri, int mode)
{
    PrintMsg_Arrange(0x41, msg_id, x + 2, y + 2, OPT_COL_SHADOW,
                     alp, pri, 0, 0, mode);                             /* 1156 */
    PrintMsg_Arrange(0x41, msg_id, x + 1, y + 1, OPT_COL_SHADOW,
                     alp, pri, 0, 0, mode);                             /* 1158 */
    PrintMsg_Arrange(0x41, msg_id, x, y, col,
                     alp, pri, 0, 0, mode);                             /* 1160 */
}

static void OptDispCautionMsg(int msg_id, int alp)
{
    DISP_SQAR DispSqar;
    SQAR_DAT SqarDat;

    memset(&SqarDat, 0, sizeof(SqarDat));                               /* 1186 */
    SqarDat.w = 640;
    SqarDat.h = 448;

    CopySqrDToSqr(&DispSqar, &SqarDat);                                 /* 1188 */
    DispSqar.alpha = (u_char)((float)alp * 0.69999999f);                /* 1190 */
    DispSqrD(&DispSqar);                                                /* 1191 */

    DrawCmnWindow(0, 70.0f, 160.0f, 500.0f, 112.0f, 0x50, (u_char)alp); /* 1194 */
    PrintMsg(0x41, msg_id, 100, 0xb0, 1, alp, 0);                       /* 1198 */
}

/* The brightness preview.  Below 0x80 it darkens with a normal blend
 * (alphar 0x44) and above it lightens additively (0x49), so the one quad
 * covers both halves of the range.  GS TEST is set to pass everything. */
static void OptBrightnessAdjustmentFilterDraw(int alp)
{
    DISP_SQAR DispSqar;
    SQAR_DAT SqarDat = { 442, 234, 99, 60, 0, 0, 0, 0, 0 };             /* 1210 */

    CopySqrDToSqr(&DispSqar, &SqarDat);                                 /* 1212 */

    DispSqar.test = 0x30003;                                            /* 1213 */

    if (optm.brightness <= 0x80)                                        /* 1214 */
    {
        DispSqar.alpha = (u_char)(0x80 - optm.brightness);              /* 1215 */
        DispSqar.alphar = 0x44;                                         /* 1216 */
    }
    else
    {
        DispSqar.alpha = (u_char)((optm.brightness << 1) + 0xff);       /* 1220 */
        DispSqar.alphar = 0x49;                                         /* 1221 */
    }

    DispSqar.alpha = (u_char)((float)DispSqar.alpha * (float)alp * 0.0078125f); /* 1223 */
    DispSqrD(&DispSqar);                                                /* 1224 */
}
