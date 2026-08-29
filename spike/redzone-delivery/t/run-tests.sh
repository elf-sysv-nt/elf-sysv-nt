#!/usr/bin/env bash
#
# Checks on the spike itself, not on Windows.
#
# Three kinds. What the two commands refuse, because an option parser that
# accepts nonsense quietly is how a transcript ends up recording a run nobody
# asked for. That the summary is stable across runs, because a spike is correct
# when rerunning it regenerates its transcript and a shape that wandered would
# make a real change indistinguishable from noise. And two negative controls,
# for the two claims the spike's own output cannot show.
#
# Those last two are the reason this is a file rather than a paragraph in the
# README. A red zone reported intact means either that nothing wrote there or
# that the watcher went blind, and from outside those are the same zero; a
# preserved computation means either that the reservation held the value or
# that nothing disturbed it. So the probe is built twice more -- once with the
# watcher writing into its own painted region, so the quiet control has to go
# red, and once with resume-integrity delivered naively onto %rsp-8, so its
# value has to break. Watched failing before they are believed passing.
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

runner=$spike/redzone-delivery.sh

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
# building it twice would let the two copies disagree. The counts are cut right
# down: what is under test here is the harness, not Windows.
printf '\none short run, kept for what follows\n'
expect_status 0 'the cases pass' \
	bash "$runner" --keep --work "$work/run" --output "$work/run.txt" \
	--rounds 2000 --events 40 --quiet
probe=$work/run/redzone.exe

printf '\nwhat the probe refuses\n'
if [ ! -x "$probe" ]; then
	no 'the probe was built' 'the kept run left no probe behind'
else
	expect_status 2 'an unknown option'      "$probe" --nonesuch
	expect_status 2 'an unknown case name'   "$probe" --case nosuch
	expect_status 2 'a --depth off the word' "$probe" --depth 12
	expect_status 2 'a zero --rounds'        "$probe" --rounds 0
	expect_status 2 'a missing value'        "$probe" --depth
	expect_status 0 'a version request'      "$probe" --version
	expect_status 0 'a help request'         "$probe" --help
fi

printf '\nthe summary is stable across runs\n'
if [ -x "$probe" ]; then
	for round in one two; do
		"$probe" --terse --rounds 2000 --events 40 > "$work/shape-$round.txt" 2>&1
		sed -n 's/^shape=//p' "$work/shape-$round.txt" > "$work/shape-$round"
	done
	if [ -s "$work/shape-one" ] && cmp -s "$work/shape-one" "$work/shape-two"; then
		ok 'two runs agree on every case'
	else
		no 'two runs agree on every case' 'the shapes differ'
		[ "$verbose" = 1 ] && diff "$work/shape-one" "$work/shape-two"
	fi
fi

# The first negative control. The watcher writes one word into its own painted
# region every round, so the quiet case -- the control every reserved
# measurement leans on -- has to go red against it, at offset 8 and one word
# short of the round count, because the scribble lands after the scan so the
# last one is never looked at again.
printf '\nthe watcher can fail\n'
if gcc -O1 -DSPIKE_REDZONE_SELF_CLOBBER -Wall -o "$work/selfclobber.exe" \
	"$spike/redzone.c" "$spike/redzone.S" > "$work/self.log" 2>&1; then
	expect_status 1 'a watcher that scribbles reports itself' \
		"$work/selfclobber.exe" --terse --case quiet --rounds 2000
	"$work/selfclobber.exe" --terse --case quiet --rounds 2000 \
		> "$work/self.txt" 2>&1
	expect_key "$work/self.txt" case_quiet \
		'fail,nearest:8,words:1999,deliveries:0,handled:0' \
		'and says which word it lost, once per pass'
else
	no 'the self-clobber build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/self.log"
fi

# The second. resume-integrity is delivered naively instead of reserved, so the
# handler frame takes %rsp-8 -- the accumulate watcher's own scratch word -- and
# the value it folds forward has to diverge from the one an undisturbed run
# reaches. The measurement passing is only meaningful if the same case can be
# made to fail.
printf '\nresume-integrity can fail\n'
if gcc -O1 -DSPIKE_INTEGRITY_NAIVE -Wall -o "$work/intnaive.exe" \
	"$spike/redzone.c" "$spike/redzone.S" > "$work/int.log" 2>&1; then
	expect_status 1 'a naive delivery breaks the preserved value' \
		"$work/intnaive.exe" --terse --case resume-integrity --events 200
	"$work/intnaive.exe" --terse --case resume-integrity --events 200 \
		> "$work/int.txt" 2>&1
	got=$(sed -n 's/^ *case_resume-integrity=//p' "$work/int.txt")
	case $got in
		fail,*) ok "and reports the case failed ($got)" ;;
		*)      no 'resume-integrity fails under naive delivery' \
			   "case_resume-integrity reported ${got:-nothing}" ;;
	esac
else
	no 'the integrity-naive build compiles' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/int.log"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
