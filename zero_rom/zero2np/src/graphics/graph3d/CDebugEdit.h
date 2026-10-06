/* ==========================================================================
 *  CDebugEdit.h
 *
 *  The debug-edit object tree.  IEditObject is the abstract base for an
 *  editable debug value (label + on-change callback + parent back-pointer);
 *  CEditRoot is a non-leaf node that owns a vector of child IEditObjects and
 *  can look them up by index or by label.  The concrete editable value types
 *  (int/float/bool/enum editors) derive from IEditObject elsewhere.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _CDEBUGEDIT_H
#define _CDEBUGEDIT_H

#include <string>
#include <vector>
#include <typeinfo>

/* The prototype's string type: std::basic_string with the game's custom
 * char-traits and allocator.  Treated as std::string here. */
typedef std::string string;

class CEditRoot;

/* Callback invoked when an editable value changes. */
typedef void (*LPFUNC_ONEDITVALUECHANGED)(void *pContext);

/* --------------------------------------------------------------------------
 *  IEditObject -- abstract base for an editable debug value.       (0x14)
 * ------------------------------------------------------------------------ */
class IEditObject
{
protected:
    string                   m_strLabel;            /* 0x00 */
    LPFUNC_ONEDITVALUECHANGED m_fpOnValueChanged;   /* 0x04 */
    void                    *m_pContext;            /* 0x08 */
    CEditRoot               *m_pParentRoot;         /* 0x0c */
public:
                            /* __vtbl_ptr_type *$vf1310 @ 0x10 */
    IEditObject();
    virtual ~IEditObject();

    virtual int    IsItem() = 0;
    virtual char  *ValueString();
    virtual void   Inc(int bExecCallback);
    virtual void   Inc(float fScale, int bExecCallback);
    virtual void   Dec(int bExecCallback);
    virtual void   Dec(float fScale, int bExecCallback);
    virtual void   Clear();
    char          *LabelString();
    virtual char  *TypeString();
    virtual std::type_info &TypeInfo();
    void           SetHandler(LPFUNC_ONEDITVALUECHANGED fp, void *pContext);
    CEditRoot     *GetParentRoot();
};

/* --------------------------------------------------------------------------
 *  CEditRoot -- a node owning a list of child edit objects.        (0x24)
 * ------------------------------------------------------------------------ */
class CEditRoot : public IEditObject
{
    std::vector<IEditObject *> m_vpEditItem;        /* 0x14 */
public:
    static void *operator new(size_t Size);
    static void *operator new [](size_t Size);
    static void  operator delete(void *pData, size_t Size);
    static void  operator delete [](void *pData, size_t Size);

    CEditRoot();
    CEditRoot(char *pStr, CEditRoot *pParentRoot);
    virtual ~CEditRoot();

    virtual int    IsItem();
    int            Create(char *strLabel);
    virtual void   Clear();
    CEditRoot     *AddRoot(char *pStr, ...);
    void           SwitchNextItem();
    void           SwitchPrevItem();
    int            GetNumObject();
    IEditObject   *GetObject(int n);
    IEditObject   *GetObject(char *strLabel, int n);
    virtual char  *TypeString();
};

#endif /* _CDEBUGEDIT_H */
