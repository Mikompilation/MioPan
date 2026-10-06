/* libcdvd.h - PS2 CD/DVD drive library - PC-port shim. */
#ifndef _LIBCDVD_H
#define _LIBCDVD_H

#include "scetypes.h"

#define STMNBLK 0
#define STMBLK  1

#define SCECdINIT 0
#define SCECdINoD 1
#define SCECdEXIT 5

#define SCECdCD  1
#define SCECdDVD 2

#define SCECdSpinMax    0
#define SCECdSpinNom    1
#define SCECdSpinStm    0
#define SCECdSpinDvdDL0 0
#define SCECdSpinX1     2

#define SCECdSecS2048 0
#define SCECdSecS2328 1
#define SCECdSecS2340 2

/* ---- IOP side (system/iop) --------------------------------------------
 * iopsys.irx drives the drive directly rather than through the EE's
 * sceCdReadFile()/sceCdStRead() path, so it needs the raw command set and the
 * disc-type / error codes MyCdRead() and start() test. */

/* sceCdDiskReady() mode, and the result meaning "ready". */
#define SCECdBlock          0x00
#define SCECdNonblock       0x01
#define SCECdComplete       0x02

/* sceCdGetDiskType() results. */
#define SCECdNODISC         0x00
#define SCECdDETCT          0x01
#define SCECdPSCD           0x10
#define SCECdPSCDDA         0x11
#define SCECdPS2CD          0x12
#define SCECdPS2CDDA        0x13
#define SCECdPS2DVD         0x14
#define SCECdCDDA           0xfd
#define SCECdDVDV           0xfe
#define SCECdIllgalMedia    0xff

/* sceCdGetError() results -- only the two MyCdRead() tests. */
#define SCECdErNO           0x00
#define SCECdErIPI          0x20

/* sceCdCallback() reasons: the completed command's function code. */
#define SCECdFuncRead       1
#define SCECdFuncSeek       4
#define SCECdFuncStandby    5
#define SCECdFuncStop       6
#define SCECdFuncPause      7

/* RTC clock read back from the drive mechacon.  Fields are BCD-encoded. */
typedef struct                      /* 0x8 */
{
    /* 0x0 */ u_char stat;
    /* 0x1 */ u_char second;
    /* 0x2 */ u_char minute;
    /* 0x3 */ u_char hour;
    /* 0x4 */ u_char pad;
    /* 0x5 */ u_char day;
    /* 0x6 */ u_char month;
    /* 0x7 */ u_char year;
} sceCdCLOCK;

typedef struct                      /* 0x4 */
{
    /* 0x0 */ u_char trycount;
    /* 0x1 */ u_char spindlctrl;
    /* 0x2 */ u_char datapattern;
    /* 0x3 */ u_char pad;
} sceCdRMode;

typedef struct
{
    /* 0x000 */ u_int  lsn;
    /* 0x004 */ u_int  size;
    /* 0x008 */ char   name[1024];
    /* 0x408 */ u_char date[8];
    /* 0x410 */ u_int  flag;
} sceCdlFILE;

#ifdef __cplusplus
extern "C" {
#endif

int sceCdInit(int init_mode);
int sceCdMmode(int media);
int sceCdReadClock(sceCdCLOCK *clock);
int sceCdRead(u_int lsn, u_int sectors, void *buf, sceCdRMode *mode);
int sceCdReadFile(const char *name, u_int offset, void *buf, u_int size);
int sceCdSync(int mode);
int sceCdBreak(void);
int sceCdGetError(void);
int sceCdStRead(u_int size, u_int *buf, u_int mode, u_int *err);
int sceCdStStop(void);
int sceCdDiskReady(int mode);
int sceCdStInit(u_int bufmax, u_int bankmax, u_int iop_bufaddr);
int sceCdSearchFile(sceCdlFILE *fp, const char *name);
int sceCdStStart(u_int lbn, sceCdRMode *mode);

/* ---- IOP side ---------------------------------------------------------- */
typedef void (*sceCdCBFunc)(int reason);

int         sceCdSeek(u_int lsn);
int         sceCdGetDiskType(void);
/* Installs the drive's command-completion callback.  MyCdRead() arms this and
 * then blocks on the semaphore cdvd_callback() signals, so a read that never
 * reports completion parks the IOP's reader thread forever. */
sceCdCBFunc sceCdCallback(sceCdCBFunc func);

#ifdef __cplusplus
}
#endif

#endif /* _LIBCDVD_H */
