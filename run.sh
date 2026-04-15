#!/usr/bin/env bash
# Compile a C file with the MtG clang and run it through ursa.
#
# Usage: ./run.sh <source.c> [extra clang args...]
#
# Examples:
#   ./run.sh hello.c
#   ./run.sh hello.c -DFOO=1
#   echo 7 | ./run.sh input_test.c          # AInput / BInput read from stdin
#
# Outputs the program's stdout (i.e. characters emitted via __mtg_output)
# and exits with the simulator's status. Intermediate .s lives next to
# the source so it's easy to inspect after a failure (delete with `rm
# yoursrc.s` if you want a clean re-run).

set -euo pipefail

if [[ $# -lt 1 ]]; then
    cat >&2 <<EOF
usage: $0 <source.c> [extra clang args...]
EOF
    exit 2
fi

SRC=$1
shift

# Project root = the directory holding this script (so it works regardless
# of where the user cd'd to).
ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

CLANG=$ROOT/build/bin/clang
URSA=$ROOT/ursa/src/main.py

if [[ ! -x $CLANG ]]; then
    echo "error: clang not built — try 'ninja -C build -j4 clang'" >&2
    exit 1
fi
if [[ ! -f $URSA ]]; then
    echo "error: ursa not found at $URSA" >&2
    exit 1
fi

# Emit asm next to the source: foo.c -> foo.s.
ASM=${SRC%.c}.s

"$CLANG" --target=mtg -O0 -S "$SRC" -o "$ASM" "$@"
exec python3 "$URSA" "$ASM"
