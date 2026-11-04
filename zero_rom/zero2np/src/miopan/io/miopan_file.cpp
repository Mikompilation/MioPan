#include "miopan_file.h"

#include <stdlib.h>

#include <SDL3/SDL_iostream.h>

#include <string>

namespace
{
/* See MioPan_FileMapRegion().  Written before any reader exists, so the readers
 * need no lock. */
struct Region
{
    std::string name;
    std::string host_path;
    uint64_t    offset = 0;
    uint64_t    size = 0;
};
Region g_region;

bool IsRegion(const char *path)
{
    return !g_region.name.empty() && g_region.name == path;
}
}

extern "C" {

void MioPan_FileMapRegion(const char *name, const char *host_path, uint64_t offset,
                          uint64_t size)
{
    g_region.name = name;
    g_region.host_path = host_path;
    g_region.offset = offset;
    g_region.size = size;
}

int MioPan_FileReadAt(const char *host_path, uint64_t offset, void *buf, size_t size)
{
    SDL_IOStream *io;
    unsigned char *dst;
    size_t         remaining;
    size_t         readable = size;

    if (host_path == 0 || buf == 0)
    {
        return 0;
    }

    /* The end of a region is the end of the file: past it, a read comes up
     * short here just as it would on an IMG_BD.BIN of its own. */
    if (IsRegion(host_path))
    {
        const uint64_t left = offset < g_region.size ? g_region.size - offset : 0;
        readable = (uint64_t)size < left ? size : (size_t)left;
        host_path = g_region.host_path.c_str();
        offset += g_region.offset;
    }

    io = SDL_IOFromFile(host_path, "rb");
    if (io == 0)
    {
        return 0;
    }

    if (offset != 0 && SDL_SeekIO(io, (Sint64)offset, SDL_IO_SEEK_SET) < 0)
    {
        SDL_CloseIO(io);
        return 0;
    }

    dst = (unsigned char *)buf;
    remaining = readable;
    while (remaining > 0)
    {
        size_t got = SDL_ReadIO(io, dst, remaining);
        if (got == 0)
        {
            break;
        }
        dst += got;
        remaining -= got;
    }

    SDL_CloseIO(io);

    // A short read means the caller did NOT get the bytes it asked for -- the
    // offset was past EOF, or the file is truncated.  Reporting success here
    // makes the loader believe the buffer is valid, so it skips its zero-fill
    // and "file load missing" diagnostic and the game silently parses zeros.
    if (remaining != 0 || readable != size)
    {
        return 0;
    }

    return 1;
}

int64_t MioPan_FileSize(const char *host_path)
{
    SDL_IOStream *io;
    Sint64        size;

    if (host_path == 0)
    {
        return -1;
    }

    if (IsRegion(host_path))
    {
        return (int64_t)g_region.size;
    }

    io = SDL_IOFromFile(host_path, "rb");
    if (io == 0)
    {
        return -1;
    }

    size = SDL_GetIOSize(io);
    SDL_CloseIO(io);
    return (int64_t)size;
}

// ---- handle-based stream API -----------------------------------------------

struct MioPan_File
{
    SDL_IOStream *io;
    /* Opened by a region's name: where it starts in `io`, and its length. */
    int      is_region;
    int64_t  region_offset;
    int64_t  region_size;
};

MioPan_File *MioPan_FileOpen(const char *host_path, const char *mode)
{
    if (host_path == 0 || mode == 0)
    {
        return 0;
    }

    const int is_region = IsRegion(host_path);
    SDL_IOStream *io = is_region ? SDL_IOFromFile(g_region.host_path.c_str(), "rb")
                                 : SDL_IOFromFile(host_path, mode);
    if (io == 0)
    {
        return 0;
    }

    MioPan_File *f = (MioPan_File *)malloc(sizeof(MioPan_File));
    if (f == 0)
    {
        SDL_CloseIO(io);
        return 0;
    }
    f->io = io;
    f->is_region = is_region;
    f->region_offset = is_region ? (int64_t)g_region.offset : 0;
    f->region_size = is_region ? (int64_t)g_region.size : 0;
    if (is_region && SDL_SeekIO(io, f->region_offset, SDL_IO_SEEK_SET) < 0)
    {
        MioPan_FileCloseHandle(f);
        return 0;
    }
    return f;
}

size_t MioPan_FileRead(MioPan_File *f, void *buf, size_t size)
{
    if (f == 0 || f->io == 0 || buf == 0)
    {
        return 0;
    }
    if (f->is_region)
    {
        const int64_t left =
            f->region_offset + f->region_size - (int64_t)SDL_TellIO(f->io);
        if (left <= 0)
        {
            return 0;
        }
        if ((uint64_t)size > (uint64_t)left)
        {
            size = (size_t)left;
        }
    }
    return SDL_ReadIO(f->io, buf, size);
}

size_t MioPan_FileWrite(MioPan_File *f, const void *buf, size_t size)
{
    if (f == 0 || f->io == 0 || buf == 0)
    {
        return 0;
    }
    return SDL_WriteIO(f->io, buf, size);
}

int MioPan_FileSeek(MioPan_File *f, int64_t offset, int whence)
{
    SDL_IOWhence w;

    if (f == 0 || f->io == 0)
    {
        return -1;
    }

    if (whence == MIOPAN_SEEK_CUR)
    {
        w = SDL_IO_SEEK_CUR;
    }
    else if (whence == MIOPAN_SEEK_END)
    {
        w = SDL_IO_SEEK_END;
    }
    else
    {
        w = SDL_IO_SEEK_SET;
    }

    /* Either end of a region is somewhere in the file it is a part of. */
    if (f->is_region && w != SDL_IO_SEEK_CUR)
    {
        offset += f->region_offset + (w == SDL_IO_SEEK_END ? f->region_size : 0);
        w = SDL_IO_SEEK_SET;
    }

    return (SDL_SeekIO(f->io, (Sint64)offset, w) < 0) ? -1 : 0;
}

int64_t MioPan_FileTell(MioPan_File *f)
{
    if (f == 0 || f->io == 0)
    {
        return -1;
    }
    return (int64_t)SDL_TellIO(f->io) - f->region_offset;
}

int64_t MioPan_FileStreamSize(MioPan_File *f)
{
    if (f == 0 || f->io == 0)
    {
        return -1;
    }
    return f->is_region ? f->region_size : (int64_t)SDL_GetIOSize(f->io);
}

void MioPan_FileCloseHandle(MioPan_File *f)
{
    if (f == 0)
    {
        return;
    }
    if (f->io != 0)
    {
        SDL_CloseIO(f->io);
    }
    free(f);
}

}
