Introduction
--------------------------------------------------------------------------
jdupes is a program for identifying and taking actions upon duplicate
files.

A WORD OF WARNING: jdupes IS NOT a drop-in compatible replacement for
fdupes! Do not blindly replace fdupes with jdupes in scripts and expect
everything to work the same way. Option availability and meanings differ
between the two programs. For example, the `-I` switch in jdupes means
"isolate" and blocks intra-argument matching, while in fdupes it means
"immediately delete files during scanning without prompting the user."

Please consider financially supporting continued developemnt of jdupes:

https://www.subscribestar.com/JodyBruchon


v1.15+ specific: Why is the addition of single files not working?
--------------------------------------------------------------------------
If a file was added through recursion and also added explicitly, that file
would end up matching itself. This issue can be seen in v1.14.1 or older
versions that support single file addition using a command like this in
the jdupes source code directory:

/usr/src/jdupes$ jdupes -rH testdir/isolate/1/ testdir/isolate/1/1.txt
testdir/isolate/1/1.txt
testdir/isolate/1/1.txt
testdir/isolate/1/2.txt

Even worse, using the special dot directory will make it happen without
the -H option, which is how I discovered this bug:


/usr/src/jdupes/testdir/isolate/1$ jdupes . 1.txt
./1.txt
./2.txt
1.txt

This works for any path with a single dot directory anywhere in the path,
so it has a good deal of potential for data loss in some use cases. As
such, the best option was to shove out a new minor release with this
feature turned off until some additional checking can be done, e.g. by
making sure the canonical paths aren't identical between any two files.

A future release will fix this safely.


Why use jdupes instead of the original fdupes or other duplicate finders?
--------------------------------------------------------------------------
The biggest reason is raw speed. In testing on various data sets, jdupes is
over 7 times faster than fdupes-1.51 on average.

jdupes provides a native Windows port. Most duplicate scanners built on
Linux and other UNIX-like systems do not compile for Windows out-of-the-box
and even if they do, they don't support Unicode and other Windows-specific
quirks and features.

jdupes is generally stable. All releases of jdupes are compared against a
known working reference versions of fdupes or jdupes to be certain that
output does not change. You get the benefits of an aggressive development
process without putting your data at increased risk.

Code in jdupes is written with data loss avoidance as the highest priority.
If a choice must be made between being aggressive or careful, the careful
way is always chosen.

jdupes includes features that are not always found elsewhere. Examples of
such features include block-level data deduplication and control over
which file is kept when a match set is automatically deleted. jdupes is
not afraid of dropping features of low value; a prime example is the `-1`
switch which outputs all matches in a set on one line, a feature which was
found to be useless in real-world tests and therefore thrown out.

While jdupes maintains some degree of compatibility with fdupes from which
it was originally derived, there is no guarantee that it will continue to
maintain such compatibility in the future. However, compatibility will be
retained between minor versions, i.e. jdupes-1.6 and jdupes-1.6.1 should
not have any significant differences in results with identical command
lines.

If the program eats your dog or sets fire to your lawn, the authors cannot
be held responsible. If you notice a bug, please report it.


What jdupes is not: a similar (but not identical) file finding tool
--------------------------------------------------------------------------
Please note that jdupes ONLY works on 100% exact matches. It does not have
any sort of "similarity" matching, nor does it know anything about any
specific file formats such as images or sounds. Something as simple as a
change in embedded metadata such as the ID3 tags in an MP3 file or the EXIF
information in a JPEG image will not change the sound or image presented to
the user when opened, but technically it makes the file no longer identical
to the original.

Plenty of excellent tools already exist to "fuzzy match" specific file types
using knowledge of their file formats to help. There are no plans to add
this type of matching to jdupes.

There are some match options available in jdupes that enable dangerous file
matching based on partial or likely but not 100% certain matching. These
are considered expert options for special situations and are clearly and
loudly documented as being dangerous. The `-Q` and `-T` options are notable
examples, and the extreme danger of the `-T` option is safeguarded by a
requirement to specify it twice so it can't be used accidentally.


How can I do stuff with jdupes that isn't supported by jdupes?
--------------------------------------------------------------------------
The standard output format of jdupes is extremely simple. Match sets are
presented with one file path per line, and match sets are separated by a
blank line. This is easy to process with fairly simple shell scripts. You
can find example shell scripts in the "example scripts" directory in the
jdupes source code. The main example script, "example.sh", is easy to
modify to take basic actions on each file in a match set. These scripts
are used by piping the standard jdupes output to them:

jdupes dir1 dir2 dir3 | example.sh scriptparameters


Usage
--------------------------------------------------------------------------
```
Usage: jdupes [options] DIRECTORY...
```

Duplicate file sets will be printed by default unless a different action
option is specified (delete, summarize, link, dedupe, etc.)

```
 -@ --loud              output annoying low-level debug info while running
 -0 --printnull         output nulls instead of CR/LF (like 'find -print0')
 -1 --one-file-system   do not match files on different filesystems/devices
 -A --nohidden          exclude hidden files from consideration
 -B --dedupe            do a copy-on-write (reflink/clone) deduplication
 -C --chunksize=#       override I/O chunk size (min 4096, max 16777216)
 -d --delete            prompt user for files to preserve and delete all
                        others; important: under particular circumstances,
                        data may be lost when using this option together
                        with -s or --symlinks, or when specifying a
                        particular directory more than once; refer to the
                        documentation for additional information
 -D --debug             output debug statistics after completion
 -f --omitfirst         omit the first file in each set of matches
 -h --help              display this help message
 -H --hardlinks         treat any linked files as duplicate files. Normally
                        linked files are treated as non-duplicates for safety
 -i --reverse           reverse (invert) the match sort order
 -I --isolate           files in the same specified directory won't match
 -j --json              produce JSON (machine-readable) output
 -K --skiphash            skip full file hashing (may be faster; 100% safe)
 -l --linksoft          make relative symlinks for duplicates w/o prompting
 -L --linkhard          hard link all duplicate files without prompting
                        Windows allows a maximum of 1023 hard links per file
 -m --summarize         summarize dupe information
 -M --printwithsummary  will print matches and --summarize at the end
 -N --noprompt          together with --delete, preserve the first file in
                        each set of duplicates and delete the rest without
                        prompting the user
 -o --order=BY          select sort order for output, linking and deleting:
                        by mtime (BY=time) or filename (BY=name, the default)
 -O --paramorder        sort output files in order of command line parameter sequence
                        Parameter order is more important than selected -o sort
                        which applies should several files share the same parameter order
 -p --permissions       don't consider files with different owner/group or
                        permission bits as duplicates
 -P --print=type        print extra info (partial, early, fullhash)
 -q --quiet             hide progress indicator
 -Q --quick             skip byte-by-byte duplicate verification. WARNING:
                        this may delete non-duplicates! Read the manual first!
 -r --recurse           for every directory, process its subdirectories too
 -R --recurse:          for each directory given after this option follow
                        subdirectories encountered within (note the ':' at
                        the end of the option, manpage for more details)
 -s --symlinks          follow symlinks
 -S --size              show size of duplicate files
 -t --nochangecheck     disable security check for file changes (aka TOCTTOU)
 -T --partial-only      match based on partial hashes only. WARNING:
                        EXTREMELY DANGEROUS paired with destructive actions!
                        -T must be specified twice to work. Read the manual!
 -u --printunique       print only a list of unique (non-matched) files
 -U --notravcheck 	disable double-traversal safety check (BE VERY CAREFUL)
 -v --version           display jdupes version and license information
 -x --xsize=SIZE        exclude files of size < SIZE bytes from consideration
    --xsize=+SIZE       '+' specified before SIZE, exclude size > SIZE
 -X --extfilter=x:y     filter files based on specified criteria
                        Use '-X help' for detailed extfilter help
 -z --zeromatch         consider zero-length files to be duplicates
 -Z --softabort         If the user aborts (i.e. CTRL-C) act on matches so far
                        You can send SIGUSR1 to the program to toggle this


Detailed help for jdupes -X/--extfilter options
General format: jdupes -X filter[:value][size_suffix]

noext:ext1[,ext2,...]           Exclude files with certain extension(s)

onlyext:ext1[,ext2,...]         Only include files with certain extension(s)

size[+-=]:size[suffix]          Exclude files meeting certain size criteria
                                Size specs: + larger, - smaller, = equal to
                                Specs can be mixed, i.e. size+=:100k will
                                exclude files 100KiB or larger in size.

nostr:text_string               Exclude all paths containing the string
onlystr:text_string             Only allow paths containing the string
                                HINT: you can use these for directories:
                                -X nostr:/dir_x/  or  -X onlystr:/dir_x/
newer:datetime                  Reject files newer than the specified date
older:datetime                  Reject files newer than the specified date
                                Date/time format: "YYYY-MM-DD HH:MM:SS"
                                Time is optional (remember to escape spaces!)

Some filters take no value or multiple values. Filters that can take
a numeric option generally support the size multipliers K/M/G/T/P/E
with or without an added iB or B. Multipliers are binary-style unless
the B is used, which will use decimal multipliers. For example,
10k or 10kib = 10240; 10kb = 10000. Multipliers are case-insensitive.

Filters have cumulative effects: jdupes -X size+:100 -X size-:100 will
cause only files of exactly 100 bytes in size to be included.
```

For sizes, K/M/G/T/P/E[B|iB] suffixes can be used (case-insensitive)

The `-U`/`--notravcheck` option disables the double-traversal prevention tree.
In the VAST MAJORITY of circumstances, this SHOULD NOT BE DONE, as it protects
against several dangerous user errors, including specifying the same files or
directories twice causing them to match themselves and potentially be lost or
irreversibly damaged, or a symbolic link to a directory making an endless loop
of recursion that will cause the program to hang indefinitely. This option was
added because Google Drive File Stream presents directories in the virtual hard
drive used by GDFS with identical device:inode pairs despite the directories
actually being different. This triggers double-traversal prevention against
every directory, effectively blocking all recursion. Disabling this check will
reduce safety, but will allow duplicate scanning inside Google Drive File
Stream drives. This also results in a very minor speed boost during recursion,
but the boost is unlikely to be noticeable.

The `-t`/`--nochangecheck` option disables file change checks during/after
scanning. This opens a security vulnerability that is called a TOCTTOU (time
of check to time of use) vulnerability. The program normally runs checks
immediately before scanning or taking action upon a file to see if the file
has changed in some way since it was last checked. With this option enabled,
the program will not run any of these checks, making the algorithm slightly
faster, but also increasing the risk that the program scans a file, the file
is changed after the scan, and the program still acts like the file was in
its previous state. This is particularly dangerous when considering actions
such as linking and deleting. In the most extreme case, a file could be
deleted during scanning but match other files prior to that deletion; if the
file is the first in the list of duplicates and auto-delete is used, all of
the remaining matched files will be deleted as well. This option was added
due to user reports of some filesystems (particularly network filesystems)
changing the reported file information inappropriately, rendering the entire
program unusable on such filesystems.

The `-n`/`--noempty` option was removed for safety. Matching zero-length files as
duplicates now requires explicit use of the `-z`/`--zeromatch` option instead.

Duplicate files are listed together in groups with each file displayed on a
separate line. The groups are then separated from each other by blank lines.

The `-s`/`--symlinks` option will treat symlinked files as regular files, but
direct symlinks will be treated as if they are hard linked files and the
-H/--hardlinks option will apply to them in the same manner.

When using `-d` or `--delete`, care should be taken to insure against accidental
data loss. While no information will be immediately lost, using this option
together with `-s` or `--symlink` can lead to confusing information being
presented to the user when prompted for files to preserve. Specifically, a
user could accidentally preserve a symlink while deleting the file it points
to. A similar problem arises when specifying a particular directory more
than once. All files within that directory will be listed as their own
duplicates, leading to data loss should a user preserve a file without its
"duplicate" (the file itself!)

Using `-1` or `--one-file-system` prevents matches that cross filesystems, but a
more relaxed form of this option may be added that allows cross-matching for
all filesystems that each parameter is present on.

`-Z` or `--softabort` used to be `--hardabort` in jdupes prior to v1.5 and had the
opposite behavior. Defaulting to taking action on abort is probably not what
most users would expect. The decision to invert rather than reassign to a
different option was made because this feature was still fairly new at the
time of the change.

On non-Windows platforms that support SIGUSR1, you can toggle the state of
the `-Z` option by sending a SIGUSR1 to the program. This is handy if you want
to abort jdupes, didn't specify `-Z`, and changed your mind and don't want to
lose all the work that was done so far. Just do '`killall -USR1 jdupes`' and
you wll be able to abort with `-Z`. This works in reverse: if you want to
prevent a `-Z` from happening, a SIGUSR1 will toggle it back off. That's a lot
less useful because you can just stop and kill the program to get the same
effect, but it's there if you want it for some reason. Sending the signal
twice while the program is stopped will behave as if it was only sent once,
as per normal POSIX signal behavior.

The `-O` or `--paramorder` option allows the user greater control over what
appears in the first position of a match set, specifically for keeping the `-N`
option from deleting all but one file in a set in a seemingly random way. All
directories specified on the command line will be used as the sorting order
of result sets first, followed by the sorting algorithm set by the `-o` or
`--order` option. This means that the order of all match pairs for a single
directory specification will retain the old sorting behavior even if this
option is specified.

When used together with options `-s` or `--symlink`, a user could accidentally
preserve a symlink while deleting the file it points to.

The `-Q` or `--quick option` only reads each file once, hashes it, and performs
comparisons based solely on the hashes. There is a small but significant risk
of a hash collision which is the purpose of the failsafe byte-for-byte
comparison that this option explicitly bypasses. Do not use it on ANY data
set for which any amount of data loss is unacceptable. You have been warned!

The `-T` or `--partial-only` option produces results based on a hash of the first
block of file data in each file, ignoring everything else in the file.
Partial hash checks have always been an important exclusion step in the
jdupes algorithm, usually hashing the first 4096 bytes of data and allowing
files that are different at the start to be rejected early. In certain
scenarios it may be a useful heuristic for a user to see that a set of files
has the same size and the same starting data, even if the remaining data does
not match; one example of this would be comparing files with data blocks that
are damaged or missing such as an incomplete file transfer or checking a data
recovery against known-good copies to see what damaged data can be deleted in
favor of restoring the known-good copy. This option is meant to be used with
informational actions and can result in EXTREME DATA LOSS if used with
options that delete files, create hard links, or perform other destructive
actions on data based on the matching output. Because of the potential for
massive data destruction, this option MUST BE SPECIFIED TWICE to take effect
and will error out if it is only specified once.

The `-I`/`--isolate` option attempts to block matches that are contained in
the same specified directory parameter on the command line. Due to the
underlying nature of the jdupes algorithm, a lot of matches will be
blocked by this option that probably should not be. This code could use
improvement.

The `-C`/`--chunksize` option overrides the size of the I/O "chunk" used for all
file operations. Larger numbers will increase the amount of data read at
once from each file and may improve performance when scanning lots of files
that are larger than the default chunk size by reducing "thrashing" of the
hard disk heads. Smaller numbers may increase algorithm speed depending on
the characteristics of your CPU but will usually increase I/O and system
call overhead as well. The number also directly affects memory usage: I/O
chunk size is used for at least three allocations in the program, so using
a chunk size of 16777216 (16 MiB) will require 48 MiB of RAM. The default
is usually between 32768 and 65536 which results in the fastest raw speed
of the algorithm and generally good all-around performance. Feel free to
experiment with the number on your data set and report your experiences
(preferably with benchmarks and info on your data set.)

Using `-P`/`--print` will cause the program to print extra information that
may be useful but will pollute the output in a way that makes scripted
handling difficult. Its current purpose is to reveal more information about
the file matching process by printing match pairs that pass certain steps
of the process prior to full file comparison. This can be useful if you
have two files that are passing early checks but failing after full checks.


Hard and soft (symbolic) linking status symbols and behavior
--------------------------------------------------------------------------
A set of arrows are used in file linking to show what action was taken on
each link candidate. These arrows are as follows:

`---->` File was hard linked to the first file in the duplicate chain

`-@@->` File was symlinked to the first file in the chain

`-==->` Already a hard link to the first file in the chain

`-//->` File linking failed due to an error during the linking process

If your data set has linked files and you do not use `-H` to always consider
them as duplicates, you may still see linked files appear together in match
sets. This is caused by a separate file that matches with linked files
independently and is the correct behavior. See notes below on the "triangle
problem" in jdupes for technical details.


Microsoft Windows platform-specific notes
--------------------------------------------------------------------------
Windows has a hard limit of 1024 hard links per file. There is no way to
change this. The documentation for CreateHardLink() states: "The maximum
number of hard links that can be created with this function is 1023 per
file. If more than 1023 links are created for a file, an error results."
(The number is actually 1024, but they're ignoring the first file.)


The current jdupes algorithm's "triangle problem"
--------------------------------------------------------------------------
Pairs of files are excluded individually based on how the two files compare.
For example, if `--hardlinks` is not specified then two files which are hard
linked will not match one another for duplicate scanning purposes. The
problem with only examining files in pairs is that certain circumstances
will lead to the exclusion being overridden.

Let's say we have three files with identical contents:

```
a/file1
a/file2
a/file3
```

and `a/file1` is linked to `a/file3`. Here's how `jdupes a/` sees them:

---
        Are 'a/file1' and 'a/file2' matches? Yes
        [point a/file1->duplicates to a/file2]

        Are 'a/file1' and 'a/file3' matches? No (hard linked already, `-H` off)

        Are 'a/file2' and 'a/file3' matches? Yes
        [point a/file2->duplicates to a/file3]
---

Now you have the following duplicate list:

```
a/file1->duplicates ==> a/file2->duplicates ==> a/file3
```

The solution is to split match sets into multiple sets, but doing this
will also remove the guarantee that files will only ever appear in one
match set and could result in data loss if handled improperly. In the
future, options for "greedy" and "sparse" may be introduced to switch
between allowing triangle matches to be in the same set vs. splitting
sets after matching finishes without the "only ever appears once"
guarantee.


Does jdupes meet the "Good Practice when Deleting Duplicates" by rmlint?
--------------------------------------------------------------------------
Yes. If you've not read this list of cautions, it is available at
http://rmlint.readthedocs.io/en/latest/cautions.html

Here's a breakdown of how jdupes addresses each of the items listed.

### "Backup your data"/"Measure twice, cut once"
These guidelines are for the user of duplicate scanning software, not the
software itself. Back up your files regularly. Use jdupes to print a list
of what is found as duplicated and check that list very carefully before
automatically deleting the files.

### "Beware of unusual filename characters"
The only character that poses a concern in jdupes is a newline `\n` and
that is only a problem because the duplicate set printer uses them to
separate file names. Actions taken by jdupes are not parsed like a
command line, so spaces and other weird characters in names aren't a
problem. Escaping the names properly if acting on the printed output is a
problem for the user's shell script or other external program.

### "Consider safe removal options"
This is also an exercise for the user.

### "Traversal Robustness"
jdupes tracks each directory traversed by dev:inode pair to avoid adding
the contents of the same directory twice. This prevents the user from
being able to register all of their files twice by duplicating an entry
on the command line. Symlinked directories are only followed if they
weren't already followed earlier. Files are renamed to a temporary name
before any linking is done and if the link operation fails they are renamed
back to the original name.

### "Collision Robustness"
jdupes uses jodyhash for file data hashing. This hash is extremely fast
with a low collision rate, but it still encounters collisions as any hash
function will ("secure" or otherwise) due to the pigeonhole principle. This
is why jdupes performs a full-file verification before declaring a match.
It's slower than matching by hash only, but the pigeonhole principle puts
all data sets larger than the hash at risk of collision, meaning a false
duplicate detection and data loss. The slower completion time is not as
important as data integrity. Checking for a match based on hashes alone
is irresponsible, and using secure hashes like MD5 or the SHA families
is orders of magnitude slower than jodyhash while still suffering from
the risk brought about by the pigeonholing. An example of this problem is
as follows: if you have 365 days in a year and 366 people, the chance of
having at least two birthdays on the same day is guaranteed; likewise,
even though SHA512 is a 512-bit (64-byte) wide hash, there are guaranteed
to be at least 256 pairs of data streams that causes a collision once any
of the data streams being hashed for comparison is 65 bytes (520 bits) or
larger.

### "Unusual Characters Robustness"
jdupes does not protect the user from putting ASCII control characters in
their file names; they will mangle the output if printed, but they can
still be operated upon by the actions (delete, link, etc.) in jdupes.

### "Seek Thrash Robustness"
jdupes uses an I/O chunk size that is optimized for reading as much as
possible from disk at once to take advantage of high sequential read
speeds in traditional rotating media drives while balancing against the
significantly higher rate of CPU cache misses triggered by an excessively
large I/O buffer size. Enlarging the I/O buffer further may allow for
lots of large files to be read with less head seeking, but the CPU cache
misses slow the algorithm down and memory usage increases to hold these
large buffers. jdupes is benchmarked periodically to make sure that the
chosen I/O chunk size is the best compromise for a wide variety of data
sets.

### "Memory Usage Robustness"
This is a very subjective concern considering that even a cell phone in
someone's pocket has at least 1GB of RAM, however it still applies in the
embedded device world where 32MB of RAM might be all that you can have.
Even when processing a data set with over a million files, jdupes memory
usage (tested on Linux x86_64 with -O3 optimization) doesn't exceed 2GB.
A low memory mode can be chosen at compile time to reduce overall memory
usage with a small performance penalty.


Contact information
--------------------------------------------------------------------------
For all jdupes inquiries, contact Jody Bruchon <jody@jodybruchon.com>
Please DO NOT contact Adrian Lopez about issues with jdupes.


Legal information and software license
--------------------------------------------------------------------------
jdupes is Copyright (C) 2015-2020 by Jody Bruchon <jody@jodybruchon.com>
Derived from the original 'fdupes' 1.51 (C) 1999-2014 by Adrian Lopez
Includes other code libraries which are (C) 2015-2020 by Jody Bruchon

The MIT License

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
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
