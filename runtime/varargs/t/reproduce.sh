#!/usr/bin/env bash
#
# WP-24's certification: the enumeration, the generated veneer, the bridge, and
# the done-condition all hold.
#
# Four gates, in order:
#   1. the variadic set is consistent with the export inventory -- every name a
#      real export, the non-variadic look-alikes absent (derive-variadic.sh);
#   2. re-generating the veneer reproduces veneer.gen.c and .h byte for byte;
#   3. the bridge (sv2ms.c) and the prototype-driven entries (nonformat.c)
#      compile clean, and the generated veneer compiles under the test prefix;
#   4. the runnable test passes: a sixteen-argument printf prints what glibc
#      prints, and vfprintf reached through a System V va_list formats through a
#      Microsoft-ABI callee.
#
# Usage:
#   reproduce.sh [options]
#
# Options:
#   --cc=CC      C compiler. [default: gcc, or $CC]
#   -q, --quiet  errors only.
#   -h, --help   print this message and exit.
#
# Exit: 0 all four pass, 1 a gate failed, 2 usage.

set -u

prog=reproduce
here=$(cd "$(dirname "$0")" && pwd)
varargs=$(cd "$here/.." && pwd)
cc=${CC:-gcc}
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  usage; exit 0 ;;
		--cc)       cc=${2:-}; shift 2 ;;
		--cc=*)     cc=${1#*=}; shift ;;
		-q|--quiet) quiet=1; shift ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

enum=$varargs/variadic-exports.tsv
vc=$varargs/veneer.gen.c
vh=$varargs/veneer.gen.h
for f in "$enum" "$vc" "$vh" "$varargs/sv2ms.c" "$varargs/nonformat.c"; do
	[ -f "$f" ] || fail "missing committed artifact $f"
done
command -v "$cc" >/dev/null 2>&1 || fail "no C compiler ($cc)"

tmp=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'
trap 'rm -rf "$tmp"' EXIT

# 1. the set is consistent.
bash "$varargs/derive-variadic.sh" -q || fail 'the variadic set is not consistent with the inventory'
say "1/4 set consistent: $(bash "$varargs/derive-variadic.sh" --terse 2>/dev/null | sed -n 's/^total\t//p') variadic exports"

# 2. the veneer reproduces.
bash "$varargs/gen-veneer.sh" --c "$tmp/veneer.gen.c" --h "$tmp/veneer.gen.h" 2>/dev/null \
	|| fail 'the generator failed'
diff -u "$vc" "$tmp/veneer.gen.c" >"$tmp/dc" 2>&1 || { printf '%s: veneer.gen.c differs:\n' "$prog" >&2; head -40 "$tmp/dc" >&2; exit 1; }
diff -u "$vh" "$tmp/veneer.gen.h" >"$tmp/dh" 2>&1 || { printf '%s: veneer.gen.h differs:\n' "$prog" >&2; head -40 "$tmp/dh" >&2; exit 1; }
say "2/4 veneer reproduces: $(grep -c '^__attribute__((sysv_abi))$' "$vc") wrappers"

# 3. the bridge and the prototype-driven entries compile, and so does the veneer.
"$cc" -c -O2 -Wall -Wextra -I"$varargs" "$varargs/sv2ms.c" -o "$tmp/sv2ms.o" 2>"$tmp/e1" \
	|| { cat "$tmp/e1" >&2; fail 'sv2ms.c did not compile'; }
[ -s "$tmp/e1" ] && { cat "$tmp/e1" >&2; fail 'sv2ms.c compiled with warnings'; }
"$cc" -c -O2 -Wall -Wextra -fno-builtin -I"$varargs" "$varargs/nonformat.c" -o "$tmp/nonformat.o" 2>"$tmp/e2" \
	|| { cat "$tmp/e2" >&2; fail 'nonformat.c did not compile'; }
[ -s "$tmp/e2" ] && { cat "$tmp/e2" >&2; fail 'nonformat.c compiled with warnings'; }
bash "$varargs/gen-veneer.sh" --prefix v24_ --c "$tmp/vp.c" --h "$tmp/vp.h" 2>/dev/null || fail 'prefixed generation failed'
"$cc" -c -O2 -Wall -Wextra -I"$varargs" -I"$tmp" "$tmp/vp.c" -o "$tmp/vp.o" 2>"$tmp/e3" \
	|| { cat "$tmp/e3" >&2; fail 'the generated veneer did not compile'; }
[ -s "$tmp/e3" ] && { cat "$tmp/e3" >&2; fail 'the generated veneer compiled with warnings'; }
say "3/4 compiles clean: bridge, prototype-driven entries, and 54 generated wrappers"

# 4. the done-condition runs.
qflag=; [ "$quiet" = 1 ] && qflag=-q
bash "$here/run-tests.sh" --cc "$cc" $qflag >"$tmp/test.log" 2>&1 || { cat "$tmp/test.log" >&2; fail 'the done-condition test failed'; }
say "4/4 done-condition: sixteen-argument printf matches glibc, vfprintf crosses through a System V va_list"

say "reproduces: set, veneer, compile, and the done-condition all hold"
exit 0
