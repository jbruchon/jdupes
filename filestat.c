/* jdupes (C) 2015-2026 Jody Bruchon <jody@jodybruchon.com>

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
#include <libjodycode.h>
#include "jdupes.h"
#include "likely_unlikely.h"

/* Check file's stat() info to make sure nothing has changed
 * Returns 1 if changed, 0 if not changed, negative if error */
int file_has_changed(file_t * const restrict file)
{
  struct JC_STAT s;

  /* If -t/--no-change-check specified then completely bypass this code */
  if (ISFLAG(flags, F_NOCHANGECHECK)) return 0;

  if (unlikely(file == NULL || file->d_name == NULL)) jc_nullptr("file_has_changed()");
  LOUD(fprintf(stderr, "file_has_changed('%s')\n", file->d_name);)

  if (!ISFLAG(file->flags, FF_VALID_STAT)) return -66;

  if (jc_stat(file->d_name, &s) != 0) return -2;
  if (file->inode != s.st_ino) return 1;
  if (file->size != s.st_size) return 1;
  if (file->device != s.st_dev) return 1;
  if (file->mode != s.st_mode) return 1;
#ifndef NO_MTIME
  if (file->mtime != s.st_mtime) return 1;
#endif
#ifndef NO_PERMS
  if (file->uid != s.st_uid) return 1;
  if (file->gid != s.st_gid) return 1;
#endif
#ifndef NO_SYMLINKS
  if (lstat(file->d_name, &s) != 0) return -3;
  if ((JC_S_ISLNK(s.st_mode) > 0) ^ ISFLAG(file->flags, FF_IS_SYMLINK)) return 1;
#endif

  return 0;
}


int getfilestats(file_t * const restrict file)
{
  struct JC_STAT s;

  if (unlikely(file == NULL || file->d_name == NULL)) jc_nullptr("getfilestats()");
  LOUD(fprintf(stderr, "getfilestats('%s')\n", file->d_name);)

  /* Don't stat the same file more than once */
  if (ISFLAG(file->flags, FF_VALID_STAT)) return 0;
  SETFLAG(file->flags, FF_VALID_STAT);

  if (jc_stat(file->d_name, &s) != 0) return -1;
  file->size = s.st_size;
  file->inode = s.st_ino;
  file->device = s.st_dev;
#ifndef NO_MTIME
  file->mtime = s.st_mtime;
#endif
#ifndef NO_ATIME
  file->atime = s.st_atime;
#endif
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
  if (JC_S_ISLNK(s.st_mode) > 0) SETFLAG(file->flags, FF_IS_SYMLINK);
#endif
  return 0;
}


/* Returns -1 if stat() fails, 0 if it's a directory, 1 if it's not */
int getdirstats(const char * const restrict name,
        jdupes_ino_t * const restrict inode, dev_t * const restrict dev,
        jdupes_mode_t * const restrict mode)
{
  struct JC_STAT s;

  if (unlikely(name == NULL || inode == NULL || dev == NULL)) jc_nullptr("getdirstats");
  LOUD(fprintf(stderr, "getdirstats('%s', %p, %p)\n", name, (void *)inode, (void *)dev);)

  if (jc_stat(name, &s) != 0) return -1;
  *inode = s.st_ino;
  *dev = s.st_dev;
  *mode = s.st_mode;
  if (!JC_S_ISDIR(s.st_mode)) return 1;
  return 0;
}
