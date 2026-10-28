#include "miopan_rmlui.h"
#include "miopan_rml_options.h"
#include "miopan_rml_load.h"
#include "miopan_rml_pause.h"
#include "miopan_rml_save.h"
#include "miopan_rml_savepoint.h"
#include "miopan_rml_setup.h"
#include "miopan_rml_title.h"
#include "../io/miopan_file.h"
#include "../io/miopan_input.h"
#include "../io/miopan_paths.h"
#include "../rendering/miopan_renderer.h"

#include <RmlUi/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_surface.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace
{
/* The ROM's screen, which one dp stands for -- see the header. */
constexpr float kDesignWidth = 640.0f;
constexpr float kDesignHeight = 448.0f;

constexpr const char *kFontFiles[] = {
    "resources/fonts/CrimsonText-Regular.ttf",
    "resources/fonts/CrimsonText-SemiBold.ttf",
    "resources/fonts/CrimsonText-Bold.ttf",
    "resources/fonts/CrimsonText-Italic.ttf",
};

/* Pad navigation: up, down, left, right, as the arrow keys RmlUi and the
 * screens navigate with.  A held direction repeats like a held key. */
constexpr Rml::Input::KeyIdentifier kNavKeys[4] = {
    Rml::Input::KI_UP, Rml::Input::KI_DOWN, Rml::Input::KI_LEFT,
    Rml::Input::KI_RIGHT};
constexpr Uint64 kNavRepeatDelayMs = 380;
constexpr Uint64 kNavRepeatIntervalMs = 85;
/* The left stick counts as a direction past the first and lets go below the
 * second, so a stick resting near the edge does not chatter. */
constexpr float kStickPress = 0.6f;
constexpr float kStickRelease = 0.35f;

struct Geometry
{
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};

/* One RenderGeometry() call.  The geometry itself is copied into the frame's
 * vertex and index lists, so a command outlives the handle it came from. */
struct DrawCommand
{
    /* Never null: untextured geometry names the white texture. */
    SDL_GPUTexture *texture;
    Uint32 first_index;
    Uint32 index_count;
    Sint32 vertex_offset;
    bool scissor;
    /* In context pixels. */
    Rml::Rectanglei scissor_region;
    Rml::Matrix4f transform;
    Rml::Vector2f translation;
};

/* Matches rmlui.vert.hlsl's cbuffer. */
struct VertexUniforms
{
    float transform[16];
    float translation[4];
};

/* A texture RmlUi made.  `refs` counts the draw lists that name it, the
 * recorded one and every packet's copy, and it is released only once RmlUi is
 * done with it AND no list does -- a packet can sit on the presenting thread
 * for a few frames after RmlUi has moved on. */
struct TextureRecord
{
    int refs;
    bool released;
};
}

namespace MioPanRml
{
struct DrawSnapshot
{
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
    std::vector<DrawCommand> commands;
    /* Each texture the commands name, once, for TextureRecord::refs. */
    std::vector<SDL_GPUTexture *> textures;
    /* Where the context's origin sits in the output, in output pixels. */
    int origin_x = 0;
    int origin_y = 0;
};
}

namespace
{
struct RmlState
{
    SDL_Window *window = nullptr;
    SDL_GPUDevice *device = nullptr;
    SDL_GPUShader *vertex_shader = nullptr;
    SDL_GPUShader *fragment_shader = nullptr;
    SDL_GPUGraphicsPipeline *pipeline = nullptr;
    SDL_GPUSampler *sampler = nullptr;
    SDL_GPUTexture *white_texture = nullptr;

    /* Shared by every present, cycled on each upload. */
    SDL_GPUBuffer *vertex_buffer = nullptr;
    Uint32 vertex_capacity = 0;
    SDL_GPUBuffer *index_buffer = nullptr;
    Uint32 index_capacity = 0;
    SDL_GPUTransferBuffer *transfer_buffer = nullptr;
    Uint32 transfer_capacity = 0;

    bool rml_initialised = false;
    Rml::Context *context = nullptr;

    std::unordered_map<SDL_GPUTexture *, TextureRecord> textures;

    /* What Frame() recorded, and the render state while it records. */
    MioPanRml::DrawSnapshot frame;
    bool scissor_enabled = false;
    Rml::Rectanglei scissor_region;
    Rml::Matrix4f transform = Rml::Matrix4f::Identity();

    /* The UI holding the controls -- see UpdateControlHold(). */
    bool holding_controls = false;

    /* Pad navigation, per direction: held by the d-pad, by the left stick,
     * and when the next repeat is due. */
    bool nav_dpad[4] = {};
    bool nav_stick[4] = {};
    Uint64 nav_next_repeat[4] = {};
    float stick_x = 0.0f;
    float stick_y = 0.0f;
    /* Which device the player last used: the pad, for the button prompts,
     * and the mouse, for whether hover may move a cursor. */
    bool using_pad = false;
    bool using_mouse = false;

    /* Images made at run time, by name -- see SetGeneratedImage(). */
    struct GeneratedImage
    {
        std::vector<Uint8> rgba;
        int width;
        int height;
    };
    std::unordered_map<std::string, GeneratedImage> generated;
};

RmlState g_rml;

void Retain(const std::vector<SDL_GPUTexture *> &textures)
{
    for (SDL_GPUTexture *texture : textures)
    {
        auto record = g_rml.textures.find(texture);
        if (record != g_rml.textures.end())
        {
            record->second.refs++;
        }
    }
}

void Drop(const std::vector<SDL_GPUTexture *> &textures)
{
    for (SDL_GPUTexture *texture : textures)
    {
        auto record = g_rml.textures.find(texture);
        if (record == g_rml.textures.end())
        {
            continue;
        }
        if (--record->second.refs <= 0 && record->second.released)
        {
            SDL_ReleaseGPUTexture(g_rml.device, texture);
            g_rml.textures.erase(record);
        }
    }
}

/* Game thread.  The upload is its own submit, as ImGui's texture updates are:
 * RmlUi asks for a texture in the middle of laying a frame out, long before
 * there is a frame command buffer to put it in. */
SDL_GPUTexture *CreateTexture(const Uint8 *pixels, int width, int height,
                              int pitch)
{
    if (g_rml.device == nullptr || pixels == nullptr || width <= 0 ||
        height <= 0)
    {
        return nullptr;
    }

    SDL_GPUTextureCreateInfo info{};
    info.type = SDL_GPU_TEXTURETYPE_2D;
    info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    info.width = (Uint32)width;
    info.height = (Uint32)height;
    info.layer_count_or_depth = 1;
    info.num_levels = 1;
    info.sample_count = SDL_GPU_SAMPLECOUNT_1;
    SDL_GPUTexture *texture = SDL_CreateGPUTexture(g_rml.device, &info);
    if (texture == nullptr)
    {
        SDL_Log("MioPan RmlUi: SDL_CreateGPUTexture(%dx%d) failed: %s", width,
                height, SDL_GetError());
        return nullptr;
    }

    const Uint32 row = (Uint32)width * 4;
    SDL_GPUTransferBufferCreateInfo transfer_info{};
    transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transfer_info.size = row * (Uint32)height;
    SDL_GPUTransferBuffer *transfer =
        SDL_CreateGPUTransferBuffer(g_rml.device, &transfer_info);
    Uint8 *staging = transfer != nullptr
                         ? (Uint8 *)SDL_MapGPUTransferBuffer(g_rml.device,
                                                             transfer, false)
                         : nullptr;
    if (staging == nullptr)
    {
        SDL_Log("MioPan RmlUi: texture staging failed: %s", SDL_GetError());
        if (transfer != nullptr)
        {
            SDL_ReleaseGPUTransferBuffer(g_rml.device, transfer);
        }
        SDL_ReleaseGPUTexture(g_rml.device, texture);
        return nullptr;
    }
    for (int y = 0; y < height; y++)
    {
        std::memcpy(staging + (size_t)y * row, pixels + (size_t)y * pitch, row);
    }
    SDL_UnmapGPUTransferBuffer(g_rml.device, transfer);

    bool submitted = false;
    SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(g_rml.device);
    if (cmd != nullptr)
    {
        SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(cmd);
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        source.pixels_per_row = (Uint32)width;
        source.rows_per_layer = (Uint32)height;
        SDL_GPUTextureRegion destination{};
        destination.texture = texture;
        destination.w = (Uint32)width;
        destination.h = (Uint32)height;
        destination.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
        SDL_EndGPUCopyPass(copy);
        submitted = SDL_SubmitGPUCommandBuffer(cmd);
    }
    /* Safe straight after the submit: SDL keeps it until the copy is done. */
    SDL_ReleaseGPUTransferBuffer(g_rml.device, transfer);
    if (!submitted)
    {
        SDL_Log("MioPan RmlUi: texture upload failed: %s", SDL_GetError());
        SDL_ReleaseGPUTexture(g_rml.device, texture);
        return nullptr;
    }
    return texture;
}

/* Grow-only, doubling.  The old buffer may still be bound by a submitted
 * present; SDL defers the release until it is not. */
bool EnsureBuffer(SDL_GPUBuffer **buffer, Uint32 *capacity,
                  SDL_GPUBufferUsageFlags usage, Uint32 size)
{
    if (*buffer != nullptr && *capacity >= size)
    {
        return true;
    }
    if (*buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(g_rml.device, *buffer);
        *buffer = nullptr;
        *capacity = 0;
    }

    SDL_GPUBufferCreateInfo info{};
    info.usage = usage;
    info.size = std::max<Uint32>({size, *capacity * 2, 64u * 1024u});
    *buffer = SDL_CreateGPUBuffer(g_rml.device, &info);
    if (*buffer == nullptr)
    {
        SDL_Log("MioPan RmlUi: SDL_CreateGPUBuffer(%u) failed: %s", info.size,
                SDL_GetError());
        return false;
    }
    *capacity = info.size;
    return true;
}

bool EnsureTransferBuffer(Uint32 size)
{
    if (g_rml.transfer_buffer != nullptr && g_rml.transfer_capacity >= size)
    {
        return true;
    }
    if (g_rml.transfer_buffer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(g_rml.device, g_rml.transfer_buffer);
        g_rml.transfer_buffer = nullptr;
    }

    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    info.size = std::max<Uint32>(
        {size, g_rml.transfer_capacity * 2, 128u * 1024u});
    g_rml.transfer_buffer = SDL_CreateGPUTransferBuffer(g_rml.device, &info);
    g_rml.transfer_capacity =
        g_rml.transfer_buffer != nullptr ? info.size : 0;
    return g_rml.transfer_buffer != nullptr;
}

/* RmlUi's files are the install's resources: a document names its style
 * sheets and images relative to itself, RmlUi joins that onto the document's
 * own path, and what arrives here is relative to the install. */
class FileInterface final : public Rml::FileInterface
{
public:
    Rml::FileHandle Open(const Rml::String &path) override
    {
        char host_path[1024];
        if (!MioPan_PathResolveAsset(path.c_str(), host_path,
                                     sizeof(host_path)))
        {
            SDL_Log("MioPan RmlUi: no such resource: %s", path.c_str());
            return 0;
        }
        return (Rml::FileHandle)MioPan_FileOpen(host_path, "rb");
    }

    void Close(Rml::FileHandle file) override
    {
        MioPan_FileCloseHandle((MioPan_File *)file);
    }

    size_t Read(void *buffer, size_t size, Rml::FileHandle file) override
    {
        return MioPan_FileRead((MioPan_File *)file, buffer, size);
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override
    {
        const int whence = origin == SEEK_CUR   ? MIOPAN_SEEK_CUR
                           : origin == SEEK_END ? MIOPAN_SEEK_END
                                                : MIOPAN_SEEK_SET;
        return MioPan_FileSeek((MioPan_File *)file, offset, whence) == 0;
    }

    size_t Tell(Rml::FileHandle file) override
    {
        const int64_t position = MioPan_FileTell((MioPan_File *)file);
        return position < 0 ? 0 : (size_t)position;
    }

    size_t Length(Rml::FileHandle file) override
    {
        const int64_t length = MioPan_FileStreamSize((MioPan_File *)file);
        return length < 0 ? 0 : (size_t)length;
    }
};

class SystemInterface final : public Rml::SystemInterface
{
public:
    double GetElapsedTime() override
    {
        return (double)SDL_GetTicksNS() / 1e9;
    }

    /* Into miopan.log, which is where a broken document gets reported. */
    bool LogMessage(Rml::Log::Type type, const Rml::String &message) override
    {
        if (type != Rml::Log::LT_DEBUG)
        {
            SDL_Log("RmlUi: %s", message.c_str());
        }
        return true;
    }

    void SetClipboardText(const Rml::String &text) override
    {
        SDL_SetClipboardText(text.c_str());
    }

    void GetClipboardText(Rml::String &text) override
    {
        char *clipboard = SDL_GetClipboardText();
        text = clipboard != nullptr ? clipboard : "";
        SDL_free(clipboard);
    }

    void ActivateKeyboard(Rml::Vector2f caret_position,
                          float line_height) override
    {
        if (g_rml.window != nullptr)
        {
            const SDL_Rect area = {(int)caret_position.x + g_rml.frame.origin_x,
                                   (int)caret_position.y + g_rml.frame.origin_y,
                                   1, (int)line_height};
            SDL_SetTextInputArea(g_rml.window, &area, 0);
            SDL_StartTextInput(g_rml.window);
        }
    }

    void DeactivateKeyboard() override
    {
        if (g_rml.window != nullptr)
        {
            SDL_StopTextInput(g_rml.window);
        }
    }
};

/* Records rather than draws: see the header.  Clip masks, layers, filters and
 * shaders are left at RmlUi's defaults, which ignore them -- nothing the
 * menus need yet. */
class RenderInterface final : public Rml::RenderInterface
{
public:
    Rml::CompiledGeometryHandle
    CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                    Rml::Span<const int> indices) override
    {
        auto *geometry = new Geometry();
        geometry->vertices.assign(vertices.begin(), vertices.end());
        geometry->indices.assign(indices.begin(), indices.end());
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override
    {
        delete reinterpret_cast<Geometry *>(handle);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture_handle) override
    {
        const auto *geometry = reinterpret_cast<const Geometry *>(handle);
        if (geometry == nullptr || geometry->indices.empty())
        {
            return;
        }

        MioPanRml::DrawSnapshot &frame = g_rml.frame;
        SDL_GPUTexture *texture =
            texture_handle != 0 ? reinterpret_cast<SDL_GPUTexture *>(texture_handle)
                                : g_rml.white_texture;

        DrawCommand command;
        command.texture = texture;
        command.first_index = (Uint32)frame.indices.size();
        command.index_count = (Uint32)geometry->indices.size();
        command.vertex_offset = (Sint32)frame.vertices.size();
        command.scissor = g_rml.scissor_enabled;
        command.scissor_region = g_rml.scissor_region;
        command.transform = g_rml.transform;
        command.translation = translation;
        frame.commands.push_back(command);
        frame.vertices.insert(frame.vertices.end(), geometry->vertices.begin(),
                              geometry->vertices.end());
        frame.indices.insert(frame.indices.end(), geometry->indices.begin(),
                             geometry->indices.end());

        if (texture != g_rml.white_texture &&
            std::find(frame.textures.begin(), frame.textures.end(), texture) ==
                frame.textures.end())
        {
            frame.textures.push_back(texture);
        }
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &dimensions,
                                   const Rml::String &source) override
    {
        dimensions = {0, 0};

        /* A generated image arrives joined to the document's directory. */
        const size_t generated_at = source.find(MioPanRml::kGeneratedPrefix);
        if (generated_at != Rml::String::npos)
        {
            const auto image = g_rml.generated.find(source.substr(generated_at));
            if (image == g_rml.generated.end())
            {
                return 0;
            }
            SDL_GPUTexture *texture =
                CreateTexture(image->second.rgba.data(), image->second.width,
                              image->second.height, image->second.width * 4);
            if (texture != nullptr)
            {
                dimensions = {image->second.width, image->second.height};
                g_rml.textures[texture] = TextureRecord{0, false};
            }
            return reinterpret_cast<Rml::TextureHandle>(texture);
        }

        char host_path[1024];
        const int64_t size =
            MioPan_PathResolveAsset(source.c_str(), host_path, sizeof(host_path))
                ? MioPan_FileSize(host_path)
                : -1;
        if (size <= 0)
        {
            SDL_Log("MioPan RmlUi: no such image: %s", source.c_str());
            return 0;
        }
        std::vector<Uint8> bytes((size_t)size);
        if (!MioPan_FileReadAt(host_path, 0, bytes.data(), bytes.size()))
        {
            return 0;
        }

        /* BMP or PNG, by content. */
        SDL_Surface *surface = SDL_LoadSurface_IO(
            SDL_IOFromConstMem(bytes.data(), bytes.size()), true);
        SDL_Surface *rgba = surface != nullptr
                                ? SDL_ConvertSurface(surface,
                                                     SDL_PIXELFORMAT_RGBA32)
                                : nullptr;
        SDL_DestroySurface(surface);
        if (rgba == nullptr || !SDL_PremultiplySurfaceAlpha(rgba, false))
        {
            SDL_Log("MioPan RmlUi: could not decode %s: %s", source.c_str(),
                    SDL_GetError());
            SDL_DestroySurface(rgba);
            return 0;
        }

        SDL_GPUTexture *texture = CreateTexture((const Uint8 *)rgba->pixels,
                                                rgba->w, rgba->h, rgba->pitch);
        if (texture != nullptr)
        {
            dimensions = {rgba->w, rgba->h};
            g_rml.textures[texture] = TextureRecord{0, false};
        }
        SDL_DestroySurface(rgba);
        return reinterpret_cast<Rml::TextureHandle>(texture);
    }

    /* Font atlases and the like, already premultiplied. */
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i dimensions) override
    {
        if (source.size() < (size_t)dimensions.x * dimensions.y * 4)
        {
            return 0;
        }
        SDL_GPUTexture *texture =
            CreateTexture(source.data(), dimensions.x, dimensions.y,
                          dimensions.x * 4);
        if (texture != nullptr)
        {
            g_rml.textures[texture] = TextureRecord{0, false};
        }
        return reinterpret_cast<Rml::TextureHandle>(texture);
    }

    void ReleaseTexture(Rml::TextureHandle handle) override
    {
        auto *texture = reinterpret_cast<SDL_GPUTexture *>(handle);
        auto record = g_rml.textures.find(texture);
        if (record == g_rml.textures.end())
        {
            return;
        }
        record->second.released = true;
        if (record->second.refs <= 0)
        {
            SDL_ReleaseGPUTexture(g_rml.device, texture);
            g_rml.textures.erase(record);
        }
    }

    void EnableScissorRegion(bool enable) override
    {
        g_rml.scissor_enabled = enable;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        g_rml.scissor_region = region;
    }

    void SetTransform(const Rml::Matrix4f *transform) override
    {
        g_rml.transform =
            transform != nullptr ? *transform : Rml::Matrix4f::Identity();
    }
};

bool CreateGpuResources(SDL_GPUTextureFormat format)
{
    SDL_GPUVertexBufferDescription vertex_buffer{};
    vertex_buffer.slot = 0;
    vertex_buffer.pitch = sizeof(Rml::Vertex);
    vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0].location = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[0].offset = offsetof(Rml::Vertex, position);
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[1].offset = offsetof(Rml::Vertex, colour);
    attributes[2].location = 2;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(Rml::Vertex, tex_coord);

    /* Premultiplied alpha throughout: RmlUi's colours and atlases come that
     * way, and LoadTexture() premultiplies what it decodes. */
    SDL_GPUColorTargetDescription colour_target{};
    colour_target.format = format;
    colour_target.blend_state.enable_blend = true;
    colour_target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour_target.blend_state.dst_color_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colour_target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
    colour_target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    colour_target.blend_state.dst_alpha_blendfactor =
        SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    colour_target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = g_rml.vertex_shader;
    info.fragment_shader = g_rml.fragment_shader;
    info.vertex_input_state.vertex_buffer_descriptions = &vertex_buffer;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
    info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
    info.target_info.color_target_descriptions = &colour_target;
    info.target_info.num_color_targets = 1;
    g_rml.pipeline = SDL_CreateGPUGraphicsPipeline(g_rml.device, &info);
    if (g_rml.pipeline == nullptr)
    {
        SDL_Log("MioPan RmlUi: SDL_CreateGPUGraphicsPipeline failed: %s",
                SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo sampler{};
    sampler.min_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
    sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    /* Wrapping, as RmlUi's own backends sample: an image decorator with
     * `repeat` tiles by running its texture coordinates past 1. */
    sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    g_rml.sampler = SDL_CreateGPUSampler(g_rml.device, &sampler);
    if (g_rml.sampler == nullptr)
    {
        SDL_Log("MioPan RmlUi: SDL_CreateGPUSampler failed: %s",
                SDL_GetError());
        return false;
    }

    static const Uint8 kWhite[4] = {255, 255, 255, 255};
    g_rml.white_texture = CreateTexture(kWhite, 1, 1, 4);
    return g_rml.white_texture != nullptr;
}

/* Follow the game picture, and the scale of one dp with it. */
void UpdateContextGeometry()
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    MioPan_RendererGetPresentRect(&x, &y, &width, &height);
    if (width <= 0 || height <= 0)
    {
        /* Nothing presented yet. */
        x = 0;
        y = 0;
        SDL_GetWindowSizeInPixels(g_rml.window, &width, &height);
    }
    width = std::max(width, 1);
    height = std::max(height, 1);

    g_rml.frame.origin_x = x;
    g_rml.frame.origin_y = y;
    const Rml::Vector2i dimensions(width, height);
    if (g_rml.context->GetDimensions() != dimensions)
    {
        g_rml.context->SetDimensions(dimensions);
    }
    const float dp_ratio = std::min((float)width / kDesignWidth,
                                    (float)height / kDesignHeight);
    if (g_rml.context->GetDensityIndependentPixelRatio() != dp_ratio)
    {
        g_rml.context->SetDensityIndependentPixelRatio(dp_ratio);
    }
}

/* ---------------------------------------------------------------------------
 *  Input.
 * ------------------------------------------------------------------------ */

int KeyModifiers()
{
    const SDL_Keymod mod = SDL_GetModState();
    int modifiers = 0;
    if (mod & SDL_KMOD_CTRL)
    {
        modifiers |= Rml::Input::KM_CTRL;
    }
    if (mod & SDL_KMOD_SHIFT)
    {
        modifiers |= Rml::Input::KM_SHIFT;
    }
    if (mod & SDL_KMOD_ALT)
    {
        modifiers |= Rml::Input::KM_ALT;
    }
    if (mod & SDL_KMOD_GUI)
    {
        modifiers |= Rml::Input::KM_META;
    }
    if (mod & SDL_KMOD_NUM)
    {
        modifiers |= Rml::Input::KM_NUMLOCK;
    }
    if (mod & SDL_KMOD_CAPS)
    {
        modifiers |= Rml::Input::KM_CAPSLOCK;
    }
    return modifiers;
}

/* What menus and text fields use; anything else reaches RmlUi as unknown. */
Rml::Input::KeyIdentifier ToRmlKey(SDL_Keycode key)
{
    using namespace Rml::Input;
    if (key >= SDLK_A && key <= SDLK_Z)
    {
        return (KeyIdentifier)(KI_A + (key - SDLK_A));
    }
    if (key >= SDLK_0 && key <= SDLK_9)
    {
        return (KeyIdentifier)(KI_0 + (key - SDLK_0));
    }
    if (key >= SDLK_F1 && key <= SDLK_F12)
    {
        return (KeyIdentifier)(KI_F1 + (key - SDLK_F1));
    }
    switch (key)
    {
    case SDLK_UP: return KI_UP;
    case SDLK_DOWN: return KI_DOWN;
    case SDLK_LEFT: return KI_LEFT;
    case SDLK_RIGHT: return KI_RIGHT;
    case SDLK_RETURN: return KI_RETURN;
    case SDLK_KP_ENTER: return KI_NUMPADENTER;
    case SDLK_ESCAPE: return KI_ESCAPE;
    case SDLK_SPACE: return KI_SPACE;
    case SDLK_TAB: return KI_TAB;
    case SDLK_BACKSPACE: return KI_BACK;
    case SDLK_DELETE: return KI_DELETE;
    case SDLK_INSERT: return KI_INSERT;
    case SDLK_HOME: return KI_HOME;
    case SDLK_END: return KI_END;
    case SDLK_PAGEUP: return KI_PRIOR;
    case SDLK_PAGEDOWN: return KI_NEXT;
    default: return KI_UNKNOWN;
    }
}

/* The pad's buttons as keys, on the game's own terms: Cross confirms, and
 * Triangle -- FF2's back button -- Circle and START all back out, START
 * because it is what closes the pause screen.  L1 / R1 page between sections
 * and SELECT is Tab, the keyboard's SELECT.  The d-pad is navigation and goes
 * through SetNavHeld() instead. */
Rml::Input::KeyIdentifier PadKey(Uint8 button)
{
    using namespace Rml::Input;
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH: return KI_RETURN;
    case SDL_GAMEPAD_BUTTON_EAST:
    case SDL_GAMEPAD_BUTTON_NORTH:
    case SDL_GAMEPAD_BUTTON_START: return KI_ESCAPE;
    case SDL_GAMEPAD_BUTTON_BACK: return KI_TAB;
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return KI_PRIOR;
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return KI_NEXT;
    default: return KI_UNKNOWN;
    }
}

int PadDirection(Uint8 button)
{
    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 1;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return 2;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return 3;
    default: return -1;
    }
}

/* One source of one direction going down or up.  The key goes down when the
 * first source takes it and up when the last lets go, so the d-pad and the
 * stick together do not double a move. */
void SetNavHeld(int direction, bool from_dpad, bool held)
{
    bool &source = from_dpad ? g_rml.nav_dpad[direction]
                             : g_rml.nav_stick[direction];
    if (source == held)
    {
        return;
    }
    const bool was = g_rml.nav_dpad[direction] || g_rml.nav_stick[direction];
    source = held;
    const bool now = g_rml.nav_dpad[direction] || g_rml.nav_stick[direction];
    if (!was && now)
    {
        g_rml.context->ProcessKeyDown(kNavKeys[direction], 0);
        g_rml.nav_next_repeat[direction] = SDL_GetTicks() + kNavRepeatDelayMs;
    }
    else if (was && !now)
    {
        g_rml.context->ProcessKeyUp(kNavKeys[direction], 0);
    }
}

/* The left stick as four directions, the stronger axis winning so a diagonal
 * moves one way at a time. */
void UpdateStickNavigation()
{
    const float x = g_rml.stick_x;
    const float y = g_rml.stick_y;
    const bool horizontal = SDL_fabsf(x) > SDL_fabsf(y);
    const float along = horizontal ? x : y;
    for (int direction = 0; direction < 4; direction++)
    {
        const bool on_axis = (direction >= 2) == horizontal;
        const bool negative = direction == 0 || direction == 2;
        const bool toward = negative ? along < 0.0f : along > 0.0f;
        const float threshold =
            g_rml.nav_stick[direction] ? kStickRelease : kStickPress;
        SetNavHeld(direction, false,
                   on_axis && toward && SDL_fabsf(along) > threshold);
    }
}

void RepeatNavigation()
{
    const Uint64 now = SDL_GetTicks();
    for (int direction = 0; direction < 4; direction++)
    {
        if ((g_rml.nav_dpad[direction] || g_rml.nav_stick[direction]) &&
            now >= g_rml.nav_next_repeat[direction])
        {
            g_rml.context->ProcessKeyDown(kNavKeys[direction], 0);
            g_rml.nav_next_repeat[direction] = now + kNavRepeatIntervalMs;
        }
    }
}

/* Forget held directions without telling RmlUi: called when no menu wants the
 * controls, or the window has lost them. */
void ClearNavigation()
{
    for (int direction = 0; direction < 4; direction++)
    {
        g_rml.nav_dpad[direction] = false;
        g_rml.nav_stick[direction] = false;
    }
    g_rml.stick_x = 0.0f;
    g_rml.stick_y = 0.0f;
}

/* A held Enter must not click again and again. */
bool IsActivationKey(Rml::Input::KeyIdentifier key)
{
    return key == Rml::Input::KI_RETURN || key == Rml::Input::KI_NUMPADENTER ||
           key == Rml::Input::KI_SPACE || key == Rml::Input::KI_ESCAPE;
}

/* Window coordinates to context pixels. */
Rml::Vector2i ToContext(float x, float y)
{
    float density = SDL_GetWindowPixelDensity(g_rml.window);
    if (!(density > 0.0f))
    {
        density = 1.0f;
    }
    return {(int)std::lround(x * density) - g_rml.frame.origin_x,
            (int)std::lround(y * density) - g_rml.frame.origin_y};
}

void DispatchEvent(const SDL_Event &event)
{
    Rml::Context *context = g_rml.context;
    switch (event.type)
    {
    case SDL_EVENT_MOUSE_MOTION:
    {
        g_rml.using_pad = false;
        g_rml.using_mouse = true;
        const Rml::Vector2i mouse = ToContext(event.motion.x, event.motion.y);
        context->ProcessMouseMove(mouse.x, mouse.y, KeyModifiers());
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    {
        const int button = event.button.button == SDL_BUTTON_LEFT    ? 0
                           : event.button.button == SDL_BUTTON_RIGHT ? 1
                           : event.button.button == SDL_BUTTON_MIDDLE ? 2
                                                                     : -1;
        if (button < 0)
        {
            break;
        }
        g_rml.using_pad = false;
        g_rml.using_mouse = true;
        const Rml::Vector2i mouse = ToContext(event.button.x, event.button.y);
        context->ProcessMouseMove(mouse.x, mouse.y, KeyModifiers());
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN)
        {
            context->ProcessMouseButtonDown(button, KeyModifiers());
        }
        else
        {
            context->ProcessMouseButtonUp(button, KeyModifiers());
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL:
        context->ProcessMouseWheel(Rml::Vector2f(-event.wheel.x, -event.wheel.y),
                                   KeyModifiers());
        break;
    case SDL_EVENT_KEY_DOWN:
    {
        g_rml.using_pad = false;
        g_rml.using_mouse = false;
        const Rml::Input::KeyIdentifier key = ToRmlKey(event.key.key);
        if (!(event.key.repeat && IsActivationKey(key)))
        {
            context->ProcessKeyDown(key, KeyModifiers());
        }
        break;
    }
    case SDL_EVENT_KEY_UP:
        context->ProcessKeyUp(ToRmlKey(event.key.key), KeyModifiers());
        break;
    case SDL_EVENT_TEXT_INPUT:
        context->ProcessTextInput(Rml::String(event.text.text));
        break;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
    {
        g_rml.using_pad = true;
        g_rml.using_mouse = false;
        const int direction = PadDirection(event.gbutton.button);
        if (direction >= 0)
        {
            SetNavHeld(direction, true, event.gbutton.down);
            break;
        }
        const Rml::Input::KeyIdentifier key = PadKey(event.gbutton.button);
        if (key == Rml::Input::KI_UNKNOWN)
        {
            break;
        }
        if (event.gbutton.down)
        {
            context->ProcessKeyDown(key, 0);
        }
        else
        {
            context->ProcessKeyUp(key, 0);
        }
        break;
    }
    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
    {
        const float value = (float)event.gaxis.value / 32767.0f;
        if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX)
        {
            g_rml.stick_x = value;
        }
        else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY)
        {
            g_rml.stick_y = value;
        }
        else
        {
            break;
        }
        if (SDL_fabsf(value) > kStickPress)
        {
            g_rml.using_pad = true;
            g_rml.using_mouse = false;
        }
        UpdateStickNavigation();
        break;
    }
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        context->ProcessMouseLeave();
        break;
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        context->ProcessMouseLeave();
        ClearNavigation();
        break;
    default:
        break;
    }
}

/* The game reads nothing while a document is up.  A menu closes on a press,
 * and the input layer keeps that press from the game until it is let go
 * (MioPan_InputSetMenuSuppressed), so the hold ends with the document. */
void UpdateControlHold()
{
    const bool holding = MioPanRml::WantsInput();
    if (holding != g_rml.holding_controls)
    {
        g_rml.holding_controls = holding;
        MioPan_InputSetMenuSuppressed(holding ? 1 : 0);
    }
}

void ReleaseGpuResources()
{
    if (g_rml.device == nullptr)
    {
        return;
    }
    for (const auto &[texture, record] : g_rml.textures)
    {
        (void)record;
        SDL_ReleaseGPUTexture(g_rml.device, texture);
    }
    g_rml.textures.clear();
    if (g_rml.white_texture != nullptr)
    {
        SDL_ReleaseGPUTexture(g_rml.device, g_rml.white_texture);
    }
    if (g_rml.vertex_buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(g_rml.device, g_rml.vertex_buffer);
    }
    if (g_rml.index_buffer != nullptr)
    {
        SDL_ReleaseGPUBuffer(g_rml.device, g_rml.index_buffer);
    }
    if (g_rml.transfer_buffer != nullptr)
    {
        SDL_ReleaseGPUTransferBuffer(g_rml.device, g_rml.transfer_buffer);
    }
    if (g_rml.sampler != nullptr)
    {
        SDL_ReleaseGPUSampler(g_rml.device, g_rml.sampler);
    }
    if (g_rml.pipeline != nullptr)
    {
        SDL_ReleaseGPUGraphicsPipeline(g_rml.device, g_rml.pipeline);
    }
    if (g_rml.vertex_shader != nullptr)
    {
        SDL_ReleaseGPUShader(g_rml.device, g_rml.vertex_shader);
    }
    if (g_rml.fragment_shader != nullptr)
    {
        SDL_ReleaseGPUShader(g_rml.device, g_rml.fragment_shader);
    }
}

/* Stateless -- everything they touch is g_rml -- and static, so they outlive
 * the Rml::Shutdown() that still calls into them. */
SystemInterface g_system_interface;
RenderInterface g_render_interface;
FileInterface g_file_interface;
}

namespace MioPanRml
{
bool Init(SDL_Window *window, SDL_GPUDevice *device, SDL_GPUTextureFormat format,
          SDL_GPUShader *vertex_shader, SDL_GPUShader *fragment_shader)
{
    if (g_rml.context != nullptr)
    {
        return true;
    }

    g_rml.window = window;
    g_rml.device = device;
    g_rml.vertex_shader = vertex_shader;
    g_rml.fragment_shader = fragment_shader;
    if (window == nullptr || device == nullptr || vertex_shader == nullptr ||
        fragment_shader == nullptr ||
        format == SDL_GPU_TEXTUREFORMAT_INVALID || !CreateGpuResources(format))
    {
        SDL_Log("MioPan RmlUi: renderer resources unavailable; menus disabled");
        Shutdown();
        return false;
    }

    Rml::SetSystemInterface(&g_system_interface);
    Rml::SetRenderInterface(&g_render_interface);
    Rml::SetFileInterface(&g_file_interface);
    g_rml.rml_initialised = Rml::Initialise();
    if (!g_rml.rml_initialised)
    {
        SDL_Log("MioPan RmlUi: Rml::Initialise failed; menus disabled");
        Shutdown();
        return false;
    }

    for (const char *font : kFontFiles)
    {
        if (!Rml::LoadFontFace(font))
        {
            SDL_Log("MioPan RmlUi: could not load %s", font);
        }
    }

    int width = 1;
    int height = 1;
    SDL_GetWindowSizeInPixels(window, &width, &height);
    g_rml.context = Rml::CreateContext(
        "miopan", Rml::Vector2i(std::max(width, 1), std::max(height, 1)));
    if (g_rml.context == nullptr)
    {
        SDL_Log("MioPan RmlUi: Rml::CreateContext failed; menus disabled");
        Shutdown();
        return false;
    }
    UpdateContextGeometry();

    MioPanRmlOptions::Init(g_rml.context);
    MioPanRmlPause::Init(g_rml.context);
    MioPanRmlTitle::Init(g_rml.context);
    MioPanRmlLoad::Init(g_rml.context);
    MioPanRmlSave::Init(g_rml.context);
    MioPanRmlSavePoint::Init(g_rml.context);
    MioPanRmlSetup::Init(g_rml.context);
    return true;
}

void Shutdown()
{
    MioPanRmlSetup::Shutdown();
    MioPanRmlSavePoint::Shutdown();
    MioPanRmlSave::Shutdown();
    MioPanRmlLoad::Shutdown();
    MioPanRmlTitle::Shutdown();
    MioPanRmlPause::Shutdown();
    MioPanRmlOptions::Shutdown();
    if (g_rml.rml_initialised)
    {
        /* Releases every document's geometry and texture through the
         * interfaces. */
        Rml::Shutdown();
    }
    if (g_rml.holding_controls)
    {
        MioPan_InputSetMenuSuppressed(0);
    }
    ReleaseGpuResources();
    g_rml = RmlState();
}

void ProcessEvent(const SDL_Event *event)
{
    if (g_rml.context == nullptr || event == nullptr)
    {
        return;
    }
    if (WantsInput())
    {
        DispatchEvent(*event);
    }
    UpdateControlHold();
}

void Frame()
{
    if (g_rml.context == nullptr)
    {
        return;
    }

    UpdateContextGeometry();
    if (WantsInput())
    {
        RepeatNavigation();
    }
    else
    {
        ClearNavigation();
    }
    MioPanRmlOptions::Frame();
    MioPanRmlPause::Frame();
    MioPanRmlTitle::Frame();
    MioPanRmlLoad::Frame();
    MioPanRmlSave::Frame();
    MioPanRmlSavePoint::Frame();
    MioPanRmlSetup::Frame();
    g_rml.context->Update();

    DrawSnapshot &frame = g_rml.frame;
    std::vector<SDL_GPUTexture *> previous;
    previous.swap(frame.textures);
    frame.vertices.clear();
    frame.indices.clear();
    frame.commands.clear();
    g_rml.scissor_enabled = false;
    g_rml.transform = Rml::Matrix4f::Identity();
    g_rml.context->Render();
    Retain(frame.textures);
    Drop(previous);

    UpdateControlHold();
}

bool WantsInput()
{
    if (g_rml.context == nullptr)
    {
        return false;
    }
    /* Modal documents only: a non-modal one is decoration over the game --
     * a prompt, a hint -- and must not take the controls. */
    for (int i = 0; i < g_rml.context->GetNumDocuments(); i++)
    {
        Rml::ElementDocument *document = g_rml.context->GetDocument(i);
        if (document->IsVisible() && document->IsModal())
        {
            return true;
        }
    }
    return false;
}

bool UsingPad()
{
    return g_rml.using_pad;
}

bool PointerActive()
{
    return g_rml.using_mouse;
}

const char *PadButtonName(SDL_GamepadButton button)
{
    /* The opened pad's own lettering when there is one; the game's DUALSHOCK
     * names otherwise. */
    SDL_Gamepad *pad = nullptr;
    int count = 0;
    SDL_JoystickID *pads = SDL_GetGamepads(&count);
    for (int i = 0; pads != nullptr && i < count && pad == nullptr; i++)
    {
        pad = SDL_GetGamepadFromID(pads[i]);
    }
    SDL_free(pads);

    bool playstation = true;
    if (pad != nullptr)
    {
        switch (SDL_GetGamepadButtonLabel(pad, button))
        {
        case SDL_GAMEPAD_BUTTON_LABEL_A: return "A";
        case SDL_GAMEPAD_BUTTON_LABEL_B: return "B";
        case SDL_GAMEPAD_BUTTON_LABEL_X: return "X";
        case SDL_GAMEPAD_BUTTON_LABEL_Y: return "Y";
        case SDL_GAMEPAD_BUTTON_LABEL_CROSS: return "Cross";
        case SDL_GAMEPAD_BUTTON_LABEL_CIRCLE: return "Circle";
        case SDL_GAMEPAD_BUTTON_LABEL_SQUARE: return "Square";
        case SDL_GAMEPAD_BUTTON_LABEL_TRIANGLE: return "Triangle";
        default: break;
        }
        const SDL_GamepadType type = SDL_GetGamepadType(pad);
        playstation = type == SDL_GAMEPAD_TYPE_PS3 ||
                      type == SDL_GAMEPAD_TYPE_PS4 ||
                      type == SDL_GAMEPAD_TYPE_PS5;
    }

    switch (button)
    {
    case SDL_GAMEPAD_BUTTON_SOUTH: return "Cross";
    case SDL_GAMEPAD_BUTTON_EAST: return "Circle";
    case SDL_GAMEPAD_BUTTON_WEST: return "Square";
    case SDL_GAMEPAD_BUTTON_NORTH: return "Triangle";
    case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return playstation ? "L1" : "LB";
    case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return playstation ? "R1" : "RB";
    case SDL_GAMEPAD_BUTTON_START: return playstation ? "START" : "Menu";
    case SDL_GAMEPAD_BUTTON_BACK: return playstation ? "SELECT" : "View";
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return "Left";
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return "Right";
    default: return "?";
    }
}

Rml::Context *GetContext()
{
    return g_rml.context;
}

SDL_Window *GetWindow()
{
    return g_rml.window;
}

void SetGeneratedImage(const std::string &name, const Uint8 *rgba, int width,
                       int height)
{
    if (rgba == nullptr || width <= 0 || height <= 0)
    {
        return;
    }
    auto &image = g_rml.generated[name];
    image.width = width;
    image.height = height;
    image.rgba.assign(rgba, rgba + (size_t)width * height * 4);

    /* Premultiplied, as LoadTexture() leaves what it decodes. */
    for (size_t i = 0; i < image.rgba.size(); i += 4)
    {
        const unsigned alpha = image.rgba[i + 3];
        for (size_t c = 0; c < 3; c++)
        {
            image.rgba[i + c] = (Uint8)(image.rgba[i + c] * alpha / 255);
        }
    }
}

bool HasGeneratedImage(const std::string &name)
{
    return g_rml.generated.find(name) != g_rml.generated.end();
}

bool PrepareDrawData(const DrawSnapshot *snapshot,
                     SDL_GPUCommandBuffer *command_buffer)
{
    const DrawSnapshot &list = snapshot != nullptr ? *snapshot : g_rml.frame;
    if (g_rml.pipeline == nullptr || command_buffer == nullptr ||
        list.commands.empty())
    {
        return false;
    }

    const Uint32 vertex_bytes =
        (Uint32)(list.vertices.size() * sizeof(Rml::Vertex));
    const Uint32 index_bytes = (Uint32)(list.indices.size() * sizeof(int));
    if (!EnsureBuffer(&g_rml.vertex_buffer, &g_rml.vertex_capacity,
                      SDL_GPU_BUFFERUSAGE_VERTEX, vertex_bytes) ||
        !EnsureBuffer(&g_rml.index_buffer, &g_rml.index_capacity,
                      SDL_GPU_BUFFERUSAGE_INDEX, index_bytes) ||
        !EnsureTransferBuffer(vertex_bytes + index_bytes))
    {
        return false;
    }

    /* Cycled, so a present still in flight keeps the bytes it was given. */
    auto *staging = (Uint8 *)SDL_MapGPUTransferBuffer(
        g_rml.device, g_rml.transfer_buffer, true);
    if (staging == nullptr)
    {
        return false;
    }
    std::memcpy(staging, list.vertices.data(), vertex_bytes);
    std::memcpy(staging + vertex_bytes, list.indices.data(), index_bytes);
    SDL_UnmapGPUTransferBuffer(g_rml.device, g_rml.transfer_buffer);

    SDL_GPUCopyPass *copy = SDL_BeginGPUCopyPass(command_buffer);
    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = g_rml.transfer_buffer;
    SDL_GPUBufferRegion destination{};
    destination.buffer = g_rml.vertex_buffer;
    destination.size = vertex_bytes;
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    source.offset = vertex_bytes;
    destination.buffer = g_rml.index_buffer;
    destination.size = index_bytes;
    SDL_UploadToGPUBuffer(copy, &source, &destination, true);
    SDL_EndGPUCopyPass(copy);
    return true;
}

void RenderDrawData(const DrawSnapshot *snapshot,
                    SDL_GPUCommandBuffer *command_buffer,
                    SDL_GPURenderPass *render_pass, Uint32 width,
                    Uint32 height)
{
    const DrawSnapshot &list = snapshot != nullptr ? *snapshot : g_rml.frame;
    if (g_rml.pipeline == nullptr || command_buffer == nullptr ||
        render_pass == nullptr || list.commands.empty() || width == 0 ||
        height == 0)
    {
        return;
    }

    SDL_BindGPUGraphicsPipeline(render_pass, g_rml.pipeline);
    const SDL_GPUViewport viewport = {0.0f, 0.0f, (float)width, (float)height,
                                      0.0f, 1.0f};
    SDL_SetGPUViewport(render_pass, &viewport);
    const SDL_GPUBufferBinding vertices = {g_rml.vertex_buffer, 0};
    SDL_BindGPUVertexBuffers(render_pass, 0, &vertices, 1);
    const SDL_GPUBufferBinding indices = {g_rml.index_buffer, 0};
    SDL_BindGPUIndexBuffer(render_pass, &indices,
                           SDL_GPU_INDEXELEMENTSIZE_32BIT);

    /* Context pixels, y down, to the target's clip space. */
    const float w = (float)width;
    const float h = (float)height;
    const Rml::Matrix4f projection = Rml::Matrix4f::FromRows(
        Rml::Vector4f(2.0f / w, 0.0f, 0.0f, 2.0f * list.origin_x / w - 1.0f),
        Rml::Vector4f(0.0f, -2.0f / h, 0.0f, 1.0f - 2.0f * list.origin_y / h),
        Rml::Vector4f(0.0f, 0.0f, 1.0f, 0.0f),
        Rml::Vector4f(0.0f, 0.0f, 0.0f, 1.0f));

    const SDL_Rect whole = {0, 0, (int)width, (int)height};
    for (const DrawCommand &command : list.commands)
    {
        SDL_Rect scissor = whole;
        if (command.scissor)
        {
            const SDL_Rect region = {
                list.origin_x + command.scissor_region.Left(),
                list.origin_y + command.scissor_region.Top(),
                command.scissor_region.Width(),
                command.scissor_region.Height()};
            if (!SDL_GetRectIntersection(&region, &whole, &scissor))
            {
                continue;
            }
        }
        SDL_SetGPUScissor(render_pass, &scissor);

        const SDL_GPUTextureSamplerBinding binding = {command.texture,
                                                      g_rml.sampler};
        SDL_BindGPUFragmentSamplers(render_pass, 0, &binding, 1);

        VertexUniforms uniforms{};
        const Rml::Matrix4f transform = projection * command.transform;
        std::memcpy(uniforms.transform, transform.data(),
                    sizeof(uniforms.transform));
        uniforms.translation[0] = command.translation.x;
        uniforms.translation[1] = command.translation.y;
        SDL_PushGPUVertexUniformData(command_buffer, 0, &uniforms,
                                     sizeof(uniforms));

        SDL_DrawGPUIndexedPrimitives(render_pass, command.index_count, 1,
                                     command.first_index,
                                     command.vertex_offset, 0);
    }
}

void CaptureDrawData(DrawSnapshot **snapshot)
{
    if (snapshot == nullptr)
    {
        return;
    }
    if (*snapshot == nullptr)
    {
        *snapshot = new DrawSnapshot();
    }

    DrawSnapshot &out = **snapshot;
    std::vector<SDL_GPUTexture *> previous;
    previous.swap(out.textures);
    out.vertices = g_rml.frame.vertices;
    out.indices = g_rml.frame.indices;
    out.commands = g_rml.frame.commands;
    out.textures = g_rml.frame.textures;
    out.origin_x = g_rml.frame.origin_x;
    out.origin_y = g_rml.frame.origin_y;
    Retain(out.textures);
    Drop(previous);
}

void FreeDrawData(DrawSnapshot *snapshot)
{
    if (snapshot == nullptr)
    {
        return;
    }
    Drop(snapshot->textures);
    delete snapshot;
}
}
