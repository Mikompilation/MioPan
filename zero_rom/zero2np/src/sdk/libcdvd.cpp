/* ==========================================================================
 *  libcdvd.cpp  (SCE CD/DVD library -- PC-port shim)
 * ======================================================================== */

#include "libcdvd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../miopan/os/miopan_time.h" // MioPan_GetLocalTime (RTC)
#include "../miopan/io/miopan_file.h" // host file I/O
#include "../miopan/io/miopan_paths.h" // the data folder

#define MIOPAN_CD_FILE_LSN_BASE 0x71000000u
#define MIOPAN_CD_MAX_FILES     64

typedef struct
{
    u_int lsn;
    u_int size;
    char  path[1024];
} MioPanCdFile;

static MioPanCdFile cd_files[MIOPAN_CD_MAX_FILES];
static u_int        cd_next_file_lsn = MIOPAN_CD_FILE_LSN_BASE;
static int          cd_last_error;
static MioPan_File  *cd_stream_file;
static sceCdCBFunc  cd_callback;

static u_char DecToBcd(u_char dec)
{
    return (u_char)(((dec / 10) << 4) | (dec % 10));
}

// Path resolution lives in miopan/io/miopan_paths (the disc is the data
// folder) and host reads in miopan/io/miopan_file.  ResolveDataFile /
// ReadFileBytes are thin adapters.

static int ResolveDataFile(const char *name, char *out, size_t out_size)
{
    return MioPan_PathResolveData(name, out, out_size);
}

static int ReadFileBytes(const char *path, uint64_t offset, void *buf, u_int size)
{
    if (MioPan_FileReadAt(path, offset, buf, (size_t)size) == 0)
    {
        cd_last_error = 1;
        return 0;
    }
    cd_last_error = 0;
    return 1;
}

static MioPanCdFile *FindFileByLsn(u_int lsn)
{
    int i;

    for (i = 0; i < MIOPAN_CD_MAX_FILES; i++)
    {
        if (cd_files[i].path[0] != '\0' && cd_files[i].lsn == lsn)
        {
            return &cd_files[i];
        }
    }

    return 0;
}

static MioPanCdFile *AddFileMapping(const char *path, u_int lsn, u_int size)
{
    int i;

    for (i = 0; i < MIOPAN_CD_MAX_FILES; i++)
    {
        if (cd_files[i].path[0] != '\0' && strcmp(cd_files[i].path, path) == 0)
        {
            return &cd_files[i];
        }
    }

    for (i = 0; i < MIOPAN_CD_MAX_FILES; i++)
    {
        if (cd_files[i].path[0] == '\0')
        {
            cd_files[i].lsn = lsn;
            cd_files[i].size = size;
            snprintf(cd_files[i].path, sizeof(cd_files[i].path), "%s", path);
            return &cd_files[i];
        }
    }

    return 0;
}

extern "C" {

int sceCdInit(int init_mode)
{
    (void)init_mode;
    return 1;
}

int sceCdMmode(int media)
{
    (void)media;
    return 1;
}

int sceCdReadClock(sceCdCLOCK *clock)
{
    MioPan_DateTime dt;
    int year;

    if (clock == 0)
    {
        return 0;
    }

    if (!MioPan_GetLocalTime(&dt))
    {
        memset(clock, 0, sizeof(*clock));
        clock->stat = 0x80;
        return 0;
    }

    year = dt.year - 2000;
    if (year < 0)
    {
        year = 0;
    }
    else if (year > 99)
    {
        year = 99;
    }

    clock->stat = 0;
    clock->second = DecToBcd((u_char)dt.second);
    clock->minute = DecToBcd((u_char)dt.minute);
    clock->hour = DecToBcd((u_char)dt.hour);
    clock->pad = 0;
    clock->day = DecToBcd((u_char)dt.day);
    clock->month = DecToBcd((u_char)dt.month);
    clock->year = DecToBcd((u_char)year);

    return 1;
}

/*
 * Say why a read failed.
 *
 * MyCdRead() answers every failure with the same "Disk Not Ready OR
 * ReadCommand Err" banner and then retries for ever, because on the real
 * machine the only way sceCdRead() refused a command was an open tray.  Here
 * there are three quite different causes and the banner cannot tell them
 * apart, so the infinite loop it produces says nothing about which one it is.
 * Rate-limited, because the caller is a spin loop.
 */
static void ReportCdFailure(const char *why, u_int lsn, u_int sectors,
                            const void *buf)
{
    static int reported;

    if (reported < 8)
    {
        reported++;
        printf("cd: read failed (%s) lsn=%u sectors=%u offset=%llu buf=%p\n",
               why, lsn, sectors, (unsigned long long)lsn * 0x800ull, buf);
    }
}

static int CdReadSectors(u_int lsn, u_int sectors, void *buf)
{
    char path[1024];
    MioPanCdFile *file;

    if (buf == 0)
    {
        ReportCdFailure("null destination", lsn, sectors, buf);
        cd_last_error = 1;
        return 0;
    }

    file = FindFileByLsn(lsn);
    if (file != 0)
    {
        if (ReadFileBytes(file->path, 0, buf, sectors * 0x800u) != 0)
        {
            return 1;
        }

        ReportCdFailure("mapped file short read", lsn, sectors, buf);
        return 0;
    }

    if (ResolveDataFile("\\IMG_BD.BIN;1", path, sizeof(path)) == 0)
    {
        ReportCdFailure("IMG_BD.BIN not found", lsn, sectors, buf);
        cd_last_error = 1;
        return 0;
    }

    /* 64-bit before the multiply.  IMG_BD.BIN is 2.47 GB today so a u_int
     * still holds every offset in it, but the product is one disc revision
     * away from wrapping, and a wrap reads the wrong sector silently instead
     * of failing. */
    if (ReadFileBytes(path, (uint64_t)lsn * 0x800ull, buf,
                      sectors * 0x800u) != 0)
    {
        return 1;
    }

    ReportCdFailure("short read past end of image", lsn, sectors, buf);
    return 0;
}

/* The IOP big kernel lock, declared by hand rather than including iop_host.h:
 * this file is shared with the EE side and that header carries IOP-side struct
 * names that collide there. */
extern "C" int  MioPan_IopBklSuspend(void);
extern "C" void MioPan_IopBklResume(int depth);

int sceCdRead(u_int lsn, u_int sectors, void *buf, sceCdRMode *mode)
{
    int ok;

    (void)mode;

    /* The drive was hardware: on the PS2 the IOP thread that asked for a read
     * sat in WaitSema(sema_Sync_CD) for its whole duration and every other IOP
     * thread kept running.  The read is a synchronous host read here, and since
     * iopReqRead() now performs it on the calling thread rather than handing it
     * to a reader thread, that thread is holding the IOP lock -- so give it
     * back for the read, or one stream's refill stalls every other IOP context
     * for the length of a file read. */
    int bkl = MioPan_IopBklSuspend();
    ok = CdReadSectors(lsn, sectors, buf);
    MioPan_IopBklResume(bkl);

    /* The read is synchronous here, but the IOP's MyCdRead() does not know
     * that: it arms sceCdCallback() and then blocks on the semaphore
     * cdvd_callback() signals.  Reporting completion is what releases it --
     * without this the IOP's reader thread parks on its first read. */
    if (cd_callback != 0)
    {
        cd_callback(SCECdFuncRead);
    }

    return ok;
}

int sceCdReadFile(const char *name, u_int offset, void *buf, u_int size)
{
    char path[1024];

    if (buf == 0 || name == 0 || ResolveDataFile(name, path, sizeof(path)) == 0)
    {
        cd_last_error = 1;
        return 0;
    }

    return ReadFileBytes(path, offset, buf, size);
}

int sceCdSync(int mode)
{
    (void)mode;
    return 0;
}

int sceCdBreak(void)
{
    return 1;
}

int sceCdGetError(void)
{
    return cd_last_error;
}

int sceCdStRead(u_int size, u_int *buf, u_int mode, u_int *err)
{
    u_char *dst;
    size_t want;
    size_t total;

    (void)mode;

    if (err != 0)
    {
        *err = 0;
    }

    if (cd_stream_file == 0 || buf == 0)
    {
        return 0;
    }

    dst = (u_char *)buf;
    want = (size_t)size * 0x800u;
    total = 0;
    while (total < want)
    {
        size_t got;

        got = MioPan_FileRead(cd_stream_file, dst + total, want - total);
        if (got == 0)
        {
            break;
        }
        total += got;
    }

    return (int)(total / 0x800u);
}

int sceCdStStop(void)
{
    if (cd_stream_file != 0)
    {
        MioPan_FileCloseHandle(cd_stream_file);
        cd_stream_file = 0;
    }

    return 1;
}

int sceCdDiskReady(int mode)
{
    (void)mode;
    return 2;
}

int sceCdStInit(u_int bufmax, u_int bankmax, u_int iop_bufaddr)
{
    (void)bufmax;
    (void)bankmax;
    (void)iop_bufaddr;
    return 1;
}

int sceCdSearchFile(sceCdlFILE *fp, const char *name)
{
    char path[1024];
    int64_t size64;
    u_int size;
    u_int lsn;

    if (fp == 0 || name == 0 || ResolveDataFile(name, path, sizeof(path)) == 0)
    {
        return 0;
    }

    size64 = MioPan_FileSize(path);
    if (size64 <= 0 || size64 > 0xffffffffll)
    {
        return 0;
    }

    size = (u_int)size64;
    if (strstr(path, "IMG_BD.BIN") != 0 || strstr(path, "img_bd.bin") != 0)
    {
        lsn = 0;
    }
    else
    {
        lsn = cd_next_file_lsn++;
    }

    AddFileMapping(path, lsn, size);

    memset(fp, 0, sizeof(*fp));
    fp->lsn = lsn;
    fp->size = size;
    snprintf(fp->name, sizeof(fp->name), "%s", name);

    return 1;
}

int sceCdStStart(u_int lbn, sceCdRMode *mode)
{
    MioPanCdFile *file;

    (void)mode;

    sceCdStStop();

    file = FindFileByLsn(lbn);
    if (file == 0)
    {
        return 0;
    }

    cd_stream_file = MioPan_FileOpen(file->path, "rb");
    return cd_stream_file != 0;
}

/* --------------------------------------------------------------------------
 *  IOP side (system/iop)
 * ------------------------------------------------------------------------ */

int sceCdSeek(u_int lsn)
{
    (void)lsn;

    /* Uncalled: every read on this path goes through MyCdRead(), which lets
     * the drive seek for itself. */
    return 1;
}

/* start() only prints the answer, so any PS2 disc will do. */
int sceCdGetDiskType(void)
{
    return SCECdPS2DVD;
}

sceCdCBFunc sceCdCallback(sceCdCBFunc func)
{
    sceCdCBFunc old = cd_callback;
    cd_callback = func;
    return old;
}

}
