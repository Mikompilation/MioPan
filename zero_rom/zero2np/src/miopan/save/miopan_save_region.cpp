#include "miopan_save_region.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iterator>
#include <map>
#include <stdexcept>
#include <tuple>
namespace MioPanSave
{
namespace
{
struct Pointer { uint32_t address[3]; };
struct Resource { uint16_t id[3]; };
struct State { uint8_t bytes[12]; };
struct Object { uint32_t label; uint8_t state; };
struct StopDuration { uint32_t offset, frames[3]; };
struct CursorSpan { uint16_t event; uint8_t kind; uint32_t source, target, size; };
#include "miopan_save_region_data.inc"
constexpr uint32_t EventBase = 0xd4ec00;
using Blocks = std::array<Bytes, 49>;
unsigned Index(SaveRegion region)
{
    const auto n = unsigned(region);
    if (n > 2) throw std::runtime_error("Unknown save region.");
    return n;
}
uint32_t Get(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void Put(uint8_t *p, uint32_t n)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = uint8_t(n >> (i * 8));
}
Blocks Split(const Bytes &data, SaveRegion region)
{
    Blocks blocks;
    size_t at = 0, index = 0;
    for (unsigned i = 0; i < blocks.size(); ++i)
    {
        if (region == SaveRegion::JP && i == 12) { blocks[i] = Bytes(1); continue; }
        const size_t size = region == SaveRegion::JP ? jpSizes[index++] : euSizes[i] + (i == 11 && region == SaveRegion::US ? 8 : 0);
        blocks[i] = Bytes(data.begin() + at, data.begin() + at + size);
        at += size;
    }
    if (at + 4 != data.size()) throw std::runtime_error("Invalid regional save layout.");
    return blocks;
}
Bytes Join(const Blocks &blocks, SaveRegion region)
{
    Bytes result;
    for (unsigned i = 0; i < blocks.size(); ++i)
        if (region != SaveRegion::JP || i != 12) result.insert(result.end(), blocks[i].begin(), blocks[i].end());
    result.resize(result.size() + 4);
    Seal(result);
    return result;
}
template<size_t N>
void TranslatePointer(uint8_t *p, const Pointer (&table)[N], unsigned source, unsigned target)
{
    const auto value = Get(p);
    if (!value) return;
    for (const auto &row : table)
        if (row.address[source] == value) { Put(p, row.address[target]); return; }
    throw std::runtime_error("Unknown regional animation or ghost pointer.");
}
uint32_t FileId(uint32_t id, unsigned source, unsigned target)
{
    if (id == 0xffffffffu) return id;
    for (const auto &row : resources) if (row.id[source] == id) return row.id[target];
    throw std::runtime_error("A saved audio resource has no verified regional equivalent.");
}
template<size_t N>
uint32_t Cursor(uint32_t pointer, int event, unsigned kind, const CursorSpan (&table)[N], bool reverse)
{
    if (event < 0 || event >= 1931 || pointer < EventBase) throw std::runtime_error("Invalid saved event cursor.");
    const uint32_t offset = pointer - EventBase;
    uint32_t result = 0;
    const auto first = std::lower_bound(std::begin(table), std::end(table), std::pair<int, unsigned>{event, kind},
        [](const CursorSpan &row, const auto &key) { return std::pair<int, unsigned>{row.event, row.kind} < key; });
    for (auto it = first; it != std::end(table) && it->event == event && it->kind == kind; ++it)
    {
        const auto start = reverse ? it->target : it->source;
        if (offset < start || offset - start >= it->size) continue;
        const auto candidate = (reverse ? it->source : it->target) + offset - start;
        if (kind)
        {
            const bool jp = &table[0] == &jpCursors[0] && reverse;
            const bool boundary = jp ? std::binary_search(std::begin(jpBoundaries), std::end(jpBoundaries), candidate)
                                     : std::binary_search(std::begin(euBoundaries), std::end(euBoundaries), candidate);
            if (!boundary) continue;
        }
        if (result && result != candidate + EventBase) throw std::runtime_error("Ambiguous regional event cursor.");
        result = candidate + EventBase;
    }
    if (!result) throw std::runtime_error("This active script position cannot be converted safely. Save after the scene finishes.");
    return result;
}
uint32_t MapCursor(uint32_t p, int event, unsigned kind, SaveRegion source, SaveRegion target)
{
    if (source == target) return p;
    if ((source == SaveRegion::JP || target == SaveRegion::JP) && kind && (event == 12 || event == 1685))
        throw std::runtime_error("Save after the regional chapter/cutscene script finishes before converting.");
    if (source != SaveRegion::EU)
        p = source == SaveRegion::JP ? Cursor(p, event, kind, jpCursors, false) : Cursor(p, event, kind, usCursors, false);
    if (target != SaveRegion::EU)
        p = target == SaveRegion::JP ? Cursor(p, event, kind, jpCursors, true) : Cursor(p, event, kind, usCursors, true);
    return p;
}
uint32_t ObjectId(uint32_t id, SaveRegion source, SaveRegion target)
{
    if (source == target || (source != SaveRegion::JP && target != SaveRegion::JP)) return id;
    if (source == SaveRegion::JP)
    {
        if (id == 27026) return 71003;
        if (id == 71049) return 71050;
        if (id == 71050) return 71051;
    }
    else
    {
        if (id == 71003) return 27026;
        if (id == 71050) return 71049;
        if (id == 71051) return 71050;
        if (id == 27026) throw std::runtime_error("The unused regional object has an unexpected saved state.");
    }
    if (id / 1000 == 71) throw std::runtime_error("An active target uses a region-renumbered scene object. Save after the scene finishes.");
    return id;
}
void Furniture(Blocks &blocks, SaveRegion source, SaveRegion target)
{
    const Object *begin = source == SaveRegion::JP ? std::begin(jpObjects) : std::begin(euObjects);
    const Object *end = source == SaveRegion::JP ? std::end(jpObjects) : std::end(euObjects);
    const auto &old = blocks[42];
    if (old.size() != size_t(end - begin) * 16) throw std::runtime_error("Invalid saved furniture table.");
    std::map<uint32_t, std::array<uint8_t, 12>> states;
    for (auto it = begin; it != end; ++it)
    {
        const auto *p = old.data() + (it - begin) * 16;
        if (Get(p) != it->label) throw std::runtime_error("Unknown regional furniture identities.");
        if (target == SaveRegion::JP && source != SaveRegion::JP && it->label == 27026)
        {
            std::array<uint8_t, 12> inactive;
            std::copy_n(p + 4, 12, inactive.begin());
            // The runtime uses -1, rather than the disc's zero, for no animation.
            if (inactive[4] == 255 && inactive[5] == 255) inactive[4] = inactive[5] = 0;
            if (std::memcmp(inactive.data(), objectStates[it->state].bytes, 12)) throw std::runtime_error("The unused regional object has an unexpected saved state.");
            continue;
        }
        auto &state = states[ObjectId(it->label, source, target)];
        std::copy_n(p + 4, 12, state.begin());
    }
    begin = target == SaveRegion::JP ? std::begin(jpObjects) : std::begin(euObjects);
    end = target == SaveRegion::JP ? std::end(jpObjects) : std::end(euObjects);
    Bytes next(size_t(end - begin) * 16);
    for (auto it = begin; it != end; ++it)
    {
        auto *p = next.data() + (it - begin) * 16;
        Put(p, it->label);
        const auto found = states.find(it->label);
        std::copy_n(found == states.end() ? objectStates[it->state].bytes : found->second.data(), 12, p + 4);
        // EU chapter-eight initialization hides these non-interactive decorations.
        if (source == SaveRegion::JP && target != SaveRegion::JP && blocks[0][0] >= 8 && (it->label == 13015 || it->label == 13020)) p[6] = 0;
    }
    blocks[42] = std::move(next);
}
void SoulList(Blocks &blocks, SaveRegion source, SaveRegion target, bool copy_extra)
{
    if ((source == SaveRegion::EU) == (target == SaveRegion::EU)) return;
    const auto old = blocks[11];
    Bytes next(target == SaveRegion::EU ? 1408 : 1416);
    for (unsigned i = 0; i < 176; ++i)
        std::copy_n(old.data() + (source == SaveRegion::EU || i < 165 ? i : i + 1) * 8, 8,
                    next.data() + (target == SaveRegion::EU || i < 165 ? i : i + 1) * 8);
    if (target == SaveRegion::EU)
    {
        next[13 * 8] = std::max(next[13 * 8], old[165 * 8]);
        Put(next.data() + 13 * 8 + 4, std::max(Get(next.data() + 13 * 8 + 4), Get(old.data() + 165 * 8 + 4)));
    }
    else if (copy_extra) std::copy_n(old.data() + 13 * 8, 8, next.data() + 165 * 8);
    blocks[11] = std::move(next);
}
void Adapt(Blocks &blocks, SaveRegion source, SaveRegion target, bool copy_extra)
{
    const auto s = Index(source), t = Index(target);
    if (source == SaveRegion::JP || target == SaveRegion::JP)
    {
        // These two streamed areas have different floors and cameras in Japan.
        for (unsigned b : {1u, 2u})
            if ((b == 1 || blocks[2][0x1c0]) && (blocks[b][0xe0] == 6 || blocks[b][0xe0] == 62))
                throw std::runtime_error("This area's floor/camera layout differs in Japan. Save in another area before converting.");
    }
    TranslatePointer(blocks[5].data() + 4, motionPointers, s, t);
    TranslatePointer(blocks[47].data() + 20, ghostPointers, s, t);
    Put(blocks[1].data() + 0x344, soundPositions[0].address[t]);
    Put(blocks[2].data() + 0x220, soundPositions[1].address[t]);
    for (unsigned i = 0; i < 250; ++i)
    {
        auto *p = blocks[14].data() + i * 8;
        const int event = int16_t(uint16_t(p[2]) | uint16_t(p[3]) << 8);
        if (event >= 0) Put(p + 4, MapCursor(Get(p + 4), event, 0, source, target));
    }
    for (unsigned i = 0; i < 150; ++i)
    {
        auto *p = blocks[15].data() + i * 16;
        const int event = int32_t(Get(p));
        if (event >= 0)
        {
            const auto canonical = MapCursor(Get(p + 8), event, 1, source, SaveRegion::EU);
            const auto converted = MapCursor(Get(p + 8), event, 1, source, target);
            for (const auto &wait : stopDurations)
                if (canonical == wait.offset + EventBase && Get(p + 4))
                {
                    const auto remaining = Get(p + 4);
                    if (!wait.frames[s] || remaining > wait.frames[s]) throw std::runtime_error("Invalid active event countdown.");
                    Put(p + 4, uint32_t((uint64_t(remaining) * wait.frames[t] + wait.frames[s] / 2) / wait.frames[s]));
                }
            Put(p + 8, converted);
        }
    }
    for (const auto &field : {std::pair<unsigned, unsigned>{19, 8}, {20, 16}, {21, 8}})
        if (blocks[field.first][0]) Put(blocks[field.first].data() + field.second, FileId(Get(blocks[field.first].data() + field.second), s, t));
    for (unsigned at = 0; at < blocks[29].size(); at += 4) Put(blocks[29].data() + at, FileId(Get(blocks[29].data() + at), s, t));
    for (unsigned at = 0; at < 960; at += 4) Put(blocks[44].data() + at, FileId(Get(blocks[44].data() + at), s, t));
    // GetObjectPos types 8/9 address map placement labels rather than actors.
    if (blocks[20][0] && (Get(blocks[20].data() + 4) == 8 || Get(blocks[20].data() + 4) == 9))
        Put(blocks[20].data() + 8, ObjectId(Get(blocks[20].data() + 8), source, target));
    if ((blocks[45][0] & 2) && (blocks[45][1] == 8 || blocks[45][1] == 9))
        Put(blocks[45].data() + 4, ObjectId(Get(blocks[45].data() + 4), source, target));
    SoulList(blocks, source, target, copy_extra);
    Furniture(blocks, source, target);
}
void Timing(Bytes &native, unsigned source, unsigned target)
{
    if ((source != 25 && source != 30) || (target != 25 && target != 30)) throw std::runtime_error("Choose 50 Hz PAL or 60 Hz timing.");
    if (source == target) return;
    for (unsigned i = 0; i < 25; ++i)
    {
        auto *p = native.data() + BlockOffset(46, true) + i * 16 + 4;
        const auto frames = Get(p);
        if (frames > 359999u * source) throw std::runtime_error("Invalid mission-record time.");
        Put(p, uint32_t((uint64_t(frames) * target + source / 2) / source));
    }
    auto &fraction = native[BlockOffset(26, true)];
    if (fraction >= source) throw std::runtime_error("The saved play clock does not match the selected timing. Try the other source timing.");
    fraction = uint8_t((unsigned(fraction) * target + source / 2) / source);
    Seal(native);
}
} // namespace
const char *RegionFolder(SaveRegion region)
{
    static const char *names[] = {"BESLES-52384ZERO-BD", "BASLUS-20766ZERO-BD", "BISLPS-25303ZERO-BD"};
    return names[Index(region)];
}
const char *RegionLabel(SaveRegion region)
{
    static const char *names[] = {"Europe", "USA", "Japan"};
    return names[Index(region)];
}
size_t RegionalSize(SaveRegion region)
{
    Index(region);
    return region == SaveRegion::JP ? 36138 : region == SaveRegion::US ? 36155 : Ps2Size;
}
bool ImportRegion(const Bytes &data, SaveRegion region, SaveVersion version, Bytes &native, std::string &error, unsigned eu_hz)
{
    try
    {
        Index(region);
        if (data.size() == NativeSize() && region == SaveRegion::EU) return ToNative(data, native, error, version);
        if (data.size() != RegionalSize(region) || !Checksum(data) || Empty(data)) throw std::runtime_error("Invalid regional slot size or checksum.");
        Bytes canonical = data;
        if (region != SaveRegion::EU)
        {
            auto blocks = Split(data, region);
            Adapt(blocks, region, SaveRegion::EU, false);
            canonical = Join(blocks, SaveRegion::EU);
            version = SaveVersion::Retail;
        }
        Bytes next;
        if (!ToNative(canonical, next, error, version)) return false;
        Timing(next, region == SaveRegion::EU ? eu_hz : 30, 30);
        native = std::move(next);
        error.clear();
        return true;
    }
    catch (const std::exception &e) { error = e.what(); return false; }
}
bool ExportRegion(const Bytes &native, SaveRegion region, SaveVersion version, bool copy_extra, Bytes &data, std::string &error, unsigned eu_hz)
{
    try
    {
        Index(region);
        Bytes next = native;
        if (next.size() != NativeSize() || !Validate(next, error)) return false;
        Timing(next, 30, region == SaveRegion::EU ? eu_hz : 30);
        if (!ToPs2(next, next, error, region == SaveRegion::EU ? version : SaveVersion::Retail)) return false;
        if (region != SaveRegion::EU)
        {
            auto blocks = Split(next, SaveRegion::EU);
            Adapt(blocks, SaveRegion::EU, region, copy_extra);
            next = Join(blocks, region);
        }
        data = std::move(next);
        error.clear();
        return true;
    }
    catch (const std::exception &e) { error = e.what(); return false; }
}
std::string RegionNotice(SaveRegion source, SaveRegion target)
{
    if (source == target) return {};
    std::string note;
    if (target == SaveRegion::EU) note = "Twin Doll's two regional photo records are merged, keeping the highest score and obtained/read status.";
    if (source == SaveRegion::JP || target == SaveRegion::JP) note += " Regional scenery may retain its saved visibility; saves in changed floor areas or unsupported active scripts are blocked.";
    return note;
}
} // namespace MioPanSave
