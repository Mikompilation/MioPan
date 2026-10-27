#include "../../zero_rom/zero2np/src/sdk/iop_voice.cpp"
#undef NDEBUG
#undef printf
#include <cassert>
#include <cstdint>
#include <cstdio>
extern "C" SDL_Mutex *SDL_CreateMutex(void) { return nullptr; }
int main()
{
    for (unsigned word = 0; word < 65536; ++word)
    {
        const int expected = word & 0x8000 ? 0 : static_cast<int16_t>(word << 1);
        assert(DecodeVolume(word) == expected);
    }
    assert(DecodeVolume(0x3fff) == 32766);
    assert(DecodeVolume(0x4000) == -32768);
    assert(DecodeVolume(0x7fff) == -2);
    assert(ApplyVolume(ApplyVolume(16000, DecodeVolume(0x3fff)), DecodeVolume(0x3fff)) == 15998);
    puts("All volume words, signed phase and combined voice/master gain passed");
}
