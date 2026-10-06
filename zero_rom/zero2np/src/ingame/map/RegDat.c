// FILE: /home/zero_rom/zero2np/src/ingame/map/RegDat.c
//
// Room registration-data buffer list.  Up to eight "pzb" files are resident at
// once; each one is a flat block holding a run of placement records
// (MB_OUT_SECTION and the MDAT_* bodies that follow it) and a run of
// rectangles (MB_OUT_RECT) that bind world positions to those records.
//
// Registration builds a per-type index over the rectangles once
// (RegDatRegistPtrList), which is what makes the by-position lookups at the
// bottom of the file cheap enough to run every frame.
//
// PORT: the ROM patches MB_OUT_HEAD::reg_vecp / reg_stp in place from
// file-relative offset to absolute pointer.  Host pointers do not fit in the
// four bytes on disc, so those two fields stay offsets and are resolved on
// each read -- see RegDatVecTop() / RegDatStTop() and the #if 0 block in
// RegDatRegist().
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).  The ROM's source
// line numbers survive in the trailing /* NNN */ comments; the module's own
// numbering runs 74..1049 with a long unemitted gap at 583..762.

#include "RegDat.h"
#include "../../common/utility2.h"            /* PRINT_ERROR / PRINT_ASSERT */

#include "hit_check_base.h"                   /* HcBaseIsInTriXZ */
#include "../../graphics/graph3d/ctl/fixed_array.h"

#include <stdio.h>
#include <string.h>

static fixed_array<RD_REG_HEAD, REG_DAT_BUFF_NUM> RegDatBuff;         /* bss 421990 */

/* Position-lookup state.  RegDatGetBuffIDG() rebuilds the hit list on every
 * call; RegDatNoRegList holds buffers to skip while a door transition has
 * two rooms resident at once. */
static int RegDatNoRegNum;                                            /* sdata 3ef188 */
static int RegDatHitNum;                                              /* sdata 3ef18c */
static fixed_array<int, REG_DAT_BUFF_NUM> RegDatHitList;              /* bss 421df0 */
static fixed_array<int, REG_DAT_BUFF_NUM> RegDatNoRegList;            /* bss 421e10 */

/* PORT-only: resolve the two file-relative offsets in the header.  The header
 * sits at offset 0 of the block, so it doubles as the file base. */
static MB_OUT_RECT *RegDatVecTop(MB_OUT_HEAD *hp)
{
    return (MB_OUT_RECT *)((char *)hp + hp->reg_vecp);
}

static MB_OUT_SECTION *RegDatStTop(MB_OUT_HEAD *hp)
{
    return (MB_OUT_SECTION *)((char *)hp + hp->reg_stp);
}

/* --------------------------------------------------------------------------
 *  RegDatGetTopAddr
 *
 *  The registered block for a slot.  Inlined at every call site in the ROM, so
 *  no symbol survives and the name is inferred from its RegDatSetTopAddr()
 *  counterpart below.  The NULL test on &RegDatBuff[buff_id] can never fire --
 *  it is the ROM's, kept because it is what the emitted code checks.
 * ------------------------------------------------------------------------ */
static MB_OUT_HEAD *RegDatGetTopAddr(int buff_id)
{
    RD_REG_HEAD *hp = &RegDatBuff[buff_id];                             /* 76 */

    if (hp == nullptr)                                         /* 78 */
    {
        return nullptr;
    }
    return (MB_OUT_HEAD *)hp->RegDatPtr;                                /* 79 */
}

int RegDatSetTopAddr(int buff_id, void *addr)                           /* 84 */
{
    RD_REG_HEAD *hp = &RegDatBuff[buff_id];

    if (hp == nullptr)                                                  /* 86 */
    {
        return -1;
    }
    hp->RegDatPtr = (char *)addr;                                       /* 87 */
    return 0;                                                           /* 88 */
}                                                                       /* 89 */

/* --------------------------------------------------------------------------
 *  RegDatGetStPtrSub
 *
 *  Walk `reg_id` records down the section list.  The records are
 *  variable-length, so there is no indexing -- each header's `size` is the
 *  stride to the next.  Another inlined-only static (RegDatGetStPtr,
 *  RegDatGetVecPtrSub and RegDatGetVecNumBin all carry a copy); the name is
 *  inferred.
 *
 *  Note the range test is `>` rather than `>=`: reg_id == reg_st_num walks one
 *  record past the end.  That is the ROM's, and the callers never reach it
 *  because their reg_id comes out of a rectangle that the file itself bounds.
 * ------------------------------------------------------------------------ */
static MB_OUT_SECTION *RegDatGetStPtrSub(MB_OUT_HEAD *hp, int reg_id)
{
    MB_OUT_SECTION *dp;
    int             i;

    if (hp == (MB_OUT_HEAD *)0)                                         /* 97 */
    {
        PRINT_ERROR("HEADER_ADDR_IS_NULL [%d]\n", reg_id);              /* 98 */
        return (MB_OUT_SECTION *)0;
    }

    if (reg_id > hp->reg_st_num)                                        /* 102 */
    {
        PRINT_ERROR(" OUT_OF_ID [%d]\n", reg_id);                       /* 103 */
        return (MB_OUT_SECTION *)0;
    }

    dp = RegDatStTop(hp);                                               /* 108 */

    for (i = 0; i < reg_id; i++)                                        /* 110 */
    {
        dp = (MB_OUT_SECTION *)((char *)dp + dp->size);                 /* 111 */
    }

    return dp;
}

u_short *RegDatGetStPtr(int buff_id, int reg_id)
{
    return (u_short *)RegDatGetStPtrSub(RegDatGetTopAddr(buff_id), reg_id);
}

u_int RegDatGetStLabel(int buff_id, int reg_id)                         /* 127 */
{
    MB_OUT_SECTION *dp = (MB_OUT_SECTION *)RegDatGetStPtr(buff_id, reg_id);

    /* PORT: the ROM dereferences dp unconditionally -- RegDatGetStPtr() has
     * already reported the failure, and on the EE the resulting read of
     * *(u_int *)0x4 just returns garbage.  That faults on the host. */
    if (dp == nullptr)
    {
        return 0;
    }
    return dp->labelID;                                                 /* 130 */
}

MB_OUT_RECT *RegDatGetVecPtr(int buff_id, int type)                     /* 135 */
{
    RD_REG_HEAD *hp = &RegDatBuff[buff_id];

    if ((u_int)type >= REG_DAT_ST_TYPE_NUM)                             /* 137 */
    {
        return nullptr;
    }
    return hp->StPtrList[type].dat;                                     /* 139 */
}

int RegDatGetVecNum(int buff_id, int type)                              /* 143 */
{
    RD_REG_HEAD *hp = &RegDatBuff[buff_id];

    if ((u_int)type >= REG_DAT_ST_TYPE_NUM)                             /* 145 */
    {
        return -1;
    }
    return hp->StPtrList[type].st_num;                                  /* 147 */
}

MB_OUT_RECT *RegDatGetVecPtrStart(int buff_id)                          /* 151 */
{
    MB_OUT_HEAD *hp = RegDatGetTopAddr(buff_id);

    return (hp != nullptr) ? RegDatVecTop(hp) : nullptr;   /* 153 */
}

int RegDatGetVecNumAll(int buff_id)                                     /* 158 */
{
    MB_OUT_HEAD *hp = RegDatGetTopAddr(buff_id);

    return (hp != nullptr) ? hp->reg_vec_num : -1;             /* 160 */
}

/* Rectangles are variable-length too -- `size` is the stride, and there is no
 * bound on the walk: the caller counts. */
MB_OUT_RECT *RegDatGetNextVecPtr(MB_OUT_RECT *mst)
{
    return (MB_OUT_RECT *)((char *)mst + mst->size);                    /* 166 */
}

/* A label encodes the area and the record in one integer: label / 1000 is the
 * area id the record belongs to, label % 1000 is its index within that area's
 * section table.  The distinct negative returns are diagnostic -- callers only
 * test for < 0 -- but they say which half of the label went wrong. */
int RegDatGetStID4Label(int buff_id, int label)                         /* 172 */
{
    int          st_id = label % 1000;                                  /* 173 */
    MB_OUT_HEAD *hp    = RegDatGetTopAddr(buff_id);                     /* 174 */

    if (hp == (MB_OUT_HEAD *)0)
    {
        return -1;                                                      /* 178 */
    }
    if (hp->area_id != (label / 1000))
    {
        return -2;                                                      /* 179 */
    }
    if (st_id > hp->reg_st_num)
    {
        return -3;                                                      /* 181 */
    }
    if (st_id < 0)
    {
        return -3;                                                      /* 183 */
    }

    return st_id;                                                       /* 185 */
}                                                                       /* 186 */

u_short *RegDatGetStPtr4Label(int buff_id, int label)                   /* 193 */
{
    int st_id = RegDatGetStID4Label(buff_id, label);

    if (st_id < 0)                                                      /* 198 */
    {
        return nullptr;
    }
    return RegDatGetStPtr(buff_id, st_id);                              /* 199 */
}

/* RegDatGetStPtr4Label() without needing the buffer id: resolves it from the
 * label's area first.  Note it repeats the area scan rather than calling
 * RegDatBuffID4Label() -- kept as the ROM has it. */
u_short *RegDatGetStPtr4Label2(int label)                               /* 203 */
{
    MB_OUT_HEAD *bp;
    int          st_id;
    int          i;

    for (i = 0; i < REG_DAT_BUFF_NUM; i++)                              /* 209 */
    {
        bp = RegDatGetHead(i);                                          /* 211 */
        if ((bp != (MB_OUT_HEAD *)0) && (bp->area_id == (label / 1000)))
        {
            break;
        }
    }
    if (i >= REG_DAT_BUFF_NUM)
    {
        return nullptr;                                            /* 212 */
    }

    st_id = RegDatGetStID4Label(i, label);                              /* 215 */
    if (st_id < 0)
    {
        return nullptr;                                            /* 217 */
    }

    return RegDatGetStPtr(i, st_id);                                    /* 221 */
}

/* As ...4Label2(), but the record must be of `type` as well.  The first
 * halfword of a record is its SecStID, which is why the whole family hands
 * back u_short * rather than MB_OUT_SECTION *.
 *
 * The 232..241 gap is unemitted in the ROM -- a commented-out block. */
u_short *RegDatGetStPtr4Label3(int label, int type)                     /* 229 */
{
    u_short *hp = RegDatGetStPtr4Label2(label);

    if (hp == (u_short *)0)                                             /* 230 */
    {
        return (u_short *)0;
    }
    if ((int)*hp == type)                                               /* 231 */
    {
        return hp;
    }

    PRINT_ASSERT("RegDat No Type : label[%d]\n", label);                /* 242 */
    return nullptr;                                                /* 245 */
}                                                                       /* 246 */

/* Begins a walk over every rectangle registered against `label`.  The cursor
 * lives in the buffer slot (LabVecID / LabVecNum / LabVecPtr), so a walk is
 * per-buffer and not reentrant -- that is the ROM's design, and MrecIsInEventSub
 * relies on it by calling Find once and then NextFind until it runs dry.
 *
 * The rectangle type is taken from the section's own SecStID: registration
 * rectangles are grouped by type, and the section header names which group it
 * belongs to.  Returns 0 on success, or a distinct negative for each way the
 * lookup can come up empty. */
int RegDatVecFind4Label(int buff_id, int label)                         /* 250 */
{
    RD_REG_HEAD *hp;
    u_short     *sp;

    if ((u_int)buff_id >= REG_DAT_BUFF_NUM)                             /* 254 */
    {
        return -1;
    }
    hp = &RegDatBuff[buff_id];

    sp = RegDatGetStPtr4Label(buff_id, label);                          /* 258 */
    if (sp == nullptr)
    {
        return -2;
    }

    hp->LabVecID = RegDatGetStID4Label(buff_id, label);                 /* 260 */
    if (hp->LabVecID < 0)                                               /* 262 */
    {
        return -3;
    }

    hp->LabVecNum = RegDatGetVecNum(buff_id, (int)*sp);
    if (hp->LabVecNum <= 0)                                             /* 265 */
    {
        return -4;
    }

    hp->LabVecPtr = RegDatGetVecPtr(buff_id, (int)*sp);
    return (hp->LabVecPtr != (MB_OUT_RECT *)0) ? 0 : -5;                /* 266 */
}                                                                       /* 269 */

/* Next rectangle in the walk RegDatVecFind4Label() started, or NULL when the
 * group is exhausted.  The group holds every rectangle of that type in the
 * room, so the loop skips the ones registered to a different record. */
MB_OUT_RECT *RegDatVecNextFind(int buff_id)                             /* 273 */
{
    RD_REG_HEAD *hp;
    MB_OUT_RECT *rec;

    if ((u_int)buff_id >= REG_DAT_BUFF_NUM)                             /* 276 */
    {
        PRINT_ERROR(" BUFF_ID_ERR [%d]\n", buff_id);                    /* 277 */
        return (MB_OUT_RECT *)0;                                        /* 278 */
    }
    hp = &RegDatBuff[buff_id];

    while (--hp->LabVecNum >= 0)                                        /* 282 */
    {
        rec = hp->LabVecPtr;

        if (rec->reg_id == hp->LabVecID)                                /* 283 */
        {
            hp->LabVecPtr = RegDatGetNextVecPtr(rec);                   /* 285 */
            return rec;                                                 /* 286 */
        }

        hp->LabVecPtr = RegDatGetNextVecPtr(hp->LabVecPtr);             /* 289 */
    }

    return (MB_OUT_RECT *)0;                                            /* 291 */
}                                                                       /* 292 */

/* Which loaded buffer holds `labelID`'s area.  Callers that only have a label
 * -- the event macros and MapObjSetHit() -- use this to find the buffer before
 * any of the buff_id-taking accessors. */
int RegDatBuffID4Label(int labelID)                                     /* 296 */
{
    MB_OUT_HEAD *bp;
    int          i;

    for (i = 0; i < REG_DAT_BUFF_NUM; i++)                              /* 300 */
    {
        bp = RegDatGetHead(i);                                          /* 302 */
        if ((bp != (MB_OUT_HEAD *)0) && (bp->area_id == (labelID / 1000)))  /* 303 */
        {
            return i;                                                   /* 305 */
        }
    }

    return -1;                                                          /* 307 */
}

/* First slot with no block registered, wiping its per-type index on the way
 * out so RegDatRegistPtrList() starts from a clean table. */
static int RegDatGetBuffFreeSpace(void)                                 /* 313 */
{
    int i;
    int j;

    for (i = 0; i < REG_DAT_BUFF_NUM; i++)                              /* 316 */
    {
        if (RegDatBuff[i].RegDatPtr == (char *)0)
        {
            for (j = 0; j < REG_DAT_ST_TYPE_NUM; j++)                   /* 320 */
            {
                RegDatBuff[i].StPtrList[j].dat    = (MB_OUT_RECT *)0;
                RegDatBuff[i].StPtrList[j].st_num = 0;
            }                                                           /* 324 */
            return i;                                                   /* 325 */
        }
    }                                                                   /* 327 */

    return -1;                                                          /* 328 */
}                                                                       /* 329 */

static RD_REG_HEAD *RegDatGetBuffListPtr(int id)                        /* 333 */
{
    return ((u_int)id < REG_DAT_BUFF_NUM) ? &RegDatBuff[id]             /* 334 */
                                          : nullptr;
}                                                                       /* 336 */

/* Drops the block from the slot.  Only RegDatPtr is cleared -- the per-type
 * index and both cursors are left stale, and are rebuilt by the next
 * RegDatRegist() into this slot. */
void RegDatDeleteBuffList(int id)
{
    if ((u_int)id < REG_DAT_BUFF_NUM)                                   /* 341 */
    {
        RegDatBuff[id].RegDatPtr = (char *)0;
    }
}                                                                       /* 343 */

void RegDatDeleteAllBuffList(void)
{
    int i;

    for (i = 0; i < REG_DAT_BUFF_NUM; i++)                              /* 350 */
    {
        RegDatDeleteBuffList(i);                                        /* 351 */
    }                                                                   /* 352 */
}

/* --------------------------------------------------------------------------
 *  RegDatGetVecPtrSub
 *
 *  First rectangle belonging to section type `RegStID`, and via *num its index
 *  in the file's rectangle run.  The rectangles are stored sorted by type, so
 *  the scan gives up as soon as it walks past the type it wants.
 * ------------------------------------------------------------------------ */
static MB_OUT_RECT *RegDatGetVecPtrSub(MB_OUT_HEAD *stp, int RegStID, int *num)  /* 358 */
{
    MB_OUT_RECT    *vp;
    MB_OUT_SECTION *dp;
    int             reg_id;
    int             i;

    vp = RegDatVecTop(stp);
    for (i = 0; i < stp->reg_vec_num; i++)                              /* 365 */
    {
        reg_id = vp->reg_id;

        dp = RegDatGetStPtrSub(stp, reg_id);
        if (dp == (MB_OUT_SECTION *)0)                                  /* 371 */
        {
            return (MB_OUT_RECT *)0;
        }

        if ((int)dp->SecStID == RegStID)                                /* 373 */
        {
            *num = i;                                                   /* 374 */
            return vp;                                                  /* 384 */
        }
        if (RegStID < (int)dp->SecStID)                                 /* 387 */
        {
            return (MB_OUT_RECT *)0;
        }

        vp = RegDatGetNextVecPtr(vp);                                   /* 391 */
    }                                                                   /* 392 */

    return (MB_OUT_RECT *)0;                                            /* 393 */
}                                                                       /* 394 */

static MB_OUT_RECT *RegDatGetVecPtrBin(MB_OUT_HEAD *stp, int RegStID)   /* 398 */
{
    int work;

    return RegDatGetVecPtrSub(stp, RegStID, &work);                     /* 401 */
}

/* How many consecutive rectangles carry section type `RegStID`.  Starts from
 * where RegDatGetVecPtrSub() left off rather than rescanning. */
static int RegDatGetVecNumBin(MB_OUT_HEAD *stp, int RegStID)            /* 406 */
{
    MB_OUT_RECT    *vp;
    MB_OUT_SECTION *dp;
    int             reg_id;
    int             st_num;
    int             cnt;
    int             i;

    vp = RegDatGetVecPtrSub(stp, RegStID, &cnt);                        /* 412 */
    if (vp == (MB_OUT_RECT *)0)                                         /* 413 */
    {
        return 0;
    }

    st_num = 0;
    for (i = cnt; i < stp->reg_vec_num; i++)                            /* 427 */
    {
        reg_id = vp->reg_id;

        dp = RegDatGetStPtrSub(stp, reg_id);
        if (dp == (MB_OUT_SECTION *)0)                                  /* 431 */
        {
            return 0;
        }
        if ((int)dp->SecStID != RegStID)                                /* 432 */
        {
            return st_num;
        }

        st_num++;                                                       /* 433 */
        vp = RegDatGetNextVecPtr(vp);                                   /* 436 */
    }                                                                   /* 444 */

    return st_num;                                                      /* 445 */
}                                                                       /* 446 */

/* Build the per-type index once, at registration time. */
static void RegDatRegistPtrList(RD_REG_HEAD *hp)                        /* 450 */
{
    int i;

    for (i = 0; i < REG_DAT_ST_TYPE_NUM; i++)                           /* 452 */
    {
        hp->StPtrList[i].st_num =
            RegDatGetVecNumBin((MB_OUT_HEAD *)hp->RegDatPtr, i);        /* 453 */
        hp->StPtrList[i].dat =
            RegDatGetVecPtrBin((MB_OUT_HEAD *)hp->RegDatPtr, i);        /* 455 */
    }                                                                   /* 456 */
}

/* Slide every rectangle by (x, z) and flatten it onto the floor plane: the
 * rectangles are 2D footprints, so Y is forced to 0 and W to 1 whatever the
 * exporter wrote.  `y` is accepted and ignored, in the ROM as here. */
static void RegDatAddOffset(MB_OUT_HEAD *hp, float x, float y, float z) /* 462 */
{
    MB_OUT_RECT *rp;
    int          i;
    int          j;

    (void)y;

    rp = RegDatVecTop(hp);
    for (i = 0; i < hp->reg_vec_num; i++)                               /* 470 */
    {
        for (j = 0; j < 4; j++)                                         /* 471 */
        {
            rp->vec[j][0] += x;                                         /* 472 */

            rp->vec[j][1] = 0.0f;                                       /* 474 */
            rp->vec[j][2] += z;                                         /* 475 */
            rp->vec[j][3] = 1.0f;                                       /* 476 */
        }                                                               /* 477 */

        rp = RegDatGetNextVecPtr(rp);                                   /* 479 */
    }                                                                   /* 480 */
}

/* Move the whole area to (x, y, z).  Pos is used as scratch for the delta
 * before being overwritten with the new absolute position -- and there is no
 * NULL check, so this faults on an empty slot.  Both are the ROM's. */
void RegDatSetOffset(int buff_id, float x, float y, float z)            /* 485 */
{
    MB_OUT_HEAD *hp = RegDatGetTopAddr(buff_id);

    hp->Pos[0] = x - hp->Pos[0];                                        /* 487 */
    hp->Pos[1] = y - hp->Pos[1];                                        /* 488 */
    hp->Pos[2] = z - hp->Pos[2];                                        /* 489 */

    RegDatAddOffset(hp, hp->Pos[0], hp->Pos[1], hp->Pos[2]);            /* 492 */

    hp->Pos[0] = x;                                                     /* 493 */
    hp->Pos[1] = y;                                                     /* 494 */
    hp->Pos[2] = z;                                                     /* 495 */
}

/* --------------------------------------------------------------------------
 *  RegDatRegist
 *
 *  Take a loaded "pzb" block into a free slot: relocate its two internal
 *  offsets, flip the exporter's Y/Z sign convention, index the rectangles by
 *  type, then bake the area origin into every rectangle.
 *
 *  Returns the slot id, -1 for a NULL block, -2 when no slot is free or the
 *  block is not a registration file.
 * ------------------------------------------------------------------------ */
int RegDatRegist(char *mst)                                             /* 509 */
{
    RD_REG_HEAD *rp;
    int          id;

    if (mst == (char *)0)
    {
        PRINT_ERROR("REG_DAT_ADDR_IS_NULL\n");                          /* 510 */
        return -1;                                                      /* 511 */
    }

    id = RegDatGetBuffFreeSpace();                                      /* 514 */
    if (id < 0)
    {
        PRINT_ERROR("NO_REG_DAT_SPACE\n");                              /* 515 */
        return -2;                                                      /* 516 */
    }

    if (strcmp(mst, "pzb") != 0)                                        /* 521 */
    {
        PRINT_ERROR("NO_REG_FILE_DAT\n");                               /* 522 */
        return -2;                                                      /* 523 */
    }

    ((MB_OUT_HEAD *)mst)->Pos[1] = -((MB_OUT_HEAD *)mst)->Pos[1];       /* 528 */
    ((MB_OUT_HEAD *)mst)->Pos[2] = -((MB_OUT_HEAD *)mst)->Pos[2];       /* 529 */

#if 0   /* ROM: relocate the two internal offsets to absolute pointers.
         * They no longer fit in the four bytes on disc, so the port leaves
         * them alone and resolves through RegDatVecTop / RegDatStTop. */
    work = (u_int)((MB_OUT_HEAD *)mst)->reg_stp;                        /* 531 */
    work += (u_int)mst;                                                 /* 532 */
    ((MB_OUT_HEAD *)mst)->reg_stp = (MB_OUT_SECTION *)work;             /* 533 */
    work = (u_int)((MB_OUT_HEAD *)mst)->reg_vecp;                       /* 534 */
    work += (u_int)mst;                                                 /* 535 */
    ((MB_OUT_HEAD *)mst)->reg_vecp = (MB_OUT_RECT *)work;               /* 536 */
#endif

    rp = RegDatGetBuffListPtr(id);                                      /* 539 */
    rp->RegDatPtr = mst;                                                /* 540 */

    RegDatRegistPtrList(rp);                                            /* 546 */
    RegDatAddOffset((MB_OUT_HEAD *)mst,                                 /* 548 */
                    ((MB_OUT_HEAD *)mst)->Pos[0],
                    ((MB_OUT_HEAD *)mst)->Pos[1],
                    ((MB_OUT_HEAD *)mst)->Pos[2]);

    return id;                                                          /* 550 */
}                                                                       /* 551 */

void RegDatInit(void)                                                   /* 555 */
{
    int i;

    memset(&RegDatBuff[0], 0, sizeof(RD_REG_HEAD) * REG_DAT_BUFF_NUM);

    for (i = 0; i < REG_DAT_BUFF_NUM; i++)                              /* 560 */
    {
        RegDatBuff[i].RegDatPtr = nullptr;
        RegDatBuff[i].LabVecID  = -1;
        RegDatBuff[i].LabVecNum = -1;
        RegDatBuff[i].LabVecPtr = nullptr;
    }                                                                   /* 565 */

    RegDatDeleteAllBuffList();                                          /* 567 */

    RegDatResetNoRegistList();                                          /* 569 */
}

void RegDatDeleteBuff(int buff_id)
{
    RegDatSetTopAddr(buff_id, nullptr);
}

int RegDatCheckBuff(int buff_id)                                        /* 579 */
{
    if (RegDatGetTopAddr(buff_id) == nullptr)
    {
        return -1;
    }
    return 0;
}

/* Type-6 rectangles are the room outlines.  A point is inside one when it is
 * inside either triangle of the quad. */
static int RegDatCheckHitRect(MB_OUT_RECT *mp, const float *vPos)       /* 764 */
{
    if (HcBaseIsInTriXZ(vPos, mp->vec[0], mp->vec[1], mp->vec[2]) != 0) /* 775 */
    {
        return 1;
    }
    return (HcBaseIsInTriXZ(vPos, mp->vec[0], mp->vec[2], mp->vec[3]) != 0);  /* 776 */
}                                                                       /* 781 */

MB_OUT_HEAD *RegDatGetHead(int buff_id)                                 /* 785 */
{
    return RegDatGetTopAddr(buff_id);
}

/* Which loaded registration buffer contains vPos.  kai == -1 matches any
 * floor.  Buffers on the no-register list are skipped (the door-transition
 * code parks the room being left there).
 *
 * The return encodes ambiguity as well as the answer, and callers rely on
 * it: -1 nothing, -2 every buffer hit (impossible geometry), -3 more than
 * one hit -- for -3 the caller re-reads the full list through
 * RegDatGetHitList()/RegDatGetHitNum(). */
int RegDatGetBuffIDG(int kai, const float *vPos)                        /* 793 */
{
    MB_OUT_HEAD *bp;
    MB_OUT_RECT *mp;
    int          cnt;
    int          i;
    int          j;

    RegDatHitNum = 0;                                                   /* 798 */

    for (i = 0; i < REG_DAT_BUFF_NUM; i++)                              /* 801 */
    {
        bp = RegDatGetHead(i);                                          /* 803 */
        if (bp == (MB_OUT_HEAD *)0)                                     /* 804 */
        {
            continue;
        }
        if ((kai != -1) && (bp->kai != kai))                            /* 806 */
        {
            continue;
        }

        for (j = 0; j < RegDatNoRegNum; j++)                            /* 808 */
        {
            if (RegDatNoRegList[j] == i)                                /* 809 */
            {
                break;
            }
        }
        if (j != RegDatNoRegNum)                                        /* 811 */
        {
            continue;
        }

        mp = RegDatGetVecPtr(i, RECORD_TYPE_ROOM_OUTLINE);                                     /* 813 */
        if (mp == (MB_OUT_RECT *)0)                                     /* 814 */
        {
            continue;
        }

        cnt = RegDatGetVecNum(i, RECORD_TYPE_ROOM_OUTLINE);                                    /* 816 */
        for (j = 0; j < cnt; j++)                                       /* 817 */
        {
            if (RegDatCheckHitRect(mp, vPos) != 0)                      /* 818 */
            {
                RegDatHitList[RegDatHitNum++] = i;                      /* 826 */
                break;
            }
            mp = RegDatGetNextVecPtr(mp);                               /* 820 */
        }
    }                                                                   /* 828 */

    if (RegDatHitNum == 0)                                              /* 832 */
    {
        return -1;
    }
    if (RegDatHitNum == REG_DAT_BUFF_NUM)                               /* 833 */
    {
        return -2;
    }
    if (RegDatHitNum >= 2)                                              /* 834 */
    {
        return -3;
    }

    return RegDatHitList[0];
}                                                                       /* 836 */

void *RegDatGetStat(int kai, float *vPos, int type)
{
    return RegDatGetRectAndStat((MB_OUT_RECT **)0, (void *)0, kai, vPos, type);  /* 842 */
}

/* As RegDatGetRectAndStat2, but resolves the buffer from vPos first.  An
 * ambiguous position (-2/-3) means several rooms overlap here, so every
 * candidate buffer is tried: without a preferred record the first hit wins,
 * with one the search keeps going until it finds that exact record and falls
 * back to the first hit if it never does. */
void *RegDatGetRectAndStat(MB_OUT_RECT **ppRect, void *pRectStat, int kai,  /* 849 */
                           float *vPos, int type)
{
    int   buff_id;
    int   HitNum;
    int  *pHitList;
    void *pStat;
    void *pRet = (void *)0;                                             /* 852 */
    int   i;

    if (ppRect != (MB_OUT_RECT **)0)                                    /* 854 */
    {
        *ppRect = (MB_OUT_RECT *)0;
    }

    buff_id = RegDatGetBuffIDG(kai, vPos);                              /* 857 */
    if (buff_id >= 0)                                                   /* 859 */
    {
        return RegDatGetRectAndStat2(ppRect, pRectStat, buff_id, vPos, type);  /* 894 */
    }
    if (buff_id == -1)                                                  /* 860 */
    {
        return pRet;
    }

    HitNum   = RegDatGetHitNum();                                       /* 864 */
    pHitList = RegDatGetHitList();                                      /* 865 */

    if (pRectStat == (void *)0)                                         /* 867 */
    {
        for (i = 0; i < HitNum; i++)                                    /* 868 */
        {
            pRet = RegDatGetRectAndStat2(ppRect, nullptr,             /* 870 */
                                         pHitList[i], vPos, type);
            if (pRet != (void *)0)                                      /* 871 */
            {
                return pRet;
            }
        }                                                               /* 872 */
    }
    else
    {
        for (i = 0; i < HitNum; i++)                                    /* 879 */
        {
            pStat = RegDatGetRectAndStat2(ppRect, pRectStat,            /* 881 */
                                          pHitList[i], vPos, type);
            if (pStat == pRectStat)
            {
                pRet = pRectStat;                                       /* 884 */
                break;
            }
            if ((pStat != (void *)0) && (pRet == (void *)0))            /* 886 */
            {
                pRet = pStat;
            }
        }                                                               /* 889 */
    }

    return pRet;                                                        /* 897 */
}

/* Finds the registration record whose type-`type` rectangle contains vPos
 * inside one already-loaded buffer.  When pStat is given, a match on it wins
 * outright (that is what keeps a camera latched to the rectangle it is
 * already using); otherwise the first hit is kept.  ppRect, when supplied,
 * receives the rectangle the match came from. */
void *RegDatGetRectAndStat2(MB_OUT_RECT **ppRect, void *pStat, int buff_id,  /* 903 */
                            float *vPos, int type)
{
    MB_OUT_RECT *mp;
    MB_OUT_RECT *pRetMbOutRect = (MB_OUT_RECT *)0;                      /* 907 */
    void        *pRet          = (void *)0;                             /* 906 */
    void        *pTmpStat;
    int          cnt;
    int          j;

    if (ppRect != (MB_OUT_RECT **)0)                                    /* 909 */
    {
        *ppRect = (MB_OUT_RECT *)0;
    }

    mp = RegDatGetVecPtr(buff_id, type);                                /* 912 */
    if (mp == (MB_OUT_RECT *)0)                                         /* 913 */
    {
        return (void *)0;
    }

    cnt = RegDatGetVecNum(buff_id, type);                               /* 916 */
    for (j = 0; j < cnt; j++)                                           /* 917 */
    {
        if (RegDatCheckHitRect(mp, vPos) != 0)                          /* 918 */
        {
            pTmpStat = RegDatGetStPtr(buff_id, mp->reg_id);             /* 921 */
            if (pTmpStat != (void *)0)                                  /* 922 */
            {
                if (pTmpStat == pStat)
                {
                    pRet          = pTmpStat;                           /* 923 */
                    pRetMbOutRect = mp;                                 /* 924 */
                    break;              /* the caller's own record wins */
                }
                if ((pTmpStat != (void *)0) && (pRet == (void *)0))     /* 927 */
                {
                    pRet          = pTmpStat;                           /* 928 */
                    pRetMbOutRect = mp;                                 /* 929 */
                }
            }
        }
        mp = RegDatGetNextVecPtr(mp);                                   /* 932 */
    }                                                                   /* 933 */

    if (pRet == (void *)0)                                              /* 934 */
    {
        return (void *)0;
    }
    if (ppRect != (MB_OUT_RECT **)0)                                    /* 936 */
    {
        *ppRect = pRetMbOutRect;
    }
    return pRet;                                                        /* 939 */
}                                                                       /* 940 */

/* Non-zero while pStat is still the record covering vPos -- the test that
 * stops the map camera cutting every frame. */
int RegDatCheckSameRectStat(void *pStat, int kai, float *vPos, int type) /* 945 */
{
    int  buff_id;
    int  HitNum;
    int *pHitList;
    int  RetVal = 0;                                                    /* 948 */
    int  i;

    if (pStat == (void *)0)                                             /* 950 */
    {
        return RetVal;
    }

    buff_id = RegDatGetBuffIDG(kai, vPos);                              /* 953 */
    if (buff_id >= 0)                                                   /* 955 */
    {
        return (RegDatGetRectAndStat2((MB_OUT_RECT **)0, pStat,         /* 972 */
                                      buff_id, vPos, type) == pStat);
    }
    if (buff_id == -1)                                                  /* 956 */
    {
        return RetVal;
    }

    HitNum   = RegDatGetHitNum();                                       /* 960 */
    pHitList = RegDatGetHitList();                                      /* 961 */

    for (i = 0; i < HitNum; i++)                                        /* 963 */
    {
        if (RegDatGetRectAndStat2((MB_OUT_RECT **)0, pStat,             /* 964 */
                                  pHitList[i], vPos, type) == pStat)
        {
            RetVal = 1;                                                 /* 966 */
            break;
        }
    }                                                                   /* 968 */

    return RetVal;                                                      /* 977 */
}                                                                       /* 978 */

int RegDatGetBuffID(float *vPos)
{
    return RegDatGetBuffIDG(-1, vPos);                                  /* 984 */
}

int *RegDatGetHitList(void)                                             /* 988 */
{
    return &RegDatHitList[0];
}

int RegDatGetHitNum(void)
{
    return RegDatHitNum;
}

/* The overflow report does not stop the append: the ROM prints and writes
 * anyway, which walks off the end of the array.  Kept as-is -- with a real
 * fixed_array the subscript asserts first, exactly as it does on target. */
void RegDatAddNoRegistList(int id)                                      /* 995 */
{
    if (RegDatNoRegNum >= REG_DAT_BUFF_NUM)                             /* 996 */
    {
        PRINT_ERROR("NO_REGIST_LIST_MAX_OVER[%d]\n", id);               /* 997 */
    }

    RegDatNoRegList[RegDatNoRegNum++] = id;                             /* 1000 */
}

void RegDatResetNoRegistList(void)
{
    RegDatNoRegNum = 0;                                                 /* 1006 */
}

/* Arms the per-buffer section cursor for a walk over every record of `type`. */
void RegDatGetStPtrStart(int buff_id, int type)                         /* 1013 */
{
    RD_REG_HEAD *hp = &RegDatBuff[buff_id];
    MB_OUT_HEAD *mp;

    if (hp == nullptr)                                         /* 1017 */
    {
        return;
    }
    mp = (MB_OUT_HEAD *)hp->RegDatPtr;                                  /* 1018 */
    if (mp == nullptr)                                         /* 1019 */
    {
        return;
    }

    hp->type_id = type;                                                 /* 1021 */

    hp->type_search_num = 0;                                            /* 1023 */
    hp->type_search_p   = RegDatStTop(mp);                              /* 1024 */
}                                                                       /* 1025 */

MB_OUT_SECTION *RegDatGetNextStPtr(int buff_id)                         /* 1030 */
{
    RD_REG_HEAD    *hp = &RegDatBuff[buff_id];
    MB_OUT_HEAD    *mp;
    MB_OUT_SECTION *wp;

    if (hp == nullptr)                                         /* 1033 */
    {
        return nullptr;
    }
    mp = (MB_OUT_HEAD *)hp->RegDatPtr;                                  /* 1034 */
    if (mp == nullptr)                                         /* 1035 */
    {
        return nullptr;
    }

    while (hp->type_search_num < mp->reg_st_num)                        /* 1038 */
    {
        wp = hp->type_search_p;                                         /* 1039 */

        hp->type_search_p = (MB_OUT_SECTION *)((char *)wp + wp->size);  /* 1043 */

        hp->type_search_num++;                                          /* 1045 */
        if ((int)wp->SecStID == hp->type_id)                            /* 1046 */
        {
            return wp;
        }
    }

    return nullptr;                                         /* 1048 */
}                                                                       /* 1049 */
