#!/usr/bin/env bash
#
# Does NT's exception dispatch write into the 128 bytes below the faulting
# thread's %rsp? A System V leaf keeps live temporaries there; substrate N takes
# every synchronous fault (a lazy-commit first touch, a SIGSEGV, an int3) as an
# NT exception on the faulting thread's own stack. DR-0050 retired
# -mno-red-zone on the asynchronous path's evidence (spike 38); this is the
# synchronous path's.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -n N, --iterations=N    Faults per case. [default: 1000]
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
iterations=${MEASURE_ITERATIONS:-1000}
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
if [ "$keep" = 1 ]; then bin=$here/redzone-probe.exe; else bin=$work/redzone-probe.exe; fi

note 'building the probe'
"$cc" -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/redzone-probe.c" "$here/redzone.S" \
	> "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

note "running the probe, $iterations faults per case"
"$bin" --iterations "$iterations" > "$work/probe.raw" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }
tr -d '\r' < "$work/probe.raw" > "$work/probe.out"

val() { sed -n "s/^$1=//p" "$work/probe.out"; }

installed=$(val handler_installed)
q1_loss=$(val q1_lazy_commit_fault_runs_with_loss)
q1_near=$(val q1_lazy_commit_fault_nearest_clobbered_offset)
q1_rec=$(val q1_lazy_commit_fault_record_below_rsp)
q1_ctx=$(val q1_lazy_commit_fault_context_below_rsp)
q1_hf=$(val q1_lazy_commit_fault_handler_frame_below_rsp)
q2_loss=$(val q2_breakpoint_runs_with_loss)
q2_near=$(val q2_breakpoint_nearest_clobbered_offset)
q3_loss=$(val q3_control_no_fault_runs_with_loss)
q4_loss=$(val q4_control_handler_clobbers_runs_with_loss)
q4_near=$(val q4_control_handler_clobbers_nearest_clobbered_offset)
faults=$(val faults_handled)
breaks=$(val breakpoints_handled)
unexpected=$(val unexpected_exceptions)

if [ "$installed" != 1 ]; then finding=handler-not-installed
elif [ "$faults" != "$iterations" ]; then finding="faults-not-taken-$faults-of-$iterations"
elif [ "$q4_loss" != "$iterations" ] || [ "$q4_near" != 8 ]; then finding=control-blind
elif [ "$q3_loss" != 0 ]; then finding=leaf-corrupts-itself
elif [ "${q1_loss:-1}" = 0 ] && [ "${q2_loss:-1}" = 0 ]; then finding=redzone-intact-through-fault-dispatch
else finding="redzone-clobbered-fault-$q1_loss-nearest-$q1_near-int3-$q2_loss-nearest-$q2_near"; fi

{
	printf 'the red zone across a synchronous fault dispatched by NT\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cc" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version | tr -d '\r')"

	printf 'reading, question by question\n\n'
	printf '  q1  a leaf with a painted red zone stores to an uncommitted page; the handler commits it and the store re-executes:\n'
	printf '      %s of %s runs lost a byte; nearest clobbered offset %s\n' "${q1_loss:-?}" "$iterations" "${q1_near:-?}"
	printf '      where NT put the dispatch: EXCEPTION_RECORD %s bytes below the interrupted rsp, CONTEXT %s below, the handler'"'"'s own frame %s below\n' \
		"${q1_rec:-?}" "${q1_ctx:-?}" "${q1_hf:-?}"
	printf '  q2  the same leaf hits int3; the handler steps over it: %s of %s runs lost a byte; nearest %s\n' \
		"${q2_loss:-?}" "$iterations" "${q2_near:-?}"
	printf '  q3  the same leaf, no fault at all: %s runs lost a byte (the leaf does not corrupt itself)\n' "${q3_loss:-?}"
	printf '  q4  the handler deliberately flips one byte eight below rsp: %s of %s runs lost a byte; nearest %s (the watcher can see)\n' \
		"${q4_loss:-?}" "$iterations" "${q4_near:-?}"
	printf '      faults handled %s, breakpoints %s, unexpected exceptions %s\n\n' "${faults:-?}" "${breaks:-?}" "${unexpected:-?}"

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
