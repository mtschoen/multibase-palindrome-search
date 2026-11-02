#!/usr/bin/env sh
# Builds the four search programs with clang and optionally runs the fast
# consistency checks. Mirrors the Makefile for environments without make
# (for example Git Bash on Windows, where this script was tested).
#
#   sh build.sh          build into bin/
#   sh build.sh check    build, then run self-tests, cross-program agreement
#                        on the base 2 + base 16 palindrome list, and the
#                        incremental-residue verify mode up to 24 hex digits
set -eu

CC="${CC:-clang}"
CFLAGS="${CFLAGS:--O3 -march=native -Wall -Wextra}"
RTLIB="${RTLIB:--rtlib=compiler-rt}"   # 128-bit division helpers; harmless on Linux and macOS
BIN="${BIN:-bin}"

mkdir -p "$BIN"
"$CC" $CFLAGS -o "$BIN/tripal" src/tripal.c
"$CC" $CFLAGS -o "$BIN/tripal_hex" src/tripal_hex.c
"$CC" $CFLAGS $RTLIB -o "$BIN/tripal128" src/tripal128.c
"$CC" $CFLAGS $RTLIB -o "$BIN/tripal_wide" src/tripal_wide.c
echo "built $BIN/tripal $BIN/tripal_hex $BIN/tripal128 $BIN/tripal_wide"

if [ "${1:-}" = "check" ]; then
    "$BIN/tripal128" selftest
    "$BIN/tripal_wide" selftest
    "$BIN/tripal_hex" pairs 8 | grep '^[0-9]' | sort -n > "$BIN/pairs_hex.txt"
    "$BIN/tripal128" pairs 8 | grep '^[0-9]' | sort -n > "$BIN/pairs_128.txt"
    "$BIN/tripal_wide" pairs 1 8 | grep '^[0-9]' | sort -n > "$BIN/pairs_wide.txt"
    cmp "$BIN/pairs_hex.txt" "$BIN/pairs_128.txt"
    cmp "$BIN/pairs_hex.txt" "$BIN/pairs_wide.txt"
    echo "pair lists agree: $(wc -l < "$BIN/pairs_hex.txt") base 2 + base 16 palindromes up to 8 hex digits"
    "$BIN/tripal_wide" verify 1 24 | grep -v '^[0-9]'
    echo "check passed"
fi
