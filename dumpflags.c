/* Debug flag dumping
 * This file is part of jdupes; see jdupes.c for license information */

#include <stdio.h>
#include "jdupes.h"

#ifdef DEBUG
void dump_all_flags(void)
{
  fprintf(stderr, "\nSet flag dump:");
  /* Behavior modification flags */
  if (ISFLAG(flags, F_RECURSE)) fprintf(stderr, " F_RECURSE");
  if (ISFLAG(flags, F_HIDEPROGRESS)) fprintf(stderr, " F_HIDEPROGRESS");
  if (ISFLAG(flags, F_SOFTABORT)) fprintf(stderr, " F_SOFTABORT");
  if (ISFLAG(flags, F_FOLLOWLINKS)) fprintf(stderr, " F_FOLLOWLINKS");
  if (ISFLAG(flags, F_INCLUDEEMPTY)) fprintf(stderr, " F_INCLUDEEMPTY");
  if (ISFLAG(flags, F_CONSIDERHARDLINKS)) fprintf(stderr, " F_CONSIDERHARDLINKS");
  if (ISFLAG(flags, F_RECURSEAFTER)) fprintf(stderr, " F_RECURSEAFTER");
  if (ISFLAG(flags, F_NOPROMPT)) fprintf(stderr, " F_NOPROMPT");
  if (ISFLAG(flags, F_EXCLUDEHIDDEN)) fprintf(stderr, " F_EXCLUDEHIDDEN");
  if (ISFLAG(flags, F_PERMISSIONS)) fprintf(stderr, " F_PERMISSIONS");
  if (ISFLAG(flags, F_EXCLUDESIZE)) fprintf(stderr, " F_EXCLUDESIZE");
  if (ISFLAG(flags, F_QUICKCOMPARE)) fprintf(stderr, " F_QUICKCOMPARE");
  if (ISFLAG(flags, F_USEPARAMORDER)) fprintf(stderr, " F_USEPARAMORDER");
  if (ISFLAG(flags, F_REVERSESORT)) fprintf(stderr, " F_REVERSESORT");
  if (ISFLAG(flags, F_ISOLATE)) fprintf(stderr, " F_ISOLATE");
  if (ISFLAG(flags, F_ONEFS)) fprintf(stderr, " F_ONEFS");
  if (ISFLAG(flags, F_PARTIALONLY)) fprintf(stderr, " F_PARTIALONLY");
  if (ISFLAG(flags, F_NOCHANGECHECK)) fprintf(stderr, " F_NOCHANGECHECK");
  if (ISFLAG(flags, F_NOTRAVCHECK)) fprintf(stderr, " F_NOTRAVCHECK");
  if (ISFLAG(flags, F_BENCHMARKSTOP)) fprintf(stderr, " F_BENCHMARKSTOP");
  if (ISFLAG(flags, F_HASHDB)) fprintf(stderr, " F_HASHDB");

  if (ISFLAG(flags, F_LOUD)) fprintf(stderr, " F_LOUD");
  if (ISFLAG(flags, F_DEBUG)) fprintf(stderr, " F_DEBUG");

  /* Action-related flags */
  if (ISFLAG(a_flags, FA_PRINTMATCHES)) fprintf(stderr, " FA_PRINTMATCHES");
  if (ISFLAG(a_flags, FA_PRINTUNIQUE)) fprintf(stderr, " FA_PRINTUNIQUE");
  if (ISFLAG(a_flags, FA_OMITFIRST)) fprintf(stderr, " FA_OMITFIRST");
  if (ISFLAG(a_flags, FA_SUMMARIZEMATCHES)) fprintf(stderr, " FA_SUMMARIZEMATCHES");
  if (ISFLAG(a_flags, FA_DELETEFILES)) fprintf(stderr, " FA_DELETEFILES");
  if (ISFLAG(a_flags, FA_SHOWSIZE)) fprintf(stderr, " FA_SHOWSIZE");
  if (ISFLAG(a_flags, FA_HARDLINKFILES)) fprintf(stderr, " FA_HARDLINKFILES");
  if (ISFLAG(a_flags, FA_DEDUPEFILES)) fprintf(stderr, " FA_DEDUPEFILES");
  if (ISFLAG(a_flags, FA_MAKESYMLINKS)) fprintf(stderr, " FA_MAKESYMLINKS");
  if (ISFLAG(a_flags, FA_PRINTNULL)) fprintf(stderr, " FA_PRINTNULL");
  if (ISFLAG(a_flags, FA_PRINTJSON)) fprintf(stderr, " FA_PRINTJSON");
  if (ISFLAG(a_flags, FA_ERRORONDUPE)) fprintf(stderr, " FA_ERRORONDUPE");
  if (ISFLAG(a_flags, FA_BREAKLINKS)) fprintf(stderr, " FA_BREAKLINKS");

  /* Extra print flags */
  if (ISFLAG(p_flags, PF_PARTIAL)) fprintf(stderr, " PF_PARTIAL");
  if (ISFLAG(p_flags, PF_EARLYMATCH)) fprintf(stderr, " PF_EARLYMATCH");
  if (ISFLAG(p_flags, PF_FULLHASH)) fprintf(stderr, " PF_FULLHASH");
  fprintf(stderr, " [end of list]\n\n");
  fflush(stderr);
  return;
}
#endif
