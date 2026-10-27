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
static int hits;
static void Interrupt(int bits, void *)
{
 ++hits;
 for(int c=0;c<2;++c)if(bits&(1<<c))sd_core[c].attr[SD_C_IRQ_ENABLE>>1]=0;
}
static void Tick(int n=1){while(n--)MixVoices();}
int main()
{
 for(int i=0;i<IOP_VOICE_NUM;++i){voices[i]={};voices[i].index=i;voices[i].pair=-1;voices[i].block_pos=ADPCM_BLOCK_SAMPLES;}
 sd_spu2_handler=reinterpret_cast<void *>(Interrupt);
 auto &v=voices[2];const unsigned base=0x10000;v.lsa=v.nax=base;v.pitch=PITCH_ONE;
 sd_ram[base+4096-16+1]=3;
 sceSdSetAddr(SD_A_IRQA,base+2048);sceSdSetCoreAttr(SD_C_IRQ_ENABLE,1);
 Tick(28);assert(hits==1);assert(v.nax==base);assert(!v.playing&&v.block_pos==ADPCM_BLOCK_SAMPLES);
 // Late IRQ arming must still be reached on the next silent buffer traversal.
 sceSdSetCoreAttr(SD_C_IRQ_ENABLE,1);Tick(28);assert(hits==2&&v.nax==base);
 // A later LSAX write is followed at the next loop end.
 v.lsa=0x7000;sd_ram[0x7001]=7;Tick(28);assert(v.nax==0x7000);
 sceSdSetAddr(SD_A_IRQA,0x7000);sceSdSetCoreAttr(SD_C_IRQ_ENABLE,1);Tick();assert(hits==3);
 sceSdSetAddr(SD_A_IRQA,0x8000);sceSdSetCoreAttr(SD_C_IRQ_ENABLE,1);Tick();assert(hits==3);
 sceSdSetCoreAttr(SD_C_IRQ_ENABLE,0);sceSdSetAddr(SD_A_IRQA,0x7000);Tick();assert(hits==3);
 sceSdSetAddr(SD_A_IRQA|1,0x7000);sceSdSetCoreAttr(SD_C_IRQ_ENABLE|1,1);Tick();assert(hits==4);
 // Silent reads wrap within the 2 MiB SPU address space.
 v.nax=0x1ffff0;v.header=0;AdvanceVoiceBlock(&v);assert(v.nax==0);
 v.nax=v.lsa;FillAdpcmHeader(&v);
 // Pitch zero leaves the address unchanged; idle voices never produce PCM.
 v.pitch=0;Tick(28);assert(v.nax==0x7000);
 for(int c=0;c<IOP_VOICE_CORES;++c)for(int f=0;f<MIX_BLOCK_FRAMES;++f)assert(!acc_dry_l[c][f]&&!acc_dry_r[c][f]&&!acc_wet_l[c][f]&&!acc_wet_r[c][f]);
 puts("Silent voice: sample-clock traversal, looping, late IRQs, LSAX changes, disabled/mismatched IRQs and zero PCM passed");
}
