/* ==========================================================================
 *  gra3dSGDData.c
 *
 *  SGD data layer: offset<->pointer relocation (sgdRemap / sgdRemapInverse),
 *  bone-coordinate evaluation, light-data verification and bounding-box
 *  extraction.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "gra3dSGDData.h"
#include "gra3dSGD.h"
#include "gra3d.h"
#include "g3dDma.h"
#include "g3ddbg.h"
#include "g3dUtil.h"
#include "g3dxVu0.h"            /* EqualMemory128, sceVu0* */
#include "gra3dConst.h"        /* g_v0000 */
#include "gra3dTRI2.h"
#include "miopan/rendering/miopan_graph3d.h"
#include "ctl/fixed_array.h"   /* fixed_array<G3DLIGHT, NUM_GRA3DLIGHTID> indexing */
#include <limits.h>

static int s_bEnableOptimizeTexture = 0;        /* sdata */

/*
 * The PS2 remapper wrote native 32-bit pointers into these fields.  The host
 * keeps the same four-byte file layout and stores a displacement from the
 * field itself.  Never allow a host pointer to be narrowed silently: most
 * targets are inside the SGD, while the weighted buffers live in emulated EE
 * RAM close enough to remain representable.
 */
template <class T>
static void SetSelfRelative32(SGDSELF32<T> &rRef, T *pTarget,
                              const char *pFieldName)
{
    if (pTarget == NULL)
    {
        rRef.setRaw(0);
        return;
    }

    intptr_t relative = (intptr_t)pTarget - (intptr_t)&rRef;

    G3DASSERT(relative >= INT32_MIN && relative <= INT32_MAX,
              "%s is outside self-relative range (field:%p, target:%p)",
              pFieldName, (void *)&rRef, (void *)pTarget);
    if (relative < INT32_MIN || relative > INT32_MAX)
    {
        rRef.setRaw(0);
        return;
    }

    rRef.setRaw((int)relative);
}

inline SGDTRI2FILEHEADER * NextTri2(SGDTRI2FILEHEADER *tri2Head, unsigned short nQwc)
{
    return (SGDTRI2FILEHEADER *) ((uintptr_t)tri2Head + (nQwc*16+16));
}

inline SGDTRI2FILEHEADER * GetTri2(SGDPROCUNITHEADER * pPUHead, SGDTEXTUREIMAGEDESC *TexDesc)
{
    return (SGDTRI2FILEHEADER *) ((uintptr_t)pPUHead + TexDesc->iPaddingSize);
}

static void MappingCoordinateData(u_int *intpointer, HeaderSection *hs)
{
}

static void MappingVUVNDataPreset(u_int *intpointer, int mtype, int gloops, int hsize)
{
}

static void RebuildTRI2FilesInverse(SGDPROCUNITHEADER *pPUHead)
{
}

static void MappingCoordinateDataInverse(SGDPROCUNITHEADER *intpointer, SGDFILEHEADER *hs)
{
}

static void MappingVUVNDataInverse(SGDPROCUNITHEADER *pPUHead, SGDFILEHEADER *pSGDHead)
{
}

static void MappingVUVNDataPresetInverse(u_int *p, int mtype, int gloops, int hsize)
{
}

static void MappingVertexListInverse(_VERTEXLIST *pVL, SGDVECTORINFO *pVectorInfo)
{
}

void sgdEnableOptimizeTexture(int b)
{
    s_bEnableOptimizeTexture = b;
}

static void RebuildTRI2Files(SGDPROCUNITHEADER *pPUHead)
{
    if (!s_bEnableOptimizeTexture)
    {
        return;
    }

    SGDTEXTUREIMAGEDESC &rTexDesc = pPUHead->TexDesc;

    TRI2SIZEDATA         TRI2SizeData;
    memset(&TRI2SizeData, 0, sizeof(TRI2SizeData));
    TRI2SizeData.uiMinAddress = -1;

    if (rTexDesc.iNumTexture <= 2)
    {
        return;
    }

    _SetPREVIOUSTRI2PRIM(NULL);
    SGDTRI2FILEHEADER *pTRI2Head = (SGDTRI2FILEHEADER *)((uintptr_t)pPUHead + (rTexDesc.iPaddingSize + 16));
    gra3dLoadTRI2FileToVRAM(rTexDesc.iNumTexture, pTRI2Head, 1);

    for (int i = 0; i < rTexDesc.iNumTexture; i++)
    {
        unsigned short nQwc = pTRI2Head->GetTRI2Size();
        gra3dGetTRI2SizeData(&TRI2SizeData, &TRI2SizeData, pTRI2Head);
        pTRI2Head = NextTri2(pTRI2Head, nQwc);
    }

    gra3dSetGsRegister(0, 0x3f);
    g3dDmaFlush();
    TRI2SizeData.uiPageSize = gra3dCalcVRAMPageSize(TRI2SizeData.uiMaxAddress - TRI2SizeData.uiMinAddress);

    {
        SGDPROCUNITHEADER *pNext = pPUHead->pNext;
        uintptr_t          gap   = pNext != NULL ? (uintptr_t)pNext - (uintptr_t)pPUHead : 0;

        G3DRETURN(gap >= TRI2SizeData.uiPageSize * 1024 * 8 + 16 * 24,
                  "Not Enough Memory %d %d\n", (int)gap,
                  TRI2SizeData.uiPageSize * 1024 * 8 + 16 * 24);
    }

    rTexDesc.iNumTexture = gra3dGenerateTRI2FileFromVRAM((SGDTRI2FILEHEADER *)((uintptr_t)pPUHead + (rTexDesc.iPaddingSize + 16)), &TRI2SizeData);
}

static void MappingVUVNData(SGDPROCUNITHEADER *pPUHead, SGDFILEHEADER *pSGDHead)
{
    SGDVUVNDESC   &rVUVNDesc   = pPUHead->VUVNDesc;
    _VECTORDATA   *pVectorData = (_VECTORDATA *)&pPUHead[3];
    SGDVECTORINFO *pVectorInfo = pSGDHead->pVectorInfo;
    int            i;

    switch (rVUVNDesc.ucVectorType)
    {
    case SVA_UNIQUE:                                     /* 0: model-local arrays */
    {
        sceVu0FVECTOR *vp = pVectorInfo->aAddress[SVA_UNIQUE].pvVertex;
        sceVu0FVECTOR *np = pVectorInfo->aAddress[SVA_UNIQUE].pvNormal;

        for (i = 0; i < rVUVNDesc.sNumVertex; i++)
        {
            unsigned int vertexId = pVectorData[i].vIndex.uiVertexId;
            unsigned int normalId = pVectorData[i].vIndex.uiNormalId;

            SetSelfRelative32(pVectorData[i].vAddress.pVertex, vp + vertexId,
                              "VUVN unique vertex");
            SetSelfRelative32(pVectorData[i].vAddress.pNormal, np + normalId,
                              "VUVN unique normal");
        }
        break;
    }

    case SVA_COMMON:                                     /* 1: nothing to remap */
    {
        break;
    }

    case SVA_WEIGHTED:                                   /* 2 (and >1 default) */
    default:
    {
        if (pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList == NULL)
        {
            sceVu0FVECTOR *vp = pVectorInfo->aAddress[SVA_WEIGHTED].pvVertex;
            sceVu0FVECTOR *np = pVectorInfo->aAddress[SVA_WEIGHTED].pvNormal;

            for (i = rVUVNDesc.sNumVertex; i > 0; i--)
            {
                unsigned int vertexId = pVectorData->vIndex.uiVertexId;
                unsigned int normalId = pVectorData->vIndex.uiNormalId;

                SetSelfRelative32(pVectorData->vAddress.pVertex,
                                  vp + vertexId * 2,
                                  "VUVN weighted vertex");
                SetSelfRelative32(pVectorData->vAddress.pNormal,
                                  np + normalId * 2,
                                  "VUVN weighted normal");
                pVectorData++;
            }
        }
        else
        {
            float (*vp)[4] = _GetGlobalVertexBuffer();
            float (*np)[4] = _GetGlobalNormalBuffer();

            for (i = 0; i < rVUVNDesc.sNumVertex; i++)
            {
                unsigned int vertexId = pVectorData[i].vIndex.uiVertexId;
                unsigned int normalId = pVectorData[i].vIndex.uiNormalId;

                SetSelfRelative32(pVectorData[i].vAddress.pVertex,
                                  (sceVu0FVECTOR *)(vp + vertexId),
                                  "VUVN skinned vertex");
                SetSelfRelative32(pVectorData[i].vAddress.pNormal,
                                  (sceVu0FVECTOR *)(np + normalId),
                                  "VUVN skinned normal");
            }
        }
        break;
    }
    }
}

static void MappingMeshData(SGDPROCUNITHEADER *pPUHeader, u_int *vuvnprim, SGDFILEHEADER *pSGDHead)
{
    int mtype  = pPUHeader->VUMeshDesc.ucMeshType;
    int gloops = pPUHeader->VUMeshDesc.ucNumMesh;

    if (mtype & 0x10)
    {
        if (!(mtype & 0x40))
        {
            MappingVUVNDataPreset(vuvnprim, mtype, gloops, 0);
        }
    }
    else if (!(mtype & 0xc0))
    {
        MappingVUVNData((SGDPROCUNITHEADER *)vuvnprim, pSGDHead);
    }
}

static void MappingMeshDataInverse(SGDPROCUNITHEADER *pPUHeader, u_int *vuvnprim,
                                   SGDFILEHEADER *pSGDHead)
{
    int mtype = pPUHeader->VUMeshDesc.ucMeshType;
    int gloops = pPUHeader->VUMeshDesc.ucNumMesh;

    if (mtype & 0x10)
    {
        if (!(mtype & 0x40))
        {
            MappingVUVNDataPresetInverse(vuvnprim, mtype, gloops, 0);
        }
    }
    else if (!(mtype & 0xc0))
    {
        MappingVUVNDataInverse((SGDPROCUNITHEADER *)vuvnprim, pSGDHead);
    }
}

static void MappingVertexList(_VERTEXLIST *pVL, SGDVECTORINFO *pVectorInfo)
{
    int size  = 0;
    int vnnum;
    int i;

    for (i = 0; i < pVL->iNumList; i++)
    {
        vnnum              = pVL->aList[i].usNumVector;
        pVL->aList[i].vOff = (u_short)size;
        size              += vnnum;
    }

    if (_GetGlobalBufferSize() < size)
    {
        printf("VNBuffer Over size %d needs %d\n", _GetGlobalBufferSize(), size);
        G3DASSERT(0, "");                              /* #exp == "0" (DAT_003f1358) */
        pVectorInfo->aAddress[SVA_UNIQUE].pVertexList   = NULL;
        pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList = NULL;
    }
}


void sgdRemap(SGDFILEHEADER *pSGDHead)
{
    unsigned int       i;
    u_int             *vuvnprim;
    SGDCOORDINATE     *pCoord;
    SGDMATERIAL       *pMaterial;
    SGDVECTORINFO     *pVectorInfo;

    G3DRETURN(pSGDHead, "");
    G3DRETURN(pSGDHead->uiVersionId == SGD_VALID_VERSIONID, "");

    if (pSGDHead->ucMapFlag != 0)
    {
        return;
    }
    /* A freshly unmapped allocation may be a different model at the exact
     * same address.  Retire its immutable GPU geometry before pointer fixup. */
    MioPan_Graph3dInvalidateSkinCache(pSGDHead);
    pSGDHead->ucMapFlag = 1;

    pCoord = pSGDHead->pCoord;
    pMaterial = pSGDHead->pMaterial;
    pVectorInfo = pSGDHead->pVectorInfo;

    if (pMaterial != NULL)
    {
        G3DASSERT(!((uintptr_t)pMaterial & 0xf), "material alignment is illegal");
    }

    if (pCoord != NULL)
    {
        for (i = 0; i < pSGDHead->uiNumBlock - 1; i++)
        {
            SGDCOORDINATE &rCoord = pCoord[i];
            int j = rCoord.pParent.raw();

            G3DASSERT(j <= (int)pSGDHead->uiNumBlock - 2,
                      "coordinate parent link is illegal\nj : %d, pSGDHead->uiNumBlock : %d",
                      j, pSGDHead->uiNumBlock);

            if (j < 0)
            {
                SetSelfRelative32(rCoord.pParent, (SGDCOORDINATE *)NULL,
                                  "coordinate parent");
            }
            else
            {
                SetSelfRelative32(rCoord.pParent, &pCoord[j],
                                  "coordinate parent");
            }
        }
    }

    if (pVectorInfo != NULL)
    {
        for (i = 0; i < pVectorInfo->uiNumAddress; i++)
        {
            SGDVECTORADDRESS &rVA = pVectorInfo->aAddress[i];

            if (rVA.uiSize != 0 && rVA.pvVertex.raw() != 0)
            {
                u_int raw = (u_int)rVA.pvVertex.raw();
                SetSelfRelative32(rVA.pvVertex,
                                  (sceVu0FVECTOR *)SGD_ADDR(pSGDHead, raw),
                                  "vector-info vertex");
            }
            if (rVA.uiSize > 1 && rVA.pvNormal.raw() != 0)
            {
                u_int raw = (u_int)rVA.pvNormal.raw();
                SetSelfRelative32(rVA.pvNormal,
                                  (sceVu0FVECTOR *)SGD_ADDR(pSGDHead, raw),
                                  "vector-info normal");
            }
            if (rVA.uiSize > 2 && rVA.pVertexList.raw() != 0)
            {
                u_int raw = (u_int)rVA.pVertexList.raw();
                SetSelfRelative32(rVA.pVertexList,
                                  (_VERTEXLIST *)SGD_ADDR(pSGDHead, raw),
                                  "vector-info list");
            }
        }

        if (pVectorInfo->aAddress[0].pVertexList)
        {
            if (pVectorInfo->uiNumAddress == 4)
            {
                _VERTEXLIST *pVL;

                if (_GetGlobalBufferSize() == 0)
                {
                    pVectorInfo->aAddress[SVA_UNIQUE].pVertexList = NULL;
                    pVectorInfo->aAddress[SVA_COMMON].pVertexList = NULL;
                    pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList = NULL;
                }
                else if (pVectorInfo->aAddress[SVA_WEIGHTED].pvVertex == NULL &&
                         pVectorInfo->aAddress[SVA_WEIGHTED].pvNormal == NULL)
                {
                    pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList = NULL;
                }

                pVL = pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList;
                pVectorInfo->aAddress[SVA_UNIQUE].pVertexList = NULL;

                if (pVL != NULL)
                {
                    MappingVertexList(pVL, pVectorInfo);
                    MappingVertexList((_VERTEXLIST *)&pVL->aList[pVL->iNumList], pVectorInfo);
                }
            }
        }
    }

    for (i = 0; i < pSGDHead->uiNumBlock; i++)
    {
        SGDSELF32<SGDPROCUNITHEADER> &rpPH = pSGDHead->apProcUnitHead[i];

        G3DASSERT(!((int)rpPH.raw() & 0xf), "sgd is illegal");
        if (rpPH.raw() != 0)
        {
            u_int raw = (u_int)rpPH.raw();
            SetSelfRelative32(rpPH,
                              (SGDPROCUNITHEADER *)SGD_ADDR(pSGDHead, raw),
                              "process-unit head");
        }
    }

    {
        SGDPROCUNITHEADER *pTop = NULL;

        if (pVectorInfo != NULL)
        {
            pTop = (SGDPROCUNITHEADER *)(sceVu0FVECTOR *)pVectorInfo->aAddress[0].pvVertex;
        }

        if ((pTop || pSGDHead->apProcUnitHead[0]) && pTop == pSGDHead->apProcUnitHead[0])
        {
            G3DASSERT(0, "Illegal SGD Data\n");
        }
    }

    for (i = 0; i < pSGDHead->uiNumBlock; i++)
    {
        vuvnprim = NULL;
        SGDPROCUNITHEADER* pPUHead = pSGDHead->apProcUnitHead[i], *pNext;

        while (pPUHead != NULL)
        {
            G3DASSERT(!((uintptr_t)pPUHead & 0xf), "sgd is illegal");

            if (!pPUHead->pNext)
            {
                break;
            }

            SGDPROCUNITHEADER* pNext = pPUHead->pNext;
            G3DASSERT(!((uintptr_t)pPUHead->pNext.get() & 0xf), "sgd is illegal");

            switch (pPUHead->iCategory)
            {
            case SPC_VUVN:
            {
                vuvnprim = (u_int *)pPUHead;
                break;
            }
            case SPC_MESH:
            {
                MappingMeshData(pPUHead, vuvnprim, pSGDHead);
                break;
            }
            case SPC_MATERIAL:
            {
                int materialIndex = pPUHead->VUMaterialDesc.iMaterialIndex;
                SetSelfRelative32(pPUHead->VUMaterialDesc.pMat,
                                  pMaterial + materialIndex,
                                  "process-unit material");
                break;
            }
            case SPC_COORDINATE:
            {
                MappingCoordinateData((u_int *)pPUHead, (HeaderSection *)pSGDHead);
                break;
            }
            case SPC_BOUNDINGBOX:
            case SPC_NOP:
            {
                break;
            }
            case SPC_TRI2:
            {
                RebuildTRI2Files(pPUHead);
                break;
            }
            case SPC_UNSUPPORTED:
            {
                G3DASSERT(0, "");
                break;
            }
            default:
            {
                G3DASSERT(0, "pPUHead->iCategory : %d", pPUHead->iCategory);
                break;
            }
            }

            pPUHead = pNext;
        }
    }
}

#if 0
void sgdRemap(SGDFILEHEADER *pSGDHead)
{
    SGDPROCUNITHEADER **apProcUnitHead;
    unsigned int        i;
    int                 j;
    u_int              *vuvnprim;
    SGDPROCUNITHEADER  *pPUHead;

    G3DRETURN(pSGDHead, "");                                              /* 622 */
    G3DRETURN(pSGDHead->uiVersionId == SGD_VALID_VERSIONID, "");          /* 625 */

    if (pSGDHead->ucMapFlag != 0)                                         /* 628 */
    {
        return;
    }
    pSGDHead->ucMapFlag = 1;                                              /* 630 */

    if ((u_int)pSGDHead->pCoord < SGD_REMAP_BORDER && pSGDHead->pCoord)   /* 635 */
    {
        pSGDHead->pCoord = (SGDCOORDINATE *)SGD_ADDR(pSGDHead, pSGDHead->pCoord);
    }

    if ((u_int)pSGDHead->pMaterial < SGD_REMAP_BORDER)                    /* 640 */
    {
        if (pSGDHead->pMaterial)
        {
            pSGDHead->pMaterial = (SGDMATERIAL *)SGD_ADDR(pSGDHead, pSGDHead->pMaterial);
            /* 645  「データのアラインがおかしいよ。」= "the data alignment is wrong." */
            G3DASSERT(!((u_int)pSGDHead->pMaterial & 0xf), "データのアラインがおかしいよ。");
        }
    }

    if (pSGDHead->pCoord)                                                 /* 649 */
    {
        for (i = 0; i < pSGDHead->uiNumBlock - 1; i++)                   /* 653 */
        {
            SGDCOORDINATE &rCoord = pSGDHead->pCoord[i];                /* 656 */
            j      = (int)rCoord.pParent;                              /* 657 */

            /* 660 "coordinate parent-child link looks wrong" */
            G3DASSERT((int)j <= (int)pSGDHead->uiNumBlock - 2, "こーでねーとの親子関係がおかしい風\nj : %d, pSGDHead->uiNumBlock : %d", j, pSGDHead->uiNumBlock);

            if (j < 0)
            {
                rCoord.pParent = NULL;                                  /* 664 */
            }
            else if (j < (int)pSGDHead->pCoord)
            {
                rCoord.pParent = &pSGDHead->pCoord[j];                  /* 668 */
            }
            G3DASSERT(!((int)rCoord.pParent & 0xf), "こーでねーとの親子関係がおかしい風"); /* 670 */
        }
    }

    if (pSGDHead->pVectorInfo)                                           /* 676 */
    {
        SGDVECTORINFO *pVectorInfo = (SGDVECTORINFO *)SGD_ADDR(pSGDHead, pSGDHead->pVectorInfo);

        pSGDHead->pVectorInfo = pVectorInfo;                           /* 681 */
        G3DASSERT(pVectorInfo, "pVectorInfo is NULL");                  /* 683 */

        for (i = 0; i < pVectorInfo->uiNumAddress; i++)                 /* 683 */
        {
            SGDVECTORADDRESS &rVA = pVectorInfo->aAddress[i];           /* 685 */

            if (rVA.uiSize != 0 && rVA.pvVertex)
            {
                rVA.pvVertex = (sceVu0FVECTOR *)SGD_ADDR(pSGDHead, rVA.pvVertex);
            }
            if (rVA.uiSize > 1 && rVA.pvNormal)
            {
                rVA.pvNormal = (sceVu0FVECTOR *)SGD_ADDR(pSGDHead, rVA.pvNormal);
            }
            if (rVA.uiSize > 2 && rVA.pVertexList)
            {
                rVA.pVertexList = (_VERTEXLIST *)SGD_ADDR(pSGDHead, rVA.pVertexList);
            }
        }

        if (pVectorInfo->aAddress[0].pVertexList)                       /* 698 */
        {
            if (pVectorInfo->uiNumAddress == 4)
            {
                _VERTEXLIST *pVL;

                if (_GetGlobalBufferSize() == 0)
                {
                    pVectorInfo->aAddress[SVA_UNIQUE].pVertexList = NULL;
                    pVectorInfo->aAddress[SVA_COMMON].pVertexList = NULL;
                    pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList = NULL;
                }
                else if (pVectorInfo->aAddress[SVA_WEIGHTED].pvVertex == NULL &&
                         pVectorInfo->aAddress[SVA_WEIGHTED].pvNormal == NULL)
                {
                    pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList = NULL;
                }

                pVL = pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList;
                pVectorInfo->aAddress[SVA_UNIQUE].pVertexList = NULL;

                if (pVL)
                {
                    MappingVertexList(pVL, pVectorInfo);               /* 720 */
                    MappingVertexList((_VERTEXLIST *)&pVL->aList[pVL->iNumList], pVectorInfo);
                }
            }
        }
    }

    apProcUnitHead = pSGDHead->apProcUnitHead;                           /* 731 */
    for (i = 0; i < pSGDHead->uiNumBlock; i++)                           /* 733 */
    {
        SGDPROCUNITHEADER *&rpPH = apProcUnitHead[i];

        G3DASSERT(!((int)rpPH & 0xf), "sgd is illegal");                /* 738 */

        if (rpPH)
        {
            rpPH = (SGDPROCUNITHEADER *)SGD_ADDR(pSGDHead, rpPH);
        }
    }

    /* Sanity check: the first proc-unit head must not alias the vertex data
     * pointer (that would mean the file's block table was never written).    */
    {                                                                    /* 748 */
        SGDPROCUNITHEADER *pTop = NULL;

        if (pSGDHead->pVectorInfo)
        {
            pTop = (SGDPROCUNITHEADER *)pSGDHead->pVectorInfo->aAddress[0].pvVertex;
        }

        if ((pTop || pSGDHead->apProcUnitHead[0]) && pTop == pSGDHead->apProcUnitHead[0])
        {
            G3DASSERT(0, "Illegal SGD Data\n");                          /* 0x2f0, #exp == "0" */
        }
    }

    for (i = 0; i < pSGDHead->uiNumBlock; i++)                           /* 756 */
    {
        vuvnprim = NULL;
        pPUHead  = pSGDHead->apProcUnitHead[i];

        while (pPUHead)                                                  /* 770 */
        {
            G3DASSERT(!((int)pPUHead & 0xf), "sgd is illegal");        /* 772 */

            if (pPUHead->pNext == NULL)                                /* 775 */
            {
                break;
            }

            pPUHead->pNext = (SGDPROCUNITHEADER *)((int)&pPUHead->pNext + (int)pPUHead->pNext);
            G3DASSERT(!((int)pPUHead->pNext & 0xf), "sgd is illegal"); /* 780 */

            switch (pPUHead->iCategory)                                 /* 782 */
            {
            case SPC_VUVN:
            {
                vuvnprim = (u_int *)pPUHead;                            /* 787 */
                break;
            }
            case SPC_MESH:
            {
                MappingMeshData(pPUHead, vuvnprim, pSGDHead);          /* 791 */
                pPUHead = pPUHead->pNext;
                continue;
            }
            case SPC_MATERIAL:
            {
                pPUHead->VUMaterialDesc.pMat =                         /* 799 */
                    pSGDHead->pMaterial + pPUHead->VUMaterialDesc.iMaterialIndex;
                break;
            }
            case SPC_COORDINATE:
            {
                MappingCoordinateData((u_int *)pPUHead, (HeaderSection *)pSGDHead); /* 804 */
                pPUHead = pPUHead->pNext;
                continue;
            }
            case SPC_BOUNDINGBOX:
            case SPC_NOP:
            {
                break;
            }
            case SPC_TRI2:
            {
                RebuildTRI2Files(pPUHead);                             /* 808 */
                pPUHead = pPUHead->pNext;
                continue;
            }
            case SPC_UNSUPPORTED:
            {
                G3DASSERT(0, "");                                      /* 0x32c, #exp == "0", msg == "" */
                pPUHead = pPUHead->pNext;
                continue;
            }
            default:
            {
                G3DASSERT(0, "pPUHead->iCategory : %d", pPUHead->iCategory); /* 818 */
                break;
            }
            }

            pPUHead = pPUHead->pNext;                                   /* 821 */
        }
    }
}

#endif

void sgdResetMaterialCache(SGDFILEHEADER *pSGDData)
{
    unsigned int i;

    for (i = pSGDData->usNumMaterial; i != 0; i--)
    {
        pSGDData->pMaterial[i - 1].iCacheStatus = -1;
    }
}


void sgdCalcCoordinateMatrix(SGDCOORDINATE *pCoord)
{
    SGDCOORDINATE *pParent;

    if (pCoord == NULL || pCoord == (SGDCOORDINATE *)-1 || pCoord->bCalc)
    {
        return;
    }

    pParent = pCoord->pParent;
    if (pParent == NULL || pParent == (SGDCOORDINATE *)-1)
    {
        sceVu0CopyMatrix(pCoord->matLocalWorld, pCoord->matCoord);
    }
    else
    {
        sgdCalcCoordinateMatrix(pParent);

        /* EE instruction order, restored.  sceVu0MulMatrix is backed by cglm,
         * which is column-major: glm_mat4_mul(a, b, dest) works out to
         * dest = b * a over the raw indices, so sceVu0MulMatrix(m0, m1, m2)
         * yields m0 = m2 * m1.  With that, passing the parent on the left --
         * as the original does -- composes the bone's own transform first,
         * which is what the row-vector convention here needs. */
        sceVu0MulMatrix(pCoord->matLocalWorld, pCoord->pParent->matLocalWorld, pCoord->matCoord);
    }

    pCoord->bCalc = true;
}

void sgdCalcBoneCoordinate(SGDCOORDINATE *pCoord, int iNumBlock)
{
    int i;

    for (i = 0; i < iNumBlock; i++)
    {
        pCoord[i].bCalc = 0;
    }

    for (i = 0; i < iNumBlock; i++)
    {
        sgdCalcCoordinateMatrix(&pCoord[i]);
    }
}

void sgdCalcCoordinate(SGDFILEHEADER *pSGDData, float matLocalWorld[4][4])
{
    int iNumBlock;

    G3DRETURN(pSGDData, "");

    iNumBlock = pSGDData->uiNumBlock;
    sceVu0CopyMatrix(pSGDData->pCoord->matCoord, matLocalWorld);
    sgdCalcBoneCoordinate(pSGDData->pCoord, iNumBlock - 1);
}

void sgdVerifyLightData(GRA3DLIGHTDATA *pRet, ZERO2LIGHTDATAFILE *pZLD)
{
    int i;

    G3DASSERT(pZLD, "Lightdata is illegal");                                            /* 0x3be, #exp == "pZLD" */
    G3DASSERT(pZLD->iSignature == GRA3DSIGNATURE_ZERO2LIGHTDATAFILE, "Lightdata is illegal");
    G3DASSERT(pZLD->iSizeOfThisFile == sizeof(ZERO2LIGHTDATAFILE), "Lightdata is illegal(iSizeOfThisFile:%d(sizeof( ZERO2LIGHTDATAFILE ):%d))", pZLD->iSizeOfThisFile, sizeof(ZERO2LIGHTDATAFILE));

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        G3DLIGHT *pLight = &pZLD->LD.aLight[i];

        if (pLight->Type == G3DLIGHT_POINT)
        {
            g3dxVu0CopyVector(pLight->vDirection, g_v0000);
        }

        pLight->fFalloff = 1.0f;
    }

    for (i = 0; i < NUM_GRA3DLIGHTID; i++)
    {
        G3DLIGHT *pLight = &pZLD->LD.aLight[i];

        G3DASSERT(pLight->Type == G3DLIGHT_DIRECTIONAL || pLight->fMaxRange >= pLight->fMinRange, "");
        G3DASSERT(pLight->Type != G3DLIGHT_SPOT      || pLight->fAngleOutside >= pLight->fAngleInside, "");
        G3DASSERT(pLight->Type != G3DLIGHT_POINT     || EqualMemory128(pLight->vDirection, g_v0000, 1), "");
        G3DASSERT(pLight->fFalloff == 1.0f, "");
    }

    memcpy(pRet, &pZLD->LD, sizeof(GRA3DLIGHTDATA));
}

void sgdGetLocalWorldMatrix(const void *pSGDTop, float mat[4][4], int iObjectId)
{
    const SGDFILEHEADER *pFH = (const SGDFILEHEADER *)pSGDTop;

    if (iObjectId < (int)pFH->uiNumBlock)
    {
        sceVu0CopyMatrix(mat, pFH->pCoord[iObjectId].matLocalWorld);
    }
}

static void sgdClearCoordCalcFlgParents(SGDCOORDINATE *pCoord)   /* recursive helper */
{
    SGDCOORDINATE *pParent;

    pParent = pCoord->pParent;
    if (pParent != NULL)
    {
        if (pParent == (SGDCOORDINATE *)&_heap_size)
        {
            pCoord->bCalc = 0;
            return;
        }

        sgdClearCoordCalcFlgParents(pParent);
    }

    pCoord->bCalc = 0;
}

/* Public entry point: invalidate one bone and every ancestor above it.  The ROM
 * overloads the name -- this is the two-argument form that looks the bone up in
 * the SGD, the static helper above is the recursion. */
void sgdClearCoordCalcFlgParents(void *pSGDData, int bone_no)
{
    SGDFILEHEADER *pFH = (SGDFILEHEADER *)pSGDData;

    if (bone_no >= (int)pFH->uiNumBlock - 1)                    /* 1035 */
    {
        printf("sgdClearCoordCalcFlgParents Assert\n");         /* 1036 */
        for (;;)                                                /* 1037 */
        {
        }
    }

    sgdClearCoordCalcFlgParents(&pFH->pCoord[bone_no]);         /* 1041 */
}

void sgdClearCoordCalcFlgAll(void *pSGDData)
{
    SGDFILEHEADER *pFH = (SGDFILEHEADER *)pSGDData;
    int            i;

    for (i = 0; i < (int)pFH->uiNumBlock - 1; i++)
    {
        pFH->pCoord[i].bCalc = 0;
    }
}

void sgdClearCoordCalcFlg(void *pSGDData, int bone_no)
{
    SGDFILEHEADER *pFH = (SGDFILEHEADER *)pSGDData;

    if (bone_no >= (int)pFH->uiNumBlock - 1)
    {
        printf("sgdClearCoordCalcFlg Assert\n");
        for (;;)
        {
        }
    }

    pFH->pCoord[bone_no].bCalc = 0;
}

SGDPROCUNITHEADER *sgdGetProcUnit(SGDPROCUNITHEADER *pPUHead, int iProcUnitId, int iUnitIndex)
{
    int iCount = 0;

    for (SGDPROCUNITHEADER *p = pPUHead; p; p = p->pNext)
    {
        if (p->iCategory == iProcUnitId)
        {
            if (iCount == iUnitIndex)
            {
                return p;
            }
            iCount++;
        }
    }

    return NULL;
}

void sgdGetBoundingBox(SGDFILEHEADER *pFH, float avBB[8][4])
{
    /* The 8 corners follow the BB proc-unit header (the union payload); each
     * corner is grown along its own sign convention so the result is the
     * tightest AABB enclosing every block's box (negative faces take the min,
     * positive faces take the max -- see GRA3DBOUNDINGBOXVERTEXINDEX).        */
    memset(avBB, 0, sizeof(avBB));

    for (int i = 0; i < pFH->uiNumBlock; i++)
    {
        SGDPROCUNITHEADER *pPUHead;

        for (pPUHead = pFH->apProcUnitHead[i]; pPUHead; pPUHead = pPUHead->pNext)
        {
            if (pPUHead->iCategory == SPC_BOUNDINGBOX)
            {
                const float *avCorner = (const float *)&pPUHead->BoundingBoxDesc;

                /* corner 0 (---): all min */
                if (avCorner[0]  < avBB[BBVI_MMM][0]) { avBB[BBVI_MMM][0] = avCorner[0];  }
                if (avCorner[1]  < avBB[BBVI_MMM][1]) { avBB[BBVI_MMM][1] = avCorner[1];  }
                if (avCorner[2]  < avBB[BBVI_MMM][2]) { avBB[BBVI_MMM][2] = avCorner[2];  }

                /* corner 1 (+--): x max, y/z min */
                if (avBB[BBVI_PMM][0] < avCorner[4])  { avBB[BBVI_PMM][0] = avCorner[4];  }
                if (avCorner[5]  < avBB[BBVI_PMM][1]) { avBB[BBVI_PMM][1] = avCorner[5];  }
                if (avCorner[6]  < avBB[BBVI_PMM][2]) { avBB[BBVI_PMM][2] = avCorner[6];  }

                /* corner 2 (-+-): x min, y max, z min */
                if (avCorner[12] < avBB[BBVI_MPM][0]) { avBB[BBVI_MPM][0] = avCorner[12]; }
                if (avBB[BBVI_MPM][1] < avCorner[13]) { avBB[BBVI_MPM][1] = avCorner[13]; }
                if (avCorner[14] < avBB[BBVI_MPM][2]) { avBB[BBVI_MPM][2] = avCorner[14]; }

                /* corner 3 (++-): x/y max, z min */
                if (avBB[BBVI_PPM][0] < avCorner[16]) { avBB[BBVI_PPM][0] = avCorner[16]; }
                if (avBB[BBVI_PPM][1] < avCorner[17]) { avBB[BBVI_PPM][1] = avCorner[17]; }
                if (avCorner[18] < avBB[BBVI_PPM][2]) { avBB[BBVI_PPM][2] = avCorner[18]; }

                /* corner 4 (--+): x/y min, z max */
                if (avCorner[20] < avBB[BBVI_MMP][0]) { avBB[BBVI_MMP][0] = avCorner[20]; }
                if (avCorner[21] < avBB[BBVI_MMP][1]) { avBB[BBVI_MMP][1] = avCorner[21]; }
                if (avBB[BBVI_MMP][2] < avCorner[22]) { avBB[BBVI_MMP][2] = avCorner[22]; }

                /* corner 5 (+-+): x max, y min, z max */
                if (avBB[BBVI_PMP][0] < avCorner[24]) { avBB[BBVI_PMP][0] = avCorner[24]; }
                if (avCorner[25] < avBB[BBVI_PMP][1]) { avBB[BBVI_PMP][1] = avCorner[25]; }
                if (avBB[BBVI_PMP][2] < avCorner[26]) { avBB[BBVI_PMP][2] = avCorner[26]; }

                /* corner 6 (-++): x min, y/z max */
                if (avCorner[28] < avBB[BBVI_MPP][0]) { avBB[BBVI_MPP][0] = avCorner[28]; }
                if (avBB[BBVI_MPP][1] < avCorner[29]) { avBB[BBVI_MPP][1] = avCorner[29]; }
                if (avBB[BBVI_MPP][2] < avCorner[30]) { avBB[BBVI_MPP][2] = avCorner[30]; }

                /* corner 7 (+++): all max */
                if (avBB[BBVI_PPP][0] < avCorner[32]) { avBB[BBVI_PPP][0] = avCorner[32]; }
                if (avBB[BBVI_PPP][1] < avCorner[33]) { avBB[BBVI_PPP][1] = avCorner[33]; }
                if (avBB[BBVI_PPP][2] < avCorner[34]) { avBB[BBVI_PPP][2] = avCorner[34]; }
            }
        }
    }
}

void sgdRemapInverse(SGDFILEHEADER *pSGDHead)
{
    unsigned int i;
    SGDVECTORINFO *pVectorInfo;
    SGDCOORDINATE *pCoord;
    SGDMATERIAL *pMaterial;

    G3DRETURN(pSGDHead, "");
    G3DRETURN(pSGDHead->uiVersionId == SGD_VALID_VERSIONID, "");

    if (pSGDHead->ucMapFlag == 0)
    {
        return;
    }
    MioPan_Graph3dInvalidateSkinCache(pSGDHead);
    pSGDHead->ucMapFlag = 0;

    pCoord = pSGDHead->pCoord;
    pMaterial = pSGDHead->pMaterial;
    if (pCoord != NULL)
    {
        for (i = 0; i < pSGDHead->uiNumBlock - 1; i++)
        {
            SGDCOORDINATE &rCoord = pCoord[i];
            SGDCOORDINATE *pParent = rCoord.pParent;

            if (pParent == NULL)
            {
                rCoord.pParent.setRaw(-1);
            }
            else
            {
                rCoord.pParent.setRaw(indexof<SGDCOORDINATE>(pCoord, pParent));
            }
        }
    }

    for (i = 0; i < pSGDHead->uiNumBlock; i++)
    {
        u_int *vuvnprim = NULL;

        for (SGDPROCUNITHEADER *pPUHead = pSGDHead->apProcUnitHead[i];
             pPUHead != NULL; )
        {
            SGDPROCUNITHEADER *pNext = pPUHead->pNext;

            if (pNext == NULL)
            {
                break;
            }

            switch (pPUHead->iCategory)
            {
            case SPC_VUVN:
            {
                vuvnprim = (u_int *)pPUHead;
                break;
            }
            case SPC_MESH:
            {
                MappingMeshDataInverse(pPUHead, vuvnprim, pSGDHead);
                break;
            }
            case SPC_MATERIAL:
            {
                pPUHead->VUMaterialDesc.iMaterialIndex =
                    indexof<SGDMATERIAL>(pMaterial,
                                         (SGDMATERIAL *)pPUHead->VUMaterialDesc.pMat);
                break;
            }
            case SPC_COORDINATE:
            {
                MappingCoordinateDataInverse(pPUHead, pSGDHead);
                break;
            }
            case SPC_TRI2:
            {
                RebuildTRI2FilesInverse(pPUHead);
                break;
            }
            default:
            {
                break;
            }
            }

            pPUHead = pNext;
        }
    }

    pVectorInfo = pSGDHead->pVectorInfo;
    if (pVectorInfo != NULL)
    {
        for (i = 0; i < pVectorInfo->uiNumAddress; i++)
        {
            SGDVECTORADDRESS &rVA = pVectorInfo->aAddress[i];

            if (rVA.uiSize != 0 && rVA.pvVertex)
            {
                rVA.pvVertex.setRaw(SGD_UNMAP(pSGDHead, (sceVu0FVECTOR *)rVA.pvVertex));
            }
            if (rVA.uiSize > 1 && rVA.pvNormal)
            {
                rVA.pvNormal.setRaw(SGD_UNMAP(pSGDHead, (sceVu0FVECTOR *)rVA.pvNormal));
            }
            if (rVA.uiSize > 2 && rVA.pVertexList)
            {
                rVA.pVertexList.setRaw(SGD_UNMAP(pSGDHead, (_VERTEXLIST *)rVA.pVertexList));
            }
        }
    }

    for (i = 0; i < pSGDHead->uiNumBlock; i++)
    {
        if (pSGDHead->apProcUnitHead[i])
        {
            pSGDHead->apProcUnitHead[i].setRaw(SGD_UNMAP(pSGDHead,
                                                         (SGDPROCUNITHEADER *)pSGDHead->apProcUnitHead[i]));
        }
    }
}

#if 0
void sgdRemapInverse(SGDFILEHEADER *pSGDHead)
{
    SGDPROCUNITHEADER **apProcUnitHead;
    unsigned int        i;
    int                 iIndex;
    u_int              *vuvnprim;
    SGDPROCUNITHEADER  *pPUHead;

    G3DRETURN(pSGDHead, "");                                             /* 0x46e */
    G3DRETURN(pSGDHead->uiVersionId == SGD_VALID_VERSIONID, "");         /* 0x471 */

    if (pSGDHead->ucMapFlag == 0)
    {
        return;
    }
    pSGDHead->ucMapFlag = 0;

    if (pSGDHead->pCoord)                                               /* coordinate parents -> indices */
    {
        for (i = 0; i < pSGDHead->uiNumBlock - 1; i++)
        {
            SGDCOORDINATE &rCoord = pSGDHead->pCoord[i];

            if (rCoord.pParent == NULL)
            {
                rCoord.pParent = (SGDCOORDINATE *)-1;
            }
            else
            {
                iIndex = indexof<SGDCOORDINATE>(pSGDHead->pCoord, rCoord.pParent);
                G3DASSERT((int)iIndex <= (int)pSGDHead->uiNumBlock - 2, "こーでねーとの親子関係がおかしい風\nj : %d, pSGDHead->uiNumBlock : %d", iIndex, pSGDHead->uiNumBlock);
                rCoord.pParent = (SGDCOORDINATE *)iIndex;
            }
        }

        pSGDHead->pCoord = (SGDCOORDINATE *)SGD_UNMAP(pSGDHead, pSGDHead->pCoord);
    }

    if (pSGDHead->pVectorInfo)                                          /* buffers -> offsets */
    {
        SGDVECTORINFO *pVectorInfo = pSGDHead->pVectorInfo;

        for (i = 0; i < pVectorInfo->uiNumAddress; i++)
        {
            SGDVECTORADDRESS &rVA = pVectorInfo->aAddress[i];

            if (rVA.uiSize != 0 && rVA.pvVertex)
            {
                rVA.pvVertex = (sceVu0FVECTOR *)SGD_UNMAP(pSGDHead, rVA.pvVertex);
            }
            if (rVA.uiSize > 1 && rVA.pvNormal)
            {
                rVA.pvNormal = (sceVu0FVECTOR *)SGD_UNMAP(pSGDHead, rVA.pvNormal);
            }
            if (rVA.uiSize > 2 && rVA.pVertexList)
            {
                rVA.pVertexList = (_VERTEXLIST *)SGD_UNMAP(pSGDHead, rVA.pVertexList);
            }
        }

        if (pVectorInfo->aAddress[0].pVertexList && pVectorInfo->uiNumAddress == 4)
        {
            pVectorInfo->aAddress[SVA_UNIQUE].pVertexList = NULL;
            pVectorInfo->aAddress[SVA_COMMON].pVertexList = NULL;
            pVectorInfo->aAddress[SVA_WEIGHTED].pVertexList = NULL;
        }

        pSGDHead->pVectorInfo = (SGDVECTORINFO *)SGD_UNMAP(pSGDHead, pVectorInfo);
    }

    apProcUnitHead = pSGDHead->apProcUnitHead;
    for (i = 0; i < pSGDHead->uiNumBlock; i++)
    {
        vuvnprim = NULL;

        for (pPUHead = apProcUnitHead[i]; pPUHead; )
        {
            SGDPROCUNITHEADER *pNext;

            G3DASSERT(!((int)pPUHead & 0xf), "sgd is illegal");        /* 0x514 */

            pNext = pPUHead->pNext;
            if (pNext == NULL)
            {
                break;
            }

            switch (pPUHead->iCategory)
            {
            case SPC_VUVN:
            {
                vuvnprim = (u_int *)pPUHead;
                break;
            }
            case SPC_MESH:
            {
                MappingMeshDataInverse(pPUHead, vuvnprim, pSGDHead);
                pNext = pPUHead->pNext;
                break;
            }
            case SPC_MATERIAL:
            {
                pPUHead->VUMaterialDesc.iMaterialIndex =
                    indexof<SGDMATERIAL>(pSGDHead->pMaterial,
                                         pPUHead->VUMaterialDesc.pMat);
                pNext = pPUHead->pNext;
                break;
            }
            case SPC_COORDINATE:
            {
                MappingCoordinateDataInverse(pPUHead, pSGDHead);
                pPUHead->pNext = (SGDPROCUNITHEADER *)SGD_UNMAP(pPUHead, pNext);
                pPUHead = pNext;
                continue;
            }
            case SPC_BOUNDINGBOX:
            case SPC_NOP:
            {
                break;
            }
            case SPC_TRI2:
            {
                RebuildTRI2FilesInverse(pPUHead);
                pPUHead->pNext = (SGDPROCUNITHEADER *)SGD_UNMAP(pPUHead, pNext);
                pPUHead = pNext;
                continue;
            }
            case SPC_UNSUPPORTED:
            {
                G3DASSERT(0, "");                                      /* 0x539 */
                pNext = pPUHead->pNext;
                break;
            }
            default:
            {
                G3DASSERT(0, "pPUHead->iCategory : %d", pPUHead->iCategory); /* 0x53f */
                pNext = pPUHead->pNext;
                break;
            }
            }

            pPUHead->pNext = (SGDPROCUNITHEADER *)SGD_UNMAP(pPUHead, pNext);
            pPUHead = pNext;
        }

        if (apProcUnitHead[i])
        {
            apProcUnitHead[i] = (SGDPROCUNITHEADER *)SGD_UNMAP(pSGDHead, apProcUnitHead[i]);
        }
    }

    if (pSGDHead < (SGDFILEHEADER *)pSGDHead->pMaterial)                /* material table -> offset */
    {
        pSGDHead->pMaterial = (SGDMATERIAL *)SGD_UNMAP(pSGDHead, pSGDHead->pMaterial);
        G3DASSERT(!((u_int)pSGDHead->pMaterial & 0xf), "データのアラインがおかしいよ。");   /* 0x552 */
    }
}
#endif
