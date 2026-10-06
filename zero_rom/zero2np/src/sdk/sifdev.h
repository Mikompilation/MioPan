/* sifdev.h — PS2 SIF device / CD-ROM directory interface — PC-port declarations.
 *
 * FF2 reads the disc directory through the classic sce_dirent + sceDopen/
 * sceDread/sceDclose API (the FF1 port uses the newer sceCd* set instead, so
 * these are declared here from the prototype's call sites + the PS2 SDK
 * <sifdev.h>).  Disc I/O must be ported to host file I/O at link time.
 */
#ifndef _SIFDEV_H
#define _SIFDEV_H

#include "scetypes.h"

/* File status returned inside a directory entry. */
struct sce_stat {
    u_int  st_mode;
    u_int  st_attr;
    u_int  st_size;
    u_char st_ctime[8];
    u_char st_atime[8];
    u_char st_mtime[8];
    u_int  st_hisize;
    u_int  st_private[6];
};

/* One CD/DVD directory entry. */
struct sce_dirent {
    struct sce_stat d_stat;
    char            d_name[256];
    void           *d_private;
};
typedef struct sce_stat   sce_stat;
typedef struct sce_dirent sce_dirent;

#ifdef __cplusplus
extern "C" {
#endif

int sceDopen(const char *name);
int sceDread(int fd, sce_dirent *buf);
int sceDclose(int fd);

/* ---- IOP bring-up: reboot, loadfile and iopheap ------------------------
 *
 * These are the <sifdev.h> / <sifcmd.h> entry points ee_iop.c uses to reboot
 * the IOP and push the game's IRX modules into it.  There is no IOP in the
 * port, so the shims succeed and do nothing -- see the note on
 * ee_iop_boot_iop in system/eeiop/ee_iop.c. */

/* Reboot the IOP from an IOPRP image; non-zero on success. */
int  sceSifRebootIop(const char *img);
/* Non-zero once the reboot has completed. */
int  sceSifSyncIop(void);

void sceSifLoadFileReset(void);
void sceFsReset(void);

/* Start an IRX already resident in IOP memory; < 0 on failure. */
int  sceSifLoadModuleBuffer(void *addr, int arglen, const char *args);

void         sceSifInitIopHeap(void);
/* sceSifAllocSysMemory (mode 1 = anywhere, 2 = at `addr`) is in sif.h. */
/* IOP-side allocation; the result is an IOP address, so callers keep it in an
 * int rather than a pointer.  NULL on failure -- movie.c's iopalloc() hangs on
 * that, so the shim must not fail. */
void        *sceSifAllocIopHeap(unsigned int size);
int          sceSifFreeIopHeap(void *addr);
/* Read `name` straight into IOP memory at `addr`; < 0 on failure. */
int          sceSifLoadIopHeap(const char *name, void *addr);
unsigned int sceSifQueryMaxFreeMemSize(void);
unsigned int sceSifQueryTotalFreeMemSize(void);

#ifdef __cplusplus
}
#endif

#endif /* _SIFDEV_H */
