#!/bin/bash

# Runs the installed *dupes* binary and the built binary and compares
# the output for sameness. Also displays timing statistics.

ERR=0
[ -z "$SORT" ] && SORT=cat

# Detect installed jdupes
if [ -z "$ORIG_JDUPES" ]
	then
	jdupes -v 2>/dev/null >/dev/null && ORIG_JDUPES=jdupes
	test ! -z "$WINDIR" && "$WINDIR/jdupes.exe" -v 2>/dev/null >/dev/null && ORIG_JDUPES="$WINDIR/jdupes.exe"
	[ -z "$ORIG_JDUPES" ] && echo "error: can't find old jdupes; set ORIG_JDUPES manually" >&2 && exit 1
fi

if [ ! $ORIG_JDUPES -v 2>/dev/null >/dev/null ]
	then echo "Can't run installed jdupes"
	echo "To manually specify an original jdupes, use: ORIG_JDUPES=path/to/jdupes $0"
	exit 1
fi

test ! -e ./jdupes && echo "Build jdupes first, silly" && exit 1

echo -n "Installed $ORIG_JDUPES:"
sync
time $ORIG_JDUPES -q "$@" | $SORT > installed_output.txt || ERR=1
echo -en "\nBuilt jdupes:"
sync
time ./jdupes -q "$@" | $SORT > built_output.txt || ERR=1
diff -Nau installed_output.txt built_output.txt

rm -f installed_output.txt built_output.txt
test "$ERR" != "0" && echo "Errors were returned during execution"
