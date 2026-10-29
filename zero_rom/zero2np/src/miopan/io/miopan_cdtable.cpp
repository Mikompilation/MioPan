/* ==========================================================================
 *  miopan_cdtable.cpp  --  the CD table for the release the data folder holds
 *
 *  See miopan_cdtable.h for how the prototype's and the EU retail release's
 *  tables differ.  This tells the two apart, writes the rows in, and answers
 *  reads by sector for an extracted tree.
 *
 *  An IMG_BD.BIN is told apart by where its files are: each table's sound
 *  headers have to open on their magic at the sectors it gives them.  The two
 *  tables put every file at a different sector, so the wrong one finds none.
 *  An extracted tree has no sectors, but the loader still takes each file's
 *  size from the table, so there it is a file whose size differs between the
 *  releases: the English message file.
 *
 *  A tree still has to answer reads by sector, because the IOP's stream reader
 *  asks for nothing else.  The chosen table is indexed by sector once, and a
 *  sector is served from the extracted file whose row holds it.
 * ======================================================================== */

#include "miopan_cdtable.h"

#include "miopan_file.h"
#include "miopan_paths.h"
#include "miopan_xbox_assets.h"         /* MIOPAN_ROM_FILE_NUM */

#include "../../system/eeiop/cddat.h"   /* CD_DAT_TBL / FNAME_DAT / MSG_OBJ */

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{

const CD_DAT_TBL kEuRetail[] = {
#include "miopan_cdtable_eu_retail.inc"
};
static_assert(SDL_arraysize(kEuRetail) == MIOPAN_ROM_FILE_NUM,
              "the EU retail table has a row for every ROM file");

constexpr int kHeaderChecks = 16;

const char *g_release = "prototype";

/* The ROM files' sectors as the chosen table has them: [first, end), sorted.
 * Built at boot before any read, read-only after. */
struct Span
{
    uint32_t first;
    uint32_t end;
    int      file_no;
};
std::vector<Span> g_spans;
const CD_DAT_TBL *g_rows = nullptr;
const FNAME_DAT *g_names = nullptr;
char **g_paths = nullptr;

/* Each file's host path in the tree, once looked up; empty when it is not
 * there.  The IOP's reader and the EE's loader both come through here. */
std::mutex g_host_mutex;
std::unordered_map<int, std::string> g_host;

bool EndsWith(const char *s, const char *tail)
{
    const size_t n = std::strlen(s);
    const size_t m = std::strlen(tail);
    return n >= m && SDL_strcasecmp(s + n - m, tail) == 0;
}

/* How many of `files`, sound headers, open on 'HXD\0' (as GCC packs it, see
 * hxd.c) at the sector `rows` gives each. */
int HeadersInPlace(MioPan_File *img, const CD_DAT_TBL *rows, const std::vector<int> &files)
{
    int found = 0;
    for (int no : files)
    {
        uint8_t head[4] = {};
        if (MioPan_FileSeek(img, (int64_t)rows[no].start_sector * 0x800, MIOPAN_SEEK_SET) == 0 &&
            MioPan_FileRead(img, head, sizeof(head)) == sizeof(head) && head[0] == 0x00 &&
            head[1] == 'D' && head[2] == 'X' && head[3] == 'H')
        {
            found++;
        }
    }
    return found;
}

/* 1 the files are the EU retail release's, 0 the prototype's, -1 neither
 * table fits them.  `why` says how it was told, for the log. */
int IsRetail(const CD_DAT_TBL *proto, const FNAME_DAT *names, char **paths, int count,
             std::string &why)
{
    char host[1024];
    if (MioPan_PathResolveData("\\IMG_BD.BIN;1", host, sizeof(host)) != 0)
    {
        MioPan_File *img = MioPan_FileOpen(host, "rb");
        if (img == nullptr)
        {
            why = "IMG_BD.BIN could not be opened";
            return -1;
        }
        /* Headers spread across the whole archive, not packed and present in
         * both releases. */
        std::vector<int> hxd;
        for (int no = 0; no < count; no++)
        {
            if (proto[no].exist_flg != 0 && proto[no].cmp_flg == 0 && names[no].name != nullptr &&
                EndsWith(names[no].name, ".hxd"))
            {
                hxd.push_back(no);
            }
        }
        std::vector<int> files;
        for (int k = 0; k < kHeaderChecks && !hxd.empty(); k++)
        {
            files.push_back(hxd[(size_t)k * hxd.size() / kHeaderChecks]);
        }
        const int retail = HeadersInPlace(img, kEuRetail, files);
        const int prototype = HeadersInPlace(img, proto, files);
        MioPan_FileCloseHandle(img);

        why = "IMG_BD.BIN has " + std::to_string(retail) + " of " +
              std::to_string(files.size()) + " sound headers where the retail table puts "
              "them, " + std::to_string(prototype) + " where the prototype's does";
        if (retail == (int)files.size() && prototype < retail)
        {
            return 1;
        }
        if (prototype == (int)files.size() && retail < prototype)
        {
            return 0;
        }
        return -1;
    }

    const std::string msg = std::string("host0:") + paths[(unsigned char)names[MSG_OBJ].path_no] +
                            names[MSG_OBJ].name;
    if (MioPan_PathResolveData(msg.c_str(), host, sizeof(host)) != 0)
    {
        /* Some extraction tools write every file out to whole sectors. */
        const int64_t size = MioPan_FileSize(host);
        auto is = [size](int64_t want) {
            return size == want || size == ((want + 0x7ff) & ~(int64_t)0x7ff);
        };
        why = "the extracted msg.obj is " + std::to_string(size) + " bytes";
        if (is(kEuRetail[MSG_OBJ].size))
        {
            return 1;
        }
        if (is(proto[MSG_OBJ].size))
        {
            return 0;
        }
        return -1;
    }
    why = "no IMG_BD.BIN and no extracted msg.obj";
    return -1;
}

/* Write the rows for the data folder's release into `rows`. */
void Choose(CD_DAT_TBL *rows, const FNAME_DAT *names, char **paths, int rom_file_num)
{
    if (rom_file_num != MIOPAN_ROM_FILE_NUM)
    {
        SDL_Log("[port] CD table: %d ROM rows, not the %d both releases have; keeping "
                "system.c's", rom_file_num, MIOPAN_ROM_FILE_NUM);
        return;
    }

    std::string why;
    const int retail = IsRetail(rows, names, paths, rom_file_num, why);
    if (retail == 0)
    {
        g_release = "prototype";
        SDL_Log("[port] CD table: the Feb 2004 prototype's -- %s", why.c_str());
        return;
    }
    if (retail < 0)
    {
        SDL_Log("[port] CD table: the data folder's files fit neither the EU retail "
                "release's table nor the prototype's (%s); using the retail one",
                why.c_str());
    }

    /* The two tables agree on which files exist and which are packed -- the
     * generator checks that -- so only the numbers change. */
    for (int no = 0; no < rom_file_num; no++)
    {
        if (rows[no].cmp_flg != kEuRetail[no].cmp_flg ||
            rows[no].exist_flg != kEuRetail[no].exist_flg)
        {
            SDL_Log("[port] CD table: row %d disagrees with the prototype's on what it "
                    "is; keeping system.c's (regenerate miopan_cdtable_eu_retail.inc)", no);
            return;
        }
    }
    std::memcpy(rows, kEuRetail, sizeof(kEuRetail));
    g_release = "EU retail";
    if (retail > 0)
    {
        SDL_Log("[port] CD table: the EU retail release's -- %s", why.c_str());
    }
}

void Index(const CD_DAT_TBL *rows, const FNAME_DAT *names, char **paths, int rom_file_num)
{
    g_rows = rows;
    g_names = names;
    g_paths = paths;
    g_spans.clear();
    for (int no = 0; no < rom_file_num; no++)
    {
        const CD_DAT_TBL &row = rows[no];
        const uint32_t stored = (uint32_t)(row.cmp_flg != 0 ? row.cmp_size : row.size);
        if (row.exist_flg != 0 && stored != 0)
        {
            g_spans.push_back({row.start_sector, row.start_sector + (stored + 0x7ff) / 0x800, no});
        }
    }
    std::sort(g_spans.begin(), g_spans.end(),
              [](const Span &a, const Span &b) { return a.first < b.first; });
}

/* The tree's copy of `file_no` as the disc stores it: a packed file is the
 * "<name-with-dot-as-underscore>.cmp" fileload.c reads, the rest the file of
 * the ROM's name. */
std::string HostOf(int file_no)
{
    std::lock_guard<std::mutex> lock(g_host_mutex);
    const auto cached = g_host.find(file_no);
    if (cached != g_host.end())
    {
        return cached->second;
    }

    const FNAME_DAT &f = g_names[file_no];
    std::string name = std::string("host0:") + g_paths[(unsigned char)f.path_no] + f.name;
    if (g_rows[file_no].cmp_flg != 0)
    {
        const size_t dot = name.find_last_of('.');
        if (dot != std::string::npos)
        {
            name[dot] = '_';
        }
        name += ".cmp";
    }
    char host[1024];
    std::string found;
    if (MioPan_PathResolveData(name.c_str(), host, sizeof(host)) != 0 &&
        MioPan_FileSize(host) >= 0)
    {
        found = host;
    }
    else
    {
        SDL_Log("[port] CD table: a sector read wants %s, which the extracted tree "
                "does not have", name.c_str() + 6);
    }
    g_host.emplace(file_no, found);
    return found;
}

} // namespace

extern "C" void MioPan_CdTableSelect(void *tbl, const void *names_, char **paths,
                                     int rom_file_num)
{
    CD_DAT_TBL *rows = static_cast<CD_DAT_TBL *>(tbl);
    const FNAME_DAT *names = static_cast<const FNAME_DAT *>(names_);
    Choose(rows, names, paths, rom_file_num);
    Index(rows, names, paths, std::min(rom_file_num, (int)MIOPAN_ROM_FILE_NUM));
}

extern "C" const char *MioPan_CdTableRelease(void)
{
    return g_release;
}

extern "C" int MioPan_CdTableReadTree(unsigned int lsn, void *buf, size_t size)
{
    auto it = std::upper_bound(g_spans.begin(), g_spans.end(), lsn,
                               [](unsigned int l, const Span &s) { return l < s.first; });
    if (it == g_spans.begin() || lsn >= (--it)->end || buf == nullptr)
    {
        return -1;
    }
    const std::string host = HostOf(it->file_no);
    if (host.empty())
    {
        return 0;
    }

    MioPan_File *f = MioPan_FileOpen(host.c_str(), "rb");
    if (f == nullptr)
    {
        return 0;
    }
    const int64_t have = MioPan_FileStreamSize(f);
    const int64_t offset = (int64_t)(lsn - it->first) * 0x800;
    size_t got = 0;
    bool ok = true;
    if (offset < have)
    {
        const size_t want = (size_t)std::min<int64_t>((int64_t)size, have - offset);
        ok = MioPan_FileSeek(f, offset, MIOPAN_SEEK_SET) == 0 &&
             (got = MioPan_FileRead(f, buf, want)) == want;
    }
    MioPan_FileCloseHandle(f);
    if (!ok)
    {
        return 0;
    }
    std::memset(static_cast<uint8_t *>(buf) + got, 0, size - got);

    static std::once_flag s_said;
    std::call_once(s_said, [&] {
        SDL_Log("[port] CD table: no IMG_BD.BIN; sectors are read from the extracted "
                "files (first: %s)", host.c_str());
    });
    return 1;
}
