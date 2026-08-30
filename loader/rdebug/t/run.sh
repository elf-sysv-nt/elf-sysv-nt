#!/usr/bin/env bash
#
# WP-39 certification: build the rendezvous with the host compiler and hold it
# to two bars. The unit test asserts what a debugger relies on without needing
# one -- that r_debug and link_map have the exact SVr4/gdb byte layout, that the
# map is walkable by the offset arithmetic gdb uses, that r_brk names the
# breakpoint function, and that r_state and the chain move correctly through a
# startup population, a dlopen add and a dlclose remove. The end-to-end test
# cross-links a real program against a real shared library, walks it with WP-33,
# lifts the walk into the rendezvous, and reads the object list back through
# r_map -- proving the moment the plan names, the day the loader can announce a
# second object. The live-gdb check (a gdb built for the triple sets a
# breakpoint in a dlopen'd object) is WP-60 and is deferred there; no gdb built
# for the triple exists yet, so it is not faked here. Every step reports through
# the session monitor when a .smon marker sits above the working directory.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries instead of a tmp dir.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or test failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
rdebug=$here/..
elf=$here/../../elf
graph=$here/../../graph

keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say()  { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

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

# The cross toolchain that emits the e2e specimens.
export PATH="$HOME/x-elfsysvnt/bin:$PATH"
xg=x86_64-elfsysvnt-linux-gnu-gcc

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

cc=gcc
cflags="-std=gnu11 -Wall -Wextra -O2 -Wno-unused-parameter"
# Freestanding, no libc: the specimens carry none, and they are never run --
# the graph reads their dynamic section statically. Pad segments to the Windows
# 64K granule (DR-0008), as the other cross specimens do.
xf="-ffreestanding -nostdlib -fcf-protection=none -O2"
xlf="-Wl,-z,max-page-size=0x10000"

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp39.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

srcs="$rdebug/rdebug.c $graph/elf_graph.c $graph/ldso_cache.c $elf/elf_parse.c"

smon_session build wp39-rdebug-rendezvous
smon_plan build unit specimens e2e

rc=0

# ---- build the unit test and the e2e driver -----------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$bin/rdebug_test" "$here/rdebug_test.c" "$rdebug/rdebug.c" &&
   smon_cmd $cc $cflags -o "$bin/graph_e2e" "$here/graph_e2e.c" $srcs; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "build failed"
fi

# ---- the layout-and-transitions unit test -------------------------------
smon_step_start unit
if smon_cmd "$bin/rdebug_test"; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

# ---- cross-link the e2e specimens ---------------------------------------
smon_step_start specimens
specimens_ok=0
if command -v "$xg" >/dev/null 2>&1; then
	if smon_cmd $xg $xf $xlf -fpic -shared -Wl,-soname,libsecond.so.0 \
		-o "$bin/libsecond.so.0" "$here/second.c" &&
	   smon_cmd $xg $xf $xlf -fpie -pie -Wl,-e,entry \
		-o "$bin/root" "$here/root.c" \
		-L"$bin" -l:libsecond.so.0 -Wl,-rpath,"$bin"; then
		specimens_ok=1
		smon_step_ok specimens
	else
		smon_step_fail specimens $?; rc=1
	fi
else
	smon_step_ok specimens
	smon_note specimens "cross gcc $xg not on PATH; e2e skipped"
	say "$prog: cross toolchain absent, e2e skipped"
fi

# ---- the end-to-end announce-a-second-object test -----------------------
e2e_ran=0
smon_step_start e2e
if [ "$specimens_ok" = 1 ]; then
	if smon_cmd "$bin/graph_e2e" "$bin/root"; then
		e2e_ran=1
		smon_step_ok e2e
	else
		smon_step_fail e2e $?; rc=1
	fi
else
	smon_step_ok e2e
fi

# ---- the item verdict ---------------------------------------------------
if [ "$rc" = 0 ] && [ "$e2e_ran" = 1 ]; then
	smon_item wp39 met "r_debug and link_map are byte-correct against the SVr4/gdb layout, the map is walkable by gdb's offsets, r_brk names _dl_debug_state, r_state and the chain move correctly through add and remove, and the loader announces a second real object walked from WP-33; live gdb deferred to WP-60"
elif [ "$rc" = 0 ]; then
	smon_item wp39 partial "layout and transitions certified; the end-to-end second-object walk was skipped for want of the cross toolchain on this host"
else
	smon_item wp39 unmet "a rendezvous check did not reach its expected result"
fi

say ""
if [ "$rc" = 0 ]; then say "all rendezvous checks passed"; else say "a rendezvous check failed"; fi
smon_end $rc
exit $rc
