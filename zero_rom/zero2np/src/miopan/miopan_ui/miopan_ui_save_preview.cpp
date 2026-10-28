#include "miopan_ui_save_preview.h"
#include "ingame/menu/tim_dat/map_room_dat.h"
#include "miopan/io/miopan_fileload.h"
#include "system/eeiop/cddat.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstring>
#include <map>
namespace MioPanUiSavePreview
{
namespace
{
SDL_GPUDevice *device;
struct Texture
{
    SDL_GPUTexture *gpu = nullptr;
    int width = 0, height = 0;
};
std::map<int, Texture> textures;
unsigned U16(const uint8_t *p) { return unsigned(p[0]) | unsigned(p[1]) << 8; }
unsigned U32(const uint8_t *p) { return U16(p) | U16(p + 2) << 16; }
MioPanSave::Bytes LoadAsset(int file)
{
    const unsigned bytes = GetFileSize(file);
    if (!bytes || bytes > 4 * 1024 * 1024)
        return {};
    MioPanSave::Bytes input(bytes);
    char path[256];
    GetFileNameBuffer(file, path);
    std::string named = path;
    std::string raw = named;
    if (cddatIsCmpFile(file))
    {
        const auto dot = named.find_last_of('.');
        if (dot != std::string::npos)
            named[dot] = '_';
        named += ".cmp";
    }
    MioPan_FileLoadReq req{};
    req.ee_adrs = reinterpret_cast<uintptr_t>(input.data());
    req.request_size = int(cddatIsCmpFile(file) ? GetFileCmpSize(file) : bytes);
    req.start_sector = GetFileStartSector(file);
    req.cmp_file_name = named.c_str();
    req.raw_file_name = raw.c_str();
    req.raw_size = bytes;
    req.wrk_size = int(bytes);
    req.is_cmp = cddatIsCmpFile(file);
    req.file_no = file;
    MioPan_FileLoadServe(&req);
    return input;
}
Texture Load(int file)
{
    Texture texture;
    MioPanSave::Bytes rgba;
    const auto input = LoadAsset(file);
    if (!Decode(input, texture.width, texture.height, rgba))
        return texture;
    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = unsigned(texture.width);
    info.height = unsigned(texture.height);
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    texture.gpu = SDL_CreateGPUTexture(device, &info);
    SDL_GPUTransferBufferCreateInfo upload_info{};
    upload_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    upload_info.size = unsigned(rgba.size());
    auto *upload = SDL_CreateGPUTransferBuffer(device, &upload_info);
    void *mapped = upload ? SDL_MapGPUTransferBuffer(device, upload, false) : nullptr;
    if (!texture.gpu || !mapped)
    {
        if (texture.gpu)
            SDL_ReleaseGPUTexture(device, texture.gpu);
        if (upload)
            SDL_ReleaseGPUTransferBuffer(device, upload);
        texture.gpu = nullptr;
        return texture;
    }
    std::memcpy(mapped, rgba.data(), rgba.size());
    SDL_UnmapGPUTransferBuffer(device, upload);
    auto *command = SDL_AcquireGPUCommandBuffer(device);
    auto *copy = command ? SDL_BeginGPUCopyPass(command) : nullptr;
    if (copy)
    {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = upload;
        SDL_GPUTextureRegion target{};
        target.texture = texture.gpu;
        target.w = info.width;
        target.h = info.height;
        target.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &target, false);
        SDL_EndGPUCopyPass(copy);
        if (!SDL_SubmitGPUCommandBuffer(command))
        {
            SDL_ReleaseGPUTexture(device, texture.gpu);
            texture.gpu = nullptr;
        }
    }
    else
    {
        if (command)
            SDL_CancelGPUCommandBuffer(command);
        SDL_ReleaseGPUTexture(device, texture.gpu);
        texture.gpu = nullptr;
    }
    SDL_ReleaseGPUTransferBuffer(device, upload);
    return texture;
}
} // namespace
bool Decode(const MioPanSave::Bytes &file, int &width, int &height, MioPanSave::Bytes &rgba)
{
    if (file.size() >= 32 && std::memcmp(file.data(), "TIM2", 4) != 0)
    {
        const size_t offset = U32(file.data() + 16);
        if (!U32(file.data()) || offset < 32 || offset > file.size() - 64 ||
            std::memcmp(file.data() + offset, "TIM2", 4) != 0)
            return false;
        MioPanSave::Bytes picture(file.begin() + offset, file.end());
        return Decode(picture, width, height, rgba);
    }
    if (file.size() < 64 || file[5] > 1 || std::memcmp(file.data(), "TIM2", 4) != 0)
        return false;
    const size_t offset = file[5] == 1 ? 128 : 16;
    if (offset + 48 > file.size())
        return false;
    const auto *p = file.data() + offset;
    const unsigned header = U16(p + 12), image = U32(p + 8), clut = U32(p + 4);
    const unsigned w = U16(p + 20), h = U16(p + 22), type = p[19];
    if (header < 48 || w == 0 || h == 0 || w > 1024 || h > 1024 ||
        size_t(header) + image + clut > file.size() - offset)
        return false;
    const unsigned bpp = type == 1   ? 16
                         : type == 2 ? 24
                         : type == 3 ? 32
                         : type == 4 ? 4
                         : type == 5 ? 8
                                     : 0;
    if (!bpp || image < (size_t(w) * h * bpp + 7) / 8)
        return false;
    const auto *pixels = p + header, *palette = pixels + image;
    const unsigned colors = U16(p + 14), palette_type = p[18] & 0x3f;
    const unsigned palette_bytes = palette_type == 1   ? 2
                                   : palette_type == 2 ? 3
                                   : palette_type == 3 ? 4
                                                       : 0;
    if (bpp <= 8 &&
        (!palette_bytes || colors != (bpp == 4 ? 16u : 256u) || clut < colors * palette_bytes))
        return false;
    MioPanSave::Bytes out(size_t(w) * h * 4);
    for (size_t i = 0; i < size_t(w) * h; ++i)
    {
        const uint8_t *color = nullptr;
        unsigned color_bytes = bpp / 8;
        if (bpp <= 8)
        {
            unsigned index = bpp == 4 ? (pixels[i / 2] >> ((i % 2) * 4)) & 15 : pixels[i];
            if (bpp == 8 && !(p[18] & 0x80))
                index = (index & 0xe7) | ((index & 8) << 1) | ((index & 16) >> 1);
            color = palette + index * palette_bytes;
            color_bytes = palette_bytes;
        }
        else
            color = pixels + i * color_bytes;
        if (color_bytes == 2)
        {
            const unsigned rgb = U16(color);
            for (unsigned c = 0; c < 3; ++c)
                out[i * 4 + c] = uint8_t(((rgb >> (5 * c)) & 31) * 255 / 31);
        }
        else
            std::copy_n(color, 3, out.data() + i * 4);
        out[i * 4 + 3] = 255;
    }
    if (w < 106 || h < 63)
        return false;
    MioPanSave::Bytes cropped(105 * 62 * 4);
    for (unsigned y = 0; y < 62; ++y)
        std::copy_n(out.data() + ((y + 1) * w + 1) * 4, 105 * 4, cropped.data() + y * 105 * 4);
    width = 105;
    height = 62;
    rgba = std::move(cropped);
    return true;
}
void Init(SDL_GPUDevice *gpu) { device = gpu; }
void Shutdown()
{
    if (device)
        for (auto &[id, t] : textures)
            if (t.gpu)
                SDL_ReleaseGPUTexture(device, t.gpu);
    textures.clear();
    device = nullptr;
}
void Draw(const MioPanSave::SlotInfo &slot, ImVec2 size)
{
    int file = -1;
    if (device && slot.occupied)
    {
        if (slot.finder)
            file = SNP_CLEAR_DATA_PK2;
        else
            for (const auto &room : room_info_dat)
            {
                if (room.map_label == -1)
                    break;
                if (room.room_label == slot.area)
                {
                    file = room.snap_tex_label;
                    break;
                }
            }
        if (file >= 0 && file < GetFileNum() && !textures.count(file))
            textures.emplace(file, Load(file));
    }
    const auto found = textures.find(file);
    if (found != textures.end() && found->second.gpu)
        ImGui::Image((ImTextureID)(intptr_t)found->second.gpu, size);
    else
    {
        const auto at = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddRectFilled(at, ImVec2(at.x + size.x, at.y + size.y),
                                                  IM_COL32(32, 35, 38, 255));
        ImGui::GetWindowDrawList()->AddText(ImVec2(at.x + 5, at.y + size.y / 2 - 6),
                                            IM_COL32(140, 145, 150, 255),
                                            slot.occupied ? "No preview" : "Empty");
        ImGui::Dummy(size);
    }
}
} // namespace MioPanUiSavePreview
