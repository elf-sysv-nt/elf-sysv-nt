#!/usr/bin/env bash
#
# Checks on the spike itself, not on Windows.
#
# Three kinds. What the two commands refuse, because an option parser that
# accepts nonsense quietly is how a transcript ends up recording a run nobody
# asked for. That the generator is deterministic, because a spike is correct
# when rerunning it regenerates its transcript and a generator that varied
# would make a real change indistinguishable from noise. And a negative
# control for the one claim in the spike that its own output cannot show: the
# trampoline preserving registers.
#
# That last one is why this is a file rather than a paragraph in the README.
# abi_probe reporting zero means either the trampoline saved everything or the
# check is broken, and from outside those look identical. So the stub is built
# a second time with -DSPIKE_NO_SAVE, which leaves the saving out and nothing
# else, and the check has to report 0xff and 0x3ff against it. Watched failing
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

gen=$spike/make-elf.py
runner=$spike/map-and-jump.sh

printf 'what the generator refuses\n'
expect_status 2 'no --code'            python3 "$gen" --output "$work/x.elf"
expect_status 2 'no --output'          python3 "$gen" --code "$0"
expect_status 2 'an unknown option'    python3 "$gen" --nonesuch
expect_status 2 'a non-numeric --base' python3 "$gen" --code "$0" --output "$work/x.elf" --base twelve
expect_status 1 'an --align under a page' \
	python3 "$gen" --code "$0" --output "$work/x.elf" --align 0x800 --quiet
expect_status 1 'an unaligned --base' \
	python3 "$gen" --code "$0" --output "$work/x.elf" --base 0x400001 --quiet
expect_status 0 'a version request'    python3 "$gen" --version

printf '\nwhat the runner refuses\n'
expect_status 2 'a non-numeric --timeout' bash "$runner" --timeout soon
expect_status 2 'a non-numeric --repeat'  bash "$runner" --repeat often
expect_status 2 'an unknown option'       bash "$runner" --nonesuch
expect_status 2 'a positional argument'   bash "$runner" surplus
expect_status 0 'a version request'       bash "$runner" --version

# One real run, kept, because everything below needs its specimens and its
# payload and building them twice would let the two copies disagree.
printf '\none run of the flat case, kept for what follows\n'
expect_status 0 'the flat case passes' \
	bash "$runner" --case flat --repeat 0 --keep --work "$work/run" --output "$work/run.txt"
expect_status 1 'an unknown case name is refused' \
	bash "$runner" --case nosuch --work "$work/none" --output "$work/none.txt"

printf '\nwhat the stub refuses\n'
stub=$work/run/map-and-jump-stub.exe
if [ ! -x "$stub" ]; then
	no 'the stub was built' 'the kept run left no stub behind'
else
	expect_status 2 'no argument'        "$stub"
	expect_status 2 'two arguments'      "$stub" a b
	expect_status 2 'an unknown option'  "$stub" --nonesuch
	expect_status 2 'a file that is not ELF' "$stub" "$0"
	expect_status 2 'a stack below a granule' "$stub" --stack 0x1000 "$work/run/flat.elf"
	expect_status 0 'a version request'  "$stub" --version
	expect_status 0 'a where request'    "$stub" --where
fi

printf '\nthe generator is deterministic\n'
if [ -r "$work/run/payload.bin" ]; then
	for round in one two; do
		python3 "$gen" --code "$work/run/payload.bin" \
			--output "$work/det-$round.elf" --base 0x400000 \
			--align 0x1000 --quiet
	done
	if cmp -s "$work/det-one.elf" "$work/det-two.elf"; then
		ok 'two runs produce the same bytes'
	else
		no 'two runs produce the same bytes' 'the images differ'
	fi
	as --64 -o "$work/again.o" "$spike/payload.S" 2>/dev/null &&
		objcopy -O binary --only-section=.text "$work/again.o" "$work/again.bin"
	if cmp -s "$work/again.bin" "$work/run/payload.bin"; then
		ok 'the payload assembles to the same bytes'
	else
		no 'the payload assembles to the same bytes' 'the blobs differ'
	fi
else
	no 'the payload was kept' 'the kept run left no payload behind'
fi

# The negative control. Same sources, same specimen, the saving left out.
printf '\nthe register check can fail\n'
if [ -r "$work/run/flat.elf" ]; then
	if gcc -O1 -DSPIKE_NO_SAVE -Wall -o "$work/nosave.exe" \
		"$spike/stub.c" "$spike/enter.S" > "$work/build.log" 2>&1; then
		"$work/nosave.exe" --terse "$work/run/flat.elf" > "$work/nosave.txt" 2>&1
		gprs=$(sed -n 's/^case_gpr_mask=//p' "$work/nosave.txt")
		xmms=$(sed -n 's/^case_xmm_mask=//p' "$work/nosave.txt")
		if [ "$gprs" = 0xff ]; then
			ok "a trampoline that saves nothing loses every GPR ($gprs)"
		else
			no 'a trampoline that saves nothing loses every GPR' \
				"reported $gprs rather than 0xff"
		fi
		if [ "$xmms" = 0x3ff ]; then
			ok "and every XMM ($xmms)"
		else
			no 'and every XMM' "reported $xmms rather than 0x3ff"
		fi
	else
		no 'the no-save build compiles' 'see the build log'
		[ "$verbose" = 1 ] && sed -e 's/^/      /' "$work/build.log"
	fi
else
	no 'the flat specimen was kept' 'the kept run left no specimen behind'
fi

printf '\n%d passed, %d failed\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
