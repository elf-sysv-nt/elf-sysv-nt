#!/usr/bin/env bash
#
# Can a process reserve TlsSlots[63] by setting bit 63 of the PEB's TlsBitmap,
# so that no TlsAlloc, from any DLL loaded later, ever hands the slot out?
# And where does the array end, so that the canary and the pointer guard the
# ABI keeps at fixed offsets from the thread pointer have somewhere to live?
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
release='measure 1.2'
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
q7a=$(val q7_slot63_offset_is_0x1678); q7b=$(val q7_array_span_bytes); q7c=$(val q7_first_byte_past_array)
q7d=$(val q7_a_slot_lands_on_tp_plus_8); q7e=$(val q7_a_slot_lands_on_tp_plus_16); q7n=$(val q7_indices_written)
q8a=$(val q8_bit62_after_set); q8b=$(val q8_bit61_after_set); q8c=$(val q8_reserved_slots_handed_out)
q8d=$(val q8_reserved_slots_handed_out_after_dll); q8e=$(val q8_bits61_63_still_set); q8p=$(val q8_primary_slots_taken)
q9a=$(val q9_gs_tp_matches); q9b=$(val q9_gs_canary_matches); q9c=$(val q9_gs_guard_matches)
q9d=$(val q9_tlsgetvalue_62_sees_raw_write)
q9t=$(val q9_new_thread_tp); q9k=$(val q9_new_thread_canary); q9g=$(val q9_new_thread_guard)

if [ "$q1_buf" = 1 ] && [ "$q2" = 1 ] && [ "${q3_hit:-1}" = 0 ] && [ "$q3_still" = 1 ] && [ "${q3_exp:-0}" -gt 0 ] \
   && [ "$q4a" = 1 ] && [ "$q4b" = 1 ] && [ "$q4c" = 1 ] && [ "$q5a" = 1 ] && [ "${q5b:-1}" = 0 ] \
   && [ "$q7a" = 1 ] && [ "${q7b:-0}" = 512 ] && [ "${q7d:-1}" = 0 ] && [ "${q7e:-1}" = 0 ] \
   && [ "$q8a" = 1 ] && [ "$q8b" = 1 ] && [ "${q8c:-1}" = 0 ] && [ "${q8d:-1}" = 0 ] && [ "$q8e" = 1 ] \
   && [ "$q9a" = 1 ] && [ "$q9b" = 1 ] && [ "$q9c" = 1 ] && [ "$q9d" = 1 ]; then
	finding=three-bits-reserved-array-ends-at-the-thread-pointer
else
	finding="bitmap-${q1_buf:-x}-set-${q2:-x}-handed-${q3_hit:-x}-after-dll-${q5b:-x}-carrier-${q4a:-x}${q4b:-x}${q4c:-x}-span-${q7b:-x}-past-${q7d:-x}${q7e:-x}-three-${q8c:-x}${q8d:-x}${q8e:-x}-abi-${q9a:-x}${q9b:-x}${q9c:-x}"
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
	printf '  q6  a new thread starts with the slot at %s\n' "${q6:-?}"
	printf '  q7  the array: slot 63 lands at 0x1678 %s, the array spans %s bytes, the first byte past it is %s; of %s held indices, one lands on TP+8 %s and on TP+16 %s\n' \
		"$(yn "$q7a")" "${q7b:-?}" "${q7c:-?}" "${q7n:-?}" "$(yn "${q7d:-x}")" "$(yn "${q7e:-x}")"
	printf '  q8  bits 62 and 61 set %s and %s; after freeing what q3 and q5 took, seventy allocs (%s primary) hand out a reserved slot %s times, ten more after a DLL load %s times; all three bits still set %s\n' \
		"$(yn "$q8a")" "$(yn "$q8b")" "${q8p:-?}" "${q8c:-?}" "${q8d:-?}" "$(yn "$q8e")"
	printf '  q9  the three words: %%gs at TP %s, at TP-8 %s, at TP-16 %s; a raw write to TP-8 is what TlsGetValue(62) returns %s; a new thread starts them at %s, %s, %s\n\n' \
		"$(yn "$q9a")" "$(yn "$q9b")" "$(yn "$q9c")" "$(yn "$q9d")" "${q9t:-?}" "${q9k:-?}" "${q9g:-?}"
	printf 'raw\n\n'
	sed -e 's/^/    /' "$work/probe.out"
	printf '\nverdict\n\n'
	printf '    finding=%s\n' "$finding"
} > "$work/report"

if [ "$output" = - ]; then cat "$work/report"; else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
