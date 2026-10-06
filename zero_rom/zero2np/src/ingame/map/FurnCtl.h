/* ==========================================================================
 *  ingame/map/FurnCtl.h
 *
 *  Furniture controller -- the registry of loaded furniture/door models, plus
 *  a small scratch allocator tagged by owner.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _INGAME_MAP_FURNCTL_H
#define _INGAME_MAP_FURNCTL_H

#include "eetypes.h"

enum
{
    FURN_CTL_NUM  = 48,
    FURN_WORK_NUM = 32
};

/* FURN_CTL::flg bits, as the two DeleteFlgAll entry points name them. */
enum
{
    FURN_CTL_FLG_MANIM = 1,
    FURN_CTL_FLG_DRAW  = 2
};

/* One loaded model.  `buff_id` is the owning room buffer and doubles as the
 * free marker (negative = empty slot).  `addr` is the raw file; `model` and
 * `mot` are resolved out of it by FurnCtlModelInit().  Lookups key on
 * buff_id plus the first four characters of `name`. */
typedef struct FURN_CTL             /* 0x3c */
{
    /* 0x00 */ int   buff_id;
    /* 0x04 */ short attr;
    /* 0x06 */ short flg;
    /* 0x08 */ int   size;
    /* 0x0c */ char  name[36];
    /* 0x30 */ char *addr;
    /* 0x34 */ char *model;
    /* 0x38 */ char *mot;
} FURN_CTL;

/* One scratch buffer.  A slot is free when addr is NULL; `id` only says who
 * owns it, so FurnWorkFree() can release one owner's buffers. */
typedef struct FURN_WORK_HEAD       /* 0x8 */
{
    /* 0x0 */ int   id;
    /* 0x4 */ char *addr;
} FURN_WORK_HEAD;

void FurnCtlInit(void);
void FurnCtlTerm(void);

/* ---- model names ------------------------------------------------------- */

/* strcmp semantics on the extension: 0 when `f_name` ends in `.type`; 1 when
 * it has no extension at all. */
int FurnCtlCheckKakuType(char *f_name, char *type);

/* Non-zero when `name` is a real model rather than an "eff_" effect
 * placeholder.  Note the inverted sense -- an effect gives 0. */
int FurnCtlGetType(char *name);

/* Normalises a placed object's name to its model file: strips the "_p" / "_ev"
 * variant suffixes and appends ".sgd".  `out` needs 36 bytes.  (The ROM's
 * misspelling of "Model" is the linked symbol name and is kept.) */
void FurnCtlGetMdoelName(char *out, char *in);

/* Variant number from a model name ('f'/'d' plus three digits), or -1. */
int FurnCtlGetID(char *name);

/* Animation slot for a model.  type 0 = cloth (18 entries, 1-based), 1 = bone
 * (32 entries, 2-based).  -1 when the variant is out of range. */
int FurnCtlGetAnimID(char *name, int type);

/* ---- registration ------------------------------------------------------ */

/* Records a loaded model against `buff_id`.  Returns 0, or -1 when the 48-slot
 * table is full.  FurnLoad.c is the only caller. */
int  FurnCtlRegist(int buff_id, char *name, char *addr, int attr, u_int size);
void FurnCtlClearBuff(int buff_id);
void FurnCtlClearAll(void);

/* Resolves model/motion addresses for `buff_id`'s registrations once their
 * files are in memory.  Must run before any of the lookups below. */
void FurnCtlModelInit(int buff_id);

/* ---- iteration ---------------------------------------------------------
 * One module-wide cursor, so only one walk at a time.  FindInit() arms it and
 * the GetNext* calls yield NULL once the buffer's registrations run out. */
void  FurnCtlFindInit(int buff_id);
char *FurnCtlGetNextName(void);
char *FurnCtlGetNextModelAddr(void);

/* ---- lookup ------------------------------------------------------------
 * All keyed on buff_id plus the first four characters of `name`.  Each prints
 * NO_MODEL_IN_FURN_CTL and reports empty when there is no such registration. */
FURN_CTL *FurnCtlGetHeadPtr(int buff_id, char *name);
short    *FurnCtlGetFlgPtr(int buff_id, char *name);
short     FurnCtlGetAttr(int buff_id, char *name);
int       FurnCtlGetSize(int buff_id, char *name);

/* Attribute 1 models are handed out as a private copy taken from the room's
 * free memory (and the watermark advanced); everything else shares the loaded
 * image.  Returns NULL when there is no room for the copy. */
char *FurnCtlGetModelAddr(int buff_id, char *name);

/* The default motion (pak index 1), or an arbitrary one.  Index 0 is the model
 * itself, so MotAddrEx rejects it. */
char *FurnCtlGetMotAddr(int buff_id, char *name);
char *FurnCtlGetMotAddrEx(int buff_id, char *name, int iIndex);

/* Both addresses at once; 0 on success, -1 when not registered. */
int FurnCtlGetAddr(int buff_id, char *name, char **model, char **mot);

void FurnCtlDeleteDrawFlgAll(int buff_id);
void FurnCtlDeleteManimFlgAll(int buff_id);

void CurnCtlSetTopWorkAddr(int buff_id, char *addr);

/* ---- per-model scratch allocator --------------------------------------- */

void  FurnWorkInit(void);
void  FurnWorkTrem(void);
char *FurnWorkAlloc(int ctl_id, int size);
/* Frees just the buffers `ctl_id` allocated. */
void  FurnWorkFree(int ctl_id);
/* Frees every buffer regardless of owner. */
void  FurnWorkFreeAll(void);

#endif /* _INGAME_MAP_FURNCTL_H */
