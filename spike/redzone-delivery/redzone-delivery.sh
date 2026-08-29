#!/usr/bin/env bash
#
# Can a signal-delivery path reserve the red zone before it builds the
# handler's frame, so a sysv_abi leaf keeps its 128 bytes across a delivery,
# and does the handler still run and the interrupted computation still finish?
#
# The sequel to spike 3, which found Cygwin's delivery destroying the red zone
# at %rsp-8. This drives delivery itself, from the same thread hijack spike 3
# used, and builds the handler frame two ways: at the interrupted %rsp, which
# has to clobber, and at %rsp-128, which must not. It measures a model of
# delivery rather than Cygwin's real sigdelayed; WP-43 re-measures the real
# path.
#
# The probe this drives is two files, redzone.c and redzone.S. It builds them,
# runs the seven cases, and writes the transcript. If a source is missing the
# script says so and stops rather than pretending to a verdict. See README.md
# for the mechanism and the cases.
#
# Usage:
#   redzone-delivery.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -w DIR, --work=DIR      Keep the build here.
#   -c CASE, --case=CASE    Run one case by name rather than all seven.
#   -r N, --rounds=N        Passes for the quiet case. [default: 200000]
#   -e N, --events=N        Deliveries per measured case. [default: 2000]
#   -z N, --depth=N         Bytes below %rsp to watch. [default: 1024]
#   -T N, --timeout=N       Seconds the probe may take. [default: 600]
#   -k, --keep              Do not delete the working directory.
#   -t, --terse             The summary block alone, one key=value per line.
#   -q, --quiet             Errors only. Only useful with --output.
#   -v, --verbose           Name each case on stderr as it starts.
#   -d, --debug             Trace execution; implies --verbose.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as REDZONE_DELIVERY_<OPTION>, and the option
# wins over the variable.

set -u

prog=redzone-delivery
release='redzone-delivery 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${REDZONE_DELIVERY_OUTPUT:--}
work=${REDZONE_DELIVERY_WORK:-}
only=${REDZONE_DELIVERY_CASE:-}
rounds=${REDZONE_DELIVERY_ROUNDS:-200000}
events=${REDZONE_DELIVERY_EVENTS:-2000}
depth=${REDZONE_DELIVERY_DEPTH:-1024}
timeout_s=${REDZONE_DELIVERY_TIMEOUT:-600}
keep=${REDZONE_DELIVERY_KEEP:-0}
terse=${REDZONE_DELIVERY_TERSE:-0}
quiet=${REDZONE_DELIVERY_QUIET:-0}
verbose=${REDZONE_DELIVERY_VERBOSE:-0}
debug=${REDZONE_DELIVERY_DEBUG:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-w|--work)    work=${2:-}; shift 2 ;;
		--work=*)     work=${1#*=}; shift ;;
		-c|--case)    only=${2:-}; shift 2 ;;
		--case=*)     only=${1#*=}; shift ;;
		-r|--rounds)  rounds=${2:-}; shift 2 ;;
		--rounds=*)   rounds=${1#*=}; shift ;;
		-e|--events)  events=${2:-}; shift 2 ;;
		--events=*)   events=${1#*=}; shift ;;
		-z|--depth)   depth=${2:-}; shift 2 ;;
		--depth=*)    depth=${1#*=}; shift ;;
		-T|--timeout) timeout_s=${2:-}; shift 2 ;;
		--timeout=*)  timeout_s=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-t|--terse)   terse=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-v|--verbose) verbose=$((verbose + 1)); shift ;;
		-d|--debug)   debug=1; verbose=$((verbose + 1)); shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done

[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

for pair in "timeout $timeout_s" "rounds $rounds" "events $events" "depth $depth"; do
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
[ $((depth % 8)) -eq 0 ] || {
	printf '%s: --depth wants a multiple of 8, got %s\n' "$prog" "$depth" >&2
	exit 2
}

[ "$debug" = 1 ] && set -x

for tool in gcc; do
	command -v "$tool" >/dev/null 2>&1 || die "no $tool on PATH"
done

# The probe is two files, as in spike 3: redzone.c holds the delivery driver
# and the seven cases, redzone.S holds the no-call watchers and the handler
# stub whose frame the driver places by hand. If either is missing the script
# stops here, loudly, rather than pretending to a verdict.
sources="$here/redzone.c $here/redzone.S"
missing=
for src in $sources; do
	[ -f "$src" ] || missing="$missing ${src##*/}"
done
if [ -n "$missing" ]; then
	printf '%s: probe sources missing:%s\n' "$prog" "$missing" >&2
	printf '%s: see README.md for the design.\n' "$prog" >&2
	exit 3
fi

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

probe=$work/redzone.exe

note 'building the probe'
gcc -O1 -g -Wall -Wextra -o "$probe" "$here/redzone.c" "$here/redzone.S" \
	> "$work/build.log" 2>&1 || {
	cat "$work/build.log" >&2
	die 'the probe did not build'
}
[ -s "$work/build.log" ] && cat "$work/build.log" >&2

note 'running the probe'
set -- --rounds "$rounds" --events "$events" --depth "$depth"
[ -n "$only" ] && set -- "$@" --case "$only"
[ "$verbose" -gt 0 ] && set -- "$@" --debug
[ "$terse" = 1 ] && set -- "$@" --terse

timeout "$timeout_s" "$probe" "$@" > "$work/body" 2> "$work/probe.err"
st=$?
[ -s "$work/probe.err" ] && cat "$work/probe.err" >&2
if [ $st -eq 124 ]; then
	die "the probe did not finish inside $timeout_s seconds"
fi
if [ $st -eq 2 ]; then
	die 'the probe refused its arguments'
fi
[ -s "$work/body" ] || die 'the probe wrote nothing'

verdict=$(sed -n 's/^ *verdict=//p' "$work/body")
[ -n "$verdict" ] || die 'the probe reported no verdict'

if [ "$terse" = 1 ]; then
	cat "$work/body" > "$work/report"
else
	{
		printf 'reserving the red zone before the handler frame\n\n'
		printf 'host        %s\n' "$(hostname 2>/dev/null)"
		printf 'kernel      %s\n' "$(uname -srm)"
		printf 'processors  %s\n' "$(getconf _NPROCESSORS_ONLN 2>/dev/null)"
		printf 'compiler    %s\n' "$(gcc --version | head -1)"
		printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf 'script      %s\n' "$release"
		printf 'probe       %s\n' "$("$probe" --version)"
		printf 'rounds      %s passes, %s deliveries, %s bytes watched\n\n' \
			"$rounds" "$events" "$depth"
		cat "$work/body"
	} > "$work/report"
fi

if [ "$output" = - ]; then
	cat "$work/report"
else
	cat "$work/report" > "$output" || die "cannot write $output"
	note "transcript written to $output"
fi

[ "$verdict" = yes ] && [ $st -eq 0 ]
