/* Delete duplicate files automatically or interactively
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include "jdupes.h"
#include "jody_win_unicode.h"
#include "act_deletefiles.h"

extern void deletefiles(file_t *files, int prompt, FILE *tty)
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

  if (!files) return;

  groups = get_max_dupes(files, &max, NULL);

  max++;

  dupelist = (file_t **) malloc(sizeof(file_t*) * max);
  preserve = (unsigned int *) malloc(sizeof(int) * max);
  preservestr = (char *) malloc(INPUT_SIZE);

  if (!dupelist || !preserve || !preservestr) oom("deletefiles() structures");

  for (; files; files = files->next) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      curgroup++;
      counter = 1;
      dupelist[counter] = files;

      if (prompt) {
        printf("[%u] ", counter); fwprint(stdout, files->d_name, 1);
      }

      tmpfile = files->duplicates;

      while (tmpfile) {
        dupelist[++counter] = tmpfile;
        if (prompt) {
          printf("[%u] ", counter); fwprint(stdout, tmpfile->d_name, 1);
        }
        tmpfile = tmpfile->duplicates;
      }

      if (prompt) printf("\n");

      /* preserve only the first file */
      if (!prompt) {
        preserve[1] = 1;
        for (x = 2; x <= counter; x++) preserve[x] = 0;
      } else do {
        /* prompt for files to preserve */
        printf("Set %u of %u: keep which files? (1 - %u, [a]ll, [n]one)",
          curgroup, groups, counter);
        if (ISFLAG(flags, F_SHOWSIZE)) printf(" (%" PRIuMAX " byte%c each)", (uintmax_t)files->size,
          (files->size != 1) ? 's' : ' ');
        printf(": ");
        fflush(stdout);

        /* treat fgets() failure as if nothing was entered */
        if (!fgets(preservestr, INPUT_SIZE, tty)) preservestr[0] = '\n';

        i = strlen(preservestr) - 1;

        /* tail of buffer must be a newline */
        while (preservestr[i] != '\n') {
          tstr = (char *)realloc(preservestr, strlen(preservestr) + 1 + INPUT_SIZE);
          if (!tstr) oom("deletefiles() prompt string");

          preservestr = tstr;
          if (!fgets(preservestr + i + 1, INPUT_SIZE, tty))
          {
            preservestr[0] = '\n'; /* treat fgets() failure as if nothing was entered */
            break;
          }
          i = strlen(preservestr) - 1;
        }

        for (x = 1; x <= counter; x++) preserve[x] = 0;

        token = strtok(preservestr, " ,\n");
        if (token != NULL && (*token == 'n' || *token == 'N')) goto preserve_none;

        while (token != NULL) {
          if (*token == 'a' || *token == 'A')
            for (x = 0; x <= counter; x++) preserve[x] = 1;

          number = 0;
          sscanf(token, "%u", &number);
          if (number > 0 && number <= counter) preserve[number] = 1;

          token = strtok(NULL, " ,\n");
        }

        for (sum = 0, x = 1; x <= counter; x++) sum += preserve[x];
      } while (sum < 1); /* save at least one file */
preserve_none:

      printf("\n");

      for (x = 1; x <= counter; x++) {
        if (preserve[x]) {
          printf("   [+] "); fwprint(stdout, dupelist[x]->d_name, 1);
        } else {
#ifdef UNICODE
          if (!M2W(dupelist[x]->d_name, wstr)) {
            printf("   [!] "); fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- MultiByteToWideChar failed\n");
            continue;
          }
#endif
          if (file_has_changed(dupelist[x])) {
            printf("   [!] "); fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- file changed since being scanned\n");
#ifdef UNICODE
          } else if (DeleteFileW(wstr) != 0) {
#else
          } else if (remove(dupelist[x]->d_name) == 0) {
#endif
            printf("   [-] "); fwprint(stdout, dupelist[x]->d_name, 1);
          } else {
            printf("   [!] "); fwprint(stdout, dupelist[x]->d_name, 0);
            printf("-- unable to delete file\n");
          }
        }
      }
      printf("\n");
    }
  }
  free(dupelist);
  free(preserve);
  free(preservestr);
  return;
}
