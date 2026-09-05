#!/usr/bin/env bash
#
# What a WHP partition costs to bring up, how many one process may hold, how
# many vCPUs one partition may carry, whether a vCPU can be run from one thread
# and then another, and whether exits from several vCPUs at once cost what one
# does.
#
# Substrate H's two shapes, a host process per Linux process or one kernel
# process for all, each lean on one of these:
# shape A builds a partition per fork and holds one per Linux process; shape B
# pools vCPUs in one partition and hands them between threads. This builds the
# probe, runs it, and writes a dated transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -p N, --partitions=N    Cap on partitions to try holding at once. [default: 512]
#   -c N, --vcpus=N         Cap on vCPUs to create in one partition. [default: 256]
#   -n N, --exits=N         Exits to time per vCPU in q6. [default: 5000]
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
partitions=${MEASURE_PARTITIONS:-512}
vcpus=${MEASURE_VCPUS:-256}
exits=${MEASURE_EXITS:-5000}
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
		-p|--partitions) partitions=${2:-}; shift 2 ;;
		--partitions=*)  partitions=${1#*=}; shift ;;
		-c|--vcpus)      vcpus=${2:-}; shift 2 ;;
		--vcpus=*)       vcpus=${1#*=}; shift ;;
		-n|--exits)      exits=${2:-}; shift 2 ;;
		--exits=*)       exits=${1#*=}; shift ;;
		-k|--keep)       keep=1; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		-v|--verbose)    verbose=1; shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
for v in partitions vcpus exits; do
	case ${!v} in ''|*[!0-9]*) printf '%s: --%s wants a count, got %s\n' "$prog" "$v" "${!v}" >&2; exit 2 ;; esac
done

command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'
[ -r /usr/include/w32api/winhvplatform.h ] || die 'no winhvplatform.h under /usr/include/w32api'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/partition-probe.exe; else bin=$work/partition-probe.exe; fi

note 'building the probe'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/partition-probe.c" -lwinhvplatform -lpsapi \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args="--partitions $partitions --vcpus $vcpus --exits $exits"
[ "$verbose" = 1 ] && probe_args="$probe_args --verbose"

note 'running the probe'
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }

val() { sed -n "s/^$1=//p" "$work/probe.out"; }

present=$(val hypervisor_present)
q1_ok=$(val q1_created)
q1_ns=$(val q1_create_setup_median_ns)
q1_del=$(val q1_delete_median_ns)
q2_ok=$(val q2_first_run_ok)
q2_ns=$(val q2_create_to_first_exit_median_ns)
q3_held=$(val q3_partitions_held)
q3_stage=$(val q3_refusal_stage)
q3_hr=$(val q3_refusal_hresult)
q3_second=$(val q3_second_process)
q3b_both=$(val q3b_two_partitions_both_map)
q3b_after=$(val q3b_other_maps_after_unmap | cut -d: -f1)
q3c_child=$(val q3c_child_maps_own_view)
q4_max=$(val q4_processor_count_max_accepted)
q4_att=$(val q4_vcpus_attempted)
q4_made=$(val q4_vcpus_created)
q4_ran=$(val q4_vcpus_ran_to_halt)
q4_hr=$(val q4_vcpu_create_refusal_hresult)
q4_ns=$(val q4_vcpu_create_median_ns)
q5=$(val q5_handoff)
q6=$(val q6_concurrent)
q6_n=$(val q6_vcpus)
q6_single=$(val q6_single_vcpu_median_ns)
q6_lo=$(val q6_concurrent_median_ns_min)
q6_hi=$(val q6_concurrent_median_ns_max)
q6_agg=$(val q6_aggregate_exits_per_second)
q6_one=$(val q6_single_exits_per_second)

# q3's word. The ceiling this host imposes is on mapped partitions per
# process: several may be set up, one at a time may hold guest memory, and a
# second process is unaffected.
if [ "${q3_held:-0}" = 1 ] && [ "$q3_stage" = map ] && [ "$q3_second" = held ] \
   && [ "$q3b_both" = refused ] && [ "$q3b_after" = accepted ]; then partitions_word=one-mapped-partition-per-process
elif [ "${q3_held:-0}" -gt 1 ]; then partitions_word=several-mapped-partitions-per-process
else partitions_word="partitions-${q3_held:-0}-${q3_stage:-none}-${q3_second:-none}-${q3b_both:-none}"; fi

case $q3c_child in
	mapped,sees-parent-write:1) shared_word=section-shared-across-processes ;;
	*) shared_word="section-across-processes-${q3c_child:-none}" ;;
esac

if [ -n "$q4_made" ] && [ "${q4_made:-0}" -gt 0 ] && [ "$q4_made" = "$q4_ran" ]; then vcpu_word=created-vcpus-all-run
else vcpu_word="vcpus-${q4_made:-0}-ran-${q4_ran:-0}"; fi

if [ "$q5" = alternates ]; then handoff_word=vcpu-hands-off-between-threads; else handoff_word="handoff-${q5:-none}"; fi
if [ "$q6" = ran ]; then concurrent_word=concurrent-exits-run; else concurrent_word="concurrent-${q6:-none}"; fi

if [ "$present" != 1 ]; then finding=hypervisor-absent
elif [ "${q2_ok:-0}" = 0 ]; then finding=whp-partition-failed
else finding="$partitions_word,$shared_word,$vcpu_word,$handoff_word,$concurrent_word"; fi

{
	printf 'what a partition and a vCPU cost, and how many one process may hold\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$(gcc --version | head -1)"
	printf 'processors  %s\n' "$(val host_processors)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  create, size to one processor, set up: %s of 40; median %s ns, delete %s ns\n' \
		"${q1_ok:-?}" "${q1_ns:-?}" "${q1_del:-?}"
	printf '  q2  the whole way to the first exit (create, set up, map, vCPU, registers, run): %s of 40; median %s ns\n' \
		"${q2_ok:-?}" "${q2_ns:-?}"
	printf '  q3  partitions one process may hold with memory mapped: %s\n' "$partitions_word"
	printf '      held %s before a refusal at stage %s (%s); a second process holding its own: %s\n' \
		"${q3_held:-?}" "${q3_stage:-?}" "${q3_hr:-?}" "${q3_second:-?}"
	printf '      two set-up partitions both mapping: %s; the other may map once the first unmaps: %s\n' \
		"${q3b_both:-?}" "${q3b_after:-?}"
	printf '  q3c one section, two processes, two partitions: %s (%s)\n' "$shared_word" "${q3c_child:-?}"
	printf '  q4  ProcessorCount accepted up to %s; %s of %s vCPUs created (refusal %s), %s ran to a halt; create median %s ns\n' \
		"${q4_max:-?}" "${q4_made:-?}" "${q4_att:-?}" "${q4_hr:-?}" "${q4_ran:-?}" "${q4_ns:-?}"
	printf '  q5  one vCPU run alternately from two threads: %s\n' "$handoff_word"
	printf '  q6  %s vCPUs exiting at once: %s; single-vCPU exit %s ns, concurrent per-vCPU medians %s to %s ns\n' \
		"${q6_n:-?}" "$concurrent_word" "${q6_single:-?}" "${q6_lo:-?}" "${q6_hi:-?}"
	printf '      aggregate %s exits/s against %s for one vCPU\n\n' "${q6_agg:-?}" "${q6_one:-?}"

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
