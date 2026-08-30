#!/usr/bin/env bash
#
# WP-30's re-measurement of the real Cygwin _my_tls, against the running kernel
# rather than the spike/gs-thread-pointer stand-in. DR-0003 named two things to
# re-measure -- the padding constant, and the behaviour when Cygwin moves a
# thread onto an alternate signal stack -- and this driver builds and runs the
# three probes that take them, writing a transcript in the shape a decision
# record quotes.
#
#   remeasure-my-tls  the real CYGTLS_PADSIZE, the geometry below StackBase on
#                     the main thread, a fresh thread and a fork child, and
#                     whether NtTib.StackBase moves on the alternate signal stack
#   map-cygtls        which words of the _cygtls reservation Cygwin uses, so the
#                     carrier can be placed in genuine pad or, where it cannot,
#                     below the reservation entirely
#   owned-stack       that a runtime-allocated stack makes StackBase reflect the
#                     allocation, that its floor is a safe owned carrier slot,
#                     and that fork works from such a thread
#
# Counts belong to the machine and the minute; the shape is what travels. Run it
# from the pinned root, since the answer belongs to the running Windows kernel.
#
# Usage:
#   measure.sh [options]
#
# Options:
#   -o FILE, --output=FILE   Transcript destination; - is stdout. [default: -]
#   -c CC, --cc=CC           Compiler to build the probes with. [default: gcc]
#   -q, --quiet              Errors only.
#   -h, --help               Print this message and exit.

set -u

prog=measure
here=$(cd "$(dirname "$0")" && pwd)
output=-
cc=gcc
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
die() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)     usage; exit 0 ;;
		-o|--output)   output=${2:-}; shift 2 ;;
		--output=*)    output=${1#*=}; shift ;;
		-c|--cc)       cc=${2:-}; shift 2 ;;
		--cc=*)        cc=${1#*=}; shift ;;
		-q|--quiet)    quiet=1; shift ;;
		--)            shift; break ;;
		-?*)           printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)             break ;;
	esac
done
command -v "$cc" >/dev/null 2>&1 || die "no compiler named $cc"

scratch=$(mktemp -d "${TMPDIR:-/tmp}/wp30m.XXXXXX") || die "no scratch dir"
trap 'rm -rf "$scratch"' EXIT

for p in remeasure-my-tls map-cygtls owned-stack; do
	[ "$quiet" = 1 ] || printf '%s: building %s\n' "$prog" "$p" >&2
	$cc -O2 -std=gnu11 -Wall -Wextra -o "$scratch/$p.exe" "$here/$p.c" -lpthread ||
		die "$p did not build"
done

emit() {
	printf '\n'
	printf 'WP-30 re-measurement of the real Cygwin _my_tls\n\n'
	printf '%-11s %s\n' host "$(hostname)"
	printf '%-11s %s\n' kernel "$(uname -s -r -m 2>/dev/null)"
	printf '%-11s %s\n' windows "$(cmd /c ver 2>/dev/null | tr -d '\r' | sed -n 's/.*Version \([0-9.]*\).*/\1/p')"
	printf '%-11s %s\n' compiler "$("$cc" --version 2>/dev/null | head -1)"
	printf '%-11s %s\n' date "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
	printf '\n--- remeasure-my-tls: padding constant, geometry, altstack ---\n'
	"$scratch/remeasure-my-tls.exe"
	printf '\n--- map-cygtls: used and unused words of the reservation ---\n'
	"$scratch/map-cygtls.exe"
	printf '\n--- owned-stack: a runtime-owned stack carries the word safely ---\n'
	"$scratch/owned-stack.exe"
}

if [ "$output" = - ]; then emit; else emit > "$output"; fi
[ "$quiet" = 1 ] || printf '%s: done\n' "$prog" >&2
