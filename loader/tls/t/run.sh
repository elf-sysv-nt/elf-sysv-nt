#!/usr/bin/env bash
#
# WP-37 certification: build the loader's TLS with the host gcc that targets
# x86_64-pc-cygwin -- the synthetic style WP-36 uses, no cross build -- and hold
# it to the done-when bar. The unit lays out two PT_TLS modules as the initial
# static set and asserts the static offsets are correct and aligned, that an
# initial-exec/local-exec address (tp + offset) reads the module's init value,
# that __tls_get_addr returns the static module's own address and a lazily
# allocated correct block for a dynamic one, and that all four TLS models
# resolve against the one layout. Then, with the live carrier, a managed thread
# is created, a third module is registered after it (a dlopen), the thread
# resolves it, and teardown frees the dynamic block and the DTV.
#
# The test links runtime/tls/tp.c, which lives in the Cygwin world, so the
# build carries -mno-red-zone, the standing policy DR-0006 keeps until WP-43.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep   Keep the built binary in the work dir instead of a tmp.
#   -q, --quiet  Errors only.
#   -h, --help   Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)      # loader/tls/t
tls=$here/..                             # loader/tls
runtime_tls=$here/../../../runtime/tls   # runtime/tls

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

cc=gcc
cflags="-std=gnu11 -O2 -g -mno-red-zone -Wall -Wextra"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp37.XXXXXX"); fi
test_bin=$bin/tls_test.exe
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

smon_session build wp37-loader-tls
smon_plan build acceptance

rc=0

# --- build ---------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$test_bin" \
	"$here/tls_test.c" "$tls/elf_tls.c" "$runtime_tls/tp.c" -lpthread; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the loader-TLS unit did not build"
fi

# --- acceptance: the test's verdict --------------------------------------
smon_step_start acceptance
if smon_cmd "$test_bin"; then
	smon_step_ok acceptance
else
	smon_step_fail acceptance $?
	rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp37 met "the static TLS block is sized from the initial PT_TLS set with a documented surplus, the four TLS models resolve against one layout (initial-exec and local-exec as tp-relative offsets, general-dynamic and local-dynamic through __tls_get_addr), a module dlopen'd after a thread was created resolves in that thread with its block allocated lazily, and teardown frees the per-module dynamic blocks and the DTV."
else
	smon_item wp37 unmet "a build or acceptance check did not reach its expected result"
fi

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: the four models resolve and a prior thread sees a dlopen'd module; WP-37 certified"; }
exit $rc
