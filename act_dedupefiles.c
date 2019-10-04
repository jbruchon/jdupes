/* BTRFS deduplication of file blocks
 * This file is part of jdupes; see jdupes.c for license information */

#include "jdupes.h"

#ifdef ENABLE_BTRFS
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>

/* Use built-in static BTRFS header if requested */
#ifdef STATIC_BTRFS_H
#include "btrfs-static.h"
#else
#include <linux/btrfs.h>
#endif

#include <sys/ioctl.h>
#include <sys/utsname.h>
#include "act_dedupefiles.h"

#define BTRFS_DEDUP_MAX_SIZE 16777216

/* Message to append to BTRFS warnings based on write permissions */
static const char *readonly_msg[] = {
   "",
   " (no write permission)"
};

static char *dedupeerrstr(int err) {
  tempname[sizeof(tempname)-1] = '\0';
  if (err == BTRFS_SAME_DATA_DIFFERS) {
    snprintf(tempname, sizeof(tempname), "BTRFS_SAME_DATA_DIFFERS (data modified in the meantime?)");
    return tempname;
  } else if (err < 0) {
    return strerror(-err);
  } else {
    snprintf(tempname, sizeof(tempname), "Unknown error %d", err);
    return tempname;
  }
}

extern void dedupefiles(file_t * restrict files)
{
  struct utsname utsname;
  struct btrfs_ioctl_same_args *same;
  char **dupe_filenames; /* maps to same->info indices */

  file_t *curfile;
  unsigned int n_dupes, max_dupes, cur_info;
  unsigned int cur_file = 0, max_files, total_files = 0;

  int fd;
  int ret, status, readonly;
  int64_t cur_offset;

  LOUD(fprintf(stderr, "\nRunning dedupefiles()\n");)

  /* Refuse to dedupe on 2.x kernels; they could damage user data */
  if (uname(&utsname)) {
    fprintf(stderr, "Failed to get kernel version! Aborting.\n");
    exit(EXIT_FAILURE);
  }
  LOUD(fprintf(stderr, "dedupefiles: uname got release '%s'\n", utsname.release));
  if (*(utsname.release) == '2' && *(utsname.release + 1) == '.') {
    fprintf(stderr, "Refusing to dedupe on a 2.x kernel; data loss could occur. Aborting.\n");
    exit(EXIT_FAILURE);
  }

  /* Find the largest dupe set, alloc space to hold structs for it */
  get_max_dupes(files, &max_dupes, &max_files);
  /* Kernel dupe count is a uint16_t so exit if the type's limit is exceeded */
  if (max_dupes > 65535) {
    fprintf(stderr, "Largest duplicate set (%d) exceeds the 65535-file dedupe limit.\n", max_dupes);
    fprintf(stderr, "Ask the program author to add this feature if you really need it. Exiting!\n");
    exit(EXIT_FAILURE);
  }
  same = calloc(sizeof(struct btrfs_ioctl_same_args) +
                sizeof(struct btrfs_ioctl_same_extent_info) * max_dupes, 1);
  dupe_filenames = malloc(max_dupes * sizeof(char *));
  LOUD(fprintf(stderr, "dedupefiles structs: alloc1 size %lu => %p, alloc2 size %lu => %p\n",
        sizeof(struct btrfs_ioctl_same_args) + sizeof(struct btrfs_ioctl_same_extent_info) * max_dupes,
        (void *)same, max_dupes * sizeof(char *), (void *)dupe_filenames);)
  if (!same || !dupe_filenames) oom("dedupefiles() structures");

  /* Main dedupe loop */
  while (files) {
    if (ISFLAG(files->flags, F_HAS_DUPES) && files->size) {
      cur_file++;
      if (!ISFLAG(flags, F_HIDEPROGRESS)) {
        fprintf(stderr, "Dedupe [%u/%u] %u%% \r", cur_file, max_files,
            cur_file * 100 / max_files);
      }

      /* Open each file to be deduplicated */
      cur_info = 0;
      for (curfile = files->duplicates; curfile; curfile = curfile->duplicates) {
        int errno2;

        /* Never allow hard links to be passed to dedupe */
        if (curfile->device == files->device && curfile->inode == files->inode) {
          LOUD(fprintf(stderr, "skipping hard linked file pair: '%s' = '%s'\n", curfile->d_name, files->d_name);)
          continue;
        }

        dupe_filenames[cur_info] = curfile->d_name;
        readonly = 0;
        if (access(curfile->d_name, W_OK) != 0) readonly = 1;
        fd = open(curfile->d_name, O_RDWR);
        LOUD(fprintf(stderr, "opening loop: open('%s', O_RDWR) [%d]\n", curfile->d_name, fd);)

        /* If read-write open fails, privileged users can dedupe in read-only mode */
        if (fd == -1) {
          /* Preserve errno in case read-only fallback fails */
          LOUD(fprintf(stderr, "opening loop: open('%s', O_RDWR) failed: %s\n", curfile->d_name, strerror(errno));)
          errno2 = errno;
          fd = open(curfile->d_name, O_RDONLY);
          if (fd == -1) {
            LOUD(fprintf(stderr, "opening loop: fallback open('%s', O_RDONLY) failed: %s\n", curfile->d_name, strerror(errno));)
            fprintf(stderr, "Unable to open '%s': %s%s\n", curfile->d_name,
                strerror(errno2), readonly_msg[readonly]);
            continue;
          }
          LOUD(fprintf(stderr, "opening loop: fallback open('%s', O_RDONLY) succeeded\n", curfile->d_name);)
        }

        same->info[cur_info].fd = fd;
        cur_info++;
        total_files++;
      }
      n_dupes = cur_info;
      same->dest_count = (uint16_t)n_dupes;  /* kernel type is __u16 */

      fd = open(files->d_name, O_RDONLY);
      LOUD(fprintf(stderr, "source: open('%s', O_RDONLY) [%d]\n", files->d_name, fd);)
      if (fd == -1) {
        fprintf(stderr, "unable to open(\"%s\", O_RDONLY): %s\n", files->d_name, strerror(errno));
        goto cleanup;
      }

      /* Call dedupe ioctl to pass the files to the kernel */
      ret = 0;
      same->length = (uint64_t)BTRFS_DEDUP_MAX_SIZE;
      for (cur_offset = 0; cur_offset < files->size; cur_offset += BTRFS_DEDUP_MAX_SIZE) {
        same->logical_offset = (uint64_t)cur_offset;
        for (cur_info = 0; cur_info < n_dupes; cur_info++) {
          same->info[cur_info].logical_offset = (uint64_t)cur_offset;
        }
        if (BTRFS_DEDUP_MAX_SIZE + cur_offset < files->size)
          same->length = (uint64_t)BTRFS_DEDUP_MAX_SIZE;
        else
          same->length = (uint64_t)(files->size - cur_offset);

        ret = ioctl(fd, BTRFS_IOC_FILE_EXTENT_SAME, same);
        if (ret < 0)
          break;
        LOUD(fprintf(stderr, "dedupe: ioctl('%s' [%d], BTRFS_IOC_FILE_EXTENT_SAME, same) => %d\n", files->d_name, fd, ret);)
      } 
      if (close(fd) == -1) fprintf(stderr, "Unable to close(\"%s\"): %s\n", files->d_name, strerror(errno));

      if (ret < 0) {
        fprintf(stderr, "dedupe failed against file '%s' (%d matches): %s\n", files->d_name, n_dupes, strerror(errno));
        goto cleanup;
      }

      for (cur_info = 0; cur_info < n_dupes; cur_info++) {
        status = same->info[cur_info].status;
        if (status != 0) {
          if (same->info[cur_info].bytes_deduped == 0) {
            fprintf(stderr, "warning: dedupe failed: %s => %s: %s [%d]%s\n",
              files->d_name, dupe_filenames[cur_info], dedupeerrstr(status),
              status, readonly_msg[readonly]);
          } else {
            fprintf(stderr, "warning: dedupe only did %" PRIdMAX " bytes: %s => %s: %s [%d]%s\n",
              (intmax_t)same->info[cur_info].bytes_deduped, files->d_name,
              dupe_filenames[cur_info], dedupeerrstr(status), status, readonly_msg[readonly]);
          }
        }
      }

cleanup:
      for (cur_info = 0; cur_info < n_dupes; cur_info++) {
        if (close((int)same->info[cur_info].fd) == -1) {
          fprintf(stderr, "unable to close(\"%s\"): %s", dupe_filenames[cur_info],
            strerror(errno));
        }
      }

    } /* has dupes */

    files = files->next;
  }

  if (!ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, "Deduplication done (%d files processed)\n", total_files);
  free(same);
  free(dupe_filenames);
  return;
}
#endif /* ENABLE_BTRFS */
