## Introduction

jdupes is a program for identifying and taking actions upon duplicate
files. This fork known as 'jdupes' is heavily modified from and improved
over the original. See [CHANGES](https://github.com/jbruchon/jdupes/blob/master/CHANGES.md) for details.

>**A WORD OF WARNING**: jdupes **IS NOT** a drop-in compatible replacement for
fdupes! Do not blindly replace fdupes with jdupes in scripts and expect
everything to work the same way. Option availability and meanings differ
between the two programs. For example, the `-I` switch in jdupes means
"isolate" and blocks intra-argument matching, while in fdupes it means
"immediately delete files during scanning without prompting the user."

#### Installation

See [installation guide](https://github.com/jbruchon/jdupes/blob/master/INSTALL.md).


## Why use jdupes instead of the original fdupes or other forks?

The biggest reason is raw speed. In testing on various data sets, jdupes is
over 7 times faster than fdupes-1.51 on average.

jdupes is the only Windows port of fdupes. Most duplicate scanners built on
Linux and other UNIX-like systems do not compile for Windows out-of-the-box
and even if they do, they don't support Unicode and other Windows-specific
quirks and features.

jdupes is generally stable. All releases of jdupes are compared against a
known working reference version of fdupes or jdupes to be certain that
output does not change. You get the benefits of an aggressive development
process without putting your data at increased risk.

Code in jdupes is written with data loss avoidance as the highest priority.
If a choice must be made between being aggressive or careful, the careful
way is always chosen.

jdupes includes features that are not found in fdupes. Examples of
such features include btrfs block-level deduplication and control over
which file is kept when a match set is automatically deleted. jdupes is
not afraid of dropping features of low value; a prime example is the `-1`
switch which outputs all matches in a set on one line, a feature which was
found to be useless in real-world tests and therefore thrown out.

The downside is that jdupes development is never guaranteed to be bug-free!
If the program eats your dog or sets fire to your lawn, the authors cannot
be held responsible. If you notice a bug, please report it.

While jdupes maintains some degree of compatibility with fdupes from which
it was originally derived, there is no guarantee that it will continue to
maintain such compatibility in the future.


## What jdupes is not: a similar (but not identical) file finding tool

Please note that jdupes **ONLY** works on 100% exact matches. It does not have
any sort of "similarity" matching, nor does it know anything about any
specific file formats such as images or sounds. Something as simple as a
change in embedded metadata such as the ID3 tags in an MP3 file or the EXIF
information in a JPEG image will not change the sound or image presented to
the user when opened, but technically it makes the file no longer identical
to the original.

Plenty of excellent tools already exist to "fuzzy match" specific file types
using knowledge of their file formats to help. There are no plans to add
this type of matching to jdupes.


## Usage

```
Usage: jdupes [options] DIRECTORY...

 -A --nohidden          exclude hidden files from consideration
 -d --delete            prompt user for files to preserve and delete all
                        others; important: under particular circumstances,
                        data may be lost when using this option together
                        with -s or --symlinks, or when specifying a
                        particular directory more than once; refer to the
                        documentation for additional information
 -f --omitfirst         omit the first file in each set of matches
 -h --help              display this help message
 -H --hardlinks         treat hard-linked files as duplicate files. Normally
                        hard links are treated as non-duplicates for safety
 -i --reverse           reverse (invert) the match sort order
 -I --isolate           files in the same specified directory won't match
 -L --linkhard          hard link duplicate files to the first file in
                        each set of duplicates without prompting the user
 -m --summarize         summarize dupe information
 -n --noempty           exclude zero-length files from consideration
 -N --noprompt          together with --delete, preserve the first file in
                        each set of duplicates and delete the rest without
                        prompting the user
 -o --order=BY          select sort order for output, linking and deleting; by
                        mtime (BY=time) or filename (BY=name, the default)
 -O --paramorder        Parameter order is more important than selected -O sort
 -p --permissions       don't consider files with different owner/group or
                        permission bits as duplicates
 -r --recurse           for every directory given follow subdirectories
                        encountered within
 -R --recurse:          for each directory given after this option follow
                        subdirectories encountered within (note the ':' at
                        the end of the option, manpage for more details)
 -s --symlinks          follow symlinks
 -S --size              show size of duplicate files
 -q --quiet             hide progress indicator
 -v --version           display jdupes version and license information
 -x --xsize=SIZE        exclude files of size < SIZE bytes from consideration
    --xsize=+SIZE       '+' specified before SIZE, exclude size > SIZE
                        K/M/G size suffixes can be used (case-insensitive)
 -Z --softabort         If the user aborts (i.e. CTRL-C) act on matches so far
 ```

Duplicate files are listed together in groups with each file displayed on a
Separate line. The groups are then separated from each other by blank lines.

When using `-d` or `--delete`, care should be taken to insure against
accidental data loss. While no information will be immediately
lost, using this option together with `-s` or `--symlink` can lead
to confusing information being presented to the user when prompted
for files to preserve. Specifically, a user could accidentally
preserve a symlink while deleting the file it points to. A similar
problem arises when specifying a particular directory more than
once. All files within that directory will be listed as their own
duplicates, leading to data loss should a user preserve a file
without its "duplicate" (the file itself!).

The `-I` or `--isolate` option attempts to block matches that are contained in
the same specified directory parameter on the command line. Due to the
underlying nature of the jdupes algorithm, a lot of matches will be
blocked by this option that probably should not be. This code could use
improvement.


## Hard linking status symbols and behavior

A set of arrows are used in hard linking to show what action was taken on
each link candidate.  
These arrows are as follows:  
`---->` File was hard linked to the first file in the duplicate chain

`-==->` Already a hard link to the first file in the chain

`-//->` Hard linking failed due to an error during the linking process

If your data set has hard linked files and you do not use `-L` to always
consider them as duplicates, you may still see hard linked files appear
together in match sets. This is caused by a separate file that matches
the hard linked files and is the correct behavior.

## Microsoft Windows platform-specific notes

The Windows port does not support Unicode, only ANSI file names. This is
because Unicode support on Windows is difficult to add to existing code
without making it very messy or breaking things. Support is eventually
planned for Unicode on Windows.

Windows has a hard limit of 1024 hard links per file. There is no way to
change this. The documentation for CreateHardLink() states: "The maximum
number of hard links that can be created with this function is 1023 per
file. If more than 1023 links are created for a file, an error results."
(The number is actually 1024, but they're ignoring the first file.)


## Contact Information

For all jdupes inquiries, contact Jody Bruchon <jody@jodybruchon.com>
Please do NOT contact Adrian Lopez about issues with jdupes!


## Legal Information

jdupes is Copyright (C) 2015-2016 by Jody Bruchon
Derived from the original 'fdupes' (C) 1999-2016 by Adrian Lopez
Includes jody_hash (C) 2015-2016 Jody Bruchon <jody@jodybruchon.com>

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
