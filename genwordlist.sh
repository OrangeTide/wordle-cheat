#!/bin/sh

set -e

if [ "$#" -ne 1 ]; then
	echo "$0:requires to arguments" >&2
	exit 1
fi

INFILE=$1
shift 1

if [ ! -r "$INFILE" ]; then
	echo "$INFILE:file not found" >&2
	exit 1
fi

# replace extension of input file to use for output files (.c and .h)
BASE="${INFILE%.*}"

# NOTE: we're fixing the array to 6 bytes per row to make it as small as possible (no pointers needed)

# Output header (.h)
exec > "${BASE}.h"
HEADERNAME="$(echo "$BASE"_H | tr 'a-z ' 'A-Z_')"
echo "#ifndef $HEADERNAME"
echo "#define $HEADERNAME"
echo "extern const char wordlist[][6];"
echo "extern const unsigned wordlist_count;"
echo "#endif"

# Output source (.c)
exec < "$INFILE" > "${BASE}.c"
count=0
echo "#include \"${BASE}.h\""
echo "const char wordlist[][6] = {"
while read w ; do
	printf '\t"%s",\n' "$w"
	count=$((count+1))
done
echo "};"
echo "const unsigned wordlist_count = $count;"
