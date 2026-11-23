#!/bin/sh

JD="$1"
[ -z "$JD" ] && JD=./jdupes
[ -z "$JOPT" ] && JOPT=-q

[ ! -x "$JD" ] && echo "Build jdupes first, or supply a binary path on the command line" >&2 && exit 1

FAIL=0

TD_FULL_FILECNT=$(find testdir -type f | wc -l)
if [ "$TD_FULL_FILECNT" -gt 0 ]; then
	echo "testdirFullFileCnt: VALID"
else
	echo "testdirFullFileCnt: FAIL"
	FAIL=1
fi

TD_FILECNT=$(find testdir -type f -size +0 | wc -l)
TD_ZERO_FILECNT=$(find testdir -type f -size 0 | wc -l)
if [ "$((TD_FILECNT + TD_ZERO_FILECNT))" -eq "$TD_FULL_FILECNT" ] && [ "$TD_FILECNT" -gt 0 ]; then
	echo "testdirFileCnt: VALID"
else
	echo "testdirFileCnt: FAIL"
	FAIL=1
fi

if [ "$TD_ZERO_FILECNT" -gt 0 ]; then
	echo "testdirZeroFileCnt: VALID"
else
	echo "testdirZeroFileCnt: FAIL"
	FAIL=1
fi

TD_64K_FILECNT=$(find testdir -type f -size +64k | wc -l)
if [ "$TD_64K_FILECNT" -le "$TD_FILECNT" ]; then
	echo "testdir64kFileCnt: VALID"
else
	echo "testdir64kFileCnt: FAIL"
	FAIL=1
fi

JD_NOOPTS_CNT=$("$JD" $JOPT testdir | grep testdir | wc -l)
if [ "$JD_NOOPTS_CNT" -le "$TD_FILECNT" ]; then
	echo "jdupesNoOpts: PASS"
else
	echo "jdupesNoOpts: FAIL"
	FAIL=1
fi

JD_R_CNT=$("$JD" $JOPT -r testdir | grep testdir | wc -l)
if [ "$JD_R_CNT" -ge "$JD_NOOPTS_CNT" ] && [ "$JD_R_CNT" -le "$TD_FILECNT" ]; then
	echo "jdupesR: PASS"
else
	echo "jdupesR: FAIL"
	FAIL=1
fi

JD_RU_CNT=$("$JD" $JOPT -ru testdir | grep testdir | wc -l)
if [ "$((JD_R_CNT + JD_RU_CNT))" -eq "$TD_FILECNT" ]; then
	echo "jdupesRu: PASS"
else
	echo "jdupesRu: FAIL"
	FAIL=1
fi

JD_RZ_CNT=$("$JD" $JOPT -rz testdir | grep testdir | wc -l)
if [ "$JD_RZ_CNT" -ge "$JD_R_CNT" ] && [ "$JD_RZ_CNT" -le "$TD_FULL_FILECNT" ]; then
	echo "jdupesRz: PASS"
else
	echo "jdupesRz: FAIL"
	FAIL=1
fi

# Version output check
if "$JD" $JOPT -v | grep -Eq 'jdupes [0-9]+\.[0-9]+'; then
	echo "jdupesVersion: PASS"
else
	echo "jdupesVersion: FAIL"
	FAIL=1
fi

# Summary (-m) and combined print+summary (-M) output checks
if "$JD" $JOPT -mr testdir | grep -Eq 'duplicate.+occupying'; then
	echo "jdupesMr: PASS"
else
	echo "jdupesMr: FAIL"
	FAIL=1
fi

if "$JD" $JOPT -M testdir | grep -C20 tinydupe2 | grep -Eq 'duplicate.+occupying'; then
	echo "jdupesM: PASS"
else
	echo "jdupesM: FAIL"
	FAIL=1
fi

# Null-delimited (-r0) output check
if "$JD" $JOPT -r0 testdir | grep -aq 'larger_file_1'; then
	echo "jdupesR0: PASS"
else
	echo "jdupesR0: FAIL"
	FAIL=1
fi

# Hard-linked files are excluded by default (-H off): tinydupe2 matches, file100 does not
if "$JD" $JOPT -r testdir | grep -q 'tinydupe2' && ! "$JD" $JOPT -r testdir | grep -q 'file100'; then
	echo "jdupesHardlinkDefaultExclude: PASS"
else
	echo "jdupesHardlinkDefaultExclude: FAIL"
	FAIL=1
fi

# Non-existent target handling (Debian #1030933)
if "$JD" $JOPT -r xyz-123 2>&1 | grep -q "No duplicates found"; then
	echo "jdupesNonexistentTarget: PASS"
else
	echo "jdupesNonexistentTarget: FAIL"
	FAIL=1
fi

# Multi-dir duplicate + unique sum invariant (Debian #1063079)
JD_MDIR_DUP=$("$JD" $JOPT -r testdir/recursed_a testdir/recursed_c | grep testdir | wc -l)
JD_MDIR_UNIQ=$("$JD" $JOPT -ru testdir/recursed_a testdir/recursed_c | grep testdir | wc -l)
MDIR_TOTAL=$(find testdir/recursed_a testdir/recursed_c -type f -size +0 | wc -l)
if [ "$((JD_MDIR_DUP + JD_MDIR_UNIQ))" -eq "$MDIR_TOTAL" ]; then
	echo "jdupesMultiDirUniqueSum: PASS"
else
	echo "jdupesMultiDirUniqueSum: FAIL"
	FAIL=1
fi

exit "$FAIL"
