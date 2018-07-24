/* Print null terminated matched file sets
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "jdupes.h"
#include "jody_win_unicode.h"
#include "act_printnull.h"

extern void printnull(file_t * restrict files)
{
  file_t * restrict tmpfile;
  int printed = 0;

  LOUD(fprintf(stderr, "act_printnull: %p\n", files));

  while (files != NULL) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      printed = 1;
      if (!ISFLAG(flags, F_OMITFIRST)) {
        fwprint0(stdout, files->d_name);
      }
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        fwprint0(stdout, tmpfile->d_name);
        tmpfile = tmpfile->duplicates;
      }
      if (files->next != NULL) fwprint0(stdout, "");

    }

    files = files->next;
  }

  if (printed == 0) fwprint(stderr, "No duplicates found.", 1);

  return;
}
