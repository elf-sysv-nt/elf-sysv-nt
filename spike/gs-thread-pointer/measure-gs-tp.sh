#!/usr/bin/env bash
#
# A thread pointer this runtime owns, reached through %gs, measured for
# persistence, addressing, lifecycle and cost against four carriers.
#
# Spike 1 settled that a user-written %fs base does not survive the scheduler.
# %gs is different in kind: the base is NT's, maintained for the TEB, and the
# thread pointer has to be a word fetched out of a structure NT owns rather
# than the base itself. This builds gs-tp-probe.c, runs it, and writes what
# came back -- a per-carrier table, since under AGENTS.md the TLS model is the
# operator's and this script does not pick one.
#
# Nothing is installed and no privilege is wanted. The probe compiles into a
# scratch directory that is removed on the way out.
#
# Usage:
#   measure-gs-tp.sh [options]
#
# Options:
#   -o FILE, --output=FILE      Transcript destination; - is stdout. [default: -]
#   -r N, --rounds=N            Rounds for the cheap cases. [default: 20000]
#   -j N, --threads=N           Threads in the load case. [default: two per cpu]
#   -s N, --seconds=N           Seconds per timed case. [default: 10]
#   -C ID, --carrier=ID         Measure one carrier (C1..C4) instead of all four.
#   -c CC, --cc=CC              Compiler to build the probe with. [default: gcc]
#   -k DIR, --keep-binary=DIR   Keep the built probe here instead of discarding it.
#   -t, --terse                 The summary block alone, one key=value per line.
#   -q, --quiet                 Errors only. Only useful with --output.
#   -v, --verbose               Say what is being built and run.
#   -d, --debug                 Trace execution; implies --verbose.
#   -V, --version               Print the version and exit.
#   -h, --help                  Print this message and exit.
#
# Each option is also settable as MEASURE_GS_TP_<OPTION>, and the option wins
# over the variable.

set -u

prog=measure-gs-tp
release='measure-gs-tp 1.0'

output=${MEASURE_GS_TP_OUTPUT:--}
rounds=${MEASURE_GS_TP_ROUNDS:-20000}
threads=${MEASURE_GS_TP_THREADS:-}
seconds=${MEASURE_GS_TP_SECONDS:-10}
carrier=${MEASURE_GS_TP_CARRIER:-}
cc=${MEASURE_GS_TP_CC:-gcc}
keep=${MEASURE_GS_TP_KEEP_BINARY:-}
terse=${MEASURE_GS_TP_TERSE:-0}
quiet=${MEASURE_GS_TP_QUIET:-0}
verbose=${MEASURE_GS_TP_VERBOSE:-0}
debug=${MEASURE_GS_TP_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

# A usage error is exit 2, per docopt and this project's CLI conventions, and
# distinct from the runtime failures die() reports.
usage_error() { printf '%s: %s\n' "$prog" "$*" >&2; exit 2; }

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
		-C|--carrier)     carrier=${2:-}; shift 2 ;;
		--carrier=*)      carrier=${1#*=}; shift ;;
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

case $rounds in ''|*[!0-9]*) usage_error "--rounds wants a number, not ${rounds:-nothing}" ;; esac
case $seconds in ''|*[!0-9]*) usage_error "--seconds wants a number, not ${seconds:-nothing}" ;; esac
if [ -n "$threads" ]; then
	case $threads in ''|*[!0-9]*) usage_error "--threads wants a number, not $threads" ;; esac
fi
if [ -n "$carrier" ]; then
	case $carrier in
		C1|C2|C3|C4|c1|c2|c3|c4) ;;
		*) usage_error "--carrier wants one of C1 C2 C3 C4, not $carrier" ;;
	esac
fi
command -v "$cc" >/dev/null 2>&1 || die "no compiler named $cc"

here=$(cd "$(dirname "$0")" && pwd) || die "cannot find my own directory"
source=$here/gs-tp-probe.c
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
	built=$keep/gs-tp-probe.exe
else
	scratch=$(mktemp -d) || die "cannot make a scratch directory"
	built=$scratch/gs-tp-probe.exe
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
	emit 'a runtime-owned thread pointer through %gs, four carriers side by side'
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
[ -n "$carrier" ] && set -- "$@" -C "$carrier"
[ "$terse" = 1 ] && set -- "$@" --terse
[ "$debug" = 1 ] && set -- "$@" --debug

[ "$verbose" -ge 1 ] && note "running the probe; the timed cases take ${seconds}s each per carrier"

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
	0) note 'every available carrier passes; read the per-carrier table' ;;
	3) note 'a carrier fails; read the table for which case broke it' ;;
	4) note 'at least one case could not run; the answer is not in yet' ;;
	*) note "the probe exited $rc" ;;
esac

[ -n "$keep" ] && note "the probe is at $built"

exit $rc
