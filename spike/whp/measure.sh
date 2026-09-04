#!/usr/bin/env bash
#
# Is the Windows Hypervisor Platform a substrate this host can carry, and what
# does one guest-to-host exit cost?
#
# Proposal 0011 offers two substrates and defers the choice to its open
# question 1, which turns on a number nobody here has measured: the cost of a
# syscall under substrate H. This builds the probe, runs it, and writes a dated
# transcript. The finding is a word about whether the substrate works at all;
# the latency rides along in the reading, where a number belongs.
#
# The hypervisor answer needs no privilege — WHvGetCapability is the
# measurement, not DISM — so this runs unelevated and stays that way.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --iterations=N    Exits to time per mechanism. [default: 20000]
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
iterations=${MEASURE_ITERATIONS:-20000}
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
		-n|--iterations) iterations=${2:-}; shift 2 ;;
		--iterations=*)  iterations=${1#*=}; shift ;;
		-k|--keep)       keep=1; shift ;;
		-q|--quiet)      quiet=1; shift ;;
		-v|--verbose)    verbose=1; shift ;;
		--)              shift; break ;;
		-?*)             printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)               break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
case $iterations in
	''|*[!0-9]*) printf '%s: --iterations wants a count, got %s\n' "$prog" "$iterations" >&2; exit 2 ;;
esac

command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'
[ -r /usr/include/w32api/winhvplatform.h ] || die 'no winhvplatform.h under /usr/include/w32api'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/whp-probe.exe; else bin=$work/whp-probe.exe; fi

note 'building the probe'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/whp-probe.c" -lwinhvplatform \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args="--iterations $iterations"
[ "$verbose" = 1 ] && probe_args="$probe_args --verbose"

note "running the probe over $iterations exits per mechanism"
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }

val() { sed -n "s/^$1=//p" "$work/probe.out"; }
yn() { [ "$1" = 1 ] && printf 'yes' || printf 'no'; }

q1=$(val q1_hypervisor_present)
q2=$(val q2_partition_created)
q3=$(val q3_memory_mapped)
q4=$(val q4_vcpu_created)
q5=$(val q5_long_mode_ring3)
q6=$(val q6_syscall_exit)
q7=$(val q7_page_fault_exit)
q8_mech=$(val q8_exit_mechanism)
q8_med=$(val q8_exit_median_ns)
q8_p99=$(val q8_exit_p99_ns)
sys_med=$(val q8_syscall_median_ns)
sys_p99=$(val q8_syscall_p99_ns)
timed=$(val q8_exit_exits_timed)
native=$(val q8_native_syscall_ns)
vendor=$(val q1_processor_vendor)

# The finding names the furthest the substrate got, and nothing about speed. A
# hypervisor that is not there and an optional feature that is switched off are
# different facts with different remedies, so they get different words; both are
# outside what an unelevated spike can change.
if [ "$q1" != 1 ]; then
	finding=hypervisor-absent
elif [ "$q2" != 1 ]; then
	finding=whp-feature-disabled
elif [ "$q3" != 1 ] || [ "$q4" != 1 ]; then
	finding=whp-partition-only
elif [ "$q5" != 1 ]; then
	finding=whp-no-ring3
elif [ "$q6" = no ]; then
	finding=whp-no-syscall-exit
elif [ "$q7" != 1 ]; then
	finding=whp-no-fault-exit
else
	finding=whp-usable
fi

{
	printf 'windows hypervisor platform as a substrate\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$(gcc --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  hypervisor present: %s, vendor %s\n' "$(yn "$q1")" "${vendor:-unknown}"
	printf '  q2  partition created, sized to one processor, set up: %s\n' "$(yn "$q2")"
	printf '  q3  host allocation mapped as guest physical memory: %s\n' "$(yn "$q3")"
	printf '  q4  virtual processor created, registers set: %s\n' "$(yn "$q4")"
	printf '  q5  long mode at CPL 3, guest code executing: %s\n' "$(yn "$q5")"
	printf '  q6  a ring-3 syscall reaching the host: %s\n' "$q6"
	printf '  q7  an unmapped guest page as a memory-access exit at the right GPA: %s\n' "$(yn "$q7")"
	printf '  q8  exit latency over %s exits, mechanism %s: median %s ns, p99 %s ns\n' \
		"${timed:-0}" "${q8_mech:-none}" "${q8_med:-n/a}" "${q8_p99:-n/a}"
	printf '      a full ring-3 syscall round trip: median %s ns, p99 %s ns\n' \
		"${sys_med:-n/a}" "${sys_p99:-n/a}"
	printf '      a native NT syscall on the same host: median %s ns\n\n' "${native:-n/a}"

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
