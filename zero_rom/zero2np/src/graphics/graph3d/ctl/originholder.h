/* ==========================================================================
 *  ctl/originholder.h
 *
 *  originholder<T> -- a tiny RAII save/restore guard.  On construction it
 *  snapshots the current value of the object it is handed; on destruction it
 *  writes that snapshot back, undoing whatever the enclosing scope did to the
 *  object in between.  gra3dShadow.c's _CalcColor uses it to temporarily widen
 *  the spotlight cone (`originholder<G3DLIGHT> oh(&s_Light)`), letting the
 *  global s_Light be mutated freely and restored automatically on return.
 *
 *  Layout (from the prototype's debug type info, originholder<G3DLIGHT> == 0x80):
 *      0x00  T *m_pOriginValue;   // the object to restore
 *      0x10  T  m_OriginValue;    // the saved snapshot
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _CTL_ORIGINHOLDER_H
#define _CTL_ORIGINHOLDER_H

template <class T>
struct originholder
{
private:
    T *m_pOriginValue;          /* 0x00 */
    T  m_OriginValue;           /* 0x10 */

public:
    /* snapshot *pValue so it can be restored on scope exit */
    originholder(T *pValue)
    {
        m_pOriginValue = pValue;
        store(*pValue);
    }

    /* restore the saved value into the held object */
    ~originholder()
    {
        *m_pOriginValue = m_OriginValue;
    }

    void store(const T &value)
    {
        m_OriginValue = value;
    }

    void restore()
    {
        *m_pOriginValue = m_OriginValue;
    }

private:
    /* non-copyable */
    originholder(const originholder<T> &);
    originholder<T> &operator=(const originholder<T> &);
};

#endif /* _CTL_ORIGINHOLDER_H */
