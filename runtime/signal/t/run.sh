#!/usr/bin/env bash
#
# WP-43 certification: build the signal path with the host gcc that targets
# x86_64-pc-cygwin and hold it to the done-when bar.
#
# The iretq probe runs first, because everything else rests on it. A return
# from a handler restores rip, rflags and rsp from a frame on a stack the
# runtime owns, and it does that with a same-privilege iretq rather than with
# setcontext's push-and-ret, which would write below the destination stack
# pointer and into the red zone this package exists to keep. If iretq is not
# available to user code on this host then the return path is wrong and no
# later result means anything, so the probe gates the run.
#
# The unit test then holds the frame to its arithmetic: the sizes and offsets a
# handler compiled against Linux headers computes for itself, the placement 128
# bytes below the interrupted stack pointer, the alternate stack, the mask and
# flag semantics, and one full round trip driven on a stack of its own so the
# reserved bytes can be painted and read back.
#
# The fuzz target feeds elf_sigframe_check mutated and truncated frames against
# a guard page under the undefined-behaviour sanitizer. It is the one thing
# here that reads bytes it did not write: the handler ran with a pointer to its
# own return state, and what comes back is installed into a register file and
# iretq'd into.
#
# The end-to-end test is the done-when itself. A worker thread runs a leaf whose
# accumulator lives only in its red zone; the main thread delivers into it
# through the host's real suspend, read, redirect and resume; and the fold is
# recomputed at the end from the seed and the round count. The control arm
# repeats it with the reservation switched off and is required to break the
# fold, so a probe that cannot see the damage cannot report a pass.
#
# Every step reports through the session monitor when a .smon marker sits above
# the working directory, and is a no-op emitter otherwise.
#
# Usage:
#   run.sh [options]
#
# Options:
#   -k, --keep      Keep the built binaries in the work dir instead of a tmp.
#   -n, --cases N   Fuzz cases (default 200000).
#   -e, --events N  Deliveries per end-to-end arm (default 500).
#   -q, --quiet     Errors only.
#   -h, --help      Print this message and exit.
#
# Exit: 0 everything passed, 1 a build or check failed, 2 usage.

set -u

prog=run
here=$(cd "$(dirname "$0")" && pwd)      # runtime/signal/t
sig=$here/..                             # runtime/signal
root=$sig/../..                          # the tree

keep=0
quiet=0
cases=200000
events=500

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
say() { [ "$quiet" = 1 ] || printf '%s\n' "$*"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)   usage; exit 0 ;;
		-k|--keep)   keep=1; shift ;;
		-q|--quiet)  quiet=1; shift ;;
		-n|--cases)  cases=${2:-200000}; shift 2 ;;
		-e|--events) events=${2:-500}; shift 2 ;;
		--)          shift; break ;;
		-?*)         printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)           break ;;
	esac
done

cc=gcc
# -mno-red-zone is the standing policy DR-0006 keeps until this package retires
# it. The certification is built with it precisely so that the one thing in the
# tree that does rely on the red zone is the hand-written leaf in spin.S, which
# is the residue a compiler flag never reaches.
cflags="-std=gnu11 -O2 -g -mno-red-zone -Wall -Wextra"
ubsan="-fsanitize=undefined -fsanitize-undefined-trap-on-error"

pkg="$sig/sigframe.c $sig/sigdisp.c $sig/sigenter.S"
host="$sig/sig_host.c"

smon_lib=/c/-/repo/session-monitor/lib/smon.sh
if [ -f "$smon_lib" ]; then . "$smon_lib"; fi
command -v smon_session >/dev/null 2>&1 || {
	smon_session() { :; }; smon_plan() { :; }; smon_step_start() { :; }
	smon_step_ok() { :; }; smon_step_fail() { :; }; smon_cmd() { "$@"; }
	smon_item() { :; }; smon_note() { :; }; smon_end() { :; }
}

if [ "$keep" = 1 ]; then bin=$here; else bin=$(mktemp -d "${TMPDIR:-/tmp}/wp43.XXXXXX"); fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$bin"; }
trap cleanup EXIT

smon_session build wp43-signals
smon_plan build iretq unit fuzz e2e

rc=0

# --- build ----------------------------------------------------------------
smon_step_start build
if smon_cmd $cc $cflags -o "$bin/iretq_probe" "$here/iretq_probe.c" &&
   smon_cmd $cc $cflags -o "$bin/unit" "$here/unit.c" "$here/trial.S" $pkg &&
   smon_cmd $cc $cflags $ubsan -o "$bin/fuzz" "$here/fuzz.c" $pkg &&
   smon_cmd $cc $cflags -o "$bin/sig_e2e" "$here/sig_e2e.c" "$here/spin.S" \
	    $pkg $host -lpthread; then
	smon_step_ok build
else
	smon_step_fail build $?; fail "the signal path did not build"
fi

# --- the return instruction, before anything rests on it ------------------
smon_step_start iretq
if smon_cmd "$bin/iretq_probe"; then
	smon_step_ok iretq
else
	smon_step_fail iretq $?
	smon_item wp43 unmet "a same-privilege iretq is not available to user code on this host, so the return path this package is built on does not exist here"
	smon_end 1
	fail "iretq is not usable in user mode on this host"
fi

# --- the shape, the policy, the placement and one round trip --------------
smon_step_start unit
if smon_cmd "$bin/unit"; then
	smon_step_ok unit
else
	smon_step_fail unit $?; rc=1
fi

# --- the returning frame against a guard page -----------------------------
smon_step_start fuzz
q=; [ "$quiet" = 1 ] && q=-q
if smon_cmd "$bin/fuzz" -n "$cases" $q; then
	smon_step_ok fuzz
else
	smon_step_fail fuzz $?; rc=1
fi

# --- the done-when --------------------------------------------------------
smon_step_start e2e
if smon_cmd "$bin/sig_e2e" -n "$events"; then
	smon_step_ok e2e
else
	smon_step_fail e2e $?; rc=1
fi

if [ "$rc" = 0 ]; then
	smon_item wp43 met "a signal delivered into a running thread through the host's own suspend, redirect and resume builds a Linux rt_sigframe on the ELF side, runs the handler, and returns through a same-privilege iretq with every general register, the flags and the stack pointer restored. The frame is placed 128 bytes below the interrupted stack pointer, and a hand-written leaf whose only live variable is in its red zone folds correctly across hundreds of deliveries; with the reservation switched off the same leaf's fold breaks, so the probe is watching the bytes it claims to watch. sigaltstack works and a delivery onto it leaves the interrupted stack untouched; SA_SIGINFO passes siginfo and ucontext, SA_RESTART decides the interrupted call, SA_NODEFER and SA_RESETHAND mean what they mean. The return path re-derives every invariant from the returning bytes and is fuzzed against a guard page."
else
	smon_item wp43 unmet "a build or acceptance check did not reach its expected result"
fi

smon_end $rc
[ "$rc" = 0 ] && say "$prog: signals deliver onto an ELF stack and return with the red zone whole; WP-43 certified"
exit $rc
