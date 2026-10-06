/* ==========================================================================
 *  fileio.cpp  (SCE EE file I/O -- PC-port shim)
 *
 *  Thin translation of the sceOpen/Read/Write/Lseek/Close API onto the miopan
 *  file wrapper (miopan/io/miopan_file) and the path module
 *  (miopan/io/miopan_paths).  All host file access and path resolution live
 *  there; this file only maps PS2 fd handles and open-flag semantics.
 *
 *  The ROM opens "host0:" for both, and the two halves go to different places.
 *  A read is game data.  A write is the developer output the "host0:" prefix
 *  meant -- save_data.dat from the debug save transfer, and the tuned light
 *  banks scn_test.c dumps -- and it belongs in the user directory: the data
 *  folder can be a read-only mount, a disc image's neighbour, or on Android not
 *  a filesystem path at all.
 * ======================================================================== */

#include "fileio.h"

#include <stddef.h>

#include "../miopan/io/miopan_file.h"
#include "../miopan/io/miopan_paths.h"

#define MIOPAN_MAX_OPEN_FILES 64

static MioPan_File *open_files[MIOPAN_MAX_OPEN_FILES];

static const char *ModeFromFlag(int flag)
{
    if ((flag & SCE_RDWR) == SCE_RDWR)
    {
        if ((flag & SCE_APPEND) != 0)
        {
            return "a+b";
        }
        if ((flag & SCE_TRUNC) != 0)
        {
            return "w+b";
        }
        return "r+b";
    }

    if ((flag & SCE_WRONLY) != 0)
    {
        if ((flag & SCE_APPEND) != 0)
        {
            return "ab";
        }
        return "wb";
    }

    return "rb";
}

static int AllocFileHandle(MioPan_File *fp)
{
    int i;

    for (i = 0; i < MIOPAN_MAX_OPEN_FILES; i++)
    {
        if (open_files[i] == 0)
        {
            open_files[i] = fp;
            return i + 1;
        }
    }

    return -1;
}

static MioPan_File *GetFileHandle(int fd)
{
    if (fd <= 0 || fd > MIOPAN_MAX_OPEN_FILES)
    {
        return 0;
    }

    return open_files[fd - 1];
}

extern "C" {

int sceOpen(const char *filename, int flag)
{
    char         path[1024];
    MioPan_File *fp;
    int          fd;
    int          write_mode;

    write_mode = ((flag & SCE_WRONLY) != 0) || ((flag & SCE_RDWR) == SCE_RDWR) ||
                 ((flag & SCE_CREAT) != 0) || ((flag & SCE_TRUNC) != 0);

    if (write_mode)
    {
        if (MioPan_PathPrepareUser(filename, path, sizeof(path)) == 0)
        {
            return -1;
        }
    }
    else if (MioPan_PathResolveData(filename, path, sizeof(path)) == 0)
    {
        /* Not in the data folder.  It may be something this port wrote there
         * earlier: LoadPCFile() reads back exactly the save_data.dat that
         * SavePCFile() produced.  Resolve rather than prepare -- a read that
         * misses must not leave a directory behind, and the loader misses often
         * by design. */
        if (MioPan_PathResolveUser(filename, path, sizeof(path)) == 0)
        {
            return -1;
        }
    }

    fp = MioPan_FileOpen(path, ModeFromFlag(flag));
    if (fp == 0 && (flag & SCE_CREAT) != 0)
    {
        fp = MioPan_FileOpen(path, "w+b");
    }
    if (fp == 0)
    {
        return -1;
    }

    fd = AllocFileHandle(fp);
    if (fd < 0)
    {
        MioPan_FileCloseHandle(fp);
    }

    return fd;
}

int sceClose(int fd)
{
    MioPan_File *fp;

    fp = GetFileHandle(fd);
    if (fp == 0)
    {
        return -1;
    }

    MioPan_FileCloseHandle(fp);
    open_files[fd - 1] = 0;
    return 0;
}

int sceRead(int fd, void *buf, int size)
{
    MioPan_File *fp;

    fp = GetFileHandle(fd);
    if (fp == 0 || buf == 0 || size < 0)
    {
        return -1;
    }

    return (int)MioPan_FileRead(fp, buf, (size_t)size);
}

int sceWrite(int fd, const void *buf, int size)
{
    MioPan_File *fp;

    fp = GetFileHandle(fd);
    if (fp == 0 || buf == 0 || size < 0)
    {
        return -1;
    }

    return (int)MioPan_FileWrite(fp, buf, (size_t)size);
}

int sceLseek(int fd, int offset, int whence)
{
    MioPan_File *fp;
    int          origin;

    fp = GetFileHandle(fd);
    if (fp == 0)
    {
        return -1;
    }

    if (whence == SCE_SEEK_CUR)
    {
        origin = MIOPAN_SEEK_CUR;
    }
    else if (whence == SCE_SEEK_END)
    {
        origin = MIOPAN_SEEK_END;
    }
    else
    {
        origin = MIOPAN_SEEK_SET;
    }

    if (MioPan_FileSeek(fp, offset, origin) != 0)
    {
        return -1;
    }

    return (int)MioPan_FileTell(fp);
}

}
