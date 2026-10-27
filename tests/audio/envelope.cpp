#include "../../zero_rom/zero2np/src/sdk/iop_voice.cpp"
#include "../../zero_rom/zero2np/src/sdk/iop_libsd.cpp"
#undef NDEBUG
#undef printf
#include <cassert>
#include <cstdio>
extern "C" SDL_Mutex *SDL_CreateMutex(void) { return reinterpret_cast<SDL_Mutex *>(1); }
extern "C" void SDL_LockMutex(SDL_Mutex *) {}
extern "C" void SDL_UnlockMutex(SDL_Mutex *) {}
extern "C" int MioPan_LogPrintf(const char *, ...) { return 0; }
extern "C" void MioPan_ReverbSetEnable(int, int) {}
static unsigned Duration(bool attack, unsigned release)
{
    IopVoice v{};
    v.pair = -1; v.playing = 1;
    v.adsr1 = 0x80ff; v.adsr2 = release;
    v.adsr_phase = attack ? VOICE_ADSR_ATTACK : VOICE_ADSR_RELEASE;
    v.adsr_level = attack ? 0 : ADSR_LEVEL_MAX;
    unsigned n = 0;
    while ((attack ? v.adsr_phase == VOICE_ADSR_ATTACK : v.adsr_phase != VOICE_ADSR_OFF) && n < 480000)
    { GetVoiceAdsrLevel(&v); ++n; }
    return n;
}
int main()
{
    assert(Duration(true, 0x5fe3) == 6);
    assert(Duration(false, 0x5fc0) == 2);
    assert(Duration(false, 0x5fe0) == 15);
    assert(Duration(false, 0x5fe3) == 128);
    IopVoice v{};
    v.pair = -1; v.playing = 1;
    v.adsr1 = 0x80ff; v.adsr2 = 0x5fe3;
    v.adsr_phase = VOICE_ADSR_SUSTAIN; v.adsr_level = ADSR_LEVEL_MAX;
    for (int n = 0; n < 40 * VOICE_SAMPLE_RATE; ++n) GetVoiceAdsrLevel(&v);
    assert(v.playing && v.adsr_phase == VOICE_ADSR_SUSTAIN && v.adsr_level > 32000);
    v.adsr_counter = 123;
    KeyOffUnlocked(&v); assert(v.adsr_counter == 0);
    for (int n = 0; n < 200; ++n) GetVoiceAdsrLevel(&v);
    assert(!v.playing && v.adsr_phase == VOICE_ADSR_OFF);
    // Sustain direction is bit 14; bit 13 belongs to its rate.
    v.playing = 1; v.adsr_phase = VOICE_ADSR_SUSTAIN;
    v.adsr2 = 0x2000; v.adsr_level = 8192; v.adsr_counter = 0;
    assert(GetVoiceAdsrLevel(&v) > 8192);
    v.adsr2 = 0x4000; v.adsr_level = 20000; v.adsr_counter = 0;
    assert(GetVoiceAdsrLevel(&v) < 20000);
    // Key-off during a silent attack must not restart at full volume.
    v.playing = 1; v.adsr_level = 0; v.adsr_phase = VOICE_ADSR_ATTACK;
    KeyOffUnlocked(&v); assert(GetVoiceAdsrLevel(&v) == 0 && !v.playing);
    // Zero registers are a valid envelope rather than an envelope bypass.
    v.playing = 1; v.adsr1 = v.adsr2 = 0;
    v.adsr_level = 0; v.adsr_phase = VOICE_ADSR_ATTACK;
    assert(GetVoiceAdsrLevel(&v) > 0 && v.adsr_level < ADSR_LEVEL_MAX);
    voice_ready = 1; audio_dev = 1;
    out_stream = reinterpret_cast<SDL_AudioStream *>(1);
    v.adsr_counter = 123; v.adsr_level = 42;
    KeyOnUnlocked(&v);
    assert(v.adsr_counter == 0 && v.adsr_level == 0 && v.adsr_phase == VOICE_ADSR_ATTACK);
    assert(ApplyEnvelope(32767, 32767) == 32766);
    puts("Register attack/release timing, 40-second sustain, key-off and envelope gain passed");
}
