/* File order sorting functions
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_SORT_H
#define JDUPES_SORT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"

#ifndef NO_MTIME
int sort_pairs_by_mtime(file_t *f1, file_t *f2);
#endif
int sort_pairs_by_filename(file_t *f1, file_t *f2);
void sort_match_sets(file_t **files_head);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_SORT_H */
