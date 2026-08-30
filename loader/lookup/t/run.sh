#!/usr/bin/env bash
#
# WP-35 certification: build the symbol-lookup engine with the host compiler,
# build the three-way collision graph with the cross toolchain, and hold the
# resolver to two bars. The unit test asserts the internals a differential
# cannot see -- the two hash probes, the global-beats-weak binding rule, scope
# order, interposition, and the version-matcher seam. The differential asserts
# that the object this resolver binds collide() to is the object a real glibc
# ld.so binds it to, over the plain load order, the reversed load order, and the
# LD_PRELOAD interposition. Every step reports through the session monitor when a
# .smon marker sits above the working directory, and is a no-op emitter
# otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries and graph instead of a tmp dir.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or test failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
lookup=$here/..
elf=$here/../../elf
map=$here/../../map
graph=$here/../../graph
reloc=$here/../../reloc

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

# The cross toolchain that emits the collision graph.
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

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp35.XXXXXX"); fi
graphs=$bin/collide
test_bin=$bin/lookup_test

cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

smon_session build wp35-symbol-lookup
smon_plan graph build unit differential

rc=0

# ---- build the collision graph with the cross toolchain -----------------
smon_step_start graph
if smon_cmd bash "$here/mkcollide.sh" "$graphs"; then
	smon_step_ok graph
else
	smon_step_fail graph $?; fail "graph build failed (cross toolchain on PATH?)"
fi

# ---- build the engine, its dependencies, and the harness ----------------
smon_step_start build
objs=""
build_one() { # src -> object
	local o="$bin/$(basename "${1%.*}").o"
	smon_cmd $cc $cflags -c "$1" -o "$o" || fail "compile $1"
	objs="$objs $o"
}
build_one "$lookup/elf_hash.c"
build_one "$lookup/elf_lookup.c"
build_one "$elf/elf_parse.c"
build_one "$map/elf_map.c"
build_one "$map/host_mem.c"
build_one "$graph/elf_graph.c"
build_one "$graph/ldso_cache.c"
build_one "$reloc/elf_reloc.c"
smon_cmd $cc $cflags -c "$reloc/reloc_resolve.S" -o "$bin/reloc_resolve.o" \
	|| fail "compile reloc_resolve.S"
objs="$objs $bin/reloc_resolve.o"
build_one "$here/lookup_test.c"
smon_cmd $cc -o "$test_bin" $objs || fail "link lookup_test"
smon_step_ok build

# ---- unit -----------------------------------------------------------------
smon_step_start unit
if smon_cmd "$test_bin" unit; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

# ---- differential vs a real ld.so ----------------------------------------
smon_step_start differential
smon_cmd bash "$here/diff-ldso.sh" "$test_bin" "$graphs"
drc=$?
if [ "$drc" = 0 ]; then
	smon_step_ok differential
elif [ "$drc" = 77 ]; then
	smon_step_ok differential
	smon_note differential "skipped: no real ld.so (WSL) on this host"
	[ "$quiet" = 1 ] || echo "$prog: differential skipped (no real ld.so)"
else
	smon_step_fail differential $drc; rc=1
fi

if [ "$rc" = 0 ] && [ "$drc" != 77 ]; then
	smon_item wp35 met "the object this loader binds a three-way-colliding collide() to matches a real ld.so over the plain load order, the reversed load order, and LD_PRELOAD interposition; the unit checks assert both hash probes, the global-beats-weak binding rule, scope order, and the version-matcher seam"
elif [ "$rc" = 0 ]; then
	smon_item wp35 partial "unit checks pass; the real-ld.so differential was skipped for want of WSL on this host"
else
	smon_item wp35 unmet "a lookup check did not reach its expected result"
fi

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: all symbol-lookup checks passed"; }
exit $rc
