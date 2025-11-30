/* Hard link or symlink files
 * This file is part of jdupes; see jdupes.c for license information */

#include "jdupes.h"

/* Compile out the code if no linking support is built in */
#if !(defined NO_HARDLINKS && !defined NO_SYMLINKS && !defined ENABLE_DEDUPE)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <libjodycode.h>
#include "act_linkfiles.h"
#ifndef NO_HASHDB
 #include "hashdb.h"
#endif

/* Apple clonefile() is basically a hard link */
#ifdef ENABLE_DEDUPE
 #ifdef __APPLE__
  #ifdef NO_HARDLINKS
   #error Hard link support is required for dedupe on macOS
  #endif
  #include <sys/attr.h>
  #include <copyfile.h>
  #ifndef NO_CLONEFILE
   #include <sys/clonefile.h>
   #define ENABLE_CLONEFILE_LINK 1
  #endif /* NO_CLONEFILE */
 #endif /* __APPLE__ */
#endif /* ENABLE_DEDUPE */


#ifdef ENABLE_CLONEFILE_LINK
static void clonefile_error(const char * const restrict func, const char * const restrict name)
{
  fprintf(stderr, "error: %s failed for destination file, reverting:\n-##-> ", func);
  jc_fwprint(stderr, name, 1);
  exit_status = EXIT_FAILURE;
  return;
}
#endif /* ENABLE_CLONEFILE_LINK */


/* Only build this function if some functionality does not exist */
#if defined NO_SYMLINKS || defined NO_HARDLINKS || !defined ENABLE_CLONEFILE_LINK
static void linkfiles_nosupport(const char * const restrict call, const char * const restrict type)
{
  fprintf(stderr, "internal error: linkfiles(%s) called without %s support\nPlease report this to the author as a program bug\n", call, type);
  exit(EXIT_FAILURE);
}
#endif /* anything unsupported */


static void revert_failed(const char * const restrict orig, const char * const restrict current)
{
  fprintf(stderr, "\nerror: couldn't revert the file to its original name\n");
  fprintf(stderr, "original: "); jc_fwprint(stderr, orig, 1);
  fprintf(stderr, "current:  "); jc_fwprint(stderr, current, 1);
  exit_status = EXIT_FAILURE;
  return;
}


/* linktype: 0=symlink, 1=hardlink, 2=clonefile() */
void linkfiles(file_t *files, const int linktype, const int only_current)
{
  static file_t *tmpfile;
  static file_t *srcfile;
  static file_t *curfile;
  static file_t ** restrict dupelist;
  static unsigned int counter = 0;
  static unsigned int max = 0;
  static unsigned int x = 0;
  static size_t name_len = 0;
  static int i, success;
#ifndef NO_SYMLINKS
  static unsigned int symsrc;
  static char rel_path[JC_PATHBUF_SIZE];
#endif
#if defined ON_WINDOWS || defined ENABLE_CLONEFILE_LINK
  static struct JC_STAT s;
#endif
#ifdef ENABLE_CLONEFILE_LINK
  static unsigned int srcfile_preserved_flags = 0;
  static unsigned int dupfile_preserved_flags = 0;
  static unsigned int dupfile_original_flags = 0;
  static struct timeval dupfile_original_tval[2];
#endif

  LOUD(fprintf(stderr, "linkfiles(%d): %p\n", linktype, files);)
  curfile = files;

  /* Calculate a maximum */
  while (curfile) {
    if (ISFLAG(curfile->flags, FF_HAS_DUPES)) {
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

  if (!dupelist) jc_oom("linkfiles() dupelist");

  while (files) {
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
      counter = 1;
      dupelist[counter] = files;

      tmpfile = files->duplicates;

      while (tmpfile) {
       counter++;
       dupelist[counter] = tmpfile;
       tmpfile = tmpfile->duplicates;
      }

      /* Link every file to the first file */

      if (linktype != 0) {
#ifndef NO_HARDLINKS
        x = 2;
        srcfile = dupelist[1];
#else
        linkfiles_nosupport("hard", "hard link");
#endif
      } else {
#ifndef NO_SYMLINKS
        x = 1;
        /* Symlinks should target a normal file if one exists */
        srcfile = NULL;
        for (symsrc = 1; symsrc <= counter; symsrc++) {
          if (!ISFLAG(dupelist[symsrc]->flags, FF_IS_SYMLINK)) {
            srcfile = dupelist[symsrc];
            break;
          }
        }
        /* If no normal file exists, abort */
        if (srcfile == NULL) goto linkfile_loop;
#else
        linkfiles_nosupport("soft", "symlink");
#endif
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        printf("[SRC] "); jc_fwprint(stdout, srcfile->d_name, 1);
      }
      if (linktype == 2) {
#ifdef ENABLE_CLONEFILE_LINK
        if (jc_stat(srcfile->d_name, &s) != 0) {
          fprintf(stderr, "error: stat() on source file failed, skipping:\n[SRC] ");
          jc_fwprint(stderr, srcfile->d_name, 1);
          exit_status = EXIT_FAILURE;
          goto linkfile_loop;
        }

        /* macOS unexpectedly copies the compressed flag when copying metadata
         * (which can result in files being unreadable), so we want to retain
         * the compression flag of srcfile */
        srcfile_preserved_flags = s.st_flags & UF_COMPRESSED;
#else
        linkfiles_nosupport("clone", "clonefile");
#endif
      }
      for (; x <= counter; x++) {
        if (linktype == 1 || linktype == 2) {
          /* Can't hard link files on different devices */
          if (srcfile->device != dupelist[x]->device) {
            fprintf(stderr, "error: hard link target on different device, not linking:\n-//-> ");
            jc_fwprint(stderr, dupelist[x]->d_name, 1);
            exit_status = EXIT_FAILURE;
            continue;
          } else {
            /* The devices for the files are the same, but we still need to skip
             * anything that is already hard linked (-L and -H both set) */
            if (srcfile->inode == dupelist[x]->inode) {
              /* Don't show == arrows when not matching against other hard links */
              if (ISFLAG(flags, F_CONSIDERHARDLINKS))
                if (!ISFLAG(flags, F_HIDEPROGRESS)) {
                  printf("-==-> "); jc_fwprint(stdout, dupelist[x]->d_name, 1);
                }
              continue;
            }
          }
        } else {
          /* Symlink prerequisite check code can go here */
          /* Do not attempt to symlink a file to itself or to another symlink */
#ifndef NO_SYMLINKS
          if (ISFLAG(dupelist[x]->flags, FF_IS_SYMLINK) &&
              ISFLAG(dupelist[symsrc]->flags, FF_IS_SYMLINK)) continue;
          if (x == symsrc) continue;
#endif
        }

        /* Do not attempt to hard link files for which we don't have write access */
        if (
#ifdef ON_WINDOWS
        !JC_S_ISRO(dupelist[x]->mode) &&
#endif
        (jc_access(dupelist[x]->d_name, JC_W_OK) != 0))
        {
          fprintf(stderr, "error: link target is a read-only file, not linking:\n-//-> ");
          jc_fwprint(stderr, dupelist[x]->d_name, 1);
          exit_status = EXIT_FAILURE;
          continue;
        }
        /* Check file pairs for modification before linking */
        /* Safe linking: don't actually delete until the link succeeds */
        i = file_has_changed(srcfile);
        if (i) {
          fprintf(stderr, "error: source file modified since scanned; changing source file:\n[SRC] ");
          jc_fwprint(stderr, dupelist[x]->d_name, 1);
          LOUD(fprintf(stderr, "file_has_changed: %d\n", i);)
          srcfile = dupelist[x];
          exit_status = EXIT_FAILURE;
          continue;
        }
        if (file_has_changed(dupelist[x])) {
          fprintf(stderr, "error: target file modified since scanned, not linking:\n-//-> ");
          jc_fwprint(stderr, dupelist[x]->d_name, 1);
          exit_status = EXIT_FAILURE;
          continue;
        }
#ifdef ON_WINDOWS
        /* For Windows, the hard link count maximum is 1023 (+1); work around
         * by skipping linking or changing the link source file as needed */
        if (jc_stat(srcfile->d_name, &s) != 0) {
          fprintf(stderr, "error: win_stat() on source file failed, changing source file:\n[SRC] ");
          jc_fwprint(stderr, dupelist[x]->d_name, 1);
          srcfile = dupelist[x];
          exit_status = EXIT_FAILURE;
          continue;
        }
        if (s.st_nlink >= 1024) {
          fprintf(stderr, "error: maximum source link count reached, changing source file:\n[SRC] ");
          srcfile = dupelist[x];
          exit_status = EXIT_FAILURE;
          continue;
        }
        if (jc_stat(dupelist[x]->d_name, &s) != 0) continue;
        if (s.st_nlink >= 1024) {
          fprintf(stderr, "error: maximum destination link count reached, skipping:\n-//-> ");
          jc_fwprint(stderr, dupelist[x]->d_name, 1);
          exit_status = EXIT_FAILURE;
          continue;
        }
#endif
#ifdef ENABLE_CLONEFILE_LINK
        if (linktype == 2) {
          if (jc_stat(dupelist[x]->d_name, &s) != 0) {
            fprintf(stderr, "error: stat() on destination file failed, skipping:\n-##-> ");
            jc_fwprint(stderr, dupelist[x]->d_name, 1);
            exit_status = EXIT_FAILURE;
            continue;
          }

          /* macOS unexpectedly copies the compressed flag when copying metadata
           * (which can result in files being unreadable), so we want to ignore
           * the compression flag on dstfile in favor of the one from srcfile */
          dupfile_preserved_flags = s.st_flags & ~(unsigned int)UF_COMPRESSED;
          dupfile_original_flags = s.st_flags;
          dupfile_original_tval[0].tv_sec = s.st_atime;
          dupfile_original_tval[1].tv_sec = s.st_mtime;
          dupfile_original_tval[0].tv_usec = 0;
          dupfile_original_tval[1].tv_usec = 0;
        }
#endif

        /* Make sure the name will fit in the buffer before trying */
        name_len = strlen(dupelist[x]->d_name) + 14;
        if (name_len > JC_PATHBUF_SIZE) continue;
        /* Assemble a temporary file name */
        strcpy(tempname, dupelist[x]->d_name);
        strcat(tempname, ".__jdupes__.tmp");
        /* Rename the destination file to the temporary name */
        i = jc_rename(dupelist[x]->d_name, tempname);
        if (i != 0) {
          fprintf(stderr, "error: cannot move link target to a temporary name, not linking:\n-//-> ");
          jc_fwprint(stderr, dupelist[x]->d_name, 1);
          exit_status = EXIT_FAILURE;
          /* Just in case the rename succeeded yet still returned an error, roll back the rename */
          jc_rename(tempname, dupelist[x]->d_name);
          continue;
        }

        /* Create the desired hard link with the original file's name */
        errno = 0;
        success = 0;
        if (linktype == 1) {
          if (jc_link(srcfile->d_name, dupelist[x]->d_name) == 0) success = 1;
#ifdef ENABLE_CLONEFILE_LINK
        } else if (linktype == 2) {
          if (clonefile(srcfile->d_name, dupelist[x]->d_name, 0) == 0) {
            if (copyfile(tempname, dupelist[x]->d_name, NULL, COPYFILE_METADATA) == 0) {
              /* If the preserved flags match what we just copied from the original dupfile, we're done.
               * Otherwise, we need to update the flags to avoid data loss due to differing compression flags */
              if (dupfile_original_flags == (srcfile_preserved_flags | dupfile_preserved_flags)) {
                success = 1;
              } else if (chflags(dupelist[x]->d_name, srcfile_preserved_flags | dupfile_preserved_flags) == 0) {
                /* chflags overrides the timestamps that were restored by copyfile, so we need to reapply those as well */
                if (utimes(dupelist[x]->d_name, dupfile_original_tval) == 0) {
                  success = 1;
                } else clonefile_error("utimes", dupelist[x]->d_name);
              } else clonefile_error("chflags", dupelist[x]->d_name);
            } else clonefile_error("copyfile", dupelist[x]->d_name);
          } else clonefile_error("clonefile", dupelist[x]->d_name);
#endif /* ENABLE_CLONEFILE_LINK */
        }
#ifndef NO_SYMLINKS
        else {
          i = jc_make_relative_link_name(srcfile->d_name, dupelist[x]->d_name, rel_path);
          LOUD(fprintf(stderr, "symlink MRLN: %s to %s = %s\n", srcfile->d_name, dupelist[x]->d_name, rel_path));
          if (i < 0) {
            fprintf(stderr, "error: make_relative_link_name() failed (%d)\n", i);
	    exit_status = EXIT_FAILURE;
          } else if (i == 1) {
            fprintf(stderr, "error: files to be linked have the same canonical path; not linking\n");
	    exit_status = EXIT_FAILURE;
          } else if (symlink(rel_path, dupelist[x]->d_name) == 0) success = 1;
        }
#endif /* NO_SYMLINKS */
        if (success) {
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            switch (linktype) {
              case 0: /* symlink */
                printf("-@@-> ");
                break;
              default:
              case 1: /* hardlink */
                printf("----> ");
                break;
#ifdef ENABLE_CLONEFILE_LINK
              case 2: /* clonefile */
                printf("-##-> ");
                break;
#endif
            }
            jc_fwprint(stdout, dupelist[x]->d_name, 1);
          }
#ifndef NO_HASHDB
          if (ISFLAG(flags, F_HASHDB)) {
            /* Delete hashdb entry for symlinks - the original file is gone */
            switch (linktype) {
              case 0:
                dupelist[x]->mtime = -1;
                add_hashdb_entry(NULL, 0, dupelist[x], 0);
                break;
              case 1:
		/* Copy hard link source info to link target and update hashdb */
                dupelist[x]->filehash_partial = srcfile->filehash_partial;
                dupelist[x]->filehash = srcfile->filehash;
                dupelist[x]->mtime = srcfile->mtime;
                dupelist[x]->inode = srcfile->inode;
                dupelist[x]->flags |= FF_HASHDB_DIRTY | (srcfile->flags & (FF_HASH_PARTIAL | FF_HASH_FULL));
                add_hashdb_entry(NULL, 0, dupelist[x], 1);
              default:
		break;
            }
          }
#endif
        } else {
          /* The link failed. Warn the user and put the link target back */
          exit_status = EXIT_FAILURE;
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            printf("-//-> "); jc_fwprint(stdout, dupelist[x]->d_name, 1);
          }
          fprintf(stderr, "error: unable to link '"); jc_fwprint(stderr, dupelist[x]->d_name, 0);
          fprintf(stderr, "' -> '"); jc_fwprint(stderr, srcfile->d_name, 0);
          fprintf(stderr, "': %s\n", strerror(errno));
          i = jc_rename(tempname, dupelist[x]->d_name);
          if (i != 0) revert_failed(dupelist[x]->d_name, tempname);
          continue;
        }

        /* Remove temporary file to clean up; if we can't, reverse the linking */
        i = jc_remove(tempname);
        if (i != 0) {
          /* If the temp file can't be deleted, there may be a permissions problem
           * so reverse the process and warn the user */
          fprintf(stderr, "\nerror: can't delete temp file, reverting: ");
          jc_fwprint(stderr, tempname, 1);
          exit_status = EXIT_FAILURE;
          i = jc_remove(dupelist[x]->d_name);
          /* This last error really should not happen, but we can't assume it won't */
          if (i != 0) fprintf(stderr, "\nerror: couldn't remove link to restore original file\n");
          else {
            i = jc_rename(tempname, dupelist[x]->d_name);
            if (i != 0) revert_failed(dupelist[x]->d_name, tempname);
          }
        }
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) printf("\n");
    }
#if !defined NO_SYMLINKS || defined ENABLE_CLONEFILE_LINK
linkfile_loop:
#endif
    if (only_current == 1) break;
    files = files->next;
  }

  if (counter == 0) printf("%s", s_no_dupes);

  free(dupelist);
  return;
}
#endif /* NO_HARDLINKS + NO_SYMLINKS + !ENABLE_DEDUPE */
