#ifndef MIOPAN_XBOX_ASSETS_H
#define MIOPAN_XBOX_ASSETS_H

#ifdef __cplusplus
#include <string>

extern "C" {
#endif

/*
 * The Xbox Director's Cut extras, imported from an Xbox copy of the game
 * through Options > Special (miopan_rml_options.cpp).
 *
 * The DC added six costume pairs and four accessory pairs to what the Feb 2004
 * prototype carries.  Its own chapter-select debug menu names them: costumes
 * TYPE I..N (swimsuit A, kagerou, doa, swimsuit B..D) after the prototype's
 * TYPE A..H, and accessory sets none / glass / summer / halloween / festival /
 * japan.  Each accessory set is one item per girl, Mio's even and Mayu's odd
 * (i006/i007 are the prototype's glasses), and the prototype's aBoneLabelTbl[]
 * already has a head-bone slot for every item up to i015.
 *
 * The import converts them (miopan_xbox_convert.cpp) into PS2-format files
 * under the user directory's xbox/man/mdl/ and xbox/item/; files placed by hand
 * in the data folder's xbox/ are found too.  Each one has:
 *
 *   * a fixed file number after the ROM's 4491 (no ROM file moves), with a CD
 *     table entry the reconstructed loader serves exactly like a disc file.
 *     The slots exist whether the files do or not, which is what lets an
 *     import take effect without a restart;
 *   * a model number after the ROM's 78 for a costume -- 78 + 2k for Mio and
 *     79 + 2k for Mayu -- or an item number for an accessory: the DC's own
 *     9..15, plus 16 for the sun visor, whose DC number 8 is the prototype's
 *     eye patch.
 *
 * Every per-model table the game indexes by model number (manmdl_dat[],
 * morph_dat[], the character bounding boxes) is sized MIOPAN_MAN_MDL_MAX, and
 * an extra costume borrows Mio's or Mayu's own entries.
 */

#define MIOPAN_ROM_FILE_NUM        0x118b   /* IMG_BD.BIN's 4491 entries        */
#define MIOPAN_ROM_MAN_MDL_NUM     78       /* CH000_MIO_MDL .. MAN_MDL_END - 1  */
#define MIOPAN_ROM_ITEM_NUM        9        /* I000_PLAY_CAMERA_PK2 .. I008      */

#define MIOPAN_XBOX_COSTUME_MAX    6
#define MIOPAN_XBOX_ACCESSORY_MAX  8
#define MIOPAN_XBOX_FILE_MAX       (2 * MIOPAN_XBOX_COSTUME_MAX + MIOPAN_XBOX_ACCESSORY_MAX)

#define MIOPAN_MAN_MDL_MAX         (MIOPAN_ROM_MAN_MDL_NUM + 2 * MIOPAN_XBOX_COSTUME_MAX)
#define MIOPAN_ACCESSORY_NUM_MAX   17       /* aBoneLabelTbl: the ROM's 16 + 16 */

#define MIOPAN_XBOX_GIRL_MIO       0
#define MIOPAN_XBOX_GIRL_MAYU      1

/* Give every extra its slot in the CD table and mark the ones whose files are
 * there.  `tbl` (a CD_DAT_TBL[]), `names` (a FNAME_DAT[]) and `ext` must have
 * room for MIOPAN_ROM_FILE_NUM + MIOPAN_XBOX_FILE_MAX entries, and stay alive:
 * a later import updates them in place.  `path_mdl` / `path_item` are the
 * filename_path[] indices of "../xbox/man/mdl/" and "../xbox/item/".  Returns
 * the file count cddatInit() is handed.  Called once, before cddatInit(). */
int MioPan_XboxAssetsRegister(void *tbl, void *names, char *ext,
                              int path_mdl, int path_item);

/* File number for a model / item number past the ROM's, or -1 when it is not
 * one of the extras or its file is not there. */
int MioPan_XboxModelFileNo(int mdl_no);
int MioPan_XboxItemFileNo(int item_no);

/* 1 when the number is the ROM's own or an extra whose file is there. */
int MioPan_XboxModelUsable(int mdl_no);
int MioPan_XboxItemUsable(int item_no);

/* The model an extra costume borrows its per-model tables from: 0 for Mio's,
 * 1 for Mayu's, the number itself for a ROM model. */
int MioPan_XboxModelBase(int mdl_no);

/* Menus.  Costumes and accessory sets are listed in the DC's own order and
 * only when both girls' files are there. */
int         MioPan_XboxCostumeCount(void);
const char *MioPan_XboxCostumeName(int index);
int         MioPan_XboxCostumeModel(int index, int girl);

int         MioPan_XboxAccessoryCount(void);
const char *MioPan_XboxAccessoryName(int index);
int         MioPan_XboxAccessoryItem(int index, int girl);

#ifdef __cplusplus
}

/* The import, for the options screen. */
namespace MioPanXbox
{

/* Convert the extras out of an Xbox copy of the game on a worker thread.
 * `folder` (UTF-8) may be the game's root -- the one holding default.xbe -- or
 * anything between it and bin/data/3ddata.  Returns false if one is already
 * running. */
bool StartImport(const std::string &folder);
bool ImportRunning();

/* One line for the screen: the file in hand while running, else the last
 * import's outcome, else empty. */
std::string ImportStatus();

/* Game thread, once a frame: once a finished import's files are on disk,
 * re-read which extras are there so the menus offer them straight away. */
void Poll();

} // namespace MioPanXbox
#endif

#endif /* MIOPAN_XBOX_ASSETS_H */
