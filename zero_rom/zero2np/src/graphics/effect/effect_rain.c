// FILE: /home/zero_rom/zero2np/src/graphics/effect/effect_rain.c
//
// Weather: rain, the spray it kicks up, and dripping water.
//
// The three sub-systems share nothing but this file.  Rain is a fixed pool of
// gouraud line segments drawn straight into a GIF packet; spray and dripping
// water are camera-facing textured quads that go out through
// Set3DPosTexure().
//
// The world is Y-down here: rain spawns near Y = -8000 (high up) and its
// velocity is +Y.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0015c120.

#include "effect_rain.h"

#include <string.h>                          /* memset */

#include "effect.h"                          /* EFFECT_MALLOC / EffectGetRandom */
#include "effect_sub.h"                      /* Get2PosRot / Set3DPosTexure / SE */

#include "../draw_env.h"
#include "../graphics.h"                     /* RendererWorldLine */
#include "../graph2d/g2d_draw.h"             /* Start/EndDmaDirectTrans */
#include "../graph3d/gra3d.h"                /* gra3dGetCamera */
#include "../../sdk/libvu0.h"

/* ==========================================================================
 *  Static state.
 * ======================================================================== */

static EFFECT_RAIN_CTRL  RainCtrl;                              /* bss 46e7b0 */
static EFFECT_SPRAY_CTRL SprayCtrl;                             /* bss 46e7f0 */
static EFFECT_SPRAY_CTRL SprayCtrl2;                            /* bss 46e850 */
static EFFECT_DROP_CTRL  DropCtrl;                              /* bss 46e8b0 */

/* Where the heavy spray appears: the ground quad in the middle of the yard,
 * then the four roof slopes around it.  All five carry the same weight. */
static const SPRAY_APPEAR_DATA SprayRect[5] =                   /* rdata 3a72b0 */
{
    { { { 778.47034f,  -748.82697f, 2457.6284f,  1.0f },
        { 778.47034f,  -748.82697f,  779.41193f, 1.0f },
        { 2351.4629f,  -748.82697f,  779.41193f, 1.0f },
        { 2351.4629f,  -748.82697f, 2457.6284f,  1.0f } }, 2000 },
    { { { 768.26935f, -5138.5405f,   800.0506f,  1.0f },
        { -147.00475f, -6091.8662f, -115.22365f, 1.0f },
        { 3261.603f,  -6091.8662f,  -115.223274f, 1.0f },
        { 2346.3289f, -5138.5405f,   800.0508f,  1.0f } }, 2000 },
    { { { 2346.3289f, -5138.5405f,   800.0508f,  1.0f },
        { 3261.603f,  -6091.8662f,  -115.223274f, 1.0f },
        { 3261.603f,  -6091.8662f,  3293.3845f,  1.0f },
        { 2346.3286f, -5138.5405f,  2378.1101f,  1.0f } }, 2000 },
    { { { 2346.3286f, -5138.5405f,  2378.1101f,  1.0f },
        { 3261.603f,  -6091.8662f,  3293.3845f,  1.0f },
        { -147.00494f, -6091.8662f, 3293.384f,   1.0f },
        { 768.26923f, -5138.5405f,  2378.1101f,  1.0f } }, 2000 },
    { { { 768.26923f, -5138.5405f,  2378.1101f,  1.0f },
        { -147.00494f, -6091.8662f, 3293.384f,   1.0f },
        { -147.00475f, -6091.8662f, -115.22365f, 1.0f },
        { 768.26935f, -5138.5405f,   800.0506f,  1.0f } }, 2000 },
};

/* The fine spray: eaves, the two lantern caps, and the strip of tiles running
 * along the walkway.  Weights are proportional to each quad's area, which is
 * why they are all over the place. */
static const SPRAY_APPEAR_DATA SprayRect2[23] =                 /* rdata 3a7440 */
{
    { { { 784.54156f, -1298.8446f,  776.90399f, 1.0f },
        { 784.5415f,  -1298.8446f, 1275.9807f,  1.0f },
        { 751.42554f, -1299.5651f, 1276.9141f,  1.0f },
        { 751.42554f, -1299.5651f,  758.77942f, 1.0f } },  585 },
    { { { 751.42554f, -1299.5651f,  758.77942f, 1.0f },
        { 1258.022f,  -1299.5651f,  758.77948f, 1.0f },
        { 1257.7542f, -1298.8446f,  776.90399f, 1.0f },
        { 784.54156f, -1298.8446f,  776.90399f, 1.0f } },  308 },
    { { { 1830.1226f, -1299.5651f,  758.7796f,  1.0f },
        { 2368.4387f, -1299.5651f,  758.7796f,  1.0f },
        { 2325.6116f, -1298.8446f,  776.90399f, 1.0f },
        { 1829.7357f, -1298.8446f,  776.90399f, 1.0f } },  325 },
    { { { 2368.4387f, -1299.5651f,  758.7796f,  1.0f },
        { 2368.4387f, -1299.5651f, 2468.5898f,  1.0f },
        { 2325.6116f, -1298.8446f, 2441.4548f,  1.0f },
        { 2325.6116f, -1298.8446f,  776.90399f, 1.0f } }, 2510 },
    { { { 1683.8845f, -1298.8446f, 2441.4548f,  1.0f },
        { 2325.6116f, -1298.8446f, 2441.4548f,  1.0f },
        { 2368.4387f, -1299.5651f, 2468.5898f,  1.0f },
        { 1689.3468f, -1299.5651f, 2468.5896f,  1.0f } },  622 },
    { { { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f },
        { 1069.9176f, -1287.3365f, 1074.1967f,  1.0f },
        { 1304.6276f, -1287.3365f, 1074.1967f,  1.0f },
        { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f } },  478 },
    { { { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f },
        { 1304.6276f, -1287.3365f, 1074.1967f,  1.0f },
        { 1304.6276f, -1287.3365f, 1308.9066f,  1.0f },
        { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f } },  478 },
    { { { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f },
        { 1304.6276f, -1287.3365f, 1308.9066f,  1.0f },
        { 1069.9176f, -1287.3365f, 1308.9066f,  1.0f },
        { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f } },  478 },
    { { { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f },
        { 1069.9176f, -1287.3365f, 1308.9066f,  1.0f },
        { 1069.9176f, -1287.3365f, 1074.1967f,  1.0f },
        { 1187.2726f, -1366.4635f, 1191.5516f,  1.0f } },  478 },
    { { { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f },
        { 1937.1957f, -1287.3365f, 2029.3695f,  1.0f },
        { 2171.9055f, -1287.3365f, 2029.3695f,  1.0f },
        { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f } },  478 },
    { { { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f },
        { 2171.9055f, -1287.3365f, 2029.3695f,  1.0f },
        { 2171.9055f, -1287.3365f, 2264.0793f,  1.0f },
        { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f } },  478 },
    { { { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f },
        { 2171.9055f, -1287.3365f, 2264.0793f,  1.0f },
        { 1937.1957f, -1287.3365f, 2264.0793f,  1.0f },
        { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f } },  478 },
    { { { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f },
        { 1937.1957f, -1287.3365f, 2264.0793f,  1.0f },
        { 1937.1957f, -1287.3365f, 2029.3695f,  1.0f },
        { 2054.5505f, -1366.4635f, 2146.7244f,  1.0f } },  478 },
    { { { 1223.9926f, -1587.0914f, 1573.2516f,  1.0f },
        { 1259.0763f, -1599.9691f, 1573.1693f,  1.0f },
        { 1282.2646f, -1596.1403f, 1605.8469f,  1.0f },
        { 1221.5521f, -1593.2307f, 1607.3831f,  1.0f } },   56 },
    { { { 1259.0763f, -1599.9691f, 1573.1693f,  1.0f },
        { 1364.5884f, -1618.0629f, 1535.9727f,  1.0f },
        { 1375.3195f, -1615.9896f, 1572.4619f,  1.0f },
        { 1282.2646f, -1596.1403f, 1605.8469f,  1.0f } },  241 },
    { { { 1364.5884f, -1618.0629f, 1535.9727f,  1.0f },
        { 1490.8872f, -1555.188f,  1515.4331f,  1.0f },
        { 1497.6943f, -1560.9951f, 1548.1321f,  1.0f },
        { 1375.3195f, -1615.9896f, 1572.4619f,  1.0f } },  246 },
    { { { 1490.8872f, -1555.188f,  1515.4331f,  1.0f },
        { 1612.3242f, -1538.9093f, 1497.868f,   1.0f },
        { 1618.5739f, -1550.6133f, 1524.9659f,  1.0f },
        { 1497.6943f, -1560.9951f, 1548.1321f,  1.0f } },  211 },
    { { { 1612.3242f, -1538.9093f, 1497.868f,   1.0f },
        { 1732.0107f, -1501.5901f, 1486.6635f,  1.0f },
        { 1735.5002f, -1508.7927f, 1527.3604f,  1.0f },
        { 1618.5739f, -1550.6133f, 1524.9659f,  1.0f } },  167 },
    { { { 1732.0107f, -1501.5901f, 1486.6635f,  1.0f },
        { 1821.3309f, -1536.562f,  1553.9957f,  1.0f },
        { 1807.566f,  -1535.8417f, 1576.8181f,  1.0f },
        { 1735.5002f, -1508.7927f, 1527.3604f,  1.0f } },  252 },
    { { { 1821.3309f, -1536.562f,  1553.9957f,  1.0f },
        { 1931.1552f, -1519.8286f, 1579.8457f,  1.0f },
        { 1930.7504f, -1521.2568f, 1601.2216f,  1.0f },
        { 1807.566f,  -1535.8417f, 1576.8181f,  1.0f } },  191 },
    { { { 1931.1552f, -1519.8286f, 1579.8457f,  1.0f },
        { 1955.5275f, -1536.8105f, 1585.2806f,  1.0f },
        { 1955.614f,  -1530.5952f, 1606.8973f,  1.0f },
        { 1930.7504f, -1521.2568f, 1601.2216f,  1.0f } },   45 },
    { { { 1955.5275f, -1536.8105f, 1585.2806f,  1.0f },
        { 2142.4343f, -1558.3037f, 1628.1146f,  1.0f },
        { 2139.1753f, -1558.3037f, 1642.248f,   1.0f },
        { 1955.614f,  -1530.5952f, 1606.8973f,  1.0f } },  366 },
    { { { 2142.4343f, -1558.3037f, 1628.1146f,  1.0f },
        { 2202.5625f, -1582.3859f, 1613.1326f,  1.0f },
        { 2201.2283f, -1582.3859f, 1625.8098f,  1.0f },
        { 2139.1753f, -1558.3037f, 1642.248f,   1.0f } },   61 },
};

/* The building's footprint, and the open courtyard inside it.  Rain that has
 * fallen into the footprint but not into the courtyard is under the roof and
 * gets recycled -- see EffectRainParticleHitCheck(). */
static const RAIN_HIT_BOX OutsideBox =                          /* rdata 3a7b70 */
{
    {  4508.1714f,  -728.30206f, 4528.5435f,   0.0f },
    { -1400.4363f, -4958.4976f, -1380.0638f,   0.0f },
};

static const RAIN_HIT_BOX InsideBox =                           /* rdata 3a7b90 */
{
    { 2427.5969f,  -726.86011f, 2510.0818f,    0.0f },
    {  685.78442f, -4957.0557f,  677.92352f,   0.0f },
};

/* Where water drips from: eaves, beams and the well.  Index 34 is the one
 * that carries a sound. */
static const float DropOfWaterPos[35][4] =                      /* rdata 3a7bb0 */
{
    {  758.35339f, -1963.2294f,  730.22589f, 1.0f },
    { 2353.9358f,  -2451.0315f, 1410.3632f,  1.0f },
    { 1296.6605f,  -1282.8506f, 1084.0515f,  1.0f },
    { 1833.1094f,  -1424.4664f, 1443.2339f,  1.0f },
    { 2352.0767f,  -2452.2563f, 2216.8499f,  1.0f },
    { 2359.9128f,  -1939.9524f, 1198.1536f,  1.0f },
    { 1858.1028f,  -3585.5952f, 2357.5286f,  1.0f },
    { 1638.7574f,  -3955.4778f, 2387.813f,   1.0f },
    { 1417.9153f,  -4112.8267f, 2332.0508f,  1.0f },
    { 1076.5575f,  -3653.6528f, 2342.292f,   1.0f },
    { 1904.5446f,  -3880.9221f, 2503.1909f,  1.0f },
    { 1906.2111f,  -3976.0535f, 2344.5425f,  1.0f },
    { 1728.5076f,  -4290.6191f, 2348.0217f,  1.0f },
    { 1867.7373f,  -3787.7476f, 2407.6829f,  1.0f },
    { 2100.0112f,  -3553.5078f, 2265.5378f,  1.0f },
    { 2131.6663f,  -3518.4485f, 2344.2612f,  1.0f },
    { 2172.241f,   -3499.9084f, 2344.4907f,  1.0f },
    { 2117.6936f,  -3712.6814f, 2401.8965f,  1.0f },
    { 2178.7329f,  -3482.2268f, 2459.4224f,  1.0f },
    { 2173.6672f,  -3222.7031f, 2277.793f,   1.0f },
    { 2081.2307f,  -3103.0562f, 2435.9673f,  1.0f },
    { 1342.7859f,  -4095.0847f,  829.26624f, 1.0f },
    {  923.22406f, -4095.0847f,  850.19482f, 1.0f },
    { 1952.9712f,  -4890.2769f, 2398.0415f,  1.0f },
    { 2171.1587f,  -4890.2769f, 2395.8723f,  1.0f },
    { 2224.9434f,  -4913.4888f, 2351.999f,   1.0f },
    { 2271.6809f,  -4888.9805f, 2197.2329f,  1.0f },
    { 2271.6809f,  -4888.9805f, 2021.8964f,  1.0f },
    { 2271.6809f,  -4888.9805f, 1831.5739f,  1.0f },
    { 1729.9268f,  -4888.9805f, 2396.8066f,  1.0f },
    { 1311.741f,   -2612.624f,  2118.7146f,  1.0f },
    { 1480.1091f,  -2349.6604f, 2069.9348f,  1.0f },
    { 2039.5933f,  -4085.9878f, 2442.98f,    1.0f },
    { 2346.5591f,  -4301.0601f, 2264.4675f,  1.0f },
    { 2460.7773f,  -4824.6177f, 2195.8926f,  1.0f },
};

/* ==========================================================================
 *  Rain.
 * ======================================================================== */

static void EffectRainReqNum(int RainNum, float *Offset);
static void EffectRainParticleInit(EFFECT_RAIN_PARTICLE *pParticle,
                                   float *Offset, float PosY);
static void EffectRainParticleDraw(const EFFECT_RAIN_PARTICLE *pParticle,
                                   const int *Color0, const int *Color1);
static void EffectRainParticleUpdate(EFFECT_RAIN_PARTICLE *pParticle,
                                     float *Offset);
static int  EffectRainParticleHitCheck(const EFFECT_RAIN_PARTICLE *pParticle,
                                       float *Offset);

void EffectRainInit(void)
{
    EFFECT_RAIN_CTRL *pRainCtrl = &RainCtrl;                        /* 540 */

    pRainCtrl->pParticle = nullptr;         /* 542 */
    pRainCtrl->ParticleNum = 0;                                     /* 543 */
    pRainCtrl->Offset[0] = 0.0f; pRainCtrl->Offset[1] = 0.0f;
    pRainCtrl->Offset[2] = 0.0f; pRainCtrl->Offset[3] = 0.0f;       /* 544 */
    pRainCtrl->Color0[0] = 15; pRainCtrl->Color0[1] = 15;
    pRainCtrl->Color0[2] = 16; pRainCtrl->Color0[3] = 40;           /* 545 */
    pRainCtrl->Color1[0] = 20; pRainCtrl->Color1[1] = 20;
    pRainCtrl->Color1[2] = 20; pRainCtrl->Color1[3] = 35;           /* 546 */
}

void EffectRainReq(float *Offset)
{
    EffectRainReqNum(306, Offset);                                  /* 559 */
}

/* Allocate the pool and scatter it through the volume.  A second request
 * while rain is already up is ignored rather than reallocating. */
static void EffectRainReqNum(int RainNum, float *Offset)
{
    EFFECT_RAIN_CTRL *pRainCtrl = &RainCtrl;                        /* 570 */

    if (pRainCtrl->pParticle != nullptr)    /* 574 */
    {
        return;
    }

    if (RainNum <= 0)
    {
        return;
    }

    pRainCtrl->pParticle = (EFFECT_RAIN_PARTICLE *) EFFECT_MALLOC(RainNum * (int)sizeof(EFFECT_RAIN_PARTICLE)); /* 575 */
    if (pRainCtrl->pParticle == nullptr)    /* 576 */
    {
        return;
    }

    pRainCtrl->Offset[0] = Offset[0];
    pRainCtrl->Offset[1] = Offset[1];
    pRainCtrl->Offset[2] = Offset[2];
    pRainCtrl->Offset[3] = 0.0f;                                    /* 578 */
    pRainCtrl->ParticleNum = RainNum;

    /* Spread the initial heights over the whole column so the rain does not
     * arrive as one sheet. */
    for (int i = 0; i < RainNum; i++)                                   /* 580 */
    {
        EffectRainParticleInit(&pRainCtrl->pParticle[i], Offset,
                               EffectGetRandom(-8000.0f, -728.30225f)); /* 582 */
    }                                                               /* 583 */
}

void EffectRainCut(void)
{
    EFFECT_RAIN_CTRL *pRainCtrl = &RainCtrl;                        /* 592 */

    if (pRainCtrl->pParticle != nullptr)                            /* 593 */
    {
        EFFECT_FREE(pRainCtrl->pParticle);                          /* 595 */
        EffectRainInit();                                           /* 596 */
    }                                                               /* 597 */
}

/* Place one streak at a random spot in the volume's XZ footprint, at PosY,
 * and give it a mostly-downward velocity.
 *
 * PORT NOTE on the velocity constants: the ROM really does roll
 * EffectGetRandom(990, 1010) and then subtract 1000, rather than rolling
 * (-10, 10) directly -- the +990 and the -1000 survive as separate float ops
 * because GCC may not reassociate them.  Kept as written so the rounding
 * matches; the ranges are +/-10 for X and Z and 476..942 for Y. */
static void EffectRainParticleInit(EFFECT_RAIN_PARTICLE *pParticle,
                                   float *Offset, float PosY)
{
    float PosX, PosZ;
    float VeloX, VeloY, VeloZ;

    PosX = EffectGetRandom(-150.43639f, 3258.1714f);
    PosZ = EffectGetRandom(-130.064f, 3278.5435f);

    pParticle->Position[0] = PosX;
    pParticle->Position[1] = PosY;
    pParticle->Position[2] = PosZ;
    pParticle->Position[3] = 1.0f;                                  /* 617 */
    sceVu0AddVector(pParticle->Position, pParticle->Position, Offset); /* 618 */

    VeloX = EffectGetRandom(990.0f, 1010.0f) - 1000.0f;
    VeloY = EffectGetRandom(1476.0f, 1942.0f) - 1000.0f;
    VeloZ = EffectGetRandom(990.0f, 1010.0f) - 1000.0f;

    pParticle->Velocity[0] = VeloX;
    pParticle->Velocity[1] = VeloY;
    pParticle->Velocity[2] = VeloZ;
    pParticle->Velocity[3] = 0.0f;                                  /* 629 */
    pParticle->Length = 601.0f;                                     /* 630 */

    /* Lean the streak along the way it is falling. */
    Vector2Rot(pParticle->Velocity, &pParticle->RotX, &pParticle->RotY); /* 632 */
}

/* One gouraud-shaded, alpha-blended GS line from +Length/2 to -Length/2 along
 * the streak's own Z, colour-ramped head to tail. */
static void EffectRainParticleDraw(const EFFECT_RAIN_PARTICLE *pParticle,
                                   const int *Color0, const int *Color1)
{
    float matWorldLocal[4][4];
    float matLocalScreen[4][4];
    float vLinePos[2][4];
    int   ivLinePos[2][4];
    int   ClipFlg;
    int   i;
    Q_WORDDATA *pBuf;

    GRA3DCAMERA* pCam = gra3dGetCamera();                                        /* 689 */
    ClipFlg = 0;                                                    /* 690 */

    vLinePos[0][0] = 0.0f; vLinePos[0][1] = 0.0f;
    vLinePos[0][2] = pParticle->Length * 0.5f;  vLinePos[0][3] = 1.0f;  /* 693 */
    vLinePos[1][0] = 0.0f; vLinePos[1][1] = 0.0f;
    vLinePos[1][2] = -pParticle->Length * 0.5f; vLinePos[1][3] = 1.0f;  /* 694 */

    sceVu0UnitMatrix(matWorldLocal);                                /* 695 */
    sceVu0RotMatrixX(matWorldLocal, matWorldLocal, pParticle->RotX); /* 696 */
    sceVu0RotMatrixY(matWorldLocal, matWorldLocal, pParticle->RotY); /* 697 */
    sceVu0TransMatrix(matWorldLocal, matWorldLocal, (float *)pParticle->Position);                /* 698 */
    sceVu0MulMatrix(matLocalScreen, pCam->matWorldScreen, matWorldLocal); /* 699 */
    sceVu0RotTransPersN((sceVu0IVECTOR *)ivLinePos, matLocalScreen, (sceVu0FVECTOR *)vLinePos, 2, 0);           /* 700 */

    /* PORT: the DIRECT packet below is faithful but inert -- dmaVif1 collects
     * packets and never executes them -- so the streak also goes out as a host
     * line.  Queued before the PS2 guard band test for the same reason
     * MakePacket3D() does it: a disagreement in the emulated fixed-point
     * projection must not be able to hide an otherwise visible streak.
     *
     * depth_test is on because the ROM's TEST register (0x5000d) enables ZTE
     * with ZTST = GEQUAL.  Its ZBUF (0x10a000118) masks Z writes; that costs
     * nothing here, as rain never occludes anything behind it. */
    {
        float aWorld[2][4];

        sceVu0ApplyMatrix(aWorld[0], matWorldLocal, vLinePos[0]);
        sceVu0ApplyMatrix(aWorld[1], matWorldLocal, vLinePos[1]);

        RendererWorldLine(aWorld[0], aWorld[1],
                          (u_char)Color0[0], (u_char)Color0[1],
                          (u_char)Color0[2], (u_char)Color0[3],
                          (u_char)Color1[0], (u_char)Color1[1],
                          (u_char)Color1[2], (u_char)Color1[3], 1);
    }

    /* Guard band test on both ends: X/Y must land inside 0x300..0xfd00 and Z
     * inside 0xf..0xfffff.  Biasing then comparing unsigned catches the
     * below-minimum case in the same instruction -- underflow wraps high. */
    for (i = 0; i < 2; i++)                                         /* 702 */
    {
        if ((u_int)(ivLinePos[i][0] - 0x300) > 64000)                /* 703 */
        {
            ClipFlg = 1;
        }
        if ((u_int)(ivLinePos[i][1] - 0x300) > 64000)                /* 704 */
        {
            ClipFlg = 1;
        }
        if ((u_int)(ivLinePos[i][2] - 0xf) > 0xffff0)                /* 705 */
        {
            ClipFlg = 1;
        }
    }                                                               /* 706 */

    if (ClipFlg != 0)                                               /* 709 */
    {
        return;
    }

    /* The ROM's line numbers for the block below (647..673) sit above this
     * function's own (684..709), so the packet writer was a separate static
     * routine defined just before EffectRainParticleDraw() that GCC inlined
     * here -- functions.txt still carries its Pos1/pBuf locals. */
    pBuf = StartDmaDirectTrans();                                   /* 647 */

    /* GIF tag: NLOOP 1, EOP, PACKED, 4 regs, PRE with PRIM = LINE | IIP | ABE;
     * REGS 0x4141 = RGBAQ, XYZ2, RGBAQ, XYZ2. */
    pBuf[0].ul64[0] = 0x4024c00000008001ULL;                        /* 649 */
    pBuf[0].ul64[1] = 0x4141;                                       /* 650 */

    pBuf[1].iv[0] = Color0[0];                                      /* 652 */
    pBuf[1].iv[1] = Color0[1];                                      /* 653 */
    pBuf[1].iv[2] = Color0[2];                                      /* 654 */
    pBuf[1].iv[3] = Color0[3];                                      /* 655 */

    pBuf[2].iv[0] = ivLinePos[0][0];                                /* 657 */
    pBuf[2].iv[1] = ivLinePos[0][1];                                /* 658 */
    pBuf[2].iv[2] = ivLinePos[0][2];                                /* 659 */
    pBuf[2].iv[3] = 0;                                              /* 660 */

    pBuf[3].iv[0] = Color1[0];                                      /* 662 */
    pBuf[3].iv[1] = Color1[1];                                      /* 663 */
    pBuf[3].iv[2] = Color1[2];                                      /* 664 */
    pBuf[3].iv[3] = Color1[3];                                      /* 665 */

    pBuf[4].iv[0] = ivLinePos[1][0];                                /* 667 */
    pBuf[4].iv[1] = ivLinePos[1][1];                                /* 668 */
    pBuf[4].iv[2] = ivLinePos[1][2];                                /* 669 */
    pBuf[4].iv[3] = 0;                                              /* 670 */

    EndDmaDirectTrans(pBuf + 5);                                    /* 673 */
}

static void EffectRainParticleUpdate(EFFECT_RAIN_PARTICLE *pParticle,
                                     float *Offset)
{
    sceVu0AddVector(pParticle->Position, pParticle->Position,
                    pParticle->Velocity);                           /* 720 */

    /* Recycled from the top of the column, not from where it stopped. */
    if (EffectRainParticleHitCheck(pParticle, Offset) != 0)         /* 721 */
    {
        EffectRainParticleInit(pParticle, Offset, -8000.0f);        /* 723 */
    }                                                               /* 724 */
}

void EffectRainDraw(void)
{
    EFFECT_RAIN_CTRL *pRainCtrl = &RainCtrl;                        /* 733 */
    DRAW_ENV_NOTEX EnvNoTex;

    if (pRainCtrl->pParticle == nullptr)                            /* 739 */
    {
        return;
    }

    EnvNoTex.alpha = 0x48;
    EnvNoTex.test  = 0x5000d;
    EnvNoTex.zbuf  = 0x10a000118ULL;                                /* 741 */
    SetDrawEnvNoTex(0, &EnvNoTex);                                  /* 750 */

    /* Drawn and stepped in the same pass, so the streak on screen is the one
     * from before this frame's move. */
    for (int i = 0; i < pRainCtrl->ParticleNum; i++)                    /* 752 */
    {
        EffectRainParticleDraw(&pRainCtrl->pParticle[i], pRainCtrl->Color0, pRainCtrl->Color1); /* 753 */
        EffectRainParticleUpdate(&pRainCtrl->pParticle[i], pRainCtrl->Offset);                /* 754 */
    }                                                               /* 755 */
}

/* Nonzero when the streak has run out: it has reached ground level (Y >= 0),
 * or it is inside the building footprint but not in the open courtyard, i.e.
 * it is now under a roof.  Both boxes are relative to the volume's Offset. */
static int EffectRainParticleHitCheck(const EFFECT_RAIN_PARTICLE *pParticle,
                                      float *Offset)
{
    int Hit = 1;

    if (pParticle->Position[1] < 0.0f)                              /* 773 */
    {
        {
            const RAIN_HIT_BOX *pBox = &OutsideBox;
            int Inside = 0;

            if (Offset[0] + pBox->Min[0] <= pParticle->Position[0] &&
                pParticle->Position[0] <= Offset[0] + pBox->Max[0] &&
                Offset[1] + pBox->Min[1] <= pParticle->Position[1] &&
                pParticle->Position[1] <= Offset[1] + pBox->Max[1] &&
                Offset[2] + pBox->Min[2] <= pParticle->Position[2] &&
                pParticle->Position[2] <= Offset[2] + pBox->Max[2])
            {
                Inside = 1;
            }

            /* Still in open air outside the building. */
            if (Inside == 0)
            {
                return 0;                                           /* 776 */
            }
        }

        {
            const RAIN_HIT_BOX *pBox = &InsideBox;
            int Inside = 0;

            if (Offset[0] + pBox->Min[0] <= pParticle->Position[0] &&
                pParticle->Position[0] <= Offset[0] + pBox->Max[0] &&
                Offset[1] + pBox->Min[1] <= pParticle->Position[1] &&
                pParticle->Position[1] <= Offset[1] + pBox->Max[1] &&
                Offset[2] + pBox->Min[2] <= pParticle->Position[2] &&
                pParticle->Position[2] <= Offset[2] + pBox->Max[2])
            {
                Inside = 1;
            }

            /* Down the middle of the courtyard, which is open to the sky. */
            if (Inside != 0)
            {
                return 0;                                           /* 800 */
            }
        }
    }                                                               /* 808 */

    return Hit;                                                     /* 810 */
}

/* ==========================================================================
 *  Spray.
 *
 *  Two independent controls over the same code: SprayCtrl is the heavy spray
 *  bouncing off the ground and the roofs, SprayCtrl2 the fine mist along the
 *  eaves.  Only SprayCtrl gives its particles a velocity; the mist just sits
 *  and fades.
 * ======================================================================== */

static void EffectSprayReqNumRect(EFFECT_SPRAY_CTRL *pSprayCtrl, float *Offset,
                                  int SprayNum,
                                  const SPRAY_APPEAR_DATA *pRect, int RectNum);
static void EffectSprayCut(EFFECT_SPRAY_CTRL *pSprayCtrl);
static EFFECT_SPRAY_PARTICLE *
            EffectSprayParticleBufferGet(EFFECT_SPRAY_CTRL *pSprayCtrl);
static void EffectSprayParticleReqOneFrame(EFFECT_SPRAY_CTRL *pSprayCtrl,
                                           float *Offset);
static void EffectSprayParticleUpdate(EFFECT_SPRAY_PARTICLE *pParticle);
static void EffectSprayDraw(EFFECT_SPRAY_CTRL *pSprayCtrl);
static void EffectSprayParticleDraw(const float *Position, float RotX,
                                    float RotY, int R, int G, int B, int Alpha,
                                    float ScaleX, float ScaleY);
static void EffectSprayParticleClear(EFFECT_SPRAY_PARTICLE *pParticle);
static void EffectSprayParticleInit(EFFECT_SPRAY_PARTICLE *pParticle,
                                    const float (*pRectVec)[4], float *Offset,
                                    float *Velocity, int LifeTime,
                                    float StartScale, float LastScale);

void EffectSprayInit(void)
{
    EFFECT_SPRAY_CTRL *pSprayCtrl  = &SprayCtrl;                    /* 821 */
    EFFECT_SPRAY_CTRL *pSprayCtrl2 = &SprayCtrl2;                   /* 822 */

    pSprayCtrl->R = 0x48;                                           /* 862 */
    pSprayCtrl->G = 0x47;                                           /* 863 */
    pSprayCtrl->B = 0x48;                                           /* 864 */
    pSprayCtrl->Alpha = 0x14;                                       /* 865 */
    pSprayCtrl->AppearNumMin = 2;                                   /* 866 */
    pSprayCtrl->AppearNumMax = 5;                                   /* 867 */
    pSprayCtrl->InitLifeTime = 0x26;                                /* 868 */
    pSprayCtrl->SpeedXMax = 0.0f;                                   /* 869 */
    pSprayCtrl->SpeedXMin = 0.0f;                                   /* 870 */
    pSprayCtrl->SpeedYMax = 5.0f;                                   /* 871 */
    pSprayCtrl->SpeedYMin = -1.0f;                                  /* 872 */
    pSprayCtrl->SpeedZMax = 0.0f;                                   /* 873 */
    pSprayCtrl->SpeedZMin = 0.0f;                                   /* 874 */
    pSprayCtrl->StartScale = 1.0f;                                  /* 875 */
    pSprayCtrl->LastScaleMax = 10.0f;                               /* 876 */
    pSprayCtrl->LastScaleMin = 2.5f;                                /* 877 */

    pSprayCtrl2->R = 0x3d;                                          /* 879 */
    pSprayCtrl2->G = 0x3d;                                          /* 880 */
    pSprayCtrl2->B = 0x3d;                                          /* 881 */
    pSprayCtrl2->Alpha = 0x16;                                      /* 882 */
    pSprayCtrl2->AppearNumMin = 1;                                  /* 883 */
    pSprayCtrl2->AppearNumMax = 0xc;                                /* 884 */
    pSprayCtrl2->InitLifeTime = 0xf;                                /* 885 */
    pSprayCtrl2->SpeedXMax = 0.0f;                                  /* 886 */
    pSprayCtrl2->SpeedXMin = 0.0f;                                  /* 887 */
    pSprayCtrl2->SpeedYMax = 0.0f;                                  /* 888 */
    pSprayCtrl2->SpeedYMin = 0.0f;                                  /* 889 */
    pSprayCtrl2->SpeedZMax = 0.0f;                                  /* 890 */
    pSprayCtrl2->SpeedZMin = 0.0f;                                  /* 891 */
    pSprayCtrl2->StartScale = 0.52f;                                /* 892 */
    pSprayCtrl2->LastScaleMax = 0.52f;                              /* 893 */
    pSprayCtrl2->LastScaleMin = 0.52f;                              /* 894 */

    pSprayCtrl->pAppearData = (const SPRAY_APPEAR_DATA *)nullptr;   /* 896 */
    pSprayCtrl->RectNum = 0;                                        /* 897 */
    pSprayCtrl->ParticleNum = 0;                                    /* 898 */
    pSprayCtrl->pParticle = (EFFECT_SPRAY_PARTICLE *)nullptr;       /* 899 */
    pSprayCtrl->Offset[0] = 0.0f; pSprayCtrl->Offset[1] = 0.0f;
    pSprayCtrl->Offset[2] = 0.0f; pSprayCtrl->Offset[3] = 0.0f;     /* 900 */

    pSprayCtrl2->pAppearData = (const SPRAY_APPEAR_DATA *)nullptr;  /* 902 */
    pSprayCtrl2->RectNum = 0;                                       /* 903 */
    pSprayCtrl2->ParticleNum = 0;                                   /* 904 */
    pSprayCtrl2->pParticle = (EFFECT_SPRAY_PARTICLE *)nullptr;      /* 905 */
    pSprayCtrl2->Offset[0] = 0.0f; pSprayCtrl2->Offset[1] = 0.0f;
    pSprayCtrl2->Offset[2] = 0.0f; pSprayCtrl2->Offset[3] = 0.0f;   /* 906 */
}

void EffectSprayReq(float *Offset)
{
    EffectSprayReqNumRect(&SprayCtrl, Offset, 100, SprayRect, 5);   /* 921 */
    EffectSprayReqNumRect(&SprayCtrl2, Offset, 52, SprayRect2, 23); /* 922 */
}

static void EffectSprayReqNumRect(EFFECT_SPRAY_CTRL *pSprayCtrl, float *Offset,
                                  int SprayNum,
                                  const SPRAY_APPEAR_DATA *pRect, int RectNum)
{
    int i;

    if (pSprayCtrl->pParticle != (EFFECT_SPRAY_PARTICLE *)nullptr ||
        pRect == (const SPRAY_APPEAR_DATA *)nullptr ||
        SprayNum <= 0 || RectNum <= 0)                              /* 938 */
    {
        return;
    }

    pSprayCtrl->pParticle = (EFFECT_SPRAY_PARTICLE *)
        EFFECT_MALLOC(SprayNum * (int)sizeof(EFFECT_SPRAY_PARTICLE)); /* 939 */
    if (pSprayCtrl->pParticle == (EFFECT_SPRAY_PARTICLE *)nullptr)  /* 940 */
    {
        return;
    }

    pSprayCtrl->Offset[0] = Offset[0];
    pSprayCtrl->Offset[1] = Offset[1];
    pSprayCtrl->Offset[2] = Offset[2];
    pSprayCtrl->Offset[3] = Offset[3];                              /* 942 */
    pSprayCtrl->pAppearData = pRect;                                /* 943 */
    pSprayCtrl->RectNum = RectNum;                                  /* 944 */
    pSprayCtrl->Offset[3] = 0.0f;
    pSprayCtrl->ParticleNum = SprayNum;                             /* 946 */

    for (i = 0; i < SprayNum; i++)                                  /* 947 */
    {
        EffectSprayParticleClear(&pSprayCtrl->pParticle[i]);
    }

    /* Seed the first frame's worth so the spray does not start empty. */
    EffectSprayParticleReqOneFrame(pSprayCtrl, Offset);             /* 949 */
}

void EffectSprayAllCut(void)
{
    EffectSprayCut(&SprayCtrl);                                     /* 959 */
    EffectSprayCut(&SprayCtrl2);                                    /* 960 */
}

static void EffectSprayCut(EFFECT_SPRAY_CTRL *pSprayCtrl)
{
    if (pSprayCtrl->pParticle != (EFFECT_SPRAY_PARTICLE *)nullptr)  /* 970 */
    {
        EFFECT_FREE(pSprayCtrl->pParticle);                         /* 971 */
        pSprayCtrl->ParticleNum = 0;                                /* 972 */
        pSprayCtrl->pParticle = (EFFECT_SPRAY_PARTICLE *)nullptr;   /* 973 */
        pSprayCtrl->pAppearData = (const SPRAY_APPEAR_DATA *)nullptr;
        pSprayCtrl->RectNum = 0;                                    /* 975 */
    }
}

/* First free slot, or NULL when the pool is full. */
static EFFECT_SPRAY_PARTICLE *
EffectSprayParticleBufferGet(EFFECT_SPRAY_CTRL *pSprayCtrl)
{
    EFFECT_SPRAY_PARTICLE *pRet;
    int i;

    pRet = (EFFECT_SPRAY_PARTICLE *)nullptr;                        /* 991 */

    for (i = 0; i < pSprayCtrl->ParticleNum; i++)                   /* 992 */
    {
        pRet = &pSprayCtrl->pParticle[i];
        if (pRet->LifeTime == -1)                                   /* 994 */
        {
            return pRet;
        }
        pRet = (EFFECT_SPRAY_PARTICLE *)nullptr;                    /* 996 */
    }

    return pRet;                                                    /* 998 */
}

/* Release this frame's batch: AppearNumMin..AppearNumMax particles, each on a
 * rectangle picked by weight. */
static void EffectSprayParticleReqOneFrame(EFFECT_SPRAY_CTRL *pSprayCtrl,
                                           float *Offset)
{
    float Velocity[4];
    EFFECT_SPRAY_PARTICLE *pParticle;
    int   AppearNum;
    int   i, j;
    int   RectNo;
    int   Count;
    int   RandVal;
    float StartScale;
    float LastScale;

    /* The ROM takes Offset but never reads it -- the placement below goes
     * through pSprayCtrl->Offset.  Both callers pass the same value, so this
     * is dead rather than wrong. */
    (void)Offset;

    AppearNum = (int)((float)(pSprayCtrl->AppearNumMax -
                              pSprayCtrl->AppearNumMin) *
                      EffectGetRandom(0.0f, 1.0f)) +
                pSprayCtrl->AppearNumMin;                           /* 1017 */

    for (i = 0; i < AppearNum; i++)                                 /* 1019 */
    {
        pParticle = EffectSprayParticleBufferGet(pSprayCtrl);       /* 1020 */
        if (pParticle == nullptr)                                   /* 1021 */
        {
            break;
        }

        /* Weighted pick: walk the table accumulating Rate until it passes the
         * roll.  Rates sum to 10000, so overshooting the end can only happen
         * on rounding -- the last rectangle catches it. */
        RandVal = (int)(EffectGetRandom(0.0f, 1.0f) * 10000.0f);
        RectNo  = pSprayCtrl->RectNum - 1;

        if (pSprayCtrl->RectNum > 0)                                /* 1028 */
        {
            Count = pSprayCtrl->pAppearData[0].Rate;                /* 1029 */
            if (RandVal <= Count)                                   /* 1030 */
            {
                RectNo = 0;                                         /* 1032 */
            }
            else
            {
                for (j = 1; j < pSprayCtrl->RectNum; j++)           /* 1034 */
                {
                    Count += pSprayCtrl->pAppearData[j].Rate;       /* 1029 */
                    RectNo = j;                                     /* 1031 */
                    if (RandVal <= Count)                           /* 1030 */
                    {
                        break;
                    }
                }
            }
        }

        StartScale = pSprayCtrl->StartScale;                        /* 1036 */
        LastScale  = EffectGetRandom(pSprayCtrl->LastScaleMin,
                                     pSprayCtrl->LastScaleMax);     /* 1037 */

        Velocity[0] = EffectGetRandom(pSprayCtrl->SpeedXMin,
                                      pSprayCtrl->SpeedXMax);       /* 529 */
        Velocity[1] = EffectGetRandom(pSprayCtrl->SpeedYMin,
                                      pSprayCtrl->SpeedYMax);       /* 530 */
        Velocity[2] = EffectGetRandom(pSprayCtrl->SpeedZMin,
                                      pSprayCtrl->SpeedZMax);       /* 531 */
        Velocity[3] = 0.0f;                                         /* 532 */

        EffectSprayParticleInit(pParticle,
                                pSprayCtrl->pAppearData[RectNo].Rect,
                                pSprayCtrl->Offset, Velocity,
                                pSprayCtrl->InitLifeTime,
                                StartScale, LastScale);             /* 1039 */
    }                                                               /* 1040 */
}

static void EffectSprayParticleUpdate(EFFECT_SPRAY_PARTICLE *pParticle)
{
    sceVu0AddVector(pParticle->Position, pParticle->Position,
                    pParticle->Velocity);                           /* 1049 */

    if (pParticle->LifeTime == 0)                                   /* 1050 */
    {
        pParticle->LifeTime = -1;                                   /* 1051 */
    }
    else
    {
        pParticle->LifeTime = pParticle->LifeTime - 1;              /* 1055 */
    }
}

void EffectSprayAllDraw(void)
{
    EffectSprayDraw(&SprayCtrl);                                    /* 1102 */
    EffectSprayDraw(&SprayCtrl2);                                   /* 1103 */
}

/* Draw, step, then top the pool back up -- so a particle is visible for its
 * whole life including the frame it was born on. */
static void EffectSprayDraw(EFFECT_SPRAY_CTRL *pSprayCtrl)
{
    float RotX, RotY;
    int   i;
    float Progress;
    float Scale;

    GRA3DCAMERA* pCam = gra3dGetCamera();                           /* 1119 */

    if (pSprayCtrl->pParticle == nullptr)                           /* 1120 */
    {
        return;
    }

    /* One camera rotation for the whole batch: every puff faces the same way,
     * which is what makes them read as a sheet rather than as sprites. */
    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &RotX, &RotY); /* 1124 */

    for (i = 0; i < pSprayCtrl->ParticleNum; i++)                   /* 1125 */
    {
        EFFECT_SPRAY_PARTICLE *pParticle = &pSprayCtrl->pParticle[i];

        if (pParticle->LifeTime == -1)                              /* 1127 */
        {
            continue;
        }

        /* 0 at birth, 1 at death: grows from StartScale to LastScale while
         * fading out. */
        Progress = (float)(pSprayCtrl->InitLifeTime - pParticle->LifeTime) /
                   (float)pSprayCtrl->InitLifeTime;                 /* 1128 */
        Scale = (pParticle->LastScale - pParticle->StartScale) * Progress +
                pParticle->StartScale;                              /* 1133 */

        EffectSprayParticleDraw(pParticle->Position, RotX, RotY,
                                pSprayCtrl->R, pSprayCtrl->G, pSprayCtrl->B,
                                (int)((float)pSprayCtrl->Alpha *
                                      (1.0f - Progress)),
                                Scale, Scale);                      /* 1134 */
        EffectSprayParticleUpdate(pParticle);                       /* 1135 */
    }                                                               /* 1139 */

    EffectSprayParticleReqOneFrame(pSprayCtrl, pSprayCtrl->Offset); /* 1141 */
}

static void EffectSprayParticleDraw(const float *Position, float RotX,
                                    float RotY, int R, int G, int B, int Alpha,
                                    float ScaleX, float ScaleY)
{
    float matWorldLocal[4][4];
    DRAW_ENV DrawEnv;

    DrawEnv.tex1  = 0x161;
    DrawEnv.alpha = 0x48;
    DrawEnv.zbuf  = 0x10a000118ULL;
    DrawEnv.test  = 0x5000d;
    DrawEnv.clamp = SCE_GS_SET_CLAMP(0, 0, 0, 0, 0, 0);
    DrawEnv.prim  = SCE_GIF_SET_TAG(4, 1, 1, SCE_GS_SET_PRIM(SCE_GS_PRIM_TRISTRIP, 0, 1, 0, 1, 0, 0, 0, 0), 0, 3); /* 1160 */

    sceVu0UnitMatrix(matWorldLocal);                                /* 1169 */
    sceVu0RotMatrixX(matWorldLocal, matWorldLocal, RotX);           /* 1170 */
    sceVu0RotMatrixY(matWorldLocal, matWorldLocal, RotY);           /* 1171 */
    sceVu0TransMatrix(matWorldLocal, matWorldLocal, (float *)Position); /* 1172 */

    /* Texture 0x40 out of the effect bank; the scales are in metres here and
     * the quad wants centimetres. */
    Set3DPosTexure(matWorldLocal, &DrawEnv, 0x40,
                   ScaleX * 100.0f, ScaleY * 100.0f,
                   (u_char)R, (u_char)G, (u_char)B, (u_char)Alpha); /* 1176 */
}

static void EffectSprayParticleClear(EFFECT_SPRAY_PARTICLE *pParticle)
{
    pParticle->LifeTime = -1;                                       /* 1186 */
}

/* Place the particle at a random point on the rectangle.  The same roll `t`
 * slides along edge 0->1 and along edge 3->2, then a second roll interpolates
 * between the two -- so the pair of edges does not have to be parallel and
 * the distribution follows the quad however it is sheared. */
static void EffectSprayParticleInit(EFFECT_SPRAY_PARTICLE *pParticle,
                                    const float (*pRectVec)[4], float *Offset,
                                    float *Velocity, int LifeTime,
                                    float StartScale, float LastScale)
{
    float SideVector[2][4];
    float InsideVector[4];
    float t;

    if (pParticle == (EFFECT_SPRAY_PARTICLE *)nullptr)              /* 1199 */
    {
        return;
    }

    t = EffectGetRandom(0.0f, 1.0f);                                /* 1204 */

    sceVu0SubVector(SideVector[0], (float *)pRectVec[1], (float *)pRectVec[0]);                          /* 1208 */
    sceVu0ScaleVector(SideVector[0], SideVector[0], t);             /* 1209 */
    sceVu0AddVector(SideVector[0], SideVector[0], (float *)pRectVec[0]); /* 1210 */

    sceVu0SubVector(SideVector[1], (float *)pRectVec[2], (float *)pRectVec[3]);                          /* 1212 */
    sceVu0ScaleVector(SideVector[1], SideVector[1], t);             /* 1213 */
    sceVu0AddVector(SideVector[1], SideVector[1], (float *)pRectVec[3]); /* 1214 */

    sceVu0SubVector(InsideVector, SideVector[1], SideVector[0]);    /* 1217 */
    sceVu0ScaleVector(InsideVector, InsideVector, EffectGetRandom(0.0f, 1.0f));                 /* 1218 */
    sceVu0AddVector(pParticle->Position, InsideVector, SideVector[0]); /* 1219 */
    sceVu0AddVector(pParticle->Position, pParticle->Position, Offset); /* 1221 */

    pParticle->Velocity[0] = Velocity[0];
    pParticle->Velocity[1] = Velocity[1];
    pParticle->Velocity[2] = Velocity[2];
    pParticle->Velocity[3] = Velocity[3];                           /* 1224 */
    pParticle->LifeTime = LifeTime;                                 /* 1225 */
    pParticle->StartScale = StartScale;                             /* 1226 */
    pParticle->LastScale = LastScale;                               /* 1227 */
}

/* ==========================================================================
 *  Dripping water.
 * ======================================================================== */

static int  EffectDropParticleUpdate(EFFECT_DROP_PARTICLE *pParticle);
static void EffectDropOfWaterReqAppearPos(EFFECT_DROP_CTRL *pCtrl,
                                          const float (*pAppearPos)[4],
                                          float *Offset, int AppearPosNum,
                                          float ScaleX, float ScaleY,
                                          int R, int G, int B, int Alpha);
static void EffectDropParticleReqOneFrame(EFFECT_DROP_CTRL *pDropCtrl);
static void EffectDropParticleInit(EFFECT_DROP_PARTICLE *pParticle,
                                   const float *Position, const float *Velocity,
                                   float Gravity, float GroundHeight);

void EffectDropOfWaterInit(void)
{
    EFFECT_DROP_CTRL *pDropCtrl = &DropCtrl;                        /* 1237 */

    memset(pDropCtrl, 0, sizeof(EFFECT_DROP_CTRL));                 /* 1239 */
    pDropCtrl->pList = (SINGLE_LINK_LIST *)nullptr;                 /* 1240 */
    pDropCtrl->pAppearPos = (const float (*)[4])nullptr;            /* 1241 */
    pDropCtrl->Offset[0] = 0.0f; pDropCtrl->Offset[1] = 0.0f;
    pDropCtrl->Offset[2] = 0.0f; pDropCtrl->Offset[3] = 0.0f;       /* 1242 */
}

void EffectDropOfWaterDraw(void)
{
    EFFECT_DROP_CTRL *pDropCtrl = &DropCtrl;
    EFFECT_DROP_PARTICLE *pParticle;
    float RotX, RotY;
    GRA3DCAMERA *pCam;
    SLL_CELL *pCell;
    SLL_CELL *pNextCell;

    pCam = gra3dGetCamera();                                        /* 1249 */

    if (pDropCtrl->pList == (SINGLE_LINK_LIST *)nullptr)            /* 1250 */
    {
        return;
    }

    Get2PosRot(gra3dcamGetPosition(), pCam->vTarget, &RotX, &RotY); /* 1253 */

    /* The next cell is taken before the body runs, because the update can
     * remove the cell that is being walked. */
    pCell = SingleLinkListBeginCell(pDropCtrl->pList);              /* 1254 */
    if (pCell != (SLL_CELL *)nullptr)
    {
        pNextCell = SingleLinkListNextCell(pCell);

        for (;;)
        {
            pParticle =
                (EFFECT_DROP_PARTICLE *)SingleLinkListCellBodyPtr(pCell); /* 1268 */

            EffectSprayParticleDraw(pParticle->Position, RotX, RotY,
                                    pDropCtrl->R, pDropCtrl->G, pDropCtrl->B,
                                    pDropCtrl->Alpha,
                                    pDropCtrl->ScaleX, pDropCtrl->ScaleY); /* 1269 */

            if (EffectDropParticleUpdate(pParticle) == 0)           /* 1272 */
            {
                SingleLinkListRemove(pDropCtrl->pList, pCell);      /* 1275 */
            }

            if (pNextCell == (SLL_CELL *)nullptr)                   /* 1277 */
            {
                break;
            }

            pCell     = pNextCell;                                  /* 1278 */
            pNextCell = SingleLinkListNextCell(pNextCell);          /* 1279 */
        }                                                           /* 1281 */
    }

    EffectDropParticleReqOneFrame(pDropCtrl);                       /* 1284 */
}

/* Nonzero while the drop is still above the ground height it was given. */
static int EffectDropParticleUpdate(EFFECT_DROP_PARTICLE *pParticle)
{
    int RetVal;

    RetVal = (pParticle->Position[1] < pParticle->GroundHeight);    /* 1297 */

    if (RetVal != 0)
    {
        sceVu0AddVector(pParticle->Position, pParticle->Position,
                        pParticle->Velocity);                       /* 1301 */
        pParticle->Velocity[1] = pParticle->Velocity[1] +
                                 pParticle->Gravity;                /* 1302 */
    }

    return RetVal;                                                  /* 1308 */
}

void EffectDropOfWaterReq(float *Offset)
{
    EffectDropOfWaterReqAppearPos(&DropCtrl, DropOfWaterPos, Offset, 35,
                                  0.05f, 0.09f, 0x4b, 0x4b, 0x4b, 0x3b); /* 1326 */
    EffectSndFileReadyReq(0xcc7);                                   /* 1328 */
}

static void EffectDropOfWaterReqAppearPos(EFFECT_DROP_CTRL *pCtrl,
                                          const float (*pAppearPos)[4],
                                          float *Offset, int AppearPosNum,
                                          float ScaleX, float ScaleY,
                                          int R, int G, int B, int Alpha)
{
    SINGLE_LINK_LIST *pNewList;

    if (pCtrl->pList != (SINGLE_LINK_LIST *)nullptr ||
        pCtrl == (EFFECT_DROP_CTRL *)nullptr ||
        pAppearPos == (const float (*)[4])nullptr ||
        pCtrl->pAppearPos != (const float (*)[4])nullptr)           /* 1346 */
    {
        return;
    }

    pNewList = SingleLinkListAlloc((u_int)sizeof(EFFECT_DROP_PARTICLE)); /* 1347 */
    if (pNewList == (SINGLE_LINK_LIST *)nullptr)                    /* 1348 */
    {
        return;
    }

    pCtrl->Offset[0] = Offset[0];
    pCtrl->Offset[1] = Offset[1];
    pCtrl->Offset[2] = Offset[2];
    pCtrl->Offset[3] = Offset[3];                                   /* 1350 */
    pCtrl->pList = pNewList;                                        /* 1352 */
    pCtrl->pAppearPos = pAppearPos;                                 /* 1353 */
    pCtrl->AppearPosNum = AppearPosNum;                             /* 1354 */
    pCtrl->ScaleX = ScaleX;                                         /* 1355 */
    pCtrl->ScaleY = ScaleY;                                         /* 1356 */
    pCtrl->R = R;                                                   /* 1357 */
    pCtrl->G = G;                                                   /* 1358 */
    pCtrl->B = B;                                                   /* 1359 */
    pCtrl->Alpha = Alpha;                                           /* 1360 */
    pCtrl->Offset[3] = 0.0f;                                        /* 1361 */
}

void EffectDropOfWaterCut(void)
{
    EFFECT_DROP_CTRL *pDropCtrl = &DropCtrl;                        /* 1372 */

    if (pDropCtrl->pList != (SINGLE_LINK_LIST *)nullptr)            /* 1374 */
    {
        SingleLinkListFree(pDropCtrl->pList);                       /* 1375 */
        pDropCtrl->pAppearPos = (const float (*)[4])nullptr;        /* 1376 */
        pDropCtrl->pList = (SINGLE_LINK_LIST *)nullptr;             /* 1377 */
    }

    EffectSndFileRelease(0xcc7);                                    /* 1379 */
}

/* One drop, 53 frames in 100 on average, from a random one of the 35 points.
 * Drops start still and are pulled down by Gravity; DropOfWaterPos[34] is the
 * one over the well, so it gets a splash sound. */
static void EffectDropParticleReqOneFrame(EFFECT_DROP_CTRL *pDropCtrl)
{
    float Position[4];
    float Frequency;
    float RandVal;
    int   ReqNum;
    int   i;
    EFFECT_DROP_PARTICLE Particle;
    float Velocity[4];
    int   PosNo;

    Frequency = 53.0f;                                              /* 1392 */
    RandVal = EffectGetRandom(0.0f, 1.0f) * 100.0f;                 /* 1400 */

    ReqNum = 0;                                                     /* 1405 */
    if (RandVal <= Frequency)
    {
        ReqNum = 1;                                                 /* 1406 */
    }

    if (ReqNum == 0)                                                /* 1417 */
    {
        return;
    }

    for (i = 0; i < ReqNum; i++)
    {
        PosNo = (int)((float)pDropCtrl->AppearPosNum *
                      EffectGetRandom(0.0f, 1.0f));                 /* 1420 */
        if (PosNo >= pDropCtrl->AppearPosNum)                       /* 1422 */
        {
            PosNo = pDropCtrl->AppearPosNum - 1;
        }

        /* The ROM indexes the file-scope table here rather than the
         * pAppearPos it was handed, which makes pAppearPos a presence flag in
         * practice.  Kept as written -- EffectDropOfWaterReq() is the only
         * caller and it passes DropOfWaterPos anyway. */
        Position[0] = DropOfWaterPos[PosNo][0];
        Position[1] = DropOfWaterPos[PosNo][1];
        Position[2] = DropOfWaterPos[PosNo][2];
        Position[3] = DropOfWaterPos[PosNo][3];
        sceVu0AddVector(Position, Position, pDropCtrl->Offset);     /* 1426 */

        Velocity[0] = EffectGetRandom(0.0f, 0.0f);
        Velocity[1] = EffectGetRandom(0.0f, 0.0f);
        Velocity[2] = EffectGetRandom(0.0f, 0.0f);
        Velocity[3] = 0.0f;                                         /* 532 */

        EffectDropParticleInit(&Particle, Position, Velocity,
                               2.2f, -800.0f);                      /* 1436 */
        SingleLinkListAddEnd(pDropCtrl->pList, &Particle);          /* 1438 */

        if (PosNo == 34)                                            /* 1440 */
        {
            EffectSndPlay(0xcc7, 0, 1, 0, (float (*)[3])Position);  /* 1441 */
        }
    }                                                               /* 1443 */
}

static void EffectDropParticleInit(EFFECT_DROP_PARTICLE *pParticle,
                                   const float *Position, const float *Velocity,
                                   float Gravity, float GroundHeight)
{
    if (pParticle == (EFFECT_DROP_PARTICLE *)nullptr)               /* 1457 */
    {
        return;
    }

    pParticle->Position[0] = Position[0];
    pParticle->Position[1] = Position[1];
    pParticle->Position[2] = Position[2];
    pParticle->Position[3] = Position[3];

    pParticle->Velocity[0] = Velocity[0];
    pParticle->Velocity[1] = Velocity[1];
    pParticle->Velocity[2] = Velocity[2];
    pParticle->Velocity[3] = Velocity[3];                           /* 1461 */

    pParticle->GroundHeight = GroundHeight;                         /* 1462 */
    pParticle->Gravity = Gravity;                                   /* 1463 */
}
