#include "miopan_rml_menu_status.h"
#include "miopan_rml_menu_art.h"

#include "../../common/variable.h"
#include "../../ingame/item/prg/item.h"
#include "../../ingame/menu/menu.h"
#include "../../ingame/menu/tim_dat/menu_cmn_dat.h"
#include "../../ingame/photo/m_plyr_camera.h"
#include "../../ingame/plyr/player.h"

#include <cstdio>

namespace
{
using MioPanRmlMenuArt::Art;
using MioPanRmlMenuArt::Number;
using MioPanRmlMenuArt::Scale;

/* menu_cmn_dat[], as MenuPlyrDataDisp() and its helpers use it: the plate
 * without and with the lens tray, the portrait and its tail, the four health
 * segments right to left, the five films' icons in film-type order, Type-
 * Zero's word, and each Power-Up Lens's kanji in CAMERA_SUB_FUNC_ENUM order. */
constexpr int kPlate[2][2] = {{0, 1}, {2, 3}};
constexpr int kPortrait[2] = {4, 5};
constexpr int kHealth[4] = {9, 8, 7, 6};
constexpr int kFilmIcon[5] = {10, 11, 12, 13, 14};
constexpr int kFilmZeroWord = 26;
constexpr int kLensIcon[10] = {-1, 28, 27, 30, 35, 31, 33, 29, 32, 34};

/* The number in each film's name, film_num_tex[] in menu_cmn_disp.c. */
constexpr int kFilmNumber[5] = {7, 14, 61, 90, 0};

/* Item numbers: the camera itself.  Film type n is item n; type 0 never runs
 * out and type 4 is Type-Zero. */
constexpr int kItemCamera = 10;
constexpr int kFilmUnlimited = 0;
constexpr int kFilmZero = 4;

const char *kDash = "\xE2\x80\x93";

/* The health bar as MenuPlyrHPDisp() draws it: the four segments right to
 * left, whole while there is health left for them, and the one it runs out
 * inside cut from its left edge -- u and x moved on by what is missing -- so
 * what is left stays flush with the bar's right-hand end. */
std::string Health(const Scale &scale)
{
    int full = 0;
    for (const int segment : kHealth)
    {
        full += menu_cmn_dat[segment].w;
    }
    const int hpmax = plyr_wrk.cmn_wrk.st.hpmax;
    int left = hpmax > 0 ? (int)((float)full * ((float)plyr_wrk.cmn_wrk.st.hp /
                                                (float)hpmax))
                         : 0;
    std::string rml;
    for (const int segment : kHealth)
    {
        const SPRT_DAT &piece = menu_cmn_dat[segment];
        if (left - piece.w >= 0)
        {
            rml += Art(scale, MENU_STATUS_TEX_ADRS, piece);
            left -= piece.w;
            continue;
        }
        if (left > 0)
        {
            SPRT_DAT cut = piece;
            cut.x = piece.x + piece.w - left;
            cut.u = (u_short)(piece.u + piece.w - left);
            cut.w = (u_short)left;
            rml += Art(scale, MENU_STATUS_TEX_ADRS, cut);
        }
        break;
    }
    return rml;
}

/* The fitted lenses, as MenuCmnPlyrEquipReinforcedLensDisp() lays them out:
 * three slots 37 apart for the kanji and 39 for the level with the tray part
 * fitted, one further right without it; each kanji in its lens's colour, an
 * empty slot a dash. */
std::string Lenses(const Scale &scale, bool tray)
{
    char fitted[3] = {0, 0, 0};
    m_plyr_camera.eq_tray.GetSubFuncArray(fitted);
    std::string rml;
    for (int i = 0; i < (tray ? 3 : 1); i++)
    {
        const int icon_x = tray ? 9 + i * 37 : 75;
        const int icon_y = tray ? 15 : 16;
        const int level_x = tray ? 31 + i * 39 : 125;
        const int level_y = tray ? 45 : 43;
        const int lens = fitted[i];
        if (lens <= 0 || lens >= 10 || kLensIcon[lens] < 0)
        {
            rml += Number(scale, kDash, level_x, level_y, 10, "plate-num");
            continue;
        }
        SPRT_DAT icon = menu_cmn_dat[kLensIcon[lens]];
        icon.x = icon_x;
        icon.y = icon_y;
        const NEQUIP_FUNC_DAT &tint = CNEquipTrayWrk::equip_func_tbl[lens];
        rml += Art(scale, MENU_BG_TEX_ADRS, icon, 0, 0, tint.r, tint.g, tint.b);
        rml += Number(scale, std::to_string(m_plyr_camera.eq_tray.mSave.mSubFuncLv[lens].Get()),
                      level_x, level_y, 10, "plate-num");
    }
    return rml;
}

/* The film loaded and how many are left, as MenuFilmDisp() prints them: the
 * film's number after the plate's own "Type-", the count after its "x"; the
 * unlimited film's count and a missing camera's whole readout are dashes. */
std::string Film(const Scale &scale)
{
    const std::string dashes = std::string(kDash) + kDash;
    if (GetPlyrItemHaveNum(kItemCamera) <= 0)
    {
        return Number(scale, dashes, 216, 41, 20, "plate-num") +
               Number(scale, dashes, 253, 41, 20, "plate-num");
    }
    const int type = GetPlyrEquipmentFilmType();
    if (type < 0 || type > 4)
    {
        return std::string();
    }
    std::string rml = Art(scale, MENU_STATUS_TEX_ADRS, menu_cmn_dat[kFilmIcon[type]]);
    if (type == kFilmZero)
    {
        rml += Art(scale, MENU_STATUS_TEX_ADRS, menu_cmn_dat[kFilmZeroWord]);
    }
    else
    {
        char number[8];
        std::snprintf(number, sizeof(number), "%02d", kFilmNumber[type]);
        rml += Number(scale, number, 216, 41, 20, "plate-num");
    }
    rml += Number(scale, type == kFilmUnlimited ? dashes
                                         : std::to_string(GetPlyrItemHaveNum(type)),
                  253, 41, 20, "plate-num count");
    return rml;
}
}

namespace MioPanRmlMenuStatus
{
std::string Rml(const MioPanRmlMenuArt::Scale &scale)
{
    /* Drawn in MenuPlyrDataDisp()'s order: the bar under the plate's
     * window, the plate, the portrait, then what is printed on it. */
    const bool tray = m_plyr_camera.camera_power_up.mCamPartsSetFlg.IsUp(1) != 0;
    std::string rml = Health(scale);
    for (const int piece : kPlate[tray ? 1 : 0])
    {
        rml += Art(scale, MENU_STATUS_TEX_ADRS, menu_cmn_dat[piece]);
    }
    for (const int piece : kPortrait)
    {
        rml += Art(scale, MENU_STATUS_TEX_ADRS, menu_cmn_dat[piece]);
    }
    rml += Lenses(scale, tray);
    rml += Film(scale);
    return rml;
}
}
