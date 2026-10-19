/* jdupes file check functions
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <string.h>
#include <libgen.h>
#include <inttypes.h>

#include <libjodycode.h>
#include "likely_unlikely.h"
#ifndef NO_EXTFILTER
 #include "extfilter.h"
#endif
#include "filestat.h"
#include "jdupes.h"


/***** End definitions, begin code *****/

/***** Add new functions here *****/


/* Check a pair of files for match exclusion conditions
 * Returns:
 *  0 if all condition checks pass
 * -1 or 1 on compare result less/more
 * -2 on an absolute exclusion condition met
 *  2 on an absolute match condition met
 * -3 on exclusion due to isolation
 * -4 on exclusion due to same filesystem
 * -5 on exclusion due to permissions */
int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2)
{
  DBG(if (unlikely(file1 == NULL || file2 == NULL || file1->d_name == NULL || file2->d_name == NULL)) jc_nullptr("check_conditions()");)

  LOUD(fprintf(stderr, "check_conditions('%s', '%s')\n", file1->d_name, file2->d_name);)

  /* Exclude files that are not the same size */
  if (file1->size > file2->size) {
    LOUD(fprintf(stderr, "check_conditions: no match: size of file1 > file2 (%" PRIdMAX " > %" PRIdMAX ")\n",
      (intmax_t)file1->size, (intmax_t)file2->size));
    return -1;
  }
  if (file1->size < file2->size) {
    LOUD(fprintf(stderr, "check_conditions: no match: size of file1 < file2 (%" PRIdMAX " < %"PRIdMAX ")\n",
      (intmax_t)file1->size, (intmax_t)file2->size));
    return 1;
  }

#ifndef NO_USER_ORDER
  /* Exclude based on -I/--isolate */
  if (ISFLAG(flags, F_ISOLATE) && (file1->user_order == file2->user_order)) {
    LOUD(fprintf(stderr, "check_conditions: files ignored: parameter isolation\n"));
    return -3;
  }
#endif /* NO_USER_ORDER */

  /* Exclude based on -1/--one-file-system */
  if (ISFLAG(flags, F_ONEFS) && (file1->device != file2->device)) {
    LOUD(fprintf(stderr, "check_conditions: files ignored: not on same filesystem\n"));
    return -4;
  }

   /* Exclude files by permissions if requested */
  if (ISFLAG(flags, F_PERMISSIONS) &&
          (file1->mode != file2->mode
#ifndef NO_PERMS
          || file1->uid != file2->uid
          || file1->gid != file2->gid
#endif
          )) {
    return -5;
    LOUD(fprintf(stderr, "check_conditions: no match: permissions/ownership differ (-p on)\n"));
  }

  /* Hard link and symlink + '-s' check */
#ifndef NO_HARDLINKS
  if ((file1->inode == file2->inode) && (file1->device == file2->device)) {
    if (ISFLAG(flags, F_CONSIDERHARDLINKS)) {
      LOUD(fprintf(stderr, "check_conditions: files match: hard/soft linked (-H on)\n"));
      return 2;
    } else {
      LOUD(fprintf(stderr, "check_conditions: files ignored: hard/soft linked (-H off)\n"));
      return -2;
    }
  } else if (ISFLAG(a_flags, FA_BREAKLINKS)) {
    LOUD(fprintf(stderr, "check_conditions: files ignored: not hard-linked (-K on)\n"));
    return -6;
  }
#endif

  /* Fall through: all checks passed */
  LOUD(fprintf(stderr, "check_conditions: all condition checks passed\n"));
  return 0;
}


/* Check for exclusion conditions for a single file (1 = fail) */
int check_singlefile(file_t * const restrict newfile)
{
  char * restrict tp = tempname;

  if (unlikely(newfile == NULL)) jc_nullptr("check_singlefile()");

  LOUD(fprintf(stderr, "check_singlefile: checking '%s'\n", newfile->d_name));

  /* Exclude hidden files if requested */
  if (likely(ISFLAG(flags, F_EXCLUDEHIDDEN))) {
    if (unlikely(newfile->d_name == NULL)) jc_nullptr("check_singlefile newfile->d_name");
    memcpy(tp, newfile->d_name, newfile->d_name_len + 1);
    tp = basename(tp);
    if (tp[0] == '.' && jc_streq(tp, ".") && jc_streq(tp, "..")) {
      LOUD(fprintf(stderr, "check_singlefile: excluding hidden file (-A on)\n"));
      return 1;
    }
  }

  /* Get file information and check for validity */
  const int i = getfilestats(newfile);

  if (i != 0 || newfile->size == -1) {
    LOUD(fprintf(stderr, "check_singlefile: excluding due to bad stat()\n"));
    return 1;
  }

  if (!JC_S_ISREG(newfile->mode) && !JC_S_ISDIR(newfile->mode)) {
    LOUD(fprintf(stderr, "check_singlefile: excluding non-regular file\n"));
    return 1;
  }

  if (!JC_S_ISDIR(newfile->mode)) {
    /* Exclude zero-length files if requested */
    if (newfile->size == 0 && !ISFLAG(flags, F_INCLUDEEMPTY)) {
    LOUD(fprintf(stderr, "check_singlefile: excluding zero-length empty file (-z not set)\n"));
    return 1;
  }

#ifndef NO_EXTFILTER
    if (extfilter_exclude(newfile)) {
      LOUD(fprintf(stderr, "check_singlefile: excluding based on an extfilter option\n"));
      return 1;
    }
#endif /* NO_EXTFILTER */
  }

#ifdef ON_WINDOWS
  /* Windows has a 1023 (+1) hard link limit. If we're hard linking,
   * ignore all files that have hit this limit */
 #ifndef NO_HARDLINKS
  if (ISFLAG(a_flags, FA_HARDLINKFILES) && newfile->nlink >= 1024) {
  #ifdef DEBUG
    hll_exclude++;
  #endif
    LOUD(fprintf(stderr, "check_singlefile: excluding due to Windows 1024 hard link limit\n"));
    return 1;
  }
 #endif /* NO_HARDLINKS */
#endif /* ON_WINDOWS */
  LOUD(fprintf(stderr, "check_singlefile: all checks passed\n"));
  return 0;
}
