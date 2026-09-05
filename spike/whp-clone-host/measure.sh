#!/usr/bin/env bash
#
# RtlCloneUserProcess of a Win32 process that owns a WHP partition: does the
# clone run, what of Win32 works in it, can it build a partition of its own
# and run a vCPU, what does the inherited partition handle do, and what does
# the clone cost with guest memory mapped?
#
# Shape A of substrate H (one host process per Linux process) forks this way. Spike 35
# cloned a plain process; nothing had cloned one holding the hypervisor. Builds
# the native probe with the mingw cross-compiler, runs it, writes a dated
# transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --iterations=N    Clones to time per case. [default: 20]
#   -k, --keep              Keep the built binary beside the sources.
#   -q, --quiet             Errors only.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>.

set -u

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)
cc=${MEASURE_CC:-x86_64-w64-mingw32-gcc}

output=${MEASURE_OUTPUT:--}
iterations=${MEASURE_ITERATIONS:-20}
keep=${MEASURE_KEEP:-0}
quiet=${MEASURE_QUIET:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)       usage; exit 0 ;;
		-V|--version)    printf '%s\n' "$release"; exit 0 ;;
		-o|--output)     output=${2:-}; shift 2 ;;
		--output=*)      output=${1#*=}; shift ;;
		-n|--iterations) iterations=${2:-}; shift 2 ;;
		--iterations=*)  iterations=${1#*=}; shift ;;
		-k|--keep)       keep=1; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
case $iterations in
	''|*[!0-9]*) printf '%s: --iterations wants a count, got %s\n' "$prog" "$iterations" >&2; exit 2 ;;
esac

command -v "$cc" >/dev/null 2>&1 || die "no $cc on PATH"

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/clone-host-probe.exe; else bin=$work/clone-host-probe.exe; fi

note 'building the probe'
"$cc" -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/clone-host-probe.c" -lwinhvplatform \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

note "running the probe, $iterations clones per timing case"
"$bin" --iterations "$iterations" > "$work/probe.raw" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }
# a native binary writes CRLF; the transcript and the comparisons want LF
tr -d '\r' < "$work/probe.raw" > "$work/probe.out"

val() { sed -n "s/^$1=//p" "$work/probe.out"; }
yn() { [ "$1" = 1 ] && printf 'yes' || printf 'no'; }
all1() { for x in "$@"; do [ "$x" = 1 ] || return 1; done; return 0; }

present=$(val hypervisor_present)
rtl=$(val rtlclone_available)
q1=$(val q1_parent_partition_first_exit_halt)
q2a=$(val q2a_inherited_handle_run)
q2a_step=$(val q2a_child_last_step)
q2_child=$(val q2_child)
q2_ran=$(val q2_child_ran)
q2_heap=$(val q2_heap_ok); q2_ev=$(val q2_event_ok); q2_wait=$(val q2_wait_ok)
q2_va=$(val q2_virtualalloc_ok); q2_tls=$(val q2_tls_ok); q2_ll=$(val q2_loadlibrary_ok)
q2_halt=$(val q2_partition_first_exit_halt)
q2_ns=$(val q2_partition_create_to_exit_ns)
q2_second=$(val q2_second_partition_map_hresult)
q2_inh=$(val q2_inherited_handle_run)
q2_inh_reason=$(val q2_inherited_handle_run_reason)
q3=$(val q3_parent_partition_runs_after_clone)
q4_ns=$(val q4_clone_median_ns_4mb_mapped)
q4_fail=$(val q4_clone_failures)
q5_map=$(val q5_map_256mb_hresult)
q5_ns=$(val q5_clone_median_ns_260mb_mapped)
q5_fail=$(val q5_clone_failures)
q5_halt=$(val q5_partition_first_exit_halt)
q5_after=$(val q5_parent_partition_runs_after)
q5b_ns=$(val q5b_clone_median_ns_256mb_touched_unmapped)

if [ "$present" != 1 ]; then finding=hypervisor-absent
elif [ "$rtl" != 1 ]; then finding=no-rtlcloneuserprocess
elif [ "$q1" != 1 ]; then finding=parent-partition-failed
else
	if [ "$q2_child" = reported ] && [ "$q2_ran" = 1 ]; then w_run=clone-runs; else w_run="clone-${q2_child:-none}"; fi
	if all1 "$q2_heap" "$q2_ev" "$q2_wait" "$q2_va" "$q2_tls"; then
		if [ "$q2_ll" = 1 ]; then w_win=win32-works; else w_win=win32-works-loadlibrary-fails; fi
	else w_win="win32-${q2_heap:-0}${q2_ev:-0}${q2_wait:-0}${q2_va:-0}${q2_tls:-0}${q2_ll:-0}"; fi
	if [ "$q2_halt" = 1 ] && [ "$q5_halt" = 1 ]; then w_part=child-builds-and-runs-partition; else w_part="child-partition-${q2_halt:-0}-${q5_halt:-0}"; fi
	if [ "$q2a" = hangs ] && [ "$q2_inh" = returned ] && [ "$q2_inh_reason" = 0x8 ]; then w_inh=inherited-handle-hangs-first-runs-after-own
	else w_inh="inherited-handle-${q2a:-none}-then-${q2_inh:-none}"; fi
	case $q2_second in 0xc0370008|0x00000000c0370008) w_one=one-mapped-partition-in-child ;; *) w_one="second-partition-${q2_second:-none}" ;; esac
	if [ "$q3" = 1 ] && [ "$q5_after" = 1 ]; then w_par=parent-partition-survives-clones; else w_par="parent-partition-${q3:-0}-${q5_after:-0}"; fi
	if [ "${q4_fail:-1}" = 0 ] && [ "${q5_fail:-1}" = 0 ]; then w_clone=clones-with-mapped-memory-succeed; else w_clone="clone-failures-${q4_fail:-x}-${q5_fail:-x}"; fi
	finding="$w_run,$w_win,$w_part,$w_inh,$w_one,$w_par,$w_clone"
fi

{
	printf 'RtlCloneUserProcess of a process holding a WHP partition\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cc" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version | tr -d '\r')"

	printf 'reading, question by question\n\n'
	printf '  q1  the parent: kernel32 %s, WinHvPlatform %s, its partition runs to a halt %s\n' \
		"$(yn "$(val q1_kernel32_loaded)")" "$(yn "$(val q1_winhvplatform_loaded)")" "$(yn "$q1")"
	printf '  q2a a clone whose first WHP call runs the inherited partition handle: %s (child stopped at step %s)\n' \
		"${q2a:-?}" "${q2a_step:-?}"
	printf '  q2  a clone that builds its own partition first: %s, ran %s\n' "${q2_child:-?}" "$(yn "$q2_ran")"
	printf '      heap %s, event %s, wait %s, VirtualAlloc %s, TLS %s, LoadLibrary %s\n' \
		"$(yn "$q2_heap")" "$(yn "$q2_ev")" "$(yn "$q2_wait")" "$(yn "$q2_va")" "$(yn "$q2_tls")" "$(yn "$q2_ll")"
	printf '      own partition to first halt %s in %s ns; a second mapped partition beside it: %s\n' \
		"$(yn "$q2_halt")" "${q2_ns:-?}" "${q2_second:-?}"
	printf '      the inherited handle, run last: %s, exit reason %s\n' "${q2_inh:-?}" "${q2_inh_reason:-?}"
	printf '  q3  the parent'"'"'s partition still runs after the clones: %s\n' "$(yn "$q3")"
	printf '  q4  clone cost, 4 MB mapped: median %s ns over %s, %s failures\n' "${q4_ns:-?}" "$iterations" "${q4_fail:-?}"
	printf '  q5  clone cost, 256 MB touched and mapped (%s): median %s ns, %s failures; child report as q2: %s\n' \
		"${q5_map:-?}" "${q5_ns:-?}" "${q5_fail:-?}" "$(yn "$q5_halt")"
	printf '      control, the same 256 MB touched and unmapped: median %s ns\n\n' "${q5b_ns:-?}"

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
