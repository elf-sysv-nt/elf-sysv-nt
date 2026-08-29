#!/usr/bin/env bash
#
# Classify t/sample-dump.txt and compare the summary against counts worked out
# by hand, then check the fetcher's refusals. Run this after touching either
# script. Nothing here touches the network.
#
# A classifier that quietly stops recognizing one record shape does not fail.
# It reports a smaller number, and a smaller number is the one outcome this
# whole measurement exists to avoid believing.

set -u

here=$(cd "$(dirname "$0")" && pwd)
script=$here/../count-vendor-misses.sh
fetcher=$here/../fetch-host-tests.sh

[ -x "$script" ] || { echo "not executable: $script" >&2; exit 1; }
[ -x "$fetcher" ] || { echo "not executable: $fetcher" >&2; exit 1; }

fails=0

# Exit status and message together. A refusal that exits 2 with the wrong
# explanation is a defect a status-only check waves through.
refuses() {
	want=$1 pattern=$2; shift 2
	out=$("$@" 2>&1); st=$?
	if [ "$st" != "$want" ]; then
		printf 'FAIL, expected exit %s and got %s from: %s\n' "$want" "$st" "$*" >&2
		fails=$((fails + 1))
		return
	fi
	case $out in
		*"$pattern"*) ;;
		*) printf 'FAIL, no mention of %s in: %s\n' "$pattern" "$out" >&2
			fails=$((fails + 1)) ;;
	esac
}

got=$(mktemp "${TMPDIR:-/tmp}/run-tests.XXXXXX")
trap 'rm -f "$got"' EXIT

"$script" --dump "$here/sample-dump.txt" --terse --quiet > "$got" || {
	echo 'the classifier exited nonzero' >&2
	exit 1
}

if diff -u "$here/expected-summary.txt" "$got"; then
	echo 'ok, summary matches the hand-worked counts'
else
	echo 'FAIL, summary drifted from the hand-worked counts' >&2
	fails=$((fails + 1))
fi

# The literal grep's exclusion list, against a tree of six packages: one
# carrying a real host test, five carrying the shapes measured as noise on
# 2026-08-29. Each noise package is covered by exactly one exclusion, so
# dropping any single rule takes the count from one to two. Checked that way,
# rule by rule, the day it was written.
lit=$("$script" --root "$here/lit-noise" --terse --quiet 2>/dev/null)
for want in packages_seen=6 packages_literal_vendor=1; do
	case $lit in
		*"$want"*) ;;
		*) printf 'FAIL, expected %s from the lit-noise tree\n' "$want" >&2
			fails=$((fails + 1)) ;;
	esac
done

refuses 2 'no destination' "$fetcher"
refuses 2 'wants all or host-tests' "$fetcher" --dest /nonexistent --extract sideways
refuses 2 'wants a number' "$fetcher" --dest /nonexistent --limit twelve
refuses 2 'wants a number' "$fetcher" --dest /nonexistent --jobs some
refuses 2 'at least 1' "$fetcher" --dest /nonexistent --jobs 0
refuses 2 'unknown option' "$fetcher" --dest /nonexistent --frobnicate

# The lock. Two runs against one destination race on the markers, and the
# start-up reap of stale working directories would delete a live run's own.
# Both were live defects until 2026-08-29, when two instances overlapped and
# one skipped a package the other had just finished.
den=$(mktemp -d "${TMPDIR:-/tmp}/run-tests-lock.XXXXXX")
sleep 60 &
holder=$!
printf '%s\n' "$holder" > "$den/lock"
refuses 1 'another run holds' "$fetcher" --dest "$den" --dry-run
kill "$holder" 2>/dev/null
wait "$holder" 2>/dev/null

# A lock naming a pid that is gone is stale, and taking it is the point of
# checking rather than merely testing for the file. The repository below does
# not exist, so the run dies at its first fetch; what is under test is that it
# got past the lock to reach one. A file: URL rather than an unreachable host,
# because a dropped connection costs curl its connect timeout and this check
# is not about waiting.
printf '%s\n' 999999 > "$den/lock"
out=$("$fetcher" --dest "$den" --dry-run --repo file:///nonexistent-el8-repo 2>&1)
case $out in
	*'another run holds'*)
		echo 'FAIL, a stale lock was treated as live' >&2
		fails=$((fails + 1)) ;;
esac
rm -rf "$den"

if [ "$fails" -eq 0 ]; then
	echo 'ok, the fetcher refuses what it should'
else
	printf '%s checks failed\n' "$fails" >&2
	exit 1
fi
