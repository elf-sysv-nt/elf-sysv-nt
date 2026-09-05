#!/usr/bin/env bash
#
# Can a running WHP virtual processor be forced out of its run from a second
# host thread, and is the interruption resumable rather than a teardown?
# Proposal 0011 section 3 names the substrate call thread_interrupt(tid), and
# section 7 says that under substrate H it is WHvCancelRunVirtualProcessor;
# spike (f) built the partition and priced an exit but never called it. This is
# the H-side twin of spike (d)'s NT hijack, and it is that gap.
#
# This builds the native probe (whp-interrupt.c), runs it, and writes the
# transcript: a header of where the run happened, the reading question by
# question, the raw key=value block, and the verdict with its finding word. The
# probe is mingw, not Cygwin -- the thing measured is a Win32 hypervisor call --
# so the build uses x86_64-w64-mingw32-gcc. If the source is missing the script
# says so and stops rather than pretending to a verdict. See README.md.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -l N, --latency=N       Timed cancels of a running vCPU. [default: 2000]
#   -r N, --races=N         Cancels raced against the guest loop. [default: 10000]
#   -w DIR, --work=DIR      Keep the build here; implies --keep.
#   -k, --keep              Do not delete the working directory.
#   -q, --quiet             Errors only. Only useful with --output.
#   -v, --verbose           Pass --verbose to the probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as MEASURE_<OPTION>, and the option wins over the
# variable.

set -u

prog=measure
release='measure 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${MEASURE_OUTPUT:--}
latency=${MEASURE_LATENCY:-2000}
races=${MEASURE_RACES:-10000}
work=${MEASURE_WORK:-}
keep=${MEASURE_KEEP:-0}
quiet=${MEASURE_QUIET:-0}
verbose=${MEASURE_VERBOSE:-0}

cc=${MEASURE_CC:-x86_64-w64-mingw32-gcc}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
step() { [ "$verbose" -gt 0 ] && printf '%s: %s\n' "$prog" "$*" >&2; return 0; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-l|--latency) latency=${2:-}; shift 2 ;;
		--latency=*)  latency=${1#*=}; shift ;;
		-r|--races)   races=${2:-}; shift 2 ;;
		--races=*)    races=${1#*=}; shift ;;
		-w|--work)    work=${2:-}; shift 2 ;;
		--work=*)     work=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-v|--verbose) verbose=$((verbose + 1)); shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done

[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

for pair in "latency $latency" "races $races"; do
	name=${pair%% *} value=${pair#* }
	case $value in
		'' | *[!0-9]*)
			printf '%s: --%s wants a number, got %s\n' "$prog" "$name" "$value" >&2
			exit 2 ;;
	esac
	[ "$value" -gt 0 ] || {
		printf '%s: --%s wants a positive number\n' "$prog" "$name" >&2
		exit 2
	}
done

command -v "$cc" >/dev/null 2>&1 || die "no $cc on PATH"
[ -r /usr/include/w32api/winhvplatform.h ] || die 'no winhvplatform.h under /usr/include/w32api'

src=$here/whp-interrupt.c
[ -f "$src" ] || { printf '%s: probe source missing: whp-interrupt.c\n' "$prog" >&2; exit 3; }

if [ -n "$work" ]; then
	mkdir -p "$work" || die "cannot create $work"
	work=$(cd "$work" && pwd)
	keep=1
else
	work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
fi
cleanup() { [ "$keep" = 1 ] || rm -rf "$work"; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM

probe=$work/whp-interrupt.exe

step 'building the probe'
"$cc" -O1 -g -Wall -Wextra -o "$probe" "$src" -lwinhvplatform \
	> "$work/build.log" 2>&1 || {
	cat "$work/build.log" >&2
	die 'the probe did not build'
}
[ -s "$work/build.log" ] && cat "$work/build.log" >&2

step "running the probe over $latency timed cancels and $races races"
set -- --latency "$latency" --races "$races"
[ "$verbose" -gt 0 ] && set -- "$@" --verbose

"$probe" "$@" > "$work/body.raw" 2> "$work/probe.err"
st=$?
[ -s "$work/probe.err" ] && cat "$work/probe.err" >&2
[ "$st" -eq 2 ] && die 'the probe refused its arguments'
[ -s "$work/body.raw" ] || die 'the probe wrote nothing'

# the probe is a native mingw exe and writes CRLF; the transcript is LF, as
# every other spike's is, so strip the carriage returns before anything reads it
tr -d '\r' < "$work/body.raw" > "$work/body"

finding=$(sed -n 's/^ *finding=//p' "$work/body")
verdict=$(sed -n 's/^ *verdict=//p' "$work/body")
[ -n "$finding" ] || die 'the probe reported no finding'

win=$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')
[ -n "$win" ] || win=$(uname -s | sed 's/.*-//')

{
	printf 'interrupting a running virtual processor from another thread\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$win"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cc" --version | head -1 | tr -d '\r')"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n' "$("$probe" --version | tr -d '\r')"
	printf '\n'
	cat "$work/body"
} > "$work/report"

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi

[ "$verdict" = yes ]
