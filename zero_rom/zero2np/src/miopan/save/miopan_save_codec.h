#ifndef MIOPAN_SAVE_CODEC_H
#define MIOPAN_SAVE_CODEC_H
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
namespace MioPanSave
{
using Bytes = std::vector<uint8_t>;
constexpr size_t Ps2Size = 36147;
constexpr size_t HeadSize = 136;
constexpr const char *FolderName = "BESLES-52384ZERO-BD";
size_t NativeSize();
bool Empty(const Bytes &data);
bool Checksum(const Bytes &data);
void Seal(Bytes &data);
Bytes EmptySlot(size_t size);
size_t BlockOffset(unsigned block, bool native);
bool Validate(const Bytes &data, std::string &error);
bool ToNative(const Bytes &ps2, Bytes &native, std::string &error);
bool ToPs2(const Bytes &native, Bytes &ps2, std::string &error);
} // namespace MioPanSave
#endif
