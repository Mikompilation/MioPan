// FILE: /home/zero_rom/zero2np/src/common/SingleLinkList.c
//
// Singly-linked list of fixed-size elements, backed by the effect heap.
// Used by the effect modules that grow and shrink their particle sets every
// frame (dripping water, falling leaves, butterflies).
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x001167a0.

#include "SingleLinkList.h"

#include <string.h>                              /* memcpy */

#include "../graphics/effect/effect.h"           /* EFFECT_MALLOC / EFFECT_FREE */

/* --------------------------------------------------------------------------
 *  Cells.  Every cell is its own heap block: header + ElemSize body.
 * ------------------------------------------------------------------------ */
static SLL_CELL *SingleLinkListCellAlloc(const SINGLE_LINK_LIST *pSLL)
{
    SLL_CELL *pCell;

    pCell = (SLL_CELL *)EFFECT_MALLOC((int)(pSLL->ElemSize +
                                            SLL_CELL_HEADER_SIZE));      /* 44 */
    if (pCell != (SLL_CELL *)nullptr)
    {
        pCell->pNext = (SLL_CELL *)nullptr;                              /* 51 */
    }

    return pCell;
}

static void SingleLinkListCellFree(SLL_CELL *pCell)
{
    if (pCell != (SLL_CELL *)nullptr)                                    /* 61 */
    {
        EFFECT_FREE(pCell);                                              /* 62 */
    }
}

/* --------------------------------------------------------------------------
 *  List lifetime.
 * ------------------------------------------------------------------------ */
SINGLE_LINK_LIST *SingleLinkListAlloc(u_int ElemSize)
{
    SINGLE_LINK_LIST *pSLL;

    pSLL = (SINGLE_LINK_LIST *)nullptr;                                  /* 75 */

    if (ElemSize != 0)
    {
        /* PORT NOTE: the ROM asks for its own literal 0x10.  The host struct
         * is 0x18 because both cell pointers doubled, so the request is sized
         * off sizeof() instead. */
        pSLL = (SINGLE_LINK_LIST *)EFFECT_MALLOC((int)sizeof(SINGLE_LINK_LIST));
        if (pSLL != (SINGLE_LINK_LIST *)nullptr)
        {
            SingleLinkListInit(pSLL, ElemSize);                          /* 84 */
        }
    }

    return pSLL;
}

void SingleLinkListInit(SINGLE_LINK_LIST *pSLL, u_int ElemSize)
{
    if (pSLL != (SINGLE_LINK_LIST *)nullptr)                             /* 95 */
    {
        pSLL->ElemSize   = ElemSize;
        pSLL->RegCount   = 0;
        pSLL->pBeginCell = (SLL_CELL *)nullptr;
        pSLL->pEndCell   = (SLL_CELL *)nullptr;                          /* 98 */
    }
}

/* Drop every cell, then the list header itself. */
void SingleLinkListFree(SINGLE_LINK_LIST *pSLL)
{
    if (pSLL != (SINGLE_LINK_LIST *)nullptr)
    {
        while (SingleLinkListRegCount(pSLL) != 0)
        {
            SingleLinkListRemoveBegin(pSLL);
        }

        EFFECT_FREE(pSLL);                                               /* 113 */
    }
}

/* As above but the header survives and is reset for reuse. */
void SingleLinkListAllCellFree(SINGLE_LINK_LIST *pSLL)
{
    if (pSLL != (SINGLE_LINK_LIST *)nullptr)
    {
        while (SingleLinkListRegCount(pSLL) != 0)
        {
            SingleLinkListRemoveBegin(pSLL);
        }

        pSLL->RegCount = 0;                                              /* 128 */
    }
}

/* --------------------------------------------------------------------------
 *  Elements.
 * ------------------------------------------------------------------------ */
SLL_CELL *SingleLinkListAddEnd(SINGLE_LINK_LIST *pSLL, const void *pData)
{
    SLL_CELL *pRetVal;
    SLL_CELL *pCell;

    pRetVal = (SLL_CELL *)nullptr;                                       /* 142 */

    if (pSLL != (SINGLE_LINK_LIST *)nullptr)
    {
        pCell = SingleLinkListCellAlloc(pSLL);
        if (pCell != (SLL_CELL *)nullptr)
        {
            memcpy(SingleLinkListCellBodyPtr(pCell), pData,
                   (size_t)(int)pSLL->ElemSize);

            if (pSLL->pEndCell == (SLL_CELL *)nullptr)
            {
                pSLL->pBeginCell = pCell;
            }
            else
            {
                pSLL->pEndCell->pNext = pCell;
            }

            pSLL->pEndCell = pCell;
            pSLL->RegCount = pSLL->RegCount + 1;
            pRetVal = pCell;                                             /* 159 */
        }
    }

    return pRetVal;
}

int SingleLinkListRemoveBegin(SINGLE_LINK_LIST *pSLL)
{
    int       RetVal;
    SLL_CELL *pCell;

    RetVal = 0;

    if (SingleLinkListRegCount(pSLL) != 0)
    {
        pCell            = pSLL->pBeginCell;
        pSLL->pBeginCell = pCell->pNext;

        SingleLinkListCellFree(pCell);

        RetVal         = 1;
        pSLL->RegCount = pSLL->RegCount - 1;

        /* Emptying the list has to clear pEndCell too, or AddEnd() would
         * chain onto a freed cell. */
        if (pSLL->RegCount == 0)
        {
            pSLL->pEndCell = (SLL_CELL *)nullptr;                        /* 183 */
        }
    }

    return RetVal;
}

int SingleLinkListRemove(SINGLE_LINK_LIST *pSLL, SLL_CELL *pDelCell)
{
    int       RetVal;
    SLL_CELL *pCell;

    RetVal = 0;

    if (SingleLinkListRegCount(pSLL) != 0)
    {
        if (pSLL->pBeginCell == pDelCell)
        {
            RetVal = SingleLinkListRemoveBegin(pSLL);
        }
        else
        {
            /* Walk to the cell in front of pDelCell.  A cell that is not on
             * this list runs off the end and leaves with RetVal 0.  The
             * end-of-list test comes before the match test, which is what
             * makes a NULL pDelCell fail rather than fault. */
            SLL_CELL *pNext = pSLL->pBeginCell;

            do
            {
                pCell = pNext;
                pNext = pCell->pNext;

                if (pNext == (SLL_CELL *)nullptr)
                {
                    return 0;
                }
            } while (pNext != pDelCell);

            pCell->pNext = pDelCell->pNext;

            if (pSLL->pEndCell == pDelCell)
            {
                pSLL->pEndCell = pCell;
            }

            SingleLinkListCellFree(pDelCell);

            RetVal         = 1;
            pSLL->RegCount = pSLL->RegCount - 1;                         /* 222 */
        }
    }

    return RetVal;
}

void *SingleLinkListCellBodyPtr(const SLL_CELL *pCell)
{
    return (void *)&pCell->BodyStart;                                    /* 231 */
}
