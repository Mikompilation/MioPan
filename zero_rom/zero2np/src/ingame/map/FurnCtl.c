// FILE: /home/zero_rom/zero2np/src/ingame/map/FurnCtl.c
//
// Furniture controller: the registry of loaded furniture/door models.
//
// FurnLoad.c fills it -- one FURN_CTL per distinct model it pulled off the
// disc -- and everything that later needs "where is model f012.sgd" comes back
// here.  A registration is keyed by (buff_id, first 4 chars of name), and holds
// the raw file address plus the two addresses resolved out of it by
// FurnCtlModelInit(): the model and, if the file is a pak, its motion.
//
// The second half (FurnWork*) is an unrelated 32-slot allocator for per-model
// scratch buffers, tagged by the ctl id that owns them so a room teardown can
// free everything one owner allocated.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), FurnCtl.o
// 0x00101a70..0x0010290f.

#include "FurnCtl.h"

#include "MapLoad.h"                            /* MapLoad*FreeMemAddr */
#include "../../graphics/graph3d/ctl/fixed_array.h"
#include "../../common/heapctrl.h"              /* SAFE_MALLOC / heapCtrlFree */
#include "../../common/packfile.h"              /* Pk2GetNum / Pk2GetAddr */
#include "../../common/utility2.h"              /* PRINT_ASSERT */
#include "../../graphics/graph3d/gra3dSGDData.h" /* sgdRemap */
#include "../../graphics/graph3d/sgd_types.h"   /* SGDFILEHEADER */
#include "../../miopan/miopan_profiler.h"
#include "../../system/os/system.h"             /* GetSystemHeapWrkP */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

/* sdata 3eebc0 / 3eebc8 / 3eebd0 / 3eebd8 */
#define FURN_EFFECT_PREFIX  "eff_"
#define FURN_SUFFIX_PUSH    "_p"
#define FURN_SUFFIX_EVENT   "_ev"
#define FURN_MODEL_EXT      ".sgd"

/* First word of the loaded file, as FurnCtlCheckFileType() reads it.  The two
 * string tags are little-endian character constants. */
enum
{
    FURN_FILE_PZB = 0x627a70,       /* "pzb" */
    FURN_FILE_PHF = 0x666870,       /* "phf" */
    FURN_FILE_SGD = 0x1050          /* bare SGD -- no container */
};

static int FurnCtlFindBuffID;                                           /* sdata 3eebe0 */
static int FurnCtlFindListID;                                           /* sdata 3eebe4 */

static fixed_array<FURN_CTL, FURN_CTL_NUM> FurnCtlList;                              /* bss 3f5948 */

/* [buff_id][0] is the top of the room's furniture work area.  Slot 1 of each
 * pair is never written by this module. */
static char *FurnCtlWorkList[2][2];                                     /* bss 3f6488 */

static FURN_WORK_HEAD FurnWorkList[FURN_WORK_NUM];                      /* bss 3f6498 */

/* ---- model names -------------------------------------------------------- */

/* strcmp semantics on the extension: 0 when `f_name` ends in `.type`.  A name
 * with no dot at all reports 1, i.e. "does not match". */
int FurnCtlCheckKakuType(char *f_name, char *type)                      /* 69 */
{
    char *p = strrchr(f_name, '.');                                 /* 71 */

    if (p == nullptr)
    {
        return 1;
    }

    return strcmp(p + 1, type);                                         /* 72 */
}

/* Non-zero when `name` is a real model rather than one of the "eff_" entries,
 * which name an effect placed in the room and have no model file behind them.
 * Note the sense: strncmp() == 0 (it IS an effect) yields 0. */
int FurnCtlGetType(char *name)                                          /* 77 */
{
    return (strncmp(name, FURN_EFFECT_PREFIX, 4) != 0);                 /* 79 */
}

/* Normalises a placed object's name to the model file that backs it: strips
 * the "_p" / "_ev" variant suffixes and appends the extension, so "f012_ev"
 * and "f012_p" both collapse to "f012.sgd".  That collapse is what lets CBuff
 * dedupe several placements down to a single load.
 *
 * (The ROM's spelling of "Model" is preserved -- it is the linked symbol.) */
void FurnCtlGetMdoelName(char *out, char *in)                           /* 89 */
{
    strcpy(out, in);                                                    /* 91 */

    char *p = strstr(out, FURN_SUFFIX_PUSH);                  /* 92 */
    if (p != nullptr)
    {
        *p = '\0';
    }

    p = strstr(out, FURN_SUFFIX_EVENT);
    if (p != nullptr)
    {
        *p = '\0';
    }

    strcat(out, FURN_MODEL_EXT);                                  /* 94 */
}

/* Variant number out of a model name: 'f' or 'd' followed by exactly three
 * digits.  Anything else is a content error, so the ROM prints and returns -1
 * rather than guessing. */
int FurnCtlGetID(char *name)                                            /* 100 */
{
    int num;

    if ((*name == 'f') || (*name == 'd'))                               /* 104 */
    {
        num = 0;
        for (int i = 1; i < 4; i++)                                     /* 106 */
        {
            if (!isdigit((unsigned char)name[i]))                       /* 108 */
            {
                goto err;
            }
            num = (num * 10) + (name[i] - '0');                         /* 109 */
        }
        return num;                                                     /* 111 */
    }

err:
    PRINT_ERROR("NO_FURN_OR_DOOR_NAME[%s]\n", name);            /* 116 */

    return -1;                                                          /* 117 */
}

/* Animation slot for a model, by animation kind.  The variant number indexes a
 * per-kind table whose bounds differ (18 cloth entries, 32 bone), and the
 * biases differ too -- cloth is 1-based, bone 2-based. */
int FurnCtlGetAnimID(char *name, int type)                              /* 122 */
{
    u_int id = (u_int)(FurnCtlGetID(name) % 100);                       /* 124 */

    if (type == 0)                                                      /* 126 */
    {
        if (id < 18)
        {
            return (int)(id + 1);                                       /* 127 */
        }
        PRINT_ERROR("NO_CLOTH_ANIM_DATA[%d]\n", id);                /* 128 */
    }
    else if (type == 1)                                                 /* 132 */
    {
        if (id < 32)
        {
            return (int)(id + 2);                                       /* 133 */
        }
        PRINT_ERROR("NO_BORN_ANIM_DATA[%d]\n", id);                 /* 134 */
    }
    else
    {
        PRINT_ERROR("NO_ANIM_TYPE[%d]\n", type);                    /* 139 */
    }

    return -1;                                                          /* 140 */
}

/* ---- the registration list ---------------------------------------------- */

/* Claims the first slot whose buff_id is negative and stamps the owner in.
 * `size`, `name` and `addr` are left to the caller -- only the fields the
 * caller does not set are cleared here. */
static FURN_CTL *FurnCtlGetFreeSpace(int buff_id)                       /* 146 */
{
    for (int i = 0; i < FURN_CTL_NUM; i++)                                  /* 149 */
    {
        if (FurnCtlList[i].buff_id < 0)                                 /* 152 */
        {
            FurnCtlList[i].buff_id = buff_id;                           /* 153 */
            FurnCtlList[i].flg     = 0;                                 /* 154 */
            FurnCtlList[i].attr    = 0;                                 /* 155 */
            FurnCtlList[i].model   = nullptr;                           /* 156 */
            FurnCtlList[i].mot     = nullptr;

            return &FurnCtlList[i];                                     /* 158 */
        }
    }

    PRINT_ASSERT(" NO_FREE_SPACE\n");                                   /* 161 */

    return nullptr;                                               /* 162 */
}

int FurnCtlRegist(int buff_id, char *name, char *addr, int attr, u_int size)
{                                                                       /* 170 */
    FURN_CTL *mp = FurnCtlGetFreeSpace(buff_id);                        /* 171 */

    if (mp == nullptr)                                            /* 172 */
    {
        return -1;
    }

    strcpy(mp->name, name);                                             /* 173 */
    mp->addr = addr;                                                    /* 174 */
    mp->attr = (short)attr;
    mp->size = (int)size;                                               /* 175 */

    return 0;                                                           /* 176 */
}

/* Releases every registration owned by `buff_id` -- only the id is reset, the
 * rest of the slot is left for the next claimant to overwrite. */
void FurnCtlClearBuff(int buff_id)                                      /* 180 */
{
    for (int i = 0; i < FURN_CTL_NUM; i++)                              /* 182 */
    {
        if (FurnCtlList[i].buff_id == buff_id)
        {
            FurnCtlList[i].buff_id = -1;                                /* 185 */
        }
    }
}

void FurnCtlClearAll(void)                                              /* 190 */
{
    for (int i = 0; i < FURN_CTL_NUM; i++)                              /* 193 */
    {
        FurnCtlList[i].buff_id = -1;                                    /* 195 */
    }
}

/* ---- iteration ----------------------------------------------------------
 * A single module-wide cursor, so only one walk can be in flight at a time.
 * FindInit() arms it; each GetNext* call yields the next registration of that
 * buffer and NULL once the list is exhausted (after which the cursor latches
 * at -1 and every further call reports NULL). */

void FurnCtlFindInit(int buff_id)                                       /* 205 */
{
    FurnCtlFindBuffID = buff_id;
    FurnCtlFindListID = 0;                                              /* 206 */
}

static FURN_CTL *FurnCtlGetNext(void)                                   /* 211 */
{
    if (FurnCtlFindListID < 0)                                          /* 214 */
    {
        return nullptr;
    }

    for (int i = FurnCtlFindListID; i < FURN_CTL_NUM; i++)              /* 215 */
    {
        if (FurnCtlList[i].buff_id == FurnCtlFindBuffID)                /* 217 */
        {
            FurnCtlFindListID = i + 1;                                  /* 219 */
            return &FurnCtlList[i];                                     /* 221 */
        }
    }

    FurnCtlFindListID = -1;                                             /* 222 */

    return nullptr;                                               /* 223 */
}

char *FurnCtlGetNextName(void)                                          /* 228 */
{
    FURN_CTL *fp = FurnCtlGetNext();

    return (fp != nullptr) ? fp->name : nullptr;    /* 229 */
}

char *FurnCtlGetNextModelAddr(void)                                     /* 235 */
{
    FURN_CTL *fp = FurnCtlGetNext();

    return (fp != nullptr) ? fp->model : nullptr;               /* 236 */
}

/* ---- lookup ------------------------------------------------------------- */

/* Only the first four characters of the name are compared: the registration
 * holds "f012.sgd" while callers ask with names like "f012_ev", so the key is
 * really the four-character model id. */
FURN_CTL *FurnCtlGetHeadPtr(int buff_id, char *name)                    /* 243 */
{
    for (int i = 0; i < FURN_CTL_NUM; i++)                              /* 246 */
    {
        if ((FurnCtlList[i].buff_id == buff_id) &&                      /* 251 */
            (strncmp(FurnCtlList[i].name, name, 4) == 0))
        {
            return &FurnCtlList[i];                                     /* 252 */
        }
    }

    PRINT_ERROR(" NO_MODEL_IN_FURN_CTL[%s] addr[%p]\n", name, name);/* 255 */

    return nullptr;                                               /* 256 */
}

short *FurnCtlGetFlgPtr(int buff_id, char *name)                        /* 264 */
{
    FURN_CTL *cp = FurnCtlGetHeadPtr(buff_id, name);

    return (cp != nullptr) ? &cp->flg : nullptr;               /* 265 */
}

static void FurnCtlDeleteFlg(int buff_id, int flg)                      /* 270 */
{
    for (int i = 0; i < FURN_CTL_NUM; i++)                              /* 273 */
    {
        if (FurnCtlList[i].buff_id == buff_id)                          /* 275 */
        {
            FurnCtlList[i].flg &= ~(u_short)flg;                        /* 276 */
        }
    }
}

void FurnCtlDeleteDrawFlgAll(int buff_id)
{
    FurnCtlDeleteFlg(buff_id, FURN_CTL_FLG_DRAW);
}

void FurnCtlDeleteManimFlgAll(int buff_id)
{
    FurnCtlDeleteFlg(buff_id, FURN_CTL_FLG_MANIM);
}

short FurnCtlGetAttr(int buff_id, char *name)                           /* 293 */
{
    FURN_CTL *cp = FurnCtlGetHeadPtr(buff_id, name);

    return (cp != nullptr) ? cp->attr : (short)-1;                /* 294 */
}

/* Attribute 1 models are handed out as a private copy: the file is memcpy'd to
 * the top of the room's free memory, the watermark is pushed past it and the
 * copy is remapped.  Everything else shares the one loaded image.
 *
 * The copy exists because a remap rewrites the SGD's offsets in place, so a
 * model that needs per-instance remapping cannot use the shared image. */
char *FurnCtlGetModelAddr(int buff_id, char *name)                      /* 301 */
{
    FURN_CTL      *cp = FurnCtlGetHeadPtr(buff_id, name);               /* 303 */
    SGDFILEHEADER *pSGDHead;

    if (cp == nullptr)                                            /* 306 */
    {
        return nullptr;
    }

    if (cp->attr != 1)
    {
        return cp->model;                                               /* 329 */
    }

    pSGDHead = (SGDFILEHEADER *)MapLoadGetFreeMemAddr(buff_id);         /* 316 */
    if (pSGDHead != nullptr)
    {
        memcpy(pSGDHead, cp->model, (size_t)cp->size);                  /* 318 */
        MapLoadSetFreeMemAddr(buff_id, (char *)pSGDHead + cp->size);    /* 320 */
        sgdRemap(pSGDHead);                                             /* 323 */
    }

    return (char *)pSGDHead;                                            /* 325 */
}

int FurnCtlGetSize(int buff_id, char *name)                             /* 336 */
{
    FURN_CTL *cp = FurnCtlGetHeadPtr(buff_id, name);

    return (cp != nullptr) ? cp->size : 0;                        /* 337 */
}

/* Motion `iIndex` out of the model's pak.  Index 0 is the model itself, so a
 * request for it is rejected along with anything past the end. */
char *FurnCtlGetMotAddrEx(int buff_id, char *name, int iIndex)          /* 344 */
{
    FURN_CTL *cp = FurnCtlGetHeadPtr(buff_id, name);                    /* 346 */

    if ((cp != nullptr) && (iIndex > 0) &&                        /* 347 */
        (iIndex < Pk2GetNum((u_int *)cp->addr)))
    {
        return (char *)Pk2GetAddr((u_int *)cp->addr, iIndex);           /* 349 */
    }

    return nullptr;                                                   /* 350 */
}

/* The default motion -- pak index 1, resolved once by FurnCtlModelInit(). */
char *FurnCtlGetMotAddr(int buff_id, char *name)                        /* 356 */
{
    FURN_CTL *cp = FurnCtlGetHeadPtr(buff_id, name);

    return (cp != nullptr) ? cp->mot : nullptr;                 /* 357 */
}

int FurnCtlGetAddr(int buff_id, char *name, char **model, char **mot)   /* 363 */
{
    FURN_CTL *cp = FurnCtlGetHeadPtr(buff_id, name);                    /* 364 */

    if (cp == nullptr)                                            /* 365 */
    {
        return -1;
    }

    *model = cp->model;                                                 /* 366 */
    *mot   = cp->mot;                                                   /* 367 */

    return 0;                                                           /* 368 */
}

void CurnCtlSetTopWorkAddr(int buff_id, char *addr)
{
    FurnCtlWorkList[buff_id][0] = addr;                                 /* 373 */
}

/* ---- model initialisation ----------------------------------------------- */

/* Classifies a loaded file by its first word.  Type 4 is the pak case: no
 * recognised tag, but the three words after the first are zero, which is what a
 * pak header looks like.  A non-zero word there means the file is not something
 * this module knows how to open. */
static int FurnCtlCheckFileType(int *mst)                               /* 381 */
{
    if (mst == nullptr)
    {
        return -1;
    }

    if (mst[0] == FURN_FILE_PZB)                                        /* 382 */
    {
        return 0;
    }
    if (mst[0] == FURN_FILE_PHF)                                        /* 383 */
    {
        return 1;
    }
    if (mst[0] == FURN_FILE_SGD)                                        /* 384 */
    {
        return 2;
    }
    if ((mst[1] == 0) && (mst[2] == 0) && (mst[3] == 0))                /* 385 */
    {
        return 4;
    }

    return -1;                                                          /* 387 */
}

/* Resolves the model (and motion) address of every registration owned by
 * `buff_id`, once the files are actually in memory.  A bare SGD is its own
 * model; a pak has the model at index 0 and the default motion at index 1.
 *
 * Attribute 1 models are NOT remapped here -- they are copied and remapped
 * per request by FurnCtlGetModelAddr(), and remapping the shared image first
 * would double-apply the fixups. */
void FurnCtlModelInit(int buff_id)                                      /* 391 */
{
    MioPanProfileScope profile_scope(MIOPAN_PROFILE_ROOM_MODEL_INIT);

    for (int i = 0; i < FURN_CTL_NUM; i++)                                  /* 394 */
    {
        FURN_CTL *cp = &FurnCtlList[i];

        if (cp->buff_id != buff_id)                                     /* 397 */
        {
            continue;
        }

        int type = FurnCtlCheckFileType((int *) cp->addr);                   /* 400 */

        if (type == 2)                                                  /* 402 */
        {
            cp->model = cp->addr;                                       /* 403 */
            sgdRemap((SGDFILEHEADER *)cp->model);                       /* 404 */
        }
        else if (type < 2 || type > 4)
        {
            PRINT_ERROR(" CANNOT_INIT[%s]\n", cp->name);                /* 428 */
        }
        else
        {
            cp->model = (char *)Pk2GetAddr((u_int *)cp->addr, 0);       /* 412 */
            if (cp->model != (char *)0)
            {
                if (cp->attr != 1)                                      /* 417 */
                {
                    sgdRemap((SGDFILEHEADER *)cp->model);               /* 418 */
                }
                if (Pk2GetNum((u_int *)cp->addr) > 1)                   /* 422 */
                {
                    cp->mot = (char *)Pk2GetAddr((u_int *)cp->addr, 1); /* 423 */
                }
            }
        }
    }
}

void FurnCtlInit(void)
{
    FurnCtlClearAll();                                                  /* 439 */
}

void FurnCtlTerm(void)                                                  /* 446 */
{
}

/* ---- per-model scratch allocator ----------------------------------------
 * 32 slots, each tagged with the ctl id that asked for it, so tearing down one
 * owner frees exactly its buffers.  A slot is free when its addr is NULL --
 * the id is not the free marker here, unlike FurnCtlList above. */

static FURN_WORK_HEAD *FurnWorkGetFreeHead(void)                        /* 473 */
{
    for (int i = 0; i < FURN_WORK_NUM; i++)                                 /* 476 */
    {
        if (FurnWorkList[i].addr == nullptr)                          /* 477 */
        {
            return &FurnWorkList[i];                                    /* 479 */
        }
    }

    PRINT_ASSERT("ERR! NO_FREE_SPACE\n");                               /* 481 */

    return nullptr;                                                     /* 482 */
}

char *FurnWorkAlloc(int ctl_id, int size)                               /* 488 */
{
    FURN_WORK_HEAD *hp = FurnWorkGetFreeHead();                         /* 489 */

    if (hp == nullptr)
    {
        return nullptr;
    }

    hp->id   = ctl_id;                                                  /* 494 */
    hp->addr = (char *)SAFE_MALLOC(GetSystemHeapWrkP(), nullptr, size);

    return hp->addr;                                                    /* 496 */
}

void FurnWorkFree(int ctl_id)                                           /* 500 */
{
    for (int i = 0; i < FURN_WORK_NUM; i++)                                 /* 503 */
    {
        if ((FurnWorkList[i].addr != nullptr) &&                      /* 504 */
            (FurnWorkList[i].id == ctl_id))
        {
            heapCtrlFree(GetSystemHeapWrkP(), FurnWorkList[i].addr);    /* 507 */
            FurnWorkList[i].addr = nullptr;                           /* 508 */
            FurnWorkList[i].id   = -1;                                  /* 509 */
        }
    }
}

/* Unconditional -- the owner id is not consulted. */
void FurnWorkFreeAll(void)                                              /* 515 */
{
    for (int i = 0; i < FURN_WORK_NUM; i++)                                 /* 518 */
    {
        if (FurnWorkList[i].addr != nullptr)                          /* 519 */
        {
            heapCtrlFree(GetSystemHeapWrkP(), FurnWorkList[i].addr);    /* 521 */
            FurnWorkList[i].addr = nullptr;                           /* 522 */
            FurnWorkList[i].id   = -1;                                  /* 523 */
        }
    }
}

void FurnWorkInit(void)
{
    FurnWorkFreeAll();                                                  /* 533 */
}

void FurnWorkTrem(void)                                                 /* 540 */
{
}
