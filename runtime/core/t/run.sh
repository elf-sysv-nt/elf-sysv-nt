#!/usr/bin/env bash
#
# WP-22 certification: build the host-facing core and its probe with the host
# gcc that targets x86_64-pc-cygwin, confirm the compiler emits SEH unwind data
# for the ms_abi entry points, confirm the -mno-red-zone policy is actually in
# force, and run the crossing test. The test's verdict must be yes: every
# entry point Windows calls in through reaches System V code and returns with
# the callee-saved set each convention promises intact, a fault beneath a
# System V frame reaches Cygwin and returns, and the two leaky controls light
# their masks so the register check is known able to fail.
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

# The host-facing core is compiled with the host toolchain, not the cross one:
# these are the functions Windows calls into, and they live in the Cygwin world
# the way spike 3's did. The cross toolchain builds the ELF/System V side.
cc=gcc
cflags="-std=gnu11 -O1 -g -mno-red-zone -Wall -Wextra"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp22.XXXXXX"); fi
test_bin=$bin/core_test.exe
entry_o=$bin/entry.o

smon_session build wp22-host-facing-core
smon_plan build unwind redzone crossing

rc=0

# --- build ---------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$test_bin" \
	"$here/core_test.c" "$here/probe.S" "$core/entry.c" &&
   smon_cmd $cc $cflags -c "$core/entry.c" -o "$entry_o"; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the core did not build"
fi

# --- unwind: the compiler emits host-format unwind data ------------------
# The ms_abi entry points must carry .pdata/.xdata, because Cygwin's exception
# and signal delivery is the host's own SEH walking those records. The runtime
# check inside the test asks RtlLookupFunctionEntry directly; this step
# confirms the records are present in the object at all, which is the thing the
# host then finds.
smon_step_start unwind
if objdump -h "$entry_o" | grep -q '\.pdata' &&
   objdump -h "$entry_o" | grep -q '\.xdata'; then
	smon_step_ok unwind
else
	smon_step_fail unwind 1
	smon_note "entry.o carries no .pdata/.xdata; the host could not walk these frames" warn
	rc=1
fi

# --- redzone: the -mno-red-zone policy is actually in force --------------
# DR-0006 keeps the flag as scaffolding until WP-43 repairs the delivery site.
# This confirms it changes code generation here rather than being a no-op: a
# sysv_abi leaf built with the flag adjusts %rsp, and the same leaf without it
# keeps its locals below %rsp and does not. If the two are identical the flag
# has stopped meaning anything and the policy is silently gone.
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
	smon_item wp22 partial "the host-facing entry points (DllMain, thread start, APC, TLS callback, vectored handler, signal landing) cross to System V code and back with each convention's callee-saved set intact and host-recognized SEH unwind data; a fault beneath a System V frame reaches Cygwin and returns. Certified at stand-in width: firing DllMain and PE TLS callbacks from a linked DLL is WP-41's, the down-hand trampolines are WP-23's, and the red-zone delivery repair is WP-43's."
else
	smon_item wp22 unmet "a build or crossing check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: the crossing holds; WP-22 certified at stand-in width"; }
exit $rc
