/* ==========================================================================
 *  g3dRenderTarget.h
 *
 *  CRenderTarget and its auto-state machinery.  A render target owns a GS TEX0
 *  describing the destination buffer (its TBP/TBW and the width/height encoded
 *  in the TW/TH log2 fields), a clear colour and a max-Z.  Begin() programs the
 *  GS FRAME / XYOFFSET / SCISSOR for that buffer (saving the previous values on
 *  an auto-state stack); End() restores them; Clear() paints the whole target
 *  with a flat sprite.
 *
 *  The save/restore is handled by two RAII-style helpers, CAutoGsRegisters<N>
 *  (push/pop a run of N GS registers) and CAutoTransform<S> (push/pop a
 *  transform-state matrix), both deriving from IAutoState -> IG3DCompatible.
 *  IG3DCompatible holds the function-pointer bridge back into the g3d core
 *  (set/get GS register, set/get transform) so the resource layer need not
 *  link g3dCore directly.
 *
 *  Implemented in g3dRenderTarget.cpp.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _G3DRENDERTARGET_H
#define _G3DRENDERTARGET_H

#include "eetypes.h"
#include <libvu0.h>             /* sceVu0CopyMatrix */
#include "sce_gs.h"             /* sceGsTex0, sceGifPackAd */
#include "g3dMath.h"            /* sceVu0FMATRIX aliases */
#include "gra3dTypes.h"         /* G3DCOLOR, RENDERTARGETCREATIONDATA, G3DTRANSFORMSTATETYPE */
#include "g3ddbg.h"

/* ---- function-pointer bridge into the g3d core ------------------------- */
typedef int        (*LPFUNC_SETGSREGISTER)(long int lData, long int lAddress, int iDmaChan);
typedef int        (*LPFUNC_SETGSREGISTERS)(const sceGifPackAd *aGPA, int iNum, int iDmaChan);
typedef long int  &(*LPFUNC_GETGSREGISTERREF)(long int lAddress);
typedef int        (*LPFUNC_SETTRANSFORM)(G3DTRANSFORMSTATETYPE State, const float (*mat)[4]);
typedef float    (&(*LPFUNC_GETTRANSFORMREF)(G3DTRANSFORMSTATETYPE State))[4][4];

/* --------------------------------------------------------------------------
 *  IG3DCompatible
 *
 *  Bridge base: holds the static function pointers wired to the g3d core, and
 *  the protected forwarders the resource classes call through.
 * ------------------------------------------------------------------------ */
class IG3DCompatible
{
public:
    static LPFUNC_SETGSREGISTER   s_pFuncSetGsRegister;
    static LPFUNC_SETGSREGISTERS  s_pFuncSetGsRegisters;
    static LPFUNC_GETGSREGISTERREF s_pFuncGetGsRegisterRef;
    static LPFUNC_SETTRANSFORM    s_pFuncSetTransform;
    static LPFUNC_GETTRANSFORMREF s_pFuncGetTransformRef;

protected:
    int SetGsRegister(long int lData, long int lAddress, int iDmaChan)
    {
        return s_pFuncSetGsRegister(lData, lAddress, iDmaChan);
    }

    int SetGsRegisters(const sceGifPackAd *aGPA, int iNum, int iDmaChan)
    {
        return s_pFuncSetGsRegisters(aGPA, iNum, iDmaChan);
    }

    long int &GetGsRegisterRef(long int lAddress)
    {
        return s_pFuncGetGsRegisterRef(lAddress);
    }

    int SetTransform(G3DTRANSFORMSTATETYPE State, const float (*mat)[4])
    {
        return s_pFuncSetTransform(State, mat);
    }

    float (&GetTransformRef(G3DTRANSFORMSTATETYPE State))[4][4]
    {
        return s_pFuncGetTransformRef(State);
    }
};

/* --------------------------------------------------------------------------
 *  IAutoState
 *
 *  Single-level save/restore guard.  PushStack / PopStack guard the depth
 *  (max 1) and Pop() is overridden by each concrete helper to do the restore.
 * ------------------------------------------------------------------------ */
class IAutoState : public IG3DCompatible
{
private:
    int m_iNumStack;            /* 0x4 */

protected:
    enum { MAX_STACK = 1 };

    int PushStack(void)
    {
        G3DRETURNVAL((m_iNumStack < MAX_STACK), 0, "Stack overflow");
        m_iNumStack++;
        return 1;
    }

    int PopStack(void)
    {
        G3DRETURNVAL((m_iNumStack > 0), 0, "Stack underflow");
        m_iNumStack--;
        return 1;
    }

    int GetNumStack(void)
    {
        return m_iNumStack;
    }

public:
    IAutoState()
    {
        m_iNumStack = 0;
    }

    virtual void Pop(void) = 0;
};

/* --------------------------------------------------------------------------
 *  CAutoGsRegisters<N>
 *
 *  Save N GS registers on Push (capturing the current values from the core)
 *  and restore them on Pop.
 * ------------------------------------------------------------------------ */
template <int N>
class CAutoGsRegisters : public IAutoState
{
private:
    sceGifPackAd m_aGPA[N];     /* 0x10 */

public:
    void Push(const sceGifPackAd *aGPA)
    {
        int i;

        if (PushStack() != 0)
        {
            for (i = 0; i < N; i = i + 1)
            {
                m_aGPA[i].ADDR = aGPA[i].ADDR;
                m_aGPA[i].DATA = GetGsRegisterRef(aGPA[i].ADDR);
            }
            SetGsRegisters(aGPA, N, 1);
        }
    }

    virtual void Pop(void)
    {
        if (PopStack() != 0)
        {
            SetGsRegisters(m_aGPA, N, 1);
        }
    }

    u_long &operator[](int i)
    {
        return m_aGPA[i].DATA;
    }
};

/* --------------------------------------------------------------------------
 *  CAutoTransform<S>
 *
 *  Save the transform-state matrix S on Push and restore it on Pop.
 * ------------------------------------------------------------------------ */
template <G3DTRANSFORMSTATETYPE S>
class CAutoTransform : public IAutoState
{
private:
    float m_mat[4][4];          /* 0x10 */

public:
    void Push(const float (*mat)[4])
    {
        if (PushStack() != 0)
        {
            float (&rmat)[4][4] = GetTransformRef(S);
            sceVu0CopyMatrix(m_mat, (float (*)[4])rmat);
            SetTransform(S, mat);
        }
    }

    virtual void Pop(void)
    {
        if (PopStack() != 0)
        {
            SetTransform(S, m_mat);
        }
    }
};

/* --------------------------------------------------------------------------
 *  CRenderTarget
 * ------------------------------------------------------------------------ */
class CRenderTarget : public IG3DCompatible
{
private:
    sceGsTex0                   m_gsTex0;            /* 0x08 */
    CAutoGsRegisters<3>         m_AutoGsRegisters;   /* 0x10 */
    CAutoTransform<G3DTS_VIEW>  m_AutoTransformView; /* 0x50 */
    G3DCOLOR                    m_ClearColor;        /* 0xa0 */
    float                       m_fZMax;             /* 0xa4 */

public:
    CRenderTarget();
    virtual ~CRenderTarget();

    int  Create(const RENDERTARGETCREATIONDATA *pCD);
    void Begin(void);
    void End(void);
    void Clear(G3DCOLOR Color);
    void Clear(void);

    void SetClearColor(G3DCOLOR Color)
    {
        m_ClearColor = Color;
    }

    void SetWidth(int iWidth);
    void SetHeight(int iHeight);
    int  GetWidth(void);
    int  GetHeight(void);

    sceGsTex0 &GetGsTex0Ref(void)
    {
        return m_gsTex0;
    }
};

#endif /* _G3DRENDERTARGET_H */
