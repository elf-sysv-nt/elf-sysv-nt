#!/usr/bin/env bash
#
# WP-25 certification: build the compatibility counter and its test with the
# host gcc, and run it. The test's verdict must be yes: a program built against
# a lower minor runs against a higher runtime, a program built against a higher
# minor is refused with a diagnostic rather than a crash, the generation digit
# in the name refuses a different runtime, and the struct-size backup refuses a
# structurally different stamp.
#
# The counter is plain integer and string comparison with no ABI content, so it
# is built and run here with the host gcc to produce a real verdict, the way
# WP-22 certified the host-facing crossing on the host toolchain. The same
# compat.c is the code that links into elfsysv1.dll; it carries no host-only
# construct. It is not cross-compiled here because the cross toolchain has no
# libc headers installed at this stage of the tree -- compat.c uses <string.h>
# and <stdio.h>, which are the runtime's own and resolve once the veneer
# headers (WP-50) are in the cross sysroot, not before. README.md records this.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
ver=$here/..

keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)  usage; exit 0 ;;
		-k|--keep)  keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--)         shift; break ;;
		-?*)        printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)          break ;;
	esac
done

# -mno-red-zone is the standing policy (DR-0006) even where nothing touches ELF
# code, so the counter is built under it like everything else in the runtime.
cc=gcc
cflags="-std=gnu11 -O1 -g -mno-red-zone -Wall -Wextra"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp25.XXXXXX"); fi
test_bin=$bin/compat_test.exe

smon_session build wp25-compat-counter
smon_plan build run

rc=0

# --- build ---------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$test_bin" \
	"$here/compat_test.c" "$ver/compat.c"; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the counter did not build"
fi

# --- run: the test's verdict ---------------------------------------------
smon_step_start run
test_args=""; [ "$quiet" = 1 ] && test_args="--quiet"
if smon_cmd "$test_bin" $test_args; then
	smon_step_ok run
else
	smon_step_fail run $?
	smon_note "the compatibility check returned a wrong verdict on some case" warn
	rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp25 met "the compatibility counter enforces Cygwin's backward-only rule on elfsysv1.dll's combined api major.minor: a program built against a lower minor runs against a higher runtime, a program built against a higher minor is refused with a diagnostic rather than a crash, the generation digit in the name refuses a different runtime, and the stamp-size backup refuses a structurally different stamp."
else
	smon_item wp25 unmet "a build or verdict check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: the counter holds both directions; WP-25 certified"; }
exit $rc
