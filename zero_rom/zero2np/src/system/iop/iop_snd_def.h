/* ==========================================================================
 *  system/iop/iop_snd_def.h
 *
 *  The EE<->IOP sound protocol, IOP side.  Every struct here is the receiving
 *  end of one the EE builds in system/eeiop/snd_def.h, and the two must agree
 *  byte for byte -- iopCommand() casts straight into the received buffer.
 *
 *  IOP_COMMAND_ENUM is the tag the EE's iopCommandRegister() writes ahead of
 *  each payload; iopCommand() switches on it.  IOP_COMMAND_QUERY_ENUM is the
 *  same idea for the second, synchronous RPC binding -- one command per call,
 *  no queue -- that iopCommandQuery() serves and the EE's ee_iop_q.c drives.
 *
 *  Reconstructed from iopsys.irx (Feb 6 2004 prototype, SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_IOP_IOP_SND_DEF_H
#define _SYSTEM_IOP_IOP_SND_DEF_H

typedef enum _IOP_COMMAND_ENUM
{
    IOP_COM_END                  = 0,
    REQ_IOP_REBOOT               = 1,
    REQ_CDVD_INIT                = 2,
    REQ_SET_CD_DAT               = 3,
    REQ_CD_READ_MODE_CHANGE      = 4,
    REQ_IOP_SND_INIT             = 5,
    REQ_SET_SND_EFFECT           = 6,
    REQ_VOICE_STOP               = 7,
    REQ_VOICE_LOOP_SET           = 8,
    REQ_STREAM_CREATE            = 9,
    REQ_STREAM_RELEASE           = 10,
    REQ_STREAM_START             = 11,
    REQ_STREAM_PLAY              = 12,
    REQ_STREAM_STOP              = 13,
    REQ_STREAM_ABORT             = 14,
    REQ_STREAM_PAUSE             = 15,
    REQ_STREAM_RESTART           = 16,
    REQ_STREAM_SETVOL            = 17,
    REQ_STREAM_SETPITCH          = 18,
    REQ_SB_INIT                  = 19,
    REQ_SB_PLAY                  = 20,
    REQ_SB_STOP                  = 21,
    REQ_SB_PAUSE                 = 22,
    REQ_SB_RESTART               = 23,
    REQ_SB_SETVOL                = 24,
    REQ_SB_SETPITCH              = 25,
    REQ_PCM_STREAMCREATE         = 26,
    REQ_PCM_STREAMINIT           = 27,
    REQ_PCM_STREAMSTART          = 28,
    REQ_PCM_STREAMPLAY           = 29,
    REQ_PCM_STREAMSTOP           = 30,
    REQ_PCM_STREAMPAUSE          = 31,
    REQ_PCM_STREAMRESTART        = 32,
    REQ_PCM_STREAMSETVOL         = 33,
    IOP_COMMAND_ENUM_FORCE_DWORD = -1
} IOP_COMMAND_ENUM;

/* The query channel (RPC 2).  Each value is one of the EE's ee_iop_q.c entry
 * points: QueryFileSize, ReqQueryLoadCancel, ReqQuerySPUTransCoreGet and
 * ReqQuerySPUTransCoreRelease, in that order. */
typedef enum _IOP_COMMAND_QUERY_ENUM
{
    REQ_FILE_SIZE              = 0,
    REQ_Q_LOAD_CANCEL          = 1,
    REQ_SPU_TRANS_CORE_GET     = 2,
    REQ_SPU_TRANS_CORE_RELEASE = 3
} IOP_COMMAND_QUERY_ENUM;

/* --------------------------------------------------------------------------
 *  Common scalars
 * ------------------------------------------------------------------------ */

typedef struct _VOLSET              /* 0x4 */
{
    /* 0x0 */ short l;
    /* 0x2 */ short r;
} VOLSET;

typedef struct _VOICE_ATTR          /* 0x2 */
{
    /* 0x0:0 */ unsigned short effect : 1;
    /* 0x0:1 */ unsigned short type   : 3;
    /* 0x0:4 */ unsigned short s3d    : 1;
    /* 0x0:5 */ unsigned short loop   : 1;
    /* 0x0:6 */ unsigned short core   : 1;
    /* 0x0:7 */ unsigned short male   : 1;
    /* 0x1:0 */ unsigned short dummy  : 8;
} VOICE_ATTR;

/* SPU2 reverb attributes (libsd type). */
typedef struct _sceSdEffectAttr     /* 0x14 */
{
    /* 0x00 */ unsigned int core;
    /* 0x04 */ unsigned int mode;
    /* 0x08 */ short        depth_L;
    /* 0x0a */ short        depth_R;
    /* 0x0c */ int          delay;
    /* 0x10 */ int          feedback;
} sceSdEffectAttr;

/* --------------------------------------------------------------------------
 *  Payloads
 * ------------------------------------------------------------------------ */

typedef struct _CD_READ_MODE_CHANGE /* 0x4  REQ_CD_READ_MODE_CHANGE */
{
    /* 0x0 */ int mode;             /* goes straight into sceCdRMode.spindlctrl */
} CD_READ_MODE_CHANGE;

/* REQ_SET_CD_DAT carries a bare `char file_name[16]` -- iopCommand() reads it
 * as a string rather than through a struct, so the ROM declares none. */

typedef struct _IOP_SND_INIT        /* 0xc  REQ_IOP_SND_INIT */
{
    /* 0x0 */ int   media;
    /* 0x4 */ int   mvol;
    /* 0x8 */ void *stop_block;     /* SPU address of the silent block */
} IOP_SND_INIT;

typedef struct _SET_SND_EFFECT      /* 0x1c REQ_SET_SND_EFFECT */
{
    /* 0x00 */ int             core;
    /* 0x04 */ int             end_adrs;
    /* 0x08 */ sceSdEffectAttr r_attr;
} SET_SND_EFFECT;

typedef struct _VOICE_STOP          /* 0x4  REQ_VOICE_STOP */
{
    /* 0x0 */ short voice_no;
    /* 0x2 */ short core;
} VOICE_STOP;

typedef struct _VOICE_LOOP_SET      /* 0x8  REQ_VOICE_LOOP_SET */
{
    /* 0x0 */ short core;
    /* 0x2 */ short voice_no;
    /* 0x4 */ int   loop_adrs;
} VOICE_LOOP_SET;

/* ---- one-shot sample buffer (iop_sb.c) --------------------------------- */

typedef struct _SOUND_BUF_PLAY      /* 0x14 REQ_SB_PLAY */
{
    /* 0x00 */ VOLSET       vol;
    /* 0x04 */ unsigned int adrs;   /* SPU address of the sample */
    /* 0x08 */ short        pitch;
    /* 0x0a */ short        adsr1;
    /* 0x0c */ short        adsr2;
    /* 0x0e */ VOICE_ATTR   attr;
    /* 0x10 */ int          voice_no;
} SOUND_BUF_PLAY;

typedef struct _SOUND_BUF_STOP      /* 0x4  REQ_SB_STOP */
{
    /* 0x0 */ char voice_no;
    /* 0x1 */ char core;
    /* 0x2 */ char padding[2];
} SOUND_BUF_STOP;

typedef struct _SOUND_BUF_PAUSE     /* 0x4  REQ_SB_PAUSE */
{
    /* 0x0 */ char voice_no;
    /* 0x1 */ char core;
    /* 0x2 */ char padding[2];
} SOUND_BUF_PAUSE;

typedef struct _SOUND_BUF_RESTART   /* 0x4  REQ_SB_RESTART */
{
    /* 0x0 */ char voice_no;
    /* 0x1 */ char core;
    /* 0x2 */ char padding[2];
} SOUND_BUF_RESTART;

typedef struct _SOUND_BUF_SETVOL    /* 0x8  REQ_SB_SETVOL */
{
    /* 0x0 */ VOLSET vol;
    /* 0x4 */ char   voice_no;
    /* 0x5 */ char   core;
    /* 0x6 */ char   padding[2];
} SOUND_BUF_SETVOL;

typedef struct _SOUND_BUF_SETPITCH  /* 0x4  REQ_SB_SETPITCH */
{
    /* 0x0 */ short pitch;
    /* 0x2 */ char  voice_no;
    /* 0x3 */ char  core;
} SOUND_BUF_SETPITCH;

/* ---- ADPCM stream (iop_stream.c) --------------------------------------- */

typedef struct _STREAM_START        /* 0x13c REQ_STREAM_START */
{
    /* 0x000 */ int          start_sector;
    /* 0x004 */ int          size;
    /* 0x008 */ unsigned int spu_packet[2][2];  /* [ch][0]=adrs [ch][1]=size */
    /* 0x018 */ short        nchannel;
    /* 0x01a */ char         irq_core;
    /* 0x01b */ char         wrk_id;
    /* 0x01c */ int          interleave_byte;
    /* 0x020 */ int          loop_start_block;
    /* 0x024 */ int          loop_end_block;
    /* 0x028 */ int          loop_start_fraction;
    /* 0x02c */ int          loop_end_fraction;
    /* 0x030 */ unsigned int spu_loop_packet[2];
    /* 0x038 */ int          offset;
    /* 0x03c */ char         file_name[256];
} STREAM_START;

typedef struct _STREAM_PLAY         /* 0x1c REQ_STREAM_PLAY */
{
    /* 0x00 */ VOLSET     vol[2];
    /* 0x08 */ VOICE_ATTR attr[2];
    /* 0x0c */ short      adsr1[2];
    /* 0x10 */ short      adsr2[2];
    /* 0x14 */ char       voice[2];
    /* 0x16 */ short      pitch;
    /* 0x18 */ short      irq_core;
    /* 0x1a */ short      wrk_id;
} STREAM_PLAY;

typedef struct _STREAM_SETVOL       /* 0xc  REQ_STREAM_SETVOL */
{
    /* 0x0 */ VOLSET vol[2];
    /* 0x8 */ int    wrk_id;
} STREAM_SETVOL;

typedef struct _STREAM_SETPITCH     /* 0x4  REQ_STREAM_SETPITCH */
{
    /* 0x0 */ short wrk_id;
    /* 0x2 */ short pitch;
} STREAM_SETPITCH;

typedef struct _STREAM_RELEASE { /* 0x4 */ int wrk_id; } STREAM_RELEASE;
typedef struct _STREAM_STOP    { /* 0x4 */ int wrk_id; } STREAM_STOP;
typedef struct _STREAM_ABORT   { /* 0x4 */ int wrk_id; } STREAM_ABORT;
typedef struct _STREAM_PAUSE   { /* 0x4 */ int wrk_id; } STREAM_PAUSE;
typedef struct _STREAM_RESTART { /* 0x4 */ int wrk_id; } STREAM_RESTART;

/* ---- straight PCM stream (iop_pcmstream.c) ----------------------------- */

typedef struct _PCM_STREAM_START    /* 0x110 REQ_PCM_STREAMSTART */
{
    /* 0x000 */ int   start_sector;
    /* 0x004 */ int   size;
    /* 0x008 */ short nchannel;
    /* 0x00a */ short wrk_id;
    /* 0x00c */ int   offset;
    /* 0x010 */ char  file_name[256];
} PCM_STREAM_START;

typedef struct _PCM_STREAM_PLAY     /* 0x4  REQ_PCM_STREAMPLAY */
{
    /* 0x0 */ short wrk_id;
    /* 0x2 */ short vol;
} PCM_STREAM_PLAY;

typedef struct _PCM_STREAM_SETVOL   /* 0x4  REQ_PCM_STREAMSETVOL */
{
    /* 0x0 */ short wrk_id;
    /* 0x2 */ short vol;
} PCM_STREAM_SETVOL;

typedef struct _PCM_STREAM_INIT    { /* 0x4 */ int wrk_id; } PCM_STREAM_INIT;
typedef struct _PCM_STREAM_STOP    { /* 0x4 */ int wrk_id; } PCM_STREAM_STOP;
typedef struct _PCM_STREAM_PAUSE   { /* 0x4 */ int wrk_id; } PCM_STREAM_PAUSE;
typedef struct _PCM_STREAM_RESTART { /* 0x4 */ int wrk_id; } PCM_STREAM_RESTART;

#endif /* _SYSTEM_IOP_IOP_SND_DEF_H */
