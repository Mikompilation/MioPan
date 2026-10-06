#ifndef _G3DDBG_H
#define _G3DDBG_H

/* ===================== g3ddbg.h ===================================== *
 * Mangled symbols fix the prototypes exactly:
 *   _SetLineInfo__FPCciT0T0   -> (const char*, int, const char*, const char*)
 *   g3ddbgAssert__FbPCce      -> (bool, const char*, ...)   // 'e' = ellipsis
 *   g3ddbgWarning__FbPCce     -> (bool, const char*, ...)
 * g3ddbgAssert / g3ddbgWarning format their message printf-style.
 * ------------------------------------------------------------------- */
typedef struct _G3DLINEINFO
{
    const char *pExpression;
    const char *pFileName;
    int         iLine;
    const char *pFunctionName;
} _G3DLINEINFO;

void _SetLineInfo (const char *file, int line, const char *func, const char *expr);
void g3ddbgAssert (int cond, const char *fmt, ...);
void g3ddbgWarning(int cond, const char *fmt, ...);
int  g3ddbgPrintf (const char *fmt, ...);
void g3ddbgPrintReturn(int has_retval, int retval);

/* Stash __FILE__/__LINE__/__FUNCTION__ and the *stringized condition*,
 * then evaluate the (variadic) check. The condition text is what the
 * debugger prints; the fmt+args are the human/JP message. */
#define G3DASSERT(exp, ...)                                                 \
    do { if (!(exp)) {                                                      \
\
            _SetLineInfo(__FILE__, __LINE__, __FUNCTION__, #exp);           \
            g3ddbgAssert((exp) != 0, __VA_ARGS__);                          \
    } } while (0)

#define G3DWARNING(exp, ...)                                                \
    do { if (!(exp)) {                                                      \
            _SetLineInfo(__FILE__, __LINE__, __FUNCTION__, #exp);           \
            g3ddbgWarning((exp) != 0, __VA_ARGS__);                         \
    } } while (0)

/* Non-fatal check that returns void on failure. NOTE the macro evaluates
 * `exp` twice (warning arg + return guard); for a memory operand the
 * compiler reloads it, which is exactly the "double test" you see in the
 * version checks of sgdRemap / sgdRemapInverse. For a register-constant
 * operand (the NULL checks) the two tests CSE into one. */
#define G3DRETURN(exp, ...)                                                 \
    do { G3DWARNING(exp, ##__VA_ARGS__);                                    \
         if (!(exp)) {                                                      \
            _SetLineInfo(__FILE__, __LINE__, __FUNCTION__, #exp);           \
            g3ddbgPrintReturn(0, 0);                                        \
            return;                                                         \
    } } while (0)

/* Value-returning variant of G3DRETURN: warns then `return retval;` on
 * failure.  The failure log carries the returned value ("[G3DRETURN:%d]..."). */
#define G3DRETURNVAL(exp, retval, ...)                                      \
    do { G3DWARNING(exp, ##__VA_ARGS__);                                    \
         if (!(exp)) {                                                      \
            _SetLineInfo(__FILE__, __LINE__, __FUNCTION__, #exp);           \
            g3ddbgPrintReturn(1, (int)(retval));                            \
            return (retval);                                                \
    } } while (0)

#endif /* _G3DDBG_H */
