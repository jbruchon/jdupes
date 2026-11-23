/* jdupes extended filters
 * See jdupes.c for license information */

#ifndef NO_EXTFILTER

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>

#include <libjodycode.h>
#include "helptext.h"
#include "jdupes.h"

/* Extended filter parameter flags */
#define XF_EXCL_EXT     0x00000001U
#define XF_SIZE_EQ      0x00000002U
#define XF_SIZE_GT      0x00000004U
#define XF_SIZE_LT      0x00000008U
#define XF_ONLY_EXT     0x00000010U
#define XF_EXCL_STR     0x00000020U
#define XF_ONLY_STR     0x00000040U
#define XF_DATE_NEWER   0x00000080U
#define XF_DATE_OLDER   0x00000100U
/* The X-than-or-equal are combination flags */
#define XF_SIZE_GTEQ    0x00000006U
#define XF_SIZE_LTEQ    0x0000000aU

/* Flags that use a numeric size with optional suffix */
#define XF_REQ_NUMBER   0x0000000eU
/* Flags that require a data parameter (after a colon) */
#define XF_REQ_VALUE    0x0000001fU
/* Flags that take a date that needs to be converted to time_t seconds */
#define XF_REQ_DATE     0x00000180U

/* -X extended filter parameter stack */
struct extfilter {
  struct extfilter *next;
  unsigned int flags;
  int64_t size;  /* also used for other large integers */
  char param[];
};

struct extfilter_tags {
  const char * const tag;
  const uint32_t flags;
};

/* Extended filter tree head and static tag list */
static struct extfilter *extfilter_head = NULL;
static const struct extfilter_tags extfilter_tags[] = {
  { "noext",   XF_EXCL_EXT   },
  { "onlyext", XF_ONLY_EXT   },
  { "size+",   XF_SIZE_GT    },
  { "size-",   XF_SIZE_LT    },
  { "size+=",  XF_SIZE_GTEQ  },
  { "size-=",  XF_SIZE_LTEQ  },
  { "size=",   XF_SIZE_EQ    },
  { "nostr",   XF_EXCL_STR   },
  { "onlystr", XF_ONLY_STR   },
  { "newer",   XF_DATE_NEWER },
  { "older",   XF_DATE_OLDER },
  { NULL, 0 },
};


/* Does a file have one of these comma-separated extensions?
 * Returns 1 after any match, 0 if no matches */
static int match_extensions(char *path, const char *extlist)
{
  char *dot;
  const char *ext;
  size_t len, extlen;

  LOUD(fprintf(stderr, "match_extensions('%s', '%s')\n", path, extlist);)
  if (path == NULL || extlist == NULL) jc_nullptr("match_extensions");

  dot = NULL;
  /* Scan to end of path, save the last dot, reset on path separators */
  while (*path != '\0') {
    if (*path == '.') dot = path;
    if (*path == '/' || *path == '\\') dot = NULL;
    path++;
  }
  /* No dots in the file name = no extension, so give up now */
  if (dot == NULL) return 0;
  dot++;
  /* Handle a dot at the end of a file name */
  if (*dot == '\0') return 0;

  /* Get the length of the file's extension for later checking */
  extlen = strlen(dot);
  LOUD(fprintf(stderr, "match_extensions: file has extension '%s' with length %" PRIdMAX "\n", dot, (intmax_t)extlen);)

  /* dot is now at the location of the last file extension; check the list */
  /* Skip any commas at the start of the list */
  while (*extlist == ',') extlist++;
  ext = extlist;
  len = 0;
  while (1) {
    /* Reject upon hitting the end with no more extensions to process */
    if (*extlist == '\0' && len == 0) return 0;
    /* Process extension once a comma or EOL is hit */
    if (*extlist == ',' || *extlist == '\0') {
      /* Skip serial commas */
      while (*extlist == ',') extlist++;
      if (extlist == ext)  goto skip_empty;
      if (jc_strncaseeq(dot, ext, len) == 0 && extlen == len) {
        LOUD(fprintf(stderr, "match_extensions: matched on extension '%s' (len %" PRIdMAX ")\n", dot, (intmax_t)len);)
        return 1;
      }
      LOUD(fprintf(stderr, "match_extensions: no match: '%s' (%" PRIdMAX "), '%s' (%" PRIdMAX ")\n", dot, (intmax_t)len, ext, (intmax_t)extlen);)
skip_empty:
      ext = extlist;
      len = 0;
      continue;
    }
    extlist++; len++;
    /* LOUD(fprintf(stderr, "match_extensions: DEBUG: '%s' : '%s' (%ld), '%s' (%ld)\n", extlist, dot, len, ext, extlen);) */
  }
  return 0;
}


/* Add a filter to the filter stack */
void add_extfilter(const char *option)
{
  char *opt, *p;
  time_t tt;
  struct extfilter *extf = extfilter_head;
  const struct extfilter_tags *tags = extfilter_tags;

  if (option == NULL) jc_nullptr("add_extfilter()");

  LOUD(fprintf(stderr, "add_extfilter '%s'\n", option);)

  opt = malloc(strlen(option) + 1);
  if (opt == NULL) jc_oom("add_extfilter option");
  strcpy(opt, option);
  p = opt;

  while (*p != ':' && *p != '\0') p++;

  /* Split tag string into *opt (tag) and *p (value) */
  if (*p == ':') {
    *p = '\0';
    p++;
  }

  while (tags->tag != NULL && jc_streq(tags->tag, opt) != 0) tags++;
  if (tags->tag == NULL) goto error_bad_filter;

  /* Check for a tag that requires a value */
  if (tags->flags & XF_REQ_VALUE && *p == '\0') goto error_value_missing;

  /* *p is now at the value, NOT the tag string! */

  if (extfilter_head != NULL) {
    /* Add to end of exclusion stack if head is present */
    while (extf->next != NULL) extf = extf->next;
    extf->next = malloc(sizeof(struct extfilter) + strlen(p) + 1);
    if (extf->next == NULL) jc_oom("add_extfilter alloc");
    extf = extf->next;
  } else {
    /* Allocate extfilter_head if no exclusions exist yet */
    extfilter_head = malloc(sizeof(struct extfilter) + strlen(p) + 1);
    if (extfilter_head == NULL) jc_oom("add_extfilter alloc");
    extf = extfilter_head;
  }

  /* Set tag value from predefined tag array */
  extf->flags = tags->flags;

  /* Initialize the new extfilter element */
  extf->next = NULL;
  if (extf->flags & XF_REQ_NUMBER) {
    /* Exclude uses a number; handle it with possible suffixes */
    *(extf->param) = '\0';
    if (jc_sizestr_to_int64(p, &extf->size) != 0) goto error_bad_size_suffix;
  } else if (extf->flags & XF_REQ_DATE) {
    /* Exclude uses a date; convert it to seconds since the epoch */
    *(extf->param) = '\0';
    tt = jc_strtoepoch(p);
    LOUD(fprintf(stderr, "extfilter: jody_strtoepoch: '%s' -> %" PRIdMAX "\n", p, (intmax_t)tt);)
    if (tt == -1) goto error_bad_time;
    extf->size = tt;
  } else {
    /* Exclude uses string data; just copy it */
    extf->size = 0;
    if (*p != '\0') strcpy(extf->param, p);
    else *(extf->param) = '\0';
  }

  LOUD(fprintf(stderr, "Added extfilter: tag '%s', data '%s', size %lld, flags %d\n", opt, extf->param, (long long)extf->size, extf->flags);)
  free(opt);
  return;

error_bad_time:
  fprintf(stderr, "Invalid extfilter date[time] was specified: -X filter:datetime\n");
  goto extf_help_and_exit;
error_value_missing:
  fprintf(stderr, "extfilter value missing or invalid: -X filter:value\n");
  goto extf_help_and_exit;
error_bad_filter:
  fprintf(stderr, "Invalid extfilter filter name was specified\n");
  goto extf_help_and_exit;
error_bad_size_suffix:
  fprintf(stderr, "Invalid extfilter size suffix specified; use B or KMGTPE[i][B]\n");
  goto extf_help_and_exit;
extf_help_and_exit:
  help_text_extfilter();
  exit(EXIT_FAILURE);
}


/* Exclude single files based on extended filter stack; return 0 = exclude */
int extfilter_exclude(file_t * const restrict newfile)
{
  for (struct extfilter *extf = extfilter_head; extf != NULL; extf = extf->next) {
    uint32_t sflag = extf->flags;
    LOUD(fprintf(stderr, "check_singlefile: extfilter check: %08x %" PRIdMAX " %" PRIdMAX " %s\n", sflag, (intmax_t)newfile->size, (intmax_t)extf->size, newfile->d_name);)
    if (
         /* Any line that passes will result in file exclusion */
            ((sflag == XF_SIZE_EQ)    && (newfile->size != extf->size))
         || ((sflag == XF_SIZE_LTEQ)  && (newfile->size > extf->size))
         || ((sflag == XF_SIZE_GTEQ)  && (newfile->size < extf->size))
         || ((sflag == XF_SIZE_GT)    && (newfile->size <= extf->size))
         || ((sflag == XF_SIZE_LT)    && (newfile->size >= extf->size))
         || ((sflag == XF_EXCL_EXT)   && match_extensions(newfile->d_name, extf->param))
         || ((sflag == XF_ONLY_EXT)   && !match_extensions(newfile->d_name, extf->param))
         || ((sflag == XF_EXCL_STR)   && strstr(newfile->d_name, extf->param))
         || ((sflag == XF_ONLY_STR)   && !strstr(newfile->d_name, extf->param))
#ifndef NO_MTIME
         || ((sflag == XF_DATE_NEWER) && (newfile->mtime < extf->size))
         || ((sflag == XF_DATE_OLDER) && (newfile->mtime >= extf->size))
#endif
    ) return 1;
  }
  return 0;
}

#endif /* NO_EXTFILTER */
