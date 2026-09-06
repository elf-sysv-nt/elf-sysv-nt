#!/usr/bin/env bash
#
# Does an I/O call accept a user buffer whose pages are reserved but not
# committed, given a vectored handler that would commit them on the fault?
# Proposal 0012 § 4 (DR-0098) says no, from a reading of the I/O manager's
# probe; this measures it. Builds the native probe, runs it, writes a dated
# transcript.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
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
		-k|--keep) keep=1; shift ;;
		-q|--quiet) quiet=1; shift ;;
		--) shift; break ;;
		-?*) printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*) break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }
command -v "$cc" >/dev/null 2>&1 || die "no $cc on PATH"
work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/readfile-probe.exe; else bin=$work/readfile-probe.exe; fi
note 'building the probe'
"$cc" -std=gnu11 -O1 -Wall -Wextra -o "$bin" "$here/readfile-probe.c" > "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }
note 'running the probe'
"$bin" > "$work/probe.raw" 2>"$work/probe.err" || { cat "$work/probe.err" >&2; die 'the probe did not run'; }
tr -d '\r' < "$work/probe.raw" > "$work/probe.out"
val() { sed -n "s/^$1=//p" "$work/probe.out"; }

q1=$(val q1_user_touch_handler_hits)
q2ok=$(val q2_read_sync_reserved_ok); q2e=$(val q2_read_sync_reserved_error); q2h=$(val q2_read_sync_reserved_handler_hits)
q3ok=$(val q3_read_async_reserved_ok); q3e=$(val q3_read_async_reserved_error); q3h=$(val q3_read_async_reserved_handler_hits)
q4ok=$(val q4_read_sync_noaccess_ok); q4e=$(val q4_read_sync_noaccess_error)
q5ok=$(val q5_write_sync_reserved_ok); q5e=$(val q5_write_sync_reserved_error); q5h=$(val q5_write_sync_reserved_handler_hits)
q6a=$(val q6_read_sync_committed_ok); q6b=$(val q6_read_async_committed_ok)
q7ok=$(val q7_read_sync_half_decommitted_ok); q7e=$(val q7_read_sync_half_decommitted_error); q7h=$(val q7_read_sync_half_decommitted_handler_hits)

if [ "$q1" != 1 ]; then finding=handler-blind
elif [ "$q6a" != 1 ] || [ "$q6b" != 1 ]; then finding=control-failed
elif [ "$q2ok" = 0 ] && [ "$q2h" = 0 ] && [ "$q3ok" = 0 ] && [ "$q3h" = 0 ] && [ "$q5ok" = 0 ] && [ "$q5h" = 0 ] && [ "$q7ok" = 0 ] && [ "$q7h" = 0 ]; then
	finding=io-probe-fails-uncommitted-buffer-handler-never-runs
elif [ "$q2ok" = 1 ] || [ "$q2h" != 0 ]; then finding=io-reaches-handler-or-commits
else finding="io-mixed-r${q2ok}h${q2h}-a${q3ok}h${q3h}-w${q5ok}h${q5h}-d${q7ok}h${q7h}"; fi

{
	printf 'an I/O call against a user buffer the handler has not committed yet\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'windows     %s\n' "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*\[Version \(.*\)\]/\1/p')"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$("$cc" --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version | tr -d '\r')"
	printf 'reading, question by question\n\n'
	printf '  q1  a user-mode touch of the reserved buffer takes the handler: %s hit\n' "${q1:-?}"
	printf '  q2  ReadFile into reserved memory, synchronous handle: ok %s, error %s, handler hits %s\n' "${q2ok:-?}" "${q2e:-?}" "${q2h:-?}"
	printf '  q3  the same on an overlapped handle: ok %s, error %s, handler hits %s\n' "${q3ok:-?}" "${q3e:-?}" "${q3h:-?}"
	printf '  q4  ReadFile into committed PAGE_NOACCESS memory: ok %s, error %s\n' "${q4ok:-?}" "${q4e:-?}"
	printf '  q5  WriteFile from reserved memory: ok %s, error %s, handler hits %s\n' "${q5ok:-?}" "${q5e:-?}" "${q5h:-?}"
	printf '  q6  the control, committed memory: sync ok %s, async ok %s\n' "${q6a:-?}" "${q6b:-?}"
	printf '  q7  a committed buffer with its middle decommitted: ok %s, error %s, handler hits %s\n\n' "${q7ok:-?}" "${q7e:-?}" "${q7h:-?}"
	printf 'raw\n\n'; sed -e 's/^/    /' "$work/probe.out"
	printf '\nverdict\n\n    finding=%s\n' "$finding"
} > "$work/report"
if [ "$output" = - ]; then cat "$work/report"; else cat "$work/report" > "$output" || die "cannot write $output"; note "transcript written to $output"; fi
