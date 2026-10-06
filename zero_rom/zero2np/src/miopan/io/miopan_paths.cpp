/* ==========================================================================
 *  miopan_paths.cpp  --  the three roots, resolved once
 *
 *  See miopan_paths.h for the layout and why it is that layout.
 *
 *  This replaces a twenty-candidate probe that ran per file open: the resolver
 *  used to try $MIOPAN_DATA_DIR, the executable's directory and seven of its
 *  ancestors, ".", "..", "../..", "../../..", "data", "../data" and
 *  "../../data", each with two path variants, and took the first that opened.
 *  Three things were wrong with it.  It cost up to sixty failed opens per miss,
 *  on a loader that misses often by design.  It was silent -- a stray bin/ in
 *  any ancestor directory won, and the game read the wrong data with no
 *  diagnostic.  And it could not be pointed anywhere, which is the only thing
 *  that helps on a platform where the data is not near the binary at all.
 *
 *  Now each root is decided once, logged, and joined to directly.  Discovery
 *  still walks the same neighbourhood so a build tree needs no configuration,
 *  but a candidate only wins by holding a sentinel that says what it is, and
 *  the answer is written into miopan.ini so it is visible and editable.
 *
 *  PORT DEVIATION, deliberate, same as libmc.cpp: this file uses
 *  std::filesystem directly rather than going through miopan_file.  It has to
 *  test for directories and create them, and miopan_file is a stream API with
 *  no notion of either.  File existence still goes through SDL so that an
 *  Android APK asset -- which is not a filesystem path at all -- answers.
 * ======================================================================== */

#include "miopan_paths.h"

#include "../miopan_config.h"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <cstdlib>
#include <cstring>

namespace
{

/* Android hands SDL_GetBasePath() an "assets://" URL rather than a path: the
 * shipped resources live inside the APK and only SDL's I/O layer can open
 * them.  Anything that would hand such a string to std::filesystem has to be
 * skipped, so it is worth one predicate. */
bool IsAssetUrl(const char *path)
{
    return path != nullptr && std::strncmp(path, "assets://", 9) == 0;
}

bool IsSlash(char ch)
{
    return ch == '/' || ch == '\\';
}

/* Existence through SDL, so an APK asset and a real file both answer. */
bool FileExists(const std::string &path)
{
    SDL_IOStream *io = SDL_IOFromFile(path.c_str(), "rb");
    if (io == nullptr)
    {
        return false;
    }
    SDL_CloseIO(io);
    return true;
}

bool DirExists(const std::string &path)
{
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

/*
 * One separator, one trailing slash: '/' throughout, exactly one at the end, so
 * every join is a plain concatenation.
 *
 * Forward slashes on Windows too.  Win32 accepts them everywhere, and the
 * alternative is a data_folder line in miopan.ini reading "C:\games\ff2/",
 * which is what mixing SDL_GetBasePath's separator with ours produces.  A path
 * the player is expected to edit should not look like that.
 */
std::string WithTrailingSlash(std::string path)
{
    if (path.empty())
    {
        return "./";
    }
    for (char &ch : path)
    {
        if (ch == '\\')
        {
            ch = '/';
        }
    }
    while (path.size() > 1 && path.back() == '/')
    {
        path.pop_back();
    }
    if (path.back() != '/')
    {
        path += '/';
    }
    return path;
}

/*
 * Normalize a PS2-style name: drop the "device:" prefix, any leading slashes
 * and the ";version" suffix, and turn '\' into '/'.  What comes back is
 * relative to whichever root the caller is about to join it to.
 */
std::string NormalizePs2Path(const char *name)
{
    if (name == nullptr)
    {
        return std::string();
    }

    const char *src = std::strchr(name, ':');
    src = (src != nullptr) ? src + 1 : name;

    while (IsSlash(*src))
    {
        src++;
    }

    std::string out;
    for (; *src != '\0' && *src != ';'; src++)
    {
        out += (*src == '\\') ? '/' : *src;
    }
    return out;
}

/*
 * Reduce a caller-supplied name to a relative path that cannot leave the root
 * it is about to be joined to: no drive letter, no leading separator, no "."
 * or ".." component.  Used for the user directory, where the names come from
 * the ROM ("host0:../bin/data/scene/sceneXX.slt") and must not write outside
 * it.
 */
std::string SanitizeRelative(const char *name)
{
    std::string flat = NormalizePs2Path(name);
    std::string out;
    std::size_t i = 0;

    while (i < flat.size())
    {
        const std::size_t end = flat.find('/', i);
        const std::string part =
            flat.substr(i, end == std::string::npos ? std::string::npos : end - i);

        if (!part.empty() && part != "." && part != "..")
        {
            if (!out.empty())
            {
                out += '/';
            }
            out += part;
        }

        if (end == std::string::npos)
        {
            break;
        }
        i = end + 1;
    }

    return out;
}

/* Drop the last component of a directory path.  Empty when there is nothing
 * left to drop, which is how the ancestor walk terminates. */
std::string ParentOf(const std::string &path)
{
    std::size_t len = path.size();

    while (len > 0 && IsSlash(path[len - 1]))
    {
        len--;
    }
    while (len > 0 && !IsSlash(path[len - 1]))
    {
        len--;
    }
    while (len > 0 && IsSlash(path[len - 1]))
    {
        len--;
    }

    return (len == 0) ? std::string() : path.substr(0, len);
}

/*
 * The neighbourhood a root can be discovered in: the executable's directory
 * and its ancestors, then the working directory and its ancestors.
 *
 * The executable's directory comes first and is the one that does not move.
 * The working directory is anything at all -- a multi-config generator adds a
 * per-configuration level, and an IDE, debugger or launcher sets it to
 * whatever it likes -- so it is a fallback, not a basis.
 */
void AppendSearchRoots(std::vector<std::string> *out)
{
    const char *base = SDL_GetBasePath();

    if (base != nullptr && base[0] != '\0' && !IsAssetUrl(base))
    {
        std::string dir = base;
        for (int level = 0; level < 8 && !dir.empty(); level++)
        {
            out->push_back(dir);
            dir = ParentOf(dir);
        }
    }

    std::string cwd = ".";
    for (int level = 0; level < 4; level++)
    {
        out->push_back(cwd);
        cwd += "/..";
    }
}

/* ---- the user directory ------------------------------------------------- */

/*
 * Carry a build tree's settings and card over the one time the destination is
 * empty.
 *
 * Until this module existed both lived beside the executable, and moving where
 * they are read from without moving the files themselves would present as the
 * player's saves and settings having vanished.  Guarded on the destination
 * being absent, so it runs once and never overwrites anything.
 */
void MigrateLegacyUserData(const std::string &user_dir)
{
    const char *base = SDL_GetBasePath();

    if (base == nullptr || base[0] == '\0' || IsAssetUrl(base))
    {
        return;
    }

    const std::filesystem::path legacy(base);
    const std::filesystem::path target(user_dir);
    std::error_code ec;

    if (std::filesystem::equivalent(legacy, target, ec))
    {
        return;
    }

    struct Item
    {
        const char *name;
        bool        directory;
    };
    static const Item items[] = {
        {"miopan.ini", false},
        {"memcard", true},
    };

    for (const Item &item : items)
    {
        const std::filesystem::path from = legacy / item.name;
        const std::filesystem::path to   = target / item.name;

        if (std::filesystem::exists(to, ec) || !std::filesystem::exists(from, ec))
        {
            continue;
        }

        if (item.directory)
        {
            std::filesystem::copy(from, to,
                                  std::filesystem::copy_options::recursive, ec);
        }
        else
        {
            std::filesystem::copy_file(from, to, ec);
        }

        if (ec)
        {
            SDL_Log("MioPan paths: could not carry %s over from %s: %s",
                    item.name, base, ec.message().c_str());
            ec.clear();
        }
        else
        {
            SDL_Log("MioPan paths: carried %s over from %s", item.name, base);
        }
    }
}

/*
 * Each of the three roots is computed exactly once, by whichever thread asks
 * first, through a function-local static.  That initialization is serialized by
 * the language, and it has to be: the IOP emulation reads game data on its own
 * thread (MyCdRead() -> sceCdRead()), so the root can genuinely be asked for
 * from two threads at the same time.  A hand-rolled "if (!dir.empty())" cache
 * would be a data race here, not merely a redundant computation.
 */
std::string ComputeUserDir()
{
    std::string dir;

    char *pref = SDL_GetPrefPath("Mikompilation", "MioPan");
    if (pref != nullptr && pref[0] != '\0')
    {
        dir = WithTrailingSlash(pref);
        SDL_free(pref);
    }
    else
    {
        if (pref != nullptr)
        {
            SDL_free(pref);
        }
        /* SDL could not name a per-user directory (no HOME, a sandbox with
         * nothing writable).  A relative directory keeps the writers working
         * rather than turning every save into a failure. */
        dir = "MioPan/";
        SDL_Log("MioPan paths: SDL_GetPrefPath failed (%s); writing under %s",
                SDL_GetError(), dir.c_str());
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);

    MigrateLegacyUserData(dir);

    SDL_Log("MioPan paths: user   = %s", dir.c_str());
    return dir;
}

const std::string &UserDir()
{
    static const std::string dir = ComputeUserDir();
    return dir;
}

/* ---- the data root ----------------------------------------------------- */

/* What makes a directory the game's: the disc image, or either spelling of an
 * extracted tree. */
bool LooksLikeDataRoot(const std::string &dir)
{
    const std::string root = WithTrailingSlash(dir);

    return FileExists(root + "IMG_BD.BIN") ||
           DirExists(root + "bin/data") ||
           DirExists(root + "bin2/data");
}

std::string DiscoverDataDir()
{
    std::vector<std::string> candidates;

    AppendSearchRoots(&candidates);

    /* A "data" subdirectory of any of those, which is how the old resolver's
     * "data", "../data" and "../../data" entries were reachable. */
    const std::size_t direct = candidates.size();
    for (std::size_t i = 0; i < direct; i++)
    {
        candidates.push_back(WithTrailingSlash(candidates[i]) + "data");
    }

    for (const std::string &candidate : candidates)
    {
        if (LooksLikeDataRoot(candidate))
        {
            return candidate;
        }
    }

    return std::string();
}

/* Which source the data root came from, for MioPan_PathDataDirSource().  Set by
 * ComputeDataDir() on the way out, so it is written exactly once under the same
 * static-initialization guard that produces the root itself. */
const char *g_data_source = "not found";

std::string ComputeDataDir()
{
    std::string dir;

    /* [paths] data_folder is one of the three sources below, so the file has to
     * have been read before any of them is consulted.  MioPan_ConfigLoad() needs
     * only the user directory, which is resolved independently of this one, so
     * the two cannot deadlock. */
    MioPan_ConfigLoad();

    /*
     * The environment first, so this keeps the precedence the rest of the
     * settings have -- defaults < file < environment -- and a test run against
     * another copy of the data does not have to disturb the file to get it.
     * Deliberately NOT written back for the same reason: an override is a
     * one-off by intent, and persisting it would silently make it permanent.
     */
    const char *env = std::getenv("MIOPAN_DATA_DIR");
    if (env != nullptr && env[0] != '\0')
    {
        dir = WithTrailingSlash(env);
        g_data_source = "MIOPAN_DATA_DIR";
        SDL_Log("MioPan paths: data   = %s  (from MIOPAN_DATA_DIR)", dir.c_str());
        if (!LooksLikeDataRoot(dir))
        {
            SDL_Log("MioPan paths: warning -- no IMG_BD.BIN or bin/data under "
                    "MIOPAN_DATA_DIR; the game will not find its files");
        }
        return dir;
    }

    const char       *configured = miopan_config.paths.data_folder;
    const std::string wanted =
        (configured[0] != '\0') ? WithTrailingSlash(configured) : std::string();

    if (!wanted.empty() && LooksLikeDataRoot(wanted))
    {
        dir = wanted;
        g_data_source = "miopan.ini";
        SDL_Log("MioPan paths: data   = %s  (from miopan.ini)", dir.c_str());
        /* Write the tidied form back, so a hand-typed line acquires its
         * trailing slash and loses its backslashes once rather than looking
         * different from every other path in the file forever.  A no-op when
         * the value is already in that form. */
        MioPan_ConfigSetDataFolder(dir.c_str());
        return dir;
    }

    /*
     * Either nothing is configured, or what is configured no longer holds the
     * game -- so look, and prefer what is found.
     *
     * Falling back rather than failing is the point.  The value in the file is
     * an absolute path that discovery itself wrote, and a build tree that moves
     * or a copy of the game handed to someone else takes every such path with
     * it.  Honouring a stale line over a data folder sitting right beside the
     * executable would turn a rename into "the game stopped working", with the
     * cause a line the player never typed.
     */
    const std::string found = DiscoverDataDir();
    if (!found.empty())
    {
        dir = WithTrailingSlash(found);
        g_data_source = "found automatically";
        if (!wanted.empty())
        {
            SDL_Log("MioPan paths: [paths] data_folder = %s holds no IMG_BD.BIN "
                    "or bin/data; using %s instead", wanted.c_str(),
                    dir.c_str());
        }
        SDL_Log("MioPan paths: data   = %s  (discovered)", dir.c_str());

        /* Record it, so the file says where the game is reading from and the
         * player can repoint it without having to guess the key. */
        MioPan_ConfigSetDataFolder(dir.c_str());
        return dir;
    }

    /* Configured, wrong, and nothing better anywhere.  Keep what was asked for:
     * the loader's diagnostics then name the folder the player chose, which is
     * the thing they can act on. */
    if (!wanted.empty())
    {
        dir = wanted;
        g_data_source = "miopan.ini";
        SDL_Log("MioPan paths: data   = %s  (from miopan.ini)", dir.c_str());
        SDL_Log("MioPan paths: warning -- no IMG_BD.BIN or bin/data there, and "
                "none found near the executable either; the game will not find "
                "its files");
        return dir;
    }

    /* Nothing found.  The executable's directory is the honest guess and gives
     * the loader's diagnostics a sensible path to name. */
    const char *base = SDL_GetBasePath();
    dir = WithTrailingSlash((base != nullptr && base[0] != '\0') ? base : ".");
    SDL_Log("MioPan paths: no IMG_BD.BIN and no bin/data found near the "
            "executable or the working directory.  Set [paths] data_folder in "
            "%smiopan.ini to the folder holding the game's files.",
            UserDir().c_str());
    SDL_Log("MioPan paths: data   = %s  (fallback)", dir.c_str());
    return dir;
}

const std::string &DataDir()
{
    static const std::string dir = ComputeDataDir();
    return dir;
}

/* ---- the assets root --------------------------------------------------- */

std::string ComputeAssetsDir()
{
    std::string dir;

    const char *base = SDL_GetBasePath();

    /* Android: the resources are in the APK and SDL_IOFromFile reads them from
     * a bare relative path.  There is nothing to probe and nothing to log a
     * path for. */
    if (IsAssetUrl(base))
    {
        dir = "";
        SDL_Log("MioPan paths: assets = <APK>");
        return dir;
    }

    std::vector<std::string> candidates;
    AppendSearchRoots(&candidates);

    for (const std::string &candidate : candidates)
    {
        if (DirExists(WithTrailingSlash(candidate) + "resources/shaders"))
        {
            dir = WithTrailingSlash(candidate);
            SDL_Log("MioPan paths: assets = %s", dir.c_str());
            return dir;
        }
    }

    dir = WithTrailingSlash((base != nullptr && base[0] != '\0') ? base : ".");
    SDL_Log("MioPan paths: assets = %s  (no resources/shaders found there -- "
            "shaders and controller mappings will be missing)", dir.c_str());
    return dir;
}

const std::string &AssetsDir()
{
    static const std::string dir = ComputeAssetsDir();
    return dir;
}

/* Copy `value` out, or fail rather than truncate: a silently shortened path is
 * a wrong path. */
int Emit(const std::string &value, char *out, size_t out_size)
{
    if (out == nullptr || out_size == 0)
    {
        return 0;
    }
    if (value.size() + 1 > out_size)
    {
        SDL_Log("MioPan paths: path does not fit %zu bytes: %s", out_size,
                value.c_str());
        out[0] = '\0';
        return 0;
    }
    std::memcpy(out, value.c_str(), value.size() + 1);
    return 1;
}

} // namespace

extern "C" {

const char *MioPan_PathUserDir(void)
{
    return UserDir().c_str();
}

const char *MioPan_PathDataDir(void)
{
    return DataDir().c_str();
}

const char *MioPan_PathAssetsDir(void)
{
    return AssetsDir().c_str();
}

const char *MioPan_PathDataDirSource(void)
{
    DataDir();
    return g_data_source;
}

int MioPan_PathLooksLikeDataDir(const char *dir)
{
    if (dir == nullptr || dir[0] == '\0')
    {
        return 0;
    }
    return LooksLikeDataRoot(dir) ? 1 : 0;
}

int MioPan_PathResolveData(const char *ps2_name, char *out, size_t out_size)
{
    const std::string relative = NormalizePs2Path(ps2_name);

    if (relative.empty())
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    const std::string root = DataDir();

    /*
     * Three spellings of the same file, all against the one root.  The ROM's
     * table is written relative to the ELF's directory, so every game path
     * arrives as "../bin/data/..." -- dropping that "../" is what lets the data
     * sit in the root itself, which is how every install of this port is laid
     * out.  "bin2" is the second extracted-tree spelling in circulation.
     */
    std::string tried[3];
    int         count = 0;

    tried[count++] = root + relative;

    if (relative.compare(0, 3, "../") == 0)
    {
        tried[count++] = root + relative.substr(3);
    }

    {
        const std::string bare =
            (relative.compare(0, 3, "../") == 0) ? relative.substr(3) : relative;

        if (bare.compare(0, 4, "bin/") == 0)
        {
            tried[count++] = root + "bin2/" + bare.substr(4);
        }
    }

    for (int i = 0; i < count; i++)
    {
        if (FileExists(tried[i]))
        {
            return Emit(tried[i], out, out_size);
        }
    }

    if (out != nullptr && out_size != 0)
    {
        out[0] = '\0';
    }
    return 0;
}

int MioPan_PathResolveAsset(const char *relative, char *out, size_t out_size)
{
    if (relative == nullptr || relative[0] == '\0')
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    const std::string path = AssetsDir() + relative;

    if (!FileExists(path))
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    return Emit(path, out, out_size);
}

int MioPan_PathResolveUser(const char *relative, char *out, size_t out_size)
{
    const std::string safe = SanitizeRelative(relative);

    if (safe.empty())
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    return Emit(UserDir() + safe, out, out_size);
}

int MioPan_PathPrepareUser(const char *relative, char *out, size_t out_size)
{
    char path[1024];

    if (MioPan_PathResolveUser(relative, path, sizeof(path)) == 0)
    {
        if (out != nullptr && out_size != 0)
        {
            out[0] = '\0';
        }
        return 0;
    }

    /* Writers do not create their own directories, and the ROM's debug dumpers
     * name subdirectories that have never existed on the host. */
    const std::string full(path);
    const std::size_t slash = full.find_last_of('/');
    if (slash != std::string::npos)
    {
        const std::string parent = full.substr(0, slash);
        std::error_code   ec;

        std::filesystem::create_directories(parent, ec);
        if (ec)
        {
            SDL_Log("MioPan paths: could not create %s: %s", parent.c_str(),
                    ec.message().c_str());
            if (out != nullptr && out_size != 0)
            {
                out[0] = '\0';
            }
            return 0;
        }
    }

    return Emit(full, out, out_size);
}

}
