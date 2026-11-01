#pragma once
#include <vector>
namespace MioPanFonts
{
struct Image
{
    int width = 0, height = 0;
    std::vector<unsigned char> rgba;
};
// Fingerprint the original font packs once per launch, before gameplay.
void Prepare();
int CurrentSet();
bool LoadOverride(int bank, int width, int height, Image &image);
}
