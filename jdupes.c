/* jdupes (C) 2015-2021 Jody Bruchon <jody@jodybruchon.com>
   Forked from fdupes 1.51 (C) 1999-2014 Adrian Lopez

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
#include <strings.h>
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
#include <time.h>
#include <sys/time.h>
#include "jdupes.h"
#include "xxhash.h"
#include "oom.h"
#ifdef ENABLE_DEDUPE
#include <sys/utsname.h>
#endif

/* Jody Bruchon's helpful functions */
#include "string_malloc.h"
#include "jody_sort.h"
#include "jody_win_unicode.h"
#include "jody_cacheinfo.h"
#include "jody_strtoepoch.h"
#include "version.h"

/* Headers for post-scanning actions */
#include "act_deletefiles.h"
#include "act_dedupefiles.h"
#include "act_linkfiles.h"
#include "act_printmatches.h"
#include "act_printjson.h"
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
static wpath_t wname, wstr;
int out_mode = _O_TEXT;
int err_mode = _O_TEXT;
#endif /* UNICODE */

#ifndef NO_SYMLINKS
#include "jody_paths.h"
#endif

/* Behavior modification flags (a=action, p=-P) */
uint_fast32_t flags = 0, a_flags = 0, p_flags = 0;

static const char *program_name;

/* Stat and SIGUSR */
#ifdef ON_WINDOWS
 struct winstat s;
#else
 struct stat s;
 static int usr1_toggle = 0;
#endif

/* Larger chunk size makes large files process faster but uses more RAM */
#define MIN_CHUNK_SIZE 4096
#define MAX_CHUNK_SIZE 16777216
#ifndef CHUNK_SIZE
 #define CHUNK_SIZE 65536
#endif
#ifndef PARTIAL_HASH_SIZE
 #define PARTIAL_HASH_SIZE 4096
#endif

static size_t auto_chunk_size = CHUNK_SIZE;

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

/* Size suffixes - this gets exported */
const struct size_suffix size_suffix[] = {
  /* Byte (someone may actually try to use this) */
  { "b", 1 },
  { "k", 1024 },
  { "kib", 1024 },
  { "m", 1048576 },
  { "mib", 1048576 },
  { "g", (uint64_t)1048576 * 1024 },
  { "gib", (uint64_t)1048576 * 1024 },
  { "t", (uint64_t)1048576 * 1048576 },
  { "tib", (uint64_t)1048576 * 1048576 },
  { "p", (uint64_t)1048576 * 1048576 * 1024},
  { "pib", (uint64_t)1048576 * 1048576 * 1024},
  { "e", (uint64_t)1048576 * 1048576 * 1048576},
  { "eib", (uint64_t)1048576 * 1048576 * 1048576},
  /* Decimal suffixes */
  { "kb", 1000 },
  { "mb", 1000000 },
  { "gb", 1000000000 },
  { "tb", 1000000000000 },
  { "pb", 1000000000000000 },
  { "eb", 1000000000000000000 },
  { NULL, 0 },
};

/* Assemble extension string from compile-time options */
const char *extensions[] = {
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
    #ifdef ENABLE_DEDUPE
    "fsdedup",
    #endif
    #ifdef LOW_MEMORY
    "lowmem",
    #endif
    #ifdef SMA_PAGE_SIZE
    "smapage",
    #endif
    #ifdef NO_PERMS
    "noperm",
    #endif
    #ifdef NO_HARDLINKS
    "nohardlink",
    #endif
    #ifdef NO_SYMLINKS
    "nosymlink",
    #endif
    #ifdef NO_USER_ORDER
    "nouserorder",
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

/* Extended filter tree head and static tag list */
struct extfilter *extfilter_head = NULL;
const struct extfilter_tags extfilter_tags[] = {
  { "noext",	XF_EXCL_EXT },
  { "onlyext",	XF_ONLY_EXT },
  { "size+",	XF_SIZE_GT },
  { "size-",	XF_SIZE_LT },
  { "size+=",	XF_SIZE_GTEQ },
  { "size-=",	XF_SIZE_LTEQ },
  { "size=",	XF_SIZE_EQ },
  { "nostr",	XF_EXCL_STR },
  { "onlystr",	XF_ONLY_STR },
  { "newer",	XF_DATE_NEWER },
  { "older",	XF_DATE_OLDER },
  { NULL, 0 },
};

/* Required for progress indicator code */
static uintmax_t filecount = 0;
static uintmax_t progress = 0, item_progress = 0, dupecount = 0;
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

/* File tree head */
static filetree_t *checktree = NULL;

/* Directory/file parameter position counter */
static unsigned int user_item_count = 1;

/* registerfile() direction options */
enum tree_direction { NONE, LEFT, RIGHT };

/* Sort order reversal */
static int sort_direction = 1;

/* Signal handler */
static int interrupt = 0;

/* Progress indicator time */
struct timeval time1, time2;

/* For path name mangling */
char tempname[PATHBUF_SIZE * 2];

/* Compare two hashes like memcmp() */
#define HASH_COMPARE(a,b) ((a > b) ? 1:((a == b) ? 0:-1))

static void help_text_extfilter(void);

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


#ifndef ON_WINDOWS
void sigusr1(const int signum)
{
  (void)signum;
  if (!ISFLAG(flags, F_SOFTABORT)) {
    SETFLAG(flags, F_SOFTABORT);
    usr1_toggle = 1;
  } else {
    CLEARFLAG(flags, F_SOFTABORT);
    usr1_toggle = 2;
  }
  return;
}
#endif


/* De-allocate on exit */
void clean_exit(void)
{
#ifndef SMA_PASSTHROUGH
  string_malloc_destroy();
#endif
  return;
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
#ifndef ON_WINDOWS
  /* Notify of change to soft abort status if SIGUSR1 received */
  if (usr1_toggle != 0) {
    fprintf(stderr, "\njdupes received a USR1 signal; soft abort (-Z) is now %s\n", usr1_toggle == 1 ? "ON" : "OFF" );
    usr1_toggle = 0;
  }
#endif
  return;
}


/***** Add new functions here *****/


/* Does a file have one of these comma-separated extensions?
 * Returns 1 after any match, 0 if no matches */
int match_extensions(char *path, const char *extlist)
{
  char *dot;
  const char *ext;
  size_t len, extlen;

  LOUD(fprintf(stderr, "match_extensions('%s', '%s')\n", path, extlist);)
  if (path == NULL || extlist == NULL) nullptr("match_extensions");

  dot = NULL;
  /* Scan to end of path, save the last dot, reset on path separators */
  while (*path != '\0') {
    if (*path == '.') dot = path;
    if (*path == '/' || *path == '\\') dot = NULL;
    path++;
  }
  /* No dots in the file name = no extension, so give up now */
  if (dot == NULL) return 0;
  dot++;
  /* Handle a dot at the end of a file name */
  if (*dot == '\0') return 0;

  /* Get the length of the file's extension for later checking */
  extlen = strlen(dot);
  LOUD(fprintf(stderr, "match_extensions: file has extension '%s' with length %ld\n", dot, extlen);)

  /* dot is now at the location of the last file extension; check the list */
  /* Skip any commas at the start of the list */
  while (*extlist == ',') extlist++;
  ext = extlist;
  len = 0;
  while (1) {
    /* Reject upon hitting the end with no more extensions to process */
    if (*extlist == '\0' && len == 0) return 0;
    /* Process extension once a comma or EOL is hit */
    if (*extlist == ',' || *extlist == '\0') {
      /* Skip serial commas */
      while (*extlist == ',') extlist++;
      if (extlist == ext)  goto skip_empty;
      if (strncasecmp(dot, ext, len) == 0 && extlen == len) {
        LOUD(fprintf(stderr, "match_extensions: matched on extension '%s' (len %ld)\n", dot, len);)
        return 1;
      }
      LOUD(fprintf(stderr, "match_extensions: no match: '%s' (%ld), '%s' (%ld)\n", dot, len, ext, extlen);)
skip_empty:
      ext = extlist;
      len = 0;
      continue;
    }
    extlist++; len++;
    /* LOUD(fprintf(stderr, "match_extensions: DEBUG: '%s' : '%s' (%ld), '%s' (%ld)\n", extlist, dot, len, ext, extlen);) */
  }
  return 0;
}


/* Check file's stat() info to make sure nothing has changed
 * Returns 1 if changed, 0 if not changed, negative if error */
extern int file_has_changed(file_t * const restrict file)
{
  /* If -t/--nochangecheck specified then completely bypass this code */
  if (ISFLAG(flags, F_NOCHANGECHECK)) return 0;

  if (file == NULL || file->d_name == NULL) nullptr("file_has_changed()");
  LOUD(fprintf(stderr, "file_has_changed('%s')\n", file->d_name);)

  if (!ISFLAG(file->flags, FF_VALID_STAT)) return -66;

  if (STAT(file->d_name, &s) != 0) return -2;
  if (file->inode != s.st_ino) return 1;
  if (file->size != s.st_size) return 1;
  if (file->device != s.st_dev) return 1;
  if (file->mtime != s.st_mtime) return 1;
  if (file->mode != s.st_mode) return 1;
#ifndef NO_PERMS
  if (file->uid != s.st_uid) return 1;
  if (file->gid != s.st_gid) return 1;
#endif
#ifndef NO_SYMLINKS
  if (lstat(file->d_name, &s) != 0) return -3;
  if ((S_ISLNK(s.st_mode) > 0) ^ ISFLAG(file->flags, FF_IS_SYMLINK)) return 1;
#endif

  return 0;
}


extern inline int getfilestats(file_t * const restrict file)
{
  if (file == NULL || file->d_name == NULL) nullptr("getfilestats()");
  LOUD(fprintf(stderr, "getfilestats('%s')\n", file->d_name);)

  /* Don't stat the same file more than once */
  if (ISFLAG(file->flags, FF_VALID_STAT)) return 0;
  SETFLAG(file->flags, FF_VALID_STAT);

  if (STAT(file->d_name, &s) != 0) return -1;
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
  if (lstat(file->d_name, &s) != 0) return -1;
  if (S_ISLNK(s.st_mode) > 0) SETFLAG(file->flags, FF_IS_SYMLINK);
#endif
  return 0;
}


static void add_extfilter(const char *option)
{
  char *opt, *p;
  time_t tt;
  struct extfilter *extf = extfilter_head;
  const struct extfilter_tags *tags = extfilter_tags;
  const struct size_suffix *ss = size_suffix;

  if (option == NULL) nullptr("add_extfilter()");

  LOUD(fprintf(stderr, "add_extfilter '%s'\n", option);)

  /* Invoke help text if requested */
  if (strcasecmp(option, "help") == 0) { help_text_extfilter(); exit(EXIT_SUCCESS); }

  opt = string_malloc(strlen(option) + 1);
  if (opt == NULL) oom("add_extfilter option");
  strcpy(opt, option);
  p = opt;

  while (*p != ':' && *p != '\0') p++;

  /* Split tag string into *opt (tag) and *p (value) */
  if (*p == ':') {
    *p = '\0';
    p++;
  }

  while (tags->tag != NULL && strcmp(tags->tag, opt) != 0) tags++;
  if (tags->tag == NULL) goto error_bad_filter;

  /* Check for a tag that requires a value */
  if (tags->flags & XF_REQ_VALUE && *p == '\0') goto error_value_missing;

  /* *p is now at the value, NOT the tag string! */

  if (extfilter_head != NULL) {
    /* Add to end of exclusion stack if head is present */
    while (extf->next != NULL) extf = extf->next;
    extf->next = string_malloc(sizeof(struct extfilter) + strlen(p) + 1);
    if (extf->next == NULL) oom("add_extfilter alloc");
    extf = extf->next;
  } else {
    /* Allocate extfilter_head if no exclusions exist yet */
    extfilter_head = string_malloc(sizeof(struct extfilter) + strlen(p) + 1);
    if (extfilter_head == NULL) oom("add_extfilter alloc");
    extf = extfilter_head;
  }

  /* Set tag value from predefined tag array */
  extf->flags = tags->flags;

  /* Initialize the new extfilter element */
  extf->next = NULL;
  if (extf->flags & XF_REQ_NUMBER) {
    /* Exclude uses a number; handle it with possible suffixes */
    *(extf->param) = '\0';
    /* Get base size */
    if (*p < '0' || *p > '9') goto error_bad_size_suffix;
    extf->size = strtoll(p, &p, 10);
    /* Handle suffix, if any */
    if (*p != '\0') {
      while (ss->suffix != NULL && strcasecmp(ss->suffix, p) != 0) ss++;
      if (ss->suffix == NULL) goto error_bad_size_suffix;
      extf->size *= ss->multiplier;
    }
  } else if (extf->flags & XF_REQ_DATE) {
    *(extf->param) = '\0';
    tt = strtoepoch(p);
    LOUD(fprintf(stderr, "extfilter: jody_strtoepoch: '%s' -> %ld\n", p, tt);)
    if (tt == -1) goto error_bad_time;
    extf->size = tt;
  } else {
    /* Exclude uses string data; just copy it */
    extf->size = 0;
    if (*p != '\0') strcpy(extf->param, p);
    else *(extf->param) = '\0';
  }

  LOUD(fprintf(stderr, "Added extfilter: tag '%s', data '%s', size %lld, flags %d\n", opt, extf->param, (long long)extf->size, extf->flags);)
  string_free(opt);
  return;

error_bad_time:
  fprintf(stderr, "Invalid extfilter date[time] was specified: -X filter:datetime\n");
  help_text_extfilter();
  exit(EXIT_FAILURE);
error_value_missing:
  fprintf(stderr, "extfilter value missing or invalid: -X filter:value\n");
  help_text_extfilter();
  exit(EXIT_FAILURE);
error_bad_filter:
  fprintf(stderr, "Invalid extfilter filter name was specified\n");
  help_text_extfilter();
  exit(EXIT_FAILURE);
error_bad_size_suffix:
  fprintf(stderr, "Invalid extfilter size suffix specified; use B or KMGTPE[i][B]\n");
  help_text_extfilter();
  exit(EXIT_FAILURE);
}


/* Returns -1 if stat() fails, 0 if it's a directory, 1 if it's not */
extern int getdirstats(const char * const restrict name,
        jdupes_ino_t * const restrict inode, dev_t * const restrict dev,
        jdupes_mode_t * const restrict mode)
{
  if (name == NULL || inode == NULL || dev == NULL) nullptr("getdirstats");
  LOUD(fprintf(stderr, "getdirstats('%s', %p, %p)\n", name, (void *)inode, (void *)dev);)

  if (STAT(name, &s) != 0) return -1;
  *inode = s.st_ino;
  *dev = s.st_dev;
  *mode = s.st_mode;
  if (!S_ISDIR(s.st_mode)) return 1;
  return 0;
}


/* Check a pair of files for match exclusion conditions
 * Returns:
 *  0 if all condition checks pass
 * -1 or 1 on compare result less/more
 * -2 on an absolute exclusion condition met
 *  2 on an absolute match condition met
 * -3 on exclusion due to isolation
 * -4 on exclusion due to same filesystem
 * -5 on exclusion due to permissions */
extern int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2)
{
  if (file1 == NULL || file2 == NULL || file1->d_name == NULL || file2->d_name == NULL) nullptr("check_conditions()");

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
  }
#endif

  /* Fall through: all checks passed */
  LOUD(fprintf(stderr, "check_conditions: all condition checks passed\n"));
  return 0;
}


/* Check for exclusion conditions for a single file (1 = fail) */
static int check_singlefile(file_t * const restrict newfile)
{
  char * restrict tp = tempname;
  int excluded;

  if (newfile == NULL) nullptr("check_singlefile()");

  LOUD(fprintf(stderr, "check_singlefile: checking '%s'\n", newfile->d_name));

  /* Exclude hidden files if requested */
  if (ISFLAG(flags, F_EXCLUDEHIDDEN)) {
    if (newfile->d_name == NULL) nullptr("check_singlefile newfile->d_name");
    strcpy(tp, newfile->d_name);
    tp = basename(tp);
    if (tp[0] == '.' && strcmp(tp, ".") && strcmp(tp, "..")) {
      LOUD(fprintf(stderr, "check_singlefile: excluding hidden file (-A on)\n"));
      return 1;
    }
  }

  /* Get file information and check for validity */
  const int i = getfilestats(newfile);
  if (i || newfile->size == -1) {
    LOUD(fprintf(stderr, "check_singlefile: excluding due to bad stat()\n"));
    return 1;
  }

  if (!S_ISDIR(newfile->mode)) {
    /* Exclude zero-length files if requested */
    if (newfile->size == 0 && !ISFLAG(flags, F_INCLUDEEMPTY)) {
    LOUD(fprintf(stderr, "check_singlefile: excluding zero-length empty file (-z not set)\n"));
    return 1;
  }

    /* Exclude files based on exclusion stack size specs */
    excluded = 0;
    for (struct extfilter *extf = extfilter_head; extf != NULL; extf = extf->next) {
      uint32_t sflag = extf->flags;
      LOUD(fprintf(stderr, "check_singlefile: extfilter check: %08x %ld %ld %s\n", sflag, newfile->size, extf->size, newfile->d_name);)
      if (
           /* Any line that passes will result in file exclusion */
           ((sflag == XF_SIZE_EQ) && (newfile->size != extf->size)) ||
           ((sflag == XF_SIZE_LTEQ) && (newfile->size > extf->size)) ||
           ((sflag == XF_SIZE_GTEQ) && (newfile->size < extf->size)) ||
           ((sflag == XF_SIZE_GT) && (newfile->size <= extf->size)) ||
           ((sflag == XF_SIZE_LT) && (newfile->size >= extf->size)) ||
           ((sflag == XF_EXCL_EXT) && match_extensions(newfile->d_name, extf->param)) ||
           ((sflag == XF_ONLY_EXT) && !match_extensions(newfile->d_name, extf->param)) ||
           ((sflag == XF_EXCL_STR) && strstr(newfile->d_name, extf->param)) ||
           ((sflag == XF_ONLY_STR) && !strstr(newfile->d_name, extf->param)) ||
           ((sflag == XF_DATE_NEWER) && (newfile->mtime < extf->size)) ||
           ((sflag == XF_DATE_OLDER) && (newfile->mtime >= extf->size))
      ) excluded = 1;
    }
    if (excluded) {
      LOUD(fprintf(stderr, "check_singlefile: excluding based on an extfilter option\n"));
      return 1;
    }
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


static file_t *init_newfile(const size_t len, file_t * restrict * const restrict filelistp)
{
  file_t * const restrict newfile = (file_t *)string_malloc(sizeof(file_t));

  if (!newfile) oom("init_newfile() file structure");
  if (!filelistp) nullptr("init_newfile() filelistp");

  LOUD(fprintf(stderr, "init_newfile(len %lu, filelistp %p)\n", len, filelistp));

  memset(newfile, 0, sizeof(file_t));
  newfile->d_name = (char *)string_malloc(len);
  if (!newfile->d_name) oom("init_newfile() filename");

  newfile->next = *filelistp;
#ifndef NO_USER_ORDER
  newfile->user_order = user_item_count;
#endif
  newfile->size = -1;
  newfile->duplicates = NULL;
  return newfile;
}


/* Create a new traversal check object and initialize its values */
static struct travdone *travdone_alloc(const dev_t device, const jdupes_ino_t inode)
{
  struct travdone *trav;

  LOUD(fprintf(stderr, "travdone_alloc(%" PRIdMAX ", %" PRIdMAX ")\n", (intmax_t)inode, (intmax_t)device);)

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


/* De-allocate the travdone tree */
static void travdone_free(struct travdone * const restrict cur)
{
  LOUD(fprintf(stderr, "travdone_free(%p)\n", cur);)
  if (cur == NULL) return;
  if (cur->left != NULL) travdone_free(cur->left);
  if (cur->right != NULL) travdone_free(cur->right);
  string_free(cur);
  return;
}


/* Check to see if device:inode pair has already been traversed */
static int traverse_check(const dev_t device, const jdupes_ino_t inode)
{
  struct travdone *traverse = travdone_head;

  if (travdone_head == NULL) {
    travdone_head = travdone_alloc(device, inode);
    if (travdone_head == NULL) return 2;
  } else {
    traverse = travdone_head;
    while (1) {
      if (traverse == NULL) nullptr("traverse_check()");
      /* Don't re-traverse directories we've already seen */
      if (inode == traverse->inode && device == traverse->device) {
        LOUD(fprintf(stderr, "traverse_check: already seen: %ld:%ld\n", device,inode);)
        return 1;
      } else if (inode > traverse->inode || (inode == traverse->inode && device > traverse->device)) {
        /* Traverse right */
        if (traverse->right == NULL) {
          LOUD(fprintf(stderr, "traverse item right: %ld:%ld\n", device, inode);)
          traverse->right = travdone_alloc(device, inode);
          if (traverse->right == NULL) return 2;
          break;
        }
        traverse = traverse->right;
        continue;
      } else {
        /* Traverse left */
        if (traverse->left == NULL) {
          LOUD(fprintf(stderr, "traverse item left %ld,%ld\n", device, inode);)
          traverse->left = travdone_alloc(device, inode);
          if (traverse->left == NULL) return 2;
          break;
        }
        traverse = traverse->left;
        continue;
      }
    }
  }
  return 0;
}


/* This is disabled until a check is in place to make it safe */
#if 0
/* Add a single file to the file tree */
static inline file_t *grokfile(const char * const restrict name, file_t * restrict * const restrict filelistp)
{
  file_t * restrict newfile;

  if (!name || !filelistp) nullptr("grokfile()");
  LOUD(fprintf(stderr, "grokfile: '%s' %p\n", name, filelistp));

  /* Allocate the file_t and the d_name entries */
  newfile = init_newfile(strlen(name) + 2, filelistp);

  strcpy(newfile->d_name, name);

  /* Single-file [l]stat() and exclusion condition check */
  if (check_singlefile(newfile) != 0) {
    LOUD(fprintf(stderr, "grokfile: check_singlefile rejected file\n"));
    string_free(newfile->d_name);
    string_free(newfile);
    return NULL;
  }
  return newfile;
}
#endif


/* Load a directory's contents into the file tree, recursing as needed */
static void grokdir(const char * const restrict dir,
                file_t * restrict * const restrict filelistp,
                int recurse)
{
  file_t * restrict newfile;
  struct dirent *dirinfo;
  static int grokdir_level = 0;
  size_t dirlen;
  int i, single = 0;
  jdupes_ino_t inode;
  dev_t device, n_device;
  jdupes_mode_t mode;
#ifdef UNICODE
  WIN32_FIND_DATA ffd;
  HANDLE hFind = INVALID_HANDLE_VALUE;
  char *p;
#else
  DIR *cd;
#endif

  if (dir == NULL || filelistp == NULL) nullptr("grokdir()");
  LOUD(fprintf(stderr, "grokdir: scanning '%s' (order %d, recurse %d)\n", dir, user_item_count, recurse));

  /* Get directory stats (or file stats if it's a file) */
  i = getdirstats(dir, &inode, &device, &mode);
  if (i < 0) goto error_travdone;
  /* if dir is actually a file, just add it to the file tree */
  if (i == 1) {
/* Single file addition is disabled for now because there is no safeguard
 * against the file being compared against itself if it's added in both a
 * recursion and explicitly on the command line. */
#if 0
    LOUD(fprintf(stderr, "grokdir -> grokfile '%s'\n", dir));
    newfile = grokfile(dir, filelistp);
    if (newfile == NULL) {
      LOUD(fprintf(stderr, "grokfile rejected '%s'\n", dir));
      return;
    }
    single = 1;
    goto add_single_file;
#endif
    fprintf(stderr, "\nFile specs on command line disabled in this version for safety\n");
    fprintf(stderr, "This should be restored (and safe) in a future release\n");
    fprintf(stderr, "See https://github.com/jbruchon/jdupes or email jody@jodybruchon.com\n");
    return; /* Remove when single file is restored */
  }

  /* Double traversal prevention tree */
  if (!ISFLAG(flags, F_NOTRAVCHECK)) {
    i = traverse_check(device, inode);
    if (i == 1) return;
    if (i == 2) goto error_travdone;
  }

  item_progress++;
  grokdir_level++;

#ifdef UNICODE
  /* Windows requires \* at the end of directory names */
  strncpy(tempname, dir, PATHBUF_SIZE * 2 - 1);
  dirlen = strlen(tempname) - 1;
  p = tempname + dirlen;
  if (*p == '/' || *p == '\\') *p = '\0';
  strncat(tempname, "\\*", PATHBUF_SIZE * 2 - 1);

  if (!M2W(tempname, wname)) goto error_cd;

  LOUD(fprintf(stderr, "FindFirstFile: %s\n", dir));
  hFind = FindFirstFileW(wname, &ffd);
  if (hFind == INVALID_HANDLE_VALUE) { LOUD(fprintf(stderr, "\nfile handle bad\n")); goto error_cd; }
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
    if (!strcmp(dirinfo->d_name, ".") || !strcmp(dirinfo->d_name, "..")) continue;
    if (!ISFLAG(flags, F_HIDEPROGRESS)) {
      gettimeofday(&time2, NULL);
      if (progress == 0 || time2.tv_sec > time1.tv_sec) {
        fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " dirs (in %u specified)",
            progress, item_progress, user_item_count);
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

    /* Allocate the file_t and the d_name entries */
    newfile = init_newfile(dirlen + d_name_len + 2, filelistp);

    tp = tempname;
    memcpy(newfile->d_name, tp, dirlen + d_name_len);

    /*** WARNING: tempname global gets reused by check_singlefile here! ***/

    /* Single-file [l]stat() and exclusion condition check */
    if (check_singlefile(newfile) != 0) {
      LOUD(fprintf(stderr, "grokdir: check_singlefile rejected file\n"));
      string_free(newfile->d_name);
      string_free(newfile);
      continue;
    }

    /* Optionally recurse directories, including symlinked ones if requested */
    if (S_ISDIR(newfile->mode)) {
      if (recurse) {
        /* --one-file-system - WARNING: this clobbers inode/mode */
        if (ISFLAG(flags, F_ONEFS)
            && (getdirstats(newfile->d_name, &inode, &n_device, &mode) == 0)
            && (device != n_device)) {
          LOUD(fprintf(stderr, "grokdir: directory: not recursing (--one-file-system)\n"));
          string_free(newfile->d_name);
          string_free(newfile);
          continue;
        }
#ifndef NO_SYMLINKS
        else if (ISFLAG(flags, F_FOLLOWLINKS) || !ISFLAG(newfile->flags, FF_IS_SYMLINK)) {
          LOUD(fprintf(stderr, "grokdir: directory(symlink): recursing (-r/-R)\n"));
          grokdir(newfile->d_name, filelistp, recurse);
        }
#else
        else {
          LOUD(fprintf(stderr, "grokdir: directory: recursing (-r/-R)\n"));
          grokdir(newfile->d_name, filelistp, recurse);
        }
#endif
      } else { LOUD(fprintf(stderr, "grokdir: directory: not recursing\n")); }
      string_free(newfile->d_name);
      string_free(newfile);
      continue;
    } else {
//add_single_file:
      /* Add regular files to list, including symlink targets if requested */
#ifndef NO_SYMLINKS
      if (!ISFLAG(newfile->flags, FF_IS_SYMLINK) || (ISFLAG(newfile->flags, FF_IS_SYMLINK) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
      if (S_ISREG(newfile->mode)) {
#endif
        *filelistp = newfile;
        filecount++;
        progress++;

      } else {
        LOUD(fprintf(stderr, "grokdir: not a regular file: %s\n", newfile->d_name);)
        string_free(newfile->d_name);
        string_free(newfile);
        if (single == 1) {
          single = 0;
          goto skip_single;
        }
        continue;
      }
    }
    /* Skip directory stuff if adding only a single file */
    if (single == 1) {
      single = 0;
      goto skip_single;
    }
  }

#ifdef UNICODE
  while (FindNextFileW(hFind, &ffd) != 0);
  FindClose(hFind);
#else
  closedir(cd);
#endif

skip_single:
  grokdir_level--;
  if (grokdir_level == 0 && !ISFLAG(flags, F_HIDEPROGRESS)) {
    fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " items (in %u specified)",
            progress, item_progress, user_item_count);
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


/* Hash part or all of a file */
static jdupes_hash_t *get_filehash(const file_t * const restrict checkfile,
                const size_t max_read)
{
  off_t fsize;
  /* This is an array because we return a pointer to it */
  static jdupes_hash_t hash[1];
  static jdupes_hash_t *chunk = NULL;
  FILE *file;
  int check = 0;
  XXH64_state_t *xxhstate;

  if (checkfile == NULL || checkfile->d_name == NULL) nullptr("get_filehash()");
  LOUD(fprintf(stderr, "get_filehash('%s', %" PRIdMAX ")\n", checkfile->d_name, (intmax_t)max_read);)

  /* Allocate on first use */
  if (chunk == NULL) {
    chunk = (jdupes_hash_t *)string_malloc(auto_chunk_size);
    if (!chunk) oom("get_filehash() chunk");
  }

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
   */

  *hash = 0;
  if (ISFLAG(checkfile->flags, FF_HASH_PARTIAL)) {
    *hash = checkfile->filehash_partial;
    /* Don't bother going further if max_read is already fulfilled */
    if (max_read != 0 && max_read <= PARTIAL_HASH_SIZE) {
      LOUD(fprintf(stderr, "Partial hash size (%d) >= max_read (%" PRIuMAX "), not hashing anymore\n", PARTIAL_HASH_SIZE, (uintmax_t)max_read);)
      return hash;
    }
  }
  errno = 0;
#ifdef UNICODE
  if (!M2W(checkfile->d_name, wstr)) file = NULL;
  else file = _wfopen(wstr, FILE_MODE_RO);
#else
  file = fopen(checkfile->d_name, FILE_MODE_RO);
#endif
  if (file == NULL) {
    fprintf(stderr, "\n%s error opening file ", strerror(errno)); fwprint(stderr, checkfile->d_name, 1);
    return NULL;
  }
  /* Actually seek past the first chunk if applicable
   * This is part of the filehash_partial skip optimization */
  if (ISFLAG(checkfile->flags, FF_HASH_PARTIAL)) {
    if (fseeko(file, PARTIAL_HASH_SIZE, SEEK_SET) == -1) {
      fclose(file);
      fprintf(stderr, "\nerror seeking in file "); fwprint(stderr, checkfile->d_name, 1);
      return NULL;
    }
    fsize -= PARTIAL_HASH_SIZE;
  }

  xxhstate = XXH64_createState();
  if (xxhstate == NULL) nullptr("xxhstate");
  XXH64_reset(xxhstate, 0);

  /* Read the file in CHUNK_SIZE chunks until we've read it all. */
  while (fsize > 0) {
    size_t bytes_to_read;

    if (interrupt) return 0;
    bytes_to_read = (fsize >= (off_t)auto_chunk_size) ? auto_chunk_size : (size_t)fsize;
    if (fread((void *)chunk, bytes_to_read, 1, file) != 1) {
      fprintf(stderr, "\nerror reading from file "); fwprint(stderr, checkfile->d_name, 1);
      fclose(file);
      return NULL;
    }

    XXH64_update(xxhstate, chunk, bytes_to_read);

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

  *hash = XXH64_digest(xxhstate);
  XXH64_freeState(xxhstate);

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

  return;
}


#ifdef TREE_DEPTH_STATS
#define TREE_DEPTH_UPDATE_MAX() { if (max_depth < tree_depth) max_depth = tree_depth; tree_depth = 0; }
#else
#define TREE_DEPTH_UPDATE_MAX()
#endif


/* Check two files for a match */
static file_t **checkmatch(filetree_t * restrict tree, file_t * const restrict file)
{
  int cmpresult = 0;
  int cantmatch = 0;
  const jdupes_hash_t * restrict filehash;

  if (tree == NULL || file == NULL || tree->file == NULL || tree->file->d_name == NULL || file->d_name == NULL) nullptr("checkmatch()");
  LOUD(fprintf(stderr, "checkmatch ('%s', '%s')\n", tree->file->d_name, file->d_name));

  /* If device and inode fields are equal one of the files is a
   * hard link to the other or the files have been listed twice
   * unintentionally. We don't want to flag these files as
   * duplicates unless the user specifies otherwise. */

  /* Count the total number of comparisons requested */
  DBG(comparisons++;)

/* If considering hard linked files as duplicates, they are
 * automatically duplicates without being read further since
 * they point to the exact same inode. If we aren't considering
 * hard links as duplicates, we just return NULL. */

  cmpresult = check_conditions(tree->file, file);
  switch (cmpresult) {
    case 2: return &tree->file;  /* linked files + -H switch */
    case -2: return NULL;  /* linked files, no -H switch */
    case -3:    /* user order */
    case -4:    /* one filesystem */
    case -5:    /* permissions */
        cantmatch = 1;
        cmpresult = 0;
        break;
    default: break;
  }

  /* Print pre-check (early) match candidates if requested */
  if (ISFLAG(p_flags, PF_EARLYMATCH)) printf("Early match check passed:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

  /* If preliminary matching succeeded, do main file data checks */
  if (cmpresult == 0) {
    LOUD(fprintf(stderr, "checkmatch: starting file data comparisons\n"));
    /* Attempt to exclude files quickly with partial file hashing */
    if (!ISFLAG(tree->file->flags, FF_HASH_PARTIAL)) {
      filehash = get_filehash(tree->file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) return NULL;

      tree->file->filehash_partial = *filehash;
      SETFLAG(tree->file->flags, FF_HASH_PARTIAL);
    }

    if (!ISFLAG(file->flags, FF_HASH_PARTIAL)) {
      filehash = get_filehash(file, PARTIAL_HASH_SIZE);
      if (filehash == NULL) return NULL;

      file->filehash_partial = *filehash;
      SETFLAG(file->flags, FF_HASH_PARTIAL);
    }

    cmpresult = HASH_COMPARE(file->filehash_partial, tree->file->filehash_partial);
    LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: partial hashes match\n"));
    LOUD(if (cmpresult) fprintf(stderr, "checkmatch: partial hashes do not match\n"));
    DBG(partial_hash++;)

    /* Print partial hash matching pairs if requested */
    if (cmpresult == 0 && ISFLAG(p_flags, PF_PARTIAL))
      printf("\nPartial hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

    if (file->size <= PARTIAL_HASH_SIZE || ISFLAG(flags, F_PARTIALONLY)) {
      if (ISFLAG(flags, F_PARTIALONLY)) { LOUD(fprintf(stderr, "checkmatch: partial only mode: treating partial hash as full hash\n")); }
      else { LOUD(fprintf(stderr, "checkmatch: small file: copying partial hash to full hash\n")); }
      /* filehash_partial = filehash if file is small enough */
      if (!ISFLAG(file->flags, FF_HASH_FULL)) {
        file->filehash = file->filehash_partial;
        SETFLAG(file->flags, FF_HASH_FULL);
        DBG(small_file++;)
      }
      if (!ISFLAG(tree->file->flags, FF_HASH_FULL)) {
        tree->file->filehash = tree->file->filehash_partial;
        SETFLAG(tree->file->flags, FF_HASH_FULL);
        DBG(small_file++;)
      }
    } else if (cmpresult == 0) {
//      if (ISFLAG(flags, F_SKIPHASH)) {
//        LOUD(fprintf(stderr, "checkmatch: skipping full file hashes (F_SKIPMATCH)\n"));
//      } else {
        /* If partial match was correct, perform a full file hash match */
        if (!ISFLAG(tree->file->flags, FF_HASH_FULL)) {
          filehash = get_filehash(tree->file, 0);
          if (filehash == NULL) return NULL;

          tree->file->filehash = *filehash;
          SETFLAG(tree->file->flags, FF_HASH_FULL);
        }

        if (!ISFLAG(file->flags, FF_HASH_FULL)) {
          filehash = get_filehash(file, 0);
          if (filehash == NULL) return NULL;

          file->filehash = *filehash;
          SETFLAG(file->flags, FF_HASH_FULL);
        }

        /* Full file hash comparison */
        cmpresult = HASH_COMPARE(file->filehash, tree->file->filehash);
        LOUD(if (!cmpresult) fprintf(stderr, "checkmatch: full hashes match\n"));
        LOUD(if (cmpresult) fprintf(stderr, "checkmatch: full hashes do not match\n"));
        DBG(full_hash++);
//      }
    } else {
      DBG(partial_elim++);
    }
  }  /* if (cmpresult == 0) */

  if ((cantmatch != 0) && (cmpresult == 0)) {
    LOUD(fprintf(stderr, "checkmatch: rejecting because match not allowed (cantmatch = 1)\n"));
    cmpresult = -1;
  }

  /* How the file tree works
   *
   * The tree is sorted by size as files arrive. If the files are the same
   * size, they are possible duplicates and are checked for duplication.
   * If they are not a match, the hashes are used to decide whether to
   * continue with the file to the left or the right in the file tree.
   * If the direction decision points to a leaf node, the duplicate scan
   * continues down that path; if it points to an empty node, the current
   * file is attached to the file tree at that point.
   *
   * This allows for quickly finding files of the same size by avoiding
   * tree branches with differing size groups.
   */
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
    if (ISFLAG(p_flags, PF_FULLHASH)) printf("Full hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);
    return &tree->file;
  }
  /* Fall through - should never be reached */
  return NULL;
}


/* Do a byte-by-byte comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */
static inline int confirmmatch(FILE * const restrict file1, FILE * const restrict file2, const off_t size)
{
  static char *c1 = NULL, *c2 = NULL;
  size_t r1, r2;
  off_t bytes = 0;
  int check = 0;

  if (file1 == NULL || file2 == NULL) nullptr("confirmmatch()");
  LOUD(fprintf(stderr, "confirmmatch running\n"));

  /* Allocate on first use; OOM if either is ever NULLed */
  if (!c1) {
    c1 = (char *)string_malloc(auto_chunk_size);
    c2 = (char *)string_malloc(auto_chunk_size);
  }
  if (!c1 || !c2) oom("confirmmatch() c1/c2");

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
    if (ISFLAG(files->flags, FF_HAS_DUPES)) {
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


#ifndef NO_USER_ORDER
static int sort_pairs_by_param_order(file_t *f1, file_t *f2)
{
  if (!ISFLAG(flags, F_USEPARAMORDER)) return 0;
  if (f1 == NULL || f2 == NULL) nullptr("sort_pairs_by_param_order()");
  if (f1->user_order < f2->user_order) return -sort_direction;
  if (f1->user_order > f2->user_order) return sort_direction;
  return 0;
}
#endif


static int sort_pairs_by_mtime(file_t *f1, file_t *f2)
{
  if (f1 == NULL || f2 == NULL) nullptr("sort_pairs_by_mtime()");

#ifndef NO_USER_ORDER
  int po = sort_pairs_by_param_order(f1, f2);
  if (po != 0) return po;
#endif /* NO_USER_ORDER */

  if (f1->mtime < f2->mtime) return -sort_direction;
  else if (f1->mtime > f2->mtime) return sort_direction;

  /* If the mtimes match, use the names to break the tie */
  return numeric_sort(f1->d_name, f2->d_name, sort_direction);
}


static int sort_pairs_by_filename(file_t *f1, file_t *f2)
{
  if (f1 == NULL || f2 == NULL) nullptr("sort_pairs_by_filename()");

#ifndef NO_USER_ORDER
  int po = sort_pairs_by_param_order(f1, f2);
  if (po != 0) return po;
#endif /* NO_USER_ORDER */

  return numeric_sort(f1->d_name, f2->d_name, sort_direction);
}


static void registerpair(file_t **matchlist, file_t *newmatch,
                int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  /* NULL pointer sanity checks */
  if (matchlist == NULL || newmatch == NULL || comparef == NULL) nullptr("registerpair()");
  LOUD(fprintf(stderr, "registerpair: '%s', '%s'\n", (*matchlist)->d_name, newmatch->d_name);)

  SETFLAG((*matchlist)->flags, FF_HAS_DUPES);
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
        SETFLAG(newmatch->flags, FF_HAS_DUPES);
        CLEARFLAG(traverse->flags, FF_HAS_DUPES); /* flag is only for first file in dupe chain */
      } else back->duplicates = newmatch;

      break;
    } else {
      if (traverse->duplicates == 0) {
        traverse->duplicates = newmatch;
        if (!back) SETFLAG(traverse->flags, FF_HAS_DUPES);

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
  printf("Usage: jdupes [options] FILES and/or DIRECTORIES...\n\n");

  printf("Duplicate file sets will be printed by default unless a different action\n");
  printf("option is specified (delete, summarize, link, dedupe, etc.)\n");
#ifdef LOUD
  printf(" -@ --loud        \toutput annoying low-level debug info while running\n");
#endif
  printf(" -0 --print-null  \toutput nulls instead of CR/LF (like 'find -print0')\n");
  printf(" -1 --one-file-system\tdo not match files on different filesystems/devices\n");
  printf(" -A --no-hidden    \texclude hidden files from consideration\n");
#ifdef ENABLE_DEDUPE
  printf(" -B --dedupe      \tdo a copy-on-write (reflink/clone) deduplication\n");
#endif
  printf(" -C --chunk-size=#\toverride I/O chunk size (min %d, max %d)\n", MIN_CHUNK_SIZE, MAX_CHUNK_SIZE);
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tdocumentation for additional information\n");
#ifdef DEBUG
  printf(" -D --debug       \toutput debug statistics after completion\n");
#endif
  printf(" -f --omit-first  \tomit the first file in each set of matches\n");
  printf(" -h --help        \tdisplay this help message\n");
#ifndef NO_HARDLINKS
  printf(" -H --hard-links  \ttreat any linked files as duplicate files. Normally\n");
  printf("                  \tlinked files are treated as non-duplicates for safety\n");
#endif
  printf(" -i --reverse     \treverse (invert) the match sort order\n");
#ifndef NO_USER_ORDER
  printf(" -I --isolate     \tfiles in the same specified directory won't match\n");
#endif
  printf(" -j --json        \tproduce JSON (machine-readable) output\n");
/*  printf(" -K --skip-hash   \tskip full file hashing (may be faster; 100%% safe)\n");
    printf("                  \tWARNING: in development, not fully working yet!\n"); */
#ifndef NO_SYMLINKS
  printf(" -l --link-soft    \tmake relative symlinks for duplicates w/o prompting\n");
#endif
#ifndef NO_HARDLINKS
  printf(" -L --link-hard    \thard link all duplicate files without prompting\n");
 #ifdef ON_WINDOWS
  printf("                  \tWindows allows a maximum of 1023 hard links per file;\n");
  printf("                  \tlinking large match sets will result in multiple sets\n");
  printf("                  \tof hard linked files due to this limit.\n");
 #endif /* ON_WINDOWS */
#endif /* NO_HARDLINKS */
  printf(" -m --summarize   \tsummarize dupe information\n");
  printf(" -M --print-summarize\tprint match sets and --summarize at the end\n");
  printf(" -N --no-prompt   \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf("                  \tmtime (BY=time) or filename (BY=name, the default)\n");
#ifndef NO_USER_ORDER
  printf(" -O --param-order  \tParameter order is more important than selected -o sort\n");
#endif
#ifndef NO_PERMS
  printf(" -p --permissions \tdon't consider files with different owner/group or\n");
  printf("                  \tpermission bits as duplicates\n");
#endif
  printf(" -P --print=type  \tprint extra info (partial, early, fullhash)\n");
  printf(" -q --quiet       \thide progress indicator\n");
  printf(" -Q --quick       \tskip byte-for-byte confirmation for quick matching\n");
  printf("                  \tWARNING: -Q can result in data loss! Be very careful!\n");
  printf(" -r --recurse     \tfor every directory, process its subdirectories too\n");
  printf(" -R --recurse:    \tfor each directory given after this option follow\n");
  printf("                  \tsubdirectories encountered within (note the ':' at\n");
  printf("                  \tthe end of the option, manpage for more details)\n");
#ifndef NO_SYMLINKS
  printf(" -s --symlinks    \tfollow symlinks\n");
#endif
  printf(" -S --size        \tshow size of duplicate files\n");
  printf(" -t --no-change-check\tdisable security check for file changes (aka TOCTTOU)\n");
  printf(" -T --partial-only \tmatch based on partial hashes only. WARNING:\n");
  printf("                  \tEXTREMELY DANGEROUS paired with destructive actions!\n");
  printf("                  \t-T must be specified twice to work. Read the manual!\n");
  printf(" -u --print-unique\tprint only a list of unique (non-matched) files\n");
  printf(" -U --no-trav-check\tdisable double-traversal safety check (BE VERY CAREFUL)\n");
  printf("                  \tThis fixes a Google Drive File Stream recursion issue\n");
  printf(" -v --version     \tdisplay jdupes version and license information\n");
  printf(" -X --ext-filter=x:y\tfilter files based on specified criteria\n");
  printf("                  \tUse '-X help' for detailed extfilter help\n");
  printf(" -z --zero-match  \tconsider zero-length files to be duplicates\n");
  printf(" -Z --soft-abort  \tIf the user aborts (i.e. CTRL-C) act on matches so far\n");
#ifndef ON_WINDOWS
  printf("                  \tYou can send SIGUSR1 to the program to toggle this\n");
#endif
#ifdef OMIT_GETOPT_LONG
  printf("Note: Long options are not supported in this build.\n\n");
#endif
}


static void help_text_extfilter(void)
{
  printf("Detailed help for jdupes -X/--ext-filter options\n");
  printf("General format: jdupes -X filter[:value][size_suffix]\n\n");

  /* FIXME: Remove after v1.19.0 */
  printf("****** WARNING: THE MEANINGS HAVE CHANGED IN v1.19.0 - READ CAREFULLY ******\n\n");

  printf("noext:ext1[,ext2,...]   \tExclude files with certain extension(s)\n\n");
  printf("onlyext:ext1[,ext2,...] \tOnly include files with certain extension(s)\n\n");
  printf("size[+-=]:size[suffix]  \tOnly Include files matching size criteria\n");
  printf("                        \tSize specs: + larger, - smaller, = equal to\n");
  printf("                        \tSpecs can be mixed, i.e. size+=:100k will\n");
  printf("                        \tonly include files 100KiB or more in size.\n\n");
  printf("nostr:text_string       \tExclude all paths containing the string\n");
  printf("onlystr:text_string     \tOnly allow paths containing the string\n");
  printf("                        \tHINT: you can use these for directories:\n");
  printf("                        \t-X nostr:/dir_x/  or  -X onlystr:/dir_x/\n");
  printf("newer:datetime          \tOnly include files newer than specified date\n");
  printf("older:datetime          \tOnly include files older than specified date\n");
  printf("                        \tDate/time format: \"YYYY-MM-DD HH:MM:SS\"\n");
  printf("                        \tTime is optional (remember to escape spaces!)\n");
/*  printf("\t\n"); */

  printf("\nSome filters take no value or multiple values. Filters that can take\n");
  printf(  "a numeric option generally support the size multipliers K/M/G/T/P/E\n");
  printf(  "with or without an added iB or B. Multipliers are binary-style unless\n");
  printf(  "the -B suffix is used, which will use decimal multipliers. For example,\n");
  printf(  "16k or 16kib = 16384; 16kb = 16000. Multipliers are case-insensitive.\n\n");

  printf(  "Filters have cumulative effects: jdupes -X size+:99 -X size-:101 will\n");
  printf(  "cause only files of exactly 100 bytes in size to be included.\n\n");

  printf(  "Extension matching is case-insensitive.\n");
  printf(  "Path substring matching is case-sensitive.\n");
}


#ifdef UNICODE
int wmain(int argc, wchar_t **wargv)
#else
int main(int argc, char **argv)
#endif
{
  static file_t *files = NULL;
  static file_t *curfile;
  static char **oldargv;
  static int firstrecurse;
  static int opt;
  static int pm = 1;
  static int partialonly_spec = 0;
  static ordertype_t ordertype = ORDER_NAME;
  static long manual_chunk_size = 0;
#ifdef __linux__
  static struct proc_cacheinfo pci;
#endif
#ifdef ENABLE_DEDUPE
  static struct utsname utsname;
#endif

#ifndef OMIT_GETOPT_LONG
  static const struct option long_options[] =
  {
    { "loud", 0, 0, '@' },
    { "printnull", 0, 0, '0' }, //LEGACY
    { "print-null", 0, 0, '0' },
    { "one-file-system", 0, 0, '1' },
    { "nohidden", 0, 0, 'A' }, //LEGACY
    { "no-hidden", 0, 0, 'A' },
    { "dedupe", 0, 0, 'B' },
    { "chunksize", 1, 0, 'C' }, //LEGACY
    { "chunk-size", 1, 0, 'C' },
    { "debug", 0, 0, 'D' },
    { "delete", 0, 0, 'd' },
    { "omitfirst", 0, 0, 'f' }, //LEGACY
    { "omit-first", 0, 0, 'f' },
    { "hardlinks", 0, 0, 'H' }, //LEGACY
    { "hard-links", 0, 0, 'H' },
    { "help", 0, 0, 'h' },
    { "isolate", 0, 0, 'I' },
    { "reverse", 0, 0, 'i' },
    { "json", 0, 0, 'j' },
/*    { "skip-hash", 0, 0, 'K' }, */
    { "linkhard", 0, 0, 'L' }, //LEGACY
    { "link-hard", 0, 0, 'L' },
    { "linksoft", 0, 0, 'l' }, //LEGACY
    { "link-soft", 0, 0, 'l' },
    { "printwithsummary", 0, 0, 'M'}, //LEGACY
    { "print-summarize", 0, 0, 'M'},
    { "summarize", 0, 0, 'm'},
    { "noprompt", 0, 0, 'N' }, //LEGACY
    { "no-prompt", 0, 0, 'N' },
    { "paramorder", 0, 0, 'O' }, //LEGACY
    { "param-order", 0, 0, 'O' },
    { "order", 1, 0, 'o' },
    { "print", 1, 0, 'P' },
    { "permissions", 0, 0, 'p' },
    { "quick", 0, 0, 'Q' },
    { "quiet", 0, 0, 'q' },
    { "recurse:", 0, 0, 'R' },
    { "recurse", 0, 0, 'r' },
    { "size", 0, 0, 'S' },
    { "symlinks", 0, 0, 's' },
    { "partial-only", 0, 0, 'T' },
    { "nochangecheck", 0, 0, 't' }, //LEGACY
    { "no-change-check", 0, 0, 't' },
    { "notravcheck", 0, 0, 'U' }, //LEGACY
    { "no-trav-check", 0, 0, 'U' },
    { "printunique", 0, 0, 'u' }, //LEGACY
    { "print-unique", 0, 0, 'u' },
    { "version", 0, 0, 'v' },
    { "extfilter", 1, 0, 'X' }, //LEGACY
    { "ext-filter", 1, 0, 'X' },
    { "softabort", 0, 0, 'Z' }, //LEGACY
    { "soft-abort", 0, 0, 'Z' },
    { "zeromatch", 0, 0, 'z' }, //LEGACY
    { "zero-match", 0, 0, 'z' },
    { NULL, 0, 0, 0 }
  };
#define GETOPT getopt_long
#else
#define GETOPT getopt
#endif

#define GETOPT_STRING "@01ABC:DdfHhIijKLlMmNnOo:P:pQqRrSsTtUuVvX:Zz"

/* Windows buffers our stderr output; don't let it do that */
#ifdef ON_WINDOWS
  if (setvbuf(stderr, NULL, _IONBF, 0) != 0)
    fprintf(stderr, "warning: setvbuf() failed\n");
#endif

#ifdef UNICODE
  /* Create a UTF-8 **argv from the wide version */
  static char **argv;
  argv = (char **)string_malloc(sizeof(char *) * (size_t)argc);
  if (!argv) oom("main() unicode argv");
  widearg_to_argv(argc, wargv, argv);
  /* fix up __argv so getopt etc. don't crash */
  __argv = argv;
  /* Only use UTF-16 for terminal output, else use UTF-8 */
  if (!_isatty(_fileno(stdout))) out_mode = _O_BINARY;
  else out_mode = _O_U16TEXT;
  if (!_isatty(_fileno(stderr))) err_mode = _O_BINARY;
  else err_mode = _O_U16TEXT;
#endif /* UNICODE */

#ifdef __linux__
  /* Auto-tune chunk size to be half of L1 data cache if possible */
  get_proc_cacheinfo(&pci);
  if (pci.l1 != 0) auto_chunk_size = (pci.l1 / 2);
  else if (pci.l1d != 0) auto_chunk_size = (pci.l1d / 2);
  /* Must be at least 4096 (4 KiB) and cannot exceed CHUNK_SIZE */
  if (auto_chunk_size < MIN_CHUNK_SIZE || auto_chunk_size > MAX_CHUNK_SIZE) auto_chunk_size = CHUNK_SIZE;
  /* Force to a multiple of 4096 if it isn't already */
  if ((auto_chunk_size & 0x00000fffUL) != 0)
    auto_chunk_size = (auto_chunk_size + 0x00000fffUL) & 0x000ff000;
#endif /* __linux__ */

  /* Is stderr a terminal? If not, we won't write progress to it */
#ifdef ON_WINDOWS
  if (!_isatty(_fileno(stderr))) SETFLAG(flags, F_HIDEPROGRESS);
#else
  if (!isatty(fileno(stderr))) SETFLAG(flags, F_HIDEPROGRESS);
#endif

  program_name = argv[0];
  oldargv = cloneargs(argc, argv);
  /* Clean up string_malloc on any exit */
  atexit(clean_exit);

  while ((opt = GETOPT(argc, argv, GETOPT_STRING
#ifndef OMIT_GETOPT_LONG
          , long_options, NULL
#endif
         )) != EOF) {
    if ((uintptr_t)optarg == 0x20) goto error_optarg;
    switch (opt) {
    case '0':
      SETFLAG(a_flags, FA_PRINTNULL);
      LOUD(fprintf(stderr, "opt: print null instead of newline (--printnull)\n");)
      break;
    case '1':
      SETFLAG(flags, F_ONEFS);
      LOUD(fprintf(stderr, "opt: recursion across filesystems disabled (--onefs)\n");)
      break;
    case 'A':
      SETFLAG(flags, F_EXCLUDEHIDDEN);
      break;
    case 'C':
      manual_chunk_size = strtol(optarg, NULL, 10) & 0x0ffff000L;  /* Align to 4K sizes */
      if (manual_chunk_size < MIN_CHUNK_SIZE || manual_chunk_size > MAX_CHUNK_SIZE) {
        fprintf(stderr, "warning: invalid manual chunk size (must be %d-%d); using defaults\n", MIN_CHUNK_SIZE, MAX_CHUNK_SIZE);
        LOUD(fprintf(stderr, "Manual chunk size (failed) was apparently '%s' => %ld\n", optarg, manual_chunk_size));
        manual_chunk_size = 0;
      } else auto_chunk_size = (size_t)manual_chunk_size;
      LOUD(fprintf(stderr, "Manual chunk size is %ld\n", manual_chunk_size));
      break;
    case 'd':
      SETFLAG(a_flags, FA_DELETEFILES);
      LOUD(fprintf(stderr, "opt: delete files after matching (--deletefiles)\n");)
      break;
    case 'D':
#ifdef DEBUG
      SETFLAG(flags, F_DEBUG);
#endif
      break;
    case 'f':
      SETFLAG(a_flags, FA_OMITFIRST);
      LOUD(fprintf(stderr, "opt: omit first match from each match set (--omitfirst)\n");)
      break;
    case 'h':
      help_text();
      string_malloc_destroy();
      exit(EXIT_FAILURE);
#ifndef NO_HARDLINKS
    case 'H':
      SETFLAG(flags, F_CONSIDERHARDLINKS);
      LOUD(fprintf(stderr, "opt: hard links count as matches (--hardlinks)\n");)
      break;
    case 'L':
      SETFLAG(a_flags, FA_HARDLINKFILES);
      LOUD(fprintf(stderr, "opt: convert duplicates to hard links (--linkhard)\n");)
      break;
#endif
    case 'i':
      SETFLAG(flags, F_REVERSESORT);
      LOUD(fprintf(stderr, "opt: sort order reversal enabled (--reverse)\n");)
      break;
#ifndef NO_USER_ORDER
    case 'I':
      SETFLAG(flags, F_ISOLATE);
      LOUD(fprintf(stderr, "opt: intra-parameter match isolation enabled (--isolate)\n");)
      break;
    case 'O':
      SETFLAG(flags, F_USEPARAMORDER);
      LOUD(fprintf(stderr, "opt: parameter order takes precedence (--paramorder)\n");)
      break;
#else
    case 'I':
    case 'O':
      fprintf(stderr, "warning: -I and -O are disabled and ignored in this build\n");
      break;
#endif
    case 'j':
      SETFLAG(a_flags, FA_PRINTJSON);
      LOUD(fprintf(stderr, "opt: print output in JSON format (--printjson)\n");)
      break;
    case 'K':
      SETFLAG(flags, F_SKIPHASH);
      break;
    case 'm':
      SETFLAG(a_flags, FA_SUMMARIZEMATCHES);
      LOUD(fprintf(stderr, "opt: print a summary of match stats (--summarize)\n");)
      break;
    case 'M':
      SETFLAG(a_flags, FA_SUMMARIZEMATCHES);
      SETFLAG(a_flags, FA_PRINTMATCHES);
      LOUD(fprintf(stderr, "opt: print matches with a summary (--printwithsummary)\n");)
      break;
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      LOUD(fprintf(stderr, "opt: delete files without prompting (--noprompt)\n");)
      break;
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      LOUD(fprintf(stderr, "opt: permissions must also match (--permissions)\n");)
      break;
    case 'P':
      LOUD(fprintf(stderr, "opt: print early: '%s' (--print)\n", optarg);)
      if (strcmp(optarg, "partial") == 0) SETFLAG(p_flags, PF_PARTIAL);
      else if (strcmp(optarg, "early") == 0) SETFLAG(p_flags, PF_EARLYMATCH);
      else if (strcmp(optarg, "fullhash") == 0) SETFLAG(p_flags, PF_FULLHASH);
      else {
        fprintf(stderr, "Option '%s' is not valid for -P\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'q':
      SETFLAG(flags, F_HIDEPROGRESS);
      break;
    case 'Q':
      SETFLAG(flags, F_QUICKCOMPARE);
      fprintf(stderr, "\nBIG FAT WARNING: -Q/--quick MAY BE DANGEROUS! Read the manual!\n\n");
      LOUD(fprintf(stderr, "opt: byte-for-byte safety check disabled (--quick)\n");)
      break;
    case 'r':
      SETFLAG(flags, F_RECURSE);
      LOUD(fprintf(stderr, "opt: global recursion enabled (--recurse)\n");)
      break;
    case 'R':
      SETFLAG(flags, F_RECURSEAFTER);
      LOUD(fprintf(stderr, "opt: partial recursion enabled (--recurseafter)\n");)
      break;
    case 't':
      SETFLAG(flags, F_NOCHANGECHECK);
      LOUD(fprintf(stderr, "opt: TOCTTOU safety check disabled (--nochangecheck)\n");)
      break;
    case 'T':
      if (partialonly_spec == 0)
        partialonly_spec = 1;
      else {
        partialonly_spec = 2;
        fprintf(stderr, "\nBIG FAT WARNING: -T/--partialonly is EXTREMELY DANGEROUS! Read the manual!\n\n");
        SETFLAG(flags, F_PARTIALONLY);
      }
      break;
    case 'u':
      SETFLAG(a_flags, FA_PRINTUNIQUE);
      LOUD(fprintf(stderr, "opt: print only non-matched (unique) files (--printunique)\n");)
      break;
    case 'U':
      SETFLAG(flags, F_NOTRAVCHECK);
      LOUD(fprintf(stderr, "opt: double-traversal safety check disabled (--notravcheck)\n");)
      break;
#ifndef NO_SYMLINKS
    case 'l':
      SETFLAG(a_flags, FA_MAKESYMLINKS);
      LOUD(fprintf(stderr, "opt: convert duplicates to symbolic links (--linksoft)\n");)
      break;
    case 's':
      SETFLAG(flags, F_FOLLOWLINKS);
      LOUD(fprintf(stderr, "opt: follow symbolic links enabled (--symlinks)\n");)
      break;
#endif
    case 'S':
      SETFLAG(a_flags, FA_SHOWSIZE);
      LOUD(fprintf(stderr, "opt: show size of files enabled (--size)\n");)
      break;
    case 'X':
      add_extfilter(optarg);
      break;
    case 'z':
      SETFLAG(flags, F_INCLUDEEMPTY);
      LOUD(fprintf(stderr, "opt: zero-length files count as matches (--zeromatch)\n");)
      break;
    case 'Z':
      SETFLAG(flags, F_SOFTABORT);
      LOUD(fprintf(stderr, "opt: soft-abort mode enabled (--softabort)\n");)
      break;
    case '@':
#ifdef LOUD_DEBUG
      SETFLAG(flags, F_DEBUG | F_LOUD | F_HIDEPROGRESS);
#endif
      LOUD(fprintf(stderr, "opt: loud debugging enabled, hope you can handle it (--loud)\n");)
      break;
    case 'v':
    case 'V':
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
      printf("\nCopyright (C) 2015-2021 by Jody Bruchon and contributors\n");
      printf("Forked from fdupes 1.51, (C) 1999-2014 Adrian Lopez and contributors\n\n");
      printf("Permission is hereby granted, free of charge, to any person obtaining a copy of\n");
      printf("this software and associated documentation files (the \"Software\"), to deal in\n");
      printf("the Software without restriction, including without limitation the rights to\n");
      printf("use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies\n");
      printf("of the Software, and to permit persons to whom the Software is furnished to do\n");
      printf("so, subject to the following conditions:\n\n");

      printf("The above copyright notice and this permission notice shall be included in all\n");
      printf("copies or substantial portions of the Software.\n\n");
      printf("THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n");
      printf("IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n");
      printf("FITNESS FOR A PARTICULAR PURPOSE, AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n");
      printf("AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n");
      printf("LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,\n");
      printf("OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE\n");
      printf("SOFTWARE.\n");
      printf("\nIf you find this software useful, please consider financially supporting\n");
      printf("its development through the author's home page: https://www.jodybruchon.com/\n");
      printf("\nReport bugs and get the latest releases: https://github.com/jbruchon/jdupes\n");
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
#ifdef ENABLE_DEDUPE
      /* Refuse to dedupe on 2.x kernels; they could damage user data */
      if (uname(&utsname)) {
        fprintf(stderr, "Failed to get kernel version! Aborting.\n");
        exit(EXIT_FAILURE);
      }
      LOUD(fprintf(stderr, "dedupefiles: uname got release '%s'\n", utsname.release));
      if (*(utsname.release) == '2' && *(utsname.release + 1) == '.') {
        fprintf(stderr, "Refusing to dedupe on a 2.x kernel; data loss could occur. Aborting.\n");
        exit(EXIT_FAILURE);
      }
      SETFLAG(a_flags, FA_DEDUPEFILES);
      /* btrfs will do the byte-for-byte check itself */
      SETFLAG(flags, F_QUICKCOMPARE);
      /* It is completely useless to dedupe zero-length extents */
      CLEARFLAG(flags, F_INCLUDEEMPTY);
#else
      fprintf(stderr, "This program was built without dedupe support\n");
      exit(EXIT_FAILURE);
#endif
      LOUD(fprintf(stderr, "opt: CoW/block-level deduplication enabled (--dedupe)\n");)
      break;

    default:
      if (opt != '?') fprintf(stderr, "Sorry, using '-%c' is not supported in this build.\n", opt);
      fprintf(stderr, "Try `jdupes --help' for more information.\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "no files or directories specified (use -h option for help)\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (partialonly_spec == 1) {
    fprintf(stderr, "--partial-only specified only once (it's VERY DANGEROUS, read the manual!)\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_PARTIALONLY) && ISFLAG(flags, F_QUICKCOMPARE)) {
    fprintf(stderr, "--partial-only overrides --quick and is even more dangerous (read the manual!)\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    fprintf(stderr, "options --recurse and --recurse: are not compatible\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(a_flags, FA_SUMMARIZEMATCHES) && ISFLAG(a_flags, FA_DELETEFILES)) {
    fprintf(stderr, "options --summarize and --delete are not compatible\n");
    string_malloc_destroy();
    exit(EXIT_FAILURE);
  }

#ifdef ENABLE_DEDUPE
  if (ISFLAG(flags, F_CONSIDERHARDLINKS) && ISFLAG(a_flags, FA_DEDUPEFILES))
    fprintf(stderr, "warning: option --dedupe overrides the behavior of --hardlinks\n");
#endif

  /* If pm == 0, call printmatches() */
  pm = !!ISFLAG(a_flags, FA_SUMMARIZEMATCHES) +
      !!ISFLAG(a_flags, FA_DELETEFILES) +
      !!ISFLAG(a_flags, FA_HARDLINKFILES) +
      !!ISFLAG(a_flags, FA_MAKESYMLINKS) +
      !!ISFLAG(a_flags, FA_PRINTJSON) +
      !!ISFLAG(a_flags, FA_PRINTUNIQUE) +
      !!ISFLAG(a_flags, FA_DEDUPEFILES);

  if (pm > 1) {
      fprintf(stderr, "Only one of --summarize, --printwithsummary, --delete, --linkhard,\n--linksoft, --json, or --dedupe may be used\n");
      string_malloc_destroy();
      exit(EXIT_FAILURE);
  }
  if (pm == 0) SETFLAG(a_flags, FA_PRINTMATCHES);

#ifndef ON_WINDOWS
  /* Catch SIGUSR1 and use it to enable -Z */
  signal(SIGUSR1, sigusr1);
#endif

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
      user_item_count++;
    }

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (int x = firstrecurse; x < argc; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, 1);
      user_item_count++;
    }
  } else {
    for (int x = optind; x < argc; x++) {
      slash_convert(argv[x]);
      grokdir(argv[x], &files, ISFLAG(flags, F_RECURSE));
      user_item_count++;
    }
  }

  /* We don't need the double traversal check tree anymore */
  travdone_free(travdone_head);

  if (ISFLAG(flags, F_REVERSESORT)) sort_direction = -1;
  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\n");
  if (!files) {
    fwprint(stderr, "No duplicates found.", 1);
    string_malloc_destroy();
    exit(EXIT_SUCCESS);
  }

  curfile = files;
  progress = 0;

  /* Catch CTRL-C */
  signal(SIGINT, sighandler);

  while (curfile) {
    static file_t **match = NULL;
    static FILE *file1;
    static FILE *file2;

    if (interrupt) {
      fprintf(stderr, "\nStopping file scan due to user abort\n");
      if (!ISFLAG(flags, F_SOFTABORT)) exit(EXIT_FAILURE);
      interrupt = 0;  /* reset interrupt for re-use */
      goto skip_file_scan;
    }

    LOUD(fprintf(stderr, "\nMAIN: current file: %s\n", curfile->d_name));

    if (!checktree) registerfile(&checktree, NONE, curfile);
    else match = checkmatch(checktree, curfile);

    /* Byte-for-byte check that a matched pair are actually matched */
    if (match != NULL) {
      /* Quick or partial-only compare will never run confirmmatch()
       * Also skip match confirmation for hard-linked files
       * (This set of comparisons is ugly, but quite efficient) */
      if (ISFLAG(flags, F_QUICKCOMPARE) || ISFLAG(flags, F_PARTIALONLY) ||
           (ISFLAG(flags, F_CONSIDERHARDLINKS) &&
           (curfile->inode == (*match)->inode) &&
           (curfile->device == (*match)->device))
         ) {
        LOUD(fprintf(stderr, "MAIN: notice: hard linked, quick, or partial-only match (-H/-Q/-T)\n"));
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
        dupecount++;
        goto skip_full_check;
      }

#ifdef UNICODE
      if (!M2W(curfile->d_name, wstr)) file1 = NULL;
      else file1 = _wfopen(wstr, FILE_MODE_RO);
#else
      file1 = fopen(curfile->d_name, FILE_MODE_RO);
#endif
      if (!file1) {
        LOUD(fprintf(stderr, "MAIN: warning: file1 fopen() failed ('%s')\n", curfile->d_name));
        curfile = curfile->next;
        continue;
      }

#ifdef UNICODE
      if (!M2W((*match)->d_name, wstr)) file2 = NULL;
      else file2 = _wfopen(wstr, FILE_MODE_RO);
#else
      file2 = fopen((*match)->d_name, FILE_MODE_RO);
#endif
      if (!file2) {
        fclose(file1);
        LOUD(fprintf(stderr, "MAIN: warning: file2 fopen() failed ('%s')\n", (*match)->d_name));
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
  if (ISFLAG(a_flags, FA_DELETEFILES)) {
    if (ISFLAG(flags, F_NOPROMPT)) deletefiles(files, 0, 0);
    else deletefiles(files, 1, stdin);
  }
#ifndef NO_SYMLINKS
  if (ISFLAG(a_flags, FA_MAKESYMLINKS)) linkfiles(files, 0);
#endif
#ifndef NO_HARDLINKS
  if (ISFLAG(a_flags, FA_HARDLINKFILES)) linkfiles(files, 1);
#endif /* NO_HARDLINKS */
#ifdef ENABLE_DEDUPE
  if (ISFLAG(a_flags, FA_DEDUPEFILES)) dedupefiles(files);
#endif /* ENABLE_DEDUPE */
  if (ISFLAG(a_flags, FA_PRINTMATCHES)) printmatches(files);
  if (ISFLAG(a_flags, FA_PRINTUNIQUE)) printunique(files);
  if (ISFLAG(a_flags, FA_PRINTJSON)) printjson(files, argc, argv);
  if (ISFLAG(a_flags, FA_SUMMARIZEMATCHES)) {
    if (ISFLAG(a_flags, FA_PRINTMATCHES)) printf("\n\n");
    summarizematches(files);
  }

  string_malloc_destroy();

#ifdef DEBUG
  if (ISFLAG(flags, F_DEBUG)) {
    fprintf(stderr, "\n%d partial (+%d small) -> %d full hash -> %d full (%d partial elim) (%d hash%u fail)\n",
        partial_hash, small_file, full_hash, partial_to_full,
        partial_elim, hash_fail, (unsigned int)sizeof(jdupes_hash_t)*8);
    fprintf(stderr, "%" PRIuMAX " total files, %" PRIuMAX " comparisons, branch L %u, R %u, both %u, max tree depth %u\n",
        filecount, comparisons, left_branch, right_branch,
        left_branch + right_branch, max_depth);
    fprintf(stderr, "SMA: allocs %" PRIuMAX ", free %" PRIuMAX " (merge %" PRIuMAX ", repl %" PRIuMAX "), fail %" PRIuMAX ", reuse %" PRIuMAX ", scan %" PRIuMAX ", tails %" PRIuMAX "\n",
        sma_allocs, sma_free_good, sma_free_merged, sma_free_replaced,
        sma_free_ignored, sma_free_reclaimed,
        sma_free_scanned, sma_free_tails);
    if (manual_chunk_size > 0) fprintf(stderr, "I/O chunk size: %ld KiB (manually set)\n", manual_chunk_size >> 10);
    else {
#ifdef __linux__
      fprintf(stderr, "I/O chunk size: %" PRIuMAX " KiB (%s)\n", (uintmax_t)(auto_chunk_size >> 10), (pci.l1 + pci.l1d) != 0 ? "dynamically sized" : "default size");
#else
      fprintf(stderr, "I/O chunk size: %" PRIuMAX " KiB (default size)\n", (uintmax_t)(auto_chunk_size >> 10));
#endif /* __linux__ */
    }
#ifdef ON_WINDOWS
 #ifndef NO_HARDLINKS
    if (ISFLAG(a_flags, FA_HARDLINKFILES))
      fprintf(stderr, "Exclusions based on Windows hard link limit: %u\n", hll_exclude);
 #endif
#endif
  }
#endif /* DEBUG */

  exit(EXIT_SUCCESS);

error_optarg:
  fprintf(stderr, "error: option '%c' requires an argument\n", opt);
  exit(EXIT_FAILURE);
}
