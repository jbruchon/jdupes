/* Delete duplicate files automatically or interactively
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef NO_DELETE

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include <libjodycode.h>
#include "jdupes.h"
#include "likely_unlikely.h"
#include "act_deletefiles.h"
#include "act_linkfiles.h"
#ifndef NO_HASHDB
 #include "hashdb.h"
#endif

/* For interactive deletion input */
#define INPUT_SIZE 1024


/* Count the following statistics:
   - Maximum number of files in a duplicate set (length of longest dupe chain)
   - Total number of duplicate file sets (groups) */
static unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max)
{
  unsigned int groups = 0;

  if (unlikely(files == NULL || max == NULL)) jc_nullptr("get_max_dupes()");
  LOUD(fprintf(stderr, "get_max_dupes(%p, %p)\n", (const void *)files, (void *)max);)

  *max = 0;

  while (files) {
    unsigned int n_dupes;
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      groups++;
      n_dupes = 1;
      for (file_t *curdupe = files->duplicates; curdupe; curdupe = curdupe->duplicates) n_dupes++;
      if (n_dupes > *max) *max = n_dupes;
    }
    files = files->next;
  }
  return groups;
}


void deletefiles(file_t *files, int prompt, FILE *tty)
{
  unsigned int counter, groups;
  unsigned int curgroup = 0;
  file_t *tmpfile;
  file_t **dupelist;
  unsigned int *preserve;
  char *preservestr;
  char *token;
  char *tstr;
  unsigned int number, sum, max, x;
  size_t i;

  LOUD(fprintf(stderr, "deletefiles: %p, %d, %p\n", files, prompt, tty));

  groups = get_max_dupes(files, &max);

  max++;

  dupelist = (file_t **) malloc(sizeof(file_t*) * max);
  preserve = (unsigned int *) malloc(sizeof(int) * max);
  preservestr = (char *) malloc(INPUT_SIZE);

  if (!dupelist || !preserve || !preservestr) jc_oom("deletefiles() structures");

  for (; files; files = files->next) {
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      curgroup++;
      counter = 1;
      dupelist[counter] = files;

      if (prompt) {
        printf("[%u] ", counter); jc_fwprint(stdout, files->d_name, 1);
      }

      tmpfile = files->duplicates;

      while (tmpfile) {
        dupelist[++counter] = tmpfile;
        if (prompt) {
          printf("[%u] ", counter); jc_fwprint(stdout, tmpfile->d_name, 1);
        }
        tmpfile = tmpfile->duplicates;
      }

      if (prompt) printf("\n");

      /* Preserve only the first file */
      if (!prompt) {
        preserve[1] = 1;
        for (x = 2; x <= counter; x++) preserve[x] = 0;
      } else do {
        /* Prompt for files to preserve */
        printf("Specify multiple files with commas like this: 1,2,4,6\n");
        printf("Set %u of %u: keep which files? (1 - %u, [a]ll, [n]one", curgroup, groups, counter);
#ifndef NO_HARDLINKS
       printf(", [l]ink all");
#endif
#ifndef NO_SYMLINKS
       printf(", [s]ymlink all");
#endif
       printf(")");
        if (ISFLAG(a_flags, FA_SHOWSIZE)) printf(" (%" PRIuMAX " byte%c each)", (uintmax_t)files->size,
          (files->size != 1) ? 's' : ' ');
        printf(": ");
        fflush(stdout);

        /* Treat fgets() failure as if nothing was entered */
        if (!fgets(preservestr, INPUT_SIZE, tty)) preservestr[0] = '\n';

        /* If nothing is entered, treat it as if 'a' was entered */
        if (preservestr[0] == '\n') strcpy(preservestr, "a\n");

        i = strlen(preservestr) - 1;

        /* tail of buffer must be a newline */
        while (preservestr[i] != '\n') {
          tstr = (char *)realloc(preservestr, strlen(preservestr) + 1 + INPUT_SIZE);
          if (!tstr) jc_oom("deletefiles() prompt");

          preservestr = tstr;
          if (!fgets(preservestr + i + 1, INPUT_SIZE, tty))
          {
            preservestr[0] = '\n'; /* treat fgets() failure as if nothing was entered */
            break;
          }
          i = strlen(preservestr) - 1;
        }

        for (x = 1; x <= counter; x++) preserve[x] = 0;

        /* Catch attempts to use invalid characters and block them */
        for (char *pscheck = preservestr; *pscheck != '\0'; pscheck++) {
          switch (*pscheck) {
            case ',':
            case ' ':
            case 'a':
            case 'A':
            case 's':
            case 'S':
            case 'l':
            case 'L':
            case 'n':
            case 'N':
            case '\n':
            case '\0':
              continue;
            default:
              break;
          }
          if (*pscheck >= '0' && *pscheck <= '9') continue;
          if (*pscheck == '-') {
            fprintf(stderr, "error: number ranges are not yet supported; taking no action\n");
            goto skip_deletion;
          }
          fprintf(stderr, "error: invalid character '%c' in preserve answer; taking no action\n", *pscheck);
          goto skip_deletion;
        }
        token = strtok(preservestr, " ,\n");
        if (token != NULL) {
#if defined NO_HARDLINKS && defined NO_SYMLINKS
          /* no linktype needed */
#else
          int linktype = -1;
#endif /* defined NO_HARDLINKS && defined NO_SYMLINKS */
          /* "Delete none" = stop parsing string */
          if (*token == 'n' || *token == 'N') goto stop_scanning;
          /* If requested, link this set instead */
#ifndef NO_HARDLINKS
          if (*token == 'l' || *token == 'L') linktype = 1; /* hard link */
#endif
#ifndef NO_SYMLINKS
          if (*token == 's' || *token == 'S') linktype = 0; /* symlink */
#endif
#if defined NO_HARDLINKS && defined NO_SYMLINKS
          /* no linking calls */
#else
          if (linktype != -1) {
            linkfiles(files, linktype, 1);
            goto skip_deletion;
          }
#endif /* defined NO_HARDLINKS && defined NO_SYMLINKS */
        }

        while (token != NULL) {
          if (*token == 'a' || *token == 'A')
            for (x = 0; x <= counter; x++) preserve[x] = 1;

          number = 0;
          sscanf(token, "%u", &number);
          if (number > 0 && number <= counter) preserve[number] = 1;
          else {
            fprintf(stderr, "invalid number '%u' in preserve answer; taking no action\n", number);
            goto skip_deletion;
          }

          token = strtok(NULL, " ,\n");
        }

        for (sum = 0, x = 1; x <= counter; x++) sum += preserve[x];
      } while (sum < 1); /* save at least one file */
stop_scanning:

      printf("\n");

      for (x = 1; x <= counter; x++) {
        if (preserve[x]) {
          printf("   [+] "); jc_fwprint(stdout, dupelist[x]->d_name, 1);
        } else {
          if (file_has_changed(dupelist[x])) {
            printf("   [!] "); jc_fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- file changed since being scanned\n");
            exit_status = EXIT_FAILURE;
          } else if (jc_remove(dupelist[x]->d_name) == 0) {
            printf("   [-] "); jc_fwprint(stdout, dupelist[x]->d_name, 1);
#ifndef NO_HASHDB
            /* Erase hash database entry for the deleted file */
            if (ISFLAG(flags, F_HASHDB)) {
              dupelist[x]->mtime = -1;
              add_hashdb_entry(NULL, 0, dupelist[x], 0, 0);
          }
#endif
          } else {
            printf("   [!] "); jc_fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- unable to delete file\n");
            exit_status = EXIT_FAILURE;
          }
        }
      }
#if defined NO_HARDLINKS && defined NO_SYMLINKS
      /* label not needed */
#else
#endif /* defined NO_HARDLINKS && defined NO_SYMLINKS */
skip_deletion:
      printf("\n");
    }
  }
  free(dupelist);
  free(preserve);
  free(preservestr);
  return;
}

#endif /* NO_DELETE */
