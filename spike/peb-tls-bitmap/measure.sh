#!/usr/bin/env bash
#
# Can a process reserve TlsSlots[63] by setting bit 63 of the PEB's TlsBitmap,
# so that no TlsAlloc, from any DLL loaded later, ever hands the slot out?
#
# Carrier C1 of spike 6 is one load through %gs at a fixed TlsSlots index and
# DR-0003 declined it for the hazard that TlsAlloc draws from the same bits.
# Proposal 0012's open question 1 recommends C1 with the bit reserved. This
# builds the native probe, runs it, and writes a dated transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -i FILE, --input=FILE   Render from a probe output brought back from another host: no build, no run.
#   -k, --keep              Keep the built binary beside the sources.
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u

prog=measure
release='measure 1.1'
here=$(cd "$(dirname "$0")" && pwd)
cc=${MEASURE_CC:-x86_64-w64-mingw32-gcc}

output=${MEASURE_OUTPUT:--}
input=${MEASURE_INPUT:-}
keep=${MEASURE_KEEP:-0}
quiet=${MEASURE_QUIET:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)     usage; exit 0 ;;
		-V|--version)  printf '%s\n' "$release"; exit 0 ;;
		-o|--output)   output=${2:-}; shift 2 ;;
		--output=*)    output=${1#*=}; shift ;;
		-i|--input)    input=${2:-}; shift 2 ;;
		--input=*)     input=${1#*=}; shift ;;
		-k|--keep)     keep=1; shift ;;
		-q|--quiet)    quiet=1; shift ;;
		--)            shift; break ;;
		-?*)           printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)             break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
[ -n "$input" ] || command -v "$cc" >/dev/null 2>&1 || die "no $cc on PATH"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/bitmap-probe.exe; else bin=$work/bitmap-probe.exe; fi

if [ -z "$input" ]; then
note 'building the probe'
"$cc" -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/bitmap-probe.c" > "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }
note 'running the probe'
"$bin" > "$work/probe.raw" 2>"$work/probe.err" || { cat "$work/probe.err" >&2; die 'the probe did not run'; }
tr -d '\r' < "$work/probe.raw" > "$work/probe.out"
else
	tr -d '\r' < "$input" > "$work/probe.out" || die "cannot read $input"
fi

val() { sed -n "s/^$1=//p" "$work/probe.out"; }
yn() { [ "$1" = 1 ] && printf 'yes' || printf 'no'; }

q1_buf=$(val q1_bitmap_buffer_is_peb_0x80); q1_bits=$(val q1_bits_at_start); q1_low=$(val q1_lowest_free_at_start)
q2=$(val q2_bit63_after_set)
q3_hit=$(val q3_slot63_handed_out); q3_prim=$(val q3_primary_slots_taken); q3_exp=$(val q3_expansion_slots_taken); q3_still=$(val q3_bit63_still_set)
q4a=$(val q4_tlssetvalue_63_ok); q4b=$(val q4_gs_read_matches); q4c=$(val q4_tlsgetvalue_sees_raw_write)
q5a=$(val q5_dll_loaded); q5b=$(val q5_slot63_handed_out_after_dll)
q6=$(val q6_new_thread_slot_at_start)

if [ "$q1_buf" = 1 ] && [ "$q2" = 1 ] && [ "${q3_hit:-1}" = 0 ] && [ "$q3_still" = 1 ] && [ "${q3_exp:-0}" -gt 0 ] \
   && [ "$q4a" = 1 ] && [ "$q4b" = 1 ] && [ "$q4c" = 1 ] && [ "$q5a" = 1 ] && [ "${q5b:-1}" = 0 ]; then
	finding=bit-reserved-tlsalloc-never-returns-63
else
	finding="bitmap-${q1_buf:-x}-set-${q2:-x}-handed-${q3_hit:-x}-after-dll-${q5b:-x}-carrier-${q4a:-x}${q4b:-x}${q4c:-x}"
fi

# The header facts: from this host, or from the lines run.cmd wrote at the
# top of a probe output collected on another.
if [ -n "$input" ]; then
	hdr() { sed -n "s/^# $1: //p" "$work/probe.out" | head -1; }
	h_host=$(hdr host); h_windows=$(hdr windows); h_cygwin="none, collected by $(hdr runner)"
	h_compiler=$(hdr compiler); h_probe=$(hdr probe)
else
	h_host="$(hostname 2>/dev/null)"; h_windows="$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"; h_cygwin="$(uname -r)"
	h_compiler="$("$cc" --version | head -1)"; h_probe="$("$bin" --version | tr -d '\r')"
fi
{
	printf 'reserving a TlsSlots index through the PEB bitmap\n\n'
	printf 'host        %s\n' "$h_host"
	printf 'windows     %s\n' "$h_windows"
	printf 'cygwin      %s\n' "$h_cygwin"
	printf 'compiler    %s\n' "$h_compiler"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$h_probe"
	printf 'reading, question by question\n\n'
	printf '  q1  the bitmap: 64 bits at PEB+0x80 %s; at start %s, lowest free index %s\n' "$(yn "$q1_buf")" "${q1_bits:-?}" "${q1_low:-?}"
	printf '  q2  RtlSetBit(TlsBitmap, 63) sets it: %s\n' "$(yn "$q2")"
	printf '  q3  seventy TlsAllocs: slot 63 handed out %s times; %s primary and %s expansion slots taken; bit 63 still set %s\n' \
		"${q3_hit:-?}" "${q3_prim:-?}" "${q3_exp:-?}" "$(yn "$q3_still")"
	printf '  q4  the slot as a carrier: TlsSetValue(63) %s, the %%gs read at 0x1678 agrees %s, a raw %%gs write is what TlsGetValue returns %s\n' \
		"$(yn "$q4a")" "$(yn "$q4b")" "$(yn "$q4c")"
	printf '  q5  a DLL loaded after the reservation, ten more allocs: slot 63 handed out %s times\n' "${q5b:-?}"
	printf '  q6  a new thread starts with the slot at %s\n\n' "${q6:-?}"
	printf 'raw\n\n'
	sed -e 's/^/    /' "$work/probe.out"
	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then cat "$work/report"; else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
