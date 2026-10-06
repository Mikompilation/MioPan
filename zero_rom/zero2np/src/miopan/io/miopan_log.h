#ifndef MIOPAN_LOG_H
#define MIOPAN_LOG_H

#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The log file.
 *
 * Everything the port has to say goes into one file in the user directory (see
 * miopan_paths.h): `miopan.log`, rotated so old runs do not accumulate.  Until
 * this existed every diagnostic went to a console that a shipped build has no
 * business owning, which meant the evidence for any bug report was gone the
 * moment the window closed -- and a boot-time assert, which spins forever in
 * PrintAssertReal() while no hook is installed, presented as a frozen window
 * with the reason visible nowhere at all.
 *
 * THREE STREAMS, ONE SINK.  The port has three unrelated sources of diagnostic
 * text and they all end up in the same file:
 *
 *   ROM    the reconstruction's own tracing -- ~1040 printf() call sites,
 *          transcribed verbatim from the Feb 6 2004 prototype.  Redirected as
 *          a whole in sdk/pc_prefix.h, which is force-included into every
 *          translation unit: `#define printf(...) MioPan_LogPrintf(...)`.  One
 *          macro in the port's own shim layer beats editing a thousand call
 *          sites in files that are meant to stay a matching decompilation.
 *
 *   PORT   the host layer -- paths, config, renderer, input.  Already goes
 *          through SDL_Log*, so SDL_SetLogOutputFunction() captures every one
 *          of those sites with a single call and changes nothing else.
 *
 *   SDL    SDL's own warnings and errors, which arrive through the same hook
 *          and are labelled with the category they came from.
 *
 * Records are tagged `rom` or `port` by which of those routes they came in on,
 * not by which directory the caller lives in -- so a port-layer file that still
 * reports through printf shows up as `rom`, which is a fair hint that it should
 * be reporting through SDL_Log instead.
 *
 * WHY SPDLOG AND SDL, NOT ONE OR THE OTHER.  They are not alternatives: SDL
 * owns the *capture* of the port layer (its log function is the only way to
 * intercept SDL's internal messages at all) and spdlog owns the *sink* -- file
 * rotation and retention, level filtering and a flush policy, none of which
 * SDL has any notion of.  spdlog was already fetched and linked and included
 * by nothing; this is what it was fetched for.
 *
 * FLUSHED PER RECORD, DELIBERATELY.  The interesting log is always the one
 * belonging to a process that died badly, so every record is flushed as it is
 * written.  That is a write() per record, not an fsync: a hard kill
 * (TerminateProcess, Task Manager) loses nothing, because the bytes are
 * already the OS's; only pulling the power would.  It also means the log does
 * not depend on a destructor running, which matters here because there is no
 * clean exit -- main() never returns and SDL_EVENT_QUIT calls std::exit(0)
 * from inside the event pump.
 *
 * TUNING, ENVIRONMENT ONLY.  MIOPAN_LOG_LEVEL, MIOPAN_LOG_ROM and
 * MIOPAN_LOG_CONSOLE (see the banner in miopan_log.cpp for the values).  These
 * deliberately do NOT live in miopan.ini alongside the renderer settings: a
 * session that takes any MIOPAN_* override the config module knows about stops
 * saving settings for the whole run (see EnvOverrideActive() in
 * miopan_config.cpp), and "I raised the log level once to reproduce a bug"
 * must not be a reason for the player's window size to stop persisting.  The
 * defaults are the useful ones, so a bug report needs no configuration at all:
 * "send me miopan.log" is the whole instruction.
 */

/* NOTE for whoever edits this file next: sdk/pc_prefix.h includes it, and that
 * is force-included into every translation unit -- so touching this header,
 * comments included, rebuilds the whole tree.  miopan_log.cpp does not; put
 * anything that is going to be revised there. */

/* Bring the log up: resolve its path, open the file, install the SDL hook and
 * write the startup banner.  Idempotent, and called lazily by the first record
 * from any source, so anything that reports before main() -- a global
 * constructor, say -- is still captured.  main() calls it explicitly so the
 * banner is the first thing in the file even on a silent boot. */
void MioPan_LogInit(void);

/* Absolute path of the current log file, or NULL when no file could be opened
 * (the port then logs to the console alone rather than losing the output). */
const char *MioPan_LogPath(void);

/* Push everything through to the OS.  Records are already flushed as they are
 * written, so this is for a caller that is about to do something the logger
 * cannot see coming -- the crash handler, when that lands. */
void MioPan_LogFlush(void);

/*
 * The ROM's printf.  Not called by name anywhere: sdk/pc_prefix.h routes
 * printf() here for every translation unit.  printf-compatible, including the
 * return value, and line-buffered per thread so that a reporter which writes
 * its banner and its body as two calls -- PRINT_ERROR does exactly that --
 * still lands as one record.
 */
#if defined(__GNUC__)
int MioPan_LogPrintf(const char *fmt, ...)
    __attribute__((format(gnu_printf, 1, 2)));
#else
int MioPan_LogPrintf(const char *fmt, ...);
#endif
int MioPan_LogVPrintf(const char *fmt, va_list ap);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_LOG_H */
