/* ==========================================================================
 *  common/ol_load.h
 *
 *  Two things share this translation unit:
 *
 *    * the out-game load heap -- a single HEAP_WRK the menu / title / logo /
 *      album screens allocate their transient buffers from;
 *    * OL_LOAD / OL_LOAD_ONE, the reference-counted asynchronous model loader
 *      the scene and ingame code use to stream 3D models in and out of that
 *      same heap.
 *
 *  OL_LOAD owns 30 OL_LOAD_ONE slots.  A slot is claimed by file number: Req()
 *  either bumps the refcount of a slot already holding that file or takes a
 *  free one and issues the load; Main() pumps every slot once a frame;
 *  IsReady() reports progress and hands back the buffer; Clear() drops a
 *  reference and frees the buffer when the last one goes.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _COMMON_OL_LOAD_H
#define _COMMON_OL_LOAD_H

#include "heapctrl.h"
#include "../graphics/graph3d/ctl/fixed_array.h"

/* --------------------------------------------------------------------------
 *  Out-game load heap
 * ------------------------------------------------------------------------ */
void         ol_loadHeapInit(void *adrs, unsigned int size);
void         ol_loadHeapReset(void *adrs, unsigned int size);
void        *ol_loadGetHeap(int file_size);
void         ol_loadFreeHeap(void *buf);
void         ol_loadDrawMemory(HEAP_DRAW_FUNC draw_rect_func,
                               HEAP_DRAW_FUNC draw_line_func,
                               int xx, int yy, int ww, int hh);
unsigned int ol_loadQueryMaxFreeSize();
unsigned int ol_loadQueryTotalFreeSize();

/* --------------------------------------------------------------------------
 *  Loader result codes
 * ------------------------------------------------------------------------ */
enum _OL_LOAD_ERR
{
    OL_LOAD_ERR_OK          = 0,
    OL_LOAD_ERR_MEMORY_LACK = 1,        /* heap could not satisfy the request */
    OL_LOAD_ERR_WORK_LACK   = 2,        /* all 30 slots busy                  */
    OL_LOAD_ERR_FORCE_DWORD = -1
};
typedef _OL_LOAD_ERR OL_LOAD_ERR;

enum OL_LOAD_READY
{
    OL_LOAD_READY_NOT_READY   = 0,      /* buffer allocated, load in flight   */
    OL_LOAD_READY_READY       = 1,      /* resident, already mapped once      */
    OL_LOAD_READY_READY_FIRST = 3,      /* resident, first time seen -- the
                                         * caller does its one-off fixups     */
    OL_LOAD_READY_WAIT_MEMORY = 4       /* no buffer yet; retrying each Main  */
};

/* --------------------------------------------------------------------------
 *  OL_LOAD_ONE -- one slot
 * ------------------------------------------------------------------------ */
class OL_LOAD_ONE                      /* 0x10 */
{
private:
    /* 0x0:0 */ unsigned int mapping  : 1;   /* IsReady() has reported it once */
    /* 0x0:1 */ unsigned int load_end : 1;   /* transfer complete              */
    /* 0x4 */   int          cnt;            /* reference count                */
    /* 0x8 */   void        *adrs;           /* heap buffer, NULL until got    */
    /* 0xc */   int          file_no;        /* -1 when the slot is free       */

public:
    /* Inlined in the original -- they show up as "inlined from ol_load.h" in
     * every caller rather than as their own functions. */
    void Init()       { file_no = -1; }
    void CntUp()      { cnt++; }
    int  GetFileNo()  { return file_no; }

    int          Main();
    int          Release();
    OL_LOAD_ERR  Req(int FileNo);
    OL_LOAD_READY IsReady(void **mdl_pp);
    void         Print(char *pSBuf);
};

/* --------------------------------------------------------------------------
 *  OL_LOAD -- the slot table
 * ------------------------------------------------------------------------ */
class OL_LOAD                          /* 0x1e0 */
{
private:
    /* 0x000 */ fixed_array<OL_LOAD_ONE, 30> one;

public:
    void          Init();
    void          Main();
    OL_LOAD_READY IsReady(int FileNo, void **mdl_pp);
    OL_LOAD_ERR   Req(int FileNo);
    void          Clear(int FileNo);
    void          Print(char *pBuf);
};

extern OL_LOAD ol_load;                 /* data 0x339eb0 */

#endif /* _COMMON_OL_LOAD_H */
