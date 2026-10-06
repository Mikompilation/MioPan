/* ==========================================================================
 *  CDebugEdit.cpp
 *
 *  Implementation of CEditRoot, the container node of the debug-edit object
 *  tree (see CDebugEdit.h).  A CEditRoot carries a label (inherited from
 *  IEditObject) and a vector of child IEditObject pointers; children are
 *  created with AddRoot (printf-formatted label), enumerated with
 *  GetNumObject / GetObject, looked up by label, and torn down by Clear /
 *  the destructor (which deletes every owned child).
 *
 *  The basic_string<>/vector<> template bodies, the ctl::custom_allocator
 *  and IEditObject RTTI/vtable thunks, and _delete_ContainerPtr_ForEach<>
 *  are all compiler-instantiated; they are not reproduced here.  Only the
 *  hand-written CEditRoot methods are reconstructed.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#include "CDebugEdit.h"
#include "g3dMemory.h"          /* g3dMalloc / g3dFree */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

/* --------------------------------------------------------------------------
 *  IEditObject default virtuals / accessors.
 * ------------------------------------------------------------------------ */
IEditObject::IEditObject()
    : m_strLabel()
{
    m_fpOnValueChanged = NULL;
    m_pContext         = NULL;
    m_pParentRoot      = NULL;
}

IEditObject::~IEditObject()
{
}

char *IEditObject::ValueString()
{
    return (char *)"";
}

void IEditObject::Inc(int bExecCallback)
{
    (void)bExecCallback;
}

void IEditObject::Inc(float fScale, int bExecCallback)
{
    (void)fScale;
    (void)bExecCallback;
}

void IEditObject::Dec(int bExecCallback)
{
    (void)bExecCallback;
}

void IEditObject::Dec(float fScale, int bExecCallback)
{
    (void)fScale;
    (void)bExecCallback;
}

void IEditObject::Clear()
{
}

char *IEditObject::LabelString()
{
    return (char *)m_strLabel.c_str();
}

char *IEditObject::TypeString()
{
    return (char *)"IEditObject";
}

std::type_info &IEditObject::TypeInfo()
{
    return const_cast<std::type_info &>(typeid(IEditObject));
}

void IEditObject::SetHandler(LPFUNC_ONEDITVALUECHANGED fp, void *pContext)
{
    m_fpOnValueChanged = fp;
    m_pContext         = pContext;
}

CEditRoot *IEditObject::GetParentRoot()
{
    return m_pParentRoot;
}

/* --------------------------------------------------------------------------
 *  CEditRoot allocator / simple virtuals.
 * ------------------------------------------------------------------------ */
void *CEditRoot::operator new(size_t Size)
{
    return g3dMalloc(Size, (char *)"CEditRoot");
}

void *CEditRoot::operator new [](size_t Size)
{
    return g3dMalloc(Size, (char *)"CEditRoot[]");
}

void CEditRoot::operator delete(void *pData, size_t Size)
{
    (void)Size;
    g3dFree(pData);
}

void CEditRoot::operator delete [](void *pData, size_t Size)
{
    (void)Size;
    g3dFree(pData);
}

int CEditRoot::IsItem()
{
    return 0;
}

int CEditRoot::GetNumObject()
{
    return (int)m_vpEditItem.size();
}

char *CEditRoot::TypeString()
{
    return (char *)"CEditRoot";
}

/* ==========================================================================
 *  Compiler-generated material (not reproduced):
 *    - basic_string<...>::Rep::clone / replace / alloc
 *    - ctl::vector<IEditObject *>::_M_insert_aux
 *    - _delete_ContainerPtr_ForEach<ctl::vector<IEditObject *> >
 *    - ctl::custom_allocator<char> / <IEditObject *> allocate/deallocate
 *    - type_info functions / IEditObject vtable thunks / operator new/delete
 * ======================================================================== */

/* --------------------------------------------------------------------------
 *  CEditRoot::CEditRoot
 *
 *  Default constructor: empty label, no callback/context/parent, empty
 *  child vector.
 * ------------------------------------------------------------------------ */
CEditRoot::CEditRoot()
    : IEditObject()
{
}

/* --------------------------------------------------------------------------
 *  CEditRoot::CEditRoot
 *
 *  Construct with a label string and a parent node.
 * ------------------------------------------------------------------------ */
CEditRoot::CEditRoot(char *pStr, CEditRoot *pParentRoot)
    : IEditObject()
{
    m_strLabel    = pStr;
    m_pParentRoot = pParentRoot;
}

/* --------------------------------------------------------------------------
 *  CEditRoot::~CEditRoot
 *
 *  Delete every owned child, then release the child vector and the label.
 * ------------------------------------------------------------------------ */
CEditRoot::~CEditRoot()
{
    int i;

    for (i = 0; i < (int)m_vpEditItem.size(); i++)
    {
        delete m_vpEditItem[i];
    }
    m_vpEditItem.clear();
}

/* --------------------------------------------------------------------------
 *  CEditRoot::Create
 *
 *  (Re)assign the node's label.
 * ------------------------------------------------------------------------ */
int CEditRoot::Create(char *strLabel)
{
    m_strLabel = strLabel;
    return 1;
}

/* --------------------------------------------------------------------------
 *  CEditRoot::Clear
 *
 *  Clear every child (vtable Clear, arg 3) then empty the child vector.
 * ------------------------------------------------------------------------ */
void CEditRoot::Clear()
{
    int i;

    for (i = 0; i < (int)m_vpEditItem.size(); i++)
    {
        IEditObject *pObject;

        pObject = m_vpEditItem[i];
        if (pObject != NULL)
        {
            pObject->Clear();
        }
        m_vpEditItem[i] = NULL;
    }
    m_vpEditItem.clear();
}

/* --------------------------------------------------------------------------
 *  CEditRoot::AddRoot
 *
 *  Allocate a child CEditRoot with a printf-formatted label, append it to
 *  the child vector and return it.
 * ------------------------------------------------------------------------ */
CEditRoot *CEditRoot::AddRoot(char *pStr, ...)
{
    char str[256];
    CEditRoot *pRoot;
    va_list VaList;

    va_start(VaList, pStr);
    vsprintf(str, pStr, VaList);
    va_end(VaList);

    pRoot = new CEditRoot(str, this);
    m_vpEditItem.push_back(pRoot);
    return pRoot;
}

/* --------------------------------------------------------------------------
 *  CEditRoot::SwitchNextItem / SwitchPrevItem
 *
 *  Stubbed out in the prototype.
 * ------------------------------------------------------------------------ */
void CEditRoot::SwitchNextItem()
{
}

void CEditRoot::SwitchPrevItem()
{
}

/* --------------------------------------------------------------------------
 *  CEditRoot::GetObject
 *
 *  Return the n-th child, or NULL if out of range.
 * ------------------------------------------------------------------------ */
IEditObject *CEditRoot::GetObject(int n)
{
    IEditObject *pObject;

    pObject = NULL;
    if (n < (int)m_vpEditItem.size())
    {
        pObject = m_vpEditItem[n];
    }
    return pObject;
}

/* --------------------------------------------------------------------------
 *  CEditRoot::GetObject
 *
 *  Return the n-th child whose label matches strLabel, or NULL.
 * ------------------------------------------------------------------------ */
IEditObject *CEditRoot::GetObject(char *strLabel, int n)
{
    int iCount;
    int i;

    iCount = 0;
    for (i = 0; i < (int)m_vpEditItem.size(); i++)
    {
        if (strcmp(strLabel, m_vpEditItem[i]->LabelString()) == 0)
        {
            if (iCount == n)
            {
                return m_vpEditItem[i];
            }
            iCount = iCount + 1;
        }
    }
    return NULL;
}
