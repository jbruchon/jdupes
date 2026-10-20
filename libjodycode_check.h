/* libjodycode version check headear
 * See libjodycode_check.c for license information */

#ifndef LIBJODYCODE_CHECK_H
#define LIBJODYCODE_CHECK_H

#ifdef __cplusplus
extern "C" {
#endif

/* Set this to the minimum feature level required by your program */
#define MY_FEATURELEVEL_REQ 6

extern const int jc_build_api_major;
extern const int jc_build_api_minor;
extern const char *jc_build_version;
extern const char *jc_build_featurelevel;
extern const unsigned char jc_build_api_versiontable[];

extern int libjodycode_version_check(int verbose, int bail);

#ifdef __cplusplus
}
#endif

#endif /* LIBJODYCODE_CHECK_H */
