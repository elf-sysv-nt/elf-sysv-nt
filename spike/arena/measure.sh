#!/usr/bin/env bash
#
# Can the kernel own the whole user address range as a placeholder reservation
# and replace pieces of it with section views?
#
# Proposal 0011 section 4.4 puts the N substrate's entire address space on that
# sentence, and its own "Not verified" section says the sentence was read out of
# documentation rather than measured: "that placeholders accept a section view
# at 64 KB inside a reservation of tens of terabytes". Phase 0's (c) spike is
# where that becomes a transcript. This builds the native probe, runs it, and
# writes a dated transcript with a verdict.
#
# The probe is built with the mingw cross compiler and not with Cygwin's gcc,
# because the question belongs to the NT memory manager and a Cygwin binary
# would ask it through a layer that has opinions of its own about mmap.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -i FILE, --input=FILE   Render from a probe output brought back from another host: no build, no run.
#   -p N, --pages=N         Pages in the fault-cost sweep. [default: 2048]
#   -k, --keep              Keep the built binary beside the sources.
#   -q, --quiet             Errors only.
#   -v, --verbose           Pass --verbose to the probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_ARENA_<OPTION>.

set -u

prog=measure-arena
release='measure-arena 1.1'
here=$(cd "$(dirname "$0")" && pwd)

output=${MEASURE_ARENA_OUTPUT:--}
input=${MEASURE_ARENA_INPUT:-}
pages=${MEASURE_ARENA_PAGES:-2048}
keep=${MEASURE_ARENA_KEEP:-0}
quiet=${MEASURE_ARENA_QUIET:-0}
verbose=${MEASURE_ARENA_VERBOSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-i|--input)   input=${2:-}; shift 2 ;;
		--input=*)    input=${1#*=}; shift ;;
		-p|--pages)   pages=${2:-}; shift 2 ;;
		--pages=*)    pages=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-v|--verbose) verbose=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

cc=x86_64-w64-mingw32-gcc
[ -n "$input" ] || command -v "$cc" >/dev/null 2>&1 || die "no $cc on PATH"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/arena-probe.exe; else bin=$work/arena-probe.exe; fi

if [ -z "$input" ]; then
note 'building the probe'
"$cc" -std=gnu11 -O1 -Wall -Wextra -o "$bin" \
	"$here/arena-probe.c" "$here/arena-fault.c" > "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args="--pages $pages"
[ "$verbose" = 1 ] && probe_args="$probe_args --verbose"

note 'running the probe'
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.raw" 2>"$work/probe.err"
[ -s "$work/probe.raw" ] || { cat "$work/probe.err" >&2; die 'the probe produced nothing'; }

# The probe is a native binary, so its stdout arrives with CRLF line endings and
# every value read out of it would carry a trailing carriage return. Strip them
# once, here, rather than at each of the two dozen reads below.
tr -d '\r' < "$work/probe.raw" > "$work/probe.out"
else
	tr -d '\r' < "$input" > "$work/probe.out" || die "cannot read $input"
fi

val() { sed -n "s/^$1=//p" "$work/probe.out"; }

q1_ok=$(val q1_placeholder_reserved)
q1_largest=$(val q1_largest_span)
q1_multi=$(val q1_multi_terabyte)
q1_refused=$(val q1_first_refusal_span)
# The status name, not its number: the number reduces to N when two transcripts
# are compared, and a refusal that changes its reason has to show as a word.
q1_refstat=$(val q1_first_refusal_status | awk '{print $2}')
q2_ok=$(val q2_split_placeholder)
q3_ok=$(val q3_view_replaced_64k)
q3_rw=$(val q3_readback_64k)
q4_ok=$(val q4_view_replaced_4k)
q4_gran=$(val q4_granularity)
q4_split=$(val q4_split_4k_size)
q5_ok=$(val q5_file_backed_view)
q5_match=$(val q5_file_contents_match)
q5_exec=$(val q5_file_backed_exec_view)
q5_gran=$(val q5_granularity)
q5_offgran=$(val q5_offset_granularity)
q6_ok=$(val q6_lazy_commit_veh)
q7_fault=$(val q7_fault_ns)
q7_ctrl=$(val q7_control_ns)
q8_ok=$(val q8_placeholder_survives_neighbours)
q8_below=$(val q8_neighbour_below)
q8_above=$(val q8_neighbour_above)

# The finding, in the order the arena falls apart. Each rung is a different
# thing going wrong and a different consequence for section 4.4: no placeholder
# at all and the design has no address space; a placeholder that will not take a
# view and the VMA tree has no way to realise a mapping; no file-backed view and
# ELF segments cannot come off disk; a disturbed neighbour and the arena is not
# an arena. Only the last two rungs are good news, and they differ by the
# granularity the kernel would then have to impose on ELF segments.
if [ "$q1_ok" != yes ]; then
	finding=placeholder-refused
elif [ "$q2_ok" != yes ] || [ "$q3_ok" != yes ] || [ "$q3_rw" != yes ]; then
	finding=no-placeholder-replacement
elif [ "$q5_ok" != yes ] || [ "$q5_match" != yes ]; then
	finding=no-file-backed-replacement
elif [ "$q8_ok" != yes ]; then
	finding=neighbours-disturbed
elif [ "$q6_ok" != yes ]; then
	finding=no-lazy-commit
elif [ "$q1_multi" != accepted ]; then
	finding=arena-holds-but-not-multi-terabyte
elif [ "$q4_gran" = 4k ] && [ "$q5_gran" = 4k ] && [ "$q5_offgran" = 4k ]; then
	finding=arena-holds-at-4k
else
	finding=arena-holds-at-64k
fi

winver=$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')
[ -n "$winver" ] || winver=$(uname -s | sed 's/^CYGWIN_NT-//')

# The header facts: from this host, or from the lines run.cmd wrote at the
# top of a probe output collected on another.
if [ -n "$input" ]; then
	hdr() { sed -n "s/^# $1: //p" "$work/probe.out" | head -1; }
	h_host=$(hdr host); h_windows=$(hdr windows); h_cygwin="none, collected by $(hdr runner)"
	h_compiler=$(hdr compiler); h_probe=$(hdr probe)
else
	h_host="$(hostname 2>/dev/null)"; h_windows="$winver"; h_cygwin="$(uname -r)"
	h_compiler="$("$cc" --version | head -1)"; h_probe="$("$bin" --version)"
fi
{
	printf 'the arena: a placeholder reservation of the user range, replaced piecewise\n\n'
	printf 'host        %s\n' "$h_host"
	printf 'windows     %s\n' "$h_windows"
	printf 'cygwin      %s\n' "$h_cygwin"
	printf 'compiler    %s\n' "$h_compiler"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$h_probe"

	printf 'reading, question by question\n\n'
	printf '  q1  a placeholder over the user range: %s, largest span accepted %s, multi-terabyte %s\n' \
		"$([ "$q1_ok" = yes ] && printf 'reserved' || printf 'REFUSED')" \
		"$q1_largest" "$q1_multi"
	printf '      first refusal: %s\n' \
		"$([ "$q1_refused" = none ] && printf 'none, every span in the ladder was accepted' || printf 'at %s, %s' "$q1_refused" "$q1_refstat")"
	printf '  q2  splitting a piece out of the standing placeholder: %s, piece %s wide\n' \
		"$([ "$q2_ok" = yes ] && printf 'accepted' || printf 'REFUSED')" "$(val q2_piece_width)"
	printf '  q3  that piece replaced by a pagefile-backed view: %s, read and write through it %s\n' \
		"$([ "$q3_ok" = yes ] && printf 'accepted' || printf 'REFUSED')" \
		"$([ "$q3_rw" = yes ] && printf 'correct' || printf 'WRONG')"
	printf '  q4  the same at 4 KB: split %s, view %s, granularity %s\n' \
		"$q4_split" \
		"$([ "$q4_ok" = yes ] && printf 'accepted' || printf 'refused')" "$q4_gran"
	printf '  q5  a view of a real file replacing a placeholder: %s, contents %s, execute protection %s\n' \
		"$([ "$q5_ok" = yes ] && printf 'accepted' || printf 'REFUSED')" \
		"$([ "$q5_match" = yes ] && printf 'correct' || printf 'WRONG')" \
		"$([ "$q5_exec" = yes ] && printf 'accepted' || printf 'refused')"
	printf '      file view granularity %s, at a section offset aligned only to %s\n' \
		"$q5_gran" "$q5_offgran"
	printf '  q6  lazy commit from a vectored handler: %s\n' \
		"$([ "$q6_ok" = yes ] && printf 'the faulting instruction re-executed and stored' || printf 'DID NOT WORK')"
	printf '  q7  context, not a finding: fault and commit %s ns against a plain first touch at %s ns\n' \
		"$q7_fault" "$q7_ctrl"
	printf '  q8  around a replaced piece: %s; the neighbour below reads %s, above %s\n\n' \
		"$([ "$q8_ok" = yes ] && printf 'still splittable, and no view was disturbed' || printf 'THE ARENA DID NOT SURVIVE')" \
		"$q8_below" "$q8_above"

	printf 'raw\n\n'
	sed -e 's/^/    /' "$work/probe.out"

	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi
