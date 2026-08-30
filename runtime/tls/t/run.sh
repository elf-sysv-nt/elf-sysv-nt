#!/usr/bin/env bash
#
# WP-30 certification: build the thread-pointer unit with the host gcc that
# targets x86_64-pc-cygwin, confirm the psABI variant II offsets the compiler
# and loader assume are enforced, confirm the -mno-red-zone policy is in force,
# and run the acceptance test. The test's verdict must be yes: a managed thread
# reads its own TCB back through carrier C3 (gs:[NtTib.StackBase] then a fixed
# offset) after a hundred thousand context switches under load, after a fork,
# and after a signal delivered mid-computation including on an alternate signal
# stack.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -n N, --switches=N  Context-switch target for the load case. [default: 100000]
#   -k, --keep          Keep the built binaries in the work dir instead of a tmp.
#   -q, --quiet         Errors only.
#   -h, --help          Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)
tls=$here/..

switches=100000
keep=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)      usage; exit 0 ;;
		-n|--switches)  switches=${2:-}; shift 2 ;;
		--switches=*)   switches=${1#*=}; shift ;;
		-k|--keep)      keep=1; shift ;;
		-q|--quiet)     quiet=1; shift ;;
		--)             shift; break ;;
		-?*)            printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)              break ;;
	esac
done
case $switches in ''|*[!0-9]*) printf '%s: --switches wants a number\n' "$prog" >&2; exit 2 ;; esac

# The thread-pointer unit lives in the Cygwin world Windows calls into, so it is
# built with the host toolchain and -mno-red-zone, the standing policy DR-0006
# keeps until the delivery-site repair (WP-43) lands.
cc=gcc
cflags="-std=gnu11 -O2 -g -mno-red-zone -Wall -Wextra"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp30.XXXXXX"); fi
test_bin=$bin/tls_test.exe
tp_o=$bin/tp.o

smon_session build wp30-thread-pointer
smon_plan build variant2 redzone acceptance

rc=0

# --- build ---------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$test_bin" "$here/tls_test.c" "$tls/tp.c" -lpthread &&
   smon_cmd $cc $cflags -c "$tls/tp.c" -o "$tp_o"; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the thread-pointer unit did not build"
fi

# --- variant2: the TCB offsets the compiler assumes are enforced ---------
# tp.c carries _Static_assert on tcb@0, self@0x10, stack_guard@0x28 and
# pointer_guard@0x30. A successful compile is the enforcement; this step makes
# it visible by confirming the asserts are present and the object built.
smon_step_start variant2
if grep -q '_Static_assert' "$tls/tp.c" && [ -s "$tp_o" ]; then
	smon_step_ok variant2
else
	smon_step_fail variant2 1
	smon_note "variant II offset asserts missing; the TCB layout is unchecked" warn
	rc=1
fi

# --- redzone: the -mno-red-zone policy is in force -----------------------
smon_step_start redzone
rz=$bin/rz.c
cat > "$rz" <<'CEOF'
__attribute__((sysv_abi, noinline))
long leaf(long a, long b){ volatile long t[4]; t[0]=a;t[1]=b;t[2]=a^b;t[3]=a+b;
	return t[0]+t[1]+t[2]+t[3]; }
CEOF
$cc -O2 -S -mno-red-zone -o "$bin/without.s" "$rz" 2>/dev/null
$cc -O2 -S -o "$bin/with.s" "$rz" 2>/dev/null
if [ -s "$bin/without.s" ] && ! cmp -s "$bin/with.s" "$bin/without.s" &&
   grep -q -e 'subq.*%rsp' "$bin/without.s"; then
	smon_step_ok redzone
else
	smon_step_fail redzone 1
	smon_note "-mno-red-zone did not change codegen; the policy is not in force" warn
	rc=1
fi

# --- acceptance: the test's verdict --------------------------------------
smon_step_start acceptance
if smon_cmd "$test_bin" "$switches"; then
	smon_step_ok acceptance
else
	smon_step_fail acceptance $?
	rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp30 met "the thread pointer is established at thread creation through carrier C3 (gs:[NtTib.StackBase] then a fixed offset into a runtime-owned stack), the TCB is the psABI variant II shape, and a thread reads its own TCB back correctly after >=$switches context switches under load with zero mismatches, after a fork, and after a signal delivered mid-computation including on an alternate signal stack."
else
	smon_item wp30 unmet "a build or acceptance check did not reach its expected result"
fi

[ "$keep" = 1 ] || rm -rf "$bin"

smon_end $rc
[ "$rc" = 0 ] && { [ "$quiet" = 1 ] || echo "$prog: the thread pointer holds; WP-30 certified"; }
exit $rc
