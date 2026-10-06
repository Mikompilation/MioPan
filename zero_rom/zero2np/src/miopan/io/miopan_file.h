#ifndef MIOPAN_FILE_H
#define MIOPAN_FILE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Host file-system wrapper.  Confines all host file I/O (SDL_IOStream / fopen)
// to the miopan layer, so the SDK shims (libcdvd, fileio) don't call SDL/stdio
// directly.  Previously each of those shims carried its own near-identical
// NormalizeXxxPath + root-search.
//
// This is bytes only -- every path handed to it is already absolute.  Deciding
// WHERE a file is belongs to miopan_paths.h, which is the only place that knows
// about data folders, shipped resources and the user directory.

// Seek origins (match SEEK_SET/CUR/END semantics, backend-independent).
#define MIOPAN_SEEK_SET 0
#define MIOPAN_SEEK_CUR 1
#define MIOPAN_SEEK_END 2

// One-shot read of `size` bytes starting at `offset` from an already-resolved
// host path.  Zero-fills nothing; returns 1 if the stream opened (a short read
// stops early), 0 if the file could not be opened.
int MioPan_FileReadAt(const char *host_path, uint64_t offset, void *buf,
                      size_t size);

// Byte size of a host file, or -1 on failure.
int64_t MioPan_FileSize(const char *host_path);

// ---- handle-based stream API (backs fileio.cpp + libcdvd streaming) --------

typedef struct MioPan_File MioPan_File;

// `mode` is an fopen-style string ("rb", "wb", "r+b", "ab", "a+b", "w+b").
MioPan_File *MioPan_FileOpen(const char *host_path, const char *mode);
size_t       MioPan_FileRead(MioPan_File *f, void *buf, size_t size);
size_t       MioPan_FileWrite(MioPan_File *f, const void *buf, size_t size);
int          MioPan_FileSeek(MioPan_File *f, int64_t offset, int whence);
int64_t      MioPan_FileTell(MioPan_File *f);
int64_t      MioPan_FileStreamSize(MioPan_File *f);
void         MioPan_FileCloseHandle(MioPan_File *f);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_FILE_H */
