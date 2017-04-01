/* jdupes main program header
 * See jdupes.c for license information */

#ifndef JDUPES_H
#define JDUPES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <limits.h>
#include <stdint.h>
#include <sys/types.h>
#include <unistd.h>
#include "string_malloc.h"
#include "jody_hash.h"
#include "jody_sort.h"
#include "version.h"

/* Optional btrfs support */
#ifdef ENABLE_BTRFS
#include <sys/ioctl.h>
#include <linux/btrfs.h>
#endif

/* Detect Windows and modify as needed */
#if defined _WIN32 || defined __CYGWIN__
 #define ON_WINDOWS 1
 #define NO_SYMLINKS 1
 #define NO_PERMS 1
 #define NO_SIGACTION 1
 #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
 #endif
 #include <windows.h>
 #include <io.h>
 #include "win_stat.h"
 #define S_ISREG WS_ISREG
 #define S_ISDIR WS_ISDIR
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
extern wchar_t wname[PATH_MAX];
extern wchar_t wname2[PATH_MAX];
extern wchar_t wstr[PATH_MAX];
extern int out_mode;
extern int err_mode;
 #define M2W(a,b) MultiByteToWideChar(CP_UTF8, 0, a, -1, (LPWSTR)b, PATH_MAX)
 #define W2M(a,b) WideCharToMultiByte(CP_UTF8, 0, a, -1, (LPSTR)b, PATH_MAX, NULL, NULL)
#endif /* UNICODE */

#ifndef NO_SYMLINKS
#include "jody_paths.h"
#endif

#define ISFLAG(a,b) ((a & b) == b)
#define SETFLAG(a,b) (a |= b)
#define CLEARFLAG(a,b) (a &= (~b))

/* Low memory option overrides */
#ifdef LOW_MEMORY
 #undef USE_TREE_REBALANCE
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


/* How many operations to wait before updating progress counters */
#define DELAY_COUNT 256

/* Behavior modification flags */
extern uint_fast32_t flags;
#define F_RECURSE		0x00000001U
#define F_HIDEPROGRESS		0x00000002U
#define F_SOFTABORT		0x00000004U
#define F_FOLLOWLINKS		0x00000008U
#define F_DELETEFILES		0x00000010U
#define F_INCLUDEEMPTY		0x00000020U
#define F_CONSIDERHARDLINKS	0x00000040U
#define F_SHOWSIZE		0x00000080U
#define F_OMITFIRST		0x00000100U
#define F_RECURSEAFTER		0x00000200U
#define F_NOPROMPT		0x00000400U
#define F_SUMMARIZEMATCHES	0x00000800U
#define F_EXCLUDEHIDDEN		0x00001000U
#define F_PERMISSIONS		0x00002000U
#define F_HARDLINKFILES		0x00004000U
#define F_EXCLUDESIZE		0x00008000U
#define F_QUICKCOMPARE		0x00010000U
#define F_USEPARAMORDER		0x00020000U
#define F_DEDUPEFILES		0x00040000U
#define F_REVERSESORT		0x00080000U
#define F_ISOLATE		0x00100000U
#define F_MAKESYMLINKS		0x00200000U
#define F_PRINTMATCHES		0x00400000U
#define F_ONEFS			0x00800000U

#define F_LOUD			0x40000000U
#define F_DEBUG			0x80000000U

/* Per-file true/false flags */
#define F_VALID_STAT		0x00000001U
#define F_HASH_PARTIAL		0x00000002U
#define F_HASH_FULL		0x00000004U
#define F_HAS_DUPES		0x00000008U
#define F_IS_SYMLINK		0x00000010U

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

/* For interactive deletion input */
#define INPUT_SIZE 512

struct _file; /* prototype */

/* Per-file information unique to a filename */
typedef struct _filename {
  struct _filename *next;
  struct _file *file;
  char *d_name;
  unsigned int user_order; /* Order of the originating command-line parameter */
} filename_t;

/* Per-file information unique to an inode */
typedef struct _file {
  struct _file *duplicates;
  struct _file *next;
  filename_t *filename;
  dev_t device;
  jdupes_mode_t mode;
  off_t size;
  jdupes_ino_t inode;
  hash_t filehash_partial;
  hash_t filehash;
  time_t mtime;
  uint32_t flags;  /* Status flags */
#ifndef NO_PERMS
  uid_t uid;
  gid_t gid;
#endif
#ifndef NO_HARDLINKS
 #ifdef ON_WINDOWS
  DWORD nlink;
 #else
  nlink_t nlink;
 #endif
#endif
} file_t;

typedef struct _filetree {
  file_t *file;
  struct _filetree *left;
  struct _filetree *right;
#ifdef USE_TREE_REBALANCE
  struct _filetree *parent;
  unsigned int left_weight;
  unsigned int right_weight;
#endif /* USE_TREE_REBALANCE */
} filetree_t;

/* This gets used in many functions */
#ifdef ON_WINDOWS
extern struct winstat ws;
#else
extern struct stat s;
#endif

extern void oom(const char * const restrict msg);
extern void nullptr(const char * restrict func);
extern int file_has_changed(filename_t * const restrict filename);
extern int getfilestats(file_t * const restrict file);
extern int getdirstats(const char * const restrict name,
        jdupes_ino_t * const restrict file, dev_t * const restrict dev);
extern int check_conditions(const file_t * const restrict file1, const file_t * const restrict file2);
extern unsigned int get_max_dupes(const file_t *files, unsigned int * const restrict max,
		                unsigned int * const restrict n_files);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_H */
