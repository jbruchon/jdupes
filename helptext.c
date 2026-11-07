/* Help text and version information
 * This file is part of jdupes; see jdupes.c for license information */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>

#include <libjodycode.h>
#include "filehash.h"
#include "helptext.h"
#include "jdupes.h"
#include "version.h"


#ifndef NO_HELPTEXT
/* Assemble feature flag string from compile-time options */
const char *feature_flags[] = {
  #ifdef ENABLE_DEDUPE
  "dedupe",
  #endif
  #ifdef DEBUG
  "debug",
  #endif
  #ifdef __FAST_MATH__
  "fastmath",
  #endif
  #ifdef LOUD_DEBUG
  "loud",
  #endif
  #ifdef LOW_MEMORY
  "lowmem",
  #endif
  #ifdef NO_CHUNKSIZE
  "nochunk",
  #endif
  #ifdef NO_DELETE
  "nodel",
  #endif
  #ifdef NO_ERRORONDUPE
  "noeod",
  #endif
  #ifdef NO_EXTFILTER
  "noxf",
  #endif
  #ifdef NO_HARDLINKS
  "nohlink",
  #endif
  #ifdef NO_HASHDB
  "nohashdb",
  #endif
  #ifdef NO_NUMSORT
  "nojsort",
  #endif
  #ifdef NO_JSON
  "nojson",
  #endif
  #ifdef NO_GETOPT_LONG
  "nolongopt",
  #endif
  #ifdef NO_MTIME
  "nomtime",
  #endif
  #ifdef NO_PERMS
  "noperm",
  #endif
  #ifdef NO_SYMLINKS
  "noslink",
  #endif
  #ifdef NO_TRAVCHECK
  "notrav",
  #endif
  #ifdef NO_USER_ORDER
  "nouorder",
  #endif
  #ifdef NO_UNICODE
  "nounicode",
  #endif
  #ifdef UNICODE
  "unicode",
  #endif
  #ifdef ON_WINDOWS
  "windows",
  #endif
  NULL
};
#endif /* NO_HELPTEXT */


void help_text(void)
{
#ifndef NO_HELPTEXT
  printf("\nUsage: jdupes [options] FILES and/or DIRECTORIES...\n\n");

  printf("Duplicate file sets will be printed by default unless a different action\n");
  printf("option is specified (delete, summarize, link, dedupe, etc.)\n");
#ifdef NO_GETOPT_LONG
  printf("\nWARNING: getopt_long disabled in this build! Long options will not work.\n\n");
#endif
#ifdef LOUD
  printf(" -@ --loud        \toutput annoying low-level debug info while running\n");
#endif
  printf(" -0 --print-null  \toutput nulls instead of CR/LF (like 'find -print0')\n");
  printf(" -1 --one-file-system\tdo not match files on different filesystems/devices\n");
  printf(" -A --no-hidden    \texclude hidden files from consideration\n");
#ifndef NO_HASHDB
  printf(" -b --hash-db-opts=opt\thash database options and operations\n");
  printf("                  \tUse '-b help' for extended hashdb options help\n");
#endif
#ifdef ENABLE_DEDUPE
  printf(" -B --dedupe      \tdo a copy-on-write (reflink/clone) deduplication\n");
#endif
#ifndef NO_CHUNKSIZE
  printf(" -C --chunk-size=#\toverride I/O chunk size; valid range is 0-18.\n");
  printf("                  \tUse '-C help' for detailed chunk size help\n");
#endif /* NO_CHUNKSIZE */
#ifndef NO_DELETE
  printf(" -d --delete      \tprompt user for files to preserve and delete all\n");
  printf("                  \tothers; important: under particular circumstances,\n");
  printf("                  \tdata may be lost when using this option together\n");
  printf("                  \twith -s or --symlinks, or when specifying a\n");
  printf("                  \tparticular directory more than once; refer to the\n");
  printf("                  \tdocumentation for additional information\n");
#endif /* NO_DELETE */
#ifdef DEBUG
  printf(" -D --debug       \toutput debug statistics after completion\n");
#endif
#ifndef NO_ERRORONDUPE
  printf(" -e --error-on-dupe\texit on any duplicate found with status code 255\n");
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
#ifndef NO_JSON
  printf(" -j --json        \tproduce JSON (machine-readable) output\n");
#endif /* NO_JSON */
  printf(" -K --break-links \tsplit hard-linked file sets back into separate files\n");
  printf("                  \tThis is a partial 'undo' for the -L/--link-hard option\n");
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
#ifndef NO_DELETE
  printf(" -N --no-prompt   \ttogether with --delete, preserve the first file in\n");
  printf("                  \teach set of duplicates and delete the rest without\n");
  printf("                  \tprompting the user\n");
#endif /* NO_DELETE */
#ifndef NO_MTIME
  printf(" -o --order=BY    \tselect sort order for output, linking and deleting; by\n");
  printf("                  \tmtime (BY=time) or filename (BY=name, the default)\n");
#endif
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
  printf(" -u --print-unique\tprint only a list of unique (non-matched) files\n");
  printf(" -U --no-trav-check\tdisable double-traversal safety check (BE VERY CAREFUL)\n");
  printf("                  \tThis fixes a Google Drive File Stream recursion issue\n");
  printf(" -v --version     \tdisplay jdupes version and license information\n");
#ifndef NO_EXTFILTER
  printf(" -X --ext-filter=x:y\tfilter files based on specified criteria\n");
  printf("                  \tUse '-X help' for detailed extfilter help\n");
#endif /* NO_EXTFILTER */
#ifndef NO_HASHDB
  printf(" -y --hash-db=file\tuse a hash database text file to speed up repeat runs\n");
  printf("                  \tPassing '-y .' will expand to  '-y jdupes_hashdb.txt'\n");
#endif /* NO_HASHDB */
  printf(" -z --zero-match  \tconsider zero-length files to be duplicates\n");
  printf(" -Z --soft-abort  \tIf the user aborts (i.e. CTRL-C) act on matches so far\n");
#ifndef ON_WINDOWS
  printf("                  \tYou can send SIGUSR1 to the program to toggle this\n");
#endif

#else /* NO_HELPTEXT */
  version_text(0);
#endif /* NO_HELPTEXT */
  return;
}


void help_text_extfilter(void)
{
#ifndef NO_HELPTEXT
  printf("\nDetailed help for jdupes -X/--ext-filter options\n");
  printf("General format: jdupes -X filter[:value][size_suffix]\n\n");

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
#else /* NO_HELPTEXT */
  version_text(0);
#endif /* NO_HELPTEXT */
}


#ifndef NO_CHUNKSIZE
void help_text_chunksize(void)
{
#ifndef NO_HELPTEXT
  printf("\nDetailed help for jdupes -C/--chunk-size option\n\n");
  printf("This option overrides the I/O chunk size used to read file data. jdupes\n");
  printf("attempts to tune the chunk size based on your CPU L1 cache size, or at least\n");
  printf("use a sensible default for most systems. This maximizes raw throughput by \n");
  printf("minimizing cache flushes. For fast devices like PCI-Express solid-state drives\n");
  printf("(SSDs) or data that is already present in the operating system's disk caches,\n");
  printf("this is the fastest way to read file data.\n\n");
  printf("When reading from drives that have a \"seek penalty,\" this is NOT the fastest\n");
  printf("way because switching between reading two files imposes a massive time penalty.\n");
  printf("For example, if two files are ~6ms apart on a disk, the chunk size is 32K, and\n");
  printf("the files are 64M in size, there will be 2047 seeks = 12.8 extra seconds to\n");
  printf("compare the two files. Increasing the chunk size to 64M would read both files\n");
  printf("sequentially with only one seek, eliminating the penalty.\n\n");
  printf("Unfortunately, larger chunks will increase cache misses and place more memory\n");
  printf("pressure on the underlying OS, reducing overall performance. It is only\n");
  printf("appropriate to use large chunks when the performance boost from reducing seek \n");
  printf("delays will be greater than the slowdown from cache and RAM penalties imposed\n");
  printf("by the larger chunks. This usually means that -C is useful with mechanical\n");
  printf("hard drives and many large (> 64K) files.\n\n");
  printf("Valid values are 0-18 and represent sizes in powers of two:\n");
  printf("0 = 4K    1 = 8K    2 = 16K   3 = 32K   4 = 64K   5 = 128K  6 = 256K  7 = 512K\n");
  printf("8 = 1M    9 = 2M    10= 4M    11= 8M    12= 16M   13= 32M   14= 64M   15= 128M\n");
  printf("16= 256M  17= 512M  18= 1G\n");
#else /* NO_HELPTEXT */
  version_text(0);
#endif /* NO_HELPTEXT */
}
#endif /* NO_CHUNKSIZE */


void version_text(int short_version)
{
  printf("jdupes %s (%s) ", VER, VERDATE);

#ifndef NO_HELPTEXT
  /* Indicate bitness information */
  if (sizeof(uintptr_t) == 8) {
    if (sizeof(long) == 4) printf("64-bit i32");
    else if (sizeof(long) == 8) printf("64-bit");
  } else if (sizeof(uintptr_t) == 4) {
    if (sizeof(long) == 4) printf("32-bit");
    else if (sizeof(long) == 8) printf("32-bit i64");
#if defined(__x86_64__) && SIZE_MAX == 0xffffffff
    printf(" (x32 ABI)");
#endif
  } else printf("%u-bit i%u", (unsigned int)(sizeof(uintptr_t) * 8),
      (unsigned int)(sizeof(long) * 8));
  if (!short_version) {
    printf(", linked to libjodycode %s (%s)\n", jc_version, jc_verdate);
    printf("Hash algorithms available:");
    for (int i = 0; i < HASH_ALGO_COUNT; i++) printf(" %s%c", hash_algo_list[i], i == (HASH_ALGO_COUNT - 1) ? '\n' : ',');
  } else printf("\n");

  printf("Compile-time feature flags:");
  if (*feature_flags != NULL) {
    int c = 0;
    while (feature_flags[c] != NULL) {
      printf(" %s", feature_flags[c]);
      c++;
    }
  } else printf(" none");
  printf("\n");
  if (short_version) return;
  printf("Copyright (C) 2015-2026 by Jody Bruchon and contributors\n\n");
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
  printf("Report bugs, get new releases, or learn about jdupes: http://www.jdupes.com/\n");
#else
  (void)short_version;
  printf("\nBuilt with no help text. You're on your own.\n");
#endif /* NO_HELPTEXT */
  return;
}


#ifndef NO_HASHDB
void help_text_hashdb_opts(void)
{
#ifndef NO_HELPTEXT
  printf("\nDetailed help for jdupes -b/--hash-db-opts options\n");
  printf("Note that -y/--hash-db is required for these to have any effect!\n");
  printf("General format: jdupes -b option[[:|=]option_modifiers]\n\n");

  printf("cleanup\n");
  printf("\n");
  printf("  Invalidates all entries in the loaded hash database that have either\n");
  printf("  been deleted or changed since the database was last written. This is\n");
  printf("  a destructive cleanup that can significantly reduce the size of the\n");
  printf("  database after large deletions. Changes are written on program exit.\n\n");

  printf("backup=backup_hashdb_name\n");
  printf("\n");
  printf("  If a new hashdb is written, rename the old hashdb to given backup_hashdb_name\n");
  printf("  Using backup=. will set the backup database name to 'jdupes_hashdb_old.txt'\n\n");

  printf("populate\n");
  printf("\n");
  printf("  Don't duplicate scan, only pre-populate the hashdb with complete hash data\n");
#else /* NO_HELPTEXT */
  version_text(0);
#endif /* NO_HELPTEXT */
}
#endif /* NO_HASHDB */
