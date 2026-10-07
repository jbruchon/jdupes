/* File order sorting functions
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libjodycode.h>
#include "likely_unlikely.h"
#include "jdupes.h"
#include "sort.h"


#ifndef NO_USER_ORDER
static int sort_pairs_by_param_order(const file_t * const f1, const file_t * const f2)
{
  if (!ISFLAG(flags, F_USEPARAMORDER)) return 0;
  if (unlikely(f1 == NULL || f2 == NULL)) jc_nullptr("sort_pairs_by_param_order()");
  if (f1->user_order < f2->user_order) return -sort_direction;
  if (f1->user_order > f2->user_order) return sort_direction;
  return 0;
}
#endif


#ifndef NO_MTIME
int sort_pairs_by_mtime(const file_t * const f1, const file_t * const f2)
{
  if (unlikely(f1 == NULL || f2 == NULL)) jc_nullptr("sort_pairs_by_mtime()");

#ifndef NO_USER_ORDER
  int po = sort_pairs_by_param_order(f1, f2);
  if (po != 0) return po;
#endif /* NO_USER_ORDER */

  if (f1->mtime < f2->mtime) return -sort_direction;
  else if (f1->mtime > f2->mtime) return sort_direction;

#ifndef NO_NUMSORT
  /* If the mtimes match, use the names to break the tie */
  return jc_numeric_strcmp(f1->d_name, f2->d_name, 0) > 0 ? sort_direction : -sort_direction;
#else
  return strcmp(f1->d_name, f2->d_name, 0) > 0 ? sort_direction : -sort_direction;
#endif /* NO_NUMSORT */
}
#endif


int sort_pairs_by_filename(const file_t * const f1, const file_t * const f2)
{
  if (unlikely(f1 == NULL || f2 == NULL)) jc_nullptr("sort_pairs_by_filename()");

#ifndef NO_USER_ORDER
  int po = sort_pairs_by_param_order(f1, f2);
  if (po != 0) return po;
#endif /* NO_USER_ORDER */

#ifndef NO_NUMSORT
  return jc_numeric_strcmp(f1->d_name, f2->d_name, 0) > 0 ? sort_direction : -sort_direction;
#else
  return strcmp(f1->d_name, f2->d_name) > 0 ? sort_direction : -sort_direction;
#endif /* NO_NUMSORT */
}


/* Intra-set: orders individual files inside a match set */
static int sort_intra_set_cmp(const void *a, const void *b)
{
  const file_t *f1 = *(const file_t * const *)a;
  const file_t *f2 = *(const file_t * const *)b;

#ifndef NO_MTIME
  if (ordertype == ORDER_TIME) {
    return sort_pairs_by_mtime(f1, f2);
  }
#endif
  return sort_pairs_by_filename(f1, f2);
}


/* Inter-set: orders duplicate match sets relative to each other */
static int sort_inter_set_cmp(const void *a, const void *b)
{
  const file_t * const f1 = *(const file_t * const *)a;
  const file_t * const f2 = *(const file_t * const *)b;

#ifndef NO_USER_ORDER
  int po = sort_pairs_by_param_order(f1, f2);
  if (po != 0) return po;
#endif

  /* Primary set ordering: file size (largest first) */
  if (f1->size != f2->size) {
    if (f1->size > f2->size) return (sort_direction < 0) ? 1 : -1;
    else return (sort_direction < 0) ? -1 : 1;
  }

  /* Secondary set ordering: respects parameter order (-u), mtime (-T), or filename */
#ifndef NO_MTIME
  if (ordertype == ORDER_TIME) {
    return sort_pairs_by_mtime(f1, f2);
  }
#endif
  return sort_pairs_by_filename(f1, f2);
}


/* Sort match sets against each other AND sort the intra-set paths */
void sort_match_sets(file_t **files_head)
{
  file_t *cur;
  file_t **set_array = NULL;
  size_t set_count = 0;
  size_t set_capacity = 0;

  if (files_head == NULL || *files_head == NULL) return;

  cur = *files_head;

  /* Pass 1: Intra-set sorting & collection of distinct set heads */
  while (cur != NULL) {
    file_t *next_file = cur->next;

    /* Skip non-head duplicate nodes; they are traversed via cur->duplicates */
    if (ISFLAG(cur->flags, FF_NOT_UNIQUE)) {
      cur = next_file;
      continue;
    }

    /* If this set has duplicates, sort them internally */
    if (cur->duplicates != NULL || ISFLAG(cur->flags, FF_HAS_DUPES)) {
      file_t *item;
      file_t **item_array = NULL;
      size_t item_count = 0;

      for (item = cur; item != NULL; item = item->duplicates) {
        item_count++;
      }

      if (item_count > 1) {
        item_array = (file_t **)malloc(item_count * sizeof(file_t *));
        if (item_array != NULL) {
          size_t idx = 0;
          for (item = cur; item != NULL; item = item->duplicates) {
            item_array[idx++] = item;
          }

          qsort(item_array, item_count, sizeof(file_t *), sort_intra_set_cmp);

          /* Re-link intra-set duplicates chain */
          for (idx = 0; idx < item_count - 1; idx++) {
            item_array[idx]->duplicates = item_array[idx + 1];
          }
          item_array[item_count - 1]->duplicates = NULL;

          /* Update canonical set head and flags */
          cur = item_array[0];
          SETFLAG(cur->flags, FF_HAS_DUPES);
          CLEARFLAG(cur->flags, FF_NOT_UNIQUE);

          for (idx = 1; idx < item_count; idx++) {
            CLEARFLAG(item_array[idx]->flags, FF_HAS_DUPES);
            SETFLAG(item_array[idx]->flags, FF_NOT_UNIQUE);
          }

          free(item_array);
        }
      }
    }

    /* Record set head for inter-set sorting */
    if (set_count >= set_capacity) {
      set_capacity = set_capacity ? set_capacity * 2 : 16;
      file_t **new_arr = (file_t **)realloc(set_array, set_capacity * sizeof(file_t *));
      if (new_arr == NULL) {
        if (set_array) free(set_array);
        return;
      }
      set_array = new_arr;
    }
    set_array[set_count++] = cur;

    cur = next_file;
  }

  /* Pass 2: Inter-set sorting (re-order match sets relative to each other) */
  if (set_count > 0 && set_array != NULL) {
    qsort(set_array, set_count, sizeof(file_t *), sort_inter_set_cmp);

    for (size_t i = 0; i < set_count - 1; i++) {
      set_array[i]->next = set_array[i + 1];
    }
    set_array[set_count - 1]->next = NULL;

    *files_head = set_array[0];
  }

  if (set_array != NULL) free(set_array);
}
