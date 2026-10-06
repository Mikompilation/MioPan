/* ==========================================================================
 *  sdk/libmc.h  (SCE PS2 memory-card library)
 *
 *  Only the eleven entry points the reconstructed system/mc modules reach are
 *  declared: sceMcInit, sceMcSync, sceMcGetInfo, sceMcGetDir, sceMcOpen,
 *  sceMcClose, sceMcRead, sceMcWrite, sceMcMkdir, sceMcDelete, sceMcFormat.
 *  ZERO2.MAP links the rest of libmc.a in (sceMcSeek, sceMcFlush, sceMcRename,
 *  sceMcChdir, sceMcUnformat, sceMcGetEntSpace, sceMcSetFileInfo, ...) but a
 *  scan of the loadable segments finds no call site for any of them.
 *
 *  The library is asynchronous: every request returns immediately with 0 when
 *  it was accepted, and the caller then polls sceMcSync() until it reports
 *  sceMcExecFinish and hands back the operation's own result code.  Every
 *  system/mc module is a step machine built around exactly that handshake.
 * ======================================================================== */

#ifndef _SDK_LIBMC_H
#define _SDK_LIBMC_H

/* ---- sceMcSync() progress ---------------------------------------------- */
#define sceMcExecRun     0  /* the request is still running                 */
#define sceMcExecFinish  1  /* it finished; *result holds its result code   */
#define sceMcExecIdle   -1  /* nothing is running -- the request never took */

/* ---- operation result codes (sceMcSync's *result) --------------------- */
#define sceMcResSucceed         0
#define sceMcResChangedCard    -1   /* card swapped since the last access   */
#define sceMcResNoFormat       -2
#define sceMcResFullDevice     -3
#define sceMcResNoEntry        -4
#define sceMcResDeniedPermit   -5
#define sceMcResNotEmpty       -6
#define sceMcResUpLimitHandle  -7
#define sceMcResFailReplace    -8

/* ---- request rejection codes (the sceMcXxx return value) --------------- */
#define sceMcErrNotInit      -100   /* sceMcInit() was never called         */
#define sceMcErrBusy         -200   /* another request is still running     */
#define sceMcErrPortSlot     -201
#define sceMcErrEntryName    -210   /* malformed file or directory name     */

/* ---- sceMcOpen() mode bits --------------------------------------------- */
#define sceMcFileAttrReadable    0x0001
#define sceMcFileAttrWriteable   0x0002
#define sceMcFileAttrExecutable  0x0004
#define sceMcFileAttrDupProhibit 0x0008
#define sceMcFileAttrFile        0x0010
#define sceMcFileAttrSubdir      0x0020
#define sceMcFileCreateDir       0x0040
#define sceMcFileAttrClosed      0x0080
#define sceMcFileCreateFile      0x0200
#define sceMcFileAttrPDAExec     0x0800
#define sceMcFileAttrPS1         0x1000
#define sceMcFileAttrHidden      0x2000

/* ---- card type (sceMcGetInfo's *type) ---------------------------------- */
#define sceMcTypePS1  1
#define sceMcTypePS2  2
#define sceMcTypePDA  3

#ifdef __cplusplus
extern "C" {
#endif

typedef struct                          /* 0x8 */
{
    /* 0x0 */ unsigned char  Resv2;
    /* 0x1 */ unsigned char  Sec;
    /* 0x2 */ unsigned char  Min;
    /* 0x3 */ unsigned char  Hour;
    /* 0x4 */ unsigned char  Day;
    /* 0x5 */ unsigned char  Month;
    /* 0x6 */ unsigned short Year;
} sceMcStDateTime;

/* One entry out of sceMcGetDir().  0x40 bytes on the EE, and mc_check_dir.c's
 * MC_DIR_INFO reserves eighteen of them (0x480), so the layout has to hold.
 *
 * The 64-byte alignment is load-bearing, not decoration: the IOP DMAs the
 * listing straight into this buffer.  It is also what makes MC_DIR_INFO come
 * out at the ROM's 0x4c0 rather than 0x484 -- with 18 entries plus a short, the
 * struct's own alignment supplies 0x3c bytes of tail, and ZERO2.MAP shows the
 * same 0x3c gap in front of mc_dir_info in mc_check_dir.o's .bss. */
typedef struct alignas(64)              /* 0x40 */
{
    /* 0x00 */ sceMcStDateTime _Create;
    /* 0x08 */ sceMcStDateTime _Modify;
    /* 0x10 */ unsigned int    FileSizeByte;
    /* 0x14 */ unsigned short  AttrFile;
    /* 0x16 */ unsigned short  Reserve1;
    /* 0x18 */ unsigned int    Reserve2;
    /* 0x1c */ unsigned int    PdaAplNo;
    /* 0x20 */ unsigned char   EntryName[32];
} sceMcTblGetDir;

/* icon.sys.  The game builds one of these in MemoryCardSetIconSysData() and
 * writes it out verbatim, so every field offset matters. */
typedef int   _iconVu0IVECTOR[4];
typedef float _iconVu0FVECTOR[4];

typedef struct                          /* 0x3c4 */
{
    /* 0x000 */ unsigned char   Head[4];        /* "PS2D"                   */
    /* 0x004 */ unsigned short  Reserv1;
    /* 0x006 */ unsigned short  OffsLF;         /* line-break offset in the title */
    /* 0x008 */ unsigned int    Reserv2;
    /* 0x00c */ unsigned int    TransRate;
    /* 0x010 */ _iconVu0IVECTOR BgColor[4];
    /* 0x050 */ _iconVu0FVECTOR LightDir[3];
    /* 0x080 */ _iconVu0FVECTOR LightColor[3];
    /* 0x0b0 */ _iconVu0FVECTOR Ambient;
    /* 0x0c0 */ unsigned char   TitleName[68];
    /* 0x104 */ unsigned char   FnameView[64];
    /* 0x144 */ unsigned char   FnameCopy[64];
    /* 0x184 */ unsigned char   FnameDel[64];
    /* 0x1c4 */ unsigned char   Reserve3[512];
} sceMcIconSys;

int sceMcInit(void);

/* mode 0 blocks until the running request finishes, mode 1 polls.  Returns
 * one of the sceMcExec* values above; `cmd` (unused by this game, always 0)
 * would receive the function code and `result` the operation's result. */
int sceMcSync(int mode, int *cmd, int *result);

int sceMcGetInfo(int port, int slot, int *type, int *free, int *format);
int sceMcGetDir(int port, int slot, const char *name, unsigned int mode,
                int maxent, sceMcTblGetDir *table);
int sceMcOpen(int port, int slot, const char *name, int mode);
int sceMcClose(int fd);
int sceMcRead(int fd, void *buff, int size);
int sceMcWrite(int fd, const void *buff, int size);
int sceMcMkdir(int port, int slot, const char *name);
int sceMcDelete(int port, int slot, const char *name);
int sceMcFormat(int port, int slot);

#ifdef __cplusplus
}
#endif

#endif /* _SDK_LIBMC_H */
