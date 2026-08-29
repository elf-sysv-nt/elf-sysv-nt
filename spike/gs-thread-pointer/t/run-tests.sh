#!/usr/bin/env bash
#
# Checks on the spike itself, not on Windows.
#
# Three kinds. What the driver and the probe refuse, because an option parser
# that accepts nonsense quietly is how a transcript records a run nobody asked
# for. That the shape line is stable across round counts, because a spike is
# correct when rerunning it regenerates its result and a shape that drifted
# would make a real regression indistinguishable from noise. And a negative
# control for the claim the transcript cannot show on its own: that the probe
# would notice a carrier that failed.
#
# That last one is why this is a file rather than a paragraph in the README.
# Every carrier passes, and from outside a probe that saw a stable pointer and
# a probe that cannot see are the same output. So the probe is built a second
# time with -DSPIKE_BREAK_CARRIER, which biases the fetch by one and nothing
# else, and every available carrier has to come back fail. Watched failing
# before it is believed passing.
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

runner=$spike/measure-gs-tp.sh
source=$spike/gs-tp-probe.c

printf 'what the driver refuses\n'
expect_status 2 'a non-numeric --rounds'  bash "$runner" --rounds twelve
expect_status 2 'a non-numeric --seconds' bash "$runner" --seconds soon
expect_status 2 'a non-numeric --threads' bash "$runner" --threads many
expect_status 2 'an unknown carrier'      bash "$runner" --carrier C9
expect_status 2 'an unknown option'       bash "$runner" --nonesuch
expect_status 0 'a version request'       bash "$runner" --version
expect_status 0 'a help request'          bash "$runner" --help

# Build once, kept, so the checks below share one binary and cannot disagree
# about what the probe does.
probe=$work/gs-tp-probe.exe
printf '\nthe probe builds\n'
if gcc -O2 -Wall -Wextra -std=gnu99 -o "$probe" "$source" -lpthread \
	> "$work/build.log" 2>&1; then
	ok 'a clean build with -Wall -Wextra'
else
	no 'a clean build with -Wall -Wextra' 'see the build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/build.log"
fi

printf '\nwhat the probe refuses\n'
if [ -x "$probe" ]; then
	expect_status 2 'a non-numeric --rounds' "$probe" --rounds twelve
	expect_status 2 'an unknown carrier'     "$probe" --carrier C9
	expect_status 2 'an unknown option'      "$probe" --nonesuch
	expect_status 0 'a version request'      "$probe" --version
	expect_status 0 'a help request'         "$probe" --help
fi

# The shape line is what a rerun diffs. Two runs at different round counts must
# produce the same shape, character for character.
printf '\nthe shape is stable across round counts\n'
if [ -x "$probe" ]; then
	"$probe" -r 300 -s 1 -j 2 --terse > "$work/a.txt" 2>&1
	"$probe" -r 900 -s 1 -j 4 --terse > "$work/b.txt" 2>&1
	sa=$(sed -n 's/^shape=//p' "$work/a.txt")
	sb=$(sed -n 's/^shape=//p' "$work/b.txt")
	if [ -n "$sa" ] && [ "$sa" = "$sb" ]; then
		ok 'two round counts give the same shape'
	else
		no 'two round counts give the same shape' 'the shapes differ'
		[ "$verbose" = 1 ] && { printf '      %s\n' "$sa"; printf '      %s\n' "$sb"; }
	fi
	if grep -q '^carriers_fail=0$' "$work/a.txt"; then
		ok 'no available carrier fails a clean run'
	else
		no 'no available carrier fails a clean run' 'a carrier reported fail'
	fi
fi

# The negative control. Same source, the fetch biased by one, nothing else.
# Every available carrier must fail, or the passes above prove nothing.
printf '\nthe carrier check can fail\n'
broken=$work/gs-tp-broken.exe
if gcc -O2 -DSPIKE_BREAK_CARRIER -std=gnu99 -o "$broken" "$source" -lpthread \
	> "$work/broken-build.log" 2>&1; then
	"$broken" -r 300 -s 1 -j 2 --terse > "$work/broken.txt" 2>&1
	npass=$(grep -c ':pass' "$work/broken.txt")
	cfail=$(sed -n 's/^carriers_fail=//p' "$work/broken.txt")
	cpass=$(sed -n 's/^carriers_pass=//p' "$work/broken.txt")
	if [ "$cpass" = 0 ] && [ "${cfail:-0}" -ge 1 ]; then
		ok "a biased fetch fails every available carrier (fail=$cfail pass=$cpass)"
	else
		no 'a biased fetch fails every available carrier' \
			"reported pass=$cpass fail=$cfail"
	fi
	if [ "${npass:-1}" = 0 ]; then
		ok 'and no case in the shape reports pass'
	else
		no 'and no case in the shape reports pass' "$npass cases still passed"
	fi
else
	no 'the broken build compiles' 'see the broken build log'
	[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/broken-build.log"
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
