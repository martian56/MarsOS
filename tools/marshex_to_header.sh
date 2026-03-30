#!/usr/bin/env sh
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <input.marshex> <symbol_name>" >&2
    exit 1
fi

in="$1"
symbol="$2"

if [ ! -f "$in" ]; then
    echo "error: input file not found: $in" >&2
    exit 1
fi

echo "/* Auto-generated. Do not edit directly. */"
echo "#ifndef MARS_OS_GENERATED_USER_APPS_H"
echo "#define MARS_OS_GENERATED_USER_APPS_H"
printf "static const char %s[] =\n" "$symbol"
sed 's/\\/\\\\/g; s/"/\\"/g; s/.*/    "&"/' "$in"
echo ";"
echo "#endif"
