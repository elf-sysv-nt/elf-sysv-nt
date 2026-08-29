#!/usr/bin/env bash
#
# What does an access through a zeroed %fs base do, and can a handler resume?
#
# Spike 1 found the base zero after anything that deschedules the thread and
# stopped there. This asks what the next instruction does, because that decides
# whether a load-time TLS rewriter for vendor binaries may be a heuristic over
# a sound fallback or has to be exhaustive over nothing.
#
# Nothing is installed and no privilege is wanted. The probe compiles into a
# scratch directory that is removed on the way out.
#
# Usage:
#   measure-fs-base-fault.sh [options]
#
# Options:
#   -o FILE, --output=FILE      Transcript destination; - is stdout. [default: -]
#   -r N, --rounds=N            Rounds in the cost case. [default: 200000]
#   -j N, --threads=N           Threads in the concurrency case. [default: two per cpu]
#   -s N, --seconds=N           Seconds per timed case. [default: 5]
#   -c CC, --cc=CC              Compiler to build the probe with. [default: gcc]
#   -k DIR, --keep-binary=DIR   Keep the built probe here instead of discarding it.
#   -t, --terse                 The summary block alone, one key=value per line.
#   -q, --quiet                 Errors only. Only useful with --output.
#   -v, --verbose               Say what is being built and run.
#   -d, --debug                 Trace execution; implies --verbose.
#   -V, --version               Print the version and exit.
#   -h, --help                  Print this message and exit.
#
# Each option is also settable as MEASURE_FS_FAULT_<OPTION>, and the option
# wins over the variable.

set -u

prog=measure-fs-base-fault
release='measure-fs-base-fault 1.0'

output=${MEASURE_FS_FAULT_OUTPUT:--}
rounds=${MEASURE_FS_FAULT_ROUNDS:-200000}
threads=${MEASURE_FS_FAULT_THREADS:-}
seconds=${MEASURE_FS_FAULT_SECONDS:-5}
cc=${MEASURE_FS_FAULT_CC:-gcc}
keep=${MEASURE_FS_FAULT_KEEP_BINARY:-}
terse=${MEASURE_FS_FAULT_TERSE:-0}
quiet=${MEASURE_FS_FAULT_QUIET:-0}
verbose=${MEASURE_FS_FAULT_VERBOSE:-0}
debug=${MEASURE_FS_FAULT_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

# Separate from die on purpose: a usage error is a 2 under this project's
# command-line convention, and a failure to run is a 1.
argh() { printf '%s: %s\n' "$prog" "$*" >&2; exit 2; }

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

[ $# -eq 0 ] || { printf '%s: no positional arguments\n' "$prog" >&2; exit 2; }

[ "$debug" = 1 ] && set -x

case $rounds in ''|*[!0-9]*) argh "--rounds wants a number, not ${rounds:-nothing}" ;; esac
case $seconds in ''|*[!0-9]*) argh "--seconds wants a number, not ${seconds:-nothing}" ;; esac
[ "$rounds" -gt 0 ] || argh '--rounds wants a positive number'
[ "$seconds" -gt 0 ] || argh '--seconds wants a positive number'
if [ -n "$threads" ]; then
	case $threads in ''|*[!0-9]*) argh "--threads wants a number, not $threads" ;; esac
	[ "$threads" -gt 0 ] || argh '--threads wants a positive number'
fi
command -v "$cc" >/dev/null 2>&1 || die "no compiler named $cc"

here=$(cd "$(dirname "$0")" && pwd) || die 'cannot find my own directory'
source=$here/fs-fault-probe.c
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
	built=$keep/fs-fault-probe.exe
else
	scratch=$(mktemp -d) || die 'cannot make a scratch directory'
	built=$scratch/fs-fault-probe.exe
fi

# -mno-red-zone because this project's policy is that nothing relies on the
# reserved 128 bytes, and a probe that resumes threads out of a vectored
# handler is the last place to make an exception. -O1 rather than -O2: the
# instruction forms under test are written in asm and their surroundings should
# stay recognisable in a disassembly when a result wants checking by hand.
[ "$verbose" -ge 1 ] && note "building with $cc"
"$cc" -O1 -Wall -Wextra -std=gnu99 -mno-red-zone -o "$built" "$source" -lpthread ||
	die 'the probe did not build'

emit() { printf '%s\n' "$*"; }

header() {
	local kernel windows model cpus
	kernel=$(uname -s -r 2>/dev/null)
	windows=$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*Version \([0-9.]*\).*/\1/p')
	model=$(sed -n 's/^model name[[:space:]]*: //p' /proc/cpuinfo 2>/dev/null | head -1)
	cpus=$(getconf _NPROCESSORS_ONLN 2>/dev/null)

	emit ''
	emit 'an access through a zeroed FS base, and a handler over it'
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
	0) note 'the access faults and the handler resumes from it' ;;
	3) note 'the handler cannot carry every case; read the table' ;;
	4) note 'at least one case could not run; the answer is not in yet' ;;
	*) note "the probe exited $rc" ;;
esac

[ -n "$keep" ] && note "the probe is at $built"

exit $rc
