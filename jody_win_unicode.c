/* Jody Bruchon's Windows Unicode helper routines
 *
 * Copyright (C) 2014-2018 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */
#include "jdupes.h"

#ifdef UNICODE
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
/* Convert slashes to backslashes in a file path */
extern void slash_convert(char *path)
{
  while (*path != '\0') {
    if (*path == '/') *path = '\\';
    path++;
  }
  return;
}


/* Copy Windows wide character arguments to UTF-8 */
extern void widearg_to_argv(int argc, wchar_t **wargv, char **argv)
{
  static char temp[PATHBUF_SIZE * 2];
  int len;

  if (!argv) goto error_bad_argv;
  for (int counter = 0; counter < argc; counter++) {
    len = W2M(wargv[counter], &temp);
    if (len < 1) goto error_wc2mb;

    argv[counter] = (char *)string_malloc((size_t)len + 1);
    if (!argv[counter]) oom("widearg_to_argv()");
    strncpy(argv[counter], temp, (size_t)len + 1);
  }
  return;

error_bad_argv:
  fprintf(stderr, "fatal: bad argv pointer\n");
  exit(EXIT_FAILURE);
error_wc2mb:
  fprintf(stderr, "fatal: WideCharToMultiByte failed\n");
  exit(EXIT_FAILURE);
}


/* Print a string that is wide on Windows but normal on POSIX */
extern int fwprint(FILE * const restrict stream, const char * const restrict str, const int cr)
{
  int retval;
  int stream_mode = out_mode;

  if (stream == stderr) stream_mode = err_mode;

  if (stream_mode == _O_U16TEXT) {
    /* Convert to wide string and send to wide console output */
    if (!M2W(str,wstr)) return -1;
    fflush(stream);
    _setmode(_fileno(stream), stream_mode);
    retval = fwprintf(stream, L"%S%S", wstr, cr ? L"\n" : L"");
    fflush(stream);
    _setmode(_fileno(stream), _O_TEXT);
    return retval;
  } else {
    return fprintf(stream, "%s%s", str, cr ? "\n" : "");
  }
}
#else
 #define fwprint(a,b,c) fprintf(a, "%s%s", b, c ? "\n" : "")
 #define slash_convert(a)
#endif /* UNICODE */
