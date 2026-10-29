/* ==========================================================================
 *  miopan_undub.cpp  --  the Japanese voices: importing them, and pointing
 *  the CD table at them
 *
 *  See miopan_undub.h for what is swapped and why only that.
 *
 *  Threads.  The CD table, the setting and Poll() belong to the game thread.
 *  Two others come through here: the IOP's stream reader, for a voice's
 *  sectors (MioPan_UndubRead), and anything resolving a file name -- the
 *  loader, the movie stream.  Both see g_files, whose shape is fixed once
 *  MioPan_UndubRegister() has run, and g_state under g_mutex.  The import's
 *  worker reads the Japanese folder and the English game and writes into the
 *  user directory; it reports through g_job.
 * ======================================================================== */

#include "miopan_undub.h"

#include "miopan_file.h"
#include "miopan_paths.h"
#include "miopan_xbox_assets.h"         /* MIOPAN_ROM_FILE_NUM */
#include "../miopan_config.h"

#include "../../main/main.h"            /* GetSubTitleAddr */
#include "../../system/eeiop/cddat.h"   /* CD_DAT_TBL / FNAME_DAT / SUBTITLE_OBJ */
#include "../../system/encodes/cmp.h"   /* CMP_Init / CMP_Decode */

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{

constexpr unsigned int kSector = 0x800;

/*
 * Where an imported file's sectors are.  Each ROM file number has a window of
 * its own past the end of IMG_BD.BIN (2.4 GB, about 0x12d000 sectors), so the
 * sector the table hands out never moves while the file is in use -- a stream
 * the IOP is still reading keeps reading the file it started on, whatever the
 * setting does meanwhile.  A window is 256 MB; the longest film is 196 MB.
 * The whole range has to fit CD_DAT_TBL::start_sector's 30 bits.
 */
constexpr unsigned int kLsnBase = 0x10000000;
constexpr unsigned int kLsnWindow = 0x20000;
static_assert((unsigned long long)kLsnBase +
                      (unsigned long long)MIOPAN_ROM_FILE_NUM * kLsnWindow <
                  (1ull << 30),
              "the undub sectors do not fit CD_DAT_TBL::start_sector");

/* The caption table: 250 offsets, then the captions they point at. */
constexpr int kSubtitleNum = 250;

using Bytes = std::vector<uint8_t>;

enum class Kind
{
    Voice,      /* a streamed line: bin/adpcm/ .hxd + .str */
    Bank,       /* a sound bank of short calls: bin/data/se/ .hxd + .bd */
    Movie,
    Subtitle,
};

struct File
{
    int         file_no;
    Kind        kind;
    std::string relative;       /* "adpcm/MAYU_TALK_37.str": under bin/ on the
                                 * disc, under undub/ here.  The ROM's
                                 * spelling. */
    int         partner = -1;   /* the other half of a pair, as an index here */
    bool        body = false;   /* the .str or .bd of a pair, not its .hxd */
};

/* Per file, under g_mutex. */
struct State
{
    int64_t size = 0;           /* the imported copy's, 0 when there is none */
    bool    applied = false;    /* the CD table row points at the copy */
};

std::vector<File> g_files;
std::unordered_map<int, int> g_index_of_file;           /* file_no -> index */
std::unordered_map<std::string, int> g_index_of_name;   /* lower-case relative */
std::atomic<bool> g_registered{false};

std::mutex g_mutex;
std::vector<State> g_state;

/* Game thread. */
CD_DAT_TBL *g_tbl = nullptr;
std::vector<CD_DAT_TBL> g_rom;  /* every ROM row as system.c had it */
bool g_enabled = true;
bool g_enabled_set = false;
std::unordered_map<int, double> g_caption_scale;

/* The import's own reads of the English game must not be redirected to the
 * copies it is in the middle of replacing. */
thread_local bool t_bypass = false;

std::string Lower(std::string s)
{
    for (char &c : s)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return s;
}

bool EndsWith(const std::string &s, const char *tail)
{
    const size_t n = std::strlen(tail);
    return s.size() >= n && s.compare(s.size() - n, n, tail) == 0;
}

std::string Join(const std::string &dir, const std::string &name)
{
    if (dir.empty() || dir.back() == '/' || dir.back() == '\\')
    {
        return dir + name;
    }
    return dir + "/" + name;
}

std::string UserPath(const File &f)
{
    return std::string(MioPan_PathUserDir()) + "undub/" + f.relative;
}

bool IsDirectory(const std::string &path)
{
    SDL_PathInfo info;
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_DIRECTORY;
}

SDL_EnumerationResult SDLCALL CollectName(void *userdata, const char *dirname,
                                          const char *fname)
{
    (void)dirname;
    static_cast<std::vector<std::string> *>(userdata)->emplace_back(fname);
    return SDL_ENUM_CONTINUE;
}

std::vector<std::string> List(const std::string &dir)
{
    std::vector<std::string> names;
    SDL_EnumerateDirectory(dir.c_str(), CollectName, &names);
    return names;
}

/* A child of `dir` by name, ignoring case: the discs and the tools that
 * extract them do not agree on it. */
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

unsigned int VirtualLsn(int file_no)
{
    return kLsnBase + (unsigned int)file_no * kLsnWindow;
}

/* A pre-rendered cutscene: sNNNN.pss.  The rest of bin/movie/ is the logo,
 * the promos, the game-over film and the movie room's reels, and none of
 * those is a dub. */
bool IsCutscene(const std::string &name)
{
    const std::string n = Lower(name);
    if (n.size() != 9 || n[0] != 's' || !EndsWith(n, ".pss"))
    {
        return false;
    }
    for (int i = 1; i < 5; i++)
    {
        if (!std::isdigit((unsigned char)n[i]))
        {
            return false;
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 *  The CD table
 * ------------------------------------------------------------------------ */

/* Called with g_mutex held. */
bool Usable(int i)
{
    const File &f = g_files[i];
    const int64_t size = g_state[i].size;

    if (size <= 0 || size > (int64_t)kLsnWindow * kSector)
    {
        return false;
    }
    if (f.kind == Kind::Voice || f.kind == Kind::Bank)
    {
        /* A header indexes its own body: half a pair would play the wrong
         * samples, or none. */
        return f.partner >= 0 && g_state[f.partner].size > 0;
    }
    if (f.kind == Kind::Subtitle)
    {
        /* The table is malloc'd once at boot from GetFileSize(), and switching
         * copies it over the loaded one, so it has to be the same size.  The
         * import writes it that way. */
        return size == g_rom[f.file_no].size;
    }
    return true;
}

/* What is in undub/ now, file by file.  One listing per folder and a stat per
 * file found, rather than a probe per ROM file: most of them have no copy. */
void Scan()
{
    std::map<std::string, bool> dirs;
    for (const File &f : g_files)
    {
        dirs[f.relative.substr(0, f.relative.find_last_of('/'))] = true;
    }

    std::unordered_map<std::string, int64_t> sizes;
    for (const auto &entry : dirs)
    {
        const std::string dir = std::string(MioPan_PathUserDir()) + "undub/" + entry.first;
        for (const std::string &name : List(dir))
        {
            SDL_PathInfo info;
            if (!EndsWith(name, ".part") &&
                SDL_GetPathInfo(Join(dir, name).c_str(), &info) &&
                info.type == SDL_PATHTYPE_FILE)
            {
                sizes[Lower(entry.first + "/" + name)] = (int64_t)info.size;
            }
        }
    }

    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_files.size(); i++)
    {
        const auto it = sizes.find(Lower(g_files[i].relative));
        g_state[i].size = it != sizes.end() ? it->second : 0;
    }
}

/* `size` bytes from `offset` into the `have`-byte file at `base` in `f`, and
 * zeroes past its end, as a sector read past a file's end would give. */
bool ReadPadded(MioPan_File *f, int64_t base, int64_t have, int64_t offset, void *buf,
                size_t size)
{
    size_t got = 0;
    if (offset < have)
    {
        const size_t want = (size_t)std::min<int64_t>((int64_t)size, have - offset);
        if (MioPan_FileSeek(f, base + offset, MIOPAN_SEEK_SET) != 0 ||
            (got = MioPan_FileRead(f, buf, want)) != want)
        {
            return false;
        }
    }
    if (got < size)
    {
        std::memset(static_cast<uint8_t *>(buf) + got, 0, size - got);
    }
    return true;
}

/* Read the ROM file `file_no` the way the loader would with nothing imported:
 * IMG_BD.BIN at its sector, or the extracted tree's file of that name. */
class RomFile
{
public:
    explicit RomFile(int file_no) : file_no_(file_no)
    {
        const CD_DAT_TBL &row = g_rom[file_no];
        char host[1024];

        t_bypass = true;
        if (MioPan_PathResolveData("\\IMG_BD.BIN;1", host, sizeof(host)) != 0)
        {
            handle_ = MioPan_FileOpen(host, "rb");
            base_ = (int64_t)row.start_sector * kSector;
            size_ = row.cmp_flg ? row.cmp_size : row.size;
        }
        else if (g_index_of_file.count(file_no) != 0)
        {
            const File &f = g_files[g_index_of_file.at(file_no)];
            std::string name = "host0:../bin/" + f.relative;
            if (row.cmp_flg)
            {
                /* An extracted tree names a compressed file
                 * "<name-with-dot-as-underscore>.cmp", as fileload.c does. */
                const size_t dot = name.find_last_of('.');
                if (dot != std::string::npos)
                {
                    name[dot] = '_';
                }
                name += ".cmp";
            }
            if (MioPan_PathResolveData(name.c_str(), host, sizeof(host)) != 0)
            {
                handle_ = MioPan_FileOpen(host, "rb");
                base_ = 0;
                size_ = handle_ != nullptr ? MioPan_FileStreamSize(handle_) : 0;
            }
        }
        t_bypass = false;
    }
    ~RomFile()
    {
        if (handle_ != nullptr)
        {
            MioPan_FileCloseHandle(handle_);
        }
    }
    RomFile(const RomFile &) = delete;
    RomFile &operator=(const RomFile &) = delete;

    bool Ok() const
    {
        return handle_ != nullptr;
    }
    /* The bytes as stored: compressed for a compressed file. */
    int64_t Size() const
    {
        return size_;
    }
    bool Read(int64_t offset, void *buf, size_t size)
    {
        if (handle_ == nullptr ||
            MioPan_FileSeek(handle_, base_ + offset, MIOPAN_SEEK_SET) != 0)
        {
            return false;
        }
        return MioPan_FileRead(handle_, buf, size) == size;
    }
    bool ReadPadded(int64_t offset, void *buf, size_t size)
    {
        return handle_ != nullptr &&
               ::ReadPadded(handle_, base_, size_, offset, buf, size);
    }
    /* The whole file, expanded if it is stored compressed. */
    bool ReadAll(Bytes &out)
    {
        if (size_ <= 0)
        {
            return false;
        }
        Bytes raw((size_t)size_);
        if (!Read(0, raw.data(), raw.size()))
        {
            return false;
        }
        if (!g_rom[file_no_].cmp_flg)
        {
            out = std::move(raw);
            return true;
        }
        if (raw.size() < sizeof(CMP_HEADER))
        {
            return false;
        }
        CMP_HEADER *header = reinterpret_cast<CMP_HEADER *>(raw.data());
        if (header->div_num <= 0 || header->div_size <= 0 || header->size < 0 ||
            header->data_offset <= 0 || (size_t)header->data_offset > raw.size() ||
            header->div_p <= 0 ||
            (size_t)header->div_p + 4u * (size_t)header->div_num > raw.size())
        {
            return false;
        }
        /* CMP_Decode() writes whole blocks, so the buffer is the block
         * footprint and is cut back to the file's size after. */
        CMP_Init(header);
        out.assign((size_t)header->div_num * (size_t)header->div_size, 0);
        const int size = CMP_Decode(header, out.data());
        if (size < 0 || (size_t)size > out.size())
        {
            return false;
        }
        out.resize((size_t)size);
        return true;
    }

private:
    int          file_no_;
    MioPan_File *handle_ = nullptr;
    int64_t      base_ = 0;
    int64_t      size_ = 0;
};

bool LoadHost(const std::string &path, Bytes &out)
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

/* How long a voice stream plays, from its .hxd and the .str's size.  Each
 * channel is a run of 16-byte VAG blocks of 28 samples, and the rate is the
 * first channel's.  0 when the header does not say. */
double StreamSeconds(const Bytes &hxd, int64_t str_size)
{
    if (hxd.size() < 0x24 || str_size <= 0)
    {
        return 0.0;
    }
    int32_t num = 0;
    int32_t rate = 0;
    std::memcpy(&num, hxd.data() + 0x08, 4);
    std::memcpy(&rate, hxd.data() + 0x20, 4);
    if (num <= 0 || num > 8 || rate <= 0)
    {
        return 0.0;
    }
    return (double)str_size / num / 16.0 * 28.0 / rate;
}

/* The caption table as it sits in memory, swapped for the version the table
 * now names.  Only the frame counts differ between the two -- the import keeps
 * the English layout -- so a caption on screen carries on from where it was. */
void SwapLoadedSubtitles(bool japanese)
{
    int *addr = GetSubTitleAddr();
    if (addr == nullptr)
    {
        return;     /* not loaded yet: the boot load reads whichever is named */
    }
    const auto it = g_index_of_file.find(SUBTITLE_OBJ);
    if (it == g_index_of_file.end())
    {
        return;
    }

    Bytes bytes;
    const bool ok = japanese ? LoadHost(UserPath(g_files[it->second]), bytes)
                             : RomFile(SUBTITLE_OBJ).ReadAll(bytes);
    if (!ok || bytes.size() != (size_t)g_rom[SUBTITLE_OBJ].size)
    {
        SDL_Log("[port] undub: could not swap the loaded caption table");
        return;
    }
    std::memcpy(addr, bytes.data(), bytes.size());
}

/* Point every row at the copy or back at the disc, as the setting says. */
void Apply()
{
    if (g_tbl == nullptr)
    {
        return;
    }

    int subtitle_switched = -1;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        for (size_t i = 0; i < g_files.size(); i++)
        {
            const File &f = g_files[i];
            const bool on = g_enabled && Usable((int)i);
            CD_DAT_TBL &row = g_tbl[f.file_no];

            if (on)
            {
                row.cmp_flg = 0;
                row.exist_flg = 1;
                row.start_sector = VirtualLsn(f.file_no);
                row.size = (int)g_state[i].size;
                row.cmp_size = (int)g_state[i].size;
            }
            else
            {
                row = g_rom[f.file_no];
            }

            if (on != g_state[i].applied && f.kind == Kind::Subtitle)
            {
                subtitle_switched = on ? 1 : 0;
            }
            g_state[i].applied = on;
        }
    }
    g_caption_scale.clear();

    if (subtitle_switched >= 0)
    {
        SwapLoadedSubtitles(subtitle_switched != 0);
    }
}

/* ---------------------------------------------------------------------------
 *  The caption table
 * ------------------------------------------------------------------------ */

struct Caption
{
    bool valid = false;
    size_t at = 0;
    int stream = 0;                 /* the CD file the voice is in */
    std::vector<int> msg;
    std::vector<int> frame;
    std::vector<size_t> frame_at;   /* where each frame count is */
};

int16_t Short(const Bytes &b, size_t at)
{
    int16_t v;
    std::memcpy(&v, b.data() + at, 2);
    return v;
}

/* SUBTITLE_DATA, as subtitle.h lays it out: StreamFile, CharId, then
 * (MsgId, Frame) pairs up to a -1 message. */
std::vector<Caption> ParseSubtitles(const Bytes &b)
{
    std::vector<Caption> out(kSubtitleNum);
    if (b.size() < kSubtitleNum * 4u)
    {
        return out;
    }
    for (int n = 0; n < kSubtitleNum; n++)
    {
        uint32_t off;
        std::memcpy(&off, b.data() + 4 * n, 4);
        if (off < kSubtitleNum * 4u || off + 4u > b.size())
        {
            continue;
        }
        Caption &c = out[n];
        c.at = off;
        c.stream = Short(b, off);
        for (size_t p = off + 4; p + 4 <= b.size() && c.msg.size() < 64; p += 4)
        {
            const int msg = Short(b, p);
            if (msg == -1)
            {
                c.valid = true;
                break;
            }
            c.msg.push_back(msg);
            c.frame.push_back(Short(b, p + 2));
            c.frame_at.push_back(p + 2);
        }
    }
    return out;
}

/* Share `total` frames out between the lines in proportion to `frames`. */
std::vector<int> Distribute(const std::vector<int> &frames, int total)
{
    int sum = 0;
    for (int f : frames)
    {
        sum += std::max(f, 0);
    }
    std::vector<int> out(frames.size(), 1);
    if (sum <= 0 || frames.empty())
    {
        return out;
    }
    double carry = 0.0;
    for (size_t i = 0; i < frames.size(); i++)
    {
        const double want = (double)std::max(frames[i], 0) * total / sum + carry;
        out[i] = std::max(1, (int)std::lround(want));
        carry = want - out[i];
    }
    return out;
}

struct RetimeStats
{
    int japanese = 0;   /* took the Japanese table's own timing */
    int regrouped = 0;  /* same voice, lines split differently: scaled to it */
    int scaled = 0;     /* no Japanese caption: scaled by the recordings */
};

/*
 * The English caption table, timed to the Japanese voices.
 *
 * The two tables share their numbering -- caption n is the same line in both,
 * and its voice is the same file, numbered 113 lower on the Japanese disc,
 * which has no French, German, Spanish or Italian message files ahead of
 * bin/adpcm.  Their message ids mostly agree too, because the English table
 * was made from the Japanese one.  So for each caption:
 *
 *   * the same message ids: take the Japanese frame counts as they are -- 186
 *     of the 250, and 130 of those were never changed for English at all;
 *   * other ids, the line split differently: keep the English lines and share
 *     out the Japanese caption's total time between them;
 *   * no Japanese caption (the line is a sound with no words in Japanese):
 *     scale the English timing by the ratio of the two recordings.
 *
 * Only the frame counts change, so the result has the English table's layout
 * and size byte for byte, which is what lets SwapLoadedSubtitles() copy it
 * over the loaded one.
 */
bool RetimeSubtitles(Bytes &eu, const Bytes &jp,
                     const std::function<double(int)> &ratio_of,
                     RetimeStats &stats)
{
    const std::vector<Caption> e = ParseSubtitles(eu);
    const std::vector<Caption> j = ParseSubtitles(jp);

    /* How far apart the two discs number the voice files: the offset most
     * captions agree on.  A caption that disagrees is not trusted to be the
     * same line. */
    std::map<int, int> votes;
    for (int n = 0; n < kSubtitleNum; n++)
    {
        if (e[n].valid && j[n].valid)
        {
            votes[e[n].stream - j[n].stream]++;
        }
    }
    if (votes.empty())
    {
        return false;
    }
    const int delta = std::max_element(votes.begin(), votes.end(),
                                       [](const auto &a, const auto &b) {
                                           return a.second < b.second;
                                       })->first;

    std::vector<bool> done(eu.size(), false);
    for (int n = 0; n < kSubtitleNum; n++)
    {
        const Caption &c = e[n];
        if (!c.valid || c.msg.empty() || done[c.at])
        {
            continue;
        }
        done[c.at] = true;

        const Caption *jc =
            (j[n].valid && c.stream - j[n].stream == delta) ? &j[n] : nullptr;
        std::vector<int> frames;
        if (jc != nullptr && jc->msg == c.msg)
        {
            frames = jc->frame;
            stats.japanese++;
        }
        else if (jc != nullptr && !jc->msg.empty())
        {
            int total = 0;
            for (int f : jc->frame)
            {
                total += std::max(f, 0);
            }
            frames = Distribute(c.frame, total);
            stats.regrouped++;
        }
        else
        {
            const double ratio = ratio_of(c.stream);
            if (ratio <= 0.0)
            {
                continue;
            }
            int total = 0;
            for (int f : c.frame)
            {
                total += std::max(f, 0);
            }
            frames = Distribute(c.frame, (int)std::lround(total * ratio));
            stats.scaled++;
        }

        for (size_t k = 0; k < frames.size(); k++)
        {
            const int16_t v = (int16_t)std::clamp(frames[k], 1, 0x7fff);
            std::memcpy(eu.data() + c.frame_at[k], &v, 2);
        }
    }
    return true;
}

/* ---------------------------------------------------------------------------
 *  The import
 * ------------------------------------------------------------------------ */

bool IsBin(const std::string &dir)
{
    const std::string adpcm = Child(dir, "adpcm");
    const std::string movie = Child(dir, "movie");
    return !adpcm.empty() && !movie.empty() && IsDirectory(adpcm) && IsDirectory(movie);
}

/* The bin folder the player meant: the folder itself, the disc's root above
 * it, or adpcm / data / movie inside it.  The search goes a few levels down
 * because an extracted disc is often one folder deeper than it looks, and is
 * breadth first so the shallowest copy wins. */
std::string FindBin(std::string chosen)
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
        const std::string parent = chosen.substr(0, cut);
        if ((leaf == "adpcm" || leaf == "movie" || leaf == "data") && IsBin(parent))
        {
            return parent;
        }
    }

    std::deque<std::pair<std::string, int>> queue = {{chosen, 0}};
    int visited = 0;
    while (!queue.empty() && visited < 400)
    {
        const auto [dir, depth] = queue.front();
        queue.pop_front();
        visited++;
        if (IsBin(dir))
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

/* The Japanese folder's files, found by name the way the ROM spells them. */
class JapaneseTree
{
public:
    explicit JapaneseTree(const std::string &bin) : bin_(bin)
    {
    }

    std::string Path(const std::string &relative)
    {
        const size_t slash = relative.find_last_of('/');
        const std::string dir = relative.substr(0, slash);
        auto it = dirs_.find(dir);
        if (it == dirs_.end())
        {
            std::string host = bin_;
            size_t from = 0;
            while (!host.empty() && from <= dir.size())
            {
                const size_t to = std::min(dir.find('/', from), dir.size());
                host = Child(host, dir.substr(from, to - from));
                from = to + 1;
            }
            std::unordered_map<std::string, std::string> names;
            if (!host.empty())
            {
                for (const std::string &n : List(host))
                {
                    names[Lower(n)] = Join(host, n);
                }
            }
            it = dirs_.emplace(dir, std::move(names)).first;
        }
        const auto found = it->second.find(Lower(relative.substr(slash + 1)));
        return found != it->second.end() ? found->second : std::string();
    }

private:
    std::string bin_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dirs_;
};

/*
 * Whether the Japanese file is anything other than the English one: 1 it is,
 * 0 the same bytes, -1 one of them could not be read.
 *
 * Trailing zeroes past the English length are the same file.  Some extraction
 * tools write every file out to a whole number of sectors, and a .hxd that
 * came out as 2048 bytes is still the 88-byte header.
 */
int Differs(int file_no, const std::string &jp_path)
{
    const int64_t eu_size = g_rom[file_no].size;
    const int64_t jp_size = MioPan_FileSize(jp_path.c_str());
    if (jp_size <= 0)
    {
        return -1;
    }
    if (jp_size < eu_size || jp_size > ((eu_size + kSector - 1) & ~(int64_t)(kSector - 1)))
    {
        return 1;
    }

    RomFile rom(file_no);
    MioPan_File *jp = MioPan_FileOpen(jp_path.c_str(), "rb");
    if (!rom.Ok() || jp == nullptr)
    {
        if (jp != nullptr)
        {
            MioPan_FileCloseHandle(jp);
        }
        return -1;
    }

    constexpr size_t kChunk = 1 << 20;
    Bytes a(kChunk);
    Bytes b(kChunk);
    int result = 0;
    for (int64_t at = 0; at < jp_size && result == 0; at += kChunk)
    {
        const size_t n = (size_t)std::min<int64_t>(kChunk, jp_size - at);
        if (MioPan_FileRead(jp, b.data(), n) != n)
        {
            result = -1;
            break;
        }
        const size_t from_rom = (size_t)std::clamp<int64_t>(eu_size - at, 0, (int64_t)n);
        if (from_rom != 0 && !rom.Read(at, a.data(), from_rom))
        {
            result = -1;
            break;
        }
        if (std::memcmp(a.data(), b.data(), from_rom) != 0)
        {
            result = 1;
        }
        for (size_t k = from_rom; k < n && result == 0; k++)
        {
            if (b[k] != 0)
            {
                result = 1;
            }
        }
    }
    MioPan_FileCloseHandle(jp);
    return result;
}

/* Into the user directory, through a temporary file, so a half-written file
 * is never where the loader looks. */
bool Save(const File &f, const std::string &from, const Bytes *bytes, std::string &error)
{
    char path[1024];
    if (MioPan_PathPrepareUser(("undub/" + f.relative).c_str(), path, sizeof(path)) == 0)
    {
        error = "could not create a folder for it";
        return false;
    }
    const std::string part = std::string(path) + ".part";

    bool ok = false;
    if (bytes != nullptr)
    {
        ok = SDL_SaveFile(part.c_str(), bytes->data(), bytes->size());
    }
    else
    {
        SDL_IOStream *out = SDL_IOFromFile(part.c_str(), "wb");
        MioPan_File *in = MioPan_FileOpen(from.c_str(), "rb");
        if (out != nullptr && in != nullptr)
        {
            Bytes chunk(1 << 20);
            ok = true;
            for (;;)
            {
                const size_t got = MioPan_FileRead(in, chunk.data(), chunk.size());
                if (got == 0)
                {
                    break;
                }
                if (SDL_WriteIO(out, chunk.data(), got) != got)
                {
                    ok = false;
                    break;
                }
            }
        }
        if (in != nullptr)
        {
            MioPan_FileCloseHandle(in);
        }
        if (out != nullptr && !SDL_CloseIO(out))
        {
            ok = false;
        }
    }

    if (!ok || !SDL_RenamePath(part.c_str(), path))
    {
        error = SDL_GetError();
        SDL_RemovePath(part.c_str());
        return false;
    }
    return true;
}

/* A .hxd says what it is in its first word: 'HXD\0' as GCC packs it (hxd.c). */
bool LooksLikeHxd(const std::string &path)
{
    uint8_t head[4];
    return MioPan_FileReadAt(path.c_str(), 0, head, sizeof(head)) != 0 &&
           head[0] == 0x00 && head[1] == 'D' && head[2] == 'X' && head[3] == 'H';
}

/* A PSS opens on an MPEG-2 pack header. */
bool LooksLikePss(const std::string &path)
{
    uint8_t head[4];
    return MioPan_FileReadAt(path.c_str(), 0, head, sizeof(head)) != 0 &&
           head[0] == 0x00 && head[1] == 0x00 && head[2] == 0x01 && head[3] == 0xba;
}

/* The worker is detached: it is never waited for, so quitting mid-import
 * just leaves a .part file behind, which nothing reads. */
struct Job
{
    std::mutex mutex;
    std::atomic<bool> running{false};
    std::atomic<bool> finished{false};
    std::string status;         /* guarded by mutex */
    int voices = 0;
    int banks = 0;
    int movies = 0;
    int same = 0;
    int failed = 0;
    bool subtitles = false;
    bool no_folder = false;
    std::vector<int> same_files;    /* indices whose sound is the English one */
};

Job g_job;

void SetStatus(const std::string &text)
{
    std::lock_guard<std::mutex> lock(g_job.mutex);
    g_job.status = text;
}

void Count(int Job::*field)
{
    std::lock_guard<std::mutex> lock(g_job.mutex);
    g_job.*field += 1;
}

void Same(int index)
{
    std::lock_guard<std::mutex> lock(g_job.mutex);
    g_job.same++;
    g_job.same_files.push_back(index);
    if (g_files[index].partner >= 0)
    {
        g_job.same_files.push_back(g_files[index].partner);
    }
}

/*
 * Every streamed line and every sound bank whose recording is not the English
 * one, each with its own header.
 *
 * The test is the body alone.  A header differs far more often than its sound
 * does -- most of the 121 sound banks whose samples are the same have a
 * re-mixed header, footsteps and doors included -- and that is a mix, not a
 * language.  Where the samples do differ the pair goes together: the header
 * holds each sample's offset into its body.
 */
void ImportVoices(JapaneseTree &tree)
{
    std::vector<int> pairs;     /* the body of each pair */
    for (size_t i = 0; i < g_files.size(); i++)
    {
        if ((g_files[i].kind == Kind::Voice || g_files[i].kind == Kind::Bank) &&
            g_files[i].body && g_files[i].partner >= 0)
        {
            pairs.push_back((int)i);
        }
    }

    for (size_t k = 0; k < pairs.size(); k++)
    {
        if (k % 8 == 0)
        {
            char text[96];
            std::snprintf(text, sizeof(text), "Comparing voices (%zu of %zu)...",
                          k + 1, pairs.size());
            SetStatus(text);
        }

        const File &body = g_files[pairs[k]];
        const File &hxd = g_files[body.partner];
        const std::string jp_body = tree.Path(body.relative);
        const std::string jp_hxd = tree.Path(hxd.relative);
        if (jp_body.empty() || jp_hxd.empty())
        {
            SDL_Log("[port] undub: %s is not in that folder", body.relative.c_str());
            Count(&Job::failed);
            continue;
        }

        const int differs = Differs(body.file_no, jp_body);
        if (differs < 0)
        {
            SDL_Log("[port] undub: could not compare %s", body.relative.c_str());
            Count(&Job::failed);
            continue;
        }
        if (differs == 0)
        {
            Same(pairs[k]);
            continue;
        }
        if (!LooksLikeHxd(jp_hxd))
        {
            SDL_Log("[port] undub: %s is not a sound header", jp_hxd.c_str());
            Count(&Job::failed);
            continue;
        }

        std::string error;
        if (Save(hxd, jp_hxd, nullptr, error) && Save(body, jp_body, nullptr, error))
        {
            Count(body.kind == Kind::Voice ? &Job::voices : &Job::banks);
        }
        else
        {
            SDL_Log("[port] undub: %s failed: %s", body.relative.c_str(), error.c_str());
            Count(&Job::failed);
        }
    }
}

void ImportMovies(JapaneseTree &tree)
{
    std::vector<int> movies;
    for (size_t i = 0; i < g_files.size(); i++)
    {
        if (g_files[i].kind == Kind::Movie)
        {
            movies.push_back((int)i);
        }
    }

    for (size_t k = 0; k < movies.size(); k++)
    {
        const File &f = g_files[movies[k]];
        {
            char text[96];
            std::snprintf(text, sizeof(text), "Comparing cutscenes (%zu of %zu)...",
                          k + 1, movies.size());
            SetStatus(text);
        }

        const std::string jp = tree.Path(f.relative);
        if (jp.empty())
        {
            SDL_Log("[port] undub: %s is not in that folder", f.relative.c_str());
            Count(&Job::failed);
            continue;
        }
        const int differs = Differs(f.file_no, jp);
        if (differs == 0)
        {
            Same(movies[k]);
            continue;
        }
        if (differs < 0 || !LooksLikePss(jp))
        {
            SDL_Log("[port] undub: %s could not be read as a movie", jp.c_str());
            Count(&Job::failed);
            continue;
        }

        {
            char text[96];
            std::snprintf(text, sizeof(text), "Copying cutscenes (%zu of %zu)...",
                          k + 1, movies.size());
            SetStatus(text);
        }
        std::string error;
        if (Save(f, jp, nullptr, error))
        {
            Count(&Job::movies);
        }
        else
        {
            SDL_Log("[port] undub: %s failed: %s", f.relative.c_str(), error.c_str());
            Count(&Job::failed);
        }
    }
}

void ImportSubtitles(JapaneseTree &tree)
{
    const auto it = g_index_of_file.find(SUBTITLE_OBJ);
    if (it == g_index_of_file.end())
    {
        return;
    }
    const File &f = g_files[it->second];
    SetStatus("Retiming the subtitles...");

    Bytes eu;
    Bytes jp;
    const std::string jp_path = tree.Path(f.relative);
    if (!RomFile(f.file_no).ReadAll(eu) || eu.size() != (size_t)g_rom[f.file_no].size)
    {
        SDL_Log("[port] undub: could not read the game's own caption table");
        return;
    }
    if (jp_path.empty() || !LoadHost(jp_path, jp))
    {
        SDL_Log("[port] undub: no caption table in that folder; the captions keep "
                "the English timing");
        return;
    }

    /* For a caption the Japanese table leaves blank: how much longer the
     * Japanese recording is. */
    auto ratio_of = [&tree](int str_file_no) -> double {
        const auto s = g_index_of_file.find(str_file_no);
        const auto h = g_index_of_file.find(str_file_no - 1);
        if (s == g_index_of_file.end() || h == g_index_of_file.end())
        {
            return 0.0;
        }
        Bytes eu_hxd;
        Bytes jp_hxd;
        const std::string jp_str = tree.Path(g_files[s->second].relative);
        if (!RomFile(str_file_no - 1).ReadAll(eu_hxd) || jp_str.empty() ||
            !LoadHost(tree.Path(g_files[h->second].relative), jp_hxd))
        {
            return 0.0;
        }
        const double eu_s = StreamSeconds(eu_hxd, g_rom[str_file_no].size);
        const double jp_s = StreamSeconds(jp_hxd, MioPan_FileSize(jp_str.c_str()));
        return eu_s > 0.0 && jp_s > 0.0 ? jp_s / eu_s : 0.0;
    };

    RetimeStats stats;
    if (!RetimeSubtitles(eu, jp, ratio_of, stats))
    {
        SDL_Log("[port] undub: %s is not a caption table; the captions keep the "
                "English timing", jp_path.c_str());
        return;
    }

    std::string error;
    if (!Save(f, std::string(), &eu, error))
    {
        SDL_Log("[port] undub: could not write the caption table: %s", error.c_str());
        Count(&Job::failed);
        return;
    }
    SDL_Log("[port] undub: captions retimed -- %d from the Japanese table, %d "
            "regrouped onto its timing, %d scaled by the recordings",
            stats.japanese, stats.regrouped, stats.scaled);
    std::lock_guard<std::mutex> lock(g_job.mutex);
    g_job.subtitles = true;
}

void ImportThread(std::string chosen)
{
    SetStatus("Looking for the Japanese game's files...");
    const std::string bin = FindBin(chosen);
    if (bin.empty())
    {
        SDL_Log("[port] undub: no bin/adpcm and bin/movie under %s", chosen.c_str());
        std::lock_guard<std::mutex> lock(g_job.mutex);
        g_job.no_folder = true;
        g_job.status = "That folder does not hold the game's files. Choose the "
                       "Japanese disc's extracted bin folder -- the one with "
                       "adpcm, data and movie in it -- or a folder above it.";
        g_job.finished = true;
        return;
    }
    SDL_Log("[port] undub: importing from %s", bin.c_str());

    JapaneseTree tree(bin);
    ImportVoices(tree);
    ImportMovies(tree);

    bool any = false;
    {
        std::lock_guard<std::mutex> lock(g_job.mutex);
        any = g_job.voices + g_job.banks + g_job.movies != 0;
    }
    /* Only for a disc that turned out to be a different dub: retiming the
     * English captions against the English game's own table would change
     * nothing, and writing it would make the import look like it found
     * something. */
    if (any)
    {
        ImportSubtitles(tree);
    }
    g_job.finished = true;
}

} // namespace

extern "C" void MioPan_UndubRegister(void *tbl, void *names, char **paths,
                                     int rom_file_num)
{
    g_tbl = static_cast<CD_DAT_TBL *>(tbl);
    const FNAME_DAT *fname = static_cast<const FNAME_DAT *>(names);
    g_rom.assign(g_tbl, g_tbl + rom_file_num);

    for (int no = 0; no < rom_file_num; no++)
    {
        const std::string dir = paths[(unsigned char)fname[no].path_no];
        const std::string name = fname[no].name;
        if (dir.compare(0, 7, "../bin/") != 0 || g_rom[no].exist_flg == 0)
        {
            continue;
        }

        File f;
        f.file_no = no;
        f.relative = dir.substr(7) + name;
        const std::string lower = Lower(name);
        if (no == SUBTITLE_OBJ)
        {
            f.kind = Kind::Subtitle;
        }
        else if (g_rom[no].cmp_flg != 0)
        {
            continue;
        }
        else if (dir == "../bin/adpcm/" &&
                 (EndsWith(lower, ".str") || EndsWith(lower, ".hxd")))
        {
            f.kind = Kind::Voice;
            f.body = EndsWith(lower, ".str");
        }
        else if (dir == "../bin/data/se/" &&
                 (EndsWith(lower, ".bd") || EndsWith(lower, ".hxd")))
        {
            f.kind = Kind::Bank;
            f.body = EndsWith(lower, ".bd");
        }
        else if (dir == "../bin/movie/" && IsCutscene(name))
        {
            f.kind = Kind::Movie;
        }
        else
        {
            continue;
        }
        g_index_of_file[no] = (int)g_files.size();
        g_index_of_name[Lower(f.relative)] = (int)g_files.size();
        g_files.push_back(std::move(f));
    }

    /* Pair each body with its header.  The ROM's convention is that the
     * header is the file just below it -- StreamAutoPlay(n, n - 1) for a
     * stream, ANE_SISUTEMU_HXD / _BD for a bank -- and the names agree on
     * every pair. */
    for (File &f : g_files)
    {
        if (!f.body)
        {
            continue;
        }
        const auto it = g_index_of_file.find(f.file_no - 1);
        if (it == g_index_of_file.end())
        {
            continue;
        }
        File &h = g_files[it->second];
        const std::string stem =
            Lower(f.relative.substr(0, f.relative.find_last_of('.')));
        if (h.kind == f.kind && !h.body && Lower(h.relative) == stem + ".hxd")
        {
            f.partner = it->second;
            h.partner = g_index_of_file[f.file_no];
        }
    }

    g_state.assign(g_files.size(), State());
    g_registered.store(true, std::memory_order_release);

    if (!g_enabled_set)
    {
        MioPan_ConfigLoad();
        g_enabled = miopan_config.audio.japanese_voices != 0;
    }
    Scan();
    Apply();

    if (MioPan_UndubAvailable())
    {
        SDL_Log("[port] undub: %d voice line(s), %d sound bank(s) and %d "
                "cutscene(s) imported; speaking %s",
                MioPan_UndubVoiceCount(), MioPan_UndubBankCount(),
                MioPan_UndubMovieCount(), g_enabled ? "Japanese" : "English");
    }
}

extern "C" int MioPan_UndubAvailable(void)
{
    if (!g_registered.load(std::memory_order_acquire))
    {
        return 0;
    }
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_files.size(); i++)
    {
        if (g_files[i].kind != Kind::Subtitle && Usable((int)i))
        {
            return 1;
        }
    }
    return 0;
}

extern "C" int MioPan_UndubGetEnabled(void)
{
    return g_enabled ? 1 : 0;
}

extern "C" void MioPan_UndubSetEnabled(int enable)
{
    g_enabled_set = true;
    if (g_enabled == (enable != 0))
    {
        return;
    }
    g_enabled = enable != 0;
    Apply();
}

namespace
{

/* Imported files of one kind, a pair counting once. */
int CountUsable(Kind kind)
{
    if (!g_registered.load(std::memory_order_acquire))
    {
        return 0;
    }
    int count = 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    for (size_t i = 0; i < g_files.size(); i++)
    {
        if (g_files[i].kind == kind && (g_files[i].partner < 0 || g_files[i].body) &&
            Usable((int)i))
        {
            count++;
        }
    }
    return count;
}

} // namespace

extern "C" int MioPan_UndubVoiceCount(void)
{
    return CountUsable(Kind::Voice);
}

extern "C" int MioPan_UndubBankCount(void)
{
    return CountUsable(Kind::Bank);
}

extern "C" int MioPan_UndubMovieCount(void)
{
    return CountUsable(Kind::Movie);
}

extern "C" int MioPan_UndubRead(unsigned int lsn, void *buf, size_t size)
{
    if (lsn < kLsnBase || !g_registered.load(std::memory_order_acquire))
    {
        return -1;
    }
    const int file_no = (int)((lsn - kLsnBase) / kLsnWindow);
    const auto it = g_index_of_file.find(file_no);
    if (it == g_index_of_file.end())
    {
        return -1;
    }

    int64_t have;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        have = g_state[it->second].size;
    }
    const int64_t offset = (int64_t)((lsn - kLsnBase) % kLsnWindow) * kSector;
    if (buf == nullptr)
    {
        return 0;
    }

    if (have > 0)
    {
        MioPan_File *f = MioPan_FileOpen(UserPath(g_files[it->second]).c_str(), "rb");
        if (f != nullptr)
        {
            const bool ok = ReadPadded(f, 0, have, offset, buf, size);
            MioPan_FileCloseHandle(f);
            if (ok)
            {
                return 1;
            }
        }
    }

    /* The copy has gone from under a stream that started on it -- a re-import
     * dropped it, or the folder was emptied by hand.  Failing would leave the
     * IOP retrying the read for ever, so the disc's own file stands in.  Not
     * for the caption table, whose disc copy is compressed and which nothing
     * reads by sector. */
    if (g_rom[file_no].cmp_flg != 0)
    {
        return 0;
    }
    RomFile rom(file_no);
    return rom.ReadPadded(offset, buf, size) ? 1 : 0;
}

extern "C" int MioPan_UndubResolve(const char *bare, char *out, size_t out_size)
{
    if (t_bypass || bare == nullptr || std::strncmp(bare, "bin/", 4) != 0 ||
        !g_registered.load(std::memory_order_acquire))
    {
        return 0;
    }
    const auto it = g_index_of_name.find(Lower(bare + 4));
    if (it == g_index_of_name.end())
    {
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        if (!g_state[it->second].applied)
        {
            return 0;
        }
    }
    const std::string path = UserPath(g_files[it->second]);
    if (out == nullptr || path.size() + 1 > out_size)
    {
        return 0;
    }
    std::memcpy(out, path.c_str(), path.size() + 1);
    return 1;
}

extern "C" int MioPan_UndubCaptionFrame(int str_file_no, int frame)
{
    if (!g_registered.load(std::memory_order_acquire))
    {
        return frame;
    }
    auto cached = g_caption_scale.find(str_file_no);
    if (cached == g_caption_scale.end())
    {
        double scale = 1.0;
        const auto s = g_index_of_file.find(str_file_no);
        const auto h = g_index_of_file.find(str_file_no - 1);
        bool applied = false;
        int64_t size = 0;
        if (s != g_index_of_file.end() && h != g_index_of_file.end())
        {
            std::lock_guard<std::mutex> lock(g_mutex);
            applied = g_state[s->second].applied;
            size = g_state[s->second].size;
        }
        if (applied)
        {
            Bytes eu_hxd;
            Bytes jp_hxd;
            if (RomFile(str_file_no - 1).ReadAll(eu_hxd) &&
                LoadHost(UserPath(g_files[h->second]), jp_hxd))
            {
                const double eu_s = StreamSeconds(eu_hxd, g_rom[str_file_no].size);
                const double jp_s = StreamSeconds(jp_hxd, size);
                if (eu_s > 0.0 && jp_s > 0.0)
                {
                    scale = eu_s / jp_s;
                }
            }
        }
        cached = g_caption_scale.emplace(str_file_no, scale).first;
    }
    return (int)(frame * cached->second);
}

namespace MioPanUndub
{

bool StartImport(const std::string &folder)
{
    if (g_job.running.load() || !g_registered.load(std::memory_order_acquire))
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(g_job.mutex);
        g_job.status.clear();
        g_job.voices = 0;
        g_job.banks = 0;
        g_job.movies = 0;
        g_job.same = 0;
        g_job.failed = 0;
        g_job.subtitles = false;
        g_job.no_folder = false;
        g_job.same_files.clear();
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

    int voices;
    int banks;
    int movies;
    int failed;
    bool subtitles;
    std::vector<int> same_files;
    {
        std::lock_guard<std::mutex> lock(g_job.mutex);
        if (g_job.no_folder)
        {
            return;             /* the worker's own message stands */
        }
        voices = g_job.voices;
        banks = g_job.banks;
        movies = g_job.movies;
        failed = g_job.failed;
        subtitles = g_job.subtitles;
        same_files = g_job.same_files;
    }

    /*
     * A copy whose sound this disc has in English too is left over from an
     * earlier import -- another pressing of the game, or an older version of
     * this one that compared differently -- and goes, so undub/ holds what
     * this disc differs in and nothing else.  Only for a disc that turned out
     * to be a different dub: choosing the English game's own folder by mistake
     * finds everything the same, and must not undo an import.
     *
     * Here rather than on the worker, so the rows follow at once below.  A
     * stream still reading one of them falls back to the disc's own bytes
     * (MioPan_UndubRead), which are the same sound.
     */
    if (voices + banks + movies != 0)
    {
        int removed = 0;
        for (int i : same_files)
        {
            const std::string path = UserPath(g_files[i]);
            SDL_PathInfo info;
            if (SDL_GetPathInfo(path.c_str(), &info) && SDL_RemovePath(path.c_str()))
            {
                removed++;
            }
        }
        if (removed != 0)
        {
            SDL_Log("[port] undub: removed %d earlier copies this disc has the "
                    "same as the English game",
                    removed);
        }
    }

    Scan();
    /* Importing them is asking for them. */
    if (voices + banks + movies != 0)
    {
        g_enabled = true;
    }
    Apply();

    char text[320];
    if (voices + banks + movies == 0)
    {
        std::snprintf(text, sizeof(text),
                      "Nothing was imported: the voices in that folder are the "
                      "ones the game already has. Choose a copy of the Japanese "
                      "release.%s",
                      failed != 0 ? " Some files could not be read; the log says which."
                                  : "");
    }
    else
    {
        std::snprintf(text, sizeof(text),
                      "Imported %d voice line%s, %d set%s of calls and cries and %d "
                      "cutscene%s%s.%s",
                      voices, voices == 1 ? "" : "s", banks, banks == 1 ? "" : "s",
                      movies, movies == 1 ? "" : "s",
                      subtitles ? ", with the subtitles retimed to them" : "",
                      failed != 0 ? " Some files could not be imported; the log "
                                    "says which."
                                  : "");
    }
    SDL_Log("[port] undub: import done -- %d voice line(s), %d sound bank(s), %d "
            "cutscene(s), %d failed, captions %s",
            voices, banks, movies, failed, subtitles ? "retimed" : "unchanged");
    std::lock_guard<std::mutex> lock(g_job.mutex);
    g_job.status = text;
}

} // namespace MioPanUndub
