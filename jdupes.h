/* jdupes main program header
 * See jdupes.c for license information */

#ifndef JDUPES_H
#define JDUPES_H

#ifdef __cplusplus
extern "C" {
#endif

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __CYGWIN__
 #ifndef ON_WINDOWS
  #define ON_WINDOWS 1
 #endif
 #define NO_SYMLINKS 1
 #define NO_PERMS 1
 #define NO_SIGACTION 1
 #define MAX_HARDLINK_COUNT 1023 
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <io.h>
 #include "win_stat.h"
#else
  /* this is the maximum number of hardlinks per file on ext4. For other file systems this can be relaxed. 
     Would need a specific configuration option or probing at the beginning */
 #define MAX_HARDLINK_COUNT 65000 
#endif /* Win32 */

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include "string_malloc.h"
#include "jody_sort.h"
#include "version.h"

#include "xxhash.h"

/* Set hash type (change this if swapping in a different hash function) */
 typedef XXH64_hash_t jdupes_hash_t;

/* Some types are different on Windows */
#ifdef ON_WINDOWS
 typedef uint64_t jdupes_ino_t;
 typedef uint32_t jdupes_mode_t;
 extern const char dir_sep;
 #ifdef UNICODE
  extern const wchar_t *FILE_MODE_RO;
 #else
  extern const char *FILE_MODE_RO;
 #endif /* UNICODE */

#else /* Not Windows */
 #include <sys/stat.h>
 typedef ino_t jdupes_ino_t;
 typedef mode_t jdupes_mode_t;
 extern const char *FILE_MODE_RO;
 extern const char dir_sep;
 #ifdef UNICODE
  #error Do not define UNICODE on non-Windows platforms.
  #undef UNICODE
 #endif
#endif /* _WIN32 || __CYGWIN__ */

/* Windows + Unicode compilation */
#ifdef UNICODE
 #define WPATH_MAX 8192
 #define PATHBUF_SIZE WPATH_MAX
  typedef wchar_t wpath_t[WPATH_MAX];
  extern int out_mode;
  extern int err_mode;
 #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, WPATH_MAX)
 #define W2M(a,b) WideCharToMultiByte(CP_UTF8, 0, a, -1, (LPSTR)b, WPATH_MAX, NULL, NULL)
#endif /* UNICODE */

#ifndef NO_SYMLINKS
#include "jody_paths.h"
#endif

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)
#define CLEARFLAG(a,b) (a &= (~b))

/* Low memory option overrides */
#ifdef LOW_MEMORY
 #ifndef NO_PERMS
  #define NO_PERMS 1
 #endif
#endif

/* Aggressive verbosity for deep debugging */
#ifdef LOUD_DEBUG
 #ifndef DEBUG
  #define DEBUG
 #endif
 #define LOUD(...) if ISFLAG(flags, F_LOUD) __VA_ARGS__
#else
 #define LOUD(a)
#endif

/* Compile out debugging stat counters unless requested */
#ifdef DEBUG
 #define DBG(a) a
 #ifndef TREE_DEPTH_STATS
  #define TREE_DEPTH_STATS
 #endif
#else
 #define DBG(a)
#endif


/* Behavior modification flags */
extern uint_fast32_t flags;
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

#ifndef PARTIAL_HASH_SIZE
 #define PARTIAL_HASH_SIZE 4096
#endif

/* Maximum path buffer size to use; must be large enough for a path plus
 * any work that might be done to the array it's stored in. PATH_MAX is
 * not always true. Read this article on the false promises of PATH_MAX:
 * http://insanecoding.blogspot.com/2007/11/pathmax-simply-isnt.html
 * Windows + Unicode needs a lot more space than UTF-8 in Linux/Mac OS X
 */
#ifndef PATHBUF_SIZE
#define PATHBUF_SIZE 4096
#endif

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
 #ifndef ON_WINDOWS
  nlink_t nlink;
 #else
  uint32_t nlink;  /* link count on Windows is always a DWORD */
 #endif
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

/* This gets used in many functions */
#ifdef ON_WINDOWS
 extern struct winstat s;
 #define STAT win_stat
#else
 extern struct stat s;
 #define STAT stat
#endif


/* -X extended filter parameter stack */
struct extfilter {
  struct extfilter *next;
  unsigned int flags;
  int64_t size;
  char param[];
};

/* Extended filter parameter flags */
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
struct extfilter_tags {
  const char * const tag;
  const uint32_t flags;
};

extern const struct extfilter_tags extfilter_tags[];
extern struct extfilter *extfilter_head;


/* Suffix definitions (treat as case-insensitive) */
struct size_suffix {
  const char * const suffix;
  const int64_t multiplier;
};

extern const struct size_suffix size_suffix[];
extern char tempname[PATHBUF_SIZE * 2];

extern const char *extensions[];

extern void oom(const char * const restrict msg);
extern void nullptr(const char * restrict func);
extern int file_has_changed(file_t * const restrict file);
extern int getfilestats(file_t * const restrict file);
extern int getdirstats(const char * const restrict name,
        jdupes_ino_t * const restrict inode, dev_t * const restrict dev,
	jdupes_mode_t * const restrict mode);
extern int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2);
extern unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
		                unsigned int * const restrict n_files);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_H */
