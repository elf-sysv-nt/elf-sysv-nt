#!/usr/bin/env bash
#
# WP-24 done-condition, run.
#
# Builds the veneer under the v24_ prefix -- the generator's own output, only
# renamed so it does not collide with the host libc -- alongside the sv2ms
# bridge and the core stand-in, links the test, and runs it. The test's four
# cases are the va_list incompatibility, a sixteen-argument printf checked
# against glibc's output, a vfprintf reached through a System V va_list with a
# Microsoft-ABI callee, and a scanf round-trip. A zero exit means all passed.
#
# Usage:
#   run-tests.sh [options]
#
# Options:
#   --cc=CC       C compiler. [default: gcc, or $CC]
#   -w, --work=D  build here and keep it.
#   -q, --quiet   errors only.
#   -h, --help    print this message and exit.

set -u

prog=run-tests
here=$(cd "$(dirname "$0")" && pwd)
varargs=$(cd "$here/.." && pwd)
cc=${CC:-gcc}
work=
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  usage; exit 0 ;;
		--cc)       cc=${2:-}; shift 2 ;;
		--cc=*)     cc=${1#*=}; shift ;;
		-w|--work)  work=${2:-}; shift 2 ;;
		--work=*)   work=${1#*=}; shift ;;
		-q|--quiet) quiet=1; shift ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

command -v "$cc" >/dev/null 2>&1 || fail "no C compiler ($cc)"

if [ -n "$work" ]; then mkdir -p "$work" || fail "cannot create $work"; work=$(cd "$work" && pwd)
else work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp dir'; trap 'rm -rf "$work"' EXIT; fi

say 'generating the veneer under the v24_ prefix'
bash "$varargs/gen-veneer.sh" --prefix v24_ --c "$work/vp.c" --h "$work/vp.h" 2>/dev/null \
	|| fail 'the generator failed'

say 'building the test'
"$cc" -O2 -Wall -Wextra -I"$varargs" -I"$work" -o "$work/varargs-test.exe" \
	"$work/vp.c" "$varargs/sv2ms.c" "$here/core.c" "$here/varargs-test.c" 2>"$work/cc.err" \
	|| { cat "$work/cc.err" >&2; fail 'the test did not build'; }
[ -s "$work/cc.err" ] && { cat "$work/cc.err" >&2; fail 'the test built with warnings'; }

say 'running the test'
"$work/varargs-test.exe"
rc=$?
[ $rc -eq 0 ] || fail "the test reported failures (exit $rc)"
say 'done-condition met: printf sixteen-argument and vfprintf-through-System-V both pass'
exit 0
