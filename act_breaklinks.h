/* jdupes action for breaking hard linked file sets
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef ACT_BREAKLINKS_H
#define ACT_BREAKLINKS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "jdupes.h"

void breaklinks(file_t * restrict files);

#ifdef __cplusplus
}
#endif

#endif /* ACT_BREAKLINKS_H */
