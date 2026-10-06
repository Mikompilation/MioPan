// FILE: /home/zero_rom/zero2np/src/common/packfile.c
//
// Packed-file accessors.  FF2 room resources store a count at +0x00, then a
// sequence of records beginning at +0x10.  Each record has a 0x10-byte local
// header whose first word is the payload size; the payload begins at +0x10.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "packfile.h"

#include <stdint.h>

/* --------------------------------------------------------------------------
 *  Pk2GetNum / Pk2GetAddr
 *
 *  "pk2" packs carry an offset table rather than chained record headers:
 *  word 0 is the entry count, words 4.. are byte offsets from the base.
 * ------------------------------------------------------------------------ */
int Pk2GetNum(u_int *top_addr)
{
    if (top_addr == (u_int *)0)
    {
        return 0;
    }

    return (int)*top_addr;
}

u_int *Pk2GetAddr(u_int *top_addr, int index)
{
    if (index >= 0 && index < Pk2GetNum(top_addr))
    {
        return (u_int *)((char *)top_addr + top_addr[index + 4]);
    }

    return (u_int *)0;
}

/* Round up to the next quadword boundary. */
u_int *PakAlign128(u_int *addr)
{
    if (((uintptr_t)addr & 0xf) != 0)
    {
        addr = (u_int *)((uintptr_t)addr + (0x10 - ((uintptr_t)addr & 0xf)));
    }

    return addr;
}

/* --------------------------------------------------------------------------
 *  GetPakTaleAddr
 *
 *  Walk past every record in the pack and return the first aligned word after
 *  it -- i.e. scratch space immediately following the pack's payload.
 * ------------------------------------------------------------------------ */
void *GetPakTaleAddr(void *pak_head)
{
    unsigned char *entry;
    int            num;

    num = *(int *)pak_head;
    entry = (unsigned char *)pak_head + 0x10;

    while (num > 0)
    {
        entry += *(int *)entry + 0x10;
        num--;
    }

    return PakAlign128((u_int *)(entry + 0x10));
}

int GetNumInPak(void *pak_head)
{
    if (pak_head == (void *)nullptr)
    {
        return 0;
    }

    return *(int *)pak_head;
}

void *GetFileInPak(void *pak_head, int num)
{
    if (pak_head == (void *)nullptr || num < 0)
    {
        return (void *)nullptr;
    }

    int count = *(int *) pak_head;
    if (num >= count)
    {
        return (void *)nullptr;
    }

    unsigned char *entry = (unsigned char *) pak_head + 0x10;
    while (num > 0)
    {
        int size = *(int *)entry;
        if (size < 0)
        {
            return (void *)nullptr;
        }

        entry += size + 0x10;
        num--;
    }

    return entry + 0x10;
}
