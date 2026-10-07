/* Print matched file sets
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "jdupes.h"
#include <libjodycode.h>
#include "act_printmatches.h"

void printmatches(file_t * restrict files)
{
  file_t * restrict tmpfile;
  int printed = 0;
  int cr = 1;

  LOUD(fprintf(stderr, "printmatches: %p\n", files));

  if (ISFLAG(a_flags, FA_PRINTNULL)) cr = 2;

  while (files != NULL) {
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      printed = 1;
      if (!ISFLAG(a_flags, FA_OMITFIRST)) {
        if (ISFLAG(a_flags, FA_SHOWSIZE)) printf("%" PRIdMAX " byte%c each:\n", (intmax_t)files->size,
            (files->size != 1) ? 's' : ' ');
        jc_fwprint(stdout, files->d_name, cr);
      }
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        jc_fwprint(stdout, tmpfile->d_name, cr);
        tmpfile = tmpfile->duplicates;
      }
      if (files->next != NULL) jc_fwprint(stdout, "", cr);

    }

    files = files->next;
  }

  if (printed == 0) printf("%s", s_no_dupes); else jc_fwprint(stdout, "", 1);

  return;
}


/* Print files that have no duplicates (unique files) */
void printunique(file_t *files)
{
  file_t *chain, *scan;
  int printed = 0;
  int cr = 1;

  LOUD(fprintf(stderr, "print_uniques: %p\n", files));

  if (ISFLAG(a_flags, FA_PRINTNULL)) cr = 2;

  scan = files;
  while (scan != NULL) {
    if (ISFLAG(scan->flags, FF_HAS_DUPES)) {
      chain = scan;
      while (chain != NULL) {
        SETFLAG(chain->flags, FF_NOT_UNIQUE);
        chain = chain->duplicates;
      }
    }
    scan = scan->next;
  }

  while (files != NULL) {
    if (!ISFLAG(files->flags, FF_NOT_UNIQUE)) {
      printed = 1;
      if (ISFLAG(a_flags, FA_SHOWSIZE)) printf("%" PRIdMAX " byte%c each:\n", (intmax_t)files->size,
          (files->size != 1) ? 's' : ' ');
      jc_fwprint(stdout, files->d_name, cr);
    }
    files = files->next;
  }

  if (printed == 0) jc_fwprint(stderr, "No unique files found.", 1); else jc_fwprint(stdout, "", 1);

  return;
}
