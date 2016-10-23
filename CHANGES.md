jdupes 1.5

 - Invert -Z option: only "soft abort" if asked explicitly to do so
 - Tweak internal data chunk size to reduce data cache misses
 - Fix partial hash optimization
 - Change PREFIX for building from /usr/local back to /usr

jdupes 1.4

 - Add support for Unicode file paths on Windows platforms
 - Discard floating point code of dubious value
 - Remove -1/--sameline feature which is not practically useful
 - Process partially complete duplicate scan if CTRL+C is pressed
 - Add -Z/--hardabort option to disable the new CTRL+C behavior
 - Add [n]one option to -d/--delete to discard all files in a match set
 - Minor bug fixes and tweaks to improve behavior

jdupes 1.3

 - Add -i/--reverse to invert the match sort order
 - Add -I/--isolate to force cross-parameter matching
 - Add "loud" debugging messages (-@ switch, build with 'make LOUD=1')

jdupes 1.2.1

 - Fix a serious bug that caused some duplicates to be missed

jdupes 1.2

 - Change I/O block size for improved performance
 - Improved progress indicator behavior with large files; now the progress
  indicator will update more frequently when full file reads are needed
 - Windows read speed boost with _O_SEQUENTIAL file flag
 - Experimental tree rebalance code tuning

jdupes 1.1.1

 - Fix a bug where the -r switch was always on even if not specified

jdupes 1.1

 - Work around the 1023-link limit for Windows hard linking so that linking
  can continue even when the limit is reached
 - Update documentation to include hard link arrow explanations
 - Add "time of check to time of use" checks immediately prior to taking
  actions on files so that files which changed since being checked will not
  be touched, avoiding potential data loss on "live" data sets
 - Add debug stats for files skipped due to Windows hard link limit
 - Change default sort to filename instead of modification time
 - Replaced Windows "get inode number" code with simpler, faster version
 - Fixed a bug where an extra newline was at the end of printed matches
 - Reduced progress delay interval; it was a bit slow on many large files

jdupes 1.0.2

 - Update jody_hash code to latest version
 - Change string_malloc to enable future string_free() improvements
 - Add string_malloc counters for debug stat mode
 - Add '+size' option to -x/--xsize switch to exclude files larger than the
  specified size instead of smaller than that size

jdupes 1.0.1

 - Fix bug in deletion set counter that would show e.g. "Set 1 of 0"
 - Minor size reductions by merging repeated fixed strings
 - Add feature flag 'fastmath' to show when compiled with -ffast-math
 - Corrections to code driven by -Wconversion and -Wwrite-strings


jdupes 1.0

First release. For changes before the 'jdupes' name change, see OLD_CHANGES
