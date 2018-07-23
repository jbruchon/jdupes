/* Jody Bruchon's Windows Unicode helper routines
 * See jody_win_unicode.c for license information */

#ifndef JODY_WIN_UNICODE_H
#define JODY_WIN_UNICODE_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"

#ifdef UNICODE
extern void slash_convert(char *path);
extern void widearg_to_argv(int argc, wchar_t **wargv, char **argv);
extern int fwprint(FILE * const restrict stream, const char * const restrict str, const int cr);
extern int fwprint0(FILE * const restrict stream, const char * const restrict str);
#else
 #define fwprint(a,b,c) fprintf(a, "%s%s", b, c ? "\n" : "")
 #define fwprint0(a,b) fprintf(a, "%s%c", b, 0)
 #define slash_convert(a)
#endif /* UNICODE */

#ifdef __cplusplus
}
#endif

#endif /* JODY_WIN_UNICODE_H */
