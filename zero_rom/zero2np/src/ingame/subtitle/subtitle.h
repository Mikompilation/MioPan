/* ==========================================================================
 *  ingame/subtitle/subtitle.h
 *
 *  Ingame spoken-line captions.
 *
 *  The subtitle file is a self-relative table.  GetSubTitleAddr() gives its
 *  base; the first 250 words are byte offsets from that same base, and each
 *  one leads to a SUBTITLE_DATA -- the CD file number of the voice stream, the
 *  speaker, and then the (message id, frame count) pairs that make up the
 *  caption, terminated by a -1 message id.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), subtitle.o.
 * ======================================================================== */

#ifndef _INGAME_SUBTITLE_H
#define _INGAME_SUBTITLE_H

#include "../../sdk/libvu0.h"               /* sceVu0FVECTOR                 */

/* Playback state of the one caption the game can have on screen. */
enum SUBTITLE_STATUS
{
    SUBTITLE_STATUS_IDLE    = 0,
    SUBTITLE_STATUS_PRELOAD = 1,        /* stream queued, waiting on the CD  */
    SUBTITLE_STATUS_EXEC    = 2         /* stream playing, captions running  */
};

/* One line of a caption: which message to draw and for how many frames.  The
 * frame count is authored at 60Hz; SubTitleMain() scales it by 5/6 on PAL. */
typedef struct                          /* 0x4 */
{
    /* 0x0 */ short MsgId;              /* -1 ends the caption               */
    /* 0x2 */ short Frame;
} SUBTITLE_MSG_DATA;

/* One caption.  The ROM declares MsgData as a zero-length trailing array
 * (stab `ar50;0;4294967295`); it is [1] here so MSVC takes it too.  Nothing
 * ever asks for sizeof(SUBTITLE_DATA) -- only &MsgData[i], which is at the
 * ROM's offset either way. */
typedef struct                          /* 0x4 + the trailing pairs          */
{
    /* 0x0 */ short StreamFile;         /* CD file of the voice stream       */
    /* 0x2 */ short CharId;             /* speaker; drives the mouth anim    */
    /* 0x4 */ SUBTITLE_MSG_DATA MsgData[1];
} SUBTITLE_DATA;

/* The single caption slot.  A new request stops whatever was running.
 * Note SubTitleCtrlStructInit() clears everything except ObjType/ObjId --
 * those keep their last values until the next 3D request overwrites them.
 *
 * The offsets are the ROM's; everything after pSubTitleData sits 4 bytes
 * higher here because that pointer is 8 bytes on the host, and sizeof() comes
 * out 0x20 rather than 0x1c.  Nothing depends on either. */
typedef struct                          /* 0x1c */
{
    /* 0x00 */ SUBTITLE_DATA *pSubTitleData;
    /* 0x04 */ int StreamId;            /* -1 = nothing queued               */
    /* 0x08 */ int Status;              /* SUBTITLE_STATUS                   */
    /* 0x0c */ int Counter;             /* frames the current line has run   */
    /* 0x10 */ int MsgDataNo;           /* index into MsgData[]              */
    /* 0x14 */ int ObjType;             /* GetObjectPos() class, -1 = fixed  */
    /* 0x18 */ int ObjId;
} SUBTITLE_CTRL;

extern SUBTITLE_CTRL SubTitleCtrl;      /* data 34fe80                       */

void SubTitleInit(void);
void SubTitleMain(int DrawFlg);

/* The four request forms.  All of them go through SubTitleReqSub(); they
 * differ only in where the voice is heard from, and whether the stream is
 * theirs to start. */
void SubTitleReq(int SubTitleNo);
void SubTitleReq3D(int SubTitleNo, float *Position);
void SubTitleReq3DObj(int SubTitleNo, int ObjType, int ObjId);

/* A passive ghost's spoken line.  enemy.c starts the stream itself (so it can
 * put the ghost's velocity into the 3D mixer), then hands the id over here for
 * the caption to ride on; SubTitleStreamFileNoGet() is how it learns which CD
 * file to ask for in the first place. */
int  SubTitleStreamFileNoGet(int SubTitleNo);
void SubTitleReqAutoEnemy(int SubTitleNo, int StreamId);

void SubTitleStop(void);
int  SubTitleIsEnd(void);

#endif /* _INGAME_SUBTITLE_H */
