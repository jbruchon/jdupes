/* Help text and version information
 * This file is part of jdupes; see jdupes.c for license information */

#ifndef JDUPES_HELPTEXT_H
#define JDUPES_HELPTEXT_H

#ifdef __cplusplus
extern "C" {
#endif

void version_text(int short_version);
void help_text(void);
void help_text_extfilter(void);
void help_text_chunksize(void);
void help_text_hashdb_opts(void);

#ifdef __cplusplus
}
#endif

#endif /* JDUPES_HELPTEXT_H */
