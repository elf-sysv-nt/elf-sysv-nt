#!/usr/bin/env bash
#
# Checks on the spike itself, not on Windows.
#
# Four kinds. What the two commands refuse, because an option parser that
# accepts nonsense quietly is how a transcript ends up recording a run nobody
# asked for. That the summary is stable across runs, because a spike is correct
# when rerunning it regenerates its transcript and a shape that wandered would
# make a real change indistinguishable from noise. That the first run's own
# defect stays fixed, since the probe once reported a fault it had certainly
# taken as an access that never faulted, and that reads from outside exactly
# like the verdict going the other way. And three negative controls.
#
# The controls are the reason this is a file rather than a paragraph in the
# README. A handled fault that came back with the right value means either that
# the handler emulated it or that the value was already there; a value read from
# the right offset means either that the offset came from the fault or that
# nothing checked; and a decoded length means either that the length was right
# or that nothing downstream of it ran. So the probe is built three more times:
# once refusing to supply a value, once emulating eight bytes off, and once
# reporting the length one short. The first two have to report wrong values and
# the third has to die, because resuming into the middle of an instruction is
# not a wrong answer.
#
# Usage:
#   run-tests.sh [options]
#
# Options:
#   -k, --keep     Do not delete the working directory.
#   -v, --verbose  Print every command as it runs.
#   -h, --help     Print this message and exit.

set -u

prog=run-tests
here=$(cd "$(dirname "$0")" && pwd)
spike=$(cd "$here/.." && pwd)
keep=0
verbose=0

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    sed -n '/^# Usage:/,/^$/ { s/^# \{0,1\}//; p; }' "$0"; exit 0 ;;
		-k|--keep)    keep=1; shift ;;
		-v|--verbose) verbose=1; shift ;;
		*)            printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
	esac
done

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || exit 1
cleanup() { [ "$keep" = 1 ] || rm -rf "$work"; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

pass=0
fail=0

ok() { pass=$((pass + 1)); printf '  ok    %s\n' "$1"; }
no() { fail=$((fail + 1)); printf '  FAIL  %s -- %s\n' "$1" "$2"; }

expect_status() {
	want=$1 what=$2
	shift 2
	[ "$verbose" = 1 ] && printf '    $ %s\n' "$*"
	"$@" > "$work/out" 2>&1
	got=$?
	if [ "$got" = "$want" ]; then
		ok "$what"
	else
		no "$what" "wanted status $want, got $got"
		[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/out"
	fi
}

expect_nonzero() {
	what=$1
	shift
	[ "$verbose" = 1 ] && printf '    $ %s\n' "$*"
	"$@" > "$work/out" 2>&1
	got=$?
	if [ "$got" != 0 ]; then
		ok "$what (exit $got)"
	else
		no "$what" 'it exited 0'
	fi
}

expect_key() {
	file=$1 key=$2 want=$3 what=$4
	got=$(sed -n "s/^ *$key=//p" "$file")
	if [ "$got" = "$want" ]; then
		ok "$what ($got)"
	else
		no "$what" "$key reported ${got:-nothing} rather than $want"
	fi
}

runner=$spike/measure-fs-base-fault.sh

printf 'what the runner refuses\n'
expect_status 2 'a non-numeric --rounds'  bash "$runner" --rounds many
expect_status 2 'a non-numeric --seconds' bash "$runner" --seconds soon
expect_status 2 'a non-numeric --threads' bash "$runner" --threads lots
expect_status 2 'a zero --seconds'        bash "$runner" --seconds 0
expect_status 2 'a zero --rounds'         bash "$runner" --rounds 0
expect_status 2 'an unknown option'       bash "$runner" --nonesuch
expect_status 2 'a positional argument'   bash "$runner" surplus
expect_status 0 'a version request'       bash "$runner" --version
expect_status 0 'a help request'          bash "$runner" --help

# One real run, kept, because everything below needs the built probe and
# building it twice would let the two copies disagree. The counts are cut right
# down: what is under test here is the spike, not Windows.
printf '\none short run, kept for what follows\n'
expect_status 0 'every case passes' \
	bash "$runner" --keep-binary "$work/bin" --output "$work/run.txt" \
	--seconds 1 --rounds 5000 --threads 2 --quiet
probe=$work/bin/fs-fault-probe.exe

printf '\nwhat the probe refuses\n'
if [ ! -x "$probe" ]; then
	no 'the probe was built' 'the kept run left no probe behind'
else
	expect_status 2 'an unknown option' "$probe" --nonesuch
	expect_status 2 'a zero --rounds'   "$probe" --rounds 0
	expect_status 2 'a --seconds of x'  "$probe" --seconds x
	expect_status 0 'a version request' "$probe" --version
	expect_status 0 'a help request'    "$probe" --help
fi

printf '\nthe run says what it should\n'
if [ -s "$work/run.txt" ]; then
	expect_key "$work/run.txt" verdict faults-resumable 'the verdict'
	expect_key "$work/run.txt" cases_failed 0 'no case failed'
	expect_key "$work/run.txt" cases_incomplete 0 'no case went unrun'
	expect_key "$work/run.txt" spin_wrong 0 'nothing wrong under preemption'
	expect_key "$work/run.txt" thread_wrong 0 'nothing wrong under concurrency'
	expect_key "$work/run.txt" faults_refused 1 'exactly one form refused'
fi

# The defect the first run had. Every access in the event section faulted and
# every one was reported as having read a value, because a plain static flag
# written from a Windows callback is a flag the optimizer may cache. From
# outside, that is indistinguishable from the answer this spike exists to rule
# out, so the shape line is checked for it by name rather than left to a total.
printf '\nno event reports a read\n'
if [ -s "$work/run.txt" ]; then
	if sed -n 's/^ *event_shape=//p' "$work/run.txt" | grep -q ':read'; then
		no 'no event read through a zeroed base' \
		   'an event reported a read; see event_shape'
	else
		ok 'no event read through a zeroed base'
	fi
fi

printf '\nthe summary is stable across runs\n'
if [ -x "$probe" ]; then
	for round in one two; do
		"$probe" --terse --seconds 1 --rounds 5000 --threads 2 \
			> "$work/shape-$round.txt" 2>&1
		sed -n 's/^shape=//p' "$work/shape-$round.txt" > "$work/shape-$round"
	done
	if [ -s "$work/shape-one" ] && cmp -s "$work/shape-one" "$work/shape-two"; then
		ok 'two runs agree on every case'
	else
		no 'two runs agree on every case' 'the shapes differ'
		[ "$verbose" = 1 ] && diff "$work/shape-one" "$work/shape-two"
	fi
fi

build_control() {
	gcc -O1 -Wall -std=gnu99 -mno-red-zone "-D$1" -o "$work/$2.exe" \
		"$spike/fs-fault-probe.c" -lpthread > "$work/$2.log" 2>&1
}

printf '\nthe handler can fail to emulate\n'
if build_control SPIKE_FAULT_NO_EMULATE noemul; then
	expect_status 3 'a handler that supplies no value is caught' \
		"$work/noemul.exe" --terse --seconds 1 --rounds 5000 --threads 2
	"$work/noemul.exe" --terse --seconds 1 --rounds 5000 --threads 2 \
		> "$work/noemul.txt" 2>&1
	if sed -n 's/^shape=//p' "$work/noemul.txt" | grep -q 'mov %fs:0x0,r64:fail'; then
		ok 'and names the load form that came back poisoned'
	else
		no 'the load form fails under no-emulate' 'the shape says otherwise'
	fi
else
	no 'the no-emulate build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/noemul.log"
fi

printf '\nthe offset can be wrong\n'
if build_control SPIKE_FAULT_BAD_OFFSET badoff; then
	expect_status 3 'an emulation eight bytes off is caught' \
		"$work/badoff.exe" --terse --seconds 1 --rounds 5000 --threads 2
	"$work/badoff.exe" --terse --seconds 1 --rounds 5000 --threads 2 \
		> "$work/badoff.txt" 2>&1
	got=$(sed -n 's/^spin_wrong=//p' "$work/badoff.txt")
	case ${got:-0} in
		0) no 'the preemption case fails under a bad offset' \
		      'spin_wrong stayed at zero' ;;
		*) ok "and the preemption case sees it ($got wrong)" ;;
	esac
else
	no 'the bad-offset build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/badoff.log"
fi

# No expected status here beyond "not zero". A length one short resumes into the
# middle of the instruction it was meant to step over, and what happens next is
# whatever those bytes mean; it is not a case that fails, it is a process that
# stops. That it stops is the point.
printf '\nthe decoded length is load-bearing\n'
if build_control SPIKE_FAULT_BAD_LENGTH badlen; then
	# Run from the working directory: Cygwin writes a stack dump beside the
	# process, and a control that is meant to die should not leave one in
	# the spike.
	expect_nonzero 'a length one byte short does not survive' \
		sh -c 'cd "$0" && exec timeout 60 ./badlen.exe --terse --seconds 1 --rounds 5000 --threads 2' \
		"$work"
else
	no 'the bad-length build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/badlen.log"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
