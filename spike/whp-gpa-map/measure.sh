#!/usr/bin/env bash
#
# What does WHvMapGpaRange do to the host memory behind it: populate it, pin
# it, or leave it to the memory manager? And what does the guest see when the
# host page behind a mapped GPA is reserved, decommitted, or released?
#
# Substrate H's whole memory story -- lazy commit "one level down", a 64 GB
# MAP_NORESERVE that costs nothing until touched, MADV_DONTNEED as a decommit
# -- rests on the answers. This is the first of the
# measurements that decide between H's two shapes, because pinning would hurt
# the single-kernel-process shape most. This builds the probe, runs it, and
# writes a dated transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -k, --keep              Keep the built binary beside the sources.
#   -q, --quiet             Errors only.
#   -v, --verbose           Pass --verbose to the probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${MEASURE_OUTPUT:--}
keep=${MEASURE_KEEP:-0}
quiet=${MEASURE_QUIET:-0}
verbose=${MEASURE_VERBOSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)       usage; exit 0 ;;
		-V|--version)    printf '%s\n' "$release"; exit 0 ;;
		-o|--output)     output=${2:-}; shift 2 ;;
		--output=*)      output=${1#*=}; shift ;;
		-k|--keep)       keep=1; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		-v|--verbose)    verbose=1; shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'
[ -r /usr/include/w32api/winhvplatform.h ] || die 'no winhvplatform.h under /usr/include/w32api'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/gpa-probe.exe; else bin=$work/gpa-probe.exe; fi

note 'building the probe'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/gpa-probe.c" -lwinhvplatform -lpsapi \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args=
[ "$verbose" = 1 ] && probe_args="--verbose"

note 'running the probe'
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }

val() { sed -n "s/^$1=//p" "$work/probe.out"; }
yn() { [ "$1" = 1 ] && printf 'yes' || printf 'no'; }

present=$(val hypervisor_present)
ready=$(val partition_ready)

# q2: mapping a committed gigabyte. Lazy if the working set did not move and
# no sampled page became resident; populating if pages became resident;
# pinning if any of those are locked.
q2_delta=$(val q2_working_set_delta_kb)
q2_res=$(val q2_after_map_resident)
q2_lock=$(val q2_after_map_locked)
q2_hv4k=$(val q2_hv_mapped_4k_pages)
if [ "${q2_lock:-0}" -gt 0 ]; then map_word=map-pins
elif [ "${q2_res:-0}" -gt 2 ]; then map_word=map-populates
else map_word=map-lazy; fi

# q3: the guest touching mapped, unresident pages.
q3_zero=$(val q3_first_touch_reads_zero)
q3_first=$(val q3_first_touch_median_ns)
q3_again=$(val q3_second_touch_median_ns)
q3_res=$(val q3_after_touch_resident)
q3_lock=$(val q3_after_touch_locked)
if [ "${q3_zero:-0}" = 256 ] && [ "${q3_lock:-1}" = 0 ]; then touch_word=touched-pages-resident-unlocked
elif [ "${q3_zero:-0}" = 256 ]; then touch_word=touched-pages-locked
else touch_word=touch-failed; fi

# q4: reserve-only host memory behind a mapping.
q4_map=$(val q4_map_reserved)
q4_exit=$(val q4_guest_touch_reserved_exit)
q4_acc=$(val q4_guest_touch_reserved_access | cut -d, -f1)
q4_after=$(val q4_guest_touch_after_commit_exit)
q4_child=$(val q4_child)
q4_n=$(val q4_commit_on_exit_resumed)
q4_ns=$(val q4_commit_on_exit_median_ns)
if [ "$q4_map" = accepted ] && [ "$q4_exit" = memory-access-exit ] && [ "$q4_acc" = gpa-mapped-host-absent ] \
   && [ "$q4_after" = halt ] && [ "$q4_child" = exited-clean ]; then reserve_word=reserved-maps-exits-until-committed
elif [ "$q4_map" = refused ]; then reserve_word=reserved-refused
else reserve_word="reserved-${q4_map:-unknown}-${q4_exit:-none}-${q4_child:-none}"; fi

# q5: decommit and release behind a live mapping.
q5_dec=$(val q5_decommit_behind_map)
q5_dec_exit=$(val q5_guest_touch_decommitted_exit)
q5_re=$(val q5_guest_touch_recommitted_exit)
q5_re_val=$(val q5_guest_touch_recommitted_value)
q5_rel=$(val q5_release_behind_map)
q5_rel_exit=$(val q5_guest_touch_released_exit)
q5_child=$(val q5_child)
if [ "$q5_dec" = succeeded ] && [ "$q5_dec_exit" = memory-access-exit ] && [ "$q5_re" = halt ] \
   && [ "$q5_re_val" = 0x0 ] && [ "$q5_rel" = succeeded ] && [ "$q5_rel_exit" = memory-access-exit ] \
   && [ "$q5_child" = exited-clean ]; then decommit_word=decommit-and-release-exit-cleanly
else decommit_word="decommit-${q5_dec:-none}-${q5_dec_exit:-none}-release-${q5_rel:-none}-${q5_rel_exit:-none}-${q5_child:-none}"; fi

q6_4k=$(val q6_map_4k_median_ns)
q6_u4k=$(val q6_unmap_4k_median_ns)
q6_2m=$(val q6_map_2m_median_ns)
q6_1g=$(val q2_map_1gb_ns)
q6_pop=$(val q6_populate_64mb_ns)
q6_pop_res=$(val q6_after_populate_resident)
q6_pop_touch=$(val q6_first_touch_after_populate_median_ns)
q6_pin_hr=$(val q6_pin_64mb_hresult)
q6_pin_lock=$(val q6_after_pin_locked)

q7_exits=$(val q7_unmapped_exits)
q7_res=$(val q7_resumed_after_map)
q7_wrong=$(val q7_wrong_values)
q7_ns=$(val q7_fault_map_resume_median_ns)
if [ "${q7_exits:-0}" = 1024 ] && [ "${q7_res:-0}" = 1024 ] && [ "${q7_wrong:-1}" = 0 ]; then lazy_word=map-on-exit-works
else lazy_word="map-on-exit-${q7_exits:-0}-${q7_res:-0}-${q7_wrong:-x}"; fi

if [ "$present" != 1 ]; then finding=hypervisor-absent
elif [ "$ready" != 1 ]; then finding=whp-partition-failed
else finding="$map_word,$touch_word,$reserve_word,$decommit_word,$lazy_word"; fi

{
	printf 'what WHvMapGpaRange does to the host memory behind it\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$(gcc --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  the API: AdviseGpaRange %s, MapGpaRange2 %s, partition counters %s, populate flags %s\n' \
		"$(yn "$(val q1_advise_gpa_range_present)")" "$(yn "$(val q1_map_gpa_range2_present)")" \
		"$(yn "$(val q1_partition_counters_present)")" "$(val q1_populate_flags)"
	printf '  q2  mapping one committed, untouched gigabyte: %s\n' "$map_word"
	printf '      working set moved %s KB; %s of 256 sampled pages resident, %s locked; hypervisor reports %s 4K pages mapped\n' \
		"${q2_delta:-?}" "${q2_res:-?}" "${q2_lock:-?}" "${q2_hv4k:-?}"
	printf '      the map call itself: %s ns for the gigabyte\n' "${q6_1g:-?}"
	printf '  q3  the guest touching those pages: %s\n' "$touch_word"
	printf '      first touch median %s ns, second touch %s ns; %s of 256 resident afterwards, %s locked\n' \
		"${q3_first:-?}" "${q3_again:-?}" "${q3_res:-?}" "${q3_lock:-?}"
	printf '  q4  reserve-only host memory behind a mapping: %s\n' "$reserve_word"
	printf '      the exit says %s; commit on the exit and resume: %s of 256, median %s ns round trip\n' \
		"${q4_acc:-?}" "${q4_n:-?}" "${q4_ns:-?}"
	printf '  q5  decommit, then release, behind a live mapping: %s\n' "$decommit_word"
	printf '  q6  a map call: 4 KB %s ns, unmap %s ns; 2 MB %s ns\n' "${q6_4k:-?}" "${q6_u4k:-?}" "${q6_2m:-?}"
	printf '      populate advice over 64 MB: %s ns, %s of 64 resident after, guest first touch then %s ns\n' \
		"${q6_pop:-?}" "${q6_pop_res:-?}" "${q6_pop_touch:-?}"
	printf '      pin advice: hresult %s, %s of 64 reported locked\n' "${q6_pin_hr:-?}" "${q6_pin_lock:-?}"
	printf '  q7  mapping a page on its memory-access exit and resuming: %s\n' "$lazy_word"
	printf '      %s exits, %s resumed, %s wrong values, median %s ns for fault, map and resume\n\n' \
		"${q7_exits:-?}" "${q7_res:-?}" "${q7_wrong:-?}" "${q7_ns:-?}"

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
