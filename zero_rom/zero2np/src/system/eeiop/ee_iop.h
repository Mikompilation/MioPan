/* ==========================================================================
 *  system/eeiop/ee_iop.h
 *
 *  Public interface for the EE<->IOP bring-up layer (ee_iop.c).  EEIOP_DEF is
 *  the definition block InitSystemON() fills in and hands to ee_iopInit(): the
 *  EE-side allocators, the IOP module / HIL / DIL image names, and the per-
 *  subsystem work-block counts and load priorities.  ee_iopMain() pumps the
 *  EE<->IOP command queue.
 *
 *  EEIOP_DEF's layout and the IOP command ids are verbatim from the
 *  prototype's debug info (types.txt).  The module-loader internals of
 *  ee_iop.c are not declared here.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_EE_IOP_H
#define _SYSTEM_EEIOP_EE_IOP_H

/* IOP command-queue request ids (the command byte iopCommandRegister posts). */
typedef enum _IOP_COMMAND_ENUM
{
    IOP_COM_END                 = 0,
    REQ_IOP_REBOOT              = 1,
    REQ_CDVD_INIT               = 2,
    REQ_SET_CD_DAT              = 3,
    REQ_CD_READ_MODE_CHANGE     = 4,
    REQ_IOP_SND_INIT            = 5,
    REQ_SET_SND_EFFECT          = 6,
    REQ_VOICE_STOP              = 7,
    REQ_VOICE_LOOP_SET          = 8,
    REQ_STREAM_CREATE           = 9,
    REQ_STREAM_RELEASE          = 10,
    REQ_STREAM_START            = 11,
    REQ_STREAM_PLAY             = 12,
    REQ_STREAM_STOP             = 13,
    REQ_STREAM_ABORT            = 14,
    REQ_STREAM_PAUSE            = 15,
    REQ_STREAM_RESTART          = 16,
    REQ_STREAM_SETVOL           = 17,
    REQ_STREAM_SETPITCH         = 18,
    REQ_SB_INIT                 = 19,
    REQ_SB_PLAY                 = 20,
    REQ_SB_STOP                 = 21,
    REQ_SB_PAUSE                = 22,
    REQ_SB_RESTART              = 23,
    REQ_SB_SETVOL               = 24,
    REQ_SB_SETPITCH             = 25,
    REQ_PCM_STREAMCREATE        = 26,
    REQ_PCM_STREAMINIT          = 27,
    REQ_PCM_STREAMSTART         = 28,
    REQ_PCM_STREAMPLAY          = 29,
    REQ_PCM_STREAMSTOP          = 30,
    REQ_PCM_STREAMPAUSE         = 31,
    REQ_PCM_STREAMRESTART       = 32,
    REQ_PCM_STREAMSETVOL        = 33,
    IOP_COMMAND_ENUM_FORCE_DWORD = -1
} IOP_COMMAND_ENUM;

typedef struct                      /* 0x3c */
{
    /* 0x00 */ void *(*malloc64)(int size);      /* EE allocator (64-byte aligned) */
    /* 0x04 */ void  (*free64)(void *adrs);
    /* 0x08 */ int    command_send_buffer_size;
    /* 0x0c */ char  *iop_def_module;           /* IOPRP image                    */
    /* 0x10 */ char  *hil_file_name;            /* IRX host image list            */
    /* 0x14 */ char  *dil_file_name;            /* IRX disc image list            */
    /* 0x18 */ int    media;
    /* 0x1c */ int    file_load_wrk_num;        /* file-loader request slots      */
    /* 0x20 */ int    snd_bank_wrk_num;
    /* 0x24 */ int    stream_auto_wrk_num;
    /* 0x28 */ int    auto_bd_wrk_num;
    /* 0x2c */ int    snd_stream_load_priority;
    /* 0x30 */ int    snd_bank_load_priority;
    /* 0x34 */ int    cmp_use_flg;              /* enable the EE decode path      */
    /* 0x38 */ int    rom_boot;
} EEIOP_DEF;

/* The HIL image is an index over the DIL: a header giving the module count
 * and the DIL's size, then one record per IRX. */
typedef struct _HIL_FORMAT          /* 0x10 */
{
    /* 0x0 */ int num;
    /* 0x4 */ int dil_size;
    /* 0x8 */ int padding[2];
} HIL_FORMAT;

typedef struct _HIL_ONE_FORMAT      /* 0x10 */
{
    /* 0x0 */ char irx_name[8];     /* not NUL-terminated */
    /* 0x8 */ int  offset;          /* into the DIL image */
    /* 0xc */ int  size;
} HIL_ONE_FORMAT;

typedef struct _CD_READ_MODE_CHANGE /* 0x4  REQ_CD_READ_MODE_CHANGE */
{
    /* 0x0 */ int mode;
} CD_READ_MODE_CHANGE;

/* Per-stream state the IOP writes back into iop_ret every frame. */
typedef struct _IOP_STREAM_RET      /* 0x8 */
{
    /* 0x0 */ int status;           /* an EEIOP_STREAM_STATUS */
    /* 0x4 */ int offset;
} IOP_STREAM_RET;

/* The whole status block the IOP DMAs back.  `voice_end` is a bit per voice,
 * set once that voice has run past its end point. */
typedef struct _IOP_RET_STATUS      /* 0x1a8 */
{
    /* 0x000 */ int            voice_end[2];
    /* 0x008 */ IOP_STREAM_RET stream_ret[2];
    /* 0x018 */ IOP_STREAM_RET pcm_stream_ret[2];
    /* 0x028 */ int            mpLoopAdrs[2][24];
    /* 0x0e8 */ int            mpNowAdrs[2][24];
} IOP_RET_STATUS;

void *ee_iopMalloc(int size);
void  ee_iopFree(void *adrs);
int   ee_iopGetNeedSize(EEIOP_DEF *def);
int   ee_iopInit(EEIOP_DEF *def);
void  ee_iopMain(void);
/* Spins until the previous frame's RPC has completed. */
void  WaitMainRpc(void);
void  iopCommandFrameInit(void);
int   iopCommandRegister(IOP_COMMAND_ENUM command, char *buf, int size2);

void  SetCDReadMode(int mode);

/* Accessors over the IOP's status block. */
int             CheckEndPointThrough(int core, int voice_no);
IOP_STREAM_RET *GetStreamWrkRet(int wrk_id);
IOP_STREAM_RET *GetPCMStreamWrkRet(int wrk_id);
int             GetVoiceNowAdrs(int core, int voice);
int             GetVoiceLoopAdrs(int core, int voice);

#endif /* _SYSTEM_EEIOP_EE_IOP_H */
