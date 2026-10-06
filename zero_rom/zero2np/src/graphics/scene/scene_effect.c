/* /home/zero_rom/zero2np/src/graphics/scene/scene_effect.c
   Reconstructed from the Feb 6 2004 prototype (SLES_523.84), 0x0024cfd0.

   The scene-effect controller: the interpreter for the FOD file's effect
   byte-stream.  Every cutscene post-process -- dither, blur, deform, focus,
   contrast, nega, frame fade, screen fades, lens flare, lights, torches,
   heat haze, model fades, particle deforms, pad vibration -- arrives here as
   a per-frame request block and is either fired immediately or parked in one
   of seven little controllers that re-feed the effect layer every frame.

   Layout of the request stream (all offsets from the block base):
     SCENE_EFFECT_HEADER { TableNum, TimingTableOffset, VibrationDataOffset }
     at TimingTableOffset: SCENE_EFFECT_TABLE[TableNum] of { Frame, Offset }
     at each Offset:       a run of variable-size records, each opening with
                           a short FuncNo, dispatched through SceneEffectFunc[]
                           until FuncNo 0 (SceneEffectReqDataEnd) ends the run
     at VibrationDataOffset: SCENE_EFFECT_VIBRATION2[] while FuncNo == 15

   PORT DEVIATIONS, all flagged at the site:
   - The ROM's variadic `SetEffects(id, fl, ...)` calls are the typed
     SetEffects_* entry points (see effect.h for why varargs cannot survive
     the 64-bit host).  Argument order and values are the ROM's.
   - `MallocSize = ChangeDataSize + 0x20` sizes the change-data cell off the
     PS2 offsetof(SCENE_EFFECT_CTRL_DATA, ChangeData); the host spells it
     offsetof() so the 8-byte pointers keep the payload area intact.
   - The lq/sq quadword copy inlined from g3dxVu0.h is g3dxVu0CopyVector().

   Line annotations are measured from the ROM's $LM records (symbols.txt);
   statements whose only code was an inlined header accessor (SingleLinkList.h
   54/65/76, fixed_array.h 124/125) carry no number because the ROM kept none. */

#include "scene_effect.h"
#include "scene.h"
#include "fod.h"
#include "../graph3d/ctl/fixed_array.h"
#include "../graph3d/gra3d.h"
#include "../graph3d/gra3dMisc.h"
#include "../graph3d/g3dxVu0.h"
#include "../effect/effect.h"
#include "../effect/effect_scr.h"
#include "../effect/effect_sub.h"
#include "../effect/effect_oth.h"
#include "../effect/effect_torch.h"
#include "../motion/motion.h"
#include "../../common/heapctrl.h"
#include "../../common/SingleLinkList.h"
#include "../../system/os/system.h"
#include "../../system/pad/pad.h"
#include "../../debug/scn_test.h"
#include "sdk/libvu0.h"

#include <stddef.h>
#include <stdio.h>

/* --------------------------------------------------------------------------
 *  Request records, one per SceneEffectFunc[] slot (types verbatim from the
 *  prototype's debug info).  Every record opens with its own FuncNo so a
 *  handler can step the stream cursor past itself.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0xc */
{
    int   TableNum;                                 /* 0x0 */
    u_int TimingTableOffset;                        /* 0x4 */
    u_int VibrationDataOffset;                      /* 0x8 */
} SCENE_EFFECT_HEADER;

typedef struct                          /* 0x8 */
{
    int Frame;                                      /* 0x0 */
    int Offset;                                     /* 0x4 */
} SCENE_EFFECT_TABLE;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short EffectId;                                 /* 0x2 */
} SCENE_EFFECT_OFF;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short Type;                                     /* 0x2 */
} SCENE_EFFECT_FOCUSDEPTH;

typedef struct                          /* 0x14 */
{
    short FuncNo;                                   /* 0x00 */
    short Type;                                     /* 0x02 */
    short StartSpeed;                               /* 0x04 */
    short EndSpeed;                                 /* 0x06 */
    short StartAlpha;                               /* 0x08 */
    short EndAlpha;                                 /* 0x0a */
    short AlphaMax;                                 /* 0x0c */
    short ColorMax;                                 /* 0x0e */
    short Time;                                     /* 0x10 */
    short Pad;                                      /* 0x12 */
} SCENE_EFFECT_DITHER;

typedef struct                          /* 0x14 */
{
    short FuncNo;                                   /* 0x00 */
    short Type;                                     /* 0x02 */
    short StartAlpha;                               /* 0x04 */
    short EndAlpha;                                 /* 0x06 */
    short StartScale;                               /* 0x08 */
    short EndScale;                                 /* 0x0a */
    short StartRot;                                 /* 0x0c */
    short EndRot;                                   /* 0x0e */
    short Time;                                     /* 0x10 */
    short Pad;                                      /* 0x12 */
} SCENE_EFFECT_BLUR;

typedef struct                          /* 0xc */
{
    short FuncNo;                                   /* 0x0 */
    short Type;                                     /* 0x2 */
    short StartRate;                                /* 0x4 */
    short EndRate;                                  /* 0x6 */
    short Time;                                     /* 0x8 */
    short Pad;                                      /* 0xa */
} SCENE_EFFECT_DEFORM;

typedef struct                          /* 0x8 */
{
    short FuncNo;                                   /* 0x0 */
    short StartRate;                                /* 0x2 */
    short EndRate;                                  /* 0x4 */
    short Time;                                     /* 0x6 */
} SCENE_EFFECT_FOCUS;

typedef struct                          /* 0x10 */
{
    short FuncNo;                                   /* 0x0 */
    short Type;                                     /* 0x2 */
    short StartColor;                               /* 0x4 */
    short EndColor;                                 /* 0x6 */
    short StartAlpha;                               /* 0x8 */
    short EndAlpha;                                 /* 0xa */
    short Time;                                     /* 0xc */
    short Pad;                                      /* 0xe */
} SCENE_EFFECT_CONTRAST;

typedef struct                          /* 0x10 */
{
    short FuncNo;                                   /* 0x0 */
    short StartColor;                               /* 0x2 */
    short EndColor;                                 /* 0x4 */
    short StartAlpha;                               /* 0x6 */
    short EndAlpha;                                 /* 0x8 */
    short StartAlpha2;                              /* 0xa */
    short EndAlpha2;                                /* 0xc */
    short Time;                                     /* 0xe */
} SCENE_EFFECT_NEGA;

typedef struct                          /* 0x8 */
{
    short FuncNo;                                   /* 0x0 */
    short StartAlpha;                               /* 0x2 */
    short EndAlpha;                                 /* 0x4 */
    short Time;                                     /* 0x6 */
} SCENE_EFFECT_FADEFRAME;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short Frame;                                    /* 0x2 */
} SCENE_EFFECT_OVERLAP;

typedef struct                          /* 0xc */
{
    short FuncNo;                                   /* 0x0 */
    short ModelType;                                /* 0x2 */
    short ModelId;                                  /* 0x4 */
    short StartAlpha;                               /* 0x6 */
    short EndAlpha;                                 /* 0x8 */
    short Frame;                                    /* 0xa */
} SCENE_EFFECT_MODELFADE;

typedef struct                          /* 0x24 */
{
    short FuncNo;                                   /* 0x00 */
    short ModelId;                                  /* 0x02 */
    short Type;                                     /* 0x04 */
    short StartScaleX;                              /* 0x06 */
    short EndScaleX;                                /* 0x08 */
    short StartScaleY;                              /* 0x0a */
    short EndScaleY;                                /* 0x0c */
    short StartAlpha;                               /* 0x0e */
    short EndAlpha;                                 /* 0x10 */
    short StartWaveRate;                            /* 0x12 */
    short EndWaveRate;                              /* 0x14 */
    short StartTexRate;                             /* 0x16 */
    short EndTexRate;                               /* 0x18 */
    short StartWaveSpeed;                           /* 0x1a */
    short EndWaveSpeed;                             /* 0x1c */
    short Distance;                                 /* 0x1e */
    short Time;                                     /* 0x20 */
    short Pad;                                      /* 0x22 */
} SCENE_EFFECT_PDEFORM;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short Pad;                                      /* 0x2 */
} SCENE_EFFECT_LENZ_FLARE;

typedef struct                          /* 0x14 */
{
    short FuncNo;                                   /* 0x00 */
    short ModelId;                                  /* 0x02 */
    short Size;                                     /* 0x04 */
    short AlphaRate;                                /* 0x06 */
    short R;                                        /* 0x08 */
    short G;                                        /* 0x0a */
    short B;                                        /* 0x0c */
    short A;                                        /* 0x0e */
    short Adjust;                                   /* 0x10 */
    short Pad;                                      /* 0x12 */
} SCENE_EFFECT_ENE_AURA;

typedef struct                          /* 0x10 */
{
    short FuncNo;                                   /* 0x0 */
    short ModelType;                                /* 0x2 */
    short ModelId;                                  /* 0x4 */
    short LightNo;                                  /* 0x6 */
    short R;                                        /* 0x8 */
    short G;                                        /* 0xa */
    short B;                                        /* 0xc */
    short Power;                                    /* 0xe */
} SCENE_EFFECT_POINTLIGHT;

typedef struct                          /* 0x14 */
{
    short FuncNo;                                   /* 0x00 */
    short ModelType;                                /* 0x02 */
    short ModelId;                                  /* 0x04 */
    short LightNo;                                  /* 0x06 */
    short R;                                        /* 0x08 */
    short G;                                        /* 0x0a */
    short B;                                        /* 0x0c */
    short Power;                                    /* 0x0e */
    short Cone;                                     /* 0x10 */
    short Pad;                                      /* 0x12 */
} SCENE_EFFECT_SPOTLIGHT;

typedef struct                          /* 0xc */
{
    short FuncNo;                                   /* 0x0 */
    short R;                                        /* 0x2 */
    short G;                                        /* 0x4 */
    short B;                                        /* 0x6 */
    short Power;                                    /* 0x8 */
    short Cone;                                     /* 0xa */
} SCENE_EFFECT_HANDSPOTLIGHT;

typedef struct                          /* 0x8 */
{
    short FuncNo;                                   /* 0x0 */
    short ModelId;                                  /* 0x2 */
    short TorchType;                                /* 0x4 */
    short Pad;                                      /* 0x6 */
} SCENE_EFFECT_TORCH;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short ModelId;                                  /* 0x2 */
} SCENE_EFFECT_PDEFORM_OFF;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short OnFlg;                                    /* 0x2 */
} SCENE_EFFECT_MONOCHRO_ONOFF;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short ModelId;                                  /* 0x2 */
} SCENE_EFFECT_HAZE;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short ModelId;                                  /* 0x2 */
} SCENE_EFFECT_HAZE_OFF;

typedef struct                          /* 0x8 */
{
    short FuncNo;                                   /* 0x0 */
    short InFlg;                                    /* 0x2 */
    short ColorType;                                /* 0x4 */
    short Time;                                     /* 0x6 */
} SCENE_EFFECT_FADE_IN_OUT;

typedef struct                          /* 0xc */
{
    short FuncNo;                                   /* 0x0 */
    short ColorR;                                   /* 0x2 */
    short ColorG;                                   /* 0x4 */
    short ColorB;                                   /* 0x6 */
    short Alpha;                                    /* 0x8 */
    short Pad;                                      /* 0xa */
} SCENE_EFFECT_FADE_SCREEN;

typedef struct                          /* 0x4 */
{
    short FuncNo;                                   /* 0x0 */
    short Pad;                                      /* 0x2 */
} SCENE_EFFECT_FADE_SCREEN_OFF;

typedef struct                          /* 0xe */
{
    short FuncNo;                                   /* 0x0 */
    short StartTime;                                /* 0x2 */
    short EndTime;                                  /* 0x4 */
    short BlankTime;                                /* 0x6 */
    short VibrateTime;                              /* 0x8 */
    short Power;                                    /* 0xa */
    short ActuaterNo;                               /* 0xc */
} SCENE_EFFECT_VIBRATION2;

/* --------------------------------------------------------------------------
 *  Controller state.
 * ------------------------------------------------------------------------ */

/* One parked long-running effect.  ChangeData is the payload area the
 * request record is copied into; the ROM aligns it to a quadword at +0x20
 * (struct size 0x30).  On the host the pointer members land it at +0x20 by
 * themselves, and SceneEffectCtrlRegist() sizes the cell with offsetof(), so
 * the exact value is not load-bearing. */
struct _SCENE_EFFECT_CTRL_DATA          /* 0x30 on target */
{
    void *pRet;                                     /* 0x00 */
    int   EffectId;                                 /* 0x04 */
    int   StartFrame;                               /* 0x08 */
    int   EndFrame;                                 /* 0x0c */
    struct _SCENE_EFFECT_CTRL_DATA *pNext;          /* 0x10 */
    char  ChangeData;                               /* 0x20 */
};

typedef struct _SCENE_EFFECT_CTRL_DATA SCENE_EFFECT_CTRL_DATA;

typedef struct                          /* 0x40 */
{
    SCENE_EFFECT_CTRL_DATA *pDataTop;               /* 0x00 */
    SCENE_EFFECT_CTRL_DATA *pDataLast;              /* 0x04 */
    int   LenzFlareFlg;                             /* 0x08 */
    float SpotLightPos[4];                          /* 0x10 */
    float SpotLightRot[4];                          /* 0x20 */
    int   ExecutedFrame;                            /* 0x30 */
} SCENE_EFFECT_CTRL;

struct _SCENE_FADE_MODEL_DATA           /* 0x1c on target */
{
    int ModelType;                                  /* 0x00 */
    int ModelId;                                    /* 0x04 */
    int StartFrame;                                 /* 0x08 */
    int EndFrame;                                   /* 0x0c */
    int StartAlpha;                                 /* 0x10 */
    int EndAlpha;                                   /* 0x14 */
    struct _SCENE_FADE_MODEL_DATA *pNext;           /* 0x18 */
};

typedef struct _SCENE_FADE_MODEL_DATA SCENE_FADE_MODEL_DATA;

typedef struct                          /* 0x8 */
{
    SCENE_FADE_MODEL_DATA *pDataTop;                /* 0x0 */
    SCENE_FADE_MODEL_DATA *pDataLast;               /* 0x4 */
} SCENE_FADE_MODEL_CTRL;

typedef struct                          /* 0x50 on target (8 tail pad) */
{
    float Position[4];                              /* 0x00 */
    SCENE_EFFECT_PDEFORM Param;                     /* 0x10 */
    float WaveSpeed;                                /* 0x34 */
    float WaveRate;                                 /* 0x38 */
    float TexRate;                                  /* 0x3c */
    int   StartFrame;                               /* 0x40 */
    int   EndFrame;                                 /* 0x44 */
} SCENE_PDEFORM_DATA;

struct _SCENE_ENE_AURA_DATA             /* 0x30 on target */
{
    float Position[4];                              /* 0x00 */
    int   ModelId;                                  /* 0x10 */
    u_int Rgba;                                     /* 0x14 */
    float Size;                                     /* 0x18 */
    float AlphaRate;                                /* 0x1c */
    float Adjust;                                   /* 0x20 */
    void *pRet;                                     /* 0x24 */
    struct _SCENE_ENE_AURA_DATA *pNext;             /* 0x28 */
};

typedef struct _SCENE_ENE_AURA_DATA SCENE_ENE_AURA_DATA;

typedef struct                          /* 0x8 */
{
    SCENE_ENE_AURA_DATA *pDataTop;                  /* 0x0 */
    SCENE_ENE_AURA_DATA *pDataLast;                 /* 0x4 */
} SCENE_ENE_AURA_CTRL;

typedef struct                          /* 0x50 on target (8 tail pad) */
{
    float Position[4];                              /* 0x00 */
    float HipPosition[4];                           /* 0x10 */
    float OldHipPosition[4];                        /* 0x20 */
    float Rot[4];                                   /* 0x30 */
    int   ModelId;                                  /* 0x40 */
    void *pEffRet;                                  /* 0x44 */
} SCENE_HAZE_DATA;

struct _SCENE_VIBRATION_DATA            /* 0x18 on target */
{
    short StartFrame;                               /* 0x00 */
    short EndFrame;                                 /* 0x02 */
    short BlankTime;                                /* 0x04 */
    short VibrateTime;                              /* 0x06 */
    short Power;                                    /* 0x08 */
    short ActuaterNo;                               /* 0x0a */
    int   BlankCount;                               /* 0x0c */
    int   VibrateCount;                             /* 0x10 */
    struct _SCENE_VIBRATION_DATA *pNext;            /* 0x14 */
};

typedef struct _SCENE_VIBRATION_DATA SCENE_VIBRATION_DATA;

typedef struct                          /* 0x8 */
{
    SCENE_VIBRATION_DATA *pDataTop;                 /* 0x0 */
    SCENE_VIBRATION_DATA *pDataLast;                /* 0x4 */
} SCENE_VIBRATION_CTRL;

typedef struct                          /* 0x20 */
{
    float     Position[4];                          /* 0x00 */
    void     *pEffectRet;                           /* 0x10 */
    ANI_CTRL *pMdlAnm;                              /* 0x14 (0x18 host) */
} SCENE_EFFECT_TORCH_DATA;

/* --------------------------------------------------------------------------
 *  File statics.
 * ------------------------------------------------------------------------ */
static SCENE_EFFECT_CTRL     SceneEffectCtrl;       /* bss  4bbf70 */
static SCENE_FADE_MODEL_CTRL SceneFadeModelCtrl;    /* sbss 3f4f68 */
static SINGLE_LINK_LIST      ScenePDeformCtrl;      /* bss  4bbfb0 */
static SCENE_ENE_AURA_CTRL   SceneEneAuraCtrl;      /* sbss 3f4f70 */
static SCENE_VIBRATION_CTRL  SceneVibrationCtrl;    /* sbss 3f4f78 */
static SINGLE_LINK_LIST      SceneTorchCtrl;        /* bss  4bbfc0 */
static SINGLE_LINK_LIST      SceneHazeCtrl;         /* bss  4bbfd0 */

/* The hip -> haze anchor offset (450 units straight up). */
static const float SaeHazeOffset[4] = { 0.0f, 450.0f, 0.0f, 0.0f };    /* rdata 3c5ee0 */

static void SceneEffectCtrlInit(void);
static void SceneEffectCtrlAllDelete(void);
static SCENE_EFFECT_CTRL_DATA *SceneEffectCtrlRegist(void *pRet, int EffectId, int ChangeDataSize);
static int SceneEffectCtrlDelete(int EffectId);
static void SceneEffectReq(short *pDataTop, int Frame);
static void SceneEffectRegistEffectReq(int NowFrame);
static void SceneEffectVibrationReq(short *pDataTop, int Frame);
static void SceneEffectReqOneData(short *pData, int NowFrame);
static void *SceneEffectChangeDataPtr(const SCENE_EFFECT_CTRL_DATA *pCtrl);
static short *SceneEffectReqDataEnd(short *pData, int Frame);
static short *SceneEffectReqOff(short *pData, int Frame);
static short *SceneEffectReqFocusDepth(short *pData, int Frame);
static short *SceneEffectReqDither(short *pData, int Frame);
static void SceneEffectCallDither(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqBlur(short *pData, int Frame);
static void SceneEffectCallBlur(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqDeform(short *pData, int Frame);
static void SceneEffectCallDeform(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqFocus(short *pData, int Frame);
static void SceneEffectCallFocus(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqContrast(short *pData, int Frame);
static void SceneEffectCallContrast(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqNega(short *pData, int Frame);
static void SceneEffectCallNega(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqFadeFrame(short *pData, int Frame);
static void SceneEffectCallFadeFrame(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame);
static short *SceneEffectReqOverlap(short *pData, int Frame);
static short *SceneEffectReqModelFade(short *pData, int Frame);
static short *SceneEffectReqPDeform(short *pData, int Frame);
static short *SceneEffectReqLenzFlare(short *pData, int Frame);
static short *SceneEffectReqEneAura(short *pData, int Frame);
static short *SceneEffectReqVibration(short *pData, int Frame);
static short *SceneEffectReqPointLight(short *pData, int Frame);
static short *SceneEffectReqSpotLight(short *pData, int Frame);
static short *SceneEffectReqHandSpotLight(short *pData, int Frame);
static short *SceneEffectReqTorch(short *pData, int Frame);
static short *SceneEffectReqPDeformOff(short *pData, int Frame);
static short *SceneEffectMonochroModeOnOff(short *pData, int Frame);
static short *SceneEffectReqHaze(short *pData, int Frame);
static short *SceneEffectHazeOff(short *pData, int Frame);
static short *SceneEffectFadeInOut(short *pData, int Frame);
static short *SceneEffectReqFadeScreen(short *pData, int Frame);
static short *SceneEffectReqFadeScreenOff(short *pData, int Frame);
static void SceneEffectFadeModelCtrlInit(void);
static int SceneEffectFadeModelRegist(SCENE_EFFECT_MODELFADE *pModelFade, int Frame);
static void SceneEffectFadeModelAllDelete(void);
static void SceneEffectFadeModelCtrlExec(SCENE_CTRL *pSceneCtrl);
static void SceneEffectPDeformCtrlInit(void);
static void SceneEffectPDeformCtrlAllDelete(void);
static void SceneEffectPDeformCtrlDelete(int ModelId);
static int SceneEffectPDeformRegist(SCENE_EFFECT_PDEFORM *pPDeform, int NowFrame);
static void SceneEffectPDeformCtrlExec(int NowFrame);
static void SceneEffectLenzFlareExec(SCENE_CTRL *pSceneCtrl);
static void SceneEffectEneAuraCtrlInit(void);
static void SceneEffectEneAuraCtrlAllDelete(void);
static int SceneEffectEneAuraRegist(SCENE_EFFECT_ENE_AURA *pEneAura);
static void SceneEffectEneAuraCtrlExec(void);
static void SceneEffectHazeCtrlInit(void);
static void SceneEffectHazeCtrlDelete(int ModelId);
static void SceneEffectHazeCtrlAllDelete(void);
static int SceneEffectHazeRegist(SCENE_EFFECT_HAZE *pHaze);
static void SceneEffectHazeCtrlExec(void);
static void SceneEffectVibrationCtrlInit(void);
static void SceneEffectVibrationCtrlAllDelete(void);
static int SceneEffectVibrationRegist(SCENE_EFFECT_VIBRATION2 *pVibration, int Frame);
static void SceneEffectVibrationCtrlExec(int NowFrame);
static void SceneEffectVibrationExecPAL(int NowFrame);
static void SceneEffectVibrationCountPAL(int NowFrame);
static void SceneTorchCtrlInit(void);
static void SceneTorchCtrlRegistAndReq(SCENE_EFFECT_TORCH_DATA *pData, int TorchType);
static void SceneTorchCtrlAllDelete(void);
static void SceneTorchCtrlExec(void);

/* The FuncNo dispatch table.  Each handler returns the cursor for the next
 * record in the run; slot 0 ends the run by returning NULL. */
static short *(*const SceneEffectFunc[])(short *pData, int Frame) =    /* rdata 3c5e68 */
{
    SceneEffectReqDataEnd,              /*  0 */
    SceneEffectReqOff,                  /*  1 */
    SceneEffectReqFocusDepth,           /*  2 */
    SceneEffectReqDither,               /*  3 */
    SceneEffectReqBlur,                 /*  4 */
    SceneEffectReqDeform,               /*  5 */
    SceneEffectReqFocus,                /*  6 */
    SceneEffectReqContrast,             /*  7 */
    SceneEffectReqNega,                 /*  8 */
    SceneEffectReqFadeFrame,            /*  9 */
    SceneEffectReqOverlap,              /* 10 */
    SceneEffectReqModelFade,            /* 11 */
    SceneEffectReqPDeform,              /* 12 */
    SceneEffectReqLenzFlare,            /* 13 */
    SceneEffectReqEneAura,              /* 14 */
    SceneEffectReqVibration,            /* 15 */
    SceneEffectReqPointLight,           /* 16 */
    SceneEffectReqSpotLight,            /* 17 */
    SceneEffectReqHandSpotLight,        /* 18 */
    SceneEffectReqTorch,                /* 19 */
    SceneEffectReqPDeformOff,           /* 20 */
    SceneEffectMonochroModeOnOff,       /* 21 */
    SceneEffectReqHaze,                 /* 22 */
    SceneEffectHazeOff,                 /* 23 */
    SceneEffectFadeInOut,               /* 24 */
    SceneEffectReqFadeScreen,           /* 25 */
    SceneEffectReqFadeScreenOff,        /* 26 */
};

/* ==========================================================================
 *  Exports.
 * ======================================================================== */

/* 0x0024d0a8 */
void SceneEffectInit(void)
{
    SceneEffectCtrlInit();                                          /* 455 */
    SceneEffectFadeModelCtrlInit();                                 /* 456 */
    SceneEffectPDeformCtrlInit();                                   /* 457 */
    SceneEffectEneAuraCtrlInit();                                   /* 458 */
    SceneEffectVibrationCtrlInit();                                 /* 459 */
    SceneTorchCtrlInit();                                           /* 460 */
    SceneEffectHazeCtrlInit();                                      /* 461 */
}

/* 0x0024d0f8.  Runs the request stream for every scene frame that elapsed
 * since the last call (a PAL scene steps 1.2 frames a vsync, so the loop can
 * run twice), then the per-frame controllers. */
void SceneEffectMain(SCENE_CTRL *pSceneCtrl, u_int *pDataAddr)
{
    if (pDataAddr != NULL)                                          /* 473 */
    {
        int NowFrame = pSceneCtrl->fod_ctrl.now_frame;              /* 474 */
        int ExecutedFrame = SceneEffectCtrl.ExecutedFrame;          /* 475 */
        int LoopNum = NowFrame - ExecutedFrame;                     /* 480 */
        int i;

        for (i = ExecutedFrame + 1; i <= ExecutedFrame + LoopNum; i++)  /* 482 */
        {
            SceneEffectReq((short *)pDataAddr, i);                  /* 483 */
            SceneEffectRegistEffectReq(i);                          /* 484 */
            SceneEffectVibrationReq((short *)pDataAddr, i);         /* 485 */
            SceneEffectFadeModelCtrlExec(pSceneCtrl);               /* 486 */
            SceneEffectEneAuraCtrlExec();                           /* 487 */
            SceneEffectVibrationCtrlExec(i);                        /* 489 */
            SceneTorchCtrlExec();                                   /* 490 */
            SceneEffectHazeCtrlExec();                              /* 491 */
        }                                                           /* 492 */

        if (LoopNum != 0)                                           /* 495 */
        {
            SceneEffectPDeformCtrlExec(NowFrame);                   /* 496 */
        }
        if (SceneEffectCtrl.LenzFlareFlg != 0)                      /* 498 */
        {
            SceneEffectLenzFlareExec(pSceneCtrl);                   /* 499 */
        }

        SceneEffectCtrl.ExecutedFrame = NowFrame;                   /* 502 */
    }
}

/* 0x0024d1f8 */
void SceneEffectEnd(void)
{
    SceneEffectCtrlAllDelete();                                     /* 528 */
    SceneEffectFadeModelAllDelete();                                /* 529 */
    SceneEffectPDeformCtrlAllDelete();                              /* 530 */
    SceneEffectEneAuraCtrlAllDelete();                              /* 531 */
    SceneEffectVibrationCtrlAllDelete();                            /* 532 */
    SceneTorchCtrlAllDelete();                                      /* 533 */
    SceneEffectHazeCtrlAllDelete();                                 /* 534 */
    SetParam(0, 0, 0, 0, 0, 0);                                     /* 535 */
}

/* ==========================================================================
 *  The parked-effect list (dither / blur / deform / focus / contrast /
 *  nega / frame fade / overlap keep their request here until an OFF record
 *  or SceneEffectEnd() deletes it).
 * ======================================================================== */

/* 0x0024d260 */
static void SceneEffectCtrlInit(void)
{
    SceneEffectCtrl.pDataTop = NULL;                                /* 542 */
    SceneEffectCtrl.pDataLast = NULL;                               /* 543 */
    SceneEffectCtrl.LenzFlareFlg = 0;                               /* 544 */
    SceneEffectCtrl.SpotLightPos[0] = 0.0f;                         /* 545 */
    SceneEffectCtrl.SpotLightPos[1] = 0.0f;                         /* 545 */
    SceneEffectCtrl.SpotLightPos[2] = 0.0f;                         /* 545 */
    SceneEffectCtrl.SpotLightPos[3] = 1.0f;                         /* 545 */
    SceneEffectCtrl.SpotLightRot[0] = 0.0f;                         /* 546 */
    SceneEffectCtrl.SpotLightRot[1] = 0.0f;                         /* 546 */
    SceneEffectCtrl.SpotLightRot[2] = 0.0f;                         /* 546 */
    SceneEffectCtrl.SpotLightRot[3] = 0.0f;                         /* 546 */
    SceneEffectCtrl.ExecutedFrame = 0;                              /* 547 */
}

/* 0x0024d2a8 */
static void SceneEffectCtrlAllDelete(void)
{
    SCENE_EFFECT_CTRL_DATA *pData = SceneEffectCtrl.pDataTop;       /* 556 */
    SCENE_EFFECT_CTRL_DATA *pDataNext;

    while (pData != NULL)                                           /* 559 */
    {
        if (pData->pRet != NULL)                                    /* 560 */
        {
            ResetEffects(pData->pRet);                              /* 561 */
        }

        pDataNext = pData->pNext;
        heapCtrlFree(GetSystemHeapWrkP(), pData);                   /* 564 */
        pData = pDataNext;                                          /* 565 */
    }

    SceneEffectCtrlInit();                                          /* 569 */
}

/* 0x0024d318.  Appends a parked effect; the request record is copied into
 * the cell's ChangeData area so the per-frame Call* pass can re-read it. */
static SCENE_EFFECT_CTRL_DATA *SceneEffectCtrlRegist(void *pRet, int EffectId, int ChangeDataSize)
{
    SCENE_EFFECT_CTRL *pCtrl = &SceneEffectCtrl;                    /* 584 */
    SCENE_EFFECT_CTRL_DATA *pMalloc;
    /* PORT DEVIATION: the ROM's literal 0x20 is the PS2
     * offsetof(SCENE_EFFECT_CTRL_DATA, ChangeData). */
    int MallocSize = (int)offsetof(SCENE_EFFECT_CTRL_DATA, ChangeData) + ChangeDataSize;   /* 588 */

    pMalloc = (SCENE_EFFECT_CTRL_DATA *)SAFE_MALLOC(GetSystemHeapWrkP(), NULL, MallocSize);    /* 589 */
    if (pMalloc != NULL)                                            /* 591 */
    {
        if (pCtrl->pDataLast == NULL)                               /* 593 */
        {
            pCtrl->pDataLast = pMalloc;                             /* 594 */
            pCtrl->pDataTop = pMalloc;                              /* 595 */
        }
        else
        {
            pCtrl->pDataLast->pNext = pMalloc;
            pCtrl->pDataLast = pMalloc;                             /* 599 */
        }

        pCtrl->pDataLast->pRet = pRet;                              /* 602 */
        pCtrl->pDataLast->EffectId = EffectId;                      /* 603 */
        pCtrl->pDataLast->StartFrame = 0;                           /* 604 */
        pCtrl->pDataLast->EndFrame = 0;                             /* 605 */
        pCtrl->pDataLast->pNext = NULL;                             /* 606 */
    }

    return pMalloc;                                                 /* 608 */
}

/* 0x0024d3c0.  Unlinks and frees the first entry with the given id, then
 * rebuilds pDataLast by walking what is left.  Returns 1 on a hit, -1 if
 * nothing matched. */
static int SceneEffectCtrlDelete(int EffectId)
{
    SCENE_EFFECT_CTRL *pCtrl = &SceneEffectCtrl;                    /* 621 */
    SCENE_EFFECT_CTRL_DATA *pData = pCtrl->pDataTop;                /* 622 */
    SCENE_EFFECT_CTRL_DATA *pPreData = pData;
    int RetVal = -1;                                                /* 624 */

    while (pData != NULL)                                           /* 627 */
    {
        if (pData->EffectId == EffectId)                            /* 628 */
        {
            if (pPreData != pData)                                  /* 629 */
            {
                pPreData->pNext = pData->pNext;                     /* 630 */
            }
            else
            {
                pCtrl->pDataTop = pData->pNext;                     /* 633 */
            }
            ResetEffects(pData->pRet);                              /* 635 */
            heapCtrlFree(GetSystemHeapWrkP(), pData);               /* 636 */
            RetVal = 1;                                             /* 637 */
            break;
        }
        pPreData = pData;
        pData = pData->pNext;                                       /* 641 */
    }

    pData = pCtrl->pDataTop;                                        /* 644 */
    pCtrl->pDataLast = NULL;
    while (pData != NULL)                                           /* 646 */
    {
        pCtrl->pDataLast = pData;                                   /* 647 */
        pData = pData->pNext;                                       /* 648 */
    }

    return RetVal;                                                  /* 651 */
}

/* ==========================================================================
 *  The request stream.
 * ======================================================================== */

/* 0x0024d498.  Looks this frame up in the timing table and runs its block. */
static void SceneEffectReq(short *pDataTop, int Frame)
{
    SCENE_EFFECT_TABLE *pTable;
    int i;

    if (pDataTop != NULL)                                           /* 666 */
    {
        pTable = (SCENE_EFFECT_TABLE *)((char *)pDataTop + ((SCENE_EFFECT_HEADER *)pDataTop)->TimingTableOffset);   /* 668 */
        for (i = 0; i < ((SCENE_EFFECT_HEADER *)pDataTop)->TableNum; i++)   /* 669 */
        {
            if (pTable[i].Frame == Frame)                           /* 670 */
            {
                SceneEffectReqOneData((short *)((char *)pDataTop + pTable[i].Offset), Frame);   /* 671 */
                break;                                              /* 672 */
            }
        }                                                           /* 674 */
    }
}

/* 0x0024d4f8.  Feeds every parked interpolating effect for this frame. */
static void SceneEffectRegistEffectReq(int NowFrame)
{
    SCENE_EFFECT_CTRL_DATA *pData;

    for (pData = SceneEffectCtrl.pDataTop; pData != NULL; pData = pData->pNext)
    {
        if (pData->pRet == NULL)
        {
            if (pData->EffectId == 2)
            {
                SceneEffectCallDither(pData, NowFrame);
            }
            else if (pData->EffectId >= 3 && pData->EffectId <= 5)
            {
                SceneEffectCallBlur(pData, NowFrame);
            }
            else if (pData->EffectId == 6)
            {
                SceneEffectCallDeform(pData, NowFrame);
            }
            else if (pData->EffectId == 7)
            {
                SceneEffectCallFocus(pData, NowFrame);
            }
            else if (pData->EffectId >= 0xd && pData->EffectId <= 0xf)
            {
                SceneEffectCallContrast(pData, NowFrame);
            }
            else if (pData->EffectId == 0xc)
            {
                SceneEffectCallNega(pData, NowFrame);
            }
            else if (pData->EffectId == 9)
            {
                SceneEffectCallFadeFrame(pData, NowFrame);
            }
        }
    }
}

/* 0x0024d648.  Walks the vibration block; records due this frame are moved
 * onto the vibration controller. */
static void SceneEffectVibrationReq(short *pDataTop, int Frame)
{
    SCENE_EFFECT_VIBRATION2 *pVibData;

    if (pDataTop != NULL)                                           /* 740 */
    {
        pVibData = (SCENE_EFFECT_VIBRATION2 *)((char *)pDataTop + ((SCENE_EFFECT_HEADER *)pDataTop)->VibrationDataOffset);  /* 742 */
        while (pVibData->FuncNo == 0xf)                             /* 744 */
        {
            if (pVibData->StartTime == Frame)                       /* 745 */
            {
                SceneEffectVibrationRegist(pVibData, Frame);        /* 746 */
            }
            pVibData++;                                             /* 748 */
        }
    }
}

/* 0x0024d6c8.  Runs one frame's record run through the dispatch table.
 * There is no bounds check on FuncNo -- the ROM trusts its own data. */
static void SceneEffectReqOneData(short *pData, int NowFrame)
{
    while (pData != NULL)                                           /* 760 */
    {
        printf("SceneEffectReqOneData() --> func no : %d\n", *pData);   /* 762 */
        pData = SceneEffectFunc[*pData](pData, NowFrame);           /* 764 */
    }
}

/* 0x0024d750 */
static void *SceneEffectChangeDataPtr(const SCENE_EFFECT_CTRL_DATA *pCtrl)
{
    return (void *)&pCtrl->ChangeData;                              /* 774 */
}

/* 0x0024d758.  FuncNo 0: end of the run. */
static short *SceneEffectReqDataEnd(short *pData, int Frame)
{
    return NULL;                                                    /* 785 */
}

/* 0x0024d760.  FuncNo 1: stop a parked effect.  The record's EffectId is an
 * OFF selector, not the parked id -- note case 8's body sits above case 7's
 * in the ROM's own line numbers. */
static short *SceneEffectReqOff(short *pData, int Frame)
{
    SCENE_EFFECT_OFF *pOff = (SCENE_EFFECT_OFF *)pData;

    switch (pOff->EffectId)                                         /* 799 */
    {
      case 2:
        SceneEffectCtrlDelete(1);                                   /* 801 */
        break;                                                      /* 802 */
      case 3:
        SceneEffectCtrlDelete(2);                                   /* 804 */
        break;                                                      /* 805 */
      case 4:
        SceneEffectCtrlDelete(3);                                   /* 807 */
        SceneEffectCtrlDelete(4);                                   /* 808 */
        SceneEffectCtrlDelete(5);                                   /* 809 */
        break;                                                      /* 810 */
      case 5:
        SceneEffectCtrlDelete(6);                                   /* 812 */
        break;                                                      /* 813 */
      case 6:
        SceneEffectCtrlDelete(7);                                   /* 815 */
        break;                                                      /* 816 */
      case 8:
        SceneEffectCtrlDelete(0xc);                                 /* 818 */
        break;                                                      /* 819 */
      case 7:
        SceneEffectCtrlDelete(0xd);                                 /* 821 */
        SceneEffectCtrlDelete(0xe);                                 /* 822 */
        SceneEffectCtrlDelete(0xf);                                 /* 823 */
        break;                                                      /* 824 */
      case 9:
        SceneEffectCtrlDelete(9);                                   /* 826 */
        break;                                                      /* 827 */
      case 10:
        SceneEffectCtrlDelete(8);                                   /* 829 */
        break;                                                      /* 830 */
      case 13:
        SceneEffectCtrl.LenzFlareFlg = 0;                           /* 832 */
        break;
    }

    return (short *)(pOff + 1);                                     /* 839 */
}

/* 0x0024d870.  FuncNo 2: the depth-of-field base state. */
static short *SceneEffectReqFocusDepth(short *pData, int Frame)
{
    SCENE_EFFECT_FOCUSDEPTH *pDepth = (SCENE_EFFECT_FOCUSDEPTH *)pData;

    if (pDepth->Type != 0)                                          /* 855 */
    {
        if (pDepth->Type == 1)                                      /* 856 */
        {
            SetEffects_Z_DEP(2);                                    /* 857 */
        }
    }
    else
    {
        SceneEffectCtrlDelete(1);                                   /* 866 */
    }

    return (short *)(pDepth + 1);                                   /* 873 */
}

/* 0x0024d8c8.  FuncNo 3: park a dither ramp (parked id 2). */
static short *SceneEffectReqDither(short *pData, int Frame)
{
    SCENE_EFFECT_DITHER *pDither = (SCENE_EFFECT_DITHER *)pData;    /* 881 */
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(2);                                       /* 886 */

    if (pDither->Type != 0)                                         /* 888 */
    {
        pCtrlData = SceneEffectCtrlRegist(NULL, 2, sizeof(SCENE_EFFECT_DITHER));    /* 889 */
        if (pCtrlData != NULL)                                      /* 890 */
        {
            *(SCENE_EFFECT_DITHER *)SceneEffectChangeDataPtr(pCtrlData) /* 891 */
                = *pDither;                                         /* 892 */
            pCtrlData->StartFrame = Frame;                          /* 893 */
            pCtrlData->EndFrame = Frame + pDither->Time;            /* 894 */
        }
    }

    return (short *)(pDither + 1);                                  /* 899 */
}

/* 0x0024d988 */
static void SceneEffectCallDither(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_DITHER *pDither = (SCENE_EFFECT_DITHER *)SceneEffectChangeDataPtr(pData);  /* 907 */
    float Alpha;
    float Speed;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_DITHER) == 0)              /* 913 */
    {
        if (pDither->Time <= 0 || pData->EndFrame < NowFrame)       /* 915 */
        {
            Alpha = (float)pDither->EndAlpha;                       /* 916 */
            Speed = (float)pDither->EndSpeed;                       /* 917 */
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pDither->Time;    /* 920 */
            Alpha = (float)(pDither->EndAlpha - pDither->StartAlpha) * Progress + (float)pDither->StartAlpha;   /* 921 */
            Speed = (float)(pDither->EndSpeed - pDither->StartSpeed) * Progress + (float)pDither->StartSpeed;   /* 922 */
        }

        SetEffects_DITHER(1, pDither->Type, Alpha, Speed,
                          pDither->AlphaMax, pDither->ColorMax, 0, 0, 0);   /* 925 */
    }
}

/* 0x0024dac0.  FuncNo 4: park a blur ramp; Type 1/2/else selects parked id
 * 3/4/5 (near / back / whole). */
static short *SceneEffectReqBlur(short *pData, int Frame)
{
    SCENE_EFFECT_BLUR *pBlur = (SCENE_EFFECT_BLUR *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;
    int BlurType;

    SceneEffectCtrlDelete(3);                                       /* 939 */
    SceneEffectCtrlDelete(4);                                       /* 940 */
    SceneEffectCtrlDelete(5);                                       /* 941 */

    if (pBlur->Type != 0)                                           /* 943 */
    {
        if (pBlur->Type == 1)                                       /* 944 */
        {
            BlurType = 3;
        }
        else if (pBlur->Type == 2)
        {
            BlurType = 4;                                           /* 947 */
        }
        else
        {
            BlurType = 5;
        }

        pCtrlData = SceneEffectCtrlRegist(NULL, BlurType, sizeof(SCENE_EFFECT_BLUR));   /* 954 */
        if (pCtrlData != NULL)                                      /* 956 */
        {
            *(SCENE_EFFECT_BLUR *)SceneEffectChangeDataPtr(pCtrlData)   /* 957 */
                = *pBlur;                                           /* 958 */
            pCtrlData->StartFrame = Frame;                          /* 959 */
            pCtrlData->EndFrame = Frame + pBlur->Time;              /* 960 */
        }
    }

    return (short *)(pBlur + 1);                                    /* 965 */
}

/* 0x0024dba8.  The blur alpha stays in a static because the effect layer
 * keeps reading it through the pointer after this call returns. */
static void SceneEffectCallBlur(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_BLUR *pBlur = (SCENE_EFFECT_BLUR *)SceneEffectChangeDataPtr(pData);    /* 973 */
    static int BlurAlpha = 0;                                       /* sdata 3f3eb8 */
    int Scale;
    int Rot;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_BLUR_N) != 0)              /* 980 */
    {
        return;
    }
    if (SceneTestEffectFlgGet(SCN_DB_EFF_BLUR_B) != 0)              /* 981 */
    {
        return;
    }
    if (SceneTestEffectFlgGet(SCN_DB_EFF_BLUR_W) != 0)              /* 982 */
    {
        return;
    }

    if (pBlur->Time <= 0 || pData->EndFrame < NowFrame)             /* 984 */
    {
        BlurAlpha = pBlur->EndAlpha;                                /* 985 */
        Scale = pBlur->EndScale;                                    /* 986 */
        Rot = pBlur->EndRot;                                        /* 987 */
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pBlur->Time;  /* 990 */
        BlurAlpha = (int)((float)(pBlur->EndAlpha - pBlur->StartAlpha) * Progress + (float)pBlur->StartAlpha);  /* 991 */
        Scale = (int)((float)(pBlur->EndScale - pBlur->StartScale) * Progress + (float)pBlur->StartScale);      /* 992 */
        Rot = (int)((float)(pBlur->EndRot - pBlur->StartRot) * Progress + (float)pBlur->StartRot);              /* 993 */
    }

    SetEffects_BLUR(pData->EffectId, 1, &BlurAlpha, Scale, Rot, 320.0f, 224.0f);    /* 996 */
}

/* 0x0024dd08.  FuncNo 5: park a screen deform (parked id 6). */
static short *SceneEffectReqDeform(short *pData, int Frame)
{
    SCENE_EFFECT_DEFORM *pDeform = (SCENE_EFFECT_DEFORM *)pData;    /* 1004 */
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(6);                                       /* 1009 */

    if (pDeform->Type != 0)                                         /* 1011 */
    {
        pCtrlData = SceneEffectCtrlRegist(NULL, 6, sizeof(SCENE_EFFECT_DEFORM));    /* 1012 */
        if (pCtrlData != NULL)                                      /* 1013 */
        {
            *(SCENE_EFFECT_DEFORM *)SceneEffectChangeDataPtr(pCtrlData) /* 1014 */
                = *pDeform;                                         /* 1015 */
            pCtrlData->StartFrame = Frame;                          /* 1016 */
            pCtrlData->EndFrame = Frame + pDeform->Time;            /* 1017 */
        }
    }

    return (short *)(pDeform + 1);                                  /* 1022 */
}

/* 0x0024ddb8 */
static void SceneEffectCallDeform(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_DEFORM *pDeform = (SCENE_EFFECT_DEFORM *)SceneEffectChangeDataPtr(pData);  /* 1030 */
    int Rate;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_DEFORM) == 0)              /* 1035 */
    {
        if (pDeform->Time <= 0 || pData->EndFrame < NowFrame)       /* 1037 */
        {
            Rate = pDeform->EndRate;                                /* 1038 */
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pDeform->Time;    /* 1041 */
            Rate = (int)((float)(pDeform->EndRate - pDeform->StartRate) * Progress + (float)pDeform->StartRate);    /* 1042 */
        }

        SetEffects_DEFORM(1, pDeform->Type, Rate, 0, 0, 0);         /* 1045 */
    }
}

/* 0x0024de88.  FuncNo 6: park a focus ramp (parked id 7, no gate). */
static short *SceneEffectReqFocus(short *pData, int Frame)
{
    SCENE_EFFECT_FOCUS *pFocus = (SCENE_EFFECT_FOCUS *)pData;       /* 1053 */
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(7);                                       /* 1058 */

    pCtrlData = SceneEffectCtrlRegist(NULL, 7, sizeof(SCENE_EFFECT_FOCUS));     /* 1060 */
    if (pCtrlData != NULL)                                          /* 1061 */
    {
        *(SCENE_EFFECT_FOCUS *)SceneEffectChangeDataPtr(pCtrlData)  /* 1062 */
            = *pFocus;                                              /* 1063 */
        pCtrlData->StartFrame = Frame;                              /* 1064 */
        pCtrlData->EndFrame = Frame + pFocus->Time;                 /* 1065 */
    }

    return (short *)(pFocus + 1);                                   /* 1069 */
}

/* 0x0024df18 */
static void SceneEffectCallFocus(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_FOCUS *pFocus = (SCENE_EFFECT_FOCUS *)SceneEffectChangeDataPtr(pData);     /* 1077 */
    int Rate;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_FOCUS) == 0)               /* 1082 */
    {
        if (pFocus->Time <= 0 || pData->EndFrame < NowFrame)        /* 1084 */
        {
            Rate = pFocus->EndRate;                                 /* 1085 */
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pFocus->Time;     /* 1088 */
            Rate = (int)((float)(pFocus->EndRate - pFocus->StartRate) * Progress + (float)pFocus->StartRate);   /* 1089 */
        }

        SetEffects_FOCUS(1, Rate);                                  /* 1092 */
    }
}

/* 0x0024dfe8.  FuncNo 7: park a contrast filter; Type 2/3/4 selects parked
 * id 0xd/0xe/0xf, anything else parks nothing. */
static short *SceneEffectReqContrast(short *pData, int Frame)
{
    SCENE_EFFECT_CONTRAST *pContrast = (SCENE_EFFECT_CONTRAST *)pData;
    SCENE_EFFECT_CTRL_DATA *pCtrlData;
    int Type;

    SceneEffectCtrlDelete(0xd);                                     /* 1106 */
    SceneEffectCtrlDelete(0xe);                                     /* 1107 */
    SceneEffectCtrlDelete(0xf);                                     /* 1108 */

    switch (pContrast->Type)                                        /* 1110 */
    {
      case 2:  Type = 0xd;  break;                                  /* 1111 */
      case 3:  Type = 0xe;  break;                                  /* 1112 */
      case 4:  Type = 0xf;  break;                                  /* 1113 */
      default: Type = 0;    break;
    }

    if (Type != 0)                                                  /* 1119 */
    {
        pCtrlData = SceneEffectCtrlRegist(NULL, Type, sizeof(SCENE_EFFECT_CONTRAST));   /* 1120 */
        if (pCtrlData != NULL)                                      /* 1121 */
        {
            *(SCENE_EFFECT_CONTRAST *)SceneEffectChangeDataPtr(pCtrlData)   /* 1122 */
                = *pContrast;                                       /* 1123 */
            pCtrlData->StartFrame = Frame;                          /* 1124 */
            pCtrlData->EndFrame = Frame + pContrast->Time;          /* 1125 */
        }
    }

    return (short *)(pContrast + 1);                                /* 1130 */
}

/* 0x0024e0d8 */
static void SceneEffectCallContrast(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_CONTRAST *pContrast = (SCENE_EFFECT_CONTRAST *)SceneEffectChangeDataPtr(pData);    /* 1138 */
    int Color;
    int Alpha;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_CONTRAST1) != 0)           /* 1144 */
    {
        return;
    }
    if (SceneTestEffectFlgGet(SCN_DB_EFF_CONTRAST2) != 0)           /* 1145 */
    {
        return;
    }
    if (SceneTestEffectFlgGet(SCN_DB_EFF_CONTRAST3) != 0)           /* 1146 */
    {
        return;
    }

    if (pContrast->Time <= 0 || pData->EndFrame < NowFrame)         /* 1148 */
    {
        Alpha = pContrast->EndAlpha;                                /* 1150 */
        Color = pContrast->EndColor;
    }
    else
    {
        Progress = (float)(NowFrame - pData->StartFrame) / (float)pContrast->Time;      /* 1153 */
        Color = (int)((float)(pContrast->EndColor - pContrast->StartColor) * Progress + (float)pContrast->StartColor);  /* 1154 */
        Alpha = (int)((float)(pContrast->EndAlpha - pContrast->StartAlpha) * Progress + (float)pContrast->StartAlpha);  /* 1155 */
    }

    SetEffects_NCONTRAST(pData->EffectId, 1, Color, Alpha);         /* 1158 */
}

/* 0x0024e1f0.  FuncNo 8: park a nega filter (parked id 0xc, no gate). */
static short *SceneEffectReqNega(short *pData, int Frame)
{
    SCENE_EFFECT_NEGA *pNega = (SCENE_EFFECT_NEGA *)pData;          /* 1166 */
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(0xc);                                     /* 1171 */

    pCtrlData = SceneEffectCtrlRegist(NULL, 0xc, sizeof(SCENE_EFFECT_NEGA));    /* 1173 */
    if (pCtrlData != NULL)                                          /* 1174 */
    {
        *(SCENE_EFFECT_NEGA *)SceneEffectChangeDataPtr(pCtrlData)   /* 1175 */
            = *pNega;                                               /* 1176 */
        pCtrlData->StartFrame = Frame;                              /* 1177 */
        pCtrlData->EndFrame = Frame + pNega->Time;                  /* 1178 */
    }

    return (short *)(pNega + 1);                                    /* 1182 */
}

/* 0x0024e290.  The second alpha travels through a static byte the effect
 * layer keeps a pointer to (same idea as CallBlur's alpha). */
static void SceneEffectCallNega(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_NEGA *pNega = (SCENE_EFFECT_NEGA *)SceneEffectChangeDataPtr(pData);    /* 1190 */
    static u_char nalp = 0;                                         /* sdata 3f3ebc */
    int Color;
    int Alpha;
    int Alpha2;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_NEGA) == 0)                /* 1198 */
    {
        if (pNega->Time <= 0 || pData->EndFrame < NowFrame)         /* 1200 */
        {
            Color = pNega->EndColor;                                /* 1201 */
            Alpha = pNega->EndAlpha;
            Alpha2 = pNega->EndAlpha2;                              /* 1203 */
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pNega->Time;      /* 1206 */
            Color = (int)((float)(pNega->EndColor - pNega->StartColor) * Progress + (float)pNega->StartColor);      /* 1207 */
            Alpha = (int)((float)(pNega->EndAlpha - pNega->StartAlpha) * Progress + (float)pNega->StartAlpha);      /* 1208 */
            Alpha2 = (int)((float)(pNega->EndAlpha2 - pNega->StartAlpha2) * Progress + (float)pNega->StartAlpha2);  /* 1209 */
        }

        nalp = (u_char)Alpha2;
        SetEffects_NEGA(1, Color, Alpha, 0, 0, 0, &nalp);           /* 1213 */
    }
}

/* 0x0024e3c0.  FuncNo 9: park a frame fade (parked id 9, no gate). */
static short *SceneEffectReqFadeFrame(short *pData, int Frame)
{
    SCENE_EFFECT_FADEFRAME *pFadeFrame = (SCENE_EFFECT_FADEFRAME *)pData;   /* 1221 */
    SCENE_EFFECT_CTRL_DATA *pCtrlData;

    SceneEffectCtrlDelete(9);                                       /* 1226 */

    pCtrlData = SceneEffectCtrlRegist(NULL, 9, sizeof(SCENE_EFFECT_FADEFRAME));     /* 1228 */
    if (pCtrlData != NULL)                                          /* 1229 */
    {
        *(SCENE_EFFECT_FADEFRAME *)SceneEffectChangeDataPtr(pCtrlData)  /* 1230 */
            = *pFadeFrame;                                          /* 1231 */
        pCtrlData->StartFrame = Frame;                              /* 1232 */
        pCtrlData->EndFrame = Frame + pFadeFrame->Time;             /* 1233 */
    }

    return (short *)(pFadeFrame + 1);                               /* 1237 */
}

/* 0x0024e450 */
static void SceneEffectCallFadeFrame(SCENE_EFFECT_CTRL_DATA *pData, int NowFrame)
{
    SCENE_EFFECT_FADEFRAME *pFadeFrame = (SCENE_EFFECT_FADEFRAME *)SceneEffectChangeDataPtr(pData);     /* 1245 */
    int Alpha;
    float Progress;

    if (SceneTestEffectFlgGet(SCN_DB_EFF_FADE_FRAME) == 0)          /* 1250 */
    {
        if (pFadeFrame->Time <= 0 || pData->EndFrame < NowFrame)    /* 1252 */
        {
            Alpha = pFadeFrame->EndAlpha;                           /* 1253 */
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pFadeFrame->Time;     /* 1256 */
            Alpha = (int)((float)(pFadeFrame->EndAlpha - pFadeFrame->StartAlpha) * Progress + (float)pFadeFrame->StartAlpha);   /* 1257 */
        }

        SetEffects_FADEFRAME(1, Alpha, 0);                          /* 1260 */
    }
}

/* 0x0024e520.  FuncNo 10: a cross fade, fired immediately and parked only so
 * an OFF record can reset it. */
static short *SceneEffectReqOverlap(short *pData, int Frame)
{
    SceneEffectCtrlRegist(SetEffects_OVERLAP(2, ((SCENE_EFFECT_OVERLAP *)pData)->Frame),    /* 1276 */
                          8, 0);                                    /* 1277 */

    return pData + 2;                                               /* 1280 */
}

/* 0x0024e568.  FuncNo 11. */
static short *SceneEffectReqModelFade(short *pData, int Frame)
{
    SceneEffectFadeModelRegist((SCENE_EFFECT_MODELFADE *)pData, Frame);     /* 1290 */

    return pData + 6;                                               /* 1293 */
}

/* 0x0024e590.  FuncNo 12. */
static short *SceneEffectReqPDeform(short *pData, int Frame)
{
    SceneEffectPDeformRegist((SCENE_EFFECT_PDEFORM *)pData, Frame);     /* 1303 */

    return pData + 18;                                              /* 1306 */
}

/* 0x0024e5b8.  FuncNo 13. */
static short *SceneEffectReqLenzFlare(short *pData, int Frame)
{
    SceneEffectCtrl.LenzFlareFlg = 1;                               /* 1321 */

    return pData + 2;                                               /* 1324 */
}

/* 0x0024e5d0.  FuncNo 14. */
static short *SceneEffectReqEneAura(short *pData, int Frame)
{
    SceneEffectEneAuraRegist((SCENE_EFFECT_ENE_AURA *)pData);       /* 1334 */

    return pData + 10;                                              /* 1337 */
}

/* 0x0024e5f8.  FuncNo 15: nothing to do here -- vibration records live in
 * their own block, walked by SceneEffectVibrationReq().  The record skipped
 * here is the six-short SCENE_EFFECT_VIBRATION, not the seven-short
 * SCENE_EFFECT_VIBRATION2 that block uses. */
static short *SceneEffectReqVibration(short *pData, int Frame)
{
    return pData + 6;                                               /* 1351 */
}

/* 0x0024e600.  FuncNo 16: colours arrive in thousandths. */
static short *SceneEffectReqPointLight(short *pData, int Frame)
{
    SceneChangeLightParameter(3,
                              ((SCENE_EFFECT_POINTLIGHT *)pData)->LightNo,
                              ((SCENE_EFFECT_POINTLIGHT *)pData)->ModelType,
                              ((SCENE_EFFECT_POINTLIGHT *)pData)->ModelId,
                              (float)((SCENE_EFFECT_POINTLIGHT *)pData)->R / 1000.0f,
                              (float)((SCENE_EFFECT_POINTLIGHT *)pData)->G / 1000.0f,
                              (float)((SCENE_EFFECT_POINTLIGHT *)pData)->B / 1000.0f,
                              (float)((SCENE_EFFECT_POINTLIGHT *)pData)->Power,
                              0.0f);                                /* 1373 */

    return pData + 8;                                               /* 1376 */
}

/* 0x0024e698.  FuncNo 17: the cone arrives in tenths of a degree. */
static short *SceneEffectReqSpotLight(short *pData, int Frame)
{
    SceneChangeLightParameter(2,
                              ((SCENE_EFFECT_SPOTLIGHT *)pData)->LightNo,
                              ((SCENE_EFFECT_SPOTLIGHT *)pData)->ModelType,
                              ((SCENE_EFFECT_SPOTLIGHT *)pData)->ModelId,
                              (float)((SCENE_EFFECT_SPOTLIGHT *)pData)->R / 1000.0f,
                              (float)((SCENE_EFFECT_SPOTLIGHT *)pData)->G / 1000.0f,
                              (float)((SCENE_EFFECT_SPOTLIGHT *)pData)->B / 1000.0f,
                              (float)((SCENE_EFFECT_SPOTLIGHT *)pData)->Power,
                              (float)((SCENE_EFFECT_SPOTLIGHT *)pData)->Cone / 10.0f);  /* 1400 */

    return pData + 10;                                              /* 1403 */
}

/* 0x0024e750.  FuncNo 18. */
static short *SceneEffectReqHandSpotLight(short *pData, int Frame)
{
    SceneChangeHandSpotLightParameter((float)((SCENE_EFFECT_HANDSPOTLIGHT *)pData)->R / 1000.0f,
                                      (float)((SCENE_EFFECT_HANDSPOTLIGHT *)pData)->G / 1000.0f,
                                      (float)((SCENE_EFFECT_HANDSPOTLIGHT *)pData)->B / 1000.0f,
                                      (float)((SCENE_EFFECT_HANDSPOTLIGHT *)pData)->Power,
                                      (float)((SCENE_EFFECT_HANDSPOTLIGHT *)pData)->Cone / 10.0f);  /* 1422 */

    return pData + 6;                                               /* 1425 */
}

/* 0x0024e7f8.  FuncNo 19: light a torch at the model's raised-hand bone. */
static short *SceneEffectReqTorch(short *pData, int Frame)
{
    SCENE_EFFECT_TORCH *pTorch = (SCENE_EFFECT_TORCH *)pData;
    SCENE_EFFECT_TORCH_DATA TorchData;

    TorchData.pMdlAnm = SceneGetAniCtrl(pTorch->ModelId);           /* 1436 */

    if (TorchData.pMdlAnm != NULL)                                  /* 1438 */
    {
        motGetBukiUpPos(TorchData.Position, TorchData.pMdlAnm);     /* 1439 */
        SceneTorchCtrlRegistAndReq(&TorchData, pTorch->TorchType);  /* 1440 */
    }

    return (short *)(pTorch + 1);                                   /* 1444 */
}

/* 0x0024e848.  FuncNo 20. */
static short *SceneEffectReqPDeformOff(short *pData, int Frame)
{
    SceneEffectPDeformCtrlDelete(((SCENE_EFFECT_PDEFORM_OFF *)pData)->ModelId);     /* 1459 */

    return pData + 2;                                               /* 1462 */
}

/* 0x0024e878.  FuncNo 21: flip monochrome mode and re-prelight whatever
 * rooms are resident so the baked colours match. */
static short *SceneEffectMonochroModeOnOff(short *pData, int Frame)
{
    SCENE_EFFECT_MONOCHRO_ONOFF *pMonoOnOff = (SCENE_EFFECT_MONOCHRO_ONOFF *)pData;
    int RoomNo;
    int SubRoomNo;

    RoomNo = SceneRoomNoGet();                                      /* 1476 */
    SubRoomNo = SceneSubRoomNoGet();                                /* 1477 */

    if (pMonoOnOff->OnFlg != 0)                                     /* 1479 */
    {
        EffWrkMonochroModeSet(1);                                   /* 1480 */
        gra3dMonotoneDrawEnable(1);                                 /* 1481 */
    }
    else
    {
        EffWrkMonochroModeSet(0);                                   /* 1484 */
        gra3dMonotoneDrawEnable(0);                                 /* 1485 */
    }

    if (RoomNo != -1)                                               /* 1489 */
    {
        gra3dPrelightScene(RoomNo);                                 /* 1490 */
    }
    if (SubRoomNo != -1)                                            /* 1492 */
    {
        gra3dPrelightScene(SubRoomNo);                              /* 1493 */
    }

    return (short *)(pMonoOnOff + 1);                               /* 1497 */
}

/* 0x0024e918.  FuncNo 22. */
static short *SceneEffectReqHaze(short *pData, int Frame)
{
    SceneEffectHazeRegist((SCENE_EFFECT_HAZE *)pData);              /* 1507 */

    return pData + 2;                                               /* 1510 */
}

/* 0x0024e940.  FuncNo 23. */
static short *SceneEffectHazeOff(short *pData, int Frame)
{
    SceneEffectHazeCtrlDelete(((SCENE_EFFECT_HAZE_OFF *)pData)->ModelId);   /* 1520 */

    return pData + 2;                                               /* 1523 */
}

/* 0x0024e970.  FuncNo 24: the four screen fades. */
static short *SceneEffectFadeInOut(short *pData, int Frame)
{
    SCENE_EFFECT_FADE_IN_OUT *pFade = (SCENE_EFFECT_FADE_IN_OUT *)pData;

    if (pFade->InFlg != 0)                                          /* 1540 */
    {
        if (pFade->ColorType == 1)                                  /* 1541 */
        {
            SetWhiteIn2(pFade->Time);                               /* 1542 */
        }
        else
        {
            SetBlackIn2(pFade->Time);                               /* 1545 */
        }
    }
    else
    {
        if (pFade->ColorType == 1)                                  /* 1549 */
        {
            SetWhiteOut2(pFade->Time);                              /* 1550 */
        }
        else
        {
            SetBlackOut2(pFade->Time);                              /* 1553 */
        }
    }

    return (short *)(pFade + 1);                                    /* 1559 */
}

/* 0x0024e9f8.  FuncNo 25: an immediate flat screen tint. */
static short *SceneEffectReqFadeScreen(short *pData, int Frame)
{
    SetParam(((SCENE_EFFECT_FADE_SCREEN *)pData)->Alpha, 0,
             ((SCENE_EFFECT_FADE_SCREEN *)pData)->ColorR,
             ((SCENE_EFFECT_FADE_SCREEN *)pData)->ColorG,
             ((SCENE_EFFECT_FADE_SCREEN *)pData)->ColorB, 0);       /* 1578 */

    return pData + 6;                                               /* 1581 */
}

/* 0x0024ea38.  FuncNo 26. */
static short *SceneEffectReqFadeScreenOff(short *pData, int Frame)
{
    SetParam(0, 0, 0, 0, 0, 0);                                     /* 1596 */

    return pData + 2;                                               /* 1599 */
}

/* ==========================================================================
 *  Model fades: per-model alpha ramps applied straight onto the scene's
 *  SCN_ANM_MDL entries.
 * ======================================================================== */

/* 0x0024ea78 */
static void SceneEffectFadeModelCtrlInit(void)
{
    SceneFadeModelCtrl.pDataTop = NULL;                             /* 1610 */
    SceneFadeModelCtrl.pDataLast = NULL;                            /* 1611 */
}

/* 0x0024ea88 */
static int SceneEffectFadeModelRegist(SCENE_EFFECT_MODELFADE *pModelFade, int Frame)
{
    SCENE_FADE_MODEL_CTRL *pCtrl = &SceneFadeModelCtrl;
    SCENE_FADE_MODEL_DATA *pMalloc;

    pMalloc = (SCENE_FADE_MODEL_DATA *)SAFE_MALLOC(GetSystemHeapWrkP(), NULL, sizeof(SCENE_FADE_MODEL_DATA));   /* 1628 */
    if (pMalloc == NULL)                                            /* 1630 */
    {
        return -1;
    }

    if (pCtrl->pDataLast != NULL)                                   /* 1632 */
    {
        pCtrl->pDataLast->pNext = pMalloc;
    }
    else
    {
        pCtrl->pDataTop = pMalloc;                                  /* 1634 */
    }
    pCtrl->pDataLast = pMalloc;                                     /* 1638 */

    pCtrl->pDataLast->ModelType = pModelFade->ModelType;            /* 1641 */
    pCtrl->pDataLast->ModelId = pModelFade->ModelId;                /* 1642 */
    pCtrl->pDataLast->StartFrame = Frame;                           /* 1643 */
    pCtrl->pDataLast->EndFrame = Frame + pModelFade->Frame;         /* 1644 */
    pCtrl->pDataLast->StartAlpha = pModelFade->StartAlpha;          /* 1645 */
    pCtrl->pDataLast->EndAlpha = pModelFade->EndAlpha;              /* 1646 */
    pCtrl->pDataLast->pNext = NULL;                                 /* 1647 */

    return 0;                                                       /* 1649 */
}

/* 0x0024eb28 */
static void SceneEffectFadeModelAllDelete(void)
{
    SCENE_FADE_MODEL_DATA *pData;
    SCENE_FADE_MODEL_DATA *pDataNext;

    pData = SceneFadeModelCtrl.pDataTop;                            /* 1661 */
    while (pData != NULL)                                           /* 1662 */
    {
        pDataNext = pData->pNext;
        /* The null test around the free is the ROM's own. */
        if (pData != NULL)                                          /* 1664 */
        {
            heapCtrlFree(GetSystemHeapWrkP(), pData);
        }
        pData = pDataNext;                                          /* 1666 */
    }

    SceneEffectFadeModelCtrlInit();                                 /* 1669 */
}

/* 0x0024eb88.  Applies every live fade onto the matching models' mdl_alpha;
 * the 0x80000000 bit marks the alpha as fade-driven for the drawer. */
static void SceneEffectFadeModelCtrlExec(SCENE_CTRL *pSceneCtrl)
{
    SCENE_FADE_MODEL_DATA *pFadeData;
    SCN_ANM_MDL *pAnmMdl;
    int ModelNum;
    int NowFrame = pSceneCtrl->fod_ctrl.now_frame;                  /* 1681 */
    int i;
    int DivFrame;

    pFadeData = SceneFadeModelCtrl.pDataTop;                        /* 1684 */

    while (pFadeData != NULL)                                       /* 1686 */
    {
        switch (pFadeData->ModelType)                               /* 1687 */
        {
          case 0:
            ModelNum = pSceneCtrl->man_mdl_num;
            pAnmMdl = &pSceneCtrl->man_mdl[0];                      /* 1691 */
            break;
          case 1:
            ModelNum = pSceneCtrl->furn_num;
            pAnmMdl = &pSceneCtrl->furn_mdl[0];                     /* 1695 */
            break;
          case 2:
            ModelNum = pSceneCtrl->door_num;
            pAnmMdl = &pSceneCtrl->door_mdl[0];                     /* 1699 */
            break;
          case 3:
            ModelNum = pSceneCtrl->item_num;
            pAnmMdl = &pSceneCtrl->item_mdl[0];                     /* 1703 */
            break;
          default:
            ModelNum = 0;
            pAnmMdl = NULL;
            break;
        }

        if (pAnmMdl != NULL)                                        /* 1709 */
        {
            for (i = ModelNum; i > 0; )                             /* 1711 */
            {
                if (pFadeData->ModelId == (int)pAnmMdl->mdl_no)     /* 1712 */
                {
                    if (NowFrame < pFadeData->EndFrame)             /* 1713 */
                    {
                        DivFrame = pFadeData->EndFrame - pFadeData->StartFrame;     /* 1717 */

                        if (DivFrame != 0)                          /* 1719 */
                        {
                            pAnmMdl->mdl_alpha = (pFadeData->EndAlpha - pFadeData->StartAlpha) * (NowFrame - pFadeData->StartFrame) / DivFrame + pFadeData->StartAlpha;     /* 1720 */
                        }
                        else
                        {
                            pAnmMdl->mdl_alpha = pFadeData->EndAlpha;   /* 1723 */
                        }
                    }
                    else
                    {
                        pAnmMdl->mdl_alpha = pFadeData->EndAlpha;
                    }

                    pAnmMdl->mdl_alpha |= 0x80000000;               /* 1726 */
                }
                i--;                                                /* 1728 */
                pAnmMdl++;                                          /* 1729 */
            }
        }

        pFadeData = pFadeData->pNext;                               /* 1730 */
    }
}

/* ==========================================================================
 *  Particle deforms: full-screen heat ripples anchored on a model.
 * ======================================================================== */

/* 0x0024ed10 */
static void SceneEffectPDeformCtrlInit(void)
{
    SingleLinkListInit(&ScenePDeformCtrl, sizeof(SCENE_PDEFORM_DATA));  /* 1742 */
}

/* 0x0024ed38 */
static void SceneEffectPDeformCtrlAllDelete(void)
{
    SingleLinkListAllCellFree(&ScenePDeformCtrl);                   /* 1750 */
}

/* 0x0024ed58 */
static void SceneEffectPDeformCtrlDelete(int ModelId)
{
    SLL_CELL *pCell;
    SINGLE_LINK_LIST *pSLL = &ScenePDeformCtrl;

    pCell = SingleLinkListBeginCell(pSLL);
    while (pCell != NULL)                                           /* 1760 */
    {
        SLL_CELL *pCellNext = SingleLinkListNextCell(pCell);
        SCENE_PDEFORM_DATA *pData = (SCENE_PDEFORM_DATA *)SingleLinkListCellBodyPtr(pCell);     /* 1762 */

        if (pData->Param.ModelId == ModelId)                        /* 1764 */
        {
            SingleLinkListRemove(pSLL, pCell);                      /* 1765 */
        }

        pCell = pCellNext;                                          /* 1768 */
    }
}

/* 0x0024edf8.  The cell is reserved with whatever is on the stack and then
 * filled in place -- the ROM passes TmpData uninitialised on purpose. */
static int SceneEffectPDeformRegist(SCENE_EFFECT_PDEFORM *pPDeform, int NowFrame)
{
    SCENE_PDEFORM_DATA TmpData;
    SLL_CELL *pCell;
    int RetVal = -1;
    SCENE_PDEFORM_DATA *pData;

    pCell = SingleLinkListAddEnd(&ScenePDeformCtrl, &TmpData);      /* 1787 */
    if (pCell != NULL)                                              /* 1789 */
    {
        pData = (SCENE_PDEFORM_DATA *)SingleLinkListCellBodyPtr(pCell);     /* 1790 */
        RetVal = 0;

        pData->Position[0] = 0.0f;                                  /* 1792 */
        pData->Position[1] = 0.0f;                                  /* 1792 */
        pData->Position[2] = 0.0f;                                  /* 1792 */
        pData->Position[3] = 1.0f;                                  /* 1792 */
        pData->Param = *pPDeform;                                   /* 1793 */
        pData->WaveSpeed = 0.0f;                                    /* 1794 */
        pData->WaveRate = 0.0f;                                     /* 1795 */
        pData->TexRate = 1.0f;                                      /* 1796 */
        pData->StartFrame = NowFrame;                               /* 1797 */
        pData->EndFrame = NowFrame + pData->Param.Time;             /* 1798 */
    }

    return RetVal;                                                  /* 1803 */
}

/* 0x0024eed8.  Steps every ripple's five ramps (table values are in
 * hundredths) and re-feeds the PDEFORM effect. */
static void SceneEffectPDeformCtrlExec(int NowFrame)
{
    int Alpha;
    float ScaleX;
    float ScaleY;
    float Progress;
    SLL_CELL *pCell;
    SINGLE_LINK_LIST *pSLL = &ScenePDeformCtrl;
    SCENE_PDEFORM_DATA *pData;

    pCell = SingleLinkListBeginCell(pSLL);
    while (pCell != NULL)                                           /* 1816 */
    {
        pData = (SCENE_PDEFORM_DATA *)SingleLinkListCellBodyPtr(pCell);     /* 1817 */

        if (pData->Param.Time <= 0 || pData->EndFrame < NowFrame)   /* 1819 */
        {
            Alpha = pData->Param.EndAlpha;                          /* 1820 */
            ScaleX = (float)pData->Param.EndScaleX / 100.0f;        /* 1821 */
            ScaleY = (float)pData->Param.EndScaleY / 100.0f;        /* 1822 */
            pData->WaveSpeed = (float)pData->Param.EndWaveSpeed / 100.0f;   /* 1823 */
            pData->WaveRate = (float)pData->Param.EndWaveRate / 100.0f;     /* 1824 */
            pData->TexRate = (float)pData->Param.EndTexRate / 100.0f;       /* 1825 */
        }
        else
        {
            Progress = (float)(NowFrame - pData->StartFrame) / (float)pData->Param.Time;    /* 1828 */
            Alpha = (int)((float)(pData->Param.EndAlpha - pData->Param.StartAlpha) * Progress + (float)pData->Param.StartAlpha);    /* 1829 */
            ScaleX = ((float)(pData->Param.EndScaleX - pData->Param.StartScaleX) * Progress + (float)pData->Param.StartScaleX) / 100.0f;    /* 1830 */
            ScaleY = ((float)(pData->Param.EndScaleY - pData->Param.StartScaleY) * Progress + (float)pData->Param.StartScaleY) / 100.0f;    /* 1831 */
            pData->WaveSpeed = ((float)(pData->Param.EndWaveSpeed - pData->Param.StartWaveSpeed) * Progress + (float)pData->Param.StartWaveSpeed) / 100.0f;     /* 1832 */
            pData->WaveRate = ((float)(pData->Param.EndWaveRate - pData->Param.StartWaveRate) * Progress + (float)pData->Param.StartWaveRate) / 100.0f;         /* 1833 */
            pData->TexRate = ((float)(pData->Param.EndTexRate - pData->Param.StartTexRate) * Progress + (float)pData->Param.StartTexRate) / 100.0f;             /* 1834 */
        }

        SceneGetModelPDeformPos(pData->Position, SceneManModelNoChange(pData->Param.ModelId), (float)pData->Param.Distance);    /* 1836 */

        /* The ROM reads Type as a byte (lbu) -- the cast is the source's. */
        SetEffects_PDEFORM(1, (u_char)pData->Param.Type, Alpha, ScaleX, ScaleY,
                           pData->Position, 0, 0, 0, NULL,
                           &pData->WaveSpeed, &pData->WaveRate, &pData->TexRate,
                           0x80, 0x80, 0x80);                       /* 1848 */

        pCell = SingleLinkListNextCell(pCell);
    }
}

/* 0x0024f218.  The lens flare hangs off the player's lantern: model slot 0's
 * accessory light position, looked at from bone 6. */
static void SceneEffectLenzFlareExec(SCENE_CTRL *pSceneCtrl)
{
    ANI_CTRL *pAniCtrl = NULL;
    int i;
    float LocalWorld[4][4];
    float TmpVec[4];
    float TmpFloat;

    for (i = 0; i < pSceneCtrl->man_mdl_num; i++)                   /* 1866 */
    {
        if (pSceneCtrl->man_mdl[i].mdl_no == 0)
        {
            pAniCtrl = pSceneCtrl->man_mdl[i].pMdlAnm;
            break;                                                  /* 1870 */
        }
    }

    if (pAniCtrl != NULL)                                           /* 1874 */
    {
        GetPlyrAcsLightPos(SceneEffectCtrl.SpotLightPos, pAniCtrl);     /* 1879 */
        motGetLocalWorldMatrix(LocalWorld, pAniCtrl->mpk_p, 6);     /* 1880 */
        sceVu0SubVector(TmpVec, SceneEffectCtrl.SpotLightPos, LocalWorld[3]);   /* 1881 */
        Vector2Rot(TmpVec, &TmpFloat, &SceneEffectCtrl.SpotLightRot[1]);        /* 1882 */

        SetEffects_RENZFLARE(1, 4, SceneEffectCtrl.SpotLightPos, SceneEffectCtrl.SpotLightRot);     /* 1884 */
    }
}

/* ==========================================================================
 *  Enemy auras.
 * ======================================================================== */

/* 0x0024f308 */
static void SceneEffectEneAuraCtrlInit(void)
{
    SceneEneAuraCtrl.pDataTop = NULL;                               /* 1896 */
    SceneEneAuraCtrl.pDataLast = NULL;                              /* 1897 */
}

/* 0x0024f318 */
static void SceneEffectEneAuraCtrlAllDelete(void)
{
    SCENE_ENE_AURA_DATA *pData;
    SCENE_ENE_AURA_DATA *pDataNext;

    pData = SceneEneAuraCtrl.pDataTop;                              /* 1906 */

    while (pData != NULL)                                           /* 1909 */
    {
        if (pData->pRet != NULL)                                    /* 1910 */
        {
            ResetEffects(pData->pRet);                              /* 1911 */
        }

        pDataNext = pData->pNext;
        /* The null test around the free is the ROM's own. */
        if (pData != NULL)                                          /* 1914 */
        {
            heapCtrlFree(GetSystemHeapWrkP(), pData);
        }
        pData = pDataNext;                                          /* 1915 */
    }

    SceneEffectEneAuraCtrlInit();                                   /* 1919 */
}

/* 0x0024f390.  Parks an ENEFIRE aura on a model; size/colour/rate all live
 * in the cell so the effect layer can keep reading them through pointers. */
static int SceneEffectEneAuraRegist(SCENE_EFFECT_ENE_AURA *pEneAura)
{
    SCENE_ENE_AURA_CTRL *pCtrl = &SceneEneAuraCtrl;                 /* 1933 */
    SCENE_ENE_AURA_DATA *pMalloc;

    pMalloc = (SCENE_ENE_AURA_DATA *)SAFE_MALLOC(GetSystemHeapWrkP(), NULL, sizeof(SCENE_ENE_AURA_DATA));   /* 1936 */
    if (pMalloc == NULL)                                            /* 1938 */
    {
        return -1;
    }

    if (pCtrl->pDataLast != NULL)                                   /* 1940 */
    {
        pCtrl->pDataLast->pNext = pMalloc;
    }
    else
    {
        pCtrl->pDataTop = pMalloc;                                  /* 1942 */
    }
    pCtrl->pDataLast = pMalloc;                                     /* 1946 */

    SceneGetModelPDeformPos(pCtrl->pDataLast->Position, SceneManModelNoChange(pEneAura->ModelId), 0.0f);    /* 1949 */
    pCtrl->pDataLast->Position[1] += (float)pEneAura->Adjust;       /* 1950 */
    pCtrl->pDataLast->ModelId = pEneAura->ModelId;                  /* 1951 */
    pCtrl->pDataLast->Rgba = ((u_char)pEneAura->R << 24) | ((u_char)pEneAura->G << 16) | ((u_char)pEneAura->B << 8) | (u_char)pEneAura->A;  /* 1953 */
    pCtrl->pDataLast->Size = (float)pEneAura->Size;                 /* 1954 */
    pCtrl->pDataLast->AlphaRate = (float)pEneAura->AlphaRate / 100.0f;  /* 1955 */
    pCtrl->pDataLast->Adjust = (float)pEneAura->Adjust;             /* 1956 */

    pCtrl->pDataLast->pRet = SetEffects_ENEFIRE(1, 2, pCtrl->pDataLast->Position, NULL,
                                                &pCtrl->pDataLast->Rgba,
                                                &pCtrl->pDataLast->Size, 0xa0,
                                                &pCtrl->pDataLast->AlphaRate);  /* 1966 */
    pCtrl->pDataLast->pNext = NULL;                                 /* 1967 */

    return 0;                                                       /* 1969 */
}

/* 0x0024f4d0.  Re-anchors every aura on its model each frame. */
static void SceneEffectEneAuraCtrlExec(void)
{
    SCENE_ENE_AURA_DATA *pData;

    pData = SceneEneAuraCtrl.pDataTop;                              /* 1980 */

    while (pData != NULL)                                           /* 1982 */
    {
        SceneGetModelPDeformPos(pData->Position, SceneManModelNoChange(pData->ModelId), 0.0f);  /* 1983 */
        pData->Position[1] += pData->Adjust;                        /* 1984 */

        pData = pData->pNext;                                       /* 1986 */
    }
}

/* ==========================================================================
 *  Heat haze.
 * ======================================================================== */

/* 0x0024f530 */
static void SceneEffectHazeCtrlInit(void)
{
    SingleLinkListInit(&SceneHazeCtrl, sizeof(SCENE_HAZE_DATA));    /* 1999 */
}

/* 0x0024f558 */
static void SceneEffectHazeCtrlDelete(int ModelId)
{
    SLL_CELL *pCell;
    SINGLE_LINK_LIST *pSLL = &SceneHazeCtrl;

    pCell = SingleLinkListBeginCell(pSLL);                          /* 2009 */
    while (pCell != NULL)
    {
        SLL_CELL *pCellNext = SingleLinkListNextCell(pCell);
        SCENE_HAZE_DATA *pHaze = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);   /* 2011 */

        if (pHaze->ModelId == ModelId)                              /* 2013 */
        {
            EffectSaeHazeCut(pHaze->pEffRet);                       /* 2014 */
            SingleLinkListRemove(pSLL, pCell);                      /* 2015 */
            break;                                                  /* 2016 */
        }

        pCell = pCellNext;                                          /* 2018 */
    }
}

/* 0x0024f5e8 */
static void SceneEffectHazeCtrlAllDelete(void)
{
    SLL_CELL *pCell;
    SINGLE_LINK_LIST *pSLL = &SceneHazeCtrl;

    pCell = SingleLinkListBeginCell(pSLL);
    while (pCell != NULL)                                           /* 2029 */
    {
        SLL_CELL *pCellNext = SingleLinkListNextCell(pCell);
        SCENE_HAZE_DATA *pHaze = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);   /* 2031 */

        EffectSaeHazeCut(pHaze->pEffRet);                           /* 2033 */
        SingleLinkListRemove(pSLL, pCell);                          /* 2034 */
        pCell = pCellNext;
    }                                                               /* 2036 */
}

/* 0x0024f678.  Anchors a haze column 450 units above the model's hip. */
static int SceneEffectHazeRegist(SCENE_EFFECT_HAZE *pHaze)
{
    SCENE_HAZE_DATA TmpData;
    SLL_CELL *pCell;
    int RetVal = -1;
    SCENE_HAZE_DATA *pData;

    pCell = SingleLinkListAddEnd(&SceneHazeCtrl, &TmpData);         /* 2054 */
    if (pCell != NULL)                                              /* 2056 */
    {
        pData = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);    /* 2057 */
        RetVal = 0;

        pData->ModelId = pHaze->ModelId;                            /* 2059 */
        SceneManModelHipPositionGet(pData->HipPosition, pData->ModelId);    /* 2060 */
        SceneManModelHipPositionGet(pData->OldHipPosition, pData->ModelId);     /* 2061 */
        sceVu0AddVector(pData->Position, pData->HipPosition, (float *)SaeHazeOffset);   /* 2062 */
        pData->Rot[0] = 0.0f;                                       /* 2063 */
        pData->Rot[1] = 0.0f;                                       /* 2063 */
        pData->Rot[2] = 0.0f;                                       /* 2063 */
        pData->Rot[3] = 1.0f;                                       /* 2063 */
        pData->pEffRet = EffectSaeHazeReq(pData->Position, pData->Rot, NULL);   /* 2064 */
    }

    return RetVal;                                                  /* 2069 */
}

/* 0x0024f738.  Follows the hip each frame; when it moved more than a hair,
 * the haze column leans along the movement. */
static void SceneEffectHazeCtrlExec(void)
{
    SLL_CELL *pCell;
    float RotX;
    SINGLE_LINK_LIST *pSLL = &SceneHazeCtrl;
    SCENE_HAZE_DATA *pHaze;

    pCell = SingleLinkListBeginCell(pSLL);
    while (pCell != NULL)                                           /* 2083 */
    {
        pHaze = (SCENE_HAZE_DATA *)SingleLinkListCellBodyPtr(pCell);    /* 2084 */

        g3dxVu0CopyVector(pHaze->OldHipPosition, pHaze->HipPosition);   /* g3dxVu0.h */
        SceneManModelHipPositionGet(pHaze->HipPosition, pHaze->ModelId);    /* 2087 */
        sceVu0AddVector(pHaze->Position, pHaze->HipPosition, (float *)SaeHazeOffset);   /* 2088 */

        /* The 0.01 is a double in the ROM too (no f suffix) -- this compare
         * went through the soft-float dpcmp. */
        if (Get2PLength(pHaze->OldHipPosition, pHaze->HipPosition) > 0.01)  /* 2090 */
        {
            Get2PosRot(pHaze->OldHipPosition, pHaze->HipPosition, &RotX, &pHaze->Rot[1]);   /* 2091 */
        }

        pCell = SingleLinkListNextCell(pCell);
    }
}

/* ==========================================================================
 *  Pad vibration.
 * ======================================================================== */

/* 0x0024f838 */
static void SceneEffectVibrationCtrlInit(void)
{
    SceneVibrationCtrl.pDataTop = NULL;                             /* 2106 */
    SceneVibrationCtrl.pDataLast = NULL;                            /* 2107 */
}

/* 0x0024f848 */
static void SceneEffectVibrationCtrlAllDelete(void)
{
    SCENE_VIBRATION_DATA *pData;
    SCENE_VIBRATION_DATA *pDataNext;

    pData = SceneVibrationCtrl.pDataTop;                            /* 2116 */

    while (pData != NULL)                                           /* 2119 */
    {
        pDataNext = pData->pNext;
        /* The null test around the free is the ROM's own. */
        if (pData != NULL)                                          /* 2121 */
        {
            heapCtrlFree(GetSystemHeapWrkP(), pData);
        }
        pData = pDataNext;                                          /* 2122 */
    }

    SceneEffectVibrationCtrlInit();                                 /* 2126 */
}

/* 0x0024f8a8 */
static int SceneEffectVibrationRegist(SCENE_EFFECT_VIBRATION2 *pVibration, int Frame)
{
    SCENE_VIBRATION_CTRL *pCtrl = &SceneVibrationCtrl;
    SCENE_VIBRATION_DATA *pMalloc;

    pMalloc = (SCENE_VIBRATION_DATA *)SAFE_MALLOC(GetSystemHeapWrkP(), NULL, sizeof(SCENE_VIBRATION_DATA));     /* 2143 */
    if (pMalloc == NULL)                                            /* 2145 */
    {
        return -1;
    }

    if (pCtrl->pDataLast != NULL)                                   /* 2147 */
    {
        pCtrl->pDataLast->pNext = pMalloc;
    }
    else
    {
        pCtrl->pDataTop = pMalloc;                                  /* 2149 */
    }
    pCtrl->pDataLast = pMalloc;                                     /* 2153 */

    pCtrl->pDataLast->StartFrame = pVibration->StartTime;           /* 2156 */
    pCtrl->pDataLast->EndFrame = pVibration->EndTime;               /* 2157 */
    pCtrl->pDataLast->BlankTime = pVibration->BlankTime;            /* 2158 */
    pCtrl->pDataLast->VibrateTime = pVibration->VibrateTime;        /* 2159 */
    pCtrl->pDataLast->Power = pVibration->Power;                    /* 2160 */
    pCtrl->pDataLast->BlankCount = pVibration->BlankTime;           /* 2161 */
    pCtrl->pDataLast->ActuaterNo = pVibration->ActuaterNo;          /* 2162 */
    pCtrl->pDataLast->VibrateCount = pVibration->VibrateTime;       /* 2163 */
    pCtrl->pDataLast->pNext = NULL;                                 /* 2164 */

    return 0;                                                       /* 2166 */
}

/* 0x0024f950.  The NTSC path: rumble while VibrateCount runs, rest while
 * BlankCount runs, reload both when the rest expires. */
static void SceneEffectVibrationCtrlExec(int NowFrame)
{
    SCENE_VIBRATION_DATA *pData;

    pData = SceneVibrationCtrl.pDataTop;                            /* 2177 */

    while (pData != NULL)                                           /* 2179 */
    {
        if (pData->StartFrame <= NowFrame && NowFrame < pData->EndFrame)    /* 2180 */
        {
            if (pData->VibrateCount != 0)                           /* 2181 */
            {
                if (pData->ActuaterNo == 0)                         /* 2182 */
                {
                    VibrateRequest1(0, 1);                          /* 2183 */
                }
                else
                {
                    VibrateRequest2(0, pData->Power);               /* 2186 */
                }
                pData->VibrateCount--;                              /* 2188 */
            }
            else
            {
                if (pData->BlankCount == 0)                         /* 2191 */
                {
                    pData->VibrateCount = pData->VibrateTime;       /* 2192 */
                    pData->BlankCount = pData->BlankTime;           /* 2193 */
                }
                else
                {
                    pData->BlankCount--;                            /* 2196 */
                }
            }
        }

        pData = pData->pNext;                                       /* 2201 */
    }
}

/* 0x0024fa18.  The movie/PAL split of the same machine: fire the actuators
 * for the frame without touching the counters... */
static void SceneEffectVibrationExecPAL(int NowFrame)
{
    SCENE_VIBRATION_DATA *pData;

    pData = SceneVibrationCtrl.pDataTop;                            /* 2214 */

    while (pData != NULL)                                           /* 2216 */
    {
        if (pData->StartFrame <= NowFrame && NowFrame < pData->EndFrame)    /* 2217 */
        {
            if (pData->VibrateCount != 0)                           /* 2218 */
            {
                if (pData->ActuaterNo == 0)                         /* 2219 */
                {
                    VibrateRequest1(0, 1);                          /* 2220 */
                }
                else
                {
                    VibrateRequest2(0, pData->Power);               /* 2223 */
                }
            }
        }

        pData = pData->pNext;                                       /* 2228 */
    }
}

/* 0x0024faa8.  ...and step the counters for one movie frame without firing. */
static void SceneEffectVibrationCountPAL(int NowFrame)
{
    SCENE_VIBRATION_DATA *pData;

    pData = SceneVibrationCtrl.pDataTop;                            /* 2240 */

    while (pData != NULL)                                           /* 2242 */
    {
        if (pData->StartFrame <= NowFrame && NowFrame < pData->EndFrame)    /* 2243 */
        {
            if (pData->VibrateCount != 0)                           /* 2244 */
            {
                pData->VibrateCount--;                              /* 2245 */
            }
            else
            {
                if (pData->BlankCount == 0)                         /* 2248 */
                {
                    pData->VibrateCount = pData->VibrateTime;       /* 2249 */
                    pData->BlankCount = pData->BlankTime;           /* 2250 */
                }
                else
                {
                    pData->BlankCount--;                            /* 2253 */
                }
            }
        }

        pData = pData->pNext;                                       /* 2258 */
    }
}

/* ==========================================================================
 *  Torches: a flame effect pinned to a model's raised-hand bone.
 * ======================================================================== */

/* 0x0024fb18 */
static void SceneTorchCtrlInit(void)
{
    SingleLinkListInit(&SceneTorchCtrl, sizeof(SCENE_EFFECT_TORCH_DATA));   /* 2272 */
}

/* 0x0024fb40 */
static void SceneTorchCtrlRegistAndReq(SCENE_EFFECT_TORCH_DATA *pData, int TorchType)
{
    SLL_CELL *pCell;
    SCENE_EFFECT_TORCH_DATA *pTorch;

    pCell = SingleLinkListAddEnd(&SceneTorchCtrl, pData);           /* 2282 */
    if (pCell != NULL)                                              /* 2283 */
    {
        pTorch = (SCENE_EFFECT_TORCH_DATA *)SingleLinkListCellBodyPtr(pCell);   /* 2284 */
        pTorch->pEffectRet = EffectSetTorch2NoSE(pTorch->Position, TorchType);  /* 2285 */
    }
}

/* 0x0024fba0 */
static void SceneTorchCtrlAllDelete(void)
{
    SLL_CELL *pCell;
    SINGLE_LINK_LIST *pSLL = &SceneTorchCtrl;

    pCell = SingleLinkListBeginCell(pSLL);
    while (pCell != NULL)                                           /* 2296 */
    {
        SLL_CELL *pCellNext = SingleLinkListNextCell(pCell);
        SCENE_EFFECT_TORCH_DATA *pTorch = (SCENE_EFFECT_TORCH_DATA *)SingleLinkListCellBodyPtr(pCell);  /* 2298 */

        EffectResetTorch2(pTorch->pEffectRet);                      /* 2300 */
        SingleLinkListRemove(pSLL, pCell);                          /* 2301 */
        pCell = pCellNext;
    }                                                               /* 2303 */
}

/* 0x0024fc30.  Keeps every torch on its bone. */
static void SceneTorchCtrlExec(void)
{
    SLL_CELL *pCell;
    SINGLE_LINK_LIST *pSLL = &SceneTorchCtrl;
    SCENE_EFFECT_TORCH_DATA *pTorch;

    pCell = SingleLinkListBeginCell(pSLL);
    while (pCell != NULL)                                           /* 2316 */
    {
        pTorch = (SCENE_EFFECT_TORCH_DATA *)SingleLinkListCellBodyPtr(pCell);   /* 2317 */

        motGetBukiUpPos(pTorch->Position, pTorch->pMdlAnm);         /* 2319 */

        pCell = SingleLinkListNextCell(pCell);
    }
}

/* 0x0024fca8.  The movie-side entry point: the effect stream keyed on movie
 * frames instead of scene frames.  Only the requests and the vibration
 * machine run here -- a PAL movie steps 1.2 counts a frame, so the counters
 * advance per movie frame and the actuators fire once at the end. */
void SceneMovieEffectMain(int NowFrame, u_int *pDataAddr)
{
    int ExecutedFrame;
    int i;

    if (pDataAddr != NULL)                                          /* 2334 */
    {
        ExecutedFrame = SceneEffectCtrl.ExecutedFrame;              /* 2335 */

        if (GetPALMode() != 0)                                      /* 2339 */
        {
            NowFrame = (int)((float)NowFrame * 1.1999999f);         /* 2340 */
        }

        for (i = ExecutedFrame + 1; i <= NowFrame; i++)             /* 2345 */
        {
            SceneEffectReq((short *)pDataAddr, i);                  /* 2346 */
            SceneEffectVibrationReq((short *)pDataAddr, i);         /* 2347 */

            SceneEffectVibrationCountPAL(i);                        /* 2350 */
        }                                                           /* 2351 */

        SceneEffectVibrationExecPAL(NowFrame);                      /* 2353 */

        SceneEffectCtrl.ExecutedFrame = NowFrame;                   /* 2355 */
    }
}
