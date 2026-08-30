#!/usr/bin/env bash
#
# WP-21's certification: the committed inventory and wrappers reproduce from
# the DLL, compile, and hold the boundary.
#
# Five checks, in order, each a gate:
#   1. the source DLL is the one this inventory was cut from (SHA-256 pin);
#   2. re-extracting the import inventory reproduces cygwin-imports.tsv byte
#      for byte;
#   3. re-generating the wrappers reproduces wrappers.gen.c and .h byte for
#      byte;
#   4. wrappers.gen.c compiles, and every thunk is a bare tail jump -- no
#      prologue, no epilogue, no stack frame -- which is what makes a
#      signature-agnostic forward correct;
#   5. the audit sees the imports named only by the wrappers object.
#
# This is the guarantee WP-22 leans on: the wrappers it calls are the wrappers
# the DLL's import table produces, they build, and they are the sole namer of
# the raw imports.
#
# Usage:
#   reproduce.sh [options]
#
# Options:
#   --dll=FILE     cygwin1.dll to read. [default: /c/-/cygwin/root/bin/cygwin1.dll]
#   --any-dll      Do not require the pinned DLL (for a deliberate re-cut).
#   --cc=CC        C compiler for the compile check. [default: gcc, or $CC]
#   -q, --quiet    Errors only.
#   -h, --help     Print this message and exit.
#
# Exit: 0 all five pass, 1 a check failed, 2 usage.

set -u

prog=reproduce
here=$(cd "$(dirname "$0")" && pwd)
imports=$(cd "$here/.." && pwd)

# The DLL this inventory was cut from: Cygwin 3.6.10, the runtime base DR-0007
# names. A different binary is a different import surface and must not silently
# reproduce a different list.
pinned_sha=d66788fce4ef1ce787fc1a83f2dd1e063e58bbf0d48ad93164ee195a983c035e
dll=/c/-/cygwin/root/bin/cygwin1.dll
any_dll=0
cc=${CC:-gcc}
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)   usage; exit 0 ;;
		--dll)       dll=${2:-}; shift 2 ;;
		--dll=*)     dll=${1#*=}; shift ;;
		--any-dll)   any_dll=1; shift ;;
		--cc)        cc=${2:-}; shift 2 ;;
		--cc=*)      cc=${1#*=}; shift ;;
		-q|--quiet)  quiet=1; shift ;;
		--)          shift; break ;;
		-?*)         printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)           break ;;
	esac
done

inv=$imports/cygwin-imports.tsv
wc=$imports/wrappers.gen.c
wh=$imports/wrappers.gen.h
extractor=$imports/extract-imports.sh
generator=$imports/gen-wrappers.sh
auditor=$imports/audit-imports.sh

for f in "$inv" "$wc" "$wh"; do [ -f "$f" ] || fail "missing committed artifact $f"; done
for x in "$extractor" "$generator" "$auditor"; do [ -x "$x" ] || fail "missing tool $x"; done
[ -f "$dll" ] || fail "no cygwin1.dll at $dll"

# 1. the pin.
if [ "$any_dll" = 0 ]; then
	have=$(sha256sum "$dll" 2>/dev/null | awk '{print $1}')
	[ "$have" = "$pinned_sha" ] || fail "cygwin1.dll SHA-256 is ${have:-unknown}, not the pinned $pinned_sha (--any-dll to override)"
	say "1/5 dll pinned: $pinned_sha"
fi

tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# 2. the inventory reproduces.
"$extractor" --dll "$dll" -o "$tmp/imports.tsv" || fail 'the extractor failed'
if ! diff -u "$inv" "$tmp/imports.tsv" >/dev/null 2>&1; then
	printf '%s: committed inventory differs from a fresh extraction:\n' "$prog" >&2
	diff -u "$inv" "$tmp/imports.tsv" >&2 | head -40
	exit 1
fi
say "2/5 inventory reproduces: $(wc -l < "$inv" | tr -d ' ') rows"

# 3. the wrappers reproduce.
"$generator" --inventory "$tmp/imports.tsv" --c "$tmp/wrappers.gen.c" --h "$tmp/wrappers.gen.h" 2>/dev/null || fail 'the generator failed'
if ! diff -u "$wc" "$tmp/wrappers.gen.c" >/dev/null 2>&1; then
	printf '%s: committed wrappers.gen.c differs from a fresh generation:\n' "$prog" >&2
	diff -u "$wc" "$tmp/wrappers.gen.c" >&2 | head -40
	exit 1
fi
if ! diff -u "$wh" "$tmp/wrappers.gen.h" >/dev/null 2>&1; then
	printf '%s: committed wrappers.gen.h differs from a fresh generation:\n' "$prog" >&2
	diff -u "$wh" "$tmp/wrappers.gen.h" >&2 | head -40
	exit 1
fi
say "3/5 wrappers reproduce: $(grep -c '^__attribute__' "$wc") thunks"

# 4. the wrappers compile with no stack frame.
command -v "$cc" >/dev/null 2>&1 || fail "no C compiler ($cc)"
"$cc" -c -O2 -fomit-frame-pointer -mno-red-zone -Wall "$wc" -o "$tmp/wrappers.gen.o" 2>"$tmp/cc.err" || {
	printf '%s: wrappers.gen.c did not compile:\n' "$prog" >&2; cat "$tmp/cc.err" >&2; exit 1; }
frame=$(objdump -d "$tmp/wrappers.gen.o" 2>/dev/null | grep -E '\b(push|pop|leave)\b|sub[^,]*,%rsp|%rbp' | head)
[ -z "$frame" ] || fail "a thunk carries a stack frame, which breaks the tail jump:
$frame"
nthunk=$(nm "$tmp/wrappers.gen.o" | grep -c ' T w_')
nslot=$(nm -u "$tmp/wrappers.gen.o" | grep -c '__imp_')
say "4/5 compiles frameless: $nthunk thunks, $nslot import slots"

# 5. the audit: imports named only by the wrappers object.
cp "$tmp/wrappers.gen.o" "$tmp/wrappers.gen.o.audit" 2>/dev/null
"$auditor" --inventory "$tmp/imports.tsv" --wrappers wrappers.gen.o -q "$tmp/wrappers.gen.o" || fail 'the audit rejected the wrappers object'
say "5/5 audit clean: wrappers are the sole namer of the imports"

say "reproduces: inventory, wrappers, compile, and boundary all hold at $pinned_sha"
exit 0
