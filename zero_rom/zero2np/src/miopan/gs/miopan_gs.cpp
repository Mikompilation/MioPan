#include "miopan_gs.h"
#include "miopan_gs_c.h"
#include "../miopan_profiler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <vector>

namespace MioPan::GS
{
static constexpr int kGsVramSize = 4 * 1024 * 1024;

static constexpr int kBlockTablePSMCT32[] = {
    0, 1, 4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23,
    8, 9, 12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
};

static constexpr int kColumnTablePSMCT32[] = {
    0, 1, 4, 5, 8, 9, 12, 13, 2, 3, 6, 7, 10, 11, 14, 15,
};

static constexpr int kBlockTablePSMCT16[] = {
    0,  2,  8,  10, 1,  3,  9,  11, 4,  6,  12, 14, 5,  7,  13, 15,
    16, 18, 24, 26, 17, 19, 25, 27, 20, 22, 28, 30, 21, 23, 29, 31,
};

static constexpr int kColumnTablePSMCT16[] = {
    0, 2, 8, 10, 16, 18, 24, 26, 1, 3, 9, 11, 17, 19, 25, 27,
    4, 6, 12, 14, 20, 22, 28, 30, 5, 7, 13, 15, 21, 23, 29, 31,
};

static constexpr int kBlockTablePSMT8[] = {
    0, 1, 4,  5,  16, 17, 20, 21, 2,  3,  6,  7,  18, 19, 22, 23,
    8, 9, 12, 13, 24, 25, 28, 29, 10, 11, 14, 15, 26, 27, 30, 31,
};

static constexpr int kColumnTablePSMT8[] = {
    0,   4,   16,  20,  32,  36,  48,  52,  2,   6,   18,  22,  34,  38,  50,  54,
    8,   12,  24,  28,  40,  44,  56,  60,  10,  14,  26,  30,  42,  46,  58,  62,
    33,  37,  49,  53,  1,   5,   17,  21,  35,  39,  51,  55,  3,   7,   19,  23,
    41,  45,  57,  61,  9,   13,  25,  29,  43,  47,  59,  63,  11,  15,  27,  31,
    96,  100, 112, 116, 64,  68,  80,  84,  98,  102, 114, 118, 66,  70,  82,  86,
    104, 108, 120, 124, 72,  76,  88,  92,  106, 110, 122, 126, 74,  78,  90,  94,
    65,  69,  81,  85,  97,  101, 113, 117, 67,  71,  83,  87,  99,  103, 115, 119,
    73,  77,  89,  93,  105, 109, 121, 125, 75,  79,  91,  95,  107, 111, 123, 127,
    128, 132, 144, 148, 160, 164, 176, 180, 130, 134, 146, 150, 162, 166, 178, 182,
    136, 140, 152, 156, 168, 172, 184, 188, 138, 142, 154, 158, 170, 174, 186, 190,
    161, 165, 177, 181, 129, 133, 145, 149, 163, 167, 179, 183, 131, 135, 147, 151,
    169, 173, 185, 189, 137, 141, 153, 157, 171, 175, 187, 191, 139, 143, 155, 159,
    224, 228, 240, 244, 192, 196, 208, 212, 226, 230, 242, 246, 194, 198, 210, 214,
    232, 236, 248, 252, 200, 204, 216, 220, 234, 238, 250, 254, 202, 206, 218, 222,
    193, 197, 209, 213, 225, 229, 241, 245, 195, 199, 211, 215, 227, 231, 243, 247,
    201, 205, 217, 221, 233, 237, 249, 253, 203, 207, 219, 223, 235, 239, 251, 255,
};

static constexpr int kBlockTablePSMT4[] = {
    0, 2, 8, 10, 1, 3, 9, 11, 4, 6, 12, 14, 5, 7, 13, 15,
};

static constexpr int kColumnTablePSMT4[] = {
    0,   8,   32,  40,  64,  72,  96,  104, 2,   10,  34,  42,  66,  74,  98,  106,
    4,   12,  36,  44,  68,  76,  100, 108, 6,   14,  38,  46,  70,  78,  102, 110,
    16,  24,  48,  56,  80,  88,  112, 120, 18,  26,  50,  58,  82,  90,  114, 122,
    20,  28,  52,  60,  84,  92,  116, 124, 22,  30,  54,  62,  86,  94,  118, 126,
    65,  73,  97,  105, 1,   9,   33,  41,  67,  75,  99,  107, 3,   11,  35,  43,
    69,  77,  101, 109, 5,   13,  37,  45,  71,  79,  103, 111, 7,   15,  39,  47,
    81,  89,  113, 121, 17,  25,  49,  57,  83,  91,  115, 123, 19,  27,  51,  59,
    85,  93,  117, 125, 21,  29,  53,  61,  87,  95,  119, 127, 23,  31,  55,  63,
    192, 200, 224, 232, 128, 136, 160, 168, 194, 202, 226, 234, 130, 138, 162, 170,
    196, 204, 228, 236, 132, 140, 164, 172, 198, 206, 230, 238, 134, 142, 166, 174,
    208, 216, 240, 248, 144, 152, 176, 184, 210, 218, 242, 250, 146, 154, 178, 186,
    212, 220, 244, 252, 148, 156, 180, 188, 214, 222, 246, 254, 150, 158, 182, 190,
    129, 137, 161, 169, 193, 201, 225, 233, 131, 139, 163, 171, 195, 203, 227, 235,
    133, 141, 165, 173, 197, 205, 229, 237, 135, 143, 167, 175, 199, 207, 231, 239,
    145, 153, 177, 185, 209, 217, 241, 249, 147, 155, 179, 187, 211, 219, 243, 251,
    149, 157, 181, 189, 213, 221, 245, 253, 151, 159, 183, 191, 215, 223, 247, 255,
    256, 264, 288, 296, 320, 328, 352, 360, 258, 266, 290, 298, 322, 330, 354, 362,
    260, 268, 292, 300, 324, 332, 356, 364, 262, 270, 294, 302, 326, 334, 358, 366,
    272, 280, 304, 312, 336, 344, 368, 376, 274, 282, 306, 314, 338, 346, 370, 378,
    276, 284, 308, 316, 340, 348, 372, 380, 278, 286, 310, 318, 342, 350, 374, 382,
    321, 329, 353, 361, 257, 265, 289, 297, 323, 331, 355, 363, 259, 267, 291, 299,
    325, 333, 357, 365, 261, 269, 293, 301, 327, 335, 359, 367, 263, 271, 295, 303,
    337, 345, 369, 377, 273, 281, 305, 313, 339, 347, 371, 379, 275, 283, 307, 315,
    341, 349, 373, 381, 277, 285, 309, 317, 343, 351, 375, 383, 279, 287, 311, 319,
    448, 456, 480, 488, 384, 392, 416, 424, 450, 458, 482, 490, 386, 394, 418, 426,
    452, 460, 484, 492, 388, 396, 420, 428, 454, 462, 486, 494, 390, 398, 422, 430,
    464, 472, 496, 504, 400, 408, 432, 440, 466, 474, 498, 506, 402, 410, 434, 442,
    468, 476, 500, 508, 404, 412, 436, 444, 470, 478, 502, 510, 406, 414, 438, 446,
    385, 393, 417, 425, 449, 457, 481, 489, 387, 395, 419, 427, 451, 459, 483, 491,
    389, 397, 421, 429, 453, 461, 485, 493, 391, 399, 423, 431, 455, 463, 487, 495,
    401, 409, 433, 441, 465, 473, 497, 505, 403, 411, 435, 443, 467, 475, 499, 507,
    405, 413, 437, 445, 469, 477, 501, 509, 407, 415, 439, 447, 471, 479, 503, 511,
};

GSHelper gsHelper;
}

using MioPan::GS::PSMCT16;
using MioPan::GS::PSMCT16S;
using MioPan::GS::PSMCT24;
using MioPan::GS::PSMCT32;
using MioPan::GS::PSMT4;
using MioPan::GS::PSMT4HH;
using MioPan::GS::PSMT4HL;
using MioPan::GS::PSMT8;
using MioPan::GS::PSMT8H;
using MioPan::GS::PSMZ16;
using MioPan::GS::PSMZ16S;
using MioPan::GS::PSMZ24;
using MioPan::GS::PSMZ32;

struct GsFrameMetrics
{
    /* 64-bit because these run for the whole session: the live totals below
     * are deltaed by the caller, and a 32-bit byte count wraps in under two
     * minutes at the upload rates a busy room sustains. */
    uint64_t upload_count;
    uint64_t upload_bytes;
    double upload_ms;
    uint64_t download_count;
    uint64_t download_bytes;
    double download_ms;
};

struct GsUploadRegion
{
    int addr;
    int size;
};

static GsFrameMetrics g_gs_frame_metrics = {0, 0, 0.0, 0, 0, 0.0};
static GsFrameMetrics g_gs_last_frame = {0, 0, 0.0, 0, 0, 0.0};
static std::vector<GsUploadRegion> g_pending_uploads;
static std::vector<unsigned char> g_texture_buffer;
static bool g_first_upload_done = false;
/* MioPan_GsSetUploadSuppressed(). */
static bool g_upload_suppressed = false;
/* MioPan_GsSetUploadObserver(). */
static void (*g_upload_observer)(const sceGsLoadImage *, void *) = nullptr;
static void *g_upload_observer_user = nullptr;

static uint64_t NowNs()
{
    return (uint64_t)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

static inline bool InRangeGsByte(const std::vector<unsigned char>& mem, int addr) noexcept
{
    return addr >= 0 && static_cast<size_t>(addr) < mem.size();
}

static unsigned char ReadGsByte(int addr)
{
    const auto &mem = MioPan::GS::gsHelper.mem_;
    if (addr >= 0)
    {
        const auto index = static_cast<size_t>(addr);
        if (index < mem.size())
        {
            return mem[index];
        }
    }
    return 0;
}

static void WriteGsByte(int addr, unsigned char value)
{
    auto& mem = MioPan::GS::gsHelper.mem_;
    if (addr >= 0)
    {
        const size_t index = static_cast<size_t>(addr);
        if (index < mem.size())
        {
            mem[index] = value;
        }
    }
}

static int GetBlockIdPSMCT32(int block, int x, int y)
{
    const int block_y = (y >> 3) & 0x03;
    const int block_x = (x >> 3) & 0x07;
    return block + ((x >> 1) & ~0x1f) +
           MioPan::GS::kBlockTablePSMCT32[(block_y << 3) | block_x];
}

static int GetPixelAddressPSMCT32(int block, int width, int x, int y)
{
    const int page = (block >> 5) + (y >> 5) * width + (x >> 6);
    const int column_base = ((y >> 1) & 0x03) << 4;
    const int column_y = y & 0x01;
    const int column_x = x & 0x07;
    const int column =
        column_base + MioPan::GS::kColumnTablePSMCT32[(column_y << 3) | column_x];
    const int addr =
        ((page << 11) +
         (GetBlockIdPSMCT32(block & 0x1f, x & 0x3f, y & 0x1f) << 6) +
         column);
    return (addr << 2) & 0x003ffffc;
}

static int GetBlockIdPSMCT16(int block, int x, int y)
{
    const int block_y = (y >> 3) & 0x07;
    const int block_x = (x >> 4) & 0x03;
    return block + MioPan::GS::kBlockTablePSMCT16[(block_y << 2) | block_x];
}

static int GetPixelAddressPSMCT16(int block, int width, int x, int y)
{
    const int page = (block >> 5) + (y >> 6) * width + (x >> 6);
    const int column_base = ((y >> 1) & 0x03) << 5;
    const int column_y = y & 0x01;
    const int column_x = x & 0x0f;
    const int column =
        column_base + MioPan::GS::kColumnTablePSMCT16[(column_y << 4) | column_x];
    const int addr = (page << 13) +
                     (GetBlockIdPSMCT16(block & 0x1f, x & 0x3f, y & 0x3f) << 8) +
                     (column << 1);
    return addr & 0x003ffffe;
}

static inline int GetBlockIdPSMT8(int block, int x, int y)
{
    const int block_y = (y >> 4) & 0x03;
    const int block_x = (x >> 4) & 0x07;
    return block + ((x >> 2) & ~0x1f) +
           MioPan::GS::kBlockTablePSMT8[(block_y << 3) | block_x];
}

static inline int GetPixelAddressPSMT8(int block, int width, int x, int y)
{
    const int page = (block >> 5) + (y >> 6) * (width >> 1) + (x >> 7);
    const int block_id = GetBlockIdPSMT8(block & 0x1f, x & 0x7f, y & 0x3f);
    const int column_index = ((y & 0x0f) << 4) | (x & 0x0f);
    const int column = MioPan::GS::kColumnTablePSMT8[column_index];
    return (page << 13) + (block_id << 8) + column;
}

static int GetBlockIdPSMT4(int block, int x, int y)
{
    const int block_base = ((y >> 6) & 0x01) << 4;
    const int block_y = (y >> 4) & 0x03;
    const int block_x = (x >> 5) & 0x03;
    return block + ((x >> 2) & ~0x1f) + block_base +
           MioPan::GS::kBlockTablePSMT4[(block_y << 2) | block_x];
}

static int GetPixelAddressPSMT4(int block, int width, int x, int y)
{
    const int page = (block >> 5) + (y >> 7) * (width >> 1) + (x >> 7);
    const int column_y = y & 0x0f;
    const int column_x = x & 0x1f;
    const int column = MioPan::GS::kColumnTablePSMT4[(column_y << 5) | column_x];
    return (page << 14) +
           (GetBlockIdPSMT4(block & 0x1f, x & 0x7f, y & 0x7f) << 9) +
           column;
}

static int GetTex0BufferWidth(const sceGsTex0 *tex0)
{
    if (tex0 == nullptr)
    {
        return 1;
    }

    return tex0->TBW > 0 ? (int)tex0->TBW : 1;
}

static int TextureByteSize(int psm, int width, int height)
{
    if (width <= 0 || height <= 0)
    {
        return 0;
    }

    switch ((MioPan::GS::PixelStorageFormat)psm)
    {
        case PSMZ32:
        case PSMCT32:
            return width * height * 4;
        case PSMZ24:
        case PSMCT24:
            return width * height * 3;
        case PSMZ16:
        case PSMZ16S:
        case PSMCT16:
        case PSMCT16S:
            return width * height * 2;
        case PSMT8:
        case PSMT8H:
            return width * height;
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
            return (width * height + 1) >> 1;
        default:
            return 0;
    }
}

static int GetRawTextureRegion(sceGsTex0 *tex0, int *out_addr, int *out_size)
{
    if (tex0 == nullptr || out_addr == nullptr || out_size == nullptr)
    {
        return 0;
    }

    const int width = 1 << tex0->TW;
    const int height = 1 << tex0->TH;
    int addr = 0;
    int size = TextureByteSize((int)tex0->PSM, width, height);

    switch ((MioPan::GS::PixelStorageFormat)tex0->PSM)
    {
        case PSMZ32:
        case PSMZ24:
        case PSMCT32:
        case PSMCT24:
            addr = GetPixelAddressPSMCT32(tex0->TBP0, tex0->TBW, 0, 0);
            break;
        case PSMZ16:
        case PSMZ16S:
        case PSMCT16:
        case PSMCT16S:
            addr = GetPixelAddressPSMCT16(tex0->TBP0, tex0->TBW, 0, 0);
            break;
        case PSMT8:
        case PSMT8H:
            addr = GetPixelAddressPSMT8(tex0->TBP0, tex0->TBW, 0, 0);
            break;
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
            addr = GetPixelAddressPSMT4(tex0->TBP0, tex0->TBW, 0, 0) >> 1;
            break;
        default:
            return 0;
    }

    if (size <= 0 || !InRangeGsByte(MioPan::GS::gsHelper.mem_, addr))
    {
        return 0;
    }

    /* Both ends, not just the start.  GetPixelAddress* masks its result to
     * 0x3ffffc, so an address near the top of VRAM passes a start-only check
     * and then walks off the end of mem_ when the caller reads `size` bytes
     * from it -- MioPan_GetTextureHash() hashes the whole span in one go.
     *
     * Clamp rather than reject.  TW/TH are log2 sizes, so a TEX0 routinely
     * declares more than it uses: the smallest legal pair covering the
     * movie's 640x448 page is 1024x512, which is 2 MB declared at block
     * 0x2bc0 and runs 770 KB past the end of 4 MB VRAM.  The GS never reads
     * those texels because the sprite's UVs stop at 640x448 -- and that part
     * does fit -- so hashing what exists is the right answer.  Rejecting the
     * whole texture made GetTexture() hand back nullptr and the movie drew
     * nothing at all. */
    if ((size_t)addr + (size_t)size > MioPan::GS::gsHelper.mem_.size())
    {
        size = (int)(MioPan::GS::gsHelper.mem_.size() - (size_t)addr);
    }

    *out_addr = addr;
    *out_size = size;
    return 1;
}

static int GetClutRegion(sceGsTex0 *tex0, int *out_addr, int *out_size)
{
    if (tex0 == nullptr || out_addr == nullptr || out_size == nullptr)
    {
        return 0;
    }

    if (tex0->PSM != PSMT8 && tex0->PSM != PSMT8H &&
        tex0->PSM != PSMT4 && tex0->PSM != PSMT4HL &&
        tex0->PSM != PSMT4HH)
    {
        return 0;
    }

    int min_addr = (int)MioPan::GS::gsHelper.mem_.size();
    int max_addr = 0;
    const int clut_width = GetTex0BufferWidth(tex0);

    for (int cy = 0; cy < 16; cy++)
    {
        for (int cx = 0; cx < 16; cx++)
        {
            const int addr = GetPixelAddressPSMCT32(tex0->CBP, clut_width, cx, cy);
            min_addr = std::min(min_addr, addr);
            max_addr = std::max(max_addr, addr + 4);
        }
    }

    if (!InRangeGsByte(MioPan::GS::gsHelper.mem_, min_addr))
    {
        return 0;
    }

    *out_addr = min_addr;
    *out_size = max_addr - min_addr;
    return 1;
}

static int GetTextureInvalidationRegion(sceGsTex0 *tex0, int *out_addr, int *out_size)
{
    int tex_addr;
    int tex_size;
    int clut_addr;
    int clut_size;

    if (!GetRawTextureRegion(tex0, &tex_addr, &tex_size))
    {
        return 0;
    }

    if (GetClutRegion(tex0, &clut_addr, &clut_size))
    {
        const int region_addr = std::min(tex_addr, clut_addr);
        const int region_end = std::max(tex_addr + tex_size, clut_addr + clut_size);
        *out_addr = region_addr;
        *out_size = region_end - region_addr;
        return 1;
    }

    *out_addr = tex_addr;
    *out_size = tex_size;
    return 1;
}

static int GetUploadRegion(sceGsLoadImage *image_load, int *out_addr, int *out_size)
{
    if (image_load == nullptr || out_addr == nullptr || out_size == nullptr)
    {
        return 0;
    }

    const int width = (int)image_load->trxreg.RRW;
    const int height = (int)image_load->trxreg.RRH;
    int addr = 0;
    int size = TextureByteSize((int)image_load->bitbltbuf.DPSM, width, height);

    switch ((MioPan::GS::PixelStorageFormat)image_load->bitbltbuf.DPSM)
    {
        case PSMZ32:
        case PSMZ24:
        case PSMCT32:
        case PSMCT24:
            addr = GetPixelAddressPSMCT32(image_load->bitbltbuf.DBP,
                                          image_load->bitbltbuf.DBW,
                                          image_load->trxpos.DSAX,
                                          image_load->trxpos.DSAY);
            break;
        case PSMZ16:
        case PSMZ16S:
        case PSMCT16:
        case PSMCT16S:
            addr = GetPixelAddressPSMCT16(image_load->bitbltbuf.DBP,
                                          image_load->bitbltbuf.DBW,
                                          image_load->trxpos.DSAX,
                                          image_load->trxpos.DSAY);
            break;
        case PSMT8:
        case PSMT8H:
            addr = GetPixelAddressPSMT8(image_load->bitbltbuf.DBP,
                                        image_load->bitbltbuf.DBW,
                                        image_load->trxpos.DSAX,
                                        image_load->trxpos.DSAY);
            break;
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
            addr = GetPixelAddressPSMT4(image_load->bitbltbuf.DBP,
                                        image_load->bitbltbuf.DBW,
                                        image_load->trxpos.DSAX,
                                        image_load->trxpos.DSAY) >>
                   1;
            break;
        default:
            return 0;
    }

    /* Both ends, not just the start.  GetPixelAddress* masks its result to
     * 0x3ffffc, so an address near the top of VRAM passes a start-only check
     * and then walks off the end of mem_ when the caller reads `size` bytes
     * from it -- MioPan_GetTextureHash() hashes the whole span in one go. */
    if (size <= 0 || !InRangeGsByte(MioPan::GS::gsHelper.mem_, addr) ||
        (size_t)addr + (size_t)size > MioPan::GS::gsHelper.mem_.size())
    {
        return 0;
    }

    *out_addr = addr;
    *out_size = size;
    return 1;
}

/*
 * Content hash of a span of GS memory.
 *
 * This is hotter than it looks.  MioPan_GetTextureHash() runs it over a
 * texture's whole GS span -- 64 KiB for a 256x256 PSMT8, 256 KiB for a
 * PSMCT32 -- and the renderer calls that on every TEX0 cache miss, including
 * the ones that go on to hit the content cache and decode nothing.  A quiet
 * room frame that downloads no textures at all still spends milliseconds here.
 *
 * One byte per iteration of `hash ^= b; hash *= prime;` is a serial dependency
 * chain through a multiply, so it retired roughly one byte every four cycles.
 * Reading 32 bytes into four independent lanes lets those multiplies pipeline
 * instead of queueing behind each other.
 *
 * Same contract as before: a pure function of the bytes, chainable through
 * `seed`, no alignment requirement (the memcpy compiles to a plain load).  The
 * values it produces differ from the old hash, which only means live cache
 * entries re-key once on the build this ships in -- no hash is persisted.
 */
static uint64_t HashBytes(const void *data, int size, uint64_t seed)
{
    const uint64_t kP0 = 0x9e3779b97f4a7c15ull;
    const uint64_t kP1 = 0xbf58476d1ce4e5b9ull;
    const uint64_t kP2 = 0x94d049bb133111ebull;

    const auto *bytes = static_cast<const unsigned char *>(data);
    size_t remaining = size > 0 ? (size_t)size : (size_t)0;
    const uint64_t base = seed ? seed : 14695981039346656037ull;
    uint64_t lane0 = base ^ kP0;
    uint64_t lane1 = base ^ kP1;
    uint64_t lane2 = base ^ kP2;
    uint64_t lane3 = base + kP0;

    while (remaining >= 32)
    {
        uint64_t a;
        uint64_t b;
        uint64_t c;
        uint64_t d;

        std::memcpy(&a, bytes + 0, sizeof(a));
        std::memcpy(&b, bytes + 8, sizeof(b));
        std::memcpy(&c, bytes + 16, sizeof(c));
        std::memcpy(&d, bytes + 24, sizeof(d));

        lane0 = (lane0 ^ a) * kP0;
        lane1 = (lane1 ^ b) * kP1;
        lane2 = (lane2 ^ c) * kP2;
        lane3 = (lane3 ^ d) * kP0;

        bytes += 32;
        remaining -= 32;
    }

    uint64_t hash = lane0 ^ ((lane1 << 17) | (lane1 >> 47)) ^
                    ((lane2 << 34) | (lane2 >> 30)) ^
                    ((lane3 << 51) | (lane3 >> 13));

    while (remaining >= 8)
    {
        uint64_t a;

        std::memcpy(&a, bytes, sizeof(a));
        hash = (hash ^ a) * kP0;
        bytes += 8;
        remaining -= 8;
    }
    while (remaining != 0)
    {
        hash = (hash ^ *bytes++) * 1099511628211ull;
        remaining--;
    }

    /* Avalanche, so a span differing in one tail byte does not collide with a
     * near neighbour in the low bits the cache's bucket index reads. */
    hash ^= hash >> 33;
    hash *= kP1;
    hash ^= hash >> 29;
    hash *= kP2;
    hash ^= hash >> 32;
    return hash;
}

static unsigned char AdjustPS2Alpha(unsigned char alpha)
{
    return alpha <= 127 ? (unsigned char)(alpha << 1) : (unsigned char)0xff;
}

namespace MioPan::GS
{
GSHelper::GSHelper() : mem_(kGsVramSize)
{
}

void GSHelper::UploadPSMCT32(int dbp, int dbw, int dsax, int dsay, int rrw,
                             int rrh, const uint8_t *inbuf)
{
    int src_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMCT32(dbp, dbw, x, y);
            WriteGsByte(addr + 0, inbuf[src_addr + 0]);
            WriteGsByte(addr + 1, inbuf[src_addr + 1]);
            WriteGsByte(addr + 2, inbuf[src_addr + 2]);
            WriteGsByte(addr + 3, inbuf[src_addr + 3]);
            src_addr += 4;
        }
    }
}

void GSHelper::UploadPSMCT24(int dbp, int dbw, int dsax, int dsay, int rrw,
                             int rrh, const uint8_t *inbuf)
{
    int src_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMCT32(dbp, dbw, x, y);
            WriteGsByte(addr + 0, inbuf[src_addr + 0]);
            WriteGsByte(addr + 1, inbuf[src_addr + 1]);
            WriteGsByte(addr + 2, inbuf[src_addr + 2]);
            src_addr += 3;
        }
    }
}

void GSHelper::UploadPSMCT16(int dbp, int dbw, int dsax, int dsay, int rrw,
                             int rrh, const uint8_t *inbuf)
{
    int src_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMCT16(dbp, dbw, x, y);
            WriteGsByte(addr + 0, inbuf[src_addr + 0]);
            WriteGsByte(addr + 1, inbuf[src_addr + 1]);
            src_addr += 2;
        }
    }
}

void GSHelper::UploadPSMT8(int dbp, int dbw, int dsax, int dsay, int rrw,
                           int rrh, const uint8_t *inbuf, int psm)
{
    int src_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            if (psm == PSMT8H)
            {
                // PSMT8H stores the index in the top byte of the PSMCT32 word.
                WriteGsByte(GetPixelAddressPSMCT32(dbp, dbw, x, y) + 3,
                            inbuf[src_addr++]);
            }
            else
            {
                WriteGsByte(GetPixelAddressPSMT8(dbp, dbw, x, y), inbuf[src_addr++]);
            }
        }
    }
}

void GSHelper::UploadPSMT4(int dbp, int dbw, int dsax, int dsay, int rrw,
                           int rrh, const uint8_t *inbuf, int psm)
{
    int src_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int src_nibble =
                (inbuf[src_addr >> 1] >> ((src_addr & 1) << 2)) & 0x0f;
            if (psm == PSMT4HL || psm == PSMT4HH)
            {
                // "High" 4-bit formats pack into the top byte of the PSMCT32
                // word at this texel — bits 24..27 (HL) or 28..31 (HH) — so
                // the download reads back the same nibble.  Must match
                // DownloadImagePSMT4's PSMT4HL/HH branch.
                const int p32 = GetPixelAddressPSMCT32(dbp, dbw, x, y) + 3;
                const unsigned char old = ReadGsByte(p32);
                if (psm == PSMT4HL)
                {
                    WriteGsByte(p32, (unsigned char)((old & 0xf0) | src_nibble));
                }
                else
                {
                    WriteGsByte(p32, (unsigned char)((old & 0x0f) | (src_nibble << 4)));
                }
            }
            else
            {
                const int addr = GetPixelAddressPSMT4(dbp, dbw, x, y);
                const int dst_shift = (addr & 1) << 2;
                const int dst_addr = addr >> 1;
                const unsigned char old = ReadGsByte(dst_addr);
                WriteGsByte(dst_addr, (unsigned char)((src_nibble << dst_shift) |
                                                      (old & (0xf0 >> dst_shift))));
            }
            src_addr++;
        }
    }
}

void GSHelper::DownloadPSMCT32(unsigned char *outbuf, int dbp, int dbw,
                               int dsax, int dsay, int rrw, int rrh)
{
    int dst_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMCT32(dbp, dbw, x, y);
            outbuf[dst_addr + 0] = ReadGsByte(addr + 0);
            outbuf[dst_addr + 1] = ReadGsByte(addr + 1);
            outbuf[dst_addr + 2] = ReadGsByte(addr + 2);
            outbuf[dst_addr + 3] = ReadGsByte(addr + 3);
            dst_addr += 4;
        }
    }
}

void GSHelper::StorePSMCT24(unsigned char *outbuf, int dbp, int dbw, int dsax,
                            int dsay, int rrw, int rrh)
{
    int dst_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMCT32(dbp, dbw, x, y);
            outbuf[dst_addr + 0] = ReadGsByte(addr + 0);
            outbuf[dst_addr + 1] = ReadGsByte(addr + 1);
            outbuf[dst_addr + 2] = ReadGsByte(addr + 2);
            dst_addr += 3;
        }
    }
}

void GSHelper::StorePSMCT16(unsigned char *outbuf, int dbp, int dbw, int dsax,
                            int dsay, int rrw, int rrh)
{
    int dst_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMCT16(dbp, dbw, x, y);
            outbuf[dst_addr + 0] = ReadGsByte(addr + 0);
            outbuf[dst_addr + 1] = ReadGsByte(addr + 1);
            dst_addr += 2;
        }
    }
}

void GSHelper::StorePSMT8(unsigned char *outbuf, int dbp, int dbw, int dsax,
                          int dsay, int rrw, int rrh)
{
    int dst_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            outbuf[dst_addr++] = ReadGsByte(GetPixelAddressPSMT8(dbp, dbw, x, y));
        }
    }
}

void GSHelper::StorePSMT4(unsigned char *outbuf, int dbp, int dbw, int dsax,
                          int dsay, int rrw, int rrh)
{
    std::memset(outbuf, 0, (rrw * rrh + 1) >> 1);

    int dst_pixel = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            const int addr = GetPixelAddressPSMT4(dbp, dbw, x, y);
            const int src_nibble = (ReadGsByte(addr >> 1) >> ((addr & 1) << 2)) & 0x0f;
            const int dst_shift = (dst_pixel & 1) << 2;
            outbuf[dst_pixel >> 1] =
                (unsigned char)((src_nibble << dst_shift) |
                                (outbuf[dst_pixel >> 1] & (0xf0 >> dst_shift)));
            dst_pixel++;
        }
    }
}

static void ExpandPSMCT16Pixel(uint16_t p, unsigned char *out)
{
    const unsigned char r5 = (unsigned char)(p & 0x1f);
    const unsigned char g5 = (unsigned char)((p >> 5) & 0x1f);
    const unsigned char b5 = (unsigned char)((p >> 10) & 0x1f);

    out[0] = (unsigned char)((r5 << 3) | (r5 >> 2));
    out[1] = (unsigned char)((g5 << 3) | (g5 >> 2));
    out[2] = (unsigned char)((b5 << 3) | (b5 >> 2));
    out[3] = (p & 0x8000) ? 0x80 : 0x00;
}

void GSHelper::DownloadImagePSMT8(unsigned char *outbuf, int dbp, int dbw,
                                  int dsax, int dsay, int rrw, int rrh,
                                  int cbp, int cbw, int alpha_reg, int psm)
{
    /* Resolve the palette once, not once per texel.
     *
     * Everything between the index and the output bytes -- the CLUT index bit
     * shuffle, a full GetPixelAddressPSMCT32() swizzle, and four bounds-checked
     * byte reads -- depends only on the index, so it was the same work repeated
     * for every texel that shares a colour: 65536 times for a 256x256 texture
     * against 256 distinct answers.  Hoisting it leaves one index read and a
     * four-byte copy in the inner loop. */
    unsigned char palette[256][4];
    for (int i = 0; i < 256; i++)
    {
        int cy = (i & 0xe0) >> 4;
        int cx = i & 0x07;
        if (i & 0x08)
        {
            cy++;
        }
        if (i & 0x10)
        {
            cx += 8;
        }

        const int p = GetPixelAddressPSMCT32(cbp, cbw, cx, cy);
        palette[i][0] = ReadGsByte(p + 0);
        palette[i][1] = ReadGsByte(p + 1);
        palette[i][2] = ReadGsByte(p + 2);
        palette[i][3] =
            alpha_reg >= 0 ? (unsigned char)alpha_reg : ReadGsByte(p + 3);
    }

    int dst_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            // PSMT8H stores the 8-bit index in the top byte (bits 24..31) of the
            // PSMCT32 word at this texel; plain PSMT8 uses the packed T8 buffer.
            const unsigned char clut_index =
                (psm == PSMT8H)
                    ? ReadGsByte(GetPixelAddressPSMCT32(dbp, dbw, x, y) + 3)
                    : ReadGsByte(GetPixelAddressPSMT8(dbp, dbw, x, y));
            std::memcpy(&outbuf[dst_addr], palette[clut_index], 4);
            dst_addr += 4;
        }
    }
}

void GSHelper::DownloadImagePSMT4(unsigned char *outbuf, int dbp, int dbw,
                                  int dsax, int dsay, int rrw, int rrh,
                                  int cbp, int cbw, int csa, int alpha_reg,
                                  int psm)
{
    /* Sixteen distinct answers, so resolve them once rather than re-deriving
     * the CLUT address for every texel.  See DownloadImagePSMT8(). */
    unsigned char palette[16][4];
    for (int i = 0; i < 16; i++)
    {
        const int cy = ((i >> 3) & 1) + (csa & 0x0e);
        const int cx = (i & 0x07) + ((csa & 1) << 3);
        const int p = GetPixelAddressPSMCT32(cbp, cbw, cx, cy);
        palette[i][0] = ReadGsByte(p + 0);
        palette[i][1] = ReadGsByte(p + 1);
        palette[i][2] = ReadGsByte(p + 2);
        palette[i][3] =
            alpha_reg >= 0 ? (unsigned char)alpha_reg : ReadGsByte(p + 3);
    }

    int dst_addr = 0;
    for (int y = dsay; y < dsay + rrh; ++y)
    {
        for (int x = dsax; x < dsax + rrw; ++x)
        {
            int clut_index;
            if (psm == PSMT4HL || psm == PSMT4HH)
            {
                // "High" 4-bit formats: the index is not in a packed T4 buffer
                // but in the high nibbles of the PSMCT32 word at this texel —
                // PSMT4HL uses bits 24..27, PSMT4HH bits 28..31.  Address the
                // word with the 32-bit swizzle, then pick the nibble.
                const int p32 = GetPixelAddressPSMCT32(dbp, dbw, x, y);
                const int byte = ReadGsByte(p32 + 3);
                clut_index = (psm == PSMT4HL) ? (byte & 0x0f) : ((byte >> 4) & 0x0f);
            }
            else
            {
                const int addr = GetPixelAddressPSMT4(dbp, dbw, x, y);
                clut_index = (ReadGsByte(addr >> 1) >> ((addr & 1) << 2)) & 0x0f;
            }
            std::memcpy(&outbuf[dst_addr], palette[clut_index], 4);
            dst_addr += 4;
        }
    }
}

void GSHelper::Clear()
{
    std::fill(mem_.begin(), mem_.end(), 0);
}

namespace
{
/* The nibbles one pixel of `psm` at (x, y) occupies: the first one's address
 * (byte address * 2, plus one for a high nibble) through `first`, and how many
 * -- exactly as the Upload* functions above write them and the decoders read
 * them.  0 for a format neither handles. */
int PixelNibbles(int psm, int bp, int bw, int x, int y, int *first)
{
    switch ((PixelStorageFormat)psm)
    {
        case PSMCT32:
        case PSMZ32:
            *first = GetPixelAddressPSMCT32(bp, bw, x, y) * 2;
            return 8;
        case PSMCT24:
        case PSMZ24:
            *first = GetPixelAddressPSMCT32(bp, bw, x, y) * 2;
            return 6;
        case PSMCT16:
        case PSMCT16S:
        case PSMZ16:
        case PSMZ16S:
            *first = GetPixelAddressPSMCT16(bp, bw, x, y) * 2;
            return 4;
        case PSMT8:
            *first = GetPixelAddressPSMT8(bp, bw, x, y) * 2;
            return 2;
        case PSMT4:
            *first = GetPixelAddressPSMT4(bp, bw, x, y);
            return 1;
        case PSMT8H:
            *first = (GetPixelAddressPSMCT32(bp, bw, x, y) + 3) * 2;
            return 2;
        case PSMT4HL:
            *first = (GetPixelAddressPSMCT32(bp, bw, x, y) + 3) * 2;
            return 1;
        case PSMT4HH:
            *first = (GetPixelAddressPSMCT32(bp, bw, x, y) + 3) * 2 + 1;
            return 1;
        default:
            return 0;
    }
}

/* The pixel rectangle one 256-byte block holds in `psm` -- every pixel of an
 * aligned one lands in the same block -- and whether a whole one writes all
 * 512 of the block's nibbles.  The 24-bit and "H" formats leave some alone. */
bool BlockCell(int psm, int *w, int *h, bool *fills)
{
    switch ((PixelStorageFormat)psm)
    {
        case PSMCT32:
        case PSMZ32:
            *w = 8;
            *h = 8;
            *fills = true;
            return true;
        case PSMCT24:
        case PSMZ24:
        case PSMT8H:
        case PSMT4HL:
        case PSMT4HH:
            *w = 8;
            *h = 8;
            *fills = false;
            return true;
        case PSMCT16:
        case PSMCT16S:
        case PSMZ16:
        case PSMZ16S:
            *w = 16;
            *h = 8;
            *fills = true;
            return true;
        case PSMT8:
            *w = 16;
            *h = 16;
            *fills = true;
            return true;
        case PSMT4:
            *w = 32;
            *h = 16;
            *fills = true;
            return true;
        default:
            return false;
    }
}

constexpr int kGsBlockCount = kGsVramSize / 256;
}

UploadCoverage::UploadCoverage() : full_(kGsBlockCount / 64, 0)
{
}

void UploadCoverage::Clear()
{
    std::fill(full_.begin(), full_.end(), 0);
    partial_.clear();
}

/* Whole blocks by their bit; a block the rectangle only crosses nibble by
 * nibble.  Addresses past the end of GS memory are never written (the upload
 * drops them), so they are not marked. */
void UploadCoverage::Mark(int psm, int bp, int bw, int x0, int y0, int w, int h)
{
    int cw;
    int ch;
    bool fills;
    if (w <= 0 || h <= 0 || !BlockCell(psm, &cw, &ch, &fills))
    {
        return;
    }
    for (int cy = y0 - y0 % ch; cy < y0 + h; cy += ch)
    {
        for (int cx = x0 - x0 % cw; cx < x0 + w; cx += cw)
        {
            const int ix0 = std::max(cx, x0);
            const int ix1 = std::min(cx + cw, x0 + w);
            const int iy0 = std::max(cy, y0);
            const int iy1 = std::min(cy + ch, y0 + h);
            int first = 0;
            PixelNibbles(psm, bp, bw, ix0, iy0, &first);
            const int block = first >> 9;
            if (block < 0 || block >= kGsBlockCount)
            {
                continue;
            }
            if (fills && ix0 == cx && iy0 == cy && ix1 == cx + cw &&
                iy1 == cy + ch)
            {
                full_[(size_t)block >> 6] |= 1ull << (block & 63);
                continue;
            }
            std::array<uint64_t, 8> &nibbles = partial_[block];
            for (int y = iy0; y < iy1; y++)
            {
                for (int x = ix0; x < ix1; x++)
                {
                    const int count = PixelNibbles(psm, bp, bw, x, y, &first);
                    for (int n = 0; n < count; n++)
                    {
                        const int bit = (first + n) & 511;
                        nibbles[(size_t)bit >> 6] |= 1ull << (bit & 63);
                    }
                }
            }
        }
    }
}

/* Every nibble a read of this rectangle touches was marked.  A read past the
 * end of GS memory comes back 0 whatever was uploaded, so it depends on
 * nothing and passes. */
bool UploadCoverage::Marked(int psm, int bp, int bw, int x0, int y0, int w,
                            int h) const
{
    int cw;
    int ch;
    bool fills;
    if (w <= 0 || h <= 0)
    {
        return true;
    }
    if (!BlockCell(psm, &cw, &ch, &fills))
    {
        return false;
    }
    for (int cy = y0 - y0 % ch; cy < y0 + h; cy += ch)
    {
        for (int cx = x0 - x0 % cw; cx < x0 + w; cx += cw)
        {
            const int ix0 = std::max(cx, x0);
            const int ix1 = std::min(cx + cw, x0 + w);
            const int iy0 = std::max(cy, y0);
            const int iy1 = std::min(cy + ch, y0 + h);
            int first = 0;
            PixelNibbles(psm, bp, bw, ix0, iy0, &first);
            const int block = first >> 9;
            if (block < 0 || block >= kGsBlockCount ||
                (full_[(size_t)block >> 6] >> (block & 63) & 1) != 0)
            {
                continue;
            }
            const auto nibbles = partial_.find(block);
            if (nibbles == partial_.end())
            {
                return false;
            }
            for (int y = iy0; y < iy1; y++)
            {
                for (int x = ix0; x < ix1; x++)
                {
                    const int count = PixelNibbles(psm, bp, bw, x, y, &first);
                    for (int n = 0; n < count; n++)
                    {
                        const int bit = (first + n) & 511;
                        if ((nibbles->second[(size_t)bit >> 6] >> (bit & 63) &
                             1) == 0)
                        {
                            return false;
                        }
                    }
                }
            }
        }
    }
    return true;
}

/* MioPan_GsUpload()'s rectangle in its own format. */
void UploadCoverage::Add(const sceGsLoadImage &image)
{
    Mark((int)image.bitbltbuf.DPSM, (int)image.bitbltbuf.DBP,
         (int)image.bitbltbuf.DBW, (int)image.trxpos.DSAX,
         (int)image.trxpos.DSAY, (int)image.trxreg.RRW, (int)image.trxreg.RRH);
}

/* What MioPan_GsDownloadTexture() reads: every texel at TBP0/TBW, and for the
 * indexed formats the CLUT at CBP with the texture's buffer width -- all 256
 * entries for 8-bit, the CSA-selected 8x2 for 4-bit -- alpha included. */
bool UploadCoverage::Covers(const sceGsTex0 &tex0) const
{
    const int psm = (int)tex0.PSM;
    const int width = 1 << tex0.TW;
    const int height = 1 << tex0.TH;
    if (!Marked(psm, (int)tex0.TBP0, (int)tex0.TBW, 0, 0, width, height))
    {
        return false;
    }
    const int clut_width = tex0.TBW > 0 ? (int)tex0.TBW : 1;
    switch ((PixelStorageFormat)psm)
    {
        case PSMT8:
        case PSMT8H:
            return Marked(PSMCT32, (int)tex0.CBP, clut_width, 0, 0, 16, 16);
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
            return Marked(PSMCT32, (int)tex0.CBP, clut_width,
                          ((int)tex0.CSA & 1) << 3, (int)tex0.CSA & 0x0e, 8, 2);
        default:
            return true;
    }
}
}

extern "C" {

void MioPan_GsClear(void)
{
    MioPan::GS::gsHelper.Clear();
    g_first_upload_done = false;
    g_pending_uploads.clear();
    g_pending_uploads.push_back({0, MioPan::GS::kGsVramSize});
}

void MioPan_GsSetUploadSuppressed(int suppressed)
{
    g_upload_suppressed = suppressed != 0;
}

void MioPan_GsSetUploadObserver(void (*observer)(const sceGsLoadImage *image,
                                                 void *user),
                                void *user)
{
    g_upload_observer = observer;
    g_upload_observer_user = user;
}

void MioPan_GsUpload(sceGsLoadImage *image_load, unsigned char *image)
{
    if (image_load == nullptr || image == nullptr || g_upload_suppressed)
    {
        return;
    }

    MioPanProfileScope profile_scope(MIOPAN_PROFILE_GS_UPLOAD);

    g_first_upload_done = true;
    if (g_upload_observer != nullptr)
    {
        g_upload_observer(image_load, g_upload_observer_user);
    }

    int up_addr;
    int up_size;
    if (GetUploadRegion(image_load, &up_addr, &up_size))
    {
        g_pending_uploads.push_back({up_addr, up_size});
    }

    const uint64_t t0 = NowNs();
    const int upload_w = (int)image_load->trxreg.RRW;
    const int upload_h = (int)image_load->trxreg.RRH;

    switch ((MioPan::GS::PixelStorageFormat)image_load->bitbltbuf.DPSM)
    {
        case PSMZ32:
        case PSMCT32:
            MioPan::GS::gsHelper.UploadPSMCT32(
                image_load->bitbltbuf.DBP, image_load->bitbltbuf.DBW,
                image_load->trxpos.DSAX, image_load->trxpos.DSAY,
                upload_w, upload_h, image);
            break;
        case PSMZ24:
        case PSMCT24:
            MioPan::GS::gsHelper.UploadPSMCT24(
                image_load->bitbltbuf.DBP, image_load->bitbltbuf.DBW,
                image_load->trxpos.DSAX, image_load->trxpos.DSAY,
                upload_w, upload_h, image);
            break;
        case PSMZ16:
        case PSMZ16S:
        case PSMCT16:
        case PSMCT16S:
            MioPan::GS::gsHelper.UploadPSMCT16(
                image_load->bitbltbuf.DBP, image_load->bitbltbuf.DBW,
                image_load->trxpos.DSAX, image_load->trxpos.DSAY,
                upload_w, upload_h, image);
            break;
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
            MioPan::GS::gsHelper.UploadPSMT4(
                image_load->bitbltbuf.DBP, image_load->bitbltbuf.DBW,
                image_load->trxpos.DSAX, image_load->trxpos.DSAY,
                upload_w, upload_h, image, (int)image_load->bitbltbuf.DPSM);
            break;
        case PSMT8:
        case PSMT8H:
            MioPan::GS::gsHelper.UploadPSMT8(
                image_load->bitbltbuf.DBP, image_load->bitbltbuf.DBW,
                image_load->trxpos.DSAX, image_load->trxpos.DSAY,
                upload_w, upload_h, image, (int)image_load->bitbltbuf.DPSM);
            break;
        default:
            break;
    }

    g_gs_frame_metrics.upload_count++;
    g_gs_frame_metrics.upload_bytes +=
        TextureByteSize((int)image_load->bitbltbuf.DPSM, upload_w, upload_h);
    g_gs_frame_metrics.upload_ms += (double)(NowNs() - t0) / 1000000.0;
}

void MioPan_GsStore(sceGsStoreImage *sp, unsigned char *out)
{
    if (sp == nullptr || out == nullptr)
    {
        return;
    }

    const int sbp = (int)sp->bitbltbuf.SBP;
    const int sbw = (int)sp->bitbltbuf.SBW;
    const int spsm = (int)sp->bitbltbuf.SPSM;
    const int x = (int)sp->trxpos.SSAX;
    const int y = (int)sp->trxpos.SSAY;
    const int w = (int)sp->trxreg.RRW;
    const int h = (int)sp->trxreg.RRH;
    const int size = TextureByteSize(spsm, w, h);

    switch ((MioPan::GS::PixelStorageFormat)spsm)
    {
        case PSMZ32:
        case PSMCT32:
            MioPan::GS::gsHelper.DownloadPSMCT32(out, sbp, sbw, x, y, w, h);
            break;
        case PSMZ24:
        case PSMCT24:
            MioPan::GS::gsHelper.StorePSMCT24(out, sbp, sbw, x, y, w, h);
            break;
        case PSMZ16:
        case PSMZ16S:
        case PSMCT16:
        case PSMCT16S:
            MioPan::GS::gsHelper.StorePSMCT16(out, sbp, sbw, x, y, w, h);
            break;
        case PSMT8:
        case PSMT8H:
            MioPan::GS::gsHelper.StorePSMT8(out, sbp, sbw, x, y, w, h);
            break;
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
            MioPan::GS::gsHelper.StorePSMT4(out, sbp, sbw, x, y, w, h);
            break;
        default:
            if (size > 0)
            {
                std::memset(out, 0, (size_t)size);
            }
            break;
    }
}

/*
 * Push opaque colour outwards into fully transparent texels.
 *
 * The GS alpha test IS emulated now (SetTestRegister() forwards to
 * MioPan_RendererSetGsTestRegister), but it does not make this unnecessary,
 * and the reason is worth writing down because the old note here had it
 * wrong.  Measured over a full session -- 574,618 TEST writes, 7 distinct
 * values -- the strongest test this game ever asks for is GREATER against
 * AREF 0x00, i.e. "discard fully transparent" and nothing more.  So the
 * hardware kept every partly-transparent edge texel too.
 *
 * What throws the edge off is the bilinear filter, not the test.  At the PS2's
 * 640x448 a texel was a pixel; magnified to a modern window the filter reaches
 * across the cut-out boundary and pulls the RGB stored *behind* it into the
 * silhouette, and cut-out art routinely leaves the artist's backdrop colour
 * there -- black on an interior sheet, fog on an outdoor one, which is why
 * trees read as outlines against a night sky.
 *
 * Replacing an invisible texel's RGB with its visible neighbours' does not
 * change any fragment the GS would have drawn -- alpha is untouched, and at
 * alpha 0 the RGB is unobservable on its own.  It only decides what the filter
 * blends towards at the boundary, which becomes the leaf's own colour.  Two
 * rings is all a non-mipmapped bilinear tap can reach.
 */
static void BleedTransparentTexels(unsigned char *rgba, int width, int height)
{
    if (rgba == nullptr || width <= 1 || height <= 1)
    {
        return;
    }

    const size_t count = (size_t)width * (size_t)height;

    /* Most textures are fully opaque and must leave here having done nothing
     * but read alpha -- no allocation, no writes.  A fully transparent one has
     * nothing to bleed from.  Keep this ahead of everything below. */
    size_t transparent = 0;
    for (size_t i = 0; i < count; i++)
    {
        transparent += rgba[i * 4 + 3] == 0;
    }
    if (transparent == 0 || transparent == count)
    {
        return;
    }

    /*
     * Scratch reused across textures rather than allocated per call.  A room
     * switch decodes ~60 textures back to back, and at 256x256 the buffers this
     * replaces were 64 KiB apiece plus a full copy of one of them per pass.
     * thread_local because nothing here is shared and the download path is not
     * promised to stay single-threaded.  The lists only ever grow, so a large
     * texture sizes them once for the rest of the session.
     */
    static thread_local std::vector<unsigned char> known;
    static thread_local std::vector<size_t>        interior;
    static thread_local std::vector<size_t>        border;
    static thread_local std::vector<size_t>        newly_filled;

    try
    {
        known.assign(count, 0);
        if (interior.size() < transparent)
        {
            interior.resize(transparent);
        }
        if (border.size() < transparent)
        {
            border.resize(transparent);
        }
        if (newly_filled.size() < transparent)
        {
            newly_filled.resize(transparent);
        }
    }
    catch (const std::bad_alloc &)
    {
        /* Skipping the bleed only costs this one texture a filtered edge. */
        return;
    }

    /*
     * Hot loops address the scratch through raw pointers.  These are indexed
     * eight times per transparent texel per pass, and an unoptimised build does
     * not inline std::vector::operator[], so the checked accessor was itself a
     * measurable share of the work.
     */
    unsigned char *kn = known.data();
    size_t        *in = interior.data();
    size_t        *bo = border.data();
    size_t        *nf = newly_filled.data();
    size_t         n_in = 0;
    size_t         n_bo = 0;

    /*
     * `known` marks every texel whose RGB may be used as a source: opaque to
     * begin with, plus whatever earlier passes filled in.  Without it a second
     * pass would keep re-averaging the same first ring instead of stepping
     * outwards, because a bled texel still reads alpha 0.
     *
     * The transparent texels also go into work lists here, which is what turns
     * each pass below from a sweep of the whole image into a sweep of only the
     * texels that can still change -- and the list shrinks again once the first
     * pass has filled the outer ring.  Interior and border are kept apart
     * because the interior case, which is nearly all of them, can reach its
     * eight neighbours by fixed offsets with no per-tap bounds test.
     */
    for (int y = 0; y < height; y++)
    {
        const size_t row      = (size_t)y * (size_t)width;
        const bool   edge_row = (y == 0 || y + 1 == height);

        for (int x = 0; x < width; x++)
        {
            const size_t index = row + (size_t)x;

            if (rgba[index * 4 + 3] != 0)
            {
                kn[index] = 1;
            }
            else if (!edge_row && x > 0 && x + 1 < width)
            {
                in[n_in++] = index;
            }
            else
            {
                bo[n_bo++] = index;
            }
        }
    }

    const ptrdiff_t w         = (ptrdiff_t)width;
    const ptrdiff_t offset[8] = {-w - 1, -w, -w + 1, -1, 1, w - 1, w, w + 1};

    for (int pass = 0; pass < 2; pass++)
    {
        size_t n_nf = 0;

        for (size_t slot = 0; slot < n_in; slot++)
        {
            const size_t   index  = in[slot];
            unsigned char *texel  = rgba + index * 4;
            int            sum[3] = {0, 0, 0};
            int            taps   = 0;

            for (int k = 0; k < 8; k++)
            {
                const size_t si = (size_t)((ptrdiff_t)index + offset[k]);

                if (!kn[si])
                {
                    continue;
                }

                const unsigned char *n = rgba + si * 4;

                sum[0] += n[0];
                sum[1] += n[1];
                sum[2] += n[2];
                taps++;
            }

            if (taps == 0)
            {
                continue;
            }

            /* Alpha stays 0: this texel is still invisible, it just no
             * longer drags a foreign colour into the filter. */
            texel[0] = (unsigned char)(sum[0] / taps);
            texel[1] = (unsigned char)(sum[1] / taps);
            texel[2] = (unsigned char)(sum[2] / taps);
            nf[n_nf++] = index;
        }

        for (size_t slot = 0; slot < n_bo; slot++)
        {
            const size_t   index  = bo[slot];
            const int      x      = (int)(index % (size_t)width);
            const int      y      = (int)(index / (size_t)width);
            unsigned char *texel  = rgba + index * 4;
            int            sum[3] = {0, 0, 0};
            int            taps   = 0;

            for (int dy = -1; dy <= 1; dy++)
            {
                for (int dx = -1; dx <= 1; dx++)
                {
                    const int sx = x + dx;
                    const int sy = y + dy;

                    if ((dx == 0 && dy == 0) || sx < 0 || sy < 0 ||
                        sx >= width || sy >= height ||
                        !kn[(size_t)sy * width + sx])
                    {
                        continue;
                    }

                    const unsigned char *n =
                        rgba + ((size_t)sy * width + sx) * 4;

                    sum[0] += n[0];
                    sum[1] += n[1];
                    sum[2] += n[2];
                    taps++;
                }
            }

            if (taps == 0)
            {
                continue;
            }

            texel[0] = (unsigned char)(sum[0] / taps);
            texel[1] = (unsigned char)(sum[1] / taps);
            texel[2] = (unsigned char)(sum[2] / taps);
            nf[n_nf++] = index;
        }

        if (n_nf == 0)
        {
            break;
        }

        /*
         * `known` is deliberately published only here, between passes.  Holding
         * it frozen for the duration of a pass is what makes the bleed step one
         * ring outwards per pass instead of running away along a row, and it is
         * also why a pass needs no particular ordering: it writes only texels
         * that are unknown and reads only texels that are known.
         */
        for (size_t i = 0; i < n_nf; i++)
        {
            kn[nf[i]] = 1;
        }

        /* Retire the filled texels so the second pass visits only what is
         * still unresolved. */
        size_t keep = 0;
        for (size_t slot = 0; slot < n_in; slot++)
        {
            if (!kn[in[slot]])
            {
                in[keep++] = in[slot];
            }
        }
        n_in = keep;

        keep = 0;
        for (size_t slot = 0; slot < n_bo; slot++)
        {
            if (!kn[bo[slot]])
            {
                bo[keep++] = bo[slot];
            }
        }
        n_bo = keep;
    }
}

unsigned char *MioPan_GsDownloadTexture(sceGsTex0 *tex0, uint64_t *hash)
{
    if (hash != nullptr)
    {
        *hash = 0;
    }

    if (!g_first_upload_done || tex0 == nullptr)
    {
        return nullptr;
    }

    const int width = 1 << tex0->TW;
    const int height = 1 << tex0->TH;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096)
    {
        return nullptr;
    }

    const uint64_t t0 = NowNs();
    if (hash != nullptr)
    {
        *hash = MioPan_GetTextureHash(tex0);
    }

    g_texture_buffer.resize((size_t)width * (size_t)height * 4);

    switch ((MioPan::GS::PixelStorageFormat)tex0->PSM)
    {
        case PSMCT32:
            MioPan::GS::gsHelper.DownloadPSMCT32(
                g_texture_buffer.data(), tex0->TBP0, tex0->TBW, 0, 0,
                width, height);
            break;
        case PSMCT24:
        {
            int dst_addr = 0;
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    const int addr =
                        GetPixelAddressPSMCT32(tex0->TBP0, tex0->TBW, x, y);
                    g_texture_buffer[(size_t)dst_addr + 0] = ReadGsByte(addr + 0);
                    g_texture_buffer[(size_t)dst_addr + 1] = ReadGsByte(addr + 1);
                    g_texture_buffer[(size_t)dst_addr + 2] = ReadGsByte(addr + 2);
                    /*
                     * PSMCT24 has no stored alpha component.  TEXA supplies
                     * TA0 on the GS; use the normal opaque value here until
                     * the shared draw-environment register is host-tracked.
                     */
                    g_texture_buffer[(size_t)dst_addr + 3] = 0x80;
                    dst_addr += 4;
                }
            }
            break;
        }
        case PSMCT16:
        case PSMCT16S:
        {
            int dst_addr = 0;
            for (int y = 0; y < height; y++)
            {
                for (int x = 0; x < width; x++)
                {
                    const int addr = GetPixelAddressPSMCT16(tex0->TBP0, tex0->TBW, x, y);
                    const uint16_t p =
                        (uint16_t)ReadGsByte(addr + 0) |
                        ((uint16_t)ReadGsByte(addr + 1) << 8);
                    MioPan::GS::ExpandPSMCT16Pixel(p, &g_texture_buffer[(size_t)dst_addr]);
                    dst_addr += 4;
                }
            }
            break;
        }
        case PSMT4:
        case PSMT4HL:
        case PSMT4HH:
        {
            const int clut_width = GetTex0BufferWidth(tex0);
            MioPan::GS::gsHelper.DownloadImagePSMT4(
                g_texture_buffer.data(), tex0->TBP0, tex0->TBW, 0, 0,
                width, height, tex0->CBP, clut_width, tex0->CSA, -1,
                (int)tex0->PSM);
            break;
        }
        case PSMT8:
        case PSMT8H:
        {
            const int clut_width = GetTex0BufferWidth(tex0);
            MioPan::GS::gsHelper.DownloadImagePSMT8(
                g_texture_buffer.data(), tex0->TBP0, tex0->TBW, 0, 0,
                width, height, tex0->CBP, clut_width, -1, (int)tex0->PSM);
            break;
        }
        default:
            return nullptr;
    }

    for (size_t i = 3; i < g_texture_buffer.size(); i += 4)
    {
        /*
         * TEX0.TCC=RGB tells the GS to ignore texture alpha.  The host shader
         * always samples RGBA, so represent that mode with opaque sampled
         * alpha and leave the vertex/material alpha in control.
         */
        g_texture_buffer[i] =
            tex0->TCC == 0 ? 0xff : AdjustPS2Alpha(g_texture_buffer[i]);
    }

    BleedTransparentTexels(g_texture_buffer.data(), width, height);

    g_gs_frame_metrics.download_count++;
    g_gs_frame_metrics.download_bytes += width * height * 4;
    g_gs_frame_metrics.download_ms += (double)(NowNs() - t0) / 1000000.0;

    return g_texture_buffer.data();
}

uint64_t MioPan_GetTextureHash(sceGsTex0 *tex0)
{
    int addr;
    int size;
    int clut_addr;
    int clut_size;

    if (!g_first_upload_done || !GetRawTextureRegion(tex0, &addr, &size))
    {
        return 0;
    }

    uint64_t tex0_value = 0;
    std::memcpy(&tex0_value, tex0, sizeof(tex0_value));

    uint64_t hash = HashBytes(&MioPan::GS::gsHelper.mem_[(size_t)addr], size, 0);
    hash = HashBytes(&tex0_value, (int)sizeof(tex0_value), hash);

    if (GetClutRegion(tex0, &clut_addr, &clut_size))
    {
        hash = HashBytes(&MioPan::GS::gsHelper.mem_[(size_t)clut_addr], clut_size, hash);
    }

    return hash;
}

void MioPan_GsResetFrameMetrics(void)
{
    g_gs_last_frame = g_gs_frame_metrics;
    g_gs_frame_metrics = {0, 0, 0.0, 0, 0, 0.0};
}

int MioPan_GsGetUploadCount(void) { return (int)g_gs_last_frame.upload_count; }
int MioPan_GsGetUploadBytes(void) { return (int)g_gs_last_frame.upload_bytes; }
/* The frame currently being accumulated, not the rolled-over snapshot the
 * three above read.  MioPan_GsResetFrameMetrics() rolls and zeroes this at the
 * end of the frame, so a caller that runs before that -- the stutter note --
 * reads these directly and must not delta them against a previous value. */
uint64_t MioPan_GsGetUploadCountLive(void) { return g_gs_frame_metrics.upload_count; }
uint64_t MioPan_GsGetUploadBytesLive(void) { return g_gs_frame_metrics.upload_bytes; }
float MioPan_GsGetUploadMs(void) { return (float)g_gs_last_frame.upload_ms; }
int MioPan_GsGetDownloadCount(void) { return (int)g_gs_last_frame.download_count; }
int MioPan_GsGetDownloadBytes(void) { return (int)g_gs_last_frame.download_bytes; }
float MioPan_GsGetDownloadMs(void) { return (float)g_gs_last_frame.download_ms; }

int MioPan_GsHasPendingUploads(void)
{
    return g_pending_uploads.empty() ? 0 : 1;
}

void MioPan_GsConsumePendingUploads(void (*cb)(int addr, int size, void *ud), void *ud)
{
    /* Image packets commonly rewrite overlapping GS pages.  Passing every
     * packet through separately made the renderer rescan its complete TEX0
     * lookup table once per upload during a room's first draw.  Invalidation
     * depends only on the union of the written bytes, so sort and merge that
     * union before invoking the callback. */
    std::sort(g_pending_uploads.begin(), g_pending_uploads.end(),
              [](const GsUploadRegion &a, const GsUploadRegion &b) {
                  return a.addr < b.addr;
              });
    size_t merged_count = 0;
    for (const GsUploadRegion &region : g_pending_uploads)
    {
        if (region.size <= 0)
        {
            continue;
        }
        const int64_t start64 = std::max<int64_t>(0, region.addr);
        const int64_t end64 = std::min<int64_t>(
            MioPan::GS::kGsVramSize,
            (int64_t)region.addr + (int64_t)region.size);
        if (end64 <= start64)
        {
            continue;
        }

        const int start = (int)start64;
        const int end = (int)end64;
        if (merged_count != 0)
        {
            GsUploadRegion &previous = g_pending_uploads[merged_count - 1];
            const int previous_end = previous.addr + previous.size;
            if (start <= previous_end)
            {
                previous.size = std::max(previous_end, end) - previous.addr;
                continue;
            }
        }
        g_pending_uploads[merged_count++] = {start, end - start};
    }
    g_pending_uploads.resize(merged_count);

    if (cb != nullptr)
    {
        for (const auto &r : g_pending_uploads)
        {
            cb(r.addr, r.size, ud);
        }
    }
    g_pending_uploads.clear();
}

void MioPan_GetTextureGsRegion(sceGsTex0 *tex0, int *out_addr, int *out_size)
{
    if (out_addr == nullptr || out_size == nullptr)
    {
        return;
    }

    if (!GetTextureInvalidationRegion(tex0, out_addr, out_size))
    {
        *out_addr = 0;
        *out_size = 0;
    }
}
}

std::vector<unsigned char> MioPan::GS::TakeDownloadedTexture()
{
    std::vector<unsigned char> texture;
    texture.swap(g_texture_buffer);
    return texture;
}
