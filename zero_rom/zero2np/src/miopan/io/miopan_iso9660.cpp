/* ==========================================================================
 *  miopan_iso9660.cpp  --  files out of a disc image
 *
 *  See miopan_iso9660.h for what it reads and what it does not.  The layout is
 *  ECMA-119's: volume descriptors from sector 16, the primary one (type 1)
 *  holding the root's directory record at +156, and every multi-byte number
 *  stored both ways round, of which this reads the little-endian half.
 * ======================================================================== */

#include "miopan_iso9660.h"

#include "miopan_file.h"

#include <cctype>
#include <cstring>

namespace MioPanIso9660
{

namespace
{

constexpr uint64_t kDescriptorSector = 16;
constexpr int kDescriptorMax = 64;      /* a real disc has two or three */
constexpr uint32_t kDirectoryMax = 16u << 20;

uint32_t Le32(const uint8_t *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
           (uint32_t)p[3] << 24;
}

uint16_t Le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | p[1] << 8);
}

std::string Lower(std::string s)
{
    for (char &c : s)
    {
        c = (char)std::tolower((unsigned char)c);
    }
    return s;
}

/* "IMG_BD.BIN;1" -> "IMG_BD.BIN", and "NOEXT.;1" -> "NOEXT". */
std::string BareName(const uint8_t *id, size_t length)
{
    std::string name((const char *)id, length);
    const size_t semi = name.find(';');
    if (semi != std::string::npos)
    {
        name.resize(semi);
    }
    if (!name.empty() && name.back() == '.')
    {
        name.pop_back();
    }
    return name;
}

} // namespace

Image::~Image()
{
    if (file_ != nullptr)
    {
        MioPan_FileCloseHandle(file_);
    }
}

OpenResult Image::Open(const std::string &path)
{
    path_ = path;
    file_ = MioPan_FileOpen(path.c_str(), "rb");
    if (file_ == nullptr)
    {
        return OpenResult::Unreadable;
    }
    const int64_t size = MioPan_FileStreamSize(file_);
    if (size < 0)
    {
        return OpenResult::Unreadable;
    }
    size_ = (uint64_t)size;
    if (size_ < (kDescriptorSector + 1) * 2048)
    {
        return OpenResult::NotAnImage;
    }

    /* The descriptors are 2048 bytes apart whatever the volume's block size,
     * which the primary one goes on to name. */
    uint8_t d[2048];
    for (int k = 0; k < kDescriptorMax; k++)
    {
        if (!Read((kDescriptorSector + (uint64_t)k) * 2048, d, sizeof(d)))
        {
            return k == 0 ? OpenResult::Unreadable : OpenResult::NotAnImage;
        }
        if (std::memcmp(d + 1, "CD001", 5) != 0 || d[0] == 255)
        {
            return OpenResult::NotAnImage;
        }
        if (d[0] != 1)
        {
            continue;
        }
        block_ = Le16(d + 128);
        const uint8_t *root = d + 156;
        if (block_ < 512 || block_ > 2048 || (block_ & (block_ - 1)) != 0 || root[0] < 34)
        {
            return OpenResult::NotAnImage;
        }
        root_.name.clear();
        root_.offset = (uint64_t)Le32(root + 2) * block_;
        root_.size = Le32(root + 10);
        root_.directory = true;
        return OpenResult::Ok;
    }
    return OpenResult::NotAnImage;
}

bool Image::Read(uint64_t offset, void *buf, size_t size)
{
    return file_ != nullptr && offset <= size_ && size <= size_ - offset &&
           MioPan_FileSeek(file_, (int64_t)offset, MIOPAN_SEEK_SET) == 0 &&
           MioPan_FileRead(file_, buf, size) == size;
}

std::vector<Entry> Image::List(const Entry &dir)
{
    std::vector<Entry> out;
    if (!dir.directory || dir.size == 0 || dir.size > kDirectoryMax)
    {
        return out;
    }
    std::vector<uint8_t> data((size_t)dir.size);
    if (!Read(dir.offset, data.data(), data.size()))
    {
        return out;
    }

    /* A record never straddles a sector: a zero length is the padding to the
     * next one. */
    size_t at = 0;
    while (at < data.size())
    {
        const size_t length = data[at];
        if (length == 0)
        {
            at = (at / 2048 + 1) * 2048;
            continue;
        }
        if (length < 34 || at + length > data.size())
        {
            break;
        }
        const uint8_t *r = data.data() + at;
        const size_t id_length = r[32];
        at += length;
        if (33 + id_length > length)
        {
            break;
        }
        /* 0 and 1 are "." and "..". */
        if (id_length == 1 && (r[33] == 0 || r[33] == 1))
        {
            continue;
        }
        Entry e;
        e.name = BareName(r + 33, id_length);
        e.offset = (uint64_t)Le32(r + 2) * block_;
        e.size = Le32(r + 10);
        e.directory = (r[25] & 0x02) != 0;
        out.push_back(std::move(e));
    }
    return out;
}

bool Image::Find(const std::string &path, Entry &out)
{
    Entry at = root_;
    size_t from = 0;
    while (from < path.size())
    {
        size_t to = path.find_first_of("/\\", from);
        if (to == std::string::npos)
        {
            to = path.size();
        }
        if (to == from)
        {
            from++;
            continue;
        }
        const std::string want =
            Lower(BareName((const uint8_t *)path.data() + from, to - from));
        bool found = false;
        for (Entry &e : List(at))
        {
            if (Lower(e.name) == want)
            {
                at = std::move(e);
                found = true;
                break;
            }
        }
        if (!found)
        {
            return false;
        }
        from = to + 1;
    }
    out = at;
    return true;
}

} // namespace MioPanIso9660
