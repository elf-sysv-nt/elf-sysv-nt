#!/usr/bin/env bash
#
# Checks on the spike itself, not on Windows.
#
# Three kinds. What the two commands refuse, because an option parser that
# accepts nonsense quietly is how a transcript ends up recording a run nobody
# asked for. That the summary is stable across runs, because a spike is
# correct when rerunning it regenerates its transcript and a shape that
# wandered would make a real change indistinguishable from noise. And two
# negative controls, for the two claims the spike's own output cannot show.
#
# Those last two are the reason this is a file rather than a paragraph in the
# README. A callee-saved mask of zero means either that nothing leaked or that
# the check stopped looking, and from outside those are the same number; a red
# zone reported intact means either that nothing wrote there or that the
# watcher went blind. So the probe is built twice more -- once with the callee
# swapped for leaky_face, which destroys every register it is not entitled to
# destroy, and once with the watcher writing into its own painted region --
# and the checks have to go red against both. Watched failing before they are
# believed passing.
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

expect_key() {
	file=$1 key=$2 want=$3 what=$4
	got=$(sed -n "s/^ *$key=//p" "$file")
	if [ "$got" = "$want" ]; then
		ok "$what ($got)"
	else
		no "$what" "$key reported ${got:-nothing} rather than $want"
	fi
}

runner=$spike/abi-crossing.sh

printf 'what the runner refuses\n'
expect_status 2 'a non-numeric --timeout' bash "$runner" --timeout soon
expect_status 2 'a non-numeric --rounds'  bash "$runner" --rounds many
expect_status 2 'a non-numeric --events'  bash "$runner" --events lots
expect_status 2 'a zero --depth'          bash "$runner" --depth 0
expect_status 2 'a --depth off the word'  bash "$runner" --depth 12
expect_status 2 'an unknown option'       bash "$runner" --nonesuch
expect_status 2 'a positional argument'   bash "$runner" surplus
expect_status 0 'a version request'       bash "$runner" --version

# One real run, kept, because everything below needs the built probe and
# building it twice would let the two copies disagree. The counts are cut
# right down: what is under test here is the harness, not Windows.
printf '\none short run, kept for what follows\n'
expect_status 0 'the crossing cases pass' \
	bash "$runner" --keep --work "$work/run" --output "$work/run.txt" \
	--rounds 2000 --events 50 --quiet
probe=$work/run/crossing.exe

printf '\nwhat the probe refuses\n'
if [ ! -x "$probe" ]; then
	no 'the probe was built' 'the kept run left no probe behind'
else
	expect_status 2 'an unknown option'    "$probe" --nonesuch
	expect_status 2 'an unknown case name' "$probe" --case nosuch
	expect_status 2 'a --depth off the word' "$probe" --depth 12
	expect_status 2 'a zero --rounds'      "$probe" --rounds 0
	expect_status 2 'a missing value'      "$probe" --depth
	expect_status 0 'a version request'    "$probe" --version
	expect_status 0 'a help request'       "$probe" --help
fi

printf '\nthe summary is stable across runs\n'
if [ -x "$probe" ]; then
	for round in one two; do
		"$probe" --terse --rounds 2000 --events 50 > "$work/shape-$round.txt" 2>&1
		sed -n 's/^shape=//p' "$work/shape-$round.txt" > "$work/shape-$round"
	done
	if [ -s "$work/shape-one" ] && cmp -s "$work/shape-one" "$work/shape-two"; then
		ok 'two runs agree on every case'
	else
		no 'two runs agree on every case' 'the shapes differ'
		[ "$verbose" = 1 ] && diff "$work/shape-one" "$work/shape-two"
	fi
fi

# The first negative control. leaky_face returns having destroyed everything a
# callee of either convention owes its caller, so every bit in both masks has
# to light: 0x3d for System V, which is %rbx and %r12 through %r15 with %rbp
# left alone, 0xfd for Microsoft, which adds %rsi and %rdi, and 0x3ff fo
# %xmm6 through %xmm15.
printf '\nthe register checks can fail\n'
if gcc -O1 -DSPIKE_CLOBBER -Wall -o "$work/clobber.exe" \
	"$spike/crossing.c" "$spike/crossing.S" > "$work/clobber.log" 2>&1; then
	"$work/clobber.exe" --terse --case sysv-face > "$work/clobber-sysv.txt" 2>&1
	expect_key "$work/clobber-sysv.txt" sysv_callee_saved_mask 0x3d \
		'a callee that saves nothing loses every System V register'
	"$work/clobber.exe" --terse --case ms-face > "$work/clobber-ms.txt" 2>&1
	expect_key "$work/clobber-ms.txt" ms_callee_saved_gpr_mask 0xfd \
		'and every Microsoft GPR but the frame pointer'
	expect_key "$work/clobber-ms.txt" ms_callee_saved_xmm_mask 0x3ff \
		'and all ten callee-saved XMMs'
else
	no 'the clobber build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/clobber.log"
fi

# The second. The quiet case is the control every red-zone measurement leans
# on, so it has to be shown capable of going red. This build has the watche
# write one word into its own painted region on every pass, and nothing else
# changes.
printf '\nthe red-zone watcher can fail\n'
if gcc -O1 -DSPIKE_REDZONE_SELF_CLOBBER -Wall -o "$work/selfclobber.exe" \
	"$spike/crossing.c" "$spike/crossing.S" > "$work/self.log" 2>&1; then
	expect_status 1 'a watcher that scribbles reports itself' \
		"$work/selfclobber.exe" --terse --case rz-quiet --rounds 2000
	"$work/selfclobber.exe" --terse --case rz-quiet --rounds 2000 \
		> "$work/self.txt" 2>&1
	# One short of the round count, and it has to be: the scribble lands
	# after the scan, so the last one is never looked at again.
	expect_key "$work/self.txt" redzone_quiet 'nearest:8,furthest:8,words:1999' \
		'and says which word it lost, once per pass'
else
	no 'the self-clobber build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/self.log"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
