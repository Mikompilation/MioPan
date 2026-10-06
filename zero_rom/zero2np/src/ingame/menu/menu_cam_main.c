// FILE: /home/zero_rom/zero2np/src/ingame/menu/menu_cam_main.c
//
// The camera menu's frame (menu_cam_main.o).  Twenty-five functions -- the
// nineteen exports and six statics -- four index tables in .rodata, three
// dispatch tables in .sdata and one six-byte work block in .sbss.
//
// It owns everything the two camera pages share and nothing either of them
// does: the two paks, the loader ladder, the dispatch between them, and the
// four widgets they both draw with.
//
// Facts worth knowing before touching it:
//
//  * There are two step ladders, not one.  MenuCamMain()'s `step` is the
//    outer one -- 0 first-init, 1 waiting on the paks, 2 running, 3 done --
//    and MenuCamModeMain()'s `sub_step` the inner one that runs whichever
//    page `mode` selects.  A page reporting non-zero puts sub_step at 3, and
//    only then is the parked request acted on: exit_flg ends the whole menu,
//    otherwise mode takes next_mode and sub_step drops back to 0 so the new
//    page gets its Init.  That is the entire page-change mechanism -- there
//    is no cross-fade here.
//
//  * The three dispatch tables are two entries wide and hold the *other* two
//    objects' entry points: { MenuCamTopInit, MenuCamEditInit } and so on.
//    menu_cam_top.o and menu_cam_edit.o are reached from nowhere else.
//
//  * The two paks are claimed out of a heap the *caller* supplies.
//    MenuCamMainBackGroundLoadReq() takes a get/free pair and parks it in
//    MenuCamMemGetFunc / MenuCamMemFreeFunc; menu.c hands over mem_util's and
//    outgame.c / title.c ol_load's.  That is what lets one screen serve both
//    the in-game menu and Mission Mode's setup, and it is why the assert
//    there fires on a *second* install rather than on a missing one.
//
//  * GetHaveReinforcedLensNum() starts its loop at 1, not 0.  Bit 0 of
//    mTemperedRenzFlg belongs to CAMERA_SUB_FUNC_NONE, so counting it would
//    report one lens too many for every save.  Its two siblings start at 0.
//    All three loops read BIT_FLAGS<N>::IsUp() with the loop bound equal to
//    N, which is why GCC could fold the range assert away entirely: the
//    back-edge test `i < N` *is* the "skip the assert" condition, and the two
//    share one register.
//
//  * MenuCamNumberDisp()'s `type` picks the digit face and its advance, and
//    the two agree with the art: type 0 is menu_camera_tex[90..99], 14 px
//    wide, and off_x is 14; types 1 and 2 are the 17 px faces at [112..121]
//    and [100..109], and off_x is 17.  A type outside 0..2 asserts and then
//    carries on with off_x still 0, stacking every digit on one spot.
//
//  * The four widget index tables carry no -1 in this build even though every
//    draw tests for one, so no widget is ever silently skipped.  lens_tbl[]
//    is a permutation of menu_camera_tex[75..84]: the CAMERA_SUB_FUNC_ENUM
//    order is not the order the icons sit on the sheet.
//
// Line numbers: the asserts pin them exactly.  PRINT_ASSERT passes its own
// __LINE__ to SetAssertPreMessage(), and the ROM's four sites carry 0xa5,
// 0x287, 0x2bb and 0x2ca -- 165, 647, 699 and 714, which are the lines the
// annotations below give them.  Every function's *opening* line is the `$LM`
// that symbols.txt interleaves immediately before its PROC/STATICPROC record,
// which is not the first line Ghidra reports -- MenuCamMainInit() opens at 139
// and its first statement is 142.  Header lines that show up in this object are
// fixed_array.h 124/125 and 129/130 (the two operator[] overloads; the
// reference_fixed_array subscripts report the second), fixed_array.h 231/232
// (reference_fixed_array's ctor, i.e. the local static's declaration line)
// and variable.h 852..858 (BIT_FLAGS::IsUp).
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), menu_cam_main.o.
// All 19 ZERO2.MAP exports plus the 6 statics, verified 19/19 against the map
// and 25/25 against functions.txt (its four remaining entries are the
// fixed_array boilerplate).  .text is accounted for byte-for-byte --
// 0x1ed610..0x1ee638 = 0x1028 = 4136 bytes: twenty-nine bodies totalling 4072
// (the twenty-five real ones and the four fixed_array boilerplate ones) plus
// sixteen 4-byte alignment fills, with no gap of 8 bytes or more anywhere.
// MENU_CAM_MAIN_CTRL (0x6) is the ROM's own types.txt record and is confirmed
// by an offsetof harness; all four .rodata index tables and the whole of
// menu_camera_tex[299] are diffed verbatim out of the compiled .obj.

#include "menu_cam_main.h"

#include "menu_cam_edit.h"                      /* MenuCamEditInit / Main / Disp */
#include "menu_cam_top.h"                       /* MenuCamTopFirstInit / ...  */
#include "tim_dat/menu_camera_dat.h"            /* menu_camera_tex[]          */

#include "../photo/m_plyr_camera.h"             /* m_plyr_camera              */
#include "../photo/n_equip_tray.h"              /* equip_func_tbl / GetSubFuncArray */
#include "../../common/utility2.h"              /* PRINT_ASSERT               */
#include "../../graphics/graph2d/g2d_draw.h"    /* DISP_SPRT / CopySprDToSpr  */
#include "../../graphics/graph2d/tim2.h"        /* PK2SendVram                */
#include "../../graphics/graph3d/ctl/fixed_array.h"  /* fixed_array / reference_fixed_array */
#include "../../system/eeiop/cddat.h"           /* GetFileSize / MENU_CAMERA_PK2 */
#include "../../system/eeiop/fileload.h"        /* FileLoadReqEE / FileLoadIsEnd2 */
#include "../../system/os/system.h"             /* GetLanguage                */

/* --------------------------------------------------------------------------
 *  File statics
 * ------------------------------------------------------------------------ */

/* The heap MenuCamMainBackGroundLoadReq() was handed.  Both are cleared by
 * MenuCamMainMemFree(), and a non-NULL pair is what the install asserts on. */
static void *(*MenuCamMemGetFunc)(int)   = nullptr;     /* sdata 3f2ba0 */
static void  (*MenuCamMemFreeFunc)(void *) = nullptr;   /* sdata 3f2ba4 */

static void *menu_cam_tex_addr      = nullptr;          /* sdata 3f2ba8 */
static void *menu_cam_edit_tex_addr = nullptr;          /* sdata 3f2bac */

static void (*menu_cam_init_func[MENU_CAM_MODE_NUM])(void) =    /* sdata 3f2bb0 */
{
    MenuCamTopInit,
    MenuCamEditInit,
};

static int (*menu_cam_main_func[MENU_CAM_MODE_NUM])(void) =     /* sdata 3f2bb8 */
{
    MenuCamTopMain,
    MenuCamEditMain,
};

static void (*menu_cam_disp_func[MENU_CAM_MODE_NUM])(void) =    /* sdata 3f2bc0 */
{
    MenuCamTopDisp,
    MenuCamEditDisp,
};

static MENU_CAM_MAIN_CTRL menu_cam_main_ctrl;           /* sbss 3f4e08 */

/* --------------------------------------------------------------------------
 *  Forward declarations of the file's statics
 * ------------------------------------------------------------------------ */
static void GetMenuCamMainTexMem(void **tex_addr, int data_label);
static int  MenuCamMainTexLoadWait(void);
static int  MenuCamModeMain(void);
static void LiberateMenuCamMainTexMem(void **tex_addr);
static void MenuCamMainTexLoadCancel(void *tex_addr, int data_label);
static void MenuCamNumberDisp_One(int data, int x, int y, u_char alpha,
                                  int pri, u_char type);

/* --------------------------------------------------------------------------
 *  Entry / texture
 * ------------------------------------------------------------------------ */

void MenuCamMainInit(char init_type)                                    /* 139 */
{
    menu_cam_main_ctrl.step      = 0;                                   /* 142 */
    menu_cam_main_ctrl.sub_step  = 0;                                   /* 143 */
    menu_cam_main_ctrl.mode      = MENU_CAM_MODE_TOP;                   /* 144 */
    menu_cam_main_ctrl.next_mode = MENU_CAM_MODE_TOP;                   /* 145 */
    menu_cam_main_ctrl.exit_flg  = 0;                                   /* 146 */
    menu_cam_main_ctrl.init_type = init_type;                           /* 147 */
}

/* Claim and load both paks.  The assert is on installing a second heap over
 * a live one, not on a missing one -- so a screen that forgot its MemFree()
 * is what trips it. */
void MenuCamMainBackGroundLoadReq(void *(*mem_get)(int),
                                  void (*mem_free)(void *))             /* 157 */
{
    if ((MenuCamMemGetFunc == nullptr) && (MenuCamMemFreeFunc == nullptr)) { /* 160 */
        MenuCamMemGetFunc  = mem_get;                                   /* 161 */
        MenuCamMemFreeFunc = mem_free;                                  /* 162 */
    }
    else {
        PRINT_ASSERT("Error! %s", __FUNCTION__);                        /* 165 */
    }

    if (menu_cam_tex_addr != nullptr) {                                 /* 168 */
        LiberateMenuCamMainTexMem(&menu_cam_tex_addr);                  /* 169 */
    }
    if (menu_cam_edit_tex_addr != nullptr) {                            /* 171 */
        LiberateMenuCamMainTexMem(&menu_cam_edit_tex_addr);             /* 172 */
    }

    GetMenuCamMainTexMem(&menu_cam_tex_addr,
                         MENU_CAMERA_PK2 + GetLanguage());              /* 177 */
    GetMenuCamMainTexMem(&menu_cam_edit_tex_addr,
                         MENU_CAMERA_EDT_PK2 + GetLanguage());          /* 178 */

    FileLoadReqEE(MENU_CAMERA_PK2 + GetLanguage(), menu_cam_tex_addr,
                  2, nullptr, nullptr);                                 /* 181 */
    FileLoadReqEE(MENU_CAMERA_EDT_PK2 + GetLanguage(), menu_cam_edit_tex_addr,
                  2, nullptr, nullptr);                                 /* 182 */
}

static void GetMenuCamMainTexMem(void **tex_addr, int data_label)       /* 192 */
{
    if (*tex_addr != nullptr) {                                         /* 195 */
        LiberateMenuCamMainTexMem(tex_addr);                            /* 196 */
    }

    *tex_addr = (*MenuCamMemGetFunc)((int)GetFileSize(data_label));     /* 200 */
}

static int MenuCamMainTexLoadWait(void)                                 /* 209 */
{
    if (FileLoadIsEnd2(MENU_CAMERA_PK2 + GetLanguage(),
                       menu_cam_tex_addr) != 0) {                       /* 217 */
        if (FileLoadIsEnd2(MENU_CAMERA_EDT_PK2 + GetLanguage(),
                           menu_cam_edit_tex_addr) != 0) {              /* 218 */
            return 1;
        }
    }

    return 0;                                                           /* 224 */
}

/* --------------------------------------------------------------------------
 *  The machine
 * ------------------------------------------------------------------------ */

int MenuCamMain(void)                                                   /* 236 */
{
    if (menu_cam_main_ctrl.step == 0) {                                 /* 242 */
        MenuCamTopFirstInit();                                          /* 244 */

        menu_cam_main_ctrl.step = 1;                                    /* 246 */
    }

    if (menu_cam_main_ctrl.step == 1) {                                 /* 250 */
        if (MenuCamMainTexLoadWait() != 0) {                            /* 252 */
            menu_cam_main_ctrl.step = 2;                                /* 253 */
        }
    }

    if (menu_cam_main_ctrl.step == 2) {                                 /* 257 */
        if (MenuCamModeMain() != 0) {                                   /* 258 */
            menu_cam_main_ctrl.step = 3;                                /* 259 */
        }
    }

    return (menu_cam_main_ctrl.step == 3);                              /* 263 */
}

/* Run the page `mode` selects, and apply whatever request it parked when it
 * reports done.  A NULL slot in either table is skipped, and skipping the
 * main one leaves sub_step at 2 for ever -- there is no timeout. */
static int MenuCamModeMain(void)                                        /* 276 */
{
    int res;

    res = 0;

    if (menu_cam_main_ctrl.sub_step == 0) {                             /* 282 */
        if (menu_cam_init_func[menu_cam_main_ctrl.mode] != nullptr) {   /* 283 */
            (*menu_cam_init_func[menu_cam_main_ctrl.mode])();           /* 284 */
        }

        menu_cam_main_ctrl.sub_step = 2;                                /* 287 */
    }

    if (menu_cam_main_ctrl.sub_step == 2) {                             /* 292 */
        if (menu_cam_main_func[menu_cam_main_ctrl.mode] != nullptr) {   /* 293 */
            if ((*menu_cam_main_func[menu_cam_main_ctrl.mode])() != 0) { /* 294 */
                menu_cam_main_ctrl.sub_step = 3;                        /* 295 */
            }
        }
    }

    if (menu_cam_main_ctrl.sub_step == 3) {                             /* 300 */
        if (menu_cam_main_ctrl.exit_flg != 0) {                         /* 302 */
            res = 1;                                                    /* 303 */
        }
        else {
            menu_cam_main_ctrl.mode     = menu_cam_main_ctrl.next_mode; /* 307 */
            menu_cam_main_ctrl.sub_step = 0;                            /* 308 */
        }
    }

    return res;                                                         /* 313 */
}

/* --------------------------------------------------------------------------
 *  Requests and accessors
 * ------------------------------------------------------------------------ */

void MenuCamExitReq(void)                                               /* 319 */
{
    menu_cam_main_ctrl.exit_flg = 1;                                    /* 322 */
}

void MenuCamGoToTopReq(void)                                            /* 330 */
{
    menu_cam_main_ctrl.next_mode = MENU_CAM_MODE_TOP;                   /* 333 */
}

void MenuCamGoToEditReq(void)                                           /* 341 */
{
    menu_cam_main_ctrl.next_mode = MENU_CAM_MODE_EDIT;                  /* 344 */
}

int GetMenuCamInitType(void)                                            /* 356 */
{
    return menu_cam_main_ctrl.init_type;                                /* 360 */
}

void *GetMenuCameraPk2Addr(void)                                        /* 367 */
{
    return menu_cam_tex_addr;                                           /* 371 */
}

void *GetMenuCameraEdtPk2Addr(void)                                     /* 378 */
{
    return menu_cam_edit_tex_addr;                                      /* 382 */
}

/* --------------------------------------------------------------------------
 *  What the player has
 *
 *  Three copies of one loop over a BIT_FLAGS, differing only in the flag set
 *  and the bound.  The lens one starts at 1 because bit 0 is
 *  CAMERA_SUB_FUNC_NONE.
 * ------------------------------------------------------------------------ */

int GetHaveAddFuncNum(void)                                             /* 393 */
{
    int i;
    int have_cnt;

    have_cnt = 0;                                                       /* 398 */

    for (i = 0; i < 4; i++) {                                           /* 401 */
        if (m_plyr_camera.camera_power_up.mAdditionFlg.IsUp(i) != 0) {
            have_cnt++;
        }
    }                                                                   /* 406 */

    return have_cnt;                                                    /* 409 */
}

int GetHaveEquipFuncNum(void)                                           /* 416 */
{
    int i;
    int have_cnt;

    have_cnt = 0;                                                       /* 421 */

    for (i = 0; i < 4; i++) {                                           /* 424 */
        if (m_plyr_camera.camera_power_up.mCamPartsFlg.IsUp(i) != 0) {
            have_cnt++;
        }
    }                                                                   /* 429 */

    return have_cnt;                                                    /* 432 */
}

int GetHaveReinforcedLensNum(void)                                      /* 439 */
{
    int i;
    int have_cnt;

    have_cnt = 0;                                                       /* 444 */

    for (i = 1; i < 10; i++) {                                          /* 447 */
        if (m_plyr_camera.camera_power_up.mTemperedRenzFlg.IsUp(i) != 0) {
            have_cnt++;
        }
    }                                                                   /* 452 */

    return have_cnt;                                                    /* 455 */
}

/* How many of the three tray slots are filled.  Unlike its three siblings
 * this one asks the tray, not the upgrade record -- an owned sub-function
 * that is not currently fitted does not count. */
int GetEquipReinforcedLensNum(void)                                     /* 462 */
{
    int                 i;
    int                 equip_num;
    fixed_array<char,3> equip_special;

    equip_num = 0;

    m_plyr_camera.eq_tray.GetSubFuncArray(&equip_special[0]);

    for (i = 0; i < 3; i++) {
        if (equip_special[i] != 0) {
            equip_num++;
        }
    }                                                                   /* 479 */

    return equip_num;                                                   /* 482 */
}

/* --------------------------------------------------------------------------
 *  Teardown
 * ------------------------------------------------------------------------ */

void MenuCamMainMemFree(void)                                           /* 492 */
{
    MenuCamMainTexLoadCancel(menu_cam_tex_addr,
                             MENU_CAMERA_PK2 + GetLanguage());          /* 495 */
    MenuCamMainTexLoadCancel(menu_cam_edit_tex_addr,
                             MENU_CAMERA_EDT_PK2 + GetLanguage());      /* 496 */

    LiberateMenuCamMainTexMem(&menu_cam_tex_addr);                      /* 500 */
    LiberateMenuCamMainTexMem(&menu_cam_edit_tex_addr);                 /* 501 */

    MenuCamMemGetFunc  = nullptr;                                       /* 504 */
    MenuCamMemFreeFunc = nullptr;                                       /* 505 */
}

static void LiberateMenuCamMainTexMem(void **tex_addr)                  /* 511 */
{
    if (*tex_addr != nullptr) {                                         /* 514 */
        (*MenuCamMemFreeFunc)(*tex_addr);                               /* 515 */
        *tex_addr = nullptr;                                            /* 516 */
    }
}

static void MenuCamMainTexLoadCancel(void *tex_addr, int data_label)    /* 527 */
{
    if (tex_addr != nullptr) {                                          /* 530 */
        if (FileLoadIsEnd2(data_label, tex_addr) == 0) {                /* 532 */
            FileLoadCancel2(data_label, tex_addr, nullptr, nullptr);    /* 533 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  Drawing
 * ------------------------------------------------------------------------ */

void MenuCamMainDisp(void)                                              /* 547 */
{
    if (menu_cam_main_ctrl.step == 2) {                                 /* 551 */
        if (menu_cam_disp_func[menu_cam_main_ctrl.mode] != nullptr) {   /* 552 */
            (*menu_cam_disp_func[menu_cam_main_ctrl.mode])();           /* 553 */
        }
    }
}

/* One sub-function lens icon, tinted with that function's own tray colour.
 * lens_tbl[] is a permutation of menu_camera_tex[75..84]. */
void MenuCamCmnReinforcedLensDisp(float x, float y, u_char alpha,
                                  int lens_label)                       /* 571 */
{
    DISP_SPRT lens_ds;

    static int lens_tbl_dat[10] =                                       /* rdata 3bcf48 */
    {
        84, 77, 75, 81, 83, 78, 80, 76, 79, 82,
    };
    static reference_fixed_array<int, 10> lens_tbl(lens_tbl_dat);       /* sbss 3f4de8 */

    if (lens_tbl[lens_label] != -1) {
        CopySprDToSpr(&lens_ds, &menu_camera_tex[lens_tbl[lens_label]]);

        lens_ds.x = x;   lens_ds.y = y;                                 /* 595 */
        lens_ds.alpha = (u_char)(lens_ds.alpha * alpha >> 7);           /* 596 */
        lens_ds.r = CNEquipTrayWrk::equip_func_tbl[lens_label].r;
        lens_ds.g = CNEquipTrayWrk::equip_func_tbl[lens_label].g;
        lens_ds.b = CNEquipTrayWrk::equip_func_tbl[lens_label].b;       /* 597 */
        DispSprD(&lens_ds);                                             /* 598 */
    }
}

/* `num` digits of `data` in one of the three camera digit faces.
 *
 * `count` is the digit's position and the running x is `x + off_x * count`;
 * GCC strength-reduced the pair into one register, which is why the preheader
 * carries a `0 * off_x` multiply that folds to nothing. */
void MenuCamNumberDisp(int data, int num, int x, int y, u_char alpha, int pri,
                       u_char type, u_char zero_flg)                    /* 614 */
{
    int    i;
    int    j;
    int    count;
    int    tmp;
    int    ten_tmp;
    int    off_x;
    u_char set_flg;

    PK2SendVram((uintptr_t)menu_cam_tex_addr, -1, -1, 0);               /* 623 */

    ten_tmp = 1;                                                        /* 629 */
    set_flg = (zero_flg == 1);                                          /* 631 */
    count   = 0;

    off_x = 0;                                                          /* 635 */

    switch (type) {                                                     /* 638 */
    case 0:
        off_x = 14;                                                     /* 640 */
        break;                                                          /* 641 */
    case 1:
    case 2:
        off_x = 17;                                                     /* 644 */
        break;                                                          /* 645 */
    default:
        PRINT_ASSERT("Error! MenuCamNumberDisp Type %d", type);         /* 647 */
        break;
    }

    for (i = num; 0 < i; i--) {                                         /* 651 */
        for (j = i - 1; 0 < j; j = j - 1) {                             /* 652 */
            ten_tmp = ten_tmp * 10;                                     /* 653 */
        }                                                               /* 654 */

        /* Once a non-zero leading digit is seen, draw the rest. */
        if (data / ten_tmp != 0) {                                      /* 655 */
            set_flg = 1;
        }                                                               /* 657 */
        if (((zero_flg == 0) && (data == 0)) && (i == 1)) {             /* 661 */
            set_flg = 1;
        }

        if (i == 1) {                                                   /* 666 */
            tmp = data % 10;                                            /* 667 */
        }
        else {
            tmp = (data / ten_tmp) % 10;                                /* 670 */
        }

        if (set_flg == 1) {                                             /* 673 */
            MenuCamNumberDisp_One(tmp, x + off_x * count, y, alpha, pri,
                                  type);                                /* 675 */
        }

        count   = count + 1;                                            /* 678 */
        ten_tmp = 1;                                                    /* 679 */
    }                                                                   /* 680 */
}

/* One digit.  The three faces are three separate runs in menu_camera_tex[],
 * and the out-of-range message is the hardcoded literal below rather than
 * __FUNCTION__ -- the banner above it carries the name. */
static void MenuCamNumberDisp_One(int data, int x, int y, u_char alpha,
                                  int pri, u_char type)                 /* 694 */
{
    DISP_SPRT num_ds;

    if (9 < data) {                                                     /* 698 */
        PRINT_ASSERT("Error!! MenuCamNumberDisp_One");                  /* 699 */
    }

    switch (type) {                                                     /* 703 */
    case 0:
        CopySprDToSpr(&num_ds, &menu_camera_tex[90 + data]);            /* 705 */
        break;                                                          /* 706 */
    case 1:
        CopySprDToSpr(&num_ds, &menu_camera_tex[112 + data]);           /* 708 */
        break;                                                          /* 709 */
    case 2:
        CopySprDToSpr(&num_ds, &menu_camera_tex[100 + data]);           /* 711 */
        break;                                                          /* 712 */
    default:
        PRINT_ASSERT("Error! MenuCamNumberDisp_One Type %d", type);     /* 714 */
        break;
    }

    num_ds.x = (float)x;   num_ds.y = (float)y;                         /* 717 */
    num_ds.alpha = alpha;                                               /* 718 */
    num_ds.pri = pri;   num_ds.z = 0xfffff - (pri & 0xfffff);           /* 719 */
    DispSprD(&num_ds);                                                  /* 720 */
}

/* One addition-function icon.  No tint -- unlike the lens widget these carry
 * their colour in the art. */
void MenuCamCmnAdditionalFunctionDisp(float x, float y, u_char alpha,
                                      int add_label)                    /* 731 */
{
    DISP_SPRT add_ds;

    static int add_tbl_dat[4] =                                         /* rdata 3bd010 */
    {
        130, 131, 132, 133,
    };
    static reference_fixed_array<int, 4> add_tbl(add_tbl_dat);          /* sbss 3f4df0 */

    if (add_tbl[add_label] != -1) {
        CopySprDToSpr(&add_ds, &menu_camera_tex[add_tbl[add_label]]);

        add_ds.x = x;   add_ds.y = y;                                   /* 749 */
        add_ds.alpha = (u_char)(add_ds.alpha * alpha >> 7);             /* 750 */
        DispSprD(&add_ds);                                              /* 751 */
    }
}

/* One camera part.  The two tables are the same four icons at the same four
 * places out of two different sheets -- fitted and not -- so `flg` swaps the
 * art and nothing else. */
void MenuCamCmnEquipFunctionDisp(float x, float y, u_char alpha,
                                 int parts_label, u_char flg)           /* 765 */
{
    DISP_SPRT parts_ds;

    static int parts_tbl_dat[4] =                                       /* rdata 3bd020 */
    {
        148, 146, 147, 149,
    };
    static reference_fixed_array<int, 4> parts_tbl(parts_tbl_dat);      /* sbss 3f4df8 */

    static int non_parts_tbl_dat[4] =                                   /* rdata 3bd030 */
    {
        144, 142, 143, 145,
    };
    static reference_fixed_array<int, 4> non_parts_tbl(non_parts_tbl_dat); /* sbss 3f4e00 */

    if (flg != 0) {                                                     /* 792 */
        if (parts_tbl[parts_label] != -1) {                             /* 793 */
            CopySprDToSpr(&parts_ds, &menu_camera_tex[parts_tbl[parts_label]]);
                                                                        /* 794 */
            parts_ds.x = x;   parts_ds.y = y;                           /* 795 */
            parts_ds.alpha = (u_char)(parts_ds.alpha * alpha >> 7);     /* 796 */
            DispSprD(&parts_ds);                                        /* 797 */
        }
    }
    else {
        if (non_parts_tbl[parts_label] != -1) {                         /* 801 */
            CopySprDToSpr(&parts_ds,
                          &menu_camera_tex[non_parts_tbl[parts_label]]); /* 802 */
            parts_ds.x = x;   parts_ds.y = y;                           /* 803 */
            parts_ds.alpha = (u_char)(parts_ds.alpha * alpha >> 7);     /* 804 */
            DispSprD(&parts_ds);                                        /* 805 */
        }
    }
}
