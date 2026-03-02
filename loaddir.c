/* jdupes directory scanning code
 * This file is part of jdupes; see jdupes.c for license information */

#include <dirent.h>
#include <inttypes.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>

#include <libjodycode.h>
#include "likely_unlikely.h"
#include "jdupes.h"
#include "checks.h"
#include "filestat.h"
#ifndef NO_HASHDB
 #include "hashdb.h"
#endif
#include "progress.h"
#include "interrupt.h"
#ifndef NO_TRAVCHECK
 #include "travcheck.h"
#endif

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __MINGW32__
 const char dir_sep = '\\';
#else /* Not Windows */
 const char dir_sep = '/';
#endif /* _WIN32 || __MINGW32__ */

/* Directory queue to avoid immediate recursion */
struct dq_entry {
  char *d_name;
  int d_name_len;
  int recurse;
};

struct dq {
  struct dq *next;
  struct dq *prev;
  struct dq_entry entry;
};

static struct dq *dq_head = NULL;
static struct dq *dq_tail = NULL;


char *remove_leading_dotslashes(char *path)
{
  char *origpath;

  origpath = path;
  while (*path == '.') {
    if (*(path + 1) == '/' || *(path + 1) == '\\') {
      path += 2;
      while (*path == '/' || *path == '\\') path++;
    } else break;
    /* A bunch of dot-slashes should just become a dot */
    if (unlikely(*path == '\0')) {
      path = origpath;
      *(path + 1) = '\0';
      break;
    }
  }
  return path;
}


static file_t *init_newfile(const size_t pathlen, file_t * restrict * const restrict filelistp)
{
  file_t * const restrict newfile = (file_t *)calloc(1, sizeof(file_t));

  if (unlikely(!newfile)) jc_oom("init_newfile() file structure");
  if (unlikely(!filelistp)) jc_nullptr("init_newfile() filelistp");

  LOUD(fprintf(stderr, "init_newfile(len %" PRIuMAX ", filelistp %p)\n", (uintmax_t)pathlen, filelistp));

  newfile->d_name = (char *)calloc(1, EXTEND64(pathlen + 1));
  if (!newfile->d_name) jc_oom("init_newfile() filename");

  newfile->d_name_len = pathlen;
  newfile->next = *filelistp;
#ifndef NO_USER_ORDER
  newfile->user_order = user_item_count;
#endif
  newfile->size = -1;
  newfile->duplicates = NULL;
  return newfile;
}


/* Queue for items for sequential directory completion; avoid recursion */
static int push_dirqueue(file_t *newfile, int recurse)
{
  struct dq *dq_item = calloc(1, sizeof(struct dq));

  if (dq_item == NULL) return -1;

  dq_item->entry.d_name = newfile->d_name;
  dq_item->entry.d_name_len = newfile->d_name_len;
  dq_item->entry.recurse = recurse;
  dq_item->next = NULL;
  dq_item->prev = dq_tail;  /* NULL if first item */

  /* Link to end of list - first time, set up queue head/tail */
  if (unlikely(dq_head == NULL)) dq_head = dq_item;
  else dq_tail->next = dq_item;
  dq_tail = dq_item;

  return 0;
}


static int pop_dirqueue(struct dq_entry *found)
{
  struct dq *dq_item = dq_tail;

  if (dq_item == NULL) return -1;

  /* If list is emptied, nuke it, else snip the tail */
  if (dq_item->prev != NULL) dq_item->prev->next = NULL;
  if (dq_item == dq_head) dq_head = NULL;
  dq_tail = dq_item->prev;

  memcpy(found, &(dq_item->entry), sizeof(struct dq_entry));
  free(dq_item);
  return 0;
}


/* This is disabled until a check is in place to make it safe */
#if 0
/* Add a single file to the file tree */
file_t *grokfile(const char * const restrict name, file_t * restrict * const restrict filelistp)
{
  file_t * restrict newfile;

  if (!name || !filelistp) jc_nullptr("grokfile()");
  LOUD(fprintf(stderr, "grokfile: '%s' %p\n", name, filelistp));

  /* Allocate the file_t and the d_name entries */
  newfile = init_newfile(strlen(name) + 2, filelistp);

  strcpy(newfile->d_name, name);

  /* Single-file [l]stat() and exclusion condition check */
  if (check_singlefile(newfile) != 0) {
    LOUD(fprintf(stderr, "grokfile: check_singlefile rejected file\n"));
    free(newfile->d_name);
    free(newfile);
    return NULL;
  }
  return newfile;
}
#endif

/* Load a directory's contents into the file tree, recursing as needed */
void loaddir(char *dir, file_t * restrict * const restrict filelistp, int recurse)
{
  file_t *newfile;
  file_t *rootfile;
  struct dq_entry dq;
  struct JC_DIRENT *dirinfo;
  size_t dirlen, dirpos;
//  int i, single = 0, dotdir = 0;
  int i, dotdir = 0;
  jdupes_ino_t inode;
  dev_t device, n_device;
  jdupes_mode_t mode;
  JC_DIR *cd;
  static int sf_warning = 0; /* single file warning should only appear once */

  if (unlikely(dir == NULL || filelistp == NULL || *dir == '\0')) jc_nullptr("loaddir()");
  LOUD(fprintf(stderr, "loaddir: scanning '%s' (order %d, recurse %d)\n", dir, user_item_count, recurse));

  if (unlikely(interrupt != 0)) return;

  dir = remove_leading_dotslashes(dir);
  if (*dir == '.' && *(dir + 1) == '\0') dotdir = 1;
  /* Convert forward slashes to backslashes if on Windows */
  jc_slash_convert(dir);

  /* Push the first dir on the dir queue so we can use the loop for it */
  i = strlen(dir);
  rootfile = init_newfile(i, filelistp);  /* filelistp is just to pass a null pointer check */
  memcpy(rootfile->d_name, dir, i);
  if (push_dirqueue(rootfile, recurse) != 0) goto error_push_dq;
  free(rootfile);

/* I AM NOT INDENTING FOR THIS! XXX */
while (pop_dirqueue(&dq) == 0) {

  /* Get directory stats (or file stats if it's a file) */
  i = getdirstats(dq.d_name, &inode, &device, &mode);
  if (unlikely(i < 0)) goto error_stat_dir;

  /* if dir is actually a file, just add it to the file tree */
  if (i == 1) {
/* Single file addition is disabled for now because there is no safeguard
 * against the file being compared against itself if it's added in both a
 * recursion and explicitly on the command line. */
#if 0
    LOUD(fprintf(stderr, "loaddir -> grokfile '%s'\n", dir));
    newfile = grokfile(dir, filelistp);
    if (newfile == NULL) {
      LOUD(fprintf(stderr, "grokfile rejected '%s'\n", dir));
      return;
    }
    single = 1;
    goto add_single_file;
#endif
    if (sf_warning == 0) {
      fprintf(stderr, "\nFile specs on command line disabled in this version for safety\n");
      fprintf(stderr, "This should be restored (and safe) in a future release\n");
      fprintf(stderr, "More info at www.jdupes.com or email jody@jodybruchon.com\n");
      sf_warning = 1;
    }
    return; /* Remove when single file is restored */
  }

/* Double traversal prevention tree */
#ifndef NO_TRAVCHECK
  if (likely(!ISFLAG(flags, F_NOTRAVCHECK))) {
    i = traverse_check(device, inode);
    if (unlikely(i == 1)) return;
    if (unlikely(i == 2)) goto error_stat_dir;
  }
#endif /* NO_TRAVCHECK */

  item_progress++;

  cd = jc_opendir(dq.d_name);
  if (unlikely(!cd)) goto error_cd;
  dirlen = dq.d_name_len;

  while ((dirinfo = jc_readdir(cd)) != NULL) {
    char * restrict tp = tempname;
    size_t d_name_len;

    if (unlikely(interrupt != 0)) return;
    LOUD(fprintf(stderr, "loaddir: readdir: '%s'\n", dirinfo->d_name));
    if (unlikely(!jc_streq(dirinfo->d_name, ".") || !jc_streq(dirinfo->d_name, ".."))) continue;
    check_sigusr1();
    if (jc_alarm_ring != 0) {
      jc_alarm_ring = 0;
      update_phase1_progress("dirs");
    }

    /* Assemble the file's full path name, optimized to avoid strcat() */
    d_name_len = jc_get_d_namlen(dirinfo);
    dirpos = 0;
    /* Avoid prefixing '.\' if the dir spec is effectively '.' */
    if (likely(dotdir == 0)) {
      dirpos = dirlen;
      memcpy(tp, dq.d_name, dirpos + 1);
      if (dirpos != 0 && tp[dirpos - 1] != dir_sep) {
        tp[dirpos] = dir_sep;
        dirpos++;
      }
      if (unlikely(dirpos + d_name_len + 1 >= (JC_PATHBUF_SIZE * 2))) goto error_overflow;
      tp += dirpos;
    }
    memcpy(tp, dirinfo->d_name, d_name_len);
    tp += d_name_len;
    *tp = '\0';
    d_name_len += dirpos;

    /* Allocate the file_t and the d_name entries */
    newfile = init_newfile(d_name_len, filelistp);

    tp = tempname;
    memcpy(newfile->d_name, tp, d_name_len + 1);

    /*** WARNING: tempname global gets reused by check_singlefile here! ***/

    /* Single-file [l]stat() and exclusion condition check */
    if (check_singlefile(newfile) != 0) {
      LOUD(fprintf(stderr, "loaddir: check_singlefile rejected file\n"));
      free(newfile->d_name);
      free(newfile);
      continue;
    }

    /* Optionally recurse directories, including symlinked ones if requested */
    if (JC_S_ISDIR(newfile->mode)) {
      if (recurse) {
        /* --one-file-system - WARNING: this clobbers inode/mode */
        if (ISFLAG(flags, F_ONEFS)
            && (getdirstats(newfile->d_name, &inode, &n_device, &mode) == 0)
            && (device != n_device)) {
          LOUD(fprintf(stderr, "loaddir: directory: not recursing (--one-file-system)\n"));
          free(newfile->d_name);
          free(newfile);
          continue;
        }
#ifndef NO_SYMLINKS
        else if (ISFLAG(flags, F_FOLLOWLINKS) || !ISFLAG(newfile->flags, FF_IS_SYMLINK)) {
          LOUD(fprintf(stderr, "loaddir: directory(symlink): recursing (-r/-R)\n"));
          if (push_dirqueue(newfile, recurse) != 0) goto error_push_dq;
        }
#else
        else {
          LOUD(fprintf(stderr, "loaddir: directory: recursing (-r/-R)\n"));
          if (push_dirqueue(newfile, recurse) != 0) goto error_push_dq;
        }
#endif /* NO_SYMLINKS */
      } else {
        LOUD(fprintf(stderr, "loaddir: directory: not recursing\n"));
        free(newfile->d_name);
      }
      free(newfile);
      if (unlikely(interrupt != 0)) return;
      continue;
    } else {
//add_single_file:
      /* Add regular files to list, including symlink targets if requested */
#ifndef NO_SYMLINKS
      if (!ISFLAG(newfile->flags, FF_IS_SYMLINK) || (ISFLAG(newfile->flags, FF_IS_SYMLINK) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
      if (JC_S_ISREG(newfile->mode)) {
#endif
#ifndef NO_HASHDB
        if (ISFLAG(flags, F_HASHDB)) read_hashdb_entry(newfile);
#endif
        *filelistp = newfile;
        filecount++;
        progress++;

      } else {
        LOUD(fprintf(stderr, "loaddir: not a regular file: %s\n", newfile->d_name);)
        free(newfile->d_name);
        free(newfile);
//    if (single == 1) return;
        continue;
      }
    }
    /* Skip directory stuff if adding only a single file */
//    if (single == 1) return;
  }

  jc_closedir(cd);
  free(dq.d_name);
/* end of queue loop */
}

  return;

error_stat_dir:
  fprintf(stderr, "\ncould not stat dir "); jc_fwprint(stderr, dir, 1);
  exit_status = EXIT_FAILURE;
  return;
error_cd:
  fprintf(stderr, "\ncould not chdir to "); jc_fwprint(stderr, dir, 1);
  exit_status = EXIT_FAILURE;
  return;
error_push_dq:
  fprintf(stderr, "\nfailed to allocate memory for the directory queue\n");
  exit_status = EXIT_FAILURE;
  return;
error_overflow:
  fprintf(stderr, "\nerror: a path overflowed (longer than JC_PATHBUF_SIZE) cannot continue\n");
  exit(EXIT_FAILURE);
}
