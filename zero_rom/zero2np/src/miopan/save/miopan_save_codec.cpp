#include "miopan_save_codec.h"
#include "common/variable.h"
#include "ingame/enemy/fene_entry.h"
#include "ingame/event/prg/ev_exe.h"
#include "ingame/plyr/sister.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace MioPanSave
{
namespace
{
constexpr std::array<size_t, 49> sizes = {
    12,  896, 592, 1472, 1,   8,   464, 160, 40,    1,  160, 1408, 1,    7736, 2000, 2400, 1088,
    484, 360, 16,  24,   32,  6,   4,   208, 32,    1,  240, 9,    1320, 12,   2112, 32,   32,
    4,   1,   4,   12,   312, 205, 8,   6,   10768, 16, 964, 32,   400,  32,   16};
size_t Size(unsigned block, bool native)
{
    if (!native)
        return sizes[block];
    switch (block)
    {
    case 1:
        return sizeof(PLYR_WRK);
    case 2:
        return sizeof(SIS_WRK);
    case 3:
        return sizeof(SIS_TRACE);
    case 5:
        return sizeof(SIS_MOTION);
    case 14:
        return 250 * 16;
    case 15:
        return EV_EXE_CTRL_MAX * sizeof(EV_EXE_CTRL);
    case 45:
        return 24;
    case 47:
        return sizeof(CFEneEntry);
    default:
        return sizes[block];
    }
}
uint64_t Get(const uint8_t *p, size_t n)
{
    uint64_t v = 0;
    for (size_t i = 0; i < n; ++i)
        v |= uint64_t(p[i]) << (i * 8);
    return v;
}
void Put(uint8_t *p, uint64_t v, size_t n)
{
    for (size_t i = 0; i < n; ++i)
        p[i] = uint8_t(v >> (i * 8));
}
int32_t I32(const uint8_t *p) { return int32_t(uint32_t(Get(p, 4))); }
bool Fail(std::string &error, const char *message)
{
    error = message;
    return false;
}
struct Span
{
    size_t ee, host, bytes;
};
void Copy(uint8_t *out, const uint8_t *in, bool native, std::initializer_list<Span> spans)
{
    for (const auto &s : spans)
        std::memcpy(out + (native ? s.host : s.ee), in + (native ? s.ee : s.host), s.bytes);
}
constexpr uint32_t motion[] = {0x34e088, 0x2f9cd0, 0x2f9ce0, 0x34e058, 0x34e060,
                               0x2f9cf0, 0x34e068, 0x34e070, 0x34e078, 0x34e080};
// EU retail static ghost-table addresses, paired with the port's stable set IDs.
struct Ghost
{
    uint32_t address, code;
};
constexpr Ghost ghosts[] = {
    {0x34b980, 1},   {0x33f970, 17},  {0x33f976, 18},  {0x33f97c, 19},  {0x33f982, 20},
    {0x33f988, 21},  {0x33f98e, 22},  {0x33f9c0, 65},  {0x33f9c6, 66},  {0x33f9cc, 67},
    {0x33f9d2, 68},  {0x33f9d8, 69},  {0x33fa28, 113}, {0x33fa2e, 114}, {0x33fa34, 115},
    {0x33fa3a, 116}, {0x33fa40, 117}, {0x33fab0, 129}, {0x33fab6, 130}, {0x33fabc, 131},
    {0x33fac2, 132}, {0x33fac8, 133}, {0x33f958, 193}, {0x33f95e, 194}, {0x33f964, 195},
    {0x33f96a, 196}, {0x33f9e0, 241}, {0x33f9e6, 242}, {0x33f9ec, 243}, {0x33f9f2, 244},
    {0x33fa78, 289}, {0x33fa7e, 290}, {0x33fa84, 291}, {0x33fa8a, 292}, {0x33fa90, 293},
    {0x33fa60, 465}, {0x33fa66, 466}, {0x33fa6c, 467}, {0x34b988, 545}, {0x33fa10, 625},
    {0x33fa16, 626}, {0x33fa1c, 627}, {0x33fa48, 641}, {0x33fa4e, 642}, {0x33fa54, 643},
    {0x33f998, 769}, {0x33f99e, 770}, {0x33f9a4, 771}, {0x33f9aa, 772}, {0x33f9b0, 773},
    {0x33f9b6, 774}, {0x33fad0, 833}, {0x33fad6, 834}, {0x33fadc, 835}, {0x33fae2, 836},
    {0x33fae8, 837}, {0x34b990, 897}, {0x33f9f8, 945}, {0x33f9fe, 946}, {0x33fa04, 947},
    {0x33fa98, 993}, {0x33fa9e, 994}, {0x33faa4, 995},
};
uint32_t GhostPointer(uint64_t value, bool native)
{
    if (!value)
        return 0;
    for (const auto &g : ghosts)
        if (value == (native ? g.address : g.code))
            return native ? g.code : g.address;
    return 0xffffffffu;
}
void Record(unsigned block, uint8_t *out, const uint8_t *in, bool native)
{
    switch (block)
    {
    case 1:
        Copy(out, in, native, {{0, 0, 0x344}, {0x350, offsetof(PLYR_WRK, fl_pow), 0x30}});
        // Sound references are rebound by PlayerMain/SisterMain, never imported as pointers.
        if (!native)
            Put(out + 0x344, 0x2e8c70, 4);
        break;
    case 2:
        Copy(out, in, native,
             {{0, 0, 0x110},
              {0x110, offsetof(SIS_WRK, alg), 0xa8},
              {0x1bc, offsetof(SIS_WRK, alg) + offsetof(SISALG_WRK, flag), 1},
              {0x1c0, offsetof(SIS_WRK, on), 0x30},
              {0x1f0, offsetof(SIS_WRK, wpos), 0x30},
              {0x230, offsetof(SIS_WRK, bwp), 0x18}});
        // Only inactive scripts are convertible: their stale interpreter pointers are unused.
        std::memset(out + (native ? offsetof(SIS_WRK, alg) : 0x110) + 0x10, 0, 0x98);
        out[(native ? offsetof(SIS_WRK, alg) : 0x110) + 2] = 0;
        if (!native)
            Put(out + 0x220, 0x2f9f90, 4);
        break;
    case 3:
        Copy(out, in, native,
             {{0, 0, 0x24},
              {0x30, offsetof(SIS_TRACE, vwpos), 0x48},
              {0x80, offsetof(SIS_TRACE, p), 0x540}});
        break;
    case 5:
    {
        Copy(out, in, native, {{0, 0, 3}});
        uint64_t pointer =
            Get(in + (native ? 4 : offsetof(SIS_MOTION, sat)), native ? 4 : sizeof(void *));
        uint64_t value = 0;
        if (native)
        {
            for (unsigned i = 0; i < 10; ++i)
                if (motion[i] == pointer)
                    value = i + 1;
        }
        else if (pointer >= 1 && pointer <= 10)
            value = motion[pointer - 1];
        Put(out + (native ? offsetof(SIS_MOTION, sat) : 4), value, native ? sizeof(void *) : 4);
        break;
    }
    case 14:
        for (unsigned i = 0; i < 250; ++i)
        {
            auto *dst = out + i * (native ? 16 : 8);
            const auto *p = in + i * (native ? 8 : 16);
            std::memcpy(dst, p, 4);
            Put(dst + (native ? 8 : 4), Get(p + (native ? 4 : 8), native ? 4 : 8), native ? 8 : 4);
        }
        break;
    case 15:
        for (unsigned i = 0; i < 150; ++i)
        {
            auto *dst = out + i * (native ? sizeof(EV_EXE_CTRL) : 16);
            const auto *p = in + i * (native ? 16 : sizeof(EV_EXE_CTRL));
            Copy(dst, p, native, {{0, 0, 8}, {12, offsetof(EV_EXE_CTRL, if_state), 2}});
            Put(dst + 8, Get(p + 8, native ? 4 : 8), native ? 8 : 4);
        }
        break;
    case 45:
        Copy(out, in, native, {{0, 0, 8}, {16, 8, 16}});
        break;
    case 47:
        Copy(out, in, native, {{0, 0, 0x11}, {24, 32, 8}});
        Put(out + (native ? 24 : 20),
            GhostPointer(Get(in + (native ? 20 : 24), native ? 4 : 8), native), native ? 8 : 4);
        break;
    }
}
bool Convert(const Bytes &in, Bytes &out, bool native, std::string &error)
{
    if (!Validate(in, error))
        return false;
    bool source_native = in.size() == NativeSize();
    if (source_native == native)
    {
        out = in;
        return true;
    }
    const auto *sis = in.data() + BlockOffset(2, source_native);
    const auto *alg = sis + (source_native ? offsetof(SIS_WRK, alg) : 0x110);
    unsigned mode = in[BlockOffset(4, source_native)];
    if ((mode == 2 || mode == 7) && alg[2])
        return Fail(error, "Mayu is running a scripted action. Save again after it finishes.");
    Bytes result(native ? NativeSize() : Ps2Size, 0);
    for (unsigned b = 0; b < sizes.size(); ++b)
    {
        const auto *p = in.data() + BlockOffset(b, source_native);
        auto *dst = result.data() + BlockOffset(b, native);
        if (Size(b, false) == Size(b, true))
            std::memcpy(dst, p, Size(b, false));
        else
            Record(b, dst, p, native);
    }
    Seal(result);
    out = std::move(result);
    error.clear();
    return true;
}
} // namespace
size_t NativeSize()
{
    size_t size = 4;
    for (unsigned b = 0; b < sizes.size(); ++b)
        size += Size(b, true);
    return size;
}
size_t BlockOffset(unsigned block, bool native)
{
    size_t offset = 0;
    for (unsigned b = 0; b < block; ++b)
        offset += Size(b, native);
    return offset;
}
bool Empty(const Bytes &d)
{
    return d.size() >= 4 && Get(d.data() + d.size() - 4, 4) == 0xffffffffu &&
           std::all_of(d.begin(), d.end() - 4, [](uint8_t b) { return b == 0; });
}
bool Checksum(const Bytes &d)
{
    if (d.size() < 4)
        return false;
    uint32_t sum = 0;
    for (size_t i = 0; i < d.size() - 4; ++i)
        sum += int8_t(d[i]);
    return sum == Get(d.data() + d.size() - 4, 4);
}
void Seal(Bytes &d)
{
    uint32_t sum = 0;
    for (size_t i = 0; i < d.size() - 4; ++i)
        sum += int8_t(d[i]);
    Put(d.data() + d.size() - 4, sum, 4);
}
Bytes EmptySlot(size_t size)
{
    Bytes d(size, 0);
    Put(d.data() + size - 4, 0xffffffffu, 4);
    return d;
}
bool Validate(const Bytes &d, std::string &error)
{
    if (sizeof(void *) != 8)
        return Fail(error, "Save conversion requires a 64-bit build.");
    if (d.size() != Ps2Size && d.size() != NativeSize())
        return Fail(error, "Unknown EU gameplay save size/version.");
    if (!Checksum(d) || Empty(d))
        return Fail(error, "Invalid checksum or empty gameplay slot.");
    bool native = d.size() == NativeSize();
    if (d[0] >= 11 || d[1] > 3 || d[2] > 99 || d[3] > 1)
        return Fail(error, "Invalid saved chapter, difficulty or clear flags.");
    for (unsigned block : {1u, 2u})
    {
        const auto *p = d.data() + BlockOffset(block, native);
        for (unsigned i = 0; i < 4; ++i)
        {
            float f;
            std::memcpy(&f, p + 16 + i * 4, 4);
            if (!std::isfinite(f))
                return Fail(error, "Invalid player transform.");
        }
    }
    const auto *motion_data = d.data() + BlockOffset(5, native);
    auto ptr = Get(motion_data + (native ? offsetof(SIS_MOTION, sat) : 4), native ? 8 : 4);
    if (ptr && ((native && ptr > 10) || (!native && std::find(std::begin(motion), std::end(motion),
                                                              ptr) == std::end(motion))))
        return Fail(error, "Unknown EU Mayu animation table.");
    const auto *ghost = d.data() + BlockOffset(47, native);
    ptr = Get(ghost + (native ? 24 : 20), native ? 8 : 4);
    if (GhostPointer(ptr, !native) == 0xffffffffu)
        return Fail(error, "Unknown EU ghost state table.");
    for (unsigned block : {14u, 15u})
    {
        const auto *p = d.data() + BlockOffset(block, native);
        unsigned count = block == 14 ? 250 : 150;
        size_t stride = Size(block, native) / count;
        for (unsigned i = 0; i < count; ++i, p += stride)
        {
            int event = block == 14 ? int16_t(Get(p + 2, 2)) : I32(p);
            if (event < -1 || event >= 1931)
                return Fail(error, "Invalid saved event ID.");
            if (event < 0)
                continue;
            size_t at = block == 14 ? (native ? 8 : 4) : 8;
            auto cursor = Get(p + at, native ? 8 : 4);
            if (cursor < 0xd4ec00 || cursor >= 0xd9ec00)
                return Fail(error, "Saved event cursor is outside the EU event buffer.");
        }
    }
    error.clear();
    return true;
}
bool ToNative(const Bytes &ps2, Bytes &native, std::string &error)
{
    return Convert(ps2, native, true, error);
}
bool ToPs2(const Bytes &native, Bytes &ps2, std::string &error)
{
    return Convert(native, ps2, false, error);
}
} // namespace MioPanSave
