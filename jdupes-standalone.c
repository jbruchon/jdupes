/* jdupes (C) 2015-2020 Jody Bruchon <jody@jodybruchon.com>
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

#define VER "1.13.1"
#define VERDATE "2020-06-10"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <libgen.h>
#include <limits.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
/* Optional FIDEDUPERANGE support */
#ifdef ENABLE_DEDUPE
 #ifndef __linux__
  #error "Filesystem-managed deduplication only available for Linux."
 #endif /* __linux__ */
 #include <sys/ioctl.h>
 #include <sys/utsname.h>
 #ifdef STATIC_BTRFS_H
  /* Static BTRFS header */
  #include <linux/types.h>
  #define BTRFS_IOCTL_MAGIC 0x94
  #define BTRFS_DEVICE_PATH_NAME_MAX 1024
  #define FILE_DEDUPE_RANGE_DIFFERS	1
  struct file_dedupe_range_info {
   __s64 fd; __u64 logical_offset; __u64 bytes_deduped; __s32 status; __u32 reserved;
  };
  struct file_dedupe_range {
   __u64 logical_offset; __u64 length; __u16 dest_count; __u16 reserved1;
   __u32 reserved2; struct file_dedupe_range_info info[0];
  };
  #define FIDEDUPERANGE _IOWR(BTRFS_IOCTL_MAGIC, 54, struct file_dedupe_range)
  /* Static BTRFS header */
 #else
  #include <linux/fs.h>
 #endif /* STATIC_BTRFS_H */
#endif /* ENABLE_DEDUPE */

#define JODY_HASH_WIDTH 32
typedef uint32_t jodyhash_t;
/* Set hash type (change this if swapping in a different hash function) */
typedef jodyhash_t jdupes_hash_t;

typedef ino_t jdupes_ino_t;
typedef mode_t jdupes_mode_t;

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)
#define CLEARFLAG(a,b) (a &= (~b))

/* Behavior modification flags */
#define F_RECURSE		(1U << 0)
#define F_HIDEPROGRESS		(1U << 1)
#define F_SOFTABORT		(1U << 2)
#define F_FOLLOWLINKS		(1U << 3)
#define F_DELETEFILES		(1U << 4)
#define F_INCLUDEEMPTY		(1U << 5)
#define F_CONSIDERHARDLINKS	(1U << 6)
#define F_SHOWSIZE		(1U << 7)
#define F_OMITFIRST		(1U << 8)
#define F_RECURSEAFTER		(1U << 9)
#define F_NOPROMPT		(1U << 10)
#define F_SUMMARIZEMATCHES	(1U << 11)
#define F_EXCLUDEHIDDEN		(1U << 12)
#define F_PERMISSIONS		(1U << 13)
#define F_HARDLINKFILES		(1U << 14)
#define F_EXCLUDESIZE		(1U << 15)
#define F_QUICKCOMPARE		(1U << 16)
#define F_USEPARAMORDER		(1U << 17)
#define F_DEDUPEFILES		(1U << 18)
#define F_REVERSESORT		(1U << 19)
#define F_ISOLATE		(1U << 20)
#define F_MAKESYMLINKS		(1U << 21)
#define F_PRINTMATCHES		(1U << 22)
#define F_ONEFS			(1U << 23)
#define F_PRINTNULL		(1U << 24)
#define F_PARTIALONLY		(1U << 25)
#define F_NOCHANGECHECK		(1U << 26)
#define F_PRINTJSON		(1U << 27)

#define F_LOUD			(1U << 30)
#define F_DEBUG			(1U << 31)

/* Per-file true/false flags */
#define F_VALID_STAT		(1U << 0)
#define F_HASH_PARTIAL		(1U << 1)
#define F_HASH_FULL		(1U << 2)
#define F_HAS_DUPES		(1U << 3)
#define F_IS_SYMLINK		(1U << 4)

/* Extra print flags */
#define P_PARTIAL		(1U << 0)
#define P_EARLYMATCH		(1U << 1)
#define P_FULLHASH		(1U << 2)

typedef enum {
  ORDER_NAME = 0,
  ORDER_TIME
} ordertype_t;

/* For interactive deletion input */
#define INPUT_SIZE 512

/* Per-file information */
typedef struct _file {
  struct _file *duplicates;
  struct _file *next;
  char *d_name;
  dev_t device;
  jdupes_mode_t mode;
  off_t size;
  jdupes_ino_t inode;
  jdupes_hash_t filehash_partial;
  jdupes_hash_t filehash;
  time_t mtime;
  uint32_t flags;  /* Status flags */
#ifndef NO_USER_ORDER
  unsigned int user_order; /* Order of the originating command-line parameter */
#endif
#ifndef NO_HARDLINKS
  nlink_t nlink;
#endif
#ifndef NO_PERMS
  uid_t uid;
  gid_t gid;
#endif
} file_t;

typedef struct _filetree {
  file_t *file;
  struct _filetree *left;
  struct _filetree *right;
} filetree_t;

/* -X exclusion parameter stack */
struct exclude {
  struct exclude *next;
  unsigned int flags;
  int64_t size;
  char param[];
};

/* Exclude parameter flags */
#define X_DIR			0x00000001U
#define X_SIZE_EQ		0x00000002U
#define X_SIZE_GT		0x00000004U
#define X_SIZE_LT		0x00000008U
/* The X-than-or-equal are combination flags */
#define X_SIZE_GTEQ		0x00000006U
#define X_SIZE_LTEQ		0x0000000aU

/* Size specifier flags */
#define XX_EXCL_SIZE		0x0000000eU
/* Flags that use numeric offset instead of a string */
#define XX_EXCL_OFFSET		0x0000000eU
/* Flags that require a data parameter */
#define XX_EXCL_DATA		0x0000000fU

/* Exclude definition array */
struct exclude_tags {
  const char * const tag;
  const uint32_t flags;
};

/* Suffix definitions (treat as case-insensitive) */
struct size_suffix {
  const char * const suffix;
  const int64_t multiplier;
};

const char *FILE_MODE_RO = "rb";
const char dir_sep = '/';

/* Behavior modification flags */
static uint_fast32_t flags = 0, p_flags = 0;

static const char *program_name;

/* This gets used in many functions */
static struct stat s;

/* Larger chunk size makes large files process faster but uses more RAM */
#ifndef CHUNK_SIZE
 #define CHUNK_SIZE 32768
#endif
#define PARTIAL_HASH_SIZE 4096

/* Maximum path buffer size to use; must be large enough for a path plus
 * any work that might be done to the array it's stored in. PATH_MAX is
 * not always true. Read this article on the false promises of PATH_MAX:
 * http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
 */
#define PATHBUF_SIZE 4096

/* Size suffixes - this gets exported */
static const struct size_suffix size_suffix[] = {
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

/* Tree to track each directory traversed */
struct travdone {
  struct travdone *left;
  struct travdone *right;
  jdupes_ino_t inode;
  dev_t device;
};
static struct travdone *travdone_head = NULL;

/* Exclusion tree head and static tag list */
struct exclude *exclude_head = NULL;
static const struct exclude_tags exclude_tags[] = {
  { "dir",	X_DIR },
  { "size+",	X_SIZE_GT },
  { "size+=",	X_SIZE_GTEQ },
  { "size-=",	X_SIZE_LTEQ },
  { "size-",	X_SIZE_LT },
  { "size=",	X_SIZE_EQ },
  { NULL, 0 },
};

/* Required for progress indicator code */
static uintmax_t filecount = 0;
static uintmax_t progress = 0, item_progress = 0, dupecount = 0;
/* Number of read loops before checking progress indicator */
#define CHECK_MINIMUM 256

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

/* for temporary path mangling */
static char tempname[PATHBUF_SIZE * 2];

/***** End definitions, begin code *****/


/* Catch CTRL-C and either notify or terminate */
void sighandler(const int signum)
{
  (void)signum;
  if (interrupt || !ISFLAG(flags, F_SOFTABORT)) {
    fprintf(stderr, "\n");
    exit(EXIT_FAILURE);
  }
  interrupt = 1;
  return;
}


void sigusr1(const int signum)
{
  (void)signum;
  if (!ISFLAG(flags, F_SOFTABORT)) SETFLAG(flags, F_SOFTABORT);
  else CLEARFLAG(flags, F_SOFTABORT);
  return;
}


/* Out of memory */
static void oom(const char * const restrict msg)
{
  fprintf(stderr, "\nout of memory: %s\n", msg);
  exit(EXIT_FAILURE);
}


/* Null pointer failure */
static void nullptr(const char * restrict func)
{
  static const char n[] = "(NULL)";
  if (func == NULL) func = n;
  fprintf(stderr, "\ninternal error: NULL pointer passed to %s\n", func);
  exit(EXIT_FAILURE);
}


/* Jody Bruchon's fast hashing function
 * Copyright (C) 2014-2020 by Jody Bruchon <jody@jodybruchon.com>
 * Released under The MIT License
 */

#define JODY_HASH_SHIFT 14
#define JODY_HASH_CONSTANT 0x1f3d5b79U
static const jodyhash_t tail_mask[] = {
	0x00000000,
	0x000000ff,
	0x0000ffff,
	0x00ffffff,
	0xffffffff,
};

static jodyhash_t jody_block_hash(const jodyhash_t * restrict data,
		const jodyhash_t start_hash, const size_t count)
{
	jodyhash_t hash = start_hash;
	jodyhash_t element;
	jodyhash_t partial_salt;
	size_t len;

	/* Don't bother trying to hash a zero-length block */
	if (count == 0) return hash;

	len = count / sizeof(jodyhash_t);
	for (; len > 0; len--) {
		element = *data;
		hash += element;
		hash += JODY_HASH_CONSTANT;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(jodyhash_t) * 8 - JODY_HASH_SHIFT); /* bit rotate left */
		hash ^= element;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(jodyhash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= JODY_HASH_CONSTANT;
		hash += element;
		data++;
	}

	/* Handle data tail (for blocks indivisible by sizeof(jodyhash_t)) */
	len = count & (sizeof(jodyhash_t) - 1);
	if (len) {
		partial_salt = JODY_HASH_CONSTANT & tail_mask[len];
		element = *data & tail_mask[len];
		hash += element;
		hash += partial_salt;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(jodyhash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= element;
		hash = (hash << JODY_HASH_SHIFT) | hash >> (sizeof(jodyhash_t) * 8 - JODY_HASH_SHIFT);
		hash ^= partial_salt;
		hash += element;
	}

	return hash;
}


/* Compare two hashes like memcmp() */
#define HASH_COMPARE(a,b) ((a > b) ? 1:((a == b) ? 0:-1))


static inline char **cloneargs(const int argc, char **argv)
{
  static int x;
  static char **args;

  args = (char **)malloc(sizeof(char *) * (unsigned int)argc);
  if (args == NULL) oom("cloneargs() start");

  for (x = 0; x < argc; x++) {
    args[x] = (char *)malloc(strlen(argv[x]) + 1);
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

/* Check file's stat() info to make sure nothing has changed
 * Returns 1 if changed, 0 if not changed, negative if error */
static int file_has_changed(file_t * const restrict file)
{
  /* If -t/--nochangecheck specified then completely bypass this code */
  if (ISFLAG(flags, F_NOCHANGECHECK)) return 0;

  if (file == NULL || file->d_name == NULL) nullptr("file_has_changed()");

  if (!ISFLAG(file->flags, F_VALID_STAT)) return -66;

  if (stat(file->d_name, &s) != 0) return -2;
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
  if ((S_ISLNK(s.st_mode) > 0) ^ ISFLAG(file->flags, F_IS_SYMLINK)) return 1;
#endif

  return 0;
}


static inline int getfilestats(file_t * const restrict file)
{
  if (file == NULL || file->d_name == NULL) nullptr("getfilestats()");

  /* Don't stat the same file more than once */
  if (ISFLAG(file->flags, F_VALID_STAT)) return 0;
  SETFLAG(file->flags, F_VALID_STAT);

  if (stat(file->d_name, &s) != 0) return -1;
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
  if (S_ISLNK(s.st_mode) > 0) SETFLAG(file->flags, F_IS_SYMLINK);
#endif
  return 0;
}


static void add_exclude(const char *option)
{
  char *opt, *p;
  struct exclude *excl = exclude_head;
  const struct exclude_tags *tags = exclude_tags;
  const struct size_suffix *ss = size_suffix;

  if (option == NULL) nullptr("add_exclude()");

  opt = malloc(strlen(option) + 1);
  if (opt == NULL) oom("add_exclude option");
  strcpy(opt, option);
  p = opt;

  while (*p != ':' && *p != '\0') p++;

  /* Split tag string into *opt (tag) and *p (value) */
  if (*p == ':') {
    *p = '\0';
    p++;
  }

  while (tags->tag != NULL && strcmp(tags->tag, opt) != 0) tags++;
  if (tags->tag == NULL) goto bad_tag;

  /* Check for a tag that requires a value */
  if (tags->flags & XX_EXCL_DATA && *p == '\0') goto spec_missing;

  /* *p is now at the value, NOT the tag string! */

  if (exclude_head != NULL) {
    /* Add to end of exclusion stack if head is present */
    while (excl->next != NULL) excl = excl->next;
    excl->next = malloc(sizeof(struct exclude) + strlen(p) + 1);
    if (excl->next == NULL) oom("add_exclude alloc");
    excl = excl->next;
  } else {
    /* Allocate exclude_head if no exclusions exist yet */
    exclude_head = malloc(sizeof(struct exclude) + strlen(p) + 1);
    if (exclude_head == NULL) oom("add_exclude alloc");
    excl = exclude_head;
  }

  /* Set tag value from predefined tag array */
  excl->flags = tags->flags;

  /* Initialize the new exclude element */
  excl->next = NULL;
  if (excl->flags & XX_EXCL_OFFSET) {
    /* Exclude uses a number; handle it with possible suffixes */
    *(excl->param) = '\0';
    /* Get base size */
    if (*p < '0' || *p > '9') goto bad_size_suffix;
    excl->size = strtoll(p, &p, 10);
    /* Handle suffix, if any */
    if (*p != '\0') {
      while (ss->suffix != NULL && strcasecmp(ss->suffix, p) != 0) ss++;
      if (ss->suffix == NULL) goto bad_size_suffix;
      excl->size *= ss->multiplier;
    }
  } else {
    /* Exclude uses string data; just copy it */
    excl->size = 0;
    if (*p != '\0') strcpy(excl->param, p);
    else *(excl->param) = '\0';
  }

  free(opt);
  return;

spec_missing:
  fprintf(stderr, "Exclude spec missing or invalid: -X spec:data\n");
  exit(EXIT_FAILURE);
bad_tag:
  fprintf(stderr, "Invalid exclusion tag was specified\n");
  exit(EXIT_FAILURE);
bad_size_suffix:
  fprintf(stderr, "Invalid -X size suffix specified; use B or KMGTPE[i][B]\n");
  exit(EXIT_FAILURE);
}


static int getdirstats(const char * const restrict name,
        jdupes_ino_t * const restrict inode, dev_t * const restrict dev,
        jdupes_mode_t * const restrict mode)
{
  if (name == NULL || inode == NULL || dev == NULL) nullptr("getdirstats");

  if (stat(name, &s) != 0) return -1;
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
 * -4 on exlusion due to same filesystem
 * -5 on exclusion due to permissions */
static int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2)
{
  if (file1 == NULL || file2 == NULL || file1->d_name == NULL || file2->d_name == NULL) nullptr("check_conditions()");

  /* Exclude files that are not the same size */
  if (file1->size > file2->size) return -1;
  if (file1->size < file2->size) return 1;

#ifndef NO_USER_ORDER
  /* Exclude based on -I/--isolate */
  if (ISFLAG(flags, F_ISOLATE) && (file1->user_order == file2->user_order)) return -3;
#endif /* NO_USER_ORDER */

  /* Exclude based on -1/--one-file-system */
  if (ISFLAG(flags, F_ONEFS) && (file1->device != file2->device)) return -4;

   /* Exclude files by permissions if requested */
  if (ISFLAG(flags, F_PERMISSIONS) &&
          (file1->mode != file2->mode
#ifndef NO_PERMS
          || file1->uid != file2->uid
          || file1->gid != file2->gid
#endif
          )) {
    return -5;
  }

  /* Hard link and symlink + '-s' check */
#ifndef NO_HARDLINKS
  if ((file1->inode == file2->inode) && (file1->device == file2->device)) {
    if (ISFLAG(flags, F_CONSIDERHARDLINKS)) return 2;
    else return -2;
  }
#endif

  /* Fall through: all checks passed */
  return 0;
}


/* Check for exclusion conditions for a single file (1 = fail) */
static int check_singlefile(file_t * const restrict newfile)
{
  char * restrict tp = tempname;
  int excluded;

  if (newfile == NULL) nullptr("check_singlefile()");

  /* Exclude hidden files if requested */
  if (ISFLAG(flags, F_EXCLUDEHIDDEN)) {
    if (newfile->d_name == NULL) nullptr("check_singlefile newfile->d_name");
    strcpy(tp, newfile->d_name);
    tp = basename(tp);
    if (tp[0] == '.' && strcmp(tp, ".") && strcmp(tp, "..")) return 1;
  }

  /* Get file information and check for validity */
  const int i = getfilestats(newfile);
  if (i || newfile->size == -1) return 1;

  if (!S_ISDIR(newfile->mode)) {
    /* Exclude zero-length files if requested */
    if (newfile->size == 0 && !ISFLAG(flags, F_INCLUDEEMPTY)) return 1;

    /* Exclude files based on exclusion stack size specs */
    excluded = 0;
    for (struct exclude *excl = exclude_head; excl != NULL; excl = excl->next) {
      uint32_t sflag = excl->flags & XX_EXCL_SIZE;
      if (
           ((sflag == X_SIZE_EQ) && (newfile->size == excl->size)) ||
           ((sflag == X_SIZE_LTEQ) && (newfile->size <= excl->size)) ||
           ((sflag == X_SIZE_GTEQ) && (newfile->size >= excl->size)) ||
           ((sflag == X_SIZE_GT) && (newfile->size > excl->size)) ||
           ((sflag == X_SIZE_LT) && (newfile->size < excl->size))
      ) excluded = 1;
    }
    if (excluded) return 1;
  }

  return 0;
}


static file_t *init_newfile(const size_t len, file_t * restrict * const restrict filelistp)
{
  file_t * const restrict newfile = (file_t *)malloc(sizeof(file_t));

  if (!newfile) oom("init_newfile() file structure");
  if (!filelistp) nullptr("init_newfile() filelistp");

  memset(newfile, 0, sizeof(file_t));
  newfile->d_name = (char *)malloc(len);
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
static struct travdone *travdone_alloc(const jdupes_ino_t inode, const dev_t device)
{
  struct travdone *trav;

  trav = (struct travdone *)malloc(sizeof(struct travdone));
  if (trav == NULL) return NULL;
  trav->left = NULL;
  trav->right = NULL;
  trav->inode = inode;
  trav->device = device;
  return trav;
}


/* De-allocate the travdone tree */
static void travdone_free(struct travdone * const restrict cur)
{
  if (cur == NULL) return;
  if (cur->left != NULL) travdone_free(cur->left);
  if (cur->right != NULL) travdone_free(cur->right);
  free(cur);
  return;
}


/* Add a single file to the file tree */
static inline file_t *grokfile(const char * const restrict name, file_t * restrict * const restrict filelistp)
{
  file_t * restrict newfile;

  if (!name || !filelistp) nullptr("grokfile()");

  /* Allocate the file_t and the d_name entries */
  newfile = init_newfile(strlen(name) + 2, filelistp);

  strcpy(newfile->d_name, name);

  /* Single-file [l]stat() and exclusion condition check */
  if (check_singlefile(newfile) != 0) {
    free(newfile->d_name);
    free(newfile);
    return NULL;
  }
  return newfile;
}


/* Count the following statistics:
   - Maximum number of files in a duplicate set (length of longest dupe chain)
   - Number of non-zero-length files that have duplicates (if n_files != NULL)
   - Total number of duplicate file sets (groups) */
static unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
                unsigned int * const restrict n_files) {
  unsigned int groups = 0;

  if (files == NULL || max == NULL) nullptr("get_max_dupes()");

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


/* BTRFS deduplication of file blocks */
#ifdef ENABLE_DEDUPE

/* Message to append to BTRFS warnings based on write permissions */
static const char *readonly_msg[] = {
   "",
   " (no write permission)"
};

static char *dedupeerrstr(int err) {

  tempname[sizeof(tempname)-1] = '\0';
  if (err == FILE_DEDUPE_RANGE_DIFFERS) {
    snprintf(tempname, sizeof(tempname), "FILE_DEDUPE_RANGE_DIFFERS (data modified in the meantime?)");
    return tempname;
  } else if (err < 0) {
    return strerror(-err);
  } else {
    snprintf(tempname, sizeof(tempname), "Unknown error %d", err);
    return tempname;
  }
}

static void dedupefiles(file_t * restrict files)
{
  struct utsname utsname;
  struct file_dedupe_range *same;
  char **dupe_filenames; /* maps to same->info indices */

  file_t *curfile;
  unsigned int n_dupes, max_dupes, cur_info;
  unsigned int cur_file = 0, max_files, total_files = 0;

  int fd;
  int ret, status, readonly;

  /* Refuse to dedupe on 2.x kernels; they could damage user data */
  if (uname(&utsname)) {
    fprintf(stderr, "Failed to get kernel version! Aborting.\n");
    exit(EXIT_FAILURE);
  }
  if (*(utsname.release) == '2' && *(utsname.release + 1) == '.') {
    fprintf(stderr, "Refusing to dedupe on a 2.x kernel; data loss could occur. Aborting.\n");
    exit(EXIT_FAILURE);
  }

  /* Find the largest dupe set, alloc space to hold structs for it */
  get_max_dupes(files, &max_dupes, &max_files);
  /* Kernel dupe count is a uint16_t so exit if the type's limit is exceeded */
  if (max_dupes > 65535) {
    fprintf(stderr, "Largest duplicate set (%d) exceeds the 65535-file dedupe limit.\n", max_dupes);
    fprintf(stderr, "Ask the program author to add this feature if you really need it. Exiting!\n");
    exit(EXIT_FAILURE);
  }
  same = calloc(sizeof(struct file_dedupe_range) +
                sizeof(struct file_dedupe_range_info) * max_dupes, 1);
  dupe_filenames = malloc(max_dupes * sizeof(char *));
  if (!same || !dupe_filenames) oom("dedupefiles() structures");

  /* Main dedupe loop */
  while (files) {
    if (ISFLAG(files->flags, F_HAS_DUPES) && files->size) {
      cur_file++;
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        fprintf(stderr, "Dedupe [%u/%u] %u%% \r", cur_file, max_files,
            cur_file * 100 / max_files);
      }

      /* Open each file to be deduplicated */
      cur_info = 0;
      for (curfile = files->duplicates; curfile; curfile = curfile->duplicates) {
        int errno2;

        /* Never allow hard links to be passed to dedupe */
        if (curfile->device == files->device && curfile->inode == files->inode) continue;

        dupe_filenames[cur_info] = curfile->d_name;
        readonly = 0;
        if (access(curfile->d_name, W_OK) != 0) readonly = 1;
        fd = open(curfile->d_name, O_RDWR);

        /* If read-write open fails, privileged users can dedupe in read-only mode */
        if (fd == -1) {
          /* Preserve errno in case read-only fallback fails */
          errno2 = errno;
          fd = open(curfile->d_name, O_RDONLY);
          if (fd == -1) {
            fprintf(stderr, "Unable to open '%s': %s%s\n", curfile->d_name,
                strerror(errno2), readonly_msg[readonly]);
            continue;
          }
        }

        same->info[cur_info].fd = fd;
        same->info[cur_info].logical_offset = 0;
        cur_info++;
        total_files++;
      }
      n_dupes = cur_info;

      same->logical_offset = 0;
      same->length = (uint64_t)files->size;
      same->dest_count = (uint16_t)n_dupes;  /* kernel type is __u16 */

      fd = open(files->d_name, O_RDONLY);
      if (fd == -1) {
        fprintf(stderr, "unable to open(\"%s\", O_RDONLY): %s\n", files->d_name, strerror(errno));
        goto cleanup;
      }

      /* Call dedupe ioctl to pass the files to the kernel */
      ret = ioctl(fd, BTRFS_IOC_FILE_EXTENT_SAME, same);
      if (close(fd) == -1) fprintf(stderr, "Unable to close(\"%s\"): %s\n", files->d_name, strerror(errno));

      if (ret < 0) {
        fprintf(stderr, "dedupe failed against file '%s' (%d matches): %s\n", files->d_name, n_dupes, strerror(errno));
        goto cleanup;
      }

      for (cur_info = 0; cur_info < n_dupes; cur_info++) {
        status = same->info[cur_info].status;
        if (status != 0) {
          if (same->info[cur_info].bytes_deduped == 0) {
            fprintf(stderr, "warning: dedupe failed: %s => %s: %s [%d]%s\n",
              files->d_name, dupe_filenames[cur_info], dedupeerrstr(status),
              status, readonly_msg[readonly]);
          } else {
            fprintf(stderr, "warning: dedupe only did %" PRIdMAX " bytes: %s => %s: %s [%d]%s\n",
              (intmax_t)same->info[cur_info].bytes_deduped, files->d_name,
              dupe_filenames[cur_info], dedupeerrstr(status), status, readonly_msg[readonly]);
          }
        }
      }

cleanup:
      for (cur_info = 0; cur_info < n_dupes; cur_info++) {
        if (close((int)same->info[cur_info].fd) == -1) {
          fprintf(stderr, "unable to close(\"%s\"): %s", dupe_filenames[cur_info],
            strerror(errno));
        }
      }

    } /* has dupes */

    files = files->next;
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "Deduplication done (%d files processed)\n", total_files);
  free(same);
  free(dupe_filenames);
  return;
}
#endif /* ENABLE_DEDUPE */


/* Delete duplicate files automatically or interactively */

static void deletefiles(file_t *files, int prompt, FILE *tty)
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
        printf("[%u] %s\n", counter, files->d_name);
      }

      tmpfile = files->duplicates;

      while (tmpfile) {
        dupelist[++counter] = tmpfile;
        if (prompt) {
          printf("[%u] %s\n", counter, tmpfile->d_name);
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
          printf("   [+] %s\n", dupelist[x]->d_name);
        } else {
          if (file_has_changed(dupelist[x])) {
            printf("   [!] %s", dupelist[x]->d_name);
            printf("-- file changed since being scanned\n");
          } else if (remove(dupelist[x]->d_name) == 0) {
            printf("   [-] %s\n", dupelist[x]->d_name);
          } else {
            printf("   [!] %s", dupelist[x]->d_name);
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


/* Hard link or symlink files */
/* Compile out link code if no linking support is built in */
#if !(defined NO_HARDLINKS && defined NO_SYMLINKS)

static void linkfiles(file_t *files, const int hard)
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
#endif

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
#endif
      }
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        printf("[SRC] %s\n", srcfile->d_name);
      }
      for (; x <= counter; x++) {
        if (hard == 1) {
          /* Can't hard link files on different devices */
          if (srcfile->device != dupelist[x]->device) {
            fprintf(stderr, "warning: hard link target on different device, not linking:\n-//-> %s\n", dupelist[x]->d_name);
            continue;
          } else {
            /* The devices for the files are the same, but we still need to skip
             * anything that is already hard linked (-L and -H both set) */
            if (srcfile->inode == dupelist[x]->inode) {
              /* Don't show == arrows when not matching against other hard links */
              if (ISFLAG(flags, F_CONSIDERHARDLINKS))
                if (!ISFLAG(flags, F_HIDEPROGRESS)) {
                  printf("-==-> %s\n", dupelist[x]->d_name);
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

        /* Do not attempt to hard link files for which we don't have write access */
        if (access(dupelist[x]->d_name, W_OK) != 0)
        {
          fprintf(stderr, "warning: link target is a read-only file, not linking:\n-//-> %s\n", dupelist[x]->d_name);
          continue;
        }
        /* Check file pairs for modification before linking */
        /* Safe linking: don't actually delete until the link succeeds */
        i = file_has_changed(srcfile);
        if (i) {
          fprintf(stderr, "warning: source file modified since scanned; changing source file:\n[SRC] %s\n", dupelist[x]->d_name);
          srcfile = dupelist[x];
          continue;
        }
        if (file_has_changed(dupelist[x])) {
          fprintf(stderr, "warning: target file modified since scanned, not linking:\n-//-> %s\n", dupelist[x]->d_name);
          continue;
        }

        /* Make sure the name will fit in the buffer before trying */
        name_len = strlen(dupelist[x]->d_name) + 14;
        if (name_len > PATHBUF_SIZE) continue;
        /* Assemble a temporary file name */
        strcpy(tempname, dupelist[x]->d_name);
        strcat(tempname, ".__jdupes__.tmp");
        /* Rename the source file to the temporary name */
        i = rename(dupelist[x]->d_name, tempname);
        if (i != 0) {
          fprintf(stderr, "warning: cannot move link target to a temporary name, not linking:\n-//-> %s\n", dupelist[x]->d_name);
          /* Just in case the rename succeeded yet still returned an error, roll back the rename */
          rename(tempname, dupelist[x]->d_name);
          continue;
        }

        /* Create the desired hard link with the original file's name */
        errno = 0;
        success = 0;
        if (success) {
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            printf("%s %s\n", hard ? "---->" : "-@@->", dupelist[x]->d_name);
          }
        } else {
          /* The link failed. Warn the user and put the link target back */
          if (!ISFLAG(flags, F_HIDEPROGRESS)) {
            printf("-//-> %s\n", dupelist[x]->d_name);
          }
          fprintf(stderr, "warning: unable to link '%s' -> '%s': %s\n",
            dupelist[x]->d_name, srcfile->d_name, strerror(errno));
          i = rename(tempname, dupelist[x]->d_name);
          if (i != 0) {
            fprintf(stderr, "error: cannot rename temp file back to original\n");
            fprintf(stderr, "original: %s\n", dupelist[x]->d_name);
            fprintf(stderr, "current:  %s\n", tempname);
          }
          continue;
        }

        /* Remove temporary file to clean up; if we can't, reverse the linking */
        i = remove(tempname);
        if (i != 0) {
          /* If the temp file can't be deleted, there may be a permissions problem
           * so reverse the process and warn the user */
          fprintf(stderr, "\nwarning: can't delete temp file, reverting: %s\n", tempname);
          i = remove(dupelist[x]->d_name);
          /* This last error really should not happen, but we can't assume it won't */
          if (i != 0) fprintf(stderr, "\nwarning: couldn't remove link to restore original file\n");
          else {
            i = rename(tempname, dupelist[x]->d_name);
            if (i != 0) {
              fprintf(stderr, "\nwarning: couldn't revert the file to its original name\n");
              fprintf(stderr, "original: %s\n", dupelist[x]->d_name);
              fprintf(stderr, "current:  %s\n", tempname);
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
#endif /* NO_HARDLINKS && NO_SYMLINKS */


/* Print matched file sets */
static int fwprint(FILE * const restrict stream, const char * const restrict str, const int cr)
{
  if (cr == 2) return fprintf(stream, "%s%c", str, 0);
  else return fprintf(stream, "%s%s", str, cr == 1 ? "\n" : "");
}


/* Print comprehensive information to stdout in JSON format */
#define TO_HEX(a) (char)(((a) & 0x0f) <= 0x09 ? (a) + 0x30 : (a) + 0x57)

static void json_escape(char *string, char *escaped)
{
  int length = 0;
  while (*string != '\0' && length < (PATH_MAX * 2 - 1)) {
    switch (*string) {
      case '\"':
      case '\\':
        *escaped++ = '\\';
        *escaped++ = *string++;
        length += 2;
        break;
      default:
	if (!(*string & 0xe0)) {
	  strcpy(escaped, "\\u00");
	  escaped += 4;
	  *escaped++ = TO_HEX((*string >> 4));
	  *escaped++ = TO_HEX(*string++);
	  length += 6;
	} else {
          *escaped++ = *string++;
          length++;
	}
        break;
    }
  }
  *escaped = '\0';
  return;
}

extern void printjson(file_t * restrict files, const int argc, char **argv)
{
  file_t * restrict tmpfile;
  int arg = 0, comma = 0;
  char *temp = malloc(PATH_MAX * 2);
  char *temp2 = malloc(PATH_MAX * 2);
  char *temp_insert = temp;

  /* Output information about the jdupes command environment */
  printf("{\n  \"jdupesVersion\": \"%s\",\n  \"jdupesVersionDate\": \"%s\",\n", VER, VERDATE);

  printf("  \"commandLine\": \"");
  while (arg < argc) {
    sprintf(temp_insert, " %s", argv[arg]);
    temp_insert += strlen(temp_insert);
    arg++;
  }
  json_escape(temp, temp2);
  printf("%s\",\n", temp2);
  printf("  \"extensionFlags\": \"standalone\",\n");

  printf("  \"matchSets\": [\n");
  while (files != NULL) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      if (comma) printf(",\n");
      printf("    {\n      \"fileSize\": %" PRIdMAX ",\n      \"fileList\": [\n        { \"filePath\": \"", (intmax_t)files->size);
      sprintf(temp, "%s", files->d_name);
      json_escape(temp, temp2);
      fwprint(stdout, temp2, 0);
      printf("\"");
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        printf(" },\n        { \"filePath\": \"");
        sprintf(temp, "%s", tmpfile->d_name);
        json_escape(temp, temp2);
        fwprint(stdout, temp2, 0);
        printf("\"");
        tmpfile = tmpfile->duplicates;
      }
      printf(" }\n      ]\n    }");
      comma = 1;
    }
    files = files->next;
  }

  printf("\n  ]\n}\n");

  free(temp); free(temp2);
  return;
}


static void printmatches(file_t * restrict files)
{
  file_t * restrict tmpfile;
  int printed = 0;
  int cr = 1;

  if (ISFLAG(flags, F_PRINTNULL)) cr = 2;

  while (files != NULL) {
    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      printed = 1;
      if (!ISFLAG(flags, F_OMITFIRST)) {
        if (ISFLAG(flags, F_SHOWSIZE)) printf("%" PRIdMAX " byte%c each:\n", (intmax_t)files->size,
         (files->size != 1) ? 's' : ' ');
        fwprint(stdout, files->d_name, cr);
      }
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        fwprint(stdout, tmpfile->d_name, cr);
        tmpfile = tmpfile->duplicates;
      }
      if (files->next != NULL) fwprint(stdout, "", cr);
    }

    files = files->next;
  }

  if (printed == 0) fwprint(stderr, "No duplicates found.", 1);

  return;
}


/* Print summary of match statistics to stdout */

static void summarizematches(const file_t * restrict files)
{
  unsigned int numsets = 0;
  off_t numbytes = 0;
  int numfiles = 0;

  while (files != NULL) {
    file_t *tmpfile;

    if (ISFLAG(files->flags, F_HAS_DUPES)) {
      numsets++;
      tmpfile = files->duplicates;
      while (tmpfile != NULL) {
        numfiles++;
        numbytes += files->size;
        tmpfile = tmpfile->duplicates;
      }
    }
    files = files->next;
  }

  if (numsets == 0)
    printf("No duplicates found.\n");
  else
  {
    printf("%d duplicate files (in %d sets), occupying ", numfiles, numsets);
    if (numbytes < 1000) printf("%" PRIdMAX " byte%c\n", (intmax_t)numbytes, (numbytes != 1) ? 's' : ' ');
    else if (numbytes <= 1000000) printf("%" PRIdMAX " KB\n", (intmax_t)(numbytes / 1000));
    else printf("%" PRIdMAX " MB\n", (intmax_t)(numbytes / 1000000));
  }
  return;
}


/* Load a directory's contents into the file tree, recursing as needed */
static void grokdir(const char * const restrict dir,
                file_t * restrict * const restrict filelistp,
                int recurse)
{
  file_t * restrict newfile;
  struct dirent *dirinfo;
  static int grokdir_level = 0;
  size_t dirlen;
  struct travdone *traverse;
  int i, single = 0;
  jdupes_ino_t inode;
  dev_t device, n_device;
  jdupes_mode_t mode;
  DIR *cd;

  if (dir == NULL || filelistp == NULL) nullptr("grokdir()");

  /* Double traversal prevention tree */
  i = getdirstats(dir, &inode, &device, &mode);
  if (i < 0) goto error_travdone;

  if (travdone_head == NULL) {
    travdone_head = travdone_alloc(inode, device);
    if (travdone_head == NULL) goto error_travdone;
  } else {
    traverse = travdone_head;
    while (1) {
      if (traverse == NULL) nullptr("grokdir() traverse");
      /* Don't re-traverse directories we've already seen */
      if (S_ISDIR(mode) && inode == traverse->inode && device == traverse->device) return;
      else if (inode > traverse->inode || (inode == traverse->inode && device > traverse->device)) {
        /* Traverse right */
        if (traverse->right == NULL) {
          traverse->right = travdone_alloc(inode, device);
          if (traverse->right == NULL) goto error_travdone;
          break;
        }
        traverse = traverse->right;
        continue;
      } else {
        /* Traverse left */
        if (traverse->left == NULL) {
          traverse->left = travdone_alloc(inode, device);
          if (traverse->left == NULL) goto error_travdone;
          break;
        }
        traverse = traverse->left;
        continue;
      }
    }
  }

  item_progress++;
  grokdir_level++;

  /* if dir is actually a file, just add it to the file tree */
  if (i == 1) {
    newfile = grokfile(dir, filelistp);
    if (newfile == NULL) return;
    single = 1;
    goto add_single_file;
  }

  cd = opendir(dir);
  if (!cd) goto error_cd;

  while ((dirinfo = readdir(cd)) != NULL) {
    char * restrict tp = tempname;
    size_t d_name_len;

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

    /* Single-file [l]stat() and exclusion condition check */
    if (check_singlefile(newfile) != 0) {
      free(newfile->d_name);
      free(newfile);
      continue;
    }

    /* Optionally recurse directories, including symlinked ones if requested */
    if (S_ISDIR(newfile->mode)) {
      if (recurse) {
        /* --one-file-system */
        if (ISFLAG(flags, F_ONEFS)
            && (getdirstats(newfile->d_name, &inode, &n_device, &mode) == 0)
            && (device != n_device)) {
          free(newfile->d_name);
          free(newfile);
          continue;
        }
#ifndef NO_SYMLINKS
        else if (ISFLAG(flags, F_FOLLOWLINKS) || !ISFLAG(newfile->flags, F_IS_SYMLINK))
          grokdir(newfile->d_name, filelistp, recurse);
#else
        else grokdir(newfile->d_name, filelistp, recurse);
#endif
      }
      free(newfile->d_name);
      free(newfile);
      continue;
    } else {
add_single_file:
      /* Add regular files to list, including symlink targets if requested */
#ifndef NO_SYMLINKS
      if (!ISFLAG(newfile->flags, F_IS_SYMLINK) || (ISFLAG(newfile->flags, F_IS_SYMLINK) && ISFLAG(flags, F_FOLLOWLINKS))) {
#else
      if (S_ISREG(newfile->mode)) {
#endif
        *filelistp = newfile;
        filecount++;
        progress++;

      } else {
        free(newfile->d_name);
        free(newfile);
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

  closedir(cd);

skip_single:
  grokdir_level--;
  if (grokdir_level == 0 && !ISFLAG(flags, F_HIDEPROGRESS)) {
    fprintf(stderr, "\rScanning: %" PRIuMAX " files, %" PRIuMAX " items (in %u specified)",
            progress, item_progress, user_item_count);
  }
  return;

error_travdone:
  fprintf(stderr, "\ncould not stat dir %s\n", dir);
  return;
error_cd:
  fprintf(stderr, "\ncould not chdir to %s\n", dir);
  return;
error_overflow:
  fprintf(stderr, "\nerror: a path buffer overflowed\n");
  exit(EXIT_FAILURE);
}


/* Use Jody Bruchon's hash function on part or all of a file */
static jdupes_hash_t *get_filehash(const file_t * const restrict checkfile,
                const size_t max_read)
{
  off_t fsize;
  /* This is an array because we return a pointer to it */
  static jdupes_hash_t hash[1];
  static jdupes_hash_t *chunk = NULL;
  FILE *file;
  int check = 0;

  if (checkfile == NULL || checkfile->d_name == NULL) nullptr("get_filehash()");

  /* Allocate on first use */
  if (chunk == NULL) {
    chunk = (jdupes_hash_t *)malloc(CHUNK_SIZE);
    if (!chunk) oom("get_filehash() chunk");
  }

  /* Get the file size. If we can't read it, bail out early */
  if (checkfile->size == -1) return NULL;
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
  if (ISFLAG(checkfile->flags, F_HASH_PARTIAL)) {
    *hash = checkfile->filehash_partial;
    /* Don't bother going further if max_read is already fulfilled */
    if (max_read != 0 && max_read <= PARTIAL_HASH_SIZE) return hash;
  }
  errno = 0;
  file = fopen(checkfile->d_name, FILE_MODE_RO);
  if (file == NULL) {
    fprintf(stderr, "\n%s error opening file %s\n", strerror(errno), checkfile->d_name);
    return NULL;
  }
  /* Actually seek past the first chunk if applicable
   * This is part of the filehash_partial skip optimization */
  if (ISFLAG(checkfile->flags, F_HASH_PARTIAL)) {
    if (fseeko(file, PARTIAL_HASH_SIZE, SEEK_SET) == -1) {
      fclose(file);
      fprintf(stderr, "\nerror seeking in file %s\n", checkfile->d_name);
      return NULL;
    }
    fsize -= PARTIAL_HASH_SIZE;
  }

  /* Read the file in CHUNK_SIZE chunks until we've read it all. */
  while (fsize > 0) {
    size_t bytes_to_read;

    if (interrupt) return 0;
    bytes_to_read = (fsize >= (off_t)CHUNK_SIZE) ? CHUNK_SIZE : (size_t)fsize;
    if (fread((void *)chunk, bytes_to_read, 1, file) != 1) {
      fprintf(stderr, "\nerror reading from file %s\n", checkfile->d_name);
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

  return hash;
}


static void registerfile(filetree_t * restrict * const restrict nodeptr,
                const enum tree_direction d, file_t * const restrict file)
{
  filetree_t * restrict branch;

  if (nodeptr == NULL || file == NULL || (d != NONE && *nodeptr == NULL)) nullptr("registerfile()");

  /* Allocate and initialize a new node for the file */
  branch = (filetree_t *)malloc(sizeof(filetree_t));
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
      exit(EXIT_FAILURE);
      break;
  }

  return;
}


/* Check two files for a match */
static file_t **checkmatch(filetree_t * restrict tree, file_t * const restrict file)
{
  int cmpresult = 0;
  int cantmatch = 0;
  const jdupes_hash_t * restrict filehash;

  if (tree == NULL || file == NULL || tree->file == NULL || tree->file->d_name == NULL || file->d_name == NULL) nullptr("checkmatch()");

  /* If device and inode fields are equal one of the files is a
   * hard link to the other or the files have been listed twice
   * unintentionally. We don't want to flag these files as
   * duplicates unless the user specifies otherwise. */

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
  if (ISFLAG(p_flags, P_EARLYMATCH)) printf("Early match check passed:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

  /* If preliminary matching succeeded, do main file data checks */
  if (cmpresult == 0) {
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

    /* Print partial hash matching pairs if requested */
    if (cmpresult == 0 && ISFLAG(p_flags, P_PARTIAL))
      printf("Partial hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

    if (file->size <= PARTIAL_HASH_SIZE || ISFLAG(flags, F_PARTIALONLY)) {
      /* filehash_partial = filehash if file is small enough */
      if (!ISFLAG(file->flags, F_HASH_FULL)) {
        file->filehash = file->filehash_partial;
        SETFLAG(file->flags, F_HASH_FULL);
      }
      if (!ISFLAG(tree->file->flags, F_HASH_FULL)) {
        tree->file->filehash = tree->file->filehash_partial;
        SETFLAG(tree->file->flags, F_HASH_FULL);
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
    }
  }

  if( (cantmatch!=0) && (cmpresult==0) ) {
    cmpresult = -1;
  }

  if (cmpresult < 0) {
    if (tree->left != NULL) {
      return checkmatch(tree->left, file);
    } else {
      registerfile(&tree, LEFT, file);
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (tree->right != NULL) {
      return checkmatch(tree->right, file);
    } else {
      registerfile(&tree, RIGHT, file);
      return NULL;
    }
  } else {
    /* All compares matched */
    if (ISFLAG(p_flags, P_FULLHASH)) printf("Full hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);
    return &tree->file;
  }
  /* Fall through - should never be reached */
  return NULL;
}


/* Do a byte-by-byte comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */
static inline int confirmmatch(FILE * const restrict file1, FILE * const restrict file2, off_t size)
{
  static char *c1 = NULL, *c2 = NULL;
  static size_t r1, r2;
  off_t bytes = 0;
  int check = 0;

  if (file1 == NULL || file2 == NULL) nullptr("confirmmatch()");

  /* Allocate on first use; OOM if either is ever NULLed */
  if (!c1) {
    c1 = (char *)malloc(CHUNK_SIZE);
    c2 = (char *)malloc(CHUNK_SIZE);
  }
  if (!c1 || !c2) oom("confirmmatch() c1/c2");

  fseek(file1, 0, SEEK_SET);
  fseek(file2, 0, SEEK_SET);

  do {
    if (interrupt) return 0;
    r1 = fread(c1, sizeof(char), CHUNK_SIZE, file1);
    r2 = fread(c2, sizeof(char), CHUNK_SIZE, file2);

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

  return 0;
}


static int sort_pairs_by_filename(file_t *f1, file_t *f2)
{
  if (f1 == NULL || f2 == NULL) nullptr("sort_pairs_by_filename()");

#ifndef NO_USER_ORDER
  int po = sort_pairs_by_param_order(f1, f2);
  if (po != 0) return po;
#endif /* NO_USER_ORDER */
  int sc = strcmp(f1->d_name, f2->d_name);

  return ((sort_direction > 0) ? sc : -sc);
}


static void registerpair(file_t **matchlist, file_t *newmatch,
                int (*comparef)(file_t *f1, file_t *f2))
{
  file_t *traverse;
  file_t *back;

  /* NULL pointer sanity checks */
  if (matchlist == NULL || newmatch == NULL || comparef == NULL) nullptr("registerpair()");

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
  printf("Usage: jdupes [options] FILES and/or DIRECTORIES...\n\n");

  printf("Duplicate file sets will be printed by default unless a different action\n");
  printf("option is specified (delete, summarize, link, dedupe, etc.)\n");
  printf(" -0 --printnull   \toutput nulls instead of CR/LF (like 'find -print0')\n");
  printf(" -1 --one-file-system \tdo not match files on different filesystems/devices\n");
  printf(" -A --nohidden    \texclude hidden files from consideration\n");
#ifdef ENABLE_DEDUPE
  printf(" -B --dedupe      \tsend matches to filesystem for block-level deduplication\n");
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
#ifndef NO_USER_ORDER
  printf(" -I --isolate     \tfiles in the same specified directory won't match\n");
#endif
  printf(" -j --json        \tproduce JSON (machine-readable) output\n");
#ifndef NO_SYMLINKS
  printf(" -l --linksoft    \tmake relative symlinks for duplicates w/o prompting\n");
#endif
#ifndef NO_HARDLINKS
  printf(" -L --linkhard    \thard link all duplicate files without prompting\n");
#endif /* NO_HARDLINKS */
  printf(" -m --summarize   \tsummarize dupe information\n");
  printf(" -M --printwithsummary\twill print matches and --summarize at the end\n");
  printf(" -N --noprompt    \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
#ifndef NO_USER_ORDER
  printf(" -O --paramorder  \tParameter order is more important than selected -O sort\n");
  printf("                  \tmtime (BY=time) or filename (BY=name, the default)\n");
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
  printf(" -t --nochangecheck  \tdisable security check for file changes (aka TOCTTOU)\n");
  printf(" -T --partial-only \tmatch based on partial hashes only. WARNING:\n");
  printf("                  \tEXTREMELY DANGEROUS paired with destructive actions!\n");
  printf("                  \t-T must be specified twice to work. Read the manual!\n");
  printf(" -v --version     \tdisplay jdupes version and license information\n");
  printf(" -x --xsize=SIZE  \texclude files of size < SIZE bytes from consideration\n");
  printf("    --xsize=+SIZE \t'+' specified before SIZE, exclude size > SIZE\n");
  printf(" -X --exclude=spec:info\texclude files based on specified criteria\n");
  printf("                  \tspecs: size+-=\n");
  printf("                  \tExclusions are cumulative: -X dir:abc -X dir:efg\n");
  printf(" -z --zeromatch   \tconsider zero-length files to be duplicates\n");
  printf(" -Z --softabort   \tIf the user aborts (i.e. CTRL-C) act on matches so far\n");
  printf("                  \tYou can send SIGUSR1 to the program to toggle this\n");
  printf("\nFor sizes, K/M/G/T/P/E[B|iB] suffixes can be used (case-insensitive)\n");
}


int main(int argc, char **argv)
{
  static file_t *files = NULL;
  static file_t *curfile;
  static char **oldargv;
  static char *xs;
  static int firstrecurse;
  static int opt;
  static int pm = 1;
  static int partialonly_spec = 0;
  static ordertype_t ordertype = ORDER_NAME;

  static const struct option long_options[] =
  {
    { "loud", 0, 0, '@' },
    { "printnull", 0, 0, '0' },
    { "one-file-system", 0, 0, '1' },
    { "nohidden", 0, 0, 'A' },
    { "dedupe", 0, 0, 'B' },
    { "chunksize", 1, 0, 'C' },
    { "delete", 0, 0, 'd' },
    { "debug", 0, 0, 'D' },
    { "omitfirst", 0, 0, 'f' },
    { "help", 0, 0, 'h' },
    { "hardlinks", 0, 0, 'H' },
    { "reverse", 0, 0, 'i' },
    { "isolate", 0, 0, 'I' },
    { "json", 0, 0, 'j' },
    { "linksoft", 0, 0, 'l' },
    { "linkhard", 0, 0, 'L' },
    { "summarize", 0, 0, 'm'},
    { "printwithsummary", 0, 0, 'M'},
    { "noempty", 0, 0, 'n' },
    { "noprompt", 0, 0, 'N' },
    { "order", 1, 0, 'o' },
    { "paramorder", 0, 0, 'O' },
    { "permissions", 0, 0, 'p' },
    { "print", 0, 0, 'P' },
    { "quiet", 0, 0, 'q' },
    { "quick", 0, 0, 'Q' },
    { "recurse", 0, 0, 'r' },
    { "recursive", 0, 0, 'r' },
    { "recurse:", 0, 0, 'R' },
    { "recursive:", 0, 0, 'R' },
    { "symlinks", 0, 0, 's' },
    { "size", 0, 0, 'S' },
    { "nochangecheck", 0, 0, 't' },
    { "partial-only", 0, 0, 'T' },
    { "version", 0, 0, 'v' },
    { "xsize", 1, 0, 'x' },
    { "exclude", 1, 0, 'X' },
    { "zeromatch", 0, 0, 'z' },
    { "softabort", 0, 0, 'Z' },
    { NULL, 0, 0, 0 }
  };

  /* Is stderr a terminal? If not, we won't write progress to it */
  if (!isatty(fileno(stderr))) SETFLAG(flags, F_HIDEPROGRESS);

  program_name = argv[0];

  oldargv = cloneargs(argc, argv);

  while ((opt = getopt_long(argc, argv,
  "@01ABC:dDfhHiIjlLmMnNOpP:qQrRsStTvVzZo:x:X:",
  long_options, NULL)) != EOF) {
    switch (opt) {
    /* Unsupported but benign options can just be skipped */
    case '@':
    case 'C':
    case 'D':
      break;
    case '0':
      SETFLAG(flags, F_PRINTNULL);
      break;
    case '1':
      SETFLAG(flags, F_ONEFS);
      break;
    case 'A':
      SETFLAG(flags, F_EXCLUDEHIDDEN);
      break;
    case 'd':
      SETFLAG(flags, F_DELETEFILES);
      break;
    case 'f':
      SETFLAG(flags, F_OMITFIRST);
      break;
    case 'h':
      help_text();
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
#ifndef NO_USER_ORDER
    case 'I':
      SETFLAG(flags, F_ISOLATE);
      break;
    case 'O':
      SETFLAG(flags, F_USEPARAMORDER);
      break;
#else
    case 'I':
    case 'O':
      fprintf(stderr, "warning: -I and -O are disabled and ignored in this build\n");
      break;
#endif
    case 'j':
      SETFLAG(flags, F_PRINTJSON);
      break;
    case 'm':
      SETFLAG(flags, F_SUMMARIZEMATCHES);
      break;
    case 'M':
      SETFLAG(flags, F_SUMMARIZEMATCHES);
      SETFLAG(flags, F_PRINTMATCHES);
      break;
    case 'n':
      //fprintf(stderr, "note: -n/--noempty is the default behavior now and is deprecated.\n");
      break;
    case 'N':
      SETFLAG(flags, F_NOPROMPT);
      break;
    case 'p':
      SETFLAG(flags, F_PERMISSIONS);
      break;
    case 'P':
      if (strcmp(optarg, "partial") == 0) SETFLAG(p_flags, P_PARTIAL);
      else if (strcmp(optarg, "early") == 0) SETFLAG(p_flags, P_EARLYMATCH);
      else if (strcmp(optarg, "fullhash") == 0) SETFLAG(p_flags, P_FULLHASH);
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
      break;
    case 'r':
      SETFLAG(flags, F_RECURSE);
      break;
    case 'R':
      SETFLAG(flags, F_RECURSEAFTER);
      break;
    case 't':
      SETFLAG(flags, F_NOCHANGECHECK);
      break;
    case 'T':
      if (partialonly_spec == 0)
        partialonly_spec = 1;
      else {
        partialonly_spec = 2;
        SETFLAG(flags, F_PARTIALONLY);
      }
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
      fprintf(stderr, "-x/--xsize is deprecated; use -X size[+-=]:size[suffix] instead\n");
      xs = malloc(8 + strlen(optarg));
      if (xs == NULL) oom("xsize temp string");
      strcpy(xs, "size");
      if (*optarg == '+') {
        strcat(xs, "+:");
        optarg++;
      } else {
        strcat(xs, "-=:");
      }
      strcat(xs, optarg);
      add_exclude(xs);
      free(xs);
      break;
    case 'X':
      add_exclude(optarg);
      break;
    case 'v':
    case 'V':
      printf("jdupes small stand-alone version (derived from v%s, %s)", VER, VERDATE);
      printf("\nCopyright (C) 2015-2020 by Jody Bruchon <jody@jodybruchon.com>\n");
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
      SETFLAG(flags, F_DEDUPEFILES);
      /* btrfs will do the byte-for-byte check itself */
      SETFLAG(flags, F_QUICKCOMPARE);
      /* It is completely useless to dedupe zero-length extents */
      CLEARFLAG(flags, F_INCLUDEEMPTY);
#else
      fprintf(stderr, "btrfs dedupe not supported\n");
      exit(EXIT_FAILURE);
#endif
      break;

    default:
      if (opt != '?') fprintf(stderr, "Sorry, using '-%c' is not supported in this build.\n", opt);
      fprintf(stderr, "Try `jdupes --help' for more information.\n");
      exit(EXIT_FAILURE);
    }
  }

  if (optind >= argc) {
    fprintf(stderr, "no files or directories specified (use -h option for help)\n");
    exit(EXIT_FAILURE);
  }

  if (partialonly_spec == 1) {
    fprintf(stderr, "--partial-only specified only once (it's VERY DANGEROUS, read the manual!)\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_PARTIALONLY) && ISFLAG(flags, F_QUICKCOMPARE)) {
    fprintf(stderr, "--partial-only overrides --quick and is even more dangerous (read the manual!)\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_RECURSE) && ISFLAG(flags, F_RECURSEAFTER)) {
    fprintf(stderr, "options --recurse and --recurse: are not compatible\n");
    exit(EXIT_FAILURE);
  }

  if (ISFLAG(flags, F_SUMMARIZEMATCHES) && ISFLAG(flags, F_DELETEFILES)) {
    fprintf(stderr, "options --summarize and --delete are not compatible\n");
    exit(EXIT_FAILURE);
  }

#ifdef ENABLE_DEDUPE
  if (ISFLAG(flags, F_CONSIDERHARDLINKS) && ISFLAG(flags, F_DEDUPEFILES))
    fprintf(stderr, "warning: option --dedupe overrides the behavior of --hardlinks\n");
#endif

  /* If pm == 0, call printmatches() */
  pm = !!ISFLAG(flags, F_SUMMARIZEMATCHES) +
      !!ISFLAG(flags, F_DELETEFILES) +
      !!ISFLAG(flags, F_HARDLINKFILES) +
      !!ISFLAG(flags, F_MAKESYMLINKS) +
      !!ISFLAG(flags, F_PRINTJSON) +
      !!ISFLAG(flags, F_DEDUPEFILES);

  if (pm > 1) {
      fprintf(stderr, "error: only one final action may be specified.\n");
      exit(EXIT_FAILURE);
  }
  if (pm == 0) SETFLAG(flags, F_PRINTMATCHES);

  if (ISFLAG(flags, F_RECURSEAFTER)) {
    firstrecurse = nonoptafter("--recurse:", argc, oldargv, argv);

    if (firstrecurse == argc)
      firstrecurse = nonoptafter("-R", argc, oldargv, argv);

    if (firstrecurse == argc) {
      fprintf(stderr, "-R option must be isolated from other options\n");
      exit(EXIT_FAILURE);
    }

    /* F_RECURSE is not set for directories before --recurse: */
    for (int x = optind; x < firstrecurse; x++) {
      grokdir(argv[x], &files, 0);
      user_item_count++;
    }

    /* Set F_RECURSE for directories after --recurse: */
    SETFLAG(flags, F_RECURSE);

    for (int x = firstrecurse; x < argc; x++) {
      grokdir(argv[x], &files, 1);
      user_item_count++;
    }
  } else {
    for (int x = optind; x < argc; x++) {
      grokdir(argv[x], &files, ISFLAG(flags, F_RECURSE));
      user_item_count++;
    }
  }

  /* We don't need the double traversal check tree anymore */
  travdone_free(travdone_head);

  if (ISFLAG(flags, F_REVERSESORT)) sort_direction = -1;
  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\n");
  if (!files) {
    fprintf(stderr, "No duplicates found.\n");
    exit(EXIT_SUCCESS);
  }

  curfile = files;
  progress = 0;

  /* Catch CTRL-C */
  signal(SIGINT, sighandler);
  /* Catch SIGUSR1 and use it to enable -Z */
  signal(SIGUSR1, sigusr1);

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
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
        dupecount++;
        goto skip_full_check;
      }

      file1 = fopen(curfile->d_name, FILE_MODE_RO);
      if (!file1) {
        curfile = curfile->next;
        continue;
      }

      file2 = fopen((*match)->d_name, FILE_MODE_RO);
      if (!file2) {
        fclose(file1);
        curfile = curfile->next;
        continue;
      }

      if (confirmmatch(file1, file2, curfile->size)) {
        registerpair(match, curfile,
            (ordertype == ORDER_TIME) ? sort_pairs_by_mtime : sort_pairs_by_filename);
        dupecount++;
      }

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
#ifndef NO_SYMLINKS
  if (ISFLAG(flags, F_MAKESYMLINKS)) linkfiles(files, 0);
#endif
#ifndef NO_HARDLINKS
  if (ISFLAG(flags, F_HARDLINKFILES)) linkfiles(files, 1);
#endif /* NO_HARDLINKS */
#ifdef ENABLE_DEDUPE
  if (ISFLAG(flags, F_DEDUPEFILES)) dedupefiles(files);
#endif /* ENABLE_DEDUPE */
  if (ISFLAG(flags, F_PRINTMATCHES)) printmatches(files);
  if (ISFLAG(flags, F_PRINTJSON)) printjson(files, argc, argv);
  if (ISFLAG(flags, F_SUMMARIZEMATCHES)) {
    if (ISFLAG(flags, F_PRINTMATCHES)) printf("\n\n");
    summarizematches(files);
  }

  exit(EXIT_SUCCESS);
}
