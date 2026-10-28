#include "miopan_save_transfer.h"
#include "miopan/io/miopan_paths.h"
#include "miopan_memory_card.h"
#include "miopan_save_paths.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <set>
#include <system_error>
#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
namespace MioPanSave
{
namespace
{
namespace fs = std::filesystem;
constexpr const char *Icon = "zero-b-game.ico";
std::string Name(int slot) { return "Zero2Play" + std::to_string(slot); }
uint32_t U32(const uint8_t *p)
{
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
std::string Unique()
{
    static std::atomic<unsigned> n{0};
    return std::to_string(std::chrono::system_clock::now().time_since_epoch().count()) + "-" +
           std::to_string(n++);
}
Bytes Read(const fs::path &p, size_t limit = 1024 * 1024)
{
    if (fs::is_symlink(p) || !fs::is_regular_file(p))
        throw std::runtime_error("Missing or unsafe save file: " + PathUtf8(p.filename()));
    auto size = fs::file_size(p);
    if (size > limit)
        throw std::runtime_error("Save file is too large: " + PathUtf8(p.filename()));
    std::ifstream f(p, std::ios::binary);
    Bytes data(size);
    if (!f || !f.read(reinterpret_cast<char *>(data.data()), size))
        throw std::runtime_error("Cannot read save file: " + PathUtf8(p.filename()));
    return data;
}
void Write(const fs::path &p, const Bytes &bytes, bool exclusive = false)
{
#ifdef _WIN32
    FILE *f = _wfopen(p.c_str(), exclusive ? L"wbx" : L"wb");
#else
    FILE *f = std::fopen(p.c_str(), exclusive ? "wbx" : "wb");
#endif
    if (!f)
        throw std::runtime_error("Cannot create staged file: " + PathUtf8(p.filename()));
    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size() && std::fflush(f) == 0;
#ifdef _WIN32
    if (ok)
        ok = _commit(_fileno(f)) == 0;
#else
    if (ok)
        ok = fsync(fileno(f)) == 0;
#endif
    if (std::fclose(f) != 0)
        ok = false;
    if (!ok)
    {
        if (exclusive)
        {
            std::error_code ec;
            fs::remove(p, ec);
        }
        throw std::runtime_error("Writing save data failed; original files were retained.");
    }
}
void SyncDirectory(const fs::path &path)
{
#ifndef _WIN32
    int fd = open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (fd < 0)
        throw std::runtime_error("Cannot sync save directory.");
    const int result = fsync(fd);
    const int error = errno;
    close(fd);
    // Some removable/network filesystems cannot flush directory handles.
    if (result != 0 && error != EINVAL && error != ENOTSUP)
        throw std::runtime_error("Cannot sync save directory.");
#endif
}
Bytes ConvertSystem(Bytes data, SaveVersion version)
{
    const bool empty = Empty(data);
    if (version != SaveVersion::Prototype && version != SaveVersion::Retail)
        throw std::runtime_error("Unknown PS2 game version.");
    if (version == SaveVersion::Retail && data.size() == 44)
    {
        // Retail adds an effects volume after the prototype's shared/music volume.
        const Bytes volume(data.begin() + 28, data.begin() + 32);
        data.insert(data.begin() + 32, volume.begin(), volume.end());
    }
    else if (version == SaveVersion::Prototype && data.size() == 48)
        data.erase(data.begin() + 32, data.begin() + 36);
    if (!empty)
        Seal(data);
    return data;
}
Folder Fresh(const Folder &source, bool ps2)
{
    Folder out;
    for (auto name : {"Zero2System", "icon.sys", Icon})
        out.files[name] = source.files.at(name);
    out.files[FolderName] = Bytes(HeadSize, 0);
    Seal(out.files[FolderName]);
    for (int i = 0; i < 5; ++i)
        out.files[Name(i)] = EmptySlot(ps2 ? Ps2Size : NativeSize());
    return out;
}
void WriteFolder(const fs::path &path, const Folder &folder)
{
    fs::create_directories(path);
    for (const auto &f : folder.files)
        Write(path / f.first, f.second);
    SyncDirectory(path);
    SyncDirectory(path.parent_path());
}
} // namespace
std::filesystem::path PcFolder()
{
    return std::filesystem::u8path(MioPan_PathUserDir()) / "memcard/mc0-0" / FolderName;
}
bool Inspect(const fs::path &path, bool native_allowed, Folder &folder, std::string &error)
{
    try
    {
        if (fs::is_symlink(path) || !fs::is_directory(path))
            throw std::runtime_error("Choose the extracted EU save folder containing its files.");
        Folder result;
        std::set<std::string> names = {FolderName, "Zero2System", "icon.sys", Icon};
        for (int i = 0; i < 5; ++i)
            names.insert(Name(i));
        for (const auto &e : fs::directory_iterator(path))
            if (!names.count(PathUtf8(e.path().filename())))
                throw std::runtime_error("Unexpected file in the save folder: " +
                                         PathUtf8(e.path().filename()));
        for (const auto &name : names)
            result.files[name] = Read(path / name);
        return InspectFiles(result.files, native_allowed, folder, error);
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
bool InspectFiles(const SaveFiles &files, bool native_allowed, Folder &folder, std::string &error)
{
    try
    {
        if (files.size() != 9)
            throw std::runtime_error("An EU save folder must contain its nine game files.");
        for (auto name : {FolderName, "Zero2System", "icon.sys", Icon})
            if (!files.count(name))
                throw std::runtime_error(std::string("Missing game file: ") + name);
        for (int i = 0; i < 5; ++i)
            if (!files.count(Name(i)))
                throw std::runtime_error("Missing game file: " + Name(i));
        Folder result;
        result.files = files;
        const auto &head = result.files.at(FolderName);
        const auto &sys = result.files.at("Zero2System");
        const auto &icon = result.files.at("icon.sys");
        if (head.size() != HeadSize || !Checksum(head))
            throw std::runtime_error("Invalid EU save header or checksum.");
        if (sys.size() != 44 && sys.size() != 48)
            throw std::runtime_error("Unknown FF2 system-save version; EU retail or February 2004 "
                                     "prototype saves are required.");
        if (!Checksum(sys) && !Empty(sys))
            throw std::runtime_error("Invalid system-save checksum.");
        result.version = sys.size() == 48 ? SaveVersion::Retail : SaveVersion::Prototype;
        // Older MioPan exports paired prototype options with retail gameplay pointers.
        if (sys.size() == 44)
        {
            std::string detail;
            for (int i = 0; i < 5; ++i)
                if (head[125 + i] == 1 &&
                    !Validate(files.at(Name(i)), detail, SaveVersion::Prototype) &&
                    Validate(files.at(Name(i)), detail, SaveVersion::Retail))
                    result.version = SaveVersion::Retail;
        }
        if (icon.size() != 964 || std::memcmp(icon.data(), "PS2D", 4) != 0 ||
            result.files.at(Icon).empty())
            throw std::runtime_error("Missing or invalid PS2 save icons.");
        for (int i = 0; i < 5; ++i)
        {
            const auto &d = result.files.at(Name(i));
            auto &slot = result.slots[i];
            if (d.size() != Ps2Size && (!native_allowed || d.size() != NativeSize()))
                throw std::runtime_error(
                    "Unsupported slot size/version; EU PS2 saves are required.");
            if (head[125 + i] > 1 || head[120 + i] > 1)
                throw std::runtime_error("Invalid slot availability flag.");
            slot.occupied = head[125 + i] == 1;
            if (!slot.occupied)
            {
                if (!Empty(d))
                    throw std::runtime_error("Empty slot metadata does not match the slot file.");
                slot.description = "Empty";
                continue;
            }
            if (!Validate(d, error, result.version))
                return false;
            const auto *g = d.data();
            if (U32(head.data() + i * 4) != g[0] || U32(head.data() + 20 + i * 4) != g[2] ||
                head[120 + i] != g[3])
                throw std::runtime_error("Slot metadata does not match gameplay data.");
            slot.player = "Mio";
            slot.chapter = g[0];
            slot.area = int(U32(head.data() + 40 + i * 4));
            slot.finder = head[120 + i] != 0;
            const auto *time = head.data() + 60 + i * 12;
            slot.hours = U32(time);
            slot.minutes = U32(time + 4);
            slot.seconds = U32(time + 8);
            if (slot.minutes >= 60 || slot.seconds >= 60 || slot.area < 0 || slot.area > 264)
                throw std::runtime_error("Invalid saved play time or room.");
            slot.description = "Mio | chapter " + std::to_string(slot.chapter) + " | room " +
                               std::to_string(slot.area);
        }
        folder = std::move(result);
        error.clear();
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
} // namespace MioPanSave

namespace MioPanSave
{
bool ReadStorage(const fs::path &path, StorageKind kind, bool native_allowed, Storage &storage,
                 std::string &error)
{
    try
    {
        Storage result;
        result.kind = kind;
        result.path = path;
        if (kind == StorageKind::BlankCard)
        {
            storage = std::move(result);
            error.clear();
            return true;
        }
        result.exists = fs::exists(path);
        if (result.exists)
        {
            if (kind == StorageKind::Card)
            {
                result.card = Read(path, 8650752);
                SaveFiles files;
                if (result.card.size() == 8388608 || result.card.size() == 8650752)
                {
                    if (!ReadMemoryCard(result.card, files, result.has_save, error))
                        return false;
                }
                else
                {
                    std::string directory;
                    if (!ReadSaveArchive(result.card, directory, files, error))
                        return false;
                    if (directory != FolderName)
                        throw std::runtime_error("This archive is not an EU Project Zero II save.");
                    result.has_save = true;
                }
                if (result.has_save && !InspectFiles(files, false, result.save, error))
                    return false;
            }
            else
            {
                if (!Inspect(path, native_allowed, result.save, error))
                    return false;
                result.has_save = true;
            }
        }
        else if (kind == StorageKind::Card)
            throw std::runtime_error("Choose an existing memory-card file.");
        storage = std::move(result);
        error.clear();
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
bool BuildBatch(const Folder &source, const Folder *destination,
                const std::vector<SlotMapping> &slots, bool export_ps2, Folder &result,
                std::string &error, SaveVersion version)
{
    try
    {
        if (slots.empty() || slots.size() > 5)
            throw std::runtime_error("Select between one and five saves.");
        Folder checked;
        if (!InspectFiles(source.files, export_ps2, checked, error))
            return false;
        if (destination && !InspectFiles(destination->files, !export_ps2, checked, error))
            return false;
        Folder next = destination ? *destination : Fresh(source, export_ps2);
        std::set<int> sources, targets;
        for (const auto &slot : slots)
        {
            if (slot.source < 0 || slot.source >= 5 || slot.destination < 0 ||
                slot.destination >= 5)
                throw std::runtime_error("Assign a destination slot to each selected save.");
            if (!sources.insert(slot.source).second || !targets.insert(slot.destination).second)
                throw std::runtime_error("Each source and destination slot can be used only once.");
            if (!source.slots[slot.source].occupied)
                throw std::runtime_error("Cannot transfer an empty slot.");
            auto payload = source.files.at(Name(slot.source));
            if (!ToNative(payload, payload, error, source.version))
                return false;
            if (export_ps2 && !ToPs2(payload, payload, error, version))
                return false;
            next.files[Name(slot.destination)] = std::move(payload);
            auto *out = next.files[FolderName].data();
            const auto *in = source.files.at(FolderName).data();
            for (const auto &field :
                 {std::pair<size_t, size_t>{0, 4}, {20, 4}, {40, 4}, {60, 12}, {120, 1}, {125, 1}})
                std::memcpy(out + field.first + slot.destination * field.second,
                            in + field.first + slot.source * field.second, field.second);
        }
        Seal(next.files[FolderName]);
        next.files["Zero2System"] = ConvertSystem(next.files.at("Zero2System"),
                                                  export_ps2 ? version : SaveVersion::Prototype);
        return InspectFiles(next.files, !export_ps2, result, error);
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
namespace
{
bool SameStorage(const Storage &expected, bool native, std::string &error)
{
    Storage current;
    return ReadStorage(expected.path, expected.kind, native, current, error) &&
           current.exists == expected.exists && current.has_save == expected.has_save &&
           current.card == expected.card && current.save.files == expected.save.files;
}
void CommitFolder(const Storage &expected, const Folder &folder, bool native, std::string &error)
{
    const auto dest = fs::absolute(expected.path).lexically_normal();
    if (fs::is_symlink(dest))
        throw std::runtime_error("Destination cannot be a symbolic link.");
    fs::create_directories(dest.parent_path());
    const auto stage = dest.parent_path() / (".miopan-transfer-" + Unique());
    const auto rollback = dest.parent_path() / (".miopan-rollback-" + Unique());
    bool moved = false, installed = false;
    try
    {
        if (!fs::create_directory(stage))
            throw std::runtime_error("Cannot create staging directory.");
        WriteFolder(stage, folder);
        Folder verified;
        if (!Inspect(stage, native, verified, error) || verified.files != folder.files)
            throw std::runtime_error("Staged save verification failed. " + error);
        if (!SameStorage(expected, native, error))
            throw std::runtime_error("Destination changed; refresh before transferring.");
        if (expected.exists)
        {
            fs::rename(dest, rollback);
            moved = true;
        }
        fs::rename(stage, dest);
        installed = true;
        SyncDirectory(dest.parent_path());
    }
    catch (...)
    {
        std::error_code ec;
        if (installed)
        {
            fs::rename(dest, stage, ec);
            if (ec)
                throw std::runtime_error(
                    "Could not roll back the transfer; previous folder remains at " +
                    PathUtf8(rollback));
        }
        if (moved)
        {
            fs::rename(rollback, dest, ec);
            if (ec)
                throw std::runtime_error("Could not restore the previous folder; it remains at " +
                                         PathUtf8(rollback));
        }
        fs::remove_all(stage, ec);
        throw;
    }
    std::error_code ec;
    if (moved)
        fs::remove_all(rollback, ec);
    if (ec)
        error = "Transfer complete; could not remove temporary folder " + PathUtf8(rollback);
}
void CommitCard(const fs::path &path, const Bytes &bytes)
{
    if (path.empty() || fs::exists(path) || fs::is_symlink(path))
        throw std::runtime_error(
            "Choose a new output filename; existing output files are never overwritten.");
    auto dest = fs::absolute(path).lexically_normal();
    fs::create_directories(dest.parent_path());
    const auto stage = dest.parent_path() / (".miopan-card-" + Unique());
    bool published = false;
    try
    {
        Write(stage, bytes);
        // Copy the validated bytes into a new file; exclusive creation refuses overwrites.
        Write(dest, bytes, true);
        published = true;
        fs::remove(stage);
        SyncDirectory(dest.parent_path());
    }
    catch (...)
    {
        std::error_code ec;
        fs::remove(stage, ec);
        if (published)
            fs::remove(dest, ec);
        throw;
    }
}
} // namespace
bool TransferBatch(const Storage &source, const Storage &destination,
                   const std::vector<SlotMapping> &slots, bool export_ps2,
                   const fs::path &card_output, std::string &error, bool ecc,
                   SaveArchiveFormat archive, SaveVersion version)
{
    try
    {
        if (source.kind == StorageKind::BlankCard || !source.has_save)
            throw std::runtime_error("Source contains no EU MioPan saves.");
        if ((!export_ps2 && destination.kind != StorageKind::Folder) ||
            (export_ps2 && source.kind != StorageKind::Folder))
            throw std::runtime_error("Choose PC saves on the PC side of the transfer.");
        if (destination.kind == StorageKind::Folder && source.kind == StorageKind::Folder &&
            fs::weakly_canonical(source.path) == fs::weakly_canonical(destination.path))
            throw std::runtime_error("Source and destination must be different folders.");
        if (!SameStorage(source, export_ps2, error) ||
            !SameStorage(destination, !export_ps2, error))
            throw std::runtime_error(
                "Source or destination changed; refresh and review the slots again. " + error);
        Folder next;
        if (!BuildBatch(source.save, destination.has_save ? &destination.save : nullptr, slots,
                        export_ps2, next, error, version))
            return false;
        error.clear();
        if (destination.kind == StorageKind::Folder)
            CommitFolder(destination, next, !export_ps2, error);
        else
        {
            Bytes output;
            if (archive == SaveArchiveFormat::None)
            {
                if (!WriteMemoryCard(destination.card, next.files, output, error, ecc))
                    return false;
            }
            else if (!WriteSaveArchive(FolderName, next.files, archive, output, error))
                return false;
            if (!SameStorage(source, export_ps2, error) || !SameStorage(destination, false, error))
                throw std::runtime_error(
                    "Save files changed while preparing the card; refresh before exporting.");
            CommitCard(card_output, output);
        }
        return true;
    }
    catch (const std::exception &e)
    {
        error = e.what();
        return false;
    }
}
} // namespace MioPanSave
