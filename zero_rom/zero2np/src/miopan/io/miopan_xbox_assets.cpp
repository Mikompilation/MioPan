/* ==========================================================================
 *  miopan_xbox_assets.cpp  --  the Director's Cut extras: where they come
 *  from, and the file / model / item numbers the port gives them
 *
 *  See miopan_xbox_assets.h for the numbering and why it is that numbering.
 *
 *  Nothing here knows the formats.  The import hands each Xbox file to
 *  miopan_xbox_convert.cpp and writes what comes back; once a file is in
 *  xbox/ it is a PS2 .mdl or .pk2, and the reconstructed loader reads it
 *  through the same CD table entry a disc file has.
 *
 *  Threads: the CD table, the menus' lists and Poll() belong to the game
 *  thread.  The import's worker only reads the Xbox folder and writes into the
 *  user directory, and reports through g_job under its mutex.
 * ======================================================================== */

#include "miopan_xbox_assets.h"

#include "miopan_file.h"
#include "miopan_paths.h"
#include "miopan_xbox_convert.h"

#include "../../system/eeiop/cddat.h"   /* CD_DAT_TBL / FNAME_DAT */

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace
{

/* GetFileExtLabel() values the ROM's table carries for these two kinds. */
constexpr char kExtMdl = 6;
constexpr char kExtPk2 = 2;

struct Costume
{
    const char *name;
    const char *file[2];        /* Mio, Mayu: the Xbox names, lower-cased */
};

/* TYPE I..N, after the prototype's TYPE A..H. */
const Costume kCostumes[MIOPAN_XBOX_COSTUME_MAX] = {
    { "Swimsuit A", { "ch000_mizugia.mdl", "ch001_mizugia.mdl" } },
    { "Kagerou",    { "ch000_kagerou.mdl", "ch001_kagerou.mdl" } },
    { "DOA",        { "ch000_doa.mdl",     "ch001_doa.mdl"     } },
    { "Swimsuit B", { "ch000_mizugib.mdl", "ch001_mizugib.mdl" } },
    { "Swimsuit C", { "ch000_mizugic.mdl", "ch001_mizugic.mdl" } },
    { "Swimsuit D", { "ch000_mizugid.mdl", "ch001_mizugid.mdl" } },
};

struct Accessory
{
    const char *name;
    const char *file[2];
    int         item[2];
};

/* The DC's four sets after "glass", which is the prototype's own i006/i007. */
const Accessory kAccessories[MIOPAN_XBOX_ACCESSORY_MAX / 2] = {
    { "Summer",    { "i008_sunvisor.pk2", "i009_mugiwara.pk2" }, { 16,  9 } },
    { "Halloween", { "i010_tongari.pk2",  "i011_pumpkin.pk2"  }, { 10, 11 } },
    { "Festival",  { "i012_kitunea.pk2",  "i013_kituneb.pk2"  }, { 12, 13 } },
    { "Japan",     { "i014_kabuto.pk2",   "i015_hatimaki.pk2" }, { 14, 15 } },
};

/* Every extra has a fixed slot, MIOPAN_ROM_FILE_NUM + slot being its file
 * number: the costumes' twelve, Mio's then Mayu's, then the accessories'. */
int CostumeSlot(int k, int girl)
{
    return 2 * k + girl;
}

int AccessorySlot(int k, int girl)
{
    return 2 * MIOPAN_XBOX_COSTUME_MAX + 2 * k + girl;
}

struct SlotInfo
{
    const char *file;
    const char *dir;            /* under xbox/ */
    const char *xbox_dir;       /* under the Xbox 3ddata/ */
    char        ext;
};

SlotInfo Slot(int slot)
{
    if (slot < 2 * MIOPAN_XBOX_COSTUME_MAX)
    {
        return {kCostumes[slot / 2].file[slot & 1], "man/mdl", "character", kExtMdl};
    }
    slot -= 2 * MIOPAN_XBOX_COSTUME_MAX;
    return {kAccessories[slot / 2].file[slot & 1], "item", "item", kExtPk2};
}

struct Table
{
    CD_DAT_TBL *tbl = nullptr;
    FNAME_DAT  *names = nullptr;
    char       *ext = nullptr;
    int         path_mdl = 0;
    int         path_item = 0;
};

Table g_table;
bool g_present[MIOPAN_XBOX_FILE_MAX];

/* Menu lists: indices into kCostumes / kAccessories whose files are all here. */
int g_costumes[MIOPAN_XBOX_COSTUME_MAX];
int g_costume_count;
int g_accessories[MIOPAN_XBOX_ACCESSORY_MAX / 2];
int g_accessory_count;

/* Mark one slot present or not.  The path is spelled exactly as GetFileName()
 * will build it, so the loader's own lookup finds the same file this probe
 * did. */
void Probe(int slot)
{
    const SlotInfo info = Slot(slot);
    CD_DAT_TBL &t = g_table.tbl[MIOPAN_ROM_FILE_NUM + slot];
    char ps2_name[256];
    char host[1024];
    int64_t size = 0;

    std::snprintf(ps2_name, sizeof(ps2_name), "host0:../xbox/%s/%s", info.dir, info.file);
    if (MioPan_PathResolveData(ps2_name, host, sizeof(host)) != 0)
    {
        size = MioPan_FileSize(host);
        if (size <= 0 || size > 0x7fffffff)
        {
            SDL_Log("[port] xbox: %s has an unusable size (%lld), skipped", host,
                    (long long)size);
            size = 0;
        }
    }

    g_present[slot] = size > 0;
    t.cmp_flg = 0;
    t.exist_flg = g_present[slot] ? 1 : 0;
    t.start_sector = 0;         /* never read: the loader serves it by name */
    t.size = (int)size;
    t.cmp_size = (int)size;
}

void Scan()
{
    for (int slot = 0; slot < MIOPAN_XBOX_FILE_MAX; slot++)
    {
        Probe(slot);
    }

    g_costume_count = 0;
    for (int k = 0; k < MIOPAN_XBOX_COSTUME_MAX; k++)
    {
        if (g_present[CostumeSlot(k, 0)] && g_present[CostumeSlot(k, 1)])
        {
            g_costumes[g_costume_count++] = k;
        }
    }
    g_accessory_count = 0;
    for (int k = 0; k < MIOPAN_XBOX_ACCESSORY_MAX / 2; k++)
    {
        if (g_present[AccessorySlot(k, 0)] && g_present[AccessorySlot(k, 1)])
        {
            g_accessories[g_accessory_count++] = k;
        }
    }
}

int ItemSlot(int item_no)
{
    for (int k = 0; k < MIOPAN_XBOX_ACCESSORY_MAX / 2; k++)
    {
        for (int girl = 0; girl < 2; girl++)
        {
            if (kAccessories[k].item[girl] == item_no)
            {
                return AccessorySlot(k, girl);
            }
        }
    }
    return -1;
}

/* ---------------------------------------------------------------------------
 *  The import
 * ------------------------------------------------------------------------ */

std::string Lower(std::string s)
{
    for (char &c : s)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return s;
}

bool IsDirectory(const std::string &path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

std::string Join(const std::string &dir, const std::string &name)
{
    if (dir.empty() || dir.back() == '/' || dir.back() == '\\')
    {
        return dir + name;
    }
    return dir + "/" + name;
}

struct Listing
{
    std::vector<std::string> names;
};

SDL_EnumerationResult SDLCALL CollectName(void *userdata, const char *dirname,
                                          const char *fname)
{
    (void)dirname;
    static_cast<Listing *>(userdata)->names.emplace_back(fname);
    return SDL_ENUM_CONTINUE;
}

std::vector<std::string> List(const std::string &dir)
{
    Listing listing;
    SDL_EnumerateDirectory(dir.c_str(), CollectName, &listing);
    return listing.names;
}

/* A child of `dir` by name, ignoring case: the Xbox disc mixes it
 * (ch000_mizugiA.mdl beside ch000_MIO.ppd), and only some hosts do not care. */
std::string Child(const std::string &dir, const std::string &name)
{
    const std::string want = Lower(name);
    for (const std::string &n : List(dir))
    {
        if (Lower(n) == want)
        {
            return Join(dir, n);
        }
    }
    return std::string();
}

bool IsXboxData(const std::string &dir)
{
    const std::string character = Child(dir, "character");
    const std::string item = Child(dir, "item");
    return !character.empty() && !item.empty() && IsDirectory(character) &&
           IsDirectory(item);
}

/* The 3ddata folder the player meant.  Anything from the game's root down is
 * accepted -- the root itself, bin, data, 3ddata, or character / item inside
 * it -- and a dumped disc is often one folder deeper than it looks, so the
 * search goes a few levels down before it gives up. */
std::string Find3dData(std::string chosen)
{
    while (chosen.size() > 1 && (chosen.back() == '/' || chosen.back() == '\\'))
    {
        chosen.pop_back();
    }
    if (!IsDirectory(chosen))
    {
        return std::string();
    }

    const size_t cut = chosen.find_last_of("/\\");
    if (cut != std::string::npos)
    {
        const std::string leaf = Lower(chosen.substr(cut + 1));
        if (leaf == "character" || leaf == "item" || leaf == "special")
        {
            const std::string parent = chosen.substr(0, cut);
            if (IsXboxData(parent))
            {
                return parent;
            }
        }
    }

    std::deque<std::pair<std::string, int>> queue = {{chosen, 0}};
    int visited = 0;
    while (!queue.empty() && visited < 400)
    {
        const auto [dir, depth] = queue.front();
        queue.pop_front();
        visited++;
        if (IsXboxData(dir))
        {
            return dir;
        }
        if (depth >= 5)
        {
            continue;
        }
        for (const std::string &n : List(dir))
        {
            const std::string sub = Join(dir, n);
            if (IsDirectory(sub))
            {
                queue.emplace_back(sub, depth + 1);
            }
        }
    }
    return std::string();
}

bool Load(const std::string &path, MioPanXboxConvert::Bytes &out)
{
    size_t size = 0;
    void *data = SDL_LoadFile(path.c_str(), &size);
    if (data == nullptr)
    {
        return false;
    }
    const uint8_t *bytes = static_cast<const uint8_t *>(data);
    out.assign(bytes, bytes + size);
    SDL_free(data);
    return true;
}

/* Into the user directory, through a temporary file, so a half-written file
 * is never where the loader looks. */
bool Save(const SlotInfo &info, const MioPanXboxConvert::Bytes &data, std::string &error)
{
    char relative[256];
    char path[1024];
    std::snprintf(relative, sizeof(relative), "xbox/%s/%s", info.dir, info.file);
    if (MioPan_PathPrepareUser(relative, path, sizeof(path)) == 0)
    {
        error = "could not create a folder for it";
        return false;
    }
    const std::string part = std::string(path) + ".part";
    if (!SDL_SaveFile(part.c_str(), data.data(), data.size()) ||
        !SDL_RenamePath(part.c_str(), path))
    {
        error = SDL_GetError();
        SDL_RemovePath(part.c_str());
        return false;
    }
    return true;
}

/* The worker is detached: it is never waited for, so quitting mid-import just
 * leaves a .part file behind, and there is no joinable thread for a static
 * destructor to trip over. */
struct Job
{
    std::mutex  mutex;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::string status;         /* guarded by mutex */
    int converted = 0;
    int missing = 0;
    int failed = 0;
    bool no_folder = false;
};

Job g_job;

void SetStatus(const std::string &text)
{
    std::lock_guard<std::mutex> lock(g_job.mutex);
    g_job.status = text;
}

void ImportThread(std::string chosen)
{
    SetStatus("Looking for the game's files...");
    const std::string root = Find3dData(chosen);
    if (root.empty())
    {
        SDL_Log("[port] xbox: no bin/data/3ddata under %s", chosen.c_str());
        std::lock_guard<std::mutex> lock(g_job.mutex);
        g_job.no_folder = true;
        g_job.status = "That folder does not hold the Xbox game. Choose the one "
                       "with default.xbe in it.";
        g_job.finished = true;
        return;
    }
    SDL_Log("[port] xbox: importing from %s", root.c_str());

    for (int slot = 0; slot < MIOPAN_XBOX_FILE_MAX; slot++)
    {
        const SlotInfo info = Slot(slot);
        {
            char text[160];
            std::snprintf(text, sizeof(text), "Converting %s (%d of %d)...", info.file,
                          slot + 1, MIOPAN_XBOX_FILE_MAX);
            SetStatus(text);
        }

        const std::string dir = Child(root, info.xbox_dir);
        const std::string src = dir.empty() ? std::string() : Child(dir, info.file);
        std::string stem = info.file;
        stem = stem.substr(0, stem.find_last_of('.')) + ".ppd";
        const std::string ppd = dir.empty() ? std::string() : Child(dir, stem);

        MioPanXboxConvert::Bytes a;
        MioPanXboxConvert::Bytes b;
        if (src.empty() || ppd.empty() || !Load(src, a) || !Load(ppd, b))
        {
            SDL_Log("[port] xbox: %s or its .ppd is missing", info.file);
            std::lock_guard<std::mutex> lock(g_job.mutex);
            g_job.missing++;
            continue;
        }

        const MioPanXboxConvert::Result r = info.ext == kExtMdl
                                                ? MioPanXboxConvert::Character(a, b)
                                                : MioPanXboxConvert::Item(a, b);
        std::string error = r.error;
        if (error.empty() && Save(info, r.data, error))
        {
            SDL_Log("[port] xbox: %s -> %zu bytes (%d units, %d textures, %d VRAM "
                    "blocks, %d strips in %d batches)",
                    info.file, r.data.size(), r.units, r.textures, r.vram_blocks,
                    r.strips, r.batches);
            std::lock_guard<std::mutex> lock(g_job.mutex);
            g_job.converted++;
        }
        else
        {
            SDL_Log("[port] xbox: %s failed: %s", info.file, error.c_str());
            std::lock_guard<std::mutex> lock(g_job.mutex);
            g_job.failed++;
        }
    }
    g_job.finished = true;
}

} // namespace

extern "C" int MioPan_XboxAssetsRegister(void *tbl, void *names, char *ext,
                                         int path_mdl, int path_item)
{
    g_table.tbl = (CD_DAT_TBL *)tbl;
    g_table.names = (FNAME_DAT *)names;
    g_table.ext = ext;
    g_table.path_mdl = path_mdl;
    g_table.path_item = path_item;

    /* Every slot gets its name now, present or not, so the table never has to
     * grow: an import only flips exist_flg and the sizes. */
    for (int slot = 0; slot < MIOPAN_XBOX_FILE_MAX; slot++)
    {
        const SlotInfo info = Slot(slot);
        const int file_no = MIOPAN_ROM_FILE_NUM + slot;

        g_table.names[file_no].cmp_flg = 0;
        g_table.names[file_no].path_no = (char)(info.ext == kExtMdl ? path_mdl : path_item);
        g_table.names[file_no].name = const_cast<char *>(info.file);
        g_table.ext[file_no] = info.ext;
    }
    Scan();

    int present = 0;
    for (bool p : g_present)
    {
        present += p ? 1 : 0;
    }
    if (present != 0)
    {
        SDL_Log("[port] xbox: %d converted files, %d costume(s) and %d "
                "accessory set(s) available",
                present, g_costume_count, g_accessory_count);
    }
    return MIOPAN_ROM_FILE_NUM + MIOPAN_XBOX_FILE_MAX;
}

extern "C" int MioPan_XboxModelFileNo(int mdl_no)
{
    const int slot = mdl_no - MIOPAN_ROM_MAN_MDL_NUM;
    if (slot < 0 || slot >= 2 * MIOPAN_XBOX_COSTUME_MAX || !g_present[slot])
    {
        return -1;
    }
    return MIOPAN_ROM_FILE_NUM + slot;
}

extern "C" int MioPan_XboxItemFileNo(int item_no)
{
    if (item_no < MIOPAN_ROM_ITEM_NUM)
    {
        return -1;
    }
    const int slot = ItemSlot(item_no);
    if (slot < 0 || !g_present[slot])
    {
        return -1;
    }
    return MIOPAN_ROM_FILE_NUM + slot;
}

extern "C" int MioPan_XboxModelUsable(int mdl_no)
{
    if (mdl_no >= 0 && mdl_no < MIOPAN_ROM_MAN_MDL_NUM)
    {
        return 1;
    }
    return MioPan_XboxModelFileNo(mdl_no) >= 0;
}

extern "C" int MioPan_XboxItemUsable(int item_no)
{
    if (item_no >= 0 && item_no < MIOPAN_ROM_ITEM_NUM)
    {
        return 1;
    }
    return MioPan_XboxItemFileNo(item_no) >= 0;
}

extern "C" int MioPan_XboxModelBase(int mdl_no)
{
    if (mdl_no < MIOPAN_ROM_MAN_MDL_NUM || mdl_no >= MIOPAN_MAN_MDL_MAX)
    {
        return mdl_no;
    }
    return (mdl_no - MIOPAN_ROM_MAN_MDL_NUM) & 1;
}

extern "C" int MioPan_XboxCostumeCount(void)
{
    return g_costume_count;
}

extern "C" const char *MioPan_XboxCostumeName(int index)
{
    if (index < 0 || index >= g_costume_count)
    {
        return "";
    }
    return kCostumes[g_costumes[index]].name;
}

extern "C" int MioPan_XboxCostumeModel(int index, int girl)
{
    if (index < 0 || index >= g_costume_count || (girl != 0 && girl != 1))
    {
        return girl == MIOPAN_XBOX_GIRL_MAYU ? 1 : 0;
    }
    return MIOPAN_ROM_MAN_MDL_NUM + CostumeSlot(g_costumes[index], girl);
}

extern "C" int MioPan_XboxAccessoryCount(void)
{
    return g_accessory_count;
}

extern "C" const char *MioPan_XboxAccessoryName(int index)
{
    if (index < 0 || index >= g_accessory_count)
    {
        return "";
    }
    return kAccessories[g_accessories[index]].name;
}

extern "C" int MioPan_XboxAccessoryItem(int index, int girl)
{
    if (index < 0 || index >= g_accessory_count || (girl != 0 && girl != 1))
    {
        return -1;
    }
    return kAccessories[g_accessories[index]].item[girl];
}

namespace MioPanXbox
{

bool StartImport(const std::string &folder)
{
    if (g_job.running.load())
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_job.mutex);
        g_job.status.clear();
        g_job.converted = 0;
        g_job.missing = 0;
        g_job.failed = 0;
        g_job.no_folder = false;
    }
    g_job.finished = false;
    g_job.running = true;
    std::thread(ImportThread, folder).detach();
    return true;
}

bool ImportRunning()
{
    return g_job.running.load();
}

std::string ImportStatus()
{
    std::lock_guard<std::mutex> lock(g_job.mutex);
    return g_job.status;
}

void Poll()
{
    if (!g_job.finished.exchange(false))
    {
        return;
    }
    g_job.running = false;
    if (g_table.tbl != nullptr)
    {
        Scan();
    }

    std::lock_guard<std::mutex> lock(g_job.mutex);
    if (g_job.no_folder)
    {
        return;                 /* the worker's own message stands */
    }
    char text[256];
    if (g_job.converted == 0)
    {
        std::snprintf(text, sizeof(text),
                      "Nothing was imported: none of the Director's Cut's extra "
                      "costumes are in that copy of the game.");
    }
    else
    {
        std::snprintf(text, sizeof(text),
                      "Imported %d costume%s and %d accessory set%s.%s",
                      g_costume_count, g_costume_count == 1 ? "" : "s",
                      g_accessory_count, g_accessory_count == 1 ? "" : "s",
                      g_job.failed + g_job.missing != 0
                          ? " Some files could not be converted; the log says which."
                          : "");
    }
    g_job.status = text;
    SDL_Log("[port] xbox: import done -- %d converted, %d missing, %d failed",
            g_job.converted, g_job.missing, g_job.failed);
}

} // namespace MioPanXbox
