/* File hash database management
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_HASHDB_H
#define JDUPES_HASHDB_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "jdupes.h"

typedef struct _hashdb {
  struct _hashdb *left;
  struct _hashdb *right;
  uint64_t path_hash;
  char *path;
  int pathlen;
  uint64_t partialhash;
  uint64_t fullhash;
  jdupes_ino_t inode;
  off_t size;
  time_t mtime;
  uint_fast8_t hashcount;
} hashdb_t;

extern int save_hash_database(const char * const restrict dbname, uint64_t * const restrict entries, const int destroy);
extern hashdb_t *add_hashdb_entry(char *in_path, const int in_pathlen, const file_t *check, int update);
extern int64_t load_hash_database(const char * const restrict dbname);
extern int read_hashdb_entry(file_t *file);
extern uint64_t dump_hashdb(void);
extern int cleanup_hashdb(uint64_t *cnt, hashdb_t *cur);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_HASHDB_H */
