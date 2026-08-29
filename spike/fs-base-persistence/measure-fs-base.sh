#!/usr/bin/env bash
#
# Does Windows preserve a user-written FS base across a context switch?
#
# The System V psABI reaches TLS through %fs and on x86-64 Windows that
# register is unused, so it is available. Available and preserved are
# different claims and only the second one holds the TLS layer up. This
# builds fs-base-probe.c, runs it, and writes what came back.
#
# Nothing is installed and no privilege is wanted. The probe compiles into a
# scratch directory that is removed on the way out.
#
# Usage:
#   measure-fs-base.sh [options]
#
# Options:
#   -o FILE, --output=FILE      Transcript destination; - is stdout. [default: -]
#   -r N, --rounds=N            Rounds for the cheap cases. [default: 20000]
#   -j N, --threads=N           Threads in the load case. [default: two per cpu]
#   -s N, --seconds=N           Seconds per timed case. [default: 10]
#   -c CC, --cc=CC              Compiler to build the probe with. [default: gcc]
#   -k DIR, --keep-binary=DIR   Keep the built probe here instead of discarding it.
#   -t, --terse                 The summary block alone, one key=value per line.
#   -q, --quiet                 Errors only. Only useful with --output.
#   -v, --verbose               Say what is being built and run.
#   -d, --debug                 Trace execution; implies --verbose.
#   -V, --version               Print the version and exit.
#   -h, --help                  Print this message and exit.
#
# Each option is also settable as MEASURE_FS_BASE_<OPTION>, and the option
# wins over the variable.

set -u

prog=measure-fs-base
release='measure-fs-base 1.0'

output=${MEASURE_FS_BASE_OUTPUT:--}
rounds=${MEASURE_FS_BASE_ROUNDS:-20000}
threads=${MEASURE_FS_BASE_THREADS:-}
seconds=${MEASURE_FS_BASE_SECONDS:-10}
cc=${MEASURE_FS_BASE_CC:-gcc}
keep=${MEASURE_FS_BASE_KEEP_BINARY:-}
terse=${MEASURE_FS_BASE_TERSE:-0}
quiet=${MEASURE_FS_BASE_QUIET:-0}
verbose=${MEASURE_FS_BASE_VERBOSE:-0}
debug=${MEASURE_FS_BASE_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)        usage; exit 0 ;;
		-V|--version)     printf '%s\n' "$release"; exit 0 ;;
		-o|--output)      output=${2:-}; shift 2 ;;
		--output=*)       output=${1#*=}; shift ;;
		-r|--rounds)      rounds=${2:-}; shift 2 ;;
		--rounds=*)       rounds=${1#*=}; shift ;;
		-j|--threads)     threads=${2:-}; shift 2 ;;
		--threads=*)      threads=${1#*=}; shift ;;
		-s|--seconds)     seconds=${2:-}; shift 2 ;;
		--seconds=*)      seconds=${1#*=}; shift ;;
		-c|--cc)          cc=${2:-}; shift 2 ;;
		--cc=*)           cc=${1#*=}; shift ;;
		-k|--keep-binary) keep=${2:-}; shift 2 ;;
		--keep-binary=*)  keep=${1#*=}; shift ;;
		-t|--terse)       terse=1; shift ;;
		-q|--quiet)       quiet=1; shift ;;
		-v|--verbose)     verbose=$((verbose + 1)); shift ;;
		-d|--debug)       debug=1; verbose=$((verbose + 1)); shift ;;
		--)               shift; break ;;
		-?*)              printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)                break ;;
	esac
done

[ "$debug" = 1 ] && set -x

case $rounds in ''|*[!0-9]*) die "--rounds wants a number, not ${rounds:-nothing}" ;; esac
case $seconds in ''|*[!0-9]*) die "--seconds wants a number, not ${seconds:-nothing}" ;; esac
if [ -n "$threads" ]; then
	case $threads in ''|*[!0-9]*) die "--threads wants a number, not $threads" ;; esac
fi
command -v "$cc" >/dev/null 2>&1 || die "no compiler named $cc"

here=$(cd "$(dirname "$0")" && pwd) || die "cannot find my own directory"
source=$here/fs-base-probe.c
[ -r "$source" ] || die "no probe source at $source"

scratch=
cleanup() {
	[ -n "$scratch" ] && [ -d "$scratch" ] && rm -rf "$scratch"
}
trap cleanup EXIT
trap 'cleanup; exit 130' INT
trap 'cleanup; exit 143' TERM

if [ -n "$keep" ]; then
	mkdir -p "$keep" || die "cannot make $keep"
	built=$keep/fs-base-probe.exe
else
	scratch=$(mktemp -d) || die "cannot make a scratch directory"
	built=$scratch/fs-base-probe.exe
fi

[ "$verbose" -ge 1 ] && note "building with $cc"
"$cc" -O2 -Wall -Wextra -std=gnu99 -o "$built" "$source" -lpthread ||
	die "the probe did not build"

emit() { printf '%s\n' "$*"; }

header() {
	local kernel windows model cpus
	kernel=$(uname -s -r 2>/dev/null)
	windows=$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*Version \([0-9.]*\).*/\1/p')
	model=$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo 2>/dev/null | head -1)
	cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null)

	emit ''
	emit 'a user-written FS base against the Windows scheduler'
	emit ''
	printf '%-11s %s\n' host "$(hostname)"
	printf '%-11s %s\n' kernel "${kernel:-unknown} $(uname -m)"
	printf '%-11s %s\n' windows "${windows:-unknown}"
	printf '%-11s %s\n' cpu "${model:-unknown}"
	printf '%-11s %s\n' processors "${cpus:-unknown}"
	printf '%-11s %s\n' compiler "$("$cc" --version 2>/dev/null | head -1)"
	printf '%-11s %s\n' date "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf '%-11s %s\n' script "$release"
	emit ''
}

set -- -r "$rounds" -s "$seconds"
[ -n "$threads" ] && set -- "$@" -j "$threads"
[ "$terse" = 1 ] && set -- "$@" --terse
[ "$debug" = 1 ] && set -- "$@" --debug

[ "$verbose" -ge 1 ] && note "running the probe; the timed cases take ${seconds}s each"

run() {
	[ "$terse" = 1 ] || header
	"$built" "$@"
}

if [ "$output" = - ]; then
	run "$@"
	rc=$?
else
	run "$@" > "$output"
	rc=$?
fi

case $rc in
	0) note 'the base survives every case' ;;
	3) note 'the base does not survive; read the case table' ;;
	4) note 'at least one case could not run; the answer is not in yet' ;;
	*) note "the probe exited $rc" ;;
esac

[ -n "$keep" ] && note "the probe is at $built"

exit $rc
