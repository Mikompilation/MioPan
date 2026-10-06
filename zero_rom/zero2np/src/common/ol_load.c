/* ==========================================================================
 *  common/ol_load.c
 *
 *  Out-game load heap plus the asynchronous model loader built on top of it.
 *
 *  Heap: a single HEAP_WRK (ol_load_heap_wrk) carved out of the model-heap
 *  region by ol_loadHeapInit() at boot.  The out-game screens allocate their
 *  texture / data buffers from it and give them back when the screen tears
 *  down, so the region churns without touching the long-lived system heap.
 *
 *  Loader: OL_LOAD holds 30 OL_LOAD_ONE slots, each a refcounted handle on one
 *  file streaming into that heap.  A slot goes
 *
 *      free (file_no == -1)
 *        -> Req()      claim, allocate, FileLoadReqEE
 *        -> Main()     poll FileLoadIsEnd2 until load_end
 *        -> IsReady()  READY_FIRST once, then READY
 *        -> Release()  refcount to zero, free (or cancel a load in flight)
 *
 *  A slot whose allocation failed keeps file_no set with adrs == NULL; Main()
 *  retries the allocation every frame and IsReady() reports WAIT_MEMORY, so a
 *  request made while the heap is full completes as soon as room appears.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "ol_load.h"

#include "heapctrl.h"                       /* HEAP_WRK / heapCtrl* / SAFE_MALLOC */
#include "utility2.h"                       /* PRINT_ASSERT */
#include "../system/eeiop/cddat.h"          /* GetFileSize / GetFileName */
#include "../system/eeiop/fileload.h"       /* FileLoadReqEE / IsEnd2 / Cancel2 */

#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------------------
 *  Module state
 * ------------------------------------------------------------------------ */
static HEAP_WRK ol_load_heap_wrk;           /* bss  0x4bbac0 */
OL_LOAD         ol_load;                    /* data 0x339eb0 */

static void intr_LoadCancelFunc(void *buffer, void *dummy);

/* --------------------------------------------------------------------------
 *  Out-game load heap
 * ------------------------------------------------------------------------ */
/* Callers pass the raw EE memory-map constant (MODEL_HEAP_ADDR); my_mallocInit
 * translates the base once, so ol_loadGetHeap hands out host pointers. */
void ol_loadHeapInit(void *adrs, unsigned int size)
{
    heapCtrlInit(&ol_load_heap_wrk, adrs, size);
}

void ol_loadHeapReset(void *adrs, unsigned int size)
{
    heapCtrlReset(&ol_load_heap_wrk, adrs, size);
}

void *ol_loadGetHeap(int file_size)
{
    return SAFE_MALLOC(&ol_load_heap_wrk, (void *)0, file_size);
}

void ol_loadFreeHeap(void *buf)
{
    if (buf != (void *)0)
    {
        heapCtrlFree(&ol_load_heap_wrk, buf);
    }
}

void ol_loadDrawMemory(HEAP_DRAW_FUNC draw_rect_func,
                       HEAP_DRAW_FUNC draw_line_func,
                       int xx, int yy, int ww, int hh)
{
    heapCtrlDrawMemory(&ol_load_heap_wrk, draw_rect_func, draw_line_func,
                       xx, yy, ww, hh);
}

unsigned int ol_loadQueryMaxFreeSize(void)
{
    return heapCtrlQueryMaxOneSize(&ol_load_heap_wrk);
}

unsigned int ol_loadQueryTotalFreeSize(void)
{
    return heapCtrlMemSize(&ol_load_heap_wrk, HEAPMEM_LEAVE_SIZE);
}

/* --------------------------------------------------------------------------
 *  OL_LOAD_ONE::Req
 *
 *  Claim this slot for FileNo and start the transfer.  A failed allocation is
 *  not fatal: the slot keeps the file number and Main() retries.
 *
 *  `cnt` is this slot's first reference.  Writing it as a local `int cnt = 1`
 *  shadows the member and leaves the refcount at whatever it held, which makes
 *  Release() decrement 0 to -1, take the `cnt != 0` early out, and never free
 *  the slot -- every Req() then leaks a slot until the table is full and
 *  OL_LOAD::Req asserts "OL_LOAD Wrk Is Not Vacant".
 * ------------------------------------------------------------------------ */
OL_LOAD_ERR OL_LOAD_ONE::Req(int FileNo)
{                                                                       /* 188 */
    if (FileNo == -1)                                                   /* 190 */
    {
        PRINT_ASSERT("OL_LOAD_ONE Req");                                /* 191 */
    }

    mapping  = 0;                                                       /* 195 */
    load_end = 0;                                                       /* 196 */
    file_no  = FileNo;                                                  /* 197 */
    cnt      = 1;                                                       /* 198 */

    adrs = ol_loadGetHeap(GetFileSize(FileNo));                         /* 201 */
    if (adrs != (void *)0)                                              /* 203 */
    {
        FileLoadReqEE(FileNo, adrs, 7, (FILE_LOAD_CALLBACK)0, (void *)0); /* 206 */
    }

    return (adrs == (void *)nullptr) ? OL_LOAD_ERR_MEMORY_LACK          /* 207 */
                                     : OL_LOAD_ERR_OK;                  /* 209 */
}                                                                       /* 211 */

/* --------------------------------------------------------------------------
 *  OL_LOAD_ONE::Main
 *
 *  One frame of progress.  Returns non-zero only while a transfer is actually
 *  being polled -- an idle, a retrying and a finished slot all return 0.
 * ------------------------------------------------------------------------ */
int OL_LOAD_ONE::Main()
{
    if (file_no == -1)
    {
        return 0;
    }

    if (adrs == (void *)nullptr)
    {
        /* Allocation failed earlier (or the heap was full at Req time) -- try
         * again now and kick the load off if it lands. */
        adrs = ol_loadGetHeap(GetFileSize(file_no));
        if (adrs != (void *)nullptr)
        {
            FileLoadReqEE(file_no, adrs, 7, (FILE_LOAD_CALLBACK)0, (void *)0);
        }
        return 0;
    }

    if (load_end == 0)
    {
        load_end = FileLoadIsEnd2(file_no, adrs);
        return 1;
    }

    return 0;
}

/* --------------------------------------------------------------------------
 *  OL_LOAD_ONE::IsReady
 *
 *  Hands back the buffer and distinguishes the first successful query from
 *  later ones, so the caller can do its one-off pointer fixups exactly once.
 * ------------------------------------------------------------------------ */
OL_LOAD_READY OL_LOAD_ONE::IsReady(void **mdl_pp)
{
    if (load_end == 0)
    {
        *mdl_pp = nullptr;
        /* No buffer at all means we are still waiting on the heap, which the
         * caller may want to report differently from a transfer in flight. */
        return (adrs != (void *)nullptr) ? OL_LOAD_READY_NOT_READY
                                   : OL_LOAD_READY_WAIT_MEMORY;
    }

    *mdl_pp = this->adrs;

    if (mapping == 0)
    {
        mapping = 1;
        return OL_LOAD_READY_READY_FIRST;
    }

    return OL_LOAD_READY_READY;
}

/* --------------------------------------------------------------------------
 *  intr_LoadCancelFunc
 *
 *  Completion hook for a cancelled transfer: the loader owns the buffer until
 *  the request actually unwinds, so the free happens here rather than inline
 *  in Release().
 * ------------------------------------------------------------------------ */
static void intr_LoadCancelFunc(void *buffer, void *dummy)
{
    (void)dummy;
    ol_loadFreeHeap(buffer);
}

/* --------------------------------------------------------------------------
 *  OL_LOAD_ONE::Release
 *
 *  Drop one reference.  Returns 1 only when the last one went and the slot was
 *  actually freed.
 * ------------------------------------------------------------------------ */
int OL_LOAD_ONE::Release()
{
    void *buf;

    if (file_no == -1)
    {
        return 0;
    }

    cnt--;
    if (cnt != 0)
    {
        return 0;
    }

    buf = adrs;
    if (buf != (void *)nullptr)
    {
        if (load_end == 0)
        {
            /* Still in flight -- cancel and let the callback free it. */
            FileLoadCancel2(file_no, buf, intr_LoadCancelFunc, (void *)0);
        }
        else
        {
            ol_loadFreeHeap(buf);
        }
        adrs = (void *)nullptr;
    }

    file_no = -1;

    return 1;
}

/* --------------------------------------------------------------------------
 *  OL_LOAD_ONE::Print
 *
 *  NOTE: the original splits the file name with CFileName / basic_string and
 *  prints only the leaf; that decomposition is not reconstructed, so the whole
 *  path is printed here.  The format string and fields are the originals.
 * ------------------------------------------------------------------------ */
void OL_LOAD_ONE::Print(char *pSBuf)
{
    char StringTemp[1000];

    if (file_no == -1)
    {
        return;
    }

    sprintf(StringTemp, "F[%s] A[%p] S[%x] C[%d]\n", GetFileName(file_no), adrs, GetFileSize(file_no), cnt);
    strcat(pSBuf, StringTemp);
}

/* --------------------------------------------------------------------------
 *  OL_LOAD::Init
 *
 *  Mark every slot free.  Nothing else is cleared -- a slot is fully
 *  initialised by Req().
 * ------------------------------------------------------------------------ */
void OL_LOAD::Init()
{
    int i;

    for (i = 0; i < 30; i++)
    {
        one[i].Init();
    }
}

/* --------------------------------------------------------------------------
 *  OL_LOAD::Main
 * ------------------------------------------------------------------------ */
void OL_LOAD::Main()
{
    int i;

    for (i = 0; i < 30; i++)
    {
        one[i].Main();
    }
}

/* --------------------------------------------------------------------------
 *  OL_LOAD::Clear
 *
 *  Drop a reference on every slot holding FileNo.  The scan does not stop at
 *  the first hit.
 * ------------------------------------------------------------------------ */
void OL_LOAD::Clear(int FileNo)
{
    int i;

    for (i = 0; i < 30; i++)
    {
        if (one[i].GetFileNo() == FileNo)
        {
            one[i].Release();
        }
    }
}

/* --------------------------------------------------------------------------
 *  OL_LOAD::IsReady
 * ------------------------------------------------------------------------ */
OL_LOAD_READY OL_LOAD::IsReady(int FileNo, void **mdl_pp)
{
    for (int i = 0; i < 30; i++)
    {
        if (one[i].GetFileNo() == FileNo)
        {
            return one[i].IsReady(mdl_pp);
        }
    }

    return OL_LOAD_READY_NOT_READY;
}

/* --------------------------------------------------------------------------
 *  OL_LOAD::Req
 *
 *  Share the slot when the file is already loaded or loading, otherwise take
 *  a free one.  Two separate passes: every slot is checked for a match before
 *  any free slot is considered.
 * ------------------------------------------------------------------------ */
OL_LOAD_ERR OL_LOAD::Req(int FileNo)
{
    int i;

    for (i = 0; i < 30; i++)
    {
        if (one[i].GetFileNo() == FileNo)
        {
            one[i].CntUp();
            return OL_LOAD_ERR_OK;
        }
    }

    for (i = 0; i < 30; i++)
    {
        if (one[i].GetFileNo() == -1)
        {
            return one[i].Req(FileNo);
        }
    }

    PRINT_ASSERT("OL_LOAD Wrk Is Not Vacant");

    return OL_LOAD_ERR_WORK_LACK;
}

/* --------------------------------------------------------------------------
 *  OL_LOAD::Print
 *
 *  Dump the table into pBuf, or straight to stdout when pBuf is NULL.
 * ------------------------------------------------------------------------ */
void OL_LOAD::Print(char *pBuf)
{
    char  StringBuffer[2000];
    char  StringTemp[1000];

    char *str = pBuf;
    if (pBuf == (char *)nullptr)
    {
        str = StringBuffer;
    }

    sprintf(str, "\n");
    strcat(str, "\n");
    strcat(str, ">>> OL_LOAD STATE <<<\n");

    sprintf(StringTemp, ">TopAdrs[%p] Size[%x]\n", ol_load_heap_wrk.malloc.adrs, ol_load_heap_wrk.malloc.size);
    strcat(str, StringTemp);

    for (int i = 0; i < 30; i++)
    {
        one[i].Print(str);
    }

    strcat(str, "\n");
    strcat(str, "\n");

    if (pBuf == (char *)nullptr)
    {
        printf("%s\n", str);
    }
}
