/* ==========================================================================
 *  system/eeiop/snd_def.h
 *
 *  Types shared by the EE-side sound modules (snd.c, snd_buffer.c, snd_bank.c,
 *  snd_stream.c, snd_pcmstream.c, snd_util.c) and the IOP sound server.
 *  Everything here is either a payload struct handed to iopCommandRegister()
 *  -- one per IOP_COMMAND_ENUM value -- or a record parsed out of a sound
 *  file (SOUND_INFO / HXD_HEADER).
 *
 *  PORT NOTE ON PLACEMENT: the ROM's own header name for these is not
 *  recoverable.  None of the sound object files carries a SOL stab naming a
 *  header (nothing in them is an inline), so the debug info only proves the
 *  layouts, which are verbatim from types.txt.  They are gathered here rather
 *  than scattered because the EE and IOP sides both need the same bytes, and
 *  ee_iop.h -- which declares IOP_COMMAND_ENUM, the tag for exactly these
 *  payloads -- would otherwise have to pull in the whole sound API.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SYSTEM_EEIOP_SND_DEF_H
#define _SYSTEM_EEIOP_SND_DEF_H

/* --------------------------------------------------------------------------
 *  Common scalars
 * ------------------------------------------------------------------------ */

/* A stereo SPU volume pair.  Full scale is 0x3fff. */
typedef struct _VOLSET              /* 0x4 */
{
    /* 0x0 */ short l;
    /* 0x2 */ short r;
} VOLSET;

/* The per-voice attribute word.  `type` is an SND_GROUP; `core` selects which
 * of the two SPU2 cores the voice lives on. */
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

/* Mixer group.  SndSetGroupVolume()/SndGetGroupVolume() are indexed by it, and
 * SOUND_SYS::type_vol[] has five slots even though only two are named. */
typedef enum _SND_GROUP
{
    SND_GROUP_SOUND_EFFECT = 0,
    SND_GROUP_BGM          = 1
} SND_GROUP;

/* --------------------------------------------------------------------------
 *  Sound-file records
 * ------------------------------------------------------------------------ */

/* One entry of an HXD sound header: everything needed to key a voice except
 * the sample data itself, whose address is `offset` bytes into the matching
 * BD body. */
typedef struct _SOUND_INFO          /* 0x1c */
{
    /* 0x00 */ int            smpl_rate;
    /* 0x04 */ int            offset;
    /* 0x08 */ short          pitch;
    /* 0x0a */ short          vol;
    /* 0x0c */ unsigned short adsr1;
    /* 0x0e */ unsigned short adsr2;
    /* 0x10 */ VOICE_ATTR     attr;
    /* 0x12 */ short          pan;
    /* 0x14 */ int            loopstart;
    /* 0x18 */ int            loopend;
} SOUND_INFO;

/* The HXD file header.  `num` SOUND_INFO records follow it; CheckHXDData()
 * validates `name`/`version`. */
typedef struct _HXD_HEADER          /* 0x20 */
{
    /* 0x00 */ int name;
    /* 0x04 */ int version;
    /* 0x08 */ int num;
    /* 0x0c */ int type;
    /* 0x10 */ int size;
    /* 0x14 */ int interleave_byte;
    /* 0x18 */ int padding[2];
} HXD_HEADER;

/* --------------------------------------------------------------------------
 *  SPU2 reverb attributes (SDK type; layout from SET_SND_EFFECT's 0x14-byte
 *  tail).  Named as the SDK names it so the field uses read the same way.
 * ------------------------------------------------------------------------ */
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
 *  IOP command payloads.  Each is the `buf` handed to
 *  iopCommandRegister(<matching IOP_COMMAND_ENUM>, ...).
 * ------------------------------------------------------------------------ */

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

/* ---- one-shot sample buffer (snd_buffer.c) ----------------------------- */

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

/* ---- ADPCM stream (snd_stream.c) --------------------------------------- */

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

typedef struct _STREAM_STOP    { /* 0x4 */ int wrk_id; } STREAM_STOP;
typedef struct _STREAM_ABORT   { /* 0x4 */ int wrk_id; } STREAM_ABORT;
typedef struct _STREAM_PAUSE   { /* 0x4 */ int wrk_id; } STREAM_PAUSE;
typedef struct _STREAM_RESTART { /* 0x4 */ int wrk_id; } STREAM_RESTART;
typedef struct _STREAM_RELEASE { /* 0x4 */ int wrk_id; } STREAM_RELEASE;

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

/* ---- straight PCM stream (snd_pcmstream.c) ----------------------------- */

typedef struct _PCM_STREAM_PLAY     /* 0x4  REQ_PCM_STREAMPLAY */
{
    /* 0x0 */ short wrk_id;
    /* 0x2 */ short vol;
} PCM_STREAM_PLAY;

typedef struct _PCM_STREAM_START    /* 0x110 REQ_PCM_STREAMSTART */
{
    /* 0x000 */ int   start_sector;
    /* 0x004 */ int   size;
    /* 0x008 */ short nchannel;
    /* 0x00a */ short wrk_id;
    /* 0x00c */ int   offset;
    /* 0x010 */ char  file_name[256];
} PCM_STREAM_START;

typedef struct _PCM_STREAM_INIT    { /* 0x4 */ int wrk_id; } PCM_STREAM_INIT;
typedef struct _PCM_STREAM_STOP    { /* 0x4 */ int wrk_id; } PCM_STREAM_STOP;
typedef struct _PCM_STREAM_PAUSE   { /* 0x4 */ int wrk_id; } PCM_STREAM_PAUSE;
typedef struct _PCM_STREAM_RESTART { /* 0x4 */ int wrk_id; } PCM_STREAM_RESTART;

typedef struct _PCM_STREAM_SETVOL   /* 0x4  REQ_PCM_STREAMSETVOL */
{
    /* 0x0 */ short wrk_id;
    /* 0x2 */ short vol;
} PCM_STREAM_SETVOL;

/* --------------------------------------------------------------------------
 *  Stream slot state, shared by snd_stream.c and snd_pcmstream.c.  The IOP
 *  drives it forward; the EE reads it back through GetStreamWrkRet() /
 *  GetPCMStreamWrkRet().
 * ------------------------------------------------------------------------ */
typedef enum _EEIOP_STREAM_STATUS
{
    ST_STREAM_NO_USE      = 0,
    ST_STREAM_HEADER_LOAD = 1,
    ST_STREAM_PRE_LOAD    = 2,
    ST_STREAM_PLAYING     = 3,
    ST_STREAM_WAIT_END    = 4,
    ST_STREAM_END         = 5,
    ST_STREAM_START       = 6,
    ST_STREAM_PRE_NO_USE  = 7,
    ST_STREAM_FORCE_DWORD = -1
} EEIOP_STREAM_STATUS;

#endif /* _SYSTEM_EEIOP_SND_DEF_H */
