/* ==========================================================================
 *  sdk/libscf.h  (SCE PS2 system-configuration library)
 * ======================================================================== */

#ifndef _SDK_LIBSCF_H
#define _SDK_LIBSCF_H

#define SCE_JAPANESE_LANGUAGE 0
#define SCE_ENGLISH_LANGUAGE 1
#define SCE_FRENCH_LANGUAGE 2
#define SCE_SPANISH_LANGUAGE 3
#define SCE_GERMAN_LANGUAGE 4
#define SCE_ITALIAN_LANGUAGE 5
#define SCE_DUTCH_LANGUAGE 6
#define SCE_PORTUGUESE_LANGUAGE 7

#ifdef __cplusplus
extern "C" {
#endif

int sceScfGetLanguage(void);

#ifdef __cplusplus
}
#endif

#endif /* _SDK_LIBSCF_H */
