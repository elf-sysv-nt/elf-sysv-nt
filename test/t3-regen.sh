#!/usr/bin/env bash
#
# WP-T3 -- rerun every spike's script and hold the result to its committed
# transcript, so a spike whose script has rotted fails the way a broken unit
# test fails. A spike is correct when rerunning its script regenerates its
# recorded findings on the same machine; this is the runner that checks it.
#
# For each row of test/spike-regen.tsv it runs the regeneration command inside
# the spike directory and holds the fresh output to the committed transcript by
# its findings. A rerun reproduces the recorded findings or it has rotted; the
# exit status is not the test, because a spike whose finding is a negative
# verdict -- fs-base-persistence exits non-zero to report that the base does not
# survive -- reproduces correctly while exiting non-zero. So a script rots when
# it produces no transcript where it produced one, or when the findings move,
# not when it exits non-zero. Addresses, timings, dates, thread counts and tool
# versions differ between runs and are stripped; the findings do not differ and
# are compared.
#
# Not running a spike is reported two ways, because they mean different things.
# A spike the manifest marks SKIP has no regeneration by design and is not
# applicable here; it does not affect the verdict. A spike whose input is absent
# went unchecked -- the suite has no evidence it holds -- so it does not pass in
# silence: it makes the run INCOMPLETE and fails it, the way a green suite that
# quietly skipped its checks would be lying. A missing input is the reason to go
# find or regenerate the input, not to certify around it.
#
# Run it on an unloaded host. The host-measurement spikes -- fs-base, gs-tp,
# redzone -- time events whose outcome bends under scheduler pressure: a
# certified apc:pass reads as apc:fail, or a probe comes back empty, when heavy
# builds run alongside. That is a false failure of the instrument, not a rotted
# spike. A FAIL on a measurement spike is re-confirmed alone before it is
# believed; a clean certification is taken with nothing else building.
#
# Usage: t3-regen.sh [-o FILE] [SPIKE...]
#   with no SPIKE, every row in the manifest is run; otherwise only the named
#   spike directories. The manifest is $T3_MANIFEST or test/spike-regen.tsv.
# Exit: 0 only if every applicable spike regenerated its findings; non-zero if
#   any rotted, any findings moved, or any spike went unchecked for a missing
#   input (an incomplete run is not a passing one).

set -u
prog=t3-regen
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/.." && pwd)
manifest=${T3_MANIFEST:-$here/spike-regen.tsv}

out=-
while [ $# -gt 0 ] && [ "${1#-}" != "$1" ]; do
	case $1 in
		-o) shift; out=${1:?-o wants a file}; shift ;;
		-h|--help) sed -n '2,20p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
		*) echo "$prog: unknown option $1" >&2; exit 2 ;;
	esac
done
only=" $* "
[ "$out" = - ] || exec > "$out" 2>&1

export PATH=/c/-/x-elfsysvnt/bin:$PATH

# The findings of a transcript, reduced to its structure and verdicts. A rerun
# reproduces findings, not measurements: the case labels, the pass/fail words,
# the qualitative verdicts hold, while every number moves -- cycle counts, byte
# offsets, fold totals, timings. So every number is reduced to N, hex to 0xN,
# dates to DATE, and runs of whitespace to one space, which leaves the words --
# pass, holds, reachable, refused, the case names -- to compare, and lets a
# column shift when a number narrows read as no change. The provenance header
# is dropped whole: the compiler, kernel, host, binutils and date lines record
# where a transcript was made, not what it found, and they differ by design
# across the environments DR-0038 moved between. Those rows align their value
# under two or more spaces (or sit behind an = sign); a prose finding that opens
# with the same word -- "host mmap...", "kernel dispatch..." -- has a single
# space after it and is kept. Live temp paths collapse to TMPPATH, so an mktemp
# suffix and the root a transcript was captured under fall away; the rows of a
# VirtualQuery memory survey (MEM_FREE, MEM_COMMIT, MEM_RESERVE) drop whole,
# since the address space's region list varies run to run and is context, not a
# finding. A script that still runs regenerates the same words over different
# numbers and matches; a rotted one produces different words, or none, and does
# not. Sorted, so a reordering does not read as a change.
findings() {
	grep -avE '^[[:space:]]*($|#|Captured|Generated)|^[[:space:]]*(kernel|compiler|date|run_date|host|hostname|toolchain|uname|os|platform|ld|readelf|nm|objdump)([[:space:]]{2,}|=)|MEM_(FREE|COMMIT|RESERVE)' "$1" 2>/dev/null \
	| sed -E '
	    s@[^[:space:]]*[/\\][Tt][Mm][Pp][/\\][^[:space:]]*@TMPPATH@g;
	    s/0x[0-9a-fA-F]+/0xN/g;
	    s/[0-9]{4}-[0-9]{2}-[0-9]{2}(T[0-9:]+Z?)?/DATE/g;
	    s/[0-9]+\.[0-9]+/N/g;
	    s/[0-9]+/N/g;
	    s/[[:space:]]+/ /g;
	    s/^ //; s/ $//' \
	| sort
}

pass=0; fail=0; na=0; absent=0
printf '# WP-T3 spike regeneration -- %s\n\n' "$(date +%F)"

# The manifest is read on fd 3, not stdin, so that a regeneration command which
# consumes stdin -- wsl.exe does -- cannot swallow the rows that follow it and
# drop them from the run in silence. Spikes keep their own stdin untouched.
while IFS=$'\t' read -r dir needs txt cmd <&3; do
	case $dir in ''|\#*) continue ;; esac
	[ "$only" = "  " ] || case "$only" in *" $dir "*) ;; *) continue ;; esac
	# Label a row by its script, which is unique across the manifest, so the two
	# rows that share a directory read apart; fall back to the directory.
	scr=$(printf '%s\n' "$cmd" | grep -oE '[[:alnum:]_-]+\.sh' | head -1)
	label=${scr:+${scr%.sh}}; label=${label:-$dir}
	sdir=$root/spike/$dir
	[ -d "$sdir" ] || { printf '%-28s UNMET no such spike directory (manifest names one that is not here)\n' "$label"; absent=$((absent+1)); continue; }

	# Two kinds of not-run, and they are not the same. A spike the manifest
	# declares SKIP has no regeneration by design -- it is not applicable here
	# and does not taint the verdict. A spike whose input is simply absent went
	# unchecked: the suite has no evidence it holds, so it cannot be counted as
	# certified, and its absence makes the whole run incomplete rather than green.
	if [ "$needs" = SKIP ]; then
		printf '%-28s n/a   not applicable here (no standalone regeneration; see the spike)\n' "$label"; na=$((na+1)); continue
	fi
	if [ "$needs" != - ] && [ ! -e "$needs" ]; then
		printf '%-28s UNMET not checked -- input absent: %s\n' "$label" "$needs"; absent=$((absent+1)); continue
	fi

	# The exit status is deliberately not checked: a spike may report a
	# negative finding by exiting non-zero and still be reproducing exactly.
	fresh=$(mktemp)
	( cd "$sdir" && eval "$cmd" ) > "$fresh" 2>/dev/null
	if [ ! -s "$fresh" ]; then
		printf '%-28s FAIL  the script rotted: it regenerated no transcript\n' "$label"; fail=$((fail+1)); rm -f "$fresh"; continue
	fi

	committed=$(ls -t "$sdir"/$txt 2>/dev/null | head -1)
	if [ -z "$committed" ]; then
		printf '%-28s FAIL  no committed transcript matches %s\n' "$label" "$txt"; fail=$((fail+1)); rm -f "$fresh"; continue
	fi

	if [ "$(findings "$committed")" = "$(findings "$fresh")" ]; then
		printf '%-28s ok    regenerates %s\n' "$label" "$(basename "$committed")"; pass=$((pass+1))
	else
		printf '%-28s FAIL  findings moved from %s:\n' "$label" "$(basename "$committed")"; fail=$((fail+1))
		diff <(findings "$committed") <(findings "$fresh") | sed 's/^/     /'
	fi
	rm -f "$fresh"
done 3< "$manifest"

printf '\n%s: %d regenerated, %d rotted or moved, %d not applicable, %d not checked (input absent)\n' \
	"$prog" "$pass" "$fail" "$na" "$absent"
if [ "$absent" -gt 0 ]; then
	printf '%s: INCOMPLETE -- %d spike(s) went unchecked for want of an input; this is not a green certification.\n' \
		"$prog" "$absent"
fi
# Green requires that nothing rotted and that nothing went unchecked. A spike
# skipped for a missing input leaves the suite unable to certify it, so it fails
# the run exactly as a rotted spike would, rather than passing in silence.
[ "$fail" = 0 ] && [ "$absent" = 0 ]
