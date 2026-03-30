#!/usr/bin/env sh
set -eu

if [ "$#" -lt 1 ] || [ "$#" -gt 2 ]; then
    echo "usage: $0 <input-elf> [output-file]" >&2
    exit 1
fi

in="$1"
out="${2:-}"

if [ ! -f "$in" ]; then
    echo "error: input file not found: $in" >&2
    exit 1
fi

if command -v xxd >/dev/null 2>&1; then
    hex_stream=$(xxd -p -c 1024 "$in" | tr -d '\n')
else
    hex_stream=$(od -An -tx1 -v "$in" | tr -d ' \n')
fi

if [ -z "$hex_stream" ]; then
    echo "error: input file is empty" >&2
    exit 1
fi

spaced_hex=$(printf '%s' "$hex_stream" | sed 's/../& /g' | sed 's/[[:space:]]*$//')
line="MARSHEX $spaced_hex"

if [ -n "$out" ]; then
    printf '%s\n' "$line" > "$out"
else
    printf '%s\n' "$line"
fi
