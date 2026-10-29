/* ==========================================================================
 *  miopan_cdtable.cpp  --  the CD table for the release the data folder holds
 *
 *  See miopan_cdtable.h for how the prototype's and the EU retail release's
 *  tables differ.  This only tells the two apart and writes the rows in.
 *
 *  An IMG_BD.BIN is told apart by where its files are: each table's sound
 *  headers have to open on their magic at the sectors it gives them.  The two
 *  tables put every file at a different sector, so the wrong one finds none.
 *  An extracted tree has no sectors, but the loader still takes each file's
 *  size from the table, so there it is a file whose size differs between the
 *  releases: the English message file.
 * ======================================================================== */

#include "miopan_cdtable.h"

#include "miopan_file.h"
#include "miopan_paths.h"
#include "miopan_xbox_assets.h"         /* MIOPAN_ROM_FILE_NUM */

#include "../../system/eeiop/cddat.h"   /* CD_DAT_TBL / FNAME_DAT / MSG_OBJ */

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <cstring>
#include <string>
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

} // namespace

extern "C" void MioPan_CdTableSelect(void *tbl, const void *names_, char **paths,
                                     int rom_file_num)
{
    CD_DAT_TBL *rows = static_cast<CD_DAT_TBL *>(tbl);
    const FNAME_DAT *names = static_cast<const FNAME_DAT *>(names_);
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

extern "C" const char *MioPan_CdTableRelease(void)
{
    return g_release;
}
