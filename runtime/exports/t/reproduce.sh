#!/usr/bin/env bash
#
# WP-20's certification: the committed export inventory reproduces from source.
#
# Pins the newlib-cygwin checkout to the ref DR-0007 names, reruns the
# extractor, and diffs its output against the committed cygwin-exports.tsv. A
# byte of difference, or a checkout that has moved off the ref, fails. This is
# the guarantee WP-21 and WP-51 lean on: the list they read is the list the
# source produces, not a snapshot that has quietly aged.
#
# Usage:
#   reproduce.sh [options]
#
# Options:
#   --din-repo=DIR   newlib-cygwin checkout. [default: /c/-/repo/newlib-cygwin]
#   --any-ref        Do not require the pinned ref (for a deliberate re-cut).
#   -q, --quiet      Errors only.
#   -h, --help       Print this message and exit.
#
# Exit: 0 reproduces, 1 differs or the ref is wrong, 2 usage.

set -u

prog=reproduce
here=$(cd "$(dirname "$0")" && pwd)
exports=$(cd "$here/.." && pwd)

pinned=b11613e477c006b2ce0332463ed07f1118260e79
din_repo=/c/-/repo/newlib-cygwin
any_ref=0
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }
say()  { [ "$quiet" = 1 ] || printf '%s: %s\n' "$prog" "$*" >&2; }
fail() { printf '%s: %s\n' "$prog" "$*" >&2; exit 1; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)   usage; exit 0 ;;
		--din-repo)  din_repo=${2:-}; shift 2 ;;
		--din-repo=*) din_repo=${1#*=}; shift ;;
		--any-ref)   any_ref=1; shift ;;
		-q|--quiet)  quiet=1; shift ;;
		--)          shift; break ;;
		-?*)         printf '%s: unknown option %s\n' "$prog" "$1" >&2; exit 2 ;;
		*)           break ;;
	esac
done

committed=$exports/cygwin-exports.tsv
extractor=$exports/extract-exports.sh
din=$din_repo/winsup/cygwin/cygwin.din

[ -f "$committed" ] || fail "no committed inventory at $committed"
[ -x "$extractor" ] || fail "no extractor at $extractor"
[ -f "$din" ]       || fail "no cygwin.din at $din (is $din_repo checked out?)"

if [ "$any_ref" = 0 ]; then
	have=$(git -C "$din_repo" rev-parse HEAD 2>/dev/null)
	[ "$have" = "$pinned" ] || fail "newlib-cygwin is at ${have:-unknown}, not the pinned $pinned (--any-ref to override)"
fi

tmp=$(mktemp "${TMPDIR:-/tmp}/$prog.XXXXXX") || fail 'no temp file'
trap 'rm -f "$tmp"' EXIT

"$extractor" --din "$din" -o "$tmp" || fail 'the extractor failed'

if diff -u "$committed" "$tmp" >/dev/null 2>&1; then
	say "reproduces: $(wc -l < "$committed" | tr -d ' ') rows, identical to source at $pinned"
	exit 0
else
	printf '%s: committed inventory differs from a fresh extraction:\n' "$prog" >&2
	diff -u "$committed" "$tmp" >&2 | head -40
	exit 1
fi
