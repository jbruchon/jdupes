/* jdupes (C) 2015-2017 Jody Bruchon <jody@jodybruchon.com>
   Derived from fdupes (C) 1999-2017 Adrian Lopez

   Permission is hereby granted, free of charge, to any person
   obtaining a copy of this software and associated documentation files
   (the "Software"), to deal in the Software without restriction,
   including without limitation the rights to use, copy, modify, merge,
   publish, distribute, sublicense, and/or sell copies of the Software,
   and to permit persons to whom the Software is furnished to do so,
   subject to the following conditions:

   The above copyright notice and this permission notice shall be
   included in all copies or substantial portions of the Software.

   THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
   OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
   MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
   IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
   CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
   TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
   SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE. */

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <fcntl.h>
#include <dirent.h>
#include <signal.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#ifndef OMIT_GETOPT_LONG
 #include <getopt.h>
#endif
#include <string.h>
#include <errno.h>
#include <libgen.h>
#include <sys/time.h>
#include <search.h>
#include "jdupes.h"
#include "string_malloc.h"
#include "jody_hash.h"
#include "jody_sort.h"
#include "jody_win_unicode.h"
#include "jody_cacheinfo.h"
#include "version.h"

/* Headers for post-scanning actions */
#include "act_deletefiles.h"
#include "act_dedupefiles.h"
#include "act_linkfiles.h"
#include "act_printmatches.h"
#include "act_summarize.h"

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __CYGWIN__
 const char dir_sep = '\\';
 #ifdef UNICODE
  const wchar_t *FILE_MODE_RO = L"rbS";
 #else
  const char *FILE_MODE_RO = "rbS";
 #endif /* UNICODE */

#else /* Not Windows */
 const char *FILE_MODE_RO = "rb";
 const char dir_sep = '/';
 #ifdef UNICODE
  #error Do not define UNICODE on non-Windows platforms.
  #undef UNICODE
 #endif
#endif /* _WIN32 || __CYGWIN__ */

/* Windows + Unicode compilation */
#ifdef UNICODE
wchar_t wname[PATH_MAX];
wchar_t wname2[PATH_MAX];
wchar_t wstr[PATH_MAX];
int out_mode = _O_TEXT;
int err_mode = _O_TEXT;
 #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, PATH_MAX)
 #define W2M(a,b) WideCharToMultiByte(CP_UTF8, 0, a, -1, (LPSTR)b, PATH_MAX, NULL, NULL)
#endif /* UNICODE */

#ifndef NO_SYMLINKS
#include "jody_paths.h"
#endif

/* Behavior modification flags */
uint_fast32_t flags = 0;

static const char *program_name;

/* This gets used in many functions */
#ifdef ON_WINDOWS
struct winstat ws;
#else
struct stat s;
#endif

static uintmax_t excludesize = 0;
static enum {
  SMALLERTHAN,
  LARGERTHAN
} excludetype = SMALLERTHAN;

/* Larger chunk size makes large files process faster but uses more RAM */
#ifndef CHUNK_SIZE
 #define CHUNK_SIZE 32768
#endif
#ifndef PARTIAL_HASH_SIZE
 #define PARTIAL_HASH_SIZE 4096
#endif

static size_t auto_chunk_size;

/* Maximum path buffer size to use; must be large enough for a path plus
 * any work that might be done to the array it's stored in. PATH_MAX is
 * not always true. Read this article on the false promises of PATH_MAX:
 * http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
 * Windows + Unicode needs a lot more space than UTF-8 in Linux/Mac OS X
 */
#ifndef PATHBUF_SIZE
#define PATHBUF_SIZE 4096
#endif
/* Refuse to build if PATHBUF_SIZE is too small */
#if PATHBUF_SIZE < PATH_MAX
#error "PATHBUF_SIZE can't be less than PATH_MAX"
#endif

#ifndef INITIAL_DEPTH_THRESHOLD
#define INITIAL_DEPTH_THRESHOLD 8
#endif

/* For interactive deletion input */
#define INPUT_SIZE 512

/* Assemble extension string from compile-time options */
static const char *extensions[] = {
  #ifdef ON_WINDOWS
    "windows",
    #endif
    #ifdef UNICODE
    "unicode",
    #endif
    #ifdef OMIT_GETOPT_LONG
    "nolong",
    #endif
    #ifdef __FAST_MATH__
    "fastmath",
    #endif
    #ifdef DEBUG
    "debug",
    #endif
    #ifdef LOUD_DEBUG
    "loud",
    #endif
    #ifdef ENABLE_BTRFS
    "btrfs",
    #endif
    #ifdef LOW_MEMORY
    "lowmem",
    #endif
    #ifdef SMA_PAGE_SIZE
    "smapage",
    #endif
    #if JODY_HASH_WIDTH == 32
    "hash32",
    #endif
    #if JODY_HASH_WIDTH == 16
    "hash16",
    #endif
    #ifdef NO_PERMS
    "noperm",
    #endif
    #ifdef NO_SYMLINKS
    "nosymlink",
    #endif
    #ifdef USE_TREE_REBALANCE
    "rebal",
    #endif
    #ifdef CONSIDER_IMBALANCE
    "ci",
    #endif
    #ifdef BALANCE_THRESHOLD
    "bt",
    #endif
    NULL
};

/* Tree to track each directory traversed */
struct travdone {
  struct travdone *left;
  struct travdone *right;
  jdupes_ino_t inode;
  dev_t device;
};
static struct travdone *travdone_head = NULL;

/* Required for progress indicator code */
static uintmax_t filecount = 0;
static uintmax_t progress = 0, dir_progress = 0, dupecount = 0;
/* Number of read loops before checking progress indicator */
#define CHECK_MINIMUM 256

/* Hash/compare performance statistics (debug mode) */
#ifdef DEBUG
static unsigned int small_file = 0, partial_hash = 0, partial_elim = 0;
static unsigned int full_hash = 0, partial_to_full = 0, hash_fail = 0;
static uintmax_t comparisons = 0;
static unsigned int left_branch = 0, right_branch = 0;
 #ifdef ON_WINDOWS
  #ifndef NO_HARDLINKS
static unsigned int hll_exclude = 0;
  #endif
 #endif
#endif /* DEBUG */

#ifdef TREE_DEPTH_STATS
static unsigned int tree_depth = 0;
static unsigned int max_depth = 0;
#endif

/* reverse lookup {device, inode} -> file_t */
static void *revtree = NULL;

/* File tree head */
static filetree_t *checktree = NULL;

/* Directory parameter position counter */
static unsigned int user_dir_count = 1;

/* registerfile() direction options */
enum tree_direction { NONE, LEFT, RIGHT };

/* Sort order reversal */
static int sort_direction = 1;

/* Signal handler */
static int interrupt = 0;

/* Progress indicator time */
struct timeval time1, time2;


/***** End definitions, begin code *****/


/* Catch CTRL-C and either notify or terminate */
void sighandler(const int signum)
{
  (void)signum;
  if (interrupt || !ISFLAG(flags, F_SOFTABORT)) {
    fprintf(stderr, "\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }
  interrupt = 1;
  return;
}


/* Out of memory */
extern void oom(const char * const restrict msg)
{
  fprintf(stderr, "\nout of memory: %s\n", msg);
  string_malloc_destroy();
  exit(EXIT_FAILURE);
}


/* Null pointer failure */
extern void nullptr(const char * restrict func)
{
  static const char n[] = "(NULL)";
  if (func == NULL) func = n;
  fprintf(stderr, "\ninternal error: NULL pointer passed to %s\n", func);
  string_malloc_destroy();
  exit(EXIT_FAILURE);
}

/* Compare two jody_hashes like memcmp() */
#define HASH_COMPARE(a,b) ((a > b) ? 1:((a == b) ? 0:-1))


static inline char **cloneargs(const int argc, char **argv)
{
  static int x;
  static char **args;

  args = (char **)string_malloc(sizeof(char *) * (unsigned int)argc);
  if (args == NULL) oom("cloneargs() start");

  for (x = 0; x < argc; x++) {
    args[x] = (char *)string_malloc(strlen(argv[x]) + 1);
    if (args[x] == NULL) oom("cloneargs() loop");
    strcpy(args[x], argv[x]);
  }

  return args;
}


static int findarg(const char * const arg, const int start,
                const int argc, char **argv)
{
  int x;

  for (x = start; x < argc; x++)
    if (strcmp(argv[x], arg) == 0)
      return x;

  return x;
}

/* Find the first non-option argument after specified option. */
static int nonoptafter(const char *option, const int argc,
                char **oldargv, char **newargv)
{
  int x;
  int targetind;
  int testind;
  int startat = 1;

  targetind = findarg(option, 1, argc, oldargv);

  for (x = optind; x < argc; x++) {
    testind = findarg(newargv[x], startat, argc, oldargv);
    if (testind > targetind) return x;
    else startat = testind;
  }

  return x;
}


/* Update progress indicator if requested */
static void update_progress(const char * const restrict msg, const int file_percent)
{
  static int did_fpct = 0;

  /* The caller should be doing this anyway...but don't trust that they did */
  if (ISFLAG(flags, F_HIDEPROGRESS)) return;

  gettimeofday(&time2, NULL);

  if (progress == 0 || time2.tv_sec > time1.tv_sec) {
    fprintf(stderr, "\rProgress [%" PRIuMAX "/%" PRIuMAX ", %" PRIuMAX " pairs matched] %" PRIuMAX "%%",
      progress, filecount, dupecount, (progress * 100) / filecount);
    if (file_percent > -1 && msg != NULL) {
      fprintf(stderr, "  (%s: %d%%)         ", msg, file_percent);
      did_fpct = 1;
    } else if (did_fpct != 0) {
      fprintf(stderr, "                     ");
      did_fpct = 0;
    }
    fflush(stderr);
  }
  time1.tv_sec = time2.tv_sec;
  return;
}

/* comparison function for reverse-lookup tree
 * sort order: first by device then by inode */
static int compare_revtree(const void * const node1, const void * const node2) {
    const file_t * const file1 = (const file_t * const) node1;
    const file_t * const file2 = (const file_t * const) node2;

    int compare = (file1->device > file2->device) - (file1->device < file2->device);
    if (compare == 0) {
        compare = (file1->inode > file2->inode) - (file1->inode < file2->inode);
    }
    return compare;
}

/* Check file's stat() info to make sure nothing has changed
 * Returns 1 if changed, 0 if not changed, negative if error */
extern int file_has_changed(filename_t * const restrict filename)
{
  if (filename == NULL || filename->file == NULL || filename->d_name == NULL) nullptr("file_has_changed()");
  LOUD(fprintf(stderr, "file_has_changed('%s')\n", filename->d_name);)

  if (!ISFLAG(filename->file->flags, F_VALID_STAT)) return -66;

#ifdef ON_WINDOWS
  int i;
  if ((i = win_stat(filename->d_name, &ws)) != 0) return i;
  if (filename->file->inode != ws.inode) return 1;
  if (filename->file->size != ws.size) return 1;
  if (filename->file->device != ws.device) return 1;
  if (filename->file->mtime != ws.mtime) return 1;
  if (filename->file->mode != ws.mode) return 1;
#else
  if (stat(filename->d_name, &s) != 0) return -2;
  if (filename->file->inode != s.st_ino) return 1;
  if (filename->file->size != s.st_size) return 1;
  if (filename->file->device != s.st_dev) return 1;
  if (filename->file->mtime != s.st_mtime) return 1;
  if (filename->file->mode != s.st_mode) return 1;
 #ifndef NO_PERMS
  if (filename->file->uid != s.st_uid) return 1;
  if (filename->file->gid != s.st_gid) return 1;
 #endif
 #ifndef NO_SYMLINKS
  if (lstat(filename->d_name, &s) != 0) return -3;
  if ((S_ISLNK(s.st_mode) > 0) ^ ISFLAG(filename->file->flags, F_IS_SYMLINK)) return 1;
 #endif
#endif /* ON_WINDOWS */

  return 0;
}


extern inline int getfilestats(file_t * const restrict file)
{
  if (file == NULL || file->filename == NULL || file->filename->d_name == NULL) nullptr("getfilestats()");
  LOUD(fprintf(stderr, "getfilestats('%s')\n", file->filename->d_name);)

  /* Don't stat the same file more than once */
  if (ISFLAG(file->flags, F_VALID_STAT)) return 0;
  SETFLAG(file->flags, F_VALID_STAT);

#ifdef ON_WINDOWS
  if (win_stat(file->filename->d_name, &ws) != 0) return -1;
  file->inode = ws.inode;
  file->size = ws.size;
  file->device = ws.device;
  file->mtime = ws.mtime;
  file->mode = ws.mode;
 #ifndef NO_HARDLINKS
  file->nlink = ws.nlink;
 #endif /* NO_HARDLINKS */
#else
  if (stat(file->filename->d_name, &s) != 0) return -1;
  file->inode = s.st_ino;
  file->size = s.st_size;
  file->device = s.st_dev;
  file->mtime = s.st_mtime;
  file->mode = s.st_mode;
 #ifndef NO_HARDLINKS
  file->nlink = s.st_nlink;
 #endif
 #ifndef NO_PERMS
  file->uid = s.st_uid;
  file->gid = s.st_gid;
 #endif
 #ifndef NO_SYMLINKS
  if (lstat(file->filename->d_name, &s) != 0) return -1;
  if (S_ISLNK(s.st_mode) > 0) SETFLAG(file->flags, F_IS_SYMLINK);
 #endif
#endif /* ON_WINDOWS */
  return 0;
}


extern int getdirstats(const char * const restrict name,
        jdupes_ino_t * const restrict inode, dev_t * const restrict dev)
{
  if (name == NULL || inode == NULL || dev == NULL) nullptr("getdirstats");
  LOUD(fprintf(stderr, "getdirstats('%s', %p, %p)\n", name, (void *)file, (void *)dev);)

#ifdef ON_WINDOWS
  if (win_stat(name, &ws) != 0) return -1;
  *inode = ws.inode;
  *dev = ws.device;
#else
  if (stat(name, &s) != 0) return -1;
  *inode = s.st_ino;
  *dev = s.st_dev;
#endif /* ON_WINDOWS */
  return 0;
}


/* Check a pair of files for match exclusion conditions
 * Returns:
 *  0 if all condition checks pass
 * -1 or 1 on compare result less/more
 * -2 on an absolute exclusion condition met
 *  2 on an absolute match condition met */
extern int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2)
{
  if (file1 == NULL || file1->filename == NULL || file1->filename->d_name == NULL
      || file2 == NULL || file2->filename == NULL || file2->filename->d_name == NULL) nullptr("check_conditions()");

  LOUD(fprintf(stderr, "check_conditions('%s', '%s')\n", file1->filename->d_name, file2->filename->d_name);)

  /* Exclude based on -I/--isolate */
  if (ISFLAG(flags, F_ISOLATE) && (file1->filename->user_order == file2->filename->user_order)) {
    LOUD(fprintf(stderr, "check_conditions: files ignored: parameter isolation\n"));
    return -1;
  }

  /* Exclude based on -1/--one-file-system */
  if (ISFLAG(flags, F_ONEFS) && (file1->device != file2->device)) {
    LOUD(fprintf(stderr, "check_conditions: files ignored: not on same filesystem\n"));
    return -1;
  }

 /* Exclude files by permissions if requested */
  if (ISFLAG(flags, F_PERMISSIONS) &&
          (file1->mode != file2->mode
#ifndef NO_PERMS
          || file1->uid != file2->uid
          || file1->gid != file2->gid
#endif
          )) {
    return -1;
    LOUD(fprintf(stderr, "check_conditions: no match: permissions/ownership differ (-p on)\n"));
  }

  /* Hard link and symlink + '-s' check */
#ifndef NO_HARDLINKS
  if ((file1->filename == file2->filename) && (file1->device == file2->device)) {
    if (ISFLAG(flags, F_CONSIDERHARDLINKS)) {
      LOUD(fprintf(stderr, "check_conditions: files match: hard/soft linked (-H on)\n"));
      return 2;
    } else {
      LOUD(fprintf(stderr, "check_conditions: files ignored: hard/soft linked (-H off)\n"));
      return -2;
    }
  }
#endif

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

  /* Fall through: all checks passed */
  LOUD(fprintf(stderr, "check_conditions: all condition checks passed\n"));
  return 0;
}


/* Create a new traversal check object and initialize its values */
static struct travdone *travdone_alloc(const jdupes_ino_t inode, const dev_t device)
{
  struct travdone *trav;

  LOUD(fprintf(stderr, "travdone_alloc(%" PRIdMAX ", %" PRIdMAX ")\n", (intmax_t)file, (intmax_t)device);)

  trav = (struct travdone *)string_malloc(sizeof(struct travdone));
  if (trav == NULL) {
    LOUD(fprintf(stderr, "travdone_alloc: malloc failed\n");)
    return NULL;
  }
  trav->left = NULL;
  trav->right = NULL;
  trav->inode = inode;
  trav->device = device;
  LOUD(fprintf(stderr, "travdone_alloc returned %p\n", (void *)trav);)
  return trav;
}


/* Load a directory's contents into the file tree, recursing as needed */
static void grokdir(const char * const restrict dir,
                file_t * restrict * const restrict filelistp,
                int recurse)
{
  filename_t * restrict newfilename;
  file_t * restrict newfile;
  file_t * restrict filerev;
#ifndef NO_SYMLINKS
  static struct stat linfo;
#endif
  struct dirent *dirinfo;
  static int grokdir_level = 0;
  static char tempname[PATHBUF_SIZE * 2];
  size_t dirlen;
  struct travdone *traverse;
  jdupes_ino_t inode, n_inode;
  dev_t device, n_device;
#ifdef UNICODE
  WIN32_FIND_DATA ffd;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  char *p;
#else
  DIR *cd;
#endif

  if (dir == NULL || filelistp == NULL) nullptr("grokdir()");
  LOUD(fprintf(stderr, "grokdir: scanning '%s' (order %d)\n", dir, user_dir_count));

  /* Double traversal prevention tree */
  if (getdirstats(dir, &inode, &device) != 0) goto error_travdone;
  if (travdone_head == NULL) {
    travdone_head = travdone_alloc(inode, device);
    if (travdone_head == NULL) goto error_travdone;
  } else {
    traverse = travdone_head;
    while (1) {
      if (traverse == NULL) nullptr("grokdir() traverse");
      /* Don't re-traverse directories we've already seen */
      if (inode == traverse->inode && device == traverse->device) {
        LOUD(fprintf(stderr, "already seen dir '%s', skipping\n", dir);)
        return;
      } else if (inode > traverse->inode || (inode == traverse->inode && device > traverse->device)) {
        /* Traverse right */
        if (traverse->right == NULL) {
          LOUD(fprintf(stderr, "traverse dir right '%s'\n", dir);)
          traverse->right = travdone_alloc(inode, device);
          if (traverse->right == NULL) goto error_travdone;
          break;
        }
        traverse = traverse->right;
        continue;
      } else {
        /* Traverse left */
        if (traverse->left == NULL) {
          LOUD(fprintf(stderr, "traverse dir left '%s'\n", dir);)
          traverse->left = travdone_alloc(inode, device);
          if (traverse->left == NULL) goto error_travdone;
          break;
        }
        traverse = traverse->left;
        continue;
      }
    }
  }

  dir_progress++;
  grokdir_level++;

#ifdef UNICODE
  /* Windows requires \* at the end of directory names */
  strncpy(tempname, dir, PATHBUF_SIZE * 2);
  dirlen = strlen(tempname) - 1;
  p = tempname + dirlen;
  if (*p == '/' || *p == '\\') *p = '\0';
  strncat(tempname, "\\*", PATHBUF_SIZE * 2);

  if (!M2W(tempname, wname)) goto error_cd;

  LOUD(fprintf(stderr, "FindFirstFile: %s\n", dir));
  hFind = FindFirstFile((LPCWSTR)wname, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) { fprintf(stderr, "\nfile handle bad\n"); goto error_cd; }
  LOUD(fprintf(stderr, "Loop start\n"));
  do {
    char * restrict tp = tempname;
    size_t d_name_len;

    /* Get necessary length and allocate d_name */
    dirinfo = (struct dirent *)string_malloc(sizeof(struct dirent));
    if (!W2M(ffd.cFileName, dirinfo->d_name)) continue;
#else
  cd = opendir(dir);
  if (!cd) goto error_cd;

  while ((dirinfo = readdir(cd)) != NULL) {
    char * restrict tp = tempname;
    size_t d_name_len;
#endif /* UNICODE */

    LOUD(fprintf(stderr, "grokdir: readdir: '%s'\n", dirinfo->d_name));
    if (strcmp(dirinfo->d_name, ".") && strcmp(dirinfo->d_name, "..")) {
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        gettimeofday(&time2, NULL);
        if (progress == 0 || time2.tv_sec > time1.tv_sec) {
          fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " dirs (in %u specified)",
              progress, dir_progress, user_dir_count);
        }
        time1.tv_sec = time2.tv_sec;
      }

      /* Assemble the file's full path name, optimized to avoid strcat() */
      dirlen = strlen(dir);
      d_name_len = strlen(dirinfo->d_name);
      memcpy(tp, dir, dirlen+1);
      if (dirlen != 0 && tp[dirlen-1] != dir_sep) {
        tp[dirlen] = dir_sep;
        dirlen++;
      }
      if (dirlen + d_name_len + 1 >= (PATHBUF_SIZE * 2)) goto error_overflow;
      tp += dirlen;
      memcpy(tp, dirinfo->d_name, d_name_len);
      tp += d_name_len;
      *tp = '\0';
      d_name_len++;

      /* Allocate the file_t, filename_t and the d_name entries */
      newfile = (file_t *)string_malloc(sizeof(file_t));
      if (!newfile) oom("grokdir() file structure");
      newfilename = (filename_t *)string_malloc(sizeof(filename_t));
      if (!newfilename) oom("grokdir() filename structure");
      newfilename->d_name = (char *)string_malloc(dirlen + d_name_len + 2);
      if (!newfilename->d_name) oom("grokdir() filename");

      newfilename->file = newfile;
      newfilename->user_order = user_dir_count;
      
      newfile->filename = newfilename;

      newfile->next = *filelistp;
      newfile->size = -1;
      newfile->device = 0;
      newfile->inode = 0;
      newfile->mtime = 0;
      newfile->mode = 0;
#ifndef NO_HARDLINKS
      newfile->nlink = 0;
#endif
#ifndef NO_PERMS
      newfile->uid = 0;
      newfile->gid = 0;
#endif
      newfile->filehash = 0;
      newfile->filehash_partial = 0;
      newfile->duplicates = NULL;
      newfile->flags = 0;

      tp = tempname;
      memcpy(newfilename->d_name, tp, dirlen + d_name_len);

      if (ISFLAG(flags, F_EXCLUDEHIDDEN)) {
        /* WARNING: Re-used tp here to eliminate a strdup() */
        strncpy(tp, newfilename->d_name, dirlen + d_name_len);
        tp = basename(tp);
        if (tp[0] == '.' && strcmp(tp, ".") && strcmp(tp, "..")) {
          LOUD(fprintf(stderr, "grokdir: excluding hidden file (-A on)\n"));
          string_free(newfilename->d_name);
          string_free(newfile);
          string_free(newfile);
          continue;
        }
      }

      /* Get file information and check for validity */
      const int i = getfilestats(newfile);
      if (i || newfile->size == -1) {
        LOUD(fprintf(stderr, "grokdir: excluding due to bad stat()\n"));
        string_free(newfilename->d_name);
        string_free(newfile);
        string_free(newfile);
        continue;
      }

      /* Exclude zero-length files if requested */
      if (!S_ISDIR(newfile->mode) && newfile->size == 0 && !ISFLAG(flags, F_INCLUDEEMPTY)) {
        LOUD(fprintf(stderr, "grokdir: excluding zero-length empty file (-z not set)\n"));
        string_free(newfilename->d_name);
        string_free(newfile);
        string_free(newfile);
        continue;
      }

      /* Exclude files below --xsize parameter */
      if (!S_ISDIR(newfile->mode) && ISFLAG(flags, F_EXCLUDESIZE)) {
        if (
            ((excludetype == SMALLERTHAN) && (newfile->size < (off_t)excludesize)) ||
            ((excludetype == LARGERTHAN) && (newfile->size > (off_t)excludesize))
        ) {
          LOUD(fprintf(stderr, "grokdir: excluding based on xsize limit (-x set)\n"));
          string_free(newfilename->d_name);
          string_free(newfile);
          string_free(newfile);
          continue;
        }
      }

#ifndef NO_SYMLINKS
      /* Get lstat() information */
      if (lstat(newfilename->d_name, &linfo) == -1) {
        LOUD(fprintf(stderr, "grokdir: excluding due to bad lstat()\n"));
        string_free(newfilename->d_name);
        string_free(newfile);
        string_free(newfile);
        continue;
      }
#endif

      /* Windows has a 1023 (+1) hard link limit. If we're hard linking,
       * ignore all files that have hit this limit */
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
      if (ISFLAG(flags, F_HARDLINKFILES) && newfile->nlink >= 1024) {
  #ifdef DEBUG
        hll_exclude++;
  #endif
        LOUD(fprintf(stderr, "grokdir: excluding due to Windows 1024 hard link limit\n"));
        string_free(newfilename->d_name);
        string_free(newfile);
        string_free(newfile);
        continue;
      }
 #endif
#endif
      /* Optionally recurse directories, including symlinked ones if requested */
      if (S_ISDIR(newfile->mode)) {
        if (recurse) {
          /* --one-file-system */
          if (ISFLAG(flags, F_ONEFS)
              && (getdirstats(newfilename->d_name, &n_inode, &n_device) == 0)
              && (device != n_device)) {
            LOUD(fprintf(stderr, "grokdir: directory: not recursing (--one-file-system)\n"));
            string_free(newfilename->d_name);
            string_free(newfile);
            string_free(newfile);
            continue;
          }
#ifndef NO_SYMLINKS
          else if (/*ISFLAG(flags, F_FOLLOWLINKS) ||*/ !S_ISLNK(linfo.st_mode)) {
            LOUD(fprintf(stderr, "grokdir: directory: recursing (-r/-R)\n"));
            grokdir(newfilename->d_name, filelistp, recurse);
          }
#else
          else {
            LOUD(fprintf(stderr, "grokdir: directory: recursing (-r/-R)\n"));
            grokdir(newfilename->d_name, filelistp, recurse);
          }
#endif
        }
        LOUD(fprintf(stderr, "grokdir: directory: not recursing\n"));
        string_free(newfilename->d_name);
        string_free(newfile);
        string_free(newfile);
        continue;
      } else {
        /* Add regular files to list, including symlink targets if requested */
#ifndef NO_SYMLINKS
        if (S_ISREG(linfo.st_mode) || (S_ISLNK(linfo.st_mode) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
        if (S_ISREG(newfile->mode)) {
#endif
          filecount++;
          progress++;

#ifndef NO_HARDLINKS
          /* store reverse lookup {device, inode} -> file
           *
           * as this is used to detect hardlinked files,
           * it is only needed if the link count is > 1 */
	  if (newfile->nlink > 1) {
	    filerev = tsearch(newfile, &revtree, compare_revtree);
	    if (!filerev) oom("grokdir() reverse lookup tree node");

	    filerev = *(file_t **) filerev; /* get pointer from tree node */
	    if (filerev == newfile) {
	      /* a fresh device/inode pair */
	      LOUD(fprintf(stderr, "new reverse lookup for %s\n", newfile->filename->d_name));

	      /* store file_t normally */
	      *filelistp = newfile;
	    }

	    else {
	      /* already known device/inode pair */
	      LOUD(fprintf(stderr, "known reverse lookup from %s to %s\n", newfile->filename->d_name, filerev->filename->d_name));

	      /* throw away new file_t */
	      string_free(newfile);

	      /* add filename to existing file_t */
	      newfilename->next = filerev->filename;
	      filerev->filename = newfilename;

	      /* add reverse lookup from filename to file */
	      newfilename->file = filerev;
	    }
	  } else {
	    LOUD(fprintf(stderr, "skip reverse lookup, nlink < 2\n"));
#endif /* NO_HARDLINKS */
	    /* store file_t normally */
	    *filelistp = newfile;
#ifndef NO_HARDLINKS
	  }
#endif

        } else {
          LOUD(fprintf(stderr, "grokdir: not a regular file: %s\n", newfilename->d_name);)
          string_free(newfilename->d_name);
          string_free(newfilename);
          string_free(newfile);
          continue;
        }
      }
    }
  }
#ifdef UNICODE
  while (FindNextFile(hFind, &ffd) != 0);
  FindClose(hFind);
#else
  closedir(cd);
#endif


  grokdir_level--;
  if (grokdir_level == 0 && !ISFLAG(flags, F_HIDEPROGRESS)) {
    fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " dirs (in %u specified)",
            progress, dir_progress, user_dir_count);
  }
  return;

error_travdone:
  fprintf(stderr, "\ncould not stat dir "); fwprint(stderr, dir, 1);
  return;
error_cd:
  fprintf(stderr, "\ncould not chdir to "); fwprint(stderr, dir, 1);
  return;
error_overflow:
  fprintf(stderr, "\nerror: a path buffer overflowed\n");
  exit(EXIT_FAILURE);
}

/* Use Jody Bruchon's hash function on part or all of a file */
static hash_t *get_filehash(const file_t * const restrict checkfile,
                const size_t max_read)
{
  off_t fsize;
  /* This is an array because we return a pointer to it */
  static hash_t hash[1];
  static hash_t chunk[(CHUNK_SIZE / sizeof(hash_t))];
  FILE *file;
  int check = 0;

  if (checkfile == NULL || checkfile->filename == NULL || checkfile->filename->d_name == NULL) nullptr("get_filehash()");
  LOUD(fprintf(stderr, "get_filehash('%s', %" PRIdMAX ")\n", checkfile->filename->d_name, (intmax_t)max_read);)

  /* Get the file size. If we can't read it, bail out early */
  if (checkfile->size == -1) {
    LOUD(fprintf(stderr, "get_filehash: not hashing because stat() info is bad\n"));
    return NULL;
  }
  fsize = checkfile->size;

  /* Do not read more than the requested number of bytes */
  if (max_read > 0 && fsize > (off_t)max_read)
    fsize = (off_t)max_read;

  /* Initialize the hash and file read parameters (with filehash_partial skipped)
   *
   * If we already hashed the first chunk of this file, we don't want to
   * wastefully read and hash it again, so skip the first chunk and use
   * the computed hash for that chunk as our starting point.
   *
   * WARNING: We assume max_read is NEVER less than CHUNK_SIZE here! */

  *hash = 0;
  if (ISFLAG(checkfile->flags, F_HASH_PARTIAL)) {
    *hash = checkfile->filehash_partial;
    /* Don't bother going further if max_read is already fulfilled */
    if (max_read != 0 && max_read <= PARTIAL_HASH_SIZE) {
      LOUD(fprintf(stderr, "Partial hash size (%d) >= max_read (%" PRIuMAX "), not hashing anymore\n", PARTIAL_HASH_SIZE, (uintmax_t)max_read);)
      return hash;
    }
  }
#ifdef UNICODE
  if (!M2W(checkfile->filename->d_name, wstr)) file = NULL;
  else file = _wfopen(wstr, FILE_MODE_RO);
#else
  file = fopen(checkfile->filename->d_name, FILE_MODE_RO);
#endif
  if (file == NULL) {
    fprintf(stderr, "\nerror opening file "); fwprint(stderr, checkfile->filename->d_name, 1);
    return NULL;
  }
  /* Actually seek past the first chunk if applicable
   * This is part of the filehash_partial skip optimization */
  if (ISFLAG(checkfile->flags, F_HASH_PARTIAL)) {
    if (fseeko(file, PARTIAL_HASH_SIZE, SEEK_SET) == -1) {
      fclose(file);
      fprintf(stderr, "\nerror seeking in file "); fwprint(stderr, checkfile->filename->d_name, 1);
      return NULL;
    }
    fsize -= PARTIAL_HASH_SIZE;
  }
  /* Read the file in CHUNK_SIZE chunks until we've read it all. */
  while (fsize > 0) {
    size_t bytes_to_read;

    if (interrupt) return 0;
    bytes_to_read = (fsize >= (off_t)auto_chunk_size) ? auto_chunk_size : (size_t)fsize;
    if (fread((void *)chunk, bytes_to_read, 1, file) != 1) {
      fprintf(stderr, "\nerror reading from file "); fwprint(stderr, checkfile->filename->d_name, 1);
      fclose(file);
      return NULL;
    }

    *hash = jody_block_hash(chunk, *hash, bytes_to_read);
    if ((off_t)bytes_to_read > fsize) break;
    else fsize -= (off_t)bytes_to_read;

    if (!ISFLAG(flags, F_HIDEPROGRESS)) {
      check++;
      if (check > CHECK_MINIMUM) {
        update_progress("hashing", (int)(((checkfile->size - fsize) * 100) / checkfile->size));
        check = 0;
      }
    }
  }

  fclose(file);

  LOUD(fprintf(stderr, "get_filehash: returning hash: 0x%016jx\n", (uintmax_t)*hash));
  return hash;
}


static inline void registerfile(filetree_t * restrict * const restrict nodeptr,
                const enum tree_direction d, file_t * const restrict file)
{
  filetree_t * restrict branch;

  if (nodeptr == NULL || file == NULL || (d != NONE && *nodeptr == NULL)) nullptr("registerfile()");
  LOUD(fprintf(stderr, "registerfile(direction %d)\n", d));

  /* Allocate and initialize a new node for the file */
  branch = (filetree_t *)string_malloc(sizeof(filetree_t));
  if (branch == NULL) oom("registerfile() branch");
  branch->file = file;
  branch->left = NULL;
  branch->right = NULL;
#ifdef USE_TREE_REBALANCE
  branch->left_weight = 0;
  branch->right_weight = 0;

  /* Attach the new node to the requested branch and the parent */
  switch (d) {
    case LEFT:
      branch->parent = *nodeptr;
      (*nodeptr)->left = branch;
      (*nodeptr)->left_weight++;
      break;
    case RIGHT:
      branch->parent = *nodeptr;
      (*nodeptr)->right = branch;
      (*nodeptr)->right_weight++;
      break;
    case NONE:
      /* For the root of the tree only */
      branch->parent = NULL;
      *nodeptr = branch;
      break;
    default:
      /* This should never ever happen */
      fprintf(stderr, "\ninternal error: invalid direction for registerfile(), report this\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
      break;
  }

  /* Propagate weights up the tree */
  while (branch->parent != NULL) {
    filetree_t * restrict up;

    up = branch->parent;
    if (up->left == branch) up->left_weight++;
    else if (up->right == branch) up->right_weight++;
    else {
      fprintf(stderr, "\nInternal error: file tree linkage is broken\n");
      exit(EXIT_FAILURE);
    }
    branch = up;
  }
#else /* USE_TREE_REBALANCE */
  /* Attach the new node to the requested branch */
  switch (d) {
    case LEFT:
      (*nodeptr)->left = branch;
      break;
    case RIGHT:
      (*nodeptr)->right = branch;
      break;
    case NONE:
      /* For the root of the tree only */
      *nodeptr = branch;
      break;
    default:
      /* This should never ever happen */
      fprintf(stderr, "\ninternal error: invalid direction for registerfile(), report this\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
      break;
  }

#endif /* USE_TREE_REBALANCE */

  return;
}


/* Experimental tree rebalance code. This slows things down in testing
 * but may be more useful in the future. Pass -DUSE_TREE_REBALANCE
 * to try it. */
#ifdef USE_TREE_REBALANCE

/* How much difference to ignore when considering a rebalance */
#ifndef BALANCE_THRESHOLD
#define BALANCE_THRESHOLD 4
#endif

/* Rebalance the file tree to reduce search depth */
static inline void rebalance_tree(filetree_t * const tree)
{
  filetree_t * restrict promote;
  filetree_t * restrict demote;
  int difference, direction;
#ifdef CONSIDER_IMBALANCE
  int l, r, imbalance;
#endif

  if (!tree) return;

  /* Rebalance all children first */
  if (tree->left_weight > BALANCE_THRESHOLD) rebalance_tree(tree->left);
  if (tree->right_weight > BALANCE_THRESHOLD) rebalance_tree(tree->right);

  /* If weights are within a certain threshold, do nothing */
  direction = tree->right_weight - tree->left_weight;
  difference = direction;
  if (difference < 0) difference = -difference;
  if (difference <= BALANCE_THRESHOLD) return;

  /* Determine if a tree rotation will help, and do it if so */
  if (direction > 0) {
#ifdef CONSIDER_IMBALANCE
    l = tree->right->left_weight + tree->right_weight;
    r = tree->right->right_weight;
    imbalance = l - r;
    if (imbalance < 0) imbalance = -imbalance;
    /* Don't rotate if imbalance will increase */
    if (imbalance >= difference) return;
#endif /* CONSIDER_IMBALANCE */

    /* Rotate the right node up one level */
    promote = tree->right;
    demote = tree;
    /* Attach new parent's left tree to old parent */
    demote->right = promote->left;
    demote->right_weight = promote->left_weight;
    /* Attach old parent to new parent */
    promote->left = demote;
    promote->left_weight = demote->left_weight + demote->right_weight + 1;
    /* Reconnect parent linkages */
    promote->parent = demote->parent;
    if (demote->right) demote->right->parent = demote;
    demote->parent = promote;
    if (promote->parent == NULL) checktree = promote;
    else if (promote->parent->left == demote) promote->parent->left = promote;
    else promote->parent->right = promote;
    return;
  } else if (direction < 0) {
#ifdef CONSIDER_IMBALANCE
    r = tree->left->right_weight + tree->left_weight;
    l = tree->left->left_weight;
    imbalance = r - l;
    if (imbalance < 0) imbalance = -imbalance;
    /* Don't rotate if imbalance will increase */
    if (imbalance >= difference) return;
#endif /* CONSIDER_IMBALANCE */

    /* Rotate the left node up one level */
    promote = tree->left;
    demote = tree;
    /* Attach new parent's right tree to old parent */
    demote->left = promote->right;
    demote->left_weight = promote->right_weight;
    /* Attach old parent to new parent */
    promote->right = demote;
    promote->right_weight = demote->right_weight + demote->left_weight + 1;
    /* Reconnect parent linkages */
    promote->parent = demote->parent;
    if (demote->left) demote->left->parent = demote;
    demote->parent = promote;
    if (promote->parent == NULL) checktree = promote;
    else if (promote->parent->left == demote) promote->parent->left = promote;
    else promote->parent->right = promote;
    return;

  }

  /* Fall through */
  return;
}

#endif /* USE_TREE_REBALANCE */


#ifdef TREE_DEPTH_STATS
#define TREE_DEPTH_UPDATE_MAX() { if (max_depth < tree_depth) max_depth = tree_depth; tree_depth = 0; }
#else
#define TREE_DEPTH_UPDATE_MAX()
#endif


/* Check two files for a match */
static file_t **checkmatch(filetree_t * restrict tree, file_t * const restrict file)
{
  int cmpresult = 0;
  const hash_t * restrict filehash;

  if (tree == NULL || tree->file == NULL || tree->file->filename == NULL || tree->file->filename->d_name == NULL
      || file == NULL || file->filename==NULL || file->filename->d_name == NULL) nullptr("checkmatch()");
  LOUD(fprintf(stderr, "checkmatch ('%s', '%s')\n", tree->file->filename->d_name, file->filename->d_name));

  /* If device and file fields are equal one of the files is a
   * hard link to the other or the files have been listed twice
   * unintentionally. We don't want to flag these files as
   * duplicates unless the user specifies otherwise. */

  /* Count the total number of comparisons requested */
  DBG(comparisons++;)

/* If considering hard linked files as duplicates, they are
 * automatically duplicates without being read further since
 * they point to the exact same file. If we aren't considering
 * hard links as duplicates, we just return NULL. */

  cmpresult = check_conditions(tree->file, file);
  switch (cmpresult) {
    case 2: return &tree->file;  /* linked files + -H switch */
    case -2: return NULL;  /* linked files, no -H switch */
    default: break;
  }

  /* If preliminary matching succeeded, move to full file checks */
  if (cmpresult == 0) {
    LOUD(fprintf(stderr, "checkmatch: starting file data comparisons\n"));
    /* Attempt to exclude files quickly with partial file hashing */
    if (!ISFLAG(tree->file->flags, F_HASH_PARTIAL)) {
      filehash = get_filehash(tree->file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) return NULL;

      tree->file->filehash_partial = *filehash;
      SETFLAG(tree->file->flags, F_HASH_PARTIAL);
    }

    if (!ISFLAG(file->flags, F_HASH_PARTIAL)) {
      filehash = get_filehash(file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) return NULL;

      file->filehash_partial = *filehash;
      SETFLAG(file->flags, F_HASH_PARTIAL);
    }

    cmpresult = HASH_COMPARE(file->filehash_partial, tree->file->filehash_partial);
    LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: partial hashes match\n"));
    LOUD(if (cmpresult) fprintf(stderr, "checkmatch: partial hashes do not match\n"));
    DBG(partial_hash++;)

    if (file->size <= PARTIAL_HASH_SIZE) {
      LOUD(fprintf(stderr, "checkmatch: small file: copying partial hash to full hash\n"));
      /* filehash_partial = filehash if file is small enough */
      if (!ISFLAG(file->flags, F_HASH_FULL)) {
        file->filehash = file->filehash_partial;
        SETFLAG(file->flags, F_HASH_FULL);
        DBG(small_file++;)
      }
      if (!ISFLAG(tree->file->flags, F_HASH_FULL)) {
        tree->file->filehash = tree->file->filehash_partial;
        SETFLAG(tree->file->flags, F_HASH_FULL);
        DBG(small_file++;)
      }
    } else if (cmpresult == 0) {
      /* If partial match was correct, perform a full file hash match */
      if (!ISFLAG(tree->file->flags, F_HASH_FULL)) {
        filehash = get_filehash(tree->file, 0);
        if (filehash == NULL) return NULL;

        tree->file->filehash = *filehash;
        SETFLAG(tree->file->flags, F_HASH_FULL);
      }

      if (!ISFLAG(file->flags, F_HASH_FULL)) {
        filehash = get_filehash(file, 0);
        if (filehash == NULL) return NULL;

        file->filehash = *filehash;
        SETFLAG(file->flags, F_HASH_FULL);
      }

      /* Full file hash comparison */
      cmpresult = HASH_COMPARE(file->filehash, tree->file->filehash);
      LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: full hashes match\n"));
      LOUD(if (cmpresult) fprintf(stderr, "checkmatch: full hashes do not match\n"));
      DBG(full_hash++);
    } else {
      DBG(partial_elim++);
    }
  }

  if (cmpresult < 0) {
    if (tree->left != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: left\n"));
      DBG(left_branch++; tree_depth++;)
      return checkmatch(tree->left, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: left\n"));
      registerfile(&tree, LEFT, file);
      TREE_DEPTH_UPDATE_MAX();
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (tree->right != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: right\n"));
      DBG(right_branch++; tree_depth++;)
      return checkmatch(tree->right, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: right\n"));
      registerfile(&tree, RIGHT, file);
      TREE_DEPTH_UPDATE_MAX();
      return NULL;
    }
  } else {
    /* All compares matched */
    DBG(partial_to_full++;)
    TREE_DEPTH_UPDATE_MAX();
    LOUD(fprintf(stderr, "checkmatch: files appear to match based on hashes\n"));
    return &tree->file;
  }
  /* Fall through - should never be reached */
  return NULL;
}


/* Do a byte-by-byte comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */
static inline int confirmmatch(FILE * const restrict file1, FILE * const restrict file2, off_t size)
{
  static char c1[CHUNK_SIZE], c2[CHUNK_SIZE];
  size_t r1, r2;
  off_t bytes = 0;
  int check = 0;

  if (file1 == NULL || file2 == NULL) nullptr("confirmmatch()");
  LOUD(fprintf(stderr, "confirmmatch running\n"));

  fseek(file1, 0, SEEK_SET);
  fseek(file2, 0, SEEK_SET);

  do {
    if (interrupt) return 0;
    r1 = fread(c1, sizeof(char), auto_chunk_size, file1);
    r2 = fread(c2, sizeof(char), auto_chunk_size, file2);

    if (r1 != r2) return 0; /* file lengths are different */
    if (memcmp (c1, c2, r1)) return 0; /* file contents are different */

    if (!ISFLAG(flags, F_HIDEPROGRESS)) {
      check++;
      bytes += (off_t)r1;
      if (check > CHECK_MINIMUM) {
        update_progress("confirm", (int)((bytes * 100) / size));
        check = 0;
      }
    }
  } while (r2);

  return 1;
}


/* Count the following statistics:
   - Maximum number of files in a duplicate set (length of longest dupe chain)
   - Number of non-zero-length files that have duplicates (if n_files != NULL)
   - Total number of duplicate file sets (groups) */
extern unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
                unsigned int * const restrict n_files) {
  unsigned int groups = 0;

  if (files == NULL || max == NULL) nullptr("get_max_dupes()");
  LOUD(fprintf(stderr, "get_max_dupes(%p, %p, %p)\n", (const void *)files, (void *)max, (void *)n_files));

  *max = 0;
  if (n_files) *n_files = 0;

  while (files) {
    unsigned int n_dupes;
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      groups++;
      if (n_files && files->size) (*n_files)++;
      n_dupes = 1;
      for (file_t *curdupe = files->duplicates; curdupe; curdupe = curdupe->duplicates) n_dupes++;
      if (n_dupes > *max) *max = n_dupes;
    }
    files = files->next;
  }
  return groups;
}


static int sort_pairs_by_param_order(file_t *f1, file_t *f2)
{
  if (!ISFLAG(flags, F_USEPARAMORDER)) return 0;
  if (f1 == NULL || f2 == NULL || f1->filename == NULL || f2->filename == NULL) nullptr("sort_pairs_by_param_order()");
  if (f1->filename->user_order < f2->filename->user_order) return -sort_direction;
  if (f1->filename->user_order > f2->filename->user_order) return sort_direction;
  return 0;
}


static int sort_pairs_by_mtime(file_t *f1, file_t *f2)
{
  if (f1 == NULL || f2 == NULL) nullptr("sort_pairs_by_mtime()");
  int po = sort_pairs_by_param_order(f1, f2);

  if (po != 0) return po;

  if (f1->mtime < f2->mtime) return -sort_direction;
  else if (f1->mtime > f2->mtime) return sort_direction;

  return 0;
}


static int sort_pairs_by_filename(file_t *f1, file_t *f2)
{
  if (f1 == NULL || f2 == NULL || f1->filename == NULL || f2->filename == NULL) nullptr("sort_pairs_by_filename()");
  int po = sort_pairs_by_param_order(f1, f2);

  if (po != 0) return po;

  return numeric_sort(f1->filename->d_name, f2->filename->d_name, sort_direction);
}


static void registerpair(file_t **matchlist, file_t *newmatch,
                int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  /* NULL pointer sanity checks */
  if (matchlist == NULL || (*matchlist) == NULL || (*matchlist)->filename == NULL
      || newmatch == NULL || newmatch->filename == NULL
      || comparef == NULL) nullptr("registerpair()");
  LOUD(fprintf(stderr, "registerpair: '%s', '%s'\n", (*matchlist)->filename->d_name, newmatch->filename->d_name);)

  SETFLAG((*matchlist)->flags, F_HAS_DUPES);
  back = NULL;
  traverse = *matchlist;

  /* FIXME: This needs to be changed! As it currently stands, the compare
   * function only runs on a pair as it is registered and future pairs can
   * mess up the sort order. A separate sorting function should happen before
   * the dupe chain is acted upon rather than while pairs are registered. */
  while (traverse) {
    if (comparef(newmatch, traverse) <= 0) {
      newmatch->duplicates = traverse;

      if (!back) {
        *matchlist = newmatch; /* update pointer to head of list */
        SETFLAG(newmatch->flags, F_HAS_DUPES);
        CLEARFLAG(traverse->flags, F_HAS_DUPES); /* flag is only for first file in dupe chain */
      } else back->duplicates = newmatch;

      break;
    } else {
      if (traverse->duplicates == 0) {
        traverse->duplicates = newmatch;
        if (!back) SETFLAG(traverse->flags, F_HAS_DUPES);

        break;
      }
    }

    back = traverse;
    traverse = traverse->duplicates;
  }
  return;
}


static inline void help_text(void)
{
  printf("Usage: jdupes [options] DIRECTORY...\n\n");

  printf(" -1 --one-file-system \tdo not match files on different filesystems/devices\n");
  printf(" -A --nohidden    \texclude hidden files from consideration\n");
#ifdef ENABLE_BTRFS
  printf(" -B --dedupe      \tSend matches to btrfs for block-level deduplication\n");
#endif
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tdocumentation for additional information\n");
  printf(" -f --omitfirst   \tomit the first file in each set of matches\n");
  printf(" -h --help        \tdisplay this help message\n");
#ifndef NO_HARDLINKS
  printf(" -H --hardlinks   \ttreat any linked files as duplicate files. Normally\n");
  printf("                  \tlinked files are treated as non-duplicates for safety\n");
#endif
  printf(" -i --reverse     \treverse (invert) the match sort order\n");
  printf(" -I --isolate     \tfiles in the same specified directory won't match\n");
#ifndef NO_SYMLINKS
  printf(" -l --linksoft    \tmake relative symlinks for duplicates w/o prompting\n");
#endif
#ifndef NO_HARDLINKS
  printf(" -L --linkhard    \thard link all duplicate files without prompting\n");
 #ifdef ON_WINDOWS
  printf("                  \tWindows allows a maximum of 1023 hard links per file\n");
 #endif /* ON_WINDOWS */
#endif /* NO_HARDLINKS */
  printf(" -m --summarize   \tsummarize dupe information\n");
  //printf(" -n --noempty     \texclude zero-length files from consideration\n");
  printf(" -N --noprompt    \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf(" -O --paramorder  \tParameter order is more important than selected -O sort\n");
  printf("                  \tmtime (BY=time) or filename (BY=name, the default)\n");
#ifndef NO_PERMS
  printf(" -p --permissions \tdon't consider files with different owner/group or\n");
  printf("                  \tpermission bits as duplicates\n");
#endif
  printf(" -r --recurse     \tfor every directory given follow subdirectories\n");
  printf("                  \tencountered within\n");
  printf(" -R --recurse:    \tfor each directory given after this option follow\n");
  printf("                  \tsubdirectories encountered within (note the ':' at\n");
  printf("                  \tthe end of the option, manpage for more details)\n");
#ifndef NO_SYMLINKS
  printf(" -s --symlinks    \tfollow symlinks\n");
#endif
  printf(" -S --size        \tshow size of duplicate files\n");
  printf(" -q --quiet       \thide progress indicator\n");
/* This is undocumented in the quick help because it is a dangerous option. If you
 * really want it, uncomment it here, and may your data rest in peace. */
/*  printf(" -Q --quick       \tskip byte-by-byte duplicate verification. WARNING:\n");
  printf("                  \tthis may delete non-duplicates! Read the manual first!\n"); */
  printf(" -v --version     \tdisplay jdupes version and license information\n");
  printf(" -x --xsize=SIZE  \texclude files of size < SIZE bytes from consideration\n");
  printf("    --xsize=+SIZE \t'+' specified before SIZE, exclude size > SIZE\n");
  printf("                  \tK/M/G size suffixes can be used (case-insensitive)\n");
  printf(" -z --zeromatch   \tconsider zero-length files to be duplicates\n");
  printf(" -Z --softabort   \tIf the user aborts (i.e. CTRL-C) act on matches so far\n");
#ifdef OMIT_GETOPT_LONG
  printf("Note: Long options are not supported in this build.\n\n");
#endif
}


#ifdef UNICODE
int wmain(int argc, wchar_t **wargv)
#else
int main(int argc, char **argv)
#endif
{
  static struct proc_cacheinfo pci;
  static file_t *files = NULL;
  static file_t *curfile;
  static char **oldargv;
  static char *endptr;
  static int firstrecurse;
  static int opt;
  static int pm = 1;
  static ordertype_t ordertype = ORDER_NAME;

#ifndef OMIT_GETOPT_LONG
  static const struct option long_options[] =
  {
    { "loud", 0, 0, '@' },
    { "one-file-system", 0, 0, '1' },
    { "nohidden", 0, 0, 'A' },
    { "dedupe", 0, 0, 'B' },
    { "delete", 0, 0, 'd' },
    { "debug", 0, 0, 'D' },
    { "omitfirst", 0, 0, 'f' },
    { "help", 0, 0, 'h' },
#ifndef NO_HARDLINKS
    { "hardlinks", 0, 0, 'H' },
    { "linkhard", 0, 0, 'L' },
#endif
    { "reverse", 0, 0, 'i' },
    { "isolate", 0, 0, 'I' },
    { "summarize", 0, 0, 'm'},
    { "summary", 0, 0, 'm' },
    { "noempty", 0, 0, 'n' },
    { "noprompt", 0, 0, 'N' },
    { "order", 1, 0, 'o' },
    { "paramorder", 0, 0, 'O' },
#ifndef NO_PERMS
    { "permissions", 0, 0, 'p' },
#endif
    { "quiet", 0, 0, 'q' },
    { "quick", 0, 0, 'Q' },
    { "recurse", 0, 0, 'r' },
    { "recursive", 0, 0, 'r' },
    { "recurse:", 0, 0, 'R' },
    { "recursive:", 0, 0, 'R' },
#ifndef NO_SYMLINKS
    { "linksoft", 0, 0, 'l' },
    { "symlinks", 0, 0, 's' },
#endif
    { "size", 0, 0, 'S' },
    { "version", 0, 0, 'v' },
    { "xsize", 1, 0, 'x' },
    { "zeromatch", 0, 0, 'z' },
    { "softabort", 0, 0, 'Z' },
    { 0, 0, 0, 0 }
  };
#define GETOPT getopt_long
#else
#define GETOPT getopt
#endif

/* Windows buffers our stderr output; don't let it do that */
#ifdef ON_WINDOWS
  if (setvbuf(stderr, NULL, _IONBF, 0) != 0)
    fprintf(stderr, "warning: setvbuf() failed\n");
#endif

#ifdef UNICODE
  /* Create a UTF-8 **argv from the wide version */
  static char **argv;
  argv = (char **)string_malloc(sizeof(char *) * argc);
  if (!argv) oom("main() unicode argv");
  widearg_to_argv(argc, wargv, argv);
  /* Only use UTF-16 for terminal output, else use UTF-8 */
  if (!_isatty(_fileno(stdout))) out_mode = _O_BINARY;
  else out_mode = _O_U16TEXT;
  if (!_isatty(_fileno(stderr))) err_mode = _O_BINARY;
  else err_mode = _O_U16TEXT;
#endif /* UNICODE */

  /* Auto-tune chunk size to be half of L1 data cache if possible */
  get_proc_cacheinfo(&pci);
  if (pci.l1 != 0) auto_chunk_size = (pci.l1 / 2);
  else if (pci.l1d != 0) auto_chunk_size = (pci.l1d / 2);
  /* Must be at least 4096 (4 KiB) and cannot exceed CHUNK_SIZE */
  if (auto_chunk_size < 4096 || auto_chunk_size > CHUNK_SIZE) auto_chunk_size = CHUNK_SIZE;
  /* Force to a multiple of 4096 if it isn't already */
  if ((auto_chunk_size & 0x00000fffUL) != 0)
    auto_chunk_size = (auto_chunk_size + 0x00000fffUL) & 0x000ff000;

  program_name = argv[0];

  oldargv = cloneargs(argc, argv);

  while ((opt = GETOPT(argc, argv,
  "@1ABdDfhHiIlLmnNOpqQrRsSvzZo:x:"
#ifndef OMIT_GETOPT_LONG
          , long_options, NULL
#endif
         )) != EOF) {
    switch (opt) {
    case '1':
      SETFLAG(flags, F_ONEFS);
      break;
    case 'A':
      SETFLAG(flags, F_EXCLUDEHIDDEN);
      break;
    case 'd':
      SETFLAG(flags, F_DELETEFILES);
      break;
    case 'D':
#ifdef DEBUG
      SETFLAG(flags, F_DEBUG);
#endif
      break;
    case 'f':
      SETFLAG(flags, F_OMITFIRST);
      break;
    case 'h':
      help_text();
      string_malloc_destroy();
      exit(EXIT_FAILURE);
#ifndef NO_HARDLINKS
    case 'H':
      SETFLAG(flags, F_CONSIDERHARDLINKS);
      break;
    case 'L':
      SETFLAG(flags, F_HARDLINKFILES);
      break;
#endif
    case 'i':
      SETFLAG(flags, F_REVERSESORT);
      break;
    case 'I':
      SETFLAG(flags, F_ISOLATE);
      break;
    case 'm':
      SETFLAG(flags, F_SUMMARIZEMATCHES);
      break;
    case 'n':
      //fprintf(stderr, "note: -n/--noempty is the default behavior now and is deprecated.\n");
      break;
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      break;
    case 'O':
      SETFLAG(flags, F_USEPARAMORDER);
      break;
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      break;
    case 'q':
      SETFLAG(flags, F_HIDEPROGRESS);
      break;
    case 'Q':
      SETFLAG(flags, F_QUICKCOMPARE);
      break;
    case 'r':
      SETFLAG(flags, F_RECURSE);
      break;
    case 'R':
      SETFLAG(flags, F_RECURSEAFTER);
      break;
#ifndef NO_SYMLINKS
    case 'l':
      SETFLAG(flags, F_MAKESYMLINKS);
      break;
    case 's':
      SETFLAG(flags, F_FOLLOWLINKS);
      break;
#endif
    case 'S':
      SETFLAG(flags, F_SHOWSIZE);
      break;
    case 'z':
      SETFLAG(flags, F_INCLUDEEMPTY);
      break;
    case 'Z':
      SETFLAG(flags, F_SOFTABORT);
      break;
    case 'x':
      SETFLAG(flags, F_EXCLUDESIZE);
      if (*optarg == '+') {
        excludetype = LARGERTHAN;
        optarg++;
      }
      excludesize = (uintmax_t)strtoull(optarg, &endptr, 0);
      switch (*endptr) {
        case 'k':
        case 'K':
          excludesize = excludesize * 1024;
          endptr++;
          break;
        case 'm':
        case 'M':
          excludesize = excludesize * 1024 * 1024;
          endptr++;
          break;
        case 'g':
        case 'G':
          excludesize = excludesize * 1024 * 1024 * 1024;
          endptr++;
          break;
        default:
          break;
      }
      if (*endptr != '\0') {
        fprintf(stderr, "invalid value for --xsize: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case '@':
#ifdef LOUD_DEBUG
      SETFLAG(flags, F_DEBUG | F_LOUD | F_HIDEPROGRESS);
#endif
      break;
    case 'v':
      printf("jdupes %s (%s) ", VER, VERDATE);

      /* Indicate bitness information */
      if (sizeof(uintptr_t) == 8) {
        if (sizeof(long) == 4) printf("64-bit i32\n");
        else if (sizeof(long) == 8) printf("64-bit\n");
      } else if (sizeof(uintptr_t) == 4) {
        if (sizeof(long) == 4) printf("32-bit\n");
        else if (sizeof(long) == 8) printf("32-bit i64\n");
      } else printf("%u-bit i%u\n", (unsigned int)(sizeof(uintptr_t) * 8),
          (unsigned int)(sizeof(long) * 8));

      printf("Compile-time extensions:");
      if (*extensions != NULL) {
        int c = 0;
        while (extensions[c] != NULL) {
          printf(" %s", extensions[c]);
          c++;
        }
      } else printf(" none");
      printf("\nCopyright (C) 2015-2017 by Jody Bruchon\n");
      printf("\nPermission is hereby granted, free of charge, to any person\n");
      printf("obtaining a copy of this software and associated documentation files\n");
      printf("(the \"Software\"), to deal in the Software without restriction,\n");
      printf("including without limitation the rights to use, copy, modify, merge,\n");
      printf("publish, distribute, sublicense, and/or sell copies of the Software,\n");
      printf("and to permit persons to whom the Software is furnished to do so,\n");
      printf("subject to the following conditions:\n\n");
      printf("The above copyright notice and this permission notice shall be\n");
      printf("included in all copies or substantial portions of the Software.\n\n");
      printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS\n");
      printf("OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF\n");
      printf("MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.\n");
      printf("IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY\n");
      printf("CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,\n");
      printf("TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE\n");
      printf("SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.\n");
      exit(EXIT_SUCCESS);
    case 'o':
      if (!strncasecmp("name", optarg, 5)) {
        ordertype = ORDER_NAME;
      } else if (!strncasecmp("time", optarg, 5)) {
        ordertype = ORDER_TIME;
      } else {
        fprintf(stderr, "invalid value for --order: '%s'\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'B':
#ifdef ENABLE_BTRFS
    SETFLAG(flags, F_DEDUPEFILES);
    /* btrfs will do the byte-for-byte check itself */
    SETFLAG(flags, F_QUICKCOMPARE);
    /* It is completely useless to dedupe zero-length extents */
    CLEARFLAG(flags, F_INCLUDEEMPTY);
#else
    fprintf(stderr, "This program was built without btrfs support\n");
    exit(EXIT_FAILURE);
#endif
    break;

    default:
      fprintf(stderr, "Try `jdupes --help' for more information.\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "no directories specified (use -h option for help)\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_ISOLATE) && optind == (argc - 1)) {
    fprintf(stderr, "Isolation requires at least two directories on the command line\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    fprintf(stderr, "options --recurse and --recurse: are not compatible\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_SUMMARIZEMATCHES) && ISFLAG(flags, F_DELETEFILES)) {
    fprintf(stderr, "options --summarize and --delete are not compatible\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

#ifdef ENABLE_BTRFS
  if (ISFLAG(flags, F_CONSIDERHARDLINKS) && ISFLAG(flags, F_DEDUPEFILES))
    fprintf(stderr, "warning: option --dedupe overrides the behavior of --hardlinks\n");
#endif

  /* If pm == 0, call printmatches() */
  pm = !!ISFLAG(flags, F_SUMMARIZEMATCHES) +
      !!ISFLAG(flags, F_DELETEFILES) +
      !!ISFLAG(flags, F_HARDLINKFILES) +
      !!ISFLAG(flags, F_MAKESYMLINKS) +
      !!ISFLAG(flags, F_DEDUPEFILES);

  if (pm > 1) {
      fprintf(stderr, "Only one of --summarize, --delete, --linkhard, --linksoft, or --dedupe\nmay be used\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
  }
  if (pm == 0) SETFLAG(flags, F_PRINTMATCHES);

  if (ISFLAG(flags, F_RECURSEAFTER)) {
    firstrecurse = nonoptafter("--recurse:", argc, oldargv, argv);

    if (firstrecurse == argc)
      firstrecurse = nonoptafter("-R", argc, oldargv, argv);

    if (firstrecurse == argc) {
      fprintf(stderr, "-R option must be isolated from other options\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
    }

    /* F_RECURSE is not set for directories before --recurse: */
    for (int x = optind; x < firstrecurse; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, 0);
      user_dir_count++;
    }

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (int x = firstrecurse; x < argc; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, 1);
      user_dir_count++;
    }
  } else {
    for (int x = optind; x < argc; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, ISFLAG(flags, F_RECURSE));
      user_dir_count++;
    }
  }

  if (ISFLAG(flags, F_REVERSESORT)) sort_direction = -1;
  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\n");
  if (!files) exit(EXIT_SUCCESS);

  curfile = files;
  progress = 0;

  /* Catch CTRL-C */
  signal(SIGINT, sighandler);

  while (curfile) {
    static file_t **match = NULL;
    static FILE *file1;
    static FILE *file2;
#ifdef USE_TREE_REBALANCE
    static unsigned int depth_threshold = INITIAL_DEPTH_THRESHOLD;
#endif

    if (interrupt) {
      fprintf(stderr, "\nStopping file scan due to user abort\n");
      if (!ISFLAG(flags, F_SOFTABORT)) exit(EXIT_FAILURE);
      interrupt = 0;  /* reset interrupt for re-use */
      goto skip_file_scan;
    }

    LOUD(fprintf(stderr, "\nMAIN: current file: %s\n", curfile->filename->d_name));

    if (!checktree) registerfile(&checktree, NONE, curfile);
    else match = checkmatch(checktree, curfile);

#ifdef USE_TREE_REBALANCE
    /* Rebalance the match tree after a certain number of files processed */
    if (max_depth > depth_threshold) {
      rebalance_tree(checktree);
      max_depth = 0;
      if (depth_threshold < 512) depth_threshold <<= 1;
      else depth_threshold += 64;
    }
#endif /* USE_TREE_REBALANCE */

    /* Byte-for-byte check that a matched pair are actually matched */
    if (match != NULL) {
      /* Quick comparison mode will never run confirmmatch()
       * Also skip match confirmation for hard-linked files
       * (This set of comparisons is ugly, but quite efficient) */
      if (ISFLAG(flags, F_QUICKCOMPARE) ||
           (ISFLAG(flags, F_CONSIDERHARDLINKS) &&
           (curfile->inode == (*match)->inode) &&
           (curfile->device == (*match)->device))
         ) {
        LOUD(fprintf(stderr, "MAIN: notice: quick compare match (-Q)\n"));
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
        dupecount++;
        goto skip_full_check;
      }

#ifdef UNICODE
      if (!M2W(curfile->filename->d_name, wstr)) file1 = NULL;
      else file1 = _wfopen(wstr, FILE_MODE_RO);
#else
      file1 = fopen(curfile->filename->d_name, FILE_MODE_RO);
#endif
      if (!file1) {
        curfile = curfile->next;
        continue;
      }

#ifdef UNICODE
      if (!M2W((*match)->file->d_name, wstr)) file2 = NULL;
      else file2 = _wfopen(wstr, FILE_MODE_RO);
#else
      file2 = fopen((*match)->filename->d_name, FILE_MODE_RO);
#endif
      if (!file2) {
        fclose(file1);
        curfile = curfile->next;
        continue;
      }

      if (confirmmatch(file1, file2, curfile->size)) {
        LOUD(fprintf(stderr, "MAIN: registering matched file pair\n"));
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
        dupecount++;
      } DBG(else hash_fail++;)

      fclose(file1);
      fclose(file2);
    }

skip_full_check:
    curfile = curfile->next;

    if (!ISFLAG(flags, F_HIDEPROGRESS)) update_progress(NULL, -1);
    progress++;
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r%60s\r", " ");

skip_file_scan:
  /* Stop catching CTRL+C */
  signal(SIGINT, SIG_DFL);
  if (ISFLAG(flags, F_DELETEFILES)) {
    if (ISFLAG(flags, F_NOPROMPT)) deletefiles(files, 0, 0);
    else deletefiles(files, 1, stdin);
  }
  if (ISFLAG(flags, F_SUMMARIZEMATCHES)) summarizematches(files);
#ifndef NO_SYMLINKS
  if (ISFLAG(flags, F_MAKESYMLINKS)) linkfiles(files, 0);
#endif
#ifndef NO_HARDLINKS
  if (ISFLAG(flags, F_HARDLINKFILES)) linkfiles(files, 1);
#endif /* NO_HARDLINKS */
#ifdef ENABLE_BTRFS
  if (ISFLAG(flags, F_DEDUPEFILES)) dedupefiles(files);
#endif /* ENABLE_BTRFS */
  if (ISFLAG(flags, F_PRINTMATCHES)) printmatches(files);

  string_malloc_destroy();

#ifdef DEBUG
  if (ISFLAG(flags, F_DEBUG)) {
    fprintf(stderr, "\n%d partial (+%d small) -> %d full hash -> %d full (%d partial elim) (%d hash%u fail)\n",
        partial_hash, small_file, full_hash, partial_to_full,
        partial_elim, hash_fail, (unsigned int)sizeof(hash_t)*8);
    fprintf(stderr, "%" PRIuMAX " total files, %" PRIuMAX " comparisons, branch L %u, R %u, both %u\n",
        filecount, comparisons, left_branch, right_branch,
        left_branch + right_branch);
    fprintf(stderr, "Max tree depth: %u; SMA: allocs %" PRIuMAX ", free %" PRIuMAX ", fail %" PRIuMAX ", reuse %" PRIuMAX ", scan %" PRIuMAX ", tails %" PRIuMAX "\n",
        max_depth, sma_allocs, sma_free_good, sma_free_ignored,
        sma_free_reclaimed, sma_free_scanned, sma_free_tails);
    fprintf(stderr, "I/O chunk size: %" PRIuMAX " KiB (%s)\n", (uintmax_t)(auto_chunk_size >> 10),
        (pci.l1 + pci.l1d) != 0 ? "dynamically sized" : "default size");
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
    if (ISFLAG(flags, F_HARDLINKFILES))
      fprintf(stderr, "Exclusions based on Windows hard link limit: %u\n", hll_exclude);
 #endif
#endif
  }
#endif /* DEBUG */

  exit(EXIT_SUCCESS);
}
