/* Hard link or symlink files
 * This file is part of jdupes; see jdupes.c for license information */

#include "jdupes.h"

/* Compile out the code if no linking support is built in */
#if !(defined NO_HARDLINKS && defined NO_SYMLINKS)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "act_linkfiles.h"
#include "jody_win_unicode.h"
#ifdef ON_WINDOWS
 #include "win_stat.h"
#endif

extern void linkfiles(file_t *files, const int hard)
{
  static file_t *tmpfile;
  static file_t *srcfile;
  static file_t *curfile;
  static file_t ** restrict dupelist;
  static unsigned int counter;
  static unsigned int max = 0;
  static unsigned int x = 0;
  static size_t name_len = 0;
  static int i, success;
#ifndef NO_SYMLINKS
  static unsigned int symsrc;
  static char rel_path[PATHBUF_SIZE];
#endif
  static char temp_path[PATHBUF_SIZE];

  LOUD(fprintf(stderr, "Running linkfiles(%d)\n", hard);)
  curfile = files;

  while (curfile) {
    if (ISFLAG(curfile->flags, F_HAS_DUPES)) {
      counter = 1;
      tmpfile = curfile->duplicates;
      while (tmpfile) {
       counter++;
       tmpfile = tmpfile->duplicates;
      }

      if (counter > max) max = counter;
    }

    curfile = curfile->next;
  }

  max++;

  dupelist = (file_t**) malloc(sizeof(file_t*) * max);

  if (!dupelist) oom("linkfiles() dupelist");

  while (files) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      counter = 1;
      dupelist[counter] = files;

      tmpfile = files->duplicates;

      while (tmpfile) {
       counter++;
       dupelist[counter] = tmpfile;
       tmpfile = tmpfile->duplicates;
      }

      /* Link every file to the first file */

      if (hard) {
#ifndef NO_HARDLINKS
        x = 2;
        srcfile = dupelist[1];
#else
        fprintf(stderr, "internal error: linkfiles(hard) called without hard link support\nPlease report this to the author as a program bug\n");
        exit(EXIT_FAILURE);
#endif
      } else {
#ifndef NO_SYMLINKS
        x = 1;
        /* Symlinks should target a normal file if one exists */
        srcfile = NULL;
        for (symsrc = 1; symsrc <= counter; symsrc++) {
          if (!ISFLAG(dupelist[symsrc]->flags, F_IS_SYMLINK)) {
            srcfile = dupelist[symsrc];
            break;
          }
        }
        /* If no normal file exists, abort */
        if (srcfile == NULL) continue;
#else
        fprintf(stderr, "internal error: linkfiles(soft) called without symlink support\nPlease report this to the author as a program bug\n");
        exit(EXIT_FAILURE);
#endif
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        printf("[SRC] "); fwprint(stdout, srcfile->d_name, 1);
      }
      for (; x <= counter; x++) {
        if (hard == 1) {
          /* Can't hard link files on different devices */
          if (srcfile->device != dupelist[x]->device) {
            fprintf(stderr, "warning: hard link target on different device, not linking:\n-//-> ");
            fwprint(stderr, dupelist[x]->d_name, 1);
            continue;
          } else {
            /* The devices for the files are the same, but we still need to skip
             * anything that is already hard linked (-L and -H both set) */
            if (srcfile->inode == dupelist[x]->inode) {
              /* Don't show == arrows when not matching against other hard links */
              if (ISFLAG(flags, F_CONSIDERHARDLINKS))
                if (!ISFLAG(flags, F_HIDEPROGRESS)) {
                  printf("-==-> "); fwprint(stdout, dupelist[x]->d_name, 1);
                }
            continue;
            }
          }
        } else {
          /* Symlink prerequisite check code can go here */
          /* Do not attempt to symlink a file to itself or to another symlink */
#ifndef NO_SYMLINKS
          if (ISFLAG(dupelist[x]->flags, F_IS_SYMLINK) &&
              ISFLAG(dupelist[symsrc]->flags, F_IS_SYMLINK)) continue;
          if (x == symsrc) continue;
#endif
        }
#ifdef UNICODE
        if (!M2W(dupelist[x]->d_name, wname)) {
          fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
#endif /* UNICODE */

        /* Do not attempt to hard link files for which we don't have write access */
#ifdef ON_WINDOWS
        if (dupelist[x]->mode & FILE_ATTRIBUTE_READONLY)
#else
        if (access(dupelist[x]->d_name, W_OK) != 0)
#endif
        {
          fprintf(stderr, "warning: link target is a read-only file, not linking:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
        /* Check file pairs for modification before linking */
        /* Safe linking: don't actually delete until the link succeeds */
        i = file_has_changed(srcfile);
        if (i) {
          fprintf(stderr, "warning: source file modified since scanned; changing source file:\n[SRC] ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          LOUD(fprintf(stderr, "file_has_changed: %d\n", i);)
          srcfile = dupelist[x];
          continue;
        }
        if (file_has_changed(dupelist[x])) {
          fprintf(stderr, "warning: target file modified since scanned, not linking:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
#ifdef ON_WINDOWS
        /* For Windows, the hard link count maximum is 1023 (+1); work around
         * by skipping linking or changing the link source file as needed */
        if (win_stat(srcfile->d_name, &ws) != 0) {
          fprintf(stderr, "warning: win_stat() on source file failed, changing source file:\n[SRC] ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          srcfile = dupelist[x];
          continue;
        }
        if (ws.nlink >= 1024) {
          fprintf(stderr, "warning: maximum source link count reached, changing source file:\n[SRC] ");
          srcfile = dupelist[x];
          continue;
        }
        if (win_stat(dupelist[x]->d_name, &ws) != 0) continue;
        if (ws.nlink >= 1024) {
          fprintf(stderr, "warning: maximum destination link count reached, skipping:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          continue;
        }
#endif

        /* Make sure the name will fit in the buffer before trying */
        name_len = strlen(dupelist[x]->d_name) + 14;
        if (name_len > PATHBUF_SIZE) continue;
        /* Assemble a temporary file name */
        strcpy(temp_path, dupelist[x]->d_name);
        strcat(temp_path, ".__jdupes__.tmp");
        /* Rename the source file to the temporary name */
#ifdef UNICODE
        if (!M2W(temp_path, wname2)) {
          fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, srcfile->d_name, 1);
          continue;
        }
        i = MoveFileW(wname, wname2) ? 0 : 1;
#else
        i = rename(dupelist[x]->d_name, temp_path);
#endif
        if (i != 0) {
          fprintf(stderr, "warning: cannot move link target to a temporary name, not linking:\n-//-> ");
          fwprint(stderr, dupelist[x]->d_name, 1);
          /* Just in case the rename succeeded yet still returned an error, roll back the rename */
#ifdef UNICODE
          MoveFileW(wname2, wname);
#else
          rename(temp_path, dupelist[x]->d_name);
#endif
          continue;
        }

        /* Create the desired hard link with the original file's name */
        errno = 0;
        success = 0;
#ifdef ON_WINDOWS
 #ifdef UNICODE
        if (!M2W(srcfile->d_name, wname2)) {
          fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, srcfile->d_name, 1);
          continue;
        }
        if (CreateHardLinkW((LPCWSTR)wname, (LPCWSTR)wname2, NULL) == TRUE) success = 1;
 #else
        if (CreateHardLink(dupelist[x]->d_name, srcfile->d_name, NULL) == TRUE) success = 1;
 #endif
#else
        if (hard) {
          if (link(srcfile->d_name, dupelist[x]->d_name) == 0) success = 1;
 #ifdef NO_SYMLINKS
        }
 #else
        } else {
          i = make_relative_link_name(srcfile->d_name, dupelist[x]->d_name, rel_path);
          LOUD(fprintf(stderr, "symlink GRN: %s to %s = %s\n", srcfile->d_name, dupelist[x]->d_name, rel_path));
          if (i < 0) {
            fprintf(stderr, "warning: make_relative_link_name() failed (%d)\n", i);
          } else if (i == 1) {
            fprintf(stderr, "warning: files to be linked have the same canonical path; not linking\n");
          } else if (symlink(rel_path, dupelist[x]->d_name) == 0) success = 1;
        }
 #endif /* NO_SYMLINKS */
#endif /* ON_WINDOWS */
        if (success) {
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            printf("%s ", hard ? "---->" : "-@@->");
            fwprint(stdout, dupelist[x]->d_name, 1);
          }
        } else {
          /* The link failed. Warn the user and put the link target back */
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            printf("-//-> "); fwprint(stdout, dupelist[x]->d_name, 1);
          }
          fprintf(stderr, "warning: unable to link '"); fwprint(stderr, dupelist[x]->d_name, 0);
          fprintf(stderr, "' -> '"); fwprint(stderr, srcfile->d_name, 0);
          fprintf(stderr, "': %s\n", strerror(errno));
#ifdef UNICODE
          if (!M2W(temp_path, wname2)) {
            fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, temp_path, 1);
            continue;
          }
          i = MoveFileW(wname2, wname) ? 0 : 1;
#else
          i = rename(temp_path, dupelist[x]->d_name);
#endif
          if (i != 0) {
            fprintf(stderr, "error: cannot rename temp file back to original\n");
            fprintf(stderr, "original: "); fwprint(stderr, dupelist[x]->d_name, 1);
            fprintf(stderr, "current:  "); fwprint(stderr, temp_path, 1);
          }
          continue;
        }

        /* Remove temporary file to clean up; if we can't, reverse the linking */
#ifdef UNICODE
          if (!M2W(temp_path, wname2)) {
            fprintf(stderr, "error: MultiByteToWideChar failed: "); fwprint(stderr, temp_path, 1);
            continue;
          }
        i = DeleteFileW(wname2) ? 0 : 1;
#else
        i = remove(temp_path);
#endif
        if (i != 0) {
          /* If the temp file can't be deleted, there may be a permissions problem
           * so reverse the process and warn the user */
          fprintf(stderr, "\nwarning: can't delete temp file, reverting: ");
          fwprint(stderr, temp_path, 1);
#ifdef UNICODE
          i = DeleteFileW(wname) ? 0 : 1;
#else
          i = remove(dupelist[x]->d_name);
#endif
          /* This last error really should not happen, but we can't assume it won't */
          if (i != 0) fprintf(stderr, "\nwarning: couldn't remove link to restore original file\n");
          else {
#ifdef UNICODE
            i = MoveFileW(wname2, wname) ? 0 : 1;
#else
            i = rename(temp_path, dupelist[x]->d_name);
#endif
            if (i != 0) {
              fprintf(stderr, "\nwarning: couldn't revert the file to its original name\n");
              fprintf(stderr, "original: "); fwprint(stderr, dupelist[x]->d_name, 1);
              fprintf(stderr, "current:  "); fwprint(stderr, temp_path, 1);
            }
          }
        }
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("\n");
    }
    files = files->next;
  }

  free(dupelist);
  return;
}
#endif /* NO_HARDLINKS */
