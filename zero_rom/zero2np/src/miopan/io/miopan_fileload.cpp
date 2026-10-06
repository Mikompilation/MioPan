#include "miopan_fileload.h"

#include <libsd.h>          // MioPan_SpuRamPointer (SPU destinations)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../miopan_memory.h"        // MioPan_TryGetHostPointer
#include "libcdvd.h"                 // sceCdSearchFile / sceCdReadFile / sceCdlFILE
#include "../../system/encodes/cmp.h" // CMP_HEADER / CMP_Init / CMP_Decode

// ──────────────────────────────────────────────────────────────────────
// Host read/decode primitives (formerly the HostFileLoad* family in
// fileload.c).  These deal only in host I/O — no game/cddat concepts.

static int ReadNamed(const char *file_name, int offset, void *target,
                     unsigned int request_size)
{
    sceCdlFILE   file_info;
    unsigned int read_size;

    if (file_name == (const char *)0 || target == (void *)0)
    {
        return 0;
    }

    memset(&file_info, 0, sizeof(file_info));
    if (sceCdSearchFile(&file_info, file_name) == 0)
    {
        return 0;
    }

    if (offset < 0)
    {
        offset = 0;
    }

    if ((unsigned int)offset >= file_info.size)
    {
        read_size = 0;
    }
    else
    {
        read_size = file_info.size - (unsigned int)offset;
    }

    if (read_size > request_size)
    {
        read_size = request_size;
    }

    if (read_size != 0 &&
        sceCdReadFile(file_name, (u_int)offset, target, read_size) == 0)
    {
        return 0;
    }

    if (read_size < request_size)
    {
        memset((char *)target + read_size, 0, request_size - read_size);
    }

    return 1;
}

static int ReadArchive(int start_sector, void *target, unsigned int request_size)
{
    if (target == (void *)0)
    {
        return 0;
    }

    return sceCdReadFile("\\IMG_BD.BIN;1", (u_int)start_sector * 0x800u, target, request_size);
}

static int DecodeBuffer(void *cmp_buffer, void *target, unsigned int output_size)
{
    CMP_HEADER *header;
    int         decoded_size;

    if (cmp_buffer == (void *)0 || target == (void *)0)
    {
        return 0;
    }

    header = (CMP_HEADER *)cmp_buffer;
    if (header->div_num <= 0 || header->div_size <= 0 || header->data_offset <= 0)
    {
        return 0;
    }
    if (header->size < 0 || (unsigned int)header->size > output_size)
    {
        return 0;
    }

    CMP_Init(header);

    // CMP_Decode advances the output pointer by a full div_size for every one
    // of div_num blocks, so it writes div_num*div_size bytes — header->size
    // rounded up to a whole block, not to 16 like GetFileSize().  When the
    // file's tail block is short the final store runs past target's end (target
    // is only output_size bytes), corrupting the neighbouring heap chunk and
    // faulting the next free().  If the block footprint doesn't fit, decode
    // into a scratch buffer of the true footprint and copy back output_size.
    {
        size_t footprint = (size_t)header->div_num * (size_t)header->div_size;

        if (footprint <= output_size)
        {
            decoded_size = CMP_Decode(header, target);
        }
        else
        {
            void *scratch = malloc(footprint);
            if (scratch == (void *)0)
            {
                return 0;
            }

            decoded_size = CMP_Decode(header, scratch);
            if (decoded_size >= 0)
            {
                unsigned int copy_size = output_size;
                if ((unsigned int)decoded_size < copy_size)
                {
                    copy_size = (unsigned int)decoded_size;
                }
                memcpy(target, scratch, copy_size);
            }

            free(scratch);
        }
    }

    if (decoded_size < 0)
    {
        return 0;
    }

    if ((unsigned int)decoded_size < output_size)
    {
        memset((char *)target + decoded_size, 0, output_size - (unsigned int)decoded_size);
    }

    return 1;
}

static int DecodeNamed(const char *file_name, int offset, void *target,
                       unsigned int request_size, unsigned int output_size)
{
    void *cmp_buffer;
    int   ret;

    if (offset != 0 || request_size == 0)
    {
        return 0;
    }

    cmp_buffer = malloc(request_size);
    if (cmp_buffer == (void *)0)
    {
        return 0;
    }

    ret = 0;
    if (ReadNamed(file_name, 0, cmp_buffer, request_size) != 0)
    {
        ret = DecodeBuffer(cmp_buffer, target, output_size);
    }

    free(cmp_buffer);
    return ret;
}

static int DecodeArchive(int start_sector, void *target,
                         unsigned int request_size, unsigned int output_size)
{
    void *cmp_buffer;
    int   ret;

    if (request_size == 0)
    {
        return 0;
    }

    cmp_buffer = malloc(request_size);
    if (cmp_buffer == (void *)0)
    {
        return 0;
    }

    ret = 0;
    if (ReadArchive(start_sector, cmp_buffer, request_size) != 0)
    {
        ret = DecodeBuffer(cmp_buffer, target, output_size);
    }

    free(cmp_buffer);
    return ret;
}

// ──────────────────────────────────────────────────────────────────────
// The load-serve orchestration (formerly HostFileLoadReqSub, minus the RPC
// completion bookkeeping which stays on the reconstructed loader side).

extern "C" int MioPan_FileLoadServe(const MioPan_FileLoadReq *req)
{
    void            *target;
    unsigned int     request_size;
    int              loaded;
    int              read_size = 0;
    static int       missing_warn_count;

    if (req == (const MioPan_FileLoadReq *)0)
    {
        return 0;
    }

    request_size = (req->request_size < 0) ? 0u : (unsigned int)req->request_size;

    // Load destinations arrive in two flavours and MioPan_TryGetHostPointer
    // tells them apart by range:
    //   * a raw memory-map constant (EVENT_DATA_ADDR, MSG_DATA_ADDR, ...) is a
    //     PS2 address and must be translated -- the game reads those regions
    //     back through MioPan_GetHostPointer / Tim2GetHostAddress;
    //   * a heap buffer (mem_util / ol_load) is already a host pointer, because
    //     heapCtrlReset -> my_mallocInit translated the heap base once at init.
    // This only works because the emulated RAM block is allocated OUTSIDE
    // 0x00400000..0x05000000 (see MioPan_InitPs2Memory), so a translated
    // pointer can never be mistaken for a PS2 address and translated twice.
    if (req->is_spu != 0)
    {
        // An SPU address is an offset into the SPU's own 2 MB, not anything the
        // host can dereference; libsd owns that memory.  A bank body that does
        // not fit is dropped rather than written somewhere arbitrary.
        unsigned int spu_size = (req->raw_size != 0) ? req->raw_size : request_size;

        target = MioPan_SpuRamPointer((u_int)req->ee_adrs, spu_size);
        if (target == (void *)0)
        {
            printf("MioPan file load: SPU address %x + %x is outside SPU RAM [%s]\n",
                   (unsigned int)req->ee_adrs, spu_size,
                   req->cmp_file_name != (const char *)0 ? req->cmp_file_name : "");
            return 0;
        }
    }
    else if (MioPan_TryGetHostPointer(req->ee_adrs, &target) == 0)
    {
        target = (void *)req->ee_adrs;
    }

    loaded = 0;
    if (target != (void *)0 && req->is_cmp != 0)
    {
        if (req->raw_size != 0)
        {
            loaded = ReadNamed(req->raw_file_name, req->read_offset,
                               target, req->raw_size);
            if (loaded != 0)
            {
                read_size = req->wrk_size;
            }
        }
    }

    if (loaded == 0 && target != (void *)0)
    {
        if (req->is_cmp != 0)
        {
            loaded = DecodeNamed(req->cmp_file_name, req->read_offset,
                                 target, request_size, req->raw_size);
        }
        else
        {
            loaded = ReadNamed(req->cmp_file_name, req->read_offset,
                               target, request_size);
        }
        if (loaded != 0)
        {
            read_size = (int)request_size;
        }
    }

    if (loaded == 0 && target != (void *)0)
    {
        if (req->is_cmp != 0)
        {
            loaded = DecodeArchive(req->start_sector, target,
                                   request_size, req->raw_size);
        }
        else
        {
            loaded = ReadArchive(req->start_sector, target, request_size);
        }
        if (loaded != 0)
        {
            read_size = (int)request_size;
        }
    }

    if (loaded == 0)
    {
        if (target != (void *)0 && request_size != 0)
        {
            unsigned int clear_size = request_size;
            if (req->is_cmp != 0)
            {
                clear_size = req->raw_size;
            }
            memset(target, 0, clear_size);
        }

        if (missing_warn_count < 256)
        {
            printf("MioPan file load missing: file_no[%d] sector[%d] size[%x] cmp[%d] "
                   "raw_size[%x] [%s] [%s]\n",
                   req->file_no, req->start_sector, request_size, req->is_cmp,
                   req->raw_size, req->cmp_file_name, req->raw_file_name);
            missing_warn_count++;
        }
        read_size = (int)request_size;
    }

    return read_size;
}
