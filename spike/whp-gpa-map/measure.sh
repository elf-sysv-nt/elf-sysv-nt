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
#   -i FILE, --input=FILE   Render from a probe output brought back from another host: no build, no run.
#   -k, --keep              Keep the built binary beside the sources.
#   -b N, --big-gb=N        Largest reserve-only mapping q8 tries, in GB. [default: 64]
#   -c N, --commit-gb=N     Committed mapping q9 samples, in GB. [default: 8]
#   -p N, --pressure-mb=N   Host memory q9 touches for pressure; 0 skips. [default: 2048]
#   -q, --quiet             Errors only.
#   -v, --verbose           Pass --verbose to the probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u

prog=measure
release='measure 1.2'
here=$(cd "$(dirname "$0")" && pwd)

output=${MEASURE_OUTPUT:--}
input=${MEASURE_INPUT:-}
keep=${MEASURE_KEEP:-0}
big_gb=${MEASURE_BIG_GB:-64}
commit_gb=${MEASURE_COMMIT_GB:-8}
pressure_mb=${MEASURE_PRESSURE_MB:-2048}
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
		-i|--input)      input=${2:-}; shift 2 ;;
		--input=*)       input=${1#*=}; shift ;;
		-k|--keep)       keep=1; shift ;;
		-b|--big-gb)     big_gb=${2:-}; shift 2 ;;
		--big-gb=*)      big_gb=${1#*=}; shift ;;
		-c|--commit-gb)  commit_gb=${2:-}; shift 2 ;;
		--commit-gb=*)   commit_gb=${1#*=}; shift ;;
		-p|--pressure-mb) pressure_mb=${2:-}; shift 2 ;;
		--pressure-mb=*) pressure_mb=${1#*=}; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		-v|--verbose)    verbose=1; shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

[ -n "$input" ] || command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'
[ -r /usr/include/w32api/winhvplatform.h ] || die 'no winhvplatform.h under /usr/include/w32api'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/gpa-probe.exe; else bin=$work/gpa-probe.exe; fi

if [ -z "$input" ]; then
note 'building the probe'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/gpa-probe.c" -lwinhvplatform -lpsapi \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args="--big-gb $big_gb --commit-gb $commit_gb --pressure-mb $pressure_mb"
[ "$verbose" = 1 ] && probe_args="$probe_args --verbose"

note 'running the probe'
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }
else
	tr -d '\r' < "$input" > "$work/probe.out" || die "cannot read $input"
fi

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

q8_top=$(val q8_${big_gb}gb_map_hresult)
q8_top_ns=$(val q8_${big_gb}gb_map_ns)
q8_top_per_gb=$(val q8_${big_gb}gb_map_ns_per_gb)
q8_top_ws=$(val q8_${big_gb}gb_working_set_delta_kb)
q8_top_4k=$(val q8_${big_gb}gb_hv_mapped_4k_pages)
q8_top_touch=$(val q8_${big_gb}gb_top_touch_reserved | cut -d, -f1)
q8_top_commit=$(val q8_${big_gb}gb_top_touch_committed)
q8_top_unmap_ns=$(val q8_${big_gb}gb_unmap_ns)
q8_retries=$(grep -E '^q8_[0-9]+gb_map_retries=' "$work/probe.out" | cut -d= -f2 | paste -sd+ | bc 2>/dev/null || echo 0)
q8_child=$(val q8_child)
if [ "$q8_child" = exited-clean ] && [ "$q8_top" = 0x00000000 ] && [ "${q8_top_ws:-99999}" -le 4096 ] \
   && [ "$q8_top_touch" = gpa-mapped-host-absent ] && [ "$q8_top_commit" = "halt,value-correct:1" ]; then scale_word="map-lazy-at-${big_gb}gb"
else scale_word="map-at-${big_gb}gb-${q8_top:-none}-ws-${q8_top_ws:-x}-top-${q8_top_touch:-x}"; fi

q9_hr=$(val q9_map_hresult)
q9_retries=$(val q9_map_retries)
q9_ns=$(val q9_map_ns)
q9_after_map=$(val q9_after_map_resident)
q9_touch_ok=$(val q9_guest_first_touch_halted)
q9_touch_ns=$(val q9_guest_first_touch_median_ns)
q9_after_touch=$(val q9_after_touch_resident)
q9_trim=$(val q9_empty_working_set)
q9_after_trim=$(val q9_after_trim_resident)
q9_trim_ok=$(val q9_after_trim_guest_reads_correct)
q9_trim_bad=$(val q9_after_trim_guest_reads_other)
q9_trim_ns=$(val q9_after_trim_guest_reread_median_ns)
q9_after_reread=$(val q9_after_reread_resident)
q9_pmb=$(val q9_pressure_mb)
q9_pres=$(val q9_under_pressure_resident)
q9_pres_ok=$(val q9_under_pressure_guest_reads_correct)
q9_pres_ns=$(val q9_under_pressure_guest_reread_median_ns)
q9_child=$(val q9_child)
if [ "$q9_child" = exited-clean ] && [ "$q9_hr" = 0x00000000 ] && [ "${q9_after_map:-1}" = 0 ] \
   && [ "${q9_touch_ok:-0}" = 1024 ] && [ "${q9_after_touch:-0}" = 1024 ] && [ "$q9_trim" = 1 ] \
   && [ "${q9_after_trim:-1}" = 0 ] && [ "${q9_trim_ok:-0}" = 1024 ]; then trim_word=trimmed-pages-return-transparently
else trim_word="trim-${q9_hr:-none}-touched-${q9_after_touch:-x}-trimmed-${q9_after_trim:-x}-reread-${q9_trim_ok:-x}"; fi
if [ "${q9_pmb:-0}" -gt 0 ] && [ "${q9_pres_ok:-0}" != 1024 ]; then trim_word="$trim_word,under-pressure-${q9_pres_ok:-x}-of-1024"; fi
if [ "${q8_retries:-0}" != 0 ] || [ "${q9_retries:-0}" != 0 ]; then trim_word="$trim_word,map-needed-retry"; fi

if [ "$present" != 1 ]; then finding=hypervisor-absent
elif [ "$ready" != 1 ]; then finding=whp-partition-failed
else finding="$map_word,$touch_word,$reserve_word,$decommit_word,$lazy_word,$scale_word,$trim_word"; fi

# The header facts: from this host, or from the lines run.cmd wrote at the
# top of a probe output collected on another.
if [ -n "$input" ]; then
	hdr() { sed -n "s/^# $1: //p" "$work/probe.out" | head -1; }
	h_host=$(hdr host); h_windows=$(hdr windows); h_cygwin="none, collected by $(hdr runner)"
	h_compiler=$(hdr compiler); h_probe=$(hdr probe)
else
	h_host="$(hostname 2>/dev/null)"; h_windows="$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"; h_cygwin="$(uname -r)"
	h_compiler="$(gcc --version | head -1)"; h_probe="$("$bin" --version)"
fi
{
	printf 'what WHvMapGpaRange does to the host memory behind it\n\n'
	printf 'host        %s\n' "$h_host"
	printf 'windows     %s\n' "$h_windows"
	printf 'cygwin      %s\n' "$h_cygwin"
	printf 'compiler    %s\n' "$h_compiler"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$h_probe"

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
	printf '  q8  reserve-only mappings up to %s GB, in a child: %s\n' "$big_gb" "$scale_word"
	for gb in 8 16 32 64; do
		[ "$gb" -le "$big_gb" ] || continue
		printf '      %2s GB: map %s ns (%s ns/GB, %s retries), working set %s KB, %s 4 KB pages counted; top page %s, committed: %s; unmap %s ns\n' \
			"$gb" "$(val q8_${gb}gb_map_ns)" "$(val q8_${gb}gb_map_ns_per_gb)" "$(val q8_${gb}gb_map_retries)" \
			"$(val q8_${gb}gb_working_set_delta_kb)" "$(val q8_${gb}gb_hv_mapped_4k_pages)" \
			"$(val q8_${gb}gb_top_touch_reserved | cut -d, -f1)" "$(val q8_${gb}gb_top_touch_committed)" "$(val q8_${gb}gb_unmap_ns)"
	done
	printf '  q9  %s GB committed and mapped (hresult %s, %s retries, %s ns), 1024 pages sampled: %s resident after the map;\n' \
		"$commit_gb" "${q9_hr:-?}" "${q9_retries:-?}" "${q9_ns:-?}" "${q9_after_map:-?}"
	printf '      the guest writes each (%s halted, median %s ns), %s resident; the working set emptied (%s): %s resident,\n' \
		"${q9_touch_ok:-?}" "${q9_touch_ns:-?}" "${q9_after_touch:-?}" "$(yn "$q9_trim")" "${q9_after_trim:-?}"
	printf '      the guest reads them back: %s correct, %s other, median %s ns, %s resident again: %s\n' \
		"${q9_trim_ok:-?}" "${q9_trim_bad:-?}" "${q9_trim_ns:-?}" "${q9_after_reread:-?}" "$trim_word"
	if [ "${q9_pmb:-0}" -gt 0 ]; then
		printf '      under %s MB of host pressure: %s resident, guest reads %s correct, median %s ns\n' \
			"$q9_pmb" "${q9_pres:-?}" "${q9_pres_ok:-?}" "${q9_pres_ns:-?}"
	fi
	printf '\n'

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
