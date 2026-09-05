#!/usr/bin/env bash
#
# fork as a page-table copy inside one WHP partition: does a guest page fault
# reach the host as an exception exit, what does copying the tables of a
# 64 MB and a 576 MB process cost, what does one copy-on-write fault cost end to
# end, is the isolation right, and what does switching a vCPU between roots
# cost?
#
# This is shape B's as_clone, measured before it is
# designed in detail. Proposal 0011 § 4 describes fork under H in exactly these
# terms and nothing had run them. Builds the probe, runs it, writes a dated
# transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --cow-faults=N    Copy-on-write faults to time. [default: 1024]
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
cow_faults=${MEASURE_COW_FAULTS:-1024}
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
		-n|--cow-faults) cow_faults=${2:-}; shift 2 ;;
		--cow-faults=*)  cow_faults=${1#*=}; shift ;;
		-k|--keep)       keep=1; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		-v|--verbose)    verbose=1; shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
case $cow_faults in
	''|*[!0-9]*) printf '%s: --cow-faults wants a count, got %s\n' "$prog" "$cow_faults" >&2; exit 2 ;;
esac

command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'
[ -r /usr/include/w32api/winhvplatform.h ] || die 'no winhvplatform.h under /usr/include/w32api'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/fork-probe.exe; else bin=$work/fork-probe.exe; fi

note 'building the probe'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/fork-probe.c" -lwinhvplatform \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args="--cow-faults $cow_faults"
[ "$verbose" = 1 ] && probe_args="$probe_args --verbose"

note 'running the probe'
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }

val() { sed -n "s/^$1=//p" "$work/probe.out"; }
yn() { [ "$1" = 1 ] && printf 'yes' || printf 'no'; }
all1() { for x in "$@"; do [ "$x" = 1 ] || return 1; done; return 0; }

present=$(val hypervisor_present)
pf=$(val q1_page_fault_exit_available)
ready=$(val partition_ready)

q2=$(val q2_guest_runs)
q2_nofault=$(val q2_guest_write_no_fault)
q2_read=$(val q2_guest_read_back)

q3_small_t=$(val q3_fork_64mb_tables_copied)
q3_small=$(val q3_fork_64mb_median_ns)
q3_big_t=$(val q3_fork_576mb_tables_copied)
q3_big=$(val q3_fork_576mb_median_ns)
q3_frames=$(val q2_parent_frames_mapped)

q4_done=$(val q4_child_writes_completed)
q4_one=$(val q4_child_writes_one_fault_each)
q4_copied=$(val q4_frames_copied)
q4_ns=$(val q4_cow_fault_round_trip_median_ns)
q4_host=$(val q4_cow_host_work_median_ns)
q4_pi=$(val q4_parent_frames_intact)
q4_cp=$(val q4_child_frames_private)
q4_gp=$(val q4_guest_reads_parent_sampled)
q4_gc=$(val q4_guest_reads_child_sampled)
q4_err=$(val q4_first_fault_error_code)
q4_rip=$(val q4_first_fault_rip_at_store)
q4_param=$(val q4_first_fault_parameter)
q4_cr2=$(val q4_first_fault_cr2_register)
q4b_one=$(val q4b_no_flush_one_fault_each)
q4b_want=$(val q4b_no_flush_samples_wanted)
q4b_wrong=$(val q4b_no_flush_wrong_values)
q4b_ns=$(val q4b_no_flush_round_trip_median_ns)
q4c_ns=$(val q4c_private_write_round_trip_median_ns)
q4c_faults=$(val q4c_private_write_faults)

q5_a=$(val q5_parent_write_shared_copied)
q5_b=$(val q5_child_still_sees_original)
q5_c=$(val q5_parent_write_taken_no_copy)
q5_d=$(val q5_child_unaffected_by_taken)
q5_e=$(val q5_parent_reads_own_write)
q5_f=$(val q5_grandchild_sees_child_value)
q5_g=$(val q5_child_unaffected_by_grandchild)

q6_bad=$(val q6_runs_not_halting)
q6_same=$(val q6_same_root_median_ns)
q6_alt=$(val q6_alternating_roots_median_ns)

if [ "$present" != 1 ]; then finding=hypervisor-absent
elif [ "$pf" != 1 ]; then finding=no-page-fault-exit
elif [ "$ready" != 1 ]; then finding=whp-partition-failed
else
	if all1 "$q2" "$q2_nofault" "$q2_read"; then w2=guest-runs-on-4k-tables; else w2=guest-failed; fi
	if [ -n "$q3_big" ] && [ "${q3_big_t:-0}" -gt 0 ]; then w3=fork-copies-tables; else w3=fork-failed; fi
	if [ "$q4_done" = "$cow_faults" ] && [ "$q4_one" = "$cow_faults" ] && [ "$q4_copied" = "$cow_faults" ] \
	   && all1 "$q4_pi" "$q4_cp" && [ "$q4_gp" = 16 ] && [ "$q4_gc" = 16 ] && [ "$q4_rip" = 1 ]; then w4=cow-isolates
	else w4="cow-${q4_done:-0}-of-$cow_faults-intact-${q4_pi:-0}-private-${q4_cp:-0}"; fi
	if [ "$q4b_one" = "$q4b_want" ] && [ "${q4b_wrong:-1}" = 0 ] && [ "${q4c_faults:-1}" = 0 ]; then w4b=no-tlb-reload-needed
	else w4b="no-flush-${q4b_one:-0}-of-${q4b_want:-0}-wrong-${q4b_wrong:-x}"; fi
	if all1 "$q5_a" "$q5_b" "$q5_c" "$q5_d" "$q5_e" "$q5_f" "$q5_g"; then w5=all-cow-paths-correct
	else w5="cow-paths-${q5_a:-0}${q5_b:-0}${q5_c:-0}${q5_d:-0}${q5_e:-0}${q5_f:-0}${q5_g:-0}"; fi
	if [ "${q6_bad:-1}" = 0 ]; then w6=root-switch-works; else w6=root-switch-failed; fi
	finding="page-fault-exits-to-host,$w2,$w3,$w4,$w4b,$w5,$w6"
fi

{
	printf 'fork as a page-table copy inside one WHP partition\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$(gcc --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  a guest #PF routed to the host as an exception exit: %s (bitmap %s)\n' \
		"$(yn "$pf")" "$(val q1_exception_exit_bitmap)"
	printf '  q2  a process on 4 KB tables, %s frames mapped, guest writes and reads back without a fault: %s\n' \
		"${q3_frames:-?}" "$(yn "$q2")"
	printf '  q3  the fork, a table copy with every leaf made read-only in both trees:\n'
	printf '      64 MB mapped, %s table pages copied: median %s ns\n' "${q3_small_t:-?}" "${q3_small:-?}"
	printf '      576 MB mapped, %s table pages copied: median %s ns\n' "${q3_big_t:-?}" "${q3_big:-?}"
	printf '  q4  copy-on-write from the child, %s pages, one fault each: %s of %s completed, %s frames copied\n' \
		"$cow_faults" "${q4_done:-?}" "$cow_faults" "${q4_copied:-?}"
	printf '      the exit carries error code %s with %%rip still at the store: %s; parameter %s, CR2 register %s\n' \
		"${q4_err:-?}" "$(yn "$q4_rip")" "${q4_param:-?}" "${q4_cr2:-?}"
	printf '      round trip median %s ns, of which host work %s ns; the same path onto an owned page, no fault: %s ns\n' \
		"${q4_ns:-?}" "${q4_host:-?}" "${q4c_ns:-?}"
	printf '      without the CR3 reload after the copy: %s of %s still one fault, %s wrong, median %s ns\n' \
		"${q4b_one:-?}" "${q4b_want:-?}" "${q4b_wrong:-?}" "${q4b_ns:-?}"
	printf '      parent frames intact %s, child frames private %s; through the guest, parent %s/16 and child %s/16\n' \
		"$(yn "$q4_pi")" "$(yn "$q4_cp")" "${q4_gp:-?}" "${q4_gc:-?}"
	printf '  q5  parent writes a shared page (copies) %s, child keeps the original %s; parent writes a page the child\n' \
		"$(yn "$q5_a")" "$(yn "$q5_b")"
	printf '      already copied (takes it, no copy) %s, child unaffected %s, parent reads its own %s; a grandchild\n' \
		"$(yn "$q5_c")" "$(yn "$q5_d")" "$(yn "$q5_e")"
	printf '      sees the child'"'"'s value %s and its write leaves the child alone %s\n' "$(yn "$q5_f")" "$(yn "$q5_g")"
	printf '  q6  one vCPU, same root every run %s ns; alternating between two roots %s ns; runs not halting %s\n\n' \
		"${q6_same:-?}" "${q6_alt:-?}" "${q6_bad:-?}"

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
