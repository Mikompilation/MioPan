#ifndef MIOPAN_UI_SAVE_PREVIEW_H
#define MIOPAN_UI_SAVE_PREVIEW_H
#include "miopan/save/miopan_save_transfer.h"
#include <imgui.h>
struct SDL_GPUDevice;
namespace MioPanUiSavePreview
{
void Init(SDL_GPUDevice *device);
void Shutdown();
void Draw(const MioPanSave::SlotInfo &slot, ImVec2 size);
// Pure TIM2 decoding; previews never upload into the game's GS memory.
bool Decode(const MioPanSave::Bytes &file, int &width, int &height, MioPanSave::Bytes &rgba);
} // namespace MioPanUiSavePreview
#endif
