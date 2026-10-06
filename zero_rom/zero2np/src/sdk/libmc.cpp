/* ==========================================================================
 *  libmc.cpp  (SCE PS2 memory-card library -- PC-port shim)
 *
 *  Backs the eleven libmc entry points system/mc uses with a directory on the
 *  host: <user dir>/memcard/mc<port>-<slot>/, the user directory being the one
 *  miopan/io/miopan_paths names.  The card is a filesystem, so the honest port
 *  of it is a filesystem rather than a "no card inserted" stub -- with the
 *  latter the whole system/mc folder would be unreachable code.
 *
 *  PORT DEVIATION, deliberate: this file talks to std::filesystem directly
 *  rather than going through miopan/io/miopan_file.  That wrapper is a stream
 *  API and has no notion of directories, creation, enumeration or deletion --
 *  all four of which libmc is.  Growing it a directory API for one consumer
 *  would be worse than this note.
 *
 *  The asynchronous handshake is preserved, because every system/mc module is
 *  a step machine built around it: a request does its work immediately, parks
 *  the result, and the next sceMcSync() hands that result back once.  A second
 *  sync with nothing parked answers sceMcExecIdle, exactly as the real library
 *  does, which is the path those step machines use to re-issue a lost request.
 *
 *  Two synthetic facts the game depends on:
 *    - Every directory listing starts with "." and "..".  MC_DIR_INFO's users
 *      hardcode that (MemoryCardAllFileDelInit starts at entry 2, and
 *      GetMemoryCardCheckDirSize() skips two entries before summing sizes).
 *    - A formatted 8 MB card holds 8135 usable 1 KB clusters.  Free space is
 *      that minus what the card directory actually contains, so
 *      MemoryCardCheckEmpty() answers something meaningful.
 * ======================================================================== */

#include "libmc.h"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <cstdio>
#include <cstring>

#include "../miopan/io/miopan_paths.h"

namespace
{

/* A formatted 8 MB card: 8135 clusters of 1024 bytes available to the user. */
const int MC_CLUSTER_SIZE  = 1024;
const int MC_FREE_CLUSTERS = 8135;

const int MC_MAX_OPEN_FILES = 8;

struct McFile
{
    std::FILE  *fp;
    std::string path;
};

McFile open_files[MC_MAX_OPEN_FILES];

int  init_done;
int  sync_pending;
int  sync_result;

/* ---- paths ------------------------------------------------------------- */

std::filesystem::path CardRoot(int port, int slot)
{
    char leaf[64];
    char path[1024];

    std::snprintf(leaf, sizeof(leaf), "memcard/mc%d-%d", port, slot);

    if (MioPan_PathPrepareUser(leaf, path, sizeof(path)) == 0)
    {
        return std::filesystem::path(".") / leaf;
    }

    return std::filesystem::path(path);
}

/* The game hands over names in three shapes: "DIR", "/DIR/FILE" and the
 * wildcard "DIR/*" that sceMcGetDir() takes.  Reduce all three to a relative
 * path under the card root. */
std::string NormalizeName(const char *name)
{
    std::string s(name != 0 ? name : "");
    std::size_t n;

    while (!s.empty() && (s[0] == '/' || s[0] == '\\'))
    {
        s.erase(0, 1);
    }

    /* Trailing "/*" or "*" is the listing wildcard, not part of the path. */
    n = s.size();
    if (n >= 1 && s[n - 1] == '*')
    {
        s.erase(n - 1);
        n = s.size();
        if (n >= 1 && (s[n - 1] == '/' || s[n - 1] == '\\'))
        {
            s.erase(n - 1);
        }
    }

    return s;
}

int PostResult(int result)
{
    sync_result  = result;
    sync_pending = 1;
    return 0;
}

/* ---- handles ----------------------------------------------------------- */

int AllocHandle(std::FILE *fp, const std::string &path)
{
    int i;

    for (i = 0; i < MC_MAX_OPEN_FILES; i++)
    {
        if (open_files[i].fp == 0)
        {
            open_files[i].fp   = fp;
            open_files[i].path = path;
            return i;
        }
    }

    return -1;
}

McFile *GetHandle(int fd)
{
    if (fd < 0 || fd >= MC_MAX_OPEN_FILES || open_files[fd].fp == 0)
    {
        return 0;
    }

    return &open_files[fd];
}

/* ---- space ------------------------------------------------------------- */

int Clusters(std::uintmax_t bytes)
{
    return (int)((bytes + (MC_CLUSTER_SIZE - 1)) / MC_CLUSTER_SIZE);
}

int UsedClusters(const std::filesystem::path &root)
{
    std::error_code ec;
    int             used = 0;

    if (!std::filesystem::exists(root, ec))
    {
        return 0;
    }

    for (std::filesystem::recursive_directory_iterator it(root, ec), end;
         it != end && !ec; it.increment(ec))
    {
        if (it->is_directory(ec))
        {
            /* A card directory costs one cluster for its own entry table. */
            used += 1;
        }
        else
        {
            used += Clusters(it->file_size(ec));
        }
    }

    return used;
}

} /* namespace */

extern "C" {

int sceMcInit(void)
{
    int i;

    for (i = 0; i < MC_MAX_OPEN_FILES; i++)
    {
        if (open_files[i].fp != 0)
        {
            std::fclose(open_files[i].fp);
            open_files[i].fp = 0;
        }
    }

    sync_pending = 0;
    sync_result  = sceMcResSucceed;
    init_done    = 1;
    return 0;
}

int sceMcSync(int mode, int *cmd, int *result)
{
    (void)mode;   /* the host has no request that can still be in flight */

    if (cmd != 0)
    {
        *cmd = 0;
    }

    if (sync_pending == 0)
    {
        return sceMcExecIdle;
    }

    if (result != 0)
    {
        *result = sync_result;
    }

    sync_pending = 0;
    return sceMcExecFinish;
}

int sceMcGetInfo(int port, int slot, int *type, int *free, int *format)
{
    std::filesystem::path root;
    std::error_code       ec;
    int                   used;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }
    if (port < 0 || port > 1 || slot < 0 || slot > 1)
    {
        return sceMcErrPortSlot;
    }

    root = CardRoot(port, slot);
    std::filesystem::create_directories(root, ec);

    used = UsedClusters(root);

    if (type != 0)
    {
        *type = sceMcTypePS2;
    }
    if (format != 0)
    {
        *format = 1;
    }
    if (free != 0)
    {
        *free = MC_FREE_CLUSTERS - used;
        if (*free < 0)
        {
            *free = 0;
        }
    }

    /* Never sceMcResChangedCard: the host card cannot be swapped mid-session,
     * and reporting a swap would send every caller back round its retry. */
    return PostResult(sceMcResSucceed);
}

int sceMcGetDir(int port, int slot, const char *name, unsigned int mode,
                int maxent, sceMcTblGetDir *table)
{
    std::filesystem::path dir;
    std::error_code       ec;
    int                   count;
    int                   i;

    (void)mode;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }
    if (table == 0 || maxent <= 0)
    {
        return sceMcErrEntryName;
    }

    dir = CardRoot(port, slot) / NormalizeName(name);

    std::memset(table, 0, (std::size_t)maxent * sizeof(sceMcTblGetDir));

    if (!std::filesystem::is_directory(dir, ec))
    {
        return PostResult(0);
    }

    /* "." and ".." first -- the game indexes past them by position. */
    count = 0;
    for (i = 0; i < 2 && count < maxent; i++)
    {
        table[count].AttrFile = sceMcFileAttrSubdir | sceMcFileAttrReadable |
                                sceMcFileAttrWriteable | sceMcFileAttrExecutable;
        table[count].EntryName[0] = '.';
        if (i == 1)
        {
            table[count].EntryName[1] = '.';
        }
        count++;
    }

    for (std::filesystem::directory_iterator it(dir, ec), end;
         it != end && !ec && count < maxent; it.increment(ec))
    {
        std::string leaf = it->path().filename().string();

        if (leaf.size() >= sizeof(table[count].EntryName))
        {
            continue;
        }

        if (it->is_directory(ec))
        {
            table[count].AttrFile = sceMcFileAttrSubdir | sceMcFileAttrReadable |
                                    sceMcFileAttrWriteable;
        }
        else
        {
            table[count].AttrFile     = sceMcFileAttrFile | sceMcFileAttrReadable |
                                        sceMcFileAttrWriteable | sceMcFileAttrClosed;
            table[count].FileSizeByte = (unsigned int)it->file_size(ec);
        }

        std::memcpy(table[count].EntryName, leaf.c_str(), leaf.size() + 1);
        count++;
    }

    return PostResult(count);
}

int sceMcOpen(int port, int slot, const char *name, int mode)
{
    std::filesystem::path path;
    std::error_code       ec;
    const char           *fmode;
    std::FILE            *fp;
    int                   fd;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    path = CardRoot(port, slot) / NormalizeName(name);

    if ((mode & sceMcFileCreateFile) != 0)
    {
        std::filesystem::create_directories(path.parent_path(), ec);
        fmode = "w+b";
    }
    else if ((mode & sceMcFileAttrWriteable) != 0)
    {
        fmode = "r+b";
    }
    else
    {
        fmode = "rb";
    }

    fp = std::fopen(path.string().c_str(), fmode);
    if (fp == 0)
    {
        return PostResult(sceMcResNoEntry);
    }

    fd = AllocHandle(fp, path.string());
    if (fd < 0)
    {
        std::fclose(fp);
        return PostResult(sceMcResUpLimitHandle);
    }

    /* The fd travels back as the operation's result, which is what
     * MemoryCardFileOpenMain() stores through its `fd` out-parameter. */
    return PostResult(fd);
}

int sceMcClose(int fd)
{
    McFile *f;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    f = GetHandle(fd);
    if (f == 0)
    {
        return PostResult(sceMcResNoEntry);
    }

    std::fclose(f->fp);
    f->fp = 0;
    f->path.clear();

    return PostResult(sceMcResSucceed);
}

int sceMcRead(int fd, void *buff, int size)
{
    McFile     *f;
    std::size_t got;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    f = GetHandle(fd);
    if (f == 0 || buff == 0 || size < 0)
    {
        return PostResult(sceMcResNoEntry);
    }

    got = std::fread(buff, 1, (std::size_t)size, f->fp);
    return PostResult((int)got);
}

int sceMcWrite(int fd, const void *buff, int size)
{
    McFile     *f;
    std::size_t put;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    f = GetHandle(fd);
    if (f == 0 || buff == 0 || size < 0)
    {
        return PostResult(sceMcResNoEntry);
    }

    put = std::fwrite(buff, 1, (std::size_t)size, f->fp);
    std::fflush(f->fp);
    return PostResult((int)put);
}

int sceMcMkdir(int port, int slot, const char *name)
{
    std::filesystem::path path;
    std::error_code       ec;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    path = CardRoot(port, slot) / NormalizeName(name);

    std::filesystem::create_directories(path, ec);
    if (!std::filesystem::is_directory(path, ec))
    {
        return PostResult(sceMcResFullDevice);
    }

    return PostResult(sceMcResSucceed);
}

int sceMcDelete(int port, int slot, const char *name)
{
    std::filesystem::path path;
    std::error_code       ec;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    path = CardRoot(port, slot) / NormalizeName(name);

    if (!std::filesystem::exists(path, ec))
    {
        return PostResult(sceMcResNoEntry);
    }

    /* A directory only goes away once it is empty, matching the card: that is
     * why MemoryCardDirDelMain() empties it first and deletes it after. */
    if (std::filesystem::is_directory(path, ec) &&
        !std::filesystem::is_empty(path, ec))
    {
        return PostResult(sceMcResNotEmpty);
    }

    std::filesystem::remove(path, ec);
    return PostResult(ec ? sceMcResDeniedPermit : sceMcResSucceed);
}

int sceMcFormat(int port, int slot)
{
    std::filesystem::path root;
    std::error_code       ec;

    if (init_done == 0)
    {
        return sceMcErrNotInit;
    }

    root = CardRoot(port, slot);

    std::filesystem::remove_all(root, ec);
    std::filesystem::create_directories(root, ec);

    return PostResult(ec ? sceMcResDeniedPermit : sceMcResSucceed);
}

} /* extern "C" */
