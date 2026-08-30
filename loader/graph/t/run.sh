#!/usr/bin/env bash
#
# WP-33 certification: build the object-graph walker, the cache and its tools,
# and the constructed dependency graphs; then hold the walk to two bars. The
# unit test asserts the internals -- breadth-first order, which search source
# resolved each name, the RPATH-versus-RUNPATH precedence and inheritance
# difference, a flagged missing dependency, a cache hit, and a cache reader that
# refuses a corrupt file. The differential asserts the load order matches a real
# glibc ld.so over every one of those graphs. Every step reports through the
# session monitor when a .smon marker sits above the working directory.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries and graphs instead of a tmp dir.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or test failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
graph=$here/..
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

# The cross toolchain that emits the Linux ELF graphs.
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

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp33.XXXXXX"); fi
graphs=$bin/graphs
cache=$bin/ld.so.cache
test_bin=$bin/graph_test
ldconfig_bin=$bin/elf-ldconfig
ldd_bin=$bin/elf-ldd

srcs="$graph/elf_graph.c $graph/ldso_cache.c $elf/elf_parse.c"

smon_session build wp33-object-graph
smon_plan graphs build-tools build-test cache conf unit differential

rc=0

smon_step_start graphs
if smon_cmd bash "$here/mkgraph.sh" "$graphs"; then
	smon_step_ok graphs
else
	smon_step_fail graphs $?; fail "graph build failed (cross toolchain on PATH?)"
fi

smon_step_start build-tools
if smon_cmd $cc $cflags -o "$ldd_bin" "$graph/elf_ldd.c" $srcs &&
   smon_cmd $cc $cflags -o "$ldconfig_bin" "$graph/ldconfig.c" \
	   "$graph/ldso_cache.c" "$elf/elf_parse.c"; then
	smon_step_ok build-tools
else
	smon_step_fail build-tools $?; fail "tool build failed"
fi

smon_step_start build-test
if smon_cmd $cc $cflags -o "$test_bin" "$here/graph_test.c" $srcs; then
	smon_step_ok build-test
else
	smon_step_fail build-test $?; fail "unit test build failed"
fi

smon_step_start cache
if smon_cmd "$ldconfig_bin" -o "$cache" "$graphs/cacheonly/hidden"; then
	smon_step_ok cache
else
	smon_step_fail cache $?; rc=1
fi

# WP-62: the search-path configuration in el8's shape. A conf file naming one
# directory and including conf.d/*.conf by a relative pattern must reach the
# objects in both directories, and a file that includes itself must terminate
# at the depth cap rather than recurse forever.
smon_step_start conf
confdir=$bin/conf
mkdir -p "$confdir/conf.d" "$confdir/a" "$confdir/b"
so_a=$(find "$graphs" -name 'lib*.so*' -type f | sort | head -1)
so_b=$(find "$graphs" -name 'lib*.so*' -type f | sort | tail -1)
cp "$so_a" "$confdir/a/" && cp "$so_b" "$confdir/b/"
printf '# comment\n\n%s\ninclude conf.d/*.conf\n' "$confdir/a" \
	> "$confdir/ld.so.conf"
printf '%s\n' "$confdir/b" > "$confdir/conf.d/dirs.conf"
printf 'include loop.conf\n' > "$confdir/conf.d/loop.conf"
conf_ok=1
smon_cmd "$ldconfig_bin" -o "$confdir/cache" -f "$confdir/ld.so.conf" \
	2>/dev/null || conf_ok=0
if [ "$conf_ok" = 1 ]; then
	listing=$("$ldconfig_bin" -o "$confdir/cache" -p) || conf_ok=0
	printf '%s\n' "$listing" | grep -q "$confdir/a/" || conf_ok=0
	printf '%s\n' "$listing" | grep -q "$confdir/b/" || conf_ok=0
fi
if [ "$conf_ok" = 1 ]; then
	smon_step_ok conf
else
	smon_step_fail conf 1; rc=1
fi

smon_step_start unit
if smon_cmd "$test_bin" "$graphs" "$cache"; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

# The differential needs a real ld.so through WSL; it exits 77 when none is
# present, which is a skip rather than a failure so a host without WSL can still
# build. The certification host has one and runs it for real.
smon_step_start differential
smon_cmd bash "$here/diff-ldso.sh" "$ldd_bin" "$graphs"
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
	smon_item wp33 met "ldd-equivalent output lists the same objects in the same order a real ld.so lists them, over a diamond, RPATH/RUNPATH precedence and inheritance, \$ORIGIN, a cache-only name, and a missing dependency; the cache reader refuses a corrupt file"
elif [ "$rc" = 0 ]; then
	smon_item wp33 partial "unit checks pass; the real-ld.so differential was skipped for want of WSL on this host"
else
	smon_item wp33 unmet "a graph check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: all object-graph checks passed"; }
exit $rc
