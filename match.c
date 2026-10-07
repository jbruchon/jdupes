/* jdupes file matching functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifdef __linux__
 #include <fcntl.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libjodycode.h>

#include "jdupes.h"
#include "likely_unlikely.h"
#include "checks.h"
#include "filehash.h"
#ifndef NO_HASHDB
 #include "hashdb.h"
#endif
#include "interrupt.h"
#include "match.h"
#include "progress.h"


#ifndef NO_HASHDB
void add_file_to_hashdb(file_t *file)
{
  if (ISFLAG(file->flags, FF_HASHDB_DIRTY)) {
    CLEARFLAG(file->flags, FF_HASHDB_DIRTY);
    add_hashdb_entry(NULL, 0, file, 0);
  }
  return;
}
#endif /* NO_HASHDB */


#ifndef NO_HARDLINKS
/* Copy any hashes between entries for detected hard-linked files */
static void cross_copy_hashes(file_t *file1, file_t *file2)
{
#ifndef NO_HASHDB
  int dirty1 = 0, dirty2 = 0;
#endif

  if (file1 == NULL || file2 == NULL) jc_nullptr("cross_copy_hashes()");

  if (ISFLAG(file1->flags, FF_HASH_FULL)) {
    if (ISFLAG(file2->flags, FF_HASH_FULL)) return;
    file2->filehash_partial = file1->filehash_partial;
    file2->filehash = file1->filehash;
    SETFLAG(file2->flags, FF_HASH_PARTIAL | FF_HASH_FULL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
    dirty2 = 1;
#endif
  } else if (ISFLAG(file2->flags, FF_HASH_FULL)) {
    if (ISFLAG(file1->flags, FF_HASH_FULL)) return;
    file1->filehash_partial = file2->filehash_partial;
    file1->filehash = file2->filehash;
    SETFLAG(file1->flags, FF_HASH_PARTIAL | FF_HASH_FULL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
    dirty1 = 1;
#endif
  } else if (ISFLAG(file1->flags, FF_HASH_PARTIAL)) {
    if (ISFLAG(file2->flags, FF_HASH_PARTIAL)) return;
    file2->filehash_partial = file1->filehash_partial;
    SETFLAG(file2->flags, FF_HASH_PARTIAL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
    dirty2 = 1;
#endif
  } else if (ISFLAG(file2->flags, FF_HASH_PARTIAL)) {
    if (ISFLAG(file1->flags, FF_HASH_PARTIAL)) return;
    file1->filehash_partial = file2->filehash_partial;
    SETFLAG(file1->flags, FF_HASH_PARTIAL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
    dirty1 = 1;
#endif
  }

  /* Add to hash database */
#ifndef NO_HASHDB
  if (ISFLAG(flags, F_HASHDB)) {
    if (dirty1 == 1) add_hashdb_entry(NULL, 0, file1, 0);
    if (dirty2 == 1) add_hashdb_entry(NULL, 0, file2, 0);
 }
#endif

  return;
}
#endif  /* NO_HARDLINKS */


void registerpair(file_t **matchlist, file_t *newmatch)
{
  file_t *traverse;

  if (unlikely(matchlist == NULL || newmatch == NULL)) jc_nullptr("registerpair()");
  LOUD(fprintf(stderr, "registerpair: '%s', '%s'\n", (*matchlist)->d_name, newmatch->d_name);)

#ifndef NO_ERRORONDUPE
  if (ISFLAG(a_flags, FA_ERRORONDUPE)) {
    if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "\r");
    fprintf(stderr, "Exiting based on user request (-e); duplicates found:\n");
    printf("%s\n%s\n", (*matchlist)->d_name, newmatch->d_name);
    exit(255);
  }
#endif

  SETFLAG((*matchlist)->flags, FF_HAS_DUPES);
  SETFLAG(newmatch->flags, FF_NOT_UNIQUE);
  newmatch->duplicates = NULL;

  /* Append the new match to the end of the duplicate chain */
  traverse = *matchlist;
  while (traverse->duplicates != NULL) {
    traverse = traverse->duplicates;
  }
  traverse->duplicates = newmatch;
  return;
}


void registerfile(filetree_t * restrict * const restrict nodeptr, const enum tree_direction d, file_t * const restrict file)
{
  filetree_t * restrict branch;

  if (unlikely(nodeptr == NULL || file == NULL || (d != NONE && *nodeptr == NULL))) jc_nullptr("registerfile()");
  LOUD(fprintf(stderr, "registerfile(direction %d)\n", d));

  /* Allocate and initialize a new node for the file */
  branch = (filetree_t *)malloc(sizeof(filetree_t));
  if (unlikely(branch == NULL)) jc_oom("registerfile() branch");
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
file_t **checkmatch(filetree_t * restrict tree, file_t * const restrict file)
{
  int cmpresult = 0;
  int cantmatch = 0;
  const uint64_t * restrict filehash;
#ifndef NO_HASHDB
  int dirtyfile = 0, dirtytree = 0;
#endif

  DBG(if (unlikely(tree == NULL || file == NULL || tree->file == NULL || tree->file->d_name == NULL || file->d_name == NULL)) jc_nullptr("checkmatch()");)
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
#ifndef NO_HARDLINKS
    case 2:
      cross_copy_hashes(tree->file, file);
      return &tree->file;  /* linked files + -H switch */
    case -2: return NULL;  /* linked files, no -H switch */
#endif
    case -3:    /* user order */
    case -4:    /* one filesystem */
    case -5:    /* permissions */
        cantmatch = 1;
        cmpresult = 0;
        break;
    default: break;
  }

  /* If preliminary matching succeeded, do main file data checks */
  if (cmpresult == 0) {
    /* Print pre-check (early) match candidates if requested */
    if (ISFLAG(p_flags, PF_EARLYMATCH)) printf("Early match check passed:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);

    LOUD(fprintf(stderr, "checkmatch: starting file data comparisons\n"));
    /* Attempt to exclude files quickly with partial file hashing */
    if (!ISFLAG(tree->file->flags, FF_HASH_PARTIAL)) {
      filehash = get_filehash(tree->file, PARTIAL_HASH_SIZE, hash_algo);
      if (filehash == NULL) return NULL;

      tree->file->filehash_partial = *filehash;
      SETFLAG(tree->file->flags, FF_HASH_PARTIAL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
      dirtytree = 1;
#endif
    }

    if (!ISFLAG(file->flags, FF_HASH_PARTIAL)) {
      filehash = get_filehash(file, PARTIAL_HASH_SIZE, hash_algo);
      if (filehash == NULL) return NULL;

      file->filehash_partial = *filehash;
      SETFLAG(file->flags, FF_HASH_PARTIAL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
      dirtyfile = 1;
#endif
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
        SETFLAG(file->flags, FF_HASH_FULL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
        dirtyfile = 1;
#endif
        DBG(small_file++;)
      }
      if (!ISFLAG(tree->file->flags, FF_HASH_FULL)) {
        tree->file->filehash = tree->file->filehash_partial;
        SETFLAG(tree->file->flags, FF_HASH_FULL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
        dirtytree = 1;
#endif
        DBG(small_file++;)
      }
    } else if (cmpresult == 0) {
//      if (ISFLAG(flags, F_SKIPHASH)) {
//        LOUD(fprintf(stderr, "checkmatch: skipping full file hashes (F_SKIPMATCH)\n"));
//      } else {
        /* If partial match was correct, perform a full file hash match */
        if (!ISFLAG(tree->file->flags, FF_HASH_FULL)) {
          filehash = get_filehash(tree->file, 0, hash_algo);
          if (filehash == NULL) return NULL;

          tree->file->filehash = *filehash;
          SETFLAG(tree->file->flags, FF_HASH_FULL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
          dirtytree = 1;
#endif
        }

        if (!ISFLAG(file->flags, FF_HASH_FULL)) {
          filehash = get_filehash(file, 0, hash_algo);
          if (filehash == NULL) return NULL;

          file->filehash = *filehash;
          SETFLAG(file->flags, FF_HASH_FULL | FF_HASHDB_DIRTY);
#ifndef NO_HASHDB
          dirtyfile = 1;
#endif
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

  /* Add to hash database */
#ifndef NO_HASHDB
  if (ISFLAG(flags, F_HASHDB)) {
    if (dirtyfile == 1) add_file_to_hashdb(file);
    if (dirtytree == 1) add_file_to_hashdb(tree->file);
 }
#endif

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
      return checkmatch(tree->left, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: left\n"));
      registerfile(&tree, LEFT, file);
      return NULL;
    }
  } else if (cmpresult > 0) {
    if (tree->right != NULL) {
      LOUD(fprintf(stderr, "checkmatch: recursing tree: right\n"));
      return checkmatch(tree->right, file);
    } else {
      LOUD(fprintf(stderr, "checkmatch: registering file: right\n"));
      registerfile(&tree, RIGHT, file);
      return NULL;
    }
  } else {
    /* All compares matched */
    DBG(partial_to_full++;)
    LOUD(fprintf(stderr, "checkmatch: files appear to match based on hashes\n"));
    if (ISFLAG(p_flags, PF_FULLHASH)) printf("Full hashes match:\n   %s\n   %s\n\n", file->d_name, tree->file->d_name);
    return &tree->file;
  }
}


/* Do a byte-by-byte comparison in case two different files produce the
   same signature. Unlikely, but better safe than sorry. */
int confirmmatch(const char * const restrict file1, const char * const restrict file2, const off_t size)
{
  static char *c1 = NULL, *c2 = NULL;
  FILE *fp1, *fp2;
  size_t r1, r2;
  off_t bytes = 0;
  int retval = 0;

  if (unlikely(file1 == NULL || file2 == NULL)) jc_nullptr("confirmmatch()");
  LOUD(fprintf(stderr, "confirmmatch running\n"));

  if (unlikely(c1 == NULL || c2 == NULL)) {
    c1 = (char *)malloc(auto_chunk_size);
    c2 = (char *)malloc(auto_chunk_size);
  }
  if (unlikely(c1 == NULL || c2 == NULL)) jc_oom("confirmmatch() buffers");

  fp1 = jc_fopen(file1, JC_FILE_MODE_RDONLY_SEQ);
  fp2 = jc_fopen(file2, JC_FILE_MODE_RDONLY_SEQ);
  if (fp1 == NULL) {
    if (fp2 != NULL) jc_fclose(fp2);
    LOUD(fprintf(stderr, "confirmmatch: warning: file open failed ('%s')\n", file1);)
    goto different;
  }
  if (fp2 == NULL) {
    if (fp1 != NULL) jc_fclose(fp1);
    LOUD(fprintf(stderr, "confirmmatch: warning: file open failed ('%s')\n", file2);)
    goto different;
  }

  fseek(fp1, 0, SEEK_SET);
  fseek(fp2, 0, SEEK_SET);
#ifdef __linux__
  /* Tell Linux we will accees sequentially and soon */
  posix_fadvise(fileno(fp1), 0, size, POSIX_FADV_SEQUENTIAL);
  posix_fadvise(fileno(fp1), 0, size, POSIX_FADV_WILLNEED);
  posix_fadvise(fileno(fp2), 0, size, POSIX_FADV_SEQUENTIAL);
  posix_fadvise(fileno(fp2), 0, size, POSIX_FADV_WILLNEED);
#endif /* __linux__ */

  do {
    if (interrupt) goto different;
    r1 = fread(c1, sizeof(char), auto_chunk_size, fp1);
    r2 = fread(c2, sizeof(char), auto_chunk_size, fp2);

    if (r1 != r2) goto different; /* file lengths are different */
    if (memcmp (c1, c2, r1)) goto different; /* file contents are different */

    bytes += (off_t)r1;
    if (jc_alarm_ring != 0) {
      jc_alarm_ring = 0;
      update_phase2_progress("confirm", (int)((bytes * 100) / size));
    }
  } while (r2);

  /* Success: return 0 */
  goto finish_confirm;

different:
  retval = 1;

finish_confirm:
//  free(c1); free(c2);
  jc_fclose(fp1); jc_fclose(fp2);
  return retval;
}
