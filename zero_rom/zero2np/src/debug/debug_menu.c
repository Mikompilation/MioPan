// FILE: /home/zero_rom/zero2np/src/debug/debug_menu.c
//
// Generic nested debug-menu storage and renderer.
//
// The menu tables occupy most of the original file (DrawDbgMenuSub() starts at
// line 7129), so the ROM line numbers on the two draw functions are large.
// Five of the tables are statically initialised in .data (0x2d9cb0..0x2db0b0);
// the six item submenus were built by a C++ dynamic initialiser because the
// ROM reached plyr_item through fixed_array::operator[], which is not a
// constant expression.  Here they are static (see DBM_ITEM below) -- the
// resulting tables are byte-identical, which the ROM images confirm.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "debug_menu.h"

#include "zero2_debug.h"

#include "../common/variable.h"                  // debug_var / opt_wrk / key_now
#include "../graphics/graph2d/g2d_draw.h"        // DISP_SQAR / SQAR_DAT / DispSqrD
#include "../graphics/graph2d/message.h"         // SetASCIIString2 / SetString2
#include "../graphics/obj_draw_ctrl.h"           // obj_draw_ctrl
#include "camera_menu.h"                         // dbg_camera_main (port addition)

#include "../ingame/enemy/fene_entry.h"          // dbg_random_ghost
#include "../ingame/event/prg/ev_debug.h"        // dbg_event_debug (port addition)
#include "../ingame/item/prg/item.h"             // plyr_item
#include "../ingame/map/MhCtl.h"                 // dbg_room_main
#include "../ingame/photo/n_equip_tray.h"        // dbg_stock_num
#include "../main/glob.h"                        // opt_wrk
#include "../sdk/sce_gs.h"                       // sceGsDBuff
#include "../system/os/system.h"                 // gdb
#include "../system/pad/pad.h"                   // paddat

#include <stdio.h>
#include <string.h>

static char s_dbm_end[] = "_end_";
static char s_dbm_fmt_str[] = "%s";
static char s_dbm_fmt_float[] = "%f";
static char s_dbm_fmt_int[] = "%5d";
static char s_dbm_on[] = " ON";
static char s_dbm_off[] = "OFF";
static char s_dbm_mark[] = "*";
static char s_dbm_exit[] = "EXIT";
static char s_dbm_fmt_len[] = "%.02f";

static char s_dbg_menu_title[] = "DEBUG MENU";
static char s_dbg_item[] = "ITEM";
static char s_dbg_room[] = "ROOM";
static char s_dbg_hit_rect[] = "HIT RECTANGLE";
static char s_dbg_ene_stop[] = "ENE STOP";
static char s_dbg_high_speed[] = "HIGH SPEED";
static char s_dbg_perf_counter[] = "PERF COUNTER";
static char s_dbg_memory_disp[] = "MEMORY_DISP";
static char s_dbg_stock_num[] = "STOCK_NUM";
static char s_dbg_muteki[] = "MUTEKI";
static char s_dbg_disp[] = "DISP";
static char s_dbg_brightness[] = "BRIGHTNESS";
static char s_dbg_random_enemy[] = "RANDOM ENEMY";
static char s_dbg_sis_trace[] = "SIS TRACE DEBUG";
/* Port additions -- not ROM strings; see the dbg_menu_main rows below. */
static char s_dbg_event_debug[] = "EVENT DEBUG";
/* Distinct from s_dbg_camera below, which is the ITEM menu's camera obscura
 * row -- same text, different table. */
static char s_dbg_camera_menu[] = "CAMERA";

static char s_dbg_disp_title[] = "Disp Main";
static char s_dbg_player_disp[] = "PLAYER DISP";
static char s_dbg_sister_disp[] = "SISTER DISP";
static char s_dbg_room_disp[] = "ROOM DISP";
static char s_dbg_obj_disp[] = "OBJ DISP";
static char s_dbg_sky_disp[] = "SKY DISP";
static char s_dbg_shadow_disp[] = "SHADOW DISP";

static char s_dbg_item_title[] = "PLAYER ITEM";
static char s_dbg_film_item[] = "FILM ITEM";
static char s_dbg_recovery_item[] = "RECOVERY ITEM";
static char s_dbg_event_item1[] = "EVENT ITEM 1";
static char s_dbg_event_item2[] = "EVENT ITEM 2";
static char s_dbg_event_item3[] = "EVENT ITEM 3";
static char s_dbg_event_item4[] = "EVENT ITEM 4";

static char s_dbg_cam_film_07[] = "CAM_FILM_07";
static char s_dbg_cam_film_14[] = "CAM_FILM_14";
static char s_dbg_cam_film_61[] = "CAM_FILM_61";
static char s_dbg_cam_film_90[] = "CAM_FILM_90";
static char s_dbg_cam_film_00[] = "CAM_FILM_00";

static char s_dbg_manyougan[] = "MANYOUGAN";
static char s_dbg_gosinsui[] = "GOSINSUI";
static char s_dbg_kagamiisi[] = "KAGAMIISI";
static char s_dbg_reiseki[] = "REISEKI";

static char s_dbg_camera[] = "CAMERA";
static char s_dbg_flashlight[] = "FLASHLIGHT";
static char s_dbg_hutago_key_r[] = "HUTAGO KEY R";
static char s_dbg_hutago_key_l[] = "HUTAGO KEY L";
static char s_dbg_miyako_book1[] = "MIYAKO BOOK 1";
static char s_dbg_miyako_book2[] = "MIYAKO BOOK 2";
static char s_dbg_komonjo[] = "KOMONJO";
static char s_dbg_ousaka_map[] = "OUSAKA_MAP";
static char s_dbg_hina_kubi[] = "HINA KUBI";
static char s_dbg_hudakagi_higasi[] = "HUDAKAGI HIGASI";
static char s_dbg_hudakagi_hina[] = "HUDAKAGI HINA";
static char s_dbg_hudakagi_dozou[] = "HUDAKAGI DOZOU";
static char s_dbg_hudakagi_wakido[] = "HUDAKAGI WAKIDO";
static char s_dbg_kyakuma_key[] = "KYAKUMA KEY";

static char s_dbg_pzl_roku_hon1[] = "PZL ROKU HON1";
static char s_dbg_pzl_roku_hon2[] = "PZL ROKU HON2";
static char s_dbg_pzl_roku_hon3[] = "PZL ROKU HON3";
static char s_dbg_pzl_roku_hon4[] = "PZL ROKU HON4";
static char s_dbg_pzl_roku_hon5[] = "PZL ROKU HON5";
static char s_dbg_zashiki_nai_key1[] = "ZASHIKI NAI KEY1";
static char s_dbg_zashiki_nai_key2[] = "ZASHIKI NAI KEY2";
static char s_dbg_zashiki_gai_key[] = "ZASHIKI GAI KEY";
static char s_dbg_mari[] = "MARI";
static char s_dbg_reel1[] = "REEL1";
static char s_dbg_reel2[] = "REEL2";
static char s_dbg_reel3[] = "REEL3";
static char s_dbg_reel4[] = "REEL4";
static char s_dbg_reel5[] = "REEL5";
static char s_dbg_reel6[] = "REEL6";
static char s_dbg_reel7[] = "REEL7";
static char s_dbg_suzu[] = "SUZU";

static char s_dbg_doll_head[] = "DOLL HEAD";
static char s_dbg_doll_r_arm[] = "DOLL R ARM";
static char s_dbg_doll_l_arm[] = "DOLL L ARM";
static char s_dbg_doll_eye[] = "DOLL EYE";
static char s_dbg_kaza_panel1[] = "KAZA PANEL1";
static char s_dbg_kaza_panel2[] = "KAZA PANEL2";
static char s_dbg_kaza_panel3[] = "KAZA PANEL3";
static char s_dbg_kaza_panel4[] = "KAZA PANEL4";
static char s_dbg_radio[] = "RADIO";
static char s_dbg_miyako_bag[] = "MIYAKO BAG";
static char s_dbg_doll_sekkei[] = "DOLL SEKKEI";
static char s_dbg_himo_r_doll[] = "HIMO R DOLL";
static char s_dbg_narabi_key[] = "NARABI KEY";
static char s_dbg_kura_key[] = "KURA_KEY";

static char s_dbg_fukamichi_key[] = "FUKAMICHI KEY";

static char s_dbg_mem_title[] = "Memory Main";
static char s_dbg_spu[] = "SPU";
static char s_dbg_model[] = "MODEL";
static char s_dbg_common[] = "COMMON";
static char s_dbg_system[] = "SYSTEM";
static char s_dbg_iop[] = "IOP";

static char s_dbg_ene_title[] = "ENEMY IN_OUT";
static char s_dbg_dat_no[] = "DAT_NO";
static char s_dbg_in_out[] = "IN OUT";

/* Framebuffer clear colour pushed into both draw environments at the end of
 * DrawDbgMenu().  Nothing ever writes it, so it only forces black; it is kept
 * because the ROM's write is unconditional and would otherwise be lost. */
static sceVu0IVECTOR s_ivBGColor;                                       /* data 2d9ca0 */

DEBUG_MENU dbg_menu_main =                                              /* data 2d9cb0 */
{
    nullptr,
    nullptr,
    s_dbg_menu_title,
    {
        { s_dbg_item,          DBM_ATTR_MENU,   &dbg_item_main,          0.0f,   0.0f, 0.0f },
        { s_dbg_room,          DBM_ATTR_MENU,   &dbg_room_main,          0.0f,   0.0f, 0.0f },
        { s_dbg_hit_rect,      DBM_ATTR_SWITCH, &debug_var.hit_disp,     0.0f,   1.0f, 1.0f },
        { s_dbg_ene_stop,      DBM_ATTR_SWITCH, &debug_var.ene_stop,     0.0f,   1.0f, 1.0f },
        { s_dbg_high_speed,    DBM_ATTR_SWITCH, &debug_var.hi_spd,       0.0f,   1.0f, 1.0f },
        { s_dbg_perf_counter,  DBM_ATTR_SWITCH, &debug_var.perf_count_sw, 0.0f,  1.0f, 1.0f },
        { s_dbg_memory_disp,   DBM_ATTR_MENU,   &dbg_mem_main,           0.0f,   0.0f, 0.0f },
        { s_dbg_stock_num,     DBM_ATTR_VALUE | DBM_ATTR_ONESHOT,
                                                &dbg_stock_num,          0.0f,   5.0f, 1.0f },
        { s_dbg_muteki,        DBM_ATTR_SWITCH, &debug_var.muteki,       0.0f,   1.0f, 1.0f },
        { s_dbg_disp,          DBM_ATTR_MENU,   &dbg_disp_main,          0.0f,   0.0f, 0.0f },
        { s_dbg_brightness,    DBM_ATTR_VALUE,  &opt_wrk.brightness,     0.0f, 255.0f, 1.0f },
        { s_dbg_random_enemy,  DBM_ATTR_SWITCH, &dbg_random_ghost,       0.0f,   1.0f, 1.0f },
        { s_dbg_sis_trace,     DBM_ATTR_SWITCH, &debug_var.sis_tr_point, 0.0f,   1.0f, 1.0f },
        /* Port addition -- the ROM's table ends at SIS TRACE DEBUG.  ev_debug.o
         * exists but nothing calls it, so this row is what reaches EvDbgMain().
         * The table is 20 slots and mnum is recounted from "_end_" every frame,
         * so appending here needs no other change. */
        { s_dbg_event_debug,   DBM_ATTR_SWITCH, &dbg_event_debug,        0.0f,   1.0f, 1.0f },
        /* Port addition -- camera_menu.o carries the three DebugCameraMenu
         * flags and no code at all in this build, so its menu had to be
         * rebuilt.  Same reasoning as the EVENT DEBUG row above. */
        { s_dbg_camera_menu,   DBM_ATTR_MENU,   &dbg_camera_main,        0.0f,   0.0f, 0.0f },
        { s_dbm_end,           0,               nullptr,                 0.0f,   0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_disp_main =                                              /* data 2d9eb0 */
{
    &dbg_menu_main,
    nullptr,
    s_dbg_disp_title,
    {
        { s_dbg_player_disp, DBM_ATTR_SWITCH, &obj_draw_ctrl.player, 0.0f, 1.0f, 1.0f },
        { s_dbg_sister_disp, DBM_ATTR_SWITCH, &obj_draw_ctrl.sister, 0.0f, 1.0f, 1.0f },
        { s_dbg_room_disp,   DBM_ATTR_SWITCH, &obj_draw_ctrl.room,   0.0f, 1.0f, 1.0f },
        { s_dbg_obj_disp,    DBM_ATTR_SWITCH, &obj_draw_ctrl.obj,    0.0f, 1.0f, 1.0f },
        { s_dbg_sky_disp,    DBM_ATTR_SWITCH, &obj_draw_ctrl.sky,    0.0f, 1.0f, 1.0f },
        { s_dbg_shadow_disp, DBM_ATTR_SWITCH, &obj_draw_ctrl.shadow, 0.0f, 1.0f, 1.0f },
        { s_dbm_end,         0,               nullptr,               0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_item_main =                                              /* data 2da0b0 */
{
    &dbg_menu_main,
    nullptr,
    s_dbg_item_title,
    {
        { s_dbg_film_item,     DBM_ATTR_MENU, &dbg_film_item,     0.0f, 0.0f, 0.0f },
        { s_dbg_recovery_item, DBM_ATTR_MENU, &dbg_recovery_item, 0.0f, 0.0f, 0.0f },
        { s_dbg_event_item1,   DBM_ATTR_MENU, &dbg_event_item,    0.0f, 0.0f, 0.0f },
        { s_dbg_event_item2,   DBM_ATTR_MENU, &dbg_event_item2,   0.0f, 0.0f, 0.0f },
        { s_dbg_event_item3,   DBM_ATTR_MENU, &dbg_event_item3,   0.0f, 0.0f, 0.0f },
        { s_dbg_event_item4,   DBM_ATTR_MENU, &dbg_event_item4,   0.0f, 0.0f, 0.0f },
        { s_dbm_end,           0,             nullptr,            0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

/* The item rows write plyr_item[].have_num straight through, so a row set to
 * a non-zero count is exactly "the player is now carrying this".
 *
 * The ROM reached the element through fixed_array::operator[], which is not a
 * constant expression, which is why its tables needed a dynamic initialiser.
 * fixed_array_base keeps m_aData protected and its storage is the whole
 * object, so casting the array's address to the element type gives the same
 * link-time constant and keeps these tables static. */
#define DBM_ITEM(n) (&((PLYR_ITEM *)&plyr_item)[n].have_num)

DEBUG_MENU dbg_film_item =                                              /* data 2da2b0 */
{
    &dbg_item_main,
    nullptr,
    s_dbg_film_item,
    {
        { s_dbg_cam_film_07, DBM_ATTR_VALUE, DBM_ITEM(0), 0.0f, 99.0f, 1.0f },
        { s_dbg_cam_film_14, DBM_ATTR_VALUE, DBM_ITEM(1), 0.0f, 99.0f, 1.0f },
        { s_dbg_cam_film_61, DBM_ATTR_VALUE, DBM_ITEM(2), 0.0f, 99.0f, 1.0f },
        { s_dbg_cam_film_90, DBM_ATTR_VALUE, DBM_ITEM(3), 0.0f, 99.0f, 1.0f },
        { s_dbg_cam_film_00, DBM_ATTR_VALUE, DBM_ITEM(4), 0.0f, 99.0f, 1.0f },
        { s_dbm_end,         0,              nullptr,     0.0f,  0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_recovery_item =                                          /* data 2da4b0 */
{
    &dbg_item_main,
    nullptr,
    s_dbg_recovery_item,
    {
        { s_dbg_manyougan, DBM_ATTR_VALUE, DBM_ITEM(5), 0.0f, 99.0f, 1.0f },
        { s_dbg_gosinsui,  DBM_ATTR_VALUE, DBM_ITEM(6), 0.0f, 99.0f, 1.0f },
        { s_dbg_kagamiisi, DBM_ATTR_VALUE, DBM_ITEM(7), 0.0f,  1.0f, 1.0f },
        { s_dbg_reiseki,   DBM_ATTR_VALUE, DBM_ITEM(8), 0.0f, 99.0f, 1.0f },
        { s_dbm_end,       0,              nullptr,     0.0f,  0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_event_item =                                             /* data 2da6b0 */
{
    &dbg_item_main,
    nullptr,
    s_dbg_event_item1,
    {
        { s_dbg_camera,          DBM_ATTR_VALUE, DBM_ITEM(10), 0.0f, 1.0f, 1.0f },
        { s_dbg_flashlight,      DBM_ATTR_VALUE, DBM_ITEM(11), 0.0f, 1.0f, 1.0f },
        { s_dbg_hutago_key_r,    DBM_ATTR_VALUE, DBM_ITEM(12), 0.0f, 1.0f, 1.0f },
        { s_dbg_hutago_key_l,    DBM_ATTR_VALUE, DBM_ITEM(13), 0.0f, 1.0f, 1.0f },
        { s_dbg_miyako_book1,    DBM_ATTR_VALUE, DBM_ITEM(14), 0.0f, 1.0f, 1.0f },
        { s_dbg_miyako_book2,    DBM_ATTR_VALUE, DBM_ITEM(15), 0.0f, 1.0f, 1.0f },
        { s_dbg_komonjo,         DBM_ATTR_VALUE, DBM_ITEM(16), 0.0f, 1.0f, 1.0f },
        /* plyr_item[17] has no row -- the gap is in the ROM table too. */
        { s_dbg_ousaka_map,      DBM_ATTR_VALUE, DBM_ITEM(18), 0.0f, 1.0f, 1.0f },
        { s_dbg_hina_kubi,       DBM_ATTR_VALUE, DBM_ITEM(19), 0.0f, 1.0f, 1.0f },
        { s_dbg_hudakagi_higasi, DBM_ATTR_VALUE, DBM_ITEM(20), 0.0f, 1.0f, 1.0f },
        { s_dbg_hudakagi_hina,   DBM_ATTR_VALUE, DBM_ITEM(21), 0.0f, 1.0f, 1.0f },
        { s_dbg_hudakagi_dozou,  DBM_ATTR_VALUE, DBM_ITEM(22), 0.0f, 1.0f, 1.0f },
        { s_dbg_hudakagi_wakido, DBM_ATTR_VALUE, DBM_ITEM(23), 0.0f, 1.0f, 1.0f },
        { s_dbg_kyakuma_key,     DBM_ATTR_VALUE, DBM_ITEM(24), 0.0f, 1.0f, 1.0f },
        { s_dbm_end,             0,              nullptr,      0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_event_item2 =                                            /* data 2da8b0 */
{
    &dbg_item_main,
    nullptr,
    s_dbg_event_item2,
    {
        { s_dbg_pzl_roku_hon1,    DBM_ATTR_VALUE, DBM_ITEM(25), 0.0f, 1.0f, 1.0f },
        { s_dbg_pzl_roku_hon2,    DBM_ATTR_VALUE, DBM_ITEM(26), 0.0f, 1.0f, 1.0f },
        { s_dbg_pzl_roku_hon3,    DBM_ATTR_VALUE, DBM_ITEM(27), 0.0f, 1.0f, 1.0f },
        { s_dbg_pzl_roku_hon4,    DBM_ATTR_VALUE, DBM_ITEM(28), 0.0f, 1.0f, 1.0f },
        { s_dbg_pzl_roku_hon5,    DBM_ATTR_VALUE, DBM_ITEM(29), 0.0f, 1.0f, 1.0f },
        { s_dbg_zashiki_nai_key1, DBM_ATTR_VALUE, DBM_ITEM(30), 0.0f, 1.0f, 1.0f },
        { s_dbg_zashiki_nai_key2, DBM_ATTR_VALUE, DBM_ITEM(31), 0.0f, 1.0f, 1.0f },
        { s_dbg_zashiki_gai_key,  DBM_ATTR_VALUE, DBM_ITEM(32), 0.0f, 1.0f, 1.0f },
        { s_dbg_mari,             DBM_ATTR_VALUE, DBM_ITEM(33), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel1,            DBM_ATTR_VALUE, DBM_ITEM(34), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel2,            DBM_ATTR_VALUE, DBM_ITEM(35), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel3,            DBM_ATTR_VALUE, DBM_ITEM(36), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel4,            DBM_ATTR_VALUE, DBM_ITEM(37), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel5,            DBM_ATTR_VALUE, DBM_ITEM(38), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel6,            DBM_ATTR_VALUE, DBM_ITEM(39), 0.0f, 1.0f, 1.0f },
        { s_dbg_reel7,            DBM_ATTR_VALUE, DBM_ITEM(40), 0.0f, 1.0f, 1.0f },
        { s_dbg_suzu,             DBM_ATTR_VALUE, DBM_ITEM(41), 0.0f, 1.0f, 1.0f },
        { s_dbm_end,              0,              nullptr,      0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_event_item3 =                                            /* data 2daab0 */
{
    &dbg_item_main,
    nullptr,
    s_dbg_event_item3,
    {
        { s_dbg_doll_head,   DBM_ATTR_VALUE, DBM_ITEM(42), 0.0f, 1.0f, 1.0f },
        { s_dbg_doll_r_arm,  DBM_ATTR_VALUE, DBM_ITEM(43), 0.0f, 1.0f, 1.0f },
        { s_dbg_doll_l_arm,  DBM_ATTR_VALUE, DBM_ITEM(44), 0.0f, 1.0f, 1.0f },
        { s_dbg_doll_eye,    DBM_ATTR_VALUE, DBM_ITEM(45), 0.0f, 1.0f, 1.0f },
        { s_dbg_kaza_panel1, DBM_ATTR_VALUE, DBM_ITEM(46), 0.0f, 1.0f, 1.0f },
        { s_dbg_kaza_panel2, DBM_ATTR_VALUE, DBM_ITEM(47), 0.0f, 1.0f, 1.0f },
        { s_dbg_kaza_panel3, DBM_ATTR_VALUE, DBM_ITEM(48), 0.0f, 1.0f, 1.0f },
        { s_dbg_kaza_panel4, DBM_ATTR_VALUE, DBM_ITEM(49), 0.0f, 1.0f, 1.0f },
        { s_dbg_radio,       DBM_ATTR_VALUE, DBM_ITEM(50), 0.0f, 1.0f, 1.0f },
        { s_dbg_miyako_bag,  DBM_ATTR_VALUE, DBM_ITEM(51), 0.0f, 1.0f, 1.0f },
        { s_dbg_doll_sekkei, DBM_ATTR_VALUE, DBM_ITEM(52), 0.0f, 1.0f, 1.0f },
        { s_dbg_himo_r_doll, DBM_ATTR_VALUE, DBM_ITEM(53), 0.0f, 1.0f, 1.0f },
        { s_dbg_narabi_key,  DBM_ATTR_VALUE, DBM_ITEM(54), 0.0f, 1.0f, 1.0f },
        { s_dbg_kura_key,    DBM_ATTR_VALUE, DBM_ITEM(55), 0.0f, 1.0f, 1.0f },
        { s_dbm_end,         0,              nullptr,      0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

DEBUG_MENU dbg_event_item4 =                                            /* data 2dacb0 */
{
    &dbg_item_main,
    nullptr,
    s_dbg_event_item4,
    {
        { s_dbg_fukamichi_key, DBM_ATTR_VALUE, DBM_ITEM(56), 0.0f, 1.0f, 1.0f },
        { s_dbm_end,           0,              nullptr,      0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

#undef DBM_ITEM

DEBUG_MENU dbg_mem_main =                                               /* data 2daeb0 */
{
    &dbg_menu_main,
    nullptr,
    s_dbg_mem_title,
    {
        { s_dbg_spu,    DBM_ATTR_SWITCH, &dbg_spu_mem_disp,    0.0f, 1.0f, 1.0f },
        { s_dbg_model,  DBM_ATTR_SWITCH, &dbg_mdl_mem_disp,    0.0f, 1.0f, 1.0f },
        { s_dbg_common, DBM_ATTR_SWITCH, &dbg_cmn_mem_disp,    0.0f, 1.0f, 1.0f },
        { s_dbg_system, DBM_ATTR_SWITCH, &dbg_system_mem_disp, 0.0f, 1.0f, 1.0f },
        { s_dbg_iop,    DBM_ATTR_SWITCH, &dbg_iop_mem_disp,    0.0f, 1.0f, 1.0f },
        { s_dbm_end,    0,               nullptr,              0.0f, 0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

/* Reachable only by pointing another menu's child at it -- nothing in the
 * shipped tables links to it, but the table is in the ROM's .data. */
DEBUG_MENU dbg_ene_main =                                               /* data 2db0b0 */
{
    &dbg_menu_main,
    nullptr,
    s_dbg_ene_title,
    {
        { s_dbg_dat_no, DBM_ATTR_VALUE,  &dbg_ene_no,       0.0f, 20.0f, 1.0f },
        { s_dbg_in_out, DBM_ATTR_SWITCH, &dbg_enemy_button, 0.0f,  1.0f, 1.0f },
        { s_dbm_end,    0,               nullptr,           0.0f,  0.0f, 0.0f },
    },
    0,
    0,
    0,
    0,
};

static DEBUG_MENU *now_tree = &dbg_menu_main;                           /* sdata 3efa50 */

void DebugInit(void)
{
    memset(&debug_var, 0, sizeof(DEBUG_VAR));

    debug_var.fog_sw = 0;
    debug_var.pl_amb = 0.5f;
    debug_var.sis_para_r = 0.04f;
    debug_var.sis_para_g = 0.04f;
    debug_var.sis_para_b = 0.04f;
    debug_var.fStaticDirLightColStepR = 0.3f;
    debug_var.fStaticDirLightColStepG = 0.1f;
    debug_var.fStaticDirLightColStepB = 0.1f;
    debug_var.fYFlashlightStep = -270.0f;
    debug_var.fRangeFlashlightStep = 311.0f;
    debug_var.fl2_range = 1000.0f;
    debug_var.shadow_model_disp = 0;

    /* Port addition -- the free camera's tunables.  Without this its floats
     * arrive zeroed and the menu's display clamp snaps FOV to its 0.1 rad
     * minimum the first time the CAMERA panel is drawn. */
    DebugCameraMenuInit();
}

/* Width in characters that "%.02f" will render f as -- the value column is
 * right-aligned against wlp->max, so the caller needs the length up front. */
int getFStrLength(float f)
{
    char cwo[256];

    sprintf(cwo, s_dbm_fmt_len, (double)f);
    return (int)strlen(cwo);
}

DEBUG_MENU *GetNowMenu(void)
{
    return now_tree;
}

/* Draws one menu panel plus, recursively, every ancestor behind it.  fl is 1
 * for the focused menu and 0 for the parents, which only changes the two grey
 * levels.  Each panel is a stack of five nested quads (the bevel), four corner
 * notches, and a highlight bar under the cursor row. */
void DrawDbgMenuSub(DEBUG_MENU *wlp, int fl)                            /* 7129 */
{
    int i;
    int kai;
    int max;
    int mnum;
    int x0;                 /* panel left  (24 * kai + 20) */
    int y0;                 /* panel top   (32 * kai + 24) */
    int x1;                 /* panel right                */
    int y1;                 /* panel bottom               */
    int tx;                 /* text left                  */
    int ty;
    int pri;
    u_int lay;
    u_char r;
    u_char rgb1;
    DEBUG_SUB_MENU *pnow;
    SQAR_DAT sq;
    DISP_SQAR dq;

    /* The PS2 submits ancestors last and relies on GS depth to leave the
     * focused child visible.  MioPan's host 2D queue is painter-ordered with
     * depth disabled, so submit ancestors first for the same visual result. */
    if (wlp->parent != nullptr)
    {
        DrawDbgMenuSub(wlp->parent, 0);
    }

    kai = wlp->kai;                                                     /* 7135 */
    max = wlp->max;                                                     /* 7137 */
    mnum = wlp->mnum;                                                   /* 7138 */

    sq.w = 0x280;                                                       /* 7142 */
    sq.h = 0x1c0;
    sq.x = 0;
    sq.y = 0;
    sq.pri = 0;
    sq.r = 0;
    sq.g = 0;
    sq.b = 0;
    sq.alpha = 0x80;

    x0 = kai * 0x18 + 0x14;                                             /* 7135 */
    y0 = kai * 0x20 + 0x18;                                             /* 7136 */

    /* Deeper menus sit on a lower layer so the focused one draws on top. */
    lay = (u_int)(kai < 0x10 ? 0xf - kai : 0) & 0xf;                    /* 7145 */

    if (fl == 0)                                                        /* 7146 */
    {
        rgb1 = 0x20;                                                    /* 7150 */
        r = 0x40;                                                       /* 7151 */
    }
    else
    {
        rgb1 = 0x40;
        r = 0x80;                                                       /* 7148 */
    }

    CopySqrDToSqr(&dq, &sq);                                            /* 7154 */

    dq.pri = lay * 0x10;                                                /* 7155 */
    dq.z = 0xfffff - lay * 0x10;
    dq.alpha = 0x80;                                                    /* 7156 */
    dq.zbuf = 0xa000118;                                                /* 7158 */
    dq.test = 0x5000d;                                                  /* 7159 */

    x1 = x0 + max * 0xc + 0xc;                                          /* 7160 */
    y1 = y0 + mnum * 0xe + 4;

    /* Bevel: five concentric quads, each one pixel in from the last. */
    dq.x[0] = x0;                                                       /* 7160 */
    dq.x[1] = x1 + 0xe;
    dq.x[2] = x0;
    dq.x[3] = x1 + 0xe;
    dq.y[0] = y0;
    dq.y[1] = y0;
    dq.y[2] = y1 + 0x23;
    dq.y[3] = y1 + 0x23;
    for (i = 0; i < 4; i++)                                             /* 7161 */
    {
        dq.r[i] = 0;
        dq.g[i] = 0;
        dq.b[i] = 0;
    }
    DispSqrD(&dq);                                                      /* 7162 */

    dq.zbuf = 0x10a000118;                                              /* 7163 */
    dq.x[0] = x0 + 1;                                                   /* 7164 */
    dq.x[1] = x1 + 0xd;
    dq.x[2] = x0 + 1;
    dq.x[3] = x1 + 0xd;
    dq.y[0] = y0 + 1;
    dq.y[1] = y0 + 1;
    dq.y[2] = y1 + 0x21;
    dq.y[3] = y1 + 0x21;
    for (i = 0; i < 4; i++)                                             /* 7165 */
    {
        dq.r[i] = rgb1;
        dq.g[i] = rgb1;
        dq.b[i] = rgb1;
    }
    DispSqrD(&dq);                                                      /* 7166 */

    dq.x[0] = x0 + 3;                                                   /* 7167 */
    dq.x[1] = x1 + 0xb;
    dq.x[2] = x0 + 3;
    dq.x[3] = x1 + 0xb;
    dq.y[0] = y0 + 3;
    dq.y[1] = y0 + 3;
    dq.y[2] = y1 + 0x1f;
    dq.y[3] = y1 + 0x1f;
    for (i = 0; i < 4; i++)                                             /* 7168 */
    {
        dq.r[i] = r;
        dq.g[i] = r;
        dq.b[i] = r;
    }
    DispSqrD(&dq);                                                      /* 7169 */

    dq.x[0] = x0 + 5;                                                   /* 7170 */
    dq.x[1] = x1 + 9;
    dq.x[2] = x0 + 5;
    dq.x[3] = x1 + 9;
    dq.y[0] = y0 + 5;
    dq.y[1] = y0 + 5;
    dq.y[2] = y1 + 0x1d;
    dq.y[3] = y1 + 0x1d;
    for (i = 0; i < 4; i++)                                             /* 7171 */
    {
        dq.r[i] = rgb1;
        dq.g[i] = rgb1;
        dq.b[i] = rgb1;
    }
    DispSqrD(&dq);                                                      /* 7172 */

    dq.x[0] = x0 + 7;                                                   /* 7173 */
    dq.x[1] = x1 + 7;
    dq.x[2] = x0 + 7;
    dq.x[3] = x1 + 7;
    dq.y[0] = y0 + 7;
    dq.y[1] = y0 + 7;
    dq.y[2] = y1 + 0x1b;
    dq.y[3] = y1 + 0x1b;
    for (i = 0; i < 4; i++)                                             /* 7174 */
    {
        dq.r[i] = 0;
        dq.g[i] = 0;
        dq.b[i] = 0;
    }
    DispSqrD(&dq);                                                      /* 7175 */

    /* Title separator: two thin bars under the caption. */
    dq.x[0] = x0 + 5;                                                   /* 7177 */
    dq.x[1] = x1 + 9;
    dq.x[2] = x0 + 5;
    dq.x[3] = x1 + 9;
    dq.y[0] = y0 + 0x18;
    dq.y[1] = y0 + 0x18;
    dq.y[2] = y0 + 0x1d;
    dq.y[3] = y0 + 0x1d;
    for (i = 0; i < 4; i++)                                             /* 7178 */
    {
        dq.r[i] = rgb1;
        dq.g[i] = rgb1;
        dq.b[i] = rgb1;
    }
    DispSqrD(&dq);                                                      /* 7179 */

    dq.x[0] = x0 + 3;                                                   /* 7180 */
    dq.x[1] = x1 + 0xb;
    dq.x[2] = x0 + 3;
    dq.x[3] = x1 + 0xb;
    dq.y[0] = y0 + 0x1a;
    dq.y[1] = y0 + 0x1a;
    dq.y[2] = y0 + 0x1b;
    dq.y[3] = y0 + 0x1b;
    for (i = 0; i < 4; i++)                                             /* 7181 */
    {
        dq.r[i] = r;
        dq.g[i] = r;
        dq.b[i] = r;
    }
    DispSqrD(&dq);                                                      /* 7182 */

    /* Four black corner notches. */
    for (i = 0; i < 4; i++)                                             /* 7184 */
    {
        dq.r[i] = 0;
        dq.g[i] = 0;
        dq.b[i] = 0;
    }

    dq.x[0] = x0;                                                       /* 7185 */
    dq.x[1] = x0 + 4;
    dq.x[2] = x0;
    dq.x[3] = x0 + 4;
    dq.y[0] = y0;
    dq.y[1] = y0;
    dq.y[2] = y0 + 4;
    dq.y[3] = y0 + 4;
    DispSqrD(&dq);                                                      /* 7186 */

    dq.x[0] = x0;                                                       /* 7187 */
    dq.x[1] = x0 + 4;
    dq.x[2] = x0;
    dq.x[3] = x0 + 4;
    dq.y[0] = y1 + 0x1f;
    dq.y[1] = y1 + 0x1f;
    dq.y[2] = y1 + 0x23;
    dq.y[3] = y1 + 0x23;
    DispSqrD(&dq);                                                      /* 7188 */

    dq.x[0] = x1 + 0xa;                                                 /* 7189 */
    dq.x[1] = x1 + 0xe;
    dq.x[2] = x1 + 0xa;
    dq.x[3] = x1 + 0xe;
    dq.y[0] = y0;
    dq.y[1] = y0;
    dq.y[2] = y0 + 4;
    dq.y[3] = y0 + 4;
    DispSqrD(&dq);                                                      /* 7190 */

    dq.x[0] = x1 + 0xa;                                                 /* 7191 */
    dq.x[1] = x1 + 0xe;
    dq.x[2] = x1 + 0xa;
    dq.x[3] = x1 + 0xe;
    dq.y[0] = y1 + 0x1f;
    dq.y[1] = y1 + 0x1f;
    dq.y[2] = y1 + 0x23;
    dq.y[3] = y1 + 0x23;
    DispSqrD(&dq);                                                      /* 7192 */

    /* Cursor bar: blue-only, one row tall. */
    for (i = 0; i < 4; i++)                                             /* 7195 */
    {
        dq.r[i] = 0;
        dq.g[i] = 0;
        dq.b[i] = rgb1;
    }

    tx = kai * 0x18 + 0x21;
    pri = (int)(lay << 4);

    dq.x[0] = x0 + 7;                                                   /* 7196 */
    dq.x[1] = x1 + 7;
    dq.x[2] = x0 + 7;
    dq.x[3] = x1 + 7;
    dq.y[0] = y0 + wlp->pos * 0xe + 0x1d;
    dq.y[1] = dq.y[0];
    dq.y[2] = y0 + wlp->pos * 0xe + 0x2d;
    dq.y[3] = dq.y[2];
    DispSqrD(&dq);                                                      /* 7197 */

    SetASCIIString2(pri, (float)tx, (float)(kai * 0x20 + 0x22), 0,
                    r, r, r, wlp->title);                               /* 7201 */

    /* Rows are emitted last-to-first; the ROM walks the table backwards. */
    pnow = wlp->submenu + wlp->mnum - 1;
    ty = (wlp->mnum - 1) * 0xe + y0 + 0x21;
    for (i = wlp->mnum - 1; i >= 0; i--)                                /* 7202 */
    {
        if ((pnow->attr & DBM_ATTR_VALUE) != 0)                         /* 7204 */
        {
            SetString2(pri, (float)tx, (float)ty, 0,
                       r, r, r, s_dbm_fmt_str, pnow->name);             /* 7205 */

            if ((pnow->attr & DBM_ATTR_FLOAT) != 0)                     /* 7206 */
            {
                int len = getFStrLength(*(float *)pnow->child);
                SetString2(pri, (float)(x0 + (wlp->max - len) * 0xc + 1),
                           (float)ty, 0, r, r, r, s_dbm_fmt_float,
                           (double)*(float *)pnow->child);              /* 7208 */
            }
            else
            {
                SetString2(pri, (float)(x0 + wlp->max * 0xc - 0x3b),
                           (float)ty, 0, r, r, r, s_dbm_fmt_int,
                           *(int *)pnow->child);                        /* 7210 */
            }
        }
        else if ((pnow->attr & DBM_ATTR_SWITCH) != 0)                   /* 7212 */
        {
            SetString2(pri, (float)tx, (float)ty, 0,
                       r, r, r, s_dbm_fmt_str, pnow->name);             /* 7213 */
            SetString2(pri, (float)(x0 + wlp->max * 0xc - 0x23),
                       (float)ty, 0, r, r, r, s_dbm_fmt_str,
                       *(int *)pnow->child != 0 ? s_dbm_on : s_dbm_off);/* 7214 */
        }
        else if ((pnow->attr & DBM_ATTR_MENU) != 0)                     /* 7215 */
        {
            SetString2(pri, (float)tx, (float)ty, 0,
                       r, r, r, s_dbm_fmt_str, pnow->name);             /* 7216 */
            SetString2(pri, (float)(x0 + wlp->max * 0xc + 1),
                       (float)ty, 0, r, r, r, s_dbm_mark);              /* 7217 */
        }
        else
        {
            SetASCIIString2(pri, (float)tx, (float)ty, 0,
                            r, r, r, pnow->name);                       /* 7219 */
        }

        ty -= 0xe;
        pnow--;
    }

}

/* One frame of the debug menu: recompute the layout metrics, take input, then
 * draw.  Returns non-zero when SELECT asks for the menu to close. */
int DrawDbgMenu(void)                                                   /* 7240 */
{
    DEBUG_MENU *wlp;
    DEBUG_MENU *up;
    DEBUG_SUB_MENU *pnow;
    DEBUG_MENU *(*func)(char *name);
    DEBUG_MENU *next;
    u_int attr;
    int i;
    int len;
    int pos;
    int iv;
    float fv;
    float add;
    int sw;

    wlp = now_tree;                                                     /* 7240 */

    /* off_num lets an owner keep the cursor across visits (nobody uses it in
     * the shipped tables, but the read/write pair is in the ROM). */
    if (wlp->off_num != nullptr)                                        /* 7243 */
    {
        wlp->pos = *wlp->off_num;                                       /* 7244 */
    }

    wlp->kai = 0;                                                       /* 7250 */
    for (up = wlp->parent; up != nullptr; up = up->parent)              /* 7252 */
    {
        wlp->kai++;
    }

    wlp->mnum = 0;                                                      /* 7256 */
    while (strcmp(wlp->submenu[wlp->mnum].name, s_dbm_end) != 0)        /* 7257 */
    {
        wlp->mnum++;                                                    /* 7258 */
    }

    /* Widest row in characters, and, in the same pass, the clamp of every
     * value back inside [nmin, nmax] -- the ROM re-clamps on display so that a
     * value another module wrote out of range snaps back. */
    wlp->max = 0;                                                       /* 7262 */
    for (i = 0; i < wlp->mnum; i++)                                     /* 7263 */
    {
        pnow = &wlp->submenu[i];
        len = (int)strlen(pnow->name);                                  /* 7264 */

        if ((pnow->attr & DBM_ATTR_MENU) != 0)                          /* 7265 */
        {
            len += 5;
        }

        if ((pnow->attr & DBM_ATTR_VALUE) != 0)                         /* 7268 */
        {
            if ((pnow->attr & DBM_ATTR_FLOAT) != 0)                     /* 7269 */
            {
                len += getFStrLength(*(float *)pnow->child) + 4;        /* 7270 */

                fv = *(float *)pnow->child;                             /* 7271 */
                if (fv < pnow->nmin)
                {
                    fv = pnow->nmin;
                }
                *(float *)pnow->child = fv;
                if (fv > pnow->nmax)
                {
                    fv = pnow->nmax;
                }
                *(float *)pnow->child = fv;
            }
            else
            {
                len += 9;                                               /* 7274 */

                iv = *(int *)pnow->child;                               /* 7275 */
                if (iv < (int)pnow->nmin)
                {
                    iv = (int)pnow->nmin;
                }
                if (iv > (int)pnow->nmax)
                {
                    iv = (int)pnow->nmax;
                }
                *(int *)pnow->child = iv;                               /* 7276 */
            }
        }

        if ((pnow->attr & DBM_ATTR_SWITCH) != 0)                        /* 7279 */
        {
            len += 7;                                                   /* 7280 */

            iv = *(int *)pnow->child;                                   /* 7281 */
            if (iv > (int)pnow->nmax)
            {
                iv = (int)pnow->nmax;
            }
            *(int *)pnow->child = iv;
        }

        if (len > wlp->max)                                             /* 7283 */
        {
            wlp->max = len;
        }
    }

    if ((int)strlen(wlp->title) > wlp->max)                             /* 7285 */
    {
        wlp->max = (int)strlen(wlp->title);                             /* 7286 */
    }

    /* DOWN / UP -- paddat[8] and paddat[9]. */
    if (*paddat[8] == 1)                                                /* 7289 */
    {
        pos = wlp->pos + 1;
        if (wlp->pos >= wlp->mnum - 1)
        {
            pos = 0;
        }
        wlp->pos = pos;                                                 /* 7290 */
        if (wlp->off_num != nullptr)                                    /* 7291 */
        {
            *wlp->off_num = pos;                                        /* 7292 */
        }
    }

    if (*paddat[9] == 1)                                                /* 7295 */
    {
        pos = wlp->pos;
        if (pos < 1)
        {
            pos = wlp->mnum;
        }
        wlp->pos = pos - 1;                                             /* 7296 */
        if (wlp->off_num != nullptr)                                    /* 7297 */
        {
            *wlp->off_num = pos - 1;                                    /* 7298 */
        }
    }

    pnow = &wlp->submenu[wlp->pos];                                     /* 7301 */
    attr = pnow->attr;

    /* RIGHT (paddat[10]) increases.  L2 (key_now[9]) forces one step per
     * press instead of the default per-frame repeat; R1 (key_now[10]) triples
     * the step; R2 (key_now[11]) suppresses the edit entirely so that the
     * R2+L2 "zero this value" gesture below does not also nudge it. */
    if (*paddat[10] != 0)                                               /* 7304 */
    {
        if (*key_now[9] == 0 || *paddat[10] == 1)                       /* 7306 */
        {
            add = pnow->nadd;
            if (*key_now[10] != 0)                                      /* 7308 */
            {
                add = add * 3.0f;
            }

            if ((attr & DBM_ATTR_ONESHOT) != 0)                         /* 7310 */
            {
                sw = (*paddat[10] == 1);                                /* 7311 */
            }
            else
            {
                sw = ((attr & DBM_ATTR_EDITABLE) != 0);
            }

            if (sw != 0 && *key_now[11] == 0)                           /* 7328, 7330 */
            {
                if ((attr & DBM_ATTR_SWITCH) != 0)                      /* 7340 */
                {
                    if (*paddat[10] == 1)                               /* 7341 */
                    {
                        if ((attr & DBM_ATTR_FLOAT) != 0)               /* 7342 */
                        {
                            fv = *(float *)pnow->child + add;           /* 7343 */
                            *(float *)pnow->child = fv > pnow->nmax ? 0.0f : fv;
                        }
                        else
                        {
                            iv = *(int *)pnow->child + (int)add;        /* 7345 */
                            *(int *)pnow->child = iv > (int)pnow->nmax ? 0 : iv;
                        }
                    }
                }
                else if ((attr & DBM_ATTR_WRAP) != 0)                   /* 7349 */
                {
                    if ((attr & DBM_ATTR_FLOAT) != 0)                   /* 7350 */
                    {
                        fv = *(float *)pnow->child + add;               /* 7351 */
                        *(float *)pnow->child = fv < pnow->nmax ? fv : pnow->nmin;
                    }
                    else
                    {
                        iv = *(int *)pnow->child + (int)add;            /* 7353 */
                        *(int *)pnow->child =
                            iv < (int)pnow->nmax ? iv : (int)pnow->nmin;
                    }
                }
                else
                {
                    if ((attr & DBM_ATTR_FLOAT) != 0)                   /* 7356 */
                    {
                        fv = *(float *)pnow->child + add;               /* 7357 */
                        *(float *)pnow->child = fv < pnow->nmax ? fv : pnow->nmax;
                    }
                    else
                    {
                        iv = *(int *)pnow->child + (int)add;            /* 7359 */
                        *(int *)pnow->child =
                            iv < (int)pnow->nmax ? iv : (int)pnow->nmax;
                    }
                }

            }
        }
    }

    /* LEFT (paddat[11]) decreases.  Note the one-shot gate also accepts RIGHT
     * here -- that asymmetry with the increase path above is in the ROM, and
     * looks like a copy/paste slip in the original; it is preserved. */
    if (*paddat[11] != 0)                                               /* 7367 */
    {
        if (*key_now[9] == 0 || *paddat[11] == 1)                       /* 7369 */
        {
            add = pnow->nadd;
            if (*key_now[10] != 0)                                      /* 7371 */
            {
                add = add * 3.0f;
            }

            if ((attr & DBM_ATTR_ONESHOT) != 0)                         /* 7373 */
            {
                sw = (*paddat[11] == 1 || *paddat[10] == 1);            /* 7374 */
            }
            else
            {
                sw = ((attr & DBM_ATTR_EDITABLE) != 0);                 /* 7375 */
            }

            if (sw != 0 && *key_now[11] == 0)                           /* 7391, 7393 */
            {
                if ((attr & DBM_ATTR_SWITCH) != 0)                      /* 7404 */
                {
                    if (*paddat[11] == 1)                               /* 7405 */
                    {
                        if ((attr & DBM_ATTR_FLOAT) != 0)               /* 7406 */
                        {
                            fv = *(float *)pnow->child - add;           /* 7407 */
                            *(float *)pnow->child = fv < 0.0f ? pnow->nmax : fv;
                        }
                        else
                        {
                            iv = *(int *)pnow->child - (int)add;        /* 7409 */
                            *(int *)pnow->child = iv < 0 ? (int)pnow->nmax : iv;
                        }
                    }
                }
                else if ((attr & DBM_ATTR_WRAP) != 0)                   /* 7413 */
                {
                    if ((attr & DBM_ATTR_FLOAT) != 0)                   /* 7414 */
                    {
                        fv = *(float *)pnow->child - add;               /* 7415 */
                        *(float *)pnow->child = fv < pnow->nmin ? pnow->nmax : fv;
                    }
                    else
                    {
                        iv = *(int *)pnow->child - (int)add;            /* 7417 */
                        *(int *)pnow->child =
                            iv < (int)pnow->nmin ? (int)pnow->nmax : iv;
                    }
                }
                else
                {
                    if ((attr & DBM_ATTR_FLOAT) != 0)                   /* 7420 */
                    {
                        fv = *(float *)pnow->child - add;               /* 7421 */
                        *(float *)pnow->child = fv < pnow->nmin ? pnow->nmin : fv;
                    }
                    else
                    {
                        iv = *(int *)pnow->child - (int)add;            /* 7423 */
                        *(int *)pnow->child =
                            iv < (int)pnow->nmin ? (int)pnow->nmin : iv;
                    }
                }

            }
        }
    }

    /* R2 + L2 on a numeric row zeroes it. */
    if ((attr & DBM_ATTR_VALUE) != 0 &&
        *key_now[11] != 0 && *key_now[9] != 0)                          /* 7433, 7434 */
    {
        *(int *)pnow->child = 0;                                        /* 7435 */
    }

    if (*paddat[0] == 1)                                                /* 7440 */
    {
        attr = pnow->attr;
        if ((attr & DBM_ATTR_LEAF) != 0)                                /* 7441 */
        {
            /* Confirm on a value row just steps back out. */
            if (wlp->parent != nullptr)                                 /* 7442 */
            {
                now_tree = wlp->parent;                                 /* 7443 */
            }
        }
        else if ((attr & DBM_ATTR_FUNC) != 0)                           /* 7448 */
        {
            func = (DEBUG_MENU *(*)(char *))pnow->child;
            next = func(pnow->name);                                    /* 7451 */
            now_tree = next != nullptr ? next : wlp;                    /* 7452 */
        }
        else
        {
            now_tree = (DEBUG_MENU *)pnow->child;                       /* 7454 */
        }

        /* The ROM tests the root menu's row against "EXIT" and does nothing
         * with the answer -- whatever it used to guard is gone from this
         * build.  The call is kept so the reconstruction stays 1:1. */
        if (wlp->kai == 0)                                              /* 7457 */
        {
            (void)strcmp(pnow->name, s_dbm_exit);
        }
    }

    DrawDbgMenuSub(wlp, 1);                                             /* 7462 */

    if (*paddat[1] == 1)                                                /* 7465 */
    {
        if (wlp->parent != nullptr)
        {
            now_tree = wlp->parent;                                     /* 7466 */
        }
    }

    /* Byte stores, matching the ROM: only the low byte of each RGBAQ word is
     * touched, so the alpha/upper bits of the clear packets survive. */
    ((u_char *)&gdb.clear0.rgbaq.R)[0] = (u_char)s_ivBGColor[0];        /* 7473 */
    ((u_char *)&gdb.clear0.rgbaq.G)[0] = (u_char)s_ivBGColor[1];        /* 7474 */
    ((u_char *)&gdb.clear0.rgbaq.B)[0] = (u_char)s_ivBGColor[2];        /* 7475 */
    ((u_char *)&gdb.clear1.rgbaq.R)[0] = (u_char)s_ivBGColor[0];        /* 7476 */
    ((u_char *)&gdb.clear1.rgbaq.G)[0] = (u_char)s_ivBGColor[1];        /* 7477 */
    ((u_char *)&gdb.clear1.rgbaq.B)[0] = (u_char)s_ivBGColor[2];        /* 7478 */

    return *key_now[0xd] == 1;                                          /* 7482 */
}

/* STUB: DbmSave() really lives in outgame/menu/menu_dat.c (0x1f4160), which
 * has not been reconstructed.  It writes the tuned values of a menu back out
 * as a C header over host0:, so it is a no-op on the PC port anyway.  Parked
 * here until menu_dat.c lands, because MhCtl.c's SAVE row calls it. */
void DbmSave(DEBUG_MENU *in, char *path, char *fname, char *label)
{
    (void)in;
    (void)path;
    (void)fname;
    (void)label;
}
