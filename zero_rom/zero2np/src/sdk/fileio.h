/* fileio.h - PS2 EE file I/O declarations for the PC syntax pass. */
#ifndef _FILEIO_H
#define _FILEIO_H

#include "scetypes.h"
#include "sifdev.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SCE_RDONLY   0x0001
#define SCE_WRONLY   0x0002
#define SCE_RDWR     0x0003
#define SCE_CREAT    0x0200
#define SCE_TRUNC    0x0400
#define SCE_APPEND   0x0100

#define SCE_SEEK_SET 0
#define SCE_SEEK_CUR 1
#define SCE_SEEK_END 2

int sceOpen(const char *filename, int flag);
int sceClose(int fd);
int sceRead(int fd, void *buf, int size);
int sceWrite(int fd, const void *buf, int size);
int sceLseek(int fd, int offset, int whence);

#ifdef __cplusplus
}
#endif

#endif /* _FILEIO_H */
