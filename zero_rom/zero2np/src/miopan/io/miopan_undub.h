#ifndef MIOPAN_UNDUB_H
#define MIOPAN_UNDUB_H

#include <stddef.h>

#ifdef __cplusplus
#include <string>

extern "C" {
#endif

/*
 * The Japanese voices, imported from the player's own copy of the Japanese
 * release (Zero ~Akai Chou~) through Options > Special (miopan_rml_options.cpp).
 *
 * What is swapped, all of it measured against the Feb 2004 prototype's
 * IMG_BD.BIN and an extracted Japanese disc:
 *
 *   * the voice streams under bin/adpcm/ -- 268 of its 530 .hxd/.str pairs
 *     differ.  A pair always travels together: the Japanese header names its
 *     own volume, pitch, loop points and, for four of Mayu's lines, a second
 *     channel the English recording does not have;
 *   * the sound banks that hold speech, under bin/data/se/: Mio's and Mayu's
 *     own (mio_sys_bd, and ane_sisutemu, which has Mayu's call when Mio
 *     leaves her behind) and 24 ghosts' battle banks.  These are the 26 of
 *     147 whose samples differ.  The rest are the same sounds, most of them
 *     under re-mixed headers -- a mix, not a language -- and stay as they are.
 *     A bank already in SPU RAM is reused by file number, so a switch reaches
 *     these the next time each one is loaded;
 *   * the pre-rendered cutscenes, bin/movie/sNNNN.pss.  Every one has the same
 *     picture count and frame rate in both releases -- only the soundtrack
 *     differs -- so the caption tables, which count film frames, still fit.
 *     The other movies are left alone: the logo and one promo are different
 *     films, not different dubs;
 *   * the in-game caption table, data/subtitle/subtitle.obj.  Its frame counts
 *     time each line against the English recording.  The Japanese disc has the
 *     same table timed against its own, so the import keeps the English
 *     message ids and takes the Japanese timing (see RetimeSubtitles()).
 *
 * Nothing else changes.  The text stays in whichever language the game is set
 * to, and the real-time cutscenes need nothing: their .scn files are
 * byte-identical between the releases.
 *
 * The copies live in the user directory's undub/, laid out as under bin/.  A
 * ROM file that has one is re-pointed in the CD table while the setting is on:
 * uncompressed, its own size, and a start sector past the end of IMG_BD.BIN
 * that MioPan_UndubRead() answers for.  The names stay the ROM's, and
 * MioPan_PathResolveData() hands out the copy for them, so the loaders that
 * read by name (headers, movies, the caption table) and the IOP's stream
 * reader, which reads by sector, agree on which file they are reading.
 */

/* Bind the CD table and apply the imported files if the setting asks for them.
 * `tbl` (a CD_DAT_TBL[]), `names` (a FNAME_DAT[]) and `paths` (the directory
 * strings FNAME_DAT::path_no selects) must hold `rom_file_num` ROM entries and
 * stay alive: switching the voices rewrites rows in place.  Called once,
 * before cddatInit(). */
void MioPan_UndubRegister(void *tbl, void *names, char **paths, int rom_file_num);

/* 1 when an import is on disk -- there is something to switch to. */
int MioPan_UndubAvailable(void);

/* The setting: 1 speak Japanese when it has been imported, 0 English.  It
 * takes effect at once, from the next line spoken; one already playing
 * finishes in the voice it started in. */
int  MioPan_UndubGetEnabled(void);
void MioPan_UndubSetEnabled(int enable);

/* What an import holds: streamed lines (.hxd/.str pairs), sound banks
 * (.hxd/.bd pairs) and cutscene movies. */
int MioPan_UndubVoiceCount(void);
int MioPan_UndubBankCount(void);
int MioPan_UndubMovieCount(void);

/* libcdvd and the file loader: `size` bytes from sector `lsn`, when that is one
 * of the sectors the CD table was given for an imported file.  -1 when it is
 * not (read IMG_BD.BIN as usual), else 1 read or 0 failed.  Past the copy's
 * end reads as zeroes, as the next file's bytes would have in IMG_BD.BIN. */
int MioPan_UndubRead(unsigned int lsn, void *buf, size_t size);

/* miopan_paths: the host path standing in for `bare` -- a data path with its
 * "../" gone, "bin/adpcm/..." -- while the Japanese voices are on.  1 and the
 * path when there is one, else 0. */
int MioPan_UndubResolve(const char *bare, char *out, size_t out_size);

/* menu_radio: where `frame` frames into the recording in `str_file_no` falls
 * in a caption table timed against the English one.  The radio's captions
 * are walked by a frame count, and the Japanese recordings run 0.64 to 1.36
 * times the English length, so the count is scaled by the ratio.  `frame`
 * itself when the English recording is the one playing. */
int MioPan_UndubCaptionFrame(int str_file_no, int frame);

#ifdef __cplusplus
}

/* The import, for the options screen. */
namespace MioPanUndub
{

/* Copy the Japanese voices out of an extracted Japanese disc on a worker
 * thread.  `folder` (UTF-8) may be the bin folder that holds adpcm, data and
 * movie, or any folder above it.  Returns false if one is already running. */
bool StartImport(const std::string &folder);
bool ImportRunning();

/* One line for the screen: what the worker is doing while it runs, else the
 * last import's outcome, else empty. */
std::string ImportStatus();

/* Game thread, once a frame: once a finished import's files are on disk,
 * switch to them. */
void Poll();

} // namespace MioPanUndub
#endif

#endif /* MIOPAN_UNDUB_H */
