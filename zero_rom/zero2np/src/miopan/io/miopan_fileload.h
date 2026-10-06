#ifndef MIOPAN_FILELOAD_H
#define MIOPAN_FILELOAD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// PC host-side file-load service.
//
// On the PS2 a queued load is dispatched to the IOP file server over SIF RPC.
// The PC port has no IOP, so the loader thread instead calls this synchronous
// host implementation, which reads (and, for compressed files, decodes) the
// data straight into emulated EE memory.  This logic lived in fileload.c as the
// HostFileLoad* family; it is port-specific glue with no PS2-original
// counterpart, so it lives here in the miopan layer.
//
// The caller (the reconstructed loader) resolves all game-specific facts
// (compression flag, file names, uncompressed size) from the CD data table and
// passes them in — this module deals only in host I/O and knows nothing about
// file_no / cddat.

typedef struct
{
    uintptr_t    ee_adrs;        // destination EE address (translated internally)
    int          request_size;   // requested transfer bytes (may be negative -> 0)
    int          start_sector;   // IMG_BD.BIN archive start sector
    const char  *cmp_file_name;  // named path (possibly ".cmp"-mangled)
    const char  *raw_file_name;  // plain extracted file name
    unsigned int raw_size;       // uncompressed byte size (GetFileSize)
    int          read_offset;    // byte offset already consumed (wrk->read_size)
    int          wrk_size;       // total request size (wrk->size)
    int          is_cmp;         // non-zero if the file is stored compressed
    int          is_spu;         // non-zero if ee_adrs is an SPU address, not EE
    int          file_no;        // diagnostics only (missing-file warning)
} MioPan_FileLoadReq;

// Serve one load request into emulated EE memory.  Tries, in order: the raw
// extracted file, the named (.cmp/raw) file, then the IMG_BD.BIN archive span;
// on total failure zero-fills the target and emits a capped diagnostic.
// Returns the number of bytes to report as read (to store in the RPC reply).
int MioPan_FileLoadServe(const MioPan_FileLoadReq *req);

#ifdef __cplusplus
}
#endif

#endif /* MIOPAN_FILELOAD_H */
