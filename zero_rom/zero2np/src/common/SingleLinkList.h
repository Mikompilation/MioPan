/* ==========================================================================
 *  common/SingleLinkList.h
 *
 *  Intrusive singly-linked list of fixed-size elements.  A list owns one
 *  ElemSize; every cell is a separate EFFECT_MALLOC() block whose header is
 *  the forward link and whose body is a byte-copy of the caller's element.
 *
 *  The accessors below are inline in the original -- the decompiler reports
 *  them as "inlined from SingleLinkList.h" at lines 54 / 65 / 76, which is
 *  why they live in the header rather than in the .c.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x001167a0.
 * ======================================================================== */

#ifndef _COMMON_SINGLELINKLIST_H
#define _COMMON_SINGLELINKLIST_H

#include <stddef.h>                 /* offsetof */

#include "eetypes.h"

/* One cell: forward link, then the element body.
 *
 * PORT NOTE: the ROM's body starts at 0x10 behind a 4-byte pNext.  The host
 * pointer is 8 bytes, so the gap to 0x10 is spelled out rather than left to
 * the compiler -- SingleLinkListCellAlloc() sizes its block off this offset
 * and the ROM's literal 0x10 has to keep meaning "the header". */
struct _SLL_CELL                    /* 0x20 on target */
{
    struct _SLL_CELL *pNext;                            /* 0x00 */
    u_char aPad[16 - sizeof(struct _SLL_CELL *)];
    char   BodyStart;                                   /* 0x10 */
};

typedef struct _SLL_CELL SLL_CELL;

typedef struct                      /* 0x10 */
{
    u_int     ElemSize;                                 /* 0x0 */
    u_int     RegCount;                                 /* 0x4 */
    SLL_CELL *pBeginCell;                               /* 0x8 */
    SLL_CELL *pEndCell;                                 /* 0xc */
} SINGLE_LINK_LIST;

/* Bytes of cell header ahead of the element body. */
#define SLL_CELL_HEADER_SIZE ((u_int)offsetof(SLL_CELL, BodyStart))

/* ---- inline accessors -------------------------------------------------- */

/* Registered element count; a null list reads as empty rather than faulting. */
static inline u_int SingleLinkListRegCount(const SINGLE_LINK_LIST *pSLL) /* 54 */
{
    if (pSLL == nullptr)
    {
        return 0;
    }

    return pSLL->RegCount;
}

/* First cell, or NULL while the list is empty.  Note the emptiness test goes
 * through RegCount, not through pBeginCell. */
static inline SLL_CELL *SingleLinkListBeginCell(const SINGLE_LINK_LIST *pSLL) /* 65 */
{
    if (SingleLinkListRegCount(pSLL) == 0)
    {
        return (SLL_CELL *)nullptr;
    }

    return pSLL->pBeginCell;
}

static inline SLL_CELL *SingleLinkListNextCell(const SLL_CELL *pCell)   /* 76 */
{
    if (pCell == (const SLL_CELL *)nullptr)
    {
        return (SLL_CELL *)nullptr;
    }

    return pCell->pNext;
}

/* ---- list lifetime ----------------------------------------------------- */

SINGLE_LINK_LIST *SingleLinkListAlloc(u_int ElemSize);
void  SingleLinkListInit(SINGLE_LINK_LIST *pSLL, u_int ElemSize);
void  SingleLinkListFree(SINGLE_LINK_LIST *pSLL);
void  SingleLinkListAllCellFree(SINGLE_LINK_LIST *pSLL);

/* ---- elements ---------------------------------------------------------- */

SLL_CELL *SingleLinkListAddEnd(SINGLE_LINK_LIST *pSLL, const void *pData);
int   SingleLinkListRemoveBegin(SINGLE_LINK_LIST *pSLL);
int   SingleLinkListRemove(SINGLE_LINK_LIST *pSLL, SLL_CELL *pDelCell);
void *SingleLinkListCellBodyPtr(const SLL_CELL *pCell);

#endif /* _COMMON_SINGLELINKLIST_H */
