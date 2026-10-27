/*
 * The overlay's "Assists" menu, and the rows of its "Debug" menu that reach
 * into the game.
 *
 * The Feb 2004 prototype still carries its developers' debug menu
 * (debug/debug_menu.c), reached from SELECT -- which miopan.ini now gates off
 * by default, see MioPan_ConfigDeveloper.  The parts of it a player would want
 * are here instead, and every entry drives exactly what the ROM's own row
 * drives: the same flag, the same int, the same range.  Where a row's limits
 * are data in the ROM -- the item submenus -- they are read off its tables
 * rather than copied, so the two cannot drift apart.
 *
 * Also here, because the ROM has them as one-shot debug calls with no menu row
 * at all: DebugAllClearFlgUp() (menu_top.c's controller-2 switch) and
 * CCameraPowerUp::AllRelease() (EachDebugMain()'s L1+R1+CROSS combo).
 *
 * The menu is drawn from after_super(), on the game thread between one game
 * frame and the next, so a write here is seen by the very next frame and never
 * races one.  The shape follows MikuPan's Cheats menu
 * (src/mikupan/ui/mikupan_ui_cheats.cpp): Inventory, Unlocks, then the rest.
 *
 * The ROM menu's DISP switches and the way into the ROM menu itself are not
 * things a player reaches for, so they go on the Debug menu instead -- drawn
 * from here all the same, because this file is where the game's state is.
 */
#include "miopan_ui_assists.h"

#include "../miopan_config.h"

#include "../../common/variable.h"              /* debug_var                   */
#include "../../debug/camera_menu.h"            /* DebugCameraMenu / DebugFreeCamera */
#include "../../debug/debug_menu.h"             /* dbg_film_item / dbg_event_item... */
#include "../../graphics/obj_draw_ctrl.h"       /* obj_draw_ctrl               */
#include "../../ingame/clear/prg/clear_flg.h"   /* DebugAllClearFlgUp          */
#include "../../ingame/enemy/fene_entry.h"      /* dbg_random_ghost            */
#include "../../ingame/ingame.h"                /* IngameDecideNextPhase / SetIngameDbgMenu */
#include "../../ingame/photo/m_plyr_camera.h"   /* m_plyr_camera / dbg_stock_num */
#include "../../main/gphase.h"                  /* GPhaseIsActive / GPhaseIsSettled */

#include <imgui.h>

#include <algorithm>
#include <cstring>

namespace
{

/* ---------------------------------------------------------------------------
 *  Where the game is.
 *
 *  The ROM's menu was only ever open in GID_STORY_DEBUG: the player in a room
 *  and the world stopped.  The overlay opens anywhere, so each entry first
 *  asks whether the game is somewhere its write means what it meant there.
 * ------------------------------------------------------------------------ */

/* Walking about, paused, or in the ROM's own two debug phases.  Every other
 * phase is left out rather than audited one by one -- the item menu, for one,
 * builds its list once when it opens and would not see a count change. */
bool InPlay()
{
    return GPhaseIsActive(GID_STORY_NORMAL) != 0 ||
           GPhaseIsActive(GID_STORY_PAUSE) != 0 ||
           GPhaseIsActive(GID_STORY_PAUSE_MISSION) != 0 ||
           GPhaseIsActive(GID_STORY_DEBUG) != 0 ||
           GPhaseIsActive(GID_STORY_DEBUG_CAM) != 0;
}

/* A phase change of our own is only safe when nothing else is queued: the
 * overlay runs after every phase function this frame, so SetNextGPhase() here
 * would overwrite a transition the game has just asked for. */
bool CanChangePhase(GPHASE_ID_ENUM from)
{
    return GPhaseIsSettled() != 0 && GPhaseIsActive(from) != 0;
}

void Tooltip(const char *text)
{
    if (text != nullptr &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
    {
        ImGui::SetTooltip("%s", text);
    }
}

/* One of the ROM menu's DBM_ATTR_SWITCH rows: an int it flips between 0 and 1. */
void SwitchItem(const char *label, int *flag, bool enabled,
                const char *tooltip)
{
    if (ImGui::MenuItem(label, nullptr, *flag != 0, enabled))
    {
        *flag = *flag != 0 ? 0 : 1;
    }
    Tooltip(tooltip);
}

/* ---------------------------------------------------------------------------
 *  Inventory
 *
 *  The ROM's item submenus point each row straight at plyr_item[n].have_num
 *  (debug_menu.c's DBM_ITEM), so a non-zero count is "the player now carries
 *  it" and nothing else needs telling.  ItemDbg_PlyrItemLink(), which the
 *  ingame phases call every frame, is an empty 8-byte body in this build, so
 *  writing the counts from here skips no fix-up.
 * ------------------------------------------------------------------------ */

struct RowGloss
{
    const char *rom;
    const char *label;
};

/* English names for the rows a player would look for by name.  The key items
 * keep the ROM's labels: they are the developers' working names, and there
 * are forty-six of them. */
const RowGloss kItemGloss[] = {
    {"CAM_FILM_07", "Type-07 film"},
    {"CAM_FILM_14", "Type-14 film"},
    {"CAM_FILM_61", "Type-61 film"},
    {"CAM_FILM_90", "Type-90 film"},
    {"CAM_FILM_00", "Type-Zero film"},
    {"MANYOUGAN", "Herbal Medicine"},
    {"GOSINSUI", "Sacred Water"},
    {"KAGAMIISI", "Stone Mirror"},
    {"REISEKI", "Spirit Stone"},
};

const char *RowLabel(const DEBUG_SUB_MENU &row)
{
    for (const RowGloss &gloss : kItemGloss)
    {
        if (std::strcmp(gloss.rom, row.name) == 0)
        {
            return gloss.label;
        }
    }
    return row.name;
}

/* The ROM terminates every table with a row named "_end_" and recounts it each
 * frame the menu is drawn (DrawDbgMenu()), so the count is not stored. */
template <typename Fn>
void ForEachValueRow(DEBUG_MENU &menu, Fn fn)
{
    for (int i = 0; i < (int)(sizeof(menu.submenu) / sizeof(menu.submenu[0]));
         i++)
    {
        DEBUG_SUB_MENU &row = menu.submenu[i];
        if (std::strcmp(row.name, "_end_") == 0)
        {
            break;
        }
        /* Every item row is an int DBM_ATTR_VALUE; anything else is not ours
         * to draw. */
        if ((row.attr & DBM_ATTR_VALUE) != 0 &&
            (row.attr & DBM_ATTR_FLOAT) == 0)
        {
            fn(row);
        }
    }
}

/* A 0..1 row is a thing held or not; anything wider is a count.  Both clamp to
 * the row's own limits, which is what DrawDbgMenu() re-applies on display. */
void ItemRow(DEBUG_SUB_MENU &row)
{
    int *value = static_cast<int *>(row.child);
    const int lo = (int)row.nmin;
    const int hi = (int)row.nmax;

    ImGui::PushID(row.child);
    if (hi - lo == 1)
    {
        bool held = *value > lo;
        if (ImGui::Checkbox(RowLabel(row), &held))
        {
            *value = held ? hi : lo;
        }
    }
    else
    {
        int count = std::clamp(*value, lo, hi);
        if (ImGui::SliderInt(RowLabel(row), &count, lo, hi))
        {
            *value = std::clamp(count, lo, hi);
        }
    }
    ImGui::PopID();
}

void FillRows(DEBUG_MENU &menu)
{
    ForEachValueRow(menu, [](DEBUG_SUB_MENU &row) {
        *static_cast<int *>(row.child) = (int)row.nmax;
    });
}

void DrawRows(DEBUG_MENU &menu)
{
    ForEachValueRow(menu, ItemRow);
}

/* The four EVENT ITEM submenus, in the ROM's order. */
DEBUG_MENU *const kKeyItemMenus[] = {
    &dbg_event_item,
    &dbg_event_item2,
    &dbg_event_item3,
    &dbg_event_item4,
};

void DrawInventoryMenu(bool in_play)
{
    if (!ImGui::BeginMenu("Inventory", in_play))
    {
        return;
    }

    ImGui::PushItemWidth(ImGui::GetFontSize() * 10.0f);

    if (ImGui::MenuItem("Fill film and recovery items"))
    {
        FillRows(dbg_film_item);
        FillRows(dbg_recovery_item);
    }
    Tooltip("Every film and recovery row to its ROM debug-menu maximum.");

    ImGui::Separator();
    ImGui::TextDisabled("Film");
    DrawRows(dbg_film_item);

    ImGui::TextDisabled("Recovery");
    DrawRows(dbg_recovery_item);

    ImGui::TextDisabled("Camera");
    {
        /* dbg_menu_main's STOCK_NUM row, 0..5.  CNEquipTrayWrk::Work() copies
         * dbg_stock_num into the tray every frame, which is the only reason the
         * ROM menu could edit it, and the tray writes it back whenever stock
         * moves -- so this reads live. */
        int stock = std::clamp(dbg_stock_num, 0, 5);
        if (ImGui::SliderInt("Special-function stock", &stock, 0, 5))
        {
            dbg_stock_num = std::clamp(stock, 0, 5);
        }
        Tooltip("The charges a fitted special function spends (STOCK_NUM).\n"
                "The tray itself holds 4 at most. 5 is the ROM menu's top\n"
                "value and sits past every '< 5' test the tray makes, so it\n"
                "is never spent and never refilled: unlimited.");
    }

    ImGui::Separator();
    if (ImGui::BeginMenu("Key items"))
    {
        if (ImGui::MenuItem("Give all key items"))
        {
            for (DEBUG_MENU *menu : kKeyItemMenus)
            {
                FillRows(*menu);
            }
        }
        Tooltip("Key items gate story events: carrying one early can skip\n"
                "or confuse a scripted sequence. Save first.");
        ImGui::Separator();
        for (DEBUG_MENU *menu : kKeyItemMenus)
        {
            ImGui::TextDisabled("%s", menu->title);
            DrawRows(*menu);
        }
        ImGui::EndMenu();
    }

    ImGui::PopItemWidth();
    ImGui::EndMenu();
}

/* ---------------------------------------------------------------------------
 *  Unlocks
 * ------------------------------------------------------------------------ */

void DrawUnlocksMenu(bool in_play)
{
    if (!ImGui::BeginMenu("Unlocks", in_play))
    {
        return;
    }

    if (ImGui::MenuItem("Unlock everything"))
    {
        DebugAllClearFlgUp();
    }
    /* In play only, and not just for tidiness: the clear chain it runs also
     * hands this playthrough its rewards -- FileGet()s of five scrap files and
     * five camera upgrade bits -- which belong to a loaded game. */
    Tooltip("DebugAllClearFlgUp(): a clear at every difficulty, the ghost\n"
            "list complete, both endings and both mission awards -- every\n"
            "costume, accessory and difficulty the clear record holds --\n"
            "plus the scrap files and camera upgrades a finished\n"
            "playthrough hands out. The record is merged onto the memory\n"
            "card by your next save. The playthrough's own clear count is\n"
            "left alone, because the story branches on it.");

    ImGui::Separator();
    ImGui::TextDisabled("Camera");
    if (ImGui::MenuItem("All camera upgrades"))
    {
        CCameraPowerUp::AllRelease(&m_plyr_camera.camera_power_up);
    }
    Tooltip("CCameraPowerUp::AllRelease(): every lens, part and addition,\n"
            "and every gem -- the three upgrade gems and the ten\n"
            "sub-function ones. The grades themselves are untouched,\n"
            "exactly as in the ROM: spend the gems in the camera's\n"
            "upgrade menu.");

    ImGui::EndMenu();
}

/* ---------------------------------------------------------------------------
 *  Free camera -- debug/camera_menu.c
 * ------------------------------------------------------------------------ */

/* The ROM reaches GID_STORY_DEBUG_CAM from its debug menu: CAMERA DEBUG raises
 * CameraDebugON and one_Story_Debug() moves the phase on.  From here it is
 * entered straight from normal play.  The GID_STORY_DEBUG step in between has
 * empty init and end functions, so leaving it out changes nothing. */
void EnterFrozenCamera()
{
    DebugCameraMenu.CameraDebugON = 1;
    SetNextGPhase(GID_STORY_DEBUG_CAM);
}

/* The same way out one_Story_Debug() takes when its menu closes, which also
 * honours a pending dbg_menu request: a free camera entered from the ROM's
 * own menu goes back to that menu. */
void LeaveFrozenCamera()
{
    DebugCameraMenu.CameraDebugON = 0;
    SetNextGPhase(IngameDecideNextPhase());
}

void DrawFreeCameraMenu(bool in_play)
{
    if (!ImGui::BeginMenu("Free camera", in_play))
    {
        return;
    }

    DEBUG_FREE_CAMERA *fc = &DebugFreeCamera;
    const bool frozen = GPhaseIsActive(GID_STORY_DEBUG_CAM) != 0;
    const bool can_toggle_frozen = frozen
                                       ? CanChangePhase(GID_STORY_DEBUG_CAM)
                                       : CanChangePhase(GID_STORY_NORMAL);

    if (ImGui::MenuItem("Fly, world frozen", nullptr, frozen,
                        can_toggle_frozen))
    {
        if (frozen)
        {
            LeaveFrozenCamera();
        }
        else
        {
            EnterFrozenCamera();
        }
    }
    Tooltip("The ROM's CAMERA DEBUG: the player and the ghosts stop and the\n"
            "camera is yours. Only from normal play. SELECT (Tab) leaves it\n"
            "too -- into the debug menu when Debug > 'SELECT opens the debug\n"
            "menu' is on, as in the ROM.");

    SwitchItem("Fly, world running", &DebugCameraMenu.FreeCameraON, true,
               "The ROM's FREE CAMERA: the game keeps running and the\n"
               "viewfinder HUD is hidden. The stick that flies the camera still walks Mio,\n"
               "which is the ROM's own gating -- the frozen mode is the one\n"
               "to fly in.");

    SwitchItem("Orbit the player", &DebugCameraMenu.PlayerFollowON, true,
               "PLAYER FOLLOW: circle whoever the game camera follows\n"
               "instead of flying freely.");

    ImGui::Separator();
    ImGui::PushItemWidth(ImGui::GetFontSize() * 10.0f);
    /* dbg_camera_main's own limits for these three rows. */
    ImGui::SliderFloat("Move speed", &fc->fMoveSpeed, 1.0f, 500.0f, "%.0f");
    ImGui::SliderFloat("Turn speed", &fc->fTurnSpeed, 0.005f, 0.5f, "%.3f");
    ImGui::SliderAngle("Field of view", &fc->fFov, 5.73f, 171.89f);
    ImGui::PopItemWidth();
    fc->fMoveSpeed = std::clamp(fc->fMoveSpeed, 1.0f, 500.0f);
    fc->fTurnSpeed = std::clamp(fc->fTurnSpeed, 0.005f, 0.5f);
    fc->fFov = std::clamp(fc->fFov, 0.1f, 3.0f);

    const bool flying =
        DebugCameraMenu.CameraDebugON != 0 || DebugCameraMenu.FreeCameraON != 0;
    if (ImGui::MenuItem("Snap back to the game camera", nullptr, false,
                        flying))
    {
        /* The menu's ACTIVE row: dropping it re-seeds the pose from the live
         * camera on the next frame. */
        fc->bActive = 0;
    }

    ImGui::Separator();
    ImGui::TextDisabled("Look    aim stick        W A S D");
    ImGui::TextDisabled("Move    other stick/pad  arrow keys");
    ImGui::TextDisabled("Up/down R1 / L1          F / R");
    ImGui::TextDisabled("Speed   L2 slow, R2 fast Z / X");

    ImGui::EndMenu();
}

} // namespace

namespace MioPanUi
{

void DrawAssistsMenu()
{
    if (!ImGui::BeginMenu("Assists"))
    {
        return;
    }

    const bool in_play = InPlay();

    if (!in_play)
    {
        ImGui::TextDisabled("Most of these need a game in progress.");
        ImGui::Separator();
    }

    /* debug_var is cleared once, by DebugInit() at boot, and dbg_random_ghost
     * is only ever set by its initialiser; nothing else writes any of these
     * four, so they can be set before a game starts and hold across rooms,
     * deaths and loads. */
    SwitchItem("Invincible", &debug_var.muteki, true,
               "MUTEKI: no damage from anything, and a ghost close by no\n"
               "longer drains spirit power.");
    SwitchItem("Fast movement", &debug_var.hi_spd, true,
               "HIGH SPEED: Mio moves four times as fast, Mayu five.");
    SwitchItem("Freeze ghosts", &debug_var.ene_stop, true,
               "ENE STOP: ghosts stand still and their behaviour stops --\n"
               "they no longer move or act.");
    {
        /* RANDOM ENEMY is on by default -- the switch says whether random
         * encounters happen, so it reads inverted here. */
        bool no_random = dbg_random_ghost == 0;
        if (ImGui::MenuItem("No random ghosts", nullptr, &no_random))
        {
            dbg_random_ghost = no_random ? 0 : 1;
        }
        Tooltip("RANDOM ENEMY off: wandering ghosts stop appearing. Scripted\n"
                "encounters are unaffected.");
    }

    ImGui::Separator();
    DrawInventoryMenu(in_play);
    DrawUnlocksMenu(in_play);

    ImGui::Separator();
    DrawFreeCameraMenu(in_play);

    ImGui::EndMenu();
}

/* ---------------------------------------------------------------------------
 *  Debug-menu rows -- see the header
 * ------------------------------------------------------------------------ */

/* The ROM menu's DISP submenu (dbg_disp_main). */
void DrawSceneLayersMenu()
{
    if (!ImGui::BeginMenu("Scene layers", GPhaseIsActive(GID_STORY_MAIN) != 0))
    {
        return;
    }

    /* The game sets these too (a game over hides everyone but the player,
     * for one), so they read live rather than remembering a choice. */
    SwitchItem("Player", &obj_draw_ctrl.player, true, nullptr);
    SwitchItem("Sister", &obj_draw_ctrl.sister, true, nullptr);
    SwitchItem("Room", &obj_draw_ctrl.room, true, nullptr);
    SwitchItem("Objects", &obj_draw_ctrl.obj, true, nullptr);
    SwitchItem("Sky", &obj_draw_ctrl.sky, true, nullptr);
    SwitchItem("Shadows", &obj_draw_ctrl.shadow, true, nullptr);

    ImGui::EndMenu();
}

void DrawDeveloperItems()
{
    bool select_opens = MioPan_ConfigDebugMenuEnabled() != 0;
    if (ImGui::MenuItem("SELECT opens the debug menu", nullptr,
                        &select_opens))
    {
        MioPan_ConfigSetDebugMenuEnabled(select_opens ? 1 : 0);
    }
    Tooltip("The prototype's own shortcut, off by default because SELECT\n"
            "is Tab and the gamepad's Back button. Saved to miopan.ini\n"
            "as [developer] debug_menu.");

    if (ImGui::MenuItem("Open the debug menu now", nullptr, false,
                        GPhaseIsActive(GID_STORY_NORMAL) != 0))
    {
        /* The request SELECT would have raised; normal play turns it into
         * GID_STORY_DEBUG on its next frame, and SELECT closes the menu. */
        SetIngameDbgMenu(1);
    }
    Tooltip("The developers' menu itself, whatever the setting above says.\n"
            "SELECT closes it.");
}

int GameMoveOperate()
{
    return opt_wrk.move_operate;
}

int GameAnaReplace()
{
    return opt_wrk.ana_replace;
}

} // namespace MioPanUi
