#!/bin/bash

# stupid_dupes: find duplicates like jdupes but more slowly with a shell script
# Copyright (C) 2020 by Jody Bruchon <jody@jodybruchon.com>
#
# The MIT License (MIT)
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

#########################################
#             HOW IT WORKS              #
#########################################
#
# This script loads each file into an array of file paths, then compares every
# file against every other file, using various tricks to discard candidates as
# quickly as possible without reading and comparing entire files. These include
# skipping pairs with mismatched file sizes and hashing only the first 4K block
# of each file and comparing the partial hashes.
#
# Every file is referred to by its index number. Since GNU Bash can use arrays
# but doesn't have anything remotely like a C structure to conveniently pack
# a bunch of related variables together within, C structures are simulated with
# the array index number used as a "pointer." For example, a doubly-linked list
# in C is pretty easy to declare:
#
# struct match { struct match *left; struct match *right; }
#
# And then an array of these matches: struct match matchlist[MAX_MATCHES];
#
# Using arrays, we simulate this (e.g. with file index 15, match 2):
#
# MLEFT[2]  = index number of "left" file in match chain
# MRIGHT[2] = index number of "right" file in match chain
#
# FILES[15] = file path for file #15, referenced by one of the above items
# SIZES[15] = file size for file #15
# PHASH[15] = the 4K partial file hash for file #15
# FHASH[15] = the full file hash for file #15
#
# The basic algorithm is: verify size match, verify partial hash match, verify
# full hash match, verify files match byte-for-byte to be sure.
#
# There is some extra code to check for match pairing that is doubled up, and
# a "processed" flag to prevent double processing of files.


PROGNAME=stupid_dupes.sh
VER=1.0
VERDATE=2020-02-18

V=1		# Verbosity
AC=0		# Argument count
PHS=4096	# Partial hash size
FQUICK=0	# Quick (no final compare) mode
FICNT=0		# File index counter
MSCNT=0		# Match set counter
STATUS=0	# Exit status

# A hash command that outputs a plain file hash (no file names)
test -z "$HASHCMD" && HASHCMD=jodyhash

# 'find' defaults to no-recurse
FRECURSE="-maxdepth 1"

# sort option (cat = none)
test -z "$SORTCMD" && SORTCMD="cat"

### Function definitions

# $1: file path to add
add_file () {
	((V > 1)) && echo "add_file: '$1'" >&2
	SZ="$(stat -c '%s' "$1" || echo FAIL)"
	if [ "$SZ" = "FAIL" ]
		then echo "error: add_file: can't stat '$1'"  >&2
		STATUS=1
		return
	fi
	((FICNT += 1))
	FILES[FICNT]="$1"
	SIZES[FICNT]="$SZ"
	PHASH[FICNT]="NULL"
	FHASH[FICNT]="NULL"
	((V > 1)) && echo "add_file: added as file number $FICNT" >&2
}

# $1: hash to get (partial/full); $2: file # to hash
get_filehash () {
	((V > 1)) && echo "get_filehash: $1:$2 '${FILES[$2]}'" >&2
	test -z "${FILES[$2]}" && \
		echo "internal error: get_filehash: bad file number passed" >&2 && exit 1
	case "$1" in
		partial)
			PHASH[$2]="$(dd if="${FILES[$2]}" bs=4096 count=1 2>/dev/null | $HASHCMD || echo "FAIL")"
			test "${PHASH[$2]}" = "FAIL" && \
				echo "get_filehash: hashing failed: '${FILES[$2]}'" >&2 && STATUS=1
			;;
		full)
			FHASH[$2]="$($HASHCMD "${FILES[$2]}" || echo "FAIL")"
			test "${FHASH[$2]}" = "FAIL" && \
				echo "get_filehash: hashing failed: '${FILES[$2]}'" >&2 && STATUS=1
			;;
		*)
			echo "internal error: get_filehash: invalid hash type '$1'" >&2
			exit 1;
			;;
	esac
	((V > 1)) && echo "get_filehash: PHASH=${PHASH[$2]}" >&2
	return 0
}

# $1/$2: file numbers to check for a match
check_match () {
	((V > 1)) && echo "check_match: checking: $1:'${FILES[$1]}', $2:'${FILES[$2]}'" >&2
	# Sizes must match
	if [ ${SIZES[$1]} != ${SIZES[$2]} ]
		then ((V > 1)) && \
			echo "check_match: sizes differ: ${SIZES[$1]} != ${SIZES[$2]}" >&2
		return 1
	fi

	# Check partial hashes
	test "${PHASH[$1]}" = "NULL" && get_filehash partial "$1"
	test "${PHASH[$1]}" = "FAIL" && STATUS=1 && return 1
	test "${PHASH[$2]}" = "NULL" && get_filehash partial "$2"
	test "${PHASH[$2]}" = "FAIL" && STATUS=1 && return 1
	if [ "${PHASH[$1]}" != "${PHASH[$2]}" ]
		then ((V > 1)) && echo "check_match: partial hashes don't match" >&2
		return 1
		else ((V > 1)) && echo "check_match: partial hashes match" >&2
	fi

	# Check full hashes
	test "{$FHASH[$1]}" = "NULL" && get_filehash full "$1"
	test "{$FHASH[$1]}" = "FAIL" && STATUS=1 && return 1
	test "{$FHASH[$2]}" = "NULL" && get_filehash full "$2"
	test "{$FHASH[$2]}" = "FAIL" && STATUS=1 && return 1
	if [ "${FHASH[$1]}" != "${FHASH[$2]}" ]
		then ((V > 1)) && echo "check_match: full hashes don't match" >&2
		return 1
		else ((V > 1)) && echo "check_match: full hashes match" >&2
	fi

	# Byte-for-byte compare the files
	if ((FQUICK == 1)) || cmp -s "${FILES[$1]}" "${FILES[$2]}"
		then ((V > 1)) && echo "check_match: files are identical" >&2
		return 0
		else ((V > 1)) && echo "check_match: files are not identical" >&2
		return 1
	fi
	return 1  # should never be reached
}

# Link a pair of matched file numbers
add_to_matches () {
	((V > 1)) && echo "add_to_matches: adding: '${FILES[$1]}','${FILES[$2]}'" >&2
	MSCNT=$((MSCNT + 1))
	MLEFT[$MSCNT]=$1
	MRIGHT[$MSCNT]=$2
	MPROC[$MSCNT]=0		# Flips to 1 during final processing
	((V > 1)) && echo "add_to_matches: set $MSCNT = $1:$2" >&2
	return 0
}

# Print all matched files
print_matches () {
	((V > 1)) && echo "print_matches: running" >&2
	FIRST=1
	CURFILE=0
	# Outer loop: find a match pair to start with
	for ((PRINTCNT = 1; PRINTCNT <= MSCNT; PRINTCNT++))
		do
		((V > 1)) && echo "               outer loop: print count $PRINTCNT, match count $MSCNT" >&2
		# Don't reprint already-printed match pairings
		if (( MPROC[PRINTCNT] != 0))
			then
			((V > 1)) && echo "               skipping processed pair $PRINTCNT" >&2
			continue
		fi
		CURFILE=${MLEFT[PRINTCNT]}
		# Print a newline before each new set EXCEPT the first set
		if ((FIRST == 1)); then FIRST=0; else echo; fi
		echo "${FILES[CURFILE]}"
		# Inner loop: find match pairs to print
		CURCNT=$PRINTCNT; PREVCNT=1; unset PREV; PREV[1]=$CURFILE
		for ((; CURCNT < MSCNT; CURCNT++))
			do
			((V > 1)) && echo "                 inner loop: CC $CURCNT" >&2
			((V > 1)) && echo "                 files: ${MLEFT[CURCNT]}:'${FILES[${MLEFT[CURCNT]}]}', ${MRIGHT[CURCNT]}:'${FILES[${MRIGHT[CURCNT]}]}'" >&2
			if (( MPROC[PRINTCNT] != 0))
				then
				((V > 1)) && echo "                 skipping processed pair $CURCNT" >&2
				continue
			fi
			CURMATCH_L=0; CURMATCH_R=0; PCCNT=0
			# For each pair, check both sides for any known match number
			while ((PCCNT < PREVCNT))
				do
				PCCNT=$((PCCNT + 1))
				((V > 1)) && echo -n "                   deep loop: $PCCNT <= $PREVCNT" >&2
				(( MLEFT[CURCNT] == PREV[PCCNT] )) && CURMATCH_L=${MRIGHT[CURCNT]}
				(( MRIGHT[CURCNT] == PREV[PCCNT])) && CURMATCH_R=${MLEFT[CURCNT]}
				((V > 1)) && echo ", curmatch: $CURMATCH = ${MLEFT[CURCNT]} < ${PREV[PCCNT]} > ${MRIGHT[CURCNT]}" >&2
				# If both sides of this pair have been previously seen,
				# just flag the pair and print nothing.
				if (( CURMATCH_L != 0 && CURMATCH_R != 0 ))
					then
					MPROC[$CURCNT]=1
					((V > 1)) && echo "                   Flagging: pair $CURCNT (${MLEFT[CURCNT]}:${MRIGHT[CURCNT]}) (R)" >&2
					break
				fi
			done

			# If L or R match exists, we have a printable match
			CURMATCH=0
			(( CURMATCH_L != 0 && CURMATCH_R == 0)) && CURMATCH=$CURMATCH_L
			(( CURMATCH_R != 0 && CURMATCH_L == 0)) && CURMATCH=$CURMATCH_R
			if ((CURMATCH != 0))
				then echo "${FILES[CURMATCH]}"
				MPROC[$CURCNT]=1
				((V > 1)) && echo "                   Flagging: pair $CURCNT (${MLEFT[CURCNT]}:${MRIGHT[CURCNT]})" >&2
				PREVCNT=$((PREVCNT + 1))
				PREV[$PREVCNT]=$CURMATCH
			fi
		done
	done
	((V > 1)) && echo "print_matches: complete" >&2
	return 0
}

show_help () {
	COPYTEXT="Copyright (C) 2020 by Jody Bruchon <jody@jodybruchon.com>\n"
	echo "$PROGNAME $VER ($VERDATE)"
	if [ "$2" = "full" ]
		then
		echo -e "$COPYTEXT"
		echo -e "\nUsage: $PROGNAME [options] file_or_dir1 [more_files ...]\n"
		echo -e "Options:\n"
		echo "-r|--recurse     Recurse into any subdirectories"
		echo "-q|--quiet       Only show final output and errors"
		echo "-Q|--quick       Skip the full file byte-for-byte comparison"
		echo "-D|--debug       Show lots of extra debugging text"
		echo "-v|-V|--version  Display program version and exit"
		echo "-h|--help        Show this help text and exit"
		echo "--license        Show the full program license text"
		echo -e "\njdupes is better than me. Get it at github.com/jbruchon/jdupes\n"
	fi
	if [ "$2" = "license" ]
		then
		echo -e "$COPYTEXT"
		echo -e "\nThe MIT License (MIT)\n"
		echo "Permission is hereby granted, free of charge, to any person obtaining a copy of"
		echo "this software and associated documentation files (the \"Software\"), to deal in"
		echo "the Software without restriction, including without limitation the rights to"
		echo "use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of"
		echo "the Software, and to permit persons to whom the Software is furnished to do so,"
		echo -e "subject to the following conditions:\n"
		echo "The above copyright notice and this permission notice shall be included in all"
		echo -e "copies or substantial portions of the Software.\n"
		echo "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR"
		echo "IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS"
		echo "FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR"
		echo "COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER"
		echo "IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN"
		echo "CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."
	fi
	exit $1
}

### End function definitions

### Begin main program

# Process arguments
[[ "$@" = "" ]] && show_help 1 full
for X in "$@"
	do
	case "$X" in
		-q|--quiet) V=0 ;;
		-D|--debug) V=2 ;;
		-r|--recurse) FRECURSE="" ;;
		-Q|--quick) FQUICK=1 ;;
		-v|-V|--version) show_help 0 version ;;
		-h|--help) show_help 0 full ;;
		--license) show_help 0 license ;;
		*) AC=$((AC + 1)); ARGS[AC]="$X" ;;
	esac
done

((V > 1)) && echo "Command line: $(printf %q "$0" "$@")" >&2

# Main loop
for ((ARGNUM=1; ARGNUM < AC; ARGNUM++))
	do
	((V > 1)) && echo -e "Processing argument $ARGNUM: '${ARGS[ARGNUM]}'" >&2
	if [[ ! -f "${ARGS[ARGNUM]}" && ! -d "${ARGS[ARGNUM]}" || -h "${ARGS[ARGNUM]}" ]]
		then echo "warning: not a regular file or directory: '${ARGS[ARGNUM]}'" >&2
		STATUS=1
		continue
	fi

	# Add files/dirs to the list, recursing as needed
	while IFS= read -r X
		do add_file "$X"
	done < <(find "${ARGS[ARGNUM]}" $FRECURSE -type f -size +0 | $SORTCMD)
done

# If there are not enough files, just exit with no matches
((FICNT < 2)) && echo "No matches found." && exit $STATUS

# Check every file pair for matches
for ((CNT=1; CNT < FICNT; CNT++))
do
	for ((SCAN=CNT; SCAN < FICNT;))
	do
		((SCAN++))
		check_match $CNT $SCAN && add_to_matches $CNT $SCAN
	done
done

print_matches

exit $STATUS
