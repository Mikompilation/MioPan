/* ==========================================================================
 *  sgd_types.h
 *
 *  On-disc / in-memory layout of an SGD ("Scene Graph Data") model file and
 *  its process-unit chain, plus the small descriptor/data structs that hang
 *  off it.  Shared by gra3dSGD.c, gra3dSGDData.c and gra3dShadow.c.
 *
 *  Offsets/sizes are taken verbatim from the prototype's debug type info.
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _SGD_TYPES_H
#define _SGD_TYPES_H

#include "eetypes.h"            /* u_int, u_char, u_short, qword */
#include <stddef.h>
#include <stdint.h>
#include <libvu0.h>             /* sceVu0FVECTOR */
#include "g3dLight.h"           /* G3DLIGHTTYPE, SGDLIGHTTYPE */
#include "gra3dTypes.h"         /* GRA3DMATERIALINDEXCACHE (-> SGDMATERIALCACHE) */

#define SGD_VALID_VERSIONID  0x1050u

/* mesh vector-type ids used by the decompiled SGD walkers */
#define iMT_0   0x10
#define iMT_2   0x12
#define iMT_2F  0x32

typedef sceVu0FVECTOR DVECTOR[2];
typedef void (*LPFUNC_CALCWEIGHTEDVECTORBUFFER)(float *dp, float *v);

template <class T> struct SGDSELF32
{
    int value;

    T *get() const
    {
        if (value == 0)
        {
            return (T *)0;
        }

        return (T *)((uintptr_t)this + (intptr_t)value);
    }

    int raw() const
    {
        return value;
    }

    void setRaw(int v)
    {
        value = v;
    }

    operator T *() const
    {
        return get();
    }

    T *operator->() const
    {
        return get();
    }

    T &operator[](ptrdiff_t i) const
    {
        return get()[i];
    }

    SGDSELF32 &operator=(T *ptr)
    {
        value = ptr != (T *)0 ? (int)((intptr_t)ptr - (intptr_t)this) : 0;
        return *this;
    }
};

template <class T, intptr_t FieldOffset> struct SGDFILE32
{
    u_int value;

    T *get() const
    {
        if (value == 0)
        {
            return (T *)0;
        }

        return (T *)((uintptr_t)this - FieldOffset + (uintptr_t)value);
    }

    u_int raw() const
    {
        return value;
    }

    void setRaw(u_int v)
    {
        value = v;
    }

    operator T *() const
    {
        return get();
    }

    T *operator->() const
    {
        return get();
    }

    T &operator[](ptrdiff_t i) const
    {
        return get()[i];
    }

    SGDFILE32 &operator=(T *ptr)
    {
        value = ptr != (T *)0 ? (u_int)((uintptr_t)ptr - ((uintptr_t)this - FieldOffset)) : 0;
        return *this;
    }
};

struct SGDFILEHEADER;
struct SGDPROCUNITHEADER;
struct SGDCOORDINATE;
struct SGDMATERIAL;
struct SGDVECTORINFO;
struct _VERTEXLIST;

struct PHEAD                                /* 0x34 */
{
    u_int HeaderSections;                   /* 0x00 */
    u_int UniqHeaderSize;                   /* 0x04 */
    SGDSELF32<float> pUniqVertex;           /* 0x08 */
    SGDSELF32<float> pUniqNormal;           /* 0x0c */
    SGDSELF32<u_int> pUniqList;             /* 0x10 */
    u_int CommonHeaderSize;                 /* 0x14 */
    SGDSELF32<float> pCommonVertex;         /* 0x18 */
    SGDSELF32<float> pCommonNormal;         /* 0x1c */
    SGDSELF32<u_int> pCommonList;           /* 0x20 */
    u_int WeightedHeaderSize;               /* 0x24 */
    SGDSELF32<float> pWeightedVertex;       /* 0x28 */
    SGDSELF32<float> pWeightedNormal;       /* 0x2c */
    SGDSELF32<u_int> pWeightedList;         /* 0x30 */
};

struct _LIGHTCOMPAREDATA                    /* 0x8 */
{
    float fMaxPower;                        /* 0x0 */
    int   iIndex;                           /* 0x4 */
};

/* ---- material ---------------------------------------------------------- */
typedef GRA3DMATERIALINDEXCACHE SGDMATERIALCACHE;

struct SGDMATERIAL                          /* 0xb0 */
{
    unsigned int     uiPrimType;            /* 0x00 */
    char             strTexName[12];        /* 0x04 */
    float            vAmbient[4];           /* 0x10 */
    float            vDiffuse[4];           /* 0x20 */
    float            vSpecular[4];          /* 0x30 */
    float            vEmission[4];          /* 0x40 */
    int              iCacheStatus;          /* 0x50 */
    unsigned int     iTagAddressOld;        /* 0x54 */
    int              iSizeOld;              /* 0x58 */
    int              iPad;                  /* 0x5c */
    SGDMATERIALCACHE aCache[3];             /* 0x60 */
    int              aiPad[8];              /* 0x90 */
};

/* ---- vertex / normal addressing ---------------------------------------- */
enum SGDVECTORADDRESSID
{
    SVA_UNIQUE   = 0,
    SVA_COMMON   = 1,
    SVA_WEIGHTED = 2,
    NUM_SGDVECTORADDRESSID = 3,
    SGDVECTORADDRESSID_FORCE_DWORD = -1
};

struct _VECTORINDEX                         /* 0x8 */
{
    unsigned int uiVertexId;                /* 0x0 */
    unsigned int uiNormalId;                /* 0x4 */
};

struct _VECTORADDRESS                        /* 0x8 */
{
    SGDSELF32<sceVu0FVECTOR> pVertex;       /* 0x0 */
    SGDSELF32<sceVu0FVECTOR> pNormal;       /* 0x4 */
};

union _VECTORDATA                            /* 0x8 */
{
    _VECTORINDEX   vIndex;                   /* 0x0 */
    _VECTORADDRESS vAddress;                 /* 0x0 */
};

struct _ONELIST                              /* 0x8 */
{
    short int sCoordId0;                     /* 0x0 */
    short int sCoordId1;                     /* 0x2 */
    u_short   usNumVector;                   /* 0x4 */
    u_short   vOff;                          /* 0x6 */
};

struct _VERTEXLIST                           /* 0xc */
{
    int      iNumList;                       /* 0x0 */
    _ONELIST aList[1];                       /* 0x4 */
};

struct SGDVECTORADDRESS                      /* 0x10 */
{
    unsigned int   uiSize;                   /* 0x0 */
    SGDSELF32<sceVu0FVECTOR> pvVertex;       /* 0x4 */
    SGDSELF32<sceVu0FVECTOR> pvNormal;       /* 0x8 */
    SGDSELF32<_VERTEXLIST>   pVertexList;    /* 0xc */
};

struct SGDVECTORINFO                         /* 0x34 */
{
    unsigned int     uiNumAddress;           /* 0x00 */
    SGDVECTORADDRESS aAddress[3];            /* 0x04 */
};

/* ---- per-block coordinate (bone) -------------------------------------- */
struct SGDCOORDINATE                         /* 0xe0 */
{
    float          matCoord[4][4];           /* 0x00 */
    float          matLocalWorld[4][4];      /* 0x40 */
    float          _matWork[4][4];           /* 0x80 */
    float          vRot[4];                  /* 0xc0 */
    SGDSELF32<SGDCOORDINATE> pParent;        /* 0xd0 */
    unsigned int   bCalc;                    /* 0xd4 */
    unsigned int   edge_check;               /* 0xd8 */
    int            bInViewvolume;            /* 0xdc */
};

/* ---- process-unit descriptors (the 0x8 union payload) ------------------ */
struct SGDVUVNDESC                           /* 0x8 */
{
    short int     sNumVertex;                /* 0x0 */
    short int     sNumNormal;                /* 0x2 */
    unsigned char ucSize;                    /* 0x4 */
    unsigned char ucVectorType;              /* 0x5 */
    unsigned char aucPad[2];                 /* 0x6 */
};

struct SGDVUMESHDESC                          /* 0x8 */
{
    int           iTagSize;                  /* 0x0 */
    unsigned char ucPad0;                    /* 0x4 */
    unsigned char ucMeshType;                /* 0x5 */
    unsigned char ucNumMesh;                 /* 0x6 */
    unsigned char ucPad1;                    /* 0x7 */
};

struct SGDVUMATERIALDESC                      /* 0x8 */
{
    union                                    /* 0x0 */
    {
        int                    iMaterialIndex;
        SGDSELF32<SGDMATERIAL> pMat;
    };
    int iPad;                                /* 0x4 */
};

struct SGDCOORDINATEDESC                      /* 0x8 */
{
    int iCoordId0;                           /* 0x0 */
    int iCoordId1;                           /* 0x4 */
};

struct SGDBOUNDINGBOXDESC                     /* 0x8 */
{
    int iCoordId;                            /* 0x0 */
    int iPad;                                /* 0x4 */
};

struct SGDGSIMAGEDESC                          /* 0x8 */
{
    int iQWordSize;                          /* 0x0 */
    int iPad;                                /* 0x4 */
};

struct SGDLIGHTDESC                            /* 0x8 */
{
    SGDLIGHTTYPE Type;                       /* 0x0 */
    int          iNum;                       /* 0x4 */
};

struct SGDTEXTUREIMAGEDESC                     /* 0x8 */
{
    int iNumTexture;                         /* 0x0 */
    int iPaddingSize;                        /* 0x4 */
};

struct SGDTEXTUREANIMATIONDESC                 /* 0x8 */
{
    unsigned char ucNumTexture;              /* 0x0 */
    unsigned char ucPaddingSize;             /* 0x1 */
    unsigned char bEnable;                   /* 0x2 */
    unsigned char ucStep;                    /* 0x3 */
    unsigned char ucCount;                   /* 0x4 */
    unsigned char bLoop;                     /* 0x5 */
    unsigned char aucPad[2];                 /* 0x6 */
};

enum ProcUnitType
{
    VUVN = 0,
    MESH = 1,
    MATERIAL = 2,
    COORDINATE = 3,
    BOUNDING_BOX = 4,
    GS_IMAGE = 5,
    TRI2 = 10,
    END = 11,
    INVALID = 12,
    MonotoneTRI2 = 13,
    StackTRI2 = 14,
};

/* ---- process-unit header ---------------------------------------------- *
 * pNext is stored self-relative on disc and fixed up by sgdRemap.         */
struct SGDPROCUNITHEADER                       /* 0x10 */
{
    SGDSELF32<SGDPROCUNITHEADER> pNext;      /* 0x0 */
    int                iCategory;            /* 0x4 */
    union                                    /* 0x8 */
    {
        long long               lPrimType;
        SGDVUVNDESC             VUVNDesc;
        SGDVUMESHDESC           VUMeshDesc;
        SGDVUMATERIALDESC       VUMaterialDesc;
        SGDCOORDINATEDESC       CoordDesc;
        SGDBOUNDINGBOXDESC      BoundingBoxDesc;
        SGDGSIMAGEDESC          GSImageDesc;
        SGDLIGHTDESC            LightDesc;
        SGDTEXTUREIMAGEDESC     TexDesc;
        SGDTEXTUREANIMATIONDESC TexAnimDesc;
    };
};


/* iCategory values handled by the remap / draw walkers. */
enum SGDPROCUNITCATEGORY
{
    SPC_VUVN        = 0,        /* root vertex/normal block            */
    SPC_MESH        = 1,        /* mesh primitive                      */
    SPC_MATERIAL    = 2,        /* material reference (index on disc)   */
    SPC_COORDINATE  = 3,        /* coordinate/bone reference           */
    SPC_BOUNDINGBOX = 4,        /* 8-corner AABB                       */
    SPC_GSIMAGE     = 5,        /* GS image upload                     */
    SPC_TRI2        = 0xa,      /* embedded TRI2 texture file          */
    SPC_UNSUPPORTED = 0xb,      /* present but not handled in build    */
    SPC_NOP         = 0xd       /* no fix-up needed                    */
};

/* ---- TRI2 embedded texture file --------------------------------------- */
struct SGDTRI2FILEHEADER                       /* 0x70 */
{
	/* 0x00 */ unsigned int      uiVif1Code_NOP0;
	/* 0x04 */ unsigned int      uiVif1Code_NOP1;
	/* 0x08 */ unsigned int      uiVif1Code_FLUSH;
	/* 0x0c */ unsigned int      uiVif1Code_DIRECT;
	/* 0x10 */ sceGsLoadImage    gsli;

	/* uiVif1Code_DIRECT is a plain word, not a VIFcode bitfield struct:
	 * types.txt types it "unsigned int", and _MakeTRI2FileHeader stores the
	 * whole word in one sw with no 16-bit mask on the computed qwc, which a
	 * bitfield assignment would have needed.  The readers take the low half
	 * through GetTRI2Size(), which is the lhu the ROM emits. */

	/* 172 */ unsigned int GetTRI2Size() const
    {
        return this->uiVif1Code_DIRECT & 0xffff;
    }
};

/* PORT DEVIATION -- do NOT simplify this back to a plain array.
 *
 * The ROM's header ends at 0x18 and the file then carries uiNumBlock
 * self-relative block heads.  Spelling that as apProcUnitHead[1] makes every
 * apProcUnitHead[i] with i >= 1 an out-of-bounds access -- undefined
 * behaviour -- and GCC at -O3 acts on it.  sgdRemap() fixes the heads up with
 *
 *     SGDSELF32<SGDPROCUNITHEADER> &rpPH = pSGDHead->apProcUnitHead[i];
 *     SetSelfRelative32(rpPH, SGD_ADDR(pSGDHead, rpPH.raw()), ...);
 *
 * and at -O3 those stores were DELETED for every i >= 1.  The heads then kept
 * their on-disc file-base-relative values, SGDSELF32::get() resolved them
 * against the field address rather than the file base (off by exactly
 * 0x18 + 4*i), and gra3dsgdDrawPresetDataObject() followed them into vertex
 * data and segfaulted.  Measured on room ros05 (108 blocks): sgdRemap ran and
 * set ucMapFlag, yet headRaw[1] still read its on-disc 0x000fb610.  -O2 did
 * not exploit it, which is exactly what made the crash release-only.
 *
 * Widening the bound would change sizeof(SGDFILEHEADER), which a static_assert
 * pins to the ROM's 0x20.  So the member keeps its exact storage and hands out
 * a laundered base pointer instead, putting the declared bound out of the
 * optimiser's reach.  Only the conversion operator is provided: adding an
 * operator[] as well would make obj[i] ambiguous against the built-in
 * subscript on the converted pointer. */
struct SGDPROCUNITHEADTABLE                    /* 0x04 -- uiNumBlock entries */
{
    SGDSELF32<SGDPROCUNITHEADER> aEntry[1];

    operator SGDSELF32<SGDPROCUNITHEADER> *() const
    {
        /* Portable optimisation barrier.  The round trip through a volatile
         * pointer forces the value through memory, and a volatile read yields
         * a value the optimiser knows nothing about -- so the declared [1]
         * bound cannot follow it into the caller's indexing.  Plain casts are
         * not enough (provenance survives them) and __asm__ is GCC/Clang only;
         * this compiles identically under MSVC, GCC and Clang.
         *
         * A volatile access cannot be hoisted, so a subscript inside a loop
         * costs one store+load per iteration (measured: sgdRemap keeps it in
         * the loop).  That is bounded by uiNumBlock -- 108 for the largest
         * room -- and none of these walkers is per-vertex, so it is noise. */
        SGDSELF32<SGDPROCUNITHEADER> *volatile p =
            const_cast<SGDSELF32<SGDPROCUNITHEADER> *>(aEntry);
        return p;
    }
};

/* ---- file header ------------------------------------------------------ */
struct SGDFILEHEADER                           /* 0x20 */
{
    unsigned int       uiVersionId;          /* 0x00 */
    unsigned char      ucMapFlag;            /* 0x04 -- set while remapped */
    unsigned char      ucModelType;          /* 0x05 */
    short unsigned int usNumMaterial;        /* 0x06 */
    SGDFILE32<SGDCOORDINATE, 0x08> pCoord;   /* 0x08 */
    SGDFILE32<SGDMATERIAL, 0x0c>   pMaterial;/* 0x0c */
    SGDFILE32<SGDVECTORINFO, 0x10> pVectorInfo; /* 0x10 */
    unsigned int       uiNumBlock;           /* 0x14 */
    SGDPROCUNITHEADTABLE apProcUnitHead;     /* 0x18 -- uiNumBlock entries */
    int                aiPad[1];             /* 0x1c */
};

/* Untyped mirror of SGDFILEHEADER used by the coordinate-mapping helpers. */
struct HeaderSection                           /* 0x1c */
{
    u_int          VersionID;                /* 0x00 */
    u_char         MAPFLAG;                  /* 0x04 */
    u_char         kind;                     /* 0x05 */
    u_short        materials;                /* 0x06 */
    SGDFILE32<SGDCOORDINATE, 0x08> coordp;   /* 0x08 */
    SGDFILE32<SGDMATERIAL, 0x0c>   matp;     /* 0x0c */
    SGDFILE32<u_int, 0x10>         phead;    /* 0x10 */
    u_int          blocks;                   /* 0x14 */
    u_int          primitives[1];            /* 0x18 */
};

static_assert(sizeof(SGDSELF32<int>) == 4, "SGD 32-bit reference must stay 4 bytes");
static_assert(sizeof(SGDFILE32<int, 0>) == 4, "SGD file offset must stay 4 bytes");
static_assert(sizeof(PHEAD) == 0x34, "PHEAD must match PS2 file layout");
static_assert(sizeof(_VECTORDATA) == 0x8, "_VECTORDATA must match PS2 file layout");
static_assert(sizeof(SGDCOORDINATE) == 0xe0, "SGDCOORDINATE must match PS2 file layout");
static_assert(sizeof(SGDPROCUNITHEADER) == 0x10, "SGDPROCUNITHEADER must match PS2 file layout");
static_assert(sizeof(SGDTRI2FILEHEADER) == 0x70, "SGDTRI2FILEHEADER must match PS2 file layout");
static_assert(sizeof(SGDFILEHEADER) == 0x20, "SGDFILEHEADER must match PS2 file layout");

enum SGDRENDERTYPE
{
    SRT_REALTIME        = 0,
    SRT_PRELIGHTING     = 1,
    SRT_MAPSHADOW       = 2,
    SRT_CLEARPRELIGHTING = 3,
    NUM_SGDRENDERTYPE   = 4
};

/* Size data accumulated while rebuilding TRI2 files (gra3dSGDData.c). */
struct TRI2SIZEDATA                            /* 0x14 */
{
    unsigned int uiMaxAddress;               /* 0x00 */
    unsigned int uiMinAddress;               /* 0x04 */
    unsigned int uiVRAMTexSize;              /* 0x08 */
    unsigned int uiMaxTbp;                   /* 0x0c */
    unsigned int uiPageSize;                 /* 0x10 */
};

#endif /* _SGD_TYPES_H */
