// FILE: /home/zero_rom/zero2np/src/system/os/eecdvd.c
//
// CD/DVD file-load front end.  Thin convenience layer over the lower file
// loader (FileLoadReqEE / FileLoadIsEnd / GetFileSize / AllFileLoadIsEnd):
//
//   * LoadReq / LoadReqGetAddr - size a file, then queue a load at a fixed
//                     address (priority 6).  LoadReqGetAddr also returns the
//                     next free address (input addr + the 64-byte-aligned
//                     size) so callers can pack several files back to back.
//   * The *_L variants (FileLoadReqEE_L / FileLoadIsEnd2_L / GetFileSize_L)
//                     bias the file number by the current language index
//                     (GetLanguage()), i.e. they select the localized variant
//                     of a file id.
//   * IsLoadEnd / IsLoadEndAll - completion polling.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "eecdvd.h"                 // this file's public API
#include "system.h"                 // GetLanguage()
#include "../eeiop/cddat.h"         // GetFileSize
#include "../eeiop/fileload.h"      // FileLoadReqEE / FileLoadIsEnd / AllFileLoadIsEnd
#include "../../common/utility2.h"  // GetAlignUp

// ──────────────────────────────────────────────────────────────────────
// Queue a load of file `file_no` to `addr` at the default priority (6).
// Returns the loader id, or -1 if the file does not exist.

int LoadReq(int file_no, uintptr_t addr)
{
    unsigned int size;

    size = GetFileSize(file_no);
    if (size != 0)
    {
        return FileLoadReqEE(file_no, (void *)addr, 6, 0, (void *)0);
    }

    return -1;
}

// ──────────────────────────────────────────────────────────────────────
// As LoadReq, but report the load id through *id (when non-NULL) and return
// the next free address: addr + the file size rounded up to 64 bytes.  A
// missing file yields id == -1 but the address still advances by 0.

uintptr_t LoadReqGetAddr(int file_no, uintptr_t addr, int *id)
{
    unsigned int size;
    int          ret_id;

    size = GetFileSize(file_no);
    size = GetAlignUp(size, 6);     // round up to a multiple of 64 bytes

    ret_id = -1;
    if (size != 0)
    {
        ret_id = FileLoadReqEE(file_no, (void *)addr, 6, 0, (void *)0);
    }

    if (id != (int *)0)
    {
        *id = ret_id;
    }

    return size + addr;
}

// ──────────────────────────────────────────────────────────────────────
// Sound-effect load request.  Stubbed out in this prototype build.

int LoadReqSe(int file_no, unsigned char se_type)
{
    return 0;
}

// ──────────────────────────────────────────────────────────────────────
// Completion polling.

int IsLoadEndAll(void)
{
    return AllFileLoadIsEnd();
}

int IsLoadEnd(int id)
{
    return FileLoadIsEnd(id);
}

// ──────────────────────────────────────────────────────────────────────
// Language-aware wrappers: bias the file number by the current language
// index so a single logical id resolves to its localized file.

int FileLoadReqEE_L(int file_no, void *adrs, int priority,
                    void (*func)(void *, void *), void *arg)
{
    u_char language = GetLanguage();
    return FileLoadReqEE(file_no + (char)language, adrs, priority, func, arg);
}

int FileLoadIsEnd2_L(int file_no, void *adrs)
{
    u_char language = GetLanguage();
    return FileLoadIsEnd2(file_no + (char)language, adrs);
}

unsigned int GetFileSize_L(int file_no)
{
    u_char language = GetLanguage();
    return GetFileSize(file_no + (char)language);
}
