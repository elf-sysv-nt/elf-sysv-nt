#!/usr/bin/env bash
#
# WP-31 and WP-T1 certification: build the parser and its tests, regenerate the
# fixture corpus, run the unit checks over it, and run the fuzz target. Every
# step reports through the session monitor when a .smon marker sits above the
# working directory, and is a no-op emitter otherwise, so this script runs the
# same on a machine that has never heard of the monitor.
#
# The tests are built with -fsanitize=undefined -fsanitize-undefined-trap-on-error
# so undefined behaviour traps without a runtime library, which this toolchain
# does not ship; memory safety past the end of an image is caught by the guard
# page the drivers place. A crash in either driver therefore fails the run.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -n N, --count=N   Fuzz cases to run. [default: 2000000]
#   -s HEX, --seed=HEX  Fuzz PRNG seed. [default: 0x9e3779b97f4a7c15]
#   -k, --keep        Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet       Errors only.
#   -h, --help        Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or test failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)

count=2000000
seed=0x9e3779b97f4a7c15
keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)   usage; exit 0 ;;
		-n|--count)  count=${2:-}; shift 2 ;;
		--count=*)   count=${1#*=}; shift ;;
		-s|--seed)   seed=${2:-}; shift 2 ;;
		--seed=*)    seed=${1#*=}; shift ;;
		-k|--keep)   keep=1; shift ;;
		-q|--quiet)  quiet=1; shift ;;
		--)          shift; break ;;
		-?*)         printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)           break ;;
	esac
done

# The monitor emitter, if this checkout carries one. Sourcing a missing file is
# not fatal; the functions simply become undefined, so guard the calls.
smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

cc=gcc
cflags="-std=c11 -Wall -Wextra -O2 -fsanitize=undefined -fsanitize-undefined-trap-on-error"
src="$here/../elf_parse.c"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp31.XXXXXX"); fi
unit_bin=$bin/unit
fuzz_bin=$bin/fuzz

smon_session build wp31-loader-parse
smon_plan fixtures build-unit run-unit build-fuzz run-fuzz

rc=0

smon_step_start fixtures
if smon_cmd python3 "$here/mkfixtures.py" -o "$here/corpus" ${quiet:+-q}; then
	smon_step_ok fixtures
else
	smon_step_fail fixtures $?; fail "fixture generation failed"
fi

smon_step_start build-unit
if smon_cmd $cc $cflags -o "$unit_bin" "$here/unit.c" "$src"; then
	smon_step_ok build-unit
else
	smon_step_fail build-unit $?; fail "unit build failed"
fi

smon_step_start run-unit
if smon_cmd "$unit_bin" "$here/corpus"; then
	smon_step_ok run-unit
	smon_item WP-T1 met "the committed corpus parses to its recorded verdicts, each rejection naming its field"
else
	urc=$?
	smon_step_fail run-unit $urc
	smon_item WP-T1 unmet "a corpus fixture reached the wrong verdict"
	rc=1
fi

smon_step_start build-fuzz
if smon_cmd $cc $cflags -o "$fuzz_bin" "$here/fuzz.c" "$src"; then
	smon_step_ok build-fuzz
else
	smon_step_fail build-fuzz $?; fail "fuzz build failed"
fi

smon_step_start run-fuzz
if smon_cmd "$fuzz_bin" -n "$count" -s "$seed" --corpus="$here/corpus" ${quiet:+-q}; then
	smon_step_ok run-fuzz
	smon_note "fuzz ran $count cases at seed $seed with no crash, UB, or missing diagnostic"
	smon_item WP-31 met "the parser survived $count fuzz cases; every rejection carries a field and every acceptance holds the invariants"
else
	frc=$?
	smon_step_fail run-fuzz $frc
	smon_item WP-31 unmet "the fuzz target found a crash, a rejection without a diagnostic, or a violated invariant"
	rc=1
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: all checks passed ($count fuzz cases)"; }
exit $rc
