/* ==========================================================================
 *  miopan_log.cpp  --  one file, three streams
 *
 *  See miopan_log.h for what this is and why it is shaped this way.  What
 *  follows is the part that only matters if you are editing this file.
 *
 *  sdk/pc_prefix.h routes printf() here, and pc_prefix.h is force-included
 *  into every translation unit -- including this one.  So the first thing the
 *  file does is undefine the macro again: MioPan_LogPrintf() has to be able to
 *  reach the real printf, and more to the point spdlog's headers must not be
 *  compiled with printf meaning "call the function being defined below".
 *
 *  TUNING (environment only; see the header for why these are not ini keys):
 *
 *    MIOPAN_LOG_LEVEL    trace | debug | info | warn | error | critical | off
 *                        Default debug, i.e. everything including the ROM's
 *                        own tracing.  Raising it filters records after they
 *                        are formatted, exactly as a logger level should; it
 *                        is not a performance knob.
 *
 *    MIOPAN_LOG_ROM      0 to drop the ROM's printf tracing without formatting
 *                        it at all -- one comparison per call site instead of
 *                        a vsnprintf.  This IS the performance knob.  Lines
 *                        that look like one of the engine's four reporters are
 *                        kept even so, because losing an assert to a verbosity
 *                        setting is never the trade the setting was asking for.
 *
 *    MIOPAN_LOG_CONSOLE  0 to suppress the console mirror.  Moot once the
 *                        executable stops being a console application; the
 *                        colour sink is a no-op without a console handle, so
 *                        nothing here has to change when that lands.
 *
 *  LEVELS OUT OF A STREAM THAT HAS NONE.  The ROM's printf carries no
 *  severity: the engine's four reporters (PRINT_ERROR, PRINT_WARNING,
 *  PRINT_ASSERT via PrintAssertReal, and graph3d's G3DASSERT via _Assert) are
 *  distinguished only by the banner text they print.  Those banners are the
 *  only severity information in the stream, so classification reads them --
 *  see kRomMarkers.  It is a text match and it is honest about being one; the
 *  alternative is a thousand annotated call sites in files that are supposed
 *  to stay a matching decompilation.
 * ======================================================================== */

/* Before anything else: see the banner above. */
#undef printf
#undef vprintf

#include "miopan_log.h"

#include "miopan_paths.h"

#include <SDL3/SDL_cpuinfo.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_platform.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_version.h>

#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <memory>
#include <string>
#include <vector>

/* Build stamp.  Supplied per-source by CMakeLists.txt so that a new commit
 * rebuilds this file and not the other thousand-odd translation units; absent
 * when the tree was configured without git, which is not worth failing over. */
#ifndef MIOPAN_BUILD_COMMIT
#define MIOPAN_BUILD_COMMIT "unknown"
#endif
#ifndef MIOPAN_BUILD_TYPE
#define MIOPAN_BUILD_TYPE "unknown"
#endif

namespace
{

/* ---- rotation ---------------------------------------------------------
 *
 * One file per run and three runs of history.  `rotate_on_open` is what makes
 * that true: miopan.log is always the run you just did, miopan.1.log the one
 * before it, and a player asked for "the log" never has to work out which of a
 * dated pile is the right one.  4 MB is roughly forty thousand records, which
 * is far more than a session that went wrong produces before somebody notices;
 * the cap exists so that a run stuck in a per-frame error cannot fill a disk. */
constexpr const char *kLogRelativePath = "miopan.log";
constexpr std::size_t kRotateBytes = 4u * 1024u * 1024u;
constexpr std::size_t kRotateFiles = 3u;

/* Per-thread line assembly.  A fixed buffer rather than a std::string: this is
 * on the path of every one of the ROM's printf sites, several of which run per
 * frame, and it has no business allocating.  8 KB comfortably clears the two
 * biggest producers, PrintAssertReal's 2000-byte compose buffer and
 * g3ddbgAssert's 2048-byte one. */
constexpr std::size_t kPendingCap = 8192;

thread_local char        t_pending[kPendingCap];
thread_local std::size_t t_pending_len;

/* Severity carry.  Several of the reporters print their banner and their body
 * as separate printf calls, each ending in a newline, so the body arrives as
 * its own record with nothing in it left to classify -- PrintAssertReal emits
 * "<<assert>>\n" and then "%s\n" of the composed message, and graph3d's
 * _Warning() spends six calls on one warning.  A banner therefore lends its
 * level to a known number of following records from the same thread; see
 * RomMarker::carry_records.
 *
 * Deliberately not "hold the banner and merge it with what follows": a held
 * record is a record that is not in the file yet, and the whole promise of
 * this log is that what has been written has been written. */
thread_local int t_carry_level = -1;
thread_local int t_carry_left;

bool                             g_ready;
bool                             g_initializing;
bool                             g_rom_enabled = true;
std::string                      g_path;

/* Raw and never deleted, deliberately.  There is no clean exit (see the
 * header): std::exit(0) runs static destructors while the game's threads are
 * still printing, and file-scope objects elsewhere print from their own
 * destructors -- g3dRenderTarget's stack-underflow warning, for one.  As
 * shared_ptrs these were destroyed somewhere in that teardown, and whatever
 * logged after it called into a freed spdlog::logger.  Raw pointers are also
 * constant-initialised, so a static constructor in another TU that logs before
 * this one's have run still sees nullptr rather than garbage. */
spdlog::logger                  *g_rom;
spdlog::logger                  *g_port;

/* Set by the atexit flush.  From then on records skip spdlog and go straight
 * to stderr: the colour console sink locks a function-local static mutex of
 * spdlog's that is destroyed during the same teardown.  Late records miss the
 * file, but by then they are destructor noise. */
std::atomic<bool>                g_closed;

/* Records that arrived while the log was still opening -- the paths module
 * reports which directories it resolved, and it does that from inside the very
 * call that gives us somewhere to write.  Replayed once the sinks exist, so
 * those lines are in the file rather than on a console nobody kept. */
struct PendingRecord
{
    spdlog::level::level_enum level;
    bool                      from_rom;
    std::string               text;
};
std::vector<PendingRecord> g_startup_records;
constexpr std::size_t      kMaxStartupRecords = 256;

/* ---- level ------------------------------------------------------------- */

spdlog::level::level_enum ParseLevel(const char *value,
                                     spdlog::level::level_enum fallback)
{
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }

    static const struct
    {
        const char               *name;
        spdlog::level::level_enum level;
    } kNames[] = {
        {"trace", spdlog::level::trace},
        {"debug", spdlog::level::debug},
        {"info", spdlog::level::info},
        {"warn", spdlog::level::warn},
        {"warning", spdlog::level::warn},
        {"error", spdlog::level::err},
        {"err", spdlog::level::err},
        {"critical", spdlog::level::critical},
        {"off", spdlog::level::off},
        {"none", spdlog::level::off},
    };

    for (const auto &entry : kNames)
    {
        if (SDL_strcasecmp(value, entry.name) == 0)
        {
            return entry.level;
        }
    }
    return fallback;
}

bool EnvFlag(const char *name, bool fallback)
{
    const char *value = SDL_getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    return !(value[0] == '0' || SDL_strcasecmp(value, "off") == 0 ||
             SDL_strcasecmp(value, "false") == 0 ||
             SDL_strcasecmp(value, "no") == 0);
}

/* ---- classifying the ROM's stream -------------------------------------- */

/*
 * The engine's four reporters, by the banner each one prints.  Sources:
 *
 *   common/utility2.h   PRINT_ERROR  -> "***ERR!! file(line):" then the body
 *                       PRINT_WARNING-> "<<<<<<<<<WARNING FILE[..] LINE[..]"
 *   common/utility2.c   PrintWarningReal with no hook -> "<<warning>>"
 *                       PrintAssertReal  with no hook -> "<<assert>>"
 *   common/zero2_util.c Zero2PrintWarningFunc         -> "<<<Warning"
 *   graph3d/g3dDebug.c  _Assert  -> "========== D E B U G   A S S E R T"
 *                       _Warning -> "--------- D E B U G   W A R N I N G"
 *
 * Longest first where one is a prefix of another; the scan takes the first
 * match.  Everything else is the reconstruction's ordinary tracing.
 */
struct RomMarker
{
    const char               *text;
    std::size_t               length;
    spdlog::level::level_enum level;
    /* How many records that follow this banner are part of the same report and
     * should carry its level.  Counted off the reporter's own source, not
     * guessed: PRINT_ERROR's body has no newline of its own and so joins this
     * record (0), PrintAssertReal follows its marker with one composed message
     * (1), and _Warning() spends six calls -- location, function, condition,
     * message and the closing rule (5). */
    int carry_records;
};

#define MIOPAN_MARKER(str, level, carry)                                       \
    {                                                                          \
        (str), sizeof(str) - 1, (level), (carry)                               \
    }

const RomMarker kRomMarkers[] = {
    MIOPAN_MARKER("***ERR!!", spdlog::level::err, 0),
    MIOPAN_MARKER("<<<<<<<<<WARNING", spdlog::level::warn, 0),
    MIOPAN_MARKER("<<<Warning", spdlog::level::warn, 1),
    MIOPAN_MARKER("<<warning>>", spdlog::level::warn, 1),
    MIOPAN_MARKER("<<assert>>", spdlog::level::critical, 1),
    MIOPAN_MARKER("========== D E B U G   A S S E R T",
                  spdlog::level::critical, 0),
    MIOPAN_MARKER("--------- D E B U G   W A R N I N G",
                  spdlog::level::warn, 5),
};

#undef MIOPAN_MARKER

const RomMarker *MatchRomMarker(const char *text, std::size_t length)
{
    for (const RomMarker &marker : kRomMarkers)
    {
        if (length >= marker.length &&
            std::memcmp(text, marker.text, marker.length) == 0)
        {
            return &marker;
        }
    }
    return nullptr;
}

/*
 * Is this format string worth formatting at all when the ROM stream is off?
 *
 * Only the first character is looked at, because that is all the reporters
 * need: they open with '*', '<', '=' or '-', and the two that hand a composed
 * message to a separate call open with '%'.  Over-inclusive on purpose -- a
 * record kept here still has to pass the level filter -- and cheap enough that
 * turning the ROM stream off really does cost one comparison per call site.
 */
bool FormatEvenWithRomOff(const char *fmt)
{
    switch (fmt[0])
    {
    case '*':
    case '<':
    case '=':
    case '-':
    case '%':
        return true;
    default:
        return false;
    }
}

/* ---- SDL categories ---------------------------------------------------- */

const char *SdlCategoryName(int category)
{
    switch (category)
    {
    case SDL_LOG_CATEGORY_APPLICATION:
        return nullptr; /* the port's own SDL_Log; needs no attribution */
    case SDL_LOG_CATEGORY_ERROR:
        return "sdl/error";
    case SDL_LOG_CATEGORY_ASSERT:
        return "sdl/assert";
    case SDL_LOG_CATEGORY_SYSTEM:
        return "sdl/system";
    case SDL_LOG_CATEGORY_AUDIO:
        return "sdl/audio";
    case SDL_LOG_CATEGORY_VIDEO:
        return "sdl/video";
    case SDL_LOG_CATEGORY_RENDER:
        return "sdl/render";
    case SDL_LOG_CATEGORY_INPUT:
        return "sdl/input";
    case SDL_LOG_CATEGORY_TEST:
        return "sdl/test";
    case SDL_LOG_CATEGORY_GPU:
        return "sdl/gpu";
    default:
        return "sdl";
    }
}

spdlog::level::level_enum SdlLevel(SDL_LogPriority priority)
{
    switch (priority)
    {
    case SDL_LOG_PRIORITY_TRACE:
        return spdlog::level::trace;
    case SDL_LOG_PRIORITY_VERBOSE:
    case SDL_LOG_PRIORITY_DEBUG:
        return spdlog::level::debug;
    case SDL_LOG_PRIORITY_WARN:
        return spdlog::level::warn;
    case SDL_LOG_PRIORITY_ERROR:
        return spdlog::level::err;
    case SDL_LOG_PRIORITY_CRITICAL:
        return spdlog::level::critical;
    case SDL_LOG_PRIORITY_INFO:
    default:
        return spdlog::level::info;
    }
}

/* ---- emitting ---------------------------------------------------------- */

void EnsureLog();

void Emit(spdlog::level::level_enum level, bool from_rom, const char *text,
          std::size_t length)
{
    if (length == 0)
    {
        return;
    }

    if (!g_ready)
    {
        if (g_initializing)
        {
            if (g_startup_records.size() < kMaxStartupRecords)
            {
                g_startup_records.push_back(
                    PendingRecord{level, from_rom, std::string(text, length)});
            }
            return;
        }
        EnsureLog();
        if (!g_ready)
        {
            return;
        }
    }

    if (g_closed.load(std::memory_order_acquire))
    {
        std::fwrite(text, 1, length, stderr);
        std::fputc('\n', stderr);
        return;
    }

    spdlog::logger *logger = from_rom ? g_rom : g_port;
    if (logger != nullptr)
    {
        /* The non-formatting overload.  The ROM's text is data, not a format
         * string, and a stray '{' in it would otherwise reach fmt's parser. */
        logger->log(level, spdlog::string_view_t(text, length));
    }
}

/* Hand the assembled line over as one record: trim the newlines the ROM's own
 * formatting leaves at both ends, classify what is left, and reset. */
void EmitPending()
{
    char       *text   = t_pending;
    std::size_t length = t_pending_len;

    t_pending_len = 0;

    while (length > 0 && (text[length - 1] == '\n' || text[length - 1] == '\r'))
    {
        length--;
    }
    while (length > 0 && (text[0] == '\n' || text[0] == '\r'))
    {
        text++;
        length--;
    }
    if (length == 0)
    {
        /* A bare "\n" -- the ROM uses those as spacing, and a timestamped
         * empty record is not spacing. */
        return;
    }

    spdlog::level::level_enum level = spdlog::level::debug;
    const RomMarker          *marker = MatchRomMarker(text, length);

    if (marker != nullptr)
    {
        level         = marker->level;
        t_carry_left  = marker->carry_records;
        t_carry_level = t_carry_left > 0 ? (int)marker->level : -1;
    }
    else if (t_carry_level >= 0)
    {
        level = (spdlog::level::level_enum)t_carry_level;
        if (--t_carry_left <= 0)
        {
            t_carry_level = -1;
        }
    }

    Emit(level, true, text, length);
}

/* Returns the number of characters the call contributed, so that
 * MioPan_LogPrintf() can honour printf's contract. */
int AppendAndMaybeEmit(const char *fmt, va_list ap)
{
    va_list retry;
    va_copy(retry, ap);

    const std::size_t room    = kPendingCap - t_pending_len;
    int               written = std::vsnprintf(t_pending + t_pending_len, room,
                                               fmt, ap);
    if (written < 0)
    {
        va_end(retry);
        return 0;
    }

    if ((std::size_t)written >= room)
    {
        /* It did not fit.  vsnprintf has already scribbled a truncated copy
         * past t_pending_len, so cut the buffer back to what was genuinely
         * pending, ship that, and format again into the empty buffer. */
        t_pending[t_pending_len] = '\0';
        EmitPending();

        written = std::vsnprintf(t_pending, kPendingCap, fmt, retry);
        if (written < 0)
        {
            va_end(retry);
            return 0;
        }
        if ((std::size_t)written >= kPendingCap)
        {
            /* One record longer than the whole buffer.  Nothing in the tree
             * produces one; take the truncation rather than the allocation. */
            written = (int)(kPendingCap - 1);
        }
        t_pending_len = (std::size_t)written;
    }
    else
    {
        t_pending_len += (std::size_t)written;
    }
    va_end(retry);

    if (t_pending_len > 0 && t_pending[t_pending_len - 1] == '\n')
    {
        EmitPending();
    }
    else if (t_pending_len >= kPendingCap - 1)
    {
        /* A producer that never terminates its line must not wedge the buffer. */
        EmitPending();
    }

    return written;
}

/* ---- the SDL hook ------------------------------------------------------ */

void SDLCALL SdlLogOutput(void *userdata, int category,
                          SDL_LogPriority priority, const char *message)
{
    (void)userdata;

    if (message == nullptr)
    {
        return;
    }

    std::size_t length = std::strlen(message);
    while (length > 0 &&
           (message[length - 1] == '\n' || message[length - 1] == '\r'))
    {
        length--;
    }
    if (length == 0)
    {
        return;
    }

    const char *category_name = SdlCategoryName(category);
    if (category_name == nullptr)
    {
        Emit(SdlLevel(priority), false, message, length);
        return;
    }

    /* SDL's own messages carry no attribution of their own, and "could not
     * create texture" reads very differently once you know it came from the
     * GPU backend rather than from audio. */
    std::string tagged;
    tagged.reserve(length + 16);
    tagged.push_back('[');
    tagged.append(category_name);
    tagged.append("] ");
    tagged.append(message, length);
    Emit(SdlLevel(priority), false, tagged.c_str(), tagged.size());
}

/* ---- bring-up ---------------------------------------------------------- */

void WriteBanner(spdlog::level::level_enum level, bool rom_enabled)
{
    const int linked = SDL_GetVersion();

    g_port->info("======== MioPan ========================================"
                 "====================");
    g_port->info("build     : {} {}  commit {}  ({})", __DATE__, __TIME__,
                 MIOPAN_BUILD_COMMIT, MIOPAN_BUILD_TYPE);
    g_port->info("platform  : {}  {} logical cores  {} MB RAM",
                 SDL_GetPlatform(), SDL_GetNumLogicalCPUCores(),
                 SDL_GetSystemRAM());
    g_port->info("libraries : SDL {}.{}.{} (built against {}.{}.{}), "
                 "spdlog {}.{}.{}",
                 SDL_VERSIONNUM_MAJOR(linked), SDL_VERSIONNUM_MINOR(linked),
                 SDL_VERSIONNUM_MICRO(linked), SDL_MAJOR_VERSION,
                 SDL_MINOR_VERSION, SDL_MICRO_VERSION, SPDLOG_VER_MAJOR,
                 SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
    g_port->info("user dir  : {}", MioPan_PathUserDir());
    if (g_path.empty())
    {
        g_port->warn("log file  : none -- could not open {} under the user "
                     "directory; console only",
                     kLogRelativePath);
    }
    else
    {
        g_port->info("log file  : {}  ({} MB x {} kept, flushed per record)",
                     g_path, kRotateBytes / (1024u * 1024u), kRotateFiles + 1u);
    }
    g_port->info("log level : {}  (rom stream {})",
                 spdlog::level::to_string_view(level),
                 rom_enabled ? "on" : "off");
    g_port->info("============================================================================");
    /* The data folder is not resolved yet -- the paths module works it out
     * lazily, on the first file the game asks for, and reports it there. */
}

void EnsureLog()
{
    if (g_ready || g_initializing)
    {
        return;
    }
    g_initializing = true;

    /* The hook goes in first so that the diagnostics the paths module emits
     * while resolving the user directory -- inside the very next call -- are
     * captured rather than lost to a console. */
    SDL_SetLogOutputFunction(SdlLogOutput, nullptr);

    const spdlog::level::level_enum level =
        ParseLevel(SDL_getenv("MIOPAN_LOG_LEVEL"), spdlog::level::debug);
    g_rom_enabled = EnvFlag("MIOPAN_LOG_ROM", true);
    const bool console = EnvFlag("MIOPAN_LOG_CONSOLE", true);

    char path[1024];
    if (MioPan_PathPrepareUser(kLogRelativePath, path, sizeof(path)) != 0)
    {
        g_path = path;
    }

    std::vector<spdlog::sink_ptr> sinks;

    if (!g_path.empty())
    {
        try
        {
            auto file = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                g_path, kRotateBytes, kRotateFiles, true);
            file->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%-4n] [%-8l] %v");
            sinks.push_back(std::move(file));
        }
        catch (const std::exception &)
        {
            /* A read-only or full user directory is a reason to lose the file,
             * not a reason to lose the game. */
            g_path.clear();
        }
    }

    if (console)
    {
        auto out = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        out->set_pattern("[%H:%M:%S.%e] [%-4n] %^[%L] %v%$");
        sinks.push_back(std::move(out));
    }

    g_rom  = new spdlog::logger("rom", sinks.begin(), sinks.end());
    g_port = new spdlog::logger("port", sinks.begin(), sinks.end());

    for (spdlog::logger *logger : {g_rom, g_port})
    {
        /* Opened wide so the banner lands whatever level was asked for -- a log
         * filtered down to warnings is still a log somebody has to read, and it
         * is worthless without the build it came from.  Narrowed to the
         * requested level immediately after the banner, below.  MIOPAN_LOG_ROM
         * is the exception it does not need: `off` means off. */
        logger->set_level(level == spdlog::level::off ? spdlog::level::off
                                                      : spdlog::level::trace);
        /* Every record, straight through to the OS.  See the header. */
        logger->flush_on(spdlog::level::trace);
        /* A logging failure reports itself once and is then ignored; it must
         * never propagate into the game as an exception. */
        logger->set_error_handler([](const std::string &message) {
            static bool reported;
            if (!reported)
            {
                reported = true;
                std::fprintf(stderr, "MioPan log: %s\n", message.c_str());
            }
        });
    }

    g_ready        = true;
    g_initializing = false;

    WriteBanner(level, g_rom_enabled);

    g_rom->set_level(level);
    g_port->set_level(level);

    /* Replayed after the banner rather than before it, so the file opens with
     * the header a bug report is read from.  These carry the replay timestamp,
     * which is a millisecond or two after the fact. */
    for (const PendingRecord &record : g_startup_records)
    {
        Emit(record.level, record.from_rom, record.text.c_str(),
             record.text.size());
    }
    g_startup_records.clear();
    g_startup_records.shrink_to_fit();

    /* std::exit(0) out of the renderer's event pump honours atexit, and
     * nothing else here would run.  Registered after the sinks exist, so it
     * runs before spdlog's own statics are torn down; anything logged after it
     * goes to stderr (see g_closed). */
    std::atexit([]() {
        MioPan_LogFlush();
        g_closed.store(true, std::memory_order_release);
    });
}

} // namespace

extern "C" {

void MioPan_LogInit(void)
{
    EnsureLog();
}

const char *MioPan_LogPath(void)
{
    EnsureLog();
    return g_path.empty() ? nullptr : g_path.c_str();
}

void MioPan_LogFlush(void)
{
    if (!g_ready || g_closed.load(std::memory_order_acquire))
    {
        return;
    }
    if (t_pending_len != 0)
    {
        EmitPending();
    }
    if (g_rom)
    {
        g_rom->flush();
    }
    if (g_port)
    {
        g_port->flush();
    }
}

int MioPan_LogVPrintf(const char *fmt, va_list ap)
{
    if (fmt == nullptr)
    {
        return 0;
    }

    if (!g_rom_enabled && t_pending_len == 0 && t_carry_level < 0 &&
        !FormatEvenWithRomOff(fmt))
    {
        return 0;
    }

    if (!g_ready && !g_initializing)
    {
        EnsureLog();
    }

    return AppendAndMaybeEmit(fmt, ap);
}

int MioPan_LogPrintf(const char *fmt, ...)
{
    va_list ap;
    int     result;

    va_start(ap, fmt);
    result = MioPan_LogVPrintf(fmt, ap);
    va_end(ap);
    return result;
}

}
