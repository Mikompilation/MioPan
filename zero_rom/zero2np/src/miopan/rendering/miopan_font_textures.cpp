#include "miopan_font_textures.h"
#include "../io/miopan_paths.h"
#include "miopan_font_identity.h"
#include "../io/miopan_fileload.h"
#include "system/eeiop/cddat.h"
#include "system/os/system.h"
#include <array>
#include <algorithm>
#include <SDL3/SDL.h>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
namespace MioPanFonts
{
namespace {
std::array<std::string, 5> identities;
bool ReadFont(int file, std::vector<unsigned char> &data)
{
    if (file < 0 || file >= GetFileNum()) return false;
    const unsigned int raw_size = GetFileSize(file);
    const bool compressed = cddatIsCmpFile(file) != 0;
    const unsigned int stored_size = compressed ? GetFileCmpSize(file) : raw_size;
    if (!raw_size || !stored_size || raw_size > 16 * 1024 * 1024 || stored_size > 16 * 1024 * 1024) return false;
    char path[256];
    GetFileNameBuffer(file, path);
    std::string stored = path;
    if (compressed) {
        const auto dot = stored.find_last_of('.');
        if (dot != std::string::npos) stored[dot] = '_';
        stored += ".cmp";
    }
    data.resize(std::max(raw_size, stored_size));
    MioPan_FileLoadReq req{};
    req.request_size = req.wrk_size = (int)stored_size;
    req.start_sector = GetFileStartSector(file);
    req.cmp_file_name = stored.c_str(); req.raw_file_name = path;
    req.raw_size = raw_size; req.is_cmp = compressed; req.file_no = file;
    if (!MioPan_FileLoadReadData(&req, data.data(), nullptr)) return false;
    data.resize(raw_size);
    return true;
}
}
void Prepare()
{
    static bool prepared = false;
    if (prepared) return;
    prepared = true;
    for (int language = 0; language < 5; ++language)
    {
        std::vector<std::vector<unsigned char>> packs;
        bool valid = true;
        for (int file : {VRAM_TEX_PK2 + language})
        {
            std::vector<unsigned char> pack;
            if (!ReadFont(file, pack)) { valid = false; break; }
            packs.push_back(std::move(pack));
        }
        if (valid) identities[language] = Fingerprint(packs);
        SDL_Log("MioPan: original font set %d -> %s", language,
                identities[language].empty() ? "unavailable; using original fonts" : identities[language].c_str());
    }
}
int CurrentSet()
{
    const int language = GetLanguage();
    return language >= 0 && language < 5 ? language : -1;
}
bool LoadOverride(int bank, int width, int height, Image &image)
{
    const int set = CurrentSet();
    if (set < 0 || identities[set].empty()) return false;
    const std::string relative = "resources/fonts/" + identities[set] + "/hd_texture_font_" + std::to_string(bank) + ".png";
    char path[4096];
    if (!MioPan_PathResolveAsset(relative.c_str(), path, sizeof(path))) return false;
    unsigned char header[24]{};
    std::ifstream input(std::filesystem::u8path(path), std::ios::binary);
    input.read(reinterpret_cast<char *>(header), sizeof(header));
    const auto size = [&](int at) { return unsigned(header[at]) << 24 | unsigned(header[at + 1]) << 16 | unsigned(header[at + 2]) << 8 | header[at + 3]; };
    const unsigned w = size(16), h = size(20);
    if (!input || std::memcmp(header, "\x89PNG\r\n\x1a\n", 8) ||
        width <= 0 || height <= 0 || w < unsigned(width) || w > unsigned(width) * 4 ||
        w % width || h != unsigned(height) * (w / width))
    {
        SDL_Log("MioPan: ignored font bank %d PNG; expected an unchanged atlas layout at 1x, 2x, 3x or 4x", bank);
        return false;
    }
    SDL_Surface *loaded = SDL_LoadPNG(path);
    SDL_Surface *rgba = loaded ? SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32) : nullptr;
    SDL_DestroySurface(loaded);
    if (!rgba || unsigned(rgba->w) != w || unsigned(rgba->h) != h)
    {
        SDL_DestroySurface(rgba);
        SDL_Log("MioPan: could not decode font bank %d PNG: %s", bank, SDL_GetError());
        return false;
    }
    image.width = int(w); image.height = int(h);
    image.rgba.resize(size_t(w) * h * 4);
    for (unsigned y = 0; y < h; ++y)
        std::memcpy(image.rgba.data() + size_t(y) * w * 4,
                    static_cast<const unsigned char *>(rgba->pixels) + size_t(y) * rgba->pitch, size_t(w) * 4);
    SDL_DestroySurface(rgba);
    return true;
}
}
