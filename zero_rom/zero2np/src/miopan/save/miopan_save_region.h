#ifndef MIOPAN_SAVE_REGION_H
#define MIOPAN_SAVE_REGION_H
#include "miopan_save_codec.h"
namespace MioPanSave
{
enum class SaveRegion { EU, US, JP };
struct RegionOptions
{
    SaveRegion region = SaveRegion::EU;
    bool copy_extra = false;
    unsigned source_hz = 0, target_hz = 25;
};
const char *RegionFolder(SaveRegion region);
const char *RegionLabel(SaveRegion region);
size_t RegionalSize(SaveRegion region);
// PC saves use the port's 30 Hz clock; stock EU PS2 saves use 25 Hz.
bool ImportRegion(const Bytes &data, SaveRegion region, SaveVersion version,
                  Bytes &native, std::string &error, unsigned eu_hz = 25);
bool ExportRegion(const Bytes &native, SaveRegion region, SaveVersion version,
                  bool copy_extra, Bytes &data, std::string &error, unsigned eu_hz = 25);
std::string RegionNotice(SaveRegion source, SaveRegion target);
} // namespace MioPanSave
#endif
