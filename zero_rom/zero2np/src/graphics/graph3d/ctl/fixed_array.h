/* ==========================================================================
 *  ctl/fixed_array.h
 *
 *  Fixed-capacity, bounds-checked array container used throughout the zero2np
 *  engine (light tables, coordinate caches, furniture lists, ...).  The
 *  bounded stack built on top of it lives in ctl/fixed_stack.h.
 *
 *  Everything here is templated and fully inline, so it is emitted into each
 *  translation unit that instantiates it.  The two private helpers
 *  (_fixed_array_assert / _fixed_array_verifyrange<T>) are declared `static`
 *  for that reason -- which is exactly why a copy of each shows up at the top
 *  of gra3dSGD.c, gra3dSGDData.c, gra3dShadow.c, CBuff.c, and every other
 *  object file in the disassembly.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _CTL_FIXED_ARRAY_H
#define _CTL_FIXED_ARRAY_H

#include <stddef.h>             /* size_t                       */
#include <typeinfo>             /* typeid(T).name()             */
#include "../g3ddbg.h"          /* g3ddbgAssert (variadic)      */

/* --------------------------------------------------------------------------
 *  Range-check failure message.
 *
 *  This is the string the disassembly shows at str_777 / DAT_0039db00 etc.:
 *      "fixed_array<%s,%d>:illegal array access, out of range( nIndex:%d )"
 *  args: type name, capacity (_Nm), offending index.
 * ------------------------------------------------------------------------ */
#define FIXED_ARRAY_RANGE_MSG \
    "fixed_array<%s,%d>:illegal array access, out of range( nIndex:%d )"

/* Out-of-line-ish failure reporter.  One `static` copy per object file.
 * Mirrors _fixed_array_assert(char const*, size_t, size_t) in the binary,
 * which is just a thin wrapper around the variadic g3ddbgAssert. */
static void _fixed_array_assert(const char *strType, size_t nMax, size_t nIndex)
{
    g3ddbgAssert(false, FIXED_ARRAY_RANGE_MSG, strType, nMax, nIndex);
}

/* Bounds check used by operator[].  Asserts when v is outside [0, _max).
 *
 * The mangled symbol _fixed_array_verifyrange<T> returns T* and, in this
 * build, the body always returns 0 -- the return value is unused; the caller
 * (operator[]) does its own indexing right after.  Kept faithful to that. */
template <class T>
static T *_fixed_array_verifyrange(size_t v, size_t _max)
{
    if (_max <= v)
    {
        _fixed_array_assert(typeid(T).name(), _max, v);
    }
    return 0;
}

/* --------------------------------------------------------------------------
 *  fixed_array_base<T, _Nm, _Tp>
 *
 *  The storage + accessors.  _Tp is the raw array type (T[_Nm]); it is carried
 *  as a third template argument purely so the mangled names match the binary
 *  (e.g. fixed_array_base<G3DLIGHT,39,G3DLIGHT[39]>).
 * ------------------------------------------------------------------------ */
template <class T, size_t _Nm, class _Tp>
struct fixed_array_base
{
public:
    typedef T          value_type;
    typedef T         &reference;
    typedef const T   &const_reference;
    typedef T         *pointer;
    typedef const T   *const_pointer;
    typedef T         *iterator;
    typedef const T   *const_iterator;

protected:
    T m_aData[_Nm];

protected:
    /* Protected default ctor: only the derived fixed_array may construct. */
    fixed_array_base()
    {
    }

public:
    reference operator[](size_t n)
    {
        _fixed_array_verifyrange<T>(n, _Nm);
        return m_aData[n];
    }

    const_reference operator[](size_t n) const
    {
        _fixed_array_verifyrange<T>(n, _Nm);
        return m_aData[n];
    }

    pointer       data()        { return m_aData; }
    const_pointer data()  const { return m_aData; }

    iterator       begin()        { return m_aData; }
    const_iterator begin()  const { return m_aData; }

    iterator       end()        { return m_aData + _Nm; }
    const_iterator end()  const { return m_aData + _Nm; }

    void fill(const_reference value)
    {
        size_t i;

        for (i = 0; i < _Nm; i++)
        {
            m_aData[i] = value;
        }
    }

    size_t size()  const { return _Nm; }
    bool   empty() const { return _Nm == 0; }
};

/* --------------------------------------------------------------------------
 *  fixed_array<T, _Nm>
 *
 *  The user-facing container: a fixed_array_base with the raw-array type
 *  argument filled in.  Same size/layout as a plain T[_Nm].
 * ------------------------------------------------------------------------ */
template <class T, size_t _Nm>
struct fixed_array : public fixed_array_base<T, _Nm, T[_Nm]>
{
public:
    fixed_array()
    {
    }
};

/* Non-owning fixed-array view used by a few original translation units for
 * static tables emitted separately in .data/.sdata. */
template <class T, size_t _Nm>
struct fixed_array_base<T, _Nm, T *>
{
public:
    typedef T          value_type;
    typedef T         &reference;
    typedef const T   &const_reference;
    typedef T         *pointer;
    typedef const T   *const_pointer;
    typedef T         *iterator;
    typedef const T   *const_iterator;

protected:
    T *m_aData;

protected:
    fixed_array_base()
    {
        m_aData = 0;
    }

public:
    fixed_array_base(T *data)
    {
        m_aData = data;
    }

    reference operator[](size_t n)
    {
        _fixed_array_verifyrange<T>(n, _Nm);
        return m_aData[n];
    }

    const_reference operator[](size_t n) const
    {
        _fixed_array_verifyrange<T>(n, _Nm);
        return m_aData[n];
    }

    pointer       data()        { return m_aData; }
    const_pointer data()  const { return m_aData; }

    iterator       begin()        { return m_aData; }
    const_iterator begin()  const { return m_aData; }

    iterator       end()        { return m_aData + _Nm; }
    const_iterator end()  const { return m_aData + _Nm; }

    void fill(const_reference value)
    {
        size_t i;

        for (i = 0; i < _Nm; i++)
        {
            m_aData[i] = value;
        }
    }

    void reset(pointer data)
    {
        m_aData = data;
    }

    size_t size()  const { return _Nm; }
    bool   empty() const { return _Nm == 0; }
};

template <class T, size_t _Nm>
struct reference_fixed_array : public fixed_array_base<T, _Nm, T *>
{
public:
    reference_fixed_array()
        : fixed_array_base<T, _Nm, T *>()
    {
    }

    reference_fixed_array(T *data)
        : fixed_array_base<T, _Nm, T *>(data)
    {
    }
};

/* fixed_stack<T,_Nm> lives in its own header (ctl/fixed_stack.h), matching the
 * original tree -- it is built on the fixed_array above. */

#endif /* _CTL_FIXED_ARRAY_H */
