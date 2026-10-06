/* ==========================================================================
 *  graphics/scene/fod.h
 *
 *  Scene FOD camera / light / post-effect controller.
 *
 *  A scene ships three parallel FOD streams -- camera, light and effect --
 *  each a FOD_FILE_HDR followed by one variable-length record per frame.
 *  FOD_CTRL walks all three in lockstep, keeping a "current" and a "next"
 *  record for the camera and the lights so scene.c can interpolate between
 *  them.
 *
 *  Every struct here is sized against types.txt; FOD_LIGHT is 0x14c0 and
 *  FOD_CTRL 0x1510 on the target.  Host offsets drift from the ROM's only
 *  where a struct holds pointers (4 bytes on target, 8 here) -- FOD_CTRL's
 *  eleven stream pointers, and FOD_EFF_PARAM's fix[]/pdf_p.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_SCENE_FOD_H
#define _GRAPHICS_SCENE_FOD_H

#include "eetypes.h"
#include "../graph3d/gra3dTypes.h"
#include "../graph3d/ctl/fixed_array.h"

enum
{
    FOD_LIGHT_MAX = 36,
    FOD_AMBIENT_MAX = 6,
};

typedef struct                          /* 0x10 */
{
    u_int file_id;
    float version;
    u_int reso;
    u_int frame;
} FOD_FILE_HDR;

/* One entry per light in the light stream's header table: which G3DLIGHT
 * slot it drives, its type (1 = directional, 2 = spot, 3 = point) and
 * whether the per-frame records carry animation for it. */
typedef struct                          /* 0x20 */
{
    u_char light_no;
    u_char light_type;
    u_char anm_flg;
    char   pad;
    char   light_name[28];
} FOD_LIT_SERIAL;

/* --------------------------------------------------------------------------
 *  Stream record headers.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x10 */
{
    u_int                    frame;
    u_int                    size;
    fixed_array<u_char, 8>   anm;       /* one flag per animated channel */
} FOD_CAM_FRAME;

typedef struct                          /* 0x10 */
{
    u_int frame;
    u_int size;
    u_int pad[2];
} FOD_LIT_FRAME;

/* Per-light sub-record inside a FOD_LIT_FRAME: `type` repeats the light's
 * FOD_LIT_SERIAL::light_type and `anm` says which of colour / position /
 * direction / cone follow, each in its own quadword. */
typedef struct                          /* 0x10 */
{
    int                    light_id;
    u_int                  type;
    fixed_array<u_char, 8> anm;
} FOD_LIT_ANM;

typedef struct                          /* 0x10 */
{
    u_int  frame_no;
    u_int  size;
    u_char zdepth;
    u_char mono;
    u_char sepia;
    u_char color_change;
    u_int  pad;
} FOD_EFF_FRAME;

/* --------------------------------------------------------------------------
 *  Light-stream header records, in file order after the FOD_LIT_SERIAL table:
 *  one FOD_LIT_AMB, then one FOD_LIT_INF / _SPOT / _POINT per light.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x20 */
{
    u_int id;
    u_int size;
    u_int pad[2];
    float color[4];
} FOD_LIT_AMB;

typedef struct                          /* 0x30 */
{
    u_int id;
    u_int size;
    u_int pad[2];
    float color[4];
    float direction[4];
} FOD_LIT_INF;

typedef struct                          /* 0x50 */
{
    u_int id;
    u_int size;
    u_int pad[2];
    float color[4];
    float position[4];
    float interest[4];
    float cone;
    float spread;
    float fpad[2];
} FOD_LIT_SPOT;

typedef struct                          /* 0x30 */
{
    u_int id;
    u_int size;
    u_int pad[2];
    float color[4];
    float position[4];
} FOD_LIT_POINT;

/* --------------------------------------------------------------------------
 *  Post-processing effect data.
 *
 *  The effect stream opens with a FOD_EF_FIX_HDR followed by `num` fixed
 *  (whole-scene) FOD_EFF_DATA records; the per-frame FOD_EFF_FRAMEs follow.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x10 */
{
    int num;
    int shibata;
    int pad[2];
} FOD_EF_FIX_HDR;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char type;
    u_char alpmax;
    u_char colmax;
    float  speed;
    float  alpha;
    u_int  ipad;
} FOD_EF_DITHER;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char blur_type;
    u_char alpha;
    u_char cpad;
    u_int  scale;
    u_int  rot;
    u_int  ipad;
} FOD_EF_BLUR;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char type;
    u_char volume;
    u_char cpad;
    u_int  ipad[3];
} FOD_EF_DEFORM;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char volume;
    u_char cpad[2];
    u_int  ipad[3];
} FOD_EF_FOCUS;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char color;
    u_char alpha;
    u_char cont_type;
    u_int  ipad[3];
} FOD_EF_CONTRAST;

/* The one oversized member -- and the reason sizeof(FOD_EFF_DATA) is 0x1c
 * rather than the 0x10 every record in the file actually occupies. */
typedef struct                          /* 0x1c */
{
    u_char efct_id;
    u_char color;
    u_char alpha;
    u_char alpha2;
    u_char cont_type;
    u_char hole[3];                     /* 0x05..0x07: alignment, not a ROM member */
    u_int  cpad[3];
    u_int  ipad[2];
} FOD_EF_NEGA;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char volume;
    u_char cpad[2];
    u_int  ipad[3];
} FOD_EF_F_FRAME;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char type;
    u_char cpad[2];
    float  x;
    float  y;
    float  z;
} FOD_EF_LENZ_F;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char volume;
    u_char cpad[2];
    u_int  ipad[3];
} FOD_EF_CROSS_F;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char data_type;
    u_char type;
    u_char alpha;
    float  rate;
    float  trate;
    int    pad;
} FOD_EF_P_DEFORM1;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char data_type;
    u_char cpad[2];
    float  posx;
    float  posy;
    float  posz;
} FOD_EF_P_DEFORM2;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char data_type;
    u_char cpad[2];
    float  sclx;
    float  scly;
    float  pad;
} FOD_EF_P_DEFORM3;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char cpad[3];
    u_char r;
    u_char g;
    u_char b;
    u_char a;
    u_int  ipad[2];
} FOD_EF_FADE_SCR;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char alpha;
    u_char cpad[2];
    u_char prefix[4];
    u_int  mdl_id;
    u_int  ipad;
} FOD_EF_FADE_MDL;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char cpad[3];
    float  posx;
    float  posy;
    float  posz;
} FOD_EF_FIRE;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char cpad[3];
    float  posx;
    float  posy;
    float  posz;
} FOD_EF_ITEM;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char type;
    u_char cpad[2];
    float  posx;
    float  posy;
    float  posz;
} FOD_EF_AMULET;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char cpad[3];
    u_char prefix[4];
    u_int  ipad[2];
} FOD_EF_ENEMY;

typedef struct                          /* 0x10 */
{
    u_char efct_id;
    u_char type;
    u_char cpad[2];
    u_int  frame;
    u_int  ipad[2];
} FOD_EF_VIBRATE;

typedef union                           /* 0x1c */
{
    FOD_EF_DITHER    dither;
    FOD_EF_BLUR      blur;
    FOD_EF_DEFORM    deform;
    FOD_EF_FOCUS     focus;
    FOD_EF_CONTRAST  contrast;
    FOD_EF_NEGA      nega;
    FOD_EF_F_FRAME   f_frame;
    FOD_EF_LENZ_F    lenz_f;
    FOD_EF_CROSS_F   cross_f;
    FOD_EF_P_DEFORM1 pdf1;
    FOD_EF_P_DEFORM2 pdf2;
    FOD_EF_P_DEFORM3 pdf3;
    FOD_EF_FADE_SCR  fade_scr;
    FOD_EF_FADE_MDL  fade_mdl;
    FOD_EF_FIRE      fire;
    FOD_EF_ITEM      item;
    FOD_EF_AMULET    amulet;
    FOD_EF_ENEMY     enemy;
    FOD_EF_VIBRATE   vibrate;
    u_char           unit[16];
} FOD_EFF_DATA;

/* --------------------------------------------------------------------------
 *  FOD_LIGHT -- the resolved light set for one scene.
 *
 *  `all_lit` is built once by FodGetFirstLight() from the stream header and
 *  then mutated per frame by FodGetToSgLight(); FodSetMyLight() picks the
 *  subset that matches a prefix and pushes it into the gra3d light data.
 * ------------------------------------------------------------------------ */
typedef struct                                              /* 0x14c0 */
{
    fixed_array<FOD_LIT_SERIAL, FOD_LIGHT_MAX> lit_serial;   /* 0x0000 */
    fixed_array<G3DLIGHT, FOD_LIGHT_MAX>       all_lit;      /* 0x0480 */
    fixed_array<float[4], FOD_AMBIENT_MAX>     amb;          /* 0x1440 */
    u_int                                     *lit_top;      /* 0x14a0 */
    u_int                                      ilit_num;     /* 0x14a4 */
    u_int                                      slit_num;     /* 0x14a8 */
    u_int                                      plit_num;     /* 0x14ac */
    u_int                                      all_lit_num;  /* 0x14b0 */
    int                                        hand_spot_no; /* 0x14b4 */
} FOD_LIGHT;

typedef struct                          /* 0x30 */
{
    float vPosition[4];
    float vTarget[4];
    float fRoll;
    float fFov;
    float fNearZ;
    float fFarZ;
} FOD_CAMERA_DATA;

typedef struct                                      /* 0x1510 */
{
    FOD_LIGHT      fod_light;                       /* 0x0000 */
    FOD_FILE_HDR  *cam_file_hdr;                    /* 0x14c0 */
    FOD_CAM_FRAME *cam_frame_top;                   /* 0x14c4 */
    FOD_CAM_FRAME *cam_frame;                       /* 0x14c8 */
    FOD_CAM_FRAME *cam_frame_next;                  /* 0x14cc */
    FOD_FILE_HDR  *lit_file_hdr;                    /* 0x14d0 */
    FOD_LIT_FRAME *lit_frame_top;                   /* 0x14d4 */
    FOD_LIT_FRAME *lit_frame;                       /* 0x14d8 */
    FOD_LIT_FRAME *lit_frame_next;                  /* 0x14dc */
    FOD_FILE_HDR  *eff_file_hdr;                    /* 0x14e0 */
    FOD_EFF_FRAME *eff_frame_top;                   /* 0x14e4 */
    FOD_EFF_FRAME *eff_frame;                       /* 0x14e8 */
    u_int          now_frame;                       /* 0x14ec */
    u_int          frame_max;                       /* 0x14f0 */
    u_int          now_reso;                        /* 0x14f4 */
    u_char         resolution;                      /* 0x14f8 */
    u_char         end_flg;                         /* 0x14f9 */
    u_char         pad0[2];                         /* 0x14fa */
    float          float_now_frame;                 /* 0x14fc */
    int            cut_timing_index;                /* 0x1500 */
} FOD_CTRL;

typedef struct                          /* 0x80 */
{
    FOD_EFF_DATA *fix[12];              /* 0x00 */
    float         lenz_pos[4];          /* 0x30 */
    float         lenz_rot[4];          /* 0x40 */
    float         pdf_pos[4];           /* 0x50 */
    float         pdf_spd;              /* 0x60 */
    float         pdf_rate;             /* 0x64 */
    float         pdf_trate;            /* 0x68 */
    void         *pdf_p;                /* 0x6c */
    u_int         fix_eff_num;          /* 0x70 */
    u_int         fade_mdl_cnt;         /* 0x74 */
    u_int         fire_num;             /* 0x78 */
    u_int         mono_flg;             /* 0x7c */
} FOD_EFF_PARAM;

#ifdef __cplusplus
extern "C" {
#endif

/* All three are GLOBAL in the ROM's symbol table, not file-static. */
extern GRA3DLIGHTDATA FodLight;
extern float          fod_cmn_mtx[4][4];
extern FOD_EFF_PARAM  eff_param;

void            FodInit(FOD_CTRL *fc, u_int *tcp, u_int *tlp, u_int *tep, float *offset);
int             FodNextFrame(FOD_CTRL *fc, int SceneNo);
void            FodSetFrame(FOD_CTRL *fc, u_int frame);
void            FodSetMyLight(FOD_LIGHT *fl, char *pfx, const float *eye);
void            FodChangeFodLightToGra3dLight(GRA3DLIGHTDATA *pGra3dLight, FOD_LIGHT *fl,
                                              char *pfx, const float *eye);
void            FodSetSpotLights(G3DLIGHT *sl, u_int num);
void            FodGetToSgLight(FOD_LIGHT *pFodLignt, FOD_LIT_FRAME *pFodLitFrame,
                                float *offset);
void            FodGetDropSpotPos(FOD_LIGHT *fl, char *pfx, float *lp, float *li);
void            FodGetFirstCam(FOD_CAMERA_DATA *pFodCam, FOD_CTRL *pFodCtrl, float *offset);
void            FodGetCamData(FOD_CAMERA_DATA *pFodCam, FOD_CAM_FRAME *pFodCamFrame,
                              float *offset);
GRA3DLIGHTDATA *FodGetGra3DLight(void);

#ifdef __cplusplus
}
#endif

#endif /* _GRAPHICS_SCENE_FOD_H */
