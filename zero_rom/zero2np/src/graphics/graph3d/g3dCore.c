/* ==========================================================================
 *  g3dCore.c
 *
 *  The heart of the zero2np 3D engine: it owns the single G3DCOREOBJECT
 *  (s_pObject) that mirrors the renderer's entire fixed-function state -- the
 *  render/global state words, the global ambient, the material, the five
 *  transform matrices, the nine light slots, the bound texture and the GS
 *  register file -- and keeps the VU1 micro-memory image (Vu1Mem) in sync as
 *  state is pushed in.
 *
 *  Most of the per-vector / per-matrix work below is VU0 macro-mode code that
 *  the compiler inlined from the SCE EE VU0 library (libvu0.h) and the engine's
 *  own g3dxVu0.h helpers.  Where an inlined block is plainly a standard
 *  matrix/vector primitive it is written here as the corresponding sceVu0* /
 *  g3dxVu0* call (the same convention gra3dSGDData.c / gra3dShadow.c use).
 *
 *  The per-lighting-type kernels (_Vu0LoadMaterial*, _Vu0CalcColorData*,
 *  _Vu0LoadColorData*, _Vu0LoadColorCoeff*, _Vu0CalcVertexColor*Light*) are
 *  selected through the s_apf* function-pointer tables, indexed by the current
 *  G3DLIGHTINGTYPE (constant / lambert / phong).
 *
 *  Where those 21 kernels live, and why they are here
 *  --------------------------------------------------
 *  In the ROM they are *not* part of g3dCore.c.  The stabs mark them
 *  `SOL g3dLight.h`, prototyped around lines 57-95 of that header and defined
 *  around lines 133-1500; eighteen were `inline` (hence one
 *  `.gnu.linkonce.t._Vu0*` section each at 0x2b4b68..0x2b572c, emitted only
 *  into g3dCore.o -- no other translation unit used them) and the three
 *  point-light ones were plain `static`, so they kept ordinary .text addresses
 *  at 0x0019d778 / 0x0019d888 / 0x0019d9d8.
 *
 *  They stay in g3dCore.c here because this reconstruction puts G3DMATERIAL and
 *  the G3DVU1COLOR_* blocks in gra3dTypes.h, which already includes g3dLight.h
 *  -- moving the bodies into that header would close an include cycle.  Nothing
 *  outside this file calls them, so the only cost is the misplaced home.
 *
 *  Because the kernel bodies are single `asm` statements in g3dLight.h, their
 *  `; Line NNN` markers count in *that* header, not in g3dCore.c -- 1058 for
 *  _Vu0CalcVertexColorPointLightConstant sits inside g3dCalcVertexColor's own
 *  1008-1060 span.  Only the g3dCore.c functions carry trailing ROM-line
 *  annotations below; the kernels deliberately carry none.
 *
 *  Shift-JIS assert messages are documented with the decoded Japanese in a
 *  comment above the assert.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "g3dCore.h"
#include "g3dDma.h"             /* g3dDmaOpenPacket / ClosePacket / SetGsRegister */
#include "g3dTexture.h"        /* CTexture::PreLoad / GetTextureDataRef */
#include "g3ddbg.h"
#include "g3dDebug.h"           /* g3ddbgVerifyVu1MemAddress / g3ddbgVerifyGsRegisterAddress */
#include "gra3dConst.h"        /* g_v0000 / g_matUnit / g_NullMaterial */
#include "g3dMath.h"           /* g3dClampf */
#include "g3dxVu0.h"            /* EqualMemory128 + VU0 macro-mode intrinsics */
#include <string.h>            /* memcmp */
#include <mathf.h>             /* cosf / powf */
#include "libvu0.h"

/* --------------------------------------------------------------------------
 *  The VU0 register file, made explicit.
 *
 *  On the EE the kernels below exchanged everything through physical VU0
 *  registers, which is why their ROM signatures carry nothing but a
 *  destination pointer: _Vu0LoadMaterial* lqc2'd the material into vf3/vf4/vf5,
 *  _Vu0LoadColorData* the per-light colours into vf25..vf31, g3dCalcVertexColor
 *  staged the geometry in vf7..vf16 plus the t0/t1/t5 flag words, and
 *  _Vu0LoadColorCoeff* left the N.L / N.H terms in vf17/vf18 with their masks
 *  in t3/t4.  A decompile cannot carry a cross-call register data path, so the
 *  two register sets travel as explicit arguments instead.  Every field keeps
 *  the register name it stands for so the disassembly still reads against this
 *  file, and each is still written by the ROM function that loaded the register
 *  it replaces, at the point that function is called.
 *
 *  Two independent chains use the file, so they get one struct each:
 *  _CalcColor drives the material set, g3dCalcVertexColor the lighting set.
 * ------------------------------------------------------------------------ */
typedef struct                          /* _CalcColor's chain               */
{
    sceVu0FVECTOR vf3;                  /* Material.vDiffuse                */
    sceVu0FVECTOR vf4;                  /* Material.vAmbient                */
    sceVu0FVECTOR vf5;                  /* Material.vSpecular; .w = fPower  */
} G3DVU0MATERIALREGS;

typedef struct                          /* g3dCalcVertexColor's chain       */
{
    /* ---- staged by g3dCalcVertexColor, once per vertex ----------------- */
    sceVu0FVECTOR vf7;                  /* unit world-space vertex normal   */
    sceVu0FVECTOR vf11;                 /* unit camera->vertex dir (phong)  */
    u_int         t5;                   /* Light.status.lAS enable bits     */

    /* ---- staged by g3dCalcVertexColor, once per light group ------------ */
    sceVu0FVECTOR avf8_10[3];           /* vf8/vf9/vf10: unit light->vertex */
    sceVu0FVECTOR vf15;                 /* xyz: per-light distance atten.   */
    sceVu0FVECTOR vf16;                 /* xyz: per-light spot cone falloff */
    u_int         t0;                   /* skip mask derived from vf15      */
    u_int         t1;                   /* skip mask derived from vf16      */

    /* ---- written by _Vu0LoadColorCoeff* -------------------------------- */
    sceVu0FVECTOR vf17;                 /* xyz: per-light N.L               */
    sceVu0FVECTOR vf18;                 /* xyz: per-light N.H               */
    u_int         t3;                   /* skip mask derived from vf17      */
    u_int         t4;                   /* skip mask derived from vf18      */

    /* ---- written by _Vu0LoadColorData* --------------------------------- */
    sceVu0FVECTOR avAmbient[G3D_MAX_LIGHT_PER_TYPE];    /* vf25/vf26/vf27   */
    sceVu0FVECTOR avDiffuse[G3D_MAX_LIGHT_PER_TYPE];    /* vf28/vf29/vf30   */
    sceVu0FVECTOR avSpecular[G3D_MAX_LIGHT_PER_TYPE];   /* vf31/vf21/vf22   */
} G3DVU0LIGHTREGS;

/* --------------------------------------------------------------------------
 *  Per-lighting-type kernel dispatch.
 *
 *  Each of the three lighting models (G3DLT_CONSTANT / _LAMBERT / _PHONG)
 *  supplies one routine of each kind; the tables are indexed by the current
 *  G3DGS_LIGHTINGTYPE.  The ROM's own signatures are the trailing arguments;
 *  the leading register-file pointer is this port's addition.
 * ------------------------------------------------------------------------ */
typedef void (*LPFUNC_CALCCOLORDATA)(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL);
typedef void (*LPFUNC_LOADMATERIAL)(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat);
typedef void (*LPFUNC_LOADCOLOR)(G3DVU0LIGHTREGS *r, const void *pCD);
typedef void (*LPFUNC_LOADCOLORCOEFF)(G3DVU0LIGHTREGS *r);
typedef void (*LPFUNC_CALCVERTEXCOLOR)(float *vDestColor, const G3DVU0LIGHTREGS *r);

/* VIF1 FLUSH opcode (cmd byte 0x11) emitted ahead of the DIRECT GIFTAG. */
#define VIF1_FLUSH              0x11000000

/* Engine sizeof helpers used by the GS-register bounds asserts. */
#define DWSIZEOF(t)                     (sizeof(t) / 8)
#define memberarraysizeof(t, m)         (sizeof(((t *)0)->m) / sizeof(((t *)0)->m.alReg[0]))

/* The renderer supports a single texture stage. */
#define G3D_MAX_TEXTURESTAGE    1

/* --------------------------------------------------------------------------
 *  Module state (see globals.txt).
 * ------------------------------------------------------------------------ */
static G3DCOREOBJECT *s_pObject;        /* the one engine object */

/* ----- forward declarations for the kernels referenced by the tables ----- */
static void _Vu0LoadMaterialConstant(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat);
static void _Vu0LoadMaterialLambert(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat);
static void _Vu0LoadMaterialPhong(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat);
static void _Vu0CalcColorDataConstant(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL);
static void _Vu0CalcColorDataLambert(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL);
static void _Vu0CalcColorDataPhong(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL);
static void _Vu0LoadColorDataConstant(G3DVU0LIGHTREGS *r, const void *pCD);
static void _Vu0LoadColorDataLambert(G3DVU0LIGHTREGS *r, const void *pCD);
static void _Vu0LoadColorDataPhong(G3DVU0LIGHTREGS *r, const void *pCD);
static void _Vu0LoadColorCoeffConstant(G3DVU0LIGHTREGS *r);
static void _Vu0LoadColorCoeffLambert(G3DVU0LIGHTREGS *r);
static void _Vu0LoadColorCoeffPhong(G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorDirectinalLightConstant(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorDirectinalLightLambert(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorDirectinalLightPhong(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorPointLightConstant(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorPointLightLambert(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorPointLightPhong(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorSpotLightConstant(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorSpotLightLambert(float *vDestColor, const G3DVU0LIGHTREGS *r);
static void _Vu0CalcVertexColorSpotLightPhong(float *vDestColor, const G3DVU0LIGHTREGS *r);

/* ----- dispatch tables, indexed by G3DLIGHTINGTYPE ---------------------- */
static LPFUNC_CALCCOLORDATA   s_apfCalcColorData[3] =
{
    _Vu0CalcColorDataConstant,
    _Vu0CalcColorDataLambert,
    _Vu0CalcColorDataPhong,
};
static LPFUNC_LOADMATERIAL    s_apfLoadMaterial[3] =
{
    _Vu0LoadMaterialConstant,
    _Vu0LoadMaterialLambert,
    _Vu0LoadMaterialPhong,
};
static LPFUNC_LOADCOLOR       s_apfLoadColor[3] =
{
    _Vu0LoadColorDataConstant,
    _Vu0LoadColorDataLambert,
    _Vu0LoadColorDataPhong,
};
static LPFUNC_LOADCOLORCOEFF  s_apfLoadColorCoeff[3] =
{
    _Vu0LoadColorCoeffConstant,
    _Vu0LoadColorCoeffLambert,
    _Vu0LoadColorCoeffPhong,
};
static LPFUNC_CALCVERTEXCOLOR s_apfCalcVertexColorDirectionalLight[3] =
{
    _Vu0CalcVertexColorDirectinalLightConstant,
    _Vu0CalcVertexColorDirectinalLightLambert,
    _Vu0CalcVertexColorDirectinalLightPhong,
};
static LPFUNC_CALCVERTEXCOLOR s_apfCalcVertexColorPointLight[3] =
{
    _Vu0CalcVertexColorPointLightConstant,
    _Vu0CalcVertexColorPointLightLambert,
    _Vu0CalcVertexColorPointLightPhong,
};
static LPFUNC_CALCVERTEXCOLOR s_apfCalcVertexColorSpotLight[3] =
{
    _Vu0CalcVertexColorSpotLightConstant,
    _Vu0CalcVertexColorSpotLightLambert,
    _Vu0CalcVertexColorSpotLightPhong,
};

/* --------------------------------------------------------------------------
 *  g3dGetLightType
 *
 *  Map a 0..8 light slot id to its G3DLIGHTTYPE (inlined at the call sites in
 *  the prototype; kept as a helper here for readability).
 * ------------------------------------------------------------------------ */
static G3DLIGHTTYPE g3dGetLightType(int iLightId)
{
    if (iLightId <= G3DLIDX_DIRECTIONAL_2)
    {
        return G3DLIGHT_DIRECTIONAL;
    }

    if (iLightId <= G3DLIDX_POINT_2)
    {
        return G3DLIGHT_POINT;
    }

    if (iLightId <= G3DLIDX_SPOT_2)
    {
        return G3DLIGHT_SPOT;
    }

    return G3DLIGHTTYPE_FORCE_DWORD;
}

/* --------------------------------------------------------------------------
 *  _CalcColor   [ROM g3dCore.c 147-161]
 *
 *  Re-derive the per-light colour data (ambient/diffuse/specular) in VU1
 *  memory from the material and the light colours, dispatched on the current
 *  lighting type: the material is loaded into the VU0 register file once, then
 *  folded into each of the three light groups in turn.  Called whenever the
 *  material or the light set changes.
 *
 *  Notes on the reconstruction:
 *
 *  - The assert's stringified condition survives in .rodata at 0x3aeb20 as
 *    `s_pObject->auiGlobalState[ G3DGS_LIGHTINGTYPE ] < NUM_G3DLIGHTINGTYPE` --
 *    the spacing inside the brackets is the ROM source's own.  The message sits
 *    at 0x3aeb68 and __LINE__ arrives as `li a1,0x94` = 148.
 *
 *  - Statements 151 and 152 materialise &Color (s_pObject + 0x810) and
 *    &ColorOrigin (+0xbc0) into callee-saved registers ahead of the first call;
 *    the ROM never computes &Vu1Mem.Packed (+0x690) at all.  Neither carries a
 *    stab, but that does not rule out named pointers: GCC drops the stab for a
 *    pointer it folds into an address expression, which is exactly why
 *    _CalcAmbient's inlined helper lists pv0 and pv2 but not pv1.  Only 151
 *    emits an instruction; 152's single addiu sank into the delay slot at
 *    0x19b59c, leaving its note standing on 157's first instruction.
 *
 *  - Lines 159-161 reload each group base from s_pObject rather than stepping
 *    the two pointers, so `&pColor->point` and `&...Packed.Color.point` are
 *    indistinguishable here -- both are one addiu off a live base.
 *
 *  - The epilogue is tagged line 138, below the function's own opening 147.
 *    That is an inline's closing brace leaking onto the return, not a statement
 *    here: g3dInitialize, g3dSetAmbient and g3dSetTransform all carry the same
 *    138 on their epilogues.  Ghidra's `; Line` numbers for this function
 *    (1649 on the prologue, 414 in the assert) have no SOL behind them and
 *    disagree with the stabs; symbols.txt is the authority.
 * ------------------------------------------------------------------------ */
static void _CalcColor(void)
{
    G3DVU0MATERIALREGS  r;
    G3DVU1COLOR        *pColor;
    const G3DVU1COLOR  *pColorOrigin;
    int                 iLightingType;

    G3DASSERT(s_pObject->auiGlobalState[G3DGS_LIGHTINGTYPE] < NUM_G3DLIGHTINGTYPE, "memory illegal access occured");                             /* 148 */

    pColor        = &s_pObject->Vu1Mem.Packed.Color;                        /* 151 */
    pColorOrigin  = &s_pObject->Vu1Mem.Packed.ColorOrigin;                  /* 152 */

    iLightingType = s_pObject->auiGlobalState[G3DGS_LIGHTINGTYPE];          /* 155 */

    /* Load the material constants into the VU0 register file once... */
    s_apfLoadMaterial[iLightingType](&r, &s_pObject->Vu1Mem.Packed.Material); /* 157 */

    /* ...then fold each light type's source colours into the working set. */
    s_apfCalcColorData[iLightingType](&r, &pColor->dir,   &pColorOrigin->dir);   /* 159 */
    s_apfCalcColorData[iLightingType](&r, &pColor->point, &pColorOrigin->point); /* 160 */
    s_apfCalcColorData[iLightingType](&r, &pColor->spot,  &pColorOrigin->spot);  /* 161 */
}

/* --------------------------------------------------------------------------
 *  _CalcAmbient   [ROM g3dCore.c 177-181]
 *
 *  Calc.vAmbient = vAmbient(global) * Material.vAmbient + Material.vEmissive --
 *  the constant term added to every lit vertex, refreshed whenever the global
 *  ambient or the material changes.
 *
 *  Notes on the reconstruction:
 *
 *  - Both steps are xyz-masked (`vmul.xyz` then `vadd.xyz`), so Calc.vAmbient.w
 *    is never written and vWork.w is never initialised.  That is only safe
 *    because the second step masks w out as well.  Reproduced as found: the
 *    quadword is uploaded to VU1 memory whole, so the w lane is live there --
 *    an earlier pass folded all four lanes and clobbered it.
 *
 *  - The multiply is the g3dxVu0.h 229-230 inline and the add the 172-173 one.
 *    The object contains no `jal` at all, so neither is a libvu0 call.
 *
 *  - Only line 180 is measured; 181 is interpolated.  GCC deletes a line note
 *    that is immediately followed by another with no instruction between, and
 *    the second statement's note sits between the first expansion's end note
 *    and the second's start note -- all three collapsed onto g3dxVu0.h 172 at
 *    0x19b610, which is also why no SOL switches back to g3dCore.c here.
 *
 *  - functions.txt's `pv0` / `pv2` for this function are the inlined helper's
 *    own parameters surviving in the caller's stabs, not locals of
 *    _CalcAmbient; `pv1` is absent because GCC folded it into vWork's frame
 *    address.
 * ------------------------------------------------------------------------ */
static void _CalcAmbient(void)
{
    sceVu0FVECTOR vWork;

    g3dxVu0MulVectorXYZ(vWork,
                        s_pObject->vAmbient,
                        s_pObject->Vu1Mem.Packed.Material.vAmbient);        /* 180 */
    g3dxVu0AddVectorXYZ(s_pObject->Vu1Mem.Packed.Calc.vAmbient,
                        vWork,
                        s_pObject->Vu1Mem.Packed.Material.vEmissive);       /* 181 */
}

/* --------------------------------------------------------------------------
 *  _ApplyLightDirectional   [ROM g3dCore.c 219-245]
 *
 *  For each of the three directional slots: set the per-light enable bit in the
 *  VU1 light status word, then either upload the slot's ambient/diffuse/
 *  specular "origin" colours and the normalised light direction, or fill all
 *  four with vf0 when the light is disabled.
 *
 *  Notes on the reconstruction:
 *
 *  - The slot index is plain `i`, not `G3D_START_LIGHT_DIRECTIONAL + i`.  Both
 *    fold to the same code (the constant is 0), but _ApplyLightPoint and
 *    _ApplyLightSpot each carry an `iId` local for their own offset and this
 *    function's stabs have none -- so the original indexed with `i` directly.
 *    The status bit is `i` as well: directional occupies bits 0-2, point 4-6
 *    and spot 8-10 (see G3DVU1LIGHTACTIVITYSTATUS).
 *
 *  - There is no `_LIGHTDATA *` local either.  The ROM re-reads s_pObject
 *    inside the branch (`lw a3,-0x4ce8(gp)` on line 231) rather than holding a
 *    record pointer; functions.txt lists only rVu1Mem, i and bEnable, plus the
 *    inlined copy helper's own pv0/pv1 parameters.
 *
 *  - The colour copies are the g3dxVu0.h 134-135 inline (lq/sq through a GPR),
 *    not a call to sceVu0CopyVector -- the object contains no jal at all.  The
 *    normalise is the g3dxVu0.h 477/481 inline, which forces w to 0
 *    (`vsubw.w vf5,vf0,vf0`); that is sceVu0Normalize's semantics, and is how
 *    g3dLightEx.c already spells the same expansion.
 *
 *  - Only lines 231 and 240-243 are measured for the two arms; 232/233 and 236
 *    are interpolated into the gap, because the copy inline swallows the
 *    caller's line note once it has switched the current file to g3dxVu0.h.
 *    236 rather than 235 because the `} else {` shape is fixed across the three
 *    functions: _ApplyLightPoint's last then-statement is 272 against a first
 *    else-statement of 276, and _ApplyLightSpot's are 315 against 319, so the
 *    last then-statement here is 240 - 4.  234-235 hold no code.
 * ------------------------------------------------------------------------ */
static void _ApplyLightDirectional(void)
{
    G3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pObject->Vu1Mem.Direct;             /* 221 */
    int                     i;
    int                     bEnable;

    for (i = 0; i < G3D_NUM_LIGHT_DIRECTIONAL; i++)                         /* 222 */
    {
        bEnable = s_pObject->aLightData[i].bEnable;                         /* 225 */

        /* The two tests are separate in the ROM: the clear is threaded into
         * the set and only the merged result is stored (one `sd` at 0x338). */
        if (bEnable == 0)                                                   /* 227 */
        {
            rVu1Mem.lightstatus.lAS &= ~(1 << i);
        }
        else
        {
            rVu1Mem.lightstatus.lAS |= (1 << i);
        }

        if (bEnable != 0)                                                   /* 229 */
        {
            g3dxVu0CopyVector(rVu1Mem.avAmbientDirectionalOrigin[i],
                              s_pObject->aLightData[i].L.vAmbient);         /* 231 */
            g3dxVu0CopyVector(rVu1Mem.avDiffuseDirectionalOrigin[i],
                              s_pObject->aLightData[i].L.vDiffuse);         /* 232 */
            g3dxVu0CopyVector(rVu1Mem.avSpecularDirectionalOrigin[i],
                              s_pObject->aLightData[i].L.vSpecular);        /* 233 */

            /* normalise the light direction into the VU1 image */
            sceVu0Normalize(rVu1Mem.avDirectionDirectional[i],
                            s_pObject->aLightData[i].L.vDirection);         /* 236 */
        }
        else
        {
            /* vf0, i.e. (0,0,0,1) -- see g3dxVu0ClearVector.  Not g_v0000:
             * the disabled slot keeps w == 1. */
            g3dxVu0ClearVector(rVu1Mem.avAmbientDirectionalOrigin[i]);      /* 240 */
            g3dxVu0ClearVector(rVu1Mem.avDiffuseDirectionalOrigin[i]);      /* 241 */
            g3dxVu0ClearVector(rVu1Mem.avSpecularDirectionalOrigin[i]);     /* 242 */
            g3dxVu0ClearVector(rVu1Mem.avDirectionDirectional[i]);          /* 243 */
        }
    }                                                                       /* 245 */
}

/* --------------------------------------------------------------------------
 *  _ApplyLightPoint   [ROM g3dCore.c 252-284]
 *
 *  As _ApplyLightDirectional, for the three point slots: upload colours, the
 *  light position, the max range and the 1/(max-min) range inverse -- or fill
 *  all four vectors with vf0 and both scalars with 0 when the slot is off.
 *
 *  Notes on the reconstruction, beyond the ones on _ApplyLightDirectional
 *  (which apply here unchanged -- lq/sq copies rather than sceVu0CopyVector,
 *  vf0 rather than g_v0000, s_pObject re-read inside the branch rather than
 *  held in a _LIGHTDATA *):
 *
 *  - `iId` is a real local here (functions.txt: v0), because point lights start
 *    at slot 3.  The status bit is computed separately from it, as i + 4:
 *    directional occupies bits 0-2, point 4-6 and spot 8-10, with a pad bit
 *    between each group (see G3DVU1LIGHTACTIVITYSTATUS).  Whether the source
 *    spelled that off `iId` or off a per-type bit base is not recoverable.
 *
 *  - Both arms write the range inverse *before* the max range (0x3b0 then
 *    0x3a0), which is the reverse of their order in the VU1 image.
 *
 *  - The range inverse is an unguarded `div.s` against 1.0f materialised by
 *    `lui at,0x3f80`; a light with fMaxRange == fMinRange divides by zero.
 *
 *  - Lines 265 and 271/272/276-279/281/282 are measured; 266-268 are
 *    interpolated, as in _ApplyLightDirectional.  269-270, 273-275, 280 and
 *    283 hold no code.
 * ------------------------------------------------------------------------ */
static void _ApplyLightPoint(void)
{
    G3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pObject->Vu1Mem.Direct;             /* 254 */
    int                     i;
    int                     iId;
    int                     bEnable;

    for (i = 0; i < G3D_NUM_LIGHT_POINT; i++)                               /* 256 */
    {
        iId     = G3D_START_LIGHT_POINT + i;                                /* 258 */
        bEnable = s_pObject->aLightData[iId].bEnable;                       /* 259 */

        if (bEnable == 0)                                                   /* 261 */
        {
            rVu1Mem.lightstatus.lAS &= ~(1 << (G3D_START_LIGHT_POINT + 1 + i));
        }
        else
        {
            rVu1Mem.lightstatus.lAS |= (1 << (G3D_START_LIGHT_POINT + 1 + i));
        }

        if (bEnable != 0)                                                   /* 263 */
        {
            g3dxVu0CopyVector(rVu1Mem.avAmbientPointOrigin[i],
                              s_pObject->aLightData[iId].L.vAmbient);       /* 265 */
            g3dxVu0CopyVector(rVu1Mem.avDiffusePointOrigin[i],
                              s_pObject->aLightData[iId].L.vDiffuse);       /* 266 */
            g3dxVu0CopyVector(rVu1Mem.avSpecularPointOrigin[i],
                              s_pObject->aLightData[iId].L.vSpecular);      /* 267 */
            g3dxVu0CopyVector(rVu1Mem.avPositionPoint[i],
                              s_pObject->aLightData[iId].L.vPosition);      /* 268 */

            rVu1Mem.vMax_Min_RangeInversePoint[i] =
                1.0f / (s_pObject->aLightData[iId].L.fMaxRange
                        - s_pObject->aLightData[iId].L.fMinRange);          /* 271 */
            rVu1Mem.vMaxRangePoint[i] = s_pObject->aLightData[iId].L.fMaxRange; /* 272 */
        }
        else
        {
            /* vf0, i.e. (0,0,0,1) -- see g3dxVu0ClearVector. */
            g3dxVu0ClearVector(rVu1Mem.avAmbientPointOrigin[i]);            /* 276 */
            g3dxVu0ClearVector(rVu1Mem.avDiffusePointOrigin[i]);            /* 277 */
            g3dxVu0ClearVector(rVu1Mem.avSpecularPointOrigin[i]);           /* 278 */
            g3dxVu0ClearVector(rVu1Mem.avPositionPoint[i]);                 /* 279 */

            rVu1Mem.vMax_Min_RangeInversePoint[i] = 0.0f;                   /* 281 */
            rVu1Mem.vMaxRangePoint[i]             = 0.0f;                   /* 282 */
        }
    }                                                                       /* 284 */
}

/* --------------------------------------------------------------------------
 *  _ApplyLightSpot   [ROM g3dCore.c 291-331]
 *
 *  As above for the three spot slots: colours, position, normalised
 *  direction, max range, range inverse, cos(outside) and 1/(cosIn-cosOut).
 *
 *  Notes on the reconstruction, beyond the ones on _ApplyLightDirectional and
 *  _ApplyLightPoint (lq/sq copies, vf0 rather than g_v0000, s_pObject re-read
 *  inside the branch rather than held in a _LIGHTDATA *):
 *
 *  - The two arms disagree on the order of the range pair.  Enabled writes the
 *    max range first (0x420, line 310) and the inverse second (0x430, 311);
 *    disabled zeroes the inverse first (325) and the max range second (326) --
 *    which is the order _ApplyLightPoint uses in *both* of its arms.  The ROM's
 *    own asymmetry, reproduced.
 *
 *  - There is one float local, not two.  `fCosOutside` is assigned on line 313
 *    and survives in $f21 across the second cosf(); the inside cosine is
 *    consumed in place, its `lwc1 f12,0x54(s1)` and `jal cosf` both tagged 314
 *    along with the divide and the store.  Float locals leave no stab, so the
 *    callee-saved register is the only evidence either way.
 *
 *  - Both divides are unguarded: fMaxRange == fMinRange, or a cone whose inside
 *    and outside angles are equal, divides by zero.
 *
 *  - This is the only one of the three with a stack frame (s0-s4, ra and
 *    f20/f21 are saved), which is why its prologue carries the opening line 291
 *    interleaved with the first two statements.
 *
 *  - Lines 303, 310-315 and 319-329 are measured; 304-306 are interpolated as
 *    in the other two, and the normalise falls in the 307-309 gap -- 308 here,
 *    since every other statement group in this function is separated by exactly
 *    one blank line.
 * ------------------------------------------------------------------------ */
static void _ApplyLightSpot(void)
{
    G3DVU1MEMLAYOUT_DIRECT &rVu1Mem = s_pObject->Vu1Mem.Direct;             /* 293 */
    int                     i;
    int                     iId;
    int                     bEnable;

    for (i = 0; i < G3D_NUM_LIGHT_SPOT; i++)                                /* 294 */
    {
        iId     = G3D_START_LIGHT_SPOT + i;                                 /* 296 */
        bEnable = s_pObject->aLightData[iId].bEnable;                       /* 297 */

        if (bEnable == 0)                                                   /* 299 */
        {
            rVu1Mem.lightstatus.lAS &= ~(1 << (G3D_START_LIGHT_SPOT + 2 + i));
        }
        else
        {
            rVu1Mem.lightstatus.lAS |= (1 << (G3D_START_LIGHT_SPOT + 2 + i));
        }

        if (bEnable != 0)                                                   /* 301 */
        {
            float fCosOutside;

            g3dxVu0CopyVector(rVu1Mem.avAmbientSpotOrigin[i],
                              s_pObject->aLightData[iId].L.vAmbient);       /* 303 */
            g3dxVu0CopyVector(rVu1Mem.avDiffuseSpotOrigin[i],
                              s_pObject->aLightData[iId].L.vDiffuse);       /* 304 */
            g3dxVu0CopyVector(rVu1Mem.avSpecularSpotOrigin[i],
                              s_pObject->aLightData[iId].L.vSpecular);      /* 305 */
            g3dxVu0CopyVector(rVu1Mem.avPositionSpot[i],
                              s_pObject->aLightData[iId].L.vPosition);      /* 306 */

            /* normalise the spot direction into the VU1 image */
            sceVu0Normalize(rVu1Mem.avDirectionSpot[i],
                            s_pObject->aLightData[iId].L.vDirection);       /* 308 */

            rVu1Mem.vMaxRangeSpot[i] = s_pObject->aLightData[iId].L.fMaxRange; /* 310 */
            rVu1Mem.vMax_Min_RangeInverseSpot[i] =
                1.0f / (s_pObject->aLightData[iId].L.fMaxRange
                        - s_pObject->aLightData[iId].L.fMinRange);          /* 311 */

            fCosOutside = cosf(s_pObject->aLightData[iId].L.fAngleOutside); /* 313 */
            rVu1Mem.vCosIn_Out_InverseSpot[i] =
                1.0f / (cosf(s_pObject->aLightData[iId].L.fAngleInside)
                        - fCosOutside);                                     /* 314 */
            rVu1Mem.vCosOutsideSpot[i] = fCosOutside;                       /* 315 */
        }
        else
        {
            /* vf0, i.e. (0,0,0,1) -- see g3dxVu0ClearVector. */
            g3dxVu0ClearVector(rVu1Mem.avAmbientSpotOrigin[i]);             /* 319 */
            g3dxVu0ClearVector(rVu1Mem.avDiffuseSpotOrigin[i]);             /* 320 */
            g3dxVu0ClearVector(rVu1Mem.avSpecularSpotOrigin[i]);            /* 321 */
            g3dxVu0ClearVector(rVu1Mem.avPositionSpot[i]);                  /* 322 */
            g3dxVu0ClearVector(rVu1Mem.avDirectionSpot[i]);                 /* 323 */

            rVu1Mem.vMax_Min_RangeInverseSpot[i] = 0.0f;                    /* 325 */
            rVu1Mem.vMaxRangeSpot[i]             = 0.0f;                    /* 326 */

            rVu1Mem.vCosIn_Out_InverseSpot[i]    = 0.0f;                    /* 328 */
            rVu1Mem.vCosOutsideSpot[i]           = 0.0f;                    /* 329 */
        }
    }                                                                       /* 331 */
}

/* --------------------------------------------------------------------------
 *  g3dInitialize   [ROM g3dCore.c 344-384]
 *
 *  Bind the engine to the caller's G3DCOREOBJECT and prime it: default render
 *  / global state, the null material, a zero ambient, the nine default lights
 *  (one per slot type) all disabled, and identity in all five transforms.
 * ------------------------------------------------------------------------ */
void g3dInitialize(const G3DCREATIONDATA *pCD)
{
    int          i;
    _LIGHTDATA  *rLD;
    G3DLIGHTTYPE iLightType;

    s_pObject = pCD->pObj;

    g3ddbgVerifyVu1MemAddress();
    g3ddbgVerifyGsRegisterAddress();

    s_pObject->auiGlobalState[G3DGS_LIGHTINGTYPE]       = G3DLT_LAMBERT;
    s_pObject->auiRenderState[G3DRS_LIGHTING]           = 1;
    s_pObject->auiRenderState[G3DRS_COLORVERTEX]        = 1;
    s_pObject->auiRenderState[G3DRS_SPECULARENABLE]     = 1;
    s_pObject->auiRenderState[G3DRS_DIFFUSEMATERIALSOURCE]  = 0;
    s_pObject->auiRenderState[G3DRS_SPECULARMATERIALSOURCE] = 0;
    s_pObject->auiRenderState[G3DRS_AMBIENTMATERIALSOURCE]  = 0;
    s_pObject->auiRenderState[G3DRS_EMISSIVEMATERIALSOURCE] = 0;
    s_pObject->auiGlobalState[G3DGS_LIGHTATTENUATIONTYPE]   = 0;

    s_pObject->Material = g_NullMaterial;

    sceVu0CopyVector(s_pObject->vAmbient, g_v0000);

    for (i = 0; i < NUM_G3DLIGHTINDEX; i++)
    {
        rLD = &s_pObject->aLightData[i];
        rLD->bEnable = 0;

        iLightType = g3dGetLightType(i);
        g3dutilSetLightDefault(&rLD->L, iLightType);
    }

    for (i = 0; i < NUM_G3DTRANSFORMSTATETYPE; i++)
    {
        sceVu0CopyMatrix(s_pObject->amatTransform[i], g_matUnit);
    }
}

/* --------------------------------------------------------------------------
 *  g3dSetRenderState   [ROM g3dCore.c 397-416]
 *
 *  Store a render-state word; the fog colour is mirrored straight out to the
 *  GS FOGCOL register (BGR packed).
 * ------------------------------------------------------------------------ */
int g3dSetRenderState(G3DRENDERSTATETYPE State, unsigned int uiValue)
{
    /* "g3d is not initialized yet" */
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(State < NUM_G3DRENDERSTATETYPE, "");

    s_pObject->auiRenderState[State] = uiValue;

    if (State == G3DRS_FOGCOLOR)
    {
        g3dSetGsRegister((long)(uiValue & 0xff)
                         | (long)((uiValue >> 8) & 0xff) << 8
                         | (long)((uiValue >> 0x10) & 0xff) << 0x10,
                         SCE_GS_FOGCOL, 1);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dGetRenderStateRef   [ROM g3dCore.c 438-442]
 * ------------------------------------------------------------------------ */
unsigned int &g3dGetRenderStateRef(G3DRENDERSTATETYPE State)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(State < NUM_G3DRENDERSTATETYPE, "");

    return s_pObject->auiRenderState[State];
}

/* --------------------------------------------------------------------------
 *  g3dSetGlobalState   [ROM g3dCore.c 449-464]
 * ------------------------------------------------------------------------ */
int g3dSetGlobalState(G3DGLOBALSTATETYPE State, unsigned int uiValue)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(State < NUM_G3DGLOBALSTATETYPE, "");

    s_pObject->auiGlobalState[State] = uiValue;

    if (State == G3DGS_LIGHTINGTYPE)
    {
        G3DASSERT(uiValue < NUM_G3DLIGHTINGTYPE, "");
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dGetGlobalStateRef   [ROM g3dCore.c 485-489]
 * ------------------------------------------------------------------------ */
unsigned int &g3dGetGlobalStateRef(G3DGLOBALSTATETYPE State)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(State < NUM_G3DGLOBALSTATETYPE, "");

    return s_pObject->auiGlobalState[State];
}

/* --------------------------------------------------------------------------
 *  g3dSetAmbient   [ROM g3dCore.c 496-503]
 *
 *  Store the global ambient colour both in the object and in the VU1 global
 *  block, then refresh the precomputed Calc.vAmbient term.
 * ------------------------------------------------------------------------ */
void g3dSetAmbient(float *vAmbient)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");

    g3dxVu0CopyVector(s_pObject->vAmbient, vAmbient);
    g3dxVu0CopyVector(s_pObject->Vu1Mem.Packed.Global.vAmbient, vAmbient);

    _CalcAmbient();
}

/* --------------------------------------------------------------------------
 *  g3dGetAmbientRef   [ROM g3dCore.c 521-523]
 * ------------------------------------------------------------------------ */
float (&g3dGetAmbientRef(void))[4]
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");

    return s_pObject->vAmbient;
}

/* --------------------------------------------------------------------------
 *  g3dSetTransform   [ROM g3dCore.c 571-604]
 *
 *  Store a transform matrix into amatTransform[State] and propagate it into
 *  the VU1 transform block.  For VIEW and PROJECTION the cached
 *  world->screen matrix (projection * view) is rebuilt.
 * ------------------------------------------------------------------------ */
int g3dSetTransform(G3DTRANSFORMSTATETYPE State, float (*mat)[4])
{
    G3DVU1TRANSFORM *rT;
    float          (*pm0)[4];

    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(State < NUM_G3DTRANSFORMSTATETYPE, "");

    sceVu0CopyMatrix(s_pObject->amatTransform[State], mat);

    rT = &s_pObject->Vu1Mem.Packed.Transform;

    switch (State)
    {
        case G3DTS_VIEW:
        {
            sceVu0CopyMatrix(rT->matCamera, mat);
            /* matWorldScreen = projection * view */
            sceVu0MulMatrix(rT->matWorldScreen, s_pObject->amatTransform[G3DTS_PROJECTION], mat);
            break;
        }
        case G3DTS_PROJECTION:
        {
            /* matWorldScreen = projection * view */
            sceVu0MulMatrix(rT->matWorldScreen, mat, s_pObject->amatTransform[G3DTS_VIEW]);
            break;
        }
        case G3DTS_WORLD:
        {
            sceVu0CopyMatrix(rT->matLocalWorld, mat);
            break;
        }
        case G3DTS_WORLD1:
        {
            pm0 = s_pObject->Vu1Mem.Direct.matLocalWorld1;
            sceVu0CopyMatrix(pm0, mat);
            break;
        }
        case G3DTS_WORLDCLIP:
        {
            pm0 = s_pObject->Vu1Mem.Direct.matWorldClip;
            sceVu0CopyMatrix(pm0, mat);
            break;
        }
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dGetTransformRef   [ROM g3dCore.c 625-629]
 * ------------------------------------------------------------------------ */
float (&g3dGetTransformRef(G3DTRANSFORMSTATETYPE State))[4][4]
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(State < NUM_G3DTRANSFORMSTATETYPE, "");

    return s_pObject->amatTransform[State];
}

/* --------------------------------------------------------------------------
 *  g3dSetMaterial   [ROM g3dCore.c 709-727]
 *
 *  Copy the material into the object and the VU1 material block, stash the
 *  diffuse alpha into Calc.vMisc, then rebuild the ambient term and the
 *  per-light colour data.
 * ------------------------------------------------------------------------ */
int g3dSetMaterial(const G3DMATERIAL *pMaterial)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    /* "pMaterial" */
    G3DASSERT(pMaterial, "");

    s_pObject->Material               = *pMaterial;
    s_pObject->Vu1Mem.Packed.Material = s_pObject->Material;

    s_pObject->Vu1Mem.Packed.Calc.vMisc[0] = s_pObject->Material.vDiffuse[3];

    _CalcAmbient();
    _CalcColor();

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dGetMaterialRef   [ROM g3dCore.c 749-751]
 * ------------------------------------------------------------------------ */
G3DMATERIAL &g3dGetMaterialRef(void)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");

    return s_pObject->Material;
}

/* --------------------------------------------------------------------------
 *  g3dLightEnable   [ROM g3dCore.c 763-769]
 * ------------------------------------------------------------------------ */
int g3dLightEnable(int iLightId, int bEnable)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(iLightId < NUM_G3DLIGHTINDEX, "iLightId:%d", iLightId);

    s_pObject->aLightData[iLightId].bEnable = bEnable;

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dIsLightEnable   [ROM g3dCore.c 776-780]
 * ------------------------------------------------------------------------ */
int g3dIsLightEnable(int iLightId)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(iLightId < NUM_G3DLIGHTINDEX, "iLightId : %d", iLightId);

    return s_pObject->aLightData[iLightId].bEnable;
}

/* --------------------------------------------------------------------------
 *  g3dSetLight   [ROM g3dCore.c 787-806]
 *
 *  Validate a light against its slot (type must match, ranges/angles sane,
 *  point lights must carry a zero direction) and copy it into the slot.
 * ------------------------------------------------------------------------ */
int g3dSetLight(int iLightId, const G3DLIGHT *pLight)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    /* "pLight" */
    G3DASSERT(pLight, "");
    G3DASSERT(iLightId < NUM_G3DLIGHTINDEX, "");

    /* お手数ですが、ライトＩＤと、G3DLIGHT::Type が対応しているか確認しやがれ。
     * ("Sorry, but make sure the light ID and G3DLIGHT::Type correspond.") */
    G3DASSERT(g3dGetLightType( iLightId ) == pLight->Type, "お手数ですが、ライトＩＤと、G3DLIGHT::Type が対応しているか確認しやがれ。");
    G3DWARNING(pLight->fMinRange >= 0.0f, "");
    //G3DWARNING(pLight->fMaxRange >= pLight->fMinRange, "");
    G3DWARNING(pLight->fAngleOutside >= pLight->fAngleInside, "");
    G3DWARNING(pLight->Type != G3DLIGHT_POINT || EqualMemory128(pLight->vDirection, g_v0000, 1), "");

    g3dutilCopyLight(&s_pObject->aLightData[iLightId].L, pLight);

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dGetLightRef   [ROM g3dCore.c 826-830]
 * ------------------------------------------------------------------------ */
G3DLIGHT &g3dGetLightRef(int iLightId)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(iLightId < NUM_G3DLIGHTINDEX, "");

    return s_pObject->aLightData[iLightId].L;
}

/* --------------------------------------------------------------------------
 *  g3dApplyLight   [ROM g3dCore.c 837-850]
 *
 *  Re-apply all nine lights into the VU1 image, refresh the VU1 light status
 *  word, and rebuild the per-light colour data.
 * ------------------------------------------------------------------------ */
int g3dApplyLight(void)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");

    _ApplyLightDirectional();
    _ApplyLightPoint();
    _ApplyLightSpot();

    g3dSetLightStatus(&s_pObject->Vu1Mem.Direct.lightstatus);
    _CalcColor();

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dSetTexture   [ROM g3dCore.c 862-889]
 *
 *  Bind a texture to a stage and, if non-NULL, preload it and push its TEX0 /
 *  TEX1 register values to the GS.
 * ------------------------------------------------------------------------ */
int g3dSetTexture(int iStage, CTexture *pTexture)
{
    sceGifPackAd aGPA[2];

    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(iStage < G3D_MAX_TEXTURESTAGE, "");

    s_pObject->Resource.apTexture[iStage] = pTexture;

    if (pTexture != 0)
    {
        pTexture->PreLoad();

        aGPA[0].DATA = pTexture->GetTextureDataRef().l.lTex0;
        aGPA[1].DATA = pTexture->GetTextureDataRef().l.lTex1;
        aGPA[0].ADDR = SCE_GS_TEX0_1;
        aGPA[1].ADDR = SCE_GS_TEX1_1;
        g3dSetGsRegisters(aGPA, 2, 1);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  _Vu0FlagsLEZ
 *
 *  Stand-in for the `cfc2 t,vc1 / andi t,t,0xee` pair the ROM runs after every
 *  vector term it wants to gate on.  VU0 records a flag pair per component on
 *  each op; the 0xee mask keeps the xyz halves of the two nibbles, and each
 *  kernel then tests one component pair -- 0x88 for x, 0x44 for y, 0x22 for z
 *  (x is the high bit of each nibble) -- skipping the light when either bit is
 *  set.  Between them the pair means "negative or zero", so reproducing the
 *  masks is a per-component `<= 0` test rather than a flag-register model.
 * ------------------------------------------------------------------------ */
static u_int _Vu0FlagsLEZ(const float *v)
{
    u_int uiFlags = 0;

    if (v[0] <= 0.0f) uiFlags |= 0x88;
    if (v[1] <= 0.0f) uiFlags |= 0x44;
    if (v[2] <= 0.0f) uiFlags |= 0x22;

    return uiFlags;
}

/* --------------------------------------------------------------------------
 *  _Vu0SetupPositionalLights
 *
 *  The block g3dCalcVertexColor inlines twice, once for the point group and
 *  once for the spot group: per light, the unit light->vertex direction and
 *  the distance, then the three distance attenuations packed into vf15.x/y/z
 *  with their skip mask in t0.
 *
 *  The attenuation is g3dCalcLightDistanceAttenuation() against the two values
 *  the VU1 image carries instead of the G3DLIGHT -- its two arms collapse into
 *  one clamped (fMaxRange - d) * 1/(fMaxRange - fMinRange).  Note there is no
 *  G3DGS_LIGHTATTENUATIONTYPE branch here: the ROM's inlined form is linear
 *  only, and a runtime test on that state word would have left a branch.
 * ------------------------------------------------------------------------ */
/* ==========================================================================
 *  VU1 draw vs EE prelight bake.
 *
 *  gra3dCalcVertexColor() serves both, and they are not the same calculation.
 *  The four microprograms share one lighting library -- sgsuv.o and sgsuvp0.o
 *  are byte-identical over VU 0x0000..0x0086, which holds the spot kernel
 *  (0x0000), the point kernel (0x0041) and the dispatcher -- so every *draw*
 *  uses the same formula whatever its draw type.  Prelighting is the odd one
 *  out: SetPreRenderMeshData() runs on the EE and bakes into the mesh
 *  packet's avColor[], and it really does use the ROM's own
 *  g3dCalcLightDistanceAttenuation() ramp and one flat x255.
 *
 *  Two things differ in realtime mode:
 *
 *  1. The point attenuation law (see _Vu0SetupPositionalLights).
 *  2. The colour scales.  The VU1 works in GS units and scales each term
 *     separately -- gra3dCalcVu1MaterialData*() folds fAmbientScale (128 for
 *     an SGD primitive) into GLOBALAMBIENT and fDiffuseScale (192) into
 *     DIRCOLDIF / POINTCOLDIF, where the host multiplies the finished 0..1
 *     colour by a flat 255.  That left the host's ambient 1.99x and its
 *     diffuse 1.33x too bright on every lit object.
 *
 *  The VU1 also has no per-light ambient term at all: neither
 *  GRA3DVU1MATERIALDATA_DIRECTIONAL nor _POINT carries one, and neither
 *  kernel reads one.  It is suppressed in realtime mode below.
 *
 *  UPDATE 2026-08-20: with the microcode itself in hand (vu1/*.vsm) the
 *  realtime side of this no longer lives here.  Every draw path -- the vertex
 *  shader, its CPU mirror, the per-fragment spot mode, and the preset/runtime
 *  colour builders -- now runs gra3dCalcVu1VertexColor() / the transcribed
 *  kernels against gra3dSnapshotVu1Lighting(), which is the VU1's own light
 *  image rather than an approximation of it.  The only surviving caller of
 *  g3dSetRealtimeLighting() is SetPreRenderMeshData(), and it passes 0.
 *
 *  So s_bRealtimeLighting is permanently 0 and this file is now purely the
 *  prelight model, which is the one thing the ROM really does evaluate on the
 *  EE.  The realtime arm below, s_fVu1*Scale, g3dSetVu1ColourScales() and
 *  g3dSnapshotVertexLighting() are all unreachable; they are kept rather than
 *  ripped out because untangling them touches every branch in
 *  g3dCalcVertexColor(), and the bake is the one path that must not regress.
 * ======================================================================== */
static int   s_bRealtimeLighting  = 0;
static float s_fVu1AmbientScale   = 255.0f;
static float s_fVu1DiffuseScale   = 255.0f;
static float s_fVu1SpecularScale  = 255.0f;

int g3dSetRealtimeLighting(int bRealtime)
{
    int bPrev = s_bRealtimeLighting;

    s_bRealtimeLighting = bRealtime;
    return bPrev;
}

void g3dSetVu1ColourScales(float fAmbient, float fDiffuse, float fSpecular)
{
    s_fVu1AmbientScale  = fAmbient;
    s_fVu1DiffuseScale  = fDiffuse;
    s_fVu1SpecularScale = fSpecular;
}

/* The host accumulates in 0..1 and multiplies by 255 at the very end, so a
 * term the VU1 scales by S has to be pre-divided by 255 here to land on the
 * same GS value.  Outside realtime mode both come out 1.0 and nothing moves.
 *
 * There is deliberately no ambient twin of these two.  g3dCalcVertexColor()'s
 * global-ambient fold is a bare `vadd.xyz` against Calc.vAmbient with no
 * scale of any kind (ROM 0x0019d1c8), so applying s_fVu1AmbientScale there
 * would be inventing a term the ROM does not have.  g3dSnapshotVertexLighting()
 * still scales the ambient it hands the GPU path, which is where the 128/255
 * correction belongs -- that function builds a VU1 packet image, this one
 * mirrors the EE routine. */
static float _Vu1DiffuseFactor(void)
{
    return s_bRealtimeLighting ? (s_fVu1DiffuseScale / 255.0f) : 1.0f;
}

/* fSpecularSum * fSpecularScale, the scale gra3dCalcVu1MaterialDataDirectional
 * folds into DIRCOLSPC.  fSpecularSum is the add-across of the *material's*
 * specular, so a material with no specular scales to zero -- which is the
 * right answer and is why this never showed on anything measured so far.
 *
 * The point and spot packets use the same numerator over SetMaxColor255's
 * divisor, but their kernels also raise the *diffuse* term to the fourth
 * rather than taking a half-vector dot (VU 0x006f / 0x0073), so their specular
 * remains structurally approximate here -- only the magnitude is corrected. */
static float _Vu1SpecularFactor(void)
{
    return s_bRealtimeLighting ? (s_fVu1SpecularScale / 255.0f) : 1.0f;
}

/* ==========================================================================
 *  The five blocks g3dCalcVertexColor() inlines.
 *
 *  Each is a single `asm` statement in the ROM -- four in g3dLight.h and one
 *  in g3dCore.c itself -- expanded straight into g3dCalcVertexColor()'s body,
 *  which is why none of them carries a symbol of its own.  They are named here
 *  for readability, and each keeps the ROM line its `$LM` marker reports;
 *  those come from symbols.txt's interleaved SOL/`$LM` records, which say
 *  which *file* every line number belongs to, not from Ghidra's `; Line NNN`
 *  alone:
 *
 *      g3dCore.c  190            world position + unit world normal
 *      g3dLight.h 257, 293       camera -> vertex direction (PHONG only)
 *      g3dLight.h 355, 365       directional: negate the light directions
 *      g3dLight.h 848, 905,      positional: per-light direction, distance and
 *                 913, 962       the distance attenuation with its skip mask
 *      g3dLight.h 1332, 1333     spot: the cone ramp with its skip mask
 *
 *  The leading `r` argument is this port's; on the EE these exchanged their
 *  results through physical VU0 registers (see G3DVU0LIGHTREGS above), so the
 *  ROM's own inlines take no arguments at all -- every address they touch is a
 *  compile-time constant off s_pObject, which is also why the stab list hoists
 *  no parameter names for four of the five.  Only the fifth,
 *  g3dxVu0ClampVector(), leaves any: `fMin` and `fMax`, in f1 and f0.
 * ======================================================================== */

/* ROM g3dCore.c 190.  Transform the vertex by the world matrix and the normal
 * by its rotation part, then normalise the normal.
 *
 *     vmulax.xyz / vmadday.xyz / vmaddz.xyz   row-vector x matrix, xyz masked
 *     vadd.xyz vf6, vf14, vf15                add the translation row (position
 *                                             only -- the normal skips it)
 *     vmul.xyz / vadday.x / vmaddz.x          |n|^2
 *     vrsqrt Q, vf0, vf17                     1/|n|
 *     vmulx.w vf16, vf0, vf0                  vf16.w = 0 before the scale
 *     vmulq.xyzw vf7, vf16, Q
 *
 * Two field masks are worth keeping.  `vadd.xyz` leaves vf6.w holding the
 * *vertex's* own w -- the lqc2 loaded a whole quadword and nothing overwrote
 * it -- and the explicitly cleared vf16.w means the normal reaches the kernels
 * with w == 0. */
static void _Vu0CalcVertexPositionNormal(G3DVU0LIGHTREGS *r, sceVu0FVECTOR vPos,
                                         const float *vVertex, const float *vNormal)
{
    const float (*matWorld)[4] = s_pObject->amatTransform[G3DTS_WORLD];
    sceVu0FVECTOR vNrm;
    float         fLen2;
    float         q;
    int           c;

    for (c = 0; c < 3; c++)
    {
        vPos[c] = vVertex[0] * matWorld[0][c] + vVertex[1] * matWorld[1][c]
                + vVertex[2] * matWorld[2][c] + matWorld[3][c];
        vNrm[c] = vNormal[0] * matWorld[0][c] + vNormal[1] * matWorld[1][c]
                + vNormal[2] * matWorld[2][c];
    }
    vPos[3] = vVertex[3];                   /* vadd.xyz: vf6.w is the loaded w */
    vNrm[3] = 0.0f;                         /* vmulx.w vf16, vf0, vf0          */

    fLen2 = vNrm[0] * vNrm[0] + vNrm[1] * vNrm[1] + vNrm[2] * vNrm[2];

    /* PORT DEVIATION.  `vrsqrt` on a zero-length normal raises VU0's D flag
     * and yields a huge finite value, so the kernels' <= 0 tests downstream
     * still behave; a host 1/sqrtf(0.0f) is +inf and poisons the colour all
     * the way to the final clamp.  A degenerate normal contributes nothing
     * here instead. */
    q = (fLen2 > 0.0f) ? (1.0f / g3dxVu0Sqrt(fLen2)) : 0.0f;

    for (c = 0; c < 4; c++)
    {
        r->vf7[c] = vNrm[c] * q;            /* vmulq.xyzw -- w rides through 0 */
    }
}

/* ROM g3dLight.h 257, 293.  Unit camera -> vertex direction, the half-vector's
 * other half.  Only _Vu0LoadColorCoeffPhong() reads it, which is why the ROM
 * computes it under the PHONG test alone.
 *
 *     vsub.xyz vf11, vf6, vf11        worldPos - amatTransform[VIEW][3]
 *     vmul.xyz / vadday.x / vmaddz.x / vrsqrt
 *     vmulq.xyz vf11, vf11, Q         xyz only -- vf11.w is left untouched
 *
 * The `vsubw.w vf20, vf0, vf0` sitting in the middle of the ROM's block writes
 * a register that is dead by the next instruction: a leftover from the shared
 * normalise macro, with no effect on anything. */
static void _Vu0CalcEyeDirection(G3DVU0LIGHTREGS *r, const float *vPos)
{
    const float *vEye = s_pObject->amatTransform[G3DTS_VIEW][3];
    float        fLen2;
    float        q;
    int          c;

    for (c = 0; c < 3; c++)
    {
        r->vf11[c] = vPos[c] - vEye[c];
    }

    fLen2 = r->vf11[0] * r->vf11[0] + r->vf11[1] * r->vf11[1] + r->vf11[2] * r->vf11[2];
    q     = (fLen2 > 0.0f) ? (1.0f / g3dxVu0Sqrt(fLen2)) : 0.0f;

    for (c = 0; c < 3; c++)
    {
        r->vf11[c] *= q;
    }
}

/* ROM g3dLight.h 355, 365.  vf8/vf9/vf10 = -avDirection[i].
 *
 * Negating turns "surface toward the light" -- the sense a directional
 * G3DLIGHT::vDirection carries -- into "light toward the surface", which is
 * what the two positional groups reach by subtracting the light position from
 * the vertex, and what _Vu0LoadColorCoeff* then dots against the normal.
 * `vsub.xyz` once more, so each slot's w survives from the loaded direction. */
static void _Vu0SetupDirectionalLights(G3DVU0LIGHTREGS *r, const sceVu0FVECTOR *avDirection)
{
    int i;
    int c;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        for (c = 0; c < 3; c++)
        {
            r->avf8_10[i][c] = -avDirection[i][c];
        }
        r->avf8_10[i][3] = avDirection[i][3];
    }
}

/* ROM g3dLight.h 848, 905, 913, 962.  Expanded twice -- once for the point
 * group and once for the spot group -- with only the three source addresses
 * differing (rL + 0x40/0x70/0x80 against rL + 0x90/0xf0/0x100).
 *
 *  905: per light, vf8/9/10 = vertex - lightPos and vf12.xyz = |that|.  The
 *       ROM takes `vsqrt` for the length and then `vdiv Q, vf0w, vf12x` to
 *       normalise -- it does not use a reciprocal square root here, because it
 *       wants the distance itself for the ramp below.
 *  962: vf15.xyz = clamp((vMaxRange - d) * vMax_Min_InverseRange, 0, 1), the
 *       clamp being `vmaxx` against vf0.x = 0 then `vminibcw` against
 *       vf0.w = 1, with the MAC flags of the result masked into t0.
 *
 * THERE IS NO ATTENUATION-LAW BRANCH HERE, in either expansion.  The whole ROM
 * function contains exactly five conditional branches -- the assert, the PHONG
 * test and the three group masks -- and none of them tests a lighting mode.
 * The pre-chewed pair the VU1 image carries *is* the formula: _ApplyLightPoint()
 * and _ApplyLightSpot() upload vMaxRange alongside 1/(fMaxRange - fMinRange)
 * precisely so this collapses to one multiply and one clamp, and that
 * reproduces g3dCalcLightDistanceAttenuation() exactly -- inside fMinRange the
 * product is already >= 1, so the single clamp covers both of its arms.
 *
 * The ROM emits the `vminibcw` twice, once either side of the `cfc2`/`andi`
 * pair that captures the flags (0x0019cf60 and 0x0019cf80 are byte-identical).
 * min(min(x, 1), 1) is idempotent, so one clamp is written here; the duplicate
 * is an artefact of the macro, not a second operation. */
static void _Vu0SetupPositionalLights(G3DVU0LIGHTREGS *r, const float *vPos,
                                      const sceVu0FVECTOR *avPosition,
                                      const float *vMaxRange,
                                      const float *vMax_Min_InverseRange)
{
    sceVu0FVECTOR vDistance;
    int           i;
    int           c;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)                           /* 905 */
    {
        float fLen2;
        float d;

        /* `vsub.xyz`: the light positions land in vf20/vf19/vf2, so vf8/9/10
         * keep whatever w the previous group left in them.  Nothing reads it
         * -- every dot below this point is three-component. */
        for (c = 0; c < 3; c++)
        {
            r->avf8_10[i][c] = vPos[c] - avPosition[i][c];
        }

        fLen2 = r->avf8_10[i][0] * r->avf8_10[i][0]
              + r->avf8_10[i][1] * r->avf8_10[i][1]
              + r->avf8_10[i][2] * r->avf8_10[i][2];

        d            = g3dxVu0Sqrt(fLen2);
        vDistance[i] = d;

        if (d > 0.0f)
        {
            float q = 1.0f / d;             /* vdiv Q, vf0w, vf12x            */

            for (c = 0; c < 3; c++)
            {
                r->avf8_10[i][c] *= q;      /* vmulq.xyz                      */
            }
        }
    }
    vDistance[3] = 0.0f;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)                           /* 962 */
    {
        r->vf15[i] = g3dClampf((vMaxRange[i] - vDistance[i]) * vMax_Min_InverseRange[i],
                               0.0f, 1.0f);
    }
    r->vf15[3] = 0.0f;

    /* `cfc2 t2, $17` reads VU0's MAC flag register and `andi 0xee` keeps the
     * zero and sign bits of the xyz lanes; after the low clamp only the zero
     * bits can be set, so the mask means "this light's attenuation came out
     * at or below zero" -- out of range. */
    r->t0 = _Vu0FlagsLEZ(r->vf15);
}

/* ROM g3dLight.h 1332, 1333.  vf16.xyz = the spot cone ramp.
 *
 *     vmul.xyz then a vadda/vmadd pair      dot(avDirection[i], vf8/9/10)
 *     vsub.xyz vf16, vf16, vCosOutside
 *     vmul.xyz vf16, vf16, vCosIn_Out_Inverse
 *     cfc2 / andi 0xee                     flags of the MULTIPLY -> t1
 *     vminibcw.xyz vf16, vf16, vf0         high clamp only
 *
 * Note what is *not* there: no `vmaxx` against zero.  The low end is left
 * unclamped on purpose -- a vertex outside the cone leaves a negative ramp,
 * the flag word catches it, and _Vu0AccumulateLightGroup() then drops that
 * light entirely rather than scaling it to nothing.  The flags are read from
 * the multiply, before the high clamp, so the order below matters.
 *
 * The three dots do not sum in the same order.  A VU add-across can broadcast
 * only one field, so the x lane is `vadday.x` + `vmaddz.x` -- (t.x + t.y) + t.z
 * -- the y lane `vaddax.y` + `vmaddz.y`, which is the same sum commuted, but
 * the z lane is `vaddax.z` + `vmaddy.z`, i.e. (t.z + t.x) + t.y.  That is the
 * ISA choosing lanes for one source-level dot product, not three different
 * expressions, so a plain dot is written here; it costs about 7e-8 relative on
 * the z light and nothing else in this function differs from the ROM by a
 * single bit.
 *
 * cos(alpha) is dot(vDirection, normalize(vertex - lightPos)): the cone opens
 * ALONG vDirection, the direction the beam travels.  That is what the ROM's own
 * g3dCalcSpotlightFalloff() computes -- it takes acos(-dot) against
 * normalize(lightPos - vertex) -- and what every authored spot in the game
 * needs; all 26 in rooms 0 and 2 sit at ceiling height aimed down.  Two ROM
 * sites are written against the opposite reading and the port deviates from
 * both; see the notes in MapLightSetPlayerReal() and MapDrawRoomOne(). */
static void _Vu0CalcSpotCone(G3DVU0LIGHTREGS *r, const sceVu0FVECTOR *avDirection,
                             const float *vCosOutside, const float *vCosIn_Out_Inverse)
{
    int i;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)                           /* 1333 */
    {
        r->vf16[i] = (g3dxVu0InnerProduct(avDirection[i], r->avf8_10[i])
                      - vCosOutside[i]) * vCosIn_Out_Inverse[i];
    }
    r->vf16[3] = 0.0f;

    r->t1 = _Vu0FlagsLEZ(r->vf16);

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        if (r->vf16[i] > 1.0f) r->vf16[i] = 1.0f;
    }
}

/* --------------------------------------------------------------------------
 *  g3dCalcVertexColor   [ROM g3dCore.c 1008-1060, 0x0019cc88, 0x59c bytes]
 *
 *  The CPU-side mirror of the per-vertex lighting: transform the vertex and
 *  normal into world space, seed the destination with the source colour, then
 *  for each active light group -- directional, point, spot -- load that
 *  group's colour data, stage its per-light geometry in the VU0 register set
 *  and accumulate the group's contribution.  Finally fold in the constant
 *  global ambient and clamp to [0,1].
 *
 *  Every instruction in the ROM body is accounted for by the eighteen
 *  g3dCore.c line markers below plus the five inlined blocks above.  The
 *  trailing annotations are those markers, measured -- the ones this file
 *  carried before were partly guessed and several were wrong.
 * ------------------------------------------------------------------------ */
void g3dCalcVertexColor(float *vDest, const float *vVertex, const float *vNormal,
                        const float *vColorSource)
{
    float                   vWork[4];
    G3DVU1MEMLAYOUT_PACKED &rLO = s_pObject->Vu1Mem.Packed;                /* 1012 */
    G3DVU1LIGHT            &rL  = rLO.Light;                               /* 1013 */
    G3DVU0LIGHTREGS         r;
    sceVu0FVECTOR           vPos;
    int                     iLightingType;

    G3DASSERT(s_pObject, "g3d is not initialized yet");                    /* 1009 */

    iLightingType = s_pObject->auiGlobalState[G3DGS_LIGHTINGTYPE];         /* 1015 */

    _Vu0CalcVertexPositionNormal(&r, vPos, vVertex, vNormal);              /* 190 */

    /* For the other two lighting models the ROM leaves vf11 holding the world
     * matrix's first row, because nothing but _Vu0LoadColorCoeffPhong() reads
     * it.  Zeroed here rather than left indeterminate. */
    g3dxVu0CopyVector(r.vf11, g_v0000);
    if (iLightingType == G3DLT_PHONG)                                      /* 1021 */
    {
        _Vu0CalcEyeDirection(&r, vPos);                                    /* 293 */
    }

    g3dxVu0CopyVector(vDest, vColorSource);                                /* 135 */

    r.t5 = (u_int)rL.status.lAS;

    /* The ROM tests the packed status word with one mask per class -- 0x7 for
     * the three directional bits, 0x70 for point, 0x700 for spot -- so a group
     * runs when any light of that class is on, and it re-loads the whole
     * 64-bit word after each group rather than caching it. */
    if (rL.status.lAS & 0x7L)                                              /* 1028 */
    {
        s_apfLoadColor[iLightingType](&r, &rLO.Color.dir);                 /* 1031 */
        _Vu0SetupDirectionalLights(&r, rL.dir.avDirection);                /* 365 */

        s_apfLoadColorCoeff[iLightingType](&r);                            /* 1033 */
        s_apfCalcVertexColorDirectionalLight[iLightingType](vWork, &r);    /* 1034 */

        g3dxVu0AddVector(vDest, vDest, vWork);                             /* 161 */
    }

    if (rL.status.lAS & 0x70L)                                             /* 1040 */
    {
        s_apfLoadColor[iLightingType](&r, &rLO.Color.point);               /* 1043 */
        _Vu0SetupPositionalLights(&r, vPos, rL.point.avPosition,           /* 905 */
                                  rL.point.vMaxRange,
                                  rL.point.vMax_Min_InverseRange);

        s_apfLoadColorCoeff[iLightingType](&r);                            /* 1046 */
        s_apfCalcVertexColorPointLight[iLightingType](vWork, &r);          /* 1047 */

        g3dxVu0AddVector(vDest, vDest, vWork);                             /* 161 */
    }

    if (rL.status.lAS & 0x700L)                                            /* 1052 */
    {
        s_apfLoadColor[iLightingType](&r, &rLO.Color.spot);                /* 1055 */
        _Vu0SetupPositionalLights(&r, vPos, rL.spot.avPosition,            /* 905 */
                                  rL.spot.vMaxRange,
                                  rL.spot.vMax_Min_InverseRange);
        _Vu0CalcSpotCone(&r, rL.spot.avDirection, rL.spot.vCosOutside,     /* 1333 */
                         rL.spot.vCosIn_Out_Inverse);

        s_apfLoadColorCoeff[iLightingType](&r);                            /* 1059 */
        s_apfCalcVertexColorSpotLight[iLightingType](vWork, &r);           /* 1060 */

        g3dxVu0AddVector(vDest, vDest, vWork);                             /* 161 */
    }

    /* Global ambient.  A bare `vadd.xyz` against Calc.vAmbient with no scale
     * of any kind -- the xyz mask is what leaves the vertex alpha coming
     * through from vColorSource untouched.  Calc.vAmbient is already
     * ambient * mat.vAmbient + mat.vEmissive, folded by _CalcAmbient(). */
    g3dxVu0AddVectorXYZ(vDest, vDest, rLO.Calc.vAmbient);                  /* 173 */

    /* `vmaxx.xyzw` then `vminibcx.xyzw` -- all four components, alpha too.
     * fMin and fMax are the ROM's own names for the two bounds; they survive
     * as hoisted RSYMs in this function's stab list, in f1 and f0. */
    g3dxVu0ClampVector(vDest, vDest, 0.0f, 1.0f);                          /* 737 */
}

/* --------------------------------------------------------------------------
 *  g3dSnapshotVertexLighting
 *
 *  Copy the state g3dCalcVertexColor consumes after g3dApplyLight() and
 *  g3dSetMaterial() have derived the light/material products.  This is a
 *  read-only host-renderer bridge: it deliberately does not apply or mutate
 *  light state, because the SGD walker has already selected the exact slots
 *  for the mesh being submitted.
 * ------------------------------------------------------------------------ */
int g3dSnapshotVertexLighting(G3DVERTEXLIGHTINGSTATE *out)
{
    static_assert(sizeof(G3DVERTEXLIGHTINGSTATE) == 48u * 16u,
                  "GPU vertex-light snapshot must stay float4 aligned");

    if (out == NULL || s_pObject == NULL)
    {
        return 0;
    }

    memset(out, 0, sizeof(*out));
    G3DVU1MEMLAYOUT_DIRECT &vu = s_pObject->Vu1Mem.Direct;

    out->config[0] = s_pObject->auiGlobalState[G3DGS_LIGHTINGTYPE];
    out->config[1] = s_pObject->auiGlobalState[G3DGS_LIGHTATTENUATIONTYPE];
    out->config[2] = (unsigned int)vu.lightstatus.lAS;

    out->eye_position[0] = s_pObject->amatTransform[G3DTS_VIEW][3][0];
    out->eye_position[1] = s_pObject->amatTransform[G3DTS_VIEW][3][1];
    out->eye_position[2] = s_pObject->amatTransform[G3DTS_VIEW][3][2];
    out->eye_position[3] = s_pObject->Vu1Mem.Packed.Material.fPower;
    memcpy(out->ambient, vu.vAmbientCalculated, sizeof(out->ambient));

    memcpy(out->directional_direction, vu.avDirectionDirectional,
           sizeof(out->directional_direction));
    memcpy(out->directional_ambient, vu.avAmbientDirectional,
           sizeof(out->directional_ambient));
    memcpy(out->directional_diffuse, vu.avDiffuseDirectional,
           sizeof(out->directional_diffuse));
    memcpy(out->directional_specular, vu.avSpecularDirectional,
           sizeof(out->directional_specular));

    memcpy(out->point_position, vu.avPositionPoint,
           sizeof(out->point_position));
    memcpy(out->point_ambient, vu.avAmbientPoint,
           sizeof(out->point_ambient));
    memcpy(out->point_diffuse, vu.avDiffusePoint,
           sizeof(out->point_diffuse));
    memcpy(out->point_specular, vu.avSpecularPoint,
           sizeof(out->point_specular));
    /* The GPU path is always a VU1 draw, so the per-term scales are baked into
     * the snapshot rather than sent as extra cbuffer lanes -- the shader keeps
     * its layout and simply consumes colours that are already in the right
     * proportion.  Ambient and diffuse only; the specular scale differs
     * between the directional packet (fSpecularSum * fSpecularScale) and the
     * point/spot ones (that, over SetMaxColor255's divisor), and every
     * material measured so far has vSpecular == 0. */
    {
        const float ka = s_fVu1AmbientScale / 255.0f;
        const float kd = s_fVu1DiffuseScale / 255.0f;
        const float ks = s_fVu1SpecularScale / 255.0f;

        for (int c = 0; c < 3; c++)
        {
            out->ambient[c] *= ka;
        }
        for (int i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
        {
            for (int c = 0; c < 3; c++)
            {
                out->directional_diffuse[i][c] *= kd;
                out->point_diffuse[i][c]       *= kd;
                out->spot_diffuse[i][c]        *= kd;
                out->directional_specular[i][c] *= ks;
                out->point_specular[i][c]       *= ks;
                out->spot_specular[i][c]        *= ks;
                /* no per-light ambient on the VU1 */
                out->directional_ambient[i][c] = 0.0f;
                out->point_ambient[i][c]       = 0.0f;
                out->spot_ambient[i][c]        = 0.0f;
            }
        }
    }

    /* .x is fMaxRange, which is what the realtime inverse-distance law uses.
     * .y is the prelight ramp's 1/(max-min) and must NOT drive the shader --
     * it reintroduces the hard range cutoff. */
    for (int i = 0; i < G3D_NUM_LIGHT_POINT; i++)
    {
        out->point_params[i][0] = vu.vMaxRangePoint[i];
        out->point_params[i][1] = vu.vMax_Min_RangeInversePoint[i];
    }

    memcpy(out->spot_position, vu.avPositionSpot,
           sizeof(out->spot_position));
    memcpy(out->spot_direction, vu.avDirectionSpot,
           sizeof(out->spot_direction));
    memcpy(out->spot_ambient, vu.avAmbientSpot,
           sizeof(out->spot_ambient));
    memcpy(out->spot_diffuse, vu.avDiffuseSpot,
           sizeof(out->spot_diffuse));
    memcpy(out->spot_specular, vu.avSpecularSpot,
           sizeof(out->spot_specular));
    for (int i = 0; i < G3D_NUM_LIGHT_SPOT; i++)
    {
        out->spot_params[i][0] = vu.vMaxRangeSpot[i];
        out->spot_params[i][1] = vu.vMax_Min_RangeInverseSpot[i];
        out->spot_params[i][2] = vu.vCosOutsideSpot[i];
        out->spot_params[i][3] = vu.vCosIn_Out_InverseSpot[i];
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dSetGsRegister (single)   [ROM g3dCore.c 1084-1122]
 *
 *  Write a single GS register's value into the local shadow, and -- unless
 *  iDmaChan is -1 -- push it down the DMA chain immediately.
 * ------------------------------------------------------------------------ */
int g3dSetGsRegister(long int lData, long int lAddress, int iDmaChan)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(lAddress < DWSIZEOF(G3DGSREGISTERLAYOUT), "");

    s_pObject->GsRegister.Array.alReg[lAddress] = lData;

    if (iDmaChan != -1)
    {
        g3dDmaSetGsRegister(lData, lAddress);
    }

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dSetGsRegisters (multiple)   [ROM g3dCore.c 1128-1211]
 *
 *  Write iNum GS registers.  With iDmaChan == -1 only the local shadow is
 *  updated; otherwise a single DIRECT/PACKED-A+D packet is opened, every
 *  register is written to both the shadow and the packet, and the packet is
 *  closed (or cancelled when nothing was written).
 * ------------------------------------------------------------------------ */
int g3dSetGsRegisters(const sceGifPackAd *aGPA, int iNum, int iDmaChan)
{
    _PACKET          *pPacket;
    int               iNumRegist;
    int               i;
    sceGifPackAd      rGPA;

    G3DRETURNVAL(s_pObject, 0, "g3d is not initialized yet");

    if (iDmaChan == -1)
    {
        for (i = 0; i < iNum; i++)
        {
            s_pObject->GsRegister.Array.alReg[aGPA[i].ADDR] = aGPA[i].DATA;
        }
        return 1;
    }

    pPacket = (_PACKET *)g3dDmaOpenPacket();
    pPacket->qwVif1Code[2] = VIF1_FLUSH;
    pPacket->qwVif1Code[0] = 0;
    pPacket->qwVif1Code[1] = 0;
    /* GIFTAG: one register, A+D */
    pPacket->gt.lRegs = (pPacket->gt.lRegs & ~0xfL) | 0xe;

    iNumRegist = 0;
    for (i = 0; i < iNum; i++)
    {
        rGPA = aGPA[i];

        G3DASSERT(rGPA.ADDR < memberarraysizeof(G3DGSREGISTERLAYOUT, Array), "");

        s_pObject->GsRegister.Array.alReg[rGPA.ADDR] = rGPA.DATA;

        pPacket->aGPA[iNumRegist].DATA = rGPA.DATA;
        pPacket->aGPA[iNumRegist].ADDR = rGPA.ADDR;
        iNumRegist++;
    }
    if (iNumRegist == 0)
    {
        g3dDmaCancelPacket();
        return 0;
    }

    /* DIRECT VIF1 code (qwc = iNumRegist+1) + GIFTAG (NLOOP, EOP, PACKED) */
    pPacket->qwVif1Code[3] = (iNumRegist + 1) | 0x50000000;
    pPacket->gt.lTag = (long)iNumRegist | 0x1000000000008000;

    g3dDmaClosePacket(&pPacket->aGPA[iNumRegist]);

    return 1;
}

/* --------------------------------------------------------------------------
 *  g3dGetGsRegisterRef   [ROM g3dCore.c 1217-1221]
 * ------------------------------------------------------------------------ */
long int &g3dGetGsRegisterRef(long int lAddress)
{
    G3DASSERT(s_pObject, "g3d is not initialized yet");
    G3DASSERT(lAddress < DWSIZEOF(G3DGSREGISTERLAYOUT), "");

    return s_pObject->GsRegister.Array.alReg[lAddress];
}

/* --------------------------------------------------------------------------
 *  g3dCalcGsPrimitiveCoord   [ROM g3dCore.c 1230-1239]
 *
 *  Convert a float screen coordinate to a GS XYZ primitive coordinate: the
 *  xy are floored to 4.x fixed point and biased by the XYOFFSET register; z is
 *  scaled/offset into the GS depth range.
 * ------------------------------------------------------------------------ */
void g3dCalcGsPrimitiveCoord(sceGsXyz *pGsXyz, const float *vScreenCoord)
{
    sceVu0IVECTOR iv;

    G3DASSERT(s_pObject, "g3d is not initialized yet");

    /* float xy -> 12.4 fixed point */
    _ftoi4(iv, vScreenCoord);

    /* bias by the XYOFFSET register (OFX in [0:15], OFY in [32:47]) */
    pGsXyz->X = (u_short)s_pObject->GsRegister.Direct.gsXyoffset_1.OFX + (short)iv[0];
    pGsXyz->Y = (u_short)s_pObject->GsRegister.Direct.gsXyoffset_1.OFY + (short)iv[1];
    pGsXyz->Z = (u_int)((vScreenCoord[2] - 0.0f) * -65535.0f + 65535.0f);
}

/* --------------------------------------------------------------------------
 *  g3dCalcScreenCoord   [ROM g3dCore.c 1249-1257]
 *
 *  Inverse of g3dCalcGsPrimitiveCoord: subtract the XYOFFSET bias and convert
 *  the GS XYZ back to float screen coordinates.
 * ------------------------------------------------------------------------ */
void g3dCalcScreenCoord(float *vScreenCoord, sceGsXyz *pGsXyz)
{
    sceVu0IVECTOR iv;

    G3DASSERT(s_pObject, "g3d is not initialized yet");

    iv[0] = (int)((u_int)pGsXyz->X - (u_int)(u_short)s_pObject->GsRegister.Direct.gsXyoffset_1.OFX);
    iv[1] = (int)((u_int)pGsXyz->Y - (u_int)(u_short)s_pObject->GsRegister.Direct.gsXyoffset_1.OFY);
    iv[2] = (int)pGsXyz->Z;

    /* 12.4 fixed point -> float */
    vScreenCoord[0] = (float)iv[0] / 16.0f;
    vScreenCoord[1] = (float)iv[1] / 16.0f;
    vScreenCoord[2] = (float)iv[2] / 16.0f;
    vScreenCoord[3] = (float)iv[3] / 16.0f;
}

/* ==========================================================================
 *  Per-lighting-type VU0 macro-mode kernels.
 *
 *  Reversed from the ROM bodies, not inferred by analogy: the eighteen inline
 *  ones each left a `.gnu.linkonce.t._Vu0*` copy at 0x2b4b68..0x2b572c and the
 *  three point-light statics sit at 0x0019d778 / 0x0019d888 / 0x0019d9d8, so
 *  all twenty-one have real code to read.
 *
 *  Register map recovered from those bodies:
 *
 *    vf0            (0,0,0,1), the VU hardware constant -- vf0.w is what the
 *                   `vminibcw` clamp-to-1 broadcasts, vf0.x the zero `vmaxx`
 *                   clamps against
 *    vf1            (1,1,1,1)
 *    vf3/vf4/vf5    material vDiffuse / vAmbient / vSpecular (vf5.w = fPower)
 *    vf7            unit world normal
 *    vf8/vf9/vf10   unit light->vertex direction, per light
 *    vf11           unit camera->vertex direction
 *    vf15/vf16      xyz = per-light distance attenuation / spot cone falloff
 *    vf17/vf18      xyz = per-light N.L / N.H
 *    vf25/26/27     per-light ambient colour
 *    vf28/29/30     per-light diffuse colour
 *    vf31/21/22     per-light specular colour
 *    t0/t1/t3/t4    skip masks derived from vf15 / vf16 / vf17 / vf18
 *    t5             Light.status.lAS
 *
 *  All of that now travels through the G3DVU0MATERIALREGS / G3DVU0LIGHTREGS
 *  arguments instead of the register file; see the note at the top of the file.
 * ======================================================================== */

/* vmul.xyz against a material colour: the destination's w falls through from
 * the source load, because only xyz is written before the sqc2. */
static void _Vu0MulColorXYZ(float *vDst, const float *vSrc, const float *vMaterial)
{
    vDst[0] = vSrc[0] * vMaterial[0];
    vDst[1] = vSrc[1] * vMaterial[1];
    vDst[2] = vSrc[2] * vMaterial[2];
    vDst[3] = vSrc[3];
}

/* ----- material load (constant / lambert / phong) -----------------------
 *  Stage vf3/vf4/vf5 for the _Vu0CalcColorData* kernel that follows.  Constant
 *  shading multiplies by no material at all, so its ROM body really is empty
 *  -- eight bytes, `jr ra` with a nop in the delay slot. */
static void _Vu0LoadMaterialConstant(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat)
{
    (void)r;
    (void)pMat;
}

static void _Vu0LoadMaterialLambert(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat)
{
    sceVu0CopyVector(r->vf3, (float *)pMat->vDiffuse);
    sceVu0CopyVector(r->vf4, (float *)pMat->vAmbient);
}

static void _Vu0LoadMaterialPhong(G3DVU0MATERIALREGS *r, const G3DMATERIAL *pMat)
{
    sceVu0CopyVector(r->vf3, (float *)pMat->vDiffuse);
    sceVu0CopyVector(r->vf4, (float *)pMat->vAmbient);
    sceVu0CopyVector(r->vf5, (float *)pMat->vSpecular);

    /* `vmulx.w vf5,vf0,vf20` folds fPower into the specular vector's w.  It is
     * carried but never read back: every later use of vf5 is a .xyz multiply,
     * so this CPU path applies no specular exponent.  The VU1 microcode reads
     * fPower out of the material block it is uploaded to separately. */
    r->vf5[3] = pMat->fPower;
}

/* ----- per-light colour data (light colour * material) ------------------
 *  Source is always the full ambient/diffuse/specular ColorOrigin block; the
 *  destination is only as wide as the lighting model needs. */
static void _Vu0CalcColorDataConstant(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL)
{
    G3DVU1COLOR_CONSTANT          *pDst = (G3DVU1COLOR_CONSTANT *)pCD;
    const G3DVU1COLOR_DIRECTIONAL *pSrc = (const G3DVU1COLOR_DIRECTIONAL *)pL;
    int                            i;

    (void)r;

    /* Note which array is read: the source's *diffuse* group (0x30) lands in
     * the destination's single slot (0x00), with no material multiply -- flat
     * shading takes the light's diffuse colour raw.  The destination member is
     * still named avAmbient only because G3DVU1COLOR_CONSTANT has just the one
     * array, and it is the array the accumulator adds unconditionally. */
    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        sceVu0CopyVector(pDst->avAmbient[i], (float *)pSrc->avDiffuse[i]);
    }
}

static void _Vu0CalcColorDataLambert(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL)
{
    G3DVU1COLOR_LAMBERT           *pDst = (G3DVU1COLOR_LAMBERT *)pCD;
    const G3DVU1COLOR_DIRECTIONAL *pSrc = (const G3DVU1COLOR_DIRECTIONAL *)pL;
    int                            i;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        _Vu0MulColorXYZ(pDst->avAmbient[i], pSrc->avAmbient[i], r->vf4);
        _Vu0MulColorXYZ(pDst->avDiffuse[i], pSrc->avDiffuse[i], r->vf3);
    }
}

static void _Vu0CalcColorDataPhong(const G3DVU0MATERIALREGS *r, void *pCD, const void *pL)
{
    G3DVU1COLOR_PHONG             *pDst = (G3DVU1COLOR_PHONG *)pCD;
    const G3DVU1COLOR_DIRECTIONAL *pSrc = (const G3DVU1COLOR_DIRECTIONAL *)pL;
    int                            i;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        _Vu0MulColorXYZ(pDst->avAmbient[i],  pSrc->avAmbient[i],  r->vf4);
        _Vu0MulColorXYZ(pDst->avDiffuse[i],  pSrc->avDiffuse[i],  r->vf3);
        _Vu0MulColorXYZ(pDst->avSpecular[i], pSrc->avSpecular[i], r->vf5);
    }
}

/* ----- per-light colour load into the working register set --------------
 *  vf25..vf27 / vf28..vf30 / vf31,vf21,vf22 for the accumulator. */
static void _Vu0LoadColorDataConstant(G3DVU0LIGHTREGS *r, const void *pCD)
{
    const G3DVU1COLOR_CONSTANT *pC = (const G3DVU1COLOR_CONSTANT *)pCD;
    int                         i;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        sceVu0CopyVector(r->avAmbient[i], (float *)pC->avAmbient[i]);
    }
}

static void _Vu0LoadColorDataLambert(G3DVU0LIGHTREGS *r, const void *pCD)
{
    const G3DVU1COLOR_LAMBERT *pC = (const G3DVU1COLOR_LAMBERT *)pCD;
    int                        i;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        sceVu0CopyVector(r->avAmbient[i], (float *)pC->avAmbient[i]);
        sceVu0CopyVector(r->avDiffuse[i], (float *)pC->avDiffuse[i]);
    }
}

static void _Vu0LoadColorDataPhong(G3DVU0LIGHTREGS *r, const void *pCD)
{
    const G3DVU1COLOR_PHONG *pC = (const G3DVU1COLOR_PHONG *)pCD;
    int                      i;

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        sceVu0CopyVector(r->avAmbient[i],  (float *)pC->avAmbient[i]);
        sceVu0CopyVector(r->avDiffuse[i],  (float *)pC->avDiffuse[i]);
        sceVu0CopyVector(r->avSpecular[i], (float *)pC->avSpecular[i]);
    }
}

/* ----- lighting-model coefficient calculation ---------------------------
 *  The per-light dot products, computed three at a time across the x/y/z lanes
 *  of vf17 (diffuse) and vf18 (specular), each followed by the cfc2 read that
 *  turns "this dot came out <= 0" into a skip mask. */
static void _Vu0LoadColorCoeffConstant(G3DVU0LIGHTREGS *r)
{
    /* Empty in the ROM too -- eight bytes, `jr ra` plus a nop.  Constant
     * shading has no dot products to take. */
    (void)r;
}

static void _Vu0LoadColorCoeffLambert(G3DVU0LIGHTREGS *r)
{
    int i;

    /* vf17 = -(light->vertex . N), which is +N.L: vf8/vf9/vf10 run from the
     * light toward the surface, so the negation is what puts a lit face on the
     * positive side.  No clamp here -- the mask is the clamp. */
    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        r->vf17[i] = -sceVu0InnerProduct(r->avf8_10[i], r->vf7);
    }
    r->vf17[3] = 0.0f;

    r->t3 = _Vu0FlagsLEZ(r->vf17);
}

static void _Vu0LoadColorCoeffPhong(G3DVU0LIGHTREGS *r)
{
    int i;

    /* The first half is byte-for-byte the Lambert kernel (0x2b4d70..0x2b4dd0
     * against 0x2b4d00..0x2b4d60): same vf17, same t3. */
    _Vu0LoadColorCoeffLambert(r);

    /* Blinn half-vector term.  H = vf11 + vf8 with both vectors running toward
     * the surface, so -dot(N, normalize(H)) is the usual +N.H. */
    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        sceVu0FVECTOR vHalf;
        int           iDir;

        /* ROM bug, reproduced: the third half-vector is built from vf9 --
         * light 1's direction -- not vf10.  The three adds are
         * `vadd.xyz vf20,vf11,vf8` (0x2b4dd4), `vadd.xyz vf2,vf11,vf9`
         * (0x2b4dd8) and `vadd.xyz vf19,vf11,vf9` (0x2b4e0c); the encodings
         * 0x4bc958a8 and 0x4bc95ce8 differ only in their fd field, so it is
         * not a disassembler slip.  vf10 never reaches the specular path at
         * all, and light 2 wears light 1's highlight.  Left as found. */
        iDir = (i == 2) ? 1 : i;

        sceVu0AddVector(vHalf, r->vf11, r->avf8_10[iDir]);
        vHalf[3] = 0.0f;                        /* vmulx.w against vf0.x */
        sceVu0Normalize(vHalf, vHalf);

        r->vf18[i] = -sceVu0InnerProduct(r->vf7, vHalf);
    }
    r->vf18[3] = 0.0f;

    /* mask first, then clamp: `cfc2` at 0x2b4ef8 precedes the vminibcw at
     * 0x2b4f00, so a negative N.H sets the skip bit before it is flattened */
    r->t4 = _Vu0FlagsLEZ(r->vf18);
    for (i = 0; i < 3; i++)
    {
        if (r->vf18[i] > 1.0f) r->vf18[i] = 1.0f;
    }

    /* A VU1 draw raises the half-vector dot to a fixed eighth power: the
     * directional kernel clamps with MAXx at VU 0x00dd and then squares vf16
     * three times over (0x00e2 / 0x00e7 / 0x00ed) before it reaches DIRCOLSPC.
     * The prelight path takes the material's fPower instead -- which
     * gra3dSetMaterial() pins to 1.0, i.e. no exponent at all, so a realtime
     * highlight was spread across the whole lit hemisphere. */
    if (s_bRealtimeLighting)
    {
        for (i = 0; i < 3; i++)
        {
            float f = r->vf18[i] * r->vf18[i];   /* ^2 */

            f *= f;                              /* ^4 */
            r->vf18[i] = f * f;                  /* ^8 */
        }
    }
}

/* ==========================================================================
 *  Accumulation.
 *
 *  All nine _Vu0CalcVertexColor*Light* kernels are the same three-times-
 *  unrolled loop over the group's lights, differing only in which status bits
 *  they test, which attenuation vectors they fold in, and how many of the
 *  three colour terms the lighting model filled.  Each light's contribution is
 *  capped at 1.0 on its own before joining the sum, and the sum is capped
 *  again -- two separate `vminibcw` against vf0.w.
 * ======================================================================== */
enum _VU0LIGHTGROUP
{
    _VU0GROUP_DIRECTIONAL = 0,
    _VU0GROUP_POINT       = 1,
    _VU0GROUP_SPOT        = 2
};

/* Status-word bit each group's lights own (Light.status.lAS), and the flag
 * pair each light index tests -- x is the high bit of each nibble. */
static const u_int s_auiEnableBit[3][G3D_MAX_LIGHT_PER_TYPE] =
{
    { 0x001, 0x002, 0x004 },            /* directional */
    { 0x010, 0x020, 0x040 },            /* point       */
    { 0x100, 0x200, 0x400 },            /* spot        */
};
static const u_int s_auiCompMask[G3D_MAX_LIGHT_PER_TYPE] = { 0x88, 0x44, 0x22 };

static void _Vu0AccumulateLightGroup(float *vDestColor, const G3DVU0LIGHTREGS *r,
                                     int iGroup, int bDiffuse, int bSpecular)
{
    sceVu0FVECTOR vSum;
    int           i;
    int           c;

    sceVu0CopyVector(vSum, g_v0000);

    for (i = 0; i < G3D_MAX_LIGHT_PER_TYPE; i++)
    {
        sceVu0FVECTOR vLight;
        u_int         uiMask = s_auiCompMask[i];
        /* A VU1 draw caps the per-light COEFFICIENT; the EE prelight caps the
         * finished coloured term.  See the note in the branch below. */
        const int     bCoeffCap = (s_bRealtimeLighting != 0 &&
                                   iGroup != _VU0GROUP_DIRECTIONAL);
        float         fCoeffDiffuse  = r->vf17[i];
        float         fCoeffSpecular = r->vf18[i];

        if ((r->t5 & s_auiEnableBit[iGroup][i]) == 0)             continue;
        if (iGroup != _VU0GROUP_DIRECTIONAL && (r->t0 & uiMask))  continue;
        if (iGroup == _VU0GROUP_SPOT        && (r->t1 & uiMask))  continue;

        if (bCoeffCap)
        {
            /* MINIw at VU 0x002c (spot) / 0x006b (point) clamps
             * atten * max(N.L, 0) -- the coefficient -- to 1 BEFORE the light
             * colour is multiplied in, and the cone ramp is applied after that
             * cap (VU 0x0033).  Nothing then caps the coloured result: the
             * only clamp on the way out is g3dCalcVertexColor()'s final one.
             *
             * Capping the coloured product instead drives every surface inside
             * a lamp's range to full white as soon as the attenuation exceeds
             * the reciprocal of the colour, which covers most of the near
             * field -- fMaxRange/d is already 2x at half range and 16x at a
             * tenth of it.  That is what blew out the live flashlight pass on
             * room and furniture meshes and the character lighting in
             * cutscenes.  Capping the coefficient keeps each light's own
             * colour and leaves the falloff shape intact. */
            fCoeffDiffuse  *= r->vf15[i];
            fCoeffSpecular *= r->vf15[i];
            if (fCoeffDiffuse  > 1.0f) fCoeffDiffuse  = 1.0f;
            if (fCoeffSpecular > 1.0f) fCoeffSpecular = 1.0f;

            if (iGroup == _VU0GROUP_SPOT)
            {
                fCoeffDiffuse  *= r->vf16[i];
                fCoeffSpecular *= r->vf16[i];
            }
        }

        /* The ambient term is unconditional; the other two drop out when their
         * own flag pair says the dot came out <= 0.
         *
         * A VU1 draw has no per-light ambient at all -- the material packets
         * carry only lmDiffuse / lmSpecular -- so it contributes nothing in
         * realtime mode.  Every light measured so far has vAmbient == 0, which
         * is why this never showed. */
        if (s_bRealtimeLighting)
        {
            for (c = 0; c < 3; c++) vLight[c] = 0.0f;
        }
        else
        {
            for (c = 0; c < 3; c++) vLight[c] = r->avAmbient[i][c];
        }

        if (bDiffuse && (r->t3 & uiMask) == 0)
        {
            float kd = _Vu1DiffuseFactor();

            for (c = 0; c < 3; c++) vLight[c] += r->avDiffuse[i][c] * fCoeffDiffuse * kd;
        }
        if (bSpecular && (r->t4 & uiMask) == 0)
        {
            float ks = _Vu1SpecularFactor();

            for (c = 0; c < 3; c++) vLight[c] += r->avSpecular[i][c] * fCoeffSpecular * ks;
        }

        if (!bCoeffCap)
        {
            /* Prelight form: attenuation and cone multiply the coloured term. */
            if (iGroup != _VU0GROUP_DIRECTIONAL)
            {
                for (c = 0; c < 3; c++) vLight[c] *= r->vf15[i];
            }
            if (iGroup == _VU0GROUP_SPOT)
            {
                for (c = 0; c < 3; c++) vLight[c] *= r->vf16[i];
            }
        }

        /* The directional kernel has neither of these caps: it accumulates
         * DIRCOLDIF[i] * ndotl[i] straight into vf19 and clamps exactly once,
         * at the very end, against GLOBALAMBIENT.w (VU 0x00f8 -- and that .w
         * is the 255.0 gra3dCalcVu1MaterialDataDirectional stores).  The final
         * clamp in g3dCalcVertexColor() is that one.  Point and spot behave
         * the same way once the coefficient has been capped above, so in
         * realtime mode neither cap runs; the prelight keeps both. */
        if (!s_bRealtimeLighting)
        {
            for (c = 0; c < 3; c++)
            {
                if (vLight[c] > 1.0f) vLight[c] = 1.0f;
            }
        }
        for (c = 0; c < 3; c++)
        {
            vSum[c] += vLight[c];
        }
    }

    if (!s_bRealtimeLighting)
    {
        for (c = 0; c < 3; c++)
        {
            if (vSum[c] > 1.0f) vSum[c] = 1.0f;
        }
    }

    /* The ROM's `sqc2` stores all four components, but nothing in these
     * kernels ever writes vf19.w -- every op is .xyz -- so the w that reaches
     * the caller is a leftover from whatever last used the register, and
     * g3dCalcVertexColor's `vadd.xyzw` then folds it into the vertex alpha.
     * Zero goes out instead: alpha belongs to the material, and a stale
     * register is not something to reproduce. */
    sceVu0CopyVector(vDestColor, vSum);
}

/* ----- directional-light accumulation ----------------------------------- */
static void _Vu0CalcVertexColorDirectinalLightConstant(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_DIRECTIONAL, 0, 0);
}

static void _Vu0CalcVertexColorDirectinalLightLambert(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_DIRECTIONAL, 1, 0);
}

static void _Vu0CalcVertexColorDirectinalLightPhong(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_DIRECTIONAL, 1, 1);
}

/* ----- point-light accumulation ----------------------------------------- */
static void _Vu0CalcVertexColorPointLightConstant(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_POINT, 0, 0);
}

static void _Vu0CalcVertexColorPointLightLambert(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_POINT, 1, 0);
}

static void _Vu0CalcVertexColorPointLightPhong(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_POINT, 1, 1);
}

/* ----- spot-light accumulation ------------------------------------------ */
static void _Vu0CalcVertexColorSpotLightConstant(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_SPOT, 0, 0);
}

static void _Vu0CalcVertexColorSpotLightLambert(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_SPOT, 1, 0);
}

static void _Vu0CalcVertexColorSpotLightPhong(float *vDestColor, const G3DVU0LIGHTREGS *r)
{
    _Vu0AccumulateLightGroup(vDestColor, r, _VU0GROUP_SPOT, 1, 1);
}
