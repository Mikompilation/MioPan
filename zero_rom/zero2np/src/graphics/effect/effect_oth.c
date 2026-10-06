// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_oth.c
//
// COMPLETE.  effect_oth.o is .text 0x00151cd8..0x0015bf50 (0xa280) -- 101
// functions, 64 of them ZERO2.MAP exports, across ten sub-systems: the candle
// flame, the massed candles, the halo, the heat haze, the item glint, the
// torch, the dust and leaves, the haze volumes, the storm and the door seal.
// All 64 exports and all 101 functions.txt entries are implemented; the two
// entries verify.py still reports missing are the compiler's own
// `global constructors keyed to HazeParameter` boilerplate.
//
// All static data is diffed byte-for-byte against the ROM out of the compiled
// .obj -- the three HAZE_PARAMETERs, CandlePolyDat, and both
// CandleFlameScaleData tables.  Every .rodata blob copied into a stack frame
// is written here as the local array initialiser it is, not as a file-scope
// table; globals.txt is right to list none of them.
//
// PORT DEVIATIONS, all flagged at the site:
//
//   - draw_distortion_particles() and draw_distortion_particles2() are ~80
//     instructions of VU0 macro-mode in the ROM.  There is no VU0 here, so the
//     arithmetic is written out scalar.  Two blocks of that VU0 program are
//     dead in the ROM (a screen-space bounding-box clamp and a 1/64-scaled copy
//     of the half-extents) and survive only as comments.
//   - the DIRECT GIF packets every draw builds are inert in this port, so each
//     one queues the same geometry through a renderer bridge alongside.  The
//     one thing with no bridge is draw_distortion_particles()'s *textured*
//     case, which samples the framebuffer per particle: types 2, 3 and 4 of
//     the heat haze are therefore invisible here, though their return value --
//     which is what the callers use for brightness -- is still right.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84).

#include "effect_oth.h"

#include <string.h>                             /* memset */

#include "../graph3d/gra3d.h"
#include "../graph3d/g3dxVu0.h"
#include "../../sdk/libvu0.h"
#include "../../system/eeiop/snd_util.h"        /* snd_utilAutoBDPlay */
#include "../../system/eeiop/stream_auto.h"     /* StreamAutoPreload / FadeOut */
#include "../../system/pad/pad.h"               /* VibrateRequest1 */
#include "../../common/SingleLinkList.h"
#include "../../common/packfile.h"             /* Pk2GetNum / Pk2GetAddr */
#include "../../system/os/eecdvd.h"            /* LoadReq / IsLoadEnd */
#include "../graph2d/g2d_draw.h"               /* Q_WORDDATA / StartDmaDirectTrans */
#include "../graph2d/graph2d.h"                /* SPRT_DAT effdat[] */
#include "../graphics.h"                       /* RendererPacket3D */
#include "effect_sub.h"                        /* Get2PosRot */
#include "effect_spr.h"                        /* SetEffSQITex */
#include "effect_obj.h"                        /* SetPartsDeform */
#include "effect_scr.h"                        /* SubDither3 */
#include "../../ingame/plyr/unit_ctl.h"          /* GetTrgtRot */
#include "effect_pak.h"                        /* Reserve2DPacket */
#include "../../miopan/rendering/miopan_renderer.h"  /* host draw bridges */

SINGLE_LINK_LIST ItemEffectList;                                /* data 2fc6f0 */
MANY_CANDLE_LOAD_CTRL ManyCandleLoadCtrl;                       /* data 2fc6b0 */

/* Suppresses every haze volume's drawing without tearing any down. */
static int haze_stop;                                           /* sdata 3efe6c */
int stop_lf;                                                    /* sdata 3efe68 */

/* The amulet fire's latches.  InitEffectOth() seeds both at -1 ("not
 * running"); CallAmuletFire() drops them to 0 to arm it. */
static void *amulet_fire_ret;                                   /* sbss 3f4bd4 */
static int amulet_fire_flow;                                    /* sbss 3f4bd8 */
static int amulet_fire_cnt;                                     /* sbss 3f4bdc */

/* The five "this pool has been reset since the last room load" latches.  Each
 * ContXxx() clears its own on the first frame it runs, which is what makes a
 * pool that survived a room change start empty rather than mid-flight. */
static int init_pond;                                           /* sbss 3f4bc0 */
static int init_hhaze;                                          /* sbss 3f4bc4 */
static int init_newitem;                                        /* sbss 3f4bc8 */
static int init_torch;                                          /* sbss 3f4bcc */
static int init_smoke;                                          /* sbss 3f4bd0 */

/* The shared particle pools.  Four for ghosts, one for the amulet and five for
 * torches; GetEnePartAddr / GetAmuPartAddr / GetTorchPartAddr hand one out and
 * stamp it with the caller's EFFECT_CONT address, so a pool belongs to whoever
 * claimed it until that effect ends. */
static fixed_array<HEAT_HAZE, 4> ene_particle;                  /* bss 43f610 */
static fixed_array<HEAT_HAZE, 1> amu_particle;                  /* bss 44f050 */
static fixed_array<HEAT_HAZE, 5> torch_particle;                /* bss 452ee0 */

/* Six drifts of leaves, sixteen leaves each. */
static fixed_array<EFF_LEAF, 6> eff_leaf;                       /* bss 43d750 */

THUNDER_LIGHT_CTRL ThunderLightCtrl;                            /* data 2fc700 */

/* ==========================================================================
 *  The three authored haze volumes.
 *
 *  Speeds and offsets are stored biased: the Speed* and AllSpeed* fields sit
 *  around 1000 and the Offset* around 10000, so the neutral value is the bias
 *  rather than zero.  Frequency is how often a particle is spawned, and
 *  AreaRadius / MaxY / MinY the box they are spawned in.
 *
 *  Kusabi's and Sae's are the same volume with different colours -- 47/44/43
 *  against 10/255/255 -- and Sae swaps AlphaBlendA and B, which is what makes
 *  hers a bright cyan wash rather than a dark smear.  Both drift with every
 *  AllSpeed at the neutral 1000, i.e. not at all; only the room haze drifts.
 * ======================================================================== */
HAZE_PARAMETER HazeParameter =                     /* data 2fbc20 */
{
      8414,  10461,   8614,    /* AreaRadius, MaxY, MinY */
        20,   1100,    920,    /* Frequency, SpeedXMax, SpeedXMin */
      1111,    917,   1046,    /* SpeedYMax, SpeedYMin, SpeedZMax */
       956,   1000,    992,    /* SpeedZMin, AllSpeedX_1, AllSpeedY_1 */
      1141,   1000,    955,    /* AllSpeedZ_1, AllSpeedX_2, AllSpeedY_2 */
       916,   1003,    960,    /* AllSpeedZ_2, AllSpeedX_3, AllSpeedY_3 */
      1140,     95,     95,    /* AllSpeedZ_3, AllSpeedTime_1, AllSpeedTime_2 */
        95,     20,    124,    /* AllSpeedTime_3, Alpha, AlphaInTime */
       576,    133,    510,    /* AlphaKeepTime, AlphaOutTime, StartScale */
       556,     48,     48,    /* EndScale, R, G */
        48,     36,     30,    /* B, RotZMax, RotZMin */
       239,      0,      1,    /* RotZTime, AlphaBlendA, AlphaBlendB */
         0,      1,      0,    /* AlphaBlendC, AlphaBlendD, AlphaBlendFIX */
     10000,  10000,  10000,    /* OffsetX, OffsetY, OffsetZ */
};

HAZE_PARAMETER KusabiHazeParameter =               /* data 2fbcc8 */
{
       463,  10037,   8998,    /* AreaRadius, MaxY, MinY */
        42,   1254,    757,    /* Frequency, SpeedXMax, SpeedXMin */
      1000,    850,   1050,    /* SpeedYMax, SpeedYMin, SpeedZMax */
       950,   1000,   1000,    /* SpeedZMin, AllSpeedX_1, AllSpeedY_1 */
      1000,   1000,   1000,    /* AllSpeedZ_1, AllSpeedX_2, AllSpeedY_2 */
      1000,   1000,   1000,    /* AllSpeedZ_2, AllSpeedX_3, AllSpeedY_3 */
      1000,    100,    100,    /* AllSpeedZ_3, AllSpeedTime_1, AllSpeedTime_2 */
       100,    103,      9,    /* AllSpeedTime_3, Alpha, AlphaInTime */
         0,    111,    239,    /* AlphaKeepTime, AlphaOutTime, StartScale */
       480,     47,     44,    /* EndScale, R, G */
        43,     36,     30,    /* B, RotZMax, RotZMin */
       239,      0,      2,    /* RotZTime, AlphaBlendA, AlphaBlendB */
         0,      1,      0,    /* AlphaBlendC, AlphaBlendD, AlphaBlendFIX */
     10000,  10226,  10160,    /* OffsetX, OffsetY, OffsetZ */
};

HAZE_PARAMETER SaeHazeParameter =                  /* data 2fbd70 */
{
       463,   9946,   9154,    /* AreaRadius, MaxY, MinY */
        42,   1254,    757,    /* Frequency, SpeedXMax, SpeedXMin */
      1000,    850,   1050,    /* SpeedYMax, SpeedYMin, SpeedZMax */
       950,   1000,   1000,    /* SpeedZMin, AllSpeedX_1, AllSpeedY_1 */
      1000,   1000,   1000,    /* AllSpeedZ_1, AllSpeedX_2, AllSpeedY_2 */
      1000,   1000,   1000,    /* AllSpeedZ_2, AllSpeedX_3, AllSpeedY_3 */
      1000,    100,    100,    /* AllSpeedZ_3, AllSpeedTime_1, AllSpeedTime_2 */
       100,    103,      9,    /* AllSpeedTime_3, Alpha, AlphaInTime */
         0,    111,    239,    /* AlphaKeepTime, AlphaOutTime, StartScale */
       480,     10,    255,    /* EndScale, R, G */
       255,     36,     30,    /* B, RotZMax, RotZMin */
       239,      1,      0,    /* RotZTime, AlphaBlendA, AlphaBlendB */
         0,      1,      0,    /* AlphaBlendC, AlphaBlendD, AlphaBlendFIX */
     10000,  10226,  10160,    /* OffsetX, OffsetY, OffsetZ */
};

/* The door-seal dissolve's singleton control block. */
DOOR_SEAL_DISAPPEAR_CTRL DoorSealDisappearCtrl;                 /* data 2fbe20 */

/* The candle flame's quad, in flame-local units: 1.93 wide and 2.07 tall,
 * with its origin a little below the base so the flame sits on the wick. */
static float CandlePolyDat[4][4] =                              /* data 2fbf30 */
{
    { 0.965643f, -0.136467f, 0.0f, 1.0f },
    { 0.965643f, 1.93681788f, 0.0f, 1.0f },
    { -0.965643f, -0.136467f, 0.0f, 1.0f },
    { -0.965643f, 1.93681788f, 0.0f, 1.0f },
};

/* The flame's width/height for each frame of its animation.  Read by
 * CandleFlameScaleWGet/HGet, which pick the table by flame type. */
float CandleFlameScaleData[150][2] =  /* data 2fbf70 */
{
    { 0.999935f, 1.0001359f }, { 1.0f, 1.00034499f }, { 1.0f, 1.00337696f },
    { 1.0f, 1.00915098f }, { 1.0f, 1.01719499f }, { 1.0f, 1.02704096f },
    { 1.0f, 1.03821599f }, { 1.0f, 1.05025089f }, { 1.0f, 1.05025089f },
    { 1.0f, 1.07501698f }, { 1.0f, 1.08680797f }, { 1.0f, 1.0975759f },
    { 1.0f, 1.10685194f }, { 1.0f, 1.11416495f }, { 1.0f, 1.11904395f },
    { 1.0f, 1.12101889f }, { 1.00000298f, 1.11958098f }, { 1.00022793f, 1.11181498f },
    { 1.00072896f, 1.09727299f }, { 1.00139499f, 1.07756698f }, { 1.00211692f, 1.05431294f },
    { 1.00278294f, 1.02912498f }, { 1.0032829f, 1.00361693f }, { 1.0035069f, 0.979404f },
    { 1.00334394f, 0.958099961f }, { 1.002684f, 0.941319942f }, { 1.00141597f, 0.93067795f },
    { 0.999438941f, 0.927556f }, { 0.996881962f, 0.927985966f }, { 0.993871f, 0.929917f },
    { 0.990464f, 0.933231f }, { 0.986722946f, 0.937809944f }, { 0.982706964f, 0.943536f },
    { 0.978476f, 0.950292945f }, { 0.97409f, 0.957962f }, { 0.969607949f, 0.966425955f },
    { 0.965091944f, 0.975567f }, { 0.960601f, 0.985267f }, { 0.956194f, 0.995408952f },
    { 0.951932967f, 1.00587595f }, { 0.947877f, 1.01654899f }, { 0.944084942f, 1.02731097f },
    { 0.940618f, 1.03804493f }, { 0.937535942f, 1.04863191f }, { 0.934899f, 1.05895495f },
    { 0.932767f, 1.06889689f }, { 0.931198955f, 1.07833898f }, { 0.930256963f, 1.087165f },
    { 0.929999f, 1.09525597f }, { 0.930542f, 1.1033479f }, { 0.931908965f, 1.11220896f },
    { 0.934017956f, 1.12171888f }, { 0.936786f, 1.1317569f }, { 0.940130949f, 1.14220691f },
    { 0.943968952f, 1.1529479f }, { 0.948218942f, 1.16386199f }, { 0.952797f, 1.17482793f },
    { 0.957621f, 1.18572891f }, { 0.962608f, 1.19644392f }, { 0.967676f, 1.20685494f },
    { 0.972741961f, 1.21684289f }, { 0.977722943f, 1.22628796f }, { 0.982538f, 1.2350719f },
    { 0.987102f, 1.24307489f }, { 0.991333961f, 1.25017893f }, { 0.99514997f, 1.2562629f },
    { 0.998469f, 1.26120889f }, { 1.00120699f, 1.26489794f }, { 1.00328195f, 1.26720989f },
    { 1.00461197f, 1.2680279f }, { 1.00511289f, 1.26722991f }, { 1.00470793f, 1.26466691f },
    { 1.00345993f, 1.25961697f }, { 1.00146294f, 1.25201297f }, { 0.998780966f, 1.24211395f },
    { 0.995478f, 1.23017693f }, { 0.991615f, 1.21646094f }, { 0.987257957f, 1.2012229f },
    { 0.98247f, 1.18472099f }, { 0.977314f, 1.16721296f }, { 0.971854f, 1.14895689f },
    { 0.966154f, 1.13021195f }, { 0.960275948f, 1.11123395f }, { 0.954283953f, 1.09228289f },
    { 0.948242962f, 1.07361495f }, { 0.942213953f, 1.05548799f }, { 0.936262965f, 1.03816199f },
    { 0.930452f, 1.02189291f }, { 0.924845f, 1.00693893f }, { 0.919505954f, 0.993558943f },
    { 0.914496958f, 0.982009947f }, { 0.909882963f, 0.97255f }, { 0.905726969f, 0.965437949f },
    { 0.902093f, 0.96093f }, { 0.898975968f, 0.959467947f }, { 0.896024f, 0.962094963f },
    { 0.893195f, 0.968533f }, { 0.890504956f, 0.978301f }, { 0.887976f, 0.990915954f },
    { 0.885624945f, 1.005898f }, { 0.883470953f, 1.02276492f }, { 0.881534f, 1.04103398f },
    { 0.879830956f, 1.06022596f }, { 0.878383f, 1.07985699f }, { 0.877207f, 1.09944594f },
    { 0.876324f, 1.11851299f }, { 0.875750959f, 1.13657391f }, { 0.875507951f, 1.15314889f },
    { 0.875613f, 1.16775596f }, { 0.876086f, 1.17991292f }, { 0.876944959f, 1.18913889f },
    { 0.878209949f, 1.19495189f }, { 0.879896f, 1.19694793f }, { 0.881947f, 1.19700992f },
    { 0.884313f, 1.196172f }, { 0.886974f, 1.1944859f }, { 0.889907956f, 1.19200289f },
    { 0.893093944f, 1.18877494f }, { 0.896510959f, 1.18485296f }, { 0.900136f, 1.18028796f },
    { 0.903948963f, 1.17513299f }, { 0.907928f, 1.16943789f }, { 0.912050962f, 1.16325593f },
    { 0.916299f, 1.15663695f }, { 0.920648f, 1.14963293f }, { 0.925076962f, 1.14229596f },
    { 0.929565966f, 1.13467789f }, { 0.929565966f, 1.13467789f }, { 0.938635945f, 1.11880195f },
    { 0.938635945f, 1.11880195f }, { 0.947684944f, 1.10241699f }, { 0.952147961f, 1.09416199f },
    { 0.956542969f, 1.085935f }, { 0.960845947f, 1.07778692f }, { 0.965037942f, 1.06977f },
    { 0.969095945f, 1.06193399f }, { 0.972999f, 1.05433095f }, { 0.976726f, 1.047014f },
    { 0.980255961f, 1.04003298f }, { 0.983566f, 1.03344f }, { 0.986636f, 1.02728689f },
    { 0.989443958f, 1.02162397f }, { 0.991969f, 1.01650393f }, { 0.994188964f, 1.01197898f },
    { 0.996081948f, 1.00809789f }, { 0.997629f, 1.004915f }, { 0.998806953f, 1.00248098f },
    { 0.999594f, 1.00084591f }, { 0.999969959f, 1.00006294f }, { 1.0f, 1.00065494f },
};

/* The flame's width/height for each frame of its animation.  Read by
 * CandleFlameScaleWGet/HGet, which pick the table by flame type. */
float CandleFlameScaleData2[75][2] = /* data 2fc420 */
{
    { 0.542633f, 0.650426f }, { 0.480804f, 0.561570942f }, { 0.341887981f, 0.363259971f },
    { 0.195741f, 0.157871991f }, { 0.11222f, 0.047789f }, { 0.142718986f, 0.284504f },
    { 0.251764f, 0.66534096f }, { 0.381288975f, 0.690426f }, { 0.473353f, 0.690992f },
    { 0.494225979f, 0.679896f }, { 0.497095972f, 0.659154f }, { 0.490185976f, 0.630784f },
    { 0.475844979f, 0.596805f }, { 0.456424f, 0.559233963f }, { 0.456424f, 0.559233963f },
    { 0.411746f, 0.48139f }, { 0.391190976f, 0.445151985f }, { 0.374959f, 0.413393974f },
    { 0.365401f, 0.388135f }, { 0.364869f, 0.371391982f }, { 0.374499f, 0.365183f },
    { 0.388183f, 0.371525973f }, { 0.40447f, 0.39169398f }, { 0.423082978f, 0.423645973f },
    { 0.443745f, 0.465727985f }, { 0.46618f, 0.51641f }, { 0.490107983f, 0.574158967f },
    { 0.515253961f, 0.637445f }, { 0.541339f, 0.704733968f }, { 0.568087f, 0.774497f },
    { 0.568087f, 0.774497f }, { 0.622459948f, 0.915312946f }, { 0.649530947f, 0.983302951f },
    { 0.676155f, 1.04763794f }, { 0.702055f, 1.10678792f }, { 0.72695297f, 1.15922093f },
    { 0.750571966f, 1.20340395f }, { 0.772635f, 1.23780692f }, { 0.792865f, 1.26086295f },
    { 0.810982943f, 1.27129197f }, { 0.826714f, 1.27028191f }, { 0.839777946f, 1.25953794f },
    { 0.851111948f, 1.24076498f }, { 0.861933947f, 1.21566689f }, { 0.872253954f, 1.18594992f },
    { 0.882082f, 1.15331793f }, { 0.882082f, 1.15331793f }, { 0.900307f, 1.0861299f },
    { 0.908723f, 1.0549829f }, { 0.91669f, 1.02774096f }, { 0.924218f, 1.00610793f },
    { 0.931315958f, 0.99179095f }, { 0.937994957f, 0.984554946f }, { 0.944265962f, 0.979068f },
    { 0.95014f, 0.9748f }, { 0.955625f, 0.971658f }, { 0.960733f, 0.969547f },
    { 0.965474963f, 0.968373954f }, { 0.965474963f, 0.968373954f }, { 0.973898947f, 0.968465f },
    { 0.973898947f, 0.968465f }, { 0.980978966f, 0.971174955f }, { 0.980978966f, 0.971174955f },
    { 0.986799955f, 0.97575295f }, { 0.986799955f, 0.97575295f }, { 0.991444f, 0.981444955f },
    { 0.991444f, 0.981444955f }, { 0.994993f, 0.987499f }, { 0.994993f, 0.987499f },
    { 0.99753195f, 0.993160963f }, { 0.99753195f, 0.993160963f }, { 0.999142945f, 0.997678f },
    { 0.999142945f, 0.997678f }, { 0.999908f, 1.00029695f }, { 1.0f, 1.00066f },
};




void EffectManyCandleCut(void *pEffect);
static void EffectManyCandleLoadAllEffectReq(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);
static void EffectManyCandleLoadEnd(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);
static void *EffectManyCandleReq(float (*pCenterPos)[4], float (*pPos)[4],
                                 int DataNum, int Id);
static void ItemEffectCtrlInit(void);
static int  ItemEffectGetChangeVal(int NowFrame, int TotalFrame,
                                   int MinVal, int MaxVal);
static void EffectThunderLightInit(void);
static void EffectManyCandleLoadInit(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);
static void EffectManyCandleLoadMain(MANY_CANDLE_LOAD_CTRL *pLoadCtrl);
static void DoorSealDisappearInit(void);
static void DoorSealDisappearMain(DOOR_SEAL_DISAPPEAR_CTRL *pCtrl);
static void DoorSealDisappearCameraSet(void);
static void DoorSealDisappearPDeformParamSet(EFFECT_CONT *ec, float (*pPos)[4],
                                             int AlphaRate, float *pSpeed,
                                             float *pWaveRate);
static void DoorSealDisappearPDeformBlurParamSet(EFFECT_CONT *ec, float (*pPos)[4],
                                                 int AlphaRate);
static void SubDoorSeal(float (*pPosition)[4], float *BasePos, float AlphaRate);
static void SubCandleFlame(EFFECT_CONT *ec);
static void CandleFlameMakePacket(sceVu0IVECTOR *pIvec, u_long Tex0, u_int Depth,
                                  int R, int G, int B, int Alpha);
/* PORT-ONLY, defined beside CandleFlameMakePacket. */
static void CandleFlameHostQuad(float (*pLocalWorld)[4], u_long Tex0,
                                int R, int G, int B, int Alpha);
static HAZE_PARTICLE *EffectHazeGetParticleBuf(HAZE_CTRL *pHazeCtrl);
static void EffectHazeInitParticle(HAZE_PARTICLE *pHazeParticle, float *CenterPos,
                                   float *Offset, int HazeType);
static void *HazeCtrl(HAZE_CTRL *pHazeCtrl, float size);
static void EffectHazeParticleReqOneFrame(HAZE_CTRL *pHazeCtrl);
static void CloudOfDustCtrlInit(CLOUD_OF_DUST_CTRL *pCod, float *BasePos);
static void ManyCandleCtrlInit(MANY_CANDLE_CTRL *pMc, float (*pCenterPos)[4],
                               float (*pParticlePos)[4], int DataNum, int Id);
static void *ManyCandleCtrl(MANY_CANDLE_CTRL *pCandleCtrl);
static void ManyCandleParticleUpdate(MANY_CANDLE_CTRL *pCandleCtrl);
static void AddCloudOfDustRunParticle(CLOUD_OF_DUST_CTRL *pCod);
static void *CloudOfDustCtrl(CLOUD_OF_DUST_CTRL *pCod, float size);
static void GetHazeAllVelocity(float *Velocity, HAZE_CTRL *pHazeCtrl);
static void HazeAllVelocityCtrl(HAZE_CTRL *pHazeCtrl);
static int  UpdateHazeParticles(HAZE_PARTICLE *pPtop, float *AllVelocity,
                                int HazeType);
static void HazeCtrlInit(HAZE_CTRL *pHc, float (*pBasePos)[4], float (*pRot)[4],
                         int Id, int Type, float *pAlphaRate);


/* ==========================================================================
 *  Module init
 * ======================================================================== */

/* 418 -- the once-per-room reset.  The torch pool is the only one cleared
 * outright here; the other three are InitHeatHaze()'s job, and it keeps rather
 * than drops a pool whose flag has bit 0x80 up. */
void InitEffectOth(void)
{
    int i;

    init_pond    = 1;                                           /* 421 */
    init_hhaze   = 1;                                           /* 422 */
    init_newitem = 1;                                           /* 423 */

    init_torch = 1;                                             /* 425 */

    for (i = 0; i < 5; i++)                                     /* 426 */
    {
        torch_particle[i].flag = 0;
        torch_particle[i].cnt  = 0;
    }

    init_smoke = 1;                                             /* 431 */
    haze_stop  = 0;                                             /* 432 */

    amulet_fire_ret  = nullptr;                                 /* 434 */
    amulet_fire_flow = -1;                                      /* 435 */
    amulet_fire_cnt  = -1;                                      /* 436 */

    ItemEffectCtrlInit();                                       /* 440 */

    EffectThunderLightInit();                                   /* 442 */
    EffectManyCandleLoadInit(&ManyCandleLoadCtrl);              /* 443 */
    DoorSealDisappearInit();                                    /* 444 */
}

/* 450 -- the module's per-frame pass.  Only the two asynchronous jobs need
 * one; every other sub-system is driven from its own EFFECT_CONT handler. */
void InitEffectOthEF(void)
{
    EffectManyCandleLoadMain(&ManyCandleLoadCtrl);              /* 451 */
    DoorSealDisappearMain(&DoorSealDisappearCtrl);              /* 452 */
}

/* 1536 -- reset the three heat-haze pools between rooms.
 *
 * Note the inverted sense of bit 0x80: a pool carrying it is *kept* (the flag
 * is merely masked back down), and only a pool without it is cleared.  That is
 * what lets an effect which outlives the room load hold on to its particles. */
void InitHeatHaze(void)
{
    int i;

    for (i = 0; i < 4; i++)                                     /* 1539 */
    {
        if ((ene_particle[i].flag & 0x80) != 0)
        {
            ene_particle[i].flag &= 0x7f;
        }
        else
        {
            ene_particle[i].flag = 0;
        }
    }

    if ((amu_particle[0].flag & 0x80) != 0)                     /* 1546 */
    {
        amu_particle[0].flag &= 0x7f;
    }
    else
    {
        amu_particle[0].flag = 0;
    }

    for (i = 0; i < 5; i++)                                     /* 1553 */
    {
        if ((torch_particle[i].flag & 0x80) != 0)
        {
            torch_particle[i].flag &= 0x7f;
        }
        else
        {
            torch_particle[i].flag = 0;
        }
    }
}


/* ==========================================================================
 *  The massed candles
 *
 *  A four-state loader in front of the effect itself: the whole set is one
 *  pak (file 0x1183) holding up to five candle groups, and each group becomes
 *  its own EFFECT_CONT.  Status 0 idle, 1 loading, 2 running, 3 loading but
 *  already cancelled -- which is what lets a cut arrive mid-load without
 *  leaking the buffer.
 * ======================================================================== */

/* 1046 -- note the slot clear runs backwards, from pEffRet[4] down, and the
 * test is `-1 < iVar2` after a pre-decrement, so it really does cover all
 * five.  Reproduced as a plain forward loop would not match the ROM's
 * ordering, but nothing observes it. */
static void EffectManyCandleLoadInit(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    int i;

    pLoadCtrl->Status   = 0;                                    /* 1048 */
    pLoadCtrl->pLoadBuf = (u_int *)nullptr;                     /* 1049 */
    pLoadCtrl->LoadId   = -1;                                   /* 1050 */
    pLoadCtrl->PackNum  = 0;                                    /* 1051 */

    for (i = 4; i >= 0; i--)                                    /* 1052 */
    {
        pLoadCtrl->pEffRet[i] = nullptr;                        /* 1054 */
    }
}                                                               /* 1055 */

/* 1062 -- poll the load.  State 3 is "cancelled while loading": the load is
 * still waited on, but its result is thrown away rather than turned into
 * effects, which is what stops a late completion resurrecting a cut set. */
static void EffectManyCandleLoadMain(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    if (pLoadCtrl->Status == 1)                                 /* 1064 */
    {
        if (IsLoadEnd(pLoadCtrl->LoadId) != 0)                  /* 1066 */
        {
            EffectManyCandleLoadAllEffectReq(pLoadCtrl);        /* 1068 */
            pLoadCtrl->Status = 2;                              /* 1069 */
        }
    }
    else if (pLoadCtrl->Status == 3)                            /* 1074 */
    {
        if (IsLoadEnd(pLoadCtrl->LoadId) != 0)                  /* 1076 */
        {
            EffectManyCandleLoadEnd(pLoadCtrl);                 /* 1078 */
        }
    }
}                                                               /* 1081 */

/* 1089 -- start the load.  A failed allocation leaves Status at 0, so the
 * request is simply dropped and can be made again. */
void EffectManyCandleLoadReq(float *Offset)
{
    if (ManyCandleLoadCtrl.Status == 0)                         /* 1091 */
    {
        ManyCandleLoadCtrl.pLoadBuf = (u_int *)EFFECT_MALLOC(0x2300); /* 1093 */

        if (ManyCandleLoadCtrl.pLoadBuf != (u_int *)nullptr)    /* 1094 */
        {
            /* uintptr_t, not the ROM's u_int: truncating the buffer address to
             * 32 bits would hand the loader a bad destination on the host. */
            ManyCandleLoadCtrl.LoadId =
                LoadReq(0x1183, (uintptr_t)ManyCandleLoadCtrl.pLoadBuf); /* 1096 */
            ManyCandleLoadCtrl.Status = 1;                      /* 1097 */

            g3dxVu0CopyVector(ManyCandleLoadCtrl.Offset, Offset); /* 1099 */
        }
    }
}                                                               /* 1101 */

/* 1107 -- cancel.  Mid-load it only flips 1 -> 3 and lets Main() finish the
 * teardown when the load lands; once running it cuts every group first. */
void EffectManyCandleLoadCut(void)
{
    int i;

    if (ManyCandleLoadCtrl.Status != 0)                         /* 1109 */
    {
        if (ManyCandleLoadCtrl.Status == 1)                     /* 1111 */
        {
            ManyCandleLoadCtrl.Status = 3;                      /* 1113 */
        }
        else if (ManyCandleLoadCtrl.Status == 2)                /* 1115 */
        {
            for (i = 0; i < ManyCandleLoadCtrl.PackNum; i++)    /* 1117 */
            {
                EffectManyCandleCut(ManyCandleLoadCtrl.pEffRet[i]); /* 1119 */
            }
        }

        EffectManyCandleLoadEnd(&ManyCandleLoadCtrl);           /* 1123 */
    }
}                                                               /* 1124 */

/* 1130 -- turn the loaded pak into one effect per group.
 *
 * Pk2GetNum() is called twice -- once to latch PackNum and again as the loop
 * bound every iteration.  That is the ROM's own shape, not a decompiler
 * artefact; the count cannot change, so it is only wasted work. */
static void EffectManyCandleLoadAllEffectReq(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    int    i;
    u_int *pFile;

    pLoadCtrl->PackNum = Pk2GetNum(pLoadCtrl->pLoadBuf);        /* 1132 */

    for (i = 0; i < Pk2GetNum(pLoadCtrl->pLoadBuf); i++)        /* 1134 */
    {
        pFile = Pk2GetAddr(pLoadCtrl->pLoadBuf, i);             /* 1136 */

        pLoadCtrl->pEffRet[i] =
            EffectManyCandleReq(&pLoadCtrl->Offset,
                                (float (*)[4])(pFile + 4),
                                (int)pFile[1], i);              /* 1138 */
    }
}                                                               /* 1144 */

/* 1151 -- free the buffer and reset.  EFFECT_FREE tolerates a null. */
static void EffectManyCandleLoadEnd(MANY_CANDLE_LOAD_CTRL *pLoadCtrl)
{
    EFFECT_FREE(pLoadCtrl->pLoadBuf);                           /* 1153 */
    EffectManyCandleLoadInit(pLoadCtrl);                        /* 1154 */
}

/* 1184 -- effect id 0x20 is the massed-candle handler. */
static void *EffectManyCandleReq(float (*pCenterPos)[4], float (*pPos)[4],
                                 int DataNum, int Id)
{
    return SetEffects_MANY_CANDLE(2, pCenterPos, pPos,
                                  (u_int)DataNum, (u_int)Id);   /* 1185 */
}

/* 1192 -- the same shape as EffectHazeCut(): free the handler's own block,
 * then return the EFFECT_CONT slot. */
void EffectManyCandleCut(void *pEffect)
{
    if (pEffect != nullptr)                                     /* 1194 */
    {
        EFFECT_FREE(((EFFECT_CONT *)pEffect)->pnt[1]);          /* 1196 */
        ResetEffects(pEffect);                                  /* 1197 */
    }
}                                                               /* 1199 */

/* ==========================================================================
 *  The item glint
 *
 *  One record per placed item, keyed by the placing record's label rather
 *  than an inventory id.  Four independent counters drive the two scale and
 *  two alpha ramps, so the glint's pulse and its twinkle are out of step.
 * ======================================================================== */

/* 2722 */
static void ItemEffectCtrlInit(void)
{
    SingleLinkListInit(&ItemEffectList, sizeof(ITEM_EFFECT_DATA)); /* 2723 */
}

/* 2741 -- everything but the position and the two ids starts at zero. */
static void ItemEffectReqSub(float *Pos, int ItemNo, int Type)
{
    ITEM_EFFECT_DATA ItemEffectData;

    g3dxVu0CopyVector(ItemEffectData.Position, Pos);            /* 2730 */

    ItemEffectData.Flow          = 0;                           /* 2732 */
    ItemEffectData.Rot           = 0.0f;                        /* 2733 */
    ItemEffectData.fCounter      = 0.0f;                        /* 2734 */
    ItemEffectData.ScaleCounter0 = 0;                           /* 2735 */
    ItemEffectData.ScaleCounter1 = 0;                           /* 2736 */
    ItemEffectData.AlphaCounter0 = 0;                           /* 2737 */
    ItemEffectData.AlphaCounter1 = 0;                           /* 2738 */
    ItemEffectData.ItemNo        = ItemNo;                      /* 2739 */
    ItemEffectData.Type          = Type;                        /* 2740 */

    SingleLinkListAddEnd(&ItemEffectList, &ItemEffectData);     /* 2749 */
}                                                               /* 2750 */

/* 2754 -- the export is a bare forward; the work is all in the static. */
void ItemEffectReq(float *Pos, int ItemNo, int EffectType)
{
    ItemEffectReqSub(Pos, ItemNo, EffectType);                  /* 2755 */
}

/* 2759 -- drop every glint with this label.
 *
 * The walk is written oddly: the next pointer is taken BEFORE the body and the
 * loop is a `while (true)` broken on it being null, rather than the usual
 * for-loop.  That is what makes removing the current cell safe, and it is the
 * ROM's own shape -- the other Delete loops in the effect layer take the next
 * pointer inside the body instead. */
void ItemEffectCut(int ItemNo)
{
    SLL_CELL *pCell;
    SLL_CELL *pNext;
    ITEM_EFFECT_DATA *pData;

    pCell = SingleLinkListBeginCell(&ItemEffectList);           /* 2761 */

    if (pCell != (SLL_CELL *)nullptr)
    {
        pNext = SingleLinkListNextCell(pCell);

        while (true)
        {
            pData = (ITEM_EFFECT_DATA *)SingleLinkListCellBodyPtr(pCell);

            if (pData->ItemNo == ItemNo)                        /* 2765 */
            {
                SingleLinkListRemove(&ItemEffectList, pCell);   /* 2767 */
            }

            if (pNext == (SLL_CELL *)nullptr)
            {
                break;
            }

            pCell = pNext;
            pNext = pNext->pNext;
        }
    }
}                                                               /* 2771 */

/* 2776 */
void ItemEffectAllCut(void)
{
    SingleLinkListAllCellFree(&ItemEffectList);                 /* 2778 */
}                                                               /* 2779 */

/* ==========================================================================
 *  Haze volumes
 *
 *  All four entry points are one SetEffects_HAZE(2, ...) with different
 *  arguments; effect id 0x21 is the haze handler, and the fifth argument is
 *  the volume's id.  The room haze passes 0 (or a caller-supplied label), the
 *  two ghost volumes the fixed 998 and 999 -- which is what keeps them out of
 *  the label space EffectHazeCutId() searches.
 * ======================================================================== */

/* 4078 / 4086 -- the room haze.  The Id form exists so several can coexist.
 *
 * PORT NOTE: the ROM's trailing 0 for the rotation and alpha pointers was an
 * int in a variadic tail and reached the handler as a wild pointer on a
 * 64-bit host -- the crash that made every room haze fatal.  Mode 0 never
 * looks at the rotation, and pnt[3] is null-tested before it is read. */
void *EffectHazeReq(float *CenterPos)
{
    return SetEffects_HAZE(2, CenterPos, nullptr, 0, 0, nullptr); /* 4079 */
}

void EffectHazeReqId(float *CenterPos, int Id)
{
    SetEffects_HAZE(2, CenterPos, nullptr, (u_int)Id, 0, nullptr); /* 4087 */
}

/* 4101 / 4116 -- the two per-ghost volumes.  Position, rotation and alpha are
 * all passed by address, so the volume tracks the ghost with no per-frame
 * update; the trailing 1 and 2 are the EFF_HAZE type, which is what picks
 * KusabiHazeParameter or SaeHazeParameter. */
void *EffectKusabiHazeReq(float *Pos, float *Rot, float *pAlpha)
{
    return SetEffects_HAZE(2, Pos, Rot, 998, 1, pAlpha);        /* 4102 */
}

void *EffectSaeHazeReq(float *Pos, float *Rot, float *pAlpha)
{
    return SetEffects_HAZE(2, Pos, Rot, 999, 2, pAlpha);        /* 4117 */
}

/* 4124 -- suppress the drawing without tearing the volume down.  Despite the
 * name this is not Sae-specific: haze_stop gates every haze volume. */
void EffectSaeHazSetNoDrawFlg(int iFlg)
{
    haze_stop = iFlg;                                           /* 4125 */
}

/* 4132 -- release one volume.  pnt[1] is the HAZE_CTRL the handler allocated;
 * it is freed before the EFFECT_CONT slot is returned. */
void EffectHazeCut(void *pEffect)
{
    if (pEffect != nullptr)                                     /* 4134 */
    {
        EFFECT_FREE(((EFFECT_CONT *)pEffect)->pnt[1]);          /* 4136 */
        ResetEffects(pEffect);                                  /* 4137 */
    }
}                                                               /* 4139 */

/* 4146 -- release by id.  Walks all 48 EFFECT_CONT slots looking for a live
 * haze ('!' is 0x21, the handler id) whose HAZE_CTRL carries this Id. */
void EffectHazeCutId(int Id)
{
    EFFECT_CONT *pEffect;
    int i;

    pEffect = EffectGetBufferTopAdrs();                         /* 4148 */

    for (i = 48; i != 0; i--)                                   /* 4150 */
    {
        if ((pEffect->dat.uc8[0] == 0x21)
            && (pEffect->pnt[1] != nullptr)
            && (((HAZE_CTRL *)pEffect->pnt[1])->Id == Id))      /* 4152 */
        {
            EffectHazeCut(pEffect);                             /* 4154 */
        }

        pEffect++;
    }
}                                                               /* 4160 */

/* 4167 / 4175 -- both ghost cuts are the plain cut; the two names exist for
 * symmetry with the two Reqs. */
void EffectKusabiHazeCut(void *pEffect)
{
    EffectHazeCut(pEffect);                                     /* 4168 */
}

void EffectSaeHazeCut(void *pEffect)
{
    EffectHazeCut(pEffect);                                     /* 4176 */
}

/* 4811 / 4850 -- the preset lookup, and a second copy of it.
 *
 * The two bodies are identical -- the "Org" name suggests it was meant to hand
 * back the unmodified preset while the other returned a live, debug-editable
 * copy, but in this build both return the same three globals.  Kept as two
 * functions, as the ROM has them. */
HAZE_PARAMETER *EffectHazeGetParameterPtr(int Type)
{
    if (Type == 0)                                              /* 4813 */
    {
        return &HazeParameter;
    }

    if (Type == 1)                                              /* 4823 */
    {
        return &KusabiHazeParameter;
    }

    return &SaeHazeParameter;                                   /* 4841 */
}                                                               /* 4843 */

HAZE_PARAMETER *EffectHazeGetParameterPtrOrg(int Type)
{
    if (Type == 0)                                              /* 4852 */
    {
        return &HazeParameter;
    }

    if (Type == 1)
    {
        return &KusabiHazeParameter;
    }

    return &SaeHazeParameter;                                   /* 4862 */
}                                                               /* 4864 */

/* ==========================================================================
 *  Candle flame
 * ======================================================================== */

/* 802 / 819 -- the flame's size this frame, straight out of the authored
 * table.  Neither bounds-checks Frame; the caller wraps it. */
static float CandleFlameScaleWGet(int Type, int Frame)
{
    if (Type == 0)                                              /* 804 */
    {
        return CandleFlameScaleData[Frame][0];                  /* 806 */
    }

    return CandleFlameScaleData2[Frame][0];                     /* 810 */
}                                                               /* 812 */

static float CandleFlameScaleHGet(int Type, int Frame)
{
    if (Type == 0)                                              /* 821 */
    {
        return CandleFlameScaleData[Frame][1];                  /* 823 */
    }

    return CandleFlameScaleData2[Frame][1];                     /* 827 */
}                                                               /* 829 */

/* 3229 -- arm the amulet fire.  Both latches go to 0; InitEffectOth() seeds
 * them at -1, which is the "not running" state. */
void CallAmuletFire(void)
{
    amulet_fire_flow = 0;                                       /* 3230 */
    amulet_fire_cnt  = 0;                                       /* 3231 */
}

/* ==========================================================================
 *  The storm
 *
 *  Lightning and thunder are two independent state machines driven from one
 *  EffectThunderLightReq(): the flash starts immediately and the crack after
 *  DelayTime frames, which is what puts the distance between them.
 *
 *  Both share a status vocabulary -- 4 waiting out the delay, 1 the first
 *  flash, 2 the gap, 3 the long second flash, 0 idle -- so the flash is a
 *  double blink rather than a single one.  Neither block is authored data;
 *  every duration is a literal in the code (2, 2, 12 frames for the light;
 *  2, 2, 45 for the sound and rumble).
 * ======================================================================== */


/* 4884 */
static void EffectThunderLightInit(void)
{
    ThunderLightCtrl.LightningStatus = 0;                       /* 4886 */
    ThunderLightCtrl.LightningTime   = 0;                       /* 4887 */
    ThunderLightCtrl.LightningFlg    = 0;                       /* 4888 */
    ThunderLightCtrl.ThunderStatus   = 0;                       /* 4889 */
    ThunderLightCtrl.ThunderTime     = 0;                       /* 4890 */
}                                                               /* 4891 */

/* 4901 -- arm a strike.  Note LightningDirection's w is forced to 0 (it is a
 * direction, not a point) while ThunderPosition keeps its w. */
void EffectThunderLightReq(float *LightningDirection, int DelayTime,
                           float *ThunderPosition)
{
    g3dxVu0CopyVector(ThunderLightCtrl.LightningDirection, LightningDirection);
    ThunderLightCtrl.LightningDirection[3] = 0.0f;              /* 4904 */

    g3dxVu0CopyVector(ThunderLightCtrl.ThunderPosition, ThunderPosition);

    ThunderLightCtrl.LightningStatus = 4;                       /* 4906 */
    ThunderLightCtrl.LightningTime   = 0;                       /* 4907 */
    ThunderLightCtrl.LightningFlg    = 0;                       /* 4908 */
    ThunderLightCtrl.ThunderStatus   = 4;                       /* 4909 */
    ThunderLightCtrl.ThunderTime     = DelayTime;               /* 4910 */
}                                                               /* 4911 */

/* 4915 -- step both machines.
 *
 * The lightning's delay arm tests `--LightningTime != -1`, i.e. it fires on
 * the frame the counter goes negative; the request seeds it at 0, so the flash
 * begins immediately.  The thunder's delay arm is the same shape seeded with
 * DelayTime, which is where the gap comes from.
 *
 * The rumble is requested every frame of both thunder legs 1 and 3, not once
 * -- VibrateRequest1(0, 1) is inside the per-frame path. */
void EffectThunderLightExec(void)
{
    SND_3D_SET Snd3d;

    /* ---- lightning ---- */
    if (ThunderLightCtrl.LightningStatus == 4)                  /* 4917 */
    {
        ThunderLightCtrl.LightningTime--;

        if (ThunderLightCtrl.LightningTime == -1)
        {
            ThunderLightCtrl.LightningStatus = 1;
            ThunderLightCtrl.LightningTime   = 2;
        }
    }

    if (ThunderLightCtrl.LightningStatus == 1)                  /* 4929 */
    {
        ThunderLightCtrl.LightningFlg = 1;
        ThunderLightCtrl.LightningTime--;

        if (ThunderLightCtrl.LightningTime == 0)
        {
            ThunderLightCtrl.LightningFlg    = 0;
            ThunderLightCtrl.LightningTime   = 2;
            ThunderLightCtrl.LightningStatus = 2;
        }
    }
    else if (ThunderLightCtrl.LightningStatus == 2)             /* 4941 */
    {
        ThunderLightCtrl.LightningTime--;

        if (ThunderLightCtrl.LightningTime == 0)
        {
            ThunderLightCtrl.LightningStatus = 3;
            ThunderLightCtrl.LightningTime   = 12;
        }
    }
    else if (ThunderLightCtrl.LightningStatus == 3)             /* 4951 */
    {
        ThunderLightCtrl.LightningFlg = 1;
        ThunderLightCtrl.LightningTime--;

        if (ThunderLightCtrl.LightningTime == 0)
        {
            ThunderLightCtrl.LightningStatus = 0;
            ThunderLightCtrl.LightningFlg    = 0;
        }
    }

    /* ---- thunder ---- */
    if (ThunderLightCtrl.ThunderStatus == 4)                    /* 4963 */
    {
        ThunderLightCtrl.ThunderTime--;

        if (ThunderLightCtrl.ThunderTime == -1)
        {
            memset(&Snd3d, 0, sizeof(SND_3D_SET));
            Snd3d.pos = &ThunderLightCtrl.ThunderPosition;

            snd_utilAutoBDPlay(0xd0d, 0xd0c, 0, 0, 0x3200, 0x1000, 0, &Snd3d);

            ThunderLightCtrl.ThunderStatus = 1;
            ThunderLightCtrl.ThunderTime   = 2;
        }
    }

    if (ThunderLightCtrl.ThunderStatus == 1)                    /* 4975 */
    {
        ThunderLightCtrl.ThunderTime--;
        VibrateRequest1(0, 1);

        if (ThunderLightCtrl.ThunderTime == 0)
        {
            ThunderLightCtrl.ThunderTime   = 2;
            ThunderLightCtrl.ThunderStatus = 2;
        }
    }
    else if (ThunderLightCtrl.ThunderStatus == 2)               /* 4983 */
    {
        if (ThunderLightCtrl.ThunderTime - 1 == 0)
        {
            ThunderLightCtrl.ThunderStatus = 3;
            ThunderLightCtrl.ThunderTime   = 45;
        }
        else
        {
            ThunderLightCtrl.ThunderTime--;
        }
    }
    else if (ThunderLightCtrl.ThunderStatus == 3)               /* 4990 */
    {
        ThunderLightCtrl.ThunderTime--;
        VibrateRequest1(0, 1);

        if (ThunderLightCtrl.ThunderTime == 0)
        {
            ThunderLightCtrl.ThunderStatus = 0;
        }
    }
}                                                               /* 4995 */

/* 5001 -- hang the flash on the room's first directional light.  Only while
 * LightningFlg is up, so the room goes back to its own lighting between
 * blinks. */
void EffectThunderLightSetRoomLight(void)
{
    G3DLIGHT TmpLight = {0};

    if (ThunderLightCtrl.LightningFlg != 0)                     /* 5003 */
    {
        EffectThunderLightGetG3dLight(&TmpLight);               /* 5005 */
        gra3dSetLight(LID_DIRECTIONAL_1, &TmpLight);            /* 5006 */
        gra3dLightEnable(LID_DIRECTIONAL_1, 1);                 /* 5007 */
        gra3dApplyLight();                                      /* 5008 */
    }
}                                                               /* 5010 */

/* 5015 -- the flash as a G3DLIGHT.  Diffuse is 0.8 / 0.8 / 1.0 -- a
 * blue-tinted white -- and the direction is the request's vector normalised
 * through the usual VU0 vrsqrt/vmulq idiom. */
void EffectThunderLightGetG3dLight(G3DLIGHT *pLight)
{
    pLight->vDiffuse[0] = 0.8f;                                 /* 5018 */
    pLight->vDiffuse[1] = 0.8f;
    pLight->vDiffuse[2] = 1.0f;
    pLight->vDiffuse[3] = 1.0f;

    sceVu0Normalize(pLight->vDirection, ThunderLightCtrl.LightningDirection);

    pLight->Type     = G3DLIGHT_DIRECTIONAL;                    /* 5021 */
    pLight->fFalloff = 1.0f;                                    /* 5022 */
}

/* 5026 */
int EffectThunderLightGetLightningFlg(void)
{
    return ThunderLightCtrl.LightningFlg;                       /* 5028 */
}                                                               /* 5029 */

/* ==========================================================================
 *  The item glint
 *
 *  The marker that says "there is something here": the twinkle drawn over
 *  every placed pick-up, and the only thing that makes a floor item findable.
 *  It is not a model.  MapObjRegistEffect() gives an `eff_item` record a
 *  MapPut slot whose sole job is to call MapObjEffCallback(), which calls
 *  ItemEffectDrawOne() with the placing record's labelID as the key; the
 *  record ItemEffectReq() filed carries the world position and the counters.
 *
 *  ITEM_EFFECT_DATA::Type forks it into two unrelated looks, and only one is
 *  reachable in this build.  ItemEffectReq() has exactly one call site in the
 *  loadable segments -- MapObjSetEffect at 0x10d690, whose delay slot is
 *  `addiu a2, zero, 1` -- so Type is always 1 and the Type 0 pulse below is
 *  dead code.  It is reconstructed anyway, because it is the ROM's.
 *
 *    Type 0  a slow pulse: a three-state machine on Flow (hold / swell /
 *            shrink) driving one size, plus a spin whose speed and sprite
 *            set depend on GetCornHitCheck() -- i.e. on whether the player
 *            is inside 1200 units.  Far away it is one halo and one core;
 *            close up the core is drawn three times instead, tinted blue /
 *            green / red about three different axes, which is what turns it
 *            into a chromatic star.
 *    Type 1  the steady glint: no state machine at all, four free-running
 *            counters through ItemEffectGetChangeVal().  Each counter's wrap
 *            point is exactly twice its triangle's half-period (154/77,
 *            44/22, 142/71, 74/37), and the four periods share no factor, so
 *            the two sizes and the two alphas never come back into phase --
 *            which is what makes it read as a twinkle rather than a throb.
 * ======================================================================== */

/* 2784 -- the record placed for `ItemNo`, or NULL.  ItemNo is the map
 * record's labelID, not an inventory id.
 *
 * The loop's null test and SingleLinkListNextCell()'s own were merged by GCC,
 * which is why one branch carries SingleLinkList.h line 76. */
static ITEM_EFFECT_DATA *ItemEffectGetItemEffectData(int ItemNo)
{
    SLL_CELL         *pCell;
    ITEM_EFFECT_DATA *pRetData = (ITEM_EFFECT_DATA *)nullptr;   /* 2788 */

    for (pCell = SingleLinkListBeginCell(&ItemEffectList);
         pCell != (SLL_CELL *)nullptr;
         pCell = SingleLinkListNextCell(pCell))
    {
        ITEM_EFFECT_DATA *pItemEffectData =
            (ITEM_EFFECT_DATA *)SingleLinkListCellBodyPtr(pCell); /* 2789 */

        if (pItemEffectData->ItemNo == ItemNo)                  /* 2791 */
        {
            pRetData = pItemEffectData;                         /* 2792 */
            break;
        }
    }

    return pRetData;                                            /* 2797 */
}

/* 2803 -- the Type 0 pulse.  Unreachable in this build; see the banner.
 *
 * Both arms of the distance test are the same six steps with different
 * constants -- exactly 18 source lines apart, which is what identifies the
 * pairs that GCC cross-jumped.  The spin steps are PI/54 (3 1/3 degrees a
 * frame) far and PI/36 (5 degrees) near.
 *
 * The name is the port's: the ROM inlined this completely and emitted no
 * symbol for it.  Everything else -- the parameter, the local names and their
 * spelling -- comes from functions.txt. */
static void ItemEffectDrawOneType0(ITEM_EFFECT_DATA *pItemEffectData,
                                   DRAW_ENV *pDrawEnv)
{                                                               /* 2803 */
    GRA3DCAMERA  *pCam;
    float        *cam_pos;
    int           flow;
    int           flg;
    float         rot_z;
    float         cnt;
    float         wait;
    float         out;
    float         size = 0.0f;                                  /* 2811 */
    float         rot_x;
    float         rot_y;
    float         wpos[4];
    sceVu0FMATRIX wlm[5];

    pCam    = gra3dGetCamera();                                 /* 2814 */
    cam_pos = gra3dcamGetPosition();                            /* 2815 */

    flow  = pItemEffectData->Flow;                              /* 2818 */
    cnt   = pItemEffectData->fCounter;                          /* 2819 */
    rot_z = pItemEffectData->Rot;                               /* 2820 */

    g3dxVu0CopyVector(wpos, pItemEffectData->Position);         /* 2821 */

    flg = GetCornHitCheck(wpos, 1200.0f);                       /* 2822 */

    if (flg == 0)                                               /* 2824 */
    {
        /* Far: the slow spin and the long hold. */
        if (EffWrkStopFlgGet() == 0)                            /* 2825 */
        {
            rot_z += 0.05817764f;                               /* 2826 */

            if (rot_z > 3.1415925f)
            {
                rot_z -= 6.283185f;
            }
        }

        wait = 60.0f;                                           /* 2828 */
        out  = 30.0f;                                           /* 2829 */

        switch (flow)                                           /* 2831 */
        {
        case 0:
            if (cnt > wait)                                     /* 2833 */
            {
                cnt = wait;                                     /* 2834 */
            }
            break;

        case 1:
        case 2:
            /* Cross-jumped onto the near arm's copy at 2856/2857. */
            if (cnt > out)
            {
                cnt = out;
            }
            break;
        }
    }
    else                                                        /* 2842 */
    {
        /* Near: faster spin, shorter hold, longer ramp. */
        if (EffWrkStopFlgGet() == 0)                            /* 2843 */
        {
            rot_z += 0.08726645f;                               /* 2844 */

            if (rot_z > 3.1415925f)
            {
                rot_z -= 6.283185f;
            }
        }

        wait = 10.0f;                                           /* 2846 */
        out  = 20.0f;                                           /* 2847 */

        switch (flow)                                           /* 2849 */
        {
        case 0:
            if (cnt > wait)                                     /* 2851 */
            {
                cnt = wait;                                     /* 2852 */
            }
            break;

        case 1:
        case 2:
            if (cnt > out)                                      /* 2856 */
            {
                cnt = out;                                      /* 2857 */
            }
            break;
        }
    }

    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);         /* 2862 */

    /* The pulse itself.  Each arm's `else` is the same two lines, so GCC left
     * one copy of them -- the one carrying case 2's line numbers. */
    switch (flow)                                               /* 2864 */
    {
    case 0:
        size = 0.0f;                                            /* 2866 */

        if (cnt >= wait)                                        /* 2867 */
        {
            cnt  = 0.0f;                                        /* 2868 */
            flow = 1;                                           /* 2869 */
        }
        else if (EffWrkStopFlgGet() == 0)
        {
            cnt += 1.0f;
        }
        break;                                                  /* 2875 */

    case 1:
        size = cnt / out;                                       /* 2877 */

        if (cnt >= out)                                         /* 2878 */
        {
            cnt  = 0.0f;                                        /* 2879 */
            flow = 2;                                           /* 2880 */
        }
        else if (EffWrkStopFlgGet() == 0)
        {
            cnt += 1.0f;
        }
        break;

    case 2:
        size = (out - cnt) / out;                               /* 2888 */

        if (cnt >= out)                                         /* 2889 */
        {
            cnt  = 0.0f;                                        /* 2890 */
            flow = 0;                                           /* 2891 */
        }
        else if (EffWrkStopFlgGet() == 0)                       /* 2893 */
        {
            cnt += 1.0f;                                        /* 2894 */
        }
        break;
    }

    if (flg == 0)                                               /* 2900 */
    {
        /* wlm[0] faces the camera, wlm[1] adds the spin about Z. */
        sceVu0UnitMatrix(wlm[0]);                               /* 2902 */
        sceVu0RotMatrixX(wlm[0], wlm[0], rot_x);                /* 2903 */
        sceVu0RotMatrixY(wlm[0], wlm[0], rot_y);                /* 2904 */
        sceVu0TransMatrix(wlm[0], wlm[0], wpos);                /* 2905 */

        sceVu0UnitMatrix(wlm[1]);                               /* 2907 */
        sceVu0RotMatrixZ(wlm[1], wlm[1], rot_z);                /* 2908 */
        sceVu0RotMatrixX(wlm[1], wlm[1], rot_x);                /* 2909 */
        sceVu0RotMatrixY(wlm[1], wlm[1], rot_y);                /* 2910 */
        sceVu0TransMatrix(wlm[1], wlm[1], wpos);                /* 2911 */

        Set3DPosTexure(wlm[1], pDrawEnv, 0x50,
                       size * 120.0f, size * 120.0f,
                       0xb4, 0xb4, 0xb4, 0x50);                 /* 2913 */
        Set3DPosTexure(wlm[0], pDrawEnv, 0x52,
                       54.0f, 54.0f,
                       0xb4, 0xb4, 0xb4, (u_char)(size * 48.0f)); /* 2914 */
    }
    else
    {
        /* Near.  wlm[0] is built exactly as above and then never drawn -- the
         * camera-facing core is replaced by the three tinted copies below.
         * The dead matrix is the ROM's; kept as found. */
        sceVu0UnitMatrix(wlm[0]);                               /* 2916 */
        sceVu0RotMatrixX(wlm[0], wlm[0], rot_x);                /* 2917 */
        sceVu0RotMatrixY(wlm[0], wlm[0], rot_y);                /* 2918 */
        sceVu0TransMatrix(wlm[0], wlm[0], wpos);                /* 2919 */

        sceVu0UnitMatrix(wlm[1]);                               /* 2921 */
        sceVu0RotMatrixZ(wlm[1], wlm[1], rot_z);                /* 2922 */
        sceVu0RotMatrixX(wlm[1], wlm[1], rot_x);                /* 2923 */
        sceVu0RotMatrixY(wlm[1], wlm[1], rot_y);                /* 2924 */
        sceVu0TransMatrix(wlm[1], wlm[1], wpos);                /* 2925 */

        /* Three fixed 45-degree planes, each spun on a different axis. */
        sceVu0UnitMatrix(wlm[2]);                               /* 2927 */
        sceVu0RotMatrixX(wlm[2], wlm[2], rot_z);                /* 2928 */
        sceVu0RotMatrixY(wlm[2], wlm[2], 0.7853981f);           /* 2929 */
        sceVu0TransMatrix(wlm[2], wlm[2], wpos);                /* 2930 */

        sceVu0UnitMatrix(wlm[3]);                               /* 2931 */
        sceVu0RotMatrixY(wlm[3], wlm[3], rot_z);                /* 2932 */
        sceVu0RotMatrixZ(wlm[3], wlm[3], 0.7853981f);           /* 2933 */
        sceVu0TransMatrix(wlm[3], wlm[3], wpos);                /* 2934 */

        sceVu0UnitMatrix(wlm[4]);                               /* 2935 */
        sceVu0RotMatrixX(wlm[4], wlm[4], 0.7853981f);           /* 2936 */
        sceVu0RotMatrixZ(wlm[4], wlm[4], rot_z);                /* 2937 */
        sceVu0TransMatrix(wlm[4], wlm[4], wpos);                /* 2938 */

        Set3DPosTexure(wlm[1], pDrawEnv, 0x50,
                       size * 150.0f, size * 150.0f,
                       0xff, 0xff, 0xff, 0x50);                 /* 2940 */
        Set3DPosTexure(wlm[2], pDrawEnv, 0x52,
                       78.0f, 78.0f,
                       0xf0, 0xf0, 0xff, (u_char)(size * 64.0f)); /* 2941 */
        Set3DPosTexure(wlm[3], pDrawEnv, 0x52,
                       78.0f, 78.0f,
                       0xf0, 0xff, 0xf0, (u_char)(size * 64.0f)); /* 2942 */
        Set3DPosTexure(wlm[4], pDrawEnv, 0x52,
                       78.0f, 78.0f,
                       0xff, 0xf0, 0xf0, (u_char)(size * 64.0f)); /* 2943 */
    }

    pItemEffectData->Flow     = flow;                           /* 2945 */
    pItemEffectData->Rot      = rot_z;                          /* 2946 */
    pItemEffectData->fCounter = cnt;                            /* 2947 */
}                                                               /* 2948 */

/* 2984 / 3007 / 3030 / 3053 -- the Type 1 glint's four ramps.
 *
 * Four separate statics, each one statement, each fully inlined: the ROM's
 * line stream carries 3001 / 3024 / 3047 / 3070 once apiece rather than one
 * line four times, which is what proves they are four functions and not one
 * called four times.  The 23-line spacing between them is exact.
 *
 * The two size ramps divide by 100 here and the caller multiplies by 100
 * again -- the helpers hand back a rate and the draw scales a 100-unit base
 * sprite by it.  Kept as found; the round trip is not free but it is the
 * ROM's own shape.  The names are the port's. */
static float ItemEffectGetSizeRate0(const ITEM_EFFECT_DATA *pItemEffectData)
{
    return (float)ItemEffectGetChangeVal(pItemEffectData->ScaleCounter0,
                                         77, 22, 22) / 100.0f;  /* 3001 */
}

static float ItemEffectGetSizeRate1(const ITEM_EFFECT_DATA *pItemEffectData)
{
    return (float)ItemEffectGetChangeVal(pItemEffectData->ScaleCounter1,
                                         22, 59, 80) / 100.0f;  /* 3024 */
}

static int ItemEffectGetAlpha0(const ITEM_EFFECT_DATA *pItemEffectData)
{
    return ItemEffectGetChangeVal(pItemEffectData->AlphaCounter0,
                                  71, 32, 68);                  /* 3047 */
}

static int ItemEffectGetAlpha1(const ITEM_EFFECT_DATA *pItemEffectData)
{
    return ItemEffectGetChangeVal(pItemEffectData->AlphaCounter1,
                                  37, 25, 59);                  /* 3070 */
}

/* 3077 -- the Type 1 glint, and the one that actually runs.
 *
 * Two billboards on one camera-facing matrix, drawn small-then-large: a dark
 * blue core (texture 0x36) pinned at 22 units because its size ramp has
 * Min == Max, then a brighter halo (0x38) breathing between 59 and 80 over
 * it.  Neither is gated on EffWrkStopFlgGet(), so the glint keeps twinkling
 * while effects are frozen -- unlike the Type 0 pulse above.
 *
 * The two are exactly coplanar (Set3DPosTexureSub puts every corner at local
 * z = 0) and this file's DRAW_ENV has ZMSK clear, so both write depth.  That
 * is free on a 16-bit GS Z buffer, where the second quantises onto the first
 * and ZTST GEQUAL passes, and it self-fights on a float depth buffer -- see
 * the PORT DEVIATION in ApplyGsDrawEnv().
 *
 * ROM lines 3081..3120 hold no code: forty lines of comment or disabled work
 * between taking the camera and stepping the counters. */
static void ItemEffectDrawOneType1(ITEM_EFFECT_DATA *pItemEffectData,
                                   DRAW_ENV *pDrawEnv)
{                                                               /* 3077 */
    GRA3DCAMERA *pCam;
    float       *CamPos;
    float        RotX;
    float        RotY;
    float        LocalWorld[4][4];

    pCam   = gra3dGetCamera();                                  /* 3079 */
    CamPos = gra3dcamGetPosition();                             /* 3080 */

    /* Each wrap is 2 * the triangle's half-period, so every counter completes
     * a whole up-and-down cycle before it resets. */
    pItemEffectData->ScaleCounter0++;                           /* 3121 */
    if (pItemEffectData->ScaleCounter0 > 154)
    {
        pItemEffectData->ScaleCounter0 = 0;                     /* 3122 */
    }

    pItemEffectData->ScaleCounter1++;                           /* 3124 */
    if (pItemEffectData->ScaleCounter1 > 44)
    {
        pItemEffectData->ScaleCounter1 = 0;                     /* 3125 */
    }

    pItemEffectData->AlphaCounter0++;                           /* 3127 */
    if (pItemEffectData->AlphaCounter0 > 142)
    {
        pItemEffectData->AlphaCounter0 = 0;                     /* 3128 */
    }

    pItemEffectData->AlphaCounter1++;                           /* 3130 */
    if (pItemEffectData->AlphaCounter1 > 74)
    {
        pItemEffectData->AlphaCounter1 = 0;                     /* 3131 */
    }

    /* The four ramps are read before Get2PosRot(), which is where the ROM
     * emits them -- GCC does not move a call across another call, so that is
     * source order, not scheduling.  Whether they were named locals is not
     * recoverable: the stab list is exhaustive for ints and names neither
     * alpha, which argues they were written straight into the two
     * Set3DPosTexure() argument lists.  Kept as locals because the emission
     * order is the stronger signal; the two readings compile identically.  */
    {
        float Size0   = ItemEffectGetSizeRate0(pItemEffectData);
        float Size1   = ItemEffectGetSizeRate1(pItemEffectData);
        int   Alpha0  = ItemEffectGetAlpha0(pItemEffectData);
        int   Alpha1  = ItemEffectGetAlpha1(pItemEffectData);

        Get2PosRot(CamPos, pCam->vTarget, &RotX, &RotY);        /* 3139 */

        sceVu0UnitMatrix(LocalWorld);                           /* 3142 */
        sceVu0RotMatrixX(LocalWorld, LocalWorld, RotX);         /* 3143 */
        sceVu0RotMatrixY(LocalWorld, LocalWorld, RotY);         /* 3144 */
        sceVu0TransMatrix(LocalWorld, LocalWorld,
                          pItemEffectData->Position);           /* 3145 */

        Set3DPosTexure(LocalWorld, pDrawEnv, 0x36,
                       Size0 * 100.0f, Size0 * 100.0f,
                       0x36, 0x42, 0xb5, (u_char)Alpha0);    /* 3147 */
        Set3DPosTexure(LocalWorld, pDrawEnv, 0x38,
                       Size1 * 100.0f, Size1 * 100.0f,
                       0x64, 0x7b, 0xea, (u_char)Alpha1);    /* 3148 */
    }
}                                                               /* 3149 */

/* 3152 -- both draw envs are built whichever branch runs; the ROM copies both
 * .rodata images into the frame before it has even looked at Type.  They
 * differ only in ALPHA: 0x44 for the pulse, 0x48 for the glint. */
void ItemEffectDrawOne(int ItemNo)
{
    DRAW_ENV DrawEnv00 =                                        /* 3154 */
    {
        0x161,                      /* tex1  */
        0x44,                       /* alpha */
        0xa000118ULL,               /* zbuf  */
        0x5000dULL,                 /* test  */
        0,                          /* clamp */
        0x302a400000008004ULL,      /* prim  */
    };
    DRAW_ENV DrawEnv01 =                                        /* 3162 */
    {
        0x161,                      /* tex1  */
        0x48,                       /* alpha */
        0xa000118ULL,               /* zbuf  */
        0x5000dULL,                 /* test  */
        0,                          /* clamp */
        0x302a400000008004ULL,      /* prim  */
    };

    ITEM_EFFECT_DATA *pRetData = ItemEffectGetItemEffectData(ItemNo);

    if (pRetData == (ITEM_EFFECT_DATA *)nullptr)                /* 3173 */
    {
        return;
    }

    if (pRetData->Type == 0)                                    /* 3174 */
    {
        ItemEffectDrawOneType0(pRetData, &DrawEnv00);
    }
    else
    {
        ItemEffectDrawOneType1(pRetData, &DrawEnv01);
    }

    gra3dSetGsRegisterDefault();                                /* 3182 */
}

/* ---- STUB: needed by enemy.c ---- */

/* ==========================================================================
 *  The door-seal dissolve
 *
 *  A singleton the photo phase drives: Req() starts it, Draw() runs a frame,
 *  IsEnd() reports completion and EndProc() tears it down.  Status 0 is idle,
 *  which is exactly what IsEnd() tests -- so the stub that used to return a
 *  hardcoded 1 was returning the *correct* value for the idle state, and only
 *  wrong once a dissolve was actually running.
 *
 *  The dissolve borrows the camera, so it saves and restores it around itself
 *  rather than leaving the phase to do that.
 * ======================================================================== */


/* 5119 */
static void DoorSealDisappearInit(void)
{
    DoorSealDisappearCtrl.Status           = 0;                 /* 5121 */
    DoorSealDisappearCtrl.Counter          = 0;                 /* 5122 */
    DoorSealDisappearCtrl.AlphaRate        = 0.0f;              /* 5123 */
    DoorSealDisappearCtrl.DeformAlphaRate  = 0.0f;              /* 5124 */
}                                                               /* 5125 */

/* 5132 -- start it, once.  A second request while one is running is ignored,
 * which is what makes the effect a singleton.  The cue is *preloaded* rather
 * than played, so the stream is ready when the dissolve reaches its sound. */
void DoorSealDisappearReq(void)
{
    if (DoorSealDisappearCtrl.Status == 0)                      /* 5134 */
    {
        DoorSealDisappearCtrl.StreamId =
            StreamAutoPreload(0xa3f, 0xa3e, 0x11 /* event priority */,
                              0, 0, 0x3200, 0, (SND_3D_SET *)nullptr); /* 5136 */

        DoorSealDisappearCtrl.Status          = 1;              /* 5137 */
        DoorSealDisappearCtrl.Counter         = 0;              /* 5138 */
        DoorSealDisappearCtrl.AlphaRate       = 0.0f;           /* 5139 */
        DoorSealDisappearCtrl.DeformAlphaRate = 0.0f;           /* 5140 */
    }
}                                                               /* 5141 */

/* 5149 */
int DoorSealDisappearIsEnd(void)
{
    return (DoorSealDisappearCtrl.Status == 0);                 /* 5155 */
}                                                               /* 5157 */

/* 5164 -- tear down.  The stream is faded rather than stopped, so the tail of
 * the cue survives the phase change. */
void DoorSealDisappearEndProc(void)
{
    StreamAutoFadeOut(DoorSealDisappearCtrl.StreamId, 1);       /* 5166 */

    DoorSealDisappearCtrl.Status          = 0;                  /* 5167 */
    DoorSealDisappearCtrl.Counter         = 0;                  /* 5168 */
    DoorSealDisappearCtrl.AlphaRate       = 0.0f;               /* 5169 */
    DoorSealDisappearCtrl.DeformAlphaRate = 0.0f;               /* 5170 */
}                                                               /* 5171 */

/* 5178 -- the dissolve's clock, run once a frame from InitEffectOthEF().
 *
 * Seven stages, each an `if (Status == n)` in sequence rather than a switch, so
 * a stage that completes hands straight on to the next one inside the same
 * frame:
 *
 *   1  waiting for the cue to finish preloading
 *   2  the seal's own texture fades in            (10 frames)
 *   3  a hold, then the cue is played             (15 frames)
 *   4  the parts-deform fades in                  (30 frames)
 *   5  a hold                                     (35 frames)
 *   6  the seal fades out                         (62 frames)
 *   7  the deform fades out                      (105 frames)  -> Status 0
 *
 * The four ramp durations are block-scope locals in the ROM (functions.txt
 * lists SoulInTime, DeformInTime, SoulOutTime and DeformOutTime, three of them
 * sharing v0 because their scopes do not overlap); the two holds are bare
 * literals.
 *
 * Stage 7's test is `==`, not `>=`, unlike the other three ramps.  Harmless --
 * Counter only ever steps by one from zero -- but it is the ROM's own
 * asymmetry, so it is kept. */
static void DoorSealDisappearMain(DOOR_SEAL_DISAPPEAR_CTRL *pCtrl)
{
    if (pCtrl->Status == 1)                                     /* 5197 */
    {
        if (StreamAutoIsPreload(pCtrl->StreamId) != 0)          /* 5198 */
        {
            pCtrl->Status  = 2;                                 /* 5199 */
            pCtrl->Counter = 0;                                 /* 5200 */
        }
    }

    if (pCtrl->Status == 2)                                     /* 5203 */
    {
        int SoulInTime = 10;

        if (pCtrl->Counter >= SoulInTime)                       /* 5204 */
        {
            pCtrl->Status    = 3;                               /* 5205 */
            pCtrl->AlphaRate = 1.0f;                            /* 5207 */
            pCtrl->Counter   = 0;
        }
        else
        {
            pCtrl->AlphaRate = (float)pCtrl->Counter /
                               (float)SoulInTime;               /* 5211 */
            pCtrl->Counter++;                                   /* 5216 */
        }
    }

    if (pCtrl->Status == 3)                                     /* 5219 */
    {
        if (pCtrl->Counter >= 15)                               /* 5220 */
        {
            pCtrl->Status  = 4;                                 /* 5221 */
            pCtrl->Counter = 0;                                 /* 5222 */

            StreamAutoPreloadPlay(pCtrl->StreamId);             /* 5223 */
        }
        else
        {
            pCtrl->Counter++;                                   /* 5226 */
        }
    }

    if (pCtrl->Status == 4)                                     /* 5229 */
    {
        int DeformInTime = 30;

        if (pCtrl->Counter >= DeformInTime)                     /* 5230 */
        {
            pCtrl->Status          = 5;                         /* 5231 */
            pCtrl->DeformAlphaRate = 1.0f;                      /* 5233 */
            pCtrl->Counter         = 0;
        }
        else
        {
            pCtrl->DeformAlphaRate = (float)pCtrl->Counter /
                                     (float)DeformInTime;       /* 5237 */
            pCtrl->Counter++;                                   /* 5242 */
        }
    }

    if (pCtrl->Status == 5)                                     /* 5245 */
    {
        if (pCtrl->Counter >= 35)                               /* 5246 */
        {
            pCtrl->Status  = 6;                                 /* 5247 */
            pCtrl->Counter = 0;                                 /* 5248 */
        }
        else
        {
            pCtrl->Counter++;                                   /* 5251 */
        }
    }

    if (pCtrl->Status == 6)                                     /* 5254 */
    {
        int SoulOutTime = 62;

        if (pCtrl->Counter >= SoulOutTime)                      /* 5255 */
        {
            pCtrl->Status    = 7;                               /* 5256 */
            pCtrl->Counter   = 0;                               /* 5257 */
            pCtrl->AlphaRate = 0.0f;                            /* 5258 */
        }
        else
        {
            pCtrl->AlphaRate = 1.0f - (float)pCtrl->Counter /
                                      (float)SoulOutTime;       /* 5262 */
            pCtrl->Counter++;                                   /* 5267 */
        }
    }

    if (pCtrl->Status == 7)                                     /* 5270 */
    {
        int DeformOutTime = 105;

        if (pCtrl->Counter == DeformOutTime)                    /* 5271 */
        {
            pCtrl->Status          = 0;                         /* 5272 */
            pCtrl->DeformAlphaRate = 0.0f;                      /* 5274 */
            pCtrl->Counter         = 0;
        }
        else
        {
            pCtrl->DeformAlphaRate = 1.0f - (float)pCtrl->Counter /
                                            (float)DeformOutTime;   /* 5278 */
            pCtrl->Counter++;                                   /* 5283 */
        }
    }
}

/* 5299 / 5313 -- the camera save/restore pair.  Note the restore ends with
 * gra3dApplyCamera(NULL, 0), which is what actually pushes the four restored
 * values back into the live camera. */
static void DoorSealDisappearCameraBackup(DOOR_SEAL_CAMERA_BACKUP *pBakBuf)
{
    g3dxVu0CopyVector(pBakBuf->Position, gra3dcamGetPosition()); /* 5301 */
    g3dxVu0CopyVector(pBakBuf->Target,   gra3dcamGetTarget());   /* 5302 */

    pBakBuf->Fov  = gra3dcamGetFov();                           /* 5304 */
    pBakBuf->Roll = gra3dcamGetRoll();                          /* 5305 */
}                                                               /* 5306 */

static void DoorSealDisappearCameraReturn(DOOR_SEAL_CAMERA_BACKUP *pBakBuf)
{
    gra3dcamSetPosition(pBakBuf->Position);                     /* 5315 */
    gra3dcamSetTarget(pBakBuf->Target, 1);                      /* 5316 */
    gra3dcamSetRoll(pBakBuf->Roll);                             /* 5317 */
    gra3dcamSetFov(pBakBuf->Fov);                               /* 5318 */

    gra3dApplyCamera((GRA3DCAMERA *)nullptr, 0);
}

/* 5516 -- where the seal's texture sits.  A fixed point 12 units up. */
static void DoorSealDisappearTexPosGet(float *Position)
{
    Position[0] = 0.0f;                                         /* 5518 */
    Position[1] = 12.0f;                                        /* 5519 */
    Position[2] = 0.0f;                                         /* 5520 */
    Position[3] = 1.0f;                                         /* 5521 */
}                                                               /* 5529 */

/* 5436 -- one frame of the dissolve.
 *
 * The camera is borrowed for the duration and put back at the end, so the seal
 * is composited from its own fixed viewpoint whatever the room camera is doing.
 * Between those two the seal is drawn face-on (rotated by pi about X, which is
 * what puts the texture the right way up under this camera), the screen dither
 * is driven from the deform's own alpha, and the two parts-deform records are
 * rebuilt and submitted -- blur first, so it lands underneath. */
void DoorSealDisappearDraw(void)
{
    /* data 2fc678 -- a file-scope static of this function.  TEST 0x3000d is
       ATST GREATER / 0x0d with ZTST ALWAYS: the seal is not depth tested,
       because under the borrowed camera there is nothing to test against. */
    static DRAW_ENV DrawEnv =
    {
        0x161,                      /* tex1  */
        0x48,                       /* alpha */
        0x10a000118ULL,             /* zbuf  */
        0x3000dULL,                 /* test  */
        0,                          /* clamp */
        0x302a400000008004ULL,      /* prim  */
    };

    float LocalWorld[4][4];
    float BasePos[4];
    float DeformAlpha;
    DOOR_SEAL_CAMERA_BACKUP CameraBak;

    if (DoorSealDisappearCtrl.Status != 0)                      /* 5439 */
    {
        DoorSealDisappearCameraBackup(&CameraBak);
        DoorSealDisappearCameraSet();

        DoorSealDisappearTexPosGet(BasePos);

        sceVu0UnitMatrix(LocalWorld);
        sceVu0RotMatrixX(LocalWorld, LocalWorld, 3.1415925f);
        sceVu0TransMatrix(LocalWorld, LocalWorld, BasePos);

        Set3DPosTexure(LocalWorld, &DrawEnv, 0x48,
                       300.0f, 455.999969f,
                       0xa1, 0xb5, 0xd8,
                       (u_char)(int)(DoorSealDisappearCtrl.AlphaRate * 97.0f));

        SubDither3(1, DoorSealDisappearCtrl.DeformAlphaRate * 64.0f,
                   24.0f, 0x40, 0x40);

        DeformAlpha = DoorSealDisappearCtrl.DeformAlphaRate * 77.0f;

        g3dxVu0CopyVector(DoorSealDisappearCtrl.BasePos, BasePos);

        DoorSealDisappearPDeformBlurParamSet(
            &DoorSealDisappearCtrl.EffectContBlur,
            &DoorSealDisappearCtrl.BasePos,
            (int)(DoorSealDisappearCtrl.DeformAlphaRate * 85.0f));
        SetPartsDeform(&DoorSealDisappearCtrl.EffectContBlur);

        DoorSealDisappearPDeformParamSet(
            &DoorSealDisappearCtrl.EffectCont,
            &DoorSealDisappearCtrl.BasePos,
            (int)DeformAlpha,
            &DoorSealDisappearCtrl.DeformSpeed,
            &DoorSealDisappearCtrl.DeformRate);
        SetPartsDeform(&DoorSealDisappearCtrl.EffectCont);

        DoorSealDisappearCameraReturn(&CameraBak);
    }
}


/* ==========================================================================
 *  The candle flame
 *
 *  Effect id 0x15 is the flame proper and 0x16 the amulet fire.  The three
 *  Call* front doors differ only in how much of the flame is authored: the
 *  amulet fire takes none of it, and the two-colour forms take an inner and an
 *  outer colour with a scale each.
 * ======================================================================== */

/* 468 -- the amulet fire.  Every one of r/g/b/scale is dropped on the floor;
 * `rate` is a function-local static (globals.txt does not list it) holding a
 * constant 1.0f, and the same pointer is handed over twice. */
void* CallFire(void *pos, u_char r, u_char g, u_char b, float scale)
{
    static float rate = 1.0f;                                   /* sdata 3efe70 */

    (void)r; (void)g; (void)b; (void)scale;

    return SetEffects_TORCH(2, 1, pos, &rate, &rate);           /* 471 */
}

/* 487 -- the two-colour flame.  Mode 3 is "both colours authored". */
void* CallFire2(void *pos, u_char r, u_char g, u_char b, float scl, u_char r2, u_char g2, u_char b2, float scl2)
{
    return SetEffects_FIRE(2, 3, pos, r, g, b, scl,
                           r2, g2, b2, scl2);                   /* 488 */
}

/* 505 -- the same, with the mode exposed: a non-zero `type` asks for mode 0
 * (the plain flame) instead of mode 3. */
void* CallFire3(void *pos, int type, u_char r, u_char g, u_char b, float scl, u_char r2, u_char g2, u_char b2, float scl2)
{
    return SetEffects_FIRE(2, (type != 0) ? 0 : 3, pos, r, g, b, scl,
                           r2, g2, b2, scl2);                   /* 506 */
}

/* 519 -- one frame of a candle flame.
 *
 * A flame is *two* quads drawn on top of each other from the same animation
 * table, the second one five frames behind the first and at a lower alpha.
 * That lag is the whole trick: the trailing copy smears the leading one and the
 * flame reads as continuous motion rather than a 150-frame flipbook.
 *
 * `flow` is the state, and it is a small machine:
 *
 *   0   just created -- both quads flat at scale 1, hands straight on to 3
 *   1   flaring up   -- scale ramps over 7 frames        (FlareUpReq's entry)
 *   2   settling     -- scale ramps back over 10 frames
 *   3   burning      -- the 150-frame table, looping
 *   4   guttering    -- one quad only, stepping through a strip of textures
 *   5   recovering   -- the 75-frame table, once, then back to 3
 *
 * Only flow 3 and 5 read the scale tables; the rest compute their scale
 * directly.  `in` is the trailing quad's alpha ramp and `keep` the gutter's
 * texture index.
 *
 * The whole flame is drawn rotated pi about X, which is what stands
 * CandlePolyDat the right way up, and only the Y rotation follows the camera --
 * so a flame never leans.
 *
 * ROM QUIRK, reproduced: the gutter's texture index adds the monochrome offset
 * twice, once into the local and again at each use.  Both entries of tex0[]
 * come out identical, and since flow 4 draws a single quad only tex0[0] is ever
 * read -- so it never shows. */
static void SubCandleFlame(EFFECT_CONT *ec)
{
    float TmpMatrix[4][4];
    float LocalWorld[4][4];
    float LocalScreen[2][4][4];
    float ScaleMatrix[2][4][4];
    sceVu0IVECTOR ivec[2][4];
    float vpos[4];
    float FreaPos[4];

    /* A local copy of the flame quad -- the same values as CandlePolyDat, from
       this function's own .rodata blob. */
    float wpos[4][4] =                                          /* 525 */
    {
        {  0.965643f, -0.136467f,   0.0f, 1.0f },
        {  0.965643f,  1.93681788f, 0.0f, 1.0f },
        { -0.965643f, -0.136467f,   0.0f, 1.0f },
        { -0.965643f,  1.93681788f, 0.0f, 1.0f },
    };

    float ScaleAnimeW[2];
    float ScaleAnimeH[2];
    int   Alpha[2];
    float rot_x;
    float rot_y;
    u_long tex0[2];
    DRAW_ENV_5 env;
    int   DispFlameNum;
    u_char mr, mg, mb;
    float Scale;
    GRA3DCAMERA *pCam;
    float *cam_pos;
    int   ClipFlg;
    int   i;
    int   j;
    int   n;
    int   type;
    int   loop;

    static float RandVal;                                       /* sdata 3efe74 */

    DispFlameNum = 2;

    pCam    = gra3dGetCamera();
    cam_pos = gra3dcamGetPosition();

    if (EffWrkMonochroModeGet() == 0)
    {
        mr = ec->dat.uc8[2];
        mg = ec->dat.uc8[3];
        mb = ec->dat.uc8[4];
    }
    else
    {
        mr = (u_char)(((int)ec->dat.uc8[2] + (int)ec->dat.uc8[3] +
                       (int)ec->dat.uc8[4]) / 3);
        mg = mr;
        mb = mr;
    }

    Scale = ec->dat.fl32[2];

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 552 */
    {
        ResetEffects(ec);
        return;
    }

    /* fw[1] and fw[2] carry last frame's scale across; FlareUpReq() writes 25
       into both to start a flare. */
    for (i = 0; i < 2; i++)
    {
        ScaleAnimeW[i] = ec->fw[1];
        ScaleAnimeH[i] = ec->fw[2];
    }

    switch (ec->flow)                                           /* 573 */
    {
    case 0:
        DispFlameNum = 2;
        for (i = 0; i < 2; i++)
        {
            ScaleAnimeW[i] = 1.0f;
            ScaleAnimeH[i] = 1.0f;
            Alpha[i]       = 0x80;
        }
        break;

    case 1:
        DispFlameNum = 2;
        ScaleAnimeW[0] = ((float)ec->cnt * 20.0f) / 6.0f + 25.0f;
        ScaleAnimeH[0] = ((float)ec->cnt * 35.0f) / 6.0f + 25.0f;
        for (i = 0; i < 2; i++)
        {
            ScaleAnimeW[1] = ScaleAnimeW[0];
            ScaleAnimeH[1] = ScaleAnimeH[0];
            Alpha[i]       = 0x80;
        }
        break;

    case 2:
        DispFlameNum = 2;
        ScaleAnimeW[0] = ((float)ec->cnt * -20.0f) / 9.0f + 45.0f;
        ScaleAnimeH[0] = ((float)ec->cnt * -35.0f) / 9.0f + 60.0f;
        for (i = 0; i < 2; i++)
        {
            ScaleAnimeW[1] = ScaleAnimeW[0];
            ScaleAnimeH[1] = ScaleAnimeH[0];
            Alpha[i]       = 0x80;
        }
        break;

    case 3:
    case 5:
        DispFlameNum = 2;

        loop = (ec->flow == 3) ? 150 : 75;
        type = (ec->flow != 3) ? 1 : 0;

        /* The trailing quad is five frames behind, wrapped. */
        j = (int)ec->cnt - 5;
        if (j < 0)
        {
            j += loop;
        }

        ScaleAnimeW[0] = CandleFlameScaleWGet(type, (int)ec->cnt) * 25.0f;
        ScaleAnimeH[0] = CandleFlameScaleHGet(type, (int)ec->cnt) * 25.0f;
        ScaleAnimeW[1] = CandleFlameScaleWGet(type, j) * 25.0f;
        ScaleAnimeH[1] = CandleFlameScaleHGet(type, j) * 25.0f;

        Alpha[0] = 0x80;

        /* `in` runs 0..19: the trailing quad dips to 0x40 over the first ten
           frames and climbs back over the next ten. */
        if (ec->in < 10)
        {
            Alpha[1] = (int)(ec->in * -0x40) / 10 + 0x80;
        }
        else
        {
            Alpha[1] = (int)((ec->in - 10) * 0x40) / 10 + 0x40;
        }
        break;

    case 4:
        for (i = 0; i < 2; i++)
        {
            ScaleAnimeW[i] = 25.0f;
            ScaleAnimeH[i] = 25.0f;
            Alpha[i]       = 0x80;
        }
        DispFlameNum = 1;
        break;
    }

    g3dxVu0CopyVector(vpos, (float *)ec->pnt[0]);

    Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);

    sceVu0UnitMatrix(LocalWorld);
    sceVu0RotMatrixX(LocalWorld, LocalWorld, 3.1415925f);
    sceVu0RotMatrixY(LocalWorld, LocalWorld, rot_y);
    sceVu0TransMatrix(LocalWorld, LocalWorld, vpos);

    sceVu0MulMatrix(TmpMatrix, pCam->matWorldScreen, LocalWorld);

    for (i = 0; i < DispFlameNum; i++)
    {
        sceVu0UnitMatrix(ScaleMatrix[i]);
        ScaleMatrix[i][0][0] = ScaleAnimeW[i] * Scale;
        ScaleMatrix[i][1][1] = ScaleAnimeH[i] * Scale;
        ScaleMatrix[i][2][2] = ScaleAnimeW[i] * Scale;

        sceVu0MulMatrix(LocalScreen[i], TmpMatrix, ScaleMatrix[i]);
    }

    for (i = 0; i < DispFlameNum; i++)
    {
        for (j = 0; j < 4; j++)
        {
            sceVu0RotTransPers(ivec[i][j], LocalScreen[i], wpos[j], 1);
        }
    }

    /* Only the leading quad is guard-band tested; the trailing one follows it. */
    ClipFlg = 0;

    for (j = 0; j < 4; j++)
    {
        if ((u_int)(ivec[0][j][0] - 0x4000) > 0x8000)
        {
            ClipFlg = 1;
        }
        if ((u_int)(ivec[0][j][1] - 0x4000) > 0x8000)
        {
            ClipFlg = 1;
        }
        if ((u_int)(ivec[0][j][2] - 0xff) > 0xfffff00)
        {
            ClipFlg = 1;
        }
    }

    if (ClipFlg == 0)
    {
        env.alpha = 0x48;
        env.tex1  = 0x161;
        env.clamp = 0;
        env.test  = 0x5000dULL;
        env.zbuf  = 0x10a000118ULL;

        SetDrawEnv(0, &env);

        if (EffWrkStopFlgGet() == 0)
        {
            RandVal = (float)(int)EffectGetRandom(0.0f, 8.0f);
        }

        g3dxVu0CopyVector(FreaPos, vpos);
        FreaPos[1] += -29.9999981f;

        DrawFrea(FreaPos, Scale, (int)ec->z, mr, mg, mb,
                 (int)(RandVal * 0.5f + 5.0f));

        if (ec->flow == 4)                                      /* 706 */
        {
            n = (int)ec->keep * 2 + EffWrkMonochroModeGet() + 0x12;

            tex0[0] = effdat[n + EffWrkMonochroModeGet()].tex0;  /* 708 */
            tex0[1] = effdat[n + EffWrkMonochroModeGet()].tex0;  /* 709 */
        }
        else
        {
            tex0[0] = effdat[EffWrkMonochroModeGet() + 0x30].tex0;   /* 702 */
            tex0[1] = effdat[EffWrkMonochroModeGet() + 0x32].tex0;   /* 703 */
        }

        for (i = 0; i < DispFlameNum; i++)                      /* 712 */
        {
            /* PORT: the packet below is inert.  LocalScreen[i] is
               matWorldScreen * (LocalWorld * ScaleMatrix[i]), so the
               local->world half is that same product without the camera --
               sceVu0MulMatrix(m0, m1, m2) being m0 = m2 * m1. */
            float FlameWorld[4][4];

            sceVu0MulMatrix(FlameWorld, LocalWorld, ScaleMatrix[i]);
            CandleFlameHostQuad(FlameWorld, tex0[i], mr, mg, mb, Alpha[i]);

            CandleFlameMakePacket(ivec[i], tex0[i], ec->z,
                                  mr, mg, mb, Alpha[i]);
        }

        ec->fw[1] = ScaleAnimeW[0];
        ec->fw[2] = ScaleAnimeH[0];
    }

    if (EffWrkStopFlgGet() == 0)
    {
        ec->cnt++;
    }

    switch (ec->flow)
    {
    case 0:
        ec->in   = 0;
        ec->flow = 3;
        ec->cnt  = 0;
        break;

    case 1:
        if (ec->cnt >= 7)
        {
            ec->cnt  = 0;
            ec->flow = 2;
        }
        break;

    case 2:
        if (ec->cnt >= 10)
        {
            ec->in   = 0;
            ec->flow = 3;
            ec->cnt  = 0;
        }
        break;

    case 3:
    case 5:
        loop = (ec->flow == 3) ? 150 : 75;

        if (ec->cnt >= (u_int)loop)
        {
            ec->cnt = 0;

            if (ec->flow == 5)
            {
                ec->flow = 3;
            }
        }

        ec->in++;

        if (ec->in >= 20)
        {
            ec->in = 0;
        }
        break;

    case 4:
        if (ec->cnt > 1)
        {
            ec->cnt = 0;
            ec->keep++;
        }

        if (ec->keep >= 15)
        {
            ec->keep = 0;
            ec->flow = 5;
            ec->in   = 0;
            ec->cnt  = 0;
        }
        break;
    }
}

/* PORT-ONLY.  The host draw for one candle flame quad.
 *
 * CandleFlameMakePacket() below builds the ROM's DIRECT packet, which dmaVif1
 * collects and never executes, so the same quad is queued here from its
 * world-space corners.  Going through the world rather than through the
 * already-projected ivec is what gets it depth-tested against the room, which
 * is what the callers' draw env (TEST 0x5000d -- ZTE, ZTST GEQUAL) asks for;
 * ProjectWorldToHostClip() also runs it through the widened clip matrix, so it
 * sits with the 3D scene on any aspect.
 *
 * `st` is CandleFlameMakePacket()'s own table and cannot be spelled as a
 * RendererPacket3D sub-rectangle: that form runs u along v0->v1, which for
 * CandlePolyDat is the quad's *y* axis (v0 and v1 are one column, not one
 * row), so it draws the flame on its side.  Here u follows local +x and v
 * local -y, as the ROM's ST does. */
static void CandleFlameHostQuad(float (*pLocalWorld)[4], u_long Tex0,
                                int R, int G, int B, int Alpha)
{
    static const float st[8] = {
        0.99309695f,   0.99309695f,
        0.99309695f,   0.0069029997f,
        0.0069029997f, 0.99309695f,
        0.0069029997f, 0.0069029997f,
    };
    float world[4][4];
    int   i;

    for (i = 0; i < 4; i++)
    {
        sceVu0ApplyMatrix(world[i], pLocalWorld, CandlePolyDat[i]);
    }

    RendererPacket3DUV(world, 4, st, R, G, B, Alpha,
                       (const sceGsTex0 *)&Tex0);
}

/* 834 -- the flame's own GIF packet: one four-vertex textured strip.
 *
 * Every vertex takes the caller's single Depth rather than its own projected z,
 * so the whole quad sits at one depth -- which is what stops a flame leaning
 * away from the camera from cutting into the candle it sits on.
 *
 * The ST inset of 0.0069 is half a texel at 72 pixels, and the corner order
 * runs 1,1 / 1,0 / 0,1 / 0,0 to match CandlePolyDat's winding. */
static void CandleFlameMakePacket(sceVu0IVECTOR *pIvec, u_long Tex0, u_int Depth, int R, int G, int B, int Alpha)
{
    float st[4][2];
    U32DATA ts, tt, tq;
    Q_WORDDATA *pbuf;
    int ndpkt;
    int i;

    st[0][0] = 0.99309695f;   st[0][1] = 0.99309695f;
    st[1][0] = 0.99309695f;   st[1][1] = 0.0069029997f;
    st[2][0] = 0.0069029997f; st[2][1] = 0.99309695f;
    st[3][0] = 0.0069029997f; st[3][1] = 0.0069029997f;

    pbuf = StartDmaDirectTrans();
    Reserve2DPacket(0x10);

    pbuf[0].ul64[0] = 0x1000000000008002ULL;
    pbuf[0].ul64[1] = 0x0e;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = 0x3f;
    pbuf[2].ul64[0] = Tex0;
    pbuf[2].ul64[1] = 0x06;

    /* PRIM 0x5c: TRIANGLE_STRIP, gouraud, textured, alpha blended. */
    pbuf[3].ul64[0] = 0x302e400000008004ULL;
    pbuf[3].ul64[1] = 0x412;

    ndpkt = 4;

    for (i = 0; i < 4; i++)
    {
        tq.fl32 = 1.0f / (float)pIvec[i][3];
        ts.fl32 = tq.fl32 * st[i][0];
        tt.fl32 = tq.fl32 * st[i][1];

        pbuf[ndpkt].ui32[0] = (u_int)ts.ui32;
        pbuf[ndpkt].ui32[1] = (u_int)tt.ui32;
        pbuf[ndpkt].ui32[2] = (u_int)tq.ui32;
        pbuf[ndpkt].ui32[3] = 0;

        pbuf[ndpkt + 1].iv[0] = R;
        pbuf[ndpkt + 1].iv[1] = G;
        pbuf[ndpkt + 1].iv[2] = B;
        pbuf[ndpkt + 1].iv[3] = Alpha;

        pbuf[ndpkt + 2].iv[0]   = pIvec[i][0];
        pbuf[ndpkt + 2].iv[1]   = pIvec[i][1];
        pbuf[ndpkt + 2].ui32[2] = Depth;
        /* ADC: the first two vertices only prime the strip. */
        pbuf[ndpkt + 2].ui32[3] = (i < 2) ? 0x8000 : 0;

        ndpkt += 3;
    }

    EndDmaDirectTrans(pbuf + 0x10);
}

/* 1205 -- lay out one gathering of candles.
 *
 * The positions come straight out of the pak file in model units and are
 * scaled by 25 here, with y and z *negated* -- the authored data is in the
 * model's own handedness and this is where it is flipped into world space.
 *
 * Each candle's flicker counter starts somewhere random in the 150-frame cycle,
 * which is what stops a whole altar of candles pulsing in unison. */
static void ManyCandleCtrlInit(MANY_CANDLE_CTRL *pMc, float (*pCenterPos)[4], float (*pParticlePos)[4], int DataNum, int Id)
{
    int i;

    if (pMc != nullptr)                                         /* 1208 */
    {
        g3dxVu0CopyVector(pMc->CenterPos, *pCenterPos);

        pMc->Id      = Id;
        pMc->R       = 0x80;
        pMc->G       = 0x80;
        pMc->B       = 0x82;
        pMc->DataNum = DataNum;

        for (i = 0; i < pMc->DataNum; i++)                      /* 1218 */
        {
            g3dxVu0CopyVector(pMc->Particles[i].Position, pParticlePos[i]);

            pMc->Particles[i].Position[0] *=  25.0f;
            pMc->Particles[i].Position[1] *= -25.0f;
            pMc->Particles[i].Position[2] *= -25.0f;

            pMc->Particles[i].Alpha = 0;
            pMc->Particles[i].Count = (int)EffectGetRandom(0.0f, 149.0f);
        }
    }
}

/* 1240 -- draw a whole gathering of candles, then step it.
 *
 * Each candle is *two* quads: the flame itself, and a soft flare fifty units
 * above it.  They use different bases -- the flame turns about Y only, so it
 * stands upright, while the flare is a full camera-facing billboard -- and
 * that is why the flare keeps its round shape when the camera looks down while
 * the flame stays vertical.
 *
 * Both quads share one clip flag, so a candle whose flare has left the screen
 * takes its flame with it.  That is the ROM's own coupling and it is what keeps
 * the two halves of a candle from separating at the screen edge.
 *
 * The candles are depth sorted back to front on the flame's first corner, and
 * each is drawn flare-first so the flame composites over it.  The flame is
 * always at alpha 0x7f; only the flare's alpha flickers.
 *
 * 480 = 120 candles x 4 corners, which is exactly the particle array's size. */
static void *ManyCandleCtrl(MANY_CANDLE_CTRL *pCandleCtrl)
{
    GRA3DCAMERA *pCam;
    float *cam_pos;
    float WorkMat[4][4];
    float LocalWorld[4][4];
    float LocalScreen[4][4];
    float BaseMat[4][4];
    float FreaBaseMat[4][4];
    float FlameLocalWorld[4][4];            /* PORT: host draw, see below */
    float FreaLocalWorld[4][4];             /* PORT */
    float RotX;
    float RotY;
    fixed_array<sceVu0IVECTOR, 480> ivec;
    fixed_array<sceVu0IVECTOR, 480> ivecFrea;
    fixed_array<int, 120> Order;
    fixed_array<int, 120> ToPolyNo;
    DRAW_ENV_5 env;
    float FreaPosition[4];
    int PolyNo;
    int ColorR;
    int ColorG;
    int ColorB;
    u_long Tex0;
    u_long FreaTex0;
    int ClipFlg;
    int i;
    int j;
    int k;

    pCam    = gra3dGetCamera();                                 /* 1247 */
    cam_pos = gra3dcamGetPosition();

    PolyNo   = 0;
    Tex0     = effdat[EffWrkMonochroModeGet() + 0x30].tex0;
    FreaTex0 = effdat[EffWrkMonochroModeGet() + 0x4c].tex0;

    if (pCandleCtrl == nullptr)
    {
        return nullptr;
    }

    if (EffWrkMonochroModeGet() == 0)
    {
        ColorR = pCandleCtrl->R;
        ColorG = pCandleCtrl->G;
        ColorB = pCandleCtrl->B;
    }
    else
    {
        ColorR = (pCandleCtrl->R + pCandleCtrl->G + pCandleCtrl->B) / 3;
        ColorG = ColorR;
        ColorB = ColorR;
    }

    env.alpha = 0x48;
    env.tex1  = 0x161;
    env.clamp = 0;
    env.test  = 0x5000dULL;
    env.zbuf  = 0x10a000118ULL;

    SetDrawEnv(0, &env);

    Get2PosRot(cam_pos, pCam->vTarget, &RotX, &RotY);

    /* The flame's base: Y only, and a negative height because CandlePolyDat's
       y runs downward. */
    sceVu0UnitMatrix(WorkMat);
    WorkMat[0][0] =  25.0f;
    WorkMat[1][1] = -25.0f;
    WorkMat[2][2] =  25.0f;
    sceVu0RotMatrixY(BaseMat, WorkMat, RotY);

    /* The flare's base: a full billboard. */
    sceVu0UnitMatrix(WorkMat);
    WorkMat[0][0] = 25.0f;
    WorkMat[1][1] = 25.0f;
    WorkMat[2][2] = 25.0f;
    sceVu0RotMatrixX(WorkMat, WorkMat, RotX);
    sceVu0RotMatrixY(FreaBaseMat, WorkMat, RotY);

    for (i = 0; i < pCandleCtrl->DataNum; i++)                  /* 1272 */
    {
        ClipFlg = 0;

        /* ---- the flame ---------------------------------------------- */
        sceVu0UnitMatrix(WorkMat);
        WorkMat[0][0] =
            CandleFlameScaleData[pCandleCtrl->Particles[i].Count][0] * 1.0f;
        WorkMat[1][1] =
            CandleFlameScaleData[pCandleCtrl->Particles[i].Count][1] * 1.0f;
        WorkMat[2][2] = WorkMat[0][0];

        sceVu0MulMatrix(WorkMat, BaseMat, WorkMat);
        sceVu0TransMatrix(LocalWorld, WorkMat,
                          pCandleCtrl->Particles[i].Position);
        sceVu0MulMatrix(LocalScreen, pCam->matWorldScreen, LocalWorld);
        /* PORT: kept for the host draw at the accept below -- LocalWorld is
           reused by the flare a few lines down. */
        sceVu0CopyMatrix(FlameLocalWorld, LocalWorld);

        for (j = 0; j < 4; j++)
        {
            sceVu0RotTransPers(ivec[PolyNo * 4 + j], LocalScreen,
                               CandlePolyDat[j], 0);

            if (ivec[PolyNo * 4 + j][0] < 0x4000 ||
                ivec[PolyNo * 4 + j][0] > 0xc000)
            {
                ClipFlg = 1;
            }
            if (ivec[PolyNo * 4 + j][1] < 0x4000 ||
                ivec[PolyNo * 4 + j][1] > 0xc000)
            {
                ClipFlg = 1;
            }
            if (ivec[PolyNo * 4 + j][2] < 0xff ||
                ivec[PolyNo * 4 + j][2] > 0xffffff)
            {
                ClipFlg = 1;
            }
        }

        /* ---- the flare, fifty units up ------------------------------- */
        g3dxVu0CopyVector(FreaPosition, pCandleCtrl->Particles[i].Position);
        FreaPosition[1] += 50.0f;

        sceVu0UnitMatrix(WorkMat);
        WorkMat[0][0] = 3.0f;
        WorkMat[1][1] = 3.0f;
        WorkMat[2][2] = 3.0f;

        sceVu0MulMatrix(WorkMat, FreaBaseMat, WorkMat);
        sceVu0TransMatrix(LocalWorld, WorkMat, FreaPosition);
        sceVu0MulMatrix(LocalScreen, pCam->matWorldScreen, LocalWorld);
        sceVu0CopyMatrix(FreaLocalWorld, LocalWorld);           /* PORT */

        for (j = 0; j < 4; j++)
        {
            sceVu0RotTransPers(ivecFrea[PolyNo * 4 + j], LocalScreen,
                               CandlePolyDat[j], 0);

            if (ivecFrea[PolyNo * 4 + j][0] < 0x4000 ||
                ivecFrea[PolyNo * 4 + j][0] > 0xc000)
            {
                ClipFlg = 1;
            }
            if (ivecFrea[PolyNo * 4 + j][1] < 0x4000 ||
                ivecFrea[PolyNo * 4 + j][1] > 0xc000)
            {
                ClipFlg = 1;
            }
            if (ivecFrea[PolyNo * 4 + j][2] < 0xff ||
                ivecFrea[PolyNo * 4 + j][2] > 0xffffff)
            {
                ClipFlg = 1;
            }
        }

        if (ClipFlg == 0)
        {
            ToPolyNo[PolyNo] = i;
            PolyNo++;

            /* PORT: the sorted loop below builds only inert packets, and it
               is the one place LocalWorld is no longer available.  Drawing
               here instead is safe because the blend is additive (ALPHA 0x48
               = Cs*As + Cd), so the back-to-front sort it applies does not
               change the result. */
            CandleFlameHostQuad(FreaLocalWorld, FreaTex0,
                                ColorR, ColorG, ColorB,
                                pCandleCtrl->Particles[i].FreaAlpha);
            CandleFlameHostQuad(FlameLocalWorld, Tex0,
                                ColorR, ColorG, ColorB, 0x7f);
        }
    }

    for (i = 0; i < PolyNo; i++)
    {
        Order[i] = i;
    }

    for (i = 0; i < PolyNo - 1; i++)
    {
        for (j = i + 1; j < PolyNo; j++)
        {
            if (ivec[Order[j] * 4][2] < ivec[Order[i] * 4][2])
            {
                k        = Order[j];
                Order[j] = Order[i];
                Order[i] = k;
            }
        }
    }

    for (i = 0; i < PolyNo; i++)
    {
        CandleFlameMakePacket(&ivecFrea[Order[i] * 4], FreaTex0,
                              (u_int)ivecFrea[Order[i] * 4][2],
                              ColorR, ColorG, ColorB,
                              pCandleCtrl->Particles[ToPolyNo[Order[i]]].FreaAlpha);

        CandleFlameMakePacket(&ivec[Order[i] * 4], Tex0,
                              (u_int)ivec[Order[i] * 4][2],
                              ColorR, ColorG, ColorB, 0x7f);
    }

    if (EffWrkStopFlgGet() == 0)
    {
        ManyCandleParticleUpdate(pCandleCtrl);
    }

    return pCandleCtrl;                                         /* 1382 */
}

/* 1388 -- step every candle's flicker.
 *
 * The counter runs 0..149 and wraps -- it indexes CandleFlameScaleData[] -- and
 * the flare's own alpha is re-rolled in 17..21 every frame, which is the
 * high-frequency twinkle on top of the slow scale animation. */
static void ManyCandleParticleUpdate(MANY_CANDLE_CTRL *pCandleCtrl)
{
    int i;

    for (i = 0; i < pCandleCtrl->DataNum; i++)                  /* 1391 */
    {
        pCandleCtrl->Particles[i].Count++;

        if (pCandleCtrl->Particles[i].Count > 149)
        {
            pCandleCtrl->Particles[i].Count = 0;
        }

        pCandleCtrl->Particles[i].FreaAlpha =
            (int)EffectGetRandom(17.0f, 21.0f);
    }
}

/* 1414 -- draw one candle flame at frame `Count` of its animation.
 *
 * Only the Y rotation is applied, not the X: a flame always stands upright and
 * merely turns to face the camera, which is why it does not lie over when the
 * camera looks down at it.  The height scale is negative because
 * CandlePolyDat's y runs downward.
 *
 * `Count` indexes CandleFlameScaleData[150] and is not range checked here --
 * ManyCandleParticleUpdate() and SubCandleFlame() are what keep it in bounds. */
void EffectCandleFlameDraw(float *Position, int *Color, float Scale, int Count)
{
    GRA3DCAMERA *pCam;
    float *cam_pos;
    float WorkMat[4][4];
    float LocalWorld[4][4];
    float LocalScreen[4][4];
    float RotX;
    float RotY;
    sceVu0IVECTOR ivec[4];
    int ColorR;
    int ColorG;
    int ColorB;
    u_long Tex0;
    int ClipFlg;
    int i;

    pCam    = gra3dGetCamera();                                 /* 1420 */
    cam_pos = gra3dcamGetPosition();

    Tex0 = effdat[EffWrkMonochroModeGet() + 0x30].tex0;

    if (EffWrkMonochroModeGet() == 0)
    {
        ColorR = Color[0];
        ColorG = Color[1];
        ColorB = Color[2];
    }
    else
    {
        ColorR = (Color[0] + Color[1] + Color[2]) / 3;
        ColorG = ColorR;
        ColorB = ColorR;
    }

    Get2PosRot(cam_pos, pCam->vTarget, &RotX, &RotY);

    sceVu0UnitMatrix(WorkMat);
    WorkMat[0][0] = CandleFlameScaleData[Count][0] *  25.0f * Scale;
    WorkMat[1][1] = CandleFlameScaleData[Count][1] * -25.0f * Scale;
    WorkMat[2][2] = WorkMat[0][0];

    sceVu0RotMatrixY(WorkMat, WorkMat, RotY);
    sceVu0TransMatrix(LocalWorld, WorkMat, Position);
    sceVu0MulMatrix(LocalScreen, pCam->matWorldScreen, LocalWorld);

    ClipFlg = 0;

    for (i = 0; i < 4; i++)
    {
        sceVu0RotTransPers(ivec[i], LocalScreen, CandlePolyDat[i], 0);

        if ((u_int)(ivec[i][0] - 0x4000) > 0x8000)
        {
            ClipFlg = 1;
        }
        if ((u_int)(ivec[i][1] - 0x4000) > 0x8000)
        {
            ClipFlg = 1;
        }
        if ((u_int)(ivec[i][2] - 0xff) > 0xffff00)
        {
            ClipFlg = 1;
        }
    }

    if (ClipFlg == 0)
    {
        /* PORT: the DIRECT packet below is inert.  This was a
           RendererPacket3D over the whole texture, which spreads u along
           v0->v1 -- the quad's *y* axis here -- and so drew the flame on its
           side; CandleFlameHostQuad() carries the ROM's own ST instead. */
        CandleFlameHostQuad(LocalWorld, Tex0, ColorR, ColorG, ColorB,
                            Color[3]);

        /* Every vertex takes ivec[0]'s z, not its own. */
        CandleFlameMakePacket(ivec, Tex0, (u_int)ivec[0][2],
                              ColorR, ColorG, ColorB, Color[3]);   /* 1452 */
    }
}

/* 1471 -- the halo around a light source.
 *
 * A camera-facing quad is transformed only to find its screen *extent*; the
 * halo itself is then drawn as two concentric 2D sprites through
 * SetEffSQITex(), the outer one twice the size of the inner.  That is why the
 * quad's own projected corners are thrown away after `w` is taken from them.
 *
 * `rnbk` is the flicker: a fresh random number each frame, unless the effect
 * system is frozen, and it drives both sprites' alpha (the outer at
 * (rnbk/2 + 7)%, the inner at (rnbk + 3)%).  An odd `type` keeps it in 3..9, an
 * even one lets it reach 0 -- so an even type can blink right out.
 *
 * `z` is unused: the sprites take their depth from ipos[2]. */
void SubHalo(float *p, int type, int textp, u_int z, u_char r, u_char g, u_char b, u_char alp, float sc)
{
    float wlm[4][4];
    float slm[4][4];
    int   ipos[4];
    sceVu0IVECTOR ivec[4];
    float vpos[4];

    /* .rodata 0x3a6cc0, a local initialiser: an 8x8 quad in halo-local units. */
    float wpos[4][4] =                                          /* 1478 */
    {
        { -4.0f,  4.0f, 0.0f, 1.0f },
        {  4.0f,  4.0f, 0.0f, 1.0f },
        { -4.0f, -4.0f, 0.0f, 1.0f },
        {  4.0f, -4.0f, 0.0f, 1.0f },
    };

    /* Function-local statics -- globals.txt does not list them, functions.txt
       does.  scw/sch are the halo's base size and nothing in the build writes
       either, so they are effectively constants. */
    static int   rnbk = 0;                                      /* sdata 3efe78 */
    static float scw  = 25.0f;                                  /* sdata 3efe7c */
    static float sch  = 25.0f;                                  /* sdata 3efe80 */

    float f;
    float rot_x;
    float rot_y;
    int   i;
    int   rn;
    int   w;
    int   n;
    GRA3DCAMERA *pCam;

    (void)z;

    pCam = gra3dGetCamera();                                    /* 1487 */

    g3dxVu0CopyVector(vpos, p);                                 /* g3dxVu0.h 134 */

    Vector2Rot(gra3dcamGetDirection(), &rot_x, &rot_y);         /* 1488 / 1491 */

    sceVu0UnitMatrix(wlm);                                      /* 1494 */
    wlm[0][0] = scw * sc;                                       /* 1495 */
    wlm[1][1] = sch * sc;                                       /* 1496 */
    wlm[2][2] = wlm[0][0];                                      /* 1497 */

    sceVu0RotMatrixX(wlm, wlm, rot_x);                          /* 1497 */
    sceVu0RotMatrixY(wlm, wlm, rot_y);                          /* 1498 */
    sceVu0TransMatrix(wlm, wlm, vpos);                          /* 1499 */

    sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);            /* 1500 */

    n = 0;                                                      /* 1502 */

    /* A tighter guard band than the rest of the effect layer: x within
       [0x5800, 0xa800] and y within [0x6400, 0x9c00] of the 2048-centred GS
       space, so a halo is dropped well before it touches the screen edge. */
    for (i = 0; i < 4; i++)                                     /* 1504 */
    {
        sceVu0RotTransPers(ivec[i], slm, wpos[i], 0);           /* 1505 */

        if ((u_int)(ivec[i][0] - 0x5800) > 0x5000)              /* 1506 */
        {
            n = 1;
        }
        if ((u_int)(ivec[i][1] - 0x6400) > 0x3800)              /* 1507 */
        {
            n = 1;
        }
        if ((u_int)(ivec[i][2] - 0xff) > 0xffff00)              /* 1508 */
        {
            n = 1;
        }
    }                                                           /* 1509 */

    if (n == 0)                                                 /* 1510 */
    {
        ipos[0] = (ivec[0][0] + ivec[3][0]) / 2;                /* 1511 */
        ipos[1] = (ivec[0][1] + ivec[3][1]) / 2;                /* 1512 */
        ipos[2] = (ivec[0][2] + ivec[3][2]) / 2;                /* 1513 */
        ipos[3] = 0;                                            /* 1514 */

        /* The larger of the two half-extents, so a halo seen edge-on keeps its
           width rather than collapsing. */
        w = (ivec[3][0] - ivec[0][0]) / 2;                      /* 1516 */
        if (w < (ivec[3][1] - ivec[0][1]) / 2)
        {
            w = (ivec[3][1] - ivec[0][1]) / 2;
        }

        f = (float)w * 0.0625f;                                 /* 1517 */

        if (EffWrkStopFlgGet() == 0)                            /* 1519 */
        {
            if ((type & 1) != 0)                                /* 1520 */
            {
                rnbk = (int)EffectGetRandom(3.0f, 9.0f);        /* effect.h 217 */
            }
            else
            {
                rnbk = (int)EffectGetRandom(0.0f, 9.0f);        /* 1525 */
            }
        }

        rn = rnbk;

        /* The monochrome twin sits at the next index, as everywhere in the
           effect layer. */
        SetEffSQITex(textp * 2 + EffWrkMonochroModeGet() + 0xc, ipos, 1,
                     f + f, f + f, r, g, b,
                     (u_char)((rn / 2 + 7) * (u_int)alp / 100));    /* 1530 */

        SetEffSQITex(textp * 2 + EffWrkMonochroModeGet() + 0xc, ipos, 1,
                     f, f, r, g, b,
                     (u_char)((rn + 3) * (u_int)alp / 100));        /* 1531 */
    }
}

/* 1571 -- the heat-haze billboard: draw_distortion_particles2()'s sibling, and
 * the one that actually refracts.
 *
 * The difference is where the texture coordinates come from.  Its sibling reads
 * them out of a table, so the particle is a sprite.  This one derives them from
 * the particle's own *screen* position -- ST is (screen + st_add + warp_add)
 * scaled by 1/1024 and 1/512, the framebuffer page's dimensions -- so each
 * particle samples whatever was already drawn behind it, displaced by
 * `distortion_amount`.  That is the whole refraction, and it is why
 * LocalCopyLtoL() has to snapshot the frame first.
 *
 * The geometry is a *diamond* rather than a square: the fan goes centre, -y,
 * +x, +y, -x, -y.
 *
 * `type` picks three things at once -- whether the frame is sampled at all
 * (dtex), the GS TEST register and the GS ALPHA register:
 *
 *   1        untextured, TEST 0x50003, ALPHA 0x48
 *   2        textured,   TEST 0x30003, ALPHA 0x48
 *   3 / 4    textured,   TEST 0x50003, ALPHA 0x48
 *   5        untextured, TEST 0x50003, ALPHA 0x48
 *   6        untextured, TEST 0x50003, ALPHA 0x44
 *
 * `fr` is unused -- a2 is clobbered by the first memset and no path reads it.
 *
 * PORT DEVIATION: as in the sibling, the VU0 macro-mode block is written out
 * scalar, and the same two dead register blocks are left as comments.  The
 * textured case has no host bridge -- a per-particle framebuffer refraction
 * needs a renderer feature the port does not have yet -- so types 2/3/4 are
 * invisible here, while the three untextured types go out as gouraud triangles.
 * The return value is right either way, which is what the callers use. */
static int draw_distortion_particles(float (*local_screen)[4], float (*local_clip)[4],
                                     int fr, int t_particles, PARTICLE *pParticles,
                                     float psize, float distortion_amount, int type)
{
    int   i;
    int   n;
    int   num;
    u_long *d;
    u_long areg;
    u_long treg;
    float y_correction = 0.0f;                                  /* 1577 */
    float rr, gg, bb;

    /* .rodata 0x3a6d00 / 0x3a6d10 / 0x3a6d20 / 0x3a6d30, local initialisers. */
    float warp_add[4] = { distortion_amount + y_correction, 0.0f, 0.0f, 0.0f };  /* 1581 */
    float screen_size[4]   = { 639.0f, 447.0f, 0.0f, 0.0f };    /* 1582 */
    float particle_size[4] = { psize, psize * 0.5f, 0.0f, 0.0f };  /* 1583 */
    int   dtex;
    float ones[4]     = { 1.0f, 1.0f, 1.0f, 0.0f };             /* 1589 */
    float st_add[4]   = { -1728.0f, -1824.0f, 0.0f, 0.0f };     /* 1590 */
    float st_scale[4] = { 0.0009765625f, 0.001953125f, 0.0f, 0.0f };  /* 1591 */
    int   ndpkt;
    Q_WORDDATA *pbuf;
    DRAW_ENV_5  env;
    u_int clip_flags;

    (void)fr; (void)screen_size; (void)ones;

    switch (type)                                               /* 1619 */
    {
    case 2:
    case 3:
    case 4:
        dtex = 1;                                               /* 1624 */
        break;

    default:
        dtex = 0;                                               /* 1629 */
        break;
    }

    if (dtex != 0 && type < 7 && type > 0)                      /* 1634 / 1635 */
    {
        /* Snapshot the frame so the particles have something to refract.  The
           source page alternates with the frame parity. */
        LocalCopyLtoL(1, (sys_wrk.count & 1) * 4480, 0x3480);   /* 1642 */
    }

    treg = 0;                                                   /* 1648 */
    if (type == 2)
    {
        treg = 0x30003;                                         /* 1650 */
    }
    else if (type == 1 || (type >= 3 && type <= 6))
    {
        treg = 0x50003;                                         /* 1658 */
    }

    areg = 0;
    switch (type)                                               /* 1663 */
    {
    case 6:
        areg = 0x44;                                            /* 1666 */
        break;

    case 1:
    case 2:
    case 3:
    case 4:
    case 5:
        areg = 0x48;                                            /* 1675 */
        break;
    }

    env.alpha = areg;                                           /* 1681 */
    env.tex1  = 0x161;
    env.clamp = 0;
    env.test  = treg;
    env.zbuf  = 0x10a000118ULL;

    SetDrawEnv(0, &env);                                        /* 1689 */

    Reserve2DPacket(0x10);                                      /* 1692 */
    pbuf = StartDmaDirectTrans();                               /* 1694 */
    d    = (u_long *)pbuf;                                      /* 1695 */

    /* A+D tag for one register write: TEXFLUSH.  TEX0 and ALPHA went out
       through SetDrawEnv() rather than the packet. */
    pbuf[0].ul64[0] = 0x1000000000008001ULL;                    /* 1699 */
    pbuf[0].ul64[1] = 0x0e;                                     /* 1700 */
    pbuf[1].ul64[0] = 0;                                        /* 1701 */
    pbuf[1].ul64[1] = 0x3f;                                     /* 1702 */

    /* `n` counts u_longs, not quadwords -- the head is two of each. */
    n   = 4;
    num = 0;

    /* 1732 / 1748 -- both matrices and the five constant vectors go into VU0
       registers here. */

    for (i = 0; i < t_particles; i++)                           /* 1751 */
    {
        PARTICLE *p = &pParticles[i];
        float clip[4];
        float screen[4];
        float q;
        float dx, dy;       /* screen half-extents  */
        float sx, sy;       /* their ST equivalents */
        float st[4];

        if (!(p->color[3] > 0.0f))                              /* 1752 */
        {
            continue;
        }

        num++;                                                  /* 1757 */

        rr = p->color[0];                                       /* 1759 */
        gg = p->color[1];                                       /* 1760 */
        bb = p->color[2];                                       /* 1761 */

        if (EffWrkMonochroModeGet() != 0)                       /* 1762 */
        {
            p->color[0] = p->color[1] = p->color[2] =
                (rr + gg + bb) / 3.0f;                          /* 1763 */
        }

        sceVu0ApplyMatrix(clip, local_clip, p->position);       /* 1784 */

        clip_flags = 0;
        if (clip[0] >  clip[3]) { clip_flags |= 0x01; }
        if (clip[0] < -clip[3]) { clip_flags |= 0x02; }
        if (clip[1] >  clip[3]) { clip_flags |= 0x04; }
        if (clip[1] < -clip[3]) { clip_flags |= 0x08; }
        if (clip[2] >  clip[3]) { clip_flags |= 0x10; }
        if (clip[2] < -clip[3]) { clip_flags |= 0x20; }

        if ((clip_flags & 0x3f) != 0)                           /* 1786 */
        {
            p->color[0] = rr;
            p->color[1] = gg;
            p->color[2] = bb;
            continue;
        }

        sceVu0ApplyMatrix(screen, local_screen, p->position);   /* 1788 */

        q = 1.0f / screen[3];
        screen[0] *= q;
        screen[1] *= q;
        screen[2] *= q;
        screen[3] *= q;

        dx = particle_size[0] * q;
        dy = particle_size[1] * q;

        /* The framebuffer sample point: screen position shifted into GS window
           space, displaced by the warp, then normalised over the page.  Q is
           forced to 1, so this lookup is deliberately not perspective
           corrected. */
        st[0] = (screen[0] + st_add[0] + warp_add[0]) * st_scale[0];
        st[1] = (screen[1] + st_add[1] + warp_add[1]) * st_scale[1];
        st[2] = 1.0f;
        st[3] = 0.0f;

        sx = dx * st_scale[0];
        sy = dy * st_scale[1];

        /* The dead bounding-box clamp sat here, exactly as in the sibling:
             vf13 = max(vf27 - size - ones, 0);
             vf14 = min(vf27 + size + ones, screen_size);
           and nothing reads either. */

        /* PRIM 0x4d | dtex << 4: TRIANGLE_FAN, gouraud, alpha blended, and
           textured only when this type refracts. */
        d[n]     = ((u_long)(((u_long)dtex << 4) | 0x4d) << 47)
                 | 0xe000400000008001ULL;                       /* 1810 */
        d[n + 1] = 0x0042424242421421ULL;                       /* 1822 */
        n += 2;

        {                                                       /* 1892 */
            Q_WORDDATA *pv = (Q_WORDDATA *)&d[n];
            /* centre, then -y, +x, +y, -x, -y: a diamond, not a square. */
            static const float ox[5] = {  0.0f,  1.0f,  0.0f, -1.0f,  0.0f };
            static const float oy[5] = { -1.0f,  0.0f,  1.0f,  0.0f, -1.0f };
            int k;

            pv[0].iv[0] = (int)p->color[0];
            pv[0].iv[1] = (int)p->color[1];
            pv[0].iv[2] = (int)p->color[2];
            pv[0].iv[3] = (int)p->color[3];

            pv[1].fl32[0] = st[0];
            pv[1].fl32[1] = st[1];
            pv[1].fl32[2] = st[2];
            pv[1].fl32[3] = st[3];

            pv[2].iv[0] = (int)(screen[0] * 16.0f);
            pv[2].iv[1] = (int)(screen[1] * 16.0f);
            pv[2].iv[2] = (int)(screen[2] * 16.0f);
            pv[2].iv[3] = (int)(screen[3] * 16.0f);

            /* Corner colour: same RGB, alpha zero. */
            pv[3].iv[0] = (int)p->color[0];
            pv[3].iv[1] = (int)p->color[1];
            pv[3].iv[2] = (int)p->color[2];
            pv[3].iv[3] = 0;

            for (k = 0; k < 5; k++)
            {
                Q_WORDDATA *pst  = &pv[4 + k * 2];
                Q_WORDDATA *pxyz = &pv[5 + k * 2];

                pst->fl32[0] = st[0] + ox[k] * sx;
                pst->fl32[1] = st[1] + oy[k] * sy;
                pst->fl32[2] = st[2];
                pst->fl32[3] = st[3];

                pxyz->iv[0] = (int)((screen[0] + ox[k] * dx) * 16.0f);
                pxyz->iv[1] = (int)((screen[1] + oy[k] * dy) * 16.0f);
                pxyz->iv[2] = (int)(screen[2] * 16.0f);
                pxyz->iv[3] = (int)(screen[3] * 16.0f);
            }

            /* Host path for the three untextured types.  The textured ones need
               a per-particle framebuffer sample the renderer cannot do yet. */
            if (dtex == 0)
            {
                float xy[12 * 2];
                u_char rgba[12 * 4];
                int    v = 0;

                for (k = 0; k < 4; k++)
                {
                    const float px[3] = { screen[0],
                                          screen[0] + ox[k]     * dx,
                                          screen[0] + ox[k + 1] * dx };
                    const float py[3] = { screen[1],
                                          screen[1] + oy[k]     * dy,
                                          screen[1] + oy[k + 1] * dy };
                    int j;

                    for (j = 0; j < 3; j++, v++)
                    {
                        xy[v * 2 + 0] = px[j] - 2048.0f + 320.0f;
                        xy[v * 2 + 1] = py[j] - 2048.0f + 224.0f;

                        rgba[v * 4 + 0] = (u_char)(int)p->color[0];
                        rgba[v * 4 + 1] = (u_char)(int)p->color[1];
                        rgba[v * 4 + 2] = (u_char)(int)p->color[2];
                        rgba[v * 4 + 3] = (j == 0)
                                        ? (u_char)(int)p->color[3] : 0;
                    }
                }

                MioPan_RendererDrawSolidTriangles2D(xy, rgba, 12);
            }
            else
            {
                /* The refracting types (2, 3, 4).  Same diamond, but each
                 * vertex samples the frame copy LocalCopyLtoL() parked at
                 * 0x3480 -- displaced by warp_add, which is the refraction.
                 *
                 * The ROM's ST is (window position + st_add + warp_add) scaled
                 * by 1/1024 and 1/512, and the TEX0 it inherits is 512x256, so
                 * the GS reads texel = pixel / 2 -- exactly the half-scale copy
                 * type 1 produced.  Normalising over the copy's own 320x224
                 * instead gives pixel / 640 and pixel / 448, which is the same
                 * sample point expressed without the page size.
                 *
                 * TEX0 is inherited GS state here rather than carried in the
                 * packet, so it is rebuilt for the bridge: TBP0 0x3480, TBW 5
                 * (320 px) and PSMCT24, which is what identifies the slot. */
                static const u_long haze_tex0 = 0x2000000224117480ULL;
                float  xy[12 * 2];
                float  uv[12 * 2];
                float  zz[12];
                u_char rgba[12 * 4];
                int    v = 0;
                /* Depth comes through with the geometry, the same way the
                   sibling draw_distortion_particles2() passes it: `env` here
                   is TEST 0x5000d -- ZTST GEQUAL with ZMSK -- so the haze is
                   tested against the room and writes nothing.  The whole
                   diamond sits at the particle's own depth, as the GS sprite
                   above does: every XYZF2 it writes carries screen[2].
                   matLocalClip is the engine's world->clip, so clip[2]/clip[3]
                   is already the symmetric convention the sprite shader's
                   MikuPanFixClipZ() expects -- do not convert it here. */
                const float haze_z = clip[3] != 0.0f ? clip[2] / clip[3] : 0.0f;

                for (k = 0; k < 4; k++)
                {
                    const float px[3] = { screen[0],
                                          screen[0] + ox[k]     * dx,
                                          screen[0] + ox[k + 1] * dx };
                    const float py[3] = { screen[1],
                                          screen[1] + oy[k]     * dy,
                                          screen[1] + oy[k + 1] * dy };
                    int j;

                    for (j = 0; j < 3; j++, v++)
                    {
                        xy[v * 2 + 0] = px[j] - 2048.0f + 320.0f;
                        xy[v * 2 + 1] = py[j] - 2048.0f + 224.0f;
                        zz[v] = haze_z;

                        /* The ROM's own ST, unaltered: window position shifted
                         * by st_add and displaced by warp_add, scaled by
                         * st_scale (1/1024, 1/512).  The bridge does the rest,
                         * forming texel = ST * 2^TW exactly as the GS would and
                         * then normalising over the copy -- which for this
                         * 512x256 page over a 320x224 copy comes out at
                         * pixel/640 and pixel/448.
                         *
                         * The sample point carries warp_add and the position
                         * does not.  That difference is the whole effect. */
                        uv[v * 2 + 0] = (px[j] + st_add[0] + warp_add[0])
                                      * st_scale[0];
                        uv[v * 2 + 1] = (py[j] + st_add[1] + warp_add[1])
                                      * st_scale[1];

                        rgba[v * 4 + 0] = (u_char)(int)p->color[0];
                        rgba[v * 4 + 1] = (u_char)(int)p->color[1];
                        rgba[v * 4 + 2] = (u_char)(int)p->color[2];
                        rgba[v * 4 + 3] = (j == 0)
                                        ? (u_char)(int)p->color[3] : 0;
                    }
                }

                MioPan_RendererDrawTexturedTriangles2D(
                    (sceGsTex0 *)&haze_tex0, xy, uv, rgba, zz, 12, 1, 1, 1);
            }
        }

        n += 28;                                                /* 1894 */

        p->color[0] = rr;                                       /* 1896 */
        p->color[1] = gg;                                       /* 1897 */
        p->color[2] = bb;                                       /* 1898 */
    }                                                           /* 1900 */

    EndDmaDirectTrans(pbuf + (n + 1) / 2);                      /* 1901 / 1903 */

    return num;                                                 /* 1905 */
}

/* ==========================================================================
 *  The shared particle pool
 *
 *  A HEAT_HAZE is a ring of 200 PARTICLEs.  `head` is where the next one is
 *  written and wraps at 200, so a pool that is over-fed silently overwrites its
 *  own oldest particles rather than dropping the new one.
 *
 *  Two producer/consumer pairs share the ring and differ only in what alpha
 *  means.  add_particle / update_particles fade *out* from the requested alpha
 *  on a straight ramp -- `alp_step` is the per-frame delta.  add_particle2 /
 *  update_particles2 fade *in* and back out on a triangle whose peak is 90% of
 *  the way through the lifetime -- there `alp_step` holds the peak value and
 *  colour[3] starts at zero.
 * ======================================================================== */

/* 2178 -- claim the ring's next slot and fill it.  `type` is unused; every
 * caller passes one and no path reads it. */
void add_particle(int type, HEAT_HAZE *hh, float *pos, float *vel, float r, float g, float b, float a)
{
    float oolife;
    PARTICLE *p;

    (void)type;

    p = &hh->particles[hh->head];                               /* 2184 */

    /* Position and velocity are one source line each -- all four stores of
     * each group sit under a single line marker. */
    p->position[0] = pos[0]; p->position[1] = pos[1]; p->position[2] = pos[2]; p->position[3] = 1.0f;   /* 2186 */
    p->velocity[0] = vel[0]; p->velocity[1] = vel[1]; p->velocity[2] = vel[2]; p->velocity[3] = 0.0f;   /* 2187 */

    oolife = 1.0f / (float)hh->blife;                           /* 2189 */

    p->color[0] = r; p->color[1] = g; p->color[2] = b; p->color[3] = a;                                 /* 2190 */

    p->alp_step = -(a * oolife);                                /* 2191 */
    p->lifetime = hh->blife;                                    /* 2192 */

    hh->head = (hh->head + 1) % 200;                            /* 2193 */
}

/* 2197 -- the straight fade-out step.  `grv` is the vertical acceleration and
 * is zero in this build, so the velocity[2] update is a no-op; it is kept
 * because the ROM really does emit the add.  (functions.txt lists only `i` for
 * this function -- float locals do not always leave a stab -- but the 0.0f is
 * loaded once before the loop under its own line number, which is what a
 * declaration looks like and not what a hoisted literal looks like.) */
void update_particles(PARTICLE *prt)
{
    int i;
    float grv = 0.0f;                                           /* 2200 */

    for (i = 0; i < 200; i++)                                   /* 2202 */
    {
        if (prt[i].lifetime != 0)                               /* 2203 */
        {
            prt[i].position[0] += prt[i].velocity[0];           /* 2204 */
            prt[i].position[1] += prt[i].velocity[1];           /* 2205 */
            prt[i].position[2] += prt[i].velocity[2];           /* 2206 */

            prt[i].color[3] += prt[i].alp_step;                 /* 2208 */

            prt[i].velocity[2] += grv;                          /* 2210 */

            prt[i].lifetime--;                                  /* 2212 */
        }
        else
        {
            prt[i].color[3] = 0.0f;                             /* 2214 */
        }
    }
}

/* 2220 -- the fade-in variant.  colour[3] starts at zero and `alp_step` keeps
 * the peak alpha for update_particles2() to shape. */
void add_particle2(int type, HEAT_HAZE *hh, float *pos, float *vel, float r, float g, float b, float a)
{
    PARTICLE *p;

    (void)type;

    p = &hh->particles[hh->head];                               /* 2225 */

    p->position[0] = pos[0]; p->position[1] = pos[1]; p->position[2] = pos[2]; p->position[3] = 1.0f;   /* 2227 */
    p->velocity[0] = vel[0]; p->velocity[1] = vel[1]; p->velocity[2] = vel[2]; p->velocity[3] = 0.0f;   /* 2228 */

    p->color[0] = r; p->color[1] = g; p->color[2] = b; p->color[3] = 0.0f;                              /* 2230 */

    p->alp_step = a;                                            /* 2231 */
    p->lifetime = hh->blife;                                    /* 2232 */

    hh->head = (hh->head + 1) % 200;                            /* 2233 */
}

/* 2237 -- the triangle-envelope step, and the pool's distance fade.
 *
 * `lng` is the camera distance: past 800 the pool is at full strength, under
 * 500 it is invisible, and between the two it ramps.  Each particle's own alpha
 * then rises over the first 90% of its life and falls over the last 10%.
 *
 * `f2` is spelled `1.0f - center` in the source, and GCC folded it in single
 * precision -- which is why the ROM's constant is 0x3dccccd0 (0.10000002) and
 * not 0.1f's 0x3dcccccd.  Writing 0.1f here would not round-trip. */
void update_particles2(HEAT_HAZE *hh, float lng, float arate)
{
    int   i;
    float a;
    float f;
    float f2;
    float center = 0.9f;                                        /* 2240 */

    a = (lng > 800.0f) ? arate
      : (lng < 500.0f) ? 0.0f
                       : (lng - 500.0f) / 300.0f * arate;       /* 2245 */

    for (i = 0; i < 200; i++)                                   /* 2247 */
    {
        if (hh->particles[i].lifetime != 0)                     /* 2250 */
        {
            hh->particles[i].position[0] += hh->particles[i].velocity[0];   /* 2251 */
            hh->particles[i].position[1] += hh->particles[i].velocity[1];   /* 2252 */
            hh->particles[i].position[2] += hh->particles[i].velocity[2];   /* 2253 */

            f = (float)(hh->blife - hh->particles[i].lifetime) /
                (float)hh->blife;                               /* 2255 */

            f2 = 1.0f - center;                                 /* 2257 */

            if (f <= center)                                    /* 2259 */
            {
                hh->particles[i].color[3] =
                    hh->particles[i].alp_step * f / center * a; /* 2260 */
            }
            else
            {
                hh->particles[i].color[3] =
                    hh->particles[i].alp_step * (1.0f - f) / f2 * a;    /* 2262 */
            }

            hh->particles[i].lifetime--;                        /* 2267 */
        }
        else
        {
            hh->particles[i].color[3] = 0.0f;                   /* 2269 */
        }
    }
}

/* 2278 -- the heat-haze controller: one frame of a pool, drawn and stepped.
 *
 * Four live types, and they are quite different effects sharing one pool:
 *
 *   1  the amulet fire -- a halo plus a slow rising column, one particle every
 *      other frame and only while (cnt / 25) is even, which gives it a puffing
 *      cadence rather than a steady stream
 *   2  a ghost's aura -- a halo whose size and alpha track how many particles
 *      are actually on screen, plus one particle a frame
 *   3  the standing haze around a passive ghost
 *   4  the same, larger and denser, for a hostile one
 *
 * Type 0 returns immediately, and a null pool prints and returns null -- that
 * is the only diagnostic in the file and it is a plain printf, not PRINT_ERROR.
 *
 * `st` is the catch-up flag.  When it is set no new particles are added and the
 * pool is stepped five times instead of once, which is how a torch that has
 * just been lit fills out in a single frame.
 *
 * The distortion amount is -1 for the two halo types (a fixed inward pull) and
 * a sine wave for the two ghost hazes, whose amplitude grows as the camera
 * closes from 600 units to 300.  pcnt1 is that wave's phase and is shared by
 * every type-3 and type-4 pool in the room, so they all breathe together. */
void *ContHeatHaze(void *addr, int type, float *pos, float *pos2, int st, float r, float g, float b, float a, float size, float arate)
{
    static float pcnt1;                                         /* sbss 3f4bb8 */
    static float pcnt2;                                         /* sbss 3f4bbc */

    HEAT_HAZE *hh = (HEAT_HAZE *)addr;
    int   n1;
    int   i;
    float f;
    float fw1;
    float lng;
    float escl;
    float wpos[4];
    float local_clip[4][4];
    float local_world[4][4];
    float local_screen[4][4];
    float work[4][4];
    float ppos[4];
    float pvel[4];
    GRA3DCAMERA *pCam;
    float *cam_dir;
    float *cam_pos;
    float zero_vec[4];
    float fx, fy, fz;
    float rx, rz;
    float rot[4];
    u_char alp;

    ppos[0] = 0.0f; ppos[1] = 0.0f; ppos[2] = 0.0f; ppos[3] = 1.0f;   /* 2288 */
    pvel[0] = 0.0f; pvel[1] = 0.0f; pvel[2] = 0.0f; pvel[3] = 0.0f;

    pCam    = gra3dGetCamera();
    cam_dir = gra3dcamGetDirection();
    cam_pos = gra3dcamGetPosition();

    lng  = 0.0f;
    escl = 3.79999995f;

    zero_vec[0] = 0.0f; zero_vec[1] = 0.0f;
    zero_vec[2] = 0.0f; zero_vec[3] = 1.0f;

    if (addr == nullptr)                                        /* 2300 */
    {
        printf("Particle Buffer is Full : in ContHeatHaze()\n");
        return nullptr;
    }

    /* Claim the pool and mark it to survive the next room reset -- bit 0x80 is
       what InitHeatHaze() masks off rather than clearing. */
    hh->flag |= 0xff;

    if (type == 0)
    {
        return addr;
    }

    if (init_hhaze != 0)                                        /* 2312 */
    {
        pcnt1 = 0.0f;
        pcnt2 = 0.0f;

        for (i = 0; i < 4; i++)
        {
            ene_particle[i].flag = 0;
            ene_particle[i].cnt  = 0;
        }

        amu_particle[0].flag = 0;
        amu_particle[0].cnt  = 0;

        init_hhaze = 0;
    }

    /* ---- the local-to-screen basis ------------------------------------- */
    if (type == 2)
    {
        /* The aura faces the camera's *direction*, not its position, so a
           ghost at the edge of the frame is not sheared. */
        GetTrgtRot(zero_vec, cam_dir, rot, 3);

        sceVu0UnitMatrix(work);
        work[0][0] = pCam->fFov * 25.0f;
        work[1][1] = work[0][0];
        work[2][2] = work[0][0];

        sceVu0RotMatrixX(work, work, rot[0]);
        sceVu0RotMatrixY(work, work, rot[1]);

        sceVu0TransMatrix(local_world, work, pos);
        sceVu0MulMatrix(local_screen, pCam->matWorldScreen, local_world);
        sceVu0MulMatrix(local_clip, pCam->matWorldClipPolygon, local_world);
    }
    else if (type == 1 || (type >= 3 && type < 5))
    {
        /* pos2, when present, is the far end of a segment: the pool is then
           laid along it rather than standing upright. */
        if (pos2 == nullptr)
        {
            rx = 0.0f;
            rz = 0.0f;
        }
        else
        {
            Get2PosRot2(pos2, pos, &rx, &rz);
        }

        fx = pos[0] - cam_pos[0];
        fy = pos[1] - cam_pos[1];
        fz = pos[2] - cam_pos[2];
        lng = sqrtf(fx * fx + fy * fy + fz * fz);

        sceVu0UnitMatrix(work);
        work[0][0] = 25.0f;
        work[1][1] = 25.0f;
        work[2][2] = 25.0f;

        sceVu0RotMatrixX(work, work, rx);
        sceVu0RotMatrixZ(work, work, -rz);

        sceVu0TransMatrix(local_world, work, pos);
        sceVu0MulMatrix(local_screen, pCam->matWorldScreen, local_world);
        sceVu0MulMatrix(local_clip, pCam->matWorldClipPolygon, local_world);
    }

    /* ---- the halo (types 1 and 2) and the distortion draw --------------- */
    if (type == 2)
    {
        g3dxVu0CopyVector(wpos, pos);
        wpos[1] -= 20.0f;

        /* Both the halo's alpha and its size track how much of the pool was
           actually on screen last frame, so a ghost fading out dims its own
           glow without anything else being told. */
        alp = (u_char)((float)hh->disp * 80.0f / (float)hh->max);

        SubHalo(wpos, 0, 0, 0, 0x30, 0x30, 0xff, alp,
                (float)hh->disp * 0.5f / (float)hh->max);

        fw1 = -1.0f;
        f   = size / pCam->fFov;

        hh->disp = (short)draw_distortion_particles(local_screen, local_clip,
                                                    (int)(sys_wrk.count & 1),
                                                    200, &hh->particles[0],
                                                    f, fw1, type);
    }
    else if (type == 1)
    {
        g3dxVu0CopyVector(wpos, pos);
        wpos[1] -= 10.0f;

        SubHalo(wpos, 1, 0, 0, 0x28, 0x28, 0x6e, 0x6e, 0.599999964f);

        fw1 = -1.0f;
        f   = (size * escl) / pCam->fFov;

        hh->disp = (short)draw_distortion_particles(local_screen, local_clip,
                                                    (int)(sys_wrk.count & 1),
                                                    200, &hh->particles[0],
                                                    f, fw1, type);
    }
    else if (type == 3 || type == 4)
    {
        float amp;
        float amin = (type == 3) ?  2.0f :  4.0f;
        float amax = (type == 3) ?  8.0f : 12.0f;
        float step = (type == 3) ? 0.00872664526f : 0.0174532905f;

        g3dxVu0CopyVector(wpos, pos);
        wpos[1] -= 10.0f;

        fx = pos[0] - cam_pos[0];
        fy = pos[1] - cam_pos[1];
        fz = pos[2] - cam_pos[2];
        fw1 = sqrtf(fx * fx + fy * fy + fz * fz);

        if (fw1 > 600.0f)
        {
            amp = amin;
        }
        else if (fw1 < 300.0f)
        {
            amp = amax;
        }
        else
        {
            amp = ((600.0f - fw1) * (amax - amin)) / 300.0f + amin;
        }

        fw1 = sinf(pcnt1) * amp;

        pcnt1 += EffectGetRandom(0.0f, 1.0f) * step;

        if (pcnt1 > 3.1415925f)
        {
            pcnt1 -= 6.28318501f;
        }

        f = (size * escl) / pCam->fFov;

        hh->disp = (short)draw_distortion_particles(local_screen, local_clip,
                                                    (int)(sys_wrk.count & 1),
                                                    200, &hh->particles[0],
                                                    f, fw1, type);
    }

    if (EffWrkStopFlgGet() != 0)
    {
        return addr;
    }

    /* ---- spawning ------------------------------------------------------ */
    if (st == 0)
    {
        n1 = hh->cnt + 1;

        if (type == 2)
        {
            for (i = 0; i < 1; i++)
            {
                ppos[0] = EffectGetRandom(-0.5f, 0.5f) * 1.29999995f;
                ppos[1] = 0.0f;
                ppos[2] = EffectGetRandom(-0.5f, 0.5f) * 1.29999995f;

                pvel[0] = EffectGetRandom(-0.5f, 0.5f) * 0.00999999978f;
                pvel[1] = EffectGetRandom(-0.0199999996f, -0.0399999991f);
                pvel[2] = EffectGetRandom(-0.5f, 0.5f) * 0.00999999978f;

                add_particle(2, hh, ppos, pvel, r, g, b, a);
            }

            n1 = hh->cnt + 1;
        }
        else if (type == 1)
        {
            /* Even 25-frame block, even frame -- the puffing cadence. */
            if (((hh->cnt / 25) & 1) == 0 && (hh->cnt & 1) == 0)
            {
                ppos[0] = EffectGetRandom(-0.5f, 0.5f) * 1.29999995f;
                ppos[1] = 0.0f;
                ppos[2] = EffectGetRandom(-0.5f, 0.5f) * 1.29999995f;

                pvel[0] = EffectGetRandom(-0.5f, 0.5f) * 0.00999999978f;
                pvel[1] = EffectGetRandom(-0.00999999978f, -0.0499999991f);
                pvel[2] = EffectGetRandom(-0.5f, 0.5f) * 0.00999999978f;

                add_particle(1, hh, ppos, pvel, r, g, b, a);

                n1 = hh->cnt + 1;
            }
        }
        else if (type == 3)
        {
            if ((hh->cnt & 1) == 0)
            {
                ppos[0] = EffectGetRandom(-0.5f, 0.5f) * 1.5f * escl;
                ppos[1] = 0.0f;
                ppos[2] = EffectGetRandom(-0.5f, 0.5f) * 1.5f * escl;

                pvel[0] = EffectGetRandom(-0.5f, 0.5f) * 0.0149999997f * escl;
                pvel[1] = EffectGetRandom(-0.0399999991f, -0.0599999987f) * escl;
                pvel[2] = EffectGetRandom(-0.5f, 0.5f) * 0.0149999997f * escl;

                add_particle2(3, hh, ppos, pvel, r, g, b, a);

                n1 = hh->cnt + 1;
            }
        }
        else if (type == 4)
        {
            if ((hh->cnt & 3) == 0)
            {
                ppos[0] = EffectGetRandom(-0.5f, 0.5f) * 1.5f * escl;
                ppos[1] = 0.0f;
                ppos[2] = EffectGetRandom(-0.5f, 0.5f) * 1.5f * escl;

                pvel[0] = EffectGetRandom(-0.5f, 0.5f) * 0.00999999978f * escl;
                pvel[1] = EffectGetRandom(-0.00999999978f, -0.0299999993f) * escl;
                pvel[2] = EffectGetRandom(-0.5f, 0.5f) * 0.00999999978f * escl;

                add_particle2(4, hh, ppos, pvel, r, g, b, a);

                n1 = hh->cnt + 1;
            }
        }

        hh->cnt = n1;
    }

    /* ---- stepping ------------------------------------------------------ */
    if (type > 0)
    {
        if (type < 3)
        {
            if (st != 0)
            {
                update_particles(&hh->particles[0]);
                update_particles(&hh->particles[0]);
                update_particles(&hh->particles[0]);
                update_particles(&hh->particles[0]);
            }

            update_particles(&hh->particles[0]);
        }
        else if (type < 5)
        {
            if (st == 0)
            {
                update_particles2(hh, lng, arate);
            }
            else
            {
                update_particles2(hh, lng, arate);
                update_particles2(hh, lng, arate);
                update_particles2(hh, lng, arate);
                update_particles2(hh, lng, arate);
                update_particles2(hh, lng, arate);
            }
        }
    }

    return addr;                                                /* 2579 */
}

/* 2583 -- claim a pool and size its particles' lifetimes.
 *
 * `type` selects the lifetime pair; the six cases appear in the ROM's source in
 * the order 1, 5, 6, 2, 3, 4 -- both the address order of the bodies and the
 * line numbers agree on that, so it is the source's own ordering, not a
 * codegen artefact.  Cases 1 and 4 (the two with constant lifetimes) share one
 * store tail, which GCC cross-jumped.
 *
 * There is no default arm and no assert: a `type` outside 1..6 leaves `blife`
 * and `max` holding whatever the pool's previous owner left there. */
void SetPartInit(HEAT_HAZE *addr, int type, int lifetime)
{
    int j;

    for (j = 0; j < 200; j++)                                   /* 2591 */
    {
        addr->particles[j].position[0] = 0.0f; addr->particles[j].position[1] = 0.0f;
        addr->particles[j].position[2] = 0.0f; addr->particles[j].position[3] = 1.0f;   /* 2592 */
        addr->particles[j].color[3] = 0.0f;                     /* 2593 */
        addr->particles[j].lifetime  = 0;                       /* 2594 */
    }

    addr->head = 0;                                             /* 2596 */
    addr->flag = 1;                                             /* 2597 */
    addr->cnt  = 0;                                             /* 2598 */
    addr->disp = 0;                                             /* 2599 */

    switch (type)                                               /* 2601 */
    {
    case 1:
        addr->max   = 100;                                      /* 2603 */
        addr->blife = 200;
        break;

    case 5:
        addr->max   = (short)(lifetime * 2);                    /* 2609 */
        addr->blife = (short)lifetime;
        break;

    case 6:
        addr->max   = (short)(lifetime / 8);                    /* 2614 */
        addr->blife = (short)lifetime;                          /* 2615 */
        break;                                                  /* 2616 */

    case 2:
        addr->max   = 200;                                      /* 2619 */
        addr->blife = (short)lifetime;                          /* 2620 */
        break;                                                  /* 2621 */

    case 3:
        addr->max   = (short)(lifetime / 2);                    /* 2623 */
        addr->blife = (short)lifetime;                          /* 2624 */
        break;                                                  /* 2625 */

    case 4:
        addr->max   = 75;                                       /* 2627 */
        addr->blife = 300;                                      /* 2628 */
        break;
    }
}                                                               /* 2629 */

/* 2637 / 2659 / 2681 -- hand out a free pool.
 *
 * All three have the same shape: a non-null `addr` is already this caller's
 * pool and comes straight back, and only a null one goes looking.  The search
 * takes the first pool whose flag is 0, initialises it, and stops -- `n` is the
 * found latch and sits in the for-condition rather than driving a break.
 *
 * A search that finds nothing returns null, and every caller has to cope: the
 * pools are a fixed resource and the fifth simultaneous torch simply gets no
 * particles. */
void *GetEnePartAddr(void *addr, int type, int lifetime)
{
    int i;
    int n;
    void *ret;

    if (addr == nullptr)                                        /* 2642 */
    {
        ret = nullptr;                                          /* 2643 */

        for (i = 0, n = 0; i < 4 && n == 0; i++)                /* 2644 */
        {
            if (ene_particle[i].flag == 0)                      /* 2646 */
            {
                SetPartInit(&ene_particle[i], type, lifetime);  /* 2647 */
                ret = &ene_particle[i];
                n = 1;                                          /* 2649 */
            }
        }
    }
    else
    {
        ret = addr;                                             /* 2653 */
    }

    return ret;                                                 /* 2655 */
}

void *GetAmuPartAddr(void *addr, int type, int lifetime)
{
    int i;
    int n;
    void *ret;

    if (addr == nullptr)                                        /* 2664 */
    {
        ret = nullptr;                                          /* 2665 */

        for (i = 0, n = 0; i < 1 && n == 0; i++)                /* 2666 */
        {
            if (amu_particle[i].flag == 0)                      /* 2668 */
            {
                SetPartInit(&amu_particle[i], type, lifetime);  /* 2669 */
                ret = &amu_particle[i];
                n = 1;                                          /* 2671 */
            }
        }
    }
    else
    {
        ret = addr;                                             /* 2675 */
    }

    return ret;                                                 /* 2677 */
}

/* The torch pool is the only one that announces itself.  Plain printf, one
 * jal, no PRINT_* banner. */
void *GetTorchPartAddr(void *addr, int type, int lifetime)
{
    int i;
    int n;
    void *ret;

    if (addr == nullptr)                                        /* 2686 */
    {
        ret = nullptr;                                          /* 2687 */

        for (i = 0, n = 0; i < 5 && n == 0; i++)                /* 2688 */
        {
            if (torch_particle[i].flag == 0)                    /* 2690 */
            {
                printf("Set Torch work no = [%d]\n", i);        /* 2691 */
                ret = &torch_particle[i];

                SetPartInit(&torch_particle[i], type, lifetime); /* 2692 */
                n = 1;                                          /* 2694 */
            }
        }
    }
    else
    {
        ret = addr;                                             /* 2698 */
    }

    return ret;                                                 /* 2700 */
}

/* 2955 -- a triangle wave over a 2 * TotalFrame cycle: Min up to Max over the
 * first half, back down over the second.  A TotalFrame of zero or less is
 * treated as "already at the end", which pins the result at Max rather than
 * dividing by it. */
static int ItemEffectGetChangeVal(int NowFrame, int TotalFrame, int MinVal, int MaxVal)
{
    float rate;

    if (NowFrame < TotalFrame)                                  /* 2957 */
    {
        rate = (TotalFrame < 1) ? 1.0f
                                : (float)NowFrame / (float)TotalFrame;

        return (int)((float)(MaxVal - MinVal) * rate + (float)MinVal);
    }

    rate = (TotalFrame < 1) ? 1.0f
                            : (float)(NowFrame - TotalFrame) / (float)TotalFrame;

    return (int)((float)(MinVal - MaxVal) * rate + (float)MaxVal);  /* 2980 */
}

/* 3238 -- the torch controller.  ContHeatHaze()'s sibling, and the same shape:
 * build a basis, draw a halo and the particle pool, then spawn and step.
 *
 * `tp2` is the flame's look and picks the halo's colour, its two size rates and
 * the vertical speed pair; SetTorch() is what maps dat.uc8[2] onto it.  Types 2
 * and 4 draw *two* halos, an outer wash and an inner core -- which is what
 * makes the blue flame and the flare read as brighter than the plain torch
 * rather than merely differently coloured.
 *
 * `st` is a repeat count, not a flag as it is in ContHeatHaze(): the whole
 * spawn-and-step body runs `st` times, so SetTorch()'s st = 3 for the flare
 * fills the pool three times faster.  Each pass adds four particles.
 *
 * `arate` is unused, and the camera distance is computed and thrown away --
 * the sqrt is issued and its result never read.  Both kept as found. */
void *ContTorch(void *addr, int type, float *pos, float *pos2, int st, float r, float g, float b, float a, float size, float arate, int tp2, float ar, float sr)
{
    HEAT_HAZE *hh = (HEAT_HAZE *)addr;
    float rrate;
    float escl;
    float ysp1;
    float ysp2;
    int   i;
    int   j;
    float wind;
    float work[4][4];
    float local_clip[4][4];
    float local_world[4][4];
    float local_screen[4][4];
    float ppos[4];
    float pvel[4];
    float wpos[4];
    GRA3DCAMERA *pCam;
    float *cam_pos;
    float rx, rz;
    float fx, fy, fz;
    float psize;
    u_char alp;
    u_char hr, hg, hb;
    int    htype;

    (void)arate;

    ppos[0] = 0.0f; ppos[1] = 0.0f; ppos[2] = 0.0f; ppos[3] = 1.0f;
    pvel[0] = 0.0f; pvel[1] = 0.0f; pvel[2] = 0.0f; pvel[3] = 0.0f;

    pCam    = gra3dGetCamera();
    cam_pos = gra3dcamGetPosition();

    init_torch = 0;

    ysp1  = 0.129999995f;
    ysp2  = 0.109999999f;
    rrate = 3.79999995f;
    escl  = 3.39999986f;

    if (addr == nullptr)                                        /* 3253 */
    {
        printf("Particle Buffer is Full : in ContTorch()\n");
        return nullptr;
    }

    hh->flag |= 0xff;

    if (pos2 == nullptr)
    {
        rx = 0.0f;
        rz = 0.0f;
    }
    else
    {
        Get2PosRot2(pos2, pos, &rx, &rz);
    }

    /* Computed and discarded -- the ROM issues the sqrt and never reads it. */
    fx = pos[0] - cam_pos[0];
    fy = pos[1] - cam_pos[1];
    fz = pos[2] - cam_pos[2];
    (void)sqrtf(fx * fx + fy * fy + fz * fz);

    sceVu0UnitMatrix(work);
    work[0][0] = 25.0f;
    work[1][1] = 25.0f;
    work[2][2] = 25.0f;

    sceVu0RotMatrixX(work, work, rx);
    sceVu0RotMatrixZ(work, work, -rz);

    sceVu0TransMatrix(local_world, work, pos);
    sceVu0MulMatrix(local_screen, pCam->matWorldScreen, local_world);
    sceVu0MulMatrix(local_clip, pCam->matWorldClipPolygon, local_world);

    g3dxVu0CopyVector(wpos, pos);

    switch (tp2)                                                /* 3300 */
    {
    case 0:                     /* the ordinary torch */
        wpos[1] -= 40.0f;

        alp   = (u_char)((float)hh->disp * 80.0f * ar / (float)hh->max);
        htype = 0;
        hr = 0x80; hg = 0x5c; hb = 0x3c;
        escl  = 1.13999999f;

        SubHalo(wpos, htype, 0, 0, hr, hg, hb, alp,
                ((float)hh->disp * 0.299999982f * sr) / (float)hh->max);

        psize = size * 3.79999995f;
        break;

    case 1:                     /* the cold blue flame */
        wpos[1] -= 40.0f;

        alp   = (u_char)((float)hh->disp * 80.0f * ar / (float)hh->max);
        htype = 0;
        hr = 0x3c; hg = 0x5c; hb = 0x80;
        escl  = 1.13999999f;

        SubHalo(wpos, htype, 0, 0, hr, hg, hb, alp,
                ((float)hh->disp * 0.299999982f * sr) / (float)hh->max);

        psize = size * 3.79999995f;
        break;

    case 2:                     /* the darkest blue: a wash plus a white core */
        wpos[1] -= 20.0f;

        SubHalo(wpos, 0, 0, 0, 0x3c, 0x5c, 0x80,
                (u_char)((float)hh->disp * 80.0f * ar / (float)hh->max),
                ((float)hh->disp * 0.199999988f * sr) / (float)hh->max);

        wpos[1] += 10.0f;

        alp   = (u_char)((float)hh->disp * 200.0f * ar / (float)hh->max);
        htype = 0;
        hr = 0xff; hg = 0xff; hb = 0xff;
        escl  = 1.19999993f;

        SubHalo(wpos, htype, 0, 0, hr, hg, hb, alp,
                ((float)hh->disp * 0.0399999991f * sr) / (float)hh->max);

        psize = size * 3.79999995f;
        break;

    case 3:                     /* the big torch */
        ysp1 = 0.119999997f;
        ysp2 = 0.099999994f;

        wpos[1] -= 40.0f;

        alp   = (u_char)((float)hh->disp * 128.0f * ar / (float)hh->max);
        htype = 2;
        hr = 0xff; hg = 0xa0; hb = 0x80;
        escl  = 3.39999986f;

        SubHalo(wpos, htype, 0, 0, hr, hg, hb, alp,
                ((float)hh->disp * 0.799999952f * sr) / (float)hh->max);

        psize = size * 3.79999995f;
        break;

    case 4:                     /* the flare */
        wpos[1] -= 40.0f;

        ysp1 = 0.159999996f;
        ysp2 = 0.299999982f;

        SubHalo(wpos, 2, 0, 0, 0xff, 0xa0, 0x80,
                (u_char)((float)hh->disp * 128.0f * ar / (float)hh->max),
                ((float)hh->disp * 0.799999952f * sr) / (float)hh->max);

        escl  = 4.79999971f;
        psize = size * 3.79999995f;
        break;

    default:
        psize = size * rrate;
        escl  = 3.39999986f;
        break;
    }

    hh->disp = (short)draw_distortion_particles(local_screen, local_clip,
                                                (int)(sys_wrk.count & 1),
                                                200, &hh->particles[0],
                                                (psize * sr) / pCam->fFov,
                                                -1.0f, type);

    /* One shared horizontal drift for every particle added this frame. */
    wind = EffectGetRandom(-0.00999999978f, 0.00999999978f);

    if (EffWrkStopFlgGet() == 0 && st > 0)
    {
        for (i = 0; i < st; i++)
        {
            if (tp2 >= 0 && tp2 < 3)
            {
                for (j = 0; j < 4; j++)
                {
                    ppos[0] = EffectGetRandom(-0.5f, 0.5f) * 0.699999988f * escl;
                    ppos[1] = 0.0f;
                    ppos[2] = EffectGetRandom(-0.5f, 0.5f) * 0.699999988f * escl;

                    pvel[0] = EffectGetRandom(-0.5f, 0.5f) * 0.0199999996f + wind;
                    pvel[1] = EffectGetRandom(-0.049999997f, -0.0799999982f);
                    pvel[2] = EffectGetRandom(-0.5f, 0.5f) * 0.0199999996f;

                    add_particle(type, hh, ppos, pvel, r, g, b, a * ar);
                }
            }
            else if (tp2 >= 3 && tp2 < 5)
            {
                for (j = 0; j < 4; j++)
                {
                    ppos[0] = EffectGetRandom(-0.5f, 0.5f) * 0.899999976f * escl;
                    ppos[1] = 0.0f;
                    ppos[2] = EffectGetRandom(-0.5f, 0.5f) * 0.899999976f * escl;

                    pvel[0] = EffectGetRandom(-0.5f, 0.5f) * 0.0199999996f * escl
                            + wind;
                    pvel[1] = EffectGetRandom(-ysp2, -ysp2 - ysp1);
                    pvel[2] = EffectGetRandom(-0.5f, 0.5f) * 0.0199999996f * escl;

                    add_particle(type, hh, ppos, pvel, r, g, b, a * ar);
                }
            }

            hh->cnt++;

            update_particles(&hh->particles[0]);
        }
    }

    return addr;                                                /* 3407 */
}

/* ==========================================================================
 *  The dust a run kicks up
 *
 *  Thirty particles about a fixed point, drawn through the shared distortion
 *  billboard so they refract slightly rather than just being grey sprites.
 *  Unlike the haze volume this one is fire-and-forget: once every particle has
 *  expired CloudOfDustCtrl() frees its own block and reports null, and SetDust()
 *  tears the effect down.
 * ======================================================================== */

/* 3518 -- claim and clear a dust cloud.  BrakeRate 0.75 is what makes the puff
 * stall rather than keep travelling. */
static void CloudOfDustCtrlInit(CLOUD_OF_DUST_CTRL *pCod, float *BasePos)
{
    int i;

    if (pCod != nullptr)                                        /* 3521 */
    {
        for (i = 0; i < 30; i++)                                /* 3523 */
        {
            pCod->particles[i].position[0] = 0.0f;
            pCod->particles[i].position[1] = 0.0f;
            pCod->particles[i].position[2] = 0.0f;
            pCod->particles[i].position[3] = 1.0f;
            pCod->particles[i].color[3]    = 0.0f;
            pCod->particles[i].lifetime    = 0;
        }

        g3dxVu0CopyVector(pCod->Pos, BasePos);

        pCod->head      = 0;
        pCod->BrakeRate = 0.75f;
        pCod->cnt       = 0;
        pCod->disp      = 0;
        pCod->blife     = 0;
    }
}

/* 3547 -- add one grain.
 *
 * Every speed is drawn around the same 1000 bias the haze parameters use, so
 * the horizontal spread is +/- 0.5 a frame and the vertical drift is a small
 * positive push.  Colour is a flat dark grey at alpha 23; the lifetime is 20 to
 * 47 frames, and BaseLifeTime keeps it so the fade knows where it started. */
static void AddCloudOfDustRunParticle(CLOUD_OF_DUST_CTRL *pCod)
{
    PARTICLE *p;

    p = &pCod->particles[pCod->head];                           /* 3552 */

    p->position[0] = 0.0f;
    p->position[1] = 0.0f;
    p->position[2] = 0.0f;
    p->position[3] = 1.0f;

    p->velocity[0] = (EffectGetRandom(950.0f, 1050.0f) - 1000.0f) / 100.0f;
    p->velocity[1] = (EffectGetRandom(1005.0f, 1010.0f) - 1000.0f) / 100.0f;
    p->velocity[2] = (EffectGetRandom(950.0f, 1050.0f) - 1000.0f) / 100.0f;

    p->acceleration[0] = 0.0f;
    p->acceleration[1] = (EffectGetRandom(1005.0f, 1020.0f) - 1000.0f) / 1000.0f;
    p->acceleration[2] = 0.0f;

    p->color[0] = 50.0f;
    p->color[1] = 50.0f;
    p->color[2] = 47.0f;
    p->color[3] = 23.0f;

    p->alp_step = 0.0f;
    p->Scale    = 3.0f;

    p->lifetime     = (int)EffectGetRandom(20.0f, 47.0f);
    p->BaseLifeTime = p->lifetime;

    pCod->head = (pCod->head + 1) % 30;                         /* 3590 */
}

/* 3601 -- step the cloud and report how many grains are still alive.
 *
 * The velocity is scaled by BrakeRate *after* it has been applied and before
 * the acceleration is added back, which is what lets a grain keep drifting
 * upward while its horizontal travel dies away.  Alpha and scale both ramp on
 * the fraction of the grain's own life used up. */
int UpdateCloudOfDustParticles(PARTICLE *pPtop, float BrakeRate, int StartAlpha, int EndAlpha, float StartScale, float EndScale)
{
    int   i;
    int   num;
    float rate;

    num = 0;

    if (pPtop != nullptr)                                       /* 3606 */
    {
        for (i = 0; i < 30; i++)                                /* 3608 */
        {
            if (pPtop[i].BaseLifeTime == 0)
            {
                rate = 1.0f;
            }
            else
            {
                rate = 1.0f - (float)pPtop[i].lifetime /
                              (float)pPtop[i].BaseLifeTime;
            }

            if (pPtop[i].lifetime == 0)
            {
                pPtop[i].color[3] = 0.0f;
            }
            else
            {
                sceVu0AddVector(pPtop[i].position, pPtop[i].position,
                                pPtop[i].velocity);
                sceVu0ScaleVector(pPtop[i].velocity, pPtop[i].velocity,
                                  BrakeRate);
                sceVu0AddVector(pPtop[i].velocity, pPtop[i].velocity,
                                pPtop[i].acceleration);

                num++;
                pPtop[i].lifetime--;

                pPtop[i].color[3] = (float)(EndAlpha - StartAlpha) * rate +
                                    (float)StartAlpha;
                pPtop[i].Scale    = (EndScale - StartScale) * rate + StartScale;
            }
        }
    }

    return num;                                                 /* 3634 */
}

/* 3640 -- draw the cloud, then step it.
 *
 * The particle size is divided by the camera's field of view, so the puff keeps
 * its apparent size when the camera zooms.  Returning null once the last grain
 * has gone is how the effect ends -- and note the block is freed here, by the
 * routine that drew it. */
static void *CloudOfDustCtrl(CLOUD_OF_DUST_CTRL *pCod, float size)
{
    GRA3DCAMERA *pCam;
    float *cam_pos;
    float work[4][4];
    float local_clip[4][4];
    float local_world[4][4];
    float local_screen[4][4];
    float RotX;
    float RotY;

    pCam    = gra3dGetCamera();                                 /* 3645 */
    cam_pos = gra3dcamGetPosition();

    if (pCod == nullptr)                                        /* 3650 */
    {
        return nullptr;
    }

    Get2PosRot(cam_pos, pCam->vTarget, &RotX, &RotY);

    sceVu0UnitMatrix(work);
    work[0][0] = 25.0f;
    work[1][1] = 25.0f;
    work[2][2] = 25.0f;
    sceVu0RotMatrixX(work, work, RotX);
    sceVu0RotMatrixY(work, work, RotY);
    sceVu0TransMatrix(local_world, work, pCod->Pos);

    sceVu0MulMatrix(local_screen, pCam->matWorldScreen, local_world);
    sceVu0MulMatrix(local_clip, pCam->matWorldClipPolygon, local_world);

    pCod->disp = (short)draw_distortion_particles2(
        local_screen, local_clip, 30, &pCod->particles[0],
        size / pCam->fFov,
        effdat[EffWrkMonochroModeGet() + 0x10].tex0, 0x44);

    if (EffWrkStopFlgGet() == 0)
    {
        if (UpdateCloudOfDustParticles(&pCod->particles[0], pCod->BrakeRate,
                                       23, 0, 3.26999998f, 8.02999973f) == 0)
        {
            EFFECT_FREE(pCod);
            return nullptr;
        }
    }

    return pCod;                                                /* 3699 */
}

/* ==========================================================================
 *  Falling leaves and the drifting dust
 *
 *  Six drifts of sixteen, and the two are the same code with different
 *  authoring: type 0 is eight leaves in a wide 180-degree fan at alpha 0x80,
 *  type 1 is sixteen much dimmer dust motes in a 104-degree fan at alpha 0x1c.
 * ======================================================================== */

/* 3733 -- start a drift at `pos`.
 *
 * The player's own movement state scales it: walking gives the full spread,
 * and each of the four slower states damps the horizontal speed and lowers the
 * vertical push, so leaves kicked up by a run travel further than ones stirred
 * by a crouch.  A drift is dropped silently if all six slots are busy. */
void SetDustLeaf(float *pos, int type)
{
    /* The two authored base colours.  rgb1's initialiser is the .sdata blob at
       0x3efe88; rgb2's folded into immediates. */
    u_char rgb1[3] = { 18, 24, 30 };
    u_char rgb2[3] = { 28, 24, 20 };
    u_char mr, mg, mb;
    u_char mrr, mrg, mrb;
    int    i;
    int    num;
    int    status;
    int    leaf_num;
    float  angr;
    float  wvy;
    float  mv1x;
    float  mangr;
    EFF_LEAF *lep;

    if (type == 0)                                              /* 3745 */
    {
        num  = 8;
        mrr  = 2;
        mrg  = 2;
        mrb  = 2;
        mv1x = 20.0f;
        angr = 180.0f;
        mr   = rgb1[0];
        mg   = rgb1[1];
        mb   = rgb1[2];
    }
    else
    {
        num  = 16;
        mrr  = 1;
        mrg  = 1;
        mrb  = 1;
        mv1x = 34.0f;
        angr = 104.0f;
        mr   = rgb2[0];
        mg   = rgb2[1];
        mb   = rgb2[2];
    }

    leaf_num = -1;

    for (i = 0; i < 6; i++)                                     /* 3771 */
    {
        if (eff_leaf[i].flag == 0)
        {
            leaf_num = i;
            break;
        }
    }

    if (leaf_num == -1)                                         /* 3777 */
    {
        return;
    }

    status = plyr_wrk.cmn_wrk.st.sta;

    if ((status & 1) != 0)
    {
        wvy = 20.0f;
    }
    else if ((status & 2) != 0)
    {
        mv1x = mv1x * 0.399999976f;
        wvy  = 7.99999952f;
    }
    else if ((status & 4) != 0)
    {
        mv1x = mv1x * 0.299999982f;
        wvy  = 5.99999952f;
    }
    else if ((status & 8) != 0)
    {
        mv1x = mv1x * 0.199999988f;
        wvy  = 3.99999976f;
    }
    else
    {
        mv1x = mv1x * 0.699999988f;
        wvy  = 13.999999f;
    }

    angr = angr * 3.1415925f / 180.0f;

    lep       = &eff_leaf[leaf_num];
    lep->flag = 1;
    lep->type = type;

    g3dxVu0CopyVector(lep->bpos, pos);

    for (i = 0; i < num; i++)
    {
        /* Note the argument order: the fan is drawn from +half to -half, so
           EffectGetRandom()'s min is the larger of the two.  Faithful. */
        mangr = EffectGetRandom(angr * 0.5f, -angr * 0.5f);

        if (mangr < -3.1415925f)
        {
            mangr += 6.28318501f;
        }
        else if (mangr > 3.1415925f)
        {
            mangr -= 6.28318501f;
        }

        lep->lo[i].mang = mangr;
        lep->lo[i].cnt  = 0.0f;

        lep->lo[i].pos[0] = 0.0f;
        lep->lo[i].pos[1] = 0.0f;
        lep->lo[i].pos[2] = 0.0f;

        lep->lo[i].vel[0] = EffectGetRandom(mv1x, mv1x + 10.0f);
        lep->lo[i].vel[1] = EffectGetRandom(wvy, wvy + 15.0f);

        lep->lo[i].fl = 0;

        lep->lo[i].ang[0] = EffectGetRandom(-3.1415925f, 3.1415925f);
        lep->lo[i].ang[1] = EffectGetRandom(-3.1415925f, 3.1415925f);

        lep->lo[i].r = (u_char)EffectGetRandom((float)mr, (float)(mr + mrr));
        lep->lo[i].g = (u_char)EffectGetRandom((float)mg, (float)(mg + mrg));
        lep->lo[i].b = (u_char)EffectGetRandom((float)mb, (float)(mb + mrb));

        lep->lo[i].a = (lep->type == 0) ? 0x80 : 0x1c;
    }
}

/* 3854 / 3860 -- the two named fronts on SetDustLeaf(): leaves are type 0 and
 * the heavier dust drift is type 1. */
void SetLeaf(float *pos)
{
    SetDustLeaf(pos, 0);                                        /* 3855 */
}

void SetDust2(float *pos)
{
    SetDustLeaf(pos, 1);                                        /* 3861 */
}

/* 3865 -- step and draw one drift.
 *
 * Each leaf follows a plain ballistic arc in its own plane -- x is speed times
 * time, y is `3t^2 - vt` -- and `mang` is the heading that plane is rotated to
 * about Y.  `fl` tracks where in the arc it is: 0 still rising, 1 past the top,
 * 2 back at or below the launch height.  When every leaf has reached 2 the
 * whole drift releases its slot.
 *
 * The two types diverge in three places.  A leaf tumbles on its own two
 * authored angles and is 6 x 13.5 units; a dust mote is a 40 x 40 camera-facing
 * billboard.  A leaf steps at 0.3 throughout, while a mote steps at 0.3 only
 * while it is rising and then crawls at 0.06.  And only eight leaves are
 * stepped against twelve motes -- SetDustLeaf() seeds sixteen for type 1, so
 * the last four motes are authored and then never touched.
 *
 * The quads are depth sorted back to front on a fifth transformed point (the
 * quad's own centre, which is what ppos[][4] is for) before they are drawn.
 *
 * ROM BUG, reproduced: the `fl != 2` test that skips a finished leaf is indexed
 * by the *loop* counter while everything else in the same iteration is indexed
 * through the sort order, so a leaf can be suppressed because a different leaf
 * has landed.  It shows as the odd leaf blinking out early once a drift starts
 * to settle. */
void RunLeafSub(EFF_LEAF *lep)
{
    fixed_array<int, 16> so;
    int   disp[32];
    float rot_x;
    float rot_y;
    int   ivec[16][5][4];
    float wlm[4][4];
    float slm[4][4];
    float wpos[4];

    /* .rodata 0x3a6f60, a local initialiser: the leaf quad then the dust quad,
       each with a fifth centre point used only as the sort key. */
    float ppos[2][5][4] =
    {
        {
            {  -6.0f, 13.5f, 0.0f, 1.0f },
            {   6.0f, 13.5f, 0.0f, 1.0f },
            {  -6.0f,  0.0f, 0.0f, 1.0f },
            {   6.0f,  0.0f, 0.0f, 1.0f },
            {   0.0f, 6.69999981f, 0.0f, 1.0f },
        },
        {
            { -20.0f,  20.0f, 0.0f, 1.0f },
            {  20.0f,  20.0f, 0.0f, 1.0f },
            { -20.0f, -20.0f, 0.0f, 1.0f },
            {  20.0f, -20.0f, 0.0f, 1.0f },
            {   0.0f,   0.0f, 0.0f, 1.0f },
        },
    };

    DRAW_ENV_5 env;
    int th;
    int tw;
    int leaf_num;
    GRA3DCAMERA *pCam;
    float *cam_pos;
    Q_WORDDATA *pbuf;
    int   ndpkt;
    int   i;
    int   j;
    int   k;
    int   n;
    int   ClipFlg;
    float step;
    float sn, cs;

    /* PORT-ONLY: the world matrices are kept so the host bridge can draw the
       quads in the sorted order.  The ROM only needs the screen-space ivec by
       then, so it does not keep them. */
    float wlmSave[16][4][4];

    pCam    = gra3dGetCamera();
    cam_pos = gra3dcamGetPosition();

    leaf_num = (lep->type != 0) ? 12 : 8;

    for (i = 0; i < leaf_num; i++)
    {
        lep->lo[i].pos[0] = lep->lo[i].vel[0] * lep->lo[i].cnt;
        lep->lo[i].pos[1] = -lep->lo[i].vel[1] * lep->lo[i].cnt +
                            3.0f * lep->lo[i].cnt * lep->lo[i].cnt;

        if (lep->lo[i].pos[1] > 0.0f)
        {
            lep->lo[i].fl = 2;
        }
        else if (lep->lo[i].opos[1] < lep->lo[i].pos[1])
        {
            lep->lo[i].fl = 1;
        }
        else
        {
            lep->lo[i].fl = 0;
        }

        step = 0.299999982f;
        if (lep->type != 0)
        {
            step = 0.0599999912f;
            if (lep->lo[i].fl < 1)
            {
                step = 0.299999982f;
            }
        }

        if (EffWrkStopFlgGet() == 0 && stop_lf == 0)
        {
            lep->lo[i].cnt += step;
        }

        g3dxVu0CopyVector(lep->lo[i].opos, lep->lo[i].pos);

        g3dxVu0CopyVector(wpos, lep->bpos);

        sn = sinf(lep->lo[i].mang);
        cs = cosf(lep->lo[i].mang);

        wpos[0] += lep->lo[i].pos[0] * cs - lep->lo[i].pos[2] * sn;
        wpos[1] += lep->lo[i].pos[1];
        wpos[2] += lep->lo[i].pos[0] * sn + lep->lo[i].pos[2] * cs;

        sceVu0UnitMatrix(wlm);
        wlm[0][0] = 4.0f;
        wlm[1][1] = 4.0f;
        wlm[2][2] = 4.0f;

        if (lep->type == 0)
        {
            sceVu0RotMatrixX(wlm, wlm, lep->lo[i].ang[0]);
            sceVu0RotMatrixY(wlm, wlm, lep->lo[i].ang[1]);
        }
        else
        {
            Get2PosRot(cam_pos, pCam->vTarget, &rot_x, &rot_y);
            sceVu0RotMatrixX(wlm, wlm, rot_x);
            sceVu0RotMatrixY(wlm, wlm, rot_y);
        }

        sceVu0TransMatrix(wlm, wlm, wpos);
        sceVu0MulMatrix(slm, pCam->matWorldScreen, wlm);

        memcpy(wlmSave[i], wlm, sizeof(wlm));

        ClipFlg = 0;

        for (j = 0; j < 5; j++)
        {
            sceVu0RotTransPers(ivec[i][j], slm, ppos[lep->type][j], 0);

            if ((u_int)(ivec[i][j][0] - 0x4000) > 0x8000)
            {
                ClipFlg = 1;
            }
            if ((u_int)(ivec[i][j][1] - 0x4000) > 0x8000)
            {
                ClipFlg = 1;
            }
            if ((u_int)(ivec[i][j][2] - 0xff) > 0xffff00)
            {
                ClipFlg = 1;
            }
        }

        disp[i] = (ClipFlg == 0);
    }

    /* Every leaf at fl == 2 sums to twice the count, and the drift ends. */
    n = 0;
    for (i = 0; i < leaf_num; i++)
    {
        n += lep->lo[i].fl;
    }

    if (n / 2 == leaf_num)
    {
        lep->flag = 0;
    }

    for (i = 0; i < leaf_num; i++)
    {
        so[i] = i;
    }

    for (i = 0; i < leaf_num - 1; i++)
    {
        for (j = i + 1; j < leaf_num; j++)
        {
            if (ivec[so[j]][4][2] < ivec[so[i]][4][2])
            {
                k     = so[j];
                so[j] = so[i];
                so[i] = k;
            }
        }
    }

    tw = (int)(u_short)effdat[EffWrkMonochroModeGet() + 10].w << 4;
    th = (int)(u_short)effdat[EffWrkMonochroModeGet() + 10].h << 4;

    env.alpha = 0x44;
    env.tex1  = 0x161;
    env.clamp = 0;
    env.test  = 0x5000dULL;
    env.zbuf  = 0x10a000118ULL;

    SetDrawEnv(0, &env);

    pbuf = StartDmaDirectTrans();
    Reserve2DPacket(0x10);

    pbuf[0].ul64[0] = 0x1000000000008002ULL;
    pbuf[0].ul64[1] = 0x0e;
    pbuf[1].ul64[0] = 0;
    pbuf[1].ul64[1] = 0x3f;
    pbuf[2].ul64[0] = effdat[EffWrkMonochroModeGet() + 10].tex0;
    pbuf[2].ul64[1] = 0x06;

    ndpkt = 3;

    for (i = 0; i < leaf_num; i++)
    {
        int  r, g, b;

        /* See the header note: this test uses `i`, the rest of the body uses
           so[i].  That is the ROM's own indexing. */
        if (lep->lo[i].fl == 2)
        {
            continue;
        }

        k = so[i];

        if (disp[k] == 0)
        {
            continue;
        }

        /* PRIM 0x5c with UV rather than ST: TRIANGLE_STRIP, gouraud, textured,
           alpha blended, addressed in 1/16 texels. */
        pbuf[ndpkt].ul64[0] = 0x30ae400000008004ULL;
        pbuf[ndpkt].ul64[1] = 0x431;
        ndpkt++;

        if (EffWrkMonochroModeGet() == 0)
        {
            r = lep->lo[k].r;
            g = lep->lo[k].g;
            b = lep->lo[k].b;
        }
        else
        {
            r = ((int)lep->lo[k].r + (int)lep->lo[k].g + (int)lep->lo[k].b) / 3;
            r &= 0xff;
            g = r;
            b = r;
        }

        for (j = 0; j < 4; j++)
        {
            pbuf[ndpkt].ui32[0] = (u_int)r;
            pbuf[ndpkt].ui32[1] = (u_int)g;
            pbuf[ndpkt].ui32[2] = (u_int)b;
            pbuf[ndpkt].ui32[3] = (u_int)lep->lo[k].a;

            pbuf[ndpkt + 1].ui32[0] = (u_int)((j & 1) ? tw - 8 : 8);
            pbuf[ndpkt + 1].iv[1]   = (j / 2) ? th - 8 : 8;
            pbuf[ndpkt + 1].ui32[2] = 0;
            pbuf[ndpkt + 1].ui32[3] = 0;

            pbuf[ndpkt + 2].iv[0] = ivec[k][j][0];
            pbuf[ndpkt + 2].iv[1] = ivec[k][j][1];
            pbuf[ndpkt + 2].iv[2] = ivec[k][j][2];
            /* ADC: the first two vertices only prime the strip. */
            pbuf[ndpkt + 2].ui32[3] = (j < 2) ? 0x8000 : 0;

            ndpkt += 3;
        }

        /* Host path: the same quad through the renderer bridge. */
        {
            const sceGsTex0 *pTx0 = (const sceGsTex0 *)
                &effdat[EffWrkMonochroModeGet() + 10].tex0;
            float aWorld[4][4];

            for (j = 0; j < 4; j++)
            {
                sceVu0ApplyMatrix(aWorld[j], wlmSave[k], ppos[lep->type][j]);
            }

            RendererPacket3D(aWorld, 4, r, g, b, lep->lo[k].a,
                             0.5f, 0.5f,
                             (float)(tw >> 4) - 1.0f, (float)(th >> 4) - 1.0f,
                             (float)(1 << pTx0->TW), (float)(1 << pTx0->TH),
                             pTx0);
        }
    }

    EndDmaDirectTrans(pbuf + ndpkt);
}

/* 4183 -- claim a haze volume's work block.
 *
 * pBasePos / pRot / pAlphaRate are kept as *pointers* into the caller's own
 * storage, which is what lets a ghost's haze follow the ghost with no per-frame
 * update from enemy.c.  A null pRot means the volume does not rotate with
 * anything and its authored offset is dropped (see the ReqOneFrame below). */
static void HazeCtrlInit(HAZE_CTRL *pHc, float (*pBasePos)[4], float (*pRot)[4], int Id, int Type, float *pAlphaRate)
{
    int i;

    if (pHc != nullptr)                                         /* 4186 */
    {
        for (i = 0; i < 64; i++)                                /* 4188 */
        {
            pHc->Particles[i].Position[0] = 0.0f;
            pHc->Particles[i].Position[1] = 0.0f;
            pHc->Particles[i].Position[2] = 0.0f;
            pHc->Particles[i].Position[3] = 1.0f;
            pHc->Particles[i].Alpha       = 0;
            pHc->Particles[i].Lifetime    = 0;
            pHc->Particles[i].Scale       = 0.0f;
            pHc->Particles[i].RotZ        = 0.0f;
        }

        pHc->Type              = (short)Type;
        pHc->pPos              = (sceVu0FVECTOR *)pBasePos;
        pHc->pRot              = (sceVu0FVECTOR *)pRot;
        pHc->pAlphaRate        = pAlphaRate;
        pHc->Id                = Id;
        pHc->AllVelocityTime   = 0;
        pHc->AllVelocityStatus = 0;
        pHc->disp              = 0;
    }
}

/* 4266 -- draw a haze volume, then step it.
 *
 * Every live particle becomes a camera-facing 10x10 quad scaled by its own
 * Scale and spun by its own RotZ.  The projected corners go into one shared
 * ivec[256] (four per quad, so 64 quads fill it exactly), and ToPolyNo[] maps
 * a slot in that buffer back to the particle it came from.
 *
 * The quads are then **depth sorted back to front** by a bubble sort over
 * Order[], keyed on the first corner's z, and drawn one GIF packet each.  That
 * sort is the reason the volume reads as a volume rather than as a pile of
 * flat cards: soft alpha billboards have to be composited in order.
 *
 * The parameter set's five AlphaBlend fields are assembled straight into the
 * GS ALPHA register here -- A/B/C/D in bits 0..7 and FIX at bit 32 -- which is
 * how Sae's volume gets a bright cyan wash out of the same code that gives
 * Kusabi a dark smear.
 *
 * A pAlphaRate that has reached zero kills every particle outright rather than
 * fading them, so a ghost that vanishes takes its haze with it in one frame.
 *
 * `size` is unused: the quad size is the fixed 25 in BaseMat.  Returns the
 * control block, or null if there was none -- SetHaze() reads that as "tear
 * the effect down". */
static void *HazeCtrl(HAZE_CTRL *pHazeCtrl, float size)
{
    float WorkMat[4][4];
    float LocalWorld[4][4];
    float LocalScreen[4][4];
    float BaseMat[4][4];
    GRA3DCAMERA *pCam;
    float *cam_pos;
    float RotX;
    float RotY;
    fixed_array<sceVu0IVECTOR, 256> ivec;
    int   PolyNo;

    /* .rodata local initialiser: a 10x10 quad in haze-local units. */
    float PolyDat[4][4] =                                       /* 4281 */
    {
        { -5.0f,  5.0f, 0.0f, 1.0f },
        {  5.0f,  5.0f, 0.0f, 1.0f },
        { -5.0f, -5.0f, 0.0f, 1.0f },
        {  5.0f, -5.0f, 0.0f, 1.0f },
    };

    int i;
    int j;
    fixed_array<int, 64> Order;
    fixed_array<int, 64> ToPolyNo;
    HAZE_PARAMETER *pHazeParam;
    int   ColorR;
    int   ColorG;
    int   ColorB;
    float AlphaRate;
    int   ClipFlg;
    int   BufNo;
    int   Key;
    HAZE_PARTICLE *pPart;
    sceVu0IVECTOR *pIvec;
    u_long Tex0;
    int    Alpha;
    float  st[4][2];
    Q_WORDDATA *pbuf;
    U32DATA ts, tt, tq;
    float AllVelocity[4];
    DRAW_ENV_5 env;

    (void)size;

    pCam    = gra3dGetCamera();                                 /* 4270 */
    cam_pos = gra3dcamGetPosition();                            /* 4271 */

    PolyNo = 0;                                                 /* 4280 */

    if (pHazeCtrl == nullptr)                                   /* 4296 */
    {
        return nullptr;
    }

    pHazeParam = EffectHazeGetParameterPtr(pHazeCtrl->Type);    /* 4306 */

    /* Type 2 is Sae's volume, and it keeps its colour even in monochrome --
       the wash is the effect. */
    if (EffWrkMonochroModeGet() == 0 || pHazeCtrl->Type == 2)   /* 4308 / 4309 */
    {
        ColorR = pHazeParam->R;                                 /* 4312 */
        ColorG = pHazeParam->G;                                 /* 4313 */
        ColorB = pHazeParam->B;                                 /* 4314 */
    }
    else
    {
        ColorR = (pHazeParam->R + pHazeParam->G + pHazeParam->B) / 3;
        ColorG = ColorR;
        ColorB = ColorR;
    }

    /* GS ALPHA: A bits 0-1, B 2-3, C 4-5, D 6-7, FIX at 32. */
    env.alpha = (u_long)pHazeParam->AlphaBlendA
              | ((u_long)pHazeParam->AlphaBlendB << 2)
              | ((u_long)pHazeParam->AlphaBlendC << 4)
              | ((u_long)pHazeParam->AlphaBlendD << 6)
              | ((u_long)pHazeParam->AlphaBlendFIX << 32);      /* 4318 */
    env.tex1  = 0x161;
    env.clamp = 0;
    env.test  = 0x5000dULL;
    env.zbuf  = 0x10a000118ULL;

    SetDrawEnv(0, &env);                                        /* 4325 */

    if (pHazeCtrl->pAlphaRate == nullptr)                       /* 4328 */
    {
        AlphaRate = 1.0f;
    }
    else
    {
        AlphaRate = *pHazeCtrl->pAlphaRate;                     /* 4329 */

        if (AlphaRate == 0.0f)                                  /* 4331 */
        {
            for (i = 0; i < 64; i++)                            /* 4332 */
            {
                pHazeCtrl->Particles[i].Lifetime = 0;           /* 4333 */
            }
        }
    }

    /* One camera-facing basis, built once and shared by every quad. */
    Get2PosRot(cam_pos, pCam->vTarget, &RotX, &RotY);           /* 4342 */

    sceVu0UnitMatrix(WorkMat);                                  /* 4343 */
    WorkMat[0][0] = 25.0f;                                      /* 4344 */
    WorkMat[1][1] = 25.0f;
    WorkMat[2][2] = 25.0f;
    sceVu0RotMatrixX(WorkMat, WorkMat, RotX);                   /* 4345 */
    sceVu0RotMatrixY(BaseMat, WorkMat, RotY);                   /* 4346 */

    BufNo = 0;

    for (i = 0; i < 64; i++)                                    /* 4349 */
    {
        ClipFlg = 0;

        if (pHazeCtrl->Particles[i].Lifetime == 0)              /* 4352 */
        {
            continue;
        }

        sceVu0UnitMatrix(WorkMat);                              /* 4353 */
        WorkMat[0][0] = pHazeCtrl->Particles[i].Scale;          /* 4354 */
        WorkMat[1][1] = WorkMat[0][0];
        WorkMat[2][2] = WorkMat[0][0];

        sceVu0RotMatrixZ(WorkMat, WorkMat,
                         pHazeCtrl->Particles[i].RotZ);         /* 4355 */
        sceVu0MulMatrix(WorkMat, BaseMat, WorkMat);             /* 4356 */
        sceVu0TransMatrix(LocalWorld, WorkMat,
                          pHazeCtrl->Particles[i].Position);    /* 4357 */
        sceVu0MulMatrix(LocalScreen, pCam->matWorldScreen, LocalWorld); /* 4358 */

        for (j = 0; j < 4; j++)                                 /* 4360 */
        {
            sceVu0RotTransPers(ivec[BufNo + j], LocalScreen, PolyDat[j], 0); /* 4361 */

            if (ivec[BufNo + j][0] < 0x4000 || ivec[BufNo + j][0] > 0xc000)
            {
                ClipFlg = 1;
            }
            if (ivec[BufNo + j][1] < 0x4000 || ivec[BufNo + j][1] > 0xc000)
            {
                ClipFlg = 1;
            }
            if (ivec[BufNo + j][2] < 0xff || ivec[BufNo + j][2] > 0xffffff)
            {
                ClipFlg = 1;
            }
        }                                                       /* 4366 */

        if (ClipFlg == 0)
        {
            ToPolyNo[PolyNo] = i;
            BufNo += 4;
            PolyNo++;
        }
    }

    for (i = 0; i < PolyNo; i++)
    {
        Order[i] = i;
    }

    /* Back to front, on the first corner's z. */
    for (i = 0; i < PolyNo - 1; i++)
    {
        for (j = i + 1; j < PolyNo; j++)
        {
            if (ivec[Order[j] * 4][2] < ivec[Order[i] * 4][2])
            {
                Key      = Order[j];
                Order[j] = Order[i];
                Order[i] = Key;
            }
        }
    }

    for (i = 0; i < PolyNo; i++)
    {
        pPart = &pHazeCtrl->Particles[ToPolyNo[Order[i]]];
        pIvec = &ivec[Order[i] * 4];

        /* Note the ST corners run 1,1 / 1,0 / 0,1 / 0,0 -- the texture is
           mirrored against PolyDat's corner order. */
        st[0][0] = 1.0f; st[0][1] = 1.0f;
        st[1][0] = 1.0f; st[1][1] = 0.0f;
        st[2][0] = 0.0f; st[2][1] = 1.0f;
        st[3][0] = 0.0f; st[3][1] = 0.0f;

        Alpha = (int)((float)pPart->Alpha * AlphaRate);
        Tex0  = effdat[EffWrkMonochroModeGet() + 0x54].tex0;

        pbuf = StartDmaDirectTrans();
        Reserve2DPacket(0x10);

        pbuf[0].ul64[0] = 0x1000000000008002ULL;
        pbuf[0].ul64[1] = 0x0e;
        pbuf[1].ul64[0] = 0;
        pbuf[1].ul64[1] = 0x3f;
        pbuf[2].ul64[0] = Tex0;
        pbuf[2].ul64[1] = 0x06;
        pbuf[3].ul64[0] = 0x302e400000008004ULL;
        pbuf[3].ul64[1] = 0x412;

        for (j = 0; j < 4; j++)
        {
            tq.fl32 = 1.0f / (float)pIvec[j][3];
            ts.fl32 = tq.fl32 * st[j][0];
            tt.fl32 = tq.fl32 * st[j][1];

            pbuf[4 + j * 3].ui32[0] = (u_int)ts.ui32;
            pbuf[4 + j * 3].ui32[1] = (u_int)tt.ui32;
            pbuf[4 + j * 3].ui32[2] = (u_int)tq.ui32;
            pbuf[4 + j * 3].ui32[3] = 0;

            pbuf[5 + j * 3].iv[0] = ColorR;
            pbuf[5 + j * 3].iv[1] = ColorG;
            pbuf[5 + j * 3].iv[2] = ColorB;
            pbuf[5 + j * 3].iv[3] = Alpha;

            pbuf[6 + j * 3].iv[0] = pIvec[j][0];
            pbuf[6 + j * 3].iv[1] = pIvec[j][1];
            pbuf[6 + j * 3].iv[2] = pIvec[j][2];
            /* ADC: the first two vertices only prime the strip. */
            pbuf[6 + j * 3].ui32[3] = (j < 2) ? 0x8000 : 0;
        }

        /* Host path.  The DIRECT packet is inert in this port, so the same quad
           is queued through the renderer bridge.  The world matrix is rebuilt
           rather than kept from the pass above, because the sort reorders the
           quads and only the particle index survives it. */
        {
            const sceGsTex0 *pTx0 =
                (const sceGsTex0 *)&effdat[EffWrkMonochroModeGet() + 0x54];
            float tw = (float)(1 << pTx0->TW);
            float th = (float)(1 << pTx0->TH);
            float aWorld[4][4];

            sceVu0UnitMatrix(WorkMat);
            WorkMat[0][0] = pPart->Scale;
            WorkMat[1][1] = WorkMat[0][0];
            WorkMat[2][2] = WorkMat[0][0];
            sceVu0RotMatrixZ(WorkMat, WorkMat, pPart->RotZ);
            sceVu0MulMatrix(WorkMat, BaseMat, WorkMat);
            sceVu0TransMatrix(LocalWorld, WorkMat, pPart->Position);

            for (j = 0; j < 4; j++)
            {
                sceVu0ApplyMatrix(aWorld[j], LocalWorld, PolyDat[j]);
            }

            RendererPacket3D(aWorld, 4, ColorR, ColorG, ColorB, Alpha,
                             0.0f, 0.0f, tw, th, tw, th, pTx0);
        }

        EndDmaDirectTrans(pbuf + 0x10);
    }

    if (EffWrkStopFlgGet() == 0)
    {
        EffectHazeParticleReqOneFrame(pHazeCtrl);
        GetHazeAllVelocity(AllVelocity, pHazeCtrl);
        HazeAllVelocityCtrl(pHazeCtrl);

        pHazeCtrl->disp = (short)UpdateHazeParticles(&pHazeCtrl->Particles[0],
                                                     AllVelocity,
                                                     pHazeCtrl->Type);
    }

    return pHazeCtrl;                                           /* 4408 */
}

/* 4436 -- spawn this frame's share of particles.
 *
 * Frequency is authored in hundredths, so a value of 20 means one particle
 * every fifth frame: the integer part is spawned outright and the fractional
 * part is a per-frame coin toss.  That is what keeps a thin haze from pulsing.
 *
 * The authored offset is applied only when the volume has a rotation to follow
 * -- a ghost's haze is offset in the ghost's own frame, and the room haze,
 * which has no pRot, gets no offset at all. */
static void EffectHazeParticleReqOneFrame(HAZE_CTRL *pHazeCtrl)
{
    HAZE_PARAMETER *pParam;
    HAZE_PARTICLE  *pHazeParticle;
    float matWork[4][4];
    float OffsetVec[4];
    float Offset[4];
    int   num;
    int   i;

    pParam = EffectHazeGetParameterPtr(pHazeCtrl->Type);        /* 4443 */

    num = (int)((float)pParam->Frequency / 100.0f);             /* 4445 */

    if (EffectGetRandom(0.0f, 100.0f) <=
        (float)pParam->Frequency - (float)num * 100.0f)         /* 4447 */
    {
        num++;
    }

    if (pHazeCtrl->pRot == nullptr)                             /* 4451 */
    {
        Offset[0] = 0.0f;
        Offset[1] = 0.0f;
        Offset[2] = 0.0f;
        Offset[3] = 0.0f;
    }
    else
    {
        /* The offsets are stored biased by 10000, so 10000 is "no offset". */
        OffsetVec[0] = (float)(pParam->OffsetX - 10000);
        OffsetVec[1] = (float)(pParam->OffsetY - 10000);
        OffsetVec[2] = (float)(pParam->OffsetZ - 10000);
        OffsetVec[3] = 1.0f;

        sceVu0UnitMatrix(matWork);
        sceVu0RotMatrixY(matWork, matWork, (*pHazeCtrl->pRot)[1]);
        sceVu0ApplyMatrix(Offset, matWork, OffsetVec);
    }

    for (i = 0; i < num; i++)                                   /* 4468 */
    {
        if (pHazeCtrl->disp >= 64)
        {
            break;
        }

        pHazeParticle = EffectHazeGetParticleBuf(pHazeCtrl);

        if (pHazeParticle != nullptr)
        {
            EffectHazeInitParticle(pHazeParticle, *pHazeCtrl->pPos,
                                   Offset, pHazeCtrl->Type);
            pHazeCtrl->disp++;
        }
    }
}

/* 4485 -- the first dead slot, or null if all 64 are live. */
static HAZE_PARTICLE *EffectHazeGetParticleBuf(HAZE_CTRL *pHazeCtrl)
{
    int i;

    for (i = 0; i < 64; i++)                                    /* 4489 */
    {
        if (pHazeCtrl->Particles[i].Lifetime == 0)              /* 4491 */
        {
            return &pHazeCtrl->Particles[i];                    /* 4493 */
        }
    }

    return nullptr;                                             /* 4496 */
}

/* 4503 -- seed one particle.
 *
 * The spawn point is a *ring*, not a box: a random radius along +Z is rotated
 * by a random angle about Y, which is why AreaRadius alone places it in x/z,
 * and only the height is drawn from the MinY..MaxY pair.
 *
 * Every speed is authored biased by 1000 and in hundredths, so `1000` is
 * stationary and `1100` is one unit per frame.  RotZ is always zero at birth;
 * a RotZTime of zero means the particle never spins, and otherwise the spin
 * direction is a coin toss. */
static void EffectHazeInitParticle(HAZE_PARTICLE *pHazeParticle, float *CenterPos, float *Offset, int HazeType)
{
    HAZE_PARAMETER *pParam;
    float matWork[4][4];
    float vWork[4];
    float RotZMax;
    float RotZMin;

    pParam = EffectHazeGetParameterPtr(HazeType);               /* 4509 */

    vWork[0] = 0.0f;
    vWork[1] = 0.0f;
    vWork[2] = EffectGetRandom(0.0f, (float)pParam->AreaRadius);
    vWork[3] = 1.0f;

    sceVu0UnitMatrix(matWork);
    sceVu0RotMatrixY(matWork, matWork,
                     EffectGetRandom(-3.1415925f, 3.1415925f));
    sceVu0ApplyMatrix(pHazeParticle->Position, matWork, vWork);

    pHazeParticle->Position[1] =
        EffectGetRandom((float)(pParam->MinY - 10000),
                        (float)(pParam->MaxY - 10000));

    sceVu0AddVector(pHazeParticle->Position, pHazeParticle->Position, CenterPos);
    sceVu0AddVector(pHazeParticle->Position, pHazeParticle->Position, Offset);

    pHazeParticle->Velocity[0] =
        EffectGetRandom((float)(pParam->SpeedXMin - 1000) / 100.0f,
                        (float)(pParam->SpeedXMax - 1000) / 100.0f);
    pHazeParticle->Velocity[1] =
        EffectGetRandom((float)(pParam->SpeedYMin - 1000) / 100.0f,
                        (float)(pParam->SpeedYMax - 1000) / 100.0f);
    pHazeParticle->Velocity[2] =
        EffectGetRandom((float)(pParam->SpeedZMin - 1000) / 100.0f,
                        (float)(pParam->SpeedZMax - 1000) / 100.0f);
    pHazeParticle->Velocity[3] = 0.0f;

    pHazeParticle->Lifetime = pParam->AlphaInTime + pParam->AlphaKeepTime +
                              pParam->AlphaOutTime;
    pHazeParticle->Scale    = (float)pParam->StartScale / 100.0f;
    pHazeParticle->Alpha    = 0;
    pHazeParticle->RotZ     = 0.0f;

    RotZMax = (float)pParam->RotZMax * 0.0174532905f;
    RotZMin = (float)pParam->RotZMin * 0.0174532905f;

    if ((float)pParam->RotZTime == 0.0f)
    {
        pHazeParticle->RotZSpeed = 0.0f;
    }
    else
    {
        pHazeParticle->RotZSpeed = EffectGetRandom(RotZMin, RotZMax) /
                                   (float)pParam->RotZTime;

        if (EffectGetRandom(0.0f, 2.0f) < 1.0f)
        {
            pHazeParticle->RotZSpeed = -pHazeParticle->RotZSpeed;
        }
    }
}

/* 4556 -- the whole-volume drift.
 *
 * Three authored velocities with a hold on each and a 60-frame lerp between
 * them, cycling 1 -> 2 -> 3 -> 1.  The even statuses hold, the odd ones
 * interpolate.  All three of Kusabi's and Sae's are the neutral 1000, so only
 * the room haze actually drifts. */
static void GetHazeAllVelocity(float *Velocity, HAZE_CTRL *pHazeCtrl)
{
    HAZE_PARAMETER *pParam;
    float rate;
    float x1, y1, z1, x2, y2, z2, x3, y3, z3;
    float x, y, z;

    pParam = EffectHazeGetParameterPtr(pHazeCtrl->Type);        /* 4563 */

    rate = (float)pHazeCtrl->AllVelocityTime / 60.0f;

    x1 = (float)(pParam->AllSpeedX_1 - 1000) / 100.0f;
    y1 = (float)(pParam->AllSpeedY_1 - 1000) / 100.0f;
    z1 = (float)(pParam->AllSpeedZ_1 - 1000) / 100.0f;
    x2 = (float)(pParam->AllSpeedX_2 - 1000) / 100.0f;
    y2 = (float)(pParam->AllSpeedY_2 - 1000) / 100.0f;
    z2 = (float)(pParam->AllSpeedZ_2 - 1000) / 100.0f;
    x3 = (float)(pParam->AllSpeedX_3 - 1000) / 100.0f;
    y3 = (float)(pParam->AllSpeedY_3 - 1000) / 100.0f;
    z3 = (float)(pParam->AllSpeedZ_3 - 1000) / 100.0f;

    x = 0.0f;
    y = 0.0f;
    z = 0.0f;

    switch (pHazeCtrl->AllVelocityStatus)
    {
    case 0:  x = x1; y = y1; z = z1; break;
    case 1:  x = (x2 - x1) * rate + x1;
             y = (y2 - y1) * rate + y1;
             z = (z2 - z1) * rate + z1; break;
    case 2:  x = x2; y = y2; z = z2; break;
    case 3:  x = (x3 - x2) * rate + x2;
             y = (y3 - y2) * rate + y2;
             z = (z3 - z2) * rate + z2; break;
    case 4:  x = x3; y = y3; z = z3; break;
    case 5:  x = (x1 - x3) * rate + x3;
             y = (y1 - y3) * rate + y3;
             z = (z1 - z3) * rate + z3; break;
    }

    Velocity[0] = x;
    Velocity[1] = y;
    Velocity[2] = z;
    Velocity[3] = 0.0f;
}

/* 4680 -- step the drift's six-stage cycle.  The three hold stages take their
 * duration from the parameter set; the three lerp stages are a fixed 60
 * frames, which is what GetHazeAllVelocity() divides by. */
static void HazeAllVelocityCtrl(HAZE_CTRL *pHazeCtrl)
{
    HAZE_PARAMETER *pParam;

    pParam = EffectHazeGetParameterPtr(pHazeCtrl->Type);        /* 4685 */

    pHazeCtrl->AllVelocityTime++;

    switch (pHazeCtrl->AllVelocityStatus)
    {
    case 0:
        if (pHazeCtrl->AllVelocityTime >= pParam->AllSpeedTime_1)
        {
            pHazeCtrl->AllVelocityTime   = 0;
            pHazeCtrl->AllVelocityStatus = 1;
        }
        break;

    case 1:
        if (pHazeCtrl->AllVelocityTime >= 60)
        {
            pHazeCtrl->AllVelocityTime   = 0;
            pHazeCtrl->AllVelocityStatus = 2;
        }
        break;

    case 2:
        if (pHazeCtrl->AllVelocityTime >= pParam->AllSpeedTime_2)
        {
            pHazeCtrl->AllVelocityTime   = 0;
            pHazeCtrl->AllVelocityStatus = 3;
        }
        break;

    case 3:
        if (pHazeCtrl->AllVelocityTime >= 60)
        {
            pHazeCtrl->AllVelocityTime   = 0;
            pHazeCtrl->AllVelocityStatus = 4;
        }
        break;

    case 4:
        if (pHazeCtrl->AllVelocityTime >= pParam->AllSpeedTime_3)
        {
            pHazeCtrl->AllVelocityTime   = 0;
            pHazeCtrl->AllVelocityStatus = 5;
        }
        break;

    case 5:
        if (pHazeCtrl->AllVelocityTime >= 60)
        {
            pHazeCtrl->AllVelocityStatus = 0;
            pHazeCtrl->AllVelocityTime   = 0;
        }
        break;
    }
}

/* 4734 -- step every particle and report how many are still alive.
 *
 * Alpha follows the authored in / keep / out envelope, scale ramps from
 * StartScale to EndScale over the whole life, and RotZ is wrapped back into
 * [-pi, pi] each frame.  A total life of zero is guarded (the fade would
 * divide by it) and leaves every particle at full alpha. */
static int UpdateHazeParticles(HAZE_PARTICLE *pPtop, float *AllVelocity, int HazeType)
{
    HAZE_PARAMETER *pParam;
    int   i;
    int   num;
    int   InTime;
    int   KeepEnd;
    int   OutTime;
    int   AllTime;
    int   Frame;
    float AlphaRate;
    float ScaleRate;
    float StartScale;

    pParam = EffectHazeGetParameterPtr(HazeType);               /* 4741 */

    InTime     = pParam->AlphaInTime;
    KeepEnd    = InTime + pParam->AlphaKeepTime;
    OutTime    = pParam->AlphaOutTime;
    AllTime    = KeepEnd + OutTime;
    StartScale = (float)pParam->StartScale / 100.0f;

    num = 0;

    if (pPtop != nullptr)
    {
        for (i = 0; i < 64; i++)
        {
            if (AllTime == 0)
            {
                AlphaRate = 0.0f;
                ScaleRate = 1.0f;
            }
            else
            {
                Frame     = AllTime - pPtop[i].Lifetime;
                ScaleRate = 1.0f - (float)pPtop[i].Lifetime / (float)AllTime;

                if (Frame < InTime)
                {
                    AlphaRate = 1.0f;
                    if (InTime != 0)
                    {
                        AlphaRate = (float)Frame / (float)InTime;
                    }
                }
                else if (Frame >= KeepEnd)
                {
                    AlphaRate = 0.0f;
                    if (OutTime != 0)
                    {
                        AlphaRate = 1.0f - (float)(Frame - KeepEnd) /
                                           (float)OutTime;
                    }
                }
                else
                {
                    AlphaRate = 1.0f;
                }
            }

            if (pPtop[i].Lifetime == 0)
            {
                pPtop[i].Alpha = 0;
            }
            else
            {
                pPtop[i].Alpha = (int)((float)pParam->Alpha * AlphaRate);
                pPtop[i].Scale = ((float)pParam->EndScale / 100.0f - StartScale)
                                 * ScaleRate + StartScale;

                sceVu0AddVector(pPtop[i].Position, pPtop[i].Position,
                                pPtop[i].Velocity);
                sceVu0AddVector(pPtop[i].Position, pPtop[i].Position,
                                AllVelocity);

                pPtop[i].RotZ += pPtop[i].RotZSpeed;

                if (pPtop[i].RotZ > 3.1415925f)
                {
                    pPtop[i].RotZ -= 6.28318501f;
                }
                if (pPtop[i].RotZ < -3.1415925f)
                {
                    pPtop[i].RotZ += 6.28318501f;
                }

                num++;
                pPtop[i].Lifetime--;
            }
        }
    }

    return num;                                                 /* 4805 */
}

/* 5053 -- draw the seal itself.
 *
 * Two Get2PosRot() passes, and they do different jobs.  The first rotates a
 * unit +w offset to face the seal from the camera and adds it to BasePos, so
 * the quad is nudged one unit towards the viewer and cannot z-fight the door.
 * The second builds the usual camera-facing billboard about that nudged point.
 * pPosition is an out-parameter: the caller keeps the nudged position.
 *
 * The 280 x 511 size is spelled from the ROM's own truncated words
 * (0x438bffff / 0x43ff7ffe); writing 280.0f / 511.0f would not round-trip. */
static void SubDoorSeal(float (*pPosition)[4], float *BasePos, float AlphaRate)
{
    GRA3DCAMERA *pCam;
    float LocalWorld[4][4];
    float RotX;
    float RotY;
    DRAW_ENV DrawEnv;
    float Offset[4] = { 0.0f, 0.0f, 0.0f, 1.0f };               /* 5081 */

    pCam = gra3dGetCamera();                                    /* 5055 */

    /* TEST 0x5000d: ATST GREATER / 0x0d with ZTST GREATER. */
    DrawEnv.tex1  = 0x161;                                      /* 5058 */
    DrawEnv.alpha = 0x48;
    DrawEnv.zbuf  = 0x10a000118ULL;
    DrawEnv.test  = 0x5000dULL;
    DrawEnv.clamp = 0;
    DrawEnv.prim  = 0x302a400000008004ULL;

    Get2PosRot(gra3dcamGetPosition(), BasePos, &RotX, &RotY);   /* 5056 / 5094 */

    sceVu0UnitMatrix(LocalWorld);                               /* 5097 */
    sceVu0RotMatrixX(LocalWorld, LocalWorld, RotX);             /* 5098 */
    sceVu0RotMatrixY(LocalWorld, LocalWorld, RotY);             /* 5099 */
    sceVu0ApplyMatrix(Offset, LocalWorld, Offset);              /* 5100 */
    sceVu0AddVector(*pPosition, Offset, BasePos);               /* 5101 */

    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &RotX, &RotY); /* 5103 / 5104 */

    sceVu0UnitMatrix(LocalWorld);                               /* 5105 */
    sceVu0RotMatrixX(LocalWorld, LocalWorld, RotX);             /* 5106 */
    sceVu0RotMatrixY(LocalWorld, LocalWorld, RotY);             /* 5107 */
    sceVu0TransMatrix(LocalWorld, LocalWorld, *pPosition);

    Set3DPosTexure(LocalWorld, &DrawEnv, 0x48,
                   279.999969f, 510.999939f,
                   100, 95, 103, (u_char)(int)(AlphaRate * 76.0f)); /* 5109 */
}

/* 5327 -- park the camera where the dissolve is authored: 500 units back along
 * -Z looking at the origin, no roll, 60 degrees of field.  The seal's own
 * texture position is the matching fixed point twelve units up. */
static void DoorSealDisappearCameraSet(void)
{
    static float DoorSealCameraPos[4] = { 0.0f, 0.0f, -500.0f, 1.0f }; /* rdata 3a7100 */
    static float DoorSealCameraTgt[4] = { 0.0f, 0.0f,    0.0f, 1.0f }; /* rdata 3a7110 */

    gra3dcamSetPosition(DoorSealCameraPos);                     /* 5331 */
    gra3dcamSetTarget(DoorSealCameraTgt, 1);                    /* 5332 */
    gra3dcamSetRoll(0.0f);                                      /* 5333 */
    gra3dcamSetFov(1.04719746f);                                /* 5334 */

    gra3dApplyCamera((GRA3DCAMERA *)nullptr, 0);                /* 5335 */
}

/* 5344 -- fill in the parts-deform record that dissolves the seal.
 *
 * Effect id 0x18 with sub-type 0x18 is the deform kernel; dat.fl32[2] and [3]
 * are its two wave terms.  The speed and wave rate are written through the
 * caller's own floats and the pointers to them are stored in the record, so the
 * dissolve can be retuned per frame without rebuilding it. */
static void DoorSealDisappearPDeformParamSet(EFFECT_CONT *ec, float (*pPos)[4], int AlphaRate, float *pSpeed, float *pWaveRate)
{
    ec->dat.uc8[0]  = 0x18;                                     /* 5364 */
    ec->dat.uc8[1]  = 1;                                        /* 5365 */
    ec->dat.uc8[2]  = 0x18;                                     /* 5366 */
    ec->max         = AlphaRate;                                /* 5367 */
    ec->dat.uc8[4]  = 0xff;                                     /* 5368 */
    ec->dat.fl32[2] = 0.98999995f;                              /* 5369 */
    ec->dat.fl32[3] = 0.809999943f;                             /* 5370 */
    ec->pnt[0]      = pPos;                                     /* 5371 */

    ec->fw[0] = 0.0f;
    ec->fw[1] = 1.0f;

    ec->cnt  = 0;                                               /* 5375 */
    ec->in   = 0;                                               /* 5376 */
    ec->keep = 0;                                               /* 5377 */
    ec->out  = 0;                                               /* 5378 */

    ec->pnt[1] = nullptr;                                       /* 5380 */

    *pSpeed    = 2.91999984f;                                   /* 5381 */
    ec->pnt[2] = pSpeed;                                        /* 5382 */

    *pWaveRate = 2.0f;                                          /* 5383 */
    ec->pnt[4] = pWaveRate;                                     /* 5384 */
    ec->pnt[5] = nullptr;                                       /* 5385 */

    ec->r = 0x96;                                               /* 5386 */
    ec->g = 0x96;                                               /* 5387 */
    ec->b = 0x97;                                               /* 5388 */

    ec->dat.uc8[5] = 1;                                         /* 5389 */
}

/* 5396 -- the blur pass over the same dissolve.  Sub-type 0x27 rather than
 * 0x18, a flat grey, and none of the three float pointers -- the blur takes its
 * wave terms as constants. */
static void DoorSealDisappearPDeformBlurParamSet(EFFECT_CONT *ec, float (*pPos)[4], int AlphaRate)
{
    ec->dat.uc8[0]  = 0x18;                                     /* 5406 */
    ec->dat.uc8[1]  = 1;                                        /* 5407 */
    ec->dat.uc8[2]  = 0x27;                                     /* 5408 */
    ec->max         = AlphaRate;
    ec->dat.uc8[4]  = 0xff;                                     /* 5410 */
    ec->dat.fl32[2] = 1.38f;                                    /* 5403 */
    ec->dat.fl32[3] = 0.75f;                                    /* 5404 */
    ec->pnt[0]      = pPos;

    ec->fw[0] = 0.0f;
    ec->fw[1] = 1.0f;

    ec->cnt  = 0;
    ec->in   = 0;
    ec->keep = 0;
    ec->out  = 0;

    ec->pnt[1] = nullptr;
    ec->pnt[2] = nullptr;
    ec->pnt[4] = nullptr;
    ec->pnt[5] = nullptr;

    ec->r = 0x80;
    ec->g = 0x80;
    ec->b = 0x80;

    ec->dat.uc8[5] = 1;
}


/* 1018 -- ask a flame to flare up.  fw[1] and fw[2] are the two colour scales
 * and both jump to 25; flow 1 is the flare-in leg. */
void EffOthCandleFlameFlareUpReq(EFFECT_CONT *pEffect)
{
    if (pEffect != nullptr)                                     /* 1019 */
    {
        pEffect->fw[1] = 25.0f;
        pEffect->fw[2] = 25.0f;

        pEffect->flow = 1;                                      /* 1022 */
        pEffect->in   = 0;                                      /* 1023 */
        pEffect->cnt  = 0;                                      /* 1024 */
    }
}

/* 890 -- the soft flare that sits at the head of a flame.
 *
 * One camera-facing quad, 8.5 units across before `Scale`, textured with
 * effdat[0x4c] (or its monochrome twin) and drawn as a two-triangle strip.  The
 * billboard is built by rotating a flat quad to face the camera rather than by
 * transposing the view matrix, which is why it needs Get2PosRot(). */
void DrawFrea(float *Position, float Scale, int Depth, int R, int G, int B, int Alpha)
{
    float LocalWorld[4][4];
    float TmpMatrix[4][4];
    float LocalScreen[4][4];
    float ScaleMatrix[4][4];
    sceVu0IVECTOR ivec[4];

    /* .rodata 0x3a6c60 / 0x3a6ca0, copied into the frame -- local array
       initialisers, not file-scope tables, which is why globals.txt has
       neither.  The ST inset of 0.002982 is half a texel at 168 pixels. */
    float frea_pos[4][4] =                                      /* 895 */
    {
        { -4.26278353f, -4.26278353f, 0.0f, 1.0f },
        { -4.26278353f,  4.26278353f, 0.0f, 1.0f },
        {  4.26278353f, -4.26278353f, 0.0f, 1.0f },
        {  4.26278353f,  4.26278353f, 0.0f, 1.0f },
    };

    float st[4][2] =                                            /* 900 */
    {
        { 0.00298199989f, 0.99701798f },
        { 0.00298199989f, 0.00298199989f },
        { 0.99701798f,    0.99701798f    },
        { 0.99701798f,    0.00298199989f },
    };

    float rot_x;
    float rot_y;
    Q_WORDDATA *pbuf;
    int ndpkt;
    int i;
    int ClipFlg;
    GRA3DCAMERA *pCam;
    U32DATA s, t, q;

    pCam = gra3dGetCamera();                                    /* 911 */

    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget,
               &rot_x, &rot_y);                                 /* 912 / 914 */

    sceVu0UnitMatrix(LocalWorld);                               /* 917 */
    sceVu0RotMatrixX(LocalWorld, LocalWorld, rot_x);            /* 918 */
    sceVu0RotMatrixY(LocalWorld, LocalWorld, rot_y);            /* 919 */
    sceVu0TransMatrix(LocalWorld, LocalWorld, Position);        /* 920 */

    sceVu0MulMatrix(TmpMatrix, pCam->matWorldScreen, LocalWorld); /* 921 */

    sceVu0UnitMatrix(ScaleMatrix);                              /* 922 */
    ScaleMatrix[0][0] = Scale * 25.0f;                          /* 923 */
    ScaleMatrix[1][1] = ScaleMatrix[0][0];
    ScaleMatrix[2][2] = ScaleMatrix[0][0];

    sceVu0MulMatrix(LocalScreen, TmpMatrix, ScaleMatrix);       /* 924 */

    for (i = 0; i < 4; i++)                                     /* 927 */
    {
        sceVu0RotTransPers(ivec[i], LocalScreen, frea_pos[i], 1); /* 928 */
    }

    /* Host path.  The DIRECT packet below is inert in this port, so the same
       quad is queued through the renderer bridge -- before the guard-band test,
       which only ever gated the GS packet. */
    {
        const sceGsTex0 *pTx0 =
            (const sceGsTex0 *)&effdat[EffWrkMonochroModeGet() + 0x4c];
        float tw = (float)(1 << pTx0->TW);
        float th = (float)(1 << pTx0->TH);
        float WorldMatrix[4][4];
        float aWorld[4][4];

        sceVu0MulMatrix(WorldMatrix, LocalWorld, ScaleMatrix);

        for (i = 0; i < 4; i++)
        {
            sceVu0ApplyMatrix(aWorld[i], WorldMatrix, frea_pos[i]);
        }

        /* st[] is already page-normalised here (the ROM multiplies it by Q and
           nothing else), so the texel rectangle is the whole page inset by the
           half-texel guard. */
        RendererPacket3D(aWorld, 4, R, G, B, Alpha,
                         st[0][0] * tw, st[1][1] * th,
                         (st[2][0] - st[0][0]) * tw,
                         (st[0][1] - st[1][1]) * th,
                         tw, th, pTx0);
    }

    ClipFlg = 0;                                                /* 932 */

    for (i = 0; i < 4; i++)                                     /* 933 */
    {
        if ((u_int)ivec[i][0] < 0x4000 || (u_int)ivec[i][0] > 0xc000)   /* 934 */
        {
            ClipFlg = 1;
        }
        if ((u_int)ivec[i][1] < 0x4000 || (u_int)ivec[i][1] > 0xc000)   /* 935 */
        {
            ClipFlg = 1;
        }
        if ((u_int)ivec[i][2] < 0xff || (u_int)ivec[i][2] > 0xfffffff)  /* 936 */
        {
            ClipFlg = 1;
        }
    }                                                           /* 937 */

    if (ClipFlg == 0)                                           /* 939 */
    {
        pbuf = StartDmaDirectTrans();                           /* 941 */
        Reserve2DPacket(0x10);                                  /* 942 */

        /* A+D tag: TEXFLUSH, then this quad's TEX0. */
        pbuf[0].ul64[0] = 0x1000000000008002ULL;                /* 944 */
        pbuf[0].ul64[1] = 0x0e;                                 /* 945 */
        pbuf[1].ul64[0] = 0;                                    /* 947 */
        pbuf[1].ul64[1] = 0x3f;                                 /* 948 */
        pbuf[2].ul64[0] = effdat[EffWrkMonochroModeGet() + 0x4c].tex0;  /* 950 */
        pbuf[2].ul64[1] = 0x06;                                 /* 951 */

        /* PRIM 0x5c: TRIANGLE_STRIP, gouraud, textured, alpha blended. */
        pbuf[3].ul64[0] = 0x302e400000008004ULL;                /* 953 */
        pbuf[3].ul64[1] = 0x412;                                /* 954 */

        ndpkt = 4;
        for (i = 0; i < 4; i++)                                 /* 956 */
        {
            /* Perspective-correct STQ: Q is 1/w and S/T are premultiplied. */
            q.fl32 = 1.0f / (float)ivec[i][3];                  /* 959 */
            s.fl32 = q.fl32 * st[i][0];                         /* 960 */
            t.fl32 = q.fl32 * st[i][1];                         /* 961 */

            pbuf[ndpkt].ui32[0] = (u_int)s.ui32;                /* 962 */
            pbuf[ndpkt].ui32[1] = (u_int)t.ui32;                /* 963 */
            pbuf[ndpkt].ui32[2] = (u_int)q.ui32;                /* 964 */
            pbuf[ndpkt].ui32[3] = 0;                            /* 965 */

            pbuf[ndpkt + 1].iv[0] = R;                          /* 967 */
            pbuf[ndpkt + 1].iv[1] = G;                          /* 968 */
            pbuf[ndpkt + 1].iv[2] = B;                          /* 969 */
            pbuf[ndpkt + 1].iv[3] = Alpha;                      /* 970 */

            pbuf[ndpkt + 2].iv[0] = ivec[i][0];                 /* 972 */
            pbuf[ndpkt + 2].iv[1] = ivec[i][1];                 /* 973 */
            pbuf[ndpkt + 2].iv[2] = Depth;                      /* 974 */
            /* ADC: the first two vertices only prime the strip. */
            pbuf[ndpkt + 2].ui32[3] = (i < 2) ? 0x8000 : 0;     /* 975 */

            ndpkt += 3;
        }                                                       /* 976 */

        EndDmaDirectTrans(pbuf + ndpkt);                        /* 979 */
    }
}

/* 1920 -- the shared particle billboard, and the busiest drawing routine in
 * the effect layer: every torch, every heat haze and every ghost aura goes
 * through it.  Returns how many particles were actually submitted, which is
 * what feeds a flame's own brightness.
 *
 * Each live particle becomes a six-vertex TRIANGLE_FAN -- centre plus four
 * corners plus a repeat of the first -- with the centre at the particle's
 * colour and every corner at alpha 0.  That radial falloff is the whole trick:
 * it is what turns a square texture into a soft blob, and it is why the fan
 * (rather than a strip) is worth the extra vertex.
 *
 * PORT DEVIATION, and the largest one in this file.  The ROM computes all of
 * this in VU0 macro mode -- roughly eighty instructions of inline COP2 with the
 * two matrices held in vf4..vf7 / vf28..vf31 and the four constant vectors in
 * vf19 / vf24 / vf25 / vf26.  There is no VU0 here, so the same arithmetic is
 * written out scalar.  Two blocks of that VU0 program are dead in the ROM and
 * are reproduced only as comments, since writing them would compute values
 * nothing reads:
 *
 *   - a screen-space bounding-box clamp (vf13/vf14 against `ones` and
 *     `screen_size`) whose results are overwritten before use.  `screen_size`
 *     and `st_add` exist only for it.
 *   - a 1/64-scaled copy of the two half-extent vectors (vf8/vf14 via
 *     `st_scale`), likewise overwritten.  Note vf27 is read by the first block
 *     before anything writes it, so on the very first particle it holds
 *     whatever the caller left there -- more evidence both blocks are leftovers
 *     rather than live code. */
int draw_distortion_particles2(float (*matLocalScreen)[4],
                               float (*matLocalClip)[4],
                               int t_particles, PARTICLE *pPartTop, float psize,
                               u_long tex0, u_long alpha)
{
    /* .rodata 0x3a6d80 / 0x3a6d90 / 0x3a6de0 / 0x3a6df0 / 0x3a6e00, all copied
       into the frame -- local initialisers, which is why globals.txt lists
       none of them. */
    float screen_size[4] = { 639.0f, 447.0f, 0.0f, 0.0f };      /* 1921 */
    float particle_size[4] = { 0.0f, 0.0f, 0.0f, 0.0f };        /* 1922 */

    /* Four corners inset by 1% of the page, then the centre. */
    float stq[5][4] =                                           /* 1923 */
    {
        { 0.00999999978f, 0.00999999978f, 1.0f, 0.0f },
        { 0.98999995f,    0.00999999978f, 1.0f, 0.0f },
        { 0.98999995f,    0.98999995f,    1.0f, 0.0f },
        { 0.00999999978f, 0.98999995f,    1.0f, 0.0f },
        { 0.5f,           0.5f,           1.0f, 0.0f },
    };

    float ones[4]     = { 1.0f, 1.0f, 1.0f, 0.0f };             /* 1930 */
    float st_add[4]   = { -1728.0f, -1824.0f, 0.0f, 0.0f };     /* 1931 */
    float st_scale[4] = { 0.015625f, 0.015625f, 0.0f, 0.0f };   /* 1932 */

    float FogDivVec[4] = { 0.0f, 0.0f, 0.0f, 0.015625f };       /* 1937 */
    float FogSubVec[4] = { 0.0f, 0.0f, 0.0f, 255.0f };          /* 1938 */

    Q_WORDDATA *pbuf;
    int   ndpkt;
    int   i;
    int   num;
    float rr, gg, bb;
    u_int clip_flags;
    sceVu0IVECTOR *pVec1;
    sceVu0IVECTOR *pVec2;
    int   ClipFlg;

    (void)screen_size; (void)ones; (void)st_add; (void)st_scale;

    Reserve2DPacket(0x10);                                      /* 1940 */
    pbuf = StartDmaDirectTrans();                               /* 1942 */

    /* A+D tag for three register writes: TEXFLUSH, TEX0, ALPHA. */
    pbuf[0].ul64[0] = 0x1000000000008003ULL;                    /* 1944 */
    pbuf[0].ul64[1] = 0x0e;                                     /* 1945 */
    pbuf[1].ul64[0] = 0;                                        /* 1946 */
    pbuf[1].ul64[1] = 0x3f;                                     /* 1947 */
    pbuf[2].ul64[0] = tex0;                                     /* 1948 */
    pbuf[2].ul64[1] = 0x06;                                     /* 1949 */
    pbuf[3].ul64[0] = alpha;                                    /* 1950 */
    pbuf[3].ul64[1] = 0x42;                                     /* 1951 */

    ndpkt = 4;
    num   = 0;

    /* 1965 / 1977 -- the ROM parks both matrices and the four constant vectors
       in VU0 registers here, outside the loop. */

    for (i = 0; i < t_particles; i++)                           /* 1980 */
    {
        PARTICLE *p = &pPartTop[i];
        float clip[4];
        float screen[4];
        float q;
        float fog;
        float halfw, halfh;

        if (!(p->color[3] > 0.0f))                              /* 1981 */
        {
            continue;
        }

        num++;                                                  /* 1986 */

        rr = p->color[0];                                       /* 1988 */
        gg = p->color[1];                                       /* 1989 */
        bb = p->color[2];                                       /* 1990 */

        if (EffWrkMonochroModeGet() != 0)                       /* 1991 */
        {
            /* Written back into the particle for the duration of the draw and
               restored at the bottom, rather than kept in a temporary. */
            p->color[0] = p->color[1] = p->color[2] =
                (rr + gg + bb) / 3.0f;                          /* 1992 */
        }

        particle_size[0] = psize * p->Scale;                    /* 1994 */
        particle_size[1] = particle_size[0];                    /* 1995 */

        /* 2014 -- clip-space transform, then vclipw.xyz: the six flags say
           whether |x|, |y| or |z| exceeded w. */
        sceVu0ApplyMatrix(clip, matLocalClip, p->position);     /* 2014 */

        clip_flags = 0;
        if (clip[0] >  clip[3]) { clip_flags |= 0x01; }
        if (clip[0] < -clip[3]) { clip_flags |= 0x02; }
        if (clip[1] >  clip[3]) { clip_flags |= 0x04; }
        if (clip[1] < -clip[3]) { clip_flags |= 0x08; }
        if (clip[2] >  clip[3]) { clip_flags |= 0x10; }
        if (clip[2] < -clip[3]) { clip_flags |= 0x20; }

        if ((clip_flags & 0x3f) != 0)                           /* 2016 */
        {
            p->color[0] = rr;
            p->color[1] = gg;
            p->color[2] = bb;
            continue;
        }

        /* 2050 -- screen transform, reciprocal, fog and the two half-extent
           vectors.  The fog term is 255 - w/64 clamped at zero, and ftoi4
           lands its eight bits exactly where XYZF2 wants them. */
        sceVu0ApplyMatrix(screen, matLocalScreen, p->position); /* 2050 */

        q = 1.0f / screen[3];

        fog = FogSubVec[3] - screen[3] * FogDivVec[3];
        if (fog < 0.0f)
        {
            fog = 0.0f;
        }

        screen[0] *= q;
        screen[1] *= q;
        screen[2] *= q;
        screen[3] *= q;

        halfw = particle_size[0] * q;
        halfh = particle_size[1] * q;

        /* The two dead VU0 blocks sat here:
             vf14 = screen + st_add;  vf13 = max(vf14 - size - ones, 0);
             vf14 = min(vf27 + size + ones, screen_size);
             vf8  = size * st_scale;  (and its y/x-zeroed pair)
           Every one of those registers is overwritten below. */

        /* The fan's corner order: bottom-left, bottom-right, top-right,
           top-left, then bottom-left again to close it.  cs[] picks the
           matching entry of stq[]. */
        static const int cx[5] = { -1,  1,  1, -1, -1 };
        static const int cy[5] = { -1, -1,  1,  1, -1 };
        static const int cs[5] = {  0,  1,  2,  3,  0 };

        pbuf[ndpkt].ul64[0] = 0xe03ec00000008001ULL;            /* 2052 */
        pbuf[ndpkt].ul64[1] = 0x0042424242421421ULL;            /* 2064 */

        /* 2145 -- the fan.  Centre first at full alpha, then the four corners
           and a repeat of the first, all at alpha 0. */
        {
            Q_WORDDATA *pv = &pbuf[ndpkt + 1];
            int k;

            pv[0].iv[0] = (int)p->color[0];
            pv[0].iv[1] = (int)p->color[1];
            pv[0].iv[2] = (int)p->color[2];
            pv[0].iv[3] = (int)p->color[3];

            pv[1].fl32[0] = stq[4][0];
            pv[1].fl32[1] = stq[4][1];
            pv[1].fl32[2] = stq[4][2];
            pv[1].fl32[3] = stq[4][3];

            pv[2].iv[0] = (int)(screen[0] * 16.0f);
            pv[2].iv[1] = (int)(screen[1] * 16.0f);
            pv[2].iv[2] = (int)(screen[2] * 16.0f);
            pv[2].iv[3] = (int)(fog * 16.0f);

            /* Same RGB, alpha forced to zero -- `ones` has 0.0f in w and the
               ROM moves that field straight over the converted colour. */
            pv[3].iv[0] = (int)p->color[0];
            pv[3].iv[1] = (int)p->color[1];
            pv[3].iv[2] = (int)p->color[2];
            pv[3].iv[3] = 0;

            for (k = 0; k < 5; k++)
            {
                Q_WORDDATA *pst  = &pv[4 + k * 2];
                Q_WORDDATA *pxyz = &pv[5 + k * 2];

                pst->fl32[0] = stq[cs[k]][0];
                pst->fl32[1] = stq[cs[k]][1];
                pst->fl32[2] = stq[cs[k]][2];
                pst->fl32[3] = stq[cs[k]][3];

                pxyz->iv[0] = (int)((screen[0] + (float)cx[k] * halfw) * 16.0f);
                pxyz->iv[1] = (int)((screen[1] + (float)cy[k] * halfh) * 16.0f);
                pxyz->iv[2] = (int)(screen[2] * 16.0f);
                pxyz->iv[3] = (int)(fog * 16.0f);
            }
        }

        /* Host path.  The DIRECT packet above is inert in this port, so the
           same quad is queued through the screen-space bridge.  The bridge
           takes one flat colour, so the centre-to-corner alpha gradient is lost
           -- the particle textures carry their own radial falloff, which is
           what keeps the result close.

           Depth comes through with it.  The GS sprite above carries the
           particle's Z in every corner's XYZF2 and every caller draws under
           ZTE=1 / ZTST=GEQUAL with ZMSK set (`test` 0x5000d, `zbuf`
           0x10a000118), i.e. tested against the room but writing nothing.
           matLocalClip is the engine's own world->clip, so clip[2] / clip[3] is
           the depth in the engine's symmetric convention -- near -1, far +1 --
           and no second projection is needed here.  Without it the flames drew
           over whatever stood in front of them.

           Note this is NOT the host's convention any more: the renderer runs
           reversed-Z, and MioPan_Graph3dApplyCamera() rewrites the depth row of
           the projection it hands the GPU rather than touching matLocalClip,
           which still feeds gra3dVu0ClipFlags() and its +-w test.  So the
           bridge takes clip[3], the view depth, as well, and builds the depth
           from that with the meshes' own reversed row; ndc_z only stands in
           when no perspective camera is installed.  Reversing ndc_z itself on
           the GPU (0.5 - 0.5 * ndc) cancels it down to a few digits -- tens
           of units of depth at outdoor distances, which flipped flames in front
           of and behind their torch every frame.  Keep passing the engine
           values; converting either here would double up. */
        {
            u_long tex1 = 0x161;
            const sceGsTex0 *pTx0 = (const sceGsTex0 *)&tex0;
            float tw = (float)(1 << pTx0->TW);
            float th = (float)(1 << pTx0->TH);
            float xy[8];
            float uv[8];
            /* Bridge corner order is TL, TR, BL, BR. */
            static const int ord[4] = { 0, 1, 3, 2 };
            /* clip[3] is the view-space Z (mat[2][3] is 1 and mat[3][3] 0),
               and the clip test above has already rejected everything outside
               the frustum, so it is positive here. */
            float ndc_z = clip[2] / clip[3];
            int k;

            for (k = 0; k < 4; k++)
            {
                int c = ord[k];

                xy[k * 2 + 0] = screen[0] + (float)cx[c] * halfw
                              - 2048.0f + 320.0f;
                xy[k * 2 + 1] = screen[1] + (float)cy[c] * halfh
                              - 2048.0f + 224.0f;
                uv[k * 2 + 0] = stq[c][0] * tw;
                uv[k * 2 + 1] = stq[c][1] * th;
            }

            MioPan_RendererDrawTexturedQuadDepth(
                pTx0, (const sceGsTex1 *)&tex1, xy, uv, ndc_z, clip[3],
                (u_char)(int)p->color[0],
                (u_char)(int)p->color[1],
                (u_char)(int)p->color[2],
                (u_char)(int)p->color[3]);
        }

        /* 2149 -- the guard band, read back out of the packet: corner 1 is the
           top-left vertex and corner 3 the bottom-right, so the two together
           bound the quad. */
        pVec1 = &pbuf[ndpkt + 6].iv;                            /* 2149 */
        pVec2 = &pbuf[ndpkt + 10].iv;

        ClipFlg = 0;                                            /* 2152 */

        if ((*pVec1)[0] < 0x4000 || (*pVec2)[0] > 0xc000)       /* 2154 */
        {
            ClipFlg = 1;
        }
        if ((*pVec1)[1] < 0x4000 || (*pVec2)[1] > 0xc000)       /* 2155 */
        {
            ClipFlg = 1;
        }
        if ((u_int)((*pVec1)[2] - 0xff) > 0xfffff00)            /* 2156 */
        {
            ClipFlg = 1;
        }

        if (ClipFlg == 0)                                       /* 2158 */
        {
            ndpkt += 15;
        }

        p->color[0] = rr;                                       /* 2165 */
        p->color[1] = gg;                                       /* 2166 */
        p->color[2] = bb;                                       /* 2167 */
    }                                                           /* 2169 */

    EndDmaDirectTrans(pbuf + ndpkt);                            /* 2171 */

    return num;                                                 /* 2173 */
}

/* ---- STUB: needed by mission_ctl.c ---- */

/* ---- STUB: needed by effect.c; bodies await reconstruction ---- */

#include "effect.h"

/* 4059 -- step every live drift.  Called once a frame from effect.c. */
void RunLeaf(void)
{
    int i;

    for (i = 0; i < 6; i++)                                     /* 4061 */
    {
        if (eff_leaf[i].flag != 0)                              /* 4063 */
        {
            RunLeafSub(&eff_leaf[i]);                           /* 4064 */
        }
    }
}
/* 992 -- the player walking past makes a nearby flame gutter.
 *
 * The distance test is squared against 500000, i.e. a radius of about 707
 * units, and flow 4 is the gutter animation.  A flame already guttering
 * (flow 4) or flaring (flow 5) is left alone. */
void EffOthCandleFlameYuramekiReq(EFFECT_CONT *pEffCont, float *PlayerPos)
{
    float CandlePos[4];
    float TmpVector[4];

    if (pEffCont->dat.uc8[0] == 0x15 &&
        pEffCont->flow != 4 && pEffCont->flow != 5)             /* 997 */
    {
        g3dxVu0CopyVector(CandlePos, (float *)pEffCont->pnt[0]); /* g3dxVu0.h 134 */

        sceVu0SubVector(TmpVector, CandlePos, PlayerPos);       /* 1002 */
        sceVu0MulVector(TmpVector, TmpVector, TmpVector);       /* 1003 */

        if (TmpVector[0] + TmpVector[1] + TmpVector[2] <= 500000.0f) /* 1004 / 1006 */
        {
            pEffCont->flow = 4;                                 /* 1007 */
            pEffCont->cnt  = 0;                                 /* 1008 */
            pEffCont->keep = 0;                                 /* 1009 */
        }
    }
}
/* 5039 -- the door seal's per-frame entry.  fw[0..2] is the seal's world
 * position (there is no fw[3]; the w is supplied here), and dat.uc8[1] bit 0
 * is the one-shot flag. */
void SetDoorSeal(EFFECT_CONT *ec)
{
    float BasePos[4];

    BasePos[0] = ec->fw[0];
    BasePos[1] = ec->fw[1];
    BasePos[2] = ec->fw[2];
    BasePos[3] = 1.0f;                                          /* 5043 */

    SubDoorSeal((float (*)[4])ec->pnt[0], BasePos, ec->dat.fl32[1]);    /* 5044 */

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 5047 */
    {
        ResetEffects(ec);                                       /* 5048 */
    }
}
/* 1460 -- the halo effect's per-frame entry.  dat.uc8[1] bit 0 is the
 * one-shot flag: the record tears itself down after a single draw. */
void SetHalo(EFFECT_CONT *ec)
{
    SubHalo((float*)ec->pnt[0], ec->dat.uc8[2], ec->dat.uc8[6], ec->z,
            ec->dat.uc8[3], ec->dat.uc8[4], ec->dat.uc8[5],
            100, ec->dat.fl32[2]);                              /* 1461 */

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 1463 */
    {
        ResetEffects(ec);                                       /* 1464 */
    }
}
/* 1030 -- the flame's per-frame entry.  fw[0] is the master scale and is
 * re-driven to 1.0 every frame; only fw[1] and fw[2] carry state. */
void SetFire(EFFECT_CONT *ec)
{
    ec->fw[0] = 1.0f;                                           /* 1031 */
    SubCandleFlame(ec);                                         /* 1032 */
}
/* 3413 -- the torch's per-frame entry.
 *
 * dat.uc8[2] is the flame type and 3 is special: while dat.uc8[3] is non-zero
 * it is decremented and the type is bumped to 4, so a torch can be told to
 * flare for N frames and then settle back.  Type 4 is the flare -- brighter,
 * bigger (4.5 against 3.3) and with a different pool start mode.
 *
 * The default arm leaves g and b at their pre-switch values and copies g into
 * r, which gives an unauthored type a dull blue rather than black. */
void SetTorch(EFFECT_CONT *ec)
{
    u_int tp2;
    int   st;
    float r, g, b;
    float ar, sr;
    float pos[4];

    st = 1;
    g  = 6.0f;
    b  = 32.0f;

    if (ec->dat.uc8[2] == 3)                                    /* 3421 */
    {
        if (ec->dat.uc8[3] == 0)
        {
            tp2 = 3;
        }
        else
        {
            tp2 = 4;
            ec->dat.uc8[3]--;
        }
    }
    else
    {
        tp2 = ec->dat.uc8[2];
    }

    sr = (ec->pnt[2] != nullptr) ? *(float *)ec->pnt[2] : 1.0f;
    ar = (ec->pnt[3] != nullptr) ? *(float *)ec->pnt[3] : 1.0f;

    switch (tp2)                                                /* 3441 */
    {
    case 0:                     /* the ordinary torch  */
        r = 80.0f; g = 48.0f; b = 16.0f; st = 1;
        break;

    case 1:                     /* a cold blue flame   */
        r = 32.0f; g = 32.0f; b = 80.0f; st = 1;
        break;

    case 2:                     /* the darkest blue    */
        r =  6.0f; g =  6.0f; b = 60.0f; st = 1;
        break;

    case 3:
        r = 80.0f; g = 48.0f; b = 16.0f; st = 1;
        sr = 3.29999995f;
        ar = 0.269999981f;
        break;

    case 4:                     /* the flare           */
        r = 80.0f; g = 48.0f; b = 16.0f; st = 3;
        sr = 4.5f;
        ar = 0.5f;
        break;

    default:
        r = g;
        break;
    }

    g3dxVu0CopyVector(pos, (float *)ec->pnt[0]);

    ec->pnt[1] = GetTorchPartAddr(ec->pnt[1], 5, (int)(sr * 50.0f));
    ec->pnt[1] = ContTorch(ec->pnt[1], 5, pos, nullptr, st,
                           r, g, b, 128.0f, 1300.0f, 1.0f,
                           (int)tp2, ar, sr);                   /* 3489 */

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 3491 */
    {
        ResetEffects(ec);
    }
}
/* 3702 -- the dust cloud's per-frame entry.  The three-to-seven grain burst is
 * added once, on the frame the block is claimed; every frame after that only
 * draws and steps. */
void SetDust(EFFECT_CONT *ec)
{
    CLOUD_OF_DUST_CTRL *pCod;
    int num;
    int i;

    num = (int)EffectGetRandom(3.0f, 7.0f);                     /* 3707 */

    pCod = (CLOUD_OF_DUST_CTRL *)ec->pnt[1];

    if (pCod == nullptr)                                        /* 3709 */
    {
        pCod = (CLOUD_OF_DUST_CTRL *)EFFECT_MALLOC(sizeof(CLOUD_OF_DUST_CTRL));
        ec->pnt[1] = pCod;

        if (pCod == nullptr)
        {
            ResetEffects(ec);
        }
        else
        {
            CloudOfDustCtrlInit(pCod, (float *)ec->pnt[0]);

            for (i = 0; i < num; i++)
            {
                AddCloudOfDustRunParticle((CLOUD_OF_DUST_CTRL *)ec->pnt[1]);
            }
        }

        pCod = (CLOUD_OF_DUST_CTRL *)ec->pnt[1];
    }

    ec->pnt[1] = CloudOfDustCtrl(pCod, 5000.0f);                /* 3724 */

    if (ec->pnt[1] == nullptr)                                  /* 3726 */
    {
        ResetEffects(ec);
    }
}
/* 4411 -- the haze volume's per-frame entry.  The work block is claimed lazily
 * on the first frame, and a failed allocation tears the effect down rather than
 * running with none.  haze_stop suppresses every volume at once. */
void SetHaze(EFFECT_CONT *ec)
{
    HAZE_CTRL *pHazeCtrl;

    if (haze_stop == 0)                                         /* 4413 */
    {
        pHazeCtrl = (HAZE_CTRL *)ec->pnt[1];

        if (pHazeCtrl == nullptr)                               /* 4415 */
        {
            pHazeCtrl = (HAZE_CTRL *)EFFECT_MALLOC(sizeof(HAZE_CTRL));
            ec->pnt[1] = pHazeCtrl;

            if (pHazeCtrl == nullptr)
            {
                ResetEffects(ec);
            }
            else
            {
                HazeCtrlInit(pHazeCtrl, (float (*)[4])ec->pnt[0],
                             (float (*)[4])ec->pnt[2],
                             ec->dat.iv[1], ec->dat.iv[2],
                             (float *)ec->pnt[3]);
            }

            pHazeCtrl = (HAZE_CTRL *)ec->pnt[1];
        }

        ec->pnt[1] = HazeCtrl(pHazeCtrl, 5000.0f);              /* 4426 */

        if (ec->pnt[1] == nullptr)                              /* 4428 */
        {
            ResetEffects(ec);
        }
    }
}
/* 1161 -- the massed candles' per-frame entry.  Same lazy-claim shape as the
 * haze and the dust: the work block is allocated on the first frame and a
 * failure tears the effect down. */
void SetManyCandle(EFFECT_CONT *ec)
{
    MANY_CANDLE_CTRL *pMc;

    pMc = (MANY_CANDLE_CTRL *)ec->pnt[1];

    if (pMc == nullptr)                                         /* 1165 */
    {
        pMc = (MANY_CANDLE_CTRL *)EFFECT_MALLOC(sizeof(MANY_CANDLE_CTRL));
        ec->pnt[1] = pMc;

        if (pMc == nullptr)
        {
            ResetEffects(ec);
        }
        else
        {
            ManyCandleCtrlInit(pMc, (float (*)[4])ec->pnt[0],
                               (float (*)[4])ec->pnt[2],
                               ec->dat.iv[1], ec->dat.iv[2]);
        }

        pMc = (MANY_CANDLE_CTRL *)ec->pnt[1];
    }

    ec->pnt[1] = ManyCandleCtrl(pMc);                           /* 1173 */

    if (ec->pnt[1] == nullptr)                                  /* 1175 */
    {
        ResetEffects(ec);
    }
}
/* 3190 -- the ghost aura's per-frame entry.
 *
 * The colour arrives as one packed word behind pnt[1] rather than in the
 * record's own r/g/b, laid out r:g:b:a from the top byte down.  pnt[4] is an
 * optional second position -- when it is present the pool is fed a segment
 * rather than a point, which is what makes a moving ghost trail rather than
 * smear.  dat.uc8[2] picks pool type 3 or 4. */
void SetEneFire(EFFECT_CONT *ec)
{
    u_int Color;
    int   type;
    float r, g, b, a;
    float size;
    float arate;
    float pos1[4];
    float pos2[4];
    float *pPos2;

    Color = *(u_int *)ec->pnt[1];                               /* 3195 */

    r = (float)(Color >> 24);
    g = (float)((Color >> 16) & 0xff);
    b = (float)((Color >> 8) & 0xff);
    a = (float)(Color & 0xff);

    size  = *(float *)ec->pnt[2];
    arate = *(float *)ec->pnt[5];

    type = (ec->dat.uc8[2] == 0) ? 3 : 4;

    g3dxVu0CopyVector(pos1, (float *)ec->pnt[0]);

    if (ec->pnt[4] == nullptr)
    {
        pPos2 = nullptr;
    }
    else
    {
        g3dxVu0CopyVector(pos2, (float *)ec->pnt[4]);
        pPos2 = pos2;
    }

    ec->pnt[3] = GetEnePartAddr(ec->pnt[3], type, (int)(float)(u_int)ec->dat.iv[3]);
    ec->pnt[3] = ContHeatHaze(ec->pnt[3], type, pos1, pPos2, 0,
                              r, g, b, a, size, arate);         /* 3218 */

    if ((ec->dat.uc8[1] & 1) != 0)                              /* 3220 */
    {
        ResetEffects(ec);
    }
}
