#ifndef MIOPAN_SAVE_ARCHIVE_H
#define MIOPAN_SAVE_ARCHIVE_H
#include "miopan_memory_card.h"
namespace MioPanSave
{
enum class SaveArchiveFormat
{
    None,
    Psu,
    Max
};
bool ReadSaveArchive(const Bytes &data, std::string &directory, SaveFiles &files,
                     std::string &error);
bool WriteSaveArchive(const std::string &directory, const SaveFiles &files,
                      SaveArchiveFormat format, Bytes &data, std::string &error);
} // namespace MioPanSave
#endif
