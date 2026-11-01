#ifndef MIOPAN_ISO9660_H
#define MIOPAN_ISO9660_H

/*
 * Files out of a disc image, for the imports (miopan_undub.cpp).
 *
 * Just enough ISO 9660 for a PS2 DVD as PCSX2 and the dumping tools write it:
 * 2048-byte sectors, the primary volume descriptor's directory tree, and files
 * of one extent.  No Joliet or UDF names -- a PS2 disc's ISO 9660 tree is the
 * one its own boot ROM reads -- and no raw 2352-byte CD sectors or compressed
 * images (.cso, .chd), which Open() turns away as not an image.
 */

#include <cstdint>
#include <string>
#include <vector>

struct MioPan_File;

namespace MioPanIso9660
{

struct Entry
{
    std::string name;           /* as the disc spells it, without ";1" */
    uint64_t    offset = 0;     /* bytes from the start of the image */
    uint64_t    size = 0;
    bool        directory = false;
};

enum class OpenResult
{
    Ok,
    Unreadable,     /* not there, or not readable */
    NotAnImage,     /* readable, but no ISO 9660 volume in it */
};

class Image
{
public:
    Image() = default;
    ~Image();
    Image(const Image &) = delete;
    Image &operator=(const Image &) = delete;

    OpenResult Open(const std::string &path);

    const std::string &Path() const
    {
        return path_;
    }
    /* The image file's own size: an entry that runs past it is cut short. */
    uint64_t Size() const
    {
        return size_;
    }

    /* A file or directory by its path from the root, "SYSTEM.CNF" or
     * "DIR/FILE.BIN", ignoring case and the ";1". */
    bool Find(const std::string &path, Entry &out);
    std::vector<Entry> List(const Entry &dir);

    bool Read(uint64_t offset, void *buf, size_t size);

private:
    std::string  path_;
    MioPan_File *file_ = nullptr;
    uint64_t     size_ = 0;
    uint32_t     block_ = 2048;
    Entry        root_;
};

} // namespace MioPanIso9660

#endif /* MIOPAN_ISO9660_H */
