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
 *  worker reads the Japanese disc and the English game and writes into the
 *  user directory; it reports through g_job.
 * ======================================================================== */

#include "miopan_undub.h"

#include "miopan_file.h"
#include "miopan_iso9660.h"
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
#include <memory>
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

/* A file as a disc stores it: `size` bytes at `base` in a host file -- an
 * IMG_BD.BIN, a disc image, or a file of its own -- and packed with CMP or
 * not.  Empty (not Ok()) when there is no such file. */
class Stored
{
public:
    Stored() = default;
    /* A negative `size` runs to the end of the host file. */
    Stored(const std::string &host, int64_t base, int64_t size, bool packed)
        : base_(base), size_(size), packed_(packed)
    {
        handle_ = MioPan_FileOpen(host.c_str(), "rb");
        if (handle_ != nullptr && size_ < 0)
        {
            size_ = std::max<int64_t>(MioPan_FileStreamSize(handle_) - base_, 0);
        }
    }
    ~Stored()
    {
        if (handle_ != nullptr)
        {
            MioPan_FileCloseHandle(handle_);
        }
    }
    Stored(Stored &&other) noexcept
    {
        *this = std::move(other);
    }
    Stored &operator=(Stored &&other) noexcept
    {
        std::swap(handle_, other.handle_);
        std::swap(base_, other.base_);
        std::swap(size_, other.size_);
        std::swap(packed_, other.packed_);
        return *this;
    }

    bool Ok() const
    {
        return handle_ != nullptr;
    }
    bool Packed() const
    {
        return packed_;
    }
    /* The bytes as stored: compressed for a compressed file. */
    int64_t Size() const
    {
        return size_;
    }
    bool Read(int64_t offset, void *buf, size_t size)
    {
        if (handle_ == nullptr || offset < 0 || (int64_t)size > size_ - offset ||
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
        if (!packed_)
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
    /* Its size once expanded: what CMP_HEADER::size says for a packed file. */
    int64_t Length()
    {
        if (!packed_)
        {
            return size_;
        }
        int32_t size = -1;
        return Read(0, &size, sizeof(size)) ? size : -1;
    }

private:
    MioPan_File *handle_ = nullptr;
    int64_t      base_ = 0;
    int64_t      size_ = 0;
    bool         packed_ = false;
};

/* Read the ROM file `file_no` the way the loader would with nothing imported:
 * IMG_BD.BIN at its sector, or the extracted tree's file of that name. */
class RomFile : public Stored
{
public:
    explicit RomFile(int file_no)
    {
        const CD_DAT_TBL &row = g_rom[file_no];
        char host[1024];

        t_bypass = true;
        if (MioPan_PathResolveData("\\IMG_BD.BIN;1", host, sizeof(host)) != 0)
        {
            Stored::operator=(Stored(host, (int64_t)row.start_sector * kSector,
                                     row.cmp_flg ? row.cmp_size : row.size,
                                     row.cmp_flg != 0));
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
                Stored::operator=(Stored(host, 0, -1, row.cmp_flg != 0));
            }
        }
        t_bypass = false;
    }
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

/* ---------------------------------------------------------------------------
 *  A Japanese disc image
 *
 *  The Japanese release keeps its files in an IMG_BD.BIN as the prototype
 *  does, but its executable names none of them: SLPS_253.03 has cd_dat_tbl --
 *  sector, size, packed size -- and file_ext_tbl, and no FNAME_DAT or path
 *  strings beside them.  Its table is 288 rows shorter, too (4203 against
 *  4491): no French, German, Spanish or Italian files, no PAL films for the
 *  movie room.
 *
 *  So each ROM file's Japanese row is found by lining the two tables up in
 *  order, the way a diff lines up two texts (AlignTables()).  Most files are no
 *  language at all -- 2989 of the disc's 3349 are the size the prototype's
 *  are -- and they pin the tables together; a file whose size differs sits
 *  between rows that agree.  Checked against an extraction of the disc, that
 *  names every voice file, sound bank and cutscene, and the caption table, as
 *  the extraction does: the voices line up one for one, 113 rows apart.
 *
 *  file_ext_tbl is no help: the Japanese build has an extension the prototype
 *  does not, its logo film's, and numbers the ones after it one higher.
 * ------------------------------------------------------------------------ */

/* A CD table row, from either release. */
struct DiscRow
{
    bool     exist = false;
    bool     packed = false;
    uint32_t sector = 0;
    int64_t  size = 0;          /* expanded */
    int64_t  packed_size = 0;   /* as stored; `size` when not packed */
};

DiscRow RowOf(const CD_DAT_TBL &r)
{
    DiscRow row;
    row.exist = r.exist_flg != 0;
    row.packed = r.cmp_flg != 0;
    row.sector = r.start_sector;
    row.size = r.size;
    row.packed_size = r.cmp_size;
    return row;
}

uint32_t Le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

/* CD_DAT_TBL as the EE lays it out: the flags and sector in one word, low bit
 * first, then the two sizes. */
DiscRow RowAt(const uint8_t *p)
{
    const uint32_t word = Le32(p);
    DiscRow row;
    row.packed = (word & 1) != 0;
    row.exist = (word & 2) != 0;
    row.sector = word >> 2;
    row.size = Le32(p + 4);
    row.packed_size = Le32(p + 8);
    return row;
}

/* Whether `row` can be one of an IMG_BD.BIN of `img_size` bytes.  A row for a
 * file the disc does not have keeps whatever sizes it was built with, and a
 * few files are empty. */
bool Plausible(const DiscRow &row, uint64_t img_size)
{
    if (!row.exist)
    {
        return row.sector == 0 && !row.packed;
    }
    if (row.size < 0 || row.size > INT32_MAX || row.packed_size < 0 ||
        row.packed_size > INT32_MAX || (!row.packed && row.packed_size != row.size))
    {
        return false;
    }
    return (uint64_t)row.sector * kSector + (uint64_t)row.packed_size <= img_size;
}

constexpr size_t kElfMax = 64u << 20;
constexpr size_t kCdTableMin = 1000;    /* rows, for a table to be this game's */

/* The section a symbol's address falls in, as an offset into the file, or 0. */
size_t ElfFileOffset(const Bytes &elf, uint32_t addr, uint32_t length)
{
    const uint32_t shoff = Le32(elf.data() + 0x20);
    const uint16_t shentsize = (uint16_t)(elf[0x2e] | elf[0x2f] << 8);
    const uint16_t shnum = (uint16_t)(elf[0x30] | elf[0x31] << 8);
    for (uint32_t k = 0; k < shnum && shentsize >= 40; k++)
    {
        const size_t at = (size_t)shoff + (size_t)k * shentsize;
        if (at + 40 > elf.size())
        {
            break;
        }
        const uint8_t *s = elf.data() + at;
        const uint32_t type = Le32(s + 4);
        const uint32_t sh_addr = Le32(s + 12);
        const uint32_t sh_offset = Le32(s + 16);
        const uint32_t sh_size = Le32(s + 20);
        if (type == 1 /* SHT_PROGBITS */ && addr >= sh_addr &&
            (uint64_t)addr + length <= (uint64_t)sh_addr + sh_size &&
            (uint64_t)sh_offset + sh_size <= elf.size())
        {
            return (size_t)sh_offset + (addr - sh_addr);
        }
    }
    return 0;
}

/* cd_dat_tbl by its symbol: the retail build kept its symbol table.  The
 * table's offset into the file and its row count, or false. */
bool FindCdTableBySymbol(const Bytes &elf, size_t &offset, size_t &count)
{
    const uint32_t shoff = Le32(elf.data() + 0x20);
    const uint16_t shentsize = (uint16_t)(elf[0x2e] | elf[0x2f] << 8);
    const uint16_t shnum = (uint16_t)(elf[0x30] | elf[0x31] << 8);
    auto section = [&](uint32_t k) -> const uint8_t * {
        const size_t at = (size_t)shoff + (size_t)k * shentsize;
        return k < shnum && shentsize >= 40 && at + 40 <= elf.size() ? elf.data() + at
                                                                    : nullptr;
    };

    for (uint32_t k = 0; k < shnum; k++)
    {
        const uint8_t *symtab = section(k);
        if (symtab == nullptr || Le32(symtab + 4) != 2 /* SHT_SYMTAB */)
        {
            continue;
        }
        const uint8_t *strtab = section(Le32(symtab + 24));
        if (strtab == nullptr)
        {
            continue;
        }
        const size_t sym_at = Le32(symtab + 16);
        const size_t sym_size = Le32(symtab + 20);
        const size_t str_at = Le32(strtab + 16);
        const size_t str_size = Le32(strtab + 20);
        if (sym_at + sym_size > elf.size() || str_at + str_size > elf.size())
        {
            continue;
        }
        static const char kName[] = "cd_dat_tbl";
        for (size_t s = 0; s + 16 <= sym_size; s += 16)
        {
            const uint8_t *sym = elf.data() + sym_at + s;
            const uint32_t name = Le32(sym);
            const uint32_t value = Le32(sym + 4);
            const uint32_t size = Le32(sym + 8);
            if (name + sizeof(kName) > str_size ||
                std::memcmp(elf.data() + str_at + name, kName, sizeof(kName)) != 0 ||
                size % 12 != 0 || size / 12 < kCdTableMin)
            {
                continue;
            }
            offset = ElfFileOffset(elf, value, size);
            count = size / 12;
            if (offset != 0)
            {
                return true;
            }
        }
    }
    return false;
}

/* cd_dat_tbl by its look, for an executable without symbols: the longest run
 * of rows that could be IMG_BD.BIN's, in the data the ELF loads. */
bool FindCdTableByContent(const Bytes &elf, uint64_t img_size, size_t &offset,
                          size_t &count)
{
    const uint32_t phoff = Le32(elf.data() + 0x1c);
    const uint16_t phentsize = (uint16_t)(elf[0x2a] | elf[0x2b] << 8);
    const uint16_t phnum = (uint16_t)(elf[0x2c] | elf[0x2d] << 8);
    size_t best_files = 0;
    for (uint32_t k = 0; k < phnum && phentsize >= 32; k++)
    {
        const size_t at = (size_t)phoff + (size_t)k * phentsize;
        if (at + 32 > elf.size() || Le32(elf.data() + at) != 1 /* PT_LOAD */)
        {
            continue;
        }
        const size_t from = Le32(elf.data() + at + 4);
        const size_t to = std::min(elf.size(), from + (size_t)Le32(elf.data() + at + 16));
        /* A table is word-aligned, so it starts at one of three phases of
         * a 12-byte row. */
        for (size_t phase = 0; phase < 12; phase += 4)
        {
            size_t start = 0;
            size_t rows = 0;
            size_t files = 0;
            size_t last_file = 0;   /* rows up to and including the last file */
            for (size_t p = from + phase; p + 12 <= to; p += 12)
            {
                const DiscRow row = RowAt(elf.data() + p);
                if (Plausible(row, img_size))
                {
                    if (rows == 0)
                    {
                        start = p;
                        files = 0;
                        last_file = 0;
                    }
                    rows++;
                    if (row.exist)
                    {
                        files++;
                        last_file = rows;
                    }
                    if (files > best_files)
                    {
                        best_files = files;
                        offset = start;
                        count = last_file;
                    }
                }
                else
                {
                    rows = 0;
                }
            }
        }
    }
    return best_files >= kCdTableMin;
}

/* The executable's CD table, each row checked against the image's IMG_BD.BIN:
 * one that could not be a file of it is taken as no file. */
bool ReadCdTable(const Bytes &elf, uint64_t img_size, std::vector<DiscRow> &rows)
{
    if (elf.size() < 0x34 || std::memcmp(elf.data(), "\x7f" "ELF", 4) != 0 ||
        elf[4] != 1 /* 32-bit */ || elf[5] != 1 /* little-endian */)
    {
        return false;
    }
    size_t offset = 0;
    size_t count = 0;
    const bool by_symbol = FindCdTableBySymbol(elf, offset, count);
    if (!by_symbol && !FindCdTableByContent(elf, img_size, offset, count))
    {
        return false;
    }

    rows.clear();
    size_t files = 0;
    size_t refused = 0;
    for (size_t k = 0; k < count && offset + 12 * (k + 1) <= elf.size(); k++)
    {
        DiscRow row = RowAt(elf.data() + offset + 12 * k);
        if (row.exist && !Plausible(row, img_size))
        {
            row.exist = false;
            refused++;
        }
        files += row.exist ? 1 : 0;
        rows.push_back(row);
    }
    SDL_Log("[port] undub: the Japanese CD table, found by %s: %zu rows, %zu files, "
            "%zu rows left out that do not fit IMG_BD.BIN",
            by_symbol ? "its symbol" : "its contents", rows.size(), files, refused);
    return files >= kCdTableMin;
}

/*
 * Each ROM file's row in the Japanese table, -1 where it has none.
 *
 * An edit distance over the two tables in order.  A row one table has and the
 * other does not costs 1.  Two rows of the same size cost nothing.  Two of
 * different sizes cost from 0.5 up to 1.5, more the further apart the sizes
 * are: a file recorded again is cheaper than a gap either side of it, and of
 * two candidates the nearer size wins.  `same` counts the files the tables
 * have the same size for.
 */
std::vector<int> AlignTables(const std::vector<DiscRow> &eu,
                             const std::vector<DiscRow> &jp, int &same)
{
    const size_t n = eu.size();
    const size_t m = jp.size();
    std::vector<double> eu_log(n);
    std::vector<double> jp_log(m);
    for (size_t i = 0; i < n; i++)
    {
        eu_log[i] = std::log((double)std::max<int64_t>(eu[i].size, 1));
    }
    for (size_t j = 0; j < m; j++)
    {
        jp_log[j] = std::log((double)std::max<int64_t>(jp[j].size, 1));
    }
    auto cost = [&](size_t i, size_t j) -> double {
        const DiscRow &a = eu[i];
        const DiscRow &b = jp[j];
        if (a.exist != b.exist)
        {
            return 1.5;
        }
        if (!a.exist)
        {
            return 0.0;
        }
        if (a.packed != b.packed)
        {
            return 1.5;
        }
        if (a.size == b.size && a.packed_size == b.packed_size)
        {
            return 0.0;
        }
        return 0.5 + std::min(1.0, std::fabs(eu_log[i] - jp_log[j]));
    };

    enum : uint8_t { kDiagonal, kEuOnly, kJpOnly };
    constexpr double kGap = 1.0;
    /* 19 MB for the two tables here; raw pointers, so a Debug build's checked
     * iterators do not make it crawl. */
    std::vector<uint8_t> from((n + 1) * (m + 1));
    std::vector<double> prev_row(m + 1);
    std::vector<double> cur_row(m + 1);
    uint8_t *back = from.data();
    double *prev = prev_row.data();
    double *cur = cur_row.data();
    for (size_t j = 0; j <= m; j++)
    {
        prev[j] = (double)j * kGap;
        back[j] = kJpOnly;
    }
    for (size_t i = 1; i <= n; i++)
    {
        uint8_t *line = back + i * (m + 1);
        cur[0] = (double)i * kGap;
        line[0] = kEuOnly;
        for (size_t j = 1; j <= m; j++)
        {
            double best = prev[j] + kGap;
            uint8_t how = kEuOnly;
            const double diagonal = prev[j - 1] + cost(i - 1, j - 1);
            if (diagonal < best)
            {
                best = diagonal;
                how = kDiagonal;
            }
            if (cur[j - 1] + kGap < best)
            {
                best = cur[j - 1] + kGap;
                how = kJpOnly;
            }
            cur[j] = best;
            line[j] = how;
        }
        std::swap(prev, cur);
    }

    std::vector<int> row_of(n, -1);
    same = 0;
    size_t i = n;
    size_t j = m;
    while (i > 0 || j > 0)
    {
        const uint8_t how = back[i * (m + 1) + j];
        if (how == kDiagonal)
        {
            i--;
            j--;
            row_of[i] = (int)j;
            if (eu[i].exist && jp[j].exist && cost(i, j) == 0.0)
            {
                same++;
            }
        }
        else if (how == kEuOnly)
        {
            i--;
        }
        else
        {
            j--;
        }
    }
    return row_of;
}

/* The Japanese disc's files: an extracted folder's, found by name the way the
 * ROM spells them, or a disc image's, found by the ROM file's row in the
 * Japanese CD table. */
class JapaneseTree
{
public:
    explicit JapaneseTree(const std::string &bin) : bin_(bin)
    {
    }
    /* IMG_BD.BIN `img_offset` bytes into `image`, and the Japanese row for
     * each ROM file number. */
    JapaneseTree(const std::string &image, uint64_t img_offset,
                 std::vector<DiscRow> rows, std::vector<int> row_of)
        : image_(image), img_offset_(img_offset), rows_(std::move(rows)),
          row_of_(std::move(row_of))
    {
    }

    /* The Japanese copy of `f`, not Ok() when the disc has none. */
    Stored Open(const File &f)
    {
        if (!image_.empty())
        {
            const int j = (size_t)f.file_no < row_of_.size() ? row_of_[f.file_no] : -1;
            if (j < 0 || !rows_[j].exist)
            {
                return Stored();
            }
            const DiscRow &row = rows_[j];
            return Stored(image_, (int64_t)(img_offset_ + (uint64_t)row.sector * kSector),
                          row.packed_size, row.packed);
        }
        const std::string path = Path(f.relative);
        return path.empty() ? Stored() : Stored(path, 0, -1, false);
    }

private:
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

    std::string bin_;
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> dirs_;

    std::string image_;
    uint64_t img_offset_ = 0;
    std::vector<DiscRow> rows_;
    std::vector<int> row_of_;
};

/* The boot executable SYSTEM.CNF names: "BOOT2 = cdrom0:\SLPS_253.03;1". */
bool FindBoot(MioPanIso9660::Image &image, MioPanIso9660::Entry &out)
{
    MioPanIso9660::Entry cnf;
    if (!image.Find("SYSTEM.CNF", cnf) || cnf.directory || cnf.size == 0 ||
        cnf.size > 4096)
    {
        return false;
    }
    std::string text((size_t)cnf.size, '\0');
    if (!image.Read(cnf.offset, text.data(), text.size()))
    {
        return false;
    }
    size_t at = 0;
    while (at < text.size())
    {
        size_t end = text.find_first_of("\r\n", at);
        if (end == std::string::npos)
        {
            end = text.size();
        }
        const std::string line = text.substr(at, end - at);
        at = end + 1;

        const size_t eq = line.find('=');
        std::string key = line.substr(0, eq);
        key.erase(std::remove_if(key.begin(), key.end(),
                                 [](char c) { return std::isspace((unsigned char)c); }),
                  key.end());
        if (eq == std::string::npos || Lower(key) != "boot2")
        {
            continue;
        }
        std::string value = line.substr(eq + 1);
        while (!value.empty() && std::isspace((unsigned char)value.front()))
        {
            value.erase(value.begin());
        }
        while (!value.empty() && std::isspace((unsigned char)value.back()))
        {
            value.pop_back();
        }
        if (Lower(value).compare(0, 7, "cdrom0:") == 0)
        {
            value.erase(0, 7);
        }
        return image.Find(value, out) && !out.directory;
    }
    return false;
}

void SetStatus(const std::string &text);

/* A Japanese disc image, or null and `why`, a line for the screen. */
std::unique_ptr<JapaneseTree> OpenImage(const std::string &path, std::string &why)
{
    static const char kNotTheGame[] =
        "That disc image is not Zero ~Akai Chou~, the Japanese release of "
        "Fatal Frame II: it does not hold that game's files. Choose an image "
        "of the Japanese disc.";

    MioPanIso9660::Image image;
    switch (image.Open(path))
    {
    case MioPanIso9660::OpenResult::Ok:
        break;
    case MioPanIso9660::OpenResult::Unreadable:
        SDL_Log("[port] undub: could not read %s", path.c_str());
        why = "That file could not be read. Check that it is still there and "
              "not in use, then choose it again.";
        return nullptr;
    case MioPanIso9660::OpenResult::NotAnImage:
        SDL_Log("[port] undub: %s has no ISO 9660 volume", path.c_str());
        why = "That file is not a disc image MioPan can read. Choose an .iso "
              "of the Japanese disc (a .chd or .cso has to be turned back into "
              "an .iso first), or the disc's extracted bin folder.";
        return nullptr;
    }

    MioPanIso9660::Entry img;
    MioPanIso9660::Entry boot;
    if (!image.Find("IMG_BD.BIN", img) || img.directory || !FindBoot(image, boot))
    {
        SDL_Log("[port] undub: %s has no IMG_BD.BIN or no boot executable",
                path.c_str());
        why = kNotTheGame;
        return nullptr;
    }
    if (img.offset > image.Size() || img.size > image.Size() - img.offset ||
        boot.offset > image.Size() || boot.size > image.Size() - boot.offset)
    {
        SDL_Log("[port] undub: %s is cut short: IMG_BD.BIN ends at %llu of %llu bytes",
                path.c_str(), (unsigned long long)(img.offset + img.size),
                (unsigned long long)image.Size());
        why = "That disc image is cut short: the game's files run past the end of "
              "it. Copy or dump the disc again.";
        return nullptr;
    }

    SetStatus("Reading the Japanese disc's file table...");
    Bytes elf;
    std::vector<DiscRow> jp;
    if (boot.size <= kElfMax)
    {
        elf.resize((size_t)boot.size);
    }
    if (elf.empty() || !image.Read(boot.offset, elf.data(), elf.size()) ||
        !ReadCdTable(elf, img.size, jp))
    {
        SDL_Log("[port] undub: no CD table in %s's %s", path.c_str(), boot.name.c_str());
        why = kNotTheGame;
        return nullptr;
    }

    std::vector<DiscRow> eu;
    eu.reserve(g_rom.size());
    for (const CD_DAT_TBL &row : g_rom)
    {
        eu.push_back(RowOf(row));
    }
    int same = 0;
    std::vector<int> row_of = AlignTables(eu, jp, same);

    /* The disc's own files, most of them no language at all, are what tie the
     * two tables together.  Another game shares next to none. */
    int jp_files = 0;
    for (const DiscRow &row : jp)
    {
        jp_files += row.exist ? 1 : 0;
    }
    SDL_Log("[port] undub: importing from %s (%s): %d of its %d files are the "
            "size the game's own are",
            path.c_str(), boot.name.c_str(), same, jp_files);
    if (same * 2 < jp_files)
    {
        why = kNotTheGame;
        return nullptr;
    }
    return std::make_unique<JapaneseTree>(image.Path(), img.offset, std::move(jp),
                                          std::move(row_of));
}

/* The Japanese game the player chose: a disc image, or a folder holding an
 * extracted disc.  Null and `why`, a line for the screen, when it is neither. */
std::unique_ptr<JapaneseTree> OpenJapanese(const std::string &chosen, std::string &why)
{
    SDL_PathInfo info;
    if (SDL_GetPathInfo(chosen.c_str(), &info) && info.type == SDL_PATHTYPE_FILE)
    {
        return OpenImage(chosen, why);
    }
    const std::string bin = FindBin(chosen);
    if (bin.empty())
    {
        SDL_Log("[port] undub: no bin/adpcm and bin/movie under %s", chosen.c_str());
        why = "That folder does not hold the game's files. Choose the Japanese "
              "disc's image (.iso), or its extracted bin folder -- the one with "
              "adpcm, data and movie in it -- or a folder above that.";
        return nullptr;
    }
    SDL_Log("[port] undub: importing from %s", bin.c_str());
    return std::make_unique<JapaneseTree>(bin);
}

/*
 * Whether the Japanese file is anything other than the English one: 1 it is,
 * 0 the same bytes, -1 one of them could not be read.
 *
 * Trailing zeroes past the English length are the same file.  Some extraction
 * tools write every file out to a whole number of sectors, and a .hxd that
 * came out as 2048 bytes is still the 88-byte header.
 */
int Differs(int file_no, Stored &jp)
{
    /* A file the Japanese disc packs is small: it is compared expanded. */
    Bytes expanded;
    if (!jp.Ok() || (jp.Packed() && !jp.ReadAll(expanded)))
    {
        return -1;
    }
    const int64_t eu_size = g_rom[file_no].size;
    const int64_t jp_size = jp.Packed() ? (int64_t)expanded.size() : jp.Size();
    if (jp_size <= 0)
    {
        return -1;
    }
    if (jp_size < eu_size || jp_size > ((eu_size + kSector - 1) & ~(int64_t)(kSector - 1)))
    {
        return 1;
    }

    RomFile rom(file_no);
    if (!rom.Ok())
    {
        return -1;
    }

    constexpr size_t kChunk = 1 << 20;
    Bytes a(kChunk);
    Bytes b(kChunk);
    int result = 0;
    for (int64_t at = 0; at < jp_size && result == 0; at += kChunk)
    {
        const size_t n = (size_t)std::min<int64_t>(kChunk, jp_size - at);
        if (jp.Packed())
        {
            std::memcpy(b.data(), expanded.data() + at, n);
        }
        else if (!jp.Read(at, b.data(), n))
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
    return result;
}

/* Into the user directory, through a temporary file, so a half-written file
 * is never where the loader looks.  `bytes`, or else `from` (expanded if it
 * is packed). */
bool Save(const File &f, Stored *from, const Bytes *bytes, std::string &error)
{
    char path[1024];
    if (MioPan_PathPrepareUser(("undub/" + f.relative).c_str(), path, sizeof(path)) == 0)
    {
        error = "could not create a folder for it";
        return false;
    }
    const std::string part = std::string(path) + ".part";

    Bytes expanded;
    if (bytes == nullptr && from->Packed())
    {
        if (!from->ReadAll(expanded))
        {
            error = "could not read it from the Japanese disc";
            return false;
        }
        bytes = &expanded;
    }

    bool ok = false;
    if (bytes != nullptr)
    {
        ok = SDL_SaveFile(part.c_str(), bytes->data(), bytes->size());
    }
    else
    {
        SDL_IOStream *out = SDL_IOFromFile(part.c_str(), "wb");
        if (out != nullptr)
        {
            Bytes chunk(1 << 20);
            ok = true;
            for (int64_t at = 0; at < from->Size() && ok; at += (int64_t)chunk.size())
            {
                const size_t n =
                    (size_t)std::min<int64_t>((int64_t)chunk.size(), from->Size() - at);
                if (!from->Read(at, chunk.data(), n))
                {
                    SDL_SetError("could not read it from the Japanese disc");
                    ok = false;
                }
                else if (SDL_WriteIO(out, chunk.data(), n) != n)
                {
                    ok = false;
                }
            }
            if (!SDL_CloseIO(out))
            {
                ok = false;
            }
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

/* The first four bytes of a file, expanded if it is packed. */
bool Head(Stored &s, uint8_t (&head)[4])
{
    if (!s.Packed())
    {
        return s.Read(0, head, sizeof(head));
    }
    Bytes bytes;
    if (!s.ReadAll(bytes) || bytes.size() < sizeof(head))
    {
        return false;
    }
    std::memcpy(head, bytes.data(), sizeof(head));
    return true;
}

/* A .hxd says what it is in its first word: 'HXD\0' as GCC packs it (hxd.c). */
bool LooksLikeHxd(Stored &s)
{
    uint8_t head[4];
    return Head(s, head) && head[0] == 0x00 && head[1] == 'D' && head[2] == 'X' &&
           head[3] == 'H';
}

/* A PSS opens on an MPEG-2 pack header. */
bool LooksLikePss(Stored &s)
{
    uint8_t head[4];
    return Head(s, head) && head[0] == 0x00 && head[1] == 0x00 && head[2] == 0x01 &&
           head[3] == 0xba;
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
    bool refused = false;       /* not a Japanese disc: `status` says why */
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
        Stored jp_body = tree.Open(body);
        Stored jp_hxd = tree.Open(hxd);
        if (!jp_body.Ok() || !jp_hxd.Ok())
        {
            SDL_Log("[port] undub: %s is not on that disc", body.relative.c_str());
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
            SDL_Log("[port] undub: the Japanese %s is not a sound header",
                    hxd.relative.c_str());
            Count(&Job::failed);
            continue;
        }

        std::string error;
        if (Save(hxd, &jp_hxd, nullptr, error) && Save(body, &jp_body, nullptr, error))
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

        Stored jp = tree.Open(f);
        if (!jp.Ok())
        {
            SDL_Log("[port] undub: %s is not on that disc", f.relative.c_str());
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
            SDL_Log("[port] undub: the Japanese %s could not be read as a movie",
                    f.relative.c_str());
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
        if (Save(f, &jp, nullptr, error))
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
    if (!RomFile(f.file_no).ReadAll(eu) || eu.size() != (size_t)g_rom[f.file_no].size)
    {
        SDL_Log("[port] undub: could not read the game's own caption table");
        return;
    }
    if (!tree.Open(f).ReadAll(jp))
    {
        SDL_Log("[port] undub: no caption table on that disc; the captions keep "
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
        Stored jp_str = tree.Open(g_files[s->second]);
        if (!RomFile(str_file_no - 1).ReadAll(eu_hxd) || !jp_str.Ok() ||
            !tree.Open(g_files[h->second]).ReadAll(jp_hxd))
        {
            return 0.0;
        }
        const double eu_s = StreamSeconds(eu_hxd, g_rom[str_file_no].size);
        const double jp_s = StreamSeconds(jp_hxd, jp_str.Length());
        return eu_s > 0.0 && jp_s > 0.0 ? jp_s / eu_s : 0.0;
    };

    RetimeStats stats;
    if (!RetimeSubtitles(eu, jp, ratio_of, stats))
    {
        SDL_Log("[port] undub: the Japanese %s is not a caption table; the captions "
                "keep the English timing", f.relative.c_str());
        return;
    }

    std::string error;
    if (!Save(f, nullptr, &eu, error))
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
    std::string why;
    const std::unique_ptr<JapaneseTree> found = OpenJapanese(chosen, why);
    if (found == nullptr)
    {
        std::lock_guard<std::mutex> lock(g_job.mutex);
        g_job.refused = true;
        g_job.status = why;
        g_job.finished = true;
        return;
    }

    JapaneseTree &tree = *found;
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

bool StartImport(const std::string &path)
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
        g_job.refused = false;
        g_job.same_files.clear();
    }
    g_job.finished = false;
    g_job.running = true;
    std::thread(ImportThread, path).detach();
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
        if (g_job.refused)
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
                      "Nothing was imported: the voices on that disc are the "
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
