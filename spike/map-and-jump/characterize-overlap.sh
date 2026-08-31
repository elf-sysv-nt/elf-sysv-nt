#!/usr/bin/env bash
#
# How does the host place a mapping over an occupied span?
#
# WP-32 reserved its span with MAP_FIXED and trusted the host to refuse a
# second MAP_FIXED on top of it. Cygwin 3.0.7 refused it; 3.6.10 does not, and
# loader/map's occupied-span control fails there. This builds overlap-probe,
# runs it, and writes a dated transcript with a verdict, so the WP-32 redo is
# grounded on what the host does rather than on what it used to do.
#
# It builds and runs in whatever Cygwin root invokes it and labels the
# transcript with that root's version, because a binary built against one
# root's cygwin1.dll hangs when run from another root's shell. Run it once per
# root and keep both transcripts; the divergence is the pair.
#
# Usage:
#   characterize-overlap.sh [options]
#
# Options:
#   -o FILE, --output=FILE  Transcript destination; - is stdout. [default: -]
#   -k, --keep              Keep the built binary beside the sources.
#   -q, --quiet             Errors only.
#   -v, --verbose           Pass --verbose to the probe.
#   -V, --version           Print the version and exit.
#   -h, --help              Print this message and exit.
#
# Each option is also settable as CHARACTERIZE_OVERLAP_<OPTION>.

set -u

prog=characterize-overlap
release='characterize-overlap 1.0'
here=$(cd "$(dirname "$0")" && pwd)

output=${CHARACTERIZE_OVERLAP_OUTPUT:--}
keep=${CHARACTERIZE_OVERLAP_KEEP:-0}
quiet=${CHARACTERIZE_OVERLAP_QUIET:-0}
verbose=${CHARACTERIZE_OVERLAP_VERBOSE:-0}

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }
note() { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)    usage; exit 0 ;;
		-V|--version) printf '%s\n' "$release"; exit 0 ;;
		-o|--output)  output=${2:-}; shift 2 ;;
		--output=*)   output=${1#*=}; shift ;;
		-k|--keep)    keep=1; shift ;;
		-q|--quiet)   quiet=1; shift ;;
		-v|--verbose) verbose=1; shift ;;
		--)           shift; break ;;
		-?*)          printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)            break ;;
	esac
done
[ $# -eq 0 ] || { printf '%s: takes no arguments, got %s\n' "$prog" "$1" >&2; exit 2; }

command -v gcc >/dev/null 2>&1 || die 'no gcc on PATH'

work=$(mktemp -d "${TMPDIR:-/tmp}/$prog.XXXXXX") || die 'cannot create a working directory'
trap 'rm -rf "$work"' EXIT
trap 'rm -rf "$work"; exit 130' INT TERM
if [ "$keep" = 1 ]; then bin=$here/overlap-probe.exe; else bin=$work/overlap-probe.exe; fi

note 'building the probe'
gcc -std=gnu11 -O1 -Wall -Wextra -o "$bin" \
	"$here/overlap-probe.c" "$here/overlap-winprobe.c" > "$work/build.log" 2>&1 ||
	{ cat "$work/build.log" >&2; die 'the probe did not build'; }

probe_args=
[ "$verbose" = 1 ] && probe_args=--verbose

note 'running the probe'
# shellcheck disable=SC2086
"$bin" $probe_args > "$work/probe.out" 2>"$work/probe.err" ||
	{ cat "$work/probe.err" >&2; die 'the probe did not run'; }

val() { sed -n "s/^$1=//p" "$work/probe.out"; }

q1_allowed=$(val q1_fixed_over_occupied_allowed)
q1_displaced=$(val q1_fixed_displaced_content)
q2_free=$(val q2_bare_hint_honored_when_free)
q3_reloc=$(val q3_bare_hint_relocates_when_busy)
q3_survived=$(val q3_original_survived)
q4_visible=$(val q4_reservation_visible)
q5_refused=$(val q5_win_reserve_refused)
q6_free=$(val q6_fixed_over_free_ok)

# The finding, stated as the fact that broke WP-32 and the ground its redo
# stands on. MAP_FIXED overlaying an occupied span is the regression. The redo
# has a clean path exactly when a bare hint is honored on a free span and
# relocates off an occupied one: then dropping MAP_FIXED and requiring the
# returned address to equal the requested one turns an occupied span away with
# no bookkeeping. A pre-scan of /proc/self/maps is the fallback the visibility
# answer either opens or closes.
finding=inconclusive
if [ "$q1_allowed" = 1 ]; then
	if [ "$q2_free" = 1 ] && [ "$q3_reloc" = 1 ]; then
		finding=bare-hint-discriminates
	elif [ "$q4_visible" = 1 ]; then
		finding=prescan-maps
	elif [ "$q5_refused" = 1 ]; then
		finding=win-reserve-guards
	else
		finding=no-clean-path-found
	fi
elif [ "$q1_allowed" = 0 ]; then
	finding=host-still-refuses
fi

{
	printf 'host mmap placement over an occupied span\n\n'
	printf 'host        %s\n' "$(hostname 2>/dev/null)"
	printf 'cygwin      %s\n' "$(uname -r)"
	printf 'compiler    %s\n' "$(gcc --version | head -1)"
	printf 'date        %s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf 'script      %s\n' "$release"
	printf 'probe       %s\n\n' "$("$bin" --version)"

	printf 'reading, question by question\n\n'
	printf '  q1  MAP_FIXED over an occupied span: %s%s\n' \
		"$([ "$q1_allowed" = 1 ] && printf 'ALLOWED' || { [ "$q1_allowed" = 0 ] && printf 'refused' || printf 'n/a'; })" \
		"$([ "$q1_displaced" = 1 ] && printf ', and the content underneath was displaced')"
	printf '  q2  a bare hint on a free span: %s\n' \
		"$([ "$q2_free" = 1 ] && printf 'honored exactly' || printf 'not honored')"
	printf '  q3  a bare hint on an occupied span: %s%s\n' \
		"$([ "$q3_reloc" = 1 ] && printf 'relocated elsewhere' || printf 'not relocated')" \
		"$([ "$q3_survived" = 1 ] && printf ', original intact')"
	printf '  q4  a live reservation in /proc/self/maps: %s\n' \
		"$([ "$q4_visible" = 1 ] && printf 'visible' || { [ "$q4_visible" = 0 ] && printf 'absent' || printf 'unreadable'; })"
	printf '  q5  VirtualAlloc(MEM_RESERVE) over an occupied span: %s\n' \
		"$([ "$q5_refused" = 1 ] && printf 'refused' || { [ "$q5_refused" = 0 ] && printf 'allowed' || printf 'probe error'; })"
	printf '  q6  control, MAP_FIXED over a free span: %s\n\n' \
		"$([ "$q6_free" = 1 ] && printf 'succeeds' || printf 'FAILED')"

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
