#!/usr/bin/env bash
#
# The merge gate. Runs every suite registered in suites.txt and fails if
# any of them fails. The pre-merge hook in ci/hooks runs this before a
# merge commit is created, so a failing suite -- a new fuzz crash first
# among them -- blocks the merge. That is WP-T1's done-when, and this
# script is the whole of what "CI" means in this repository: there is no
# hosted service, the repository lives on one machine, and merges happen
# here or not at all.
#
# The gate insists on the pinned verification root (Cygwin 3.0.7, the
# rhel root) because a pass anywhere else certifies nothing. The gate's
# own tests relax that with --allow-unpinned.
#
# Usage:
#   gate.sh [options]
#
# Options:
#   -n N, --count=N     Fuzz cases per suite. [default: 2000000]
#   --suites=FILE       Suite registry. [default: ci/suites.txt]
#   --allow-unpinned    Run even when this is not the pinned root.
#   -q, --quiet         Errors only.
#   -h, --help          Print this message and exit.
#
# Environment:
#   GATE_COUNT, GATE_SUITES, GATE_ALLOW_UNPINNED mirror the options;
#   an option given on the command line wins over its variable.
#
# Exit: 0 all suites passed, 1 a suite failed, 2 usage or wrong root.

set -u

prog=gate
here=$(cd "$(dirname "$0")" && pwd)
top=$(cd "$here/.." && pwd)

count=${GATE_COUNT:-2000000}
suites=${GATE_SUITES:-$top/ci/suites.txt}
allow_unpinned=${GATE_ALLOW_UNPINNED:-0}
quiet=0

usage() { awk '/^# Usage:/,/^[^#]/ { if ($0 ~ /^#/) print substr($0, 3) }' "$0"; }

while [ $# -gt 0 ]; do
	case $1 in
		-h|--help)         usage; exit 0 ;;
		-n|--count)        count=${2:-}; shift 2 ;;
		--count=*)         count=${1#*=}; shift ;;
		--suites)          suites=${2:-}; shift 2 ;;
		--suites=*)        suites=${1#*=}; shift ;;
		--allow-unpinned)  allow_unpinned=1; shift ;;
		-q|--quiet)        quiet=1; shift ;;
		*) printf '%s: unknown argument %s\n' "$prog" "$1" >&2; exit 2 ;;
	esac
done

release=$(uname -r)
case $release in
	3.0.7*) ;;
	*)
		if [ "$allow_unpinned" != 1 ]; then
			printf '%s: this is Cygwin %s, not the pinned 3.0.7 root a pass certifies against.\n' "$prog" "$release" >&2
			printf '%s: run the merge from the rhel root, or bypass deliberately with git merge --no-verify.\n' "$prog" >&2
			exit 2
		fi
		[ "$quiet" = 1 ] || printf '%s: warning: unpinned root (Cygwin %s); this run certifies nothing.\n' "$prog" "$release"
		;;
esac

[ -f "$suites" ] || { printf '%s: no suite registry at %s\n' "$prog" "$suites" >&2; exit 2; }

qflag=
[ "$quiet" = 1 ] && qflag=-q

rc=0
ran=0
while IFS= read -r line; do
	case $line in ''|'#'*) continue ;; esac
	suite=$top/$line
	[ -x "$suite" ] || { printf '%s: registered suite %s is missing or not executable\n' "$prog" "$line" >&2; rc=1; continue; }
	[ "$quiet" = 1 ] || printf '%s: running %s (n=%s)\n' "$prog" "$line" "$count"
	if "$suite" -n "$count" $qflag; then
		ran=$((ran+1))
	else
		printf '%s: FAILED: %s\n' "$prog" "$line" >&2
		rc=1
	fi
done < "$suites"

if [ "$rc" = 0 ] && [ "$ran" = 0 ]; then
	printf '%s: the registry lists no suites; an empty gate blocks nothing\n' "$prog" >&2
	rc=1
fi

if [ "$rc" = 0 ]; then
	[ "$quiet" = 1 ] || printf '%s: all %d suite(s) passed\n' "$prog" "$ran"
else
	printf '%s: gate failed; the merge is blocked\n' "$prog" >&2
fi
exit $rc
