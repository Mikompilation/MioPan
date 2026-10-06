/* ==========================================================================
 *  ctl/fixed_stack.h
 *
 *  A bounded LIFO/append container built on fixed_array.  Used by the shadow
 *  renderer for the project-model list (fixed_stack<SGDFILEHEADER *, 40>
 *  s_stackpProjectModel).
 *
 *  IMPORTANT (recovered from the binary, see gra3dShadow.c's inlined bodies):
 *  this container has NO separate element-count field.  Its layout is exactly
 *
 *      { fixed_array<T,_Nm> c;  T null; }          // size == sizeof(c)+sizeof(T)
 *
 *  and `size()` is computed on demand by scanning `c` for the first slot equal
 *  to the `null` sentinel (capped at _Nm).  The whole of `c` is therefore
 *  initialised to `null`, and push() writes at the first free (== null) slot.
 *  Everything is templated and fully inline, so it is emitted per TU.
 *
 *  Reconstructed from the Feb 6 2004 prototype (SLES_523.84).
 * ======================================================================== */

#ifndef _CTL_FIXED_STACK_H
#define _CTL_FIXED_STACK_H

#include <stddef.h>             /* size_t */
#include "ctl/fixed_array.h"    /* fixed_array<T,_Nm> */

/* --------------------------------------------------------------------------
 *  fixed_stack<T, _Nm>
 * ------------------------------------------------------------------------ */
template <class T, size_t _Nm>
struct fixed_stack
{
public:
    typedef T        value_type;
    typedef T       &reference;
    typedef const T &const_reference;

protected:
    fixed_array<T, _Nm> c;          /* 0x00  underlying storage          */
    T                   null;       /* 0xNm  empty-slot / top()-empty sentinel */

public:
    /* Construct empty: null sentinel is T(0) and every slot starts == null. */
    fixed_stack()
    {
        null = (T)0;
        c.fill(null);
    }

    /* Construct with an explicit empty sentinel value. */
    fixed_stack(const_reference nullValue)
    {
        null = nullValue;
        c.fill(null);
    }

    bool   empty() const { return size() == 0; }
    size_t max_size() const { return _Nm; }

    /* size() == index of the first slot still holding the null sentinel. */
    size_t size() const
    {
        size_t i;

        for (i = 0; i < _Nm; i++)
        {
            if (c[i] == null)
            {
                return i;
            }
        }
        return _Nm;
    }

    reference top()
    {
        size_t n = size();

        if (n == 0)
        {
            return null;
        }
        return c[n - 1];
    }

    const_reference top() const
    {
        size_t n = size();

        if (n == 0)
        {
            return null;
        }
        return c[n - 1];
    }

    void push(const_reference value)
    {
        c[size()] = value;
    }

    void pop()
    {
        size_t n = size();

        if (n != 0)
        {
            c[n - 1] = null;
        }
    }

    /* Push only if the value is not already present. */
    void push_exclusive(const_reference value)
    {
        size_t n = size();
        size_t i;

        for (i = 0; i < n; i++)
        {
            if (c[i] == value)
            {
                return;
            }
        }
        push(value);
    }

    reference operator[](size_t n)
    {
        return c[n];
    }

    void clear()
    {
        c.fill(null);
    }
};

#endif /* _CTL_FIXED_STACK_H */
