#!/usr/bin/env bash
#
# WP-32 certification: build the mapping unit and its test scaffolding, build
# the static ELF specimens with the cross toolchain, and map, probe and run
# each. The good cases must map with every segment visible to the runtime's own
# bookkeeping, take their protections as fault probes confirm, and run with
# .bss zero; the two control cases must be refused. Every step reports through
# the session monitor when a .smon marker sits above the working directory, and
# is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or test failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
map=$here/..
elf=$here/../../elf

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

# The cross toolchain that emits the static ELF specimens.
export PATH="$HOME/x-elfsysvnt/bin:$PATH"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp32.XXXXXX"); fi
test_bin=$bin/map_test

smon_session build wp32-loader-map
smon_plan specimens build run-good-64k run-good-2m refuse-granule refuse-occupied

rc=0

smon_step_start specimens
if smon_cmd bash "$here/mkspecimens.sh" "$here/specimens"; then
	smon_step_ok specimens
else
	smon_step_fail specimens $?; fail "specimen build failed (cross toolchain on PATH?)"
fi

smon_step_start build
if smon_cmd $cc $cflags -o "$test_bin" \
	"$here/map_test.c" "$here/enter.S" \
	"$map/elf_map.c" "$map/host_mem.c" "$elf/elf_parse.c"; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "map_test build failed"
fi

run_case() {  # step, human, extra-args..., specimen
	local step=$1 human=$2; shift 2
	smon_step_start "$step"
	if smon_cmd "$test_bin" "$@"; then
		smon_step_ok "$step"
	else
		smon_step_fail "$step" $?
		rc=1
	fi
}

run_case run-good-64k    "64 KB-aligned static ELF" "$here/specimens/good-64k"
run_case run-good-2m     "2 MB-aligned static ELF"  "$here/specimens/good-2m"
run_case refuse-granule  "granule-sharing refusal"  --expect-granule "$here/specimens/share-4k"
run_case refuse-occupied "occupied-span refusal"    --expect-occupied "$here/specimens/good-64k"

if [ "$rc" = 0 ]; then
	smon_item wp32 met "a static ELF maps through Cygwin's mmap with every segment visible in /proc/self/maps, protections confirmed by fault probes, .bss zero, and it runs; a granule-sharing object and an occupied span are refused"
else
	smon_item wp32 unmet "a mapping case did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: all mapping checks passed"; }
exit $rc
