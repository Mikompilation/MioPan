#include "miopan_file.h"

#include <stdlib.h>

#include <SDL3/SDL_iostream.h>

extern "C" {

int MioPan_FileReadAt(const char *host_path, uint64_t offset, void *buf, size_t size)
{
    SDL_IOStream *io;
    unsigned char *dst;
    size_t         remaining;

    if (host_path == 0 || buf == 0)
    {
        return 0;
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
    remaining = size;
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
    if (remaining != 0)
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
};

MioPan_File *MioPan_FileOpen(const char *host_path, const char *mode)
{
    if (host_path == 0 || mode == 0)
    {
        return 0;
    }

    SDL_IOStream *io = SDL_IOFromFile(host_path, mode);
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
    return f;
}

size_t MioPan_FileRead(MioPan_File *f, void *buf, size_t size)
{
    if (f == 0 || f->io == 0 || buf == 0)
    {
        return 0;
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

    return (SDL_SeekIO(f->io, (Sint64)offset, w) < 0) ? -1 : 0;
}

int64_t MioPan_FileTell(MioPan_File *f)
{
    if (f == 0 || f->io == 0)
    {
        return -1;
    }
    return (int64_t)SDL_TellIO(f->io);
}

int64_t MioPan_FileStreamSize(MioPan_File *f)
{
    if (f == 0 || f->io == 0)
    {
        return -1;
    }
    return (int64_t)SDL_GetIOSize(f->io);
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
