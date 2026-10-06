/* ==========================================================================
 *  graphics/scene/scene.h
 *
 *  Scene file loading / metadata interface.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _GRAPHICS_SCENE_SCENE_H
#define _GRAPHICS_SCENE_SCENE_H

#include "../graph3d/eetypes.h"
#include "fod.h"
#include "../motion/mdlwork.h"
#include "../graph3d/ctl/fixed_array.h"

/* --------------------------------------------------------------------------
 *  Per-model enemy effect descriptors.
 * ------------------------------------------------------------------------ */
typedef struct                          /* 0x18 */
{
    u_int  type;
    float  sclx;
    float  scly;
    u_int  alpha;
    float  rate;
    float  trate;
} SCN_ENE_EF_PDF;

typedef struct                          /* 0x4c */
{
    u_short        scene_no;
    u_short        mdl_no;
    float          aura_size;
    float          aura_rate;
    u_int          aura_rgba;
    SCN_ENE_EF_PDF pdf1;                /* 0x10 */
    SCN_ENE_EF_PDF pdf2;                /* 0x28 */
    float          pdf_dist;            /* 0x40 */
    float          aura_pos_ajst;       /* 0x44 */
    int            mdl_alpha;           /* 0x48 */
} SCN_ENE_EFCT;

typedef struct                          /* 0x4c */
{
    ANI_CTRL     *pMdlAnm;              /* 0x00 */
    SCN_ENE_EFCT *ene_efct;             /* 0x04 */
    void         *efct_addr[4];         /* 0x08 */
    char          prefix[9];            /* 0x18 */
    u_int         mdl_no;               /* 0x24 */
    u_int         mdl_alpha;            /* 0x28 */
    u_int         scn_mdl_no;           /* 0x2c */
    u_int        *mdl_addr;             /* 0x30 */
    u_int        *mdl_addr_db;          /* 0x34 */
    u_int        *mot_addr;             /* 0x38 */
    u_int        *mim_addr;             /* 0x3c */
    u_int        *mim_buf_addr;         /* 0x40 */
    u_int        *pk2_addr;             /* 0x44 */
    u_int         disp_flg;             /* 0x48 */
} SCN_ANM_MDL;

typedef struct
{
    u_int *scn_file_addr;              /* 0x00 */
    u_int  file_num;                   /* 0x04 */
    u_int *ofs_top_addr;               /* 0x08 */
    u_int *hdr_addr;                   /* 0x0c */
    u_int *cam_fod_addr;               /* 0x10 */
    u_int *lit_fod_addr;               /* 0x14 */
    u_int *eff_fod_addr;               /* 0x18 */
    u_int *man_mot_addr;               /* 0x1c */
    u_int *man_mim_addr;               /* 0x20 */
    u_int *furn_mot_addr;              /* 0x24 */
    u_int *door_mot_addr;              /* 0x28 */
    u_int *item_mot_addr;              /* 0x2c */
} SCENE_FILE;

typedef struct
{
    float min;                         /* 0x00 */
    float max;                         /* 0x04 */
    float near;                        /* 0x08 */
    float far;                         /* 0x0c */
    int   r;                           /* 0x10 */
    int   g;                           /* 0x14 */
    int   b;                           /* 0x18 */
    int   pad;                         /* 0x1c */
} SCENE_FOG;

typedef struct
{
    fixed_array<SCN_ANM_MDL,8>   man_mdl;              /* 0x0000 */
    fixed_array<SCN_ANM_MDL,14>  furn_mdl;             /* 0x0260 */
    fixed_array<SCN_ANM_MDL,8>   item_mdl;             /* 0x0688 */
    fixed_array<SCN_ANM_MDL,8>   door_mdl;             /* 0x08e8 */
    fixed_array<int,8>           man_mdl_tex;          /* 0x0b48 */
    FOD_CTRL                     fod_ctrl;             /* 0x0b70 */
    SCENE_FOG                    fog;                  /* 0x2080 */
    FOD_CAMERA_DATA              CameraData;           /* 0x20a0 */
    int                          man_mdl_num;          /* 0x20d0 */
    int                          door_num;             /* 0x20d4 */
    int                          furn_num;             /* 0x20d8 */
    int                          item_num;             /* 0x20dc */
    u_int                       *scn_data_addr;        /* 0x20e0 */
    u_int                       *light_rev_addr;       /* 0x20e4 */
    u_int                       *effect_addr;          /* 0x20e8 */
    u_int                       *pMimBuf;              /* 0x20ec */
    int                          scene_no;             /* 0x20f0 */
    int                          room_no;              /* 0x20f4 */
    int                          sub_room_no;          /* 0x20f8 */
    int                          count_flg;            /* 0x20fc */
    int                          mirror_flg;           /* 0x2100 */
    int                          init_flg;             /* 0x2104 */
    float                        fNearZBak;            /* 0x2108 */
    float                        fFarZBak;             /* 0x210c */
    int                          DrawAneFlg;           /* 0x2110 */
    int                          DrawImoutoFlg;        /* 0x2114 */
    int                          DoubleBufferId;       /* 0x2118 */
    int                          AreaNoBak;            /* 0x211c */
    int                          MonotoneEnableBak;    /* 0x2120 */
    int                         *pPlayerAccessoryPk2;  /* 0x2124 */
    int                         *pSisterAccessoryPk2;  /* 0x2128 */
} SCENE_CTRL;

typedef struct
{
    int                buf_id;          /* 0x00 */
    int                status;          /* 0x04 */
    fixed_array<int,3> id;              /* 0x08 */
    int                file_num;        /* 0x14 */
    int                adpcm_id;        /* 0x18 */
} SCENE_LOAD_CTRL;

typedef SCENE_LOAD_CTRL SCENE_LOAD;

u_int       GetPrefixNo(char *pfx);
int         PrefixToNo(char *pPrefix, int num);
int         GetFileNoFromSceneNo(int scene_no);
int         SceneDecisionMovie(int scene_no);
int         SceneEffectDataFileNoGet(int scene_no);
void        InitSceneWork(void);
int         SceneAllLoad(int scene_no, u_int *load_addr);
void        SceneInitializeIngame(void);
void        SceneDraw(int scene_no);
int         SceneIsEnd(void);
void        SceneCountFlgSet(int flg);
int         SceneIsPlay(void);
void        SceneEndProc(void);
void        SceneFodSetNowFrame(int scene_id, int frame);
void        SceneFodSetNowRezo(int scene_id, int reso);
void        SceneFodSetFrame(int scene_id, int frame);
FOD_LIGHT  *SceneFodLightPtrGet(int scene_id);
int         SceneRoomNoGet(void);
int         SceneSubRoomNoGet(void);
SCENE_CTRL *SceneCtrlGet(int buf_no);
void        SceneSetSquare(int pri, float x, float y, float w, float h, u_char r, u_char g, u_char b, u_char a);
void        SceneReleaseEffect(SCENE_CTRL *sc);
int         SceneManModelNoChange(int ModelNo);
void        SceneGetModelPDeformPos(float *Position, int ModelId, float Dist);
void        SceneMovieEffectMain(int NowFrame, u_int *pDataAddr);
ANI_CTRL   *SceneGetAniCtrl(int ModelId);
void        SceneManModelLegPositionGet(float *LegPos, int ModelId);
void        SceneManModelHipPositionGet(float *HipPos, int ModelId);
void        SceneChangeLightParameter(int LightType, int LightNo, int ModelType, int ModelId, float r, float g, float b, float Power, float Cone);
void        SceneChangeHandSpotLightParameter(float r, float g, float b, float Power, float Cone);

#include "scene_effect.h"

#endif /* _GRAPHICS_SCENE_SCENE_H */
