#!/usr/bin/env bash
#
# WP-23 certification: build the callback trampolines and their hand-written
# probe with the host gcc that targets x86_64-pc-cygwin, confirm the compiler
# emits SEH unwind data for the ms_abi trampolines, confirm the -mno-red-zone
# policy is in force, and run the crossing test. The verdict must be yes: a
# comparator, a thread start routine and an exception filter each survive a
# round trip called Microsoft x64 with the caller's callee-saved set intact and
# the arguments delivered to the System V side, and the two controls light so
# the register check is known able to fail.
#
# These are the host-facing side -- Windows calls the trampolines -- so they
# build with the host toolchain, as WP-22's do, not the cross one.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   callback-run.sh [options]
#
# Options:
#   -k, --keep    Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet   Errors only.
#   -h, --help    Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=callback-run
here=$(cd "$(dirname "$0")" && pwd)
core=$here/..

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
cflags="-std=gnu11 -O1 -g -mno-red-zone -Wall -Wextra -I$core"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp23.XXXXXX"); fi
test_bin=$bin/callback_test.exe
tramp_o=$bin/callback.o

smon_session build wp23-callback-trampolines
smon_plan build unwind redzone crossing

rc=0

# --- build ---------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$test_bin" \
	"$here/callback_test.c" "$here/callback_probe.S" "$core/callback.c" &&
   smon_cmd $cc $cflags -c "$core/callback.c" -o "$tramp_o"; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the trampolines did not build"
fi

# --- unwind: the ms_abi trampolines carry host-format unwind data ---------
# The trampoline is the one frame the host's unwinder may walk to and stop at.
# It can only be that if it carries .pdata/.xdata the host recognizes; the test
# asks RtlLookupFunctionEntry directly, and this confirms the records exist in
# the object at all.
smon_step_start unwind
if objdump -h "$tramp_o" | grep -q '\.pdata' &&
   objdump -h "$tramp_o" | grep -q '\.xdata'; then
	smon_step_ok unwind
else
	smon_step_fail unwind 1
	smon_note "callback.o carries no .pdata/.xdata; the host could not walk the trampolines" warn
	rc=1
fi

# --- redzone: the -mno-red-zone policy is in force ------------------------
smon_step_start redzone
rz_probe=$bin/rzprobe.c
cat > "$rz_probe" <<'CEOF'
__attribute__((sysv_abi, noinline))
long leaf(long a, long b){ volatile long t[4]; t[0]=a;t[1]=b;t[2]=a^b;t[3]=a+b;
	return t[0]+t[1]+t[2]+t[3]; }
CEOF
$cc -O2 -S -mno-red-zone -o "$bin/without.s" "$rz_probe" 2>/dev/null
$cc -O2 -S -o "$bin/with.s" "$rz_probe" 2>/dev/null
if [ -s "$bin/without.s" ] && [ -s "$bin/with.s" ] &&
   ! cmp -s "$bin/with.s" "$bin/without.s" &&
   grep -q -e 'subq.*%rsp' "$bin/without.s"; then
	smon_step_ok redzone
else
	smon_step_fail redzone 1
	smon_note "-mno-red-zone did not change codegen; the policy is not in force" warn
	rc=1
fi

# --- crossing: the test's verdict ----------------------------------------
smon_step_start crossing
if smon_cmd "$test_bin"; then
	smon_step_ok crossing
else
	smon_step_fail crossing $?
	rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp23 met "a qsort comparator, a thread start routine and an exception filter each survive a round trip called Microsoft x64 through their trampoline with the full Microsoft callee-saved set (rbx, rbp, rsi, rdi, r12-r15, xmm6-xmm15) intact and their arguments delivered to the System V side; the trampolines carry host-recognized SEH unwind data, and a de-bracketed control leaks exactly rsi/rdi/xmm6-xmm15 while a total-leak control lights every bit, so the register check is known able to fail."
else
	smon_item wp23 unmet "a build or crossing check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: the crossing holds; WP-23 certified"; }
exit $rc
