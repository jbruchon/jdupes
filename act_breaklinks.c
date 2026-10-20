/* jdupes action for breaking hard linked file sets
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef NO_HARDLINKS

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <inttypes.h>
#include <libjodycode.h>
#include "jdupes.h"
#include "act_breaklinks.h"
#include "filestat.h"
#include "hashdb.h"
#include "likely_unlikely.h"


static const char * const s_temp_suffix = ".__jdupes__tempcopy";


void breaklinks(file_t * restrict files)
{
#ifndef NO_HASHDB
  file_t * restrict srcfile;
#endif
  file_t * restrict tmpfile;
  int processed = 0;
  static char *buf = NULL;
  size_t path_len;
  FILE *src = NULL;
  FILE *dst = NULL;

  LOUD(fprintf(stderr, "breaklinks: %p\n", files));

  if (unlikely(buf == NULL)) buf = (char *)malloc(auto_chunk_size);
  if (unlikely(buf == NULL)) jc_oom("breaklinks() buffer");

  while (files != NULL) {
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      processed = 1;

#ifndef NO_HASHDB
      srcfile = files;
#endif
      src = jc_fopen(files->d_name, JC_FILE_MODE_RDONLY);
      if (src == NULL) {
        fprintf(stderr, "error: failed to open master file source: ");
        jc_fwprint(stderr, files->d_name, 1);
        exit_status = EXIT_FAILURE;
        files = files->next;
        continue;
      }

      printf("[SRC] ");
      jc_fwprint(stdout, files->d_name, 1);

      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        dst = NULL;
        size_t bytes;
        int read_error = 0;

        path_len = strlen(tmpfile->d_name);
        if (path_len + sizeof(".__jdupes__tempcopy") >= (JC_PATHBUF_SIZE * 2)) {
          fprintf(stderr, "error: path too long for buffer\n");
          goto error_still_linked;
        }

        memcpy(tempname, tmpfile->d_name, path_len);
        memcpy(tempname + path_len, s_temp_suffix, sizeof(".__jdupes__tempcopy"));

        dst = jc_fopen(tempname, JC_FILE_MODE_WRONLY);
        if (dst == NULL) {
          fprintf(stderr, "error: failed to open temp destination\n");
          goto error_still_linked;
        }

        if (fseek(src, 0, SEEK_SET) != 0) {
          fprintf(stderr, "error: failed to seek source file\n");
          goto error_still_linked;
        }

        /* Copy source file to temp destination */
        while ((bytes = fread(buf, 1, auto_chunk_size, src)) > 0) {
          if (fwrite(buf, 1, bytes, dst) != bytes) {
            read_error = 1;
            break;
          }
        }
        if (ferror(src)) read_error = 1;
        if (read_error) {
          fprintf(stderr, "error: read/write error during copy\n");
          goto error_still_linked;
        }

        jc_fclose(dst);
        dst = NULL;

        if (jc_remove(tmpfile->d_name) != 0) {
          fprintf(stderr, "error: error removing original hard link\n");
          goto error_still_linked;
        }

        if (jc_rename(tempname, tmpfile->d_name) != 0) {
          fprintf(stderr, "error: error renaming temp file into place\n");
          goto error_still_linked;
        }

        printf("-/ ~> ");
        jc_fwprint(stdout, tmpfile->d_name, 1);

#ifndef NO_HASHDB
	/* If using a hashdb, create a hash copy entry for the new file */
	if (ISFLAG(flags, F_HASHDB)) {
          LOUD(fprintf(stderr, "breaklinks add to hashdb: '%s' => '%s'\n", srcfile->d_name, tmpfile->d_name));
          uint64_t hashflags = srcfile->flags & (FF_HASH_PARTIAL | FF_HASH_FULL);
          if (hashflags != 0) {
	    CLEARFLAG(tmpfile->flags, FF_VALID_STAT);
            getfilestats(tmpfile);
            SETFLAG(tmpfile->flags, hashflags);
            tmpfile->filehash_partial = srcfile->filehash_partial;
            tmpfile->filehash = srcfile->filehash;
            add_hashdb_entry(NULL, 0, tmpfile, 1);
	  }
	}
#endif

        tmpfile = tmpfile->duplicates;
        continue;

error_still_linked:
        if (dst != NULL) {
          jc_fclose(dst);
          dst = NULL;
        }
        jc_remove(tempname);
        printf("-==-> ");
        jc_fwprint(stdout, tmpfile->d_name, 1);
        exit_status = EXIT_FAILURE;
        tmpfile = tmpfile->duplicates;
        continue;
      }
      jc_fclose(src);
      src = NULL;
      printf("\n");
    }
    files = files->next;
  }

  if (processed == 0) printf("No hard-linked file sets found.\n");

  return;
}

#endif /* NO_HARDLINKS */
