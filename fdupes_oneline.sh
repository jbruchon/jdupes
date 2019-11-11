#!/bin/sh

# Emulates fdupes -1 output
# Usage: jdupes command line | ./fdupes_oneline.sh

# This is a newline.
IFS='
'

if [ "$1" = "-q" ] || [ "$1" = "--shell-quote" ]; then
  # This only works with GNU (env printf) or bash (builtin printf).
  # If you are using dash, change the command to use env printf...
  escape() { printf '%q ' "$LINE"; }
else
  escape() { printf '%s' "$LINE" | sed 's/\\/\\\\/g; s/ /\\ /g'; printf ' '; }
fi

while read -r LINE
  do if [ -z "$LINE" ]
    then printf '\n'
    else escape
  fi
done
