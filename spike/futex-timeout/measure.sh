#!/usr/bin/env bash
#
# How late does a timed RtlWaitOnAddress wake, before and after the timer
# resolution is raised, and does it ever wake early? FUTEX_WAIT with a
# timeout is this call under N and the same NT wait under H; 0012 § 9 says
# the kernel raises the resolution at start. Builds the native probe, runs it
# on an idle host, writes a dated transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --iterations=N    Waits per case. [default: 200]
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
iterations=${MEASURE_ITERATIONS:-200}
keep=${MEASURE_KEEP:-0}
quiet=${MEASURE_QUIET:-0}
usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
while [ $# -gt 0 ]; do
	case $1 in
		-h|--help) usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output) output=${2:-}; shift 2 ;;
		--output=*) output=${1#*=}; shift ;;
		-n|--iterations) iterations=${2:-}; shift 2 ;;
		--iterations=*) iterations=${1#*=}; shift ;;
		-k|--keep) keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
case $iterations in ''|*[!0-9]*) printf '%s: --iterations wants a count\n' "$prog" >&2; exit 2 ;; esac
command -v "$cc" >/dev/null 2>&1 || die "no $cc on PATH"
work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/futex-probe.exe; else bin=$work/futex-probe.exe; fi
note 'building the probe'
"$cc" -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/futex-probe.c" > "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }
note "running the probe, $iterations waits per case"
"$bin" --iterations "$iterations" > "$work/probe.raw" 2>"$work/probe.err" || { cat "$work/probe.err" >&2; die 'the probe did not run'; }
tr -d '\r' < "$work/probe.raw" > "$work/probe.out"
val() { sed -n "s/^$1=//p" "$work/probe.out"; }

cur=$(val q1_resolution_current_100ns); minr=$(val q1_resolution_min_100ns)
d100=$(val q2_default_100us_late_median_ns); d1=$(val q2_default_1ms_late_median_ns); d10=$(val q2_default_10ms_late_median_ns)
granted=$(val q3_resolution_granted_100ns)
r100=$(val q3_raised_100us_late_median_ns); r100p=$(val q3_raised_100us_late_p99_ns)
r1=$(val q3_raised_1ms_late_median_ns); r1p=$(val q3_raised_1ms_late_p99_ns); r1e=$(val q3_raised_1ms_early)
r10=$(val q3_raised_10ms_late_median_ns); r10e=$(val q3_raised_10ms_early)
e100=$(val q3_raised_100us_early)
dl100=$(val q4_delay_raised_100us_late_median_ns); dl1=$(val q4_delay_raised_1ms_late_median_ns)
after=$(val q5_resolution_after_release_100ns)
early=$(( ${e100:-0} + ${r1e:-0} + ${r10e:-0} ))

# Words: where the default tick puts a short wait (a whole tick late, or
# not); whether raising the resolution brings it under a millisecond; and
# whether any wait returned before its deadline, which Linux never does.
if [ "${d100:-0}" -gt 5000000 ]; then w1=default-tick-lands-100us-wait-a-tick-late; else w1=default-tick-fine; fi
if [ "${r100:-99999999}" -lt 1000000 ] && [ "${r1:-99999999}" -lt 1000000 ]; then w2=raised-tick-sub-millisecond; else w2=raised-tick-still-late; fi
if [ "$early" -gt 0 ]; then w3=early-wakes-observed; else w3=no-early-wakes; fi
finding="$w1,$w2,$w3"

{
	printf 'a timed futex wait against the NT clock\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cc" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version | tr -d '\r')"
	printf 'reading, question by question\n\n'
	printf '  q1  the clock as found: current %s x 100 ns, finest offered %s x 100 ns\n' "${cur:-?}" "${minr:-?}"
	printf '  q2  RtlWaitOnAddress lateness at that resolution, medians: 100 us wait %s ns late, 1 ms %s, 10 ms %s\n' "${d100:-?}" "${d1:-?}" "${d10:-?}"
	printf '  q3  after NtSetTimerResolution (granted %s x 100 ns): 100 us wait %s ns late (p99 %s), 1 ms %s (p99 %s), 10 ms %s\n' \
		"${granted:-?}" "${r100:-?}" "${r100p:-?}" "${r1:-?}" "${r1p:-?}" "${r10:-?}"
	printf '      waits that returned before their deadline: %s (100 us: %s, 1 ms: %s, 10 ms: %s)\n' "$early" "${e100:-?}" "${r1e:-?}" "${r10e:-?}"
	printf '  q4  NtDelayExecution at the raised resolution, medians: 100 us %s ns late, 1 ms %s\n' "${dl100:-?}" "${dl1:-?}"
	printf '  q5  resolution after the request is released: %s x 100 ns\n\n' "${after:-?}"
	printf 'raw\n\n'; sed -e 's/^/    /' "$work/probe.out"
	printf '\nverdict\n\n    finding=%s\n' "$finding"
} > "$work/report"
if [ "$output" = - ]; then cat "$work/report"; else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
