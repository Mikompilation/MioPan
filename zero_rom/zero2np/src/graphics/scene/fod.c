// FILE: /home/zero_rom/zero2np/src/graphics/scene/fod.c
//
// Scene FOD camera / light / post-effect controller.
//
// A scene ships three parallel FOD streams -- camera, light and effect.  Each
// is a FOD_FILE_HDR followed by one variable-length record per frame, and
// FodInit()/FodNextFrame()/FodSetFrame() walk all three in lockstep.  For the
// camera and the light streams a "current" and a "next" record are kept so
// scene.c can interpolate between them; the effect stream only ever has a
// current record.
//
// The light stream's header is richer than the other two: a FOD_LIT_SERIAL
// table naming every light, then one FOD_LIT_AMB and one FOD_LIT_INF / _SPOT /
// _POINT per light.  FodGetFirstLight() turns that into the 36 resident
// G3DLIGHTs in FOD_LIGHT, FodGetToSgLight() applies each frame's animation on
// top, and FodSetMyLight() copies the subset whose name matches a prefix into
// the gra3d light data.
//
// FodGetDropSpotPos() is exported but has no call site anywhere in the ROM
// (checked by scanning every jal in the loadable segments, not by Ghidra
// xrefs).  It is reconstructed because the object file defines it.
//
// Reconstructed from the Feb 6 2004 prototype (SLES_523.84), fod.o
// 0x00194d38..0x001969bf.  Trailing /* NNN */ comments are the original source
// line numbers recovered from the ROM's line table.

#include "fod.h"

#include <math.h>                               /* cosf                        */
#include <stdio.h>                              /* printf                      */
#include <string.h>                             /* memset / strcmp / strncmp   */

#include "scene_dat.h"                          /* scene_cut_timing            */

#include "../../common/utility2.h"              /* StrToLower                  */
#include "../../sdk/libvu0.h"
#include "../../system/os/system.h"             /* GetPALMode                  */
#include "../graph3d/ctl/fixed_array.h"
#include "../graph3d/g3dLight.h"                /* g3dutilSetLightDefault      */
#include "../graph3d/g3dxVu0.h"                 /* g3dxVu0CopyVector (lq/sq)   */
#include "../graph3d/gra3d.h"                   /* gra3dSetAmbient / ...Intens */

/* `near` / `far` are the ROM's member names.  They are also macros in the
 * Windows SDK, which would silently delete them here. */
#ifdef near
#undef near
#endif
#ifdef far
#undef far
#endif

/* The camera stream's header record, immediately after its FOD_FILE_HDR: the
 * scene's opening camera, in the same channel order the per-frame
 * FOD_CAM_FRAMEs use. */
typedef struct                          /* 0x30 */
{
    float p[4];
    float i[4];
    float roll;
    float fov;
    float near;
    float far;
} FOD_FIRST_CAM;

/* --------------------------------------------------------------------------
 *  Globals.  All three are GLOBAL in the ROM's symbol table (FodLight 0x13a0
 *  at 0x312a80, fod_cmn_mtx 0x40 at 0x313e20, eff_param 0x80 at 0x313e60) and
 *  all three are zero-initialised -- fod.o's whole .data block reads as zero.
 *
 *  FodLight lands in .data rather than .bss because GRA3DLIGHTDATA's
 *  fixed_array members give it a constructor: the ROM carries a
 *  "global constructors keyed to FodLight" entry in .ctors whose
 *  __static_initialization_and_destruction_0 body is empty.  A plain
 *  file-scope definition reproduces both facts.
 * ------------------------------------------------------------------------ */
GRA3DLIGHTDATA FodLight;
float          fod_cmn_mtx[4][4];
FOD_EFF_PARAM  eff_param;

/* One .lit4 slot per inline expansion, not one per distinct value: fod.o's
 * 15 slots hold PI six times, 2*PI four times and PI/180 twice.  Read out of
 * the ELF at 0x3ee2fc rather than assumed. */
static const float FOD_ANGLE_SCALE    = 0.01745329052209854f;
static const float FOD_PI             = 3.141592502593994f;
static const float FOD_TWO_PI         = 6.283185005187988f;
static const float FOD_PAL_FRAME_STEP = 1.1999999284744263f;

static void   FodGetLightNum(FOD_LIGHT *fl);
static void   FodGetLightSerial(FOD_LIGHT *fl);
static u_int *FodGetFirstLight(FOD_LIGHT *fl, float *offset);
static void   Fodg3dSetLight(int iLightId, G3DLIGHT *pLight);
static int    FodGetLightType(FOD_LIGHT *fl, int id);
static u_int *FodGetFixEffect(u_int *tep);

/* --------------------------------------------------------------------------
 *  FodInit
 *
 *  Latch the three stream pointers and resolve the light header.  Note that
 *  nothing here is null-checked: tcp is dereferenced for frame_max and the
 *  resolution, and the light path runs unconditionally.  A scene always ships
 *  all three streams.
 *
 *  fod_cmn_mtx is the file->world transform every FOD position goes through:
 *  a uniform 25x scale with a 180-degree flip about X.
 * ------------------------------------------------------------------------ */
void FodInit(FOD_CTRL *fc, u_int *tcp, u_int *tlp, u_int *tep, float *offset)    /* 104 */
{
    sceVu0UnitMatrix(fod_cmn_mtx);                                              /* 106 */
    fod_cmn_mtx[0][0] = fod_cmn_mtx[1][1] = fod_cmn_mtx[2][2] = 25.0f;          /* 107 */
    fod_cmn_mtx[3][3] = 1.0f;                                                   /* 108 */
    sceVu0RotMatrixX(fod_cmn_mtx, fod_cmn_mtx, FOD_PI);                         /* 109 */

    memset(fc, 0, sizeof(FOD_CTRL));                                            /* 111 */
    fc->cam_file_hdr = (FOD_FILE_HDR *)tcp;                                     /* 112 */
    fc->lit_file_hdr = (FOD_FILE_HDR *)tlp;                                     /* 113 */
    fc->eff_file_hdr = (FOD_FILE_HDR *)tep;                                     /* 114 */
    fc->now_frame    = 1;                                                       /* 115 */
    fc->now_reso     = 0;                                                       /* 116 */

    fc->float_now_frame  = 1.0f;                                                /* 118 */
    fc->cut_timing_index = 0;                                                   /* 119 */

    fc->frame_max     = tcp[3];                                                 /* 122 */
    fc->resolution    = (u_char)tcp[2];                                         /* 123 */
    fc->end_flg       = 0;
    fc->cam_frame_top = (FOD_CAM_FRAME *)(tcp + 0x10);                          /* 125 */

    fc->eff_frame_top = (FOD_EFF_FRAME *)FodGetFixEffect(tep);                  /* 127 */

    fc->fod_light.lit_top = tlp;                                                /* 130 */
    FodGetLightNum(&fc->fod_light);                                             /* 131 */
    FodGetLightSerial(&fc->fod_light);                                          /* 132 */
    fc->lit_frame_top =
        (FOD_LIT_FRAME *)FodGetFirstLight(&fc->fod_light, offset);              /* 133 */

    fc->cam_frame = fc->cam_frame_top;                                          /* 136 */
    fc->lit_frame = fc->lit_frame_top;                                          /* 137 */
    fc->eff_frame = fc->eff_frame_top;                                          /* 138 */

    fc->cam_frame_next = fc->cam_frame_top;                                     /* 140 */
    fc->lit_frame_next = fc->lit_frame_top;                                     /* 141 */
}

inline void FodMoveCameraNextFrame(FOD_CTRL* fc)
{
    int* fod_cam_addr;
    int  i;
    int  FirstFrame;
    int  SecondFrame;

    FirstFrame = (int)fc->float_now_frame;                                 /* 167 */
    SecondFrame = FirstFrame + 1;                                           /* 168 */
    if ((int)fc->frame_max < SecondFrame)                                   /* 169 */
    {
        SecondFrame = (int)fc->frame_max;
    }

    if (FirstFrame <= (int)fc->frame_max && FirstFrame != 0)                /* 171 */
    {
        if (fc->cam_file_hdr->frame != 0)                                   /* 175 */
        {
            fod_cam_addr = (int*)fc->cam_frame_top;

            /* 4 ints of FOD_CAM_FRAME header, then the record's own
             * payload size.  The >>2 converts bytes to ints and the int*
             * scaling multiplies back by 4, so a size that is not a
             * multiple of 4 is rounded down -- the `size & ~3` the
             * disassembly shows. */
            for (i = 1; i < FirstFrame; i++)                                /* 176 */
            {
                fod_cam_addr +=
                    4 + (((FOD_CAM_FRAME*)fod_cam_addr)->size >> 2);       /* 178 */
            }                                                              /* 180 */
            fc->cam_frame = (FOD_CAM_FRAME*)fod_cam_addr;

            if (FirstFrame < SecondFrame)                                   /* 183 */
            {
                fod_cam_addr +=
                    4 + (((FOD_CAM_FRAME*)fod_cam_addr)->size >> 2);       /* 186 */
            }                                                              /* 187 */
            fc->cam_frame_next = (FOD_CAM_FRAME*)fod_cam_addr;             /* 189 */
        }
    }
}

inline void FodMoveLightNextFrame(FOD_CTRL* fc)
{
    int* fod_lit_addr;
    int  i;
    int  FirstFrame;
    int  SecondFrame;

    FirstFrame = (int)fc->float_now_frame;                                 /* 200 */
    SecondFrame = FirstFrame + 1;                                           /* 201 */
    if ((int)fc->frame_max < SecondFrame)                                   /* 202 */
    {
        SecondFrame = (int)fc->frame_max;
    }

    if (FirstFrame <= (int)fc->frame_max && FirstFrame != 0)                /* 204 */
    {
        if (fc->lit_file_hdr->frame != 0)                                   /* 208 */
        {
            fod_lit_addr = (int*)fc->lit_frame_top;

            /* A light record's size covers its own header, so unlike the
             * camera stream there is nothing to add for it. */
            for (i = 1; i < FirstFrame; i++)                                /* 209 */
            {
                fod_lit_addr += ((FOD_LIT_FRAME*)fod_lit_addr)->size >> 2; /* 211 */
            }                                                              /* 212 */
            fc->lit_frame = (FOD_LIT_FRAME*)fod_lit_addr;

            if (FirstFrame < SecondFrame)                                   /* 215 */
            {
                fod_lit_addr +=
                    ((FOD_LIT_FRAME*)fod_lit_addr)->size >> 2;             /* 218 */
            }
            fc->lit_frame_next = (FOD_LIT_FRAME*)fod_lit_addr;             /* 220 */
        }
    }
}

/* --------------------------------------------------------------------------
 *  FodNextFrame
 *
 *  Advance one tick.  Returns 1 on the tick the scene runs out of frames,
 *  0 otherwise.
 *
 *  The three blocks below carry ROM line numbers 147-220, i.e. from *before*
 *  FodNextFrame's own declaration at line 238.  They were static helpers in
 *  the 96-line gap between FodInit and FodNextFrame, each with a single call
 *  site and therefore fully inlined -- no out-of-line copy exists in fod.o's
 *  .text, and being static they left no name to recover.  They are written
 *  inline here, as braced blocks with their own locals, which is what
 *  functions.txt's flattened local list for FodNextFrame shows.
 *
 *  Note that FodSetFrame() walks the same streams with its own copy of this
 *  code (lines 346-355) rather than calling the helpers.
 * ------------------------------------------------------------------------ */
int FodNextFrame(FOD_CTRL *fc, int SceneNo)                                     /* 238 */
{
    char  *eff_addr;
    float  resolution;       /* the ROM's name for the per-tick frame step --
                              * not fc->resolution, which is the tick divider */
    int    cut_timing;
    int   *pTiming;

    eff_addr = (char *)fc->eff_frame;                                           /* 239 */

    fc->now_reso++;                                                             /* 241 */
    if (fc->now_reso < fc->resolution)                                          /* 242 */
    {
        return 0;
    }
    fc->now_reso = 0;

    if (GetPALMode() != 0)                                                      /* 150 */
    {
        resolution = FOD_PAL_FRAME_STEP;
    }
    else
    {
        resolution = 1.0f;                                                      /* 154 */
    }
    fc->float_now_frame += resolution;                                          /* 147 */

    /* PAL runs the frame counter 1.2 frames a tick, so it drifts off the
     * frames the scene's cuts were authored on.  Snap it back to the next
     * authored cut as soon as it is reached, so a cut lands on the same frame
     * it does in NTSC. */
    if (GetPALMode() != 0)                                                      /* 247 */
    {
        pTiming    = scene_cut_timing[SceneNo];                                 /* 249 */
        cut_timing = pTiming[fc->cut_timing_index];                             /* 251 */
        if (cut_timing != -1 && (float)cut_timing <= fc->float_now_frame)       /* 252 */
        {
            fc->float_now_frame = (float)cut_timing;                            /* 253 */
            fc->cut_timing_index++;                                             /* 254 */
        }
    }

    fc->now_frame = (u_int)(int)(fc->float_now_frame + 0.5f);                   /* 258 */

    if ((float)fc->frame_max < fc->float_now_frame)                             /* 260 */
    {
        fc->float_now_frame = (float)fc->frame_max;                             /* 261 */
    }
    if (fc->frame_max < fc->now_frame)                                          /* 265 */
    {
        fc->end_flg = 1;                                                        /* 266 */
        return 1;                                                               /* 267 */
    }

    FodMoveCameraNextFrame(fc);
    FodMoveLightNextFrame(fc);
    
    /* The effect stream steps by exactly one record a tick, from the pointer
     * captured on entry.  It advances by the raw byte size -- no >>2<<2 mask,
     * unlike FodSetFrame's copy of the same walk. */
    if (fc->eff_file_hdr->frame != 0)                                           /* 272 */
    {
        eff_addr += fc->eff_frame->size;                                        /* 273 */
        fc->eff_frame = (FOD_EFF_FRAME *)eff_addr;                              /* 274 */
    }

    return 0;                                                                   /* 277 */
}

/* --------------------------------------------------------------------------
 *  FodSetFrame
 *
 *  Seek all three streams to `frame` by walking them from the top.  It does
 *  not touch cam_frame_next / lit_frame_next or clear end_flg -- the caller
 *  (SceneFodSetFrame, debug scrubbing only) re-enters FodNextFrame for those.
 * ------------------------------------------------------------------------ */
void FodSetFrame(FOD_CTRL *fc, u_int frame)
{
    u_int  i;
    u_int *fod_cam_addr;
    u_int *fod_lit_addr;
    u_int *fod_eff_addr;

    if (frame <= fc->frame_max && frame != 0)                                   /* 337 */
    {
        fod_cam_addr = (u_int *)fc->cam_frame_top;                              /* 339 */
        fod_lit_addr = (u_int *)fc->lit_frame_top;                              /* 340 */
        fod_eff_addr = (u_int *)fc->eff_frame_top;

        for (i = 1; i < frame; i++)                                             /* 343 */
        {
            if (fc->cam_file_hdr->frame != 0)                                   /* 344 */
            {
                fod_cam_addr +=
                    4 + (((FOD_CAM_FRAME *)fod_cam_addr)->size >> 2);           /* 346 */
            }                                                                  /* 347 */
            if (fc->lit_file_hdr->frame != 0)                                   /* 349 */
            {
                fod_lit_addr += ((FOD_LIT_FRAME *)fod_lit_addr)->size >> 2;     /* 351 */
            }
            if (fc->eff_file_hdr->frame != 0)                                   /* 353 */
            {
                fod_eff_addr += ((FOD_EFF_FRAME *)fod_eff_addr)->size >> 2;     /* 355 */
            }
        }                                                                      /* 357 */

        fc->now_frame = frame;                                                  /* 358 */
        fc->now_reso  = 0;                                                      /* 359 */

        fc->cam_frame = (FOD_CAM_FRAME *)fod_cam_addr;                          /* 361 */
        fc->lit_frame = (FOD_LIT_FRAME *)fod_lit_addr;                          /* 362 */
        fc->eff_frame = (FOD_EFF_FRAME *)fod_eff_addr;                          /* 363 */
    }
}                                                                              /* 364 */

/* --------------------------------------------------------------------------
 *  FodGetLightNum
 *
 *  Read the light-stream header's four counts.  all_lit_num is one less than
 *  the stored value and is not clamped to FOD_LIGHT_MAX: a stream declaring
 *  more than 36 lights would trip fixed_array's own range check downstream.
 * ------------------------------------------------------------------------ */
static void FodGetLightNum(FOD_LIGHT *fl)                                      /* 381 */
{
    u_int *lit_addr;

    lit_addr = fl->lit_top;                                                     /* 382 */

    fl->ilit_num    = lit_addr[4];                                              /* 383 */
    fl->slit_num    = lit_addr[5];                                              /* 384 */
    fl->plit_num    = lit_addr[6];                                              /* 385 */
    fl->all_lit_num = lit_addr[7] - 1;                                          /* 386 */
}

/* --------------------------------------------------------------------------
 *  FodGetLightSerial
 *
 *  Copy the stream's FOD_LIT_SERIAL table into FOD_LIGHT, lower-casing each
 *  name so the prefix matching in FodSetMyLight is case-insensitive, and note
 *  which entry is the player's hand-held spot.  Both spellings of the name are
 *  accepted, and the first match wins.
 * ------------------------------------------------------------------------ */
static void FodGetLightSerial(FOD_LIGHT *fl)                                   /* 398 */
{
    FOD_LIT_SERIAL *fls;
    u_int          *lit_addr;
    int             i;

    lit_addr = fl->lit_top;                                                     /* 400 */
    fls      = (FOD_LIT_SERIAL *)(lit_addr + 0x10);

    fl->hand_spot_no = -1;                                                      /* 405 */

    for (i = 0; i < (int)fl->all_lit_num; i++)                                  /* 406 */
    {
        fl->lit_serial[i] = *fls;
        StrToLower(fl->lit_serial[i].light_name);

        if (strcmp(fl->lit_serial[i].light_name, "hand_spot") == 0 ||
            strcmp(fl->lit_serial[i].light_name, "hand-spot") == 0)
        {
            if (fl->hand_spot_no == -1)                                         /* 414 */
            {
                fl->hand_spot_no = i;
            }
        }

        fls++;
    }                                                                          /* 420 */
}

/* --------------------------------------------------------------------------
 *  FodGetFirstLight
 *
 *  Build the 36 resident G3DLIGHTs from the light stream's header records and
 *  return the cursor just past them -- which is where the per-frame
 *  FOD_LIT_FRAMEs begin, so the caller stores it as lit_frame_top.
 *
 *  Only amb[0] comes from the file; amb[1..5] are the per-prefix ambient
 *  offsets, cleared here and filled by the scene code.
 *
 *  A directional light's `direction` field is present in the file and ignored:
 *  the type-1 case reads nothing but the colour.
 *
 *  fl->all_lit[i] is re-subscripted for every field rather than held in a
 *  local, which is what functions.txt's local list shows and what the 46
 *  inlined fixed_array range checks in the disassembly come from.
 * ------------------------------------------------------------------------ */
static u_int *FodGetFirstLight(FOD_LIGHT *fl, float *offset)                   /* 434 */
{
    float  cone_deg;
    float  intens;
    int    i;
    int    lit_type;
    u_int *lit_addr;

    lit_addr = fl->lit_top;                                                     /* 440 */
    lit_addr = lit_addr + fl->all_lit_num * 8 + 0x10;                           /* 443 */

    for (i = 0; i < FOD_AMBIENT_MAX; i++)                                       /* 446 */
    {
        fl->amb[i][0] = 0.0f;
        fl->amb[i][1] = 0.0f;
        fl->amb[i][2] = 0.0f;
        fl->amb[i][3] = 0.0f;
    }                                                                          /* 448 */

    g3dxVu0CopyVector(fl->amb[0], ((FOD_LIT_AMB *)lit_addr)->color);
    lit_addr += 8;

    for (i = 0; i < (int)fl->all_lit_num; i++)                                  /* 452 */
    {
        lit_type = FodGetLightType(fl, (int)lit_addr[0]);                        /* 453 */

        switch (lit_type)                                                       /* 454 */
        {
        case 0:
            printf("light type seek errer!!\n");                                /* 456 */
            lit_addr += 8;                                                      /* 457 */
            break;                                                             /* 458 */

        case 1:
            g3dutilSetLightDefault(&fl->all_lit[i], G3DLIGHT_DIRECTIONAL);
            g3dxVu0CopyVector(fl->all_lit[i].vDiffuse,
                              ((FOD_LIT_INF *)lit_addr)->color);
            g3dxVu0CopyVector(fl->all_lit[i].vSpecular,
                              ((FOD_LIT_INF *)lit_addr)->color);

            lit_addr += sizeof(FOD_LIT_INF) / sizeof(*lit_addr);
            break;                                                             /* 466 */

        case 2:
            g3dutilSetLightDefault(&fl->all_lit[i], G3DLIGHT_SPOT);
            g3dxVu0CopyVector(fl->all_lit[i].vDiffuse,
                              ((FOD_LIT_SPOT *)lit_addr)->color);
            g3dxVu0CopyVector(fl->all_lit[i].vSpecular,
                              ((FOD_LIT_SPOT *)lit_addr)->color);
            g3dxVu0CopyVector(fl->all_lit[i].vPosition,
                              ((FOD_LIT_SPOT *)lit_addr)->position);
            g3dxVu0CopyVector(fl->all_lit[i].vDirection,
                              ((FOD_LIT_SPOT *)lit_addr)->interest);

            sceVu0ApplyMatrix(fl->all_lit[i].vPosition, fod_cmn_mtx,
                              fl->all_lit[i].vPosition);
            sceVu0ApplyMatrix(fl->all_lit[i].vDirection, fod_cmn_mtx,
                              fl->all_lit[i].vDirection);
            sceVu0AddVector(fl->all_lit[i].vPosition, fl->all_lit[i].vPosition,
                            offset);
            sceVu0AddVector(fl->all_lit[i].vDirection, fl->all_lit[i].vDirection,
                            offset);

            cone_deg = (((FOD_LIT_SPOT *)lit_addr)->cone * FOD_PI) / 180.0f;     /* 483 */
            intens   = cosf(cone_deg);
            gra3dSetLightIntens(&fl->all_lit[i], intens * intens);               /* 485 */

            /* gra3dSetLightIntens stashes the cone half-angle in afPad0[0];
             * a negative one means the cone folded past straight, so kill the
             * light instead. */
            if (fl->all_lit[i].afPad0[0] < 0.0f)
            {
                gra3dSetLightIntens(&fl->all_lit[i], 0.0f);
            }

            lit_addr += sizeof(FOD_LIT_SPOT) / sizeof(*lit_addr);               /* 495 */
            fl->all_lit[i].fMaxRange = 5000.0f;
            break;                                                             /* 496 */

        case 3:
            g3dutilSetLightDefault(&fl->all_lit[i], G3DLIGHT_POINT);
            g3dxVu0CopyVector(fl->all_lit[i].vDiffuse,
                              ((FOD_LIT_POINT *)lit_addr)->color);
            g3dxVu0CopyVector(fl->all_lit[i].vSpecular,
                              ((FOD_LIT_POINT *)lit_addr)->color);
            g3dxVu0CopyVector(fl->all_lit[i].vPosition,
                              ((FOD_LIT_POINT *)lit_addr)->position);

            lit_addr += sizeof(FOD_LIT_POINT) / sizeof(*lit_addr);

            sceVu0ApplyMatrix(fl->all_lit[i].vPosition, fod_cmn_mtx,
                              fl->all_lit[i].vPosition);
            sceVu0AddVector(fl->all_lit[i].vPosition, fl->all_lit[i].vPosition,
                            offset);

            fl->all_lit[i].fMaxRange = 1500.0f;
            break;                                                             /* 512 */

        default:
            /* The cursor is deliberately not advanced here: the stream is
             * already lost, and the loop is bounded by all_lit_num. */
            printf("Warning!! Unknown Light Data.\n");
            break;
        }
    }                                                                          /* 519 */

    return lit_addr;                                                            /* 520 */
}

/* --------------------------------------------------------------------------
 *  Fodg3dSetLight
 *
 *  Install one light into the module's GRA3DLIGHTDATA and enable its slot.
 *  vPosition[3] is forced to 1 because gra3d treats the light position as a
 *  point, and the FOD file leaves w at whatever the exporter wrote.
 * ------------------------------------------------------------------------ */
static void Fodg3dSetLight(int iLightId, G3DLIGHT *pLight)                     /* 527 */
{
    GRA3DLIGHTDATA *pGra3dLight;                                                /* 528 */

    pGra3dLight = &FodLight;

    pGra3dLight->aLight[iLightId]                = *pLight;
    pGra3dLight->aStatus[iLightId].bEnable       = 1;
    pGra3dLight->aLight[iLightId].vPosition[3]   = 1.0f;
}

/* --------------------------------------------------------------------------
 *  FodSetMyLight
 *
 *  The whole per-object lighting path in one call: pick this object's lights
 *  out of the scene set and push them at gra3d.  `eye` is unused.
 * ------------------------------------------------------------------------ */
void FodSetMyLight(FOD_LIGHT *fl, char *pfx, const float *eye)                 /* 540 */
{
    FodChangeFodLightToGra3dLight(&FodLight, fl, pfx, eye);                     /* 541 */

    gra3dSetAmbient(FodLight.vAmbient);                                         /* 542 */
    gra3dSetLightData(&FodLight, (float *)0);                                   /* 543 */
}

/* --------------------------------------------------------------------------
 *  FodChangeFodLightToGra3dLight
 *
 *  Select the lights that apply to one object and lay them out the way gra3d
 *  wants them: three directional in slots 0-2, three point in 3-5, three spot
 *  from slot 22 (FodSetSpotLights adds that base).
 *
 *  A light is taken if its name starts with `pfx`, or -- for anything but a
 *  character prefix ('c') -- if it starts with "room".  The player's hand-held
 *  spot is a special case handled before the name test and before the type
 *  dispatch: it goes straight into the spot set whatever its serial says.
 *
 *  `eye` is unused, as it is in FodSetMyLight.
 * ------------------------------------------------------------------------ */
void FodChangeFodLightToGra3dLight(GRA3DLIGHTDATA *pGra3dLight, FOD_LIGHT *fl,
                                   char *pfx, const float *eye)                /* 550 */
{
    /* Function-local statics in the ROM too -- .bss 0x4aefa0 / 0x4af0f0 /
     * 0x4af240, each with its own GCC initialisation guard word. */
    static fixed_array<G3DLIGHT, 3> ilight;                                     /* 552 */
    static fixed_array<G3DLIGHT, 3> slight;                                     /* 554 */
    static fixed_array<G3DLIGHT, 3> plight;                                     /* 556 */

    FOD_LIT_SERIAL *fls;
    G3DLIGHT       *org;
    int             i;
    int             il_num;
    int             sl_num;
    int             pl_num;
    int             get_light_flg;
    G3DLIGHTTYPE    iLightType;

    (void)eye;

    il_num = 0;                                                                 /* 559 */
    sl_num = 0;
    pl_num = 0;

    for (i = 0; i < (int)fl->all_lit_num; i++)                                  /* 563 */
    {
        fls = &fl->lit_serial[i];
        org = &fl->all_lit[i];

        get_light_flg = 0;

        if (fl->hand_spot_no == i)                                              /* 565 */
        {
            if (sl_num < 3)                                                     /* 567 */
            {
                slight[sl_num]      = *org;
                slight[sl_num].Type = G3DLIGHT_SPOT;
                sl_num++;                                                       /* 570 */
            }
        }
        else
        {
            if (strncmp(fls->light_name, pfx, strlen(pfx)) == 0)                 /* 572 */
            {
                get_light_flg = 1;                                              /* 574 */
            }
            else if (*pfx != 'c')                                               /* 575 */
            {
                get_light_flg = (strncmp(fls->light_name, "room", 4) == 0);      /* 577 */
            }
        }

        if (get_light_flg)                                                      /* 582 */
        {
            if (fls->light_type == 1)                                           /* 583 */
            {
                if (il_num < 3)                                                 /* 584 */
                {
                    ilight[il_num]      = *org;
                    ilight[il_num].Type = G3DLIGHT_DIRECTIONAL;
                    il_num++;                                                   /* 587 */
                }
            }
            else if (fls->light_type == 2)                                      /* 589 */
            {
                if (sl_num < 3)                                                 /* 590 */
                {
                    slight[sl_num]      = *org;
                    slight[sl_num].Type = G3DLIGHT_SPOT;
                    sl_num++;                                                   /* 593 */
                }
            }
            else if (fls->light_type == 3)                                      /* 595 */
            {
                if (pl_num < 3)                                                 /* 596 */
                {
                    plight[pl_num]      = *org;
                    plight[pl_num].Type = G3DLIGHT_POINT;
                    pl_num++;                                                   /* 599 */
                }
            }
            else
            {
                printf("Warning!! Unknown My Light Type.\n");                   /* 602 */
            }
        }
    }                                                                          /* 605 / 607 */

    /* Reset every gra3d slot to its type-correct default.  This is not
     * utilSetGRA3DLIGHTDATADefault(): that one memsets the whole record, this
     * one clears only bEnable and leaves vAmbient (overwritten just below),
     * aiNumInitial and the other three status flags alone. */
    for (i = 0; i < NUM_GRA3DLIGHTID; i++)                                      /* 611 */
    {
        pGra3dLight->aStatus[i].bEnable = 0;

        if (i - GRA3D_START_LIGHT_DIRECTIONAL < GRA3D_NUM_LIGHT_DIRECTIONAL)     /* 350 */
        {
            iLightType = G3DLIGHT_DIRECTIONAL;                                  /* 351 */
        }
        else if (i - GRA3D_START_LIGHT_POINT < GRA3D_NUM_LIGHT_POINT)            /* 353 */
        {
            iLightType = G3DLIGHT_POINT;
        }
        else if (i - GRA3D_START_LIGHT_SPOT < GRA3D_NUM_LIGHT_SPOT)              /* 355 */
        {
            iLightType = G3DLIGHT_SPOT;
        }
        else
        {
            iLightType = G3DLIGHTTYPE_FORCE_DWORD;
        }

        g3dutilSetLightDefault(&pGra3dLight->aLight[i], iLightType);             /* 350 */
    }                                                                          /* 614 */

    g3dxVu0CopyVector(pGra3dLight->vAmbient, fl->amb[0]);

    if (*pfx == 'c')                                                            /* 618 */
    {
        sceVu0AddVector(pGra3dLight->vAmbient, pGra3dLight->vAmbient, fl->amb[1]);
    }
    else if (*pfx == 'r')                                                       /* 620 */
    {
        sceVu0AddVector(pGra3dLight->vAmbient, pGra3dLight->vAmbient, fl->amb[2]);
    }
    else if (*pfx == 'f')                                                       /* 622 */
    {
        sceVu0AddVector(pGra3dLight->vAmbient, pGra3dLight->vAmbient, fl->amb[3]);
    }
    else if (*pfx == 'd')                                                       /* 624 */
    {
        sceVu0AddVector(pGra3dLight->vAmbient, pGra3dLight->vAmbient, fl->amb[4]);
    }
    else if (*pfx == 'i')                                                       /* 626 */
    {
        sceVu0AddVector(pGra3dLight->vAmbient, pGra3dLight->vAmbient, fl->amb[5]);
    }

    for (i = 0; i < il_num; i++)                                                /* 630 */
    {
        Fodg3dSetLight(i, &ilight[i]);                                          /* 633 */
    }
    for (i = 0; i < pl_num; i++)                                                /* 636 */
    {
        Fodg3dSetLight(i + 3, &plight[i]);                                      /* 639 */
    }
    if (sl_num > 0)                                                             /* 643 */
    {
        FodSetSpotLights(&slight[0], (u_int)sl_num);
    }
}

/* --------------------------------------------------------------------------
 *  FodSetSpotLights
 *
 *  Turn each spot's stored interest point into a unit direction and install it
 *  in the spot block starting at gra3d slot 22.
 * ------------------------------------------------------------------------ */
void FodSetSpotLights(G3DLIGHT *sl, u_int num)
{
    u_int i;

    if (sl != nullptr)                                                    /* 655 */
    {
        for (i = 0; i < num; i++)                                               /* 656 */
        {
            sl[i].vPosition[3] = 1.0f;                                          /* 657 */

            /* PORT DEVIATION -- the ROM subtracts the other way round here,
             * `vPosition - vDirection`, giving a vector from the target back
             * TOWARD the light.  That is the anti-beam sense the VU1's
             * CalcIntens wants, and the ROM is consistent about it.
             *
             * The port is consistent the other way: vDirection is the BEAM
             * engine-wide and the VU1 kernel transcriptions negate it
             * themselves.  This is the FOURTH member of that family, after
             * SceneSetHandSpotLightToPlyrWrk() (scene.c), MapLightSetPlayerReal()
             * (MapLight.c) and MapDrawRoomOne()'s projector (MapDraw.c) -- and
             * it was the one left un-converted.
             *
             * The cost of the half-conversion was a visibly BIDIRECTIONAL
             * flashlight in cutscenes.  One torch reached the frame as two
             * enabled copies pointing 180 degrees apart: this bank installs it
             * anti-beam into the spot slots at GRA3D_START_LIGHT_SPOT, while
             * scene.c installs the same light beam-wise into
             * LID_SPOT_FLASHLIGHT -- disjoint slots, so they coexist rather
             * than overwrite.  During a scene MapDrawEnableFlashlightOnly(0)
             * leaves the room and the cutscene models on this bank while
             * MapLightSetPlayerOnly() re-enables the other copy for MapPut
             * objects, so both cones are on screen in the same frame.
             * vu1/LIGHTING.md section 3.3. */
            sceVu0SubVector(sl[i].vDirection, sl[i].vDirection, sl[i].vPosition);/* 658 */
            sceVu0Normalize(sl[i].vDirection, sl[i].vDirection);                /* 659 */

            Fodg3dSetLight((int)i + GRA3D_START_LIGHT_SPOT, &sl[i]);            /* 661 */
        }                                                                      /* 662 */
    }
}

/* --------------------------------------------------------------------------
 *  FodGetToSgLight
 *
 *  Apply one frame's light animation on top of the resident lights.  The
 *  record is a run of FOD_LIT_ANMs, one per light whose serial has anm_flg
 *  set, each followed by a quadword per channel its anm[] flags select.
 *
 *  Every channel occupies a full quadword, including the scalar cone -- which
 *  is why the cone advances the cursor by 0x10, not by one float.
 *
 *  Unlike FodGetFirstLight, the specular colour here is copied back out of
 *  vDiffuse rather than read from the record a second time.
 *
 *  GCC merged the tails of the type-1 and type-3 cases (both are "copy a
 *  quadword, transform it, advance"), keeping only one copy's line numbers --
 *  so the 739/740/743 annotations below belong to case 3, and case 1's
 *  equivalent statements are left unannotated rather than guessed at.
 * ------------------------------------------------------------------------ */
void FodGetToSgLight(FOD_LIGHT *pFodLignt, FOD_LIT_FRAME *pFodLitFrame, float *offset)        /* 671 */
{
    int    i;
    u_int *lit_addr;
    float *data;

    lit_addr = (u_int *)(pFodLitFrame + 1);                                     /* 678 */

    for (i = 0; i < (int)pFodLignt->all_lit_num; i++)                           /* 680 */
    {
        if (pFodLignt->lit_serial[i].anm_flg != 1)
        {
            continue;
        }

        data = (float *)(lit_addr + 4);

        switch (((FOD_LIT_ANM *)lit_addr)->type)                                /* 688 */
        {
        case 1:
            if (((FOD_LIT_ANM *)lit_addr)->anm[0] != 0)
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vDiffuse, data);
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vSpecular,
                                  pFodLignt->all_lit[i].vDiffuse);
                data += 4;                                                      /* 693 */
            }
            if (((FOD_LIT_ANM *)lit_addr)->anm[2] != 0)                         /* 702 */
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vDirection, data);
                sceVu0ApplyMatrix(pFodLignt->all_lit[i].vDirection, fod_cmn_mtx,
                                  pFodLignt->all_lit[i].vDirection);
                sceVu0AddVector(pFodLignt->all_lit[i].vDirection,
                                pFodLignt->all_lit[i].vDirection, offset);
                data += 4;
            }
            break;

        case 2:
            if (((FOD_LIT_ANM *)lit_addr)->anm[0] != 0)
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vDiffuse, data);
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vSpecular,
                                  pFodLignt->all_lit[i].vDiffuse);
                data += 4;                                                      /* 708 */
            }
            if (((FOD_LIT_ANM *)lit_addr)->anm[1] != 0)
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vPosition, data);
                sceVu0ApplyMatrix(pFodLignt->all_lit[i].vPosition, fod_cmn_mtx,
                                  pFodLignt->all_lit[i].vPosition);             /* 712 */
                sceVu0AddVector(pFodLignt->all_lit[i].vPosition,
                                pFodLignt->all_lit[i].vPosition, offset);       /* 713 */
                data += 4;
            }
            if (((FOD_LIT_ANM *)lit_addr)->anm[2] != 0)
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vDirection, data);
                sceVu0ApplyMatrix(pFodLignt->all_lit[i].vDirection, fod_cmn_mtx,
                                  pFodLignt->all_lit[i].vDirection);            /* 718 */
                sceVu0AddVector(pFodLignt->all_lit[i].vDirection,
                                pFodLignt->all_lit[i].vDirection, offset);      /* 719 */
                data += 4;
            }
            if (((FOD_LIT_ANM *)lit_addr)->anm[3] != 0)
            {
                float intens = cosf((data[0] * FOD_PI) / 180.0f);               /* 724 */

                gra3dSetLightIntens(&pFodLignt->all_lit[i], intens * intens);    /* 726 */
                data += 4;
            }
            break;                                                             /* 729 */

        case 3:
            if (((FOD_LIT_ANM *)lit_addr)->anm[0] != 0)
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vDiffuse, data);
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vSpecular,
                                  pFodLignt->all_lit[i].vDiffuse);
                data += 4;                                                      /* 735 */
            }
            if (((FOD_LIT_ANM *)lit_addr)->anm[1] != 0)
            {
                g3dxVu0CopyVector(pFodLignt->all_lit[i].vPosition, data);
                sceVu0ApplyMatrix(pFodLignt->all_lit[i].vPosition, fod_cmn_mtx,
                                  pFodLignt->all_lit[i].vPosition);             /* 739 */
                sceVu0AddVector(pFodLignt->all_lit[i].vPosition,
                                pFodLignt->all_lit[i].vPosition, offset);       /* 740 */
                data += 4;
            }
            break;                                                             /* 743 */

        default:
            printf("Warning!!  Unknown Light Type.\n");                         /* 746 */
            break;
        }

        lit_addr = (u_int *)data;                                               /* 749 */
    }                                                                          /* 750 */
}

/* --------------------------------------------------------------------------
 *  FodGetDropSpotPos
 *
 *  Hand back the position and interest point of the "<pfx>_drop_spot" light,
 *  the one a scene uses to place a projected shadow or a floor pool.  The scan
 *  keeps the *last* match rather than breaking on the first.
 *
 *  Dead code: exported, but no jal site anywhere in the loadable segments.
 * ------------------------------------------------------------------------ */
void FodGetDropSpotPos(FOD_LIGHT *fl, char *pfx, float *lp, float *li)         /* 893 */
{
    FOD_LIT_SERIAL *fls;
    int             i;
    int             no;
    float          *pv1;

    no = -1;                                                                    /* 896 */

    for (i = 0; i < (int)fl->all_lit_num; i++)                                  /* 898 */
    {
        fls = &fl->lit_serial[i];

        /* Four prefix characters, one separator, then the tag. */
        if (strncmp(pfx, fls->light_name, 4) == 0 &&                             /* 900 */
            strncmp("drop_spot", fls->light_name + 5, 9) == 0)
        {
            no = i;                                                             /* 901 */
        }
    }

    if (no < 0)                                                                 /* 905 */
    {
        printf("Can't Get Drop Spot!!\n");                                      /* 906 */
        return;
    }

    pv1 = fl->all_lit[no].vPosition;
    g3dxVu0CopyVector(lp, pv1);
    pv1 = fl->all_lit[no].vDirection;
    g3dxVu0CopyVector(li, pv1);
}                                                                              /* 907 */

/* --------------------------------------------------------------------------
 *  FodGetLightType
 *
 *  Look a light's type up by the light_no its stream record carries.  Like
 *  FodGetDropSpotPos above, it keeps scanning and returns the last match; 0
 *  means "not found", which the callers treat as a malformed record.
 * ------------------------------------------------------------------------ */
static int FodGetLightType(FOD_LIGHT *fl, int id)                              /* 922 */
{
    int i;
    int ret_type;

    ret_type = 0;

    for (i = 0; i < (int)fl->all_lit_num; i++)                                  /* 924 */
    {
        if (fl->lit_serial[i].light_no == id)
        {
            ret_type = fl->lit_serial[i].light_type;
        }
    }                                                                          /* 928 */

    return ret_type;                                                            /* 929 */
}

/* --------------------------------------------------------------------------
 *  FodGetFirstCam
 *
 *  Set the scene's opening camera from the camera stream's header record.
 *
 *  Both angles are folded once into (-PI, PI] after conversion -- one step
 *  only, matching RotLimitChk2's behaviour elsewhere.
 * ------------------------------------------------------------------------ */
void FodGetFirstCam(FOD_CAMERA_DATA *pFodCam, FOD_CTRL *pFodCtrl, float *offset)
{
    FOD_FIRST_CAM *pFodFirst;

    pFodFirst = (FOD_FIRST_CAM *)(pFodCtrl->cam_file_hdr + 1);                  /* 946 */

    g3dxVu0CopyVector(pFodCam->vPosition, pFodFirst->p);
    g3dxVu0CopyVector(pFodCam->vTarget,   pFodFirst->i);

    pFodCam->fRoll = pFodFirst->roll * FOD_ANGLE_SCALE;                         /* 950 */

    sceVu0ApplyMatrix(pFodCam->vPosition, fod_cmn_mtx, pFodCam->vPosition);     /* 951 */
    sceVu0ApplyMatrix(pFodCam->vTarget,   fod_cmn_mtx, pFodCam->vTarget);       /* 952 */
    sceVu0AddVector(pFodCam->vPosition, pFodCam->vPosition, offset);            /* 953 */
    sceVu0AddVector(pFodCam->vTarget,   pFodCam->vTarget,   offset);            /* 954 */

    if (FOD_PI < pFodCam->fRoll)                                                /* 958 */
    {
        pFodCam->fRoll -= FOD_TWO_PI;
    }

    pFodCam->fFov = pFodFirst->fov * FOD_ANGLE_SCALE;                           /* 960 */
    if (FOD_PI < pFodCam->fFov)                                                 /* 961 */
    {
        pFodCam->fFov -= FOD_TWO_PI;
    }

    /* Both stores carry line 962 -- the ROM wrote them on one source line. */
    pFodCam->fNearZ = pFodFirst->near;                                          /* 962 */
    pFodCam->fFarZ  = pFodFirst->far;                                           /* 962 */
}                                                                              /* 963 */

/* --------------------------------------------------------------------------
 *  FodGetCamData
 *
 *  Apply one frame's camera animation.  anm[0..5] select position, target,
 *  roll, fov, near and far; each present channel follows in order, a quadword
 *  for the two vectors and a single float for the four scalars.
 *
 *  Roll converts as `deg * PI / 180`, fov as `deg * (PI/180)` -- two different
 *  .lit4 slots and two different expressions in the ROM for the same
 *  conversion.  Reproduced as found.
 * ------------------------------------------------------------------------ */
void FodGetCamData(FOD_CAMERA_DATA *pFodCam, FOD_CAM_FRAME *pFodCamFrame,
                   float *offset)                                              /* 970 */
{
    float  CamPos[4];
    float  CamTgt[4];
    float  CamRoll;
    float *fdat;
    int    i;

    fdat = (float *)(pFodCamFrame + 1);                                         /* 976 */

    for (i = 0; i < 6; i++)                                                     /* 977 */
    {
        if (pFodCamFrame->anm[i] != 1)                                          /* 979 */
        {
            continue;
        }

        switch (i)                                                              /* 985 */
        {
        case 0:
            g3dxVu0CopyVector(CamPos, fdat);                                    /* 986 */
            sceVu0ApplyMatrix(CamPos, fod_cmn_mtx, CamPos);                     /* 989 */
            sceVu0AddVector(CamPos, CamPos, offset);
            g3dxVu0CopyVector(pFodCam->vPosition, CamPos);
            fdat += 4;                                                          /* 995 */
            break;                                                             /* 996 */

        case 1:
            g3dxVu0CopyVector(CamTgt, fdat);                                    /* 999 */
            sceVu0ApplyMatrix(CamTgt, fod_cmn_mtx, CamTgt);                     /* 1001 */
            sceVu0AddVector(CamTgt, CamTgt, offset);                            /* 1002 */
            g3dxVu0CopyVector(pFodCam->vTarget, CamTgt);
            fdat += 4;                                                          /* 1006 */
            break;

        case 2:
            CamRoll = (fdat[0] * FOD_PI) / 180.0f;                              /* 1010 */
            if (FOD_PI < CamRoll)                                               /* 1012 */
            {
                pFodCam->fRoll = CamRoll - FOD_TWO_PI;
            }
            else
            {
                pFodCam->fRoll = CamRoll;                                       /* 1015 */
            }
            fdat++;                                                             /* 1016 */
            break;                                                             /* 1017 */

        case 3:
            pFodCam->fFov = fdat[0] * FOD_ANGLE_SCALE;                          /* 1022 */
            if (FOD_PI < pFodCam->fFov)                                         /* 1024 */
            {
                pFodCam->fFov -= FOD_TWO_PI;
            }
            fdat++;                                                             /* 1028 */
            break;                                                             /* 1029 */

        case 4:
            pFodCam->fNearZ = fdat[0];                                          /* 1031 */
            fdat++;
            break;

        case 5:
            pFodCam->fFarZ = fdat[0];                                           /* 1035 */
            fdat++;
            break;
        }
    }
}                                                                              /* 1041 */

/* --------------------------------------------------------------------------
 *  FodGetFixEffect
 *
 *  Index the effect stream's fixed (whole-scene) effect records and return the
 *  cursor just past them, where the per-frame FOD_EFF_FRAMEs begin.
 *
 *  Two ROM quirks are preserved here.  `fix_eff_num` comes straight out of the
 *  file with no clamp, so a stream declaring more than 12 fixed effects writes
 *  past eff_param.fix[] into lenz_pos and beyond.  And the two strides
 *  disagree: `fed++` steps sizeof(FOD_EFF_DATA) = 0x1c, inflated by the one
 *  oversized union member (FOD_EF_NEGA), while the returned cursor and the
 *  records in the file are 0x10 apart -- so fix[1] onwards point past their
 *  records.
 * ------------------------------------------------------------------------ */
static u_int *FodGetFixEffect(u_int *tep)
{
    FOD_EFF_DATA *fed;
    u_int         i;

    if (tep[3] == 0)                                                            /* 1140 */
    {
        eff_param.fix_eff_num = 0;                                              /* 1141 */
        return tep + 4;                                                         /* 1142 */
    }

    memset(&eff_param, 0, sizeof(eff_param));                                   /* 1144 */

    fed                   = (FOD_EFF_DATA *)&tep[8];                            /* 1147 */
    eff_param.fix_eff_num = tep[4];                                             /* 1148 */
    eff_param.pdf_p       = nullptr;                                          /* 1149 */

    for (i = 0; i < eff_param.fix_eff_num; i++)                                 /* 1151 */
    {
        eff_param.fix[i] = fed;                                                 /* 1152 */
        fed++;                                                                  /* 1153 */
    }                                                                          /* 1154 */

    tep += 4 + eff_param.fix_eff_num * 4;                                       /* 1155 */

    return tep + 4;                                                             /* 1156 */
}                                                                              /* 1157 */

GRA3DLIGHTDATA *FodGetGra3DLight(void)
{
    return &FodLight;                                                          /* 1164 */
}
